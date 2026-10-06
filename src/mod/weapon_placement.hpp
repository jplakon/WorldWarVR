// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "controller_state.hpp"

#include "xr_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace wawvr::mod {

// Camera-local controller pose used by the viewmodel attachment. Position is
// taken from the OpenXR grip pose while orientation is taken from the aim pose.
struct ControllerWeaponPose final {
    wawvr::xr::Vec3f grip_position{};
    wawvr::xr::Basis3f aim_axis{};
};

using RightControllerWeaponPose = ControllerWeaponPose;

// Converts the immutable publication-owned filtered pose for one hand into
// the same tracking-anchor-local weapon frame used by stereo.
[[nodiscard]] bool published_controller_weapon_pose(
    const ControllerFrameSnapshot& snapshot,
    wawvr::xr::Hand hand,
    std::uint64_t now_milliseconds,
    ControllerWeaponPose* pose) noexcept;

// Converts a pose expressed in a stable source frame into a child reference
// frame expressed by reference_in_source. This is H^-1 * P in IW row-basis
// coordinates and deliberately performs no filtering; callers can therefore
// stabilize a two-hand sight line before removing the moving HMD transform.
// The output is committed only after every input and result is validated.
[[nodiscard]] bool rebase_controller_weapon_pose_to_reference(
    const wawvr::xr::EnginePose& reference_in_source,
    const ControllerWeaponPose& source_pose,
    ControllerWeaponPose* pose) noexcept;

// Builds one controller pose relative to the exact head sample in the same XR
// frame. This path removes common head-and-hand motion before any filtering.
[[nodiscard]] bool current_head_local_controller_weapon_pose(
    const ControllerFrameSnapshot& snapshot,
    wawvr::xr::Hand hand,
    std::uint64_t now_milliseconds,
    ControllerWeaponPose* pose) noexcept;

// Reads the immutable publication-thread result whose EMA history is already
// current-head-local. The pose must be composed through that frame's head-world
// basis, never through the body/tracking-anchor camera basis.
[[nodiscard]] bool published_current_head_local_controller_weapon_pose(
    const ControllerFrameSnapshot& snapshot,
    wawvr::xr::Hand hand,
    std::uint64_t now_milliseconds,
    ControllerWeaponPose* pose) noexcept;

// Reproduces COD4's accepted ordering exactly: filter the controller grip and
// aim in OpenXR tracking space first, then remove the current frame's HMD pose.
// The returned pose must be composed through that same frame's head-world
// basis. This differs intentionally from filtering H^-1*C after head removal.
[[nodiscard]] bool published_cod4_current_head_local_controller_weapon_pose(
    const ControllerFrameSnapshot& snapshot,
    wawvr::xr::Hand hand,
    std::uint64_t now_milliseconds,
    ControllerWeaponPose* pose) noexcept;

// COD4's accepted two-hand rifle contract keeps the right grip as the
// positional anchor and uses only the direction to the left grip to steer the
// barrel. The retained qualification envelope is deliberately used here so a
// valid pair pose does not disappear at COD4's tighter engage boundary.
inline constexpr float kCod4TwoHandMinimumSeparationIwUnits = 3.0F;
inline constexpr float kCod4TwoHandMaximumSeparationIwUnits = 36.0F;

// Synthesizes COD4's controller pose for a two-hand rifle. The right aim basis
// supplies the roll hint; left aim orientation is intentionally irrelevant.
// The output is committed only after a finite, plausible, orthonormal pose has
// been constructed, so callers can safely retain their prior pose on failure.
[[nodiscard]] bool two_hand_controller_weapon_pose(
    const ControllerWeaponPose& right,
    const ControllerWeaponPose& left,
    ControllerWeaponPose* pose) noexcept;

// Live COD4-parity selection keeps the stabilized weapon hand as the rigid
// rear anchor/roll hint and consumes the current raw support-hand position
// without a second direction filter. Both independently validated hand paths
// and the coherent raw pair must remain usable; otherwise the caller retains
// its previously committed weapon pose instead of rendering a tracking jump.
[[nodiscard]] bool validated_cod4_two_hand_controller_weapon_pose(
    bool right_pose_ready,
    bool left_pose_ready,
    bool raw_pair_ready,
    const ControllerWeaponPose& stabilized_right,
    const ControllerWeaponPose& raw_left,
    ControllerWeaponPose* pose) noexcept;

