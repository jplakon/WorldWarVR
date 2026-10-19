// SPDX-License-Identifier: GPL-3.0-only
#include "server_impact_audit.hpp"
#include "peer_thread_quiescence.hpp"
#include "resident_page_access.hpp"
#include "stereo_diagnostics.hpp"
#include "t4_layout_selector.hpp"
#include "sight_audit.hpp"
// COLT_ZERO_DEFERRED_OBSERVER_STATUS_BEGIN
#include "server_impact_install_diagnostic.hpp"
// COLT_ZERO_DEFERRED_OBSERVER_STATUS_END

#include <windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>

namespace wawvr::mod {
namespace {
using wawvr::xr::Vec3f;
struct BulletFireParams final {
    std::int32_t weapon_entity{}, ignore_entity{};
    float damage_multiplier{};
    std::int32_t method_of_death{};
    Vec3f original_start{}, start{}, end{}, direction{};
};
struct TraceResults final {
    std::array<std::byte, 0x10> normal{};
    float fraction{};
    std::array<std::byte, 0x1A> other_trace{};
    std::uint8_t all_solid{}, start_solid{};
    std::array<std::byte, 4> trace_tail{};
    std::uint32_t hit_entity{};
    Vec3f hit_position{};
    std::array<std::byte, 8> tail{};
};
static_assert(sizeof(BulletFireParams)==0x40);
static_assert(offsetof(BulletFireParams,start)==0x1C);
static_assert(offsetof(BulletFireParams,direction)==0x34);
static_assert(sizeof(TraceResults)==0x4C);
static_assert(offsetof(TraceResults,all_solid)==0x2E);
static_assert(offsetof(TraceResults,hit_position)==0x38);
struct ShotContext final {
    bool valid{};
    std::uint32_t sequence{}, impact_count{};
    std::uint64_t milliseconds{};
    const void* attacker{};
    const void* definition{};
    ServerImpactAuditIdentity identity{};
};
thread_local ShotContext g_shot{};
std::atomic<bool> g_installed{false}, g_enabled{false};
std::atomic<std::uint32_t> g_shot_sequence{0}, g_impact_count{0};
ServerImpactIdentityReader g_identity_reader{};
// COLT_ZERO_DEFERRED_OBSERVER_STATUS_BEGIN
DeferredServerImpactInstallDiagnostic g_install_diagnostic;
// COLT_ZERO_DEFERRED_OBSERVER_STATUS_END
constexpr std::array<std::uintptr_t,2> kSeams{0x004E613FU,0x004E6254U};
constexpr std::array<std::uint8_t,5> kOriginal{0x83,0xC4,0x10,0x84,0xC0};
std::array<std::array<std::uint8_t,5>,2> g_replacements{};

// Pinned against read-only mapped retail SP PID37204 on 2026-10-16. The
// validated profile independently proves FireWeapon -> Bullet_Fire and its
// parameter ABI. These additional sentinels prove dispatch, server trace ABI,
// both initial result seams, hit-position construction, and native damage use.
constexpr std::array<std::uint8_t,27> kTraceEntry{
    0x83,0xEC,0x10,0x53,0x8B,0x5C,0x24,0x18,0x55,0x57,0x6A,0x4C,
    0x6A,0x00,0x56,0xE8,0xFC,0xF5,0x0F,0x00,0x8B,0x44,0x24,0x30,
    0x83,0xC4,0x0C};
constexpr std::array<std::uint8_t,33> kPlainContext{
    0xC7,0x44,0x24,0x0C,0,0,0,0,0x6A,0,0x53,0x55,0x57,
    0x8D,0x74,0x24,0x24,0xE8,0x41,0xFA,0xFF,0xFF,
    0x83,0xC4,0x10,0x84,0xC0,0x0F,0x84,0x92,0,0,0};
constexpr std::array<std::uint8_t,16> kPenetratingContext{
    0xE8,0x2C,0xF9,0xFF,0xFF,0x83,0xC4,0x10,0x84,0xC0,
    0x0F,0x84,0x96,0x05,0,0};
constexpr std::array<std::uint8_t,22> kPenetratingArgs{
    0x56,0x6A,0,0x53,0x55,0x57,0x8D,0x74,0x24,0x48,
    0xF3,0x0F,0x11,0x44,0x24,0x48,0xF3,0x0F,0x11,0x44,0x24,0x4C};
constexpr std::array<std::uint8_t,23> kPlainDispatch{
    0x8B,0x46,0x3C,0x57,0x50,0x8D,0x7C,0x24,0x38,
    0xE8,0x54,0xF5,0xFF,0xFF,0x8B,0x7D,0x08,0x83,0xC3,0x01,
    0x83,0xC4,0x08};
constexpr std::array<std::uint8_t,11> kPenetratingDispatch{
    0x57,0x50,0x8D,0x7C,0x24,0x38,0xE8,0x54,0xF6,0xFF,0xFF};
constexpr std::array<std::uint8_t,34> kHitPosition{
    0xF3,0x0F,0x10,0x46,0x10,0x89,0x46,0x34,
    0xF3,0x0F,0x10,0x4D,0,0xF3,0x0F,0x10,0x17,
    0xF3,0x0F,0x5C,0xD1,0xF3,0x0F,0x59,0xD0,
    0xF3,0x0F,0x58,0xD1,0xF3,0x0F,0x11,0x56,0x38};
constexpr std::array<std::uint8_t,25> kDamageUse{
    0x8D,0x46,0x38,0x50,0x83,0xC5,0x34,0x55,0x53,0x53,
    0x8B,0x46,0x34,0x50,0xE8,0xCB,0xFC,0,0,
    0x8B,0x76,0x34,0x83,0xC4,0x30};
constexpr std::array<std::uint8_t,22> kPlainImpactDispatch{
    0x6A,0x01,0x8D,0x44,0x24,0x14,0x50,0x6A,0,0x53,0x55,0x57,
    0x8B,0xC6,0xE8,0xB3,0xFB,0xFF,0xFF,0x83,0xC4,0x18};
constexpr std::array<std::uint8_t,19> kPenetratingImpactDispatch{
    0x6A,0x01,0x8D,0x44,0x24,0x20,0x50,0x6A,0,0x53,0x55,0x57,
    0x8B,0xC6,0xE8,0x9E,0xFA,0xFF,0xFF};

bool readable(const void* address,std::size_t size) noexcept {
    return resident_page_access(address,size,false,&VirtualQuery);
}
bool matches(std::uintptr_t address,std::span<const std::uint8_t> bytes) noexcept {
    return address && readable(reinterpret_cast<const void*>(address),bytes.size()) &&
        std::memcmp(reinterpret_cast<const void*>(address),bytes.data(),bytes.size())==0;
}
bool requested(const wchar_t* name) noexcept {
    std::array<wchar_t,2> value{};
    return GetEnvironmentVariableW(name,value.data(),2)==1 && value[0]==L'1';
}
bool finite(Vec3f value) noexcept {
    return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);
}
enum class PatchResult { okay, foreign, quiescence, failed };
} // namespace

