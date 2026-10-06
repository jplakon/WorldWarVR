#include "lod_fov_clamp.hpp"

#include "lod_fov_clamp_logic.hpp"
#include "peer_thread_quiescence.hpp"

#include "t4/hook_api.hpp"
#include "t4/profile.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace wawvr::mod {
namespace {

constexpr std::size_t kPatchSize = 5;
constexpr std::size_t kBridgeExecutionGuardSize = 128;
constexpr std::array<std::uint8_t, kPatchSize> kOriginalMovss{
    0xF3, 0x0F, 0x10, 0x48, 0x14,
};

std::atomic<bool> g_installed{false};
std::uintptr_t g_patch_site = 0;
std::array<std::uint8_t, kPatchSize> g_original_bytes{};
std::array<std::uint8_t, kPatchSize> g_replacement_bytes{};
thread_local bool g_stereo_scene_generation_active = false;

[[nodiscard]] bool disabled_by_environment() noexcept {
    std::array<wchar_t, 16> value{};
    const DWORD length = GetEnvironmentVariableW(
        L"WAWVR_DISABLE_XR", value.data(), static_cast<DWORD>(value.size()));
    return length != 0 && length < value.size() &&
           (value[0] == L'1' || value[0] == L'y' || value[0] == L'Y' ||
            value[0] == L't' || value[0] == L'T');
}

[[nodiscard]] bool protection_is_readable(const DWORD protection) noexcept {
    const DWORD access = protection & 0xFFU;
    return access == PAGE_READONLY || access == PAGE_READWRITE ||
           access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool readable_range(
    const void* const address, const std::size_t size) noexcept {
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
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto region_begin =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto region_end = region_begin + memory.RegionSize;
    return begin >= region_begin && begin <= region_end &&
           size <= region_end - begin;
}

[[nodiscard]] bool bytes_match(
    const void* const address,
    const std::span<const std::uint8_t> expected) noexcept {
    return readable_range(address, expected.size()) &&
           std::memcmp(address, expected.data(), expected.size()) == 0;
}

[[nodiscard]] bool make_relative_jump(
    const std::uintptr_t source, const std::uintptr_t destination,
    std::array<std::uint8_t, kPatchSize>* const output) noexcept {
    if (output == nullptr) {
        return false;
    }
    const std::int64_t displacement =
        static_cast<std::int64_t>(destination) -
        static_cast<std::int64_t>(source + kPatchSize);
    if (displacement < std::numeric_limits<std::int32_t>::min() ||
        displacement > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    (*output)[0] = 0xE9;
    const auto encoded = static_cast<std::int32_t>(displacement);
    std::memcpy(output->data() + 1, &encoded, sizeof(encoded));
    return true;
}

enum class ExactPatchResult : std::uint8_t {
    ok,
    thread_suspend_failed,
    expected_bytes_changed,
    target_protection_failed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
};

[[nodiscard]] ExactPatchResult replace_exact_bytes(
    const std::uintptr_t target_address,
    const std::array<std::uint8_t, kPatchSize>& expected,
    const std::array<std::uint8_t, kPatchSize>& replacement,
    const std::uintptr_t bridge_address,
    DWORD* const system_error) noexcept {
    const std::array<PeerThreadPatchRange, 2> patch_ranges{{
        {target_address, kPatchSize},
        {bridge_address, kBridgeExecutionGuardSize},
    }};
    SuspendedPeerThreads suspended;
    PeerThreadQuiesceResult quiesce{};
    if (!suspended.suspend(patch_ranges, &quiesce)) {
        if (system_error != nullptr) {
            *system_error = quiesce.system_error;
        }
        return ExactPatchResult::thread_suspend_failed;
    }

    auto* const target = reinterpret_cast<std::uint8_t*>(target_address);
    if (!bytes_match(target, expected)) {
        return ExactPatchResult::expected_bytes_changed;
    }

    DWORD old_protection = 0;
    if (!VirtualProtect(
            target, replacement.size(), PAGE_EXECUTE_READWRITE,
            &old_protection)) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        return ExactPatchResult::target_protection_failed;
    }

    if (std::memcmp(target, expected.data(), expected.size()) != 0) {
        DWORD ignored = 0;
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return ExactPatchResult::expected_bytes_changed;
    }
    std::memcpy(target, replacement.data(), replacement.size());
    if (std::memcmp(target, replacement.data(), replacement.size()) != 0) {
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
        DWORD ignored = 0;
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return ExactPatchResult::patch_write_failed;
    }
    if (!FlushInstructionCache(
            GetCurrentProcess(), target, replacement.size())) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
        DWORD ignored = 0;
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return ExactPatchResult::patch_cache_flush_failed;
    }

    DWORD ignored = 0;
    if (!VirtualProtect(
            target, replacement.size(), old_protection, &ignored)) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return ExactPatchResult::protection_restore_failed;
    }
    return ExactPatchResult::ok;
}

