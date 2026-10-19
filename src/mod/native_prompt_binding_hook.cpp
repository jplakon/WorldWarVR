// SPDX-License-Identifier: GPL-3.0-only
#include "native_prompt_binding_hook.hpp"

#include "automatic_reload_setting.hpp"
#include "controller_state.hpp"
#include "cursor_hint_display_contract.hpp"
#include "input_mapping.hpp"
#include "manual_grenade_runtime.hpp"
#include "native_prompt_loading_item_observation.hpp"
#include "native_prompt_loading_tip_policy.hpp"
#include "native_prompt_mission_start_policy.hpp"
#include "peer_thread_quiescence.hpp"
#include "resident_page_access.hpp"
#include "stereo_diagnostics.hpp"
#include "stereo_scene_hook.hpp"
#include "t4_layout_selector.hpp"
#include "t4_presentation_state.hpp"
#include "vr_gameplay_prompts.hpp"
#include "vr_loading_tip_text.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

namespace wawvr::mod {
namespace {

// Mapped retail SP disassembly proves this display-only call. The independent
// script getkeybinding caller at 0x00517423 is deliberately NOT patched.
constexpr wawvr::t4::Rva kDisplayCallRva=0x001C7553;
constexpr wawvr::t4::Rva kDisplayContextRva=0x001C754A;
constexpr wawvr::t4::Rva kDisplayEntryRva=0x001C7530;
constexpr wawvr::t4::Rva kBindingTargetRva=0x00078930;
constexpr std::array<std::uint8_t,5> kOriginalCall{0xE8,0xD8,0x13,0xEB,0xFF};
constexpr std::array<std::uint8_t,17> kDisplayContext{
    0x51,0x52,0x8B,0x94,0x24,0x14,0x01,0x00,0x00,
    0xE8,0xD8,0x13,0xEB,0xFF,0x83,0xC4,0x08};
constexpr std::array<std::uint8_t,23> kDisplayEntry{
    0x81,0xEC,0x00,0x01,0x00,0x00,0x55,0x57,0x8B,0xF8,0x33,0xC0,
    0x39,0x84,0x24,0x10,0x01,0x00,0x00,0x8D,0x4C,0x24,0x08};
constexpr std::array<std::uint8_t,41> kBindingTarget{
    0x83,0xEC,0x08,0x53,0x56,0x8B,0x74,0x24,0x18,0x57,0x50,
    0x8B,0x44,0x24,0x1C,0x8D,0x4C,0x24,0x10,0x51,0x52,
    0x8D,0x9E,0x80,0x00,0x00,0x00,0x50,0xC6,0x03,0x00,
    0xE8,0xCC,0xED,0xFF,0xFF,0x8B,0xF8,0x83,0xC4,0x10};
// Separately opt-in, transparent observation at one exact @key display call.
// Mapped live24/pass2 proves this key ABI and painter stack shape. Nothing
// establishes that a particular campaign hint reaches it until observed.
constexpr wawvr::t4::Rva kKeyCallRva=0x001C6FA9;
constexpr wawvr::t4::Rva kKeyContextRva=0x001C6FA1;
constexpr wawvr::t4::Rva kKeyPainterRva=0x001C6EE0;
constexpr wawvr::t4::Rva kKeyTargetRva=0x001BB5E0;
constexpr std::array<std::uint8_t,5> kKeyOriginalCall{0xE8,0x32,0x46,0xFF,0xFF};
constexpr std::array<std::uint8_t,18> kKeyContext{
    0x80,0x3F,0x40,0x75,0x0A,0x8D,0x47,0x01,0xE8,
    0x32,0x46,0xFF,0xFF,0x8B,0xF8,0x80,0x3F,0x00};
constexpr std::array<std::uint8_t,25> kKeyPainter{
    0x81,0xEC,0x1C,0x04,0x00,0x00,0xF6,0x86,0xE4,0x00,0x00,0x00,
    0x01,0x53,0x55,0x8B,0xAC,0x24,0x28,0x04,0x00,0x00,0x57,0x74,0x1A};
constexpr std::array<std::uint8_t,30> kKeyTarget{
    0x56,0x8B,0xF0,0x80,0x3E,0x15,0x75,0x05,0x83,0xC6,0x01,0xEB,
    0x11,0x56,0xE8,0x4D,0x17,0xFF,0xFF,0x83,0xC4,0x04,0x85,0xC0,
    0x0F,0x85,0xB3,0x00,0x00,0x00};
constexpr std::size_t kKeyStackPathBytes=kKeyCallRva-kKeyPainterRva;
constexpr std::uint64_t kKeyStackPathFnv=0xDF404B7350ED7691ULL;
constexpr std::size_t kLoadingTargetBytes=212;
constexpr std::uint64_t kLoadingTargetFnv=0xFD194163BFE45261ULL;
constexpr wawvr::t4::Rva kLoadingParentRva=0x001C98D5;
constexpr std::array<std::uint8_t,16> kLoadingParent{
    0x53,0xE8,0x05,0xD6,0xFF,0xFF,0x83,0xC4,0x04,0x5F,0x5E,0x5B,0x8B,0xE5,0x5D,0xC3};
constexpr wawvr::t4::Rva kEncodedCallRva=0x001C6F05;
constexpr wawvr::t4::Rva kEncodedContextRva=0x001C6EF9;
constexpr wawvr::t4::Rva kEncodedTargetRva=0x001ACF20;
constexpr std::array<std::uint8_t,5> kEncodedOriginalCall{0xE8,0x16,0x60,0xFE,0xFF};
constexpr std::array<std::uint8_t,26> kEncodedContext{
    0x6A,0x00,0x68,0xA8,0x72,0x87,0x00,0x68,0x3D,0x82,0x09,0x02,
    0xE8,0x16,0x60,0xFE,0xFF,0x83,0xC4,0x0C,0x89,0x86,0xE0,0x00,0x00,0x00};
constexpr std::array<std::uint8_t,43> kEncodedTarget{
    0x81,0xEC,0x20,0x08,0x00,0x00,0xA1,0x10,0x6F,0xDE,0x04,0x83,0xC0,
    0x01,0x99,0xB9,0x0A,0x00,0x00,0x00,0xF7,0xF9,0x53,0x55,0x56,0x57,
    0x68,0x00,0x04,0x00,0x00,0x33,0xFF,0x57,0x89,0x15,0x10,0x6F,0xDE,
    0x04,0xC1,0xE2,0x0A};
constexpr std::size_t kEncodedStackPathBytes=kEncodedCallRva-kKeyPainterRva;
constexpr std::uint64_t kEncodedStackPathFnv=0xAD921C3B4229665AULL;
constexpr wawvr::t4::Rva kCgCallRva=0x00037FE5;
constexpr wawvr::t4::Rva kCgContextRva=0x00037FDD;
constexpr wawvr::t4::Rva kCgEntryRva=0x00037F40;
constexpr wawvr::t4::Rva kCgParentContextRva=0x00038922;
constexpr std::array<std::uint8_t,5> kCgOriginalCall{0xE8,0x36,0x4F,0x17,0x00};
constexpr std::array<std::uint8_t,15> kCgContext{
    0x6A,0x00,0x68,0xC0,0xD3,0x84,0x00,0x56,0xE8,0x36,0x4F,0x17,0x00,0x8B,0xF8};
constexpr std::array<std::uint8_t,25> kCgEntry{
    0xA1,0xA8,0xEB,0x68,0x03,0x83,0xEC,0x24,0x83,0x78,0x10,0x00,
    0x53,0x8B,0x5C,0x24,0x2C,0x56,0x57,0x0F,0x85,0xFE,0x02,0x00,0x00};
constexpr std::array<std::uint8_t,14> kCgParentContext{
    0xE8,0xB9,0xF5,0xFF,0xFF,0x57,0xE8,0x13,0xF6,0xFF,0xFF,0x83,0xC4,0x04};
constexpr std::size_t kCgFunctionBytes=798;
constexpr std::uint64_t kCgFunctionFnv=0x5730E04C6843E0B0ULL;
// Complete mapped text30 source/formatting path, not a guessed global lookup.
constexpr wawvr::t4::Rva kCursorCallRva=0x000512E4;
constexpr wawvr::t4::Rva kCursorEntryRva=0x00051280;
constexpr wawvr::t4::Rva kCursorPainterRva=0x00051310;
constexpr std::size_t kCursorFunctionBytes=139,kCursorPainterBytes=1772;
constexpr std::uint64_t kCursorFunctionFnv=0x74C83DEF11459673ULL;
constexpr std::uint64_t kCursorPainterFnv=0xBAADB8976A9A1B3AULL;
constexpr std::array<std::uint8_t,5> kCursorOriginalCall{0xE8,0x37,0xBC,0x15,0x00};
constexpr std::size_t kBridgeGuardBytes=128;
// Current real x86/O2 bridge COMDAT is227bytes. Include all post-native call
// return PCs during restore, not just the older <=128-byte preobserver range.
constexpr std::size_t kLoadingBridgeGuardBytes=384;
std::atomic<bool> g_installed{false},g_enabled{false};
std::atomic<std::uint32_t> g_logged_commands{0};
std::uintptr_t g_callsite{};
std::array<std::uint8_t,5> g_replacement{};
std::atomic<bool> g_key_installed{false},g_key_enabled{false},g_key_summary_pending{false};
std::atomic<std::uint32_t> g_key_calls{0},g_key_logged{0},g_key_unknown{0},g_key_unreadable{0};
std::atomic<std::uint64_t> g_key_coop_logged{0};
// Separate observation only: this UI loading/start caption is not a gameplay
// hint, a Co-Op tip or one of the four opt-in loading replacements.
std::atomic<bool> g_key_mission_start_logged{false};
std::uintptr_t g_key_callsite{},g_key_module_begin{},g_key_module_end{};
bool g_key_diagnostics{},g_loading_stick_melee{};
std::atomic<bool> g_loading_enabled{false};
std::atomic<std::uint32_t> g_loading_copied{0};
std::atomic<bool> g_mission_start_enabled{false},g_mission_start_observed{false},g_mission_start_copied{false};
std::atomic<std::uint32_t> g_mission_start_rejections{0},g_mission_start_diagnostics{0};
MissionStartSettingSource g_mission_start_setting_source{MissionStartSettingSource::invalid};
std::array<std::uint8_t,5> g_key_replacement{};
std::atomic<bool> g_encoded_installed{false},g_encoded_enabled{false},g_encoded_summary_pending{false};
std::atomic<std::uint32_t> g_encoded_calls{0},g_encoded_logged{0},g_encoded_unknown{0},g_encoded_rejected{0};
std::uintptr_t g_encoded_callsite{},g_encoded_module_begin{},g_encoded_module_end{};
std::array<std::uint8_t,5> g_encoded_replacement{};
std::atomic<bool> g_cg_installed{false},g_cg_enabled{false},g_cg_summary_pending{false};
std::atomic<std::uint32_t> g_cg_calls{0},g_cg_logged{0},g_cg_copy_logged{0},g_cg_copied{0},g_cg_unknown{0},g_cg_rejected{0};
std::atomic<bool> g_cg_entry_logged{false};
std::atomic<std::uint32_t> g_cg_rejection_logged{0};
std::uintptr_t g_cg_callsite{};
std::array<std::uint8_t,5> g_cg_replacement{};
bool g_cg_diagnostics{},g_cg_translation{}; // immutable before enabled release
struct CursorPromptState final {
    std::atomic<bool> installed{false},enabled{false},summary_pending{false},entry_logged{false};
    std::atomic<std::uint32_t> calls{0},logged{0},copy_logged{0},copied{0},unknown{0},rejected{0},rejection_logged{0};
    std::uintptr_t callsite{};
    std::array<std::uint8_t,5> replacement{};
    bool diagnostics{},translation{}; // immutable before enabled release
};
CursorPromptState g_cursor;

bool setting(const wchar_t* name) noexcept {
    std::array<wchar_t,16> value{};
    return GetEnvironmentVariableW(name,value.data(),static_cast<DWORD>(value.size()))==1 &&
        value[0]==L'1';
}

MissionStartSetting read_mission_start_setting() noexcept {
    std::array<wchar_t,16> value{};
    const auto previous_error=GetLastError();
    SetLastError(ERROR_SUCCESS);
    const auto length=GetEnvironmentVariableW(L"WAWVR_TARGETED_MISSION_START",value.data(),static_cast<DWORD>(value.size()));
    const auto error=GetLastError();
    SetLastError(previous_error);
    if(length==0)return resolve_mission_start_setting(error!=ERROR_ENVVAR_NOT_FOUND,{});
    if(length>=value.size() || value[length]!=0)return resolve_mission_start_setting(true,{});
    return resolve_mission_start_setting(true,{value.data(),length});
}

bool matches(const std::uintptr_t address,
             const std::span<const std::uint8_t> bytes) noexcept {
    return address != 0 && !bytes.empty() &&
        resident_page_access(reinterpret_cast<const void*>(address),bytes.size(),false,&VirtualQuery) &&
        std::memcmp(reinterpret_cast<const void*>(address),bytes.data(),bytes.size())==0;
}

bool key_witness_matches() noexcept {
    constexpr std::uintptr_t base=0x00400000U;
    const auto* const path=reinterpret_cast<const std::uint8_t*>(base+kKeyPainterRva);
    if (!matches(base+kKeyContextRva,kKeyContext) ||
        !matches(base+kKeyPainterRva,kKeyPainter) || !matches(base+kKeyTargetRva,kKeyTarget) ||
        !matches(base+kLoadingParentRva,kLoadingParent) ||
        !resident_page_access(path,kKeyStackPathBytes,false,&VirtualQuery)) return false;
    std::uint64_t hash=14695981039346656037ULL;
    for (std::size_t index=0;index<kKeyStackPathBytes;++index)
        hash=(hash^path[index])*1099511628211ULL;
    if (hash!=kKeyStackPathFnv) return false;
    const auto* const target=reinterpret_cast<const std::uint8_t*>(base+kKeyTargetRva);
    if (!resident_page_access(target,kLoadingTargetBytes,false,&VirtualQuery)) return false;
    hash=14695981039346656037ULL;
    for (std::size_t index=0;index<kLoadingTargetBytes;++index)
        hash=(hash^target[index])*1099511628211ULL;
    return hash==kLoadingTargetFnv;
}

bool encoded_witness_matches() noexcept {
    constexpr std::uintptr_t base=0x00400000U;
    const auto* const path=reinterpret_cast<const std::uint8_t*>(base+kKeyPainterRva);
    if (!matches(base+kEncodedContextRva,kEncodedContext) ||
        !matches(base+kKeyPainterRva,kKeyPainter) ||
        !matches(base+kEncodedTargetRva,kEncodedTarget) ||
        !resident_page_access(path,kEncodedStackPathBytes,false,&VirtualQuery)) return false;
    std::uint64_t hash=14695981039346656037ULL;
    for (std::size_t index=0;index<kEncodedStackPathBytes;++index)
        hash=(hash^path[index])*1099511628211ULL;
    return hash==kEncodedStackPathFnv;
}

bool cg_witness_matches() noexcept {
    constexpr std::uintptr_t base=0x00400000U;
    const auto* const body=reinterpret_cast<const std::uint8_t*>(base+kCgEntryRva);
    if (!matches(base+kCgContextRva,kCgContext) || !matches(base+kCgEntryRva,kCgEntry) ||
        !matches(base+kCgParentContextRva,kCgParentContext) ||
        !matches(base+kEncodedTargetRva,kEncodedTarget) ||
        !resident_page_access(body,kCgFunctionBytes,false,&VirtualQuery)) return false;
    std::uint64_t hash=14695981039346656037ULL;
    for (std::size_t index=0;index<kCgFunctionBytes;++index)
        hash=(hash^body[index])*1099511628211ULL;
    return hash==kCgFunctionFnv;
}

bool cursor_witness_matches() noexcept {
    constexpr std::uintptr_t base=0x00400000U;
    for (const auto witness : {std::array<std::uint64_t,3>{kCursorEntryRva,kCursorFunctionBytes,kCursorFunctionFnv},
            std::array<std::uint64_t,3>{kCursorPainterRva,kCursorPainterBytes,kCursorPainterFnv}}) {
        const auto* const bytes=reinterpret_cast<const std::uint8_t*>(
            base+static_cast<std::uintptr_t>(witness[0]));
        if (!resident_page_access(bytes,static_cast<std::size_t>(witness[1]),false,&VirtualQuery))
            return false;
        std::uint64_t hash=14695981039346656037ULL;
        for (std::size_t index=0;index<witness[1];++index)
            hash=(hash^bytes[index])*1099511628211ULL;
        if (hash!=witness[2]) return false;
    }
    return matches(base+kEncodedTargetRva,kEncodedTarget);
}

enum class PatchResult { okay,foreign,quiescence,failed };
enum class DisplayWitness { none,at_key,encoded,cg,cursor };
PatchResult replace_call(const std::uintptr_t address,
    const std::array<std::uint8_t,5>& expected,
    const std::array<std::uint8_t,5>& replacement,
    const std::uintptr_t bridge,const DisplayWitness witness=DisplayWitness::none,
    const std::size_t bridge_guard_bytes=kBridgeGuardBytes) noexcept {
    const std::array<PeerThreadPatchRange,2> ranges{{
        {address,expected.size()},{bridge,bridge ? bridge_guard_bytes : 0}}};
    SuspendedPeerThreads peers;
    if (!peers.suspend(ranges)) return PatchResult::quiescence;
    if (!matches(address,expected)) return PatchResult::foreign;
    // The optional observer derives one caller-stack offset from the ENTIRE
    // 201-byte painter path; recheck that proof under peer quiescence too.
    if ((witness==DisplayWitness::at_key && !key_witness_matches()) ||
        (witness==DisplayWitness::encoded && !encoded_witness_matches()) ||
        (witness==DisplayWitness::cg && !cg_witness_matches()) ||
        (witness==DisplayWitness::cursor && !cursor_witness_matches()))
        return PatchResult::foreign;
    auto* target=reinterpret_cast<void*>(address);
    DWORD protection{};
    if (!VirtualProtect(target,replacement.size(),PAGE_EXECUTE_READWRITE,&protection))
        return PatchResult::failed;
    if (std::memcmp(target,expected.data(),expected.size())!=0) {
        DWORD ignored{}; VirtualProtect(target,expected.size(),protection,&ignored);
        return PatchResult::foreign;
    }
    std::memcpy(target,replacement.data(),replacement.size());
    const bool okay=std::memcmp(target,replacement.data(),replacement.size())==0 &&
        FlushInstructionCache(GetCurrentProcess(),target,replacement.size());
    if (!okay) {
        std::memcpy(target,expected.data(),expected.size());
        FlushInstructionCache(GetCurrentProcess(),target,expected.size());
    }
    DWORD ignored{};
    if (!VirtualProtect(target,replacement.size(),protection,&ignored)) {
        std::memcpy(target,expected.data(),expected.size());
        FlushInstructionCache(GetCurrentProcess(),target,expected.size());
        VirtualProtect(target,expected.size(),protection,&ignored);
        return PatchResult::failed;
    }
    return okay ? PatchResult::okay : PatchResult::failed;
}

bool read_exact_command(const char* source,std::array<char,64>& bytes,
                        std::string_view& result) noexcept {
    // Validate the bounded span once, not once per character on the HUD hot
    // path. A native string at an unavailable boundary simply stays native.
    if (!source || !resident_page_access(source,bytes.size(),false,&VirtualQuery))
        return false;
    for (std::size_t i=0;i<bytes.size();++i) {
        bytes[i]=source[i];
        if (bytes[i]=='\0') { result={bytes.data(),i}; return true; }
        if (bytes[i]<0x20 || bytes[i]>0x7e) return false;
    }
    return false;
}

bool read_loading_item_word(const void* const base,const std::uintptr_t offset,
    std::uint32_t& value) noexcept {
    const auto address=reinterpret_cast<std::uintptr_t>(base);
    constexpr auto maximum=(std::numeric_limits<std::uintptr_t>::max)();
    if (address==0 || address>maximum-offset || address+offset>maximum-sizeof(value)) return false;
    const auto* const field=reinterpret_cast<const void*>(address+offset);
    if (!resident_page_access(field,sizeof(value),false,&VirtualQuery)) return false;
    std::memcpy(&value,field,sizeof(value));
    return true;
}

LoadingItemSnapshot read_loading_item_snapshot(const char* const key,
    const void* const item,const void* const context) noexcept {
    LoadingItemSnapshot snapshot{};
    snapshot.item_read=read_loading_item_word(item,0xB4,snapshot.type) &&
        read_loading_item_word(item,0xE4,snapshot.flags) &&
        read_loading_item_word(item,0xE0,snapshot.text) &&
        read_loading_item_word(item,0x10C,snapshot.dvar) &&
        read_loading_item_word(item,0x140,snapshot.expression_count);
    snapshot.context_read=read_loading_item_word(context,0,snapshot.local_client);
    std::string_view ignored;
    snapshot.dvar_read=snapshot.item_read && read_exact_command(
        reinterpret_cast<const char*>(static_cast<std::uintptr_t>(snapshot.dvar)),snapshot.dvar_name,ignored);
    snapshot.key_read=read_exact_command(key,snapshot.key,ignored);
    return snapshot;
}

void observe_loading_item_first_key(const std::string_view observed_key,
    const char* const key,const void* const item,const void* const context,
    const void* const native_stack,const std::uint32_t call_return,
    const std::uint32_t parent_return) noexcept {
    if (!loading_item_key_is_exact(observed_key)) return;
    // Called only within the EXISTING first-per-exact-key logging branch:
    // maximum four extra records/process. Two immutable owned snapshots, no
    // native text/state writes or output override. Unknown names are hashed.
    const auto first=read_loading_item_snapshot(key,item,context);
    const auto second=read_loading_item_snapshot(key,item,context);
    const auto key_address=reinterpret_cast<std::uintptr_t>(key);
    const auto stack_address=reinterpret_cast<std::uintptr_t>(native_stack);
    const auto reject=loading_item_observation_rejections(first,second,key_address,
        stack_address,call_return,static_cast<std::uint32_t>(g_key_callsite+5),parent_return,observed_key);
    const auto name=loading_item_bounded_text(first.dvar_name);
    std::uint32_t hash=2166136261U;
    for (const unsigned char byte : name) hash=(hash^byte)*16777619U;
    const bool readable=loading_item_snapshot_readable(first) && loading_item_snapshot_readable(second);
    const bool stable=readable && first==second;
    stereo_diagnostic_log("VRPromptKeyItemDiag key=%s callRva=001C6FA9 parentReturn=%08X callReturn=%08X readable=%u stable=%u rejectMask=%08X item=%08X context=%08X type=%u flags=%08X text=%08X exprCount=%u dvar=%08X dvarName=%s dvarNameBytes=%u dvarNameHash=%08X keyPointer=%08X nativeStack=%08X keyOnDvarStack=%u localClient=%u nativeCalls=1 outputModified=0 observationOnly=1",
        first.key_read && loading_item_bounded_text(first.key)==observed_key ? first.key.data() : "changed-or-unreadable",
        parent_return,call_return,readable?1U:0U,stable?1U:0U,reject,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(item)),
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(context)),first.type,first.flags,first.text,
        first.expression_count,first.dvar,first.dvar_read && name=="didyouknow" ? "didyouknow" : "not-exact-or-unreadable",
        static_cast<unsigned>(name.size()),first.dvar_read?hash:0U,
        static_cast<unsigned>(key_address),static_cast<unsigned>(stack_address),
        loading_item_key_on_dvar_stack(key_address,stack_address)?1U:0U,first.local_client);
}

