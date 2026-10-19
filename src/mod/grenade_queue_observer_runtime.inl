// SPDX-License-Identifier: GPL-3.0-only
// Observation-only exact-SP writer and dispatcher adapters. Included after the
// existing native event observer; never supplies contextual transaction proof.
void* g_grenade_producer_trampoline{};
void* g_grenade_dispatcher_trampoline{};
void* g_grenade_dispatcher_end_trampoline{};
void* g_grenade_ready_trampoline{};
void* g_grenade_inline_fire_trampoline{};
void* g_grenade_initializer_trampoline{};
std::atomic<bool> g_grenade_queue_observer_enabled{false};
std::atomic<std::uint64_t> g_grenade_queue_receipt{0};
std::uintptr_t g_grenade_queue_predicted_state{};

struct GrenadeQueueProducedSlot final {
    std::uint64_t receipt{}, epoch{};
    std::uint32_t ordinal{}, event{}, parameter{};
};
struct GrenadeQueueLedger final {
    std::uintptr_t player_state{};
    std::uint64_t epoch{1};
    bool chain_valid{};
    GrenadeNativeQueueSnapshot latest{};
    std::array<GrenadeQueueProducedSlot, 4> slots{};
};
std::mutex g_grenade_queue_ledger_mutex;
std::array<GrenadeQueueLedger, 2> g_grenade_queue_ledgers{};

[[nodiscard]] std::uint64_t claim_grenade_queue_receipt() noexcept {
    auto prior = g_grenade_queue_receipt.load(std::memory_order_relaxed);
    while (prior != (std::numeric_limits<std::uint64_t>::max)()) {
        if (g_grenade_queue_receipt.compare_exchange_weak(prior, prior + 1,
                std::memory_order_relaxed)) return prior + 1;
    }
    return 0;
}

struct GrenadeQueueLastError final {
    DWORD value{GetLastError()};
    ~GrenadeQueueLastError() { SetLastError(value); }
};

[[nodiscard]] std::uint32_t grenade_queue_player_role(
    const std::uintptr_t state) noexcept {
    if (state == 0) return 0;
    std::uint32_t server{};
    if (g_local_player_entity != 0 && copy_grenade_observer_memory(&server,
            g_local_player_entity + kLocalPlayerClientPointerOffset, 4) &&
            state == server) return 1;
    return state == g_grenade_queue_predicted_state ? 2U : 0U;
}

[[nodiscard]] GrenadeNativeQueueSnapshot read_grenade_native_queue_at(
    const std::uintptr_t state) noexcept {
    GrenadeNativeQueueSnapshot snapshot{};
    std::array<std::uint32_t, 9> bytes{};
    if (state == 0 || state > (std::numeric_limits<std::uintptr_t>::max)() - 0xD0 ||
        !copy_grenade_observer_memory(bytes.data(), state + 0xD0,
            sizeof(bytes))) return snapshot;
    snapshot.readable = true;
    snapshot.producer = bytes[0];
    std::copy_n(bytes.begin() + 1, 4, snapshot.events.begin());
    std::copy_n(bytes.begin() + 5, 4, snapshot.parameters.begin());
    return snapshot;
}

[[nodiscard]] bool grenade_queue_snapshots_equal(
    const GrenadeNativeQueueSnapshot& left,
    const GrenadeNativeQueueSnapshot& right) noexcept {
    return left.readable && right.readable && left.producer == right.producer &&
        left.events == right.events && left.parameters == right.parameters;
}

struct GrenadeEnqueueFrame final {
    std::uintptr_t state{};
    std::uint64_t receipt{};
    std::uint32_t role{}, event_raw{}, parameter_raw{};
    GrenadeNativeQueueSnapshot before{};
    GrenadeNativeEnclosingEventContext parent{};
};
thread_local std::array<GrenadeEnqueueFrame, 8> g_grenade_enqueue_frames{};
thread_local std::uint32_t g_grenade_enqueue_depth{};
std::atomic<std::uint32_t> g_grenade_enqueue_lineage_logs{0};

[[nodiscard]] GrenadeNativeEnclosingEventContext
capture_grenade_enqueue_parent_context() noexcept;

[[nodiscard]] bool claim_grenade_enqueue_lineage_log() noexcept {
    constexpr std::uint32_t limit = 256;
    auto count = g_grenade_enqueue_lineage_logs.load(std::memory_order_relaxed);
    while (count < limit) {
        if (g_grenade_enqueue_lineage_logs.compare_exchange_weak(
                count, count + 1U, std::memory_order_relaxed)) return true;
    }
    return false;
}

