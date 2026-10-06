#include "d3d9ex_bridge.h"
#include "d3d9ex_bridge_lifecycle.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{

using wawvr::xr::detail::AbandonSerialDisposition;
using wawvr::xr::detail::CaptureEntryDisposition;
using wawvr::xr::detail::ConsumerFencePollDisposition;
using wawvr::xr::detail::LostDeviceResetPhase;
using wawvr::xr::detail::ProducerDeviceDisposition;
using wawvr::xr::detail::SharedBridgeSlotState;
using wawvr::xr::detail::SharedTextureDescriptionFacts;

struct TestSlot
{
    std::uint64_t serial = 0;
    SharedBridgeSlotState state = SharedBridgeSlotState::Free;
};

int failures = 0;

void expect(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_exact_serial_selection()
{
    std::array<TestSlot, 3> slots = {{
        {10, SharedBridgeSlotState::Ready},
        {11, SharedBridgeSlotState::Producing},
        {12, SharedBridgeSlotState::Ready},
    }};

    expect(wawvr::xr::detail::abandon_exact_serial(slots, 99) ==
               AbandonSerialDisposition::not_found,
           "an absent serial is rejected");
    expect(slots[0].state == SharedBridgeSlotState::Ready &&
               slots[1].state == SharedBridgeSlotState::Producing &&
               slots[2].state == SharedBridgeSlotState::Ready,
           "an absent serial changes no slot");

    expect(wawvr::xr::detail::abandon_exact_serial(slots, 12) ==
               AbandonSerialDisposition::retired,
           "the exact ready serial is retired");
    expect(slots[0].state == SharedBridgeSlotState::Ready &&
               slots[1].state == SharedBridgeSlotState::Producing &&
               slots[2].state == SharedBridgeSlotState::Free,
           "retiring a ready serial never frees another slot");
}

void test_producer_fence_retirement()
{
    std::array<TestSlot, 2> slots = {{
        {20, SharedBridgeSlotState::Producing},
        {21, SharedBridgeSlotState::Producing},
    }};

    expect(wawvr::xr::detail::abandon_exact_serial(slots, 20) ==
               AbandonSerialDisposition::producer_retirement_pending,
           "a producing serial accepts deferred abandonment");
    expect(slots[0].state == SharedBridgeSlotState::AbandonPending,
           "an abandoned producer remains non-reusable before its fence");
    expect(slots[1].state == SharedBridgeSlotState::Producing,
           "deferred abandonment is exact-serial only");
    expect(wawvr::xr::detail::producer_fence_pending(slots[0].state),
           "abandon-pending remains producer owned");
    expect(wawvr::xr::detail::producer_fence_completion_state(
               slots[0].state) == SharedBridgeSlotState::Free,
           "an abandoned slot becomes free only at producer-fence completion");
    expect(wawvr::xr::detail::producer_fence_completion_state(
               slots[1].state) == SharedBridgeSlotState::Ready,
           "a normal producer completion remains available for acquisition");
}

void test_nonblocking_newest_ready_selection()
{
    std::array<TestSlot, 4> slots = {{
        {11, SharedBridgeSlotState::Producing},
        {14, SharedBridgeSlotState::Ready},
        {12, SharedBridgeSlotState::Ready},
        {13, SharedBridgeSlotState::ConsumerPending},
    }};

    const auto selected =
        wawvr::xr::detail::select_latest_ready_serial(slots, 10);
    expect(selected && selected.index == 1 && selected.serial == 14,
           "nonblocking acquisition selects the newest completed newer serial");
    expect(slots[0].state == SharedBridgeSlotState::Producing &&
               slots[1].state == SharedBridgeSlotState::Ready &&
               slots[2].state == SharedBridgeSlotState::Ready &&
               slots[3].state == SharedBridgeSlotState::ConsumerPending,
           "selection preserves older Ready fallback until consumer validation succeeds");
}

void test_ready_selection_retires_only_stale_ready_slots()
{
    std::array<TestSlot, 6> slots = {{
        {20, SharedBridgeSlotState::Ready},
        {19, SharedBridgeSlotState::Producing},
        {18, SharedBridgeSlotState::AbandonPending},
        {17, SharedBridgeSlotState::Acquired},
        {16, SharedBridgeSlotState::ConsumerPending},
        {22, SharedBridgeSlotState::Ready},
    }};

    const auto selected =
        wawvr::xr::detail::select_latest_ready_serial(slots, 20);
    expect(selected && selected.index == 5 && selected.serial == 22,
           "selection advances to the newest ready serial above the consumed floor");
    expect(slots[0].state == SharedBridgeSlotState::Free,
           "a stale unacquired ready slot is retired");
    expect(slots[1].state == SharedBridgeSlotState::Producing &&
               slots[2].state == SharedBridgeSlotState::AbandonPending &&
               slots[3].state == SharedBridgeSlotState::Acquired &&
               slots[4].state == SharedBridgeSlotState::ConsumerPending,
           "stale cleanup never frees producer- or consumer-owned slots");
}

void test_validated_latest_selection_retires_only_older_ready_slots()
{
    std::array<TestSlot, 6> slots = {{
        {11, SharedBridgeSlotState::Producing},
        {14, SharedBridgeSlotState::Ready},
        {12, SharedBridgeSlotState::Ready},
        {13, SharedBridgeSlotState::ConsumerPending},
        {10, SharedBridgeSlotState::Acquired},
        {9, SharedBridgeSlotState::AbandonPending},
    }};

    const auto selected =
        wawvr::xr::detail::select_latest_ready_serial(slots, 10);
    expect(selected && selected.index == 1 && selected.serial == 14,
           "latest selection identifies the exact serial before cleanup");
    expect(slots[2].state == SharedBridgeSlotState::Ready,
           "an older Ready fallback survives selection-time validation");

    const std::size_t retired =
        wawvr::xr::detail::retire_ready_serials_before(
            slots, selected.serial);
    expect(retired == 1 &&
               slots[1].state == SharedBridgeSlotState::Ready &&
               slots[2].state == SharedBridgeSlotState::Free,
           "validated newest acquisition retires only the skipped Ready image");
    expect(slots[0].state == SharedBridgeSlotState::Producing &&
               slots[3].state == SharedBridgeSlotState::ConsumerPending &&
               slots[4].state == SharedBridgeSlotState::Acquired &&
               slots[5].state == SharedBridgeSlotState::AbandonPending,
           "latest cleanup never frees producer- or consumer-owned slots");
}

void test_skipped_producer_uses_exact_serial_retirement()
{
    std::array<TestSlot, 4> slots = {{
        {11, SharedBridgeSlotState::Producing},
        {14, SharedBridgeSlotState::Ready},
        {12, SharedBridgeSlotState::Free},
        {13, SharedBridgeSlotState::ConsumerPending},
    }};

    expect(wawvr::xr::detail::abandon_exact_serial(slots, 11) ==
               AbandonSerialDisposition::producer_retirement_pending,
           "a skipped older producer defers reuse until its exact fence retires");
    expect(wawvr::xr::detail::abandon_exact_serial(slots, 12) ==
               AbandonSerialDisposition::retired,
           "metadata retirement is idempotent for an already-free older serial");
    expect(slots[0].state == SharedBridgeSlotState::AbandonPending &&
               slots[1].state == SharedBridgeSlotState::Ready &&
               slots[2].state == SharedBridgeSlotState::Free &&
               slots[3].state == SharedBridgeSlotState::ConsumerPending,
           "skipped metadata retirement preserves selected and consumer ownership");
}

void test_no_ready_selection_preserves_owned_slots()
{
    std::array<TestSlot, 4> slots = {{
        {30, SharedBridgeSlotState::Producing},
        {31, SharedBridgeSlotState::AbandonPending},
        {32, SharedBridgeSlotState::Acquired},
        {33, SharedBridgeSlotState::ConsumerPending},
    }};

    const auto selected =
        wawvr::xr::detail::select_latest_ready_serial(slots, 0);
    expect(!selected,
           "nonblocking selection reports no frame when no producer is complete");
    expect(slots[0].state == SharedBridgeSlotState::Producing &&
               slots[1].state == SharedBridgeSlotState::AbandonPending &&
               slots[2].state == SharedBridgeSlotState::Acquired &&
               slots[3].state == SharedBridgeSlotState::ConsumerPending,
           "a no-ready poll leaves every owned slot unchanged");
}

void test_consumer_ownership_is_never_abandoned()
{
    std::array<TestSlot, 2> slots = {{
        {30, SharedBridgeSlotState::Acquired},
        {31, SharedBridgeSlotState::ConsumerPending},
    }};

    expect(wawvr::xr::detail::abandon_exact_serial(slots, 30) ==
               AbandonSerialDisposition::consumer_owned,
           "an acquired serial cannot be abandoned");
    expect(wawvr::xr::detail::abandon_exact_serial(slots, 31) ==
               AbandonSerialDisposition::consumer_owned,
           "a consumer-pending serial cannot be abandoned");
    expect(slots[0].state == SharedBridgeSlotState::Acquired &&
               slots[1].state == SharedBridgeSlotState::ConsumerPending,
           "consumer-owned states remain unchanged");
}

void test_pre_capture_consumer_retirement_unblocks_full_ring()
{
    std::array<TestSlot, 3> slots = {{
        {30, SharedBridgeSlotState::ConsumerPending},
        {31, SharedBridgeSlotState::ConsumerPending},
        {32, SharedBridgeSlotState::ConsumerPending},
    }};
    auto poll_one_complete = [](const TestSlot& slot) noexcept {
        return slot.serial == 31
            ? ConsumerFencePollDisposition::complete
            : ConsumerFencePollDisposition::pending;
    };
    const auto service = wawvr::xr::detail::service_consumer_fences(
        slots, poll_one_complete);
    expect(service.ok && service.recycled == 1,
           "pre-capture service reports the exact recycled slot");
    expect(slots[0].state == SharedBridgeSlotState::ConsumerPending &&
               slots[1].state == SharedBridgeSlotState::Free &&
               slots[2].state == SharedBridgeSlotState::ConsumerPending,
           "a completed consumer fence makes a full ring reusable");

    auto poll_failure = [](const TestSlot& slot) noexcept {
        return slot.serial == 32
            ? ConsumerFencePollDisposition::failed
            : ConsumerFencePollDisposition::pending;
    };
    const auto failed_service =
        wawvr::xr::detail::service_consumer_fences(slots, poll_failure);
    expect(!failed_service.ok && failed_service.recycled == 0,
           "a failed consumer fence fails closed without recycling");
    expect(slots[2].state == SharedBridgeSlotState::ConsumerPending,
           "a failed consumer fence remains non-reusable");
}

void test_idempotent_retirement_and_public_rejection()
{
    std::array<TestSlot, 2> slots = {{
        {40, SharedBridgeSlotState::Free},
        {41, SharedBridgeSlotState::AbandonPending},
    }};
    expect(wawvr::xr::detail::abandon_exact_serial(slots, 40) ==
               AbandonSerialDisposition::retired,
           "an exact already-retired serial is idempotent");
    expect(wawvr::xr::detail::abandon_exact_serial(slots, 41) ==
               AbandonSerialDisposition::producer_retirement_pending,
           "repeating a deferred abandonment is idempotent");
    expect(wawvr::xr::detail::abandon_exact_serial(slots, 0) ==
               AbandonSerialDisposition::not_found,
           "serial zero is never a valid capture");

    wawvr::xr::D3D9ExSharedTextureBridge bridge;
    expect(!bridge.AbandonSerial(0),
           "the public API rejects serial zero");
    expect(!bridge.AbandonSerial(40),
           "the public API rejects a serial before initialization");
    expect(bridge.Initialize({}, {}),
           "the public bridge initializes without allocating GPU resources");
    expect(!bridge.AbandonSerial(40),
           "the public API rejects a serial absent from an initialized bridge");
    bridge.Shutdown();
}

void test_exact_producer_device_generation()
{
    constexpr std::uintptr_t first_device = 0x1000;
    constexpr std::uintptr_t second_device = 0x2000;

    expect(wawvr::xr::detail::classify_producer_device(0, 0) ==
               ProducerDeviceDisposition::invalid,
           "a null producer device is rejected");
    expect(wawvr::xr::detail::classify_producer_device(
               0, first_device) == ProducerDeviceDisposition::unbound,
           "the first producer device can bind a new bridge generation");
    expect(wawvr::xr::detail::classify_producer_device(
               first_device, first_device) ==
               ProducerDeviceDisposition::current,
           "the exact producer device remains current");
    expect(wawvr::xr::detail::classify_producer_device(
               first_device, second_device) ==
               ProducerDeviceDisposition::changed,
           "another device requires explicit bridge invalidation");
}

void test_capture_preflight_rejects_before_d3d()
{
    constexpr std::uintptr_t first_device = 0x1000;
    constexpr std::uintptr_t second_device = 0x2000;

    expect(wawvr::xr::detail::classify_capture_entry(
               false, false, false, 0, first_device) ==
               CaptureEntryDisposition::reject_without_d3d,
           "an uninitialized bridge rejects capture before touching D3D");
    expect(wawvr::xr::detail::classify_capture_entry(
               true, false, false, 0, first_device) ==
               CaptureEntryDisposition::create_generation,
           "an available inactive bridge may inspect its first back buffer");
    expect(wawvr::xr::detail::classify_capture_entry(
               true, true, false, 0, first_device) ==
               CaptureEntryDisposition::reject_without_d3d,
           "a failed resource generation cannot retry before invalidation");
    expect(wawvr::xr::detail::classify_capture_entry(
               true, true, false, 0, first_device) ==
               CaptureEntryDisposition::reject_without_d3d,
           "repeated frames stay gated and cannot create allocation/log storms");
    expect(wawvr::xr::detail::classify_capture_entry(
               true, false, true, first_device, first_device) ==
               CaptureEntryDisposition::use_current_generation,
           "the exact active producer may capture");
    expect(wawvr::xr::detail::classify_capture_entry(
               true, false, true, first_device, second_device) ==
               CaptureEntryDisposition::reject_changed_device_without_d3d,
           "an active generation rejects another device before invoking it");
}

void test_exact_consumer_context_generation()
{
    constexpr std::uintptr_t first_device = 0x1000;
    constexpr std::uintptr_t second_device = 0x2000;

    expect(wawvr::xr::detail::consumer_context_matches(
               0, first_device, first_device),
           "an unbound bridge accepts an exact device/context pair");
    expect(wawvr::xr::detail::consumer_context_matches(
               first_device, first_device, first_device),
           "a bound consumer accepts its own immediate context");
    expect(!wawvr::xr::detail::consumer_context_matches(
               first_device, second_device, second_device),
           "another D3D11 generation cannot replace the bound consumer");
    expect(!wawvr::xr::detail::consumer_context_matches(
               first_device, first_device, second_device),
           "a query cannot be polled through another device's context");
    expect(!wawvr::xr::detail::consumer_context_matches(
               first_device, first_device, 0),
           "a context without a recoverable device is rejected");
}

void test_lost_device_reset_lifecycle()
{
    const LostDeviceResetPhase begun =
        wawvr::xr::detail::begin_lost_device_reset();
    expect(!wawvr::xr::detail::d3d9_access_allowed(begun),
           "lost-device preflight permanently forbids old D3D9 calls");
    expect(!wawvr::xr::detail::can_finalize_successful_lost_device_reset(
               begun),
           "producer resources cannot retire before D3D11 ownership does");

    const LostDeviceResetPhase consumer_retired =
        wawvr::xr::detail::complete_consumer_retirement(begun);
    expect(consumer_retired == LostDeviceResetPhase::consumer_retired,
           "D3D11 retirement advances lost-device recovery");
    expect(!wawvr::xr::detail::d3d9_access_allowed(consumer_retired),
           "old D3D9 queries remain forbidden after D3D11 retirement");
    expect(wawvr::xr::detail::can_finalize_successful_lost_device_reset(
               consumer_retired),
           "only a consumer-retired generation accepts the ResetEx barrier");
    expect(wawvr::xr::detail::complete_consumer_retirement(
               LostDeviceResetPhase::idle) == LostDeviceResetPhase::idle,
           "healthy invalidation cannot masquerade as lost-device recovery");

    wawvr::xr::D3D9ExSharedTextureBridge bridge;
    expect(bridge.Initialize({}, {}),
           "the public lost-device lifecycle bridge initializes");
    expect(!bridge.FinalizeSuccessfulLostDeviceReset(),
           "public finalization requires a successful preflight");
    expect(bridge.PrepareForLostDeviceReset(),
           "a generation without consumer ownership completes preflight");
    expect(!bridge.active(),
           "lost-device preflight keeps capture unavailable");
    expect(bridge.FinalizeSuccessfulLostDeviceReset(),
           "explicit post-ResetEx finalization permits fresh capture");
    bridge.Shutdown();
}

void test_shutdown_quarantine_policy()
{
    expect(!wawvr::xr::detail::should_quarantine_unretired_generation(
               false, false, false),
           "a fully empty generation can be destroyed");
    expect(wawvr::xr::detail::should_quarantine_unretired_generation(
               true, false, false),
           "unretired slots require process-lifetime quarantine");
    expect(wawvr::xr::detail::should_quarantine_unretired_generation(
               false, true, false),
           "a retained producer device requires quarantine");
    expect(wawvr::xr::detail::should_quarantine_unretired_generation(
               false, false, true),
           "a retained consumer device requires quarantine");
}

void test_shared_texture_description_validation()
{
    constexpr std::uint32_t expected_format = 87;
    constexpr std::uint32_t default_usage = 0;
    constexpr std::uint32_t shader_resource = 0x08;
    constexpr std::uint32_t render_target = 0x20;
    constexpr std::uint32_t required_bind =
        shader_resource | render_target;
    constexpr std::uint32_t shared_misc = 0x02;
    constexpr SharedTextureDescriptionFacts valid{
        4992, 2688, 1, 1, expected_format, 1, 0, default_usage, 0,
        required_bind, shared_misc,
    };
    const auto accepts = [](const SharedTextureDescriptionFacts& facts)
    {
        return wawvr::xr::detail::valid_shared_texture_description(
            facts, 4992, 2688, expected_format, default_usage,
            required_bind, shared_misc);
    };

    expect(accepts(valid),
           "the exact native shared render-target description is accepted");
    SharedTextureDescriptionFacts superset = valid;
    superset.bind_flags |= 0x80;
    superset.misc_flags |= 0x100;
    expect(accepts(superset),
           "benign bind and misc flag supersets remain accepted");

    const auto expect_rejected = [&accepts](
        SharedTextureDescriptionFacts facts,
        const std::string_view message)
    {
        expect(!accepts(facts), message);
    };
    SharedTextureDescriptionFacts changed = valid;
    changed.width = 2496;
    expect_rejected(changed, "a wrong shared width is rejected");
    changed = valid;
    changed.height = 2016;
    expect_rejected(changed, "a wrong shared height is rejected");
    changed = valid;
    changed.mip_levels = 2;
    expect_rejected(changed, "a mip chain is rejected");
    changed = valid;
    changed.array_size = 2;
    expect_rejected(changed, "a texture array is rejected");
    changed = valid;
    changed.format += 1;
    expect_rejected(changed, "a wrong shared format is rejected");
    changed = valid;
    changed.sample_count = 2;
    expect_rejected(changed, "a multisampled shared texture is rejected");
    changed = valid;
    changed.sample_quality = 1;
    expect_rejected(changed, "nonzero sample quality is rejected");
    changed = valid;
    changed.usage = 1;
    expect_rejected(changed, "a non-default D3D11 usage is rejected");
    changed = valid;
    changed.cpu_access_flags = 1;
    expect_rejected(changed, "CPU-accessible shared storage is rejected");
    changed = valid;
    changed.bind_flags &= ~shader_resource;
    expect_rejected(changed, "a texture without shader binding is rejected");
    changed = valid;
    changed.bind_flags &= ~render_target;
    expect_rejected(changed, "a texture without render-target binding is rejected");
    changed = valid;
    changed.misc_flags &= ~shared_misc;
    expect_rejected(changed, "a texture without the shared flag is rejected");
}

} // namespace

int main()
{
    test_exact_serial_selection();
    test_producer_fence_retirement();
    test_nonblocking_newest_ready_selection();
    test_ready_selection_retires_only_stale_ready_slots();
    test_validated_latest_selection_retires_only_older_ready_slots();
    test_skipped_producer_uses_exact_serial_retirement();
    test_no_ready_selection_preserves_owned_slots();
    test_consumer_ownership_is_never_abandoned();
    test_pre_capture_consumer_retirement_unblocks_full_ring();
    test_idempotent_retirement_and_public_rejection();
    test_exact_producer_device_generation();
    test_capture_preflight_rejects_before_d3d();
    test_exact_consumer_context_generation();
    test_lost_device_reset_lifecycle();
    test_shutdown_quarantine_policy();
    test_shared_texture_description_validation();

    if (failures != 0)
    {
        std::cerr << failures << " D3D9Ex bridge test(s) failed\n";
        return 1;
    }
    std::cout << "D3D9Ex bridge tests passed\n";
    return 0;
}