// Builds the pair direction from the same-frame raw hand-to-hand delta while
// retaining the stabilized right-hand anchor and roll. This removes steering
// lag without coupling common hand translation into a false pitch/yaw change:
// both raw positions cancel before the delta is attached to the filtered root.
[[nodiscard]] bool two_hand_controller_weapon_pose_from_raw_grip_delta(
    const ControllerWeaponPose& stabilized_right,
    const ControllerWeaponPose& raw_right,
    const ControllerWeaponPose& raw_left,
    ControllerWeaponPose* pose) noexcept;

// COD4 VR filters the weapon-hand grip and aim pose before converting it into
// the engine frame. Keep the same accepted per-publication responses here so
// raw Touch tracking shimmer does not become visible motion on every weapon.
// The state stores the filtered OpenXR-space values and advances at most once
// per published XR generation; repeated viewmodel/eye consumers of that
// generation therefore receive one identical stabilized sample.
inline constexpr float kWeaponPositionResponse = 0.45F;
inline constexpr float kWeaponOrientationResponse = 0.55F;
// The physical trace showed that the remaining visible motion is already in
// the accepted controller pose.  Preserve COD4's response for deliberate
// movement, but lower the response continuously near rest so sub-millimetre
// positional noise and sub-degree aim shimmer do not become sight motion.
// The response ramps from quiet to COD4-fast as error accumulates, so a real
// movement quickly escapes the quiet region instead of feeling sticky.
inline constexpr float kQuietWeaponPositionResponse = 0.18F;
inline constexpr float kQuietWeaponOrientationResponse = 0.18F;
inline constexpr float kQuietWeaponPositionErrorMeters = 0.0015F;
inline constexpr float kFastWeaponPositionErrorMeters = 0.0120F;
inline constexpr float kQuietWeaponOrientationErrorDegrees = 0.20F;
inline constexpr float kFastWeaponOrientationErrorDegrees = 2.00F;
inline constexpr float kMaximumWeaponPositionDiscontinuityMeters = 0.50F;
inline constexpr float kMaximumWeaponOrientationDiscontinuityDegrees = 120.0F;

[[nodiscard]] float adaptive_weapon_position_response(
    float error_meters) noexcept;

[[nodiscard]] float adaptive_weapon_orientation_response(
    float error_degrees) noexcept;

struct ControllerWeaponPoseFilterState final {
    bool valid{};
    std::uint64_t generation{};
    std::uint64_t publication_milliseconds{};
    wawvr::xr::Vec3f filtered_grip_position{};
    wawvr::xr::Quaternionf filtered_aim_orientation{};
};

// COD4 eases only the transition between the stabilized dominant-hand basis
// and the hand-line basis.  WaW's physical trace showed that a raw support-hand
// endpoint amplified independent Touch tracking noise, so the live caller now
// supplies the already-filtered support endpoint while retaining both raw
// poses solely as coherent same-frame validity gates.
inline constexpr float kCod4TwoHandEngageResponse = 0.22F;
inline constexpr float kCod4TwoHandReleaseResponse = 0.18F;
inline constexpr float kCod4TwoHandBlendSnapMinimum = 0.001F;
inline constexpr float kCod4TwoHandBlendSnapMaximum = 0.999F;

struct Cod4TwoHandEngagementBlendState final {
    bool valid{};
    std::uint64_t generation{};
    wawvr::xr::Quaternionf tracking_anchor_orientation{};
    float blend{};
    ControllerWeaponPose cached_pose{};
};

// Reproduces COD4's render-time two-hand basis blend. The pair target is the
// current stabilized support-hand position relative to the stabilized
// dominant-hand anchor. Position remains the stabilized dominant-hand anchor
// for every blend value; raw_right is retained only as the coherent-pair
// validity gate.
[[nodiscard]] bool cod4_two_hand_blended_controller_weapon_pose(
    const ControllerWeaponPose& stabilized_right,
    const ControllerWeaponPose& raw_right,
    const ControllerWeaponPose& stabilized_left,
    float blend,
    ControllerWeaponPose* pose) noexcept;

