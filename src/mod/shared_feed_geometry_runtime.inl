// SPDX-License-Identifier: GPL-3.0-only
// Included inside manual_reload_runtime's exact-SP private namespace after its
// validated detached-asset builder and scene-submission helpers.
//
// Retail culls a complete XSurface if ANY of its part bits is hidden. A clip
// can share that surface with unrelated rigid weapon pieces. Keep the native
// owned clip hide, but submit the exact complement as immutable private meshes.
// This does not modify native models, surfaces, materials, indices or renderer
// code. Detached magazine ranges are never part of the complement.
inline constexpr std::size_t kMaximumSharedFeedSurfaces = 64;
inline constexpr std::size_t kMaximumSharedFeedPieces = 60;
inline constexpr std::size_t kMaximumFeedSceneBatch =
    kMaximumSharedFeedPieces + 1;

struct SharedFeedPieceDescription final {
    DetachableMagazineMeshRecipe mesh{};
    std::uint16_t tag{};
};

struct SharedFeedSurfaceWitness final {
    std::uint8_t index{};
    XSurface surface{};
};

struct SharedFeedDescription final {
    std::array<SharedFeedPieceDescription, kMaximumSharedFeedPieces> pieces{};
    std::size_t piece_count{};
    std::array<SharedFeedSurfaceWitness, kMaximumSharedFeedSurfaces> surfaces{};
    std::size_t surface_count{};
    std::uint64_t fingerprint{14695981039346656037ULL};
};

struct SharedFeedCompensationPiece final {
    std::uint8_t source_surface{};
    std::uint8_t local_bone{};
    std::uint16_t tag{};
    DObjAnimMat bind{};
    std::uint32_t retained_vertices{};
    std::uint32_t retained_triangles{};
    MagazineAsset asset{};
};

struct SharedFeedCompensationCache final {
    DetachableMagazineWeaponProfileId profile_id{};
    XModel* source{};
    XSurface* surfaces{};
    void** materials{};
    DObjAnimMat* base_matrices{};
    std::uint16_t* bone_names{};
    std::uint8_t bone_count{};
    std::uint8_t surface_count{};
    std::uint64_t magazine_fingerprint{};
    std::uint64_t fingerprint{};
    std::array<SharedFeedSurfaceWitness, kMaximumSharedFeedSurfaces> witnesses{};
    std::size_t witness_count{};
    // Address stability is required after DObjCreate/scene submission, even
    // when a later piece fails validation. No submitted object is destroyed.
    std::deque<SharedFeedCompensationPiece> pieces{};
    bool complete{};
};

std::deque<SharedFeedCompensationCache> g_shared_feed_compensation_cache{};

struct FeedSceneBatchItem final {
    MagazineAsset* asset{};
    std::uint32_t entity{kNoSyntheticSceneLease};
};

struct FeedSceneBatch final {
    std::array<FeedSceneBatchItem, kMaximumFeedSceneBatch> items{};
    std::size_t count{};
    float light[3]{};
    std::size_t shared_surface_count{};
    std::size_t companion_count{};
    std::size_t other_native_hidden_surfaces{};
    std::uint32_t retained_vertices{};
    std::uint32_t retained_triangles{};
    bool prior_native_clip_hidden{};
};

[[nodiscard]] bool shared_feed_diagnostics_enabled() noexcept {
    static const bool enabled = []() noexcept {
        std::array<wchar_t, 2> value{};
        const bool gameplay = GetEnvironmentVariableW(L"WAWVR_GAMEPLAY_DIAGNOSTICS",
            value.data(), static_cast<DWORD>(value.size())) == 1 &&
            value[0] == L'1';
        return gameplay || simulator_detachable_magazine_probe_enabled();
    }();
    return enabled;
}

