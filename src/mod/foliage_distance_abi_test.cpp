// Exercise the actual production bridges with synthetic memory and redirected
// continuations. This test never initializes hooks or reads native game addresses.
#include "foliage_distance.cpp"

#include <bit>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace wawvr::mod {
void stereo_diagnostic_log(const char*, ...) noexcept {}
}

#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" {
__declspec(align(16)) unsigned char abi_caller_fx[512]{};
__declspec(align(16)) unsigned char abi_before_fx[512]{};
__declspec(align(16)) unsigned char abi_expected_fx[512]{};
__declspec(align(16)) unsigned char abi_after_fx[512]{};
__declspec(align(16)) std::uint32_t abi_seed_xmm[8][4]{};
std::uint32_t abi_before_gpr[8]{};
std::uint32_t abi_after_gpr[8]{};
std::uint32_t abi_before_flags{}, abi_expected_flags{}, abi_after_flags{};
std::uint32_t abi_caller_stack{}, abi_mode{}, abi_observed_branch{};
std::uint32_t abi_selected_bits{}, abi_seed_flags{0xAD7U};
std::uint32_t abi_seed_mxcsr{0x5F80U};
std::uint32_t abi_seed_eax{}, abi_seed_ebx{};
std::uint32_t abi_bench_remaining{};
std::uintptr_t abi_model{}, abi_instance{}, abi_range{}, abi_bridge{};
}

// Capture all state before any instruction which changes comparison flags.
extern "C" __declspec(naked) void abi_capture_and_return() noexcept {
    __asm {
        mov abi_after_gpr[0],eax
        mov abi_after_gpr[4],ecx
        mov abi_after_gpr[8],edx
        mov abi_after_gpr[12],ebx
        mov abi_after_gpr[16],esp
        mov abi_after_gpr[20],ebp
        mov abi_after_gpr[24],esi
        mov abi_after_gpr[28],edi
        pushfd
        pop abi_after_flags
        fxsave abi_after_fx
        mov esp,abi_caller_stack
        fxrstor abi_caller_fx
        popad
        popfd
        ret
    }
}

extern "C" __declspec(naked) void abi_cull_pass() noexcept {
    __asm {
        mov abi_observed_branch,1
        jmp abi_capture_and_return
    }
}
extern "C" __declspec(naked) void abi_cull_fail() noexcept {
    __asm {
        mov abi_observed_branch,2
        jmp abi_capture_and_return
    }
}
extern "C" __declspec(naked) void abi_range_return() noexcept {
    __asm {
        mov abi_observed_branch,3
        jmp abi_capture_and_return
    }
}

extern "C" __declspec(naked) void abi_run_bridge() noexcept {
    __asm {
        pushfd
        pushad
        mov abi_caller_stack,esp
        fxsave abi_caller_fx

        // Seed a nonempty x87 stack, nondefault rounding, and distinguishable
        // full SIMD lanes. The reference and production paths start identically.
        fninit
        fld1
        fldpi
        ldmxcsr abi_seed_mxcsr
        movups xmm0,abi_seed_xmm[0]
        movups xmm1,abi_seed_xmm[16]
        movups xmm2,abi_seed_xmm[32]
        movups xmm3,abi_seed_xmm[48]
        movups xmm4,abi_seed_xmm[64]
        movups xmm5,abi_seed_xmm[80]
        movups xmm6,abi_seed_xmm[96]
        movups xmm7,abi_seed_xmm[112]
        mov eax,abi_seed_eax
        mov ebx,abi_seed_ebx
        mov ecx,abi_range
        mov edx,0D0D1D2D3h
        mov esi,abi_instance
        mov edi,11223344h
        mov ebp,abi_model
        push abi_seed_flags
        popfd
        mov abi_before_gpr[0],eax
        mov abi_before_gpr[4],ecx
        mov abi_before_gpr[8],edx
        mov abi_before_gpr[12],ebx
        mov abi_before_gpr[16],esp
        mov abi_before_gpr[20],ebp
        mov abi_before_gpr[24],esi
        mov abi_before_gpr[28],edi
        pushfd
        pop abi_before_flags
        fxsave abi_before_fx

        // Reference: exactly the displaced native operations, using the known
        // policy output supplied by the test case rather than the selector.
        cmp abi_mode,0
        jne reference_range
        push abi_before_flags
        popfd
        comiss xmm1,abi_selected_bits
        jmp reference_complete
    reference_range:
        push abi_before_flags
        popfd
        movss xmm1,abi_selected_bits
        comiss xmm1,xmm0
    reference_complete:
        pushfd
        pop abi_expected_flags
        fxsave abi_expected_fx
        fxrstor abi_before_fx

        // Restore native live registers, then enter the production bridge by
        // JMP, just as an installed inline hook would. No extra return address.
        mov eax,abi_before_gpr[0]
        mov ecx,abi_before_gpr[4]
        mov edx,abi_before_gpr[8]
        mov ebx,abi_before_gpr[12]
        mov ebp,abi_before_gpr[20]
        mov esi,abi_before_gpr[24]
        mov edi,abi_before_gpr[28]
        push abi_before_flags
        popfd
        jmp dword ptr [abi_bridge]
    }
}

// Bounded timing loops share one setup/retirement cost per batch. Both the
// direct-native and production-bridge paths use these same seeded registers.
extern "C" __declspec(naked) void abi_bench_entry() noexcept {
    __asm {
        pushfd
        pushad
        mov abi_caller_stack,esp
        fxsave abi_caller_fx
        movups xmm0,abi_seed_xmm[0]
        movups xmm1,abi_seed_xmm[16]
        movups xmm2,abi_seed_xmm[32]
        movups xmm3,abi_seed_xmm[48]
        movups xmm4,abi_seed_xmm[64]
        movups xmm5,abi_seed_xmm[80]
        movups xmm6,abi_seed_xmm[96]
        movups xmm7,abi_seed_xmm[112]
        mov eax,abi_seed_eax
        mov ebx,abi_seed_ebx
        mov ecx,abi_range
        mov edx,0D0D1D2D3h
        mov esi,abi_instance
        mov edi,11223344h
        mov ebp,abi_model
        jmp dword ptr [abi_bridge]
    }
}
extern "C" __declspec(naked) void abi_bench_resume() noexcept {
    __asm {
        dec abi_bench_remaining
        jz finished
        jmp dword ptr [abi_bridge]
    finished:
        mov esp,abi_caller_stack
        fxrstor abi_caller_fx
        popad
        popfd
        ret
    }
}
extern "C" __declspec(naked) void abi_bench_native_cull() noexcept {
    __asm {
        comiss xmm1,dword ptr [esi-0Ch]
        jb passed
        jmp abi_bench_resume
    passed:
        jmp abi_bench_resume
    }
}
extern "C" __declspec(naked) void abi_bench_native_range() noexcept {
    __asm {
        movss xmm1,dword ptr [ecx]
        comiss xmm1,xmm0
        jmp abi_bench_resume
    }
}

namespace {
using namespace wawvr::mod;
__declspec(align(16)) unsigned char model_memory[0x200]{};
__declspec(align(16)) unsigned char instance_memory[0x5C]{};
unsigned checks{};

constexpr std::uint32_t bits(float value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) throw std::runtime_error(label);
}
template<class T> void store(void* address, T value) {
    std::memcpy(address, &value, sizeof(value));
}

class TestPage final {
public:
    TestPage() : memory_(static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,
        MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))) {
        check(memory_!=nullptr,"ownership fixture page allocation failed");
    }
    ~TestPage() { if (memory_) VirtualFree(memory_,0,MEM_RELEASE); }
    TestPage(const TestPage&)=delete;
    TestPage& operator=(const TestPage&)=delete;
    unsigned char* data() const noexcept { return memory_; }
    std::uintptr_t address() const noexcept { return reinterpret_cast<std::uintptr_t>(memory_); }
    void protect(DWORD protection) {
        DWORD previous{};
        check(VirtualProtect(memory_,4096,protection,&previous)!=0,
              "ownership fixture protection change failed");
    }
private:
    unsigned char* memory_{};
};

