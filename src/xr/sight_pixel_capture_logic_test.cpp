// SPDX-License-Identifier: GPL-3.0-only
#include "sight_pixel_capture_logic.hpp"
#include <iostream>
int main() {
    using namespace wawvr::xr;
    SightPixelCaptureState state{};
    const auto expect=[&](bool result,const char* text){if(!result) std::cerr<<text<<'\n';return result;};
    bool ok=true;
    ok&=expect(!consume_sight_pixel_capture_request({10,100,1,1000,600},10,1000,600,false,&state)&&state.attempted_assets==0,
        "missing pixel opt-in changed ordinary orientation capture state");
    ok&=expect(!consume_sight_pixel_capture_request({9,100,1,1000,600},10,1000,600,true,&state)&&
        !consume_sight_pixel_capture_request({10,0,1,1000,600},10,1000,600,true,&state)&&
        !consume_sight_pixel_capture_request({10,100,5,1000,600},10,1000,600,true,&state)&&
        !consume_sight_pixel_capture_request({10,100,1,1001,600},10,1000,600,true,&state)&&
        !consume_sight_pixel_capture_request({10,100,1,1000,600},10,1000,0,true,&state)&&state.attempted_assets==0,
        "foreign frame, missing publication or unsupported asset admitted");
    for(std::uint32_t asset=1;asset<=4;++asset) {
        ok&=expect(consume_sight_pixel_capture_request({10,100+asset,asset,1000,600},10,1000,600,true,&state)==asset,
            "exact source capture request rejected");
        ok&=expect(!consume_sight_pixel_capture_request({11,200+asset,asset,1000,600},11,1000,600,true,&state),
            "asset capture repeated after its bounded first attempt");
    }
    ok&=expect(state.attempted_assets==15,"exact four-asset capture bound changed");
    return ok?0:1;
}