MissionStartItemSnapshot read_mission_start_item_snapshot(const char* const key,
    const void* const item,const void* const context) noexcept {
    MissionStartItemSnapshot snapshot{};
    snapshot.item_read=read_loading_item_word(item,0xB4,snapshot.type) &&
        read_loading_item_word(item,0xE4,snapshot.flags) &&
        read_loading_item_word(item,0xE0,snapshot.text) &&
        read_loading_item_word(item,0x10C,snapshot.dvar) &&
        read_loading_item_word(item,0x140,snapshot.expression_count);
    snapshot.context_read=read_loading_item_word(context,0,snapshot.local_client);
    std::string_view ignored;
    snapshot.source_read=snapshot.item_read && read_exact_command(
        reinterpret_cast<const char*>(static_cast<std::uintptr_t>(snapshot.text)),snapshot.source,ignored);
    snapshot.key_read=read_exact_command(key,snapshot.key,ignored);
    return snapshot;
}

bool read_mission_start_native_text(const char* const source,std::array<char,256>& destination) noexcept {
    if(!source || !resident_page_access(source,destination.size(),false,&VirtualQuery))return false;
    for(std::size_t index=0;index<destination.size();++index){
        const auto byte=source[index];destination[index]=byte;
        if(byte==0)return true;
        if(byte<0x20 || byte>0x7E)return false;
    }
    return false;
}

