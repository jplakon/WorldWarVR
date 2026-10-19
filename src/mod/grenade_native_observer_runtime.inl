// SPDX-License-Identifier: GPL-3.0-only
// Included only by the exact-SP manual grenade runtime after its bounded
// native readers. All observations are opt-in, pass-through and non-authorizing.
constexpr wchar_t kGrenadeNativeAuditEnvironment[] =
    L"WAWVR_THROWBACK_NATIVE_AUDIT";
constexpr wawvr::t4::Rva kGrenadeNativeLevelTimeRva = 0x014F6DC8;
constexpr std::size_t kGrenadeNativeExpiryOffset = 0x1C4;
constexpr std::size_t kGrenadeNativeMissileSpan = 0x1C8;
std::atomic<bool> g_grenade_native_observer_enabled{false};
std::atomic<bool> g_grenade_native_observer_installed{false};
std::atomic<std::uint64_t> g_grenade_native_observer_receipt{0};
std::uintptr_t g_grenade_native_observer_level_time = 0;

struct GrenadeObservedMissile final {
    bool readable{};
    std::uintptr_t address{};
    std::uint32_t index{}, type{}, weapon{}, weapon_raw{}, incarnation{};
    std::uint16_t owner_handle{};
    std::uint8_t in_use{}, linked_raw{}, handler{};
    std::int32_t scheduled_expiry{};
};

struct GrenadeObservedPlayer final {
    bool readable{};
    ContextNativePlayerState state{};
    GrenadeNativeQueueSnapshot queue{};
    std::uint64_t inventory_hash{};
    std::array<std::uint8_t, kPlayerStateMinimumSpan> bytes{};
};

struct GrenadeObservedInventoryPool final {
    bool readable{}, owned{};
    std::uint32_t weapon{};
    std::int32_t ammo_index{}, clip_index{}, reserve{}, clip{};
};

[[nodiscard]] GrenadeObservedInventoryPool read_grenade_observer_inventory(
    const GrenadeObservedPlayer& player, const std::uint32_t weapon) noexcept {
    GrenadeObservedInventoryPool pool{};
    std::uint32_t definition{};
    if (!player.readable || weapon == 0 || weapon > 127 ||
        g_weapon_definition_pointer_table == 0 ||
        g_weapon_definition_pointer_table >
            (std::numeric_limits<std::uintptr_t>::max)() - weapon * 4U ||
        !copy_grenade_observer_memory(&definition,
            g_weapon_definition_pointer_table + weapon * 4U, 4)) return pool;
    std::array<std::uint8_t, 0x400> bytes{};
    if (!copy_grenade_observer_memory(bytes.data(), definition, bytes.size()))
        return pool;
    std::int32_t offhand_class{};
    std::memcpy(&offhand_class, bytes.data() + kWeaponDefinitionOffhandClassOffset, 4);
    if (offhand_class != 1) return pool;
    std::memcpy(&pool.ammo_index, bytes.data() + kWeaponDefinitionAmmoIndexOffset, 4);
    std::memcpy(&pool.clip_index, bytes.data() + kWeaponDefinitionClipIndexOffset, 4);
    if (pool.ammo_index < 0 || pool.ammo_index >= 128 ||
        pool.clip_index < 0 || pool.clip_index >= 128) return pool;
    const auto* const state = player.bytes.data();
    std::memcpy(&pool.reserve, state + kPlayerStateAmmoOffset +
        static_cast<std::size_t>(pool.ammo_index) * 4, 4);
    std::memcpy(&pool.clip, state + kPlayerStateClipAmmoOffset +
        static_cast<std::size_t>(pool.clip_index) * 4, 4);
    std::uint32_t ownership{};
    std::memcpy(&ownership, state + kPlayerStateWeaponsOffset +
        static_cast<std::size_t>(weapon / 32) * 4, 4);
    pool.owned = (ownership & (1U << (weapon % 32))) != 0;
    pool.weapon = weapon;
    pool.readable = true;
    return pool;
}

struct GrenadeObserverThreadFrame final {
    std::uint64_t receipt{};
    std::uint32_t event{}, ordinal{};
    std::uintptr_t entity{};
    std::uint64_t produced_receipt{};
    std::uint64_t dispatcher_receipt{};
};
struct GrenadeObserverPickupCandidate final {
    std::uint64_t receipt{};
    std::uintptr_t player_state{};
    std::uint32_t weapon{};
    std::int32_t owner{0x3FF};
    GrenadeObservedMissile source{};
    bool native_time_readable{};
    std::int32_t native_time{};
};
thread_local GrenadeObserverThreadFrame g_grenade_observer_thread{};
thread_local GrenadeObserverPickupCandidate g_grenade_observer_pickup{};