unsigned run_ownership_cases() {
    // Fault only these private pages belonging to the isolated test process.
    // No fixed renderer address, live game process, or installed hook is used.
    TestPage header,name,world,world_name;
    constexpr char model_text[]="foliage_synthetic_grass_owned";
    constexpr char world_text[]="maps/abi_ownership.d3dbsp";
    std::memcpy(name.data(),model_text,sizeof model_text);
    std::memcpy(world_name.data(),world_text,sizeof world_text);
    store(header.data(),static_cast<std::uint32_t>(name.address()));
    store(header.data()+0xC4,std::uint16_t{4});
    store(header.data()+6,std::uint8_t{4});
    store(header.data()+0x20,static_cast<std::uint32_t>(header.address()+0x100));
    store(header.data()+0x7C,bits(3000.0F));
    store(header.data()+0x80,std::uint16_t{1});
    store(header.data()+0x82,std::uint16_t{3});
    store(world.data(),static_cast<std::uint32_t>(world_name.address()));
    Model model;
    model.address=header.address();model.name_address=name.address();model.terminal=header.address()+0x7C;
    model.name=model_text;model.terminal_bits=bits(3000.0F);model.lod_count=4;model.surface_count=4;
    model.terminal_mesh={static_cast<std::uint32_t>(header.address()+0x100),1,3};
    g_active=false;g_cache={};g_validation_queries=0;
    g_cache.world=world.address();g_cache.world_name=world_name.address();g_cache.name=world_text;
    g_cache.models.push_back(model);
    unsigned cases=0;
    auto expect=[&](bool actual,bool expected,const char* label) {
        check(actual==expected,label);
        check(g_validation_queries==0,"guarded cache-hit ownership performed a VirtualQuery");
        ++cases;std::printf("PASS %s\n",label);
    };
    expect(guarded_identity(model),true,"valid guarded model identity");
    expect(guarded_world_name(),true,"valid guarded world name");
    expect(cache_still_owned(),true,"valid guarded complete cache ownership");

    store(header.data(),static_cast<std::uint32_t>(name.address()+1));
    expect(guarded_identity(model),false,"changed model name pointer rejected");
    store(header.data(),static_cast<std::uint32_t>(name.address()));
    name.data()[0]^=1;
    expect(guarded_identity(model),false,"changed model name content rejected");
    name.data()[0]^=1;
    name.data()[sizeof model_text-1]='x';
    expect(guarded_identity(model),false,"changed model name terminator rejected");
    name.data()[sizeof model_text-1]=0;
    store(header.data()+0x7C,bits(12000.0F));
    expect(guarded_identity(model),false,"changed terminal range rejected");
    store(header.data()+0x7C,bits(3000.0F));
    store(header.data()+0xC4,std::uint16_t{3});
    expect(guarded_identity(model),false,"changed model LOD count rejected");
    store(header.data()+0xC4,std::uint16_t{4});
    store(header.data()+6,std::uint8_t{5});
    expect(guarded_identity(model),false,"changed model surface count rejected");
    store(header.data()+6,std::uint8_t{4});
    store(header.data()+0x20,static_cast<std::uint32_t>(header.address()+0x140));
    expect(guarded_identity(model),false,"changed validated surface pointer rejected");
    store(header.data()+0x20,model.terminal_mesh.surfaces);
    store(header.data()+0x80,std::uint16_t{2});
    expect(guarded_identity(model),false,"changed terminal surface count rejected");
    store(header.data()+0x80,model.terminal_mesh.count);
    store(header.data()+0x82,std::uint16_t{2});
    expect(guarded_identity(model),false,"changed terminal first surface rejected");
    store(header.data()+0x82,model.terminal_mesh.first);

    header.protect(PAGE_NOACCESS);
    expect(guarded_identity(model),false,"NOACCESS model header fault rejected");
    expect(cache_still_owned(),false,"NOACCESS model header invalidates cache");
    header.protect(PAGE_READWRITE);
    name.protect(PAGE_NOACCESS);
    expect(guarded_identity(model),false,"NOACCESS private model name fault rejected");
    expect(cache_still_owned(),false,"NOACCESS private model name invalidates cache");
    name.protect(PAGE_READWRITE);

    store(world.data(),static_cast<std::uint32_t>(world_name.address()+1));
    expect(guarded_world_name(),false,"changed world name pointer rejected");
    store(world.data(),static_cast<std::uint32_t>(world_name.address()));
    world_name.data()[0]^=1;
    expect(guarded_world_name(),false,"changed world name content rejected");
    world_name.data()[0]^=1;
    world_name.protect(PAGE_NOACCESS);
    expect(guarded_world_name(),false,"NOACCESS world name fault rejected");
    expect(cache_still_owned(),false,"NOACCESS world name invalidates cache");
    world_name.protect(PAGE_READWRITE);
    world.protect(PAGE_NOACCESS);
    expect(guarded_world_name(),false,"NOACCESS world header fault rejected");
    expect(cache_still_owned(),false,"NOACCESS world header invalidates cache");
    world.protect(PAGE_READWRITE);
    expect(cache_still_owned(),true,"ownership recovered after fixture restoration");

    g_active=true;
    reset_foliage_distance_scope();
    expect(g_active,false,"scene-entry reset clears active distance scope");
    expect(wawvr_select_foliage_cull_bits(model.address,bits(600.0F),bits(1000.0F))==bits(600.0F),
           true,"reset scope keeps eligible cull native");
    expect(wawvr_select_foliage_range_bits(model.address,model.terminal,bits(3000.0F),bits(4000.0F),bits(600.0F))==bits(3000.0F),
           true,"reset scope keeps eligible terminal range native");
    reset_foliage_distance_scope();
    expect(g_active,false,"scene-entry reset is idempotent");
    expect(cache_still_owned(),true,"scene-entry reset preserves validated cache ownership");
    g_cache={};
    return cases;
}

unsigned run_cache_build_fault_cases() {
    TestPage header,name,world,world_name,surfaces,instances;
    constexpr char model_text[]="foliage_synthetic_grass_build";
    constexpr char world_text[]="maps/abi_build.d3dbsp";
    std::memcpy(name.data(),model_text,sizeof model_text);
    std::memcpy(world_name.data(),world_text,sizeof world_text);
    store(header.data(),static_cast<std::uint32_t>(name.address()));
    store(header.data()+6,std::uint8_t{4});
    store(header.data()+0x20,static_cast<std::uint32_t>(surfaces.address()));
    store(header.data()+0xC4,std::uint16_t{4});
    constexpr std::array ranges{bits(600.0F),bits(1200.0F),bits(2000.0F),bits(3000.0F)};
    for (unsigned index=0;index<ranges.size();++index) {
        store(header.data()+0x28+index*0x1C,ranges[index]);
        store(header.data()+0x2C+index*0x1C,std::uint16_t{1});
        store(header.data()+0x2E+index*0x1C,static_cast<std::uint16_t>(index));
        store(surfaces.data()+index*0x40+2,std::uint16_t{3});
        store(surfaces.data()+index*0x40+4,std::uint16_t{1});
    }
    store(instances.data()+0x38,static_cast<std::uint32_t>(header.address()));
    store(world.data(),static_cast<std::uint32_t>(world_name.address()));
    store(world.data()+0x25C,std::uint32_t{1});
    store(world.data()+0x2B0,static_cast<std::uint32_t>(instances.address()));
    g_active=false;g_cache={};
    unsigned cases=0;
    auto expect=[&](bool actual,bool expected,const char* label) {
        check(actual==expected,label);check(!g_active,"faulted cache build activated selectors");
        ++cases;std::printf("PASS %s\n",label);
    };
    expect(build_cache(world.address(),instances.address(),1),true,"valid isolated cache build");
    check(g_cache.models.size()==1 && cache_still_owned(),"built cache did not retain exact model ownership");
    const std::array audited_names{
        std::pair{"foliage_pacific_brush01",true},
        std::pair{"foliage_pacific_ground_cover01",true},
        std::pair{"foliage_pacific_brush02",false},
        std::pair{"foliage_pacific_ground_cover02",false},
        std::pair{"foliage_pacific_brush010",false},
        std::pair{"foliage_pacific_ground_cover010",false},
        std::pair{"foliage_pacific_brush01_tree",false},
        std::pair{"foliage_pacific_ground_cover01_palm",false},
        std::pair{"foliage_pacific_brush01_branch",false},
        std::pair{"foliage_pacific_ground_cover01_snapped",false},
        std::pair{"foliage_pacific_brush01_bamboo",false},
        std::pair{"foliage_other_ground_cover01",false}};
    std::array<unsigned char,0xDC> original_header{};
    std::memcpy(original_header.data(),header.data(),original_header.size());
    for (const auto& [text,eligible] : audited_names) {
        std::memset(name.data(),0,128);
        std::memcpy(name.data(),text,std::strlen(text)+1);
        expect(build_cache(world.address(),instances.address(),1),true,"audited-name cache build is safe");
        check(g_cache.models.size()==(eligible?1U:0U),"cache admitted a name outside the audited exact-name policy");
        check(cache_still_owned(),"audited-name cache lost ownership");
        check(!std::memcmp(original_header.data(),header.data(),original_header.size()),
              "audited-name cache build wrote model header/LOD data");
    }
    std::memset(name.data(),0,128);
    std::memcpy(name.data(),model_text,sizeof model_text);
    expect(build_cache(world.address(),instances.address(),1),true,"original cache identity restored after audited-name fixtures");
    const std::array pages{&world,&world_name,&instances,&header,&name,&surfaces};
    const std::array labels{"retired world rejected by cache build","retired world name rejected by cache build",
        "retired instance array rejected by cache build","retired model header rejected by cache build",
        "retired private model name rejected by cache build","retired terminal surfaces rejected by cache build"};
    for (unsigned index=0;index<pages.size();++index) {
        pages[index]->protect(PAGE_NOACCESS);
        expect(build_cache(world.address(),instances.address(),1),false,labels[index]);
        std::uint32_t value{};
        expect(guarded_read(pages[index]->address(),value),false,"guarded leaf read catches protected metadata fault");
        pages[index]->protect(PAGE_READWRITE);
    }
    expect(build_cache(world.address(),instances.address(),1),true,"cache build recovers after private fixture restoration");
    g_cache={};return cases;
}