const char* select_mission_start_caption(const char* const native_result,const char* const key,
    const void* const item,const void* const context,const void* const native_stack) noexcept {
    if(!g_key_enabled.load(std::memory_order_acquire) ||
        (!g_mission_start_enabled.load(std::memory_order_acquire) && !g_key_diagnostics))return nullptr;
    static thread_local bool selecting=false;
    if(selecting)return nullptr;
    selecting=true;
    struct Reset final{bool& value;~Reset(){value=false;}}reset{selecting};
    std::array<char,64> key_bytes{};
    std::string_view exact_key;
    if(!read_exact_command(key,key_bytes,exact_key) || exact_key!=kMissionStartKey)return nullptr;

    std::uint32_t call_return{},parent_return{};
    const bool stack_read=read_loading_item_word(native_stack,0,call_return) &&
        read_loading_item_word(native_stack,0x42C,parent_return);
    const auto first=read_mission_start_item_snapshot(key,item,context);
    const auto second=read_mission_start_item_snapshot(key,item,context);
    const auto item_reject=mission_start_item_rejections(first,second,reinterpret_cast<std::uintptr_t>(key),
        call_return,static_cast<std::uint32_t>(g_key_callsite+5),parent_return,exact_key);
    const auto presentation=read_t4_presentation_state();
    ControllerFrameSnapshot frame{};
    const bool has_frame=read_controller_frame(&frame);
    const auto profile=frame.frame.actions.hands[1].binding_profile;
    const bool enabled=g_mission_start_enabled.load(std::memory_order_acquire);
    const auto frame_reject=mission_start_frame_rejections({
        .enabled=enabled,.presentation_valid=presentation.valid,
        .frame_current=has_frame && controller_frame_is_current(frame,GetTickCount64()),
        .views_valid=has_frame && frame.frame.views_valid,.focused=has_frame && frame.frame.actions.focused,
        .primary_active=has_frame && frame.frame.actions.hands[1].primary.active,
        .connection=presentation.connection_state,.key_catchers=presentation.key_catchers,.right_profile=profile});
    std::array<char,256> text{},text_again{};
    const bool native_read=read_mission_start_native_text(native_result,text) &&
        read_mission_start_native_text(native_result,text_again);
    const bool english_match=native_read && text==text_again && std::string_view(text.data())==kMissionStartNativeEnglish;
    const auto reasons=frame_reject|(item_reject<<10U)|(english_match?0U:0x40000000U);
    const bool first_observation=!g_mission_start_observed.exchange(true,std::memory_order_acq_rel);
    const auto new_reasons=reasons&~g_mission_start_rejections.fetch_or(reasons,std::memory_order_acq_rel);
    if(first_observation || new_reasons!=0){
        const auto ordinal=g_mission_start_diagnostics.fetch_add(1,std::memory_order_relaxed)+1U;
        if(ordinal<=12U)
            stereo_diagnostic_log("VRMissionStartDiag item ordinal=%u key=MENU_CLICK_TO_START_THE_MISSION callReturn=%08X parentReturn=%08X stackRead=%u item=%08X context=%08X readable=%u stable=%u type=%u flags=%08X text=%08X dvar=%08X exprCount=%u localClient=%u inlinePointer=%u inlineSourceExact=%u sourceKeyExact=%u frameReject=%03X itemReject=%03X nativeEnglishMatch=%u connection=%d keyCatchers=%u primaryActive=%u rightProfile=%u enabled=%u settingPolicy=%s nativeCalls=1 outputModified=0 observationOnly=1",
                ordinal,call_return,parent_return,stack_read?1U:0U,
                static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(item)),static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(context)),
                mission_start_item_readable(first)&&mission_start_item_readable(second)?1U:0U,
                mission_start_item_readable(first)&&mission_start_item_readable(second)&&first==second?1U:0U,
                first.type,first.flags,first.text,first.dvar,first.expression_count,first.local_client,
                mission_start_inline_pointer(first.text,reinterpret_cast<std::uintptr_t>(key))?1U:0U,
                mission_start_bounded_text(first.source)==kMissionStartInlineSource?1U:0U,
                mission_start_bounded_text(first.key)==kMissionStartKey?1U:0U,frame_reject,item_reject,english_match?1U:0U,
                presentation.connection_state,presentation.key_catchers,
                has_frame&&frame.frame.actions.hands[1].primary.active?1U:0U,static_cast<unsigned>(profile),enabled?1U:0U,
                mission_start_setting_source_name(g_mission_start_setting_source));
    }
    if(!stack_read || frame_reject!=0 || item_reject!=0 || !english_match)return nullptr;
    const auto replacement=resolve_vr_mission_start_text(exact_key,std::string_view(text.data()),profile);
    if(replacement.empty())return nullptr;
    if(!g_mission_start_copied.exchange(true,std::memory_order_acq_rel))
        stereo_diagnostic_log("VRMissionStartDiag replaced key=MENU_CLICK_TO_START_THE_MISSION route=ui-at-key-post-return callRva=001C6FA9 parentRva=001C98DB sourceStable=1 inlineSourceExact=1 nativeEnglishMatch=1 connection=%d keyCatchers=16 focusedCurrentViews=1 primaryActive=1 rightProfile=%u enabled=1 settingPolicy=%s nativeCalls=1 outputModified=1 immutableReturn=1 loadingTipPolicy=unchanged",
            presentation.connection_state,static_cast<unsigned>(profile),mission_start_setting_source_name(g_mission_start_setting_source));
    return replacement.data();
}
} // namespace

// Never revoked during teardown. An already-entered transparent bridge can
// always finish its original tail-call after the display call is restored.
extern "C" std::uintptr_t wawvr_native_vr_binding_prompt_original=0;
extern "C" std::uintptr_t wawvr_native_vr_prompt_key_original=0;
extern "C" std::uintptr_t wawvr_native_vr_prompt_encoded_original=0;
extern "C" std::uintptr_t wawvr_native_vr_prompt_cg_original=0;
extern "C" std::uintptr_t wawvr_native_vr_prompt_cursor_original=0;