[[nodiscard]] std::uint64_t claim_grenade_observer_receipt() noexcept {
    auto previous = g_grenade_native_observer_receipt.load(std::memory_order_relaxed);
    while (previous < 256) {
        if (g_grenade_native_observer_receipt.compare_exchange_weak(previous,
                previous + 1, std::memory_order_relaxed)) return previous + 1;
    }
    return 0; // Saturating cap: no uint64 wrap or reused diagnostic receipt.
}

[[nodiscard]] bool read_grenade_observer_time(std::int32_t* value) noexcept {
    return value != nullptr && copy_grenade_observer_memory(value,
        g_grenade_native_observer_level_time, sizeof(*value));
}

[[nodiscard]] GrenadeObservedPlayer read_grenade_observer_player() noexcept {
    GrenadeObservedPlayer result{};
    std::uint32_t player_state{};
    if (g_local_player_entity == 0 || g_local_player_entity >
            (std::numeric_limits<std::uintptr_t>::max)() -
                kLocalPlayerClientPointerOffset ||
        !copy_grenade_observer_memory(&player_state,
            g_local_player_entity + kLocalPlayerClientPointerOffset, 4) ||
        !copy_grenade_observer_memory(result.bytes.data(), player_state,
            result.bytes.size())) return result;
    result.readable = true;
    const auto* const state = result.bytes.data();
    result.state.address = player_state;
    std::memcpy(&result.state.offhand, state + kPlayerStateOffhandIndexOffset, 4);
    std::memcpy(&result.state.owner, state + kContextPlayerStateOwnerOffset, 4);
    std::memcpy(&result.state.fuse, state + kContextPlayerStateFuseOffset, 4);
    std::memcpy(&result.state.window, state + kPlayerStateThrowbackTimeOffset, 4);
    std::memcpy(&result.state.weapon_state, state + kContextPlayerStateWeaponStateOffset, 4);
    std::memcpy(&result.state.hint_entity, state + kContextPlayerStateHintEntityOffset, 4);
    result.queue.readable = true;
    std::memcpy(&result.queue.producer, state + 0xD0, 4);
    std::memcpy(result.queue.events.data(), state + 0xD4, 16);
    std::memcpy(result.queue.parameters.data(), state + 0xE4, 16);
    // Hash all native reserve/clip pools and ownership words, not only the
    // selected weapon. This records changes; it never restores/grants ammo.
    result.inventory_hash = 14695981039346656037ULL;
    const auto hash_span = [&](const std::size_t offset,
                               const std::size_t bytes) noexcept {
        for (std::size_t i = 0; i < bytes; ++i)
            result.inventory_hash = (result.inventory_hash ^ state[offset + i]) *
                1099511628211ULL;
    };
    hash_span(kPlayerStateAmmoOffset, 128 * 4);
    hash_span(kPlayerStateClipAmmoOffset, 128 * 4);
    hash_span(kPlayerStateWeaponsOffset, 4 * 4);
    return result;
}

[[nodiscard]] GrenadeObservedMissile read_grenade_observer_missile(
    const std::uintptr_t address) noexcept {
    GrenadeObservedMissile result{};
    if (address < g_local_player_entity || g_local_player_entity == 0 ||
        (address - g_local_player_entity) % kContextEntitySize != 0 ||
        (address - g_local_player_entity) / kContextEntitySize >= 0x3FF)
        return result;
    std::array<std::uint8_t, kGrenadeNativeMissileSpan> bytes{};
    if (!copy_grenade_observer_memory(bytes.data(), address, bytes.size()))
        return result;
    result.address = address;
    result.index = static_cast<std::uint32_t>(
        (address - g_local_player_entity) / kContextEntitySize);
    const auto* const entity = bytes.data();
    std::memcpy(&result.type, entity + kContextEntityTypeOffset, 4);
    std::memcpy(&result.weapon_raw, entity + kContextEntityWeaponOffset, 4);
    std::memcpy(&result.incarnation, entity + 0x90, 4);
    std::memcpy(&result.owner_handle, entity + kContextEntityOwnerHandleOffset, 2);
    std::memcpy(&result.scheduled_expiry, entity + kGrenadeNativeExpiryOffset, 4);
    result.weapon = result.weapon_raw & 0x7FU;
    result.in_use = entity[kContextEntityInUseOffset];
    result.linked_raw = entity[0x118];
    result.handler = entity[kContextEntityHandlerOffset];
    result.readable = true;
    return result;
}

