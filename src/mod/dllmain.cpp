#include "bootstrap.hpp"

#if defined(WAWVR_HAS_INPUT)
#include "bazooka_trail_hook.hpp"
#include "input_hook.hpp"
#include "tank_reticle_hook.hpp"
#include "physical_scope_hud_hook.hpp"
#include "manual_grenade_runtime.hpp"
#include "manual_reload_runtime.hpp"
#include "tracked_hands_runtime.hpp"
#include "weapon_hook.hpp"
#endif

#if defined(WAWVR_HAS_XR) && defined(WAWVR_HAS_T4_BINDINGS)
#include "frame_boundary_hook.hpp"
#include "t4_hud_placement.hpp"
#endif
#if defined(WAWVR_HAS_XR)
#include "present_hook.hpp"
#endif

#include <windows.h>

extern "C" __declspec(dllexport) const wchar_t* WorldAtWarVR_GetVersion() {
    return L"0.4.0-t4-vr-mp";
}

extern "C" __declspec(dllexport) BOOL WorldAtWarVR_IsBootstrapComplete() {
    return wawvr::mod::bootstrap_complete() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) BOOL WorldAtWarVR_IsGameBuildValidated() {
    return wawvr::mod::game_build_validated() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) void WorldAtWarVR_RequestRendererShutdown() {
#if defined(WAWVR_HAS_XR)
    wawvr::mod::request_present_hook_shutdown();
#endif
}

extern "C" __declspec(dllexport) BOOL WorldAtWarVR_IsPresentHookInstalled() {
#if defined(WAWVR_HAS_XR)
    return wawvr::mod::present_hook_installed() ? TRUE : FALSE;
#else
    return FALSE;
#endif
}

extern "C" __declspec(dllexport) BOOL WorldAtWarVR_IsFrameBoundaryHookInstalled() {
#if defined(WAWVR_HAS_XR) && defined(WAWVR_HAS_T4_BINDINGS)
    return wawvr::mod::frame_boundary_hook_installed() ? TRUE : FALSE;
#else
    return FALSE;
#endif
}

extern "C" __declspec(dllexport) BOOL WorldAtWarVR_IsMonoXrReady() {
#if defined(WAWVR_HAS_XR)
    return wawvr::mod::mono_xr_ready() ? TRUE : FALSE;
#else
    return FALSE;
#endif
}

extern "C" __declspec(dllexport) BOOL WorldAtWarVR_IsControllerInputHookInstalled() {
#if defined(WAWVR_HAS_INPUT)
    return wawvr::mod::controller_input_hook_installed() ? TRUE : FALSE;
#else
    return FALSE;
#endif
}

extern "C" __declspec(dllexport) BOOL WorldAtWarVR_IsControllerInputEnabled() {
#if defined(WAWVR_HAS_INPUT)
    return wawvr::mod::controller_input_hook_enabled() ? TRUE : FALSE;
#else
    return FALSE;
#endif
}

extern "C" __declspec(dllexport) void WorldAtWarVR_RequestInputShutdown() {
#if defined(WAWVR_HAS_INPUT)
    wawvr::mod::request_physical_scope_hud_hook_shutdown();
    wawvr::mod::request_tank_reticle_hook_shutdown();
    wawvr::mod::request_bazooka_trail_hook_shutdown();
    wawvr::mod::request_manual_grenade_shutdown();
    wawvr::mod::request_manual_reload_orderly_shutdown();
    wawvr::mod::request_tracked_hands_shutdown();
    wawvr::mod::request_controller_input_shutdown();
    wawvr::mod::request_weapon_hook_shutdown();
#endif
}

extern "C" __declspec(dllexport) BOOL WorldAtWarVR_IsWeaponViewmodelHookInstalled() {
#if defined(WAWVR_HAS_INPUT)
    return wawvr::mod::weapon_viewmodel_hook_installed() ? TRUE : FALSE;
#else
    return FALSE;
#endif
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
#if defined(WAWVR_HAS_INPUT)
        wawvr::mod::request_physical_scope_hud_hook_shutdown();
        wawvr::mod::request_tank_reticle_hook_shutdown();
        wawvr::mod::request_bazooka_trail_hook_shutdown();
        wawvr::mod::request_manual_grenade_shutdown();
        wawvr::mod::request_manual_reload_shutdown();
        wawvr::mod::request_tracked_hands_shutdown();
        wawvr::mod::request_controller_input_shutdown();
        wawvr::mod::request_weapon_hook_shutdown();
#endif
#if defined(WAWVR_HAS_XR) && defined(WAWVR_HAS_T4_BINDINGS)
        wawvr::mod::request_frame_boundary_hook_shutdown();
        wawvr::mod::request_t4_hud_placement_draw_hook_shutdown();
#endif
#if defined(WAWVR_HAS_XR)
        wawvr::mod::request_present_hook_shutdown();
#endif
        return TRUE;
    }
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }

    DisableThreadLibraryCalls(instance);
    const HANDLE thread = CreateThread(nullptr, 0, wawvr::mod::bootstrap_thread,
                                       instance, 0, nullptr);
    if (thread != nullptr) {
        CloseHandle(thread);
    }
    return TRUE;
}
