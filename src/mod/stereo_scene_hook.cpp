#include "stereo_scene_hook.hpp"

#include "camera_comfort_logic.hpp"
#include "lod_fov_clamp.hpp"
#include "performance_timing.hpp"
#include "stereo_backend_hook.hpp"
#include "stereo_diagnostics.hpp"
#include "stereo_frame_broker.hpp"
#include "peer_thread_quiescence.hpp"
#include "physical_scope_logic.hpp"
#include "scope_visibility_snapshot.hpp"
#include "present_hook_logic.hpp"
#include "stereo_scene_math.hpp"
#include "present_hook.hpp"
#include "t4_layout_selector.hpp"
#if defined(WAWVR_HAS_T4_BINDINGS)
#include "input_hook.hpp"
#include "tank_reticle_hook.hpp"
#include "aircraft_control_runtime.hpp"
#include "t4_presentation_state.hpp"
#include "weapon_hook.hpp"
#endif

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace wawvr::mod {
namespace {

// Opt-in capture evidence, not an orientation correction. Keep native camera
// axes and the runtime metadata observable without changing either transform.
void log_orientation_scene(
    const T4SceneView& stock,
    const wawvr::xr::Posef& anchor,
    const T4StereoSceneViews& stereo) noexcept {
    static const bool enabled = []() noexcept {
        wchar_t value[2]{};
        return GetEnvironmentVariableW(
                   L"WAWVR_ORIENTATION_DIAGNOSTICS", value, 2) == 1 &&
               value[0] == L'1';
    }();
    static unsigned reports = 0;
    if (!enabled || reports >= 64 ||
        (reports != 0 && stereo.frame_id % 30 != 0)) {
        return;
    }
    ++reports;
    stereo_diagnostic_log(
        "OrientationDiag scene frame=%llu anchorP=(%.6f,%.6f,%.6f) anchorQ=(%.6f,%.6f,%.6f,%.6f) stockP=(%.4f,%.4f,%.4f) stockUp=(%.6f,%.6f,%.6f)",
        static_cast<unsigned long long>(stereo.frame_id),
        anchor.position.x, anchor.position.y, anchor.position.z,
        anchor.orientation.x, anchor.orientation.y,
        anchor.orientation.z, anchor.orientation.w,
        stock.origin.x, stock.origin.y, stock.origin.z,
        stock.axis.up.x, stock.axis.up.y, stock.axis.up.z);
    for (unsigned eye = 0; eye < wawvr::xr::kEyeCount; ++eye) {
        const auto& view = stereo.eyes[eye];
        const auto& viewport = stereo.compositor_layout.destinations[eye];
        stereo_diagnostic_log(
            "OrientationDiag scene frame=%llu eye=%u P=(%.4f,%.4f,%.4f) F=(%.6f,%.6f,%.6f) L=(%.6f,%.6f,%.6f) U=(%.6f,%.6f,%.6f) tanFov=(%.6f,%.6f) destination=(%.6f,%.6f,%.6f,%.6f)",
            static_cast<unsigned long long>(stereo.frame_id), eye,
            view.origin.x, view.origin.y, view.origin.z,
            view.axis.forward.x, view.axis.forward.y, view.axis.forward.z,
            view.axis.left.x, view.axis.left.y, view.axis.left.z,
            view.axis.up.x, view.axis.up.y, view.axis.up.z,
            view.tan_half_fov_x, view.tan_half_fov_y,
            viewport.x, viewport.y, viewport.width, viewport.height);
    }
}

// R_RenderScene inlines R_DynamicShadowType into a stack local at
// [ebp-0x0C].  Same-frame stereo generates the scene twice while sharing the
// renderer's transient shadow lists, so the selector must be clamped before
// any shadow-dependent frontend work runs.  These two exact-build patches
// cover the SHADOW_MAP and SHADOW_COOKIE assignments respectively; changing
// only the generated GfxViewInfo field after R_RenderScene returns is too
// late because the invalid transient lists have already been emitted.
constexpr std::size_t kCallInstructionSize = 5;

struct StereoSceneExecutableProfile final {
    const char* name{};
    std::uintptr_t gameplay_refdef_address{};
    std::uintptr_t gameplay_scene_call_address{};
    std::uintptr_t render_scene_address{};
    std::uintptr_t shadow_map_selector_address{};
    std::uintptr_t shadow_cookie_selector_address{};
    std::uintptr_t front_end_data_out_pointer_address{};
    std::uintptr_t clear_client_cmd_list_2d_address{};
    std::array<std::uint8_t, kCallInstructionSize> expected_gameplay_call{};
    std::array<std::uint8_t, 7> expected_shadow_map_selector{};
    std::array<std::uint8_t, 3> expected_shadow_cookie_selector{};
    std::array<std::uint8_t, 13> expected_render_scene_entry{};
    std::array<std::uint8_t, 35> expected_clear_client_cmd_list_2d{};
};

constexpr StereoSceneExecutableProfile kSpSceneProfile{
    .name = "T4 SP 1.7.1263",
    .gameplay_refdef_address = 0x03520338u,
    .gameplay_scene_call_address = 0x00438C57u,
    .render_scene_address = 0x006DEC70u,
    .shadow_map_selector_address = 0x006DCF0Du,
    .shadow_cookie_selector_address = 0x006DCF21u,
    .front_end_data_out_pointer_address = 0x03DCB498u,
    .clear_client_cmd_list_2d_address = 0x006F56B0u,
    .expected_gameplay_call = {0xE8, 0x14, 0x60, 0x2A, 0x00},
    .expected_shadow_map_selector = {
        0xC7, 0x45, 0xF4, 0x02, 0x00, 0x00, 0x00,
    },
    .expected_shadow_cookie_selector = {0x0F, 0x95, 0xC2},
    .expected_render_scene_entry = {
        0x81, 0xEC, 0xE4, 0x02, 0x00, 0x00, 0x80,
        0x3D, 0x61, 0x69, 0xBF, 0x03, 0x00,
    },
    .expected_clear_client_cmd_list_2d = {
        0xA1, 0x98, 0xB4, 0xDC, 0x03,
        0x8B, 0x88, 0xD0, 0x4D, 0x14, 0x00,
        0x8B, 0x90, 0xD4, 0x4D, 0x14, 0x00,
        0x69, 0xC9, 0x80, 0x6D, 0x00, 0x00,
        0xC7, 0x84, 0x11, 0xCC, 0x58, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xC3,
    },
};

constexpr StereoSceneExecutableProfile kMpSceneProfile{
    .name = "T4 MP 1.7.1263",
    .gameplay_refdef_address = 0x009E676Cu,
    .gameplay_scene_call_address = 0x0043E73Eu,
    .render_scene_address = 0x006B7850u,
    .shadow_map_selector_address = 0x006B588Du,
    .shadow_cookie_selector_address = 0x006B58A1u,
    .front_end_data_out_pointer_address = 0x10882FB4u,
    .clear_client_cmd_list_2d_address = 0x006BD5D0u,
    .expected_gameplay_call = {0xE8, 0x0D, 0x91, 0x27, 0x00},
    .expected_shadow_map_selector = {
        0xC7, 0x45, 0xF4, 0x02, 0x00, 0x00, 0x00,
    },
    .expected_shadow_cookie_selector = {0x0F, 0x95, 0xC2},
    .expected_render_scene_entry = {
        0x81, 0xEC, 0xE4, 0x02, 0x00, 0x00, 0x80,
        0x3D, 0x91, 0x0B, 0x88, 0x10, 0x00,
    },
    .expected_clear_client_cmd_list_2d = {
        0xA1, 0xB4, 0x2F, 0x88, 0x10,
        0x8B, 0x88, 0xD0, 0x4D, 0x14, 0x00,
        0x8B, 0x90, 0xD4, 0x4D, 0x14, 0x00,
        0x69, 0xC9, 0x80, 0x6D, 0x00, 0x00,
        0xC7, 0x84, 0x11, 0xCC, 0x58, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xC3,
    },
};

[[nodiscard]] const StereoSceneExecutableProfile*
configured_scene_profile() noexcept {
    wawvr::t4::ExecutableLayoutId layout{};
    if (!get_configured_renderer_layout(&layout)) {
        return nullptr;
    }
    switch (t4_layout_family_from_id(layout)) {
    case T4LayoutFamily::single_player_1_7_1263:
        return &kSpSceneProfile;
    case T4LayoutFamily::multiplayer_1_7_1263:
        return &kMpSceneProfile;
    case T4LayoutFamily::unsupported:
        return nullptr;
    }
    return nullptr;
}

constexpr std::array<std::uint8_t, 7> kStereoShadowMapSelector = {
    0xC7, 0x45, 0xF4, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 3> kStereoShadowCookieSelector = {
    0x33, 0xD2, 0x90,
};

constexpr std::size_t kViewportXOffset = 0x00;
constexpr std::size_t kViewportYOffset = 0x04;
constexpr std::size_t kViewportWidthOffset = 0x08;
constexpr std::size_t kViewportHeightOffset = 0x0C;
constexpr std::size_t kTanHalfFovXOffset = 0x10;
constexpr std::size_t kTanHalfFovYOffset = 0x14;
constexpr std::size_t kViewOriginOffset = 0x1C;
constexpr std::size_t kViewAxisOffset = 0x2C;
constexpr std::size_t kNearClipOffset = 0x60;
constexpr std::size_t kMappedRefdefExtent = kNearClipOffset + sizeof(float);
// Exact SP 1.7.1263 refdef offsets verified against the decrypted renderer.
// CG_DrawAdsOverlay mutates these before R_RenderScene; withholding its 2D
// commands alone does not undo the small native optic viewport.
constexpr std::size_t kNativeScopeUseScissorViewportOffset = 0x42D0;
constexpr std::size_t kFrontEndViewInfoCountOffset = 0x144DD0;
constexpr std::size_t kFrontEndViewInfoPointerOffset = 0x144DD4;
constexpr std::size_t kFrontEndViewInfoStride = 0x6D80;
constexpr std::size_t kFrontEndViewCmdsOffset = 0x58CC;

using RenderSceneFunction = void(__cdecl*)(void*, int);
using ClearClientCmdList2DFunction = void(__cdecl*)();

std::atomic<RenderSceneFunction> g_original_render_scene{nullptr};
std::atomic<bool> g_installed{false};
std::atomic<std::uint64_t> g_last_consumed_frame{0};

#if defined(WAWVR_HAS_T4_BINDINGS)
struct WeaponFrameBasePhaseDiagnostic final {
    std::uint32_t count{};
    std::uint32_t locked_count{};
    std::uint32_t maximum_same_frame_samples{};
    std::uint64_t first_frame_id{};
    std::uint64_t last_frame_id{};
    double sample_to_scene_milliseconds_sum{};
    std::uint64_t maximum_sample_to_scene_milliseconds{};
    double origin_delta_squared_sum{};
    double forward_delta_squared_sum{};
    double left_delta_squared_sum{};
    double up_delta_squared_sum{};
    float maximum_origin_delta{};
    float maximum_forward_delta{};
    float maximum_left_delta{};
    float maximum_up_delta{};
};

thread_local WeaponFrameBasePhaseDiagnostic
    g_weapon_frame_base_phase_diagnostic{};
thread_local std::uint32_t g_weapon_frame_base_phase_report_count = 0;

void observe_weapon_frame_base_phase(
    const WeaponFrameBaseReceipt& receipt,
    const WeaponFrameBasePhaseComparison& comparison,
    const bool scene_base_locked,
    const std::uint64_t scene_sample_milliseconds) noexcept {
    constexpr std::uint32_t kComparisonsPerReport = 120;
    constexpr std::uint32_t kMaximumReports = 16;
    if (!comparison.valid || !receipt.scene_base_lock_eligible ||
        scene_sample_milliseconds < receipt.weapon_sample_milliseconds ||
        g_weapon_frame_base_phase_report_count >= kMaximumReports) {
        return;
    }

    auto& state = g_weapon_frame_base_phase_diagnostic;
    const std::uint64_t sample_to_scene_milliseconds =
        scene_sample_milliseconds - receipt.weapon_sample_milliseconds;
    if (state.count == 0) {
        state.first_frame_id = receipt.frame_id;
    }
    state.last_frame_id = receipt.frame_id;
    ++state.count;
    if (scene_base_locked) {
        ++state.locked_count;
    }
    if (receipt.same_frame_sample_count > state.maximum_same_frame_samples) {
        state.maximum_same_frame_samples = receipt.same_frame_sample_count;
    }
    state.sample_to_scene_milliseconds_sum +=
        static_cast<double>(sample_to_scene_milliseconds);
    if (sample_to_scene_milliseconds >
        state.maximum_sample_to_scene_milliseconds) {
        state.maximum_sample_to_scene_milliseconds =
            sample_to_scene_milliseconds;
    }
    const auto record = [](const float value, double* const squared_sum,
                           float* const maximum) noexcept {
        *squared_sum += static_cast<double>(value) * value;
        if (value > *maximum) {
            *maximum = value;
        }
    };
    record(comparison.origin_delta_iw_units,
           &state.origin_delta_squared_sum, &state.maximum_origin_delta);
    record(comparison.forward_delta_degrees,
           &state.forward_delta_squared_sum, &state.maximum_forward_delta);
    record(comparison.left_delta_degrees,
           &state.left_delta_squared_sum, &state.maximum_left_delta);
    record(comparison.up_delta_degrees,
           &state.up_delta_squared_sum, &state.maximum_up_delta);
    if (state.count < kComparisonsPerReport) {
        return;
    }

    const double inverse_count = 1.0 / static_cast<double>(state.count);
    stereo_diagnostic_log(
        "WeaponDiag scene-base phase window frames=%llu-%llu samples=%u locked=%u sameFrameMax=%u sampleToSceneMs(mean/max)=%.3f/%llu originIw(rms/max)=%.5f/%.5f basisDegF/L/U(rms)=%.5f/%.5f/%.5f basisDegF/L/U(max)=%.5f/%.5f/%.5f",
        static_cast<unsigned long long>(state.first_frame_id),
        static_cast<unsigned long long>(state.last_frame_id), state.count,
        state.locked_count, state.maximum_same_frame_samples,
        state.sample_to_scene_milliseconds_sum * inverse_count,
        static_cast<unsigned long long>(
            state.maximum_sample_to_scene_milliseconds),
        std::sqrt(state.origin_delta_squared_sum * inverse_count),
        state.maximum_origin_delta,
        std::sqrt(state.forward_delta_squared_sum * inverse_count),
        std::sqrt(state.left_delta_squared_sum * inverse_count),
        std::sqrt(state.up_delta_squared_sum * inverse_count),
        state.maximum_forward_delta, state.maximum_left_delta,
        state.maximum_up_delta);
    ++g_weapon_frame_base_phase_report_count;
    state = {};
}
#endif

bool protection_is_readable(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffu;
    return access == PAGE_READONLY || access == PAGE_READWRITE ||
           access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

bool protection_is_writable(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffu;
    return access == PAGE_READWRITE || access == PAGE_WRITECOPY ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

bool accessible_range(
    const void* const address,
    const std::size_t size,
    const bool require_writable = false) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        !protection_is_readable(memory.Protect) ||
        (require_writable && !protection_is_writable(memory.Protect))) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto region_begin =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto region_end = region_begin + memory.RegionSize;
    return begin >= region_begin && begin <= region_end &&
           size <= region_end - begin;
}

// VirtualQuery reports one region at a time. The two exact T4 view records
// cross several image regions with different writable protections, so validate
// every contiguous subrange rather than requiring one MEMORY_BASIC_INFORMATION
// record to cover the complete span.
bool accessible_writable_span(
    const void* const address,
    const std::size_t size,
    const bool emit_diagnostics,
    const char* const diagnostic_name) noexcept {
    constexpr std::size_t kMaximumRegions = 64;
    if (address == nullptr || size == 0) {
        if (emit_diagnostics) {
            stereo_diagnostic_log(
                "StereoDiag %s span rejected: address=%p size=0x%zX",
                diagnostic_name, address, size);
        }
        return false;
    }

    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    if (size > std::numeric_limits<std::uintptr_t>::max() - begin) {
        if (emit_diagnostics) {
            stereo_diagnostic_log(
                "StereoDiag %s span rejected: address=%p size=0x%zX overflows",
                diagnostic_name, address, size);
        }
        return false;
    }
    const std::uintptr_t end = begin + size;
    std::uintptr_t cursor = begin;
    std::size_t region_index = 0;

    while (cursor < end && region_index < kMaximumRegions) {
        MEMORY_BASIC_INFORMATION memory{};
        const SIZE_T queried = VirtualQuery(
            reinterpret_cast<const void*>(cursor), &memory,
            sizeof(memory));
        const auto region_begin =
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        const bool region_end_valid =
            memory.RegionSize <=
            std::numeric_limits<std::uintptr_t>::max() - region_begin;
        const std::uintptr_t region_end = region_end_valid
            ? region_begin + memory.RegionSize
            : region_begin;
        const bool covers_cursor = queried == sizeof(memory) &&
            region_end_valid && region_begin <= cursor &&
            cursor < region_end;
        const bool committed = memory.State == MEM_COMMIT;
        const bool unguarded =
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;
        const bool readable = protection_is_readable(memory.Protect);
        const bool writable = protection_is_writable(memory.Protect);
        const bool accepted = covers_cursor && committed && unguarded &&
            readable && writable;

        if (emit_diagnostics) {
            stereo_diagnostic_log(
                "StereoDiag %s region[%zu]: cursor=%p BaseAddress=%p AllocationBase=%p RegionSize=0x%zX State=0x%08lX Protect=0x%08lX Type=0x%08lX queried=%zu accepted=%u",
                diagnostic_name, region_index,
                reinterpret_cast<const void*>(cursor), memory.BaseAddress,
                memory.AllocationBase,
                static_cast<std::size_t>(memory.RegionSize),
                static_cast<unsigned long>(memory.State),
                static_cast<unsigned long>(memory.Protect),
                static_cast<unsigned long>(memory.Type),
                static_cast<std::size_t>(queried), accepted ? 1u : 0u);
        }
        if (!accepted) {
            return false;
        }

        cursor = region_end < end ? region_end : end;
        ++region_index;
    }

    const bool accepted = cursor == end;
    if (emit_diagnostics) {
        stereo_diagnostic_log(
            "StereoDiag %s span result: begin=%p end=%p size=0x%zX regions=%zu accepted=%u",
            diagnostic_name, address, reinterpret_cast<const void*>(end),
            size, region_index, accepted ? 1u : 0u);
    }
    return accepted;
}

template <typename T>
T read_field(const std::byte* const base, const std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, base + offset, sizeof(value));
    return value;
}

template <typename T>
void write_field(
    std::byte* const base,
    const std::size_t offset,
    const T& value) noexcept {
    std::memcpy(base + offset, &value, sizeof(value));
}

using ScopeVisibilityBanks =
    std::array<std::span<std::byte>, kScopeVisibilityBankCount>;

// The retail scene frontend shares these camera/shadow visibility banks
// between every R_RenderScene call. R_FilterEntitiesIntoCells writes 2 for
// frustum misses; subsequent passes only reconsider entries that are zero.
// Guard the exact native visibility-only clear loop, not R_ClearDpvsScene
// itself (the latter also clears entity-to-scene mappings we must preserve).
bool acquire_scope_visibility_banks(ScopeVisibilityBanks* output) noexcept {
    constexpr std::uintptr_t kClearLoop = 0x006E5971u;
    constexpr std::uintptr_t kEntityCount = 0x03BF67ECu;
    constexpr std::uintptr_t kBankPointers = 0x03DA8CF4u;
    constexpr std::array<std::uint8_t, 36> kExpectedClearLoop{
        0xBE, 0xF4, 0x8C, 0xDA, 0x03, 0x8B, 0x15, 0xEC,
        0x67, 0xBF, 0x03, 0x8B, 0x06, 0x52, 0x6A, 0x00,
        0x50, 0xE8, 0x09, 0xF8, 0xEF, 0xFF, 0x83, 0xC6,
        0x04, 0x83, 0xC4, 0x0C, 0x81, 0xFE, 0x10, 0x8D,
        0xDA, 0x03, 0x7C, 0xE1};
    if (output == nullptr || configured_scene_profile() != &kSpSceneProfile ||
        !accessible_range(reinterpret_cast<const void*>(kClearLoop),
                          kExpectedClearLoop.size()) ||
        std::memcmp(reinterpret_cast<const void*>(kClearLoop),
                    kExpectedClearLoop.data(), kExpectedClearLoop.size()) != 0 ||
        !accessible_range(reinterpret_cast<const void*>(kEntityCount),
                          sizeof(std::uint32_t)) ||
        !accessible_range(reinterpret_cast<const void*>(kBankPointers),
                          kScopeVisibilityBankCount * sizeof(std::uint32_t))) {
        return false;
    }
    const auto count = read_field<std::uint32_t>(
        reinterpret_cast<const std::byte*>(kEntityCount), 0);
    if (count == 0 || count > kScopeVisibilityMaxEntities || count % 32 != 0) {
        return false;
    }
    ScopeVisibilityBanks banks{};
    for (std::size_t bank = 0; bank < banks.size(); ++bank) {
        auto* const data = read_field<std::byte*>(
            reinterpret_cast<const std::byte*>(kBankPointers),
            bank * sizeof(std::uint32_t));
        if (!accessible_writable_span(data, count, false, "scope.visibility")) {
            return false;
        }
        banks[bank] = std::span<std::byte>{data, count};
    }
    *output = banks;
    return true;
}

struct ScopeFrameDecision final {
    std::uint64_t frame_id{};
    std::int64_t display_time{};
    std::int32_t width{};
    std::int32_t height{};
    bool available{};
    PhysicalScopeSnapshot snapshot{};
};

// Native HUD generation and scene enqueue run on the same game thread. Never
// carry a decision across XR frame IDs or viewport changes, including a
// negative decision. Once selected, a long Draw2D cannot expire only one half
// of the HUD/layout contract. Scene memory is still revalidated before use.
thread_local ScopeFrameDecision g_scope_frame_decision{};

bool select_scope_for_frame(const wawvr::xr::FrameState& frame,
    const std::int32_t width, const std::int32_t height,
    PhysicalScopeSnapshot* const output) noexcept {
    if (output == nullptr || frame.frame_id == 0) return false;
    auto& decision = g_scope_frame_decision;
    if (decision.frame_id != frame.frame_id ||
        decision.display_time != frame.predicted_display_time || decision.width != width ||
        decision.height != height) {
        decision = {};
        decision.frame_id = frame.frame_id;
        decision.display_time = frame.predicted_display_time;
        decision.width = width;
        decision.height = height;
        ScopeVisibilityBanks banks{};
        decision.available = configured_scene_profile() == &kSpSceneProfile &&
            physical_scope_reserved_width(true, width, height) != 0 &&
            read_fresh_physical_scope_snapshot(GetTickCount64(), &decision.snapshot) &&
            acquire_scope_visibility_banks(&banks);
    }
    if (decision.available) *output = decision.snapshot;
    return decision.available;
}

std::array<std::uint32_t, 3> visibility_counts(
    const std::span<std::byte> bank) noexcept {
    std::array<std::uint32_t, 3> counts{};
    for (const auto value : bank) {
        const auto index = std::to_integer<unsigned>(value);
        if (index < counts.size()) {
            ++counts[index];
        }
    }
    return counts;
}

bool read_scene_view(void* const refdef, T4SceneView* const output) noexcept {
    const auto* const profile = configured_scene_profile();
    if (profile == nullptr || output == nullptr ||
        reinterpret_cast<std::uintptr_t>(refdef) !=
            profile->gameplay_refdef_address ||
        !accessible_range(refdef, kMappedRefdefExtent, true)) {
        return false;
    }
    const auto* const base = static_cast<const std::byte*>(refdef);
    T4SceneView result{};
    result.x = read_field<std::int32_t>(base, kViewportXOffset);
    result.y = read_field<std::int32_t>(base, kViewportYOffset);
    result.width = read_field<std::int32_t>(base, kViewportWidthOffset);
    result.height = read_field<std::int32_t>(base, kViewportHeightOffset);
    result.tan_half_fov_x = read_field<float>(base, kTanHalfFovXOffset);
    result.tan_half_fov_y = read_field<float>(base, kTanHalfFovYOffset);
    result.origin = read_field<wawvr::xr::Vec3f>(base, kViewOriginOffset);
    result.axis = read_field<wawvr::xr::Basis3f>(base, kViewAxisOffset);
    result.near_clip = read_field<float>(base, kNearClipOffset);
    *output = result;
    return true;
}

void write_scene_view(void* const refdef, const T4SceneView& view) noexcept {
    auto* const base = static_cast<std::byte*>(refdef);
    write_field(base, kViewportXOffset, view.x);
    write_field(base, kViewportYOffset, view.y);
    write_field(base, kViewportWidthOffset, view.width);
    write_field(base, kViewportHeightOffset, view.height);
    write_field(base, kTanHalfFovXOffset, view.tan_half_fov_x);
    write_field(base, kTanHalfFovYOffset, view.tan_half_fov_y);
    write_field(base, kViewOriginOffset, view.origin);
    write_field(base, kViewAxisOffset, view.axis);
    write_field(base, kNearClipOffset, view.near_clip);
}

struct FrontendStereoSlots final {
    std::byte* data{};
    std::byte* views{};
    void* client_commands_2d{};
};

bool hud_slot_profile_ready() noexcept {
    const auto* const profile = configured_scene_profile();
    if (profile == nullptr) {
        return false;
    }
    return accessible_range(
               reinterpret_cast<const void*>(
                   profile->clear_client_cmd_list_2d_address),
               profile->expected_clear_client_cmd_list_2d.size()) &&
           std::memcmp(
               reinterpret_cast<const void*>(
                   profile->clear_client_cmd_list_2d_address),
               profile->expected_clear_client_cmd_list_2d.data(),
               profile->expected_clear_client_cmd_list_2d.size()) == 0;
}

bool acquire_frontend_stereo_slots(
    FrontendStereoSlots* const output,
    const std::uint32_t required_slots) noexcept {
    if (output == nullptr || required_slots < 2u || required_slots > 3u) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.hud.acquire rejected: output=%p requiredSlots=%u",
            output, required_slots);
        return false;
    }
    const auto* const profile = configured_scene_profile();
    if (profile == nullptr) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.hud.acquire rejected: renderer profile is not configured");
        return false;
    }
    if (!hud_slot_profile_ready()) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.hud.acquire rejected: exact R_ClearClientCmdList2D sentinel mismatch at %p",
            reinterpret_cast<const void*>(
                profile->clear_client_cmd_list_2d_address));
        return false;
    }
    if (!accessible_range(
            reinterpret_cast<const void*>(
                profile->front_end_data_out_pointer_address),
            sizeof(void*))) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.hud.acquire rejected: frontEndDataOut global %p is unreadable",
            reinterpret_cast<const void*>(
                profile->front_end_data_out_pointer_address));
        return false;
    }

    std::byte* data = nullptr;
    std::memcpy(
        &data,
        reinterpret_cast<const void*>(
            profile->front_end_data_out_pointer_address),
        sizeof(data));
    if (data == nullptr) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.hud.acquire rejected: frontEndDataOut=null");
        return false;
    }
    if (!accessible_range(
            data + kFrontEndViewInfoCountOffset,
            sizeof(std::uint32_t), true)) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.hud.acquire rejected: data=%p count@+0x144DD0 is not writable",
            data);
        return false;
    }
    if (!accessible_range(
            data + kFrontEndViewInfoPointerOffset,
            sizeof(void*), true)) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.hud.acquire rejected: data=%p views@+0x144DD4 is not writable",
            data);
        return false;
    }
    const std::uint32_t count =
        read_field<std::uint32_t>(data, kFrontEndViewInfoCountOffset);
    if (count != 0u) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.hud.acquire rejected: data=%p frontendCount=%u expected=0 views=%p",
            data, count,
            read_field<void*>(data, kFrontEndViewInfoPointerOffset));
        return false;
    }

    std::byte* views =
        read_field<std::byte*>(data, kFrontEndViewInfoPointerOffset);
    static std::atomic_flag span_diagnostic_gate = ATOMIC_FLAG_INIT;
    const bool emit_span_diagnostics =
        !span_diagnostic_gate.test_and_set(std::memory_order_relaxed);
    if (!accessible_writable_span(
            views, kFrontEndViewInfoStride * required_slots,
            emit_span_diagnostics, "scene.hud.required-slots")) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.hud.acquire rejected: data=%p views=%p requiredSlots=%u span=0x%zX is not writable",
            data, views, required_slots,
            kFrontEndViewInfoStride * required_slots);
        return false;
    }

    void* const commands =
        read_field<void*>(views, kFrontEndViewCmdsOffset);
    if (commands == nullptr || !accessible_range(commands, sizeof(std::uint32_t))) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.hud.acquire rejected: data=%p views=%p slot0.cmds=%p readable=%u",
            data, views, commands,
            commands != nullptr &&
                    accessible_range(commands, sizeof(std::uint32_t))
                ? 1u
                : 0u);
        return false;
    }

    *output = {data, views, commands};
    return true;
}

