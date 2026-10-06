#include "physical_scope_hud_hook.hpp"

#include "peer_thread_quiescence.hpp"
#include "stereo_diagnostics.hpp"
#include "stereo_scene_hook.hpp"
#include "t4_presentation_state.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace wawvr::mod {
namespace {

constexpr wawvr::t4::Rva kOriginalRva = 0x0003D000;
// Exact decrypted retail SP CG_GetWeapReticleZoom. EDX is cg_s*, ESI is the
// writable float output, and AL is the boolean result. There are no stack args.
constexpr std::array<std::uint8_t, 48> kOriginalEntry{
    0xF6,0x82,0xA8,0xAC,0x0A,0x00,0x02,0x74,0x08,
    0x8B,0x82,0x94,0xAD,0x0A,0x00,0xEB,0x06,
    0x8B,0x82,0x9C,0xAD,0x0A,0x00,
    0x8B,0x0C,0x85,0x70,0x67,0x8F,0x00,
    0x83,0xB9,0x1C,0x05,0x00,0x00,0x00,0x0F,0x57,0xC9,
    0xF3,0x0F,0x10,0x82,0xA8,0xAD,0x0A,0x00};
constexpr std::array<std::uint8_t, 35> kReticleContext{
    0x83,0xEC,0x1C,0x56,0x8D,0x74,0x24,0x04,
    0xBA,0xB8,0x32,0x47,0x03,0xE8,0xBE,0xF8,0xFF,0xFF,
    0x84,0xC0,0x75,0x0D,0xF3,0x0F,0x10,0x05,0x4C,0x6A,0x82,0x00,
    0x5E,0x83,0xC4,0x1C,0xC3};
constexpr std::array<std::uint8_t, 18> kScopeVisibilityContext{
    0x56,0x8D,0x74,0x24,0x04,0xBA,0xB8,0x32,0x47,0x03,
    0xE8,0x42,0x11,0xFD,0xFF,0x5E,0x59,0xC3};
constexpr std::array<std::uint8_t, 32> kUiExpressionContext{
    0x56,0x8D,0x74,0x24,0x04,0xBA,0xB8,0x32,0x47,0x03,
    0xE8,0x6C,0xBA,0xE8,0xFF,0x5E,
    0x8B,0x0D,0xF0,0xE8,0x08,0x02,0x83,0x79,0x10,0x00,
    0x0F,0xB6,0xC0,0x89,0x47,0x04};

struct HookSite final {
    wawvr::t4::Rva call_rva{};
    wawvr::t4::Rva context_rva{};
    std::array<std::uint8_t, 5> original_call{};
    std::span<const std::uint8_t> context{};
};
constexpr std::array<HookSite, 3> kSites{{
    {0x0003D73D, 0x0003D730, {0xE8,0xBE,0xF8,0xFF,0xFF}, kReticleContext},
    {0x0006BEB9, 0x0006BEAF, {0xE8,0x42,0x11,0xFD,0xFF}, kScopeVisibilityContext},
    {0x001B158F, 0x001B1585, {0xE8,0x6C,0xBA,0xE8,0xFF}, kUiExpressionContext},
}};
constexpr unsigned kAllSites = (1U << kSites.size()) - 1U;
// The x86 bridge is 0x39 bytes: the two paths preserve FXSAVE state, GPRs,
// EFLAGS and ESP; the suppressed path changes only AL and the zoom output.
constexpr std::size_t kBridgeGuardSize = 0x39;

std::atomic<unsigned> g_owned_sites{0};
std::atomic<bool> g_enabled{false};
std::array<std::uintptr_t, kSites.size()> g_callsites{};
std::array<std::array<std::uint8_t, 5>, kSites.size()> g_replacements{};

[[nodiscard]] bool disabled_by_environment() noexcept {
    std::array<wchar_t, 16> value{};
    const auto length = GetEnvironmentVariableW(
        L"WAWVR_DISABLE_XR", value.data(), static_cast<DWORD>(value.size()));
    return length != 0 && length < value.size() &&
        (value[0] == L'1' || value[0] == L'y' || value[0] == L'Y' ||
         value[0] == L't' || value[0] == L'T');
}

[[nodiscard]] bool matches(const std::uintptr_t address,
                           const std::span<const std::uint8_t> bytes) noexcept {
    if (address == 0 || bytes.empty()) return false;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &memory,
                     sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    const DWORD access = memory.Protect & 0xffU;
    if (access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE &&
        access != PAGE_EXECUTE_WRITECOPY && access != PAGE_READONLY &&
        access != PAGE_READWRITE && access != PAGE_WRITECOPY) return false;
    const auto begin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    if (address < begin || address - begin > memory.RegionSize ||
        bytes.size() > memory.RegionSize - (address - begin)) return false;
    return std::memcmp(reinterpret_cast<const void*>(address), bytes.data(),
                       bytes.size()) == 0;
}

enum class PatchResult { ok, foreign, quiescence, failed };

[[nodiscard]] PatchResult patch_call(
    const std::uintptr_t address,
    const std::array<std::uint8_t, 5>& expected,
    const std::array<std::uint8_t, 5>& desired,
    const std::uintptr_t bridge) noexcept {
    const std::array<PeerThreadPatchRange, 2> ranges{{
        {address, expected.size()}, {bridge, bridge ? kBridgeGuardSize : 0}}};
    SuspendedPeerThreads peers;
    if (!peers.suspend(ranges)) return PatchResult::quiescence;
    if (!matches(address, expected)) return PatchResult::foreign;
    auto* target = reinterpret_cast<void*>(address);
    DWORD protection{};
    if (!VirtualProtect(target, desired.size(), PAGE_EXECUTE_READWRITE,
                        &protection)) return PatchResult::failed;
    if (std::memcmp(target, expected.data(), expected.size()) != 0) {
        DWORD ignored{};
        VirtualProtect(target, desired.size(), protection, &ignored);
        return PatchResult::foreign;
    }
    std::memcpy(target, desired.data(), desired.size());
    const bool written = std::memcmp(target, desired.data(), desired.size()) == 0;
    const bool flushed = written && FlushInstructionCache(
        GetCurrentProcess(), target, desired.size());
    if (!written || !flushed) {
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
        DWORD ignored{};
        VirtualProtect(target, expected.size(), protection, &ignored);
        return PatchResult::failed;
    }
    DWORD ignored{};
    if (!VirtualProtect(target, desired.size(), protection, &ignored)) {
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
        VirtualProtect(target, expected.size(), protection, &ignored);
        return PatchResult::failed;
    }
    return PatchResult::ok;
}

} // namespace

