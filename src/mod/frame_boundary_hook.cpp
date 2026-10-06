#include "frame_boundary_hook.hpp"

#include "performance_timing.hpp"
#include "peer_thread_quiescence.hpp"
#include "present_hook.hpp"
#include "t4_hud_placement.hpp"

#include "t4/hook_api.hpp"
#include "t4/profile.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace wawvr::mod {
namespace {

constexpr std::size_t kCallInstructionSize = 5;
using ComFrameFunction = void(__cdecl*)();

std::atomic<bool> g_installed{false};
std::atomic<bool> g_service_enabled{false};
std::atomic<ComFrameFunction> g_original_com_frame{nullptr};
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
    const void* const address,
    const std::size_t size) noexcept {
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
    DWORD* const system_error) noexcept {
    SuspendedPeerThreads suspended;
    PeerThreadQuiesceResult quiesce{};
    if (!suspended.suspend(
            {target_address, kCallInstructionSize}, &quiesce)) {
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

[[nodiscard]] FrameBoundaryHookStatus install_status_from_patch(
    const PatchResult result) noexcept {
    switch (result) {
    case PatchResult::ok:
        return FrameBoundaryHookStatus::installed;
    case PatchResult::thread_suspend_failed:
        return FrameBoundaryHookStatus::thread_suspend_failed;
    case PatchResult::expected_bytes_changed:
        return FrameBoundaryHookStatus::expected_bytes_changed;
    case PatchResult::target_protection_failed:
        return FrameBoundaryHookStatus::target_protection_failed;
    case PatchResult::patch_write_failed:
        return FrameBoundaryHookStatus::patch_write_failed;
    case PatchResult::patch_cache_flush_failed:
        return FrameBoundaryHookStatus::patch_cache_flush_failed;
    case PatchResult::protection_restore_failed:
        return FrameBoundaryHookStatus::protection_restore_failed;
    }
    return FrameBoundaryHookStatus::patch_write_failed;
}

[[nodiscard]] FrameBoundaryRestoreStatus restore_status_from_patch(
    const PatchResult result) noexcept {
    switch (result) {
    case PatchResult::ok:
        return FrameBoundaryRestoreStatus::restored;
    case PatchResult::thread_suspend_failed:
        return FrameBoundaryRestoreStatus::thread_suspend_failed;
    case PatchResult::expected_bytes_changed:
        return FrameBoundaryRestoreStatus::foreign_patch_preserved;
    case PatchResult::target_protection_failed:
        return FrameBoundaryRestoreStatus::target_protection_failed;
    case PatchResult::patch_write_failed:
        return FrameBoundaryRestoreStatus::patch_write_failed;
    case PatchResult::patch_cache_flush_failed:
        return FrameBoundaryRestoreStatus::patch_cache_flush_failed;
    case PatchResult::protection_restore_failed:
        return FrameBoundaryRestoreStatus::protection_restore_failed;
    }
    return FrameBoundaryRestoreStatus::patch_write_failed;
}

} // namespace

extern "C" void __cdecl wawvr_com_frame_bridge() noexcept {
    const bool timing_enabled = performance_timing_diagnostics_enabled();
    std::chrono::steady_clock::time_point bridge_started{};
    static thread_local std::chrono::steady_clock::time_point
        previous_entry{};
    static thread_local std::chrono::steady_clock::time_point
        previous_exit{};
    if (timing_enabled) {
        begin_performance_frame();
        const auto current_entry = std::chrono::steady_clock::now();
        bridge_started = current_entry;
        if (previous_entry != std::chrono::steady_clock::time_point{}) {
            record_performance_timing(
                PerformanceTimingPhase::com_frame_interval,
                current_entry - previous_entry);
        }
        if (previous_exit != std::chrono::steady_clock::time_point{}) {
            record_performance_timing(
                PerformanceTimingPhase::com_frame_outside_bridge_gap,
                current_entry - previous_exit);
        }
        previous_entry = current_entry;
    }
    const auto original = g_original_com_frame.load(std::memory_order_acquire);
    if (original == nullptr) {
        if (timing_enabled) {
            finish_performance_frame();
        }
        return;
    }
    if (g_service_enabled.load(std::memory_order_acquire)) {
        // This is the validated WinMain thread and still precedes every piece
        // of Com_Frame work. Refresh even when the target dimensions are
        // unchanged so renderer Reset or vid_restart cannot leave stale
        // placement state. A second independently reversible boundary at the
        // sole CG_Draw2D call refreshes again from same-frame catcher state.
        static_cast<void>(service_t4_hud_placement_before_com_frame());
    }
    {
        ScopedPerformanceTiming original_timing(
            PerformanceTimingPhase::com_frame_original);
        original();
    }
    if (g_service_enabled.load(std::memory_order_acquire)) {
        service_present_hook_after_com_frame();
    }
    if (timing_enabled) {
        const auto bridge_finished = std::chrono::steady_clock::now();
        record_performance_timing(
            PerformanceTimingPhase::com_frame_bridge_total,
            bridge_finished - bridge_started);
        previous_exit = bridge_finished;
        finish_performance_frame();
        report_performance_timing_if_ready();
    }
}

FrameBoundaryHookInstallResult install_frame_boundary_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    FrameBoundaryHookInstallResult result{};
    if (g_installed.load(std::memory_order_acquire)) {
        result.status = FrameBoundaryHookStatus::already_installed;
        result.target = g_callsite;
        result.original = reinterpret_cast<std::uintptr_t>(
            g_original_com_frame.load(std::memory_order_acquire));
        return result;
    }
    if (disabled_by_environment()) {
        result.status = FrameBoundaryHookStatus::disabled_by_environment;
        return result;
    }
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    result.status =
        FrameBoundaryHookStatus::unsupported_compiler_or_architecture;
    return result;
#else
    const auto prepared = wawvr::t4::prepare_inline_hook(
        bindings, wawvr::t4::HookSiteId::main_loop_com_frame_call,
        reinterpret_cast<std::uintptr_t>(&wawvr_com_frame_bridge));
    if (!prepared.ok() ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::main_loop_com_frame_context_sentinel) ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::com_frame_entry_sentinel)) {
        result.status = FrameBoundaryHookStatus::preparation_failed;
        return result;
    }
    result.target = prepared.hook->target;
    const auto original = bindings.site_address(
        wawvr::t4::HookSiteId::com_frame_entry_sentinel);
    std::array<std::uint8_t, kCallInstructionSize> original_call{};
    if (prepared.hook->expected_size == original_call.size()) {
        std::copy_n(
            prepared.hook->expected.begin(), original_call.size(),
            original_call.begin());
    }
    if (!original.has_value() ||
        prepared.hook->expected_size != original_call.size() ||
        original_call[0] != 0xE8 ||
        decode_relative_call_target(
            prepared.hook->target, original_call) != *original) {
        result.status = FrameBoundaryHookStatus::original_target_mismatch;
        return result;
    }
    result.original = *original;

    std::array<std::uint8_t, kCallInstructionSize> replacement{};
    if (!make_relative_call(
            prepared.hook->target,
            reinterpret_cast<std::uintptr_t>(&wawvr_com_frame_bridge),
            &replacement)) {
        result.status = FrameBoundaryHookStatus::jump_out_of_range;
        return result;
    }

    g_original_com_frame.store(
        reinterpret_cast<ComFrameFunction>(*original),
        std::memory_order_release);
    g_original_call = original_call;
    DWORD system_error = 0;
    const PatchResult patch = replace_call_bytes(
        prepared.hook->target, original_call, replacement, &system_error);
    result.status = install_status_from_patch(patch);
    result.system_error = system_error;
    if (patch != PatchResult::ok) {
        g_original_com_frame.store(nullptr, std::memory_order_release);
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

void request_frame_boundary_hook_shutdown() noexcept {
    g_service_enabled.store(false, std::memory_order_release);
}

FrameBoundaryRestoreStatus restore_frame_boundary_hook() noexcept {
    request_frame_boundary_hook_shutdown();
    if (!g_installed.load(std::memory_order_acquire)) {
        return FrameBoundaryRestoreStatus::not_installed;
    }
    DWORD system_error = 0;
    const PatchResult patch = replace_call_bytes(
        g_callsite, g_replacement_call, g_original_call, &system_error);
    const FrameBoundaryRestoreStatus status =
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

bool frame_boundary_hook_installed() noexcept {
    return g_installed.load(std::memory_order_acquire);
}

const char* frame_boundary_hook_status_name(
    const FrameBoundaryHookStatus status) noexcept {
    switch (status) {
    case FrameBoundaryHookStatus::installed: return "installed";
    case FrameBoundaryHookStatus::already_installed: return "already-installed";
    case FrameBoundaryHookStatus::disabled_by_environment: return "disabled-by-environment";
    case FrameBoundaryHookStatus::unsupported_compiler_or_architecture: return "unsupported-compiler-or-architecture";
    case FrameBoundaryHookStatus::rejected_wrong_profile: return "rejected-wrong-profile";
    case FrameBoundaryHookStatus::preparation_failed: return "preparation-failed";
    case FrameBoundaryHookStatus::original_target_mismatch: return "original-target-mismatch";
    case FrameBoundaryHookStatus::jump_out_of_range: return "jump-out-of-range";
    case FrameBoundaryHookStatus::thread_suspend_failed: return "thread-suspend-failed";
    case FrameBoundaryHookStatus::expected_bytes_changed: return "expected-bytes-changed";
    case FrameBoundaryHookStatus::target_protection_failed: return "target-protection-failed";
    case FrameBoundaryHookStatus::patch_write_failed: return "patch-write-failed";
    case FrameBoundaryHookStatus::patch_cache_flush_failed: return "patch-cache-flush-failed";
    case FrameBoundaryHookStatus::protection_restore_failed: return "protection-restore-failed";
    }
    return "unknown";
}

const char* frame_boundary_restore_status_name(
    const FrameBoundaryRestoreStatus status) noexcept {
    switch (status) {
    case FrameBoundaryRestoreStatus::restored: return "restored";
    case FrameBoundaryRestoreStatus::not_installed: return "not-installed";
    case FrameBoundaryRestoreStatus::foreign_patch_preserved: return "foreign-patch-preserved";
    case FrameBoundaryRestoreStatus::thread_suspend_failed: return "thread-suspend-failed";
    case FrameBoundaryRestoreStatus::target_protection_failed: return "target-protection-failed";
    case FrameBoundaryRestoreStatus::patch_write_failed: return "patch-write-failed";
    case FrameBoundaryRestoreStatus::patch_cache_flush_failed: return "patch-cache-flush-failed";
    case FrameBoundaryRestoreStatus::protection_restore_failed: return "protection-restore-failed";
    }
    return "unknown";
}

} // namespace wawvr::mod