bool frontend_slots_still_owned(
    const FrontendStereoSlots& slots,
    const std::uint32_t expected_count) noexcept {
    const auto* const profile = configured_scene_profile();
    if (profile == nullptr) {
        return false;
    }
    if (!accessible_range(
            reinterpret_cast<const void*>(
                profile->front_end_data_out_pointer_address),
            sizeof(void*))) {
        return false;
    }
    std::byte* current_data = nullptr;
    std::memcpy(
        &current_data,
        reinterpret_cast<const void*>(
            profile->front_end_data_out_pointer_address),
        sizeof(current_data));
    return current_data == slots.data &&
           accessible_range(
               slots.data + kFrontEndViewInfoCountOffset,
               sizeof(std::uint32_t), true) &&
           accessible_range(
               slots.data + kFrontEndViewInfoPointerOffset,
               sizeof(void*), true) &&
           read_field<std::uint32_t>(
               slots.data, kFrontEndViewInfoCountOffset) == expected_count &&
           read_field<std::byte*>(
               slots.data, kFrontEndViewInfoPointerOffset) == slots.views;
}

bool enqueue_stock_fallback(
    void* const refdef,
    const int scene_flags,
    const T4SceneView& stock,
    const FrontendStereoSlots& slots,
    const RenderSceneFunction original) noexcept {
    const auto* const profile = configured_scene_profile();
    if (profile == nullptr) {
        return false;
    }
    write_scene_view(refdef, stock);

    // A fallback draw is safe only while one of the two validated records is
    // unused. At count == 2 another R_RenderScene call would advance T4 to an
    // unvalidated third record, so fail closed instead.
    std::byte* current_data = nullptr;
    if (accessible_range(
            reinterpret_cast<const void*>(
                profile->front_end_data_out_pointer_address),
            sizeof(void*))) {
        std::memcpy(
            &current_data,
            reinterpret_cast<const void*>(
                profile->front_end_data_out_pointer_address),
            sizeof(current_data));
    }
    const bool count_field_writable = accessible_range(
        slots.data + kFrontEndViewInfoCountOffset,
        sizeof(std::uint32_t), true);
    const bool views_field_writable = accessible_range(
        slots.data + kFrontEndViewInfoPointerOffset,
        sizeof(void*), true);
    static std::atomic_flag fallback_span_diagnostic_gate = ATOMIC_FLAG_INIT;
    const bool fallback_span_writable = accessible_writable_span(
        slots.views, kFrontEndViewInfoStride * 2u,
        !fallback_span_diagnostic_gate.test_and_set(
            std::memory_order_relaxed),
        "scene.stock-fallback.two-slot");
    if (current_data != slots.data || !count_field_writable ||
        !views_field_writable ||
        read_field<std::byte*>(
            slots.data, kFrontEndViewInfoPointerOffset) != slots.views ||
        !fallback_span_writable) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.stock-fallback rejected: frontend ownership/range changed savedData=%p currentData=%p savedViews=%p",
            slots.data, current_data, slots.views);
        return false;
    }

    const std::uint32_t count = read_field<std::uint32_t>(
        slots.data, kFrontEndViewInfoCountOffset);
    if (count >= 2u) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.stock-fallback rejected: frontendCount=%u; both validated slots are consumed",
            count);
        return false;
    }
    write_field(
        slots.views +
            static_cast<std::size_t>(count) * kFrontEndViewInfoStride,
        kFrontEndViewCmdsOffset, slots.client_commands_2d);
    original(refdef, scene_flags);
    WAWVR_STEREO_DIAG_ONCE(
        "StereoDiag scene.stock-fallback accepted: frontendCountBefore=%u targetSlot=%u",
        count, count);
    return true;
}

