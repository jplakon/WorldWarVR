#include "weapon_camera_patch.hpp"

#include "peer_thread_quiescence.hpp"

#include "t4/profile.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace wawvr::mod {
namespace {

std::atomic<bool> g_installed{false};
std::uintptr_t g_callsite{};
std::array<std::uint8_t, 5> g_weapon_original_call{};
std::atomic<bool> g_melee_installed{false};
std::uintptr_t g_melee_branch{};
std::array<std::uint8_t, 6> g_melee_original_branch{};
std::array<std::uint8_t, 6> g_melee_replacement{};

[[nodiscard]] bool disabled_by_environment() noexcept {
    std::array<wchar_t, 8> value{};
    const DWORD length = GetEnvironmentVariableW(
        L"WAWVR_DISABLE_WEAPON_CAMERA_PATCH", value.data(),
        static_cast<DWORD>(value.size()));
    return length != 0 && length < value.size() &&
           (value[0] == L'1' || value[0] == L'y' || value[0] == L'Y' ||
            value[0] == L't' || value[0] == L'T');
}

[[nodiscard]] bool melee_disabled_by_environment() noexcept {
    std::array<wchar_t, 8> value{};
    const DWORD length = GetEnvironmentVariableW(
        L"WAWVR_DISABLE_MELEE_CAMERA_PATCH", value.data(),
        static_cast<DWORD>(value.size()));
    return length != 0 && length < value.size() &&
           (value[0] == L'1' || value[0] == L'y' || value[0] == L'Y' ||
            value[0] == L't' || value[0] == L'T');
}

[[nodiscard]] bool readable_range(
    const void* const address, const std::size_t size) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    const DWORD access = memory.Protect & 0xffU;
    const bool readable =
        access == PAGE_READONLY || access == PAGE_READWRITE ||
        access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
        access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
    if (!readable) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto region_begin =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto region_end = region_begin + memory.RegionSize;
    return begin >= region_begin && begin <= region_end &&
           size <= region_end - begin;
}

template <std::size_t Size>
[[nodiscard]] bool bytes_match(
    const void* const address,
    const std::array<std::uint8_t, Size>& expected) noexcept {
    return readable_range(address, expected.size()) &&
           std::memcmp(address, expected.data(), expected.size()) == 0;
}

enum class ExactPatchResult : std::uint8_t {
    ok,
    thread_suspend_failed,
    expected_bytes_changed,
    target_protection_failed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
};

template <std::size_t Size>
[[nodiscard]] ExactPatchResult replace_exact_bytes(
    const std::uintptr_t target_address,
    const std::array<std::uint8_t, Size>& expected,
    const std::array<std::uint8_t, Size>& replacement,
    DWORD* const system_error) noexcept {
    SuspendedPeerThreads suspended;
    PeerThreadQuiesceResult quiesce{};
    if (!suspended.suspend(
            {target_address, replacement.size()}, &quiesce)) {
        if (system_error != nullptr) {
            *system_error = quiesce.system_error;
        }
        return ExactPatchResult::thread_suspend_failed;
    }

    auto* const target = reinterpret_cast<std::uint8_t*>(target_address);
    if (!bytes_match(target, expected)) {
        return ExactPatchResult::expected_bytes_changed;
    }

    DWORD old_protection = 0;
    if (!VirtualProtect(
            target, replacement.size(), PAGE_EXECUTE_READWRITE,
            &old_protection)) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        return ExactPatchResult::target_protection_failed;
    }

    const auto restore_protection = [&]() noexcept -> bool {
        DWORD ignored = 0;
        return VirtualProtect(
                   target, replacement.size(), old_protection, &ignored) != FALSE;
    };
    const auto rollback = [&]() noexcept {
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
    };

    if (std::memcmp(target, expected.data(), expected.size()) != 0) {
        if (!restore_protection() && system_error != nullptr) {
            *system_error = GetLastError();
            return ExactPatchResult::protection_restore_failed;
        }
        return ExactPatchResult::expected_bytes_changed;
    }

    std::memcpy(target, replacement.data(), replacement.size());
    if (std::memcmp(target, replacement.data(), replacement.size()) != 0) {
        rollback();
        if (!restore_protection()) {
            if (system_error != nullptr) {
                *system_error = GetLastError();
            }
            return ExactPatchResult::protection_restore_failed;
        }
        return ExactPatchResult::patch_write_failed;
    }
    if (!FlushInstructionCache(
            GetCurrentProcess(), target, replacement.size())) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        rollback();
        if (!restore_protection()) {
            if (system_error != nullptr) {
                *system_error = GetLastError();
            }
            return ExactPatchResult::protection_restore_failed;
        }
        return ExactPatchResult::patch_cache_flush_failed;
    }
    if (!restore_protection()) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        // The page should still be writable; return to stock semantics before
        // making one best-effort protection restoration attempt.
        rollback();
        DWORD ignored = 0;
        VirtualProtect(
            target, replacement.size(), old_protection, &ignored);
        return ExactPatchResult::protection_restore_failed;
    }
    return ExactPatchResult::ok;
}

