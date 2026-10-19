// SPDX-License-Identifier: GPL-3.0-only
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(_MSC_VER) && defined(_M_IX86)
namespace {
alignas(16) std::array<std::byte,512> g_before{},g_after{};
alignas(16) const std::array<std::uint32_t,4> g_xmm_seed{
    0x11223344,0x55667788,0x99AABBCC,0xDDEEFF00};
std::uintptr_t g_host_esp{},g_after_esp{},g_bridge{};
std::uint32_t g_eax{},g_ebx{},g_ecx{},g_edx{},g_esi{},g_edi{},g_ebp{},g_flags{};
std::uint32_t g_seed_eax{},g_seen_hit{},g_seen_kind{};
unsigned g_callback_count{};
bool g_arguments{};
}
extern "C" std::uintptr_t wawvr_server_plain_impact_resume=0;
extern "C" std::uintptr_t wawvr_server_penetrating_impact_resume=0;
extern "C" void __cdecl wawvr_observe_server_impact(
    const void* params,const void* results,const void* definition,
    const void* attacker,std::uint32_t hit,std::uint32_t kind) noexcept {
    ++g_callback_count;
    g_arguments=reinterpret_cast<std::uintptr_t>(params)==0x77777777 &&
        reinterpret_cast<std::uintptr_t>(results)==0x65656565 &&
        reinterpret_cast<std::uintptr_t>(definition)==0xB0B0B0B0 &&
        reinterpret_cast<std::uintptr_t>(attacker)==0xB2B2B2B2;
    g_seen_hit=hit; g_seen_kind=kind;
    __asm { xorps xmm0,xmm0 }
    __asm { xorps xmm6,xmm6 }
    __asm { fninit }
    __asm { stc }
}
#include "server_impact_audit_bridge.inl"

__declspec(naked) void observe_fixture() noexcept {
    __asm {
        pushfd
        pushad
        mov dword ptr [g_host_esp],esp
        mov dword ptr [wawvr_server_plain_impact_resume],offset resume_native
        mov dword ptr [wawvr_server_penetrating_impact_resume],offset resume_native
        // Original caller owned four arguments remain until the displaced
        // cleanup. The bridge is reached by JMP, never by an extra CALL.
        push 044444444h
        push 033333333h
        push 022222222h
        push 011111111h
        mov eax,dword ptr [g_seed_eax]
        mov ebx,0B2B2B2B2h
        mov ecx,0C3C3C3C3h
        mov edx,0D4D4D4D4h
        mov esi,065656565h
        mov edi,077777777h
        mov ebp,0B0B0B0B0h
        movdqu xmm0,xmmword ptr [g_xmm_seed]
        movdqu xmm6,xmmword ptr [g_xmm_seed]
        fld1
        fxsave [g_before]
        stc
        jmp dword ptr [g_bridge]
    resume_native:
        mov dword ptr [g_eax],eax
        mov dword ptr [g_ebx],ebx
        mov dword ptr [g_ecx],ecx
        mov dword ptr [g_edx],edx
        mov dword ptr [g_esi],esi
        mov dword ptr [g_edi],edi
        mov dword ptr [g_ebp],ebp
        mov dword ptr [g_after_esp],esp
        pushfd
        pop dword ptr [g_flags]
        fxsave [g_after]
        fstp st(0)
        mov esp,dword ptr [g_host_esp]
        popad
        popfd
        ret
    }
}

int main() {
    unsigned failures{};
    const auto check=[&](bool pass,const char* what) {
        if (!pass) {std::fprintf(stderr,"FAIL: %s\n",what);++failures;}
    };
    for (const unsigned kind : {0U,1U})
    for (const unsigned hit : {0U,1U,0x80U,0xFFU}) {
        g_seed_eax=0xA1B2C300U|hit;
        g_bridge=kind ? reinterpret_cast<std::uintptr_t>(&wawvr_server_penetrating_impact_bridge)
                      : reinterpret_cast<std::uintptr_t>(&wawvr_server_plain_impact_bridge);
        g_callback_count=0;g_arguments=false;g_before={};g_after={};
        observe_fixture();
        check(g_arguments && g_callback_count==1 && g_seen_kind==kind &&
            g_seen_hit==g_seed_eax,"exact EDI/ESI/EBP/EBX/EAX contract reaches observer once");
        check(g_after_esp==g_host_esp,"exact four caller-owned arguments cleaned once");
        check(g_eax==g_seed_eax && g_ebx==0xB2B2B2B2 && g_ecx==0xC3C3C3C3 &&
            g_edx==0xD4D4D4D4 && g_esi==0x65656565 && g_edi==0x77777777 &&
            g_ebp==0xB0B0B0B0,"all seven native general registers preserved");
        check(((g_flags>>6)&1U)==static_cast<unsigned>(hit==0) && ((g_flags>>7)&1U)==((hit>>7)&1U) &&
            (g_flags&0x801U)==0,"native TEST AL,AL zero/sign/carry/overflow reproduced");
        check(std::memcmp(g_before.data()+0xA0,g_after.data()+0xA0,128)==0,
            "all eight XMM registers restored");
        check(std::memcmp(g_before.data(),g_after.data(),32)==0 &&
            std::memcmp(g_before.data()+32,g_after.data()+32,128)==0,
            "x87 stack/control/status and MXCSR restored despite callback FNINIT");
    }
    if (!failures) std::puts("PASS: eight production server-impact bridge hit/branch ABI fixtures");
    return failures?1:0;
}
#else
int main(){return 1;}
#endif
