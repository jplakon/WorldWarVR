#include "viewmodel_filter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace wawvr::mod;

struct MemoryRange final {
    std::uintptr_t begin{};
    std::uintptr_t end{};
    bool writable{};
};

std::array<MemoryRange, 8> g_allowed_ranges{};
std::size_t g_allowed_range_count = 0;

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void allow_range(
    const void* const address,
    const std::size_t size,
    const bool writable = false) {
    check(g_allowed_range_count < g_allowed_ranges.size(),
          "test range capacity exceeded");
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    check(begin <= std::numeric_limits<std::uintptr_t>::max() - size,
          "test range overflow");
    g_allowed_ranges[g_allowed_range_count++] = {
        begin,
        begin + size,
        writable,
    };
}

[[nodiscard]] bool validate_test_range(
    const void* const address,
    const std::size_t size,
    const bool writable) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    if (begin > std::numeric_limits<std::uintptr_t>::max() - size) {
        return false;
    }
    const auto end = begin + size;
    for (std::size_t index = 0; index < g_allowed_range_count; ++index) {
        const auto& range = g_allowed_ranges[index];
        if (begin >= range.begin && end <= range.end &&
            (!writable || range.writable)) {
            return true;
        }
    }
    return false;
}

struct ViewmodelFixture final {
    alignas(void*) std::array<std::byte, 0x68> dobj{};
    alignas(void*) std::array<void*, 2> model_table{};
    std::array<std::byte, kT4XModelNumBonesOffset + 1> hand_model{};
    std::array<std::byte, kT4XModelNumBonesOffset + 1> weapon_model{};

    explicit ViewmodelFixture(
        const std::uint8_t hand_bones = 12,
        const std::uint8_t weapon_bones = 8,
        const std::uint8_t total_bones = 20) {
        dobj[kT4DObjNumModelsOffset] = std::byte{2};
        dobj[kT4DObjNumBonesOffset] = std::byte{total_bones};
        hand_model[kT4XModelNumBonesOffset] = std::byte{hand_bones};
        weapon_model[kT4XModelNumBonesOffset] = std::byte{weapon_bones};
        model_table = {hand_model.data(), weapon_model.data()};
        void* const table_pointer = model_table.data();
        std::memcpy(
            dobj.data() + kT4DObjModelsOffset,
            &table_pointer, sizeof(table_pointer));
        authorize();
    }

    void authorize(
        const bool header = true,
        const bool model_table_field = true,
        const bool table = true,
        const bool hand = true,
        const bool weapon = true,
        const bool hide_part_bits = true) noexcept {
        g_allowed_range_count = 0;
        if (header) {
            allow_range(dobj.data() + kT4DObjNumModelsOffset, 2);
        }
        if (model_table_field) {
            allow_range(
                dobj.data() + kT4DObjModelsOffset, sizeof(void*));
        }
        if (table) {
            allow_range(model_table.data(), sizeof(model_table));
        }
        if (hand) {
            allow_range(hand_model.data(), hand_model.size());
        }
        if (weapon) {
            allow_range(weapon_model.data(), weapon_model.size());
        }
        if (hide_part_bits) {
            allow_range(
                dobj.data() + kT4DObjHidePartBitsOffset,
                kT4DObjPartBitWordCount * sizeof(std::uint32_t), true);
        }
    }

    void set_model_count(const std::uint8_t value) noexcept {
        dobj[kT4DObjNumModelsOffset] = std::byte{value};
    }

    void set_total_bones(const std::uint8_t value) noexcept {
        dobj[kT4DObjNumBonesOffset] = std::byte{value};
    }

    void set_models(void* const hand, void* const weapon) noexcept {
        model_table = {hand, weapon};
    }

    void set_model_table(void* const table) noexcept {
        std::memcpy(
            dobj.data() + kT4DObjModelsOffset, &table, sizeof(table));
    }

    void set_hide_bits(
        const std::array<std::uint32_t, kT4DObjPartBitWordCount>& bits)
        noexcept {
        std::memcpy(
            dobj.data() + kT4DObjHidePartBitsOffset,
            bits.data(), sizeof(bits));
    }