[[nodiscard]] WeaponCameraPatchStatus map_patch_failure(
    const ExactPatchResult result) noexcept {
    switch (result) {
    case ExactPatchResult::ok:
        return WeaponCameraPatchStatus::applied;
    case ExactPatchResult::thread_suspend_failed:
        return WeaponCameraPatchStatus::thread_suspend_failed;
    case ExactPatchResult::expected_bytes_changed:
        return WeaponCameraPatchStatus::expected_bytes_changed;
    case ExactPatchResult::target_protection_failed:
        return WeaponCameraPatchStatus::target_protection_failed;
    case ExactPatchResult::patch_write_failed:
        return WeaponCameraPatchStatus::patch_write_failed;
    case ExactPatchResult::patch_cache_flush_failed:
        return WeaponCameraPatchStatus::patch_cache_flush_failed;
    case ExactPatchResult::protection_restore_failed:
        return WeaponCameraPatchStatus::protection_restore_failed;
    }
    return WeaponCameraPatchStatus::patch_write_failed;
}

[[nodiscard]] MeleeCameraPatchStatus map_melee_patch_failure(
    const ExactPatchResult result) noexcept {
    switch (result) {
    case ExactPatchResult::ok:
        return MeleeCameraPatchStatus::applied;
    case ExactPatchResult::thread_suspend_failed:
        return MeleeCameraPatchStatus::thread_suspend_failed;
    case ExactPatchResult::expected_bytes_changed:
        return MeleeCameraPatchStatus::expected_bytes_changed;
    case ExactPatchResult::target_protection_failed:
        return MeleeCameraPatchStatus::target_protection_failed;
    case ExactPatchResult::patch_write_failed:
        return MeleeCameraPatchStatus::patch_write_failed;
    case ExactPatchResult::patch_cache_flush_failed:
        return MeleeCameraPatchStatus::patch_cache_flush_failed;
    case ExactPatchResult::protection_restore_failed:
        return MeleeCameraPatchStatus::protection_restore_failed;
    }
    return MeleeCameraPatchStatus::patch_write_failed;
}

}  // namespace

WeaponCameraPatchResult install_weapon_camera_patch(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    WeaponCameraPatchResult result{};
    if (disabled_by_environment()) {
        result.status = WeaponCameraPatchStatus::disabled_by_environment;
        return result;
    }
    if (g_installed.load(std::memory_order_acquire)) {
        result.status = WeaponCameraPatchStatus::already_applied;
        result.target = g_callsite;
        return result;
    }

    const auto callsite = bindings.site_address(
        wawvr::t4::HookSiteId::viewmodel_camera_tag_matrix_call);
    if (!callsite.has_value()) {
        result.status = WeaponCameraPatchStatus::address_out_of_range;
        return result;
    }
    result.target = *callsite;
    if (!bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::viewmodel_camera_tag_matrix_context_sentinel)) {
        result.status = WeaponCameraPatchStatus::context_sentinel_mismatch;
        return result;
    }
    if (!bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::viewmodel_camera_tag_matrix_call)) {
        result.status = WeaponCameraPatchStatus::expected_bytes_changed;
        return result;
    }
    const auto* const call_site = bindings.site(
        wawvr::t4::HookSiteId::viewmodel_camera_tag_matrix_call);
    if (call_site == nullptr ||
        call_site->expected_size != g_weapon_original_call.size() ||
        call_site->expected[0] != 0xE8) {
        result.status = WeaponCameraPatchStatus::expected_bytes_changed;
        return result;
    }
    std::copy_n(
        call_site->expected.begin(), g_weapon_original_call.size(),
        g_weapon_original_call.begin());

    DWORD system_error = 0;
    const ExactPatchResult patch = replace_exact_bytes(
        *callsite, g_weapon_original_call,
        kSuppressWeaponCameraTagMatrix, &system_error);
    result.system_error = system_error;
    result.status = map_patch_failure(patch);
    if (patch != ExactPatchResult::ok) {
        g_weapon_original_call = {};
        return result;
    }

    g_callsite = *callsite;
    g_installed.store(true, std::memory_order_release);
    result.status = WeaponCameraPatchStatus::applied;
    return result;
}

