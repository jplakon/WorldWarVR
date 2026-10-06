#include "present_hook_logic.hpp"

#include "comfort_panel_constants.hpp"
#include "xr_math.h"

#include <cmath>
#include <limits>

namespace wawvr::mod {

PresentTargetMonitorAction plan_present_target_monitor(
    const bool target_installed,
    const PresentTargetIdentity installed_target,
    const bool candidate_validated,
    const PresentTargetIdentity candidate_target) noexcept {
    const bool candidate_usable =
        candidate_validated &&
        candidate_target.device_id != 0 &&
        candidate_target.swap_chain_id != 0;
    if (!target_installed) {
        return candidate_usable
            ? PresentTargetMonitorAction::install_target
            : PresentTargetMonitorAction::wait_for_target;
    }
    if (!candidate_usable) {
        return PresentTargetMonitorAction::keep_target;
    }
    if (candidate_target.device_id == installed_target.device_id &&
        candidate_target.swap_chain_id == installed_target.swap_chain_id) {
        return PresentTargetMonitorAction::keep_target;
    }
    return PresentTargetMonitorAction::rebind_target;
}

bool present_target_still_current(
    const PresentTargetIdentity expected_target,
    const bool current_target_validated,
    const PresentTargetIdentity current_target) noexcept {
    return current_target_validated &&
           expected_target.device_id != 0 &&
           expected_target.swap_chain_id != 0 &&
           current_target.device_id == expected_target.device_id &&
           current_target.swap_chain_id == expected_target.swap_chain_id;
}

bool build_hook_slot_plan(
    void* const current,
    void* const replacement,
    HookSlotPlan* const plan) noexcept {
    if (current == nullptr || replacement == nullptr || plan == nullptr ||
        current == replacement) {
        return false;
    }
    *plan = {current, replacement};
    return true;
}

ProjectionRecoveryPlan plan_projection_recovery(
    const bool current_projection_rendered,
    const bool previous_projection_available,
    const bool d3d9_device_lost) noexcept {
    if (current_projection_rendered) {
        return {ProjectionSubmission::current, false};
    }
    if (previous_projection_available) {
        return {ProjectionSubmission::previous, false};
    }
    return {
        ProjectionSubmission::none,
        d3d9_device_lost,
    };
}

wawvr::xr::CompositionLayerKind select_composition_layer_kind(
    const ProjectionSubmission submission,
    const bool current_quad_valid,
    const bool previous_submission_was_quad,
    const bool mono_presentation,
    const bool use_current_mono_projection_fallback) noexcept {
    if (submission == ProjectionSubmission::none) {
        return wawvr::xr::CompositionLayerKind::none;
    }
    const bool selected_layer_is_quad =
        submission == ProjectionSubmission::current
            ? current_quad_valid
            : previous_submission_was_quad;
    if (selected_layer_is_quad) {
        if (submission == ProjectionSubmission::current &&
            mono_presentation && use_current_mono_projection_fallback) {
            return wawvr::xr::CompositionLayerKind::projection;
        }
        return wawvr::xr::CompositionLayerKind::quad;
    }
    if (submission == ProjectionSubmission::previous &&
        mono_presentation && use_current_mono_projection_fallback) {
        // Stereo/mono transitions retire the prior interval before selection.
        // Therefore a non-quad previous layer inside this exact fallback can
        // only be the already accepted pause projection, not stale gameplay.
        return wawvr::xr::CompositionLayerKind::projection;
    }
    return mono_presentation
        ? wawvr::xr::CompositionLayerKind::none
        : wawvr::xr::CompositionLayerKind::projection;
}

bool should_use_virtual_desktop_ui_projection_fallback(
    const PresentationMode mode,
    const std::string_view active_runtime_name,
    const bool explicitly_disabled) noexcept {
    return !explicitly_disabled &&
           mode == PresentationMode::active_ui_mono &&
           active_runtime_name == "VirtualDesktopXR";
}

bool reusable_layer_intact_after_render_attempt(
    const bool current_layer_rendered,
    const std::uint32_t released_eye_mask) noexcept {
    return current_layer_rendered || released_eye_mask == 0;
}

bool reusable_layer_intact_after_frame_end(
    const bool current_layer_rendered,
    const bool frame_end_succeeded) noexcept {
    return !current_layer_rendered || frame_end_succeeded;
}

PostComFramePlan plan_post_com_frame_service(
    const bool present_seen,
    const bool present_succeeded,
    const bool d3d9_device_cooperative,
    const bool reusable_projection_available,
    const bool reset_in_progress) noexcept {
    if (reset_in_progress) {
        return {};
    }
    return {
        .allow_fresh_projection =
            present_seen && present_succeeded && d3d9_device_cooperative,
        // OpenXR pacing is independent of the lost legacy D3D9 device. Once
        // one projection has been released, keep the runtime out of loading
        // by beginning the next frame; the next boundary will resubmit it.
        .prime_next_frame =
            d3d9_device_cooperative || reusable_projection_available,
    };
}

VtableOwnership classify_hook_slot_ownership(
    const void* const current,
    const void* const original,
    const void* const replacement) noexcept {
    if (current == replacement && replacement != nullptr) {
        return VtableOwnership::installed_by_us;
    }
    if (current == original && original != nullptr) {
        return VtableOwnership::original;
    }
    return VtableOwnership::foreign;
}

wawvr::xr::StereoSourceLayout duplicate_mono_layout() noexcept {
    wawvr::xr::StereoSourceLayout layout{};
    for (auto& eye : layout.eyes) {
        eye = {0.0f, 0.0f, 1.0f, 1.0f};
    }
    layout.preserve_source_aspect = true;
    return layout;
}

bool should_enable_eye_local_bloom(
    const PresentationMode mode,
    const bool explicitly_requested) noexcept {
    return mode == PresentationMode::stereo && explicitly_requested;
}

bool presentation_transition_resets_comfort_anchor(
    const bool previous_mode_valid,
    const PresentationMode previous_mode,
    const PresentationMode current_mode) noexcept {
    if (!previous_mode_valid) {
        return true;
    }
    const bool previous_mono = previous_mode != PresentationMode::stereo;
    const bool current_mono = current_mode != PresentationMode::stereo;
    return previous_mono != current_mono;
}

bool presentation_modes_share_capture_class(
    const PresentationMode first,
    const PresentationMode second) noexcept {
    return (first == PresentationMode::stereo) ==
           (second == PresentationMode::stereo);
}

CaptureSubmissionPhaseObservation observe_capture_submission_phase(
    const std::uint64_t live_frame_id,
    const std::int64_t live_predicted_display_time,
    const std::uint64_t source_frame_id,
    const std::int64_t source_predicted_display_time,
    const bool weapon_receipt_valid,
    const std::uint64_t weapon_receipt_frame_id,
    const std::uint64_t weapon_receipt_action_sequence,
    const std::uint64_t source_action_sequence) noexcept {
    CaptureSubmissionPhaseObservation observation{};
    if (live_frame_id == 0 || source_frame_id == 0 ||
        live_predicted_display_time <= 0 ||
        source_predicted_display_time <= 0) {
        return observation;
    }

    const auto signed_frame_difference = [](
        const std::uint64_t left,
        const std::uint64_t right) noexcept {
        constexpr std::uint64_t kMaximum =
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::int64_t>::max)());
        if (left >= right) {
            const std::uint64_t delta = left - right;
            return delta > kMaximum
                ? (std::numeric_limits<std::int64_t>::max)()
                : static_cast<std::int64_t>(delta);
        }
        const std::uint64_t delta = right - left;
        return delta > kMaximum
            ? (std::numeric_limits<std::int64_t>::min)()
            : -static_cast<std::int64_t>(delta);
    };
    const auto signed_time_difference = [](
        const std::int64_t left,
        const std::int64_t right) noexcept {
        const long double delta = static_cast<long double>(left) -
            static_cast<long double>(right);
        const long double minimum = static_cast<long double>(
            (std::numeric_limits<std::int64_t>::min)());
        const long double maximum = static_cast<long double>(
            (std::numeric_limits<std::int64_t>::max)());
        return delta <= minimum
            ? (std::numeric_limits<std::int64_t>::min)()
            : delta >= maximum
                ? (std::numeric_limits<std::int64_t>::max)()
                : static_cast<std::int64_t>(delta);
    };

    observation.valid = true;
    observation.live_minus_source_frame_id =
        signed_frame_difference(live_frame_id, source_frame_id);
    observation.live_minus_source_predicted_time =
        signed_time_difference(
            live_predicted_display_time, source_predicted_display_time);
    observation.source_is_live_frame = live_frame_id == source_frame_id;
    observation.weapon_receipt_matches_source = weapon_receipt_valid &&
        weapon_receipt_frame_id == source_frame_id &&
        weapon_receipt_action_sequence == source_action_sequence;
    return observation;
}