    [[nodiscard]] std::array<
        std::uint32_t, kT4DObjPartBitWordCount> hide_bits() const noexcept {
        std::array<std::uint32_t, kT4DObjPartBitWordCount> bits{};
        std::memcpy(
            bits.data(), dobj.data() + kT4DObjHidePartBitsOffset,
            sizeof(bits));
        return bits;
    }

    [[nodiscard]] ViewmodelFilterStatus apply() noexcept {
        return hide_t4_viewmodel_hand_surfaces(
            dobj.data(), &validate_test_range);
    }
};

void check_status(
    const ViewmodelFilterStatus actual,
    const ViewmodelFilterStatus expected,
    const std::string_view message) {
    if (actual != expected) {
        throw std::runtime_error(
            std::string(message) + ": expected " +
            viewmodel_filter_status_name(expected) + ", got " +
            viewmodel_filter_status_name(actual));
    }
}

void test_hand_bone_mask_boundaries() {
    struct Case final {
        std::uint8_t hand_bones;
        std::array<std::uint32_t, kT4DObjPartBitWordCount> expected;
    };
    constexpr std::array<Case, 5> cases{{
        {1, {0x80000000U, 0, 0, 0}},
        {31, {0xFFFFFFFEU, 0, 0, 0}},
        {32, {0xFFFFFFFFU, 0, 0, 0}},
        {33, {0xFFFFFFFFU, 0x80000000U, 0, 0}},
        {127, {0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFEU}},
    }};

    for (const auto& test_case : cases) {
        const auto total_bones = static_cast<std::uint8_t>(
            test_case.hand_bones + 1U);
        ViewmodelFixture fixture(test_case.hand_bones, 1, total_bones);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::applied,
            "valid hand bone range is hidden");
        check(fixture.hide_bits() == test_case.expected,
              "mask uses T4's high-bit-first bone ordering");
    }
}

void test_native_hide_bits_are_preserved() {
    ViewmodelFixture fixture(1, 1, 2);
    constexpr std::array<std::uint32_t, kT4DObjPartBitWordCount> native{{
        0x00000001U,
        0x12345678U,
        0xA5A5A5A5U,
        0x00010000U,
    }};
    fixture.set_hide_bits(native);
    check_status(
        fixture.apply(), ViewmodelFilterStatus::applied,
        "hand mask is added to native hide bits");
    auto expected = native;
    expected[0] |= 0x80000000U;
    check(fixture.hide_bits() == expected,
          "native weapon hide-tag bits remain unchanged");
}

void test_weapon_bone_range_is_not_hidden() {
    ViewmodelFixture fixture(33, 5, 38);
    check_status(
        fixture.apply(), ViewmodelFilterStatus::applied,
        "hand model is hidden");
    constexpr std::array<std::uint32_t, kT4DObjPartBitWordCount> expected{{
        0xFFFFFFFFU,
        0x80000000U,
        0,
        0,
    }};
    check(fixture.hide_bits() == expected,
          "model-one gun bones remain outside the hidden hand range");
    check_status(
        fixture.apply(), ViewmodelFilterStatus::already_hidden,
        "reapplying the same mask is idempotent");
}

void test_all_weapon_bones_and_native_component_states_are_preserved() {
    // Exhaust all legal hand/weapon partitions, including every word
    // boundary. Render state may arrive once per eye: the second call must
    // neither add a gun hide bit nor restore an intentionally detached part.
    for (std::uint16_t hand_count = 1; hand_count < 128; ++hand_count) {
        for (std::uint16_t weapon_count = 1;
             weapon_count <= 128 - hand_count; ++weapon_count) {
            ViewmodelFixture fixture(static_cast<std::uint8_t>(hand_count),
                static_cast<std::uint8_t>(weapon_count),
                static_cast<std::uint8_t>(hand_count + weapon_count));
            std::array<std::uint32_t,kT4DObjPartBitWordCount> native{};
            for (std::uint16_t bone=hand_count;
                 bone<hand_count+weapon_count; ++bone) {
                if ((bone % 3) == 0)
                    native[bone >> 5U] |= 0x80000000U >> (bone & 31U);
            }
            fixture.set_hide_bits(native);
            check_status(fixture.apply(),ViewmodelFilterStatus::applied,
                "first-eye hand filtering succeeds for every legal partition");
            const auto first_eye = fixture.hide_bits();
            check_status(fixture.apply(),ViewmodelFilterStatus::already_hidden,
                "second eye sees the identical idempotent hand filter");
            check(fixture.hide_bits()==first_eye,
                "both eyes retain the same component visibility");
            for (std::uint16_t bone=hand_count; bone<128; ++bone) {
                const auto mask=0x80000000U >> (bone & 31U);
                check((first_eye[bone >> 5U]&mask)==(native[bone >> 5U]&mask),
                    "gun component and unused-tail bits remain exactly native");
            }
            // Native reload completion restores its own detached-component
            // bits. Filtering the new native state must not re-hide them.
            fixture.set_hide_bits({});
            check_status(fixture.apply(),ViewmodelFilterStatus::applied,
                "native restored weapon is filtered afresh after reload");
            for (std::uint16_t bone=hand_count;
                 bone<hand_count+weapon_count; ++bone)
                check((fixture.hide_bits()[bone >> 5U]&
                    (0x80000000U >> (bone & 31U)))==0,
                    "reload restoration is never overwritten for a gun bone");
        }
    }
}

