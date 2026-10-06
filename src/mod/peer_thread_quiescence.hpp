#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace wawvr::mod {

struct PeerThreadPatchRange final {
    std::uintptr_t address{};
    std::size_t size{};
};

enum class PeerThreadFailureStage : std::uint8_t {
    open,
    suspend,
    context,
};

enum class PeerThreadFailureDisposition : std::uint8_t {
    fail_closed,
    vanished,
};

// OpenThread(ERROR_INVALID_PARAMETER) is benign only for a nonzero thread ID
// obtained from a Toolhelp snapshot: that snapshot entry no longer names a
// live thread. Once a handle exists, only a signaled thread object proves that
// the thread terminated. Every ambiguous case remains fail-closed.
[[nodiscard]] constexpr PeerThreadFailureDisposition
classify_peer_thread_failure(
    const PeerThreadFailureStage stage,
    const DWORD operation_error,
    const DWORD thread_wait_result) noexcept {
    if (stage == PeerThreadFailureStage::open) {
        return operation_error == ERROR_INVALID_PARAMETER
                   ? PeerThreadFailureDisposition::vanished
                   : PeerThreadFailureDisposition::fail_closed;
    }
    return thread_wait_result == WAIT_OBJECT_0
               ? PeerThreadFailureDisposition::vanished
               : PeerThreadFailureDisposition::fail_closed;
}

[[nodiscard]] constexpr bool instruction_in_patch_ranges(
    const std::uintptr_t instruction,
    const std::span<const PeerThreadPatchRange> ranges) noexcept {
    for (const auto& range : ranges) {
        if (range.size != 0 && instruction >= range.address &&
            instruction - range.address < range.size) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr bool stack_contains_forbidden_return(
    const std::span<const std::uintptr_t> stack_words,
    const std::span<const std::uintptr_t> forbidden_returns) noexcept {
    for (const std::uintptr_t word : stack_words) {
        for (const std::uintptr_t forbidden : forbidden_returns) {
            if (forbidden != 0 && word == forbidden) {
                return true;
            }
        }
    }
    return false;
}

enum class PeerThreadQuiesceStatus : std::uint8_t {
    acquired,
    invalid_patch_ranges,
    snapshot_failed,
    enumeration_failed,
    capacity_exceeded,
    thread_open_failed,
    thread_query_failed,
    thread_suspend_failed,
    thread_context_failed,
    thread_inside_patch_range,
    thread_stack_query_failed,
    thread_stack_bounds_invalid,
    thread_stack_read_failed,
    thread_critical_return_address,
};

struct PeerThreadQuiesceResult final {
    PeerThreadQuiesceStatus status{
        PeerThreadQuiesceStatus::invalid_patch_ranges};
    DWORD system_error{};
    DWORD thread_id{};
    std::size_t vanished_thread_count{};

    [[nodiscard]] bool ok() const noexcept {
        return status == PeerThreadQuiesceStatus::acquired;
    }
};

class SuspendedPeerThreads final {
public:
    SuspendedPeerThreads() = default;
    SuspendedPeerThreads(const SuspendedPeerThreads&) = delete;
    SuspendedPeerThreads& operator=(const SuspendedPeerThreads&) = delete;

    ~SuspendedPeerThreads();

    [[nodiscard]] bool suspend(
        std::span<const PeerThreadPatchRange> patch_ranges,
        PeerThreadQuiesceResult* result = nullptr,
        std::span<const std::uintptr_t> forbidden_stack_returns = {}) noexcept;

    [[nodiscard]] bool suspend(
        PeerThreadPatchRange patch_range,
        PeerThreadQuiesceResult* result = nullptr) noexcept {
        return suspend(
            std::span<const PeerThreadPatchRange>(&patch_range, 1), result);
    }

    void resume() noexcept;

private:
    struct ThreadRecord final {
        HANDLE handle{};
        DWORD id{};
        bool suspended{};
    };

    static constexpr std::size_t kMaximumPeerThreads = 512;

    std::array<ThreadRecord, kMaximumPeerThreads> threads_{};
    std::size_t thread_count_{};
};

} // namespace wawvr::mod
