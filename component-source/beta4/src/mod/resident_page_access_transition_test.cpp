// SPDX-License-Identifier: GPL-3.0-only
#include "resident_page_access.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
int failures = 0;
void expect(bool condition, const char* label) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}

bool reference_permission(DWORD protection, bool writable) {
    if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    switch (protection & 0xFFU) {
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY: return true;
    case PAGE_READONLY:
    case PAGE_EXECUTE_READ: return !writable;
    default: return false;
    }
}

void verify_access(void* address, const char* label, bool verbose = true) {
    MEMORY_BASIC_INFORMATION native{};
    const auto bytes = VirtualQuery(address, &native, sizeof(native));
    PSAPI_WORKING_SET_EX_INFORMATION resident{};
    resident.VirtualAddress = address;
    const bool queried = QueryWorkingSetEx(
        GetCurrentProcess(), &resident, sizeof(resident)) != FALSE;
    if (verbose) {
        std::cout << label << " state=" << std::hex << native.State
                  << " VQ=" << native.Protect << " QWSvalid="
                  << (queried ? resident.VirtualAttributes.Valid : 0)
                  << " QWSprotect="
                  << (queried ? resident.VirtualAttributes.Win32Protection : 0)
                  << std::dec << '\n';
    }
    for (bool writable : {false, true}) {
        const bool expected = bytes == sizeof(native) &&
            native.State == MEM_COMMIT &&
            reference_permission(native.Protect, writable);
        const bool actual = wawvr::mod::resident_page_access(
            address, 4, writable,
            &VirtualQuery);
        if (actual != expected) {
            std::cerr << label << " writable=" << writable
                      << " expected=" << expected << " actual=" << actual
                      << '\n';
        }
        expect(actual == expected,
               "fresh resident-page access agrees with native protection");
        wawvr::mod::ResidentPageSlice slice{123, 456};
        const bool sliced = wawvr::mod::resident_page_slice(
            address, writable, &slice, &VirtualQuery);
        expect(sliced == expected,
               "fresh page-slice permission agrees with native protection");
        if (sliced) {
            SYSTEM_INFO info{};
            GetSystemInfo(&info);
            const auto location = reinterpret_cast<std::uintptr_t>(address);
            const auto page = static_cast<std::size_t>(info.dwPageSize);
            const auto page_begin = location - location % page;
            expect(slice.begin >= page_begin && slice.begin <= location &&
                       slice.size != 0 && slice.size <= page &&
                       location - slice.begin < slice.size &&
                       slice.size <= page - (slice.begin - page_begin),
                   "a validated slice never grants access beyond this page");
        } else {
            expect(slice.begin == 0 && slice.size == 0,
                   "denied page slice clears previous output facts");
        }
    }
    MEMORY_BASIC_INFORMATION after{};
    expect(VirtualQuery(address, &after, sizeof(after)) == sizeof(after) &&
               after.Protect == native.Protect && after.State == native.State,
           "queries never consume guards or change page state");
}

void private_page_transitions(const std::size_t page) {
    auto* const allocation = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, page * 3, MEM_RESERVE, PAGE_NOACCESS));
    expect(allocation != nullptr, "reserve private transition fixture");
    if (!allocation) return;
    expect(VirtualAlloc(allocation, page * 2, MEM_COMMIT, PAGE_READWRITE) == allocation,
           "commit private transition fixture");
    allocation[0] = 1;
    allocation[page] = 2;
    verify_access(allocation, "private resident readwrite");
    verify_access(allocation + page * 2, "private reserved");
    struct Transition { DWORD protect; const char* label; };
    constexpr std::array<Transition, 7> states{{
        {PAGE_READONLY, "private readonly"},
        {PAGE_EXECUTE, "private execute-only"},
        {PAGE_EXECUTE_READ, "private execute-read"},
        {PAGE_EXECUTE_READWRITE, "private execute-readwrite"},
        {PAGE_READWRITE | PAGE_GUARD, "private guarded readwrite"},
        {PAGE_NOACCESS, "private noaccess"},
        {PAGE_READWRITE, "private readwrite restored"},
    }};
    DWORD previous{};
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
        for (const auto& state : states) {
            expect(VirtualProtect(allocation, page, PAGE_READWRITE, &previous) != 0,
                   "restore writable page before resident transition");
            allocation[0] = static_cast<std::uint8_t>(iteration);
            expect(VirtualProtect(allocation, page, state.protect, &previous) != 0,
                   "apply requested native page protection");
            verify_access(allocation, state.label, iteration == 0);
        }
    }
    expect(VirtualProtect(allocation + page, page, PAGE_NOACCESS, &previous) != 0,
           "protect second page of crossing range");
    expect(!wawvr::mod::resident_page_access(
               allocation + page - 2, 4,
               false, &VirtualQuery),
           "cross-page read cannot inherit first-page permission");
    expect(VirtualFree(allocation, page, MEM_DECOMMIT) != 0,
           "decommit previously resident page");
    verify_access(allocation, "private decommitted");
    expect(VirtualAlloc(allocation, page, MEM_COMMIT, PAGE_READWRITE) == allocation,
           "recommit page without touching it");
    verify_access(allocation, "private demand-zero not touched");
    allocation[0] = 42;
    verify_access(allocation, "private recommitted resident");
    expect(VirtualFree(allocation, 0, MEM_RELEASE) != 0,
           "release transition fixture");
    verify_access(allocation, "private released");
}

void mapped_page_transitions(const std::size_t page) {
    const HANDLE section = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        static_cast<DWORD>(page), nullptr);
    expect(section != nullptr, "create pagefile-backed section");
    if (!section) return;
    void* const shared = MapViewOfFile(section, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, page);
    void* const copy = MapViewOfFile(section, FILE_MAP_COPY, 0, 0, page);
    expect(shared != nullptr && copy != nullptr, "map shared and copy-on-write views");
    if (shared && copy) {
        *static_cast<volatile std::uint8_t*>(shared) = 23;
        const volatile std::uint8_t observed = *static_cast<volatile std::uint8_t*>(copy);
        static_cast<void>(observed);
        verify_access(shared, "mapped shared readwrite");
        verify_access(copy, "mapped shared writecopy");
        *static_cast<volatile std::uint8_t*>(copy) = 47;
        verify_access(copy, "mapped privatized writecopy");
        DWORD previous{};
        expect(VirtualProtect(copy, page, PAGE_READONLY, &previous) != 0,
               "protect privatized mapping readonly");
        verify_access(copy, "mapped privatized readonly");
    }
    if (copy) UnmapViewOfFile(copy);
    if (shared) UnmapViewOfFile(shared);
    CloseHandle(section);
}
}  // namespace

int main() {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto page = static_cast<std::size_t>(info.dwPageSize);
    private_page_transitions(page);
    mapped_page_transitions(page);
    expect(!wawvr::mod::resident_page_access(nullptr, 4, false, &VirtualQuery) &&
               !wawvr::mod::resident_page_access(reinterpret_cast<void*>(1), 0, false, &VirtualQuery) &&
               !wawvr::mod::resident_page_access(
                   reinterpret_cast<void*>((std::numeric_limits<std::uintptr_t>::max)() - 1), 4,
                   false, &VirtualQuery),
           "null, empty, and overflowing ranges fail closed");
    if (failures == 0) std::cout << "Resident page transition tests passed\n";
    return failures == 0 ? 0 : 1;
}
