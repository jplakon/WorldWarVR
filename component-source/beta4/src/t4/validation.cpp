#include "validation.hpp"

#include "sha256.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace wawvr::t4 {
namespace {

std::string hex32(const std::uint32_t value) {
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
           << value;
    return output.str();
}

std::string byte_sequence(const std::span<const std::uint8_t> bytes) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0) {
            output << ' ';
        }
        output << std::setw(2) << static_cast<unsigned>(bytes[index]);
    }
    return output.str();
}

void validate_headers(const Pe32ImageInfo& image, const ExecutableProfile& profile,
                      ValidationReport& report) {
    if (image.machine != profile.machine) {
        report.add(ValidationCode::machine_mismatch,
                   "PE machine mismatch: expected " + hex32(profile.machine) +
                       ", got " + hex32(image.machine));
    }
    if (image.section_count != profile.section_count) {
        report.add(ValidationCode::section_count_mismatch,
                   "PE section count mismatch: expected " +
                       std::to_string(profile.section_count) + ", got " +
                       std::to_string(image.section_count));
    }
    if (image.coff_timestamp != profile.coff_timestamp) {
        report.add(ValidationCode::coff_timestamp_mismatch,
                   "COFF timestamp mismatch: expected " + hex32(profile.coff_timestamp) +
                       ", got " + hex32(image.coff_timestamp));
    }
    if (image.characteristics != profile.coff_characteristics) {
        report.add(ValidationCode::coff_characteristics_mismatch,
                   "COFF characteristics mismatch: expected " +
                       hex32(profile.coff_characteristics) + ", got " +
                       hex32(image.characteristics));
    }
    if (image.dll_characteristics != profile.dll_characteristics) {
        report.add(ValidationCode::dll_characteristics_mismatch,
                   "DLL characteristics mismatch: expected " +
                       hex32(profile.dll_characteristics) + ", got " +
                       hex32(image.dll_characteristics));
    }
    if (image.entry_point_rva != profile.entry_point_rva) {
        report.add(ValidationCode::entry_point_mismatch,
                   "entry point mismatch: expected RVA " + hex32(profile.entry_point_rva) +
                       ", got " + hex32(image.entry_point_rva));
    }
    if (image.preferred_image_base != profile.preferred_image_base) {
        report.add(ValidationCode::preferred_image_base_mismatch,
                   "preferred image base mismatch: expected " +
                       hex32(profile.preferred_image_base) + ", got " +
                       hex32(image.preferred_image_base));
    }
    if (image.size_of_image != profile.size_of_image) {
        report.add(ValidationCode::size_of_image_mismatch,
                   "SizeOfImage mismatch: expected " + hex32(profile.size_of_image) +
                       ", got " + hex32(image.size_of_image));
    }
}

void validate_file_hook_sites(const std::span<const std::uint8_t> file_bytes,
                              const Pe32ImageInfo& image,
                              const ExecutableProfile& profile,
                              ValidationReport& report) {
    for (const auto& site : profile.sites) {
        const auto expected = site.expected_bytes();
        const auto file_offset =
            rva_to_file_offset(image, site.rva, expected.size(), file_bytes.size());
        if (!file_offset) {
            report.add(ValidationCode::hook_site_unmapped,
                       std::string(site.name) + " at RVA " + hex32(site.rva) +
                           " is not backed by executable file bytes");
            continue;
        }
        const auto actual = file_bytes.subspan(*file_offset, expected.size());
        if (!std::ranges::equal(actual, expected)) {
            report.add(ValidationCode::hook_bytes_mismatch,
                       std::string(site.name) + " byte mismatch at RVA " +
                           hex32(site.rva) + "; expected [" + byte_sequence(expected) +
                           "], got [" + byte_sequence(actual) + "]");
        }
    }
}

}  // namespace

void ValidationReport::add(const ValidationCode code, std::string message) {
    issues.push_back({.code = code, .message = std::move(message)});
}

void ValidationReport::append(ValidationReport other) {
    issues.insert(issues.end(), std::make_move_iterator(other.issues.begin()),
                  std::make_move_iterator(other.issues.end()));
}

ValidationReport validate_executable_file(const std::span<const std::uint8_t> file_bytes,
                                          const ExecutableProfile& profile) {
    ValidationReport report{};
    if (file_bytes.size() != profile.file_size) {
        report.add(ValidationCode::file_size_mismatch,
                   "file size mismatch: expected " + std::to_string(profile.file_size) +
                       " bytes, got " + std::to_string(file_bytes.size()));
    }

    const auto actual_hash = sha256(file_bytes);
    if (actual_hash != profile.sha256) {
        report.add(ValidationCode::sha256_mismatch,
                   "SHA-256 mismatch: expected " + sha256_hex(profile.sha256) +
                       ", got " + sha256_hex(actual_hash));
    }

    auto parsed = parse_pe32(file_bytes);
    if (!parsed.ok()) {
        report.add(ValidationCode::pe_parse_failed,
                   "could not parse executable as PE32: " + parsed.error);
        return report;
    }

    validate_headers(*parsed.image, profile, report);
    if (profile.on_disk_code == OnDiskCodeState::plain) {
        validate_file_hook_sites(file_bytes, *parsed.image, profile, report);
    }
    return report;
}

ValidationReport validate_loaded_hook_sites(const ModuleView module,
                                            const ExecutableProfile& profile) {
    ValidationReport report{};
    for (const auto& site : profile.sites) {
        const auto expected = site.expected_bytes();
        const auto actual = module.bytes(site.rva, expected.size());
        if (actual.size() != expected.size()) {
            report.add(ValidationCode::hook_site_unmapped,
                       std::string(site.name) + " at RVA " + hex32(site.rva) +
                           " is outside the mapped image");
            continue;
        }
        if (!std::ranges::equal(actual, expected)) {
            report.add(ValidationCode::hook_bytes_mismatch,
                       std::string(site.name) + " byte mismatch at RVA " +
                           hex32(site.rva) + "; expected [" + byte_sequence(expected) +
                           "], got [" + byte_sequence(actual) + "]");
        }
    }
    return report;
}

ValidationReport validate_loaded_module(const ModuleView module,
                                        const ExecutableProfile& profile) {
    ValidationReport report{};
    if (module.base == nullptr || module.size < profile.size_of_image) {
        report.add(ValidationCode::module_view_too_small,
                   "mapped module is null or smaller than the expected SizeOfImage");
        return report;
    }

    const auto loaded_base = reinterpret_cast<std::uintptr_t>(module.base);
    if (loaded_base != profile.preferred_image_base) {
        report.add(ValidationCode::loaded_base_mismatch,
                   "loaded base mismatch: this relocation-stripped build must load at " +
                       hex32(profile.preferred_image_base));
    }

    const auto parsed = parse_pe32({module.base, module.size});
    if (!parsed.ok()) {
        report.add(ValidationCode::pe_parse_failed,
                   "could not parse mapped module as PE32: " + parsed.error);
        return report;
    }
    validate_headers(*parsed.image, profile, report);
    report.append(validate_loaded_hook_sites(module, profile));
    return report;
}

}  // namespace wawvr::t4
