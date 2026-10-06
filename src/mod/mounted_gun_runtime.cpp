#include "mounted_gun_runtime.hpp"

#include "firing_haptics.hpp"
#include "input_mapping.hpp"
#include "mounted_gun_logic.hpp"
#include "peer_thread_quiescence.hpp"
#include "stereo_diagnostics.hpp"
#include "t4/hook_api.hpp"
#include "t4_layout_selector.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>

namespace wawvr::mod {
namespace {

constexpr std::size_t kCallSize = 5;
constexpr std::size_t kGentitySpan = 0x194;
constexpr std::size_t kPlayerStateSpan = 0x840;
constexpr std::size_t kTurretInfoSpan = 0x44;
constexpr std::size_t kGentityClientOffset = 0x180;
constexpr std::size_t kGentityTurretOffset = 0x190;
constexpr std::size_t kGentityWeaponOffset = 0xE0;
constexpr std::size_t kGentityGunAnglesOffset = 0x54;
constexpr std::size_t kGentityCurrentAnglesOffset = 0x16C;
constexpr std::size_t kPlayerViewAnglesOffset = 0x124;
constexpr std::size_t kPoseTypeOffset = 0x02;
constexpr std::size_t kPoseTurretViewAnglesOffset = 0x48;
constexpr std::size_t kPoseTurretPlayerUsingOffset = 0x54;
constexpr std::size_t kWeaponOverlayInterfaceOffset = 0x528;
constexpr std::int32_t kTurretScopeOverlayInterface = 2;
constexpr std::int32_t kMountedEntityType = 0x0B;

[[nodiscard]] bool page_protection_is(
    const void* const address, const DWORD expected) noexcept {
    MEMORY_BASIC_INFORMATION information{};
    return VirtualQuery(address, &information, sizeof(information)) ==
            sizeof(information) &&
        information.Protect == expected;
}

[[nodiscard]] bool query_page_protection(
    const void* const address, DWORD* const protection) noexcept {
    if (protection == nullptr) return false;
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(address, &information, sizeof(information)) !=
        sizeof(information)) {
        return false;
    }
    *protection = information.Protect;
    return true;
}

std::atomic<bool> g_installed{false};
std::atomic<bool> g_enabled{false};
std::uintptr_t g_server_original = 0;
std::uintptr_t g_fire_original = 0;
std::uintptr_t g_client_original = 0;
std::uintptr_t g_local_gentity = 0;
std::uintptr_t g_weapon_definitions = 0;
std::uintptr_t g_weapon_count = 0;
std::uintptr_t g_predicted_player_state = 0;
SRWLOCK g_world_aim_lock = SRWLOCK_INIT;
MountedGunAimPublication g_world_aim{};
std::atomic<bool> g_logged_first_server_aim{false};
std::atomic<bool> g_logged_first_fire_aim{false};
std::atomic<bool> g_logged_first_visual_aim{false};

struct ServerSubstitution {
    bool active{};
    std::uintptr_t mounted_entity{};
    float* view_angles{};
    MountedGunAngles saved{};
};

struct FireSubstitution {
    bool active{};
    float* view_angles{};
    std::array<float, 3> saved{};
};

struct VisualSubstitution {
    bool active{};
    std::uint32_t* view_angles_slot{};
    std::uint32_t saved_pointer{};
    std::array<float, 2> controller_angles{};
};

thread_local ServerSubstitution g_server_substitution{};
thread_local FireSubstitution g_fire_substitution{};
thread_local bool g_local_mounted_shot_pending{};
thread_local VisualSubstitution g_visual_substitution{};

[[nodiscard]] bool accessible(
    const std::uintptr_t address, const std::size_t size,
    const bool write) noexcept {
    if (address == 0 || size == 0 ||
        address > std::numeric_limits<std::uintptr_t>::max() - size) {
        return false;
    }
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &info,
                     sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }
    const DWORD protection = info.Protect & 0xFFU;
    const bool readable = protection == PAGE_READONLY ||
        protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
        protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
    const bool writable = protection == PAGE_READWRITE ||
        protection == PAGE_WRITECOPY ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
    const auto region_begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    return readable && (!write || writable) && address >= region_begin &&
        size <= info.RegionSize - (address - region_begin);
}