class RefdefRestore final {
public:
    RefdefRestore(void* const refdef, const T4SceneView& stock) noexcept
        : refdef_(refdef), stock_(stock) {}
    ~RefdefRestore() { write_scene_view(refdef_, stock_); }
    RefdefRestore(const RefdefRestore&) = delete;
    RefdefRestore& operator=(const RefdefRestore&) = delete;

private:
    void* refdef_{};
    T4SceneView stock_{};
};

class ScopedNativeScopeScissorDisable final {
public:
    ScopedNativeScopeScissorDisable(
        void* const refdef,
        const bool required) noexcept {
        if (!required) {
            valid_ = true;
            return;
        }
        auto* const base = static_cast<std::byte*>(refdef);
        byte_ = base + kNativeScopeUseScissorViewportOffset;
        if (!accessible_range(byte_, sizeof(original_), true)) {
            return;
        }
        std::memcpy(&original_, byte_, sizeof(original_));
        if (original_ > 1u) {
            byte_ = nullptr;
            return;
        }
        constexpr std::uint8_t disabled = 0;
        std::memcpy(byte_, &disabled, sizeof(disabled));
        valid_ = true;
        active_ = true;
    }

    ~ScopedNativeScopeScissorDisable() { restore(); }
    ScopedNativeScopeScissorDisable(
        const ScopedNativeScopeScissorDisable&) = delete;
    ScopedNativeScopeScissorDisable& operator=(
        const ScopedNativeScopeScissorDisable&) = delete;

