// SPDX-License-Identifier: GPL-3.0-only
#include "manual_reload_logic.hpp"
#include "rigid_submesh_extraction.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using wawvr::gameplay::ReloadEvent;
using wawvr::gameplay::ReloadInput;
using wawvr::gameplay::ReloadOrientationSource;
using wawvr::gameplay::ReloadPositionSource;
using wawvr::gameplay::ReloadProfileKind;
using wawvr::gameplay::ReloadStage;
using wawvr::gameplay::ReloadState;
using wawvr::gameplay::RigidSubmeshRange;
using wawvr::gameplay::extract_exact_rigid_submesh;
using wawvr::gameplay::make_reload_profile;
using wawvr::gameplay::update_manual_reload;

int failures = 0;

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] ReloadInput ready_input() {
    ReloadInput input{};
    input.enabled = true;
    input.focused = true;
    input.weapon_supported = true;
    input.can_reload = true;
    return input;
}

void test_native_and_deferred_profiles_never_take_ownership() {
    for (const auto kind : {ReloadProfileKind::NativeOnly,
                            ReloadProfileKind::SingleRoundOrTube}) {
        ReloadState state{};
        auto input = ready_input();
        input.reload_pressed_edge = true;
        const auto output =
            update_manual_reload(make_reload_profile(kind), input, &state);
        expect(output.stage == ReloadStage::Ready &&
                   !output.manual_reload_active &&
                   !output.suppress_native_automatic_commit &&
                   !output.request_native_action_open &&
                   !output.request_native_commit,
               "native and deferred profiles leave the stock reload path alone");
    }
}

void test_internal_clip_requires_open_action_and_current_hand_pose() {
    const auto profile =
        make_reload_profile(ReloadProfileKind::InternalStripperClip);
    ReloadState state{};
    auto input = ready_input();
    input.reload_pressed_edge = true;
    auto output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::ActionOpen &&
               output.event == ReloadEvent::ReloadArmed &&
               output.request_native_action_open &&
               output.suppress_native_automatic_commit,
           "stripper clip begins by requesting an open bolt/action");

    input = ready_input();
    input.native_action_open = true;
    output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::FeedDeviceAvailable &&
               output.event == ReloadEvent::FeedDeviceAvailable &&
               !output.render_detached_feed_device,
           "opened action makes a clip available without drawing a floating copy");

    input = ready_input();
    input.off_hand_pose_valid = true;
    input.grip_pressed_edge = true;
    input.grip_held = true;
    input.hand_in_feed_device_zone = true;
    output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::HoldingFeedDevice &&
               output.event == ReloadEvent::FeedDeviceGrabbed &&
               output.render_detached_feed_device &&
               output.hide_authored_feed_device &&
               output.position_source ==
                   ReloadPositionSource::CurrentTrackedOffHand &&
               output.orientation_source ==
                   ReloadOrientationSource::CurrentTrackedOffHand,
           "held clip is owned by the current tracked off-hand pose");

    input = ready_input();
    input.off_hand_pose_valid = true;
    input.grip_held = true;
    output = update_manual_reload(profile, input, &state);
    expect(output.position_source ==
                   ReloadPositionSource::CurrentTrackedOffHand &&
               output.orientation_source ==
                   ReloadOrientationSource::CurrentTrackedOffHand,
           "moving a held clip never falls back to a cached or rifle pose");

    input.hand_in_insertion_zone = true;
    output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::NearInsertionPoint &&
               output.event == ReloadEvent::InsertionAssistStarted &&
               output.position_source ==
                   ReloadPositionSource::CurrentTrackedOffHand &&
               output.orientation_source ==
                   ReloadOrientationSource::InsertionGuide,
           "insertion assist changes orientation without stealing position authority");

    output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::Committing &&
               output.event == ReloadEvent::CommitRequested &&
               output.request_native_commit &&
               !output.render_detached_feed_device,
           "stripper clip contact requests one native ammo commit and retires the clip");

    input = ready_input();
    output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::Committing &&
               output.event == ReloadEvent::None &&
               output.request_native_commit,
           "an unacknowledged ammo transfer keeps requesting a bounded native commit pulse");

    input = ready_input();
    input.native_commit_completed = true;
    output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::Ready &&
               output.event == ReloadEvent::CommitCompleted &&
               !output.manual_reload_active &&
               !output.suppress_native_automatic_commit,
           "native ammo completion returns the feed transaction to Ready while the physical bolt stays open");
}

