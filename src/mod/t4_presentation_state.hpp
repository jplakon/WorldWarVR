#pragma once

#include "t4/bindings.hpp"

#include <cstdint>

namespace wawvr::mod {

struct T4PresentationState final {
    std::uint32_t key_catchers{};
    std::int32_t connection_state{};
    std::int32_t active_connection_state{10};
    bool valid{};
};

// Binds only the two exact-profile state words used to select stereo versus
// mono comfort presentation. No engine memory is mutated.
[[nodiscard]] bool bind_t4_presentation_state(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;
[[nodiscard]] T4PresentationState read_t4_presentation_state() noexcept;
void clear_t4_presentation_state() noexcept;

}  // namespace wawvr::mod
