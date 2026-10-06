#include "bootstrap.hpp"
#include "d3d9ex_bootstrap.hpp"
#include "direct_boot_patch.hpp"
#include "oversized_resolution_patch.hpp"

#if defined(WAWVR_HAS_INPUT)
#include "bazooka_trail_hook.hpp"
#include "input_hook.hpp"
#include "tank_reticle_hook.hpp"
#include "physical_scope_hud_hook.hpp"
#include "manual_grenade_runtime.hpp"
#include "manual_reload_runtime.hpp"
#include "mounted_gun_runtime.hpp"
#include "tracked_hands_runtime.hpp"
#include "weapon_hook.hpp"
#endif

#if defined(WAWVR_HAS_XR) && defined(WAWVR_HAS_T4_BINDINGS)
#include "frame_boundary_hook.hpp"
#include "lod_fov_clamp.hpp"
#include "t4_hud_placement.hpp"
#include "t4_menu_input.hpp"
#include "t4_presentation_state.hpp"
#include "weapon_camera_patch.hpp"
#endif
#if defined(WAWVR_HAS_XR)
#include "present_hook.hpp"
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#if defined(WAWVR_HAS_T4_BINDINGS)
#include "t4_layout_selector.hpp"
#include "t4/profile.hpp"
#include "t4/runtime_win32.hpp"
#endif

namespace wawvr::mod {
namespace {

std::atomic<bool> g_bootstrap_complete{false};
std::atomic<bool> g_game_build_validated{false};

[[nodiscard]] std::wstring bootstrap_event_name(
    const bool ready) {
    return std::wstring(
               ready ? L"Local\\WaWVR-BootstrapReady-"
                     : L"Local\\WaWVR-BootstrapFailed-") +
           std::to_wstring(GetCurrentProcessId());
}

void signal_launcher_bootstrap_status(const bool ready) noexcept {
    const auto name = bootstrap_event_name(ready);
    const HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (event == nullptr) {
        return;
    }
    static_cast<void>(SetEvent(event));
    CloseHandle(event);
}

bool direct_boot_disabled_by_environment() noexcept {
    std::array<wchar_t, 8> value{};
    const DWORD length = GetEnvironmentVariableW(
        L"WAWVR_DISABLE_DIRECT_BOOT", value.data(),
        static_cast<DWORD>(value.size()));
    return length == 1 && value[0] == L'1';
}

std::filesystem::path module_directory(HMODULE module) {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(),
                                            static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(path.data(), path.data() + length).parent_path();
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &value);