// Never revoked, even after restoration; an already-entered transparent
// observer can safely finish its original cleanup/test and native continuation.
extern "C" std::uintptr_t wawvr_server_plain_impact_resume=0x004E6144U;
extern "C" std::uintptr_t wawvr_server_penetrating_impact_resume=0x004E6259U;

extern "C" void __cdecl wawvr_observe_server_impact(
    const void* params_address,const void* results_address,
    const void* definition,const void* attacker,
    std::uint32_t hit_result,std::uint32_t penetrating) noexcept {
    const DWORD previous_error=GetLastError();
    if (!g_enabled.load(std::memory_order_acquire) || !g_shot.valid ||
        attacker!=g_shot.attacker || definition!=g_shot.definition ||
        GetTickCount64()-g_shot.milliseconds>250 || g_shot.impact_count>=12 ||
        !readable(params_address,sizeof(BulletFireParams)) ||
        !readable(results_address,sizeof(TraceResults))) {
        SetLastError(previous_error); return;
    }
    BulletFireParams params{}; TraceResults results{};
    std::memcpy(&params,params_address,sizeof(params));
    std::memcpy(&results,results_address,sizeof(results));
    if (!finite(params.start)||!finite(params.end)||!finite(params.direction)||
        !finite(results.hit_position)||!std::isfinite(results.fraction) ||
        params.ignore_entity!=0 || params.weapon_entity!=0) {
        SetLastError(previous_error); return;
    }
    const auto count=g_impact_count.fetch_add(1,std::memory_order_relaxed);
    const auto ordinal=g_shot.impact_count++;
    if (count<2048) {
        stereo_diagnostic_log(
            "GameplayServerImpact sequence=%u ordinal=%u name=%s weapon=%d generation=%llu penetrating=%u hit=%u start=%.5f,%.5f,%.5f end=%.5f,%.5f,%.5f direction=%.7f,%.7f,%.7f impact=%.5f,%.5f,%.5f fraction=%.7f startSolid=%u allSolid=%u evidence=authoritative-native-server-trace nativeInputsUnchanged=1 nativeResultsUnchanged=1",
            g_shot.sequence,ordinal,g_shot.identity.name.data(),g_shot.identity.weapon_index,
            static_cast<unsigned long long>(g_shot.identity.controller_generation),
            penetrating,hit_result&0xFFU,
            params.start.x,params.start.y,params.start.z,
            params.end.x,params.end.y,params.end.z,
            params.direction.x,params.direction.y,params.direction.z,
            results.hit_position.x,results.hit_position.y,results.hit_position.z,
            results.fraction,results.start_solid,results.all_solid);
    }
    SetLastError(previous_error);
}