template <std::size_t Size>
[[nodiscard]] bool copy_snapshot(
    const std::uintptr_t address,
    std::array<std::byte, Size>* const output) noexcept {
    if (output == nullptr || !accessible(address, Size, false)) {
        return false;
    }
    std::memcpy(output->data(), reinterpret_cast<const void*>(address), Size);
    return true;
}

[[nodiscard]] bool scoped_turret(const std::uintptr_t entity) noexcept {
    if (!accessible(entity + kGentityWeaponOffset, sizeof(std::uint32_t), false) ||
        !accessible(g_weapon_count, sizeof(std::uint32_t), false)) {
        return true;
    }
    std::uint32_t weapon = 0;
    std::uint32_t count = 0;
    std::memcpy(&weapon, reinterpret_cast<const void*>(entity + kGentityWeaponOffset), 4);
    std::memcpy(&count, reinterpret_cast<const void*>(g_weapon_count), 4);
    constexpr std::uint32_t kWeaponDefinitionTableEntries = 128;
    if (weapon == 0 || weapon > count ||
        weapon >= kWeaponDefinitionTableEntries ||
        count >= kWeaponDefinitionTableEntries) {
        return true;
    }
    const auto slot = g_weapon_definitions + weapon * sizeof(std::uint32_t);
    if (!accessible(slot, 4, false)) {
        return true;
    }
    std::uint32_t definition = 0;
    std::memcpy(&definition, reinterpret_cast<const void*>(slot), 4);
    if (!accessible(definition + kWeaponOverlayInterfaceOffset, 4, false)) {
        return true;
    }
    std::int32_t overlay_interface = 0;
    std::memcpy(
        &overlay_interface,
        reinterpret_cast<const void*>(definition + kWeaponOverlayInterfaceOffset),
        sizeof(overlay_interface));
    return overlay_interface == kTurretScopeOverlayInterface;
}

[[nodiscard]] bool live_context(
    const std::uintptr_t local_entity, const std::uintptr_t mounted_entity,
    MountedGunContext* const context) noexcept {
    if (context == nullptr || scoped_turret(mounted_entity)) {
        return false;
    }
    std::uint32_t client = 0;
    std::uint32_t turret = 0;
    if (!accessible(local_entity + kGentityClientOffset, 4, false) ||
        !accessible(mounted_entity + kGentityTurretOffset, 4, false)) {
        return false;
    }
    std::memcpy(&client,
                reinterpret_cast<const void*>(local_entity + kGentityClientOffset), 4);
    std::memcpy(&turret,
                reinterpret_cast<const void*>(mounted_entity + kGentityTurretOffset), 4);
    std::array<std::byte, kGentitySpan> local_bytes{};
    std::array<std::byte, kPlayerStateSpan> player_bytes{};
    std::array<std::byte, kGentitySpan> mounted_bytes{};
    std::array<std::byte, kTurretInfoSpan> turret_bytes{};
    std::int32_t entity_number = -1;
    if (!copy_snapshot(local_entity, &local_bytes) ||
        !copy_snapshot(client, &player_bytes) ||
        !copy_snapshot(mounted_entity, &mounted_bytes) ||
        !copy_snapshot(turret, &turret_bytes)) {
        return false;
    }
    std::memcpy(&entity_number, mounted_bytes.data(), sizeof(entity_number));
    const SpMountedGunMemorySnapshot snapshot{
        local_bytes, player_bytes, mounted_bytes, turret_bytes,
        entity_number, false};
    return decode_sp_mounted_gun_context(snapshot, context) ==
        MountedGunContextStatus::valid;
}

[[nodiscard]] bool current_world_aim(
    const std::uint64_t now_milliseconds,
    MountedGunAngles* const output) noexcept {
    if (output == nullptr) {
        return false;
    }
    MountedGunAimPublication publication{};
    AcquireSRWLockShared(&g_world_aim_lock);
    publication = g_world_aim;
    ReleaseSRWLockShared(&g_world_aim_lock);
    if (!mounted_gun_aim_publication_is_current(
            publication, now_milliseconds,
            kMaximumControllerFrameAgeMilliseconds)) {
        return false;
    }
    *output = publication.world;
    return true;
}

