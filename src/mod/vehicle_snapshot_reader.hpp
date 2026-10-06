// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "resident_page_access.hpp"
#include "virtual_query_timing.hpp"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace wawvr::mod {

// One synchronous traversal of the native vehicle context only. Never retain
// this object across an engine callback, frame, map transition, or protection
// change. The default reader validates each access using fresh page facts.
// An explicitly injected VirtualQuery retains the bounded per-snapshot region
// path; production page queries cannot silently bypass that custom validator.
class VehicleSnapshotReader final {
public:
    using Query = decltype(&VirtualQuery);

    VehicleSnapshotReader() noexcept
        : query_(&grouped_virtual_query<VirtualQueryTimingGroup::vehicle_input>),
          fresh_page_access_(true) {}
    explicit VehicleSnapshotReader(const Query query) noexcept
        : query_(query) {}
    VehicleSnapshotReader(const VehicleSnapshotReader&) = delete;
    VehicleSnapshotReader& operator=(const VehicleSnapshotReader&) = delete;

    [[nodiscard]] bool readable(const std::uintptr_t address,
                                const std::size_t size) noexcept {
        if (address < 0x10000 || size == 0 ||
            address > std::numeric_limits<std::uintptr_t>::max() - size) {
            return false;
        }
        if (fresh_page_access_) {
            return resident_page_access(
                reinterpret_cast<const void*>(address), size, false, query_);
        }
        if (contains(address, size)) return true;

        // A failed or differently protected query cannot leave usable facts
        // from the previous region behind.
        region_size_ = 0;
        MEMORY_BASIC_INFORMATION memory{};
        if (query_ == nullptr ||
            query_(reinterpret_cast<const void*>(address), &memory,
                   sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }
        const DWORD access = memory.Protect & 0xffU;
        if (access != PAGE_READONLY && access != PAGE_READWRITE &&
            access != PAGE_WRITECOPY && access != PAGE_EXECUTE_READ &&
            access != PAGE_EXECUTE_READWRITE &&
            access != PAGE_EXECUTE_WRITECOPY) {
            return false;
        }
        region_begin_ = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        region_size_ = memory.RegionSize;
        return contains(address, size);
    }

    template <typename T>
    [[nodiscard]] bool read(const std::uintptr_t address, T* value) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        if (value == nullptr || !readable(address, sizeof(T))) return false;
        std::memcpy(value, reinterpret_cast<const void*>(address), sizeof(T));
        return true;
    }

private:
    [[nodiscard]] bool contains(const std::uintptr_t address,
                                const std::size_t size) const noexcept {
        return region_size_ != 0 && address >= region_begin_ &&
               address - region_begin_ <= region_size_ &&
               size <= region_size_ - (address - region_begin_);
    }

    Query query_{};
    bool fresh_page_access_{};
    std::uintptr_t region_begin_{};
    std::size_t region_size_{};
};

}  // namespace wawvr::mod
