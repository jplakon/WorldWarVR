// SPDX-License-Identifier: GPL-3.0-only
#include "input_hook.hpp"
#include "aircraft_control_runtime.hpp"

#include "controller_state.hpp"
#include "input_mapping.hpp"
#include "manual_grenade_runtime.hpp"
#include "manual_reload_runtime.hpp"
#include "mounted_gun_runtime.hpp"
#include "pistol_support_pose.hpp"
#include "present_hook.hpp"
#include "present_hook_logic.hpp"
#include "satchel_input_logic.hpp"
#include "stereo_frame_broker.hpp"
#include "support_animation_policy.hpp"
#include "tank_control_logic.hpp"
#include "t4_layout_selector.hpp"
#include "tracking_anchor_sync.hpp"
#include "vehicle_snapshot_reader.hpp"
#include "weapon_hook.hpp"
#include "weapon_grip_logic.hpp"

#include "t4/hook_api.hpp"
#include "t4/profile.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>

namespace wawvr::mod {

extern "C" void __cdecl wawvr_apply_controller_usercmd_from_bridge(
    wawvr::t4::UsercmdSp* command) noexcept;
extern "C" void __cdecl wawvr_apply_controller_usercmd_mp_from_bridge(
    wawvr::t4::UsercmdMp* command) noexcept;

namespace {

constexpr std::size_t kInlinePatchSize =
    kPostBuildUsercmdDisplacedInstruction.size();
constexpr std::size_t kTrampolineSize = kInlinePatchSize + 5;

std::atomic<bool> g_controller_input_installed{false};
std::atomic<bool> g_controller_input_enabled{false};
std::atomic<bool> g_logged_first_movement{false};
std::atomic<bool> g_logged_first_aim{false};
std::atomic<bool> g_logged_first_sprint_latch{false};
std::atomic<bool> g_logged_first_body_yaw_sync{false};
std::atomic<bool> g_logged_first_right_stick{false};
std::atomic<bool> g_logged_first_smooth_turn{false};
std::atomic<bool> g_logged_first_controller_owned_support{false};
std::uintptr_t g_gameplay_refdef_origin_address = 0;
std::uintptr_t g_gameplay_refdef_axis_address = 0;
std::uintptr_t g_client_view_yaw_address = 0;
std::uintptr_t g_key_catchers_address = 0;
std::uintptr_t g_connection_state_address = 0;
std::uintptr_t g_cgame_gun_pitch_address = 0;
std::uintptr_t g_cgame_gun_yaw_address = 0;
std::int32_t g_active_connection_state = kT4SpActiveConnectionState;
SnapTurnState g_snap_turn_state{};
SmoothTurnState g_smooth_turn_state{};
TurnMode g_turn_mode = TurnMode::Snap;
SprintLatchState g_sprint_latch_state{};
PhysicalMeleeState g_physical_melee_state{};
SatchelInputState g_satchel_input_state{};
static_assert(kSatchelNativeThrowButton == wawvr::t4::button_mask(
    wawvr::t4::UsercmdButton::throw_remote_charge));
bool g_snap_gate_diagnostic_latched = false;
bool g_right_stick_melee_was_held = false;
bool g_native_melee_was_held = false;
std::uintptr_t g_tank_predicted_ps = 0;
std::uintptr_t g_tank_entities = 0;
std::uintptr_t g_tank_vehicle_info = 0;
std::uintptr_t g_tank_client_pitch = 0;
TankControlState g_tank_control_state{};
std::int32_t g_tank_owned_entity = -1;
bool g_logged_tank_controls = false;

// Read directly by the naked bridge. It is initialized before the target JMP
// is made visible while every pre-existing process thread is suspended.
void* g_input_trampoline = nullptr;
void* g_mp_original_create_cmd = nullptr;

struct ThreadRecord final {
    HANDLE handle{};
    DWORD id{};
    bool suspended{};
};

class SuspendedProcessThreads final {
public:
    SuspendedProcessThreads() = default;
    SuspendedProcessThreads(const SuspendedProcessThreads&) = delete;
    SuspendedProcessThreads& operator=(const SuspendedProcessThreads&) = delete;

    ~SuspendedProcessThreads() {
        for (std::size_t index = 0; index < thread_count_; ++index) {
            auto& thread = threads_[index];
            if (thread.suspended) {
                ResumeThread(thread.handle);
            }
            if (thread.handle != nullptr) {
                CloseHandle(thread.handle);
            }
        }
    }

    [[nodiscard]] bool open_all(
        InputHookInstallResult* const result) noexcept {
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            fail(result, InputHookStatus::thread_snapshot_failed, GetLastError());
            return false;
        }

        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (!Thread32First(snapshot, &entry)) {
            const DWORD error = GetLastError();
            CloseHandle(snapshot);
            fail(result, InputHookStatus::thread_snapshot_failed, error);
            return false;
        }

        const DWORD process_id = GetCurrentProcessId();
        const DWORD current_thread_id = GetCurrentThreadId();
        do {
            if (entry.th32OwnerProcessID == process_id &&
                entry.th32ThreadID != current_thread_id) {
                if (thread_count_ == threads_.size()) {
                    CloseHandle(snapshot);
                    fail(result, InputHookStatus::thread_snapshot_failed,
                         ERROR_INSUFFICIENT_BUFFER);
                    return false;
                }
                const HANDLE handle = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                    THREAD_QUERY_INFORMATION,
                    FALSE, entry.th32ThreadID);
                if (handle == nullptr) {
                    const DWORD error = GetLastError();
                    // Threads can disappear between the snapshot and
                    // OpenThread. A vanished thread cannot execute the site.
                    if (error != ERROR_INVALID_PARAMETER) {
                        CloseHandle(snapshot);
                        fail(result, InputHookStatus::thread_open_failed,
                             error);
                        return false;
                    }
                } else {
                    threads_[thread_count_++] = {
                        handle, entry.th32ThreadID, false};
                }
            }
            entry.dwSize = sizeof(entry);
        } while (Thread32Next(snapshot, &entry));
        CloseHandle(snapshot);
        return true;
    }

    [[nodiscard]] bool suspend_and_validate(
        const std::uintptr_t target,
        const std::size_t patch_size,
        InputHookInstallResult* const result) noexcept {
        for (std::size_t index = 0; index < thread_count_; ++index) {
            auto& thread = threads_[index];
            if (SuspendThread(thread.handle) == static_cast<DWORD>(-1)) {
                fail(result, InputHookStatus::thread_suspend_failed,
                     GetLastError());
                return false;
            }
            thread.suspended = true;

            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(thread.handle, &context)) {
                fail(result, InputHookStatus::thread_context_failed,
                     GetLastError());
                return false;
            }
#if defined(_M_IX86)
            const std::uintptr_t instruction = context.Eip;
#else
            const std::uintptr_t instruction = 0;
#endif
            if (instruction >= target && instruction < target + patch_size) {
                fail(result, InputHookStatus::target_thread_inside_patch, 0);
                return false;
            }
        }
        return true;
    }

private:
    static void fail(
        InputHookInstallResult* const result,
        const InputHookStatus status,
        const DWORD error) noexcept {
        if (result != nullptr) {
            result->status = status;
            result->system_error = error;
        }
    }

    std::array<ThreadRecord, 256> threads_{};
    std::size_t thread_count_{};
};

[[nodiscard]] bool input_disabled_by_environment() noexcept {
    std::array<wchar_t, 8> value{};
    const DWORD length = GetEnvironmentVariableW(
        L"WAWVR_DISABLE_INPUT", value.data(),
        static_cast<DWORD>(value.size()));
    return length == 1 && value[0] == L'1';
}

[[nodiscard]] TurnMode requested_turn_mode() noexcept {
    std::array<wchar_t, 16> value{};
    const DWORD length = GetEnvironmentVariableW(
        kTurnModeEnvironmentVariable, value.data(),
        static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size()) {
        return TurnMode::Snap;
    }
    return turn_mode_from_setting(
        std::wstring_view(value.data(), static_cast<std::size_t>(length)));
}

