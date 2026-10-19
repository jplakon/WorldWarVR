// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace wawvr::mod {

// Bounded, read-only copy from this process only. The caller owns a valid
// destination span. A failed copy can partially modify that span: callers MUST
// NOT parse destination bytes unless this function returns true. This leaf
// preserves the caller's Win32 last-error value on every success/failure path.
[[nodiscard]] inline bool copy_grenade_observer_memory(
    void* const destination,
    const std::uintptr_t source,
    const std::size_t bytes) noexcept {
    const DWORD callerError = GetLastError();
    bool complete = false;
    const auto destinationAddress = reinterpret_cast<std::uintptr_t>(destination);
    const auto maximumAddress = (std::numeric_limits<std::uintptr_t>::max)();
    if (destination != nullptr && source != 0 && bytes != 0 && bytes <= 0x1000U &&
        bytes <= maximumAddress - source &&
        bytes <= maximumAddress - destinationAddress) {
        SIZE_T copied = 0;
        const BOOL succeeded = ReadProcessMemory(
            GetCurrentProcess(), reinterpret_cast<const void*>(source),
            destination, bytes, &copied);
        complete = succeeded != FALSE && copied == bytes;
    }
    SetLastError(callerError);
    return complete;
}

}  // namespace wawvr::mod
