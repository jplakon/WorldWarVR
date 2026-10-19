// SPDX-License-Identifier: GPL-3.0-only
#include "vr_gameplay_prompt_overlay.hpp"
#include "vr_gameplay_prompts.hpp"
#include "bolt_action_weapon_profile.hpp"
#include "detachable_magazine_weapon_profile.hpp"
#include "manual_grenade_runtime.hpp"

#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

// Renderer fixture substitutes no native engine memory or runtime. The real
// prompt builder and real D3D9 draw/state-restore implementation are linked.
namespace {
bool g_button_grenade_fixture{};
unsigned g_button_grenade_queries{};
}
namespace wawvr::mod {
const BoltActionWeaponProfile* find_bolt_action_weapon_profile_by_internal_name(
    std::string_view) noexcept { return nullptr; }
bool has_detachable_magazine_weapon_profile_for_internal_name(
    std::string_view) noexcept { return false; }
std::int32_t read_manual_grenade_throwback_time_left() noexcept { return 0; }
bool contextual_manual_grenade_throwback_available() noexcept { return false; }
bool button_grenade_mode_enabled() noexcept {
    ++g_button_grenade_queries;
    return g_button_grenade_fixture;
}
void stereo_diagnostic_log(const char*, ...) noexcept {}
} // namespace wawvr::mod