unsigned run_shrub_near_identity_cases(bool benchmark,bool variant01=false) {
    TestPage header,name,world,world_name,surfaces,instances,handles,materials,material_names;
    const auto profile_name=variant01?kShrubNearVariantModel:kShrubNearModel;
    const auto& profile_ranges=variant01?kShrubNearVariantRanges:kShrubNearRanges;
    const auto& profile_vertices=variant01?kShrubNearVariantVertices:kShrubNearVertices;
    const auto& profile_triangles=variant01?kShrubNearVariantTriangles:kShrubNearTriangles;
    std::memcpy(name.data(),profile_name.data(),profile_name.size());
    constexpr char world_text[]="maps/pel2.d3dbsp";
    std::memcpy(world_name.data(),world_text,sizeof world_text);
    store(header.data(),static_cast<std::uint32_t>(name.address()));
    store(header.data()+6,std::uint8_t{7});store(header.data()+0xC4,std::uint16_t{4});
    store(header.data()+0x20,static_cast<std::uint32_t>(surfaces.address()));
    store(header.data()+0x24,static_cast<std::uint32_t>(handles.address()));
    for (unsigned lod=0;lod<4;++lod) {
        store(header.data()+0x28+lod*0x1C,profile_ranges[lod]);
        store(header.data()+0x2C+lod*0x1C,kShrubNearSpans[lod]);
    }
    for (unsigned surface=0;surface<7;++surface) {
        store(surfaces.data()+surface*0x40+2,profile_vertices[surface]);
        store(surfaces.data()+surface*0x40+4,profile_triangles[surface]);
        store(handles.data()+surface*4,static_cast<std::uint32_t>(materials.address()+surface*0x10));
        store(materials.data()+surface*0x10,static_cast<std::uint32_t>(material_names.address()+surface*128));
        std::memcpy(material_names.data()+surface*128,kShrubNearMaterials[surface].data(),kShrubNearMaterials[surface].size());
    }
    store(instances.data()+0x38,static_cast<std::uint32_t>(header.address()));
    store(world.data(),static_cast<std::uint32_t>(world_name.address()));
    store(world.data()+0x25C,std::uint32_t{1});store(world.data()+0x2B0,static_cast<std::uint32_t>(instances.address()));
    g_active=false;g_cache={};unsigned cases=0;
    auto rebuilt=[&]() {
        check(build_cache(world.address(),instances.address(),1),"near profile fixture build failed");
        check(g_cache.models.size()==1,"near fixture lost legacy groundcover model");
        check(g_cache.models.front().allow_groundcover_extension,"shrub fixture lost explicit groundcover policy");
        check(!g_cache.models.front().canopy_mesh.enabled,"shrub fixture gained tree canopy profile");
        check(g_cache.models.front().allow_dense_retention==g_cache.models.front().near_mesh.enabled,
            "dense permission did not require full shrub identity");
        return g_cache.models.front().near_mesh.enabled;
    };
    check(rebuilt(),"exact near profile was not admitted");
    check(g_cache.models.front().near_mesh.variant01==variant01,"wrong near variant identity admitted");
    auto owned=[&](bool expected,const char* label) {
        g_validation_queries=0;
        check(cache_still_owned()==expected,label);
        check(g_validation_queries==0,"near cache-hit identity performed VirtualQuery");
        ++cases;std::printf("PASS %s\n",label);
    };
    owned(true,"exact near profile ownership retained");
    for (unsigned lod=0;lod<4;++lod) {
        store(header.data()+0x28+lod*0x1C,profile_ranges[lod]^1U);
        owned(false,"changed near/terminal range invalidates ownership");
        store(header.data()+0x28+lod*0x1C,profile_ranges[lod]);
        store(header.data()+0x2C+lod*0x1C,kShrubNearSpans[lod]^1U);
        owned(false,"changed near/terminal span invalidates ownership");
        store(header.data()+0x2C+lod*0x1C,kShrubNearSpans[lod]);
    }
    for (unsigned surface=0;surface<7;++surface) {
        store(surfaces.data()+surface*0x40+2,static_cast<std::uint16_t>(profile_vertices[surface]^1U));
        owned(false,"changed near mesh vertices invalidates ownership");
        store(surfaces.data()+surface*0x40+2,profile_vertices[surface]);
        store(surfaces.data()+surface*0x40+4,static_cast<std::uint16_t>(profile_triangles[surface]^1U));
        owned(false,"changed near mesh triangles invalidates ownership");
        store(surfaces.data()+surface*0x40+4,profile_triangles[surface]);
        store(handles.data()+surface*4,static_cast<std::uint32_t>(materials.address()+surface*0x10+4));
        owned(false,"changed material handle invalidates ownership");
        store(handles.data()+surface*4,static_cast<std::uint32_t>(materials.address()+surface*0x10));
        store(materials.data()+surface*0x10,static_cast<std::uint32_t>(material_names.address()+surface*128+1));
        owned(false,"changed material name pointer invalidates ownership");
        store(materials.data()+surface*0x10,static_cast<std::uint32_t>(material_names.address()+surface*128));
        material_names.data()[surface*128]^=1;
        owned(false,"changed material name bytes invalidates ownership");
        material_names.data()[surface*128]^=1;
    }
    store(header.data()+0x24,static_cast<std::uint32_t>(handles.address()+4));
    owned(false,"changed material array pointer invalidates ownership");
    store(header.data()+0x24,static_cast<std::uint32_t>(handles.address()));
    for (auto* page:{&surfaces,&handles,&materials,&material_names}) {
        page->protect(PAGE_NOACCESS);owned(false,"protected near mesh/material fault invalidates ownership");
        check(!build_cache(world.address(),instances.address(),1),"protected near metadata admitted by cache build");
        page->protect(PAGE_READWRITE);
    }
    check(rebuilt(),"near profile did not recover after protected fixture restoration");
    material_names.data()[0]^=1;
    check(!rebuilt(),"same model name with wrong material was admitted to near profile");
    material_names.data()[0]^=1;check(rebuilt(),"near material restoration failed");
    store(header.data()+0x28,profile_ranges[0]^1U);
    check(!rebuilt(),"same model name with wrong range was admitted to near profile");
    store(header.data()+0x28,profile_ranges[0]);check(rebuilt(),"near range restoration failed");
    owned(true,"near ownership restored without shared field writes");
    if (benchmark) {
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        constexpr unsigned iterations=10000;
        std::array<double,3> samples{};unsigned successes=0;g_validation_queries=0;
        for (auto& sample:samples) {
            LARGE_INTEGER start{},finish{};QueryPerformanceCounter(&start);
            for (unsigned i=0;i<iterations;++i) successes+=cache_still_owned()?1U:0U;
            QueryPerformanceCounter(&finish);
            sample=static_cast<double>(finish.QuadPart-start.QuadPart)*1e9/frequency.QuadPart/iterations;
        }
        check(successes==iterations*samples.size() && g_validation_queries==0,"exact near cache benchmark lost ownership or queried regions");
        std::ranges::sort(samples);
        std::printf("BENCH exact_shrub_cached_scope variant=%s models=1 ranges=4 spans=4 meshes=7 materials=7 names=7 iterations=%u samples=3 validation_queries=0 median_ns=%.3f stereo_us=%.3f\n",
            variant01?"01":"03",iterations,samples[1],samples[1]*2/1000);
    }
    g_cache={};return cases;
}

unsigned run_canopy_identity_cases(bool benchmark) {
    TestPage header,name,world,world_name,surfaces,instances,handles,materials,material_names;
    std::memcpy(name.data(),kCanopyModel.data(),kCanopyModel.size());
    constexpr char world_text[]="maps/pel2.d3dbsp";
    std::memcpy(world_name.data(),world_text,sizeof world_text);
    store(header.data(),static_cast<std::uint32_t>(name.address()));
    store(header.data()+6,std::uint8_t{8});store(header.data()+0xC4,std::uint16_t{4});
    store(header.data()+0x20,static_cast<std::uint32_t>(surfaces.address()));
    store(header.data()+0x24,static_cast<std::uint32_t>(handles.address()));
    for (unsigned lod=0;lod<4;++lod) {
        store(header.data()+0x28+lod*0x1C,kCanopyRanges[lod]);
        store(header.data()+0x2C+lod*0x1C,kCanopySpans[lod]);
    }
    for (unsigned surface=0;surface<8;++surface) {
        store(surfaces.data()+surface*0x40+2,kCanopyVertices[surface]);
        store(surfaces.data()+surface*0x40+4,kCanopyTriangles[surface]);
        store(handles.data()+surface*4,static_cast<std::uint32_t>(materials.address()+surface*0x10));
        store(materials.data()+surface*0x10,static_cast<std::uint32_t>(material_names.address()+surface*128));
        std::memcpy(material_names.data()+surface*128,kCanopyMaterials[surface].data(),kCanopyMaterials[surface].size());
    }
    store(instances.data(),bits(7000.0F));
    store(instances.data()+0x38,static_cast<std::uint32_t>(header.address()));
    store(world.data(),static_cast<std::uint32_t>(world_name.address()));
    store(world.data()+0x25C,std::uint32_t{1});store(world.data()+0x2B0,static_cast<std::uint32_t>(instances.address()));
    g_active=false;g_cache={};unsigned cases=0;
    auto rebuild=[&](bool admitted=true) {
        check(build_cache(world.address(),instances.address(),1),"canopy fixture cache build failed");
        check(g_cache.models.size()==(admitted?1U:0U),"canopy admission did not fail closed");
        if (!admitted) return;
        const auto& model=g_cache.models.front();
        check(model.canopy_mesh.enabled && !model.near_mesh.enabled && !model.allow_groundcover_extension,
            "canopy gained shrub/groundcover policy or lost exact canopy identity");
    };
    rebuild();
    auto owned=[&](bool expected,const char* label) {
        g_validation_queries=0;check(cache_still_owned()==expected,label);
        check(g_validation_queries==0,"canopy cache-hit ownership performed VirtualQuery");
        ++cases;std::printf("PASS %s\n",label);
    };
    owned(true,"exact canopy identity retained without groundcover extension");
    for (unsigned lod=0;lod<4;++lod) {
        store(header.data()+0x28+lod*0x1C,kCanopyRanges[lod]^1U);
        owned(false,"changed canopy range invalidates identity");
        store(header.data()+0x28+lod*0x1C,kCanopyRanges[lod]);
        store(header.data()+0x2C+lod*0x1C,kCanopySpans[lod]^1U);
        owned(false,"changed canopy span invalidates identity");
        store(header.data()+0x2C+lod*0x1C,kCanopySpans[lod]);
    }
    for (unsigned surface=0;surface<8;++surface) {
        store(surfaces.data()+surface*0x40+2,static_cast<std::uint16_t>(kCanopyVertices[surface]^1U));
        owned(false,"changed canopy vertices invalidates identity");
        store(surfaces.data()+surface*0x40+2,kCanopyVertices[surface]);
        store(surfaces.data()+surface*0x40+4,static_cast<std::uint16_t>(kCanopyTriangles[surface]^1U));
        owned(false,"changed canopy triangles invalidates identity");
        store(surfaces.data()+surface*0x40+4,kCanopyTriangles[surface]);
        store(handles.data()+surface*4,static_cast<std::uint32_t>(materials.address()+surface*0x10+4));
        owned(false,"changed canopy material handle invalidates identity");
        store(handles.data()+surface*4,static_cast<std::uint32_t>(materials.address()+surface*0x10));
        store(materials.data()+surface*0x10,static_cast<std::uint32_t>(material_names.address()+surface*128+1));
        owned(false,"changed canopy material name pointer invalidates identity");
        store(materials.data()+surface*0x10,static_cast<std::uint32_t>(material_names.address()+surface*128));
        material_names.data()[surface*128]^=1;
        owned(false,"changed canopy material name bytes invalidates identity");
        material_names.data()[surface*128]^=1;
    }
    store(header.data()+0x24,static_cast<std::uint32_t>(handles.address()+4));
    owned(false,"changed canopy material array invalidates identity");
    store(header.data()+0x24,static_cast<std::uint32_t>(handles.address()));
    for (auto* page:{&surfaces,&handles,&materials,&material_names}) {
        page->protect(PAGE_NOACCESS);owned(false,"protected canopy metadata invalidates identity");
        check(!build_cache(world.address(),instances.address(),1),"protected canopy metadata admitted by build");
        page->protect(PAGE_READWRITE);
    }
    rebuild();
    material_names.data()[128]^=1;rebuild(false);
    material_names.data()[128]^=1;rebuild();
    store(header.data()+0x44,kCanopyRanges[1]^1U);rebuild(false);
    store(header.data()+0x44,kCanopyRanges[1]);rebuild();
    name.data()[kCanopyModel.size()]='x';rebuild(false);
    name.data()[kCanopyModel.size()]=0;rebuild();
    owned(true,"exact canopy identity restored without changing authored cutoff");
    check(read<std::uint32_t>(instances.address())==bits(7000.0F),"canopy cache build changed authored instance cull");
    if (benchmark) {
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        constexpr unsigned iterations=10000;std::array<double,3> samples{};unsigned successes=0;g_validation_queries=0;
        for (auto& sample:samples) {
            LARGE_INTEGER start{},finish{};QueryPerformanceCounter(&start);
            for (unsigned i=0;i<iterations;++i) successes+=cache_still_owned()?1U:0U;
            QueryPerformanceCounter(&finish);sample=static_cast<double>(finish.QuadPart-start.QuadPart)*1e9/frequency.QuadPart/iterations;
        }
        check(successes==iterations*samples.size() && g_validation_queries==0,"canopy cache benchmark lost identity");
        std::ranges::sort(samples);
        std::printf("BENCH exact_canopy_cached_scope models=1 ranges=4 spans=4 meshes=8 materials=8 names=8 iterations=%u samples=3 validation_queries=0 median_ns=%.3f stereo_us=%.3f\n",
            iterations,samples[1],samples[1]*2/1000);
    }
    g_cache={};return cases;
}