    std::ostringstream stream;
    stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

std::filesystem::path bootstrap_log_path(HMODULE module) {
    return module_directory(module) / "WorldWarVR.log";
}

void write_bootstrap_header(HMODULE module) {
    std::ofstream log(bootstrap_log_path(module), std::ios::app);
    if (log) {
        log << '[' << timestamp() << "] World War VR DLL loaded\n";
    }

    OutputDebugStringW(L"WorldAtWarVR: bootstrap DLL loaded\n");
}

bool validate_game_build(HMODULE module) {
    std::ofstream log(bootstrap_log_path(module), std::ios::app);

    const auto record_stage = [&log](const char* const stage) {
        if (log) {
            log << "bootstrap stage: " << stage << '\n';
            log.flush();
        }
    };

#if defined(WAWVR_HAS_T4_BINDINGS)
    record_stage("capturing and validating the current T4 image");
    const auto result =
        wawvr::t4::validate_and_bind_supported_current_process();
    record_stage("current T4 image captured and validated");

    if (!result.capture_error.empty()) {
        if (log) {
            log << "T4 process capture failed: " << result.capture_error << '\n';
        }
        OutputDebugStringW(L"WorldAtWarVR: process capture failed; hooks disabled\n");
        return false;
    }

    if (!result.ok()) {
        if (log) {
            log << "T4 executable validation failed; hooks disabled\n";
            for (const auto& issue : result.validation.issues) {
                log << "  - " << issue.message << '\n';
            }
        }
        OutputDebugStringW(L"WorldAtWarVR: unsupported executable; hooks disabled\n");
        return false;
    }

    const auto mod_layout =
        select_t4_layout_family(result.bindings->profile());
    if (mod_layout == T4LayoutFamily::unsupported) {
        if (log) {
            log << "validated executable selected an unsupported or inconsistent mod layout; hooks disabled\n";
        }
        OutputDebugStringW(
            L"WorldAtWarVR: executable layout configuration failed; hooks disabled\n");
        return false;
    }

    // The validated bootstrap below installs process-lifetime callsite and
    // vtable routes into this module.  Pin it before the first permanent
    // patch so no executable slot can ever outlive its thunk code.
    HMODULE pinned_module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(module), &pinned_module) ||
        pinned_module != module) {
        const DWORD error = pinned_module != nullptr
            ? ERROR_INVALID_HANDLE
            : GetLastError();
        if (log) {
            log << "WorldWarVR.dll could not be pinned before early renderer patches; hooks disabled: win32-error="
                << error << '\n';
        }
        OutputDebugStringW(
            L"WorldAtWarVR: module pin failed before early renderer patches; hooks disabled\n");
        return false;
    }

#if defined(WAWVR_HAS_XR)
    const bool renderer_hooks_configured = configure_renderer_hooks(
        result.bindings->profile().layout);
    if (!renderer_hooks_configured) {
        if (log) {
            log << "renderer profile configuration rejected; hooks disabled\n";
        }
        OutputDebugStringW(
            L"WorldAtWarVR: renderer profile configuration failed; hooks disabled\n");
        return false;
    }
#endif
    const D3D9ExBootstrapPatchResult d3d9ex_bootstrap_patch =
        install_d3d9ex_bootstrap_patch(*result.bindings);
    const OversizedResolutionPatchResult oversized_resolution_patch =
        install_oversized_resolution_patch(*result.bindings);
    if (!d3d9ex_bootstrap_patch.ok() ||
        !oversized_resolution_patch.ok()) {
        if (log) {
            log << "early native-resolution renderer bootstrap rejected; hooks disabled: D3D9Ex="
                << d3d9ex_bootstrap_patch_status_name(
                       d3d9ex_bootstrap_patch.status)
                << " oversized-source="
                << oversized_resolution_patch_status_name(
                       oversized_resolution_patch.status)
                << '\n';
        }
        OutputDebugStringW(
            L"WorldAtWarVR: early native-resolution renderer bootstrap failed; hooks disabled\n");
        return false;
    }
    record_stage("early D3D9Ex and oversized-resolution patches installed");
    g_game_build_validated.store(true, std::memory_order_release);
    DirectBootPatchResult direct_boot{};
    if (mod_layout == T4LayoutFamily::multiplayer_1_7_1263) {
        direct_boot.status = DirectBootPatchStatus::not_applicable;
    } else if (direct_boot_disabled_by_environment()) {
        direct_boot.status = DirectBootPatchStatus::disabled_by_environment;
    } else {
        direct_boot = install_direct_boot_patch(*result.bindings);
    }
#if defined(WAWVR_HAS_XR)
    const LodFovClampPatchResult lod_fov_clamp_patch =
        install_lod_fov_clamp_patch(*result.bindings);
    const bool t4_presentation_state_bound =
        bind_t4_presentation_state(*result.bindings);
    const bool t4_menu_input_bound =
        bind_t4_menu_input(*result.bindings);
    const T4HudPlacementBindResult t4_hud_placement =
        bind_t4_hud_placement(*result.bindings);
    T4HudPlacementDrawHookInstallResult t4_hud_draw_hook{};
    if (t4_hud_placement.ok()) {
        t4_hud_draw_hook =
            install_t4_hud_placement_draw_hook(*result.bindings);
    } else {
        t4_hud_draw_hook.status =
            T4HudPlacementDrawHookStatus::placement_not_bound;
    }
    const FrameBoundaryHookInstallResult frame_boundary_hook =
        install_frame_boundary_hook(*result.bindings);
    const MeleeCameraPatchResult melee_camera_patch =
        install_melee_camera_patch(*result.bindings);
    WeaponCameraPatchResult weapon_camera_patch{};
    if (frame_boundary_hook.ok()) {
        weapon_camera_patch = install_weapon_camera_patch(*result.bindings);
    } else {
        weapon_camera_patch.status =
            WeaponCameraPatchStatus::frame_boundary_unavailable;
    }
    record_stage("XR frame, presentation, HUD, and camera hooks installed");
#endif
#if defined(WAWVR_HAS_INPUT)
    const InputHookInstallResult input_hook =
        install_controller_input_hook(*result.bindings);
    record_stage("controller input hook installed");
    const auto tank_reticle_hook = input_hook.ok()
        ? install_tank_reticle_hook(*result.bindings)
        : TankReticleHookResult::not_installed;
    record_stage("tank-only native crosshair hook inspected");
    const auto physical_scope_hud_hook = input_hook.ok()
        ? install_physical_scope_hud_hook(*result.bindings)
        : PhysicalScopeHudHookResult::not_installed;
    record_stage("physical-scope native overlay and HUD guards inspected");
    MountedGunRuntimeInstallResult mounted_gun{};
    if (input_hook.ok()) {
        mounted_gun = install_mounted_gun_runtime(*result.bindings);
    } else {
        mounted_gun.status = MountedGunRuntimeStatus::dependency_unavailable;
    }
    record_stage("mounted-machine-gun controller hooks installed");
    WeaponHookInstallResult weapon_hook{};
    if (input_hook.ok()) {
        weapon_hook = install_weapon_viewmodel_hook(*result.bindings);
    } else {
        weapon_hook.status = WeaponHookStatus::input_dependency_unavailable;
    }
    record_stage("weapon viewmodel hook installed");
    BazookaTrailHookInstallResult bazooka_trail_hook{};
    if (input_hook.ok() && weapon_hook.ok()) {
        bazooka_trail_hook = install_bazooka_trail_hook(*result.bindings);
    } else {
        bazooka_trail_hook.status =
            BazookaTrailHookStatus::dependency_unavailable;
    }
    record_stage("Bazooka client trail first-element hook installed");
    ManualReloadRuntimeInstallResult manual_reload{};
    if (input_hook.ok() && weapon_hook.ok()) {
        manual_reload = install_manual_reload_runtime(*result.bindings);
    } else {
        manual_reload.status =
            ManualReloadRuntimeStatus::dependency_unavailable;
    }
    TrackedHandsRuntimeInstallResult tracked_hands{};
    if (input_hook.ok() && weapon_hook.ok()) {
        tracked_hands = install_tracked_hands_runtime(*result.bindings);
    } else {
        tracked_hands.status =
            TrackedHandsRuntimeStatus::dependency_unavailable;
    }
    ManualGrenadeRuntimeInstallResult manual_grenade{};
    if (input_hook.ok() && weapon_hook.ok() && tracked_hands.ok()) {
        manual_grenade = install_manual_grenade_runtime(*result.bindings);
    } else {
        manual_grenade.status =
            ManualGrenadeRuntimeStatus::dependency_unavailable;
    }
    CampaignTargetingHookInstallResult campaign_targeting_hook{};
    if (input_hook.ok() && weapon_hook.ok()) {
        campaign_targeting_hook =
            install_campaign_rocket_targeting_hook(*result.bindings);
    } else {
        campaign_targeting_hook.status =
            CampaignTargetingHookStatus::dependency_unavailable;
    }
    record_stage(
        "manual reload, tracked hands, grenade, and targeting hooks installed");
#endif
    record_stage("validated bootstrap complete; publishing launcher handshake");
    if (log) {
        log << "T4 executable profile validated: "
            << result.bindings->profile().id << '\n';

        log << "D3D9Ex renderer bootstrap patch: profile="
            << result.bindings->profile().id
            << " factory-rva=0x" << std::hex << std::uppercase
            << kD3D9FactoryCallRva
            << " create-device-rva=0x" << kD3D9CreateDeviceDispatchRva
            << " status="
            << d3d9ex_bootstrap_patch_status_name(
                   d3d9ex_bootstrap_patch.status)
            << std::dec;
        if (d3d9ex_bootstrap_patch.system_error != 0) {
            log << " win32-error="
                << d3d9ex_bootstrap_patch.system_error;
        }
        if (d3d9ex_bootstrap_patch.thread_id != 0) {
            log << " thread-id=" << d3d9ex_bootstrap_patch.thread_id;
        }
        if (d3d9ex_bootstrap_patch.existing_factory != 0 ||
            d3d9ex_bootstrap_patch.existing_device != 0) {
            log << " existing-factory=0x" << std::hex << std::uppercase
                << d3d9ex_bootstrap_patch.existing_factory
                << " existing-device=0x"
                << d3d9ex_bootstrap_patch.existing_device << std::dec;
        }
        log << '\n';

        log << "oversized VR source patch: profile="
            << result.bindings->profile().id
            << " context-rva=0x" << std::hex << std::uppercase
            << kCustomResolutionContextRva
            << " status="
            << oversized_resolution_patch_status_name(
                   oversized_resolution_patch.status)
            << std::dec;
        if (oversized_resolution_patch.system_error != 0) {
            log << " win32-error="
                << oversized_resolution_patch.system_error;
        }
        if (oversized_resolution_patch.thread_id != 0) {
            log << " thread-id=" << oversized_resolution_patch.thread_id;
        }
        log << '\n';

        log << "direct-boot patch: profile=" << result.bindings->profile().id
            << " context-rva=0x" << std::hex << std::uppercase
            << kStartupIntroContextRva
            << " branch-rva=0x" << kStartupIntroBranchRva
            << " expected-opcode=0x"
            << static_cast<unsigned int>(kStartupIntroConditionalBranch)
            << " replacement-opcode=0x"
            << static_cast<unsigned int>(kStartupIntroUnconditionalBranch)
            << std::dec << " status="
            << direct_boot_patch_status_name(direct_boot.status);
        if (direct_boot.system_error != 0) {
            log << " win32-error=" << direct_boot.system_error;
        }
        if (!direct_boot.ok() && direct_boot.observed_size != 0) {
            log << " observed-context=";
            for (std::size_t index = 0; index < direct_boot.observed_size; ++index) {
                if (index != 0) {
                    log << ' ';
                }
                log << std::hex << std::uppercase << std::setw(2)
                    << std::setfill('0')
                    << static_cast<unsigned int>(direct_boot.observed[index]);
            }
            log << std::dec << std::setfill(' ');
        }
        log << '\n';
        if (direct_boot.status == DirectBootPatchStatus::not_applicable) {
            log << "direct-boot behavior: not applicable to multiplayer\n";
        } else if (direct_boot.ok()) {
            log << "direct-boot behavior: suppressed startup command "
                   "`cinematic Treyarch`; launcher +devmap remains eligible\n";
        } else {
            log << "direct-boot behavior: patch rejected; executable memory "
                   "was left or restored to its original behavior\n";
        }
#if defined(WAWVR_HAS_INPUT)
        log << "controller-input hook: profile="
            << result.bindings->profile().id
            << " site-rva=0x" << std::hex << std::uppercase
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_hook_site(
                       result.bindings->profile(),
                       wawvr::t4::HookSiteId::post_build_usercmd)->rva)
            << " camera-axis-rva=0x"
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_data_symbol(
                       result.bindings->profile(),
                       wawvr::t4::DataSymbolId::gameplay_refdef_viewport)->rva +
                   kGameplayRefdefAxisOffset)
            << " target=0x" << input_hook.target
            << " trampoline=0x" << input_hook.trampoline
            << std::dec << " status="
            << input_hook_status_name(input_hook.status);
        if (input_hook.system_error != 0) {
            log << " win32-error=" << input_hook.system_error;
        }
        log << '\n';
        log << "controller-weapon viewmodel hook: profile="
            << result.bindings->profile().id
            << " site-rva=0x" << std::hex << std::uppercase
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_hook_site(
                       result.bindings->profile(),
                       wawvr::t4::HookSiteId::viewmodel_weapon_call)->rva)
            << " target=0x" << weapon_hook.target
            << " original=0x" << weapon_hook.original
            << " ballistics-target=0x" << weapon_hook.ballistics_target
            << " calc-muzzle=0x" << weapon_hook.ballistics_original
            << " spread-target=0x" << weapon_hook.spread_target
            << " bullet-fire=0x" << weapon_hook.spread_original
            << " client-effects-target=0x"
            << weapon_hook.client_effects_target
            << " client-view-origin=0x"
            << weapon_hook.client_effects_original
            << " client-spread-target=0x"
            << weapon_hook.client_spread_target
            << " get-spread=0x"
            << weapon_hook.client_spread_original
            << std::dec << " status="
            << weapon_hook_status_name(weapon_hook.status);
        if (weapon_hook.system_error != 0) {
            log << " win32-error=" << weapon_hook.system_error;
        }
        log << '\n';
        log << "Bazooka client trail first-element hook: profile="
            << result.bindings->profile().id
            << " site-rva=0x48394 target=0x" << std::hex
            << std::uppercase << bazooka_trail_hook.target
            << " original=0x" << bazooka_trail_hook.original
            << std::dec << " status="
            << bazooka_trail_hook_status_name(
                   bazooka_trail_hook.status);
        if (bazooka_trail_hook.system_error != 0) {
            log << " win32-error=" << bazooka_trail_hook.system_error;
        }
        log << '\n';
        log << "manual reload bridge: profile="
            << result.bindings->profile().id
            << " callsites=0x1E805,0x1FACD,0x2092C,0x31E288"
            << " weapon=profiled_bolt_action_top_feed"
            << " status="
            << manual_reload_runtime_status_name(manual_reload.status);
        if (manual_reload.system_error != 0) {
            log << " win32-error=" << manual_reload.system_error;
        }
        log << '\n';
        log << "manual grenade bridge: profile="
            << result.bindings->profile().id
            << " fire-grenade-callsite-rva=0x150413";
        if (button_grenade_mode_enabled()) {
            log << " mode=button input=left-index-trigger native=frag";
        } else {
            log << " mode=manual grab=left-index-trigger"
                << " hips=frag-left,tactical-right";
        }
        log << " status="
            << manual_grenade_runtime_status_name(manual_grenade.status);
        if (manual_grenade.system_error != 0) {
            log << " win32-error=" << manual_grenade.system_error;
        }
        log << '\n';
        log << "standalone tracked hands: profile="
            << result.bindings->profile().id
            << " hands=left,right stock-arms=hidden"
            << " status="
            << tracked_hands_runtime_status_name(tracked_hands.status)
            << '\n';
        log << "campaign rocket-barrage targeting hook: profile="
            << result.bindings->profile().id
            << " target=0x" << std::hex << std::uppercase
            << campaign_targeting_hook.target
            << " Scr_AddVector=0x" << campaign_targeting_hook.original
            << std::dec << " status="
            << campaign_targeting_hook_status_name(
                   campaign_targeting_hook.status);
        if (campaign_targeting_hook.system_error != 0) {
            log << " win32-error="
                << campaign_targeting_hook.system_error;
        }
        log << '\n';