WeaponCameraPatchResult restore_weapon_camera_patch() noexcept {
    WeaponCameraPatchResult result{};
    result.target = g_callsite;
    if (!g_installed.load(std::memory_order_acquire) || g_callsite == 0) {
        result.status = WeaponCameraPatchStatus::not_installed;
        return result;
    }

    const auto* const target = reinterpret_cast<const std::uint8_t*>(g_callsite);
    if (bytes_match(target, g_weapon_original_call)) {
        g_installed.store(false, std::memory_order_release);
        result.status = WeaponCameraPatchStatus::already_restored;
        return result;
    }
    if (!bytes_match(target, kSuppressWeaponCameraTagMatrix)) {
        result.status = WeaponCameraPatchStatus::foreign_patch_preserved;
        return result;
    }

    DWORD system_error = 0;
    const ExactPatchResult patch = replace_exact_bytes(
        g_callsite, kSuppressWeaponCameraTagMatrix,
        g_weapon_original_call, &system_error);
    result.system_error = system_error;
    if (patch != ExactPatchResult::ok) {
        result.status = map_patch_failure(patch);
        return result;
    }
    g_installed.store(false, std::memory_order_release);
    g_weapon_original_call = {};
    g_callsite = 0;
    result.status = WeaponCameraPatchStatus::restored;
    return result;
}

MeleeCameraPatchResult install_melee_camera_patch(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    MeleeCameraPatchResult result{};
    if (melee_disabled_by_environment()) {
        result.status = MeleeCameraPatchStatus::disabled_by_environment;
        return result;
    }
    if (g_melee_installed.load(std::memory_order_acquire)) {
        result.status = MeleeCameraPatchStatus::already_applied;
        result.target = g_melee_branch;
        return result;
    }

    const auto branch = bindings.site_address(
        wawvr::t4::HookSiteId::auto_melee_enabled_branch);
    if (!branch.has_value()) {
        result.status = MeleeCameraPatchStatus::address_out_of_range;
        return result;
    }
    result.target = *branch;
    if (!bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::auto_melee_enabled_context_sentinel)) {
        result.status = MeleeCameraPatchStatus::context_sentinel_mismatch;
        return result;
    }
    if (!bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::auto_melee_enabled_branch)) {
        result.status = MeleeCameraPatchStatus::expected_bytes_changed;
        return result;
    }
    const auto* const branch_site = bindings.site(
        wawvr::t4::HookSiteId::auto_melee_enabled_branch);
    if (branch_site == nullptr ||
        branch_site->expected_size != g_melee_original_branch.size() ||
        branch_site->expected[0] != 0x0F ||
        branch_site->expected[1] != 0x84) {
        result.status = MeleeCameraPatchStatus::expected_bytes_changed;
        return result;
    }
    std::copy_n(
        branch_site->expected.begin(), g_melee_original_branch.size(),
        g_melee_original_branch.begin());
    std::int32_t conditional_displacement = 0;
    std::memcpy(
        &conditional_displacement, g_melee_original_branch.data() + 2,
        sizeof(conditional_displacement));
    if (conditional_displacement ==
        std::numeric_limits<std::int32_t>::max()) {
        result.status = MeleeCameraPatchStatus::expected_bytes_changed;
        g_melee_original_branch = {};
        return result;
    }
    const std::int32_t jump_displacement = conditional_displacement + 1;
    g_melee_replacement = {0xE9, 0, 0, 0, 0, 0x90};
    std::memcpy(
        g_melee_replacement.data() + 1, &jump_displacement,
        sizeof(jump_displacement));

    DWORD system_error = 0;
    const ExactPatchResult patch = replace_exact_bytes(
        *branch, g_melee_original_branch,
        g_melee_replacement, &system_error);
    result.system_error = system_error;
    result.status = map_melee_patch_failure(patch);
    if (patch != ExactPatchResult::ok) {
        g_melee_original_branch = {};
        g_melee_replacement = {};
        return result;
    }

    g_melee_branch = *branch;
    g_melee_installed.store(true, std::memory_order_release);
    result.status = MeleeCameraPatchStatus::applied;
    return result;
}

