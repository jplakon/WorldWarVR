// SPDX-License-Identifier: GPL-3.0-only
// The production hook and the independent register/stack fixture include the
// same naked bridge. Do not duplicate its assembly in a second implementation.
#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) void wawvr_native_vr_binding_prompt_bridge() noexcept {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        // Entry: EDX=exact command, [original ESP+4]=localClient,
        // [original ESP+8]=two caller-owned 128-byte output slots.
        push dword ptr [ebx + 44]
        push dword ptr [ebx + 40]
        push dword ptr [ebx + 20]
        call wawvr_resolve_native_vr_binding_prompt
        add esp, 12
        test eax, eax
        jz native_binding
        mov dword ptr [ebx + 28], eax
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        ret
    native_binding:
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [wawvr_native_vr_binding_prompt_original]
    }
}

// Proven @key call in the UI painter, NOT the shared localization function or
// script getkeybinding. Observation runs before the native call and is wholly
// transparent: the original localizer remains the only producer of EAX/text.
extern "C" __declspec(naked) void wawvr_native_vr_prompt_key_bridge() noexcept {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        // Saved GPRs: EDI+0, ESI+4, EBP+8, EDX+20, EAX+28.
        // Entry ESP points at the native return address 005C6FAE. The exact
        // fingerprinted painter prologue places its own caller at ESP+42Ch.
        lea eax, [ebx + 36]
        push eax
        push dword ptr [ebx + 8]
        push dword ptr [ebx + 4]
        push dword ptr [ebx + 28]
        call wawvr_observe_native_vr_prompt_key
        add esp, 16
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [wawvr_native_vr_prompt_key_original]
    }
}

extern "C" __declspec(naked) void wawvr_native_vr_prompt_encoded_bridge() noexcept {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        // Exact005C6F05 call: original entry ESP+4/8/12 hold the
        // encoded-message/context/policy arguments of the native cdecl call.
        // Its three outstanding arguments put painter caller at ESP+438h.
        lea eax, [ebx + 36]
        push eax
        push dword ptr [ebx + 8]
        push dword ptr [ebx + 4]
        push dword ptr [ebx + 48]
        push dword ptr [ebx + 44]
        push dword ptr [ebx + 40]
        call wawvr_observe_native_vr_prompt_encoded
        add esp, 24
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [wawvr_native_vr_prompt_encoded_original]
    }
}

extern "C" __declspec(naked) void wawvr_native_vr_prompt_cg_bridge() noexcept {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        // Exact00437FE5 cdecl3 source/context/policy stack; EBX on entry is
        // the native local-client argument. Its painter parent is ESP+40h.
        lea eax, [ebx + 36]
        push eax
        push dword ptr [ebx + 16]
        push dword ptr [ebx + 48]
        push dword ptr [ebx + 44]
        push dword ptr [ebx + 40]
        call wawvr_resolve_native_vr_prompt_cg
        add esp, 20
        test eax, eax
        jz cg_native_source
        // Only the transient DISPLAY argument slot is redirected. The
        // original encoded source is untouched, and the pinned caller drops
        // these three arguments immediately after native formatting returns.
        mov dword ptr [ebx + 40], eax
    cg_native_source:
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [wawvr_native_vr_prompt_cg_original]
    }
}

extern "C" __declspec(naked) void wawvr_native_vr_prompt_cursor_bridge() noexcept {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        // Exact004512E4 cdecl3 args; EDI remains cursor painter localClient.
        // The full139-byte native use-string path places parent at ESP+11Ch.
        lea eax, [ebx + 36]
        push eax
        push dword ptr [ebx]
        push dword ptr [ebx + 48]
        push dword ptr [ebx + 44]
        push dword ptr [ebx + 40]
        call wawvr_resolve_native_vr_prompt_cursor
        add esp, 20
        test eax, eax
        jz cursor_native_source
        mov dword ptr [ebx + 40], eax
    cursor_native_source:
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [wawvr_native_vr_prompt_cursor_original]
    }
}
#endif