#endif
#if defined(WAWVR_HAS_XR)
        log << "stereo-scene LOD tanHalfFovY clamp: profile="
            << result.bindings->profile().id
            << " site-rva=0x" << std::hex << std::uppercase
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_hook_site(
                       result.bindings->profile(),
                       wawvr::t4::HookSiteId::lod_tan_half_fov_y_load)->rva)
            << " target=0x" << lod_fov_clamp_patch.target
            << " continuation=0x" << lod_fov_clamp_patch.continuation
            << std::dec << " status="
            << lod_fov_clamp_patch_status_name(
                   lod_fov_clamp_patch.status);
        if (lod_fov_clamp_patch.system_error != 0) {
            log << " win32-error=" << lod_fov_clamp_patch.system_error;
        }
        log << '\n';
        log << "T4 presentation-state binding: key-catchers-rva=0x"
            << std::hex << std::uppercase
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_data_symbol(
                       result.bindings->profile(),
                       wawvr::t4::DataSymbolId::key_catchers)->rva)
            << " connection-state-rva=0x"
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_data_symbol(
                       result.bindings->profile(),
                       wawvr::t4::DataSymbolId::connection_state)->rva)
            << std::dec << " status="
            << (t4_presentation_state_bound ? "bound" : "unavailable")
            << '\n';
        log << "T4 native menu input: CL_KeyEvent-rva=0x"
            << std::hex << std::uppercase
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_hook_site(
                       result.bindings->profile(),
                       wawvr::t4::HookSiteId::cl_key_event_entry_sentinel)
                       ->rva)
            << " UI_MouseEvent-rva=0x"
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_hook_site(
                       result.bindings->profile(),
                       wawvr::t4::HookSiteId::ui_mouse_event_entry_sentinel)
                       ->rva)
            << " Cbuf_AddText-rva=0x"
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_hook_site(
                       result.bindings->profile(),
                       wawvr::t4::HookSiteId::cbuf_add_text_entry_sentinel)
                       ->rva)
            << std::dec << " status="
            << (t4_menu_input_bound ? "bound" : "unavailable")
            << '\n';
        log << "T4 binocular HUD placement: setup-rva=0x"
            << std::hex << std::uppercase
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_hook_site(
                       result.bindings->profile(),
                       wawvr::t4::HookSiteId::
                           scr_place_setup_float_viewport_entry_sentinel)
                       ->rva)
            << " scr-place-rva=0x"
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_data_symbol(
                       result.bindings->profile(),
                       wawvr::t4::DataSymbolId::scr_place_view_zero)->rva)
            << " refdef-viewport-rva=0x"
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_data_symbol(
                       result.bindings->profile(),
                       wawvr::t4::DataSymbolId::gameplay_refdef_viewport)->rva)
            << std::dec << " status="
            << t4_hud_placement_bind_status_name(t4_hud_placement.status)
            << '\n';
        log << "same-frame CG_Draw2D HUD placement boundary: profile="
            << result.bindings->profile().id
            << " callsite-rva=0x" << std::hex << std::uppercase
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_hook_site(
                       result.bindings->profile(),
                       wawvr::t4::HookSiteId::cg_draw_2d_call)->rva)
            << " target=0x" << t4_hud_draw_hook.target
            << " original=0x" << t4_hud_draw_hook.original
            << std::dec << " status="
            << t4_hud_placement_draw_hook_status_name(
                   t4_hud_draw_hook.status);
        if (t4_hud_draw_hook.system_error != 0) {
            log << " win32-error=" << t4_hud_draw_hook.system_error;
        }
        log << '\n';
        log << "post-Com_Frame XR boundary: profile="
            << result.bindings->profile().id
            << " callsite-rva=0x" << std::hex << std::uppercase
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_hook_site(
                       result.bindings->profile(),
                       wawvr::t4::HookSiteId::main_loop_com_frame_call)->rva)
            << " target=0x" << frame_boundary_hook.target
            << " original=0x" << frame_boundary_hook.original
            << std::dec << " status="
            << frame_boundary_hook_status_name(frame_boundary_hook.status);
        if (frame_boundary_hook.system_error != 0) {
            log << " win32-error=" << frame_boundary_hook.system_error;
        }
        log << '\n';
        log << "weapon tag_camera suppression: profile="
            << result.bindings->profile().id
            << " site-rva=0x" << std::hex << std::uppercase
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_hook_site(
                       result.bindings->profile(),
                       wawvr::t4::HookSiteId::viewmodel_camera_tag_matrix_call)
                       ->rva)
            << " target=0x" << weapon_camera_patch.target
            << std::dec << " status="
            << weapon_camera_patch_status_name(weapon_camera_patch.status);
        if (weapon_camera_patch.system_error != 0) {
            log << " win32-error=" << weapon_camera_patch.system_error;
        }
        log << '\n';
        if (weapon_camera_patch.ok() &&
            weapon_camera_patch.status !=
                WeaponCameraPatchStatus::frame_boundary_unavailable) {
            log << "weapon-camera behavior: native viewmodel animation retained; "
                   "tag_camera cannot move or roll the VR refdef during reload, "
                   "sprint, or first spawn\n";
        }
        log << "auto-melee target-aim suppression: profile="
            << result.bindings->profile().id
            << " site-rva=0x" << std::hex << std::uppercase
            << static_cast<std::uint32_t>(
                   wawvr::t4::find_hook_site(
                       result.bindings->profile(),
                       wawvr::t4::HookSiteId::auto_melee_enabled_branch)->rva)
            << " target=0x" << melee_camera_patch.target
            << std::dec << " status="
            << melee_camera_patch_status_name(melee_camera_patch.status);
        if (melee_camera_patch.system_error != 0) {
            log << " win32-error=" << melee_camera_patch.system_error;
        }
        log << '\n';
        if (melee_camera_patch.ok()) {
            log << "melee-camera behavior: target pitch/yaw and command charge "
                   "lunge are suppressed; native knife action remains enabled\n";
        }
        log << "renderer-bootstrap=present-capture + post-Com_Frame-openxr\n";
