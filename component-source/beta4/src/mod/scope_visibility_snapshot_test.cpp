#include "scope_visibility_snapshot.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <type_traits>

namespace {

int failures = 0;

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <std::size_t Size>
using Storage = std::array<
    std::array<std::byte, Size>, wawvr::mod::kScopeVisibilityBankCount>;

template <std::size_t Size>
wawvr::mod::ScopeVisibilitySnapshot::Banks spans(Storage<Size>& storage) {
    wawvr::mod::ScopeVisibilitySnapshot::Banks result{};
    for (std::size_t bank = 0; bank < storage.size(); ++bank) {
        result[bank] = storage[bank];
    }
    return result;
}

// T4's filter writes a rejection, but does not clear one when a later view
// contains the entity. Its portal walk only visits unknown visibility bytes.
void native_camera_pass(std::byte& visibility, const bool inside_frustum) {
    if (!inside_frustum) {
        visibility = std::byte{2};
    } else if (visibility == std::byte{0}) {
        visibility = std::byte{1};
    }
}

void test_scope_cannot_poison_next_eye() {
    Storage<32> storage{};
    storage[0][3] = std::byte{1};
    const auto baseline = storage;
    wawvr::mod::ScopeVisibilitySnapshot snapshot;
    expect(snapshot.capture(spans(storage)), "capture known and unknown entities");
    expect(snapshot.changed_bytes() == 0, "unmodified capture reports no changes");
    native_camera_pass(storage[0][3], false);
    native_camera_pass(storage[0][4], false);
    native_camera_pass(storage[0][3], true);
    native_camera_pass(storage[0][4], true);
    expect(storage[0][3] == std::byte{2} && storage[0][4] == std::byte{2},
           "native broad pass cannot recover scope's cached rejection alone");
    expect(snapshot.changed_bytes() == 2,
           "diagnostics count visible and unknown entities newly culled by scope");
    snapshot.restore();
    expect(storage == baseline, "restore exact visible and unknown baseline bytes");
    expect(snapshot.changed_bytes() == 0, "inactive restored snapshot reports no changes");
    native_camera_pass(storage[0][3], true);
    native_camera_pass(storage[0][4], true);
    expect(storage[0][3] == std::byte{1} && storage[0][4] == std::byte{1},
           "later eye retains visible entities and reevaluates unknown entities");
    const auto later_eye = storage;
    snapshot.restore();
    expect(storage == later_eye && !snapshot.active(),
           "repeated restore cannot overwrite later eye's valid visibility");
}

void test_all_banks_and_reuse() {
    Storage<wawvr::mod::kScopeVisibilityMaxEntities> storage;
    for (std::size_t bank = 0; bank < storage.size(); ++bank) {
        for (std::size_t entity = 0; entity < storage[bank].size(); ++entity) {
            storage[bank][entity] =
                static_cast<std::byte>((entity + bank * 31) % 256);
        }
    }
    const auto baseline = storage;
    wawvr::mod::ScopeVisibilitySnapshot snapshot;
    expect(snapshot.capture(spans(storage)) && snapshot.active(),
           "all seven maximum-sized banks fit fixed storage");
    for (auto& bank : storage) {
        bank.fill(std::byte{2});
    }
    expect(!snapshot.capture(spans(storage)) && snapshot.active(),
           "active recapture cannot replace original baseline with culled state");
    snapshot.restore();
    expect(storage == baseline, "all bank bytes, including final byte, restore exactly");
    storage[0][0] = std::byte{2};
    const auto next_frame = storage;
    expect(snapshot.capture(spans(storage)), "snapshot can capture a subsequent frame");
    storage[0][0] = std::byte{1};
    snapshot.restore();
    expect(storage == next_frame, "reuse restores new baseline rather than old frame");
}

void test_rejection_and_destructor() {
    Storage<32> storage{};
    storage[2][6] = std::byte{1};
    const auto baseline = storage;
    {
        wawvr::mod::ScopeVisibilitySnapshot snapshot;
        auto invalid = spans(storage);
        invalid[6] = invalid[6].first(31);
        expect(!snapshot.capture(invalid) && !snapshot.active(),
               "mismatched final bank rejects the entire transaction");
        expect(storage == baseline, "invalid capture does not change any bank");
        invalid = spans(storage);
        invalid[0] = {};
        expect(!snapshot.capture(invalid), "empty first bank is rejected");
        invalid = spans(storage);
        invalid[4] = {};
        expect(!snapshot.capture(invalid), "empty later bank is rejected");
        snapshot.restore();
        expect(storage == baseline, "restore after rejection does not write");
        expect(snapshot.capture(spans(storage)), "valid capture works after rejection");
        storage[2][6] = std::byte{2};
    }
    expect(storage == baseline, "scope exit restores active snapshot");

    Storage<wawvr::mod::kScopeVisibilityMaxEntities + 1> oversized{};
    oversized[6].back() = std::byte{1};
    {
        wawvr::mod::ScopeVisibilitySnapshot snapshot;
        expect(!snapshot.capture(spans(oversized)) && !snapshot.active(),
               "oversized spans are rejected without truncation");
    }
    expect(oversized[6].back() == std::byte{1},
           "rejected oversized snapshot leaves final entity intact");
}

}  // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<wawvr::mod::ScopeVisibilitySnapshot>);
    static_assert(!std::is_move_constructible_v<wawvr::mod::ScopeVisibilitySnapshot>);
    test_scope_cannot_poison_next_eye();
    test_all_banks_and_reuse();
    test_rejection_and_destructor();
    if (failures == 0) {
        std::cout << "Scope visibility transaction tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
