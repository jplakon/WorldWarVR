// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>

namespace wawvr::xr::detail
{

enum class SharedBridgeSlotState : std::uint8_t
{
    Free,
    Producing,
    AbandonPending,
    Ready,
    Acquired,
    ConsumerPending,
};

enum class AbandonSerialDisposition : std::uint8_t
{
    not_found,
    retired,
    producer_retirement_pending,
    consumer_owned,
};

enum class ProducerDeviceDisposition : std::uint8_t
{
    invalid,
    unbound,
    current,
    changed,
};

enum class CaptureEntryDisposition : std::uint8_t
{
    reject_without_d3d,
    create_generation,
    use_current_generation,
    reject_changed_device_without_d3d,
};

enum class ConsumerFencePollDisposition : std::uint8_t
{
    pending,
    complete,
    failed,
};

struct ConsumerFenceServiceResult
{
    bool ok = true;
    std::size_t recycled = 0;
};

struct ReadySerialSelection
{
    static constexpr std::size_t kInvalidIndex =
        static_cast<std::size_t>(-1);

    std::size_t index = kInvalidIndex;
    std::uint64_t serial = 0;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return index != kInvalidIndex;
    }
};

enum class LostDeviceResetPhase : std::uint8_t
{
    idle,
    producer_access_forbidden,
    consumer_retired,
};

struct SharedTextureDescriptionFacts
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mip_levels = 0;
    std::uint32_t array_size = 0;
    std::uint32_t format = 0;
    std::uint32_t sample_count = 0;
    std::uint32_t sample_quality = 0;
    std::uint32_t usage = 0;
    std::uint32_t cpu_access_flags = 0;
    std::uint32_t bind_flags = 0;
    std::uint32_t misc_flags = 0;
};

// A shared-resource generation belongs to one exact D3D9 device. Another
// Ex-capable device is not interchangeable: its command stream and reset
// lifetime are independent even when both objects share the same vtable.
constexpr ProducerDeviceDisposition classify_producer_device(
    const std::uintptr_t bound_device,
    const std::uintptr_t candidate_device) noexcept
{
    if (candidate_device == 0)
    {
        return ProducerDeviceDisposition::invalid;
    }
    if (bound_device == 0)
    {
        return ProducerDeviceDisposition::unbound;
    }
    return bound_device == candidate_device
        ? ProducerDeviceDisposition::current
        : ProducerDeviceDisposition::changed;
}

// This decision must be made before invoking any method on candidate_device.
// An inactive, available bridge is the one intentional exception: it needs to
// inspect the back buffer before it can create its first generation.
constexpr CaptureEntryDisposition classify_capture_entry(
    const bool initialized,
    const bool unavailable_until_invalidate,
    const bool active,
    const std::uintptr_t bound_device,
    const std::uintptr_t candidate_device) noexcept
{
    if (!initialized || unavailable_until_invalidate || candidate_device == 0)
    {
        return CaptureEntryDisposition::reject_without_d3d;
    }
    if (!active)
    {
        return CaptureEntryDisposition::create_generation;
    }
    return bound_device == candidate_device
        ? CaptureEntryDisposition::use_current_generation
        : CaptureEntryDisposition::reject_changed_device_without_d3d;
}

// A query/SRV created by one D3D11 device must never be submitted or polled by
// another device's immediate context, even when both devices use one adapter.
constexpr bool consumer_context_matches(
    const std::uintptr_t bound_consumer_device,
    const std::uintptr_t expected_consumer_device,
    const std::uintptr_t context_device) noexcept
{
    if (expected_consumer_device == 0 || context_device == 0 ||
        expected_consumer_device != context_device)
    {
        return false;
    }
    return bound_consumer_device == 0 ||
        bound_consumer_device == expected_consumer_device;
}

constexpr LostDeviceResetPhase begin_lost_device_reset() noexcept
{
    return LostDeviceResetPhase::producer_access_forbidden;
}

constexpr LostDeviceResetPhase complete_consumer_retirement(
    const LostDeviceResetPhase phase) noexcept
{
    return phase == LostDeviceResetPhase::producer_access_forbidden ||
            phase == LostDeviceResetPhase::consumer_retired
        ? LostDeviceResetPhase::consumer_retired
        : phase;
}

constexpr bool d3d9_access_allowed(
    const LostDeviceResetPhase phase) noexcept
{
    return phase == LostDeviceResetPhase::idle;
}

constexpr bool can_finalize_successful_lost_device_reset(
    const LostDeviceResetPhase phase) noexcept
{
    return phase == LostDeviceResetPhase::consumer_retired;
}

constexpr bool should_quarantine_unretired_generation(
    const bool has_slots,
    const bool has_producer_device,
    const bool has_consumer_device) noexcept
{
    return has_slots || has_producer_device || has_consumer_device;
}