#include "server_impact_audit_bridge.inl"

namespace {
PatchResult replace_seams(bool restoring) noexcept {
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(restoring); return PatchResult::failed;
#else
    const std::array<PeerThreadPatchRange,4> ranges{{
        {kSeams[0],5},{kSeams[1],5},
        {reinterpret_cast<std::uintptr_t>(&wawvr_server_plain_impact_bridge),256},
        {reinterpret_cast<std::uintptr_t>(&wawvr_server_penetrating_impact_bridge),256}}};
    SuspendedPeerThreads peers;
    if (!peers.suspend(ranges)) return PatchResult::quiescence;
    for (std::size_t i=0;i<2;++i)
        if (!matches(kSeams[i],restoring?g_replacements[i]:kOriginal))
            return PatchResult::foreign;
    auto* target=reinterpret_cast<void*>(kSeams[0]);
    const auto extent=kSeams[1]+5-kSeams[0]; DWORD protection{};
    if (!VirtualProtect(target,extent,PAGE_EXECUTE_READWRITE,&protection))
        return PatchResult::failed;
    for (std::size_t i=0;i<2;++i)
        std::memcpy(reinterpret_cast<void*>(kSeams[i]),
            (restoring?kOriginal:g_replacements[i]).data(),5);
    bool okay=FlushInstructionCache(GetCurrentProcess(),target,extent)!=0;
    for (std::size_t i=0;i<2;++i)
        okay=okay&&matches(kSeams[i],restoring?kOriginal:g_replacements[i]);
    if (!okay) {
        for (std::size_t i=0;i<2;++i)
            std::memcpy(reinterpret_cast<void*>(kSeams[i]),
                (restoring?g_replacements[i]:kOriginal).data(),5);
        FlushInstructionCache(GetCurrentProcess(),target,extent);
    }
    DWORD ignored{};
    if (!VirtualProtect(target,extent,protection,&ignored)) {
        // The previous failed protection change leaves the region writable.
        for (std::size_t i=0;i<2;++i)
            std::memcpy(reinterpret_cast<void*>(kSeams[i]),
                (restoring?g_replacements[i]:kOriginal).data(),5);
        FlushInstructionCache(GetCurrentProcess(),target,extent);
        VirtualProtect(target,extent,protection,&ignored);
        return PatchResult::failed;
    }
    return okay?PatchResult::okay:PatchResult::failed;
#endif
}
} // namespace