[[nodiscard]] bool inspect_shared_feed_description(
    const MagazineSource& source,
    SharedFeedDescription* const output) noexcept {
    if (output == nullptr || source.model == nullptr ||
        !accessible_range(source.model, sizeof(*source.model), false)) {
        return false;
    }
    *output = {};
    const XModel& model = *source.model;
    if (model.bone_count == 0 || model.bone_count > 128 ||
        model.surface_count == 0 ||
        model.surface_count > kMaximumSharedFeedSurfaces ||
        source.local_bone >= model.bone_count ||
        static_cast<std::size_t>(source.root_bone) + model.bone_count > 128 ||
        model.surfaces == nullptr || model.material_handles == nullptr ||
        model.base_matrices == nullptr || model.bone_names == nullptr ||
        !accessible_range(model.surfaces,
            model.surface_count * sizeof(XSurface), false) ||
        !accessible_range(model.material_handles,
            model.surface_count * sizeof(void*), false) ||
        !accessible_range(model.base_matrices,
            model.bone_count * sizeof(DObjAnimMat), false) ||
        !accessible_range(model.bone_names,
            model.bone_count * sizeof(std::uint16_t), false)) {
        return false;
    }
    const auto clip_word = source.local_bone >> 5U;
    const auto clip_mask = 0x80000000U >> (source.local_bone & 31U);
    for (std::size_t surface_index = 0;
         surface_index < model.surface_count; ++surface_index) {
        const XSurface& surface = model.surfaces[surface_index];
        if ((static_cast<std::uint32_t>(surface.part_bits[clip_word]) &
             clip_mask) == 0) continue;
        constexpr std::size_t maximum_ranges = 16;
        if (surface.deformed || surface.rigid_vertex_list_count == 0 ||
            surface.rigid_vertex_list_count > maximum_ranges ||
            surface.rigid_vertex_list == nullptr || surface.vertices == nullptr ||
            surface.triangle_indices == nullptr || surface.index_buffer == nullptr ||
            surface.vertex_count == 0 || surface.vertex_count > 32767 ||
            surface.triangle_count == 0 ||
            !accessible_range(surface.rigid_vertex_list,
                surface.rigid_vertex_list_count * sizeof(RigidVertexList), false) ||
            !accessible_range(surface.vertices,
                surface.vertex_count * sizeof(PackedVertex), false) ||
            !accessible_range(surface.triangle_indices,
                surface.triangle_count * 3U * sizeof(std::uint16_t), false)) {
            return false;
        }
        std::array<gameplay::RigidSubmeshRange, maximum_ranges> ranges{};
        std::array<std::uint32_t, 4> expected_parts{};
        for (std::size_t index = 0;
             index < surface.rigid_vertex_list_count; ++index) {
            const RigidVertexList& rigid = surface.rigid_vertex_list[index];
            const auto bone = static_cast<std::uint16_t>(rigid.bone_offset >> 6U);
            if ((rigid.bone_offset & 0x3FU) != 0 || bone >= model.bone_count)
                return false;
            ranges[index] = {bone, rigid.vertex_count,
                rigid.triangle_offset, rigid.triangle_count};
            expected_parts[bone >> 5U] |= 0x80000000U >> (bone & 31U);
        }
        if (std::memcmp(expected_parts.data(), surface.part_bits,
                        sizeof(expected_parts)) != 0) return false;

        const auto split = plan_rigid_visibility_split(
            surface.vertex_count, surface.triangle_count,
            std::span<const gameplay::RigidSubmeshRange>{ranges.data(),
                surface.rigid_vertex_list_count}, source.local_bone,
            std::span<const std::uint16_t>{surface.triangle_indices,
                surface.triangle_count * 3U});
        if (!split.has_value()) return false;
        // A surface containing only the detachable bone needs no complement.
        if (split->retained_count == 0) continue;
        void* const material = model.material_handles[surface_index];
        const char* material_name = nullptr;
        if (!read_material_name(material, &material_name) ||
            output->surface_count >= output->surfaces.size()) return false;
        output->surfaces[output->surface_count++] = {
            static_cast<std::uint8_t>(surface_index), surface};
        output->fingerprint = append_asset_fingerprint(output->fingerprint,
            &surface, sizeof(surface));
        output->fingerprint = append_asset_fingerprint(output->fingerprint,
            surface.vertices, surface.vertex_count * sizeof(PackedVertex));
        output->fingerprint = append_asset_fingerprint(output->fingerprint,
            surface.triangle_indices,
            surface.triangle_count * 3U * sizeof(std::uint16_t));
        output->fingerprint = append_asset_fingerprint(output->fingerprint,
            surface.rigid_vertex_list,
            surface.rigid_vertex_list_count * sizeof(RigidVertexList));
        output->fingerprint = append_asset_fingerprint(output->fingerprint,
            &material, sizeof(material));
        for (std::size_t index = 0; index < split->retained_count; ++index) {
            if (output->piece_count >= output->pieces.size()) return false;
            const auto& retained = split->retained[index];
            const auto bone = retained.bone_id;
            if (bone >= model.bone_count || model.bone_names[bone] == 0)
                return false;
            const DObjAnimMat& bind = model.base_matrices[bone];
            SharedFeedPieceDescription& piece =
                output->pieces[output->piece_count++];
            piece.tag = model.bone_names[bone];
            piece.mesh = {
                .expected_model_bone_count = model.bone_count,
                .expected_model_surface_count = model.surface_count,
                .magazine_bone_index = static_cast<std::uint8_t>(bone),
                .source_surface_index = static_cast<std::uint8_t>(surface_index),
                .source_surface_rigid_subrange_count =
                    static_cast<std::uint8_t>(surface.rigid_vertex_list_count),
                .source_rigid_subrange_index =
                    static_cast<std::uint8_t>(retained.range_index),
                .source_surface_vertex_count = surface.vertex_count,
                .source_surface_triangle_count = surface.triangle_count,
                .vertex_offset = static_cast<std::uint16_t>(retained.first_vertex),
                .vertex_count = static_cast<std::uint16_t>(retained.vertex_count),
                .triangle_offset = static_cast<std::uint16_t>(retained.triangle_offset),
                .triangle_count = static_cast<std::uint16_t>(retained.triangle_count),
                .bind_pose = {
                    .translation = {bind.translation[0], bind.translation[1],
                                    bind.translation[2]},
                    .orientation = {bind.quaternion[0], bind.quaternion[1],
                                    bind.quaternion[2], bind.quaternion[3]},
                },
            };
            output->fingerprint = append_asset_fingerprint(output->fingerprint,
                &bind, sizeof(bind));
            output->fingerprint = append_asset_fingerprint(output->fingerprint,
                &piece.tag, sizeof(piece.tag));
        }
    }
    return output->fingerprint != 0;
}

