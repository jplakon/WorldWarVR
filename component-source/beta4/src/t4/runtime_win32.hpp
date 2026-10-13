#pragma once

#include "bindings.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wawvr::t4 {

inline constexpr std::uint32_t kWrappedCodeReadinessTimeoutMs = 30'000;
inline constexpr std::uint32_t kWrappedCodeReadinessIntervalMs = 25;

enum class LoadedCodeReadinessStatus : std::uint8_t {
    ready,
    timed_out,
};

struct LoadedCodeReadinessResult final {
    LoadedCodeReadinessStatus status{LoadedCodeReadinessStatus::timed_out};
    std::uint32_t attempts{};
    std::uint64_t elapsed_ms{};

    [[nodiscard]] constexpr bool ready() const noexcept {
        return status == LoadedCodeReadinessStatus::ready;
    }
};

// Polls only the selected profile's mapped instruction sentinels. This is used
// for a SteamStub-wrapped process after its complete on-disk identity has
// matched, allowing the main process a bounded interval to restore game code.
// Plain profiles do not use this delay.
[[nodiscard]] LoadedCodeReadinessResult wait_for_loaded_code_readiness(
    ModuleView loaded_module,
    const ExecutableProfile& profile,
    std::uint32_t timeout_ms = kWrappedCodeReadinessTimeoutMs,
    std::uint32_t interval_ms = kWrappedCodeReadinessIntervalMs);

struct CurrentProcessSnapshot final {
    std::filesystem::path executable_path{};
    std::vector<std::uint8_t> executable_file{};
    ModuleView loaded_module{};
};

struct SnapshotResult final {
    std::optional<CurrentProcessSnapshot> snapshot{};
    std::string error{};

    [[nodiscard]] bool ok() const noexcept { return snapshot.has_value(); }
};

// Call this from a bootstrap worker after DllMain has returned. File I/O and
// PSAPI calls are intentionally kept out of the loader lock.
[[nodiscard]] SnapshotResult capture_current_process();

struct CurrentProcessBindingResult final {
    std::filesystem::path executable_path{};
    std::optional<ValidatedBindings> bindings{};
    ValidationReport validation{};
    std::string capture_error{};

    [[nodiscard]] bool ok() const noexcept {
        return capture_error.empty() && bindings.has_value() && validation.ok();
    }
};

[[nodiscard]] CurrentProcessBindingResult validate_and_bind_current_process(
    const ExecutableProfile& profile);

// Captures the process once, selects only an immutable size+SHA-256 identity
// from `supported_profiles()`, and then performs the complete disk/mapped-image
// validation for that selected profile.
[[nodiscard]] CurrentProcessBindingResult
validate_and_bind_supported_current_process();

}  // namespace wawvr::t4
