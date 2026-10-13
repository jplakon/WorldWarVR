#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace wawvr::mod {

enum class ImmutableThunkRouteRegistration : std::uint8_t {
    inserted,
    already_registered,
    conflicting_callback,
    capacity_exhausted,
    invalid,
};

// A vtable thunk can still be entered after its slot has been restored: a
// calling thread may already have fetched the old function pointer.  Routes
// therefore remain valid for the process lifetime. Callers may key a table by
// the exact COM object or by its shared vtable. A key's callback is immutable,
// so an old object or vtable can never be redirected through a newer target's
// native callback.
//
// Registration has one writer (the renderer monitor). Lookup is lock-free and
// may run concurrently on any game/render thread. The callback is published
// before the release-store of its non-null key.
template <typename Key, typename Callback, std::size_t Capacity>
class ImmutableThunkRouteTable final {
    static_assert(Capacity != 0);
    static_assert(std::is_trivially_copyable_v<Key>);
    static_assert(std::is_trivially_copyable_v<Callback>);

public:
    [[nodiscard]] ImmutableThunkRouteRegistration Register(
        const Key key,
        const Callback callback) noexcept {
        if (key == Key{} || callback == Callback{}) {
            return ImmutableThunkRouteRegistration::invalid;
        }

        Entry* first_empty = nullptr;
        for (auto& entry : entries_) {
            const Key registered_key =
                entry.key.load(std::memory_order_acquire);
            if (registered_key == key) {
                return entry.callback.load(std::memory_order_acquire) ==
                               callback
                           ? ImmutableThunkRouteRegistration::already_registered
                           : ImmutableThunkRouteRegistration::conflicting_callback;
            }
            if (registered_key == Key{} && first_empty == nullptr) {
                first_empty = &entry;
            }
        }
        if (first_empty == nullptr) {
            return ImmutableThunkRouteRegistration::capacity_exhausted;
        }

        first_empty->callback.store(callback, std::memory_order_relaxed);
        first_empty->key.store(key, std::memory_order_release);
        return ImmutableThunkRouteRegistration::inserted;
    }

    [[nodiscard]] Callback Lookup(const Key key) const noexcept {
        if (key == Key{}) {
            return Callback{};
        }
        for (const auto& entry : entries_) {
            if (entry.key.load(std::memory_order_acquire) == key) {
                return entry.callback.load(std::memory_order_acquire);
            }
        }
        return Callback{};
    }

private:
    struct Entry final {
        std::atomic<Key> key{};
        std::atomic<Callback> callback{};
    };

    std::array<Entry, Capacity> entries_{};
};

// During a D3D9 device recreation, a replacement object can begin using an
// already-patched shared vtable before the renderer monitor has registered the
// new object pointer. The immutable shared-vtable route preserves the native
// call through that transition instead of fabricating D3DERR_INVALIDCALL. A
// readable, registered current vtable is authoritative if Windows recycled an
// object address; the exact-object route remains a fallback for a retired
// in-flight call whose vtable can no longer be read.
template <
    typename ObjectKey, typename SharedKey, typename Callback,
    std::size_t ObjectCapacity, std::size_t SharedCapacity>
[[nodiscard]] Callback lookup_immutable_thunk_route(
    const ImmutableThunkRouteTable<
        ObjectKey, Callback, ObjectCapacity>& object_routes,
    const ObjectKey object_key,
    const ImmutableThunkRouteTable<
        SharedKey, Callback, SharedCapacity>& shared_routes,
    const SharedKey shared_key) noexcept {
    const Callback shared = shared_routes.Lookup(shared_key);
    return shared != Callback{} ? shared : object_routes.Lookup(object_key);
}

} // namespace wawvr::mod
