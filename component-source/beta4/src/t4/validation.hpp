#pragma once

#include "address.hpp"
#include "pe_image.hpp"
#include "profile.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace wawvr::t4 {

enum class ValidationCode : std::uint8_t {
    file_size_mismatch,
    sha256_mismatch,
    pe_parse_failed,
    machine_mismatch,
    section_count_mismatch,
    coff_timestamp_mismatch,
    coff_characteristics_mismatch,
    dll_characteristics_mismatch,
    entry_point_mismatch,
    preferred_image_base_mismatch,
    loaded_base_mismatch,
    size_of_image_mismatch,
    module_view_too_small,
    hook_site_unmapped,
    hook_bytes_mismatch,
};

struct ValidationIssue final {
    ValidationCode code{};
    std::string message{};
};

struct ValidationReport final {
    std::vector<ValidationIssue> issues{};

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
    void add(ValidationCode code, std::string message);
    void append(ValidationReport other);
};

// Strong on-disk gate: validates the complete SHA-256 plus PE metadata and
// expected bytes. This must pass before the process is launched or patched.
[[nodiscard]] ValidationReport validate_executable_file(
    std::span<const std::uint8_t> file_bytes, const ExecutableProfile& profile);

// In-memory gate used immediately before hooks are installed. It verifies the
// loaded base/header and rechecks every expected byte. It intentionally does not
// attempt to hash mutable mapped memory.
[[nodiscard]] ValidationReport validate_loaded_module(
    ModuleView module, const ExecutableProfile& profile);

// Exposed separately so pure unit tests can exercise byte/range validation on
// synthetic mapped images without requiring a module at address 0x00400000.
[[nodiscard]] ValidationReport validate_loaded_hook_sites(
    ModuleView module, const ExecutableProfile& profile);

}  // namespace wawvr::t4
