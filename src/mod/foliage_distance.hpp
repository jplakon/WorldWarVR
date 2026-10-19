#pragma once

#include <cstdint>

namespace wawvr::t4 { class ValidatedBindings; }

namespace wawvr::mod {

// Enable only after the exact SP layout and its native
// static-camera/LOD/surface consumers match the independently audited bytes.
void initialize_foliage_distance(const t4::ValidatedBindings& bindings) noexcept;
bool restore_foliage_distance() noexcept;
void reset_foliage_distance_scope() noexcept;

// Select extended terminal ranges and distance culls only inside the native
// static-camera consumers during a stereo call. No shared instance, model,
// dvar, asset file, projection, or non-foliage data is modified.
class ScopedStereoFoliageDistance final {
public:
    ScopedStereoFoliageDistance(std::uint32_t view, std::uint64_t frame) noexcept;
    ~ScopedStereoFoliageDistance();
    ScopedStereoFoliageDistance(const ScopedStereoFoliageDistance&) = delete;
    ScopedStereoFoliageDistance& operator=(const ScopedStereoFoliageDistance&) = delete;
private:
    bool applied_{};
    std::uint32_t view_{};
    std::uint64_t frame_{};
    std::uint64_t prepare_us_{};
    std::int64_t native_start_{};
    std::uint32_t validation_queries_{};
};

} // namespace wawvr::mod
