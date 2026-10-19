// SPDX-License-Identifier: GPL-3.0-only
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>

#if defined(_MSC_VER) && defined(_M_IX86)
namespace {
alignas(16) std::array<std::byte,512> g_fx_before{},g_fx_after{};
alignas(16) const std::array<std::uint32_t,4> g_xmm_seed{
    0x11223344,0x55667788,0x99AABBCC,0xDDEEFF00};
std::array<char,256> g_output{};
const char g_command[]="+reload";
std::uintptr_t g_host_esp{},g_call_esp{},g_after_esp{};
std::uint32_t g_after_eax{},g_after_ebx{},g_after_ecx{},g_after_edx{},
    g_after_esi{},g_after_edi{},g_after_ebp{},g_flags_before{},g_flags_after{};
bool g_override{},g_service_saw_args{},g_native_called{},g_native_saw_args{};
const char g_key[]="MAK_TRUCK_TRIGGER";
const char g_native_key_text[]="native localized text remains unchanged";
alignas(16) std::array<std::uint32_t,4> g_key_xmm_entry{};
std::uintptr_t g_key_test_target{};
std::uint32_t g_key_native_calls{},g_key_observer_calls{},g_key_native_flags{};
bool g_key_service_saw_args{},g_key_native_saw_args{};
const char g_encoded[]="MAK_TRUCK_TRIGGER\x15" "F";
const char g_encoded_native_context[]="native display context";
bool g_encoded_service_saw_args{};
const char g_cg_private_copy[]="MAK_TRUCK_TRIGGER\x15" "LEFT X";
const char* g_cg_expected_source{};
const char* g_cg_seen_source{};
bool g_cg_copy{},g_cg_service_saw_args{};
bool g_cursor_service_saw_args{};
std::uintptr_t g_after_argument{};
}
extern "C" std::uintptr_t wawvr_native_vr_binding_prompt_original=0;
extern "C" std::uintptr_t wawvr_native_vr_prompt_key_original=0;
extern "C" std::uintptr_t wawvr_native_vr_prompt_encoded_original=0;
extern "C" std::uintptr_t wawvr_native_vr_prompt_cg_original=0;
extern "C" std::uintptr_t wawvr_native_vr_prompt_cursor_original=0;
extern "C" int __cdecl wawvr_resolve_native_vr_binding_prompt(
    const char* command,const std::int32_t client,void* output) noexcept {
    g_service_saw_args=command==g_command && client==0 && output==g_output.data();
    // Deliberately destroy live SIMD values inside the C++ callback: the
    // production bridge must restore them on BOTH override and native paths.
    __asm { xorps xmm0,xmm0 }
    __asm { xorps xmm6,xmm6 }
    if (!g_override) return 0;
    std::memcpy(output,"R-PRIMARY",10);
    return 1;
}
extern "C" void __cdecl wawvr_observe_native_vr_prompt_key(
    const char* key,const void* item,const void* context,const void* native_stack) noexcept {
    ++g_key_observer_calls;
    std::uint32_t caller{};
    std::memcpy(&caller,static_cast<const char*>(native_stack)+0x42C,sizeof(caller));
    g_key_service_saw_args=key==g_key &&
        reinterpret_cast<std::uintptr_t>(item)==0x65656565 &&
        reinterpret_cast<std::uintptr_t>(context)==0xB0B0B0B0 &&
        reinterpret_cast<std::uintptr_t>(native_stack)==g_call_esp-4 &&
        caller==0x12345678;
    // The exact production observer bridge must erase ALL observer effects,
    // while allowing the following original native call's effects through.
    __asm { xorps xmm0,xmm0 }
    __asm { xorps xmm6,xmm6 }
    __asm { xor ecx,ecx }
    __asm { xor edx,edx }
    __asm { clc }
}
extern "C" void __cdecl wawvr_observe_native_vr_prompt_encoded(
    const char* message,const void* native_context,std::int32_t policy,
    const void* item,const void* context,const void* native_stack) noexcept {
    ++g_key_observer_calls;
    std::uint32_t caller{};
    std::memcpy(&caller,static_cast<const char*>(native_stack)+0x438,sizeof(caller));
    g_encoded_service_saw_args=message==g_encoded &&
        native_context==g_encoded_native_context && policy==0 &&
        reinterpret_cast<std::uintptr_t>(item)==0x65656565 &&
        reinterpret_cast<std::uintptr_t>(context)==0xB0B0B0B0 &&
        reinterpret_cast<std::uintptr_t>(native_stack)==g_call_esp-4 && caller==0x12345678;
    __asm { xorps xmm0,xmm0 }
    __asm { xorps xmm6,xmm6 }
    __asm { xor ecx,ecx }
    __asm { xor edx,edx }
    __asm { clc }
}
extern "C" const char* __cdecl wawvr_resolve_native_vr_prompt_cg(
    const char* message,const void* native_context,std::int32_t policy,
    std::int32_t local_client,const void* native_stack) noexcept {
    ++g_key_observer_calls;
    std::uint32_t caller{};
    std::memcpy(&caller,static_cast<const char*>(native_stack)+0x40,sizeof(caller));
    g_cg_service_saw_args=message==g_encoded && native_context==g_encoded_native_context &&
        policy==0 && local_client==0 && reinterpret_cast<std::uintptr_t>(native_stack)==g_call_esp-4 &&
        caller==0x12345678;
    __asm { xorps xmm0,xmm0 }
    __asm { xorps xmm6,xmm6 }
    __asm { xor ecx,ecx }
    __asm { xor edx,edx }
    __asm { clc }
    return g_cg_copy?g_cg_private_copy:nullptr;
}
extern "C" const char* __cdecl wawvr_resolve_native_vr_prompt_cursor(
    const char* message,const void* native_context,std::int32_t policy,
    std::int32_t local_client,const void* native_stack) noexcept {
    ++g_key_observer_calls;
    std::uint32_t caller{};
    std::memcpy(&caller,static_cast<const char*>(native_stack)+0x11C,sizeof(caller));
    g_cursor_service_saw_args=message==g_encoded && native_context==g_encoded_native_context &&
        policy==0 && local_client==0 && reinterpret_cast<std::uintptr_t>(native_stack)==g_call_esp-4 &&
        caller==0x12345678;
    __asm { xorps xmm0,xmm0 }
    __asm { xorps xmm6,xmm6 }
    __asm { xor ecx,ecx }
    __asm { xor edx,edx }
    __asm { clc }
    return g_cg_copy?g_cg_private_copy:nullptr;
}
#include "native_prompt_binding_bridge.inl"