[[nodiscard]] bool shared_feed_cache_matches_live(
    const SharedFeedCompensationCache& cached,
    const DetachableMagazineWeaponProfile& profile,
    const MagazineSource& source) noexcept {
    if (!cached.complete || cached.profile_id != profile.id ||
        cached.source != source.model || source.model == nullptr ||
        !accessible_range(source.model, sizeof(*source.model), false) ||
        cached.surfaces != source.model->surfaces ||
        cached.materials != source.model->material_handles ||
        cached.base_matrices != source.model->base_matrices ||
        cached.bone_names != source.model->bone_names ||
        cached.bone_count != source.model->bone_count ||
        cached.surface_count != source.model->surface_count ||
        cached.magazine_fingerprint != source.asset_fingerprint ||
        !accessible_range(cached.surfaces,
            cached.surface_count * sizeof(XSurface), false) ||
        !accessible_range(cached.materials,
            cached.surface_count * sizeof(void*), false) ||
        !accessible_range(cached.base_matrices,
            cached.bone_count * sizeof(DObjAnimMat), false) ||
        !accessible_range(cached.bone_names,
            cached.bone_count * sizeof(std::uint16_t), false)) return false;
    for (std::size_t index = 0; index < cached.witness_count; ++index) {
        const auto& witness = cached.witnesses[index];
        if (witness.index >= cached.surface_count ||
            std::memcmp(cached.surfaces + witness.index, &witness.surface,
                        sizeof(XSurface)) != 0) return false;
    }
    for (const auto& piece : cached.pieces) {
        if (!piece.asset.ready || piece.local_bone >= cached.bone_count ||
            piece.source_surface >= cached.surface_count ||
            cached.materials[piece.source_surface] != piece.asset.materials[0] ||
            cached.bone_names[piece.local_bone] != piece.tag ||
            std::memcmp(cached.base_matrices + piece.local_bone,
                        &piece.bind, sizeof(piece.bind)) != 0) return false;
        XModel** slot = nullptr;
        XModel* model = nullptr;
        if (!read_dobj_model_slot(const_cast<std::uint8_t*>(piece.asset.dobj.data()),
                &slot, &model) || model != &piece.asset.model) return false;
    }
    return true;
}

