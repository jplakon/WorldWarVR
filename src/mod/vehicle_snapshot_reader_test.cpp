// SPDX-License-Identifier: GPL-3.0-only
#include "vehicle_snapshot_reader.hpp"
#include "weapon_identity_snapshot_reader.hpp"
#include "bounded_bone_matrices.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
int failures = 0;
unsigned queries = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

SIZE_T WINAPI counted_query(const void* address,
                           MEMORY_BASIC_INFORMATION* memory, SIZE_T size) {
    ++queries;
    return VirtualQuery(address, memory, size);
}

struct Allocation {
    void* address{};
    ~Allocation() {
        if (address != nullptr) VirtualFree(address, 0, MEM_RELEASE);
    }
};

void test_bounded_action_matrices() {
    using wawvr::mod::copy_controlled_action_matrices;
    struct Matrix { std::array<float, 8> values{}; };
    std::array<Matrix, 3> matrices{};
    matrices[0].values[0] = 1.0F;
    matrices[1].values[0] = 2.0F;
    matrices[2].values[0] = 3.0F;
    const std::span<const Matrix> input{matrices};
    Matrix root{}, parent{}, action{};
    queries = 0;
    expect(copy_controlled_action_matrices(
               input, 0, 1, 2, &root, &parent, &action) &&
               root.values[0] == 1.0F && parent.values[0] == 2.0F &&
               action.values[0] == 3.0F && queries == 0,
           "private or freshly validated matrices use pure bounded copies");
    matrices[1].values[0] = 9.0F;
    expect(copy_controlled_action_matrices(
               input, 0, 1, 2, &root, &parent, &action) &&
               parent.values[0] == 9.0F,
           "the same bounded span always samples current matrix values");
    const auto reject_without_output_change = [&](const std::size_t r,
                                                  const std::size_t p,
                                                  const std::size_t b) {
        root.values.fill(11.0F);
        parent.values.fill(12.0F);
        action.values.fill(13.0F);
        return !copy_controlled_action_matrices(
                   input, r, p, b, &root, &parent, &action) &&
            root.values[0] == 11.0F && parent.values[0] == 12.0F &&
            action.values[0] == 13.0F;
    };
    expect(reject_without_output_change(3, 1, 2) &&
               reject_without_output_change(0, 3, 2) &&
               reject_without_output_change(0, 1, 3) &&
               reject_without_output_change(0xFE, 1, 2) &&
               reject_without_output_change(0, 0xFF, 2),
           "every bone index is checked before any output is written");
    expect(!copy_controlled_action_matrices(
               std::span<const Matrix>{}, 0, 0, 0, &root, &parent, &action) &&
               !copy_controlled_action_matrices(
                   input, 0, 1, 2, static_cast<Matrix*>(nullptr),
                   &parent, &action),
           "empty matrix spans and missing outputs fail closed");
    expect(copy_controlled_action_matrices(
               input, 0, 1, 2, &matrices[1], &parent, &action) &&
               parent.values[0] == 9.0F && matrices[1].values[0] == 1.0F,
           "aliasing an output never changes a later sampled input");
}