void begin_server_impact_audit_shot(
    const void* weapon_parms,const void* attacker) noexcept {
    if (!g_enabled.load(std::memory_order_acquire)) return;
    const DWORD previous_error=GetLastError();
    g_shot={};
    if (!g_identity_reader || !readable(weapon_parms,0x40)) {
        SetLastError(previous_error); return;
    }
    std::uint32_t definition{};
    std::memcpy(&definition,static_cast<const std::byte*>(weapon_parms)+0x3C,4);
    ServerImpactAuditIdentity identity{};
    const auto* native_definition=reinterpret_cast<const void*>(
        static_cast<std::uintptr_t>(definition));
    if (!definition || !g_identity_reader(attacker,native_definition,&identity) ||
        !identity.accepted || identity.weapon_index<=0 || identity.name[0]=='\0' ||
        identity.name.back()!='\0') {
        static std::atomic<std::uint32_t> rejected{0};
        const auto rejected_index=rejected.fetch_add(1,std::memory_order_relaxed);
        if (rejected_index<16) {
            stereo_diagnostic_log(
                "GameplayServerFireRejected sample=%u attacker=0x%08X actualDefinition=0x%08X exactDefinitionIdentity=unavailable noImpactPairClaimed=1",
                rejected_index,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(attacker)),
                definition);
        }
        SetLastError(previous_error); return;
    }
    const auto sequence=g_shot_sequence.fetch_add(1,std::memory_order_relaxed);
    if (sequence>=1024) { SetLastError(previous_error); return; }
    g_shot={true,sequence,0,GetTickCount64(),attacker,native_definition,identity};
    std::array<float,12> axes_origin{};
    std::memcpy(axes_origin.data(),weapon_parms,sizeof(axes_origin));
    log_sight_audit_discharge(sequence,identity.name.data(),{
        .weapon_parms=reinterpret_cast<std::uintptr_t>(weapon_parms),
        .definition=definition,
        .weapon_index=identity.weapon_index,
        .generation=identity.controller_generation,
        .now_ms=g_shot.milliseconds,
        .origin={axes_origin[9],axes_origin[10],axes_origin[11]},
        .basis={{axes_origin[0],axes_origin[1],axes_origin[2]},
            {-axes_origin[3],-axes_origin[4],-axes_origin[5]},
            {axes_origin[6],axes_origin[7],axes_origin[8]}},
    });
    stereo_diagnostic_log(
        "GameplayServerFire sequence=%u name=%s weapon=%d generation=%llu origin=%.5f,%.5f,%.5f forward=%.7f,%.7f,%.7f right=%.7f,%.7f,%.7f up=%.7f,%.7f,%.7f rightGrip=%.3f leftGrip=%.3f evidence=authoritative-native-discharge-parameters",
        sequence,identity.name.data(),identity.weapon_index,
        static_cast<unsigned long long>(identity.controller_generation),
        axes_origin[9],axes_origin[10],axes_origin[11],
        axes_origin[0],axes_origin[1],axes_origin[2],
        axes_origin[3],axes_origin[4],axes_origin[5],
        axes_origin[6],axes_origin[7],axes_origin[8],identity.right_grip,identity.left_grip);
    SetLastError(previous_error);
}

ServerImpactAuditHookStatus install_server_impact_audit(
    const wawvr::t4::ValidatedBindings& bindings,
    const ServerImpactIdentityReader identity_reader) noexcept {
    if (g_installed.load(std::memory_order_acquire))
        return matches(kSeams[0],g_replacements[0]) && matches(kSeams[1],g_replacements[1])
            ? ServerImpactAuditHookStatus::already_installed
            : ServerImpactAuditHookStatus::fingerprint_mismatch;
    if (!requested(L"WAWVR_GAMEPLAY_DIAGNOSTICS") ||
        !requested(L"WAWVR_SERVER_IMPACT_DIAGNOSTICS") || requested(L"WAWVR_DISABLE_XR"))
        return ServerImpactAuditHookStatus::disabled;
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings); static_cast<void>(identity_reader);
    return ServerImpactAuditHookStatus::unsupported;