[[nodiscard]] GrenadeObservedMissile read_grenade_observer_source(
    const GrenadeObservedPlayer& player) noexcept {
    if (!player.readable || player.state.hint_entity <= 0 ||
        player.state.hint_entity >= 0x3FF ||
        g_local_player_entity > (std::numeric_limits<std::uintptr_t>::max)() -
            static_cast<std::uintptr_t>(player.state.hint_entity) *
                kContextEntitySize) return {};
    return read_grenade_observer_missile(g_local_player_entity +
        static_cast<std::uintptr_t>(player.state.hint_entity) * kContextEntitySize);
}

void observe_grenade_native_window(const bool input_current) noexcept {
    if (!g_grenade_native_observer_enabled.load(std::memory_order_acquire)) return;
    // Only the explicitly opted-in audit samples this native state. A positive
    // window is a locator for the next test, never an input authorization.
    thread_local bool prior_positive{};
    thread_local std::uintptr_t prior_state{}, prior_source{};
    thread_local std::uint32_t prior_incarnation{};
    const auto player = read_grenade_observer_player();
    const bool positive = player.readable && player.state.window > 0;
    const auto source = positive ? read_grenade_observer_source(player)
                                 : GrenadeObservedMissile{};
    if (positive == prior_positive && (!positive ||
            (player.state.address == prior_state && source.address == prior_source &&
             source.incarnation == prior_incarnation))) return;
    prior_positive = positive;
    prior_state = player.state.address;
    prior_source = source.address;
    prior_incarnation = source.incarnation;
    const auto pool = read_grenade_observer_inventory(player,
        source.readable ? source.weapon : player.state.offhand);
    std::int32_t time{};
    const bool time_valid = read_grenade_observer_time(&time);
    stereo_diagnostic_log(
        "GrenadeNativeWindow positive=%d inputCurrent=%d playerReadable=%d playerState=0x%08X window=%d owner=%d fuse=%d offhand=%u hintEntity=%d sourceReadable=%d source=0x%08X sourceIndex=%u allocated11D=%u linkedRaw118=%u incarnation90=%u type=%u handler=%u weapon=%u weaponRaw=%u sourceOwner=%u expiryRaw=%d levelTimeReadable=%d levelTimeRaw=%d poolReadable=%d reserve=%d clip=%d owned=%d observationOnly=1 contextualInputEnabled=0",
        positive ? 1 : 0, input_current ? 1 : 0, player.readable ? 1 : 0,
        static_cast<unsigned>(player.state.address), player.state.window,
        player.state.owner, player.state.fuse, player.state.offhand,
        player.state.hint_entity, source.readable ? 1 : 0,
        static_cast<unsigned>(source.address), source.index,
        static_cast<unsigned>(source.in_use), static_cast<unsigned>(source.linked_raw),
        source.incarnation, source.type, static_cast<unsigned>(source.handler),
        source.weapon, source.weapon_raw, static_cast<unsigned>(source.owner_handle),
        source.scheduled_expiry, time_valid ? 1 : 0, time,
        pool.readable ? 1 : 0, pool.reserve, pool.clip, pool.owned ? 1 : 0);
}

void log_grenade_observer_queue(const std::uint64_t receipt,
    const char* phase, const GrenadeObservedPlayer& player,
    const std::uint32_t ordinal, const std::uint32_t event) noexcept {
    const auto observation = observe_grenade_native_queue(
        player.queue, ordinal, event);
    stereo_diagnostic_log(
        "GrenadeNativeQueue receipt=%llu phase=%s readable=%d ordinalRaw=%u producerRaw=%u distanceModulo256=%u events=%u,%u,%u,%u parameters=%u,%u,%u,%u boundedSlotMatches=%d eventOwnershipEstablished=0 queueDrainEstablished=0",
        static_cast<unsigned long long>(receipt), phase,
        player.queue.readable ? 1 : 0, ordinal, player.queue.producer,
        observation.moduloDistance, player.queue.events[0], player.queue.events[1],
        player.queue.events[2], player.queue.events[3], player.queue.parameters[0],
        player.queue.parameters[1], player.queue.parameters[2],
        player.queue.parameters[3], observation.boundedSlotMatches ? 1 : 0);
}

