#include "stereo_scene_math.hpp"

#include "camera_comfort_logic.hpp"
#include "head_relative_pose_freeze_logic.hpp"

#include <algorithm>
#include <cmath>

namespace wawvr::mod {
namespace {

// Keep the dedicated optical camera behind the real rear lens while clipping
// the complete first-person rifle/front sight out of its magnified source.
// 48 IW units is about 1.22 m; normal eye cameras retain the stock near clip.
constexpr float kPhysicalScopeNearClipIw = 48.0F;

using wawvr::xr::Basis3f;
using wawvr::xr::EyeView;
using wawvr::xr::NormalizedViewport;
using wawvr::xr::Vec3f;

constexpr float kMaximumFovAngle = 1.55334306f; // 89 degrees
constexpr float kMinimumAxisLengthSquared = 0.64f;
constexpr float kMaximumAxisLengthSquared = 1.44f;
constexpr float kMaximumAxisDot = 0.20f;

bool finite(const float value) noexcept {
    return std::isfinite(value);
}

bool finite(const Vec3f& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

bool finite(const wawvr::xr::Quaternionf& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z) &&
           finite(value.w);
}

float dot(const Vec3f& left, const Vec3f& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

bool valid_axis(const Basis3f& axis) noexcept {
    if (!finite(axis.forward) || !finite(axis.left) || !finite(axis.up)) {
        return false;
    }
    const float forward_length = dot(axis.forward, axis.forward);
    const float left_length = dot(axis.left, axis.left);
    const float up_length = dot(axis.up, axis.up);
    return forward_length >= kMinimumAxisLengthSquared &&
           forward_length <= kMaximumAxisLengthSquared &&
           left_length >= kMinimumAxisLengthSquared &&
           left_length <= kMaximumAxisLengthSquared &&
           up_length >= kMinimumAxisLengthSquared &&
           up_length <= kMaximumAxisLengthSquared &&
           std::fabs(dot(axis.forward, axis.left)) <= kMaximumAxisDot &&
           std::fabs(dot(axis.forward, axis.up)) <= kMaximumAxisDot &&
           std::fabs(dot(axis.left, axis.up)) <= kMaximumAxisDot;
}

bool valid_pose(const wawvr::xr::Posef& pose) noexcept {
    if (!finite(pose.position) || !finite(pose.orientation)) {
        return false;
    }
    const auto& q = pose.orientation;
    const float length_squared =
        q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return length_squared > 0.25f && length_squared < 4.0f;
}

bool projection_for_eye(
    const EyeView& eye,
    float* const symmetric_x,
    float* const symmetric_y,
    NormalizedViewport* const destination) noexcept {
    if (symmetric_x == nullptr || symmetric_y == nullptr ||
        destination == nullptr) {
        return false;
    }
    const auto& fov = eye.fov;
    if (!finite(fov.angle_left) || !finite(fov.angle_right) ||
        !finite(fov.angle_up) || !finite(fov.angle_down) ||
        fov.angle_left >= 0.0f || fov.angle_right <= 0.0f ||
        fov.angle_down >= 0.0f || fov.angle_up <= 0.0f ||
        std::fabs(fov.angle_left) >= kMaximumFovAngle ||
        std::fabs(fov.angle_right) >= kMaximumFovAngle ||
        std::fabs(fov.angle_up) >= kMaximumFovAngle ||
        std::fabs(fov.angle_down) >= kMaximumFovAngle) {
        return false;
    }

    const float tangent_left = std::tan(fov.angle_left);
    const float tangent_right = std::tan(fov.angle_right);
    const float tangent_up = std::tan(fov.angle_up);
    const float tangent_down = std::tan(fov.angle_down);
    const float span_x = tangent_right - tangent_left;
    const float span_y = tangent_up - tangent_down;
    *symmetric_x = std::max(-tangent_left, tangent_right);
    *symmetric_y = std::max(-tangent_down, tangent_up);
    if (!finite(*symmetric_x) || !finite(*symmetric_y) ||
        !finite(span_x) || !finite(span_y) || *symmetric_x <= 0.0f ||
        *symmetric_y <= 0.0f || span_x <= 0.0f || span_y <= 0.0f) {
        return false;
    }

    // The engine renders a centred symmetric projection. Draw it through an
    // oversized and offset destination viewport so the runtime's asymmetric
    // projection sees the correct tangent at every output NDC coordinate.
    const float scale_x = 2.0f * *symmetric_x / span_x;
    const float scale_y = 2.0f * *symmetric_y / span_y;
    const float offset_x = -(tangent_right + tangent_left) / span_x;
    const float offset_y = -(tangent_up + tangent_down) / span_y;
    *destination = {
        (1.0f + offset_x - scale_x) * 0.5f,
        (1.0f - offset_y - scale_y) * 0.5f,
        scale_x,
        scale_y,
    };
    return finite(destination->x) && finite(destination->y) &&
           finite(destination->width) && finite(destination->height) &&
           destination->width > 0.0f && destination->width <= 8.0f &&
           destination->height > 0.0f && destination->height <= 8.0f;
}

Vec3f compose_vector(const Basis3f& base, const Vec3f& local) noexcept {
    return {
        local.x * base.forward.x + local.y * base.left.x +
            local.z * base.up.x,
        local.x * base.forward.y + local.y * base.left.y +
            local.z * base.up.y,
        local.x * base.forward.z + local.y * base.left.z +
            local.z * base.up.z,
    };
}

Basis3f compose_axis(const Basis3f& base, const Basis3f& local) noexcept {
    return {
        compose_vector(base, local.forward),
        compose_vector(base, local.left),
        compose_vector(base, local.up),
    };
}

Vec3f add(const Vec3f& left, const Vec3f& right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

bool valid_stock(const T4SceneView& stock) noexcept {
    return stock.width >= 2 && stock.width <= 16384 && stock.height > 0 &&
           stock.height <= 16384 && stock.x > -16384 && stock.x < 16384 &&
           stock.y > -16384 && stock.y < 16384 &&
           finite(stock.tan_half_fov_x) && stock.tan_half_fov_x > 0.0f &&
           finite(stock.tan_half_fov_y) && stock.tan_half_fov_y > 0.0f &&
           finite(stock.origin) && valid_axis(stock.axis) &&
           finite(stock.near_clip) && stock.near_clip >= 0.0f;
}

} // namespace

bool build_t4_stereo_scene_views(
    const T4SceneView& stock,
    const wawvr::xr::FrameState& frame,
    const wawvr::xr::Posef& tracking_anchor,
    T4StereoSceneViews* const output,
    const float engine_units_per_meter,
    const PhysicalScopeSnapshot* const physical_scope,
    const wawvr::xr::Vec3f* const world_aim_target) noexcept {
    if (output == nullptr) {
        return false;
    }
    *output = {};
    if (!valid_stock(stock) || !frame.should_render || !frame.views_valid ||
        frame.frame_id == 0 || !valid_pose(frame.head_center) ||
        !valid_pose(tracking_anchor) ||
        !finite(engine_units_per_meter) || engine_units_per_meter <= 0.0f ||
        engine_units_per_meter > 1000.0f) {
        return false;
    }

    const auto scope_reserved_width = physical_scope_reserved_width(
        physical_scope != nullptr && physical_scope->active,
        stock.width, stock.height);
    const bool dedicated_scope = scope_reserved_width != 0;
    const std::int32_t stereo_width = stock.width - scope_reserved_width;
    const std::int32_t left_width = stereo_width / 2;
    const std::int32_t right_width = stereo_width - left_width;
    if (left_width <= 0 || right_width <= 0) {
        return false;
    }

    T4StereoSceneViews result{};
    result.frame_id = frame.frame_id;
    const float inverse_width = 1.0F / static_cast<float>(stock.width);
    result.compositor_layout.eyes[0] = {
        0.0F, 0.0F, static_cast<float>(left_width) * inverse_width, 1.0F};
    result.compositor_layout.eyes[1] = {
        static_cast<float>(left_width) * inverse_width, 0.0F,
        static_cast<float>(right_width) * inverse_width, 1.0F};

    if (dedicated_scope) {
        const auto& scope_camera_origin = physical_scope->ballistic_camera_aligned
            ? physical_scope->camera_origin_world : physical_scope->lens_origin_world;
        const auto& scope_camera_axis = physical_scope->ballistic_camera_aligned
            ? physical_scope->camera_axis_world : physical_scope->lens_axis_world;
        constexpr float kPi = 3.14159265358979323846F;
        const float scope_tangent = std::tan(
            physical_scope->zoom_fov_degrees * kPi / 360.0F) * 0.75F;
        if (!finite(scope_tangent) || scope_tangent <= 0.0001F ||
            !finite(physical_scope->lens_origin_world) ||
            !valid_axis(physical_scope->lens_axis_world) ||
            !finite(physical_scope->lens_origin_camera_local) ||
            !valid_axis(physical_scope->lens_axis_camera_local) ||
            !finite(scope_camera_origin) || !valid_axis(scope_camera_axis) ||
            !finite(physical_scope->lens_radius_meters) ||
            physical_scope->lens_radius_meters <= 0.0F) {
            return false;
        }
        result.scope_active = true;
        result.scope = stock;
        result.scope.x = stock.x + stereo_width;
        result.scope.y = stock.y;
        result.scope.width = kPhysicalScopePanelPixels;
        result.scope.height = kPhysicalScopePanelPixels;
        result.scope.tan_half_fov_x = scope_tangent;
        result.scope.tan_half_fov_y = scope_tangent;
        result.scope.origin = scope_camera_origin;
        result.scope.axis = scope_camera_axis;
        result.scope.near_clip =
            std::max(stock.near_clip, kPhysicalScopeNearClipIw);

        auto& composition = result.compositor_layout.physical_scope;
        composition.active = true;
        composition.source = {
            static_cast<float>(stereo_width) * inverse_width,
            0.0F,
            static_cast<float>(kPhysicalScopePanelPixels) * inverse_width,
            static_cast<float>(kPhysicalScopePanelPixels) /
                static_cast<float>(stock.height),
        };
        const auto& local_origin = physical_scope->lens_origin_camera_local;
        const Vec3f lens_origin_anchor_meters = {
            -local_origin.y / engine_units_per_meter,
            local_origin.z / engine_units_per_meter,
            -local_origin.x / engine_units_per_meter,
        };
        const auto& local_axis = physical_scope->lens_axis_camera_local;
        const Vec3f lens_right_anchor = {
            local_axis.left.y, -local_axis.left.z, local_axis.left.x};
        const Vec3f lens_up_anchor = {
            -local_axis.up.y, local_axis.up.z, -local_axis.up.x};
        composition.lens_origin_reference_meters = add(
            tracking_anchor.position,
            wawvr::xr::Rotate(
                tracking_anchor.orientation, lens_origin_anchor_meters));
        composition.lens_right_reference = wawvr::xr::Rotate(
            tracking_anchor.orientation, lens_right_anchor);
        composition.lens_up_reference = wawvr::xr::Rotate(
            tracking_anchor.orientation, lens_up_anchor);
        composition.lens_radius_meters =
            physical_scope->lens_radius_meters;
    }

    Basis3f body_axis{};
    if (!gravity_level_t4_camera_axis(stock.axis, &body_axis)) {
        return false;
    }

    // Invert the same gravity-level game-to-reference transform used for the
    // eye cameras below. Capture this with the source layout, never derive a
    // new target from the later submission HMD pose or centre-screen HUD.
    if (world_aim_target != nullptr && finite(*world_aim_target)) {
        const Vec3f displacement{
            world_aim_target->x - stock.origin.x,
            world_aim_target->y - stock.origin.y,
            world_aim_target->z - stock.origin.z};
        const Vec3f anchor_meters{
            -dot(displacement, body_axis.left) / engine_units_per_meter,
            dot(displacement, body_axis.up) / engine_units_per_meter,
            -dot(displacement, body_axis.forward) / engine_units_per_meter};
        const Vec3f reference = add(tracking_anchor.position,
            wawvr::xr::Rotate(tracking_anchor.orientation, anchor_meters));
        if (finite(reference)) {
            result.compositor_layout.world_aim_marker = {true, reference};
        }
    }

    for (std::uint32_t eye_index = 0;
         eye_index < wawvr::xr::kEyeCount; ++eye_index) {
        if (!valid_pose(frame.eyes[eye_index].pose)) {
            return false;
        }
    }

    // Match COD4 VR's camera construction: render both eyes from one stable
    // head-centre pose, then add a symmetric IPD offset in that common camera
    // basis. The runtime eye poses remain untouched for projection-layer
    // submission; only tiny per-eye orientation/centre noise is kept out of
    // the T4 render cameras.
    wawvr::xr::EnginePose common_head{};
    if (!compose_head_world_pose(
            {stock.origin, body_axis}, frame.head_center, tracking_anchor,
            engine_units_per_meter, &common_head)) {
        return false;
    }
    const Basis3f common_axis = common_head.axis;
    const Vec3f common_origin = common_head.position;
    if (!finite(common_origin) || !valid_axis(common_axis)) {
        return false;
    }

    const Vec3f eye_separation_meters = {
        frame.eyes[0].pose.position.x - frame.eyes[1].pose.position.x,
        frame.eyes[0].pose.position.y - frame.eyes[1].pose.position.y,
        frame.eyes[0].pose.position.z - frame.eyes[1].pose.position.z,
    };
    const float eye_separation_squared =
        dot(eye_separation_meters, eye_separation_meters);
    const float half_ipd_engine_units =
        0.5F * std::sqrt(eye_separation_squared) * engine_units_per_meter;
    if (!finite(eye_separation_squared) || eye_separation_squared < 0.0F ||
        !finite(half_ipd_engine_units)) {
        return false;
    }

    for (std::uint32_t eye_index = 0;
         eye_index < wawvr::xr::kEyeCount; ++eye_index) {
        float symmetric_x = 0.0f;
        float symmetric_y = 0.0f;
        if (!projection_for_eye(
                frame.eyes[eye_index], &symmetric_x, &symmetric_y,
                &result.compositor_layout.destinations[eye_index])) {
            return false;
        }

        T4SceneView& view = result.eyes[eye_index];
        view = stock;
        view.x = eye_index == 0 ? stock.x : stock.x + left_width;
        view.width = eye_index == 0 ? left_width : right_width;
        view.tan_half_fov_x = symmetric_x;
        view.tan_half_fov_y = symmetric_y;
        const float signed_eye_offset =
            eye_index == 0 ? half_ipd_engine_units : -half_ipd_engine_units;
        view.origin = add(
            common_origin,
            {signed_eye_offset * common_axis.left.x,
             signed_eye_offset * common_axis.left.y,
             signed_eye_offset * common_axis.left.z});
        view.axis = common_axis;
        if (!finite(view.origin) || !valid_axis(view.axis)) {
            return false;
        }
    }

    *output = result;
    return true;
}

} // namespace wawvr::mod
