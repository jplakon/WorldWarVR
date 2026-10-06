// SPDX-License-Identifier: GPL-3.0-only

#include "d3d11_compositor.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace
{

int failures = 0;

void Expect(const bool condition, const char* const message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool Near(const float left, const float right)
{
    return std::abs(left - right) < 0.000001F;
}

void TestExactCompositorVertexShadersCompile()
{
    const char* const source =
        wawvr::xr::compositor_detail::PhysicalScopeShaderSourceForTesting();
    for (const char* const entry : {"VSMain", "VSReticle"})
    {
        Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT result = D3DCompile(
            source, std::strlen(source), "wawvr_compositor.hlsl", nullptr,
            nullptr, entry, "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
            bytecode.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(result) && errors != nullptr)
        {
            std::cerr << static_cast<const char*>(errors->GetBufferPointer())
                      << '\n';
        }
        Expect(SUCCEEDED(result) && bytecode != nullptr,
               "exact compositor vertex shader compiles");
    }
}

// Exercise the real shader with D3D rasterization and CPU readback. A shader
// compile or a second implementation of its UV equations cannot establish
// which source corner actually reaches a displayed output pixel.
void TestExactShaderOutputOnWarp()
{
    using Microsoft::WRL::ComPtr;
    constexpr UINT size = 64;
    using Image = std::array<std::uint32_t, size * size>;
    auto succeeded = [](HRESULT result, const char* operation) {
        Expect(SUCCEEDED(result), operation);
        return SUCCEEDED(result);
    };

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    if (!succeeded(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            0, levels, 1, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
            context.GetAddressOf()), "create offline WARP device")) return;

    const char* shader_source =
        wawvr::xr::compositor_detail::PhysicalScopeShaderSourceForTesting();
    auto compile = [&](const char* entry, const char* profile,
                       ComPtr<ID3DBlob>& code) {
        ComPtr<ID3DBlob> errors;
        const HRESULT result = D3DCompile(shader_source,
            std::strlen(shader_source), "exact_compositor_readback.hlsl",
            nullptr, nullptr, entry, profile, D3DCOMPILE_ENABLE_STRICTNESS,
            0, code.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(result) && errors)
            std::cerr << static_cast<const char*>(errors->GetBufferPointer());
        return succeeded(result, entry);
    };
    ComPtr<ID3DBlob> vertex_code, overlay_vertex_code, pixel_code, overlay_code;
    if (!compile("VSMain", "vs_4_0", vertex_code) ||
        !compile("VSReticle", "vs_4_0", overlay_vertex_code) ||
        !compile("PSMain", "ps_4_0", pixel_code) ||
        !compile("PSReticle", "ps_4_0", overlay_code)) return;

    ComPtr<ID3D11ShaderReflection> reflection;
    if (!succeeded(D3DReflect(vertex_code->GetBufferPointer(),
            vertex_code->GetBufferSize(), __uuidof(ID3D11ShaderReflection),
            reinterpret_cast<void**>(reflection.GetAddressOf())),
            "reflect actual vertex constant-buffer ABI")) return;
    auto* reflected_constants = reflection->GetConstantBufferByName(
        "CompositorConstants");
    D3D11_SHADER_BUFFER_DESC reflected_buffer{};
    if (!succeeded(reflected_constants->GetDesc(&reflected_buffer),
            "read reflected compositor buffer")) return;
    Expect(reflected_buffer.Size == 144,
           "compiled HLSL constant buffer matches the 144-byte C++ ABI");
    std::array<unsigned char, 144> constants{};
    auto set_pair = [&](const char* name, float first, float second) {
        D3D11_SHADER_VARIABLE_DESC variable{};
        if (!succeeded(reflected_constants->GetVariableByName(name)->GetDesc(
                &variable), name)) return false;
        if (variable.Size != 8 || variable.StartOffset + 8 > constants.size()) {
            Expect(false, "reflected float2 fits the C++ constant buffer");
            return false;
        }
        const float values[] = {first, second};
        std::memcpy(constants.data() + variable.StartOffset, values, sizeof(values));
        return true;
    };
    if (!set_pair("uv_scale", 0.5F, 1.0F) ||
        !set_pair("uv_offset", 0.0F, 0.0F) ||
        !set_pair("reticle_center", 0.25F, 0.25F) ||
        !set_pair("reticle_target_size", float(size), float(size))) return;

    ComPtr<ID3D11VertexShader> vertex, overlay_vertex;
    ComPtr<ID3D11PixelShader> pixel, overlay;
    if (!succeeded(device->CreateVertexShader(vertex_code->GetBufferPointer(),
            vertex_code->GetBufferSize(), nullptr, vertex.GetAddressOf()),
            "create exact base vertex shader") ||
        !succeeded(device->CreateVertexShader(overlay_vertex_code->GetBufferPointer(),
            overlay_vertex_code->GetBufferSize(), nullptr, overlay_vertex.GetAddressOf()),
            "create exact overlay vertex shader") ||
        !succeeded(device->CreatePixelShader(pixel_code->GetBufferPointer(),
            pixel_code->GetBufferSize(), nullptr, pixel.GetAddressOf()),
            "create exact copy pixel shader") ||
        !succeeded(device->CreatePixelShader(overlay_code->GetBufferPointer(),
            overlay_code->GetBufferSize(), nullptr, overlay.GetAddressOf()),
            "create exact reticle pixel shader")) return;

    // Left eye: red/green above blue/yellow. Right eye has distinct colors;
    // this detects rotating around the packed stereo atlas rather than one eye.
    constexpr std::uint32_t red = 0xff0000ff, green = 0xff00ff00;
    constexpr std::uint32_t blue = 0xffff0000, yellow = 0xff00ffff;
    const std::uint32_t colors[8] = {
        red, green, blue, yellow,
        0xffff00ff, 0xffffff00, 0xffffffff, 0xff0080ff};
    std::array<std::uint32_t, 8 * 4> source_pixels{};
    for (UINT y = 0; y < 4; ++y)
        for (UINT x = 0; x < 8; ++x)
            source_pixels[y * 8 + x] = colors[
                (x / 4) * 4 + (y / 2) * 2 + (x % 4) / 2];
    D3D11_TEXTURE2D_DESC description{};
    description.Width = 8;
    description.Height = 4;
    description.MipLevels = description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA source_data = {source_pixels.data(), 8 * 4, 0};
    ComPtr<ID3D11Texture2D> source, target, staging;
    ComPtr<ID3D11ShaderResourceView> source_view;
    if (!succeeded(device->CreateTexture2D(&description, &source_data,
            source.GetAddressOf()), "create packed-eye source") ||
        !succeeded(device->CreateShaderResourceView(source.Get(), nullptr,
            source_view.GetAddressOf()), "create source view")) return;
    description.Width = description.Height = size;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (!succeeded(device->CreateTexture2D(&description, nullptr,
            target.GetAddressOf()), "create readback render target")) return;
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (!succeeded(device->CreateTexture2D(&description, nullptr,
            staging.GetAddressOf()), "create CPU staging texture")) return;
    ComPtr<ID3D11RenderTargetView> target_view;
    if (!succeeded(device->CreateRenderTargetView(target.Get(), nullptr,
            target_view.GetAddressOf()), "create render target view")) return;
    D3D11_BUFFER_DESC buffer_description{};
    buffer_description.ByteWidth = UINT(constants.size());
    buffer_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> constant_buffer;
    if (!succeeded(device->CreateBuffer(&buffer_description, nullptr,
            constant_buffer.GetAddressOf()), "create reflected constant buffer")) return;
    D3D11_SAMPLER_DESC sampler_description{};
    sampler_description.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_description.AddressU = sampler_description.AddressV =
        sampler_description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_description.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;
    if (!succeeded(device->CreateSamplerState(&sampler_description,
            sampler.GetAddressOf()), "create unambiguous point sampler")) return;
    D3D11_RASTERIZER_DESC rasterizer_description{};
    rasterizer_description.FillMode = D3D11_FILL_SOLID;
    rasterizer_description.CullMode = D3D11_CULL_NONE;
    rasterizer_description.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> rasterizer;
    if (!succeeded(device->CreateRasterizerState(&rasterizer_description,
            rasterizer.GetAddressOf()), "create compositor rasterizer")) return;

    auto render = [&](bool reticle, const D3D11_VIEWPORT& viewport,
                      Image& output) {
        context->UpdateSubresource(constant_buffer.Get(), 0, nullptr,
            constants.data(), 0, 0);
        const float clear[] = {0, 0, 0, 0};
        context->ClearRenderTargetView(target_view.Get(), clear);
        ID3D11RenderTargetView* rtv = target_view.Get();
        context->OMSetRenderTargets(1, &rtv, nullptr);
        context->RSSetState(rasterizer.Get());
        context->RSSetViewports(1, &viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(reticle ? overlay_vertex.Get() : vertex.Get(), nullptr, 0);
        context->PSSetShader(reticle ? overlay.Get() : pixel.Get(), nullptr, 0);
        ID3D11Buffer* cb = constant_buffer.Get();
        context->VSSetConstantBuffers(0, 1, &cb);
        context->PSSetConstantBuffers(0, 1, &cb);
        ID3D11ShaderResourceView* srv = source_view.Get();
        ID3D11SamplerState* sampling = sampler.Get();
        context->PSSetShaderResources(0, 1, &srv);
        context->PSSetSamplers(0, 1, &sampling);
        context->Draw(3, 0);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (!succeeded(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0,
                &mapped), "read actual rasterized pixels")) return false;
        for (UINT y = 0; y < size; ++y)
            std::memcpy(output.data() + y * size,
                static_cast<unsigned char*>(mapped.pData) + y * mapped.RowPitch,
                size * sizeof(std::uint32_t));
        context->Unmap(staging.Get(), 0);
        return true;
    };
    const D3D11_VIEWPORT full = {0, 0, float(size), float(size), 0, 1};
    Image baseline{}, right_eye{}, overlay_baseline{};
    if (!render(false, full, baseline) ||
        !render(true, full, overlay_baseline)) return;
    Expect(baseline[8 * size + 8] == red && baseline[8 * size + 56] == green &&
           baseline[56 * size + 8] == blue && baseline[56 * size + 56] == yellow,
           "GPU base fullscreen triangle preserves top-left D3D source orientation");
    if (!set_pair("uv_offset", 0.5F, 0.0F) ||
        !render(false, full, right_eye)) return;
    Expect(right_eye[8 * size + 8] == colors[4] &&
           right_eye[8 * size + 56] == colors[5] &&
           right_eye[56 * size + 8] == colors[6] &&
           right_eye[56 * size + 56] == colors[7],
           "GPU right-eye crop preserves all four corners without swapping eyes");
    Expect((overlay_baseline[16 * size + 16] >> 24) > 180 &&
           (overlay_baseline[48 * size + 48] >> 24) == 0,
           "GPU generated overlay appears at its original OpenXR screen coordinates");

    const D3D11_VIEWPORT asymmetric = {-16, -4, 80, 80, 0, 1};
    Image asymmetric_base{};
    if (!set_pair("uv_offset", 0.0F, 0.0F) ||
        !render(false, asymmetric, asymmetric_base)) return;
    // The input optical axis is halfway through the viewport: x=24, y=36.
    // All output pixels, including the clipped margins, must respect it.
    std::size_t asymmetric_mismatches = 0;
    for (UINT y = 0; y < size; ++y) {
        for (UINT x = 0; x < size; ++x) {
            const auto expected = colors[(y >= 36 ? 2 : 0) + (x >= 24 ? 1 : 0)];
            asymmetric_mismatches += asymmetric_base[y * size + x] != expected;
        }
    }
    Expect(asymmetric_mismatches == 0,
           "GPU asymmetric FOV viewport preserves the intended optical-axis position");
    std::cout << "WARP readback: upright left/right crops and overlay verified; "
              << "asymmetric-viewport mismatches=" << asymmetric_mismatches
              << '\n';
}

void TestPackedEyesHaveDisjointBilinearBounds()
{
    using wawvr::xr::compositor_detail::BuildEyeLocalBloomSamplingRegion;
    using wawvr::xr::compositor_detail::EyeLocalBloomSamplingRegion;

    EyeLocalBloomSamplingRegion left = {};
    EyeLocalBloomSamplingRegion right = {};
    Expect(BuildEyeLocalBloomSamplingRegion(
               {0.0F, 0.0F, 0.5F, 1.0F}, 2560, 1440, &left),
           "left packed-eye bloom bounds build");
    Expect(BuildEyeLocalBloomSamplingRegion(
               {0.5F, 0.0F, 0.5F, 1.0F}, 2560, 1440, &right),
           "right packed-eye bloom bounds build");

    const float half_texel_u = 0.5F / 2560.0F;
    const float half_texel_v = 0.5F / 1440.0F;
    Expect(Near(left.min_u, half_texel_u) &&
               Near(left.max_u, 0.5F - half_texel_u),
           "left taps remain half a texel inside the left crop");
    Expect(Near(right.min_u, 0.5F + half_texel_u) &&
               Near(right.max_u, 1.0F - half_texel_u),
           "right taps remain half a texel inside the right crop");
    Expect(left.max_u < 0.5F && right.min_u > 0.5F &&
               left.max_u < right.min_u,
           "bilinear footprints cannot cross the packed-eye seam");
    Expect(Near(left.min_v, half_texel_v) &&
               Near(left.max_v, 1.0F - half_texel_v) &&
               Near(right.min_v, left.min_v) &&
               Near(right.max_v, left.max_v),
           "vertical taps remain inside both complete-height crops");
}

void TestInvalidSamplingRegionsFailClosed()
{
    using wawvr::xr::compositor_detail::BuildEyeLocalBloomSamplingRegion;
    using wawvr::xr::compositor_detail::EyeLocalBloomSamplingRegion;

    EyeLocalBloomSamplingRegion region = {};
    Expect(!BuildEyeLocalBloomSamplingRegion(
               {0.0F, 0.0F, 0.5F, 1.0F}, 0, 1440, &region),
           "zero-width source rejects bloom");
    Expect(!BuildEyeLocalBloomSamplingRegion(
               {0.0F, 0.0F, 0.5F, 1.0F}, 2560, 1440, nullptr),
           "null bloom-region output fails closed");
    Expect(!BuildEyeLocalBloomSamplingRegion(
               {0.75F, 0.0F, 0.5F, 1.0F}, 2560, 1440, &region),
           "out-of-texture crop rejects bloom");
    Expect(!BuildEyeLocalBloomSamplingRegion(
               {0.0F, 0.0F, 0.25F / 2560.0F, 1.0F}, 2560, 1440,
               &region),
           "sub-texel crop rejects bloom");
    Expect(!BuildEyeLocalBloomSamplingRegion(
               {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.5F, 1.0F},
               2560, 1440, &region),
           "non-finite crop rejects bloom");
}

void TestBloomRequiresExplicitCompleteOptIn()
{
    using wawvr::xr::CompositorEffects;
    using wawvr::xr::compositor_detail::ShouldUseEyeLocalBloom;

    const CompositorEffects default_effects = {};
    Expect(!ShouldUseEyeLocalBloom(default_effects, true, true),
           "default compositor behavior remains the exact copy path");

    CompositorEffects enabled = {};
    enabled.enable_eye_local_bloom = true;
    Expect(!ShouldUseEyeLocalBloom(enabled, false, true),
           "missing optional shader falls back to the copy path");
    Expect(!ShouldUseEyeLocalBloom(enabled, true, false),
           "invalid eye-local bounds fall back to the copy path");
    Expect(ShouldUseEyeLocalBloom(enabled, true, true),
           "explicit opt-in uses bloom only with a complete safe path");
}

void TestExactOptionalBloomShaderCompiles()
{
    const char* const source =
        wawvr::xr::compositor_detail::EyeLocalBloomShaderSourceForTesting();
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(
        source, std::strlen(source), "wawvr_eye_local_bloom.hlsl", nullptr,
        nullptr, "PSBloom", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
        bytecode.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result) && errors != nullptr)
    {
        std::cerr << static_cast<const char*>(errors->GetBufferPointer())
                  << '\n';
    }
    Expect(SUCCEEDED(result) && bytecode != nullptr,
           "the exact optional eye-local bloom shader compiles for ps_4_0");
}