[[nodiscard]] SharedFeedCompensationCache* find_or_build_shared_feed_cache(
    const DetachableMagazineWeaponProfile& profile,
    const MagazineSource& source) {
    SharedFeedDescription description{};
    if (!inspect_shared_feed_description(source, &description)) return nullptr;
    for (auto& cached : g_shared_feed_compensation_cache) {
        if (cached.profile_id == profile.id && cached.source == source.model &&
            cached.fingerprint == description.fingerprint) {
            // A failed native DObjCreate may still own engine storage. Never
            // recreate or overwrite it on repeated fail-closed frames.
            return shared_feed_cache_matches_live(cached, profile, source)
                ? &cached : nullptr;
        }
    }
    g_shared_feed_compensation_cache.emplace_back();
    auto& cached = g_shared_feed_compensation_cache.back();
    cached.profile_id = profile.id;
    cached.source = source.model;
    cached.surfaces = source.model->surfaces;
    cached.materials = source.model->material_handles;
    cached.base_matrices = source.model->base_matrices;
    cached.bone_names = source.model->bone_names;
    cached.bone_count = source.model->bone_count;
    cached.surface_count = source.model->surface_count;
    cached.magazine_fingerprint = source.asset_fingerprint;
    cached.fingerprint = description.fingerprint;
    cached.witnesses = description.surfaces;
    cached.witness_count = description.surface_count;
    for (std::size_t index = 0; index < description.piece_count; ++index) {
        const auto& description_piece = description.pieces[index];
        cached.pieces.emplace_back();
        auto& piece = cached.pieces.back();
        piece.source_surface = description_piece.mesh.source_surface_index;
        piece.local_bone = description_piece.mesh.magazine_bone_index;
        piece.tag = description_piece.tag;
        piece.bind = source.model->base_matrices[piece.local_bone];
        piece.retained_vertices = description_piece.mesh.vertex_count;
        piece.retained_triangles = description_piece.mesh.triangle_count;
        MagazineSource retained = source;
        retained.local_bone = piece.local_bone;
        retained.global_bone = static_cast<std::uint8_t>(source.root_bone + piece.local_bone);
        retained.bone_tag = piece.tag;
        retained.surface = piece.source_surface;
        retained.rigid_range = description_piece.mesh.source_rigid_subrange_index;
        retained.piece_count = 1;
        retained.piece_surfaces = {};
        retained.piece_rigid_ranges = {};
        retained.piece_surfaces[0] = retained.surface;
        retained.piece_rigid_ranges[0] = retained.rigid_range;
        DetachableMagazineWeaponProfile private_profile = profile;
        private_profile.mesh = description_piece.mesh;
        const char* material_name = nullptr;
        if (!read_material_name(source.model->material_handles[piece.source_surface],
                                &material_name)) return nullptr;
        private_profile.magazine_material_name = material_name;
        if (!build_magazine_asset(retained, private_profile, &piece.asset))
            return nullptr;
    }
    cached.complete = true;
    if (shared_feed_diagnostics_enabled()) {
        stereo_diagnostic_log(
            "VisibilityDiag exact %s shared clip surfaces preserved by %zu immutable private rigid piece(s); original models/materials unchanged",
            profile.diagnostic_name, cached.pieces.size());
    }
    return &cached;
}