[[nodiscard]] LodFovClampPatchStatus install_status_from_patch(
    const ExactPatchResult result) noexcept {
    switch (result) {
    case ExactPatchResult::ok:
        return LodFovClampPatchStatus::installed;
    case ExactPatchResult::thread_suspend_failed:
        return LodFovClampPatchStatus::thread_suspend_failed;
    case ExactPatchResult::expected_bytes_changed:
        return LodFovClampPatchStatus::expected_bytes_changed;
    case ExactPatchResult::target_protection_failed:
        return LodFovClampPatchStatus::target_protection_failed;
    case ExactPatchResult::patch_write_failed:
        return LodFovClampPatchStatus::patch_write_failed;
    case ExactPatchResult::patch_cache_flush_failed:
        return LodFovClampPatchStatus::patch_cache_flush_failed;
    case ExactPatchResult::protection_restore_failed:
        return LodFovClampPatchStatus::protection_restore_failed;
    }
    return LodFovClampPatchStatus::patch_write_failed;
}

[[nodiscard]] LodFovClampRestoreStatus restore_status_from_patch(
    const ExactPatchResult result) noexcept {
    switch (result) {
    case ExactPatchResult::ok:
        return LodFovClampRestoreStatus::restored;
    case ExactPatchResult::thread_suspend_failed:
        return LodFovClampRestoreStatus::thread_suspend_failed;
    case ExactPatchResult::expected_bytes_changed:
        return LodFovClampRestoreStatus::foreign_patch_preserved;
    case ExactPatchResult::target_protection_failed:
        return LodFovClampRestoreStatus::target_protection_failed;
    case ExactPatchResult::patch_write_failed:
        return LodFovClampRestoreStatus::patch_write_failed;
    case ExactPatchResult::patch_cache_flush_failed:
        return LodFovClampRestoreStatus::patch_cache_flush_failed;
    case ExactPatchResult::protection_restore_failed:
        return LodFovClampRestoreStatus::protection_restore_failed;
    }
    return LodFovClampRestoreStatus::patch_write_failed;
}

} // namespace

// Published before the E9 is installed and intentionally retained for the
// process lifetime. An already-entered bridge can therefore always finish its
// tail jump, even if teardown restores the original movss concurrently.
extern "C" std::uintptr_t wawvr_t4_lod_clamp_continuation_address = 0;

extern "C" std::uint32_t __cdecl wawvr_select_lod_tan_half_fov_y_bits(
    const std::uint32_t source_bits) noexcept {
    return select_lod_tan_half_fov_y_bits(
        source_bits, g_stereo_scene_generation_active);
}

