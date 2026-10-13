#pragma once

#include "profile.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace wawvr::t4 {

[[nodiscard]] Sha256Digest sha256(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] std::string sha256_hex(const Sha256Digest& digest);
[[nodiscard]] bool parse_sha256_hex(std::string_view text, Sha256Digest& output) noexcept;

}  // namespace wawvr::t4