extern "C" const char* __cdecl wawvr_resolve_native_vr_prompt_cursor(
    const char* const message,const void* const native_context,const std::int32_t policy,
    const std::int32_t local_client,const void* const native_stack) noexcept {
    if (!g_cursor.enabled.load(std::memory_order_acquire)) return nullptr;
    static thread_local bool observing=false;
    if (observing) return nullptr;
    observing=true;
    struct Reset final { bool& value; ~Reset() { value=false; } } reset{observing};
    const auto ordinal=g_cursor.calls.fetch_add(1,std::memory_order_relaxed)+1;
    std::array<char,1024> bytes{};
    const auto stack=reinterpret_cast<std::uintptr_t>(native_stack);
    CursorHintDisplayWitness witness{
        .context=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(native_context)),
        .policy=policy,.local_client=local_client};
    // sub ESP,108h; push ESI; three cdecl args; CALL => original parent +11Ch.
    witness.stack_readable=stack!=0 &&
        stack<=(std::numeric_limits<std::uintptr_t>::max)()-0x11C &&
        resident_page_access(native_stack,4,false,&VirtualQuery) &&
        resident_page_access(reinterpret_cast<const void*>(stack+0x11C),4,false,&VirtualQuery);
    if (witness.stack_readable) {
        std::memcpy(&witness.call_return,native_stack,4);
        std::memcpy(&witness.parent_return,reinterpret_cast<const void*>(stack+0x11C),4);
    }
    witness.message_readable=resident_page_access(message,bytes.size(),false,&VirtualQuery);
    const auto reject=[&](const std::uint32_t reasons) noexcept {
        g_cursor.rejected.fetch_add(1,std::memory_order_relaxed);
        const auto new_bits=reasons & ~g_cursor.rejection_logged.fetch_or(reasons,std::memory_order_acq_rel);
        if (g_cursor.diagnostics && new_bits!=0)
            stereo_diagnostic_log("VRPromptCursorDiag rejected ordinal=%u reasonMask=%03X newReasons=%03X stackReadable=%u callReturn=%08X parentReturn=%08X context=%08X policy=%d localClient=%d messageReadable=%u sourceUnchanged=1 payloadText=not-recorded",
                ordinal,reasons,new_bits,witness.stack_readable?1U:0U,witness.call_return,
                witness.parent_return,witness.context,policy,local_client,witness.message_readable?1U:0U);
    };
    if (g_cursor.diagnostics && !g_cursor.entry_logged.exchange(true,std::memory_order_acq_rel))
        stereo_diagnostic_log("VRPromptCursorDiag entered ordinal=%u stackReadable=%u callReturn=%08X expectedCallReturn=004512E9 parentReturn=%08X parentOffset=11C context=%08X policy=%d localClient=%d messageReadable=%u translationOptIn=%u sourceUnchanged=1 payloadText=not-recorded",
            ordinal,witness.stack_readable?1U:0U,witness.call_return,witness.parent_return,
            witness.context,policy,local_client,witness.message_readable?1U:0U,g_cursor.translation?1U:0U);
    if (const auto reasons=cursor_hint_display_rejection_mask(witness);reasons!=0) {
        reject(reasons); return nullptr;
    }
    std::size_t length=0;
    for (;length<bytes.size();++length) {
        bytes[length]=message[length];
        if (bytes[length]=='\0') break;
    }
    const std::string_view encoded{bytes.data(),length};
    const auto observation=length<bytes.size()?observe_vr_encoded_prompt(encoded):VrEncodedPromptObservation{};
    if (!observation.valid) { reject(length==bytes.size()?0x80U:0x100U); return nullptr; }
    if (observation.known_key_mask==0) {
        const auto unknown=g_cursor.unknown.fetch_add(1,std::memory_order_relaxed)+1;
        if (g_cursor.diagnostics && unknown<=4)
            stereo_diagnostic_log("VRPromptCursorDiag unknown ordinal=%u bytes=%u segments=%u localized=%u literalArgs=%u callRva=000512E4 parentReturn=%08X sourceUnchanged=1 payloadText=not-recorded",
                ordinal,static_cast<unsigned>(length),observation.segment_count,
                observation.localized_segments,observation.literal_arguments,witness.parent_return);
        return nullptr;
    }
    const auto presentation=read_t4_presentation_state();
    ControllerFrameSnapshot frame{};
    const bool has_frame=read_controller_frame(&frame);
    const bool current=has_frame && controller_frame_is_current(frame,GetTickCount64());
    const bool scene=stereo_scene_ready_for_current_com_frame();
    const bool active=presentation.valid && presentation.key_catchers==0 &&
        presentation.connection_state==presentation.active_connection_state &&
        scene && current && frame.frame.views_valid && frame.frame.actions.focused;
    // Deliberately no extension for plain keys or other native argument forms:
    // actual per-key grammar must first be observed on this proven route.
    const auto copy=g_cursor.translation && active?resolve_vr_encoded_use_hint_copy(encoded,
        {.left_binding_profile=frame.frame.actions.hands[0].binding_profile}):std::string_view{};
    if (!copy.empty()) g_cursor.copied.fetch_add(1,std::memory_order_relaxed);
    const auto new_bits=observation.known_key_mask &
        ~g_cursor.logged.fetch_or(observation.known_key_mask,std::memory_order_acq_rel);
    const auto new_copy=copy.empty()?0U:observation.known_key_mask &
        ~g_cursor.copy_logged.fetch_or(observation.known_key_mask,std::memory_order_acq_rel);
    if ((new_bits!=0 || new_copy!=0) && (g_cursor.diagnostics || !copy.empty())) {
        stereo_diagnostic_log("VRPromptCursorDiag exact ordinal=%u keysMask=%08X firstKeyBit=%08X singleTargetGrammar=%u bytes=%u segments=%u localized=%u literalArgs=%u callRva=000512E4 parentReturn=%08X stackValid=1 presentation=%u connection=%d keyCatchers=%u stereo=%u controllerCurrent=%u views=%u focused=%u translationOptIn=%u displayArgumentCopy=%u nativeContext=0084F274 nativePolicy=0 originalSourceUnchanged=1 outputOwner=native-formatter-and-conversion scriptGetKeyBinding=unchanged",
            ordinal,observation.known_key_mask,observation.segments[0].known_key_bit,
            observation.exact_single_target_key?1U:0U,static_cast<unsigned>(length),
            observation.segment_count,observation.localized_segments,observation.literal_arguments,
            witness.parent_return,presentation.valid?1U:0U,presentation.connection_state,presentation.key_catchers,
            scene?1U:0U,current?1U:0U,has_frame && frame.frame.views_valid?1U:0U,
            has_frame && frame.frame.actions.focused?1U:0U,g_cursor.translation?1U:0U,copy.empty()?0U:1U);
        for (std::size_t index=0;index<observation.segment_count;++index) {
            const auto& segment=observation.segments[index];
            std::array<char,64> key{};
            if (segment.known_key_bit!=0 && segment.bytes<key.size())
                std::memcpy(key.data(),bytes.data()+segment.offset,segment.bytes);
            stereo_diagnostic_log("VRPromptCursorDiag segment ordinal=%u index=%u role=%s offset=%u bytes=%u hash=%08X keyBit=%08X knownKey=%s marker16=%u payloadText=not-recorded sourceUnchanged=1",
                ordinal,static_cast<unsigned>(index),segment.localized_key?"localized-key":"literal-argument",
                segment.offset,segment.bytes,segment.hash,segment.known_key_bit,
                key[0]!=0?key.data():"not-allowlisted",segment.conversion_marker16?1U:0U);
        }
    }
    return copy.empty()?nullptr:copy.data();
}

extern "C" const char* __cdecl wawvr_resolve_native_vr_prompt_cg(
    const char* const message,const void* const native_context,const std::int32_t policy,
    const std::int32_t local_client,const void* const native_stack) noexcept {
    if (!g_cg_enabled.load(std::memory_order_acquire)) return nullptr;
    static thread_local bool observing=false;
    if (observing) return nullptr;
    observing=true;
    struct Reset final { bool& value; ~Reset() { value=false; } } reset{observing};
    const auto ordinal=g_cg_calls.fetch_add(1,std::memory_order_relaxed)+1;
    std::array<char,1024> bytes{};
    const auto stack=reinterpret_cast<std::uintptr_t>(native_stack);
    std::uint32_t call_return=0,parent_return=0;
    const bool readable_stack=stack!=0 && stack<=(std::numeric_limits<std::uintptr_t>::max)()-0x40 &&
        resident_page_access(native_stack,sizeof(call_return),false,&VirtualQuery) &&
        resident_page_access(reinterpret_cast<const void*>(stack+0x40),sizeof(parent_return),false,&VirtualQuery);
    if (readable_stack) {
        std::memcpy(&call_return,native_stack,sizeof(call_return));
        std::memcpy(&parent_return,reinterpret_cast<const void*>(stack+0x40),sizeof(parent_return));
    }
    const bool readable_message=resident_page_access(message,bytes.size(),false,&VirtualQuery);
    // Observation only: distinguish a seam that was never reached from a
    // reached seam rejected by its exact ABI gates. No text bytes are logged.
    // Bits: stack=1, call=2, parent=4, context=8, policy=10h, message=20h,
    // client=40h, missing terminator=80h, encoded grammar=100h.
    const auto reject=[&](const std::uint32_t reasons) noexcept {
        g_cg_rejected.fetch_add(1,std::memory_order_relaxed);
        if (!g_cg_diagnostics) return;
        const auto new_bits=reasons &
            ~g_cg_rejection_logged.fetch_or(reasons,std::memory_order_acq_rel);
        if (new_bits!=0)
            stereo_diagnostic_log("VRPromptCgDiag rejected ordinal=%u reasonMask=%03X newReasons=%03X stackReadable=%u callReturn=%08X expectedCallReturn=%08X parentReturn=%08X expectedParentReturn=0043892D context=%08X policy=%d localClient=%d messageReadable=%u sourceUnchanged=1 payloadText=not-recorded",
                ordinal,reasons,new_bits,readable_stack?1U:0U,call_return,
                static_cast<unsigned>(g_cg_callsite+5),parent_return,
                static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(native_context)),
                policy,local_client,readable_message?1U:0U);
    };
    if (g_cg_diagnostics && !g_cg_entry_logged.exchange(true,std::memory_order_acq_rel))
        stereo_diagnostic_log("VRPromptCgDiag entered ordinal=%u stackReadable=%u callReturn=%08X expectedCallReturn=%08X parentReturn=%08X expectedParentReturn=0043892D context=%08X policy=%d localClient=%d messageReadable=%u translationOptIn=%u sourceUnchanged=1 payloadText=not-recorded",
            ordinal,readable_stack?1U:0U,call_return,static_cast<unsigned>(g_cg_callsite+5),
            parent_return,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(native_context)),
            policy,local_client,readable_message?1U:0U,g_cg_translation?1U:0U);
    const std::uint32_t abi_reasons=(readable_stack?0U:1U)|
        (call_return==g_cg_callsite+5?0U:2U)|(parent_return==0x0043892DU?0U:4U)|
        (reinterpret_cast<std::uintptr_t>(native_context)==0x0084D3C0U?0U:8U)|
        (policy==0?0U:0x10U)|(readable_message?0U:0x20U)|(local_client==0?0U:0x40U);
    if (abi_reasons!=0) {
        reject(abi_reasons); return nullptr;
    }
    std::size_t length=0;
    for (;length<bytes.size();++length) {
        bytes[length]=message[length];
        if (bytes[length]=='\0') break;
    }
    const std::string_view encoded{bytes.data(),length};
    const auto observation=length<bytes.size() ? observe_vr_encoded_prompt(encoded)
        : VrEncodedPromptObservation{};
    if (!observation.valid) {
        reject(length==bytes.size()?0x80U:0x100U); return nullptr;
    }
    if (observation.known_key_mask==0) {
        const auto unknown=g_cg_unknown.fetch_add(1,std::memory_order_relaxed)+1;
        if (g_cg_diagnostics && unknown<=4)
            stereo_diagnostic_log("VRPromptCgDiag unknown ordinal=%u bytes=%u segments=%u localized=%u literalArgs=%u callRva=00037FE5 parentRva=0003892D nativeCalls=1 sourceUnchanged=1 payloadText=not-recorded",
                ordinal,static_cast<unsigned>(length),observation.segment_count,
                observation.localized_segments,observation.literal_arguments);
        return nullptr;
    }
    const auto presentation=read_t4_presentation_state();
    ControllerFrameSnapshot frame{};
    const bool has_frame=read_controller_frame(&frame);
    const bool current=has_frame && controller_frame_is_current(frame,GetTickCount64());
    const bool scene=stereo_scene_ready_for_current_com_frame();
    const bool active=presentation.valid && presentation.key_catchers==0 &&
        presentation.connection_state==presentation.active_connection_state &&
        scene && current && frame.frame.views_valid && frame.frame.actions.focused;
    const auto copy=g_cg_translation && active ? resolve_vr_encoded_use_hint_copy(encoded,
        {.left_binding_profile=frame.frame.actions.hands[0].binding_profile}) : std::string_view{};
    if (!copy.empty()) g_cg_copied.fetch_add(1,std::memory_order_relaxed);
    const auto new_bits=observation.known_key_mask &
        ~g_cg_logged.fetch_or(observation.known_key_mask,std::memory_order_acq_rel);
    const auto new_copy_bits=copy.empty()?0U:observation.known_key_mask &
        ~g_cg_copy_logged.fetch_or(observation.known_key_mask,std::memory_order_acq_rel);
    if ((new_bits!=0 || new_copy_bits!=0) && (g_cg_diagnostics || !copy.empty())) {
        stereo_diagnostic_log("VRPromptCgDiag exact ordinal=%u keysMask=%08X firstKeyBit=%08X singleTargetGrammar=%u bytes=%u segments=%u localized=%u literalArgs=%u callRva=00037FE5 parentRva=0003892D stackValid=1 presentation=%u connection=%d keyCatchers=%u stereo=%u controllerCurrent=%u views=%u focused=%u translationOptIn=%u displayArgumentCopy=%u nativeContext=0084D3C0 nativePolicy=0 nativeCalls=1 sourceUnchanged=1 outputOwner=native-formatter scriptGetKeyBinding=unchanged",
            ordinal,observation.known_key_mask,observation.segments[0].known_key_bit,
            observation.exact_single_target_key?1U:0U,static_cast<unsigned>(length),
            observation.segment_count,observation.localized_segments,observation.literal_arguments,
            presentation.valid?1U:0U,presentation.connection_state,presentation.key_catchers,
            scene?1U:0U,current?1U:0U,has_frame && frame.frame.views_valid?1U:0U,
            has_frame && frame.frame.actions.focused?1U:0U,g_cg_translation?1U:0U,copy.empty()?0U:1U);
        for (std::size_t index=0;index<observation.segment_count;++index) {
            const auto& segment=observation.segments[index];
            std::array<char,64> key{};
            if (segment.known_key_bit!=0 && segment.bytes<key.size())
                std::memcpy(key.data(),bytes.data()+segment.offset,segment.bytes);
            stereo_diagnostic_log("VRPromptCgDiag segment ordinal=%u index=%u role=%s offset=%u bytes=%u hash=%08X keyBit=%08X knownKey=%s marker16=%u payloadText=not-recorded sourceUnchanged=1",
                ordinal,static_cast<unsigned>(index),segment.localized_key?"localized-key":"literal-argument",
                segment.offset,segment.bytes,segment.hash,segment.known_key_bit,
                key[0]!=0?key.data():"not-allowlisted",segment.conversion_marker16?1U:0U);
        }
    }
    return copy.empty()?nullptr:copy.data();
}

