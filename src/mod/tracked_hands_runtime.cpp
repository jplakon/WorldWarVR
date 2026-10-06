// SPDX-License-Identifier: GPL-3.0-only
#include "tracked_hands_runtime.hpp"
#include "virtual_query_timing.hpp"
#include "resident_page_access.hpp"

#include "input_mapping.hpp"
#include "pistol_hand_skinning.hpp"
#include "pistol_support_pose.hpp"
#include "stereo_diagnostics.hpp"
#include "synthetic_scene_slots.hpp"
#include "t4_layout_selector.hpp"
#include "viewmodel_filter.hpp"
#include "weapon_hook.hpp"

#include "xr_math.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

namespace wawvr::mod {
namespace {

constexpr wawvr::t4::Rva kDObjCreateRva = 0x00209340;
constexpr std::array<std::uint8_t, 32> kDObjCreatePrologue{
    0x51, 0x53, 0x6A, 0x38, 0x33, 0xDB, 0x8D, 0x46,
    0x14, 0x53, 0x50, 0xE8, 0xF0, 0x6B, 0x1A, 0x00,
    0x8B, 0x54, 0x24, 0x1C, 0x8B, 0x44, 0x24, 0x18,
    0x66, 0x8B, 0x4C, 0x24, 0x20, 0x83, 0xC4, 0x0C,
};
constexpr std::size_t kDObjCreateEpilogueOffset = 0x70;
constexpr std::array<std::uint8_t, 14> kDObjCreateEpilogue{
    0x66, 0x89, 0x5E, 0x62, 0x88, 0x5E, 0x61,
    0xC6, 0x46, 0x60, 0xFF, 0x5B, 0x59, 0xC3,
};
constexpr wawvr::t4::Rva kAddDObjToSceneRva = 0x002DA340;
constexpr std::array<std::uint8_t, 32> kAddDObjToScenePrologue{
    0x0F, 0x57, 0xC0, 0x83, 0xEC, 0x38, 0x53, 0x8B,
    0x5C, 0x24, 0x48, 0x55, 0x56, 0x0F, 0x2E, 0xC8,
    0x8B, 0xF0, 0x9F, 0xF6, 0xC4, 0x44, 0x57, 0x7A,
    0x16, 0x85, 0xC9, 0x75, 0x12, 0x0F, 0x2F, 0xD8,
};
constexpr std::size_t kAddDObjSceneIndexStoreOffset = 0x1D8;
constexpr std::array<std::uint8_t, 10> kAddDObjSceneIndexStore{
    0x8B, 0x15, 0x14, 0x8D, 0xDA,
    0x03, 0x66, 0x89, 0x0C, 0x42,
};
constexpr wawvr::t4::Rva kSlFindStringRva = 0x0028DD20;
constexpr std::array<std::uint8_t, 36> kSlFindStringSentinel{
    0x8B, 0xC2, 0x56, 0x8D, 0x70, 0x01, 0x8A, 0x08,
    0x83, 0xC0, 0x01, 0x84, 0xC9, 0x75, 0xF7, 0x2B,
    0xC6, 0x83, 0xC0, 0x01, 0x50, 0x8B, 0x44, 0x24,
    0x0C, 0x52, 0xE8, 0x51, 0xFD, 0xFF, 0xFF, 0x83,
    0xC4, 0x08, 0x5E, 0xC3,
};

constexpr std::size_t kDObjSize = 0x68;
constexpr std::size_t kDObjNumModelsOffset = 0x09;
constexpr std::size_t kDObjModelsOffset = 0x64;
constexpr std::size_t kPoseSize = 0x64;
constexpr std::size_t kPoseOriginOffset = 0x24;
constexpr std::size_t kPoseAnglesOffset = 0x30;
constexpr std::uint32_t kHandRenderFlags = 0x07;
constexpr std::size_t kAssetCapacity = 8;
constexpr std::size_t kClosedPistolAssetCapacity = 32;
constexpr float kTriangleWeightMinimum = 0.60F;
constexpr float kVertexWeightMinimum = 0.35F;
constexpr std::uint32_t kMaximumVertices = 65535;
constexpr std::uint32_t kMaximumTriangles = 65535;

struct DObjAnimMat final {
    float quaternion[4]{};
    float translation[3]{};
    float translation_weight{};
};

struct alignas(16) PackedVertex final {
    float xyz[3]{};
    float binormal_sign{};
    std::uint32_t color{};
    std::uint32_t texcoord{};
    std::uint32_t normal{};
    std::uint32_t tangent{};
};

struct XSurfaceVertexInfo final {
    std::int16_t grouped_vertex_count[4]{};
    std::uint16_t* blend{};
};

struct RigidVertexList final {
    std::uint16_t bone_offset{};
    std::uint16_t vertex_count{};
    std::uint16_t triangle_offset{};
    std::uint16_t triangle_count{};
    void* collision_tree{};
};

struct XSurface final {
    std::uint8_t tile_mode{};
    bool deformed{};
    std::uint16_t vertex_count{};
    std::uint16_t triangle_count{};
    std::uint8_t zone_handle{};
    std::uint8_t padding{};
    std::uint16_t base_triangle_index{};
    std::uint16_t base_vertex_index{};
    std::uint16_t* triangle_indices{};
    XSurfaceVertexInfo vertex_info{};
    PackedVertex* vertices{};
    void* vertex_buffer{};
    std::uint32_t rigid_vertex_list_count{};
    RigidVertexList* rigid_vertex_list{};
    void* index_buffer{};
    std::int32_t part_bits[4]{};
};

struct XModelLodInfo final {
    float distance{};
    std::uint16_t surface_count{};
    std::uint16_t surface_index{};
    std::int32_t part_bits[4]{};
    std::uint8_t lod{};
    std::uint8_t smc_index_plus_one{};
    std::uint8_t smc_alloc_bits{};
    std::uint8_t unused{};
};

struct XModel final {
    const char* name{};
    std::uint8_t bone_count{};
    std::uint8_t root_bone_count{};
    std::uint8_t surface_count{};
    std::uint8_t lod_ramp_type{};
    std::uint16_t* bone_names{};
    std::uint8_t* parent_list{};
    std::int16_t* quaternions{};
    float* translations{};
    std::uint8_t* part_classification{};
    DObjAnimMat* base_matrices{};
    XSurface* surfaces{};
    void** material_handles{};
    XModelLodInfo lod_info[4]{};
    void* collision_surfaces{};
    std::int32_t collision_surface_count{};
    std::int32_t contents{};
    void* bone_info{};
    float radius{};
    float minimums[3]{};
    float maximums[3]{};
    std::int16_t lod_count{};
    std::int16_t collision_lod{};
    std::uint8_t stream_info_and_padding[4]{};
    std::int32_t memory_usage{};
    std::uint8_t flags{};
    bool bad{};
    std::uint8_t tail_padding[2]{};
    void* physics_preset{};
    void* physics_geometry{};
    void* collision_map{};
    void* physics_constraints{};
};

struct DObjModelDescription final {
    XModel* model{};
    std::uint16_t bone_name{};
    bool ignore_collision{};
    std::uint8_t padding{};
};

static_assert(sizeof(DObjAnimMat) == 0x20);
static_assert(sizeof(PackedVertex) == 0x20);
static_assert(alignof(PackedVertex) == 0x10);
static_assert(sizeof(XSurfaceVertexInfo) == 0x0C);
static_assert(sizeof(RigidVertexList) == 0x0C);
static_assert(sizeof(XSurface) == 0x40);
static_assert(sizeof(XModelLodInfo) == 0x1C);
static_assert(sizeof(XModel) == 0xE4);
static_assert(sizeof(DObjModelDescription) == 0x08);

struct SurfaceStorage final {
    std::vector<PackedVertex> vertices{};
    std::vector<std::uint16_t> blend{};
};

struct HandAsset final {
    XModel model{};
    std::vector<XSurface> surfaces{};
    std::vector<SurfaceStorage> storage{};
    std::array<DObjAnimMat, 1> base_matrices{};
    alignas(16) std::array<std::uint8_t, kDObjSize> dobj{};
    alignas(16) std::array<std::uint8_t, kPoseSize> pose{};
    wawvr::xr::Vec3f attachment_position{};
    wawvr::xr::Basis3f attachment_axis{};
    std::array<std::uint32_t, 4> stock_bone_bits{};
    std::array<std::uint16_t, 4> finger_names{};
    std::uint16_t wrist_name{};
    std::uint16_t thumb_name{};
    std::uint64_t grip_started_milliseconds{};
    std::uint32_t kept_triangles{};
    bool weapon_attachment_valid{};
    bool ready{};
};

struct HandPairAsset final {
    XModel* source{};
    std::array<HandAsset, wawvr::xr::kHandCount> hands{};
    bool ready{};
    bool failed{};
};

struct ClosedPistolHandPairAsset final {
    XModel* source{};
    const XSurface* source_surfaces{};
    const DObjAnimMat* source_bind_matrices{};
    std::uint64_t weapon_identity{};
    std::uint64_t idle_since_milliseconds{};
    std::array<HandAsset, wawvr::xr::kHandCount> hands{};
    bool ready{};
    bool failed{};
};

struct PistolHandBakeContext final {
    XModel* source{};
    void* viewmodel_dobj{};
    const void* viewmodel_pose{};
    wawvr::xr::EnginePose root_world{};
    std::array<HandSkinBoneTransform, 128>* bones{};
};

struct TrackedModelAsset final {
    XModel* source{};
    alignas(16) std::array<std::uint8_t, kDObjSize> dobj{};
    alignas(16) std::array<std::uint8_t, kPoseSize> pose{};
    bool ready{};
    bool failed{};
};

std::atomic<bool> g_installed{false};
std::atomic<bool> g_enabled{false};
std::uintptr_t g_dobj_create = 0;
std::uintptr_t g_add_dobj_to_scene = 0;
std::uintptr_t g_sl_find_string = 0;
SyntheticSceneIndexPointers g_scene_index_pointers{};
std::array<HandPairAsset, kAssetCapacity> g_assets{};
std::array<ClosedPistolHandPairAsset, kClosedPistolAssetCapacity>
    g_closed_pistol_assets{};
std::array<TrackedModelAsset, kAssetCapacity> g_tracked_model_assets{};
std::mutex g_asset_mutex{};

[[nodiscard]] bool accessible_range(
    const void* const address,
    const std::size_t size) noexcept {
    return resident_page_access(address, size, false,
        &grouped_virtual_query<VirtualQueryTimingGroup::weapon_hands>);
}

[[nodiscard]] bool accessible_writable_range(
    void* const address,
    const std::size_t size) noexcept {
    return resident_page_access(address, size, true,
        &grouped_virtual_query<VirtualQueryTimingGroup::weapon_hands>);
}

template <std::size_t N>
[[nodiscard]] bool bytes_equal(
    const void* const address,
    const std::array<std::uint8_t, N>& expected) noexcept {
    return accessible_range(address, expected.size()) &&
           std::memcmp(address, expected.data(), expected.size()) == 0;
}

[[nodiscard]] bool finite_vector(const wawvr::xr::Vec3f& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] float dot(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] wawvr::xr::Vec3f cross(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] bool normalize(wawvr::xr::Vec3f* const value) noexcept {
    if (value == nullptr || !finite_vector(*value)) {
        return false;
    }
    const double length = std::sqrt(
        static_cast<double>(value->x) * value->x +
        static_cast<double>(value->y) * value->y +
        static_cast<double>(value->z) * value->z);
    if (!std::isfinite(length) || length <= 1.0e-8) {
        return false;
    }
    const float inverse = static_cast<float>(1.0 / length);
    value->x *= inverse;
    value->y *= inverse;
    value->z *= inverse;
    return true;
}

[[nodiscard]] bool valid_basis(const wawvr::xr::Basis3f& basis) noexcept {
    return finite_vector(basis.forward) && finite_vector(basis.left) &&
           finite_vector(basis.up) &&
           std::abs(dot(basis.forward, basis.forward) - 1.0F) < 0.02F &&
           std::abs(dot(basis.left, basis.left) - 1.0F) < 0.02F &&
           std::abs(dot(basis.up, basis.up) - 1.0F) < 0.02F &&
           std::abs(dot(basis.forward, basis.left)) < 0.02F &&
           std::abs(dot(basis.forward, basis.up)) < 0.02F &&
           std::abs(dot(basis.left, basis.up)) < 0.02F &&
           dot(cross(basis.forward, basis.left), basis.up) > 0.98F;
}

[[nodiscard]] wawvr::xr::Vec3f compose(
    const wawvr::xr::Basis3f& basis,
    const wawvr::xr::Vec3f& local) noexcept {
    return {
        local.x * basis.forward.x + local.y * basis.left.x +
            local.z * basis.up.x,
        local.x * basis.forward.y + local.y * basis.left.y +
            local.z * basis.up.y,
        local.x * basis.forward.z + local.y * basis.left.z +
            local.z * basis.up.z,
    };
}

[[nodiscard]] wawvr::xr::Vec3f add(
    const wawvr::xr::Vec3f& left,
    const wawvr::xr::Vec3f& right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] wawvr::xr::Basis3f compose_axes(
    const wawvr::xr::Basis3f& outer,
    const wawvr::xr::Basis3f& inner) noexcept {
    return {
        compose(outer, inner.forward),
        compose(outer, inner.left),
        compose(outer, inner.up),
    };
}

#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) std::uint32_t __cdecl
tracked_hands_call_sl_find_string(const char*) noexcept {
    __asm {
        mov edx, dword ptr [esp + 4]
        push 0
        call dword ptr [g_sl_find_string]
        lea esp, [esp + 4]
        ret
    }
}

extern "C" __declspec(naked) void __cdecl tracked_hands_call_dobj_create(
    DObjModelDescription*, std::uint32_t, void*, void*,
    std::uint32_t) noexcept {
    __asm {
        push esi
        push edi
        mov eax, dword ptr [esp + 0Ch]
        mov ecx, dword ptr [esp + 10h]
        mov edi, dword ptr [esp + 14h]
        mov esi, dword ptr [esp + 18h]
        mov edx, dword ptr [esp + 1Ch]
        push edx
        push ecx
        push eax
        call dword ptr [g_dobj_create]
        lea esp, [esp + 0Ch]
        pop edi
        pop esi
        ret
    }
}

extern "C" __declspec(naked) void __cdecl tracked_hands_call_add_dobj_to_scene(
    void*, void*, std::uint32_t, float*, std::uint32_t) noexcept {
    __asm {
        push esi
        push edi
        mov eax, dword ptr [esp + 0Ch]
        mov esi, dword ptr [esp + 10h]
        mov edi, dword ptr [esp + 14h]
        mov edx, dword ptr [esp + 18h]
        mov ecx, dword ptr [esp + 1Ch]
        xorps xmm1, xmm1
        xorps xmm2, xmm2
        xorps xmm3, xmm3
        push edx
        push edi
        push esi
        call dword ptr [g_add_dobj_to_scene]
        lea esp, [esp + 0Ch]
        pop edi
        pop esi
        ret
    }
}
#else
std::uint32_t tracked_hands_call_sl_find_string(const char*) noexcept {
    return 0;
}
void tracked_hands_call_dobj_create(
    DObjModelDescription*, std::uint32_t, void*, void*,
    std::uint32_t) noexcept {}
void tracked_hands_call_add_dobj_to_scene(
    void*, void*, std::uint32_t, float*, std::uint32_t) noexcept {}
#endif

[[nodiscard]] bool read_dobj_source_model(
    void* const dobj,
    XModel** const model) noexcept {
    if (model == nullptr || !accessible_range(dobj, kDObjSize)) {
        return false;
    }
    *model = nullptr;
    const auto* const bytes = static_cast<const std::uint8_t*>(dobj);
    std::uint8_t model_count = 0;
    XModel** models = nullptr;
    std::memcpy(&model_count, bytes + kDObjNumModelsOffset, sizeof(model_count));
    std::memcpy(&models, bytes + kDObjModelsOffset, sizeof(models));
    if (model_count < 2 || models == nullptr ||
        !accessible_range(models, static_cast<std::size_t>(model_count) *
                                      sizeof(*models))) {
        return false;
    }
    std::memcpy(model, models, sizeof(*model));
    return *model != nullptr && accessible_range(*model, sizeof(**model));
}

[[nodiscard]] bool read_dobj_created_model(
    void* const dobj,
    XModel** const model) noexcept {
    if (model == nullptr || !accessible_range(dobj, kDObjSize)) {
        return false;
    }
    XModel** models = nullptr;
    std::memcpy(
        &models,
        static_cast<std::uint8_t*>(dobj) + kDObjModelsOffset,
        sizeof(models));
    return models != nullptr && accessible_range(models, sizeof(*models)) &&
           (std::memcpy(model, models, sizeof(*model)), *model != nullptr);
}

[[nodiscard]] int bone_parent(
    const XModel& model,
    const int bone) noexcept {
    if (bone < static_cast<int>(model.root_bone_count) ||
        bone >= static_cast<int>(model.bone_count) ||
        model.parent_list == nullptr) {
        return -1;
    }
    const int offset = model.parent_list[bone - model.root_bone_count];
    return offset > 0 && offset <= bone ? bone - offset : -1;
}

[[nodiscard]] bool descends_from(
    const XModel& model,
    int bone,
    const int ancestor) noexcept {
    for (int depth = 0; bone >= 0 && depth < 128; ++depth) {
        if (bone == ancestor) {
            return true;
        }
        bone = bone_parent(model, bone);
    }
    return false;
}

[[nodiscard]] int find_bone(
    const XModel& model,
    const char* const name) noexcept {
    if (name == nullptr || model.bone_count == 0 ||
        model.bone_names == nullptr ||
        !accessible_range(
            model.bone_names,
            static_cast<std::size_t>(model.bone_count) *
                sizeof(std::uint16_t))) {
        return -1;
    }
    const std::uint32_t string = tracked_hands_call_sl_find_string(name);
    if (string == 0 || string > 0xFFFF) {
        return -1;
    }
    for (int index = 0; index < model.bone_count; ++index) {
        if (model.bone_names[index] == string) {
            return index;
        }
    }
    return -1;
}

void accumulate_weight(
    const XModel& model,
    const int bone,
    const std::uint32_t weight,
    const int shoulder,
    const int wrist,
    const int wrist_twist,
    float* const hand_weight) noexcept {
    if (hand_weight == nullptr || weight == 0 || bone < 0 ||
        bone >= model.bone_count ||
        !descends_from(model, bone, shoulder)) {
        return;
    }
    if (bone == wrist_twist || descends_from(model, bone, wrist)) {
        *hand_weight += static_cast<float>(weight) / 65535.0F;
    }
}

[[nodiscard]] bool decode_hand_weights(
    const XModel& model,
    const XSurface& surface,
    const int shoulder,
    const int wrist,
    const int wrist_twist,
    std::vector<float>* const weights) {
    if (weights == nullptr || surface.vertex_count == 0) {
        return false;
    }
    weights->assign(surface.vertex_count, 0.0F);
    if (!surface.deformed) {
        if (surface.rigid_vertex_list_count == 0 ||
            surface.rigid_vertex_list == nullptr ||
            !accessible_range(
                surface.rigid_vertex_list,
                static_cast<std::size_t>(surface.rigid_vertex_list_count) *
                    sizeof(RigidVertexList))) {
            return false;
        }
        std::size_t vertex = 0;
        for (std::uint32_t list_index = 0;
             list_index < surface.rigid_vertex_list_count; ++list_index) {
            const auto& list = surface.rigid_vertex_list[list_index];
            const int bone = list.bone_offset >> 6U;
            if ((list.bone_offset & 0x3FU) != 0 || bone >= model.bone_count ||
                vertex + list.vertex_count > surface.vertex_count) {
                return false;
            }
            for (std::uint16_t count = 0; count < list.vertex_count;
                 ++count, ++vertex) {
                accumulate_weight(
                    model, bone, 65535, shoulder, wrist, wrist_twist,
                    &(*weights)[vertex]);
            }
        }
        return vertex == surface.vertex_count;
    }

    if (surface.vertex_info.blend == nullptr) {
        return false;
    }
    std::size_t words = 0;
    std::size_t vertices = 0;
    for (int group = 0; group < 4; ++group) {
        const auto count = surface.vertex_info.grouped_vertex_count[group];
        if (count < 0) {
            return false;
        }
        vertices += static_cast<std::size_t>(count);
        words += static_cast<std::size_t>(count) *
                 static_cast<std::size_t>(1 + group * 2);
    }
    if (vertices != surface.vertex_count || words == 0 ||
        !accessible_range(
            surface.vertex_info.blend,
            words * sizeof(std::uint16_t))) {
        return false;
    }
    const std::uint16_t* blend = surface.vertex_info.blend;
    std::size_t vertex = 0;
    for (int group = 0; group < 4; ++group) {
        const int count = surface.vertex_info.grouped_vertex_count[group];
        for (int grouped = 0; grouped < count;
             ++grouped, ++vertex, blend += 1 + group * 2) {
            std::uint32_t secondary_total = 0;
            for (int secondary = 0; secondary < group; ++secondary) {
                const std::uint32_t weight = blend[2 + secondary * 2];
                secondary_total = (std::min)(
                    65535U, secondary_total + weight);
                accumulate_weight(
                    model, blend[1 + secondary * 2] >> 6U, weight,
                    shoulder, wrist, wrist_twist, &(*weights)[vertex]);
            }
            accumulate_weight(
                model, blend[0] >> 6U, 65535U - secondary_total,
                shoulder, wrist, wrist_twist, &(*weights)[vertex]);
        }
    }
    return vertex == surface.vertex_count;
}

void unpack_unit_vector(
    const std::uint32_t packed,
    float value[3]) noexcept {
    std::uint8_t bytes[4]{};
    std::memcpy(bytes, &packed, sizeof(bytes));
    const float scale =
        (static_cast<float>(bytes[3]) + 192.0F) / 32385.0F;
    value[0] = (static_cast<float>(bytes[0]) - 127.0F) * scale;
    value[1] = (static_cast<float>(bytes[1]) - 127.0F) * scale;
    value[2] = (static_cast<float>(bytes[2]) - 127.0F) * scale;
}

[[nodiscard]] std::uint32_t pack_unit_vector(
    wawvr::xr::Vec3f value) noexcept {
    if (!normalize(&value)) {
        return 0;
    }
    float best_error = (std::numeric_limits<float>::max)();
    std::uint32_t best = 0;
    for (unsigned exponent = 0; exponent < 256; ++exponent) {
        std::uint8_t bytes[4]{};
        bytes[3] = static_cast<std::uint8_t>(exponent);
        const float scale =
            32385.0F / (static_cast<float>(bytes[3]) + 192.0F);
        const float input[3] = {value.x, value.y, value.z};
        for (int component = 0; component < 3; ++component) {
            const int encoded = static_cast<int>(
                input[component] * scale + 127.5F);
            bytes[component] = static_cast<std::uint8_t>(
                (std::clamp)(encoded, 0, 255));
        }
        std::uint32_t candidate = 0;
        std::memcpy(&candidate, bytes, sizeof(candidate));
        float decoded[3]{};
        unpack_unit_vector(candidate, decoded);
        wawvr::xr::Vec3f decoded_vector{
            decoded[0], decoded[1], decoded[2]};
        if (!normalize(&decoded_vector)) {
            continue;
        }
        const float error = std::abs(dot(value, decoded_vector) - 1.0F);
        if (error < best_error) {
            best_error = error;
            best = candidate;
        }
    }
    return best;
}

[[nodiscard]] bool rebase_vertex(
    const PackedVertex& source,
    const DObjAnimMat& wrist,
    PackedVertex* const output) noexcept {
    if (output == nullptr) {
        return false;
    }
    const auto normalized = wawvr::xr::Normalize({
        wrist.quaternion[0], wrist.quaternion[1],
        wrist.quaternion[2], wrist.quaternion[3],
    });
    if (!std::isfinite(normalized.x) || !std::isfinite(normalized.y) ||
        !std::isfinite(normalized.z) || !std::isfinite(normalized.w)) {
        return false;
    }
    const auto inverse = wawvr::xr::Conjugate(normalized);
    *output = source;
    const auto position = wawvr::xr::Rotate(
        inverse,
        {source.xyz[0] - wrist.translation[0],
         source.xyz[1] - wrist.translation[1],
         source.xyz[2] - wrist.translation[2]});
    if (!finite_vector(position)) {
        return false;
    }
    output->xyz[0] = position.x;
    output->xyz[1] = position.y;
    output->xyz[2] = position.z;
    for (std::uint32_t PackedVertex::* field :
         {&PackedVertex::normal, &PackedVertex::tangent}) {
        float decoded[3]{};
        unpack_unit_vector(source.*field, decoded);
        const auto rotated = wawvr::xr::Rotate(
            inverse, {decoded[0], decoded[1], decoded[2]});
        const std::uint32_t packed = pack_unit_vector(rotated);
        if (packed == 0) {
            return false;
        }
        output->*field = packed;
    }
    return true;
}

[[nodiscard]] bool decode_surface_skin_influences(
    const XModel& model,
    const XSurface& surface,
    std::vector<std::array<HandSkinInfluence, 4>>* const influences) {
    if (influences == nullptr || surface.vertex_count == 0 ||
        model.bone_count == 0 || model.bone_count > 128) {
        return false;
    }
    influences->assign(surface.vertex_count, {});
    if (!surface.deformed) {
        if (surface.rigid_vertex_list_count == 0 ||
            surface.rigid_vertex_list_count > surface.vertex_count ||
            surface.rigid_vertex_list == nullptr ||
            !accessible_range(
                surface.rigid_vertex_list,
                static_cast<std::size_t>(surface.rigid_vertex_list_count) *
                    sizeof(RigidVertexList))) {
            return false;
        }
        std::size_t vertex = 0;
        for (std::size_t index = 0;
             index < surface.rigid_vertex_list_count; ++index) {
            const RigidVertexList list = surface.rigid_vertex_list[index];
            if (vertex + list.vertex_count > surface.vertex_count) {
                return false;
            }
            std::array<HandSkinInfluence, 4> decoded{};
            if (!decode_pistol_hand_skin_influences(
                    {&list.bone_offset, 1}, 1, model.bone_count, &decoded)) {
                return false;
            }
            for (std::size_t count = 0; count < list.vertex_count;
                 ++count, ++vertex) {
                (*influences)[vertex] = decoded;
            }
        }
        return vertex == surface.vertex_count;
    }

    std::size_t words = 0;
    std::size_t vertices = 0;
    for (std::size_t group = 0; group < 4; ++group) {
        const auto count = surface.vertex_info.grouped_vertex_count[group];
        if (count < 0) {
            return false;
        }
        vertices += static_cast<std::size_t>(count);
        words += static_cast<std::size_t>(count) * (1 + group * 2);
    }
    if (vertices != surface.vertex_count || words == 0 ||
        surface.vertex_info.blend == nullptr ||
        !accessible_range(surface.vertex_info.blend,
                          words * sizeof(std::uint16_t))) {
        return false;
    }
    std::size_t vertex = 0;
    std::size_t cursor = 0;
    for (std::size_t group = 0; group < 4; ++group) {
        const std::size_t stride = 1 + group * 2;
        const auto count = surface.vertex_info.grouped_vertex_count[group];
        for (int index = 0; index < count; ++index, ++vertex, cursor += stride) {
            if (!decode_pistol_hand_skin_influences(
                    {surface.vertex_info.blend + cursor, stride}, group + 1,
                    model.bone_count, &(*influences)[vertex])) {
                return false;
            }
        }
    }
    return vertex == surface.vertex_count && cursor == words;
}

[[nodiscard]] bool capture_pistol_bake_bone(
    PistolHandBakeContext& context,
    const std::size_t bone) noexcept {
    if (context.source == nullptr || context.bones == nullptr ||
        bone >= context.source->bone_count || bone >= context.bones->size()) {
        return false;
    }
    auto& captured = (*context.bones)[bone];
    if (captured.valid) {
        return true;
    }
    if (
        context.source->bone_names == nullptr ||
        context.source->base_matrices == nullptr ||
        !accessible_range(context.source->bone_names,
                          context.source->bone_count * sizeof(std::uint16_t)) ||
        !accessible_range(context.source->base_matrices,
                          context.source->bone_count * sizeof(DObjAnimMat))) {
        return false;
    }
    const DObjAnimMat bind = context.source->base_matrices[bone];
    const double length_squared =
        static_cast<double>(bind.quaternion[0]) * bind.quaternion[0] +
        static_cast<double>(bind.quaternion[1]) * bind.quaternion[1] +
        static_cast<double>(bind.quaternion[2]) * bind.quaternion[2] +
        static_cast<double>(bind.quaternion[3]) * bind.quaternion[3];
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8) {
        return false;
    }
    const auto quaternion = wawvr::xr::Normalize({
        bind.quaternion[0], bind.quaternion[1],
        bind.quaternion[2], bind.quaternion[3]});
    HandSkinBoneTransform staged{};
    staged.bind_pose = {
        .position = {bind.translation[0], bind.translation[1], bind.translation[2]},
        .axis = {
            wawvr::xr::Rotate(quaternion, {1, 0, 0}),
            wawvr::xr::Rotate(quaternion, {0, 1, 0}),
            wawvr::xr::Rotate(quaternion, {0, 0, 1}),
        },
    };
    const std::uint16_t tag = context.source->bone_names[bone];
    if (!finite_vector(staged.bind_pose.position) ||
        !valid_basis(staged.bind_pose.axis) || tag == 0 ||
        !read_viewmodel_world_tag_pose(
            context.viewmodel_dobj, tag, context.viewmodel_pose,
            &staged.animated_world_pose) ||
        !finite_vector(staged.animated_world_pose.position) ||
        !valid_basis(staged.animated_world_pose.axis)) {
        return false;
    }
    staged.valid = true;
    captured = staged;
    return true;
}

[[nodiscard]] bool bake_pistol_hand_vertex(
    const PackedVertex& source,
    const std::array<HandSkinInfluence, 4>& influences,
    PistolHandBakeContext& context,
    PackedVertex* const output) noexcept {
    if (output == nullptr || context.bones == nullptr) {
        return false;
    }
    std::array<HandSkinInfluence, 4> active_influences{};
    std::size_t active_count = 0;
    for (const auto& influence : influences) {
        if (influence.weight == 0) {
            continue;
        }
        if (!capture_pistol_bake_bone(context, influence.bone)) {
            return false;
        }
        active_influences[active_count++] = influence;
    }
    float normal[3]{};
    float tangent[3]{};
    unpack_unit_vector(source.normal, normal);
    unpack_unit_vector(source.tangent, tangent);
    HandSkinnedVertex skinned{};
    if (!skin_pistol_hand_vertex_to_root(
            {source.xyz[0], source.xyz[1], source.xyz[2]},
            {normal[0], normal[1], normal[2]},
            {tangent[0], tangent[1], tangent[2]},
            {active_influences.data(), active_count}, *context.bones,
            context.root_world, &skinned)) {
        return false;
    }
    const std::uint32_t packed_normal = pack_unit_vector(skinned.normal);
    const std::uint32_t packed_tangent = pack_unit_vector(skinned.tangent);
    if (packed_normal == 0 || packed_tangent == 0) {
        return false;
    }
    PackedVertex staged = source;
    staged.xyz[0] = skinned.position.x;
    staged.xyz[1] = skinned.position.y;
    staged.xyz[2] = skinned.position.z;
    staged.normal = packed_normal;
    staged.tangent = packed_tangent;
    *output = staged;
    return true;
}

[[nodiscard]] bool bone_origin_in_wrist_space(
    const XModel& model,
    const int wrist,
    const DObjAnimMat& wrist_bind,
    const char* const name,
    wawvr::xr::Vec3f* const output) noexcept {
    if (output == nullptr) {
        return false;
    }
    const int bone = find_bone(model, name);
    if (bone < 0 || !descends_from(model, bone, wrist)) {
        return false;
    }
    const auto wrist_quaternion = wawvr::xr::Normalize({
        wrist_bind.quaternion[0], wrist_bind.quaternion[1],
        wrist_bind.quaternion[2], wrist_bind.quaternion[3],
    });
    const auto inverse = wawvr::xr::Conjugate(wrist_quaternion);
    const auto& bone_bind = model.base_matrices[bone];
    *output = wawvr::xr::Rotate(
        inverse,
        {bone_bind.translation[0] - wrist_bind.translation[0],
         bone_bind.translation[1] - wrist_bind.translation[1],
         bone_bind.translation[2] - wrist_bind.translation[2]});
    return finite_vector(*output);
}

[[nodiscard]] bool build_anatomical_attachment(
    const XModel& model,
    const int wrist,
    const DObjAnimMat& wrist_bind,
    const bool left_hand,
    wawvr::xr::Vec3f* const attachment_position,
    wawvr::xr::Basis3f* const attachment_axis) noexcept {
    if (attachment_position == nullptr || attachment_axis == nullptr) {
        return false;
    }
    const char* const fingers_left[] = {
        "j_index_le_0", "j_mid_le_0", "j_ring_le_0", "j_pinky_le_0"};
    const char* const fingers_right[] = {
        "j_index_ri_0", "j_mid_ri_0", "j_ring_ri_0", "j_pinky_ri_0"};
    const char* const* fingers = left_hand ? fingers_left : fingers_right;
    wawvr::xr::Vec3f finger_center{};
    int finger_count = 0;
    for (int index = 0; index < 4; ++index) {
        wawvr::xr::Vec3f finger{};
        if (!bone_origin_in_wrist_space(
                model, wrist, wrist_bind, fingers[index], &finger)) {
            continue;
        }
        finger_center = add(finger_center, finger);
        ++finger_count;
    }
    wawvr::xr::Vec3f thumb{};
    if (finger_count == 0 ||
        !bone_origin_in_wrist_space(
            model, wrist, wrist_bind,
            left_hand ? "j_thumb_le_0" : "j_thumb_ri_0", &thumb)) {
        return false;
    }
    const float inverse_count = 1.0F / static_cast<float>(finger_count);
    finger_center.x *= inverse_count;
    finger_center.y *= inverse_count;
    finger_center.z *= inverse_count;
    auto fingers_forward = finger_center;
    if (!normalize(&fingers_forward)) {
        return false;
    }
    const float along = dot(thumb, fingers_forward);
    wawvr::xr::Vec3f little_to_thumb{
        thumb.x - along * fingers_forward.x,
        thumb.y - along * fingers_forward.y,
        thumb.z - along * fingers_forward.z,
    };
    if (!normalize(&little_to_thumb)) {
        return false;
    }
    auto into_palm = cross(fingers_forward, little_to_thumb);
    if (!normalize(&into_palm)) {
        return false;
    }
    little_to_thumb = cross(into_palm, fingers_forward);
    if (!normalize(&little_to_thumb)) {
        return false;
    }

    // Both meshes are expressed in their own wrist bind space. Defining the
    // anatomical frame from finger, thumb and palm directions removes the
    // authored left/right mirror without a guessed controller Euler offset.
    wawvr::xr::Basis3f model_grip{
        little_to_thumb,
        {-into_palm.x, -into_palm.y, -into_palm.z},
        {-fingers_forward.x, -fingers_forward.y, -fingers_forward.z},
    };
    if (!valid_basis(model_grip)) {
        return false;
    }
    const wawvr::xr::Basis3f inverse_model{
        {model_grip.forward.x, model_grip.left.x, model_grip.up.x},
        {model_grip.forward.y, model_grip.left.y, model_grip.up.y},
        {model_grip.forward.z, model_grip.left.z, model_grip.up.z},
    };
    if (!valid_basis(inverse_model)) {
        return false;
    }
    const wawvr::xr::Vec3f palm_anchor{
        finger_center.x * 0.5F,
        finger_center.y * 0.5F,
        finger_center.z * 0.5F,
    };
    const auto palm_controller = compose(inverse_model, palm_anchor);
    *attachment_position = {
        -palm_controller.x, -palm_controller.y, -palm_controller.z};
    *attachment_axis = inverse_model;
    return finite_vector(*attachment_position);
}

[[nodiscard]] bool select_triangle(
    const std::uint16_t* const indices,
    const std::uint16_t vertex_count,
    const std::vector<float>& weights) noexcept {
    if (indices == nullptr) {
        return false;
    }
    float sum = 0.0F;
    float maximum = 0.0F;
    for (int corner = 0; corner < 3; ++corner) {
        if (indices[corner] >= vertex_count) {
            return false;
        }
        sum += weights[indices[corner]];
        maximum = (std::max)(maximum, weights[indices[corner]]);
    }
    return sum >= kTriangleWeightMinimum &&
           maximum >= kVertexWeightMinimum;
}

[[nodiscard]] bool build_hand_asset(
    XModel* const source,
    const bool left_hand,
    HandAsset* const asset,
    PistolHandBakeContext* const bake_context = nullptr) {
    if (source == nullptr || asset == nullptr || source->bone_count == 0 ||
        source->bone_count > 128 || source->surface_count == 0 ||
        source->surfaces == nullptr || source->base_matrices == nullptr ||
        !accessible_range(
            source->surfaces,
            static_cast<std::size_t>(source->surface_count) *
                sizeof(XSurface)) ||
        !accessible_range(
            source->base_matrices,
            static_cast<std::size_t>(source->bone_count) *
                sizeof(DObjAnimMat))) {
        return false;
    }
    const char* const shoulder_name =
        left_hand ? "j_shoulder_le" : "j_shoulder_ri";
    const char* const wrist_name =
        left_hand ? "j_wrist_le" : "j_wrist_ri";
    const char* const twist_name =
        left_hand ? "j_wristtwist_le" : "j_wristtwist_ri";
    const std::array<const char*, 4> finger_names = left_hand
        ? std::array<const char*, 4>{
              "j_index_le_0", "j_mid_le_0",
              "j_ring_le_0", "j_pinky_le_0"}
        : std::array<const char*, 4>{
              "j_index_ri_0", "j_mid_ri_0",
              "j_ring_ri_0", "j_pinky_ri_0"};
    const char* const thumb_name =
        left_hand ? "j_thumb_le_0" : "j_thumb_ri_0";
    const int shoulder = find_bone(*source, shoulder_name);
    const int wrist = find_bone(*source, wrist_name);
    const int twist = find_bone(*source, twist_name);
    const int thumb = find_bone(*source, thumb_name);
    if (shoulder < 0 || wrist < 0 || twist < 0 ||
        thumb < 0 ||
        !descends_from(*source, wrist, shoulder) ||
        !descends_from(*source, twist, shoulder) ||
        !descends_from(*source, thumb, wrist)) {
        return false;
    }

    *asset = {};
    asset->wrist_name = source->bone_names[wrist];
    asset->thumb_name = source->bone_names[thumb];
    for (std::size_t index = 0; index < finger_names.size(); ++index) {
        const int finger = find_bone(*source, finger_names[index]);
        if (finger >= 0 && descends_from(*source, finger, wrist)) {
            asset->finger_names[index] = source->bone_names[finger];
        }
    }
    for (int bone = 0; bone < source->bone_count; ++bone) {
        // Start with the selected authored arm. Surface-level dependencies are
        // added below after triangle ownership is known.
        if (descends_from(*source, bone, shoulder)) {
            asset->stock_bone_bits[static_cast<std::size_t>(bone) >> 5U] |=
                0x80000000U >> (static_cast<std::size_t>(bone) & 31U);
        }
    }
    const auto wrist_bind = source->base_matrices[wrist];
    if (!build_anatomical_attachment(
            *source, wrist, wrist_bind, left_hand,
            &asset->attachment_position, &asset->attachment_axis)) {
        return false;
    }
    asset->surfaces.assign(
        source->surfaces, source->surfaces + source->surface_count);
    asset->storage.resize(source->surface_count);
    bool bounds_initialized = false;
    float minimums[3]{};
    float maximums[3]{};
    float radius_squared = 0.0F;

    for (std::size_t surface_index = 0;
         surface_index < source->surface_count; ++surface_index) {
        const XSurface& original = source->surfaces[surface_index];
        if (original.vertex_count == 0 ||
            original.vertex_count > kMaximumVertices ||
            (bake_context != nullptr && original.vertex_count >
                static_cast<std::uint16_t>((std::numeric_limits<std::int16_t>::max)())) ||
            original.triangle_count == 0 ||
            original.triangle_count > kMaximumTriangles ||
            original.vertices == nullptr ||
            original.triangle_indices == nullptr ||
            !accessible_range(
                original.vertices,
                static_cast<std::size_t>(original.vertex_count) *
                    sizeof(PackedVertex)) ||
            !accessible_range(
                original.triangle_indices,
                static_cast<std::size_t>(original.triangle_count) * 3U *
                    sizeof(std::uint16_t))) {
            return false;
        }
        std::vector<float> weights{};
        if (!decode_hand_weights(
                *source, original, shoulder, wrist, twist, &weights)) {
            return false;
        }
        std::vector<std::uint8_t> kept(original.vertex_count, 0);
        bool surface_contains_selected_hand = false;
        for (std::size_t triangle = 0;
             triangle < original.triangle_count; ++triangle) {
            const std::uint16_t* indices =
                original.triangle_indices + triangle * 3U;
            if (!select_triangle(indices, original.vertex_count, weights)) {
                continue;
            }
            surface_contains_selected_hand = true;
            ++asset->kept_triangles;
            for (int corner = 0; corner < 3; ++corner) {
                kept[indices[corner]] = 1;
            }
        }

        if (surface_contains_selected_hand) {
            // T4 culls an entire XSurface when any referenced DObj part is
            // hidden. Some first-person sleeve/glove surfaces depend on bones
            // outside the wrist/shoulder subtree, so clearing only anatomical
            // descendants can make a sole gripping hand disappear. Clear the
            // exact dependency mask of every surface that contains selected-
            // hand triangles; the free opposite glove remains a standalone
            // controller-tracked submission.
            for (std::size_t word = 0;
                 word < asset->stock_bone_bits.size(); ++word) {
                asset->stock_bone_bits[word] |=
                    static_cast<std::uint32_t>(original.part_bits[word]);
            }
        }

        auto& storage = asset->storage[surface_index];
        std::vector<std::array<HandSkinInfluence, 4>> skin_influences{};
        if (bake_context != nullptr && surface_contains_selected_hand &&
            !decode_surface_skin_influences(*source, original, &skin_influences)) {
            return false;
        }
        storage.vertices.assign(
            original.vertices, original.vertices + original.vertex_count);
        storage.blend.assign(original.vertex_count, 0);
        if ((reinterpret_cast<std::uintptr_t>(storage.vertices.data()) &
             0x0FU) != 0) {
            return false;
        }
        for (std::size_t vertex = 0; vertex < original.vertex_count;
             ++vertex) {
            if (!kept[vertex]) {
                storage.vertices[vertex].xyz[0] = 0.0F;
                storage.vertices[vertex].xyz[1] = 0.0F;
                storage.vertices[vertex].xyz[2] = 0.0F;
                continue;
            }
            PackedVertex rebased{};
            const bool rebased_ok = bake_context != nullptr
                ? bake_pistol_hand_vertex(
                      original.vertices[vertex], skin_influences[vertex],
                      *bake_context, &rebased)
                : rebase_vertex(original.vertices[vertex], wrist_bind, &rebased);
            if (!rebased_ok) {
                return false;
            }
            storage.vertices[vertex] = rebased;
            for (int component = 0; component < 3; ++component) {
                const float value = rebased.xyz[component];
                if (!bounds_initialized) {
                    minimums[component] = value;
                    maximums[component] = value;
                } else {
                    minimums[component] =
                        (std::min)(minimums[component], value);
                    maximums[component] =
                        (std::max)(maximums[component], value);
                }
            }
            bounds_initialized = true;
            radius_squared = (std::max)(
                radius_squared,
                rebased.xyz[0] * rebased.xyz[0] +
                    rebased.xyz[1] * rebased.xyz[1] +
                    rebased.xyz[2] * rebased.xyz[2]);
        }

        auto& surface = asset->surfaces[surface_index];
        surface.deformed = true;
        surface.vertices = storage.vertices.data();
        surface.vertex_info.grouped_vertex_count[0] =
            static_cast<std::int16_t>(original.vertex_count);
        surface.vertex_info.grouped_vertex_count[1] = 0;
        surface.vertex_info.grouped_vertex_count[2] = 0;
        surface.vertex_info.grouped_vertex_count[3] = 0;
        surface.vertex_info.blend = storage.blend.data();
        surface.rigid_vertex_list_count = 0;
        surface.rigid_vertex_list = nullptr;
        surface.part_bits[0] = static_cast<std::int32_t>(0x80000000U);
        surface.part_bits[1] = 0;
        surface.part_bits[2] = 0;
        surface.part_bits[3] = 0;
    }
    if (!bounds_initialized || asset->kept_triangles == 0) {
        return false;
    }

    asset->base_matrices[0].quaternion[3] = 1.0F;
    asset->base_matrices[0].translation_weight = 2.0F;
    asset->model = *source;
    asset->model.bone_count = 1;
    asset->model.root_bone_count = 1;
    asset->model.parent_list = nullptr;
    asset->model.base_matrices = asset->base_matrices.data();
    asset->model.surfaces = asset->surfaces.data();
    for (auto& lod : asset->model.lod_info) {
        lod.surface_count = asset->model.surface_count;
        lod.surface_index = 0;
        lod.part_bits[0] = static_cast<std::int32_t>(0x80000000U);
        lod.part_bits[1] = 0;
        lod.part_bits[2] = 0;
        lod.part_bits[3] = 0;
    }
    for (int component = 0; component < 3; ++component) {
        asset->model.minimums[component] = minimums[component] - 1.0F;
        asset->model.maximums[component] = maximums[component] + 1.0F;
    }
    asset->model.radius = std::sqrt(radius_squared) + 1.0F;

    DObjModelDescription description{};
    description.model = &asset->model;
    tracked_hands_call_dobj_create(
        &description, 1, nullptr, asset->dobj.data(),
        kSyntheticDObjCreationEntity);
    XModel* created = nullptr;
    asset->ready = read_dobj_created_model(asset->dobj.data(), &created) &&
                   created == &asset->model;
    return asset->ready;
}

[[nodiscard]] HandPairAsset* resolve_asset(XModel* const source) {
    for (auto& asset : g_assets) {
        if (asset.source == source) {
            return asset.ready ? &asset : nullptr;
        }
    }
    for (auto& asset : g_assets) {
        if (asset.source != nullptr) {
            continue;
        }
        asset.source = source;
        try {
            const bool left = build_hand_asset(
                source, true,
                &asset.hands[static_cast<std::uint32_t>(
                    wawvr::xr::Hand::Left)]);
            const bool right = build_hand_asset(
                source, false,
                &asset.hands[static_cast<std::uint32_t>(
                    wawvr::xr::Hand::Right)]);
            asset.ready = left && right;
        } catch (...) {
            asset.ready = false;
        }
        asset.failed = !asset.ready;
        return asset.ready ? &asset : nullptr;
    }
    return nullptr;
}

[[nodiscard]] TrackedModelAsset* resolve_tracked_model_asset(
    XModel* const source) noexcept {
    if (source == nullptr || !accessible_range(source, sizeof(*source))) {
        return nullptr;
    }
    for (auto& asset : g_tracked_model_assets) {
        if (asset.source == source) {
            return asset.ready ? &asset : nullptr;
        }
    }
    for (auto& asset : g_tracked_model_assets) {
        if (asset.source != nullptr) {
            continue;
        }
        asset.source = source;
        DObjModelDescription description{};
        description.model = source;
        tracked_hands_call_dobj_create(
            &description, 1, nullptr, asset.dobj.data(),
            kSyntheticDObjCreationEntity);
        XModel* created = nullptr;
        asset.ready = read_dobj_created_model(asset.dobj.data(), &created) &&
                      created == source;
        asset.failed = !asset.ready;
        return asset.ready ? &asset : nullptr;
    }
    return nullptr;
}

[[nodiscard]] bool axis_to_angles(
    const wawvr::xr::Basis3f& axis,
    float angles[3]) noexcept {
    if (angles == nullptr || !valid_basis(axis)) {
        return false;
    }
    constexpr double kDegrees = 57.29577951308232;
    const double horizontal = std::sqrt(
        static_cast<double>(axis.forward.x) * axis.forward.x +
        static_cast<double>(axis.forward.y) * axis.forward.y);
    angles[0] = static_cast<float>(
        std::atan2(-static_cast<double>(axis.forward.z), horizontal) *
        kDegrees);
    angles[1] = horizontal <= 1.0e-8
        ? 0.0F
        : static_cast<float>(
              std::atan2(
                  static_cast<double>(axis.forward.y),
                  static_cast<double>(axis.forward.x)) * kDegrees);
    angles[2] = static_cast<float>(
        std::atan2(
            static_cast<double>(axis.left.z),
            static_cast<double>(axis.up.z)) * kDegrees);
    return std::isfinite(angles[0]) && std::isfinite(angles[1]) &&
           std::isfinite(angles[2]);
}

[[nodiscard]] bool calculate_hand_world_pose(
    const ControllerFrameSnapshot& snapshot,
    const wawvr::xr::Hand hand,
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const HandAsset& asset,
    wawvr::xr::EnginePose* const output) noexcept {
    if (output == nullptr || !asset.ready ||
        !controller_frame_is_current(snapshot, GetTickCount64()) ||
        !snapshot.frame.actions.focused || !snapshot.frame.views_valid ||
        !finite_vector(camera_origin) || !valid_basis(camera_axis)) {
        return false;
    }
    const auto& grip = snapshot.frame.actions.hands[
        static_cast<std::uint32_t>(hand)].grip;
    if (!grip.active || !grip.position_valid || !grip.orientation_valid ||
        !finite_vector(grip.pose.position)) {
        return false;
    }
    const auto controller = wawvr::xr::OpenXrPoseToIwRelative(
        grip.pose, snapshot.tracking_anchor, wawvr::xr::kIwUnitsPerMeter);
    if (!finite_vector(controller.position) || !valid_basis(controller.axis)) {
        return false;
    }
    const auto local_origin = add(
        controller.position,
        compose(controller.axis, asset.attachment_position));
    const auto local_axis = compose_axes(
        controller.axis, asset.attachment_axis);
    output->position = add(camera_origin, compose(camera_axis, local_origin));
    output->axis = compose_axes(camera_axis, local_axis);
    return finite_vector(output->position) && valid_basis(output->axis);
}

[[nodiscard]] bool read_authored_gripping_hand_pose(
    void* const viewmodel_dobj,
    const void* const viewmodel_pose,
    const HandAsset& asset,
    wawvr::xr::EnginePose* const output) noexcept {
    if (output == nullptr || asset.wrist_name == 0 ||
        asset.thumb_name == 0) {
        return false;
    }

    wawvr::xr::Vec3f wrist{};
    wawvr::xr::Vec3f thumb_world{};
    if (!read_viewmodel_world_tag_position(
            viewmodel_dobj, asset.wrist_name, viewmodel_pose, &wrist) ||
        !read_viewmodel_world_tag_position(
            viewmodel_dobj, asset.thumb_name, viewmodel_pose,
            &thumb_world)) {
        return false;
    }

    wawvr::xr::Vec3f finger_center{};
    int finger_count = 0;
    for (const std::uint16_t finger_name : asset.finger_names) {
        wawvr::xr::Vec3f finger_world{};
        if (finger_name == 0 ||
            !read_viewmodel_world_tag_position(
                viewmodel_dobj, finger_name, viewmodel_pose,
                &finger_world)) {
            continue;
        }
        finger_center = add(finger_center, {
            finger_world.x - wrist.x,
            finger_world.y - wrist.y,
            finger_world.z - wrist.z,
        });
        ++finger_count;
    }
    if (finger_count == 0) {
        return false;
    }
    const float inverse_count = 1.0F / static_cast<float>(finger_count);
    finger_center.x *= inverse_count;
    finger_center.y *= inverse_count;
    finger_center.z *= inverse_count;

    auto fingers_forward = finger_center;
    if (!normalize(&fingers_forward)) {
        return false;
    }
    wawvr::xr::Vec3f thumb{
        thumb_world.x - wrist.x,
        thumb_world.y - wrist.y,
        thumb_world.z - wrist.z,
    };
    const float along = dot(thumb, fingers_forward);
    wawvr::xr::Vec3f little_to_thumb{
        thumb.x - along * fingers_forward.x,
        thumb.y - along * fingers_forward.y,
        thumb.z - along * fingers_forward.z,
    };
    if (!normalize(&little_to_thumb)) {
        return false;
    }
    auto into_palm = cross(fingers_forward, little_to_thumb);
    if (!normalize(&into_palm)) {
        return false;
    }
    little_to_thumb = cross(into_palm, fingers_forward);
    if (!normalize(&little_to_thumb)) {
        return false;
    }

    const wawvr::xr::Basis3f authored_grip{
        little_to_thumb,
        {-into_palm.x, -into_palm.y, -into_palm.z},
        {-fingers_forward.x, -fingers_forward.y, -fingers_forward.z},
    };
    const wawvr::xr::Basis3f root_axis = compose_axes(
        authored_grip, asset.attachment_axis);
    if (!valid_basis(authored_grip) || !valid_basis(root_axis)) {
        return false;
    }
    output->position = wrist;
    output->axis = root_axis;
    return finite_vector(output->position);
}

[[nodiscard]] ClosedPistolHandPairAsset* resolve_closed_pistol_asset(
    XModel* const source,
    void* const viewmodel_dobj,
    const void* const viewmodel_pose,
    const HandPairAsset& ordinary_pair,
    const TrackedHandsWeaponState& weapon_state) {
    if (!weapon_state.pistol_support_pose ||
        weapon_state.pistol_weapon_identity == 0 || source == nullptr ||
        !ordinary_pair.ready || source->surfaces == nullptr ||
        source->base_matrices == nullptr) {
        return nullptr;
    }
    ClosedPistolHandPairAsset* slot = nullptr;
    for (auto& cached : g_closed_pistol_assets) {
        if (cached.source == source &&
            cached.source_surfaces == source->surfaces &&
            cached.source_bind_matrices == source->base_matrices &&
            cached.weapon_identity == weapon_state.pistol_weapon_identity) {
            slot = &cached;
            break;
        }
    }
    if (slot != nullptr && (slot->ready || slot->failed)) {
        return slot->ready ? slot : nullptr;
    }
    if (slot == nullptr) {
        for (auto& cached : g_closed_pistol_assets) {
            if (cached.source == nullptr) {
                slot = &cached;
                slot->source = source;
                slot->source_surfaces = source->surfaces;
                slot->source_bind_matrices = source->base_matrices;
                slot->weapon_identity = weapon_state.pistol_weapon_identity;
                break;
            }
        }
    }
    if (slot == nullptr) {
        WAWVR_STEREO_DIAG_ONCE(
            "HandDiag closed pistol glove cache full; existing hand presentation retained without evicting submitted geometry");
        return nullptr;
    }
    if (!weapon_state.pistol_bake_allowed || !weapon_state.weapon_pose_valid) {
        slot->idle_since_milliseconds = 0;
        return nullptr;
    }
    const std::uint64_t now = GetTickCount64();
    if (slot->idle_since_milliseconds == 0 ||
        now < slot->idle_since_milliseconds) {
        slot->idle_since_milliseconds = now;
        return nullptr;
    }
    constexpr std::uint64_t kClosedGloveIdleSettleMilliseconds = 200;
    if (now - slot->idle_since_milliseconds < kClosedGloveIdleSettleMilliseconds) {
        return nullptr;
    }
    std::array<wawvr::xr::EnginePose, wawvr::xr::kHandCount> root_world{};
    for (std::size_t hand = 0; hand < root_world.size(); ++hand) {
        if (!read_authored_gripping_hand_pose(
                viewmodel_dobj, viewmodel_pose, ordinary_pair.hands[hand],
                &root_world[hand])) {
            return nullptr;
        }
    }
    // Sample the evaluated finger bones once, while the pistol is idle. Bake
    // only private vertices into exactly the same authored anatomical root used
    // by subsequent hand placement. No native bones, meshes, or aim state are
    // changed. Occupied slots are never evicted while rendering may reference
    // their geometry; a ready slot performs no per-frame vertex writes.
    bool built = false;
    try {
        std::array<HandSkinBoneTransform, 128> bones{};
        PistolHandBakeContext context{
            .source = source,
            .viewmodel_dobj = viewmodel_dobj,
            .viewmodel_pose = viewmodel_pose,
            .bones = &bones,
        };
        constexpr auto left = static_cast<std::size_t>(wawvr::xr::Hand::Left);
        constexpr auto right = static_cast<std::size_t>(wawvr::xr::Hand::Right);
        context.root_world = root_world[left];
        const bool left_ready = build_hand_asset(
            source, true, &slot->hands[left], &context);
        context.root_world = root_world[right];
        const bool right_ready = left_ready && build_hand_asset(
            source, false, &slot->hands[right], &context);
        built = left_ready && right_ready;
    } catch (...) {
        built = false;
    }
    slot->ready = built;
    slot->failed = !built;
    stereo_diagnostic_log(
        "HandDiag closed pistol glove bake %s: identity=%llu leftTriangles=%u rightTriangles=%u; native evaluated bones read only and cached private vertices immutable",
        built ? "ready" : "rejected",
        static_cast<unsigned long long>(slot->weapon_identity),
        slot->hands[static_cast<std::size_t>(wawvr::xr::Hand::Left)].kept_triangles,
        slot->hands[static_cast<std::size_t>(wawvr::xr::Hand::Right)].kept_triangles);
    return slot->ready ? slot : nullptr;
}

[[nodiscard]] bool calculate_weapon_latched_hand_pose(
    void* const viewmodel_dobj,
    const void* const viewmodel_pose,
    const bool gripping,
    const TrackedHandsWeaponState& weapon_state,
    const wawvr::xr::EnginePose& free_pose,
    HandAsset* const asset,
    wawvr::xr::EnginePose* const output,
    const wawvr::xr::EnginePose* const display_override = nullptr) noexcept {
    if (asset == nullptr || output == nullptr) {
        return false;
    }
    *output = free_pose;
    if (!gripping) {
        asset->weapon_attachment_valid = false;
        asset->grip_started_milliseconds = 0;
        return true;
    }

    if (!weapon_state.weapon_pose_valid ||
        !valid_basis(weapon_state.weapon_axis) ||
        asset->wrist_name == 0) {
        return true;
    }

    wawvr::xr::EnginePose authored_pose{};
    if (display_override != nullptr) {
        authored_pose = *display_override;
    } else if (!read_authored_gripping_hand_pose(
            viewmodel_dobj, viewmodel_pose, *asset, &authored_pose)) {
        return true;
    }

    const std::uint64_t now = GetTickCount64();
    if (!asset->weapon_attachment_valid) {
        asset->weapon_attachment_valid = true;
        asset->grip_started_milliseconds = now;
    }

    // The extracted standalone mesh is rebased into wrist space. Align its
    // anatomical frame with T4's live wrist/finger/thumb pose so a one-hand
    // owner wraps the rifle exactly as the authored paired viewmodel does.
    // Blend only translation for a direct chest pickup; after a paired grip,
    // the authored orientation is already the visible orientation.
    constexpr float kAttachmentBlendMilliseconds = 120.0F;
    const float elapsed = now >= asset->grip_started_milliseconds
        ? static_cast<float>(now - asset->grip_started_milliseconds)
        : 0.0F;
    const float blend = (std::clamp)(
        elapsed / kAttachmentBlendMilliseconds, 0.0F, 1.0F);
    output->position = {
        free_pose.position.x +
            (authored_pose.position.x - free_pose.position.x) * blend,
        free_pose.position.y +
            (authored_pose.position.y - free_pose.position.y) * blend,
        free_pose.position.z +
            (authored_pose.position.z - free_pose.position.z) * blend,
    };
    output->axis = authored_pose.axis;
    return finite_vector(output->position);
}

[[nodiscard]] bool expose_stock_gripping_hand(
    void* const viewmodel_dobj,
    const HandAsset& asset) noexcept {
    if (viewmodel_dobj == nullptr) {
        return false;
    }
    auto* const bits_address = static_cast<std::uint8_t*>(viewmodel_dobj) +
        kT4DObjHidePartBitsOffset;
    if (!accessible_writable_range(
            bits_address, sizeof(asset.stock_bone_bits))) {
        return false;
    }
    std::array<std::uint32_t, 4> hidden{};
    std::memcpy(hidden.data(), bits_address, sizeof(hidden));
    for (std::size_t word = 0; word < hidden.size(); ++word) {
        hidden[word] &= ~asset.stock_bone_bits[word];
    }
    std::memcpy(bits_address, hidden.data(), sizeof(hidden));
    return true;
}

[[nodiscard]] bool submit_hand(
    HandAsset* const asset,
    const void* const viewmodel_pose,
    const wawvr::xr::EnginePose& desired,
    const wawvr::xr::Vec3f& lighting_origin,
    std::uint32_t* const submitted_entity) noexcept {
    if (asset == nullptr || !asset->ready || viewmodel_pose == nullptr ||
        submitted_entity == nullptr ||
        !accessible_range(viewmodel_pose, kPoseSize)) {
        return false;
    }
    float angles[3]{};
    if (!axis_to_angles(desired.axis, angles)) {
        return false;
    }
    std::memcpy(asset->pose.data(), viewmodel_pose, kPoseSize);
    const float origin[3] = {
        desired.position.x, desired.position.y, desired.position.z};
    std::memcpy(
        asset->pose.data() + kPoseOriginOffset, origin, sizeof(origin));
    std::memcpy(
        asset->pose.data() + kPoseAnglesOffset, angles, sizeof(angles));
    float light[3] = {
        lighting_origin.x, lighting_origin.y, lighting_origin.z};
    std::uint32_t entity_number = kNoSyntheticSceneLease;
    if (!find_available_synthetic_scene_slot(
            g_scene_index_pointers, &entity_number)) {
        return false;
    }
    tracked_hands_call_add_dobj_to_scene(
        asset->pose.data(), asset->dobj.data(), entity_number, light,
        kHandRenderFlags);
    *submitted_entity = entity_number;
    return true;
}

}  // namespace