#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) void wawvr_t4_lod_clamp_bridge() noexcept {
    __asm {
        // Preserve the displaced instruction exactly: EAX remains the refdef
        // pointer and only XMM1 receives [EAX+0x14].
        movss xmm1, dword ptr [eax+14h]

        // The displaced movss has no EFLAGS or GPR effects. Preserve those
        // states, plus every non-target XMM register, across the integer-only
        // selector call.
        pushfd
        pushad
        sub esp, 112
        movups [esp], xmm0
        movups [esp+16], xmm2
        movups [esp+32], xmm3
        movups [esp+48], xmm4
        movups [esp+64], xmm5
        movups [esp+80], xmm6
        movups [esp+96], xmm7

        sub esp, 4
        movss dword ptr [esp], xmm1
        call wawvr_select_lod_tan_half_fov_y_bits
        add esp, 4
        push eax
        movss xmm1, dword ptr [esp]
        add esp, 4

        movups xmm0, [esp]
        movups xmm2, [esp+16]
        movups xmm3, [esp+32]
        movups xmm4, [esp+48]
        movups xmm5, [esp+64]
        movups xmm6, [esp+80]
        movups xmm7, [esp+96]
        add esp, 112
        popad
        popfd
        jmp dword ptr [wawvr_t4_lod_clamp_continuation_address]
    }
}
#endif

ScopedStereoLodFovClamp::ScopedStereoLodFovClamp() noexcept
    : previous_(g_stereo_scene_generation_active) {
    g_stereo_scene_generation_active = true;
}

ScopedStereoLodFovClamp::~ScopedStereoLodFovClamp() {
    g_stereo_scene_generation_active = previous_;
}

LodFovClampPatchResult install_lod_fov_clamp_patch(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    LodFovClampPatchResult result{};
    if (g_installed.load(std::memory_order_acquire)) {
        result.status = LodFovClampPatchStatus::already_installed;
        result.target = g_patch_site;
        result.continuation = wawvr_t4_lod_clamp_continuation_address;
        return result;
    }
    if (disabled_by_environment()) {
        result.status = LodFovClampPatchStatus::disabled_by_environment;
        return result;
    }
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    result.status =
        LodFovClampPatchStatus::unsupported_compiler_or_architecture;
    return result;
#else
    const auto* const site = bindings.site(
        wawvr::t4::HookSiteId::lod_tan_half_fov_y_load);
    const wawvr::t4::Rva required_rva =
        bindings.profile().layout ==
                wawvr::t4::ExecutableLayoutId::t4_sp_1_7_1263
            ? wawvr::t4::Rva{0x002DEA1B}
            : bindings.profile().layout ==
                      wawvr::t4::ExecutableLayoutId::t4_mp_1_7_1263
                  ? wawvr::t4::Rva{0x002B75FB}
                  : wawvr::t4::Rva{};
    if (site == nullptr || required_rva == 0 || site->rva != required_rva ||
        site->minimum_patch_bytes != kPatchSize ||
        site->expected_size != kPatchSize ||
        !std::ranges::equal(site->expected_bytes(), kOriginalMovss)) {
        result.status = LodFovClampPatchStatus::preparation_failed;
        return result;
    }

    const auto prepared = wawvr::t4::prepare_inline_hook(
        bindings, wawvr::t4::HookSiteId::lod_tan_half_fov_y_load,
        reinterpret_cast<std::uintptr_t>(&wawvr_t4_lod_clamp_bridge));
    if (!prepared.ok() || prepared.hook->expected_size != kPatchSize ||
        prepared.hook->minimum_patch_bytes != kPatchSize ||
        !std::ranges::equal(prepared.hook->expected_bytes(), kOriginalMovss) ||
        prepared.hook->target >
            std::numeric_limits<std::uintptr_t>::max() - kPatchSize) {
        result.status = LodFovClampPatchStatus::preparation_failed;
        return result;
    }

    result.target = prepared.hook->target;
    result.continuation = prepared.hook->target + kPatchSize;
    std::array<std::uint8_t, kPatchSize> replacement{};
    if (!make_relative_jump(
            prepared.hook->target,
            reinterpret_cast<std::uintptr_t>(&wawvr_t4_lod_clamp_bridge),
            &replacement)) {
        result.status = LodFovClampPatchStatus::jump_out_of_range;
        return result;
    }

    wawvr_t4_lod_clamp_continuation_address = result.continuation;
    DWORD system_error = 0;
    const ExactPatchResult patch = replace_exact_bytes(
        result.target, kOriginalMovss, replacement,
        reinterpret_cast<std::uintptr_t>(&wawvr_t4_lod_clamp_bridge),
        &system_error);
    result.status = install_status_from_patch(patch);
    result.system_error = system_error;
    if (patch != ExactPatchResult::ok) {
        wawvr_t4_lod_clamp_continuation_address = 0;
        return result;
    }

    g_patch_site = result.target;
    g_original_bytes = kOriginalMovss;
    g_replacement_bytes = replacement;
    g_installed.store(true, std::memory_order_release);
    return result;
#endif
}