extern "C" void __cdecl wawvr_observe_native_vr_prompt_encoded(
    const char* const message,const void* const native_context,const std::int32_t policy,
    const void* const item,const void* const context,const void* const native_stack) noexcept {
    if (!g_encoded_enabled.load(std::memory_order_acquire)) return;
    static thread_local bool observing=false;
    if (observing) return;
    observing=true;
    struct Reset final { bool& value; ~Reset() { value=false; } } reset{observing};
    const auto ordinal=g_encoded_calls.fetch_add(1,std::memory_order_relaxed)+1;
    std::array<char,1024> bytes{};
    // Exact constants are established by this one fingerprinted painter call;
    // they are NOT a general request to observe arbitrary formatter users.
    if (reinterpret_cast<std::uintptr_t>(message)!=0x0209823DU ||
        reinterpret_cast<std::uintptr_t>(native_context)!=0x008772A8U || policy!=0 ||
        !resident_page_access(message,bytes.size(),false,&VirtualQuery)) {
        g_encoded_rejected.fetch_add(1,std::memory_order_relaxed); return;
    }
    std::size_t length=0;
    for (;length<bytes.size();++length) {
        bytes[length]=message[length];
        if (bytes[length]=='\0') break;
    }
    const auto observation=length<bytes.size()
        ? observe_vr_encoded_prompt({bytes.data(),length}) : VrEncodedPromptObservation{};
    if (!observation.valid) {
        g_encoded_rejected.fetch_add(1,std::memory_order_relaxed); return;
    }
    if (observation.known_key_mask==0) {
        const auto unknown=g_encoded_unknown.fetch_add(1,std::memory_order_relaxed)+1;
        if (unknown<=4)
            stereo_diagnostic_log("VRPromptEncodedDiag unknown ordinal=%u bytes=%u segments=%u localized=%u literalArgs=%u route=ui-encoded callRva=001C6F05 nativeCalls=1 outputModified=0 payloadText=not-recorded",
                ordinal,static_cast<unsigned>(length),observation.segment_count,
                observation.localized_segments,observation.literal_arguments);
        return;
    }
    const auto new_bits=observation.known_key_mask &
        ~g_encoded_logged.fetch_or(observation.known_key_mask,std::memory_order_acq_rel);
    if (new_bits==0) return;
    std::uint32_t call_return=0,painter_return=0;
    const auto stack=reinterpret_cast<std::uintptr_t>(native_stack);
    constexpr std::uintptr_t caller_offset=0x438;
    bool stack_valid=stack!=0 && stack<=(std::numeric_limits<std::uintptr_t>::max)()-caller_offset;
    if (stack_valid) {
        const auto* const caller=reinterpret_cast<const void*>(stack+caller_offset);
        stack_valid=resident_page_access(native_stack,sizeof(call_return),false,&VirtualQuery) &&
            resident_page_access(caller,sizeof(painter_return),false,&VirtualQuery);
        if (stack_valid) {
            std::memcpy(&call_return,native_stack,sizeof(call_return));
            std::memcpy(&painter_return,caller,sizeof(painter_return));
            stack_valid=call_return==g_encoded_callsite+kEncodedOriginalCall.size();
        }
    }
    const auto caller_rva=stack_valid && painter_return>=g_encoded_module_begin &&
        painter_return<g_encoded_module_end ? painter_return-g_encoded_module_begin : 0;
    const auto presentation=read_t4_presentation_state();
    ControllerFrameSnapshot frame{};
    const bool has_frame=read_controller_frame(&frame);
    stereo_diagnostic_log("VRPromptEncodedDiag exact ordinal=%u keysMask=%08X newKeysMask=%08X firstKeyBit=%08X singleTargetGrammar=%u bytes=%u segments=%u localized=%u literalArgs=%u callRva=001C6F05 painterCallerRva=%08X stackValid=%u item=%08X context=%08X nativeContext=008772A8 nativePolicy=0 presentation=%u connection=%d keyCatchers=%u stereo=%u controllerCurrent=%u views=%u focused=%u nativeCalls=1 outputModified=0 scriptGetKeyBinding=unchanged",
        ordinal,observation.known_key_mask,new_bits,observation.segments[0].known_key_bit,
        observation.exact_single_target_key?1U:0U,static_cast<unsigned>(length),
        observation.segment_count,observation.localized_segments,observation.literal_arguments,
        static_cast<unsigned>(caller_rva),stack_valid?1U:0U,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(item)),
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(context)),
        presentation.valid?1U:0U,presentation.connection_state,presentation.key_catchers,
        stereo_scene_ready_for_current_com_frame()?1U:0U,
        has_frame && controller_frame_is_current(frame,GetTickCount64())?1U:0U,
        has_frame && frame.frame.views_valid?1U:0U,
        has_frame && frame.frame.actions.focused?1U:0U);
    for (std::size_t index=0;index<observation.segment_count;++index) {
        const auto& segment=observation.segments[index];
        std::array<char,64> known_key{};
        if (segment.known_key_bit!=0 && segment.bytes<known_key.size())
            std::memcpy(known_key.data(),bytes.data()+segment.offset,segment.bytes);
        stereo_diagnostic_log("VRPromptEncodedDiag segment ordinal=%u index=%u role=%s offset=%u bytes=%u hash=%08X keyBit=%08X knownKey=%s marker16=%u payloadText=not-recorded outputModified=0 contextualThrowbackEnabled=0",
            ordinal,static_cast<unsigned>(index),segment.localized_key?"localized-key":"literal-argument",
            segment.offset,segment.bytes,segment.hash,segment.known_key_bit,
            known_key[0]!=0?known_key.data():"not-allowlisted",segment.conversion_marker16?1U:0U);
    }
}

extern "C" void __cdecl wawvr_observe_native_vr_prompt_key(
    const char* const key,const void* const item,const void* const context,
    const void* const native_stack) noexcept {
    if (!g_key_enabled.load(std::memory_order_acquire) || !g_key_diagnostics) return;
    // No observer lock spans a native call. Re-entry cannot grow a TLS stack or
    // turn logging into a recursive rendering observer.
    static thread_local bool observing=false;
    if (observing) return;
    observing=true;
    struct Reset final { bool& value; ~Reset() { value=false; } } reset{observing};
    const auto ordinal=g_key_calls.fetch_add(1,std::memory_order_relaxed)+1;
    std::array<char,64> key_bytes{};
    std::string_view exact_key;
    if (!read_exact_command(key,key_bytes,exact_key)) {
        g_key_unreadable.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    const auto bit=observed_vr_localized_hint_bit(exact_key);
    const auto coop_bit=observed_vr_coop_loading_hint_bit(exact_key);
    const bool mission_start=exact_key=="MENU_CLICK_TO_START_THE_MISSION";
    if (bit==0 && coop_bit==0 && !mission_start) {
        const auto unknown=g_key_unknown.fetch_add(1,std::memory_order_relaxed)+1;
        // Prove the opt-in seam is executing without recording arbitrary
        // native strings, subtitles, player names or localized text.
        if (unknown<=4) {
            std::uint32_t hash=2166136261U;
            for (const unsigned char byte : exact_key) hash=(hash^byte)*16777619U;
            stereo_diagnostic_log("VRPromptKeyDiag unknown ordinal=%u keyBytes=%u keyHash=%08X route=ui-at-key callRva=001C6FA9 nativeCalls=1 outputModified=0",
                ordinal,static_cast<unsigned>(exact_key.size()),hash);
        }
        return;
    }
    if (mission_start) {
        if (g_key_mission_start_logged.exchange(true,std::memory_order_acq_rel)) return;
    } else if (coop_bit!=0) {
        if ((g_key_coop_logged.fetch_or(coop_bit,std::memory_order_acq_rel)&coop_bit)!=0) return;
    } else if ((g_key_logged.fetch_or(bit,std::memory_order_acq_rel)&bit)!=0) return;
    std::uint32_t call_return=0,painter_return=0;
    const auto stack=reinterpret_cast<std::uintptr_t>(native_stack);
    constexpr std::uintptr_t caller_offset=0x42C;
    bool stack_valid=stack!=0 && stack<=(std::numeric_limits<std::uintptr_t>::max)()-caller_offset;
    if (stack_valid) {
        const auto* const caller=reinterpret_cast<const void*>(stack+caller_offset);
        stack_valid=resident_page_access(native_stack,sizeof(call_return),false,&VirtualQuery) &&
            resident_page_access(caller,sizeof(painter_return),false,&VirtualQuery);
        if (stack_valid) {
            std::memcpy(&call_return,native_stack,sizeof(call_return));
            std::memcpy(&painter_return,caller,sizeof(painter_return));
            stack_valid=call_return==g_key_callsite+kKeyOriginalCall.size();
        }
    }
    const bool caller_in_module=stack_valid && painter_return>=g_key_module_begin &&
        painter_return<g_key_module_end;
    const auto caller_rva=caller_in_module ? painter_return-g_key_module_begin : 0;
    const auto presentation=read_t4_presentation_state();
    ControllerFrameSnapshot frame{};
    const bool has_frame=read_controller_frame(&frame);
    const bool current=has_frame && controller_frame_is_current(frame,GetTickCount64());
    if (mission_start) {
        // Record the real caller/state without authorizing any replacement or
        // claiming this caption proves that a loading tip was rendered.
        stereo_diagnostic_log("VRPromptKeyDiag mission-start key=MENU_CLICK_TO_START_THE_MISSION ordinal=%u route=ui-at-key callRva=001C6FA9 painterCallerRva=%08X stackValid=%u item=%08X context=%08X presentation=%u connection=%d keyCatchers=%u stereo=%u controllerCurrent=%u views=%u focused=%u nativeCalls=1 outputModified=0 classificationOnly=1 loadingTipCoverage=unproven",
            ordinal,static_cast<unsigned>(caller_rva),stack_valid?1U:0U,
            static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(item)),
            static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(context)),
            presentation.valid?1U:0U,presentation.connection_state,presentation.key_catchers,
            stereo_scene_ready_for_current_com_frame()?1U:0U,current?1U:0U,
            has_frame && frame.frame.views_valid?1U:0U,
            has_frame && frame.frame.actions.focused?1U:0U);
        return;
    }
    if (coop_bit!=0) {
        // Diagnostic identity only: same native call/context and unchanged
        // output, independently bounded to once per exact stock table key.
        stereo_diagnostic_log("VRPromptKeyDiag coop-loading key=%s bit=%016llX ordinal=%u route=ui-at-key callRva=001C6FA9 painterCallerRva=%08X stackValid=%u item=%08X context=%08X presentation=%u connection=%d keyCatchers=%u stereo=%u controllerCurrent=%u views=%u focused=%u nativeCalls=1 outputModified=0 scriptGetKeyBinding=unchanged contextualThrowbackEnabled=0",
            key_bytes.data(),static_cast<unsigned long long>(coop_bit),ordinal,
            static_cast<unsigned>(caller_rva),stack_valid?1U:0U,
            static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(item)),
            static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(context)),
            presentation.valid?1U:0U,presentation.connection_state,presentation.key_catchers,
            stereo_scene_ready_for_current_com_frame()?1U:0U,current?1U:0U,
            has_frame && frame.frame.views_valid?1U:0U,
            has_frame && frame.frame.actions.focused?1U:0U);
        return;
    }
    stereo_diagnostic_log("VRPromptKeyDiag exact key=%s bit=%08X ordinal=%u route=ui-at-key callRva=001C6FA9 painterCallerRva=%08X stackValid=%u item=%08X context=%08X presentation=%u connection=%d keyCatchers=%u stereo=%u controllerCurrent=%u views=%u focused=%u nativeCalls=1 outputModified=0 scriptGetKeyBinding=unchanged contextualThrowbackEnabled=0",
        key_bytes.data(),bit,ordinal,static_cast<unsigned>(caller_rva),stack_valid?1U:0U,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(item)),
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(context)),
        presentation.valid?1U:0U,presentation.connection_state,presentation.key_catchers,
        stereo_scene_ready_for_current_com_frame()?1U:0U,current?1U:0U,
        has_frame && frame.frame.views_valid?1U:0U,
        has_frame && frame.frame.actions.focused?1U:0U);
    observe_loading_item_first_key(exact_key,key,item,context,native_stack,call_return,painter_return);
}

