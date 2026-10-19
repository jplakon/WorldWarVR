// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sight_audit_logic.hpp"
#include "t4/bindings.hpp"
#include <string_view>

namespace wawvr::mod {
struct T4SceneView;
// Explicit diagnostic opt-in only. These observers never write game memory,
// alter a weapon profile or call an engine setter/trace.
[[nodiscard]] bool initialize_sight_audit(const wawvr::t4::ValidatedBindings&) noexcept;
[[nodiscard]] bool sight_audit_enabled() noexcept;
void shutdown_sight_audit() noexcept;
void invalidate_rendered_sight_audit() noexcept;
void publish_rendered_sight_audit(const SightAuditMetadata&) noexcept;
void observe_rendered_sight_audit_after_scene(std::uint64_t frame,std::uint32_t view) noexcept;
void observe_sight_pixel_after_scene(std::uint64_t frame,std::uint32_t eye,
    const T4SceneView&,std::uint32_t source_width,std::uint32_t source_height,
    wawvr::xr::StereoSourceLayout*) noexcept;
void clear_sight_audit_discharge_source() noexcept;
void arm_sight_audit_discharge_source(const SightAuditFireRequest&) noexcept;
void log_sight_audit_discharge(std::uint32_t sequence,std::string_view name,
    const SightAuditFireRequest&) noexcept;
} // namespace wawvr::mod