    [[nodiscard]] bool valid() const noexcept { return valid_; }

    void restore() noexcept {
        if (active_ && byte_ != nullptr) {
            std::memcpy(byte_, &original_, sizeof(original_));
            active_ = false;
        }
    }

private:
    std::byte* byte_{};
    std::uint8_t original_{};
    bool valid_{};
    bool active_{};
};

void __cdecl stereo_scene_thunk(void* const refdef, const int scene_flags) noexcept {
    const auto original =
        g_original_render_scene.load(std::memory_order_acquire);
    if (original == nullptr) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.entry rejected: original R_RenderScene pointer=null");
        return;
    }
    const auto* const profile = configured_scene_profile();
    if (profile == nullptr) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.entry rejected: renderer profile is not configured");
        original(refdef, scene_flags);
        return;
    }

#if defined(WAWVR_HAS_T4_BINDINGS)
    if (profile == &kMpSceneProfile) {
        const T4PresentationState presentation =
            read_t4_presentation_state();
        if (!should_pack_stereo_scene(
                true, presentation.valid, presentation.connection_state,
                presentation.active_connection_state,
                presentation.key_catchers)) {
            WAWVR_STEREO_DIAG_ONCE(
                "StereoDiag scene.stock-mono accepted: MP stateValid=%u connection=%d active=%d keyCatchers=0x%X",
                presentation.valid ? 1u : 0u,
                presentation.connection_state,
                presentation.active_connection_state,
                presentation.key_catchers);
            original(refdef, scene_flags);
            return;
        }
    }
