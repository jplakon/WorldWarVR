#include "physical_scope_draw_filter.hpp"
#include "draw_surface_validation_logic.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const char* const message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::uint64_t draw_surface(
    const std::uint32_t surface_type,
    const std::uint16_t object_id) {
    return static_cast<std::uint64_t>(object_id) |
           (static_cast<std::uint64_t>(surface_type) << 54U);
}

template <typename T>
void write_at(
    std::vector<std::byte>* const bytes,
    const std::size_t offset,
    const T value) {
    std::memcpy(bytes->data() + offset, &value, sizeof(value));
}

void test_surface_validation_access() {
    using namespace wawvr::mod;
    expect(!needs_mod_draw_surface_inspection(false, false, true),
           "ordinary draw without scope owners passes to native without inspection");
    expect(needs_mod_draw_surface_inspection(true, false, true),
           "emissive eye repair always retains descriptor validation");
    expect(needs_mod_draw_surface_inspection(false, true, true),
           "an active scope pass never takes the native-only shortcut");
    expect(needs_mod_draw_surface_inspection(false, false, false),
           "published or uncertain scope owners retain descriptor validation");
    std::array<std::uint64_t, 4> surfaces{};
    std::size_t queries = 0;
    bool readable = true;
    auto query = [&](const void* const address,
                     const std::size_t size) noexcept {
        ++queries;
        return readable && address == surfaces.data() &&
            size == sizeof(surfaces);
    };
    expect(validate_draw_surface_access(
               surfaces.data(), 4, 8, sizeof(std::uint64_t),
               DrawSurfaceAccess::native_passthrough, query) == nullptr &&
               queries == 0,
           "native passthrough does not query an array the mod never reads");
    expect(validate_draw_surface_access(
               surfaces.data(), 4, 8, sizeof(std::uint64_t),
               DrawSurfaceAccess::inspect, query) == nullptr && queries == 1,
           "scope and diagnostic readers validate the exact whole array");
    readable = false;
    const char* const rejected = validate_draw_surface_access(
        surfaces.data(), 4, 8, sizeof(std::uint64_t),
        DrawSurfaceAccess::inspect, query);
    expect(rejected != nullptr &&
               std::strcmp(rejected, "surface-range-unreadable") == 0 &&
               queries == 2,
           "each reader performs a fresh query and rejects changed protection");

    for (const auto access : {
             DrawSurfaceAccess::native_passthrough,
             DrawSurfaceAccess::inspect}) {
        queries = 0;
        expect(validate_draw_surface_access(
                   nullptr, 0, 8, sizeof(std::uint64_t), access, query) ==
                   nullptr && queries == 0,
               "empty draw arrays never dereference or query null data");
        const char* const null_reason = validate_draw_surface_access(
            nullptr, 1, 8, sizeof(std::uint64_t), access, query);
        expect(null_reason != nullptr && std::strcmp(
                   null_reason, "nonzero-count-null-surfaces") == 0,
               "both routes reject a nonempty null array");
        const char* const cap_reason = validate_draw_surface_access(
            surfaces.data(), 9, 8, sizeof(std::uint64_t), access, query);
        expect(cap_reason != nullptr && std::strcmp(
                   cap_reason, "count-over-cap") == 0,
               "both routes retain the surface count cap");
        const char* const overflow_reason = validate_draw_surface_access(
            surfaces.data(), 2, 8,
            (std::numeric_limits<std::size_t>::max)(), access, query);
        expect(overflow_reason != nullptr && std::strcmp(
                   overflow_reason, "surface-byte-overflow") == 0,
               "both routes reject surface-byte multiplication overflow");
        expect(queries == 0,
               "structurally rejected arrays do not reach a memory reader");
    }
}

}  // namespace