extern "C" const char* __cdecl wawvr_resolve_native_vr_loading_tip(
    const char* const native_result,const char* const key,const void* const item,
    const void* const context,const void* const native_stack) noexcept {
    // The shared bridge has ALREADY called native exactly once and saved its
    // complete output state/LastError. The old diagnostic remains no-write.
    wawvr_observe_native_vr_prompt_key(key,item,context,native_stack);
    if(const auto caption=select_mission_start_caption(native_result,key,item,context,native_stack))return caption;
    if (!g_key_enabled.load(std::memory_order_acquire) ||
        !g_loading_enabled.load(std::memory_order_acquire)) return nullptr;
    static thread_local bool selecting=false;
    if (selecting) return nullptr;
    selecting=true;
    struct Reset final { bool& value; ~Reset() { value=false; } } reset{selecting};
    const auto presentation=read_t4_presentation_state();
    ControllerFrameSnapshot frame{};
    const bool has_frame=read_controller_frame(&frame);
    if (!loading_tip_frame_allows({
        .opt_in=true,.presentation_valid=presentation.valid,
        .frame_current=has_frame && controller_frame_is_current(frame,GetTickCount64()),
        .views_valid=has_frame && frame.frame.views_valid,
        .focused=has_frame && frame.frame.actions.focused,
        .connection=presentation.connection_state,.key_catchers=presentation.key_catchers})) return nullptr;
    std::array<char,64> key_bytes{};
    std::string_view exact_key;
    if (!read_exact_command(key,key_bytes,exact_key) || !loading_item_key_is_exact(exact_key)) return nullptr;
    std::uint32_t call_return{},parent_return{};
    if (!read_loading_item_word(native_stack,0,call_return) ||
        !read_loading_item_word(native_stack,0x42C,parent_return)) return nullptr;
    const auto first=read_loading_item_snapshot(key,item,context);
    const auto second=read_loading_item_snapshot(key,item,context);
    if (loading_item_observation_rejections(first,second,reinterpret_cast<std::uintptr_t>(key),
        reinterpret_cast<std::uintptr_t>(native_stack),call_return,
        static_cast<std::uint32_t>(g_key_callsite+5),parent_return,exact_key)!=0) return nullptr;
    // Exact complete English strings only; other languages/custom overrides
    // retain the original native pointer. Both bounded copies must agree.
    std::array<char,256> text{},text_again{};
    const auto copy_native=[&](std::array<char,256>& destination) noexcept {
        if (!native_result || !resident_page_access(native_result,destination.size(),false,&VirtualQuery)) return false;
        for (std::size_t index=0;index<destination.size();++index) {
            const auto byte=native_result[index];destination[index]=byte;
            if (byte==0) return true;
            if (byte<0x20 || byte>0x7E) return false;
        }
        return false;
    };
    if (!copy_native(text) || !copy_native(text_again) || text!=text_again) return nullptr;
    const auto replacement=resolve_vr_loading_tip_text(exact_key,std::string_view(text.data()),g_loading_stick_melee);
    if (replacement.empty()) return nullptr;
    const auto bit=observed_vr_localized_hint_bit(exact_key);
    if ((g_loading_copied.fetch_or(bit,std::memory_order_acq_rel)&bit)==0)
        stereo_diagnostic_log("VRLoadingTipDiag replaced key=%s route=ui-at-key-post-return callRva=001C6FA9 parentRva=001C98DB sourceStable=1 nativeEnglishMatch=1 frontendConnection=2 keyCatchers=16 focusedCurrentViews=1 optIn=1 nativeCalls=1 outputModified=1 immutableReturn=1 contextualThrowbackEnabled=0",
            key_bytes.data());
    return replacement.data();
}

extern "C" int __cdecl wawvr_resolve_native_vr_binding_prompt(
    const char* command,const std::int32_t local_client,void* output) noexcept {
    if (!g_enabled.load(std::memory_order_acquire) || local_client!=0 || !output)
        return 0;
    const auto presentation=read_t4_presentation_state();
    if (!presentation.valid || presentation.key_catchers!=0 ||
        presentation.connection_state!=presentation.active_connection_state ||
        !stereo_scene_ready_for_current_com_frame()) return 0;
    ControllerFrameSnapshot frame{};
    if (!read_controller_frame(&frame) || !frame.frame.views_valid ||
        !frame.frame.actions.focused ||
        !controller_frame_is_current(frame,GetTickCount64())) return 0;
    std::array<char,64> command_bytes{};
    std::string_view exact_command;
    if (!read_exact_command(command,command_bytes,exact_command)) return 0;
    static const bool automatic=setting(kAutomaticReloadEnvironmentVariable);
    static const bool stick_melee=setting(L"WAWVR_RIGHT_STICK_MELEE");
    const auto label=resolve_vr_native_binding_prompt(exact_command,{
        .automatic_reload=automatic,
        .button_grenades=button_grenade_mode_enabled(),
        .right_stick_melee=stick_melee,
        .support_weapon_selected=vr_gameplay_prompt_support_weapon_selected(),
        .left_binding_profile=frame.frame.actions.hands[0].binding_profile,
        .right_binding_profile=frame.frame.actions.hands[1].binding_profile,
    });
    if (label.empty() || label.size()>=128 ||
        !resident_page_access(output,256,true,&VirtualQuery)) return 0;
    // The lower native resolver owns exactly two 128-byte label slots. Return
    // one binding; the stock display resolver performs its normal localization
    // fallback/copy and the stock HUD keeps its full sentence/weapon name.
    std::memset(output,0,256);
    std::memcpy(output,label.data(),label.size());
    std::uint32_t command_index=0;
    for (const auto known : {"+activate","+usereload","+reload","+sprint",
                            "+melee","+frag","+actionslot 4","+attack"}) {
        if (known==exact_command) break;
        ++command_index;
    }
    const auto mask=1U<<command_index;
    if ((g_logged_commands.fetch_or(mask,std::memory_order_acq_rel)&mask)==0) {
        stereo_diagnostic_log("VRPromptDiag native display binding mapped: command=%s label=%.*s scriptGetKeyBinding=unchanged sentences/subtitles=unchanged",
            command_bytes.data(),static_cast<int>(label.size()),label.data());
    }
    return 1;
}

#include "native_prompt_binding_bridge.inl"
#include "native_prompt_loading_bridge.inl"

