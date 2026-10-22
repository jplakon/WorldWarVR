#include "foliage_distance.hpp"
#include "foliage_distance_logic.hpp"
#include "peer_thread_quiescence.hpp"
#include "stereo_diagnostics.hpp"
#include "t4/bindings.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace wawvr::mod {
namespace {
constexpr std::uintptr_t kWorldSlot = 0x03BF392Cu;
constexpr std::size_t kInstanceStride = 0x5Cu;
constexpr std::size_t kModelExtent = 0xC8;
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_pair_installed{false};
bool g_identity_diagnostic{};
thread_local std::uint32_t g_validation_queries{};
bool accessible(const void* pointer, std::size_t size) noexcept {
    auto address = reinterpret_cast<std::uintptr_t>(pointer);
    if (address < 0x10000 || !size || address > UINTPTR_MAX - size) return false;
    const auto end = address + size;
    while (address < end) {
        MEMORY_BASIC_INFORMATION memory{};
        ++g_validation_queries;
        if (VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const auto p = memory.Protect & 0xFFU;
        if (p != PAGE_READONLY && p != PAGE_READWRITE && p != PAGE_WRITECOPY &&
            p != PAGE_EXECUTE_READ && p != PAGE_EXECUTE_READWRITE && p != PAGE_EXECUTE_WRITECOPY) return false;
        const std::uintptr_t next = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
        if (next <= address) return false;
        address = (std::min)(next, end);
    }
    return true;
}
template<class T> T read(std::uintptr_t address) noexcept {
    T value{}; std::memcpy(&value, reinterpret_cast<void*>(address), sizeof(value)); return value;
}
bool guarded_copy(std::uintptr_t address,void* destination,std::size_t size) noexcept {
    if (address < 0x10000 || !size || address > UINTPTR_MAX-size) return false;
#if defined(_MSC_VER)
    __try {
        std::memcpy(destination,reinterpret_cast<void*>(address),size);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
    return false;
#endif
}
template<class T> bool guarded_read(std::uintptr_t address,T& value) noexcept {
    return guarded_copy(address,&value,sizeof(value));
}
bool copy_name(std::uintptr_t address, std::string& result) {
    if (!accessible(reinterpret_cast<void*>(address), 128)) return false;
    std::array<char,128> snapshot{};
    if (!guarded_copy(address,snapshot.data(),snapshot.size())) return false;
    const auto* name = snapshot.data();
    const auto* end = static_cast<const char*>(std::memchr(name, 0, 128));
    if (!end || end == name) return false;
    result.assign(name, end); return true;
}
struct TerminalSurfaceIdentity {
    std::uint32_t surfaces{};
    std::uint16_t count{},first{};
};
struct FullSurfaceIdentity {
    bool enabled{};
    bool variant01{};
    const FullFoliageProfile* profile{};
    std::uint32_t material_array{};
    std::array<std::uint32_t,kMaximumProfileLods> ranges{},spans{};
    std::array<std::uint16_t,kMaximumProfileSurfaces> vertices{},triangles{};
    std::array<std::uint32_t,kMaximumProfileSurfaces> materials{},name_addresses{};
    std::array<std::string,kMaximumProfileSurfaces> names;
};
struct Model {
    std::uintptr_t address{}, name_address{}, terminal{};
    std::string name;
    std::uint32_t terminal_bits{};
    std::uint16_t lod_count{};
    std::uint8_t surface_count{};
    TerminalSurfaceIdentity terminal_mesh{};
    bool allow_groundcover_extension{};
    bool allow_dense_retention{};
    FullSurfaceIdentity near_mesh,canopy_mesh;
};
struct Cache {
    std::uintptr_t world{}, instances{}, world_name{};
    std::uint32_t count{};
    std::string name;
    std::vector<Model> models;
};
thread_local Cache g_cache;
thread_local bool g_active{};
thread_local std::uint32_t g_cull_changes{}, g_range_changes{}, g_retained_culls{}, g_retained_ranges{}, g_reports{};
thread_local std::uint32_t g_near_changes{};
thread_local std::uint32_t g_canopy_changes{};
bool current_world(std::uintptr_t& world, std::uintptr_t& instances, std::uint32_t& count) noexcept {
#if defined(_MSC_VER)
    // No shared fields are mutated. Cheap guarded reads fail closed if a world
    // is retired or protected; VirtualQuery's full-region scan belongs at the
    // cache-build boundary, not in every eye's frontend path.
    __try {
        world = read<std::uint32_t>(kWorldSlot);
        if (world < 0x10000) return false;
        count = read<std::uint32_t>(world + 0x25C);
        instances = read<std::uint32_t>(world + 0x2B0);
        return count && count <= 65536 && instances >= 0x10000;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
    return false;
#endif
}
bool terminal_surface_valid(std::uintptr_t model, std::uint16_t lod_count, std::uint8_t total,
    TerminalSurfaceIdentity& identity,bool& faulted) noexcept {
    faulted = false;
#if defined(_MSC_VER)
    __try {
    const auto last = model + 0x28 + (lod_count - 1U) * 0x1C;
    const auto count = read<std::uint16_t>(last + 4), first = read<std::uint16_t>(last + 6);
    const auto surfaces = read<std::uint32_t>(model + 0x20);
    if (!count || count > 32 || first + count > total) return false;
    if (!accessible(reinterpret_cast<void*>(surfaces), total * 0x40)) { faulted=true; return false; }
    std::uint32_t triangles = 0;
    for (std::uint32_t index = first; index < static_cast<std::uint32_t>(first) + count; ++index) {
        const auto surface = surfaces + index * 0x40;
        const auto vertices = read<std::uint16_t>(surface + 2), tris = read<std::uint16_t>(surface + 4);
        if (!vertices || !tris || vertices > 16384 || tris > 16384) return false;
        triangles += tris;
    }
    if (!triangles || triangles > 4096) return false;
    identity.surfaces=surfaces;identity.count=count;identity.first=first;
    return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { faulted=true; return false; }
#else
    faulted=true;return false;
#endif
}
bool capture_full_identity(Model& model,const FullFoliageProfile* profile,FullSurfaceIdentity& destination) {
    if (!profile || model.lod_count!=profile->lod_count || model.surface_count!=profile->surface_count) return true;
    FullSurfaceIdentity next;
    for (unsigned lod=0;lod<profile->lod_count;++lod) {
        if (!guarded_read(model.address+0x28+lod*0x1C,next.ranges[lod]) ||
            !guarded_read(model.address+0x2C+lod*0x1C,next.spans[lod])) return false;
    }
    if (!guarded_read(model.address+0x24,next.material_array) ||
        !accessible(reinterpret_cast<void*>(next.material_array),profile->surface_count*4) ||
        !guarded_copy(next.material_array,next.materials.data(),profile->surface_count*4)) return false;
    std::array<std::string_view,kMaximumProfileSurfaces> names{};
    for (unsigned surface=0;surface<profile->surface_count;++surface) {
        const auto address=model.terminal_mesh.surfaces+surface*0x40;
        if (!guarded_read(address+2,next.vertices[surface]) ||
            !guarded_read(address+4,next.triangles[surface]) ||
            !guarded_read(next.materials[surface],next.name_addresses[surface]) ||
            !copy_name(next.name_addresses[surface],next.names[surface])) return false;
        names[surface]=next.names[surface];
    }
    next.enabled=full_foliage_profile_matches(*profile,model.name,model.lod_count,model.surface_count,
        next.ranges,next.spans,next.vertices,next.triangles,names);
    next.variant01=model.name==kShrubNearVariantModel;
    next.profile=next.enabled?profile:nullptr;
    if (next.enabled) destination=std::move(next);
    return true;
}
bool capture_near_identity(Model& model) {
    const bool result=capture_full_identity(model,find_foliage_profile(model.name,FoliageRetention::dense),model.near_mesh);
    model.allow_dense_retention=result && model.near_mesh.enabled;
    return result;
}
bool capture_canopy_identity(Model& model) {
    return capture_full_identity(model,find_foliage_profile(model.name,FoliageRetention::canopy),model.canopy_mesh);
}
bool build_cache(std::uintptr_t world, std::uintptr_t instances, std::uint32_t count) {
    if (!accessible(reinterpret_cast<void*>(world),0x2B4)) return false;
    if (!accessible(reinterpret_cast<void*>(instances),count*kInstanceStride)) return false;
    Cache next;
    next.world = world; next.instances = instances; next.count = count;
    std::uint32_t world_name{};
    if (!guarded_read(world,world_name)) return false;
    next.world_name = world_name;
    if (!copy_name(next.world_name, next.name) || !next.name.starts_with("maps/")) return false;
    std::vector<std::uintptr_t> visited;
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t model_pointer{};
        if (!guarded_read(instances + index * kInstanceStride + 0x38,model_pointer)) return false;
        const auto model = static_cast<std::uintptr_t>(model_pointer);
        if (std::ranges::find(visited, model) != visited.end()) continue;
        visited.push_back(model);
        if (!accessible(reinterpret_cast<void*>(model), kModelExtent)) return false;
        Model candidate;
        std::uint32_t name_pointer{};
        candidate.address = model;
        if (!guarded_read(model,name_pointer) || !guarded_read(model + 0xC4,candidate.lod_count) ||
            !guarded_read(model + 6,candidate.surface_count)) return false;
        candidate.name_address = name_pointer;
        if (!copy_name(candidate.name_address, candidate.name)) return false;
        candidate.allow_groundcover_extension=foliage_model_is_groundcover(candidate.name);
        if ((!candidate.allow_groundcover_extension && !find_foliage_profile(candidate.name,FoliageRetention::canopy)) || candidate.lod_count < 1 ||
            candidate.lod_count > 4 || !candidate.surface_count) continue;
        bool surface_fault{};
        if (!terminal_surface_valid(model,candidate.lod_count,candidate.surface_count,candidate.terminal_mesh,surface_fault)) {
            if (surface_fault) return false;
            continue;
        }
        bool ordered = true;
        float previous = 0;
        for (std::uint16_t lod = 0; lod < candidate.lod_count; ++lod) {
            float distance{};
            if (!guarded_read(model + 0x28 + lod * 0x1C,distance)) return false;
            if (!std::isfinite(distance) || distance <= previous) ordered = false;
            previous = distance;
        }
        candidate.terminal = model + 0x28 + (candidate.lod_count - 1U) * 0x1C;
        if (!guarded_read(candidate.terminal,candidate.terminal_bits)) return false;
        if (!ordered || select_extended_foliage_terminal_range_bits(candidate.terminal_bits, true) == candidate.terminal_bits) continue;
        if (!capture_near_identity(candidate) || !capture_canopy_identity(candidate)) return false;
        if (!candidate.allow_groundcover_extension && !candidate.canopy_mesh.enabled) continue;
        next.models.push_back(std::move(candidate));
    }
    std::ranges::sort(next.models, {}, &Model::address); g_cache = std::move(next);
    const auto near_count=std::ranges::count_if(g_cache.models,[](const Model& model){return model.near_mesh.enabled;});
    const auto canopy_count=std::ranges::count_if(g_cache.models,[](const Model& model){return model.canopy_mesh.enabled;});
    const auto groundcover_count=std::ranges::count_if(g_cache.models,[](const Model& model){return model.allow_groundcover_extension;});
    stereo_diagnostic_log("FoliageDiag cache: world=%s instances=%u groundcoverModels=%zu shortCullCap=8000 terminalFactor=4 denseModels=%zu denseLod=0 denseEndpoint=currentTerminal canopyModels=%zu canopyLods=profile canopyCull=unchanged sharedWrites=0",
        g_cache.name.c_str(), g_cache.count,static_cast<std::size_t>(groundcover_count),static_cast<std::size_t>(near_count),static_cast<std::size_t>(canopy_count));
    return true;
}
bool guarded_full_identity(const Model& model,const FullSurfaceIdentity& identity) noexcept {
    if (!identity.enabled) return true;
    if(!identity.profile || identity.profile->lod_count!=model.lod_count || identity.profile->surface_count!=model.surface_count) return false;
#if defined(_MSC_VER)
    __try {
        if (read<std::uint32_t>(model.address+0x24)!=identity.material_array) return false;
        for (unsigned lod=0;lod<identity.profile->lod_count;++lod) {
            if (read<std::uint32_t>(model.address+0x28+lod*0x1C)!=identity.ranges[lod] ||
                read<std::uint32_t>(model.address+0x2C+lod*0x1C)!=identity.spans[lod]) return false;
        }
        for (unsigned surface=0;surface<identity.profile->surface_count;++surface) {
            const auto address=model.terminal_mesh.surfaces+surface*0x40;
            if (read<std::uint16_t>(address+2)!=identity.vertices[surface] ||
                read<std::uint16_t>(address+4)!=identity.triangles[surface] ||
                read<std::uint32_t>(identity.material_array+surface*4)!=identity.materials[surface] ||
                read<std::uint32_t>(identity.materials[surface])!=identity.name_addresses[surface] ||
                std::memcmp(reinterpret_cast<void*>(identity.name_addresses[surface]),identity.names[surface].c_str(),identity.names[surface].size()+1)) return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
    return false;
#endif
}
bool guarded_near_identity(const Model& model) noexcept { return guarded_full_identity(model,model.near_mesh); }
bool guarded_canopy_identity(const Model& model) noexcept {
    return guarded_full_identity(model,model.canopy_mesh);
}
bool guarded_identity(const Model& model) noexcept {
#if defined(_MSC_VER)
    __try {
        return read<std::uint32_t>(model.address) == model.name_address &&
            read<std::uint16_t>(model.address + 0xC4) == model.lod_count &&
            read<std::uint8_t>(model.address + 6) == model.surface_count &&
            read<std::uint32_t>(model.terminal) == model.terminal_bits &&
            read<std::uint32_t>(model.address + 0x20) == model.terminal_mesh.surfaces &&
            read<std::uint16_t>(model.terminal + 4) == model.terminal_mesh.count &&
            read<std::uint16_t>(model.terminal + 6) == model.terminal_mesh.first &&
            !std::memcmp(reinterpret_cast<void*>(model.name_address),model.name.c_str(),model.name.size()+1) &&
            guarded_near_identity(model) && guarded_canopy_identity(model);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
    return false;
#endif
}
bool guarded_world_name() noexcept {
#if defined(_MSC_VER)
    __try {
        return read<std::uint32_t>(g_cache.world) == g_cache.world_name &&
            !std::memcmp(reinterpret_cast<void*>(g_cache.world_name),g_cache.name.c_str(),g_cache.name.size()+1);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
    return false;
#endif
}
bool cache_still_owned() noexcept {
    if (!guarded_world_name()) return false;
    for (const auto& model : g_cache.models) if (!guarded_identity(model)) return false;
    return true;
}
// Integer-only, immutable thread-local lookup inside the two bridges. No float
// math, environment reads, VirtualQuery, allocation, or logging is allowed here.
const Model* find_model(std::uintptr_t address) noexcept {
    std::size_t first = 0, last = g_cache.models.size();
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        if (g_cache.models[middle].address < address) first = middle + 1; else last = middle;
    }
    return first < g_cache.models.size() && g_cache.models[first].address == address ? &g_cache.models[first] : nullptr;
}
struct Patch {
    std::uintptr_t address{}, bridge{};
    std::array<std::uint8_t,7> original{}, replacement{};
    std::size_t size{};
    DWORD original_protection{};
    bool pending{};
};
Patch g_cull, g_range;
struct PatchOperations {
    decltype(&::VirtualProtect) protect{&::VirtualProtect};
    decltype(&::FlushInstructionCache) flush{&::FlushInstructionCache};
};
bool restore_patch(Patch& patch,const PatchOperations& operations={}) noexcept {
    if (!patch.pending) return true;
    std::array<std::uint8_t,7> observed{};
    if (!patch.size || patch.size > observed.size() ||
        !accessible(reinterpret_cast<void*>(patch.address),patch.size) ||
        !guarded_copy(patch.address,observed.data(),patch.size)) return false;
    const bool already_original = !std::memcmp(observed.data(),patch.original.data(),patch.size);
    if (!already_original && std::memcmp(observed.data(),patch.replacement.data(),patch.size)) return false;
    DWORD old{},ignored{};
    if (!operations.protect(reinterpret_cast<void*>(patch.address),patch.size,PAGE_EXECUTE_READWRITE,&old)) return false;
    if (!already_original) std::memcpy(reinterpret_cast<void*>(patch.address),patch.original.data(),patch.size);
    const bool flushed = operations.flush(GetCurrentProcess(),reinterpret_cast<void*>(patch.address),patch.size) != 0;
    const bool protection = operations.protect(reinterpret_cast<void*>(patch.address),patch.size,
        patch.original_protection ? patch.original_protection : old,&ignored) != 0;
    if (flushed && protection) patch.pending=false;
    return !patch.pending;
}
bool install_pair(Patch& cull, Patch& range,const PatchOperations& operations={}) noexcept {
    const std::array<PeerThreadPatchRange,4> spans{{{cull.address,cull.size},{range.address,range.size},
        {cull.bridge,512},{range.bridge,512}}};
    SuspendedPeerThreads suspended;
    if (!suspended.suspend(spans)) return false;
    for (auto* patch : {&cull,&range}) {
        if (!accessible(reinterpret_cast<void*>(patch->address),patch->size) ||
            std::memcmp(reinterpret_cast<void*>(patch->address),patch->original.data(),patch->size)) return false;
        patch->replacement.fill(0x90); patch->replacement[0] = 0xE9;
        const auto displacement = static_cast<std::int64_t>(patch->bridge) - static_cast<std::int64_t>(patch->address+5);
        if (displacement < INT32_MIN || displacement > INT32_MAX) return false;
        const auto encoded = static_cast<std::int32_t>(displacement);
        std::memcpy(patch->replacement.data()+1,&encoded,4);
    }
    bool success = true;
    for (auto* patch : {&cull,&range}) {
        DWORD protection{},ignored{};
        if (!operations.protect(reinterpret_cast<void*>(patch->address),patch->size,PAGE_EXECUTE_READWRITE,&protection)) { success=false; break; }
        patch->original_protection = protection;
        patch->pending = true;
        g_pair_installed.store(true,std::memory_order_release);
        std::memcpy(reinterpret_cast<void*>(patch->address),patch->replacement.data(),patch->size);
        const bool flushed = operations.flush(GetCurrentProcess(),reinterpret_cast<void*>(patch->address),patch->size) != 0;
        const bool restored = operations.protect(reinterpret_cast<void*>(patch->address),patch->size,protection,&ignored) != 0;
        if (!flushed || !restored) { success=false; break; }
    }
    if (success) return true;
    for (auto* patch : {&cull,&range}) restore_patch(*patch,operations);
    g_pair_installed.store(cull.pending || range.pending,std::memory_order_release);
    return false;
}
} // namespace
extern "C" std::uintptr_t wawvr_foliage_cull_pass = 0x00732B9Bu;
extern "C" std::uintptr_t wawvr_foliage_cull_fail = 0x00732B7Fu;
extern "C" std::uintptr_t wawvr_foliage_range_continue = 0x00732CDDu;
extern "C" std::uint32_t __cdecl wawvr_select_foliage_cull_bits(std::uintptr_t model, std::uint32_t original,
    std::uint32_t distance) noexcept {
    const auto* target=g_active?find_model(model):nullptr;
    const auto selected = select_extended_foliage_cull_distance_bits(original,target && target->allow_groundcover_extension);
    if (g_identity_diagnostic) return original;
    if (selected != original) {
        ++g_cull_changes;
        if (distance >= original && distance < selected) ++g_retained_culls;
    }
    return selected;
}
extern "C" std::uint32_t __cdecl wawvr_select_foliage_range_bits(std::uintptr_t model, std::uintptr_t range,
    std::uint32_t original, std::uint32_t distance, std::uint32_t instance_cull,
    std::uint32_t native_lod_index=UINT32_MAX,std::uint32_t native_lod_count=0) noexcept {
    const auto* target = g_active ? find_model(model) : nullptr;
    const bool native_range_owned=target && native_lod_count==target->lod_count && native_lod_index<native_lod_count &&
        range==target->address+0x28+native_lod_index*0x1C;
    if (native_range_owned && target->canopy_mesh.enabled && target->canopy_mesh.profile) {
        const auto selected=select_profile_retained_range_bits(*target->canopy_mesh.profile,original,native_lod_index,instance_cull,true);
        if (g_identity_diagnostic) return original;
        if (selected!=original) ++g_canopy_changes;
        return selected;
    }
    if (native_range_owned && target->allow_dense_retention && target->near_mesh.enabled && target->near_mesh.profile &&
        native_lod_index==target->near_mesh.profile->retained_lod) {
        const auto selected=select_profile_retained_range_bits(*target->near_mesh.profile,original,native_lod_index,instance_cull,true);
        if (g_identity_diagnostic) return original;
        if (selected!=original) ++g_near_changes;
        return selected;
    }
    const auto selected = select_extended_foliage_terminal_range_bits(original,
        native_range_owned && target->allow_groundcover_extension && native_lod_index+1==native_lod_count &&
        target->terminal == range && target->terminal_bits == original &&
        instance_cull >= 0x00800000U &&
        instance_cull < kMaximumExtendedFoliageCullDistanceBits);
    if (g_identity_diagnostic) return original;
    if (selected != original) {
        ++g_range_changes;
        if (distance >= original && distance < selected) ++g_retained_ranges;
    }
    return selected;
}
#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) void wawvr_foliage_cull_bridge() noexcept {
    __asm {
        // Most models already have a sensible draw distance. Preserve their
        // native path without a helper call or SIMD spill.
        pushfd
        cmp dword ptr [esi-0Ch],00800000h
        jb unadjusted
        cmp dword ptr [esi-0Ch],045FA0000h
        jae unadjusted
        popfd
        pushfd
        pushad
        sub esp,132
        movups [esp],xmm0
        movups [esp+16],xmm1
        movups [esp+32],xmm2
        movups [esp+48],xmm3
        movups [esp+64],xmm4
        movups [esp+80],xmm5
        movups [esp+96],xmm6
        movups [esp+112],xmm7
        push dword ptr [esp+16]
        push dword ptr [esi-0Ch]
        push ebp
        call wawvr_select_foliage_cull_bits
        add esp,12
        mov [esp+128],eax
        movups xmm0,[esp]
        movups xmm1,[esp+16]
        movups xmm2,[esp+32]
        movups xmm3,[esp+48]
        movups xmm4,[esp+64]
        movups xmm5,[esp+80]
        movups xmm6,[esp+96]
        movups xmm7,[esp+112]
        add esp,132
        popad
        popfd
        // Result slot was original ESP-40. COMISS is last flag-changing op.
        comiss xmm1,dword ptr [esp-28h]
        jb passed
        jmp dword ptr [wawvr_foliage_cull_fail]
    unadjusted:
        popfd
        comiss xmm1,dword ptr [esi-0Ch]
        jb passed
        jmp dword ptr [wawvr_foliage_cull_fail]
    passed:
        jmp dword ptr [wawvr_foliage_cull_pass]
    }
}
extern "C" __declspec(naked) void wawvr_foliage_range_bridge() noexcept {
    __asm {
        // Only the displaced native range operand is read before the guarded,
        // immutable-cache selector. EBP need not be dereferenceable here.
        movss xmm1,dword ptr [ecx]
        pushfd
        pushad
        sub esp,112
        movups [esp],xmm0
        movups [esp+16],xmm2
        movups [esp+32],xmm3
        movups [esp+48],xmm4
        movups [esp+64],xmm5
        movups [esp+80],xmm6
        movups [esp+96],xmm7
        push eax
        push ebx
        push dword ptr [esi-0Ch]
        push dword ptr [esp+12]
        push dword ptr [ecx]
        push ecx
        push ebp
        call wawvr_select_foliage_range_bits
        add esp,28
        movd xmm1,eax
        movups xmm0,[esp]
        movups xmm2,[esp+16]
        movups xmm3,[esp+32]
        movups xmm4,[esp+48]
        movups xmm5,[esp+64]
        movups xmm6,[esp+80]
        movups xmm7,[esp+96]
        add esp,112
        popad
        popfd
        comiss xmm1,xmm0
        jmp dword ptr [wawvr_foliage_range_continue]
    }
}
#endif
void initialize_foliage_distance(const t4::ValidatedBindings& bindings) noexcept {
    if (g_pair_installed.load(std::memory_order_acquire)) return;
    if (bindings.profile().layout != t4::ExecutableLayoutId::t4_sp_1_7_1263) return;
    wchar_t disabled[2]{};
    if (GetEnvironmentVariableW(L"WAWVR_DISABLE_FOLIAGE_EXTENSION",disabled,2) == 1 && disabled[0] == L'1') return;
    wchar_t identity[2]{};
    g_identity_diagnostic = GetEnvironmentVariableW(L"WAWVR_FOLIAGE_IDENTITY_DIAGNOSTIC",identity,2) == 1 && identity[0] == L'1';
#if defined(_MSC_VER) && defined(_M_IX86)
    constexpr std::array<std::uint8_t,18> camera{0xA1,0x2C,0x39,0xBF,0x03,0x53,
        0x8B,0x98,0x5C,0x02,0,0,0x8B,0x80,0x9C,0x02,0,0};
    constexpr std::array<std::uint8_t,5> caller{0xE8,0x5A,0x51,0x05,0};
    if (!accessible(reinterpret_cast<void*>(0x00732925),camera.size()) ||
        std::memcmp(reinterpret_cast<void*>(0x00732925),camera.data(),camera.size()) ||
        !accessible(reinterpret_cast<void*>(0x006DD7B1),caller.size()) ||
        std::memcmp(reinterpret_cast<void*>(0x006DD7B1),caller.data(),caller.size())) {
        stereo_diagnostic_log("FoliageDiag disabled: native static-camera layout did not match"); return;
    }
    g_cull = {0x00732B79u,reinterpret_cast<std::uintptr_t>(&wawvr_foliage_cull_bridge),{0x0F,0x2F,0x4E,0xF4,0x72,0x1C},{},6};
    g_range = {0x00732CD6u,reinterpret_cast<std::uintptr_t>(&wawvr_foliage_range_bridge),{0xF3,0x0F,0x10,0x09,0x0F,0x2F,0xC8},{},7};
    const bool installed = install_pair(g_cull,g_range);
    g_enabled.store(installed,std::memory_order_release);
    g_pair_installed.store(g_cull.pending || g_range.pending,std::memory_order_release);
    stereo_diagnostic_log("FoliageDiag SP distance selectors installed=%u; sharedWrites=0 assetFiles=unchanged",installed ? 1U : 0U);
#endif
}
bool restore_foliage_distance() noexcept {
    g_enabled.store(false,std::memory_order_release);
    g_active = false;
    if (!g_pair_installed.load(std::memory_order_acquire)) return true;
#if defined(_MSC_VER) && defined(_M_IX86)
    const std::array<PeerThreadPatchRange,6> spans{{{g_cull.address,g_cull.size},{g_range.address,g_range.size},
        {g_cull.bridge,512},{g_range.bridge,512},
        {reinterpret_cast<std::uintptr_t>(&wawvr_select_foliage_cull_bits),512},
        {reinterpret_cast<std::uintptr_t>(&wawvr_select_foliage_range_bits),512}}};
    std::array<std::uintptr_t,2> returns{};
    std::size_t return_count = 0;
    for (const auto& pair : {std::pair{g_cull.bridge,reinterpret_cast<std::uintptr_t>(&wawvr_select_foliage_cull_bits)},
                            std::pair{g_range.bridge,reinterpret_cast<std::uintptr_t>(&wawvr_select_foliage_range_bits)}}) {
        if (!accessible(reinterpret_cast<void*>(pair.first),512)) return false;
        bool found = false;
        for (std::size_t offset = 0; offset + 5 <= 512; ++offset) {
            const auto instruction = pair.first + offset;
            if (read<std::uint8_t>(instruction) == 0xE8 &&
                static_cast<std::uintptr_t>(instruction + 5 + read<std::int32_t>(instruction + 1)) == pair.second) {
                returns[return_count++] = instruction + 5; found = true; break;
            }
        }
        if (!found) return false;
    }
    SuspendedPeerThreads suspended;
    if (!suspended.suspend(spans,nullptr,returns)) return false;
    bool restored = true;
    for (auto* patch : {&g_cull,&g_range}) {
        restored = restore_patch(*patch) && restored;
    }
    if (restored) g_pair_installed.store(false,std::memory_order_release);
    return restored;
#else
    return false;
#endif
}
void reset_foliage_distance_scope() noexcept { g_active = false; }
ScopedStereoFoliageDistance::ScopedStereoFoliageDistance(std::uint32_t view,std::uint64_t frame) noexcept : view_(view),frame_(frame) {
    g_active = false;
    if (!g_enabled.load(std::memory_order_acquire)) return;
    LARGE_INTEGER started{};
    QueryPerformanceCounter(&started);
    g_validation_queries = 0;
    try {
        std::uintptr_t world{},instances{};
        std::uint32_t count{};
        if (!current_world(world,instances,count)) return;
        std::uint32_t world_name{};
        if (!guarded_read(world,world_name)) return;
        if (world != g_cache.world || instances != g_cache.instances || count != g_cache.count ||
            world_name != g_cache.world_name) {
            if (!build_cache(world,instances,count)) return;
        }
        if (!cache_still_owned()) { g_cache = {}; return; }
        g_cull_changes = 0; g_range_changes = 0; g_retained_culls = 0; g_retained_ranges = 0;
        g_near_changes = 0;
        g_canopy_changes = 0;
        g_active = !g_cache.models.empty(); applied_ = g_active;
    } catch (...) { g_cache = {}; }
    LARGE_INTEGER finished{},frequency{};
    QueryPerformanceCounter(&finished); QueryPerformanceFrequency(&frequency);
    if (frequency.QuadPart > 0) prepare_us_ = static_cast<std::uint64_t>((finished.QuadPart-started.QuadPart)*1000000/frequency.QuadPart);
    native_start_ = finished.QuadPart;
    validation_queries_ = g_validation_queries;
}
ScopedStereoFoliageDistance::~ScopedStereoFoliageDistance() {
    g_active = false;
    if (applied_ && g_reports < 32 && (g_reports < 16 || frame_ % 600 == 0)) {
        ++g_reports;
        LARGE_INTEGER now{},frequency{};
        QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
        const auto native_us = frequency.QuadPart > 0
            ? static_cast<std::uint64_t>((now.QuadPart-native_start_)*1000000/frequency.QuadPart) : 0;
        stereo_diagnostic_log("FoliageDiag scope: frame=%llu view=%u cullChanges=%u terminalChanges=%u beyondOriginalCull=%u retainedTerminal=%u prepareUs=%llu nativeUs=%llu validationQueries=%u identity=%u nearChanges=%u canopyChanges=%u sharedWrites=0",
            static_cast<unsigned long long>(frame_),view_,g_cull_changes,g_range_changes,g_retained_culls,g_retained_ranges,
            static_cast<unsigned long long>(prepare_us_),static_cast<unsigned long long>(native_us),validation_queries_,g_identity_diagnostic ? 1U : 0U,g_near_changes,g_canopy_changes);
    }
}
} // namespace wawvr::mod
