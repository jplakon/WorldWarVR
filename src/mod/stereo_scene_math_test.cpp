#include "stereo_scene_math.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

bool near(const float left, const float right, const float tolerance = 1.0e-4f) {
    return std::fabs(left - right) <= tolerance;
}

bool near(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right,
    const float tolerance = 1.0e-4f) {
    return near(left.x, right.x, tolerance) &&
           near(left.y, right.y, tolerance) &&
           near(left.z, right.z, tolerance);
}

bool near(
    const wawvr::xr::Basis3f& left,
    const wawvr::xr::Basis3f& right,
    const float tolerance = 1.0e-4f) {
    return near(left.forward, right.forward, tolerance) &&
           near(left.left, right.left, tolerance) &&
           near(left.up, right.up, tolerance);
}

bool same_view(
    const wawvr::mod::T4SceneView& left,
    const wawvr::mod::T4SceneView& right) {
    return left.x == right.x && left.y == right.y &&
           left.width == right.width && left.height == right.height &&
           near(left.tan_half_fov_x, right.tan_half_fov_x) &&
           near(left.tan_half_fov_y, right.tan_half_fov_y) &&
           near(left.origin, right.origin) && near(left.axis, right.axis) &&
           near(left.near_clip, right.near_clip);
}

bool same_lens(
    const wawvr::xr::PhysicalScopeComposition& left,
    const wawvr::xr::PhysicalScopeComposition& right) {
    return left.active == right.active &&
           near(left.source.x, right.source.x) &&
           near(left.source.y, right.source.y) &&
           near(left.source.width, right.source.width) &&
           near(left.source.height, right.source.height) &&
           near(left.lens_origin_reference_meters, right.lens_origin_reference_meters) &&
           near(left.lens_right_reference, right.lens_right_reference) &&
           near(left.lens_up_reference, right.lens_up_reference) &&
           near(left.lens_radius_meters, right.lens_radius_meters);
}

bool check(const bool condition, const char* const message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
    }
    return condition;
}

wawvr::mod::T4SceneView stock_view() {
    wawvr::mod::T4SceneView stock{};
    stock.x = 10;
    stock.y = 20;
    stock.width = 1001;
    stock.height = 600;
    stock.tan_half_fov_x = 1.0f;
    stock.tan_half_fov_y = 0.75f;
    stock.origin = {100.0f, 200.0f, 300.0f};
    stock.axis = {};
    stock.near_clip = 4.0f;
    return stock;
}

wawvr::xr::FrameState symmetric_frame() {
    wawvr::xr::FrameState frame{};
    frame.frame_id = 42;
    frame.should_render = true;
    frame.views_valid = true;
    frame.head_center.orientation = {};
    frame.eyes[0].pose.orientation = {};
    frame.eyes[1].pose.orientation = {};
    frame.eyes[0].pose.position.x = -0.032f;
    frame.eyes[1].pose.position.x = 0.032f;
    for (auto& eye : frame.eyes) {
        eye.fov.angle_left = -std::atan(1.0f);
        eye.fov.angle_right = std::atan(1.0f);
        eye.fov.angle_down = -std::atan(0.8f);
        eye.fov.angle_up = std::atan(0.8f);
    }
    return frame;
}

} // namespace

