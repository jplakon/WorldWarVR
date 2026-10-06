// SPDX-License-Identifier: GPL-3.0-only
// WorldAtWarVR controller implementation. Comparative research history is
// implemented against the validated T4 input contract.
#include "input_mapping.hpp"

#include "weapon_grip_logic.hpp"
#include "weapon_placement.hpp"

#include "camera_comfort_logic.hpp"

#include "xr_math.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <type_traits>

namespace wawvr::mod {
namespace {

std::atomic<MountedGunRouteProbe> g_mounted_gun_route_probe{nullptr};

constexpr float kRadiansToDegrees =
    180.0F / 3.14159265358979323846F;

[[nodiscard]] bool finite(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] float dot(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] bool finite_vector(const wawvr::xr::Vec3f& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

void update_melee_grip_latch(
    const wawvr::xr::FloatActionState& squeeze,
    bool* const latched) noexcept {
    if (latched == nullptr) {
        return;
    }
    if (!squeeze.active || !finite(squeeze.current)) {
        *latched = false;
        return;
    }
    *latched = *latched
        ? squeeze.current >= kWeaponGripRelease
        : squeeze.current >= kWeaponGripEngage;
}

[[nodiscard]] bool valid_orientation(
    const wawvr::xr::Quaternionf& value) noexcept {
    if (!finite(value.x) || !finite(value.y) || !finite(value.z) ||
        !finite(value.w)) {
        return false;
    }
    const float length_squared = value.x * value.x + value.y * value.y +
                                 value.z * value.z + value.w * value.w;
    return length_squared >= 0.90F && length_squared <= 1.10F;
}

[[nodiscard]] bool valid_basis(const wawvr::xr::Basis3f& axis) noexcept {
    if (!finite_vector(axis.forward) || !finite_vector(axis.left) ||
        !finite_vector(axis.up)) {
        return false;
    }

    const float forward_length = dot(axis.forward, axis.forward);
    const float left_length = dot(axis.left, axis.left);
    const float up_length = dot(axis.up, axis.up);
    constexpr float kMinimumLengthSquared = 0.80F;
    constexpr float kMaximumLengthSquared = 1.20F;
    constexpr float kMaximumAxisDot = 0.20F;
    return forward_length >= kMinimumLengthSquared &&
           forward_length <= kMaximumLengthSquared &&
           left_length >= kMinimumLengthSquared &&
           left_length <= kMaximumLengthSquared &&
           up_length >= kMinimumLengthSquared &&
           up_length <= kMaximumLengthSquared &&
           std::abs(dot(axis.forward, axis.left)) <= kMaximumAxisDot &&
           std::abs(dot(axis.forward, axis.up)) <= kMaximumAxisDot &&
           std::abs(dot(axis.left, axis.up)) <= kMaximumAxisDot;
}

[[nodiscard]] wawvr::xr::Vec3f compose_direction(
    const wawvr::xr::Basis3f& body,
    const wawvr::xr::Vec3f& local) noexcept {
    return {
        local.x * body.forward.x + local.y * body.left.x +
            local.z * body.up.x,
        local.x * body.forward.y + local.y * body.left.y +
            local.z * body.up.y,
        local.x * body.forward.z + local.y * body.left.z +
            local.z * body.up.z,
    };
}

[[nodiscard]] bool bool_held(
    const wawvr::xr::BoolActionState& action) noexcept {
    return action.active && action.current;
}

[[nodiscard]] bool float_held(
    const wawvr::xr::FloatActionState& action) noexcept {
    return action.active && finite(action.current) &&
           action.current >= kControllerButtonThreshold;
}

template <typename Command>
void add_if(
    Command& command,
    const bool held,
    const wawvr::t4::UsercmdButton button) noexcept {
    if (held) {
        wawvr::t4::add_button(command, button);
    }
}

[[nodiscard]] bool remap_stick(
    const wawvr::xr::Vec2ActionState& stick,
    float* x,
    float* y) noexcept {
    if (x == nullptr || y == nullptr || !stick.active ||
        !finite(stick.current.x) || !finite(stick.current.y)) {
        return false;
    }

    const float raw_x = std::clamp(stick.current.x, -1.0F, 1.0F);
    const float raw_y = std::clamp(stick.current.y, -1.0F, 1.0F);
    const float magnitude = std::sqrt(raw_x * raw_x + raw_y * raw_y);
    if (!finite(magnitude) || magnitude <= kControllerStickDeadzone) {
        *x = 0.0F;
        *y = 0.0F;
        return false;
    }

    const float clamped_magnitude = std::min(magnitude, 1.0F);
    const float remapped_magnitude =
        (clamped_magnitude - kControllerStickDeadzone) /
        (1.0F - kControllerStickDeadzone);
    const float scale = remapped_magnitude / magnitude;
    *x = raw_x * scale;
    *y = raw_y * scale;
    return true;
}

[[nodiscard]] std::int8_t saturating_movement_add(
    const std::int8_t native_value,
    const float vr_value) noexcept {
    if (!finite(vr_value)) {
        return native_value;
    }
    const int delta = static_cast<int>(std::lround(
        std::clamp(vr_value, -1.0F, 1.0F) * 127.0F));
    const int combined = std::clamp(
        static_cast<int>(native_value) + delta, -127, 127);
    return static_cast<std::int8_t>(combined);
}

}  // namespace

bool controller_frame_is_current(
    const ControllerFrameSnapshot& snapshot,
    const std::uint64_t now_milliseconds) noexcept {
    if (snapshot.frame.frame_id == 0 || snapshot.frame.actions.sequence == 0 ||
        !snapshot.frame.actions.focused ||
        now_milliseconds < snapshot.publication_milliseconds) {
        return false;
    }
    return now_milliseconds - snapshot.publication_milliseconds <=
           kMaximumControllerFrameAgeMilliseconds;
}

bool controller_aim_degrees(
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Basis3f& camera_axis,
    float* const pitch_degrees,
    float* const yaw_degrees) noexcept {
    if (pitch_degrees == nullptr || yaw_degrees == nullptr ||
        !valid_basis(camera_axis) ||
        !valid_orientation(snapshot.tracking_anchor.orientation)) {
        return false;
    }

    const auto& aim = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Right)].aim;
    if (!aim.active || !aim.orientation_valid ||
        !valid_orientation(aim.pose.orientation)) {
        return false;
    }

