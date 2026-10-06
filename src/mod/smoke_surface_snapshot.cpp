// SPDX-License-Identifier: GPL-3.0-only
#include "smoke_surface_snapshot.hpp"

#include "stereo_diagnostics.hpp"

#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace wawvr::mod {
namespace {

constexpr std::uint32_t kAfterRightEmissiveBit = 1u << 0u;
constexpr std::uint32_t kBeforePresentBit = 1u << 1u;
constexpr std::uint32_t kFloatZAfterLeftEyeBit = 1u << 2u;
constexpr std::uint32_t kFloatZAfterRightEyeBit = 1u << 3u;
constexpr UINT kMaximumSnapshotWidth = 3008u;
constexpr UINT kMaximumSnapshotHeight = 1344u;

std::atomic<std::uint32_t> g_claimed_snapshots{0};
std::atomic<bool> g_present_snapshot_armed{false};

[[nodiscard]] bool snapshots_enabled() noexcept {
    static const bool enabled = []() noexcept {
        wchar_t value[2]{};
        return GetEnvironmentVariableW(
                   L"WAWVR_FX_SURFACE_SNAPSHOTS", value, 2) == 1 &&
            value[0] == L'1';
    }();
    return enabled;
}

[[nodiscard]] std::wstring snapshot_directory() {
    std::array<wchar_t, 32768> value{};
    const DWORD length = GetEnvironmentVariableW(
        L"WAWVR_FX_SNAPSHOT_DIR", value.data(),
        static_cast<DWORD>(value.size()));
    if (length != 0u && length < value.size()) {
        return std::wstring(value.data(), length);
    }
    const DWORD temp_length = GetTempPathW(
        static_cast<DWORD>(value.size()), value.data());
    if (temp_length == 0u || temp_length >= value.size()) {
        return {};
    }
    return std::wstring(value.data(), temp_length);
}

[[nodiscard]] bool write_all(
    const HANDLE file, const void* const data,
    const DWORD bytes) noexcept {
    DWORD written = 0;
    return WriteFile(file, data, bytes, &written, nullptr) != FALSE &&
        written == bytes;
}

[[nodiscard]] bool build_snapshot_path(
    const wchar_t* const file_name,
    std::wstring* const path) noexcept {
    if (file_name == nullptr || path == nullptr) {
        return false;
    }
    try {
        std::wstring directory = snapshot_directory();
        if (directory.empty()) {
            return false;
        }
        if (directory.back() != L'\\' && directory.back() != L'/') {
            directory.push_back(L'\\');
        }
        *path = directory + file_name;
        return true;
    } catch (...) {
        path->clear();
        return false;
    }
}

[[nodiscard]] bool save_surface_as_bmp(
    IDirect3DDevice9* const device,
    IDirect3DSurface9* const source,
    const wchar_t* const file_name) noexcept {
    if (device == nullptr || source == nullptr || file_name == nullptr) {
        return false;
    }

    D3DSURFACE_DESC description{};
    HRESULT result = source->GetDesc(&description);
    if (FAILED(result) || description.Width == 0u ||
        description.Height == 0u ||
        (description.Format != D3DFMT_A8R8G8B8 &&
         description.Format != D3DFMT_X8R8G8B8)) {
        stereo_diagnostic_log(
            "StereoDiag smoke-snapshot rejected surface: descHr=0x%08lX size=%ux%u format=%u",
            static_cast<unsigned long>(result), description.Width,
            description.Height, static_cast<unsigned>(description.Format));
        return false;
    }

    const double scale = std::min(
        1.0,
        std::min(
            static_cast<double>(kMaximumSnapshotWidth) /
                static_cast<double>(description.Width),
            static_cast<double>(kMaximumSnapshotHeight) /
                static_cast<double>(description.Height)));
    const UINT width = std::max(
        1u, static_cast<UINT>(description.Width * scale));
    const UINT height = std::max(
        1u, static_cast<UINT>(description.Height * scale));

    IDirect3DSurface9* resolved = nullptr;
    result = device->CreateRenderTarget(
        width, height, description.Format, D3DMULTISAMPLE_NONE, 0,
        FALSE, &resolved, nullptr);
    if (FAILED(result) || resolved == nullptr) {
        stereo_diagnostic_log(
            "StereoDiag smoke-snapshot CreateRenderTarget failed: hr=0x%08lX",
            static_cast<unsigned long>(result));
        return false;
    }

    result = device->StretchRect(
        source, nullptr, resolved, nullptr, D3DTEXF_LINEAR);
    if (FAILED(result)) {
        stereo_diagnostic_log(
            "StereoDiag smoke-snapshot StretchRect failed: hr=0x%08lX",
            static_cast<unsigned long>(result));
        resolved->Release();
        return false;
    }

    IDirect3DSurface9* readback = nullptr;
    result = device->CreateOffscreenPlainSurface(
        width, height, description.Format, D3DPOOL_SYSTEMMEM,
        &readback, nullptr);
    if (FAILED(result) || readback == nullptr) {
        stereo_diagnostic_log(
            "StereoDiag smoke-snapshot CreateOffscreenPlainSurface failed: hr=0x%08lX",
            static_cast<unsigned long>(result));
        resolved->Release();
        return false;
    }

    result = device->GetRenderTargetData(resolved, readback);
    resolved->Release();
    if (FAILED(result)) {
        stereo_diagnostic_log(
            "StereoDiag smoke-snapshot GetRenderTargetData failed: hr=0x%08lX",
            static_cast<unsigned long>(result));
        readback->Release();
        return false;
    }

    D3DLOCKED_RECT locked{};
    result = readback->LockRect(
        &locked, nullptr, D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK);
    if (FAILED(result) || locked.pBits == nullptr || locked.Pitch <= 0 ||
        static_cast<UINT>(locked.Pitch) < width * 4u) {
        stereo_diagnostic_log(
            "StereoDiag smoke-snapshot LockRect failed: hr=0x%08lX pitch=%d",
            static_cast<unsigned long>(result), locked.Pitch);
        if (SUCCEEDED(result)) {
            readback->UnlockRect();
        }
        readback->Release();
        return false;
    }

    std::wstring path;
    if (!build_snapshot_path(file_name, &path)) {
        readback->UnlockRect();
        readback->Release();
        return false;
    }
    const HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        stereo_diagnostic_log(
            "StereoDiag smoke-snapshot CreateFile failed: error=%lu",
            static_cast<unsigned long>(GetLastError()));
        readback->UnlockRect();
        readback->Release();
        return false;
    }