wawvr::xr::FrameState BuildBinocularScopeFrame()
{
    wawvr::xr::FrameState frame = {};
    frame.views_valid = true;
    for (std::uint32_t eye = 0; eye < wawvr::xr::kEyeCount; ++eye)
    {
        frame.eyes[eye].pose.position.x =
            eye == 0 ? -0.032F : 0.032F;
        frame.eyes[eye].fov = {
            -0.78F,
            0.78F,
            0.78F,
            -0.78F,
        };
    }
    return frame;
}

wawvr::xr::PhysicalScopeComposition BuildVisibleScope()
{
    wawvr::xr::PhysicalScopeComposition scope = {};
    scope.active = true;
    scope.source = {0.8F, 0.0F, 0.2F, 1.0F};
    scope.lens_origin_reference_meters = {0.0F, 0.0F, -0.35F};
    scope.lens_right_reference = {1.0F, 0.0F, 0.0F};
    scope.lens_up_reference = {0.0F, 1.0F, 0.0F};
    scope.lens_radius_meters = 0.032F;
    return scope;
}

void TestPhysicalScopeRequiresBinocularlyValidGeometry()
{
    using wawvr::xr::compositor_detail::CanCompositePhysicalScopeBinocularly;

    const wawvr::xr::PhysicalScopeComposition scope = BuildVisibleScope();
    wawvr::xr::FrameState frame = BuildBinocularScopeFrame();
    Expect(CanCompositePhysicalScopeBinocularly(scope, frame, 3200, 1600),
           "a valid lens pose enables the physical optic binocularly");

    frame.eyes[1].pose.orientation = {0.0F, 1.0F, 0.0F, 0.0F};
    Expect(!CanCompositePhysicalScopeBinocularly(scope, frame, 3200, 1600),
           "one invalid eye disables the optional optic for both eyes");

    wawvr::xr::PhysicalScopeComposition inactive = scope;
    inactive.active = false;
    Expect(!CanCompositePhysicalScopeBinocularly(
               inactive, BuildBinocularScopeFrame(), 3200, 1600),
           "an inactive scope remains on the base-stereo path");
}

