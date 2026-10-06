// SPDX-License-Identifier: GPL-3.0-only
#include "aircraft_control_runtime.hpp"
#include "aircraft_control_logic.hpp"
#include "controller_state.hpp"
#include "input_mapping.hpp"
#include "stereo_diagnostics.hpp"
#include "vehicle_snapshot_reader.hpp"

#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

namespace wawvr::mod {
namespace {
std::uintptr_t g_ps{}, g_entities{}, g_info{}, g_centities{}, g_client_angles{}, g_connection{};
SRWLOCK g_camera_lock = SRWLOCK_INIT;
struct Context {
    AircraftContextData data{};
    std::int32_t command_time{}, info_index{};
    wawvr::xr::Vec3f hull_origin{};
    wawvr::xr::Basis3f hull{}, camera_axis{};
    wawvr::xr::Vec2f rest{}, delta{}, clamp_base{}, clamp_range{};
};
struct CameraLatch {
    bool valid{};
    std::int32_t entity{-1}, seat{-1}, info{-1}, command_time{};
    wawvr::xr::Vec3f hull_local_origin{};
} g_camera;
std::int32_t g_logged_seat = -1;
std::uint64_t g_last_diagnostic = 0;
std::uint32_t g_diagnostic_count = 0;
std::atomic<std::uint32_t> g_camera_seed_diagnostic_count{0};
enum class DiagnosticGate { none, context, native_lock, camera, gameplay, frame,
    stale_frame, aim, conversion, allowed };
DiagnosticGate g_diagnostic_gate = DiagnosticGate::none;
std::int32_t g_diagnostic_entity = -1, g_diagnostic_seat = -1,
    g_diagnostic_info = -1, g_diagnostic_command_time = -1;

bool diagnostics_enabled() noexcept {
    wchar_t value[4]{};
    return GetEnvironmentVariableW(L"WAWVR_AIRCRAFT_DIAGNOSTICS",value,4)>0 && value[0]==L'1';
}
void reset_diagnostic_epoch(const Context& c) noexcept {
    if (g_diagnostic_entity!=c.data.vehicle_entity || g_diagnostic_seat!=c.data.seat ||
        g_diagnostic_info!=c.info_index || c.command_time<g_diagnostic_command_time) {
        g_diagnostic_count=0;
        g_last_diagnostic=0;
        g_diagnostic_gate=DiagnosticGate::none;
    }
    g_diagnostic_entity=c.data.vehicle_entity;
    g_diagnostic_seat=c.data.seat;
    g_diagnostic_info=c.info_index;
    g_diagnostic_command_time=c.command_time;
}
void diagnostic_gate(const DiagnosticGate gate, const char* reason,
                     const Context* c=nullptr,
                     const ControllerFrameSnapshot* snapshot=nullptr) noexcept {
    if (g_diagnostic_gate==gate) return;
    g_diagnostic_gate=gate;
    if (g_diagnostic_count>=120 || !diagnostics_enabled()) return;
    ++g_diagnostic_count;
    const auto now=GetTickCount64();
    const bool current=snapshot && controller_frame_is_current(*snapshot,now);
    stereo_diagnostic_log(
        "AircraftDiag gate=%s entity=%d seat=%d commandTime=%d flags=0x%X framePresent=%d frameCurrent=%d focused=%d frameId=%llu actionSequence=%llu publicationAgeMs=%llu",
        reason,c?c->data.vehicle_entity:-1,c?c->data.seat:-1,c?c->command_time:-1,
        c?c->data.player_flags:0U,snapshot?1:0,current?1:0,
        snapshot&&snapshot->frame.actions.focused?1:0,
        static_cast<unsigned long long>(snapshot?snapshot->frame.frame_id:0),
        static_cast<unsigned long long>(snapshot?snapshot->frame.actions.sequence:0),
        static_cast<unsigned long long>(snapshot&&now>=snapshot->publication_milliseconds
            ?now-snapshot->publication_milliseconds:0));
}

bool range(const std::uintptr_t address, const std::size_t size, bool write=false) noexcept {
    if (address < 0x10000 || size == 0 || address > std::numeric_limits<std::uintptr_t>::max()-size) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &m, sizeof(m)) != sizeof(m) ||
        m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD|PAGE_NOACCESS))) return false;
    const auto p=m.Protect&0xFFU;
    const bool writable=p==PAGE_READWRITE || p==PAGE_WRITECOPY || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
    const bool readable=writable || p==PAGE_READONLY || p==PAGE_EXECUTE_READ;
    const auto begin=reinterpret_cast<std::uintptr_t>(m.BaseAddress);
    return readable && (!write || writable) && address>=begin && address-begin<=m.RegionSize && size<=m.RegionSize-(address-begin);
}
bool finite(const wawvr::xr::Vec3f& v) noexcept {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool context(Context* out) noexcept {
    if (!out || !g_ps || !g_entities || !g_info || !g_centities) return false;
    // No engine calls occur in this traversal. Reuse a containing readable
    // region while copying its fields, then discard all facts on return.
    VehicleSnapshotReader memory;
    Context c{}; std::int32_t connection{}, number{}, predicted_vehicle_type{}; std::uint32_t vehicle{};
    std::int16_t info_index{}, vehicle_type{}, client_info{}; std::uint8_t client_type{}, gun{};
    if (!memory.read(g_connection,&connection) || connection!=kT4SpActiveConnectionState ||
        // Most campaign scenes are not aircraft. Test this identity before
        // gathering the remaining camera fields on every controller/eye call.
        !memory.read(g_ps+0x844,&predicted_vehicle_type) || predicted_vehicle_type!=2 ||
        !memory.read(g_ps+0xCC,&c.data.player_flags) || (c.data.player_flags&0x4000U)==0 ||
        !memory.read(g_ps,&c.command_time) ||
        !memory.read(g_ps+0xF8,&c.data.local_client_number) ||
        !memory.read(g_ps+0x83C,&c.data.vehicle_entity) || !memory.read(g_ps+0x840,&c.data.seat) ||
        c.data.vehicle_entity<=0 || c.data.vehicle_entity>=1023 || c.data.seat<1 || c.data.seat>4) return false;
    c.data.active_connection=true;
    const auto ent=g_entities+static_cast<std::uintptr_t>(c.data.vehicle_entity)*0x378;
    if (!memory.read(ent,&number) || number!=c.data.vehicle_entity || !memory.read(ent+4,&c.data.entity_type) ||
        c.data.entity_type!=13 || !memory.read(ent+0x18C,&vehicle) || !vehicle ||
        !memory.read(vehicle+0x1CC,&info_index) || info_index<0 || info_index>=64) return false;
    c.info_index=info_index;
    const auto info=g_info+static_cast<std::uintptr_t>(info_index)*0x8FC;
    const auto index=static_cast<std::uintptr_t>(c.data.seat-1);
    std::array<char,64> name{};
    if (!memory.read(info,&name) || std::find(name.begin(),name.end(),'\0')==name.end() ||
        std::strcmp(name.data(),"pby_blackcat")!=0 ||
        !memory.read(info+0x40,&vehicle_type) || !memory.read(vehicle+0x5D8+index*4,&c.data.passenger_entity) ||
        !memory.read(info+0x204+index,&gun) || gun==0 || gun>=128) return false;
    c.data.vehicle_type=vehicle_type;
    if (!aircraft_passenger_context_present(c.data)) return false;
    // The native camera and visible aircraft use this interpolated client pose,
    // not the server physics transform (which can be a frame ahead).
    const auto cent=g_centities+static_cast<std::uintptr_t>(c.data.vehicle_entity)*0x2D4;
    wawvr::xr::Vec3f angles{};
    if (!memory.read(cent+2,&client_type) || client_type!=13 || !memory.read(cent+0xD0,&number) || number!=c.data.vehicle_entity ||
        !memory.read(cent+0xD4,&number) || number!=13 || !memory.read(cent+0x1C0,&client_info) || client_info!=info_index ||
        !memory.read(cent+0x24,&c.hull_origin) || !finite(c.hull_origin) || !memory.read(cent+0x30,&angles) ||
        !memory.read(info+0x20C+index*8,&c.rest) || !memory.read(g_ps+0x7C,&c.delta) ||
        !memory.read(g_ps+0x144,&c.clamp_base) || !memory.read(g_ps+0x14C,&c.clamp_range) ||
        !aircraft_basis_from_angles(angles,&c.hull) || !build_aircraft_comfort_seat_basis(c.hull,c.rest,&c.camera_axis)) return false;
    *out=c; return true;
}
bool latch_matches(const Context& c) noexcept {
    return g_camera.valid && g_camera.entity==c.data.vehicle_entity && g_camera.seat==c.data.seat &&
        g_camera.info==c.info_index && c.command_time>=g_camera.command_time;
}
void clear_camera() noexcept {AcquireSRWLockExclusive(&g_camera_lock);g_camera={};ReleaseSRWLockExclusive(&g_camera_lock);}
bool bool_held(const wawvr::xr::BoolActionState& action) noexcept {return action.active&&action.current;}
bool trigger_held(const wawvr::xr::HandActionState& hand) noexcept {
    return bool_held(hand.trigger_click) || (hand.trigger.active && std::isfinite(hand.trigger.current) && hand.trigger.current>=kControllerButtonThreshold);
}
} // namespace

