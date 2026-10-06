// SPDX-License-Identifier: GPL-3.0-only
#include "tank_control_logic.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using namespace wawvr::mod;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(const float actual, const float expected,
          const float tolerance = 1.0e-4F) {
    return std::abs(actual - expected) <= tolerance;
}

bool zero(const TankControlDelta delta) {
    return delta.pitch_degrees == 0.0F && delta.yaw_degrees == 0.0F;
}

TankControlInput sample(const std::uint64_t sequence,
                        const std::uint64_t milliseconds,
                        const float x = 0.0F, const float y = 0.0F) {
    return {true, true, sequence, milliseconds, x, y};
}

void test_axes_and_retained_neutral() {
    TankControlState state{};
    expect(zero(update_tank_control(sample(1, 1'000, 1.0F), &state)),
           "first held-stick sample cannot jump the turret");
    auto delta = update_tank_control(sample(2, 1'020, 1.0F), &state);
    expect(near(delta.yaw_degrees, -1.30F) && delta.pitch_degrees == 0.0F,
           "full right rotates right at 65 degrees per second");
    delta = update_tank_control(sample(3, 1'040, -1.0F), &state);
    expect(near(delta.yaw_degrees, 1.30F) && delta.pitch_degrees == 0.0F,
           "left reverses yaw without pitch drift");
    delta = update_tank_control(sample(4, 1'060, 0.0F, 1.0F), &state);
    expect(near(delta.pitch_degrees, -0.70F) && delta.yaw_degrees == 0.0F,
           "up raises the actual cannon with negative native pitch at 35 degrees per second");
    delta = update_tank_control(sample(5, 1'080, 0.0F, -1.0F), &state);
    expect(near(delta.pitch_degrees, 0.70F) && delta.yaw_degrees == 0.0F,
           "down lowers the actual cannon without yaw drift");

    float retained_pitch = 14.125F;
    float retained_yaw = 319.375F;
    for (std::uint64_t sequence = 6; sequence < 60; ++sequence) {
        delta = update_tank_control(sample(sequence, 1'000 + sequence * 20), &state);
        retained_pitch += delta.pitch_degrees;
        retained_yaw += delta.yaw_degrees;
    }
    expect(retained_pitch == 14.125F && retained_yaw == 319.375F,
           "released stick retains the exact target over many frames");
}

void test_radial_deadzone_diagonals_and_fine_aim() {
    TankControlState state{};
    static_cast<void>(update_tank_control(sample(1, 1'000), &state));
    expect(zero(update_tank_control(sample(2, 1'020, 0.20F), &state)),
           "deadzone boundary produces no turn");
    expect(zero(update_tank_control(sample(3, 1'040, 0.14F, 0.14F), &state)),
           "small diagonal inside radial deadzone produces no turn");
    const auto fine = update_tank_control(sample(4, 1'060, 0.30F), &state);
    const auto medium = update_tank_control(sample(5, 1'080, 0.60F), &state);
    const auto full = update_tank_control(sample(6, 1'100, 1.0F), &state);
    expect(fine.yaw_degrees < 0.0F &&
               std::abs(fine.yaw_degrees) < std::abs(medium.yaw_degrees) &&
               std::abs(medium.yaw_degrees) < std::abs(full.yaw_degrees) &&
               std::abs(medium.yaw_degrees) < std::abs(full.yaw_degrees) * 0.5F,
           "partial tilt progressively aims more gently than linear response");
    const auto diagonal = update_tank_control(sample(7, 1'120, 1.0F, 1.0F), &state);
    const float yaw_fraction = diagonal.yaw_degrees / -1.30F;
    const float pitch_fraction = diagonal.pitch_degrees / -0.70F;
    expect(diagonal.pitch_degrees < 0.0F && diagonal.yaw_degrees < 0.0F &&
               near(yaw_fraction, pitch_fraction) &&
               near(yaw_fraction * yaw_fraction + pitch_fraction * pitch_fraction, 1.0F),
           "diagonal aiming controls both axes without a corner speed boost");
    const auto clamped = update_tank_control(sample(8, 1'140, 3.0F, 0.0F), &state);
    expect(near(clamped.yaw_degrees, -1.30F),
           "finite out-of-range stick input is bounded to full speed");
}

void test_sequence_and_time_guards() {
    TankControlState state{};
    static_cast<void>(update_tank_control(sample(10, 1'000, 1.0F), &state));
    const auto first = update_tank_control(sample(11, 1'020, 1.0F), &state);
    expect(zero(update_tank_control(sample(11, 1'025, 1.0F), &state)),
           "duplicate native commands cannot consume one XR sample twice");
    const auto next = update_tank_control(sample(12, 1'040, 1.0F), &state);
    expect(near(first.yaw_degrees, next.yaw_degrees),
           "duplicates cannot alter the timing of the next sample");
    const auto hitch = update_tank_control(sample(13, 5'000, 1.0F), &state);
    expect(near(hitch.yaw_degrees, -3.25F),
           "a long hitch applies at most 50 milliseconds of rotation");
    expect(zero(update_tank_control(sample(14, 5'000, 1.0F), &state)),
           "different samples at the same timestamp do not add displacement");
    expect(zero(update_tank_control(sample(15, 4'000, 1.0F), &state)),
           "clock rollback rebaselines without unsigned elapsed-time overflow");
    expect(zero(update_tank_control(sample(1, 4'020, 1.0F), &state)),
           "action sequence restart rebaselines without a turn");
    const auto resumed = update_tank_control(sample(2, 4'040, 1.0F), &state);
    expect(near(resumed.yaw_degrees, -1.30F),
           "normal aiming resumes after sequence restart");
}

void test_ownership_and_invalid_input() {
    TankControlState state{};
    static_cast<void>(update_tank_control(sample(1, 1'000, 1.0F), &state));
    auto paused = sample(2, 1'020, 1.0F);
    paused.input_owned = false;
    expect(zero(update_tank_control(paused, &state)) && !state.input_was_owned &&
               state.previous_update_milliseconds == 0 && state.last_action_sequence == 0,
           "pause, focus loss, or tank exit clears integration timing");
    expect(zero(update_tank_control(sample(3, 60'000, 1.0F), &state)),
           "regaining focus with stick held cannot integrate the paused time");
    const auto resumed = update_tank_control(sample(4, 60'020, 1.0F), &state);
    expect(near(resumed.yaw_degrees, -1.30F),
           "next fresh focused sample resumes smooth aiming");

    auto invalid = sample(5, 60'040, 1.0F);
    invalid.stick_active = false;
    expect(zero(update_tank_control(invalid, &state)) && !state.input_was_owned,
           "inactive controller cannot steer the turret");
    invalid = sample(6, 60'060, std::numeric_limits<float>::quiet_NaN());
    expect(zero(update_tank_control(invalid, &state)), "NaN stick fails closed");
    invalid = sample(7, 60'080, 0.0F, std::numeric_limits<float>::infinity());
    expect(zero(update_tank_control(invalid, &state)), "infinite stick fails closed");
    expect(zero(update_tank_control(sample(0, 60'100, 1.0F), &state)),
           "unpublished action sequence fails closed");
    expect(zero(update_tank_control(sample(8, 0, 1.0F), &state)),
           "missing timestamp fails closed");
    expect(zero(update_tank_control(sample(9, 60'120, 1.0F), nullptr)),
           "missing state fails closed");
    static_cast<void>(update_tank_control(sample(10, 60'140, 1.0F), &state));
    reset_tank_control(&state);
    expect(!state.input_was_owned && state.last_action_sequence == 0 &&
               state.previous_update_milliseconds == 0,
           "explicit vehicle transition reset clears all timing");
    reset_tank_control(nullptr);
}

float integrated_yaw(const std::uint64_t interval) {
    TankControlState state{};
    static_cast<void>(update_tank_control(sample(1, 1'000, 0.75F), &state));
    float yaw = 0.0F;
    std::uint64_t sequence = 1;
    for (std::uint64_t elapsed = interval; elapsed <= 1'000; elapsed += interval) {
        yaw += update_tank_control(sample(++sequence, 1'000 + elapsed, 0.75F), &state).yaw_degrees;
    }
    return yaw;
}

void test_frame_rate_independence() {
    expect(near(integrated_yaw(10), integrated_yaw(20), 0.001F) &&
               near(integrated_yaw(20), integrated_yaw(50), 0.001F),
           "equal held-stick time produces the same aim at different frame rates");
}

void test_native_rate_limiting() {
    const TankControlDelta full_sample{-0.70F, -1.30F};
    const auto native = limit_tank_control_delta(full_sample, 45.0F);
    expect(near(native.pitch_degrees, -0.70F) && near(native.yaw_degrees, -0.90F),
           "OT-34 45-degree native rate caps yaw while preserving slower pitch");
    const auto slow = limit_tank_control_delta(full_sample, 20.0F);
    expect(near(slow.pitch_degrees, -0.40F) && near(slow.yaw_degrees, -0.40F),
           "slow native traverse caps both requested axes");
    const auto fast = limit_tank_control_delta(full_sample, 90.0F);
    expect(near(fast.pitch_degrees, full_sample.pitch_degrees) &&
               near(fast.yaw_degrees, full_sample.yaw_degrees),
           "faster native turret cannot accelerate the chosen control speeds");
    expect(zero(limit_tank_control_delta({}, 45.0F)),
           "native rate limiting cannot move a neutral target");
    for (const float invalid : {0.0F, -1.0F, 361.0F,
                               std::numeric_limits<float>::infinity(),
                               std::numeric_limits<float>::quiet_NaN()}) {
        expect(zero(limit_tank_control_delta(full_sample, invalid)),
               "missing or invalid native traverse rate prevents rotation");
    }
    expect(zero(limit_tank_control_delta(
               {std::numeric_limits<float>::quiet_NaN(), 0.0F}, 45.0F)),
           "nonfinite requested rotation cannot reach command encoding");

    TankControlState state{};
    static_cast<void>(update_tank_control(sample(1, 1'000, 1.0F), &state));
    float target_yaw = 0.0F;
    float barrel_yaw = 0.0F;
    for (std::uint64_t sequence = 2; sequence <= 101; ++sequence) {
        const auto limited = limit_tank_control_delta(update_tank_control(
            sample(sequence, 1'000 + (sequence - 1) * 20, 1.0F), &state), 45.0F);
        target_yaw += limited.yaw_degrees;
        const float error = target_yaw - barrel_yaw;
        barrel_yaw += std::clamp(error, -0.90F, 0.90F);
    }
    expect(near(target_yaw, barrel_yaw, 0.001F),
           "two seconds of held input builds no target backlog above native speed");
    const float released = barrel_yaw;
    const auto stopped = limit_tank_control_delta(
        update_tank_control(sample(102, 3'020), &state), 45.0F);
    target_yaw += stopped.yaw_degrees;
    barrel_yaw += std::clamp(target_yaw - barrel_yaw, -0.90F, 0.90F);
    expect(near(barrel_yaw, released),
           "ideal native rate follower has no queued motion after neutral");
}

void test_native_command_offsets_and_neutral_preservation() {
    // An offset CL pitch beyond +/-85 is legitimate: the native world angle
    // additionally includes ps.delta_angles. Neutral input must not clamp it.
    float pitch = 217.125F;
    float yaw = 675.0F;
    std::array<std::int32_t, 3> command{0x12345, 0xFEDC, 0x12345678};
    const auto original = command;
    expect(apply_tank_control_delta({}, &pitch, &yaw, &command) &&
               pitch == 217.125F && yaw == 675.0F && command == original,
           "neutral retains native CL offsets and serialized command bits exactly");

    expect(apply_tank_control_delta({0.0F, -0.90F}, &pitch, &yaw, &command),
           "valid yaw delta is applied to completed native command");
    const auto yaw_short_delta = static_cast<std::int32_t>(std::lround(-0.90F * (65536.0F / 360.0F)));
    const auto expected_yaw = static_cast<std::int32_t>(
        (static_cast<std::uint32_t>(original[1]) +
         static_cast<std::uint32_t>(yaw_short_delta)) & 0xFFFFU);
    expect(pitch == 217.125F && command[0] == original[0] &&
               near(yaw, -45.90F) && command[1] == expected_yaw &&
               command[2] == original[2],
           "yaw adds to existing command offset and cannot disturb pitch or roll");

    command = {0x0002, 0xFFFD, 0x11223344};
    pitch = -10.0F;
    yaw = 179.5F;
    expect(apply_tank_control_delta({-0.70F, 0.90F}, &pitch, &yaw, &command) &&
               near(pitch, -10.70F) && near(yaw, -179.60F) &&
               command[0] > 0xFF00 && command[1] < 0x0100 && command[2] == 0x11223344,
           "positive and negative encoded deltas wrap at native 16-bit boundaries");

    pitch = -0.0F;
    yaw = -0.0F;
    const auto pitch_bits = std::bit_cast<std::uint32_t>(pitch);
    const auto yaw_bits = std::bit_cast<std::uint32_t>(yaw);
    expect(apply_tank_control_delta({}, &pitch, &yaw, &command) &&
               std::bit_cast<std::uint32_t>(pitch) == pitch_bits &&
               std::bit_cast<std::uint32_t>(yaw) == yaw_bits,
           "zero input preserves even signed-zero native angle representations");

    const auto before_failure = command;
    expect(!apply_tank_control_delta(
               {1.0F, std::numeric_limits<float>::infinity()}, &pitch, &yaw, &command) &&
               command == before_failure && pitch == 0.0F && yaw == 0.0F,
           "invalid second axis cannot partially mutate the first axis");
    expect(!apply_tank_control_delta({0.0F, 1.0e30F}, &pitch, &yaw, &command) &&
               command == before_failure,
           "extreme finite delta fails before unsafe float-to-integer conversion");
    expect(!apply_tank_control_delta({}, nullptr, &yaw, &command) &&
               !apply_tank_control_delta({}, &pitch, &yaw, nullptr),
           "missing native storage fails without mutation");
}

}  // namespace

int main() {
    test_axes_and_retained_neutral();
    test_radial_deadzone_diagonals_and_fine_aim();
    test_sequence_and_time_guards();
    test_ownership_and_invalid_input();
    test_frame_rate_independence();
    test_native_rate_limiting();
    test_native_command_offsets_and_neutral_preservation();
    if (failures != 0) {
        std::cerr << failures << " tank control test(s) failed\n";
        return 1;
    }
    std::cout << "Tank control logic tests passed\n";
    return 0;
}
