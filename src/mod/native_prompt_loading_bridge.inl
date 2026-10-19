// SPDX-License-Identifier: GPL-3.0-only
// Shared production/real-x86 fixture bridge at the exact owned UI key call.
// EAX-only native localizer; original native return is retained by its caller.
// Per-call stack metadata, no TLS return-address or string buffer ownership.
#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) void wawvr_native_vr_loading_tip_bridge() noexcept {
    __asm {
        pushfd
        pushad
        mov ebx,esp
        sub esp,560
        and esp,-16
        lea eax,[ebx+36]
        mov [esp+512],eax
        mov [esp+528],eax
        mov edx,[ebx+28]
        mov [esp+516],edx
        mov edx,[ebx+4]
        mov [esp+520],edx
        mov edx,[ebx+8]
        mov [esp+524],edx
        // The EAX-only native target has no caller stack arguments. Preserve
        // its exact original stack alignment, although this wrapper's frame
        // necessarily changes the absolute stack address/native return PC.
        lea ecx,[eax+4]
        and ecx,15
        add ecx,esp
        mov [ecx+544],eax
        mov esp,ecx
        push dword ptr [ebx+32]
        popfd
        mov edi,[ebx]
        mov esi,[ebx+4]
        mov ebp,[ebx+8]
        mov edx,[ebx+20]
        mov ecx,[ebx+24]
        mov eax,[ebx+28]
        mov ebx,[ebx+16]
        call dword ptr [wawvr_native_vr_prompt_key_original]
        // Snapshot the NATIVE output flags/GPR/FX before callback machinery.
        pushfd
        pushad
        mov ebx,esp
        sub esp,544
        and esp,-16
        fxsave [esp]
        lea edx,[ebx+36]
        and edx,-16
        mov [esp+512],edx
        call GetLastError
        mov [esp+516],eax
        mov edx,[esp+512]
        push dword ptr [edx+528]
        push dword ptr [edx+524]
        push dword ptr [edx+520]
        push dword ptr [edx+516]
        push dword ptr [ebx+28]
        call wawvr_resolve_native_vr_loading_tip
        add esp,20
        test eax,eax
        jz native_result
        mov [ebx+28],eax
    native_result:
        push dword ptr [esp+516]
        call SetLastError
        fxrstor [esp]
        mov esp,ebx
        popad
        popfd
        mov esp,[esp+544]
        ret
    }
}
#endif