void __cdecl grenade_native_enqueue_begin_impl(void* player_state,
    const std::uint32_t event_raw, const std::uint32_t parameter_raw) noexcept {
    GrenadeQueueLastError preserve_error;
    const auto depth = g_grenade_enqueue_depth;
    if (depth == (std::numeric_limits<std::uint32_t>::max)()) return;
    ++g_grenade_enqueue_depth;
    if (depth >= g_grenade_enqueue_frames.size()) return;
    auto& frame = g_grenade_enqueue_frames[depth];
    frame = {};
    if (!g_grenade_queue_observer_enabled.load(std::memory_order_acquire)) return;
    frame.state = reinterpret_cast<std::uintptr_t>(player_state);
    frame.role = grenade_queue_player_role(frame.state);
    if (frame.role == 0) return;
    frame.receipt = claim_grenade_queue_receipt();
    frame.event_raw = event_raw;
    frame.parameter_raw = parameter_raw;
    frame.before = read_grenade_native_queue_at(frame.state);
    // Copy only existing same-thread observer metadata; do not read native
    // source/command fields or create an input/authorization token here.
    frame.parent = capture_grenade_enqueue_parent_context();
}

void __cdecl grenade_native_enqueue_end_impl() noexcept {
    GrenadeQueueLastError preserve_error;
    if (g_grenade_enqueue_depth == 0) return;
    const auto depth = --g_grenade_enqueue_depth;
    if (depth >= g_grenade_enqueue_frames.size()) return;
    const auto frame = g_grenade_enqueue_frames[depth];
    g_grenade_enqueue_frames[depth] = {};
    if (frame.role == 0 || frame.receipt == 0) return;
    const auto after = read_grenade_native_queue_at(frame.state);
    const auto observed = validate_completed_grenade_native_enqueue(
        frame.before, after, frame.event_raw, frame.parameter_raw);
    const auto lineage = observe_grenade_native_enqueue_lineage(
        frame.parent, frame.state, frame.role, g_local_player_entity, observed);
    bool prior_chain_matched{};
    std::uint64_t epoch{};
    try {
        std::lock_guard<std::mutex> lock(g_grenade_queue_ledger_mutex);
        auto& ledger = g_grenade_queue_ledgers[frame.role - 1];
        prior_chain_matched = ledger.chain_valid &&
            ledger.player_state == frame.state &&
            grenade_queue_snapshots_equal(ledger.latest, frame.before);
        if (!prior_chain_matched) {
            if (ledger.epoch != (std::numeric_limits<std::uint64_t>::max)())
                ++ledger.epoch;
            ledger.slots = {};
        }
        ledger.player_state = frame.state;
        ledger.chain_valid = observed.completedNativeWrite;
        ledger.latest = after;
        epoch = ledger.epoch;
        if (observed.completedNativeWrite) ledger.slots[observed.slot] = {
            frame.receipt, epoch, observed.ordinalRaw,
            observed.eventNormalized, observed.parameterNormalized};
        else ledger.slots = {};
    } catch (...) {
        // Never turn a diagnostic allocation/lock failure into native control.
    }
    if (observed.eventNormalized == 39 || observed.eventNormalized == 40 ||
        !observed.completedNativeWrite) stereo_diagnostic_log(
        "GrenadeNativeProduced receipt=%llu role=%u playerState=0x%08X ordinalRaw=%u producerAfterRaw=%u eventRaw=%u event=%u parameterRaw=%u parameter=%u beforeReadable=%d afterReadable=%d completedNativeWrite=%d priorChainMatched=%d epoch=%llu originalCalls=1 observationOnly=1 transactionOwnershipEstablished=0",
        static_cast<unsigned long long>(frame.receipt), frame.role,
        static_cast<unsigned>(frame.state), observed.ordinalRaw,
        observed.nextProducerRaw, frame.event_raw, observed.eventNormalized,
        frame.parameter_raw, observed.parameterNormalized,
        frame.before.readable ? 1 : 0, after.readable ? 1 : 0,
        observed.completedNativeWrite ? 1 : 0, prior_chain_matched ? 1 : 0,
        static_cast<unsigned long long>(epoch));
    // Separate record keeps the existing writer format intact and stays below
    // the logger's1024-byte limit. A finite lifetime budget cannot wrap/rearm.
    if (lineage.context_captured && observed.eventNormalized == 40U &&
        claim_grenade_enqueue_lineage_log()) {
        stereo_diagnostic_log(
            "GrenadeNativeEnqueueLineage receipt=%llu role=%u playerState=0x%08X ordinalRaw=%u completedNativeWrite=%d parentEventReceipt=%llu parentEventRaw=%u parentOrdinalRaw=%u parentProducedReceipt=%llu parentDispatcherReceipt=%llu activeDispatcherReceipt=%llu parentEntity=0x%08X dispatcherEntity=0x%08X dispatcherPlayerState=0x%08X dispatcherRole=%u activeDispatcher=%d contextCaptured=%d nestedEvent40WriteObserved=%d parentProducerReceiptPresent=%d rejectionMask=%03X observationOnly=1 followupLegitimacyEstablished=0 transactionOwnershipEstablished=0 predictionDrainEstablished=0 queueDrainEstablished=0",
            static_cast<unsigned long long>(frame.receipt), frame.role,
            static_cast<unsigned>(frame.state), observed.ordinalRaw,
            observed.completedNativeWrite ? 1 : 0,
            static_cast<unsigned long long>(lineage.parent.event_receipt),
            lineage.parent.event_raw, lineage.parent.ordinal_raw,
            static_cast<unsigned long long>(lineage.parent.produced_receipt),
            static_cast<unsigned long long>(lineage.parent.dispatcher_receipt),
            static_cast<unsigned long long>(lineage.parent.active_dispatcher_receipt),
            static_cast<unsigned>(lineage.parent.entity),
            static_cast<unsigned>(lineage.parent.dispatcher_entity),
            static_cast<unsigned>(lineage.parent.player_state),
            lineage.parent.dispatcher_role, lineage.parent.active_dispatcher ? 1 : 0,
            lineage.context_captured ? 1 : 0,
            lineage.nested_event40_write_observed ? 1 : 0,
            lineage.parent_producer_receipt_present ? 1 : 0,
            lineage.rejection_mask);
    }
}

