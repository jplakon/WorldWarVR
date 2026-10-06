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

// Exact WaW 1.7.1263 context around the startup command which appends
// "cinematic Treyarch\n" to command buffer 0. The executable's preferred base
// is 0x00400000, so these RVAs correspond to VAs 0x0059D678 and 0x0059D682.
inline constexpr std::uint32_t kStartupIntroContextRva = 0x0019D678;
inline constexpr std::uint32_t kStartupIntroBranchRva = 0x0019D682;
inline constexpr std::size_t kStartupIntroBranchOffset =
    kStartupIntroBranchRva - kStartupIntroContextRva;
inline constexpr std::uint8_t kStartupIntroConditionalBranch = 0x75;  // JNE +0x0C
inline constexpr std::uint8_t kStartupIntroUnconditionalBranch = 0xEB;  // JMP +0x0C

inline constexpr std::array<std::uint8_t, 24> kExpectedStartupIntroContext{
    0x8B, 0x15, 0x94, 0x64, 0xF9, 0x01,  // mov edx,[01F96494h]
    0x80, 0x7A, 0x10, 0x00,              // cmp byte ptr [edx+10h],0
    0x75, 0x0C,                          // jne 0059D690
    0xB8, 0x84, 0x25, 0x87, 0x00,        // mov eax,00872584h
    0x33, 0xC9,                          // xor ecx,ecx
    0xE8, 0x70, 0x6B, 0xFF, 0xFF,        // call 00594200 (Cbuf_AddText)
};

static_assert(kStartupIntroBranchOffset == 10);
static_assert(kExpectedStartupIntroContext[kStartupIntroBranchOffset] ==
              kStartupIntroConditionalBranch);

enum class DirectBootContextState : std::uint8_t {
    expected,
    already_patched,
    mismatch,
};

[[nodiscard]] DirectBootContextState inspect_direct_boot_context(
    std::span<const std::uint8_t> bytes) noexcept;

enum class DirectBootPatchStatus : std::uint8_t {
    applied,
    already_applied,
    not_applicable,
    disabled_by_environment,
    rejected_wrong_profile,
    address_out_of_range,
    memory_query_failed,
    memory_not_patchable,
    unexpected_bytes,
    virtual_protect_failed,
    expected_bytes_changed,
    write_verification_failed,
    instruction_cache_flush_failed,
    protection_restore_failed,
    rollback_failed,
};

struct DirectBootPatchResult final {
    DirectBootPatchStatus status{DirectBootPatchStatus::unexpected_bytes};
    std::uint32_t system_error{};
    std::array<std::uint8_t, kExpectedStartupIntroContext.size()> observed{};
    std::size_t observed_size{};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == DirectBootPatchStatus::applied ||
               status == DirectBootPatchStatus::already_applied ||
               status == DirectBootPatchStatus::not_applicable;
    }
};

[[nodiscard]] const char* direct_boot_patch_status_name(
    DirectBootPatchStatus status) noexcept;

#if defined(WAWVR_HAS_T4_BINDINGS)
// The capability argument can only be produced after both the exact on-disk
// SHA-256 profile and the mapped module have passed validation.
[[nodiscard]] DirectBootPatchResult install_direct_boot_patch(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;
#endif

}  // namespace wawvr::mod