void bind_aircraft_controls(const wawvr::t4::ValidatedBindings* bindings) noexcept {
    g_ps=g_entities=g_info=g_centities=g_client_angles=g_connection=0;
    clear_camera(); g_logged_seat=-1; g_last_diagnostic=0; g_diagnostic_count=0;
    g_camera_seed_diagnostic_count.store(0,std::memory_order_relaxed);
    g_diagnostic_gate=DiagnosticGate::none;
    g_diagnostic_entity=g_diagnostic_seat=g_diagnostic_info=g_diagnostic_command_time=-1;
    if (!bindings) return;
    g_ps=bindings->data_address(wawvr::t4::DataSymbolId::predicted_player_state,0x848).value_or(0);
    g_entities=bindings->data_address(wawvr::t4::DataSymbolId::local_player_entity,0x378).value_or(0);
    g_info=bindings->module().address(0x04380D80,0x8FC).value_or(0);
    g_centities=bindings->module().address(0x031D39F0,0x2D4).value_or(0);
    g_client_angles=bindings->module().address(0x02C7D6D0,12).value_or(0);
    g_connection=bindings->module().address(0x02C5842C,4).value_or(0);
}
bool controller_aircraft_controls_active() noexcept {Context c{};return context(&c);}

bool read_aircraft_camera_base(const wawvr::xr::Vec3f& stock_origin,
                              wawvr::xr::Vec3f* origin,wawvr::xr::Basis3f* axis) noexcept {
    Context c{};
    if (!origin || !axis || !context(&c) || !aircraft_controller_context_valid(c.data) || !finite(stock_origin)) return false;
    AcquireSRWLockExclusive(&g_camera_lock);
    const CameraLatch previous=g_camera;
    bool seeded=false;
    if (!latch_matches(c)) {
        wawvr::xr::Vec3f offset{};
        if (!aircraft_world_origin_to_hull_local(stock_origin,c.hull_origin,c.hull,&offset) ||
            offset.x*offset.x+offset.y*offset.y+offset.z*offset.z>1024.0F*1024.0F) {
            g_camera={}; ReleaseSRWLockExclusive(&g_camera_lock); return false;
        }
        g_camera={true,c.data.vehicle_entity,c.data.seat,c.info_index,c.command_time,offset};
        seeded=true;
    }
    g_camera.command_time=c.command_time;
    const auto local_origin=g_camera.hull_local_origin;
    const bool valid=aircraft_hull_local_origin_to_world(g_camera.hull_local_origin,c.hull_origin,c.hull,origin);
    *axis=c.camera_axis;
    ReleaseSRWLockExclusive(&g_camera_lock);
    if (seeded && diagnostics_enabled()) {
        const auto number=g_camera_seed_diagnostic_count.fetch_add(1,std::memory_order_relaxed);
        if (number<64) {
            const char* reason=!previous.valid ? "empty_latch" :
                previous.entity!=c.data.vehicle_entity || previous.seat!=c.data.seat || previous.info!=c.info_index
                    ? "station_changed" : "command_time_rewound";
            stereo_diagnostic_log(
                "AircraftDiag camera_seed=%u reason=%s oldEntity=%d oldSeat=%d entity=%d seat=%d oldCommandTime=%d commandTime=%d hullLocalOrigin=(%.4f,%.4f,%.4f)",
                number+1,reason,previous.entity,previous.seat,c.data.vehicle_entity,c.data.seat,
                previous.command_time,c.command_time,local_origin.x,local_origin.y,local_origin.z);
        }
    }
    return valid;
}