void test_dropping_clip_outside_insertion_does_not_reload() {
    const auto profile =
        make_reload_profile(ReloadProfileKind::InternalStripperClip);
    ReloadState state{};
    auto input = ready_input();
    input.reload_pressed_edge = true;
    static_cast<void>(update_manual_reload(profile, input, &state));
    input = ready_input();
    input.native_action_open = true;
    static_cast<void>(update_manual_reload(profile, input, &state));
    input = ready_input();
    input.off_hand_pose_valid = true;
    input.grip_pressed_edge = true;
    input.grip_held = true;
    input.hand_in_feed_device_zone = true;
    static_cast<void>(update_manual_reload(profile, input, &state));

    input = ready_input();
    input.off_hand_pose_valid = true;
    input.grip_released_edge = true;
    const auto output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::FeedDeviceAvailable &&
               output.event == ReloadEvent::FeedDeviceDropped &&
               !output.request_native_commit &&
               !output.render_detached_feed_device,
           "dropping a clip away from the guide cannot commit ammo");
}

void test_detachable_magazine_commits_on_release_in_well() {
    const auto profile =
        make_reload_profile(ReloadProfileKind::DetachableMagazine);
    ReloadState state{};
    auto input = ready_input();
    input.reload_pressed_edge = true;
    auto output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::FeedDeviceAvailable &&
               output.request_native_action_open &&
               output.manual_reload_active &&
               output.hide_authored_feed_device &&
               !output.render_detached_feed_device,
           "detachable reload ejects the authored magazine and exposes a fresh belt magazine");

    input = ready_input();
    input.off_hand_pose_valid = true;
    input.grip_pressed_edge = true;
    input.grip_held = true;
    input.hand_in_feed_device_zone = true;
    static_cast<void>(update_manual_reload(profile, input, &state));

    input = ready_input();
    input.off_hand_pose_valid = true;
    input.grip_held = true;
    input.hand_in_insertion_zone = true;
    output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::NearInsertionPoint &&
               !output.request_native_commit &&
               output.hide_authored_feed_device &&
               output.render_detached_feed_device,
           "magazine insertion guide does not commit while grip remains held");

    input.grip_held = false;
    input.grip_released_edge = true;
    output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::Committing &&
               output.event == ReloadEvent::CommitRequested &&
               output.request_native_commit &&
               output.hide_authored_feed_device &&
               !output.render_detached_feed_device,
           "releasing the magazine in the well requests the native commit");

    input = ready_input();
    input.native_commit_completed = true;
    output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::Ready &&
               output.event == ReloadEvent::CommitCompleted &&
               !output.hide_authored_feed_device &&
               !output.manual_reload_active,
           "native commit completion restores the authored loaded magazine");
}

void test_detachable_drop_and_cancel_restore_visibility_safely() {
    const auto profile =
        make_reload_profile(ReloadProfileKind::DetachableMagazine);
    ReloadState state{};
    auto input = ready_input();
    input.reload_pressed_edge = true;
    static_cast<void>(update_manual_reload(profile, input, &state));

    input = ready_input();
    input.off_hand_pose_valid = true;
    input.grip_pressed_edge = true;
    input.grip_held = true;
    input.hand_in_feed_device_zone = true;
    static_cast<void>(update_manual_reload(profile, input, &state));

    input = ready_input();
    input.off_hand_pose_valid = true;
    input.grip_released_edge = true;
    auto output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::FeedDeviceAvailable &&
               output.event == ReloadEvent::FeedDeviceDropped &&
               output.hide_authored_feed_device &&
               !output.request_native_commit,
           "dropping a fresh magazine away from the well keeps the weapon empty and permits another draw");

    input = ready_input();
    input.focused = false;
    output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::Ready &&
               output.event == ReloadEvent::Cancelled &&
               output.request_native_cancel &&
               !output.hide_authored_feed_device,
           "focus loss cancels detachable ownership and restores authored visibility");
}

void test_focus_loss_and_context_change_cancel_safely() {
    const auto profile =
        make_reload_profile(ReloadProfileKind::InternalStripperClip);
    ReloadState state{};
    auto input = ready_input();
    input.reload_pressed_edge = true;
    static_cast<void>(update_manual_reload(profile, input, &state));
    const auto active_generation = state.generation;

    input = ready_input();
    input.focused = false;
    auto output = update_manual_reload(profile, input, &state);
    expect(output.stage == ReloadStage::Ready &&
               output.event == ReloadEvent::Cancelled &&
               output.request_native_cancel &&
               !output.render_detached_feed_device &&
               state.generation > active_generation,
           "focus loss cancels ownership and requests native recovery");

    input = ready_input();
    input.reload_pressed_edge = true;
    static_cast<void>(update_manual_reload(profile, input, &state));
    input = ready_input();
    input.reset_requested = true;
    output = update_manual_reload(profile, input, &state);
    expect(output.event == ReloadEvent::Cancelled &&
               output.request_native_cancel &&
               output.stage == ReloadStage::Ready,
           "weapon or profile changes cancel without hidden device state");
}