namespace {
using Microsoft::WRL::ComPtr;
void check(const bool pass, const char* message) {
    if (!pass) throw std::runtime_error(message);
}
void hr(const HRESULT result, const char* message) {
    if (FAILED(result)) {
        std::fprintf(stderr,"D3D9 result 0x%08lX: %s\n",result,message);
        throw std::runtime_error(message);
    }
}
struct Window final {
    HWND handle{};
    Window() {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc=DefWindowProcW;
        window_class.hInstance=GetModuleHandleW(nullptr);
        window_class.lpszClassName=L"WorldWarVRPrivatePromptRendererFixture";
        check(RegisterClassW(&window_class)!=0,"register private graphics fixture window");
        handle=CreateWindowExW(0,window_class.lpszClassName,L"Private renderer test",
            WS_OVERLAPPED,0,0,1280,720,nullptr,nullptr,window_class.hInstance,nullptr);
        check(handle!=nullptr,"create hidden private graphics fixture window");
    }
    ~Window() {
        if (handle) DestroyWindow(handle);
        UnregisterClassW(L"WorldWarVRPrivatePromptRendererFixture",GetModuleHandleW(nullptr));
    }
};

void test_renderer_pixels_and_native_state_restore() {
    Window window;
    ComPtr<IDirect3D9> d3d;
    d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));
    check(d3d!=nullptr,"create private D3D9 renderer");
    D3DPRESENT_PARAMETERS parameters{};
    parameters.BackBufferWidth=1280; parameters.BackBufferHeight=720;
    parameters.BackBufferFormat=D3DFMT_A8R8G8B8;
    parameters.BackBufferCount=1;
    parameters.SwapEffect=D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow=window.handle;
    parameters.Windowed=TRUE;
    parameters.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ComPtr<IDirect3DDevice9> device;
    hr(d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window.handle,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,&parameters,&device),
        "create hidden test graphics device");
    ComPtr<IDirect3DSurface9> backbuffer,original_target,readback;
    hr(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer),"get test backbuffer");
    hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0),"clear test image");
    hr(device->CreateRenderTarget(256,128,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,
        0,FALSE,&original_target,nullptr),"create native-owner surrogate target");
    hr(device->SetRenderTarget(0,original_target.Get()),"set surrogate native target");
    const D3DVIEWPORT9 original_viewport{13,7,200,100,.1F,.9F};
    hr(device->SetViewport(&original_viewport),"set native viewport");
    hr(device->SetRenderState(D3DRS_LIGHTING,TRUE),"set native lighting");
    hr(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"set native blending");
    hr(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_CW),"set native cull mode");
    hr(device->SetRenderState(D3DRS_COLORWRITEENABLE,3),"set native color mask");
    wawvr::xr::StereoSourceLayout layout{};
    wawvr::xr::FrameState frame{};
    frame.frame_id=1; frame.should_render=true; frame.views_valid=true;
    frame.actions.focused=true;
    wawvr::mod::draw_vr_gameplay_prompt_overlay(device.Get(),layout,frame,true,false);
    ComPtr<IDirect3DSurface9> restored_target;
    hr(device->GetRenderTarget(0,&restored_target),"read restored target");
    check(restored_target.Get()==original_target.Get(),"overlay restores exact native render target");
    D3DVIEWPORT9 restored_viewport{};
    hr(device->GetViewport(&restored_viewport),"read restored viewport");
    check(std::memcmp(&restored_viewport,&original_viewport,sizeof(original_viewport))==0,
        "overlay restores exact native viewport including depth range");
    for (const auto& pair : std::array<std::pair<D3DRENDERSTATETYPE,DWORD>,4>{{
            {D3DRS_LIGHTING,TRUE},{D3DRS_ALPHABLENDENABLE,FALSE},
            {D3DRS_CULLMODE,D3DCULL_CW},{D3DRS_COLORWRITEENABLE,3}}}) {
        DWORD actual{};
        hr(device->GetRenderState(pair.first,&actual),"read restored render state");
        check(actual==pair.second,"native render state restored after overlay");
    }
    hr(device->CreateOffscreenPlainSurface(1280,720,D3DFMT_A8R8G8B8,
        D3DPOOL_SYSTEMMEM,&readback,nullptr),"create image readback");
    hr(device->GetRenderTargetData(backbuffer.Get(),readback.Get()),"read actual overlay rasterization");
    D3DLOCKED_RECT pixels{};
    hr(readback->LockRect(&pixels,nullptr,D3DLOCK_READONLY),"map actual overlay pixels");
    std::array<std::size_t,2> glyph_pixels{};
    std::size_t lower_image_changes{};
    for (int y=0;y<720;++y) {
        const auto* row=reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::byte*>(pixels.pBits)+y*pixels.Pitch);
        for (int x=0;x<1280;++x) {
            const auto rgb=row[x]&0xFFFFFFU;
            if (y>=120 && y<150 && (rgb&0xFF)>100) ++glyph_pixels[x/640];
            if (y>=200 && rgb!=0) ++lower_image_changes;
        }
    }
    hr(readback->UnlockRect(),"unmap overlay image");
    check(glyph_pixels[0]>200 && glyph_pixels[1]>200,
        "actual readable glyph pixels are drawn in both packed eyes");
    check(glyph_pixels[0]==glyph_pixels[1],"both packed eyes have identical guidance");
    check(lower_image_changes==0,"guidance never covers lower subtitles and HUD");

    // Mono/menu call must not touch either the bound native target or image.
    hr(device->SetRenderTarget(0,backbuffer.Get()),"rebind image for menu exclusion test");
    hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0),"clear menu test image");
    wawvr::mod::draw_vr_gameplay_prompt_overlay(device.Get(),layout,frame,false,false);
    hr(device->GetRenderTargetData(backbuffer.Get(),readback.Get()),"read excluded menu image");
    hr(readback->LockRect(&pixels,nullptr,D3DLOCK_READONLY),"map excluded menu image");
    std::size_t changed{};
    for (int y=0;y<720;++y) {
        const auto* row=reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::byte*>(pixels.pBits)+y*pixels.Pitch);
        for (int x=0;x<1280;++x) if ((row[x]&0xFFFFFFU)!=0) ++changed;
    }
    hr(readback->UnlockRect(),"unmap excluded menu image");
    check(changed==0,"mono/menu image remains byte-for-byte black");

    // Real held-help draw must read the existing runtime mode accessor, not
    // hard-code a hip route or retain the previous frame's grenade mode.
    // Use the ordinary640px eyes: the later480px optic fixture intentionally
    // has a different tiny-source containment/skip policy.
    frame.actions.hands[0].secondary.active=true;
    frame.actions.hands[0].secondary.current=true;
    std::array<std::uint64_t,2> mode_image_hash{};
    for (unsigned mode=0;mode<2;++mode) {
        g_button_grenade_fixture=mode!=0;
        const auto queries_before=g_button_grenade_queries;
        hr(device->SetRenderTarget(0,backbuffer.Get()),"rebind held-help mode image");
        hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0),"clear held-help mode image");
        wawvr::mod::draw_vr_gameplay_prompt_overlay(device.Get(),layout,frame,true,false);
        check(g_button_grenade_queries==queries_before+1,
            "actual overlay queries the runtime grenade setting for each held-help draw");
        hr(device->GetRenderTargetData(backbuffer.Get(),readback.Get()),"read held-help mode image");
        hr(readback->LockRect(&pixels,nullptr,D3DLOCK_READONLY),"map held-help mode image");
        glyph_pixels={};lower_image_changes=0;
        std::size_t old_high_help_pixels{};
        std::uint64_t hash=14695981039346656037ULL;
        for (int y=0;y<720;++y) {
            const auto* row=reinterpret_cast<const std::uint32_t*>(
                static_cast<const std::byte*>(pixels.pBits)+y*pixels.Pitch);
            for (int x=0;x<1280;++x) {
                const auto rgb=row[x]&0xFFFFFFU;
                hash=(hash^rgb)*1099511628211ULL;
                if (y>=300 && y<330 && (rgb&0xFF)>100) ++glyph_pixels[x/640];
                if (y>=120 && y<150 && rgb!=0) ++old_high_help_pixels;
                if (y>=500 && rgb!=0) ++lower_image_changes;
            }
        }
        hr(readback->UnlockRect(),"unmap held-help mode image");
        mode_image_hash[mode]=hash;
        check(glyph_pixels[0]>200 && glyph_pixels[0]==glyph_pixels[1],
            "both grenade modes place matching held-help glyphs near the eye center");
        check(old_high_help_pixels==0,"held help leaves its former high position empty");
        check(lower_image_changes==0,"expanded held help preserves lower subtitles/HUD");
    }
    check(mode_image_hash[0]!=mode_image_hash[1],
        "runtime hip/button grenade mode changes the actual held-help image");
    g_button_grenade_fixture=false;
    frame.actions.hands[0].secondary.current=false;
    frame.actions.hands[0].secondary.active=false;
    hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0),"clear held help before original optic fixture");

    // The same packed source can reserve a right-hand optical strip. The
    // overlay must honor current exact eye rectangles, not assume SBS 50/50.
    layout.eyes[0]={0,0,.375F,1};
    layout.eyes[1]={.375F,0,.375F,1};
    layout.physical_scope.active=true;
    layout.physical_scope.source={.75F,0,.25F,1};
    wawvr::mod::draw_vr_gameplay_prompt_overlay(device.Get(),layout,frame,true,false);
    hr(device->GetRenderTargetData(backbuffer.Get(),readback.Get()),"read optical-strip layout");
    hr(readback->LockRect(&pixels,nullptr,D3DLOCK_READONLY),"map optical-strip layout");
    glyph_pixels={};
    std::size_t optic_changes{};
    for (int y=0;y<720;++y) {
        const auto* row=reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::byte*>(pixels.pBits)+y*pixels.Pitch);
        for (int x=0;x<1280;++x) {
            const auto rgb=row[x]&0xFFFFFFU;
            if (x>=960 && rgb!=0) ++optic_changes;
            if (x<960 && y>=120 && y<150 && (rgb&0xFF)>100)
                ++glyph_pixels[x/480];
        }
    }
    hr(readback->UnlockRect(),"unmap optical-strip layout");
    check(glyph_pixels[0]>200 && glyph_pixels[0]==glyph_pixels[1],
        "optical-strip layout still contains matching two-eye glyphs");
    check(optic_changes==0,"reserved physical optic camera pixels are untouched");

    // Exercise the same real renderer with each verified controller alias.
    // Profiles can change during a session, so geometry must be rebuilt from
    // the changed prompt rather than retaining a stale generic label.
    for (const auto profile : {wawvr::xr::ControllerBindingProfile::touch,
                              wawvr::xr::ControllerBindingProfile::index,
                              wawvr::xr::ControllerBindingProfile::generic}) {
        hr(device->SetRenderTarget(0,backbuffer.Get()),"rebind alias test image");
        hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0),"clear alias test image");
        frame.actions.hands[0].binding_profile=profile;
        frame.actions.hands[1].binding_profile=profile;
        wawvr::mod::draw_vr_gameplay_prompt_overlay(device.Get(),layout,frame,true,false);
        hr(device->GetRenderTargetData(backbuffer.Get(),readback.Get()),"read controller alias glyphs");
        hr(readback->LockRect(&pixels,nullptr,D3DLOCK_READONLY),"map controller alias glyphs");
        glyph_pixels={}; optic_changes=0;
        for (int y=0;y<720;++y) {
            const auto* row=reinterpret_cast<const std::uint32_t*>(
                static_cast<const std::byte*>(pixels.pBits)+y*pixels.Pitch);
            for (int x=0;x<1280;++x) {
                const auto rgb=row[x]&0xFFFFFFU;
                if (x>=960 && rgb!=0) ++optic_changes;
                if (x<960 && y>=120 && y<150 && (rgb&0xFF)>100)
                    ++glyph_pixels[x/480];
            }
        }
        hr(readback->UnlockRect(),"unmap controller alias glyphs");
        check(glyph_pixels[0]>200 && glyph_pixels[0]==glyph_pixels[1],
            "Touch, Index and generic aliases rasterize identically in both eyes");
        check(optic_changes==0,"controller aliases never overwrite the optic strip");
    }
}