bool should_pack_stereo_scene(
    const bool multiplayer_profile,
    const bool state_valid,
    const std::int32_t connection_state,
    const std::int32_t active_connection_state,
    const std::uint32_t key_catchers) noexcept {
    if (!multiplayer_profile) {
        return true;
    }
    return state_valid &&
           connection_state == active_connection_state &&
           key_catchers == 0;
}

PresentationMode classify_presentation_mode(
    const bool state_valid,
    const std::int32_t connection_state,
    const std::uint32_t key_catchers,
    const bool stereo_scene_confirmed,
    const std::int32_t active_connection_state) noexcept {
    if (!state_valid) {
        return stereo_scene_confirmed
                   ? PresentationMode::stereo
                   : PresentationMode::full_frame_mono;
    }
    if (connection_state != active_connection_state) {
        return PresentationMode::full_frame_mono;
    }
    if ((key_catchers & kT4UiKeyCatcher) != 0) {
        return PresentationMode::active_ui_mono;
    }
    if ((key_catchers & kT4ConsoleKeyCatcher) != 0) {
        return PresentationMode::active_console_mono;
    }
    if (key_catchers != 0) {
        return PresentationMode::full_frame_mono;
    }
    return PresentationMode::stereo;
}

wawvr::xr::StereoSourceLayout presentation_layout(
    const PresentationMode mode,
    const ActiveUiMonoSource active_ui_source,
    const wawvr::xr::StereoSourceLayout* const staged_stereo_layout) noexcept {
    if (mode == PresentationMode::stereo) {
        return {};
    }
    wawvr::xr::StereoSourceLayout layout = duplicate_mono_layout();
    if (mode != PresentationMode::active_ui_mono ||
        active_ui_source == ActiveUiMonoSource::full_frame) {
        return layout;
    }
    wawvr::xr::NormalizedRect source =
        active_ui_source == ActiveUiMonoSource::right_eye
            ? wawvr::xr::NormalizedRect{0.5F, 0.0F, 0.5F, 1.0F}
            : wawvr::xr::NormalizedRect{0.0F, 0.0F, 0.5F, 1.0F};
    if (staged_stereo_layout != nullptr) {
        const std::size_t source_eye =
            active_ui_source == ActiveUiMonoSource::right_eye ? 1U : 0U;
        const auto& staged_source = staged_stereo_layout->eyes[source_eye];
        if (std::isfinite(staged_source.x) &&
            std::isfinite(staged_source.y) &&
            std::isfinite(staged_source.width) &&
            std::isfinite(staged_source.height) && staged_source.x >= 0.0F &&
            staged_source.y >= 0.0F && staged_source.width > 0.0F &&
            staged_source.height > 0.0F &&
            staged_source.x + staged_source.width <= 1.0F &&
            staged_source.y + staged_source.height <= 1.0F) {
            source = staged_source;
        }
    }
    for (auto& eye : layout.eyes) {
        eye = source;
    }
    for (float& multiplier : layout.source_aspect_multipliers) {
        multiplier = 2.0F;
    }
    return layout;
}

