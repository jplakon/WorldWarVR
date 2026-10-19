// SPDX-License-Identifier: GPL-3.0-only
// Shared verbatim by the exact-SP observer and its synthetic x86 ABI fixture.
// Callbacks are diagnostic/TLS observers only; they return no permission token.
#if defined(_MSC_VER) && defined(_M_IX86)
__declspec(naked) void grenade_native_enqueue_bridge() {
    __asm {
        pushfd
        pushad
        mov eax, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        mov dword ptr [esp + 512], eax
        // Saved EAX=PS, ECX=raw event; entry stack DWORD argument is base+40.
        push dword ptr [eax + 028h]
        push dword ptr [eax + 018h]
        push dword ptr [eax + 01Ch]
        call grenade_native_enqueue_begin_impl
        lea esp, [esp + 12]
        fxrstor [esp]
        mov esp, dword ptr [esp + 512]
        popad
        popfd

        // Native cdecl callee receives a copy, never the caller's argument slot.
        // PUSH/CALL/LEA do not overwrite the original/native-result EFLAGS.
        push dword ptr [esp + 4]
        call dword ptr [g_grenade_producer_trampoline]
        lea esp, [esp + 4]

        pushfd
        pushad
        mov eax, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        mov dword ptr [esp + 512], eax
        call grenade_native_enqueue_end_impl
        fxrstor [esp]
        mov esp, dword ptr [esp + 512]
        popad
        popfd
        ret
    }
}

__declspec(naked) void grenade_native_dispatcher_bridge() {
    __asm {
        pushfd
        pushad
        mov eax, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        mov dword ptr [esp + 512], eax
        // Original EAX is raw signed-start bit pattern; ECX is player entity.
        push dword ptr [eax + 01Ch]
        push dword ptr [eax + 018h]
        call grenade_native_dispatch_begin_impl
        lea esp, [esp + 8]
        fxrstor [esp]
        mov esp, dword ptr [esp + 512]
        popad
        popfd
        jmp dword ptr [g_grenade_dispatcher_trampoline]
    }
}

__declspec(naked) void grenade_native_dispatcher_end_bridge() {
    __asm {
        pushfd
        pushad
        mov eax, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        mov dword ptr [esp + 512], eax
        // Pinned epilogue local is entry ESP+0x10 = saved-context base+0x34.
        push dword ptr [eax + 034h]
        call grenade_native_dispatch_end_impl
        lea esp, [esp + 4]
        fxrstor [esp]
        mov esp, dword ptr [esp + 512]
        popad
        popfd
        jmp dword ptr [g_grenade_dispatcher_end_trampoline]
    }
}

__declspec(naked) void grenade_native_ready_bridge() {
    __asm {
        pushfd
        pushad
        mov eax, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        mov dword ptr [esp + 512], eax
        push dword ptr [eax + 01Ch]
        call grenade_native_inline_ready_begin_impl
        lea esp, [esp + 4]
        fxrstor [esp]
        mov esp, dword ptr [esp + 512]
        popad
        popfd
        // Native ready routine has EAX=PS and no stack arguments.
        call dword ptr [g_grenade_ready_trampoline]
        pushfd
        pushad
        mov eax, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        mov dword ptr [esp + 512], eax
        call grenade_native_inline_end_impl
        fxrstor [esp]
        mov esp, dword ptr [esp + 512]
        popad
        popfd
        ret
    }
}

__declspec(naked) void grenade_native_inline_fire_bridge() {
    __asm {
        pushfd
        pushad
        mov eax, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        mov dword ptr [esp + 512], eax
        push dword ptr [eax + 01Ch]
        call grenade_native_inline_fire_begin_impl
        lea esp, [esp + 4]
        fxrstor [esp]
        mov esp, dword ptr [esp + 512]
        popad
        popfd
        // Native inline fire routine has EAX=pmove and no stack arguments.
        call dword ptr [g_grenade_inline_fire_trampoline]
        pushfd
        pushad
        mov eax, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        mov dword ptr [esp + 512], eax
        call grenade_native_inline_end_impl
        fxrstor [esp]
        mov esp, dword ptr [esp + 512]
        popad
        popfd
        ret
    }
}

__declspec(naked) void grenade_native_init_bridge() {
    __asm {
        pushfd
        pushad
        mov eax, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        mov dword ptr [esp + 512], eax
        push dword ptr [eax + 01Ch]
        call grenade_native_inline_init_begin_impl
        lea esp, [esp + 4]
        fxrstor [esp]
        mov esp, dword ptr [esp + 512]
        popad
        popfd
        // Native initializer has EAX=PS and no stack arguments. A native tail
        // jump into this entry retains its original caller return ownership.
        call dword ptr [g_grenade_initializer_trampoline]
        pushfd
        pushad
        mov eax, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        mov dword ptr [esp + 512], eax
        call grenade_native_inline_end_impl
        fxrstor [esp]
        mov esp, dword ptr [esp + 512]
        popad
        popfd
        ret
    }
}
#endif
