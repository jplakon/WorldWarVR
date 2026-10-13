#include "tank_reticle_hook.hpp"

#include "input_hook.hpp"
#include "peer_thread_quiescence.hpp"
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

constexpr wawvr::t4::Rva kCallRva = 0x0003E1D9;
constexpr wawvr::t4::Rva kOriginalRva = 0x0003D9C0;
constexpr std::array<std::uint8_t, 5> kOriginalCall{
    0xE8, 0xE2, 0xF7, 0xFF, 0xFF};
// Verified in the decrypted Steam SP text, retail disk SHA256 732900D1...:
// test eax,4000h; je ordinary firearm; inspect weapon flags; mov edi,ebx;
// call 43D9C0. This is not CG_DrawTurretCrossHair (stationary guns).
constexpr std::array<std::uint8_t, 33> kCallContext{
    0xA9,0x00,0x40,0x00,0x00,0x74,0x1F,
    0x8B,0x15,0x38,0xE8,0x51,0x03,0xC1,0xEA,0x09,
    0xF6,0xC2,0x01,0x0F,0x85,0xD5,0x01,0x00,0x00,
    0x8B,0xFB,0xE8,0xE2,0xF7,0xFF,0xFF,0x5F};
constexpr std::array<std::uint8_t, 14> kOriginalEntry{
    0x8B,0x0D,0x8C,0xE7,0x51,0x03,0x8B,0xC7,
    0xC1,0xE0,0x0A,0x83,0xEC,0x18};
// MSVC x86 disassembly: bridge spans offsets 0x00..0x30 inclusive; both
// return paths restore the exact FXSAVE image, GPRs, EFLAGS and caller ESP.
constexpr std::size_t kBridgeGuardSize = 0x31;

std::atomic<bool> g_installed{false};
std::atomic<bool> g_enabled{false};
std::uintptr_t g_callsite{};
std::array<std::uint8_t, 5> g_replacement{};

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

// Retain the original tail target for process lifetime: restoration may occur
// while a peer is nested in the predicate. That peer must still return safely.
extern "C" std::uintptr_t wawvr_original_tank_reticle_address = 0;

extern "C" bool __cdecl wawvr_suppress_native_tank_reticle() noexcept {
    if (!g_enabled.load(std::memory_order_acquire)) return false;
    const auto presentation = read_t4_presentation_state();
    return presentation.valid &&
        presentation.connection_state == presentation.active_connection_state &&
        presentation.key_catchers == 0 && controller_tank_controls_active() &&
        stereo_scene_ready_for_current_com_frame() &&
        g_enabled.load(std::memory_order_acquire);
}

#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) void wawvr_tank_reticle_bridge() noexcept {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        call wawvr_suppress_native_tank_reticle
        test al, al
        jz native_reticle
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        ret
    native_reticle:
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [wawvr_original_tank_reticle_address]
    }
}
#endif

