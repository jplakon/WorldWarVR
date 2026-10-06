// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace wawvr::xr
{

constexpr std::uint32_t kEyeCount = 2;
constexpr std::uint32_t kHandCount = 2;

enum class LogLevel : std::uint8_t
{
    Debug,
    Info,
    Warning,
    Error,
};

using LogCallback = void (*)(
    void* user_data,
    LogLevel level,
    const char* message);

struct HostCallbacks
{
    void* user_data = nullptr;
    LogCallback log = nullptr;
};

struct Vec2f
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3f
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Quaternionf
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct Posef
{
    Quaternionf orientation = {};
    Vec3f position = {};
};

struct Fovf
{
    float angle_left = 0.0f;
    float angle_right = 0.0f;
    float angle_up = 0.0f;
    float angle_down = 0.0f;
};

// Rows follow the IW engine convention: forward, left, up.
struct Basis3f
{
    Vec3f forward = {1.0f, 0.0f, 0.0f};
    Vec3f left = {0.0f, 1.0f, 0.0f};
    Vec3f up = {0.0f, 0.0f, 1.0f};
};

struct EnginePose
{
    Vec3f position = {};
    Basis3f axis = {};
};

struct BoolActionState
{
    bool active = false;
    bool current = false;
    bool changed = false;
};

struct FloatActionState
{
    bool active = false;
    float current = 0.0f;
    bool changed = false;
};

struct Vec2ActionState
{
    bool active = false;
    Vec2f current = {};
    bool changed = false;
};

struct TrackedPose
{
    bool active = false;
    bool orientation_valid = false;
    bool position_valid = false;
    bool orientation_tracked = false;
    bool position_tracked = false;
    Posef pose = {};
};

enum class Hand : std::uint8_t
{
    Left = 0,
    Right = 1,
};

struct HandActionState
{
    TrackedPose grip = {};
    TrackedPose aim = {};
    FloatActionState trigger = {};
    BoolActionState trigger_click = {};
    FloatActionState squeeze = {};
    Vec2ActionState stick = {};
    BoolActionState primary = {};
    BoolActionState secondary = {};
    BoolActionState stick_click = {};
    BoolActionState thumbrest = {};
};

struct ActionSnapshot
{
    bool focused = false;
    std::uint64_t sequence = 0;
    HandActionState hands[kHandCount] = {};
    BoolActionState menu = {};
};

struct EyeView
{
    Posef pose = {};
    Fovf fov = {};
};

struct FrameState
{
    std::uint64_t frame_id = 0;
    std::int64_t predicted_display_time = 0;
    std::int64_t predicted_display_period = 0;
    bool should_render = false;
    bool views_valid = false;
    EyeView eyes[kEyeCount] = {};
    Posef head_center = {};
    ActionSnapshot actions = {};
};

struct EyeRenderTarget
{
    ID3D11RenderTargetView* render_target = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t image_index = 0;
    bool target_is_srgb = false;
};

struct D3D11SourceFrame
{
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t serial = 0;
    bool pixels_are_srgb_encoded = true;
    bool rendered_views_valid = false;
    EyeView rendered_eyes[kEyeCount] = {};
};

// A finite, world-space composition quad. Its pose is expressed in the
// runtime's application reference space. OpenXR defines the quad's visible
// front normal as local +Z and its width/height along local X/Y.
struct QuadLayer
{
    Posef pose = {};
    Vec2f size_meters = {};
    // The existing eye swapchains are also valid core quad-layer images.
    // Mono presentation renders identical content to both, so eye zero is
    // normally selected and exposed to both eyes by the OpenXR compositor.
    std::uint32_t source_eye = 0;
};

// The caller selects the OpenXR composition primitive explicitly. In
// particular, a missing finite menu quad must never be reinterpreted as a
// projection layer, because that turns a room panel back into a
// translation-infinite screen.
enum class CompositionLayerKind : std::uint8_t
{
    none,
    projection,
    quad,
};

struct NormalizedRect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

// A normalized destination viewport. Unlike a source rectangle, this is
// deliberately allowed to extend outside [0, 1]: D3D clips the oversized
// viewport to the eye swapchain. That is how a symmetric engine projection is
// remapped into an asymmetric OpenXR eye frustum without moving the source
// image's optical centre.
struct NormalizedViewport
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

// Metadata for the COD4-style physical rifle optic. The source is a dedicated
// square camera packed beside the two engine eye views. Lens vectors are
// expressed directly in OpenXR reference space. This keeps the optic rigidly
// attached to the rendered rifle while the player translates or tilts their
// head; the compositor projects the same physical points into both eyes.
struct PhysicalScopeComposition
{
    bool active = false;
    NormalizedRect source = {};
    Vec3f lens_origin_reference_meters = {};
    Vec3f lens_right_reference = {1.0f, 0.0f, 0.0f};
    Vec3f lens_up_reference = {0.0f, 1.0f, 0.0f};
    float lens_radius_meters = 0.032f;
};

// A capture-time world target, not a head-locked UI cursor. The source owner
// must convert the native aim point into the same OpenXR reference space as
// the captured eye poses. Both eyes project this one physical point.
struct WorldAimMarkerComposition
{
    bool active = false;
    Vec3f origin_reference_meters = {};
};

struct StereoSourceLayout
{
    NormalizedRect eyes[kEyeCount] = {
        {0.0f, 0.0f, 0.5f, 1.0f},
        {0.5f, 0.0f, 0.5f, 1.0f},
    };
    NormalizedViewport destinations[kEyeCount] = {
        {0.0f, 0.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
    };
    // A packed SBS half contains a complete logical eye squeezed into half
    // the backbuffer width. Multiplying its raw pixel aspect by two restores
    // the aspect that image had before packing. Full-frame sources use 1.
    float source_aspect_multipliers[kEyeCount] = {1.0f, 1.0f};
    // Mono menus/cinematics must retain the selected source rectangle's
    // logical aspect ratio inside each projection, with uncovered area black.
    // Gameplay stereo keeps its asymmetric oversized viewport mapping.
    bool preserve_source_aspect = false;
    PhysicalScopeComposition physical_scope = {};
    WorldAimMarkerComposition world_aim_marker = {};
};

} // namespace wawvr::xr
