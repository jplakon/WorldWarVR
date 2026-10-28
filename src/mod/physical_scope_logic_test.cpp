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

    // Exercise the same bounded optical-camera policy for every exact profile,
    // not just the original Mosin caller. These fixtures deliberately yaw,
    // pitch, and roll the gun away from headset-forward. The physical glass,
    // its active three-view lifetime, and publication identity must not move
    // when the camera is placed on this render pass's accepted projectile ray.
    struct ScopeKey final { const char* weapon; const char* model; };
    constexpr std::array<ScopeKey, 12> all_scope_names{{
        {"kar98k_scoped", "viewmodel_ger_kar98_scoped_rifle"},
        {"kar98k_scoped_zombie", "viewmodel_ger_kar98_scoped_rifle"},
        {"springfield_scoped", "viewmodel_usa_springfield_rifle_scoped"},
        {"mosin_rifle_scoped", "viewmodel_rus_mosinnagant_scoped_rifle"},
        {"type99_rifle_scoped", "viewmodel_jap_type99_rifle_scoped"},
        {"fg42_scoped", "viewmodel_ger_fg42_scoped_lmg"},
        {"ptrs41", "viewmodel_mp_ptrs41"},
        {"mosin_rifle_scoped_noflash", "viewmodel_rus_mosinnagant_scoped_rifle"},
        {"type99_rifle_scoped_wet", "viewmodel_jap_type99_rifle_scoped_wet"},
        {"ptrs41_zombie", "viewmodel_mp_ptrs41"},
        {"ptrs41_zombie", "viewmodel_zombie_ptrs41"},
        {"ptrs41_zombie_upgraded", "viewmodel_zombie_ptrs41_up"},
    }};
    const std::array<Basis3f, 4> all_scope_axes{{
        Basis3f{}, rotated_axis,
        {{0.48F, 0.64F, 0.60F}, {-0.80F, 0.60F, 0.0F},
         {-0.36F, -0.48F, 0.80F}},
        {{0.60F, 0.80F, 0.0F}, {-0.64F, 0.48F, 0.60F},
         {0.48F, -0.36F, 0.80F}},
    }};
    const auto local_to_world = [](const Basis3f& axis, const Vec3f& origin,
                                   const Vec3f& local) {
        return Vec3f{
            origin.x + local.x * axis.forward.x + local.y * axis.left.x +
                local.z * axis.up.x,
            origin.y + local.x * axis.forward.y + local.y * axis.left.y +
                local.z * axis.up.y,
            origin.z + local.x * axis.forward.z + local.y * axis.left.z +
                local.z * axis.up.z,
        };
    };
    const auto unchanged_physical_lens = [](const PhysicalScopeSnapshot& before,
                                           const PhysicalScopeSnapshot& after) {
        const auto same_vector = [](const Vec3f& left, const Vec3f& right) {
            return left.x == right.x && left.y == right.y && left.z == right.z;
        };
        const auto same_basis = [&same_vector](const Basis3f& left, const Basis3f& right) {
            return same_vector(left.forward, right.forward) &&
                   same_vector(left.left, right.left) && same_vector(left.up, right.up);
        };
        return after.active == before.active &&
               after.anchored_to_model_tag == before.anchored_to_model_tag &&
               after.controller_generation == before.controller_generation &&
               after.publication_milliseconds == before.publication_milliseconds &&
               after.weapon_index == before.weapon_index &&
               same_vector(after.lens_origin_world, before.lens_origin_world) &&
               same_basis(after.lens_axis_world, before.lens_axis_world) &&
               same_vector(after.lens_origin_camera_local, before.lens_origin_camera_local) &&
               same_basis(after.lens_axis_camera_local, before.lens_axis_camera_local) &&
               after.zoom_fov_degrees == before.zoom_fov_degrees &&
               after.lens_radius_meters == before.lens_radius_meters;
    };
    std::size_t scope_fixture_count = 0;
    for (std::size_t profile_index = 0; profile_index < all_scope_names.size();
         ++profile_index) {
        const auto* const exact_profile =
            find_physical_scope_profile(all_scope_names[profile_index].weapon,
                                        all_scope_names[profile_index].model);
        check(exact_profile != nullptr &&
                  std::strcmp(exact_profile->internal_weapon_name,
                              all_scope_names[profile_index].weapon) == 0 &&
                  std::strcmp(exact_profile->exact_model_name,
                              all_scope_names[profile_index].model) == 0,
              "each of twelve exact scoped name/model identities resolves without a prefix");
        check(find_physical_scope_profile(all_scope_names[profile_index].weapon,
                                         "unknown_explicit_model") == nullptr &&
                  find_physical_scope_profile(all_scope_names[profile_index].weapon, "") == nullptr,
              "explicit unmatched and empty models never fall back to a name-only profile");
        if (exact_profile == nullptr) {
            continue;
        }
        for (const auto& axis : all_scope_axes) {
            const Vec3f profile_muzzle{100.0F, 200.0F, 300.0F};
            const auto grip = local_to_world(axis, profile_muzzle, {-25.0F, 0.0F, 0.0F});
            const auto optic_tag =
                local_to_world(axis, profile_muzzle, {-25.0F, 2.0F, 1.0F});
            for (const bool right_gripping : {true, false}) {
                // Generic optic assembly + profile offset, exact rear glass,
                // and the legacy grip fallback are separate physical paths.
                // PTRS intentionally forbids the third path.
                for (unsigned lens_route = 0; lens_route < 3U; ++lens_route) {
                    PhysicalScopeSnapshot candidate{};
                    Vec3f owned_world{};
                    if (exact_profile->lens_owner_tag != nullptr && lens_route != 2U) {
                        check(evaluate_physical_scope_owned_lens(
                                  exact_profile, optic_tag, axis, &owned_world),
                              "reviewed PTRS lens follows its actual evaluated owner transform");
                    }
                    const Vec3f* const tag = lens_route == 2U ? nullptr :
                        exact_profile->lens_owner_tag != nullptr ? &owned_world : &optic_tag;
                    const Vec3f* const offset = lens_route == 2U
                        ? nullptr : lens_route == 0U
                            && exact_profile->lens_owner_tag == nullptr
                            ? &exact_profile->lens_from_scope_tag_weapon_local
                            : &exact_rear_tag_offset;
                    const bool built = build_physical_scope_snapshot(
                        exact_profile, static_cast<std::int32_t>(profile_index + 1U),
                        200U + profile_index, 3000U + profile_index,
                        right_gripping, true, true, tag, offset,
                        grip, axis, {90.0F, 190.0F, 290.0F}, Basis3f{}, &candidate);
                    if (lens_route == 2U && exact_profile->requires_model_tag) {
                        check(!built && !candidate.active,
                              "tag-required PTRS rejects every grip fallback pose");
                        continue;
                    }
                    check(built && candidate.active &&
                              candidate.anchored_to_model_tag == (lens_route != 2U),
                          "each profile builds exact, generic-tag and permitted fallback glass");
                    if (!built) {
                        continue;
                    }
                    ++scope_fixture_count;
                    const auto before = candidate;
                    const Vec3f lens_from_muzzle{
                        before.lens_origin_world.x - profile_muzzle.x,
                        before.lens_origin_world.y - profile_muzzle.y,
                        before.lens_origin_world.z - profile_muzzle.z,
                    };
                    const float along_ray = dot(lens_from_muzzle, axis.forward);
                    const auto nearest_point =
                        local_to_world(axis, profile_muzzle, {along_ray, 0.0F, 0.0F});
                    check(align_physical_scope_camera_to_projectile(
                              profile_muzzle, axis, &candidate) &&
                              candidate.ballistic_camera_aligned &&
                              near(candidate.camera_origin_world, nearest_point) &&
                              near(candidate.camera_axis_world, axis),
                          "all twelve scope cameras align to the same-pass ballistic line");
                    check(unchanged_physical_lens(before, candidate),
                          "all profile lens transforms, FOV, radius and publication identity stay unchanged");
                    for (const float distance : {32.0F, 512.0F, 8192.0F}) {
                        const auto hit = local_to_world(axis, profile_muzzle,
                                                        {distance, 0.0F, 0.0F});
                        const Vec3f camera_to_hit{
                            hit.x - candidate.camera_origin_world.x,
                            hit.y - candidate.camera_origin_world.y,
                            hit.z - candidate.camera_origin_world.z,
                        };
                        const float depth = dot(camera_to_hit, candidate.camera_axis_world.forward);
                        check(depth > 0.0F &&
                                  std::fabs(dot(camera_to_hit, candidate.camera_axis_world.left) /
                                            depth) < 0.000001F &&
                                  std::fabs(dot(camera_to_hit, candidate.camera_axis_world.up) /
                                            depth) < 0.000001F,
                              "all profile near, mid and far ballistic hits project to reticle center after yaw and roll");
                    }
                    check(rejects_unchanged({invalid_float, 200.0F, 300.0F}, axis, candidate),
                          "every profile rejects a nonfinite current muzzle without changing its snapshot");
                    auto bad_basis = axis;
                    bad_basis.forward = {};
                    check(rejects_unchanged(profile_muzzle, bad_basis, candidate),
                          "every profile atomically rejects a missing current projectile basis");
                    bad_basis = axis;
                    bad_basis.up.x = invalid_float;
                    check(rejects_unchanged(profile_muzzle, bad_basis, candidate),
                          "every profile atomically rejects a nonfinite projectile basis");
                    bad_basis = axis;
                    bad_basis.up = {-axis.up.x, -axis.up.y, -axis.up.z};
                    check(rejects_unchanged(profile_muzzle, bad_basis, candidate),
                          "every profile atomically rejects a reflected projectile basis");
                    auto far_lens = candidate;
                    far_lens.lens_origin_world =
                        local_to_world(axis, profile_muzzle, {-129.0F, 0.0F, 0.0F});
                    check(rejects_unchanged(profile_muzzle, axis, far_lens),
                          "all profile variants preserve the 128 IW foreign-origin guard");
                    auto lateral_lens = candidate;
                    lateral_lens.lens_origin_world =
                        local_to_world(axis, profile_muzzle, {-20.0F, 16.01F, 0.0F});
                    check(rejects_unchanged(profile_muzzle, axis, lateral_lens),
                          "all profile variants preserve the 16 IW transverse camera-shift guard");
                    auto nonfinite_lens = candidate;
                    nonfinite_lens.lens_origin_world.z = invalid_float;
                    check(rejects_unchanged(profile_muzzle, axis, nonfinite_lens),
                          "every profile atomically rejects a nonfinite physical lens");
                }
            }
        }
    }
    check(scope_fixture_count == 256U,
          "all twelve profiles exercise four rotations and both two-hand and retained-left lens routes");

    check(find_physical_scope_profile("ptrs41_zombie") == nullptr &&
              find_physical_scope_profile("PTRS41_ZOMBIE", "viewmodel_mp_ptrs41") == nullptr &&
              find_physical_scope_profile("ptrs41_zombie", "VIEWMODEL_MP_PTRS41") == nullptr &&
              find_physical_scope_profile("ptrs41_zombie_upgraded", "viewmodel_mp_ptrs41") == nullptr,
          "ambiguous name-only and wrong-case/cross-model PTRS lookups fail closed");
    const auto* const noflash = find_physical_scope_profile(
        "mosin_rifle_scoped_noflash", "viewmodel_rus_mosinnagant_scoped_rifle");
    const auto* const wet_scope = find_physical_scope_profile(
        "type99_rifle_scoped_wet", "viewmodel_jap_type99_rifle_scoped_wet");
    const auto* const type99_scope = find_physical_scope_profile("type99_rifle_scoped");
    check(noflash != nullptr && wet_scope != nullptr &&
              near(noflash->lens_from_grip_weapon_local, mosin->lens_from_grip_weapon_local) &&
              near(noflash->lens_from_scope_tag_weapon_local, mosin->lens_from_scope_tag_weapon_local) &&
              noflash->zoom_fov_degrees == mosin->zoom_fov_degrees &&
              noflash->lens_radius_meters == mosin->lens_radius_meters &&
              near(wet_scope->lens_from_grip_weapon_local, type99_scope->lens_from_grip_weapon_local) &&
              near(wet_scope->lens_from_scope_tag_weapon_local, type99_scope->lens_from_scope_tag_weapon_local) &&
              wet_scope->zoom_fov_degrees == type99_scope->zoom_fov_degrees &&
              wet_scope->lens_radius_meters == type99_scope->lens_radius_meters,
          "proven noflash/wet aliases preserve their source optical values exactly");
    const auto* const zombie_mp = find_physical_scope_profile("ptrs41_zombie", "viewmodel_mp_ptrs41");
    const auto* const zombie_root = find_physical_scope_profile("ptrs41_zombie", "viewmodel_zombie_ptrs41");
    const auto* const zombie_up = find_physical_scope_profile("ptrs41_zombie_upgraded", "viewmodel_zombie_ptrs41_up");
    check(zombie_mp != nullptr && zombie_root != nullptr && zombie_up != nullptr &&
              zombie_mp != zombie_root && zombie_mp->lens_owner_bone == 7 &&
              zombie_root->lens_owner_bone == 0 && zombie_up->lens_owner_bone == 0 &&
              zombie_mp->lens_surface_index == 9 && zombie_root->lens_surface_index == 7 &&
              zombie_up->lens_surface_index == 7 && zombie_root->lens_center_vertex == 1 &&
              zombie_root->lens_vertex_count == 21 && zombie_root->lens_triangle_count == 20 &&
              std::strcmp(zombie_root->lens_material_name, "mc/mtl_rus_scope_trans") == 0 &&
              std::strcmp(zombie_mp->lens_owner_tag, "tag_scope") == 0 &&
              std::strcmp(zombie_root->lens_owner_tag, "j_gun") == 0 &&
              std::strcmp(zombie_up->lens_owner_tag, "j_gun") == 0 &&
              std::fabs(zombie_root->lens_radius_meters - 0.017104043197631834F) < 0.00000001F,
          "PTRS model-specific policies retain independently measured owner and aperture radius");
    if (zombie_mp != nullptr && zombie_root != nullptr && zombie_up != nullptr) {
        const Vec3f authored_center{-2.777846098F, 0.0F, 3.704639912F};
        for (const auto& axis : all_scope_axes) {
            const Vec3f root_origin{100.0F, 200.0F, 300.0F};
            const auto expected_world = local_to_world(axis, root_origin, authored_center);
            const auto tag_origin = local_to_world(axis, root_origin,
                {-0.484171987F, 0.459957004F, 1.370916963F});
            Vec3f mp_world{}, root_world{}, up_world{};
            check(evaluate_physical_scope_owned_lens(zombie_mp, tag_origin, axis, &mp_world) &&
                      evaluate_physical_scope_owned_lens(zombie_root, root_origin, axis, &root_world) &&
                      evaluate_physical_scope_owned_lens(zombie_up, root_origin, axis, &up_world) &&
                      near(mp_world, expected_world) && near(root_world, expected_world) &&
                      near(up_world, expected_world),
                  "MP scope bind and Factory root bind independently reach the reviewed lens through yaw/pitch/roll");
            Vec3f moved_unused_scope = tag_origin;
            moved_unused_scope.x += 500.0F;
            // Root rows take no tag_scope transform. An unrelated child tag
            // may move arbitrarily without affecting this actual root lens.
            check(evaluate_physical_scope_owned_lens(zombie_root, root_origin, axis, &root_world) &&
                      near(root_world, expected_world) && !near(moved_unused_scope, root_origin),
                  "Factory root-owned lens is independent of an unused scope-tag transform");
        }
        auto rotated_bind = *zombie_root;
        constexpr float half_root = 0.7071067811865475244F;
        rotated_bind.lens_owner_bind = {0.0F, 0.0F, half_root, half_root,
                                       1.0F, 2.0F, 3.0F, 2.0F};
        rotated_bind.lens_center_model_space = {1.0F, 3.0F, 3.0F};
        const Vec3f owner_origin{40.0F, 50.0F, 60.0F};
        Vec3f transformed{};
        check(evaluate_physical_scope_owned_lens(&rotated_bind, owner_origin,
                  all_scope_axes.back(), &transformed) &&
                  near(transformed, local_to_world(all_scope_axes.back(), owner_origin, {1.0F, 0.0F, 0.0F})),
              "owned lens uses the full inverse authored rotation and translation before the evaluated owner matrix");
        const auto owned_rejects = [](const PhysicalScopeProfile* candidate,
                                      const Vec3f& origin, const Basis3f& axis) {
            Vec3f output{11.0F, 12.0F, 13.0F};
            return !evaluate_physical_scope_owned_lens(candidate, origin, axis, &output) &&
                   output.x == 11.0F && output.y == 12.0F && output.z == 13.0F;
        };
        check(owned_rejects(nullptr, owner_origin, Basis3f{}) &&
                  owned_rejects(mosin, owner_origin, Basis3f{}) &&
                  owned_rejects(zombie_root, {invalid_float, 0.0F, 0.0F}, Basis3f{}) &&
                  !evaluate_physical_scope_owned_lens(zombie_root, owner_origin, Basis3f{}, nullptr),
              "owned lens rejects missing policy/output and nonfinite owner position atomically");
        auto bad_owned_axis = Basis3f{};
        bad_owned_axis.up.z = -1.0F;
        check(owned_rejects(zombie_root, owner_origin, bad_owned_axis),
              "owned lens rejects reflected evaluated owner axes atomically");
        auto bad_owned_profile = *zombie_root;
        bad_owned_profile.lens_owner_bind[7] = 0.0F;
        check(owned_rejects(&bad_owned_profile, owner_origin, Basis3f{}),
              "owned lens rejects an invalid authored quaternion weight atomically");
        bad_owned_profile = *zombie_root;
        bad_owned_profile.lens_vertex_count = 22;
        check(owned_rejects(&bad_owned_profile, owner_origin, Basis3f{}),
              "owned lens rejects metadata different from the reviewed native transparent fan");
        bad_owned_profile = *zombie_root;
        bad_owned_profile.exact_model_name = nullptr;
        check(owned_rejects(&bad_owned_profile, owner_origin, Basis3f{}),
              "owned lens never accepts a policy without an explicit exact model identity");
        const Vec3f wrong_legacy_offset{0.0F, 0.0F, 1.0F};
        check(!build_physical_scope_snapshot(zombie_root, 5, 99, 1000,
                  true, true, true, nullptr, nullptr, Vec3f{}, Basis3f{},
                  Vec3f{}, Basis3f{}, &snapshot) &&
                  !build_physical_scope_snapshot(zombie_root, 5, 99, 1000,
                  true, true, true, &owner_origin, &wrong_legacy_offset,
                  Vec3f{}, Basis3f{}, Vec3f{}, Basis3f{}, &snapshot),
              "owned lens profiles forbid grip fallback and an accidental second generic scope offset");
    }

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