struct GrenadeInlineProducerFrame final {
    std::uintptr_t state{};
    std::uint64_t receipt{};
    std::uint32_t role{}, source{};
    bool expected_readable{};
    GrenadeNativeQueueSnapshot before{};
    GrenadeNativeExpectedBatch expected{};
};
thread_local std::array<GrenadeInlineProducerFrame, 8> g_grenade_inline_frames{};
thread_local std::uint32_t g_grenade_inline_depth{};

void begin_grenade_native_inline_observation(const std::uintptr_t state,
    const std::uint32_t source) noexcept {
    const auto depth = g_grenade_inline_depth;
    if (depth == (std::numeric_limits<std::uint32_t>::max)()) return;
    ++g_grenade_inline_depth;
    if (depth >= g_grenade_inline_frames.size()) return;
    auto& frame = g_grenade_inline_frames[depth];
    frame = {};
    if (!g_grenade_queue_observer_enabled.load(std::memory_order_acquire)) return;
    frame.state = state;
    frame.source = source;
    frame.role = grenade_queue_player_role(state);
    if (frame.role == 0) return;
    frame.receipt = claim_grenade_queue_receipt();
    frame.before = read_grenade_native_queue_at(state);
    GrenadeObservedPlayer player{};
    player.state.address = state;
    if (!copy_grenade_observer_memory(player.bytes.data(), state,
            player.bytes.size())) return;
    player.readable = true;
    std::memcpy(&player.state.offhand,
        player.bytes.data() + kPlayerStateOffhandIndexOffset, 4);
    std::memcpy(&player.state.owner,
        player.bytes.data() + kContextPlayerStateOwnerOffset, 4);
    std::int32_t native_mode{};
    if (state > (std::numeric_limits<std::uintptr_t>::max)() - 0x944 ||
        !copy_grenade_observer_memory(&native_mode, state + 0x944, 4)) return;
    const auto parameter = player.state.offhand & 0xFFFFU;
    if (source == 16) {
        // Complete 0x421210 initializer: no native call or other queue writer.
        if (native_mode < 3) {
            frame.expected.count = 1;
            frame.expected.writes[0] = {16, 0};
        }
    } else if (source == 38) {
        // Complete 0x421300 function: no native call or other queue writer.
        if (native_mode < 3 && player.state.owner == 0x3FF) {
            frame.expected.count = 1;
            frame.expected.writes[0] = {38, parameter};
        }
    } else {
        // Complete 0x4214E0 function emits39/85 before its own-ammo branch.
        frame.expected.count = 1;
        frame.expected.writes[0] = {native_mode >= 3 ? 85U : 39U, parameter};
        if (native_mode < 3 && player.state.owner == 0x3FF) {
            const auto inventory = read_grenade_observer_inventory(
                player, player.state.offhand);
            if (!inventory.readable) return;
            // Native ADD is32-bit; express modulo arithmetic without signed UB.
            const auto total = static_cast<std::uint32_t>(inventory.reserve) +
                static_cast<std::uint32_t>(inventory.clip);
            if (total == 0) {
                frame.expected.count = 2;
                frame.expected.writes[1] = {15, 0};
            }
        }
    }
    frame.expected_readable = true;
}

