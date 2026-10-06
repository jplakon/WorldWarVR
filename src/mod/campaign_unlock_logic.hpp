// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string_view>

namespace wawvr::mod {

// World at War's Mission Select reads the archived mis_01 mission-progression
// high-water mark.  The COD4 mis_cheat flag does not unlock WaW's mission
// list.  Queue the WaW-specific unlock from the validated post-Com_Frame path
// so profile initialization cannot replace an earlier command-line value.
inline constexpr std::string_view kCampaignUnlockCommand{
    "seta mis_01 50\n"};

enum class CampaignUnlockDisposition {
    not_applicable,
    wait_for_engine_state,
    wait_for_command_buffer,
    queue_now,
    already_queued,
};

[[nodiscard]] constexpr CampaignUnlockDisposition
campaign_unlock_disposition(
    const bool single_player_profile,
    const bool presentation_state_valid,
    const bool command_buffer_available,
    const bool already_queued) noexcept {
    if (!single_player_profile) {
        return CampaignUnlockDisposition::not_applicable;
    }
    if (already_queued) {
        return CampaignUnlockDisposition::already_queued;
    }
    if (!presentation_state_valid) {
        return CampaignUnlockDisposition::wait_for_engine_state;
    }
    if (!command_buffer_available) {
        return CampaignUnlockDisposition::wait_for_command_buffer;
    }
    return CampaignUnlockDisposition::queue_now;
}

}  // namespace wawvr::mod
