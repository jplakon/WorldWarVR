#include "d3d9ex_bootstrap.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <span>

namespace {

int failures = 0;

void check(const bool condition, const char* const message) {
    if (!condition) {
        std::fprintf(stderr, "D3D9Ex bootstrap test failed: %s\n", message);
        ++failures;
    }
}

}  // namespace

int main() {
    using namespace wawvr::mod;

    constexpr std::uintptr_t factory_site = 0x006D62BB;
    constexpr std::uintptr_t create_site = 0x006D6056;
    constexpr std::uintptr_t factory_thunk = 0x10001000;
    constexpr std::uintptr_t create_thunk = 0x10002000;
    D3D9ExBootstrapPatchPlan plan{};
    check(
        build_d3d9ex_bootstrap_patch_plan(
            factory_site, create_site, factory_thunk, create_thunk, &plan),
        "valid x86 targets did not produce a patch plan");
    check(plan.factory_call[0] == 0xE8,
          "factory redirect is not a relative call");
    check(
        plan.create_device_dispatch[0] == 0x50 &&
            plan.create_device_dispatch[1] == 0xE8,
        "CreateDevice redirect did not preserve push eax before the call");

    check(
        inspect_d3d9ex_bootstrap_context(
            kExpectedD3D9FactoryCall,
            kExpectedD3D9CreateDeviceDispatch,
            plan) == D3D9ExBootstrapContextState::expected,
        "the exact stock call spans were not accepted");
    check(
        inspect_d3d9ex_bootstrap_context(
            plan.factory_call,
            plan.create_device_dispatch,
            plan) == D3D9ExBootstrapContextState::already_patched,
        "the generated redirect spans were not recognized idempotently");

    auto partial = plan.create_device_dispatch;
    partial.back() ^= 0x01;
    check(
        inspect_d3d9ex_bootstrap_context(
            plan.factory_call, partial, plan) ==
            D3D9ExBootstrapContextState::mismatch,
        "a partial CreateDevice redirect did not fail closed");

    const std::span<const std::uint8_t> truncated{
        kExpectedD3D9FactoryCall.data(),
        kExpectedD3D9FactoryCall.size() - 1};
    check(
        inspect_d3d9ex_bootstrap_context(
            truncated, kExpectedD3D9CreateDeviceDispatch, plan) ==
            D3D9ExBootstrapContextState::mismatch,
        "a truncated factory call did not fail closed");

    std::size_t changed = 0;
    for (std::size_t index = 0; index < plan.factory_call.size(); ++index) {
        changed += plan.factory_call[index] !=
                           kExpectedD3D9FactoryCall[index]
                       ? 1U
                       : 0U;
    }
    for (std::size_t index = 0;
         index < plan.create_device_dispatch.size(); ++index) {
        changed += plan.create_device_dispatch[index] !=
                           kExpectedD3D9CreateDeviceDispatch[index]
                       ? 1U
                       : 0U;
    }
    check(changed >= 8 && changed <= 10,
          "the two compact call redirects changed an unexpected byte count");

    const D3D9ExBridgeDeviceIdentity exact_device{
        .device = 0x1000,
        .vtable = 0x2000,
        .generation = 7,
    };
    check(
        d3d9ex_bridge_device_identity_matches(
            exact_device, 0x1000, 0x2000),
        "the exact published device identity was rejected");
    check(
        !d3d9ex_bridge_device_identity_matches(
            exact_device, 0x3000, 0x2000),
        "another Ex-capable device was accepted by shared-bridge readiness");
    check(
        !d3d9ex_bridge_device_identity_matches(
            exact_device, 0x1000, 0x4000),
        "a device with a changed vtable was accepted");
    check(
        !d3d9ex_bridge_device_identity_matches(
            D3D9ExBridgeDeviceIdentity{.device = 0x1000,
                                       .vtable = 0x2000},
             0x1000, 0x2000),
        "an unpublished identity generation was accepted");

    constexpr D3D9ExCompatibilitySlots required_slots{
        .reset = 0x10,
        .create_texture = 0x20,
        .create_volume_texture = 0x30,
        .create_cube_texture = 0x40,
    };
    check(
        d3d9ex_compatibility_slots_match(required_slots, required_slots),
        "the exact permanent compatibility slots were rejected");
    auto overwritten_slots = required_slots;
    overwritten_slots.reset = 0x50;
    check(
        !d3d9ex_compatibility_slots_match(
            overwritten_slots, required_slots),
        "a foreign Reset replacement remained bridge-ready");
    auto incomplete_required_slots = required_slots;
    incomplete_required_slots.create_cube_texture = 0;
    check(
        !d3d9ex_compatibility_slots_match(
            required_slots, incomplete_required_slots),
        "an incomplete required compatibility route was accepted");
    check(
        d3d9ex_should_try_legacy_device_fallback(true, false),
        "an ordinary CreateDeviceEx failure did not permit legacy fallback");
    check(
        !d3d9ex_should_try_legacy_device_fallback(true, true),
        "a compatibility-install failure escaped through legacy fallback");
    check(
        !d3d9ex_should_try_legacy_device_fallback(false, false),
        "legacy fallback was attempted without a D3D9 factory");

    check(
        select_d3d9ex_reset_route(
            exact_device, 0x1000, 0x2000, false, 0, 0) ==
            D3D9ExResetRoute::direct_reset_ex,
        "an exact device without a handler did not select direct ResetEx");
    check(
        select_d3d9ex_reset_route(
            exact_device, 0x1000, 0x2000, true, 0x1000, 7) ==
            D3D9ExResetRoute::registered_handler,
        "the exact generation did not select its reset handler");
    check(
        select_d3d9ex_reset_route(
            exact_device, 0x1000, 0x2000, true, 0x1000, 6) ==
            D3D9ExResetRoute::direct_reset_ex,
        "a stale handler generation intercepted ResetEx");
    check(
        select_d3d9ex_reset_route(
            exact_device, 0x3000, 0x2000, true, 0x1000, 7) ==
            D3D9ExResetRoute::original_reset,
        "a foreign device did not preserve original Reset");

    return failures == 0 ? 0 : 1;
}