// Retain the original target for process lifetime. A peer nested in the
// predicate during shutdown must still be able to take the native tail path.
extern "C" std::uintptr_t wawvr_original_physical_scope_hud_zoom_address = 0;

extern "C" bool __cdecl wawvr_suppress_native_physical_scope_hud() noexcept {
    if (!g_enabled.load(std::memory_order_acquire)) return false;
    const auto presentation = read_t4_presentation_state();
    if (!presentation.valid ||
        presentation.connection_state != presentation.active_connection_state ||
        presentation.key_catchers != 0) return false;
    // Share the decision prepared at this frame's CG_Draw2D boundary. A
    // second wall-clock freshness check could expire during a frame hitch,
    // separating native mask/HUD policy from the scene's physical optic.
    // This query also revalidates current-frame stereo readiness.
    if (!stereo_scene_physical_scope_hud_active() ||
        !g_enabled.load(std::memory_order_acquire)) return false;
    WAWVR_STEREO_DIAG_ONCE(
        "Physical scope native mask/HUD visibility suppressed: current-frame optic decision; three exact zoom callers guarded; other native zoom callers unchanged");
    return true;
}

#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) void wawvr_physical_scope_hud_bridge() noexcept {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        call wawvr_suppress_native_physical_scope_hud
        test al, al
        jz native_zoom
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        mov dword ptr [esi], 0
        mov al, 0
        ret
    native_zoom:
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [wawvr_original_physical_scope_hud_zoom_address]
    }
}
#endif

PhysicalScopeHudHookResult install_physical_scope_hud_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    if (g_owned_sites.load(std::memory_order_acquire) != 0) {
        if (!physical_scope_hud_hook_installed())
            return PhysicalScopeHudHookResult::target_mismatch;
        for (std::size_t index = 0; index < kSites.size(); ++index) {
            if (!matches(g_callsites[index], g_replacements[index]))
                return PhysicalScopeHudHookResult::target_mismatch;
        }
        return PhysicalScopeHudHookResult::already_installed;
    }
    if (disabled_by_environment())
        return PhysicalScopeHudHookResult::disabled_by_environment;
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    return PhysicalScopeHudHookResult::unsupported_architecture;
#else
    const auto& profile = bindings.profile();
    const auto& plain = wawvr::t4::t4_sp_1_7_1263_profile();
    const auto& steam = wawvr::t4::t4_steam_sp_1_7_1263_profile();
    if (!((profile.id == plain.id && profile.sha256 == plain.sha256) ||
          (profile.id == steam.id && profile.sha256 == steam.sha256)) ||
        reinterpret_cast<std::uintptr_t>(bindings.module().base) != 0x00400000U)
        return PhysicalScopeHudHookResult::unsupported_profile;
    const auto original = bindings.module().address(kOriginalRva, kOriginalEntry.size());
    if (!original || !matches(*original, kOriginalEntry))
        return PhysicalScopeHudHookResult::target_mismatch;
    const auto bridge = reinterpret_cast<std::uintptr_t>(&wawvr_physical_scope_hud_bridge);
    for (std::size_t index = 0; index < kSites.size(); ++index) {
        const auto& site = kSites[index];
        const auto call = bindings.module().address(site.call_rva, site.original_call.size());
        const auto context = bindings.module().address(site.context_rva, site.context.size());
        if (!call || !context || !matches(*call, site.original_call) ||
            !matches(*context, site.context))
            return PhysicalScopeHudHookResult::target_mismatch;
        std::int32_t native_delta{};
        std::memcpy(&native_delta, site.original_call.data() + 1, sizeof(native_delta));
        if (static_cast<std::int64_t>(*call) + 5 + native_delta !=
            static_cast<std::int64_t>(*original))
            return PhysicalScopeHudHookResult::target_mismatch;
        const auto delta = static_cast<std::int64_t>(bridge) -
                           static_cast<std::int64_t>(*call + 5);
        if (delta < (std::numeric_limits<std::int32_t>::min)() ||
            delta > (std::numeric_limits<std::int32_t>::max)())
            return PhysicalScopeHudHookResult::jump_out_of_range;
        g_callsites[index] = *call;
        g_replacements[index] = site.original_call;
        const auto encoded = static_cast<std::int32_t>(delta);
        std::memcpy(g_replacements[index].data() + 1, &encoded, sizeof(encoded));
    }
    wawvr_original_physical_scope_hud_zoom_address = *original;
    // Until all sites are owned, the bridge always tail-calls the native zoom.
    // Record partial ownership so failed rollback can be retried at shutdown.
    for (std::size_t index = 0; index < kSites.size(); ++index) {
        const auto result = patch_call(g_callsites[index], kSites[index].original_call,
                                       g_replacements[index], 0);
        if (result != PatchResult::ok) {
            static_cast<void>(restore_physical_scope_hud_hook());
            if (result == PatchResult::quiescence)
                return PhysicalScopeHudHookResult::thread_suspend_failed;
            if (result == PatchResult::foreign)
                return PhysicalScopeHudHookResult::target_mismatch;
            return PhysicalScopeHudHookResult::patch_write_failed;
        }
        g_owned_sites.fetch_or(1U << index, std::memory_order_release);
    }
    g_enabled.store(true, std::memory_order_release);
    return PhysicalScopeHudHookResult::installed;
