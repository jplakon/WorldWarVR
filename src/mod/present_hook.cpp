#include "present_hook.hpp"
#include "virtual_query_timing.hpp"
#include "resident_page_access.hpp"

#include "controller_state.hpp"
#include "d3d9ex_bootstrap.hpp"
#include "menu_button_gesture.hpp"
#include "menu_panel_logic.hpp"
#include "menu_surface_logic.hpp"
#include "manual_reload_runtime.hpp"
#include "performance_timing.hpp"
#include "present_hook_logic.hpp"
#include "smoke_surface_snapshot.hpp"
#include "stereo_diagnostics.hpp"
#include "stereo_frame_broker.hpp"
#include "stereo_scene_hook.hpp"
#include "t4_layout_selector.hpp"
#include "tracking_anchor_sync.hpp"
#if defined(WAWVR_HAS_T4_BINDINGS)
#include "firing_haptics.hpp"
#include "t4_menu_input.hpp"
#include "t4_presentation_state.hpp"
#include "weapon_hook.hpp"
#endif

#include "d3d11_compositor.h"
#include "d3d9_cpu_capture.h"
#include "d3d9ex_bridge.h"
#include "openxr_runtime.h"

#include <d3d9.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cwchar>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <string_view>

namespace wawvr::mod {
namespace {

enum class PreparedCaptureKind : std::uint8_t {
    none,
    shared_gpu,
    cpu,
};

[[nodiscard]] const char* prepared_capture_kind_name(
    const PreparedCaptureKind kind) noexcept {
    switch (kind) {
    case PreparedCaptureKind::shared_gpu: return "shared-gpu";
    case PreparedCaptureKind::cpu: return "cpu";
    case PreparedCaptureKind::none:
    default: return "none";
    }
}

struct CaptureSubmissionDiagnosticWindow final {
    std::uint32_t samples{};
    double frame_lag_sum{};
    std::int64_t frame_lag_min{(std::numeric_limits<std::int64_t>::max)()};
    std::int64_t frame_lag_max{(std::numeric_limits<std::int64_t>::min)()};
    double predicted_lag_milliseconds_sum{};
    double predicted_lag_milliseconds_min{
        (std::numeric_limits<double>::max)()};
    double predicted_lag_milliseconds_max{
        (std::numeric_limits<double>::lowest)()};
    double shared_queue_depth_sum{};
    std::size_t shared_queue_depth_max{};
    std::uint32_t same_frame{};
    std::uint32_t source_older{};
    std::uint32_t source_newer{};
    std::uint32_t receipt_matching{};
    std::uint32_t weapon_live{};
    std::uint32_t weapon_retained{};
    std::uint32_t weapon_missing{};
    std::uint32_t weapon_mismatched{};
};

class SharedFrameReleaseGuard final {
public:
    SharedFrameReleaseGuard() = default;
    SharedFrameReleaseGuard(
        wawvr::xr::D3D9ExSharedTextureBridge* const bridge,
        ID3D11DeviceContext* const context,
        const wawvr::xr::SharedFrameToken& token) noexcept
        : bridge_(bridge), context_(context), token_(token), active_(true) {}

    SharedFrameReleaseGuard(const SharedFrameReleaseGuard&) = delete;
    SharedFrameReleaseGuard& operator=(const SharedFrameReleaseGuard&) = delete;

    ~SharedFrameReleaseGuard() { Release(); }

    void Arm(
        wawvr::xr::D3D9ExSharedTextureBridge* const bridge,
        ID3D11DeviceContext* const context,
        const wawvr::xr::SharedFrameToken& token) noexcept {
        Release();
        bridge_ = bridge;
        context_ = context;
        token_ = token;
        active_ = true;
    }

