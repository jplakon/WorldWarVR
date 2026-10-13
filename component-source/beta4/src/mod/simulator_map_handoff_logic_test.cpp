#include "simulator_map_handoff_logic.hpp"

#include <cstdio>

namespace {

int failures = 0;

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::fprintf(stderr, "simulator map handoff test failed: %s\n", message);
        ++failures;
    }
}

}  // namespace

int main() {
    using namespace wawvr::mod;

    SimulatorMapHandoffPresentation presentation{
        true, true, 10, 10};
    check(simulator_map_handoff_connection_ready(presentation),
          "an observed transition ending at CA_ACTIVE is ready");
    check(simulator_weapon_equip_gate_active(false, true, presentation),
          "post-map weapon equip does not depend on controller ownership");
    check(!simulator_weapon_equip_gate_active(false, false, presentation),
          "direct equips cannot bypass the controller-gameplay gate");

    presentation.transition_seen = false;
    check(!simulator_map_handoff_connection_ready(presentation),
          "an active connection without an observed transition is rejected");
    presentation.transition_seen = true;
    presentation.presentation_valid = false;
    check(!simulator_map_handoff_connection_ready(presentation),
          "an invalid presentation sample is rejected");
    presentation.presentation_valid = true;
    presentation.connection_state = 8;
    check(!simulator_map_handoff_connection_ready(presentation),
          "a still-loading connection is rejected");
    check(simulator_weapon_equip_gate_active(true, false, presentation),
          "ordinary active gameplay retains its existing direct-equip path");

    return failures == 0 ? 0 : 1;
}