[[nodiscard]] std::uint64_t observe_grenade_completed_producer_link(
    const GrenadeObservedPlayer&, std::uint32_t, std::uint32_t) noexcept;
[[nodiscard]] std::uint64_t observe_grenade_enclosing_dispatch_link(
    std::uintptr_t, std::uintptr_t) noexcept;
void install_grenade_queue_native_observer(
    const wawvr::t4::ValidatedBindings&) noexcept;

void observe_grenade_native_event_call(const std::uintptr_t original,
    void* entity, const std::uint32_t ordinal, const std::uint32_t event) {
    // This adapter ALWAYS invokes the original once, including rejected/missing
    // observations. It neither runs the contextual transaction nor changes input.
    const DWORD caller_last_error = GetLastError();
    const bool local = g_grenade_native_observer_enabled.load(
        std::memory_order_acquire) &&
        reinterpret_cast<std::uintptr_t>(entity) == g_local_player_entity;
    const auto receipt = local ? claim_grenade_observer_receipt() : 0;
    const bool sampled = local && receipt != 0 && receipt <= 256;
    const auto before = sampled ? read_grenade_observer_player()
                                : GrenadeObservedPlayer{};
    const auto produced_receipt = sampled
        ? observe_grenade_completed_producer_link(before, ordinal, event) : 0;
    const auto dispatcher_receipt = sampled
        ? observe_grenade_enclosing_dispatch_link(
            reinterpret_cast<std::uintptr_t>(entity), before.state.address) : 0;
    const auto source_before = sampled && event == 40
        ? read_grenade_observer_source(before) : GrenadeObservedMissile{};
    const auto inventory_weapon = source_before.readable &&
        source_before.type == kContextMissileType && source_before.in_use == 1
        ? source_before.weapon : before.state.offhand;
    const auto inventory_before = sampled
        ? read_grenade_observer_inventory(before, inventory_weapon)
        : GrenadeObservedInventoryPool{};
    std::int32_t time_before{};
    const bool time_before_valid = sampled && read_grenade_observer_time(&time_before);
    const auto previous = g_grenade_observer_thread;
    struct RestoreFrame final {
        GrenadeObserverThreadFrame previous;
        ~RestoreFrame() { g_grenade_observer_thread = previous; }
    } restore{previous};
    // Even an unsampled/nonlocal nested event shadows its outer frame. A
    // nested native spawn must not inherit an unrelated outer receipt.
    g_grenade_observer_thread = sampled
        ? GrenadeObserverThreadFrame{receipt, event, ordinal,
            reinterpret_cast<std::uintptr_t>(entity), produced_receipt,
            dispatcher_receipt}
        : GrenadeObserverThreadFrame{};
    SetLastError(caller_last_error);
    if (event == 40) call_context_native_pickup(original, entity);
    else call_context_native_throw(original, entity);
    struct RestoreLastError final {
        DWORD value;
        ~RestoreLastError() { SetLastError(value); }
    } restore_error{GetLastError()};
    if (local && (event == 39 || event == 40)) g_grenade_observer_pickup = {};
    if (!sampled) return;
    const auto after = read_grenade_observer_player();
    const auto inventory_after = read_grenade_observer_inventory(
        after, inventory_weapon);
    const auto source_after = source_before.readable
        ? read_grenade_observer_missile(source_before.address)
        : GrenadeObservedMissile{};
    std::int32_t time_after{};
    const bool time_after_valid = read_grenade_observer_time(&time_after);
    const bool source_transition = observe_grenade_native_allocation_release(
        {source_before.readable, source_before.address, source_before.type,
         source_before.incarnation, source_before.in_use, source_before.handler},
        {source_after.readable, source_after.address, source_after.type,
         source_after.incarnation, source_after.in_use, source_after.handler});
    if (event == 40) {
        g_grenade_observer_pickup = {};
        if (source_transition && before.readable && after.readable &&
            after.state.offhand == source_before.weapon &&
            after.state.owner >= 0 && after.state.owner < 0x3FF) {
            g_grenade_observer_pickup = {receipt, after.state.address,
                source_before.weapon, after.state.owner, source_before,
                time_after_valid, time_after};
        }
    }
    stereo_diagnostic_log(
        "GrenadeNativeEvent receipt=%llu event=%u entity=%p ordinalRaw=%u producedReceipt=%llu dispatcherReceipt=%llu beforeReadable=%d afterReadable=%d playerState=0x%08X/0x%08X hintEntity=%d/%d offhand=%u/%u owner=%d/%d fuse=%d/%d window=%d/%d state=%d/%d levelTimeReadable=%d/%d levelTimeRaw=%d/%d handlerReturned=1 nativeCalls=1 observationOnly=1 eventOwnershipEstablished=0 queueDrainEstablished=0",
        static_cast<unsigned long long>(receipt), event, entity, ordinal,
        static_cast<unsigned long long>(produced_receipt),
        static_cast<unsigned long long>(dispatcher_receipt),
        before.readable ? 1 : 0, after.readable ? 1 : 0,
        static_cast<unsigned>(before.state.address),
        static_cast<unsigned>(after.state.address),
        before.state.hint_entity, after.state.hint_entity, before.state.offhand,
        after.state.offhand, before.state.owner, after.state.owner,
        before.state.fuse, after.state.fuse, before.state.window,
        after.state.window, before.state.weapon_state, after.state.weapon_state,
        time_before_valid ? 1 : 0, time_after_valid ? 1 : 0,
        time_before, time_after);
    log_grenade_observer_queue(receipt, "before", before, ordinal, event);
    log_grenade_observer_queue(receipt, "after-handler", after, ordinal, event);
    stereo_diagnostic_log(
        "GrenadeNativeInventory receipt=%llu beforeReadable=%d afterReadable=%d reserveClipOwnershipHash=%016llX/%016llX hashEqual=%d poolReadable=%d/%d weapon=%u ammoIndex=%d/%d clipIndex=%d/%d reserve=%d/%d clip=%d/%d owned=%d/%d noInventoryWritesByObserver=1",
        static_cast<unsigned long long>(receipt), before.readable ? 1 : 0,
        after.readable ? 1 : 0,
        static_cast<unsigned long long>(before.inventory_hash),
        static_cast<unsigned long long>(after.inventory_hash),
        before.readable && after.readable &&
            before.inventory_hash == after.inventory_hash ? 1 : 0,
        inventory_before.readable ? 1 : 0, inventory_after.readable ? 1 : 0,
        inventory_weapon, inventory_before.ammo_index, inventory_after.ammo_index,
        inventory_before.clip_index, inventory_after.clip_index,
        inventory_before.reserve, inventory_after.reserve,
        inventory_before.clip, inventory_after.clip,
        inventory_before.owned ? 1 : 0, inventory_after.owned ? 1 : 0);
    if (event == 40) stereo_diagnostic_log(
        "GrenadeNativeSource receipt=%llu readable=%d/%d source=0x%08X index=%u type=%u/%u weapon=%u/%u weaponRaw=%u/%u ownerHandle=%u/%u allocated11D=%u/%u linkedRaw118=%u/%u incarnation90=%u/%u handler=%u/%u scheduledExpiryRaw=%d/%d allocationFreedObserved=%d sourceIncarnationEstablished=0",
        static_cast<unsigned long long>(receipt), source_before.readable ? 1 : 0,
        source_after.readable ? 1 : 0, static_cast<unsigned>(source_before.address),
        source_before.index, source_before.type, source_after.type,
        source_before.weapon, source_after.weapon,
        source_before.weapon_raw, source_after.weapon_raw,
        static_cast<unsigned>(source_before.owner_handle),
        static_cast<unsigned>(source_after.owner_handle),
        static_cast<unsigned>(source_before.in_use),
        static_cast<unsigned>(source_after.in_use),
        static_cast<unsigned>(source_before.linked_raw),
        static_cast<unsigned>(source_after.linked_raw),
        source_before.incarnation, source_after.incarnation,
        static_cast<unsigned>(source_before.handler),
        static_cast<unsigned>(source_after.handler),
        source_before.scheduled_expiry, source_after.scheduled_expiry,
        source_transition ? 1 : 0);
}