void TestExactPhysicalScopeShaderCompiles()
{
    const char* const source =
        wawvr::xr::compositor_detail::PhysicalScopeShaderSourceForTesting();
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(
        source, std::strlen(source), "wawvr_physical_scope.hlsl", nullptr,
        nullptr, "PSScope", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
        bytecode.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result) && errors != nullptr)
    {
        std::cerr << static_cast<const char*>(errors->GetBufferPointer())
                  << '\n';
    }
    Expect(SUCCEEDED(result) && bytecode != nullptr,
           "the exact COD4-style physical scope shader compiles for ps_4_0");
}

wawvr::xr::StereoSourceLayout BuildVisibleWorldAimMarker()
{
    wawvr::xr::StereoSourceLayout layout = {};
    layout.world_aim_marker.active = true;
    layout.world_aim_marker.origin_reference_meters = {0.0F, 0.0F, -5.0F};
    return layout;
}

void TestWorldAimMarkerHasPhysicalStereoDepth()
{
    using namespace wawvr::xr;
    using compositor_detail::BuildWorldAimMarkerProjections;
    StereoSourceLayout layout = BuildVisibleWorldAimMarker();
    const FrameState frame = BuildBinocularScopeFrame();
    D3D11SourceFrame source = {};
    CompositorReticle near_marker[kEyeCount] = {};
    CompositorReticle far_marker[kEyeCount] = {};
    Expect(BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, near_marker),
           "world aim marker projects into both eyes");
    Expect(near_marker[0].visible && near_marker[1].visible &&
               near_marker[0].target_eye == 0 && near_marker[1].target_eye == 1,
           "world marker has two explicit eye outputs, not a menu single eye");
    Expect(near_marker[0].u > 0.5F && near_marker[1].u < 0.5F &&
               Near(near_marker[0].v, 0.5F) && Near(near_marker[1].v, 0.5F),
           "one physical target has binocular disparity rather than head lock");

    layout.world_aim_marker.origin_reference_meters.z = -50.0F;
    Expect(BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, far_marker),
           "distant world aim marker projects");
    const float near_disparity = near_marker[0].u - near_marker[1].u;
    const float far_disparity = far_marker[0].u - far_marker[1].u;
    Expect(far_disparity > 0.0F &&
               std::abs(near_disparity - 10.0F * far_disparity) < 0.00001F,
           "marker disparity follows physical target distance");
}