__declspec(naked) void native_cursor_fixture() noexcept {
    __asm {
        pushfd
        pop dword ptr [g_key_native_flags]
        movdqu xmmword ptr [g_key_xmm_entry],xmm0
        inc dword ptr [g_key_native_calls]
        mov eax,dword ptr [esp+4]
        mov dword ptr [g_cg_seen_source],eax
        cmp eax,dword ptr [g_cg_expected_source]
        jne cursor_bad_args
        cmp dword ptr [esp+8],offset g_encoded_native_context
        jne cursor_bad_args
        cmp dword ptr [esp+12],0
        jne cursor_bad_args
        cmp ebx,0B2B2B2B2h
        jne cursor_bad_args
        cmp edi,0
        jne cursor_bad_args
        cmp esi,offset g_encoded
        jne cursor_bad_args
        cmp ecx,0C3C3C3C3h
        jne cursor_bad_args
        cmp edx,0D4D4D4D4h
        jne cursor_bad_args
        cmp dword ptr [esp+11Ch],012345678h
        jne cursor_bad_args
        mov byte ptr [g_key_native_saw_args],1
    cursor_bad_args:
        mov eax,offset g_native_key_text
        mov ecx,0E1E1E1E1h
        mov edx,0E2E2E2E2h
        xorps xmm0,xmm0
        clc
        ret
    }
}

__declspec(naked) void call_exact_cursor_bridge_fixture() noexcept {
    __asm {
        pushfd
        pushad
        mov dword ptr [g_host_esp],esp
        sub esp,110h
        mov dword ptr [esp+10Ch],012345678h
        push 0
        push offset g_encoded_native_context
        push offset g_encoded
        mov dword ptr [g_call_esp],esp
        mov eax,1
        mov ebx,0B2B2B2B2h
        mov ecx,0C3C3C3C3h
        mov edx,0D4D4D4D4h
        mov esi,offset g_encoded
        mov edi,0
        mov ebp,0B0B0B0B0h
        movdqu xmm0,xmmword ptr [g_xmm_seed]
        movdqu xmm6,xmmword ptr [g_xmm_seed]
        fxsave [g_fx_before]
        stc
        pushfd
        pop dword ptr [g_flags_before]
        call dword ptr [g_key_test_target]
        mov dword ptr [g_after_eax],eax
        mov dword ptr [g_after_ebx],ebx
        mov dword ptr [g_after_ecx],ecx
        mov dword ptr [g_after_edx],edx
        mov dword ptr [g_after_esi],esi
        mov dword ptr [g_after_edi],edi
        mov dword ptr [g_after_ebp],ebp
        mov dword ptr [g_after_esp],esp
        pushfd
        pop dword ptr [g_flags_after]
        fxsave [g_fx_after]
        mov eax,dword ptr [esp]
        mov dword ptr [g_after_argument],eax
        mov esp,dword ptr [g_host_esp]
        popad
        popfd
        ret
    }
}