    const std::uint64_t pixel_bytes =
        static_cast<std::uint64_t>(width) * height * 4u;
    const std::uint64_t file_bytes =
        sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + pixel_bytes;
    bool saved = file_bytes <= std::numeric_limits<DWORD>::max();
    BITMAPFILEHEADER file_header{};
    file_header.bfType = 0x4D42u;
    file_header.bfOffBits =
        sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file_header.bfSize = static_cast<DWORD>(file_bytes);
    BITMAPINFOHEADER info_header{};
    info_header.biSize = sizeof(info_header);
    info_header.biWidth = static_cast<LONG>(width);
    info_header.biHeight = -static_cast<LONG>(height);
    info_header.biPlanes = 1u;
    info_header.biBitCount = 32u;
    info_header.biCompression = BI_RGB;
    info_header.biSizeImage = static_cast<DWORD>(pixel_bytes);
    saved = saved && write_all(file, &file_header, sizeof(file_header));
    saved = saved && write_all(file, &info_header, sizeof(info_header));
    const auto* row = static_cast<const std::uint8_t*>(locked.pBits);
    const DWORD row_bytes = width * 4u;
    for (UINT y = 0; saved && y < height; ++y) {
        saved = write_all(file, row, row_bytes);
        row += locked.Pitch;
    }
    CloseHandle(file);
    const HRESULT unlock_result = readback->UnlockRect();
    readback->Release();
    saved = saved && SUCCEEDED(unlock_result);

    stereo_diagnostic_log(
        "StereoDiag smoke-snapshot %s: source=%ux%u output=%ux%u format=%u file=%ls",
        saved ? "saved" : "failed", description.Width,
        description.Height, width, height,
        static_cast<unsigned>(description.Format), path.c_str());
    return saved;
}

struct FloatZHalfStatistics final {
    float minimum{std::numeric_limits<float>::infinity()};
    float maximum{-std::numeric_limits<float>::infinity()};
    double sum{};
    std::uint64_t finite_count{};
    std::uint64_t non_finite_count{};
    std::uint64_t near_zero_count{};
    std::uint64_t near_one_count{};
    std::uint64_t below_ten_count{};
    std::uint64_t below_hundred_count{};
    std::uint64_t below_thousand_count{};
    std::uint64_t far_clear_count{};
};

