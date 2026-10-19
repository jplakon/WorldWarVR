// SPDX-License-Identifier: GPL-3.0-only
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(_MSC_VER) && defined(_M_IX86)
namespace {
alignas(16) std::array<std::byte, 512> g_host_fx{}, g_before_fx{}, g_after_fx{};
alignas(16) const std::array<std::uint32_t, 4> g_xmm_seed{
    0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00};
const std::uint32_t g_seed_mxcsr = 0x3F80, g_clobber_mxcsr = 0x5F80;
std::array<std::byte, 0x950> g_player_state{};
std::uintptr_t g_pmove{};
std::uintptr_t g_contextual_native_end{}, g_bridge_target{};
void* g_contextual_fire_trampoline{};
std::uintptr_t g_host_esp{}, g_call_esp{}, g_after_esp{}, g_native_arg{};
std::uint32_t g_after_eax{}, g_after_ebx{}, g_after_ecx{}, g_after_edx{},
    g_after_esi{}, g_after_edi{}, g_after_ebp{}, g_flags_before{}, g_flags_after{};
int g_route{};
std::uint32_t g_expected_ordinal{};
bool g_service_saw_args{}, g_native_called{};

void __cdecl call_context_native_pickup(std::uintptr_t, void*);
void __cdecl call_context_native_throw(std::uintptr_t, void*);

void clobber_adapter_floating_point_state() noexcept {
    __asm {
        xorps xmm0, xmm0
        xorps xmm1, xmm1
        xorps xmm2, xmm2
        xorps xmm3, xmm3
        xorps xmm4, xmm4
        xorps xmm5, xmm5
        xorps xmm6, xmm6
        xorps xmm7, xmm7
        fninit
        fldpi
        ldmxcsr dword ptr [g_clobber_mxcsr]
    }
}

__declspec(naked) void native_eax_fixture() noexcept {
    __asm {
        mov byte ptr [g_native_called], 1
        mov dword ptr [g_native_arg], eax
        ret
    }
}

__declspec(naked) void native_edi_fixture() noexcept {
    __asm {
        mov byte ptr [g_native_called], 1
        mov dword ptr [g_native_arg], edi
        ret
    }
}

__declspec(naked) void native_esi_fixture() noexcept {
    __asm {
        mov byte ptr [g_native_called], 1
        mov dword ptr [g_native_arg], esi
        ret
    }
}

int __cdecl context_native_fire_gate_impl(const void* pmove) noexcept {
    g_service_saw_args = pmove == &g_pmove;
    clobber_adapter_floating_point_state();
    return g_route;
}

void __cdecl context_native_pickup_bridge_impl(
    void* entity, const std::uint32_t ordinal) noexcept {
    g_service_saw_args = reinterpret_cast<std::uintptr_t>(entity) == 0x77777777 &&
        ordinal == g_expected_ordinal;
    call_context_native_pickup(
        reinterpret_cast<std::uintptr_t>(&native_edi_fixture), entity);
    clobber_adapter_floating_point_state();
}

void __cdecl context_native_throw_event_bridge_impl(
    void* entity, const std::uint32_t ordinal) noexcept {
    g_service_saw_args = reinterpret_cast<std::uintptr_t>(entity) == 0x65656565 &&
        ordinal == g_expected_ordinal;
    call_context_native_throw(
        reinterpret_cast<std::uintptr_t>(&native_esi_fixture), entity);
    clobber_adapter_floating_point_state();
}

#include "contextual_throwback_bridge.inl"

__declspec(naked) void call_actual_production_bridge_fixture() noexcept {
    __asm {
        pushfd
        pushad
        mov dword ptr [g_host_esp], esp
        sub esp, 32
        mov eax, dword ptr [g_expected_ordinal]
        mov dword ptr [esp + 010h], eax
        fxsave [g_host_fx]
        fninit
        fld1
        fldz
        ldmxcsr dword ptr [g_seed_mxcsr]
        movdqu xmm0, xmmword ptr [g_xmm_seed]
        movdqu xmm1, xmmword ptr [g_xmm_seed]
        movdqu xmm2, xmmword ptr [g_xmm_seed]
        movdqu xmm3, xmmword ptr [g_xmm_seed]
        movdqu xmm4, xmmword ptr [g_xmm_seed]
        movdqu xmm5, xmmword ptr [g_xmm_seed]
        movdqu xmm6, xmmword ptr [g_xmm_seed]
        movdqu xmm7, xmmword ptr [g_xmm_seed]
        mov eax, offset g_pmove
        mov ebx, 0B2B2B2B2h
        mov ecx, 0C3C3C3C3h
        mov edx, 0D4D4D4D4h
        mov esi, 065656565h
        mov edi, 077777777h
        mov ebp, 0B0B0B0B0h
        mov dword ptr [g_call_esp], esp
        fxsave [g_before_fx]
        stc
        pushfd
        pop dword ptr [g_flags_before]
        call dword ptr [g_bridge_target]
        mov dword ptr [g_after_eax], eax
        mov dword ptr [g_after_ebx], ebx
        mov dword ptr [g_after_ecx], ecx
        mov dword ptr [g_after_edx], edx
        mov dword ptr [g_after_esi], esi
        mov dword ptr [g_after_edi], edi
        mov dword ptr [g_after_ebp], ebp
        mov dword ptr [g_after_esp], esp
        pushfd
        pop dword ptr [g_flags_after]
        fxsave [g_after_fx]
        fxrstor [g_host_fx]
        mov esp, dword ptr [g_host_esp]
        popad
        popfd
        ret
    }
}
}  // namespace

