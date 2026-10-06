// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <span>

namespace wawvr::mod {

// The caller owns this storage, or has freshly validated the complete native
// span. No native callback may intervene between validation and this copy.
// Nothing is cached: sample all current matrices after all indices pass.
template <typename Matrix>
[[nodiscard]] bool copy_controlled_action_matrices(
    const std::span<const Matrix> matrices,
    const std::size_t root_bone,
    const std::size_t parent_bone,
    const std::size_t action_bone,
    Matrix* const root,
    Matrix* const parent,
    Matrix* const action) noexcept {
    if (matrices.data() == nullptr || root == nullptr || parent == nullptr ||
        action == nullptr || root_bone >= 0xFE || parent_bone >= 0xFE ||
        action_bone >= 0xFE || root_bone >= matrices.size() ||
        parent_bone >= matrices.size() || action_bone >= matrices.size()) {
        return false;
    }
    // Stage before writing so output aliasing cannot change a later input.
    const Matrix root_copy = matrices[root_bone];
    const Matrix parent_copy = matrices[parent_bone];
    const Matrix action_copy = matrices[action_bone];
    *root = root_copy;
    *parent = parent_copy;
    *action = action_copy;
    return true;
}

}  // namespace wawvr::mod
