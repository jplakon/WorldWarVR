// SPDX-License-Identifier: GPL-3.0-only
#include "aircraft_control_logic.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using namespace wawvr::mod;
using wawvr::xr::Basis3f;
using wawvr::xr::Vec2f;
using wawvr::xr::Vec3f;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(const float a, const float b, const float tolerance = 1.0e-4F) {
    return std::abs(a-b) <= tolerance;
}

bool near_vector(const Vec3f& a, const Vec3f& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

void test_context_guard() {
    AircraftContextData valid{true, 0, 0xA4002U, 18, 13, 2, 1, 0};
    expect(aircraft_controller_context_valid(valid),
           "live PBY front passenger owns controller route without vehicle owner handle");
    for (int seat = 1; seat <= 4; ++seat) {
        valid.seat = seat;
        expect(aircraft_controller_context_valid(valid),
               "all four native PBY gun stations are supported");
    }
    auto invalid = valid;
    invalid.active_connection = false;
    expect(!aircraft_controller_context_valid(invalid), "menu is not a flying aircraft");
    invalid = valid; invalid.local_client_number = 1;
    expect(!aircraft_controller_context_valid(invalid), "nonlocal client rejected");
    invalid = valid; invalid.passenger_entity = 1;
    expect(!aircraft_controller_context_valid(invalid),
           "passenger is raw entity number zero, not EntHandle one");
    invalid = valid; invalid.player_flags &= ~0x4000U;
    expect(!aircraft_controller_context_valid(invalid), "dismounted player rejected");
    invalid = valid; invalid.player_flags |= 0x10000U;
    expect(!aircraft_controller_context_valid(invalid), "authored rest/transition rejected");
    expect(aircraft_passenger_context_present(invalid),
           "authored aircraft transition still blocks handheld snap/stance fallthrough");
    invalid = valid; invalid.entity_type = 11;
    expect(!aircraft_controller_context_valid(invalid), "ordinary mounted gun route unchanged");
    invalid = valid; invalid.vehicle_type = 1;
    expect(!aircraft_controller_context_valid(invalid), "tracked tank route unchanged");
    expect(!aircraft_passenger_context_present(invalid), "tank is not an aircraft transition");
    for (const int seat : {-1, 0, 5, 100}) {
        invalid = valid; invalid.seat = seat;
        expect(!aircraft_controller_context_valid(invalid), "invalid or driver seat rejected");
    }
    for (const int entity : {-1, 0, 1023, 2047}) {
        invalid = valid; invalid.vehicle_entity = entity;
        expect(!aircraft_controller_context_valid(invalid), "invalid vehicle number rejected");
    }
}

void test_rest_axes_and_controller_signs() {
    const Vec2f rests[]{{0,0}, {0,90}, {0,270}, {80.9994F,180}};
    const Vec3f expected_forward[]{{1,0,0}, {0,1,0}, {0,-1,0}, {-1,0,0}};
    for (std::size_t index = 0; index < 4; ++index) {
        Basis3f full{}, level{};
        expect(compose_aircraft_seat_basis({}, rests[index], &full) &&
                   level_aircraft_seat_basis(full, &level), "compose and level each station");
        expect(near_vector(level.forward, expected_forward[index]) &&
                   near_vector(level.up, {0,0,1}),
               "station heading uses its authored rest; stereo base remains level");
        Vec2f local{};
        expect(aircraft_world_direction_to_seat_view(full.forward, {}, rests[index], &local) &&
                   near(local.x, 0) && near(local.y, 0),
               "authored full rest maps to zero native seat-local view");
    }
    Vec3f world{};
    Vec2f local{};
    expect(aircraft_controller_world_direction({}, {1,-1,0}, &world) &&
               aircraft_world_direction_to_seat_view(world, {}, {0,0}, &local) &&
               near(local.x, 0) && near(local.y, -45),
           "right-hand right direction turns the native gun right");
    expect(aircraft_controller_world_direction({}, {1,0,1}, &world) &&
               aircraft_world_direction_to_seat_view(world, {}, {0,0}, &local) &&
               near(local.x, -45) && near(local.y, 0),
           "up direction raises the gun with negative native pitch");
    expect(aircraft_world_direction_to_seat_view({1,0,10}, {}, {0,0}, &local) &&
               local.x < -80,
           "helper does not impose a front-gun 30-degree clamp on other seats");
}

void test_bank_inverse_and_no_feedback() {
    Basis3f hull{}, full{}, stable{};
    expect(aircraft_basis_from_angles({20,45,35}, &hull), "build full banked hull basis");
    expect(compose_aircraft_seat_basis(hull, {0,90}, &full) &&
               near_vector(full.forward, hull.left),
           "banked left gun neutral is hull-left, not simple summed Euler angles");
    expect(build_aircraft_comfort_seat_basis(hull,{0,90}, &stable) &&
               stable.forward.z == 0 && near_vector(stable.up, {0,0,1}),
           "controller and stereo share gravity-leveled banked station heading");
    Vec3f world{};
    Vec2f local{};
    expect(aircraft_world_direction_to_seat_view(full.forward, hull, {0,90}, &local) &&
               near(local.x, 0) && near(local.y, 0),
           "inverse full hull removes bank and authored station rest exactly once");
    expect(aircraft_controller_world_direction(stable, {1,-0.3F,0.2F}, &world) &&
               aircraft_world_direction_to_seat_view(world, hull, {0,90}, &local),
           "controller target passes from stable camera frame to native seat frame");
    const Vec3f first_world = world;
    const Vec2f first_local = local;
    for (int repeat = 0; repeat < 1000; ++repeat) {
        expect(aircraft_controller_world_direction(stable, {1,-0.3F,0.2F}, &world) &&
                   aircraft_world_direction_to_seat_view(world, hull, {0,90}, &local) &&
                   near_vector(world, first_world) &&
                   near(local.x, first_local.x) && near(local.y, first_local.y),
               "repeated native commands cannot accumulate or feed back controller aim");
    }
    Basis3f reconstructed_local{}, reconstructed_world{};
    expect(aircraft_basis_from_angles({local.x, local.y+90,0}, &reconstructed_local) &&
               compose_aircraft_seat_basis(hull, {local.x, local.y+90}, &reconstructed_world) &&
               near_vector(reconstructed_world.forward, first_world),
           "native rest re-addition and hull transform recover the exact rendered controller ray");
}

void test_rear_camera_has_no_authored_pitch_pole_flip() {
    for (const float pitch : {0.0F,8.0F,8.99F,9.01F,10.0F,20.0F}) {
        Basis3f hull{}, stable{};
        expect(aircraft_basis_from_angles({pitch,0,15},&hull) &&
                   build_aircraft_comfort_seat_basis(hull,{80.9994F,180},&stable) &&
                   near_vector(stable.forward,{-1,0,0}) &&
                   near_vector(stable.up,{0,0,1}),
               "rear comfort camera never flips when hull pitch crosses authored rest pole");
    }
}

void test_native_command_serialization() {
    AircraftNativeViewCommand output{};
    expect(encode_aircraft_native_view_command({-15,30}, {5,-3}, &output) &&
               near(output.client_pitch_yaw.x,-20) && near(output.client_pitch_yaw.y,33),
           "client and command angles remove native delta exactly once");
    expect(output.command_pitch_yaw[0] >= 0 && output.command_pitch_yaw[0] <= 65535 &&
               output.command_pitch_yaw[1] >= 0 && output.command_pitch_yaw[1] <= 65535,
           "negative pitch and positive yaw serialize to wrapped 16-bit values");
    const auto reconstruct = [](const std::int32_t value, const float delta) {
        return std::remainder(static_cast<float>(value)*(360.0F/65536.0F)+delta, 360.0F);
    };
    expect(near(reconstruct(output.command_pitch_yaw[0],5),-15,0.003F) &&
               near(reconstruct(output.command_pitch_yaw[1],-3),30,0.003F),
           "native short-plus-delta reconstruction matches target within quantization");
    expect(encode_aircraft_native_view_command({-735,750}, {725,-723}, &output) &&
               near(output.client_pitch_yaw.x,-20) && near(output.client_pitch_yaw.y,33),
           "multi-turn angle representations do not alter aiming");
}

void test_native_arc_preclamp_and_wrap() {
    Vec2f result{};
    constexpr float margin = 2.0F*(360.0F/65536.0F);
    expect(clamp_aircraft_native_view({-25,15},{-25,0},{45,60},&result) &&
               near(result.x,-25) && near(result.y,15),
           "inside-arc native targets remain unchanged");
    expect(clamp_aircraft_native_view({-81,0},{-25,0},{45,60},&result) &&
               near(result.x,-70+margin) && near(result.y,0),
           "level rear-gun target stops inside the native arc instead of winding delta");
    expect(clamp_aircraft_native_view({80,-95},{0,0},{30,90},&result) &&
               near(result.x,30-margin) && near(result.y,-90+margin),
           "front gun pitch and yaw both retain their own limits");
    expect(clamp_aircraft_native_view({0,-160},{0,170},{180,25},&result) &&
               near(result.x,0) && near(result.y,-165-margin),
           "yaw arc crossing the 180-degree seam clamps around authored center");
    expect(clamp_aircraft_native_view({1080,-535},{720,530},{180,25},&result) &&
               near(result.x,0) && near(result.y,-175),
           "equivalent multi-turn representations preserve an in-arc direction");
    expect(clamp_aircraft_native_view({25,80},{-12,170},{0,0},&result) &&
               near(result.x,-12) && near(result.y,170),
           "zero native range remains fixed at its exact center");
    expect(clamp_aircraft_native_view({75,210},{0,0},{180,360},&result) &&
               near(result.x,75) && near(result.y,-150),
           "unlimited native axes do not receive an artificial range limit");
}

void test_preclamped_serialization_cannot_cross_native_arcs() {
    const Vec2f bases[]{{0,0},{-43,26.5F},{-43,-26.5F},{-25,0}};
    const Vec2f ranges[]{{30,90},{67,66.5F},{67,66.5F},{45,60}};
    const Vec2f deltas[]{{0,0},{-3.087F,-1.461F},{177.3022F,1207.542F},
                        {-422.6996F,-112.4597F}};
    for (std::size_t seat=0;seat<4;++seat) {
        for (const auto delta:deltas) {
            for (int pitch=-180;pitch<=180;pitch+=15) {
                for (int yaw=-180;yaw<=180;yaw+=15) {
                    Vec2f clamped{};
                    AircraftNativeViewCommand command{};
                    expect(clamp_aircraft_native_view(
                               {static_cast<float>(pitch),static_cast<float>(yaw)},
                               bases[seat],ranges[seat],&clamped) &&
                               encode_aircraft_native_view_command(clamped,delta,&command),
                           "all sampled station targets safely clamp and encode");
                    const float decoded_pitch=std::remainder(
                        command.command_pitch_yaw[0]*(360.0F/65536.0F)+delta.x,360.0F);
                    const float decoded_yaw=std::remainder(
                        command.command_pitch_yaw[1]*(360.0F/65536.0F)+delta.y,360.0F);
                    expect(std::abs(std::remainder(decoded_pitch-bases[seat].x,360.0F))
                                   <ranges[seat].x &&
                               std::abs(std::remainder(decoded_yaw-bases[seat].y,360.0F))
                                   <ranges[seat].y,
                           "angle-short rounding stays strictly inside each native arc");
                }
            }
        }
    }
    const Vec2f stable_delta{-3.087F,-1.461F};
    for (int repeat=0;repeat<1000;++repeat) {
        Vec2f clamped{};
        AircraftNativeViewCommand command{};
        expect(clamp_aircraft_native_view({-81,-140},{-25,0},{45,60},&clamped) &&
                   encode_aircraft_native_view_command(clamped,stable_delta,&command),
               "held unreachable rear target repeatedly uses unchanged native delta");
        const float pitch=std::remainder(command.command_pitch_yaw[0]*(360.0F/65536.0F)+stable_delta.x,360.0F);
        const float yaw=std::remainder(command.command_pitch_yaw[1]*(360.0F/65536.0F)+stable_delta.y,360.0F);
        expect(pitch>-70 && pitch<20 && yaw>-60 && yaw<60,
               "held target never asks native range limiter to rewrite delta angles");
    }
}

void test_seat_origin_tracks_hull_not_controller_orbit() {
    Basis3f hull{};
    expect(aircraft_basis_from_angles({20,35,-15},&hull), "initial banked hull valid");
    const Vec3f hull_origin{100,200,300};
    const Vec3f authored_offset{30,-12,45};
    Vec3f stock_origin{}, captured_offset{};
    expect(aircraft_hull_local_origin_to_world(authored_offset,hull_origin,hull,&stock_origin) &&
               aircraft_world_origin_to_hull_local(stock_origin,hull_origin,hull,&captured_offset) &&
               near_vector(authored_offset,captured_offset),
           "post-native camera origin captures once in full hull space");
    Basis3f moved_hull{};
    expect(aircraft_basis_from_angles({0,90,0},&moved_hull), "moved hull valid");
    Vec3f moved_origin{};
    expect(aircraft_hull_local_origin_to_world(captured_offset,{500,600,700},moved_hull,&moved_origin) &&
               near_vector(moved_origin,{512,630,745}),
           "latched camera offset follows aircraft translation and rotation without rereading orbiting view");
}

void test_invalid_inputs_leave_outputs_unchanged() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Basis3f invalid{}; invalid.up.z = -1;
    Basis3f basis{};
    Vec3f world{7,8,9};
    Vec2f angles{7,8};
    expect(!aircraft_basis_from_angles({nan,0,0}, &basis) &&
               !compose_aircraft_seat_basis(invalid,{0,0},&basis) &&
               !aircraft_controller_world_direction({}, {0,0,0}, &world) &&
               !aircraft_controller_world_direction(invalid, {1,0,0}, &world) &&
               near_vector(world,{7,8,9}), "invalid basis/pose fails closed without changing output");
    expect(!aircraft_world_direction_to_seat_view({nan,0,0},{},{0,0},&angles) &&
               near(angles.x,7) && near(angles.y,8), "invalid inverse target leaves output unchanged");
    AircraftNativeViewCommand command{{7,8},{123,456}};
    expect(!clamp_aircraft_native_view({nan,0},{0,0},{30,90},&angles) &&
               !clamp_aircraft_native_view({0,0},{nan,0},{30,90},&angles) &&
               !clamp_aircraft_native_view({0,0},{0,0},{-1,90},&angles) &&
               !clamp_aircraft_native_view({0,0},{0,0},{30,nan},&angles) &&
               near(angles.x,7) && near(angles.y,8),
           "invalid native arc data fails without changing output");
    expect(!encode_aircraft_native_view_command({nan,0},{0,0},&command) &&
               command.client_pitch_yaw.x == 7 && command.command_pitch_yaw[0] == 123,
           "invalid native angle never creates a partial command");
    expect(!aircraft_basis_from_angles({},nullptr) &&
               !compose_aircraft_seat_basis({},{},nullptr) &&
               !level_aircraft_seat_basis({},nullptr) &&
               !aircraft_controller_world_direction({},{1,0,0},nullptr) &&
               !aircraft_world_direction_to_seat_view({1,0,0},{},{},nullptr) &&
               !clamp_aircraft_native_view({},{},{},nullptr) &&
               !encode_aircraft_native_view_command({},{},nullptr), "null outputs rejected");
}

}  // namespace

int main() {
    test_context_guard();
    test_rest_axes_and_controller_signs();
    test_bank_inverse_and_no_feedback();
    test_rear_camera_has_no_authored_pitch_pole_flip();
    test_native_command_serialization();
    test_native_arc_preclamp_and_wrap();
    test_preclamped_serialization_cannot_cross_native_arcs();
    test_seat_origin_tracks_hull_not_controller_orbit();
    test_invalid_inputs_leave_outputs_unchanged();
    if (failures != 0) return 1;
    std::cout << "Aircraft controller logic tests passed\n";
    return 0;
}
