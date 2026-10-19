// SPDX-License-Identifier: GPL-3.0-only
#include "server_impact_identity_logic.hpp"
#include <cstdio>
int main() {
    using wawvr::mod::server_impact_definition_identity_matches;
    unsigned failures{};
    const auto check=[&](bool pass,const char* name) {
        if(!pass){std::fprintf(stderr,"FAIL: %s\n",name);++failures;}
    };
    check(server_impact_definition_identity_matches(0x11110000,0x11110000,5),
        "same actual native registered definition accepted");
    check(!server_impact_definition_identity_matches(0x22220000,0x11110000,5),
        "selected player weapon must not relabel different actual native definition");
    check(!server_impact_definition_identity_matches(0,0,5),"null rejected");
    check(!server_impact_definition_identity_matches(0x11110000,0x11110000,0),
        "unregistered/zero weapon rejected");
    check(!server_impact_definition_identity_matches(0x11110000,0x11110000,-1),
        "negative weapon rejected");
    if constexpr(sizeof(std::uintptr_t)>4) {
        check(!server_impact_definition_identity_matches(
            static_cast<std::uintptr_t>(0x111111110000ULL),0x11110000,5),
            "non-x86 address rejected");
    }
    if(!failures)std::puts("PASS: exact actual-discharge definition identity gates");
    return failures?1:0;
}