void TestWorldAimMarkerTracksCapturedViewNotHeadCenter()
{
    using namespace wawvr::xr;
    using compositor_detail::BuildWorldAimMarkerProjections;
    const StereoSourceLayout layout = BuildVisibleWorldAimMarker();
    FrameState frame = BuildBinocularScopeFrame();
    D3D11SourceFrame source = {};
    CompositorReticle baseline[kEyeCount] = {};
    CompositorReticle shifted[kEyeCount] = {};
    Expect(BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, baseline),
           "baseline world marker projection succeeds");
    for (auto& eye : frame.eyes)
    {
        eye.pose.position.x += 0.20F;
        eye.pose.position.y += 0.15F;
    }
    Expect(BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, shifted) &&
               shifted[0].u < baseline[0].u && shifted[1].u < baseline[1].u &&
               shifted[0].v > baseline[0].v && shifted[1].v > baseline[1].v,
           "head translation moves a fixed world marker through both images");

    source.rendered_views_valid = true;
    for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
    {
        source.rendered_eyes[eye] = BuildBinocularScopeFrame().eyes[eye];
        frame.eyes[eye].pose.orientation = {0.0F, 1.0F, 0.0F, 0.0F};
    }
    Expect(BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, shifted) &&
               Near(shifted[0].u, baseline[0].u) &&
               Near(shifted[1].u, baseline[1].u),
           "capture-time eye poses override a newer render-worker head pose");

    constexpr float kHalfYaw = 0.10F;
    for (auto& eye : source.rendered_eyes)
    {
        eye.pose.orientation = {0.0F, std::sin(kHalfYaw), 0.0F,
                                std::cos(kHalfYaw)};
    }
    Expect(BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, shifted) &&
               shifted[0].u > baseline[0].u && shifted[1].u > baseline[1].u,
           "captured head yaw changes marker screen position in both eyes");

    source.rendered_eyes[1].fov = {-0.60F, 0.90F, 0.70F, -0.85F};
    CompositorReticle asymmetric[kEyeCount] = {};
    Expect(BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, asymmetric) &&
               Near(asymmetric[0].u, shifted[0].u) &&
               !Near(asymmetric[1].u, shifted[1].u),
           "world marker uses each captured asymmetric eye FOV independently");
}