__declspec(naked) void native_cg_fixture() noexcept {
    __asm {
        pushfd
        pop dword ptr [g_key_native_flags]
        movdqu xmmword ptr [g_key_xmm_entry],xmm0
        inc dword ptr [g_key_native_calls]
        mov eax,dword ptr [esp+4]
        mov dword ptr [g_cg_seen_source],eax
        cmp eax,dword ptr [g_cg_expected_source]
        jne cg_bad_args
        cmp dword ptr [esp+8],offset g_encoded_native_context
        jne cg_bad_args
        cmp dword ptr [esp+12],0
        jne cg_bad_args
        cmp ebx,0
        jne cg_bad_args
        cmp esi,offset g_encoded
        jne cg_bad_args
        cmp ecx,0C3C3C3C3h
        jne cg_bad_args
        cmp edx,0D4D4D4D4h
        jne cg_bad_args
        cmp dword ptr [esp+40h],012345678h
        jne cg_bad_args
        mov byte ptr [g_key_native_saw_args],1
    cg_bad_args:
        mov eax,offset g_native_key_text
        mov ecx,0E1E1E1E1h
        mov edx,0E2E2E2E2h
        xorps xmm0,xmm0
        clc
        ret
    }
}

__declspec(naked) void call_exact_cg_bridge_fixture() noexcept {
    __asm {
        pushfd
        pushad
        mov dword ptr [g_host_esp],esp
        sub esp,34h
        mov dword ptr [esp+30h],012345678h
        push 0
        push offset g_encoded_native_context
        push offset g_encoded
        mov dword ptr [g_call_esp],esp
        mov eax,1
        mov ebx,0
        mov ecx,0C3C3C3C3h
        mov edx,0D4D4D4D4h
        mov esi,offset g_encoded
        mov edi,077777777h
        mov ebp,0B0B0B0B0h
        movdqu xmm0,xmmword ptr [g_xmm_seed]
        movdqu xmm6,xmmword ptr [g_xmm_seed]
        fxsave [g_fx_before]
        stc
        pushfd
        pop dword ptr [g_flags_before]
        call dword ptr [g_key_test_target]
        mov dword ptr [g_after_eax],eax
        mov dword ptr [g_after_ebx],ebx
        mov dword ptr [g_after_ecx],ecx
        mov dword ptr [g_after_edx],edx
        mov dword ptr [g_after_esi],esi
        mov dword ptr [g_after_edi],edi
        mov dword ptr [g_after_ebp],ebp
        mov dword ptr [g_after_esp],esp
        pushfd
        pop dword ptr [g_flags_after]
        fxsave [g_fx_after]
        mov eax,dword ptr [esp]
        mov dword ptr [g_after_argument],eax
        mov esp,dword ptr [g_host_esp]
        popad
        popfd
        ret
    }
}

__declspec(naked) void native_encoded_fixture() noexcept {
    __asm {
        pushfd
        pop dword ptr [g_key_native_flags]
        movdqu xmmword ptr [g_key_xmm_entry],xmm0
        inc dword ptr [g_key_native_calls]
        cmp dword ptr [esp+4],offset g_encoded
        jne encoded_bad_args
        cmp dword ptr [esp+8],offset g_encoded_native_context
        jne encoded_bad_args
        cmp dword ptr [esp+12],0
        jne encoded_bad_args
        cmp esi,065656565h
        jne encoded_bad_args
        cmp ebp,0B0B0B0B0h
        jne encoded_bad_args
        cmp ecx,0C3C3C3C3h
        jne encoded_bad_args
        cmp edx,0D4D4D4D4h
        jne encoded_bad_args
        cmp dword ptr [esp+438h],012345678h
        jne encoded_bad_args
        mov byte ptr [g_key_native_saw_args],1
    encoded_bad_args:
        mov eax,offset g_native_key_text
        mov ecx,0E1E1E1E1h
        mov edx,0E2E2E2E2h
        xorps xmm0,xmm0
        clc
        ret
    }
}

