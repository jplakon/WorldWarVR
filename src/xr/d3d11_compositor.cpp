// SPDX-License-Identifier: GPL-3.0-only
// WorldAtWarVR D3D11 compositor. Comparative research history and the public
// API usage follows the pinned OpenXR and Direct3D SDK contracts.

#include "d3d11_compositor.h"
#include "compositor_math.hpp"

#if !defined(_WIN32)
#error WorldAtWarVR's D3D11 compositor currently supports Windows only.
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

namespace wawvr::xr
{

using Microsoft::WRL::ComPtr;

namespace
{

constexpr const char* kShaderSource = R"hlsl(
cbuffer CompositorConstants : register(b0)
{
    float2 uv_scale;
    float2 uv_offset;
    float2 reticle_center;
    float2 reticle_target_size;
    float2 bloom_uv_min;
    float2 bloom_uv_max;
    float2 bloom_texel_size;
    float2 bloom_padding;
    float4 scope_lens;
    float4 scope_basis;
    float4 scope_sample;
    float4 scope_bounds;
    float4 scope_viewport;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput VSMain(uint vertex_id : SV_VertexID)
{
    VertexOutput output;
    float2 position;
    if (vertex_id == 0)
        position = float2(-1.0, -1.0);
    else if (vertex_id == 1)
        position = float2(-1.0, 3.0);
    else
        position = float2(3.0, -1.0);

    output.position = float4(position, 0.0, 1.0);
    output.uv = float2(
        0.5 * (position.x + 1.0),
        0.5 * (1.0 - position.y));
    output.uv = output.uv * uv_scale + uv_offset;
    return output;
}

VertexOutput VSReticle(uint vertex_id : SV_VertexID)
{
    VertexOutput output;
    float2 position;
    if (vertex_id == 0)
        position = float2(-1.0, -1.0);
    else if (vertex_id == 1)
        position = float2(-1.0, 3.0);
    else
        position = float2(3.0, -1.0);

    output.position = float4(position, 0.0, 1.0);
    output.uv = float2(
        0.5 * (position.x + 1.0),
        0.5 * (1.0 - position.y));
    return output;
}

Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

float4 PSMain(VertexOutput input) : SV_Target
{
    return source_texture.Sample(source_sampler, input.uv);
}

float4 PSReticle(VertexOutput input) : SV_Target
{
    float2 pixel_delta =
        (input.uv - reticle_center) * reticle_target_size;
    float radius = length(pixel_delta);
    float ring =
        smoothstep(4.5, 5.5, radius) *
        (1.0 - smoothstep(8.0, 9.0, radius));
    float dot = 1.0 - smoothstep(1.0, 2.25, radius);
    float alpha = max(ring, dot) * 0.95;
    return float4(1.0, 0.42, 0.08, alpha);
}

float4 PSWorldAimMarker(VertexOutput input) : SV_Target
{
    // Scale with image height so the aiming cue remains readable at both
    // recovery and native headset resolutions. A dark outline is legible
    // over snow/sky, while the white center remains legible over dark terrain.
    const float scale = max(1.0, reticle_target_size.y / 1440.0);
    const float2 delta =
        abs((input.uv - reticle_center) * reticle_target_size) / scale;
    const float along = max(delta.x, delta.y);
    const float across = min(delta.x, delta.y);
    const float gap = smoothstep(3.0, 4.0, along);
    const float outline =
        (1.0 - smoothstep(2.25, 3.25, across)) *
        (1.0 - smoothstep(11.0, 12.0, along)) * gap;
    const float stroke =
        (1.0 - smoothstep(0.8, 1.5, across)) *
        (1.0 - smoothstep(9.5, 10.5, along)) * gap;
    const float radius = length(delta);
    const float dot_outline = 1.0 - smoothstep(2.0, 3.0, radius);
    const float dot = 1.0 - smoothstep(0.8, 1.5, radius);
    const float white = max(stroke, dot);
    const float alpha = max(max(outline, dot_outline), white);
    return float4(lerp(float3(0.005, 0.005, 0.005),
                       float3(1.0, 1.0, 1.0), white), alpha * 0.95);
}

float4 PSScope(VertexOutput input) : SV_Target
{
    const float2 screen_delta = input.uv - scope_lens.xy;
    const float determinant =
        scope_basis.x * scope_basis.w - scope_basis.z * scope_basis.y;
    if (abs(determinant) < 0.00001)
        discard;

    const float2 lens_delta = float2(
        (screen_delta.x * scope_basis.w -
         screen_delta.y * scope_basis.z) / determinant,
        (-screen_delta.x * scope_basis.y +
         screen_delta.y * scope_basis.x) / determinant);
    const float radius = length(lens_delta);
    if (radius > 1.0)
        discard;

    const float2 source_uv = clamp(
        scope_sample.xy + float2(lens_delta.x, -lens_delta.y) *
            scope_sample.zw,
        scope_bounds.xz, scope_bounds.yw);
    const float2 texel = scope_viewport.zw;
    const float4 center = source_texture.Sample(source_sampler, source_uv);
    const float3 neighbors =
        source_texture.Sample(source_sampler, source_uv + float2(texel.x, 0)).rgb +
        source_texture.Sample(source_sampler, source_uv - float2(texel.x, 0)).rgb +
        source_texture.Sample(source_sampler, source_uv + float2(0, texel.y)).rgb +
        source_texture.Sample(source_sampler, source_uv - float2(0, texel.y)).rgb;
    float3 color = saturate(center.rgb * 1.72 - neighbors * 0.18);
    color = saturate((color - 0.5) * 1.06 + 0.5);

    const float edge = smoothstep(0.82, 1.0, radius);
    color = lerp(color, float3(0.005, 0.007, 0.009), edge);
    const float center_gap = smoothstep(0.055, 0.09, radius);
    const float vertical =
        1.0 - smoothstep(0.008, 0.016, abs(lens_delta.x));
    const float horizontal =
        1.0 - smoothstep(0.008, 0.016, abs(lens_delta.y));
    const float reticle = max(vertical, horizontal) * center_gap *
        (1.0 - smoothstep(0.72, 0.88, radius));
    color = lerp(color, float3(0.01, 0.01, 0.01), reticle);
    return float4(color, saturate(scope_lens.z));
}
)hlsl";

// Kept separate from the mandatory copy/reticle program so an optional bloom
// compiler or device failure cannot prevent the compositor from initializing.
constexpr const char* kBloomShaderSource = R"hlsl(
cbuffer CompositorConstants : register(b0)
{
    float2 uv_scale;
    float2 uv_offset;
    float2 reticle_center;
    float2 reticle_target_size;
    float2 bloom_uv_min;
    float2 bloom_uv_max;
    float2 bloom_texel_size;
    float2 bloom_padding;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Texture2D source_texture : register(t0);
SamplerState source_sampler : register(s0);

float3 ExtractBloom(float3 color)
{
    const float peak = max(color.r, max(color.g, color.b));
    const float contribution = saturate((peak - 0.70) / 0.30);
    return color * contribution;
}

float3 BloomTap(float2 uv)
{
    return ExtractBloom(source_texture.Sample(
        source_sampler, clamp(uv, bloom_uv_min, bloom_uv_max)).rgb);
}

float4 PSBloom(VertexOutput input) : SV_Target
{
    const float2 center = clamp(input.uv, bloom_uv_min, bloom_uv_max);
    const float4 base = source_texture.Sample(source_sampler, center);
    const float2 axial = bloom_texel_size * 3.0;
    const float2 diagonal = bloom_texel_size * 2.0;

    float3 glow = 0.0;
    glow += BloomTap(center + float2( axial.x, 0.0)) * 0.15;
    glow += BloomTap(center + float2(-axial.x, 0.0)) * 0.15;
    glow += BloomTap(center + float2(0.0,  axial.y)) * 0.15;
    glow += BloomTap(center + float2(0.0, -axial.y)) * 0.15;
    glow += BloomTap(center + float2( diagonal.x,  diagonal.y)) * 0.10;
    glow += BloomTap(center + float2(-diagonal.x,  diagonal.y)) * 0.10;
    glow += BloomTap(center + float2( diagonal.x, -diagonal.y)) * 0.10;
    glow += BloomTap(center + float2(-diagonal.x, -diagonal.y)) * 0.10;

    return float4(saturate(base.rgb + glow * 0.40), base.a);
}
)hlsl";

struct alignas(16) CompositorConstants
{
    float uv_scale[2] = {};
    float uv_offset[2] = {};
    float reticle_center[2] = {};
    float reticle_target_size[2] = {};
    float bloom_uv_min[2] = {};
    float bloom_uv_max[2] = {};
    float bloom_texel_size[2] = {};
    float bloom_padding[2] = {};
    float scope_lens[4] = {};
    float scope_basis[4] = {};
    float scope_sample[4] = {};
    float scope_bounds[4] = {};
    float scope_viewport[4] = {};
};

static_assert(sizeof(CompositorConstants) == 144);
static_assert(offsetof(CompositorConstants, uv_scale) == 0);
static_assert(offsetof(CompositorConstants, uv_offset) == 8);
static_assert(offsetof(CompositorConstants, reticle_center) == 16);
static_assert(offsetof(CompositorConstants, reticle_target_size) == 24);
static_assert(offsetof(CompositorConstants, bloom_uv_min) == 32);
static_assert(offsetof(CompositorConstants, bloom_uv_max) == 40);
static_assert(offsetof(CompositorConstants, bloom_texel_size) == 48);
static_assert(offsetof(CompositorConstants, bloom_padding) == 56);
static_assert(offsetof(CompositorConstants, scope_lens) == 64);
static_assert(offsetof(CompositorConstants, scope_basis) == 80);
static_assert(offsetof(CompositorConstants, scope_sample) == 96);
static_assert(offsetof(CompositorConstants, scope_bounds) == 112);
static_assert(offsetof(CompositorConstants, scope_viewport) == 128);

bool GetSrgbViewFormats(
    const DXGI_FORMAT source,
    DXGI_FORMAT* typeless,
    DXGI_FORMAT* srgb)
{
    switch (source)
    {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        *typeless = DXGI_FORMAT_B8G8R8A8_TYPELESS;
        *srgb = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        return true;
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        *typeless = DXGI_FORMAT_B8G8R8X8_TYPELESS;
        *srgb = DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
        return true;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        *typeless = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        *srgb = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool Finite(const Vec3f& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] Vec3f Add(const Vec3f& left, const Vec3f& right)
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] Vec3f Subtract(const Vec3f& left, const Vec3f& right)
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] Vec3f Scale(const Vec3f& value, const float scale)
{
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] Vec3f Rotate(
    const Quaternionf& quaternion,
    const Vec3f& value)
{
    const Vec3f q{quaternion.x, quaternion.y, quaternion.z};
    const Vec3f twice_cross{
        2.0F * (q.y * value.z - q.z * value.y),
        2.0F * (q.z * value.x - q.x * value.z),
        2.0F * (q.x * value.y - q.y * value.x),
    };
    const Vec3f cross_again{
        q.y * twice_cross.z - q.z * twice_cross.y,
        q.z * twice_cross.x - q.x * twice_cross.z,
        q.x * twice_cross.y - q.y * twice_cross.x,
    };
    return Add(value, Add(Scale(twice_cross, quaternion.w), cross_again));
}

[[nodiscard]] bool ValidQuaternion(const Quaternionf& quaternion)
{
    const float length_squared =
        quaternion.x * quaternion.x + quaternion.y * quaternion.y +
        quaternion.z * quaternion.z + quaternion.w * quaternion.w;
    return std::isfinite(length_squared) && length_squared > 0.81F &&
           length_squared < 1.21F;
}

[[nodiscard]] bool ProjectToEye(
    const Vec3f& point,
    const EyeView& eye,
    float* const u,
    float* const v)
{
    if (u == nullptr || v == nullptr || !Finite(point) ||
        !Finite(eye.pose.position) ||
        !ValidQuaternion(eye.pose.orientation))
    {
        return false;
    }
    const Quaternionf inverse{
        -eye.pose.orientation.x,
        -eye.pose.orientation.y,
        -eye.pose.orientation.z,
        eye.pose.orientation.w,
    };
    const Vec3f local = Rotate(inverse, Subtract(point, eye.pose.position));
    const float depth = -local.z;
    const float tangent_left = std::tan(eye.fov.angle_left);
    const float tangent_right = std::tan(eye.fov.angle_right);
    const float tangent_down = std::tan(eye.fov.angle_down);
    const float tangent_up = std::tan(eye.fov.angle_up);
    const float span_x = tangent_right - tangent_left;
    const float span_y = tangent_up - tangent_down;
    if (!Finite(local) || !std::isfinite(depth) || depth <= 0.005F ||
        !std::isfinite(span_x) || !std::isfinite(span_y) ||
        span_x <= 0.0001F || span_y <= 0.0001F)
    {
        return false;
    }
    const float tangent_x = local.x / depth;
    const float tangent_y = local.y / depth;
    *u = (tangent_x - tangent_left) / span_x;
    *v = 1.0F - (tangent_y - tangent_down) / span_y;
    return std::isfinite(*u) && std::isfinite(*v);
}

[[nodiscard]] bool BuildScopeConstants(
    const PhysicalScopeComposition& scope,
    const FrameState& frame,
    const std::uint32_t eye_index,
    const std::uint32_t target_width,
    const std::uint32_t target_height,
    const std::uint32_t source_width,
    const std::uint32_t source_height,
    CompositorConstants* const constants)
{
    if (constants == nullptr || !scope.active || eye_index >= kEyeCount ||
        target_width == 0 || target_height == 0 || source_width == 0 ||
        source_height == 0 ||
        !Finite(scope.lens_origin_reference_meters) ||
        !Finite(scope.lens_right_reference) ||
        !Finite(scope.lens_up_reference) ||
        !std::isfinite(scope.lens_radius_meters) ||
        scope.lens_radius_meters <= 0.0F ||
        !std::isfinite(scope.source.x) || !std::isfinite(scope.source.y) ||
        !std::isfinite(scope.source.width) ||
        !std::isfinite(scope.source.height) || scope.source.x < 0.0F ||
        scope.source.y < 0.0F || scope.source.width <= 0.0F ||
        scope.source.height <= 0.0F ||
        scope.source.x + scope.source.width > 1.0F ||
        scope.source.y + scope.source.height > 1.0F)
    {
        return false;
    }

    const Vec3f lens_center = scope.lens_origin_reference_meters;
    const Vec3f lens_right_direction = scope.lens_right_reference;
    const Vec3f lens_up_direction = scope.lens_up_reference;
    const Vec3f lens_right = Add(
        lens_center, Scale(lens_right_direction, scope.lens_radius_meters));
    const Vec3f lens_up = Add(
        lens_center, Scale(lens_up_direction, scope.lens_radius_meters));

    float center_u = 0.0F;
    float center_v = 0.0F;
    float right_u = 0.0F;
    float right_v = 0.0F;
    float up_u = 0.0F;
    float up_v = 0.0F;
    if (!ProjectToEye(
            lens_center, frame.eyes[eye_index], &center_u, &center_v) ||
        !ProjectToEye(
            lens_right, frame.eyes[eye_index], &right_u, &right_v) ||
        !ProjectToEye(lens_up, frame.eyes[eye_index], &up_u, &up_v))
    {
        return false;
    }

    const float basis_right_u = right_u - center_u;
    const float basis_right_v = right_v - center_v;
    const float basis_up_u = up_u - center_u;
    const float basis_up_v = up_v - center_v;
    const float determinant =
        basis_right_u * basis_up_v - basis_up_u * basis_right_v;
    if (!std::isfinite(determinant) || std::fabs(determinant) < 0.00001F)
    {
        return false;
    }

    constexpr float kSourceInsetPixels = 2.0F;
    const float texel_u = 1.0F / static_cast<float>(source_width);
    const float texel_v = 1.0F / static_cast<float>(source_height);
    const float inset_u = kSourceInsetPixels * texel_u;
    const float inset_v = kSourceInsetPixels * texel_v;
    if (scope.source.width <= 2.0F * inset_u ||
        scope.source.height <= 2.0F * inset_v)
    {
        return false;
    }

    CompositorConstants result = {};
    result.scope_lens[0] = center_u;
    result.scope_lens[1] = center_v;
    result.scope_lens[2] = 1.0F;
    result.scope_lens[3] = 1.0F;
    result.scope_basis[0] = basis_right_u;
    result.scope_basis[1] = basis_right_v;
    result.scope_basis[2] = basis_up_u;
    result.scope_basis[3] = basis_up_v;
    result.scope_sample[0] = scope.source.x + scope.source.width * 0.5F;
    result.scope_sample[1] = scope.source.y + scope.source.height * 0.5F;
    result.scope_sample[2] = scope.source.width * 0.5F - inset_u;
    result.scope_sample[3] = scope.source.height * 0.5F - inset_v;
    result.scope_bounds[0] = scope.source.x + inset_u;
    result.scope_bounds[1] =
        scope.source.x + scope.source.width - inset_u;
    result.scope_bounds[2] = scope.source.y + inset_v;
    result.scope_bounds[3] =
        scope.source.y + scope.source.height - inset_v;
    result.scope_viewport[0] = 1.0F / static_cast<float>(target_width);
    result.scope_viewport[1] = 1.0F / static_cast<float>(target_height);
    result.scope_viewport[2] = texel_u;
    result.scope_viewport[3] = texel_v;
    *constants = result;
    return true;
}

} // namespace

namespace compositor_detail
{

bool BuildEyeLocalBloomSamplingRegion(
    const NormalizedRect& rectangle,
    const std::uint32_t source_width,
    const std::uint32_t source_height,
    EyeLocalBloomSamplingRegion* const region) noexcept
{
    if (region == nullptr || source_width == 0 || source_height == 0 ||
        !std::isfinite(rectangle.x) || !std::isfinite(rectangle.y) ||
        !std::isfinite(rectangle.width) ||
        !std::isfinite(rectangle.height) || rectangle.x < 0.0F ||
        rectangle.y < 0.0F || rectangle.width <= 0.0F ||
        rectangle.height <= 0.0F ||
        rectangle.x + rectangle.width > 1.0F ||
        rectangle.y + rectangle.height > 1.0F)
    {
        return false;
    }

    EyeLocalBloomSamplingRegion candidate = {};
    candidate.texel_u = 1.0F / static_cast<float>(source_width);
    candidate.texel_v = 1.0F / static_cast<float>(source_height);
    candidate.min_u = rectangle.x + 0.5F * candidate.texel_u;
    candidate.min_v = rectangle.y + 0.5F * candidate.texel_v;
    candidate.max_u =
        rectangle.x + rectangle.width - 0.5F * candidate.texel_u;
    candidate.max_v =
        rectangle.y + rectangle.height - 0.5F * candidate.texel_v;
    if (!std::isfinite(candidate.min_u) ||
        !std::isfinite(candidate.min_v) ||
        !std::isfinite(candidate.max_u) ||
        !std::isfinite(candidate.max_v) ||
        !std::isfinite(candidate.texel_u) ||
        !std::isfinite(candidate.texel_v) ||
        candidate.min_u > candidate.max_u ||
        candidate.min_v > candidate.max_v)
    {
        return false;
    }

    *region = candidate;
    return true;
}

bool ShouldUseEyeLocalBloom(
    const CompositorEffects& effects,
    const bool shader_available,
    const bool sampling_region_valid) noexcept
{
    return effects.enable_eye_local_bloom && shader_available &&
           sampling_region_valid;
}

bool CanCompositePhysicalScopeBinocularly(
    const PhysicalScopeComposition& scope,
    const FrameState& frame,
    const std::uint32_t source_width,
    const std::uint32_t source_height) noexcept
{
    if (!scope.active || !frame.views_valid)
    {
        return false;
    }

    // Target dimensions only contribute reciprocal viewport texels. Nonzero
    // sentinels let us validate all lens geometry before acquiring eye 0.
    constexpr std::uint32_t kValidationTargetDimension = 1;
    CompositorConstants constants = {};
    for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
    {
        if (!BuildScopeConstants(
                scope, frame, eye, kValidationTargetDimension,
                kValidationTargetDimension, source_width, source_height,
                &constants))
        {
            return false;
        }
    }
    return true;
}

bool BuildWorldAimMarkerProjections(
    const StereoSourceLayout& layout,
    const FrameState& frame,
    const D3D11SourceFrame& source,
    const CompositorReticle* const menu_reticle,
    CompositorReticle* const eye_reticles) noexcept
{
    if (eye_reticles == nullptr)
    {
        return false;
    }
    for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
    {
        eye_reticles[eye] = {};
    }
    if (!layout.world_aim_marker.active || !frame.views_valid ||
        layout.preserve_source_aspect || layout.physical_scope.active ||
        (menu_reticle != nullptr && menu_reticle->visible))
    {
        return false;
    }

    CompositorReticle candidates[kEyeCount] = {};
    for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
    {
        const EyeView& captured_eye = source.rendered_views_valid
            ? source.rendered_eyes[eye] : frame.eyes[eye];
        CompositorReticle& candidate = candidates[eye];
        if (!ProjectToEye(layout.world_aim_marker.origin_reference_meters,
                          captured_eye, &candidate.u, &candidate.v) ||
            candidate.u < 0.0F || candidate.u > 1.0F ||
            candidate.v < 0.0F || candidate.v > 1.0F)
        {
            return false;
        }
        candidate.visible = true;
        candidate.target_eye = eye;
    }
    for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
    {
        eye_reticles[eye] = candidates[eye];
    }
    return true;
}

const char* EyeLocalBloomShaderSourceForTesting() noexcept
{
    return kBloomShaderSource;
}

const char* PhysicalScopeShaderSourceForTesting() noexcept
{
    return kShaderSource;
}

const char* WorldAimMarkerShaderSourceForTesting() noexcept
{
    return kShaderSource;
}

} // namespace compositor_detail

struct D3D11Compositor::Impl
{
    HostCallbacks host = {};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11PixelShader> pixel_shader;
    ComPtr<ID3D11PixelShader> bloom_pixel_shader;
    ComPtr<ID3D11VertexShader> reticle_vertex_shader;
    ComPtr<ID3D11PixelShader> reticle_pixel_shader;
    ComPtr<ID3D11PixelShader> world_aim_marker_pixel_shader;
    ComPtr<ID3D11PixelShader> scope_pixel_shader;
    ComPtr<ID3D11BlendState> reticle_blend;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Buffer> scope_constants[kEyeCount];