#endif
}

void request_physical_scope_hud_hook_shutdown() noexcept {
    g_enabled.store(false, std::memory_order_release);
}

PhysicalScopeHudHookResult restore_physical_scope_hud_hook() noexcept {
    request_physical_scope_hud_hook_shutdown();
    if (g_owned_sites.load(std::memory_order_acquire) == 0)
        return PhysicalScopeHudHookResult::not_installed;
#if !defined(_MSC_VER) || !defined(_M_IX86)
    return PhysicalScopeHudHookResult::unsupported_architecture;
#else
    bool foreign_preserved = false;
    auto failure = PhysicalScopeHudHookResult::restored;
    for (std::size_t index = 0; index < kSites.size(); ++index) {
        const unsigned bit = 1U << index;
        if ((g_owned_sites.load(std::memory_order_acquire) & bit) == 0) continue;
        const auto result = patch_call(g_callsites[index], g_replacements[index],
            kSites[index].original_call,
            reinterpret_cast<std::uintptr_t>(&wawvr_physical_scope_hud_bridge));
        if (result == PatchResult::quiescence) {
            failure = PhysicalScopeHudHookResult::thread_suspend_failed;
        } else if (result == PatchResult::failed) {
            failure = PhysicalScopeHudHookResult::patch_write_failed;
        } else {
            foreign_preserved = foreign_preserved || result == PatchResult::foreign;
            g_owned_sites.fetch_and(~bit, std::memory_order_release);
        }
    }
    if (failure != PhysicalScopeHudHookResult::restored) return failure;
    return foreign_preserved ? PhysicalScopeHudHookResult::foreign_patch_preserved
                             : PhysicalScopeHudHookResult::restored;
#endif
}

bool physical_scope_hud_hook_installed() noexcept {
    return g_enabled.load(std::memory_order_acquire) &&
        g_owned_sites.load(std::memory_order_acquire) == kAllSites;
}

const char* describe_physical_scope_hud_hook_result(
    const PhysicalScopeHudHookResult result) noexcept {
    switch (result) {
    case PhysicalScopeHudHookResult::installed: return "installed";
    case PhysicalScopeHudHookResult::already_installed: return "already-installed";
    case PhysicalScopeHudHookResult::restored: return "restored";
    case PhysicalScopeHudHookResult::not_installed: return "not-installed";
    case PhysicalScopeHudHookResult::unsupported_profile: return "unsupported-profile";
    case PhysicalScopeHudHookResult::unsupported_architecture: return "unsupported-architecture";
    case PhysicalScopeHudHookResult::disabled_by_environment: return "disabled-by-environment";
    case PhysicalScopeHudHookResult::target_mismatch: return "target-mismatch";
    case PhysicalScopeHudHookResult::jump_out_of_range: return "jump-out-of-range";
    case PhysicalScopeHudHookResult::thread_suspend_failed: return "thread-suspend-failed";
    case PhysicalScopeHudHookResult::patch_write_failed: return "patch-write-failed";
    case PhysicalScopeHudHookResult::foreign_patch_preserved: return "foreign-patch-preserved";
    }
    return "unknown";
}

} // namespace wawvr::mod
