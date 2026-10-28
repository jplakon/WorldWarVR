// SPDX-License-Identifier: GPL-3.0-only
#include "reload_pouch_logic.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using namespace wawvr::mod;
using namespace wawvr::xr;
void expect(const bool condition, const char* const description) {
    if (!condition) { std::cerr << "FAIL: " << description << '\n'; std::exit(1); }
}
bool near(const float a, const float b) { return std::abs(a-b) < 0.0002F; }
Quaternionf multiply(const Quaternionf a, const Quaternionf b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
Quaternionf orientation(const float yaw, const float pitch, const float roll) {
    constexpr float half_radians = 0.008726646259971648F;
    const Quaternionf y{0, std::sin(yaw*half_radians), 0, std::cos(yaw*half_radians)};
    const Quaternionf p{std::sin(pitch*half_radians), 0, 0, std::cos(pitch*half_radians)};
    const Quaternionf r{0, 0, std::sin(roll*half_radians), std::cos(roll*half_radians)};
    return multiply(multiply(y,p),r);
}
void legacy_and_seated_geometry() {
    for (const bool left : {false,true}) {
        for (const float x : {-18.0F,0.0F,24.0F})
            for (const float y : {2.0F,13.0F,24.0F})
                for (const float z : {-42.0F,-28.0F,-14.0F})
                    expect(reload_pouch_contains({x,left?y:-y,z},left),
                        "every original standing belt corner remains reachable");
        expect(reload_pouch_contains({18,left?16.0F:-16.0F,-19},left),
            "visible front-lap center is a valid pickup for either hand");
        expect(reload_pouch_contains({28,left?28.0F:-28.0F,-11},left),
            "raised forward seated reach no longer requires side-of-chair access");
        expect(!reload_pouch_contains({18,left?-16.0F:16.0F,-19},left),
            "wrong side never draws a feed device");
        for (const ReloadPouchPoint point : {ReloadPouchPoint{18,16,-9},
             {18,16,0},{0,12,-11},{31,16,-19},{18,31,-19},{18,16,-43}})
            expect(!reload_pouch_contains({point.x,left?point.y:-point.y,point.z},left),
                "face, ordinary support hold, far reach and floor remain outside inventory");
    }
    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    const float inf = (std::numeric_limits<float>::infinity)();
    expect(!reload_pouch_contains({nan,16,-19},true) &&
        !reload_pouch_contains({18,inf,-19},true) &&
        !reload_pouch_contains({18,16,nan},true) &&
        !reload_pouch_contains({18,16,-19},true,inf), "nonfinite geometry fails closed");
}
void stable_upright_coordinates() {
    for (const float height : {1.2F,1.8F})
        for (const float yaw : {-150.0F,-90.0F,0.0F,90.0F,150.0F}) {
            ReloadPouchUprightFrame reference{};
            expect(make_reload_pouch_upright_frame({orientation(yaw,0,0),{2,height,3}},&reference),
                "valid seated/standing heading has an upright body frame");
            for (const bool left : {false,true}) {
                const auto center = reload_pouch_lap_center_tracking(reference,left);
                for (const float pitch : {-70.0F,-30.0F,0.0F,30.0F,70.0F})
                    for (const float roll : {-30.0F,0.0F,30.0F}) {
                        ReloadPouchUprightFrame frame{};
                        expect(make_reload_pouch_upright_frame(
                            {orientation(yaw,pitch,roll),{2,height,3}},&frame),
                            "looking down or tilting head retains a usable body frame");
                        ReloadPouchPoint local{};
                        expect(reload_pouch_point_from_tracking(frame,center,&local) &&
                            near(local.x,18) && near(local.y,left?16.0F:-16.0F) && near(local.z,-19) &&
                            reload_pouch_contains(local,left),
                            "same physical lap position remains inside while looking down, yawed or rolled");
                        const auto tilted_center = reload_pouch_lap_center_tracking(frame,left);
                        expect(near(center.x,tilted_center.x) && near(center.y,tilted_center.y) &&
                            near(center.z,tilted_center.z), "visual center and physical gate share stable tracking coordinates");
                    }
                for (const float pitch : {-90.001F,-90.0F,-89.999F,89.999F,90.0F,90.001F})
                    for (const float roll : {-30.0F,0.0F,30.0F}) {
                        ReloadPouchUprightFrame vertical{};
                        ReloadPouchPoint local{};
                        expect(make_reload_pouch_upright_frame(
                            {orientation(yaw,pitch,roll),{2,height,3}},&vertical,&reference) &&
                            reload_pouch_point_from_tracking(vertical,center,&local) &&
                            near(local.x,18) && near(local.y,left?16.0F:-16.0F) && near(local.z,-19) &&
                            reload_pouch_contains(local,left),
                            "straight-down/up search and tiny vertical-pose noise retain the last body heading and same pickup");
                        const auto vertical_center = reload_pouch_lap_center_tracking(vertical,left);
                        expect(near(center.x,vertical_center.x) && near(center.y,vertical_center.y) &&
                            near(center.z,vertical_center.z), "marker cannot vanish or jump at a vertical gaze");
                    }
                for (const float pitch : {-90.0F,90.0F}) {
                    ReloadPouchUprightFrame initial_vertical{};
                    ReloadPouchPoint local{};
                    expect(make_reload_pouch_upright_frame(
                        {orientation(yaw,pitch,0),{2,height,3}},&initial_vertical) &&
                        reload_pouch_point_from_tracking(initial_vertical,center,&local) &&
                        near(local.x,18) && near(local.y,left?16.0F:-16.0F) &&
                        reload_pouch_contains(local,left),
                        "initial straight-down/up pose uses a nonsingular yaw twist without needing an earlier render");
                }
            }
        }
    ReloadPouchUprightFrame frame{};
    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    expect(!make_reload_pouch_upright_frame({{0,0,0,0},{}},&frame) &&
        !make_reload_pouch_upright_frame({{nan,0,0,1},{}},&frame) &&
        !make_reload_pouch_upright_frame({{}, {0,nan,0}},&frame) &&
        !make_reload_pouch_upright_frame({{1,0,0,0},{}},&frame) &&
        !make_reload_pouch_upright_frame({},nullptr), "invalid or genuinely ambiguous upside-down pose cannot invent a pouch direction");
}
void visual_hip_anchor_preserves_reach() {
    for (const float height : {1.2F,1.8F})
        for (const float yaw : {-150.F,-90.F,0.F,90.F,150.F}) {
            ReloadPouchUprightFrame reference{};
            expect(make_reload_pouch_upright_frame({orientation(yaw,0,0),{2,height,3}},&reference),
                "hip marker has a valid standing/seated upright reference");
            for (const bool left : {false,true}) {
                const auto marker=reload_pouch_hip_marker_tracking(reference,left);
                ReloadPouchPoint local{};
                expect(reload_pouch_point_from_tracking(reference,marker,&local) &&
                    near(local.x,7.F) && near(local.y,left?10.F:-10.F) && near(local.z,-23.F),
                    "small marker sits at the true mirrored near-hip point, not the far front-lap center");
                const float side=left?local.y:-local.y;
                expect(local.x>=-18 && local.x<=24 && side>=2 && side<=24 &&
                    local.z>=-42 && local.z<=-14 && local.x>=6 && local.x<=30 &&
                    side>=2 && side<=30 && local.z>=-28 && local.z<=-10 &&
                    reload_pouch_contains(local,left),
                    "visual hip anchor itself lies inside both unchanged standing and seated grab boxes");
                for (const float pitch : {-90.F,-70.F,0.F,70.F,90.F}) {
                    ReloadPouchUprightFrame tilted{};
                    expect(make_reload_pouch_upright_frame(
                        {orientation(yaw,pitch,20),{2,height,3}},&tilted,&reference),
                        "tilted hip marker keeps a stable body frame");
                    const auto stable=reload_pouch_hip_marker_tracking(tilted,left);
                    expect(near(marker.x,stable.x) && near(marker.y,stable.y) && near(marker.z,stable.z),
                        "looking down/up cannot drag the hip marker with the headset");
                }
            }
        }
}
void discovery_is_edge_driven_and_bounded() {
    ReloadPouchCueState state{};
    ReloadPouchCueInput input{.eligible=true,.pose_valid=true,
        .point={35,16,-19},.action_sequence=1,.milliseconds=1000};
    auto sample = [&]() { return update_reload_pouch_cue(input,&state); };
    auto advance = [&]() { ++input.action_sequence; input.milliseconds += 16; };
    auto result = sample();
    expect(result.visible && !result.hovered && !result.pulse,
        "first sample baselines a free hand instead of inventing an entry");
    advance(); input.point = {18,16,-19}; result=sample();
    expect(result.visible && result.hovered && result.pulse,
        "deliberate entry emits one low-priority discovery pulse");
    for (int repeated=0; repeated<5; ++repeated) expect(!sample().pulse,
        "two eyes/repeated scene callbacks cannot replay the cue");
    for (int held=0; held<45; ++held) { advance(); expect(!sample().pulse,
        "remaining in the pouch never repeats feedback or performs a pickup"); }
    advance(); input.point={31,16,-19}; result=sample();
    expect(!result.hovered && !result.pulse, "leaving strict pickup edge removes highlight");
    advance(); input.point={29,16,-19}; expect(!sample().pulse,
        "boundary jitter inside hysteresis never counts as a new entry");
    advance(); input.point={34,16,-19}; sample();
    advance(); input.point={18,16,-19}; expect(sample().pulse,
        "a real exit and later reentry emits a new bounded cue");
    advance(); input.point={34,16,-19}; sample();
    advance(); input.point={18,16,-19}; expect(!sample().pulse,
        "rapid repeated crossings are cooldown-limited");
    input.hand_busy=true; advance(); result=sample();
    expect(!result.visible && !result.hovered && !result.pulse,
        "support grip, active action, held ammo or grenade suppresses discovery");
    input.hand_busy=false; advance(); expect(!sample().pulse,
        "releasing a busy hand inside the pouch does not fabricate an entry");
    for (int reason=0; reason<3; ++reason) {
        if (reason==0) input.eligible=false;
        if (reason==1) input.pose_valid=false;
        if (reason==2) input.action_sequence=0;
        advance(); if (reason==2) input.action_sequence=0;
        result=sample();
        expect(!result.visible && !result.pulse,
            "no inventory/menu/tracking/sequence context fails closed");
        input.eligible=input.pose_valid=true; input.action_sequence=1000+reason;
        advance(); expect(!sample().pulse,"context recovery never replays proximity");
    }
    advance(); input.point={34,16,-19}; sample();
    input.milliseconds+=150; ++input.action_sequence; input.point={18,16,-19};
    expect(!sample().pulse,"a stale/hitched sample baselines instead of dispatching old feedback");
    input.left_hand=false; input.point.y=-16; advance();
    expect(!sample().pulse,"changing feed hand cannot synthesize a proximity edge");
}
} // namespace
int main() {
    legacy_and_seated_geometry(); stable_upright_coordinates(); visual_hip_anchor_preserves_reach();
    discovery_is_edge_driven_and_bounded();
    std::cout << "Standing/seated pouch geometry, yaw stability and bounded discovery checks passed\n";
}
