// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "weapon_ballistics_logic.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace wawvr::mod {
struct CalibratedRifleReader final {
    void* context{};
    bool (*read)(void*,std::uintptr_t,void*,std::size_t) noexcept{};
};
struct CalibratedRifleRequest final {
    std::uint32_t definition{},dobj{};
    std::string_view weapon{};
    wawvr::xr::Basis3f evaluated_flash_basis{};
};
struct CalibratedRifleProof final {
    CalibratedRifleAsset asset{};
    std::uint64_t witness_fingerprint{};
    wawvr::xr::Vec3f rear_reference{},front_reference{},local_direction{};
};
enum class CalibratedRifleStatus : std::uint8_t {
    accepted,not_applicable,inaccessible,identity,binding,geometry,unstable,invalid_axis,
};
// Reads only a small pinned native sight component/triangle/bind witness,
// twice per call. No cache, allocation, evaluator call or game-memory write.
// Failure leaves both caller-owned outputs untouched; the caller retains its
// accepted prior basis atomically. Muzzle position is not an input or output.
[[nodiscard]] CalibratedRifleStatus derive_calibrated_rifle_basis(
    const CalibratedRifleReader&,const CalibratedRifleRequest&,
    wawvr::xr::Basis3f*,CalibratedRifleProof* = nullptr) noexcept;
} // namespace wawvr::mod