void test_null_state_is_fail_closed() {
    auto input = ready_input();
    input.reload_pressed_edge = true;
    const auto output = update_manual_reload(
        make_reload_profile(ReloadProfileKind::InternalStripperClip),
        input,
        nullptr);
    expect(output.stage == ReloadStage::Ready &&
               !output.manual_reload_active &&
               !output.request_native_action_open,
           "missing state cannot claim manual reload ownership");
}

constexpr std::uint16_t kJGunBone = 5;
constexpr std::uint16_t kJClipBone = 9;
constexpr std::uint16_t kJoint2Bone = 12;
constexpr std::uint32_t kColtSurfaceVertexCount = 252;
constexpr std::uint32_t kColtSurfaceTriangleCount = 202;
constexpr std::array<RigidSubmeshRange, 3> kColtRigidRanges{{
    {kJGunBone, 87, 0, 75},
    {kJClipBone, 75, 75, 55},
    {kJoint2Bone, 90, 130, 71},
}};

[[nodiscard]] std::vector<std::uint16_t> make_colt_surface_indices() {
    std::vector<std::uint16_t> indices(
        static_cast<std::size_t>(kColtSurfaceTriangleCount) * 3U,
        0);
    std::uint16_t first_vertex = 0;
    for (const RigidSubmeshRange& range : kColtRigidRanges) {
        const std::size_t first_index =
            static_cast<std::size_t>(range.triangle_offset) * 3U;
        const std::size_t index_count =
            static_cast<std::size_t>(range.triangle_count) * 3U;
        for (std::size_t index = 0; index < index_count; ++index) {
            indices[first_index + index] = static_cast<std::uint16_t>(
                first_vertex + index % range.vertex_count);
        }
        first_vertex = static_cast<std::uint16_t>(
            first_vertex + range.vertex_count);
    }
    indices[201U * 3U] = 251;
    indices[201U * 3U + 1U] = 251;
    indices[201U * 3U + 2U] = 251;
    return indices;
}

void test_colt_clip_rigid_submesh_is_exact_and_rebased() {
    const auto indices = make_colt_surface_indices();
    const auto extraction = extract_exact_rigid_submesh(
        kColtSurfaceVertexCount,
        kColtSurfaceTriangleCount,
        kColtRigidRanges,
        1,
        kJClipBone,
        indices);
    expect(extraction.has_value(),
           "exact Colt j_clip rigid topology is accepted");
    if (!extraction.has_value()) {
        return;
    }

    expect(extraction->bone_id == kJClipBone &&
               extraction->first_vertex == 87 &&
               extraction->vertex_count == 75 &&
               extraction->triangle_offset == 75 &&
               extraction->triangle_count == 55 &&
               extraction->triangle_indices.size() == 165,
           "Colt extraction selects exactly rigid 1 and its 165 indices");
    bool all_rebased = true;
    for (std::size_t index = 0;
         index < extraction->triangle_indices.size(); ++index) {
        all_rebased = all_rebased &&
                      extraction->triangle_indices[index] == index % 75U;
    }
    expect(all_rebased,
           "Colt j_clip indices are copied and rebased from vertex 87");
}

void test_rigid_submesh_rejects_selection_and_source_mismatches() {
    auto indices = make_colt_surface_indices();
    expect(!extract_exact_rigid_submesh(
                kColtSurfaceVertexCount,
                kColtSurfaceTriangleCount,
                kColtRigidRanges,
                kColtRigidRanges.size(),
                kJClipBone,
                indices),
           "out-of-range rigid selection fails closed");
    expect(!extract_exact_rigid_submesh(
                kColtSurfaceVertexCount,
                kColtSurfaceTriangleCount,
                kColtRigidRanges,
                1,
                kJGunBone,
                indices),
           "selected rigid bone mismatch fails closed");

    indices.pop_back();
    expect(!extract_exact_rigid_submesh(
                kColtSurfaceVertexCount,
                kColtSurfaceTriangleCount,
                kColtRigidRanges,
                1,
                kJClipBone,
                indices),
           "non-exact source triangle index count fails closed");
    expect(!extract_exact_rigid_submesh(
                0,
                kColtSurfaceTriangleCount,
                kColtRigidRanges,
                1,
                kJClipBone,
                indices),
           "empty surface fails closed");
}

