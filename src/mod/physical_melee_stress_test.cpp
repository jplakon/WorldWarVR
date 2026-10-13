// SPDX-License-Identifier: GPL-3.0-only
#include "input_mapping.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
using namespace wawvr;
using Gesture = mod::PhysicalMeleeGesture;
constexpr std::uint64_t kPistolIdentity = 0x123400000007ULL;

enum class Mode { free_hand, pistol, rifle };

struct Fixture {
    Mode mode;
    mod::ControllerFrameSnapshot snapshot{};
    mod::PhysicalMeleeState state{};
    std::uint64_t identity{};
    bool owned{true};
    bool supported_weapon_owned{};
    std::uint32_t attacks{};

    xr::HandActionState& right() {
        return snapshot.frame.actions.hands[
            static_cast<std::uint32_t>(xr::Hand::Right)];
    }
    xr::HandActionState& left() {
        return snapshot.frame.actions.hands[
            static_cast<std::uint32_t>(xr::Hand::Left)];
    }
    explicit Fixture(Mode requested_mode) : mode(requested_mode) {
        snapshot.frame.views_valid = true;
        snapshot.frame.actions.focused = true;
        snapshot.frame.actions.sequence = 1;
        snapshot.publication_milliseconds = 1'000;
        for (auto& hand : snapshot.frame.actions.hands) {
            hand.grip.active = true;
            hand.grip.position_valid = true;
            hand.grip.position_tracked = true;
            hand.aim.active = true;
            hand.aim.orientation_valid = true;
            hand.aim.orientation_tracked = true;
            hand.aim.pose.orientation.w = 1.0F;
            hand.squeeze = {true, 0.0F, false};
            hand.trigger = {true, 0.0F, false};
        }
        right().grip.pose.position = {0.20F, -0.25F, -0.45F};
        left().grip.pose.position = {0.20F, -0.25F, -0.75F};
        if (mode != Mode::free_hand) right().squeeze.current = 1.0F;
        if (mode == Mode::rifle) {
            left().squeeze.current = 1.0F;
            supported_weapon_owned = true;
        }
        if (mode == Mode::pistol) identity = kPistolIdentity;
        sample(0);
    }
    Gesture sample(std::uint64_t elapsed) {
        ++snapshot.frame.actions.sequence;
        snapshot.publication_milliseconds += elapsed;
        const auto result = mod::update_physical_melee_gesture(
            owned, snapshot, snapshot.publication_milliseconds + 1,
            &state, supported_weapon_owned, identity);
        if (result != Gesture::none) ++attacks;
        return result;
    }
    void move_forward(float meters) {
        right().grip.pose.position.z -= meters;
        if (mode == Mode::rifle) left().grip.pose.position.z -= meters;
    }
    Gesture expected_attack() const {
        return mode == Mode::rifle ? Gesture::two_hand_thrust :
            mode == Mode::pistol ? Gesture::right_hand_pistol_strike :
                                  Gesture::right_hand_swing;
    }
};

void require(bool condition, const char* label, Mode mode) {
    if (!condition) {
        throw std::runtime_error(std::string(label) + " (mode=" +
                                 std::to_string(static_cast<int>(mode)) + ")");
    }
}

void firing_release_regression() {
    std::uint32_t release_failures = 0;
    for (const auto mode : {Mode::free_hand, Mode::pistol, Mode::rifle}) {
        for (int trigger = 0; trigger < 3; ++trigger) {
            Fixture f(mode);
            if (trigger == 0) f.right().trigger.current = 1.0F;
            if (trigger == 1) f.right().trigger_click = {true, true, true};
            if (trigger == 2) f.left().trigger.current = 1.0F;
            f.move_forward(0.02F);
            require(f.sample(20) == Gesture::none,
                    "trigger interaction must suppress motion", mode);

            f.right().trigger.current = 0.0F;
            f.right().trigger_click.current = false;
            f.left().trigger.current = 0.0F;
            f.move_forward(0.08F);
            if (f.sample(40) != Gesture::none) {
                std::fprintf(stderr,
                    "Unexpected release attack: mode=%d trigger=%d\n",
                    static_cast<int>(mode), trigger);
                ++release_failures;
                continue;
            }
            require(f.sample(11) == Gesture::none,
                    "discarded firing motion cannot replay after release", mode);
            f.move_forward(0.08F);
            require(f.sample(40) == f.expected_attack(),
                    "fresh intentional attack must work after trigger release", mode);
            require(f.attacks == 1, "exactly one fresh attack is allowed", mode);
        }
    }
    if (release_failures != 0) {
        throw std::runtime_error("motion crossing trigger release produced " +
                                 std::to_string(release_failures) + " unexpected attacks");
    }
}

