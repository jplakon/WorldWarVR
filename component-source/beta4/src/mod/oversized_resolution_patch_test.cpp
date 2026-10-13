#include "oversized_resolution_patch.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <span>

namespace {

int failures = 0;

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::fprintf(
            stderr, "oversized resolution patch test failed: %s\n",
            message);
        ++failures;
    }
}

}  // namespace

int main() {
    using namespace wawvr::mod;

    check(
        inspect_oversized_resolution_context(
            kExpectedCustomResolutionContext) ==
            OversizedResolutionContextState::expected,
        "the exact R_SetCustomResolution body must be accepted");

    auto patched = kExpectedCustomResolutionContext;
    std::copy(
        kCustomResolutionBranchNops.begin(),
        kCustomResolutionBranchNops.end(),
        patched.begin() + kCustomResolutionWidthBranchOffset);
    std::copy(
        kCustomResolutionBranchNops.begin(),
        kCustomResolutionBranchNops.end(),
        patched.begin() + kCustomResolutionHeightBranchOffset);
    check(
        inspect_oversized_resolution_context(patched) ==
            OversizedResolutionContextState::already_patched,
        "both monitor rejection branches must be recognized as patched");

    std::size_t changed = 0;
    for (std::size_t index = 0; index < patched.size(); ++index) {
        changed +=
            patched[index] != kExpectedCustomResolutionContext[index]
                ? 1U
                : 0U;
    }
    check(changed == 4,
          "the plan must alter only the two two-byte reject branches");

    auto one_branch_only = patched;
    std::copy(
        kCustomResolutionWidthRejectBranch.begin(),
        kCustomResolutionWidthRejectBranch.end(),
        one_branch_only.begin() + kCustomResolutionWidthBranchOffset);
    check(
        inspect_oversized_resolution_context(one_branch_only) ==
            OversizedResolutionContextState::mismatch,
        "a partial install must fail closed");

    auto wrong_parse_call = kExpectedCustomResolutionContext;
    wrong_parse_call[0x1D] ^= 0x01;
    check(
        inspect_oversized_resolution_context(wrong_parse_call) ==
            OversizedResolutionContextState::mismatch,
        "a changed sscanf call target must fail closed");

    const std::span<const std::uint8_t> truncated{
        kExpectedCustomResolutionContext.data(),
        kExpectedCustomResolutionContext.size() - 1};
    check(
        inspect_oversized_resolution_context(truncated) ==
            OversizedResolutionContextState::mismatch,
        "a truncated body must fail closed");

    return failures == 0 ? 0 : 1;
}
