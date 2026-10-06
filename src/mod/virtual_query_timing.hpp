// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <windows.h>

#include <cstddef>

namespace wawvr::mod {

enum class VirtualQueryTimingGroup : std::size_t {
    weapon_identity,
    vehicle_input,
    manual_reload,
    weapon_hands,
    presentation,
    hud,
    stereo_backend,
    count
};

// Explicit call-site diagnostics only; never an API hook or memory-facts cache.
SIZE_T timed_virtual_query(VirtualQueryTimingGroup group, LPCVOID address,
                          PMEMORY_BASIC_INFORMATION memory, SIZE_T size) noexcept;

template <VirtualQueryTimingGroup Group>
SIZE_T WINAPI grouped_virtual_query(LPCVOID address,
                                   PMEMORY_BASIC_INFORMATION memory,
                                   SIZE_T size) noexcept {
#if defined(WAWVR_BUILD_DLL)
    return timed_virtual_query(Group, address, memory, size);
#else
    // Standalone validation tests retain the native API or their injected query.
    return VirtualQuery(address, memory, size);
#endif
}

} // namespace wawvr::mod