    ComPtr<ID3D11Texture2D> decoded_texture;
    ComPtr<ID3D11ShaderResourceView> decoded_view;
    std::uint32_t decoded_width = 0;
    std::uint32_t decoded_height = 0;
    DXGI_FORMAT decoded_source_format = DXGI_FORMAT_UNKNOWN;
    bool invalid_bloom_region_logged = false;
    bool invalid_scope_geometry_logged = false;
    bool initialized = false;
    bool orientation_diagnostics_checked = false;
    bool orientation_diagnostics_enabled = false;
    std::wstring orientation_diagnostics_directory;
    std::uint32_t orientation_diagnostics_mode_frames[2] = {};
    bool orientation_diagnostics_mode_attempted[2] = {};

    void Log(const LogLevel level, const char* format, ...) const
    {
        if (host.log == nullptr)
        {
            return;
        }
        char message[1024] = {};
        va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(message, sizeof(message), format, arguments);
        va_end(arguments);
        message[sizeof(message) - 1] = '\0';
        host.log(host.user_data, level, message);
    }

    void ConfigureOrientationDiagnostics()
    {
        // Remain bounded even if this compositor's runtime is recreated.
        if (orientation_diagnostics_checked) return;
        orientation_diagnostics_checked = true;
        wchar_t enabled[2] = {};
        if (GetEnvironmentVariableW(L"WAWVR_ORIENTATION_DIAGNOSTICS",
                enabled, 2) != 1 || enabled[0] != L'1') return;
        wchar_t directory[32768] = {};
        const DWORD length = GetEnvironmentVariableW(
            L"WAWVR_ORIENTATION_DIAG_DIR", directory, DWORD(std::size(directory)));
        if (length == 0 || length >= std::size(directory)) {
            Log(LogLevel::Warning,
                "OrientationDiag disabled: WAWVR_ORIENTATION_DIAG_DIR is missing/too long");
            return;
        }
        wchar_t absolute[32768] = {};
        const DWORD absolute_length = GetFullPathNameW(directory,
            DWORD(std::size(absolute)), absolute, nullptr);
        if (absolute_length == 0 || absolute_length >= std::size(absolute)) {
            Log(LogLevel::Warning,
                "OrientationDiag disabled: cannot resolve directory (error=%lu)",
                GetLastError());
            return;
        }
        const DWORD attributes = GetFileAttributesW(absolute);
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            Log(LogLevel::Warning,
                "OrientationDiag disabled: directory must already exist: %ls",
                absolute);
            return;
        }
        orientation_diagnostics_directory = absolute;
        orientation_diagnostics_enabled = true;
        Log(LogLevel::Info,
            "OrientationDiag enabled: one source/left/right top-down BMP after "
            "30 mono and 360 stereo compositor frames; directory=%ls",
            absolute);
    }

