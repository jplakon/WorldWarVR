// SPDX-License-Identifier: GPL-3.0-only
// Shared verbatim by the exact-SP runtime and its x86 register/stack/SIMD test.
#if defined(_MSC_VER) && defined(_M_IX86)
__declspec(naked) void __cdecl call_context_native_pickup(
    std::uintptr_t, void*) {
    __asm {
        mov eax, dword ptr [esp + 4]
        push edi
        mov edi, dword ptr [esp + 12]
        call eax
        pop edi
        ret
    }
}

__declspec(naked) void __cdecl call_context_native_throw(
    std::uintptr_t, void*) {
    __asm {
        mov eax, dword ptr [esp + 4]
        push esi
        mov esi, dword ptr [esp + 12]
        call eax
        pop esi
        ret
    }
}

__declspec(naked) void context_native_pickup_bridge() {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        // Pinned dispatcher local ordinal is entry ESP+0x14. EBX is
        // entry ESP-36 after pushfd/pushad; preserve the RAW uint32 value.
        push dword ptr [ebx + 038h]
        push edi
        call context_native_pickup_bridge_impl
        add esp, 8
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        ret
    }
}

__declspec(naked) void context_native_throw_event_bridge() {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        push dword ptr [ebx + 038h]
        push esi
        call context_native_throw_event_bridge_impl
        add esp, 8
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        ret
    }
}

__declspec(naked) void context_native_fire_gate_bridge() {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        push eax
        call context_native_fire_gate_impl
        add esp, 4
        test eax, eax
        jz native_passthrough
        cmp eax, 2
        je native_cancel
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        ret
    native_cancel:
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        mov eax, dword ptr [eax]
        jmp dword ptr [g_contextual_native_end]
    native_passthrough:
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [g_contextual_fire_trampoline]
    }
}
#endif
