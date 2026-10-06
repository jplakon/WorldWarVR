// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

namespace wawvr::mod {

// Unlike MP, Steam SP 1.7 maps can populate entries above 0x3FF (Der Riese
// reaches at least 0x425), so no fixed ID in this conservatively verified
// range is globally reserved for the mod. Lease a currently empty high entry
// immediately before each synthetic R_AddDObjToScene call. IDs above 0x7FD
// remain deliberately untouched; this bound does not define the engine's full
// entity capacity or its WORLD/NONE sentinels.
inline constexpr std::uint32_t kSyntheticSceneFirstLeaseCandidate = 0x400;
inline constexpr std::uint32_t kSyntheticSceneLastLeaseCandidate = 0x7FD;
inline constexpr std::uint32_t kNoSyntheticSceneLease = 0xFFFFFFFFU;
// Private DObjs are built into mod-owned storage rather than the engine's
// client-DObj handle table. Match T4's model previewer and give DObjCreate a
// neutral internal entnum; the leased renderer ID is supplied only at submit.
inline constexpr std::uint32_t kSyntheticDObjCreationEntity = 0;

// These globals hold pointers to the renderer's uint16 scene-index tables.
// They are RVAs in the exact Steam SP 1.7 executable fingerprint validated by
// the two runtime installers before this helper is called.
inline constexpr std::uint32_t kSceneXModelIndexPointerRva = 0x039A8D10;
inline constexpr std::uint32_t kSceneDObjIndexPointerRva = 0x039A8D14;
inline constexpr std::uint16_t kEmptySyntheticSceneIndex = 0xFFFF;

struct SyntheticSceneIndexPointers final {
    std::uintptr_t xmodel_index_pointer{};
    std::uintptr_t dobj_index_pointer{};
};

[[nodiscard]] bool synthetic_scene_slot_available(
    const SyntheticSceneIndexPointers& pointers,
    std::uint32_t entity_number) noexcept;

[[nodiscard]] bool find_available_synthetic_scene_slot(
    const SyntheticSceneIndexPointers& pointers,
    std::uint32_t* entity_number) noexcept;

static_assert(
    kSyntheticSceneFirstLeaseCandidate <
    kSyntheticSceneLastLeaseCandidate);
static_assert(kSyntheticSceneLastLeaseCandidate < kNoSyntheticSceneLease);

}  // namespace wawvr::mod