void ordinary_motion_stress() {
    for (const auto mode : {Mode::free_hand, Mode::pistol, Mode::rifle}) {
        for (const std::uint64_t frame_ms : {8ULL, 11ULL, 16ULL, 22ULL, 33ULL}) {
            Fixture f(mode);
            for (std::uint64_t step = 0; step < 5'000; ++step) {
                // A deterministic mixture of sight adjustment, hand noise,
                // and walking/leaning. Both hands and HMD share body motion;
                // only bounded hand noise changes their relative distances.
                const float time = static_cast<float>(step * frame_ms) / 1000.0F;
                const float body = 0.30F * std::sin(time * 0.7F);
                const float jitter = (step & 1U) ? 0.004F : -0.004F;
                const float sweep = 0.12F * std::sin(time * 1.1F);
                f.snapshot.frame.head_center.position.x = body;
                f.right().grip.pose.position = {
                    0.20F + body + sweep + jitter,
                    -0.25F + 0.015F * std::sin(time), -0.45F + jitter};
                f.left().grip.pose.position = {
                    0.20F + body + sweep - jitter,
                    -0.25F + 0.015F * std::sin(time), -0.75F - jitter};
                const float half_yaw = 0.35F * std::sin(time * 0.8F);
                f.right().aim.pose.orientation = {
                    0.0F, std::sin(half_yaw), 0.0F, std::cos(half_yaw)};
                require(f.sample(frame_ms) == Gesture::none,
                        "aiming, walking and bounded hand noise must remain quiet", mode);
                require(mod::update_physical_melee_gesture(
                            f.owned, f.snapshot, f.snapshot.publication_milliseconds + 1,
                            &f.state, f.supported_weapon_owned, f.identity) == Gesture::none,
                        "duplicate frame must remain quiet", mode);
            }
            require(f.attacks == 0, "ordinary motion must produce zero attacks", mode);
        }
    }
}

void ownership_and_tracking_stress() {
    for (const auto mode : {Mode::free_hand, Mode::pistol, Mode::rifle}) {
        for (int interruption = 0; interruption < 7; ++interruption) {
            Fixture f(mode);
            f.move_forward(0.02F);
            require(f.sample(20) == Gesture::none,
                    "partial motion cannot attack before interruption", mode);
            if (interruption == 0) f.owned = false;
            if (interruption == 1) f.snapshot.frame.actions.focused = false;
            if (interruption == 2) f.snapshot.frame.views_valid = false;
            if (interruption == 3) f.right().grip.position_valid = false;
            if (interruption == 4) f.right().grip.active = false;
            if (interruption == 5) f.move_forward(0.45F);
            f.move_forward(0.08F);
            require(f.sample(interruption == 6 ? 151 : 40) == Gesture::none,
                    "interruption must discard pending motion", mode);
            f.owned = true;
            f.snapshot.frame.actions.focused = true;
            f.snapshot.frame.views_valid = true;
            f.right().grip.position_valid = true;
            f.right().grip.active = true;
            require(f.sample(11) == Gesture::none,
                    "recovery must establish a quiet baseline", mode);
            f.move_forward(0.08F);
            require(f.sample(40) == f.expected_attack(),
                    "fresh attack must work after recovery", mode);
        }
    }
    for (const auto mode : {Mode::pistol, Mode::rifle}) {
        Fixture f(mode);
        f.move_forward(0.02F);
        require(f.sample(20) == Gesture::none, "partial grip motion", mode);
        f.right().squeeze.current = 0.0F;
        f.move_forward(0.08F);
        require(f.sample(40) == Gesture::none, "release grip must be quiet", mode);
        f.right().squeeze.current = 1.0F;
        f.move_forward(0.08F);
        require(f.sample(40) == Gesture::none, "regrip must establish baseline", mode);
        f.move_forward(0.08F);
        require(f.sample(40) == f.expected_attack(), "intentional attack after regrip", mode);
    }
    Fixture pistol(Mode::pistol);
    // The production caller withholds exact pistol identity while native
    // reload/switch/hand reservation owns it. Returning identity must discard
    // the movement accumulated during that interval.
    pistol.identity = 0;
    for (int sample = 0; sample < 8; ++sample) {
        pistol.move_forward(0.04F);
        require(pistol.sample(16) == Gesture::none,
                "reload reservation must suppress held-pistol motion", pistol.mode);
    }
    pistol.identity = kPistolIdentity;
    pistol.move_forward(0.08F);
    require(pistol.sample(40) == Gesture::none,
            "reload completion must establish a new baseline", pistol.mode);
    pistol.move_forward(0.08F);
    require(pistol.sample(40) == pistol.expected_attack(),
            "fresh attack must work after reload", pistol.mode);
}