// Numeric constants are supplied by the caller so this ownership/validation
// policy remains pure and exhaustively testable without importing D3D headers.
constexpr bool valid_shared_texture_description(
    const SharedTextureDescriptionFacts& description,
    const std::uint32_t expected_width,
    const std::uint32_t expected_height,
    const std::uint32_t expected_format,
    const std::uint32_t default_usage,
    const std::uint32_t required_bind_flags,
    const std::uint32_t shared_misc_flag) noexcept
{
    return description.width == expected_width &&
        description.height == expected_height &&
        description.mip_levels == 1 &&
        description.array_size == 1 &&
        description.format == expected_format &&
        description.sample_count == 1 &&
        description.sample_quality == 0 &&
        description.usage == default_usage &&
        description.cpu_access_flags == 0 &&
        (description.bind_flags & required_bind_flags) ==
            required_bind_flags &&
        (description.misc_flags & shared_misc_flag) != 0;
}

constexpr bool producer_fence_pending(
    const SharedBridgeSlotState state) noexcept
{
    return state == SharedBridgeSlotState::Producing ||
        state == SharedBridgeSlotState::AbandonPending;
}

constexpr SharedBridgeSlotState producer_fence_completion_state(
    const SharedBridgeSlotState state) noexcept
{
    if (state == SharedBridgeSlotState::Producing)
    {
        return SharedBridgeSlotState::Ready;
    }
    if (state == SharedBridgeSlotState::AbandonPending)
    {
        return SharedBridgeSlotState::Free;
    }
    return state;
}

// Select the newest producer-complete capture newer than the last frame the
// caller consumed. Producer polling happens once before this helper is called;
// a Producing slot is never waited on or substituted for the returned serial.
// Ready captures at or below the consumed watermark are safe to retire because
// D3D11 never acquired them. Newer Ready captures remain available until the
// caller proves that it can open the selected shared resource.
template <typename SlotRange>
constexpr ReadySerialSelection select_latest_ready_serial(
    SlotRange& slots,
    const std::uint64_t last_consumed_serial) noexcept
{
    ReadySerialSelection selection{};
    for (std::size_t index = 0; index < slots.size(); ++index)
    {
        auto& slot = slots[index];
        if (slot.state != SharedBridgeSlotState::Ready)
        {
            continue;
        }
        if (slot.serial <= last_consumed_serial)
        {
            slot.state = SharedBridgeSlotState::Free;
            continue;
        }
        if (!selection || slot.serial > selection.serial)
        {
            selection.index = index;
            selection.serial = slot.serial;
        }
    }
    return selection;
}

// Call only after the newest selected Ready slot has passed consumer-resource
// validation. Older completed captures were never acquired by D3D11 and may be
// recycled immediately. Producer- and consumer-owned states remain untouched;
// an older Producing capture must instead follow exact-serial abandonment and
// wait for its producer fence before becoming Free.
template <typename SlotRange>
constexpr std::size_t retire_ready_serials_before(
    SlotRange& slots,
    const std::uint64_t selected_serial) noexcept
{
    if (selected_serial == 0)
    {
        return 0;
    }

    std::size_t retired = 0;
    for (auto& slot : slots)
    {
        if (slot.state == SharedBridgeSlotState::Ready &&
            slot.serial < selected_serial)
        {
            slot.state = SharedBridgeSlotState::Free;
            ++retired;
        }
    }
    return retired;
}

// SlotRange deliberately needs only a state member. PollFence is invoked only
// for consumer-owned slots and returns whether the exact slot's D3D11 fence is
// still pending, complete, or failed. A failed fence remains non-reusable.
template <typename SlotRange, typename PollFence>
constexpr ConsumerFenceServiceResult service_consumer_fences(
    SlotRange& slots,
    PollFence& poll_fence) noexcept
{
    ConsumerFenceServiceResult result{};
    for (auto& slot : slots)
    {
        if (slot.state != SharedBridgeSlotState::ConsumerPending)
        {
            continue;
        }

        switch (poll_fence(slot))
        {
        case ConsumerFencePollDisposition::pending:
            break;
        case ConsumerFencePollDisposition::complete:
            slot.state = SharedBridgeSlotState::Free;
            ++result.recycled;
            break;
        case ConsumerFencePollDisposition::failed:
            result.ok = false;
            return result;
        }
    }
    return result;
}

// SlotRange deliberately needs only serial and state members. Keeping this
// ownership transition independent of COM lets its exact-serial and
// never-free-consumer rules be exhaustively unit tested.
template <typename SlotRange>
constexpr AbandonSerialDisposition abandon_exact_serial(
    SlotRange& slots,
    const std::uint64_t expected_serial) noexcept
{
    if (expected_serial == 0)
    {
        return AbandonSerialDisposition::not_found;
    }

    for (auto& slot : slots)
    {
        if (slot.serial != expected_serial)
        {
            continue;
        }

        switch (slot.state)
        {
        case SharedBridgeSlotState::Free:
            return AbandonSerialDisposition::retired;
        case SharedBridgeSlotState::Producing:
            slot.state = SharedBridgeSlotState::AbandonPending;
            return AbandonSerialDisposition::producer_retirement_pending;
        case SharedBridgeSlotState::AbandonPending:
            return AbandonSerialDisposition::producer_retirement_pending;
        case SharedBridgeSlotState::Ready:
            slot.state = SharedBridgeSlotState::Free;
            return AbandonSerialDisposition::retired;
        case SharedBridgeSlotState::Acquired:
        case SharedBridgeSlotState::ConsumerPending:
            return AbandonSerialDisposition::consumer_owned;
        }
    }

    return AbandonSerialDisposition::not_found;
}

} // namespace wawvr::xr::detail
