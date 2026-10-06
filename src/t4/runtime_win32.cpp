#include "runtime_win32.hpp"

#include "sha256.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Psapi.h>
#if defined(_MSC_VER)
#pragma comment(lib, "Psapi.lib")
#endif
#endif

namespace wawvr::t4 {
namespace {

#if defined(_WIN32)
std::optional<std::filesystem::path> current_executable_path(std::string& error) {
    std::wstring buffer(512, L'\0');
    for (;;) {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(),
                                               static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            error = "GetModuleFileNameW failed with error " +
                    std::to_string(GetLastError());
            return std::nullopt;
        }
        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return std::filesystem::path(buffer);
        }
        if (buffer.size() >= 32'768) {
            error = "current executable path exceeds the Windows path limit";
            return std::nullopt;
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::optional<ModuleView> current_module_view(std::string& error) {
    const auto module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        error = "GetModuleHandleW(nullptr) failed with error " +
                std::to_string(GetLastError());
        return std::nullopt;
    }

    MODULEINFO info{};
    if (GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)) == FALSE) {
        error = "GetModuleInformation failed with error " +
                std::to_string(GetLastError());
        return std::nullopt;
    }
    if (info.lpBaseOfDll == nullptr || info.SizeOfImage == 0) {
        error = "GetModuleInformation returned an empty executable mapping";
        return std::nullopt;
    }
    return ModuleView{
        .base = static_cast<const std::uint8_t*>(info.lpBaseOfDll),
        .size = info.SizeOfImage,
    };
}
#endif

std::optional<std::vector<std::uint8_t>> read_binary_file(
    const std::filesystem::path& path, std::string& error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "could not open executable file for validation";
        return std::nullopt;
    }
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uintmax_t>(end) >
                       std::numeric_limits<std::size_t>::max()) {
        error = "executable file size is invalid or unsupported";
        return std::nullopt;
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty() &&
        !input.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()))) {
        error = "could not read the complete executable file";
        return std::nullopt;
    }
    return bytes;
}

}  // namespace

LoadedCodeReadinessResult wait_for_loaded_code_readiness(
    const ModuleView loaded_module,
    const ExecutableProfile& profile,
    const std::uint32_t timeout_ms,
    const std::uint32_t interval_ms) {
    LoadedCodeReadinessResult result{};
#if !defined(_WIN32)
    result.attempts = 1;
    result.status = validate_loaded_hook_sites(loaded_module, profile).ok()
        ? LoadedCodeReadinessStatus::ready
        : LoadedCodeReadinessStatus::timed_out;
    return result;
#else
    const ULONGLONG started = GetTickCount64();
    const DWORD bounded_interval = std::max<DWORD>(1U, interval_ms);
    for (;;) {
        ++result.attempts;
        if (validate_loaded_hook_sites(loaded_module, profile).ok()) {
            result.elapsed_ms = GetTickCount64() - started;
            result.status = LoadedCodeReadinessStatus::ready;
            return result;
        }

        result.elapsed_ms = GetTickCount64() - started;
        if (result.elapsed_ms >= timeout_ms) {
            result.status = LoadedCodeReadinessStatus::timed_out;
            return result;
        }

        const auto remaining = static_cast<DWORD>(std::min<std::uint64_t>(
            timeout_ms - result.elapsed_ms,
            std::numeric_limits<DWORD>::max()));
        Sleep(std::min(bounded_interval, remaining));
    }
#endif
}

SnapshotResult capture_current_process() {
#if !defined(_WIN32)
    return {.snapshot = std::nullopt,
            .error = "T4 runtime capture is available only on Windows"};
#else
    std::string error{};
    auto path = current_executable_path(error);
    if (!path) {
        return {.snapshot = std::nullopt, .error = std::move(error)};
    }
    auto module = current_module_view(error);
    if (!module) {
        return {.snapshot = std::nullopt, .error = std::move(error)};
    }
    auto file = read_binary_file(*path, error);
    if (!file) {
        return {.snapshot = std::nullopt, .error = std::move(error)};
    }
    return {
        .snapshot = CurrentProcessSnapshot{
            .executable_path = std::move(*path),
            .executable_file = std::move(*file),
            .loaded_module = *module,
        },
        .error = {},
    };
#endif
}

CurrentProcessBindingResult validate_and_bind_current_process(
    const ExecutableProfile& profile) {
    auto captured = capture_current_process();
    if (!captured.ok()) {
        return {
            .capture_error = std::move(captured.error),
        };
    }

    auto& snapshot = *captured.snapshot;
    if (profile.on_disk_code == OnDiskCodeState::steam_drm_wrapped) {
        static_cast<void>(wait_for_loaded_code_readiness(
            snapshot.loaded_module, profile));
    }
    auto bound = validate_and_bind(snapshot.executable_file, snapshot.loaded_module, profile);
    return {
        .executable_path = std::move(snapshot.executable_path),
        .bindings = std::move(bound.bindings),
        .validation = std::move(bound.validation),
        .capture_error = {},
    };
}

CurrentProcessBindingResult validate_and_bind_supported_current_process() {
    auto captured = capture_current_process();
    if (!captured.ok()) {
        return {
            .capture_error = std::move(captured.error),
        };
    }

    auto& snapshot = *captured.snapshot;
    const auto digest = sha256(snapshot.executable_file);
    const auto* const profile = find_supported_profile(
        snapshot.executable_file.size(), digest);
    if (profile == nullptr) {
        ValidationReport unsupported{};
        unsupported.add(
            ValidationCode::sha256_mismatch,
            "unsupported T4 executable identity: size=" +
                std::to_string(snapshot.executable_file.size()) +
                " sha256=" + sha256_hex(digest));
        return {
            .executable_path = std::move(snapshot.executable_path),
            .bindings = std::nullopt,
            .validation = std::move(unsupported),
            .capture_error = {},
        };
    }

    if (profile->on_disk_code == OnDiskCodeState::steam_drm_wrapped) {
        static_cast<void>(wait_for_loaded_code_readiness(
            snapshot.loaded_module, *profile));
    }
    auto bound = validate_and_bind(
        snapshot.executable_file, snapshot.loaded_module, *profile);
    return {
        .executable_path = std::move(snapshot.executable_path),
        .bindings = std::move(bound.bindings),
        .validation = std::move(bound.validation),
        .capture_error = {},
    };
}

}  // namespace wawvr::t4
