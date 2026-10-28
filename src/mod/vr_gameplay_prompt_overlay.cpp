// SPDX-License-Identifier: GPL-3.0-only
#include "vr_gameplay_prompt_overlay.hpp"

#include "vr_gameplay_prompts.hpp"
#include "automatic_reload_setting.hpp"
#include "bolt_action_weapon_profile.hpp"
#include "detachable_magazine_weapon_profile.hpp"
#include "manual_grenade_runtime.hpp"
#include "manual_reload_runtime.hpp"
#include "reload_pouch_projection.hpp"
#include "rocket_barrage_logic.hpp"
#include "stereo_diagnostics.hpp"
#include "t4_menu_input.hpp"

#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

namespace wawvr::mod {
namespace {
using Microsoft::WRL::ComPtr;
struct Publication final {
    std::array<char, 64> name{};
    bool valid{};
    bool right_gripped{};
    bool left_gripped{};
    ULONGLONG sampled{};
};
std::mutex g_publication_mutex;
Publication g_publication{};
std::atomic<VrReloadPromptStage> g_reload_stage{VrReloadPromptStage::none};
std::atomic<ULONGLONG> g_reload_sampled{};

class SceneGuard final {
public:
    explicit SceneGuard(IDirect3DDevice9* device) noexcept : device_(device),
        active_(SUCCEEDED(device_->BeginScene())) {}
    ~SceneGuard() noexcept { if (active_) device_->EndScene(); }
    bool active() const noexcept { return active_; }
private:
    IDirect3DDevice9* device_{};
    bool active_{};
};

bool enabled(const wchar_t* name) noexcept {
    std::array<wchar_t, 16> value{};
    return GetEnvironmentVariableW(name, value.data(),
        static_cast<DWORD>(value.size())) == 1 && value[0] == L'1';
}

// Original 5x7 uppercase pixel font: each row is a five-bit mask. No font
// asset, texture allocation, OS text API, locale mutation or shader is needed.
std::array<unsigned char, 7> glyph(const char c) noexcept {
    switch (c) {
    case 'A': return {14,17,17,31,17,17,17};
    case 'B': return {30,17,17,30,17,17,30};
    case 'C': return {14,17,16,16,16,17,14};
    case 'D': return {30,17,17,17,17,17,30};
    case 'E': return {31,16,16,30,16,16,31};
    case 'F': return {31,16,16,30,16,16,16};
    case 'G': return {14,17,16,23,17,17,15};
    case 'H': return {17,17,17,31,17,17,17};
    case 'I': return {14,4,4,4,4,4,14};
    case 'J': return {7,2,2,2,2,18,12};
    case 'K': return {17,18,20,24,20,18,17};
    case 'L': return {16,16,16,16,16,16,31};
    case 'M': return {17,27,21,21,17,17,17};
    case 'N': return {17,25,21,19,17,17,17};
    case 'O': return {14,17,17,17,17,17,14};
    case 'P': return {30,17,17,30,16,16,16};
    case 'Q': return {14,17,17,17,21,18,13};
    case 'R': return {30,17,17,30,20,18,17};
    case 'S': return {15,16,16,14,1,1,30};
    case 'T': return {31,4,4,4,4,4,4};
    case 'U': return {17,17,17,17,17,17,14};
    case 'V': return {17,17,17,17,17,10,4};
    case 'W': return {17,17,17,21,21,21,10};
    case 'X': return {17,17,10,4,10,17,17};
    case 'Y': return {17,17,10,4,4,4,4};
    case 'Z': return {31,1,2,4,8,16,31};
    case '0': return {14,17,19,21,25,17,14};
    case '1': return {4,12,4,4,4,4,14};
    case '2': return {14,17,1,2,4,8,31};
    case '3': return {30,1,1,14,1,1,30};
    case '4': return {2,6,10,18,31,2,2};
    case '5': return {31,16,16,30,1,1,30};
    case '6': return {14,16,16,30,17,17,14};
    case '7': return {31,1,2,4,8,8,8};
    case '8': return {14,17,17,14,17,17,14};
    case '9': return {14,17,17,15,1,1,14};
    case ':': return {0,4,4,0,4,4,0};
    case ';': return {0,4,4,0,4,4,8};
    case '-': return {0,0,0,14,0,0,0};
    case '+': return {0,4,4,31,4,4,0};
    case '/': return {1,1,2,4,8,16,16};
    case '|': return {4,4,4,4,4,4,4};
    default: return {};
    }
}

struct Vertex final { float x,y,z,rhw; D3DCOLOR color; };
void rectangle(std::vector<Vertex>& vertices, const float x,
               const float y, const float width, const float height,
               const D3DCOLOR color) {
    const Vertex a{x-.5F,y-.5F,0,1,color};
    const Vertex b{x+width-.5F,y-.5F,0,1,color};
    const Vertex c{x+width-.5F,y+height-.5F,0,1,color};
    const Vertex d{x-.5F,y+height-.5F,0,1,color};
    vertices.insert(vertices.end(), {a,b,c,a,c,d});
}
void line(std::vector<Vertex>& vertices, const std::string_view text,
          const float x, const float y, const float pixel,
          const D3DCOLOR color) {
    for (std::size_t i=0; i<text.size(); ++i) {
        const auto rows = glyph(text[i]);
        for (std::size_t r=0; r<rows.size(); ++r)
            for (unsigned col=0; col<5; ++col)
                if ((rows[r] & (16U >> col)) != 0)
                    rectangle(vertices, x + static_cast<float>(i*6+col)*pixel,
                        y + static_cast<float>(r)*pixel, pixel,pixel,color);
    }
}

void stroke(std::vector<Vertex>& vertices, const float ax,const float ay,
    const float bx,const float by,const float thickness,const D3DCOLOR color) {
    const float length=std::hypot(bx-ax,by-ay);
    if (length < .001F) return;
    const float x=-(by-ay)*thickness*.5F/length;
    const float y=(bx-ax)*thickness*.5F/length;
    const Vertex a{ax+x,ay+y,0,1,color},b{bx+x,by+y,0,1,color};
    const Vertex c{bx-x,by-y,0,1,color},d{ax-x,ay-y,0,1,color};
    vertices.insert(vertices.end(),{a,b,c,a,c,d});
}

bool pouch_geometry(std::vector<Vertex>& vertices,
    const ManualReloadPouchHint& hint,const wawvr::xr::FrameState& frame,
    const wawvr::xr::StereoSourceLayout& layout,const UINT atlas_width,
    const UINT atlas_height) {
    vertices.clear();
    for(std::size_t index=0;index<2;++index) {
        const auto first_vertex=vertices.size();
        const auto& rect=layout.eyes[index];
        if(!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
            !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
            rect.x<0 || rect.y<0 || rect.width<=0 || rect.height<=0 ||
            rect.x+rect.width>1.00001F || rect.y+rect.height>1.00001F) return false;
        const auto& destination=layout.destinations[index];
        const auto marker=project_reload_pouch(hint.center_tracking,frame.eyes[index],destination);
        // Bilateral visibility is one transaction; never show only one eye.
        if(!marker.valid) {vertices.clear();return false;}
        const float w=rect.width*atlas_width,h=rect.height*atlas_height;
        const float x=(rect.x+marker.center.x*rect.width)*atlas_width;
        const float y=(rect.y+marker.center.y*rect.height)*atlas_height;
        const float pixel=(std::max)(1.F,(std::min)(2.F,std::floor(h*.0015F)));
        const float rx=(std::max)(4.F*pixel,marker.radius.x*w);
        const float ry=(std::max)(4.F*pixel,marker.radius.y*h);
        if(w<200 || h<160 || rx>w*.1F || ry>h*.1F) {vertices.clear();return false;}
        const auto color=hint.hovered ? D3DCOLOR_ARGB(255,90,255,160)
                                     : D3DCOLOR_ARGB(240,255,215,125);
        rectangle(vertices,x-rx-pixel*2,y-ry-pixel*2,rx*2+pixel*4,ry*2+pixel*4,
            D3DCOLOR_ARGB(145,8,12,16));
        // A pouch outline with three ammunition bars, not a fake magazine.
        for(unsigned edge=0;edge<4;++edge) {
            const std::array<wawvr::xr::Vec2f,4> p{{{x-rx,y-ry},{x+rx,y-ry},
                {x+rx,y+ry},{x-rx,y+ry}}};
            stroke(vertices,p[edge].x,p[edge].y,p[(edge+1)%4].x,p[(edge+1)%4].y,pixel,color);
        }
        for(int bar=-1;bar<=1;++bar)
            rectangle(vertices,x+bar*rx*.45F-pixel*.5F,y-ry*.6F,pixel,ry*1.2F,color);
        const std::string_view label=hint.hovered ? "GRIP" : "AMMO";
        const float text_width=static_cast<float>(label.size()*6)*pixel;
        if(text_width+pixel*4>w) {vertices.clear();return false;}
        const float text_x=x-text_width*.5F;
        rectangle(vertices,text_x-pixel*2,y+ry+pixel*2,
            text_width+pixel*4,pixel*10,D3DCOLOR_ARGB(180,8,12,16));
        line(vertices,label,text_x,y+ry+pixel*3,pixel,color);
        for(std::size_t v=first_vertex;v<vertices.size();++v) {
            const auto& p=vertices[v];
            if(!std::isfinite(p.x) || !std::isfinite(p.y) ||
                p.x<rect.x*atlas_width-.5F || p.x>(rect.x+rect.width)*atlas_width-.5F ||
                p.y<rect.y*atlas_height-.5F || p.y>(rect.y+rect.height)*atlas_height-.5F) {
                vertices.clear();return false;
            }
            // The entire compact cue must survive the compositor crop in
            // both eyes, not merely fit inside the pre-remap source atlas.
            const float output_u=destination.x+(p.x-rect.x*atlas_width)/w*destination.width;
            const float output_v=destination.y+(p.y-rect.y*atlas_height)/h*destination.height;
            if(!std::isfinite(output_u) || !std::isfinite(output_v) ||
                output_u<0.F || output_u>1.F || output_v<0.F || output_v>1.F) {
                vertices.clear();return false;
            }
        }
    }
    return !vertices.empty();
}

class StateGuard final {
public:
    explicit StateGuard(IDirect3DDevice9* device) noexcept : device_(device) {
        D3DCAPS9 caps{};
        if (FAILED(device_->GetDeviceCaps(&caps)) ||
            FAILED(device_->CreateStateBlock(D3DSBT_ALL, &state_)) ||
            FAILED(state_->Capture()) ||
            FAILED(device_->GetViewport(&viewport_))) return;
        target_count_ = (std::min)(caps.NumSimultaneousRTs, DWORD{4});
        for (DWORD i=0; i<target_count_; ++i) {
            const auto result = device_->GetRenderTarget(i, &targets_[i]);
            if (FAILED(result) && (i==0 || result != D3DERR_NOTFOUND)) return;
        }
        const auto depth_result = device_->GetDepthStencilSurface(&depth_);
        if (FAILED(depth_result) && depth_result != D3DERR_NOTFOUND) return;
        valid_ = true;
    }
    ~StateGuard() noexcept {
        if (!valid_) return;
        device_->SetDepthStencilSurface(nullptr);
        for (DWORD i=1; i<target_count_; ++i) device_->SetRenderTarget(i, nullptr);
        for (DWORD i=0; i<target_count_; ++i)
            device_->SetRenderTarget(i, targets_[i].Get());
        device_->SetDepthStencilSurface(depth_.Get());
        state_->Apply();
        device_->SetViewport(&viewport_);
    }
    bool valid() const noexcept { return valid_; }
    DWORD target_count() const noexcept { return target_count_; }
private:
    IDirect3DDevice9* device_{};
    ComPtr<IDirect3DStateBlock9> state_{};
    std::array<ComPtr<IDirect3DSurface9>,4> targets_{};
    ComPtr<IDirect3DSurface9> depth_{};
    D3DVIEWPORT9 viewport_{};
    DWORD target_count_{};
    bool valid_{};
};

VrReloadPromptFamily family(const std::string_view name) noexcept {
    if (find_bolt_action_weapon_profile_by_internal_name(name) != nullptr)
        return VrReloadPromptFamily::stripper_clip;
    if (name == "m1garand" || name == "m1garand_bayonet" ||
        name == "m1garand_gl") return VrReloadPromptFamily::en_bloc;
    if (has_detachable_magazine_weapon_profile_for_internal_name(name))
        return VrReloadPromptFamily::detachable_magazine;
    return VrReloadPromptFamily::native;
}
} // namespace

void publish_vr_gameplay_prompt_weapon(const std::string_view name,
    const bool valid, const bool right_gripped, const bool left_gripped) noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_publication_mutex);
        g_publication = {};
        if (valid && !name.empty() && name.size() < g_publication.name.size() &&
            name.find('\0') == std::string_view::npos) {
            std::copy(name.begin(), name.end(), g_publication.name.begin());
            g_publication.valid = true;
            g_publication.right_gripped = right_gripped;
            g_publication.left_gripped = left_gripped;
        }
        g_publication.sampled = GetTickCount64();
    } catch (...) {}
}