#else
        log << "OpenXR target was not included; renderer hooks remain disabled\n";
#endif
#if defined(WAWVR_HAS_XR)
    if (!lod_fov_clamp_patch.ok()) {
        OutputDebugStringW(
            L"WorldAtWarVR: stereo-scene LOD-only FOV clamp rejected; stock LOD preserved\n");
    } else {
        OutputDebugStringW(
            L"WorldAtWarVR: exact stereo-scene LOD-only FOV clamp installed\n");
    }
    if (!frame_boundary_hook.ok()) {
        OutputDebugStringW(
            L"WorldAtWarVR: post-Com_Frame hook rejected; XR renderer disabled\n");
    } else {
        OutputDebugStringW(
            L"WorldAtWarVR: exact post-Com_Frame XR boundary installed\n");
    }
    if (!t4_hud_draw_hook.ok()) {
        OutputDebugStringW(
            L"WorldAtWarVR: same-frame CG_Draw2D HUD placement hook rejected\n");
    } else {
        OutputDebugStringW(
            L"WorldAtWarVR: exact same-frame CG_Draw2D HUD placement boundary installed\n");
    }
    if (weapon_camera_patch.status ==
            WeaponCameraPatchStatus::disabled_by_environment) {
        OutputDebugStringW(
            L"WorldAtWarVR: weapon-camera stabilization disabled by environment\n");
    } else if (!weapon_camera_patch.ok()) {
        OutputDebugStringW(
            L"WorldAtWarVR: weapon tag_camera patch rejected; stock camera preserved\n");
    } else {
        OutputDebugStringW(
            L"WorldAtWarVR: reload/sprint tag_camera motion suppressed\n");
    }
    if (melee_camera_patch.status ==
            MeleeCameraPatchStatus::disabled_by_environment) {
        OutputDebugStringW(
            L"WorldAtWarVR: auto-melee camera suppression disabled by environment\n");
    } else if (!melee_camera_patch.ok()) {
        OutputDebugStringW(
            L"WorldAtWarVR: auto-melee camera patch rejected; command lunge clear remains active\n");
    } else {
        OutputDebugStringW(
            L"WorldAtWarVR: auto-melee target camera rotation suppressed\n");
    }