int main() {
    using namespace wawvr::mod;
    test_surface_validation_access();
    static_assert(kRetailGfxEntitiesOffset == 0x13B160U);
    static_assert(kRetailGfxEntityStride == 24U);
    static_assert(kRetailGfxEntityCount == 128U);

    constexpr std::uint16_t kObjectId = 12;
    constexpr std::uint16_t kEntityIndex = 5;
    std::vector<std::byte> data(
        kRetailGfxEntitiesOffset +
        kRetailGfxEntityCount * kRetailGfxEntityStride);
    write_at(
        &data,
        static_cast<std::size_t>(kObjectId) * 4U + 14U,
        kEntityIndex);
    write_at(
        &data,
        kRetailGfxEntitiesOffset +
            static_cast<std::size_t>(kEntityIndex) *
                kRetailGfxEntityStride,
        std::uint32_t{2});

    expect(is_depth_hacked_viewmodel_draw_surface(
               draw_surface(7, kObjectId), data) &&
               is_depth_hacked_viewmodel_draw_surface(
                   draw_surface(8, kObjectId), data) &&
               is_depth_hacked_viewmodel_draw_surface(
                   draw_surface(9, kObjectId), data),
           "all three retail XModel surface types honor the depth-hack flag");
    expect(!is_depth_hacked_viewmodel_draw_surface(
               draw_surface(6, kObjectId), data),
           "non-XModel world geometry is never filtered");
    const std::uint64_t semantic_surface_type_only =
        static_cast<std::uint64_t>(kObjectId) |
        (static_cast<std::uint64_t>(4) << 50U);
    expect(!is_depth_hacked_viewmodel_draw_surface(
               semantic_surface_type_only, data),
           "the semantic surfType field is not confused with the retail tess-dispatch field");

    write_at(
        &data,
        kRetailGfxEntitiesOffset +
            static_cast<std::size_t>(kEntityIndex) *
                kRetailGfxEntityStride,
        std::uint32_t{0});
    expect(!is_depth_hacked_viewmodel_draw_surface(
               draw_surface(7, kObjectId), data),
           "ordinary XModels stay visible");
    expect(!is_depth_hacked_viewmodel_draw_surface(
               draw_surface(7, kObjectId),
               std::span<const std::byte>{data.data(), 64}),
           "truncated backend data fails open");
    expect(!is_depth_hacked_viewmodel_draw_surface(
               draw_surface(7, 0xFFFFU), data),
           "out-of-range model-surface references fail open");

    constexpr std::uint16_t kSecondEntityIndex = 9;
    const std::size_t first_flags_offset = kRetailGfxEntitiesOffset +
        static_cast<std::size_t>(kEntityIndex) * kRetailGfxEntityStride;
    const std::size_t second_flags_offset = kRetailGfxEntitiesOffset +
        static_cast<std::size_t>(kSecondEntityIndex) *
            kRetailGfxEntityStride;
    write_at(&data, first_flags_offset, std::uint32_t{3});
    write_at(&data, second_flags_offset, std::uint32_t{6});
    RetailDepthHackFlagSnapshot snapshot{};
    expect(clear_retail_depth_hack_flags(data, &snapshot) &&
               snapshot.valid && snapshot.cleared_count == 2,
           "diagnostic clear captures every depth-hacked entity exactly once");
    std::uint32_t first_cleared = 0;
    std::uint32_t second_cleared = 0;
    std::memcpy(
        &first_cleared, data.data() + first_flags_offset,
        sizeof(first_cleared));
    std::memcpy(
        &second_cleared, data.data() + second_flags_offset,
        sizeof(second_cleared));
    expect(first_cleared == 1 && second_cleared == 4,
           "diagnostic clear removes only bit 0x2");
    expect(restore_retail_depth_hack_flags(data, snapshot),
           "diagnostic clear is reversible");
    std::uint32_t first_restored = 0;
    std::uint32_t second_restored = 0;
    std::memcpy(
        &first_restored, data.data() + first_flags_offset,
        sizeof(first_restored));
    std::memcpy(
        &second_restored, data.data() + second_flags_offset,
        sizeof(second_restored));
    expect(first_restored == 3 && second_restored == 6,
           "restore writes the exact original render flags");
    expect(!clear_retail_depth_hack_flags(
               std::span<std::byte>{data.data(), 64}, &snapshot),
           "diagnostic clear rejects truncated backend data");
    expect(!clear_retail_depth_hack_flags(data, nullptr),
           "diagnostic clear rejects a null snapshot");

    if (failures == 0) {
        std::cout << "physical scope draw filter tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
