// SPDX-License-Identifier: GPL-3.0-only
#include "finite_aim_grant_logic.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
using namespace wawvr::mod;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void paths(){
    for(auto path:{L"C:\\case\\commands.txt",L"d:/case/commands.txt",L"Z:\\x"})
        check(finite_aim_local_command_path_valid(path),"valid local absolute path rejected");
    for(auto path:{L"",L"C:",L"C:x",L"\\\\host\\commands.txt",L"\\\\?\\C:\\commands.txt",L"/case/x",L"C:\\x:ads",L"C:\\x\n"})
        check(!finite_aim_local_command_path_valid(path),"unsafe/nonlocal path allowed");
    std::wstring long_path=L"C:\\";long_path.append(1021,L'x');
    check(!finite_aim_local_command_path_valid(long_path),"oversized path allowed");
    const wchar_t nul_path[]{L'C',L':',L'\\',L'x',L'\0',L'y'};
    check(!finite_aim_local_command_path_valid({nul_path,6}),"embedded null allowed");
}
void gates(){
    FiniteAimGrantGate base{0,true,true,true,true,true,true,0,10};
    check(finite_aim_grant_allowed(base),"eligible simulator gate rejected");
    for(unsigned i=0;i<6;++i){auto g=base;bool* gates[]{&g.single_player,&g.command_path_bound,&g.actual_simulator_loaded,
        &g.post_com_frame,&g.presentation_valid,&g.bindings_ready};*gates[i]=false;
        check(!finite_aim_grant_allowed(g),"missing mutation gate allowed");}
    for(unsigned bits:{1U,2U,4U,0xFFFFFFFFU}){auto g=base;g.key_catchers=bits;check(!finite_aim_grant_allowed(g),"UI-owned mutation allowed");}
    for(int connection:{-1,0,1,9,11}){auto g=base;g.connection_state=connection;check(!finite_aim_grant_allowed(g),"inactive mutation allowed");}
    base.catalogue_index=kInvalidAllWeaponAimTestIndex;check(!finite_aim_grant_allowed(base),"arbitrary request index allowed");
}
void identities(){
    for(std::size_t request=0;request<kAllWeaponAimTestDefinitions.size();++request){
        const auto r=static_cast<std::uint16_t>(request);
        FiniteAimRegisteredIdentity a{kAllWeaponAimTestDefinitions[request].weapon,127,127,0x10000,0x20000,127,127,1024};
        check(finite_aim_registered_identity_valid(r,a),"finite catalogue identity rejected");
        check(finite_aim_registered_identity_same(r,a,a),"same identity rejected");
        FiniteAimAmmoPlan plan{};check(finite_aim_ammo_plan(r,a,&plan),"valid MG refill rejected");
        check(plan.clip==1024&&plan.reserve==8192&&plan.ammo_index==127&&plan.clip_index==127,"refill bounds wrong");
        auto b=a;b.weapon="not_in_catalogue";check(!finite_aim_registered_identity_same(r,a,b),"name-only mismatch allowed");
        b=a;++b.definition;check(!finite_aim_registered_identity_same(r,a,b),"definition changed allowed");
        b=a;++b.model;check(!finite_aim_registered_identity_same(r,a,b),"model changed allowed");
        b=a;--b.index;check(!finite_aim_registered_identity_same(r,a,b),"native index changed allowed");
    }
    FiniteAimRegisteredIdentity a{kAllWeaponAimTestDefinitions[0].weapon,1,127,1,2,0,0,1};
    for(int size:{-1,0,1025,2147483647}){auto b=a;b.clip_size=size;FiniteAimAmmoPlan out{8,9,10,11};
        check(!finite_aim_ammo_plan(0,b,&out),"invalid clip allowed");check(out.reserve==10&&out.clip==11,"failed plan changed output");}
    for(int index:{-1,128,2147483647}){auto b=a;b.ammo_index=index;check(!finite_aim_registered_identity_valid(0,b),"invalid ammo pool allowed");
        b=a;b.clip_index=index;check(!finite_aim_registered_identity_valid(0,b),"invalid clip pool allowed");}
    for(unsigned index:{0U,128U,0xFFFFFFFFU}){auto b=a;b.index=index;check(!finite_aim_registered_identity_valid(0,b),"invalid native index allowed");}
    auto b=a;b.last_index=0;check(!finite_aim_registered_identity_valid(0,b),"unregistered native index allowed");
    b=a;b.definition=0;check(!finite_aim_registered_identity_valid(0,b),"zero definition allowed");
    b=a;b.model=0;check(!finite_aim_registered_identity_valid(0,b),"missing viewmodel allowed");
    check(!finite_aim_ammo_plan(0,a,nullptr),"null output allowed");
    check(!finite_aim_registered_identity_valid(kInvalidAllWeaponAimTestIndex,a),"arbitrary index allowed");
}
}
int main(){try{paths();gates();identities();std::cout<<checks<<" finite grant policy checks passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
