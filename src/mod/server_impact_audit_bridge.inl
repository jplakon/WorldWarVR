// SPDX-License-Identifier: GPL-3.0-only
// Verbatim production bridges shared with the x86 ABI test. Both replace only
// native `add esp,16; test al,al` AFTER the original server BulletTrace returns.
#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) void wawvr_server_plain_impact_bridge() noexcept {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, -16
        fxsave [esp]
        push 0
        push dword ptr [ebx + 28]
        push dword ptr [ebx + 16]
        push dword ptr [ebx + 8]
        push dword ptr [ebx + 4]
        push dword ptr [ebx]
        call wawvr_observe_server_impact
        add esp, 24
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        add esp, 16
        test al, al
        jmp dword ptr [wawvr_server_plain_impact_resume]
    }
}

extern "C" __declspec(naked) void wawvr_server_penetrating_impact_bridge() noexcept {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 527
        and esp, -16
        fxsave [esp]
        push 1
        push dword ptr [ebx + 28]
        push dword ptr [ebx + 16]
        push dword ptr [ebx + 8]
        push dword ptr [ebx + 4]
        push dword ptr [ebx]
        call wawvr_observe_server_impact
        add esp, 24
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        add esp, 16
        test al, al
        jmp dword ptr [wawvr_server_penetrating_impact_resume]
    }
}
#endif