[[nodiscard]] bool append_feed_scene_item(
    FeedSceneBatch* const batch,
    MagazineAsset* const asset,
    const wawvr::xr::EnginePose& world,
    const wawvr::xr::Vec3f& lighting_origin,
    const void* const viewmodel_pose) noexcept {
    if (batch == nullptr || batch->count >= batch->items.size() ||
        asset == nullptr || !asset->ready ||
        !finite_vector(world.position) || !valid_basis(world.axis) ||
        !finite_vector(lighting_origin) ||
        !accessible_range(viewmodel_pose, kPoseSize, false)) return false;
    float angles[3]{};
    if (!axis_to_angles(world.axis, angles)) return false;
    std::memcpy(asset->pose.data(), viewmodel_pose, kPoseSize);
    const float origin[3]{world.position.x, world.position.y, world.position.z};
    std::memcpy(asset->pose.data() + kPoseOriginOffset, origin, sizeof(origin));
    std::memcpy(asset->pose.data() + kPoseAnglesOffset, angles, sizeof(angles));
    batch->light[0] = lighting_origin.x;
    batch->light[1] = lighting_origin.y;
    batch->light[2] = lighting_origin.z;
    batch->items[batch->count++] = {asset, kNoSyntheticSceneLease};
    return true;
}

[[nodiscard]] bool read_shared_feed_piece_world_pose(
    const DetachableMagazineWeaponProfile& profile,
    const MagazineSource& source,
    const SharedFeedCompensationPiece& piece,
    void* const dobj,
    const void* const viewmodel_pose,
    wawvr::xr::EnginePose* const world) noexcept {
    const auto global_bone = source.root_bone + piece.local_bone;
    const bool controlled_door = profile.feed_door.enabled &&
        global_bone == source.feed_door.bone;
    const bool controlled_handle = profile.charging.enabled &&
        global_bone == source.charging_handle.bone &&
        (profile.charging.suppress_native_pose_always ||
         g_magazine_charging_state.charge_required ||
         g_magazine_charging_state.handle_grabbed ||
         g_magazine_charging_state.spring_returning ||
         g_magazine_charging_state.awaiting_controls_release ||
         g_magazine_charging_state.handle_fraction > 0.0F);
    if (!controlled_door && !controlled_handle)
        return read_viewmodel_world_tag_pose(dobj, piece.tag, viewmodel_pose, world);
    if (!accessible_range(dobj, kDObjSize, false)) return false;
    const auto* const bytes = static_cast<const std::uint8_t*>(dobj);
    const auto bone_count = bytes[kDObjNumBonesOffset];
    DObjAnimMat* live = nullptr;
    std::memcpy(&live, bytes + kDObjSkelMatOffset, sizeof(live));
    if (bone_count == 0 || bone_count > g_pre_skin_scratch.matrices.size() ||
        live == nullptr || !accessible_range(live,
            bone_count * sizeof(DObjAnimMat), false)) return false;
    std::memcpy(g_pre_skin_scratch.dobj.data(), dobj, kDObjSize);
    std::memcpy(g_pre_skin_scratch.matrices.data(), live,
                bone_count * sizeof(DObjAnimMat));
    auto* const matrices = g_pre_skin_scratch.matrices.data();
    std::memcpy(g_pre_skin_scratch.dobj.data() + kDObjSkelMatOffset,
                &matrices, sizeof(matrices));
    if (controlled_door) {
        const auto stage = g_controller_state.reload.stage;
        if (!apply_magazine_feed_door_pose({matrices, bone_count}, profile,
                source, stage != gameplay::ReloadStage::Ready &&
                    stage != gameplay::ReloadStage::Committing)) return false;
    } else if (!g_magazine_visual_state.closed_charging_handle_pose_latched ||
        !apply_charging_handle_pose({matrices, bone_count}, profile, source,
            g_magazine_charging_state.handle_fraction,
            g_magazine_visual_state.closed_charging_handle_relative)) return false;
    return read_viewmodel_world_tag_pose(g_pre_skin_scratch.dobj.data(),
        piece.tag, viewmodel_pose, world);
}

