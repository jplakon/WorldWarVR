set(
    WAWVR_MOD_SOURCES
    bootstrap.cpp
    bootstrap.hpp
    d3d9ex_bootstrap.cpp
    d3d9ex_bootstrap.hpp
    direct_boot_patch.cpp
    direct_boot_patch.hpp
    oversized_resolution_patch.cpp
    oversized_resolution_patch.hpp
    dllmain.cpp
    t4_layout_selector.hpp
)

if(TARGET wawvr_xr)
    list(
        APPEND WAWVR_MOD_SOURCES
        controller_state.cpp
        controller_state.hpp
        current_head_local_controller_pose_logic.cpp
        current_head_local_controller_pose_logic.hpp
        camera_comfort_logic.cpp
        camera_comfort_logic.hpp
        comfort_panel_constants.hpp
        immutable_thunk_route.hpp
        menu_button_gesture.cpp
        menu_button_gesture.hpp
        menu_panel_logic.cpp
        menu_panel_logic.hpp
        menu_surface_logic.cpp
        menu_surface_logic.hpp
        mounted_gun_logic.cpp
        mounted_gun_logic.hpp
        peer_thread_quiescence.cpp
        peer_thread_quiescence.hpp
        performance_timing.cpp
        performance_timing.hpp
        performance_timing_logic.hpp
        physical_scope_draw_filter.cpp
        physical_scope_draw_filter.hpp
        physical_scope_logic.cpp
        physical_scope_logic.hpp
        physical_scope_layout.hpp
        scope_visibility_snapshot.hpp
        present_hook.cpp
        present_hook.hpp
        present_hook_logic.cpp
        present_hook_logic.hpp
        smoke_surface_snapshot.cpp
        smoke_surface_snapshot.hpp
        stereo_backend_hook.cpp
        stereo_backend_hook.hpp
        stereo_diagnostics.hpp
        stereo_frame_broker.cpp
        stereo_frame_broker.hpp
        stereo_scene_hook.cpp
        stereo_scene_hook.hpp
        stereo_scene_math.cpp
        stereo_scene_math.hpp
        tracking_anchor_sync.hpp
    )
endif()

if(TARGET wawvr_xr AND TARGET wawvr_t4)
    list(
        APPEND WAWVR_MOD_SOURCES
        firing_haptic_logic.cpp
        firing_haptic_logic.hpp
        firing_haptics.cpp
        firing_haptics.hpp
        mounted_gun_runtime.cpp
        mounted_gun_runtime.hpp
        frame_boundary_hook.cpp
        frame_boundary_hook.hpp
        campaign_unlock_logic.hpp
        automatic_reload_setting.cpp
        automatic_reload_setting.hpp
        button_grenade_logic.cpp
        button_grenade_logic.hpp
        lod_fov_clamp.cpp
        lod_fov_clamp.hpp
        lod_fov_clamp_logic.hpp
        hud_placement_logic.cpp
        hud_placement_logic.hpp
        bolt_action_weapon_profile.cpp
        bolt_action_weapon_profile.hpp
        bazooka_trail_hook.cpp
        bazooka_trail_hook.hpp
        bazooka_trail_seed_logic.cpp
        bazooka_trail_seed_logic.hpp
        detachable_magazine_weapon_profile.cpp
        detachable_magazine_weapon_profile.hpp
        kar98_bolt_action_logic.cpp
        kar98_bolt_action_logic.hpp
        magazine_charging_logic.cpp
        magazine_charging_logic.hpp
        magazine_chamber_lock_table.cpp
        magazine_chamber_lock_table.hpp
        weapon_camera_patch.cpp
        weapon_camera_patch.hpp
        weapon_grip_logic.cpp
        weapon_grip_logic.hpp
        support_animation_policy.cpp
        support_animation_policy.hpp
        two_hand_sight_alignment_policy.cpp
        two_hand_sight_alignment_policy.hpp
        input_hook.cpp
        input_hook.hpp
        input_mapping.cpp
        input_mapping.hpp
        satchel_input_logic.cpp
        satchel_input_logic.hpp
        tank_control_logic.cpp
        tank_control_logic.hpp
        aircraft_control_logic.cpp
        aircraft_control_logic.hpp
        aircraft_control_runtime.cpp
        aircraft_control_runtime.hpp
        tank_reticle_hook.cpp
        tank_reticle_hook.hpp
        physical_scope_hud_hook.cpp
        physical_scope_hud_hook.hpp
        manual_reload_controller_logic.cpp
        manual_reload_controller_logic.hpp
        manual_reload_runtime.cpp
        manual_reload_runtime.hpp
        manual_grenade_logic.cpp
        manual_grenade_logic.hpp
        manual_grenade_runtime.cpp
        manual_grenade_runtime.hpp
        simulator_map_handoff_logic.hpp
        synthetic_scene_slots.cpp
        synthetic_scene_slots.hpp
        menu_input_logic.cpp
        menu_input_logic.hpp
        native_gameplay_command_logic.cpp
        native_gameplay_command_logic.hpp
        performance_command_logic.cpp
        performance_command_logic.hpp
        performance_command_service.cpp
        performance_command_service.hpp
        t4_menu_input.cpp
        t4_menu_input.hpp
        t4_hud_placement.cpp
        t4_hud_placement.hpp
        t4_hud_placement_hook.cpp
        t4_presentation_state.cpp
        t4_presentation_state.hpp
        tracked_hands_runtime.cpp
        tracked_hands_runtime.hpp
        pistol_support_pose.cpp
        pistol_support_pose.hpp
        pistol_hand_skinning.cpp
        pistol_hand_skinning.hpp
        head_relative_pose_freeze_logic.cpp
        head_relative_pose_freeze_logic.hpp
        post_t4_aim_phase_logic.cpp
        post_t4_aim_phase_logic.hpp
        weapon_pose_pipeline_trace_logic.cpp
        weapon_pose_pipeline_trace_logic.hpp
        weapon_frame_base_phase_logic.cpp
        weapon_frame_base_phase_logic.hpp
        viewmodel_filter.cpp
        viewmodel_filter.hpp
        weapon_placement.cpp
        weapon_placement.hpp
        weapon_ballistics_logic.cpp
        weapon_ballistics_logic.hpp
        rocket_barrage_logic.cpp
        rocket_barrage_logic.hpp
        weapon_hook.cpp
        weapon_hook.hpp
    )
