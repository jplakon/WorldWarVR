// SPDX-License-Identifier: GPL-3.0-only
#include "manual_grenade_runtime.hpp"

#include "button_grenade_logic.hpp"
#include "input_mapping.hpp"
#include "manual_grenade_logic.hpp"
#include "grenade_native_observation.hpp"
#include "grenade_queue_receipts.hpp"
#include "grenade_observer_memory.hpp"
#include "grenade_observer_patch_logic.hpp"
#include "peer_thread_quiescence.hpp"
#include "stereo_diagnostics.hpp"
#include "t4_layout_selector.hpp"

#include "t4/profile.hpp"
#include "t4/usercmd.hpp"
#include "xr_math.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>

namespace wawvr::mod {
namespace {

constexpr wawvr::t4::Rva kFireGrenadeCallRva = 0x00150413;
constexpr wawvr::t4::Rva kFireGrenadeTargetRva = 0x0010B3B0;
constexpr wawvr::t4::Rva kFireGrenadeContextRva = 0x001503FC;
constexpr std::size_t kFireGrenadeCallOffset = 23;
constexpr std::array<std::uint8_t, 31> kFireGrenadeContext{
    0x8B, 0x54, 0x24, 0x28, 0x51, 0x8B, 0x4C, 0x24,
    0x30, 0x6A, 0x01, 0x51, 0x52, 0x8D, 0x4C, 0x24,
    0x24, 0x51, 0x83, 0xC0, 0x24, 0x50, 0x53, 0xE8,
    0x98, 0xAF, 0xFB, 0xFF, 0x83, 0xC4, 0x1C,
};
constexpr std::array<std::uint8_t, 19> kFireGrenadePrologue{
    0x53, 0x8B, 0x5C, 0x24, 0x14, 0x8B, 0x04, 0x9D,
    0x70, 0x67, 0x8F, 0x00, 0x55, 0x56, 0x57, 0x89,
    0x44, 0x24, 0x20,
};
constexpr std::size_t kFireGrenadeEpilogueOffset = 0x1AB;
constexpr std::array<std::uint8_t, 10> kFireGrenadeEpilogue{
    0x8B, 0xC5, 0x83, 0xC4, 0x10,
    0x5F, 0x5E, 0x5D, 0x5B, 0xC3,
};

// Retail SP G_LocationalTrace has a custom calling convention: the ignored
// entity number is passed in EDX while the remaining five arguments are
// caller-cleaned stack arguments.  Gate both the complete wrapper and its
// priority map before exposing the grenade hook; a mismatched executable must
// retain the native path instead of calling an inferred ABI.
constexpr wawvr::t4::Rva kLocationalTraceRva = 0x001043D0;
constexpr wawvr::t4::Rva kSetupIgnoreEntityParametersRva = 0x001ABD60;
constexpr wawvr::t4::Rva kServerTraceRva = 0x001ABDC0;
constexpr std::array<std::uint8_t, 67> kLocationalTraceBytes{
    0x83, 0xEC, 0x0C, 0x8D, 0x0C, 0x24, 0xE8, 0x85,
    0x79, 0x0A, 0x00, 0x8B, 0x44, 0x24, 0x20, 0x8B,
    0x4C, 0x24, 0x1C, 0x6A, 0x00, 0x6A, 0x00, 0x6A,
    0x01, 0x50, 0x8B, 0x44, 0x24, 0x28, 0x6A, 0x01,
    0x51, 0x8B, 0x4C, 0x24, 0x2C, 0x8D, 0x54, 0x24,
    0x18, 0x52, 0x50, 0x68, 0x68, 0x6F, 0x81, 0x00,
    0x51, 0x8B, 0x4C, 0x24, 0x38, 0xB8, 0x68, 0x6F,
    0x81, 0x00, 0xE8, 0xB1, 0x79, 0x0A, 0x00, 0x83,
    0xC4, 0x34, 0xC3,
};
constexpr std::size_t kSetupIgnoreEntityParametersCallOffset = 6;
constexpr std::size_t kServerTraceCallOffset = 58;
constexpr wawvr::t4::Rva kBulletPriorityMapRva = 0x004DC28C;
constexpr std::array<std::uint8_t, 20> kBulletPriorityMapBytes{
    0x01, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x00, 0x00,
};
constexpr std::uint32_t kSinglePlayerGrenadeTraceMask = 0x0280E091U;
constexpr std::size_t kRetailTraceResultSize = 0x34;
constexpr std::size_t kRetailTraceFractionOffset = 0x10;
constexpr std::size_t kRetailTraceStartSolidOffset = 0x2F;
constexpr float kReleaseSurfaceBackoffUnits = 2.0F;
constexpr std::int32_t kMaximumEntityNumber = 0x3FF;

constexpr std::size_t kWeaponDefinitionTableSpan = 0x200;
constexpr std::size_t kMaximumGrenadeWeaponIndex = 127;
constexpr std::size_t kWeaponDefinitionOffhandClassOffset = 0x174;
constexpr std::size_t kWeaponDefinitionProjectileSpeedOffset = 0x664;
constexpr std::size_t kWeaponDefinitionProjectileSpeedUpOffset = 0x668;
constexpr std::size_t kWeaponDefinitionProjectileSpeedForwardOffset = 0x66C;
constexpr std::size_t kWeaponDefinitionProjectileModelOffset = 0x680;
constexpr std::size_t kWeaponDefinitionGrenadeSpan = 0x684;
constexpr std::size_t kWeaponDefinitionAmmoIndexOffset = 0x3F4;
constexpr std::size_t kWeaponDefinitionClipIndexOffset = 0x3FC;
constexpr std::size_t kPlayerStateOffhandIndexOffset = 0xFC;
constexpr std::size_t kPlayerStateOffhandSpan = 0x100;
constexpr std::size_t kPlayerStateMinimumSpan = 0x920;
constexpr std::size_t kPlayerStateThrowbackTimeOffset = 0x50;
constexpr std::size_t kPlayerStateOffhandSecondaryOffset = 0x100;
constexpr std::size_t kPlayerStateAmmoOffset = 0x17C;
constexpr std::size_t kPlayerStateClipAmmoOffset = 0x5FC;
constexpr std::size_t kPlayerStateWeaponsOffset = 0x7FC;
constexpr std::size_t kPlayerStateWeaponBitsetWords = 4;
constexpr std::size_t kMaximumPlayerAmmoPools = 128;
constexpr std::size_t kLocalPlayerClientPointerOffset = 0x180;
constexpr std::size_t kLocalPlayerEntityIdentitySpan = 0x184;
constexpr wawvr::t4::Rva kLastParsedWeaponIndexRva = 0x042DE3BC;

// Exact unpacked Steam SP evidence is recorded in t4/PROVENANCE.md. The
// experiment is deliberately opt-in until real enemy-grenade tests pass.
constexpr wchar_t kContextualThrowbackExperimentEnvironment[] =
    L"WAWVR_EXPERIMENTAL_CONTEXTUAL_THROWBACK";
// Hard safety boundary: native event replay, shutdown cancellation and the
// complete inventory envelope still need independent review/live proof. Even
// an explicit experimental environment must not activate this candidate yet.
constexpr bool kContextualThrowbackCandidateClearedForLive = false;
constexpr wawvr::t4::Rva kContextFireEntryRva = 0x000214E0;
constexpr wawvr::t4::Rva kContextNativeEndRva = 0x00021630;
constexpr wawvr::t4::Rva kContextPickupCallRva = 0x000E7C05;
constexpr wawvr::t4::Rva kContextPickupTargetRva = 0x000E7910;
constexpr wawvr::t4::Rva kContextThrowEventCallRva = 0x000E7BD3;
constexpr wawvr::t4::Rva kContextThrowEventTargetRva = 0x00152780;
constexpr std::array<std::uint8_t, 9> kContextFireDisplaced{
    0x57, 0x8B, 0x38, 0x8B, 0x8F, 0xD0, 0x00, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 7> kContextPickupCallPrefix{
    0x63, 0x03, 0x00, 0x00, 0x8B, 0xFE, 0xE8,
};
constexpr std::array<std::uint8_t, 5> kContextPickupCallSuffix{
    0xE9, 0x53, 0x03, 0x00, 0x00,
};
constexpr std::size_t kContextPlayerStateOwnerOffset = 0x4C;
constexpr std::size_t kContextPlayerStateFuseOffset = 0x48;
constexpr std::size_t kContextPlayerStateHintEntityOffset = 0x870;
constexpr std::size_t kContextPlayerStateWeaponStateOffset = 0x108;
constexpr std::size_t kContextEntitySize = 0x378;
constexpr std::size_t kContextEntityTypeOffset = 0x04;
constexpr std::size_t kContextEntityWeaponOffset = 0xA8;
// Exact SP G_InitGentity sets +0x11D; G_FreeEntity clears the entity and
// advances +0x90. +0x118 is a separate native link-state byte, not allocation.
constexpr std::size_t kContextEntityInUseOffset = 0x11D;
constexpr std::size_t kContextEntityHandlerOffset = 0x19E;
constexpr std::size_t kContextEntityOwnerHandleOffset = 0x1C0;
constexpr std::uint8_t kContextFragHandler = 14;
constexpr std::uint32_t kContextMissileType = 4;
constexpr std::size_t kContextSourceIdentitySpan = 0x1C2;

constexpr std::uint64_t kPendingMilliseconds = 3000;
constexpr std::uint64_t kViewRecoveryMilliseconds = 750;
constexpr std::int64_t kMinimumVelocityDeltaNanoseconds = 4'000'000;
constexpr std::int64_t kMaximumVelocityDeltaNanoseconds = 100'000'000;
constexpr float kGrenadePalmInsetUnits = 0.60F;
constexpr float kMaximumReleaseReachUnits = 64.0F;
constexpr float kLeftGripReleaseThreshold = 0.35F;
constexpr float kTrackingAnchorPositionToleranceMeters = 0.001F;
constexpr float kTrackingAnchorOrientationDotTolerance = 0.99999F;

using FireGrenadeFunction = void*(__cdecl*)(
    void*, float*, float*, std::uint32_t, std::uint32_t, int, int);

struct ManualGrenadeRuntimeState final {
    ButtonGrenadeTriggerState button_trigger{};
    ManualGrenadeGestureState gesture{};
    ManualGrenadeVelocityHistory velocity_history{};
    std::uint64_t last_action_sequence{};
    std::int64_t previous_predicted_display_time{};
    ManualGrenadePoint previous_hand_anchor_local{};
    bool previous_hand_anchor_local_valid{};
    wawvr::xr::Posef velocity_tracking_anchor{};
    bool velocity_tracking_anchor_valid{};
    std::uint64_t grabbed_action_sequence{};
    std::uint64_t released_action_sequence{};
    std::uint64_t released_at_milliseconds{};
    std::uint64_t pending_until_milliseconds{};
    std::uint64_t view_override_until_milliseconds{};
    std::uint64_t release_velocity_sample_age_nanoseconds{};
    std::uint32_t grenade_weapon_index{};
    void* projectile_model{};
    wawvr::xr::EnginePose held_world{};
    bool held_world_valid{};
    ManualGrenadePoint release_origin_world{};
    ManualGrenadePoint release_velocity_world{};
    ManualGrenadePoint release_fallback_forward_world{1.0F, 0.0F, 0.0F};
    bool awaiting_left_grip_release{};
    bool holding_suspended{};
    bool recovery_requires_repress{};
    std::uint64_t transaction_generation{};
    std::int32_t native_throwback_fuse_at_grab{};
};

enum class NativeGrenadeCommandPolicy : std::uint32_t {
    passthrough = 0,
    hold_frag,
    hold_tactical,
    release_pending,
};

std::atomic<bool> g_installed{false};
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_button_grenades_enabled{false};
std::atomic<bool> g_left_hand_reserved{false};
std::atomic<NativeGrenadeCommandPolicy> g_native_command_policy{
    NativeGrenadeCommandPolicy::passthrough};
std::atomic<std::uint64_t> g_transaction_counter{0};
std::mutex g_state_mutex{};
ManualGrenadeRuntimeState g_state{};
FireGrenadeFunction g_original_fire_grenade = nullptr;
std::uintptr_t g_weapon_definition_pointer_table = 0;
std::uintptr_t g_local_player_entity = 0;
std::uintptr_t g_last_weapon_index = 0;
std::uintptr_t g_locational_trace = 0;
std::uintptr_t g_bullet_priority_map = 0;
std::atomic<bool> g_contextual_experiment_installed{false};
std::atomic<bool> g_contextual_transaction_active{false};
ContextualThrowbackTransaction g_contextual_transaction{};
std::uintptr_t g_contextual_predicted_player_state = 0;
std::uintptr_t g_contextual_native_pickup = 0;
std::uintptr_t g_contextual_native_end = 0;
std::uintptr_t g_contextual_native_throw = 0;
std::uintptr_t g_contextual_native_level_time = 0;
void* g_contextual_fire_trampoline = nullptr;
void install_contextual_throwback_experiment(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;

[[nodiscard]] bool button_grenades_enabled_from_environment() noexcept {
    std::array<wchar_t, 2> value{};
    const DWORD length = GetEnvironmentVariableW(
        kButtonGrenadesEnvironmentVariable, value.data(),
        static_cast<DWORD>(value.size()));
    return length == 1 &&
        button_grenade_setting_enabled(
            std::wstring_view(value.data(), length));
}

[[nodiscard]] bool readable_protection(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffU;
    return access == PAGE_READONLY || access == PAGE_READWRITE ||
           access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool writable_protection(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffU;
    return access == PAGE_READWRITE || access == PAGE_WRITECOPY ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool accessible_range(
    const void* const address,
    const std::size_t size,
    const bool writable = false) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        (writable ? !writable_protection(memory.Protect)
                  : !readable_protection(memory.Protect))) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto region_begin =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    if (memory.RegionSize >
        (std::numeric_limits<std::uintptr_t>::max)() - region_begin) {
        return false;
    }
    const auto region_end = region_begin + memory.RegionSize;
    return begin >= region_begin && begin <= region_end &&
           size <= region_end - begin;
}

template <std::size_t N>
[[nodiscard]] bool bytes_equal(
    const void* const address,
    const std::array<std::uint8_t, N>& expected) noexcept {
    return accessible_range(address, expected.size()) &&
           std::memcmp(address, expected.data(), expected.size()) == 0;
}

[[nodiscard]] std::uintptr_t decode_rel32(
    const std::uint8_t* const call) noexcept {
    if (call == nullptr || call[0] != 0xE8) {
        return 0;
    }
    std::int32_t displacement = 0;
    std::memcpy(&displacement, call + 1, sizeof(displacement));
    return reinterpret_cast<std::uintptr_t>(call + 5) + displacement;
}

[[nodiscard]] bool build_rel32(
    const std::uintptr_t source,
    const std::uintptr_t destination,
    std::array<std::uint8_t, 5>* const bytes) noexcept {
    if (bytes == nullptr) {
        return false;
    }
    const std::int64_t displacement =
        static_cast<std::int64_t>(destination) -
        static_cast<std::int64_t>(source + 5);
    if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
        displacement > (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    const auto relative = static_cast<std::int32_t>(displacement);
    (*bytes)[0] = 0xE8;
    std::memcpy(bytes->data() + 1, &relative, sizeof(relative));
    return true;
}

[[nodiscard]] bool patch_call(
    std::uint8_t* const callsite,
    const std::uintptr_t destination,
    DWORD* const system_error) noexcept {
    std::array<std::uint8_t, 5> patch{};
    if (callsite == nullptr ||
        !build_rel32(
            reinterpret_cast<std::uintptr_t>(callsite), destination,
            &patch)) {
        return false;
    }
    DWORD old_protection = 0;
    if (!VirtualProtect(
            callsite, patch.size(), PAGE_EXECUTE_READWRITE,
            &old_protection)) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        return false;
    }
    std::memcpy(callsite, patch.data(), patch.size());
    const bool flushed = FlushInstructionCache(
        GetCurrentProcess(), callsite, patch.size()) != FALSE;
    DWORD ignored = 0;
    const bool restored = VirtualProtect(
        callsite, patch.size(), old_protection, &ignored) != FALSE;
    if ((!flushed || !restored) && system_error != nullptr) {
        *system_error = GetLastError();
    }
    return flushed && restored && decode_rel32(callsite) == destination;
}

[[nodiscard]] bool context_matches(
    const std::uint8_t* const context,
    const std::uint8_t* const callsite) noexcept {
    return context != nullptr && callsite != nullptr &&
        context + kFireGrenadeCallOffset == callsite &&
        callsite[0] == 0xE8 &&
        std::memcmp(
            context, kFireGrenadeContext.data(),
            kFireGrenadeCallOffset) == 0 &&
        std::memcmp(
            callsite + 5,
            kFireGrenadeContext.data() + kFireGrenadeCallOffset + 5,
            kFireGrenadeContext.size() -
                kFireGrenadeCallOffset - 5) == 0;
}

[[nodiscard]] bool finite_point(const ManualGrenadePoint& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] bool finite_vector(const wawvr::xr::Vec3f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] bool equivalent_tracking_anchor(
    const wawvr::xr::Posef& left,
    const wawvr::xr::Posef& right) noexcept {
    if (!finite_vector(left.position) || !finite_vector(right.position) ||
        !std::isfinite(left.orientation.x) ||
        !std::isfinite(left.orientation.y) ||
        !std::isfinite(left.orientation.z) ||
        !std::isfinite(left.orientation.w) ||
        !std::isfinite(right.orientation.x) ||
        !std::isfinite(right.orientation.y) ||
        !std::isfinite(right.orientation.z) ||
        !std::isfinite(right.orientation.w)) {
        return false;
    }
    const double dx = static_cast<double>(left.position.x) -
        right.position.x;
    const double dy = static_cast<double>(left.position.y) -
        right.position.y;
    const double dz = static_cast<double>(left.position.z) -
        right.position.z;
    const double position_distance_squared = dx * dx + dy * dy + dz * dz;
    const double left_length_squared =
        static_cast<double>(left.orientation.x) * left.orientation.x +
        static_cast<double>(left.orientation.y) * left.orientation.y +
        static_cast<double>(left.orientation.z) * left.orientation.z +
        static_cast<double>(left.orientation.w) * left.orientation.w;
    const double right_length_squared =
        static_cast<double>(right.orientation.x) * right.orientation.x +
        static_cast<double>(right.orientation.y) * right.orientation.y +
        static_cast<double>(right.orientation.z) * right.orientation.z +
        static_cast<double>(right.orientation.w) * right.orientation.w;
    if (!std::isfinite(position_distance_squared) ||
        !std::isfinite(left_length_squared) ||
        !std::isfinite(right_length_squared) ||
        left_length_squared <= 1.0e-8 || right_length_squared <= 1.0e-8) {
        return false;
    }
    const double orientation_dot = std::abs(
        static_cast<double>(left.orientation.x) * right.orientation.x +
        static_cast<double>(left.orientation.y) * right.orientation.y +
        static_cast<double>(left.orientation.z) * right.orientation.z +
        static_cast<double>(left.orientation.w) * right.orientation.w) /
        std::sqrt(left_length_squared * right_length_squared);
    const double position_tolerance_squared =
        static_cast<double>(kTrackingAnchorPositionToleranceMeters) *
        kTrackingAnchorPositionToleranceMeters;
    return position_distance_squared <= position_tolerance_squared &&
        std::isfinite(orientation_dot) &&
        orientation_dot >= kTrackingAnchorOrientationDotTolerance;
}

[[nodiscard]] float dot(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] wawvr::xr::Vec3f cross(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] bool valid_basis(const wawvr::xr::Basis3f& axis) noexcept {
    return finite_vector(axis.forward) && finite_vector(axis.left) &&
        finite_vector(axis.up) &&
        std::abs(dot(axis.forward, axis.forward) - 1.0F) < 0.20F &&
        std::abs(dot(axis.left, axis.left) - 1.0F) < 0.20F &&
        std::abs(dot(axis.up, axis.up) - 1.0F) < 0.20F &&
        std::abs(dot(axis.forward, axis.left)) < 0.20F &&
        std::abs(dot(axis.forward, axis.up)) < 0.20F &&
        std::abs(dot(axis.left, axis.up)) < 0.20F &&
        dot(cross(axis.forward, axis.left), axis.up) > 0.75F;
}

[[nodiscard]] wawvr::xr::Vec3f compose(
    const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Vec3f& local) noexcept {
    return {
        local.x * basis.forward.x + local.y * basis.left.x +
            local.z * basis.up.x,
        local.x * basis.forward.y + local.y * basis.left.y +
            local.z * basis.up.y,
        local.x * basis.forward.z + local.y * basis.left.z +
            local.z * basis.up.z,
    };
}

[[nodiscard]] wawvr::xr::Basis3f compose_axes(
    const wawvr::xr::Basis3f& outer,
    const wawvr::xr::Basis3f& inner) noexcept {
    return {
        compose(outer, inner.forward),
        compose(outer, inner.left),
        compose(outer, inner.up),
    };
}

[[nodiscard]] bool level_camera_axis(
    const wawvr::xr::Basis3f& camera_axis,
    wawvr::xr::Basis3f* const body_axis) noexcept {
    if (body_axis == nullptr || !valid_basis(camera_axis)) {
        return false;
    }
    const double horizontal = std::hypot(
        static_cast<double>(camera_axis.forward.x),
        static_cast<double>(camera_axis.forward.y));
    if (!std::isfinite(horizontal) || horizontal < 1.0e-5) {
        return false;
    }
    const float inverse = static_cast<float>(1.0 / horizontal);
    const wawvr::xr::Vec3f forward{
        camera_axis.forward.x * inverse,
        camera_axis.forward.y * inverse,
        0.0F,
    };
    *body_axis = {
        forward,
        {-forward.y, forward.x, 0.0F},
        {0.0F, 0.0F, 1.0F},
    };
    return valid_basis(*body_axis);
}

[[nodiscard]] bool build_hand_sample(
    const ControllerFrameSnapshot& controller,
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    wawvr::xr::EnginePose* const held_world,
    ManualGrenadePoint* const head_local,
    ManualGrenadePoint* const anchor_local,
    ManualGrenadePoint* const fallback_forward) noexcept {
    if (held_world == nullptr || head_local == nullptr ||
        anchor_local == nullptr || fallback_forward == nullptr ||
        !finite_vector(camera_origin)) {
        return false;
    }
    wawvr::xr::Basis3f body_axis{};
    if (!level_camera_axis(camera_axis, &body_axis)) {
        return false;
    }
    const auto& left = controller.frame.actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
    if (!left.grip.active || !left.grip.position_valid ||
        !left.grip.orientation_valid ||
        !finite_vector(left.grip.pose.position)) {
        return false;
    }
    const auto relative = wawvr::xr::OpenXrPoseToIwRelative(
        left.grip.pose, controller.tracking_anchor,
        wawvr::xr::kIwUnitsPerMeter);
    const auto relative_to_head = wawvr::xr::OpenXrPoseToIwRelative(
        left.grip.pose, controller.frame.head_center,
        wawvr::xr::kIwUnitsPerMeter);
    if (!finite_vector(relative.position) || !valid_basis(relative.axis) ||
        !finite_vector(relative_to_head.position)) {
        return false;
    }
    const auto world_offset = compose(body_axis, relative.position);
    held_world->position = {
        camera_origin.x + world_offset.x,
        camera_origin.y + world_offset.y,
        camera_origin.z + world_offset.z,
    };
    held_world->axis = compose_axes(body_axis, relative.axis);
    held_world->position.x +=
        held_world->axis.left.x * kGrenadePalmInsetUnits;
    held_world->position.y +=
        held_world->axis.left.y * kGrenadePalmInsetUnits;
    held_world->position.z +=
        held_world->axis.left.z * kGrenadePalmInsetUnits;
    *head_local = {
        relative_to_head.position.x,
        relative_to_head.position.y,
        relative_to_head.position.z,
    };
    *anchor_local = {
        relative.position.x,
        relative.position.y,
        relative.position.z,
    };
    *fallback_forward = {
        body_axis.forward.x,
        body_axis.forward.y,
        body_axis.forward.z,
    };
    return finite_vector(held_world->position) &&
        valid_basis(held_world->axis) && finite_point(*head_local) &&
        finite_point(*anchor_local) && finite_point(*fallback_forward);
}

[[nodiscard]] bool slot_matches_offhand_class(
    const ManualGrenadeSlot slot,
    const std::int32_t offhand_class) noexcept {
    return (slot == ManualGrenadeSlot::frag && offhand_class == 1) ||
        (slot == ManualGrenadeSlot::tactical &&
         (offhand_class == 2 || offhand_class == 3));
}

[[nodiscard]] bool resolve_grenade_weapon(
    const std::uint32_t weapon_index,
    const ManualGrenadeSlot slot,
    std::uintptr_t* const weapon_definition,
    void** const projectile_model) noexcept {
    if (weapon_definition == nullptr || projectile_model == nullptr) {
        return false;
    }
    *weapon_definition = 0;
    *projectile_model = nullptr;
    if (weapon_index == 0 || weapon_index > kMaximumGrenadeWeaponIndex ||
        g_weapon_definition_pointer_table == 0) {
        return false;
    }
    const auto slot_address = g_weapon_definition_pointer_table +
        static_cast<std::uintptr_t>(weapon_index) * sizeof(std::uint32_t);
    if (!accessible_range(
            reinterpret_cast<const void*>(slot_address),
            sizeof(std::uint32_t))) {
        return false;
    }
    std::uint32_t definition32 = 0;
    std::memcpy(
        &definition32, reinterpret_cast<const void*>(slot_address),
        sizeof(definition32));
    const auto definition = static_cast<std::uintptr_t>(definition32);
    if (definition == 0 ||
        !accessible_range(
            reinterpret_cast<const void*>(definition),
            kWeaponDefinitionGrenadeSpan)) {
        return false;
    }
    std::int32_t offhand_class = 0;
    std::uint32_t model32 = 0;
    std::memcpy(
        &offhand_class,
        reinterpret_cast<const void*>(
            definition + kWeaponDefinitionOffhandClassOffset),
        sizeof(offhand_class));
    std::memcpy(
        &model32,
        reinterpret_cast<const void*>(
            definition + kWeaponDefinitionProjectileModelOffset),
        sizeof(model32));
    if (!slot_matches_offhand_class(slot, offhand_class) || model32 == 0) {
        return false;
    }
    *weapon_definition = definition;
    *projectile_model = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(model32));
    return true;
}

enum class GrenadeInventoryStatus : std::uint8_t {
    available,
    proven_empty,
    invalid,
};

struct GrenadeInventoryResolution final {
    GrenadeInventoryStatus status{GrenadeInventoryStatus::invalid};
    std::uint32_t weapon_index{};
    void* projectile_model{};
    bool frag_throwback_override{};
};

std::atomic<std::int32_t> g_throwback_time_left{0};
std::atomic<std::uint64_t> g_throwback_sample_milliseconds{0};
std::atomic<std::uint64_t> g_local_grenade_spawn_count{0};

[[nodiscard]] std::int32_t sample_native_throwback_time_left() noexcept {
    std::uint32_t client32 = 0;
    if (g_local_player_entity == 0 ||
        !accessible_range(reinterpret_cast<const void*>(
            g_local_player_entity + kLocalPlayerClientPointerOffset),
            sizeof(client32))) {
        return 0;
    }
    std::memcpy(&client32, reinterpret_cast<const void*>(
        g_local_player_entity + kLocalPlayerClientPointerOffset), sizeof(client32));
    if (client32 < 0x10000 || client32 >
        (std::numeric_limits<std::uintptr_t>::max)() -
            kPlayerStateThrowbackTimeOffset - sizeof(std::int32_t)) {
        return 0;
    }
    const auto address = static_cast<std::uintptr_t>(client32) +
        kPlayerStateThrowbackTimeOffset;
    std::int32_t time_left = 0;
    if (client32 == 0 || !accessible_range(
            reinterpret_cast<const void*>(address), sizeof(time_left))) {
        return 0;
    }
    std::memcpy(&time_left, reinterpret_cast<const void*>(address), sizeof(time_left));
    return time_left > 0 ? time_left : 0;
}

[[nodiscard]] std::uint32_t grenade_button_mask(
    const ManualGrenadeSlot slot) noexcept {
    if (slot == ManualGrenadeSlot::frag) {
        return wawvr::t4::button_mask(
            wawvr::t4::UsercmdButton::frag_grenade);
    }
    if (slot == ManualGrenadeSlot::tactical) {
        return wawvr::t4::button_mask(
            wawvr::t4::UsercmdButton::smoke_grenade);
    }
    return 0;
}

[[nodiscard]] NativeGrenadeCommandPolicy native_policy_for_slot(
    const ManualGrenadeSlot slot) noexcept {
    if (slot == ManualGrenadeSlot::frag) {
        return NativeGrenadeCommandPolicy::hold_frag;
    }
    if (slot == ManualGrenadeSlot::tactical) {
        return NativeGrenadeCommandPolicy::hold_tactical;
    }
    return NativeGrenadeCommandPolicy::passthrough;
}

[[nodiscard]] std::uint32_t native_hold_mask_for_policy(
    const NativeGrenadeCommandPolicy policy) noexcept {
    if (policy == NativeGrenadeCommandPolicy::hold_frag) {
        return grenade_button_mask(ManualGrenadeSlot::frag);
    }
    if (policy == NativeGrenadeCommandPolicy::hold_tactical) {
        return grenade_button_mask(ManualGrenadeSlot::tactical);
    }
    return 0;
}

[[nodiscard]] GrenadeInventoryResolution resolve_grenade_inventory(
    const ManualGrenadeSlot slot) noexcept {
    GrenadeInventoryResolution result{};
    if ((slot != ManualGrenadeSlot::frag &&
         slot != ManualGrenadeSlot::tactical) ||
        g_local_player_entity == 0 ||
        g_weapon_definition_pointer_table == 0 ||
        g_last_weapon_index == 0 ||
        !accessible_range(
            reinterpret_cast<const void*>(
                g_local_player_entity +
                kLocalPlayerClientPointerOffset),
            sizeof(std::uint32_t)) ||
        !accessible_range(
            reinterpret_cast<const void*>(
                g_last_weapon_index),
            sizeof(std::uint32_t))) {
        return result;
    }

    std::uint32_t client32 = 0;
    std::uint32_t last_weapon_index = 0;
    std::memcpy(
        &client32,
        reinterpret_cast<const void*>(
            g_local_player_entity + kLocalPlayerClientPointerOffset),
        sizeof(client32));
    std::memcpy(
        &last_weapon_index,
        reinterpret_cast<const void*>(g_last_weapon_index),
        sizeof(last_weapon_index));
    const auto player_state = static_cast<std::uintptr_t>(client32);
    if (player_state == 0 || last_weapon_index < 1 ||
        last_weapon_index > kMaximumGrenadeWeaponIndex ||
        !accessible_range(
            reinterpret_cast<const void*>(player_state),
            kPlayerStateMinimumSpan)) {
        return result;
    }

    std::int32_t throwback_time = 0;
    std::int32_t offhand_secondary = 0;
    std::array<std::uint32_t, kPlayerStateWeaponBitsetWords>
        equipped_weapons{};
    std::memcpy(
        &throwback_time,
        reinterpret_cast<const void*>(
            player_state + kPlayerStateThrowbackTimeOffset),
        sizeof(throwback_time));
    std::memcpy(
        &offhand_secondary,
        reinterpret_cast<const void*>(
            player_state + kPlayerStateOffhandSecondaryOffset),
        sizeof(offhand_secondary));
    std::memcpy(
        equipped_weapons.data(),
        reinterpret_cast<const void*>(
            player_state + kPlayerStateWeaponsOffset),
        sizeof(equipped_weapons));

    std::int32_t required_class = 1;
    if (slot == ManualGrenadeSlot::tactical) {
        if (offhand_secondary == 0) {
            required_class = 2;
        } else if (offhand_secondary == 1) {
            required_class = 3;
        } else {
            return result;
        }
    }

    for (std::uint32_t weapon_index = 1;
         weapon_index <= last_weapon_index;
         ++weapon_index) {
        const auto slot_address = g_weapon_definition_pointer_table +
            static_cast<std::uintptr_t>(weapon_index) *
                sizeof(std::uint32_t);
        if (!accessible_range(
                reinterpret_cast<const void*>(slot_address),
                sizeof(std::uint32_t))) {
            return result;
        }
        std::uint32_t definition32 = 0;
        std::memcpy(
            &definition32, reinterpret_cast<const void*>(slot_address),
            sizeof(definition32));
        const auto definition = static_cast<std::uintptr_t>(definition32);
        if (definition == 0 ||
            !accessible_range(
                reinterpret_cast<const void*>(definition),
                kWeaponDefinitionGrenadeSpan)) {
            return result;
        }
        std::int32_t offhand_class = 0;
        std::memcpy(
            &offhand_class,
            reinterpret_cast<const void*>(
                definition + kWeaponDefinitionOffhandClassOffset),
            sizeof(offhand_class));
        if (offhand_class != required_class) {
            continue;
        }
        if (weapon_index >=
            kPlayerStateWeaponBitsetWords * 32U) {
            return result;
        }
        const bool equipped =
            (equipped_weapons[weapon_index >> 5U] &
             (1U << (weapon_index & 31U))) != 0;
        if (!equipped) {
            continue;
        }

        std::uintptr_t verified_definition = 0;
        void* projectile_model = nullptr;
        if (!resolve_grenade_weapon(
                weapon_index, slot, &verified_definition,
                &projectile_model) ||
            verified_definition != definition) {
            return result;
        }
        const bool frag_throwback_override =
            manual_grenade_has_frag_throwback_override(
                slot, throwback_time);
        if (!frag_throwback_override) {
            std::int32_t ammo_index = -1;
            std::int32_t clip_index = -1;
            std::memcpy(
                &ammo_index,
                reinterpret_cast<const void*>(
                    definition + kWeaponDefinitionAmmoIndexOffset),
                sizeof(ammo_index));
            std::memcpy(
                &clip_index,
                reinterpret_cast<const void*>(
                    definition + kWeaponDefinitionClipIndexOffset),
                sizeof(clip_index));
            if (ammo_index < 0 || clip_index < 0 ||
                ammo_index >=
                    static_cast<std::int32_t>(kMaximumPlayerAmmoPools) ||
                clip_index >=
                    static_cast<std::int32_t>(kMaximumPlayerAmmoPools)) {
                return result;
            }
            std::int32_t reserve_ammo = 0;
            std::int32_t clip_ammo = 0;
            std::memcpy(
                &reserve_ammo,
                reinterpret_cast<const void*>(
                    player_state + kPlayerStateAmmoOffset +
                    static_cast<std::size_t>(ammo_index) *
                        sizeof(std::int32_t)),
                sizeof(reserve_ammo));
            std::memcpy(
                &clip_ammo,
                reinterpret_cast<const void*>(
                    player_state + kPlayerStateClipAmmoOffset +
                    static_cast<std::size_t>(clip_index) *
                        sizeof(std::int32_t)),
                sizeof(clip_ammo));
            if (static_cast<std::int64_t>(reserve_ammo) +
                    static_cast<std::int64_t>(clip_ammo) <=
                0) {
                continue;
            }
        }
        result.status = GrenadeInventoryStatus::available;
        result.weapon_index = weapon_index;
        result.projectile_model = projectile_model;
        result.frag_throwback_override = frag_throwback_override;
        return result;
    }
    result.status = GrenadeInventoryStatus::proven_empty;
    return result;
}

[[nodiscard]] std::uint64_t next_transaction_generation() noexcept {
    std::uint64_t generation =
        g_transaction_counter.fetch_add(1, std::memory_order_acq_rel) + 1U;
    if (generation == 0) {
        generation =
            g_transaction_counter.fetch_add(1, std::memory_order_acq_rel) +
            1U;
    }
    return generation;
}

void clear_transaction_locked(
    const bool preserve_trigger_baseline,
    const bool await_left_grip_release,
    const bool preserve_view_recovery) noexcept {
    const bool input_initialized = g_state.gesture.input_initialized;
    const bool trigger_was_held = g_state.gesture.trigger_was_held;
    const std::uint64_t view_override_until =
        g_state.view_override_until_milliseconds;
    g_state = {};
    if (preserve_trigger_baseline) {
        g_state.gesture.input_initialized = input_initialized;
        g_state.gesture.trigger_was_held = trigger_was_held;
    }
    g_state.awaiting_left_grip_release = await_left_grip_release;
    if (preserve_view_recovery) {
        g_state.view_override_until_milliseconds = view_override_until;
    }
    g_native_command_policy.store(
        NativeGrenadeCommandPolicy::passthrough,
        std::memory_order_release);
    g_left_hand_reserved.store(
        await_left_grip_release, std::memory_order_release);
}

[[nodiscard]] bool native_spawn_matches_transaction_locked(
    void* const parent,
    const std::uint32_t weapon_index) noexcept {
    if (parent != reinterpret_cast<void*>(g_local_player_entity) ||
        weapon_index == 0 || g_state.transaction_generation == 0 ||
        g_state.grenade_weapon_index != weapon_index ||
        (g_state.gesture.stage != ManualGrenadeStage::holding &&
         g_state.gesture.stage !=
             ManualGrenadeStage::released_pending)) {
        return false;
    }
    std::uintptr_t definition = 0;
    void* projectile_model = nullptr;
    return resolve_grenade_weapon(
        weapon_index, g_state.gesture.held_slot, &definition,
        &projectile_model);
}

[[nodiscard]] bool update_velocity_sample_locked(
    const ControllerFrameSnapshot& controller,
    const wawvr::xr::Basis3f& camera_axis,
    const ManualGrenadePoint& anchor_local,
    const std::uint64_t sample_nanoseconds) noexcept {
    if (g_state.velocity_tracking_anchor_valid &&
        !equivalent_tracking_anchor(
            g_state.velocity_tracking_anchor,
            controller.tracking_anchor)) {
        // Recenter and the body-yaw transfer both rebase the coordinate frame.
        // Never interpret that discontinuity as physical hand velocity.
        clear_manual_grenade_velocity_history(&g_state.velocity_history);
        g_state.previous_hand_anchor_local_valid = false;
        g_state.previous_predicted_display_time = 0;
    }
    g_state.velocity_tracking_anchor = controller.tracking_anchor;
    g_state.velocity_tracking_anchor_valid = true;
    if (controller.frame.actions.sequence == g_state.last_action_sequence) {
        return false;
    }
    g_state.last_action_sequence = controller.frame.actions.sequence;
    bool recorded = false;
    if (g_state.previous_hand_anchor_local_valid &&
        controller.frame.predicted_display_time >
            g_state.previous_predicted_display_time) {
        const std::int64_t delta = controller.frame.predicted_display_time -
            g_state.previous_predicted_display_time;
        if (delta >= kMinimumVelocityDeltaNanoseconds &&
            delta <= kMaximumVelocityDeltaNanoseconds) {
            const double inverse_seconds = 1.0e9 /
                static_cast<double>(delta);
            const wawvr::xr::Vec3f local_velocity{
                static_cast<float>(
                    (anchor_local.x -
                     g_state.previous_hand_anchor_local.x) *
                    inverse_seconds),
                static_cast<float>(
                    (anchor_local.y -
                     g_state.previous_hand_anchor_local.y) *
                    inverse_seconds),
                static_cast<float>(
                    (anchor_local.z -
                     g_state.previous_hand_anchor_local.z) *
                    inverse_seconds),
            };
            wawvr::xr::Basis3f body_axis{};
            if (finite_vector(local_velocity) &&
                level_camera_axis(camera_axis, &body_axis)) {
                const auto world_velocity = compose(body_axis, local_velocity);
                recorded = record_manual_grenade_velocity(
                    sample_nanoseconds,
                    {world_velocity.x, world_velocity.y, world_velocity.z},
                    &g_state.velocity_history);
            }
        }
    }
    g_state.previous_hand_anchor_local = anchor_local;
    g_state.previous_hand_anchor_local_valid = true;
    g_state.previous_predicted_display_time =
        controller.frame.predicted_display_time;
    return recorded;
}

[[nodiscard]] bool read_launch_calibration(
    const std::uintptr_t weapon_definition,
    ManualGrenadeLaunchCalibration* const calibration) noexcept {
    if (calibration == nullptr || weapon_definition == 0 ||
        !accessible_range(
            reinterpret_cast<const void*>(weapon_definition),
            kWeaponDefinitionProjectileSpeedForwardOffset +
                sizeof(std::int32_t))) {
        return false;
    }
    std::int32_t speed = 0;
    std::int32_t up = 0;
    std::int32_t forward = 0;
    std::memcpy(
        &speed,
        reinterpret_cast<const void*>(
            weapon_definition + kWeaponDefinitionProjectileSpeedOffset),
        sizeof(speed));
    std::memcpy(
        &up,
        reinterpret_cast<const void*>(
            weapon_definition + kWeaponDefinitionProjectileSpeedUpOffset),
        sizeof(up));
    std::memcpy(
        &forward,
        reinterpret_cast<const void*>(
            weapon_definition +
            kWeaponDefinitionProjectileSpeedForwardOffset),
        sizeof(forward));
    if (speed < 0 || up < 0 || forward < 0 ||
        speed > static_cast<std::int32_t>(
            kManualGrenadeMaximumNativeProjectileSpeed) ||
        up > static_cast<std::int32_t>(
            kManualGrenadeMaximumNativeProjectileSpeed) ||
        forward > static_cast<std::int32_t>(
            kManualGrenadeMaximumNativeProjectileSpeed)) {
        return false;
    }
    *calibration = {
        static_cast<float>(speed),
        static_cast<float>(forward),
        static_cast<float>(up),
    };
    return true;
}

#if defined(_MSC_VER) && defined(_M_IX86)
void invoke_locational_trace(
    void* const trace_results,
    const float* const trace_start,
    const float* const trace_end,
    const std::int32_t pass_entity_number,
    const std::uint32_t content_mask) noexcept {
    const std::uintptr_t target = g_locational_trace;
    const std::uintptr_t priority_map = g_bullet_priority_map;
    __asm {
        mov edx, pass_entity_number
        push priority_map
        push content_mask
        push trace_end
        push trace_start
        push trace_results
        mov eax, target
        call eax
        add esp, 14h
    }
}
#endif

[[nodiscard]] ManualGrenadePoint clamp_and_trace_release_origin(
    void* const parent,
    const ManualGrenadePoint& requested,
    const float native_start[3]) noexcept {
    ManualGrenadePoint result = requested;
    const ManualGrenadePoint native{
        native_start[0], native_start[1], native_start[2]};
    if (!finite_point(native)) {
        return result;
    }
    const double x = static_cast<double>(requested.x) - native_start[0];
    const double y = static_cast<double>(requested.y) - native_start[1];
    const double z = static_cast<double>(requested.z) - native_start[2];
    const double distance = std::sqrt(x * x + y * y + z * z);
    if (!std::isfinite(distance)) {
        return {native_start[0], native_start[1], native_start[2]};
    }
    if (distance > kMaximumReleaseReachUnits && distance > 1.0e-6) {
        const double scale = kMaximumReleaseReachUnits / distance;
        result = {
            static_cast<float>(native_start[0] + x * scale),
            static_cast<float>(native_start[1] + y * scale),
            static_cast<float>(native_start[2] + z * scale),
        };
    }

#if defined(_MSC_VER) && defined(_M_IX86)
    if (g_locational_trace == 0 || g_bullet_priority_map == 0 ||
        !accessible_range(parent, sizeof(std::int32_t))) {
        return result;
    }
    std::int32_t entity_number = -1;
    std::memcpy(&entity_number, parent, sizeof(entity_number));
    if (entity_number < 0 || entity_number > kMaximumEntityNumber) {
        return result;
    }
    const float trace_start[3]{native.x, native.y, native.z};
    const float trace_end[3]{result.x, result.y, result.z};
    std::array<std::uint8_t, kRetailTraceResultSize> trace{};
    invoke_locational_trace(
        trace.data(), trace_start, trace_end, entity_number,
        kSinglePlayerGrenadeTraceMask);

    float fraction = 1.0F;
    std::uint8_t start_solid = 0;
    std::memcpy(
        &fraction, trace.data() + kRetailTraceFractionOffset,
        sizeof(fraction));
    std::memcpy(
        &start_solid, trace.data() + kRetailTraceStartSolidOffset,
        sizeof(start_solid));
    if (start_solid != 0) {
        return native;
    }
    if (!std::isfinite(fraction)) {
        return native;
    }
    if (fraction >= 1.0F) {
        return result;
    }
    const double traced_x = static_cast<double>(result.x) - native.x;
    const double traced_y = static_cast<double>(result.y) - native.y;
    const double traced_z = static_cast<double>(result.z) - native.z;
    const double traced_distance = std::sqrt(
        traced_x * traced_x + traced_y * traced_y +
        traced_z * traced_z);
    if (!std::isfinite(traced_distance) || traced_distance <= 1.0e-6) {
        return native;
    }
    const double safe_fraction = std::max(
        0.0,
        static_cast<double>(fraction) -
            static_cast<double>(kReleaseSurfaceBackoffUnits) /
                traced_distance);
    result = {
        static_cast<float>(native.x + traced_x * safe_fraction),
        static_cast<float>(native.y + traced_y * safe_fraction),
        static_cast<float>(native.z + traced_z * safe_fraction),
    };
#else
    static_cast<void>(parent);
#endif
    return result;
}

[[nodiscard]] bool apply_manual_grenade_throw(
    void* const parent,
    float* const start,
    float* const velocity,
    const std::uint32_t weapon_index) noexcept {
    if (!g_enabled.load(std::memory_order_acquire) ||
        g_button_grenades_enabled.load(std::memory_order_acquire) ||
        parent == nullptr ||
        start == nullptr || velocity == nullptr ||
        reinterpret_cast<std::uintptr_t>(parent) != g_local_player_entity ||
        !accessible_range(start, 3 * sizeof(float), true) ||
        !accessible_range(velocity, 3 * sizeof(float), true)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_state_mutex);
    const std::uint64_t now_milliseconds = GetTickCount64();
    if (!g_enabled.load(std::memory_order_acquire) ||
        g_state.gesture.stage != ManualGrenadeStage::released_pending ||
        g_state.pending_until_milliseconds == 0 ||
        now_milliseconds > g_state.pending_until_milliseconds ||
        g_state.grabbed_action_sequence == 0 ||
        g_state.released_action_sequence <
            g_state.grabbed_action_sequence ||
        (g_state.grenade_weapon_index != 0 &&
         g_state.grenade_weapon_index != weapon_index) ||
        !finite_point(g_state.release_origin_world) ||
        !finite_point(g_state.release_velocity_world) ||
        !finite_point(g_state.release_fallback_forward_world)) {
        return false;
    }
    std::uintptr_t weapon_definition = 0;
    void* projectile_model = nullptr;
    if (!resolve_grenade_weapon(
            weapon_index, g_state.gesture.held_slot,
            &weapon_definition, &projectile_model)) {
        return false;
    }
    static_cast<void>(projectile_model);
    ManualGrenadeLaunchCalibration calibration{};
    ManualGrenadeLaunchResult launch{};
    if (!read_launch_calibration(weapon_definition, &calibration) ||
        !build_manual_grenade_launch_velocity(
            g_state.release_velocity_world,
            g_state.release_fallback_forward_world,
            calibration, &launch)) {
        return false;
    }
    const ManualGrenadePoint origin = clamp_and_trace_release_origin(
        parent, g_state.release_origin_world, start);
    if (!finite_point(origin) ||
        !finite_point(launch.velocity_game_units_per_second)) {
        return false;
    }
    start[0] = origin.x;
    start[1] = origin.y;
    start[2] = origin.z;
    velocity[0] = launch.velocity_game_units_per_second.x;
    velocity[1] = launch.velocity_game_units_per_second.y;
    velocity[2] = launch.velocity_game_units_per_second.z;

    const std::uint64_t release_age =
        now_milliseconds - g_state.released_at_milliseconds;
    g_state.view_override_until_milliseconds =
        now_milliseconds + kViewRecoveryMilliseconds;
    WAWVR_STEREO_DIAG_ONCE(
        "GrenadeDiag physical throw applied: weapon=%u releaseAge=%llums sampleAge=%.1fms drop=%d fallback=%d origin=(%.2f,%.2f,%.2f) velocity=(%.2f,%.2f,%.2f)",
        weapon_index, static_cast<unsigned long long>(release_age),
        static_cast<double>(
            g_state.release_velocity_sample_age_nanoseconds) / 1.0e6,
        launch.deliberate_drop ? 1 : 0,
        launch.used_fallback_direction ? 1 : 0,
        origin.x, origin.y, origin.z,
        launch.velocity_game_units_per_second.x,
        launch.velocity_game_units_per_second.y,
        launch.velocity_game_units_per_second.z);
    return true;
}

struct ContextNativePlayerState final {
    std::uintptr_t address{};
    std::uint32_t offhand{};
    std::int32_t owner{0x3FF};
    std::int32_t fuse{};
    std::int32_t window{};
    std::int32_t weapon_state{};
    std::int32_t hint_entity{};
};

[[nodiscard]] std::uintptr_t context_server_player_state() noexcept {
    const auto* const entity = reinterpret_cast<const std::uint8_t*>(
        g_local_player_entity);
    if (!accessible_range(entity, kLocalPlayerEntityIdentitySpan)) {
        return 0;
    }
    std::uint32_t client = 0;
    std::memcpy(&client, entity + kLocalPlayerClientPointerOffset,
                sizeof(client));
    return client != 0 && accessible_range(
        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(client)),
        kPlayerStateMinimumSpan, true) ? client : 0;
}

[[nodiscard]] bool read_context_native_player_state(
    const std::uintptr_t address,
    ContextNativePlayerState* const result) noexcept {
    if (result == nullptr || address == 0 || !accessible_range(
            reinterpret_cast<const void*>(address), kPlayerStateMinimumSpan,
            true)) {
        return false;
    }
    const auto* const state = reinterpret_cast<const std::uint8_t*>(address);
    *result = {};
    result->address = address;
    std::memcpy(&result->offhand, state + kPlayerStateOffhandIndexOffset, 4);
    std::memcpy(&result->owner, state + kContextPlayerStateOwnerOffset, 4);
    std::memcpy(&result->fuse, state + kContextPlayerStateFuseOffset, 4);
    std::memcpy(&result->window, state + kPlayerStateThrowbackTimeOffset, 4);
    std::memcpy(&result->weapon_state,
                state + kContextPlayerStateWeaponStateOffset, 4);
    std::memcpy(&result->hint_entity,
                state + kContextPlayerStateHintEntityOffset, 4);
    return true;
}

struct ContextNativePickupSource final {
    std::uintptr_t entity{};
    std::uint32_t weapon{};
    std::int32_t owner{0x3FF};
    std::int32_t remaining_fuse{};
};

[[nodiscard]] bool read_context_native_pickup_source(
    ContextNativePickupSource* const result) noexcept {
    ContextNativePlayerState player{};
    if (result == nullptr || !read_context_native_player_state(
            context_server_player_state(), &player) ||
        player.window <= 0 || player.window > 60000 ||
        player.hint_entity <= 0 || player.hint_entity >= 0x3FF) {
        return false;
    }
    const auto address = g_local_player_entity +
        static_cast<std::uintptr_t>(player.hint_entity) * kContextEntitySize;
    const auto* const entity = reinterpret_cast<const std::uint8_t*>(address);
    if (!accessible_range(entity, kContextSourceIdentitySpan) ||
        entity[kContextEntityInUseOffset] != 1 ||
        entity[kContextEntityHandlerOffset] != kContextFragHandler) {
        return false;
    }
    std::uint32_t type = 0;
    std::uint32_t weapon = 0;
    std::uint16_t owner_handle = 0;
    std::memcpy(&type, entity + kContextEntityTypeOffset, sizeof(type));
    std::memcpy(&weapon, entity + kContextEntityWeaponOffset, sizeof(weapon));
    std::memcpy(&owner_handle, entity + kContextEntityOwnerHandleOffset,
                sizeof(owner_handle));
    weapon &= 0x7FU;
    std::uintptr_t definition = 0;
    void* model = nullptr;
    if (type != kContextMissileType || owner_handle > 0x3FF ||
        !resolve_grenade_weapon(weapon, ManualGrenadeSlot::frag,
                                &definition, &model)) {
        return false;
    }
    *result = {address, weapon,
        owner_handle != 0 ? static_cast<std::int32_t>(owner_handle) - 1
                          : 0x3FE,
        player.window};
    return true;
}

[[nodiscard]] bool context_native_source_was_freed(
    const ContextNativePickupSource& source) noexcept {
    const auto* const entity = reinterpret_cast<const std::uint8_t*>(
        source.entity);
    return source.entity != 0 && accessible_range(
        entity, kContextEntityInUseOffset + 1) &&
        entity[kContextEntityInUseOffset] == 0;
}

[[nodiscard]] ContextualThrowbackPlayerState context_player_state_kind(
    const std::uintptr_t address) noexcept {
    if (address != 0 && address == context_server_player_state()) {
        return ContextualThrowbackPlayerState::authoritative;
    }
    return address != 0 && address == g_contextual_predicted_player_state
        ? ContextualThrowbackPlayerState::predicted
        : ContextualThrowbackPlayerState::unrelated;
}

#if defined(_MSC_VER) && defined(_M_IX86)
void __cdecl call_context_native_pickup(std::uintptr_t, void*);
void __cdecl call_context_native_throw(std::uintptr_t, void*);

#include "grenade_native_observer_runtime.inl"

void __cdecl context_native_pickup_bridge_impl(
    void* const entity, const std::uint32_t ordinal) noexcept {
    const auto original = g_contextual_native_pickup;
    if (original == 0) return;
    try {
        if (g_grenade_native_observer_enabled.load(std::memory_order_acquire)) {
            observe_grenade_native_event_call(original, entity, ordinal, 40);
            return;
        }
        std::uint64_t generation = 0;
        bool guarded = false;
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            guarded = g_contextual_experiment_installed.load(
                std::memory_order_acquire) &&
                entity == reinterpret_cast<void*>(g_local_player_entity) &&
                g_contextual_transaction.stage !=
                    ContextualThrowbackStage::inactive;
            if (guarded) {
                if (g_contextual_transaction.stage !=
                    ContextualThrowbackStage::waiting_for_pickup) return;
                generation = g_contextual_transaction.generation;
                if (!validate_contextual_throwback_pickup_attempt(
                        GetTickCount64(), generation,
                        &g_contextual_transaction)) return;
            }
        }
        if (!guarded) {
            call_context_native_pickup(original, entity);
            return;
        }
        ContextNativePickupSource source{};
        if (!read_context_native_pickup_source(&source)) {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            if (g_contextual_transaction.generation == generation)
                abort_contextual_throwback(&g_contextual_transaction);
            stereo_diagnostic_log(
                "ThrowbackContextDiag pickup rejected before native consumption transaction=%llu; no own-frag fallback",
                static_cast<unsigned long long>(generation));
            return;
        }
        // No lock is held across native script/item processing. The original
        // routine can refuse pickup even after copying owner/fuse. Its known
        // success path frees this exact in-use missile; that is mandatory.
        // Recheck after the bounded source read as well: checking only the
        // eventual witness would reject too late, after consuming a missile.
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            if (!validate_contextual_throwback_pickup_attempt(
                    GetTickCount64(), generation,
                    &g_contextual_transaction)) return;
        }
        call_context_native_pickup(original, entity);
        ContextNativePlayerState after{};
        const bool state_valid = read_context_native_player_state(
            context_server_player_state(), &after);
        const bool source_freed = context_native_source_was_freed(source);
        bool witnessed = false;
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            witnessed = witness_contextual_throwback_pickup(
                GetTickCount64(), generation, source.weapon,
                state_valid ? after.owner : 0x3FF,
                state_valid && after.fuse == source.remaining_fuse
                    ? after.fuse : 0,
                true, source_freed && state_valid &&
                    after.owner == source.owner &&
                    after.offhand == source.weapon,
                &g_contextual_transaction);
        }
        stereo_diagnostic_log(
            "ThrowbackContextDiag native pickup transaction=%llu source=%p sourceFreed=%d witnessed=%d weapon=%u owner=%d copiedFuse=%d expectedFuse=%d offhand=%u",
            static_cast<unsigned long long>(generation),
            reinterpret_cast<void*>(source.entity), source_freed ? 1 : 0,
            witnessed ? 1 : 0, source.weapon, after.owner, after.fuse,
            source.remaining_fuse, after.offhand);
    } catch (...) {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        abort_contextual_throwback(&g_contextual_transaction);
    }
}

