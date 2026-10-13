// SPDX-License-Identifier: GPL-3.0-only
#include "resident_page_access.hpp"

#include <iostream>
#include <limits>

namespace {
unsigned working_calls{}, region_calls{};
unsigned failures{};
BOOL working_result{TRUE};
bool working_valid{true}, working_bad{};
DWORD working_protection{PAGE_READWRITE};
MEMORY_BASIC_INFORMATION region{};
SIZE_T region_result{sizeof(region)};

void expect(bool result, const char* description) {
    if (!result) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

BOOL WINAPI fake_working(HANDLE process, PVOID data, DWORD size) {
    ++working_calls;
    expect(process == GetCurrentProcess() &&
               size == sizeof(PSAPI_WORKING_SET_EX_INFORMATION),
           "working-set query is one entry in the current process");
    auto* info = static_cast<PSAPI_WORKING_SET_EX_INFORMATION*>(data);
    info->VirtualAttributes.Flags = 0;
    info->VirtualAttributes.Valid = working_valid ? 1u : 0u;
    info->VirtualAttributes.Bad = working_bad ? 1u : 0u;
    info->VirtualAttributes.Win32Protection = working_protection;
    return working_result;
}

SIZE_T WINAPI fake_region(LPCVOID, PMEMORY_BASIC_INFORMATION output, SIZE_T) {
    ++region_calls;
    *output = region;
    return region_result;
}

void reset(std::uintptr_t begin, std::size_t size) {
    working_calls = region_calls = 0;
    working_result = TRUE;
    working_valid = true;
    working_bad = false;
    working_protection = PAGE_READWRITE;
    region = {};
    region.BaseAddress = reinterpret_cast<void*>(begin);
    region.RegionSize = size;
    region.State = MEM_COMMIT;
    region.Protect = PAGE_READWRITE;
    region_result = sizeof(region);
}
}

int main() {
    using namespace wawvr::mod;
    const auto page = resident_page_detail::page_size();
    const std::uintptr_t base = page * 64;
    const auto at = [base](std::size_t offset) {
        return reinterpret_cast<const void*>(base + offset);
    };
    const auto check = [&](std::size_t offset, std::size_t size, bool writable) {
        return resident_page_access(at(offset), size, writable,
                                    &fake_region, &fake_working);
    };
    reset(base, page * 3);
    expect(check(8, 16, false) && check(8, 16, true) &&
               working_calls == 2 && region_calls == 0,
           "each resident readable/writable call is fresh without a region scan");
    ResidentPageSlice slice{};
    expect(resident_page_slice(at(8), false, &slice, &fake_region, &fake_working) &&
               slice.begin == base && slice.size == page && region_calls == 0,
           "a resident string slice is exactly the containing page");
    reset(base, page * 3);
    expect(check(page - 1, 2, true) && working_calls == 0 && region_calls == 1,
           "cross-page spans always retain the full original-region fallback");
    reset(base, page);
    expect(!check(page - 1, 2, false) && region_calls == 1,
           "cross-region spans remain rejected");

    for (int mode = 0; mode < 5; ++mode) {
        reset(base, page);
        if (mode == 0) working_result = FALSE;
        if (mode == 1) working_valid = false;
        if (mode == 2) working_bad = true;
        if (mode == 3) working_protection = PAGE_NOACCESS;
        if (mode == 4) working_protection = PAGE_READWRITE | PAGE_GUARD;
        expect(check(8, 4, false) && working_calls == 1 && region_calls == 1,
               "uncertain/nonresident/denied working-set results use the fresh region fallback");
    }
    for (const DWORD protection : {PAGE_READONLY, PAGE_EXECUTE_READ}) {
        reset(base, page);
        working_protection = protection;
        region.Protect = protection;
        expect(check(8, 4, false) && region_calls == 0,
               "read-only resident protection permits a read");
        expect(!check(8, 4, true) && region_calls == 1,
               "read-only protection never permits writes");
    }
    for (const DWORD protection : {PAGE_NOACCESS, PAGE_EXECUTE,
                                   PAGE_READWRITE | PAGE_GUARD}) {
        reset(base, page);
        working_protection = protection;
        region.Protect = protection;
        expect(!check(8, 4, false) && region_calls == 1,
               "non-readable or guarded protection is rejected by both paths");
    }
    for (const DWORD protection : {PAGE_WRITECOPY, PAGE_EXECUTE_WRITECOPY,
                                   PAGE_EXECUTE_READWRITE}) {
        reset(base, page);
        working_protection = protection;
        expect(check(8, 4, true) && region_calls == 0,
               "documented writable and copy-on-write protection is accepted");
    }

    reset(base, page);
    expect(resident_page_access(at(8), 4, false, &fake_region, nullptr) &&
               working_calls == 0 && region_calls == 1,
           "explicit null working query preserves injected fallback behavior");
    reset(base, page);
    working_valid = false;
    region.State = MEM_RESERVE;
    expect(!check(8, 4, false), "uncommitted fallback region is rejected");
    region.State = MEM_COMMIT;
    region_result = 0;
    expect(!check(8, 4, false), "failed region query is rejected");
    region_result = sizeof(region);
    region.RegionSize = (std::numeric_limits<std::uintptr_t>::max)();
    expect(!check(8, 4, false), "overflowing region end is rejected");
    reset(base, page);
    expect(!resident_page_access(nullptr, 4, false, &fake_region, &fake_working) &&
               !check(8, 0, false) &&
               !resident_page_access(reinterpret_cast<void*>(
                    (std::numeric_limits<std::uintptr_t>::max)() - 1),
                    8, false, &fake_region, &fake_working) &&
               working_calls == 0 && region_calls == 0,
           "invalid and overflowing requests issue no query");
    reset(base + 4, page - 8);
    working_valid = false;
    expect(resident_page_slice(at(8), false, &slice, &fake_region, &fake_working) &&
               slice.begin == base + 4 && slice.size == page - 8,
           "fallback slice never extends beyond the actual checked region");
    region.Protect = PAGE_NOACCESS;
    expect(!resident_page_slice(at(8), false, &slice, &fake_region, &fake_working) &&
               slice.begin == 0 && slice.size == 0,
           "failure clears previous page facts");
    std::cout << "Resident page access injected tests: " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