    const char* BeginOrientationCapture(const StereoSourceLayout& layout)
    {
        if (!orientation_diagnostics_enabled) return nullptr;
        const unsigned mode = layout.preserve_source_aspect ? 0u : 1u;
        if (orientation_diagnostics_mode_attempted[mode]) return nullptr;
        const std::uint32_t wait_frames = mode == 0 ? 30u : 360u;
        if (++orientation_diagnostics_mode_frames[mode] <= wait_frames) return nullptr;
        // A failed diagnostic never retries continuously during gameplay.
        orientation_diagnostics_mode_attempted[mode] = true;
        return mode == 0 ? "mono" : "stereo";
    }

    void CaptureOrientationTexture(ID3D11Texture2D* texture, const char* mode,
        const char* role, const std::uint64_t frame_id,
        const std::uint64_t source_serial)
    {
        if (texture == nullptr || mode == nullptr) return;
        D3D11_TEXTURE2D_DESC description{};
        texture->GetDesc(&description);
        const bool rgba = description.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
            description.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
            description.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
        const bool bgra = description.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
            description.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
            description.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ||
            description.Format == DXGI_FORMAT_B8G8R8X8_UNORM ||
            description.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB ||
            description.Format == DXGI_FORMAT_B8G8R8X8_TYPELESS;
        const std::uint64_t bytes = std::uint64_t(description.Width) *
            description.Height * 4;
        if ((!rgba && !bgra) || description.Width == 0 || description.Height == 0 ||
            description.Width > 16384 || description.Height > 16384 ||
            bytes > 256u * 1024u * 1024u || description.ArraySize != 1 ||
            description.MipLevels != 1 || description.SampleDesc.Count != 1) {
            Log(LogLevel::Warning,
                "OrientationDiag skipped %s/%s frame=%llu: unsupported texture "
                "%ux%u format=%u array=%u mips=%u samples=%u",
                mode, role, static_cast<unsigned long long>(frame_id),
                description.Width, description.Height, unsigned(description.Format),
                description.ArraySize, description.MipLevels,
                description.SampleDesc.Count);
            return;
        }
        wchar_t filename[256] = {};
        std::swprintf(filename, std::size(filename),
            L"orientation-p%lu-%hs-f%llu-s%llu-%hs.bmp", GetCurrentProcessId(),
            mode, static_cast<unsigned long long>(frame_id),
            static_cast<unsigned long long>(source_serial), role);
        const std::wstring path = orientation_diagnostics_directory + L"\\" + filename;
        const DXGI_FORMAT original_format = description.Format;
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        description.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        HRESULT result = device->CreateTexture2D(&description, nullptr,
            staging.GetAddressOf());
        if (SUCCEEDED(result)) {
            context->CopyResource(staging.Get(), texture);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            result = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
            if (SUCCEEDED(result)) {
                const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE) {
                    result = HRESULT_FROM_WIN32(GetLastError());
                } else {
                    BITMAPFILEHEADER header{};
                    BITMAPINFOHEADER info{};
                    header.bfType = 0x4d42;
                    header.bfOffBits = sizeof(header) + sizeof(info);
                    header.bfSize = header.bfOffBits + DWORD(bytes);
                    info.biSize = sizeof(info);
                    info.biWidth = LONG(description.Width);
                    info.biHeight = -LONG(description.Height);
                    info.biPlanes = 1;
                    info.biBitCount = 32;
                    info.biCompression = BI_RGB;
                    info.biSizeImage = DWORD(bytes);
                    auto write = [&](const void* data, DWORD count) {
                        DWORD written = 0;
                        if (!WriteFile(file, data, count, &written, nullptr))
                            return HRESULT_FROM_WIN32(GetLastError());
                        return written == count ? S_OK : HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
                    };
                    result = write(&header, sizeof(header));
                    if (SUCCEEDED(result)) result = write(&info, sizeof(info));
                    const UINT row_bytes = description.Width * 4;
                    if (mapped.RowPitch < row_bytes) result = E_INVALIDARG;
                    std::vector<unsigned char> row(row_bytes);
                    for (UINT y = 0; SUCCEEDED(result) && y < description.Height; ++y) {
                        const auto* input = static_cast<const unsigned char*>(mapped.pData)
                            + std::size_t(y) * mapped.RowPitch;
                        std::memcpy(row.data(), input, row_bytes);
                        for (UINT x = 0; x < row_bytes; x += 4) {
                            if (rgba) std::swap(row[x], row[x + 2]);
                            row[x + 3] = 255;
                        }
                        result = write(row.data(), row_bytes);
                    }
                    CloseHandle(file);
                }
                context->Unmap(staging.Get(), 0);
            }
        }
        Log(SUCCEEDED(result) ? LogLevel::Info : LogLevel::Warning,
            "OrientationDiag %s/%s frame=%llu serial=%llu dimensions=%ux%u "
            "format=%u mapping=identity hr=0x%08lx path=%ls",
            mode, role, static_cast<unsigned long long>(frame_id),
            static_cast<unsigned long long>(source_serial), description.Width,
            description.Height, unsigned(original_format),
            result, path.c_str());
    }

    bool CompileShader(
        const char* source,
        const char* source_name,
        const char* entry,
        const char* profile,
        const LogLevel failure_level,
        ComPtr<ID3DBlob>* bytecode)
    {
        ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(
            source,
            std::strlen(source),
            source_name,
            nullptr,
            nullptr,
            entry,
            profile,
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            bytecode->GetAddressOf(),
            errors.GetAddressOf());
        if (FAILED(hr))
        {
            const char* details = errors != nullptr
                ? static_cast<const char*>(errors->GetBufferPointer())
                : "no compiler diagnostics";
            Log(failure_level, "D3DCompile(%s) failed: 0x%08lx: %s",
                entry, hr, details);
            return false;
        }
        return true;
    }

    bool CreatePipeline()
    {
        ComPtr<ID3DBlob> vertex_bytecode;
        ComPtr<ID3DBlob> pixel_bytecode;
        ComPtr<ID3DBlob> reticle_vertex_bytecode;
        ComPtr<ID3DBlob> reticle_pixel_bytecode;
        ComPtr<ID3DBlob> scope_pixel_bytecode;
        if (!CompileShader(
                kShaderSource, "wawvr_compositor.hlsl", "VSMain",
                "vs_4_0", LogLevel::Error, &vertex_bytecode) ||
            !CompileShader(
                kShaderSource, "wawvr_compositor.hlsl", "PSMain",
                "ps_4_0", LogLevel::Error, &pixel_bytecode) ||
            !CompileShader(
                kShaderSource, "wawvr_compositor.hlsl", "VSReticle",
                "vs_4_0", LogLevel::Error, &reticle_vertex_bytecode) ||
            !CompileShader(
                kShaderSource, "wawvr_compositor.hlsl", "PSReticle",
                "ps_4_0", LogLevel::Error, &reticle_pixel_bytecode) ||
            !CompileShader(
                kShaderSource, "wawvr_compositor.hlsl", "PSScope",
                "ps_4_0", LogLevel::Error, &scope_pixel_bytecode))
        {
            return false;
        }

        HRESULT hr = device->CreateVertexShader(
            vertex_bytecode->GetBufferPointer(),
            vertex_bytecode->GetBufferSize(),
            nullptr,
            vertex_shader.GetAddressOf());
        if (SUCCEEDED(hr))
        {
            hr = device->CreatePixelShader(
                pixel_bytecode->GetBufferPointer(),
                pixel_bytecode->GetBufferSize(),
                nullptr,
                pixel_shader.GetAddressOf());
        }
        if (SUCCEEDED(hr))
        {
            hr = device->CreateVertexShader(
                reticle_vertex_bytecode->GetBufferPointer(),
                reticle_vertex_bytecode->GetBufferSize(),
                nullptr,
                reticle_vertex_shader.GetAddressOf());
        }
        if (SUCCEEDED(hr))
        {
            hr = device->CreatePixelShader(
                reticle_pixel_bytecode->GetBufferPointer(),
                reticle_pixel_bytecode->GetBufferSize(),
                nullptr,
                reticle_pixel_shader.GetAddressOf());
        }
        if (SUCCEEDED(hr))
        {
            hr = device->CreatePixelShader(
                scope_pixel_bytecode->GetBufferPointer(),
                scope_pixel_bytecode->GetBufferSize(), nullptr,
                scope_pixel_shader.GetAddressOf());
        }
        if (FAILED(hr))
        {
            Log(LogLevel::Error, "Create compositor shader failed: 0x%08lx", hr);
            return false;
        }

        ComPtr<ID3DBlob> bloom_pixel_bytecode;
        ComPtr<ID3DBlob> world_aim_marker_bytecode;
        if (CompileShader(
                kShaderSource, "wawvr_world_aim_marker.hlsl",
                "PSWorldAimMarker", "ps_4_0", LogLevel::Warning,
                &world_aim_marker_bytecode))
        {
            const HRESULT marker_hr = device->CreatePixelShader(
                world_aim_marker_bytecode->GetBufferPointer(),
                world_aim_marker_bytecode->GetBufferSize(), nullptr,
                world_aim_marker_pixel_shader.GetAddressOf());
            if (FAILED(marker_hr))
            {
                world_aim_marker_pixel_shader.Reset();
                Log(LogLevel::Warning,
                    "CreatePixelShader(world aim marker) failed: 0x%08lx; "
                    "preserving base stereo without the optional marker",
                    marker_hr);
            }
        }
        if (CompileShader(
                kBloomShaderSource, "wawvr_eye_local_bloom.hlsl",
                "PSBloom", "ps_4_0", LogLevel::Warning,
                &bloom_pixel_bytecode))
        {
            const HRESULT bloom_hr = device->CreatePixelShader(
                bloom_pixel_bytecode->GetBufferPointer(),
                bloom_pixel_bytecode->GetBufferSize(), nullptr,
                bloom_pixel_shader.GetAddressOf());
            if (FAILED(bloom_hr))
            {
                bloom_pixel_shader.Reset();
                Log(LogLevel::Warning,
                    "CreatePixelShader(eye-local bloom) failed: 0x%08lx; "
                    "using the compositor copy path",
                    bloom_hr);
            }
        }

        D3D11_BLEND_DESC blend_description = {};
        D3D11_RENDER_TARGET_BLEND_DESC& target_blend =
            blend_description.RenderTarget[0];
        target_blend.BlendEnable = TRUE;
        target_blend.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        target_blend.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        target_blend.BlendOp = D3D11_BLEND_OP_ADD;
        target_blend.SrcBlendAlpha = D3D11_BLEND_ONE;
        target_blend.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        target_blend.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        target_blend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = device->CreateBlendState(
            &blend_description, reticle_blend.GetAddressOf());
        if (FAILED(hr))
        {
            Log(LogLevel::Error,
                "CreateBlendState(reticle) failed: 0x%08lx", hr);
            return false;
        }

        D3D11_SAMPLER_DESC sampler_description = {};
        sampler_description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_description.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_description.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_description.MaxLOD = D3D11_FLOAT32_MAX;
        hr = device->CreateSamplerState(
            &sampler_description, sampler.GetAddressOf());
        if (FAILED(hr))
        {
            Log(LogLevel::Error, "CreateSamplerState failed: 0x%08lx", hr);
            return false;
        }

        D3D11_BUFFER_DESC buffer_description = {};
        buffer_description.ByteWidth = sizeof(CompositorConstants);
        buffer_description.Usage = D3D11_USAGE_DYNAMIC;
        buffer_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        buffer_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = device->CreateBuffer(
            &buffer_description, nullptr, constants.GetAddressOf());
        if (FAILED(hr))
        {
            Log(LogLevel::Error, "CreateBuffer(compositor) failed: 0x%08lx", hr);
            return false;
        }
        for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
        {
            hr = device->CreateBuffer(
                &buffer_description, nullptr,
                scope_constants[eye].GetAddressOf());
            if (FAILED(hr))
            {
                for (auto& scope_buffer : scope_constants)
                {
                    scope_buffer.Reset();
                }
                scope_pixel_shader.Reset();
                Log(LogLevel::Warning,
                    "CreateBuffer(physical scope eye %u) failed: 0x%08lx; "
                    "physical optic disabled",
                    eye, hr);
                break;
            }
        }
        return true;
    }

    ID3D11ShaderResourceView* PrepareSource(const D3D11SourceFrame& source)
    {
        if (!source.pixels_are_srgb_encoded)
        {
            return source.view;
        }

        D3D11_TEXTURE2D_DESC source_description = {};
        source.texture->GetDesc(&source_description);
        DXGI_FORMAT typeless_format = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT srgb_format = DXGI_FORMAT_UNKNOWN;
        if (!GetSrgbViewFormats(
                source_description.Format, &typeless_format, &srgb_format))
        {
            Log(LogLevel::Error,
                "Cannot create an sRGB view for shared DXGI format %u",
                static_cast<unsigned>(source_description.Format));
            return nullptr;
        }

        const bool recreate =
            decoded_texture == nullptr ||
            decoded_width != source_description.Width ||
            decoded_height != source_description.Height ||
            decoded_source_format != source_description.Format;
        if (recreate)
        {
            decoded_view.Reset();
            decoded_texture.Reset();

            D3D11_TEXTURE2D_DESC decoded_description = source_description;
            decoded_description.Format = typeless_format;
            decoded_description.Usage = D3D11_USAGE_DEFAULT;
            decoded_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            decoded_description.CPUAccessFlags = 0;
            decoded_description.MiscFlags = 0;
            decoded_description.SampleDesc.Count = 1;
            decoded_description.SampleDesc.Quality = 0;
            HRESULT hr = device->CreateTexture2D(
                &decoded_description,
                nullptr,
                decoded_texture.GetAddressOf());
            if (FAILED(hr))
            {
                Log(LogLevel::Error,
                    "CreateTexture2D(sRGB decode) failed: 0x%08lx", hr);
                return nullptr;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC view_description = {};
            view_description.Format = srgb_format;
            view_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            view_description.Texture2D.MostDetailedMip = 0;
            view_description.Texture2D.MipLevels = 1;
            hr = device->CreateShaderResourceView(
                decoded_texture.Get(),
                &view_description,
                decoded_view.GetAddressOf());
            if (FAILED(hr))
            {
                Log(LogLevel::Error,
                    "CreateShaderResourceView(sRGB decode) failed: 0x%08lx", hr);
                decoded_texture.Reset();
                return nullptr;
            }
            decoded_width = source_description.Width;
            decoded_height = source_description.Height;
            decoded_source_format = source_description.Format;
        }

        // Preserve the source bits in typeless storage, then interpret them
        // through an sRGB view when the compositor samples the texture.
        context->CopyResource(decoded_texture.Get(), source.texture);
        return decoded_view.Get();
    }

    bool UpdateConstants(
        const NormalizedRect& rectangle,
        const compositor_detail::EyeLocalBloomSamplingRegion*
            bloom_region)
    {
        CompositorConstants values = {};
        values.uv_scale[0] = rectangle.width;
        values.uv_scale[1] = rectangle.height;
        values.uv_offset[0] = rectangle.x;
        values.uv_offset[1] = rectangle.y;
        if (bloom_region != nullptr)
        {
            values.bloom_uv_min[0] = bloom_region->min_u;
            values.bloom_uv_min[1] = bloom_region->min_v;
            values.bloom_uv_max[0] = bloom_region->max_u;
            values.bloom_uv_max[1] = bloom_region->max_v;
            values.bloom_texel_size[0] = bloom_region->texel_u;
            values.bloom_texel_size[1] = bloom_region->texel_v;
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        const HRESULT hr = context->Map(
            constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr) || mapped.pData == nullptr)
        {
            Log(LogLevel::Error,
                "Map(compositor constants) failed: 0x%08lx", hr);
            return false;
        }
        std::memcpy(mapped.pData, &values, sizeof(values));
        context->Unmap(constants.Get(), 0);
        return true;
    }

    bool UpdateReticleConstants(
        const CompositorReticle& reticle,
        const std::uint32_t target_width,
        const std::uint32_t target_height)
    {
        CompositorConstants values = {};
        values.reticle_center[0] = reticle.u;
        values.reticle_center[1] = reticle.v;
        values.reticle_target_size[0] = static_cast<float>(target_width);
        values.reticle_target_size[1] = static_cast<float>(target_height);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        const HRESULT hr = context->Map(
            constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr) || mapped.pData == nullptr)
        {
            Log(LogLevel::Error,
                "Map(reticle constants) failed: 0x%08lx", hr);
            return false;
        }
        std::memcpy(mapped.pData, &values, sizeof(values));
        context->Unmap(constants.Get(), 0);
        return true;
    }

    bool PrepareScopeConstants(
        const PhysicalScopeComposition& scope,
        const FrameState& frame,
        const std::uint32_t source_width,
        const std::uint32_t source_height)
    {
        // Prepare both optional overlays before acquiring eye 0. This makes
        // scope geometry and constant-buffer upload one binocular transaction:
        // any failure disables the optic for the frame without turning a
        // successfully rendered base eye into an incomplete OpenXR update.
        constexpr std::uint32_t kValidationTargetDimension = 1;
        CompositorConstants values[kEyeCount] = {};
        for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
        {
            if (scope_constants[eye] == nullptr ||
                !BuildScopeConstants(
                    scope, frame, eye, kValidationTargetDimension,
                    kValidationTargetDimension, source_width, source_height,
                    &values[eye]))
            {
                return false;
            }
        }
        for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
        {
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            const HRESULT hr = context->Map(
                scope_constants[eye].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                &mapped);
            if (FAILED(hr) || mapped.pData == nullptr)
            {
                Log(LogLevel::Warning,
                    "Map(physical scope eye %u constants) failed: "
                    "0x%08lx; preserving base stereo without the optic",
                    eye, hr);
                return false;
            }
            std::memcpy(mapped.pData, &values[eye], sizeof(values[eye]));
            context->Unmap(scope_constants[eye].Get(), 0);
        }
        return true;
    }
};