void __cdecl grenade_native_inline_ready_begin_impl(void* player_state) noexcept {
    GrenadeQueueLastError preserve_error;
    begin_grenade_native_inline_observation(
        reinterpret_cast<std::uintptr_t>(player_state), 38);
}

void __cdecl grenade_native_inline_init_begin_impl(void* player_state) noexcept {
    GrenadeQueueLastError preserve_error;
    begin_grenade_native_inline_observation(
        reinterpret_cast<std::uintptr_t>(player_state), 16);
}

void __cdecl grenade_native_inline_fire_begin_impl(void* pmove) noexcept {
    GrenadeQueueLastError preserve_error;
    std::uint32_t state{};
    // Invalid/unknown pmove still pushes a shadow frame for balanced teardown.
    if (!copy_grenade_observer_memory(&state,
            reinterpret_cast<std::uintptr_t>(pmove), 4)) state = 0;
    begin_grenade_native_inline_observation(state, 39);
}

void __cdecl grenade_native_inline_end_impl() noexcept {
    GrenadeQueueLastError preserve_error;
    if (g_grenade_inline_depth == 0) return;
    const auto depth = --g_grenade_inline_depth;
    if (depth >= g_grenade_inline_frames.size()) return;
    const auto frame = g_grenade_inline_frames[depth];
    g_grenade_inline_frames[depth] = {};
    if (frame.role == 0 || frame.receipt == 0) return;
    const auto after = read_grenade_native_queue_at(frame.state);
    auto observed = validate_completed_grenade_native_batch(
        frame.before, after, frame.expected);
    observed.completedNativeWrite = observed.completedNativeWrite &&
        frame.expected_readable;
    bool prior_chain_matched{};
    std::uint64_t epoch{};
    std::array<std::uint64_t, 2> receipts{frame.receipt, 0};
    if (frame.expected.count == 2) receipts[1] = claim_grenade_queue_receipt();
    try {
        std::lock_guard<std::mutex> lock(g_grenade_queue_ledger_mutex);
        auto& ledger = g_grenade_queue_ledgers[frame.role - 1];
        prior_chain_matched = ledger.chain_valid &&
            ledger.player_state == frame.state &&
            grenade_queue_snapshots_equal(ledger.latest, frame.before);
        if (!prior_chain_matched) {
            if (ledger.epoch != (std::numeric_limits<std::uint64_t>::max)())
                ++ledger.epoch;
            ledger.slots = {};
        }
        ledger.player_state = frame.state;
        ledger.chain_valid = observed.completedNativeWrite;
        ledger.latest = after;
        epoch = ledger.epoch;
        if (observed.completedNativeWrite) {
            for (std::uint32_t i = 0; i < frame.expected.count; ++i) {
                const auto ordinal = (frame.before.producer + i) & 0xFFU;
                ledger.slots[ordinal & 3U] = {receipts[i], epoch, ordinal,
                    observed.eventNormalized[i], observed.parameterNormalized[i]};
            }
        } else ledger.slots = {};
    } catch (...) {}
    if (frame.expected.count == 0 && observed.completedNativeWrite) return;
    for (std::uint32_t i = 0; i < (std::min)(frame.expected.count, 2U); ++i) {
        stereo_diagnostic_log(
            "GrenadeNativeProduced receipt=%llu role=%u playerState=0x%08X ordinalRaw=%u producerAfterRaw=%u eventRaw=%u event=%u parameterRaw=%u parameter=%u beforeReadable=%d afterReadable=%d completedNativeWrite=%d priorChainMatched=%d epoch=%llu originalCalls=1 producerSource=inline-%u nativeCallReceipt=%llu batchIndex=%u batchCount=%u expectedBranchReadable=%d observationOnly=1 transactionOwnershipEstablished=0",
            static_cast<unsigned long long>(receipts[i]), frame.role,
            static_cast<unsigned>(frame.state),
            (frame.before.producer + i) & 0xFFU, after.producer,
            frame.expected.writes[i].eventRaw, observed.eventNormalized[i],
            frame.expected.writes[i].parameterRaw, observed.parameterNormalized[i],
            frame.before.readable ? 1 : 0, after.readable ? 1 : 0,
            observed.completedNativeWrite ? 1 : 0, prior_chain_matched ? 1 : 0,
            static_cast<unsigned long long>(epoch), frame.source,
            static_cast<unsigned long long>(frame.receipt), i,
            frame.expected.count, frame.expected_readable ? 1 : 0);
    }
    if (!observed.completedNativeWrite && frame.expected.count == 0)
        stereo_diagnostic_log(
            "GrenadeNativeInlineRejected receipt=%llu role=%u playerState=0x%08X source=%u expectedBranchReadable=%d expectedCount=0 producerBeforeRaw=%u producerAfterRaw=%u observationOnly=1",
            static_cast<unsigned long long>(frame.receipt), frame.role,
            static_cast<unsigned>(frame.state), frame.source,
            frame.expected_readable ? 1 : 0, frame.before.producer, after.producer);
}