[[nodiscard]] bool prepare_shared_feed_scene(
    const DetachableMagazineWeaponProfile& profile,
    const MagazineSource& source,
    void* const dobj,
    const void* const viewmodel_pose,
    const wawvr::xr::Vec3f& lighting_origin,
    const bool hide_requested,
    FeedSceneBatch* const batch) {
    auto& visual = g_magazine_visual_state;
    if (batch == nullptr) return false;
    if (visual.shared_feed_compensation != nullptr) {
        batch->shared_surface_count =
            visual.shared_feed_compensation->witness_count;
    }
    if (!hide_requested) return true;
    if (source.global_bone >= 128 ||
        !accessible_range(dobj, kDObjSize, false)) return false;
    std::array<std::uint32_t, 4> native_hidden{};
    std::memcpy(native_hidden.data(), static_cast<const std::uint8_t*>(dobj) +
        kT4DObjHidePartBitsOffset, sizeof(native_hidden));
    const auto clip_word = source.global_bone >> 5U;
    const auto clip_mask = 0x80000000U >> (source.global_bone & 31U);
    const bool native_clip_hidden = visual.authored_hidden
        ? visual.live_was_hidden : (native_hidden[clip_word] & clip_mask) != 0;
    batch->prior_native_clip_hidden = native_clip_hidden;
    // Existing native visibility remains authoritative; do not resurrect a
    // surface which the engine had already intentionally hidden.
    if (native_clip_hidden) return true;
    native_hidden[clip_word] |= clip_mask; // prospective owned hide, before submit
    auto* cached = visual.shared_feed_compensation;
    if (cached == nullptr) cached = find_or_build_shared_feed_cache(profile, source);
    if (cached == nullptr || !shared_feed_cache_matches_live(*cached, profile, source))
        return false;
    visual.shared_feed_compensation = cached;
    batch->shared_surface_count = cached->witness_count;
    std::array<bool, kMaximumSharedFeedSurfaces> suppressed{};
    for (auto& piece : cached->pieces) {
        const XSurface& original = source.model->surfaces[piece.source_surface];
        std::array<std::uint32_t, 4> global_surface_parts{};
        for (std::size_t local_bone = 0;
             local_bone < source.model->bone_count; ++local_bone) {
            if ((static_cast<std::uint32_t>(original.part_bits[local_bone >> 5U]) &
                (0x80000000U >> (local_bone & 31U))) == 0) continue;
            const auto global_bone = source.root_bone + local_bone;
            global_surface_parts[global_bone >> 5U] |=
                0x80000000U >> (global_bone & 31U);
        }
        if (!should_render_owned_rigid_complement(hide_requested,
                !native_clip_hidden, source.global_bone,
                global_surface_parts, native_hidden)) {
            if (!suppressed[piece.source_surface]) {
                suppressed[piece.source_surface] = true;
                ++batch->other_native_hidden_surfaces;
            }
            continue;
        }
        wawvr::xr::EnginePose world{};
        if (!read_shared_feed_piece_world_pose(profile, source, piece, dobj,
                viewmodel_pose, &world) ||
            !append_feed_scene_item(batch, &piece.asset, world, lighting_origin,
                                    viewmodel_pose)) return false;
        ++batch->companion_count;
        batch->retained_vertices += piece.retained_vertices;
        batch->retained_triangles += piece.retained_triangles;
    }
    return true;
}

