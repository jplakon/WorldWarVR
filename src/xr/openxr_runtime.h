// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "xr_types.h"
#include "frame_timing_diagnostics.hpp"

#include <chrono>
#include <cstdint>
#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace wawvr::xr
{

enum class ReferenceSpace : std::uint8_t
{
    Local,
    Stage,
};

// Describes the most recent failed Initialize call without leaking OpenXR
// loader types into the renderer integration boundary.
enum class InitializationFailure : std::uint8_t
{
    none,
    runtime_unavailable,
    headset_unavailable,
    other,
};

struct RuntimeConfig
{
    const char* application_name = "World War VR";
    const char* engine_name = "World War VR";
    std::uint32_t application_version = 1;
    std::uint32_t engine_version = 1;
    float render_scale = 1.0f;
    ReferenceSpace reference_space = ReferenceSpace::Local;
    bool prefer_srgb_swapchain = true;
};

struct CompositionLayerSubmission final
{
    CompositionLayerKind kind{CompositionLayerKind::none};
    const D3D11SourceFrame* projection_source{};
    const QuadLayer* quad{};
};

struct EndFrameResult final
{
    bool frame_ended{};
    CompositionLayerKind accepted_kind{CompositionLayerKind::none};
};

// Owns OpenXR, its runtime-selected D3D11 device, action spaces, and one
// color swapchain per eye. No IW/T4 type is visible at this boundary.
class OpenXrRuntime final
{
public:
    OpenXrRuntime();
    ~OpenXrRuntime();

    OpenXrRuntime(const OpenXrRuntime&) = delete;
    OpenXrRuntime& operator=(const OpenXrRuntime&) = delete;

    bool Initialize(const RuntimeConfig& config, const HostCallbacks& host);
    void Shutdown();

    // PollEvents may begin/end the session in response to runtime events.
    // BeginFrame returns false until the session reaches READY.
    bool PollEvents();
    bool BeginFrame(FrameState* frame);
    // Idempotently begin native graphics for the active waited frame. The
    // default path already begins in BeginFrame. The opt-in VDXR interop test
    // defers only this native call, never prediction or input/view sampling.
    // Call before any compositor D3D11 commands; EndFrame also covers paths
    // with no compositor work (reused/zero-layer frames).
    bool BeginGraphicsFrame();
    bool AcquireEyeImage(std::uint32_t eye, EyeRenderTarget* target);
    bool ReleaseEyeImage(std::uint32_t eye);
    // The composition primitive is explicit: a quad request can never fall
    // through to projection submission when its metadata is unavailable.
    // accepted_kind reports what xrEndFrame actually received after local
    // validation and a successful runtime submission.
    EndFrameResult EndFrame(
        const FrameState& frame,
        const CompositionLayerSubmission& submission = {});

    bool ApplyHaptic(
        Hand hand,
        float amplitude,
        float duration_seconds,
        float frequency_hz = 0.0f);
    bool StopHaptic(Hand hand);
    bool RequestExit();

    bool initialized() const;
    bool session_running() const;
    bool session_focused() const;
    bool exit_requested() const;
    InitializationFailure last_initialization_failure() const;
    const char* active_runtime_name() const;
    std::uint64_t active_runtime_version() const;
    ID3D11Device* d3d11_device() const;
    ID3D11DeviceContext* d3d11_context() const;

    // Optional CPU-only instrumentation. Call from the existing serialized
    // XR owner. No clocks, graphics queries, waits or logs on disabled paths.
    bool timing_diagnostics_enabled() const;
    std::uint64_t timing_frame_id() const;
    void RecordDiagnosticStage(std::uint64_t frame_id, FrameTimingStage stage,
                               std::chrono::steady_clock::duration elapsed);
    void RecordDiagnosticSource(std::uint64_t frame_id,
                                std::uint64_t source_frame_id,
                                std::uint64_t source_serial);
    void RecordDiagnosticPresentation(std::uint64_t frame_id,
                                      bool observed_stereo, bool reused_layer);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class ScopedFrameDiagnosticStage final {
public:
    ScopedFrameDiagnosticStage(OpenXrRuntime& runtime, FrameTimingStage stage);
    ~ScopedFrameDiagnosticStage();
    ScopedFrameDiagnosticStage(const ScopedFrameDiagnosticStage&) = delete;
    ScopedFrameDiagnosticStage& operator=(const ScopedFrameDiagnosticStage&) = delete;
private:
    OpenXrRuntime* runtime_{};
    FrameTimingStage stage_{};
    std::uint64_t frame_id_{};
    std::chrono::steady_clock::time_point started_{};
};

} // namespace wawvr::xr