struct GrenadeDispatchFrame final {
    std::uintptr_t entity{}, state{};
    std::uint64_t receipt{};
    std::uint32_t role{}, start_raw{};
    GrenadeNativeQueueSnapshot before{};
};
thread_local std::array<GrenadeDispatchFrame, 8> g_grenade_dispatch_frames{};
thread_local std::uint32_t g_grenade_dispatch_depth{};

void __cdecl grenade_native_dispatch_begin_impl(void* entity,
    const std::uint32_t start_raw) noexcept {
    GrenadeQueueLastError preserve_error;
    const auto depth = g_grenade_dispatch_depth;
    if (depth == (std::numeric_limits<std::uint32_t>::max)()) return;
    ++g_grenade_dispatch_depth;
    if (depth >= g_grenade_dispatch_frames.size()) return;
    auto& frame = g_grenade_dispatch_frames[depth];
    frame = {};
    if (!g_grenade_queue_observer_enabled.load(std::memory_order_acquire) ||
        reinterpret_cast<std::uintptr_t>(entity) != g_local_player_entity) return;
    std::uint32_t state{};
    if (!copy_grenade_observer_memory(&state, g_local_player_entity +
            kLocalPlayerClientPointerOffset, 4)) return;
    frame.entity = reinterpret_cast<std::uintptr_t>(entity);
    frame.state = state;
    frame.role = grenade_queue_player_role(state);
    frame.receipt = claim_grenade_queue_receipt();
    frame.start_raw = start_raw;
    frame.before = read_grenade_native_queue_at(state);
}

void __cdecl grenade_native_dispatch_end_impl(
    const std::uint32_t completed_ordinal_raw) noexcept {
    GrenadeQueueLastError preserve_error;
    if (g_grenade_dispatch_depth == 0) return;
    const auto depth = --g_grenade_dispatch_depth;
    if (depth >= g_grenade_dispatch_frames.size()) return;
    const auto frame = g_grenade_dispatch_frames[depth];
    g_grenade_dispatch_frames[depth] = {};
    if (frame.role == 0 || frame.receipt == 0) return;
    const auto after = read_grenade_native_queue_at(frame.state);
    const auto observed = validate_completed_grenade_native_dispatch(
        frame.before, after, frame.start_raw, completed_ordinal_raw);
    // Empty intervals are numerous and do not establish a drained transaction.
    if (observed.observedDrainInterval && observed.observedEventCount == 0) return;
    stereo_diagnostic_log(
        "GrenadeNativeDispatch receipt=%llu role=%u entity=0x%08X playerState=0x%08X startOrdinalRaw=%u completedLocalOrdinalRaw=%u producerBeforeRaw=%u producerAfterRaw=%u beforeReadable=%d afterReadable=%d observedDrainInterval=%d observedEventCount=%u nativeReturned=1 completionSource=epilogue-local-ESP-plus-0x10 observationOnly=1 transactionOwnershipEstablished=0 predictionDrainEstablished=0 queueDrainEstablished=0",
        static_cast<unsigned long long>(frame.receipt), frame.role,
        static_cast<unsigned>(frame.entity), static_cast<unsigned>(frame.state),
        frame.start_raw, completed_ordinal_raw, frame.before.producer,
        after.producer, frame.before.readable ? 1 : 0, after.readable ? 1 : 0,
        observed.observedDrainInterval ? 1 : 0, observed.observedEventCount);
}

[[nodiscard]] std::uint64_t observe_grenade_completed_producer_link(
    const GrenadeObservedPlayer& player, const std::uint32_t ordinal,
    const std::uint32_t event) noexcept {
    if (!g_grenade_queue_observer_enabled.load(std::memory_order_acquire) ||
        !player.readable) return 0;
    const auto role = grenade_queue_player_role(player.state.address);
    if (role == 0) return 0;
    const auto observed = observe_grenade_native_queue(player.queue,
        ordinal, event);
    if (!observed.boundedSlotMatches) return 0;
    try {
        std::lock_guard<std::mutex> lock(g_grenade_queue_ledger_mutex);
        const auto& ledger = g_grenade_queue_ledgers[role - 1];
        const auto& slot = ledger.slots[ordinal & 3U];
        if (!ledger.chain_valid || ledger.player_state != player.state.address ||
            !grenade_queue_snapshots_equal(ledger.latest, player.queue) ||
            slot.receipt == 0 || slot.epoch != ledger.epoch ||
            slot.ordinal != ordinal || slot.event != event ||
            slot.parameter != observed.parameterAtSlot) return 0;
        return slot.receipt;
    } catch (...) { return 0; }
}

