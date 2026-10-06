#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace wawvr::t4 {

using Rva = std::uint32_t;

// A read-only view of a PE image after the Windows loader has mapped it.
// `size` is SizeOfImage, not the executable's on-disk byte count.
struct ModuleView final {
    const std::uint8_t* base{};
    std::size_t size{};

    [[nodiscard]] constexpr bool contains(const Rva rva,
                                           const std::size_t width = 1) const noexcept {
        const auto offset = static_cast<std::size_t>(rva);
        return base != nullptr && offset <= size && width <= (size - offset);
    }

    [[nodiscard]] constexpr std::span<const std::uint8_t> bytes(
        const Rva rva, const std::size_t width) const noexcept {
        if (!contains(rva, width)) {
            return {};
        }
        return {base + static_cast<std::size_t>(rva), width};
    }

    [[nodiscard]] constexpr std::optional<std::uintptr_t> address(
        const Rva rva, const std::size_t width = 1) const noexcept {
        if (!contains(rva, width)) {
            return std::nullopt;
        }

        const auto raw_base = reinterpret_cast<std::uintptr_t>(base);
        if (raw_base > std::numeric_limits<std::uintptr_t>::max() - rva) {
            return std::nullopt;
        }
        return raw_base + rva;
    }
};

[[nodiscard]] constexpr std::optional<Rva> preferred_va_to_rva(
    const std::uint32_t preferred_va, const std::uint32_t preferred_image_base) noexcept {
    if (preferred_va < preferred_image_base) {
        return std::nullopt;
    }
    return preferred_va - preferred_image_base;
}

[[nodiscard]] constexpr std::optional<std::uint32_t> rva_to_preferred_va(
    const Rva rva, const std::uint32_t preferred_image_base) noexcept {
    if (rva > std::numeric_limits<std::uint32_t>::max() - preferred_image_base) {
        return std::nullopt;
    }
    return preferred_image_base + rva;
}

}  // namespace wawvr::t4