unsigned run_full_profile_identity_cases(bool benchmark) {
    unsigned cases=0;
    for(const auto& profile:kFullFoliageProfiles) {
        TestPage header,name,world,world_name,surfaces,instances,handles,materials,material_names;
        std::memcpy(name.data(),profile.name.data(),profile.name.size());
        constexpr char world_text[]="maps/abi_full_profile.d3dbsp";
        std::memcpy(world_name.data(),world_text,sizeof world_text);
        store(header.data(),static_cast<std::uint32_t>(name.address()));
        store(header.data()+6,static_cast<std::uint8_t>(profile.surface_count));
        store(header.data()+0xC4,static_cast<std::uint16_t>(profile.lod_count));
        store(header.data()+0x20,static_cast<std::uint32_t>(surfaces.address()));
        store(header.data()+0x24,static_cast<std::uint32_t>(handles.address()));
        for(unsigned lod=0;lod<profile.lod_count;++lod){
            store(header.data()+0x28+lod*0x1C,profile.ranges[lod]);
            store(header.data()+0x2C+lod*0x1C,profile.spans[lod]);
        }
        for(unsigned surface=0;surface<profile.surface_count;++surface){
            store(surfaces.data()+surface*0x40+2,profile.vertices[surface]);
            store(surfaces.data()+surface*0x40+4,profile.triangles[surface]);
            store(handles.data()+surface*4,static_cast<std::uint32_t>(materials.address()+surface*0x10));
            store(materials.data()+surface*0x10,static_cast<std::uint32_t>(material_names.address()+surface*128));
            std::memcpy(material_names.data()+surface*128,profile.materials[surface].data(),profile.materials[surface].size());
        }
        const auto cull=profile.allow_groundcover_extension?bits(6000.0F):bits(7000.0F);
        store(instances.data(),cull);store(instances.data()+0x38,static_cast<std::uint32_t>(header.address()));
        store(world.data(),static_cast<std::uint32_t>(world_name.address()));
        store(world.data()+0x25C,std::uint32_t{1});store(world.data()+0x2B0,static_cast<std::uint32_t>(instances.address()));
        g_active=false;g_cache={};
        const auto rebuild=[&](bool exact=true){
            check(build_cache(world.address(),instances.address(),1),"full profile cache build failed");
            const auto expected=exact||profile.allow_groundcover_extension?1U:0U;
            check(g_cache.models.size()==expected,"full profile admission not fail closed");
            if(g_cache.models.empty())return;
            const auto& model=g_cache.models.front();
            check(model.allow_groundcover_extension==profile.allow_groundcover_extension,"full profile changed independent cull policy");
            check(model.allow_dense_retention==(exact && profile.retention==FoliageRetention::dense),"dense permission escaped full identity");
            check(model.near_mesh.enabled==(exact && profile.retention==FoliageRetention::dense),"dense profile admission incorrect");
            check(model.canopy_mesh.enabled==(exact && profile.retention==FoliageRetention::canopy),"canopy profile admission incorrect");
            if(exact)check((model.near_mesh.enabled?model.near_mesh.profile:model.canopy_mesh.profile)==&profile,"wrong immutable full profile selected");
        };
        const auto owned=[&](bool expected){g_validation_queries=0;check(cache_still_owned()==expected,"full profile ownership result incorrect");
            check(g_validation_queries==0,"full profile cached guard performed VirtualQuery");++cases;};
        rebuild();owned(true);
        store(header.data()+0xC4,static_cast<std::uint16_t>(profile.lod_count+1));owned(false);
        store(header.data()+0xC4,static_cast<std::uint16_t>(profile.lod_count));
        store(header.data()+6,static_cast<std::uint8_t>(profile.surface_count+1));owned(false);
        store(header.data()+6,static_cast<std::uint8_t>(profile.surface_count));
        for(unsigned lod=0;lod<profile.lod_count;++lod){
            store(header.data()+0x28+lod*0x1C,profile.ranges[lod]^1U);owned(false);
            store(header.data()+0x28+lod*0x1C,profile.ranges[lod]);
            store(header.data()+0x2C+lod*0x1C,profile.spans[lod]^1U);owned(false);
            store(header.data()+0x2C+lod*0x1C,profile.spans[lod]);
        }
        for(unsigned surface=0;surface<profile.surface_count;++surface){
            store(surfaces.data()+surface*0x40+2,static_cast<std::uint16_t>(profile.vertices[surface]^1U));owned(false);
            store(surfaces.data()+surface*0x40+2,profile.vertices[surface]);
            store(surfaces.data()+surface*0x40+4,static_cast<std::uint16_t>(profile.triangles[surface]^1U));owned(false);
            store(surfaces.data()+surface*0x40+4,profile.triangles[surface]);
            store(handles.data()+surface*4,static_cast<std::uint32_t>(materials.address()+surface*0x10+4));owned(false);
            store(handles.data()+surface*4,static_cast<std::uint32_t>(materials.address()+surface*0x10));
            store(materials.data()+surface*0x10,static_cast<std::uint32_t>(material_names.address()+surface*128+1));owned(false);
            store(materials.data()+surface*0x10,static_cast<std::uint32_t>(material_names.address()+surface*128));
            material_names.data()[surface*128]^=1;owned(false);material_names.data()[surface*128]^=1;
        }
        store(header.data()+0x24,static_cast<std::uint32_t>(handles.address()+4));owned(false);
        store(header.data()+0x24,static_cast<std::uint32_t>(handles.address()));
        for(auto* page:{&surfaces,&handles,&materials,&material_names}){
            page->protect(PAGE_NOACCESS);owned(false);
            check(!build_cache(world.address(),instances.address(),1),"protected full metadata admitted");
            page->protect(PAGE_READWRITE);
        }
        rebuild();material_names.data()[0]^=1;rebuild(false);material_names.data()[0]^=1;rebuild();
        store(header.data()+0x28,profile.ranges[0]^1U);rebuild(false);store(header.data()+0x28,profile.ranges[0]);rebuild();
        name.data()[profile.name.size()]='x';rebuild(false);name.data()[profile.name.size()]=0;rebuild();owned(true);
        check(read<std::uint32_t>(instances.address())==cull,"full profile mutated native instance cull");
        if(benchmark){
            LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);constexpr unsigned iterations=10000;
            std::array<double,3> samples{};unsigned successes=0;g_validation_queries=0;
            for(auto& sample:samples){LARGE_INTEGER start{},finish{};QueryPerformanceCounter(&start);
                for(unsigned i=0;i<iterations;++i)successes+=cache_still_owned()?1U:0U;
                QueryPerformanceCounter(&finish);sample=static_cast<double>(finish.QuadPart-start.QuadPart)*1e9/frequency.QuadPart/iterations;}
            check(successes==iterations*samples.size() && g_validation_queries==0,"full profile benchmark lost identity");
            std::ranges::sort(samples);
            std::printf("BENCH exact_full_profile name=%s lods=%u surfaces=%u samples=3 validation_queries=0 median_ns=%.3f stereo_us=%.3f\n",
                profile.name.data(),profile.lod_count,profile.surface_count,samples[1],samples[1]*2/1000);
        }
        std::printf("PASS full profile identity/fault cases %s\n",profile.name.data());g_cache={};
    }
    return cases;
}

