// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "openxr_runtime.h"

#include <memory>

namespace wawvr::xr
{

struct CompositorReticle final
{
    bool visible{};
    std::uint32_t target_eye{};
    // Raw full-quad UV. Native UI coordinates use a separately remapped
    // content UV so pillar/letterbox bars remain non-interactive.
    float u{};
    float v{};
};

// Post-copy effects are disabled by default. The presentation owner must opt
// in only for live stereo gameplay; mono bootstrap, menus, cinematics, and
// comfort layers intentionally retain the exact copy path.
struct CompositorEffects final
{
    bool enable_eye_local_bloom{};
};

namespace compositor_detail
{

// Half-texel-inset bounds keep every bilinear bloom tap inside one packed eye
// rather than allowing the sampler footprint to cross the stereo seam.
struct EyeLocalBloomSamplingRegion final
{
    float min_u{};
    float min_v{};
    float max_u{};
    float max_v{};
    float texel_u{};
    float texel_v{};
};

[[nodiscard]] bool BuildEyeLocalBloomSamplingRegion(
    const NormalizedRect& rectangle,
    std::uint32_t source_width,
    std::uint32_t source_height,
    EyeLocalBloomSamplingRegion* region) noexcept;

[[nodiscard]] bool ShouldUseEyeLocalBloom(
    const CompositorEffects& effects,
    bool shader_available,
    bool sampling_region_valid) noexcept;

// The physical optic is optional, but the base stereo projection is not. A
// scope overlay is eligible only when the same lens geometry projects safely
// into both eyes before either OpenXR image is acquired.
[[nodiscard]] bool CanCompositePhysicalScopeBinocularly(
    const PhysicalScopeComposition& scope,
    const FrameState& frame,
    std::uint32_t source_width,
    std::uint32_t source_height) noexcept;

// Uses the exact captured eye poses/FOV when supplied, as those are the poses
// submitted with the image. Invalid, behind-eye, or offscreen geometry clears
// BOTH outputs; no edge-clamping and no mono fallback. Mono/menu and physical
// scope layouts keep their established overlay behavior.
[[nodiscard]] bool BuildWorldAimMarkerProjections(
    const StereoSourceLayout& layout,
    const FrameState& frame,
    const D3D11SourceFrame& source,
    const CompositorReticle* menu_reticle,
    CompositorReticle* eye_reticles) noexcept;

// Allows the focused test to compile the exact optional program used at
// runtime rather than a duplicated approximation.
[[nodiscard]] const char* EyeLocalBloomShaderSourceForTesting() noexcept;
[[nodiscard]] const char* PhysicalScopeShaderSourceForTesting() noexcept;
[[nodiscard]] const char* WorldAimMarkerShaderSourceForTesting() noexcept;

} // namespace compositor_detail

// Minimal full-screen-triangle compositor for a packed side-by-side D3D9
// capture. It performs the required sRGB decode/copy when the shared D3D9
// texture cannot itself expose an sRGB shader-resource view.
class D3D11Compositor final
{
public:
    D3D11Compositor();
    ~D3D11Compositor();

    D3D11Compositor(const D3D11Compositor&) = delete;
    D3D11Compositor& operator=(const D3D11Compositor&) = delete;

    bool Initialize(OpenXrRuntime& runtime, const HostCallbacks& host);
    void Shutdown();

    // Renders both eyes and releases both OpenXR images. The caller still
    // owns the OpenXR frame and must call runtime.EndFrame afterwards.
    bool RenderStereo(
        OpenXrRuntime& runtime,
        const FrameState& frame,
        const D3D11SourceFrame& source,
        const StereoSourceLayout& layout = {},
        const CompositorReticle* reticle = nullptr,
        std::uint32_t* released_eye_mask = nullptr,
        CompositorEffects effects = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wawvr::xr
