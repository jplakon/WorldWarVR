#include "direct_boot_patch.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <span>

namespace {

int failures = 0;

void check(const bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "direct boot patch test failed: %s\n", message);
        ++failures;
    }
}

}  // namespace

int main() {
    using wawvr::mod::DirectBootContextState;
    using wawvr::mod::inspect_direct_boot_context;
    using wawvr::mod::kExpectedStartupIntroContext;
    using wawvr::mod::kStartupIntroBranchOffset;
    using wawvr::mod::kStartupIntroUnconditionalBranch;

    check(inspect_direct_boot_context(kExpectedStartupIntroContext) ==
              DirectBootContextState::expected,
          "the exact 1.7.1263 context must be accepted");

    auto patched = kExpectedStartupIntroContext;
    patched[kStartupIntroBranchOffset] = kStartupIntroUnconditionalBranch;
    check(inspect_direct_boot_context(patched) ==
              DirectBootContextState::already_patched,
          "the one-byte JNE-to-JMP form must be idempotently recognized");

    std::size_t changed_bytes = 0;
    std::size_t changed_index = 0;
    for (std::size_t index = 0; index < patched.size(); ++index) {
        if (patched[index] != kExpectedStartupIntroContext[index]) {
            ++changed_bytes;
            changed_index = index;
        }
    }
    check(changed_bytes == 1 && changed_index == kStartupIntroBranchOffset,
          "the patch plan must change exactly the branch opcode byte");

    auto wrong_call_target = kExpectedStartupIntroContext;
    wrong_call_target.back() ^= 0x01;
    check(inspect_direct_boot_context(wrong_call_target) ==
              DirectBootContextState::mismatch,
          "a changed Cbuf_AddText call target must fail closed");

    auto wrong_branch_distance = kExpectedStartupIntroContext;
    wrong_branch_distance[kStartupIntroBranchOffset + 1] ^= 0x01;
    check(inspect_direct_boot_context(wrong_branch_distance) ==
              DirectBootContextState::mismatch,
          "a changed branch distance must fail closed");

    const std::span<const std::uint8_t> truncated{
        kExpectedStartupIntroContext.data(),
        kExpectedStartupIntroContext.size() - 1};
    check(inspect_direct_boot_context(truncated) ==
              DirectBootContextState::mismatch,
          "a truncated context must fail closed");

    return failures == 0 ? 0 : 1;
}