[[nodiscard]] std::uint64_t observe_grenade_enclosing_dispatch_link(
    const std::uintptr_t entity, const std::uintptr_t state) noexcept {
    if (!g_grenade_queue_observer_enabled.load(std::memory_order_acquire) ||
        g_grenade_dispatch_depth == 0 ||
        g_grenade_dispatch_depth > g_grenade_dispatch_frames.size()) return 0;
    const auto& frame = g_grenade_dispatch_frames[g_grenade_dispatch_depth - 1];
    return frame.entity == entity && frame.state == state && frame.role != 0
        ? frame.receipt : 0;
}

[[nodiscard]] GrenadeNativeEnclosingEventContext
capture_grenade_enqueue_parent_context() noexcept {
    const auto parent = g_grenade_observer_thread;
    GrenadeNativeEnclosingEventContext result{};
    result.event_receipt = parent.receipt;
    result.produced_receipt = parent.produced_receipt;
    result.dispatcher_receipt = parent.dispatcher_receipt;
    result.entity = parent.entity;
    result.event_raw = parent.event;
    result.ordinal_raw = parent.ordinal;
    if (g_grenade_dispatch_depth != 0 &&
        g_grenade_dispatch_depth <= g_grenade_dispatch_frames.size()) {
        const auto& dispatch = g_grenade_dispatch_frames[g_grenade_dispatch_depth - 1U];
        result.active_dispatcher = true;
        result.active_dispatcher_receipt = dispatch.receipt;
        result.dispatcher_entity = dispatch.entity;
        result.player_state = dispatch.state;
        result.dispatcher_role = dispatch.role;
    }
    return result;
}

#include "grenade_queue_observer_bridge.inl"