[[nodiscard]] int __cdecl context_native_fire_gate_impl(
    const void* const pmove) noexcept {
    if (!g_contextual_experiment_installed.load(std::memory_order_acquire) ||
        !g_contextual_transaction_active.load(std::memory_order_acquire) ||
        !accessible_range(pmove, sizeof(std::uint32_t))) return 0;
    std::uint32_t ps = 0;
    std::memcpy(&ps, pmove, sizeof(ps));
    const auto kind = context_player_state_kind(ps);
    if (kind == ContextualThrowbackPlayerState::unrelated) return 0;
    ContextNativePlayerState snapshot{};
    if (!read_context_native_player_state(ps, &snapshot)) return 1;
    try {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        const auto decision = evaluate_contextual_throwback_fire(
            GetTickCount64(), kind, snapshot.offhand, snapshot.owner,
            &g_contextual_transaction);
        const int route = decision == ContextualThrowbackFireDecision::hold
            ? 1 : decision == ContextualThrowbackFireDecision::cancel ? 2 : 0;
        static std::array<int, 3> last_route{-1, -1, -1};
        const auto index = static_cast<std::size_t>(kind);
        if (last_route[index] != route) {
            last_route[index] = route;
            stereo_diagnostic_log(
                "ThrowbackContextDiag fire gate transaction=%llu role=%u decision=%u route=%d weapon=%u owner=%d fuse=%d nativeState=%d",
                static_cast<unsigned long long>(g_contextual_transaction.generation),
                static_cast<unsigned>(kind), static_cast<unsigned>(decision),
                route, snapshot.offhand, snapshot.owner, snapshot.fuse,
                snapshot.weapon_state);
        }
        return route;
    } catch (...) {
        return 1;  // no native event/ammo mutation on an ambiguous guard failure
    }
}

