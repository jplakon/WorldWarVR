#include "t4_presentation_state.hpp"

#include "t4/profile.hpp"

#include <windows.h>

#include <atomic>
#include <cstring>

namespace wawvr::mod {
namespace {

std::atomic<std::uintptr_t> g_key_catchers_address{0};
std::atomic<std::uintptr_t> g_connection_state_address{0};
std::atomic<std::int32_t> g_active_connection_state{10};

[[nodiscard]] bool readable_range(
    const void* const address, const std::size_t size) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    const DWORD access = memory.Protect & 0xffU;
    const bool readable =
        access == PAGE_READONLY || access == PAGE_READWRITE ||
        access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
        access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
    if (!readable) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto region_begin =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto region_end = region_begin + memory.RegionSize;
    return begin >= region_begin && begin <= region_end &&
           size <= region_end - begin;
}

}  // namespace

bool bind_t4_presentation_state(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    clear_t4_presentation_state();
    const auto key_catchers = bindings.data_address(
        wawvr::t4::DataSymbolId::key_catchers, sizeof(std::uint32_t));
    const auto connection_state = bindings.data_address(
        wawvr::t4::DataSymbolId::connection_state, sizeof(std::int32_t));
    if (!key_catchers.has_value() || !connection_state.has_value() ||
        !readable_range(
            reinterpret_cast<const void*>(*key_catchers),
            sizeof(std::uint32_t)) ||
        !readable_range(
            reinterpret_cast<const void*>(*connection_state),
            sizeof(std::int32_t))) {
        return false;
    }
    g_connection_state_address.store(
        *connection_state, std::memory_order_release);
    // Live telemetry from both exact supported executables establishes
    // CA_ACTIVE as 10. MP walks 5/7/6/8 while loading, then reaches 10 for
    // team/class UI and remains 10 after its catcher clears.
    g_active_connection_state.store(10, std::memory_order_release);
    g_key_catchers_address.store(*key_catchers, std::memory_order_release);
    return true;
}

T4PresentationState read_t4_presentation_state() noexcept {
    T4PresentationState state{};
    state.active_connection_state =
        g_active_connection_state.load(std::memory_order_acquire);
    const std::uintptr_t key_catchers =
        g_key_catchers_address.load(std::memory_order_acquire);
    const std::uintptr_t connection_state =
        g_connection_state_address.load(std::memory_order_acquire);
    if (key_catchers == 0 || connection_state == 0 ||
        !readable_range(
            reinterpret_cast<const void*>(key_catchers),
            sizeof(state.key_catchers)) ||
        !readable_range(
            reinterpret_cast<const void*>(connection_state),
            sizeof(state.connection_state))) {
        return state;
    }
    std::memcpy(
        &state.key_catchers,
        reinterpret_cast<const void*>(key_catchers),
        sizeof(state.key_catchers));
    std::memcpy(
        &state.connection_state,
        reinterpret_cast<const void*>(connection_state),
        sizeof(state.connection_state));
    state.valid = true;
    return state;
}

void clear_t4_presentation_state() noexcept {
    g_key_catchers_address.store(0, std::memory_order_release);
    g_connection_state_address.store(0, std::memory_order_release);
    g_active_connection_state.store(10, std::memory_order_release);
}

}  // namespace wawvr::mod
