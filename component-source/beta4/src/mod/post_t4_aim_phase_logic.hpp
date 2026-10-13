// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"

#include <cstdint>

namespace wawvr::mod {

inline constexpr std::uint64_t kPostT4AimPhaseMaximumPublicationGapMs =
    250;

// State for a diagnostic-only one-publication phase comparison. It never
// participates in weapon placement and can be discarded at any time.
struct PostT4AimPhaseState final {
    bool valid{};
    std::uint64_t generation{};
    std::uint64_t frame_id{};
    std::uint64_t publication_milliseconds{};
    wawvr::xr::Quaternionf tracking_anchor_orientation{};
    // Immutable controller-to-weapon orientation supplied by live placement.
    // It is never inferred from a potentially lagged raw/applied sample.
    wawvr::xr::Basis3f weapon_attachment_axis{};
    // Kept in world space so lag-1 is re-expressed in the next publication's
    // current HMD basis instead of comparing two different local frames.
    wawvr::xr::Vec3f previous_predicted_applied_world{};
};

struct PostT4AimPhaseInput final {
    std::uint64_t generation{};
    std::uint64_t frame_id{};
    std::uint64_t publication_milliseconds{};
    wawvr::xr::Posef head_pose{};
    wawvr::xr::Posef tracking_anchor{};
    wawvr::xr::Basis3f body_axis{};
    // Exact pre-attachment controller pose consumed by production placement,
    // already composed into world space. For two-hand placement this is the
    // stabilized right anchor/roll plus current raw-left steering pose.
    wawvr::xr::Basis3f production_controller_world_axis{};
    wawvr::xr::Basis3f weapon_attachment_axis{};
    // Evaluated post-T4 grip-to-flash direction, observed after native pose
    // evaluation rather than the root axis written by placement.
    wawvr::xr::Vec3f visible_world_direction{};
};

struct PostT4AimPhaseObservation final {
    bool compared{};
    bool lag1_better{};
    float lag0_error_degrees{};
    float lag1_error_degrees{};
    float publication_delta_milliseconds{};
};

// Converts a world-space direction into the current HMD-centre basis. The
// tracked head and body basis are supplied by the same immutable OpenXR
// publication as the hands. This is a diagnostic transform only.
[[nodiscard]] bool post_t4_world_direction_to_head_local(
    const wawvr::xr::Basis3f& body_axis,
    const wawvr::xr::Posef& head_pose,
    const wawvr::xr::Posef& tracking_anchor,
    const wawvr::xr::Vec3f& world_direction,
    wawvr::xr::Vec3f* head_local_direction) noexcept;

// Produces lag-0 (post-T4 visible bore versus the current production controller
// after applying live placement's immutable attachment) and lag-1 (visible bore
// versus the preceding publication's predicted world pose, re-expressed in the
// current HMD basis) angular errors. Only exact consecutive publications
// compare. Gaps, regressions, frame discontinuities, attachment changes, and
// tracking-anchor rebases reseed without producing a sample. The caller logs
// the fixed visible-bore/root offset separately; this helper never derives a
// moving-seed calibration that could absorb an already-present frame delay.
[[nodiscard]] bool update_post_t4_aim_phase(
    const PostT4AimPhaseInput& input,
    PostT4AimPhaseState* state,
    PostT4AimPhaseObservation* observation) noexcept;

}  // namespace wawvr::mod