struct GrenadeObserverSpawnBefore final {
    bool sampled{};
    GrenadeObserverThreadFrame dispatch{};
    GrenadeObserverPickupCandidate candidate{};
    GrenadeObservedPlayer player{};
    GrenadeObservedInventoryPool inventory{};
    std::int32_t level_time{};
    bool level_time_readable{};
};

[[nodiscard]] GrenadeObserverSpawnBefore observe_grenade_native_spawn_begin(
    const void* parent, const std::uint32_t weapon) noexcept {
    GrenadeObserverSpawnBefore before{};
    if (!g_grenade_native_observer_enabled.load(std::memory_order_acquire) ||
        reinterpret_cast<std::uintptr_t>(parent) != g_local_player_entity)
        return before;
    before.dispatch = g_grenade_observer_thread;
    before.sampled = before.dispatch.receipt != 0 &&
        before.dispatch.receipt <= 256 && before.dispatch.event == 39 &&
        before.dispatch.entity == g_local_player_entity;
    if (!before.sampled) return before;
    before.player = read_grenade_observer_player();
    before.inventory = read_grenade_observer_inventory(before.player, weapon);
    before.level_time_readable = read_grenade_observer_time(&before.level_time);
    // This is a descriptive candidate from the same thread, not event ownership.
    const auto candidate = g_grenade_observer_pickup;
    if (before.player.readable && candidate.receipt != 0 &&
        candidate.player_state == before.player.state.address &&
        candidate.weapon == before.player.state.offhand &&
        candidate.owner == before.player.state.owner &&
        candidate.native_time_readable && before.level_time_readable &&
        candidate.native_time >= 0 && before.level_time >= candidate.native_time &&
        static_cast<std::int64_t>(before.level_time) - candidate.native_time <= 60000) {
        before.candidate = candidate;
        // A candidate is consumed by one observed native invocation, never a
        // permission and never retained for a second replay/nested spawn.
        g_grenade_observer_pickup = {};
    }
    return before;
}

