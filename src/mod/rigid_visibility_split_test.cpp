// SPDX-License-Identifier: GPL-3.0-only
#include "rigid_visibility_split.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
using wawvr::gameplay::RigidSubmeshRange;
using wawvr::mod::plan_rigid_visibility_split;
using wawvr::mod::should_render_owned_rigid_complement;
void check(const bool condition, const char* description) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", description); std::exit(1); }
}
struct Fixture final {
    // Read-only live Colt shared surface 0: the last 202nd triangle is
    // (251,251,251), not a real triangle owned by a fourth rigid range.
    std::array<RigidSubmeshRange, 3> ranges{{
        {0, 87, 0, 75}, {2, 75, 75, 55}, {4, 90, 130, 71}}};
    std::vector<std::uint16_t> indices{};
    Fixture() {
        std::uint16_t first{};
        for (const auto& range : ranges) {
            for (std::uint32_t i = 0; i < range.triangle_count; ++i) {
                indices.push_back(first);
                indices.push_back(static_cast<std::uint16_t>(first + 1));
                indices.push_back(static_cast<std::uint16_t>(first + 2));
            }
            first = static_cast<std::uint16_t>(first + range.vertex_count);
        }
        indices.insert(indices.end(), {251,251,251});
    }
};
void test_exact_colt_complement() {
    Fixture f;
    const auto before_ranges = f.ranges;
    const auto before_indices = f.indices;
    const auto plan = plan_rigid_visibility_split(252, 202, f.ranges, 2, f.indices);
    check(plan.has_value(), "exact Colt shared topology is accepted");
    check(plan->hidden_range_count == 1 && plan->retained_count == 2 &&
        plan->retained_vertex_count == 177 && plan->retained_triangle_count == 146 &&
        plan->degenerate_trailing_triangle_count == 1,
        "Colt retains exactly 177 non-magazine vertices and 146 real triangles");
    check(plan->retained[0].range_index == 0 && plan->retained[0].bone_id == 0 &&
        plan->retained[0].first_vertex == 0 && plan->retained[0].vertex_count == 87 &&
        plan->retained[0].triangle_offset == 0 && plan->retained[0].triangle_count == 75,
        "first retained part preserves exact authored ownership");
    check(plan->retained[1].range_index == 2 && plan->retained[1].bone_id == 4 &&
        plan->retained[1].first_vertex == 162 && plan->retained[1].vertex_count == 90 &&
        plan->retained[1].triangle_offset == 130 && plan->retained[1].triangle_count == 71,
        "last retained part preserves original offsets without duplicate clip geometry");
    check(f.indices == before_indices, "planner never changes source indices");
    for (std::size_t i=0; i<f.ranges.size(); ++i) {
        check(f.ranges[i].bone_id == before_ranges[i].bone_id &&
            f.ranges[i].vertex_count == before_ranges[i].vertex_count &&
            f.ranges[i].triangle_offset == before_ranges[i].triangle_offset &&
            f.ranges[i].triangle_count == before_ranges[i].triangle_count,
            "planner never changes source rigid ranges");
    }
    check(!plan_rigid_visibility_split(252,202,f.ranges,8,f.indices).has_value(),
        "surface without magazine ownership produces no split");
    for (auto& range : f.ranges) range.bone_id = 2;
    const auto all_hidden = plan_rigid_visibility_split(252,202,f.ranges,2,f.indices);
    check(all_hidden.has_value() && all_hidden->retained_count == 0 &&
        all_hidden->hidden_range_count == 3,
        "all-magazine surface produces no duplicate companion");
}
void test_fail_closed_topology() {
    for (int malformed=0; malformed<11; ++malformed) {
        Fixture f; std::uint32_t vertices=252,triangles=202;std::uint16_t bone=2;
        switch(malformed) {
        case 0: f.ranges[1].triangle_offset=74;break;
        case 1: f.ranges[1].triangle_offset=76;break;
        case 2: f.ranges[1].vertex_count=74;break;
        case 3: f.ranges[1].triangle_count=0;break;
        case 4: f.indices[75*3]=0;break;
        case 5: f.indices.pop_back();break;
        case 6: f.indices.back()=250;break;
        case 7: f.indices[f.indices.size()-3]=252;f.indices[f.indices.size()-2]=252;f.indices.back()=252;break;
        case 8: f.ranges[2].bone_id=128;break;
        case 9: bone=128;break;
        case 10: vertices=0;break;
        }
        check(!plan_rigid_visibility_split(vertices,triangles,f.ranges,bone,f.indices).has_value(),
            "malformed/overlapping/gapped/cross-owner/real-tail topology fails closed");
    }
    Fixture f;
    check(!plan_rigid_visibility_split(252,202,{},2,f.indices).has_value(),
        "empty topology cannot request a companion");
    std::array<RigidSubmeshRange,17> too_many{};
    check(!plan_rigid_visibility_split(252,202,too_many,2,f.indices).has_value(),
        "planner is bounded to 16 ranges");
    f.indices.resize(201*3);
    const auto exact = plan_rigid_visibility_split(252,201,f.ranges,2,f.indices);
    check(exact.has_value() && exact->degenerate_trailing_triangle_count == 0,
        "complete exact topology needs no padding tail");
}
void test_owned_visibility() {
    std::array<std::uint32_t,4> surface{},hidden{};
    // DObj/global example: model-local bones 0/2/4 at base 70 -> 70/72/74.
    for(const auto bone : {70U,72U,74U}) surface[bone>>5] |= 0x80000000U>>(bone&31U);
    hidden[72>>5] = 0x80000000U>>(72&31);
    const auto source_before=surface,hidden_before=hidden;
    for(int eye=0;eye<2;++eye) for(int sample=0;sample<1000;++sample) {
        check(should_render_owned_rigid_complement(true,true,72,surface,hidden),
            "both eyes can render the same owned complement idempotently");
    }
    check(surface==source_before && hidden==hidden_before,
        "visibility decision never changes native or source masks");
    check(!should_render_owned_rigid_complement(false,true,72,surface,hidden),
        "cancel/restore/switch/tracking failure immediately suppresses companions");
    check(!should_render_owned_rigid_complement(true,false,72,surface,hidden),
        "preexisting native magazine invisibility cannot be resurrected");
    hidden[74>>5] |= 0x80000000U>>(74&31);
    check(!should_render_owned_rigid_complement(true,true,72,surface,hidden),
        "another hidden source bone suppresses the entire complement");
    hidden=hidden_before;hidden[0]|=0x80000000U;
    check(should_render_owned_rigid_complement(true,true,72,surface,hidden),
        "unrelated native hidden bones do not suppress this exact source surface");
    hidden={};
    check(!should_render_owned_rigid_complement(true,true,72,surface,hidden),
        "intent without an applied source hide cannot double-draw retained parts");
    check(!should_render_owned_rigid_complement(true,true,128,surface,hidden),
        "out-of-range hidden bone fails closed");
    surface={};hidden=hidden_before;
    check(!should_render_owned_rigid_complement(true,true,72,surface,hidden),
        "surface without the owned clip bit cannot receive companions");
}
} // namespace
int main() {
    test_exact_colt_complement();test_fail_closed_topology();test_owned_visibility();
    std::puts("PASS: bounded rigid shared-surface complement topology and owned visibility; runtime DObj/material/stereo integration not asserted");
    return 0;
}