void __cdecl context_native_throw_event_bridge_impl(
    void* const entity, const std::uint32_t ordinal) noexcept {
    const auto original = g_contextual_native_throw;
    if (original == 0) return;
    try {
        if (g_grenade_native_observer_enabled.load(std::memory_order_acquire)) {
            observe_grenade_native_event_call(original, entity, ordinal, 39);
            return;
        }
        bool guarded = false;
        bool permitted = false;
        std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            guarded = g_contextual_experiment_installed.load(
                std::memory_order_acquire) &&
                entity == reinterpret_cast<void*>(g_local_player_entity) &&
                g_contextual_transaction.stage !=
                    ContextualThrowbackStage::inactive;
            if (guarded) {
                generation = g_contextual_transaction.generation;
                ContextNativePlayerState state{};
                const bool native_matches = read_context_native_player_state(
                    context_server_player_state(), &state) &&
                    state.owner == g_contextual_transaction.native_owner &&
                    state.owner != 0x3FF &&
                    state.offhand == g_contextual_transaction.weapon_index;
                // No validated event-ownership adapter exists yet. Current
                // weapon/owner and a mod generation cannot identify a queued
                // event39 across rearm. Do not manufacture a receipt here.
                permitted = native_matches && consume_contextual_throwback_spawn(
                    state.offhand, &g_contextual_transaction, nullptr);
            }
        }
        if (guarded) {
            stereo_diagnostic_log(
                "ThrowbackContextDiag authoritative event39 transaction=%llu permitted=%d; native spawn chain never receives synthetic null",
                static_cast<unsigned long long>(generation), permitted ? 1 : 0);
            if (!permitted) return;
        }
        call_context_native_throw(original, entity);
    } catch (...) {
        // This exact void event-dispatch seam has no result consumed by the
        // caller. Rejection happens here, not by returning a null missile to
        // the native grenade caller that unconditionally dereferences it.
    }
}