// Advances COD4's fixed per-publication engagement response at most once per
// controller generation. Repeated consumers receive the cached pose and blend
// exactly. Invalid or regressed input fails closed without mutating state or
// output; explicit reset is the required reseed path.
[[nodiscard]] bool update_cod4_two_hand_engagement_pose(
    std::uint64_t generation,
    bool target_active,
    const wawvr::xr::Quaternionf& tracking_anchor_orientation,
    const ControllerWeaponPose& stabilized_right,
    const ControllerWeaponPose& raw_right,
    const ControllerWeaponPose& stabilized_left,
    Cod4TwoHandEngagementBlendState* state,
    ControllerWeaponPose* pose) noexcept;

void reset_cod4_two_hand_engagement_blend(
    Cod4TwoHandEngagementBlendState* state) noexcept;

// The two-hand direction is derived from the distance between independently
// tracked grips, so millimetre-scale differential tracking error is amplified
// into visible sight rotation. Filter that normalized direction continuously
// with a frame-rate-independent exponential half-life. There is deliberately
// no angular bypass: both small tracking shimmer and deliberate motion pass
// through the same continuous response. The current pair basis remains the
// roll hint, and position always remains the exact current right-grip anchor.
inline constexpr float kTwoHandDirectionFilterHalfLifeMilliseconds = 12.0F;

struct TwoHandWeaponOrientationFilterState final {
    bool valid{};
    std::uint64_t generation{};
    std::uint64_t publication_milliseconds{};
    wawvr::xr::Quaternionf tracking_anchor_orientation{};
    wawvr::xr::Vec3f previous_raw_direction{};
    wawvr::xr::Vec3f filtered_direction{};
    wawvr::xr::Basis3f cached_axis{};
};

// The right controller's aim ray is the only independently proven rigid
// weapon signal.  A conventional absolute hand-to-hand line replaces that
// ray with the difference between two noisy tracked positions, which turns
// ordinary differential tracking error into visible angular motion.  Keep the
// right ray authoritative and let the support hand steer only relative to the
// controller-local direction at which it first joined the rifle.
//
// A second tracked position is not allowed to perturb the proven right-hand ray
// until it demonstrates a coherent support-hand steering gesture.  The quiet
// and arming phases are exact sample-and-hold clutches: neither can mutate the
// accepted bore.  Steering is an absolute mapping from fixed event anchors and
// is slew limited, so frame count and alternating tracking noise cannot ratchet
// the weapon away from the right ray.
inline constexpr float kRightRayTwoHandIntentEngageDegrees = 3.0F;
inline constexpr float kRightRayTwoHandIntentReleaseDegrees = 1.0F;
inline constexpr float kRightRayTwoHandMinimumIntentSpeedDegreesPerSecond =
    20.0F;
inline constexpr float kRightRayTwoHandIntentDirectionCoherenceCosine =
    0.8660254F;
inline constexpr std::uint64_t kRightRayTwoHandIntentArmMilliseconds = 50;
inline constexpr float kRightRayTwoHandSettleSpeedDegreesPerSecond = 6.0F;
inline constexpr std::uint64_t kRightRayTwoHandSettleMilliseconds = 75;
inline constexpr float kRightRayTwoHandSettleToleranceDegrees = 0.25F;
inline constexpr float kRightRayTwoHandMaximumSlewDegreesPerSecond = 120.0F;
inline constexpr std::uint64_t
    kRightRayTwoHandMaximumIntentSampleIntervalMilliseconds = 50;
inline constexpr float kRightRayTwoHandMaximumCorrectionDegrees = 30.0F;

enum class RightRayTwoHandSteeringPhase : std::uint8_t {
    Quiet,
    Arming,
    Steering,
    Converging,
    Settling,
};