unsigned patch_protect_calls{},patch_flush_calls{};
std::array<unsigned,4> failed_protect_calls{},failed_flush_calls{};
BOOL WINAPI test_patch_protect(LPVOID address,SIZE_T size,DWORD protection,PDWORD previous) {
    const auto call=++patch_protect_calls;
    if (std::ranges::find(failed_protect_calls,call)!=failed_protect_calls.end()) {
        SetLastError(ERROR_ACCESS_DENIED);return FALSE;
    }
    return VirtualProtect(address,size,protection,previous);
}
BOOL WINAPI test_patch_flush(HANDLE process,LPCVOID address,SIZE_T size) {
    const auto call=++patch_flush_calls;
    if (std::ranges::find(failed_flush_calls,call)!=failed_flush_calls.end()) {
        SetLastError(ERROR_INVALID_FUNCTION);return FALSE;
    }
    return FlushInstructionCache(process,address,size);
}
void reset_patch_faults() {
    patch_protect_calls=patch_flush_calls=0;failed_protect_calls={};failed_flush_calls={};
}
struct PatchFixture {
    TestPage first,second;
    Patch cull,range;
    PatchFixture() {
        cull={first.address(),first.address()+0x100,{0x0F,0x2F,0x4E,0xF4,0x72,0x1C},{},6};
        range={second.address(),second.address()+0x100,{0xF3,0x0F,0x10,0x09,0x0F,0x2F,0xC8},{},7};
        std::memcpy(first.data(),cull.original.data(),cull.size);
        std::memcpy(second.data(),range.original.data(),range.size);
        first.protect(PAGE_EXECUTE_READ);second.protect(PAGE_EXECUTE_READ);
        g_pair_installed.store(false);reset_patch_faults();
    }
    ~PatchFixture() { g_pair_installed.store(false); }
};
DWORD fixture_protection(std::uintptr_t address) {
    MEMORY_BASIC_INFORMATION memory{};
    check(VirtualQuery(reinterpret_cast<void*>(address),&memory,sizeof memory)==sizeof memory,
          "patch fixture protection query failed");
    return memory.Protect;
}
unsigned run_patch_failure_cases() {
    // API seams affect only these local pages in this isolated test process.
    // Targets/bridge spans are dummy storage, never installed native addresses.
    const PatchOperations operations{&test_patch_protect,&test_patch_flush};
    unsigned cases=0;
    {
        PatchFixture fixture;
        failed_protect_calls={3,4,0,0};
        check(!install_pair(fixture.cull,fixture.range,operations),"injected second patch failure was not reported");
        check(fixture.cull.pending && !fixture.range.pending && g_pair_installed.load(),
              "failed rollback lost owned pending patch state");
        check(!std::memcmp(fixture.first.data(),fixture.cull.replacement.data(),fixture.cull.size),
              "failed rollback fixture did not retain its owned jump");
        reset_patch_faults();
        check(restore_patch(fixture.cull,operations),"pending owned jump could not be retried");
        check(!fixture.cull.pending && !std::memcmp(fixture.first.data(),fixture.cull.original.data(),fixture.cull.size),
              "retry did not restore the pending jump");
        check(fixture_protection(fixture.first.address())==PAGE_EXECUTE_READ,"rollback retry lost original page protection");
        ++cases;std::printf("PASS failed install rollback retains pending state and retries\n");
    }
    {
        PatchFixture fixture;
        failed_flush_calls={1,2,0,0};
        check(!install_pair(fixture.cull,fixture.range,operations),"injected install/rollback flush failure was not reported");
        check(fixture.cull.pending && g_pair_installed.load(),"original bytes with pending flush lost retry state");
        check(!std::memcmp(fixture.first.data(),fixture.cull.original.data(),fixture.cull.size),
              "flush-failure rollback did not restore original bytes");
        reset_patch_faults();
        check(restore_patch(fixture.cull,operations),"original-byte pending flush retry failed");
        check(patch_flush_calls==1 && patch_protect_calls==2 && !fixture.cull.pending,
              "original-byte retry skipped flush/protection or retained pending state");
        ++cases;std::printf("PASS original-byte rollback retries failed instruction flush\n");
    }
    {
        PatchFixture fixture;
        fixture.cull.pending=true;fixture.cull.original_protection=PAGE_EXECUTE_READ;
        failed_protect_calls={2,0,0,0};
        check(!restore_patch(fixture.cull,operations) && fixture.cull.pending,
              "failed original protection restore cleared pending state");
        check(fixture_protection(fixture.first.address())==PAGE_EXECUTE_READWRITE,
              "injected protection-restore failure did not retain RWX fixture");
        reset_patch_faults();
        check(restore_patch(fixture.cull,operations),"original protection restore could not be retried");
        check(fixture_protection(fixture.first.address())==PAGE_EXECUTE_READ && !fixture.cull.pending,
              "retry restored the current RWX protection instead of the original protection");
        ++cases;std::printf("PASS pending protection retry restores saved original protection\n");
    }
    {
        PatchFixture fixture;
        fixture.cull.pending=true;fixture.cull.original_protection=PAGE_EXECUTE_READ;
        fixture.first.protect(PAGE_EXECUTE_READWRITE);fixture.first.data()[0]=0xCC;
        fixture.first.protect(PAGE_EXECUTE_READ);
        check(!restore_patch(fixture.cull,operations) && fixture.cull.pending,"foreign bytes incorrectly cleared pending ownership");
        check(fixture.first.data()[0]==0xCC && patch_protect_calls==0 && patch_flush_calls==0,
              "foreign patch bytes were overwritten or reprotected");
        ++cases;std::printf("PASS foreign patch bytes remain untouched and pending\n");
    }
    return cases;
}

struct Case {
    const char* label;
    bool range;
    bool active;
    bool owned_model;
    unsigned lod;
    std::uint32_t source_bits;
    std::uint32_t distance_bits;
    std::uint32_t selected_bits;
    std::uint32_t instance_cull_bits;
    std::uint32_t mxcsr{0x5F80U};
    unsigned native_lod_count{4};
    unsigned native_lod_index{0xFFFFFFFFU};
    bool shrub_near{};
    bool near_admitted{true};
    bool inaccessible_model{};
    bool canopy{};
    bool near_variant01{};
    unsigned profile_index{UINT32_MAX};
    bool dense_permission{true};
};

Case canopy_case(const char* label,bool range,bool active,bool owned,unsigned lod,
    std::uint32_t source,std::uint32_t distance,std::uint32_t selected,std::uint32_t cull,
    unsigned count=4,unsigned index=UINT32_MAX,bool admitted=true,bool inaccessible=false) {
    Case result{label,range,active,owned,lod,source,distance,selected,cull};
    result.canopy=true;result.native_lod_count=count;result.native_lod_index=index;
    result.near_admitted=admitted;result.inaccessible_model=inaccessible;return result;
}
Case shrub01_case(const char* label,unsigned lod,std::uint32_t source,std::uint32_t selected,bool active=true) {
    Case result{label,true,active,true,lod,source,bits(1300.0F),selected,bits(15000.0F)};
    result.shrub_near=true;result.near_variant01=true;return result;
}
Case profile_case(const char* label,unsigned profile_index,bool range,unsigned lod,std::uint32_t source,
    std::uint32_t distance,std::uint32_t selected,std::uint32_t cull,bool active=true,bool owned=true,
    unsigned count=UINT32_MAX,unsigned index=UINT32_MAX,bool admitted=true,bool permission=true) {
    const auto& profile=kFullFoliageProfiles[profile_index];
    Case result{label,range,active,owned,lod,source,distance,selected,cull};
    result.profile_index=profile_index;result.shrub_near=profile.retention==FoliageRetention::dense;
    result.canopy=profile.retention==FoliageRetention::canopy;result.near_admitted=admitted;
    result.dense_permission=permission;result.native_lod_count=count==UINT32_MAX?profile.lod_count:count;
    result.native_lod_index=index;return result;
}

void run_case(const Case& test) {
    TestPage protected_model;
    if (test.inaccessible_model) protected_model.protect(PAGE_NOACCESS);
    g_active = test.active;
    g_cache = {};
    g_cull_changes = g_range_changes = g_retained_culls = g_retained_ranges = 0;
    g_near_changes = 0;
    g_canopy_changes = 0;
    const auto model_address = reinterpret_cast<std::uintptr_t>(model_memory);
    constexpr std::array<std::uint32_t,4> original_ranges{
        bits(600.0F),bits(1200.0F),bits(2000.0F),bits(3000.0F)};
    const auto* profile=test.profile_index<kFullFoliageProfiles.size()?&kFullFoliageProfiles[test.profile_index]:
        test.canopy?find_foliage_profile(kCanopyModel,FoliageRetention::canopy):test.shrub_near?
        find_foliage_profile(test.near_variant01?kShrubNearVariantModel:kShrubNearModel,FoliageRetention::dense):nullptr;
    const auto lod_count=profile?profile->lod_count:4U;
    const auto surface_count=profile?profile->surface_count:4U;
    std::memset(model_memory,0,sizeof model_memory);
    store(model_memory+6,static_cast<std::uint8_t>(surface_count));
    store(model_memory+0xC4,static_cast<std::uint16_t>(lod_count));
    for (unsigned lod=0;lod<lod_count;++lod)
        store(model_memory+0x28+lod*0x1C,profile?profile->ranges[lod]:original_ranges[lod]);
    store(instance_memory,test.instance_cull_bits);
    if (test.range) store(model_memory+0x28+test.lod*0x1C,test.source_bits);
    Model model;
    model.address=model_address;
    model.terminal=model_address+0x28+(lod_count-1)*0x1C;
    model.terminal_bits=profile?profile->ranges[lod_count-1]:bits(3000.0F);
    model.lod_count=static_cast<std::uint16_t>(lod_count);
    model.surface_count=static_cast<std::uint8_t>(surface_count);
    model.allow_groundcover_extension=!test.canopy;
    model.near_mesh.enabled=test.shrub_near && test.near_admitted;
    model.near_mesh.variant01=test.near_variant01;
    model.near_mesh.profile=model.near_mesh.enabled?profile:nullptr;
    model.allow_dense_retention=model.near_mesh.enabled && test.dense_permission;
    model.canopy_mesh.enabled=test.canopy && test.near_admitted;
    model.canopy_mesh.profile=model.canopy_mesh.enabled?profile:nullptr;
    // This is a synthetic validated-cache fixture. Native layout/cache
    // admission is covered separately; no fixed renderer address is touched.
    g_cache.models.push_back(std::move(model));

    abi_model=test.owned_model?model_address:model_address+0x100;
    if (test.inaccessible_model) abi_model=protected_model.address();
    abi_instance=reinterpret_cast<std::uintptr_t>(instance_memory)+0x0C;
    abi_range=model_address+0x28+test.lod*0x1C;
    abi_mode=test.range?1U:0U;
    // At the native range comparison EAX is numLods and EBX is the current
    // range index. Cull sites keep unrelated distinguishable register seeds.
    abi_seed_eax=test.range?test.native_lod_count:0x1A2B3C4DU;
    abi_seed_ebx=test.range?(test.native_lod_index==0xFFFFFFFFU?test.lod:test.native_lod_index):0x55667788U;
    abi_bridge=test.range?reinterpret_cast<std::uintptr_t>(&wawvr_foliage_range_bridge):
                          reinterpret_cast<std::uintptr_t>(&wawvr_foliage_cull_bridge);
    abi_selected_bits=test.selected_bits;
    abi_seed_mxcsr=test.mxcsr;
    abi_observed_branch=0;
    for (unsigned reg=0;reg<8;++reg)
        for (unsigned lane=0;lane<4;++lane)
            abi_seed_xmm[reg][lane]=0x3F800000U+(reg+1)*0x1000U+lane*0x101U;
    abi_seed_xmm[test.range?0:1][0]=test.distance_bits;
    std::memset(abi_before_fx,0,sizeof abi_before_fx);
    std::memset(abi_expected_fx,0,sizeof abi_expected_fx);
    std::memset(abi_after_fx,0,sizeof abi_after_fx);
    wawvr_foliage_cull_pass=reinterpret_cast<std::uintptr_t>(&abi_cull_pass);
    wawvr_foliage_cull_fail=reinterpret_cast<std::uintptr_t>(&abi_cull_fail);
    wawvr_foliage_range_continue=reinterpret_cast<std::uintptr_t>(&abi_range_return);
    abi_run_bridge();

    check(!std::memcmp(abi_before_gpr,abi_after_gpr,sizeof abi_before_gpr),
          "bridge changed a GPR or ESP");
    check(abi_after_flags==abi_expected_flags,"bridge comparison EFLAGS differ from native reference");
    check(!std::memcmp(abi_after_fx,abi_expected_fx,160),
          "bridge changed x87 state or MXCSR beyond native comparison");
    check(!std::memcmp(abi_after_fx+160,abi_expected_fx+160,128),
          "bridge SIMD state differs from displaced native instruction semantics");
    check(abi_observed_branch==(test.range?3U:(abi_expected_flags&1U?1U:2U)),
          "cull bridge chose the wrong original branch");
    const bool near_change=test.range && test.shrub_near && profile && test.lod==profile->retained_lod;
    const bool canopy_change=test.range && test.canopy;
    check((canopy_change?g_canopy_changes:near_change?g_near_changes:test.range?g_range_changes:g_cull_changes)==
              (test.selected_bits!=test.source_bits?1U:0U),
          "selector change counter differs from expected policy");
    check((test.range?g_cull_changes:g_range_changes)==0,
          "unrelated selector change counter was modified");
    check(near_change || g_near_changes==0,"unrelated near selector counter was modified");
    check(canopy_change || g_canopy_changes==0,"unrelated canopy selector counter was modified");
    check(!near_change || (g_range_changes==0 && g_cull_changes==0),"near selection changed legacy selector counters");
    check(!canopy_change || (g_range_changes==0 && g_cull_changes==0 && g_near_changes==0),"canopy selection changed groundcover counters");
    const bool retained=!near_change && !canopy_change && test.selected_bits!=test.source_bits &&
        test.distance_bits>=test.source_bits && test.distance_bits<test.selected_bits;
    check((test.range?g_retained_ranges:g_retained_culls)==(retained?1U:0U),
          "retention diagnostic used the wrong native distance argument");
    check((test.range?g_retained_culls:g_retained_ranges)==0,
          "unrelated retention diagnostic was modified");
    check(read<std::uint32_t>(reinterpret_cast<std::uintptr_t>(instance_memory))==test.instance_cull_bits,
          "bridge wrote synthetic instance data");
    if (test.range)
        check(read<std::uint32_t>(abi_range)==test.source_bits,"bridge wrote synthetic model data");
    if (test.range) {
        const auto* xmm1=reinterpret_cast<const std::uint32_t*>(abi_after_fx+176);
        check(xmm1[0]==test.selected_bits && xmm1[1]==0 && xmm1[2]==0 && xmm1[3]==0,
              "range MOVSS target upper lanes were not cleared");
    }
    std::printf("PASS %s\n",test.label);
}