void record_floatz_value(
    const float value,
    FloatZHalfStatistics* const statistics) noexcept {
    if (statistics == nullptr) {
        return;
    }
    if (!std::isfinite(value)) {
        ++statistics->non_finite_count;
        return;
    }
    statistics->minimum = std::min(statistics->minimum, value);
    statistics->maximum = std::max(statistics->maximum, value);
    statistics->sum += static_cast<double>(value);
    ++statistics->finite_count;
    if (std::abs(value) <= 1.0e-6f) {
        ++statistics->near_zero_count;
    }
    if (value >= 0.999999f) {
        ++statistics->near_one_count;
    }
    if (value < 10.0f) {
        ++statistics->below_ten_count;
    }
    if (value < 100.0f) {
        ++statistics->below_hundred_count;
    }
    if (value < 1000.0f) {
        ++statistics->below_thousand_count;
    }
    if (value >= 1999000.0f) {
        ++statistics->far_clear_count;
    }
}

[[nodiscard]] bool save_floatz_texture_as_bmp(
    IDirect3DDevice9* const device,
    IDirect3DTexture9* const texture,
    const wchar_t* const file_name) noexcept {
    if (device == nullptr || texture == nullptr || file_name == nullptr) {
        return false;
    }

    D3DSURFACE_DESC description{};
    HRESULT result = texture->GetLevelDesc(0u, &description);
    if (FAILED(result) || description.Width == 0u ||
        description.Height == 0u || description.Format != D3DFMT_R32F) {
        stereo_diagnostic_log(
            "StereoDiag smoke-floatz rejected texture: descHr=0x%08lX size=%ux%u format=%u usage=0x%lX pool=%u",
            static_cast<unsigned long>(result), description.Width,
            description.Height, static_cast<unsigned>(description.Format),
            static_cast<unsigned long>(description.Usage),
            static_cast<unsigned>(description.Pool));
        return false;
    }

    IDirect3DSurface9* source = nullptr;
    result = texture->GetSurfaceLevel(0u, &source);
    if (FAILED(result) || source == nullptr) {
        stereo_diagnostic_log(
            "StereoDiag smoke-floatz GetSurfaceLevel failed: hr=0x%08lX",
            static_cast<unsigned long>(result));
        return false;
    }

    IDirect3DSurface9* readback = nullptr;
    result = device->CreateOffscreenPlainSurface(
        description.Width, description.Height, description.Format,
        D3DPOOL_SYSTEMMEM, &readback, nullptr);
    if (FAILED(result) || readback == nullptr) {
        stereo_diagnostic_log(
            "StereoDiag smoke-floatz CreateOffscreenPlainSurface failed: hr=0x%08lX",
            static_cast<unsigned long>(result));
        source->Release();
        return false;
    }

    result = device->GetRenderTargetData(source, readback);
    source->Release();
    if (FAILED(result)) {
        stereo_diagnostic_log(
            "StereoDiag smoke-floatz GetRenderTargetData failed: hr=0x%08lX",
            static_cast<unsigned long>(result));
        readback->Release();
        return false;
    }

    D3DLOCKED_RECT locked{};
    result = readback->LockRect(
        &locked, nullptr, D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK);
    if (FAILED(result) || locked.pBits == nullptr || locked.Pitch <= 0 ||
        static_cast<UINT>(locked.Pitch) < description.Width * sizeof(float)) {
        stereo_diagnostic_log(
            "StereoDiag smoke-floatz LockRect failed: hr=0x%08lX pitch=%d",
            static_cast<unsigned long>(result), locked.Pitch);
        if (SUCCEEDED(result)) {
            readback->UnlockRect();
        }
        readback->Release();
        return false;
    }

    FloatZHalfStatistics left{};
    FloatZHalfStatistics right{};
    const auto* source_row = static_cast<const std::uint8_t*>(locked.pBits);
    for (UINT y = 0; y < description.Height; ++y) {
        const auto* values = reinterpret_cast<const float*>(source_row);
        for (UINT x = 0; x < description.Width; ++x) {
            record_floatz_value(
                values[x], x < description.Width / 2u ? &left : &right);
        }
        source_row += locked.Pitch;
    }

    const double scale = std::min(
        1.0,
        std::min(
            static_cast<double>(kMaximumSnapshotWidth) /
                static_cast<double>(description.Width),
            static_cast<double>(kMaximumSnapshotHeight) /
                static_cast<double>(description.Height)));
    const UINT width = std::max(
        1u, static_cast<UINT>(description.Width * scale));
    const UINT height = std::max(
        1u, static_cast<UINT>(description.Height * scale));
    std::wstring path;
    if (!build_snapshot_path(file_name, &path)) {
        readback->UnlockRect();
        readback->Release();
        return false;
    }
    const HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        stereo_diagnostic_log(
            "StereoDiag smoke-floatz CreateFile failed: error=%lu",
            static_cast<unsigned long>(GetLastError()));
        readback->UnlockRect();
        readback->Release();
        return false;
    }

    const std::uint64_t pixel_bytes =
        static_cast<std::uint64_t>(width) * height * 4u;
    const std::uint64_t file_bytes =
        sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + pixel_bytes;
    bool saved = file_bytes <= std::numeric_limits<DWORD>::max();
    BITMAPFILEHEADER file_header{};
    file_header.bfType = 0x4D42u;
    file_header.bfOffBits =
        sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file_header.bfSize = static_cast<DWORD>(file_bytes);
    BITMAPINFOHEADER info_header{};
    info_header.biSize = sizeof(info_header);
    info_header.biWidth = static_cast<LONG>(width);
    info_header.biHeight = -static_cast<LONG>(height);
    info_header.biPlanes = 1u;
    info_header.biBitCount = 32u;
    info_header.biCompression = BI_RGB;
    info_header.biSizeImage = static_cast<DWORD>(pixel_bytes);
    saved = saved && write_all(file, &file_header, sizeof(file_header));
    saved = saved && write_all(file, &info_header, sizeof(info_header));

    std::array<std::uint32_t, kMaximumSnapshotWidth> output_row{};
    const auto* pixels = static_cast<const std::uint8_t*>(locked.pBits);
    const DWORD row_bytes = width * 4u;
    for (UINT y = 0; saved && y < height; ++y) {
        const UINT source_y = static_cast<UINT>(
            (static_cast<std::uint64_t>(y) * description.Height) / height);
        const auto* values = reinterpret_cast<const float*>(
            pixels + static_cast<std::size_t>(source_y) * locked.Pitch);
        for (UINT x = 0; x < width; ++x) {
            const UINT source_x = static_cast<UINT>(
                (static_cast<std::uint64_t>(x) * description.Width) / width);
            const float value = values[source_x];
            constexpr float kVisualMaximum = 2000000.0f;
            const float normalized = std::isfinite(value)
                ? std::clamp(
                      std::log1p(std::max(value, 0.0f)) /
                          std::log1p(kVisualMaximum),
                      0.0f, 1.0f)
                : 0.0f;
            const auto grey = static_cast<std::uint32_t>(
                normalized * 255.0f + 0.5f);
            output_row[x] = 0xFF000000u | (grey << 16u) |
                (grey << 8u) | grey;
        }
        saved = write_all(file, output_row.data(), row_bytes);
    }
    CloseHandle(file);
    const HRESULT unlock_result = readback->UnlockRect();
    readback->Release();
    saved = saved && SUCCEEDED(unlock_result);

    const auto mean = [](const FloatZHalfStatistics& stats) noexcept {
        return stats.finite_count != 0u
            ? stats.sum / static_cast<double>(stats.finite_count)
            : 0.0;
    };
    stereo_diagnostic_log(
        "StereoDiag smoke-floatz %s: source=%ux%u output=%ux%u file=%ls left=(finite=%llu nonfinite=%llu min=%.9g max=%.9g mean=%.9g zero=%llu one=%llu lt10=%llu lt100=%llu lt1000=%llu far=%llu) right=(finite=%llu nonfinite=%llu min=%.9g max=%.9g mean=%.9g zero=%llu one=%llu lt10=%llu lt100=%llu lt1000=%llu far=%llu)",
        saved ? "saved" : "failed", description.Width,
        description.Height, width, height, path.c_str(),
        static_cast<unsigned long long>(left.finite_count),
        static_cast<unsigned long long>(left.non_finite_count),
        left.minimum, left.maximum, mean(left),
        static_cast<unsigned long long>(left.near_zero_count),
        static_cast<unsigned long long>(left.near_one_count),
        static_cast<unsigned long long>(left.below_ten_count),
        static_cast<unsigned long long>(left.below_hundred_count),
        static_cast<unsigned long long>(left.below_thousand_count),
        static_cast<unsigned long long>(left.far_clear_count),
        static_cast<unsigned long long>(right.finite_count),
        static_cast<unsigned long long>(right.non_finite_count),
        right.minimum, right.maximum, mean(right),
        static_cast<unsigned long long>(right.near_zero_count),
        static_cast<unsigned long long>(right.near_one_count),
        static_cast<unsigned long long>(right.below_ten_count),
        static_cast<unsigned long long>(right.below_hundred_count),
        static_cast<unsigned long long>(right.below_thousand_count),
        static_cast<unsigned long long>(right.far_clear_count));
    return saved;
}