struct RightRayTwoHandSteeringState final {
    bool valid{};
    std::uint64_t generation{};
    std::uint64_t publication_milliseconds{};
    wawvr::xr::Quaternionf tracking_anchor_orientation{};
    RightRayTwoHandSteeringPhase phase{
        RightRayTwoHandSteeringPhase::Quiet};
    wawvr::xr::Vec3f quiet_input_anchor_right_local{};
    wawvr::xr::Vec3f event_input_anchor_right_local{};
    wawvr::xr::Vec3f event_output_forward_right_local{};
    wawvr::xr::Vec3f accepted_forward_right_local{};
    wawvr::xr::Vec3f converging_target_forward_right_local{};
    wawvr::xr::Vec3f previous_support_direction_right_local{};
    wawvr::xr::Vec3f arming_axis_right_local{};
    wawvr::xr::Vec3f settling_input_anchor_right_local{};
    std::uint64_t arming_elapsed_milliseconds{};
    std::uint64_t settling_elapsed_milliseconds{};
    ControllerWeaponPose cached_pose{};
};

// Builds a rigid two-hand pose around the stabilized right-controller ray.
// raw_right/raw_left provide one coherent same-frame delta and raw_right basis,
// so common hand translation and rotation cancel before the support constraint
// is evaluated even when the stabilized visible ray lags the raw controller.
// The first valid pair latches the support direction and returns
// stabilized_right exactly; later support motion must sustain a coherent,
// sufficiently fast gesture before it can steer. Repeated consumers of one
// generation receive the identical cached result.
[[nodiscard]] bool update_right_ray_two_hand_steering_pose(
    std::uint64_t generation,
    std::uint64_t publication_milliseconds,
    const wawvr::xr::Quaternionf& tracking_anchor_orientation,
    const ControllerWeaponPose& stabilized_right,
    const ControllerWeaponPose& raw_right,
    const ControllerWeaponPose& raw_left,
    RightRayTwoHandSteeringState* state,
    ControllerWeaponPose* pose) noexcept;

// A pistol support palm cups the firing hand; it is not a rifle fore-end
// lever. Keep the same publication/transition contract but return the stable
// right ray without deriving a direction or minimum distance between hands.
// Tracking validity remains the caller's existing requirement. This helper is
// selected only for exact verified pistol identities by the viewmodel hook.
[[nodiscard]] bool update_pistol_two_hand_support_pose(
    std::uint64_t generation,
    std::uint64_t publication_milliseconds,
    const wawvr::xr::Quaternionf& tracking_anchor_orientation,
    const ControllerWeaponPose& stabilized_right,
    const ControllerWeaponPose& raw_right,
    const ControllerWeaponPose& raw_left,
    RightRayTwoHandSteeringState* state,
    ControllerWeaponPose* pose) noexcept;

void reset_right_ray_two_hand_steering(
    RightRayTwoHandSteeringState* state) noexcept;

// Persistent controller-to-viewmodel offset captured from T4's normal Colt
// placement. The orientation calibration is deliberately canonical rather
// than dependent on how the controller happened to be held at startup.
struct WeaponAttachmentState final {
    bool valid{};
    wawvr::xr::Vec3f position{};
    wawvr::xr::Basis3f axis{};
};

// Captures an exact controller-relative attachment from an already placed
// weapon. Applying the resulting attachment with the same controller pose
// reproduces the input origin and axis exactly, which prevents a visible jump
// or roll during a right-to-left (or left-to-right) ownership handoff.
[[nodiscard]] bool calibrate_controller_weapon_attachment(
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const ControllerWeaponPose& controller,
    const wawvr::xr::Vec3f& weapon_origin,
    const wawvr::xr::Basis3f& weapon_axis,
    WeaponAttachmentState* attachment) noexcept;

// Transfers ownership without depending on a retained world-space root. The
// outgoing controller-local attachment is first evaluated in the current
// camera/tracking reference, then the incoming controller is calibrated from
// that exact visible pose. Outputs are committed only after both operations
// succeed, so an old incoming attachment can never leak into the handoff.
[[nodiscard]] bool transfer_controller_weapon_attachment(
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const ControllerWeaponPose& outgoing_controller,
    const WeaponAttachmentState& outgoing_attachment,
    const ControllerWeaponPose& incoming_controller,
    WeaponAttachmentState* incoming_attachment,
    wawvr::xr::Vec3f* weapon_origin,
    wawvr::xr::Basis3f* weapon_axis) noexcept;