TrackedHandsRuntimeInstallResult install_tracked_hands_runtime(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    TrackedHandsRuntimeInstallResult result{};
    if (g_installed.load(std::memory_order_acquire)) {
        result.status = TrackedHandsRuntimeStatus::already_installed;
        return result;
    }
    if (select_t4_layout_family(bindings.profile()) !=
        T4LayoutFamily::single_player_1_7_1263) {
        result.status = TrackedHandsRuntimeStatus::not_applicable;
        return result;
    }
    const auto dobj = bindings.module().address(
        kDObjCreateRva, kDObjCreateEpilogueOffset + kDObjCreateEpilogue.size());
    const auto add = bindings.module().address(
        kAddDObjToSceneRva,
        kAddDObjSceneIndexStoreOffset + kAddDObjSceneIndexStore.size());
    const auto strings = bindings.module().address(
        kSlFindStringRva, kSlFindStringSentinel.size());
    const auto scene_xmodel_index_pointer = bindings.module().address(
        kSceneXModelIndexPointerRva, sizeof(std::uintptr_t));
    const auto scene_dobj_index_pointer = bindings.module().address(
        kSceneDObjIndexPointerRva, sizeof(std::uintptr_t));
    if (!dobj || !add || !strings || !scene_xmodel_index_pointer ||
        !scene_dobj_index_pointer) {
        result.status = TrackedHandsRuntimeStatus::address_out_of_range;
        return result;
    }
    const auto* const dobj_bytes = reinterpret_cast<const std::uint8_t*>(*dobj);
    if (!bytes_equal(dobj_bytes, kDObjCreatePrologue) ||
        !bytes_equal(
            dobj_bytes + kDObjCreateEpilogueOffset,
            kDObjCreateEpilogue) ||
        !bytes_equal(reinterpret_cast<const void*>(*add),
                     kAddDObjToScenePrologue) ||
        !bytes_equal(
            reinterpret_cast<const void*>(
                *add + kAddDObjSceneIndexStoreOffset),
            kAddDObjSceneIndexStore) ||
        !bytes_equal(reinterpret_cast<const void*>(*strings),
                     kSlFindStringSentinel)) {
        result.status = TrackedHandsRuntimeStatus::fingerprint_mismatch;
        return result;
    }
    g_dobj_create = *dobj;
    g_add_dobj_to_scene = *add;
    g_sl_find_string = *strings;
    g_scene_index_pointers = {
        *scene_xmodel_index_pointer, *scene_dobj_index_pointer};
    g_enabled.store(true, std::memory_order_release);
    g_installed.store(true, std::memory_order_release);
    result.status = TrackedHandsRuntimeStatus::installed;
    return result;
}