void release_boundary_and_reload_ownership() {
    for (const auto mode : {Mode::free_hand, Mode::pistol, Mode::rifle}) {
        for (const std::uint64_t release_ms : {0ULL, 1ULL, 8ULL, 16ULL, 40ULL}) {
            Fixture f(mode);
            // This includes losing input ownership while a trigger is held,
            // then resuming with that trigger still held. The first valid
            // baseline must remember the interaction before its early return.
            f.owned = false;
            f.right().trigger.current = 1.0F;
            f.move_forward(0.08F);
            require(f.sample(16) == Gesture::none,
                    "unowned held-trigger motion remains quiet", mode);
            f.owned = true;
            require(f.sample(16) == Gesture::none,
                    "fresh owned baseline remembers held trigger", mode);
            f.right().trigger.current = 0.0F;
            f.move_forward(0.08F);
            require(f.sample(release_ms) == Gesture::none,
                    "equal-clock and sub-frame trigger releases discard prior motion", mode);
            require(f.sample(11) == Gesture::none,
                    "release boundary motion cannot replay on the next clock tick", mode);
            f.move_forward(0.08F);
            require(f.sample(40) == f.expected_attack(),
                    "fresh gesture survives release timing and ownership recovery", mode);
        }

        Fixture f(mode);
        f.right().trigger.current = 1.0F;
        require(f.sample(16) == Gesture::none, "right trigger held", mode);
        f.left().trigger.current = 1.0F;
        f.right().trigger.current = 0.0F;
        f.move_forward(0.08F);
        require(f.sample(40) == Gesture::none,
                "overlapping interactions remain reserved until both triggers release", mode);
        f.left().trigger.current = 0.0F;
        f.move_forward(0.08F);
        require(f.sample(40) == Gesture::none,
                "last overlapping trigger release establishes a fresh baseline", mode);
        f.move_forward(0.08F);
        require(f.sample(40) == f.expected_attack(),
                "fresh deliberate attack works after overlapping interactions", mode);
    }

    for (const bool release_during_reload : {false, true}) {
        Fixture f(Mode::pistol);
        f.left().trigger.current = 1.0F;
        require(f.sample(16) == Gesture::none,
                "pistol reload interaction establishes trigger ownership", f.mode);
        f.identity = 0;
        for (int step = 0; step < 8; ++step) {
            f.move_forward(0.04F);
            require(f.sample(16) == Gesture::none,
                    "reserved pistol cannot turn reload motion into melee", f.mode);
        }
        if (release_during_reload) f.left().trigger.current = 0.0F;
        f.identity = kPistolIdentity + 1;
        f.move_forward(0.08F);
        require(f.sample(40) == Gesture::none,
                "returning or switching pistol identity after reload cannot inherit motion", f.mode);
        f.left().trigger.current = 0.0F;
        if (!release_during_reload) {
            f.move_forward(0.08F);
            require(f.sample(40) == Gesture::none,
                    "trigger release after pistol permission returns stays quiet", f.mode);
        }
        f.move_forward(0.08F);
        require(f.sample(40) == f.expected_attack(),
                "fresh pistol attack works after reload permission and trigger recovery", f.mode);
    }

    for (const bool right_hand_ammo : {false, true}) {
        for (const bool trigger_interaction : {false, true}) {
            Fixture f(Mode::rifle);
            auto& ammo_hand = right_hand_ammo ? f.right() : f.left();
            ammo_hand.squeeze.current = 0.0F;
            f.supported_weapon_owned = false;
            if (trigger_interaction) ammo_hand.trigger.current = 1.0F;
            for (int step = 0; step < 8; ++step) {
                f.move_forward(0.04F);
                require(f.sample(16) == Gesture::none,
                        "ammo manipulation without two-hand weapon ownership cannot thrust", f.mode);
            }
            // Gripping ammunition is not two-hand ownership of the rifle.
            // The gameplay owner must explicitly restore support permission.
            ammo_hand.squeeze.current = 1.0F;
            f.move_forward(0.08F);
            require(f.sample(40) == Gesture::none,
                    "both squeezed hands cannot claim a rifle during ammo ownership", f.mode);
            f.supported_weapon_owned = true;
            f.move_forward(0.08F);
            require(f.sample(40) == Gesture::none,
                    "restored rifle support ownership establishes a fresh baseline", f.mode);
            if (trigger_interaction) {
                ammo_hand.trigger.current = 0.0F;
                f.move_forward(0.08F);
                require(f.sample(40) == Gesture::none,
                        "ammo trigger release after support restoration stays quiet", f.mode);
            }
            f.move_forward(0.08F);
            require(f.sample(40) == f.expected_attack(),
                    "deliberate rifle thrust works after either-hand ammo ownership", f.mode);
        }
    }
}
} // namespace

int main() {
    try {
        firing_release_regression();
        ordinary_motion_stress();
        ownership_and_tracking_stress();
        release_boundary_and_reload_ownership();
        std::puts("PASS: trigger-release regression (9), ordinary motion samples (75000), duplicate commands (75000), ownership/tracking recovery (21), grip recovery (2), pistol reload reservation (1), release timing and owned-baseline recovery (15), overlapping triggers (3), pistol reload/identity/trigger recovery (2), either-hand rifle ammo ownership (4).");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
