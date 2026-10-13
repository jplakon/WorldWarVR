#include "t4_hud_placement.hpp"

#include "peer_thread_quiescence.hpp"

#include "t4/hook_api.hpp"
#include "t4/profile.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace wawvr::mod {
namespace {

constexpr std::size_t kCallInstructionSize = 5;
// pushfd + pushad + call rel32 + popad + popfd + jmp [abs32]. This guarded
// extent prevents teardown from revoking the tail target under an in-flight
// naked bridge.
constexpr std::size_t kBridgeInstructionSize = 15;
std::atomic<bool> g_installed{false};
std::atomic<bool> g_service_enabled{false};
std::uintptr_t g_callsite = 0;
std::array<std::uint8_t, kCallInstructionSize> g_original_call{};
std::array<std::uint8_t, kCallInstructionSize> g_replacement_call{};

[[nodiscard]] bool disabled_by_environment() noexcept {
    std::array<wchar_t, 16> value{};
    const DWORD length = GetEnvironmentVariableW(
        L"WAWVR_DISABLE_XR", value.data(), static_cast<DWORD>(value.size()));
    return length != 0 && length < value.size() &&
           (value[0] == L'1' || value[0] == L'y' || value[0] == L'Y' ||
            value[0] == L't' || value[0] == L'T');
}

[[nodiscard]] bool protection_is_readable(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffU;
    return access == PAGE_READONLY || access == PAGE_READWRITE ||
           access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool readable_range(
    const void* const address, const std::size_t size) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        !protection_is_readable(memory.Protect)) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto region_begin =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto region_end = region_begin + memory.RegionSize;
    return begin >= region_begin && begin <= region_end &&
           size <= region_end - begin;
}

[[nodiscard]] bool bytes_match(
    const void* const address,
    const std::span<const std::uint8_t> expected) noexcept {
    return readable_range(address, expected.size()) &&
           std::memcmp(address, expected.data(), expected.size()) == 0;
}

[[nodiscard]] bool make_relative_call(
    const std::uintptr_t source,
    const std::uintptr_t destination,
    std::array<std::uint8_t, kCallInstructionSize>* const output) noexcept {
    if (output == nullptr) {
        return false;
    }
    const std::int64_t displacement =
        static_cast<std::int64_t>(destination) -
        static_cast<std::int64_t>(source + kCallInstructionSize);
    if (displacement < std::numeric_limits<std::int32_t>::min() ||
        displacement > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    (*output)[0] = 0xE8;
    const auto encoded = static_cast<std::int32_t>(displacement);
    std::memcpy(output->data() + 1, &encoded, sizeof(encoded));
    return true;
}

[[nodiscard]] std::uintptr_t decode_relative_call_target(
    const std::uintptr_t source,
    const std::array<std::uint8_t, kCallInstructionSize>& bytes) noexcept {
    std::int32_t displacement = 0;
    std::memcpy(&displacement, bytes.data() + 1, sizeof(displacement));
    return static_cast<std::uintptr_t>(
        static_cast<std::int64_t>(source + kCallInstructionSize) +
        displacement);
}

enum class PatchResult : std::uint8_t {
    ok,
    thread_suspend_failed,
    expected_bytes_changed,
    target_protection_failed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
};

[[nodiscard]] PatchResult replace_call_bytes(
    const std::uintptr_t target_address,
    const std::array<std::uint8_t, kCallInstructionSize>& expected,
    const std::array<std::uint8_t, kCallInstructionSize>& replacement,
    const std::uintptr_t execution_guard,
    const std::size_t execution_guard_size,
    DWORD* const system_error) noexcept {
    const std::array<PeerThreadPatchRange, 2> patch_ranges{{
        {target_address, kCallInstructionSize},
        {execution_guard, execution_guard_size},
    }};
    SuspendedPeerThreads suspended;
    PeerThreadQuiesceResult quiesce{};
    if (!suspended.suspend(patch_ranges, &quiesce)) {
        if (system_error != nullptr) {
            *system_error = quiesce.system_error;
        }
        return PatchResult::thread_suspend_failed;
    }

    auto* const target = reinterpret_cast<std::uint8_t*>(target_address);
    if (!bytes_match(target, expected)) {
        return PatchResult::expected_bytes_changed;
    }
    DWORD old_protection = 0;
    if (!VirtualProtect(
            target, replacement.size(), PAGE_EXECUTE_READWRITE,
            &old_protection)) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        return PatchResult::target_protection_failed;
    }

    if (std::memcmp(target, expected.data(), expected.size()) != 0) {
        DWORD ignored = 0;
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return PatchResult::expected_bytes_changed;
    }
    std::memcpy(target, replacement.data(), replacement.size());
    if (std::memcmp(target, replacement.data(), replacement.size()) != 0) {
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
        DWORD ignored = 0;
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return PatchResult::patch_write_failed;
    }
    if (!FlushInstructionCache(
            GetCurrentProcess(), target, replacement.size())) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
        DWORD ignored = 0;
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return PatchResult::patch_cache_flush_failed;
    }

    DWORD ignored = 0;
    if (!VirtualProtect(
            target, replacement.size(), old_protection, &ignored)) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return PatchResult::protection_restore_failed;
    }
    return PatchResult::ok;
}

