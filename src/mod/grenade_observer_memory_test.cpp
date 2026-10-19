// SPDX-License-Identifier: GPL-3.0-only
#include "grenade_observer_memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using namespace wawvr::mod;

int failures = 0;
std::size_t cases = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool observe_copy(void* const destination, const std::uintptr_t source,
    const std::size_t bytes, const bool expected,
    const DWORD marker, const std::string_view message) {
    SetLastError(marker);
    const bool copied = copy_grenade_observer_memory(destination, source, bytes);
    const DWORD after = GetLastError();
    ++cases;
    expect(copied == expected, message);
    expect(after == marker, "every copy/validation path preserves caller GetLastError");
    return copied;
}

void test_validation_without_native_reads() {
    std::array<std::uint8_t, 32> source{};
    std::array<std::uint8_t, 32> destination{};
    source.fill(0x31U);
    destination.fill(0xA5U);
    const auto unchangedDestination = destination;
    const auto sourceAddress = reinterpret_cast<std::uintptr_t>(source.data());
    const auto maximum = (std::numeric_limits<std::uintptr_t>::max)();
    observe_copy(nullptr, sourceAddress, 1U, false, 0x12345678U,
        "null destination is rejected");
    observe_copy(destination.data(), 0U, 1U, false, 0x87654321U,
        "zero source is rejected");
    observe_copy(destination.data(), sourceAddress, 0U, false, 0U,
        "zero length is not a successful snapshot");
    observe_copy(destination.data(), sourceAddress, 0x1001U, false, 0xFFFFFFFFU,
        "copy above the 0x1000-byte bound is rejected before access");
    observe_copy(destination.data(), sourceAddress,
        (std::numeric_limits<std::size_t>::max)(), false, 0xE1234567U,
        "unbounded length is rejected before any address arithmetic");
    observe_copy(destination.data(), maximum, 1U, false, 0xDEADBEEFU,
        "exclusive source end overflow is rejected");
    observe_copy(destination.data(), maximum - 7U, 8U, false, 0x01234567U,
        "multi-byte source end overflow is rejected");
    observe_copy(reinterpret_cast<void*>(maximum), sourceAddress, 1U,
        false, 0x13579BDFU, "exclusive destination end overflow is rejected");
    observe_copy(reinterpret_cast<void*>(maximum - 7U), sourceAddress, 8U,
        false, 0x2468ACE0U, "multi-byte destination end overflow is rejected");
    expect(destination == unchangedDestination,
        "guarded invalid requests do not write beyond or into destination");
    observe_copy(destination.data(), 1U, 8U, false, 0xFEDCBA98U,
        "unmapped nonnull source fails as a value rather than a direct load");
    // A failed native copy is never parsed, even for this unmapped source.
}

void test_real_owned_pages() {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto pageSize = static_cast<std::size_t>(system.dwPageSize);
    expect(pageSize >= 0x1000U, "owned source page can hold maximum bounded copy");
    if (pageSize < 0x1000U) return;
    auto* const allocation = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, pageSize * 2U, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    expect(allocation != nullptr, "test allocates its own two source pages");
    if (allocation == nullptr) return;
    struct Release final {
        void* address;
        ~Release() { VirtualFree(address, 0, MEM_RELEASE); }
    } release{allocation};
    for (std::size_t i = 0; i < pageSize * 2U; ++i) {
        allocation[i] = static_cast<std::uint8_t>((i * 13U + 29U) & 0xFFU);
    }
    const auto address = reinterpret_cast<std::uintptr_t>(allocation);
    std::array<std::uint8_t, 0x1000> destination{};
    for (const auto bytes : std::array<std::size_t, 4>{1U, 0x1C8U, 0x920U, 0x1000U}) {
        destination.fill(0xA5U);
        const bool copied = observe_copy(destination.data(), address, bytes,
            true, 0xA1234567U, "readable owned pages support exact bounded spans");
        if (copied) {
            expect(std::memcmp(destination.data(), allocation, bytes) == 0,
                "successful snapshots contain every exact source byte");
        }
    }
    DWORD prior{};
    const bool readOnly = VirtualProtect(allocation, pageSize, PAGE_READONLY,
        &prior) != FALSE;
    expect(readOnly, "test marks its first owned source page read-only");
    if (readOnly) {
        const bool copied = observe_copy(destination.data(), address, 0x920U,
            true, 0U, "read-only source can be copied without requesting write access");
        if (copied) expect(std::memcmp(destination.data(), allocation, 0x920U) == 0,
            "read-only exact player-state-sized snapshot matches source");
    }
    DWORD secondPrior{};
    const bool noAccess = VirtualProtect(allocation + pageSize, pageSize,
        PAGE_NOACCESS, &secondPrior) != FALSE;
    expect(noAccess, "test protects only its own second source page");
    if (noAccess) {
        observe_copy(destination.data(), address + pageSize, 16U, false,
            0xB1234567U, "NOACCESS source returns false without an unguarded load");
        destination.fill(0xA5U);
        observe_copy(destination.data(), address + pageSize - 8U, 16U, false,
            0xC1234567U, "cross-page partial source cannot become a complete snapshot");
        // No parsing/assertion of partially copied destination bytes is allowed.
        observe_copy(destination.data(), address + pageSize - 8U, 8U, true,
            0xD1234567U, "exact readable boundary before NOACCESS remains usable");
    }
}

}  // namespace

int main() {
    test_validation_without_native_reads();
    test_real_owned_pages();
    if (failures == 0) {
        std::cout << "PASS: " << cases
                  << " bounded self-memory copy cases; exact0x920/0x1C8 spans, "
                     "NOACCESS/partial/invalid/overflow rejection and caller "
                     "GetLastError preservation; no external process accessed\n";
    }
    return failures == 0 ? 0 : 1;
}
