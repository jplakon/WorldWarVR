// SPDX-License-Identifier: GPL-3.0-only
#include "grenade_observer_patch_logic.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace wawvr::mod;

int failures = 0;
std::size_t scenarios = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

enum class Fault { none, fail_before, fail_after, throw_before, throw_after };

// Synthetic bytes/protections only. No process, code page, native CALL target,
// platform protection operation or native compatibility is exercised here.
template<std::size_t Count>
struct SyntheticSpanOperations final {
    static std::array<std::uint32_t, Count> original_bytes() {
        std::array<std::uint32_t, Count> result{};
        for (std::size_t i = 0; i < Count; ++i)
            result[i] = 0x11111111U * static_cast<std::uint32_t>(i + 1U);
        return result;
    }
    static std::array<std::uint32_t, Count> original_protections() {
        std::array<std::uint32_t, Count> result{};
        for (std::size_t i = 0; i < Count; ++i)
            result[i] = 0x20U * static_cast<std::uint32_t>(i + 1U);
        return result;
    }
    std::array<std::uint32_t, Count> bytes{original_bytes()};
    std::array<std::uint32_t, Count> protections{original_protections()};
    const std::array<std::uint32_t, Count> originals{bytes};
    const std::array<std::uint32_t, Count> originalProtections{protections};
    std::array<Fault, Count> patchFault{};
    std::array<Fault, Count> restoreFault{};
    Fault protectionCheckFault{};
    Fault restoreProtectionFault{};
    Fault originalCheckFault{};
    bool leaveProtectionMismatch{};
    std::vector<int> calls{};

    static bool before(const Fault fault) {
        if (fault == Fault::throw_before) throw std::runtime_error("synthetic");
        return fault != Fault::fail_before;
    }
    static bool after(const Fault fault) {
        if (fault == Fault::throw_after) throw std::runtime_error("synthetic");
        return fault != Fault::fail_after;
    }
    bool patch(const int index) {
        calls.push_back(index);
        const auto slot = static_cast<std::size_t>(index);
        if (!before(patchFault[slot])) return false;
        bytes[slot] = 0xAAAAAAAAU + static_cast<std::uint32_t>(index);
        protections[slot] = 0x04U;
        if (!after(patchFault[slot])) return false;
        if (!leaveProtectionMismatch) protections[slot] = originalProtections[slot];
        return true;
    }
    bool protections_match() {
        calls.push_back(static_cast<int>(Count));
        if (!before(protectionCheckFault)) return false;
        const bool matches = protections == originalProtections;
        return after(protectionCheckFault) && matches;
    }
    bool restore(const int index) {
        calls.push_back(static_cast<int>(Count) + 1 + index);
        const auto slot = static_cast<std::size_t>(index);
        if (!before(restoreFault[slot])) return false;
        bytes[slot] = originals[slot];
        return after(restoreFault[slot]);
    }
    bool restore_protections() {
        calls.push_back(static_cast<int>(Count) * 2 + 1);
        if (!before(restoreProtectionFault)) return false;
        protections = originalProtections;
        return after(restoreProtectionFault);
    }
    bool originals_match() {
        calls.push_back(static_cast<int>(Count) * 2 + 2);
        if (!before(originalCheckFault)) return false;
        const bool matches = bytes == originals;
        return after(originalCheckFault) && matches;
    }
};

// The original 31 two-CALL cases retain exactly their previous byte/protection
// values and call codes. The same fake model also covers the new three spans.
using SyntheticOperations = SyntheticSpanOperations<2>;
using SyntheticThreeSpanOperations = SyntheticSpanOperations<3>;

void expect_every_cleanup(const SyntheticOperations& operations) {
    expect(operations.calls.size() >= 4U,
        "failure attempts all four cleanup/readback operations");
    if (operations.calls.size() >= 4U) {
        const auto offset = operations.calls.size() - 4U;
        expect(operations.calls[offset] == 3 &&
            operations.calls[offset + 1U] == 4 &&
            operations.calls[offset + 2U] == 5 &&
            operations.calls[offset + 3U] == 6,
            "CALL0, CALL1, page protections and originals readback run independently");
    }
}

void test_success() {
    SyntheticOperations operations{};
    const auto result = install_grenade_observer_calls(operations);
    ++scenarios;
    expect(result.installed && !result.rollbackVerified &&
        result.retainPassThroughDependencies,
        "installation requires both patches and verified original protections");
    expect(operations.calls == std::vector<int>{0, 1, 2},
        "success patches CALL0 then CALL1 then verifies protections");
    expect(operations.bytes != operations.originals &&
        operations.protections == operations.originalProtections,
        "synthetic successful patches retain their saved page protections");
}