TankReticleHookResult install_tank_reticle_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    if (g_installed.load(std::memory_order_acquire)) {
        return matches(g_callsite, g_replacement)
            ? TankReticleHookResult::already_installed
            : TankReticleHookResult::target_mismatch;
    }
    if (disabled_by_environment())
        return TankReticleHookResult::disabled_by_environment;
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    return TankReticleHookResult::unsupported_architecture;
#else
    const auto& profile = bindings.profile();
    const auto& plain = wawvr::t4::t4_sp_1_7_1263_profile();
    const auto& steam = wawvr::t4::t4_steam_sp_1_7_1263_profile();
    if (!((profile.id == plain.id && profile.sha256 == plain.sha256) ||
          (profile.id == steam.id && profile.sha256 == steam.sha256)) ||
        reinterpret_cast<std::uintptr_t>(bindings.module().base) != 0x00400000U)
        return TankReticleHookResult::unsupported_profile;
    const auto call = bindings.module().address(kCallRva, kOriginalCall.size());
    const auto context = bindings.module().address(0x0003E1BE, kCallContext.size());
    const auto original = bindings.module().address(kOriginalRva, kOriginalEntry.size());
    if (!call || !context || !original || !matches(*call, kOriginalCall) ||
        !matches(*context, kCallContext) || !matches(*original, kOriginalEntry))
        return TankReticleHookResult::target_mismatch;
    std::int32_t native_delta{};
    std::memcpy(&native_delta, kOriginalCall.data() + 1, sizeof(native_delta));
    if (static_cast<std::int64_t>(*call) + 5 + native_delta !=
        static_cast<std::int64_t>(*original))
        return TankReticleHookResult::target_mismatch;
    const auto bridge = reinterpret_cast<std::uintptr_t>(&wawvr_tank_reticle_bridge);
    const auto delta = static_cast<std::int64_t>(bridge) -
                       static_cast<std::int64_t>(*call + 5);
    if (delta < (std::numeric_limits<std::int32_t>::min)() ||
        delta > (std::numeric_limits<std::int32_t>::max)())
        return TankReticleHookResult::jump_out_of_range;
    auto replacement = kOriginalCall;
    const auto encoded = static_cast<std::int32_t>(delta);
    std::memcpy(replacement.data() + 1, &encoded, sizeof(encoded));
    wawvr_original_tank_reticle_address = *original;
    const auto result = patch_call(*call, kOriginalCall, replacement, 0);
    if (result == PatchResult::quiescence)
        return TankReticleHookResult::thread_suspend_failed;
    if (result == PatchResult::foreign)
        return TankReticleHookResult::target_mismatch;
    if (result != PatchResult::ok) return TankReticleHookResult::patch_write_failed;
    g_callsite = *call;
    g_replacement = replacement;
    g_installed.store(true, std::memory_order_release);
    g_enabled.store(true, std::memory_order_release);
    return TankReticleHookResult::installed;
#endif
}

void request_tank_reticle_hook_shutdown() noexcept {
    g_enabled.store(false, std::memory_order_release);
}

TankReticleHookResult restore_tank_reticle_hook() noexcept {
    request_tank_reticle_hook_shutdown();
    if (!g_installed.load(std::memory_order_acquire))
        return TankReticleHookResult::not_installed;
#if !defined(_MSC_VER) || !defined(_M_IX86)
    return TankReticleHookResult::unsupported_architecture;
#else
    const auto result = patch_call(g_callsite, g_replacement, kOriginalCall,
        reinterpret_cast<std::uintptr_t>(&wawvr_tank_reticle_bridge));
    if (result == PatchResult::quiescence)
        return TankReticleHookResult::thread_suspend_failed;
    if (result == PatchResult::failed) return TankReticleHookResult::patch_write_failed;
    g_installed.store(false, std::memory_order_release);
    return result == PatchResult::foreign
        ? TankReticleHookResult::foreign_patch_preserved
        : TankReticleHookResult::restored;
#endif
}

bool tank_reticle_hook_installed() noexcept {
    return g_installed.load(std::memory_order_acquire);
}

const char* describe_tank_reticle_hook_result(const TankReticleHookResult result) noexcept {
    switch (result) {
    case TankReticleHookResult::installed: return "installed";
    case TankReticleHookResult::already_installed: return "already-installed";
    case TankReticleHookResult::restored: return "restored";
    case TankReticleHookResult::not_installed: return "not-installed";
    case TankReticleHookResult::unsupported_profile: return "unsupported-profile";
    case TankReticleHookResult::unsupported_architecture: return "unsupported-architecture";
    case TankReticleHookResult::disabled_by_environment: return "disabled-by-environment";
    case TankReticleHookResult::target_mismatch: return "target-mismatch";
    case TankReticleHookResult::jump_out_of_range: return "jump-out-of-range";
    case TankReticleHookResult::thread_suspend_failed: return "thread-suspend-failed";
    case TankReticleHookResult::patch_write_failed: return "patch-write-failed";
    case TankReticleHookResult::foreign_patch_preserved: return "foreign-patch-preserved";
    }
    return "unknown";
}

} // namespace wawvr::mod
