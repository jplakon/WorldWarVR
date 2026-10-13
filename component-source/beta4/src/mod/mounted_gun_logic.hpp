#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace wawvr::mod {

// T4 SP uses MAX_GENTITIES=2176 and reserves 2175 as ENTITYNUM_NONE.
inline constexpr std::int32_t kSpMaximumEntityNumber = 2174;

struct MountedGunAngles {
    float pitch{};
    float yaw{};
};

// One controller-world ray captured at the verified post-CL_CreateCmd input
// boundary. Mounted simulation and rendering consume this exact publication
// instead of rebuilding the ray from a refdef that stereo rendering may have
// temporarily replaced with an eye/HMD basis.
struct MountedGunAimPublication {
    MountedGunAngles world{};
    std::uint64_t generation{};
    std::uint64_t publication_milliseconds{};
};

struct MountedGunContext {
    std::int32_t entity_number{-1};
    MountedGunAngles base_angles{};
    MountedGunAngles replicated_angles{};
    MountedGunAngles clamp_base{};
    MountedGunAngles clamp_range{};
    MountedGunAngles arc_min{};
    MountedGunAngles arc_max{};
};

// Copied memory ranges supplied by a future, fingerprinted runtime bridge.
// This logic never follows a process pointer or reads live game memory.
struct SpMountedGunMemorySnapshot {
    std::span<const std::byte> local_gentity{};
    std::span<const std::byte> player_state{};
    std::span<const std::byte> mounted_gentity{};
    std::span<const std::byte> turret_info{};
    std::int32_t mounted_entity_index{-1};
    bool scoped_or_vehicle_excluded{};
};

enum class MountedGunContextStatus {
    valid,
    excluded,
    snapshot_too_small,
    invalid_local_player,
    not_mounted,
    invalid_mounted_entity,
    invalid_turret,
    invalid_angles,
};

struct MountedGunRouteInput {
    bool mounted_context_valid{};
    bool scoped_or_vehicle_excluded{};
    bool controller_pose_valid{};
    bool controller_pose_current{};
    bool trigger_held{};
    bool handheld_grip_held{};
};

struct MountedGunRoute {
    bool use_controller_aim{};
    bool add_attack{};
    bool native_fallback{true};
};

[[nodiscard]] bool mounted_direction_to_world_angles(
    float forward_x, float forward_y, float forward_z,
    MountedGunAngles* output) noexcept;

[[nodiscard]] float mounted_angle_delta(float angle, float base) noexcept;

[[nodiscard]] bool clamp_mounted_world_angles(
    const MountedGunAngles& world, const MountedGunAngles& base,
    const MountedGunAngles& range, MountedGunAngles* output) noexcept;

[[nodiscard]] bool clamp_mounted_relative_arcs(
    const MountedGunAngles& world, const MountedGunAngles& base,
    const MountedGunAngles& arc_min, const MountedGunAngles& arc_max,
    MountedGunAngles* relative_output) noexcept;

[[nodiscard]] bool mounted_world_to_pose_local(
    const MountedGunAngles& world, const MountedGunAngles& pose,
    MountedGunAngles* local_output) noexcept;

[[nodiscard]] bool mounted_gun_aim_publication_is_current(
    const MountedGunAimPublication& publication,
    std::uint64_t now_milliseconds,
    std::uint64_t maximum_age_milliseconds) noexcept;

[[nodiscard]] MountedGunContextStatus decode_sp_mounted_gun_context(
    const SpMountedGunMemorySnapshot& snapshot,
    MountedGunContext* output) noexcept;

[[nodiscard]] MountedGunRoute select_mounted_gun_route(
    const MountedGunRouteInput& input) noexcept;

}  // namespace wawvr::mod
