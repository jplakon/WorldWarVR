// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace wawvr::mod {

using ResidentRegionQuery = decltype(&VirtualQuery);
using ResidentWorkingSetQuery = decltype(&QueryWorkingSetEx);

// This is an actual checked page slice, not a fabricated MEMORY_BASIC_INFORMATION.
// The facts are valid only for the caller's immediate synchronous traversal; do
// not retain them across native callbacks, protection changes, frames, or maps.
struct ResidentPageSlice final {
    std::uintptr_t begin{};
    std::size_t size{};
};

namespace resident_page_detail {

[[nodiscard]] inline std::size_t page_size() noexcept {
    static const auto size = []() noexcept {
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        return static_cast<std::size_t>(system.dwPageSize);
    }();
    return size;
}

[[nodiscard]] constexpr bool protection_allows(const DWORD protection,
                                              const bool writable) noexcept {
    if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    switch (protection & 0xffU) {
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    case PAGE_READONLY:
    case PAGE_EXECUTE_READ:
        return !writable;
    default:
        return false;
    }
}

[[nodiscard]] inline bool check(
    const void* const address, const std::size_t size, const bool writable,
    ResidentPageSlice* const output, const ResidentRegionQuery fallback,
    const ResidentWorkingSetQuery working) noexcept {
    if (output != nullptr) *output = {};
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    constexpr auto maximum = (std::numeric_limits<std::uintptr_t>::max)();
    if (begin == 0 || size == 0 || size > maximum - begin) return false;

    const auto page = page_size();
    const auto page_begin = page != 0 ? begin - begin % page : 0;
    const bool page_end_valid = page != 0 && page <= maximum - page_begin;
    const auto page_end = page_end_valid ? page_begin + page : 0;
    // Deliberately keep the first optimization to one page. A cross-page range
    // must still satisfy the original one-contiguous-VirtualQuery-region rule.
    if (working != nullptr && page_end_valid && size <= page_end - begin) {
        PSAPI_WORKING_SET_EX_INFORMATION information{};
        information.VirtualAddress = const_cast<void*>(address);
        if (working(GetCurrentProcess(), &information, sizeof(information)) &&
            information.VirtualAttributes.Valid != 0 &&
            information.VirtualAttributes.Bad == 0 &&
            protection_allows(static_cast<DWORD>(
                information.VirtualAttributes.Win32Protection), writable)) {
            if (output != nullptr) *output = {page_begin, page};
            return true;
        }
    }

    // Invalid/nonresident/unsupported/denied working-set results are not proof
    // of accessibility. Fall back to a fresh exact region/protection validation.
    MEMORY_BASIC_INFORMATION memory{};
    if (fallback == nullptr ||
        fallback(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        !protection_allows(memory.Protect, writable)) {
        return false;
    }
    const auto region_begin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    if (memory.RegionSize == 0 || memory.RegionSize > maximum - region_begin ||
        begin < region_begin || begin - region_begin >= memory.RegionSize ||
        size > memory.RegionSize - (begin - region_begin)) {
        return false;
    }
    if (output != nullptr) {
        if (!page_end_valid) return false;
        const auto checked_begin = (std::max)(page_begin, region_begin);
        const auto checked_end = (std::min)(page_end, region_begin +
            static_cast<std::uintptr_t>(memory.RegionSize));
        if (checked_end <= checked_begin) return false;
        *output = {checked_begin, checked_end - checked_begin};
    }
    return true;
}

} // namespace resident_page_detail

[[nodiscard]] inline bool resident_page_access(
    const void* const address, const std::size_t size, const bool writable,
    const ResidentRegionQuery fallback,
    const ResidentWorkingSetQuery working = &QueryWorkingSetEx) noexcept {
    return resident_page_detail::check(address, size, writable, nullptr,
                                       fallback, working);
}

[[nodiscard]] inline bool resident_page_slice(
    const void* const address, const bool writable, ResidentPageSlice* const output,
    const ResidentRegionQuery fallback,
    const ResidentWorkingSetQuery working = &QueryWorkingSetEx) noexcept {
    if (output == nullptr) return false;
    return resident_page_detail::check(address, 1, writable, output,
                                       fallback, working);
}

} // namespace wawvr::mod
