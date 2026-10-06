#pragma once

#include "address.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wawvr::t4 {

struct Pe32Section final {
    std::array<char, 9> name{};
    Rva virtual_address{};
    std::uint32_t virtual_size{};
    std::uint32_t raw_offset{};
    std::uint32_t raw_size{};
    std::uint32_t characteristics{};
};

struct Pe32ImageInfo final {
    std::uint16_t machine{};
    std::uint16_t section_count{};
    std::uint32_t coff_timestamp{};
    std::uint16_t characteristics{};
    std::uint16_t optional_header_magic{};
    Rva entry_point_rva{};
    std::uint32_t preferred_image_base{};
    std::uint32_t section_alignment{};
    std::uint32_t file_alignment{};
    std::uint32_t size_of_image{};
    std::uint32_t size_of_headers{};
    std::uint16_t dll_characteristics{};
    std::vector<Pe32Section> sections{};
};

struct PeParseResult final {
    std::optional<Pe32ImageInfo> image{};
    std::string error{};

    [[nodiscard]] bool ok() const noexcept { return image.has_value(); }
};

// Parses only PE/COFF metadata. It performs bounds checks before every read and
// does not reinterpret_cast untrusted bytes into Windows structs.
[[nodiscard]] PeParseResult parse_pe32(std::span<const std::uint8_t> image_bytes);

// Converts an RVA to an on-disk file offset. Returns no value for virtual-only
// data (for example the uninitialized tail of .data) or for an invalid range.
[[nodiscard]] std::optional<std::size_t> rva_to_file_offset(
    const Pe32ImageInfo& image, Rva rva, std::size_t width,
    std::size_t file_size) noexcept;

}  // namespace wawvr::t4
