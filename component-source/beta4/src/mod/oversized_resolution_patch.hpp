#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#if defined(WAWVR_HAS_T4_BINDINGS)
namespace wawvr::t4 {
class ValidatedBindings;
}
#endif

namespace wawvr::mod {

// T4 SP 1.7.1263 R_SetCustomResolution. The stock function parses
// r_customMode correctly, then rejects a window whose packed backbuffer is
// wider or taller than the active monitor. VR needs an off-screen-sized
// packed source (two 2496x2688 eyes plus a 1024x1024 scope camera =
// 6016x2688), so only the two monitor
// comparison branches are suppressed. Parsing and every other renderer check
// remain native.
inline constexpr std::uint32_t kCustomResolutionContextRva = 0x002D6690;
inline constexpr std::size_t kCustomResolutionWidthBranchOffset = 0x4D;
inline constexpr std::size_t kCustomResolutionHeightBranchOffset = 0x55;

inline constexpr std::array<std::uint8_t, 96>
    kExpectedCustomResolutionContext{
        0x83, 0xEC, 0x08, 0x53, 0x56, 0x57, 0x8D, 0x58,
        0x20, 0x8D, 0x78, 0x1C, 0xA1, 0x20, 0x72, 0x2B,
        0x04, 0x8B, 0x48, 0x10, 0x53, 0x57, 0x68, 0x98,
        0xE6, 0x89, 0x00, 0x51, 0xE8, 0xA8, 0x4E, 0x0D,
        0x00, 0x83, 0xC4, 0x10, 0x83, 0xF8, 0x02, 0x74,
        0x09, 0x32, 0xC0, 0x5F, 0x5E, 0x5B, 0x83, 0xC4,
        0x08, 0xC3, 0x8D, 0x54, 0x24, 0x10, 0x52, 0x8D,
        0x74, 0x24, 0x10, 0xE8, 0x10, 0xE8, 0xFF, 0xFF,
        0x83, 0xC4, 0x04, 0x84, 0xC0, 0x74, 0x10, 0x8B,
        0x07, 0x3B, 0x44, 0x24, 0x0C, 0x7F, 0xDA, 0x8B,
        0x0B, 0x3B, 0x4C, 0x24, 0x10, 0x7F, 0xD2, 0x5F,
        0x5E, 0xB0, 0x01, 0x5B, 0x83, 0xC4, 0x08, 0xC3,
    };

inline constexpr std::array<std::uint8_t, 2>
    kCustomResolutionWidthRejectBranch{0x7F, 0xDA};
inline constexpr std::array<std::uint8_t, 2>
    kCustomResolutionHeightRejectBranch{0x7F, 0xD2};
inline constexpr std::array<std::uint8_t, 2>
    kCustomResolutionBranchNops{0x90, 0x90};

static_assert(
    kExpectedCustomResolutionContext[kCustomResolutionWidthBranchOffset] ==
        kCustomResolutionWidthRejectBranch[0]);
static_assert(
    kExpectedCustomResolutionContext[kCustomResolutionWidthBranchOffset + 1] ==
        kCustomResolutionWidthRejectBranch[1]);
static_assert(
    kExpectedCustomResolutionContext[kCustomResolutionHeightBranchOffset] ==
        kCustomResolutionHeightRejectBranch[0]);
static_assert(
    kExpectedCustomResolutionContext[kCustomResolutionHeightBranchOffset + 1] ==
        kCustomResolutionHeightRejectBranch[1]);

enum class OversizedResolutionContextState : std::uint8_t {
    expected,
    already_patched,
    mismatch,
};

[[nodiscard]] OversizedResolutionContextState
inspect_oversized_resolution_context(
    std::span<const std::uint8_t> bytes) noexcept;

enum class OversizedResolutionPatchStatus : std::uint8_t {
    applied,
    already_applied,
    not_applicable,
    address_out_of_range,
    memory_query_failed,
    memory_not_patchable,
    unexpected_bytes,
    peer_thread_quiesce_failed,
    virtual_protect_failed,
    expected_bytes_changed,
    write_verification_failed,
    instruction_cache_flush_failed,
    protection_restore_failed,
    rollback_failed,
};

struct OversizedResolutionPatchResult final {
    OversizedResolutionPatchStatus status{
        OversizedResolutionPatchStatus::unexpected_bytes};
    std::uint32_t system_error{};
    std::uint32_t thread_id{};
    std::array<std::uint8_t, kExpectedCustomResolutionContext.size()>
        observed{};
    std::size_t observed_size{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == OversizedResolutionPatchStatus::applied ||
               status == OversizedResolutionPatchStatus::already_applied ||
               status == OversizedResolutionPatchStatus::not_applicable;
    }
};

[[nodiscard]] const char* oversized_resolution_patch_status_name(
    OversizedResolutionPatchStatus status) noexcept;

#if defined(WAWVR_HAS_T4_BINDINGS)
[[nodiscard]] OversizedResolutionPatchResult
install_oversized_resolution_patch(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;
#endif

}  // namespace wawvr::mod
