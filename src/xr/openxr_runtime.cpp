// SPDX-License-Identifier: GPL-3.0-only
// WorldAtWarVR OpenXR runtime implementation. Comparative research history,
// standards basis, and source-comparison scope are documented in
// Khronos OpenXR SDK terms are recorded in THIRD-PARTY-NOTICES.md.

#include "openxr_runtime.h"
#include "runtime_policy.hpp"
#include "frame_begin_lifecycle.hpp"

#if !defined(_WIN32)
#error WorldAtWarVR's OpenXR D3D11 backend currently supports Windows only.
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wawvr::xr
{

using Microsoft::WRL::ComPtr;

namespace
{

constexpr std::array<const char*, 1> kRequiredExtensions = {
    XR_KHR_D3D11_ENABLE_EXTENSION_NAME,
};

// Never let a wedged graphics/runtime path block WaW's main thread forever.
// One tenth of a second is already several headset refresh intervals; on a
// timeout the owning session is recreated instead of leaving the HMD loading.
constexpr XrDuration kSwapchainImageWaitTimeout = 100'000'000;

bool EnvironmentFlagEnabled(const wchar_t* const name)
{
    wchar_t value[2] = {};
    return GetEnvironmentVariableW(name, value, 2) == 1 &&
           value[0] == L'1';
}

template <std::size_t Size>
void CopyXrString(char (&destination)[Size], const char* source)
{
    std::snprintf(destination, Size, "%s", source != nullptr ? source : "");
    destination[Size - 1] = '\0';
}

Posef FromXrPose(const XrPosef& pose)
{
    return {
        {pose.orientation.x, pose.orientation.y, pose.orientation.z,
         pose.orientation.w},
        {pose.position.x, pose.position.y, pose.position.z},
    };
}

Fovf FromXrFov(const XrFovf& fov)
{
    return {fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown};
}

XrPosef ToXrPose(const Posef& pose)
{
    XrPosef result = {};
    result.orientation = {
        pose.orientation.x,
        pose.orientation.y,
        pose.orientation.z,
        pose.orientation.w,
    };
    result.position = {
        pose.position.x,
        pose.position.y,
        pose.position.z,
    };
    return result;
}

XrFovf ToXrFov(const Fovf& fov)
{
    return {
        fov.angle_left,
        fov.angle_right,
        fov.angle_up,
        fov.angle_down,
    };
}

bool IsSrgbFormat(const std::int64_t format)
{
    return format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
           format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}

bool IsValidQuadLayer(const QuadLayer& quad)
{
    const auto finite = [](const float value)
    {
        return std::isfinite(value);
    };
    const Quaternionf& orientation = quad.pose.orientation;
    const float orientation_length_squared =
        orientation.x * orientation.x + orientation.y * orientation.y +
        orientation.z * orientation.z + orientation.w * orientation.w;
    return quad.source_eye < kEyeCount &&
           finite(quad.pose.position.x) && finite(quad.pose.position.y) &&
           finite(quad.pose.position.z) && finite(orientation.x) &&
           finite(orientation.y) && finite(orientation.z) &&
           finite(orientation.w) && finite(orientation_length_squared) &&
           orientation_length_squared >= 0.98F &&
           orientation_length_squared <= 1.02F &&
           finite(quad.size_meters.x) && finite(quad.size_meters.y) &&
           quad.size_meters.x > 0.0F && quad.size_meters.y > 0.0F;
}

} // namespace

struct OpenXrRuntime::Impl
{
    struct Swapchain
    {
        XrSwapchain handle = XR_NULL_HANDLE;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool acquired = false;
        bool waited = false;
        std::uint32_t acquired_index = 0;
        std::vector<XrSwapchainImageD3D11KHR> images;
        std::vector<ComPtr<ID3D11RenderTargetView>> render_targets;
    };

    struct Actions
    {
        XrActionSet set = XR_NULL_HANDLE;
        XrAction grip_pose = XR_NULL_HANDLE;
        XrAction aim_pose = XR_NULL_HANDLE;
        XrAction trigger = XR_NULL_HANDLE;
        XrAction trigger_click = XR_NULL_HANDLE;
        XrAction squeeze = XR_NULL_HANDLE;
        XrAction stick = XR_NULL_HANDLE;
        XrAction primary = XR_NULL_HANDLE;
        XrAction secondary = XR_NULL_HANDLE;
        XrAction stick_click = XR_NULL_HANDLE;
        XrAction thumbrest = XR_NULL_HANDLE;
        XrAction menu = XR_NULL_HANDLE;
        XrAction haptic = XR_NULL_HANDLE;
        XrPath hands[kHandCount] = {XR_NULL_PATH, XR_NULL_PATH};
        XrSpace grip_spaces[kHandCount] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
        XrSpace aim_spaces[kHandCount] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    } actions;

    RuntimeConfig config = {};
    HostCallbacks host = {};
    std::string application_name;
    std::string engine_name;
    std::string active_runtime_name;
    std::uint64_t active_runtime_version = 0;
    InitializationFailure last_initialization_failure =
        InitializationFailure::none;

    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace app_space = XR_NULL_HANDLE;
    XrReferenceSpaceType app_reference_type = XR_REFERENCE_SPACE_TYPE_LOCAL;
    // Observation-only VIEW space. Never substituted into camera/input state.
    XrSpace orientation_diagnostic_view_space = XR_NULL_HANDLE;
    bool orientation_diagnostics = false;
    std::uint32_t orientation_diagnostic_reports = 0;
    std::uint64_t orientation_diagnostic_last_frame = 0;
    XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
    bool initialized = false;
    bool session_running = false;
    bool session_focused = false;
    bool exit_requested = false;
    FrameBeginLifecycle frame_lifecycle{};
    bool defer_graphics_begin = false;
    bool current_should_render = false;
    bool current_views_valid = false;
    bool predicted_display_period_logged = false;
    std::uint64_t frame_id = 0;
    std::uint64_t action_sequence = 0;
    XrTime current_display_time = 0;
    bool frame_timing_diagnostics = false;
    std::uint64_t timing_wait_samples = 0;
    std::uint64_t timing_end_samples = 0;
    std::uint64_t timing_should_not_render = 0;
    std::uint64_t timing_inferred_missed_intervals = 0;
    std::uint64_t timing_period_nanoseconds_sum = 0;
    double timing_wait_ms_sum = 0.0;
    double timing_wait_ms_max = 0.0;
    double timing_period_ms_max = 0.0;
    double timing_predicted_step_ms_sum = 0.0;
    double timing_predicted_step_ms_max = 0.0;
    std::uint64_t timing_predicted_step_samples = 0;
    double timing_application_ms_sum = 0.0;
    double timing_application_ms_max = 0.0;
    double timing_end_frame_ms_sum = 0.0;
    double timing_end_frame_ms_max = 0.0;
    XrTime timing_previous_predicted_display_time = 0;
    XrDuration timing_previous_predicted_display_period = 0;
    std::chrono::steady_clock::time_point current_frame_begin_wall{};
    FrameTimingSample current_detailed_timing{};
    std::array<FrameLayerTimingWindow, 3> detailed_timing_windows{};
    std::chrono::steady_clock::time_point detailed_report_started{};
    std::uint64_t invalid_quad_occurrences = 0;
    std::uint64_t invalid_projection_occurrences = 0;

    PFN_xrGetD3D11GraphicsRequirementsKHR get_d3d11_requirements = nullptr;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_9_1;

    std::array<XrViewConfigurationView, kEyeCount> view_configurations = {};
    std::array<XrView, kEyeCount> views = {};
    std::array<Swapchain, kEyeCount> swapchains = {};
    std::int64_t swapchain_format = DXGI_FORMAT_UNKNOWN;
    XrEnvironmentBlendMode blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

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

    const char* ResultName(const XrResult result) const
    {
        if (result == XR_ERROR_RUNTIME_UNAVAILABLE)
        {
            return "XR_ERROR_RUNTIME_UNAVAILABLE (-51)";
        }
        static thread_local char buffer[XR_MAX_RESULT_STRING_SIZE] = {};
        if (instance != XR_NULL_HANDLE &&
            XR_SUCCEEDED(xrResultToString(instance, result, buffer)))
        {
            return buffer;
        }
        std::snprintf(buffer, sizeof(buffer), "XrResult(%d)", result);
        return buffer;
    }

    bool Check(const XrResult result, const char* operation) const
    {
        if (XR_SUCCEEDED(result))
        {
            return true;
        }
        Log(LogLevel::Error, "%s failed: %s", operation, ResultName(result));
        return false;
    }

    void RecordFrameWait(
        const XrFrameState& state,
        const std::chrono::steady_clock::duration wait_duration)
    {
        if (!frame_timing_diagnostics)
        {
            return;
        }
        const double wait_ms =
            std::chrono::duration<double, std::milli>(wait_duration).count();
        const double period_ms = duration_milliseconds(
            state.predictedDisplayPeriod);
        if (state.predictedDisplayPeriod > 0)
        {
            const double previous_period_ms = duration_milliseconds(
                timing_previous_predicted_display_period);
            if (timing_previous_predicted_display_period > 0 &&
                std::abs(period_ms - previous_period_ms) >= 0.5)
            {
                Log(LogLevel::Warning,
                    "OpenXR predicted display period changed from %.3f ms (%.2f Hz) to %.3f ms (%.2f Hz); runtime-side half-rate reprojection may have changed state",
                    previous_period_ms,
                    predicted_refresh_hz(
                        timing_previous_predicted_display_period),
                    period_ms,
                    predicted_refresh_hz(state.predictedDisplayPeriod));
            }
            timing_previous_predicted_display_period =
                state.predictedDisplayPeriod;
        }
        timing_wait_samples += 1;
        timing_wait_ms_sum += wait_ms;
        timing_wait_ms_max = std::max(timing_wait_ms_max, wait_ms);
        timing_period_ms_max = std::max(timing_period_ms_max, period_ms);
        if (state.predictedDisplayPeriod > 0)
        {
            timing_period_nanoseconds_sum += static_cast<std::uint64_t>(
                state.predictedDisplayPeriod);
        }
        timing_should_not_render += state.shouldRender != XR_TRUE ? 1u : 0u;
        if (timing_previous_predicted_display_time != 0 &&
            state.predictedDisplayTime > timing_previous_predicted_display_time)
        {
            const XrDuration predicted_step =
                state.predictedDisplayTime -
                timing_previous_predicted_display_time;
            const double step_ms = duration_milliseconds(predicted_step);
            timing_predicted_step_samples += 1;
            timing_predicted_step_ms_sum += step_ms;
            timing_predicted_step_ms_max =
                std::max(timing_predicted_step_ms_max, step_ms);
            timing_inferred_missed_intervals +=
                inferred_missed_display_intervals(
                    predicted_step, state.predictedDisplayPeriod);
        }
        timing_previous_predicted_display_time = state.predictedDisplayTime;
    }

    void ReportFrameTimingIfReady()
    {
        if (!should_report_frame_timing(
                timing_period_nanoseconds_sum, timing_wait_samples))
        {
            return;
        }
        const auto average_period_nanoseconds = static_cast<std::int64_t>(
            timing_period_nanoseconds_sum / timing_wait_samples);
        const double average_period_ms = duration_milliseconds(
            average_period_nanoseconds);
        const double predicted_hz = predicted_refresh_hz(
            average_period_nanoseconds);
        const double average_predicted_step_ms =
            timing_predicted_step_samples != 0
                ? timing_predicted_step_ms_sum /
                      static_cast<double>(timing_predicted_step_samples)
                : 0.0;
        const double average_application_ms =
            timing_end_samples != 0
                ? timing_application_ms_sum /
                      static_cast<double>(timing_end_samples)
                : 0.0;
        const double average_end_frame_ms =
            timing_end_samples != 0
                ? timing_end_frame_ms_sum /
                      static_cast<double>(timing_end_samples)
                : 0.0;
        Log(LogLevel::Info,
            "OpenXR frame timing (%llu frames): predictedDisplayPeriod avg/max=%.3f/%.3f ms (%.2f Hz), predictedStep avg/max=%.3f/%.3f ms, inferredMissedDisplayIntervals=%llu, xrWaitFrame avg/max=%.3f/%.3f ms, app-work avg/max=%.3f/%.3f ms, xrEndFrame avg/max=%.3f/%.3f ms, shouldRender=false=%llu",
            static_cast<unsigned long long>(timing_wait_samples),
            average_period_ms, timing_period_ms_max, predicted_hz,
            average_predicted_step_ms, timing_predicted_step_ms_max,
            static_cast<unsigned long long>(
                timing_inferred_missed_intervals),
            timing_wait_ms_sum / static_cast<double>(timing_wait_samples),
            timing_wait_ms_max, average_application_ms,
            timing_application_ms_max, average_end_frame_ms,
            timing_end_frame_ms_max,
            static_cast<unsigned long long>(timing_should_not_render));

        timing_wait_samples = 0;
        timing_end_samples = 0;
        timing_should_not_render = 0;
        timing_inferred_missed_intervals = 0;
        timing_period_nanoseconds_sum = 0;
        timing_wait_ms_sum = 0.0;
        timing_wait_ms_max = 0.0;
        timing_period_ms_max = 0.0;
        timing_predicted_step_ms_sum = 0.0;
        timing_predicted_step_ms_max = 0.0;
        timing_predicted_step_samples = 0;
        timing_application_ms_sum = 0.0;
        timing_application_ms_max = 0.0;
        timing_end_frame_ms_sum = 0.0;
        timing_end_frame_ms_max = 0.0;
    }

    void ReportDetailedFrameTiming(
        const std::chrono::steady_clock::time_point now)
    {
        if (detailed_report_started == std::chrono::steady_clock::time_point{}) {
            detailed_report_started = now;
            return;
        }
        if (now - detailed_report_started < std::chrono::seconds(10)) return;
        for (std::size_t layer = 0; layer < detailed_timing_windows.size(); ++layer) {
            auto& window = detailed_timing_windows[layer];
            if (window.frames == 0) continue;
            Log(LogLevel::Info,
                "XR pacing window layer=%s frames=%llu failedEnd=%llu observedStereo/mono/unknown=%llu/%llu/%llu reused=%llu; CPU-wall timings only; p95/p99 are histogram upper bounds (inf means >336ms); nested stages must not be summed",
                kFrameLayerTimingNames[layer],
                static_cast<unsigned long long>(window.frames),
                static_cast<unsigned long long>(window.failed_ends),
                static_cast<unsigned long long>(window.observed_stereo_frames),
                static_cast<unsigned long long>(window.observed_mono_frames),
                static_cast<unsigned long long>(window.unknown_presentation_frames),
                static_cast<unsigned long long>(window.reused_layers));
            for (std::size_t start = 0; start < kFrameTimingStageCount; start += 4) {
                char line[960] = {};
                std::size_t used = static_cast<std::size_t>(std::snprintf(
                    line, sizeof(line), "XR pacing stages layer=%s:",
                    kFrameLayerTimingNames[layer]));
                bool any = false;
                for (std::size_t i = start; i < std::min(start + 4, kFrameTimingStageCount); ++i) {
                    const auto& timing = window.stages[i];
                    if (timing.count == 0) continue;
                    any = true;
                    const int written = std::snprintf(line + used, sizeof(line) - used,
                        " %s n=%llu avg/p95/p99/max=%.3f/%.3f/%.3f/%.3fms;",
                        kFrameTimingStageNames[i], static_cast<unsigned long long>(timing.count),
                        static_cast<double>(timing.total_ns) / static_cast<double>(timing.count) / 1e6,
                        timing.percentile_upper_ms(95), timing.percentile_upper_ms(99),
                        static_cast<double>(timing.max_ns) / 1e6);
                    if (written < 0 || static_cast<std::size_t>(written) >= sizeof(line) - used) break;
                    used += static_cast<std::size_t>(written);
                }
                if (any) Log(LogLevel::Info, "%s", line);
            }
            for (std::size_t metric = 0; metric < window.worst.size(); ++metric) {
                for (const auto& sample : window.worst[metric]) {
                    if (sample.frame_id == 0) continue;
                    const auto ms = [&](FrameTimingStage stage) {
                        return static_cast<double>(sample.value(stage)) / 1e6;
                    };
                    Log(LogLevel::Info,
                        "XR pacing worst layer=%s rankBy=%s frame=%llu display=%lld period=%.3fms source=%llu serial=%llu render=%u endOK=%u contextValid/stereo/reused=%u/%u/%u mask=0x%X wait/begin/app/end=%.3f/%.3f/%.3f/%.3fms capture/present/sharedAcquire/sharedRelease=%.3f/%.3f/%.3f/%.3fms prepare/submit=%.3f/%.3fms acquireLR=%.3f/%.3f waitLR=%.3f/%.3f releaseLR=%.3f/%.3f flushLR=%.3f/%.3fms",
                        kFrameLayerTimingNames[layer],
                        kFrameTimingStageNames[static_cast<std::size_t>(FrameLayerTimingWindow::kRankStages[metric])],
                        static_cast<unsigned long long>(sample.frame_id), static_cast<long long>(sample.display_time),
                        static_cast<double>(sample.display_period) / 1e6,
                        static_cast<unsigned long long>(sample.source_frame_id), static_cast<unsigned long long>(sample.source_serial),
                        sample.should_render ? 1u : 0u, sample.end_succeeded ? 1u : 0u,
                        sample.presentation_valid ? 1u : 0u, sample.observed_stereo ? 1u : 0u,
                        sample.reused_layer ? 1u : 0u, sample.stage_mask,
                        ms(FrameTimingStage::wait_frame), ms(FrameTimingStage::begin_frame),
                        ms(FrameTimingStage::application), ms(FrameTimingStage::end_frame),
                        ms(FrameTimingStage::d3d9_capture), ms(FrameTimingStage::d3d9_present),
                        ms(FrameTimingStage::shared_acquire), ms(FrameTimingStage::shared_release),
                        ms(FrameTimingStage::compositor_prepare), ms(FrameTimingStage::compositor_submit),
                        ms(FrameTimingStage::acquire_left), ms(FrameTimingStage::acquire_right),
                        ms(FrameTimingStage::wait_left), ms(FrameTimingStage::wait_right),
                        ms(FrameTimingStage::release_left), ms(FrameTimingStage::release_right),
                        ms(FrameTimingStage::flush_left), ms(FrameTimingStage::flush_right));
                }
            }
            window = {};
        }
        detailed_report_started = now;
    }

    void RecordFrameEnd(
        const std::chrono::steady_clock::duration application_duration,
        const std::chrono::steady_clock::duration end_frame_duration,
        const CompositionLayerKind prepared_kind, const bool end_succeeded,
        const std::chrono::steady_clock::time_point end_finished)
    {
        if (!frame_timing_diagnostics)
        {
            return;
        }
        const double application_ms =
            std::chrono::duration<double, std::milli>(
                application_duration).count();
        const double end_frame_ms =
            std::chrono::duration<double, std::milli>(
                end_frame_duration).count();
        timing_end_samples += 1;
        timing_application_ms_sum += application_ms;
        timing_application_ms_max =
            std::max(timing_application_ms_max, application_ms);
        timing_end_frame_ms_sum += end_frame_ms;
        timing_end_frame_ms_max =
            std::max(timing_end_frame_ms_max, end_frame_ms);
        current_detailed_timing.kind = prepared_kind;
        current_detailed_timing.end_succeeded = end_succeeded;
        current_detailed_timing.add(frame_id, FrameTimingStage::application,
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(application_duration).count()));
        current_detailed_timing.add(frame_id, FrameTimingStage::end_frame,
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end_frame_duration).count()));
        detailed_timing_windows[frame_layer_timing_index(prepared_kind)].add(current_detailed_timing);
        ReportDetailedFrameTiming(end_finished);
        ReportFrameTimingIfReady();
    }

    bool CreateInstance()
    {
        std::uint32_t extension_count = 0;
        if (!Check(
                xrEnumerateInstanceExtensionProperties(
                    nullptr, 0, &extension_count, nullptr),
                "xrEnumerateInstanceExtensionProperties(count)"))
        {
            return false;
        }

        std::vector<XrExtensionProperties> extensions(
            extension_count, {XR_TYPE_EXTENSION_PROPERTIES});
        if (!Check(
                xrEnumerateInstanceExtensionProperties(
                    nullptr,
                    extension_count,
                    &extension_count,
                    extensions.data()),
                "xrEnumerateInstanceExtensionProperties"))
        {
            return false;
        }

        for (const char* required : kRequiredExtensions)
        {
            const bool found = std::any_of(
                extensions.begin(), extensions.end(),
                [required](const XrExtensionProperties& extension)
                {
                    return std::strcmp(extension.extensionName, required) == 0;
                });
            if (!found)
            {
                Log(LogLevel::Error, "OpenXR runtime lacks required extension %s", required);
                return false;
            }
        }

        XrInstanceCreateInfo create_info = {XR_TYPE_INSTANCE_CREATE_INFO};
        CopyXrString(create_info.applicationInfo.applicationName,
                     application_name.c_str());
        create_info.applicationInfo.applicationVersion =
            config.application_version;
        CopyXrString(create_info.applicationInfo.engineName,
                     engine_name.c_str());
        create_info.applicationInfo.engineVersion = config.engine_version;
        // The core intentionally uses only OpenXR 1.0 functionality. Asking
        // for 1.0 keeps older conformant runtimes usable even though the
        // vendored headers/loader are newer.
        create_info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
        create_info.enabledExtensionCount =
            static_cast<std::uint32_t>(kRequiredExtensions.size());
        create_info.enabledExtensionNames = kRequiredExtensions.data();

        const XrResult create_result = xrCreateInstance(&create_info, &instance);
        if (XR_FAILED(create_result))
        {
            last_initialization_failure =
                create_result == XR_ERROR_RUNTIME_UNAVAILABLE
                    ? InitializationFailure::runtime_unavailable
                    : InitializationFailure::other;
            Check(create_result, "xrCreateInstance");
            return false;
        }

        XrInstanceProperties instance_properties = {
            XR_TYPE_INSTANCE_PROPERTIES};
        const XrResult properties_result =
            xrGetInstanceProperties(instance, &instance_properties);
        if (XR_SUCCEEDED(properties_result))
        {
            active_runtime_name = instance_properties.runtimeName;
            active_runtime_version = instance_properties.runtimeVersion;
            Log(
                LogLevel::Info,
                "Active OpenXR runtime: %s %llu.%llu.%llu",
                active_runtime_name.c_str(),
                static_cast<unsigned long long>(
                    XR_VERSION_MAJOR(active_runtime_version)),
                static_cast<unsigned long long>(
                    XR_VERSION_MINOR(active_runtime_version)),
                static_cast<unsigned long long>(
                    XR_VERSION_PATCH(active_runtime_version)));
        }
        else
        {
            Check(properties_result, "xrGetInstanceProperties");
            active_runtime_name = "<runtime name unavailable>";
        }

        XrSystemGetInfo system_info = {XR_TYPE_SYSTEM_GET_INFO};
        system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        const XrResult system_result =
            xrGetSystem(instance, &system_info, &system);
        if (XR_FAILED(system_result))
        {
            last_initialization_failure =
                system_result == XR_ERROR_FORM_FACTOR_UNAVAILABLE
                    ? InitializationFailure::headset_unavailable
                    : system_result == XR_ERROR_RUNTIME_UNAVAILABLE
                        ? InitializationFailure::runtime_unavailable
                        : InitializationFailure::other;
            Check(system_result, "xrGetSystem");
            return false;
        }
        return true;
    }

    bool CreateD3D11Device()
    {
        PFN_xrVoidFunction function = nullptr;
        if (!Check(
                xrGetInstanceProcAddr(
                    instance,
                    "xrGetD3D11GraphicsRequirementsKHR",
                    &function),
                "xrGetInstanceProcAddr(xrGetD3D11GraphicsRequirementsKHR)"))
        {
            return false;
        }
        get_d3d11_requirements =
            reinterpret_cast<PFN_xrGetD3D11GraphicsRequirementsKHR>(function);

        XrGraphicsRequirementsD3D11KHR requirements = {
            XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        if (!Check(
                get_d3d11_requirements(instance, system, &requirements),
                "xrGetD3D11GraphicsRequirementsKHR"))
        {
            return false;
        }

        ComPtr<IDXGIFactory1> factory;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(hr))
        {
            Log(LogLevel::Error, "CreateDXGIFactory1 failed: 0x%08lx", hr);
            return false;
        }

        for (UINT index = 0;; ++index)
        {
            ComPtr<IDXGIAdapter1> candidate;
            if (factory->EnumAdapters1(index, candidate.GetAddressOf()) ==
                DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            DXGI_ADAPTER_DESC1 description = {};
            if (SUCCEEDED(candidate->GetDesc1(&description)) &&
                description.AdapterLuid.HighPart ==
                    requirements.adapterLuid.HighPart &&
                description.AdapterLuid.LowPart == requirements.adapterLuid.LowPart)
            {
                adapter = candidate;
                break;
            }
        }

        if (adapter == nullptr)
        {
            Log(LogLevel::Error, "Could not find the OpenXR runtime's DXGI adapter");
            return false;
        }

        constexpr std::array<D3D_FEATURE_LEVEL, 7> all_feature_levels = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
            D3D_FEATURE_LEVEL_9_3,
            D3D_FEATURE_LEVEL_9_2,
            D3D_FEATURE_LEVEL_9_1,
        };
        std::vector<D3D_FEATURE_LEVEL> feature_levels;
        for (const D3D_FEATURE_LEVEL level : all_feature_levels)
        {
            if (level >= requirements.minFeatureLevel)
            {
                feature_levels.push_back(level);
            }
        }
        if (feature_levels.empty())
        {
            feature_levels.push_back(requirements.minFeatureLevel);
        }

        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        hr = D3D11CreateDevice(
            adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            flags,
            feature_levels.data(),
            static_cast<UINT>(feature_levels.size()),
            D3D11_SDK_VERSION,
            device.GetAddressOf(),
            &feature_level,
            context.GetAddressOf());

        if (hr == E_INVALIDARG &&
            !feature_levels.empty() &&
            feature_levels.front() == D3D_FEATURE_LEVEL_11_1)
        {
            feature_levels.erase(feature_levels.begin());
            hr = D3D11CreateDevice(
                adapter.Get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                flags,
                feature_levels.data(),
                static_cast<UINT>(feature_levels.size()),
                D3D11_SDK_VERSION,
                device.GetAddressOf(),
                &feature_level,
                context.GetAddressOf());
        }

        if (FAILED(hr))
        {
            Log(LogLevel::Error, "D3D11CreateDevice failed: 0x%08lx", hr);
            return false;
        }
        return true;
    }

    bool CreateSessionAndSpace()
    {
        XrGraphicsBindingD3D11KHR binding = {
            XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
        binding.device = device.Get();

        XrSessionCreateInfo session_info = {XR_TYPE_SESSION_CREATE_INFO};
        session_info.next = &binding;
        session_info.systemId = system;
        if (!Check(xrCreateSession(instance, &session_info, &session),
                   "xrCreateSession"))
        {
            return false;
        }

        XrReferenceSpaceCreateInfo space_info = {
            XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        space_info.poseInReferenceSpace.orientation.w = 1.0f;
        space_info.referenceSpaceType =
            prefer_stage_reference_space(active_runtime_name.c_str(),
                config.reference_space == ReferenceSpace::Stage)
                ? XR_REFERENCE_SPACE_TYPE_STAGE
                : XR_REFERENCE_SPACE_TYPE_LOCAL;

        XrResult result = xrCreateReferenceSpace(session, &space_info, &app_space);
        if (result == XR_ERROR_REFERENCE_SPACE_UNSUPPORTED &&
            space_info.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE)
        {
            Log(LogLevel::Warning,
                "Stage reference space unavailable; falling back to local");
            space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
            result = xrCreateReferenceSpace(session, &space_info, &app_space);
        }
        if (!Check(result, "xrCreateReferenceSpace"))
        {
            return false;
        }
        app_reference_type = space_info.referenceSpaceType;
        Log(LogLevel::Info,
            "OpenXR application reference space: %s; eyes, hands and submitted layers share this space; no image or pointer axis inversion",
            app_reference_type == XR_REFERENCE_SPACE_TYPE_STAGE ? "STAGE" : "LOCAL");

        if (orientation_diagnostics)
        {
            XrReferenceSpaceCreateInfo view_info = {
                XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
            view_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
            view_info.poseInReferenceSpace.orientation.w = 1.0f;
            const XrResult view_result = xrCreateReferenceSpace(
                session, &view_info, &orientation_diagnostic_view_space);
            Log(XR_SUCCEEDED(view_result) ? LogLevel::Info : LogLevel::Warning,
                "OrientationDiag observation-only VIEW space: %s; sparse reports capped at 64, spacing 90 frames",
                ResultName(view_result));
        }

        std::uint32_t blend_mode_count = 0;
        if (!Check(
                xrEnumerateEnvironmentBlendModes(
                    instance,
                    system,
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                    0,
                    &blend_mode_count,
                    nullptr),
                "xrEnumerateEnvironmentBlendModes(count)"))
        {
            return false;
        }
        std::vector<XrEnvironmentBlendMode> blend_modes(blend_mode_count);
        if (!Check(
                xrEnumerateEnvironmentBlendModes(
                    instance,
                    system,
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                    blend_mode_count,
                    &blend_mode_count,
                    blend_modes.data()),
                "xrEnumerateEnvironmentBlendModes"))
        {
            return false;
        }
        constexpr std::array<XrEnvironmentBlendMode, 3> preferences = {
            XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
            XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND,
            XR_ENVIRONMENT_BLEND_MODE_ADDITIVE,
        };
        const auto selected = std::find_first_of(
            preferences.begin(),
            preferences.end(),
            blend_modes.begin(),
            blend_modes.end());
        if (selected == preferences.end())
        {
            Log(LogLevel::Error, "Runtime exposes no supported environment blend mode");
            return false;
        }
        blend_mode = *selected;
        return true;
    }

    XrPath Path(const char* text) const
    {
        XrPath path = XR_NULL_PATH;
        if (!Check(xrStringToPath(instance, text, &path), text))
        {
            return XR_NULL_PATH;
        }
        return path;
    }

    bool CreateAction(
        const XrActionType type,
        const char* name,
        const char* localized_name,
        const bool per_hand,
        XrAction* action)
    {
        XrActionCreateInfo info = {XR_TYPE_ACTION_CREATE_INFO};
        info.actionType = type;
        CopyXrString(info.actionName, name);
        CopyXrString(info.localizedActionName, localized_name);
        if (per_hand)
        {
            info.countSubactionPaths = kHandCount;
            info.subactionPaths = actions.hands;
        }
        return Check(xrCreateAction(actions.set, &info, action), name);
    }

    bool SuggestBindings(
        const char* profile_name,
        const std::vector<XrActionSuggestedBinding>& bindings)
    {
        const XrPath profile = Path(profile_name);
        if (profile == XR_NULL_PATH)
        {
            return false;
        }
        XrInteractionProfileSuggestedBinding suggestion = {
            XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggestion.interactionProfile = profile;
        suggestion.countSuggestedBindings =
            static_cast<std::uint32_t>(bindings.size());
        suggestion.suggestedBindings = bindings.data();
        const XrResult result =
            xrSuggestInteractionProfileBindings(instance, &suggestion);
        if (XR_FAILED(result))
        {
            Log(LogLevel::Warning, "Bindings for %s were rejected: %s",
                profile_name, ResultName(result));
            return false;
        }
        return true;
    }

    bool CreateActions()
    {
        actions.hands[0] = Path("/user/hand/left");
        actions.hands[1] = Path("/user/hand/right");
        if (actions.hands[0] == XR_NULL_PATH ||
            actions.hands[1] == XR_NULL_PATH)
        {
            return false;
        }

        XrActionSetCreateInfo set_info = {XR_TYPE_ACTION_SET_CREATE_INFO};
        CopyXrString(set_info.actionSetName, "gameplay");
        CopyXrString(set_info.localizedActionSetName, "Gameplay");
        set_info.priority = 0;
        if (!Check(xrCreateActionSet(instance, &set_info, &actions.set),
                   "xrCreateActionSet"))
        {
            return false;
        }

        if (!CreateAction(XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip pose",
                          true, &actions.grip_pose) ||
            !CreateAction(XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim pose",
                          true, &actions.aim_pose) ||
            !CreateAction(XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger",
                          true, &actions.trigger) ||
            !CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "trigger_click",
                          "Trigger click", true, &actions.trigger_click) ||
            !CreateAction(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Squeeze",
                          true, &actions.squeeze) ||
            !CreateAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "stick", "Thumbstick",
                          true, &actions.stick) ||
            !CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "primary",
                          "Primary button", true, &actions.primary) ||
            !CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary",
                          "Secondary button", true, &actions.secondary) ||
            !CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_click",
                          "Thumbstick click", true, &actions.stick_click) ||
            !CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbrest_touch",
                          "Thumbrest touch", true, &actions.thumbrest) ||
            !CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu", true,
                          &actions.menu) ||
            !CreateAction(XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Haptic",
                          true, &actions.haptic))
        {
            return false;
        }

        const auto binding = [this](XrAction action, const char* path)
        {
            return XrActionSuggestedBinding{action, Path(path)};
        };

        std::vector<XrActionSuggestedBinding> touch = {
            binding(actions.grip_pose, "/user/hand/left/input/grip/pose"),
            binding(actions.grip_pose, "/user/hand/right/input/grip/pose"),
            binding(actions.aim_pose, "/user/hand/left/input/aim/pose"),
            binding(actions.aim_pose, "/user/hand/right/input/aim/pose"),
            binding(actions.trigger, "/user/hand/left/input/trigger/value"),
            binding(actions.trigger, "/user/hand/right/input/trigger/value"),
            binding(actions.squeeze, "/user/hand/left/input/squeeze/value"),
            binding(actions.squeeze, "/user/hand/right/input/squeeze/value"),
            binding(actions.stick, "/user/hand/left/input/thumbstick"),
            binding(actions.stick, "/user/hand/right/input/thumbstick"),
            binding(actions.primary, "/user/hand/left/input/x/click"),
            binding(actions.primary, "/user/hand/right/input/a/click"),
            binding(actions.secondary, "/user/hand/left/input/y/click"),
            binding(actions.secondary, "/user/hand/right/input/b/click"),
            binding(actions.stick_click,
                    "/user/hand/left/input/thumbstick/click"),
            binding(actions.stick_click,
                    "/user/hand/right/input/thumbstick/click"),
            binding(actions.thumbrest,
                    "/user/hand/right/input/thumbrest/touch"),
            binding(actions.menu, "/user/hand/left/input/menu/click"),
            binding(actions.haptic, "/user/hand/left/output/haptic"),
            binding(actions.haptic, "/user/hand/right/output/haptic"),
        };
        SuggestBindings("/interaction_profiles/oculus/touch_controller", touch);

        std::vector<XrActionSuggestedBinding> index = {
            binding(actions.grip_pose, "/user/hand/left/input/grip/pose"),
            binding(actions.grip_pose, "/user/hand/right/input/grip/pose"),
            binding(actions.aim_pose, "/user/hand/left/input/aim/pose"),
            binding(actions.aim_pose, "/user/hand/right/input/aim/pose"),
            binding(actions.trigger, "/user/hand/left/input/trigger/value"),
            binding(actions.trigger, "/user/hand/right/input/trigger/value"),
            binding(actions.trigger_click,
                    "/user/hand/left/input/trigger/click"),
            binding(actions.trigger_click,
                    "/user/hand/right/input/trigger/click"),
            binding(actions.squeeze, "/user/hand/left/input/squeeze/value"),
            binding(actions.squeeze, "/user/hand/right/input/squeeze/value"),
            binding(actions.stick, "/user/hand/left/input/thumbstick"),
            binding(actions.stick, "/user/hand/right/input/thumbstick"),
            binding(actions.primary, "/user/hand/left/input/a/click"),
            binding(actions.primary, "/user/hand/right/input/a/click"),
            binding(actions.secondary, "/user/hand/left/input/b/click"),
            binding(actions.secondary, "/user/hand/right/input/b/click"),
            binding(actions.stick_click,
                    "/user/hand/left/input/thumbstick/click"),
            binding(actions.stick_click,
                    "/user/hand/right/input/thumbstick/click"),
            binding(actions.haptic, "/user/hand/left/output/haptic"),
            binding(actions.haptic, "/user/hand/right/output/haptic"),
        };
        SuggestBindings("/interaction_profiles/valve/index_controller", index);

        std::vector<XrActionSuggestedBinding> simple = {
            binding(actions.grip_pose, "/user/hand/left/input/grip/pose"),
            binding(actions.grip_pose, "/user/hand/right/input/grip/pose"),
            binding(actions.aim_pose, "/user/hand/left/input/aim/pose"),
            binding(actions.aim_pose, "/user/hand/right/input/aim/pose"),
            binding(actions.trigger_click,
                    "/user/hand/left/input/select/click"),
            binding(actions.trigger_click,
                    "/user/hand/right/input/select/click"),
            binding(actions.menu, "/user/hand/left/input/menu/click"),
            binding(actions.haptic, "/user/hand/left/output/haptic"),
            binding(actions.haptic, "/user/hand/right/output/haptic"),
        };
        SuggestBindings("/interaction_profiles/khr/simple_controller", simple);

        XrActionSpaceCreateInfo space_info = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
        space_info.poseInActionSpace.orientation.w = 1.0f;
        for (std::uint32_t hand = 0; hand < kHandCount; ++hand)
        {
            space_info.subactionPath = actions.hands[hand];
            space_info.action = actions.grip_pose;
            if (!Check(xrCreateActionSpace(
                           session, &space_info, &actions.grip_spaces[hand]),
                       "xrCreateActionSpace(grip)"))
            {
                return false;
            }
            space_info.action = actions.aim_pose;
            if (!Check(xrCreateActionSpace(
                           session, &space_info, &actions.aim_spaces[hand]),
                       "xrCreateActionSpace(aim)"))
            {
                return false;
            }
        }

        XrSessionActionSetsAttachInfo attach_info = {
            XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach_info.countActionSets = 1;
        attach_info.actionSets = &actions.set;
        return Check(xrAttachSessionActionSets(session, &attach_info),
                     "xrAttachSessionActionSets");
    }

    bool CreateSwapchains()
    {
        std::uint32_t view_count = 0;
        if (!Check(
                xrEnumerateViewConfigurationViews(
                    instance,
                    system,
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                    0,
                    &view_count,
                    nullptr),
                "xrEnumerateViewConfigurationViews(count)"))
        {
            return false;
        }
        if (view_count != kEyeCount)
        {
            Log(LogLevel::Error, "Runtime reported %u stereo views; expected 2",
                view_count);
            return false;
        }

        for (auto& view : view_configurations)
        {
            view = {XR_TYPE_VIEW_CONFIGURATION_VIEW};
        }
        if (!Check(
                xrEnumerateViewConfigurationViews(
                    instance,
                    system,
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                    kEyeCount,
                    &view_count,
                    view_configurations.data()),
                "xrEnumerateViewConfigurationViews"))
        {
            return false;
        }

        std::uint32_t format_count = 0;
        if (!Check(xrEnumerateSwapchainFormats(
                       session, 0, &format_count, nullptr),
                   "xrEnumerateSwapchainFormats(count)"))
        {
            return false;
        }
        std::vector<std::int64_t> formats(format_count);
        if (!Check(xrEnumerateSwapchainFormats(
                       session,
                       format_count,
                       &format_count,
                       formats.data()),
                   "xrEnumerateSwapchainFormats"))
        {
            return false;
        }

        const std::array<std::int64_t, 4> srgb_first = {
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_B8G8R8A8_UNORM,
        };
        const std::array<std::int64_t, 4> linear_first = {
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
        };
        const auto& preferences =
            config.prefer_srgb_swapchain ? srgb_first : linear_first;
        for (const std::int64_t preferred : preferences)
        {
            if (std::find(formats.begin(), formats.end(), preferred) !=
                formats.end())
            {
                swapchain_format = preferred;
                break;
            }
        }
        if (swapchain_format == DXGI_FORMAT_UNKNOWN)
        {
            Log(LogLevel::Error, "No supported RGBA/BGRA OpenXR swapchain format");
            return false;
        }

        const float render_scale = std::clamp(config.render_scale, 0.25f, 2.0f);
        for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
        {
            Swapchain& swapchain = swapchains[eye];
            swapchain.width = std::max(
                1u,
                static_cast<std::uint32_t>(std::lround(
                    view_configurations[eye].recommendedImageRectWidth *
                    render_scale)));
            swapchain.height = std::max(
                1u,
                static_cast<std::uint32_t>(std::lround(
                    view_configurations[eye].recommendedImageRectHeight *
                    render_scale)));

            XrSwapchainCreateInfo info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
            info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            info.format = swapchain_format;
            info.sampleCount = 1;
            info.width = swapchain.width;
            info.height = swapchain.height;
            info.faceCount = 1;
            info.arraySize = 1;
            info.mipCount = 1;
            if (!Check(xrCreateSwapchain(session, &info, &swapchain.handle),
                       "xrCreateSwapchain"))
            {
                return false;
            }

            std::uint32_t image_count = 0;
            if (!Check(xrEnumerateSwapchainImages(
                           swapchain.handle, 0, &image_count, nullptr),
                       "xrEnumerateSwapchainImages(count)"))
            {
                return false;
            }
            swapchain.images.assign(
                image_count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            if (!Check(xrEnumerateSwapchainImages(
                           swapchain.handle,
                           image_count,
                           &image_count,
                           reinterpret_cast<XrSwapchainImageBaseHeader*>(
                               swapchain.images.data())),
                       "xrEnumerateSwapchainImages"))
            {
                return false;
            }

            swapchain.render_targets.resize(image_count);
            for (std::uint32_t image = 0; image < image_count; ++image)
            {
                // OpenXR runtimes may expose a typeless D3D11 texture even
                // though the swapchain was created with a typed format.
                // Always specify the negotiated typed view explicitly.
                D3D11_RENDER_TARGET_VIEW_DESC view_description = {};
                view_description.Format =
                    static_cast<DXGI_FORMAT>(swapchain_format);
                view_description.ViewDimension =
                    D3D11_RTV_DIMENSION_TEXTURE2D;
                view_description.Texture2D.MipSlice = 0;
                const HRESULT hr = device->CreateRenderTargetView(
                    swapchain.images[image].texture,
                    &view_description,
                    swapchain.render_targets[image].GetAddressOf());
                if (FAILED(hr))
                {
                    Log(LogLevel::Error,
                        "CreateRenderTargetView(OpenXR eye %u) failed: 0x%08lx",
                        eye, hr);
                    return false;
                }
            }
        }
        Log(LogLevel::Info,
            "OpenXR eye swapchains: left recommended=%ux%u selected=%ux%u; right recommended=%ux%u selected=%ux%u; render scale=%.2f",
            view_configurations[0].recommendedImageRectWidth,
            view_configurations[0].recommendedImageRectHeight,
            swapchains[0].width,
            swapchains[0].height,
            view_configurations[1].recommendedImageRectWidth,
            view_configurations[1].recommendedImageRectHeight,
            swapchains[1].width,
            swapchains[1].height,
            render_scale);
        return true;
    }

    BoolActionState ReadBool(XrAction action, XrPath hand) const
    {
        XrActionStateGetInfo info = {XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        info.subactionPath = hand;
        XrActionStateBoolean state = {XR_TYPE_ACTION_STATE_BOOLEAN};
        if (XR_FAILED(xrGetActionStateBoolean(session, &info, &state)))
        {
            return {};
        }
        return {
            state.isActive == XR_TRUE,
            state.currentState == XR_TRUE,
            state.changedSinceLastSync == XR_TRUE,
        };
    }

    FloatActionState ReadFloat(XrAction action, XrPath hand) const
    {
        XrActionStateGetInfo info = {XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        info.subactionPath = hand;
        XrActionStateFloat state = {XR_TYPE_ACTION_STATE_FLOAT};
        if (XR_FAILED(xrGetActionStateFloat(session, &info, &state)))
        {
            return {};
        }
        return {
            state.isActive == XR_TRUE,
            state.currentState,
            state.changedSinceLastSync == XR_TRUE,
        };
    }

    Vec2ActionState ReadVec2(XrAction action, XrPath hand) const
    {
        XrActionStateGetInfo info = {XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        info.subactionPath = hand;
        XrActionStateVector2f state = {XR_TYPE_ACTION_STATE_VECTOR2F};
        if (XR_FAILED(xrGetActionStateVector2f(session, &info, &state)))
        {
            return {};
        }
        return {
            state.isActive == XR_TRUE,
            {state.currentState.x, state.currentState.y},
            state.changedSinceLastSync == XR_TRUE,
        };
    }

    TrackedPose ReadPose(XrAction action, XrSpace space, XrPath hand) const
    {
        XrActionStateGetInfo info = {XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        info.subactionPath = hand;
        XrActionStatePose state = {XR_TYPE_ACTION_STATE_POSE};
        if (XR_FAILED(xrGetActionStatePose(session, &info, &state)) ||
            state.isActive != XR_TRUE)
        {
            return {};
        }

        XrSpaceLocation location = {XR_TYPE_SPACE_LOCATION};
        if (XR_FAILED(xrLocateSpace(
                space, app_space, current_display_time, &location)))
        {
            return {};
        }
        TrackedPose result = {};
        result.active = true;
        result.orientation_valid =
            (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
        result.position_valid =
            (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
        result.orientation_tracked =
            (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
        result.position_tracked =
            (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
        result.pose = FromXrPose(location.pose);
        return result;
    }

    void ReadActions(ActionSnapshot* snapshot)
    {
        *snapshot = {};
        snapshot->focused = session_focused;
        snapshot->sequence = ++action_sequence;

        XrActiveActionSet active = {};
        active.actionSet = actions.set;
        active.subactionPath = XR_NULL_PATH;
        XrActionsSyncInfo sync_info = {XR_TYPE_ACTIONS_SYNC_INFO};
        sync_info.countActiveActionSets = 1;
        sync_info.activeActionSets = &active;
        const XrResult result = xrSyncActions(session, &sync_info);
        if (XR_FAILED(result))
        {
            if (result != XR_SESSION_NOT_FOCUSED)
            {
                Log(LogLevel::Warning, "xrSyncActions failed: %s",
                    ResultName(result));
            }
            return;
        }

        BoolActionState menu_states[kHandCount] = {};
        for (std::uint32_t hand = 0; hand < kHandCount; ++hand)
        {
            HandActionState& output = snapshot->hands[hand];
            const XrPath hand_path = actions.hands[hand];
            output.grip = ReadPose(
                actions.grip_pose, actions.grip_spaces[hand], hand_path);
            output.aim = ReadPose(
                actions.aim_pose, actions.aim_spaces[hand], hand_path);
            output.trigger = ReadFloat(actions.trigger, hand_path);
            output.trigger_click = ReadBool(actions.trigger_click, hand_path);
            if (!output.trigger.active && output.trigger_click.active)
            {
                output.trigger.active = true;
                output.trigger.current = output.trigger_click.current ? 1.0f : 0.0f;
                output.trigger.changed = output.trigger_click.changed;
            }
            output.squeeze = ReadFloat(actions.squeeze, hand_path);
            output.stick = ReadVec2(actions.stick, hand_path);
            output.primary = ReadBool(actions.primary, hand_path);
            output.secondary = ReadBool(actions.secondary, hand_path);
            output.stick_click = ReadBool(actions.stick_click, hand_path);
            output.thumbrest = ReadBool(actions.thumbrest, hand_path);
            menu_states[hand] = ReadBool(actions.menu, hand_path);
        }
        snapshot->menu.active = menu_states[0].active || menu_states[1].active;
        snapshot->menu.current = menu_states[0].current || menu_states[1].current;
        snapshot->menu.changed = menu_states[0].changed || menu_states[1].changed;
    }

    void RecordOrientationDiagnostics(const FrameState& frame)
    {
        constexpr std::uint32_t kMaximumReports = 64;
        constexpr std::uint64_t kReportSpacingFrames = 90;
        if (!orientation_diagnostics || !frame.views_valid ||
            orientation_diagnostic_reports >= kMaximumReports ||
            (orientation_diagnostic_reports != 0 &&
             frame.frame_id - orientation_diagnostic_last_frame <
                 kReportSpacingFrames))
        {
            return;
        }
        const std::uint32_t report = ++orientation_diagnostic_reports;
        orientation_diagnostic_last_frame = frame.frame_id;
        const auto frame_number = static_cast<unsigned long long>(frame.frame_id);
        const auto sequence = static_cast<unsigned long long>(frame.actions.sequence);
        Log(LogLevel::Info,
            "OrientationDiag report=%u frame=%llu sequence=%llu time=%lld focused=%u shouldRender=%u runtime=%s",
            report, frame_number, sequence,
            static_cast<long long>(frame.predicted_display_time),
            frame.actions.focused ? 1u : 0u, frame.should_render ? 1u : 0u,
            active_runtime_name.c_str());
        for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
        {
            // Report the untouched xrLocateViews output, not a derived basis.
            const auto& raw = views[eye];
            const auto& p = raw.pose.position;
            const auto& q = raw.pose.orientation;
            Log(LogLevel::Info,
                "OrientationDiag report=%u frame=%llu sequence=%llu eye=%u position=(%.6f,%.6f,%.6f) xyzw=(%.7f,%.7f,%.7f,%.7f) fovLRUD=(%.7f,%.7f,%.7f,%.7f)",
                report, frame_number, sequence, eye, p.x, p.y, p.z,
                q.x, q.y, q.z, q.w, raw.fov.angleLeft, raw.fov.angleRight,
                raw.fov.angleUp, raw.fov.angleDown);
        }
        XrSpaceLocation view_location = {XR_TYPE_SPACE_LOCATION};
        XrResult view_result = XR_ERROR_HANDLE_INVALID;
        if (orientation_diagnostic_view_space != XR_NULL_HANDLE)
        {
            view_result = xrLocateSpace(orientation_diagnostic_view_space,
                app_space, frame.predicted_display_time, &view_location);
        }
        const auto& vp = view_location.pose.position;
        const auto& vq = view_location.pose.orientation;
        Log(LogLevel::Info,
            "OrientationDiag report=%u frame=%llu sequence=%llu VIEW-in-app result=%d flags=0x%llX position=(%.6f,%.6f,%.6f) xyzw=(%.7f,%.7f,%.7f,%.7f)",
            report, frame_number, sequence, static_cast<int>(view_result),
            static_cast<unsigned long long>(view_location.locationFlags),
            vp.x, vp.y, vp.z, vq.x, vq.y, vq.z, vq.w);
        for (std::uint32_t hand = 0; hand < kHandCount; ++hand)
        {
            const auto& state = frame.actions.hands[hand];
            const TrackedPose* poses[] = {&state.grip, &state.aim};
            const char* names[] = {"grip", "aim"};
            for (std::uint32_t kind = 0; kind < 2; ++kind)
            {
                const auto& tracked = *poses[kind];
                const auto& p = tracked.pose.position;
                const auto& q = tracked.pose.orientation;
                Log(LogLevel::Info,
                    "OrientationDiag report=%u frame=%llu sequence=%llu hand=%u pose=%s active=%u validOP=%u%u trackedOP=%u%u position=(%.6f,%.6f,%.6f) xyzw=(%.7f,%.7f,%.7f,%.7f) squeezeActive=%u squeeze=%.4f",
                    report, frame_number, sequence, hand, names[kind],
                    tracked.active ? 1u : 0u,
                    tracked.orientation_valid ? 1u : 0u,
                    tracked.position_valid ? 1u : 0u,
                    tracked.orientation_tracked ? 1u : 0u,
                    tracked.position_tracked ? 1u : 0u,
                    p.x, p.y, p.z, q.x, q.y, q.z, q.w,
                    state.squeeze.active ? 1u : 0u, state.squeeze.current);
            }
        }
    }

    void DestroySwapchains()
    {
        for (Swapchain& swapchain : swapchains)
        {
            swapchain.render_targets.clear();
            swapchain.images.clear();
            if (swapchain.handle != XR_NULL_HANDLE)
            {
                xrDestroySwapchain(swapchain.handle);
            }
            swapchain = {};
        }
    }

    void DestroyActions()
    {
        for (XrSpace& space : actions.grip_spaces)
        {
            if (space != XR_NULL_HANDLE)
            {
                xrDestroySpace(space);
            }
        }
        for (XrSpace& space : actions.aim_spaces)
        {
            if (space != XR_NULL_HANDLE)
            {
                xrDestroySpace(space);
            }
        }
        if (actions.set != XR_NULL_HANDLE)
        {
            xrDestroyActionSet(actions.set);
        }
        actions = {};
    }
};

OpenXrRuntime::OpenXrRuntime() : impl_(std::make_unique<Impl>()) {}

OpenXrRuntime::~OpenXrRuntime()
{
    Shutdown();
}

bool OpenXrRuntime::Initialize(
    const RuntimeConfig& config,
    const HostCallbacks& host)
{
    Shutdown();
    impl_->config = config;
    impl_->host = host;
    impl_->application_name =
        config.application_name != nullptr ? config.application_name : "World War VR";
    impl_->engine_name =
        config.engine_name != nullptr ? config.engine_name : "World War VR";
    impl_->frame_timing_diagnostics =
        EnvironmentFlagEnabled(L"WAWVR_FRAME_TIMING_DIAGNOSTICS");
    impl_->orientation_diagnostics =
        EnvironmentFlagEnabled(L"WAWVR_ORIENTATION_DIAGNOSTICS");

    if (!impl_->CreateInstance() ||
        !impl_->CreateD3D11Device() ||
        !impl_->CreateSessionAndSpace() ||
        !impl_->CreateActions() ||
        !impl_->CreateSwapchains())
    {
        const InitializationFailure failure =
            impl_->last_initialization_failure == InitializationFailure::none
                ? InitializationFailure::other
                : impl_->last_initialization_failure;
        std::string runtime_name = std::move(impl_->active_runtime_name);
        const std::uint64_t runtime_version = impl_->active_runtime_version;
        Shutdown();
        impl_->last_initialization_failure = failure;
        impl_->active_runtime_name = std::move(runtime_name);
        impl_->active_runtime_version = runtime_version;
        return false;
    }

    const bool defer_begin_requested =
        EnvironmentFlagEnabled(L"WAWVR_DEFER_XR_BEGIN_FRAME");
    impl_->defer_graphics_begin = defer_graphics_begin_for_runtime(
        defer_begin_requested,
        EnvironmentFlagEnabled(L"WAWVR_DEFER_XR_BEGIN_FRAME_SIMULATOR_TEST"),
        impl_->active_runtime_name);
    impl_->initialized = true;
    if (defer_begin_requested)
    {
        impl_->Log(LogLevel::Info,
                   "Deferred native xrBeginFrame interop experiment: %s for runtime '%s' (Wait/prediction/input remain early; Begin precedes D3D11 composition, not native D3D9 rendering)",
                   impl_->defer_graphics_begin ? "ENABLED" : "REJECTED by runtime gate",
                   impl_->active_runtime_name.c_str());
    }
    impl_->Log(LogLevel::Info,
               "OpenXR initialized with D3D feature level 0x%x and %s swapchains",
               impl_->feature_level,
               IsSrgbFormat(impl_->swapchain_format) ? "sRGB" : "linear");
    if (impl_->frame_timing_diagnostics)
    {
        impl_->Log(LogLevel::Info,
                   "Sparse OpenXR frame-timing diagnostics enabled (10-second layer-split correlated pacing windows plus legacy 60-second aggregates; no per-frame logging or additional graphics waits/queries)");
    }
    return true;
}

void OpenXrRuntime::Shutdown()
{
    if (!impl_)
    {
        return;
    }

    for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
    {
        Impl::Swapchain& swapchain = impl_->swapchains[eye];
        if (swapchain.acquired && swapchain.waited)
        {
            XrSwapchainImageReleaseInfo release_info = {
                XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            if (XR_SUCCEEDED(xrReleaseSwapchainImage(
                    swapchain.handle, &release_info)))
            {
                swapchain.acquired = false;
                swapchain.waited = false;
            }
        }
    }
    // A healthy deferred frame still needs Wait -> Begin -> End if teardown
    // arrives before composition. Do not invent Begin on a stopping/lost
    // session or after Begin failed; direct session destruction is sufficient.
    if (impl_->frame_lifecycle.needs_begin() && impl_->session_running &&
        !impl_->exit_requested &&
        impl_->session_state != XR_SESSION_STATE_STOPPING &&
        impl_->session_state != XR_SESSION_STATE_EXITING &&
        impl_->session_state != XR_SESSION_STATE_LOSS_PENDING)
    {
        BeginGraphicsFrame();
    }
    if (impl_->frame_lifecycle.begun() && impl_->session != XR_NULL_HANDLE)
    {
        XrFrameEndInfo end_info = {XR_TYPE_FRAME_END_INFO};
        end_info.displayTime = impl_->current_display_time;
        end_info.environmentBlendMode = impl_->blend_mode;
        xrEndFrame(impl_->session, &end_info);
    }
    impl_->frame_lifecycle = {};

    // xrEndSession is application-callable only after the runtime reports
    // STOPPING. Direct session destruction is the valid teardown elsewhere.
    if (impl_->session_running &&
        impl_->session_state == XR_SESSION_STATE_STOPPING &&
        impl_->session != XR_NULL_HANDLE)
    {
        xrEndSession(impl_->session);
    }
    impl_->session_running = false;

    impl_->DestroySwapchains();
    impl_->DestroyActions();

    if (impl_->orientation_diagnostic_view_space != XR_NULL_HANDLE)
    {
        xrDestroySpace(impl_->orientation_diagnostic_view_space);
        impl_->orientation_diagnostic_view_space = XR_NULL_HANDLE;
    }
    if (impl_->app_space != XR_NULL_HANDLE)
    {
        xrDestroySpace(impl_->app_space);
        impl_->app_space = XR_NULL_HANDLE;
    }
    if (impl_->session != XR_NULL_HANDLE)
    {
        xrDestroySession(impl_->session);
        impl_->session = XR_NULL_HANDLE;
    }
    impl_->context.Reset();
    impl_->device.Reset();
    impl_->adapter.Reset();
    if (impl_->instance != XR_NULL_HANDLE)
    {
        xrDestroyInstance(impl_->instance);
        impl_->instance = XR_NULL_HANDLE;
    }

    const HostCallbacks host = impl_->host;
    *impl_ = Impl{};
    impl_->host = host;
}

bool OpenXrRuntime::PollEvents()
{
    if (impl_->instance == XR_NULL_HANDLE)
    {
        return false;
    }

    XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};
    for (;;)
    {
        const XrResult poll_result = xrPollEvent(impl_->instance, &event);
        if (poll_result == XR_EVENT_UNAVAILABLE)
        {
            break;
        }
        if (XR_FAILED(poll_result))
        {
            impl_->Log(LogLevel::Error, "xrPollEvent failed: %s",
                       impl_->ResultName(poll_result));
            impl_->exit_requested = true;
            return false;
        }

        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
        {
            const auto* changed =
                reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            if (changed->session == impl_->session)
            {
                impl_->session_state = changed->state;
                impl_->session_focused = changed->state == XR_SESSION_STATE_FOCUSED;

                if (changed->state == XR_SESSION_STATE_READY &&
                    !impl_->session_running)
                {
                    XrSessionBeginInfo begin_info = {XR_TYPE_SESSION_BEGIN_INFO};
                    begin_info.primaryViewConfigurationType =
                        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    if (impl_->Check(xrBeginSession(impl_->session, &begin_info),
                                     "xrBeginSession"))
                    {
                        impl_->session_running = true;
                    }
                }
                else if (changed->state == XR_SESSION_STATE_STOPPING &&
                         impl_->session_running)
                {
                    impl_->Check(xrEndSession(impl_->session), "xrEndSession");
                    impl_->session_running = false;
                }
                else if (changed->state == XR_SESSION_STATE_EXITING ||
                         changed->state == XR_SESSION_STATE_LOSS_PENDING)
                {
                    impl_->exit_requested = true;
                    impl_->session_running = false;
                }
            }
        }
        else if (event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)
        {
            const auto* changed =
                reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&event);
            if (changed->session == impl_->session)
            {
                const auto& pose = changed->poseInPreviousSpace;
                impl_->Log(LogLevel::Info,
                    "OpenXR reference-space change: type=%d selected=%d applies=%u time=%lld valid=%u previousP=(%.6f,%.6f,%.6f) previousQ=(%.7f,%.7f,%.7f,%.7f)",
                    static_cast<int>(changed->referenceSpaceType),
                    static_cast<int>(impl_->app_reference_type),
                    changed->referenceSpaceType == impl_->app_reference_type ? 1u : 0u,
                    static_cast<long long>(changed->changeTime), changed->poseValid,
                    pose.position.x, pose.position.y, pose.position.z,
                    pose.orientation.x, pose.orientation.y,
                    pose.orientation.z, pose.orientation.w);
            }
        }
        else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
        {
            impl_->exit_requested = true;
        }

        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
    return !impl_->exit_requested;
}

bool OpenXrRuntime::BeginFrame(FrameState* frame)
{
    if (frame == nullptr || !impl_->initialized || !impl_->frame_lifecycle.can_wait())
    {
        return false;
    }
    *frame = {};
    PollEvents();
    if (!impl_->session_running || impl_->exit_requested)
    {
        return false;
    }

    XrFrameWaitInfo wait_info = {XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState state = {XR_TYPE_FRAME_STATE};
    std::chrono::steady_clock::time_point wait_started{};
    if (impl_->frame_timing_diagnostics)
    {
        wait_started = std::chrono::steady_clock::now();
    }
    if (!impl_->Check(xrWaitFrame(impl_->session, &wait_info, &state),
                      "xrWaitFrame"))
    {
        return false;
    }
    std::chrono::steady_clock::time_point wait_finished{};
    if (impl_->frame_timing_diagnostics)
    {
        wait_finished = std::chrono::steady_clock::now();
    }
    if (!impl_->predicted_display_period_logged &&
        state.predictedDisplayPeriod > 0)
    {
        impl_->Log(
            LogLevel::Info,
            "OpenXR runtime display timing: predictedDisplayPeriod=%.3f ms (%.2f Hz)",
            duration_milliseconds(state.predictedDisplayPeriod),
            predicted_refresh_hz(state.predictedDisplayPeriod));
        impl_->predicted_display_period_logged = true;
    }
    impl_->frame_lifecycle.waited();
    if (impl_->frame_timing_diagnostics)
    {
        // Keep the application wall interval anchored at Wait completion in
        // both modes; delayed Begin must not make measured native work vanish.
        impl_->current_frame_begin_wall = wait_finished;
        impl_->RecordFrameWait(state, wait_finished - wait_started);
    }
    impl_->current_display_time = state.predictedDisplayTime;
    impl_->current_should_render = state.shouldRender == XR_TRUE;
    impl_->current_views_valid = false;

    frame->frame_id = ++impl_->frame_id;
    if (impl_->frame_timing_diagnostics)
    {
        impl_->current_detailed_timing = {};
        impl_->current_detailed_timing.frame_id = frame->frame_id;
        impl_->current_detailed_timing.display_time = state.predictedDisplayTime;
        impl_->current_detailed_timing.display_period = state.predictedDisplayPeriod;
        impl_->current_detailed_timing.should_render = state.shouldRender == XR_TRUE;
        RecordDiagnosticStage(frame->frame_id, FrameTimingStage::wait_frame,
                              wait_finished - wait_started);
    }
    if (!impl_->defer_graphics_begin && !BeginGraphicsFrame()) return false;
    frame->predicted_display_time = state.predictedDisplayTime;
    frame->predicted_display_period = state.predictedDisplayPeriod;
    frame->should_render = impl_->current_should_render;

    for (XrView& view : impl_->views)
    {
        view = {XR_TYPE_VIEW};
    }
    if (frame->should_render)
    {
        XrViewLocateInfo locate_info = {XR_TYPE_VIEW_LOCATE_INFO};
        locate_info.viewConfigurationType =
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate_info.displayTime = state.predictedDisplayTime;
        locate_info.space = impl_->app_space;
        XrViewState view_state = {XR_TYPE_VIEW_STATE};
        std::uint32_t view_count = 0;
        const XrResult result = xrLocateViews(
            impl_->session,
            &locate_info,
            &view_state,
            kEyeCount,
            &view_count,
            impl_->views.data());
        const XrViewStateFlags valid_flags =
            XR_VIEW_STATE_ORIENTATION_VALID_BIT |
            XR_VIEW_STATE_POSITION_VALID_BIT;
        impl_->current_views_valid =
            XR_SUCCEEDED(result) && view_count == kEyeCount &&
            (view_state.viewStateFlags & valid_flags) == valid_flags;
        if (XR_FAILED(result))
        {
            impl_->Log(LogLevel::Warning, "xrLocateViews failed: %s",
                       impl_->ResultName(result));
        }
    }

    frame->views_valid = impl_->current_views_valid;
    if (frame->views_valid)
    {
        for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
        {
            frame->eyes[eye].pose = FromXrPose(impl_->views[eye].pose);
            frame->eyes[eye].fov = FromXrFov(impl_->views[eye].fov);
        }
        frame->head_center.orientation = frame->eyes[0].pose.orientation;
        frame->head_center.position = {
            (frame->eyes[0].pose.position.x + frame->eyes[1].pose.position.x) *
                0.5f,
            (frame->eyes[0].pose.position.y + frame->eyes[1].pose.position.y) *
                0.5f,
            (frame->eyes[0].pose.position.z + frame->eyes[1].pose.position.z) *
                0.5f,
        };
    }
    impl_->ReadActions(&frame->actions);
    impl_->RecordOrientationDiagnostics(*frame);
    return true;
}

bool OpenXrRuntime::BeginGraphicsFrame()
{
    if (impl_->frame_lifecycle.begun()) return true;
    if (!impl_->initialized || !impl_->frame_lifecycle.needs_begin() ||
        !impl_->session_running || impl_->exit_requested ||
        impl_->session == XR_NULL_HANDLE ||
        impl_->session_state == XR_SESSION_STATE_STOPPING ||
        impl_->session_state == XR_SESSION_STATE_EXITING ||
        impl_->session_state == XR_SESSION_STATE_LOSS_PENDING)
    {
        return false;
    }
    XrFrameBeginInfo begin_info = {XR_TYPE_FRAME_BEGIN_INFO};
    std::chrono::steady_clock::time_point begin_started{};
    if (impl_->frame_timing_diagnostics)
        begin_started = std::chrono::steady_clock::now();
    const XrResult result = xrBeginFrame(impl_->session, &begin_info);
    const bool began = impl_->frame_lifecycle.begin_completed(XR_SUCCEEDED(result));
    if (impl_->frame_timing_diagnostics && began)
        RecordDiagnosticStage(impl_->frame_id, FrameTimingStage::begin_frame,
                              std::chrono::steady_clock::now() - begin_started);
    if (!impl_->Check(result, "xrBeginFrame"))
    {
        // Never enter a second Wait after an unmatched successful Wait. The
        // caller must recreate the session; teardown must not End this frame.
        impl_->exit_requested = true;
        return false;
    }
    return began;
}

bool OpenXrRuntime::AcquireEyeImage(
    const std::uint32_t eye,
    EyeRenderTarget* target)
{
    if (target == nullptr || !impl_->frame_lifecycle.begun() || eye >= kEyeCount)
    {
        return false;
    }
    *target = {};
    Impl::Swapchain& swapchain = impl_->swapchains[eye];
    if (swapchain.acquired && swapchain.waited)
    {
        return false;
    }
    if (!swapchain.acquired)
    {
        XrSwapchainImageAcquireInfo acquire_info = {
            XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrResult acquire_result;
        {
            ScopedFrameDiagnosticStage timing(*this, eye == 0 ?
                FrameTimingStage::acquire_left : FrameTimingStage::acquire_right);
            acquire_result = xrAcquireSwapchainImage(
                swapchain.handle, &acquire_info, &swapchain.acquired_index);
        }
        if (!impl_->Check(acquire_result, "xrAcquireSwapchainImage"))
        {
            return false;
        }
        swapchain.acquired = true;
    }

    XrSwapchainImageWaitInfo wait_info = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait_info.timeout = kSwapchainImageWaitTimeout;
    XrResult wait_result;
    {
        ScopedFrameDiagnosticStage timing(*this, eye == 0 ?
            FrameTimingStage::wait_left : FrameTimingStage::wait_right);
        wait_result = xrWaitSwapchainImage(swapchain.handle, &wait_info);
    }
    if (wait_result == XR_TIMEOUT_EXPIRED)
    {
        impl_->Log(
            LogLevel::Error,
            "xrWaitSwapchainImage exceeded 100 ms; recreating XR instead of blocking the game thread");
        impl_->exit_requested = true;
        return false;
    }
    if (!impl_->Check(wait_result, "xrWaitSwapchainImage"))
    {
        // The image is still acquired: OpenXR forbids releasing it until a
        // wait succeeds. Mark the runtime fatal and leave teardown to destroy
        // the owning swapchain/session without issuing an invalid release.
        impl_->exit_requested = true;
        return false;
    }
    swapchain.waited = true;

    target->render_target =
        swapchain.render_targets[swapchain.acquired_index].Get();
    target->width = swapchain.width;
    target->height = swapchain.height;
    target->image_index = swapchain.acquired_index;
    target->target_is_srgb = IsSrgbFormat(impl_->swapchain_format);
    return true;
}

bool OpenXrRuntime::ReleaseEyeImage(const std::uint32_t eye)
{
    if (eye >= kEyeCount)
    {
        return false;
    }
    Impl::Swapchain& swapchain = impl_->swapchains[eye];
    if (!swapchain.acquired || !swapchain.waited)
    {
        return false;
    }
    XrSwapchainImageReleaseInfo release_info = {
        XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    XrResult release_result;
    {
        ScopedFrameDiagnosticStage timing(*this, eye == 0 ?
            FrameTimingStage::release_left : FrameTimingStage::release_right);
        release_result = xrReleaseSwapchainImage(swapchain.handle, &release_info);
    }
    if (!impl_->Check(release_result, "xrReleaseSwapchainImage"))
    {
        impl_->exit_requested = true;
        return false;
    }
    swapchain.acquired = false;
    swapchain.waited = false;
    return true;
}

EndFrameResult OpenXrRuntime::EndFrame(
    const FrameState& frame,
    const CompositionLayerSubmission& submission)
{
    if (!impl_->frame_lifecycle.active() || frame.frame_id != impl_->frame_id ||
        !BeginGraphicsFrame())
    {
        return {};
    }

    // shouldRender=false is a normal runtime pacing/focus state. xrEndFrame
    // must still be called, but with zero layers; it is not malformed quad or
    // projection metadata and must never produce a per-frame error storm.
    CompositionLayerKind requested_kind = layer_kind_for_runtime_frame(
        impl_->current_should_render, submission.kind);
    bool submission_rejected = false;

    for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
    {
        if (impl_->swapchains[eye].acquired &&
            impl_->swapchains[eye].waited)
        {
            if (!ReleaseEyeImage(eye))
            {
                impl_->exit_requested = true;
            }
            submission_rejected = submission_rejected ||
                requested_kind != CompositionLayerKind::none;
            requested_kind = CompositionLayerKind::none;
        }
        else if (impl_->swapchains[eye].acquired)
        {
            submission_rejected = submission_rejected ||
                requested_kind != CompositionLayerKind::none;
            requested_kind = CompositionLayerKind::none;
        }
    }

    std::array<XrCompositionLayerProjectionView, kEyeCount> projection_views = {};
    XrCompositionLayerProjection projection = {
        XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerQuad quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    std::array<const XrCompositionLayerBaseHeader*, 1> layers = {};
    std::uint32_t layer_count = 0;
    CompositionLayerKind prepared_kind = CompositionLayerKind::none;

    if (requested_kind == CompositionLayerKind::quad)
    {
        if (impl_->current_should_render && submission.quad != nullptr &&
            IsValidQuadLayer(*submission.quad))
        {
            const Impl::Swapchain& source_swapchain =
                impl_->swapchains[submission.quad->source_eye];
            quad.layerFlags = 0;
            quad.space = impl_->app_space;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = source_swapchain.handle;
            quad.subImage.imageRect.offset = {0, 0};
            quad.subImage.imageRect.extent = {
                static_cast<std::int32_t>(source_swapchain.width),
                static_cast<std::int32_t>(source_swapchain.height),
            };
            quad.subImage.imageArrayIndex = 0;
            quad.pose = ToXrPose(submission.quad->pose);
            quad.size = {
                submission.quad->size_meters.x,
                submission.quad->size_meters.y,
            };
            layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
                &quad);
            layer_count = 1;
            prepared_kind = CompositionLayerKind::quad;
        }
        else
        {
            // Do not silently reinterpret invalid finite-panel metadata as a
            // zero-depth projection; that recreates the exact head-following
            // failure this layer mode is intended to prevent.
            impl_->invalid_quad_occurrences += 1;
            if (should_log_repeated_runtime_issue(
                    impl_->invalid_quad_occurrences))
            {
                impl_->Log(
                    LogLevel::Error,
                    "Rejected invalid OpenXR quad-layer metadata (occurrence %llu; repeated reports are throttled)",
                    static_cast<unsigned long long>(
                        impl_->invalid_quad_occurrences));
            }
            submission_rejected = true;
        }
    }
    else if (requested_kind == CompositionLayerKind::projection)
    {
        if (impl_->current_should_render && impl_->current_views_valid)
        {
            const EyeView* rendered_eyes = frame.eyes;
            if (submission.projection_source != nullptr &&
                submission.projection_source->rendered_views_valid)
            {
                rendered_eyes = submission.projection_source->rendered_eyes;
            }
            for (std::uint32_t eye = 0; eye < kEyeCount; ++eye)
            {
                projection_views[eye] = {
                    XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
                projection_views[eye].pose = ToXrPose(rendered_eyes[eye].pose);
                projection_views[eye].fov = ToXrFov(rendered_eyes[eye].fov);
                projection_views[eye].subImage.swapchain =
                    impl_->swapchains[eye].handle;
                projection_views[eye].subImage.imageRect.offset = {0, 0};
                projection_views[eye].subImage.imageRect.extent = {
                    static_cast<std::int32_t>(impl_->swapchains[eye].width),
                    static_cast<std::int32_t>(impl_->swapchains[eye].height),
                };
                projection_views[eye].subImage.imageArrayIndex = 0;
            }
            projection.space = impl_->app_space;
            projection.viewCount = kEyeCount;
            projection.views = projection_views.data();
            layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
                &projection);
            layer_count = 1;
            prepared_kind = CompositionLayerKind::projection;
        }
        else
        {
            impl_->invalid_projection_occurrences += 1;
            if (should_log_repeated_runtime_issue(
                    impl_->invalid_projection_occurrences))
            {
                impl_->Log(
                    LogLevel::Error,
                    "Rejected OpenXR projection request without valid current views (occurrence %llu; repeated reports are throttled)",
                    static_cast<unsigned long long>(
                        impl_->invalid_projection_occurrences));
            }
            submission_rejected = true;
        }
    }

    XrFrameEndInfo end_info = {XR_TYPE_FRAME_END_INFO};
    end_info.displayTime = impl_->current_display_time;
    end_info.environmentBlendMode = impl_->blend_mode;
    end_info.layerCount = layer_count;
    end_info.layers = layer_count != 0 ? layers.data() : nullptr;
    std::chrono::steady_clock::time_point end_frame_started{};
    if (impl_->frame_timing_diagnostics)
    {
        end_frame_started = std::chrono::steady_clock::now();
    }
    const XrResult result = xrEndFrame(impl_->session, &end_info);
    if (impl_->frame_timing_diagnostics)
    {
        const auto end_frame_finished = std::chrono::steady_clock::now();
        impl_->RecordFrameEnd(
            end_frame_started - impl_->current_frame_begin_wall,
            end_frame_finished - end_frame_started, prepared_kind,
            XR_SUCCEEDED(result), end_frame_finished);
    }
    impl_->frame_lifecycle.end_completed(XR_SUCCEEDED(result));
    const bool frame_ended = impl_->Check(result, "xrEndFrame");
    if (!frame_ended)
    {
        // Swapchain images may already have been replaced and released. Force
        // session recreation rather than allowing old layer metadata to be
        // paired with those new images on the following frame.
        impl_->exit_requested = true;
    }
    return {
        .frame_ended = frame_ended,
        .accepted_kind = frame_ended && !submission_rejected
            ? prepared_kind
            : CompositionLayerKind::none,
    };
}

bool OpenXrRuntime::ApplyHaptic(
    const Hand hand,
    const float amplitude,
    const float duration_seconds,
    const float frequency_hz)
{
    if (!impl_->session_running || !impl_->session_focused)
    {
        return false;
    }
    const std::uint32_t hand_index = static_cast<std::uint32_t>(hand);
    if (hand_index >= kHandCount)
    {
        return false;
    }

    XrHapticActionInfo info = {XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = impl_->actions.haptic;
    info.subactionPath = impl_->actions.hands[hand_index];
    XrHapticVibration vibration = {XR_TYPE_HAPTIC_VIBRATION};
    vibration.amplitude = std::clamp(amplitude, 0.0f, 1.0f);
    vibration.duration = duration_seconds > 0.0f
        ? static_cast<XrDuration>(duration_seconds * 1000000000.0)
        : XR_MIN_HAPTIC_DURATION;
    vibration.frequency = frequency_hz > 0.0f
        ? frequency_hz
        : XR_FREQUENCY_UNSPECIFIED;
    return impl_->Check(
        xrApplyHapticFeedback(
            impl_->session,
            &info,
            reinterpret_cast<const XrHapticBaseHeader*>(&vibration)),
        "xrApplyHapticFeedback");
}

bool OpenXrRuntime::StopHaptic(const Hand hand)
{
    if (!impl_->session_running || !impl_->session_focused)
    {
        return false;
    }
    const std::uint32_t hand_index = static_cast<std::uint32_t>(hand);
    if (hand_index >= kHandCount)
    {
        return false;
    }
    XrHapticActionInfo info = {XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = impl_->actions.haptic;
    info.subactionPath = impl_->actions.hands[hand_index];
    return impl_->Check(
        xrStopHapticFeedback(impl_->session, &info),
        "xrStopHapticFeedback");
}

bool OpenXrRuntime::RequestExit()
{
    return impl_->session != XR_NULL_HANDLE &&
           impl_->Check(xrRequestExitSession(impl_->session),
                        "xrRequestExitSession");
}

bool OpenXrRuntime::initialized() const { return impl_->initialized; }
bool OpenXrRuntime::session_running() const { return impl_->session_running; }
bool OpenXrRuntime::session_focused() const { return impl_->session_focused; }
bool OpenXrRuntime::exit_requested() const { return impl_->exit_requested; }
InitializationFailure OpenXrRuntime::last_initialization_failure() const
{
    return impl_->last_initialization_failure;
}
const char* OpenXrRuntime::active_runtime_name() const
{
    return impl_->active_runtime_name.empty()
        ? "<runtime unavailable>"
        : impl_->active_runtime_name.c_str();
}
std::uint64_t OpenXrRuntime::active_runtime_version() const
{
    return impl_->active_runtime_version;
}
ID3D11Device* OpenXrRuntime::d3d11_device() const { return impl_->device.Get(); }
ID3D11DeviceContext* OpenXrRuntime::d3d11_context() const
{
    return impl_->context.Get();
}

bool OpenXrRuntime::timing_diagnostics_enabled() const
{
    return impl_->frame_timing_diagnostics;
}

std::uint64_t OpenXrRuntime::timing_frame_id() const
{
    return impl_->frame_timing_diagnostics && impl_->frame_lifecycle.active() ? impl_->frame_id : 0;
}

void OpenXrRuntime::RecordDiagnosticStage(
    const std::uint64_t frame_id, const FrameTimingStage stage,
    const std::chrono::steady_clock::duration elapsed)
{
    if (!impl_->frame_timing_diagnostics || !impl_->frame_lifecycle.active() ||
        elapsed < std::chrono::steady_clock::duration::zero()) return;
    impl_->current_detailed_timing.add(frame_id, stage,
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()));
}

void OpenXrRuntime::RecordDiagnosticSource(
    const std::uint64_t frame_id, const std::uint64_t source_frame_id,
    const std::uint64_t source_serial)
{
    if (!impl_->frame_timing_diagnostics || !impl_->frame_lifecycle.active() || frame_id == 0 ||
        impl_->current_detailed_timing.frame_id != frame_id) return;
    impl_->current_detailed_timing.source_frame_id = source_frame_id;
    impl_->current_detailed_timing.source_serial = source_serial;
}

void OpenXrRuntime::RecordDiagnosticPresentation(
    const std::uint64_t frame_id, const bool observed_stereo, const bool reused_layer)
{
    if (!impl_->frame_timing_diagnostics || !impl_->frame_lifecycle.active() || frame_id == 0 ||
        impl_->current_detailed_timing.frame_id != frame_id) return;
    impl_->current_detailed_timing.presentation_valid = true;
    impl_->current_detailed_timing.observed_stereo = observed_stereo;
    impl_->current_detailed_timing.reused_layer = reused_layer;
}

ScopedFrameDiagnosticStage::ScopedFrameDiagnosticStage(
    OpenXrRuntime& runtime, const FrameTimingStage stage)
{
    if (!runtime.timing_diagnostics_enabled()) return;
    frame_id_ = runtime.timing_frame_id();
    if (frame_id_ == 0) return;
    runtime_ = &runtime;
    stage_ = stage;
    started_ = std::chrono::steady_clock::now();
}

ScopedFrameDiagnosticStage::~ScopedFrameDiagnosticStage()
{
    if (runtime_ != nullptr)
        runtime_->RecordDiagnosticStage(frame_id_, stage_,
            std::chrono::steady_clock::now() - started_);
}

} // namespace wawvr::xr