void test_patch_failures_before_and_after_mutation() {
    for (const int index : {0, 1}) {
        for (const auto fault : {Fault::fail_before, Fault::fail_after,
                 Fault::throw_before, Fault::throw_after}) {
            SyntheticOperations operations{};
            operations.patchFault[static_cast<std::size_t>(index)] = fault;
            const auto result = install_grenade_observer_calls(operations);
            ++scenarios;
            expect(!result.installed && result.rollbackVerified &&
                result.retainPassThroughDependencies,
                "failed/throwing patch never installs and independently rolls back");
            expect_every_cleanup(operations);
            const auto expectedCalls = index == 0
                ? std::vector<int>{0, 3, 4, 5, 6}
                : std::vector<int>{0, 1, 3, 4, 5, 6};
            expect(operations.calls == expectedCalls,
                "CALL1 is never attempted after ambiguous CALL0 failure");
            expect(operations.bytes == operations.originals &&
                operations.protections == operations.originalProtections,
                "clean rollback regains both originals and exact saved protections");
        }
    }
}

void test_protection_mismatch_and_check_failure() {
    for (const auto fault : {Fault::none, Fault::fail_before, Fault::fail_after,
             Fault::throw_before, Fault::throw_after}) {
        SyntheticOperations operations{};
        operations.leaveProtectionMismatch = true;
        operations.protectionCheckFault = fault;
        const auto result = install_grenade_observer_calls(operations);
        ++scenarios;
        expect(!result.installed && result.rollbackVerified &&
            result.retainPassThroughDependencies,
            "changed/ambiguous page protections cannot be accepted as installed");
        expect_every_cleanup(operations);
        expect(operations.bytes == operations.originals &&
            operations.protections == operations.originalProtections,
            "protection-check failure restores bytes and distinct saved protections");
    }
}

void test_every_cleanup_failure_is_independent() {
    for (const int failedOperation : {3, 4, 5, 6}) {
        for (const auto fault : {Fault::fail_before, Fault::fail_after,
                 Fault::throw_before, Fault::throw_after}) {
            SyntheticOperations operations{};
            operations.leaveProtectionMismatch = true;
            if (failedOperation == 3 || failedOperation == 4) {
                operations.restoreFault[static_cast<std::size_t>(
                    failedOperation - 3)] = fault;
            } else if (failedOperation == 5) {
                operations.restoreProtectionFault = fault;
            } else {
                operations.originalCheckFault = fault;
            }
            const auto result = install_grenade_observer_calls(operations);
            ++scenarios;
            expect(!result.installed && !result.rollbackVerified &&
                result.retainPassThroughDependencies,
                "any cleanup ambiguity retains dependencies and denies rollback proof");
            expect_every_cleanup(operations);
            for (std::size_t slot = 0; slot < 2U; ++slot) {
                const bool failedBeforeMutation =
                    failedOperation == static_cast<int>(slot) + 3 &&
                    (fault == Fault::fail_before || fault == Fault::throw_before);
                expect((operations.bytes[slot] == operations.originals[slot]) ==
                    !failedBeforeMutation,
                    "all restorable synthetic native originals are regained");
            }
            const bool protectionRestoreFailedBefore = failedOperation == 5 &&
                (fault == Fault::fail_before || fault == Fault::throw_before);
            expect((operations.protections == operations.originalProtections) ==
                !protectionRestoreFailedBefore,
                "byte restore failures do not skip exact protection recovery");
        }
    }
}

void test_multiple_cleanup_failures_do_not_short_circuit() {
    SyntheticOperations operations{};
    operations.patchFault[0] = Fault::fail_after;
    operations.restoreFault = {Fault::throw_before, Fault::fail_before};
    operations.restoreProtectionFault = Fault::throw_before;
    operations.originalCheckFault = Fault::throw_after;
    const auto result = install_grenade_observer_calls(operations);
    ++scenarios;
    expect(!result.installed && !result.rollbackVerified &&
        result.retainPassThroughDependencies,
        "multiple ambiguous cleanups never become installation or rollback proof");
    expect_every_cleanup(operations);
    expect(operations.calls == std::vector<int>{0, 3, 4, 5, 6},
        "every cleanup still runs after prior failure or exception");
}

void expect_three_span_cleanup(const SyntheticThreeSpanOperations& operations) {
    expect(operations.calls.size() >= 5U,
        "three-span failure attempts all three restores and both final checks");
    if (operations.calls.size() >= 5U) {
        const auto offset = operations.calls.size() - 5U;
        expect(operations.calls[offset] == 4 &&
            operations.calls[offset + 1U] == 5 &&
            operations.calls[offset + 2U] == 6 &&
            operations.calls[offset + 3U] == 7 &&
            operations.calls[offset + 4U] == 8,
            "three spans, saved protections and originals readback run independently");
    }
}