void publish_vr_gameplay_reload_hint(const VrReloadPromptStage stage) noexcept {
    g_reload_stage.store(stage, std::memory_order_relaxed);
    g_reload_sampled.store(GetTickCount64(), std::memory_order_release);
}

bool vr_gameplay_prompt_support_weapon_selected() noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_publication_mutex);
        const auto now=GetTickCount64();
        if (!g_publication.valid || now < g_publication.sampled ||
            now-g_publication.sampled > 500) return false;
        const std::string_view name(g_publication.name.data());
        return name == kRocketBarrageWeaponName || name == kAirSupportWeaponName;
    } catch (...) { return false; }
}

void draw_vr_gameplay_prompt_overlay(IDirect3DDevice9* const device,
    const wawvr::xr::StereoSourceLayout& layout,
    const wawvr::xr::FrameState& frame,
    const bool active_stereo_gameplay, const bool multiplayer) noexcept {
    static const bool disabled = enabled(L"WAWVR_DISABLE_VR_PROMPTS");
    static const bool automatic_reload = enabled(kAutomaticReloadEnvironmentVariable);
    static const bool stick_melee = enabled(L"WAWVR_RIGHT_STICK_MELEE");
    static ULONGLONG entered{};
    static VrGameplayPrompt last{};
    if (disabled || device == nullptr || !active_stereo_gameplay ||
        !frame.actions.focused || !frame.should_render || !frame.views_valid) {
        entered = 0;
        return;
    }
    const auto now = GetTickCount64();
    if (entered == 0) entered = now;
    try {
        Publication publication{};
        {
            std::lock_guard<std::mutex> lock(g_publication_mutex);
            publication = g_publication;
        }
        const bool weapon_fresh = publication.valid && now >= publication.sampled &&
            now-publication.sampled <= 500;
        const std::string_view name = weapon_fresh
            ? std::string_view(publication.name.data()) : std::string_view{};
        const auto sampled = g_reload_sampled.load(std::memory_order_acquire);
        const auto stage = sampled != 0 && now >= sampled && now-sampled <= 500
            ? g_reload_stage.load(std::memory_order_relaxed)
            : VrReloadPromptStage::none;
        const auto& secondary = frame.actions.hands[0].secondary;
        const VrGameplayPrompt prompt = build_vr_gameplay_prompt({
            .active_stereo_gameplay = true,
            .input_focused = true,
            .introduction_visible = now-entered <= 10000,
            .help_held = secondary.active && secondary.current,
            .automatic_reload = automatic_reload,
            .button_grenades = button_grenade_mode_enabled(),
            .right_stick_melee = stick_melee,
            .multiplayer = multiplayer,
            .support_weapon_selected = name == kRocketBarrageWeaponName ||
                name == kAirSupportWeaponName,
            .enemy_grenade_available = contextual_manual_grenade_throwback_available() &&
                read_manual_grenade_throwback_time_left() > 0,
            .reload_family = family(name),
            .reload_stage = stage,
            .left_binding_profile = frame.actions.hands[0].binding_profile,
            .right_binding_profile = frame.actions.hands[1].binding_profile,
        });
        ManualReloadPouchHint pouch{};
        bool pouch_visible=!automatic_reload && !multiplayer &&
            read_manual_reload_pouch_hint(&pouch) && pouch.available &&
            pouch.action_sequence!=0 && pouch.action_sequence<=frame.actions.sequence &&
            now>=pouch.published_milliseconds && now-pouch.published_milliseconds<=100;
        if(pouch_visible) {
            const auto& hand=frame.actions.hands[pouch.left_hand ? 0:1].grip;
            pouch_visible=hand.active && hand.position_valid && hand.orientation_valid &&
                std::isfinite(hand.pose.position.x) && std::isfinite(hand.pose.position.y) &&
                std::isfinite(hand.pose.position.z);
        }
        if (prompt.kind == VrGameplayPromptKind::none && !pouch_visible) { last = prompt; return; }
        ComPtr<IDirect3DSurface9> backbuffer;
        D3DSURFACE_DESC desc{};
        if (FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer)) ||
            FAILED(backbuffer->GetDesc(&desc)) || !desc.Width || !desc.Height) return;
        StateGuard restore(device);
        if (!restore.valid()) return;
        SceneGuard scene(device);
        if (!scene.active()) return;
        bool ok = SUCCEEDED(device->SetDepthStencilSurface(nullptr));
        for (DWORD i=1; i<restore.target_count(); ++i)
            ok = SUCCEEDED(device->SetRenderTarget(i,nullptr)) && ok;
        ok = SUCCEEDED(device->SetRenderTarget(0,backbuffer.Get())) && ok;
        const D3DVIEWPORT9 viewport{0,0,desc.Width,desc.Height,0,1};
        ok = SUCCEEDED(device->SetViewport(&viewport)) && ok;
        ok = SUCCEEDED(device->SetVertexShader(nullptr)) && ok;
        ok = SUCCEEDED(device->SetPixelShader(nullptr)) && ok;
        ok = SUCCEEDED(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE)) && ok;
        ok = SUCCEEDED(device->SetTexture(0,nullptr)) && ok;
        const std::array<std::pair<D3DRENDERSTATETYPE,DWORD>,17> states{{
            {D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},
            {D3DRS_STENCILENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},
            {D3DRS_ALPHABLENDENABLE,TRUE},{D3DRS_SRCBLEND,D3DBLEND_SRCALPHA},
            {D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA},{D3DRS_BLENDOP,D3DBLENDOP_ADD},
            {D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},
            {D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_LIGHTING,FALSE},
            {D3DRS_FOGENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},
            {D3DRS_COLORWRITEENABLE,15},{D3DRS_SRGBWRITEENABLE,FALSE},
            {D3DRS_CLIPPING,TRUE},
        }};
        for (const auto& [state,value] : states)
            ok = SUCCEEDED(device->SetRenderState(state,value)) && ok;
        ok = SUCCEEDED(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1)) && ok;
        ok = SUCCEEDED(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE)) && ok;
        ok = SUCCEEDED(device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1)) && ok;
        ok = SUCCEEDED(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE)) && ok;
        ok = SUCCEEDED(device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE)) && ok;
        // Cache both geometry and storage. A static help panel must not
        // rebuild thousands of glyph rectangles on every rendered frame.
        static std::vector<Vertex> vertices;
        static VrGameplayPrompt geometry_prompt{};
        static std::array<wawvr::xr::NormalizedRect,2> geometry_eyes{};
        static UINT geometry_width{},geometry_height{};
        const std::string_view first(prompt.first.data()),second(prompt.second.data());
        const auto longest = (std::max)(std::size_t{1},(std::max)(first.size(),second.size()));
        bool prompt_geometry_ok=ok;
        if (geometry_prompt != prompt || geometry_width != desc.Width ||
            geometry_height != desc.Height || std::memcmp(geometry_eyes.data(),
                layout.eyes,sizeof(layout.eyes)) != 0) {
            vertices.clear();
            if (vertices.capacity() == 0)
                vertices.reserve(kVrPromptLineBytes*7*5*6*4);
            if(prompt.kind != VrGameplayPromptKind::none) for (const auto& eye : layout.eyes) {
                if (!std::isfinite(eye.x) || !std::isfinite(eye.y) ||
                    !std::isfinite(eye.width) || !std::isfinite(eye.height) ||
                    eye.x < 0 || eye.y < 0 || eye.width <= 0 || eye.height <= 0 ||
                    eye.x+eye.width > 1.00001F || eye.y+eye.height > 1.00001F) {
                    vertices.clear(); prompt_geometry_ok=false; break;
                }
                const float width=eye.width*desc.Width,height=eye.height*desc.Height;
                const float pixel=(std::max)(1.F,std::floor((std::min)(
                    height*.0038F,width*.64F/static_cast<float>(longest*6))));
                // Tiny recovery/test viewports must not let a minimum-size
                // glyph overflow into the other eye or the optic strip.
                if (static_cast<float>(longest*6)*pixel > width*.92F) {
                    vertices.clear(); prompt_geometry_ok=false; break;
                }
                const float x=eye.x*desc.Width+width*.5F;
                // Retrievable held help belongs near the binocular center,
                // not the high transient-hint band. Leave every other prompt
                // (including radio aiming and reloads) at its accepted height.
                const bool held_help = prompt.kind == VrGameplayPromptKind::controls ||
                    prompt.kind == VrGameplayPromptKind::support_selector;
                const float y=eye.y*desc.Height+height*(held_help ? .42F : .17F);
                rectangle(vertices,x-width*.34F,y-pixel*2,width*.68F,pixel*21,
                    D3DCOLOR_ARGB(160,8,12,16));
                const auto color = prompt.kind == VrGameplayPromptKind::enemy_grenade
                    ? D3DCOLOR_ARGB(255,255,215,125) : D3DCOLOR_ARGB(255,238,246,255);
                line(vertices,first,x-static_cast<float>(first.size()*6)*pixel*.5F,y,pixel,color);
                line(vertices,second,x-static_cast<float>(second.size()*6)*pixel*.5F,y+pixel*10,pixel,color);
            }
            if (prompt_geometry_ok) {
                geometry_prompt=prompt;
                geometry_width=desc.Width; geometry_height=desc.Height;
                std::copy(std::begin(layout.eyes),std::end(layout.eyes),geometry_eyes.begin());
            }
        }
        const bool drawn = prompt_geometry_ok && !vertices.empty() && SUCCEEDED(device->DrawPrimitiveUP(
            D3DPT_TRIANGLELIST,static_cast<UINT>(vertices.size()/3),
            vertices.data(),sizeof(Vertex)));
        if (drawn && prompt != last) {
            stereo_diagnostic_log("VRPromptDiag stereo guidance kind=%u eyes=2 text1=%s text2=%s",
                static_cast<unsigned>(prompt.kind),prompt.first.data(),prompt.second.data());
            last=prompt;
        }
        static std::vector<Vertex> pouch_vertices;
        static unsigned last_pouch_state{};
        unsigned pouch_state{};
        if(ok && pouch_visible && pouch_geometry(pouch_vertices,pouch,frame,layout,desc.Width,desc.Height) &&
            SUCCEEDED(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
                static_cast<UINT>(pouch_vertices.size()/3),pouch_vertices.data(),sizeof(Vertex)))) {
            pouch_state=1U | (pouch.hovered ? 2U : 0U) | (pouch.left_hand ? 4U : 0U);
        }
        if(pouch_state != last_pouch_state) {
            stereo_diagnostic_log("PouchOverlayDiag visible=%u hovered=%u left=%u eyes=%u sequence=%llu",
                pouch_state ? 1U:0U,pouch.hovered ? 1U:0U,pouch.left_hand ? 1U:0U,
                pouch_state ? 2U:0U,static_cast<unsigned long long>(pouch.action_sequence));
            last_pouch_state=pouch_state;
        }
    } catch (...) {
        // The native rendering/capture path remains the authority on failure.
    }
}
} // namespace wawvr::mod