#endif

    bool began_stereo_calls = false;
    try {
        T4SceneView stock{};
        PendingStereoFrame pending{};
        FrontendStereoSlots slots{};
        const std::uint64_t previous =
            g_last_consumed_frame.load(std::memory_order_acquire);
        if (!stereo_backend_hook_ready()) {
            WAWVR_STEREO_DIAG_ONCE(
                "StereoDiag scene.stage rejected: exact backend hook is not ready");
            original(refdef, scene_flags);
            return;
        }
        if (!read_scene_view(refdef, &stock)) {
            WAWVR_STEREO_DIAG_ONCE(
                "StereoDiag scene.stage rejected: refdef=%p expected=%p mappedExtent=0x%zX",
                refdef,
                reinterpret_cast<const void*>(
                    profile->gameplay_refdef_address),
                kMappedRefdefExtent);
            original(refdef, scene_flags);
            return;
        }
        if (!try_acquire_pending_stereo_frame(previous, &pending)) {
            WAWVR_STEREO_DIAG_ONCE(
                "StereoDiag scene.stage rejected: no pending XR frame afterFrame=%llu",
                static_cast<unsigned long long>(previous));
            original(refdef, scene_flags);
            return;
        }

        T4SceneView stereo_stock = stock;
#if defined(WAWVR_HAS_T4_BINDINGS)
        WeaponFrameBaseReceipt weapon_base_receipt{};
        WeaponFrameBasePhaseComparison weapon_base_comparison{};
        wawvr::xr::Basis3f scene_body_axis{};
        wawvr::xr::Vec3f selected_scene_origin{};
        wawvr::xr::Basis3f selected_scene_axis{};
        bool scene_base_locked = false;
        if (gravity_level_t4_camera_axis(stock.axis, &scene_body_axis) &&
            read_weapon_frame_base_receipt(
                pending.frame.frame_id, &weapon_base_receipt)) {
            scene_base_locked = select_weapon_aligned_scene_base(
                weapon_base_receipt, pending.frame,
                pending.tracking_anchor, stock.origin, scene_body_axis,
                &selected_scene_origin, &selected_scene_axis,
                &weapon_base_comparison);
            if (scene_base_locked) {
                stereo_stock.origin = selected_scene_origin;
                stereo_stock.axis = selected_scene_axis;
                WAWVR_STEREO_DIAG_ONCE(
                    "WeaponDiag exact-frame scene base locked to the post-T4 steady two-hand M1 weapon sample");
            }
            observe_weapon_frame_base_phase(
                weapon_base_receipt, weapon_base_comparison,
                scene_base_locked, GetTickCount64());
        }
        if (profile == &kSpSceneProfile) {
            wawvr::xr::Vec3f aircraft_origin{};
            wawvr::xr::Basis3f aircraft_axis{};
            if (read_aircraft_camera_base(stock.origin, &aircraft_origin, &aircraft_axis)) {
                stereo_stock.origin = aircraft_origin;
                stereo_stock.axis = aircraft_axis;
            }
        }
#endif

        PhysicalScopeSnapshot physical_scope{};
        ScopeVisibilityBanks scope_visibility_banks{};
        bool physical_scope_available =
            profile == &kSpSceneProfile &&
            select_scope_for_frame(pending.frame, stock.width,
                                   stock.height, &physical_scope);
        if (physical_scope_available &&
            !acquire_scope_visibility_banks(&scope_visibility_banks)) {
            physical_scope_available = false;
            WAWVR_STEREO_DIAG_ONCE(
                "ScopeDiag omitted optional optic: exact retail visibility banks could not be validated; ordinary stereo preserved");
        }
        wawvr::xr::Vec3f tank_aim_target{};
        bool tank_aim_available = false;
#if defined(WAWVR_HAS_T4_BINDINGS)
        tank_aim_available = profile == &kSpSceneProfile &&
            tank_reticle_hook_installed() &&
            read_controller_tank_aim_target(&tank_aim_target);
#endif
        T4StereoSceneViews stereo{};
        if (!build_t4_stereo_scene_views(
                stereo_stock, pending.frame, pending.tracking_anchor, &stereo,
                wawvr::xr::kIwUnitsPerMeter,
                physical_scope_available ? &physical_scope : nullptr,
                tank_aim_available ? &tank_aim_target : nullptr)) {
            WAWVR_STEREO_DIAG_ONCE(
                "StereoDiag scene.stage rejected: stereo math frame=%llu shouldRender=%u viewsValid=%u stockViewport=(%d,%d %dx%d) fov=(%.6f,%.6f)",
                static_cast<unsigned long long>(pending.frame.frame_id),
                pending.frame.should_render ? 1u : 0u,
                pending.frame.views_valid ? 1u : 0u, stock.x, stock.y,
                stock.width, stock.height, stock.tan_half_fov_x,
                stock.tan_half_fov_y);
            // Consume malformed predicted data once so a repeated gameplay
            // call cannot repeatedly attempt the same rejected transform.
            g_last_consumed_frame.store(
                pending.frame.frame_id, std::memory_order_release);
            original(refdef, scene_flags);
            return;
        }

        log_orientation_scene(stereo_stock, pending.tracking_anchor, stereo);
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag COD4-parity common-head stereo cameras active; runtime eye poses remain authoritative for projection submission");
        if (stereo.compositor_layout.world_aim_marker.active) {
            const auto& aim_reference = stereo.compositor_layout.world_aim_marker.origin_reference_meters;
            WAWVR_STEREO_DIAG_ONCE(
                "TankDiag capture-time world aim marker active: target=(%.3f,%.3f,%.3f) reference=(%.3f,%.3f,%.3f)",
                tank_aim_target.x, tank_aim_target.y, tank_aim_target.z,
                aim_reference.x, aim_reference.y, aim_reference.z);
        }

        const std::uint32_t required_views =
            stereo.scope_active ? 3u : 2u;
        if (!acquire_frontend_stereo_slots(&slots, required_views)) {
            original(refdef, scene_flags);
            return;
        }

        ScopedNativeScopeScissorDisable native_scope_scissor(
            refdef, stereo.scope_active);
        if (!native_scope_scissor.valid()) {
            WAWVR_STEREO_DIAG_ONCE(
                "StereoDiag scene.scope rejected: exact native ADS scissor byte at refdef+0x%zX is inaccessible or invalid",
                kNativeScopeUseScissorViewportOffset);
            original(refdef, scene_flags);
            return;
        }

        g_last_consumed_frame.store(stereo.frame_id, std::memory_order_release);
        RefdefRestore restore(refdef, stock);
        std::array<T4SceneView, 3> render_views{};
        std::array<void*, 3> render_commands{};
        if (stereo.scope_active) {
            // Keep the established scope,left,right layout (including the
            // matching left-eye FloatZ clear mesh). The scope's temporary
            // visibility decisions are restored before either normal eye.
            render_views = {stereo.scope, stereo.eyes[0], stereo.eyes[1]};
            // Only the optical feed is HUD-free. The narrow native scope-HUD
            // guards prevent the legacy fullscreen mask before commands are
            // queued, so both normal eyes retain compass/ammo/status widgets.
        } else {
            render_views = {stereo.eyes[0], stereo.eyes[1], {}};
        }
        const auto hud_views = stereo_hud_command_views(stereo.scope_active);
        for (std::size_t view = 0; view < render_commands.size(); ++view) {
            render_commands[view] = hud_views[view]
                ? slots.client_commands_2d : nullptr;
        }
        if (stereo.scope_active) {
            WAWVR_STEREO_DIAG_ONCE(
                "ScopeHudDiag optical feed HUD-free; ordinary left/right HUD commands restored (%p,%p,%p)",
                render_commands[0], render_commands[1], render_commands[2]);
        }

        const bool batch_timing_enabled =
            performance_timing_diagnostics_enabled();
        const auto batch_timing_started = batch_timing_enabled
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        for (std::uint32_t view_index = 0;
             view_index < required_views; ++view_index) {
            std::byte* const slot =
                slots.views +
                static_cast<std::size_t>(view_index) *
                    kFrontEndViewInfoStride;
            if (view_index == 0u) {
                write_field(
                    slot, kFrontEndViewCmdsOffset,
                    render_commands[view_index]);
            } else {
                reinterpret_cast<ClearClientCmdList2DFunction>(
                    profile->clear_client_cmd_list_2d_address)();
                if (!frontend_slots_still_owned(slots, view_index) ||
                    read_field<void*>(slot, kFrontEndViewCmdsOffset) !=
                        nullptr) {
                    WAWVR_STEREO_DIAG_ONCE(
                        "StereoDiag scene.hud.clear rejected: data=%p views=%p expectedCount=%u slot=%u cmds=%p expected=null",
                        slots.data, slots.views, view_index, view_index,
                        read_field<void*>(slot, kFrontEndViewCmdsOffset));
                    if (view_index < 2u) {
                        enqueue_stock_fallback(
                            refdef, scene_flags, stock, slots, original);
                    }
                    return;
                }
                write_field(
                    slot, kFrontEndViewCmdsOffset,
                    render_commands[view_index]);
            }

            ScopeVisibilitySnapshot scope_visibility;
            const bool scope_pass = stereo.scope_active && view_index == 0u;
            if (scope_pass &&
                !scope_visibility.capture(scope_visibility_banks)) {
                WAWVR_STEREO_DIAG_ONCE(
                    "ScopeDiag visibility capture rejected before any frontend calls");
                enqueue_stock_fallback(refdef, scene_flags, stock, slots, original);
                return;
            }
            static unsigned visibility_reports = 0;
            const bool report_visibility = scope_pass &&
                visibility_reports < 12 &&
                (visibility_reports == 0 || stereo.frame_id % 60 == 0);
            const auto before_visibility = report_visibility
                ? visibility_counts(scope_visibility_banks[0])
                : std::array<std::uint32_t, 3>{};
            write_scene_view(refdef, render_views[view_index]);
            began_stereo_calls = true;
            {
                ScopedStereoLodFovClamp lod_clamp;
                ScopedPerformanceTiming timing(
                    PerformanceTimingPhase::stereo_frontend_view);
                original(refdef, scene_flags);
            }
            if (scope_pass) {
                const auto after_visibility = report_visibility
                    ? visibility_counts(scope_visibility_banks[0])
                    : std::array<std::uint32_t, 3>{};
                scope_visibility.restore();
                if (report_visibility) {
                    const auto restored = visibility_counts(scope_visibility_banks[0]);
                    ++visibility_reports;
                    stereo_diagnostic_log(
                        "ScopeDiag visibility isolated: frame=%llu entities=%zu cameraBefore=[%u,%u,%u] afterScope=[%u,%u,%u] restored=[%u,%u,%u]",
                        static_cast<unsigned long long>(stereo.frame_id),
                        scope_visibility_banks[0].size(),
                        before_visibility[0], before_visibility[1], before_visibility[2],
                        after_visibility[0], after_visibility[1], after_visibility[2],
                        restored[0], restored[1], restored[2]);
                }
            }
            if (!frontend_slots_still_owned(slots, view_index + 1u) ||
                read_field<void*>(slot, kFrontEndViewCmdsOffset) !=
                    render_commands[view_index]) {
                WAWVR_STEREO_DIAG_ONCE(
                    "StereoDiag scene.view rejected after call: frame=%llu view=%u required=%u countExpected=%u slot=%p cmds=%p expectedCmds=%p",
                    static_cast<unsigned long long>(stereo.frame_id),
                    view_index, required_views, view_index + 1u, slot,
                    read_field<void*>(slot, kFrontEndViewCmdsOffset),
                    render_commands[view_index]);
                if (view_index == 0u) {
                    enqueue_stock_fallback(
                        refdef, scene_flags, stock, slots, original);
                }
                return;
            }
        }
        if (batch_timing_enabled) {
            record_performance_timing(
                PerformanceTimingPhase::stereo_frontend_batch,
                std::chrono::steady_clock::now() - batch_timing_started);
        }
        stage_stereo_frame_for_backend(
            stereo.frame_id, stereo.compositor_layout);
        mark_performance_frame_scene(stereo.frame_id, required_views);
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.stage accepted: frame=%llu data=%p views=%p count=%u scope=%u cmds=%p",
            static_cast<unsigned long long>(stereo.frame_id), slots.data,
            slots.views, required_views, stereo.scope_active ? 1u : 0u,
            slots.client_commands_2d);
    } catch (...) {
        WAWVR_STEREO_DIAG_ONCE(
            "StereoDiag scene.stage rejected: exception beganStereoCalls=%u",
            began_stereo_calls ? 1u : 0u);
        // This catch covers publication-lock/allocation failures. The stock
        // scene remains the only safe fallback when stereo setup did not run.
        if (!began_stereo_calls) {
            original(refdef, scene_flags);
        }
    }
}