    // Position is intentionally zeroed: the first MVP uses T4's native muzzle
    // origin and needs only the controller aim orientation.
    wawvr::xr::Posef controller_orientation{};
    controller_orientation.orientation = aim.pose.orientation;
    wawvr::xr::Posef anchor_orientation{};
    anchor_orientation.orientation = snapshot.tracking_anchor.orientation;
    // The exact T4 global refdef is stock here: the stereo scene thunk applies
    // HMD orientation only to temporary eye views, then restores this global.
    // The controller must therefore be relative to the same frozen anchor as
    // those eyes. Current-head-relative math would incorrectly cancel HMD yaw.
    const wawvr::xr::EnginePose relative =
        wawvr::xr::OpenXrPoseToIwRelative(
            controller_orientation, anchor_orientation, 1.0F);
    if (!valid_basis(relative.axis)) {
        return false;
    }

    wawvr::xr::Basis3f body_axis{};
    if (!gravity_level_t4_camera_axis(camera_axis, &body_axis)) {
        return false;
    }
    wawvr::xr::Vec3f world =
        compose_direction(body_axis, relative.axis.forward);
    const float length_squared = dot(world, world);
    if (!finite_vector(world) || !finite(length_squared) ||
        length_squared <= 1.0e-8F) {
        return false;
    }
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    world.x *= inverse_length;
    world.y *= inverse_length;
    world.z *= inverse_length;

    const float horizontal = std::sqrt(
        world.x * world.x + world.y * world.y);
    const float pitch = -std::atan2(world.z, horizontal) * kRadiansToDegrees;
    const float yaw = std::atan2(world.y, world.x) * kRadiansToDegrees;
    if (!finite(pitch) || !finite(yaw)) {
        return false;
    }

    *pitch_degrees = pitch;
    *yaw_degrees = yaw;
    return true;
}

bool controller_body_yaw_delta_degrees(
    const ControllerFrameSnapshot& snapshot,
    float* const yaw_degrees) noexcept {
    if (yaw_degrees == nullptr || !snapshot.frame.views_valid ||
        !valid_orientation(snapshot.frame.head_center.orientation) ||
        !valid_orientation(snapshot.tracking_anchor.orientation)) {
        return false;
    }

    wawvr::xr::Posef head_orientation{};
    head_orientation.orientation = snapshot.frame.head_center.orientation;
    wawvr::xr::Posef anchor_orientation{};
    anchor_orientation.orientation = snapshot.tracking_anchor.orientation;
    const wawvr::xr::EnginePose relative =
        wawvr::xr::OpenXrPoseToIwRelative(
            head_orientation, anchor_orientation, 1.0F);
    if (!valid_basis(relative.axis)) {
        return false;
    }

    const float horizontal_squared =
        relative.axis.forward.x * relative.axis.forward.x +
        relative.axis.forward.y * relative.axis.forward.y;
    if (!finite(horizontal_squared) || horizontal_squared <= 0.01F) {
        return false;
    }
    const float yaw = std::atan2(
        relative.axis.forward.y, relative.axis.forward.x) *
        kRadiansToDegrees;
    if (!finite(yaw)) {
        return false;
    }
    *yaw_degrees = yaw;
    return true;
}

bool automatic_body_yaw_sync_allowed(
    const bool gameplay_controller_allowed,
    const bool controller_frame_current,
    const bool sprint_movement_active) noexcept {
    return gameplay_controller_allowed && controller_frame_current &&
        sprint_movement_active;
}

float consume_snap_turn_degrees(
    const wawvr::xr::Vec2ActionState& right_stick,
    SnapTurnState* const state) noexcept {
    if (state == nullptr) {
        return 0.0F;
    }
    if (!right_stick.active || !finite(right_stick.current.x) ||
        !finite(right_stick.current.y)) {
        state->armed = true;
        return 0.0F;
    }

    const float x = std::clamp(right_stick.current.x, -1.0F, 1.0F);
    const float y = std::clamp(right_stick.current.y, -1.0F, 1.0F);
    const float absolute_x = std::abs(x);
    if (absolute_x < kSnapTurnReleaseThreshold) {
        state->armed = true;
        return 0.0F;
    }
    if (!state->armed ||
        absolute_x < std::abs(y) + kSnapTurnVerticalDominanceMargin) {
        return 0.0F;
    }
    if (x >= kSnapTurnEngageThreshold) {
        state->armed = false;
        return -kSnapTurnDegrees;
    }
    if (x <= -kSnapTurnEngageThreshold) {
        state->armed = false;
        return kSnapTurnDegrees;
    }
    return 0.0F;
}

