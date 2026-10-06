#include "mounted_gun_logic.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool near(
    const float left, const float right,
    const float tolerance = 1.0e-3F) noexcept {
    return std::abs(left - right) <= tolerance;
}

template <typename T, std::size_t Size>
void write(std::array<std::byte, Size>* const bytes,
           const std::size_t offset, const T& value) {
    std::memcpy(bytes->data() + offset, &value, sizeof(value));
}

struct Snapshots {
    std::array<std::byte, 0x194> local{};
    std::array<std::byte, 0x840> player{};
    std::array<std::byte, 0x194> mounted{};
    std::array<std::byte, 0x44> turret{};

    Snapshots() {
        const std::int32_t zero = 0;
        const std::uint32_t pointer = 0x12345678U;
        const std::int32_t e_flags = 0x300;
        const std::int32_t view_lock = 1;
        const std::int32_t mounted_number = 7;
        const std::int32_t mounted_type = 0x0B;
        const wawvr::mod::MountedGunAngles base{10.0F, 350.0F};
        const wawvr::mod::MountedGunAngles gun{2.0F, -3.0F};
        const wawvr::mod::MountedGunAngles range{25.0F, 40.0F};
        const wawvr::mod::MountedGunAngles arc_min{-20.0F, -35.0F};
        const wawvr::mod::MountedGunAngles arc_max{25.0F, 45.0F};
        write(&local, 0x00, zero);
        write(&local, 0x180, pointer);
        write(&player, 0xCC, e_flags);
        write(&player, 0xF8, zero);
        write(&player, 0x144, base);
        write(&player, 0x14C, range);
        write(&player, 0x838, view_lock);
        write(&player, 0x83C, mounted_number);
        write(&mounted, 0x00, mounted_number);
        write(&mounted, 0x04, mounted_type);
        write(&mounted, 0x54, gun);
        write(&mounted, 0x16C, base);
        write(&mounted, 0x190, pointer);
        write(&turret, 0x34, arc_min);
        write(&turret, 0x3C, arc_max);
    }

    [[nodiscard]] wawvr::mod::SpMountedGunMemorySnapshot snapshot(
        const bool excluded = false) const {
        return {
            .local_gentity = local,
            .player_state = player,
            .mounted_gentity = mounted,
            .turret_info = turret,
            .mounted_entity_index = 7,
            .scoped_or_vehicle_excluded = excluded,
        };
    }
};