D3D11Compositor::D3D11Compositor() : impl_(std::make_unique<Impl>()) {}

D3D11Compositor::~D3D11Compositor()
{
    Shutdown();
}

bool D3D11Compositor::Initialize(
    OpenXrRuntime& runtime,
    const HostCallbacks& host)
{
    Shutdown();
    if (!runtime.initialized() || runtime.d3d11_device() == nullptr ||
        runtime.d3d11_context() == nullptr)
    {
        return false;
    }
    impl_->host = host;
    impl_->device = runtime.d3d11_device();
    impl_->context = runtime.d3d11_context();
    impl_->ConfigureOrientationDiagnostics();
    if (!impl_->CreatePipeline())
    {
        Shutdown();
        return false;
    }
    impl_->initialized = true;
    return true;
}

void D3D11Compositor::Shutdown()
{
    if (!impl_)
    {
        return;
    }
    impl_->decoded_view.Reset();
    impl_->decoded_texture.Reset();
    for (auto& scope_buffer : impl_->scope_constants)
    {
        scope_buffer.Reset();
    }
    impl_->constants.Reset();
    impl_->reticle_blend.Reset();
    impl_->reticle_pixel_shader.Reset();
    impl_->world_aim_marker_pixel_shader.Reset();
    impl_->scope_pixel_shader.Reset();
    impl_->reticle_vertex_shader.Reset();
    impl_->sampler.Reset();
    impl_->bloom_pixel_shader.Reset();
    impl_->pixel_shader.Reset();
    impl_->vertex_shader.Reset();
    impl_->context.Reset();
    impl_->device.Reset();
    impl_->decoded_width = 0;
    impl_->decoded_height = 0;
    impl_->decoded_source_format = DXGI_FORMAT_UNKNOWN;
    impl_->invalid_bloom_region_logged = false;
    impl_->invalid_scope_geometry_logged = false;
    impl_->initialized = false;
}