endif()

add_library(WorldAtWarVR SHARED ${WAWVR_MOD_SOURCES})

target_link_libraries(WorldAtWarVR PRIVATE wawvr_project_options)
if(TARGET wawvr_gameplay)
    target_link_libraries(WorldAtWarVR PRIVATE wawvr_gameplay)
endif()
target_compile_definitions(WorldAtWarVR PRIVATE WAWVR_BUILD_DLL=1)

if(TARGET wawvr_t4)
    target_link_libraries(WorldAtWarVR PRIVATE wawvr_t4)
    target_compile_definitions(WorldAtWarVR PRIVATE WAWVR_HAS_T4_BINDINGS=1)
endif()

if(TARGET wawvr_xr)
    target_link_libraries(WorldAtWarVR PRIVATE wawvr_xr d3d9)
    target_compile_definitions(WorldAtWarVR PRIVATE WAWVR_HAS_XR=1)
endif()

if(TARGET wawvr_xr AND TARGET wawvr_t4)
    target_compile_definitions(WorldAtWarVR PRIVATE WAWVR_HAS_INPUT=1)
endif()

set_target_properties(
    WorldAtWarVR
    PROPERTIES
        OUTPUT_NAME "WorldWarVR"
        PREFIX ""
)

if(WAWVR_BUILD_TESTS AND BUILD_TESTING)
    add_executable(WorldAtWarVRResidentPageAccessTest
        resident_page_access.hpp resident_page_access_test.cpp)
    target_link_libraries(WorldAtWarVRResidentPageAccessTest PRIVATE wawvr_project_options psapi)
    add_test(NAME mod.resident_page_access COMMAND WorldAtWarVRResidentPageAccessTest)

    add_executable(WorldAtWarVRResidentPageAccessTransitionTest
        resident_page_access.hpp resident_page_access_transition_test.cpp)
    target_link_libraries(WorldAtWarVRResidentPageAccessTransitionTest PRIVATE wawvr_project_options psapi)
    add_test(NAME mod.resident_page_access_transitions COMMAND WorldAtWarVRResidentPageAccessTransitionTest)

    add_executable(WorldAtWarVRVehicleSnapshotReaderTest
        vehicle_snapshot_reader.hpp vehicle_snapshot_reader_test.cpp)
    target_link_libraries(WorldAtWarVRVehicleSnapshotReaderTest PRIVATE wawvr_project_options)
    add_test(NAME mod.vehicle_snapshot_reader COMMAND WorldAtWarVRVehicleSnapshotReaderTest)

    add_executable(WorldAtWarVRPerformanceCommandLogicTest
        performance_command_logic.cpp performance_command_logic.hpp performance_command_logic_test.cpp)
    target_link_libraries(WorldAtWarVRPerformanceCommandLogicTest PRIVATE wawvr_project_options)
    add_test(NAME mod.performance_command_logic COMMAND WorldAtWarVRPerformanceCommandLogicTest)

    add_executable(WorldAtWarVRSatchelInputLogicTest
        satchel_input_logic.cpp satchel_input_logic.hpp satchel_input_logic_test.cpp)
    target_link_libraries(WorldAtWarVRSatchelInputLogicTest PRIVATE wawvr_project_options)
    add_test(NAME mod.satchel_input_logic COMMAND WorldAtWarVRSatchelInputLogicTest)
    add_executable(WorldAtWarVRAircraftControlLogicTest
        aircraft_control_logic.cpp aircraft_control_logic.hpp aircraft_control_logic_test.cpp)
    target_link_libraries(WorldAtWarVRAircraftControlLogicTest PRIVATE wawvr_project_options wawvr_xr)
    add_test(NAME mod.aircraft_control_logic COMMAND WorldAtWarVRAircraftControlLogicTest)
    add_executable(
        WorldAtWarVRTankControlLogicTest
        tank_control_logic.cpp
        tank_control_logic.hpp
        tank_control_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRTankControlLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.tank_control_logic
        COMMAND WorldAtWarVRTankControlLogicTest
    )

    add_executable(
        WorldAtWarVRMountedGunLogicTest
        mounted_gun_logic.cpp
        mounted_gun_logic.hpp
        mounted_gun_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRMountedGunLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.mounted_gun_logic
        COMMAND WorldAtWarVRMountedGunLogicTest
    )

    add_executable(
        WorldAtWarVRCampaignUnlockLogicTest
        campaign_unlock_logic.hpp
        campaign_unlock_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRCampaignUnlockLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.campaign_unlock_logic
        COMMAND WorldAtWarVRCampaignUnlockLogicTest
    )

    add_executable(
        WorldAtWarVRTwoHandSightAlignmentPolicyTest
        two_hand_sight_alignment_policy.cpp
        two_hand_sight_alignment_policy.hpp
        two_hand_sight_alignment_policy_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRTwoHandSightAlignmentPolicyTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.two_hand_sight_alignment_policy
        COMMAND WorldAtWarVRTwoHandSightAlignmentPolicyTest
    )

    add_executable(
        WorldAtWarVRSupportAnimationPolicyTest
        support_animation_policy.cpp
        support_animation_policy.hpp
        support_animation_policy_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRSupportAnimationPolicyTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.support_animation_policy
        COMMAND WorldAtWarVRSupportAnimationPolicyTest
    )

    add_executable(
        WorldAtWarVRPistolSupportPoseTest
        pistol_support_pose.cpp
        pistol_support_pose.hpp
        pistol_support_pose_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRPistolSupportPoseTest
        PRIVATE wawvr_project_options
    )
    target_include_directories(
        WorldAtWarVRPistolSupportPoseTest
        PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
    )
    add_test(
        NAME mod.pistol_support_pose
        COMMAND WorldAtWarVRPistolSupportPoseTest
    )

    add_executable(
        WorldAtWarVRPistolHandSkinningTest
        pistol_hand_skinning.cpp
        pistol_hand_skinning.hpp
        pistol_hand_skinning_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRPistolHandSkinningTest
        PRIVATE wawvr_project_options
    )
    target_include_directories(
        WorldAtWarVRPistolHandSkinningTest
        PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
    )
    add_test(
        NAME mod.pistol_hand_skinning
        COMMAND WorldAtWarVRPistolHandSkinningTest
    )

    add_executable(
        WorldAtWarVRAutomaticReloadSettingTest
        automatic_reload_setting.cpp
        automatic_reload_setting.hpp
        automatic_reload_setting_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRAutomaticReloadSettingTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.automatic_reload_setting
        COMMAND WorldAtWarVRAutomaticReloadSettingTest
    )

    add_executable(
        WorldAtWarVRButtonGrenadeLogicTest
        button_grenade_logic.cpp
        button_grenade_logic.hpp
        button_grenade_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRButtonGrenadeLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.button_grenade_logic
        COMMAND WorldAtWarVRButtonGrenadeLogicTest
    )

    add_executable(WorldAtWarVRFiringHapticLogicTest
        firing_haptic_logic.cpp firing_haptic_logic.hpp firing_haptic_logic_test.cpp)
    target_link_libraries(WorldAtWarVRFiringHapticLogicTest PRIVATE wawvr_project_options)
    add_test(NAME mod.firing_haptic_logic COMMAND WorldAtWarVRFiringHapticLogicTest)

    add_executable(
        WorldAtWarVRPerformanceTimingLogicTest
        performance_timing_logic.hpp
        performance_timing_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRPerformanceTimingLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.performance_timing_logic
        COMMAND WorldAtWarVRPerformanceTimingLogicTest
    )

    add_executable(WorldAtWarVRDllSmokeTest dll_smoke_test.cpp)
    target_link_libraries(WorldAtWarVRDllSmokeTest PRIVATE wawvr_project_options)
    add_dependencies(WorldAtWarVRDllSmokeTest WorldAtWarVR)

    add_test(
        NAME mod.dll_smoke
        COMMAND WorldAtWarVRDllSmokeTest $<TARGET_FILE:WorldAtWarVR>
    )

    add_executable(
        WorldAtWarVRDirectBootPatchTest
        direct_boot_patch.cpp
        direct_boot_patch.hpp
        direct_boot_patch_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRDirectBootPatchTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.direct_boot_patch_logic
        COMMAND WorldAtWarVRDirectBootPatchTest
    )

    add_executable(
        WorldAtWarVROversizedResolutionPatchTest
        oversized_resolution_patch.cpp
        oversized_resolution_patch.hpp
        oversized_resolution_patch_test.cpp
    )
    target_link_libraries(
        WorldAtWarVROversizedResolutionPatchTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.oversized_resolution_patch_logic
        COMMAND WorldAtWarVROversizedResolutionPatchTest
    )

    add_executable(
        WorldAtWarVRD3D9ExBootstrapTest
        d3d9ex_bootstrap.cpp
        d3d9ex_bootstrap.hpp
        d3d9ex_bootstrap_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRD3D9ExBootstrapTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.d3d9ex_bootstrap_logic
        COMMAND WorldAtWarVRD3D9ExBootstrapTest
    )

    add_executable(
        WorldAtWarVRLodFovClampLogicTest
        lod_fov_clamp_logic.hpp
        lod_fov_clamp_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRLodFovClampLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.lod_fov_clamp_logic
        COMMAND WorldAtWarVRLodFovClampLogicTest
    )

    if(TARGET wawvr_t4)
        add_executable(
            WorldAtWarVRT4LayoutSelectorTest
            t4_layout_selector.hpp
            t4_layout_selector_test.cpp
        )
        target_link_libraries(
            WorldAtWarVRT4LayoutSelectorTest
            PRIVATE wawvr_project_options wawvr_t4
        )
        add_test(
            NAME mod.t4_layout_selector
            COMMAND WorldAtWarVRT4LayoutSelectorTest
        )
    endif()

    add_executable(
        WorldAtWarVRMenuButtonGestureTest
        menu_button_gesture.cpp
        menu_button_gesture.hpp
        menu_button_gesture_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRMenuButtonGestureTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.menu_button_gesture
        COMMAND WorldAtWarVRMenuButtonGestureTest
    )

    add_executable(
        WorldAtWarVRMenuInputLogicTest
        menu_input_logic.cpp
        menu_input_logic.hpp
        menu_input_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRMenuInputLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.menu_input_logic
        COMMAND WorldAtWarVRMenuInputLogicTest
    )

    add_executable(
        WorldAtWarVRNativeGameplayCommandLogicTest
        native_gameplay_command_logic.cpp
        native_gameplay_command_logic.hpp
        native_gameplay_command_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRNativeGameplayCommandLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.native_gameplay_command_logic
        COMMAND WorldAtWarVRNativeGameplayCommandLogicTest
    )

    add_executable(
        WorldAtWarVRSimulatorMapHandoffLogicTest
        simulator_map_handoff_logic.hpp
        simulator_map_handoff_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRSimulatorMapHandoffLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.simulator_map_handoff_logic
        COMMAND WorldAtWarVRSimulatorMapHandoffLogicTest
    )

    add_executable(
        WorldAtWarVRRocketBarrageLogicTest
        rocket_barrage_logic.cpp
        rocket_barrage_logic.hpp
        rocket_barrage_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRRocketBarrageLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.rocket_barrage_logic
        COMMAND WorldAtWarVRRocketBarrageLogicTest
    )

    add_executable(
        WorldAtWarVRBazookaTrailSeedLogicTest
        bazooka_trail_seed_logic.cpp
        bazooka_trail_seed_logic.hpp
        bazooka_trail_seed_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRBazookaTrailSeedLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.bazooka_trail_seed_logic
        COMMAND WorldAtWarVRBazookaTrailSeedLogicTest
    )

    add_executable(
        WorldAtWarVRHudPlacementLogicTest
        hud_placement_logic.cpp
        hud_placement_logic.hpp
        hud_placement_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRHudPlacementLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.hud_placement_logic
        COMMAND WorldAtWarVRHudPlacementLogicTest
    )

    if(TARGET wawvr_xr)
        add_executable(
            WorldAtWarVRCurrentHeadLocalControllerPoseLogicTest
            current_head_local_controller_pose_logic.cpp
            current_head_local_controller_pose_logic.hpp
            current_head_local_controller_pose_logic_test.cpp
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.cpp"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.h"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_types.h"
        )
        target_link_libraries(
            WorldAtWarVRCurrentHeadLocalControllerPoseLogicTest
            PRIVATE wawvr_project_options
        )
        target_include_directories(
            WorldAtWarVRCurrentHeadLocalControllerPoseLogicTest
            PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
        )
        add_test(
            NAME mod.current_head_local_controller_pose_logic
            COMMAND WorldAtWarVRCurrentHeadLocalControllerPoseLogicTest
        )

        add_executable(
            WorldAtWarVRHeadRelativePoseFreezeLogicTest
            head_relative_pose_freeze_logic.cpp
            head_relative_pose_freeze_logic.hpp
            head_relative_pose_freeze_logic_test.cpp
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.cpp"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.h"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_types.h"
        )
        target_link_libraries(
            WorldAtWarVRHeadRelativePoseFreezeLogicTest
            PRIVATE wawvr_project_options
        )
        target_include_directories(
            WorldAtWarVRHeadRelativePoseFreezeLogicTest
            PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
        )
        add_test(
            NAME mod.head_relative_pose_freeze_logic
            COMMAND WorldAtWarVRHeadRelativePoseFreezeLogicTest
        )

        add_executable(
            WorldAtWarVRPostT4AimPhaseLogicTest
            post_t4_aim_phase_logic.cpp
            post_t4_aim_phase_logic.hpp
            post_t4_aim_phase_logic_test.cpp
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.cpp"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.h"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_types.h"
        )
        target_link_libraries(
            WorldAtWarVRPostT4AimPhaseLogicTest
            PRIVATE wawvr_project_options
        )
        target_include_directories(
            WorldAtWarVRPostT4AimPhaseLogicTest
            PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
        )
        add_test(
            NAME mod.post_t4_aim_phase_logic
            COMMAND WorldAtWarVRPostT4AimPhaseLogicTest
        )

        add_executable(
            WorldAtWarVRWeaponPosePipelineTraceLogicTest
            weapon_pose_pipeline_trace_logic.cpp
            weapon_pose_pipeline_trace_logic.hpp
            weapon_pose_pipeline_trace_logic_test.cpp
            weapon_grip_logic.hpp
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.cpp"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.h"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_types.h"
        )
        target_link_libraries(
            WorldAtWarVRWeaponPosePipelineTraceLogicTest
            PRIVATE wawvr_project_options
        )
        target_include_directories(
            WorldAtWarVRWeaponPosePipelineTraceLogicTest
            PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
        )
        add_test(
            NAME mod.weapon_pose_pipeline_trace_logic
            COMMAND WorldAtWarVRWeaponPosePipelineTraceLogicTest
        )

        add_executable(
            WorldAtWarVRWeaponFrameBasePhaseLogicTest
            weapon_frame_base_phase_logic.cpp
            weapon_frame_base_phase_logic.hpp
            weapon_frame_base_phase_logic_test.cpp
            "${CMAKE_SOURCE_DIR}/src/xr/xr_types.h"
        )
        target_link_libraries(
            WorldAtWarVRWeaponFrameBasePhaseLogicTest
            PRIVATE wawvr_project_options
        )
        target_include_directories(
            WorldAtWarVRWeaponFrameBasePhaseLogicTest
            PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
        )
        add_test(
            NAME mod.weapon_frame_base_phase_logic
            COMMAND WorldAtWarVRWeaponFrameBasePhaseLogicTest
        )

        add_executable(
            WorldAtWarVRPhysicalScopeLogicTest
            physical_scope_logic.cpp
            physical_scope_logic.hpp
            physical_scope_logic_test.cpp
        )
        target_link_libraries(
            WorldAtWarVRPhysicalScopeLogicTest
            PRIVATE wawvr_project_options
        )
        target_include_directories(
            WorldAtWarVRPhysicalScopeLogicTest
            PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
        )
        add_test(
            NAME mod.physical_scope_logic
            COMMAND WorldAtWarVRPhysicalScopeLogicTest
        )

        add_executable(
            WorldAtWarVRScopeVisibilitySnapshotTest
            scope_visibility_snapshot.hpp
            scope_visibility_snapshot_test.cpp
        )
        target_link_libraries(
            WorldAtWarVRScopeVisibilitySnapshotTest
            PRIVATE wawvr_project_options
        )
        add_test(
            NAME mod.scope_visibility_snapshot
            COMMAND WorldAtWarVRScopeVisibilitySnapshotTest
        )

        add_executable(
            WorldAtWarVRPhysicalScopeDrawFilterTest
            physical_scope_draw_filter.cpp
            physical_scope_draw_filter.hpp
            physical_scope_draw_filter_test.cpp
        )
        target_link_libraries(
            WorldAtWarVRPhysicalScopeDrawFilterTest
            PRIVATE wawvr_project_options
        )
        add_test(
            NAME mod.physical_scope_draw_filter
            COMMAND WorldAtWarVRPhysicalScopeDrawFilterTest
        )

        add_executable(
            WorldAtWarVRPeerThreadQuiescenceTest
            peer_thread_quiescence.cpp
            peer_thread_quiescence.hpp
            peer_thread_quiescence_test.cpp
        )
        target_link_libraries(
            WorldAtWarVRPeerThreadQuiescenceTest
            PRIVATE wawvr_project_options
        )
        add_test(
            NAME mod.peer_thread_quiescence
            COMMAND WorldAtWarVRPeerThreadQuiescenceTest
        )

        add_executable(
            WorldAtWarVRMenuPanelLogicTest
            menu_panel_logic.cpp
            menu_panel_logic.hpp
            menu_panel_logic_test.cpp
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.cpp"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.h"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_types.h"
        )
        target_link_libraries(
            WorldAtWarVRMenuPanelLogicTest
            PRIVATE wawvr_project_options
        )
        target_include_directories(
            WorldAtWarVRMenuPanelLogicTest
            PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
        )
        add_test(
            NAME mod.menu_panel_logic
            COMMAND WorldAtWarVRMenuPanelLogicTest
        )

        add_executable(
            WorldAtWarVRMenuSurfaceLogicTest
            menu_panel_logic.cpp
            menu_panel_logic.hpp
            menu_surface_logic.cpp
            menu_surface_logic.hpp
            menu_surface_logic_test.cpp
            present_hook_logic.cpp
            present_hook_logic.hpp
            "${CMAKE_SOURCE_DIR}/src/xr/compositor_math.cpp"
            "${CMAKE_SOURCE_DIR}/src/xr/compositor_math.hpp"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.cpp"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.h"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_types.h"
        )
        target_link_libraries(
            WorldAtWarVRMenuSurfaceLogicTest
            PRIVATE wawvr_project_options
        )
        target_include_directories(
            WorldAtWarVRMenuSurfaceLogicTest
            PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
        )
        add_test(
            NAME mod.menu_surface_logic
            COMMAND WorldAtWarVRMenuSurfaceLogicTest
        )

        add_executable(
            WorldAtWarVRCameraComfortLogicTest
            camera_comfort_logic.cpp
            camera_comfort_logic.hpp
            camera_comfort_logic_test.cpp
            "${CMAKE_SOURCE_DIR}/src/xr/xr_types.h"
        )
        target_link_libraries(
            WorldAtWarVRCameraComfortLogicTest
            PRIVATE wawvr_project_options
        )
        target_include_directories(
            WorldAtWarVRCameraComfortLogicTest
            PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
        )
        add_test(
            NAME mod.camera_comfort_logic
            COMMAND WorldAtWarVRCameraComfortLogicTest
        )

        add_executable(
            WorldAtWarVRStereoFrameBrokerTest
            stereo_frame_broker.cpp
            stereo_frame_broker.hpp
            stereo_frame_broker_test.cpp
            stereo_diagnostics.hpp
            "${CMAKE_SOURCE_DIR}/src/xr/xr_types.h"
        )
        target_link_libraries(
            WorldAtWarVRStereoFrameBrokerTest
            PRIVATE wawvr_project_options
        )
        target_include_directories(
            WorldAtWarVRStereoFrameBrokerTest
            PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
        )
        add_test(
            NAME mod.stereo_frame_broker
            COMMAND WorldAtWarVRStereoFrameBrokerTest
        )
    endif()

    if(TARGET wawvr_xr)
        add_executable(
            WorldAtWarVRPresentHookLogicTest
            immutable_thunk_route.hpp
            present_hook_logic.cpp
            present_hook_logic.hpp
            present_hook_logic_test.cpp
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.cpp"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.h"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_types.h"
        )
        target_link_libraries(
            WorldAtWarVRPresentHookLogicTest
            PRIVATE wawvr_project_options
        )
        target_include_directories(
            WorldAtWarVRPresentHookLogicTest
            PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
        )
        add_test(
            NAME mod.present_hook_logic
            COMMAND WorldAtWarVRPresentHookLogicTest
        )

        add_executable(
            WorldAtWarVRStereoSceneMathTest
            camera_comfort_logic.cpp
            camera_comfort_logic.hpp
            head_relative_pose_freeze_logic.cpp
            head_relative_pose_freeze_logic.hpp
            stereo_scene_math.cpp
            stereo_scene_math.hpp
            stereo_scene_math_test.cpp
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.cpp"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_math.h"
            "${CMAKE_SOURCE_DIR}/src/xr/xr_types.h"
        )
        target_link_libraries(
            WorldAtWarVRStereoSceneMathTest
            PRIVATE wawvr_project_options
        )
        target_include_directories(
            WorldAtWarVRStereoSceneMathTest
            PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
        )
        add_test(
            NAME mod.stereo_scene_math
            COMMAND WorldAtWarVRStereoSceneMathTest
        )

        if(TARGET wawvr_t4)
    add_executable(
        WorldAtWarVRInputMappingTest
                camera_comfort_logic.cpp
                camera_comfort_logic.hpp
                controller_state.cpp
                controller_state.hpp
                current_head_local_controller_pose_logic.cpp
                current_head_local_controller_pose_logic.hpp
                input_mapping.cpp
                input_mapping.hpp
                input_mapping_test.cpp
                weapon_placement.cpp
                weapon_placement.hpp
                "${CMAKE_SOURCE_DIR}/src/xr/xr_math.cpp"
                "${CMAKE_SOURCE_DIR}/src/xr/xr_math.h"
                "${CMAKE_SOURCE_DIR}/src/xr/xr_types.h"
            )
            target_link_libraries(
                WorldAtWarVRInputMappingTest
                PRIVATE wawvr_project_options wawvr_t4
            )
            target_include_directories(
                WorldAtWarVRInputMappingTest
                PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
            )
    add_test(
        NAME mod.input_mapping
        COMMAND WorldAtWarVRInputMappingTest
    )

    add_executable(
        WorldAtWarVRManualGrenadeLogicTest
        manual_grenade_logic.cpp
        manual_grenade_logic.hpp
        manual_grenade_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRManualGrenadeLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.manual_grenade_logic
        COMMAND WorldAtWarVRManualGrenadeLogicTest
    )

    add_executable(
        WorldAtWarVRBoltActionWeaponProfileTest
        bolt_action_weapon_profile.cpp
        bolt_action_weapon_profile.hpp
        bolt_action_weapon_profile_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRBoltActionWeaponProfileTest
        PRIVATE wawvr_project_options wawvr_gameplay
    )
    target_include_directories(
        WorldAtWarVRBoltActionWeaponProfileTest
        PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
    )
    add_test(
        NAME mod.bolt_action_weapon_profile
        COMMAND WorldAtWarVRBoltActionWeaponProfileTest
    )

    add_executable(
        WorldAtWarVRDetachableMagazineWeaponProfileTest
        detachable_magazine_weapon_profile.cpp
        detachable_magazine_weapon_profile.hpp
        detachable_magazine_weapon_profile_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRDetachableMagazineWeaponProfileTest
        PRIVATE wawvr_project_options wawvr_gameplay
    )
    target_include_directories(
        WorldAtWarVRDetachableMagazineWeaponProfileTest
        PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
    )
    add_test(
        NAME mod.detachable_magazine_weapon_profile
        COMMAND WorldAtWarVRDetachableMagazineWeaponProfileTest
    )

    add_executable(
        WorldAtWarVRMagazineChamberLockTableTest
        magazine_chamber_lock_table.cpp
        magazine_chamber_lock_table.hpp
        magazine_chamber_lock_table_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRMagazineChamberLockTableTest
        PRIVATE wawvr_project_options wawvr_gameplay
    )
    target_include_directories(
        WorldAtWarVRMagazineChamberLockTableTest
        PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
    )
    add_test(
        NAME mod.magazine_chamber_lock_table
        COMMAND WorldAtWarVRMagazineChamberLockTableTest
    )

    add_executable(
        WorldAtWarVRMagazineChargingLogicTest
        magazine_charging_logic.cpp
        magazine_charging_logic.hpp
        magazine_charging_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRMagazineChargingLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.magazine_charging_logic
        COMMAND WorldAtWarVRMagazineChargingLogicTest
    )

    add_executable(
        WorldAtWarVRSyntheticSceneSlotsTest
        synthetic_scene_slots.cpp
        synthetic_scene_slots.hpp
        synthetic_scene_slots_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRSyntheticSceneSlotsTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.synthetic_scene_slots
        COMMAND WorldAtWarVRSyntheticSceneSlotsTest
    )

    add_executable(
        WorldAtWarVRStereoBackendEyeOrderLogicTest
        stereo_backend_eye_order_logic.hpp
        stereo_backend_eye_order_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRStereoBackendEyeOrderLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.stereo_backend_eye_order_logic
        COMMAND WorldAtWarVRStereoBackendEyeOrderLogicTest
    )

    add_executable(
        WorldAtWarVRStereoEmissiveViewportLogicTest
        stereo_emissive_viewport_logic.hpp
        stereo_emissive_viewport_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRStereoEmissiveViewportLogicTest
        PRIVATE wawvr_project_options
    )
    add_test(
        NAME mod.stereo_emissive_viewport_logic
        COMMAND WorldAtWarVRStereoEmissiveViewportLogicTest
    )

    add_executable(
        WorldAtWarVRManualReloadControllerLogicTest
        bolt_action_weapon_profile.cpp
        bolt_action_weapon_profile.hpp
        kar98_bolt_action_logic.cpp
        kar98_bolt_action_logic.hpp
        manual_reload_controller_logic.cpp
        manual_reload_controller_logic.hpp
        manual_reload_controller_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRManualReloadControllerLogicTest
        PRIVATE wawvr_project_options wawvr_gameplay
    )
    target_include_directories(
        WorldAtWarVRManualReloadControllerLogicTest
        PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
    )
    add_test(
        NAME mod.manual_reload_controller_logic
        COMMAND WorldAtWarVRManualReloadControllerLogicTest
    )

    add_executable(
        WorldAtWarVRKar98BoltActionLogicTest
        bolt_action_weapon_profile.cpp
        bolt_action_weapon_profile.hpp
        kar98_bolt_action_logic.cpp
        kar98_bolt_action_logic.hpp
        kar98_bolt_action_logic_test.cpp
    )
    target_link_libraries(
        WorldAtWarVRKar98BoltActionLogicTest
        PRIVATE wawvr_project_options wawvr_gameplay
    )
    target_include_directories(
        WorldAtWarVRKar98BoltActionLogicTest
        PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
    )
    add_test(
        NAME mod.kar98_bolt_action_logic
        COMMAND WorldAtWarVRKar98BoltActionLogicTest
    )

            add_executable(
                WorldAtWarVRWeaponPlacementTest
                controller_state.cpp
                controller_state.hpp
                current_head_local_controller_pose_logic.cpp
                current_head_local_controller_pose_logic.hpp
                weapon_placement.cpp
                weapon_placement.hpp
                weapon_placement_test.cpp
                "${CMAKE_SOURCE_DIR}/src/xr/xr_math.cpp"
                "${CMAKE_SOURCE_DIR}/src/xr/xr_math.h"
                "${CMAKE_SOURCE_DIR}/src/xr/xr_types.h"
            )
            target_link_libraries(
                WorldAtWarVRWeaponPlacementTest
                PRIVATE wawvr_project_options wawvr_t4
            )
            target_include_directories(
                WorldAtWarVRWeaponPlacementTest
                PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
            )
            add_test(
                NAME mod.weapon_placement
                COMMAND WorldAtWarVRWeaponPlacementTest
            )

            add_executable(
                WorldAtWarVRWeaponGripLogicTest
                weapon_grip_logic.cpp
                weapon_grip_logic.hpp
                weapon_grip_logic_test.cpp
            )
            target_link_libraries(
                WorldAtWarVRWeaponGripLogicTest
                PRIVATE wawvr_project_options
            )
            add_test(
                NAME mod.weapon_grip_logic
                COMMAND WorldAtWarVRWeaponGripLogicTest
            )

            add_executable(
                WorldAtWarVRWeaponBallisticsLogicTest
                weapon_ballistics_logic.cpp
                weapon_ballistics_logic.hpp
                weapon_ballistics_logic_test.cpp
                "${CMAKE_SOURCE_DIR}/src/xr/xr_types.h"
            )
            target_link_libraries(
                WorldAtWarVRWeaponBallisticsLogicTest
                PRIVATE wawvr_project_options
            )
            target_include_directories(
                WorldAtWarVRWeaponBallisticsLogicTest
                PRIVATE "${CMAKE_SOURCE_DIR}/src/xr"
            )
            add_test(
                NAME mod.weapon_ballistics_logic
                COMMAND WorldAtWarVRWeaponBallisticsLogicTest
            )

            add_executable(
                WorldAtWarVRViewmodelFilterTest
                viewmodel_filter.cpp
                viewmodel_filter.hpp
                viewmodel_filter_test.cpp
            )
            target_link_libraries(
                WorldAtWarVRViewmodelFilterTest
                PRIVATE wawvr_project_options
            )
            add_test(
                NAME mod.viewmodel_filter
                COMMAND WorldAtWarVRViewmodelFilterTest
            )
        endif()
    endif()
endif()
