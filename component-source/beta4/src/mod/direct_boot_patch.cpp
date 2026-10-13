#include "direct_boot_patch.hpp"

#include <algorithm>
#include <cstring>

#if defined(WAWVR_HAS_T4_BINDINGS)
#include "t4_layout_selector.hpp"
#include "t4/bindings.hpp"
#include "t4/profile.hpp"

#include <windows.h>
#endif

namespace wawvr::mod {
namespace {

[[nodiscard]] std::array<std::uint8_t, kExpectedStartupIntroContext.size()>
patched_context() noexcept {
    auto patched = kExpectedStartupIntroContext;
    patched[kStartupIntroBranchOffset] = kStartupIntroUnconditionalBranch;
    return patched;
}

#if defined(WAWVR_HAS_T4_BINDINGS)
void snapshot_context(
    const std::uint8_t* context,
    DirectBootPatchResult& result) noexcept {
    std::memcpy(result.observed.data(), context, result.observed.size());
    result.observed_size = result.observed.size();
}

[[nodiscard]] bool restore_original_byte(
    volatile std::uint8_t* branch,
    DirectBootPatchResult& result) noexcept {
    *branch = kStartupIntroConditionalBranch;
    if (!FlushInstructionCache(GetCurrentProcess(),
                               const_cast<const std::uint8_t*>(branch), 1)) {
        result.system_error = GetLastError();
        result.status = DirectBootPatchStatus::rollback_failed;
        return false;
    }
    return true;
}
#endif

}  // namespace

DirectBootContextState inspect_direct_boot_context(
    const std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() != kExpectedStartupIntroContext.size()) {
        return DirectBootContextState::mismatch;
    }

    if (std::equal(bytes.begin(), bytes.end(),
                   kExpectedStartupIntroContext.begin())) {
        return DirectBootContextState::expected;
    }

    const auto patched = patched_context();
    if (std::equal(bytes.begin(), bytes.end(), patched.begin())) {
        return DirectBootContextState::already_patched;
    }

    return DirectBootContextState::mismatch;
}

const char* direct_boot_patch_status_name(
    const DirectBootPatchStatus status) noexcept {
    switch (status) {
    case DirectBootPatchStatus::applied:
        return "applied";
    case DirectBootPatchStatus::already_applied:
        return "already-applied";
    case DirectBootPatchStatus::not_applicable:
        return "not-applicable";
    case DirectBootPatchStatus::disabled_by_environment:
        return "disabled-by-environment";
    case DirectBootPatchStatus::rejected_wrong_profile:
        return "rejected-wrong-profile";
    case DirectBootPatchStatus::address_out_of_range:
        return "address-out-of-range";
    case DirectBootPatchStatus::memory_query_failed:
        return "memory-query-failed";
    case DirectBootPatchStatus::memory_not_patchable:
        return "memory-not-patchable";
    case DirectBootPatchStatus::unexpected_bytes:
        return "unexpected-bytes";
    case DirectBootPatchStatus::virtual_protect_failed:
        return "virtual-protect-failed";
    case DirectBootPatchStatus::expected_bytes_changed:
        return "expected-bytes-changed";
    case DirectBootPatchStatus::write_verification_failed:
        return "write-verification-failed";
    case DirectBootPatchStatus::instruction_cache_flush_failed:
        return "instruction-cache-flush-failed";
    case DirectBootPatchStatus::protection_restore_failed:
        return "protection-restore-failed";
    case DirectBootPatchStatus::rollback_failed:
        return "rollback-failed";
    }
    return "unknown";
}

