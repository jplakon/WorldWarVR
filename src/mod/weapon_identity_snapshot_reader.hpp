// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "resident_page_access.hpp"
#include "virtual_query_timing.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace wawvr::mod {

// Stack-owned reader for one synchronous weapon-table/name snapshot. Do not
// retain it across engine callbacks, frames, or map loads. Native values are
// copied afresh. The default reader validates each access independently using
// fresh page facts. An explicitly injected VirtualQuery keeps the old bounded
// per-snapshot region path so custom validators cannot be silently bypassed.
class WeaponIdentitySnapshotReader final {
public:
    using Query = decltype(&VirtualQuery);

    WeaponIdentitySnapshotReader() noexcept
        : query_(&grouped_virtual_query<VirtualQueryTimingGroup::weapon_identity>),
          fresh_page_access_(true) {}
    explicit WeaponIdentitySnapshotReader(const Query query) noexcept
        : query_(query) {}
    WeaponIdentitySnapshotReader(const WeaponIdentitySnapshotReader&) = delete;
    WeaponIdentitySnapshotReader& operator=(const WeaponIdentitySnapshotReader&) = delete;

    [[nodiscard]] bool readable(const std::uintptr_t address,
                                const std::size_t size) noexcept {
        if (address == 0 || size == 0 ||
            size > (std::numeric_limits<std::uintptr_t>::max)() - address) {
            return false;
        }
        if (fresh_page_access_) {
            return resident_page_access(
                reinterpret_cast<const void*>(address), size, false, query_);
        }
        if (contains(address, size)) return true;
        return query_region(address) && contains(address, size);
    }

    template <typename T>
    [[nodiscard]] bool read(const std::uintptr_t address, T* value) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        if (value == nullptr || !readable(address, sizeof(T))) return false;
        std::memcpy(value, reinterpret_cast<const void*>(address), sizeof(T));
        return true;
    }

    template <std::size_t Capacity>
    [[nodiscard]] bool copy_c_string(
        const std::uintptr_t address,
        std::array<char, Capacity>* const output) noexcept {
        if (address == 0 || output == nullptr || Capacity < 2) return false;
        output->fill('\0');
        std::size_t copied = 0;
        while (copied < Capacity) {
            if (copied > (std::numeric_limits<std::uintptr_t>::max)() - address) {
                return false;
            }
            const auto cursor = address + copied;
            std::size_t available{};
            if (fresh_page_access_) {
                ResidentPageSlice slice{};
                if (!resident_page_slice(reinterpret_cast<const void*>(cursor),
                                         false, &slice, query_) ||
                    cursor < slice.begin || cursor - slice.begin >= slice.size) {
                    return false;
                }
                available = slice.size - (cursor - slice.begin);
            } else {
                if (!readable(cursor, 1)) return false;
                available = region_size_ - (cursor - region_begin_);
            }
            const auto chunk = (std::min)(Capacity - copied, available);
            std::memcpy(output->data() + copied,
                        reinterpret_cast<const void*>(cursor), chunk);
            if (std::memchr(output->data() + copied, '\0', chunk) != nullptr) {
                return (*output)[0] != '\0';
            }
            copied += chunk;
        }
        return false;
    }

private:
    [[nodiscard]] bool contains(const std::uintptr_t address,
                                const std::size_t size) const noexcept {
        return region_size_ != 0 && address >= region_begin_ &&
            address - region_begin_ < region_size_ &&
            size <= region_size_ - (address - region_begin_);
    }

    [[nodiscard]] bool query_region(const std::uintptr_t address) noexcept {
        region_size_ = 0;
        MEMORY_BASIC_INFORMATION memory{};
        if (query_ == nullptr ||
            query_(reinterpret_cast<const void*>(address), &memory,
                   sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }
        const auto protection = memory.Protect & 0xffU;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
            protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
            protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY) {
            return false;
        }
        const auto begin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if (memory.RegionSize == 0 ||
            memory.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - begin ||
            address < begin || address - begin >= memory.RegionSize) {
            return false;
        }
        region_begin_ = begin;
        region_size_ = memory.RegionSize;
        return true;
    }

    Query query_{};
    bool fresh_page_access_{};
    std::uintptr_t region_begin_{};
    std::size_t region_size_{};
};

}  // namespace wawvr::mod