double clock_seconds() {
    LARGE_INTEGER counter{},frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(counter.QuadPart)/static_cast<double>(frequency.QuadPart);
}
void benchmark_bridges() {
    constexpr unsigned iterations=150000;
    const std::array cases{
        Case{"bench short cull inactive",false,false,true,3,bits(600.0F),bits(1000.0F),bits(600.0F),bits(600.0F)},
        Case{"bench short cull active",false,true,true,3,bits(600.0F),bits(1000.0F),bits(2400.0F),bits(600.0F)},
        Case{"bench long cull fast path",false,true,true,3,bits(8000.0F),bits(1000.0F),bits(8000.0F),bits(8000.0F)},
        Case{"bench terminal range inactive",true,false,true,3,bits(3000.0F),bits(4000.0F),bits(3000.0F),bits(600.0F)},
        Case{"bench terminal range active",true,true,true,3,bits(3000.0F),bits(4000.0F),bits(12000.0F),bits(600.0F)},
        Case{"bench long-instance range fast path",true,true,true,3,bits(3000.0F),bits(4000.0F),bits(3000.0F),bits(8000.0F)},
        Case{"bench near-LOD range fast path",true,true,true,0,bits(600.0F),bits(500.0F),bits(600.0F),bits(600.0F)},
        Case{"bench exact shrub dense range",true,true,true,0,bits(600.0F),bits(624.0F),bits(8000.0F),bits(12000.0F),0x5F80U,4,0xFFFFFFFFU,true},
        Case{"bench shrub structural match not admitted",true,true,true,0,bits(600.0F),bits(624.0F),bits(600.0F),bits(12000.0F),0x5F80U,4,0xFFFFFFFFU,true,false},
        shrub01_case("bench exact shrubs01 dense range",0,bits(600.0F),bits(10000.0F)),
        canopy_case("bench canopy retain lowest leaf LOD1",true,true,true,1,bits(2000.0F),bits(2100.0F),bits(10000.0F),bits(7000.0F)),
        canopy_case("bench canopy native cull unchanged",false,true,true,3,bits(7000.0F),bits(6999.0F),bits(7000.0F),bits(7000.0F))
    };
    for (const auto& test:cases) {
        run_case(test);
        // Match the live eleven-model lookup size, including a successful
        // binary search at the middle of the immutable synthetic cache.
        const Model target=g_cache.models.front();
        g_cache.models.clear();
        for (unsigned index=0;index<11;++index) {
            Model model=target;
            model.address=target.address+(static_cast<int>(index)-5)*0x100;
            g_cache.models.push_back(std::move(model));
        }
        wawvr_foliage_cull_pass=reinterpret_cast<std::uintptr_t>(&abi_bench_resume);
        wawvr_foliage_cull_fail=reinterpret_cast<std::uintptr_t>(&abi_bench_resume);
        wawvr_foliage_range_continue=reinterpret_cast<std::uintptr_t>(&abi_bench_resume);
        const auto production=abi_bridge;
        const auto native=test.range?reinterpret_cast<std::uintptr_t>(&abi_bench_native_range):
                                     reinterpret_cast<std::uintptr_t>(&abi_bench_native_cull);
        std::array<double,3> production_ns{},native_ns{};
        for (unsigned sample=0;sample<3;++sample) {
            abi_bridge=native;
            abi_bench_remaining=iterations;
            auto start=clock_seconds();abi_bench_entry();
            native_ns[sample]=(clock_seconds()-start)*1e9/iterations;
            abi_bridge=production;
            abi_bench_remaining=iterations;
            start=clock_seconds();abi_bench_entry();
            production_ns[sample]=(clock_seconds()-start)*1e9/iterations;
        }
        std::ranges::sort(production_ns);std::ranges::sort(native_ns);
        std::printf("BENCH %s iterations=%u samples=3 native_ns=%.3f bridge_ns=%.3f extra_ns=%.3f\n",
            test.label,iterations,native_ns[1],production_ns[1],production_ns[1]-native_ns[1]);
    }
}

void benchmark_cached_prepare() {
    struct SyntheticModel {
        std::array<unsigned char,0xDC> header{};
        std::array<char,128> name{};
    };
    std::array<SyntheticModel,11> models{};
    std::array<unsigned char,0x2B4> world{};
    constexpr std::size_t instance_bytes=11000*kInstanceStride;
    auto* const instances=static_cast<unsigned char*>(VirtualAlloc(nullptr,instance_bytes,
        MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(instances!=nullptr,"synthetic benchmark allocation failed");
    const auto world_address=reinterpret_cast<std::uintptr_t>(world.data());
    const auto instance_address=reinterpret_cast<std::uintptr_t>(instances);
    std::uint32_t world_slot=static_cast<std::uint32_t>(world_address);
    store(world.data()+0x25C,std::uint32_t{11000});
    store(world.data()+0x2B0,static_cast<std::uint32_t>(instance_address));
    g_active=false;g_cache={};
    constexpr char world_name[]="maps/abi_cached_scope.d3dbsp";
    const auto world_name_address=reinterpret_cast<std::uintptr_t>(world_name);
    store(world.data(),static_cast<std::uint32_t>(world_name_address));
    g_cache.world=world_address;g_cache.instances=instance_address;g_cache.count=11000;
    g_cache.world_name=world_name_address;g_cache.name=world_name;
    for (unsigned index=0;index<models.size();++index) {
        auto& input=models[index];
        std::snprintf(input.name.data(),input.name.size(),"foliage_synthetic_grass07_cached_model_%02u",index);
        const auto name=reinterpret_cast<std::uintptr_t>(input.name.data());
        const auto model=reinterpret_cast<std::uintptr_t>(input.header.data());
        store(input.header.data(),static_cast<std::uint32_t>(name));
        store(input.header.data()+0xC4,std::uint16_t{4});
        store(input.header.data()+6,std::uint8_t{4});
        store(input.header.data()+0x7C,bits(3000.0F));
        Model entry;entry.address=model;entry.name_address=name;entry.terminal=model+0x7C;
        entry.name=input.name.data();entry.terminal_bits=bits(3000.0F);entry.lod_count=4;entry.surface_count=4;
        g_cache.models.push_back(std::move(entry));
    }
    auto equivalent_prepare=[&]() {
        const auto pointer=read<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&world_slot));
        const auto count=read<std::uint32_t>(pointer+0x25C);
        const auto array=read<std::uint32_t>(pointer+0x2B0);
        return count==11000 && array==instance_address && cache_still_owned();
    };
    g_validation_queries=0;
    check(equivalent_prepare(),"synthetic cached scope preparation failed");
    constexpr unsigned iterations=10000;
    unsigned successes=0;
    std::array<double,3> nanoseconds{};
    for (auto& sample:nanoseconds) {
        const auto start=clock_seconds();
        for (unsigned iteration=0;iteration<iterations;++iteration)
            successes+=equivalent_prepare()?1U:0U;
        sample=(clock_seconds()-start)*1e9/iterations;
    }
    check(successes==iterations*nanoseconds.size(),"cached scope benchmark lost ownership");
    check(g_validation_queries==0,"cached scope benchmark performed a VirtualQuery");
    std::ranges::sort(nanoseconds);
    std::printf("BENCH synthetic_cached_scope instances=11000 bytes=%zu models=11 iterations=%u samples=3 validation_queries=0 median_ns=%.3f stereo_us=%.3f\n",
        instance_bytes,iterations,nanoseconds[1],nanoseconds[1]*2/1000);
    g_cache={};VirtualFree(instances,0,MEM_RELEASE);
}
}