[[nodiscard]] bool current_mounted_aim(
    const std::uintptr_t local_entity, const std::uintptr_t mounted_entity,
    MountedGunAngles* const world, MountedGunContext* const context) noexcept {
    return current_world_aim(GetTickCount64(), world) &&
        live_context(local_entity, mounted_entity, context);
}

extern "C" void __cdecl begin_server_substitution(
    void* const self, void* const other) noexcept {
    g_server_substitution = {};
    if (!g_enabled.load(std::memory_order_acquire) || self == nullptr ||
        other == nullptr) {
        return;
    }
    MountedGunAngles world{};
    MountedGunContext context{};
    const auto local = reinterpret_cast<std::uintptr_t>(other);
    const auto mounted = reinterpret_cast<std::uintptr_t>(self);
    if (local != g_local_gentity ||
        !current_mounted_aim(local, mounted, &world, &context)) {
        return;
    }
    std::uint32_t client = 0;
    std::memcpy(&client,
                reinterpret_cast<const void*>(local + kGentityClientOffset), 4);
    const auto address = static_cast<std::uintptr_t>(client) +
        kPlayerViewAnglesOffset;
    if (!accessible(address, sizeof(MountedGunAngles), true)) {
        return;
    }
    auto* const angles = reinterpret_cast<float*>(address);
    g_server_substitution = {
        true, mounted, angles, {angles[0], angles[1]}};
    angles[0] = world.pitch;
    angles[1] = world.yaw;
    if (!g_logged_first_server_aim.exchange(true, std::memory_order_acq_rel)) {
        WAWVR_STEREO_DIAG_ONCE(
            "MountedGunDiag authoritative turret aim consumed cached input-boundary controller ray pitch=%.2f yaw=%.2f",
            world.pitch, world.yaw);
    }
}

extern "C" void __cdecl end_server_substitution() noexcept {
    if (g_server_substitution.active &&
        accessible(g_server_substitution.mounted_entity +
                       kGentityGunAnglesOffset,
                   sizeof(MountedGunAngles), false)) {
        MountedGunAngles relative{};
        std::memcpy(
            &relative,
            reinterpret_cast<const void*>(
                g_server_substitution.mounted_entity +
                kGentityGunAnglesOffset),
            sizeof(relative));
        WAWVR_STEREO_DIAG_ONCE(
            "MountedGunDiag native turret clamp produced relative gun angles pitch=%.2f yaw=%.2f",
            relative.pitch, relative.yaw);
    }
    if (g_server_substitution.active &&
        accessible(reinterpret_cast<std::uintptr_t>(
                       g_server_substitution.view_angles),
                   sizeof(MountedGunAngles), true)) {
        g_server_substitution.view_angles[0] = g_server_substitution.saved.pitch;
        g_server_substitution.view_angles[1] = g_server_substitution.saved.yaw;
    }
    g_server_substitution = {};
}