int main() {
    int failures = 0;
    const auto check = [&](const bool passed, const char* text) {
        if (!passed) { std::fprintf(stderr, "FAIL: %s\n", text); ++failures; }
    };
    g_pmove = reinterpret_cast<std::uintptr_t>(g_player_state.data());
    g_contextual_native_end = reinterpret_cast<std::uintptr_t>(&native_eax_fixture);
    g_contextual_fire_trampoline = reinterpret_cast<void*>(&native_eax_fixture);
    // Zero, wrap-edge and wide raw values must survive unmasked. This is a
    // synthetic caller-stack ABI test, not native ordinal ownership proof.
    for (const auto ordinal : {0U, 255U, 0xFFFFFFFEU}) {
      g_expected_ordinal = ordinal;
      for (int path = 0; path < 5; ++path) {
        g_service_saw_args = g_native_called = false;
        g_native_arg = 0;
        g_route = path < 3 ? path : 0;
        g_bridge_target = path == 3
            ? reinterpret_cast<std::uintptr_t>(&context_native_pickup_bridge)
            : path == 4
                ? reinterpret_cast<std::uintptr_t>(&context_native_throw_event_bridge)
                : reinterpret_cast<std::uintptr_t>(&context_native_fire_gate_bridge);
        call_actual_production_bridge_fixture();
        check(g_service_saw_args,
            "exact entity register and raw dispatcher ordinal reach adapter");
        check(g_after_esp == g_call_esp, "caller stack ownership is unchanged");
        check(g_after_ebx == 0xB2B2B2B2 && g_after_ecx == 0xC3C3C3C3 &&
            g_after_edx == 0xD4D4D4D4 && g_after_esi == 0x65656565 &&
            g_after_edi == 0x77777777 && g_after_ebp == 0xB0B0B0B0,
            "all other GPRs survive fire, pickup and void-event adapters");
        check(g_after_eax == (path == 2 ? g_pmove :
              reinterpret_cast<std::uintptr_t>(&g_pmove)),
            "EAX is preserved except exact pmove-to-playerState cancel conversion");
        check(g_flags_after == g_flags_before, "all caller EFLAGS survive adapters");
        check(std::memcmp(g_before_fx.data() + 0xA0,
                          g_after_fx.data() + 0xA0, 128) == 0,
            "all eight XMM registers survive deliberately destructive callback");
        check(std::memcmp(g_before_fx.data(), g_after_fx.data(), 5) == 0 &&
              std::memcmp(g_before_fx.data() + 0x18,
                          g_after_fx.data() + 0x18, 4) == 0,
            "x87 control/status/tag and MXCSR survive callback");
        for (std::size_t slot = 0; slot < 8; ++slot)
            check(std::memcmp(g_before_fx.data() + 0x20 + slot * 16,
                              g_after_fx.data() + 0x20 + slot * 16, 10) == 0,
                "live x87 register stack payload survives callback");
        check(g_native_called == (path != 1),
            "hold path performs no native call; other exact paths reach native");
        if (path != 1) {
            const auto expected_native_arg = path == 2 ? g_pmove :
                path == 3 ? 0x77777777U : path == 4 ? 0x65656565U :
                reinterpret_cast<std::uintptr_t>(&g_pmove);
            check(g_native_arg == expected_native_arg,
                "native EAX/EDI/ESI target receives exact documented argument");
        }
      }
    }
    if (!failures) std::puts(
        "PASS: actual production throwback fire/pickup/void-event bridges preserve register/stack/FPU/SIMD ABI");
    return failures ? 1 : 0;
}
#else
int main() { return 1; }
#endif