void test_math() {
    using namespace wawvr::mod;
    MountedGunAngles angles{};
    expect(mounted_direction_to_world_angles(1.0F, 0.0F, 0.0F, &angles) &&
               near(angles.pitch, 0.0F) && near(angles.yaw, 0.0F),
           "forward ray converts to zero pitch/yaw");
    expect(mounted_direction_to_world_angles(0.0F, 1.0F, 0.0F, &angles) &&
               near(angles.yaw, 90.0F),
           "left axis converts to positive yaw");
    expect(mounted_direction_to_world_angles(1.0F, 0.0F, -1.0F, &angles) &&
               near(angles.pitch, 45.0F),
           "upward ray converts to positive CoD pitch");
    expect(!mounted_direction_to_world_angles(0.0F, 0.0F, 0.0F, &angles),
           "zero ray fails closed");
    expect(!mounted_direction_to_world_angles(
               std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F,
               &angles),
           "non-finite ray fails closed");

    expect(near(mounted_angle_delta(5.0F, 355.0F), 10.0F) &&
               near(mounted_angle_delta(355.0F, 5.0F), -10.0F),
           "angle delta wraps across zero");

    expect(clamp_mounted_world_angles(
               {80.0F, 20.0F}, {10.0F, 350.0F}, {25.0F, 40.0F},
               &angles) &&
               near(angles.pitch, 35.0F) && near(angles.yaw, 380.0F),
           "symmetric world clamp honors pitch and wrapped yaw stops");
    expect(!clamp_mounted_world_angles(
               {}, {}, {std::numeric_limits<float>::quiet_NaN(), 10.0F},
               &angles),
           "NaN clamp range fails closed");

    expect(clamp_mounted_relative_arcs(
               {-50.0F, 80.0F}, {0.0F, 10.0F}, {-20.0F, -30.0F},
               {25.0F, 40.0F}, &angles) &&
               near(angles.pitch, -20.0F) && near(angles.yaw, 40.0F),
           "asymmetric native turret arcs clamp relative output");
    expect(!clamp_mounted_relative_arcs(
               {}, {}, {1.0F, -1.0F}, {-1.0F, 1.0F}, &angles),
           "reversed mechanical arc fails closed");

    expect(mounted_world_to_pose_local(
               {5.0F, 355.0F}, {355.0F, 5.0F}, &angles) &&
               near(angles.pitch, 10.0F) && near(angles.yaw, -10.0F),
           "world angles convert to wrapped pose-local angles");

    const MountedGunAimPublication fresh{{12.0F, 34.0F}, 7, 1'000};
    expect(mounted_gun_aim_publication_is_current(fresh, 1'200, 250),
           "fresh input-boundary mounted aim publication is accepted");
    expect(!mounted_gun_aim_publication_is_current(fresh, 1'251, 250),
           "stale mounted aim publication fails closed");
    expect(!mounted_gun_aim_publication_is_current(fresh, 999, 250),
           "future-dated mounted aim publication fails closed");
    auto invalid_publication = fresh;
    invalid_publication.world.pitch =
        std::numeric_limits<float>::quiet_NaN();
    expect(!mounted_gun_aim_publication_is_current(
               invalid_publication, 1'100, 250),
           "non-finite mounted aim publication fails closed");
    invalid_publication = fresh;
    invalid_publication.generation = 0;
    expect(!mounted_gun_aim_publication_is_current(
               invalid_publication, 1'100, 250),
           "unpublished mounted aim generation fails closed");
}

void test_context_decoder() {
    using namespace wawvr::mod;
    Snapshots memory{};
    MountedGunContext context{};
    expect(decode_sp_mounted_gun_context(memory.snapshot(), &context) ==
               MountedGunContextStatus::valid &&
               context.entity_number == 7 &&
               near(context.clamp_range.pitch, 25.0F) &&
               near(context.arc_max.yaw, 45.0F),
           "known SP snapshot offsets decode a mounted MG context");
    expect(decode_sp_mounted_gun_context(memory.snapshot(true), &context) ==
               MountedGunContextStatus::excluded,
           "scoped or vehicle route is explicitly excluded");

    auto short_snapshot = memory.snapshot();
    short_snapshot.player_state = short_snapshot.player_state.first(0x100);
    expect(decode_sp_mounted_gun_context(short_snapshot, &context) ==
               MountedGunContextStatus::snapshot_too_small,
           "short copied snapshot fails without reading past it");

    Snapshots bad = memory;
    const std::uint32_t null_pointer = 0;
    write(&bad.local, 0x180, null_pointer);
    expect(decode_sp_mounted_gun_context(bad.snapshot(), &context) ==
               MountedGunContextStatus::invalid_local_player,
           "null local gclient pointer fails closed");

    bad = memory;
    const std::int32_t no_flags = 0;
    write(&bad.player, 0xCC, no_flags);
    expect(decode_sp_mounted_gun_context(bad.snapshot(), &context) ==
               MountedGunContextStatus::not_mounted,
           "non-mounted player state retains native route");

    bad = memory;
    const std::int32_t entitynum_none = 2175;
    write(&bad.player, 0x83C, entitynum_none);
    auto none_snapshot = bad.snapshot();
    none_snapshot.mounted_entity_index = entitynum_none;
    expect(decode_sp_mounted_gun_context(none_snapshot, &context) ==
               MountedGunContextStatus::not_mounted,
           "SP ENTITYNUM_NONE cannot enter the mounted route");

    Snapshots maximum = memory;
    write(&maximum.player, 0x83C, kSpMaximumEntityNumber);
    write(&maximum.mounted, 0x00, kSpMaximumEntityNumber);
    auto maximum_snapshot = maximum.snapshot();
    maximum_snapshot.mounted_entity_index = kSpMaximumEntityNumber;
    expect(decode_sp_mounted_gun_context(maximum_snapshot, &context) ==
               MountedGunContextStatus::valid &&
               context.entity_number == kSpMaximumEntityNumber,
           "SP mounted entity 2174 is accepted at the valid boundary");

    bad = memory;
    const std::int32_t wrong_type = 2;
    write(&bad.mounted, 0x04, wrong_type);
    expect(decode_sp_mounted_gun_context(bad.snapshot(), &context) ==
               MountedGunContextStatus::invalid_mounted_entity,
           "wrong entity type cannot enter MG route");

    bad = memory;
    write(&bad.mounted, 0x190, null_pointer);
    expect(decode_sp_mounted_gun_context(bad.snapshot(), &context) ==
               MountedGunContextStatus::invalid_turret,
           "missing turret object fails closed");

    bad = memory;
    const MountedGunAngles invalid_range{
        std::numeric_limits<float>::infinity(), 20.0F};
    write(&bad.player, 0x14C, invalid_range);
    expect(decode_sp_mounted_gun_context(bad.snapshot(), &context) ==
               MountedGunContextStatus::invalid_angles,
           "non-finite replicated clamp fails closed");
}

void test_route_policy() {
    using namespace wawvr::mod;
    MountedGunRouteInput input{
        .mounted_context_valid = true,
        .controller_pose_valid = true,
        .controller_pose_current = true,
        .trigger_held = true,
        .handheld_grip_held = false,
    };
    auto route = select_mounted_gun_route(input);
    expect(route.use_controller_aim && route.add_attack &&
               !route.native_fallback,
           "mounted trigger fires without requiring handheld weapon grip");

    input.controller_pose_current = false;
    route = select_mounted_gun_route(input);
    expect(!route.use_controller_aim && !route.add_attack &&
               route.native_fallback,
           "stale controller snapshot falls back atomically to native");

    input.controller_pose_current = true;
    input.scoped_or_vehicle_excluded = true;
    route = select_mounted_gun_route(input);
    expect(!route.use_controller_aim && !route.add_attack &&
               route.native_fallback,
           "scoped or vehicle path remains native");
}

}  // namespace

int main() {
    test_math();
    test_context_decoder();
    test_route_policy();
    if (failures != 0) {
        std::cerr << failures << " mounted-gun logic test(s) failed\n";
        return 1;
    }
    std::cout << "Mounted-gun logic tests passed\n";
    return 0;
}
