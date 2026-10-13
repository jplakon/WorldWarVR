#include "stereo_emissive_viewport_logic.hpp"

#include <cstdlib>

namespace {

void require(const bool condition) {
    if (!condition) {
        std::abort();
    }
}

} // namespace

int main() {
    using wawvr::mod::StereoEmissiveViewport;
    using wawvr::mod::should_force_stereo_emissive_viewport;
    using wawvr::mod::stereo_emissive_viewport_fits_target;

    constexpr StereoEmissiveViewport left{0, 0, 3008, 2688};
    constexpr StereoEmissiveViewport right{3008, 0, 3008, 2688};
    constexpr StereoEmissiveViewport full{0, 0, 6016, 2688};

    static_assert(stereo_emissive_viewport_fits_target(left, 6016, 2688));
    static_assert(stereo_emissive_viewport_fits_target(right, 6016, 2688));
    static_assert(!stereo_emissive_viewport_fits_target(
        StereoEmissiveViewport{-1, 0, 3008, 2688}, 6016, 2688));
    static_assert(!stereo_emissive_viewport_fits_target(
        StereoEmissiveViewport{3008, 0, 3009, 2688}, 6016, 2688));

    require(!should_force_stereo_emissive_viewport(
        false, 2u, right, left, 6016, 2688));
    require(!should_force_stereo_emissive_viewport(
        true, 0u, right, left, 6016, 2688));
    require(!should_force_stereo_emissive_viewport(
        true, 2u, right, right, 6016, 2688));
    require(should_force_stereo_emissive_viewport(
        true, 2u, right, full, 6016, 2688));
    require(should_force_stereo_emissive_viewport(
        true, 2u, right, left, 6016, 2688));
    return EXIT_SUCCESS;
}
