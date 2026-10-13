#include "mounted_gun_logic.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace wawvr::mod {
namespace {

constexpr std::size_t kGentityNumberOffset = 0x00;
constexpr std::size_t kGentityTypeOffset = 0x04;
constexpr std::size_t kGentityGunAnglesOffset = 0x54;
constexpr std::size_t kGentityCurrentAnglesOffset = 0x16C;
constexpr std::size_t kGentityClientPointerOffset = 0x180;
constexpr std::size_t kGentityTurretPointerOffset = 0x190;
constexpr std::size_t kGentitySnapshotSpan = 0x194;

constexpr std::size_t kPlayerStateEFlagsOffset = 0xCC;
constexpr std::size_t kPlayerStateClientNumberOffset = 0xF8;
constexpr std::size_t kPlayerStateClampBaseOffset = 0x144;
constexpr std::size_t kPlayerStateClampRangeOffset = 0x14C;
constexpr std::size_t kPlayerStateViewLockOffset = 0x838;
constexpr std::size_t kPlayerStateViewLockEntityOffset = 0x83C;
constexpr std::size_t kPlayerStateSnapshotSpan = 0x840;

constexpr std::size_t kTurretArcMinimumOffset = 0x34;
constexpr std::size_t kTurretArcMaximumOffset = 0x3C;
constexpr std::size_t kTurretSnapshotSpan = 0x44;

constexpr std::int32_t kMountedEntityType = 0x0B;
constexpr std::int32_t kLocalClientNumber = 0;
constexpr std::int32_t kMountedEFlags = 0x300;

template <typename T>
[[nodiscard]] bool read_value(
    const std::span<const std::byte> bytes, const std::size_t offset,
    T* const output) noexcept {
    if (output == nullptr || offset > bytes.size() ||
        sizeof(T) > bytes.size() - offset) {
        return false;
    }
    std::memcpy(output, bytes.data() + offset, sizeof(T));
    return true;
}

[[nodiscard]] bool finite_angles(const MountedGunAngles& value) noexcept {
    return std::isfinite(value.pitch) && std::isfinite(value.yaw);
}

[[nodiscard]] bool valid_symmetric_ranges(
    const MountedGunAngles& value) noexcept {
    return finite_angles(value) && value.pitch >= 0.0F &&
        value.pitch <= 180.0F && value.yaw >= 0.0F &&
        value.yaw <= 180.0F;
}

[[nodiscard]] bool valid_arcs(
    const MountedGunAngles& minimum,
    const MountedGunAngles& maximum) noexcept {
    return finite_angles(minimum) && finite_angles(maximum) &&
        minimum.pitch <= maximum.pitch && minimum.yaw <= maximum.yaw &&
        minimum.pitch >= -180.0F && maximum.pitch <= 180.0F &&
        minimum.yaw >= -180.0F && maximum.yaw <= 180.0F;
}

}  // namespace

bool mounted_direction_to_world_angles(
    const float forward_x, const float forward_y, const float forward_z,
    MountedGunAngles* const output) noexcept {
    if (output == nullptr || !std::isfinite(forward_x) ||
        !std::isfinite(forward_y) || !std::isfinite(forward_z)) {
        return false;
    }
    const float length_squared = forward_x * forward_x +
        forward_y * forward_y + forward_z * forward_z;
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8F) {
        return false;
    }
    constexpr float kRadiansToDegrees = 57.29577951308232F;
    const float horizontal = std::hypot(forward_x, forward_y);
    const MountedGunAngles candidate{
        .pitch = std::atan2(-forward_z, horizontal) * kRadiansToDegrees,
        .yaw = std::atan2(forward_y, forward_x) * kRadiansToDegrees,
    };
    if (!finite_angles(candidate)) {
        return false;
    }
    *output = candidate;
    return true;
}

