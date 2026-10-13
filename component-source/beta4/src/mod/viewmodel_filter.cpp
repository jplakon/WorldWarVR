// SPDX-License-Identifier: GPL-3.0-only
#include "viewmodel_filter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace wawvr::mod {

namespace {

static_assert(sizeof(void*) == sizeof(std::uint32_t));

[[nodiscard]] void* offset_address(
    void* const base,
    const std::size_t offset) noexcept {
    const auto base_value = reinterpret_cast<std::uintptr_t>(base);
    if (base_value > std::numeric_limits<std::uintptr_t>::max() - offset) {
        return nullptr;
    }
    return reinterpret_cast<void*>(base_value + offset);
}

[[nodiscard]] const void* offset_address(
    const void* const base,
    const std::size_t offset) noexcept {
    return offset_address(const_cast<void*>(base), offset);
}

}  // namespace

ViewmodelFilterStatus hide_t4_viewmodel_hand_surfaces(
    void* const viewmodel_dobj,
    const ViewmodelMemoryRangeValidator validate_range) noexcept {
    if (viewmodel_dobj == nullptr) {
        return ViewmodelFilterStatus::null_dobj;
    }
    if (validate_range == nullptr) {
        return ViewmodelFilterStatus::null_validator;
    }

    const void* const model_count_address =
        offset_address(viewmodel_dobj, kT4DObjNumModelsOffset);
    const void* const model_table_field =
        offset_address(viewmodel_dobj, kT4DObjModelsOffset);
    if (model_count_address == nullptr || model_table_field == nullptr ||
        !validate_range(model_count_address, 2, false) ||
        !validate_range(model_table_field, sizeof(void*), false)) {
        return ViewmodelFilterStatus::inaccessible_dobj_header;
    }

    std::uint8_t model_count = 0;
    std::uint8_t total_bone_count = 0;
    void* model_table = nullptr;
    std::memcpy(&model_count, model_count_address, sizeof(model_count));
    std::memcpy(
        &total_bone_count,
        offset_address(viewmodel_dobj, kT4DObjNumBonesOffset),
        sizeof(total_bone_count));
    std::memcpy(&model_table, model_table_field, sizeof(model_table));

    if (model_count < 2 || model_count > 32) {
        return ViewmodelFilterStatus::invalid_model_count;
    }
    if (total_bone_count == 0 ||
        total_bone_count > kT4DObjMaximumBones) {
        return ViewmodelFilterStatus::invalid_total_bone_count;
    }
    if (model_table == nullptr) {
        return ViewmodelFilterStatus::null_model_table;
    }
    if (!validate_range(
            model_table, static_cast<std::size_t>(model_count) * sizeof(void*),
            false)) {
        return ViewmodelFilterStatus::inaccessible_model_table;
    }

    std::array<void*, 2> models{};
    std::memcpy(models.data(), model_table, sizeof(models));
    if (models[0] == nullptr || models[1] == nullptr) {
        return ViewmodelFilterStatus::null_model;
    }

    for (const void* const model : models) {
        if (!validate_range(
                model, kT4XModelNumBonesOffset + sizeof(std::uint8_t),
                false)) {
            return ViewmodelFilterStatus::inaccessible_model;
        }
    }

    std::uint8_t hand_bone_count = 0;
    std::uint8_t weapon_bone_count = 0;
    std::memcpy(
        &hand_bone_count,
        offset_address(models[0], kT4XModelNumBonesOffset),
        sizeof(hand_bone_count));
    std::memcpy(
        &weapon_bone_count,
        offset_address(models[1], kT4XModelNumBonesOffset),
        sizeof(weapon_bone_count));
    if (hand_bone_count == 0 || weapon_bone_count == 0 ||
        hand_bone_count >= total_bone_count ||
        static_cast<std::size_t>(hand_bone_count) + weapon_bone_count >
            total_bone_count) {
        return ViewmodelFilterStatus::invalid_model_bone_counts;
    }

    void* const hide_part_bits_address =
        offset_address(viewmodel_dobj, kT4DObjHidePartBitsOffset);
    constexpr std::size_t kHidePartBitsSize =
        kT4DObjPartBitWordCount * sizeof(std::uint32_t);
    if (hide_part_bits_address == nullptr ||
        !validate_range(hide_part_bits_address, kHidePartBitsSize, true)) {
        return ViewmodelFilterStatus::inaccessible_hide_part_bits;
    }

    std::array<std::uint32_t, kT4DObjPartBitWordCount> hide_part_bits{};
    std::memcpy(
        hide_part_bits.data(), hide_part_bits_address,
        sizeof(hide_part_bits));
    const auto native_hide_part_bits = hide_part_bits;
    for (std::size_t bone_index = 0; bone_index < hand_bone_count;
         ++bone_index) {
        hide_part_bits[bone_index >> 5U] |=
            0x80000000U >> (bone_index & 31U);
    }

    if (hide_part_bits == native_hide_part_bits) {
        return ViewmodelFilterStatus::already_hidden;
    }

    std::memcpy(
        hide_part_bits_address, hide_part_bits.data(),
        sizeof(hide_part_bits));
    return ViewmodelFilterStatus::applied;
}

const char* viewmodel_filter_status_name(
    const ViewmodelFilterStatus status) noexcept {
    switch (status) {
        case ViewmodelFilterStatus::applied:
            return "applied";
        case ViewmodelFilterStatus::already_hidden:
            return "already_hidden";
        case ViewmodelFilterStatus::null_dobj:
            return "null_dobj";
        case ViewmodelFilterStatus::null_validator:
            return "null_validator";
        case ViewmodelFilterStatus::inaccessible_dobj_header:
            return "inaccessible_dobj_header";
        case ViewmodelFilterStatus::invalid_model_count:
            return "invalid_model_count";
        case ViewmodelFilterStatus::invalid_total_bone_count:
            return "invalid_total_bone_count";
        case ViewmodelFilterStatus::null_model_table:
            return "null_model_table";
        case ViewmodelFilterStatus::inaccessible_model_table:
            return "inaccessible_model_table";
        case ViewmodelFilterStatus::null_model:
            return "null_model";
        case ViewmodelFilterStatus::inaccessible_model:
            return "inaccessible_model";
        case ViewmodelFilterStatus::invalid_model_bone_counts:
            return "invalid_model_bone_counts";
        case ViewmodelFilterStatus::inaccessible_hide_part_bits:
            return "inaccessible_hide_part_bits";
    }
    return "unknown";
}

}  // namespace wawvr::mod