StereoSceneHookResult map_backend_result(
    const StereoBackendHookResult result) noexcept {
    switch (result) {
    case StereoBackendHookResult::installed:
    case StereoBackendHookResult::already_installed:
        return StereoSceneHookResult::installed;
    case StereoBackendHookResult::profile_mismatch:
        return StereoSceneHookResult::profile_mismatch;
    case StereoBackendHookResult::thread_suspend_failed:
        return StereoSceneHookResult::thread_suspend_failed;
    case StereoBackendHookResult::patch_write_failed:
        return StereoSceneHookResult::patch_write_failed;
    }
    return StereoSceneHookResult::patch_write_failed;
}

[[nodiscard]] bool suspend_scene_patch_threads(
    SuspendedPeerThreads* const suspended) noexcept {
    const auto* const profile = configured_scene_profile();
    if (suspended == nullptr || profile == nullptr) {
        return false;
    }
    const std::array<PeerThreadPatchRange, 3> patch_ranges{{
        {profile->gameplay_scene_call_address, kCallInstructionSize},
        {profile->shadow_map_selector_address,
         profile->expected_shadow_map_selector.size()},
        {profile->shadow_cookie_selector_address,
         profile->expected_shadow_cookie_selector.size()},
    }};
    return suspended->suspend(patch_ranges);
}

