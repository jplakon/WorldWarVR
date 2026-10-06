// SPDX-License-Identifier: GPL-3.0-only
#include "campaign_unlock_logic.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}  // namespace

int main() {
    using wawvr::mod::CampaignUnlockDisposition;
    using wawvr::mod::campaign_unlock_disposition;
    using wawvr::mod::kCampaignUnlockCommand;

    expect(
        campaign_unlock_disposition(false, true, true, false) ==
            CampaignUnlockDisposition::not_applicable,
        "multiplayer must never receive the campaign unlock command");
    expect(
        campaign_unlock_disposition(true, false, true, false) ==
            CampaignUnlockDisposition::wait_for_engine_state,
        "single player must wait until engine presentation is initialized");
    expect(
        campaign_unlock_disposition(true, true, false, false) ==
            CampaignUnlockDisposition::wait_for_command_buffer,
        "a missing validated command-buffer binding must retry later");
    expect(
        campaign_unlock_disposition(true, true, true, false) ==
            CampaignUnlockDisposition::queue_now,
        "initialized single player must queue the stock campaign unlock");
    expect(
        campaign_unlock_disposition(true, true, true, true) ==
            CampaignUnlockDisposition::already_queued,
        "a successful unlock must not be queued every frame");

    expect(
        kCampaignUnlockCommand == "seta mis_01 50\n",
        "campaign unlock must use WaW's stock mission progression command");
    expect(
        kCampaignUnlockCommand.find("mis_cheat") == std::string_view::npos &&
            kCampaignUnlockCommand.find("mis_difficulty") ==
                std::string_view::npos &&
            kCampaignUnlockCommand.find("mis_01_unlock") ==
                std::string_view::npos &&
            kCampaignUnlockCommand.find("ui_sp_unlock") ==
                std::string_view::npos,
        "campaign unlock must change only WaW's mission high-water mark");

    return EXIT_SUCCESS;
}