LodFovClampRestoreStatus restore_lod_fov_clamp_patch() noexcept {
    if (!g_installed.load(std::memory_order_acquire)) {
        return LodFovClampRestoreStatus::not_installed;
    }
#if !defined(_MSC_VER) || !defined(_M_IX86)
    return LodFovClampRestoreStatus::not_installed;
#else
    DWORD system_error = 0;
    const ExactPatchResult patch = replace_exact_bytes(
        g_patch_site, g_replacement_bytes, g_original_bytes,
        reinterpret_cast<std::uintptr_t>(&wawvr_t4_lod_clamp_bridge),
        &system_error);
    const LodFovClampRestoreStatus status = restore_status_from_patch(patch);
    if (patch == ExactPatchResult::ok ||
        patch == ExactPatchResult::expected_bytes_changed) {
        g_installed.store(false, std::memory_order_release);
        if (patch == ExactPatchResult::ok) {
            g_patch_site = 0;
            g_original_bytes = {};
            g_replacement_bytes = {};
        }
    }
    return status;
#endif
}

bool lod_fov_clamp_patch_installed() noexcept {
    return g_installed.load(std::memory_order_acquire);
}

const char* lod_fov_clamp_patch_status_name(
    const LodFovClampPatchStatus status) noexcept {
    switch (status) {
    case LodFovClampPatchStatus::installed: return "installed";
    case LodFovClampPatchStatus::already_installed:
        return "already-installed";
    case LodFovClampPatchStatus::disabled_by_environment:
        return "disabled-by-environment";
    case LodFovClampPatchStatus::unsupported_compiler_or_architecture:
        return "unsupported-compiler-or-architecture";
    case LodFovClampPatchStatus::preparation_failed:
        return "preparation-failed";
    case LodFovClampPatchStatus::jump_out_of_range:
        return "jump-out-of-range";
    case LodFovClampPatchStatus::thread_suspend_failed:
        return "thread-suspend-failed";
    case LodFovClampPatchStatus::expected_bytes_changed:
        return "expected-bytes-changed";
    case LodFovClampPatchStatus::target_protection_failed:
        return "target-protection-failed";
    case LodFovClampPatchStatus::patch_write_failed:
        return "patch-write-failed";
    case LodFovClampPatchStatus::patch_cache_flush_failed:
        return "patch-cache-flush-failed";
    case LodFovClampPatchStatus::protection_restore_failed:
        return "protection-restore-failed";
    }
    return "unknown";
}

const char* lod_fov_clamp_restore_status_name(
    const LodFovClampRestoreStatus status) noexcept {
    switch (status) {
    case LodFovClampRestoreStatus::restored: return "restored";
    case LodFovClampRestoreStatus::not_installed: return "not-installed";
    case LodFovClampRestoreStatus::foreign_patch_preserved:
        return "foreign-patch-preserved";
    case LodFovClampRestoreStatus::thread_suspend_failed:
        return "thread-suspend-failed";
    case LodFovClampRestoreStatus::target_protection_failed:
        return "target-protection-failed";
    case LodFovClampRestoreStatus::patch_write_failed:
        return "patch-write-failed";
    case LodFovClampRestoreStatus::patch_cache_flush_failed:
        return "patch-cache-flush-failed";
    case LodFovClampRestoreStatus::protection_restore_failed:
        return "protection-restore-failed";
    }
    return "unknown";
}

} // namespace wawvr::mod