std::array<wawvr::xr::EyeView, wawvr::xr::kEyeCount>
monoscopic_comfort_views(
    const wawvr::xr::Posef& center_pose) noexcept {
    std::array<wawvr::xr::EyeView, wawvr::xr::kEyeCount> views{};
    for (auto& view : views) {
        view.pose = center_pose;
        view.fov = menu_panel_comfort_fov();
    }
    return views;
}

wawvr::xr::Posef leveled_tracking_anchor(
    const wawvr::xr::Posef& head_center,
    const wawvr::xr::Quaternionf* const fallback_yaw) noexcept {
    wawvr::xr::Posef anchor = head_center;
    const auto try_yaw = [](
        const wawvr::xr::Quaternionf& orientation,
        const float minimum_horizontal_squared,
        float* const yaw) noexcept {
        const float length_squared =
            orientation.x * orientation.x +
            orientation.y * orientation.y +
            orientation.z * orientation.z +
            orientation.w * orientation.w;
        if (yaw == nullptr || !std::isfinite(length_squared) ||
            length_squared <= 1.0e-8F) {
            return false;
        }
        const wawvr::xr::Vec3f forward = wawvr::xr::Rotate(
            wawvr::xr::Normalize(orientation),
            {0.0F, 0.0F, -1.0F});
        const float horizontal_squared =
            forward.x * forward.x + forward.z * forward.z;
        if (!std::isfinite(horizontal_squared) ||
            horizontal_squared <= minimum_horizontal_squared) {
            return false;
        }
        *yaw = std::atan2(-forward.x, -forward.z);
        return std::isfinite(*yaw);
    };

    float yaw = 0.0F;
    // Within roughly 5.7 degrees of vertical, the projected heading is too
    // noise-sensitive to redefine facing. Preserve the prior level yaw.
    if (!try_yaw(head_center.orientation, 0.01F, &yaw) &&
        (fallback_yaw == nullptr ||
         !try_yaw(*fallback_yaw, 1.0e-8F, &yaw))) {
        anchor.orientation = {};
        return anchor;
    }
    const float half_yaw = yaw * 0.5F;
    anchor.orientation = {
        0.0F,
        std::sin(half_yaw),
        0.0F,
        std::cos(half_yaw),
    };
    return anchor;
}

} // namespace wawvr::mod
