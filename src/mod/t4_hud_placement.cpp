#include "t4_hud_placement.hpp"
#include "virtual_query_timing.hpp"
#include "resident_page_access.hpp"

#include "hud_placement_logic.hpp"
#include "stereo_scene_hook.hpp"
#include "stereo_diagnostics.hpp"
#include "t4_presentation_state.hpp"

#include "t4/profile.hpp"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wawvr::mod {

#if defined(_MSC_VER) && defined(_M_IX86)
// Exact T4 ScrPlace_SetupFloatViewport contract at preferred VA 0x0047A1C0:
// EDI is ScreenPlacement*, followed by caller-owned x/y/width/height floats on
// the stack. Preserve the C++ caller's EDI around that proprietary register
// ABI and leave every other callee-saved register to the validated target.
extern "C" __declspec(naked) void __cdecl
wawvr_call_t4_scr_place_setup_float_viewport(
    std::uintptr_t, void*, float, float, float, float) noexcept {
    __asm {
        push edi
        mov eax, dword ptr [esp + 8]
        mov edi, dword ptr [esp + 12]
        push dword ptr [esp + 28]
        push dword ptr [esp + 28]
        push dword ptr [esp + 28]
        push dword ptr [esp + 28]
        call eax
        add esp, 16
        pop edi
        ret
    }
}
#endif

namespace {

struct T4RefdefViewport final {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};
};

static_assert(sizeof(T4RefdefViewport) == 0x10);

enum class PlacementServicePhase : std::uint8_t {
    unbound,
    active,
    quiesce_requested,
    quiesced,
    disabled,
};

class ExclusivePlacementLock final {
public:
    ExclusivePlacementLock() noexcept { AcquireSRWLockExclusive(&g_lock); }
    ~ExclusivePlacementLock() { ReleaseSRWLockExclusive(&g_lock); }
    ExclusivePlacementLock(const ExclusivePlacementLock&) = delete;
    ExclusivePlacementLock& operator=(const ExclusivePlacementLock&) = delete;

private:
    static SRWLOCK g_lock;
};

SRWLOCK ExclusivePlacementLock::g_lock = SRWLOCK_INIT;

std::uintptr_t g_setup_function_address = 0;
std::uintptr_t g_placement_address = 0;
std::uintptr_t g_refdef_address = 0;
const wawvr::t4::HookSite* g_setup_site = nullptr;
HANDLE g_quiesce_event = nullptr;
DWORD g_game_thread_id = 0;
std::int32_t g_last_valid_full_width = 0;
std::int32_t g_last_valid_full_height = 0;
bool g_services_enabled = false;
bool g_ever_applied = false;
PlacementServicePhase g_phase = PlacementServicePhase::unbound;
T4HudPlacementQuiesceResult g_quiesce_result{};