float mounted_angle_delta(const float angle, const float base) noexcept {
    if (!std::isfinite(angle) || !std::isfinite(base)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return std::remainder(angle - base, 360.0F);
}

bool clamp_mounted_world_angles(
    const MountedGunAngles& world, const MountedGunAngles& base,
    const MountedGunAngles& range, MountedGunAngles* const output) noexcept {
    if (output == nullptr || !finite_angles(world) || !finite_angles(base) ||
        !valid_symmetric_ranges(range)) {
        return false;
    }
    const float pitch_delta = mounted_angle_delta(world.pitch, base.pitch);
    const float yaw_delta = mounted_angle_delta(world.yaw, base.yaw);
    if (!std::isfinite(pitch_delta) || !std::isfinite(yaw_delta)) {
        return false;
    }
    *output = {
        .pitch = base.pitch + std::clamp(
            pitch_delta, -range.pitch, range.pitch),
        .yaw = base.yaw + std::clamp(yaw_delta, -range.yaw, range.yaw),
    };
    return finite_angles(*output);
}

bool clamp_mounted_relative_arcs(
    const MountedGunAngles& world, const MountedGunAngles& base,
    const MountedGunAngles& arc_min, const MountedGunAngles& arc_max,
    MountedGunAngles* const relative_output) noexcept {
    if (relative_output == nullptr || !finite_angles(world) ||
        !finite_angles(base) || !valid_arcs(arc_min, arc_max)) {
        return false;
    }
    const float pitch_delta = mounted_angle_delta(world.pitch, base.pitch);
    const float yaw_delta = mounted_angle_delta(world.yaw, base.yaw);
    if (!std::isfinite(pitch_delta) || !std::isfinite(yaw_delta)) {
        return false;
    }
    *relative_output = {
        .pitch = std::clamp(pitch_delta, arc_min.pitch, arc_max.pitch),
        .yaw = std::clamp(yaw_delta, arc_min.yaw, arc_max.yaw),
    };
    return true;
}

bool mounted_world_to_pose_local(
    const MountedGunAngles& world, const MountedGunAngles& pose,
    MountedGunAngles* const local_output) noexcept {
    if (local_output == nullptr || !finite_angles(world) ||
        !finite_angles(pose)) {
        return false;
    }
    *local_output = {
        .pitch = mounted_angle_delta(world.pitch, pose.pitch),
        .yaw = mounted_angle_delta(world.yaw, pose.yaw),
    };
    return finite_angles(*local_output);
}

bool mounted_gun_aim_publication_is_current(
    const MountedGunAimPublication& publication,
    const std::uint64_t now_milliseconds,
    const std::uint64_t maximum_age_milliseconds) noexcept {
    return publication.generation != 0 &&
        finite_angles(publication.world) &&
        now_milliseconds >= publication.publication_milliseconds &&
        now_milliseconds - publication.publication_milliseconds <=
            maximum_age_milliseconds;
}

MountedGunContextStatus decode_sp_mounted_gun_context(
    const SpMountedGunMemorySnapshot& snapshot,
    MountedGunContext* const output) noexcept {
    if (output == nullptr ||
        snapshot.local_gentity.size() < kGentitySnapshotSpan ||
        snapshot.player_state.size() < kPlayerStateSnapshotSpan ||
        snapshot.mounted_gentity.size() < kGentitySnapshotSpan ||
        snapshot.turret_info.size() < kTurretSnapshotSpan) {
        return MountedGunContextStatus::snapshot_too_small;
    }
    if (snapshot.scoped_or_vehicle_excluded) {
        return MountedGunContextStatus::excluded;
    }

    std::int32_t local_entity_number = -1;
    std::uint32_t client_pointer = 0;
    std::int32_t client_number = -1;
    if (!read_value(snapshot.local_gentity, kGentityNumberOffset,
                    &local_entity_number) ||
        !read_value(snapshot.local_gentity, kGentityClientPointerOffset,
                    &client_pointer) ||
        !read_value(snapshot.player_state, kPlayerStateClientNumberOffset,
                    &client_number) ||
        local_entity_number != kLocalClientNumber || client_pointer == 0 ||
        client_number != kLocalClientNumber) {
        return MountedGunContextStatus::invalid_local_player;
    }

    std::int32_t e_flags = 0;
    std::int32_t view_lock = 0;
    std::int32_t view_locked_entity = -1;
    if (!read_value(snapshot.player_state, kPlayerStateEFlagsOffset, &e_flags) ||
        !read_value(snapshot.player_state, kPlayerStateViewLockOffset,
                    &view_lock) ||
        !read_value(snapshot.player_state, kPlayerStateViewLockEntityOffset,
                    &view_locked_entity) ||
        (e_flags & kMountedEFlags) == 0 || view_lock == 0 ||
        snapshot.mounted_entity_index < 0 ||
        snapshot.mounted_entity_index > kSpMaximumEntityNumber ||
        view_locked_entity != snapshot.mounted_entity_index) {
        return MountedGunContextStatus::not_mounted;
    }

    std::int32_t mounted_number = -1;
    std::int32_t mounted_type = -1;
    std::uint32_t turret_pointer = 0;
    if (!read_value(snapshot.mounted_gentity, kGentityNumberOffset,
                    &mounted_number) ||
        !read_value(snapshot.mounted_gentity, kGentityTypeOffset,
                    &mounted_type) ||
        !read_value(snapshot.mounted_gentity, kGentityTurretPointerOffset,
                    &turret_pointer) ||
        mounted_number != snapshot.mounted_entity_index ||
        mounted_type != kMountedEntityType) {
        return MountedGunContextStatus::invalid_mounted_entity;
    }
    if (turret_pointer == 0) {
        return MountedGunContextStatus::invalid_turret;
    }

    MountedGunContext candidate{.entity_number = mounted_number};
    if (!read_value(snapshot.mounted_gentity, kGentityCurrentAnglesOffset,
                    &candidate.base_angles) ||
        !read_value(snapshot.mounted_gentity, kGentityGunAnglesOffset,
                    &candidate.replicated_angles) ||
        !read_value(snapshot.player_state, kPlayerStateClampBaseOffset,
                    &candidate.clamp_base) ||
        !read_value(snapshot.player_state, kPlayerStateClampRangeOffset,
                    &candidate.clamp_range) ||
        !read_value(snapshot.turret_info, kTurretArcMinimumOffset,
                    &candidate.arc_min) ||
        !read_value(snapshot.turret_info, kTurretArcMaximumOffset,
                    &candidate.arc_max) ||
        !finite_angles(candidate.base_angles) ||
        !finite_angles(candidate.replicated_angles) ||
        !finite_angles(candidate.clamp_base) ||
        !valid_symmetric_ranges(candidate.clamp_range) ||
        !valid_arcs(candidate.arc_min, candidate.arc_max)) {
        return MountedGunContextStatus::invalid_angles;
    }

    *output = candidate;
    return MountedGunContextStatus::valid;
}

MountedGunRoute select_mounted_gun_route(
    const MountedGunRouteInput& input) noexcept {
    const bool controller_route = input.mounted_context_valid &&
        !input.scoped_or_vehicle_excluded && input.controller_pose_valid &&
        input.controller_pose_current;
    return {
        .use_controller_aim = controller_route,
        // Mounted firing belongs to the live trigger even though the ordinary
        // handheld weapon may be intentionally ungripped/suppressed.
        .add_attack = controller_route && input.trigger_held,
        .native_fallback = !controller_route,
    };
}

}  // namespace wawvr::mod