void test_central_held_help_across_source_sizes_and_eye_rectangles() {
    // These are real D3D9 pixel checks, not headset-comfort acceptance. The
    // compositor may remap each eye further, so the final position needs HMD QA.
    for (const auto size : std::array<std::pair<UINT,UINT>,3>{{
            {2560,1440},{3744,2016},{6016,2688}}}) {
        for (const bool optic : {false,true}) {
            Window window;
            ComPtr<IDirect3D9> d3d;
            d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));
            check(d3d!=nullptr,"create resolution graphics fixture");
            D3DPRESENT_PARAMETERS parameters{};
            parameters.BackBufferWidth=size.first;
            parameters.BackBufferHeight=size.second;
            parameters.BackBufferFormat=D3DFMT_A8R8G8B8;
            parameters.BackBufferCount=1;
            parameters.SwapEffect=D3DSWAPEFFECT_DISCARD;
            parameters.hDeviceWindow=window.handle;
            parameters.Windowed=TRUE;
            parameters.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
            ComPtr<IDirect3DDevice9> device;
            hr(d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window.handle,
                D3DCREATE_SOFTWARE_VERTEXPROCESSING,&parameters,&device),
                "create resolution test device");
            ComPtr<IDirect3DSurface9> backbuffer,readback;
            hr(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer),
                "get resolution test target");
            hr(device->CreateOffscreenPlainSurface(size.first,size.second,
                D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr),
                "create resolution readback");
            wawvr::xr::StereoSourceLayout layout{};
            if (optic) {
                layout.eyes[0]={0,.125F,.375F,.75F};
                layout.eyes[1]={.375F,.125F,.375F,.75F};
                layout.physical_scope.active=true;
                layout.physical_scope.source={.75F,0,.25F,1};
            }
            wawvr::xr::FrameState frame{};
            frame.frame_id=1;frame.should_render=true;frame.views_valid=true;
            frame.actions.focused=true;
            frame.actions.hands[0].secondary.active=true;
            frame.actions.hands[0].secondary.current=true;
            for (const bool multiplayer : {false,true}) {
                hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0),
                    "clear resolution help image");
                wawvr::mod::draw_vr_gameplay_prompt_overlay(
                    device.Get(),layout,frame,true,multiplayer);
                hr(device->GetRenderTargetData(backbuffer.Get(),readback.Get()),
                    "read resolution help raster");
                D3DLOCKED_RECT pixels{};
                hr(readback->LockRect(&pixels,nullptr,D3DLOCK_READONLY),
                    "map resolution help raster");
                std::array<std::size_t,2> glyphs{};
                std::size_t outside_central_band{},outside_eyes{};
                for (UINT y=0;y<size.second;++y) {
                    const auto* row=reinterpret_cast<const std::uint32_t*>(
                        static_cast<const std::byte*>(pixels.pBits)+y*pixels.Pitch);
                    for (UINT x=0;x<size.first;++x) {
                        const auto rgb=row[x]&0xFFFFFFU;
                        if (!rgb) continue;
                        int owner=-1;
                        for (unsigned eye=0;eye<2;++eye) {
                            const auto& rect=layout.eyes[eye];
                            const auto left=rect.x*size.first,top=rect.y*size.second;
                            const auto width=rect.width*size.first,height=rect.height*size.second;
                            if (x>=left && x<left+width && y>=top && y<top+height) {
                                owner=static_cast<int>(eye);
                                if (y<top+height*.39F || y>=top+height*.53F)
                                    ++outside_central_band;
                                break;
                            }
                        }
                        if (owner<0) ++outside_eyes;
                        else if ((rgb&0xFFU)>100) ++glyphs[owner];
                    }
                }
                hr(readback->UnlockRect(),"unmap resolution help raster");
                check(glyphs[0]>200 && glyphs[0]==glyphs[1],
                    "campaign and multiplayer held help have matching binocular glyphs");
                check(outside_central_band==0,
                    "held help stays in the central eye-local vertical band at every quality");
                check(outside_eyes==0,
                    "held help never paints an optic strip or area outside the eye rectangles");
            }
        }
    }
}
} // namespace

int main() {
    try {
        test_renderer_pixels_and_native_state_restore();
        test_central_held_help_across_source_sizes_and_eye_rectangles();
        std::puts("PASS: exact D3D9 overlay pixels in both eyes; native state and menus preserved");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what());
        return 1;
    }
}