void test_rigid_submesh_rejects_noncontiguous_or_mismatched_topology() {
    const auto indices = make_colt_surface_indices();

    auto ranges = kColtRigidRanges;
    ranges[1].triangle_offset = 76;
    expect(!extract_exact_rigid_submesh(
                kColtSurfaceVertexCount,
                kColtSurfaceTriangleCount,
                ranges,
                1,
                kJClipBone,
                indices),
           "triangle gap before the selected rigid fails closed");

    ranges = kColtRigidRanges;
    ranges[2].triangle_offset = 129;
    expect(!extract_exact_rigid_submesh(
                kColtSurfaceVertexCount,
                kColtSurfaceTriangleCount,
                ranges,
                1,
                kJClipBone,
                indices),
           "overlapping later rigid topology fails closed");

    ranges = kColtRigidRanges;
    --ranges[0].vertex_count;
    expect(!extract_exact_rigid_submesh(
                kColtSurfaceVertexCount,
                kColtSurfaceTriangleCount,
                ranges,
                1,
                kJClipBone,
                indices),
           "incomplete ordered rigid vertex coverage fails closed");

    ranges = kColtRigidRanges;
    ranges[2].vertex_count =
        (std::numeric_limits<std::uint32_t>::max)();
    expect(!extract_exact_rigid_submesh(
                kColtSurfaceVertexCount,
                kColtSurfaceTriangleCount,
                ranges,
                1,
                kJClipBone,
                indices),
           "overflowing rigid vertex accumulation fails closed");

    ranges = kColtRigidRanges;
    ranges[2].triangle_count = 73;
    expect(!extract_exact_rigid_submesh(
                kColtSurfaceVertexCount,
                kColtSurfaceTriangleCount,
                ranges,
                1,
                kJClipBone,
                indices),
           "rigid triangle range beyond the surface fails closed");
}

void test_rigid_submesh_rejects_foreign_selected_vertices() {
    auto indices = make_colt_surface_indices();
    constexpr std::size_t kClipIndexOffset = 75U * 3U;

    indices[kClipIndexOffset] = 86;
    expect(!extract_exact_rigid_submesh(
                kColtSurfaceVertexCount,
                kColtSurfaceTriangleCount,
                kColtRigidRanges,
                1,
                kJClipBone,
                indices),
           "selected triangle referencing the preceding rigid fails closed");

    indices = make_colt_surface_indices();
    indices[kClipIndexOffset + 1U] = 162;
    expect(!extract_exact_rigid_submesh(
                kColtSurfaceVertexCount,
                kColtSurfaceTriangleCount,
                kColtRigidRanges,
                1,
                kJClipBone,
                indices),
           "selected triangle referencing the following rigid fails closed");

    indices = make_colt_surface_indices();
    indices[0] = 87;
    expect(!extract_exact_rigid_submesh(
                kColtSurfaceVertexCount,
                kColtSurfaceTriangleCount,
                kColtRigidRanges,
                1,
                kJClipBone,
                indices),
           "prefix triangle referencing the selected rigid fails closed");

    indices = make_colt_surface_indices();
    indices[130U * 3U] = 161;
    expect(!extract_exact_rigid_submesh(
                kColtSurfaceVertexCount,
                kColtSurfaceTriangleCount,
                kColtRigidRanges,
                1,
                kJClipBone,
                indices),
           "later rigid triangle referencing the selected rigid fails closed");

    const std::array<RigidSubmeshRange, 2> overflowing_ranges{{
        {kJGunBone,
         (std::numeric_limits<std::uint32_t>::max)(),
         0,
         1},
        {kJClipBone, 1, 1, 1},
    }};
    const std::array<std::uint16_t, 6> small_indices{};
    expect(!extract_exact_rigid_submesh(
                (std::numeric_limits<std::uint32_t>::max)(),
                2,
                overflowing_ranges,
                1,
                kJClipBone,
                small_indices),
           "unaddressable surface and cumulative vertex overflow fail closed");
}

}  // namespace

int main() {
    test_native_and_deferred_profiles_never_take_ownership();
    test_internal_clip_requires_open_action_and_current_hand_pose();
    test_dropping_clip_outside_insertion_does_not_reload();
    test_detachable_magazine_commits_on_release_in_well();
    test_detachable_drop_and_cancel_restore_visibility_safely();
    test_focus_loss_and_context_change_cancel_safely();
    test_null_state_is_fail_closed();
    test_colt_clip_rigid_submesh_is_exact_and_rebased();
    test_rigid_submesh_rejects_selection_and_source_mismatches();
    test_rigid_submesh_rejects_noncontiguous_or_mismatched_topology();
    test_rigid_submesh_rejects_foreign_selected_vertices();

    if (failures != 0) {
        std::cerr << failures << " manual-reload logic test(s) failed\n";
        return 1;
    }
    std::cout << "manual-reload logic tests passed\n";
    return 0;
}