void observe_grenade_native_spawn_end(const GrenadeObserverSpawnBefore& before,
    void* grenade, const std::uint32_t weapon, const int fuse_argument) noexcept {
    if (!before.sampled) return;
    const auto after = read_grenade_observer_player();
    const auto inventory_after = read_grenade_observer_inventory(after, weapon);
    const auto missile = read_grenade_observer_missile(
        reinterpret_cast<std::uintptr_t>(grenade));
    std::int32_t level_time_after{};
    const bool time_readable = read_grenade_observer_time(&level_time_after);
    const bool candidate_present = before.candidate.receipt != 0;
    stereo_diagnostic_log(
        "GrenadeNativeSpawn receipt=%llu event=39 ordinalRaw=%u producedReceipt=%llu dispatcherReceipt=%llu weapon=%u returnedMissile=%p readable=%d index=%u allocated11D=%u linkedRaw118=%u incarnation90=%u type=%u handler=%u missileWeapon=%u missileWeaponRaw=%u ownerHandle=%u scheduledExpiryRaw=%d playerReadable=%d/%d playerState=0x%08X/0x%08X levelTimeReadable=%d/%d levelTimeRaw=%d/%d nativeRemainingFuse=%d/%d fuseArgument=%d pickupCandidateReceipt=%llu candidateSource=0x%08X sourceExpiryRaw=%d rawExpiryEqual=%d expiryPreservationEstablished=0 eventOwnershipEstablished=0 nativeInputsUnchangedByObserver=1",
        static_cast<unsigned long long>(before.dispatch.receipt),
        before.dispatch.ordinal,
        static_cast<unsigned long long>(before.dispatch.produced_receipt),
        static_cast<unsigned long long>(before.dispatch.dispatcher_receipt),
        weapon, grenade, missile.readable ? 1 : 0,
        missile.index, static_cast<unsigned>(missile.in_use),
        static_cast<unsigned>(missile.linked_raw), missile.incarnation, missile.type,
        static_cast<unsigned>(missile.handler), missile.weapon, missile.weapon_raw,
        static_cast<unsigned>(missile.owner_handle), missile.scheduled_expiry,
        before.player.readable ? 1 : 0, after.readable ? 1 : 0,
        static_cast<unsigned>(before.player.state.address),
        static_cast<unsigned>(after.state.address),
        before.level_time_readable ? 1 : 0, time_readable ? 1 : 0,
        before.level_time, level_time_after,
        before.player.state.fuse, after.state.fuse, fuse_argument,
        static_cast<unsigned long long>(before.candidate.receipt),
        static_cast<unsigned>(before.candidate.source.address),
        before.candidate.source.scheduled_expiry,
        candidate_present && missile.readable &&
            before.candidate.source.scheduled_expiry == missile.scheduled_expiry
                ? 1 : 0);
    stereo_diagnostic_log(
        "GrenadeNativeSpawnInventory receipt=%llu weapon=%u readable=%d/%d reserve=%d/%d clip=%d/%d owned=%d/%d noInventoryWritesByObserver=1",
        static_cast<unsigned long long>(before.dispatch.receipt), weapon,
        before.inventory.readable ? 1 : 0, inventory_after.readable ? 1 : 0,
        before.inventory.reserve, inventory_after.reserve,
        before.inventory.clip, inventory_after.clip,
        before.inventory.owned ? 1 : 0, inventory_after.owned ? 1 : 0);
}