extern "C" void __cdecl begin_fire_substitution(
    void* const self, void* const other) noexcept {
    g_fire_substitution = {};
    g_local_mounted_shot_pending = false;
    if (!g_enabled.load(std::memory_order_acquire) || self == nullptr ||
        other == nullptr) {
        return;
    }
    const auto mounted = reinterpret_cast<std::uintptr_t>(self);
    const auto local = reinterpret_cast<std::uintptr_t>(other);
    MountedGunAngles cached_world{};
    MountedGunContext context{};
    if (local != g_local_gentity ||
        !live_context(local, mounted, &context)) {
        return;
    }
    // This call belongs to Fire_Lead, not the repeatedly evaluated mounted
    // model. Remember the validated local discharge independently of whether
    // the optional controller-aim substitution is usable for this shot.
    g_local_mounted_shot_pending = true;
    if (!current_world_aim(GetTickCount64(), &cached_world) ||
        !accessible(mounted + kGentityCurrentAnglesOffset,
                    sizeof(std::array<float, 3>), false) ||
        !accessible(mounted + kGentityGunAnglesOffset,
                    sizeof(MountedGunAngles), false) ||
        !accessible(local + kGentityClientOffset, 4, false)) {
        return;
    }
    std::array<float, 3> base{};
    MountedGunAngles relative{};
    std::uint32_t client = 0;
    std::memcpy(
        &base,
        reinterpret_cast<const void*>(
            mounted + kGentityCurrentAnglesOffset),
        sizeof(base));
    std::memcpy(
        &relative,
        reinterpret_cast<const void*>(
            mounted + kGentityGunAnglesOffset),
        sizeof(relative));
    std::memcpy(
        &client,
        reinterpret_cast<const void*>(local + kGentityClientOffset), 4);
    const std::array<float, 3> authoritative_world{
        base[0] + relative.pitch,
        base[1] + relative.yaw,
        base[2],
    };
    const auto view_angles_address = static_cast<std::uintptr_t>(client) +
        kPlayerViewAnglesOffset;
    if (!std::isfinite(authoritative_world[0]) ||
        !std::isfinite(authoritative_world[1]) ||
        !std::isfinite(authoritative_world[2]) ||
        !accessible(view_angles_address, sizeof(authoritative_world), true)) {
        return;
    }
    auto* const view_angles =
        reinterpret_cast<float*>(view_angles_address);
    g_fire_substitution = {
        true, view_angles,
        {view_angles[0], view_angles[1], view_angles[2]}};
    view_angles[0] = authoritative_world[0];
    view_angles[1] = authoritative_world[1];
    view_angles[2] = authoritative_world[2];
    if (!g_logged_first_fire_aim.exchange(
            true, std::memory_order_acq_rel)) {
        WAWVR_STEREO_DIAG_ONCE(
            "MountedGunDiag firing ray consumes authoritative clamped barrel pitch=%.2f yaw=%.2f",
            authoritative_world[0], authoritative_world[1]);
    }
}

extern "C" void __cdecl end_fire_substitution() noexcept {
    const bool local_shot = g_local_mounted_shot_pending;
    g_local_mounted_shot_pending = false;
    if (g_fire_substitution.active &&
        accessible(reinterpret_cast<std::uintptr_t>(
                   g_fire_substitution.view_angles),
                   sizeof(g_fire_substitution.saved), true)) {
        g_fire_substitution.view_angles[0] =
            g_fire_substitution.saved[0];
        g_fire_substitution.view_angles[1] =
            g_fire_substitution.saved[1];
        g_fire_substitution.view_angles[2] =
            g_fire_substitution.saved[2];
    }
    g_fire_substitution = {};
    if (local_shot && g_enabled.load(std::memory_order_acquire)) {
        queue_firing_haptic(GetTickCount64());
    }
}

