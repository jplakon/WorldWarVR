#include "oversized_resolution_patch.hpp"

#include <algorithm>
#include <cstring>

#if defined(WAWVR_HAS_T4_BINDINGS)
#include "peer_thread_quiescence.hpp"
#include "t4_layout_selector.hpp"
#include "t4/bindings.hpp"

#include <windows.h>
#endif

namespace wawvr::mod {
namespace {

[[nodiscard]] std::array<std::uint8_t,
                         kExpectedCustomResolutionContext.size()>
patched_context() noexcept {
    auto patched = kExpectedCustomResolutionContext;
    std::copy(kCustomResolutionBranchNops.begin(),
              kCustomResolutionBranchNops.end(),
              patched.begin() + kCustomResolutionWidthBranchOffset);
    std::copy(kCustomResolutionBranchNops.begin(),
              kCustomResolutionBranchNops.end(),
              patched.begin() + kCustomResolutionHeightBranchOffset);
    return patched;
}

#if defined(WAWVR_HAS_T4_BINDINGS)
void snapshot_context(
    const std::uint8_t* const context,
    OversizedResolutionPatchResult& result) noexcept {
    std::memcpy(result.observed.data(), context, result.observed.size());
    result.observed_size = result.observed.size();
}

[[nodiscard]] bool restore_original_branches(
    volatile std::uint8_t* const context,
    OversizedResolutionPatchResult& result) noexcept {
    context[kCustomResolutionWidthBranchOffset] =
        kCustomResolutionWidthRejectBranch[0];
    context[kCustomResolutionWidthBranchOffset + 1] =
        kCustomResolutionWidthRejectBranch[1];
    context[kCustomResolutionHeightBranchOffset] =
        kCustomResolutionHeightRejectBranch[0];
    context[kCustomResolutionHeightBranchOffset + 1] =
        kCustomResolutionHeightRejectBranch[1];
    if (!FlushInstructionCache(
            GetCurrentProcess(),
            const_cast<const std::uint8_t*>(context),
            kExpectedCustomResolutionContext.size())) {
        result.system_error = GetLastError();
        result.status = OversizedResolutionPatchStatus::rollback_failed;
        return false;
    }
    return true;
}
#endif

}  // namespace

OversizedResolutionContextState inspect_oversized_resolution_context(
    const std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() != kExpectedCustomResolutionContext.size()) {
        return OversizedResolutionContextState::mismatch;
    }
    if (std::equal(bytes.begin(), bytes.end(),
                   kExpectedCustomResolutionContext.begin())) {
        return OversizedResolutionContextState::expected;
    }
    const auto patched = patched_context();
    if (std::equal(bytes.begin(), bytes.end(), patched.begin())) {
        return OversizedResolutionContextState::already_patched;
    }
    return OversizedResolutionContextState::mismatch;
}

const char* oversized_resolution_patch_status_name(
    const OversizedResolutionPatchStatus status) noexcept {
    switch (status) {
    case OversizedResolutionPatchStatus::applied:
        return "applied";
    case OversizedResolutionPatchStatus::already_applied:
        return "already-applied";
    case OversizedResolutionPatchStatus::not_applicable:
        return "not-applicable";
    case OversizedResolutionPatchStatus::address_out_of_range:
        return "address-out-of-range";
    case OversizedResolutionPatchStatus::memory_query_failed:
        return "memory-query-failed";
    case OversizedResolutionPatchStatus::memory_not_patchable:
        return "memory-not-patchable";
    case OversizedResolutionPatchStatus::unexpected_bytes:
        return "unexpected-bytes";
    case OversizedResolutionPatchStatus::peer_thread_quiesce_failed:
        return "peer-thread-quiesce-failed";
    case OversizedResolutionPatchStatus::virtual_protect_failed:
        return "virtual-protect-failed";
    case OversizedResolutionPatchStatus::expected_bytes_changed:
        return "expected-bytes-changed";
    case OversizedResolutionPatchStatus::write_verification_failed:
        return "write-verification-failed";
    case OversizedResolutionPatchStatus::instruction_cache_flush_failed:
        return "instruction-cache-flush-failed";
    case OversizedResolutionPatchStatus::protection_restore_failed:
        return "protection-restore-failed";
    case OversizedResolutionPatchStatus::rollback_failed:
        return "rollback-failed";
    }
    return "unknown";
}