int main(int argc,char** argv) {
    try {
        const std::array cases{
            Case{"inactive short cull pass",false,false,true,3,bits(600.0F),bits(500.0F),bits(600.0F),bits(600.0F)},
            Case{"inactive short cull strict equality",false,false,true,3,bits(600.0F),bits(600.0F),bits(600.0F),bits(600.0F)},
            Case{"active cull fourfold below cap",false,true,true,3,bits(400.0F),bits(1000.0F),bits(1600.0F),bits(400.0F)},
            Case{"active short cull extends beyond previous cap",false,true,true,3,bits(600.0F),bits(2200.0F),bits(2400.0F),bits(600.0F)},
            Case{"active extended cull equality",false,true,true,3,bits(600.0F),bits(2400.0F),bits(2400.0F),bits(600.0F)},
            Case{"active cull original 1999 extends fourfold",false,true,true,3,bits(1999.0F),bits(7995.0F),bits(7996.0F),bits(1999.0F)},
            Case{"active cull original 2000 reaches cap",false,true,true,3,bits(2000.0F),bits(7999.0F),bits(8000.0F),bits(2000.0F)},
            Case{"active cull original 2001 capped",false,true,true,3,bits(2001.0F),bits(7999.0F),bits(8000.0F),bits(2001.0F)},
            Case{"active cull original 7999 capped",false,true,true,3,bits(7999.0F),bits(7999.0F),bits(8000.0F),bits(7999.0F)},
            Case{"active capped cull equality rejects",false,true,true,3,bits(7500.0F),bits(8000.0F),bits(8000.0F),bits(7500.0F)},
            Case{"active cull original 8000 fast path",false,true,true,3,bits(8000.0F),bits(7999.0F),bits(8000.0F),bits(8000.0F)},
            Case{"active cull original 8001 fast path",false,true,true,3,bits(8001.0F),bits(8001.0F),bits(8001.0F),bits(8001.0F)},
            Case{"active medium cull capped",false,true,true,3,bits(3000.0F),bits(5000.0F),bits(8000.0F),bits(3000.0F)},
            Case{"inactive long cull fast path",false,false,true,3,bits(3000.0F),bits(2500.0F),bits(3000.0F),bits(3000.0F)},
            Case{"active foreign model short cull",false,true,false,3,bits(600.0F),bits(1000.0F),bits(600.0F),bits(600.0F)},
            Case{"inactive cull signaling NaN",false,false,true,3,0x7F812345U,bits(500.0F),0x7F812345U,0x7F812345U},
            Case{"active cull infinity fast path",false,true,true,3,0x7F800000U,bits(500.0F),0x7F800000U,0x7F800000U},
            Case{"active cull negative fast path",false,true,true,3,bits(-1.0F),0U,bits(-1.0F),bits(-1.0F)},
            Case{"active cull subnormal DAZ fast path",false,true,true,3,1U,0U,1U,1U,0x9FC0U},
            Case{"inactive short-instance terminal range",true,false,true,3,bits(3000.0F),bits(4000.0F),bits(3000.0F),bits(600.0F)},
            Case{"active short-instance terminal range",true,true,true,3,bits(3000.0F),bits(4000.0F),bits(12000.0F),bits(600.0F)},
            Case{"active short-instance terminal equality",true,true,true,3,bits(3000.0F),bits(12000.0F),bits(12000.0F),bits(600.0F)},
            Case{"active terminal range near-distance diagnostic",true,true,true,3,bits(3000.0F),bits(1000.0F),bits(12000.0F),bits(600.0F)},
            Case{"active near LOD0 fast path",true,true,true,0,bits(600.0F),bits(500.0F),bits(600.0F),bits(600.0F)},
            Case{"active near LOD2 equality fast path",true,true,true,2,bits(2000.0F),bits(2000.0F),bits(2000.0F),bits(600.0F)},
            Case{"active terminal medium-instance extends",true,true,true,3,bits(3000.0F),bits(4000.0F),bits(12000.0F),bits(3000.0F)},
            Case{"active terminal instance 1999 extends",true,true,true,3,bits(3000.0F),bits(4000.0F),bits(12000.0F),bits(1999.0F)},
            Case{"active terminal instance 2000 extends",true,true,true,3,bits(3000.0F),bits(4000.0F),bits(12000.0F),bits(2000.0F)},
            Case{"active terminal instance 2001 extends",true,true,true,3,bits(3000.0F),bits(4000.0F),bits(12000.0F),bits(2001.0F)},
            Case{"active terminal instance 7999 extends",true,true,true,3,bits(3000.0F),bits(4000.0F),bits(12000.0F),bits(7999.0F)},
            Case{"active terminal instance 8000 fast path",true,true,true,3,bits(3000.0F),bits(4000.0F),bits(3000.0F),bits(8000.0F)},
            Case{"active terminal instance 8001 fast path",true,true,true,3,bits(3000.0F),bits(4000.0F),bits(3000.0F),bits(8001.0F)},
            Case{"active terminal subnormal-instance fast path",true,true,true,3,bits(3000.0F),bits(4000.0F),bits(3000.0F),1U,0x9FC0U},
            Case{"active terminal NaN-instance fast path",true,true,true,3,bits(3000.0F),bits(4000.0F),bits(3000.0F),0x7F812345U},
            Case{"active foreign model range",true,true,false,3,bits(3000.0F),bits(4000.0F),bits(3000.0F),bits(600.0F)},
            Case{"active changed terminal source preserved",true,true,true,3,bits(2000.0F),bits(4000.0F),bits(2000.0F),bits(600.0F)},
            Case{"inactive range signaling NaN",true,false,true,3,0x7F812345U,bits(500.0F),0x7F812345U,bits(600.0F)},
            Case{"active range signaling NaN preserved",true,true,true,3,0x7F812345U,bits(500.0F),0x7F812345U,bits(600.0F)},
            Case{"terminal register index mismatch fast path",true,true,true,3,bits(3000.0F),bits(4000.0F),bits(3000.0F),bits(600.0F),0x5F80U,4,2},
            Case{"exact shrub dense LOD0 long-cull endpoint",true,true,true,0,bits(600.0F),bits(624.0F),bits(8000.0F),bits(12000.0F),0x5F80U,4,0xFFFFFFFFU,true},
            Case{"exact shrub later LOD1 unchanged",true,true,true,1,bits(1200.0F),bits(1300.0F),bits(1200.0F),bits(12000.0F),0x5F80U,4,0xFFFFFFFFU,true},
            Case{"exact shrub later LOD2 unchanged",true,true,true,2,bits(2600.0F),bits(3000.0F),bits(2600.0F),bits(12000.0F),0x5F80U,4,0xFFFFFFFFU,true},
            Case{"exact shrub dense strict endpoint equality",true,true,true,0,bits(600.0F),bits(8000.0F),bits(8000.0F),bits(12000.0F),0x5F80U,4,0xFFFFFFFFU,true},
            Case{"inactive shrub near keeps native",true,false,true,0,bits(600.0F),bits(624.0F),bits(600.0F),bits(12000.0F),0x5F80U,4,0xFFFFFFFFU,true},
            Case{"foreign shrub near keeps native",true,true,false,0,bits(600.0F),bits(624.0F),bits(600.0F),bits(12000.0F),0x5F80U,4,0xFFFFFFFFU,true},
            Case{"unadmitted material profile keeps native",true,true,true,0,bits(600.0F),bits(624.0F),bits(600.0F),bits(12000.0F),0x5F80U,4,0xFFFFFFFFU,true,false},
            Case{"shrubs terminal long cull remains native",true,true,true,3,bits(8000.0F),bits(9000.0F),bits(8000.0F),bits(12000.0F),0x5F80U,4,0xFFFFFFFFU,true},
            Case{"shrubs terminal retains candidate01 short cull",true,true,true,3,bits(8000.0F),bits(9000.0F),bits(32000.0F),bits(6000.0F),0x5F80U,4,0xFFFFFFFFU,true},
            Case{"near register index mismatch stays native",true,true,true,0,bits(600.0F),bits(624.0F),bits(600.0F),bits(12000.0F),0x5F80U,4,2,true},
            Case{"near ECX LOD0 terminal EBX3 stays native",true,true,true,0,bits(600.0F),bits(624.0F),bits(600.0F),bits(12000.0F),0x5F80U,4,3,true},
            Case{"near native count3 index2 stays native",true,true,true,2,bits(2600.0F),bits(3000.0F),bits(2600.0F),bits(12000.0F),0x5F80U,3,2,true},
            Case{"inactive inaccessible EBP valid ECX stays native",true,false,false,0,bits(600.0F),bits(624.0F),bits(600.0F),bits(12000.0F),0x5F80U,4,0xFFFFFFFFU,true,true,true},
            Case{"active inaccessible EBP valid ECX stays native",true,true,false,0,bits(600.0F),bits(624.0F),bits(600.0F),bits(12000.0F),0x5F80U,4,0xFFFFFFFFU,true,true,true},
            Case{"near changed source stays native",true,true,true,0,0x44160001U,bits(624.0F),0x44160001U,bits(12000.0F),0x5F80U,4,0xFFFFFFFFU,true},
            Case{"near signaling NaN source stays native",true,true,true,0,0x7F812345U,bits(624.0F),0x7F812345U,bits(12000.0F),0x5F80U,4,0xFFFFFFFFU,true},
            shrub01_case("shrubs01 dense LOD0 endpoint",0,bits(600.0F),bits(10000.0F)),
            shrub01_case("shrubs01 later LOD1 unchanged",1,bits(1200.0F),bits(1200.0F)),
            shrub01_case("shrubs01 later LOD2 unchanged",2,bits(2400.0F),bits(2400.0F)),
            shrub01_case("shrubs01 original terminal unchanged",3,bits(10000.0F),bits(10000.0F)),
            shrub01_case("shrubs01 inactive keeps native",0,bits(600.0F),bits(600.0F),false),
            canopy_case("canopy LOD0 remains native",true,true,true,0,bits(1000.0F),bits(999.0F),bits(1000.0F),bits(7000.0F)),
            canopy_case("canopy lowest leaf LOD1 retained",true,true,true,1,bits(2000.0F),bits(2010.0F),bits(10000.0F),bits(7000.0F)),
            canopy_case("canopy retained LOD1 original equality",true,true,true,1,bits(2000.0F),bits(2000.0F),bits(10000.0F),bits(7000.0F)),
            canopy_case("canopy leaf retained below terminal",true,true,true,1,bits(2000.0F),bits(9999.0F),bits(10000.0F),bits(7000.0F)),
            canopy_case("canopy terminal equality still rejects",true,true,true,1,bits(2000.0F),bits(10000.0F),bits(10000.0F),bits(7000.0F)),
            canopy_case("canopy bark LOD2 source unchanged",true,true,true,2,bits(9750.0F),bits(9800.0F),bits(9750.0F),bits(7000.0F)),
            canopy_case("canopy original terminal not extended",true,true,true,3,bits(10000.0F),bits(10001.0F),bits(10000.0F),bits(7000.0F)),
            canopy_case("canopy author cull7000 remains native",false,true,true,3,bits(7000.0F),bits(6999.0F),bits(7000.0F),bits(7000.0F)),
            canopy_case("canopy author cull equality rejects",false,true,true,3,bits(7000.0F),bits(7000.0F),bits(7000.0F),bits(7000.0F)),
            canopy_case("canopy even short cull remains native",false,true,true,3,bits(1000.0F),bits(1001.0F),bits(1000.0F),bits(1000.0F)),
            canopy_case("inactive canopy range remains native",true,false,true,1,bits(2000.0F),bits(2100.0F),bits(2000.0F),bits(7000.0F)),
            canopy_case("foreign canopy range remains native",true,true,false,1,bits(2000.0F),bits(2100.0F),bits(2000.0F),bits(7000.0F)),
            canopy_case("unadmitted canopy profile remains native",true,true,true,1,bits(2000.0F),bits(2100.0F),bits(2000.0F),bits(7000.0F),4,UINT32_MAX,false),
            canopy_case("canopy native index mismatch remains native",true,true,true,1,bits(2000.0F),bits(2100.0F),bits(2000.0F),bits(7000.0F),4,3),
            canopy_case("canopy native count mismatch remains native",true,true,true,1,bits(2000.0F),bits(2100.0F),bits(2000.0F),bits(7000.0F),3,1),
            canopy_case("canopy changed source remains native",true,true,true,1,0x44FA0001U,bits(2100.0F),0x44FA0001U,bits(7000.0F)),
            canopy_case("canopy invalid source preserves FP state",true,true,true,1,0x7F812345U,bits(2100.0F),0x7F812345U,bits(7000.0F)),
            canopy_case("canopy inactive NOACCESS EBP remains native",true,false,false,1,bits(2000.0F),bits(2100.0F),bits(2000.0F),bits(7000.0F),4,UINT32_MAX,true,true),
            canopy_case("canopy active NOACCESS EBP remains native",true,true,false,1,bits(2000.0F),bits(2100.0F),bits(2000.0F),bits(7000.0F),4,UINT32_MAX,true,true),
            // New exact terminal leaf-loss profiles also participate in every
            // generic full-identity mutation/fault and actual ABI loop below.
            profile_case("normal mgrve1 measured far LOD0 pinned",8,true,0,bits(1000.0F),bits(1056.58F),bits(10000.0F),bits(6500.0F)),
            profile_case("normal mgrve1 measured near LOD0 pinned",8,true,0,bits(1000.0F),bits(965.16F),bits(10000.0F),bits(6500.0F)),
            profile_case("normal mgrve1 authored endpoint strict equality",8,true,0,bits(1000.0F),bits(10000.0F),bits(10000.0F),bits(6500.0F)),
            profile_case("normal mgrve1 later LOD2 comparison unchanged",8,true,2,bits(9750.0F),bits(9750.0F),bits(9750.0F),bits(6500.0F)),
            profile_case("normal mgrve1 original terminal cannot extend",8,true,3,bits(10000.0F),bits(10000.0F),bits(10000.0F),bits(600.0F)),
            profile_case("normal mgrve1 authored cull6500 unchanged",8,false,3,bits(6500.0F),bits(6500.0F),bits(6500.0F),bits(6500.0F)),
            profile_case("normal mgrve1 unadmitted near default deny",8,true,0,bits(1000.0F),bits(1056.58F),bits(1000.0F),bits(6500.0F),true,true,UINT32_MAX,UINT32_MAX,false),
            profile_case("mgrve1 noshadow lowest leaf LOD2 retains authored endpoint",9,true,2,bits(9750.0F),bits(9999.0F),bits(10000.0F),bits(7000.0F)),
            profile_case("birch03 lowest leaf LOD2 retains authored endpoint",10,true,2,bits(15000.0F),bits(15500.0F),bits(16000.0F),bits(7000.0F)),
            profile_case("birch03 strict authored equality remains reject",10,true,2,bits(15000.0F),bits(16000.0F),bits(16000.0F),bits(7000.0F)),
            profile_case("birch03 original terminal cannot extend",10,true,3,bits(16000.0F),bits(16000.0F),bits(16000.0F),bits(6000.0F)),
            profile_case("mgrve1 source LOD1 remains native",8,true,1,bits(2000.0F),bits(2000.0F),bits(2000.0F),bits(7000.0F)),
            profile_case("new canopy short instance cull remains native",10,false,3,bits(7000.0F),bits(7000.0F),bits(7000.0F),bits(7000.0F)),
        };
        for (const auto& test:cases) run_case(test);
        for(const auto mxcsr:{0x1F80U,0x3F80U,0x5F80U,0x7F80U,0x9FC0U}){
            auto state_case=profile_case("normal mgrve1 pinned detail preserves native FP/GPR/flags state",8,true,0,
                bits(1000.0F),bits(1056.58F),bits(10000.0F),bits(6500.0F));
            state_case.mxcsr=mxcsr;run_case(state_case);
        }
        unsigned full_profile_bridge_cases=0;
        for(unsigned profile_index=0;profile_index<kFullFoliageProfiles.size();++profile_index){
            const auto& profile=kFullFoliageProfiles[profile_index];
            for(const auto cull:{bits(6000.0F),bits(7999.0F),bits(8000.0F),bits(15000.0F),
                0U,1U,0x80000000U,0xBF800000U,0x7F800000U,0x7F812345U}){
                const bool valid_instance=cull>=0x00800000U && cull<0x7F800000U;
                const bool short_instance=profile.allow_groundcover_extension && cull>=0x00800000U && cull<bits(8000.0F);
                const auto endpoint=profile.ranges[profile.lod_count-1]+(short_instance?0x01000000U:0U);
                for(unsigned lod=0;lod<profile.lod_count;++lod){
                    const auto selected=(valid_instance && lod==profile.retained_lod) || lod+1==profile.lod_count?endpoint:profile.ranges[lod];
                    for(const auto distance:{endpoint-1U,endpoint,endpoint+1U}){
                        run_case(profile_case("full profile actual x86 endpoint/strict equality",profile_index,true,lod,
                            profile.ranges[lod],distance,selected,cull));++full_profile_bridge_cases;
                    }
                }
            }
            const auto lod=profile.retained_lod,original=profile.ranges[lod];
            for(auto negative:{
                profile_case("full profile inactive native-equivalence",profile_index,true,lod,original,bits(1700.0F),original,bits(6000.0F),false),
                profile_case("full profile foreign model native-equivalence",profile_index,true,lod,original,bits(1700.0F),original,bits(6000.0F),true,false),
                profile_case("full profile wrong native count",profile_index,true,lod,original,bits(1700.0F),original,bits(6000.0F),true,true,profile.lod_count-1),
                profile_case("full profile wrong native index",profile_index,true,lod,original,bits(1700.0F),original,bits(6000.0F),true,true,profile.lod_count,(lod+1)%profile.lod_count),
                profile_case("full profile unadmitted identity",profile_index,true,lod,original,bits(1700.0F),original,bits(6000.0F),true,true,UINT32_MAX,UINT32_MAX,false),
                profile_case("full profile changed source",profile_index,true,lod,original^1U,bits(1700.0F),original^1U,bits(6000.0F)),
                profile_case("full profile signaling NaN native FP preservation",profile_index,true,lod,0x7F812345U,bits(1700.0F),0x7F812345U,bits(6000.0F))}){
                run_case(negative);++full_profile_bridge_cases;
            }
            if(profile.retention==FoliageRetention::dense){
                run_case(profile_case("dense permission false despite profile identity",profile_index,true,lod,original,bits(1700.0F),original,
                    bits(6000.0F),true,true,UINT32_MAX,UINT32_MAX,true,false));++full_profile_bridge_cases;
            }
            auto inaccessible=profile_case("full profile NOACCESS foreign EBP valid ECX",profile_index,true,lod,original,bits(1700.0F),original,bits(6000.0F),true,false);
            inaccessible.inaccessible_model=true;run_case(inaccessible);++full_profile_bridge_cases;
            const auto cull=profile.allow_groundcover_extension?bits(6000.0F):bits(7000.0F);
            run_case(profile_case("full profile independent native instance cull policy",profile_index,false,profile.lod_count-1,cull,cull,
                profile.allow_groundcover_extension?bits(8000.0F):cull,cull));++full_profile_bridge_cases;
            std::printf("PASS full profile native x86 fixtures %s\n",profile.name.data());
        }
        constexpr std::array<std::uint32_t,13> non_normal_bits{
            0x00000000U,0x80000000U,0x00000001U,0x007FFFFFU,
            0x80000001U,0x807FFFFFU,0xBF800000U,0x7F800000U,
            0xFF800000U,0x7F800001U,0x7FC12345U,0xFF800001U,0xFFC12345U};
        for (const auto value : non_normal_bits) {
            run_case(Case{"active invalid cull preserves exact bits and native FP state",false,true,true,3,
                value,bits(500.0F),value,value});
            run_case(Case{"active terminal invalid instance gate preserves native range",true,true,true,3,
                bits(3000.0F),bits(4000.0F),bits(3000.0F),value});
            run_case(Case{"active invalid terminal preserves exact bits and native FP state",true,true,true,3,
                value,bits(500.0F),value,bits(600.0F)});
        }
        const auto ownership_cases=run_ownership_cases();
        const auto build_fault_cases=run_cache_build_fault_cases();
        const auto near_identity_cases=run_shrub_near_identity_cases(argc==2 && std::string_view(argv[1])=="--benchmark");
        const auto near_variant_cases=run_shrub_near_identity_cases(argc==2 && std::string_view(argv[1])=="--benchmark",true);
        const auto canopy_identity_cases=run_canopy_identity_cases(argc==2 && std::string_view(argv[1])=="--benchmark");
        const auto full_identity_cases=run_full_profile_identity_cases(argc==2 && std::string_view(argv[1])=="--benchmark");
        const auto patch_failure_cases=run_patch_failure_cases();
        std::printf("checks=%u failures=0 bridge_cases=%zu ownership_cases=%u cache_build_cases=%u near_identity_cases=%u near_variant_cases=%u canopy_identity_cases=%u patch_failure_cases=%u\n",
            checks,cases.size()+non_normal_bits.size()*3U+5U,ownership_cases,build_fault_cases,near_identity_cases,near_variant_cases,canopy_identity_cases,patch_failure_cases);
        std::printf("full_profile_bridge_cases=%u full_profile_identity_cases=%u profiles=%zu\n",full_profile_bridge_cases,full_identity_cases,kFullFoliageProfiles.size());
        if (argc==2 && std::string_view(argv[1])=="--benchmark") {
            benchmark_bridges();benchmark_cached_prepare();
            std::printf("BENCH limitations=synthetic_cpu_only warm_cache no_live_game no_gpu no_frame_pacing_proof\n");
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what());
        return 1;
    }
}
#else
int main() {
    std::fprintf(stderr,"Win32 MSVC is required for native foliage bridge ABI validation.\n");
    return 2;
}
#endif
