// SPDX-License-Identifier: GPL-3.0-only
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(_MSC_VER) && defined(_M_IX86)
namespace {

alignas(16) std::array<std::byte, 512> g_host_fx{}, g_before_fx{}, g_input_fx{},
    g_native_fx{}, g_after_fx{};
alignas(16) const std::array<std::uint32_t, 4> g_seed_xmm{
    0x11223344U, 0x55667788U, 0x99AABBCCU, 0xDDEEFF00U};
alignas(16) const std::array<std::uint32_t, 4> g_native_xmm{
    0xAABBCCDDU, 0xEEFF0011U, 0x22334455U, 0x66778899U};
const std::uint32_t g_seed_mxcsr = 0x3F80U, g_native_mxcsr = 0x5F80U,
    g_clobber_mxcsr = 0x1F80U;
std::array<std::uint32_t, 7> g_before_regs{}, g_input_regs{}, g_after_regs{};
std::uintptr_t g_host_esp{}, g_expected_after_esp{}, g_after_esp{};
std::uint32_t g_before_flags{}, g_input_flags{}, g_native_flags{}, g_after_flags{};
std::uint32_t g_raw{}, g_parameter{}, g_parameter_after{}, g_local_seen{},
    g_local_guard_seen{}, g_callback_ps{}, g_callback_event{}, g_callback_parameter{},
    g_native_ps{}, g_native_event{}, g_native_parameter{}, g_callback_entity{},
    g_callback_start{}, g_callback_end{};
int g_path{}, g_native_calls{}, g_enqueue_begin_calls{}, g_enqueue_end_calls{},
    g_dispatch_begin_calls{}, g_dispatch_end_calls{}, g_ready_begin_calls{},
    g_fire_begin_calls{}, g_init_begin_calls{}, g_inline_end_calls{};
std::uint32_t g_inline_raw_eax{};
void* g_grenade_producer_trampoline{};
void* g_grenade_dispatcher_trampoline{};
void* g_grenade_dispatcher_end_trampoline{};
void* g_grenade_ready_trampoline{};
void* g_grenade_inline_fire_trampoline{};
void* g_grenade_initializer_trampoline{};
void* g_bridge{};

// Deliberately violates even nonvolatile-GPR preservation to test the bridge's
// memory-held save-frame pointer. ESP/return ownership remains cdecl-correct.
__declspec(naked) void clobber_callback_context() noexcept {
    __asm {
        fninit
        fldz
        ldmxcsr dword ptr [g_clobber_mxcsr]
        xorps xmm0, xmm0
        xorps xmm1, xmm1
        xorps xmm2, xmm2
        xorps xmm3, xmm3
        xorps xmm4, xmm4
        xorps xmm5, xmm5
        xorps xmm6, xmm6
        xorps xmm7, xmm7
        mov eax, 0DEADBEEFh
        mov ebx, 0DEADBEEFh
        mov ecx, 0DEADBEEFh
        mov edx, 0DEADBEEFh
        mov esi, 0DEADBEEFh
        mov edi, 0DEADBEEFh
        mov ebp, 0DEADBEEFh
        push 0202h
        popfd
        ret
    }
}

__declspec(naked) void __cdecl grenade_native_enqueue_begin_impl(
    void*, std::uint32_t, std::uint32_t) noexcept {
    __asm {
        inc dword ptr [g_enqueue_begin_calls]
        mov eax, dword ptr [esp + 4]
        mov dword ptr [g_callback_ps], eax
        mov eax, dword ptr [esp + 8]
        mov dword ptr [g_callback_event], eax
        mov eax, dword ptr [esp + 12]
        mov dword ptr [g_callback_parameter], eax
        call clobber_callback_context
        ret
    }
}

__declspec(naked) void __cdecl grenade_native_enqueue_end_impl() noexcept {
    __asm {
        inc dword ptr [g_enqueue_end_calls]
        call clobber_callback_context
        ret
    }
}

__declspec(naked) void __cdecl grenade_native_dispatch_begin_impl(
    void*, std::uint32_t) noexcept {
    __asm {
        inc dword ptr [g_dispatch_begin_calls]
        mov eax, dword ptr [esp + 4]
        mov dword ptr [g_callback_entity], eax
        mov eax, dword ptr [esp + 8]
        mov dword ptr [g_callback_start], eax
        call clobber_callback_context
        ret
    }
}

__declspec(naked) void __cdecl grenade_native_dispatch_end_impl(
    std::uint32_t) noexcept {
    __asm {
        inc dword ptr [g_dispatch_end_calls]
        mov eax, dword ptr [esp + 4]
        mov dword ptr [g_callback_end], eax
        call clobber_callback_context
        ret
    }
}

__declspec(naked) void __cdecl grenade_native_inline_ready_begin_impl(void*) noexcept {
    __asm {
        inc dword ptr [g_ready_begin_calls]
        mov eax, dword ptr [esp + 4]
        mov dword ptr [g_inline_raw_eax], eax
        call clobber_callback_context
        ret
    }
}

__declspec(naked) void __cdecl grenade_native_inline_fire_begin_impl(void*) noexcept {
    __asm {
        inc dword ptr [g_fire_begin_calls]
        mov eax, dword ptr [esp + 4]
        mov dword ptr [g_inline_raw_eax], eax
        call clobber_callback_context
        ret
    }
}

__declspec(naked) void __cdecl grenade_native_inline_init_begin_impl(void*) noexcept {
    __asm {
        inc dword ptr [g_init_begin_calls]
        mov eax, dword ptr [esp + 4]
        mov dword ptr [g_inline_raw_eax], eax
        call clobber_callback_context
        ret
    }
}

__declspec(naked) void __cdecl grenade_native_inline_end_impl() noexcept {
    __asm {
        inc dword ptr [g_inline_end_calls]
        call clobber_callback_context
        ret
    }
}

#include "grenade_queue_observer_bridge.inl"

// Models the verified parent helper's tail jump: no additional return address
// may be manufactured between that caller and the shared initializer wrapper.
__declspec(naked) void initializer_tail_forwarder_fixture() noexcept {
    __asm {
        jmp grenade_native_init_bridge
    }
}

// Records the complete context delivered by entry/end tail jumps without
// changing it, then enters the selected synthetic continuation.
__declspec(naked) void record_native_input() noexcept {
    __asm {
        mov dword ptr [g_input_regs + 0], eax
        mov dword ptr [g_input_regs + 4], ebx
        mov dword ptr [g_input_regs + 8], ecx
        mov dword ptr [g_input_regs + 12], edx
        mov dword ptr [g_input_regs + 16], esi
        mov dword ptr [g_input_regs + 20], edi
        mov dword ptr [g_input_regs + 24], ebp
        pushfd
        pop dword ptr [g_input_flags]
        fxsave [g_input_fx]
        ret
    }
}

__declspec(naked) void producer_native_fixture() noexcept {
    __asm {
        call record_native_input
        inc dword ptr [g_native_calls]
        mov dword ptr [g_native_ps], eax
        mov dword ptr [g_native_event], ecx
        mov eax, dword ptr [esp + 4]
        mov dword ptr [g_native_parameter], eax
        // The native callee may touch its copied argument, not the caller slot.
        mov dword ptr [esp + 4], 0BADF00Dh
        fninit
        fldpi
        fld1
        ldmxcsr dword ptr [g_native_mxcsr]
        movdqu xmm0, xmmword ptr [g_native_xmm]
        movdqu xmm1, xmmword ptr [g_native_xmm]
        movdqu xmm2, xmmword ptr [g_native_xmm]
        movdqu xmm3, xmmword ptr [g_native_xmm]
        movdqu xmm4, xmmword ptr [g_native_xmm]
        movdqu xmm5, xmmword ptr [g_native_xmm]
        movdqu xmm6, xmmword ptr [g_native_xmm]
        movdqu xmm7, xmmword ptr [g_native_xmm]
        mov eax, 0A1A1A1A1h
        mov ebx, 0B1B1B1B1h
        mov ecx, 0C1C1C1C1h
        mov edx, 0D1D1D1D1h
        mov esi, 0E1E1E1E1h
        mov edi, 0F1F1F1F1h
        mov ebp, 0ABABABABh
        push 0247h
        popfd
        pushfd
        pop dword ptr [g_native_flags]
        fxsave [g_native_fx]
        ret
    }
}

__declspec(naked) void dispatcher_native_fixture() noexcept {
    __asm {
        call record_native_input
        pushfd
        inc dword ptr [g_native_calls]
        popfd
        ret
    }
}

__declspec(naked) void inline_native_fixture() noexcept {
    __asm {
        call record_native_input
        inc dword ptr [g_native_calls]
        fninit
        fldpi
        fld1
        ldmxcsr dword ptr [g_native_mxcsr]
        movdqu xmm0, xmmword ptr [g_native_xmm]
        movdqu xmm1, xmmword ptr [g_native_xmm]
        movdqu xmm2, xmmword ptr [g_native_xmm]
        movdqu xmm3, xmmword ptr [g_native_xmm]
        movdqu xmm4, xmmword ptr [g_native_xmm]
        movdqu xmm5, xmmword ptr [g_native_xmm]
        movdqu xmm6, xmmword ptr [g_native_xmm]
        movdqu xmm7, xmmword ptr [g_native_xmm]
        mov eax, 0A1A1A1A1h
        mov ebx, 0B1B1B1B1h
        mov ecx, 0C1C1C1C1h
        mov edx, 0D1D1D1D1h
        mov esi, 0E1E1E1E1h
        mov edi, 0F1F1F1F1h
        mov ebp, 0ABABABABh
        push 0247h
        popfd
        pushfd
        pop dword ptr [g_native_flags]
        fxsave [g_native_fx]
        ret
    }
}

__declspec(naked) void dispatcher_end_native_fixture() noexcept {
    __asm {
        call record_native_input
        pushfd
        inc dword ptr [g_native_calls]
        popfd
        push eax
        mov eax, dword ptr [esp + 014h]
        mov dword ptr [g_local_seen], eax
        mov eax, dword ptr [esp + 018h]
        mov dword ptr [g_local_guard_seen], eax
        pop eax
        // Exact native displaced epilogue: 5F 5E 5B 8B E5 5D C3.
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    }
}

__declspec(naked) void call_actual_bridge_fixture() noexcept {
    __asm {
        pushfd
        pushad
        mov dword ptr [g_host_esp], esp
        fxsave [g_host_fx]
        fninit
        fld1
        fldz
        ldmxcsr dword ptr [g_seed_mxcsr]
        movdqu xmm0, xmmword ptr [g_seed_xmm]
        movdqu xmm1, xmmword ptr [g_seed_xmm]
        movdqu xmm2, xmmword ptr [g_seed_xmm]
        movdqu xmm3, xmmword ptr [g_seed_xmm]
        movdqu xmm4, xmmword ptr [g_seed_xmm]
        movdqu xmm5, xmmword ptr [g_seed_xmm]
        movdqu xmm6, xmmword ptr [g_seed_xmm]
        movdqu xmm7, xmmword ptr [g_seed_xmm]
        mov ebx, 0B2B2B2B2h
        mov edx, 0D4D4D4D4h
        mov esi, 065656565h
        mov edi, 077777777h
        cmp dword ptr [g_path], 2
        je prepare_epilogue
        mov ebp, 0B0B0B0B0h
        cmp dword ptr [g_path], 3
        jge prepare_inline
        cmp dword ptr [g_path], 1
        je prepare_dispatch
        push dword ptr [g_parameter]
        mov eax, 011111111h
        mov ecx, dword ptr [g_raw]
        jmp regular_ready
    prepare_dispatch:
        mov eax, dword ptr [g_raw]
        mov ecx, 055555555h
        jmp regular_ready
    prepare_inline:
        mov eax, dword ptr [g_raw]
        mov ecx, 0C3C3C3C3h
    regular_ready:
        mov dword ptr [g_expected_after_esp], esp
        mov dword ptr [g_before_regs + 0], eax
        mov dword ptr [g_before_regs + 4], ebx
        mov dword ptr [g_before_regs + 8], ecx
        mov dword ptr [g_before_regs + 12], edx
        mov dword ptr [g_before_regs + 16], esi
        mov dword ptr [g_before_regs + 20], edi
        mov dword ptr [g_before_regs + 24], ebp
        fxsave [g_before_fx]
        push 0247h
        popfd
        pushfd
        pop dword ptr [g_before_flags]
        call dword ptr [g_bridge]
        jmp capture_return
    prepare_epilogue:
        mov dword ptr [g_expected_after_esp], esp
        push offset capture_return
        push ebp
        mov ebp, esp
        sub esp, 32
        push ebx
        push esi
        push edi
        mov eax, dword ptr [g_raw]
        mov dword ptr [esp + 010h], eax
        mov dword ptr [esp + 014h], 0ACACACACh
        mov eax, 011111111h
        mov ecx, 022222222h
        mov dword ptr [g_before_regs + 0], eax
        mov dword ptr [g_before_regs + 4], ebx
        mov dword ptr [g_before_regs + 8], ecx
        mov dword ptr [g_before_regs + 12], edx
        mov dword ptr [g_before_regs + 16], esi
        mov dword ptr [g_before_regs + 20], edi
        mov dword ptr [g_before_regs + 24], ebp
        fxsave [g_before_fx]
        push 0247h
        popfd
        pushfd
        pop dword ptr [g_before_flags]
        jmp dword ptr [g_bridge]
    capture_return:
        mov dword ptr [g_after_regs + 0], eax
        mov dword ptr [g_after_regs + 4], ebx
        mov dword ptr [g_after_regs + 8], ecx
        mov dword ptr [g_after_regs + 12], edx
        mov dword ptr [g_after_regs + 16], esi
        mov dword ptr [g_after_regs + 20], edi
        mov dword ptr [g_after_regs + 24], ebp
        mov dword ptr [g_after_esp], esp
        pushfd
        pop dword ptr [g_after_flags]
        fxsave [g_after_fx]
        cmp dword ptr [g_path], 0
        jne restore_host
        mov eax, dword ptr [esp]
        mov dword ptr [g_parameter_after], eax
        lea esp, [esp + 4]
    restore_host:
        fxrstor [g_host_fx]
        mov esp, dword ptr [g_host_esp]
        popad
        popfd
        ret
    }
}

bool same_fx(const std::array<std::byte, 512>& left,
    const std::array<std::byte, 512>& right) {
    if (std::memcmp(left.data(), right.data(), 5) != 0 ||
        std::memcmp(left.data() + 0x18, right.data() + 0x18, 4) != 0 ||
        std::memcmp(left.data() + 0xA0, right.data() + 0xA0, 128) != 0) return false;
    for (std::size_t slot = 0; slot < 8U; ++slot) {
        if (std::memcmp(left.data() + 0x20 + slot * 16U,
            right.data() + 0x20 + slot * 16U, 10) != 0) return false;
    }
    return true;
}

}  // namespace