MeleeCameraPatchResult restore_melee_camera_patch() noexcept {
    MeleeCameraPatchResult result{};
    result.target = g_melee_branch;
    if (!g_melee_installed.load(std::memory_order_acquire) ||
        g_melee_branch == 0) {
        result.status = MeleeCameraPatchStatus::not_installed;
        return result;
    }

    const auto* const target =
        reinterpret_cast<const std::uint8_t*>(g_melee_branch);
    if (bytes_match(target, g_melee_original_branch)) {
        g_melee_installed.store(false, std::memory_order_release);
        result.status = MeleeCameraPatchStatus::already_restored;
        return result;
    }
    if (!bytes_match(target, g_melee_replacement)) {
        result.status = MeleeCameraPatchStatus::foreign_patch_preserved;
        return result;
    }

    DWORD system_error = 0;
    const ExactPatchResult patch = replace_exact_bytes(
        g_melee_branch, g_melee_replacement,
        g_melee_original_branch, &system_error);
    result.system_error = system_error;
    if (patch != ExactPatchResult::ok) {
        result.status = map_melee_patch_failure(patch);
        return result;
    }
    g_melee_installed.store(false, std::memory_order_release);
    g_melee_original_branch = {};
    g_melee_replacement = {};
    g_melee_branch = 0;
    result.status = MeleeCameraPatchStatus::restored;
    return result;
}

bool weapon_camera_patch_installed() noexcept {
    return g_installed.load(std::memory_order_acquire);
}

bool melee_camera_patch_installed() noexcept {
    return g_melee_installed.load(std::memory_order_acquire);
}

const char* weapon_camera_patch_status_name(
    const WeaponCameraPatchStatus status) noexcept {
    switch (status) {
    case WeaponCameraPatchStatus::applied: return "applied";
    case WeaponCameraPatchStatus::already_applied: return "already-applied";
    case WeaponCameraPatchStatus::restored: return "restored";
    case WeaponCameraPatchStatus::already_restored: return "already-restored";
    case WeaponCameraPatchStatus::not_installed: return "not-installed";
    case WeaponCameraPatchStatus::disabled_by_environment: return "disabled-by-environment";
    case WeaponCameraPatchStatus::frame_boundary_unavailable: return "frame-boundary-unavailable";
    case WeaponCameraPatchStatus::rejected_wrong_profile: return "rejected-wrong-profile";
    case WeaponCameraPatchStatus::address_out_of_range: return "address-out-of-range";
    case WeaponCameraPatchStatus::context_sentinel_mismatch: return "context-sentinel-mismatch";
    case WeaponCameraPatchStatus::thread_suspend_failed: return "thread-suspend-failed";
    case WeaponCameraPatchStatus::target_protection_failed: return "target-protection-failed";
    case WeaponCameraPatchStatus::expected_bytes_changed: return "expected-bytes-changed";
    case WeaponCameraPatchStatus::patch_write_failed: return "patch-write-failed";
    case WeaponCameraPatchStatus::patch_cache_flush_failed: return "patch-cache-flush-failed";
    case WeaponCameraPatchStatus::protection_restore_failed: return "protection-restore-failed";
    case WeaponCameraPatchStatus::foreign_patch_preserved: return "foreign-patch-preserved";
    }
    return "unknown";
}

const char* melee_camera_patch_status_name(
    const MeleeCameraPatchStatus status) noexcept {
    switch (status) {
    case MeleeCameraPatchStatus::applied: return "applied";
    case MeleeCameraPatchStatus::already_applied: return "already-applied";
    case MeleeCameraPatchStatus::restored: return "restored";
    case MeleeCameraPatchStatus::already_restored: return "already-restored";
    case MeleeCameraPatchStatus::not_installed: return "not-installed";
    case MeleeCameraPatchStatus::disabled_by_environment: return "disabled-by-environment";
    case MeleeCameraPatchStatus::rejected_wrong_profile: return "rejected-wrong-profile";
    case MeleeCameraPatchStatus::address_out_of_range: return "address-out-of-range";
    case MeleeCameraPatchStatus::context_sentinel_mismatch: return "context-sentinel-mismatch";
    case MeleeCameraPatchStatus::thread_suspend_failed: return "thread-suspend-failed";
    case MeleeCameraPatchStatus::target_protection_failed: return "target-protection-failed";
    case MeleeCameraPatchStatus::expected_bytes_changed: return "expected-bytes-changed";
    case MeleeCameraPatchStatus::patch_write_failed: return "patch-write-failed";
    case MeleeCameraPatchStatus::patch_cache_flush_failed: return "patch-cache-flush-failed";
    case MeleeCameraPatchStatus::protection_restore_failed: return "protection-restore-failed";
    case MeleeCameraPatchStatus::foreign_patch_preserved: return "foreign-patch-preserved";
    }
    return "unknown";
}

}  // namespace wawvr::mod