void test_weapon_identity_snapshot_reader(const std::size_t page) {
    using wawvr::mod::WeaponIdentitySnapshotReader;
    Allocation allocation{VirtualAlloc(nullptr, page * 2,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)};
    expect(allocation.address != nullptr, "allocate weapon snapshot fixture");
    if (allocation.address == nullptr) return;
    const auto base = reinterpret_cast<std::uintptr_t>(allocation.address);
    const std::uint32_t count = 4;
    std::memcpy(allocation.address, &count, sizeof(count));
    constexpr char weapon_name[] = "m1garand";
    std::memcpy(reinterpret_cast<void*>(base + 128), weapon_name,
                sizeof(weapon_name));
    {
        WeaponIdentitySnapshotReader memory(&counted_query);
        queries = 0;
        std::uint32_t value{};
        std::array<char, 64> name{};
        expect(memory.read(base, &value) && value == count &&
                   memory.readable(base + 32, 32) &&
                   memory.copy_c_string(base + 128, &name) &&
                   std::string_view{name.data()} == weapon_name && queries == 1,
               "weapon table and name in one region share one synchronous query");
        constexpr char renamed[] = "kar98k";
        std::memcpy(reinterpret_cast<void*>(base + 128), renamed,
                    sizeof(renamed));
        expect(memory.copy_c_string(base + 128, &name) &&
                   std::string_view{name.data()} == renamed && queries == 1,
               "weapon names are copied afresh, never cached");
        std::memset(reinterpret_cast<void*>(base + 256), 'x', 64);
        expect(!memory.copy_c_string(base + 256, &name),
               "unterminated weapon names fail at the output capacity");
        *reinterpret_cast<char*>(base + 256 + 63) = '\0';
        expect(memory.copy_c_string(base + 256, &name) && name[63] == '\0',
               "a terminator in the final output byte is accepted");
        *reinterpret_cast<char*>(base + 256) = '\0';
        expect(!memory.copy_c_string(base + 256, &name),
               "empty weapon names remain rejected");
        expect(!memory.readable(0, 4) && !memory.readable(base, 0) &&
                   !memory.readable((std::numeric_limits<std::uintptr_t>::max)() - 1, 4),
               "weapon reads reject null, zero-size, and overflowing spans");
    }
    constexpr char boundary_name[] = "ptrs41";
    std::memcpy(reinterpret_cast<void*>(base + page - 3), boundary_name,
                sizeof(boundary_name));
    DWORD old_protection{};
    expect(VirtualProtect(reinterpret_cast<void*>(base + page), page,
                          PAGE_READONLY, &old_protection) != 0,
           "split weapon name across different readable regions");
    {
        WeaponIdentitySnapshotReader memory(&counted_query);
        queries = 0;
        std::array<char, 64> name{};
        expect(memory.copy_c_string(base + page - 3, &name) &&
                   std::string_view{name.data()} == boundary_name && queries == 2,
               "a boundary-spanning weapon name validates each readable region");
        expect(!memory.readable(base + page - 3, 8),
               "ordinary weapon structure reads remain single-region bounded");
    }
    expect(VirtualProtect(reinterpret_cast<void*>(base + page), page,
                          PAGE_NOACCESS, &old_protection) != 0,
           "remove access before a fresh weapon snapshot");
    {
        WeaponIdentitySnapshotReader memory(&counted_query);
        queries = 0;
        std::array<char, 64> name{};
        expect(!memory.copy_c_string(base + page - 3, &name) && queries == 2,
               "fresh weapon snapshot rejects an unreadable continuation");
    }
    expect(VirtualProtect(reinterpret_cast<void*>(base + page), page,
                          PAGE_READWRITE | PAGE_GUARD, &old_protection) != 0,
           "guard the weapon name continuation");
    {
        WeaponIdentitySnapshotReader memory(&counted_query);
        std::array<char, 64> name{};
        expect(!memory.copy_c_string(base + page - 3, &name),
               "weapon name copy never touches a guarded continuation");
        WeaponIdentitySnapshotReader failed(nullptr);
        expect(!failed.readable(base, 4), "weapon region query failure is closed");
    }
}

void test_default_readers_refresh_access(const std::size_t page) {
    using wawvr::mod::VehicleSnapshotReader;
    using wawvr::mod::WeaponIdentitySnapshotReader;
    Allocation allocation{VirtualAlloc(nullptr, page * 2,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)};
    expect(allocation.address != nullptr, "allocate fresh-page reader fixture");
    if (allocation.address == nullptr) return;
    const auto base = reinterpret_cast<std::uintptr_t>(allocation.address);
    const std::uint32_t expected_value = 47;
    std::memcpy(allocation.address, &expected_value, sizeof(expected_value));
    constexpr char boundary_name[] = "springfield";
    std::memcpy(reinterpret_cast<void*>(base + page - 3), boundary_name,
                sizeof(boundary_name));
    WeaponIdentitySnapshotReader weapon;
    VehicleSnapshotReader vehicle;
    std::uint32_t value{};
    std::array<char, 64> copied_name{};
    expect(weapon.read(base, &value) && value == expected_value &&
               vehicle.read(base, &value) && value == expected_value,
           "default readers sample live resident values");
    expect(weapon.copy_c_string(base + page - 3, &copied_name) &&
               std::string_view{copied_name.data()} == boundary_name,
           "default name copy validates a fresh page slice for each page");

    DWORD old_protection{};
    expect(VirtualProtect(reinterpret_cast<void*>(base + page), page,
                          PAGE_READONLY, &old_protection) != 0,
           "split default reader fixture into distinct readable regions");
    expect(!weapon.readable(base + page - 2, 4) &&
               !vehicle.readable(base + page - 2, 4),
           "default structure reads preserve cross-region rejection");
    expect(weapon.copy_c_string(base + page - 3, &copied_name) &&
               std::string_view{copied_name.data()} == boundary_name,
           "default string copy permits separately validated readable regions");

    expect(VirtualProtect(allocation.address, page, PAGE_NOACCESS,
                          &old_protection) != 0,
           "remove access within the same default reader lifetime");
    expect(!weapon.readable(base, sizeof(value)) &&
               !vehicle.readable(base, sizeof(value)),
           "default readers do not retain stale readable page facts");
    expect(VirtualProtect(allocation.address, page,
                          PAGE_READWRITE | PAGE_GUARD, &old_protection) != 0,
           "guard a previously resident default-reader page");
    expect(!weapon.readable(base, sizeof(value)) &&
               !vehicle.readable(base, sizeof(value)) &&
               !weapon.copy_c_string(base, &copied_name),
           "default scalar and string readers reject a newly guarded page");
    MEMORY_BASIC_INFORMATION guarded{};
    expect(VirtualQuery(allocation.address, &guarded, sizeof(guarded)) ==
               sizeof(guarded) && (guarded.Protect & PAGE_GUARD) != 0,
           "default validation never consumes the guard by touching the page");

    expect(VirtualProtect(allocation.address, page, PAGE_READWRITE,
                          &old_protection) != 0,
           "restore default-reader page access");
    expect(weapon.read(base, &value) && value == expected_value &&
               vehicle.read(base, &value) && value == expected_value,
           "the same default readers recover using fresh access facts");
    expect(VirtualProtect(reinterpret_cast<void*>(base + page), page,
                          PAGE_NOACCESS, &old_protection) != 0,
           "remove access from a default name continuation");
    expect(!weapon.copy_c_string(base + page - 3, &copied_name),
           "default name copy rejects an unreadable continuation without a cached span");
    expect(VirtualFree(reinterpret_cast<void*>(base + page), page,
                       MEM_DECOMMIT) != 0,
           "decommit the default-reader continuation page");
    expect(!weapon.readable(base + page, 4) &&
               !vehicle.readable(base + page, 4),
           "default readers reject a page after decommit");
}
}