void test_three_span_success_and_each_patch_failure() {
    SyntheticThreeSpanOperations successful{};
    const auto installed = install_grenade_observer_spans<3>(successful);
    ++scenarios;
    expect(installed.installed && !installed.rollbackVerified &&
        installed.retainPassThroughDependencies &&
        successful.calls == std::vector<int>{0, 1, 2, 3},
        "all three spans and their exact protections are required for success");
    expect(successful.protections == successful.originalProtections,
        "successful three-span fake retains every distinct saved protection");
    for (int index = 0; index < 3; ++index) {
        for (const auto fault : {Fault::fail_before, Fault::fail_after,
                 Fault::throw_before, Fault::throw_after}) {
            SyntheticThreeSpanOperations operations{};
            operations.patchFault[static_cast<std::size_t>(index)] = fault;
            const auto result = install_grenade_observer_spans<3>(operations);
            ++scenarios;
            expect(!result.installed && result.rollbackVerified &&
                result.retainPassThroughDependencies,
                "each three-span patch failure is non-installed with clean rollback");
            std::vector<int> expectedCalls{};
            for (int attempted = 0; attempted <= index; ++attempted)
                expectedCalls.push_back(attempted);
            for (int cleanup = 4; cleanup <= 8; ++cleanup)
                expectedCalls.push_back(cleanup);
            expect(operations.calls == expectedCalls,
                "later patches stop at first failure but every span is restored");
            expect_three_span_cleanup(operations);
            expect(operations.bytes == operations.originals &&
                operations.protections == operations.originalProtections,
                "before/after mutation failures regain all three originals and protections");
        }
    }
}

void test_three_span_protection_validation_and_each_cleanup_failure() {
    for (const auto fault : {Fault::none, Fault::fail_before, Fault::fail_after,
             Fault::throw_before, Fault::throw_after}) {
        SyntheticThreeSpanOperations operations{};
        operations.leaveProtectionMismatch = true;
        operations.protectionCheckFault = fault;
        const auto result = install_grenade_observer_spans<3>(operations);
        ++scenarios;
        expect(!result.installed && result.rollbackVerified &&
            result.retainPassThroughDependencies,
            "three-span protection mismatch or ambiguous check cannot install");
        expect_three_span_cleanup(operations);
        expect(operations.bytes == operations.originals &&
            operations.protections == operations.originalProtections,
            "three-span protection failures regain all original bytes/protections");
    }
    for (int failedOperation = 4; failedOperation <= 8; ++failedOperation) {
        for (const auto fault : {Fault::fail_before, Fault::fail_after,
                 Fault::throw_before, Fault::throw_after}) {
            SyntheticThreeSpanOperations operations{};
            operations.leaveProtectionMismatch = true;
            if (failedOperation <= 6) {
                operations.restoreFault[static_cast<std::size_t>(
                    failedOperation - 4)] = fault;
            } else if (failedOperation == 7) {
                operations.restoreProtectionFault = fault;
            } else {
                operations.originalCheckFault = fault;
            }
            const auto result = install_grenade_observer_spans<3>(operations);
            ++scenarios;
            expect(!result.installed && !result.rollbackVerified &&
                result.retainPassThroughDependencies,
                "any ambiguous three-span cleanup denies rollback and retains dependencies");
            expect_three_span_cleanup(operations);
            for (std::size_t slot = 0; slot < 3U; ++slot) {
                const bool beforeMutation = failedOperation ==
                    static_cast<int>(slot) + 4 &&
                    (fault == Fault::fail_before || fault == Fault::throw_before);
                expect((operations.bytes[slot] == operations.originals[slot]) ==
                    !beforeMutation,
                    "every other restorable span regains its native original");
            }
            const bool beforeProtectionRestore = failedOperation == 7 &&
                (fault == Fault::fail_before || fault == Fault::throw_before);
            expect((operations.protections == operations.originalProtections) ==
                !beforeProtectionRestore,
                "any span restoration failure still attempts protection recovery");
        }
    }
}

void test_three_span_combined_cleanup_failures() {
    SyntheticThreeSpanOperations operations{};
    operations.patchFault[2] = Fault::throw_after;
    operations.restoreFault = {Fault::throw_before, Fault::fail_after,
        Fault::throw_after};
    operations.restoreProtectionFault = Fault::throw_after;
    operations.originalCheckFault = Fault::fail_after;
    const auto result = install_grenade_observer_spans<3>(operations);
    ++scenarios;
    expect(!result.installed && !result.rollbackVerified &&
        result.retainPassThroughDependencies,
        "combined three-span ambiguity never becomes successful installation or rollback");
    expect_three_span_cleanup(operations);
    expect(operations.calls == std::vector<int>{0, 1, 2, 4, 5, 6, 7, 8},
        "every cleanup executes despite all earlier three-span cleanup failures");
}

