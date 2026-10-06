#pragma once

#include "t4/bindings.hpp"

#include <cstdint>

namespace wawvr::mod {

inline constexpr wawvr::t4::Rva kCgDraw2DRva = 0x000388A0;

enum class T4HudPlacementBindStatus : std::uint8_t {
    bound,
    unsupported_compiler_or_architecture,
    rejected_wrong_profile,
    sentinel_mismatch,
    address_unavailable,
    memory_protection_invalid,
    synchronization_unavailable,
};

struct T4HudPlacementBindResult final {
    T4HudPlacementBindStatus status{
        T4HudPlacementBindStatus::address_unavailable};
    std::uintptr_t setup_function{};
    std::uintptr_t placement{};
    std::uintptr_t refdef{};

    [[nodiscard]] bool ok() const noexcept {
        return status == T4HudPlacementBindStatus::bound;
    }
};

enum class T4HudPlacementApplyStatus : std::uint8_t {
    applied_full_frame,
    applied_full_frame_cached_dimensions,
    applied_packed_eye,
    not_bound,
    service_disabled,
    wrong_thread,
    sentinel_changed,
    memory_unavailable,
    invalid_dimensions,
};

enum class T4HudPlacementQuiesceStatus : std::uint8_t {
    full_frame_restored,
    full_frame_restored_cached_dimensions,
    no_modification_needed,
    not_bound,
    synchronization_unavailable,
    timed_out,
    restore_failed,
    disabled_without_acknowledgement,
};

struct T4HudPlacementQuiesceResult final {
    T4HudPlacementQuiesceStatus status{
        T4HudPlacementQuiesceStatus::not_bound};
    T4HudPlacementApplyStatus apply_status{
        T4HudPlacementApplyStatus::not_bound};
    std::uint32_t wait_result{};

    [[nodiscard]] bool ok() const noexcept {
        return status == T4HudPlacementQuiesceStatus::full_frame_restored ||
               status == T4HudPlacementQuiesceStatus::
                   full_frame_restored_cached_dimensions ||
               status == T4HudPlacementQuiesceStatus::
                   no_modification_needed ||
               status == T4HudPlacementQuiesceStatus::not_bound;
    }
};

enum class T4HudPlacementDrawHookStatus : std::uint8_t {
    installed,
    already_installed,
    placement_not_bound,
    disabled_by_environment,
    unsupported_compiler_or_architecture,
    rejected_wrong_profile,
    preparation_failed,
    original_target_mismatch,
    jump_out_of_range,
    thread_suspend_failed,
    expected_bytes_changed,
    target_protection_failed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
};

struct T4HudPlacementDrawHookInstallResult final {
    T4HudPlacementDrawHookStatus status{
        T4HudPlacementDrawHookStatus::preparation_failed};
    std::uintptr_t target{};
    std::uintptr_t original{};
    std::uint32_t system_error{};

    [[nodiscard]] bool ok() const noexcept {
        return status == T4HudPlacementDrawHookStatus::installed ||
               status == T4HudPlacementDrawHookStatus::already_installed;
    }
};

enum class T4HudPlacementDrawHookRestoreStatus : std::uint8_t {
    restored,
    not_installed,
    foreign_patch_preserved,
    thread_suspend_failed,
    target_protection_failed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
};

// Resolves the exact register-ABI setup function and the two data objects only
// after the complete executable profile and both placement sentinels pass.
[[nodiscard]] T4HudPlacementBindResult bind_t4_hud_placement(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;
void clear_t4_hud_placement() noexcept;

// Must run on the validated WinMain/Com_Frame thread before the original
// Com_Frame call. It deliberately refreshes every valid frame so renderer
// initialization, Reset, and vid_restart cannot leave a stale placement.
[[nodiscard]] T4HudPlacementApplyStatus
service_t4_hud_placement_before_com_frame() noexcept;

// Same game thread, immediately before the exact CG_Draw2D call. It cannot
// establish thread ownership; the pre-Com_Frame boundary must do that first.
[[nodiscard]] T4HudPlacementApplyStatus
service_t4_hud_placement_before_cg_draw_2d() noexcept;

// Requests a game-thread full-frame write and waits only for the bounded
// acknowledgement. No engine function is called by the waiting bootstrap
// thread. After the wait, callers disable the two outer service gates and use
// disable_t4_hud_placement_services_and_drain() to close late-entry races.
[[nodiscard]] T4HudPlacementQuiesceResult
request_t4_hud_placement_quiesce(std::uint32_t timeout_milliseconds) noexcept;
[[nodiscard]] T4HudPlacementQuiesceResult
disable_t4_hud_placement_services_and_drain() noexcept;

// Replaces only the exact five-byte CG_Draw2D call. Its transparent bridge
// refreshes scrPlaceView[0] from the current, same-frame UI/game state and then
// tail-jumps to the stock EAX-argument function. Installation and restoration
// are independent of the WinMain Com_Frame call hook.
[[nodiscard]] T4HudPlacementDrawHookInstallResult
install_t4_hud_placement_draw_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept;
void request_t4_hud_placement_draw_hook_shutdown() noexcept;
[[nodiscard]] T4HudPlacementDrawHookRestoreStatus
restore_t4_hud_placement_draw_hook() noexcept;
[[nodiscard]] bool t4_hud_placement_draw_hook_installed() noexcept;

[[nodiscard]] const char* t4_hud_placement_bind_status_name(
    T4HudPlacementBindStatus status) noexcept;
[[nodiscard]] const char* t4_hud_placement_apply_status_name(
    T4HudPlacementApplyStatus status) noexcept;
[[nodiscard]] const char* t4_hud_placement_quiesce_status_name(
    T4HudPlacementQuiesceStatus status) noexcept;
[[nodiscard]] const char* t4_hud_placement_draw_hook_status_name(
    T4HudPlacementDrawHookStatus status) noexcept;
[[nodiscard]] const char* t4_hud_placement_draw_hook_restore_status_name(
    T4HudPlacementDrawHookRestoreStatus status) noexcept;

}  // namespace wawvr::mod