[[nodiscard]] T4HudPlacementDrawHookStatus install_status_from_patch(
    const PatchResult result) noexcept {
    switch (result) {
    case PatchResult::ok: return T4HudPlacementDrawHookStatus::installed;
    case PatchResult::thread_suspend_failed:
        return T4HudPlacementDrawHookStatus::thread_suspend_failed;
    case PatchResult::expected_bytes_changed:
        return T4HudPlacementDrawHookStatus::expected_bytes_changed;
    case PatchResult::target_protection_failed:
        return T4HudPlacementDrawHookStatus::target_protection_failed;
    case PatchResult::patch_write_failed:
        return T4HudPlacementDrawHookStatus::patch_write_failed;
    case PatchResult::patch_cache_flush_failed:
        return T4HudPlacementDrawHookStatus::patch_cache_flush_failed;
    case PatchResult::protection_restore_failed:
        return T4HudPlacementDrawHookStatus::protection_restore_failed;
    }
    return T4HudPlacementDrawHookStatus::patch_write_failed;
}

[[nodiscard]] T4HudPlacementDrawHookRestoreStatus restore_status_from_patch(
    const PatchResult result) noexcept {
    switch (result) {
    case PatchResult::ok:
        return T4HudPlacementDrawHookRestoreStatus::restored;
    case PatchResult::thread_suspend_failed:
        return T4HudPlacementDrawHookRestoreStatus::thread_suspend_failed;
    case PatchResult::expected_bytes_changed:
        return T4HudPlacementDrawHookRestoreStatus::foreign_patch_preserved;
    case PatchResult::target_protection_failed:
        return T4HudPlacementDrawHookRestoreStatus::target_protection_failed;
    case PatchResult::patch_write_failed:
        return T4HudPlacementDrawHookRestoreStatus::patch_write_failed;
    case PatchResult::patch_cache_flush_failed:
        return T4HudPlacementDrawHookRestoreStatus::patch_cache_flush_failed;
    case PatchResult::protection_restore_failed:
        return T4HudPlacementDrawHookRestoreStatus::protection_restore_failed;
    }
    return T4HudPlacementDrawHookRestoreStatus::patch_write_failed;
}

}  // namespace

// The exact stock target is published before the callsite is patched and kept
// valid for the process lifetime. A WinMain thread may already be nested inside
// the placement service when another thread restores the callsite; retaining
// this address lets that in-flight bridge finish its tail jump safely.
extern "C" std::uintptr_t wawvr_t4_original_cg_draw_2d_address = 0;

extern "C" void __cdecl wawvr_t4_hud_placement_draw_service() noexcept {
    if (g_service_enabled.load(std::memory_order_acquire)) {
        static_cast<void>(service_t4_hud_placement_before_cg_draw_2d());
    }
}

#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) void wawvr_t4_cg_draw_2d_bridge() noexcept {
    __asm {
        pushfd
        pushad
        call wawvr_t4_hud_placement_draw_service
        popad
        popfd
        jmp dword ptr [wawvr_t4_original_cg_draw_2d_address]
    }
}
#endif

T4HudPlacementDrawHookInstallResult install_t4_hud_placement_draw_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    T4HudPlacementDrawHookInstallResult result{};
    if (g_installed.load(std::memory_order_acquire)) {
        result.status = T4HudPlacementDrawHookStatus::already_installed;
        result.target = g_callsite;
        result.original = wawvr_t4_original_cg_draw_2d_address;
        return result;
    }
    if (disabled_by_environment()) {
        result.status = T4HudPlacementDrawHookStatus::disabled_by_environment;
        return result;
    }
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    result.status =
        T4HudPlacementDrawHookStatus::unsupported_compiler_or_architecture;
    return result;
#else
    const auto prepared = wawvr::t4::prepare_inline_hook(
        bindings, wawvr::t4::HookSiteId::cg_draw_2d_call,
        reinterpret_cast<std::uintptr_t>(&wawvr_t4_cg_draw_2d_bridge));
    if (!prepared.ok() ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::cg_draw_2d_call_context_sentinel)) {
        result.status = T4HudPlacementDrawHookStatus::preparation_failed;
        return result;
    }
    result.target = prepared.hook->target;
    std::array<std::uint8_t, kCallInstructionSize> original_call{};
    if (prepared.hook->expected_size == original_call.size()) {
        std::copy_n(
            prepared.hook->expected.begin(), original_call.size(),
            original_call.begin());
    }
    const std::uintptr_t original = decode_relative_call_target(
        prepared.hook->target, original_call);
    const auto module_begin = reinterpret_cast<std::uintptr_t>(
        bindings.module().base);
    if (prepared.hook->expected_size != original_call.size() ||
        original_call[0] != 0xE8 || original < module_begin ||
        original - module_begin >= bindings.module().size) {
        result.status = T4HudPlacementDrawHookStatus::original_target_mismatch;
        return result;
    }
    result.original = original;

    std::array<std::uint8_t, kCallInstructionSize> replacement{};
    if (!make_relative_call(
            prepared.hook->target,
            reinterpret_cast<std::uintptr_t>(&wawvr_t4_cg_draw_2d_bridge),
            &replacement)) {
        result.status = T4HudPlacementDrawHookStatus::jump_out_of_range;
        return result;
    }

    wawvr_t4_original_cg_draw_2d_address = original;
    g_original_call = original_call;
    DWORD system_error = 0;
    const PatchResult patch = replace_call_bytes(
        prepared.hook->target, original_call, replacement, 0, 0,
        &system_error);
    result.status = install_status_from_patch(patch);
    result.system_error = system_error;
    if (patch != PatchResult::ok) {
        wawvr_t4_original_cg_draw_2d_address = 0;
        g_original_call = {};
        return result;
    }

    g_callsite = prepared.hook->target;
    g_replacement_call = replacement;
    g_installed.store(true, std::memory_order_release);
    g_service_enabled.store(true, std::memory_order_release);
    return result;
