#pragma once

#include <atomic>

namespace wawvr::mod {

void stereo_diagnostic_log(const char* format, ...) noexcept;

// Writes a diagnostic to WorldWarVR.log only on the first visit to a
// particular call site. Stereo rejection paths run once per rendered frame,
// so call-site-local gates keep the live probe useful without perturbing the
// renderer with continuous file I/O.
void stereo_diagnostic_log_once(
    std::atomic_flag& gate,
    const char* format,
    ...) noexcept;

} // namespace wawvr::mod

#define WAWVR_STEREO_DIAG_ONCE(...)                                      \
    do {                                                                 \
        static std::atomic_flag wawvr_stereo_diagnostic_gate =           \
            ATOMIC_FLAG_INIT;                                            \
        ::wawvr::mod::stereo_diagnostic_log_once(                        \
            wawvr_stereo_diagnostic_gate, __VA_ARGS__);                  \
    } while (false)