bool apply_aircraft_controller_command(wawvr::t4::UsercmdSp& command,const bool gameplay_allowed) noexcept {
    Context c{};
    if (!context(&c)) {diagnostic_gate(DiagnosticGate::context,"context_unavailable");clear_camera();g_logged_seat=-1;return false;}
    reset_diagnostic_epoch(c);
    if (!aircraft_controller_context_valid(c.data)) {diagnostic_gate(DiagnosticGate::native_lock,"native_transition_lock",&c);clear_camera();return true;}
    // Wait for this station's own post-native-camera capture, not the previous
    // station's stale stock refdef. No frame accumulates controller yaw.
    AcquireSRWLockShared(&g_camera_lock);
    const bool camera_ready=latch_matches(c);
    ReleaseSRWLockShared(&g_camera_lock);
    ControllerFrameSnapshot snapshot{};
    const auto now=GetTickCount64();
    if (!camera_ready) {diagnostic_gate(DiagnosticGate::camera,"waiting_camera_latch",&c);return true;}
    if (!gameplay_allowed) {diagnostic_gate(DiagnosticGate::gameplay,"gameplay_input_not_owned",&c);return true;}
    if (!read_controller_frame(&snapshot)) {diagnostic_gate(DiagnosticGate::frame,"controller_frame_unavailable",&c);return true;}
    if (!controller_frame_is_current(snapshot,now)) {diagnostic_gate(DiagnosticGate::stale_frame,"controller_frame_not_current",&c,&snapshot);return true;}
    float pitch{},yaw{};
    if (!controller_aim_degrees(snapshot,c.camera_axis,&pitch,&yaw)) {diagnostic_gate(DiagnosticGate::aim,"right_controller_aim_unavailable",&c,&snapshot);return true;}
    wawvr::xr::Basis3f world{}; wawvr::xr::Vec2f native_view{}, clamped_view{}; AircraftNativeViewCommand encoded{};
    if (!aircraft_basis_from_angles({pitch,yaw,0.0F},&world) ||
        !aircraft_world_direction_to_seat_view(world.forward,c.hull,c.rest,&native_view) ||
        !clamp_aircraft_native_view(native_view,c.clamp_base,c.clamp_range,&clamped_view) ||
        !encode_aircraft_native_view_command(clamped_view,c.delta,&encoded) || !range(g_client_angles,8,true)) {diagnostic_gate(DiagnosticGate::conversion,"native_angle_conversion_unavailable",&c,&snapshot);return true;}
    diagnostic_gate(DiagnosticGate::allowed,"aim_applied",&c,&snapshot);
    std::memcpy(reinterpret_cast<void*>(g_client_angles),&encoded.client_pitch_yaw,8);
    command.view_angles[0]=encoded.command_pitch_yaw[0];
    command.view_angles[1]=encoded.command_pitch_yaw[1];
    const auto& left=snapshot.frame.actions.hands[static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
    const auto& right=snapshot.frame.actions.hands[static_cast<std::uint32_t>(wawvr::xr::Hand::Right)];
    if (trigger_held(right)) wawvr::t4::add_button(command,wawvr::t4::UsercmdButton::attack);
    if (trigger_held(left)) wawvr::t4::add_button(command,wawvr::t4::UsercmdButton::frag_grenade);
    if (bool_held(left.primary)) wawvr::t4::add_button(command,wawvr::t4::UsercmdButton::use);
    if (g_logged_seat!=c.data.seat) {
        stereo_diagnostic_log("AircraftDiag controller gunner active entity=%d seat=%d: native seat-local aim, trigger=fire, head independent, snap/handheld bypass",c.data.vehicle_entity,c.data.seat);
        g_logged_seat=c.data.seat;
    }
    if (g_diagnostic_count<120 && now-g_last_diagnostic>=500 && diagnostics_enabled()) {
        g_last_diagnostic=now;++g_diagnostic_count;
        stereo_diagnostic_log("AircraftDiag sample seat=%d world=(%.3f,%.3f) native=(%.3f,%.3f) clamped=(%.3f,%.3f) delta=(%.3f,%.3f) CL=(%.3f,%.3f) trigger=%d",c.data.seat,pitch,yaw,native_view.x,native_view.y,clamped_view.x,clamped_view.y,c.delta.x,c.delta.y,encoded.client_pitch_yaw.x,encoded.client_pitch_yaw.y,trigger_held(right)?1:0);
    }
    return true;
}
} // namespace wawvr::mod