[[nodiscard]] bool preflight_feed_scene_leases(FeedSceneBatch* const batch) noexcept {
    if (batch == nullptr || batch->count > batch->items.size()) return false;
    std::size_t next = 0;
    for (std::uint32_t candidate = kSyntheticSceneLastLeaseCandidate;
         next < batch->count; --candidate) {
        if (synthetic_scene_slot_available(g_scene_index_pointers, candidate))
            batch->items[next++].entity = candidate;
        if (candidate == kSyntheticSceneFirstLeaseCandidate && next < batch->count)
            return false;
    }
    return true;
}

void submit_feed_scene_batch(FeedSceneBatch& batch) noexcept {
    // All poses, immutable meshes, native identity and distinct leases were
    // preflighted before visibility changed. No fallible branch follows the
    // first submission, so a partial batch cannot be followed by restoring the
    // complete authored surface and creating duplicate geometry this frame.
    for (std::size_t index = 0; index < batch.count; ++index) {
        auto& item = batch.items[index];
        call_add_dobj_to_scene(item.asset->pose.data(), item.asset->dobj.data(),
            item.entity, batch.light, kClipRenderFlags);
    }
}

void log_shared_feed_scene_diagnostic(
    const DetachableMagazineWeaponProfile& profile,
    const MagazineSource& source,
    const ManualReloadViewmodelContext& context,
    const bool hide_requested,
    const FeedSceneBatch& batch) noexcept {
    if (!shared_feed_diagnostics_enabled()) return;
    static std::array<std::uint64_t, 9> previous{};
    static std::uint64_t last_log{};
    const auto now = GetTickCount64();
    const std::array<std::uint64_t, 9> key{
        static_cast<std::uint64_t>(profile.id),
        reinterpret_cast<std::uintptr_t>(g_magazine_visual_state.viewmodel_dobj),
        hide_requested ? 1ULL : 0ULL,
        batch.prior_native_clip_hidden ? 1ULL : 0ULL,
        batch.shared_surface_count, batch.companion_count,
        batch.other_native_hidden_surfaces, batch.retained_vertices,
        batch.retained_triangles};
    if (key == previous && last_log != 0 && now >= last_log && now - last_log < 1000)
        return;
    previous = key;
    last_log = now;
    stereo_diagnostic_log(
        "VisibilityDiag shared-feed profile=%s weapon=%d frame=%llu sequence=%llu dobj=%p model=%p sharedSurfaces=%zu baseCulledByOwnedClip=%d ownHide=%d priorNativeClipHide=%d nativeOtherHiddenSurfaces=%zu companionPieces=%zu keptVertices=%u keptTriangles=%u submitted=%zu stereoGeometry=shared camera=(%.4f,%.4f,%.4f) nativeModelsUnchanged=1",
        profile.internal_weapon_name, context.weapon_index,
        static_cast<unsigned long long>(context.controller.frame.frame_id),
        static_cast<unsigned long long>(context.controller.frame.actions.sequence),
        g_magazine_visual_state.viewmodel_dobj, source.model,
        batch.shared_surface_count,
        hide_requested && !batch.prior_native_clip_hidden &&
            batch.shared_surface_count != 0 ? 1 : 0,
        g_magazine_visual_state.authored_hidden ? 1 : 0,
        batch.prior_native_clip_hidden ? 1 : 0,
        batch.other_native_hidden_surfaces, batch.companion_count,
        batch.retained_vertices, batch.retained_triangles, batch.count,
        context.camera_origin.x, context.camera_origin.y, context.camera_origin.z);
}
