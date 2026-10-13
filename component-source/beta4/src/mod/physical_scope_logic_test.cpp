#include "physical_scope_logic.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

int failures = 0;

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::fprintf(stderr, "physical scope test failed: %s\n", message);
        ++failures;
    }
}

bool near(const float left, const float right) {
    return std::fabs(left - right) < 0.0001F;
}

bool near(const wawvr::xr::Vec3f& left, const wawvr::xr::Vec3f& right) {
    return near(left.x, right.x) && near(left.y, right.y) &&
           near(left.z, right.z);
}

bool near(const wawvr::xr::Basis3f& left, const wawvr::xr::Basis3f& right) {
    return near(left.forward, right.forward) && near(left.left, right.left) &&
           near(left.up, right.up);
}

float dot(const wawvr::xr::Vec3f& left, const wawvr::xr::Vec3f& right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

}  // namespace

int main() {
    using namespace wawvr::mod;
    using wawvr::xr::Basis3f;
    using wawvr::xr::Vec3f;

    check(find_physical_scope_profile("kar98k_scoped") != nullptr &&
              find_physical_scope_profile("kar98k_scoped_zombie") != nullptr &&
              find_physical_scope_profile("springfield_scoped") != nullptr &&
              find_physical_scope_profile("mosin_rifle_scoped") != nullptr &&
              find_physical_scope_profile("type99_rifle_scoped") != nullptr &&
              find_physical_scope_profile("fg42_scoped") != nullptr &&
              find_physical_scope_profile("ptrs41") != nullptr,
          "all seven exact WaW scoped weapon identities resolve");
    check(find_physical_scope_profile("kar98k") == nullptr &&
              find_physical_scope_profile("KAR98K_SCOPED") == nullptr &&
              find_physical_scope_profile("ptrs41_unknown") == nullptr,
          "unscoped and case-changed identities fail closed");

    const auto* const profile =
        find_physical_scope_profile("kar98k_scoped_zombie");
    PhysicalScopeSnapshot snapshot{};
    check(build_physical_scope_snapshot(
              profile, 19, 42, 1000, true, true, true,
              nullptr, nullptr,
              Vec3f{100.0F, 200.0F, 300.0F}, Basis3f{},
              Vec3f{90.0F, 200.0F, 295.0F}, Basis3f{}, &snapshot),
          "a two-hand scoped pose builds");
    check(snapshot.active && !snapshot.anchored_to_model_tag &&
              snapshot.weapon_index == 19 &&
              near(snapshot.lens_origin_world.x, 109.25F) &&
              near(snapshot.lens_origin_world.y, 200.0F) &&
              near(snapshot.lens_origin_world.z, 302.15F) &&
              near(snapshot.lens_origin_camera_local.x, 19.25F) &&
              near(snapshot.lens_origin_camera_local.z, 7.15F),
          "the lens anchor is transformed from the grip into camera-local space");

    const Vec3f exact_scope_tag{103.0F, 201.0F, 304.0F};
    const Vec3f exact_rear_tag_offset{};
    check(build_physical_scope_snapshot(
              profile, 19, 42, 1000, true, true, true, &exact_scope_tag,
              &exact_rear_tag_offset,
              Vec3f{100.0F, 200.0F, 300.0F}, Basis3f{},
              Vec3f{90.0F, 200.0F, 295.0F}, Basis3f{}, &snapshot) &&
              snapshot.anchored_to_model_tag &&
              near(snapshot.lens_origin_world.x, exact_scope_tag.x) &&
              near(snapshot.lens_origin_world.y, exact_scope_tag.y) &&
              near(snapshot.lens_origin_world.z, exact_scope_tag.z),
          "an evaluated model scope tag overrides the grip fallback exactly");
    const Vec3f generic_scope_tag{103.0F, 201.0F, 304.0F};
    check(build_physical_scope_snapshot(
              profile, 19, 42, 1000, true, true, true, &generic_scope_tag,
              &profile->lens_from_scope_tag_weapon_local,
              Vec3f{100.0F, 200.0F, 300.0F}, Basis3f{},
              Vec3f{90.0F, 200.0F, 295.0F}, Basis3f{}, &snapshot) &&
              snapshot.anchored_to_model_tag &&
              near(snapshot.lens_origin_world.x, 95.50F) &&
              near(snapshot.lens_origin_world.y, 201.0F) &&
              near(snapshot.lens_origin_world.z, 305.575F) &&
              near(snapshot.lens_radius_meters, 0.0153F),
          "a generic tag_scope anchor is corrected to the Kar98 rear glass");
    check(!build_physical_scope_snapshot(
              profile, 19, 42, 1000, true, false, true,
              nullptr, nullptr,
              Vec3f{}, Basis3f{}, Vec3f{}, Basis3f{}, &snapshot),
          "right-only holding does not activate the optical view");
    check(!build_physical_scope_snapshot(
              profile, 19, 42, 1000, false, true, false,
              nullptr, nullptr,
              Vec3f{}, Basis3f{}, Vec3f{}, Basis3f{}, &snapshot),
          "a direct left-only pickup does not activate the optical view");
    check(build_physical_scope_snapshot(
              profile, 19, 42, 1000, false, true, true,
              nullptr, nullptr,
              Vec3f{100.0F, 200.0F, 300.0F}, Basis3f{},
              Vec3f{90.0F, 200.0F, 295.0F}, Basis3f{}, &snapshot) &&
              snapshot.active && snapshot.weapon_index == 19,
          "a retained left-hand support pose keeps the optic alive through bolt manipulation");

    const auto* const ptrs = find_physical_scope_profile("ptrs41");
    const Vec3f ptrs_scope_tag{-0.484172F, 0.459957F, 1.370917F};
    check(ptrs != nullptr && ptrs->requires_model_tag &&
              build_physical_scope_snapshot(
                  ptrs, 5, 43, 1001, true, true, true, &ptrs_scope_tag,
                  &ptrs->lens_from_scope_tag_weapon_local,
                  Vec3f{}, Basis3f{}, Vec3f{}, Basis3f{}, &snapshot) &&
              snapshot.anchored_to_model_tag &&
              near(snapshot.lens_origin_world.x, -2.777846F) &&
              near(snapshot.lens_origin_world.y, 0.0F) &&
              near(snapshot.lens_origin_world.z, 3.704640F) &&
              near(snapshot.lens_radius_meters, 0.0171F),
          "the Vendetta PTRS optic is centered on its measured rear glass");
    // Rotating the rifle rotates the correction with it; the asymmetric
    // tag_scope anchor must not produce a lens hovering beside the receiver.
    const Basis3f rotated_axis{
        {0.0F, 1.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}};
    const Vec3f rotated_tag{100.0F - ptrs_scope_tag.y,
                           200.0F + ptrs_scope_tag.x,
                           300.0F + ptrs_scope_tag.z};
    check(build_physical_scope_snapshot(
              ptrs, 5, 44, 1002, false, true, true, &rotated_tag,
              &ptrs->lens_from_scope_tag_weapon_local,
              Vec3f{100.0F, 200.0F, 300.0F}, rotated_axis,
              Vec3f{100.0F, 200.0F, 300.0F}, Basis3f{}, &snapshot) &&
              near(snapshot.lens_origin_world.x, 100.0F) &&
              near(snapshot.lens_origin_world.y, 197.222154F) &&
              near(snapshot.lens_origin_world.z, 303.704640F),
          "PTRS glass remains weapon-local through rotation and support-hand reload");
    check(!build_physical_scope_snapshot(
              ptrs, 5, 45, 1003, true, true, true, nullptr, nullptr,
              Vec3f{}, Basis3f{}, Vec3f{}, Basis3f{}, &snapshot) &&
              !snapshot.active,
          "PTRS fails closed instead of drawing an uncalibrated floating grip lens");

    // The scope camera must share the accepted shot line at every distance,
    // including rotated rifles, without moving the physical rear glass.
    const std::array<Basis3f, 3> projectile_axes{{
        Basis3f{}, rotated_axis,
        {{0.48F, 0.64F, 0.60F}, {-0.80F, 0.60F, 0.0F},
         {-0.36F, -0.48F, 0.80F}},
    }};
    const Vec3f muzzle{100.0F, 200.0F, 300.0F};
    const auto* const mosin = find_physical_scope_profile("mosin_rifle_scoped");
    for (const auto& projectile_axis : projectile_axes) {
        const Vec3f expected_camera{
            muzzle.x - 20.0F * projectile_axis.forward.x,
            muzzle.y - 20.0F * projectile_axis.forward.y,
            muzzle.z - 20.0F * projectile_axis.forward.z,
        };
        const Vec3f lens{
            expected_camera.x + 3.0F * projectile_axis.left.x +
                2.0F * projectile_axis.up.x,
            expected_camera.y + 3.0F * projectile_axis.left.y +
                2.0F * projectile_axis.up.y,
            expected_camera.z + 3.0F * projectile_axis.left.z +
                2.0F * projectile_axis.up.z,
        };
        check(build_physical_scope_snapshot(
                  mosin, 4, 46, 1004, true, true, true, &lens,
                  &exact_rear_tag_offset, Vec3f{}, rotated_axis,
                  Vec3f{90.0F, 190.0F, 290.0F}, Basis3f{}, &snapshot),
              "Mosin alignment fixture builds from the physical rear glass");
        const auto before = snapshot;
        check(!snapshot.ballistic_camera_aligned &&
                  align_physical_scope_camera_to_projectile(
                      muzzle, projectile_axis, &snapshot) &&
                  snapshot.ballistic_camera_aligned &&
                  near(snapshot.camera_origin_world, expected_camera) &&
                  near(snapshot.camera_axis_world, projectile_axis),
              "scope camera uses the nearest point on the accepted shot line");
        check(snapshot.active == before.active &&
                  snapshot.anchored_to_model_tag == before.anchored_to_model_tag &&
                  snapshot.controller_generation == before.controller_generation &&
                  snapshot.publication_milliseconds == before.publication_milliseconds &&
                  snapshot.weapon_index == before.weapon_index &&
                  near(snapshot.lens_origin_world, before.lens_origin_world) &&
                  near(snapshot.lens_axis_world, before.lens_axis_world) &&
                  near(snapshot.lens_origin_camera_local, before.lens_origin_camera_local) &&
                  near(snapshot.lens_axis_camera_local, before.lens_axis_camera_local) &&
                  near(snapshot.zoom_fov_degrees, before.zoom_fov_degrees) &&
                  near(snapshot.lens_radius_meters, before.lens_radius_meters),
              "ballistic camera alignment leaves the physical lens and publication intact");
        for (const float distance : {64.0F, 512.0F, 8192.0F}) {
            const Vec3f camera_to_hit{
                muzzle.x + distance * projectile_axis.forward.x -
                    snapshot.camera_origin_world.x,
                muzzle.y + distance * projectile_axis.forward.y -
                    snapshot.camera_origin_world.y,
                muzzle.z + distance * projectile_axis.forward.z -
                    snapshot.camera_origin_world.z,
            };
            const float depth = dot(camera_to_hit, snapshot.camera_axis_world.forward);
            check(depth > 0.0F &&
                      std::fabs(dot(camera_to_hit, snapshot.camera_axis_world.left) /
                                depth) < 0.000001F &&
                      std::fabs(dot(camera_to_hit, snapshot.camera_axis_world.up) /
                                depth) < 0.000001F,
                  "near and distant hits on a rotated ballistic ray project to optic center");
        }
    }

    const auto rejects_unchanged = [](const Vec3f& origin, const Basis3f& axis,
                                     PhysicalScopeSnapshot candidate) {
        std::array<unsigned char, sizeof(candidate)> before{};
        std::memcpy(before.data(), &candidate, sizeof(candidate));
        return !align_physical_scope_camera_to_projectile(origin, axis, &candidate) &&
               std::memcmp(before.data(), &candidate, sizeof(candidate)) == 0;
    };
    check(!align_physical_scope_camera_to_projectile(muzzle, Basis3f{}, nullptr),
          "a null scope alignment output is rejected");
    auto invalid_scope = snapshot;
    invalid_scope.active = false;
    check(rejects_unchanged(muzzle, projectile_axes.back(), invalid_scope),
          "inactive scopes reject alignment without modifying prior fields");
    const float invalid_float = std::numeric_limits<float>::quiet_NaN();
    invalid_scope = snapshot;
    invalid_scope.lens_origin_world.y = invalid_float;
    check(rejects_unchanged(muzzle, projectile_axes.back(), invalid_scope) &&
              rejects_unchanged({invalid_float, 200.0F, 300.0F},
                                projectile_axes.back(), snapshot) &&
              rejects_unchanged({100.0F, std::numeric_limits<float>::infinity(), 300.0F},
                                projectile_axes.back(), snapshot),
          "nonfinite lens and muzzle positions reject atomically");
    Basis3f invalid_axis{};
    invalid_axis.forward = {};
    check(rejects_unchanged(muzzle, invalid_axis, snapshot),
          "a zero projectile forward is rejected atomically");
    invalid_axis = {};
    invalid_axis.forward.x = 0.99F;
    check(rejects_unchanged(muzzle, invalid_axis, snapshot),
          "a nonunit projectile basis is rejected atomically");
    invalid_axis = {};
    invalid_axis.left.x = 0.01F;
    check(rejects_unchanged(muzzle, invalid_axis, snapshot),
          "a nonorthogonal projectile basis is rejected atomically");
    invalid_axis = {};
    invalid_axis.up.z = -1.0F;
    check(rejects_unchanged(muzzle, invalid_axis, snapshot),
          "a reflected projectile basis is rejected atomically");
    invalid_axis = {};
    invalid_axis.up.z = invalid_float;
    check(rejects_unchanged(muzzle, invalid_axis, snapshot),
          "a nonfinite projectile basis is rejected atomically");
    check(rejects_unchanged({100.0F, 1200.0F, 300.0F},
                            projectile_axes.back(), snapshot),
          "an implausible camera displacement rejects an existing alignment atomically");
    auto boundary_scope = snapshot;
    boundary_scope.lens_origin_world = {80.0F, 216.0F, 300.0F};
    check(align_physical_scope_camera_to_projectile(muzzle, Basis3f{}, &boundary_scope) &&
              near(boundary_scope.camera_origin_world, Vec3f{80.0F, 200.0F, 300.0F}),
          "the 16 IW camera displacement boundary remains accepted");
    boundary_scope.lens_origin_world.y = 216.01F;
    check(rejects_unchanged(muzzle, Basis3f{}, boundary_scope),
          "a camera shift beyond 16 IW rejects atomically");
    boundary_scope.lens_origin_world = {-28.0F, 200.0F, 300.0F};
    check(align_physical_scope_camera_to_projectile(muzzle, Basis3f{}, &boundary_scope) &&
              near(boundary_scope.camera_origin_world, boundary_scope.lens_origin_world),
          "exactly 128 IW lens-to-muzzle separation remains accepted");
    boundary_scope.lens_origin_world = {-27.0F, 208.0F, 300.0F};
    check(align_physical_scope_camera_to_projectile(muzzle, Basis3f{}, &boundary_scope) &&
              near(boundary_scope.camera_origin_world, Vec3f{-27.0F, 200.0F, 300.0F}),
          "a nearby lens under 128 IW permits a bounded lateral camera shift");
    boundary_scope.lens_origin_world = {-28.01F, 200.0F, 300.0F};
    check(rejects_unchanged(muzzle, Basis3f{}, boundary_scope),
          "separation beyond 128 IW rejects atomically even for a collinear lens");
    boundary_scope.lens_origin_world = {-28.0F, 201.0F, 300.0F};
    check(rejects_unchanged(muzzle, Basis3f{}, boundary_scope),
          "the separation guard includes lateral distance instead of only axial distance");

    publish_physical_scope_snapshot({
        .active = true,
        .controller_generation = 42,
        .publication_milliseconds = 1000,
    });
    check(read_fresh_physical_scope_snapshot(1100, &snapshot),
          "a recent scope publication is readable");
    check(!read_fresh_physical_scope_snapshot(1200, &snapshot),
          "a stale scope publication fails closed");
    invalidate_physical_scope_snapshot();
    check(!read_fresh_physical_scope_snapshot(1100, &snapshot),
          "explicit invalidation retires the scope immediately");

    return failures == 0 ? 0 : 1;
}