bool verify_exact_profile_sites() noexcept {
    const auto* const profile = configured_scene_profile();
    if (profile == nullptr) {
        return false;
    }
    const auto* const call = reinterpret_cast<const std::uint8_t*>(
        profile->gameplay_scene_call_address);
    const auto* const target = reinterpret_cast<const std::uint8_t*>(
        profile->render_scene_address);
    return accessible_range(call, profile->expected_gameplay_call.size()) &&
           accessible_range(
               target, profile->expected_render_scene_entry.size()) &&
           std::memcmp(call, profile->expected_gameplay_call.data(),
                       profile->expected_gameplay_call.size()) == 0 &&
           std::memcmp(target, profile->expected_render_scene_entry.data(),
                       profile->expected_render_scene_entry.size()) == 0 &&
           accessible_range(
               reinterpret_cast<const void*>(
                   profile->shadow_map_selector_address),
               profile->expected_shadow_map_selector.size()) &&
           std::memcmp(
               reinterpret_cast<const void*>(
                   profile->shadow_map_selector_address),
               profile->expected_shadow_map_selector.data(),
               profile->expected_shadow_map_selector.size()) == 0 &&
           accessible_range(
               reinterpret_cast<const void*>(
                   profile->shadow_cookie_selector_address),
               profile->expected_shadow_cookie_selector.size()) &&
           std::memcmp(
               reinterpret_cast<const void*>(
                   profile->shadow_cookie_selector_address),
               profile->expected_shadow_cookie_selector.data(),
               profile->expected_shadow_cookie_selector.size()) == 0 &&
           hud_slot_profile_ready();
}

std::array<std::uint8_t, kCallInstructionSize> replacement_call() noexcept {
    std::array<std::uint8_t, kCallInstructionSize> bytes{};
    const auto* const profile = configured_scene_profile();
    if (profile == nullptr) {
        return bytes;
    }
    bytes[0] = 0xE8;
    const auto next_instruction =
        static_cast<std::uint32_t>(
            profile->gameplay_scene_call_address + kCallInstructionSize);
    const auto target = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&stereo_scene_thunk));
    const std::uint32_t displacement = target - next_instruction;
    std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
    return bytes;
}

bool scene_patch_ownership_ready() noexcept {
    const auto* const profile = configured_scene_profile();
    if (profile == nullptr) {
        return false;
    }
    const auto call = replacement_call();
    return accessible_range(
               reinterpret_cast<const void*>(
                   profile->gameplay_scene_call_address),
               call.size()) &&
           std::memcmp(
               reinterpret_cast<const void*>(
                   profile->gameplay_scene_call_address),
               call.data(), call.size()) == 0 &&
           accessible_range(
               reinterpret_cast<const void*>(
                   profile->shadow_map_selector_address),
               kStereoShadowMapSelector.size()) &&
           std::memcmp(
               reinterpret_cast<const void*>(
                   profile->shadow_map_selector_address),
               kStereoShadowMapSelector.data(),
               kStereoShadowMapSelector.size()) == 0 &&
           accessible_range(
               reinterpret_cast<const void*>(
                   profile->shadow_cookie_selector_address),
               kStereoShadowCookieSelector.size()) &&
           std::memcmp(
               reinterpret_cast<const void*>(
                   profile->shadow_cookie_selector_address),
               kStereoShadowCookieSelector.data(),
               kStereoShadowCookieSelector.size()) == 0 &&
           hud_slot_profile_ready();
}

// Changes the gameplay call and both inlined shadow-selector assignments as
// one ownership-checked transaction while peer threads are suspended by the
// caller.  No bytes are written until both code ranges are writable and all
// three current byte sequences match the requested source state.
bool write_scene_patches(const bool install) noexcept {
    const auto* const profile = configured_scene_profile();
    if (profile == nullptr ||
        profile->shadow_cookie_selector_address <
            profile->shadow_map_selector_address) {
        return false;
    }
    auto* const call = reinterpret_cast<std::uint8_t*>(
        profile->gameplay_scene_call_address);
    auto* const shadow_map = reinterpret_cast<std::uint8_t*>(
        profile->shadow_map_selector_address);
    auto* const shadow_cookie = reinterpret_cast<std::uint8_t*>(
        profile->shadow_cookie_selector_address);
    const auto replacement = replacement_call();
    const auto& expected_call =
        install ? profile->expected_gameplay_call : replacement;
    const auto& desired_call =
        install ? replacement : profile->expected_gameplay_call;
    const auto& expected_map = install
        ? profile->expected_shadow_map_selector
        : kStereoShadowMapSelector;
    const auto& desired_map = install
        ? kStereoShadowMapSelector
        : profile->expected_shadow_map_selector;
    const auto& expected_cookie = install
        ? profile->expected_shadow_cookie_selector
        : kStereoShadowCookieSelector;
    const auto& desired_cookie = install
        ? kStereoShadowCookieSelector
        : profile->expected_shadow_cookie_selector;

    const std::size_t shadow_patch_span =
        profile->shadow_cookie_selector_address -
            profile->shadow_map_selector_address +
        profile->expected_shadow_cookie_selector.size();
    DWORD old_call_protection = 0;
    DWORD old_shadow_protection = 0;
    if (!VirtualProtect(
            call, kCallInstructionSize, PAGE_EXECUTE_READWRITE,
            &old_call_protection)) {
        return false;
    }
    if (!VirtualProtect(
            shadow_map, shadow_patch_span, PAGE_EXECUTE_READWRITE,
            &old_shadow_protection)) {
        DWORD ignored = 0;
        VirtualProtect(
            call, kCallInstructionSize, old_call_protection, &ignored);
        return false;
    }

    const bool owned =
        std::memcmp(
            call, expected_call.data(), expected_call.size()) == 0 &&
        std::memcmp(
            shadow_map, expected_map.data(), expected_map.size()) == 0 &&
        std::memcmp(
            shadow_cookie, expected_cookie.data(),
            expected_cookie.size()) == 0;
    if (owned) {
        std::memcpy(call, desired_call.data(), desired_call.size());
        std::memcpy(shadow_map, desired_map.data(), desired_map.size());
        std::memcpy(
            shadow_cookie, desired_cookie.data(), desired_cookie.size());
        FlushInstructionCache(
            GetCurrentProcess(), call, kCallInstructionSize);
        FlushInstructionCache(
            GetCurrentProcess(), shadow_map, shadow_patch_span);
    }

    DWORD ignored = 0;
    VirtualProtect(
        shadow_map, shadow_patch_span, old_shadow_protection, &ignored);
    VirtualProtect(
        call, kCallInstructionSize, old_call_protection, &ignored);
    return owned;
}

} // namespace

