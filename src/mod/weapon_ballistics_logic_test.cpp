#include "weapon_ballistics_logic.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace wawvr::mod;

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] PublishedWeaponMuzzleSnapshot fresh_muzzle() noexcept {
    return {
        true,
        100,
        1'000,
        {10.0F, 20.0F, 30.0F},
    };
}

void test_publication_freshness() {
    const auto fresh = fresh_muzzle();
    check(published_weapon_muzzle_is_fresh(fresh, 100, 1'000),
          "exact generation and timestamp are fresh");
    check(published_weapon_muzzle_is_fresh(fresh, 104, 1'150),
          "bounded generation and time lag are accepted");
    check(!published_weapon_muzzle_is_fresh(fresh, 105, 1'150),
          "excessive generation lag is rejected");
    check(!published_weapon_muzzle_is_fresh(fresh, 99, 1'001),
          "future publication generation is rejected");
    check(!published_weapon_muzzle_is_fresh(fresh, 100, 1'151),
          "stale publication time is rejected");
    check(!published_weapon_muzzle_is_fresh(fresh, 100, 999),
          "clock rollback is rejected");
    check(!published_weapon_muzzle_is_fresh(fresh, 0, 1'000),
          "zero controller generation is rejected");

    auto invalid = fresh;
    invalid.valid = false;
    check(!published_weapon_muzzle_is_fresh(invalid, 100, 1'000),
          "invalidated publication is rejected");
    invalid = fresh;
    invalid.publication_milliseconds = 0;
    check(!published_weapon_muzzle_is_fresh(invalid, 100, 1'000),
          "zero publication time is rejected");
    invalid = fresh;
    invalid.origin.x = std::numeric_limits<float>::quiet_NaN();
    check(!published_weapon_muzzle_is_fresh(invalid, 100, 1'000),
          "non-finite origin is rejected");
}

void test_local_bullet_gate() {
    PhysicalMuzzleGate gate{
        true,
        0x0176C6F0,
        0x0176C6F0,
        0,
        true,
        0,
        true,
        true,
    };
    check(physical_muzzle_gate_allows(gate),
          "audited local bullet path is accepted");
    auto projectile = gate;
    projectile.weapon_type = 2;
    check(physical_muzzle_gate_allows(projectile),
          "audited local projectile path accepts the Ray Gun muzzle");

    auto rejected = gate;
    rejected.hook_enabled = false;
    check(!physical_muzzle_gate_allows(rejected),
          "disabled hook is rejected");
    rejected = gate;
    rejected.firing_entity += 0x378;
    check(!physical_muzzle_gate_allows(rejected),
          "non-local entity is rejected");
    rejected = gate;
    rejected.entity_number = 1;
    check(!physical_muzzle_gate_allows(rejected),
          "nonzero entity number is rejected");
    rejected = gate;
    rejected.has_client = false;
    check(!physical_muzzle_gate_allows(rejected),
          "entity without client is rejected");
    rejected = gate;
    rejected.weapon_type = 1;
    check(!physical_muzzle_gate_allows(rejected),
          "unsupported non-bullet/non-projectile weapon path is rejected");
    rejected = gate;
    rejected.controller_frame_current = false;
    check(!physical_muzzle_gate_allows(rejected),
          "stale controller state is rejected");
    rejected = gate;
    rejected.published_muzzle_fresh = false;
    check(!physical_muzzle_gate_allows(rejected),
          "stale muzzle publication is rejected");
}

void test_authoritative_weapon_basis_override() {
    check(authoritative_weapon_basis_is_requested(false, false, 2),
          "single-player projectiles request the visible launcher basis");
    check(authoritative_weapon_basis_is_requested(false, false, 0),
          "every single-player hitscan weapon requests the visible basis");
    check(authoritative_weapon_basis_is_requested(false, true, 0),
          "audited single-player bolt profiles request the visible basis");
    check(authoritative_weapon_basis_is_requested(true, false, 1),
          "layout-wide overrides remain authoritative for every weapon type");
    check(!authoritative_weapon_basis_is_requested(false, false, 1),
          "unprofiled non-hitscan non-projectile weapons keep native basis");

    const wawvr::xr::Basis3f visible{
        .forward = {0.8F, 0.6F, 0.0F},
        .left = {-0.6F, 0.8F, 0.0F},
        .up = {0.0F, 0.0F, 1.0F},
    };
    const AuthoritativeWeaponBasisGate accepted{
        true,
        true,
        true,
    };
    wawvr::xr::Vec3f forward{9.0F, 8.0F, 7.0F};
    wawvr::xr::Vec3f right{6.0F, 5.0F, 4.0F};
    wawvr::xr::Vec3f up{3.0F, 2.0F, 1.0F};
    check(apply_authoritative_weapon_basis_override(
              accepted, visible, &forward, &right, &up),
          "fresh accepted physical shot receives the rendered basis");
    check(forward.x == 0.8F && forward.y == 0.6F && forward.z == 0.0F,
          "WeaponParms forward matches rendered forward");
    check(right.x == 0.6F && right.y == -0.8F && right.z == 0.0F,
          "WeaponParms right is the negated IW rendered left vector");
    check(up.x == 0.0F && up.y == 0.0F && up.z == 1.0F,
          "WeaponParms up matches rendered up");

    const auto check_rejected_unchanged = [&](
        const AuthoritativeWeaponBasisGate& gate,
        const wawvr::xr::Basis3f& basis,
        const std::string_view message) {
        wawvr::xr::Vec3f rejected_forward{9.0F, 8.0F, 7.0F};
        wawvr::xr::Vec3f rejected_right{6.0F, 5.0F, 4.0F};
        wawvr::xr::Vec3f rejected_up{3.0F, 2.0F, 1.0F};
        const auto before_forward = std::bit_cast<
            std::array<std::uint32_t, 3>>(rejected_forward);
        const auto before_right = std::bit_cast<
            std::array<std::uint32_t, 3>>(rejected_right);
        const auto before_up = std::bit_cast<
            std::array<std::uint32_t, 3>>(rejected_up);
        check(!apply_authoritative_weapon_basis_override(
                  gate, basis, &rejected_forward, &rejected_right,
                  &rejected_up),
              message);
        check(std::bit_cast<std::array<std::uint32_t, 3>>(
                  rejected_forward) == before_forward &&
                  std::bit_cast<std::array<std::uint32_t, 3>>(
                      rejected_right) == before_right &&
                  std::bit_cast<std::array<std::uint32_t, 3>>(
                      rejected_up) == before_up,
              "rejected basis decision preserves all native bits");
    };

    auto rejected = accepted;
    rejected.override_requested = false;
    check_rejected_unchanged(
        rejected, visible, "unselected weapon profile is rejected");
    rejected = accepted;
    rejected.physical_muzzle_allowed = false;
    check_rejected_unchanged(
        rejected, visible, "nonlocal or otherwise rejected shot is rejected");
    rejected = accepted;
    rejected.final_visible_basis_fresh = false;
    check_rejected_unchanged(
        rejected, visible, "stale rendered basis is rejected atomically");
    auto invalid = visible;
    invalid.forward.x = std::numeric_limits<float>::quiet_NaN();
    check_rejected_unchanged(
        accepted, invalid, "non-finite rendered basis is rejected");
    check(!apply_authoritative_weapon_basis_override(
              accepted, visible, nullptr, &right, &up) &&
              !apply_authoritative_weapon_basis_override(
                  accepted, visible, &forward, nullptr, &up) &&
              !apply_authoritative_weapon_basis_override(
                  accepted, visible, &forward, &right, nullptr),
          "null basis outputs are rejected");
}

void test_hitscan_tag_flash_policy() {
    check(hitscan_uses_tag_flash_forward("ppsh"),
          "exact PPSh uses its evaluated tag_flash barrel axis");
    check(hitscan_uses_tag_flash_forward("svt40"),
          "exact SVT40 removes the rear-grip offset from its firing axis");
    check(!hitscan_uses_tag_flash_forward("m1garand") &&
              !hitscan_uses_tag_flash_forward("mosinrifle") &&
              !hitscan_uses_tag_flash_forward("") &&
              !hitscan_uses_tag_flash_forward("ppsh_upgraded") &&
              !hitscan_uses_tag_flash_forward("svt40_scoped") &&
              !hitscan_uses_tag_flash_forward("svt40_mp") &&
              !hitscan_uses_tag_flash_forward("SVT40"),
          "other accepted rifles and nonexact names keep their existing basis");
}

void test_exact_bazooka_weapon_root_launch_basis() {
    constexpr float epsilon = 1.0e-5F;
    const wawvr::xr::Basis3f tracked{
        .forward = {0.0F, 3.0F, 0.0F},
        .left = {-2.0F, 0.0F, 0.0F},
        .up = {0.0F, 0.0F, 4.0F},
    };
    const ExactBazookaWeaponRootLaunchGate accepted{true, true, true};
    wawvr::xr::Basis3f launch{};
    check(select_exact_bazooka_weapon_root_launch_basis(
              accepted, tracked, &launch),
          "exact Bazooka accepts the fresh evaluated weapon-root basis");
    check(std::fabs(launch.forward.x) < epsilon &&
              std::fabs(launch.forward.y - 1.0F) < epsilon &&
              std::fabs(launch.forward.z) < epsilon,
          "Bazooka launch forward is normalized from the weapon root");

    const auto check_rejected_unchanged = [&] (
        const ExactBazookaWeaponRootLaunchGate& gate,
        const wawvr::xr::Basis3f& basis,
        const std::string_view message) {
        wawvr::xr::Basis3f rejected{
            .forward = {9.0F, 8.0F, 7.0F},
            .left = {6.0F, 5.0F, 4.0F},
            .up = {3.0F, 2.0F, 1.0F},
        };
        const auto before =
            std::bit_cast<std::array<std::uint32_t, 9>>(rejected);
        check(!select_exact_bazooka_weapon_root_launch_basis(
                  gate, basis, &rejected), message);
        check(std::bit_cast<std::array<std::uint32_t, 9>>(rejected) ==
                  before,
              "rejected Bazooka weapon-root basis preserves every output bit");
    };

    auto rejected = accepted;
    rejected.exact_local_bazooka_route = false;
    check_rejected_unchanged(
        rejected, tracked, "non-Bazooka projectile route is rejected");
    rejected = accepted;
    rejected.physical_muzzle_allowed = false;
    check_rejected_unchanged(
        rejected, tracked, "rejected physical muzzle route is rejected");
    rejected = accepted;
    rejected.published_weapon_root_fresh = false;
    check_rejected_unchanged(
        rejected, tracked, "stale Bazooka weapon root is rejected");
    auto invalid = tracked;
    invalid.forward.x = std::numeric_limits<float>::quiet_NaN();
    check_rejected_unchanged(
        accepted, invalid, "non-finite Bazooka weapon root is rejected");
    check(!select_exact_bazooka_weapon_root_launch_basis(
              accepted, tracked, nullptr),
          "null weapon-root Bazooka launch output is rejected");
}

void test_bazooka_viewmodel_alignment() {
    constexpr float epsilon = 1.0e-4F;
    constexpr float cosine_seven_degrees = 0.9925461516F;
    constexpr float sine_seven_degrees = 0.1218693434F;
    const wawvr::xr::Basis3f identity{};
    const wawvr::xr::Basis3f tilted_bore{
        .forward = {
            cosine_seven_degrees, 0.0F, sine_seven_degrees},
        .left = {0.0F, 1.0F, 0.0F},
        .up = {
            -sine_seven_degrees, 0.0F, cosine_seven_degrees},
    };
    const wawvr::xr::Vec3f root_origin{4.0F, -3.0F, 2.0F};
    const wawvr::xr::Vec3f grip_anchor{9.0F, 5.0F, -1.0F};

    BazookaViewmodelAlignment alignment{};
    check(calculate_bazooka_viewmodel_alignment(
              root_origin, identity, grip_anchor, tilted_bore, identity,
              &alignment),
          "realistic seven-degree Bazooka bore correction is accepted");
    check(std::fabs(alignment.correction_degrees - 7.0F) < epsilon,
          "reported Bazooka visual correction is seven degrees");

    const auto transform_root_local = [&alignment](
        const wawvr::xr::Vec3f& local) noexcept {
        return wawvr::xr::Vec3f{
            alignment.corrected_root_origin.x +
                alignment.corrected_root_axis.forward.x * local.x +
                alignment.corrected_root_axis.left.x * local.y +
                alignment.corrected_root_axis.up.x * local.z,
            alignment.corrected_root_origin.y +
                alignment.corrected_root_axis.forward.y * local.x +
                alignment.corrected_root_axis.left.y * local.y +
                alignment.corrected_root_axis.up.y * local.z,
            alignment.corrected_root_origin.z +
                alignment.corrected_root_axis.forward.z * local.x +
                alignment.corrected_root_axis.left.z * local.y +
                alignment.corrected_root_axis.up.z * local.z,
        };
    };
    const wawvr::xr::Vec3f grip_local{
        grip_anchor.x - root_origin.x,
        grip_anchor.y - root_origin.y,
        grip_anchor.z - root_origin.z,
    };
    const auto corrected_grip = transform_root_local(grip_local);
    check(std::fabs(corrected_grip.x - grip_anchor.x) < epsilon &&
              std::fabs(corrected_grip.y - grip_anchor.y) < epsilon &&
              std::fabs(corrected_grip.z - grip_anchor.z) < epsilon,
          "Bazooka root correction exactly preserves the gripping-hand pivot");

    const wawvr::xr::Vec3f corrected_bore{
        alignment.corrected_root_axis.forward.x * tilted_bore.forward.x +
            alignment.corrected_root_axis.left.x * tilted_bore.forward.y +
            alignment.corrected_root_axis.up.x * tilted_bore.forward.z,
        alignment.corrected_root_axis.forward.y * tilted_bore.forward.x +
            alignment.corrected_root_axis.left.y * tilted_bore.forward.y +
            alignment.corrected_root_axis.up.y * tilted_bore.forward.z,
        alignment.corrected_root_axis.forward.z * tilted_bore.forward.x +
            alignment.corrected_root_axis.left.z * tilted_bore.forward.y +
            alignment.corrected_root_axis.up.z * tilted_bore.forward.z,
    };
    check(std::fabs(corrected_bore.x - 1.0F) < epsilon &&
              std::fabs(corrected_bore.y) < epsilon &&
              std::fabs(corrected_bore.z) < epsilon,
          "Bazooka root correction aligns the evaluated bore to tracked aim");

    BazookaViewmodelAlignment already_aligned{
        .corrected_root_origin = {99.0F, 98.0F, 97.0F},
        .corrected_root_axis = {
            .forward = {96.0F, 95.0F, 94.0F},
            .left = {93.0F, 92.0F, 91.0F},
            .up = {90.0F, 89.0F, 88.0F},
        },
        .correction_degrees = 87.0F,
    };
    check(calculate_bazooka_viewmodel_alignment(
              root_origin, identity, grip_anchor, identity, identity,
              &already_aligned),
          "already-aligned Bazooka viewmodel is accepted");
    check(already_aligned.corrected_root_origin.x == root_origin.x &&
              already_aligned.corrected_root_origin.y == root_origin.y &&
              already_aligned.corrected_root_origin.z == root_origin.z &&
              already_aligned.corrected_root_axis.forward.x == 1.0F &&
              already_aligned.corrected_root_axis.forward.y == 0.0F &&
              already_aligned.corrected_root_axis.forward.z == 0.0F &&
              already_aligned.corrected_root_axis.left.x == 0.0F &&
              already_aligned.corrected_root_axis.left.y == 1.0F &&
              already_aligned.corrected_root_axis.left.z == 0.0F &&
              already_aligned.corrected_root_axis.up.x == 0.0F &&
              already_aligned.corrected_root_axis.up.y == 0.0F &&
              already_aligned.corrected_root_axis.up.z == 1.0F &&
              already_aligned.correction_degrees == 0.0F,
          "already-aligned Bazooka produces an exact identity correction");

    const auto check_rejected_unchanged = [&] (
        const wawvr::xr::Vec3f& candidate_root_origin,
        const wawvr::xr::Basis3f& candidate_root_axis,
        const wawvr::xr::Vec3f& candidate_grip,
        const wawvr::xr::Basis3f& candidate_bore,
        const wawvr::xr::Basis3f& candidate_tracked,
        const std::string_view message) {
        BazookaViewmodelAlignment rejected{
            .corrected_root_origin = {9.0F, 8.0F, 7.0F},
            .corrected_root_axis = {
                .forward = {6.0F, 5.0F, 4.0F},
                .left = {3.0F, 2.0F, 1.0F},
                .up = {-1.0F, -2.0F, -3.0F},
            },
            .correction_degrees = 42.0F,
        };
        const auto before =
            std::bit_cast<std::array<std::uint32_t, 13>>(rejected);
        check(!calculate_bazooka_viewmodel_alignment(
                  candidate_root_origin, candidate_root_axis,
                  candidate_grip, candidate_bore, candidate_tracked,
                  &rejected),
              message);
        check(std::bit_cast<std::array<std::uint32_t, 13>>(rejected) ==
                  before,
              "rejected Bazooka alignment preserves every output bit");
    };

    constexpr float cosine_sixteen_degrees = 0.9612616959F;
    constexpr float sine_sixteen_degrees = 0.2756373558F;
    auto sixteen_degree_bore = identity;
    sixteen_degree_bore.forward = {
        cosine_sixteen_degrees, 0.0F, sine_sixteen_degrees};
    BazookaViewmodelAlignment sixteen_degree_alignment{};
    check(calculate_bazooka_viewmodel_alignment(
              root_origin, identity, grip_anchor, sixteen_degree_bore,
              identity, &sixteen_degree_alignment),
          "observed sixteen-degree Bazooka pose remains continuously aligned");
    constexpr float cosine_twenty_six_degrees = 0.8987940463F;
    constexpr float sine_twenty_six_degrees = 0.4383711468F;
    auto excessive_bore = identity;
    excessive_bore.forward = {
        cosine_twenty_six_degrees, 0.0F, sine_twenty_six_degrees};
    check_rejected_unchanged(
        root_origin, identity, grip_anchor, excessive_bore, identity,
        "Bazooka visual correction above twenty-five degrees is rejected");

    auto non_finite_root = root_origin;
    non_finite_root.x = std::numeric_limits<float>::quiet_NaN();
    check_rejected_unchanged(
        non_finite_root, identity, grip_anchor, tilted_bore, identity,
        "non-finite Bazooka alignment input is rejected");
    check(!calculate_bazooka_viewmodel_alignment(
              root_origin, identity, grip_anchor, tilted_bore, identity,
              nullptr),
          "null Bazooka alignment output is rejected");
}

void test_evaluated_projectile_basis() {
    const wawvr::xr::Basis3f root_reference{
        .forward = {1.0F, 0.0F, 0.0F},
        .left = {0.0F, 1.0F, 0.0F},
        .up = {0.0F, 0.0F, 1.0F},
    };
    wawvr::xr::Basis3f projectile{};
    check(build_evaluated_projectile_basis(
              {3.0F, 4.0F, 0.0F}, root_reference, &projectile),
          "evaluated barrel direction builds a projectile basis");
    constexpr float epsilon = 1.0e-5F;
    check(std::fabs(projectile.forward.x - 0.6F) < epsilon &&
              std::fabs(projectile.forward.y - 0.8F) < epsilon &&
              std::fabs(projectile.forward.z) < epsilon,
          "projectile forward follows the normalized evaluated barrel");
    check(std::fabs(projectile.left.x + 0.8F) < epsilon &&
              std::fabs(projectile.left.y - 0.6F) < epsilon &&
              std::fabs(projectile.left.z) < epsilon,
          "projectile left preserves evaluated weapon roll");
    check(std::fabs(projectile.up.x) < epsilon &&
              std::fabs(projectile.up.y) < epsilon &&
              std::fabs(projectile.up.z - 1.0F) < epsilon,
          "projectile up completes the orthonormal IW basis");

    wawvr::xr::Vec3f forward{};
    wawvr::xr::Vec3f right{};
    wawvr::xr::Vec3f up{};
    check(apply_authoritative_weapon_basis_override(
              {true, true, true}, projectile,
              &forward, &right, &up) &&
              std::fabs(right.x - 0.8F) < epsilon &&
              std::fabs(right.y + 0.6F) < epsilon,
          "evaluated IW left converts to WeaponParms right");

    check(!build_evaluated_projectile_basis(
              {}, root_reference, &projectile),
          "zero-length evaluated barrel fails closed");
    auto invalid_reference = root_reference;
    invalid_reference.left.x =
        std::numeric_limits<float>::quiet_NaN();
    check(!build_evaluated_projectile_basis(
              {1.0F, 0.0F, 0.0F}, invalid_reference, &projectile),
          "non-finite roll reference fails closed");
    check(!build_evaluated_projectile_basis(
              {1.0F, 0.0F, 0.0F}, root_reference, nullptr),
          "null projectile basis output fails closed");
}

void test_validated_bazooka_bore_basis() {
    constexpr float epsilon = 1.0e-5F;
    const wawvr::xr::Basis3f flash_basis{};
    // Bind-pose locations read from retail viewmodel_usa_bazooka_at. The
    // segment is the full launch tube and is nearly parallel with row zero.
    const wawvr::xr::Vec3f tag_brass{
        -44.004814F, 0.24605393F, 0.9398351F};
    const wawvr::xr::Vec3f tag_flash{
        21.321442F, 0.0F, 1.2375991F};
    wawvr::xr::Basis3f bore{};
    float length = 0.0F;
    float alignment = 0.0F;
    check(build_validated_bazooka_bore_basis(
              tag_brass, tag_flash, flash_basis, &bore,
              &length, &alignment),
          "retail Bazooka tag_brass-to-tag_flash geometry is accepted");
    check(length > 65.0F && length < 66.0F,
          "retail Bazooka bore has the measured 65-unit tube length");
    check(alignment > 0.9999F,
          "retail Bazooka bore agrees with tag_flash row zero");
    check(bore.forward.x > 0.9999F && bore.forward.y < 0.0F &&
              bore.forward.z > 0.0F,
          "Bazooka bore points from rear tag toward the muzzle tag");

    const auto rejected_unchanged = [&](
        const wawvr::xr::Vec3f& rear,
        const wawvr::xr::Vec3f& muzzle,
        const wawvr::xr::Basis3f& row_zero_basis,
        const std::string_view message) {
        wawvr::xr::Basis3f output{
            .forward = {9.0F, 8.0F, 7.0F},
            .left = {6.0F, 5.0F, 4.0F},
            .up = {3.0F, 2.0F, 1.0F},
        };
        float rejected_length = 123.0F;
        float rejected_alignment = 456.0F;
        const auto basis_before =
            std::bit_cast<std::array<std::uint32_t, 9>>(output);
        const auto length_before =
            std::bit_cast<std::uint32_t>(rejected_length);
        const auto alignment_before =
            std::bit_cast<std::uint32_t>(rejected_alignment);
        check(!build_validated_bazooka_bore_basis(
                  rear, muzzle, row_zero_basis, &output,
                  &rejected_length, &rejected_alignment),
              message);
        check(std::bit_cast<std::array<std::uint32_t, 9>>(output) ==
                  basis_before &&
                  std::bit_cast<std::uint32_t>(rejected_length) ==
                      length_before &&
                  std::bit_cast<std::uint32_t>(rejected_alignment) ==
                      alignment_before,
              "rejected Bazooka geometry preserves every output bit");
    };
    rejected_unchanged(
        {-28.0F, 0.0F, 0.0F}, {21.0F, 0.0F, 0.0F}, flash_basis,
        "short rear-to-muzzle segment is rejected");
    rejected_unchanged(
        {-60.0F, 0.0F, 0.0F}, {21.0F, 0.0F, 0.0F}, flash_basis,
        "long rear-to-muzzle segment is rejected");
    rejected_unchanged(
        {21.0F, -65.0F, 0.0F}, {21.0F, 0.0F, 0.0F}, flash_basis,
        "tube segment inconsistent with tag_flash row zero is rejected");
    rejected_unchanged(
        {86.0F, 0.0F, 0.0F}, {21.0F, 0.0F, 0.0F}, flash_basis,
        "rear tag beyond the muzzle is rejected by forward alignment");
    auto non_finite_rear = tag_brass;
    non_finite_rear.x = std::numeric_limits<float>::quiet_NaN();
    rejected_unchanged(
        non_finite_rear, tag_flash, flash_basis,
        "non-finite rear tag is rejected");
    check(!build_validated_bazooka_bore_basis(
              tag_brass, tag_flash, flash_basis, nullptr),
          "null Bazooka bore output is rejected");

    wawvr::xr::Basis3f fallback{};
    check(build_evaluated_projectile_basis(
              flash_basis.forward, flash_basis, &fallback) &&
              std::fabs(fallback.forward.x - 1.0F) < epsilon &&
              std::fabs(fallback.forward.y) < epsilon &&
              std::fabs(fallback.forward.z) < epsilon,
          "tag_flash row zero remains the validated geometry fallback");
}

void test_exact_bazooka_projectile_direction() {
    constexpr float epsilon = 1.0e-5F;
    const wawvr::xr::Basis3f evaluated_bore{
        .forward = {0.0F, 3.0F, 0.0F},
        .left = {-1.0F, 0.0F, 0.0F},
        .up = {0.0F, 0.0F, 1.0F},
    };
    wawvr::xr::Vec3f direction{};
    check(exact_bazooka_projectile_direction(
              evaluated_bore, &direction),
          "exact Bazooka route accepts the evaluated bore basis");
    check(std::fabs(direction.x) < epsilon &&
              std::fabs(direction.y - 1.0F) < epsilon &&
              std::fabs(direction.z) < epsilon,
          "exact Bazooka route normalizes and uses bore forward directly");

    auto invalid = evaluated_bore;
    invalid.forward = {};
    direction = {9.0F, 8.0F, 7.0F};
    const auto before =
        std::bit_cast<std::array<std::uint32_t, 3>>(direction);
    check(!exact_bazooka_projectile_direction(invalid, &direction) &&
              std::bit_cast<std::array<std::uint32_t, 3>>(direction) ==
                  before,
          "invalid exact Bazooka bore fails without changing direction");
    check(!exact_bazooka_projectile_direction(evaluated_bore, nullptr),
          "null exact Bazooka direction output is rejected");
}

void test_physical_launcher_profiles() {
    check(find_physical_rocket_launcher_profile("bazooka") ==
              &kBazookaPhysicalLauncher,
          "the accepted Bazooka profile remains exact");
    check(find_physical_rocket_launcher_profile("panzerschrek") ==
              &kPanzerschreckPhysicalLauncher,
          "the captured retail Panzerschreck spelling is supported");
    check(physical_launcher_model_matches(
              &kPanzerschreckPhysicalLauncher,
              "viewmodel_ger_panzerschreck_at"),
          "the captured Panzerschreck viewmodel is accepted");
    check(!physical_launcher_model_matches(
              &kPanzerschreckPhysicalLauncher, "viewmodel_ger_panzerschreck") &&
              !physical_launcher_model_matches(
                  &kPanzerschreckPhysicalLauncher, "custom_panzerschreck") &&
              !physical_launcher_model_matches(
                  &kPanzerschreckPhysicalLauncher, "") &&
              !physical_launcher_model_matches(
                  nullptr, "viewmodel_ger_panzerschreck_at"),
          "missing or unaudited launcher models do not publish a rigid basis");
    for (const std::string_view name : {
             "", "panzerschreck", "panzerschrek_mp", "panzerschrek_upgraded",
             "bazooka_mp", "ray_gun", "panzerfaust", "ptrs41"}) {
        check(find_physical_rocket_launcher_profile(name) == nullptr,
              "unaudited weapon identities do not gain the late rocket route");
    }

    const wawvr::xr::Basis3f identity{
        {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}};
    const wawvr::xr::Vec3f rear{-30.783657F, 0.001282F, 1.266979F};
    const wawvr::xr::Vec3f flash{34.454796F, 0.0F, 0.948418F};
    wawvr::xr::Basis3f result{};
    float length = 0.0F;
    float alignment = 0.0F;
    check(build_validated_physical_launcher_bore_basis(
              &kPanzerschreckPhysicalLauncher, rear, flash, identity,
              &identity, &result, &length, &alignment),
          "captured Panzerschreck tag geometry and rigid tube are accepted");
    check(std::fabs(length - 65.23923F) < 0.001F && alignment > 0.9999F,
          "captured Panzerschreck span and tag alignment match the model");
    check(result.forward.x == 1.0F && result.forward.y == 0.0F &&
              result.forward.z == 0.0F,
          "rigid tube direction does not inherit the displaced flash tag tilt");

    // Rotate the complete captured model: translation, pitch, yaw and roll
    // must not change which local axis is the physical barrel.
    const wawvr::xr::Basis3f rotated{
        {0.36F, 0.48F, 0.8F}, {-0.8F, 0.6F, 0.0F},
        {-0.48F, -0.64F, 0.6F}};
    const auto transform = [&](const wawvr::xr::Vec3f& value) {
        return wawvr::xr::Vec3f{
            800.0F + rotated.forward.x * value.x + rotated.left.x * value.y +
                rotated.up.x * value.z,
            -450.0F + rotated.forward.y * value.x + rotated.left.y * value.y +
                rotated.up.y * value.z,
            90.0F + rotated.forward.z * value.x + rotated.left.z * value.y +
                rotated.up.z * value.z};
    };
    check(build_validated_physical_launcher_bore_basis(
              &kPanzerschreckPhysicalLauncher, transform(rear), transform(flash),
              rotated, &rotated, &result),
          "world-transformed launcher geometry remains valid");
    check(std::fabs(result.forward.x - rotated.forward.x) < 0.000001F &&
              std::fabs(result.forward.y - rotated.forward.y) < 0.000001F &&
              std::fabs(result.forward.z - rotated.forward.z) < 0.000001F,
          "rotated launcher fires along the rigid visible tube");

    const auto rejected_preserves_output = [&](
        const PhysicalRocketLauncherProfile* profile,
        const wawvr::xr::Vec3f& test_rear,
        const wawvr::xr::Vec3f& test_flash,
        const wawvr::xr::Basis3f* root) {
        const wawvr::xr::Basis3f before{
            {11.0F, 12.0F, 13.0F}, {14.0F, 15.0F, 16.0F},
            {17.0F, 18.0F, 19.0F}};
        auto output = before;
        float output_length = -31.0F;
        float output_alignment = -32.0F;
        check(!build_validated_physical_launcher_bore_basis(
                  profile, test_rear, test_flash, identity, root, &output,
                  &output_length, &output_alignment),
              "invalid launcher geometry fails closed");
        check(std::bit_cast<std::array<std::uint32_t, 9>>(before) ==
                  std::bit_cast<std::array<std::uint32_t, 9>>(output) &&
                  output_length == -31.0F && output_alignment == -32.0F,
              "rejected launcher geometry preserves every output");
    };
    rejected_preserves_output(nullptr, rear, flash, &identity);
    rejected_preserves_output(
        &kPanzerschreckPhysicalLauncher, rear, flash, nullptr);
    rejected_preserves_output(
        &kPanzerschreckPhysicalLauncher, {}, {63.99F, 0.0F, 0.0F}, &identity);
    rejected_preserves_output(
        &kPanzerschreckPhysicalLauncher, {}, {66.01F, 0.0F, 0.0F}, &identity);
    rejected_preserves_output(
        &kPanzerschreckPhysicalLauncher, flash, rear, &identity);
    rejected_preserves_output(
        &kPanzerschreckPhysicalLauncher, rear, flash, &rotated);
    auto non_finite = identity;
    non_finite.forward.x = std::numeric_limits<float>::quiet_NaN();
    rejected_preserves_output(
        &kPanzerschreckPhysicalLauncher, rear, flash, &non_finite);
    const PhysicalRocketLauncherProfile copied_profile =
        kPanzerschreckPhysicalLauncher;
    rejected_preserves_output(&copied_profile, rear, flash, &identity);
    check(!build_validated_physical_launcher_bore_basis(
              &kPanzerschreckPhysicalLauncher, rear, flash, identity,
              &identity, nullptr),
          "null launcher output is rejected");

    wawvr::xr::Basis3f legacy{};
    float legacy_length = 0.0F;
    float legacy_alignment = 0.0F;
    check(build_validated_bazooka_bore_basis(
              rear, flash, identity, &legacy, &legacy_length, &legacy_alignment) &&
              build_validated_physical_launcher_bore_basis(
                  &kBazookaPhysicalLauncher, rear, flash, identity, nullptr,
                  &result, &length, &alignment),
          "Bazooka retains its original measured-tag route");
    check(std::bit_cast<std::array<std::uint32_t, 9>>(legacy) ==
              std::bit_cast<std::array<std::uint32_t, 9>>(result) &&
              legacy_length == length && legacy_alignment == alignment,
          "Bazooka profile produces bit-identical legacy results");
}

void test_physical_launcher_visual_bounds() {
    const wawvr::xr::Basis3f identity{};
    const wawvr::xr::Vec3f origin{4.0F, -3.0F, 2.0F};
    const wawvr::xr::Vec3f grip{9.0F, 5.0F, -1.0F};
    const auto tilted = [](const float degrees) {
        const float radians = degrees * (3.14159265358979323846F / 180.0F);
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return wawvr::xr::Basis3f{
            {c, 0.0F, s}, {0.0F, 1.0F, 0.0F}, {-s, 0.0F, c}};
    };
    const auto rotate = [](const wawvr::xr::Basis3f& axis,
                           const wawvr::xr::Vec3f& value) {
        return wawvr::xr::Vec3f{
            axis.forward.x * value.x + axis.left.x * value.y + axis.up.x * value.z,
            axis.forward.y * value.x + axis.left.y * value.y + axis.up.y * value.z,
            axis.forward.z * value.x + axis.left.z * value.y + axis.up.z * value.z};
    };
    for (const float angle : {0.0F, 23.776F, 44.604F, 45.0F, 59.0F, 59.9F}) {
        const auto bore = tilted(angle);
        BazookaViewmodelAlignment output{};
        check(calculate_physical_launcher_viewmodel_alignment(
                  &kPanzerschreckPhysicalLauncher, origin, identity, grip,
                  bore, identity, &output),
              "Panzerschreck idle, fire and bounded near-limit poses align");
        check(std::fabs(output.correction_degrees - angle) < 0.001F,
              "profile visual correction reports the measured angle");
        const auto pivot_offset = rotate(output.corrected_root_axis,
            {grip.x - origin.x, grip.y - origin.y, grip.z - origin.z});
        check(std::fabs(output.corrected_root_origin.x + pivot_offset.x - grip.x) < 0.0001F &&
                  std::fabs(output.corrected_root_origin.y + pivot_offset.y - grip.y) < 0.0001F &&
                  std::fabs(output.corrected_root_origin.z + pivot_offset.z - grip.z) < 0.0001F,
              "large exact-profile correction preserves the existing hand pivot");
        const auto corrected_bore = rotate(output.corrected_root_axis, bore.forward);
        check(std::fabs(corrected_bore.x - 1.0F) < 0.0001F &&
                  std::fabs(corrected_bore.y) < 0.0001F &&
                  std::fabs(corrected_bore.z) < 0.0001F,
              "Panzerschreck remains on the same tracked line between idle and fire");
    }
    const auto rejects = [&](const PhysicalRocketLauncherProfile* profile,
                              const wawvr::xr::Basis3f& bore) {
        const BazookaViewmodelAlignment before{
            {11.0F, 12.0F, 13.0F},
            {{14.0F, 15.0F, 16.0F}, {17.0F, 18.0F, 19.0F}, {20.0F, 21.0F, 22.0F}},
            23.0F};
        auto output = before;
        check(!calculate_physical_launcher_viewmodel_alignment(
                  profile, origin, identity, grip, bore, identity, &output),
              "unknown or over-limit profile alignment is rejected");
        check(std::bit_cast<std::array<std::uint32_t, 13>>(before) ==
                  std::bit_cast<std::array<std::uint32_t, 13>>(output),
              "rejected visual correction is atomic");
    };
    rejects(&kPanzerschreckPhysicalLauncher, tilted(60.1F));
    rejects(&kPanzerschreckPhysicalLauncher, tilted(61.0F));
    rejects(&kPanzerschreckPhysicalLauncher, tilted(180.0F));
    rejects(&kBazookaPhysicalLauncher, tilted(44.604F));
    rejects(nullptr, tilted(10.0F));
    auto invalid = identity;
    invalid.forward.z = std::numeric_limits<float>::quiet_NaN();
    rejects(&kPanzerschreckPhysicalLauncher, invalid);
    const PhysicalRocketLauncherProfile copied = kPanzerschreckPhysicalLauncher;
    rejects(&copied, tilted(10.0F));
    check(!calculate_physical_launcher_viewmodel_alignment(
              &kPanzerschreckPhysicalLauncher, origin, identity, grip,
              tilted(45.0F), identity, nullptr),
          "null visual correction output fails closed");

    for (const float angle : {0.0F, 7.0F, 16.0F, 23.776F}) {
        BazookaViewmodelAlignment legacy{};
        BazookaViewmodelAlignment selected{};
        check(calculate_bazooka_viewmodel_alignment(
                  origin, identity, grip, tilted(angle), identity, &legacy) &&
                  calculate_physical_launcher_viewmodel_alignment(
                      &kBazookaPhysicalLauncher, origin, identity, grip,
                      tilted(angle), identity, &selected),
              "Bazooka accepts the same original poses");
        check(std::bit_cast<std::array<std::uint32_t, 13>>(legacy) ==
                  std::bit_cast<std::array<std::uint32_t, 13>>(selected),
              "Bazooka profile preserves every original alignment result bit");
    }
}

void test_exact_bazooka_stable_missile_flag() {
    std::uint32_t flags = 0x40000001U;
    check(apply_exact_bazooka_stable_missile_flag(true, &flags),
          "exact local Bazooka missile is marked stable");
    check(flags == (0x40000001U | kStableMissilesEntityFlag),
          "stable-missile update preserves every unrelated entity flag");
    check(apply_exact_bazooka_stable_missile_flag(true, &flags) &&
              flags == (0x40000001U | kStableMissilesEntityFlag),
          "stable-missile update is idempotent");

    const std::uint32_t before = flags;
    check(!apply_exact_bazooka_stable_missile_flag(false, &flags) &&
              flags == before,
          "non-Bazooka route leaves entity flags unchanged");
    check(!apply_exact_bazooka_stable_missile_flag(true, nullptr),
          "null entity-flags output is rejected");
}

[[nodiscard]] FixedAdsSpreadGate accepted_spread_gate() noexcept {
    return {
        true,
        0x0176C6F0,
        0x0176C6F0,
        0,
        true,
        0,
        0.4F,
    };
}

void check_rejected_spread_is_unchanged(
    const FixedAdsSpreadGate& gate,
    const std::string_view message) {
    float spread = 7.25F;
    const auto before = std::bit_cast<std::uint32_t>(spread);
    check(!apply_fixed_ads_spread_override(gate, &spread), message);
    check(std::bit_cast<std::uint32_t>(spread) == before,
          "rejected spread decision preserves native bits");
}

void test_fixed_ads_spread_override() {
    const auto gate = accepted_spread_gate();
    constexpr std::array<float, 4> native_spreads{2.0F, 4.0F, 8.0F, 12.0F};
    for (const float native : native_spreads) {
        float spread = native;
        check(apply_fixed_ads_spread_override(gate, &spread),
              "eligible local VR bullet receives ADS spread");
        check(spread == gate.ads_spread_degrees,
              "movement and firing cone growth cannot change final spread");
    }

    auto shotgun = gate;
    shotgun.ads_spread_degrees = 3.0F;
    float shotgun_spread = 11.0F;
    check(apply_fixed_ads_spread_override(shotgun, &shotgun_spread),
          "eligible shotgun spread is overridden");
    check(shotgun_spread == 3.0F,
          "authored nonzero ADS pellet cone remains nonzero");

    check(!apply_fixed_ads_spread_override(gate, nullptr),
          "null spread output is rejected");

    auto rejected = gate;
    rejected.hook_enabled = false;
    check_rejected_spread_is_unchanged(rejected, "disabled hook is rejected");
    rejected = gate;
    rejected.attacker = 0;
    check_rejected_spread_is_unchanged(rejected, "null attacker is rejected");
    rejected = gate;
    rejected.local_player_entity = 0;
    check_rejected_spread_is_unchanged(rejected, "null local entity is rejected");
    rejected = gate;
    rejected.attacker += 0x378;
    check_rejected_spread_is_unchanged(rejected, "AI attacker is rejected");
    rejected = gate;
    rejected.entity_number = 1;
    check_rejected_spread_is_unchanged(rejected, "nonlocal entity number is rejected");
    rejected = gate;
    rejected.has_client = false;
    check_rejected_spread_is_unchanged(rejected, "attacker without client is rejected");
    rejected = gate;
    rejected.weapon_type = 1;
    check_rejected_spread_is_unchanged(rejected, "projectile weapon is rejected");
    rejected = gate;
    rejected.ads_spread_degrees = -0.01F;
    check_rejected_spread_is_unchanged(rejected, "negative ADS spread is rejected");
    rejected = gate;
    rejected.ads_spread_degrees = std::numeric_limits<float>::infinity();
    check_rejected_spread_is_unchanged(rejected, "infinite ADS spread is rejected");
    rejected = gate;
    rejected.ads_spread_degrees = std::numeric_limits<float>::quiet_NaN();
    check_rejected_spread_is_unchanged(rejected, "NaN ADS spread is rejected");
    rejected = gate;
    rejected.ads_spread_degrees = kMaximumReasonableAdsSpreadDegrees + 1.0F;
    check_rejected_spread_is_unchanged(rejected, "implausible ADS spread is rejected");
}

void test_fixed_ads_visual_spread_override() {
    FixedAdsVisualSpreadGate gate{
        true,
        0,
        1.25F,
    };
    float minimum = 4.0F;
    float maximum = 9.0F;
    check(apply_fixed_ads_visual_spread_override(
              gate, &minimum, &maximum) &&
              minimum == 1.25F && maximum == 1.25F,
          "local tracer and predicted impact use the authored ADS cone");

    auto rejected = gate;
    rejected.weapon_type = 2;
    minimum = 4.0F;
    maximum = 9.0F;
    check(!apply_fixed_ads_visual_spread_override(
              rejected, &minimum, &maximum) &&
              minimum == 4.0F && maximum == 9.0F,
          "projectile visuals keep native non-hitscan spread values");
    rejected = gate;
    rejected.ads_spread_degrees =
        std::numeric_limits<float>::quiet_NaN();
    check(!apply_fixed_ads_visual_spread_override(
              rejected, &minimum, &maximum),
          "non-finite visual ADS spread fails closed");
    check(!apply_fixed_ads_visual_spread_override(
              gate, nullptr, &maximum) &&
              !apply_fixed_ads_visual_spread_override(
                  gate, &minimum, nullptr),
          "null visual spread outputs fail closed");
}

}  // namespace

int main() {
    test_publication_freshness();
    test_local_bullet_gate();
    test_authoritative_weapon_basis_override();
    test_hitscan_tag_flash_policy();
    test_exact_bazooka_weapon_root_launch_basis();
    test_bazooka_viewmodel_alignment();
    test_evaluated_projectile_basis();
    test_validated_bazooka_bore_basis();
    test_exact_bazooka_projectile_direction();
    test_physical_launcher_profiles();
    test_physical_launcher_visual_bounds();
    test_exact_bazooka_stable_missile_flag();
    test_fixed_ads_spread_override();
    test_fixed_ads_visual_spread_override();
    return 0;
}