// Reorients a controller-local attachment so the weapon basis follows the
// controller basis exactly. A valid attachment retains its positional offset;
// a fresh attachment captures only the supplied root position. The operation
// is transactional and never mutates the supplied weapon origin.
[[nodiscard]] bool align_controller_weapon_attachment_to_controller_forward(
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const ControllerWeaponPose& controller,
    const wawvr::xr::Vec3f& weapon_origin,
    WeaponAttachmentState* attachment) noexcept;

// A chest-holstered rifle is deliberately diagonal, but that display pose is
// not a controller calibration. A fresh pickup clears any previous owner
// transform and restores the canonical camera-forward basis before the first
// controller-relative attachment is built.
[[nodiscard]] bool prepare_controller_forward_weapon_pickup(
    const wawvr::xr::Basis3f& camera_axis,
    WeaponAttachmentState* attachment,
    wawvr::xr::Basis3f* weapon_axis) noexcept;

// Builds C_current relative to H_anchor. T4's viewmodel refdef is stock here;
// the stereo scene hook applies HMD transforms later to temporary eye refdefs
// and restores the stock global.
[[nodiscard]] bool right_controller_weapon_pose(
    const ControllerFrameSnapshot& snapshot,
    std::uint64_t now_milliseconds,
    RightControllerWeaponPose* pose) noexcept;

[[nodiscard]] bool controller_weapon_pose(
    const ControllerFrameSnapshot& snapshot,
    wawvr::xr::Hand hand,
    std::uint64_t now_milliseconds,
    ControllerWeaponPose* pose) noexcept;

// Applies COD4-parity tracking-shimmer rejection before producing the same
// camera-local weapon contract as controller_weapon_pose(). An invalid frame
// contract resets history. Transient unavailable hand poses and implausible
// discontinuities fail closed while retaining the last accepted sample so a
// relocalized controller cannot become an unchecked first sample on the next
// call. The owning hook explicitly resets only after bounded recovery retires
// that hand. A repeated call for one snapshot generation recomposes against
// the current anchor without advancing the filter again.
[[nodiscard]] bool stabilized_controller_weapon_pose(
    const ControllerFrameSnapshot& snapshot,
    wawvr::xr::Hand hand,
    std::uint64_t now_milliseconds,
    ControllerWeaponPoseFilterState* state,
    ControllerWeaponPose* pose) noexcept;

void reset_controller_weapon_pose_filter(
    ControllerWeaponPoseFilterState* state) noexcept;

// A hand that joins an already moving rifle cannot be sampled from a fresh raw
// pose while the incumbent hand remains on an older filtered sample. That
// asymmetric age turns a common upward/forward motion into a false change in
// the hand-line angle. Rebaseline both filters from one XR publication and
// build the pair transactionally; a rejected hand or pair leaves the caller's
// established filter history untouched.
[[nodiscard]] bool rebaseline_two_hand_controller_weapon_poses(
    const ControllerFrameSnapshot& snapshot,
    std::uint64_t now_milliseconds,
    std::array<ControllerWeaponPoseFilterState, wawvr::xr::kHandCount>*
        states,
    ControllerWeaponPose* right_pose,
    ControllerWeaponPose* left_pose,
    ControllerWeaponPose* two_hand_pose) noexcept;

// Publishes the current derived two-hand direction through a persistent,
// frame-rate-independent exponential filter. The filtered forward vector is
// combined with the current pair's up/left roll hint, so dominant-hand roll is
// retained without reintroducing raw differential pitch/yaw shimmer. A
// generation/publication pair advances history at most once, repeated render
// consumers receive the cached orientation exactly, and every successful call
// copies raw_pair.grip_position without filtering. Regressed keys, stale
// publication gaps, and implausible adjacent direction discontinuities fail
// closed without mutating established history or caller output. The owning
// hook's bounded tracking-failure retirement calls the explicit reset below,
// which is the required reseed path after such a rejected discontinuity. A
// tracking-anchor orientation change is part of the cache key even when the XR
// generation/publication key is unchanged; it reseeds immediately because the
// game camera applies the equal-and-opposite body-yaw transfer in that frame.
[[nodiscard]] bool stabilized_two_hand_controller_weapon_pose(
    std::uint64_t generation,
    std::uint64_t publication_milliseconds,
    const wawvr::xr::Quaternionf& tracking_anchor_orientation,
    const ControllerWeaponPose& raw_pair,
    TwoHandWeaponOrientationFilterState* state,
    ControllerWeaponPose* pose) noexcept;