StereoSceneHookResult install_stereo_scene_hook() noexcept {
    const auto* const profile = configured_scene_profile();
    if (profile == nullptr) {
        return StereoSceneHookResult::profile_mismatch;
    }
    if (g_installed.load(std::memory_order_acquire)) {
        return stereo_backend_hook_ready() && scene_patch_ownership_ready()
                   ? StereoSceneHookResult::already_installed
                   : StereoSceneHookResult::profile_mismatch;
    }
    if (!verify_exact_profile_sites()) {
        return StereoSceneHookResult::profile_mismatch;
    }

    // Decode and independently re-check the relative target instead of
    // trusting the hard-coded address alone.
    std::int32_t displacement = 0;
    std::memcpy(
        &displacement,
        reinterpret_cast<const void*>(
            profile->gameplay_scene_call_address + 1),
        sizeof(displacement));
    const auto decoded_target = static_cast<std::uintptr_t>(
        static_cast<std::uint32_t>(
            profile->gameplay_scene_call_address +
            kCallInstructionSize + displacement));
    if (decoded_target != profile->render_scene_address) {
        return StereoSceneHookResult::target_mismatch;
    }

    const auto backend_result = install_stereo_backend_hook();
    if (backend_result != StereoBackendHookResult::installed &&
        backend_result != StereoBackendHookResult::already_installed) {
        return map_backend_result(backend_result);
    }
    if (!stereo_backend_hook_ready()) {
        static_cast<void>(restore_stereo_backend_hook());
        return StereoSceneHookResult::profile_mismatch;
    }

    StereoSceneHookResult scene_patch_result = StereoSceneHookResult::installed;
    {
        SuspendedPeerThreads suspended;
        if (!suspend_scene_patch_threads(&suspended)) {
            scene_patch_result = StereoSceneHookResult::thread_suspend_failed;
        } else if (!verify_exact_profile_sites()) {
            scene_patch_result = StereoSceneHookResult::profile_mismatch;
        } else {
            g_original_render_scene.store(
                reinterpret_cast<RenderSceneFunction>(
                    profile->render_scene_address),
                std::memory_order_release);
            if (!write_scene_patches(true)) {
                g_original_render_scene.store(
                    nullptr, std::memory_order_release);
                scene_patch_result = StereoSceneHookResult::patch_write_failed;
            }
        }
    }
    if (scene_patch_result != StereoSceneHookResult::installed) {
        static_cast<void>(restore_stereo_backend_hook());
        return scene_patch_result;
    }
    g_last_consumed_frame.store(0, std::memory_order_release);
    g_installed.store(true, std::memory_order_release);
    stereo_diagnostic_log(
        "StereoDiag scene.shadow policy installed: %s exact R_RenderScene local selectors at %p/%p force SHADOW_NONE before stereo scene generation",
        profile->name,
        reinterpret_cast<const void*>(profile->shadow_map_selector_address),
        reinterpret_cast<const void*>(
            profile->shadow_cookie_selector_address));
    return StereoSceneHookResult::installed;
}

StereoSceneHookResult restore_stereo_scene_hook() noexcept {
    const bool scene_installed =
        g_installed.load(std::memory_order_acquire);
    const bool backend_installed = stereo_backend_hook_installed();
    if (!scene_installed && !backend_installed) {
        return StereoSceneHookResult::already_installed;
    }
    if (scene_installed) {
        SuspendedPeerThreads suspended;
        if (!suspend_scene_patch_threads(&suspended)) {
            return StereoSceneHookResult::thread_suspend_failed;
        }
        if (!write_scene_patches(false)) {
            return StereoSceneHookResult::patch_write_failed;
        }
        g_installed.store(false, std::memory_order_release);
        g_original_render_scene.store(nullptr, std::memory_order_release);
        g_last_consumed_frame.store(0, std::memory_order_release);
    }

    if (backend_installed) {
        const auto backend_result = restore_stereo_backend_hook();
        if (backend_result != StereoBackendHookResult::installed &&
            backend_result != StereoBackendHookResult::already_installed) {
            return map_backend_result(backend_result);
        }
    }
    clear_pending_stereo_frame();
    return StereoSceneHookResult::installed;
}

bool stereo_scene_hook_installed() noexcept {
    return g_installed.load(std::memory_order_acquire) ||
           stereo_backend_hook_installed();
}

bool stereo_scene_ready_for_current_com_frame() noexcept {
    // The exact T4 draw path invokes the patched CG_Draw2D call before the
    // gameplay R_RenderScene call. At both placement boundaries, therefore,
    // the current publication must still be newer than the last frame this
    // scene hook consumed. This rejects a valid-looking publication retained
    // across a Reset/recovery early return without rejecting this frame's HUD.
    const std::uint64_t consumed_frame_id =
        g_last_consumed_frame.load(std::memory_order_acquire);
    if (!g_installed.load(std::memory_order_acquire) ||
        g_original_render_scene.load(std::memory_order_acquire) == nullptr ||
        !stereo_backend_hook_ready() || !scene_patch_ownership_ready() ||
        !pending_stereo_frame_ready_after(consumed_frame_id)) {
        return false;
    }
    // Close teardown/foreign-patch windows opened while the ownership and
    // broker checks ran. This remains a snapshot rather than a reservation;
    // the scene thunk independently fails closed if XR is invalidated later.
    return g_installed.load(std::memory_order_acquire) &&
           g_original_render_scene.load(std::memory_order_acquire) != nullptr &&
           stereo_backend_hook_ready() && scene_patch_ownership_ready() &&
           pending_stereo_frame_ready_after(consumed_frame_id);
}

std::int32_t stereo_scene_scope_reserved_width(
    const std::int32_t full_width, const std::int32_t full_height) noexcept {
    try {
        PhysicalScopeSnapshot scope{};
        PendingStereoFrame pending{};
        const bool scope_available =
            configured_scene_profile() == &kSpSceneProfile &&
            stereo_scene_ready_for_current_com_frame() &&
            try_acquire_pending_stereo_frame(
                g_last_consumed_frame.load(std::memory_order_acquire), &pending) &&
            select_scope_for_frame(pending.frame, full_width, full_height, &scope);
        return physical_scope_reserved_width(
            scope_available && scope.active, full_width, full_height);
    } catch (...) {
        g_scope_frame_decision = {};
        return 0;
    }
}

bool stereo_scene_physical_scope_hud_active() noexcept {
    try {
        PendingStereoFrame pending{};
        return stereo_scene_ready_for_current_com_frame() &&
            try_acquire_pending_stereo_frame(
                g_last_consumed_frame.load(std::memory_order_acquire), &pending) &&
            g_scope_frame_decision.frame_id == pending.frame.frame_id &&
            g_scope_frame_decision.display_time == pending.frame.predicted_display_time &&
            g_scope_frame_decision.available && g_scope_frame_decision.snapshot.active;
    } catch (...) {
        return false;
    }
}

const char* describe_stereo_scene_hook_result(
    const StereoSceneHookResult result) noexcept {
    switch (result) {
    case StereoSceneHookResult::installed:
        return "installed/restored";
    case StereoSceneHookResult::already_installed:
        return "already in requested state";
    case StereoSceneHookResult::profile_mismatch:
        return "exact call-site or target sentinel mismatch";
    case StereoSceneHookResult::target_mismatch:
        return "relative call did not target exact R_RenderScene";
    case StereoSceneHookResult::thread_suspend_failed:
        return "could not safely suspend peer threads";
    case StereoSceneHookResult::patch_write_failed:
        return "protected call-site write/ownership check failed";
    }
    return "unknown";
}

} // namespace wawvr::mod
