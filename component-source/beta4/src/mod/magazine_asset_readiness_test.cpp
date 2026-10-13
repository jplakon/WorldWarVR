// SPDX-License-Identifier: GPL-3.0-only
#include "magazine_asset_readiness.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

using wawvr::mod::MagazineAssetReadiness;
using wawvr::mod::refresh_magazine_asset_readiness;

void test_existing_object_recovers_without_recreation() {
    int model = 1;
    int other_model = 2;
    const int* native_model = nullptr;
    bool ready = false;
    bool failed = true;
    int validations = 0;
    const auto verify_existing_object = [&]() noexcept {
        ++validations;
        return native_model == &model;
    };

    // The creation call already returned, but its first postcondition was not
    // readable. The next visit must retain the same unavailable object.
    require(refresh_magazine_asset_readiness(
                true, ready, failed, verify_existing_object) ==
                MagazineAssetReadiness::unavailable && !ready && failed,
            "an unreadable created object stays unavailable");
    native_model = &other_model;
    require(refresh_magazine_asset_readiness(
                true, ready, failed, verify_existing_object) ==
                MagazineAssetReadiness::unavailable && !ready && failed,
            "a readable but different model cannot claim cached geometry");

    native_model = &model;
    require(refresh_magazine_asset_readiness(
                true, ready, failed, verify_existing_object) ==
                MagazineAssetReadiness::recovered && ready && !failed,
            "later exact validation recovers the existing cached object");
    require(validations == 3,
            "each unavailable cache visit rechecks the existing object");
    require(refresh_magazine_asset_readiness(
                true, ready, failed, verify_existing_object) ==
                MagazineAssetReadiness::ready && validations == 3,
            "a ready cache hit retains the original cheap path");
}

void test_precreation_failures_do_not_inspect_an_unowned_object() {
    bool ready = false;
    bool failed = true;
    int validations = 0;
    const auto verify_existing_object = [&]() noexcept {
        ++validations;
        return true;
    };
    require(refresh_magazine_asset_readiness(
                false, ready, failed, verify_existing_object) ==
                MagazineAssetReadiness::unavailable && validations == 0,
            "failure before native creation cannot validate empty DObj storage");
    failed = false;
    require(refresh_magazine_asset_readiness(
                true, ready, failed, verify_existing_object) ==
                MagazineAssetReadiness::unavailable && validations == 0,
            "only a completed failed attempt is eligible for recovery");
}

}  // namespace

int main() {
    test_existing_object_recovers_without_recreation();
    test_precreation_failures_do_not_inspect_an_unowned_object();
    return 0;
}