__declspec(naked) void call_exact_encoded_bridge_fixture() noexcept {
    __asm {
        pushfd
        pushad
        mov dword ptr [g_host_esp],esp
        sub esp,42Ch
        mov dword ptr [esp+428h],012345678h
        push 0
        push offset g_encoded_native_context
        push offset g_encoded
        mov dword ptr [g_call_esp],esp
        mov eax,1
        mov ebx,0B2B2B2B2h
        mov ecx,0C3C3C3C3h
        mov edx,0D4D4D4D4h
        mov esi,065656565h
        mov edi,077777777h
        mov ebp,0B0B0B0B0h
        movdqu xmm0,xmmword ptr [g_xmm_seed]
        movdqu xmm6,xmmword ptr [g_xmm_seed]
        fxsave [g_fx_before]
        stc
        pushfd
        pop dword ptr [g_flags_before]
        call dword ptr [g_key_test_target]
        mov dword ptr [g_after_eax],eax
        mov dword ptr [g_after_ebx],ebx
        mov dword ptr [g_after_ecx],ecx
        mov dword ptr [g_after_edx],edx
        mov dword ptr [g_after_esi],esi
        mov dword ptr [g_after_edi],edi
        mov dword ptr [g_after_ebp],ebp
        mov dword ptr [g_after_esp],esp
        pushfd
        pop dword ptr [g_flags_after]
        fxsave [g_fx_after]
        mov esp,dword ptr [g_host_esp]
        popad
        popfd
        ret
    }
}

__declspec(naked) void native_key_fixture() noexcept {
    __asm {
        pushfd
        pop dword ptr [g_key_native_flags]
        movdqu xmmword ptr [g_key_xmm_entry],xmm0
        inc dword ptr [g_key_native_calls]
        cmp eax,offset g_key
        jne key_bad_args
        cmp esi,065656565h
        jne key_bad_args
        cmp ebp,0B0B0B0B0h
        jne key_bad_args
        cmp ecx,0C3C3C3C3h
        jne key_bad_args
        cmp edx,0D4D4D4D4h
        jne key_bad_args
        cmp dword ptr [esp+42Ch],012345678h
        jne key_bad_args
        mov byte ptr [g_key_native_saw_args],1
    key_bad_args:
        // These native output effects must NOT be restored away by observer.
        mov eax,offset g_native_key_text
        mov ecx,0E1E1E1E1h
        mov edx,0E2E2E2E2h
        xorps xmm0,xmm0
        clc
        ret
    }
}

__declspec(naked) void call_exact_key_bridge_fixture() noexcept {
    __asm {
        pushfd
        pushad
        mov dword ptr [g_host_esp],esp
        sub esp,42Ch
        mov dword ptr [esp+428h],012345678h
        mov dword ptr [g_call_esp],esp
        mov eax,offset g_key
        mov ebx,0B2B2B2B2h
        mov ecx,0C3C3C3C3h
        mov edx,0D4D4D4D4h
        mov esi,065656565h
        mov edi,077777777h
        mov ebp,0B0B0B0B0h
        movdqu xmm0,xmmword ptr [g_xmm_seed]
        movdqu xmm6,xmmword ptr [g_xmm_seed]
        fxsave [g_fx_before]
        stc
        pushfd
        pop dword ptr [g_flags_before]
        call dword ptr [g_key_test_target]
        mov dword ptr [g_after_eax],eax
        mov dword ptr [g_after_ebx],ebx
        mov dword ptr [g_after_ecx],ecx
        mov dword ptr [g_after_edx],edx
        mov dword ptr [g_after_esi],esi
        mov dword ptr [g_after_edi],edi
        mov dword ptr [g_after_ebp],ebp
        mov dword ptr [g_after_esp],esp
        pushfd
        pop dword ptr [g_flags_after]
        fxsave [g_fx_after]
        mov esp,dword ptr [g_host_esp]
        popad
        popfd
        ret
    }
}