[[nodiscard]] bool capture_current_render_target(
    IDirect3DDevice9* const device,
    const wchar_t* const file_name) noexcept {
    IDirect3DSurface9* surface = nullptr;
    const HRESULT result = device != nullptr
        ? device->GetRenderTarget(0u, &surface)
        : E_POINTER;
    if (FAILED(result) || surface == nullptr) {
        stereo_diagnostic_log(
            "StereoDiag smoke-snapshot GetRenderTarget failed: hr=0x%08lX",
            static_cast<unsigned long>(result));
        return false;
    }
    const bool saved = save_surface_as_bmp(device, surface, file_name);
    surface->Release();
    return saved;
}

[[nodiscard]] bool capture_back_buffer(
    IDirect3DDevice9* const device,
    const wchar_t* const file_name) noexcept {
    IDirect3DSurface9* surface = nullptr;
    const HRESULT result = device != nullptr
        ? device->GetBackBuffer(
              0u, 0u, D3DBACKBUFFER_TYPE_MONO, &surface)
        : E_POINTER;
    if (FAILED(result) || surface == nullptr) {
        stereo_diagnostic_log(
            "StereoDiag smoke-snapshot GetBackBuffer failed: hr=0x%08lX",
            static_cast<unsigned long>(result));
        return false;
    }
    const bool saved = save_surface_as_bmp(device, surface, file_name);
    surface->Release();
    return saved;
}

} // namespace