int main() {
    int failures = 0;
    const auto check = [&](const bool passed, const char* message) {
        if (!passed) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
    };
    g_grenade_producer_trampoline = reinterpret_cast<void*>(&producer_native_fixture);
    g_grenade_dispatcher_trampoline = reinterpret_cast<void*>(&dispatcher_native_fixture);
    g_grenade_dispatcher_end_trampoline = reinterpret_cast<void*>(&dispatcher_end_native_fixture);
    g_grenade_ready_trampoline = reinterpret_cast<void*>(&inline_native_fixture);
    g_grenade_inline_fire_trampoline = reinterpret_cast<void*>(&inline_native_fixture);
    g_grenade_initializer_trampoline = reinterpret_cast<void*>(&inline_native_fixture);
    int cases = 0;
    int oldCases = 0;
    for (const auto raw : {0U, 255U, 0xFFFFFFFEU}) {
        g_raw = raw;
        g_parameter = 0xABCD0000U | (raw & 0xFFFFU);
        for (int path = 0; path < 6; ++path) {
            g_path = path;
            g_native_calls = g_enqueue_begin_calls = g_enqueue_end_calls =
                g_dispatch_begin_calls = g_dispatch_end_calls = 0;
            g_ready_begin_calls = g_fire_begin_calls = g_init_begin_calls =
                g_inline_end_calls = 0;
            g_bridge = path == 0 ? reinterpret_cast<void*>(&grenade_native_enqueue_bridge)
                : path == 1 ? reinterpret_cast<void*>(&grenade_native_dispatcher_bridge)
                : path == 2 ? reinterpret_cast<void*>(&grenade_native_dispatcher_end_bridge)
                : path == 3 ? reinterpret_cast<void*>(&grenade_native_ready_bridge)
                : path == 4 ? reinterpret_cast<void*>(&grenade_native_inline_fire_bridge)
                : reinterpret_cast<void*>(&initializer_tail_forwarder_fixture);
            call_actual_bridge_fixture();
            ++cases;
            if (path < 5) ++oldCases;
            check(g_native_calls == 1, "original native continuation executes exactly once");
            check(g_after_esp == g_expected_after_esp, "caller ESP/argument ownership survives");
            check(g_input_regs == g_before_regs, "all native input GPRs survive clobbering observer");
            check(g_input_flags == g_before_flags, "native input EFLAGS survive observer");
            check(same_fx(g_input_fx, g_before_fx),
                "native input x87/MXCSR/all XMM registers survive observer");
            if (path == 0) {
                const std::array<std::uint32_t, 7> expectedOutput{
                    0xA1A1A1A1U, 0xB1B1B1B1U, 0xC1C1C1C1U, 0xD1D1D1D1U,
                    0xE1E1E1E1U, 0xF1F1F1F1U, 0xABABABABU};
                check(g_enqueue_begin_calls == 1 && g_enqueue_end_calls == 1 &&
                    g_dispatch_begin_calls == 0 && g_dispatch_end_calls == 0,
                    "producer before/after observers each execute exactly once");
                check(g_callback_ps == 0x11111111U && g_callback_event == raw &&
                    g_callback_parameter == g_parameter &&
                    g_native_ps == 0x11111111U && g_native_event == raw &&
                    g_native_parameter == g_parameter,
                    "producer PS/raw event/raw DWORD parameter reach observer and native unchanged");
                check(g_parameter_after == g_parameter,
                    "native modification of copied parameter cannot touch caller parameter");
                check(g_after_regs == expectedOutput && g_after_flags == g_native_flags &&
                    same_fx(g_after_fx, g_native_fx),
                    "after observer preserves all native result GPR/EFLAGS/x87/MXCSR/XMM");
            } else if (path == 1) {
                check(g_dispatch_begin_calls == 1 && g_dispatch_end_calls == 0 &&
                    g_enqueue_begin_calls == 0 && g_enqueue_end_calls == 0 &&
                    g_callback_entity == 0x55555555U && g_callback_start == raw,
                    "dispatcher entry callback receives exact ECX entity and raw EAX start");
            } else if (path == 2) {
                check(g_dispatch_end_calls == 1 && g_dispatch_begin_calls == 0 &&
                    g_enqueue_begin_calls == 0 && g_enqueue_end_calls == 0 &&
                    g_callback_end == raw && g_local_seen == raw &&
                    g_local_guard_seen == 0xACACACACU,
                    "dispatcher local ordinal and adjacent caller local remain untouched");
                check(g_after_flags == g_before_flags && same_fx(g_after_fx, g_before_fx),
                    "exact seven-byte epilogue receives restored flags and floating-point state");
            } else {
                const std::array<std::uint32_t, 7> expectedOutput{
                    0xA1A1A1A1U, 0xB1B1B1B1U, 0xC1C1C1C1U, 0xD1D1D1D1U,
                    0xE1E1E1E1U, 0xF1F1F1F1U, 0xABABABABU};
                check(g_inline_raw_eax == raw &&
                    g_ready_begin_calls == (path == 3 ? 1 : 0) &&
                    g_fire_begin_calls == (path == 4 ? 1 : 0) &&
                    g_init_begin_calls == (path == 5 ? 1 : 0) &&
                    g_inline_end_calls == 1 && g_enqueue_begin_calls == 0 &&
                    g_enqueue_end_calls == 0 && g_dispatch_begin_calls == 0 &&
                    g_dispatch_end_calls == 0,
                    "ready/fire/init exact raw EAX and shared after callback run once without stack args");
                check(g_after_regs == expectedOutput && g_after_flags == g_native_flags &&
                    same_fx(g_after_fx, g_native_fx),
                    "ready/fire/init preserve every native result GPR/EFLAGS/x87/MXCSR/XMM");
            }
        }
    }
    check(oldCases == 15, "original 15 producer/dispatcher/ready/fire ABI fixtures are retained");
    if (failures == 0) std::printf(
        "PASS: %d exact shared producer/dispatcher/inline ready/fire/init bridge fixtures; "
        "native once, raw arguments/locals, complete GPR/flags/FPU/SIMD and stack preserved; "
        "synthetic ABI only, native ownership not claimed\n", cases);
    return failures == 0 ? 0 : 1;
}
#else
int main() { return 1; }
#endif
