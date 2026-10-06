#include "peer_thread_quiescence.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using namespace wawvr::mod;

    expect(
        classify_peer_thread_failure(
            PeerThreadFailureStage::open, ERROR_INVALID_PARAMETER,
            WAIT_FAILED) == PeerThreadFailureDisposition::vanished,
        "a vanished Toolhelp snapshot thread is skippable");
    expect(
        classify_peer_thread_failure(
            PeerThreadFailureStage::open, ERROR_ACCESS_DENIED,
            WAIT_OBJECT_0) == PeerThreadFailureDisposition::fail_closed,
        "OpenThread access failures remain fatal");
    expect(
        classify_peer_thread_failure(
            PeerThreadFailureStage::suspend, ERROR_INVALID_PARAMETER,
            WAIT_TIMEOUT) == PeerThreadFailureDisposition::fail_closed,
        "a live thread with a suspend failure remains fatal");
    expect(
        classify_peer_thread_failure(
            PeerThreadFailureStage::suspend, ERROR_INVALID_HANDLE,
            WAIT_OBJECT_0) == PeerThreadFailureDisposition::vanished,
        "a signaled thread object proves post-open termination");
    expect(
        classify_peer_thread_failure(
            PeerThreadFailureStage::context, ERROR_GEN_FAILURE,
            WAIT_FAILED) == PeerThreadFailureDisposition::fail_closed,
        "an unprovable context failure remains fatal");

    constexpr std::array<PeerThreadPatchRange, 2> ranges{{
        {0x1000u, 5u},
        {0x2000u, 8u},
    }};
    expect(
        instruction_in_patch_ranges(0x1000u, ranges),
        "the first byte is inside a protected range");
    expect(
        instruction_in_patch_ranges(0x1004u, ranges),
        "the final byte is inside a protected range");
    expect(
        !instruction_in_patch_ranges(0x1005u, ranges),
        "the end address is outside a half-open protected range");
    expect(
        instruction_in_patch_ranges(0x2007u, ranges),
        "all supplied patch ranges are checked");
    expect(
        !instruction_in_patch_ranges(0x1fffu, ranges),
        "addresses between protected ranges remain outside");

    constexpr std::array<std::uintptr_t, 4> harmless_stack{
        0x1000u, 0x3000u, 0x4000u, 0x5000u};
    constexpr std::array<std::uintptr_t, 2> forbidden_returns{
        0x6D62C0u, 0x6D605Cu};
    expect(
        !stack_contains_forbidden_return(
            harmless_stack, forbidden_returns),
        "a harmless stack was rejected");
    constexpr std::array<std::uintptr_t, 3> active_renderer_stack{
        0x77123456u, 0x6D605Cu, 0x77234567u};
    expect(
        stack_contains_forbidden_return(
            active_renderer_stack, forbidden_returns),
        "an exact renderer return address on the stack was missed");

    SuspendedPeerThreads suspended;
    PeerThreadQuiesceResult result{};
    expect(
        !suspended.suspend(
            std::span<const PeerThreadPatchRange>{}, &result) &&
            result.status ==
                PeerThreadQuiesceStatus::invalid_patch_ranges &&
            result.system_error == ERROR_INVALID_PARAMETER,
        "an empty patch-range set fails before thread enumeration");

    return EXIT_SUCCESS;
}