#include "contextual_throwback_bridge.inl"
#endif

void* __cdecl manual_grenade_fire_bridge(
    void* const parent,
    float* const start,
    float* const velocity,
    const std::uint32_t grenade_weapon_index,
    const std::uint32_t grenade_model,
    const int rotate,
    const int fuse_time) noexcept {
    const FireGrenadeFunction original = g_original_fire_grenade;
    if (original == nullptr) {
        return nullptr;
    }
    ManualGrenadeStage pending_stage = ManualGrenadeStage::ready;
    std::uint32_t pending_weapon_index = 0;
    std::uint64_t pending_until_milliseconds = 0;
    std::uint64_t matching_generation = 0;
    bool contextual_throwback = false;
    std::int32_t native_fuse_at_grab = 0;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        pending_stage = g_state.gesture.stage;
        pending_weapon_index = g_state.grenade_weapon_index;
        pending_until_milliseconds = g_state.pending_until_milliseconds;
        contextual_throwback = g_state.gesture.contextual_throwback;
        native_fuse_at_grab = g_state.native_throwback_fuse_at_grab;
        if (native_spawn_matches_transaction_locked(
                parent, grenade_weapon_index)) {
            matching_generation = g_state.transaction_generation;
        }
    }
    bool physical_override_applied = false;
    try {
        physical_override_applied = apply_manual_grenade_throw(
            parent, start, velocity, grenade_weapon_index);
    } catch (...) {
        // The exact retail callsite must never observe a C++ exception.
    }