void reset_snap_turn(SnapTurnState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

TurnMode turn_mode_from_setting(const std::wstring_view value) noexcept {
    return value == L"smooth" ? TurnMode::Smooth : TurnMode::Snap;
}

float consume_smooth_turn_degrees(
    const bool input_owned,
    const std::uint64_t action_sequence,
    const std::uint64_t now_milliseconds,
    const wawvr::xr::Vec2ActionState& right_stick,
    SmoothTurnState* const state) noexcept {
    if (state == nullptr) {
        return 0.0F;
    }

    const bool input_valid = input_owned && action_sequence != 0 &&
        now_milliseconds != 0 &&
        right_stick.active && finite(right_stick.current.x) &&
        finite(right_stick.current.y);
    if (!input_valid) {
        reset_smooth_turn(state);
        return 0.0F;
    }

    if (!state->input_was_owned ||
        now_milliseconds <= state->previous_update_milliseconds) {
        state->input_was_owned = true;
        state->previous_update_milliseconds = now_milliseconds;
        state->last_action_sequence = action_sequence;
        return 0.0F;
    }

    if (action_sequence == state->last_action_sequence) {
        return 0.0F;
    }
    state->last_action_sequence = action_sequence;

    const std::uint64_t elapsed_milliseconds = std::min(
        now_milliseconds - state->previous_update_milliseconds,
        kSmoothTurnMaximumElapsedMilliseconds);
    state->previous_update_milliseconds = now_milliseconds;

    const float x = std::clamp(right_stick.current.x, -1.0F, 1.0F);
    const float y = std::clamp(right_stick.current.y, -1.0F, 1.0F);
    const float absolute_x = std::abs(x);
    if (absolute_x <= kSmoothTurnDeadzone ||
        absolute_x < std::abs(y) + kSnapTurnVerticalDominanceMargin) {
        return 0.0F;
    }

    const float normalized_magnitude = std::min(
        1.0F,
        (absolute_x - kSmoothTurnDeadzone) /
            (1.0F - kSmoothTurnDeadzone));
    const float direction = x > 0.0F ? -1.0F : 1.0F;
    return direction * normalized_magnitude *
        kSmoothTurnDegreesPerSecond *
        (static_cast<float>(elapsed_milliseconds) / 1000.0F);
}

void reset_smooth_turn(SmoothTurnState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

DirectionalActionUpdate update_directional_actions(
    const bool input_owned,
    const std::uint64_t action_sequence,
    const wawvr::xr::Vec2ActionState& right_stick,
    DirectionalActionState* const state) noexcept {
    DirectionalActionUpdate update{};
    if (state == nullptr) {
        return update;
    }

    const bool valid = input_owned && action_sequence != 0 &&
        right_stick.active && finite(right_stick.current.x) &&
        finite(right_stick.current.y);
    if (!valid) {
        state->input_was_owned = false;
        state->armed = false;
        state->last_action_sequence = action_sequence;
        return update;
    }

    const float x = std::clamp(right_stick.current.x, -1.0F, 1.0F);
    const float y = std::clamp(right_stick.current.y, -1.0F, 1.0F);
    const bool neutral =
        std::abs(x) <= kDirectionalActionReleaseThreshold &&
        std::abs(y) <= kDirectionalActionReleaseThreshold;

    if (!state->input_was_owned) {
        state->input_was_owned = true;
        state->armed = neutral;
        state->last_action_sequence = action_sequence;
        return update;
    }
    if (action_sequence == state->last_action_sequence) {
        return update;
    }
    state->last_action_sequence = action_sequence;

    if (neutral) {
        state->armed = true;
        return update;
    }
    if (!state->armed ||
        std::abs(y) < kDirectionalActionEngageThreshold ||
        std::abs(y) <
            std::abs(x) + kDirectionalActionDominanceMargin) {
        return update;
    }

    state->armed = false;
    if (y < 0.0F) {
        if (state->stance == DirectionalActionState::Stance::standing) {
            state->stance = DirectionalActionState::Stance::crouched;
            update.action = DirectionalAction::crouch;
        } else if (state->stance ==
                   DirectionalActionState::Stance::crouched) {
            state->stance = DirectionalActionState::Stance::prone;
            update.action = DirectionalAction::prone;
        }
    } else if (state->stance == DirectionalActionState::Stance::prone) {
        state->stance = DirectionalActionState::Stance::crouched;
        update.action = DirectionalAction::crouch;
    } else if (state->stance ==
               DirectionalActionState::Stance::crouched) {
        state->stance = DirectionalActionState::Stance::standing;
        update.action = DirectionalAction::stand;
    } else {
        update.action = DirectionalAction::jump;
    }
    return update;
}

void reset_directional_actions(DirectionalActionState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

std::string_view directional_action_console_command(
    const DirectionalAction action) noexcept {
    switch (action) {
    case DirectionalAction::crouch: return "gocrouch\n";
    case DirectionalAction::prone: return "goprone\n";
    case DirectionalAction::stand:
    case DirectionalAction::jump:
        return "+gostand\n-gostand\n";
    case DirectionalAction::none:
        break;
    }
    return {};
}

bool update_sprint_latch(
    const bool input_owned,
    const std::uint64_t action_sequence,
    const wawvr::xr::Vec2ActionState& movement_stick,
    const wawvr::xr::BoolActionState& stick_click,
    SprintLatchState* const state) noexcept {
    if (state == nullptr) {
        return false;
    }

    const bool click_held = stick_click.active && stick_click.current;
    const bool actions_valid =
        action_sequence != 0 && movement_stick.active &&
        stick_click.active && finite(movement_stick.current.x) &&
        finite(movement_stick.current.y);
    if (!input_owned || !actions_valid) {
        state->input_was_owned = false;
        state->click_was_held = click_held;
        state->latched = false;
        state->last_action_sequence = action_sequence;
        return false;
    }

    const float movement_magnitude_squared =
        movement_stick.current.x * movement_stick.current.x +
        movement_stick.current.y * movement_stick.current.y;
    const bool locomotion_held =
        movement_magnitude_squared >
        kControllerStickDeadzone * kControllerStickDeadzone;

    // On the first command after gameplay regains ownership, baseline the
    // physical click. This prevents an L3 held through a menu transition from
    // creating a synthetic sprint edge.
    if (!state->input_was_owned) {
        state->input_was_owned = true;
        state->click_was_held = click_held;
        state->latched = false;
        state->last_action_sequence = action_sequence;
        return false;
    }

    // Several native usercmds may consume one published OpenXR action frame.
    // Update the physical edge only once for each action sequence.
    if (action_sequence != state->last_action_sequence) {
        if (click_held && !state->click_was_held && locomotion_held) {
            state->latched = true;
        }
        state->click_was_held = click_held;
        state->last_action_sequence = action_sequence;
    }
    if (!locomotion_held) {
        state->latched = false;
    }
    return state->latched;
}

void reset_sprint_latch(SprintLatchState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

PhysicalMeleeGesture update_physical_melee_gesture(
    const bool input_owned,
    const ControllerFrameSnapshot& snapshot,
    const std::uint64_t now_milliseconds,
    PhysicalMeleeState* const state,
    const bool supported_weapon_owned,
    const std::uint64_t single_hand_pistol_identity) noexcept {
    if (state == nullptr) {
        return PhysicalMeleeGesture::none;
    }

    const auto& right = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Right)];
    const auto& left = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
    const bool prior_grip_was_latched =
        state->right_grip_latched || state->left_grip_latched;
    update_melee_grip_latch(right.squeeze, &state->right_grip_latched);
    update_melee_grip_latch(left.squeeze, &state->left_grip_latched);
    const bool two_hand_weapon_held = supported_weapon_owned &&
        state->right_grip_latched && state->left_grip_latched;
    const bool readable_right_grip =
        right.squeeze.active && finite(right.squeeze.current) &&
        right.squeeze.current >= kWeaponGripRelease;
    const bool readable_left_grip =
        left.squeeze.active && finite(left.squeeze.current) &&
        left.squeeze.current >= kWeaponGripRelease;
    const bool right_hand_pistol_held = single_hand_pistol_identity != 0 &&
        state->right_grip_latched && !state->left_grip_latched &&
        left.squeeze.active && finite(left.squeeze.current) &&
        !readable_left_grip && !two_hand_weapon_held;
    const std::uint64_t held_pistol_identity = right_hand_pistol_held
        ? single_hand_pistol_identity : 0;
    const bool one_hand_weapon_transition = !two_hand_weapon_held &&
        !right_hand_pistol_held &&
        (prior_grip_was_latched || state->right_grip_latched ||
         state->left_grip_latched || readable_right_grip ||
         readable_left_grip);
    const bool pose_valid = input_owned && snapshot.frame.views_valid &&
        snapshot.frame.actions.focused &&
        snapshot.frame.actions.sequence != 0 && right.grip.active &&
        right.grip.position_valid &&
        finite_vector(right.grip.pose.position) &&
        finite_vector(snapshot.frame.head_center.position) &&
        !one_hand_weapon_transition &&
        (!right_hand_pistol_held ||
         (right.aim.active && right.aim.orientation_valid &&
          valid_orientation(right.aim.pose.orientation))) &&
        (!two_hand_weapon_held ||
         (left.grip.active && left.grip.position_valid &&
          finite_vector(left.grip.pose.position)));
    if (!pose_valid ||
        now_milliseconds < snapshot.publication_milliseconds) {
        state->input_was_owned = false;
        state->pose_was_valid = false;
        state->held_pistol_sample_count = 0;
        state->held_pistol_next_sample = 0;
        state->last_action_sequence = snapshot.frame.actions.sequence;
        return PhysicalMeleeGesture::none;
    }

    if (snapshot.frame.actions.sequence == state->last_action_sequence) {
        return PhysicalMeleeGesture::none;
    }
    state->last_action_sequence = snapshot.frame.actions.sequence;

    const wawvr::xr::Vec3f relative{
        right.grip.pose.position.x - snapshot.frame.head_center.position.x,
        right.grip.pose.position.y - snapshot.frame.head_center.position.y,
        right.grip.pose.position.z - snapshot.frame.head_center.position.z,
    };
    const wawvr::xr::Vec3f support_relative{
        left.grip.pose.position.x - snapshot.frame.head_center.position.x,
        left.grip.pose.position.y - snapshot.frame.head_center.position.y,
        left.grip.pose.position.z - snapshot.frame.head_center.position.z,
    };
    const std::uint64_t sample_milliseconds =
        snapshot.publication_milliseconds;

    if (two_hand_weapon_held) {
        const wawvr::xr::Vec3f pair{
            support_relative.x - relative.x,
            support_relative.y - relative.y,
            support_relative.z - relative.z,
        };
        const float pair_separation_squared = dot(pair, pair);
        constexpr float kMinimumPairSeparationMeters =
            kCod4TwoHandMinimumSeparationIwUnits /
            wawvr::xr::kIwUnitsPerMeter;
        constexpr float kMaximumPairSeparationMeters =
            kCod4TwoHandMaximumSeparationIwUnits /
            wawvr::xr::kIwUnitsPerMeter;
        if (!finite(pair_separation_squared) ||
            pair_separation_squared <
                kMinimumPairSeparationMeters * kMinimumPairSeparationMeters ||
            pair_separation_squared >
                kMaximumPairSeparationMeters * kMaximumPairSeparationMeters) {
            state->input_was_owned = false;
            state->pose_was_valid = false;
            return PhysicalMeleeGesture::none;
        }
    }

    const auto append_pistol_sample = [&]() noexcept {
        const auto capacity = static_cast<std::uint32_t>(
            state->held_pistol_samples.size());
        state->held_pistol_samples[state->held_pistol_next_sample] = {
            relative, right.grip.pose.position, sample_milliseconds};
        state->held_pistol_next_sample =
            (state->held_pistol_next_sample + 1) % capacity;
        state->held_pistol_sample_count = std::min(
            state->held_pistol_sample_count + 1, capacity);
    };
    const auto establish_gesture_origin = [&]() noexcept {
        state->input_was_owned = true;
        state->pose_was_valid = true;
        state->two_hand_weapon_was_held = two_hand_weapon_held;
        state->held_pistol_identity = held_pistol_identity;
        state->gesture_origin_hand_relative_to_head = relative;
        state->gesture_origin_support_hand_relative_to_head =
            support_relative;
        state->gesture_origin_hand_position = right.grip.pose.position;
        state->gesture_origin_support_hand_position =
            left.grip.pose.position;
        state->gesture_origin_milliseconds = sample_milliseconds;
        state->held_pistol_sample_count = 0;
        state->held_pistol_next_sample = 0;
        if (right_hand_pistol_held) {
            append_pistol_sample();
        }
    };
    if (!state->input_was_owned || !state->pose_was_valid ||
        two_hand_weapon_held != state->two_hand_weapon_was_held ||
        held_pistol_identity != state->held_pistol_identity ||
        sample_milliseconds < state->gesture_origin_milliseconds ||
        (!right_hand_pistol_held &&
         sample_milliseconds == state->gesture_origin_milliseconds)) {
        establish_gesture_origin();
        return PhysicalMeleeGesture::none;
    }

    const bool trigger_interaction_held =
        float_held(right.trigger) || bool_held(right.trigger_click) ||
        float_held(left.trigger);
    if (trigger_interaction_held ||
        sample_milliseconds < state->cooldown_until_milliseconds) {
        // Never allow firing, grenade, or reload motion to accumulate into a
        // delayed melee as soon as the reserved trigger/cooldown is released.
        establish_gesture_origin();
        return PhysicalMeleeGesture::none;
    }

    if (right_hand_pistol_held) {
        // An idle fixed origin can make a fast stab look slow, then discard
        // the rest of it at the 150 ms boundary. Evaluate recent real samples
        // instead. All permission/trigger/cooldown transitions above still
        // clear this history; none of those motions can be replayed later.
        const auto capacity = static_cast<std::uint32_t>(
            state->held_pistol_samples.size());
        if (state->held_pistol_sample_count == 0) {
            establish_gesture_origin();
            return PhysicalMeleeGesture::none;
        }
        const auto& previous = state->held_pistol_samples[
            (state->held_pistol_next_sample + capacity - 1) % capacity];
        if (sample_milliseconds < previous.milliseconds ||
            sample_milliseconds - previous.milliseconds >
                kPhysicalMeleeMaximumSampleMilliseconds) {
            establish_gesture_origin();
            return PhysicalMeleeGesture::none;
        }
        if (sample_milliseconds == previous.milliseconds) {
            // GetTickCount64 can give distinct 90/120 Hz action publications
            // the same integer clock tick. Defer that sample without erasing
            // earlier motion or inventing a zero-time velocity. The next tick
            // still measures real travel from the last retained observation.
            return PhysicalMeleeGesture::none;
        }
        const wawvr::xr::Vec3f frame_travel{
            right.grip.pose.position.x - previous.hand_position.x,
            right.grip.pose.position.y - previous.hand_position.y,
            right.grip.pose.position.z - previous.hand_position.z,
        };
        const float frame_travel_squared = dot(frame_travel, frame_travel);
        const float radius_squared = dot(relative, relative);
        if (!finite(frame_travel_squared) || !finite(radius_squared) ||
            frame_travel_squared > kPhysicalMeleeMaximumWorldTravelMeters *
                                       kPhysicalMeleeMaximumWorldTravelMeters) {
            // Check relocation before choosing any shorter history segment:
            // a tracking jump must not leave a usable pre-jump origin behind.
            establish_gesture_origin();
            return PhysicalMeleeGesture::none;
        }
        const auto forward = wawvr::xr::Rotate(
            right.aim.pose.orientation, {0.0F, 0.0F, -1.0F});
        const float radius = std::sqrt(radius_squared);
        while (state->held_pistol_sample_count > 0) {
            const auto oldest = (state->held_pistol_next_sample + capacity -
                                 state->held_pistol_sample_count) % capacity;
            if (sample_milliseconds -
                    state->held_pistol_samples[oldest].milliseconds <=
                kPhysicalMeleeMaximumSampleMilliseconds) {
                break;
            }
            --state->held_pistol_sample_count;
        }
        const auto first_sample = (state->held_pistol_next_sample + capacity -
                                   state->held_pistol_sample_count) % capacity;
        for (std::uint32_t index = 0;
             index < state->held_pistol_sample_count; ++index) {
            const auto& origin = state->held_pistol_samples[
                (first_sample + index) % capacity];
            const auto elapsed = sample_milliseconds - origin.milliseconds;
            if (elapsed < kPhysicalMeleeMinimumSampleMilliseconds ||
                elapsed > kPhysicalMeleeMaximumSampleMilliseconds) {
                continue;
            }
            const wawvr::xr::Vec3f travel{
                relative.x - origin.hand_relative_to_head.x,
                relative.y - origin.hand_relative_to_head.y,
                relative.z - origin.hand_relative_to_head.z,
            };
            const wawvr::xr::Vec3f world_travel{
                right.grip.pose.position.x - origin.hand_position.x,
                right.grip.pose.position.y - origin.hand_position.y,
                right.grip.pose.position.z - origin.hand_position.z,
            };
            const float travel_squared = dot(travel, travel);
            const float world_squared = dot(world_travel, world_travel);
            const float origin_radius_squared = dot(
                origin.hand_relative_to_head, origin.hand_relative_to_head);
            if (!finite(travel_squared) || !finite(world_squared) ||
                !finite(origin_radius_squared) ||
                travel_squared < kPhysicalMeleeMinimumTravelMeters *
                                     kPhysicalMeleeMinimumTravelMeters ||
                world_squared < kPhysicalMeleeMinimumWorldTravelMeters *
                                    kPhysicalMeleeMinimumWorldTravelMeters ||
                world_squared > kPhysicalMeleeMaximumWorldTravelMeters *
                                    kPhysicalMeleeMaximumWorldTravelMeters) {
                continue;
            }
            const float travel_meters = std::sqrt(travel_squared);
            const float world_meters = std::sqrt(world_squared);
            const float outward = radius - std::sqrt(origin_radius_squared);
            const float speed = travel_meters * 1000.0F /
                                static_cast<float>(elapsed);
            const float world_correlation = dot(travel, world_travel) /
                                            (travel_meters * world_meters);
            const float forward_fraction = dot(travel, forward) / travel_meters;
            if (!finite(outward) || !finite(speed) ||
                !finite(world_correlation) || !finite(forward_fraction) ||
                outward < kPhysicalMeleeMinimumTravelMeters * 0.5F ||
                speed < kPhysicalMeleeMinimumSpeedMetersPerSecond ||
                world_correlation < kPhysicalMeleeMinimumWorldDirectionCorrelation ||
                forward_fraction < kPhysicalMeleeMinimumForwardFraction) {
                continue;
            }
            state->last_trigger_relative_travel_meters = travel_meters;
            state->last_trigger_world_travel_meters = world_meters;
            state->last_trigger_speed_meters_per_second = speed;
            state->last_trigger_outward_travel_meters = outward;
            state->cooldown_until_milliseconds =
                sample_milliseconds + kPhysicalMeleeCooldownMilliseconds;
            establish_gesture_origin();
            return PhysicalMeleeGesture::right_hand_pistol_strike;
        }
        append_pistol_sample();
        return PhysicalMeleeGesture::none;
    }

    const std::uint64_t elapsed_milliseconds =
        sample_milliseconds - state->gesture_origin_milliseconds;
    if (elapsed_milliseconds > kPhysicalMeleeMaximumSampleMilliseconds) {
        establish_gesture_origin();
        return PhysicalMeleeGesture::none;
    }
    if (elapsed_milliseconds < kPhysicalMeleeMinimumSampleMilliseconds) {
        return PhysicalMeleeGesture::none;
    }

    const float previous_radius_squared = dot(
        state->gesture_origin_hand_relative_to_head,
        state->gesture_origin_hand_relative_to_head);
    const wawvr::xr::Vec3f right_travel{
        relative.x - state->gesture_origin_hand_relative_to_head.x,
        relative.y - state->gesture_origin_hand_relative_to_head.y,
        relative.z - state->gesture_origin_hand_relative_to_head.z,
    };
    const wawvr::xr::Vec3f left_travel{
        support_relative.x -
            state->gesture_origin_support_hand_relative_to_head.x,
        support_relative.y -
            state->gesture_origin_support_hand_relative_to_head.y,
        support_relative.z -
            state->gesture_origin_support_hand_relative_to_head.z,
    };
    const wawvr::xr::Vec3f right_world_travel{
        right.grip.pose.position.x - state->gesture_origin_hand_position.x,
        right.grip.pose.position.y - state->gesture_origin_hand_position.y,
        right.grip.pose.position.z - state->gesture_origin_hand_position.z,
    };
    const wawvr::xr::Vec3f left_world_travel{
        left.grip.pose.position.x -
            state->gesture_origin_support_hand_position.x,
        left.grip.pose.position.y -
            state->gesture_origin_support_hand_position.y,
        left.grip.pose.position.z -
            state->gesture_origin_support_hand_position.z,
    };
    const wawvr::xr::Vec3f travel = two_hand_weapon_held
        ? wawvr::xr::Vec3f{
              (right_travel.x + left_travel.x) * 0.5F,
              (right_travel.y + left_travel.y) * 0.5F,
              (right_travel.z + left_travel.z) * 0.5F,
        }
        : right_travel;
    const wawvr::xr::Vec3f world_travel = two_hand_weapon_held
        ? wawvr::xr::Vec3f{
              (right_world_travel.x + left_world_travel.x) * 0.5F,
              (right_world_travel.y + left_world_travel.y) * 0.5F,
              (right_world_travel.z + left_world_travel.z) * 0.5F,
        }
        : right_world_travel;

    const float travel_squared = dot(travel, travel);
    const float world_travel_squared = dot(world_travel, world_travel);
    const float right_world_travel_squared =
        dot(right_world_travel, right_world_travel);
    const float left_world_travel_squared =
        dot(left_world_travel, left_world_travel);
    if (!finite(travel_squared) || travel_squared < 0.0F ||
        !finite(world_travel_squared) || world_travel_squared < 0.0F ||
        !finite(right_world_travel_squared) ||
        right_world_travel_squared < 0.0F ||
        !finite(left_world_travel_squared) ||
        left_world_travel_squared < 0.0F) {
        establish_gesture_origin();
        return PhysicalMeleeGesture::none;
    }
    const float travel_meters = std::sqrt(travel_squared);
    const float world_travel_meters = std::sqrt(world_travel_squared);
    const float right_world_travel_meters =
        std::sqrt(right_world_travel_squared);
    const float left_world_travel_meters =
        std::sqrt(left_world_travel_squared);
    if (right_world_travel_meters >
            kPhysicalMeleeMaximumWorldTravelMeters ||
        (two_hand_weapon_held && left_world_travel_meters >
            kPhysicalMeleeMaximumWorldTravelMeters)) {
        // A normal gesture crosses the attack threshold long before either
        // hand can move thirty centimetres inside this 150 ms window. Treat a
        // larger first observed displacement as tracking relocation, not a
        // superhuman attack.
        establish_gesture_origin();
        return PhysicalMeleeGesture::none;
    }
    const float current_radius_squared = dot(relative, relative);
    const float outward_travel =
        finite(previous_radius_squared) && previous_radius_squared >= 0.0F &&
            finite(current_radius_squared) && current_radius_squared >= 0.0F
        ? std::sqrt(current_radius_squared) -
              std::sqrt(previous_radius_squared)
        : 0.0F;
    const float elapsed_seconds =
        static_cast<float>(elapsed_milliseconds) / 1000.0F;
    const float speed = elapsed_seconds > 0.0F
        ? travel_meters / elapsed_seconds
        : 0.0F;
    const float world_direction_correlation =
        travel_meters > 1.0e-6F && world_travel_meters > 1.0e-6F
        ? dot(travel, world_travel) /
            (travel_meters * world_travel_meters)
        : 0.0F;
    bool deliberate_direction =
        outward_travel >= kPhysicalMeleeMinimumTravelMeters * 0.5F;
    if (two_hand_weapon_held) {
        const wawvr::xr::Vec3f weapon_forward{
            support_relative.x - relative.x,
            support_relative.y - relative.y,
            support_relative.z - relative.z,
        };
        const float weapon_forward_squared = dot(
            weapon_forward, weapon_forward);
        if (!finite(weapon_forward_squared) ||
            weapon_forward_squared <= 1.0e-6F ||
            travel_meters <= 1.0e-6F) {
            deliberate_direction = false;
        } else {
            const float inverse_weapon_length =
                1.0F / std::sqrt(weapon_forward_squared);
            const wawvr::xr::Vec3f normalized_weapon_forward{
                weapon_forward.x * inverse_weapon_length,
                weapon_forward.y * inverse_weapon_length,
                weapon_forward.z * inverse_weapon_length,
            };
            const float forward_fraction =
                dot(travel, normalized_weapon_forward) / travel_meters;
            const float right_forward_travel =
                dot(right_travel, normalized_weapon_forward);
            const float left_forward_travel =
                dot(left_travel, normalized_weapon_forward);
            const float right_travel_squared =
                dot(right_travel, right_travel);
            const float left_travel_squared = dot(left_travel, left_travel);
            const float right_travel_meters =
                finite(right_travel_squared) && right_travel_squared > 0.0F
                ? std::sqrt(right_travel_squared) : 0.0F;
            const float left_travel_meters =
                finite(left_travel_squared) && left_travel_squared > 0.0F
                ? std::sqrt(left_travel_squared) : 0.0F;
            const float larger_hand_travel =
                std::max(right_travel_meters, left_travel_meters);
            const float smaller_hand_travel =
                std::min(right_travel_meters, left_travel_meters);
            const float hand_travel_ratio = larger_hand_travel > 0.0F
                ? smaller_hand_travel / larger_hand_travel : 0.0F;
            const float hand_direction_correlation =
                right_travel_meters > 0.0F && left_travel_meters > 0.0F
                ? dot(right_travel, left_travel) /
                    (right_travel_meters * left_travel_meters)
                : 0.0F;
            deliberate_direction = finite(forward_fraction) &&
                finite(right_forward_travel) &&
                finite(left_forward_travel) &&
                finite(hand_travel_ratio) &&
                finite(hand_direction_correlation) &&
                forward_fraction >= kPhysicalMeleeMinimumForwardFraction &&
                right_forward_travel >=
                    kPhysicalMeleeMinimumPerHandThrustMeters &&
                left_forward_travel >=
                    kPhysicalMeleeMinimumPerHandThrustMeters &&
                hand_travel_ratio >=
                    kPhysicalMeleeMinimumHandTravelRatio &&
                hand_direction_correlation >=
                    kPhysicalMeleeMinimumHandDirectionCorrelation;
        }
    }
    const float minimum_speed = two_hand_weapon_held
        ? kPhysicalMeleeTwoHandMinimumSpeedMetersPerSecond
        : kPhysicalMeleeMinimumSpeedMetersPerSecond;
    if (travel_meters < kPhysicalMeleeMinimumTravelMeters ||
        world_travel_meters < kPhysicalMeleeMinimumWorldTravelMeters ||
        !finite(world_direction_correlation) ||
        world_direction_correlation <
            kPhysicalMeleeMinimumWorldDirectionCorrelation ||
        !deliberate_direction ||
        !finite(speed) ||
        speed < minimum_speed) {
        return PhysicalMeleeGesture::none;
    }

    state->last_trigger_relative_travel_meters = travel_meters;
    state->last_trigger_world_travel_meters = world_travel_meters;
    state->last_trigger_speed_meters_per_second = speed;
    state->last_trigger_outward_travel_meters = outward_travel;
    state->cooldown_until_milliseconds =
        sample_milliseconds + kPhysicalMeleeCooldownMilliseconds;
    establish_gesture_origin();
    return two_hand_weapon_held
        ? PhysicalMeleeGesture::two_hand_thrust
        : PhysicalMeleeGesture::right_hand_swing;
}

void reset_physical_melee_gesture(
    PhysicalMeleeState* const state) noexcept {
    if (state != nullptr) {
        *state = {};
    }
}

bool snap_turn_gameplay_allowed(
    const std::uint32_t key_catchers,
    const std::int32_t connection_state,
    const std::int32_t active_connection_state) noexcept {
    return connection_state == active_connection_state &&
           (key_catchers & kSnapTurnBlockedKeyCatcherMask) == 0;
}

bool controller_gameplay_input_allowed(
    const std::uint32_t key_catchers,
    const std::int32_t connection_state,
    const std::int32_t active_connection_state) noexcept {
    return connection_state == active_connection_state &&
           key_catchers == 0;
}

void suppress_t4_melee_charge(wawvr::t4::UsercmdSp& command) noexcept {
    command.melee_charge_yaw = 0.0F;
    command.melee_charge_distance = 0;
}

namespace {

template <typename Command>
bool apply_snap_turn_to_t4_command_impl(
    Command& command,
    const float snap_degrees,
    float* const client_yaw_degrees,
    wawvr::xr::Basis3f* const sampled_camera_axis) noexcept {
    if (client_yaw_degrees == nullptr || sampled_camera_axis == nullptr ||
        !finite(snap_degrees) || !finite(*client_yaw_degrees) ||
        !valid_basis(*sampled_camera_axis)) {
        return false;
    }

    const float radians = snap_degrees / kRadiansToDegrees;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    if (!finite(cosine) || !finite(sine)) {
        return false;
    }
    wawvr::xr::Basis3f rotated = *sampled_camera_axis;
    const auto rotate_world_yaw = [cosine, sine](
                                      wawvr::xr::Vec3f* const value) noexcept {
        const float x = value->x;
        const float y = value->y;
        value->x = cosine * x - sine * y;
        value->y = sine * x + cosine * y;
    };
    rotate_world_yaw(&rotated.forward);
    rotate_world_yaw(&rotated.left);
    rotate_world_yaw(&rotated.up);
    if (!valid_basis(rotated)) {
        return false;
    }

    float new_yaw = std::fmod(*client_yaw_degrees + snap_degrees, 360.0F);
    if (new_yaw < 0.0F) {
        new_yaw += 360.0F;
    }
    constexpr float kShortUnitsPerDegree = 65536.0F / 360.0F;
    const auto short_delta = static_cast<std::int32_t>(
        std::lround(snap_degrees * kShortUnitsPerDegree));
    const std::uint32_t current =
        static_cast<std::uint32_t>(command.view_angles[1]);
    command.view_angles[1] = static_cast<std::int32_t>(
        (current + static_cast<std::uint32_t>(short_delta)) & 0xFFFFU);
    *client_yaw_degrees = new_yaw;
    *sampled_camera_axis = rotated;
    return true;
}

template <typename Command>
ControllerInputResult apply_controller_input_impl(
    Command& command,
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Basis3f& camera_axis,
    const std::uint64_t now_milliseconds) noexcept {
    ControllerInputResult result{};
    if (!controller_frame_is_current(snapshot, now_milliseconds)) {
        return result;
    }
    result.frame_accepted = true;

    const auto& actions = snapshot.frame.actions;
    const auto& left = actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
    const auto& right = actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Right)];

    float stick_x = 0.0F;
    float stick_y = 0.0F;
    if (remap_stick(left.stick, &stick_x, &stick_y) &&
        snapshot.frame.views_valid &&
        valid_orientation(snapshot.frame.head_center.orientation) &&
        valid_orientation(snapshot.tracking_anchor.orientation)) {
        wawvr::xr::Posef head_orientation{};
        head_orientation.orientation = snapshot.frame.head_center.orientation;
        wawvr::xr::Posef anchor_orientation{};
        anchor_orientation.orientation = snapshot.tracking_anchor.orientation;
        const wawvr::xr::EnginePose relative_head =
            wawvr::xr::OpenXrPoseToIwRelative(
                head_orientation, anchor_orientation, 1.0F);
        if (valid_basis(relative_head.axis)) {
            float head_forward = relative_head.axis.forward.x;
            float head_left = relative_head.axis.forward.y;
            const float horizontal_head_length = std::sqrt(
                head_forward * head_forward + head_left * head_left);
            if (finite(horizontal_head_length) &&
                horizontal_head_length > 1.0e-4F) {
                // Pitch changes only the projection length. Normalize the
                // horizontal heading so it cannot reduce movement speed.
                head_forward /= horizontal_head_length;
                head_left /= horizontal_head_length;
                const float move_forward =
                    stick_y * head_forward + stick_x * head_left;
                const float move_right =
                    stick_x * head_forward - stick_y * head_left;
                command.forward_move = saturating_movement_add(
                    command.forward_move, move_forward);
                command.right_move = saturating_movement_add(
                    command.right_move, move_right);
                result.movement_applied = true;
            }
        }
    }

    const std::uint32_t prior_buttons = command.buttons;
    // Match the COD4 VR default semantic layout. A remains native reload for
    // unsupported weapons; the Kar98 bridge suppresses it in favor of the
    // physical bolt/clip path. Left trigger is reserved for the manual
    // grenade runtime; right-stick up remains the standing jump gesture.
    add_if(command, bool_held(right.primary),
           wawvr::t4::UsercmdButton::reload);
    add_if(command, bool_held(left.primary),
           wawvr::t4::UsercmdButton::use);
    add_if(command, bool_held(left.stick_click),
           wawvr::t4::UsercmdButton::sprint);
    // When the runtime cannot expose a Menu action (SteamVR reserves Quest's
    // physical Menu button for its dashboard), B is the pause/back fallback.
    // Do not also inject a crouch on the frame that opens the pause menu.
    add_if(command, actions.menu.active && bool_held(right.secondary),
           wawvr::t4::UsercmdButton::crouch);
    const bool right_gripping =
        right.squeeze.active && std::isfinite(right.squeeze.current) &&
        right.squeeze.current >= kWeaponGripEngage;
    const bool left_supporting =
        left.squeeze.active && std::isfinite(left.squeeze.current) &&
        left.squeeze.current >= kWeaponGripEngage;
    add_if(command, right_gripping && left_supporting,
           wawvr::t4::UsercmdButton::aim_down_sights);
    const bool melee_held = bool_held(right.stick_click);
    add_if(command, melee_held, wawvr::t4::UsercmdButton::melee);
    result.melee_button_held = melee_held;
    if constexpr (std::is_same_v<Command, wawvr::t4::UsercmdSp>) {
        if (melee_held) {
            // T4's native command builder serializes target-assisted melee
            // yaw and distance after the normal view angles. Keep the knife
            // action, but clear the charge target so it cannot rotate or
            // lunge the VR camera toward a zombie.
            suppress_t4_melee_charge(command);
            result.melee_comfort_applied = true;
        }
    }
    result.gameplay_buttons_applied = command.buttons != prior_buttons;

    float pitch_degrees = 0.0F;
    float yaw_degrees = 0.0F;
    if (controller_aim_degrees(
            snapshot, camera_axis, &pitch_degrees, &yaw_degrees)) {
        // A tracked trigger may fire only while the right grip is physically
        // held. Left ownership supports bolt manipulation but cannot fire.
        // Native mouse/keyboard attack bits remain
        // untouched, and a chest-holstered controller weapon cannot shoot.
        const MountedGunRouteProbe mounted_probe =
            g_mounted_gun_route_probe.load(std::memory_order_acquire);
        const bool trigger_held =
            (float_held(right.trigger) ||
             bool_held(right.trigger_click)) &&
            (right_gripping || (mounted_probe != nullptr &&
                                mounted_probe(snapshot, now_milliseconds)));
        result.weapon_aim_applied = wawvr::t4::apply_vr_weapon_aim(
            command, pitch_degrees, yaw_degrees, trigger_held);
        result.weapon_trigger_applied =
            result.weapon_aim_applied && trigger_held;
        if (result.weapon_aim_applied) {
            result.weapon_pitch_degrees = pitch_degrees;
            result.weapon_yaw_degrees = yaw_degrees;
        }
    }
    return result;
}

}  // namespace

