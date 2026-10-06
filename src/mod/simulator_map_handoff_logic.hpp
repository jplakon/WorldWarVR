#pragma once

#include <cstdint>

namespace wawvr::mod {

struct SimulatorMapHandoffPresentation final {
    bool transition_seen{};
    bool presentation_valid{};
    std::int32_t connection_state{};
    std::int32_t active_connection_state{};
};

[[nodiscard]] constexpr bool simulator_map_handoff_connection_ready(
    const SimulatorMapHandoffPresentation& presentation) noexcept {
    return presentation.transition_seen && presentation.presentation_valid &&
        presentation.connection_state == presentation.active_connection_state;
}

// Direct equips retain the strict controller-gameplay gate. Only the second
// stage of an observed map handoff may proceed from connection restoration,
// because a native catcher can legitimately remain set after Zombies loads.
[[nodiscard]] constexpr bool simulator_weapon_equip_gate_active(
    const bool controller_gameplay_active,
    const bool post_map_weapon_phase,
    const SimulatorMapHandoffPresentation& presentation) noexcept {
    return controller_gameplay_active ||
        (post_map_weapon_phase &&
         simulator_map_handoff_connection_ready(presentation));
}

}  // namespace wawvr::mod
