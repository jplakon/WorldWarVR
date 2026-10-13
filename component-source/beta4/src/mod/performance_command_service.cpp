// SPDX-License-Identifier: GPL-3.0-only
#include "performance_command_service.hpp"

#include "performance_command_logic.hpp"
#include "present_hook_logic.hpp"
#include "stereo_diagnostics.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <string_view>

namespace wawvr::mod {
namespace {
constexpr std::uint64_t kPollMilliseconds = 250;
std::atomic<bool> g_requested{false};
// Immutable after publication. Only the WinMain service owns polling/nonces.
std::array<wchar_t, 1024> g_command_path{};
std::uint64_t g_next_poll = 0;
std::uint64_t g_last_consumed_nonce = 0;
std::uint64_t g_last_content_hash = 0;
bool g_saw_content = false;
DWORD g_last_read_error = ERROR_SUCCESS;

std::uint64_t content_hash(const std::string_view text) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char value : text) {
        hash = (hash ^ value) * 1099511628211ULL;
    }
    return hash;
}
}  // namespace

void configure_performance_command_service(const bool single_player) noexcept {
    g_requested.store(false, std::memory_order_release);
    if (!single_player) {
        return;
    }
    const DWORD size = GetEnvironmentVariableW(
        L"WAWVR_PERFORMANCE_COMMAND_FILE", g_command_path.data(),
        static_cast<DWORD>(g_command_path.size()));
    // Local absolute paths only: do not block the native frame thread on UNC
    // network reads or resolve a changing process working directory.
    if (size < 4 || size >= g_command_path.size() ||
        !((g_command_path[0] >= L'A' && g_command_path[0] <= L'Z') ||
          (g_command_path[0] >= L'a' && g_command_path[0] <= L'z')) ||
        g_command_path[1] != L':' ||
        (g_command_path[2] != L'\\' && g_command_path[2] != L'/')) {
        return;
    }
    g_next_poll = 0;
    g_last_consumed_nonce = 0;
    g_last_content_hash = 0;
    g_saw_content = false;
    g_last_read_error = ERROR_SUCCESS;
    g_requested.store(true, std::memory_order_release);
}

void clear_performance_command_service() noexcept {
    // Retain the immutable path so an in-flight service never races clearing.
    g_requested.store(false, std::memory_order_release);
}

void service_performance_command_file_after_com_frame(
    const std::uintptr_t validated_cbuf_address,
    const PerformanceCommandQueue queue,
    const std::uint64_t now_milliseconds,
    const bool presentation_valid,
    const std::uint32_t key_catchers,
    const std::int32_t connection_state) noexcept {
    // The default/physical path performs no environment lookup, module query,
    // file I/O, allocation, or logging on this per-frame service.
    if (!g_requested.load(std::memory_order_acquire)) {
        return;
    }
    if (now_milliseconds < g_next_poll || !presentation_valid ||
        validated_cbuf_address == 0 || queue == nullptr) {
        return;
    }
    g_next_poll = now_milliseconds + kPollMilliseconds;
    // XR initialization follows menu binding. Check the actual loaded module,
    // not merely XR_RUNTIME_JSON, before the first and every subsequent poll.
    if (GetModuleHandleW(L"openxr_simulator.dll") == nullptr) {
        return;
    }

    const HANDLE file = CreateFileW(
        g_command_path.data(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND &&
            error != g_last_read_error) {
            stereo_diagnostic_log(
                "PerfCommandDiag rejected read error=%lu simulator=1 keyCatchers=0x%X connection=%d",
                error, key_catchers, connection_state);
        }
        g_last_read_error = error;
        return;
    }
    g_last_read_error = ERROR_SUCCESS;
    std::array<char, kPerformanceCommandMaximumBytes> bytes{};
    LARGE_INTEGER size{};
    DWORD bytes_read = 0;
    const bool bounded = GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
        size.QuadPart <= static_cast<LONGLONG>(bytes.size());
    const bool read = bounded && ReadFile(
        file, bytes.data(), static_cast<DWORD>(size.QuadPart), &bytes_read, nullptr);
    LARGE_INTEGER size_after{};
    const bool stable_size = read && GetFileSizeEx(file, &size_after) &&
        size_after.QuadPart == size.QuadPart &&
        bytes_read == static_cast<DWORD>(size.QuadPart);
    CloseHandle(file);
    if (!stable_size) {
        return;
    }
    const std::string_view text(bytes.data(), bytes_read);
    const auto hash = content_hash(text);
    if (g_saw_content && hash == g_last_content_hash) {
        return;
    }
    g_saw_content = true;
    g_last_content_hash = hash;
    const auto request = parse_performance_command_request(text);
    if (!request.valid()) {
        stereo_diagnostic_log(
            "PerfCommandDiag rejected malformed-or-unknown request bytes=%lu simulator=1 keyCatchers=0x%X connection=%d",
            bytes_read, key_catchers, connection_state);
        return;
    }
    const auto id = performance_command_id_name(request.id);
    if (!performance_command_nonce_is_fresh(request, g_last_consumed_nonce)) {
        stereo_diagnostic_log(
            "PerfCommandDiag ignored nonce=%llu id=%s reason=stale last=%llu keyCatchers=0x%X connection=%d",
            static_cast<unsigned long long>(request.nonce), id.data(),
            static_cast<unsigned long long>(g_last_consumed_nonce),
            key_catchers, connection_state);
        return;
    }
    // Consume before calling the engine seam: even a failed queue never
    // repeats a toggle/load on every frame. A retry needs a fresh nonce.
    g_last_consumed_nonce = request.nonce;
    const auto command = performance_command_native_text(
        request.id, (key_catchers & kT4ConsoleKeyCatcher) != 0);
    const bool already_closed = request.id == PerformanceCommandId::console_close &&
        command.empty();
    const bool queued = !command.empty() && queue(validated_cbuf_address, command.data());
    stereo_diagnostic_log(
        "PerfCommandDiag ack nonce=%llu id=%s status=%s simulator=1 keyCatchers=0x%X connection=%d",
        static_cast<unsigned long long>(request.nonce), id.data(),
        already_closed ? "already-closed" : queued ? "queued" : "queue-failed",
        key_catchers, connection_state);
}

}  // namespace wawvr::mod