void bind_mounted_gun_route_probe(
    const MountedGunRouteProbe probe) noexcept {
    g_mounted_gun_route_probe.store(probe, std::memory_order_release);
}

bool apply_snap_turn_to_t4_command(
    wawvr::t4::UsercmdSp& command,
    const float snap_degrees,
    float* const client_yaw_degrees,
    wawvr::xr::Basis3f* const sampled_camera_axis) noexcept {
    return apply_snap_turn_to_t4_command_impl(
        command, snap_degrees, client_yaw_degrees, sampled_camera_axis);
}

bool apply_snap_turn_to_t4_command(
    wawvr::t4::UsercmdMp& command,
    const float snap_degrees,
    float* const client_yaw_degrees,
    wawvr::xr::Basis3f* const sampled_camera_axis) noexcept {
    return apply_snap_turn_to_t4_command_impl(
        command, snap_degrees, client_yaw_degrees, sampled_camera_axis);
}

ControllerInputResult apply_controller_input(
    wawvr::t4::UsercmdSp& command,
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Basis3f& camera_axis,
    const std::uint64_t now_milliseconds) noexcept {
    return apply_controller_input_impl(
        command, snapshot, camera_axis, now_milliseconds);
}

ControllerInputResult apply_controller_input(
    wawvr::t4::UsercmdMp& command,
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Basis3f& camera_axis,
    const std::uint64_t now_milliseconds) noexcept {
    return apply_controller_input_impl(
        command, snapshot, camera_axis, now_milliseconds);
}

}  // namespace wawvr::mod
