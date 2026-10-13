#include "stereo_backend_eye_order_logic.hpp"

#include <cstdlib>
#include <iostream>

namespace {

bool expect(
    const bool condition,
    const char* const description) noexcept {
    if (!condition) {
        std::cerr << "FAILED: " << description << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    bool passed = true;

    const auto normal = wawvr::mod::select_stereo_backend_eye_order(
        2u, false, false);
    passed &= expect(normal.count == 2u, "normal stereo count");
    passed &= expect(!normal.reversed, "normal stereo is not reversed");
    passed &= expect(
        normal.view_indices[0] == 0u && normal.view_indices[1] == 1u,
        "normal stereo order");

    const auto reversed = wawvr::mod::select_stereo_backend_eye_order(
        2u, true, false);
    passed &= expect(reversed.count == 2u, "reversed stereo count");
    passed &= expect(reversed.reversed, "diagnostic stereo is reversed");
    passed &= expect(
        reversed.view_indices[0] == 1u &&
            reversed.view_indices[1] == 0u,
        "diagnostic stereo order");

    const auto scope = wawvr::mod::select_stereo_backend_eye_order(
        3u, true, true);
    passed &= expect(scope.count == 3u, "scope stereo count");
    passed &= expect(!scope.reversed, "scope order is never reversed");
    passed &= expect(
        scope.view_indices[0] == 0u &&
            scope.view_indices[1] == 1u &&
            scope.view_indices[2] == 2u,
        "scope order remains unchanged");

    const auto invalid = wawvr::mod::select_stereo_backend_eye_order(
        4u, true, false);
    passed &= expect(invalid.count == 0u, "unsupported view count rejected");
    passed &= expect(!invalid.reversed, "unsupported view count not reversed");

    if (!passed) {
        return EXIT_FAILURE;
    }
    std::cout << "stereo backend eye-order logic tests passed\n";
    return EXIT_SUCCESS;
}