namespace {
void install_key_observer(const wawvr::t4::ValidatedBindings& bindings) noexcept {
    const bool diagnostics=setting(L"WAWVR_PROMPT_KEY_DIAGNOSTICS");
    const bool loading=setting(L"WAWVR_TARGETED_LOADING_TIPS");
    const auto mission_start=read_mission_start_setting();
    if (!mission_start_key_bridge_requested(diagnostics,loading,mission_start)) return;
#if defined(_MSC_VER) && defined(_M_IX86)
    const auto module=bindings.module();
    if (bindings.profile().id!="t4-steam-sp-1.7.1263-cl350073-732900d1" ||
        reinterpret_cast<std::uintptr_t>(module.base)!=0x00400000U ||
        module.size>(std::numeric_limits<std::uintptr_t>::max)()-0x00400000U) {
        stereo_diagnostic_log("VRPromptKeyDiag disabled reason=unproven-exact-profile outputModified=0");
        return;
    }
    const auto call=module.address(kKeyCallRva,kKeyOriginalCall.size());
    const auto context=module.address(kKeyContextRva,kKeyContext.size());
    const auto painter=module.address(kKeyPainterRva,kKeyPainter.size());
    const auto target=module.address(kKeyTargetRva,kLoadingTargetBytes);
    const auto parent=module.address(kLoadingParentRva,kLoadingParent.size());
    if (!call || !context || !painter || !target || !parent ||
        !matches(*call,kKeyOriginalCall) || !matches(*context,kKeyContext) ||
        !matches(*painter,kKeyPainter) || !matches(*target,kKeyTarget) ||
        !key_witness_matches()) {
        stereo_diagnostic_log("VRPromptKeyDiag disabled reason=exact-display-fingerprint-mismatch outputModified=0");
        return;
    }
    const auto bridge=reinterpret_cast<std::uintptr_t>(&wawvr_native_vr_loading_tip_bridge);
    const auto delta=static_cast<std::int64_t>(bridge)-static_cast<std::int64_t>(*call+5);
    if (delta<(std::numeric_limits<std::int32_t>::min)() ||
        delta>(std::numeric_limits<std::int32_t>::max)()) {
        stereo_diagnostic_log("VRPromptKeyDiag disabled reason=bridge-out-of-range outputModified=0");
        return;
    }
    std::array<std::uint8_t,5> replacement{0xE8};
    const auto displacement=static_cast<std::int32_t>(delta);
    std::memcpy(replacement.data()+1,&displacement,sizeof(displacement));
    wawvr_native_vr_prompt_key_original=*target; // immutable through teardown
    g_key_callsite=*call; g_key_replacement=replacement;
    g_key_module_begin=reinterpret_cast<std::uintptr_t>(module.base);
    g_key_module_end=g_key_module_begin+module.size;
    const auto result=replace_call(*call,kKeyOriginalCall,replacement,0,DisplayWitness::at_key);
    if (result!=PatchResult::okay) {
        stereo_diagnostic_log("VRPromptKeyDiag disabled reason=display-call-patch-failed result=%u outputModified=0",
            static_cast<unsigned>(result));
        return;
    }
    g_key_calls.store(0,std::memory_order_relaxed);
    g_key_logged.store(0,std::memory_order_relaxed);
    g_key_coop_logged.store(0,std::memory_order_relaxed);
    g_key_mission_start_logged.store(false,std::memory_order_relaxed);
    g_key_unknown.store(0,std::memory_order_relaxed);
    g_key_unreadable.store(0,std::memory_order_relaxed);
    g_key_diagnostics=diagnostics;
    g_loading_stick_melee=setting(L"WAWVR_RIGHT_STICK_MELEE");
    g_loading_copied.store(0,std::memory_order_relaxed);
    g_mission_start_observed.store(false,std::memory_order_relaxed);
    g_mission_start_copied.store(false,std::memory_order_relaxed);
    g_mission_start_rejections.store(0,std::memory_order_relaxed);
    g_mission_start_diagnostics.store(0,std::memory_order_relaxed);
    g_mission_start_setting_source=mission_start.source;
    g_key_installed.store(true,std::memory_order_release);
    g_key_summary_pending.store(diagnostics,std::memory_order_release);
    g_loading_enabled.store(loading,std::memory_order_release);
    g_mission_start_enabled.store(mission_start.enabled,std::memory_order_release);
    g_key_enabled.store(true,std::memory_order_release);
    if (diagnostics)
        stereo_diagnostic_log("VRPromptKeyDiag installed observerOnly=1 route=ui-at-key callRva=001C6FA9 localizerRva=001BB5E0 exactKeys=22 coopLoadingKeys=33 separateCoopMask=1 unknownSamplesMax=4 nativeCalls=1 outputModified=0 subtitles/scripts=unchanged phase=post-native");
    stereo_diagnostic_log("VRLoadingTipDiag installed optIn=%u exactKeys=4 frontendOnly=1 nativeCalls=1 nativeOutputsPreservedExceptEligibleEax=1 nativeLastErrorPreserved=1 naturalLoadingCoverage=unproven",
        loading?1U:0U);
    stereo_diagnostic_log("VRMissionStartDiag installed enabled=%u settingPolicy=%s exactKeys=1 loadingConnections=6,7 keyCatchers=16 inlineSourceOnly=1 nativeCalls=1 nativeOutputsPreservedExceptEligibleEax=1 nativeLastErrorPreserved=1",
        mission_start.enabled?1U:0U,mission_start_setting_source_name(mission_start.source));
#else
    static_cast<void>(bindings);
#endif
}

bool restore_key_observer() noexcept {
    if (!g_key_installed.load(std::memory_order_acquire)) return true;
#if defined(_MSC_VER) && defined(_M_IX86)
    const auto result=replace_call(g_key_callsite,g_key_replacement,kKeyOriginalCall,
        reinterpret_cast<std::uintptr_t>(&wawvr_native_vr_loading_tip_bridge),
        DisplayWitness::none,kLoadingBridgeGuardBytes);
    if (result==PatchResult::okay || result==PatchResult::foreign) {
        g_key_installed.store(false,std::memory_order_release);
        return true;
    }
#endif
    return false;
}

void install_encoded_observer(const wawvr::t4::ValidatedBindings& bindings) noexcept {
    if (!setting(L"WAWVR_PROMPT_KEY_DIAGNOSTICS")) return;
#if defined(_MSC_VER) && defined(_M_IX86)
    const auto module=bindings.module();
    if (bindings.profile().id!="t4-steam-sp-1.7.1263-cl350073-732900d1" ||
        reinterpret_cast<std::uintptr_t>(module.base)!=0x00400000U ||
        module.size>(std::numeric_limits<std::uintptr_t>::max)()-0x00400000U) {
        stereo_diagnostic_log("VRPromptEncodedDiag disabled reason=unproven-exact-profile outputModified=0");
        return;
    }
    const auto call=module.address(kEncodedCallRva,kEncodedOriginalCall.size());
    const auto context=module.address(kEncodedContextRva,kEncodedContext.size());
    const auto painter=module.address(kKeyPainterRva,kKeyPainter.size());
    const auto target=module.address(kEncodedTargetRva,kEncodedTarget.size());
    if (!call || !context || !painter || !target || !encoded_witness_matches()) {
        stereo_diagnostic_log("VRPromptEncodedDiag disabled reason=exact-display-fingerprint-mismatch outputModified=0");
        return;
    }
    const auto bridge=reinterpret_cast<std::uintptr_t>(&wawvr_native_vr_prompt_encoded_bridge);
    const auto delta=static_cast<std::int64_t>(bridge)-static_cast<std::int64_t>(*call+5);
    if (delta<(std::numeric_limits<std::int32_t>::min)() ||
        delta>(std::numeric_limits<std::int32_t>::max)()) {
        stereo_diagnostic_log("VRPromptEncodedDiag disabled reason=bridge-out-of-range outputModified=0");
        return;
    }
    std::array<std::uint8_t,5> replacement{0xE8};
    const auto displacement=static_cast<std::int32_t>(delta);
    std::memcpy(replacement.data()+1,&displacement,sizeof(displacement));
    wawvr_native_vr_prompt_encoded_original=*target;
    g_encoded_callsite=*call; g_encoded_replacement=replacement;
    g_encoded_module_begin=reinterpret_cast<std::uintptr_t>(module.base);
    g_encoded_module_end=g_encoded_module_begin+module.size;
    const auto result=replace_call(*call,kEncodedOriginalCall,replacement,0,DisplayWitness::encoded);
    if (result!=PatchResult::okay) {
        stereo_diagnostic_log("VRPromptEncodedDiag disabled reason=display-call-patch-failed result=%u outputModified=0",
            static_cast<unsigned>(result));
        return;
    }
    g_encoded_calls.store(0,std::memory_order_relaxed);
    g_encoded_logged.store(0,std::memory_order_relaxed);
    g_encoded_unknown.store(0,std::memory_order_relaxed);
    g_encoded_rejected.store(0,std::memory_order_relaxed);
    g_encoded_installed.store(true,std::memory_order_release);
    g_encoded_summary_pending.store(true,std::memory_order_release);
    g_encoded_enabled.store(true,std::memory_order_release);
    stereo_diagnostic_log("VRPromptEncodedDiag installed observerOnly=1 route=ui-encoded callRva=001C6F05 formatterRva=001ACF20 bytesMax=1023 segmentsMax=8 exactKeys=22 unknownSamplesMax=4 nativeCalls=1 outputModified=0 subtitles/scripts=unchanged");
#else
    static_cast<void>(bindings);
#endif
}

bool restore_encoded_observer() noexcept {
    if (!g_encoded_installed.load(std::memory_order_acquire)) return true;
#if defined(_MSC_VER) && defined(_M_IX86)
    const auto result=replace_call(g_encoded_callsite,g_encoded_replacement,kEncodedOriginalCall,
        reinterpret_cast<std::uintptr_t>(&wawvr_native_vr_prompt_encoded_bridge));
    if (result==PatchResult::okay || result==PatchResult::foreign) {
        g_encoded_installed.store(false,std::memory_order_release); return true;
    }
#endif
    return false;
}

void install_cg_display_adapter(const wawvr::t4::ValidatedBindings& bindings) noexcept {
    const bool diagnostics=setting(L"WAWVR_PROMPT_KEY_DIAGNOSTICS");
    const bool translation=setting(L"WAWVR_TARGETED_USE_HINTS");
    if (!diagnostics && !translation) return;
#if defined(_MSC_VER) && defined(_M_IX86)
    const auto module=bindings.module();
    if (bindings.profile().id!="t4-steam-sp-1.7.1263-cl350073-732900d1" ||
        reinterpret_cast<std::uintptr_t>(module.base)!=0x00400000U) {
        stereo_diagnostic_log("VRPromptCgDiag disabled reason=unproven-exact-profile sourceUnchanged=1");
        return;
    }
    const auto call=module.address(kCgCallRva,kCgOriginalCall.size());
    const auto body=module.address(kCgEntryRva,kCgFunctionBytes);
    const auto parent=module.address(kCgParentContextRva,kCgParentContext.size());
    const auto target=module.address(kEncodedTargetRva,kEncodedTarget.size());
    if (!call || !body || !parent || !target || !cg_witness_matches()) {
        stereo_diagnostic_log("VRPromptCgDiag disabled reason=exact-display-chain-fingerprint-mismatch sourceUnchanged=1");
        return;
    }
    const auto bridge=reinterpret_cast<std::uintptr_t>(&wawvr_native_vr_prompt_cg_bridge);
    const auto delta=static_cast<std::int64_t>(bridge)-static_cast<std::int64_t>(*call+5);
    if (delta<(std::numeric_limits<std::int32_t>::min)() ||
        delta>(std::numeric_limits<std::int32_t>::max)()) {
        stereo_diagnostic_log("VRPromptCgDiag disabled reason=bridge-out-of-range sourceUnchanged=1");
        return;
    }
    std::array<std::uint8_t,5> replacement{0xE8};
    const auto displacement=static_cast<std::int32_t>(delta);
    std::memcpy(replacement.data()+1,&displacement,sizeof(displacement));
    wawvr_native_vr_prompt_cg_original=*target;
    g_cg_callsite=*call; g_cg_replacement=replacement;
    g_cg_diagnostics=diagnostics; g_cg_translation=translation;
    const auto result=replace_call(*call,kCgOriginalCall,replacement,0,DisplayWitness::cg);
    if (result!=PatchResult::okay) {
        stereo_diagnostic_log("VRPromptCgDiag disabled reason=display-call-patch-failed result=%u sourceUnchanged=1",
            static_cast<unsigned>(result)); return;
    }
    g_cg_calls.store(0,std::memory_order_relaxed);
    g_cg_entry_logged.store(false,std::memory_order_relaxed);
    g_cg_rejection_logged.store(0,std::memory_order_relaxed);
    g_cg_logged.store(0,std::memory_order_relaxed);
    g_cg_copy_logged.store(0,std::memory_order_relaxed);
    g_cg_copied.store(0,std::memory_order_relaxed);
    g_cg_unknown.store(0,std::memory_order_relaxed);
    g_cg_rejected.store(0,std::memory_order_relaxed);
    g_cg_installed.store(true,std::memory_order_release);
    g_cg_summary_pending.store(true,std::memory_order_release);
    g_cg_enabled.store(true,std::memory_order_release);
    stereo_diagnostic_log("VRPromptCgDiag installed callRva=00037FE5 formatterRva=001ACF20 parentRva=0003892D functionBytes=798 diagnostics=%u translationOptIn=%u useKeys=12 exactLiteral=F unknownSamplesMax=4 nativeCalls=1 originalSourceUnchanged=1 outputOwner=native-formatter subtitles/scripts=unchanged",
        diagnostics?1U:0U,translation?1U:0U);
#else
    static_cast<void>(bindings);
#endif
}

bool restore_cg_display_adapter() noexcept {
    if (!g_cg_installed.load(std::memory_order_acquire)) return true;
#if defined(_MSC_VER) && defined(_M_IX86)
    const auto result=replace_call(g_cg_callsite,g_cg_replacement,kCgOriginalCall,
        reinterpret_cast<std::uintptr_t>(&wawvr_native_vr_prompt_cg_bridge));
    if (result==PatchResult::okay || result==PatchResult::foreign) {
        g_cg_installed.store(false,std::memory_order_release); return true;
    }
#endif
    return false;
}

void install_cursor_display_adapter(const wawvr::t4::ValidatedBindings& bindings) noexcept {
    const bool diagnostics=setting(L"WAWVR_PROMPT_KEY_DIAGNOSTICS");
    const bool translation=setting(L"WAWVR_TARGETED_USE_HINTS");
    if (!diagnostics && !translation) return;
#if defined(_MSC_VER) && defined(_M_IX86)
    const auto module=bindings.module();
    if (bindings.profile().id!="t4-steam-sp-1.7.1263-cl350073-732900d1" ||
        reinterpret_cast<std::uintptr_t>(module.base)!=0x00400000U) {
        stereo_diagnostic_log("VRPromptCursorDiag disabled reason=unproven-exact-profile sourceUnchanged=1");
        return;
    }
    const auto call=module.address(kCursorCallRva,kCursorOriginalCall.size());
    const auto body=module.address(kCursorEntryRva,kCursorFunctionBytes);
    const auto painter=module.address(kCursorPainterRva,kCursorPainterBytes);
    const auto target=module.address(kEncodedTargetRva,kEncodedTarget.size());
    if (!call || !body || !painter || !target || !cursor_witness_matches()) {
        stereo_diagnostic_log("VRPromptCursorDiag disabled reason=exact-cursor-display-chain-fingerprint-mismatch sourceUnchanged=1");
        return;
    }
    const auto bridge=reinterpret_cast<std::uintptr_t>(&wawvr_native_vr_prompt_cursor_bridge);
    const auto delta=static_cast<std::int64_t>(bridge)-static_cast<std::int64_t>(*call+5);
    if (delta<(std::numeric_limits<std::int32_t>::min)() ||
        delta>(std::numeric_limits<std::int32_t>::max)()) {
        stereo_diagnostic_log("VRPromptCursorDiag disabled reason=bridge-out-of-range sourceUnchanged=1");
        return;
    }
    std::array<std::uint8_t,5> replacement{0xE8};
    const auto displacement=static_cast<std::int32_t>(delta);
    std::memcpy(replacement.data()+1,&displacement,sizeof(displacement));
    wawvr_native_vr_prompt_cursor_original=*target;
    g_cursor.callsite=*call; g_cursor.replacement=replacement;
    g_cursor.diagnostics=diagnostics; g_cursor.translation=translation;
    const auto result=replace_call(*call,kCursorOriginalCall,replacement,0,DisplayWitness::cursor);
    if (result!=PatchResult::okay) {
        stereo_diagnostic_log("VRPromptCursorDiag disabled reason=display-call-patch-failed result=%u sourceUnchanged=1",
            static_cast<unsigned>(result)); return;
    }
    g_cursor.calls.store(0,std::memory_order_relaxed);
    g_cursor.logged.store(0,std::memory_order_relaxed);
    g_cursor.copy_logged.store(0,std::memory_order_relaxed);
    g_cursor.copied.store(0,std::memory_order_relaxed);
    g_cursor.unknown.store(0,std::memory_order_relaxed);
    g_cursor.rejected.store(0,std::memory_order_relaxed);
    g_cursor.rejection_logged.store(0,std::memory_order_relaxed);
    g_cursor.entry_logged.store(false,std::memory_order_relaxed);
    g_cursor.installed.store(true,std::memory_order_release);
    g_cursor.summary_pending.store(true,std::memory_order_release);
    g_cursor.enabled.store(true,std::memory_order_release);
    stereo_diagnostic_log("VRPromptCursorDiag installed callRva=000512E4 formatterRva=001ACF20 parentOffset=11C functionBytes=139 painterBytes=1772 diagnostics=%u translationOptIn=%u useKeys=12 exactLiteral=F unknownSamplesMax=4 sourceUnchanged=1 subtitles/scripts=unchanged",
        diagnostics?1U:0U,translation?1U:0U);
#else
    static_cast<void>(bindings);
#endif
}

bool restore_cursor_display_adapter() noexcept {
    if (!g_cursor.installed.load(std::memory_order_acquire)) return true;
#if defined(_MSC_VER) && defined(_M_IX86)
    const auto result=replace_call(g_cursor.callsite,g_cursor.replacement,kCursorOriginalCall,
        reinterpret_cast<std::uintptr_t>(&wawvr_native_vr_prompt_cursor_bridge));
    if (result==PatchResult::okay || result==PatchResult::foreign) {
        g_cursor.installed.store(false,std::memory_order_release); return true;
    }
#endif
    return false;
}
} // namespace