void test_invalid_memory_and_layouts_fail_closed() {
    check_status(
        hide_t4_viewmodel_hand_surfaces(nullptr, &validate_test_range),
        ViewmodelFilterStatus::null_dobj, "null DObj is rejected");

    {
        ViewmodelFixture fixture;
        check_status(
            hide_t4_viewmodel_hand_surfaces(fixture.dobj.data(), nullptr),
            ViewmodelFilterStatus::null_validator,
            "null memory validator is rejected");
        fixture.authorize(false);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::inaccessible_dobj_header,
            "inaccessible DObj header is rejected");
        fixture.authorize(true, false);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::inaccessible_dobj_header,
            "inaccessible model-table field is rejected");
    }
    {
        ViewmodelFixture fixture;
        fixture.set_model_count(1);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::invalid_model_count,
            "single-model DObj is rejected");
        fixture.set_model_count(33);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::invalid_model_count,
            "implausible model count is rejected");
    }
    {
        ViewmodelFixture fixture;
        fixture.set_total_bones(0);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::invalid_total_bone_count,
            "zero total bones are rejected");
        fixture.set_total_bones(129);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::invalid_total_bone_count,
            "bone count beyond the part-bit capacity is rejected");
    }
    {
        ViewmodelFixture fixture;
        fixture.set_model_table(nullptr);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::null_model_table,
            "null model table is rejected");
    }
    {
        ViewmodelFixture fixture;
        fixture.authorize(true, true, false);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::inaccessible_model_table,
            "inaccessible model table is rejected");
    }
    {
        ViewmodelFixture fixture;
        fixture.set_models(nullptr, fixture.weapon_model.data());
        check_status(
            fixture.apply(), ViewmodelFilterStatus::null_model,
            "null hand model is rejected");
        fixture.set_models(fixture.hand_model.data(), nullptr);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::null_model,
            "null weapon model is rejected");
    }
    {
        ViewmodelFixture fixture;
        fixture.authorize(true, true, true, false);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::inaccessible_model,
            "inaccessible hand model is rejected");
        fixture.authorize(true, true, true, true, false);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::inaccessible_model,
            "inaccessible weapon model is rejected");
    }
    {
        ViewmodelFixture fixture(0, 1, 1);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::invalid_model_bone_counts,
            "zero hand bones are rejected");
    }
    {
        ViewmodelFixture fixture(1, 0, 1);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::invalid_model_bone_counts,
            "zero weapon bones are rejected");
    }
    {
        ViewmodelFixture fixture(2, 1, 2);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::invalid_model_bone_counts,
            "hand range consuming all DObj bones is rejected");
    }
    {
        ViewmodelFixture fixture(2, 2, 3);
        check_status(
            fixture.apply(), ViewmodelFilterStatus::invalid_model_bone_counts,
            "model bone sum beyond DObj total is rejected");
    }
    {
        ViewmodelFixture fixture;
        fixture.authorize(true, true, true, true, true, false);
        check_status(
            fixture.apply(),
            ViewmodelFilterStatus::inaccessible_hide_part_bits,
            "unwritable hide-part bits are rejected");
        check(fixture.hide_bits() ==
                  std::array<std::uint32_t, kT4DObjPartBitWordCount>{},
              "rejected DObj remains unmodified");
    }
}