#endif
    }
    if (direct_boot.status == DirectBootPatchStatus::not_applicable) {
        OutputDebugStringW(
            L"WorldAtWarVR: SP direct-boot patch not applicable in multiplayer\n");
    } else if (direct_boot.status == DirectBootPatchStatus::disabled_by_environment) {
        OutputDebugStringW(
            L"WorldAtWarVR: direct-boot patch disabled by environment\n");
    } else if (!direct_boot.ok()) {
        OutputDebugStringW(
            L"WorldAtWarVR: direct-boot patch rejected; continuing without it\n");
    } else {
        OutputDebugStringW(L"WorldAtWarVR: direct-map boot patch installed\n");
    }
#if defined(WAWVR_HAS_INPUT)
    if (input_hook.status == InputHookStatus::disabled_by_environment) {
        OutputDebugStringW(
            L"WorldAtWarVR: controller input disabled by environment\n");
    } else if (!input_hook.ok()) {
        OutputDebugStringW(
            L"WorldAtWarVR: controller input hook rejected; native input preserved\n");
    } else {
        OutputDebugStringW(
            L"WorldAtWarVR: exact T4 controller usercmd hook installed\n");
    }
    if (weapon_hook.status == WeaponHookStatus::disabled_by_environment) {
        OutputDebugStringW(
            L"WorldAtWarVR: controller weapon viewmodel disabled by environment\n");
    } else if (!weapon_hook.ok()) {
        OutputDebugStringW(
            L"WorldAtWarVR: controller weapon viewmodel hook rejected; stock weapon preserved\n");
    } else {
        OutputDebugStringW(
            L"WorldAtWarVR: tracked-controller weapon viewmodel hook installed\n");
    }
    if (bazooka_trail_hook.status ==
        BazookaTrailHookStatus::not_applicable) {
        OutputDebugStringW(
            L"WorldAtWarVR: Bazooka trail seed is not applicable in multiplayer\n");
    } else if (!bazooka_trail_hook.ok()) {
        OutputDebugStringW(
            L"WorldAtWarVR: Bazooka trail seed hook rejected; native projectile trail retained\n");
    } else {
        OutputDebugStringW(
            L"WorldAtWarVR: Bazooka trail starts at the fire-time physical muzzle\n");
    }
    if (!mounted_gun.ok() && mounted_gun.status !=
            MountedGunRuntimeStatus::not_applicable) {
        OutputDebugStringW(
            L"WorldAtWarVR: mounted-machine-gun controller hooks rejected; native turret retained\n");
    } else if (mounted_gun.status == MountedGunRuntimeStatus::installed) {
        OutputDebugStringW(
            L"WorldAtWarVR: exact mounted-machine-gun aim, firing, and visual hooks installed\n");
    }
    if (!manual_reload.ok()) {
        OutputDebugStringW(
            L"WorldAtWarVR: physical bolt-action reload unavailable; native reload preserved\n");
    } else if (manual_reload.status ==
               ManualReloadRuntimeStatus::installed) {
        OutputDebugStringW(
            L"WorldAtWarVR: profiled physical bolt-action stripper-clip reload installed\n");
    }
    if (!tracked_hands.ok()) {
        OutputDebugStringW(
            L"WorldAtWarVR: standalone tracked gloves unavailable; gun-only fallback retained\n");
    } else if (tracked_hands.status ==
               TrackedHandsRuntimeStatus::installed) {
        OutputDebugStringW(
            L"WorldAtWarVR: independently tracked left and right gloves installed\n");
    }
    if (!manual_grenade.ok()) {
        OutputDebugStringW(
            L"WorldAtWarVR: physical hip grenades unavailable; native grenade controls preserved\n");
    } else if (manual_grenade.status ==
               ManualGrenadeRuntimeStatus::installed) {
        OutputDebugStringW(
            L"WorldAtWarVR: left-trigger physical hip grenades installed\n");
    }
    if (campaign_targeting_hook.status ==
        CampaignTargetingHookStatus::not_applicable) {
        OutputDebugStringW(
            L"WorldAtWarVR: campaign rocket targeting is not applicable in multiplayer\n");
    } else if (!campaign_targeting_hook.ok()) {
        OutputDebugStringW(
            L"WorldAtWarVR: campaign rocket targeting hook rejected; native script angles preserved\n");
    } else {
        OutputDebugStringW(
            L"WorldAtWarVR: Little Resistance rocket targeting follows the tracked controller\n");
    }

    if (log) {
        log << "tank-only native crosshair hook: "
            << describe_tank_reticle_hook_result(tank_reticle_hook) << '\n';
        log << "physical-scope native overlay and HUD guards: "
            << describe_physical_scope_hud_hook_result(physical_scope_hud_hook)
            << '\n';
    }
    const bool tank_reticle_ready =
        tank_reticle_hook == TankReticleHookResult::installed ||
        tank_reticle_hook == TankReticleHookResult::already_installed;
    const bool physical_scope_hud_ready =
        physical_scope_hud_hook == PhysicalScopeHudHookResult::installed ||
        physical_scope_hud_hook == PhysicalScopeHudHookResult::already_installed;

    // Retail SP relies on this complete chain for the already-accepted VR
    // controls, tracked hands, and physical reload behavior. Do not publish a
    // successful launcher handshake for a partially installed build; the
    // launcher owns the still-suspended process and will fail closed.
    if (mod_layout == T4LayoutFamily::single_player_1_7_1263 &&
        (!input_hook.ok() || !weapon_hook.ok() ||
         !bazooka_trail_hook.ok() || !manual_reload.ok() ||
         !tracked_hands.ok() || !manual_grenade.ok() || !tank_reticle_ready ||
         !physical_scope_hud_ready)) {
        if (log) {
            log << "required retail SP gameplay bootstrap incomplete; rejecting launcher handshake\n";
        }
        OutputDebugStringW(
            L"WorldAtWarVR: required retail SP gameplay bootstrap incomplete; launch rejected\n");
        return false;
    }
