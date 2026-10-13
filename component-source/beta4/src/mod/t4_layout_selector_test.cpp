#include "t4_layout_selector.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using wawvr::mod::T4LayoutFamily;
    using wawvr::mod::select_t4_layout_family;
    using wawvr::mod::t4_layout_family_from_id;
    using wawvr::t4::ExecutableLayoutId;
    using wawvr::t4::ExecutableVariant;

    check(t4_layout_family_from_id(
              ExecutableLayoutId::t4_sp_1_7_1263) ==
              T4LayoutFamily::single_player_1_7_1263,
          "SP layout ID must select the SP layout family");
    check(t4_layout_family_from_id(
              ExecutableLayoutId::t4_mp_1_7_1263) ==
              T4LayoutFamily::multiplayer_1_7_1263,
          "MP layout ID must select the MP layout family");

    check(select_t4_layout_family(
              ExecutableLayoutId::t4_sp_1_7_1263,
              ExecutableVariant::single_player) ==
              T4LayoutFamily::single_player_1_7_1263,
          "matching SP layout and variant must be accepted");
    check(select_t4_layout_family(
              ExecutableLayoutId::t4_mp_1_7_1263,
              ExecutableVariant::multiplayer) ==
              T4LayoutFamily::multiplayer_1_7_1263,
          "matching MP layout and variant must be accepted");

    check(select_t4_layout_family(
              ExecutableLayoutId::t4_sp_1_7_1263,
              ExecutableVariant::multiplayer) ==
              T4LayoutFamily::unsupported,
          "SP layout with MP variant must fail closed");
    check(select_t4_layout_family(
              ExecutableLayoutId::t4_mp_1_7_1263,
              ExecutableVariant::single_player) ==
              T4LayoutFamily::unsupported,
          "MP layout with SP variant must fail closed");

    const auto unknown = static_cast<ExecutableLayoutId>(
        static_cast<std::uint8_t>(0xFF));
    check(t4_layout_family_from_id(unknown) ==
              T4LayoutFamily::unsupported,
          "unknown layout ID must fail closed");
    check(select_t4_layout_family(
              unknown, ExecutableVariant::single_player) ==
              T4LayoutFamily::unsupported,
          "unknown profile layout must fail closed");
}
