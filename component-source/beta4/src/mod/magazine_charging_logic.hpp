// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace wawvr::mod {

enum class MagazineChargingEvent : std::uint8_t {
    None,
    ChargeRequired,
    Grabbed,
    FullyOpened,
    Released,
    SpringReleased,
    Charged,
    ControlsRearmed,
    Reset,
};

enum class MagazineChargingCompletion : std::uint8_t {
    SpringClosed,
    LatchOpen,
};

enum class MagazineChargingControlPolicy : std::uint8_t {
    // Existing detachable-magazine weapons: the free hand deliberately pulls
    // and releases the action after an empty reload.
    ManualPullRelease,
    // M1 Garand: inserting a loaded en-bloc clip releases the locked-open
    // action automatically; controller trigger/grab motion is not involved.
    EnBlocAutomatic,
};

inline constexpr std::size_t kMaximumMagazineChargingReturnSamples = 8;

struct MagazineChargingCalibration final {
    MagazineChargingControlPolicy control_policy{
        MagazineChargingControlPolicy::ManualPullRelease};
    float travel_units{};
    // Empty M1-style actions rest visibly rearward before the user tugs them
    // to the full-rear release point. Zero keeps a future slide closed.
    float locked_open_offset_units{};
    float open_threshold{};
    float spring_return_seconds{};
    float trigger_engage{};
    float trigger_release{};
    MagazineChargingCompletion completion{
        MagazineChargingCompletion::SpringClosed};
    std::uint8_t return_sample_count{};
    std::array<float, kMaximumMagazineChargingReturnSamples> return_samples{};
};

struct MagazineChargingFrame final {
    bool enabled{};
    bool focused{};
    bool weapon_supported{};
    bool reset_requested{};
    std::uint64_t action_sequence{};
    float delta_seconds{};

    // One frame at the successful end of a reload which began with an empty
    // magazine. This is deliberately independent from shot timing.
    bool arm_charge{};
    // Charging cannot clear the lock unless the exact live clip contains a
    // round. This prevents an empty pull from manufacturing a chambered shot.
    bool cartridge_available{};
    // A profile-authorized loaded en-bloc insertion has committed and may
    // release the locked-open action. Manual profiles leave this false.
    bool automatic_spring_release{};
    bool left_rifle_gripped{};
    bool right_hand_pose_valid{};
    bool right_hand_near_handle{};
    bool right_grip_held{};
    bool right_trigger_active{};
    float right_trigger_value{};

    // Projection of (right hand - closed handle anchor) onto rifle forward.
    // Pulling the controller rearward decreases this coordinate.
    float right_hand_forward_coordinate{};

    // Symmetric free-left-hand path. A trigger edge near the handle selects
    // one manipulating hand for the complete pull/release transaction.
    bool right_rifle_gripped{};
    bool left_hand_pose_valid{};
    bool left_hand_near_handle{};
    bool left_grip_held{};
    bool left_trigger_active{};
    float left_trigger_value{};
    float left_hand_forward_coordinate{};
};

struct MagazineChargingState final {
    bool input_owned{};
    std::uint64_t last_action_sequence{};
    // Right-trigger baseline retained under the original member name.
    bool trigger_was_held{};
    bool left_trigger_was_held{};

    bool manipulating_hand_selected{};
    bool manipulating_left_hand{};

    bool charge_required{};
    float handle_fraction{};
    bool handle_grabbed{};
    float grab_start_coordinate{};
    float grab_start_fraction{};
    bool fully_opened{};
    bool spring_returning{};
    float spring_return_elapsed_seconds{};
    bool awaiting_controls_release{};
};

struct MagazineChargingUpdate final {
    MagazineChargingEvent event{MagazineChargingEvent::None};
    float handle_fraction{};
    bool trigger_pressed_edge{};
    bool trigger_released_edge{};
    bool handle_grabbed{};
    bool charge_required{};
    bool block_attack{};
    bool reserve_right_grip{};
    bool reserve_left_grip{};
    bool manipulating_left_hand{};
};

[[nodiscard]] bool validate_magazine_charging_calibration(
    const MagazineChargingCalibration& calibration) noexcept;

// A completed rearward pull chambers the rifle only when the index trigger is
// deliberately released. The handle then springs closed; a partial pull also
// springs closed but keeps the chamber lock armed.
[[nodiscard]] MagazineChargingUpdate update_magazine_charging(
    const MagazineChargingCalibration& calibration,
    const MagazineChargingFrame& frame,
    MagazineChargingState* state) noexcept;

void reset_magazine_charging(MagazineChargingState* state) noexcept;

}  // namespace wawvr::mod
