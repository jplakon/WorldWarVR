// SPDX-License-Identifier: GPL-3.0-only
#include "reload_pouch_projection.hpp"
#include "reload_pouch_logic.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
void check(bool ok,const char* name) { if(!ok) throw std::runtime_error(name); }
bool near(float a,float b) { return std::abs(a-b)<.0001F; }
wawvr::xr::EyeView eye(float x,float height,float pitch=0) {
    return {{{std::sin(pitch*.5F),0,0,std::cos(pitch*.5F)},
              {x,height,0}}, {-.8F,.8F,.8F,-.8F}};
}
wawvr::xr::NormalizedViewport native_destination(const wawvr::xr::EyeView& eye) {
    const auto& f=eye.fov;
    const float l=std::tan(f.angle_left),r=std::tan(f.angle_right);
    const float u=std::tan(f.angle_up),d=std::tan(f.angle_down);
    const float sx=2.F*(std::max)(-l,r)/(r-l),sy=2.F*(std::max)(-d,u)/(u-d);
    return {(1.F-(r+l)/(r-l)-sx)*.5F,(1.F+(u+d)/(u-d)-sy)*.5F,sx,sy};
}
}
int main() {
    try {
        using namespace wawvr::mod;
        for(float height : {1.2F,1.7F}) {
            const auto l=eye(-.032F,height),r=eye(.032F,height);
            const wawvr::xr::Vec3f center{0,height,-1};
            const auto a=project_reload_pouch(center,l),b=project_reload_pouch(center,r);
            check(a.valid && b.valid,"binocular marker valid");
            check(a.center.x>.5F && b.center.x<.5F,"correct finite-depth binocular disparity");
            check(near(a.center.y,b.center.y),"no vertical stereo disparity");
            ReloadPouchUprightFrame upright{};
            check(make_reload_pouch_upright_frame({{}, {0,height,0}},&upright),"upright frame");
            const auto pouch=reload_pouch_hip_marker_tracking(upright,true);
            const auto level=project_reload_pouch(pouch,l);
            check(!level.valid,"forward gaze never pins an offscreen hip icon to the headset corner");
            const auto looking_down=project_reload_pouch(pouch,eye(-.032F,height,-1.2F));
            const auto down_right=project_reload_pouch(pouch,eye(.032F,height,-1.2F));
            check(looking_down.valid && down_right.valid,
                "looking down reveals actual hip location in both eyes");
            check(looking_down.center.x<.5F && looking_down.center.x>down_right.center.x,
                "left-hip location preserves finite-depth binocular disparity");
            check(looking_down.radius.x<=.025F && looking_down.radius.y<=.025F,
                "compact marker stays smaller than the old six-centimetre-radius cue");
            const auto steep_down=project_reload_pouch(pouch,eye(-.032F,height,-1.5707963F));
            check(steep_down.valid,"straight-down gaze reveals hip without a singularity");
            const auto partial=project_reload_pouch({-1.01F,height,-1},eye(0,height));
            check(partial.valid && partial.center.x<.02F,
                "near-edge point remains at its exact projection instead of being clamped inward");
            const auto standing=project_reload_pouch({-.2F,height-.3F,-.7F},l);
            const auto seated=project_reload_pouch({-.2F,1.2F-.3F,-.7F},eye(-.032F,1.2F));
            check(near(standing.center.x,seated.center.x) && near(standing.center.y,seated.center.y),
                  "seated reach has identical normalized visual position");
            for (const float yaw : {-2.4F,-1.57F,0.F,1.57F,2.4F}) {
                const wawvr::xr::Quaternionf turn{0,std::sin(yaw*.5F),0,std::cos(yaw*.5F)};
                ReloadPouchUprightFrame yaw_frame{};
                check(make_reload_pouch_upright_frame({turn,{0,height,0}},&yaw_frame),
                    "head-yaw inventory frame is valid");
                const auto yaw_hip=reload_pouch_hip_marker_tracking(yaw_frame,true);
                for(const float ipd : {-.032F,.032F}) {
                    auto yaw_eye=eye(ipd,height,-1.2F);
                    yaw_eye.pose.orientation=wawvr::xr::Multiply(turn,yaw_eye.pose.orientation);
                    const auto rotated_eye=wawvr::xr::Rotate(turn,{ipd,0,0});
                    yaw_eye.pose.position={rotated_eye.x,height,rotated_eye.z};
                    const auto result=project_reload_pouch(yaw_hip,yaw_eye);
                    const auto expected=project_reload_pouch(pouch,eye(ipd,height,-1.2F));
                    check(result.valid && near(result.center.x,expected.center.x) &&
                        near(result.center.y,expected.center.y),
                        "looking down while turning keeps visual hip and actual head-yaw pickup frame together");
                }
            }
        }
        auto bad=eye(0,1.7F);
        bad.fov.angle_left=0;
        check(!project_reload_pouch({0,1.7F,-1},bad).valid,"reject degenerate FoV");
        bad=eye(0,1.7F);bad.pose.orientation={0,0,0,0};
        check(!project_reload_pouch({0,1.7F,-1},bad).valid,"reject invalid orientation");
        check(!project_reload_pouch({0,1.7F,1},eye(0,1.7F)).valid,"hide points behind eye");
        check(!project_reload_pouch({std::numeric_limits<float>::quiet_NaN(),0,0},eye(0,1.7F)).valid,
              "reject nonfinite marker");
        check(!project_reload_pouch({-10,-5,-1},eye(0,1.7F)).valid,
              "offscreen point is hidden rather than drawn as a persistent HUD cue");
        // The actual Quest2 Tester FoVs expose the former one-eye crop:
        // runtime UV painted as source UV was remapped for a second time.
        for(const bool left : {true,false}) {
            auto view=eye(left?-.032F:.032F,1.7F,-1.22173047F);
            view.fov={left?-.942477763F:-.69813168F,left?.69813168F:.942477763F,
                .767595768F,-.947190166F};
            const auto destination=native_destination(view);
            const wawvr::xr::Vec3f hip{-.254F,1.7F-.5842F,-.1778F};
            const auto final=project_reload_pouch(hip,view);
            const auto source=project_reload_pouch(hip,view,destination);
            check(final.valid && source.valid &&
                near(destination.x+source.center.x*destination.width,final.center.x) &&
                near(destination.y+source.center.y*destination.height,final.center.y) &&
                near(source.radius.x*destination.width,final.radius.x) &&
                near(source.radius.y*destination.height,final.radius.y),
                "inverse native source mapping preserves true binocular hip point and size after compositor remap");
            const auto p=wawvr::xr::Rotate(wawvr::xr::Conjugate(view.pose.orientation),
                {hip.x-view.pose.position.x,hip.y-view.pose.position.y,hip.z-view.pose.position.z});
            const float tan_x=(std::max)(-std::tan(view.fov.angle_left),std::tan(view.fov.angle_right));
            const float tan_y=(std::max)(-std::tan(view.fov.angle_down),std::tan(view.fov.angle_up));
            check(near(source.center.x,.5F*(1.F+p.x/(-p.z*tan_x))) &&
                near(source.center.y,.5F*(1.F-p.y/(-p.z*tan_y))),
                "source pixels agree with actual symmetric native eye projection, not twice-shifted runtime UV");
            if(!left) check(destination.x+final.center.x*destination.width<0.F,
                "regression fixture reproduces the old right-eye marker crop");
        }
        for(const wawvr::xr::NormalizedViewport destination : {
            wawvr::xr::NormalizedViewport{0,0,0,1},{0,0,-1,1},{0,0,1,0},
            {0,0,1,9},{9,0,1,1},{0,std::numeric_limits<float>::quiet_NaN(),1,1},
            {.8F,0,.1F,1}})
            check(!project_reload_pouch({0,1.7F,-1},eye(0,1.7F),destination).valid,
                "invalid remap or physical point outside native source crop fails closed");
        std::puts("reload pouch projection: PASS (standing/seated, both eyes, reach, invalid poses)");
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