void reset_two_hand_weapon_orientation_filter(
    TwoHandWeaponOrientationFilterState* state) noexcept;

// Rigid controller attachment in IW world-space rows (forward/left/up). On
// first use the function captures the normal viewmodel offset; subsequent
// calls move that attachment with the right controller.
[[nodiscard]] bool apply_right_controller_weapon_placement(
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const RightControllerWeaponPose& controller,
    WeaponAttachmentState* attachment,
    wawvr::xr::Vec3f* weapon_origin,
    wawvr::xr::Basis3f* weapon_axis) noexcept;

[[nodiscard]] bool apply_controller_weapon_placement(
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const ControllerWeaponPose& controller,
    WeaponAttachmentState* attachment,
    wawvr::xr::Vec3f* weapon_origin,
    wawvr::xr::Basis3f* weapon_axis) noexcept;

// Converts the tracked grip from camera-local IW coordinates into the stock
// refdef's world space. The viewmodel's grip bone is aligned to this point
// after T4 has evaluated the normal weapon/arms animation.
[[nodiscard]] bool right_controller_grip_world(
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const RightControllerWeaponPose& controller,
    wawvr::xr::Vec3f* grip_world) noexcept;

[[nodiscard]] bool controller_grip_world(
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const ControllerWeaponPose& controller,
    wawvr::xr::Vec3f* grip_world) noexcept;

// Applies the second-stage grip correction to the T4 viewmodel root.
// Implausibly large or non-finite tag deltas fail closed and preserve the
// stock pose.
[[nodiscard]] bool align_viewmodel_origin_to_grip(
    const wawvr::xr::Vec3f& tracked_grip_world,
    const wawvr::xr::Vec3f& viewmodel_grip_tag_world,
    wawvr::xr::Vec3f* viewmodel_origin) noexcept;

// Clean-room layout of one cached T4 viewmodel bone transform. The engine's
// world-tag path indexes these 0x20-byte records and reads translation at
// +0x10 after proving the corresponding skeleton bit is current.
struct EvaluatedViewmodelBoneTransform final {
    float quaternion[4]{};
    wawvr::xr::Vec3f translation{};
    float translation_weight{};
};

inline constexpr std::size_t kViewmodelSkeletonBitWordCount = 4;
inline constexpr std::size_t kMaximumViewmodelBoneCount = 128;

// Applies one validated root translation to a private copy of T4's evaluated
// skeleton. Only transforms whose MSB-first skeleton bit is set are moved;
// unevaluated transforms are preserved so the engine can construct them from
// the corrected pose later. Nothing is committed when an input, selected
// transform, result, or bit outside the supplied matrix range is invalid.
[[nodiscard]] bool translate_evaluated_viewmodel_skeleton(
    const wawvr::xr::Vec3f& original_viewmodel_origin,
    const wawvr::xr::Vec3f& corrected_viewmodel_origin,
    const std::array<std::uint32_t, kViewmodelSkeletonBitWordCount>&
        evaluated_bones,
    std::span<EvaluatedViewmodelBoneTransform> bone_transforms,
    wawvr::xr::Vec3f* pose_origin) noexcept;

// Verifies the native world-tag result after a cached-matrix translation.
// Equivalent floating-point association orders may differ by a small number
// of ULPs at large map coordinates, so comparison is component-local rather
// than a fixed world-space epsilon.
[[nodiscard]] bool translated_viewmodel_tag_matches(
    const wawvr::xr::Vec3f& expected,
    const wawvr::xr::Vec3f& observed) noexcept;

// T4's GfxPlacement stores a unit quaternion. This conversion follows the
// engine's AxisToQuat row convention and is kept independent of proprietary
// engine calls so the hook remains testable and fail-closed.
[[nodiscard]] bool iw_axis_to_unit_quaternion(
    const wawvr::xr::Basis3f& axis,
    wawvr::xr::Quaternionf* quaternion) noexcept;

}  // namespace wawvr::mod