#if defined(_MSC_VER) && defined(_M_IX86)
    const auto native_observation = observe_grenade_native_spawn_begin(
        parent, grenade_weapon_index);
#endif
    void* const grenade = original(
        parent, start, velocity, grenade_weapon_index, grenade_model,
        rotate, fuse_time);
#if defined(_MSC_VER) && defined(_M_IX86)
    observe_grenade_native_spawn_end(native_observation, grenade,
        grenade_weapon_index, fuse_time);
#endif
    if (matching_generation != 0) {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (g_state.transaction_generation == matching_generation &&
            native_spawn_matches_transaction_locked(
                parent, grenade_weapon_index)) {
            g_state.view_override_until_milliseconds =
                GetTickCount64() + kViewRecoveryMilliseconds;
            clear_transaction_locked(true, true, true);
        }
    }
    if (parent == reinterpret_cast<void*>(g_local_player_entity) ||
        pending_stage == ManualGrenadeStage::released_pending) {
        const std::int64_t pending_remaining_milliseconds =
            pending_stage == ManualGrenadeStage::released_pending &&
                    pending_until_milliseconds != 0
                ? static_cast<std::int64_t>(pending_until_milliseconds) -
                    static_cast<std::int64_t>(GetTickCount64())
                : 0;
        const auto local_spawn_count = grenade != nullptr &&
            parent == reinterpret_cast<void*>(g_local_player_entity)
            ? g_local_grenade_spawn_count.fetch_add(1, std::memory_order_acq_rel) + 1
            : g_local_grenade_spawn_count.load(std::memory_order_acquire);
        stereo_diagnostic_log(
            "GrenadeDiag local G_FireGrenade bridge observed: applied=%d parentMatch=%d weapon=%u pendingStage=%u pendingWeapon=%u pendingRemaining=%lldms transaction=%llu contextualThrowback=%d fuseAtGrab=%d nativeFuseIn=%d nativeFuseOut=%d grenade=%p localSpawnCount=%llu argumentEqualityOnly=1 nativeScheduledExpiryMeasured=0",
            physical_override_applied ? 1 : 0,
            parent == reinterpret_cast<void*>(g_local_player_entity) ? 1 : 0,
            grenade_weapon_index,
            static_cast<unsigned int>(pending_stage),
            pending_weapon_index,
            static_cast<long long>(pending_remaining_milliseconds),
            static_cast<unsigned long long>(matching_generation),
            contextual_throwback ? 1 : 0, native_fuse_at_grab,
            fuse_time, fuse_time, grenade,
            static_cast<unsigned long long>(local_spawn_count));
    }
    return grenade;
}