void TestWorldAimMarkerFailsClosedBinocularly()
{
    using namespace wawvr::xr;
    using compositor_detail::BuildWorldAimMarkerProjections;
    StereoSourceLayout layout = BuildVisibleWorldAimMarker();
    FrameState frame = BuildBinocularScopeFrame();
    D3D11SourceFrame source = {};
    CompositorReticle markers[kEyeCount] = {};
    auto rejected = [&]()
    {
        markers[0].visible = markers[1].visible = true;
        return !BuildWorldAimMarkerProjections(
                   layout, frame, source, nullptr, markers) &&
               !markers[0].visible && !markers[1].visible;
    };
    layout.world_aim_marker.origin_reference_meters.z = 5.0F;
    Expect(rejected(), "behind-head marker hides for both eyes");
    layout = BuildVisibleWorldAimMarker();
    layout.world_aim_marker.origin_reference_meters.x = 100.0F;
    Expect(rejected(), "offscreen marker hides instead of clamping to an edge");
    layout = BuildVisibleWorldAimMarker();
    frame.eyes[1].pose.orientation = {0.0F, 1.0F, 0.0F, 0.0F};
    Expect(rejected(), "one behind-eye projection hides both marker outputs");
    frame = BuildBinocularScopeFrame();
    frame.eyes[1].fov.angle_right = std::numeric_limits<float>::quiet_NaN();
    Expect(rejected(), "one non-finite FOV hides both marker outputs");
    frame = BuildBinocularScopeFrame();
    frame.eyes[1].pose.orientation = {0.0F, 0.0F, 0.0F, 0.0F};
    Expect(rejected(), "invalid eye orientation hides both marker outputs");
    frame = BuildBinocularScopeFrame();
    layout.world_aim_marker.origin_reference_meters.x =
        std::numeric_limits<float>::infinity();
    Expect(rejected(), "non-finite target hides both marker outputs");
    layout = BuildVisibleWorldAimMarker();
    frame.views_valid = false;
    Expect(rejected(), "invalid frame views hide both marker outputs");
    Expect(!BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, nullptr),
           "null projection output fails closed");
}