void update_tracked_hands_viewmodel(
    void* const viewmodel_dobj,
    const void* const viewmodel_pose,
    const ControllerFrameSnapshot& controller,
    const wawvr::xr::Vec3f& camera_origin,
    const wawvr::xr::Basis3f& camera_axis,
    const TrackedHandsWeaponState& weapon_state) noexcept {
    if (!g_enabled.load(std::memory_order_acquire) ||
        viewmodel_dobj == nullptr || viewmodel_pose == nullptr) {
        return;
    }
    try {
        std::lock_guard<std::mutex> lock(g_asset_mutex);
        XModel* source = nullptr;
        if (!read_dobj_source_model(viewmodel_dobj, &source)) {
            return;
        }
        HandPairAsset* const pair = resolve_asset(source);
        if (pair == nullptr) {
            WAWVR_STEREO_DIAG_ONCE(
                "HandDiag rejected standalone two-hand glove extraction; gun-only fallback retained");
            return;
        }
        ClosedPistolHandPairAsset* const closed_pistol_pair =
            resolve_closed_pistol_asset(
                source, viewmodel_dobj, viewmodel_pose, *pair, weapon_state);
        bool submitted_left = false;
        bool submitted_right = false;
        std::uint32_t submitted_left_entity = kNoSyntheticSceneLease;
        std::uint32_t submitted_right_entity = kNoSyntheticSceneLease;
        wawvr::xr::EnginePose pistol_support_pose{};
        bool pistol_support_ready = false;
        if (closed_pistol_pair != nullptr && weapon_state.left_gripping &&
            weapon_state.weapon_pose_valid) {
            const auto& right_asset = pair->hands[
                static_cast<std::uint32_t>(wawvr::xr::Hand::Right)];
            const auto& left_asset = pair->hands[
                static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
            wawvr::xr::EnginePose right_wrist{};
            // Both hands share one authored surface in some models. Never
            // expose just the stock right arm here: it can also expose the
            // forward left arm. Submit two private gloves instead, using the
            // already placed weapon's wrist (never feeding back into aim).
            pistol_support_ready = right_asset.ready && left_asset.ready &&
                read_authored_gripping_hand_pose(
                    viewmodel_dobj, viewmodel_pose, right_asset, &right_wrist) &&
                calculate_pistol_support_hand_pose(
                    right_wrist, right_asset.attachment_axis,
                    right_asset.attachment_position, left_asset.attachment_axis,
                    left_asset.attachment_position, weapon_state.weapon_axis,
                    &pistol_support_pose);
        }
        const bool paired_native_grip =
            weapon_state.right_gripping && weapon_state.left_gripping &&
            (!weapon_state.pistol_support_pose || closed_pistol_pair == nullptr);
        for (const auto hand : {wawvr::xr::Hand::Left,
                                wawvr::xr::Hand::Right}) {
            const bool gripping = hand == wawvr::xr::Hand::Left
                ? weapon_state.left_gripping
                : weapon_state.right_gripping;
            const auto hand_index = static_cast<std::uint32_t>(hand);
            auto& asset = gripping && closed_pistol_pair != nullptr
                ? closed_pistol_pair->hands[hand_index]
                : pair->hands[hand_index];
            if (!gripping && closed_pistol_pair != nullptr) {
                closed_pistol_pair->hands[hand_index].weapon_attachment_valid = false;
                closed_pistol_pair->hands[hand_index].grip_started_milliseconds = 0;
            }
            // T4's stock hand model combines both arms into shared render
            // surfaces. While both grips are engaged, the authored hands are
            // the final submitted result, so avoid calculating standalone
            // controller/latched poses that would immediately be discarded.
            // If exposing the stock hand fails, retain the standalone path as
            // the fallback for this hand.
            if (paired_native_grip && expose_stock_gripping_hand(
                                          viewmodel_dobj, asset)) {
                if (hand == wawvr::xr::Hand::Left) {
                    submitted_left = true;
                } else {
                    submitted_right = true;
                }
                continue;
            }
            wawvr::xr::EnginePose free_pose{};
            const bool free_pose_valid = calculate_hand_world_pose(
                controller, hand, camera_origin, camera_axis,
                asset, &free_pose);
            wawvr::xr::EnginePose submitted_pose = free_pose;
            if (free_pose_valid) {
                if (!calculate_weapon_latched_hand_pose(
                        viewmodel_dobj, viewmodel_pose, gripping,
                        weapon_state, free_pose, &asset,
                        &submitted_pose,
                        hand == wawvr::xr::Hand::Left && pistol_support_ready
                            ? &pistol_support_pose : nullptr)) {
                    submitted_pose = free_pose;
                }
            } else if (!gripping) {
                asset.weapon_attachment_valid = false;
                asset.grip_started_milliseconds = 0;
            }
            if (!free_pose_valid) {
                continue;
            }
            std::uint32_t* const submitted_entity =
                hand == wawvr::xr::Hand::Left
                ? &submitted_left_entity
                : &submitted_right_entity;
            if (!submit_hand(
                    &asset, viewmodel_pose, submitted_pose,
                    camera_origin, submitted_entity)) {
                continue;
            }
            if (hand == wawvr::xr::Hand::Left) {
                submitted_left = true;
            } else {
                submitted_right = true;
            }
        }
        if (submitted_left_entity != kNoSyntheticSceneLease &&
            submitted_right_entity != kNoSyntheticSceneLease) {
            WAWVR_STEREO_DIAG_ONCE(
                "HandDiag standalone tracked gloves submitted through dynamic scene leases: leftEntity=%u rightEntity=%u",
                submitted_left_entity, submitted_right_entity);
        }
        if (pistol_support_ready && submitted_left) {
            WAWVR_STEREO_DIAG_ONCE(
                "HandDiag pistol support glove uses cached closed fingers beside the authored firing palm; weapon aim and grip ownership unchanged");
        } else if (paired_native_grip) {
            WAWVR_STEREO_DIAG_ONCE(
                "HandDiag paired grips use T4's native animated rifle hands");
        } else if (weapon_state.right_gripping ||
                   weapon_state.left_gripping) {
            WAWVR_STEREO_DIAG_ONCE(
                "HandDiag single gripping glove latched to its animated weapon wrist while the free glove remains controller tracked");
        }
    } catch (...) {
        WAWVR_STEREO_DIAG_ONCE(
            "HandDiag standalone tracked glove update failed; gun-only fallback retained");
    }
}

bool submit_tracked_first_person_model(
    void* const xmodel,
    const void* const viewmodel_pose,
    const wawvr::xr::EnginePose& world_pose,
    const wawvr::xr::Vec3f& lighting_origin) noexcept {
    if (!g_enabled.load(std::memory_order_acquire) || xmodel == nullptr ||
        viewmodel_pose == nullptr || !finite_vector(world_pose.position) ||
        !valid_basis(world_pose.axis) || !finite_vector(lighting_origin) ||
        !accessible_range(viewmodel_pose, kPoseSize)) {
        return false;
    }
    try {
        std::lock_guard<std::mutex> lock(g_asset_mutex);
        auto* const asset = resolve_tracked_model_asset(
            static_cast<XModel*>(xmodel));
        if (asset == nullptr) {
            return false;
        }
        float angles[3]{};
        if (!axis_to_angles(world_pose.axis, angles)) {
            return false;
        }
        std::memcpy(asset->pose.data(), viewmodel_pose, kPoseSize);
        const float origin[3] = {
            world_pose.position.x,
            world_pose.position.y,
            world_pose.position.z,
        };
        std::memcpy(
            asset->pose.data() + kPoseOriginOffset, origin, sizeof(origin));
        std::memcpy(
            asset->pose.data() + kPoseAnglesOffset, angles, sizeof(angles));
        float light[3] = {
            lighting_origin.x, lighting_origin.y, lighting_origin.z};
        std::uint32_t entity_number = kNoSyntheticSceneLease;
        if (!find_available_synthetic_scene_slot(
                g_scene_index_pointers, &entity_number)) {
            return false;
        }
        tracked_hands_call_add_dobj_to_scene(
            asset->pose.data(), asset->dobj.data(), entity_number, light,
            kHandRenderFlags);
        return true;
    } catch (...) {
        return false;
    }
}

void request_tracked_hands_shutdown() noexcept {
    g_enabled.store(false, std::memory_order_release);
}

const char* tracked_hands_runtime_status_name(
    const TrackedHandsRuntimeStatus status) noexcept {
    switch (status) {
        case TrackedHandsRuntimeStatus::installed: return "installed";
        case TrackedHandsRuntimeStatus::already_installed:
            return "already_installed";
        case TrackedHandsRuntimeStatus::not_applicable:
            return "not_applicable";
        case TrackedHandsRuntimeStatus::dependency_unavailable:
            return "dependency_unavailable";
        case TrackedHandsRuntimeStatus::rejected_wrong_profile:
            return "rejected_wrong_profile";
        case TrackedHandsRuntimeStatus::address_out_of_range:
            return "address_out_of_range";
        case TrackedHandsRuntimeStatus::fingerprint_mismatch:
            return "fingerprint_mismatch";
    }
    return "unknown";
}

}  // namespace wawvr::mod
