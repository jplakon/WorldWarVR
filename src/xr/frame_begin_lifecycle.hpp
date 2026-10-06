// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <string_view>

namespace wawvr::xr
{

// Delaying Begin past native D3D9 rendering is an interop experiment, not
// general OpenXR guidance. Never opt other physical runtimes into it.
[[nodiscard]] constexpr bool defer_graphics_begin_for_runtime(
    const bool requested, const bool simulator_test,
    const std::string_view runtime_name) noexcept
{
    return requested &&
        (runtime_name == "VirtualDesktopXR" ||
         (simulator_test && runtime_name == "OpenXR Simulator Runtime"));
}

// A successful Wait must be followed by Begin before another Wait. A failed
// Begin poisons the cycle until session destruction; retrying Wait can hang.
// This object tracks native call ownership only, not render/capture metadata.
class FrameBeginLifecycle final
{
public:
    [[nodiscard]] constexpr bool can_wait() const noexcept
    { return phase_ == Phase::idle; }
    [[nodiscard]] constexpr bool active() const noexcept
    { return phase_ == Phase::waited || phase_ == Phase::begun; }
    [[nodiscard]] constexpr bool needs_begin() const noexcept
    { return phase_ == Phase::waited; }
    [[nodiscard]] constexpr bool begun() const noexcept
    { return phase_ == Phase::begun; }

    constexpr bool waited() noexcept
    {
        if (!can_wait()) return false;
        phase_ = Phase::waited;
        return true;
    }
    constexpr bool begin_completed(const bool success) noexcept
    {
        if (!needs_begin()) return false;
        phase_ = success ? Phase::begun : Phase::failed;
        return success;
    }
    constexpr bool end_completed(const bool success) noexcept
    {
        if (!begun()) return false;
        phase_ = success ? Phase::idle : Phase::failed;
        return success;
    }

private:
    enum class Phase : std::uint8_t { idle, waited, begun, failed };
    Phase phase_{Phase::idle};
};

} // namespace wawvr::xr