void TestWorldAimMarkerDoesNotReplaceMenusOrScopes()
{
    using namespace wawvr::xr;
    using compositor_detail::BuildWorldAimMarkerProjections;
    StereoSourceLayout layout = {};
    const FrameState frame = BuildBinocularScopeFrame();
    D3D11SourceFrame source = {};
    CompositorReticle markers[kEyeCount] = {};
    Expect(!BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, markers),
           "default layout does not add a crosshair to other gameplay");
    layout = BuildVisibleWorldAimMarker();
    layout.preserve_source_aspect = true;
    Expect(!BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, markers),
           "mono menu and cinematic layouts never show a world marker");
    layout = BuildVisibleWorldAimMarker();
    layout.physical_scope = BuildVisibleScope();
    Expect(!BuildWorldAimMarkerProjections(
               layout, frame, source, nullptr, markers),
           "physical scopes retain their dedicated optic crosshair");
    Expect(compositor_detail::CanCompositePhysicalScopeBinocularly(
               layout.physical_scope, frame, 3200, 1600),
           "world-marker suppression does not alter physical scope geometry");
    layout = BuildVisibleWorldAimMarker();
    CompositorReticle menu = {true, 1, 0.25F, 0.75F};
    Expect(!BuildWorldAimMarkerProjections(
               layout, frame, source, &menu, markers) && menu.visible &&
               menu.target_eye == 1 && Near(menu.u, 0.25F) && Near(menu.v, 0.75F),
           "visible menu pointer suppresses the marker without changing menu UV");
}