int main() {
    using namespace wawvr;
    bool passed = true;
    constexpr float half_sqrt_two = 0.7071067811865475f;

    const mod::T4SceneView stock = stock_view();
    const xr::FrameState frame = symmetric_frame();
    xr::Posef anchor{};
    mod::T4StereoSceneViews stereo{};
    passed &= check(
        mod::build_t4_stereo_scene_views(stock, frame, anchor, &stereo),
        "symmetric stereo view build failed");
    passed &= check(stereo.frame_id == 42, "frame id was not retained");
    passed &= check(
        stereo.eyes[0].x == 10 && stereo.eyes[0].width == 500 &&
            stereo.eyes[1].x == 510 && stereo.eyes[1].width == 501,
        "odd packed viewport was not losslessly split");
    passed &= check(
        near(stereo.eyes[0].origin.y, 200.0f + 0.032f * xr::kIwUnitsPerMeter) &&
            near(stereo.eyes[1].origin.y, 200.0f - 0.032f * xr::kIwUnitsPerMeter),
        "OpenXR right/left eye positions did not map to IW left axis");
    passed &= check(
        near(stereo.compositor_layout.destinations[0].x, 0.0f) &&
            near(stereo.compositor_layout.destinations[0].y, 0.0f) &&
            near(stereo.compositor_layout.destinations[0].width, 1.0f) &&
            near(stereo.compositor_layout.destinations[0].height, 1.0f),
        "symmetric projection should use the complete destination");

    xr::FrameState common_head = frame;
    common_head.frame_id = 46;
    common_head.head_center.position = {0.25F, -0.5F, 0.75F};
    common_head.head_center.orientation =
        {0.0F, half_sqrt_two, 0.0F, half_sqrt_two};
    common_head.eyes[0].pose.position = {0.218F, -0.5F, 0.75F};
    common_head.eyes[1].pose.position = {0.282F, -0.5F, 0.75F};
    common_head.eyes[0].pose.orientation = common_head.head_center.orientation;
    common_head.eyes[1].pose.orientation = common_head.head_center.orientation;
    mod::T4StereoSceneViews common_reference{};
    passed &= check(
        mod::build_t4_stereo_scene_views(
            stock, common_head, anchor, &common_reference),
        "common-head stereo reference build failed");

    xr::FrameState noisy_eyes = common_head;
    noisy_eyes.frame_id = 47;
    noisy_eyes.eyes[0].pose.position = {4.0F, -3.0F, 2.0F};
    noisy_eyes.eyes[1].pose.position = {4.064F, -3.0F, 2.0F};
    noisy_eyes.eyes[0].pose.orientation =
        {half_sqrt_two, 0.0F, 0.0F, half_sqrt_two};
    noisy_eyes.eyes[1].pose.orientation =
        {0.0F, 0.0F, half_sqrt_two, half_sqrt_two};
    mod::T4StereoSceneViews noisy_stereo{};
    passed &= check(
        mod::build_t4_stereo_scene_views(
            stock, noisy_eyes, anchor, &noisy_stereo),
        "per-eye-noise stereo build failed");
    passed &= check(
        near(noisy_stereo.eyes[0].axis, common_reference.eyes[0].axis) &&
            near(noisy_stereo.eyes[1].axis,
                 common_reference.eyes[1].axis) &&
            near(noisy_stereo.eyes[0].origin,
                 common_reference.eyes[0].origin) &&
            near(noisy_stereo.eyes[1].origin,
                 common_reference.eyes[1].origin),
        "raw per-eye orientation or shared-position noise changed the common camera");

    noisy_eyes.frame_id = 48;
    noisy_eyes.eyes[1].pose.position.x = 4.080F;
    passed &= check(
        mod::build_t4_stereo_scene_views(
            stock, noisy_eyes, anchor, &noisy_stereo),
        "noisy-IPD stereo build failed");
    const xr::Vec3f noisy_midpoint = {
        (noisy_stereo.eyes[0].origin.x + noisy_stereo.eyes[1].origin.x) *
            0.5F,
        (noisy_stereo.eyes[0].origin.y + noisy_stereo.eyes[1].origin.y) *
            0.5F,
        (noisy_stereo.eyes[0].origin.z + noisy_stereo.eyes[1].origin.z) *
            0.5F,
    };
    const xr::Vec3f expected_common_origin = {
        stock.origin.x - 0.75F * xr::kIwUnitsPerMeter,
        stock.origin.y - 0.25F * xr::kIwUnitsPerMeter,
        stock.origin.z - 0.5F * xr::kIwUnitsPerMeter,
    };
    passed &= check(
        near(noisy_midpoint, expected_common_origin) &&
            near(noisy_stereo.eyes[0].axis,
                 noisy_stereo.eyes[1].axis),
        "raw per-eye position noise shifted the common head centre or basis");

    mod::T4SceneView scope_stock = stock;
    scope_stock.x = 0;
    scope_stock.y = 0;
    scope_stock.width = mod::kPhysicalScopePackedWidth;
    scope_stock.height = 2688;
    mod::PhysicalScopeSnapshot physical_scope{};
    physical_scope.active = true;
    physical_scope.lens_origin_world = {110.0F, 200.0F, 302.0F};
    physical_scope.lens_axis_world = {};
    physical_scope.lens_origin_camera_local = {10.0F, 0.0F, 2.0F};
    physical_scope.lens_axis_camera_local = {};
    physical_scope.zoom_fov_degrees = 20.0F;
    physical_scope.lens_radius_meters = 0.032F;
    passed &= check(
        mod::build_t4_stereo_scene_views(
            scope_stock, frame, anchor, &stereo, xr::kIwUnitsPerMeter,
            &physical_scope),
        "dedicated physical-scope scene build failed");
    passed &= check(
        stereo.scope_active && stereo.eyes[0].width == 2496 &&
            stereo.eyes[1].x == 2496 && stereo.eyes[1].width == 2496 &&
            stereo.scope.x == 4992 && stereo.scope.width == 1024 &&
            stereo.scope.height == 1024,
        "6016-wide source was not partitioned into two native eyes and one scope panel");
    passed &= check(
        near(stereo.eyes[0].near_clip, scope_stock.near_clip) &&
            near(stereo.eyes[1].near_clip, scope_stock.near_clip) &&
            near(stereo.scope.near_clip, 48.0F),
        "dedicated scope near clip did not hide near-field rifle geometry without changing either eye");
    passed &= check(
        near(stereo.compositor_layout.eyes[0].width, 2496.0F / 6016.0F) &&
            near(stereo.compositor_layout.eyes[1].x, 2496.0F / 6016.0F) &&
            stereo.compositor_layout.physical_scope.active &&
            near(stereo.compositor_layout.physical_scope.source.x,
                 4992.0F / 6016.0F) &&
            near(stereo.compositor_layout.physical_scope.source.width,
                 1024.0F / 6016.0F) &&
            near(stereo.compositor_layout.physical_scope.source.height,
                 1024.0F / 2688.0F),
        "scope and eye source rectangles do not match the COD4 packed layout");
    passed &= check(
        near(stereo.compositor_layout.physical_scope
                 .lens_origin_reference_meters.y,
             2.0F / xr::kIwUnitsPerMeter) &&
            near(stereo.compositor_layout.physical_scope
                 .lens_origin_reference_meters.z,
                 -10.0F / xr::kIwUnitsPerMeter),
        "IW anchor-local lens position was not converted to OpenXR reference space");

    const auto unaligned_stereo = stereo;
    passed &= check(
        near(stereo.scope.origin, physical_scope.lens_origin_world) &&
            near(stereo.scope.axis, physical_scope.lens_axis_world),
        "an unaligned scope no longer uses its established lens camera");
    auto aligned_scope = physical_scope;
    aligned_scope.ballistic_camera_aligned = true;
    aligned_scope.camera_origin_world = {110.0F, 199.0F, 300.0F};
    aligned_scope.camera_axis_world = {
        {0.48F, 0.64F, 0.60F}, {-0.80F, 0.60F, 0.0F},
        {-0.36F, -0.48F, 0.80F},
    };
    passed &= check(
        mod::build_t4_stereo_scene_views(
            scope_stock, frame, anchor, &stereo, xr::kIwUnitsPerMeter,
            &aligned_scope) && stereo.scope_active &&
            near(stereo.scope.origin, aligned_scope.camera_origin_world) &&
            near(stereo.scope.axis, aligned_scope.camera_axis_world),
        "aligned scope camera did not select its accepted ballistic line");
    passed &= check(
        same_view(stereo.eyes[0], unaligned_stereo.eyes[0]) &&
            same_view(stereo.eyes[1], unaligned_stereo.eyes[1]) &&
            same_lens(stereo.compositor_layout.physical_scope,
                      unaligned_stereo.compositor_layout.physical_scope) &&
            stereo.scope.x == unaligned_stereo.scope.x &&
            stereo.scope.y == unaligned_stereo.scope.y &&
            stereo.scope.width == unaligned_stereo.scope.width &&
            stereo.scope.height == unaligned_stereo.scope.height &&
            near(stereo.scope.near_clip, unaligned_stereo.scope.near_clip) &&
            near(stereo.scope.tan_half_fov_x, unaligned_stereo.scope.tan_half_fov_x) &&
            near(stereo.scope.tan_half_fov_y, unaligned_stereo.scope.tan_half_fov_y),
        "ballistic optical alignment moved an eye, the physical glass, or scope projection");

    const float invalid_camera_float = std::numeric_limits<float>::quiet_NaN();
    auto invalid_aligned_scope = aligned_scope;
    invalid_aligned_scope.camera_origin_world.x = invalid_camera_float;
    passed &= check(
        !mod::build_t4_stereo_scene_views(
            scope_stock, frame, anchor, &stereo, xr::kIwUnitsPerMeter,
            &invalid_aligned_scope),
        "nonfinite aligned scope camera origin was accepted");
    invalid_aligned_scope = aligned_scope;
    invalid_aligned_scope.camera_axis_world.forward = {};
    passed &= check(
        !mod::build_t4_stereo_scene_views(
            scope_stock, frame, anchor, &stereo, xr::kIwUnitsPerMeter,
            &invalid_aligned_scope),
        "invalid aligned scope camera axis was accepted");
    invalid_aligned_scope = aligned_scope;
    invalid_aligned_scope.camera_axis_world.up.z = invalid_camera_float;
    passed &= check(
        !mod::build_t4_stereo_scene_views(
            scope_stock, frame, anchor, &stereo, xr::kIwUnitsPerMeter,
            &invalid_aligned_scope),
        "nonfinite aligned scope camera axis was accepted");
    invalid_aligned_scope.ballistic_camera_aligned = false;
    invalid_aligned_scope.camera_origin_world.x = invalid_camera_float;
    passed &= check(
        mod::build_t4_stereo_scene_views(
            scope_stock, frame, anchor, &stereo, xr::kIwUnitsPerMeter,
            &invalid_aligned_scope) &&
            same_view(stereo.scope, unaligned_stereo.scope),
        "inactive optional ballistic fields damaged the established lens fallback");

    xr::Posef moved_anchor{};
    moved_anchor.position = {1.0F, 2.0F, 3.0F};
    moved_anchor.orientation =
        {0.0F, half_sqrt_two, 0.0F, half_sqrt_two};
    xr::FrameState independently_moved_head = frame;
    independently_moved_head.frame_id = 45;
    independently_moved_head.head_center.position = {8.0F, 9.0F, 10.0F};
    independently_moved_head.head_center.orientation =
        {half_sqrt_two, 0.0F, 0.0F, half_sqrt_two};
    passed &= check(
        mod::build_t4_stereo_scene_views(
            scope_stock, independently_moved_head, moved_anchor, &stereo,
            xr::kIwUnitsPerMeter, &physical_scope),
        "nonidentity anchor physical-scope scene build failed");
    const auto& reference_scope =
        stereo.compositor_layout.physical_scope;
    passed &= check(
        near(reference_scope.lens_origin_reference_meters.x,
             1.0F - 10.0F / xr::kIwUnitsPerMeter) &&
            near(reference_scope.lens_origin_reference_meters.y,
                 2.0F + 2.0F / xr::kIwUnitsPerMeter) &&
            near(reference_scope.lens_origin_reference_meters.z, 3.0F) &&
            near(reference_scope.lens_right_reference.x, 0.0F) &&
            near(reference_scope.lens_right_reference.z, -1.0F),
        "lens was composed with the moving head instead of the frozen tracking anchor");

    const auto moved_unaligned_stereo = stereo;
    passed &= check(
        mod::build_t4_stereo_scene_views(
            scope_stock, independently_moved_head, moved_anchor, &stereo,
            xr::kIwUnitsPerMeter, &aligned_scope) &&
            near(stereo.scope.origin, aligned_scope.camera_origin_world) &&
            near(stereo.scope.axis, aligned_scope.camera_axis_world) &&
            same_view(stereo.eyes[0], moved_unaligned_stereo.eyes[0]) &&
            same_view(stereo.eyes[1], moved_unaligned_stereo.eyes[1]) &&
            same_lens(stereo.compositor_layout.physical_scope,
                      moved_unaligned_stereo.compositor_layout.physical_scope),
        "head and tracking-anchor motion changed ballistic alignment or physical lens placement");

    xr::FrameState asymmetric = frame;
    asymmetric.frame_id = 43;
    asymmetric.eyes[0].fov.angle_left = -std::atan(1.2f);
    asymmetric.eyes[0].fov.angle_right = std::atan(0.8f);
    asymmetric.eyes[0].fov.angle_down = -std::atan(0.7f);
    asymmetric.eyes[0].fov.angle_up = std::atan(0.9f);
    passed &= check(
        mod::build_t4_stereo_scene_views(stock, asymmetric, anchor, &stereo),
        "asymmetric stereo view build failed");
    passed &= check(
        near(stereo.eyes[0].tan_half_fov_x, 1.2f) &&
            near(stereo.eyes[0].tan_half_fov_y, 0.9f),
        "engine did not receive centred max-tangent FOV");
    passed &= check(
        near(stereo.compositor_layout.destinations[0].x, 0.0f) &&
            near(stereo.compositor_layout.destinations[0].width, 1.2f) &&
            near(stereo.compositor_layout.destinations[0].y, 0.0f) &&
            near(stereo.compositor_layout.destinations[0].height, 1.125f),
        "asymmetric FOV destination remap was incorrect");

    // A 90-degree OpenXR yaw composed into an identity game camera should
    // rotate IW forward onto IW left.
    xr::FrameState turned = frame;
    turned.frame_id = 44;
    turned.head_center.orientation =
        {0.0f, half_sqrt_two, 0.0f, half_sqrt_two};
    passed &= check(
        mod::build_t4_stereo_scene_views(stock, turned, anchor, &stereo),
        "head-turned stereo view build failed");
    passed &= check(
        near(stereo.eyes[0].axis.forward.x, 0.0f) &&
            near(stereo.eyes[0].axis.forward.y, 1.0f),
        "head orientation was not composed into the stock camera");

    mod::T4SceneView animated = stock;
    animated.axis.forward = {0.8660254f, 0.0f, -0.5f};
    animated.axis.left = {0.25f, 0.8660254f, 0.4330127f};
    animated.axis.up = {0.4330127f, -0.5f, 0.75f};
    passed &= check(
        mod::build_t4_stereo_scene_views(animated, frame, anchor, &stereo),
        "animated stock camera could not build a comfort-stable view");
    passed &= check(
        near(stereo.eyes[0].axis.forward.z, 0.0f) &&
            near(stereo.eyes[0].axis.left.z, 0.0f) &&
            near(stereo.eyes[0].axis.up.z, 1.0f),
        "stock melee/recoil pitch and roll leaked into the HMD camera");

    // World cannon target is captured independently of the head and follows
    // the same game/reference coordinate mapping as the rendered scene.
    const xr::Vec3f aim_world{stock.origin.x + 10.0F * xr::kIwUnitsPerMeter,
                             stock.origin.y - xr::kIwUnitsPerMeter,
                             stock.origin.z + 2.0F * xr::kIwUnitsPerMeter};
    passed &= check(mod::build_t4_stereo_scene_views(
        stock, frame, anchor, &stereo, xr::kIwUnitsPerMeter, nullptr, &aim_world),
        "world aim target scene failed");
    passed &= check(stereo.compositor_layout.world_aim_marker.active &&
        near(stereo.compositor_layout.world_aim_marker.origin_reference_meters,
             {1.0F, 2.0F, -10.0F}), "world target conversion disagrees with eye cameras");
    passed &= check(mod::build_t4_stereo_scene_views(
        stock, independently_moved_head, moved_anchor, &stereo,
        xr::kIwUnitsPerMeter, nullptr, &aim_world), "moved head world marker scene failed");
    passed &= check(near(stereo.compositor_layout.world_aim_marker.origin_reference_meters,
        {-9.0F, 4.0F, 2.0F}), "world target incorrectly follows current head pose");
    const xr::Vec3f bad_aim{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    passed &= check(mod::build_t4_stereo_scene_views(
        stock, frame, anchor, &stereo, xr::kIwUnitsPerMeter, nullptr, &bad_aim) &&
        !stereo.compositor_layout.world_aim_marker.active,
        "invalid optional target damages stereo or creates a marker");
    passed &= check(mod::build_t4_stereo_scene_views(stock, frame, anchor, &stereo) &&
        !stereo.compositor_layout.world_aim_marker.active,
        "leaving a tank retains a stale world marker");

    xr::FrameState invalid = frame;
    invalid.views_valid = false;
    passed &= check(
        !mod::build_t4_stereo_scene_views(stock, invalid, anchor, &stereo),
        "invalid OpenXR views did not fail closed");
    mod::T4SceneView bad_stock = stock;
    bad_stock.width = 1;
    passed &= check(
        !mod::build_t4_stereo_scene_views(bad_stock, frame, anchor, &stereo),
        "one-pixel stock viewport did not fail closed");

    return passed ? 0 : 1;
}
