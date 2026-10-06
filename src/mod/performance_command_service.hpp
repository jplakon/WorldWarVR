// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

namespace wawvr::mod {

using PerformanceCommandQueue = bool (*)(std::uintptr_t, const char*) noexcept;

// Capture the opt-in path during binding only. No runtime file is read here.
void configure_performance_command_service(bool single_player) noexcept;
void clear_performance_command_service() noexcept;

// Only the validated WinMain post-Com_Frame caller may service this protocol.
// Physical runtimes never read or queue its command file, even if env is set.
void service_performance_command_file_after_com_frame(
    std::uintptr_t validated_cbuf_address,
    PerformanceCommandQueue queue,
    std::uint64_t now_milliseconds,
    bool presentation_valid,
    std::uint32_t key_catchers,
    std::int32_t connection_state) noexcept;

}  // namespace wawvr::mod