    void Release() noexcept {
        if (!active_ || bridge_ == nullptr || context_ == nullptr) {
            return;
        }
        bridge_->Release(context_, token_);
        active_ = false;
    }

private:
    wawvr::xr::D3D9ExSharedTextureBridge* bridge_{};
    ID3D11DeviceContext* context_{};
    wawvr::xr::SharedFrameToken token_{};
    bool active_{};
};

class AtomicBoolClearGuard final {
public:
    explicit AtomicBoolClearGuard(std::atomic<bool>* const value) noexcept
        : value_(value) {}
    AtomicBoolClearGuard(const AtomicBoolClearGuard&) = delete;
    AtomicBoolClearGuard& operator=(const AtomicBoolClearGuard&) = delete;
    ~AtomicBoolClearGuard() {
        if (value_ != nullptr) {
            value_->store(false, std::memory_order_release);
        }
    }

private:
    std::atomic<bool>* value_{};
};

// Clean-room facts for the exact, non-ASLR SP/MP executables.
// run_present_hook_monitor is entered only after the complete file and mapped
// image profile has passed src/t4 validation and selected one of these layouts.
struct PresentExecutableProfile final {
    const char* name{};
    std::uintptr_t d3d9_device_pointer_address{};
    std::uintptr_t target_window_index_address{};
    std::uintptr_t window_count_address{};
    std::uintptr_t window_zero_swap_chain_address{};
    std::uintptr_t rb_swap_buffers_address{};
    std::array<std::uint8_t, 40> rb_swap_buffers_sentinel{};
};

constexpr PresentExecutableProfile kSpPresentProfile{
    .name = "T4 SP 1.7.1263",
    .d3d9_device_pointer_address = 0x03BF3B08u,
    .target_window_index_address = 0x03BF6774u,
    .window_count_address = 0x03BF6778u,
    .window_zero_swap_chain_address = 0x03BF6780u,
    .rb_swap_buffers_address = 0x006FBE50u,
    .rb_swap_buffers_sentinel = {
        0x51, 0xA1, 0x74, 0x67, 0xBF, 0x03, 0x56, 0x57,
        0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00, 0xC1, 0xE0,
        0x04, 0x8B, 0x80, 0x80, 0x67, 0xBF, 0x03, 0x8B,
        0x08, 0x8B, 0x51, 0x0C, 0x6A, 0x00, 0x6A, 0x00,
        0x50, 0xFF, 0xD2, 0x3D, 0x68, 0x08, 0x76, 0x88,
    },
};

constexpr PresentExecutableProfile kMpPresentProfile{
    .name = "T4 MP 1.7.1263",
    .d3d9_device_pointer_address = 0x1087DD08u,
    .target_window_index_address = 0x10880974u,
    .window_count_address = 0x10880978u,
    .window_zero_swap_chain_address = 0x10880980u,
    .rb_swap_buffers_address = 0x006D6F90u,
    .rb_swap_buffers_sentinel = {
        0x51, 0xA1, 0x74, 0x09, 0x88, 0x10, 0x56, 0x57,
        0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00, 0xC1, 0xE0,
        0x04, 0x8B, 0x80, 0x80, 0x09, 0x88, 0x10, 0x8B,
        0x08, 0x8B, 0x51, 0x0C, 0x6A, 0x00, 0x6A, 0x00,
        0x50, 0xFF, 0xD2, 0x3D, 0x68, 0x08, 0x76, 0x88,
    },
};

constexpr int kRendererLayoutUnconfigured = -1;
constexpr int kRendererLayoutSinglePlayer = 0;
constexpr int kRendererLayoutMultiplayer = 1;
std::atomic<int> g_renderer_layout{kRendererLayoutUnconfigured};

[[nodiscard]] constexpr int renderer_layout_code(
    const wawvr::t4::ExecutableLayoutId layout) noexcept {
    switch (t4_layout_family_from_id(layout)) {
    case T4LayoutFamily::single_player_1_7_1263:
        return kRendererLayoutSinglePlayer;
    case T4LayoutFamily::multiplayer_1_7_1263:
        return kRendererLayoutMultiplayer;
    case T4LayoutFamily::unsupported:
        return kRendererLayoutUnconfigured;
    }
    return kRendererLayoutUnconfigured;
}

[[nodiscard]] const PresentExecutableProfile*
configured_present_profile() noexcept {
    wawvr::t4::ExecutableLayoutId layout{};
    if (!get_configured_renderer_layout(&layout)) {
        return nullptr;
    }
    switch (t4_layout_family_from_id(layout)) {
    case T4LayoutFamily::single_player_1_7_1263:
        return &kSpPresentProfile;
    case T4LayoutFamily::multiplayer_1_7_1263:
        return &kMpPresentProfile;
    case T4LayoutFamily::unsupported:
        return nullptr;
    }
    return nullptr;
}

constexpr std::size_t kT4WindowStride = 0x10u;
constexpr std::uint32_t kMaximumSaneWindowCount = 8;
constexpr DWORD kTargetPollMilliseconds = 50;
constexpr ULONGLONG kInstallRetryMilliseconds = 2'000;
constexpr ULONGLONG kRuntimeRetryMilliseconds = 10'000;
constexpr ULONGLONG kRecoveryTransitionLogMilliseconds = 5'000;
constexpr ULONGLONG kResetTeardownWaitMilliseconds = 5'000;

[[nodiscard]] constexpr bool d3d9ex_result_requires_recovery(
    const HRESULT result) noexcept {
    return result == D3DERR_DEVICELOST ||
           result == D3DERR_DEVICENOTRESET ||
           result == D3DERR_DEVICEHUNG ||
           result == D3DERR_DEVICEREMOVED ||
           result == S_PRESENT_MODE_CHANGED;
}

std::atomic<bool> g_headset_unavailable_notice_shown{false};

DWORD WINAPI show_headset_unavailable_notice(void*) noexcept {
    MessageBoxW(
        nullptr,
        L"World War VR could not find a PC VR headset through the active "
        L"OpenXR runtime.\n\n"
        L"For Meta Quest 2/3, wake the headset, connect with Quest Link or "
        L"Air Link, enter the PC VR environment, and make Meta Quest Link "
        L"the active OpenXR runtime. If you use SteamVR or Virtual Desktop, "
        L"start SteamVR and make it the active OpenXR runtime.\n\n"
        L"The game will keep checking every 10 seconds, so VR can recover "
        L"after the headset becomes available. Otherwise, close the game, "
        L"correct the PC VR connection, and launch again.\n\n"
        L"If this continues, send WorldWarVR.log to the mod author.",
        L"World War VR - Headset not available",
        MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
    return 0;
}

void notify_headset_unavailable_once() noexcept {
    bool expected = false;
    if (!g_headset_unavailable_notice_shown.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return;
    }
    const HANDLE thread = CreateThread(
        nullptr, 0, &show_headset_unavailable_notice, nullptr, 0, nullptr);
    if (thread != nullptr) {
        CloseHandle(thread);
    }
}

[[nodiscard]] ActiveUiMonoSource configured_active_ui_source() noexcept {
    std::array<wchar_t, 16> value{};
    const DWORD length = GetEnvironmentVariableW(
        L"WAWVR_MENU_SOURCE", value.data(),
        static_cast<DWORD>(value.size()));
    if (length != 0 && length < value.size()) {
        if (_wcsicmp(value.data(), L"left") == 0) {
            return ActiveUiMonoSource::left_eye;
        }
        if (_wcsicmp(value.data(), L"right") == 0) {
            return ActiveUiMonoSource::right_eye;
        }
        if (_wcsicmp(value.data(), L"full") == 0) {
            return ActiveUiMonoSource::full_frame;
        }
    }
    // Exact T4 CL_CGameRendering queues its stock UI after both packed scene
    // eyes without a per-eye scrPlace remap. Preserve that complete
    // backbuffer by default. Explicit left/right values remain probe aids.
    return ActiveUiMonoSource::full_frame;
}

[[nodiscard]] const char* active_ui_source_name(
    const ActiveUiMonoSource source) noexcept {
    switch (source) {
    case ActiveUiMonoSource::left_eye: return "left";
    case ActiveUiMonoSource::right_eye: return "right";
    case ActiveUiMonoSource::full_frame: return "full";
    }
    return "unknown";
}

[[nodiscard]] const char* presentation_mode_name(
    const PresentationMode mode) noexcept {
    switch (mode) {
    case PresentationMode::stereo: return "gameplay-stereo";
    case PresentationMode::full_frame_mono: return "full-frame-mono";
    case PresentationMode::active_ui_mono: return "active-ui-mono";
    case PresentationMode::active_console_mono: return "active-console-mono";
    }
    return "unknown";
}

[[nodiscard]] const char* composition_layer_kind_name(
    const wawvr::xr::CompositionLayerKind kind) noexcept {
    switch (kind) {
    case wawvr::xr::CompositionLayerKind::none: return "none";
    case wawvr::xr::CompositionLayerKind::projection: return "projection";
    case wawvr::xr::CompositionLayerKind::quad: return "quad";
    }
    return "unknown";
}

using ResetFunction = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using SwapChainPresentFunction = HRESULT(STDMETHODCALLTYPE*)(
    IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);

constexpr std::size_t kMaximumProcessLifetimeD3DThunkRoutes = 32;
ImmutableThunkRouteTable<
    IDirect3DSwapChain9*, SwapChainPresentFunction,
    kMaximumProcessLifetimeD3DThunkRoutes>
    g_swap_present_thunk_routes;
ImmutableThunkRouteTable<
    void**, SwapChainPresentFunction,
    kMaximumProcessLifetimeD3DThunkRoutes>
    g_swap_present_vtable_routes;
ImmutableThunkRouteTable<
    IDirect3DDevice9*, ResetFunction,
    kMaximumProcessLifetimeD3DThunkRoutes>
    g_reset_thunk_routes;
ImmutableThunkRouteTable<
    void**, ResetFunction,
    kMaximumProcessLifetimeD3DThunkRoutes>
    g_reset_vtable_routes;

[[nodiscard]] bool route_registration_usable(
    const ImmutableThunkRouteRegistration result) noexcept {
    return result == ImmutableThunkRouteRegistration::inserted ||
           result == ImmutableThunkRouteRegistration::already_registered;
}

[[nodiscard]] const char* route_registration_name(
    const ImmutableThunkRouteRegistration result) noexcept {
    switch (result) {
    case ImmutableThunkRouteRegistration::inserted:
        return "inserted";
    case ImmutableThunkRouteRegistration::already_registered:
        return "already registered";
    case ImmutableThunkRouteRegistration::conflicting_callback:
        return "conflicting callback";
    case ImmutableThunkRouteRegistration::capacity_exhausted:
        return "capacity exhausted";
    case ImmutableThunkRouteRegistration::invalid:
        return "invalid";
    }
    return "unknown";
}

std::atomic<bool> g_shutdown_requested{false};
std::atomic<bool> g_present_hook_installed{false};
std::atomic<bool> g_mono_xr_ready{false};
std::atomic<HMODULE> g_log_module{nullptr};
std::mutex g_log_mutex;
std::ofstream g_log_stream;
std::filesystem::path g_log_stream_path;
std::chrono::steady_clock::time_point g_log_last_flush{};

const char* level_name(const wawvr::xr::LogLevel level) noexcept {
    switch (level) {
    case wawvr::xr::LogLevel::Debug:
        return "debug";
    case wawvr::xr::LogLevel::Info:
        return "info";
    case wawvr::xr::LogLevel::Warning:
        return "warning";
    case wawvr::xr::LogLevel::Error:
        return "error";
    }
    return "unknown";
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &value);
    std::ostringstream stream;
    stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

std::filesystem::path module_log_path(const HMODULE module) {
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(
        module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return std::filesystem::current_path() / "WorldWarVR.log";
    }
    return std::filesystem::path(path.data(), path.data() + length)
               .parent_path() /
           "WorldWarVR.log";
}

void append_log(
    const HMODULE module,
    const wawvr::xr::LogLevel level,
    const std::string_view message) noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        const std::filesystem::path path = module_log_path(module);
        if (!g_log_stream.is_open() || g_log_stream_path != path) {
            if (g_log_stream.is_open()) {
                g_log_stream.flush();
                g_log_stream.close();
            }
            g_log_stream.clear();
            g_log_stream.open(path, std::ios::app);
            g_log_stream_path = path;
            g_log_last_flush = {};
        }
        if (g_log_stream) {
            g_log_stream << '[' << timestamp() << "] [" << level_name(level)
                         << "] " << message << '\n';
            const auto now = std::chrono::steady_clock::now();
            // Warnings are diagnostic telemetry, and several render-time
            // transitions can legitimately emit a short burst of them. A
            // synchronous filesystem flush for every warning stalls the game
            // thread precisely when a controller handoff is being rendered.
            // Preserve immediate durability for actual errors; the ordinary
            // one-second cadence is sufficient for warnings and information.
            const bool urgent = level == wawvr::xr::LogLevel::Error;
            if (urgent || g_log_last_flush.time_since_epoch().count() == 0 ||
                now - g_log_last_flush >= std::chrono::seconds(1)) {
                g_log_stream.flush();
                g_log_last_flush = now;
            }
        }
        std::string debugger_line = "WorldAtWarVR [";
        debugger_line += level_name(level);
        debugger_line += "]: ";
        debugger_line.append(message.data(), message.size());
        debugger_line += '\n';
        OutputDebugStringA(debugger_line.c_str());
    } catch (...) {
        OutputDebugStringA("WorldAtWarVR: renderer logging failed\n");
    }
}

bool protection_is_executable(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffu;
    return access == PAGE_EXECUTE || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

bool readable_range(const void* const address, const std::size_t size) noexcept {
    return resident_page_access(address, size, false,
        &grouped_virtual_query<VirtualQueryTimingGroup::presentation>);
}

bool executable_pointer(const void* const address) noexcept {
    MEMORY_BASIC_INFORMATION memory{};
    return address != nullptr &&
           grouped_virtual_query<VirtualQueryTimingGroup::presentation>(
               address, &memory, sizeof(memory)) == sizeof(memory) &&
           memory.State == MEM_COMMIT &&
           (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
           protection_is_executable(memory.Protect);
}

template <typename T>
bool read_absolute(const std::uintptr_t address, T* const value) noexcept {
    const auto* const source = reinterpret_cast<const T*>(address);
    if (value == nullptr || !readable_range(source, sizeof(T))) {
        return false;
    }
    *value = *source;
    return true;
}

void* read_vtable_entry(
    void* const object,
    const std::size_t index,
    void*** const table_output = nullptr) noexcept {
    if (!readable_range(object, sizeof(void*))) {
        return nullptr;
    }
    auto** const table = *reinterpret_cast<void***>(object);
    if (!readable_range(table + index, sizeof(void*))) {
        return nullptr;
    }
    if (table_output != nullptr) {
        *table_output = table;
    }
    return table[index];
}

[[nodiscard]] SwapChainPresentFunction lookup_swap_present_route(
    IDirect3DSwapChain9* const swap_chain) noexcept {
    void** table = nullptr;
    (void)read_vtable_entry(
        swap_chain, kDirect3DSwapChain9PresentIndex, &table);
    return lookup_immutable_thunk_route(
        g_swap_present_thunk_routes, swap_chain,
        g_swap_present_vtable_routes, table);
}

[[nodiscard]] ResetFunction lookup_reset_route(
    IDirect3DDevice9* const device) noexcept {
    void** table = nullptr;
    (void)read_vtable_entry(device, kDirect3DDevice9ResetIndex, &table);
    return lookup_immutable_thunk_route(
        g_reset_thunk_routes, device, g_reset_vtable_routes, table);
}

bool verify_rb_swap_buffers_sentinel(
    const PresentExecutableProfile& profile) noexcept {
    const auto* const code = reinterpret_cast<const std::uint8_t*>(
        profile.rb_swap_buffers_address);
    return readable_range(code, profile.rb_swap_buffers_sentinel.size()) &&
           std::memcmp(
               code, profile.rb_swap_buffers_sentinel.data(),
               profile.rb_swap_buffers_sentinel.size()) == 0;
}

struct T4PresentTarget final {
    IDirect3DDevice9* device{};
    IDirect3DSwapChain9* swap_chain{};
    std::int32_t target_window_index{};
    std::uint32_t window_count{};
};

bool read_t4_present_target(
    const PresentExecutableProfile& profile,
    T4PresentTarget* const target) noexcept {
    if (target == nullptr) {
        return false;
    }
    *target = {};
    if (!read_absolute(
            profile.d3d9_device_pointer_address, &target->device) ||
        !read_absolute(
            profile.target_window_index_address,
            &target->target_window_index) ||
        !read_absolute(profile.window_count_address, &target->window_count) ||
        target->device == nullptr || target->target_window_index < 0 ||
        target->window_count == 0 ||
        target->window_count > kMaximumSaneWindowCount ||
        static_cast<std::uint32_t>(target->target_window_index) >=
            target->window_count) {
        return false;
    }

    // D3D9CpuCapture currently reads implicit swapchain zero through the
    // device. Fail closed if T4 ever selects a different output window.
    if (target->target_window_index != 0) {
        return false;
    }
    const auto swap_chain_slot =
        profile.window_zero_swap_chain_address +
        static_cast<std::uintptr_t>(target->target_window_index) *
            kT4WindowStride;
    return read_absolute(swap_chain_slot, &target->swap_chain) &&
           target->swap_chain != nullptr;
}

bool environment_disables_present_hook() noexcept {
    std::array<wchar_t, 16> value{};
    const DWORD length = GetEnvironmentVariableW(
        L"WAWVR_DISABLE_XR", value.data(), static_cast<DWORD>(value.size()));
    return length != 0 && length < value.size() &&
           (value[0] == L'1' || value[0] == L'y' || value[0] == L'Y' ||
            value[0] == L't' || value[0] == L'T');
}

bool environment_enables_eye_local_bloom() noexcept {
    static const bool enabled = []() noexcept {
        std::array<wchar_t, 16> value{};
        const DWORD length = GetEnvironmentVariableW(
            L"WAWVR_ENABLE_EYE_LOCAL_BLOOM", value.data(),
            static_cast<DWORD>(value.size()));
        return length != 0 && length < value.size() &&
               (value[0] == L'1' || value[0] == L'y' ||
                value[0] == L'Y' || value[0] == L't' ||
                value[0] == L'T');
    }();
    return enabled;
}

bool environment_disables_virtual_desktop_ui_projection_fallback() noexcept {
    static const bool disabled = []() noexcept {
        std::array<wchar_t, 16> value{};
        const DWORD length = GetEnvironmentVariableW(
            L"WAWVR_DISABLE_VD_UI_PROJECTION_FALLBACK", value.data(),
            static_cast<DWORD>(value.size()));
        return length != 0 && length < value.size() &&
               (value[0] == L'1' || value[0] == L'y' ||
                value[0] == L'Y' || value[0] == L't' ||
                value[0] == L'T');
    }();
    return disabled;
}

[[nodiscard]] std::string environment_value_utf8(
    const wchar_t* const name) noexcept {
    try {
        std::array<wchar_t, 32768> value{};
        const DWORD length = GetEnvironmentVariableW(
            name, value.data(), static_cast<DWORD>(value.size()));
        if (length == 0 || length >= value.size()) {
            return {};
        }
        const int bytes = WideCharToMultiByte(
            CP_UTF8, 0, value.data(), static_cast<int>(length),
            nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) {
            return {};
        }
        std::string result(static_cast<std::size_t>(bytes), '\0');
        if (WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(length),
                result.data(), bytes, nullptr, nullptr) != bytes) {
            return {};
        }
        return result;
    } catch (...) {
        return {};
    }
}

void log_openxr_runtime_selection(const HMODULE module) noexcept {
    try {
        std::string selection = environment_value_utf8(
            L"WAWVR_OPENXR_RUNTIME_SELECTION");
        if (selection.empty()) {
            selection = "unmarked/system-default";
        }
        const std::string manifest = environment_value_utf8(
            L"XR_RUNTIME_JSON");
        std::ostringstream message;
        message << "OpenXR runtime launch selection: " << selection;
        if (manifest.empty()) {
            message << "; XR_RUNTIME_JSON is unset, so the 32-bit HKLM "
                       "ActiveRuntime is authoritative";
        } else {
            message << "; XR_RUNTIME_JSON=" << manifest;
        }
        append_log(module, wawvr::xr::LogLevel::Info, message.str());
    } catch (...) {
        append_log(
            module, wawvr::xr::LogLevel::Warning,
            "OpenXR runtime launch selection could not be formatted");
    }
}

class VtableSlotPatch final {
public:
    bool Install(
        void* const object,
        const std::size_t index,
        void* const expected,
        void* const replacement) noexcept {
        HookSlotPlan plan{};
        void** table = nullptr;
        if (!build_hook_slot_plan(expected, replacement, &plan) ||
            !executable_pointer(expected) || !executable_pointer(replacement) ||
            read_vtable_entry(object, index, &table) != expected) {
            return false;
        }

        slot_ = table + index;
        original_ = plan.original;
        replacement_ = plan.replacement;
        DWORD old_protection = 0;
        if (!VirtualProtect(
                slot_, sizeof(void*), PAGE_EXECUTE_READWRITE,
                &old_protection)) {
            Clear();
            return false;
        }
        auto* const slot = reinterpret_cast<PVOID volatile*>(slot_);
        void* const observed = InterlockedCompareExchangePointer(
            slot, replacement_, original_);
        DWORD ignored = 0;
        VirtualProtect(slot_, sizeof(void*), old_protection, &ignored);
        if (observed != original_) {
            Clear();
            return false;
        }
        installed_ = true;
        FlushInstructionCache(GetCurrentProcess(), slot_, sizeof(void*));
        return true;
    }

    VtableOwnership Restore() noexcept {
        if (!installed_ || slot_ == nullptr ||
            !readable_range(slot_, sizeof(void*))) {
            return VtableOwnership::foreign;
        }
        const auto ownership = classify_hook_slot_ownership(
            *slot_, original_, replacement_);
        if (ownership != VtableOwnership::installed_by_us) {
            installed_ = false;
            return ownership;
        }

        DWORD old_protection = 0;
        if (!VirtualProtect(
                slot_, sizeof(void*), PAGE_EXECUTE_READWRITE,
                &old_protection)) {
            return VtableOwnership::foreign;
        }
        auto* const slot = reinterpret_cast<PVOID volatile*>(slot_);
        void* const observed = InterlockedCompareExchangePointer(
            slot, original_, replacement_);
        DWORD ignored = 0;
        VirtualProtect(slot_, sizeof(void*), old_protection, &ignored);
        if (observed != replacement_) {
            return VtableOwnership::foreign;
        }
        installed_ = false;
        FlushInstructionCache(GetCurrentProcess(), slot_, sizeof(void*));
        return VtableOwnership::original;
    }

private:
    void Clear() noexcept {
        slot_ = nullptr;
        original_ = nullptr;
        replacement_ = nullptr;
        installed_ = false;
    }

    void** slot_{};
    void* original_{};
    void* replacement_{};
    bool installed_ = false;
};

class PresentHookController final {
public:
    explicit PresentHookController(const HMODULE module) noexcept : module_(module) {
        // The controller is process-lifetime storage. Publish it once and keep
        // it stable across target generations; retired thunk routes remain
        // independently callable even after final shutdown clears this pointer.
        instance_.store(this, std::memory_order_release);
    }

    bool Install(const T4PresentTarget& target) noexcept {
        if (target_swap_chain_.load(std::memory_order_acquire) != nullptr) {
            return false;
        }
        T4PresentTarget current_target{};
        if (!ReadValidatedTargetMatching(target, &current_target)) {
            Log(wawvr::xr::LogLevel::Warning,
                "D3D9 target changed before hook installation; renderer monitor will retry");
            return false;
        }
        void** swap_vtable = nullptr;
        void** reset_vtable = nullptr;
        const auto swap_present = read_vtable_entry(
            current_target.swap_chain, kDirect3DSwapChain9PresentIndex,
            &swap_vtable);
        const auto reset = read_vtable_entry(
            current_target.device, kDirect3DDevice9ResetIndex,
            &reset_vtable);
        HookSlotPlan swap_plan{};
        if (!build_hook_slot_plan(
                swap_present, reinterpret_cast<void*>(&SwapPresentThunk),
                &swap_plan) ||
            !executable_pointer(swap_present)) {
            Log(wawvr::xr::LogLevel::Warning,
                "T4 swap-chain Present slot failed executable-pointer validation");
            return false;
        }

        LogFormatted(
            wawvr::xr::LogLevel::Info,
            "Validated RB_SwapBuffers target: index=%ld windowCount=%lu device=%p swapChain=%p Present=%p",
            static_cast<long>(current_target.target_window_index),
            static_cast<unsigned long>(current_target.window_count),
            static_cast<void*>(current_target.device),
            static_cast<void*>(current_target.swap_chain), swap_present);

        const auto present_vtable_route =
            g_swap_present_vtable_routes.Register(
                swap_vtable,
                reinterpret_cast<SwapChainPresentFunction>(swap_present));
        const auto present_route = g_swap_present_thunk_routes.Register(
            current_target.swap_chain,
            reinterpret_cast<SwapChainPresentFunction>(swap_present));
        if (!route_registration_usable(present_vtable_route)) {
            LogFormatted(
                wawvr::xr::LogLevel::Warning,
                "Swap-chain Present thunk route rejected (vtable=%s object=%s); target left unpatched",
                route_registration_name(present_vtable_route),
                route_registration_name(present_route));
            return false;
        }
        if (!route_registration_usable(present_route)) {
            LogFormatted(
                wawvr::xr::LogLevel::Info,
                "Swap-chain object route unavailable (%s); the validated shared-vtable route remains active",
                route_registration_name(present_route));
        }

        if (!swap_present_patch_.Install(
                current_target.swap_chain, kDirect3DSwapChain9PresentIndex,
                swap_present, reinterpret_cast<void*>(&SwapPresentThunk))) {
            Log(wawvr::xr::LogLevel::Warning,
                "Swap-chain Present slot changed during installation; hook skipped");
            return false;
        }
        target_device_.store(current_target.device, std::memory_order_release);
        target_swap_chain_.store(
            current_target.swap_chain, std::memory_order_release);
        g_present_hook_installed.store(true, std::memory_order_release);
        Log(wawvr::xr::LogLevel::Info,
            "Exact T4 swap-chain Present hook installed (RB_SwapBuffers vtable +0x0C)");

        if (d3d9ex_shared_bridge_ready(current_target.device)) {
            D3DDEVICE_CREATION_PARAMETERS creation{};
            if (FAILED(current_target.device->GetCreationParameters(&creation)) ||
                creation.hFocusWindow == nullptr ||
                !register_d3d9ex_reset_handler(
                    current_target.device, &BootstrapResetHandler)) {
                Log(wawvr::xr::LogLevel::Error,
                    "D3D9Ex bootstrap Reset lifecycle registration failed; Present target rolled back");
                RestoreD3DSlotsForRebind();
                return false;
            }
            ex_focus_window_.store(
                creation.hFocusWindow, std::memory_order_release);
            ex_reset_failed_.store(false, std::memory_order_release);
            bootstrap_reset_handler_registered_.store(
                true, std::memory_order_release);
            reset_hook_installed_.store(true, std::memory_order_release);
            Log(wawvr::xr::LogLevel::Info,
                "D3D9Ex bootstrap ResetEx lifecycle route registered (slot 16 remains process-lifetime owned)");
        } else {
            HookSlotPlan reset_plan{};
            if (build_hook_slot_plan(
                    reset, reinterpret_cast<void*>(&ResetThunk),
                    &reset_plan) &&
                executable_pointer(reset)) {
                const auto reset_vtable_route =
                    g_reset_vtable_routes.Register(
                        reset_vtable,
                        reinterpret_cast<ResetFunction>(reset));
                const auto reset_route = g_reset_thunk_routes.Register(
                    current_target.device,
                    reinterpret_cast<ResetFunction>(reset));
                if (!route_registration_usable(reset_vtable_route)) {
                    LogFormatted(
                        wawvr::xr::LogLevel::Warning,
                        "D3D9 Reset thunk route rejected (vtable=%s object=%s); Present hook remains active",
                        route_registration_name(reset_vtable_route),
                        route_registration_name(reset_route));
                } else {
                    if (!route_registration_usable(reset_route)) {
                        LogFormatted(
                            wawvr::xr::LogLevel::Info,
                            "D3D9 Reset object route unavailable (%s); the validated shared-vtable route remains active",
                            route_registration_name(reset_route));
                    }
                    if (device_reset_patch_.Install(
                            current_target.device,
                            kDirect3DDevice9ResetIndex, reset,
                            reinterpret_cast<void*>(&ResetThunk))) {
                        legacy_reset_patch_installed_.store(
                            true, std::memory_order_release);
                        reset_hook_installed_.store(
                            true, std::memory_order_release);
                        Log(wawvr::xr::LogLevel::Info,
                            "Legacy D3D9 Reset slot hook installed without changing the device vptr");
                    } else {
                        Log(wawvr::xr::LogLevel::Warning,
                            "D3D9 Reset slot changed during installation; Present hook remains active");
                    }
                }
            } else {
                Log(wawvr::xr::LogLevel::Warning,
                    "D3D9 Reset slot failed validation; Present hook remains active");
            }
        }
        // Scene/backend installation is intentionally deferred to the first
        // post-Com_Frame service while render_mutex_ is held. Publishing the
        // Present thunk must not race a second installer on the game thread.
        if (!ReadValidatedTargetMatching(current_target, nullptr) ||
            g_shutdown_requested.load(std::memory_order_acquire)) {
            PauseFrameServiceForTargetRefresh();
            RestoreD3DSlotsForRebind();
            Log(wawvr::xr::LogLevel::Warning,
                "D3D9 target changed during hook installation; new slots restored and retry scheduled");
            return false;
        }
        target_refresh_requested_.store(false, std::memory_order_release);
        frame_service_ready_.store(true, std::memory_order_release);
        return true;
    }

    bool Rebind(const T4PresentTarget& target) noexcept {
        IDirect3DDevice9* const previous_device =
            target_device_.load(std::memory_order_acquire);
        IDirect3DSwapChain9* const previous_swap_chain =
            target_swap_chain_.load(std::memory_order_acquire);
        if (previous_device == target.device &&
            previous_swap_chain == target.swap_chain) {
            ResumeFrameServiceForCurrentTarget();
            return true;
        }

        PauseFrameServiceForTargetRefresh();
        bool reset_was_idle = false;
        if (!reset_in_progress_.compare_exchange_strong(
                reset_was_idle, true, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            Log(wawvr::xr::LogLevel::Info,
                "Renderer target rebind deferred until the active D3D9 Reset completes");
            return false;
        }
        LogFormatted(
            wawvr::xr::LogLevel::Info,
            "Validated T4 D3D target replacement: oldDevice=%p oldSwapChain=%p newDevice=%p newSwapChain=%p; rebinding renderer hooks",
            static_cast<void*>(previous_device),
            static_cast<void*>(previous_swap_chain),
            static_cast<void*>(target.device),
            static_cast<void*>(target.swap_chain));

        try {
            std::lock_guard<std::mutex> lock(render_mutex_);
            ShutdownXr("validated T4 D3D target recreation");
            RestoreD3DSlotsForRebind();
            ex_device_recovery_required_ = false;
            ResetBoundaryObservations();
            next_runtime_attempt_ = 0;
        } catch (...) {
            reset_in_progress_.store(false, std::memory_order_release);
            Log(wawvr::xr::LogLevel::Error,
                "Renderer rebind could not acquire its state lock; replacement target was not patched");
            return false;
        }

        T4PresentTarget refreshed_target{};
        const bool target_remains_current =
            ReadValidatedTargetMatching(target, &refreshed_target);
        if (!target_remains_current) {
            reset_in_progress_.store(false, std::memory_order_release);
            frame_service_ready_.store(false, std::memory_order_release);
            target_refresh_requested_.store(true, std::memory_order_release);
            Log(wawvr::xr::LogLevel::Warning,
                "Validated T4 D3D target changed during XR teardown; stale replacement was not patched and monitor will retry");
            return false;
        }

        const bool installed = Install(refreshed_target);
        reset_in_progress_.store(false, std::memory_order_release);
        if (installed) {
            target_refresh_requested_.store(false, std::memory_order_release);
            Log(wawvr::xr::LogLevel::Info,
                "Replacement D3D9 target rebound; OpenXR restart scheduled at the next exact post-Com_Frame boundary");
        } else {
            frame_service_ready_.store(false, std::memory_order_release);
            target_refresh_requested_.store(true, std::memory_order_release);
            Log(wawvr::xr::LogLevel::Warning,
                "Replacement D3D9 target was not yet safe to hook; renderer monitor will retry");
        }
        return installed;
    }

    void PauseFrameServiceForTargetRefresh() noexcept {
        frame_service_ready_.store(false, std::memory_order_release);
        if (!target_refresh_requested_.exchange(
                true, std::memory_order_acq_rel)) {
            Log(wawvr::xr::LogLevel::Info,
                "T4 D3D target is changing; XR frame service paused until a validated target is available");
        }
    }

    void ResumeFrameServiceForCurrentTarget() noexcept {
        if (target_swap_chain_.load(std::memory_order_acquire) == nullptr ||
            reset_in_progress_.load(std::memory_order_acquire)) {
            return;
        }
        if (target_refresh_requested_.exchange(
                false, std::memory_order_acq_rel)) {
            Log(wawvr::xr::LogLevel::Info,
                "Validated T4 D3D target remained current; XR frame service resumed");
        }
        frame_service_ready_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool ShutdownRenderStateAndRestoreSlots() noexcept {
        frame_service_ready_.store(false, std::memory_order_release);

        // The monitor is independent of the game's Reset thread. Claim the
        // same lifecycle gate used by Reset/Rebind before touching XR or the
        // permanent bootstrap Reset route, and retain all hooks fail-closed if
        // a native Reset does not unwind within the bounded shutdown window.
        const ULONGLONG deadline =
            GetTickCount64() + kResetTeardownWaitMilliseconds;
        bool owns_reset_lifecycle = false;
        do {
            bool reset_was_idle = false;
            owns_reset_lifecycle =
                reset_in_progress_.compare_exchange_weak(
                    reset_was_idle, true, std::memory_order_acq_rel,
                    std::memory_order_acquire);
            if (owns_reset_lifecycle) {
                break;
            }
            Sleep(1);
        } while (GetTickCount64() < deadline);
        if (!owns_reset_lifecycle) {
            Log(wawvr::xr::LogLevel::Error,
                "Renderer shutdown could not claim Reset ownership; XR state and hooks remain pinned fail-closed");
            return false;
        }
        AtomicBoolClearGuard reset_guard(&reset_in_progress_);

        try {
            std::lock_guard<std::mutex> lock(render_mutex_);
            ShutdownXr("cooperative renderer shutdown");
            RestoreSlots();
            ex_device_recovery_required_ = false;
        } catch (...) {
            Log(wawvr::xr::LogLevel::Error,
                "Renderer shutdown could not acquire its state lock; XR state and hooks remain pinned fail-closed");
            return false;
        }
        return true;
    }

    void RestoreSlots() noexcept {
        frame_service_ready_.store(false, std::memory_order_release);
        clear_pending_stereo_frame();
        clear_controller_frame();
        if (stereo_scene_hook_installed()) {
            const auto result = restore_stereo_scene_hook();
            if (result == StereoSceneHookResult::installed) {
                Log(wawvr::xr::LogLevel::Info,
                    "T4 gameplay R_RenderScene call site restored");
            } else {
                LogFormatted(
                    wawvr::xr::LogLevel::Warning,
                    "T4 gameplay scene hook restore failed closed: %s",
                    describe_stereo_scene_hook_result(result));
            }
        }
        RestoreD3DSlotsForRebind();
        PresentHookController* expected = this;
        instance_.compare_exchange_strong(
            expected, nullptr, std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    [[nodiscard]] IDirect3DDevice9* target_device() const noexcept {
        return target_device_.load(std::memory_order_acquire);
    }

    [[nodiscard]] IDirect3DSwapChain9* target_swap_chain() const noexcept {
        return target_swap_chain_.load(std::memory_order_acquire);
    }

    static void ServicePublishedAfterComFrame() noexcept {
        auto* const instance = instance_.load(std::memory_order_acquire);
        if (instance == nullptr ||
            !instance->frame_service_ready_.load(std::memory_order_acquire) ||
            g_shutdown_requested.load(std::memory_order_acquire)) {
            return;
        }
        ScopedPerformanceTiming timing(
            PerformanceTimingPhase::post_com_frame_xr_service);
        try {
            std::lock_guard<std::mutex> lock(instance->render_mutex_);
            if (g_shutdown_requested.load(std::memory_order_acquire) ||
                !instance->frame_service_ready_.load(
                    std::memory_order_acquire)) {
                return;
            }
            instance->ServiceAfterComFrame();
        } catch (...) {
            instance->Log(
                wawvr::xr::LogLevel::Error,
                "Unhandled exception at post-Com_Frame XR boundary; XR path stopped safely");
            try {
                std::lock_guard<std::mutex> lock(instance->render_mutex_);
                instance->ShutdownXr("post-Com_Frame exception");
            } catch (...) {
                instance->frame_service_ready_.store(
                    false, std::memory_order_release);
                g_shutdown_requested.store(true, std::memory_order_release);
                instance->Log(
                    wawvr::xr::LogLevel::Error,
                    "Post-Com_Frame exception cleanup could not acquire its state lock; renderer shutdown requested");
            }
        }
    }

private:
    static std::atomic<PresentHookController*> instance_;

    static HRESULT STDMETHODCALLTYPE SwapPresentThunk(
        IDirect3DSwapChain9* const swap_chain,
        const RECT* const source_rect,
        const RECT* const destination_rect,
        const HWND destination_window,
        const RGNDATA* const dirty_region,
        const DWORD flags) noexcept {
        auto* const instance = instance_.load(std::memory_order_acquire);
        if (instance == nullptr) {
            const auto original = lookup_swap_present_route(swap_chain);
            return original != nullptr
                       ? original(
                             swap_chain, source_rect, destination_rect,
                             destination_window, dirty_region, flags)
                       : D3DERR_INVALIDCALL;
        }
        return instance->OnSwapPresent(
            swap_chain, source_rect, destination_rect, destination_window,
            dirty_region, flags);
    }

    static HRESULT STDMETHODCALLTYPE ResetThunk(
        IDirect3DDevice9* const device,
        D3DPRESENT_PARAMETERS* const parameters) noexcept {
        auto* const instance = instance_.load(std::memory_order_acquire);
        if (instance == nullptr) {
            const auto original = lookup_reset_route(device);
            return original != nullptr
                       ? original(device, parameters)
                       : D3DERR_INVALIDCALL;
        }
        return instance->OnReset(device, parameters);
    }

    static std::int32_t BootstrapResetHandler(
        IDirect3DDevice9* const device,
        _D3DPRESENT_PARAMETERS_* const parameters) noexcept {
        auto* const instance = instance_.load(std::memory_order_acquire);
        return instance != nullptr
            ? static_cast<std::int32_t>(
                  instance->OnReset(device, parameters))
            : static_cast<std::int32_t>(D3DERR_DEVICELOST);
    }

    HRESULT OnSwapPresent(
        IDirect3DSwapChain9* const swap_chain,
        const RECT* const source_rect,
        const RECT* const destination_rect,
        const HWND destination_window,
        const RGNDATA* const dirty_region,
        const DWORD flags) noexcept {
        if (swap_chain ==
                target_swap_chain_.load(std::memory_order_acquire) &&
            ex_reset_failed_.load(std::memory_order_acquire)) {
            // After ResetEx reports DEVICELOST/HUNG, D3D9Ex permits only
            // ResetEx, CheckDeviceState, and Release. Do not forward another
            // Present or touch capture resources until ResetEx succeeds.
            return D3DERR_DEVICELOST;
        }
        const auto original = lookup_swap_present_route(swap_chain);
        if (original == nullptr) {
            return D3DERR_INVALIDCALL;
        }

        static thread_local bool inside_hook = false;
        bool owns_present =
            !inside_hook &&
            swap_chain == target_swap_chain_.load(std::memory_order_acquire) &&
            frame_service_ready_.load(std::memory_order_acquire) &&
            !reset_in_progress_.load(std::memory_order_acquire) &&
            !ex_reset_failed_.load(std::memory_order_acquire) &&
            !g_shutdown_requested.load(std::memory_order_acquire);
        const bool entered_hook = owns_present;
        std::uint64_t diagnostic_present_frame_id = 0;
        ScopedPerformanceTiming present_timing(
            PerformanceTimingPhase::target_swapchain_present_path,
            entered_hook);
        if (owns_present) {
            inside_hook = true;
            try {
                std::lock_guard<std::mutex> lock(render_mutex_);
                owns_present =
                    swap_chain ==
                        target_swap_chain_.load(std::memory_order_acquire) &&
                    frame_service_ready_.load(std::memory_order_acquire) &&
                    !reset_in_progress_.load(std::memory_order_acquire) &&
                    !ex_reset_failed_.load(std::memory_order_acquire) &&
                    !ex_device_recovery_required_ &&
                    !g_shutdown_requested.load(std::memory_order_acquire);
                if (owns_present) {
                    if (runtime_.timing_diagnostics_enabled()) {
                        diagnostic_present_frame_id = runtime_.timing_frame_id();
                    }
                    IDirect3DDevice9* const target_device =
                        target_device_.load(std::memory_order_acquire);
                    capture_smoke_before_present(target_device);
                    CaptureVrFrameBeforePresent(target_device);
                }
            } catch (...) {
                Log(wawvr::xr::LogLevel::Error,
                    "Unhandled exception during pre-Present capture; desktop Present preserved");
            }
        }

        const auto diagnostic_present_started = diagnostic_present_frame_id != 0
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const HRESULT result = original(
            swap_chain, source_rect, destination_rect, destination_window,
            dirty_region, flags);
        const auto diagnostic_present_finished = diagnostic_present_frame_id != 0
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};

        if (entered_hook) {
            try {
                std::lock_guard<std::mutex> lock(render_mutex_);
                if (owns_present &&
                    swap_chain ==
                        target_swap_chain_.load(std::memory_order_acquire) &&
                    !reset_in_progress_.load(std::memory_order_acquire)) {
                    if (diagnostic_present_frame_id != 0) {
                        runtime_.RecordDiagnosticStage(diagnostic_present_frame_id,
                            wawvr::xr::FrameTimingStage::d3d9_present,
                            diagnostic_present_finished - diagnostic_present_started);
                    }
                    RecordPresentResult(result);
                }
            } catch (...) {
                Log(wawvr::xr::LogLevel::Error,
                    "Unhandled exception recording D3D9 Present result");
            }
            inside_hook = false;
        }
        return result;
    }

    HRESULT OnReset(
        IDirect3DDevice9* const device,
        D3DPRESENT_PARAMETERS* const parameters) noexcept {
        const bool bootstrap_ex_route =
            bootstrap_reset_handler_registered_.load(
                std::memory_order_acquire) &&
            device == target_device_.load(std::memory_order_acquire);
        const auto original = bootstrap_ex_route
            ? nullptr
            : lookup_reset_route(device);
        if (device != target_device_.load(std::memory_order_acquire)) {
            return original != nullptr
                ? original(device, parameters)
                : D3DERR_INVALIDCALL;
        }
        if (!bootstrap_ex_route && original == nullptr) {
            return D3DERR_INVALIDCALL;
        }
        if (reset_in_progress_.exchange(true, std::memory_order_acq_rel)) {
            return D3DERR_DEVICELOST;
        }
        AtomicBoolClearGuard reset_guard(&reset_in_progress_);
        bool lost_ex_reset = false;

        bool prepared_for_reset = false;
        try {
            std::lock_guard<std::mutex> lock(render_mutex_);
            const bool present_requires_recovery =
                latest_present_result_ == D3DERR_DEVICELOST ||
                latest_present_result_ == D3DERR_DEVICENOTRESET ||
                latest_present_result_ == D3DERR_DEVICEHUNG ||
                latest_present_result_ == D3DERR_DEVICEREMOVED ||
                latest_present_result_ == S_PRESENT_MODE_CHANGED;
            lost_ex_reset = d3d9ex_reset_requires_lost_preflight(
                bootstrap_ex_route,
                ex_reset_failed_.load(std::memory_order_acquire),
                device_loss_seen_since_boundary_,
                present_requires_recovery,
                ex_device_recovery_required_);
            ID3D11DeviceContext* const d3d11_context =
                runtime_.initialized()
                    ? runtime_.d3d11_context()
                    : nullptr;
            const bool shared_prepared =
                !shared_capture_session_enabled_ ||
                (lost_ex_reset
                     ? shared_capture_.PrepareForLostDeviceReset(
                           d3d11_context)
                     : shared_capture_.Invalidate(d3d11_context));
            if (!shared_prepared) {
                if (bootstrap_ex_route) {
                    // The engine was told that this reset attempt encountered
                    // device loss. Keep every later Present and retry on the
                    // D3D11-only recovery route even when ResetEx itself was
                    // deliberately deferred before invocation.
                    ex_reset_failed_.store(true, std::memory_order_release);
                }
                Log(wawvr::xr::LogLevel::Error,
                    lost_ex_reset
                        ? "D3D9Ex lost-device Reset deferred because D3D11 shared consumers could not be retired without touching the lost D3D9 device"
                        : "D3D9Ex Reset deferred because shared GPU frames could not be retired safely");
                return D3DERR_DEVICELOST;
            }
            // Invalidation retires the complete shared generation, including
            // any capture prepared for the boundary that triggered Reset.
            queued_shared_captures_.clear();
            last_consumed_shared_capture_serial_ = 0;
            prepared_frame_ = {};
            device_loss_seen_since_boundary_ = true;
            reset_seen_since_boundary_ = true;
            clear_pending_stereo_frame();
            clear_controller_frame();
            capture_.NotifyExternalDeviceLoss();
            Log(wawvr::xr::LogLevel::Info,
                "D3D9 Reset entered: capture paused; pending OpenXR frame retained for post-Com_Frame service");
            prepared_for_reset = true;
        } catch (...) {
            if (bootstrap_ex_route) {
                ex_reset_failed_.store(true, std::memory_order_release);
            }
            Log(wawvr::xr::LogLevel::Error,
                "Unhandled exception while preparing for D3D9 Reset; Reset deferred fail-closed");
            return D3DERR_DEVICELOST;
        }
        if (!prepared_for_reset) {
            return D3DERR_DEVICELOST;
        }
        const HRESULT result = bootstrap_ex_route
            ? static_cast<HRESULT>(
                  perform_d3d9ex_reset(device, parameters))
            : original(device, parameters);
        if (bootstrap_ex_route) {
            ex_reset_failed_.store(
                FAILED(result), std::memory_order_release);
        }
        try {
            std::lock_guard<std::mutex> lock(render_mutex_);
            if (bootstrap_ex_route && SUCCEEDED(result) && lost_ex_reset) {
                ex_device_recovery_required_ = false;
                if (shared_capture_session_enabled_ &&
                    !shared_capture_.FinalizeSuccessfulLostDeviceReset()) {
                    Log(wawvr::xr::LogLevel::Error,
                        "D3D9Ex recovered, but the retained shared generation could not be finalized; GPU capture quarantined for this process");
                    shared_capture_session_enabled_ = false;
                    shared_capture_permanently_disabled_ = true;
                }
            }
            latest_reset_result_ = result;
            if (SUCCEEDED(result)) {
                Log(wawvr::xr::LogLevel::Info,
                    "D3D9 Reset completed; post-Com_Frame service will preserve or re-prime XR after the engine frame unwinds");
            } else {
                LogFormatted(
                    wawvr::xr::LogLevel::Warning,
                    "D3D9 Reset returned 0x%08lx; capture remains paused while T4 retries desktop recovery",
                    result);
            }
        } catch (...) {
            Log(wawvr::xr::LogLevel::Error,
                "Unhandled exception while recording D3D9 Reset result");
        }
        return result;
    }

    struct PreparedPendingFrame final {
        bool active{};
        bool presentation_observed{};
        std::uint64_t observed_frame_id{};
        wawvr::xr::FrameState frame{};
        wawvr::xr::StereoSourceLayout layout{};
        PresentationMode presentation_mode{PresentationMode::stereo};
        PresentationMode observed_presentation_mode{PresentationMode::stereo};
        bool t4_state_valid{};
        std::int32_t connection_state{};
        std::uint32_t key_catchers{};
        bool stereo_scene{};
        PreparedCaptureKind capture_kind{PreparedCaptureKind::none};
        std::uint64_t capture_serial{};
        wawvr::xr::D3D9CpuCaptureResult capture_result{
            wawvr::xr::D3D9CpuCaptureResult::unavailable};
#if defined(WAWVR_HAS_T4_BINDINGS)
        bool weapon_receipt_valid{};
        FinalVisibleWeaponReceipt weapon_receipt{};
#endif
    };

#if defined(WAWVR_HAS_T4_BINDINGS)
    void ObserveCaptureSubmissionDiagnostic(
        const wawvr::xr::FrameState& live_frame,
        const PreparedPendingFrame& source_prepared,
        const wawvr::xr::D3D11SourceFrame& source,
        const std::size_t shared_queue_depth) noexcept {
        constexpr std::uint32_t kSamplesPerReport = 120;
        constexpr std::uint32_t kMaximumReports = 24;
        if (capture_submission_diagnostic_report_count_ >= kMaximumReports) {
            return;
        }
        // Frontend and loading-panel frames can run for many seconds before a
        // tracked weapon is actually held. Do not spend this bounded probe
        // there. Arm on the first gameplay stereo capture carrying a live
        // controller-owned weapon receipt, then retain missing/mismatch
        // accounting so every later grip transition remains visible.
        const bool gameplay_stereo =
            source_prepared.presentation_mode == PresentationMode::stereo &&
            source_prepared.stereo_scene;
        if (!gameplay_stereo) {
            return;
        }
        if (!capture_submission_diagnostic_armed_) {
            if (!source_prepared.weapon_receipt_valid ||
                !source_prepared.weapon_receipt.live_controller_pose) {
                return;
            }
            capture_submission_diagnostic_armed_ = true;
            capture_submission_diagnostic_window_ = {};
            Log(wawvr::xr::LogLevel::Info,
                "WeaponDiag capture-submit probe armed on first live controller-held gameplay weapon receipt");
        }
        const auto observation = observe_capture_submission_phase(
            live_frame.frame_id, live_frame.predicted_display_time,
            source_prepared.frame.frame_id,
            source_prepared.frame.predicted_display_time,
            source_prepared.weapon_receipt_valid,
            source_prepared.weapon_receipt.frame_id,
            source_prepared.weapon_receipt.action_sequence,
            source_prepared.frame.actions.sequence);
        if (!observation.valid) {
            return;
        }

        auto& window = capture_submission_diagnostic_window_;
        ++window.samples;
        window.frame_lag_sum += static_cast<double>(
            observation.live_minus_source_frame_id);
        window.frame_lag_min = (std::min)(
            window.frame_lag_min,
            observation.live_minus_source_frame_id);
        window.frame_lag_max = (std::max)(
            window.frame_lag_max,
            observation.live_minus_source_frame_id);
        constexpr double kNanosecondsToMilliseconds = 1.0 / 1'000'000.0;
        const double predicted_lag_milliseconds =
            static_cast<double>(
                observation.live_minus_source_predicted_time) *
            kNanosecondsToMilliseconds;
        window.predicted_lag_milliseconds_sum +=
            predicted_lag_milliseconds;
        window.predicted_lag_milliseconds_min = (std::min)(
            window.predicted_lag_milliseconds_min,
            predicted_lag_milliseconds);
        window.predicted_lag_milliseconds_max = (std::max)(
            window.predicted_lag_milliseconds_max,
            predicted_lag_milliseconds);
        window.shared_queue_depth_sum +=
            static_cast<double>(shared_queue_depth);
        window.shared_queue_depth_max = (std::max)(
            window.shared_queue_depth_max, shared_queue_depth);
        if (observation.live_minus_source_frame_id == 0) {
            ++window.same_frame;
        } else if (observation.live_minus_source_frame_id > 0) {
            ++window.source_older;
        } else {
            ++window.source_newer;
        }
        if (!source_prepared.weapon_receipt_valid) {
            ++window.weapon_missing;
        } else if (!observation.weapon_receipt_matches_source) {
            ++window.weapon_mismatched;
        } else {
            ++window.receipt_matching;
            if (source_prepared.weapon_receipt.live_controller_pose) {
                ++window.weapon_live;
            } else {
                ++window.weapon_retained;
            }
        }
        if (window.samples < kSamplesPerReport) {
            return;
        }

        const char* const latest_weapon_pose =
            !source_prepared.weapon_receipt_valid
            ? "missing"
            : !observation.weapon_receipt_matches_source
                ? "mismatched"
                : source_prepared.weapon_receipt.live_controller_pose
                    ? "live" : "retained";
        LogFormatted(
            wawvr::xr::LogLevel::Info,
            "WeaponDiag capture-submit window[%u] n=%u liveFrame=%llu livePred=%lld sourceFrame=%llu sourcePred=%lld frameAgeMeanRange=%.3f/%lld-%lld predictedAgeMeanRange=%.3f/%.3f-%.3fms capture=%s serial=%llu queueDepthLatestMeanMax=%llu/%.3f/%llu weaponPose=%s receiptFrame=%llu receiptAction=%llu sourceAction=%llu sameFrame=%u sourceOlder=%u sourceNewer=%u receiptMatch=%u live=%u retained=%u missing=%u mismatch=%u",
            capture_submission_diagnostic_report_count_, window.samples,
            static_cast<unsigned long long>(live_frame.frame_id),
            static_cast<long long>(live_frame.predicted_display_time),
            static_cast<unsigned long long>(source_prepared.frame.frame_id),
            static_cast<long long>(
                source_prepared.frame.predicted_display_time),
            window.frame_lag_sum / static_cast<double>(window.samples),
            static_cast<long long>(window.frame_lag_min),
            static_cast<long long>(window.frame_lag_max),
            window.predicted_lag_milliseconds_sum /
                static_cast<double>(window.samples),
            window.predicted_lag_milliseconds_min,
            window.predicted_lag_milliseconds_max,
            prepared_capture_kind_name(source_prepared.capture_kind),
            static_cast<unsigned long long>(source.serial),
            static_cast<unsigned long long>(shared_queue_depth),
            window.shared_queue_depth_sum /
                static_cast<double>(window.samples),
            static_cast<unsigned long long>(
                window.shared_queue_depth_max),
            latest_weapon_pose,
            static_cast<unsigned long long>(
                source_prepared.weapon_receipt.frame_id),
            static_cast<unsigned long long>(
                source_prepared.weapon_receipt.action_sequence),
            static_cast<unsigned long long>(
                source_prepared.frame.actions.sequence),
            window.same_frame, window.source_older, window.source_newer,
            window.receipt_matching, window.weapon_live,
            window.weapon_retained, window.weapon_missing,
            window.weapon_mismatched);
        ++capture_submission_diagnostic_report_count_;
        window = {};
    }
#endif

    [[nodiscard]] bool AbandonSharedCaptureSerial(
        const std::uint64_t serial,
        const std::string_view reason) noexcept {
        if (serial == 0) {
            return true;
        }
        if (!shared_capture_session_enabled_) {
            Log(wawvr::xr::LogLevel::Error,
                "A shared capture serial survived outside its owning XR session; GPU bridge quarantined");
            shared_capture_permanently_disabled_ = true;
            return false;
        }
        try {
            if (shared_capture_.AbandonSerial(serial)) {
                return true;
            }
            LogFormatted(
                wawvr::xr::LogLevel::Error,
                "Could not relinquish D3D9Ex capture serial %llu (%.*s); invalidating the shared generation",
                static_cast<unsigned long long>(serial),
                static_cast<int>(reason.size()), reason.data());
            if (shared_capture_.Invalidate(
                    runtime_.initialized()
                        ? runtime_.d3d11_context()
                        : nullptr)) {
                queued_shared_captures_.clear();
                last_consumed_shared_capture_serial_ = 0;
                return true;
            }
        } catch (...) {
        }
        shared_capture_session_enabled_ = false;
        shared_capture_permanently_disabled_ = true;
        queued_shared_captures_.clear();
        last_consumed_shared_capture_serial_ = 0;
        Log(wawvr::xr::LogLevel::Error,
            "D3D9Ex capture ownership could not be retired safely; GPU capture quarantined for this process");
        return false;
    }

    void DiscardPreparedSharedCapture(
        PreparedPendingFrame* const prepared,
        const std::string_view reason) noexcept {
        if (prepared == nullptr || !prepared->active ||
            prepared->capture_kind != PreparedCaptureKind::shared_gpu ||
            prepared->capture_serial == 0) {
            return;
        }
        static_cast<void>(AbandonSharedCaptureSerial(
            prepared->capture_serial, reason));
        prepared->active = false;
        prepared->capture_kind = PreparedCaptureKind::none;
        prepared->capture_serial = 0;
        prepared->capture_result =
            wawvr::xr::D3D9CpuCaptureResult::unavailable;
    }

    static constexpr std::size_t kSharedCaptureQueueCapacity = 4;

    void DiscardQueuedSharedCaptures(
        const std::string_view reason) noexcept {
        while (!queued_shared_captures_.empty()) {
            const std::uint64_t serial =
                queued_shared_captures_.front().capture_serial;
            queued_shared_captures_.pop_front();
            if (!AbandonSharedCaptureSerial(serial, reason)) {
                queued_shared_captures_.clear();
                return;
            }
        }
    }

    void DiscardIncompatibleQueuedSharedCaptures(
        const PresentationMode current_mode) noexcept {
        for (;;) {
            const auto entry = std::find_if(
                queued_shared_captures_.begin(),
                queued_shared_captures_.end(),
                [current_mode](const PreparedPendingFrame& candidate) {
                    return !presentation_modes_share_capture_class(
                        candidate.presentation_mode, current_mode);
                });
            if (entry == queued_shared_captures_.end()) {
                return;
            }
            const std::uint64_t serial = entry->capture_serial;
            queued_shared_captures_.erase(entry);
            if (!AbandonSharedCaptureSerial(
                    serial, "crossed a stereo/mono presentation transition")) {
                queued_shared_captures_.clear();
                return;
            }
        }
    }

    bool QueuePreparedSharedCapture(
        const PreparedPendingFrame& prepared) noexcept {
        if (!prepared.active ||
            prepared.capture_kind != PreparedCaptureKind::shared_gpu ||
            prepared.capture_serial == 0) {
            return false;
        }
        if (queued_shared_captures_.size() >=
            kSharedCaptureQueueCapacity) {
            LogFormatted(
                wawvr::xr::LogLevel::Warning,
                "Deferred D3D9Ex capture metadata queue is full; relinquishing serial %llu without blocking the game thread",
                static_cast<unsigned long long>(prepared.capture_serial));
            return false;
        }
        try {
            queued_shared_captures_.push_back(prepared);
        } catch (...) {
            LogFormatted(
                wawvr::xr::LogLevel::Error,
                "Could not retain deferred D3D9Ex capture metadata; relinquishing serial %llu without blocking the game thread",
                static_cast<unsigned long long>(prepared.capture_serial));
            return false;
        }
        return true;
    }

    bool TakeQueuedSharedCapture(
        const std::uint64_t serial,
        PreparedPendingFrame* const prepared) noexcept {
        if (serial == 0 || prepared == nullptr) {
            return false;
        }
        const auto selected = std::find_if(
            queued_shared_captures_.begin(),
            queued_shared_captures_.end(),
            [serial](const PreparedPendingFrame& candidate) {
                return candidate.capture_serial == serial;
            });
        if (selected == queued_shared_captures_.end()) {
            return false;
        }

        // A flushing fence poll may expose a later ready slot while an older
        // serial is still producer-owned. The selected image remains paired
        // with its exact metadata; explicitly defer-retire each skipped serial
        // so it cannot become unreachable after the consumed watermark moves.
        for (auto skipped = queued_shared_captures_.begin();
             skipped != selected; ++skipped) {
            try {
                if (!shared_capture_.AbandonSerial(
                        skipped->capture_serial)) {
                    return false;
                }
            } catch (...) {
                return false;
            }
        }
        *prepared = *selected;
        queued_shared_captures_.erase(
            queued_shared_captures_.begin(), std::next(selected));
        return true;
    }

    class ScopedXrTiming final {
    public:
        ScopedXrTiming(wawvr::xr::OpenXrRuntime& runtime,
                      wawvr::xr::FrameTimingStage stage) noexcept
            : runtime_(runtime), stage_(stage),
              enabled_(runtime.timing_diagnostics_enabled()) {
            if (enabled_) {
                frame_id_ = runtime_.timing_frame_id();
                started_ = std::chrono::steady_clock::now();
            }
        }
        ~ScopedXrTiming() noexcept {
            if (enabled_) {
                runtime_.RecordDiagnosticStage(frame_id_, stage_,
                    std::chrono::steady_clock::now() - started_);
            }
        }
        ScopedXrTiming(const ScopedXrTiming&) = delete;
        ScopedXrTiming& operator=(const ScopedXrTiming&) = delete;
    private:
        wawvr::xr::OpenXrRuntime& runtime_;
        wawvr::xr::FrameTimingStage stage_;
        bool enabled_{};
        std::uint64_t frame_id_{};
        std::chrono::steady_clock::time_point started_{};
    };

    void CaptureVrFrameBeforePresent(IDirect3DDevice9* const device) {
        if (device == nullptr || !runtime_.initialized() ||
            !pending_frame_active_) {
            return;
        }
        ScopedXrTiming capture_timing(
            runtime_, wawvr::xr::FrameTimingStage::d3d9_capture);

        // The gameplay scene hook used this frame's predicted pose/FOV while
        // T4 built the current backbuffer. Copy the legacy D3D9 image before
        // Present. This callback may poll already-issued D3D11 retirement
        // fences so the shared ring can recycle, but it performs no D3D11
        // rendering or OpenXR submission; the validated post-Com_Frame
        // callsite remains the sole owner of composition and pacing.
        wawvr::xr::StereoSourceLayout layout{};
        const bool stereo_scene = try_get_rendered_stereo_layout(
            pending_frame_.frame_id, &layout);
        bool t4_state_valid = false;
        std::int32_t connection_state = 0;
        std::int32_t active_connection_state = 10;
        std::uint32_t key_catchers = 0;
#if defined(WAWVR_HAS_T4_BINDINGS)
        const T4PresentationState t4_state = read_t4_presentation_state();
        t4_state_valid = t4_state.valid;
        connection_state = t4_state.connection_state;
        active_connection_state = t4_state.active_connection_state;
        key_catchers = t4_state.key_catchers;
        manual_reload_observe_connection_state(
            t4_state_valid, connection_state, active_connection_state);
#endif
        const PresentationMode presentation_mode =
            classify_presentation_mode(
                t4_state_valid, connection_state, key_catchers,
                stereo_scene, active_connection_state);
        if (!presentation_state_log_valid_ ||
            t4_state_valid != last_logged_t4_state_valid_ ||
            connection_state != last_logged_connection_state_ ||
            active_connection_state !=
                last_logged_active_connection_state_ ||
            key_catchers != last_logged_key_catchers_ ||
            presentation_mode != last_logged_presentation_mode_) {
            LogFormatted(
                wawvr::xr::LogLevel::Info,
                "T4 presentation transition: stateValid=%u connection=%d active=%d keyCatchers=0x%X stereoScene=%u mode=%s",
                t4_state_valid ? 1u : 0u, connection_state,
                active_connection_state, key_catchers,
                stereo_scene ? 1u : 0u,
                presentation_mode_name(presentation_mode));
            presentation_state_log_valid_ = true;
            last_logged_t4_state_valid_ = t4_state_valid;
            last_logged_connection_state_ = connection_state;
            last_logged_active_connection_state_ =
                active_connection_state;
            last_logged_key_catchers_ = key_catchers;
            last_logged_presentation_mode_ = presentation_mode;
        }
        prepared_frame_.presentation_observed = true;
        prepared_frame_.observed_frame_id = pending_frame_.frame_id;
        prepared_frame_.observed_presentation_mode = presentation_mode;
        if (presentation_mode != PresentationMode::stereo) {
            layout = presentation_layout(
                presentation_mode, active_ui_source_,
                stereo_scene ? &layout : nullptr);
        }
        if (pending_frame_.should_render && pending_frame_.views_valid &&
            (stereo_scene ||
             presentation_mode != PresentationMode::stereo)) {
            // Multiple Presents may occur before the exact post-Com_Frame
            // consumer boundary. Relinquish the older GPU capture before
            // asking the ring for a replacement slot.
            DiscardPreparedSharedCapture(
                &prepared_frame_, "superseded before post-Com_Frame");
            PreparedCaptureKind capture_kind = PreparedCaptureKind::none;
            std::uint64_t capture_serial = 0;
            wawvr::xr::D3D9CpuCaptureResult capture_result =
                wawvr::xr::D3D9CpuCaptureResult::unavailable;
            const bool shared_capture_eligible =
                shared_capture_session_enabled_ &&
                reset_hook_installed_ &&
                d3d9ex_shared_bridge_ready(device);
            if (shared_capture_eligible) {
                const bool shared_fences_serviced =
                    shared_capture_.RetireCompletedFrames(
                        runtime_.d3d11_device(), runtime_.d3d11_context());
                if (shared_fences_serviced &&
                    shared_capture_.CaptureBackBuffer(
                        device, &pending_frame_, &capture_serial)) {
                    capture_kind = PreparedCaptureKind::shared_gpu;
                } else if (!shared_capture_.active()) {
                    capture_result =
                        capture_.CaptureBackBuffer(device, &pending_frame_);
                    if (capture_result ==
                        wawvr::xr::D3D9CpuCaptureResult::captured) {
                        capture_kind = PreparedCaptureKind::cpu;
                    }
                }
            } else {
                capture_result =
                    capture_.CaptureBackBuffer(device, &pending_frame_);
                if (capture_result ==
                    wawvr::xr::D3D9CpuCaptureResult::captured) {
                    capture_kind = PreparedCaptureKind::cpu;
                }
            }
            if (capture_kind != PreparedCaptureKind::none) {
                // Keep the newest successful target-swapchain image until the
                // main-loop boundary consumes it. This also lets frontend,
                // loading, and cinematic frames replace the prior projection.
                prepared_frame_.active = true;
                prepared_frame_.frame = pending_frame_;
                prepared_frame_.layout = layout;
                prepared_frame_.presentation_mode = presentation_mode;
                prepared_frame_.t4_state_valid = t4_state_valid;
                prepared_frame_.connection_state = connection_state;
                prepared_frame_.key_catchers = key_catchers;
                prepared_frame_.stereo_scene = stereo_scene;
                prepared_frame_.capture_kind = capture_kind;
                prepared_frame_.capture_serial = capture_serial;
                prepared_frame_.capture_result = capture_result;
#if defined(WAWVR_HAS_T4_BINDINGS)
                prepared_frame_.weapon_receipt = {};
                prepared_frame_.weapon_receipt_valid =
                    read_final_visible_weapon_receipt(
                        pending_frame_.frame_id,
                        &prepared_frame_.weapon_receipt);
#endif
                if (capture_kind == PreparedCaptureKind::shared_gpu &&
                    !logged_shared_capture_path_) {
                    const auto snapshot =
                        d3d9ex_bootstrap_runtime_snapshot();
                    LogFormatted(
                        wawvr::xr::LogLevel::Info,
                        "Active capture path: fenced D3D9Ex-to-D3D11 GPU shared textures (factoryEx=%u deviceEx=%u resetHook=%u textureHooks=%u convertedStaticTextures=%llu)",
                        snapshot.factory_ex_created ? 1u : 0u,
                        snapshot.device_ex_created ? 1u : 0u,
                        snapshot.reset_hook_installed ? 1u : 0u,
                        snapshot.texture_hooks_installed ? 1u : 0u,
                        static_cast<unsigned long long>(
                            snapshot.converted_static_textures));
                    logged_shared_capture_path_ = true;
                } else if (capture_kind == PreparedCaptureKind::cpu &&
                           !logged_cpu_capture_path_) {
                    Log(wawvr::xr::LogLevel::Info,
                        "Active capture path: synchronous D3D9 CPU readback/upload fallback");
                    logged_cpu_capture_path_ = true;
                }
            } else if (capture_result ==
                       wawvr::xr::D3D9CpuCaptureResult::device_lost) {
                device_loss_seen_since_boundary_ = true;
                if (bootstrap_reset_handler_registered_.load(
                        std::memory_order_acquire) &&
                    device ==
                        target_device_.load(std::memory_order_acquire)) {
                    ex_device_recovery_required_ = true;
                }
            }
        } else if (pending_frame_.should_render && pending_frame_.views_valid &&
                   !stereo_scene &&
                   presentation_mode == PresentationMode::stereo &&
                   !logged_missing_stereo_backend_) {
            Log(wawvr::xr::LogLevel::Warning,
                "Skipped fresh headset projection because the exact T4 backend did not confirm both eye draws; the last valid projection will be retained when available");
            logged_missing_stereo_backend_ = true;
        }
    }

    bool FinishPendingFrameAfterComFrame(
        const bool allow_fresh_projection,
        const bool d3d9_device_lost,
        const ULONGLONG now) {
        if (!pending_frame_active_) {
            DiscardPreparedSharedCapture(
                &prepared_frame_, "orphaned without an active OpenXR frame");
            prepared_frame_ = {};
            return runtime_.initialized();
        }
        const PreparedPendingFrame prepared = prepared_frame_;
        prepared_frame_ = {};
        // The OpenXR frame belongs to the post-Com_Frame boundary regardless
        // of whether this engine frame reached a capturable Present.
        const wawvr::xr::FrameState frame = pending_frame_;
        if (prepared.presentation_observed &&
            prepared.observed_frame_id == frame.frame_id) {
            DiscardIncompatibleQueuedSharedCaptures(
                prepared.observed_presentation_mode);
            const bool reset_comfort_anchor =
                presentation_transition_resets_comfort_anchor(
                    last_observed_presentation_mode_valid_,
                    last_observed_presentation_mode_,
                    prepared.observed_presentation_mode);
            if (reset_comfort_anchor) {
                const bool entering_mono =
                    prepared.observed_presentation_mode !=
                    PresentationMode::stereo;
                if (last_observed_presentation_mode_valid_) {
                    Log(wawvr::xr::LogLevel::Info,
                        entering_mono
                            ? "Observed stereo-to-mono transition; next finite panel capture will latch a fresh room anchor"
                            : "Observed mono-to-stereo transition; retired the finite panel anchor independently of capture availability");
                }
                comfort_pose_ = {};
                comfort_pose_valid_ = false;
                comfort_quad_ = {};
                comfort_quad_valid_ = false;

                // A projection and a finite quad are not interchangeable
                // recovery layers. Reusing the prior stereo projection after
                // entering a menu recreates a translation-infinite,
                // head-following screen; reusing a menu quad after gameplay
                // resumes leaves stale UI in the room. Retire the complete
                // prior interval and allow this frame to publish only a layer
                // constructed for the newly observed presentation class.
                last_projection_available_ = false;
                last_projection_eyes_ = {};
                last_quad_layer_ = {};
                last_submission_was_quad_ = false;
                visible_menu_quad_ = {};
                visible_menu_quad_valid_ = false;
#if defined(WAWVR_HAS_T4_BINDINGS)
                last_menu_surface_ = {};
                last_menu_surface_valid_ = false;
                visible_menu_surface_ = {};
                visible_menu_surface_valid_ = false;
#endif
            }
            last_observed_presentation_mode_ =
                prepared.observed_presentation_mode;
            last_observed_presentation_mode_valid_ = true;
        }
        wawvr::xr::D3D11SourceFrame source{};
        wawvr::xr::SharedFrameToken shared_token{};
        SharedFrameReleaseGuard shared_release{};
        bool rendered = false;
        std::uint32_t released_eye_mask = 0;
        wawvr::xr::QuadLayer current_quad{};
        bool current_quad_valid = false;
        wawvr::xr::CompositorReticle current_reticle{};
#if defined(WAWVR_HAS_T4_BINDINGS)
        MenuPointerSurface current_menu_surface{};
        bool current_menu_surface_valid = false;
#endif
        bool source_available = false;
        PreparedPendingFrame source_prepared{};
        const bool current_frame_accepts_render =
            current_frame_accepts_source_render(
                allow_fresh_projection, frame.should_render,
                frame.views_valid);
        const bool prepared_matches_submission =
            prepared.active &&
            prepared.frame.frame_id == frame.frame_id &&
            allow_fresh_projection;
        bool current_shared_queued = false;
        if (prepared_matches_submission &&
            prepared.capture_kind == PreparedCaptureKind::shared_gpu &&
            prepared.capture_serial != 0 &&
            shared_capture_session_enabled_) {
            current_shared_queued = QueuePreparedSharedCapture(prepared);
        } else if (
            prepared_matches_submission &&
            prepared.capture_kind == PreparedCaptureKind::cpu &&
            prepared.capture_result ==
                wawvr::xr::D3D9CpuCaptureResult::captured &&
            capture_.UploadLatest(
                runtime_.d3d11_device(), runtime_.d3d11_context(),
                &source)) {
            source_prepared = prepared;
            source_available = true;
        }
        if (prepared.capture_kind == PreparedCaptureKind::shared_gpu &&
            prepared.capture_serial != 0 && !current_shared_queued) {
            static_cast<void>(AbandonSharedCaptureSerial(
                prepared.capture_serial,
                "not eligible for deferred post-Com_Frame consumption"));
        }

        bool prefer_exact_current_shared_capture = false;
#if defined(WAWVR_HAS_T4_BINDINGS)
        // The runtime can timewarp the source HMD pose, but a tracked weapon is
        // already baked into these pixels. Prefer the exact current capture for
        // every live held weapon. The bridge submits its producer fence before
        // desktop Present. Exact acquisition has a bounded producer wait;
        // preserve this ownership policy and measure its actual wait cost.
        prefer_exact_current_shared_capture =
            should_prefer_exact_current_shared_capture(
                current_shared_queued, current_frame_accepts_render,
                prepared.presentation_mode, prepared.stereo_scene,
                prepared.weapon_receipt_valid,
                prepared.weapon_receipt.live_controller_pose);
#endif
        if (!source_available && prefer_exact_current_shared_capture) {
            const auto exact_capture_poll_started =
                std::chrono::steady_clock::now();
            const bool acquired = shared_capture_.AcquireSerial(
                runtime_.d3d11_device(), runtime_.d3d11_context(),
                prepared.capture_serial, &shared_token);
            const auto exact_capture_poll_finished =
                std::chrono::steady_clock::now();
            runtime_.RecordDiagnosticStage(frame.frame_id,
                wawvr::xr::FrameTimingStage::shared_acquire,
                exact_capture_poll_finished - exact_capture_poll_started);
            const auto exact_capture_poll_microseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    exact_capture_poll_finished -
                    exact_capture_poll_started).count();
            constexpr std::int64_t kNoteworthyExactCapturePollMicroseconds =
                1000;
            if (acquired &&
                exact_capture_poll_microseconds >
                    kNoteworthyExactCapturePollMicroseconds &&
                logged_slow_exact_capture_poll_count_ < 8) {
                LogFormatted(
                    wawvr::xr::LogLevel::Info,
                    "Live held-weapon exact-current capture poll took %lld us",
                    static_cast<long long>(exact_capture_poll_microseconds));
                ++logged_slow_exact_capture_poll_count_;
            }
            if (acquired) {
                last_consumed_shared_capture_serial_ =
                    shared_token.source.serial;
                if (TakeQueuedSharedCapture(
                        shared_token.source.serial, &source_prepared)) {
                    source = shared_token.source;
                    shared_release.Arm(
                        &shared_capture_, runtime_.d3d11_context(),
                        shared_token);
                    source_available = true;
                    if (!logged_exact_capture_success_) {
                        Log(wawvr::xr::LogLevel::Info,
                            "Live held weapon acquired the exact current D3D9Ex capture within the bounded producer wait");
                        logged_exact_capture_success_ = true;
                    }
                } else {
                    shared_capture_.Release(
                        runtime_.d3d11_context(), shared_token);
                    LogFormatted(
                        wawvr::xr::LogLevel::Error,
                        "Exact-current D3D9Ex serial %llu had no matching queued capture metadata; discarding the deferred generation",
                        static_cast<unsigned long long>(
                            shared_token.source.serial));
                    DiscardQueuedSharedCaptures(
                        "exact-current serial/metadata ordering violation");
                }
            } else if (!logged_exact_capture_fallback_) {
                LogFormatted(
                    wawvr::xr::LogLevel::Info,
                    "Live held-weapon exact-current capture was not ready after the %lld us bounded wait; attempting the newest completed capture without waiting",
                    static_cast<long long>(
                        exact_capture_poll_microseconds));
                logged_exact_capture_fallback_ = true;
            }
        }

        if (!source_available && current_frame_accepts_render &&
            shared_capture_session_enabled_ &&
            !queued_shared_captures_.empty()) {
            const bool acquired = [&]() {
                ScopedXrTiming acquire_timing(
                    runtime_, wawvr::xr::FrameTimingStage::shared_acquire);
                return shared_capture_.TryAcquireLatestReady(
                    runtime_.d3d11_device(), runtime_.d3d11_context(),
                    last_consumed_shared_capture_serial_, &shared_token);
            }();
            if (acquired) {
                last_consumed_shared_capture_serial_ =
                    shared_token.source.serial;
                if (TakeQueuedSharedCapture(
                        shared_token.source.serial, &source_prepared)) {
                    source = shared_token.source;
                    shared_release.Arm(
                        &shared_capture_, runtime_.d3d11_context(),
                        shared_token);
                    source_available = true;
                } else {
                    shared_capture_.Release(
                        runtime_.d3d11_context(), shared_token);
                    LogFormatted(
                        wawvr::xr::LogLevel::Error,
                        "D3D9Ex bridge completed serial %llu without matching queued capture metadata; discarding the deferred generation",
                        static_cast<unsigned long long>(
                            shared_token.source.serial));
                    DiscardQueuedSharedCaptures(
                        "deferred serial/metadata ordering violation");
                }
            }
        }
#if defined(WAWVR_HAS_T4_BINDINGS)
        if (source_available) {
            ObserveCaptureSubmissionDiagnostic(
                frame, source_prepared, source,
                queued_shared_captures_.size());
        }
#endif
        if (source_available && current_frame_accepts_render) {
            last_source_width_ = source.width;
            last_source_height_ = source.height;
            if (source_prepared.presentation_mode !=
                PresentationMode::stereo) {
                if (!comfort_pose_valid_) {
                    // Keep menu/cinematic screens upright even if the HMD is
                    // briefly rolled or resting at an angle on entry. Yaw and
                    // position still determine where the screen is anchored.
                    comfort_pose_ = leveled_tracking_anchor(
                        frame.head_center,
                        tracking_anchor_valid_
                            ? &tracking_anchor_.orientation
                            : nullptr);
                    comfort_pose_valid_ = true;
                    comfort_quad_valid_ = build_world_menu_panel(
                        comfort_pose_, &comfort_quad_);
                    if (comfort_quad_valid_) {
                        LogFormatted(
                            wawvr::xr::LogLevel::Info,
                            "Gravity-level room-anchored VR panel latched on entry: mode=%s UI-source=%s distance=%.2fm size=%.3fx%.3fm",
                            presentation_mode_name(
                                source_prepared.presentation_mode),
                            active_ui_source_name(active_ui_source_),
                            kMenuPanelDistanceMeters,
                            comfort_quad_.size_meters.x,
                            comfort_quad_.size_meters.y);
                    } else {
                        Log(wawvr::xr::LogLevel::Error,
                            "Could not construct a finite menu panel from the valid comfort anchor; withholding mono composition for this frame");
                    }
                } else if (!comfort_quad_valid_) {
                    comfort_quad_valid_ = build_world_menu_panel(
                        comfort_pose_, &comfort_quad_);
                }
                const auto comfort_views =
                    monoscopic_comfort_views(comfort_pose_);
                source.rendered_views_valid = true;
                for (std::uint32_t eye = 0;
                     eye < wawvr::xr::kEyeCount; ++eye) {
                    source.rendered_eyes[eye] = comfort_views[eye];
                }
                if (comfort_quad_valid_) {
                    current_quad = comfort_quad_;
                    current_quad_valid = true;
                }
            } else {
                if (comfort_pose_valid_) {
                    Log(wawvr::xr::LogLevel::Info,
                        "Exited mono comfort screen; restored capture-time stereo eye poses");
                }
                comfort_pose_valid_ = false;
                comfort_quad_ = {};
                comfort_quad_valid_ = false;
            }
#if defined(WAWVR_HAS_T4_BINDINGS)
            if (current_quad_valid && source_prepared.t4_state_valid) {
                current_menu_surface_valid = build_menu_pointer_surface(
                    current_quad, source_prepared.layout, source.width,
                    source.height, source_prepared.connection_state,
                    source_prepared.key_catchers, &current_menu_surface);
            }
            if (current_menu_surface_valid && frame.actions.focused) {
                const auto& right = frame.actions.hands[
                    static_cast<std::uint32_t>(wawvr::xr::Hand::Right)];
                const MenuPointerHit raw_hit = point_at_world_menu_panel(
                    right.aim, current_menu_surface.panel);
                if (raw_hit.valid && remap_menu_pointer_to_content(
                        raw_hit,
                        current_menu_surface.content_viewport).valid) {
                    current_reticle = {
                        .visible = true,
                        .target_eye = current_menu_surface.panel.source_eye,
                        .u = raw_hit.u,
                        .v = raw_hit.v,
                    };
                }
            }
#endif
            // Scope constants and source fitting belong to the pose/FOV that
            // produced these pixels. OpenXR EndFrame below still receives the
            // current live frame and display time; the source carries its own
            // immutable rendered-eye poses for runtime reprojection.
            rendered = compositor_.RenderStereo(
                runtime_, source_prepared.frame, source,
                source_prepared.layout,
                current_reticle.visible ? &current_reticle : nullptr,
                &released_eye_mask,
                wawvr::xr::CompositorEffects{
                    .enable_eye_local_bloom =
                        should_enable_eye_local_bloom(
                            source_prepared.presentation_mode,
                            environment_enables_eye_local_bloom()),
                });
        }
        // The source texture is no longer sampled after RenderStereo returns.
        // Retire the shared slot before xrEndFrame or any recovery path can
        // reset/shut down the D3D devices.
        {
            ScopedXrTiming release_timing(
                runtime_, wawvr::xr::FrameTimingStage::shared_release);
            shared_release.Release();
        }

        if (!reusable_layer_intact_after_render_attempt(
                rendered, released_eye_mask)) {
            // At least one swapchain now contains the failed attempt while
            // another may still contain the prior frame. Old pose/layer
            // metadata would submit a visually corrupt hybrid, so sacrifice
            // one recovery frame and wait for a complete fresh render.
            last_projection_available_ = false;
            last_projection_eyes_ = {};
            last_quad_layer_ = {};
            last_submission_was_quad_ = false;
            visible_menu_quad_ = {};
            visible_menu_quad_valid_ = false;
#if defined(WAWVR_HAS_T4_BINDINGS)
            last_menu_surface_ = {};
            last_menu_surface_valid_ = false;
            visible_menu_surface_ = {};
            visible_menu_surface_valid_ = false;
#endif
            if (!logged_partial_compositor_overwrite_) {
                LogFormatted(
                    wawvr::xr::LogLevel::Warning,
                    "Invalidated reusable OpenXR layer after an incomplete compositor update (released-eye-mask=0x%X)",
                    released_eye_mask);
                logged_partial_compositor_overwrite_ = true;
            }
        }

        const bool capture_device_lost =
            prepared.capture_result ==
            wawvr::xr::D3D9CpuCaptureResult::device_lost;
        const ProjectionRecoveryPlan recovery = plan_projection_recovery(
            rendered,
            last_projection_available_,
            d3d9_device_lost || capture_device_lost);
        wawvr::xr::D3D11SourceFrame previous_projection{};
        const wawvr::xr::QuadLayer* submitted_quad = nullptr;
        const bool mono_presentation =
            last_observed_presentation_mode_valid_ &&
            last_observed_presentation_mode_ != PresentationMode::stereo;
        const bool use_virtual_desktop_ui_projection_fallback =
            last_observed_presentation_mode_valid_ &&
            should_use_virtual_desktop_ui_projection_fallback(
                last_observed_presentation_mode_,
                runtime_.active_runtime_name(),
                environment_disables_virtual_desktop_ui_projection_fallback());
        if (use_virtual_desktop_ui_projection_fallback &&
            !logged_virtual_desktop_ui_projection_fallback_) {
            Log(wawvr::xr::LogLevel::Warning,
                "VirtualDesktopXR active-UI cadence fallback enabled: submitting the current pause surface as projection instead of a quad; set WAWVR_DISABLE_VD_UI_PROJECTION_FALLBACK=1 to restore quad presentation");
            logged_virtual_desktop_ui_projection_fallback_ = true;
        }
        const wawvr::xr::CompositionLayerKind requested_layer_kind =
            select_composition_layer_kind(
                recovery.submission, current_quad_valid,
                last_submission_was_quad_, mono_presentation,
                use_virtual_desktop_ui_projection_fallback);
        wawvr::xr::CompositionLayerSubmission layer_submission{
            .kind = requested_layer_kind,
        };
        if (requested_layer_kind ==
            wawvr::xr::CompositionLayerKind::quad) {
            submitted_quad =
                recovery.submission == ProjectionSubmission::current
                    ? &current_quad
                    : &last_quad_layer_;
            layer_submission.quad = submitted_quad;
        } else if (requested_layer_kind ==
                   wawvr::xr::CompositionLayerKind::projection) {
            if (recovery.submission == ProjectionSubmission::previous) {
                previous_projection = PreviousProjectionSource();
                layer_submission.projection_source = &previous_projection;
            } else {
                layer_submission.projection_source = &source;
            }
        }
        if (runtime_.timing_diagnostics_enabled()) {
            if (recovery.submission == ProjectionSubmission::current) {
                runtime_.RecordDiagnosticSource(frame.frame_id,
                    source_prepared.frame.frame_id, source.serial);
            } else if (recovery.submission == ProjectionSubmission::previous) {
                runtime_.RecordDiagnosticSource(frame.frame_id,
                    timing_last_source_frame_id_, timing_last_source_serial_);
            }
            if (last_observed_presentation_mode_valid_) {
                runtime_.RecordDiagnosticPresentation(frame.frame_id,
                    last_observed_presentation_mode_ == PresentationMode::stereo,
                    recovery.submission == ProjectionSubmission::previous);
            }
        }
        const wawvr::xr::EndFrameResult end_frame = runtime_.EndFrame(
            frame, layer_submission);
        const bool ended = end_frame.frame_ended;
        const bool accepted_current =
            ended &&
            recovery.submission == ProjectionSubmission::current &&
            end_frame.accepted_kind !=
                wawvr::xr::CompositionLayerKind::none;
        const bool accepted_previous =
            ended &&
            recovery.submission == ProjectionSubmission::previous &&
            end_frame.accepted_kind !=
                wawvr::xr::CompositionLayerKind::none;
        const bool current_quad_accepted =
            accepted_current &&
            end_frame.accepted_kind ==
                wawvr::xr::CompositionLayerKind::quad;
        if (!logged_layer_kind_valid_ ||
             requested_layer_kind != last_logged_requested_layer_kind_ ||
             end_frame.accepted_kind != last_logged_accepted_layer_kind_) {
            if (requested_layer_kind ==
                    wawvr::xr::CompositionLayerKind::quad &&
                submitted_quad != nullptr) {
                LogFormatted(
                    end_frame.accepted_kind == requested_layer_kind
                        ? wawvr::xr::LogLevel::Info
                        : wawvr::xr::LogLevel::Warning,
                    "OpenXR layer requested=quad accepted=%s LOCAL-pose=(%.3f, %.3f, %.3f | %.4f, %.4f, %.4f, %.4f) size=%.3fx%.3fm",
                    composition_layer_kind_name(end_frame.accepted_kind),
                    submitted_quad->pose.position.x,
                    submitted_quad->pose.position.y,
                    submitted_quad->pose.position.z,
                    submitted_quad->pose.orientation.x,
                    submitted_quad->pose.orientation.y,
                    submitted_quad->pose.orientation.z,
                    submitted_quad->pose.orientation.w,
                    submitted_quad->size_meters.x,
                    submitted_quad->size_meters.y);
            } else {
                LogFormatted(
                    requested_layer_kind == end_frame.accepted_kind
                        ? wawvr::xr::LogLevel::Info
                        : wawvr::xr::LogLevel::Warning,
                    "OpenXR layer requested=%s accepted=%s",
                    composition_layer_kind_name(requested_layer_kind),
                    composition_layer_kind_name(end_frame.accepted_kind));
            }
            last_logged_requested_layer_kind_ = requested_layer_kind;
            last_logged_accepted_layer_kind_ = end_frame.accepted_kind;
            logged_layer_kind_valid_ = true;
        }
        pending_frame_ = {};
        pending_frame_active_ = false;
        clear_pending_stereo_frame();

        if (!reusable_layer_intact_after_frame_end(
                rendered, accepted_current)) {
            // The compositor has already replaced/released both swapchain
            // images, so cached metadata for the older images is no longer a
            // valid recovery layer even if xrEndFrame rejected this frame.
            last_projection_available_ = false;
            last_projection_eyes_ = {};
            last_quad_layer_ = {};
            last_submission_was_quad_ = false;
            visible_menu_quad_ = {};
            visible_menu_quad_valid_ = false;
#if defined(WAWVR_HAS_T4_BINDINGS)
            last_menu_surface_ = {};
            last_menu_surface_valid_ = false;
            visible_menu_surface_ = {};
            visible_menu_surface_valid_ = false;
#endif
            Log(wawvr::xr::LogLevel::Error,
                "Invalidated all reusable OpenXR layers after a completed compositor render failed frame submission");
        }

        if (accepted_current) {
            if (runtime_.timing_diagnostics_enabled()) {
                timing_last_source_frame_id_ = source_prepared.frame.frame_id;
                timing_last_source_serial_ = source.serial;
            }
            logged_partial_compositor_overwrite_ = false;
            last_projection_available_ = true;
            last_submission_was_quad_ = current_quad_accepted;
            if (current_quad_accepted) {
                last_quad_layer_ = current_quad;
                visible_menu_quad_ = current_quad;
                visible_menu_quad_valid_ = true;
            } else {
                last_quad_layer_ = {};
                visible_menu_quad_ = {};
                visible_menu_quad_valid_ = false;
            }
#if defined(WAWVR_HAS_T4_BINDINGS)
            if (current_quad_accepted && current_menu_surface_valid) {
                last_menu_surface_ = current_menu_surface;
                last_menu_surface_valid_ = true;
                visible_menu_surface_ = current_menu_surface;
                visible_menu_surface_valid_ = true;
            } else {
                last_menu_surface_ = {};
                last_menu_surface_valid_ = false;
                visible_menu_surface_ = {};
                visible_menu_surface_valid_ = false;
            }
#endif
            for (std::uint32_t eye = 0; eye < wawvr::xr::kEyeCount; ++eye) {
                last_projection_eyes_[eye] =
                    source.rendered_views_valid
                        ? source.rendered_eyes[eye]
                        : frame.eyes[eye];
            }
            if (using_recovery_projection_) {
                if (now >= next_recovery_transition_log_) {
                    Log(wawvr::xr::LogLevel::Info,
                        "Fresh D3D9 capture resumed; stopped reusing the recovery projection");
                    next_recovery_transition_log_ =
                        now + kRecoveryTransitionLogMilliseconds;
                }
                using_recovery_projection_ = false;
            }
        } else if (accepted_previous) {
            const bool previous_quad_accepted =
                end_frame.accepted_kind ==
                wawvr::xr::CompositionLayerKind::quad;
            if (previous_quad_accepted && last_submission_was_quad_) {
                visible_menu_quad_ = last_quad_layer_;
                visible_menu_quad_valid_ = true;
            } else {
                visible_menu_quad_ = {};
                visible_menu_quad_valid_ = false;
            }
#if defined(WAWVR_HAS_T4_BINDINGS)
            if (previous_quad_accepted && last_submission_was_quad_ &&
                last_menu_surface_valid_) {
                visible_menu_surface_ = last_menu_surface_;
                visible_menu_surface_valid_ = true;
            } else {
                visible_menu_surface_ = {};
                visible_menu_surface_valid_ = false;
            }
#endif
            if (!using_recovery_projection_ &&
                now >= next_recovery_transition_log_) {
                Log(wawvr::xr::LogLevel::Warning,
                    "D3D9 frame unavailable; reusing the last released OpenXR composition layer instead of submitting a loading frame");
                next_recovery_transition_log_ =
                    now + kRecoveryTransitionLogMilliseconds;
            }
            using_recovery_projection_ = true;
        } else if (ended) {
            visible_menu_quad_ = {};
            visible_menu_quad_valid_ = false;
#if defined(WAWVR_HAS_T4_BINDINGS)
            visible_menu_surface_ = {};
            visible_menu_surface_valid_ = false;
#endif
        }

        if (recovery.submission == ProjectionSubmission::previous &&
            !using_recovery_projection_) {
            if (now >= next_recovery_transition_log_) {
                Log(wawvr::xr::LogLevel::Warning,
                    "D3D9 frame unavailable; waiting to reuse the last released OpenXR composition layer");
                next_recovery_transition_log_ =
                    now + kRecoveryTransitionLogMilliseconds;
            }
        }

        if (accepted_current && source_prepared.stereo_scene &&
            end_frame.accepted_kind ==
                wawvr::xr::CompositionLayerKind::projection &&
            source_prepared.presentation_mode == PresentationMode::stereo &&
            !logged_first_stereo_submission_) {
            Log(wawvr::xr::LogLevel::Info,
                "Submitted first pose-matched queued T4 side-by-side stereo image with asymmetric OpenXR viewport remap");
            logged_first_stereo_submission_ = true;
            logged_missing_stereo_backend_ = false;
        }
        if (accepted_current && source_prepared.stereo_scene &&
            end_frame.accepted_kind ==
                wawvr::xr::CompositionLayerKind::projection &&
            source_prepared.presentation_mode == PresentationMode::stereo &&
            !automatic_gameplay_recenter_completed_ &&
            !explicit_recenter_captured_) {
            // The first valid HMD pose normally arrives while Nacht is still
            // loading.  The player may put on or level the headset after that,
            // which
            // leaves the playable camera translated or rolled until a manual
            // recenter.  The first confirmed gameplay stereo frame is the
            // earliest reliable signal that the player view actually exists;
            // capture the following valid pose as the real startup centre.
            automatic_gameplay_recenter_pending_ = true;
        }
        if (recovery.abandon_xr) {
            ShutdownXr(
                "D3D9 device was lost before any reusable XR projection existed");
            next_runtime_attempt_ = now + kRuntimeRetryMilliseconds;
            return false;
        }
        if (!ended && runtime_.exit_requested()) {
            ShutdownXr("OpenXR frame submission failed fatally");
            next_runtime_attempt_ = now + kRuntimeRetryMilliseconds;
            return false;
        }
        return runtime_.initialized();
    }

    void RecordPresentResult(const HRESULT present_result) {
        present_seen_since_boundary_ = true;
        latest_present_result_ = present_result;
        if (d3d9ex_result_requires_recovery(present_result)) {
            device_loss_seen_since_boundary_ = true;
            if (bootstrap_reset_handler_registered_.load(
                    std::memory_order_acquire)) {
                ex_device_recovery_required_ = true;
            }
            capture_.NotifyExternalDeviceLoss();
        }
    }

    void ServiceAfterComFrame() {
        const ULONGLONG now = GetTickCount64();
        if (reset_in_progress_.load(std::memory_order_acquire)) {
            return;
        }
        if (!PublishedTargetMatchesEngine()) {
            PauseFrameServiceForTargetRefresh();
            return;
        }
        if (!stereo_scene_hook_installed() &&
            !scene_hook_permanently_disabled_ &&
            now >= next_scene_hook_attempt_) {
            TryInstallStereoSceneHook(now);
        }
        IDirect3DDevice9* const device =
            target_device_.load(std::memory_order_acquire);
        const bool bootstrap_ex_route =
            bootstrap_reset_handler_registered_.load(
                std::memory_order_acquire) &&
            device != nullptr;
        const bool ex_reset_failed =
            ex_reset_failed_.load(std::memory_order_acquire);
        HRESULT cooperative_result = D3D_OK;
        bool used_check_device_state = false;
        const bool ex_state_probe_required =
            bootstrap_ex_route &&
            (ex_reset_failed || ex_device_recovery_required_ ||
             device_loss_seen_since_boundary_ ||
             (present_seen_since_boundary_ &&
              latest_present_result_ != D3D_OK));
        if (ex_state_probe_required) {
            cooperative_result = static_cast<HRESULT>(
                check_d3d9ex_device_state(
                    device,
                    ex_focus_window_.load(std::memory_order_acquire)));
            used_check_device_state = true;
            if (d3d9ex_result_requires_recovery(cooperative_result)) {
                ex_device_recovery_required_ = true;
            }
        }
        if (device != nullptr && !bootstrap_ex_route) {
            cooperative_result = device->TestCooperativeLevel();
        }
        const bool present_seen = present_seen_since_boundary_;
        const HRESULT present_result = latest_present_result_;
        const bool present_succeeded =
            present_seen &&
            (bootstrap_ex_route
                 ? present_result == D3D_OK
                 : SUCCEEDED(present_result));
        const bool device_cooperative = bootstrap_ex_route
            ? !ex_reset_failed && !ex_device_recovery_required_ &&
                  cooperative_result == D3D_OK
            : SUCCEEDED(cooperative_result);
        const bool mode_changed =
            cooperative_result == S_PRESENT_MODE_CHANGED;
        const bool occluded =
            cooperative_result == S_PRESENT_OCCLUDED;
        const bool device_lost =
            device_loss_seen_since_boundary_ ||
            ex_reset_failed || ex_device_recovery_required_ || mode_changed ||
            (!device_cooperative && !occluded) ||
            present_result == D3DERR_DEVICELOST ||
            present_result == D3DERR_DEVICENOTRESET ||
            present_result == D3DERR_DEVICEHUNG ||
            present_result == D3DERR_DEVICEREMOVED ||
            cooperative_result == D3DERR_DEVICELOST ||
            cooperative_result == D3DERR_DEVICENOTRESET ||
            cooperative_result == D3DERR_DEVICEHUNG ||
            cooperative_result == D3DERR_DEVICEREMOVED;
        const PostComFramePlan service_plan =
            plan_post_com_frame_service(
                present_seen, present_succeeded, device_cooperative,
                last_projection_available_, false);

        if (FAILED(cooperative_result)) {
            capture_.NotifyExternalDeviceLoss();
        }

        if ((present_seen && !present_succeeded) || !device_cooperative) {
            if (!logged_present_failure_ ||
                present_result != last_present_result_ ||
                cooperative_result != last_cooperative_result_) {
                LogFormatted(
                    wawvr::xr::LogLevel::Warning,
                    "D3D9 frame-boundary state unavailable: Present=%s0x%08lx %s=0x%08lx; fresh capture paused",
                    present_seen ? "" : "not-seen/",
                    present_seen ? present_result : D3D_OK,
                    used_check_device_state
                        ? "CheckDeviceState"
                        : "TestCooperativeLevel",
                    cooperative_result);
                logged_present_failure_ = true;
                last_present_result_ = present_result;
                last_cooperative_result_ = cooperative_result;
            }
        } else if (logged_present_failure_) {
            Log(wawvr::xr::LogLevel::Info,
                "D3D9 Present and cooperative state recovered; XR priming resumed");
            logged_present_failure_ = false;
        }

        if (pending_frame_active_ &&
            !FinishPendingFrameAfterComFrame(
                service_plan.allow_fresh_projection &&
                    !device_loss_seen_since_boundary_ &&
                    !reset_seen_since_boundary_,
                device_lost,
                now)) {
            ResetBoundaryObservations();
            return;
        }

        if (device_lost && !last_projection_available_ &&
            runtime_.initialized() && !pending_frame_active_) {
            ShutdownXr(
                "D3D9 remained lost before the first reusable XR projection");
            next_runtime_attempt_ = now + kRuntimeRetryMilliseconds;
            ResetBoundaryObservations();
            return;
        }

        ResetBoundaryObservations();

        if (!runtime_.initialized() && device_cooperative &&
            now >= next_runtime_attempt_) {
            InitializeXr(now);
        }

        if (runtime_.initialized() && service_plan.prime_next_frame) {
            PrimeNextFrame(now);
        } else if (runtime_.initialized()) {
            clear_pending_stereo_frame();
            clear_controller_frame();
#if defined(WAWVR_HAS_T4_BINDINGS)
            service_firing_haptics(runtime_, nullptr);
#endif
            if (!runtime_.PollEvents() && runtime_.exit_requested()) {
                ShutdownXr("OpenXR runtime requested exit during D3D9 recovery");
                next_runtime_attempt_ = now + kRuntimeRetryMilliseconds;
            }
        }
    }

    void ResetBoundaryObservations() noexcept {
        present_seen_since_boundary_ = false;
        device_loss_seen_since_boundary_ = false;
        reset_seen_since_boundary_ = false;
        latest_present_result_ = D3D_OK;
        latest_reset_result_ = D3D_OK;
    }

    wawvr::xr::D3D11SourceFrame PreviousProjectionSource() const noexcept {
        wawvr::xr::D3D11SourceFrame source{};
        source.rendered_views_valid = last_projection_available_;
        if (last_projection_available_) {
            for (std::uint32_t eye = 0; eye < wawvr::xr::kEyeCount; ++eye) {
                source.rendered_eyes[eye] = last_projection_eyes_[eye];
            }
        }
        return source;
    }

    bool PrimeNextFrame(const ULONGLONG now) {
        DiscardPreparedSharedCapture(
            &prepared_frame_, "discarded while priming the next OpenXR frame");
        prepared_frame_ = {};
        if (!runtime_.PollEvents()) {
            clear_pending_stereo_frame();
            clear_controller_frame();
#if defined(WAWVR_HAS_T4_BINDINGS)
            service_firing_haptics(runtime_, nullptr);
#endif
            if (runtime_.exit_requested()) {
                ShutdownXr("OpenXR runtime requested exit");
                next_runtime_attempt_ = now + kRuntimeRetryMilliseconds;
            }
            return false;
        }

        wawvr::xr::FrameState next{};
        if (!runtime_.BeginFrame(&next)) {
            clear_pending_stereo_frame();
            clear_controller_frame();
#if defined(WAWVR_HAS_T4_BINDINGS)
            service_firing_haptics(runtime_, nullptr);
#endif
            return false;
        }
        pending_frame_ = next;
        pending_frame_active_ = true;

        wawvr::xr::Quaternionf body_aligned_anchor_orientation{};
        const bool body_anchor_rebase_pending =
            consume_controller_tracking_anchor_rebase(
                &body_aligned_anchor_orientation);
        if (tracking_anchor_valid_ && body_anchor_rebase_pending) {
            // The game thread transferred the prior frame's physical HMD yaw
            // into T4's native body. Apply the identical yaw-only rebase to
            // Present's long-lived anchor before publishing this prediction;
            // position remains owned exclusively by explicit recenter.
            tracking_anchor_.orientation =
                body_aligned_anchor_orientation;
        }

        const auto& menu_fallback = next.actions.hands[
            static_cast<std::uint32_t>(wawvr::xr::Hand::Right)].secondary;
        const MenuButtonSourceSelection menu_button =
            select_menu_button_source(
                next.actions.menu.active,
                next.actions.menu.current,
                menu_fallback.active,
                menu_fallback.current);
        const MenuButtonGestureUpdate menu_gesture =
            update_menu_button_gesture(
            menu_button.pressed,
            next.views_valid,
            now,
            kControllerMenuHoldMilliseconds,
            &menu_button_gesture_state_);
        if (menu_gesture.hold_started) {
            LogFormatted(
                wawvr::xr::LogLevel::Info,
                menu_button.using_secondary_fallback
                    ? "OpenXR Menu action unavailable; B fallback gesture started; release before %llu ms for Escape or keep holding to recenter"
                    : "Left-menu gesture started; release before %llu ms for Escape or keep holding to recenter",
                static_cast<unsigned long long>(
                    kControllerMenuHoldMilliseconds));
        }
        if (menu_gesture.waiting_for_valid_pose) {
            Log(wawvr::xr::LogLevel::Info,
                "Recenter hold complete; preserving timer while awaiting the next valid HMD pose");
        }
        if (automatic_gameplay_recenter_pending_ && next.views_valid) {
            // An automatic capture must never redefine physical pitch/roll:
            // the player may happen to glance up or down on this frame. Keep
            // gravity level and refresh only position + yaw. The deliberate
            // one-second Menu hold below does the same immediately on demand.
            const wawvr::xr::Quaternionf* const prior_yaw =
                tracking_anchor_valid_
                    ? &tracking_anchor_.orientation
                    : nullptr;
            tracking_anchor_ = leveled_tracking_anchor(
                next.head_center, prior_yaw);
            tracking_anchor_valid_ = true;
            automatic_gameplay_recenter_pending_ = false;
            automatic_gameplay_recenter_completed_ = true;
            LogFormatted(
                wawvr::xr::LogLevel::Info,
                "Automatic first-gameplay-frame recenter refreshed position + yaw with a gravity-level anchor: position=(%.3f, %.3f, %.3f) orientation=(%.4f, %.4f, %.4f, %.4f)",
                tracking_anchor_.position.x,
                tracking_anchor_.position.y,
                tracking_anchor_.position.z,
                tracking_anchor_.orientation.x,
                tracking_anchor_.orientation.y,
                tracking_anchor_.orientation.z,
                tracking_anchor_.orientation.w);
        }
        if (menu_gesture.recenter_capture_requested) {
            // A manual recenter must always restore a gravity-level horizon.
            // Capturing the complete current quaternion made a tilted HMD
            // pose the new permanent reference, which is exactly the wrong
            // behavior when recenter is invoked to correct tilt.
            const wawvr::xr::Quaternionf* const prior_yaw =
                tracking_anchor_valid_
                    ? &tracking_anchor_.orientation
                    : nullptr;
            tracking_anchor_ = leveled_tracking_anchor(
                next.head_center, prior_yaw);
            tracking_anchor_valid_ = true;
            automatic_gameplay_recenter_pending_ = false;
            automatic_gameplay_recenter_completed_ = true;
            explicit_recenter_captured_ = true;
            if (last_projection_available_ && last_submission_was_quad_) {
                // A static menu may not issue another Present. Reposition the
                // already released texture immediately so recenter cannot
                // resurrect the old world-space panel pose through recovery.
                comfort_pose_ = tracking_anchor_;
                comfort_pose_valid_ = true;
                comfort_quad_valid_ = build_world_menu_panel(
                    comfort_pose_, &comfort_quad_);
                if (comfort_quad_valid_) {
                    // Rebase only the metadata that the next recovery
                    // xrEndFrame will submit. The currently visible layer is
                    // still at its old pose, so its ray surface must remain
                    // unchanged until that EndFrame succeeds and promotes
                    // the rebased descriptor below.
                    last_quad_layer_ = comfort_quad_;
#if defined(WAWVR_HAS_T4_BINDINGS)
                    if (last_menu_surface_valid_) {
                        last_menu_surface_.panel = comfort_quad_;
                    }
#endif
                } else {
                    last_projection_available_ = false;
                    last_submission_was_quad_ = false;
                    last_quad_layer_ = {};
                    visible_menu_quad_ = {};
                    visible_menu_quad_valid_ = false;
#if defined(WAWVR_HAS_T4_BINDINGS)
                    last_menu_surface_ = {};
                    last_menu_surface_valid_ = false;
                    visible_menu_surface_ = {};
                    visible_menu_surface_valid_ = false;
#endif
                }
            } else {
                comfort_pose_valid_ = false;
                comfort_quad_ = {};
                comfort_quad_valid_ = false;
                visible_menu_quad_ = {};
                visible_menu_quad_valid_ = false;
#if defined(WAWVR_HAS_T4_BINDINGS)
                visible_menu_surface_ = {};
                visible_menu_surface_valid_ = false;
#endif
            }
            LogFormatted(
                wawvr::xr::LogLevel::Info,
                "Manual recenter captured position + yaw with a gravity-level anchor: position=(%.3f, %.3f, %.3f) orientation=(%.4f, %.4f, %.4f, %.4f); horizon is level and current translation/yaw are zeroed",
                tracking_anchor_.position.x,
                tracking_anchor_.position.y,
                tracking_anchor_.position.z,
                tracking_anchor_.orientation.x,
                tracking_anchor_.orientation.y,
                tracking_anchor_.orientation.z,
                tracking_anchor_.orientation.w);
        }

#if defined(WAWVR_HAS_T4_BINDINGS)
        const T4MenuInputServiceResult menu_input =
            service_t4_menu_input_after_com_frame(
                next,
                last_source_width_,
                last_source_height_,
                active_ui_source_,
                visible_menu_surface_valid_
                    ? &visible_menu_surface_
                    : nullptr,
                menu_gesture.short_tap_released,
                now,
                &menu_input_state_);
        if (menu_input.campaign_unlock_queued) {
            Log(wawvr::xr::LogLevel::Info,
                "Campaign Mission Select unlock command queued after profile initialization: seta mis_01 50");
        }
        service_manual_reload_simulator_asset_inventory();
        if (menu_input.cursor_submitted &&
            !logged_first_native_menu_cursor_) {
            Log(wawvr::xr::LogLevel::Info,
                "Native T4 UI cursor path is active: right-controller ray + trigger, with left stick + A fallback");
            logged_first_native_menu_cursor_ = true;
        }
        if (menu_input.pointer_submitted &&
            !logged_first_controller_menu_ray_) {
            Log(wawvr::xr::LogLevel::Info,
                "Right OpenXR aim ray intersected the finite menu panel and moved the native T4 cursor");
            logged_first_controller_menu_ray_ = true;
        }
        if (menu_input.menu_button_tapped) {
            Log(wawvr::xr::LogLevel::Info,
                "Left-menu short release dispatched a balanced native Escape tap");
        }
        if (menu_input.confirm_tapped) {
            if (menu_input.confirm_via_enter) {
                Log(wawvr::xr::LogLevel::Info,
                    menu_input.pointer_confirm_tapped
                        ? "VR trigger dispatched a balanced native Enter tap at the pointed in-match MP menu item"
                        : "VR A dispatched a balanced native Enter tap in the active MP menu");
            } else {
                Log(wawvr::xr::LogLevel::Info,
                    menu_input.pointer_confirm_tapped
                        ? "VR trigger dispatched a balanced native Mouse1 tap at the pointed menu item"
                        : "VR A dispatched a balanced native Mouse1 menu tap");
            }
        }
        if (menu_input.back_tapped) {
            Log(wawvr::xr::LogLevel::Info,
                "VR B dispatched a balanced native Escape menu tap");
        }
        if (menu_input.pezbot_autofill_queued) {
            Log(wawvr::xr::LogLevel::Info,
                "PeZBOT autofill armed nine bots for the next MP map/mode lifecycle");
        }
        if (menu_ui_action_invalidates_pointer_surface(
                menu_input.menu_button_tapped,
                menu_input.confirm_tapped,
                menu_input.back_tapped)) {
            // A native UI action may synchronously replace the page while its
            // connection state and key catchers remain unchanged. Deferred
            // mono pixels from the old page must never be paired with the new
            // page's hit targets.
            DiscardQueuedSharedCaptures(
                "native menu action invalidated deferred mono capture");
            // T4 can switch pages synchronously while leaving connection
            // state and key catchers unchanged. Keep displaying the last
            // released quad during recovery, but do not let any controller
            // target that stale page. A fresh successful UI submission will
            // publish a new exact descriptor in FinishPendingFrame.
            last_menu_surface_ = {};
            last_menu_surface_valid_ = false;
            visible_menu_surface_ = {};
            visible_menu_surface_valid_ = false;
        }
#endif

#if defined(WAWVR_HAS_T4_BINDINGS)
        service_firing_haptics(runtime_, &next);
#endif
        if (next.should_render && next.views_valid) {
            if (!tracking_anchor_valid_) {
                tracking_anchor_ = leveled_tracking_anchor(next.head_center);
                tracking_anchor_valid_ = true;
                Log(wawvr::xr::LogLevel::Info,
                    "Captured first valid OpenXR head centre with yaw-only horizon-level tracking anchor");
            }
            {
                TrackingAnchorSyncLock anchor_publication(
                    tracking_anchor_sync_mutex());
                publish_pending_stereo_frame(next, tracking_anchor_);
                publish_controller_frame(next, tracking_anchor_);
            }
            if (!logged_first_primed_frame_) {
                LogFormatted(
                    wawvr::xr::LogLevel::Info,
                    "Primed OpenXR frame %llu for the following T4 simulation/render",
                    static_cast<unsigned long long>(next.frame_id));
                logged_first_primed_frame_ = true;
            }
        } else {
            TrackingAnchorSyncLock anchor_publication(
                tracking_anchor_sync_mutex());
            clear_pending_stereo_frame();
            clear_controller_frame();
        }
        return true;
    }

    void TryInstallStereoSceneHook(const ULONGLONG now) noexcept {
        const auto result = install_stereo_scene_hook();
        if (result == StereoSceneHookResult::installed ||
            result == StereoSceneHookResult::already_installed) {
            const auto* const profile = configured_present_profile();
            LogFormatted(
                wawvr::xr::LogLevel::Info,
                "%s exact gameplay scene call hook active",
                profile != nullptr ? profile->name : "Configured T4");
            return;
        }

        LogFormatted(
            wawvr::xr::LogLevel::Warning,
            "T4 gameplay stereo hook not installed: %s; mono XR remains active",
            describe_stereo_scene_hook_result(result));
        if (result == StereoSceneHookResult::profile_mismatch ||
            result == StereoSceneHookResult::target_mismatch) {
            scene_hook_permanently_disabled_ = true;
        } else {
            next_scene_hook_attempt_ = now + kInstallRetryMilliseconds;
        }
    }

    void InitializeXr(const ULONGLONG now) {
        next_runtime_attempt_ = now + kRuntimeRetryMilliseconds;
        host_callbacks_ = {this, &XrLogThunk};
        shared_capture_session_enabled_ = false;
        Log(wawvr::xr::LogLevel::Info,
            "Attempting OpenXR initialization from exact post-Com_Frame boundary");
        if (!shared_capture_permanently_disabled_ &&
            shared_capture_.Initialize(
                wawvr::xr::D3D9ExBridgeConfig{
                    // Match COD4's overlap model: one image can be sampled,
                    // one retired, one producer-complete, and one producing
                    // while bounding the exact-current producer wait to 6 ms.
                    .ring_size = 4,
                    .producer_wait_budget_microseconds = 6000,
                },
                host_callbacks_)) {
            shared_capture_session_enabled_ = true;
            queued_shared_captures_.clear();
            last_consumed_shared_capture_serial_ = 0;
            Log(wawvr::xr::LogLevel::Info,
                "Nonblocking four-slot D3D9Ex capture queue enabled; completed images retain their exact render pose and layout");
        } else {
            Log(wawvr::xr::LogLevel::Warning,
                "D3D9Ex shared-texture bridge initialization failed; CPU capture remains available");
        }
        if (!capture_.Initialize(host_callbacks_)) {
            Log(wawvr::xr::LogLevel::Error,
                "D3D9 CPU capture initialization failed");
            if (shared_capture_session_enabled_) {
                shared_capture_.Shutdown();
                shared_capture_session_enabled_ = false;
            }
            return;
        }

        wawvr::xr::RuntimeConfig config{};
        config.application_name = "World War VR";
        config.engine_name = "IW/T4 compatibility layer";
        config.render_scale = 1.0f;
        config.reference_space = wawvr::xr::ReferenceSpace::Local;
        config.prefer_srgb_swapchain = true;
        if (!runtime_.Initialize(config, host_callbacks_)) {
            capture_.Shutdown();
            if (shared_capture_session_enabled_) {
                shared_capture_.Shutdown();
                shared_capture_session_enabled_ = false;
            }
            if (runtime_.last_initialization_failure() ==
                wawvr::xr::InitializationFailure::headset_unavailable) {
                LogFormatted(
                    wawvr::xr::LogLevel::Error,
                    "OpenXR runtime '%s' reports no PC VR headset is available; wake/connect the headset and enter Quest Link/Air Link or start the intended SteamVR runtime",
                    runtime_.active_runtime_name());
                Log(wawvr::xr::LogLevel::Warning,
                    "Desktop rendering remains visible while World War VR retries OpenXR every 10 seconds");
                notify_headset_unavailable_once();
            } else if (runtime_.last_initialization_failure() ==
                       wawvr::xr::InitializationFailure::runtime_unavailable) {
                Log(wawvr::xr::LogLevel::Error,
                    "No active OpenXR runtime is available; configure Meta Quest Link or SteamVR as the active OpenXR runtime and relaunch");
            } else {
                Log(wawvr::xr::LogLevel::Warning,
                    "OpenXR initialization unavailable; desktop game continues and retry is scheduled");
            }
            return;
        }
        compositor_initialized_ = compositor_.Initialize(runtime_, host_callbacks_);
        if (!compositor_initialized_) {
            ShutdownXr("initial compositor failure");
            return;
        }
        g_mono_xr_ready.store(true, std::memory_order_release);
        Log(wawvr::xr::LogLevel::Info,
            "OpenXR path ready: exact gameplay stereo plus finite room-anchored mono panels for frontend UI, loading screens, console, and cinematics");
    }

    void ShutdownXr(const std::string_view reason) noexcept {
#if defined(WAWVR_HAS_T4_BINDINGS)
        service_firing_haptics(runtime_, nullptr);
#endif
        bool teardown_exception = false;
        bool shared_retired =
            !shared_capture_session_enabled_ &&
            !shared_capture_permanently_disabled_;
        bool runtime_was_initialized = false;
        try {
            runtime_was_initialized = runtime_.initialized();
        } catch (...) {
            teardown_exception = true;
        }
        if (runtime_was_initialized) {
            LogFormatted(
                wawvr::xr::LogLevel::Info,
                "Shutting down XR path: %.*s",
                static_cast<int>(reason.size()), reason.data());
        }
        try {
            if (shared_capture_session_enabled_ &&
                !shared_capture_.Invalidate(
                    runtime_was_initialized
                        ? runtime_.d3d11_context()
                        : nullptr)) {
                Log(wawvr::xr::LogLevel::Error,
                    "D3D9Ex shared bridge could not retire cleanly during XR shutdown");
                teardown_exception = true;
                shared_capture_permanently_disabled_ = true;
            } else {
                shared_retired = true;
                queued_shared_captures_.clear();
                last_consumed_shared_capture_serial_ = 0;
            }
        } catch (...) {
            teardown_exception = true;
            shared_capture_permanently_disabled_ = true;
        }
        shared_capture_session_enabled_ = false;
        queued_shared_captures_.clear();
        last_consumed_shared_capture_serial_ = 0;
        try {
            compositor_.Shutdown();
        } catch (...) {
            teardown_exception = true;
        }
        if (shared_retired && !shared_capture_permanently_disabled_) {
            try {
                shared_capture_.Shutdown();
            } catch (...) {
                teardown_exception = true;
                shared_capture_permanently_disabled_ = true;
            }
        }
        try {
            capture_.Shutdown();
        } catch (...) {
            teardown_exception = true;
        }
        try {
            runtime_.Shutdown();
        } catch (...) {
            teardown_exception = true;
        }
        pending_frame_ = {};
        pending_frame_active_ = false;
        prepared_frame_ = {};
        last_projection_eyes_ = {};
        timing_last_source_frame_id_ = 0;
        timing_last_source_serial_ = 0;
        last_projection_available_ = false;
        last_quad_layer_ = {};
        last_submission_was_quad_ = false;
        tracking_anchor_ = {};
        tracking_anchor_valid_ = false;
        comfort_pose_ = {};
        comfort_pose_valid_ = false;
        comfort_quad_ = {};
        comfort_quad_valid_ = false;
        last_observed_presentation_mode_ = PresentationMode::stereo;
        last_observed_presentation_mode_valid_ = false;
        last_logged_presentation_mode_ = PresentationMode::stereo;
        last_logged_connection_state_ = 0;
        last_logged_active_connection_state_ = 10;
        last_logged_key_catchers_ = 0;
        last_logged_t4_state_valid_ = false;
        presentation_state_log_valid_ = false;
        last_logged_requested_layer_kind_ =
            wawvr::xr::CompositionLayerKind::none;
        last_logged_accepted_layer_kind_ =
            wawvr::xr::CompositionLayerKind::none;
        logged_layer_kind_valid_ = false;
        visible_menu_quad_ = {};
        visible_menu_quad_valid_ = false;
        menu_button_gesture_state_ = {};
#if defined(WAWVR_HAS_T4_BINDINGS)
        menu_input_state_ = {};
        last_menu_surface_ = {};
        last_menu_surface_valid_ = false;
        visible_menu_surface_ = {};
        visible_menu_surface_valid_ = false;
        // A partial aggregate must never bridge XR sessions or frame-id
        // epochs. Keep the process-wide report cap, but require a fresh
        // live held-weapon gameplay receipt before this bounded probe resumes after a
        // runtime/device restart.
        capture_submission_diagnostic_window_ = {};
        capture_submission_diagnostic_armed_ = false;
#endif
        last_source_width_ = 0;
        last_source_height_ = 0;
        automatic_gameplay_recenter_pending_ = false;
        automatic_gameplay_recenter_completed_ = false;
        explicit_recenter_captured_ = false;
        using_recovery_projection_ = false;
        next_recovery_transition_log_ = 0;
        logged_present_failure_ = false;
        logged_partial_compositor_overwrite_ = false;
        logged_virtual_desktop_ui_projection_fallback_ = false;
        logged_shared_capture_path_ = false;
        logged_cpu_capture_path_ = false;
        last_present_result_ = D3D_OK;
        last_cooperative_result_ = D3D_OK;
        ResetBoundaryObservations();
        clear_pending_stereo_frame();
        clear_controller_frame();
        discard_controller_tracking_anchor_rebase();
        compositor_initialized_ = false;
        g_mono_xr_ready.store(false, std::memory_order_release);
        if (teardown_exception) {
            Log(wawvr::xr::LogLevel::Error,
                "One or more XR teardown stages raised an exception; remaining stages and local state cleanup were still completed");
        }
    }

    static void XrLogThunk(
        void* const user_data,
        const wawvr::xr::LogLevel level,
        const char* const message) noexcept {
        if (user_data != nullptr && message != nullptr) {
            static_cast<PresentHookController*>(user_data)->Log(level, message);
        }
    }

    void Log(
        const wawvr::xr::LogLevel level,
        const std::string_view message) const noexcept {
        append_log(module_, level, message);
    }

    void LogFormatted(
        const wawvr::xr::LogLevel level,
        const char* const format,
        ...) const noexcept {
        std::array<char, 1024> buffer{};
        va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
        va_end(arguments);
        buffer.back() = '\0';
        Log(level, buffer.data());
    }

    void LogRestoreResult(
        const char* const name,
        const VtableOwnership ownership) const noexcept {
        if (ownership == VtableOwnership::original) {
            LogFormatted(wawvr::xr::LogLevel::Info, "%s slot restored", name);
        } else {
            LogFormatted(
                wawvr::xr::LogLevel::Warning,
                "%s slot ownership changed; foreign value preserved", name);
        }
    }

    [[nodiscard]] bool PublishedTargetMatchesEngine() const noexcept {
        const auto* const profile = configured_present_profile();
        T4PresentTarget current{};
        return profile != nullptr &&
               read_t4_present_target(*profile, &current) &&
               current.device ==
                   target_device_.load(std::memory_order_acquire) &&
               current.swap_chain ==
                   target_swap_chain_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool ReadValidatedTargetMatching(
        const T4PresentTarget& expected,
        T4PresentTarget* const refreshed) const noexcept {
        const auto* const profile = configured_present_profile();
        T4PresentTarget current{};
        const bool current_validated =
            profile != nullptr && read_t4_present_target(*profile, &current);
        const PresentTargetIdentity expected_identity{
            reinterpret_cast<std::uintptr_t>(expected.device),
            reinterpret_cast<std::uintptr_t>(expected.swap_chain)};
        const PresentTargetIdentity current_identity{
            reinterpret_cast<std::uintptr_t>(current.device),
            reinterpret_cast<std::uintptr_t>(current.swap_chain)};
        if (!present_target_still_current(
                expected_identity, current_validated, current_identity)) {
            return false;
        }
        if (refreshed != nullptr) {
            *refreshed = current;
        }
        return true;
    }

    void RestoreD3DSlotsForRebind() noexcept {
        frame_service_ready_.store(false, std::memory_order_release);
        IDirect3DDevice9* const reset_device =
            target_device_.load(std::memory_order_acquire);
        if (bootstrap_reset_handler_registered_.exchange(
                false, std::memory_order_acq_rel)) {
            unregister_d3d9ex_reset_handler(
                reset_device, &BootstrapResetHandler);
        }
        if (legacy_reset_patch_installed_.exchange(
                false, std::memory_order_acq_rel)) {
            LogRestoreResult("D3D9 Reset", device_reset_patch_.Restore());
        }
        reset_hook_installed_.store(false, std::memory_order_release);
        ex_reset_failed_.store(false, std::memory_order_release);
        ex_focus_window_.store(nullptr, std::memory_order_release);
        if (g_present_hook_installed.exchange(
                false, std::memory_order_acq_rel)) {
            LogRestoreResult(
                "swap-chain Present", swap_present_patch_.Restore());
        }
        ClearPublishedTargetIdentity();
    }

    void ClearPublishedTargetIdentity() noexcept {
        target_device_.store(nullptr, std::memory_order_release);
        target_swap_chain_.store(nullptr, std::memory_order_release);
    }

    HMODULE module_{};
    VtableSlotPatch swap_present_patch_;
    VtableSlotPatch device_reset_patch_;
    std::atomic<IDirect3DDevice9*> target_device_{nullptr};
    std::atomic<IDirect3DSwapChain9*> target_swap_chain_{nullptr};
    std::atomic<bool> reset_hook_installed_{false};
    std::atomic<bool> bootstrap_reset_handler_registered_{false};
    std::atomic<bool> legacy_reset_patch_installed_{false};
    std::atomic<bool> reset_in_progress_{false};
    std::atomic<bool> ex_reset_failed_{false};
    std::atomic<HWND> ex_focus_window_{nullptr};
    std::atomic<bool> frame_service_ready_{false};
    std::atomic<bool> target_refresh_requested_{false};
    std::mutex render_mutex_;
    wawvr::xr::HostCallbacks host_callbacks_{};
    wawvr::xr::OpenXrRuntime runtime_;
    wawvr::xr::D3D9ExSharedTextureBridge shared_capture_;
    wawvr::xr::D3D9CpuCapture capture_;
    wawvr::xr::D3D11Compositor compositor_;
    wawvr::xr::FrameState pending_frame_{};
    PreparedPendingFrame prepared_frame_{};
    std::deque<PreparedPendingFrame> queued_shared_captures_{};
    std::uint64_t last_consumed_shared_capture_serial_{};
    std::uint64_t timing_last_source_frame_id_{};
    std::uint64_t timing_last_source_serial_{};
    std::uint32_t logged_slow_exact_capture_poll_count_{};
    std::array<wawvr::xr::EyeView, wawvr::xr::kEyeCount>
        last_projection_eyes_{};
    wawvr::xr::Posef tracking_anchor_{};
    wawvr::xr::Posef comfort_pose_{};
    wawvr::xr::QuadLayer comfort_quad_{};
    wawvr::xr::QuadLayer visible_menu_quad_{};
    wawvr::xr::QuadLayer last_quad_layer_{};
    PresentationMode last_observed_presentation_mode_{
        PresentationMode::stereo};
    PresentationMode last_logged_presentation_mode_{
        PresentationMode::stereo};
    wawvr::xr::CompositionLayerKind last_logged_requested_layer_kind_{
        wawvr::xr::CompositionLayerKind::none};
    wawvr::xr::CompositionLayerKind last_logged_accepted_layer_kind_{
        wawvr::xr::CompositionLayerKind::none};
    MenuButtonGestureState menu_button_gesture_state_{};
#if defined(WAWVR_HAS_T4_BINDINGS)
    T4MenuInputState menu_input_state_{};
    MenuPointerSurface last_menu_surface_{};
    MenuPointerSurface visible_menu_surface_{};
    CaptureSubmissionDiagnosticWindow
        capture_submission_diagnostic_window_{};
    std::uint32_t capture_submission_diagnostic_report_count_{};
    bool capture_submission_diagnostic_armed_{};
#endif
    std::uint32_t last_source_width_{};
    std::uint32_t last_source_height_{};
    std::int32_t last_logged_connection_state_{};
    std::int32_t last_logged_active_connection_state_{10};
    std::uint32_t last_logged_key_catchers_{};
    bool compositor_initialized_ = false;
    bool pending_frame_active_ = false;
    bool last_projection_available_ = false;
    bool tracking_anchor_valid_ = false;
    bool comfort_pose_valid_ = false;
    bool comfort_quad_valid_ = false;
    bool visible_menu_quad_valid_ = false;
    bool last_submission_was_quad_ = false;
    bool last_observed_presentation_mode_valid_ = false;
    bool last_logged_t4_state_valid_ = false;
    bool presentation_state_log_valid_ = false;
    bool logged_layer_kind_valid_ = false;
    bool automatic_gameplay_recenter_pending_ = false;
    bool automatic_gameplay_recenter_completed_ = false;
    bool explicit_recenter_captured_ = false;
    bool logged_first_primed_frame_ = false;
    bool logged_first_stereo_submission_ = false;
    bool logged_shared_capture_path_ = false;
    bool logged_cpu_capture_path_ = false;
    bool logged_exact_capture_success_ = false;
    bool logged_exact_capture_fallback_ = false;
    bool logged_missing_stereo_backend_ = false;
#if defined(WAWVR_HAS_T4_BINDINGS)
    bool logged_first_native_menu_cursor_ = false;
    bool logged_first_controller_menu_ray_ = false;
    bool last_menu_surface_valid_ = false;
    bool visible_menu_surface_valid_ = false;
#endif
    bool using_recovery_projection_ = false;
    bool shared_capture_session_enabled_ = false;
    bool shared_capture_permanently_disabled_ = false;
    bool logged_present_failure_ = false;
    bool logged_partial_compositor_overwrite_ = false;
    bool logged_virtual_desktop_ui_projection_fallback_ = false;
    bool scene_hook_permanently_disabled_ = false;
    bool present_seen_since_boundary_ = false;
    bool device_loss_seen_since_boundary_ = false;
    // Protected by render_mutex_. Unlike the per-boundary observations above,
    // this survives Com_Frame service until a lost ResetEx succeeds or the
    // exact renderer target is replaced/cleared.
    bool ex_device_recovery_required_ = false;
    bool reset_seen_since_boundary_ = false;
    HRESULT latest_present_result_ = D3D_OK;
    HRESULT latest_reset_result_ = D3D_OK;
    HRESULT last_present_result_ = D3D_OK;
    HRESULT last_cooperative_result_ = D3D_OK;
    ULONGLONG next_recovery_transition_log_ = 0;
    ULONGLONG next_runtime_attempt_ = 0;
    ULONGLONG next_scene_hook_attempt_ = 0;
    ActiveUiMonoSource active_ui_source_{configured_active_ui_source()};
};

std::atomic<PresentHookController*> PresentHookController::instance_{nullptr};

} // namespace

bool configure_renderer_hooks(
    const wawvr::t4::ExecutableLayoutId layout) noexcept {
    const int desired = renderer_layout_code(layout);
    if (desired == kRendererLayoutUnconfigured) {
        return false;
    }
    int expected = kRendererLayoutUnconfigured;
    return g_renderer_layout.compare_exchange_strong(
               expected, desired, std::memory_order_acq_rel,
               std::memory_order_acquire) ||
           expected == desired;
}

bool get_configured_renderer_layout(
    wawvr::t4::ExecutableLayoutId* const layout) noexcept {
    if (layout == nullptr) {
        return false;
    }
    switch (g_renderer_layout.load(std::memory_order_acquire)) {
    case kRendererLayoutSinglePlayer:
        *layout = wawvr::t4::ExecutableLayoutId::t4_sp_1_7_1263;
        return true;
    case kRendererLayoutMultiplayer:
        *layout = wawvr::t4::ExecutableLayoutId::t4_mp_1_7_1263;
        return true;
    default:
        return false;
    }
}

void service_present_hook_after_com_frame() noexcept {
    PresentHookController::ServicePublishedAfterComFrame();
}

void input_diagnostic_log(const char* const format, ...) noexcept {
    if (format == nullptr) {
        return;
    }
    std::array<char, 1024> buffer{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
    va_end(arguments);
    buffer.back() = '\0';

    const HMODULE module = g_log_module.load(std::memory_order_acquire);
    if (module != nullptr) {
        append_log(module, wawvr::xr::LogLevel::Info, buffer.data());
    } else {
        std::string debugger_line = "WorldAtWarVR [info]: ";
        debugger_line += buffer.data();
        debugger_line += '\n';
        OutputDebugStringA(debugger_line.c_str());
    }
}

void stereo_diagnostic_log(const char* const format, ...) noexcept {
    if (format == nullptr) {
        return;
    }
    std::array<char, 1024> buffer{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
    va_end(arguments);
    buffer.back() = '\0';

    const HMODULE module = g_log_module.load(std::memory_order_acquire);
    if (module != nullptr) {
        append_log(module, wawvr::xr::LogLevel::Warning, buffer.data());
    } else {
        std::string debugger_line = "WorldAtWarVR [warning]: ";
        debugger_line += buffer.data();
        debugger_line += '\n';
        OutputDebugStringA(debugger_line.c_str());
    }
}

void stereo_diagnostic_log_once(
    std::atomic_flag& gate,
    const char* const format,
    ...) noexcept {
    if (gate.test_and_set(std::memory_order_relaxed) || format == nullptr) {
        return;
    }
    std::array<char, 1024> buffer{};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
    va_end(arguments);
    buffer.back() = '\0';

    stereo_diagnostic_log("%s", buffer.data());
}

void request_present_hook_shutdown() noexcept {
    g_shutdown_requested.store(true, std::memory_order_release);
}

bool present_hook_installed() noexcept {
    return g_present_hook_installed.load(std::memory_order_acquire);
}

bool mono_xr_ready() noexcept {
    return g_mono_xr_ready.load(std::memory_order_acquire);
}

void run_present_hook_monitor(const HMODULE module) noexcept {
    g_log_module.store(module, std::memory_order_release);
    const PresentExecutableProfile* const profile =
        configured_present_profile();
    if (profile == nullptr) {
        append_log(
            module, wawvr::xr::LogLevel::Error,
            "Renderer hooks were not configured by an exact executable profile; hooks disabled");
        return;
    }
    if (environment_disables_present_hook()) {
        append_log(
            module, wawvr::xr::LogLevel::Warning,
            "WAWVR_DISABLE_XR is set; D3D9/OpenXR hooks are disabled");
        return;
    }
    if (!verify_rb_swap_buffers_sentinel(*profile)) {
        std::array<char, 192> message{};
        std::snprintf(
            message.data(), message.size(),
            "%s RB_SwapBuffers bytes at 0x%08llX did not match the exact profile; hooks disabled",
            profile->name,
            static_cast<unsigned long long>(
                profile->rb_swap_buffers_address));
        append_log(
            module, wawvr::xr::LogLevel::Error,
            message.data());
        return;
    }
    std::array<char, 160> validation_message{};
    std::snprintf(
        validation_message.data(), validation_message.size(),
        "%s RB_SwapBuffers exact 40-byte sentinel validated at 0x%08llX",
        profile->name,
        static_cast<unsigned long long>(profile->rb_swap_buffers_address));
    append_log(
        module, wawvr::xr::LogLevel::Info,
        validation_message.data());
    append_log(
        module, wawvr::xr::LogLevel::Info,
        environment_enables_eye_local_bloom()
            ? "Experimental eye-local additive bloom explicitly enabled with WAWVR_ENABLE_EYE_LOCAL_BLOOM"
            : "COD4/F.E.A.R. sRGB color parity active; eye-local additive bloom disabled by default");
    log_openxr_runtime_selection(module);

    // Process-lifetime ownership avoids destroying XR state from DllMain.
    auto* const controller = new (std::nothrow) PresentHookController(module);
    if (controller == nullptr) {
        append_log(
            module, wawvr::xr::LogLevel::Error,
            "Could not allocate Present-hook controller; desktop game continues");
        return;
    }

    append_log(
        module, wawvr::xr::LogLevel::Info,
        "Waiting for T4 target window/swap-chain after validated D3D initialization");
    ULONGLONG next_install_attempt = 0;
    while (!g_shutdown_requested.load(std::memory_order_acquire)) {
        T4PresentTarget target{};
        const bool target_ready = read_t4_present_target(*profile, &target);
        const ULONGLONG now = GetTickCount64();
        const PresentTargetIdentity installed_target{
            reinterpret_cast<std::uintptr_t>(controller->target_device()),
            reinterpret_cast<std::uintptr_t>(controller->target_swap_chain())};
        const PresentTargetIdentity candidate_target{
            reinterpret_cast<std::uintptr_t>(target.device),
            reinterpret_cast<std::uintptr_t>(target.swap_chain)};
        const bool target_installed = installed_target.swap_chain_id != 0;
        const auto action = plan_present_target_monitor(
            target_installed, installed_target, target_ready,
            candidate_target);
        if (action == PresentTargetMonitorAction::install_target &&
            now >= next_install_attempt) {
            if (!controller->Install(target)) {
                next_install_attempt = now + kInstallRetryMilliseconds;
            }
        } else if (action == PresentTargetMonitorAction::rebind_target &&
                   now >= next_install_attempt) {
            if (!controller->Rebind(target)) {
                next_install_attempt = now + kInstallRetryMilliseconds;
            } else {
                next_install_attempt = 0;
            }
        } else if (action == PresentTargetMonitorAction::keep_target &&
                   target_installed) {
            if (target_ready) {
                controller->ResumeFrameServiceForCurrentTarget();
            } else {
                controller->PauseFrameServiceForTargetRefresh();
            }
        }
        Sleep(kTargetPollMilliseconds);
    }

    if (controller->ShutdownRenderStateAndRestoreSlots()) {
        append_log(
            module, wawvr::xr::LogLevel::Info,
            "Present-hook monitor stopped");
    } else {
        append_log(
            module, wawvr::xr::LogLevel::Error,
            "Present-hook monitor stopped with renderer hooks pinned fail-closed");
    }
}

} // namespace wawvr::mod