#endif
}

void request_t4_hud_placement_draw_hook_shutdown() noexcept {
    g_service_enabled.store(false, std::memory_order_release);
}

T4HudPlacementDrawHookRestoreStatus
restore_t4_hud_placement_draw_hook() noexcept {
    request_t4_hud_placement_draw_hook_shutdown();
    if (!g_installed.load(std::memory_order_acquire)) {
        return T4HudPlacementDrawHookRestoreStatus::not_installed;
    }
    DWORD system_error = 0;
    const PatchResult patch = replace_call_bytes(
        g_callsite, g_replacement_call, g_original_call,
        reinterpret_cast<std::uintptr_t>(&wawvr_t4_cg_draw_2d_bridge),
        kBridgeInstructionSize, &system_error);
    const T4HudPlacementDrawHookRestoreStatus status =
        restore_status_from_patch(patch);
    if (patch == PatchResult::ok ||
        patch == PatchResult::expected_bytes_changed) {
        g_installed.store(false, std::memory_order_release);
        if (patch == PatchResult::ok) {
            g_original_call = {};
            g_replacement_call = {};
            g_callsite = 0;
        }
    }
    return status;
}

bool t4_hud_placement_draw_hook_installed() noexcept {
    return g_installed.load(std::memory_order_acquire);
}

const char* t4_hud_placement_draw_hook_status_name(
    const T4HudPlacementDrawHookStatus status) noexcept {
    switch (status) {
    case T4HudPlacementDrawHookStatus::installed: return "installed";
    case T4HudPlacementDrawHookStatus::already_installed:
        return "already-installed";
    case T4HudPlacementDrawHookStatus::placement_not_bound:
        return "placement-not-bound";
    case T4HudPlacementDrawHookStatus::disabled_by_environment:
        return "disabled-by-environment";
    case T4HudPlacementDrawHookStatus::unsupported_compiler_or_architecture:
        return "unsupported-compiler-or-architecture";
    case T4HudPlacementDrawHookStatus::rejected_wrong_profile:
        return "rejected-wrong-profile";
    case T4HudPlacementDrawHookStatus::preparation_failed:
        return "preparation-failed";
    case T4HudPlacementDrawHookStatus::original_target_mismatch:
        return "original-target-mismatch";
    case T4HudPlacementDrawHookStatus::jump_out_of_range:
        return "jump-out-of-range";
    case T4HudPlacementDrawHookStatus::thread_suspend_failed:
        return "thread-suspend-failed";
    case T4HudPlacementDrawHookStatus::expected_bytes_changed:
        return "expected-bytes-changed";
    case T4HudPlacementDrawHookStatus::target_protection_failed:
        return "target-protection-failed";
    case T4HudPlacementDrawHookStatus::patch_write_failed:
        return "patch-write-failed";
    case T4HudPlacementDrawHookStatus::patch_cache_flush_failed:
        return "patch-cache-flush-failed";
    case T4HudPlacementDrawHookStatus::protection_restore_failed:
        return "protection-restore-failed";
    }
    return "unknown";
}

const char* t4_hud_placement_draw_hook_restore_status_name(
    const T4HudPlacementDrawHookRestoreStatus status) noexcept {
    switch (status) {
    case T4HudPlacementDrawHookRestoreStatus::restored: return "restored";
    case T4HudPlacementDrawHookRestoreStatus::not_installed:
        return "not-installed";
    case T4HudPlacementDrawHookRestoreStatus::foreign_patch_preserved:
        return "foreign-patch-preserved";
    case T4HudPlacementDrawHookRestoreStatus::thread_suspend_failed:
        return "thread-suspend-failed";
    case T4HudPlacementDrawHookRestoreStatus::target_protection_failed:
        return "target-protection-failed";
    case T4HudPlacementDrawHookRestoreStatus::patch_write_failed:
        return "patch-write-failed";
    case T4HudPlacementDrawHookRestoreStatus::patch_cache_flush_failed:
        return "patch-cache-flush-failed";
    case T4HudPlacementDrawHookRestoreStatus::protection_restore_failed:
        return "protection-restore-failed";
    }
    return "unknown";
}

}  // namespace wawvr::mod