void context_native_pickup_bridge();
void context_native_throw_event_bridge();

[[nodiscard]] bool grenade_observer_call_matches(const std::uintptr_t call,
    const std::uintptr_t target) noexcept {
    std::array<std::uint8_t, 5> bytes{};
    if (!copy_grenade_observer_memory(bytes.data(), call, bytes.size()) ||
        bytes[0] != 0xE8) return false;
    std::int32_t relative{};
    std::memcpy(&relative, bytes.data() + 1, 4);
    return static_cast<std::int64_t>(call) + 5 + relative ==
        static_cast<std::int64_t>(target);
}

void install_grenade_native_observer(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    std::array<wchar_t, 2> enabled{};
    if (GetEnvironmentVariableW(kGrenadeNativeAuditEnvironment,
            enabled.data(), static_cast<DWORD>(enabled.size())) != 1 ||
        enabled[0] != L'1' ||
        g_grenade_native_observer_installed.load(std::memory_order_acquire) ||
        g_contextual_experiment_installed.load(std::memory_order_acquire)) return;
    struct Fingerprint final {
        wawvr::t4::Rva rva; std::size_t size; std::uint64_t fnv;
    };
    constexpr std::array<Fingerprint, 7> fingerprints{{
        {0x000E7A20, 1380, 0xF74AF54210886B30ULL},
        {0x000E7910, 265, 0x4B9B8A182A08F0EFULL},
        {0x00152780, 75, 0x465884921B6ED666ULL},
        {0x000FD210, 355, 0xA7540E93320E5D11ULL},
        {0x0010B300, 162, 0x2EDEFC4AC6E7098AULL},
        {0x0014E870, 173, 0x9EE42182EE3CB14FULL},
        {0x0014EDC0, 984, 0x5C234E95E9571BF7ULL},
    }};
    const auto match = [&]() noexcept {
        for (const auto& item : fingerprints) {
            const auto address = bindings.module().address(item.rva, item.size);
            std::array<std::uint8_t, 1380> bytes{};
            if (!address || item.size > bytes.size() ||
                !copy_grenade_observer_memory(bytes.data(), *address,
                    item.size)) return false;
            std::uint64_t hash = 14695981039346656037ULL;
            for (std::size_t i = 0; i < item.size; ++i)
                hash = (hash ^ bytes[i]) * 1099511628211ULL;
            if (hash != item.fnv) return false;
        }
        return true;
    };
    const auto pickup_call = bindings.module().address(kContextPickupCallRva, 5);
    const auto pickup = bindings.module().address(kContextPickupTargetRva, 265);
    const auto throw_call = bindings.module().address(kContextThrowEventCallRva, 5);
    const auto native_throw = bindings.module().address(kContextThrowEventTargetRva, 75);
    const auto time = bindings.module().address(kGrenadeNativeLevelTimeRva, 4);
    if (!pickup_call || !pickup || !throw_call || !native_throw || !time ||
        !accessible_range(reinterpret_cast<const void*>(*time), 4) || !match() ||
        !grenade_observer_call_matches(*pickup_call, *pickup) ||
        !grenade_observer_call_matches(*throw_call, *native_throw)) {
        stereo_diagnostic_log("GrenadeNativeAudit unavailable: exact-SP dispatcher/target/fuse fingerprints rejected; no observer hooks installed");
        return;
    }
    const std::array<std::uintptr_t, 2> calls{*pickup_call, *throw_call};
    std::array<DWORD, 2> original_protections{};
    const auto read_protection = [](const std::uintptr_t call,
                                   DWORD* protection) noexcept {
        MEMORY_BASIC_INFORMATION information{};
        if (protection == nullptr || VirtualQuery(reinterpret_cast<void*>(call),
                &information, sizeof(information)) != sizeof(information) ||
            information.State != MEM_COMMIT ||
            (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
            information.RegionSize < 5 ||
            call - reinterpret_cast<std::uintptr_t>(information.BaseAddress) >
                information.RegionSize - 5) return false;
        *protection = information.Protect;
        return true;
    };
    if (!read_protection(calls[0], &original_protections[0]) ||
        !read_protection(calls[1], &original_protections[1])) {
        stereo_diagnostic_log("GrenadeNativeAudit unavailable: original CALL protection unreadable; no observer hooks installed");
        return;
    }
    const std::array<PeerThreadPatchRange, 2> ranges{{
        {*pickup_call, 5}, {*throw_call, 5}}};
    SuspendedPeerThreads peers;
    PeerThreadQuiesceResult quiesce{};
    std::array<DWORD, 2> stopped_protections{};
    if (!peers.suspend(ranges, &quiesce) || !match() ||
        !read_protection(calls[0], &stopped_protections[0]) ||
        !read_protection(calls[1], &stopped_protections[1]) ||
        stopped_protections != original_protections) {
        peers.resume();
        stereo_diagnostic_log("GrenadeNativeAudit unavailable: observer quiescence/fingerprint rejected error=%lu", quiesce.system_error);
        return;
    }
    // Immutable pass-through targets precede either exposed CALL. No mutex or
    // diagnostic logging is acquired while another thread may be suspended.
    g_contextual_native_pickup = *pickup;
    g_contextual_native_throw = *native_throw;
    g_grenade_native_observer_level_time = *time;
    struct PatchOperations final {
        std::array<std::uintptr_t, 2> calls, originals, bridges;
        std::array<DWORD, 2> protections;
        DWORD error{};
        bool patch(const std::size_t index) noexcept {
            return patch_call(reinterpret_cast<std::uint8_t*>(calls[index]),
                bridges[index], &error);
        }
        bool protections_match() const noexcept {
            bool result = true;
            for (std::size_t i = 0; i < calls.size(); ++i) {
                MEMORY_BASIC_INFORMATION info{};
                const bool match = VirtualQuery(reinterpret_cast<void*>(calls[i]),
                    &info, sizeof(info)) == sizeof(info) &&
                    info.State == MEM_COMMIT && info.Protect == protections[i];
                result = match && result;
            }
            return result;
        }
        bool restore(const std::size_t index) noexcept {
            return patch_call(reinterpret_cast<std::uint8_t*>(calls[index]),
                originals[index], &error);
        }
        bool restore_protections() noexcept {
            bool result = true;
            for (std::size_t i = 0; i < calls.size(); ++i) {
                DWORD ignored{};
                const bool restored = VirtualProtect(reinterpret_cast<void*>(
                    calls[i]), 5, protections[i], &ignored) != FALSE;
                if (!restored && error == 0) error = GetLastError();
                result = restored && result;
            }
            return protections_match() && result;
        }
        bool originals_match() const noexcept {
            bool result = true;
            for (std::size_t i = 0; i < calls.size(); ++i) {
                const bool match = grenade_observer_call_matches(
                    calls[i], originals[i]);
                result = match && result;
            }
            return result;
        }
    } operations{calls, {*pickup, *native_throw},
        {reinterpret_cast<std::uintptr_t>(&context_native_pickup_bridge),
         reinterpret_cast<std::uintptr_t>(&context_native_throw_event_bridge)},
        original_protections};
    const auto outcome = install_grenade_observer_calls(operations);
    if (outcome.installed) {
        g_grenade_native_observer_installed.store(true, std::memory_order_release);
        g_grenade_native_observer_enabled.store(true, std::memory_order_release);
    }
    peers.resume();
    stereo_diagnostic_log(
        "GrenadeNativeAudit installed=%d rollbackVerified=%d error=%lu schema=1 ordinalCapture=caller-entry-ESP-plus-0x14 observationOnly=1 contextualInputEnabled=0 eventOwnershipEstablished=0 queueDrainEstablished=0 expiryPreservationEstablished=0",
        outcome.installed ? 1 : 0, outcome.rollbackVerified ? 1 : 0,
        operations.error);
    if (outcome.installed) install_grenade_queue_native_observer(bindings);
}

#include "grenade_queue_observer_runtime.inl"