bool capture_smoke_after_right_emissive(
    IDirect3DDevice9* const device) noexcept {
    if (!snapshots_enabled() || device == nullptr ||
        (g_claimed_snapshots.fetch_or(
             kAfterRightEmissiveBit, std::memory_order_acq_rel) &
         kAfterRightEmissiveBit) != 0u) {
        return false;
    }
    const bool saved = capture_current_render_target(
        device, L"WorldWarVR-smoke-after-right-emissive.bmp");
    if (saved) {
        g_present_snapshot_armed.store(true, std::memory_order_release);
    }
    return saved;
}

bool capture_smoke_floatz_after_eye(
    IDirect3DDevice9* const device,
    IDirect3DTexture9* const texture,
    const std::uint32_t eye_phase) noexcept {
    const std::uint32_t bit = eye_phase == 1u
        ? kFloatZAfterLeftEyeBit
        : eye_phase == 2u ? kFloatZAfterRightEyeBit : 0u;
    if (!snapshots_enabled() || device == nullptr || texture == nullptr ||
        bit == 0u ||
        (g_claimed_snapshots.fetch_or(bit, std::memory_order_acq_rel) & bit) !=
            0u) {
        return false;
    }
    return save_floatz_texture_as_bmp(
        device, texture,
        eye_phase == 1u
            ? L"WorldWarVR-smoke-floatz-after-left-eye.bmp"
            : L"WorldWarVR-smoke-floatz-after-right-eye.bmp");
}

bool capture_smoke_before_present(IDirect3DDevice9* const device) noexcept {
    if (!snapshots_enabled() || device == nullptr ||
        !g_present_snapshot_armed.load(std::memory_order_acquire) ||
        (g_claimed_snapshots.fetch_or(
             kBeforePresentBit, std::memory_order_acq_rel) &
         kBeforePresentBit) != 0u) {
        return false;
    }
    return capture_back_buffer(
        device, L"WorldWarVR-smoke-before-present.bmp");
}

} // namespace wawvr::mod