extern "C" void __cdecl begin_visual_substitution(void* const pose) noexcept {
    g_visual_substitution = {};
    if (!g_enabled.load(std::memory_order_acquire) || pose == nullptr ||
        !accessible(reinterpret_cast<std::uintptr_t>(pose), 0x58, true)) {
        return;
    }
    const auto pose_address = reinterpret_cast<std::uintptr_t>(pose);
    std::uint8_t type = 0;
    std::uint8_t using_turret = 0;
    std::memcpy(&type, reinterpret_cast<const void*>(pose_address + kPoseTypeOffset), 1);
    std::memcpy(&using_turret,
                reinterpret_cast<const void*>(pose_address + kPoseTurretPlayerUsingOffset), 1);
    if (type != kMountedEntityType || using_turret == 0) {
        return;
    }
    std::uint32_t client = 0;
    if (!accessible(g_local_gentity + kGentityClientOffset, 4, false)) {
        return;
    }
    std::memcpy(&client,
                reinterpret_cast<const void*>(g_local_gentity + kGentityClientOffset), 4);
    if (!accessible(client + 0x83C, 4, false)) {
        return;
    }
    std::int32_t entity_number = -1;
    std::memcpy(&entity_number, reinterpret_cast<const void*>(client + 0x83C), 4);
    if (entity_number < 0 || entity_number > kSpMaximumEntityNumber) {
        return;
    }
    const auto mounted = g_local_gentity +
        static_cast<std::size_t>(entity_number) * 0x378;
    MountedGunAngles world{};
    MountedGunContext context{};
    if (!current_mounted_aim(g_local_gentity, mounted, &world, &context)) {
        return;
    }
    MountedGunAngles predicted_base{};
    MountedGunAngles predicted_range{};
    if (!accessible(g_predicted_player_state + 0x144,
                    sizeof(MountedGunAngles), false) ||
        !accessible(g_predicted_player_state + 0x14C,
                    sizeof(MountedGunAngles), false)) {
        return;
    }
    std::memcpy(&predicted_base,
                reinterpret_cast<const void*>(g_predicted_player_state + 0x144),
                sizeof(predicted_base));
    std::memcpy(&predicted_range,
                reinterpret_cast<const void*>(g_predicted_player_state + 0x14C),
                sizeof(predicted_range));
    MountedGunAngles clamped{};
    if (!clamp_mounted_world_angles(
            world, predicted_base, predicted_range, &clamped)) {
        return;
    }
    auto* const slot = reinterpret_cast<std::uint32_t*>(
        pose_address + kPoseTurretViewAnglesOffset);
    g_visual_substitution.controller_angles = {clamped.pitch, clamped.yaw};
    g_visual_substitution.view_angles_slot = slot;
    g_visual_substitution.saved_pointer = *slot;
    *slot = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(
        g_visual_substitution.controller_angles.data()));
    g_visual_substitution.active = true;
    if (!g_logged_first_visual_aim.exchange(true, std::memory_order_acq_rel)) {
        WAWVR_STEREO_DIAG_ONCE(
            "MountedGunDiag visible tag_aim consumed the same cached controller ray pitch=%.2f yaw=%.2f",
            clamped.pitch, clamped.yaw);
    }
}

extern "C" void __cdecl end_visual_substitution() noexcept {
    if (g_visual_substitution.active &&
        accessible(reinterpret_cast<std::uintptr_t>(
                       g_visual_substitution.view_angles_slot),
                   sizeof(std::uint32_t), true)) {
        *g_visual_substitution.view_angles_slot =
            g_visual_substitution.saved_pointer;
    }
    g_visual_substitution = {};
}

#if defined(_MSC_VER) && defined(_M_IX86)
__declspec(naked) void server_call_bridge() {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        push eax
        push esi
        call begin_server_substitution
        add esp, 8
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        call dword ptr [g_server_original]
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        call end_server_substitution
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        ret
    }
}

__declspec(naked) void client_call_bridge() {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        push esi
        call begin_visual_substitution
        add esp, 4
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        // Native ABI: ESI=pose, EDI=partBits, [ESP+4]=DObj. The bridge call
        // added its own return address, so recreate the caller-cleaned DObj
        // argument immediately below the native return address.
        push dword ptr [esp + 4]
        call dword ptr [g_client_original]
        add esp, 4
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        call end_visual_substitution
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        ret
    }
}

__declspec(naked) void fire_call_bridge() {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        // Fire_Lead call-site ABI at entry S: EAX=turret,
        // [S+4]=activator, [S+8]=weaponParms. After pushfd/pushad,
        // EBX=S-36, so the saved EAX and activator are at +28/+40.
        push dword ptr [ebx + 40]
        push dword ptr [ebx + 28]
        call begin_fire_substitution
        add esp, 8
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        // Duplicate the caller-owned weaponParms and activator arguments for
        // the nested native call. Fire_Lead retains its originals below us.
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        call dword ptr [g_fire_original]
        add esp, 8
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        call end_fire_substitution
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        ret
    }
}
#else
void server_call_bridge() {}
void client_call_bridge() {}
void fire_call_bridge() {}
#endif