void install_contextual_throwback_experiment(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
#else
    if constexpr (!kContextualThrowbackCandidateClearedForLive) {
        static_cast<void>(bindings);
        WAWVR_STEREO_DIAG_ONCE(
            "ThrowbackContextDiag contextual-anywhere candidate unavailable: native replay/shutdown/inventory safety review incomplete; no experimental hooks installed; existing hip route unchanged");
        return;
    }
    std::array<wchar_t, 2> enabled{};
    if (GetEnvironmentVariableW(kContextualThrowbackExperimentEnvironment,
            enabled.data(), static_cast<DWORD>(enabled.size())) != 1 ||
        enabled[0] != L'1') {
        WAWVR_STEREO_DIAG_ONCE(
            "ThrowbackContextDiag contextual-anywhere experiment disabled by default; existing hip route unchanged");
        return;
    }
    struct Fingerprint final {
        wawvr::t4::Rva rva;
        std::size_t size;
        std::uint64_t fnv1a64;
    };
    constexpr std::array<Fingerprint, 6> fingerprints{{
        {0x000214E0, 328, 0x6938992BCAD0A97DULL},
        {0x00021630, 179, 0xD177B2C5F90C4E09ULL},
        {0x000E7910, 265, 0x4B9B8A182A08F0EFULL},
        {0x000FD110, 53, 0xA3693D034C68547CULL},
        {0x000FD210, 355, 0xA7540E93320E5D11ULL},
        {0x0014EDC0, 226, 0xFD34C9C368D55DFAULL},
    }};
    const auto fingerprints_match = [&]() noexcept {
        for (const auto& item : fingerprints) {
            const auto address = bindings.module().address(item.rva, item.size);
            if (!address.has_value() || !accessible_range(
                    reinterpret_cast<const void*>(*address), item.size))
                return false;
            const auto* const bytes = reinterpret_cast<const std::uint8_t*>(
                *address);
            std::uint64_t hash = 14695981039346656037ULL;
            for (std::size_t index = 0; index < item.size; ++index)
                hash = (hash ^ bytes[index]) * 1099511628211ULL;
            if (hash != item.fnv1a64) return false;
        }
        return true;
    };
    const auto fire = bindings.module().address(kContextFireEntryRva, 9);
    const auto pickup_call = bindings.module().address(kContextPickupCallRva, 5);
    const auto pickup = bindings.module().address(kContextPickupTargetRva, 265);
    const auto native_end = bindings.module().address(kContextNativeEndRva, 179);
    const auto predicted = bindings.data_address(
        wawvr::t4::DataSymbolId::predicted_player_state, sizeof(std::uint32_t));
    const auto handler_slot = bindings.module().address(0x004DBF4C, 4);
    const auto handler = bindings.module().address(0x000FD110, 53);
    constexpr std::array<std::array<std::uint32_t, 4>, 3> descriptors{{
        {{0x00897BE4, 0x4C, 0xA, 0}},
        {{0x00897EA8, 0x50, 0xFFFFFFF0, 0}},
        {{0x00897F00, 0x870, 0xA, 0}},
    }};
    constexpr std::array<wawvr::t4::Rva, 3> descriptor_rvas{
        0x00437130, 0x00436E80, 0x00436E20};
    bool fields_match = true;
    for (std::size_t index = 0; index < descriptor_rvas.size(); ++index) {
        const auto address = bindings.module().address(descriptor_rvas[index], 16);
        fields_match = fields_match && address.has_value() &&
            accessible_range(reinterpret_cast<const void*>(*address), 16) &&
            std::memcmp(reinterpret_cast<const void*>(*address),
                        descriptors[index].data(), 16) == 0;
    }
    std::uint32_t actual_handler = 0;
    if (handler_slot.has_value() && accessible_range(
            reinterpret_cast<const void*>(*handler_slot), 4)) {
        std::memcpy(&actual_handler,
                    reinterpret_cast<const void*>(*handler_slot), 4);
    }
    if (!fire.has_value() || !pickup_call.has_value() || !pickup.has_value() ||
        !native_end.has_value() || !predicted.has_value() ||
        !handler.has_value() || actual_handler != *handler || !fields_match ||
        !fingerprints_match() ||
        !bytes_equal(reinterpret_cast<const void*>(*fire), kContextFireDisplaced) ||
        !bytes_equal(reinterpret_cast<const void*>(*pickup_call - 6),
                     kContextPickupCallPrefix) ||
        !bytes_equal(reinterpret_cast<const void*>(*pickup_call + 5),
                     kContextPickupCallSuffix) ||
        decode_rel32(reinterpret_cast<const std::uint8_t*>(*pickup_call)) !=
            *pickup) {
        stereo_diagnostic_log(
            "ThrowbackContextDiag experiment rejected: exact native code/data/ABI gates do not match; existing hip route unchanged");
        return;
    }
    auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, 14, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (trampoline == nullptr) return;
    std::memcpy(trampoline, kContextFireDisplaced.data(), 9);
    std::array<std::uint8_t, 5> resume{};
    if (!build_rel32(reinterpret_cast<std::uintptr_t>(trampoline + 9),
                     *fire + 9, &resume)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return;
    }
    resume[0] = 0xE9;
    std::memcpy(trampoline + 9, resume.data(), 5);
    DWORD old = 0;
    if (!VirtualProtect(trampoline, 14, PAGE_EXECUTE_READ, &old) ||
        !FlushInstructionCache(GetCurrentProcess(), trampoline, 14)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return;
    }
    std::array<std::uint8_t, 9> fire_patch{};
    fire_patch.fill(0x90);
    std::array<std::uint8_t, 5> branch{};
    if (!build_rel32(*fire, reinterpret_cast<std::uintptr_t>(
            &context_native_fire_gate_bridge), &branch)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return;
    }
    branch[0] = 0xE9;
    std::copy(branch.begin(), branch.end(), fire_patch.begin());
    const auto write_fire = [&](const std::array<std::uint8_t, 9>& bytes) noexcept {
        DWORD old_protection = 0;
        auto* const target = reinterpret_cast<void*>(*fire);
        if (!VirtualProtect(target, bytes.size(), PAGE_EXECUTE_READWRITE,
                            &old_protection)) return false;
        std::memcpy(target, bytes.data(), bytes.size());
        const bool flushed = FlushInstructionCache(
            GetCurrentProcess(), target, bytes.size()) != FALSE;
        DWORD ignored = 0;
        const bool restored = VirtualProtect(target, bytes.size(),
            old_protection, &ignored) != FALSE;
        return flushed && restored && bytes_equal(target, bytes);
    };
    const std::array<PeerThreadPatchRange, 2> ranges{{
        {*fire, 9}, {*pickup_call, 5}}};
    // A suspended peer may own the mod mutex. Initialize protected state
    // before suspending anyone, while contextual input is still unavailable.
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_contextual_transaction = {};
    }
    SuspendedPeerThreads peers;
    PeerThreadQuiesceResult quiesce{};
    if (!peers.suspend(ranges, &quiesce) || !fingerprints_match()) {
        peers.resume();
        VirtualFree(trampoline, 0, MEM_RELEASE);
        stereo_diagnostic_log(
            "ThrowbackContextDiag experiment patch quiescence/fingerprint rejected error=%lu",
            quiesce.system_error);
        return;
    }
    // Dependencies are valid before either branch is exposed. The feature
    // remains inactive until both patches are verified while peers are stopped.
    g_contextual_predicted_player_state = *predicted;
    g_contextual_native_pickup = *pickup;
    g_contextual_native_end = *native_end;
    g_contextual_fire_trampoline = trampoline;
    DWORD error = 0;
    const auto pickup_bridge = reinterpret_cast<std::uintptr_t>(
        &context_native_pickup_bridge);
    if (!write_fire(fire_patch) ||
        !patch_call(reinterpret_cast<std::uint8_t*>(*pickup_call),
                    pickup_bridge, &error)) {
        DWORD rollback_error = 0;
        const bool call_restored = patch_call(
            reinterpret_cast<std::uint8_t*>(*pickup_call), *pickup,
            &rollback_error);
        const bool fire_restored = write_fire(kContextFireDisplaced);
        if (call_restored && fire_restored) {
            g_contextual_fire_trampoline = nullptr;
            VirtualFree(trampoline, 0, MEM_RELEASE);
        }
        // If a rollback cannot be proved, retain the pass-through dependencies
        // and allocation. Never expose contextual input after a partial patch.
        peers.resume();
        stereo_diagnostic_log(
            "ThrowbackContextDiag experiment patch failed error=%lu rollback=%d; contextual input disabled",
            error, call_restored && fire_restored ? 1 : 0);
        return;
    }
    g_contextual_transaction_active.store(false, std::memory_order_release);
    g_contextual_experiment_installed.store(true, std::memory_order_release);
    peers.resume();
    stereo_diagnostic_log(
        "ThrowbackContextDiag exact-SP contextual experiment installed; native pickup/free witness and pre-event fire gate required; headset acceptance=0");
#endif
}

}  // namespace

ManualGrenadeRuntimeInstallResult install_manual_grenade_runtime(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    ManualGrenadeRuntimeInstallResult result{};
    if (g_installed.load(std::memory_order_acquire)) {
        result.status = ManualGrenadeRuntimeStatus::already_installed;
        return result;
    }
    if (select_t4_layout_family(bindings.profile()) !=
        T4LayoutFamily::single_player_1_7_1263) {
        result.status = ManualGrenadeRuntimeStatus::not_applicable;
        return result;
    }
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    result.status = ManualGrenadeRuntimeStatus::rejected_wrong_profile;
    return result;
#else
    const auto context_address = bindings.module().address(
        kFireGrenadeContextRva, kFireGrenadeContext.size());
    const auto call_address = bindings.module().address(
        kFireGrenadeCallRva, 5);
    const auto target_address = bindings.module().address(
        kFireGrenadeTargetRva,
        kFireGrenadeEpilogueOffset + kFireGrenadeEpilogue.size());
    const auto locational_trace_address = bindings.module().address(
        kLocationalTraceRva, kLocationalTraceBytes.size());
    const auto setup_ignore_entity_parameters_address =
        bindings.module().address(kSetupIgnoreEntityParametersRva, 1);
    const auto server_trace_address = bindings.module().address(
        kServerTraceRva, 1);
    const auto bullet_priority_map_address = bindings.module().address(
        kBulletPriorityMapRva, kBulletPriorityMapBytes.size());
    const auto last_weapon_index_address = bindings.module().address(
        kLastParsedWeaponIndexRva, sizeof(std::uint32_t));
    const auto weapon_table = bindings.data_address(
        wawvr::t4::DataSymbolId::weapon_definition_pointer_table,
        kWeaponDefinitionTableSpan);
    const auto local_player = bindings.data_address(
        wawvr::t4::DataSymbolId::local_player_entity,
        kLocalPlayerEntityIdentitySpan);
    if (!context_address.has_value() || !call_address.has_value() ||
        !target_address.has_value() ||
        !locational_trace_address.has_value() ||
        !setup_ignore_entity_parameters_address.has_value() ||
        !server_trace_address.has_value() ||
        !bullet_priority_map_address.has_value() ||
        !last_weapon_index_address.has_value() ||
        !weapon_table.has_value() || !local_player.has_value()) {
        result.status = ManualGrenadeRuntimeStatus::address_out_of_range;
        return result;
    }
    auto* const context = reinterpret_cast<std::uint8_t*>(*context_address);
    auto* const callsite = reinterpret_cast<std::uint8_t*>(*call_address);
    const auto target = *target_address;
    auto* const locational_trace = reinterpret_cast<std::uint8_t*>(
        *locational_trace_address);
    if (!context_matches(context, callsite) ||
        !bytes_equal(
            reinterpret_cast<const void*>(target),
            kFireGrenadePrologue) ||
        !bytes_equal(
            reinterpret_cast<const void*>(
                target + kFireGrenadeEpilogueOffset),
            kFireGrenadeEpilogue) ||
        !bytes_equal(locational_trace, kLocationalTraceBytes) ||
        decode_rel32(
            locational_trace +
                kSetupIgnoreEntityParametersCallOffset) !=
            *setup_ignore_entity_parameters_address ||
        decode_rel32(
            locational_trace + kServerTraceCallOffset) !=
            *server_trace_address ||
        !bytes_equal(
            reinterpret_cast<const void*>(
                *bullet_priority_map_address),
            kBulletPriorityMapBytes)) {
        result.status = ManualGrenadeRuntimeStatus::fingerprint_mismatch;
        return result;
    }
    const auto bridge = reinterpret_cast<std::uintptr_t>(
        &manual_grenade_fire_bridge);
    const auto decoded = decode_rel32(callsite);
    if (decoded == bridge) {
        if (g_original_fire_grenade !=
                reinterpret_cast<FireGrenadeFunction>(target) ||
            g_weapon_definition_pointer_table != *weapon_table ||
            g_local_player_entity != *local_player ||
            g_locational_trace != *locational_trace_address ||
            g_bullet_priority_map != *bullet_priority_map_address ||
            g_last_weapon_index != *last_weapon_index_address) {
            result.status = ManualGrenadeRuntimeStatus::fingerprint_mismatch;
            return result;
        }
        g_button_grenades_enabled.store(
            button_grenades_enabled_from_environment(),
            std::memory_order_release);
        g_enabled.store(true, std::memory_order_release);
        g_installed.store(true, std::memory_order_release);
        result.status = ManualGrenadeRuntimeStatus::already_installed;
        return result;
    }
    if (decoded != target) {
        result.status = ManualGrenadeRuntimeStatus::fingerprint_mismatch;
        return result;
    }

    SuspendedPeerThreads peers;
    PeerThreadQuiesceResult quiesce{};
    if (!peers.suspend(
            {reinterpret_cast<std::uintptr_t>(callsite), 5},
            &quiesce)) {
        result.status = ManualGrenadeRuntimeStatus::thread_suspend_failed;
        result.system_error = quiesce.system_error;
        return result;
    }
    // Publish every pass-through dependency before exposing the E8 patch.
    g_original_fire_grenade =
        reinterpret_cast<FireGrenadeFunction>(target);
    g_weapon_definition_pointer_table = *weapon_table;
    g_local_player_entity = *local_player;
    g_locational_trace = *locational_trace_address;
    g_bullet_priority_map = *bullet_priority_map_address;
    g_last_weapon_index = *last_weapon_index_address;

    DWORD patch_error = 0;
    if (!patch_call(callsite, bridge, &patch_error) ||
        decode_rel32(callsite) != bridge ||
        !context_matches(context, callsite)) {
        DWORD rollback_error = 0;
        const bool restored = patch_call(
            callsite, target, &rollback_error) &&
            decode_rel32(callsite) == target &&
            context_matches(context, callsite);
        result.system_error = patch_error != 0
            ? patch_error
            : rollback_error != 0
                ? rollback_error
                : ERROR_WRITE_FAULT;
        static_cast<void>(restored);
        result.status = ManualGrenadeRuntimeStatus::patch_failed;
        return result;
    }
    peers.resume();
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_state = {};
    }
    g_native_command_policy.store(
        NativeGrenadeCommandPolicy::passthrough,
        std::memory_order_release);
    g_left_hand_reserved.store(false, std::memory_order_release);
    g_button_grenades_enabled.store(
        button_grenades_enabled_from_environment(),
        std::memory_order_release);
    g_enabled.store(true, std::memory_order_release);
    g_installed.store(true, std::memory_order_release);
    result.status = ManualGrenadeRuntimeStatus::installed;
    install_grenade_native_observer(bindings);
    install_contextual_throwback_experiment(bindings);
    return result;
#endif
}

bool button_grenade_mode_enabled() noexcept {
    return g_enabled.load(std::memory_order_acquire) &&
        g_button_grenades_enabled.load(std::memory_order_acquire);
}

std::int32_t read_manual_grenade_throwback_time_left() noexcept {
    const auto sampled = g_throwback_sample_milliseconds.load(std::memory_order_acquire);
    const auto now = GetTickCount64();
    if (!g_enabled.load(std::memory_order_acquire) || sampled == 0 ||
        now < sampled || now - sampled > kMaximumControllerFrameAgeMilliseconds) {
        return 0;
    }
    return g_throwback_time_left.load(std::memory_order_acquire);
}