void install_grenade_queue_native_observer(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    if (!g_grenade_native_observer_enabled.load(std::memory_order_acquire)) return;
    // This second opt-in permits separate validation of the new queue seams
    // without changing an ordinary event-only audit session.
    std::array<wchar_t, 2> enabled{};
    if (GetEnvironmentVariableW(L"WAWVR_THROWBACK_QUEUE_AUDIT", enabled.data(),
            static_cast<DWORD>(enabled.size())) != 1 || enabled[0] != L'1') return;
    constexpr std::array<wawvr::t4::Rva, 6> rvas{
        0x00010310, 0x000E7A20, 0x000E7F7D, 0x00021300, 0x000214E0, 0x00021210};
    constexpr std::array<std::size_t, 6> sizes{6, 6, 7, 7, 9, 7};
    constexpr std::array<std::array<std::uint8_t, 9>, 6> originals{{
        {{0x8B,0x90,0xD0,0,0,0,0,0,0}},
        {{0x55,0x8B,0xEC,0x83,0xE4,0xF8,0,0,0}},
        {{0x5F,0x5E,0x5B,0x8B,0xE5,0x5D,0xC3,0,0}},
        {{0x83,0xB8,0x44,0x09,0,0,0x03,0,0}},
        {{0x57,0x8B,0x38,0x8B,0x8F,0xD0,0,0,0}},
        {{0x83,0xB8,0x44,0x09,0,0,0x03,0,0}}
    }};
    const auto producer_full = bindings.module().address(rvas[0], 62);
    const auto predicted = bindings.data_address(
        wawvr::t4::DataSymbolId::predicted_player_state, sizeof(std::uint32_t));
    std::array<std::uintptr_t, 6> addresses{};
    std::array<DWORD, 6> protections{};
    const auto read_protection = [](const std::uintptr_t address,
        const std::size_t size, DWORD* protection) noexcept {
        MEMORY_BASIC_INFORMATION info{};
        if (!protection || VirtualQuery(reinterpret_cast<void*>(address), &info,
                sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
            info.RegionSize < size || address - reinterpret_cast<std::uintptr_t>(
                info.BaseAddress) > info.RegionSize - size) return false;
        *protection = info.Protect;
        return true;
    };
    const auto originals_match = [&]() noexcept {
        for (std::size_t i = 0; i < addresses.size(); ++i) {
            std::array<std::uint8_t, 9> current{};
            if (!copy_grenade_observer_memory(current.data(), addresses[i], sizes[i]) ||
                !std::equal(current.begin(), current.begin() + sizes[i],
                    originals[i].begin())) return false;
        }
        std::array<std::uint8_t, 62> producer{};
        if (!producer_full || !copy_grenade_observer_memory(producer.data(),
                *producer_full, producer.size())) return false;
        std::uint64_t hash = 14695981039346656037ULL;
        for (const auto byte : producer) hash = (hash ^ byte) * 1099511628211ULL;
        if (hash != 0xF9694F5A25D85E2CULL) return false;
        struct InlineFingerprint { wawvr::t4::Rva rva; std::size_t size; std::uint64_t fnv; };
        constexpr std::array<InlineFingerprint, 3> inline_fingerprints{{
            {0x00021300, 203, 0x998F603A53998848ULL},
            {0x000214E0, 328, 0x6938992BCAD0A97DULL},
            {0x00021210, 235, 0x88682A71FBDD3AB3ULL}}};
        for (const auto& fingerprint : inline_fingerprints) {
            std::array<std::uint8_t, 328> bytes{};
            const auto address = bindings.module().address(fingerprint.rva,
                fingerprint.size);
            if (!address || !copy_grenade_observer_memory(bytes.data(),
                    *address, fingerprint.size)) return false;
            hash = 14695981039346656037ULL;
            for (std::size_t i = 0; i < fingerprint.size; ++i)
                hash = (hash ^ bytes[i]) * 1099511628211ULL;
            if (hash != fingerprint.fnv) return false;
        }
        return true;
    };
    for (std::size_t i = 0; i < addresses.size(); ++i) {
        const auto address = bindings.module().address(rvas[i], sizes[i]);
        if (!address || !read_protection(*address, sizes[i], &protections[i])) return;
        addresses[i] = *address;
    }
    if (!predicted || !originals_match()) {
        stereo_diagnostic_log("GrenadeNativeQueueAudit unavailable: exact producer/prologue/epilogue rejected; event observer remains pass-through");
        return;
    }
    std::array<void*, 6> trampolines{};
    std::array<std::array<std::uint8_t, 9>, 6> patches{};
    const std::array<std::uintptr_t, 6> bridges{
        reinterpret_cast<std::uintptr_t>(&grenade_native_enqueue_bridge),
        reinterpret_cast<std::uintptr_t>(&grenade_native_dispatcher_bridge),
        reinterpret_cast<std::uintptr_t>(&grenade_native_dispatcher_end_bridge),
        reinterpret_cast<std::uintptr_t>(&grenade_native_ready_bridge),
        reinterpret_cast<std::uintptr_t>(&grenade_native_inline_fire_bridge),
        reinterpret_cast<std::uintptr_t>(&grenade_native_init_bridge)};
    bool prepared = true;
    for (std::size_t i = 0; i < addresses.size() && prepared; ++i) {
        auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(nullptr,
            sizes[i] + 5, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        trampolines[i] = trampoline;
        if (!trampoline) { prepared = false; break; }
        std::copy_n(originals[i].begin(), sizes[i], trampoline);
        std::array<std::uint8_t, 5> resume{}, branch{};
        prepared = build_rel32(reinterpret_cast<std::uintptr_t>(trampoline) +
            sizes[i], addresses[i] + sizes[i], &resume) &&
            build_rel32(addresses[i], bridges[i], &branch);
        if (!prepared) break;
        resume[0] = branch[0] = 0xE9;
        std::copy(resume.begin(), resume.end(), trampoline + sizes[i]);
        patches[i].fill(0x90);
        std::copy(branch.begin(), branch.end(), patches[i].begin());
        DWORD ignored{};
        prepared = VirtualProtect(trampoline, sizes[i] + 5, PAGE_EXECUTE_READ,
            &ignored) && FlushInstructionCache(GetCurrentProcess(), trampoline,
                sizes[i] + 5);
    }
    if (!prepared) {
        for (auto* trampoline : trampolines)
            if (trampoline) VirtualFree(trampoline, 0, MEM_RELEASE);
        return;
    }
    const std::array<PeerThreadPatchRange, 6> ranges{{
        {addresses[0], sizes[0]}, {addresses[1], sizes[1]},
        {addresses[2], sizes[2]}, {addresses[3], sizes[3]},
        {addresses[4], sizes[4]}, {addresses[5], sizes[5]}}};
    SuspendedPeerThreads peers;
    PeerThreadQuiesceResult quiesce{};
    bool stopped_match = peers.suspend(ranges, &quiesce) && originals_match();
    for (std::size_t i = 0; i < addresses.size() && stopped_match; ++i) {
        DWORD actual{};
        stopped_match = read_protection(addresses[i], sizes[i], &actual) &&
            actual == protections[i];
    }
    if (!stopped_match) {
        peers.resume();
        for (auto* trampoline : trampolines) VirtualFree(trampoline, 0, MEM_RELEASE);
        stereo_diagnostic_log("GrenadeNativeQueueAudit unavailable: quiescence/protection rejected error=%lu", quiesce.system_error);
        return;
    }
    // These immutable dependencies outlive any exposed branch, even if a later
    // rollback cannot be established. No logger or ledger lock while suspended.
    g_grenade_producer_trampoline = trampolines[0];
    g_grenade_dispatcher_trampoline = trampolines[1];
    g_grenade_dispatcher_end_trampoline = trampolines[2];
    g_grenade_ready_trampoline = trampolines[3];
    g_grenade_inline_fire_trampoline = trampolines[4];
    g_grenade_initializer_trampoline = trampolines[5];
    g_grenade_queue_predicted_state = *predicted;
    struct Operations final {
        std::array<std::uintptr_t, 6> addresses;
        std::array<std::size_t, 6> sizes;
        std::array<DWORD, 6> protections;
        std::array<std::array<std::uint8_t, 9>, 6> originals, patches;
        DWORD error{};
        bool write(const std::size_t index,
            const std::array<std::uint8_t, 9>& bytes) noexcept {
            DWORD ignored{};
            void* target = reinterpret_cast<void*>(addresses[index]);
            if (!VirtualProtect(target, sizes[index], PAGE_EXECUTE_READWRITE,
                    &ignored)) { error = GetLastError(); return false; }
            std::memcpy(target, bytes.data(), sizes[index]);
            const bool flushed = FlushInstructionCache(GetCurrentProcess(),
                target, sizes[index]) != FALSE;
            const bool restored = VirtualProtect(target, sizes[index],
                protections[index], &ignored) != FALSE;
            std::array<std::uint8_t, 9> actual{};
            const bool equal = copy_grenade_observer_memory(actual.data(),
                addresses[index], sizes[index]) && std::equal(actual.begin(),
                    actual.begin() + sizes[index], bytes.begin());
            if ((!flushed || !restored || !equal) && error == 0) error = GetLastError();
            return flushed && restored && equal;
        }
        bool patch(const std::size_t index) noexcept { return write(index, patches[index]); }
        bool restore(const std::size_t index) noexcept { return write(index, originals[index]); }
        bool protections_match() const noexcept {
            bool equal = true;
            for (std::size_t i = 0; i < addresses.size(); ++i) {
                MEMORY_BASIC_INFORMATION info{};
                const bool current = VirtualQuery(reinterpret_cast<void*>(addresses[i]),
                    &info, sizeof(info)) == sizeof(info) && info.State == MEM_COMMIT &&
                    info.Protect == protections[i];
                equal = current && equal;
            }
            return equal;
        }
        bool restore_protections() noexcept {
            bool restored = true;
            for (std::size_t i = 0; i < addresses.size(); ++i) {
                DWORD ignored{};
                const bool current = VirtualProtect(reinterpret_cast<void*>(
                    addresses[i]), sizes[i], protections[i], &ignored) != FALSE;
                restored = current && restored;
            }
            return protections_match() && restored;
        }
        bool originals_match() const noexcept {
            bool equal = true;
            for (std::size_t i = 0; i < addresses.size(); ++i) {
                std::array<std::uint8_t, 9> actual{};
                const bool current = copy_grenade_observer_memory(actual.data(),
                    addresses[i], sizes[i]) && std::equal(actual.begin(),
                        actual.begin() + sizes[i], originals[i].begin());
                equal = current && equal;
            }
            return equal;
        }
    } operations{addresses, sizes, protections, originals, patches};
    const auto outcome = install_grenade_observer_spans<6>(operations);
    if (outcome.installed)
        g_grenade_queue_observer_enabled.store(true, std::memory_order_release);
    peers.resume();
    stereo_diagnostic_log(
        "GrenadeNativeQueueAudit installed=%d rollbackVerified=%d error=%lu schema=4 producer=exact-SP-410310 inlineInit=exact-SP-421210 inlineReady=exact-SP-421300 inlineFire=exact-SP-4214E0 dispatcher=entry-and-epilogue completedWriteValidation=1 allocationFlagOffset=0x11D incarnationOffset=0x90 linkByteOffset=0x118 observationOnly=1 contextualInputEnabled=0",
        outcome.installed ? 1 : 0, outcome.rollbackVerified ? 1 : 0, operations.error);
}