[[nodiscard]] bool relative_call(
    const std::uintptr_t source, const std::uintptr_t destination,
    std::array<std::uint8_t, kCallSize>* const output) noexcept {
    if (output == nullptr) return false;
    const std::int64_t delta = static_cast<std::int64_t>(destination) -
        static_cast<std::int64_t>(source + kCallSize);
    if (delta < std::numeric_limits<std::int32_t>::min() ||
        delta > std::numeric_limits<std::int32_t>::max()) return false;
    (*output)[0] = 0xE8;
    const auto encoded = static_cast<std::int32_t>(delta);
    std::memcpy(output->data() + 1, &encoded, sizeof(encoded));
    return true;
}

}  // namespace

bool mounted_gun_controller_route_available(
    const ControllerFrameSnapshot& snapshot,
    const std::uint64_t now_milliseconds) noexcept {
    if (!g_enabled.load(std::memory_order_acquire) ||
        !controller_frame_is_current(snapshot, now_milliseconds) ||
        !accessible(g_local_gentity + kGentityClientOffset, 4, false)) return false;
    std::uint32_t client = 0;
    std::memcpy(&client,
                reinterpret_cast<const void*>(g_local_gentity + kGentityClientOffset), 4);
    if (!accessible(client + 0x83C, 4, false)) return false;
    std::int32_t entity_number = -1;
    std::memcpy(&entity_number, reinterpret_cast<const void*>(client + 0x83C), 4);
    if (entity_number < 0 || entity_number > kSpMaximumEntityNumber) return false;
    MountedGunContext context{};
    return live_context(
        g_local_gentity,
        g_local_gentity + static_cast<std::size_t>(entity_number) * 0x378,
        &context);
}

void publish_mounted_gun_controller_aim(
    const float pitch_degrees,
    const float yaw_degrees,
    const std::uint64_t controller_generation,
    const std::uint64_t publication_milliseconds) noexcept {
    const MountedGunAimPublication publication{
        {pitch_degrees, yaw_degrees}, controller_generation,
        publication_milliseconds};
    if (!mounted_gun_aim_publication_is_current(
            publication, publication_milliseconds,
            kMaximumControllerFrameAgeMilliseconds)) {
        return;
    }
    AcquireSRWLockExclusive(&g_world_aim_lock);
    g_world_aim = publication;
    ReleaseSRWLockExclusive(&g_world_aim_lock);
}