void test_component_visibility_survives_repeated_reload_and_model_switch() {
    // Exercise additional attachment models as well as the first weapon.
    // The filter owns only the leading hands; every later component and tail
    // bit must remain exactly native across repeated independent eye passes.
    for (const auto hand_count : std::array<std::uint8_t, 4>{12, 33, 70, 105}) {
        constexpr std::uint8_t weapon_count = 11;
        constexpr std::uint8_t accessory_count = 8;
        ViewmodelFixture fixture(hand_count, weapon_count,
            static_cast<std::uint8_t>(hand_count + weapon_count + accessory_count));
        std::array<std::byte, kT4XModelNumBonesOffset + 1> alternate_weapon{};
        alternate_weapon[kT4XModelNumBonesOffset] = std::byte{weapon_count};
        std::array<void*, 4> models{};
        std::uint32_t random = 0xC0FFEE01U;
        for (unsigned frame = 0; frame < 1'024; ++frame) {
            models = {fixture.hand_model.data(),
                (frame & 1U) ? alternate_weapon.data() : fixture.weapon_model.data(),
                fixture.weapon_model.data(), alternate_weapon.data()};
            fixture.set_model_count(static_cast<std::uint8_t>(models.size()));
            fixture.set_model_table(models.data());
            fixture.authorize();
            allow_range(models.data(), sizeof(models));
            allow_range(alternate_weapon.data(), alternate_weapon.size());
            std::array<std::uint32_t, kT4DObjPartBitWordCount> native{};
            for (auto& word : native) {
                random = random * 1'664'525U + 1'013'904'223U;
                word = random;
            }
            // A native detached-magazine bit toggles during reload. Its
            // restoration is just as important as preserving it when hidden.
            const auto magazine_bone = static_cast<unsigned>(hand_count) + 1U;
            const auto magazine_mask = 0x80000000U >> (magazine_bone & 31U);
            if (frame % 4 < 2) native[magazine_bone >> 5U] |= magazine_mask;
            else native[magazine_bone >> 5U] &= ~magazine_mask;
            fixture.set_hide_bits(native);
            const auto before_dobj = fixture.dobj;
            const auto before_models = models;
            const auto before_hand = fixture.hand_model;
            const auto before_weapon = fixture.weapon_model;
            const auto before_alternate = alternate_weapon;
            const auto status = fixture.apply();
            check(status == ViewmodelFilterStatus::applied ||
                      status == ViewmodelFilterStatus::already_hidden,
                  "each switched native model state is accepted independently");
            const auto first_eye = fixture.hide_bits();
            check_status(fixture.apply(), ViewmodelFilterStatus::already_hidden,
                         "reversed or repeated eye consumption is idempotent");
            check(fixture.hide_bits() == first_eye,
                  "second eye cannot acquire stale component visibility");
            for (unsigned bone = 0; bone < kT4DObjMaximumBones; ++bone) {
                const auto mask = 0x80000000U >> (bone & 31U);
                check((first_eye[bone >> 5U] & mask) ==
                          (bone < hand_count ? mask : native[bone >> 5U] & mask),
                      "only hand-owned bits change, including attached models and unused tail");
            }
            for (std::size_t offset = 0; offset < fixture.dobj.size(); ++offset) {
                if (offset >= kT4DObjHidePartBitsOffset &&
                    offset < kT4DObjHidePartBitsOffset +
                        kT4DObjPartBitWordCount * sizeof(std::uint32_t)) continue;
                check(fixture.dobj[offset] == before_dobj[offset],
                      "filter never modifies model identity, skeleton, or material-related headers");
            }
            check(models == before_models && fixture.hand_model == before_hand &&
                      fixture.weapon_model == before_weapon &&
                      alternate_weapon == before_alternate,
                  "filter cannot rewrite any model or component metadata");
        }
    }
}

}  // namespace

int main() {
    static_assert(sizeof(void*) == sizeof(std::uint32_t));
    test_hand_bone_mask_boundaries();
    test_native_hide_bits_are_preserved();
    test_weapon_bone_range_is_not_hidden();
    test_all_weapon_bones_and_native_component_states_are_preserved();
    test_component_visibility_survives_repeated_reload_and_model_switch();
    test_invalid_memory_and_layouts_fail_closed();
    return 0;
}
