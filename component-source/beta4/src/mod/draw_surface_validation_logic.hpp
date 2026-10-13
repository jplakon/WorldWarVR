#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace wawvr::mod {

enum class DrawSurfaceAccess : std::uint8_t {
    native_passthrough,
    inspect,
};

[[nodiscard]] constexpr bool needs_mod_draw_surface_inspection(
    const bool emissive_draw, const bool scope_pass,
    const bool scope_slots_proven_empty) noexcept {
    return emissive_draw || scope_pass || !scope_slots_proven_empty;
}

// A native-only pass never dereferences the surface array in the mod. Keep
// its cheap shape checks, but query page protection only at a mod reader.
// This deliberately stores no validation state across calls or frames.
template <typename ReadableRange>
[[nodiscard]] const char* validate_draw_surface_access(
    const void* const surfaces,
    const std::uint32_t count,
    const std::uint32_t maximum_count,
    const std::size_t surface_size,
    const DrawSurfaceAccess access,
    ReadableRange&& readable_range) noexcept {
    if (count > maximum_count) {
        return "count-over-cap";
    }
    if (count != 0 && surfaces == nullptr) {
        return "nonzero-count-null-surfaces";
    }
    if (surface_size == 0 ||
        (count != 0 && count >
            (std::numeric_limits<std::size_t>::max)() / surface_size)) {
        return "surface-byte-overflow";
    }
    if (access == DrawSurfaceAccess::inspect && count != 0 &&
        !readable_range(
            surfaces, static_cast<std::size_t>(count) * surface_size)) {
        return "surface-range-unreadable";
    }
    return nullptr;
}

}  // namespace wawvr::mod