void TestExactWorldAimMarkerShaderCompiles()
{
    const char* const source =
        wawvr::xr::compositor_detail::WorldAimMarkerShaderSourceForTesting();
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(
        source, std::strlen(source), "wawvr_world_aim_marker.hlsl", nullptr,
        nullptr, "PSWorldAimMarker", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
        bytecode.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result) && errors != nullptr)
    {
        std::cerr << static_cast<const char*>(errors->GetBufferPointer()) << '\n';
    }
    Expect(SUCCEEDED(result) && bytecode != nullptr,
           "exact outlined binocular world-marker shader compiles for ps_4_0");
}

} // namespace

int main()
{
    TestExactCompositorVertexShadersCompile();
    TestExactShaderOutputOnWarp();
    TestPackedEyesHaveDisjointBilinearBounds();
    TestInvalidSamplingRegionsFailClosed();
    TestBloomRequiresExplicitCompleteOptIn();
    TestPhysicalScopeRequiresBinocularlyValidGeometry();
    TestExactOptionalBloomShaderCompiles();
    TestExactPhysicalScopeShaderCompiles();
    TestWorldAimMarkerHasPhysicalStereoDepth();
    TestWorldAimMarkerTracksCapturedViewNotHeadCenter();
    TestWorldAimMarkerFailsClosedBinocularly();
    TestWorldAimMarkerDoesNotReplaceMenusOrScopes();
    TestExactWorldAimMarkerShaderCompiles();
    if (failures != 0)
    {
        std::cerr << failures << " D3D11 compositor test(s) failed\n";
        return 1;
    }
    std::cout << "D3D11 compositor tests passed\n";
    return 0;
}