int main() {
    using wawvr::mod::VehicleSnapshotReader;
    test_bounded_action_matrices();
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto page = static_cast<std::size_t>(system.dwPageSize);
    test_weapon_identity_snapshot_reader(page);
    test_default_readers_refresh_access(page);
    Allocation allocation{VirtualAlloc(nullptr, page * 2,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)};
    if (allocation.address == nullptr) return 2;
    const auto base = reinterpret_cast<std::uintptr_t>(allocation.address);
    const std::int32_t initial = 42;
    std::memcpy(allocation.address, &initial, sizeof(initial));
    {
        VehicleSnapshotReader memory(&counted_query);
        std::int32_t value{};
        queries = 0;
        expect(memory.read(base, &value) && value == 42,
               "first scalar read validates its committed readable region");
        expect(memory.readable(base + 0xCC, sizeof(value)) &&
                   memory.readable(base + 0x83C, sizeof(value)) &&
                   memory.readable(base + 0x840, sizeof(value)) &&
                   memory.readable(base + 0x844, sizeof(value)),
               "all aircraft and tank PS fields fit the same queried region");
        expect(queries == 1, "neighboring PS reads issue only one query");
        const std::int32_t changed = 73;
        std::memcpy(allocation.address, &changed, sizeof(changed));
        expect(memory.read(base, &value) && value == changed,
               "region reuse never caches native field values");
        expect(queries == 1, "fresh values do not require another region query");
        expect(!memory.read(base, static_cast<int*>(nullptr)),
               "null output pointer is rejected");
        expect(!memory.readable(0, 4) && !memory.readable(base, 0) &&
                   !memory.readable(std::numeric_limits<std::uintptr_t>::max() - 2, 4),
               "null, zero-size, and overflowing ranges are rejected");
    }
    DWORD old_protection{};
    expect(VirtualProtect(reinterpret_cast<void*>(base + page), page,
                          PAGE_READONLY, &old_protection) != 0,
           "set up distinct readable region");
    {
        VehicleSnapshotReader memory(&counted_query);
        queries = 0;
        expect(memory.readable(base, 4) && memory.readable(base + page, 4),
               "second region is validated independently");
        expect(queries == 2, "different protection region requires new query");
        expect(!memory.readable(base + page - 2, 4),
               "a read spanning different regions retains strict bounds");
    }
    expect(VirtualProtect(allocation.address, page, PAGE_NOACCESS,
                          &old_protection) != 0,
           "set up unreadable next-snapshot region");
    {
        VehicleSnapshotReader memory(&counted_query);
        queries = 0;
        expect(!memory.readable(base, 4) && queries == 1,
               "new snapshot cannot inherit old readable protection facts");
    }
    expect(VirtualProtect(allocation.address, page, PAGE_READWRITE | PAGE_GUARD,
                          &old_protection) != 0,
           "set up guarded region");
    {
        VehicleSnapshotReader memory(&counted_query);
        expect(!memory.readable(base, 4), "guarded memory is rejected without reading it");
    }
    {
        VehicleSnapshotReader memory(nullptr);
        expect(!memory.readable(base + page, 4), "failed query is fail-closed");
    }
    if (failures == 0) std::cout << "Vehicle snapshot reader tests passed\n";
    return failures == 0 ? 0 : 1;
}