NativePromptBindingHookStatus install_native_prompt_binding_hook(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    if (g_installed.load(std::memory_order_acquire))
        return matches(g_callsite,g_replacement)
            ? NativePromptBindingHookStatus::already_installed
            : NativePromptBindingHookStatus::fingerprint_mismatch;
    if (setting(L"WAWVR_DISABLE_XR") || setting(L"WAWVR_DISABLE_VR_PROMPTS"))
        return NativePromptBindingHookStatus::disabled;
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    return NativePromptBindingHookStatus::unsupported;
#else
    if (select_t4_layout_family(bindings.profile())!=T4LayoutFamily::single_player_1_7_1263 ||
        reinterpret_cast<std::uintptr_t>(bindings.module().base)!=0x00400000U)
        return NativePromptBindingHookStatus::unsupported;
    const auto call=bindings.module().address(kDisplayCallRva,kOriginalCall.size());
    const auto entry=bindings.module().address(kDisplayEntryRva,kDisplayEntry.size());
    const auto context=bindings.module().address(kDisplayContextRva,kDisplayContext.size());
    const auto target=bindings.module().address(kBindingTargetRva,kBindingTarget.size());
    if (!call || !entry || !context || !target ||
        !matches(*call,kOriginalCall) || !matches(*entry,kDisplayEntry) ||
        !matches(*context,kDisplayContext) || !matches(*target,kBindingTarget))
        return NativePromptBindingHookStatus::fingerprint_mismatch;
    const auto bridge=reinterpret_cast<std::uintptr_t>(&wawvr_native_vr_binding_prompt_bridge);
    const auto delta=static_cast<std::int64_t>(bridge)-static_cast<std::int64_t>(*call+5);
    if (delta<(std::numeric_limits<std::int32_t>::min)() ||
        delta>(std::numeric_limits<std::int32_t>::max)())
        return NativePromptBindingHookStatus::patch_failed;
    std::array<std::uint8_t,5> replacement{0xE8};
    const auto displacement=static_cast<std::int32_t>(delta);
    std::memcpy(replacement.data()+1,&displacement,sizeof(displacement));
    wawvr_native_vr_binding_prompt_original=*target;
    const auto result=replace_call(*call,kOriginalCall,replacement,0);
    if (result==PatchResult::quiescence)
        return NativePromptBindingHookStatus::thread_suspend_failed;
    if (result!=PatchResult::okay)
        return NativePromptBindingHookStatus::patch_failed;
    g_callsite=*call; g_replacement=replacement;
    g_logged_commands.store(0,std::memory_order_release);
    g_installed.store(true,std::memory_order_release);
    g_enabled.store(true,std::memory_order_release);
    install_key_observer(bindings);
    install_encoded_observer(bindings);
    install_cg_display_adapter(bindings);
    install_cursor_display_adapter(bindings);
    return NativePromptBindingHookStatus::installed;
#endif
}

void request_native_prompt_binding_hook_shutdown() noexcept {
    g_enabled.store(false,std::memory_order_release);
    g_key_enabled.store(false,std::memory_order_release);
    g_loading_enabled.store(false,std::memory_order_release);
    g_mission_start_enabled.store(false,std::memory_order_release);
    g_encoded_enabled.store(false,std::memory_order_release);
    g_cg_enabled.store(false,std::memory_order_release);
    g_cursor.enabled.store(false,std::memory_order_release);
    if (g_key_summary_pending.exchange(false,std::memory_order_acq_rel))
        stereo_diagnostic_log("VRPromptKeyDiag summary calls=%u exactSeenMask=%08X unknown=%u unreadable=%u coopLoadingSeenMask=%016llX outputModified=0 scriptGetKeyBinding=unchanged",
            g_key_calls.load(std::memory_order_relaxed),g_key_logged.load(std::memory_order_relaxed),
            g_key_unknown.load(std::memory_order_relaxed),g_key_unreadable.load(std::memory_order_relaxed),
            static_cast<unsigned long long>(g_key_coop_logged.load(std::memory_order_relaxed)));
    if (g_encoded_summary_pending.exchange(false,std::memory_order_acq_rel))
        stereo_diagnostic_log("VRPromptEncodedDiag summary calls=%u exactSeenMask=%08X unknown=%u rejected=%u outputModified=0 scriptGetKeyBinding=unchanged",
            g_encoded_calls.load(std::memory_order_relaxed),g_encoded_logged.load(std::memory_order_relaxed),
            g_encoded_unknown.load(std::memory_order_relaxed),g_encoded_rejected.load(std::memory_order_relaxed));
    if (g_cg_summary_pending.exchange(false,std::memory_order_acq_rel))
        stereo_diagnostic_log("VRPromptCgDiag summary calls=%u exactSeenMask=%08X translatedMask=%08X copiedCalls=%u unknown=%u rejected=%u translationOptIn=%u originalSourceUnchanged=1 outputOwner=native-formatter",
            g_cg_calls.load(std::memory_order_relaxed),g_cg_logged.load(std::memory_order_relaxed),
            g_cg_copy_logged.load(std::memory_order_relaxed),g_cg_copied.load(std::memory_order_relaxed),
            g_cg_unknown.load(std::memory_order_relaxed),g_cg_rejected.load(std::memory_order_relaxed),g_cg_translation?1U:0U);
    if (g_cursor.summary_pending.exchange(false,std::memory_order_acq_rel))
        stereo_diagnostic_log("VRPromptCursorDiag summary calls=%u exactSeenMask=%08X translatedMask=%08X copiedCalls=%u unknown=%u rejected=%u rejectionReasons=%03X translationOptIn=%u originalSourceUnchanged=1 outputOwner=native-formatter-and-conversion",
            g_cursor.calls.load(std::memory_order_relaxed),g_cursor.logged.load(std::memory_order_relaxed),
            g_cursor.copy_logged.load(std::memory_order_relaxed),g_cursor.copied.load(std::memory_order_relaxed),
            g_cursor.unknown.load(std::memory_order_relaxed),g_cursor.rejected.load(std::memory_order_relaxed),
            g_cursor.rejection_logged.load(std::memory_order_relaxed),g_cursor.translation?1U:0U);
}

bool native_prompt_binding_hook_installed() noexcept {
    return g_installed.load(std::memory_order_acquire) &&
        g_enabled.load(std::memory_order_acquire);
}

bool restore_native_prompt_binding_hook() noexcept {
    request_native_prompt_binding_hook_shutdown();
    const bool key_restored=restore_key_observer();
    const bool encoded_restored=restore_encoded_observer();
    const bool cg_restored=restore_cg_display_adapter();
    const bool cursor_restored=restore_cursor_display_adapter();
    if (!g_installed.load(std::memory_order_acquire)) return key_restored && encoded_restored && cg_restored && cursor_restored;
#if defined(_MSC_VER) && defined(_M_IX86)
    const auto result=replace_call(g_callsite,g_replacement,kOriginalCall,
        reinterpret_cast<std::uintptr_t>(&wawvr_native_vr_binding_prompt_bridge));
    if (result==PatchResult::okay || result==PatchResult::foreign) {
        g_installed.store(false,std::memory_order_release);
        return key_restored && encoded_restored && cg_restored && cursor_restored;
    }
#endif
    return false;
}
} // namespace wawvr::mod