#else
    if (!identity_reader || select_t4_layout_family(bindings.profile())!=
        T4LayoutFamily::single_player_1_7_1263 ||
        reinterpret_cast<std::uintptr_t>(bindings.module().base)!=0x00400000U)
        return ServerImpactAuditHookStatus::unsupported;
    if (!matches(0x004E5B80,kTraceEntry)||!matches(0x004E6129,kPlainContext)||
        !matches(0x004E624F,kPenetratingContext)||!matches(0x004E6209,kPenetratingArgs)||
        !matches(0x004E6B9E,kPlainDispatch)||!matches(0x004E6B91,kPenetratingDispatch)||
        !matches(0x004E5C48,kHitPosition)||!matches(0x004E6092,kDamageUse)||
        !matches(0x004E614A,kPlainImpactDispatch)||
        !matches(0x004E625F,kPenetratingImpactDispatch))
        return ServerImpactAuditHookStatus::fingerprint_mismatch;
    const std::array<std::uintptr_t,2> bridges{
        reinterpret_cast<std::uintptr_t>(&wawvr_server_plain_impact_bridge),
        reinterpret_cast<std::uintptr_t>(&wawvr_server_penetrating_impact_bridge)};
    for (std::size_t i=0;i<2;++i) {
        if (!matches(kSeams[i],kOriginal))
            return ServerImpactAuditHookStatus::fingerprint_mismatch;
        const auto delta=static_cast<std::int64_t>(bridges[i])-
            static_cast<std::int64_t>(kSeams[i]+5);
        if (delta<(std::numeric_limits<std::int32_t>::min)() ||
            delta>(std::numeric_limits<std::int32_t>::max)())
            return ServerImpactAuditHookStatus::patch_failed;
        const auto displacement=static_cast<std::int32_t>(delta);
        g_replacements[i]={0xE9};
        std::memcpy(g_replacements[i].data()+1,&displacement,4);
    }
    g_identity_reader=identity_reader;
    const auto result=replace_seams(false);
    if (result==PatchResult::quiescence)
        return ServerImpactAuditHookStatus::thread_suspend_failed;
    if (result!=PatchResult::okay)
        return ServerImpactAuditHookStatus::patch_failed;
    g_installed.store(true,std::memory_order_release);
    g_enabled.store(true,std::memory_order_release);
    return ServerImpactAuditHookStatus::installed;
#endif
}
// COLT_ZERO_DEFERRED_OBSERVER_STATUS_BEGIN
void record_server_impact_audit_install_status(
    const ServerImpactAuditHookStatus status) noexcept {
    const DWORD saved = GetLastError();
    const auto post_request = [](const wchar_t* const name) noexcept {
        std::array<wchar_t, 2> value{};
        const DWORD length = GetEnvironmentVariableW(name, value.data(), 2);
        return ServerImpactInstallRequestObservation{
            length, length == 1 && value[0] == L'1'};
    };
    static_cast<void>(g_install_diagnostic.record({
        .status_known = true,
        .status = static_cast<std::uint32_t>(status),
        .installed_at_record = g_installed.load(std::memory_order_acquire),
        .enabled_at_record = g_enabled.load(std::memory_order_acquire),
        .post_gameplay = post_request(L"WAWVR_GAMEPLAY_DIAGNOSTICS"),
        .post_server_impact = post_request(L"WAWVR_SERVER_IMPACT_DIAGNOSTICS"),
        .post_disable_xr = post_request(L"WAWVR_DISABLE_XR")
    }));
    SetLastError(saved);
}

void log_server_impact_audit_install_status_if_ready() noexcept {
    const DWORD saved = GetLastError();
    ServerImpactInstallDiagnosticSnapshot snapshot{};
    if (g_install_diagnostic.claim(stereo_diagnostic_log_ready(), &snapshot)) {
        stereo_diagnostic_log(
            "GameplayServerImpactInstallDiag statusKnown=%u status=%u statusName=%s installedAtRecord=%u enabledAtRecord=%u postInstallGameplayExactOne=%u postInstallGameplayLengthResult=%u postInstallServerImpactExactOne=%u postInstallServerImpactLengthResult=%u postInstallDisableXrExactOne=%u postInstallDisableXrLengthResult=%u requestObservationAfterInstall=1 originalGateEvaluationNotCaptured=1 observationOnly=1 noRetry=1",
            snapshot.status_known ? 1U : 0U, snapshot.status,
            server_impact_install_status_name(snapshot.status),
            snapshot.installed_at_record ? 1U : 0U,
            snapshot.enabled_at_record ? 1U : 0U,
            snapshot.post_gameplay.exact_one ? 1U : 0U,
            snapshot.post_gameplay.length_result,
            snapshot.post_server_impact.exact_one ? 1U : 0U,
            snapshot.post_server_impact.length_result,
            snapshot.post_disable_xr.exact_one ? 1U : 0U,
            snapshot.post_disable_xr.length_result);
    }
    SetLastError(saved);
}
// COLT_ZERO_DEFERRED_OBSERVER_STATUS_END
void request_server_impact_audit_shutdown() noexcept {
    g_enabled.store(false,std::memory_order_release);
}
bool restore_server_impact_audit() noexcept {
    request_server_impact_audit_shutdown();
    if (!g_installed.load(std::memory_order_acquire)) return true;
    if (replace_seams(true)!=PatchResult::okay) return false;
    g_installed.store(false,std::memory_order_release);
    return true;
}
} // namespace wawvr::mod
