#include "sha256.hpp"

#include <array>
#include <bit>
#include <cstddef>

namespace wawvr::t4 {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{{
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1,
    0x923F82A4, 0xAB1C5ED5, 0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
    0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174, 0xE49B69C1, 0xEFBE4786,
    0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147,
    0x06CA6351, 0x14292967, 0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
    0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85, 0xA2BFE8A1, 0xA81A664B,
    0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A,
    0x5B9CCA4F, 0x682E6FF3, 0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
    0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2,
}};

constexpr std::uint32_t choose(const std::uint32_t x, const std::uint32_t y,
                               const std::uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}

constexpr std::uint32_t majority(const std::uint32_t x, const std::uint32_t y,
                                 const std::uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

constexpr std::uint32_t load_be32(const std::uint8_t* bytes) noexcept {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

void store_be32(std::uint8_t* output, const std::uint32_t value) noexcept {
    output[0] = static_cast<std::uint8_t>(value >> 24U);
    output[1] = static_cast<std::uint8_t>(value >> 16U);
    output[2] = static_cast<std::uint8_t>(value >> 8U);
    output[3] = static_cast<std::uint8_t>(value);
}

void transform(std::array<std::uint32_t, 8>& state, const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) {
        words[i] = load_be32(block + i * 4);
    }
    for (std::size_t i = 16; i < words.size(); ++i) {
        const auto s0 = std::rotr(words[i - 15], 7) ^ std::rotr(words[i - 15], 18) ^
                        (words[i - 15] >> 3U);
        const auto s1 = std::rotr(words[i - 2], 17) ^ std::rotr(words[i - 2], 19) ^
                        (words[i - 2] >> 10U);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    auto a = state[0];
    auto b = state[1];
    auto c = state[2];
    auto d = state[3];
    auto e = state[4];
    auto f = state[5];
    auto g = state[6];
    auto h = state[7];

    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto upper_e = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const auto temporary1 = h + upper_e + choose(e, f, g) + kRoundConstants[i] + words[i];
        const auto upper_a = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const auto temporary2 = upper_a + majority(a, b, c);

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

constexpr int hex_value(const char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

}  // namespace

Sha256Digest sha256(const std::span<const std::uint8_t> bytes) noexcept {
    std::array<std::uint32_t, 8> state{{
        0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
        0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19,
    }};

    std::size_t offset = 0;
    while (bytes.size() - offset >= 64) {
        transform(state, bytes.data() + offset);
        offset += 64;
    }

    std::array<std::uint8_t, 128> tail{};
    const auto remainder = bytes.size() - offset;
    for (std::size_t i = 0; i < remainder; ++i) {
        tail[i] = bytes[offset + i];
    }
    tail[remainder] = 0x80;

    const std::size_t tail_size = remainder < 56 ? 64 : 128;
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;
    for (std::size_t i = 0; i < 8; ++i) {
        tail[tail_size - 1 - i] = static_cast<std::uint8_t>(bit_length >> (i * 8U));
    }
    transform(state, tail.data());
    if (tail_size == 128) {
        transform(state, tail.data() + 64);
    }

    Sha256Digest digest{};
    for (std::size_t i = 0; i < state.size(); ++i) {
        store_be32(digest.bytes.data() + i * 4, state[i]);
    }
    return digest;
}

std::string sha256_hex(const Sha256Digest& digest) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2, '0');
    for (std::size_t i = 0; i < digest.bytes.size(); ++i) {
        result[i * 2] = kHex[digest.bytes[i] >> 4U];
        result[i * 2 + 1] = kHex[digest.bytes[i] & 0x0F];
    }
    return result;
}

bool parse_sha256_hex(const std::string_view text, Sha256Digest& output) noexcept {
    if (text.size() != output.bytes.size() * 2) {
        return false;
    }
    Sha256Digest parsed{};
    for (std::size_t i = 0; i < parsed.bytes.size(); ++i) {
        const auto high = hex_value(text[i * 2]);
        const auto low = hex_value(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        parsed.bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    output = parsed;
    return true;
}

}  // namespace wawvr::t4
