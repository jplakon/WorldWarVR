// Real x86 shared-production-bridge ABI fixture; synthetic native target only.
#include <windows.h>
#include "native_prompt_loading_item_observation.hpp"
#include "native_prompt_loading_tip_policy.hpp"
#include "vr_loading_tip_text.hpp"
using namespace wawvr::mod;
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if !defined(_MSC_VER) || !defined(_M_IX86)
#error This proposal requires the real MSVC x86 ABI fixture.
#endif
namespace {
constexpr std::array<const char*,4> keys{kVrLoadingTipText[0].key.data(),kVrLoadingTipText[1].key.data(),kVrLoadingTipText[2].key.data(),kVrLoadingTipText[3].key.data()};
constexpr std::array<const char*,4> native_text{kVrLoadingTipText[0].original_english.data(),kVrLoadingTipText[1].original_english.data(),kVrLoadingTipText[2].original_english.data(),kVrLoadingTipText[3].original_english.data()};
// Draft wording only, not production localization or binding acceptance.
constexpr std::array<const char*,4> replacement{kVrLoadingTipText[0].vr_text.data(),kVrLoadingTipText[1].vr_text.data(),kVrLoadingTipText[2].vr_text.data(),kVrLoadingTipText[3].vr_text.data()};
constexpr char dvar[]="didyouknow";
struct Item {
    std::array<std::byte,0xB4> pad{};
    std::uint32_t type{};
    std::array<std::byte,0x28> pad2{};
    const char* text{};
    std::uint32_t flags{};
    std::array<std::byte,0x24> pad3{};
    const char* dvar_name{dvar};
    std::array<std::byte,0x30> pad4{};
    std::uint32_t expression_count{};
};
static_assert(offsetof(Item,type)==0xB4 && offsetof(Item,text)==0xE0);
static_assert(offsetof(Item,flags)==0xE4 && offsetof(Item,dvar_name)==0x10C);
static_assert(offsetof(Item,expression_count)==0x140);
Item item{};
std::uint32_t context{};
std::uint32_t skew{},key_index{},calls{},selector_calls{},assertions{};
bool enabled{},source_current{true},profile_supported{true},native_inputs_ok{},selector_args_ok{};
bool nested_pending{},nested_return_ok{};
std::uintptr_t host_esp{},entry_esp{},after_esp{},native_esp{};
std::uint32_t expected_call_return{};
std::array<std::uint32_t,8> after_gpr{};
std::uint32_t after_flags{},native_flags{},after_error{};
std::uint32_t expected_entry_flags{},observed_entry_flags{},observed_entry_error{},observed_entry_eax{};
alignas(16) std::array<std::byte,512> native_fx{},after_fx{};
alignas(16) std::array<std::byte,512> expected_entry_fx{},observed_entry_fx{};
alignas(16) const std::array<std::uint32_t,4> native_xmm{0x11223344,0x55667788,0x99AABBCC,0xDDEEFF00};
const char* selected_native{};
const char* selected_key{};
const char* selected_override{};
bool check(bool value,const char* name){++assertions;if(!value){std::printf("FAIL %s\n",name);return false;}return true;}
bool eligible(const char* key,const char* output,const Item* observed,const void* dc,
    const void* native_stack) noexcept {
    if(!loading_tip_frame_allows({enabled,true,source_current,true,true,2,16})||
        !profile_supported||observed!=&item||dc!=&context||!native_stack||
        reinterpret_cast<std::uintptr_t>(native_stack)!=entry_esp) return false;
    std::uint32_t call_return{},parent_return{};
    std::memcpy(&call_return,native_stack,4);
    std::memcpy(&parent_return,static_cast<const char*>(native_stack)+0x42C,4);
    LoadingItemSnapshot s{};
    s.type=item.type;s.flags=item.flags;s.text=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(item.text));
    s.dvar=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(item.dvar_name));
    s.expression_count=item.expression_count;s.local_client=context;
    s.item_read=s.context_read=s.key_read=true;s.dvar_read=item.dvar_name!=nullptr;
    if(item.dvar_name)strcpy_s(s.dvar_name.data(),s.dvar_name.size(),item.dvar_name);
    strcpy_s(s.key.data(),s.key.size(),key);
    return loading_item_observation_rejections(s,s,reinterpret_cast<std::uintptr_t>(key),
        reinterpret_cast<std::uintptr_t>(native_stack),call_return,expected_call_return,parent_return,key)==0&&
        !resolve_vr_loading_tip_text(key,output?std::string_view(output):std::string_view{}).empty();
}
}
extern "C" std::uintptr_t wawvr_native_vr_prompt_key_original{};
extern "C" void invoke_loading_fixture() noexcept;
extern "C" const char* __cdecl wawvr_resolve_native_vr_loading_tip(const char* native_result,
    const char* key,const void* observed_item,const void* dc,const void* native_stack) noexcept {
    ++selector_calls;
    selector_args_ok=key==reinterpret_cast<const char*>(entry_esp+0x2D)&&
        observed_item==&item&&dc==&context&&native_stack==reinterpret_cast<const void*>(entry_esp);
    const char* result=nullptr;
    if(eligible(key,native_result,static_cast<const Item*>(observed_item),dc,native_stack)){
        const auto text=resolve_vr_loading_tip_text(key,native_result);result=text.empty()?nullptr:text.data();
    }
    // Deliberately poison callback-visible LastError, GP/FX/flags. All must be
    // erased; only the selected immutable EAX return may differ from native.
    if(nested_pending){
        nested_pending=false;const auto outer=entry_esp;invoke_loading_fixture();
        nested_return_ok=after_gpr[7]==reinterpret_cast<std::uintptr_t>(selected_override);
        entry_esp=outer;
    }
    SetLastError(0xBAD0BAD0);
    __asm { xorps xmm0,xmm0 }
    __asm { xorps xmm6,xmm6 }
    __asm { fninit }
    __asm { clc }
    return result;
}
#include "native_prompt_loading_bridge.inl"
extern "C" __declspec(naked) void offline_native_fixture() noexcept {
    __asm {
        mov [native_esp],esp
        pushfd
        pop dword ptr [observed_entry_flags]
        fxsave [observed_entry_fx]
        mov [observed_entry_eax],eax
        mov eax,dword ptr fs:[34h]
        mov [observed_entry_error],eax
        mov eax,[observed_entry_eax]
        inc dword ptr [calls]
        cmp eax,[selected_key]
        jne bad_inputs
        cmp esi,offset item
        jne bad_inputs
        cmp ebp,offset context
        jne bad_inputs
        cmp ecx,0C3C3C3C3h
        jne bad_inputs
        cmp edx,0D4D4D4D4h
        jne bad_inputs
        cmp ebx,0B2B2B2B2h
        jne bad_inputs
        cmp edi,0D1D1D1D1h
        jne bad_inputs
        mov byte ptr [native_inputs_ok],1
    bad_inputs:
        mov eax,[selected_native]
        mov ebx,012121212h
        mov ecx,023232323h
        mov edx,034343434h
        mov esi,045454545h
        mov edi,056565656h
        mov ebp,067676767h
        movdqu xmm0,xmmword ptr [native_xmm]
        movdqu xmm6,xmmword ptr [native_xmm]
        fninit
        fld1
        mov dword ptr fs:[34h],0A1B2C3D4h
        stc
        pushfd
        pop dword ptr [native_flags]
        fxsave [native_fx]
        ret
    }
}
extern "C" __declspec(naked) void invoke_loading_fixture() noexcept {
    __asm {
        pushfd
        pushad
        mov [host_esp],esp
        // Stacked host return facts are per invocation, including reentry.
        push dword ptr [host_esp]
        sub esp,460h
        sub esp,[skew]
        mov dword ptr [esp+428h],005C98DBh
        mov byte ptr [esp+28h],40h
        lea edi,[esp+29h]
        mov esi,[selected_key]
    copy_key:
        mov al,[esi]
        mov [edi],al
        inc esi
        inc edi
        test al,al
        jnz copy_key
        lea eax,[esp+29h]
        mov [selected_key],eax
        lea edx,[esp-4]
        mov [entry_esp],edx
        mov esi,offset item
        mov ebp,offset context
        mov ecx,0C3C3C3C3h
        mov edx,0D4D4D4D4h
        mov ebx,0B2B2B2B2h
        mov edi,0D1D1D1D1h
        mov dword ptr [expected_call_return],offset after_bridge
        mov dword ptr fs:[34h],01234ABCDh
        stc
        pushfd
        pop dword ptr [expected_entry_flags]
        fxsave [expected_entry_fx]
        call wawvr_native_vr_loading_tip_bridge
    after_bridge:
        pushfd
        pop dword ptr [after_flags]
        pushad
        mov esi,esp
        mov edi,offset after_gpr
        mov ecx,8
        rep movsd
        popad
        fxsave [after_fx]
        mov [after_esp],esp
        mov eax,dword ptr fs:[34h]
        mov [after_error],eax
        // Native output GPRs above are preserved in after_gpr; host restoration
        // must not depend on native/nonvolatile outputs or global nested state.
        add esp,[skew]
        add esp,460h
        pop esp
        popad
        popfd
        ret
    }
}
int main(){
    wawvr_native_vr_prompt_key_original=reinterpret_cast<std::uintptr_t>(&offline_native_fixture);
    for(std::uint32_t alignment=0;alignment<16;alignment+=4)
        for(std::uint32_t i=0;i<4;++i)for(const bool override_enabled:{false,true}){
            skew=alignment;key_index=i;enabled=override_enabled;item={};context=0;
            selected_key=keys[i];selected_native=native_text[i];selected_override=replacement[i];
            calls=selector_calls=0;native_inputs_ok=selector_args_ok=false;
            invoke_loading_fixture();
            if(!check(calls==1&&selector_calls==1,"native and selector exactly once")||
                !check(native_inputs_ok&&selector_args_ok,"exact entry argument registers and metadata")||
                !check(observed_entry_flags==expected_entry_flags,"native input flags retained")||
                !check(std::memcmp(observed_entry_fx.data(),expected_entry_fx.data(),416)==0,"native input FX retained")||
                !check(observed_entry_error==0x1234ABCD,"native input LastError retained")||
                !check((native_esp&15)==(entry_esp&15),"native stack alignment retained")||
                !check(after_esp==entry_esp+4,"original caller ESP restored")||
                !check(after_gpr[7]==reinterpret_cast<std::uintptr_t>(override_enabled?replacement[i]:native_text[i]),"EAX alone may change")||
                !check(after_gpr[0]==0x56565656&&after_gpr[1]==0x45454545&&after_gpr[2]==0x67676767&&after_gpr[4]==0x12121212&&after_gpr[5]==0x34343434&&after_gpr[6]==0x23232323,"native output GPRs retained")||
                !check(after_flags==native_flags,"native output flags retained")||
                !check(std::memcmp(after_fx.data(),native_fx.data(),416)==0,"native x87 MXCSR XMM state retained")||
                !check(after_error==0xA1B2C3D4,"native LastError retained"))return 1;
        }
    enabled=true;source_current=profile_supported=true;item={};context=0;skew=0;
    selected_key=keys[0];selected_native=native_text[0];selected_override=replacement[0];
    calls=selector_calls=0;nested_pending=true;nested_return_ok=false;
    invoke_loading_fixture();
    if(!check(calls==2&&selector_calls==2&&nested_return_ok,"nested calls own separate frames and call native once each")||
        !check(after_gpr[7]==reinterpret_cast<std::uintptr_t>(replacement[0]),"outer immutable result survives nested callback")||
        !check(after_error==0xA1B2C3D4&&after_flags==native_flags,"outer native LastError and flags survive reentry"))return 1;
    // Actual bridge fallback (not only the pure predicate): unknown item/key or
    // localization must call native once and retain that native output pointer.
    for(unsigned negative=0;negative<10;++negative){
        enabled=true;source_current=profile_supported=true;item={};context=0;skew=0;
        selected_key=keys[0];selected_native=native_text[0];
        switch(negative){
        case 0:selected_key="UNKNOWN_KEY";break;
        case 1:item.flags=2;break;
        case 2:item.flags=1;break;
        case 3:item.text=keys[0];break;
        case 4:item.expression_count=1;break;
        case 5:item.dvar_name="custom";break;
        case 6:profile_supported=false;break;
        case 7:source_current=false;break;
        case 8:selected_native="German/custom localized text";break;
        default:item.type=13;break;
        }
        calls=selector_calls=0;invoke_loading_fixture();
        if(!check(calls==1&&selector_calls==1,"fallback native exactly once")||
            !check(after_gpr[7]==reinterpret_cast<std::uintptr_t>(selected_native),"fallback exact native result pointer")||
            !check(after_error==0xA1B2C3D4&&after_flags==native_flags,"fallback native LastError and flags unchanged"))return 1;
    }
    // Pure admission negatives use only synthetic owned fixture storage.
    enabled=true;source_current=profile_supported=true;item={};context=0;
    std::array<std::byte,0x460> stack{};auto* s=stack.data();
    const std::uint32_t ret=0x005C6FAE,parent=0x005C98DB;
    std::memcpy(s,&ret,4);std::memcpy(s+0x42C,&parent,4);
    strcpy_s(reinterpret_cast<char*>(s+0x2D),64,keys[0]);entry_esp=reinterpret_cast<std::uintptr_t>(s);expected_call_return=ret;
    const auto admits=[&]{return eligible(reinterpret_cast<const char*>(s+0x2D),native_text[0],&item,&context,s);};
    if(!check(admits(),"synthetic exact didyouknow provenance"))return 1;
    item.flags=2;if(!check(!admits(),"cinematic excluded"))return 1;item.flags=1;
    if(!check(!admits(),"encoded source excluded"))return 1;item={};item.type=13;
    if(!check(!admits(),"nonplain item excluded"))return 1;item={};item.text=keys[0];
    if(!check(!admits(),"literal item text excluded"))return 1;item={};item.expression_count=1;
    if(!check(!admits(),"expression item excluded"))return 1;item={};item.dvar_name="custom";
    if(!check(!admits(),"other dvar excluded"))return 1;item={};context=1;
    if(!check(!admits(),"other local client excluded"))return 1;context=0;profile_supported=false;
    if(!check(!admits(),"unknown profile excluded"))return 1;profile_supported=true;source_current=false;
    if(!check(!admits(),"stale source excluded"))return 1;source_current=true;
    if(!check(!eligible(reinterpret_cast<const char*>(s+0x2D),"German/custom localized text",&item,&context,s),"nonexact native result stays native"))return 1;
    auto wrong=std::uint32_t{0x005C6FAF};std::memcpy(s,&wrong,4);
    if(!check(!admits(),"wrong call return excluded"))return 1;std::memcpy(s,&ret,4);
    wrong=0x005C98BE;std::memcpy(s+0x42C,&wrong,4);
    if(!check(!admits(),"other painter caller excluded"))return 1;std::memcpy(s+0x42C,&parent,4);
    if(!check(!eligible(keys[0],native_text[0],&item,&context,s),"nonlocal stack key source excluded"))return 1;
    strcpy_s(reinterpret_cast<char*>(s+0x2D),64,"UNKNOWN_KEY");
    if(!check(!admits(),"unknown key stays native"))return 1;
    std::printf("PASS %u offline synthetic ABI/admission assertions; no game linkage or production hook\n",assertions);
    return 0;
}