#endif
    OutputDebugStringW(L"WorldAtWarVR: exact T4 build validated\n");
    return true;
#else
    if (log) {
        log << "T4 bindings were not included in this build; hooks disabled\n";
    }
    return false;
#endif
}

} // namespace

DWORD WINAPI bootstrap_thread(void* module_handle) noexcept {
    try {
        const auto module = static_cast<HMODULE>(module_handle);
        write_bootstrap_header(module);
        const bool validated = validate_game_build(module);
        g_bootstrap_complete.store(true, std::memory_order_release);
        signal_launcher_bootstrap_status(validated);
#if defined(WAWVR_HAS_XR) && defined(WAWVR_HAS_T4_BINDINGS)
        if (validated && frame_boundary_hook_installed()) {
            run_present_hook_monitor(module);
        }
#if defined(WAWVR_HAS_INPUT)
        const auto scope_hud_restore = restore_physical_scope_hud_hook();
        {
            std::ofstream scope_log(bootstrap_log_path(module), std::ios::app);
            if (scope_log) scope_log << "physical-scope HUD guard restore: "
                << describe_physical_scope_hud_hook_result(scope_hud_restore)
                << '\n';
        }
        const auto tank_reticle_restore = restore_tank_reticle_hook();
        {
            std::ofstream tank_log(bootstrap_log_path(module), std::ios::app);
            if (tank_log) tank_log << "tank-only native crosshair hook restore: "
                << describe_tank_reticle_hook_result(tank_reticle_restore) << '\n';
        }
#endif
        // The Present monitor removes the stereo-scene hook first, so no new
        // thread-local stereo clamp scopes can begin before this exact E9 is
        // restored. Any residual in-flight bridge is handled fail-closed by
        // peer-thread quiescence.
        const LodFovClampRestoreStatus lod_fov_clamp_restore =
            restore_lod_fov_clamp_patch();
        // Request the final full-width write while the validated WinMain
        // boundary is still callable. The bootstrap thread only waits; it
        // never invokes the proprietary setup ABI itself.
        constexpr std::uint32_t kHudQuiesceTimeoutMilliseconds = 2'000;
        const T4HudPlacementQuiesceResult hud_quiesce_wait =
            request_t4_hud_placement_quiesce(
                kHudQuiesceTimeoutMilliseconds);
        const WeaponCameraPatchResult camera_restore =
            restore_weapon_camera_patch();
        const MeleeCameraPatchResult melee_camera_restore =
            restore_melee_camera_patch();
        // Capture an acknowledgement that may have arrived while the camera
        // patch was being restored. A zero-duration second wait does not call
        // engine code or extend shutdown latency.
        const T4HudPlacementQuiesceResult hud_quiesce_cleanup =
            hud_quiesce_wait.ok()
                ? hud_quiesce_wait
                : request_t4_hud_placement_quiesce(0);
        T4HudPlacementQuiesceResult hud_quiesce_final =
            hud_quiesce_cleanup;
        T4HudPlacementDrawHookRestoreStatus hud_draw_restore =
            T4HudPlacementDrawHookRestoreStatus::not_installed;
        FrameBoundaryRestoreStatus boundary_restore =
            FrameBoundaryRestoreStatus::not_installed;
        if (hud_quiesce_cleanup.ok()) {
            request_frame_boundary_hook_shutdown();
            request_t4_hud_placement_draw_hook_shutdown();
            // Taking the central exclusive lock here drains any service that
            // had already passed an outer gate. Late entrants then see
            // disabled state without reading the binding snapshot.
            hud_quiesce_final =
                disable_t4_hud_placement_services_and_drain();
            hud_draw_restore = restore_t4_hud_placement_draw_hook();
            boundary_restore = restore_frame_boundary_hook();
        }
        if (hud_quiesce_cleanup.ok() && hud_quiesce_final.ok()) {
            clear_t4_hud_placement();
        }
        clear_t4_menu_input();
        clear_t4_presentation_state();
        std::ofstream log(bootstrap_log_path(module), std::ios::app);
        if (log) {
            log << "stereo-scene LOD tanHalfFovY clamp restore: "
                << lod_fov_clamp_restore_status_name(
                       lod_fov_clamp_restore)
                << '\n';
            log << "weapon tag_camera suppression restore: "
                << weapon_camera_patch_status_name(camera_restore.status)
                << '\n';
            log << "auto-melee target-aim suppression restore: "
                << melee_camera_patch_status_name(
                       melee_camera_restore.status)
                << '\n';
            log << "post-Com_Frame hook restore: "
                << frame_boundary_restore_status_name(boundary_restore)
                << '\n';
            log << "same-frame CG_Draw2D HUD placement hook restore: "
                << t4_hud_placement_draw_hook_restore_status_name(
                       hud_draw_restore)
                << '\n';
            log << "scrPlaceView[0] game-thread quiesce wait: "
                << t4_hud_placement_quiesce_status_name(
                       hud_quiesce_wait.status)
                << " apply="
                << t4_hud_placement_apply_status_name(
                       hud_quiesce_wait.apply_status)
                << " wait-result=0x" << std::hex << std::uppercase
                << hud_quiesce_wait.wait_result << std::dec << '\n';
            log << "scrPlaceView[0] final drain: "
                << t4_hud_placement_quiesce_status_name(
                       hud_quiesce_final.status)
                << " apply="
                << t4_hud_placement_apply_status_name(
                       hud_quiesce_final.apply_status)
                << " binding="
                << (hud_quiesce_cleanup.ok() && hud_quiesce_final.ok()
                        ? "cleared"
                        : "retained")
                << " cleanup="
                << (hud_quiesce_cleanup.ok() ? "completed" : "deferred")
                << '\n';
        }
#elif defined(WAWVR_HAS_XR)
        static_cast<void>(validated);
#else
        static_cast<void>(validated);
#endif
    } catch (...) {
        OutputDebugStringW(L"WorldAtWarVR: bootstrap logging failed\n");
        g_bootstrap_complete.store(true, std::memory_order_release);
        signal_launcher_bootstrap_status(false);
    }
    return 0;
}

bool bootstrap_complete() noexcept {
    return g_bootstrap_complete.load(std::memory_order_acquire);
}

bool game_build_validated() noexcept {
    return g_game_build_validated.load(std::memory_order_acquire);
}

} // namespace wawvr::mod