#if defined(WAWVR_HAS_T4_BINDINGS)
DirectBootPatchResult install_direct_boot_patch(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    DirectBootPatchResult result{};

    const auto& bound_profile = bindings.profile();
    if (select_t4_layout_family(bound_profile) !=
        T4LayoutFamily::single_player_1_7_1263) {
        result.status = DirectBootPatchStatus::rejected_wrong_profile;
        return result;
    }

    const auto context_address = bindings.module().address(
        kStartupIntroContextRva, kExpectedStartupIntroContext.size());
    if (!context_address.has_value()) {
        result.status = DirectBootPatchStatus::address_out_of_range;
        return result;
    }

    const auto* const context =
        reinterpret_cast<const std::uint8_t*>(*context_address);
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(context, &memory, sizeof(memory)) != sizeof(memory)) {
        result.status = DirectBootPatchStatus::memory_query_failed;
        result.system_error = GetLastError();
        return result;
    }

    const auto context_value = reinterpret_cast<std::uintptr_t>(context);
    const auto region_value = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto region_offset = context_value >= region_value
                                   ? static_cast<std::size_t>(context_value - region_value)
                                   : memory.RegionSize;
    if (memory.State != MEM_COMMIT || (memory.Protect & PAGE_GUARD) != 0 ||
        (memory.Protect & PAGE_NOACCESS) != 0 || context_value < region_value ||
        region_offset > memory.RegionSize ||
        kExpectedStartupIntroContext.size() >
            memory.RegionSize - region_offset) {
        result.status = DirectBootPatchStatus::memory_not_patchable;
        return result;
    }

    snapshot_context(context, result);
    switch (inspect_direct_boot_context(result.observed)) {
    case DirectBootContextState::already_patched:
        result.status = DirectBootPatchStatus::already_applied;
        return result;
    case DirectBootContextState::mismatch:
        result.status = DirectBootPatchStatus::unexpected_bytes;
        return result;
    case DirectBootContextState::expected:
        break;
    }

    auto* const branch = const_cast<std::uint8_t*>(context) +
                         kStartupIntroBranchOffset;
    DWORD old_protection = 0;
    if (!VirtualProtect(branch, 1, PAGE_EXECUTE_READWRITE, &old_protection)) {
        result.status = DirectBootPatchStatus::virtual_protect_failed;
        result.system_error = GetLastError();
        return result;
    }

    snapshot_context(context, result);
    const auto state_after_protect = inspect_direct_boot_context(result.observed);
    if (state_after_protect != DirectBootContextState::expected) {
        DWORD ignored = 0;
        const bool restored = VirtualProtect(branch, 1, old_protection, &ignored) != FALSE;
        result.status = state_after_protect == DirectBootContextState::already_patched
                            ? DirectBootPatchStatus::already_applied
                            : DirectBootPatchStatus::expected_bytes_changed;
        if (!restored) {
            result.status = DirectBootPatchStatus::protection_restore_failed;
            result.system_error = GetLastError();
        }
        return result;
    }

    auto* const volatile_branch = reinterpret_cast<volatile std::uint8_t*>(branch);
    *volatile_branch = kStartupIntroUnconditionalBranch;
    snapshot_context(context, result);
    if (inspect_direct_boot_context(result.observed) !=
        DirectBootContextState::already_patched) {
        result.status = DirectBootPatchStatus::write_verification_failed;
        const bool byte_restored = restore_original_byte(volatile_branch, result);
        DWORD ignored = 0;
        if (!VirtualProtect(branch, 1, old_protection, &ignored)) {
            result.status = DirectBootPatchStatus::protection_restore_failed;
            result.system_error = GetLastError();
        } else if (!byte_restored) {
            result.status = DirectBootPatchStatus::rollback_failed;
        }
        snapshot_context(context, result);
        return result;
    }

    if (!FlushInstructionCache(GetCurrentProcess(), branch, 1)) {
        result.status = DirectBootPatchStatus::instruction_cache_flush_failed;
        result.system_error = GetLastError();
        const bool byte_restored = restore_original_byte(volatile_branch, result);
        DWORD ignored = 0;
        if (!VirtualProtect(branch, 1, old_protection, &ignored)) {
            result.status = DirectBootPatchStatus::protection_restore_failed;
            result.system_error = GetLastError();
        } else if (!byte_restored) {
            result.status = DirectBootPatchStatus::rollback_failed;
        }
        snapshot_context(context, result);
        return result;
    }

    DWORD ignored = 0;
    if (!VirtualProtect(branch, 1, old_protection, &ignored)) {
        result.status = DirectBootPatchStatus::protection_restore_failed;
        result.system_error = GetLastError();

        // The page should still be writable when restoration fails. Roll the
        // semantic change back and make a final best-effort protection restore
        // so a failed install does not silently leave a partial patch.
        const bool byte_restored = restore_original_byte(volatile_branch, result);
        DWORD retry_ignored = 0;
        const bool protection_restored =
            VirtualProtect(branch, 1, old_protection, &retry_ignored) != FALSE;
        if (!byte_restored || !protection_restored) {
            result.status = DirectBootPatchStatus::rollback_failed;
            if (!protection_restored) {
                result.system_error = GetLastError();
            }
        }
        snapshot_context(context, result);
        return result;
    }

    snapshot_context(context, result);
    // Keep the one-byte skip edge for the process lifetime. The guarded code is
    // a one-shot startup path; restoring from another thread without a proven
    // "initialization passed" boundary would introduce a race for no benefit.
    result.status = inspect_direct_boot_context(result.observed) ==
                            DirectBootContextState::already_patched
                        ? DirectBootPatchStatus::applied
                        : DirectBootPatchStatus::write_verification_failed;
    return result;
}
#endif

}  // namespace wawvr::mod