template<std::size_t Count>
void test_extended_span_inline_observer_installation() {
    using Operations = SyntheticSpanOperations<Count>;
    const auto cleanup = [](const Operations& operations) {
        std::vector<int> expected{};
        for (int code = static_cast<int>(Count) + 1;
                code <= static_cast<int>(Count) * 2 + 2; ++code)
            expected.push_back(code);
        expect(operations.calls.size() >= expected.size(),
            "all native spans and both final checks are attempted on failure");
        if (operations.calls.size() >= expected.size()) {
            expect(std::vector<int>(operations.calls.end() - expected.size(),
                operations.calls.end()) == expected,
                "extended-span cleanup never short-circuits a later restoration");
        }
    };
    Operations success{};
    auto result = install_grenade_observer_spans<Count>(success);
    ++scenarios;
    std::vector<int> successful_calls{};
    for (int code = 0; code <= static_cast<int>(Count); ++code)
        successful_calls.push_back(code);
    expect(result.installed && result.retainPassThroughDependencies &&
        success.calls == successful_calls,
        "all native seams and original protections required for success");
    for (int index = 0; index < static_cast<int>(Count); ++index) {
        for (const auto fault : {Fault::fail_before, Fault::fail_after,
                Fault::throw_before, Fault::throw_after}) {
            Operations operations{};
            operations.patchFault[static_cast<std::size_t>(index)] = fault;
            result = install_grenade_observer_spans<Count>(operations);
            ++scenarios;
            expect(!result.installed && result.rollbackVerified &&
                result.retainPassThroughDependencies &&
                operations.bytes == operations.originals &&
                operations.protections == operations.originalProtections,
                "each ambiguous inline/dispatch/producer patch restores every span");
            cleanup(operations);
        }
    }
    for (int failed = static_cast<int>(Count) + 1;
            failed <= static_cast<int>(Count) * 2 + 2; ++failed) {
        for (const auto fault : {Fault::fail_before, Fault::fail_after,
                Fault::throw_before, Fault::throw_after}) {
            Operations operations{};
            operations.leaveProtectionMismatch = true;
            if (failed < static_cast<int>(Count) * 2 + 1) operations.restoreFault[
                static_cast<std::size_t>(failed - static_cast<int>(Count) - 1)] = fault;
            else if (failed == static_cast<int>(Count) * 2 + 1)
                operations.restoreProtectionFault = fault;
            else operations.originalCheckFault = fault;
            result = install_grenade_observer_spans<Count>(operations);
            ++scenarios;
            expect(!result.installed && !result.rollbackVerified &&
                result.retainPassThroughDependencies,
                "any extended-span restoration ambiguity retains pass-through dependencies");
            cleanup(operations);
        }
    }
    for (const auto fault : {Fault::none, Fault::fail_before, Fault::fail_after,
            Fault::throw_before, Fault::throw_after}) {
        Operations operations{};
        operations.leaveProtectionMismatch = true;
        operations.protectionCheckFault = fault;
        result = install_grenade_observer_spans<Count>(operations);
        ++scenarios;
        expect(!result.installed && result.rollbackVerified &&
            result.retainPassThroughDependencies,
            "extended-span original-protection failure cannot install");
        cleanup(operations);
    }
}

}  // namespace

int main() {
    test_success();
    test_patch_failures_before_and_after_mutation();
    test_protection_mismatch_and_check_failure();
    test_every_cleanup_failure_is_independent();
    test_multiple_cleanup_failures_do_not_short_circuit();
    expect(scenarios == 31U, "the original 31 two-CALL fault scenarios are retained");
    test_three_span_success_and_each_patch_failure();
    test_three_span_protection_validation_and_each_cleanup_failure();
    test_three_span_combined_cleanup_failures();
    expect(scenarios == 70U, "all prior two/three-span fault scenarios retained");
    test_extended_span_inline_observer_installation<5>();
    expect(scenarios == 124U, "all prior five-span fault scenarios retained");
    test_extended_span_inline_observer_installation<6>();
    if (failures == 0) {
        std::cout << "PASS: " << scenarios
                  << " synthetic observer-patch success/fault scenarios; "
                     "all cleanup paths attempted; dependencies retained; "
                     "native proof not claimed\n";
    }
    return failures == 0 ? 0 : 1;
}