__declspec(naked) void native_binding_fixture() noexcept {
    __asm {
        push ecx
        push edx
        mov byte ptr [g_native_called],1
        cmp edx,offset g_command
        jne bad_args
        cmp eax,1
        jne bad_args
        cmp dword ptr [esp+12],0
        jne bad_args
        mov eax,dword ptr [esp+16]
        cmp eax,offset g_output
        jne bad_args
        mov byte ptr [g_native_saw_args],1
    bad_args:
        mov eax,dword ptr [esp+16]
        mov dword ptr [eax],6974616Eh
        mov word ptr [eax+4],6576h
        mov byte ptr [eax+6],0
        pop edx
        pop ecx
        mov eax,2
        ret
    }
}

__declspec(naked) void call_exact_bridge_fixture() noexcept {
    __asm {
        pushfd
        pushad
        mov dword ptr [g_host_esp],esp
        push offset g_output
        push 0
        mov dword ptr [g_call_esp],esp
        mov eax,1
        mov ebx,0B2B2B2B2h
        mov ecx,0C3C3C3C3h
        mov edx,offset g_command
        mov esi,065656565h
        mov edi,077777777h
        mov ebp,0B0B0B0B0h
        movdqu xmm0,xmmword ptr [g_xmm_seed]
        movdqu xmm6,xmmword ptr [g_xmm_seed]
        fxsave [g_fx_before]
        stc
        pushfd
        pop dword ptr [g_flags_before]
        call wawvr_native_vr_binding_prompt_bridge
        mov dword ptr [g_after_eax],eax
        mov dword ptr [g_after_ebx],ebx
        mov dword ptr [g_after_ecx],ecx
        mov dword ptr [g_after_edx],edx
        mov dword ptr [g_after_esi],esi
        mov dword ptr [g_after_edi],edi
        mov dword ptr [g_after_ebp],ebp
        mov dword ptr [g_after_esp],esp
        pushfd
        pop dword ptr [g_flags_after]
        fxsave [g_fx_after]
        mov esp,dword ptr [g_host_esp]
        popad
        popfd
        ret
    }
}