#if defined(WAWVR_HAS_T4_BINDINGS)
OversizedResolutionPatchResult install_oversized_resolution_patch(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    OversizedResolutionPatchResult result{};
    if (select_t4_layout_family(bindings.profile()) !=
        T4LayoutFamily::single_player_1_7_1263) {
        result.status = OversizedResolutionPatchStatus::not_applicable;
        return result;
    }

    const auto address = bindings.module().address(
        kCustomResolutionContextRva,
        kExpectedCustomResolutionContext.size());
    if (!address.has_value()) {
        result.status = OversizedResolutionPatchStatus::address_out_of_range;
        return result;
    }
    auto* const context = reinterpret_cast<std::uint8_t*>(*address);
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(context, &memory, sizeof(memory)) != sizeof(memory)) {
        result.status = OversizedResolutionPatchStatus::memory_query_failed;
        result.system_error = GetLastError();
        return result;
    }
    const auto context_value = reinterpret_cast<std::uintptr_t>(context);
    const auto region_value =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto region_offset =
        context_value >= region_value
            ? static_cast<std::size_t>(context_value - region_value)
            : memory.RegionSize;
    if (memory.State != MEM_COMMIT || (memory.Protect & PAGE_GUARD) != 0 ||
        (memory.Protect & PAGE_NOACCESS) != 0 ||
        context_value < region_value || region_offset > memory.RegionSize ||
        kExpectedCustomResolutionContext.size() >
            memory.RegionSize - region_offset) {
        result.status = OversizedResolutionPatchStatus::memory_not_patchable;
        return result;
    }

    snapshot_context(context, result);
    switch (inspect_oversized_resolution_context(result.observed)) {
    case OversizedResolutionContextState::already_patched:
        result.status = OversizedResolutionPatchStatus::already_applied;
        return result;
    case OversizedResolutionContextState::mismatch:
        result.status = OversizedResolutionPatchStatus::unexpected_bytes;
        return result;
    case OversizedResolutionContextState::expected:
        break;
    }

    constexpr std::size_t patch_span =
        kCustomResolutionHeightBranchOffset +
        kCustomResolutionBranchNops.size() -
        kCustomResolutionWidthBranchOffset;
    auto* const patch_begin =
        context + kCustomResolutionWidthBranchOffset;
    SuspendedPeerThreads suspended;
    PeerThreadQuiesceResult quiesce{};
    if (!suspended.suspend(
            PeerThreadPatchRange{
                reinterpret_cast<std::uintptr_t>(patch_begin), patch_span},
            &quiesce)) {
        result.status =
            OversizedResolutionPatchStatus::peer_thread_quiesce_failed;
        result.system_error = quiesce.system_error;
        result.thread_id = quiesce.thread_id;
        return result;
    }
    snapshot_context(context, result);
    if (inspect_oversized_resolution_context(result.observed) !=
        OversizedResolutionContextState::expected) {
        result.status =
            OversizedResolutionPatchStatus::expected_bytes_changed;
        return result;
    }
    DWORD old_protection = 0;
    if (!VirtualProtect(
            patch_begin, patch_span, PAGE_EXECUTE_READWRITE,
            &old_protection)) {
        result.status = OversizedResolutionPatchStatus::virtual_protect_failed;
        result.system_error = GetLastError();
        return result;
    }

    snapshot_context(context, result);
    const auto state_after_protect =
        inspect_oversized_resolution_context(result.observed);
    if (state_after_protect != OversizedResolutionContextState::expected) {
        DWORD ignored = 0;
        const bool restored =
            VirtualProtect(
                patch_begin, patch_span, old_protection, &ignored) != FALSE;
        result.status =
            state_after_protect ==
                    OversizedResolutionContextState::already_patched
                ? OversizedResolutionPatchStatus::already_applied
                : OversizedResolutionPatchStatus::expected_bytes_changed;
        if (!restored) {
            result.status =
                OversizedResolutionPatchStatus::protection_restore_failed;
            result.system_error = GetLastError();
        }
        return result;
    }

    auto* const volatile_context =
        reinterpret_cast<volatile std::uint8_t*>(context);
    for (std::size_t index = 0;
         index < kCustomResolutionBranchNops.size(); ++index) {
        volatile_context[kCustomResolutionWidthBranchOffset + index] =
            kCustomResolutionBranchNops[index];
        volatile_context[kCustomResolutionHeightBranchOffset + index] =
            kCustomResolutionBranchNops[index];
    }
    snapshot_context(context, result);
    if (inspect_oversized_resolution_context(result.observed) !=
        OversizedResolutionContextState::already_patched) {
        result.status =
            OversizedResolutionPatchStatus::write_verification_failed;
        const bool branches_restored =
            restore_original_branches(volatile_context, result);
        DWORD ignored = 0;
        if (!VirtualProtect(
                patch_begin, patch_span, old_protection, &ignored)) {
            result.status =
                OversizedResolutionPatchStatus::protection_restore_failed;
            result.system_error = GetLastError();
        } else if (!branches_restored) {
            result.status = OversizedResolutionPatchStatus::rollback_failed;
        }
        snapshot_context(context, result);
        return result;
    }

    if (!FlushInstructionCache(
            GetCurrentProcess(), patch_begin, patch_span)) {
        result.status =
            OversizedResolutionPatchStatus::instruction_cache_flush_failed;
        result.system_error = GetLastError();
        const bool branches_restored =
            restore_original_branches(volatile_context, result);
        DWORD ignored = 0;
        if (!VirtualProtect(
                patch_begin, patch_span, old_protection, &ignored)) {
            result.status =
                OversizedResolutionPatchStatus::protection_restore_failed;
            result.system_error = GetLastError();
        } else if (!branches_restored) {
            result.status = OversizedResolutionPatchStatus::rollback_failed;
        }
        snapshot_context(context, result);
        return result;
    }

    DWORD ignored = 0;
    if (!VirtualProtect(
            patch_begin, patch_span, old_protection, &ignored)) {
        result.status =
            OversizedResolutionPatchStatus::protection_restore_failed;
        result.system_error = GetLastError();
        const bool branches_restored =
            restore_original_branches(volatile_context, result);
        DWORD retry_ignored = 0;
        const bool protection_restored =
            VirtualProtect(
                patch_begin, patch_span, old_protection,
                &retry_ignored) != FALSE;
        if (!branches_restored || !protection_restored) {
            result.status = OversizedResolutionPatchStatus::rollback_failed;
            if (!protection_restored) {
                result.system_error = GetLastError();
            }
        }
        snapshot_context(context, result);
        return result;
    }

    snapshot_context(context, result);
    result.status =
        inspect_oversized_resolution_context(result.observed) ==
                OversizedResolutionContextState::already_patched
            ? OversizedResolutionPatchStatus::applied
            : OversizedResolutionPatchStatus::write_verification_failed;
    return result;
}
#endif

}  // namespace wawvr::mod
