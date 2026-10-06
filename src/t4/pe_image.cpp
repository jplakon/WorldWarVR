#include "pe_image.hpp"

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace wawvr::t4 {
namespace {

constexpr std::uint16_t kDosSignature = 0x5A4D;
constexpr std::uint32_t kPeSignature = 0x00004550;
constexpr std::uint16_t kPe32Magic = 0x010B;
constexpr std::size_t kCoffHeaderSize = 20;
constexpr std::size_t kSectionHeaderSize = 40;
constexpr std::size_t kMinimumPe32OptionalHeaderSize = 72;
constexpr std::size_t kMaximumReasonableSectionCount = 96;

bool range_fits(const std::size_t offset, const std::size_t width,
                const std::size_t total) noexcept {
    return offset <= total && width <= total - offset;
}

std::optional<std::uint16_t> read_u16(const std::span<const std::uint8_t> bytes,
                                      const std::size_t offset) noexcept {
    if (!range_fits(offset, 2, bytes.size())) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8U));
}

std::optional<std::uint32_t> read_u32(const std::span<const std::uint8_t> bytes,
                                      const std::size_t offset) noexcept {
    if (!range_fits(offset, 4, bytes.size())) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

PeParseResult fail(std::string message) {
    return {.image = std::nullopt, .error = std::move(message)};
}

}  // namespace

PeParseResult parse_pe32(const std::span<const std::uint8_t> bytes) {
    if (read_u16(bytes, 0) != kDosSignature) {
        return fail("missing DOS MZ signature");
    }

    const auto pe_offset_value = read_u32(bytes, 0x3C);
    if (!pe_offset_value) {
        return fail("truncated DOS header");
    }
    const auto pe_offset = static_cast<std::size_t>(*pe_offset_value);
    if (!range_fits(pe_offset, 4 + kCoffHeaderSize, bytes.size())) {
        return fail("PE header offset is outside the image");
    }
    if (read_u32(bytes, pe_offset) != kPeSignature) {
        return fail("missing PE signature");
    }

    const auto coff = pe_offset + 4;
    const auto machine = read_u16(bytes, coff);
    const auto section_count = read_u16(bytes, coff + 2);
    const auto timestamp = read_u32(bytes, coff + 4);
    const auto optional_size = read_u16(bytes, coff + 16);
    const auto characteristics = read_u16(bytes, coff + 18);
    if (!machine || !section_count || !timestamp || !optional_size || !characteristics) {
        return fail("truncated COFF header");
    }
    if (*section_count == 0 || *section_count > kMaximumReasonableSectionCount) {
        return fail("invalid PE section count");
    }
    if (*optional_size < kMinimumPe32OptionalHeaderSize) {
        return fail("PE32 optional header is too small");
    }

    const auto optional = coff + kCoffHeaderSize;
    if (!range_fits(optional, *optional_size, bytes.size())) {
        return fail("truncated PE optional header");
    }
    const auto magic = read_u16(bytes, optional);
    if (magic != kPe32Magic) {
        return fail("image is not PE32 (x86)");
    }

    const auto entry_point = read_u32(bytes, optional + 16);
    const auto image_base = read_u32(bytes, optional + 28);
    const auto section_alignment = read_u32(bytes, optional + 32);
    const auto file_alignment = read_u32(bytes, optional + 36);
    const auto size_of_image = read_u32(bytes, optional + 56);
    const auto size_of_headers = read_u32(bytes, optional + 60);
    const auto dll_characteristics = read_u16(bytes, optional + 70);
    if (!entry_point || !image_base || !section_alignment || !file_alignment ||
        !size_of_image || !size_of_headers || !dll_characteristics) {
        return fail("truncated required PE32 fields");
    }

    const auto section_table = optional + *optional_size;
    if (*section_count >
        (std::numeric_limits<std::size_t>::max() - section_table) / kSectionHeaderSize) {
        return fail("PE section table size overflows");
    }
    const auto section_table_size =
        static_cast<std::size_t>(*section_count) * kSectionHeaderSize;
    if (!range_fits(section_table, section_table_size, bytes.size())) {
        return fail("truncated PE section table");
    }

    Pe32ImageInfo result{
        .machine = *machine,
        .section_count = *section_count,
        .coff_timestamp = *timestamp,
        .characteristics = *characteristics,
        .optional_header_magic = *magic,
        .entry_point_rva = *entry_point,
        .preferred_image_base = *image_base,
        .section_alignment = *section_alignment,
        .file_alignment = *file_alignment,
        .size_of_image = *size_of_image,
        .size_of_headers = *size_of_headers,
    };
    result.dll_characteristics = *dll_characteristics;
    result.sections.reserve(*section_count);

    for (std::size_t index = 0; index < *section_count; ++index) {
        const auto offset = section_table + index * kSectionHeaderSize;
        Pe32Section section{};
        for (std::size_t character = 0; character < 8; ++character) {
            section.name[character] = static_cast<char>(bytes[offset + character]);
        }
        section.name[8] = '\0';

        const auto virtual_size = read_u32(bytes, offset + 8);
        const auto virtual_address = read_u32(bytes, offset + 12);
        const auto raw_size = read_u32(bytes, offset + 16);
        const auto raw_offset = read_u32(bytes, offset + 20);
        const auto section_characteristics = read_u32(bytes, offset + 36);
        if (!virtual_size || !virtual_address || !raw_size || !raw_offset ||
            !section_characteristics) {
            return fail("truncated PE section header");
        }
        section.virtual_size = *virtual_size;
        section.virtual_address = *virtual_address;
        section.raw_size = *raw_size;
        section.raw_offset = *raw_offset;
        section.characteristics = *section_characteristics;
        result.sections.push_back(section);
    }

    return {.image = std::move(result), .error = {}};
}

std::optional<std::size_t> rva_to_file_offset(const Pe32ImageInfo& image,
                                               const Rva rva,
                                               const std::size_t width,
                                               const std::size_t file_size) noexcept {
    const auto rva_value = static_cast<std::size_t>(rva);
    if (rva_value < image.size_of_headers) {
        const auto header_size = static_cast<std::size_t>(image.size_of_headers);
        return width <= header_size - rva_value && range_fits(rva_value, width, file_size)
                   ? std::optional{rva_value}
                   : std::nullopt;
    }

    for (const auto& section : image.sections) {
        if (rva < section.virtual_address) {
            continue;
        }
        const auto delta = static_cast<std::size_t>(rva - section.virtual_address);
        if (delta > section.raw_size || width > section.raw_size - delta) {
            continue;
        }
        const auto raw = static_cast<std::size_t>(section.raw_offset);
        if (raw > std::numeric_limits<std::size_t>::max() - delta) {
            return std::nullopt;
        }
        const auto file_offset = raw + delta;
        return range_fits(file_offset, width, file_size) ? std::optional{file_offset}
                                                         : std::nullopt;
    }
    return std::nullopt;
}

}  // namespace wawvr::t4