int main() {
    int failures{};
    auto check=[&](bool pass,const char* message) {
        if (!pass) { std::fprintf(stderr,"FAIL: %s\n",message); ++failures; }
    };
    wawvr_native_vr_binding_prompt_original=
        reinterpret_cast<std::uintptr_t>(&native_binding_fixture);
    for (const bool override_label : {false,true}) {
        g_override=override_label; g_output={};
        g_service_saw_args=g_native_called=g_native_saw_args=false;
        g_fx_before={}; g_fx_after={};
        call_exact_bridge_fixture();
        check(g_service_saw_args,"EDX command and original stack arguments reach C++ adapter");
        check(g_after_esp==g_call_esp,"exact caller-owned stack is preserved");
        check(g_after_ebx==0xB2B2B2B2 && g_after_ecx==0xC3C3C3C3 &&
            g_after_edx==reinterpret_cast<std::uintptr_t>(g_command) &&
            g_after_esi==0x65656565 && g_after_edi==0x77777777 &&
            g_after_ebp==0xB0B0B0B0,"all non-result GPRs preserved by bridge");
        check(std::memcmp(g_fx_before.data()+0xA0,g_fx_after.data()+0xA0,128)==0,
            "all eight XMM registers survive adapter callback");
        check(std::memcmp(g_fx_before.data(),g_fx_after.data(),4)==0 &&
            std::memcmp(g_fx_before.data()+0x18,g_fx_after.data()+0x18,4)==0,
            "x87 control/status and MXCSR preserved");
        if (override_label) {
            check(g_after_eax==1 && !g_native_called &&
                std::strcmp(g_output.data(),"R-PRIMARY")==0,
                "override returns exactly one display label without native call");
            check(g_flags_after==g_flags_before,"override preserves caller EFLAGS");
        } else {
            check(g_after_eax==2 && g_native_called && g_native_saw_args &&
                std::strcmp(g_output.data(),"native")==0,
                "passthrough tail-calls original with exact EAX/EDX/stack ABI");
        }
    }
    // Compare the same native localizer invoked directly and through the
    // actual production observer bridge. No duplicated bridge assembly.
    wawvr_native_vr_prompt_key_original=reinterpret_cast<std::uintptr_t>(&native_key_fixture);
    std::array<std::uint32_t,8> direct_registers{};
    std::array<std::byte,512> direct_fx{};
    for (const bool observed : {false,true}) {
        g_key_test_target=observed
            ? reinterpret_cast<std::uintptr_t>(&wawvr_native_vr_prompt_key_bridge)
            : reinterpret_cast<std::uintptr_t>(&native_key_fixture);
        g_key_native_calls=g_key_observer_calls=0;
        g_key_service_saw_args=g_key_native_saw_args=false;
        g_key_xmm_entry={}; g_fx_before={}; g_fx_after={};
        call_exact_key_bridge_fixture();
        check(g_key_native_calls==1 && g_key_native_saw_args,
            "key observer leaves exact EAX/register/painter-stack ABI for one native call");
        check(g_after_esp==g_call_esp,"key observer preserves exact caller-owned stack");
        check(g_key_native_flags==g_flags_before && g_key_xmm_entry==g_xmm_seed,
            "key observer restores entry flags and SIMD before original native localizer");
        const std::array registers{g_after_eax,g_after_ebx,g_after_ecx,g_after_edx,
            g_after_esi,g_after_edi,g_after_ebp,g_flags_after};
        if (!observed) { direct_registers=registers; direct_fx=g_fx_after; }
        else {
            check(g_key_observer_calls==1 && g_key_service_saw_args,
                "exact key/item/context/native stack reach one transparent observer callback");
            check(registers==direct_registers,
                "observer preserves native result and native register/flag effects byte-for-byte");
            check(std::memcmp(direct_fx.data()+0xA0,g_fx_after.data()+0xA0,128)==0 &&
                std::memcmp(direct_fx.data(),g_fx_after.data(),4)==0 &&
                std::memcmp(direct_fx.data()+0x18,g_fx_after.data()+0x18,4)==0,
                "observer native-result SIMD/x87/MXCSR state equals direct original call");
            check(g_after_eax==reinterpret_cast<std::uintptr_t>(g_native_key_text) &&
                std::strcmp(reinterpret_cast<const char*>(g_after_eax),g_native_key_text)==0,
                "native localized output pointer and bytes are never replaced");
        }
    }
    wawvr_native_vr_prompt_cg_original=reinterpret_cast<std::uintptr_t>(&native_cg_fixture);
    for (const int mode : {0,1,2}) {
        g_cg_copy=mode==2;
        g_cg_expected_source=g_cg_copy?g_cg_private_copy:g_encoded;
        g_key_test_target=mode==0?reinterpret_cast<std::uintptr_t>(&native_cg_fixture)
            :reinterpret_cast<std::uintptr_t>(&wawvr_native_vr_prompt_cg_bridge);
        g_key_native_calls=g_key_observer_calls=0;
        g_cg_service_saw_args=g_key_native_saw_args=false;
        g_key_xmm_entry={}; g_fx_before={}; g_fx_after={};
        call_exact_cg_bridge_fixture();
        check(g_key_native_calls==1 && g_key_native_saw_args && g_after_esp==g_call_esp &&
            g_cg_seen_source==g_cg_expected_source && g_after_argument==
                reinterpret_cast<std::uintptr_t>(g_cg_expected_source),
            "CG adapter calls original once with exact3args/stack and only intended display-copy argument redirection");
        check(g_key_native_flags==g_flags_before && g_key_xmm_entry==g_xmm_seed,
            "CG adapter restores original input flags/SIMD before native formatter");
        const std::array registers{g_after_eax,g_after_ebx,g_after_ecx,g_after_edx,
            g_after_esi,g_after_edi,g_after_ebp,g_flags_after};
        if (mode==0) { direct_registers=registers; direct_fx=g_fx_after; }
        else {
            check(g_key_observer_calls==1 && g_cg_service_saw_args,
                "CG service receives original source/context/policy/client and40h parent caller");
            check(registers==direct_registers &&
                std::memcmp(direct_fx.data()+0xA0,g_fx_after.data()+0xA0,128)==0 &&
                std::memcmp(direct_fx.data(),g_fx_after.data(),4)==0 &&
                std::memcmp(direct_fx.data()+0x18,g_fx_after.data()+0x18,4)==0 &&
                std::strcmp(g_encoded,"MAK_TRUCK_TRIGGER\x15" "F")==0,
                "CG native output effects remain exact and original encoded source bytes remain untouched");
        }
    }
    wawvr_native_vr_prompt_cursor_original=reinterpret_cast<std::uintptr_t>(&native_cursor_fixture);
    for (const int mode : {0,1,2}) {
        g_cg_copy=mode==2;
        g_cg_expected_source=g_cg_copy?g_cg_private_copy:g_encoded;
        g_key_test_target=mode==0?reinterpret_cast<std::uintptr_t>(&native_cursor_fixture)
            :reinterpret_cast<std::uintptr_t>(&wawvr_native_vr_prompt_cursor_bridge);
        g_key_native_calls=g_key_observer_calls=0;
        g_cursor_service_saw_args=g_key_native_saw_args=false;
        g_key_xmm_entry={}; g_fx_before={}; g_fx_after={};
        call_exact_cursor_bridge_fixture();
        check(g_key_native_calls==1 && g_key_native_saw_args && g_after_esp==g_call_esp &&
            g_cg_seen_source==g_cg_expected_source && g_after_argument==
                reinterpret_cast<std::uintptr_t>(g_cg_expected_source),
            "cursor adapter preserves exact3args/EDI client/11Ch parent and original-once source-slot-only redirection");
        check(g_key_native_flags==g_flags_before && g_key_xmm_entry==g_xmm_seed,
            "cursor adapter restores input flags/SIMD before native formatter");
        const std::array registers{g_after_eax,g_after_ebx,g_after_ecx,g_after_edx,
            g_after_esi,g_after_edi,g_after_ebp,g_flags_after};
        if (mode==0) { direct_registers=registers; direct_fx=g_fx_after; }
        else {
            check(g_key_observer_calls==1 && g_cursor_service_saw_args,
                "cursor service receives native source/context/policy from stack, client fromEDI and11Ch parent");
            check(registers==direct_registers &&
                std::memcmp(direct_fx.data()+0xA0,g_fx_after.data()+0xA0,128)==0 &&
                std::memcmp(direct_fx.data(),g_fx_after.data(),4)==0 &&
                std::memcmp(direct_fx.data()+0x18,g_fx_after.data()+0x18,4)==0 &&
                std::strcmp(g_encoded,"MAK_TRUCK_TRIGGER\x15" "F")==0,
                "cursor bridge preserves native result/flags/GPR/FX effects and original encoded source");
        }
    }
    wawvr_native_vr_prompt_encoded_original=reinterpret_cast<std::uintptr_t>(&native_encoded_fixture);
    for (const bool observed : {false,true}) {
        g_key_test_target=observed
            ? reinterpret_cast<std::uintptr_t>(&wawvr_native_vr_prompt_encoded_bridge)
            : reinterpret_cast<std::uintptr_t>(&native_encoded_fixture);
        g_key_native_calls=g_key_observer_calls=0;
        g_encoded_service_saw_args=g_key_native_saw_args=false;
        g_key_xmm_entry={}; g_fx_before={}; g_fx_after={};
        call_exact_encoded_bridge_fixture();
        check(g_key_native_calls==1 && g_key_native_saw_args && g_after_esp==g_call_esp,
            "encoded observer preserves all three native cdecl arguments and original-once stack ABI");
        check(g_key_native_flags==g_flags_before && g_key_xmm_entry==g_xmm_seed,
            "encoded observer restores native input flags/SIMD");
        const std::array registers{g_after_eax,g_after_ebx,g_after_ecx,g_after_edx,
            g_after_esi,g_after_edi,g_after_ebp,g_flags_after};
        if (!observed) { direct_registers=registers; direct_fx=g_fx_after; }
        else {
            check(g_key_observer_calls==1 && g_encoded_service_saw_args,
                "encoded callback receives exact message/context/policy/item/frame and438h caller");
            check(registers==direct_registers &&
                std::memcmp(direct_fx.data()+0xA0,g_fx_after.data()+0xA0,128)==0 &&
                std::memcmp(direct_fx.data(),g_fx_after.data(),4)==0 &&
                std::memcmp(direct_fx.data()+0x18,g_fx_after.data()+0x18,4)==0,
                "encoded bridge leaves native output/register/flags/SIMD effects unchanged");
            check(g_after_eax==reinterpret_cast<std::uintptr_t>(g_native_key_text) &&
                std::strcmp(g_encoded,"MAK_TRUCK_TRIGGER\x15" "F")==0,
                "native result and encoded key/argument source bytes remain unchanged");
        }
    }
    if (!failures) std::puts("PASS: exact production native-prompt bridge register/stack/SIMD ABI");
    return failures ? 1 : 0;
}
#else
int main() { return 1; }
#endif