bool D3D11Compositor::RenderStereo(
    OpenXrRuntime& runtime,
    const FrameState& frame,
    const D3D11SourceFrame& source,
    const StereoSourceLayout& layout,
    const CompositorReticle* const reticle,
    std::uint32_t* const released_eye_mask,
    const CompositorEffects effects)
{
    ScopedFrameDiagnosticStage submit_timing(runtime, FrameTimingStage::compositor_submit);
    if (released_eye_mask != nullptr)
    {
        *released_eye_mask = 0;
    }
    if (!impl_->initialized || !frame.should_render || !frame.views_valid ||
        source.texture == nullptr || source.view == nullptr ||
        source.width < 2 || source.height == 0)
    {
        return false;
    }
    if (runtime.d3d11_device() != impl_->device.Get() ||
        runtime.d3d11_context() != impl_->context.Get())
    {
        impl_->Log(LogLevel::Error,
                   "Compositor was used with a different OpenXR D3D11 device");
        return false;
    }

    // Normally already begun before native rendering. The opt-in VDXR
    // interop experiment defers only the graphics timing boundary, never the
    // previously sampled head/controller poses or their captured pixels.
    if (!runtime.BeginGraphicsFrame())
    {
        return false;
    }
    runtime.RecordDiagnosticSource(runtime.timing_frame_id(), frame.frame_id, source.serial);
    ID3D11ShaderResourceView* source_view = nullptr;
    {
        ScopedFrameDiagnosticStage prepare_timing(runtime, FrameTimingStage::compositor_prepare);
        source_view = impl_->PrepareSource(source);
    }
    if (source_view == nullptr)
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC source_description = {};
    source.texture->GetDesc(&source_description);
    const char* const orientation_capture_mode = impl_->BeginOrientationCapture(layout);
    if (orientation_capture_mode != nullptr) {
        impl_->Log(LogLevel::Info,
            "OrientationDiag capture begin mode=%s frame=%llu timing_frame=%llu "
            "serial=%llu rendered_views_valid=%u source=%ux%u mapping=identity",
            orientation_capture_mode, static_cast<unsigned long long>(frame.frame_id),
            static_cast<unsigned long long>(runtime.timing_frame_id()),
            static_cast<unsigned long long>(source.serial), source.rendered_views_valid,
            source.width, source.height);
        impl_->CaptureOrientationTexture(source.texture, orientation_capture_mode,
            "source", frame.frame_id, source.serial);
    }

    const bool scope_requested =
        layout.physical_scope.active && impl_->scope_pixel_shader != nullptr;
    const bool draw_physical_scope_for_frame =
        scope_requested && impl_->PrepareScopeConstants(
            layout.physical_scope, frame, source.width, source.height);
    if (scope_requested && !draw_physical_scope_for_frame &&
        !impl_->invalid_scope_geometry_logged)
    {
        impl_->invalid_scope_geometry_logged = true;
        impl_->Log(
            LogLevel::Warning,
            "Physical scope projection/constants were not binocularly ready; "
            "omitted the optional optic overlay while preserving base stereo");
    }

    CompositorReticle world_aim_markers[kEyeCount] = {};
    const bool draw_world_aim_marker_for_frame =
        impl_->world_aim_marker_pixel_shader != nullptr &&
        compositor_detail::BuildWorldAimMarkerProjections(
            layout, frame, source, reticle, world_aim_markers);

    for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
    {
        const NormalizedRect& rectangle = layout.eyes[eye];
        const NormalizedViewport& destination = layout.destinations[eye];
        if (rectangle.width <= 0.0f || rectangle.height <= 0.0f ||
            rectangle.x < 0.0f || rectangle.y < 0.0f ||
            rectangle.x + rectangle.width > 1.0f ||
            rectangle.y + rectangle.height > 1.0f)
        {
            impl_->Log(LogLevel::Error, "Invalid normalized eye source rectangle");
            return false;
        }
        if (!std::isfinite(destination.x) ||
            !std::isfinite(destination.y) ||
            !std::isfinite(destination.width) ||
            !std::isfinite(destination.height) ||
            destination.width <= 0.0f || destination.height <= 0.0f ||
            destination.width > 8.0f || destination.height > 8.0f ||
            destination.x < -8.0f || destination.x > 8.0f ||
            destination.y < -8.0f || destination.y > 8.0f)
        {
            impl_->Log(LogLevel::Error,
                       "Invalid normalized OpenXR destination viewport");
            return false;
        }

        compositor_detail::EyeLocalBloomSamplingRegion bloom_region = {};
        const bool bloom_region_valid =
            effects.enable_eye_local_bloom &&
            compositor_detail::BuildEyeLocalBloomSamplingRegion(
                rectangle, source_description.Width,
                source_description.Height, &bloom_region);
        const bool use_eye_local_bloom =
            compositor_detail::ShouldUseEyeLocalBloom(
                effects, impl_->bloom_pixel_shader != nullptr,
                bloom_region_valid);
        if (effects.enable_eye_local_bloom && !bloom_region_valid &&
            !impl_->invalid_bloom_region_logged)
        {
            impl_->invalid_bloom_region_logged = true;
            impl_->Log(LogLevel::Warning,
                "Eye-local bloom source bounds were invalid; using the "
                "compositor copy path");
        }

        EyeRenderTarget target = {};
        if (!runtime.AcquireEyeImage(eye, &target))
        {
            return false;
        }

        NormalizedViewport effective_destination = destination;
        bool destination_ready = true;
        if (layout.preserve_source_aspect)
        {
            const EyeView& submitted_eye = source.rendered_views_valid
                ? source.rendered_eyes[eye]
                : frame.eyes[eye];
            destination_ready = fit_source_aspect_viewport(
                rectangle, source.width, source.height,
                layout.source_aspect_multipliers[eye], destination,
                submitted_eye.fov,
                &effective_destination);
            if (!destination_ready)
            {
                impl_->Log(LogLevel::Error,
                           "Could not aspect-fit mono source rectangle");
            }
        }

        bool eye_succeeded = destination_ready && impl_->UpdateConstants(
            rectangle, use_eye_local_bloom ? &bloom_region : nullptr);
        if (eye_succeeded)
        {
            D3D11_VIEWPORT viewport = {};
            viewport.TopLeftX =
                effective_destination.x * static_cast<float>(target.width);
            viewport.TopLeftY =
                effective_destination.y * static_cast<float>(target.height);
            viewport.Width =
                effective_destination.width * static_cast<float>(target.width);
            viewport.Height =
                effective_destination.height * static_cast<float>(target.height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            impl_->context->RSSetViewports(1, &viewport);

            const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            impl_->context->ClearRenderTargetView(
                target.render_target, clear_color);
            impl_->context->OMSetRenderTargets(
                1, &target.render_target, nullptr);
            impl_->context->IASetInputLayout(nullptr);
            impl_->context->IASetPrimitiveTopology(
                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            impl_->context->VSSetShader(
                impl_->vertex_shader.Get(), nullptr, 0);
            impl_->context->PSSetShader(
                use_eye_local_bloom
                    ? impl_->bloom_pixel_shader.Get()
                    : impl_->pixel_shader.Get(),
                nullptr, 0);
            ID3D11Buffer* constant_buffer = impl_->constants.Get();
            impl_->context->VSSetConstantBuffers(0, 1, &constant_buffer);
            impl_->context->PSSetConstantBuffers(0, 1, &constant_buffer);
            ID3D11SamplerState* sampler = impl_->sampler.Get();
            impl_->context->PSSetSamplers(0, 1, &sampler);
            impl_->context->PSSetShaderResources(0, 1, &source_view);
            impl_->context->Draw(3, 0);

            if (draw_physical_scope_for_frame)
            {
                D3D11_VIEWPORT scope_viewport = {};
                scope_viewport.Width = static_cast<float>(target.width);
                scope_viewport.Height = static_cast<float>(target.height);
                scope_viewport.MinDepth = 0.0F;
                scope_viewport.MaxDepth = 1.0F;
                impl_->context->RSSetViewports(1, &scope_viewport);
                impl_->context->VSSetShader(
                    impl_->reticle_vertex_shader.Get(), nullptr, 0);
                impl_->context->PSSetShader(
                    impl_->scope_pixel_shader.Get(), nullptr, 0);
                ID3D11Buffer* scope_buffer =
                    impl_->scope_constants[eye].Get();
                impl_->context->VSSetConstantBuffers(0, 1, &scope_buffer);
                impl_->context->PSSetConstantBuffers(0, 1, &scope_buffer);
                impl_->context->Draw(3, 0);
                // The reticle path below uses the ordinary per-eye constants.
                impl_->context->VSSetConstantBuffers(0, 1, &constant_buffer);
                impl_->context->PSSetConstantBuffers(0, 1, &constant_buffer);
            }

            const bool draw_reticle =
                eye_succeeded && reticle != nullptr && reticle->visible &&
                reticle->target_eye == eye &&
                std::isfinite(reticle->u) && std::isfinite(reticle->v) &&
                reticle->u >= 0.0F && reticle->u <= 1.0F &&
                reticle->v >= 0.0F && reticle->v <= 1.0F;
            const CompositorReticle* const eye_marker = draw_reticle
                ? reticle : (draw_world_aim_marker_for_frame
                    ? &world_aim_markers[eye] : nullptr);
            if (eye_marker != nullptr)
            {
                eye_succeeded = impl_->UpdateReticleConstants(
                    *eye_marker, target.width, target.height);
                if (eye_succeeded)
                {
                    D3D11_VIEWPORT reticle_viewport = {};
                    reticle_viewport.Width = static_cast<float>(target.width);
                    reticle_viewport.Height = static_cast<float>(target.height);
                    reticle_viewport.MinDepth = 0.0F;
                    reticle_viewport.MaxDepth = 1.0F;
                    impl_->context->RSSetViewports(1, &reticle_viewport);
                    impl_->context->VSSetShader(
                        impl_->reticle_vertex_shader.Get(), nullptr, 0);
                    impl_->context->PSSetShader(
                        draw_reticle ? impl_->reticle_pixel_shader.Get()
                            : impl_->world_aim_marker_pixel_shader.Get(),
                        nullptr, 0);
                    ID3D11BlendState* blend = impl_->reticle_blend.Get();
                    constexpr float blend_factor[4] = {};
                    impl_->context->OMSetBlendState(
                        blend, blend_factor, 0xffffffffU);
                    impl_->context->Draw(3, 0);
                    impl_->context->OMSetBlendState(
                        nullptr, blend_factor, 0xffffffffU);
                }
            }

            ID3D11ShaderResourceView* null_view = nullptr;
            impl_->context->PSSetShaderResources(0, 1, &null_view);
            ID3D11RenderTargetView* null_target = nullptr;
            impl_->context->OMSetRenderTargets(1, &null_target, nullptr);

            if (orientation_capture_mode != nullptr) {
                const EyeView& submitted_eye = source.rendered_views_valid
                    ? source.rendered_eyes[eye] : frame.eyes[eye];
                const auto& pose = submitted_eye.pose;
                const auto& fov = submitted_eye.fov;
                impl_->Log(LogLevel::Info,
                    "OrientationDiag mode=%s frame=%llu serial=%llu eye=%u "
                    "rect=(%.6f,%.6f,%.6f,%.6f) destination=(%.6f,%.6f,%.6f,%.6f) "
                    "eye_position=(%.6f,%.6f,%.6f) eye_orientation=(%.6f,%.6f,%.6f,%.6f) "
                    "fov_LRUD=(%.6f,%.6f,%.6f,%.6f)",
                    orientation_capture_mode,
                    static_cast<unsigned long long>(frame.frame_id),
                    static_cast<unsigned long long>(source.serial), eye,
                    rectangle.x, rectangle.y, rectangle.width, rectangle.height,
                    effective_destination.x, effective_destination.y,
                    effective_destination.width, effective_destination.height,
                    pose.position.x, pose.position.y, pose.position.z,
                    pose.orientation.x, pose.orientation.y, pose.orientation.z,
                    pose.orientation.w, fov.angle_left, fov.angle_right,
                    fov.angle_up, fov.angle_down);
                ComPtr<ID3D11Resource> output_resource;
                ComPtr<ID3D11Texture2D> output_texture;
                target.render_target->GetResource(output_resource.GetAddressOf());
                const HRESULT texture_result = output_resource.As(&output_texture);
                if (SUCCEEDED(texture_result)) {
                    impl_->CaptureOrientationTexture(output_texture.Get(),
                        orientation_capture_mode, eye == 0 ? "left" : "right",
                        frame.frame_id, source.serial);
                } else {
                    impl_->Log(LogLevel::Warning,
                        "OrientationDiag target resource unavailable eye=%u hr=0x%08lx",
                        eye, texture_result);
                }
            }

            // xrReleaseSwapchainImage transfers the image back to the
            // runtime. Submit the D3D11 draw first; otherwise the runtime can
            // consume an image whose commands are still only queued in the
            // immediate context. Flush at this transfer boundary as well.
            {
                ScopedFrameDiagnosticStage flush_timing(runtime, eye == 0 ?
                    FrameTimingStage::flush_left : FrameTimingStage::flush_right);
                impl_->context->Flush();
            }
        }

        const bool released = runtime.ReleaseEyeImage(eye);
        if (released && released_eye_mask != nullptr)
        {
            *released_eye_mask |= (1u << eye);
        }
        if (!eye_succeeded || !released)
        {
            return false;
        }
    }
    return true;
}

} // namespace wawvr::xr