MountedGunRuntimeInstallResult install_mounted_gun_runtime(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    MountedGunRuntimeInstallResult result{};
    if (g_installed.load(std::memory_order_acquire)) {
        result.status = MountedGunRuntimeStatus::already_installed;
        return result;
    }
    if (select_t4_layout_family(bindings.profile()) !=
        T4LayoutFamily::single_player_1_7_1263) {
        result.status = MountedGunRuntimeStatus::not_applicable;
        return result;
    }
#if !defined(_MSC_VER) || !defined(_M_IX86)
    result.status = MountedGunRuntimeStatus::not_applicable;
    return result;
#else
    const auto server = wawvr::t4::prepare_inline_hook(
        bindings, wawvr::t4::HookSiteId::mounted_turret_clientaim_call,
        reinterpret_cast<std::uintptr_t>(&server_call_bridge));
    const auto client = wawvr::t4::prepare_inline_hook(
        bindings, wawvr::t4::HookSiteId::mounted_mg42_controllers_call,
        reinterpret_cast<std::uintptr_t>(&client_call_bridge));
    const auto fire = wawvr::t4::prepare_inline_hook(
        bindings,
        wawvr::t4::HookSiteId::mounted_turret_fill_weapon_parms_call,
        reinterpret_cast<std::uintptr_t>(&fire_call_bridge));
    if (!server.ok() || !client.ok() || !fire.ok()) {
        result.status = MountedGunRuntimeStatus::preparation_failed;
        return result;
    }
    result.server_call = server.hook->target;
    result.fire_call = fire.hook->target;
    result.client_call = client.hook->target;
    if (server.hook->minimum_patch_bytes != kCallSize ||
        fire.hook->minimum_patch_bytes != kCallSize ||
        client.hook->minimum_patch_bytes != kCallSize ||
        server.hook->expected_size != kCallSize ||
        fire.hook->expected_size != kCallSize ||
        client.hook->expected_size != kCallSize ||
        server.hook->expected[0] != 0xE8 ||
        fire.hook->expected[0] != 0xE8 ||
        client.hook->expected[0] != 0xE8) {
        result.status = MountedGunRuntimeStatus::unexpected_instruction_boundary;
        return result;
    }
    std::int32_t server_disp = 0;
    std::int32_t fire_disp = 0;
    std::int32_t client_disp = 0;
    std::memcpy(&server_disp, server.hook->expected.data() + 1, 4);
    std::memcpy(&fire_disp, fire.hook->expected.data() + 1, 4);
    std::memcpy(&client_disp, client.hook->expected.data() + 1, 4);
    g_server_original = server.hook->target + 5 + server_disp;
    g_fire_original = fire.hook->target + 5 + fire_disp;
    g_client_original = client.hook->target + 5 + client_disp;
    const auto local = bindings.data_address(
        wawvr::t4::DataSymbolId::local_player_entity, 0x378);
    const auto definitions = bindings.data_address(
        wawvr::t4::DataSymbolId::weapon_definition_pointer_table, 0x200);
    const auto count = bindings.data_address(
        wawvr::t4::DataSymbolId::weapon_definition_count, 4);
    const auto predicted = bindings.data_address(
        wawvr::t4::DataSymbolId::predicted_player_state, 0x840);
    if (!local || !definitions || !count || !predicted) {
        result.status = MountedGunRuntimeStatus::address_out_of_range;
        return result;
    }
    std::array<std::uint8_t, 5> server_replacement{};
    std::array<std::uint8_t, 5> fire_replacement{};
    std::array<std::uint8_t, 5> client_replacement{};
    if (!relative_call(server.hook->target,
                       reinterpret_cast<std::uintptr_t>(&server_call_bridge),
                       &server_replacement) ||
        !relative_call(fire.hook->target,
                       reinterpret_cast<std::uintptr_t>(&fire_call_bridge),
                       &fire_replacement) ||
        !relative_call(client.hook->target,
                       reinterpret_cast<std::uintptr_t>(&client_call_bridge),
                       &client_replacement)) {
        result.status = MountedGunRuntimeStatus::jump_out_of_range;
        return result;
    }
    const std::array<PeerThreadPatchRange, 3> ranges{{
        {server.hook->target, 5},
        {fire.hook->target, 5},
        {client.hook->target, 5}}};
    SuspendedPeerThreads suspended;
    PeerThreadQuiesceResult quiesce{};
    if (!suspended.suspend(ranges, &quiesce)) {
        result.status = MountedGunRuntimeStatus::thread_suspend_failed;
        result.system_error = quiesce.system_error;
        return result;
    }
    auto* const server_target = reinterpret_cast<std::uint8_t*>(server.hook->target);
    auto* const fire_target = reinterpret_cast<std::uint8_t*>(fire.hook->target);
    auto* const client_target = reinterpret_cast<std::uint8_t*>(client.hook->target);
    if (std::memcmp(server_target, server.hook->expected.data(), 5) != 0 ||
        std::memcmp(fire_target, fire.hook->expected.data(), 5) != 0 ||
        std::memcmp(client_target, client.hook->expected.data(), 5) != 0) {
        result.status = MountedGunRuntimeStatus::expected_bytes_changed;
        return result;
    }
    const std::array<const wawvr::t4::PreparedInlineHook*, 3> hooks{
        &*server.hook, &*fire.hook, &*client.hook};
    const std::array<std::uint8_t*, 3> targets{
        server_target, fire_target, client_target};
    const std::array<const std::array<std::uint8_t, 5>*, 3> replacements{
        &server_replacement, &fire_replacement, &client_replacement};
    std::array<DWORD, 3> protections{};
    for (std::size_t index = 0; index < targets.size(); ++index) {
        if (!query_page_protection(targets[index], &protections[index])) {
            result.status = MountedGunRuntimeStatus::protection_failed;
            result.system_error = GetLastError();
            return result;
        }
    }
    std::array<bool, 3> writable{};
    DWORD write_error = 0;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        DWORD discarded = 0;
        writable[index] = VirtualProtect(
            targets[index], 5, PAGE_EXECUTE_READWRITE, &discarded) != FALSE;
        if (!writable[index] && write_error == 0) {
            write_error = GetLastError();
        }
    }
    if (!writable[0] || !writable[1] || !writable[2]) {
        bool rollback_ok = true;
        for (std::size_t index = 0; index < targets.size(); ++index) {
            if (writable[index]) {
                DWORD ignored = 0;
                rollback_ok =
                    VirtualProtect(targets[index], 5, protections[index],
                                   &ignored) != FALSE &&
                    page_protection_is(targets[index], protections[index]) &&
                    rollback_ok;
            }
            rollback_ok =
                std::memcmp(targets[index], hooks[index]->expected.data(), 5) == 0 &&
                page_protection_is(targets[index], protections[index]) &&
                rollback_ok;
        }
        result.status = rollback_ok ? MountedGunRuntimeStatus::patch_failed :
                                      MountedGunRuntimeStatus::rollback_failed;
        result.system_error = write_error;
        return result;
    }
    g_local_gentity = *local;
    g_weapon_definitions = *definitions;
    g_weapon_count = *count;
    g_predicted_player_state = *predicted;
    AcquireSRWLockExclusive(&g_world_aim_lock);
    g_world_aim = {};
    ReleaseSRWLockExclusive(&g_world_aim_lock);
    g_logged_first_server_aim.store(false, std::memory_order_release);
    g_logged_first_fire_aim.store(false, std::memory_order_release);
    g_logged_first_visual_aim.store(false, std::memory_order_release);
    bool written = true;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        std::memcpy(targets[index], replacements[index]->data(), 5);
        written =
            std::memcmp(targets[index], replacements[index]->data(), 5) == 0 &&
            FlushInstructionCache(GetCurrentProcess(), targets[index], 5) &&
            written;
    }
    bool protections_restored = true;
    DWORD protect_error = 0;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        DWORD ignored = 0;
        const bool restored = VirtualProtect(
            targets[index], 5, protections[index], &ignored) != FALSE;
        if (!restored && protect_error == 0) {
            protect_error = GetLastError();
        }
        protections_restored = restored &&
            page_protection_is(targets[index], protections[index]) &&
            protections_restored;
    }
    if (!written || !protections_restored) {
        const DWORD patch_error =
            !written ? ERROR_WRITE_FAULT : protect_error;
        std::array<bool, 3> reopened{};
        for (std::size_t index = 0; index < targets.size(); ++index) {
            DWORD temporary = 0;
            reopened[index] = VirtualProtect(
                targets[index], 5, PAGE_EXECUTE_READWRITE,
                &temporary) != FALSE;
            if (reopened[index]) {
                std::memcpy(targets[index], hooks[index]->expected.data(), 5);
            }
        }
        bool rollback_ok = true;
        for (std::size_t index = 0; index < targets.size(); ++index) {
            const bool bytes_restored = reopened[index] &&
                std::memcmp(targets[index], hooks[index]->expected.data(), 5) == 0 &&
                FlushInstructionCache(GetCurrentProcess(), targets[index], 5);
            DWORD ignored = 0;
            const bool rx_restored = reopened[index] && VirtualProtect(
                targets[index], 5, protections[index], &ignored) != FALSE &&
                page_protection_is(targets[index], protections[index]);
            rollback_ok = bytes_restored && rx_restored && rollback_ok;
        }
        result.status = rollback_ok ? MountedGunRuntimeStatus::patch_failed :
                                      MountedGunRuntimeStatus::rollback_failed;
        result.system_error = patch_error;
        return result;
    }
    g_enabled.store(true, std::memory_order_release);
    bind_mounted_gun_route_probe(&mounted_gun_controller_route_available);
    g_installed.store(true, std::memory_order_release);
    result.status = MountedGunRuntimeStatus::installed;
    return result;
#endif
}

}  // namespace wawvr::mod