[[nodiscard]] bool protection_is_writable(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffU;
    return access == PAGE_READWRITE || access == PAGE_WRITECOPY ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool protection_is_readable(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffU;
    return access == PAGE_READONLY || access == PAGE_READWRITE ||
           access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool writable_range(
    const void* const address,
    const std::size_t size) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        !protection_is_writable(memory.Protect)) {
        return false;
    }
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(address);
    const std::uintptr_t region_begin =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const std::uintptr_t region_end = region_begin + memory.RegionSize;
    return begin >= region_begin && begin <= region_end &&
           size <= region_end - begin;
}

[[nodiscard]] bool readable_range(
    const void* const address,
    const std::size_t size) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        !protection_is_readable(memory.Protect)) {
        return false;
    }
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(address);
    const std::uintptr_t region_begin =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const std::uintptr_t region_end = region_begin + memory.RegionSize;
    return begin >= region_begin && begin <= region_end &&
           size <= region_end - begin;
}

template <typename T>
[[nodiscard]] bool read_tank_value(const std::uintptr_t address, T* value) noexcept {
    if (!readable_range(reinterpret_cast<const void*>(address), sizeof(T))) return false;
    std::memcpy(value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

struct TankContext {
    std::int32_t entity{-1};
    std::uintptr_t vehicle_info{};
    std::uintptr_t vehicle{};
};

[[nodiscard]] bool read_tank_context(TankContext* context) noexcept {
    if (g_tank_predicted_ps == 0 || g_tank_entities == 0 ||
        g_tank_vehicle_info == 0 || context == nullptr) return false;
    // This local object never crosses the native simulation/callback boundary.
    // Neighboring PS/entity fields share one validated region, not one syscall
    // per scalar read. All values and vehicle identities are still read afresh.
    VehicleSnapshotReader memory;
    std::int32_t connection = 0, entity = -1, seat = -1, type = 0;
    std::uint32_t flags = 0, vehicle = 0;
    std::uint16_t owner = 0;
    std::int16_t info_index = -1, vehicle_type = -1;
    if (!memory.read(g_connection_state_address, &connection) ||
        connection != kT4SpActiveConnectionState ||
        !memory.read(g_tank_predicted_ps + 0xCC, &flags) ||
        (flags & 0x4000U) == 0 ||
        !memory.read(g_tank_predicted_ps + 0x83C, &entity) ||
        !memory.read(g_tank_predicted_ps + 0x840, &seat) ||
        seat != 0 || entity <= 0 || entity >= 1023) return false;
    const auto ent = g_tank_entities + static_cast<std::uintptr_t>(entity) * 0x378;
    if (!memory.read(ent + 4, &type) || type != 13 ||
        !memory.read(ent + 0x178, &owner) || owner != 1 ||
        !memory.read(ent + 0x18C, &vehicle) || vehicle == 0 ||
        !memory.read(vehicle + 0x1CC, &info_index) ||
        info_index < 0 || info_index >= 64) return false;
    const auto info = g_tank_vehicle_info + static_cast<std::uintptr_t>(info_index) * 0x8FC;
    if (!memory.read(info + 0x40, &vehicle_type) || vehicle_type != 1) return false;
    *context = {entity, info, vehicle};
    return true;
}

// Keep the native vehicle simulation, target trace, projectile origin, and
// mechanical turret limits. Only replace its normal mouse/keyboard commands.
// HMD/controller poses never enter this route; looking around cannot steer.
void apply_tank_command(wawvr::t4::UsercmdSp& command,
                        const ControllerFrameSnapshot& snapshot,
                        const bool input_owned, const std::uint64_t now,
                        const TankContext& context) noexcept {
    if (g_tank_owned_entity != context.entity) {
        g_tank_owned_entity = context.entity;
        reset_tank_control(&g_tank_control_state);
    }
    const auto& left = snapshot.frame.actions.hands[static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
    const auto& right = snapshot.frame.actions.hands[static_cast<std::uint32_t>(wawvr::xr::Hand::Right)];
    const auto delta = update_tank_control({
        .input_owned = input_owned,
        .stick_active = right.stick.active,
        .action_sequence = snapshot.frame.actions.sequence,
        .now_milliseconds = now,
        .stick_x = right.stick.current.x,
        .stick_y = right.stick.current.y,
    }, &g_tank_control_state);
    if (!input_owned) return;

    auto* pitch = reinterpret_cast<float*>(g_tank_client_pitch);
    auto* yaw = reinterpret_cast<float*>(g_client_view_yaw_address);
    if (writable_range(pitch, sizeof(float)) && writable_range(yaw, sizeof(float)) &&
        std::isfinite(*pitch) && std::isfinite(*yaw)) {
        // CL angles are world view minus native ps.delta_angles. Clamping CL
        // directly would snap an offset tank even on a neutral sample. The
        // verified native vehicle path owns world/ground-relative limits.
        float native_rate = 0.0F;
        static_cast<void>(read_tank_value(context.vehicle_info + 0xFC, &native_rate));
        // Do not build an unreachable target faster than the physical cannon
        // can traverse, which would keep it moving after the stick releases.
        static_cast<void>(apply_tank_control_delta(
            limit_tank_control_delta(delta, native_rate), pitch, yaw,
            &command.view_angles));
    }
    if (left.stick.active && std::isfinite(left.stick.current.x) && std::isfinite(left.stick.current.y)) {
        const float x = std::clamp(left.stick.current.x, -1.0F, 1.0F);
        const float y = std::clamp(left.stick.current.y, -1.0F, 1.0F);
        const float magnitude = std::sqrt(x*x + y*y);
        if (magnitude > kControllerStickDeadzone) {
            const float scale = std::min(1.0F, (magnitude - kControllerStickDeadzone) / (1.0F - kControllerStickDeadzone)) / magnitude;
            const auto add_move = [](std::int8_t native, float value) noexcept {
                return static_cast<std::int8_t>(std::clamp(static_cast<int>(native) + static_cast<int>(std::lround(value * 127.0F)), -127, 127));
            };
            command.forward_move = add_move(command.forward_move, y * scale);
            command.right_move = add_move(command.right_move, x * scale);
        }
    }
    const bool firing = (right.trigger.active && std::isfinite(right.trigger.current) && right.trigger.current >= kControllerButtonThreshold) ||
        (right.trigger_click.active && right.trigger_click.current);
    if (firing) wawvr::t4::add_button(command, wawvr::t4::UsercmdButton::attack);
    // See2's OT-34 flamethrower is the native frag action, not ADS/handbrake.
    // Bypass the hand-thrown grenade gesture only while inside this tank.
    if (left.trigger.active && std::isfinite(left.trigger.current) && left.trigger.current >= kControllerButtonThreshold)
        wawvr::t4::add_button(command, wawvr::t4::UsercmdButton::frag_grenade);
    if (left.primary.active && left.primary.current) wawvr::t4::add_button(command, wawvr::t4::UsercmdButton::use);
    if (!g_logged_tank_controls) {
        input_diagnostic_log("TankDiag native tracked-tank controls active entity=%d: left=throttle/steer right=smooth-turret trigger=cannon; no HMD aim/snap/stance/handheld override", context.entity);
        g_logged_tank_controls = true;
    }
}

[[nodiscard]] bool bytes_match(
    const std::uint8_t* const actual,
    const std::span<const std::uint8_t> expected) noexcept {
    if (actual == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (actual[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool make_relative_instruction(
    const std::uintptr_t source,
    const std::uintptr_t destination,
    const std::uint8_t opcode,
    std::array<std::uint8_t, 5>* const jump) noexcept {
    if (jump == nullptr || source > std::numeric_limits<std::uint32_t>::max() ||
        destination > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const std::int64_t displacement =
        static_cast<std::int64_t>(destination) -
        static_cast<std::int64_t>(source + 5);
    if (displacement < std::numeric_limits<std::int32_t>::min() ||
        displacement > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }

    (*jump)[0] = opcode;
    const auto encoded = static_cast<std::int32_t>(displacement);
    std::memcpy(jump->data() + 1, &encoded, sizeof(encoded));
    return true;
}

[[nodiscard]] bool rollback_original(
    std::uint8_t* const target,
    const DWORD old_protection,
    const std::span<const std::uint8_t> expected,
    InputHookInstallResult* const result) noexcept {
    if (expected.size() < kInlinePatchSize) {
        if (result != nullptr) {
            result->status = InputHookStatus::rollback_failed;
            result->system_error = ERROR_INVALID_DATA;
        }
        return false;
    }
    const auto original = expected.first(kInlinePatchSize);
    std::memcpy(target, original.data(), kInlinePatchSize);
    const bool bytes_restored = bytes_match(target, original);
    const bool cache_flushed =
        FlushInstructionCache(GetCurrentProcess(), target, kInlinePatchSize) !=
        FALSE;
    DWORD ignored = 0;
    const bool protection_restored =
        VirtualProtect(target, kInlinePatchSize, old_protection, &ignored) !=
        FALSE;
    if (!bytes_restored || !cache_flushed || !protection_restored) {
        if (result != nullptr) {
            result->status = InputHookStatus::rollback_failed;
            result->system_error = GetLastError();
        }
        return false;
    }
    return true;
}

[[nodiscard]] InputHookInstallResult patch_target(
    const wawvr::t4::PreparedInlineHook& prepared,
    const std::array<std::uint8_t, 5>& target_jump,
    const std::uintptr_t trampoline) noexcept {
    InputHookInstallResult result{};
    result.status = InputHookStatus::preparation_failed;
    result.target = prepared.target;
    result.trampoline = trampoline;

    SuspendedProcessThreads suspended;
    if (!suspended.open_all(&result) ||
        !suspended.suspend_and_validate(
            prepared.target, kInlinePatchSize, &result)) {
        return result;
    }

    auto* const target = reinterpret_cast<std::uint8_t*>(prepared.target);
    if (!bytes_match(target, prepared.expected_bytes())) {
        result.status = InputHookStatus::expected_bytes_changed;
        return result;
    }

    DWORD old_protection = 0;
    if (!VirtualProtect(
            target, kInlinePatchSize, PAGE_EXECUTE_READWRITE,
            &old_protection)) {
        result.status = InputHookStatus::target_protection_failed;
        result.system_error = GetLastError();
        return result;
    }

    if (!bytes_match(target, prepared.expected_bytes())) {
        DWORD ignored = 0;
        if (!VirtualProtect(
                target, kInlinePatchSize, old_protection, &ignored)) {
            result.status = InputHookStatus::protection_restore_failed;
            result.system_error = GetLastError();
        } else {
            result.status = InputHookStatus::expected_bytes_changed;
        }
        return result;
    }

    std::memcpy(target, target_jump.data(), target_jump.size());
    if (!bytes_match(target, target_jump)) {
        result.status = InputHookStatus::patch_write_failed;
        static_cast<void>(
            rollback_original(
                target, old_protection, prepared.expected_bytes(), &result));
        return result;
    }
    if (!FlushInstructionCache(
            GetCurrentProcess(), target, kInlinePatchSize)) {
        result.status = InputHookStatus::patch_cache_flush_failed;
        result.system_error = GetLastError();
        static_cast<void>(
            rollback_original(
                target, old_protection, prepared.expected_bytes(), &result));
        return result;
    }

    DWORD ignored = 0;
    if (!VirtualProtect(
            target, kInlinePatchSize, old_protection, &ignored)) {
        result.status = InputHookStatus::protection_restore_failed;
        result.system_error = GetLastError();
        static_cast<void>(
            rollback_original(
                target, old_protection, prepared.expected_bytes(), &result));
        return result;
    }

    result.status = InputHookStatus::installed;
    result.system_error = 0;
    return result;
}

#if defined(_MSC_VER) && defined(_M_IX86)
__declspec(naked) void input_detour_bridge() {
    __asm {
        // This is a mid-function contract, so preserve flags, every x86 GPR,
        // and the complete x87/MMX/SSE state around the ordinary C++ thunk.
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        push eax
        call wawvr_apply_controller_usercmd_from_bridge
        add esp, 4
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [g_input_trampoline]
    }
}

__declspec(naked) void mp_create_cmd_detour_bridge() {
    __asm {
        // The replaced instruction is `call CL_CreateCmd`. Preserve its
        // usercall contract (destination in EAX, local client in ESI), then
        // service the completed 0x2C command returned in EAX before native MP
        // copies it into the command ring.
        call dword ptr [g_mp_original_create_cmd]
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, 0FFFFFFF0h
        fxsave [esp]
        push eax
        call wawvr_apply_controller_usercmd_mp_from_bridge
        add esp, 4
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        ret
    }
}
#endif

}  // namespace

bool controller_tank_controls_active() noexcept {
    TankContext context{};
    return read_tank_context(&context);
}

bool read_controller_tank_aim_target(
    wawvr::xr::Vec3f* const target_world) noexcept {
    if (target_world == nullptr) return false;
    *target_world = {};
    TankContext context{};
    wawvr::xr::Vec3f target{};
    if (!read_tank_context(&context) ||
        !read_tank_value(context.vehicle + 0x400, &target) ||
        !std::isfinite(target.x) || !std::isfinite(target.y) ||
        !std::isfinite(target.z) ||
        (target.x == 0.0F && target.y == 0.0F && target.z == 0.0F)) {
        return false;
    }
    *target_world = target;
    return true;
}

template <typename Command>
void apply_controller_usercmd_from_bridge(Command* const command) noexcept {
    if (!g_controller_input_enabled.load(std::memory_order_acquire) ||
        !writable_range(command, sizeof(*command))) {
        return;
    }

    if constexpr (std::is_same_v<Command, wawvr::t4::UsercmdSp>) {
        // This runs after the native command has been completely serialized.
        // Clear target-assisted knife rotation/lunge independently of OpenXR
        // frame freshness so a stale/disconnected controller cannot leak one
        // legacy melee-charge command into the HMD camera. MP has no verified
        // equivalent fields and intentionally skips this write.
        suppress_t4_melee_charge(*command);
    }
    if (g_gameplay_refdef_origin_address == 0 ||
        g_gameplay_refdef_axis_address == 0) {
        return;
    }
    const bool native_melee_before_vr = wawvr::t4::has_button(
        *command, wawvr::t4::UsercmdButton::melee);
    if (native_melee_before_vr && !g_native_melee_was_held) {
        input_diagnostic_log(
            "Native T4 melee entered the completed usercmd before VR input injection");
    }
    g_native_melee_was_held = native_melee_before_vr;

    ManualGrenadeGameplaySession grenade_gameplay_session =
        ManualGrenadeGameplaySession::unknown;
    std::uint32_t key_catchers = 0;
    std::int32_t connection_state = 0;
    bool key_catchers_readable = false;
    bool connection_state_readable = false;
    bool gameplay_state_readable = false;
    bool gameplay_controller_allowed = false;
    if (g_connection_state_address != 0 &&
        readable_range(
            reinterpret_cast<const void*>(g_connection_state_address),
            sizeof(connection_state))) {
        std::memcpy(
            &connection_state,
            reinterpret_cast<const void*>(g_connection_state_address),
            sizeof(connection_state));
        connection_state_readable = true;
        grenade_gameplay_session =
            connection_state == g_active_connection_state
            ? ManualGrenadeGameplaySession::active
            : ManualGrenadeGameplaySession::inactive;
    }
    if (g_key_catchers_address != 0 &&
        readable_range(
            reinterpret_cast<const void*>(g_key_catchers_address),
            sizeof(key_catchers))) {
        std::memcpy(
            &key_catchers,
            reinterpret_cast<const void*>(g_key_catchers_address),
            sizeof(key_catchers));
        key_catchers_readable = true;
    }
    gameplay_state_readable =
        key_catchers_readable && connection_state_readable;
    if (gameplay_state_readable) {
        gameplay_controller_allowed = controller_gameplay_input_allowed(
            key_catchers, connection_state, g_active_connection_state);
    }

    // Tanks consume an independent native vehicle command and never fall
    // through to handheld pose/ADS/reload or the final visible-gun override.
    if constexpr (std::is_same_v<Command, wawvr::t4::UsercmdSp>) {
        TankContext tank{};
        if (read_tank_context(&tank)) {
            g_satchel_input_state = {};
            ControllerFrameSnapshot tank_frame{};
            const auto now = GetTickCount64();
            const bool owned = read_controller_frame(&tank_frame) &&
                gameplay_controller_allowed && controller_frame_is_current(tank_frame, now);
            reset_snap_turn(&g_snap_turn_state);
            // Exiting a tank with the aim stick held must not become a snap.
            g_snap_turn_state.armed = false;
            reset_smooth_turn(&g_smooth_turn_state);
            reset_sprint_latch(&g_sprint_latch_state);
            reset_physical_melee_gesture(&g_physical_melee_state);
            g_right_stick_melee_was_held = false;
            apply_tank_command(*command, tank_frame, owned, now, tank);
            return;
        }
        g_tank_owned_entity = -1;
        reset_tank_control(&g_tank_control_state);
        if (apply_aircraft_controller_command(*command, gameplay_controller_allowed)) {
            g_satchel_input_state = {};
            reset_snap_turn(&g_snap_turn_state);
            g_snap_turn_state.armed = false;
            reset_smooth_turn(&g_smooth_turn_state);
            reset_sprint_latch(&g_sprint_latch_state);
            reset_physical_melee_gesture(&g_physical_melee_state);
            g_right_stick_melee_was_held = false;
            return;
        }
    }

    RuntimeWeaponDefinitionIdentity weapon_identity{};
    bool weapon_identity_valid = false;
    if constexpr (std::is_same_v<Command, wawvr::t4::UsercmdSp>) {
        weapon_identity_valid = read_runtime_sp_weapon_definition(
            static_cast<std::int32_t>(command->weapon), &weapon_identity);
    }
    // The selected campaign satchel uses a separate retail throw bit. Keep
    // native attack as the detonator and leave all original keyboard bits.
    const auto update_satchel_command = [&](const ControllerFrameSnapshot& frame,
                                            const bool input_owned,
                                            const bool new_press_blocked) noexcept {
        SatchelInputResult result{};
        if constexpr (std::is_same_v<Command, wawvr::t4::UsercmdSp>) {
            const auto& left = frame.frame.actions.hands[
                static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
            const auto& right = frame.frame.actions.hands[
                static_cast<std::uint32_t>(wawvr::xr::Hand::Right)];
            const bool was_held = g_satchel_input_state.trigger_held;
            result = update_satchel_input(SatchelInput{
                .gameplay_session_inactive = grenade_gameplay_session ==
                    ManualGrenadeGameplaySession::inactive,
                .weapon_context_valid = weapon_identity_valid,
                .weapon_index = command->weapon,
                .weapon_definition_identity = weapon_identity.definition_address,
                .weapon_name = weapon_identity.name.data(),
                .input_valid = input_owned && left.trigger.active,
                .right_grip_held = right.squeeze.active &&
                    std::isfinite(right.squeeze.current) &&
                    right.squeeze.current >= kWeaponGripEngage,
                .new_press_blocked = new_press_blocked,
                .left_trigger_value = left.trigger.current,
            }, &g_satchel_input_state);
            command->buttons = merge_satchel_native_throw(
                command->buttons, result.hold_native_throw);
            if (was_held != result.hold_native_throw) {
                input_diagnostic_log(
                    "Satchel left-trigger throw %s: weapon=%s buttons=0x%08X",
                    result.hold_native_throw ? "held" : "released",
                    weapon_identity.name.data(), command->buttons);
            }
        }
        return result;
    };

    ControllerFrameSnapshot snapshot{};
    if (!read_controller_frame(&snapshot)) {
        reset_smooth_turn(&g_smooth_turn_state);
        reset_sprint_latch(&g_sprint_latch_state);
        reset_physical_melee_gesture(&g_physical_melee_state);
        g_right_stick_melee_was_held = false;
        // Service grenade ownership even when Present has invalidated its
        // publication. This clears a stale held/pending gesture instead of
        // letting it survive invisibly until an unrelated future XR frame.
        const ControllerFrameSnapshot missing_snapshot{};
        const wawvr::xr::Vec3f unavailable_origin{};
        const wawvr::xr::Basis3f unavailable_axis{};
        static_cast<void>(update_manual_grenade_command(
            missing_snapshot, unavailable_origin, unavailable_axis,
            grenade_gameplay_session, false, true,
            command->offhand_index, &command->buttons));
        // A missing action publication must not release an already-held
        // satchel in the still-validated selected weapon context.
        static_cast<void>(update_satchel_command(missing_snapshot, false, true));
        return;
    }
    const std::uint64_t now_milliseconds = GetTickCount64();
    const bool controller_frame_current =
        controller_frame_is_current(snapshot, now_milliseconds);
    // A validated mounted-gun route gives the right controller exclusive
    // ownership of the barrel. Body-yaw catch-up and ordinary snap/smooth
    // turning would otherwise add a second right-stick/head-derived rotation
    // before the turret hooks consume the same controller ray.
    const bool mounted_controller_route =
        gameplay_controller_allowed && controller_frame_current &&
        mounted_gun_controller_route_available(
            snapshot, now_milliseconds);

    wawvr::xr::Vec3f camera_origin{};
    std::memcpy(
        &camera_origin,
        reinterpret_cast<const void*>(g_gameplay_refdef_origin_address),
        sizeof(camera_origin));
    wawvr::xr::Basis3f camera_axis{};
    std::memcpy(
        &camera_axis,
        reinterpret_cast<const void*>(g_gameplay_refdef_axis_address),
        sizeof(camera_axis));
    const auto& left_hand = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
    const bool sprint_latched = update_sprint_latch(
        gameplay_controller_allowed && controller_frame_current,
        snapshot.frame.actions.sequence,
        left_hand.stick,
        left_hand.stick_click,
        &g_sprint_latch_state);
    float turn_degrees = 0.0F;
    if (g_client_view_yaw_address != 0 && gameplay_state_readable &&
        writable_range(
            reinterpret_cast<void*>(g_client_view_yaw_address),
            sizeof(float))) {

        // A shouldered rifle owns its camera-local hand line. Never rebase
        // that coordinate frame underneath an active support pose; defer the
        // sprint body-yaw catch-up until the support hand releases.
        const bool sprint_body_yaw_sync_allowed =
            sprint_latched && !controller_weapon_uses_support_pose() &&
            !mounted_controller_route;
        if (automatic_body_yaw_sync_allowed(
                gameplay_controller_allowed, controller_frame_current,
                sprint_body_yaw_sync_allowed)) {
            float body_yaw_delta = 0.0F;
            if (controller_body_yaw_delta_degrees(
                    snapshot, &body_yaw_delta) &&
                std::abs(body_yaw_delta) >=
                    kBodyYawSyncMinimumDegrees) {
                auto desired_anchor = leveled_tracking_anchor(
                    snapshot.frame.head_center,
                    &snapshot.tracking_anchor.orientation);
                // Physical body catch-up must not redefine the room-scale
                // origin. Transfer yaw only; manual recenter remains the sole
                // owner of tracking-anchor position.
                desired_anchor.position = snapshot.tracking_anchor.position;

                Command prospective_command = *command;
                wawvr::xr::Basis3f prospective_axis = camera_axis;
                auto* const live_yaw = reinterpret_cast<float*>(
                    g_client_view_yaw_address);
                float prospective_yaw = *live_yaw;
                TrackingAnchorSyncLock anchor_transaction(
                    tracking_anchor_sync_mutex());
                if (apply_snap_turn_to_t4_command(
                        prospective_command, body_yaw_delta,
                        &prospective_yaw, &prospective_axis) &&
                    try_rebase_pending_stereo_tracking_anchor(
                        snapshot.frame.frame_id,
                        snapshot.tracking_anchor.orientation,
                        desired_anchor.orientation)) {
                    if (rebase_controller_frame_tracking_anchor(
                            snapshot.generation,
                            snapshot.frame.frame_id,
                            snapshot.tracking_anchor.orientation,
                            desired_anchor.orientation)) {
                        const float yaw_before = *live_yaw;
                        *command = prospective_command;
                        *live_yaw = prospective_yaw;
                        camera_axis = prospective_axis;
                        snapshot.tracking_anchor = desired_anchor;
                        if (!g_logged_first_body_yaw_sync.exchange(
                                true, std::memory_order_acq_rel)) {
                            input_diagnostic_log(
                                "Physical HMD yaw transferred into T4 body without moving the visible view: delta=%.2f yaw=%.2f->%.2f",
                                body_yaw_delta, yaw_before,
                                prospective_yaw);
                        }
                    } else {
                        // The controller publication changed after it was
                        // sampled. Restore the exact stereo publication and
                        // leave native body/view state untouched.
                        static_cast<void>(
                            try_rebase_pending_stereo_tracking_anchor(
                                snapshot.frame.frame_id,
                                desired_anchor.orientation,
                                snapshot.tracking_anchor.orientation));
                    }
                }
            }
        }

        const auto& right_stick = snapshot.frame.actions.hands[
            static_cast<std::uint32_t>(wawvr::xr::Hand::Right)].stick;
        const bool right_stick_valid =
            controller_frame_current && right_stick.active &&
            std::isfinite(right_stick.current.x) &&
            std::isfinite(right_stick.current.y);
        const float right_x = right_stick_valid
                                  ? std::clamp(right_stick.current.x, -1.0F, 1.0F)
                                  : 0.0F;
        const float right_y = right_stick_valid
                                  ? std::clamp(right_stick.current.y, -1.0F, 1.0F)
                                  : 0.0F;
        const bool right_stick_deflected =
            right_stick_valid &&
            (std::abs(right_x) >= kControllerStickDeadzone ||
             std::abs(right_y) >= kControllerStickDeadzone);
        const bool horizontal_turn_intent =
            right_stick_valid &&
            std::abs(right_x) >= (g_turn_mode == TurnMode::Smooth
                ? kSmoothTurnDeadzone
                : kSnapTurnEngageThreshold) &&
            std::abs(right_x) >=
                std::abs(right_y) + kSnapTurnVerticalDominanceMargin;

        if (right_stick_deflected &&
            !g_logged_first_right_stick.exchange(
                true, std::memory_order_acq_rel)) {
            input_diagnostic_log(
                "Right-stick action live: x=%.2f y=%.2f connectionState=%d keyCatchers=0x%08X",
                right_x, right_y, connection_state, key_catchers);
        }
        if (!right_stick_valid ||
            std::abs(right_x) < kSnapTurnReleaseThreshold) {
            g_snap_gate_diagnostic_latched = false;
        }

        if (controller_frame_current && !mounted_controller_route &&
            snap_turn_gameplay_allowed(
                key_catchers, connection_state,
                g_active_connection_state)) {
            if (g_turn_mode == TurnMode::Smooth) {
                reset_snap_turn(&g_snap_turn_state);
                turn_degrees = consume_smooth_turn_degrees(
                    true, snapshot.frame.actions.sequence,
                    now_milliseconds, right_stick,
                    &g_smooth_turn_state);
            } else {
                reset_smooth_turn(&g_smooth_turn_state);
                turn_degrees = consume_snap_turn_degrees(
                    right_stick, &g_snap_turn_state);
            }
            if (turn_degrees != 0.0F) {
                auto* const live_yaw = reinterpret_cast<float*>(
                    g_client_view_yaw_address);
                const float yaw_before = *live_yaw;
                if (!apply_snap_turn_to_t4_command(
                        *command, turn_degrees, live_yaw, &camera_axis)) {
                    input_diagnostic_log(
                        "%s turn rejected by T4 yaw/camera validation: stick=(%.2f, %.2f) delta=%.3f",
                        g_turn_mode == TurnMode::Smooth ? "Smooth" : "Snap",
                        right_x, right_y, turn_degrees);
                    turn_degrees = 0.0F;
                    if (g_turn_mode == TurnMode::Smooth) {
                        reset_smooth_turn(&g_smooth_turn_state);
                    } else {
                        g_snap_turn_state.armed = false;
                    }
                } else if (g_turn_mode == TurnMode::Snap ||
                           !g_logged_first_smooth_turn.exchange(
                               true, std::memory_order_acq_rel)) {
                    input_diagnostic_log(
                        "%s turn applied: stick=(%.2f, %.2f) delta=%.3f yaw=%.1f->%.1f commandYaw=0x%04X",
                        g_turn_mode == TurnMode::Smooth ? "Smooth" : "Snap",
                        right_x, right_y, turn_degrees, yaw_before, *live_yaw,
                        static_cast<unsigned int>(command->view_angles[1]) &
                            0xFFFFU);
                }
            }
        } else {
            // UI/focus/staleness suppression consumes the held direction.
            // A neutral stick must be observed after gameplay resumes before
            // another snap can fire.
            g_snap_turn_state.armed = false;
            reset_smooth_turn(&g_smooth_turn_state);
            if (horizontal_turn_intent &&
                !mounted_controller_route &&
                !g_snap_gate_diagnostic_latched) {
                input_diagnostic_log(
                    "%s turn gated: connectionState=%d (requires %d) keyCatchers=0x%08X blockedMask=0x%02X",
                    g_turn_mode == TurnMode::Smooth ? "Smooth" : "Snap",
                    connection_state, g_active_connection_state, key_catchers,
                    kSnapTurnBlockedKeyCatcherMask);
                g_snap_gate_diagnostic_latched = true;
            }
        }
    } else {
        g_snap_turn_state.armed = false;
        reset_smooth_turn(&g_smooth_turn_state);
    }
    ControllerInputResult applied{};
    if (gameplay_controller_allowed) {
        applied = apply_controller_input(
            *command, snapshot, camera_axis, now_milliseconds);
    }
    if (applied.melee_button_held && !g_right_stick_melee_was_held) {
        input_diagnostic_log(
            "Right-stick click triggered native T4 melee: actionSequence=%llu",
            static_cast<unsigned long long>(
                snapshot.frame.actions.sequence));
    }
    g_right_stick_melee_was_held = applied.melee_button_held;
    if (applied.weapon_aim_applied) {
        // Capture the raw right-controller ray before the ordinary handheld
        // two-hand/viewmodel path can replace it. Mounted guns suppress that
        // viewmodel and must use the same stock-body-space ray for their
        // visible tags and authoritative server firing.
        publish_mounted_gun_controller_aim(
            applied.weapon_pitch_degrees,
            applied.weapon_yaw_degrees,
            snapshot.generation,
            now_milliseconds);
    }
    // These adjacent policies concern the same command and contain no native
    // engine call. Resolve its exact weapon identity once, then consume all
    // flags before grenade/native work can advance the gameplay state.
    const ManualReloadGameplayPolicy reload_policy =
        read_manual_reload_gameplay_policy(static_cast<std::int32_t>(command->weapon));
    const bool exact_pistol = weapon_identity_valid &&
        pistol_support_pose_for_weapon_name_buffer({
            weapon_identity.name.data(), weapon_identity.name.size()});
    bool native_pistol_idle = false;
    if constexpr (std::is_same_v<Command, wawvr::t4::UsercmdSp>) {
        // The validated predicted-player-state binding is also used by the
        // tank router. This is the same native weapon-state word already read
        // by the pistol glove path; zero excludes reload/switch animations,
        // including automatic reloads that reserve no manual-reload hands.
        std::int32_t native_weapon_state = -1;
        native_pistol_idle = exact_pistol &&
            g_tank_predicted_ps != 0 &&
            read_tank_value(g_tank_predicted_ps + 0x108, &native_weapon_state) &&
            native_weapon_state == 0;
    }
    const bool single_hand_pistol_melee_allowed = native_pistol_idle &&
        !reload_policy.force_reload_button &&
        !reload_policy.reserves_right_grip &&
        !reload_policy.reserves_left_grip &&
        !reload_policy.blocks_new_left_trigger_action &&
        !manual_grenade_reserves_left_hand() &&
        !wawvr::t4::has_button(*command, wawvr::t4::UsercmdButton::reload);
    const std::uint64_t melee_pistol_identity = single_hand_pistol_melee_allowed
        ? (static_cast<std::uint64_t>(weapon_identity.definition_address) << 32U) |
            static_cast<std::uint32_t>(weapon_identity.weapon_index)
        : 0;
    const PhysicalMeleeGesture physical_melee = update_physical_melee_gesture(
        gameplay_controller_allowed && controller_frame_current,
        snapshot, now_milliseconds, &g_physical_melee_state,
        controller_weapon_uses_support_pose(), melee_pistol_identity);
    if (reload_policy.force_reload_button) {
        wawvr::t4::add_button(
            *command, wawvr::t4::UsercmdButton::reload);
        applied.gameplay_buttons_applied = true;
    }
    if (reload_policy.blocks_attack) {
        command->buttons &=
            ~wawvr::t4::button_mask(wawvr::t4::UsercmdButton::attack);
        applied.weapon_trigger_applied = false;
    }
    const bool left_reload_interaction_reserved =
        reload_policy.reserves_left_grip;
    const bool left_reload_trigger_reserved =
        reload_policy.blocks_new_left_trigger_action;
    const auto satchel_update = update_satchel_command(
        snapshot, gameplay_controller_allowed && controller_frame_current,
        left_reload_interaction_reserved || left_reload_trigger_reserved ||
            manual_grenade_reserves_left_hand());
    if (satchel_update.hold_native_throw) {
        applied.gameplay_buttons_applied = true;
    }
    const ManualGrenadeCommandUpdate grenade_update =
        update_manual_grenade_command(
            snapshot, camera_origin, camera_axis,
            grenade_gameplay_session, gameplay_controller_allowed,
            left_reload_interaction_reserved ||
                left_reload_trigger_reserved || satchel_update.reserve_left_trigger,
            command->offhand_index, &command->buttons);
    if (grenade_update.native_button_injected) {
        applied.gameplay_buttons_applied = true;
    }
    const bool left_interaction_reserved =
        left_reload_interaction_reserved ||
        grenade_update.left_hand_reserved || satchel_update.reserve_left_trigger;
    if (left_interaction_reserved) {
        // The same left controls that own a detached magazine or pistol slide
        // or a physically held grenade must not also drive T4's authored
        // support pose. Right-stick stance/jump gestures are queued
        // independently from the post-Com_Frame native command path.
        command->buttons &= ~wawvr::t4::button_mask(
            wawvr::t4::UsercmdButton::aim_down_sights);
        command->buttons &= ~wawvr::t4::button_mask(
            wawvr::t4::UsercmdButton::jump);
    }
    if constexpr (std::is_same_v<Command, wawvr::t4::UsercmdSp>) {
        const bool native_support_animation_allowed =
            !weapon_identity_valid ||
            native_support_animation_allowed_for_weapon_name(
                weapon_identity.name.data());
        const bool support_pose_active =
            controller_weapon_uses_support_pose();
        const ControllerPhysicalScopeIdentity scope_identity =
            classify_controller_weapon_physical_scope(
                static_cast<std::int32_t>(command->weapon));
        if (scope_identity !=
                ControllerPhysicalScopeIdentity::unscoped ||
            !native_support_animation_allowed) {
            // Exact physical scopes must never enter T4's native sniper ADS:
            // that path hides the tracked rifle, queues a full-screen mask,
            // and narrows refdef.scissorViewport. Treat a transient identity
            // read failure as scoped-safe too, preventing a one-frame black
            // flash before the render thread publishes its support pose. The
            // Exact policy-selected weapons also suppress only T4's authored
            // ADS animation so it cannot fight the controller-owned two-hand
            // orientation. Their logical support latch and fixed ADS spread
            // remain active through independent paths.
            command->buttons &= ~wawvr::t4::button_mask(
                wawvr::t4::UsercmdButton::aim_down_sights);
            if (!native_support_animation_allowed && support_pose_active &&
                !g_logged_first_controller_owned_support.exchange(
                    true, std::memory_order_acq_rel)) {
                input_diagnostic_log(
                    "Two-hand support remains controller-owned; native ADS animation suppressed for %s",
                    weapon_identity.name.data());
            }
        } else if (!left_interaction_reserved &&
                   support_pose_active) {
            // Unscoped weapons retain T4's authored support animation without
            // changing which hand owns the rifle.
            wawvr::t4::add_button(
                *command, wawvr::t4::UsercmdButton::aim_down_sights);
            applied.gameplay_buttons_applied = true;
        }
    } else if (!left_interaction_reserved &&
               controller_weapon_uses_support_pose()) {
        // MP has no verified WeaponDef identity reader. Preserve its prior
        // support-pose behavior rather than consulting SP-only addresses.
        wawvr::t4::add_button(
            *command, wawvr::t4::UsercmdButton::aim_down_sights);
        applied.gameplay_buttons_applied = true;
    }
    if (sprint_latched) {
        const bool native_or_physical_sprint_already_set =
            wawvr::t4::has_button(
                *command, wawvr::t4::UsercmdButton::sprint);
        wawvr::t4::add_button(
            *command, wawvr::t4::UsercmdButton::sprint);
        applied.gameplay_buttons_applied =
            applied.gameplay_buttons_applied ||
            !native_or_physical_sprint_already_set;
        if (!g_logged_first_sprint_latch.exchange(
                true, std::memory_order_acq_rel)) {
            OutputDebugStringA(
                "WorldAtWarVR: L3 click-to-sprint latch active until locomotion stick returns to neutral\n");
        }
    }
    if (physical_melee != PhysicalMeleeGesture::none) {
        const bool melee_already_set = wawvr::t4::has_button(
            *command, wawvr::t4::UsercmdButton::melee);
        wawvr::t4::add_button(
            *command, wawvr::t4::UsercmdButton::melee);
        if constexpr (std::is_same_v<Command, wawvr::t4::UsercmdSp>) {
            suppress_t4_melee_charge(*command);
            applied.melee_comfort_applied = true;
        }
        applied.gameplay_buttons_applied =
            applied.gameplay_buttons_applied || !melee_already_set;
        input_diagnostic_log(
            "Physical %s triggered native T4 melee: actionSequence=%llu sampleMs=%llu relativeTravel=%.3fm worldTravel=%.3fm speed=%.2fm/s outward=%.3fm grips=%d/%d",
            physical_melee == PhysicalMeleeGesture::two_hand_thrust
                ? "two-hand rifle thrust"
                : physical_melee == PhysicalMeleeGesture::right_hand_pistol_strike
                    ? "right-hand held-pistol strike"
                    : "right-controller swing",
            static_cast<unsigned long long>(
                snapshot.frame.actions.sequence),
            static_cast<unsigned long long>(snapshot.publication_milliseconds),
            g_physical_melee_state.last_trigger_relative_travel_meters,
            g_physical_melee_state.last_trigger_world_travel_meters,
            g_physical_melee_state.last_trigger_speed_meters_per_second,
            g_physical_melee_state.last_trigger_outward_travel_meters,
            g_physical_melee_state.right_grip_latched ? 1 : 0,
            g_physical_melee_state.left_grip_latched ? 1 : 0);
    }
    if (applied.weapon_aim_applied) {
        float final_pitch = 0.0F;
        float final_yaw = 0.0F;
        if (read_final_visible_weapon_aim(
                snapshot.generation, now_milliseconds,
                &final_pitch, &final_yaw)) {
            final_yaw += turn_degrees;
            applied.weapon_aim_applied = wawvr::t4::apply_vr_weapon_aim(
                *command, final_pitch, final_yaw,
                applied.weapon_trigger_applied);
            if (applied.weapon_aim_applied) {
                applied.weapon_pitch_degrees = final_pitch;
                applied.weapon_yaw_degrees = final_yaw;
            }
        }
    }

    if constexpr (std::is_same_v<Command, wawvr::t4::UsercmdMp>) {
        if (applied.weapon_aim_applied &&
            g_cgame_gun_pitch_address != 0 &&
            g_cgame_gun_yaw_address != 0 &&
            writable_range(
                reinterpret_cast<void*>(g_cgame_gun_pitch_address),
                sizeof(float)) &&
            writable_range(
                reinterpret_cast<void*>(g_cgame_gun_yaw_address),
                sizeof(float))) {
            std::memcpy(
                reinterpret_cast<void*>(g_cgame_gun_pitch_address),
                &applied.weapon_pitch_degrees, sizeof(float));
            std::memcpy(
                reinterpret_cast<void*>(g_cgame_gun_yaw_address),
                &applied.weapon_yaw_degrees, sizeof(float));
        }
    }

    if (applied.movement_applied &&
        !g_logged_first_movement.exchange(true, std::memory_order_acq_rel)) {
        OutputDebugStringA(
            "WorldAtWarVR: first focused HMD-oriented controller movement applied\n");
    }
    if (applied.weapon_aim_applied &&
        !g_logged_first_aim.exchange(true, std::memory_order_acq_rel)) {
        OutputDebugStringA(
            "WorldAtWarVR: first tracked right-controller gun aim applied to T4 usercmd\n");
    }
}

extern "C" void __cdecl wawvr_apply_controller_usercmd_from_bridge(
    wawvr::t4::UsercmdSp* const command) noexcept {
    apply_controller_usercmd_from_bridge(command);
}

extern "C" void __cdecl wawvr_apply_controller_usercmd_mp_from_bridge(
    wawvr::t4::UsercmdMp* const command) noexcept {
    apply_controller_usercmd_from_bridge(command);
}

InputHookInstallResult install_controller_input_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    InputHookInstallResult result{};
    if (g_controller_input_installed.load(std::memory_order_acquire)) {
        result.status = InputHookStatus::already_installed;
        return result;
    }
    if (input_disabled_by_environment()) {
        result.status = InputHookStatus::disabled_by_environment;
        return result;
    }

#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    result.status = InputHookStatus::unsupported_compiler_or_architecture;
    return result;
#else
    const auto& bound = bindings.profile();
    const auto layout = select_t4_layout_family(bound);
    if (layout == T4LayoutFamily::unsupported) {
        result.status = InputHookStatus::rejected_wrong_profile;
        return result;
    }
    const bool multiplayer =
        layout == T4LayoutFamily::multiplayer_1_7_1263;

    const auto* const refdef = bindings.data_symbol(
        wawvr::t4::DataSymbolId::gameplay_refdef_viewport);
    const auto camera_origin = refdef == nullptr
        ? std::optional<std::uintptr_t>{}
        : bindings.module().address(
              static_cast<wawvr::t4::Rva>(
                  refdef->rva + kGameplayRefdefOriginOffset),
              sizeof(wawvr::xr::Vec3f));
    const auto camera_axis = refdef == nullptr
        ? std::optional<std::uintptr_t>{}
        : bindings.module().address(
              static_cast<wawvr::t4::Rva>(
                  refdef->rva + kGameplayRefdefAxisOffset),
              sizeof(wawvr::xr::Basis3f));
    if (!camera_origin.has_value() || !camera_axis.has_value()) {
        result.status = InputHookStatus::camera_axis_out_of_range;
        return result;
    }
    const auto client_view_yaw = bindings.data_address(
        wawvr::t4::DataSymbolId::client_view_yaw_degrees, sizeof(float));
    const auto key_catchers = bindings.data_address(
        wawvr::t4::DataSymbolId::key_catchers, sizeof(std::uint32_t));
    const auto connection_state = bindings.data_address(
        wawvr::t4::DataSymbolId::connection_state, sizeof(std::int32_t));
    if (!client_view_yaw.has_value() || !key_catchers.has_value() ||
        !connection_state.has_value()) {
        result.status = InputHookStatus::snap_turn_data_out_of_range;
        return result;
    }

    std::optional<std::uintptr_t> cgame_gun_pitch{};
    std::optional<std::uintptr_t> cgame_gun_yaw{};
    if (multiplayer) {
        cgame_gun_pitch = bindings.data_address(
            wawvr::t4::DataSymbolId::cgame_gun_pitch_degrees,
            sizeof(float));
        cgame_gun_yaw = bindings.data_address(
            wawvr::t4::DataSymbolId::cgame_gun_yaw_degrees,
            sizeof(float));
        if (!cgame_gun_pitch.has_value() || !cgame_gun_yaw.has_value()) {
            result.status = InputHookStatus::snap_turn_data_out_of_range;
            return result;
        }
    }

    const auto prepared = wawvr::t4::prepare_inline_hook(
        bindings, wawvr::t4::HookSiteId::post_build_usercmd,
        multiplayer
            ? reinterpret_cast<std::uintptr_t>(
                  &mp_create_cmd_detour_bridge)
            : reinterpret_cast<std::uintptr_t>(&input_detour_bridge));
    if (!prepared.ok()) {
        result.status = InputHookStatus::preparation_failed;
        return result;
    }
    result.target = prepared.hook->target;
    const bool sp_boundary =
        !multiplayer &&
        std::equal(
            kPostBuildUsercmdDisplacedInstruction.begin(),
            kPostBuildUsercmdDisplacedInstruction.end(),
            prepared.hook->expected.begin());
    const bool mp_boundary =
        multiplayer && prepared.hook->expected[0] == 0xE8;
    if (prepared.hook->minimum_patch_bytes != kInlinePatchSize ||
        prepared.hook->expected_size < kInlinePatchSize ||
        (!sp_boundary && !mp_boundary)) {
        result.status = InputHookStatus::unexpected_instruction_boundary;
        return result;
    }

    g_gameplay_refdef_origin_address = *camera_origin;
    g_gameplay_refdef_axis_address = *camera_axis;
    g_client_view_yaw_address = *client_view_yaw;
    if (!multiplayer) {
        bind_aircraft_controls(&bindings);
        // Retail 1.7 hash/profile validated above. Pitch writes are verified at
        // 0x42DB26/0x42E886; vehicle info layout at VEH_UpdateWeapon 0x53A750.
        g_tank_predicted_ps = bindings.data_address(wawvr::t4::DataSymbolId::predicted_player_state, 0x840).value_or(0);
        g_tank_entities = bindings.data_address(wawvr::t4::DataSymbolId::local_player_entity, 0x378).value_or(0);
        g_tank_vehicle_info = bindings.module().address(0x04380D80, 0x8FC).value_or(0);
        g_tank_client_pitch = bindings.module().address(0x02C7D6D0, sizeof(float)).value_or(0);
    }
    g_key_catchers_address = *key_catchers;
    g_connection_state_address = *connection_state;
    g_cgame_gun_pitch_address = cgame_gun_pitch.value_or(0);
    g_cgame_gun_yaw_address = cgame_gun_yaw.value_or(0);
    g_active_connection_state = multiplayer
        ? kT4MpActiveConnectionState
        : kT4SpActiveConnectionState;
    g_turn_mode = requested_turn_mode();
    g_satchel_input_state = {};
    reset_snap_turn(&g_snap_turn_state);
    reset_smooth_turn(&g_smooth_turn_state);
    reset_sprint_latch(&g_sprint_latch_state);
    reset_physical_melee_gesture(&g_physical_melee_state);
    g_right_stick_melee_was_held = false;
    g_native_melee_was_held = false;
    g_logged_first_sprint_latch.store(false, std::memory_order_release);
    g_logged_first_smooth_turn.store(false, std::memory_order_release);
    g_logged_first_body_yaw_sync.store(false, std::memory_order_release);

    const auto clear_bound_state = []() noexcept {
        bind_aircraft_controls(nullptr);
        g_input_trampoline = nullptr;
        g_mp_original_create_cmd = nullptr;
        g_gameplay_refdef_origin_address = 0;
        g_gameplay_refdef_axis_address = 0;
        g_client_view_yaw_address = 0;
        g_tank_predicted_ps = 0;
        g_tank_entities = 0;
        g_tank_vehicle_info = 0;
        g_tank_client_pitch = 0;
        g_key_catchers_address = 0;
        g_connection_state_address = 0;
        g_cgame_gun_pitch_address = 0;
        g_cgame_gun_yaw_address = 0;
        g_active_connection_state = kT4SpActiveConnectionState;
        reset_sprint_latch(&g_sprint_latch_state);
        reset_physical_melee_gesture(&g_physical_melee_state);
        g_right_stick_melee_was_held = false;
        g_native_melee_was_held = false;
    };

    if (multiplayer) {
        std::int32_t original_displacement = 0;
        std::memcpy(
            &original_displacement, prepared.hook->expected.data() + 1,
            sizeof(original_displacement));
        const std::int64_t original_value =
            static_cast<std::int64_t>(prepared.hook->target + 5) +
            original_displacement;
        const auto module_begin = reinterpret_cast<std::uintptr_t>(
            bindings.module().base);
        const auto module_size = bindings.module().size;
        if (original_value < 0 ||
            static_cast<std::uint64_t>(original_value) < module_begin ||
            static_cast<std::uint64_t>(original_value) - module_begin >=
                module_size) {
            clear_bound_state();
            result.status = InputHookStatus::unexpected_instruction_boundary;
            return result;
        }
        const auto original = static_cast<std::uintptr_t>(original_value);
        std::array<std::uint8_t, 5> target_call{};
        if (!make_relative_instruction(
                prepared.hook->target,
                reinterpret_cast<std::uintptr_t>(
                    &mp_create_cmd_detour_bridge),
                0xE8, &target_call)) {
            clear_bound_state();
            result.status = InputHookStatus::jump_out_of_range;
            return result;
        }
        g_mp_original_create_cmd = reinterpret_cast<void*>(original);
        result = patch_target(*prepared.hook, target_call, original);
        if (!result.ok()) {
            clear_bound_state();
            return result;
        }
    } else {
        auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr, kTrampolineSize, MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE));
        if (trampoline == nullptr) {
            clear_bound_state();
            result.status = InputHookStatus::trampoline_allocation_failed;
            result.system_error = GetLastError();
            return result;
        }
        std::memcpy(
            trampoline, kPostBuildUsercmdDisplacedInstruction.data(),
            kInlinePatchSize);

        std::array<std::uint8_t, 5> return_jump{};
        std::array<std::uint8_t, 5> target_jump{};
        if (!make_relative_instruction(
                reinterpret_cast<std::uintptr_t>(trampoline) +
                    kInlinePatchSize,
                prepared.hook->target + kInlinePatchSize, 0xE9,
                &return_jump) ||
            !make_relative_instruction(
                prepared.hook->target,
                reinterpret_cast<std::uintptr_t>(&input_detour_bridge),
                0xE9, &target_jump)) {
            clear_bound_state();
            VirtualFree(trampoline, 0, MEM_RELEASE);
            result.status = InputHookStatus::jump_out_of_range;
            return result;
        }
        std::memcpy(
            trampoline + kInlinePatchSize, return_jump.data(),
            return_jump.size());

        DWORD old_trampoline_protection = 0;
        if (!VirtualProtect(
                trampoline, kTrampolineSize, PAGE_EXECUTE_READ,
                &old_trampoline_protection)) {
            clear_bound_state();
            result.status = InputHookStatus::trampoline_protection_failed;
            result.system_error = GetLastError();
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return result;
        }
        if (!FlushInstructionCache(
                GetCurrentProcess(), trampoline, kTrampolineSize)) {
            clear_bound_state();
            result.status = InputHookStatus::trampoline_cache_flush_failed;
            result.system_error = GetLastError();
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return result;
        }

        g_input_trampoline = trampoline;
        result = patch_target(
            *prepared.hook, target_jump,
            reinterpret_cast<std::uintptr_t>(trampoline));
        if (!result.ok()) {
            clear_bound_state();
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return result;
        }
    }

    if (!result.ok()) {
        clear_bound_state();
        return result;
    }

    g_controller_input_installed.store(true, std::memory_order_release);
    g_controller_input_enabled.store(true, std::memory_order_release);
    return result;
#endif
}

const char* input_hook_status_name(const InputHookStatus status) noexcept {
    switch (status) {
    case InputHookStatus::installed: return "installed";
    case InputHookStatus::already_installed: return "already-installed";
    case InputHookStatus::disabled_by_environment: return "disabled-by-environment";
    case InputHookStatus::rejected_wrong_profile: return "rejected-wrong-profile";
    case InputHookStatus::unsupported_compiler_or_architecture: return "unsupported-compiler-or-architecture";
    case InputHookStatus::camera_axis_out_of_range: return "camera-axis-out-of-range";
    case InputHookStatus::snap_turn_data_out_of_range: return "snap-turn-data-out-of-range";
    case InputHookStatus::preparation_failed: return "preparation-failed";
    case InputHookStatus::unexpected_instruction_boundary: return "unexpected-instruction-boundary";
    case InputHookStatus::trampoline_allocation_failed: return "trampoline-allocation-failed";
    case InputHookStatus::trampoline_protection_failed: return "trampoline-protection-failed";
    case InputHookStatus::trampoline_cache_flush_failed: return "trampoline-cache-flush-failed";
    case InputHookStatus::jump_out_of_range: return "jump-out-of-range";
    case InputHookStatus::thread_snapshot_failed: return "thread-snapshot-failed";
    case InputHookStatus::thread_open_failed: return "thread-open-failed";
    case InputHookStatus::thread_suspend_failed: return "thread-suspend-failed";
    case InputHookStatus::thread_context_failed: return "thread-context-failed";
    case InputHookStatus::target_thread_inside_patch: return "target-thread-inside-patch";
    case InputHookStatus::expected_bytes_changed: return "expected-bytes-changed";
    case InputHookStatus::target_protection_failed: return "target-protection-failed";
    case InputHookStatus::patch_write_failed: return "patch-write-failed";
    case InputHookStatus::patch_cache_flush_failed: return "patch-cache-flush-failed";
    case InputHookStatus::protection_restore_failed: return "protection-restore-failed";
    case InputHookStatus::rollback_failed: return "rollback-failed";
    }
    return "unknown";
}

void request_controller_input_shutdown() noexcept {
    g_controller_input_enabled.store(false, std::memory_order_release);
    reset_sprint_latch(&g_sprint_latch_state);
    reset_physical_melee_gesture(&g_physical_melee_state);
    g_right_stick_melee_was_held = false;
    g_native_melee_was_held = false;
}

bool controller_input_hook_installed() noexcept {
    return g_controller_input_installed.load(std::memory_order_acquire);
}

bool controller_input_hook_enabled() noexcept {
    return g_controller_input_enabled.load(std::memory_order_acquire);
}

}  // namespace wawvr::mod