[[nodiscard]] bool protection_is_readable(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffU;
    return access == PAGE_READONLY || access == PAGE_READWRITE ||
           access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool protection_is_writable(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffU;
    return access == PAGE_READWRITE || access == PAGE_WRITECOPY ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool protection_is_executable(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffU;
    return access == PAGE_EXECUTE || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool accessible_range(
    const std::uintptr_t address,
    const std::size_t size,
    const bool require_writable,
    const bool require_executable) noexcept {
    if (!require_executable) {
        return resident_page_access(reinterpret_cast<const void*>(address),
            size, require_writable,
            &grouped_virtual_query<VirtualQueryTimingGroup::hud>);
    }
    if (address == 0 || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (grouped_virtual_query<VirtualQueryTimingGroup::hud>(
            reinterpret_cast<const void*>(address), &memory,
            sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        !protection_is_readable(memory.Protect) ||
        (require_writable && !protection_is_writable(memory.Protect)) ||
        (require_executable && !protection_is_executable(memory.Protect))) {
        return false;
    }
    const auto region_begin =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto region_end = region_begin + memory.RegionSize;
    return address >= region_begin && address <= region_end &&
           size <= region_end - address;
}

[[nodiscard]] bool setup_sentinel_still_matches(
    const std::uintptr_t function,
    const wawvr::t4::HookSite* const site) noexcept {
    return site != nullptr && site->expected_size != 0 &&
           accessible_range(function, site->expected_size, false, true) &&
           std::memcmp(
               reinterpret_cast<const void*>(function),
               site->expected.data(), site->expected_size) == 0;
}

[[nodiscard]] bool valid_full_dimensions(
    const std::int32_t width, const std::int32_t height) noexcept {
    return width > 0 && height > 0 &&
           width <= kHudMaximumViewportDimension &&
           height <= kHudMaximumViewportDimension;
}

void clear_bindings_locked() noexcept {
    g_services_enabled = false;
    g_phase = PlacementServicePhase::unbound;
    g_setup_function_address = 0;
    g_placement_address = 0;
    g_refdef_address = 0;
    g_setup_site = nullptr;
    g_game_thread_id = 0;
    g_last_valid_full_width = 0;
    g_last_valid_full_height = 0;
    g_ever_applied = false;
    g_quiesce_result = {
        .status = T4HudPlacementQuiesceStatus::not_bound,
        .apply_status = T4HudPlacementApplyStatus::not_bound,
        .wait_result = WAIT_OBJECT_0,
    };
    if (g_quiesce_event != nullptr) {
        ResetEvent(g_quiesce_event);
    }
}

[[nodiscard]] T4HudPlacementApplyStatus apply_placement_locked(
    const bool force_full_frame) noexcept {
    if (g_setup_function_address == 0) {
        return T4HudPlacementApplyStatus::not_bound;
    }
    if (!setup_sentinel_still_matches(
            g_setup_function_address, g_setup_site)) {
        return T4HudPlacementApplyStatus::sentinel_changed;
    }
    if (!accessible_range(g_placement_address, 0x48, true, false)) {
        return T4HudPlacementApplyStatus::memory_unavailable;
    }

    T4RefdefViewport viewport{};
    const bool refdef_readable = accessible_range(
        g_refdef_address, sizeof(T4RefdefViewport), false, false);
    if (refdef_readable) {
        std::memcpy(
            &viewport, reinterpret_cast<const void*>(g_refdef_address),
            sizeof(viewport));
    }
    bool used_cached_dimensions = false;
    if (refdef_readable &&
        valid_full_dimensions(viewport.width, viewport.height)) {
        g_last_valid_full_width = viewport.width;
        g_last_valid_full_height = viewport.height;
    } else if (force_full_frame &&
               valid_full_dimensions(
                   g_last_valid_full_width, g_last_valid_full_height)) {
        viewport.width = g_last_valid_full_width;
        viewport.height = g_last_valid_full_height;
        used_cached_dimensions = true;
    } else {
        return refdef_readable
                   ? T4HudPlacementApplyStatus::invalid_dimensions
                   : T4HudPlacementApplyStatus::memory_unavailable;
    }

    const T4PresentationState presentation =
        force_full_frame ? T4PresentationState{}
                         : read_t4_presentation_state();
    const bool stereo_packing_ready =
        !force_full_frame && stereo_scene_ready_for_current_com_frame();
    const HudPlacementPlan plan = plan_hud_placement({
        .presentation_state_valid = presentation.valid,
        .stereo_packing_ready = stereo_packing_ready,
        .connection_state = presentation.connection_state,
        .active_connection_state = presentation.active_connection_state,
        .key_catchers = presentation.key_catchers,
        .full_width = viewport.width,
        .full_height = viewport.height,
        .scope_reserved_width = stereo_packing_ready
            ? stereo_scene_scope_reserved_width(viewport.width, viewport.height)
            : 0,
    });
    if (!plan.valid) {
        return T4HudPlacementApplyStatus::invalid_dimensions;
    }
    if (plan.mode == HudPlacementMode::packed_eye &&
        plan.width < viewport.width / 2) {
        WAWVR_STEREO_DIAG_ONCE(
            "ScopeHudDiag native HUD packed into normal eye %dx%d; optical strip excluded from %dx%d source",
            plan.width, plan.height, viewport.width, viewport.height);
    }

#if defined(_MSC_VER) && defined(_M_IX86)
    wawvr_call_t4_scr_place_setup_float_viewport(
        g_setup_function_address,
        reinterpret_cast<void*>(g_placement_address), 0.0F, 0.0F,
        static_cast<float>(plan.width), static_cast<float>(plan.height));
    if (plan.mode == HudPlacementMode::packed_eye) {
        HudScreenPlacement placement{};
        std::memcpy(
            &placement, reinterpret_cast<const void*>(g_placement_address),
            sizeof(placement));
        if (!apply_packed_hud_comfort(
                &placement, plan.width, plan.height)) {
            return T4HudPlacementApplyStatus::invalid_dimensions;
        }
        std::memcpy(
            reinterpret_cast<void*>(g_placement_address), &placement,
            sizeof(placement));
    }
    g_ever_applied = true;
    if (used_cached_dimensions) {
        return T4HudPlacementApplyStatus::
            applied_full_frame_cached_dimensions;
    }
    return plan.mode == HudPlacementMode::packed_eye
               ? T4HudPlacementApplyStatus::applied_packed_eye
               : T4HudPlacementApplyStatus::applied_full_frame;
#else
    return T4HudPlacementApplyStatus::not_bound;
#endif
}

[[nodiscard]] T4HudPlacementQuiesceResult quiesce_result_from_apply(
    const T4HudPlacementApplyStatus apply_status) noexcept {
    T4HudPlacementQuiesceStatus status =
        T4HudPlacementQuiesceStatus::restore_failed;
    if (apply_status == T4HudPlacementApplyStatus::applied_full_frame) {
        status = T4HudPlacementQuiesceStatus::full_frame_restored;
    } else if (apply_status ==
               T4HudPlacementApplyStatus::
                   applied_full_frame_cached_dimensions) {
        status = T4HudPlacementQuiesceStatus::
            full_frame_restored_cached_dimensions;
    }
    return {
        .status = status,
        .apply_status = apply_status,
        .wait_result = WAIT_OBJECT_0,
    };
}

[[nodiscard]] T4HudPlacementApplyStatus service_placement_locked(
    const bool establish_game_thread) noexcept {
    if (!g_services_enabled ||
        g_phase == PlacementServicePhase::unbound ||
        g_phase == PlacementServicePhase::disabled ||
        g_phase == PlacementServicePhase::quiesced) {
        return T4HudPlacementApplyStatus::service_disabled;
    }

    const DWORD current_thread_id = GetCurrentThreadId();
    if (establish_game_thread && g_game_thread_id == 0) {
        g_game_thread_id = current_thread_id;
    }
    if (g_game_thread_id == 0 || g_game_thread_id != current_thread_id) {
        return T4HudPlacementApplyStatus::wrong_thread;
    }

    const bool quiescing =
        g_phase == PlacementServicePhase::quiesce_requested;
    const T4HudPlacementApplyStatus apply_status =
        apply_placement_locked(quiescing);
    if (quiescing) {
        const T4HudPlacementQuiesceResult attempt =
            quiesce_result_from_apply(apply_status);
        g_quiesce_result = attempt;
        if (attempt.ok()) {
            g_phase = PlacementServicePhase::quiesced;
            if (g_quiesce_event != nullptr) {
                SetEvent(g_quiesce_event);
            }
        }
    }
    return apply_status;
}

}  // namespace

T4HudPlacementBindResult bind_t4_hud_placement(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    ExclusivePlacementLock lock;
    clear_bindings_locked();
    T4HudPlacementBindResult result{};
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    result.status =
        T4HudPlacementBindStatus::unsupported_compiler_or_architecture;
    return result;
#else
    if (g_quiesce_event == nullptr) {
        g_quiesce_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (g_quiesce_event == nullptr) {
            result.status =
                T4HudPlacementBindStatus::synchronization_unavailable;
            return result;
        }
    }

    constexpr wawvr::t4::HookSiteId required_sites[] = {
        wawvr::t4::HookSiteId::
            scr_place_setup_float_viewport_entry_sentinel,
        wawvr::t4::HookSiteId::
            scr_place_view_zero_init_context_sentinel,
    };
    for (const auto site : required_sites) {
        if (!bindings.site_bytes_still_match(site)) {
            result.status = T4HudPlacementBindStatus::sentinel_mismatch;
            return result;
        }
    }

    const auto* const setup_site = bindings.site(
        wawvr::t4::HookSiteId::
            scr_place_setup_float_viewport_entry_sentinel);
    const auto setup_function = bindings.site_address(
        wawvr::t4::HookSiteId::
            scr_place_setup_float_viewport_entry_sentinel);
    const auto placement = bindings.data_address(
        wawvr::t4::DataSymbolId::scr_place_view_zero, 0x48);
    const auto refdef = bindings.data_address(
        wawvr::t4::DataSymbolId::gameplay_refdef_viewport,
        sizeof(T4RefdefViewport));
    if (setup_site == nullptr || !setup_function.has_value() ||
        !placement.has_value() || !refdef.has_value()) {
        result.status = T4HudPlacementBindStatus::address_unavailable;
        return result;
    }
    result.setup_function = *setup_function;
    result.placement = *placement;
    result.refdef = *refdef;
    if (!setup_sentinel_still_matches(*setup_function, setup_site) ||
        !accessible_range(*placement, 0x48, true, false) ||
        !accessible_range(
            *refdef, sizeof(T4RefdefViewport), false, false)) {
        result.status =
            T4HudPlacementBindStatus::memory_protection_invalid;
        return result;
    }

    g_setup_site = setup_site;
    g_refdef_address = *refdef;
    g_placement_address = *placement;
    g_setup_function_address = *setup_function;
    g_services_enabled = true;
    g_phase = PlacementServicePhase::active;
    ResetEvent(g_quiesce_event);
    result.status = T4HudPlacementBindStatus::bound;
    return result;
#endif
}

void clear_t4_hud_placement() noexcept {
    ExclusivePlacementLock lock;
    clear_bindings_locked();
}

T4HudPlacementApplyStatus
service_t4_hud_placement_before_com_frame() noexcept {
    ExclusivePlacementLock lock;
    return service_placement_locked(true);
}

T4HudPlacementApplyStatus
service_t4_hud_placement_before_cg_draw_2d() noexcept {
    ExclusivePlacementLock lock;
    return service_placement_locked(false);
}

T4HudPlacementQuiesceResult request_t4_hud_placement_quiesce(
    const std::uint32_t timeout_milliseconds) noexcept {
    HANDLE event = nullptr;
    {
        ExclusivePlacementLock lock;
        if (g_phase == PlacementServicePhase::unbound ||
            g_setup_function_address == 0) {
            return {
                .status = T4HudPlacementQuiesceStatus::not_bound,
                .apply_status = T4HudPlacementApplyStatus::not_bound,
                .wait_result = WAIT_OBJECT_0,
            };
        }
        if (g_quiesce_event == nullptr) {
            return {
                .status =
                    T4HudPlacementQuiesceStatus::synchronization_unavailable,
                .apply_status =
                    T4HudPlacementApplyStatus::service_disabled,
                .wait_result = WAIT_FAILED,
            };
        }
        if (g_phase == PlacementServicePhase::quiesced) {
            return g_quiesce_result;
        }
        if (!g_ever_applied) {
            g_quiesce_result = {
                .status =
                    T4HudPlacementQuiesceStatus::no_modification_needed,
                .apply_status = T4HudPlacementApplyStatus::service_disabled,
                .wait_result = WAIT_OBJECT_0,
            };
            g_phase = PlacementServicePhase::quiesced;
            SetEvent(g_quiesce_event);
            return g_quiesce_result;
        }
        if (g_phase != PlacementServicePhase::quiesce_requested) {
            ResetEvent(g_quiesce_event);
            g_phase = PlacementServicePhase::quiesce_requested;
            g_quiesce_result = {
                .status = T4HudPlacementQuiesceStatus::restore_failed,
                .apply_status = T4HudPlacementApplyStatus::service_disabled,
                .wait_result = WAIT_TIMEOUT,
            };
        }
        event = g_quiesce_event;
    }

    const DWORD wait_result = WaitForSingleObject(event, timeout_milliseconds);
    ExclusivePlacementLock lock;
    if (g_phase == PlacementServicePhase::quiesced) {
        T4HudPlacementQuiesceResult result = g_quiesce_result;
        result.wait_result = wait_result;
        return result;
    }
    return {
        .status = wait_result == WAIT_TIMEOUT
                      ? T4HudPlacementQuiesceStatus::timed_out
                      : T4HudPlacementQuiesceStatus::
                            synchronization_unavailable,
        .apply_status = g_quiesce_result.apply_status,
        .wait_result = wait_result,
    };
}

T4HudPlacementQuiesceResult
disable_t4_hud_placement_services_and_drain() noexcept {
    ExclusivePlacementLock lock;
    T4HudPlacementQuiesceResult result{};
    if (g_phase == PlacementServicePhase::quiesced) {
        result = g_quiesce_result;
    } else if (g_phase == PlacementServicePhase::unbound) {
        result = {
            .status = T4HudPlacementQuiesceStatus::not_bound,
            .apply_status = T4HudPlacementApplyStatus::not_bound,
            .wait_result = WAIT_OBJECT_0,
        };
    } else {
        result = {
            .status = T4HudPlacementQuiesceStatus::
                disabled_without_acknowledgement,
            .apply_status = T4HudPlacementApplyStatus::service_disabled,
            .wait_result = WAIT_TIMEOUT,
        };
    }
    g_services_enabled = false;
    if (g_phase != PlacementServicePhase::unbound) {
        g_phase = PlacementServicePhase::disabled;
    }
    return result;
}

const char* t4_hud_placement_bind_status_name(
    const T4HudPlacementBindStatus status) noexcept {
    switch (status) {
    case T4HudPlacementBindStatus::bound: return "bound";
    case T4HudPlacementBindStatus::unsupported_compiler_or_architecture:
        return "unsupported-compiler-or-architecture";
    case T4HudPlacementBindStatus::rejected_wrong_profile:
        return "rejected-wrong-profile";
    case T4HudPlacementBindStatus::sentinel_mismatch:
        return "sentinel-mismatch";
    case T4HudPlacementBindStatus::address_unavailable:
        return "address-unavailable";
    case T4HudPlacementBindStatus::memory_protection_invalid:
        return "memory-protection-invalid";
    case T4HudPlacementBindStatus::synchronization_unavailable:
        return "synchronization-unavailable";
    }
    return "unknown";
}

const char* t4_hud_placement_apply_status_name(
    const T4HudPlacementApplyStatus status) noexcept {
    switch (status) {
    case T4HudPlacementApplyStatus::applied_full_frame:
        return "applied-full-frame";
    case T4HudPlacementApplyStatus::applied_full_frame_cached_dimensions:
        return "applied-full-frame-cached-dimensions";
    case T4HudPlacementApplyStatus::applied_packed_eye:
        return "applied-packed-eye";
    case T4HudPlacementApplyStatus::not_bound: return "not-bound";
    case T4HudPlacementApplyStatus::service_disabled:
        return "service-disabled";
    case T4HudPlacementApplyStatus::wrong_thread: return "wrong-thread";
    case T4HudPlacementApplyStatus::sentinel_changed:
        return "sentinel-changed";
    case T4HudPlacementApplyStatus::memory_unavailable:
        return "memory-unavailable";
    case T4HudPlacementApplyStatus::invalid_dimensions:
        return "invalid-dimensions";
    }
    return "unknown";
}

const char* t4_hud_placement_quiesce_status_name(
    const T4HudPlacementQuiesceStatus status) noexcept {
    switch (status) {
    case T4HudPlacementQuiesceStatus::full_frame_restored:
        return "full-frame-restored";
    case T4HudPlacementQuiesceStatus::
            full_frame_restored_cached_dimensions:
        return "full-frame-restored-cached-dimensions";
    case T4HudPlacementQuiesceStatus::no_modification_needed:
        return "no-modification-needed";
    case T4HudPlacementQuiesceStatus::not_bound: return "not-bound";
    case T4HudPlacementQuiesceStatus::synchronization_unavailable:
        return "synchronization-unavailable";
    case T4HudPlacementQuiesceStatus::timed_out: return "timed-out";
    case T4HudPlacementQuiesceStatus::restore_failed:
        return "restore-failed";
    case T4HudPlacementQuiesceStatus::disabled_without_acknowledgement:
        return "disabled-without-acknowledgement";
    }
    return "unknown";
}

}  // namespace wawvr::mod