bool contextual_manual_grenade_throwback_available() noexcept {
    return kContextualThrowbackCandidateClearedForLive &&
        g_enabled.load(std::memory_order_acquire) &&
        g_contextual_experiment_installed.load(std::memory_order_acquire) &&
        !g_button_grenades_enabled.load(std::memory_order_acquire);
}

ManualGrenadeCommandUpdate update_manual_grenade_command(
    const ControllerFrameSnapshot& controller,
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const ManualGrenadeGameplaySession gameplay_session,
    const bool input_owned,
    const bool new_grab_blocked,
    const std::uint8_t native_offhand_index,
    std::uint32_t* const buttons) noexcept {
    ManualGrenadeCommandUpdate result{};
    if (!g_enabled.load(std::memory_order_acquire) || buttons == nullptr) {
        return result;
    }
    const std::uint32_t frag_mask = grenade_button_mask(
        ManualGrenadeSlot::frag);
    const std::uint32_t tactical_mask = grenade_button_mask(
        ManualGrenadeSlot::tactical);
    const std::uint32_t all_offhand_masks = frag_mask | tactical_mask;
    const std::uint32_t original_offhand_bits =
        *buttons & all_offhand_masks;
    const auto context_now = GetTickCount64();
    const bool contextual_input_owned = input_owned &&
        gameplay_session == ManualGrenadeGameplaySession::active &&
        controller_frame_is_current(controller, context_now) &&
        controller.frame.actions.focused;
    const std::int32_t contextual_throwback_time = contextual_input_owned
        ? sample_native_throwback_time_left() : 0;
#if defined(_MSC_VER) && defined(_M_IX86)
    observe_grenade_native_window(contextual_input_owned);
#endif
    g_throwback_time_left.store(contextual_throwback_time, std::memory_order_release);
    g_throwback_sample_milliseconds.store(context_now, std::memory_order_release);
    if (g_button_grenades_enabled.load(std::memory_order_acquire)) {
        try {
            const std::uint64_t now_milliseconds = GetTickCount64();
            const auto& actions = controller.frame.actions;
            const auto& left = actions.hands[
                static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
            const bool input_valid = input_owned &&
                controller_frame_is_current(controller, now_milliseconds) &&
                actions.focused && actions.sequence != 0 &&
                left.trigger.active && std::isfinite(left.trigger.current);

            std::lock_guard<std::mutex> lock(g_state_mutex);
            bool hold_native_frag = g_state.button_trigger.trigger_held;
            static_cast<void>(update_button_grenade_trigger(
                gameplay_session == ManualGrenadeGameplaySession::inactive,
                input_valid, left.trigger.current,
                new_grab_blocked,
                &g_state.button_trigger, &hold_native_frag));

            g_left_hand_reserved.store(false, std::memory_order_release);
            g_native_command_policy.store(
                hold_native_frag
                    ? NativeGrenadeCommandPolicy::hold_frag
                    : NativeGrenadeCommandPolicy::passthrough,
                std::memory_order_release);
            *buttons = merge_button_grenade_native_hold(
                *buttons, all_offhand_masks, frag_mask,
                hold_native_frag);
            if (hold_native_frag) {
                result.native_button_injected = true;
            }
            return result;
        } catch (...) {
            const bool hold_native_frag =
                g_native_command_policy.load(std::memory_order_acquire) ==
                NativeGrenadeCommandPolicy::hold_frag;
            g_left_hand_reserved.store(false, std::memory_order_release);
            *buttons = merge_button_grenade_native_hold(
                *buttons, all_offhand_masks, frag_mask,
                hold_native_frag);
            if (hold_native_frag) {
                result.native_button_injected = true;
            }
            return result;
        }
    }
    const NativeGrenadeCommandPolicy inherited_policy =
        g_native_command_policy.load(std::memory_order_acquire);
    const std::uint32_t inherited_hold_mask =
        native_hold_mask_for_policy(inherited_policy);
    if (inherited_hold_mask != 0) {
        *buttons = (*buttons & ~all_offhand_masks) |
            inherited_hold_mask;
    } else if (inherited_policy ==
               NativeGrenadeCommandPolicy::release_pending) {
        *buttons &= ~all_offhand_masks;
    }
    try {
        const std::uint64_t now_milliseconds = GetTickCount64();
        const auto& actions = controller.frame.actions;
        const auto& left = actions.hands[
            static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
        const bool frame_current = controller_frame_is_current(
            controller, now_milliseconds);
        wawvr::xr::EnginePose held_world{};
        ManualGrenadePoint head_local{};
        ManualGrenadePoint anchor_local{};
        ManualGrenadePoint fallback_forward{};
        const bool pose_valid = build_hand_sample(
            controller, camera_origin, camera_axis, &held_world,
            &head_local, &anchor_local, &fallback_forward);
        const bool input_valid = input_owned && frame_current &&
            actions.focused && actions.sequence != 0 &&
            left.trigger.active && std::isfinite(left.trigger.current) &&
            pose_valid;
        const std::uint64_t sample_nanoseconds =
            controller.frame.predicted_display_time > 0
            ? static_cast<std::uint64_t>(
                  controller.frame.predicted_display_time)
            : now_milliseconds * 1'000'000ULL;

        std::lock_guard<std::mutex> lock(g_state_mutex);
        const auto restore_original_offhand_bits = [&]() noexcept {
            *buttons = (*buttons & ~all_offhand_masks) |
                original_offhand_bits;
        };
        const auto publish_result = [&]() noexcept {
            result.grenade_held =
                g_state.gesture.stage == ManualGrenadeStage::holding;
            result.interaction_active = result.grenade_held ||
                g_state.gesture.stage ==
                    ManualGrenadeStage::released_pending;
            result.left_hand_reserved = result.interaction_active ||
                g_state.awaiting_left_grip_release;
            g_left_hand_reserved.store(
                result.left_hand_reserved, std::memory_order_release);
        };
        if (g_contextual_transaction.stage ==
                ContextualThrowbackStage::waiting_for_pickup &&
            now_milliseconds >
                g_contextual_transaction.pickup_deadline_milliseconds) {
            abort_contextual_throwback(&g_contextual_transaction);
        }
        if (gameplay_session == ManualGrenadeGameplaySession::inactive &&
            g_contextual_transaction.stage ==
                ContextualThrowbackStage::acquired) {
            // An explicit world teardown is distinct from temporary focus or
            // tracking loss. Keep a tombstone against queued old-world events.
            g_contextual_transaction.stage = ContextualThrowbackStage::aborted;
        }
        if (g_contextual_transaction.stage == ContextualThrowbackStage::spent ||
            g_contextual_transaction.stage == ContextualThrowbackStage::aborted) {
            ContextNativePlayerState server{};
            ContextNativePlayerState predicted{};
            const bool server_idle = read_context_native_player_state(
                context_server_player_state(), &server) &&
                server.weapon_state == 0;
            const bool predicted_idle = read_context_native_player_state(
                g_contextual_predicted_player_state, &predicted) &&
                predicted.weapon_state == 0;
            const bool trigger_released = input_valid &&
                left.trigger.current <= kManualGrenadeTriggerRelease;
            const auto generation = g_contextual_transaction.generation;
            const bool retired = retire_contextual_throwback_tombstone(
                trigger_released, server_idle, predicted_idle,
                // Idle does not acknowledge old dispatcher entries. A native
                // queue-drain adapter is still missing, so retain tombstones.
                &g_contextual_transaction, nullptr);
            clear_transaction_locked(true, true, true);
            if (retired) {
                g_contextual_transaction_active.store(
                    false, std::memory_order_release);
                g_state.gesture.input_initialized = true;
                g_state.gesture.trigger_was_held = false;
                stereo_diagnostic_log(
                    "ThrowbackContextDiag tombstone retired transaction=%llu bothNativeIdle=1 triggerReleased=1",
                    static_cast<unsigned long long>(generation));
            }
            *buttons &= ~all_offhand_masks;
            g_native_command_policy.store(
                retired ? NativeGrenadeCommandPolicy::passthrough
                        : NativeGrenadeCommandPolicy::release_pending,
                std::memory_order_release);
            publish_result();
            return result;
        }
        // The fire bridge can retire a transaction after the initial atomic
        // snapshot but before this lock is acquired. Re-derive the command
        // policy from the mutex-protected state so a retired hold cannot leak
        // one stale offhand command.
        if (g_state.gesture.stage == ManualGrenadeStage::holding) {
            const std::uint32_t hold_mask = grenade_button_mask(
                g_state.gesture.held_slot);
            *buttons = (*buttons & ~all_offhand_masks) | hold_mask;
        } else if (g_state.gesture.stage ==
                   ManualGrenadeStage::released_pending) {
            *buttons &= ~all_offhand_masks;
        } else {
            restore_original_offhand_bits();
        }
        const auto suspend_held_grenade = [&]() noexcept {
            g_state.holding_suspended = true;
            g_state.held_world_valid = false;
            clear_manual_grenade_velocity_history(
                &g_state.velocity_history);
            g_state.last_action_sequence = 0;
            g_state.previous_hand_anchor_local_valid = false;
            g_state.previous_predicted_display_time = 0;
            g_state.velocity_tracking_anchor_valid = false;
            const std::uint32_t hold_mask = grenade_button_mask(
                g_state.gesture.held_slot);
            g_native_command_policy.store(
                native_policy_for_slot(g_state.gesture.held_slot),
                std::memory_order_release);
            *buttons = (*buttons & ~all_offhand_masks) | hold_mask;
            result.native_button_injected = hold_mask != 0;
            publish_result();
        };

        if (gameplay_session ==
            ManualGrenadeGameplaySession::inactive) {
            abort_contextual_throwback(&g_contextual_transaction);
            clear_transaction_locked(false, false, false);
            restore_original_offhand_bits();
            return result;
        }
        if (g_state.gesture.stage == ManualGrenadeStage::released_pending &&
            (g_state.pending_until_milliseconds == 0 ||
             now_milliseconds > g_state.pending_until_milliseconds)) {
            clear_transaction_locked(true, true, false);
            restore_original_offhand_bits();
            WAWVR_STEREO_DIAG_ONCE(
                "GrenadeDiag pending native throw expired after 3000ms; physical override safely rearmed");
        }

        const bool left_squeeze_held = left.squeeze.active &&
            std::isfinite(left.squeeze.current) &&
            left.squeeze.current > kLeftGripReleaseThreshold;
        if (g_state.awaiting_left_grip_release && !left_squeeze_held) {
            g_state.awaiting_left_grip_release = false;
        }

        if (g_state.gesture.stage ==
            ManualGrenadeStage::released_pending) {
            *buttons &= ~all_offhand_masks;
            g_native_command_policy.store(
                NativeGrenadeCommandPolicy::release_pending,
                std::memory_order_release);
            publish_result();
            return result;
        }

        if (g_state.gesture.stage == ManualGrenadeStage::holding &&
            !input_valid) {
            suspend_held_grenade();
            return result;
        }

        if (g_state.gesture.stage == ManualGrenadeStage::holding &&
            g_state.holding_suspended) {
            const float trigger_value = std::clamp(
                left.trigger.current, 0.0F, 1.0F);
            if (g_state.recovery_requires_repress) {
                if (trigger_value < kManualGrenadeTriggerEngage) {
                    suspend_held_grenade();
                    return result;
                }
                g_state.recovery_requires_repress = false;
            } else if (trigger_value <=
                       kManualGrenadeTriggerRelease) {
                g_state.recovery_requires_repress = true;
                suspend_held_grenade();
                return result;
            }
            g_state.holding_suspended = false;
            g_state.gesture.input_initialized = true;
            g_state.gesture.trigger_was_held = true;
            g_state.held_world = held_world;
            g_state.held_world_valid = true;
            g_state.last_action_sequence = actions.sequence;
            g_state.previous_hand_anchor_local = anchor_local;
            g_state.previous_hand_anchor_local_valid = true;
            g_state.previous_predicted_display_time =
                controller.frame.predicted_display_time;
            g_state.velocity_tracking_anchor = controller.tracking_anchor;
            g_state.velocity_tracking_anchor_valid = true;
            const std::uint32_t hold_mask = grenade_button_mask(
                g_state.gesture.held_slot);
            g_native_command_policy.store(
                native_policy_for_slot(g_state.gesture.held_slot),
                std::memory_order_release);
            *buttons = (*buttons & ~all_offhand_masks) | hold_mask;
            result.native_button_injected = hold_mask != 0;
            publish_result();
            return result;
        }

        const ManualGrenadeStage prior_stage = g_state.gesture.stage;
        ManualGrenadeEvent event = ManualGrenadeEvent::none;
        if (!update_manual_grenade_gesture(
                input_valid, left.trigger.active, left.trigger.current,
                head_local,
                new_grab_blocked || left_squeeze_held ||
                    g_contextual_transaction.stage !=
                        ContextualThrowbackStage::inactive,
                &g_state.gesture, &event,
                contextual_manual_grenade_throwback_available() &&
                    contextual_throwback_time > 0)) {
            return result;
        }
        if (event == ManualGrenadeEvent::reset) {
            clear_manual_grenade_velocity_history(
                &g_state.velocity_history);
            g_state.pending_until_milliseconds = 0;
            g_state.grenade_weapon_index = 0;
            g_state.projectile_model = nullptr;
            g_state.held_world_valid = false;
            g_state.previous_hand_anchor_local_valid = false;
            g_state.previous_predicted_display_time = 0;
            g_state.velocity_tracking_anchor_valid = false;
        } else if (event == ManualGrenadeEvent::grab_frag ||
                   event == ManualGrenadeEvent::grab_tactical) {
            GrenadeInventoryResolution inventory =
                resolve_grenade_inventory(g_state.gesture.held_slot);
            const bool contextual_request = g_state.gesture.contextual_throwback;
            ContextNativePickupSource contextual_source{};
            bool contextual_source_valid = !contextual_request;
            if (contextual_request &&
                read_context_native_pickup_source(&contextual_source)) {
                std::uintptr_t definition = 0;
                void* model = nullptr;
                contextual_source_valid = resolve_grenade_weapon(
                    contextual_source.weapon, ManualGrenadeSlot::frag,
                    &definition, &model);
                if (contextual_source_valid) {
                    inventory.weapon_index = contextual_source.weapon;
                    inventory.projectile_model = model;
                }
            }
            if (inventory.status != GrenadeInventoryStatus::available ||
                (contextual_request &&
                    (!inventory.frag_throwback_override ||
                     !contextual_source_valid))) {
                const ManualGrenadeSlot requested_slot =
                    g_state.gesture.held_slot;
                clear_transaction_locked(true, false, false);
                if (!contextual_request && inventory.status ==
                    GrenadeInventoryStatus::proven_empty) {
                    const std::uint32_t pulse_mask =
                        grenade_button_mask(requested_slot);
                    *buttons = (*buttons & ~all_offhand_masks) |
                        pulse_mask;
                    result.native_button_injected =
                        pulse_mask != 0;
                    WAWVR_STEREO_DIAG_ONCE(
                        "GrenadeDiag physical %s belt grab proved empty; emitted one native hint pulse",
                        requested_slot == ManualGrenadeSlot::frag
                            ? "frag" : "tactical");
                } else {
                    restore_original_offhand_bits();
                    WAWVR_STEREO_DIAG_ONCE(
                        "GrenadeDiag physical/contextual grab preflight unavailable; no native throw input emitted and no fresh-grenade fallback");
                }
                publish_result();
                return result;
            }
            clear_manual_grenade_velocity_history(
                &g_state.velocity_history);
            g_state.last_action_sequence = 0;
            g_state.previous_hand_anchor_local = anchor_local;
            g_state.previous_hand_anchor_local_valid = true;
            g_state.previous_predicted_display_time =
                controller.frame.predicted_display_time;
            g_state.velocity_tracking_anchor = controller.tracking_anchor;
            g_state.velocity_tracking_anchor_valid = true;
            g_state.grabbed_action_sequence = actions.sequence;
            g_state.released_action_sequence = 0;
            g_state.grenade_weapon_index = inventory.weapon_index;
            g_state.projectile_model = inventory.projectile_model;
            g_state.held_world = held_world;
            g_state.held_world_valid = true;
            g_state.awaiting_left_grip_release = true;
            g_state.holding_suspended = false;
            g_state.recovery_requires_repress = false;
            g_state.transaction_generation =
                next_transaction_generation();
            g_state.native_throwback_fuse_at_grab = inventory.frag_throwback_override
                ? contextual_request ? contextual_source.remaining_fuse
                                     : contextual_throwback_time
                : 0;
            if (contextual_request) {
                if (!begin_contextual_throwback(now_milliseconds,
                        g_state.transaction_generation, inventory.weapon_index,
                        contextual_source.remaining_fuse,
                        &g_contextual_transaction)) {
                    clear_transaction_locked(true, false, false);
                    *buttons &= ~all_offhand_masks;
                    publish_result();
                    return result;
                }
                g_contextual_transaction_active.store(
                    true, std::memory_order_release);
                stereo_diagnostic_log(
                    "ThrowbackContextDiag pending enemy-only acquisition transaction=%llu source=%p weapon=%u nativeWindow=%d; ownAmmoUntouched=1",
                    static_cast<unsigned long long>(g_state.transaction_generation),
                    reinterpret_cast<void*>(contextual_source.entity),
                    inventory.weapon_index, contextual_source.remaining_fuse);
            }
            g_native_command_policy.store(
                native_policy_for_slot(g_state.gesture.held_slot),
                std::memory_order_release);
            if (inventory.frag_throwback_override && !contextual_request) {
                stereo_diagnostic_log(
                    "GrenadeDiag existing hip-route live-frag request: remainingFuse=%d nativeAcquisitionWitness=unavailable contextualAnywhereEnabled=0 transaction=%llu sequence=%llu weapon=%u",
                    contextual_throwback_time,
                    static_cast<unsigned long long>(g_state.transaction_generation),
                    static_cast<unsigned long long>(actions.sequence),
                    inventory.weapon_index);
            } else {
                WAWVR_STEREO_DIAG_ONCE(
                    "GrenadeDiag physical %s grabbed from %s hip with the left index trigger",
                    event == ManualGrenadeEvent::grab_frag
                        ? "frag" : "tactical",
                    event == ManualGrenadeEvent::grab_frag
                        ? "left" : "right");
            }
        }

        const bool sample_belongs_to_throw =
            prior_stage == ManualGrenadeStage::holding ||
            g_state.gesture.stage == ManualGrenadeStage::holding ||
            event == ManualGrenadeEvent::release;
        if (sample_belongs_to_throw && pose_valid) {
            g_state.held_world = held_world;
            g_state.held_world_valid = true;
            static_cast<void>(update_velocity_sample_locked(
                controller, camera_axis, anchor_local,
                sample_nanoseconds));
        }

        if (native_offhand_index != 0 &&
            native_offhand_index == g_state.grenade_weapon_index &&
            (g_state.gesture.stage == ManualGrenadeStage::holding ||
             g_state.gesture.stage ==
                 ManualGrenadeStage::released_pending)) {
            std::uintptr_t definition = 0;
            void* model = nullptr;
            if (resolve_grenade_weapon(
                    native_offhand_index, g_state.gesture.held_slot,
                    &definition, &model)) {
                static_cast<void>(definition);
                g_state.grenade_weapon_index = native_offhand_index;
                g_state.projectile_model = model;
            }
        }

        if (event == ManualGrenadeEvent::release) {
            g_state.release_origin_world = {
                g_state.held_world.position.x,
                g_state.held_world.position.y,
                g_state.held_world.position.z,
            };
            g_state.release_velocity_world = {};
            g_state.release_velocity_sample_age_nanoseconds = 0;
            static_cast<void>(select_manual_grenade_release_velocity(
                sample_nanoseconds, g_state.velocity_history,
                &g_state.release_velocity_world,
                &g_state.release_velocity_sample_age_nanoseconds));
            g_state.release_fallback_forward_world = fallback_forward;
            g_state.released_at_milliseconds = now_milliseconds;
            g_state.released_action_sequence = actions.sequence;
            g_state.pending_until_milliseconds =
                now_milliseconds + kPendingMilliseconds;
            g_state.held_world_valid = false;
            g_state.holding_suspended = false;
            g_state.recovery_requires_repress = false;
            g_native_command_policy.store(
                NativeGrenadeCommandPolicy::release_pending,
                std::memory_order_release);
            *buttons &= ~all_offhand_masks;
            WAWVR_STEREO_DIAG_ONCE(
                "GrenadeDiag physical trigger release queued one native throw sample");
        }

        if (g_state.gesture.stage == ManualGrenadeStage::holding) {
            const std::uint32_t hold_mask = grenade_button_mask(
                g_state.gesture.held_slot);
            g_native_command_policy.store(
                native_policy_for_slot(g_state.gesture.held_slot),
                std::memory_order_release);
            *buttons = (*buttons & ~all_offhand_masks) | hold_mask;
            result.native_button_injected = hold_mask != 0;
        } else if (g_state.gesture.stage ==
                   ManualGrenadeStage::released_pending) {
            g_native_command_policy.store(
                NativeGrenadeCommandPolicy::release_pending,
                std::memory_order_release);
            *buttons &= ~all_offhand_masks;
        }
        publish_result();
        return result;
    } catch (...) {
        const NativeGrenadeCommandPolicy policy =
            g_native_command_policy.load(std::memory_order_acquire);
        const std::uint32_t hold_mask =
            native_hold_mask_for_policy(policy);
        if (hold_mask != 0) {
            *buttons = (*buttons & ~all_offhand_masks) | hold_mask;
            result.interaction_active = true;
            result.grenade_held = true;
            result.native_button_injected = true;
        } else if (policy ==
                   NativeGrenadeCommandPolicy::release_pending) {
            *buttons &= ~all_offhand_masks;
            result.interaction_active = true;
        }
        result.left_hand_reserved =
            g_left_hand_reserved.load(std::memory_order_acquire);
        return result;
    }
}

void manual_grenade_observe_player_state(
    const void* const player_state) noexcept {
    if (!g_enabled.load(std::memory_order_acquire) ||
        g_button_grenades_enabled.load(std::memory_order_acquire) ||
        !accessible_range(player_state, kPlayerStateOffhandSpan)) {
        return;
    }
    std::int32_t offhand_index = 0;
    std::memcpy(
        &offhand_index,
        static_cast<const std::uint8_t*>(player_state) +
            kPlayerStateOffhandIndexOffset,
        sizeof(offhand_index));
    if (offhand_index <= 0 ||
        offhand_index >
            static_cast<std::int32_t>(kMaximumGrenadeWeaponIndex)) {
        return;
    }
    try {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (g_state.gesture.stage != ManualGrenadeStage::holding &&
            g_state.gesture.stage !=
                ManualGrenadeStage::released_pending) {
            return;
        }
        if (g_state.grenade_weapon_index != 0 &&
            g_state.grenade_weapon_index !=
                static_cast<std::uint32_t>(offhand_index)) {
            return;
        }
        std::uintptr_t definition = 0;
        void* model = nullptr;
        if (resolve_grenade_weapon(
                static_cast<std::uint32_t>(offhand_index),
                g_state.gesture.held_slot, &definition, &model)) {
            static_cast<void>(definition);
            g_state.grenade_weapon_index =
                static_cast<std::uint32_t>(offhand_index);
            g_state.projectile_model = model;
        }
    } catch (...) {
    }
}

bool manual_grenade_reserves_left_hand() noexcept {
    return g_enabled.load(std::memory_order_acquire) &&
        !g_button_grenades_enabled.load(std::memory_order_acquire) &&
        g_left_hand_reserved.load(std::memory_order_acquire);
}

bool manual_grenade_view_override_active() noexcept {
    if (!g_enabled.load(std::memory_order_acquire) ||
        g_button_grenades_enabled.load(std::memory_order_acquire)) {
        return false;
    }
    try {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        const std::uint64_t now_milliseconds = GetTickCount64();
        return g_state.gesture.stage == ManualGrenadeStage::holding ||
            g_state.gesture.stage == ManualGrenadeStage::released_pending ||
            (g_state.view_override_until_milliseconds != 0 &&
             now_milliseconds <
                 g_state.view_override_until_milliseconds);
    } catch (...) {
        return false;
    }
}

bool read_manual_grenade_render_state(
    void** const projectile_model,
    wawvr::xr::EnginePose* const world_pose) noexcept {
    if (projectile_model == nullptr || world_pose == nullptr) {
        return false;
    }
    *projectile_model = nullptr;
    *world_pose = {};
    if (!g_enabled.load(std::memory_order_acquire) ||
        g_button_grenades_enabled.load(std::memory_order_acquire)) {
        return false;
    }
    try {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        if (g_state.gesture.stage != ManualGrenadeStage::holding ||
            g_state.projectile_model == nullptr ||
            !g_state.held_world_valid ||
            !finite_vector(g_state.held_world.position) ||
            !valid_basis(g_state.held_world.axis)) {
            return false;
        }
        *projectile_model = g_state.projectile_model;
        *world_pose = g_state.held_world;
        return true;
    } catch (...) {
        return false;
    }
}

void request_manual_grenade_shutdown() noexcept {
#if defined(_MSC_VER) && defined(_M_IX86)
    // Installed CALL adapters retain immutable native pass-through targets.
    // Shutdown stops observation; it does not authorize or clear event queues.
    g_grenade_native_observer_enabled.store(false, std::memory_order_release);
    g_grenade_queue_observer_enabled.store(false, std::memory_order_release);
#endif
    g_enabled.store(false, std::memory_order_release);
    g_native_command_policy.store(
        NativeGrenadeCommandPolicy::passthrough,
        std::memory_order_release);
    g_left_hand_reserved.store(false, std::memory_order_release);
}

const char* manual_grenade_runtime_status_name(
    const ManualGrenadeRuntimeStatus status) noexcept {
    switch (status) {
    case ManualGrenadeRuntimeStatus::installed: return "installed";
    case ManualGrenadeRuntimeStatus::already_installed:
        return "already_installed";
    case ManualGrenadeRuntimeStatus::not_applicable:
        return "not_applicable";
    case ManualGrenadeRuntimeStatus::dependency_unavailable:
        return "dependency_unavailable";
    case ManualGrenadeRuntimeStatus::rejected_wrong_profile:
        return "rejected_wrong_profile";
    case ManualGrenadeRuntimeStatus::address_out_of_range:
        return "address_out_of_range";
    case ManualGrenadeRuntimeStatus::fingerprint_mismatch:
        return "fingerprint_mismatch";
    case ManualGrenadeRuntimeStatus::thread_suspend_failed:
        return "thread_suspend_failed";
    case ManualGrenadeRuntimeStatus::patch_failed:
        return "patch_failed";
    }
    return "unknown";
}

}  // namespace wawvr::mod
