// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>

namespace wawvr::mod {

// These offsets are specific to the pinned 32-bit T4 SP 1.7.1263 profile.
// The installation path independently validates the instructions that consume
// them before enabling the viewmodel hook.
inline constexpr std::size_t kT4DObjNumModelsOffset = 0x09;
inline constexpr std::size_t kT4DObjNumBonesOffset = 0x0A;
inline constexpr std::size_t kT4DObjHidePartBitsOffset = 0x50;
inline constexpr std::size_t kT4DObjModelsOffset = 0x64;
inline constexpr std::size_t kT4XModelNumBonesOffset = 0x04;
inline constexpr std::size_t kT4DObjPartBitWordCount = 4;
inline constexpr std::size_t kT4DObjMaximumBones =
    kT4DObjPartBitWordCount * 32;

using ViewmodelMemoryRangeValidator = bool (*)(
    const void* address, std::size_t size, bool writable) noexcept;

enum class ViewmodelFilterStatus : std::uint8_t {
    applied,
    already_hidden,
    null_dobj,
    null_validator,
    inaccessible_dobj_header,
    invalid_model_count,
    invalid_total_bone_count,
    null_model_table,
    inaccessible_model_table,
    null_model,
    inaccessible_model,
    invalid_model_bone_counts,
    inaccessible_hide_part_bits,
};

// T4 viewmodels keep the animated hand/arm skeleton as model zero and attach
// the gun as model one at tag_weapon. Hiding model zero's bone range removes
// only its submitted surfaces; the skeleton and weapon attachment remain live.
[[nodiscard]] ViewmodelFilterStatus hide_t4_viewmodel_hand_surfaces(
    void* viewmodel_dobj,
    ViewmodelMemoryRangeValidator validate_range) noexcept;

[[nodiscard]] const char* viewmodel_filter_status_name(
    ViewmodelFilterStatus status) noexcept;

}  // namespace wawvr::mod
