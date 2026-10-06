// SPDX-License-Identifier: GPL-3.0-only
#include "t4_menu_input.hpp"

#include "campaign_unlock_logic.hpp"
#include "input_mapping.hpp"
#include "input_hook.hpp"
#include "aircraft_control_runtime.hpp"
#include "manual_reload_runtime.hpp"
#include "menu_panel_logic.hpp"
#include "menu_surface_logic.hpp"
#include "native_menu_mouse_gate_logic.hpp"
#include "performance_command_service.hpp"
#include "peer_thread_quiescence.hpp"
#include "simulator_map_handoff_logic.hpp"
#include "stereo_diagnostics.hpp"
#include "t4_presentation_state.hpp"

#include "t4/hook_api.hpp"
#include "t4/profile.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace wawvr::mod {

// Published before the native mouse call is patched and deliberately retained
// for the process lifetime. An in-flight bridge can therefore always finish
// its tail jump even while shutdown restores the original callsite.
extern "C" std::uintptr_t wawvr_t4_original_ui_mouse_event_address = 0;

#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" void wawvr_t4_native_menu_mouse_gate_bridge() noexcept;
#endif

#if defined(_MSC_VER) && defined(_M_IX86)
// Exact T4 Cbuf_AddText contract: EAX=text, ECX=local command-buffer index.
// The target uses a plain ret and preserves the x86 callee-saved registers.
extern "C" __declspec(naked) void __cdecl wawvr_call_t4_cbuf_add_text(
    std::uintptr_t, const char*) noexcept {
    __asm {
        mov edx, dword ptr [esp + 4]
        mov eax, dword ptr [esp + 8]
        xor ecx, ecx
        call edx
        ret
    }
}
#endif

namespace {

using ClKeyEventFunction = void(__cdecl*)(
    std::int32_t local_client_num,
    std::int32_t key,
    std::int32_t down,
    std::uint32_t time);
using UiMouseEventFunction = void(__cdecl*)(
    std::int32_t x,
    std::int32_t y);

std::atomic<std::uintptr_t> g_cl_key_event_address{0};
std::atomic<std::uintptr_t> g_ui_mouse_event_address{0};
std::atomic<std::uintptr_t> g_cbuf_add_text_address{0};
std::atomic<bool> g_native_mouse_gate_installed{false};
std::uintptr_t g_native_mouse_gate_callsite = 0;
constexpr std::size_t kNativeMouseGateCallSize = 5;
// Release x86 disassembly is exactly 17 bytes through the terminal ret.
constexpr std::size_t kNativeMouseGateBridgeExecutionGuardSize = 17;
std::array<std::uint8_t, kNativeMouseGateCallSize>
    g_native_mouse_gate_original_call{};
std::array<std::uint8_t, kNativeMouseGateCallSize>
    g_native_mouse_gate_replacement_call{};
std::atomic<bool> g_single_player_profile{false};
std::atomic<bool> g_multiplayer_profile{false};
std::atomic<bool> g_campaign_unlock_queued{false};
std::atomic<bool> g_pezbot_autofill_enabled{false};
enum class SimulatorBoltActionWeapon : std::uint8_t {
    None,
    ColtProbe,
    M1Carbine,
    M1Garand,
    M1GarandBayonet,
    M1GarandGrenadeLauncher,
    Kar98,
    Kar98ScopedZombie,
    Springfield,
    Mosin,
    MosinScoped,
    Gewehr43,
    Stg44,
    Mp40,
    Thompson,
    Bar,
    Fg42Bipod,
    Walther,
    Colt,
    Tokarev,
    Nambu,
    Svt40,
    Ppsh,
    Type100,
    Type99Rifle,
    Type99RifleBayonet,
    Type99RifleScoped,
    Type99Lmg,
    Dp28,
    Fg42,
    BarBipodAsset,
    Type99LmgBipod,
    Type100NoSound,
    ThompsonWet,
    ColtWet,
    ZombieMagazineInventory,
    ZombieMagazineProfileSweep,
    HeadsetZombieM1CarbineUpgraded,
    HeadsetZombieMagazineWeapon,
    HeadsetSvt40,
    HeadsetM1GarandGrenadeLauncher,
};
enum class SimulatorWeaponEquipPhase : std::uint8_t {
    Direct,
    NeedMap,
    Queueing,
    WaitingForMapTransition,
    NeedWeapon,
    Complete,
};
std::atomic<SimulatorBoltActionWeapon> g_simulator_weapon{
    SimulatorBoltActionWeapon::None};
std::atomic<SimulatorWeaponEquipPhase> g_simulator_weapon_equip_phase{
    SimulatorWeaponEquipPhase::Complete};
std::atomic<bool> g_simulator_kar98_queued{false};
std::atomic<bool> g_simulator_map_transition_seen{false};
std::atomic<std::uint64_t> g_simulator_map_command_queued_at{0};
std::atomic<std::uint64_t> g_simulator_kar98_eligible_since{0};
std::atomic<bool> g_simulator_kar98_service_seen{false};
std::atomic<bool> g_simulator_kar98_start_trigger_seen{false};
std::atomic<bool> g_simulator_kar98_start_trigger_released{false};
std::atomic<std::uint64_t> g_headset_svt40_selection_not_before{0};
std::atomic<std::uint64_t> g_headset_svt40_next_cycle_at{0};
std::atomic<std::uint32_t> g_headset_svt40_cycle_attempts{0};
std::atomic<bool> g_headset_svt40_selection_confirmed_logged{false};
std::atomic<bool> g_headset_svt40_selection_exhausted_logged{false};
std::atomic<std::uint64_t> g_headset_m1garand_gl_selection_not_before{0};
std::atomic<std::uint64_t> g_headset_m1garand_gl_next_cycle_at{0};
std::atomic<std::uint32_t> g_headset_m1garand_gl_cycle_attempts{0};
std::atomic<bool> g_headset_m1garand_gl_pair_installed{false};
std::atomic<bool> g_headset_m1garand_gl_selection_confirmed_logged{false};
std::atomic<bool> g_headset_m1garand_gl_selection_exhausted_logged{false};
std::atomic<std::uintptr_t> g_headset_test_local_player_entity{0};
std::atomic<std::uintptr_t> g_headset_test_weapon_definition_table{0};
std::atomic<std::uintptr_t> g_headset_test_last_weapon_index{0};
std::atomic<std::uintptr_t> g_headset_test_select_weapon_index{0};
std::atomic<std::uintptr_t> g_headset_test_give_player_weapon{0};
std::atomic<std::uint32_t> g_headset_m1garand_gl_rifle_index{0};
std::array<char, 512> g_headset_zombie_magazine_command{};
bool g_headset_zombie_magazine_uses_nacht_m1garand = false;
PezBotAutofillState g_pezbot_autofill_state{};

constexpr char kWeaponNextCommand[] = "weapnext\n";
constexpr char kPezBotAutofillCommand[] = "set svr_pezbots 9\n";
// The simulator drives the rifle unattended, so keep its disposable test
// player alive long enough to complete the scripted grip/bolt sequence.
// This path is gated by WAWVR_SIMULATOR_EQUIP_KAR98 and is never enabled by a
// normal Meta or Virtual Desktop launch.
constexpr char kSimulatorKar98Command[] = "god\ngive kar98k\n";
constexpr char kScopedZombieKar98Command[] =
    "ai_disableSpawn 1\n"
    "g_ai 0\n"
    "god\n"
    "notarget\n"
    "take weapons\n"
    "give kar98k_scoped_zombie\n"
    "give ammo\n";
// Keep the starting Nacht Colt equipped while making an unattended simulator
// reload transaction safe from zombie damage.
constexpr char kSimulatorColtProbeCommand[] = "god\n";
// The M1 test preset must be usable in a physical headset without forcing the
// tester to earn points while an interaction is still under development.  The
// stock ai_disableSpawn dvar prevents new actors, while g_ai freezes any actor
// created during the short map-start window. notarget and god provide two
// independent protections against that actor interrupting the test.
// A large native reserve refill remains finite: each magazine can still become
// genuinely empty, unlike player_sustainAmmo. The same refill is requested
// again only after a successful physical charging cycle.
constexpr char kSimulatorM1CarbineCommand[] =
    "ai_disableSpawn 1\n"
    "g_ai 0\n"
    "god\n"
    "notarget\n"
    // The starting Nacht Colt can remain selected after a plain `give`, which
    // makes an unattended rifle-pose probe test the pistol by mistake.  This
    // environment-gated simulator preset owns a disposable inventory, so use
    // the same bounded settle sequence as the proven Garand preset.
    "take weapons\n"
    "wait 30\n"
    "give m1carbine\n"
    "give ammo\n";
constexpr char kSimulatorM1GarandCommand[] =
    "ai_disableSpawn 1\n"
    "g_ai 0\n"
    "god\n"
    "notarget\n"
    "take weapons\n"
    "wait 30\n"
    "give m1garand\n"
    "give ammo\n";
constexpr char kSimulatorM1GarandBayonetCommand[] =
    "god\nnotarget\ntake weapons\nwait 30\n"
    "give m1garand_bayonet\n"
    "give ammo\n";
constexpr char kSimulatorM1GarandGrenadeLauncherCommand[] =
    // The rifle-grenade Garand is a script-owned alternate-weapon pair. The
    // environment-gated direct grant below installs that exact pair after
    // Hard Landing finishes loading; ordinary campaign inventory is untouched.
    "god\nnotarget\n";
constexpr char kHeadsetM1GarandGrenadeLauncherCommand[] =
    // Physical testing needs a deterministic grant because the late Hard
    // Landing start does not place the scripted pickup near the player. Keep
    // this destructive inventory replacement behind the explicit headset-only
    // test flag; ordinary campaign launches retain their authored loadout.
    // T4's stock `give` command resolves ordinary pickup items, but it does
    // not grant this script-owned alternate-weapon pair.  The environment-
    // gated test path below therefore installs the exact already-loaded pair
    // directly into the disposable playerState after the map settles. The
    // direct test grant commits through CG_SelectWeaponIndex in
    // the same frame, so no fragile map teleport or scripted pickup is needed.
    "god\nnotarget\n";
constexpr char kSimulatorGewehr43Command[] =
    "ai_disableSpawn 1\ng_ai 0\ngod\nnotarget\ngive gewehr43\ngive ammo\n";
constexpr char kSimulatorStg44Command[] =
    "ai_disableSpawn 1\ng_ai 0\ngod\nnotarget\ngive stg44\ngive ammo\n";
constexpr char kSimulatorMp40Command[] =
    "ai_disableSpawn 1\ng_ai 0\ngod\nnotarget\ngive mp40\ngive ammo\n";
constexpr char kSimulatorThompsonCommand[] =
    "ai_disableSpawn 1\ng_ai 0\ngod\nnotarget\ngive thompson\ngive ammo\n";
constexpr char kSimulatorBarCommand[] =
    "ai_disableSpawn 1\ng_ai 0\ngod\nnotarget\ngive bar\ngive ammo\n";
constexpr char kSimulatorFg42BipodCommand[] =
    "ai_disableSpawn 1\ng_ai 0\ngod\nnotarget\ngive fg42_bipod\ngive ammo\n";
constexpr char kSimulatorWaltherCommand[] =
    "ai_disableSpawn 1\ng_ai 0\ngod\nnotarget\ngive walther\ngive ammo\n";
constexpr char kSimulatorColtCommand[] =
    "ai_disableSpawn 1\ng_ai 0\ngod\nnotarget\ngive colt\ngive ammo\n";
constexpr char kSimulatorRussianMagazineMapCommand[] =
    "set start reich\nspdevmap ber3\n";
constexpr char kSimulatorPacificMagazineMapCommand[] =
    "set start event2\ndevmap mak\n";
// Makin's event1_lmg start is paired with an exact stock-zone manifest entry
// for sp/type99_lmg.  Inventory the packaged viewmodel there rather than
// relying on Pel1a's script-only magicbullet reference, which does not prove a
// player viewmodel asset is resident.
constexpr char kSimulatorPacificLmgMapCommand[] =
    "set start event1_lmg\nspdevmap mak\n";
constexpr char kSimulatorPeleliuInventoryMapCommand[] =
    "spdevmap pel1\n";
constexpr char kSimulatorHardLandingMapCommand[] =
    "spdevmap pel2\n";
constexpr char kHeadsetHardLandingGarandGlMapCommand[] =
    // Hard Landing registers the exact m1garand_gl/M7 pair at ordinary map
    // startup; beginning at the stock level entry is substantially more
    // reliable than jumping into a later script section.
    "spdevmap pel2\n";
constexpr char kSimulatorOkinawaWetInventoryMapCommand[] =
    "set start event2\nspdevmap oki2\n";
constexpr char kSimulatorZombieAsylumInventoryMapCommand[] =
    "devmap nazi_zombie_asylum\n";
constexpr char kSimulatorZombieFactoryInventoryMapCommand[] =
    "devmap nazi_zombie_factory\n";
constexpr char kScopedZombieKar98MapCommand[] =
    "devmap nazi_zombie_prototype\n";
// Cycle every conventional Der Riese magazine-fed identity through a live
// viewmodel. The aggregate DB pass proves immutable topology; this second,
// simulator-only route also exercises profile binding, detached asset build,
// and pre-skin charging-surface publication for each base/upgraded pair.
constexpr char kSimulatorZombieMagazineProfileSweepCommand[] =
    "ai_disableSpawn 1\ng_ai 0\ngod\nnotarget\n"
    "take weapons\ngive zombie_colt\nwait 90\n"
    "take weapons\ngive zombie_colt_upgraded\nwait 90\n"
    "take weapons\ngive zombie_m1carbine\nwait 90\n"
    "take weapons\ngive zombie_m1carbine_upgraded\nwait 90\n"
    "take weapons\ngive zombie_gewehr43\nwait 90\n"
    "take weapons\ngive zombie_gewehr43_upgraded\nwait 90\n"
    "take weapons\ngive zombie_stg44\nwait 90\n"
    "take weapons\ngive zombie_stg44_upgraded\nwait 90\n"
    "take weapons\ngive zombie_thompson\nwait 90\n"
    "take weapons\ngive zombie_thompson_upgraded\nwait 90\n"
    "take weapons\ngive zombie_mp40\nwait 90\n"
    "take weapons\ngive zombie_mp40_upgraded\nwait 90\n"
    "take weapons\ngive zombie_type100_smg\nwait 90\n"
    "take weapons\ngive zombie_type100_smg_upgraded\nwait 90\n"
    "take weapons\ngive zombie_bar\nwait 90\n"
    "take weapons\ngive zombie_bar_upgraded\nwait 90\n"
    "take weapons\ngive zombie_fg42\nwait 90\n"
    "take weapons\ngive zombie_fg42_upgraded\nwait 90\n"
    "take weapons\ngive zombie_ppsh\nwait 90\n"
    "take weapons\ngive zombie_ppsh_upgraded\ngive ammo\n";
constexpr char kHeadsetZombieM1CarbineUpgradedCommand[] =
    "ai_disableSpawn 1\n"
    "g_ai 0\n"
    "god\n"
    "notarget\n"
    "take weapons\n"
    "give zombie_m1carbine\n"
    "wait 90\n"
    "take weapons\n"
    "give zombie_m1carbine_upgraded\n"
    "give ammo\n";
// Campaign starts already own a primary weapon, so `give` may add the exact
// probe asset without selecting it. The simulator probe supplies a later
// controller weapon-cycle after the server has applied the grant. Do not
// disable campaign AI here: those dvars also stall scripted openings.
constexpr char kSimulatorTokarevCommand[] =
    "god\nnotarget\ngive tokarev\ngive ammo\n";
constexpr char kSimulatorNambuCommand[] =
    "god\nnotarget\ngive nambu\ngive ammo\n";
constexpr char kSimulatorSvt40Command[] =
    "god\nnotarget\ngive svt40\ngive ammo\n";
// The physical SVT trajectory preset first adds the exact asset. A separate
// identity-checked selector below cycles only while the rendered held weapon
// is not `svt40`; command timing alone cannot reliably outlast Ber3's scripted
// starting PPSh grant on every runtime.
constexpr char kHeadsetSvt40Command[] =
    "god\nnotarget\ngive svt40\ngive ammo\n";
constexpr char kSimulatorPpshCommand[] =
    "god\nnotarget\ngive ppsh\ngive ammo\n";
constexpr char kSimulatorType100Command[] =
    "god\nnotarget\ntake weapons\ngive type100_smg\ngive ammo\n";
constexpr char kSimulatorType99RifleCommand[] =
    "god\nnotarget\ntake weapons\ngive type99_rifle\ngive ammo\n";
constexpr char kSimulatorType99RifleBayonetCommand[] =
    "god\nnotarget\ntake weapons\ngive type99_rifle_bayonet\ngive ammo\n";
constexpr char kSimulatorType99RifleScopedCommand[] =
    "god\nnotarget\ntake weapons\ngive type99_rifle_scoped\ngive ammo\n";
constexpr char kSimulatorType99LmgCommand[] =
    "god\nnotarget\ngive type99_lmg\ngive ammo\n";
constexpr char kSimulatorSpringfieldCommand[] =
    "god\ngive springfield\n";
// Nacht does not precache the campaign Mosin. Move the disposable simulator
// process onto a Russian campaign map that authoritatively loads the base
// rifle; normal Meta/Virtual Desktop launches can never enable this path.
constexpr char kSimulatorMosinCommand[] = "devmap ber1\n";
constexpr char kSimulatorMosinScopedCommand[] =
    "god\nnotarget\ntake weapons\nwait 30\n"
    "give mosin_rifle_scoped\n"
    "give ammo\n";
constexpr char kSimulatorMosinScopedMapCommand[] =
    "set start event4\nspdevmap sniper\n";
constexpr std::uint64_t kSimulatorDirectEquipStabilityMilliseconds = 2000;
constexpr std::uint64_t kSimulatorMapHandoffTimeoutMilliseconds = 60000;
constexpr std::uint64_t kHeadsetSvt40SelectionSettleMilliseconds = 2000;
constexpr std::uint64_t kHeadsetSvt40CycleCooldownMilliseconds = 1500;
constexpr std::uint32_t kHeadsetSvt40MaximumCycleAttempts = 8;
constexpr std::uint64_t kHeadsetM1GarandGlSelectionSettleMilliseconds = 5000;
constexpr std::uint64_t kHeadsetM1GarandGlCycleCooldownMilliseconds = 1000;
constexpr std::uint32_t kHeadsetM1GarandGlMaximumCycleAttempts = 1;
constexpr std::size_t kHeadsetTestLocalPlayerClientPointerOffset = 0x180;
constexpr std::size_t kHeadsetTestPlayerStateMinimumSpan = 0x920;
constexpr std::size_t kHeadsetTestPlayerStateAmmoOffset = 0x17C;
constexpr std::size_t kHeadsetTestPlayerStateClipAmmoOffset = 0x5FC;
constexpr std::size_t kHeadsetTestWeaponBitsetWords = 4;
constexpr std::size_t kHeadsetTestWeaponDefinitionNameOffset = 0x00;
constexpr std::size_t kHeadsetTestWeaponDefinitionAmmoIndexOffset = 0x3F4;
constexpr std::size_t kHeadsetTestWeaponDefinitionClipIndexOffset = 0x3FC;
constexpr std::size_t kHeadsetTestWeaponDefinitionClipSizeOffset = 0x408;
constexpr std::size_t kHeadsetTestMaximumAmmoPools = 128;
constexpr std::size_t kHeadsetTestWeaponTableSpan = 128 * sizeof(std::uint32_t);
constexpr wawvr::t4::Rva kHeadsetTestLastWeaponIndexRva = 0x042DE3BC;
// CG_SelectWeaponIndex's internal selection commit. The exact SP 1.7 routine
// takes the weapon index in EDX and localClientNum as its single stack
// argument. Calling it in the same post-Com_Frame service pass as the
// environment-gated inventory grant avoids losing the selection edge to the
// next predicted playerState snapshot.
constexpr wawvr::t4::Rva kHeadsetTestSelectWeaponIndexRva = 0x0006BDB0;
// G_GivePlayerWeapon (preferred VA 0x005528D0): EAX=weapon index,
// ECX=playerState_s*, one stack argument=camo index. Unlike a raw weapon-bit
// write, this also establishes the weapon slot and walks the alternate-weapon
// chain, which the m1garand_gl/M7 pair requires.
constexpr wawvr::t4::Rva kHeadsetTestGivePlayerWeaponRva = 0x001528D0;
constexpr wchar_t kPezBotAutofillLaunchMarker[] =
    L"+set wawvr_pezbot_autofill 9";
constexpr wchar_t kSimulatorKar98Environment[] =
    L"WAWVR_SIMULATOR_EQUIP_KAR98";
constexpr wchar_t kSimulatorColtProbeEnvironment[] =
    L"WAWVR_SIMULATOR_PROBE_COLT";
constexpr wchar_t kSimulatorM1CarbineEnvironment[] =
    L"WAWVR_SIMULATOR_EQUIP_M1CARBINE";
constexpr wchar_t kSimulatorMagazineWeaponEnvironment[] =
    L"WAWVR_SIMULATOR_EQUIP_MAGAZINE_WEAPON";
constexpr wchar_t kHeadsetM1CarbineTestEnvironment[] =
    L"WAWVR_HEADSET_TEST_EQUIP_M1CARBINE";
constexpr wchar_t kHeadsetZombieM1CarbineUpgradedTestEnvironment[] =
    L"WAWVR_HEADSET_TEST_EQUIP_UPGRADED_M1CARBINE";
constexpr wchar_t kHeadsetZombieMagazineWeaponTestEnvironment[] =
    L"WAWVR_HEADSET_TEST_EQUIP_ZOMBIE_MAGAZINE_WEAPON";
constexpr wchar_t kHeadsetScopedZombieKar98TestEnvironment[] =
    L"WAWVR_HEADSET_TEST_EQUIP_SCOPED_KAR98";
constexpr wchar_t kHeadsetScopedMosinTestEnvironment[] =
    L"WAWVR_HEADSET_TEST_EQUIP_SCOPED_MOSIN";
constexpr wchar_t kHeadsetSvt40TestEnvironment[] =
    L"WAWVR_HEADSET_TEST_EQUIP_SVT40";
constexpr wchar_t kHeadsetPpshTestEnvironment[] =
    L"WAWVR_HEADSET_TEST_EQUIP_PPSH";
constexpr wchar_t kHeadsetM1GarandGrenadeLauncherTestEnvironment[] =
    L"WAWVR_HEADSET_TEST_EQUIP_M1GARAND_GL";
constexpr wchar_t kSimulatorSpringfieldEnvironment[] =
    L"WAWVR_SIMULATOR_EQUIP_SPRINGFIELD";
constexpr wchar_t kSimulatorMosinEnvironment[] =
    L"WAWVR_SIMULATOR_EQUIP_MOSIN";

[[nodiscard]] bool environment_flag_enabled(
    const wchar_t* const name) noexcept {
    wchar_t value[8]{};
    const DWORD length = GetEnvironmentVariableW(
        name, value, static_cast<DWORD>(std::size(value)));
    return length == 1 && value[0] == L'1';
}

[[nodiscard]] bool environment_value_contains(
    const wchar_t* const name,
    const wchar_t* const fragment) noexcept {
    wchar_t value[1024]{};
    const DWORD length = GetEnvironmentVariableW(
        name, value, static_cast<DWORD>(std::size(value)));
    return length != 0 && length < std::size(value) &&
        std::wcsstr(value, fragment) != nullptr;
}

struct HeadsetZombieMagazinePreset final {
    const wchar_t* selector;
    const char* base_weapon_name;
    const char* selected_weapon_name;
};

constexpr HeadsetZombieMagazinePreset kHeadsetZombieMagazinePresets[] = {
    {L"m1garand", "m1garand", "m1garand"},
    {L"zombie_colt", "zombie_colt", "zombie_colt"},
    {L"zombie_colt_upgraded", "zombie_colt",
     "zombie_colt_upgraded"},
    {L"zombie_m1carbine", "zombie_m1carbine", "zombie_m1carbine"},
    {L"zombie_m1carbine_upgraded", "zombie_m1carbine",
     "zombie_m1carbine_upgraded"},
    {L"zombie_gewehr43", "zombie_gewehr43", "zombie_gewehr43"},
    {L"zombie_gewehr43_upgraded", "zombie_gewehr43",
     "zombie_gewehr43_upgraded"},
    {L"zombie_stg44", "zombie_stg44", "zombie_stg44"},
    {L"zombie_stg44_upgraded", "zombie_stg44",
     "zombie_stg44_upgraded"},
    {L"zombie_thompson", "zombie_thompson", "zombie_thompson"},
    {L"zombie_thompson_upgraded", "zombie_thompson",
     "zombie_thompson_upgraded"},
    {L"zombie_mp40", "zombie_mp40", "zombie_mp40"},
    {L"zombie_mp40_upgraded", "zombie_mp40", "zombie_mp40_upgraded"},
    {L"zombie_type100_smg", "zombie_type100_smg", "zombie_type100_smg"},
    {L"zombie_type100_smg_upgraded", "zombie_type100_smg",
     "zombie_type100_smg_upgraded"},
    {L"zombie_bar", "zombie_bar", "zombie_bar"},
    {L"zombie_bar_upgraded", "zombie_bar", "zombie_bar_upgraded"},
    {L"zombie_fg42", "zombie_fg42", "zombie_fg42"},
    {L"zombie_fg42_upgraded", "zombie_fg42", "zombie_fg42_upgraded"},
    {L"zombie_ppsh", "zombie_ppsh", "zombie_ppsh"},
    {L"zombie_ppsh_upgraded", "zombie_ppsh", "zombie_ppsh_upgraded"},
};

[[nodiscard]] bool append_headset_zombie_magazine_command(
    const char* const fragment,
    std::size_t* const used) noexcept {
    if (fragment == nullptr || used == nullptr) {
        return false;
    }
    const std::size_t fragment_length = std::strlen(fragment);
    if (*used >= g_headset_zombie_magazine_command.size() ||
        fragment_length >=
            g_headset_zombie_magazine_command.size() - *used) {
        return false;
    }
    std::memcpy(
        g_headset_zombie_magazine_command.data() + *used,
        fragment, fragment_length);
    *used += fragment_length;
    g_headset_zombie_magazine_command[*used] = '\0';
    return true;
}

[[nodiscard]] bool configure_headset_zombie_magazine_command() noexcept {
    g_headset_zombie_magazine_command.fill('\0');
    g_headset_zombie_magazine_uses_nacht_m1garand = false;
    wchar_t value[64]{};
    const DWORD length = GetEnvironmentVariableW(
        kHeadsetZombieMagazineWeaponTestEnvironment, value,
        static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value)) {
        return false;
    }

    const HeadsetZombieMagazinePreset* selected = nullptr;
    for (const HeadsetZombieMagazinePreset& preset :
         kHeadsetZombieMagazinePresets) {
        if (std::wcscmp(value, preset.selector) == 0) {
            selected = &preset;
            break;
        }
    }
    if (selected == nullptr) {
        return false;
    }

    std::size_t used = 0;
    const bool upgraded =
        std::strcmp(
            selected->base_weapon_name,
            selected->selected_weapon_name) != 0;
    // T4 can leave the player weaponless when the Garand is given in the same
    // command-buffer frame as `take weapons`.  The dedicated simulator preset
    // already observes this settle interval; keep the physical-headset preset
    // on the same proven sequence so there is an actual viewmodel to grip.
    const bool requires_take_settle =
        std::strcmp(selected->base_weapon_name, "m1garand") == 0;
    bool valid =
        append_headset_zombie_magazine_command(
            requires_take_settle
                ? "ai_disableSpawn 1\ng_ai 0\ngod\nnotarget\ntake weapons\nwait 30\ngive "
                : "ai_disableSpawn 1\ng_ai 0\ngod\nnotarget\ntake weapons\ngive ",
            &used) &&
        append_headset_zombie_magazine_command(
            selected->base_weapon_name, &used) &&
        append_headset_zombie_magazine_command("\n", &used);
    if (valid && upgraded) {
        valid =
            append_headset_zombie_magazine_command(
                "wait 90\ntake weapons\ngive ", &used) &&
            append_headset_zombie_magazine_command(
                selected->selected_weapon_name, &used) &&
            append_headset_zombie_magazine_command("\n", &used);
    }
    valid = valid &&
        append_headset_zombie_magazine_command("give ammo\n", &used);
    if (!valid) {
        g_headset_zombie_magazine_command.fill('\0');
    } else {
        // The exact Der Riese headset handoff did not produce a registered
        // current Garand weapon, while the dedicated Nacht preset proves
        // `give m1garand`.  Keep only this selector on the starting Nacht map
        // so `take weapons` cannot strand the physical tester without a
        // weapon or hands.
        g_headset_zombie_magazine_uses_nacht_m1garand =
            requires_take_settle;
    }
    return valid;
}

[[nodiscard]] SimulatorBoltActionWeapon
simulator_magazine_weapon_from_environment() noexcept {
    if (!environment_value_contains(
            L"XR_RUNTIME_JSON", L"openxr_simulator")) {
        return SimulatorBoltActionWeapon::None;
    }
    wchar_t value[32]{};
    const DWORD length = GetEnvironmentVariableW(
        kSimulatorMagazineWeaponEnvironment, value,
        static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value)) {
        return SimulatorBoltActionWeapon::None;
    }
    struct Entry final {
        const wchar_t* name;
        SimulatorBoltActionWeapon weapon;
    };
    constexpr Entry entries[] = {
        {L"m1carbine", SimulatorBoltActionWeapon::M1Carbine},
        {L"m1garand", SimulatorBoltActionWeapon::M1Garand},
        {L"m1garand_bayonet", SimulatorBoltActionWeapon::M1GarandBayonet},
        {L"m1garand_gl",
         SimulatorBoltActionWeapon::M1GarandGrenadeLauncher},
        {L"mosin_rifle_scoped", SimulatorBoltActionWeapon::MosinScoped},
        {L"gewehr43", SimulatorBoltActionWeapon::Gewehr43},
        {L"stg44", SimulatorBoltActionWeapon::Stg44},
        {L"mp40", SimulatorBoltActionWeapon::Mp40},
        {L"thompson", SimulatorBoltActionWeapon::Thompson},
        {L"bar", SimulatorBoltActionWeapon::Bar},
        {L"fg42_bipod", SimulatorBoltActionWeapon::Fg42Bipod},
        {L"walther", SimulatorBoltActionWeapon::Walther},
        {L"colt", SimulatorBoltActionWeapon::Colt},
        {L"zombie_colt", SimulatorBoltActionWeapon::ColtProbe},
        {L"tokarev", SimulatorBoltActionWeapon::Tokarev},
        {L"nambu", SimulatorBoltActionWeapon::Nambu},
        {L"svt40", SimulatorBoltActionWeapon::Svt40},
        {L"ppsh", SimulatorBoltActionWeapon::Ppsh},
        {L"type100_smg", SimulatorBoltActionWeapon::Type100},
        {L"type99_rifle", SimulatorBoltActionWeapon::Type99Rifle},
        {L"type99_rifle_bayonet",
         SimulatorBoltActionWeapon::Type99RifleBayonet},
        {L"type99_rifle_scoped",
         SimulatorBoltActionWeapon::Type99RifleScoped},
        {L"kar98k_scoped_zombie",
         SimulatorBoltActionWeapon::Kar98ScopedZombie},
        {L"type99_lmg", SimulatorBoltActionWeapon::Type99Lmg},
        {L"dp28", SimulatorBoltActionWeapon::Dp28},
        {L"fg42", SimulatorBoltActionWeapon::Fg42},
        {L"bar_bipod", SimulatorBoltActionWeapon::BarBipodAsset},
        {L"type99_lmg_bipod", SimulatorBoltActionWeapon::Type99LmgBipod},
        {L"type100_smg_nosound", SimulatorBoltActionWeapon::Type100NoSound},
        {L"thompson_wet", SimulatorBoltActionWeapon::ThompsonWet},
        {L"colt_wet", SimulatorBoltActionWeapon::ColtWet},
        {L"zombie_magazine_inventory",
         SimulatorBoltActionWeapon::ZombieMagazineInventory},
        {L"zombie_magazine_profile_sweep",
         SimulatorBoltActionWeapon::ZombieMagazineProfileSweep},
    };
    for (const Entry& entry : entries) {
        if (std::wcscmp(value, entry.name) == 0) {
            return entry.weapon;
        }
    }
    return SimulatorBoltActionWeapon::None;
}

[[nodiscard]] bool simulator_weapon_requires_map_handoff(
    const SimulatorBoltActionWeapon weapon) noexcept {
    switch (weapon) {
        case SimulatorBoltActionWeapon::Tokarev:
        case SimulatorBoltActionWeapon::Nambu:
        case SimulatorBoltActionWeapon::Svt40:
        case SimulatorBoltActionWeapon::Ppsh:
        case SimulatorBoltActionWeapon::Type100:
        case SimulatorBoltActionWeapon::Type99Rifle:
        case SimulatorBoltActionWeapon::Type99RifleBayonet:
        case SimulatorBoltActionWeapon::Type99RifleScoped:
        case SimulatorBoltActionWeapon::Type99Lmg:
        case SimulatorBoltActionWeapon::Dp28:
        case SimulatorBoltActionWeapon::Fg42:
        case SimulatorBoltActionWeapon::BarBipodAsset:
        case SimulatorBoltActionWeapon::M1GarandBayonet:
        case SimulatorBoltActionWeapon::M1GarandGrenadeLauncher:
        case SimulatorBoltActionWeapon::MosinScoped:
        case SimulatorBoltActionWeapon::Type99LmgBipod:
        case SimulatorBoltActionWeapon::Type100NoSound:
        case SimulatorBoltActionWeapon::ThompsonWet:
        case SimulatorBoltActionWeapon::ColtWet:
        case SimulatorBoltActionWeapon::ZombieMagazineInventory:
        case SimulatorBoltActionWeapon::ZombieMagazineProfileSweep:
        case SimulatorBoltActionWeapon::Kar98ScopedZombie:
        case SimulatorBoltActionWeapon::HeadsetZombieM1CarbineUpgraded:
        case SimulatorBoltActionWeapon::HeadsetZombieMagazineWeapon:
        case SimulatorBoltActionWeapon::HeadsetSvt40:
        case SimulatorBoltActionWeapon::HeadsetM1GarandGrenadeLauncher:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] const char* map_command_for_simulator_weapon(
    const SimulatorBoltActionWeapon weapon) noexcept {
    switch (weapon) {
        case SimulatorBoltActionWeapon::Nambu:
        case SimulatorBoltActionWeapon::Type100:
        case SimulatorBoltActionWeapon::Type99Rifle:
        case SimulatorBoltActionWeapon::Type99RifleBayonet:
        case SimulatorBoltActionWeapon::Type99RifleScoped:
            return kSimulatorPacificMagazineMapCommand;
        case SimulatorBoltActionWeapon::Type99Lmg:
            return kSimulatorPacificLmgMapCommand;
        case SimulatorBoltActionWeapon::Type99LmgBipod:
        case SimulatorBoltActionWeapon::Type100NoSound:
            return kSimulatorPeleliuInventoryMapCommand;
        case SimulatorBoltActionWeapon::ThompsonWet:
        case SimulatorBoltActionWeapon::ColtWet:
            return kSimulatorOkinawaWetInventoryMapCommand;
        case SimulatorBoltActionWeapon::BarBipodAsset:
            return kSimulatorZombieAsylumInventoryMapCommand;
        case SimulatorBoltActionWeapon::M1GarandBayonet:
            return kSimulatorPeleliuInventoryMapCommand;
        case SimulatorBoltActionWeapon::M1GarandGrenadeLauncher:
            return kSimulatorHardLandingMapCommand;
        case SimulatorBoltActionWeapon::HeadsetM1GarandGrenadeLauncher:
            return kHeadsetHardLandingGarandGlMapCommand;
        case SimulatorBoltActionWeapon::MosinScoped:
            return kSimulatorMosinScopedMapCommand;
        case SimulatorBoltActionWeapon::ZombieMagazineInventory:
        case SimulatorBoltActionWeapon::ZombieMagazineProfileSweep:
        case SimulatorBoltActionWeapon::HeadsetZombieM1CarbineUpgraded:
        case SimulatorBoltActionWeapon::HeadsetZombieMagazineWeapon:
            return kSimulatorZombieFactoryInventoryMapCommand;
        case SimulatorBoltActionWeapon::Kar98ScopedZombie:
            return kScopedZombieKar98MapCommand;
        case SimulatorBoltActionWeapon::Tokarev:
        case SimulatorBoltActionWeapon::Ppsh:
        case SimulatorBoltActionWeapon::Svt40:
        case SimulatorBoltActionWeapon::HeadsetSvt40:
        case SimulatorBoltActionWeapon::Dp28:
        case SimulatorBoltActionWeapon::Fg42:
            return kSimulatorRussianMagazineMapCommand;
        default:
            return nullptr;
    }
}

[[nodiscard]] const char* command_for_simulator_weapon(
    const SimulatorBoltActionWeapon weapon,
    const SimulatorWeaponEquipPhase phase) noexcept {
    if (phase == SimulatorWeaponEquipPhase::NeedMap) {
        return map_command_for_simulator_weapon(weapon);
    }
    if (phase == SimulatorWeaponEquipPhase::Queueing ||
        phase == SimulatorWeaponEquipPhase::WaitingForMapTransition ||
        phase == SimulatorWeaponEquipPhase::Complete) {
        return nullptr;
    }
    switch (weapon) {
        case SimulatorBoltActionWeapon::ColtProbe:
            return kSimulatorColtProbeCommand;
        case SimulatorBoltActionWeapon::M1Carbine:
            return kSimulatorM1CarbineCommand;
        case SimulatorBoltActionWeapon::M1Garand:
            return kSimulatorM1GarandCommand;
        case SimulatorBoltActionWeapon::M1GarandBayonet:
            return kSimulatorM1GarandBayonetCommand;
        case SimulatorBoltActionWeapon::M1GarandGrenadeLauncher:
            return kSimulatorM1GarandGrenadeLauncherCommand;
        case SimulatorBoltActionWeapon::HeadsetM1GarandGrenadeLauncher:
            return kHeadsetM1GarandGrenadeLauncherCommand;
        case SimulatorBoltActionWeapon::Kar98:
            return kSimulatorKar98Command;
        case SimulatorBoltActionWeapon::Kar98ScopedZombie:
            return kScopedZombieKar98Command;
        case SimulatorBoltActionWeapon::Springfield:
            return kSimulatorSpringfieldCommand;
        case SimulatorBoltActionWeapon::Mosin:
            return kSimulatorMosinCommand;
        case SimulatorBoltActionWeapon::MosinScoped:
            return kSimulatorMosinScopedCommand;
        case SimulatorBoltActionWeapon::Gewehr43:
            return kSimulatorGewehr43Command;
        case SimulatorBoltActionWeapon::Stg44:
            return kSimulatorStg44Command;
        case SimulatorBoltActionWeapon::Mp40:
            return kSimulatorMp40Command;
        case SimulatorBoltActionWeapon::Thompson:
            return kSimulatorThompsonCommand;
        case SimulatorBoltActionWeapon::Bar:
            return kSimulatorBarCommand;
        case SimulatorBoltActionWeapon::Fg42Bipod:
            return kSimulatorFg42BipodCommand;
        case SimulatorBoltActionWeapon::Walther:
            return kSimulatorWaltherCommand;
        case SimulatorBoltActionWeapon::Colt:
            return kSimulatorColtCommand;
        case SimulatorBoltActionWeapon::Tokarev:
            return kSimulatorTokarevCommand;
        case SimulatorBoltActionWeapon::Nambu:
            return kSimulatorNambuCommand;
        case SimulatorBoltActionWeapon::Svt40:
            return kSimulatorSvt40Command;
        case SimulatorBoltActionWeapon::HeadsetSvt40:
            return kHeadsetSvt40Command;
        case SimulatorBoltActionWeapon::Ppsh:
            return kSimulatorPpshCommand;
        case SimulatorBoltActionWeapon::Type100:
            return kSimulatorType100Command;
        case SimulatorBoltActionWeapon::Type99Rifle:
            return kSimulatorType99RifleCommand;
        case SimulatorBoltActionWeapon::Type99RifleBayonet:
            return kSimulatorType99RifleBayonetCommand;
        case SimulatorBoltActionWeapon::Type99RifleScoped:
            return kSimulatorType99RifleScopedCommand;
        case SimulatorBoltActionWeapon::Type99Lmg:
            return kSimulatorType99LmgCommand;
        case SimulatorBoltActionWeapon::Dp28:
        case SimulatorBoltActionWeapon::Fg42:
        case SimulatorBoltActionWeapon::BarBipodAsset:
        case SimulatorBoltActionWeapon::Type99LmgBipod:
        case SimulatorBoltActionWeapon::Type100NoSound:
        case SimulatorBoltActionWeapon::ThompsonWet:
        case SimulatorBoltActionWeapon::ColtWet:
        case SimulatorBoltActionWeapon::ZombieMagazineInventory:
            // These assets are inventoried directly from the post-handoff
            // campaign database. Do not perturb the scripted loadout merely
            // to make their viewmodels active.
            return nullptr;
        case SimulatorBoltActionWeapon::ZombieMagazineProfileSweep:
            return kSimulatorZombieMagazineProfileSweepCommand;
        case SimulatorBoltActionWeapon::HeadsetZombieM1CarbineUpgraded:
            return kHeadsetZombieM1CarbineUpgradedCommand;
        case SimulatorBoltActionWeapon::HeadsetZombieMagazineWeapon:
            return g_headset_zombie_magazine_command[0] != '\0'
                ? g_headset_zombie_magazine_command.data()
                : nullptr;
        case SimulatorBoltActionWeapon::None:
        default:
            return nullptr;
    }
}

void append_simulator_kar98_probe(const char* const line) noexcept {
    wchar_t local_app_data[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", local_app_data,
        static_cast<DWORD>(std::size(local_app_data)));
    if (line == nullptr || length == 0 ||
        length >= std::size(local_app_data)) {
        return;
    }
    std::wstring path(local_app_data, length);
    path += L"\\OpenXR-Simulator\\wawvr_kar98_probe.log";
    const HANDLE file = CreateFileW(
        path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(
        file, line, static_cast<DWORD>(std::strlen(line)), &written,
        nullptr);
    constexpr char newline[] = "\r\n";
    WriteFile(file, newline, 2, &written, nullptr);
    CloseHandle(file);
}

[[nodiscard]] bool executable_address(
    const std::uintptr_t address) noexcept {
    if (address == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(
            reinterpret_cast<const void*>(address), &memory,
            sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    const DWORD access = memory.Protect & 0xffU;
    return access == PAGE_EXECUTE || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool protection_is_readable(const DWORD protection) noexcept {
    const DWORD access = protection & 0xffU;
    return access == PAGE_READONLY || access == PAGE_READWRITE ||
           access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool readable_range(
    const void* const address, const std::size_t size) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
        !protection_is_readable(memory.Protect)) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto region_begin =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto region_end = region_begin + memory.RegionSize;
    return begin >= region_begin && begin <= region_end &&
           size <= region_end - begin;
}

[[nodiscard]] bool writable_range(
    void* const address, const std::size_t size) noexcept {
    if (address == nullptr || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    const DWORD access = memory.Protect & 0xffU;
    if (access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
        access != PAGE_EXECUTE_READWRITE &&
        access != PAGE_EXECUTE_WRITECOPY) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto region_begin =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto region_end = region_begin + memory.RegionSize;
    return begin >= region_begin && begin <= region_end &&
           size <= region_end - begin;
}

[[nodiscard]] bool native_string_equals(
    const std::uintptr_t address, const char* const expected) noexcept {
    if (address == 0 || expected == nullptr) {
        return false;
    }
    constexpr std::size_t kMaximumWeaponNameLength = 96;
    for (std::size_t index = 0; index < kMaximumWeaponNameLength; ++index) {
        const auto* const character =
            reinterpret_cast<const char*>(address + index);
        if (!readable_range(character, sizeof(*character))) {
            return false;
        }
        char actual = '\0';
        std::memcpy(&actual, character, sizeof(actual));
        if (actual != expected[index]) {
            return false;
        }
        if (actual == '\0') {
            return true;
        }
    }
    return false;
}

enum class HeadsetM1GarandGlDirectEquipResult : std::uint8_t {
    unavailable,
    target_not_loaded,
    applied,
};

struct LoadedM1GarandGlPair final {
    std::uint32_t rifle_index{};
    std::uint32_t launcher_index{};
    std::uintptr_t rifle_definition{};
    std::uintptr_t launcher_definition{};

    [[nodiscard]] bool complete() const noexcept {
        return rifle_index != 0 && launcher_index != 0 &&
               rifle_definition != 0 && launcher_definition != 0;
    }
};

[[nodiscard]] LoadedM1GarandGlPair
find_loaded_m1garand_gl_pair() noexcept {
    LoadedM1GarandGlPair pair{};
    const std::uintptr_t weapon_table =
        g_headset_test_weapon_definition_table.load(
            std::memory_order_acquire);
    const std::uintptr_t last_weapon_index_address =
        g_headset_test_last_weapon_index.load(std::memory_order_acquire);
    if (weapon_table == 0 || last_weapon_index_address == 0 ||
        !readable_range(
            reinterpret_cast<const void*>(last_weapon_index_address),
            sizeof(std::uint32_t))) {
        return pair;
    }

    std::uint32_t last_weapon_index = 0;
    std::memcpy(
        &last_weapon_index,
        reinterpret_cast<const void*>(last_weapon_index_address),
        sizeof(last_weapon_index));
    if (last_weapon_index < 1 ||
        last_weapon_index >= kHeadsetTestWeaponBitsetWords * 32U) {
        return pair;
    }

    for (std::uint32_t weapon_index = 1;
         weapon_index <= last_weapon_index; ++weapon_index) {
        const std::uintptr_t slot =
            weapon_table + static_cast<std::uintptr_t>(weapon_index) *
                sizeof(std::uint32_t);
        if (!readable_range(
                reinterpret_cast<const void*>(slot),
                sizeof(std::uint32_t))) {
            return {};
        }
        std::uint32_t definition32 = 0;
        std::memcpy(
            &definition32, reinterpret_cast<const void*>(slot),
            sizeof(definition32));
        const std::uintptr_t definition =
            static_cast<std::uintptr_t>(definition32);
        if (definition == 0 ||
            !readable_range(
                reinterpret_cast<const void*>(definition),
                kHeadsetTestWeaponDefinitionClipSizeOffset +
                    sizeof(std::int32_t))) {
            continue;
        }
        std::uint32_t name32 = 0;
        std::memcpy(
            &name32,
            reinterpret_cast<const void*>(
                definition + kHeadsetTestWeaponDefinitionNameOffset),
            sizeof(name32));
        const std::uintptr_t name = static_cast<std::uintptr_t>(name32);
        if (native_string_equals(name, "m1garand_gl")) {
            pair.rifle_index = weapon_index;
            pair.rifle_definition = definition;
        } else if (native_string_equals(name, "m7_launcher")) {
            pair.launcher_index = weapon_index;
            pair.launcher_definition = definition;
        }
    }
    return pair;
}

[[nodiscard]] bool give_headset_test_weapon(
    const std::uintptr_t player_state,
    const std::uint32_t weapon_index) noexcept {
    const std::uintptr_t target =
        g_headset_test_give_player_weapon.load(std::memory_order_acquire);
    if (target == 0 || player_state == 0 || weapon_index == 0 ||
        !readable_range(reinterpret_cast<const void*>(target), 0x160)) {
        return false;
    }

#if defined(_M_IX86)
    std::uint32_t granted = 0;
    __asm {
        push 0
        mov eax, weapon_index
        mov ecx, player_state
        mov edx, target
        call edx
        add esp, 4
        mov granted, eax
    }
    return granted != 0;
#else
    static_cast<void>(player_state);
    static_cast<void>(weapon_index);
    return false;
#endif
}

[[nodiscard]] HeadsetM1GarandGlDirectEquipResult
directly_equip_headset_m1garand_gl() noexcept {
    const std::uintptr_t local_player_entity =
        g_headset_test_local_player_entity.load(std::memory_order_acquire);
    if (local_player_entity == 0 ||
        !readable_range(
            reinterpret_cast<const void*>(
                local_player_entity +
                kHeadsetTestLocalPlayerClientPointerOffset),
            sizeof(std::uint32_t))) {
        return HeadsetM1GarandGlDirectEquipResult::unavailable;
    }

    std::uint32_t player_state32 = 0;
    std::memcpy(
        &player_state32,
        reinterpret_cast<const void*>(
            local_player_entity +
            kHeadsetTestLocalPlayerClientPointerOffset),
        sizeof(player_state32));
    const std::uintptr_t player_state =
        static_cast<std::uintptr_t>(player_state32);
    if (player_state == 0 ||
        !writable_range(
            reinterpret_cast<void*>(player_state),
            kHeadsetTestPlayerStateMinimumSpan)) {
        return HeadsetM1GarandGlDirectEquipResult::unavailable;
    }

    const LoadedM1GarandGlPair pair = find_loaded_m1garand_gl_pair();
    if (!pair.complete()) {
        return HeadsetM1GarandGlDirectEquipResult::target_not_loaded;
    }

    if (!give_headset_test_weapon(player_state, pair.rifle_index)) {
        return HeadsetM1GarandGlDirectEquipResult::unavailable;
    }

    // G_GivePlayerWeapon establishes the slot, camo byte, alternate chain,
    // and ownership bits while intentionally leaving the authored campaign
    // weapon selected.  Preserve that old selection so CG_SelectWeaponIndex
    // can publish a real old-to-new transition and rebuild the held DObj.

    const auto refill_weapon = [player_state](
        const std::uintptr_t definition) noexcept {
        std::int32_t ammo_index = -1;
        std::int32_t clip_index = -1;
        std::int32_t clip_size = 0;
        std::memcpy(
            &ammo_index,
            reinterpret_cast<const void*>(
                definition + kHeadsetTestWeaponDefinitionAmmoIndexOffset),
            sizeof(ammo_index));
        std::memcpy(
            &clip_index,
            reinterpret_cast<const void*>(
                definition + kHeadsetTestWeaponDefinitionClipIndexOffset),
            sizeof(clip_index));
        std::memcpy(
            &clip_size,
            reinterpret_cast<const void*>(
                definition + kHeadsetTestWeaponDefinitionClipSizeOffset),
            sizeof(clip_size));
        if (ammo_index < 0 || clip_index < 0 ||
            ammo_index >=
                static_cast<std::int32_t>(kHeadsetTestMaximumAmmoPools) ||
            clip_index >=
                static_cast<std::int32_t>(kHeadsetTestMaximumAmmoPools) ||
            clip_size <= 0 || clip_size > 64) {
            return;
        }
        const std::int32_t reserve_ammo = clip_size * 8;
        std::memcpy(
            reinterpret_cast<void*>(
                player_state + kHeadsetTestPlayerStateAmmoOffset +
                static_cast<std::size_t>(ammo_index) *
                    sizeof(std::int32_t)),
            &reserve_ammo, sizeof(reserve_ammo));
        std::memcpy(
            reinterpret_cast<void*>(
                player_state + kHeadsetTestPlayerStateClipAmmoOffset +
                static_cast<std::size_t>(clip_index) *
                    sizeof(std::int32_t)),
            &clip_size, sizeof(clip_size));
    };
    refill_weapon(pair.rifle_definition);
    refill_weapon(pair.launcher_definition);
    g_headset_m1garand_gl_rifle_index.store(
        pair.rifle_index, std::memory_order_release);
    return HeadsetM1GarandGlDirectEquipResult::applied;
}

[[nodiscard]] bool select_headset_test_weapon_index(
    const std::uint32_t weapon_index) noexcept {
    const std::uintptr_t target =
        g_headset_test_select_weapon_index.load(std::memory_order_acquire);
    if (target == 0 || weapon_index == 0 ||
        !readable_range(reinterpret_cast<const void*>(target), 0x71)) {
        return false;
    }

#if defined(_M_IX86)
    __asm {
        push 0
        mov edx, weapon_index
        mov eax, target
        call eax
        add esp, 4
    }
    return true;
#else
    static_cast<void>(weapon_index);
    return false;
#endif
}

enum class M1GarandGlToggleResult : std::uint8_t {
    not_applicable,
    unavailable,
    rifle_to_launcher,
    launcher_to_rifle,
};

[[nodiscard]] M1GarandGlToggleResult
toggle_m1garand_gl_alternate() noexcept {
    const HeadsetM1GarandGlSelectionState selection =
        headset_m1garand_gl_selection_state();
    if (selection !=
            HeadsetM1GarandGlSelectionState::m1garand_gl_selected &&
        selection !=
            HeadsetM1GarandGlSelectionState::m7_launcher_selected) {
        return M1GarandGlToggleResult::not_applicable;
    }

    const LoadedM1GarandGlPair pair = find_loaded_m1garand_gl_pair();
    if (!pair.complete()) {
        return M1GarandGlToggleResult::unavailable;
    }
    if (selection ==
        HeadsetM1GarandGlSelectionState::m1garand_gl_selected) {
        return select_headset_test_weapon_index(pair.launcher_index)
            ? M1GarandGlToggleResult::rifle_to_launcher
            : M1GarandGlToggleResult::unavailable;
    }
    return select_headset_test_weapon_index(pair.rifle_index)
        ? M1GarandGlToggleResult::launcher_to_rifle
        : M1GarandGlToggleResult::unavailable;
}

[[nodiscard]] bool bytes_match(
    const void* const address,
    const std::span<const std::uint8_t> expected) noexcept {
    return readable_range(address, expected.size()) &&
           std::memcmp(address, expected.data(), expected.size()) == 0;
}

[[nodiscard]] bool make_relative_call(
    const std::uintptr_t source, const std::uintptr_t destination,
    std::array<std::uint8_t, kNativeMouseGateCallSize>* const output) noexcept {
    if (output == nullptr) {
        return false;
    }
    const std::int64_t displacement =
        static_cast<std::int64_t>(destination) -
        static_cast<std::int64_t>(source + kNativeMouseGateCallSize);
    if (displacement < std::numeric_limits<std::int32_t>::min() ||
        displacement > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    (*output)[0] = 0xE8;
    const auto encoded = static_cast<std::int32_t>(displacement);
    std::memcpy(output->data() + 1, &encoded, sizeof(encoded));
    return true;
}

[[nodiscard]] std::uintptr_t decode_relative_call_target(
    const std::uintptr_t source,
    const std::array<std::uint8_t, kNativeMouseGateCallSize>& bytes) noexcept {
    std::int32_t displacement = 0;
    std::memcpy(&displacement, bytes.data() + 1, sizeof(displacement));
    return static_cast<std::uintptr_t>(
        static_cast<std::int64_t>(source + kNativeMouseGateCallSize) +
        displacement);
}

enum class NativeMouseGatePatchResult : std::uint8_t {
    ok,
    thread_suspend_failed,
    expected_bytes_changed,
    target_protection_failed,
    patch_write_failed,
    patch_cache_flush_failed,
    protection_restore_failed,
};

[[nodiscard]] NativeMouseGatePatchResult replace_native_mouse_gate_call(
    const std::uintptr_t target_address,
    const std::array<std::uint8_t, kNativeMouseGateCallSize>& expected,
    const std::array<std::uint8_t, kNativeMouseGateCallSize>& replacement,
    DWORD* const system_error) noexcept {
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(target_address);
    static_cast<void>(expected);
    static_cast<void>(replacement);
    static_cast<void>(system_error);
    return NativeMouseGatePatchResult::patch_write_failed;
#else
    const auto bridge_address = reinterpret_cast<std::uintptr_t>(
        &wawvr_t4_native_menu_mouse_gate_bridge);
    const std::array<PeerThreadPatchRange, 2> patch_ranges{{
        {target_address, kNativeMouseGateCallSize},
        {bridge_address, kNativeMouseGateBridgeExecutionGuardSize},
    }};
    SuspendedPeerThreads suspended;
    PeerThreadQuiesceResult quiesce{};
    if (!suspended.suspend(patch_ranges, &quiesce)) {
        if (system_error != nullptr) {
            *system_error = quiesce.system_error;
        }
        return NativeMouseGatePatchResult::thread_suspend_failed;
    }

    auto* const target = reinterpret_cast<std::uint8_t*>(target_address);
    if (!bytes_match(target, expected)) {
        return NativeMouseGatePatchResult::expected_bytes_changed;
    }
    DWORD old_protection = 0;
    if (!VirtualProtect(
            target, replacement.size(), PAGE_EXECUTE_READWRITE,
            &old_protection)) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        return NativeMouseGatePatchResult::target_protection_failed;
    }
    if (std::memcmp(target, expected.data(), expected.size()) != 0) {
        DWORD ignored = 0;
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return NativeMouseGatePatchResult::expected_bytes_changed;
    }

    std::memcpy(target, replacement.data(), replacement.size());
    if (std::memcmp(target, replacement.data(), replacement.size()) != 0) {
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
        DWORD ignored = 0;
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return NativeMouseGatePatchResult::patch_write_failed;
    }
    if (!FlushInstructionCache(
            GetCurrentProcess(), target, replacement.size())) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
        DWORD ignored = 0;
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return NativeMouseGatePatchResult::patch_cache_flush_failed;
    }

    DWORD ignored = 0;
    if (!VirtualProtect(
            target, replacement.size(), old_protection, &ignored)) {
        if (system_error != nullptr) {
            *system_error = GetLastError();
        }
        std::memcpy(target, expected.data(), expected.size());
        FlushInstructionCache(GetCurrentProcess(), target, expected.size());
        VirtualProtect(target, replacement.size(), old_protection, &ignored);
        return NativeMouseGatePatchResult::protection_restore_failed;
    }
    return NativeMouseGatePatchResult::ok;
#endif
}

[[nodiscard]] bool bool_held(
    const wawvr::xr::BoolActionState& action) noexcept {
    return action.active && action.current;
}

void send_key_tap(
    const ClKeyEventFunction key_event,
    const std::int32_t key,
    const std::uint64_t now_milliseconds) noexcept {
    if (key_event == nullptr) {
        return;
    }
    const auto event_time = static_cast<std::uint32_t>(now_milliseconds);
    key_event(0, key, 1, event_time);
    key_event(0, key, 0, event_time);
}

[[nodiscard]] bool queue_console_command(
    const std::uintptr_t cbuf_add_text,
    const char* const command) noexcept {
#if defined(_MSC_VER) && defined(_M_IX86)
    if (cbuf_add_text != 0 && command != nullptr) {
        wawvr_call_t4_cbuf_add_text(cbuf_add_text, command);
        return true;
    }
#else
    static_cast<void>(cbuf_add_text);
    static_cast<void>(command);
#endif
    return false;
}

[[nodiscard]] const char* directional_action_name(
    const DirectionalAction action) noexcept {
    switch (action) {
    case DirectionalAction::crouch: return "crouch";
    case DirectionalAction::prone: return "prone";
    case DirectionalAction::stand: return "stand";
    case DirectionalAction::jump: return "jump";
    case DirectionalAction::none: return "none";
    }
    return "unknown";
}

[[nodiscard]] bool launch_requests_pezbot_autofill() noexcept {
    const wchar_t* const command_line = GetCommandLineW();
    return command_line != nullptr &&
        command_line_has_exact_marker(
            command_line, kPezBotAutofillLaunchMarker);
}

[[nodiscard]] MenuCursorRegion cursor_region(
    const std::uint32_t full_width,
    const std::uint32_t full_height,
    const bool active_gameplay_menu,
    const ActiveUiMonoSource source) noexcept {
    MenuCursorRegion region{
        .origin_x = 0,
        .width = full_width,
        .height = full_height,
    };
    if (!active_gameplay_menu || source == ActiveUiMonoSource::full_frame ||
        full_width < 2U) {
        return region;
    }
    region.width = full_width / 2U;
    if (source == ActiveUiMonoSource::right_eye) {
        region.origin_x = full_width / 2U;
    }
    return region;
}

[[nodiscard]] bool install_native_menu_mouse_gate(
    const wawvr::t4::ValidatedBindings& bindings,
    const std::uintptr_t expected_ui_mouse_event) noexcept {
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    static_cast<void>(expected_ui_mouse_event);
    return false;
#else
    if (g_native_mouse_gate_installed.load(std::memory_order_acquire)) {
        return true;
    }
    const auto prepared = wawvr::t4::prepare_inline_hook(
        bindings, wawvr::t4::HookSiteId::ui_mouse_event_native_call,
        reinterpret_cast<std::uintptr_t>(
            &wawvr_t4_native_menu_mouse_gate_bridge));
    if (!prepared.ok() ||
        !bindings.site_bytes_still_match(
            wawvr::t4::HookSiteId::ui_mouse_event_call_context_sentinel) ||
        prepared.hook->expected_size != kNativeMouseGateCallSize ||
        prepared.hook->minimum_patch_bytes != kNativeMouseGateCallSize) {
        return false;
    }

    std::array<std::uint8_t, kNativeMouseGateCallSize> original_call{};
    std::copy_n(
        prepared.hook->expected.begin(), original_call.size(),
        original_call.begin());
    if (original_call[0] != 0xE8 ||
        decode_relative_call_target(prepared.hook->target, original_call) !=
            expected_ui_mouse_event) {
        return false;
    }

    std::array<std::uint8_t, kNativeMouseGateCallSize> replacement_call{};
    if (!make_relative_call(
            prepared.hook->target,
            reinterpret_cast<std::uintptr_t>(
                &wawvr_t4_native_menu_mouse_gate_bridge),
            &replacement_call)) {
        return false;
    }

    // This callsite may execute before the Present-hook monitor gets a chance
    // to pin the module. Reject installation unless the bridge and its stable
    // original target can be guaranteed to remain process-lifetime valid.
    HMODULE pinned_module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(
                &wawvr_t4_native_menu_mouse_gate_bridge),
            &pinned_module)) {
        return false;
    }
    if (wawvr_t4_original_ui_mouse_event_address != 0 &&
        wawvr_t4_original_ui_mouse_event_address != expected_ui_mouse_event) {
        return false;
    }
    wawvr_t4_original_ui_mouse_event_address = expected_ui_mouse_event;

    DWORD system_error = 0;
    const auto patch = replace_native_mouse_gate_call(
        prepared.hook->target, original_call, replacement_call,
        &system_error);
    if (patch != NativeMouseGatePatchResult::ok) {
        return false;
    }

    g_native_mouse_gate_callsite = prepared.hook->target;
    g_native_mouse_gate_original_call = original_call;
    g_native_mouse_gate_replacement_call = replacement_call;
    g_native_mouse_gate_installed.store(true, std::memory_order_release);
    return true;
#endif
}

void restore_native_menu_mouse_gate() noexcept {
    if (!g_native_mouse_gate_installed.load(std::memory_order_acquire)) {
        return;
    }
    DWORD system_error = 0;
    const auto patch = replace_native_mouse_gate_call(
        g_native_mouse_gate_callsite, g_native_mouse_gate_replacement_call,
        g_native_mouse_gate_original_call, &system_error);
    if (patch == NativeMouseGatePatchResult::ok ||
        patch == NativeMouseGatePatchResult::expected_bytes_changed) {
        // On an ownership mismatch, preserve the foreign bytes and stop
        // claiming the callsite. The immutable original target remains valid
        // for any bridge invocation that was already in flight.
        g_native_mouse_gate_installed.store(false, std::memory_order_release);
        if (patch == NativeMouseGatePatchResult::ok) {
            g_native_mouse_gate_callsite = 0;
            g_native_mouse_gate_original_call = {};
            g_native_mouse_gate_replacement_call = {};
        }
    }
}

}  // namespace

bool simulator_requested_magazine_asset_inventory_ready() noexcept {
    if (!g_single_player_profile.load(std::memory_order_acquire) ||
        !environment_value_contains(
            L"XR_RUNTIME_JSON", L"openxr_simulator")) {
        return false;
    }
    const SimulatorBoltActionWeapon requested =
        simulator_magazine_weapon_from_environment();
    const SimulatorWeaponEquipPhase phase =
        g_simulator_weapon_equip_phase.load(std::memory_order_acquire);
    const bool inventory_window =
        phase == SimulatorWeaponEquipPhase::Direct ||
        (g_simulator_map_transition_seen.load(std::memory_order_acquire) &&
         phase == SimulatorWeaponEquipPhase::NeedWeapon);
    return requested != SimulatorBoltActionWeapon::None &&
        g_simulator_weapon.load(std::memory_order_acquire) == requested &&
        inventory_window;
}

#if defined(_MSC_VER) && defined(_M_IX86)
extern "C" __declspec(naked) void
wawvr_t4_native_menu_mouse_gate_bridge() noexcept {
    __asm {
        // x/y are the bridge's two cdecl arguments. The caller's dx/dy remain
        // at +14h/+18h after x, y, saved ESI, and the CL caller return address.
        mov eax, dword ptr [esp + 14h]
        or eax, dword ptr [esp + 18h]
        jz stationary_mouse
        jmp dword ptr [wawvr_t4_original_ui_mouse_event_address]
    stationary_mouse:
        ret
    }
}
#endif

bool bind_t4_menu_input(
    const wawvr::t4::ValidatedBindings& bindings) noexcept {
    clear_t4_menu_input();
#if !defined(_MSC_VER) || !defined(_M_IX86)
    static_cast<void>(bindings);
    return false;
#else
    constexpr wawvr::t4::HookSiteId required_ui_sites[] = {
        wawvr::t4::HookSiteId::cl_key_event_entry_sentinel,
        wawvr::t4::HookSiteId::cl_key_event_dispatch_context_sentinel,
        wawvr::t4::HookSiteId::ui_mouse_event_entry_sentinel,
        wawvr::t4::HookSiteId::ui_mouse_event_native_call,
        wawvr::t4::HookSiteId::ui_mouse_event_call_context_sentinel,
    };
    for (const auto site : required_ui_sites) {
        if (!bindings.site_bytes_still_match(site)) {
            return false;
        }
    }
    const auto cl_key_event = bindings.site_address(
        wawvr::t4::HookSiteId::cl_key_event_entry_sentinel);
    const auto ui_mouse_event = bindings.site_address(
        wawvr::t4::HookSiteId::ui_mouse_event_entry_sentinel);
    if (!cl_key_event.has_value() || !ui_mouse_event.has_value() ||
        !executable_address(*cl_key_event) ||
        !executable_address(*ui_mouse_event)) {
        return false;
    }
    if (g_native_mouse_gate_installed.load(std::memory_order_acquire) ||
        !install_native_menu_mouse_gate(bindings, *ui_mouse_event)) {
        return false;
    }
    g_ui_mouse_event_address.store(
        *ui_mouse_event, std::memory_order_release);
    g_cl_key_event_address.store(*cl_key_event, std::memory_order_release);
    const auto executable_variant = bindings.profile().variant;
    g_single_player_profile.store(
        executable_variant == wawvr::t4::ExecutableVariant::single_player,
        std::memory_order_release);
    g_multiplayer_profile.store(
        executable_variant == wawvr::t4::ExecutableVariant::multiplayer,
        std::memory_order_release);
    const bool simulator_kar98_requested =
        executable_variant == wawvr::t4::ExecutableVariant::single_player &&
        environment_flag_enabled(kSimulatorKar98Environment);
    const bool simulator_colt_probe_requested =
        executable_variant == wawvr::t4::ExecutableVariant::single_player &&
        environment_flag_enabled(kSimulatorColtProbeEnvironment);
    const bool simulator_m1carbine_requested =
        executable_variant == wawvr::t4::ExecutableVariant::single_player &&
        environment_flag_enabled(kSimulatorM1CarbineEnvironment);
    const SimulatorBoltActionWeapon simulator_magazine_weapon =
        executable_variant == wawvr::t4::ExecutableVariant::single_player
        ? simulator_magazine_weapon_from_environment()
        : SimulatorBoltActionWeapon::None;
    const bool simulator_magazine_weapon_requested =
        simulator_magazine_weapon != SimulatorBoltActionWeapon::None;
    const bool headset_m1carbine_test_requested =
        executable_variant == wawvr::t4::ExecutableVariant::single_player &&
        environment_flag_enabled(kHeadsetM1CarbineTestEnvironment);
    const bool headset_upgraded_m1carbine_test_requested =
        executable_variant == wawvr::t4::ExecutableVariant::single_player &&
        environment_flag_enabled(
            kHeadsetZombieM1CarbineUpgradedTestEnvironment);
    const bool headset_zombie_magazine_test_requested =
        executable_variant == wawvr::t4::ExecutableVariant::single_player &&
        configure_headset_zombie_magazine_command();
    const bool headset_scoped_zombie_kar98_test_requested =
        executable_variant == wawvr::t4::ExecutableVariant::single_player &&
        environment_flag_enabled(kHeadsetScopedZombieKar98TestEnvironment);
    const bool headset_scoped_mosin_test_requested =
        executable_variant == wawvr::t4::ExecutableVariant::single_player &&
        environment_flag_enabled(kHeadsetScopedMosinTestEnvironment);
    const bool headset_svt40_test_requested =
        executable_variant == wawvr::t4::ExecutableVariant::single_player &&
        environment_flag_enabled(kHeadsetSvt40TestEnvironment);
    const bool headset_ppsh_test_requested =
        executable_variant == wawvr::t4::ExecutableVariant::single_player &&
        environment_flag_enabled(kHeadsetPpshTestEnvironment);
    const bool headset_m1garand_gl_test_requested =
        executable_variant == wawvr::t4::ExecutableVariant::single_player &&
        environment_flag_enabled(
            kHeadsetM1GarandGrenadeLauncherTestEnvironment);
    const bool direct_m1garand_gl_test_requested =
        headset_m1garand_gl_test_requested ||
        simulator_magazine_weapon ==
            SimulatorBoltActionWeapon::M1GarandGrenadeLauncher;
    if (executable_variant == wawvr::t4::ExecutableVariant::single_player) {
        // The exact map-local indices vary across Campaign and Zombies. Keep
        // the read-only definition-table lookup and native selector available
        // for the normal M1 Garand/M7 Y-button toggle, not just test grants.
        const auto weapon_definition_table = bindings.data_address(
            wawvr::t4::DataSymbolId::weapon_definition_pointer_table,
            kHeadsetTestWeaponTableSpan);
        const auto last_weapon_index = bindings.module().address(
            kHeadsetTestLastWeaponIndexRva, sizeof(std::uint32_t));
        const auto select_weapon_index = bindings.module().address(
            kHeadsetTestSelectWeaponIndexRva, 0x71);
        g_headset_test_weapon_definition_table.store(
            weapon_definition_table.value_or(0), std::memory_order_release);
        g_headset_test_last_weapon_index.store(
            last_weapon_index.value_or(0), std::memory_order_release);
        g_headset_test_select_weapon_index.store(
            select_weapon_index.value_or(0), std::memory_order_release);
    }
    if (direct_m1garand_gl_test_requested) {
        const auto local_player_entity = bindings.data_address(
            wawvr::t4::DataSymbolId::local_player_entity,
            kHeadsetTestLocalPlayerClientPointerOffset +
                sizeof(std::uint32_t));
        const auto give_player_weapon = bindings.module().address(
            kHeadsetTestGivePlayerWeaponRva, 0x160);
        g_headset_test_local_player_entity.store(
            local_player_entity.value_or(0), std::memory_order_release);
        g_headset_test_give_player_weapon.store(
            give_player_weapon.value_or(0), std::memory_order_release);
    }
    const bool simulator_springfield_requested =
        executable_variant == wawvr::t4::ExecutableVariant::single_player &&
        environment_flag_enabled(kSimulatorSpringfieldEnvironment);
    const bool simulator_mosin_requested =
        executable_variant == wawvr::t4::ExecutableVariant::single_player &&
        environment_flag_enabled(kSimulatorMosinEnvironment);
    const unsigned simulator_weapon_request_count =
        static_cast<unsigned>(simulator_colt_probe_requested) +
        static_cast<unsigned>(simulator_m1carbine_requested) +
        static_cast<unsigned>(simulator_magazine_weapon_requested) +
        static_cast<unsigned>(headset_m1carbine_test_requested) +
        static_cast<unsigned>(headset_upgraded_m1carbine_test_requested) +
        static_cast<unsigned>(headset_zombie_magazine_test_requested) +
        static_cast<unsigned>(headset_scoped_zombie_kar98_test_requested) +
        static_cast<unsigned>(headset_scoped_mosin_test_requested) +
        static_cast<unsigned>(headset_svt40_test_requested) +
        static_cast<unsigned>(headset_ppsh_test_requested) +
        static_cast<unsigned>(headset_m1garand_gl_test_requested) +
        static_cast<unsigned>(simulator_kar98_requested) +
        static_cast<unsigned>(simulator_springfield_requested) +
        static_cast<unsigned>(simulator_mosin_requested);
    const SimulatorBoltActionWeapon simulator_weapon =
        simulator_weapon_request_count != 1U ?
            SimulatorBoltActionWeapon::None :
        simulator_magazine_weapon_requested ? simulator_magazine_weapon :
        headset_zombie_magazine_test_requested ?
            (g_headset_zombie_magazine_uses_nacht_m1garand ?
                 SimulatorBoltActionWeapon::M1Garand :
                 SimulatorBoltActionWeapon::HeadsetZombieMagazineWeapon) :
        headset_scoped_zombie_kar98_test_requested ?
            SimulatorBoltActionWeapon::Kar98ScopedZombie :
        headset_scoped_mosin_test_requested ?
            SimulatorBoltActionWeapon::MosinScoped :
        headset_svt40_test_requested ?
            SimulatorBoltActionWeapon::HeadsetSvt40 :
        headset_m1garand_gl_test_requested ?
            SimulatorBoltActionWeapon::HeadsetM1GarandGrenadeLauncher :
        headset_ppsh_test_requested ? SimulatorBoltActionWeapon::Ppsh :
        headset_upgraded_m1carbine_test_requested ?
            SimulatorBoltActionWeapon::HeadsetZombieM1CarbineUpgraded :
        simulator_colt_probe_requested ?
            SimulatorBoltActionWeapon::ColtProbe :
        (simulator_m1carbine_requested ||
         headset_m1carbine_test_requested) ?
            SimulatorBoltActionWeapon::M1Carbine :
        simulator_mosin_requested ? SimulatorBoltActionWeapon::Mosin :
        simulator_springfield_requested ?
            SimulatorBoltActionWeapon::Springfield :
            SimulatorBoltActionWeapon::Kar98;
    const bool simulator_kar98_enabled =
        simulator_weapon != SimulatorBoltActionWeapon::None;
    g_simulator_weapon_equip_phase.store(
        !simulator_kar98_enabled ? SimulatorWeaponEquipPhase::Complete :
        simulator_weapon_requires_map_handoff(simulator_weapon)
            ? SimulatorWeaponEquipPhase::NeedMap
            : SimulatorWeaponEquipPhase::Direct,
        std::memory_order_relaxed);
    g_simulator_kar98_queued.store(false, std::memory_order_relaxed);
    g_simulator_map_transition_seen.store(
        false, std::memory_order_relaxed);
    g_simulator_map_command_queued_at.store(
        0, std::memory_order_relaxed);
    g_simulator_kar98_eligible_since.store(0, std::memory_order_relaxed);
    g_simulator_kar98_service_seen.store(false, std::memory_order_relaxed);
    g_headset_svt40_selection_not_before.store(
        0, std::memory_order_relaxed);
    g_headset_svt40_next_cycle_at.store(0, std::memory_order_relaxed);
    g_headset_svt40_cycle_attempts.store(0, std::memory_order_relaxed);
    g_headset_svt40_selection_confirmed_logged.store(
        false, std::memory_order_relaxed);
    g_headset_svt40_selection_exhausted_logged.store(
        false, std::memory_order_relaxed);
    g_headset_m1garand_gl_selection_not_before.store(
        0, std::memory_order_relaxed);
    g_headset_m1garand_gl_next_cycle_at.store(0, std::memory_order_relaxed);
    g_headset_m1garand_gl_cycle_attempts.store(0, std::memory_order_relaxed);
    g_headset_m1garand_gl_pair_installed.store(
        false, std::memory_order_relaxed);
    g_headset_m1garand_gl_rifle_index.store(
        0, std::memory_order_relaxed);
    g_headset_m1garand_gl_selection_confirmed_logged.store(
        false, std::memory_order_relaxed);
    g_headset_m1garand_gl_selection_exhausted_logged.store(
        false, std::memory_order_relaxed);
    const bool headset_test_requested =
        headset_m1carbine_test_requested ||
        headset_upgraded_m1carbine_test_requested ||
        headset_zombie_magazine_test_requested ||
        headset_scoped_zombie_kar98_test_requested ||
        headset_scoped_mosin_test_requested ||
        headset_svt40_test_requested || headset_ppsh_test_requested ||
        headset_m1garand_gl_test_requested;
    // A physical tester cannot usefully press a disposable start trigger while
    // reconnecting a headset.  An explicit headset preset therefore enters
    // the same two-second gameplay-stability gate already used by simulator
    // probes without requiring that extra input edge.
    g_simulator_kar98_start_trigger_seen.store(
        headset_test_requested, std::memory_order_relaxed);
    g_simulator_kar98_start_trigger_released.store(
        headset_test_requested, std::memory_order_relaxed);
    // Publish the exact selector only after every latch is initialized. The
    // service acquires this one word once per frame, so it can never observe
    // an enabled request paired with another weapon's default selector.
    g_simulator_weapon.store(simulator_weapon, std::memory_order_release);
    if (environment_value_contains(
            L"XR_RUNTIME_JSON", L"openxr_simulator")) {
        stereo_diagnostic_log(
            "SimulatorDiag weapon preset requests=%u selected=%u m1garandGl=%d",
            simulator_weapon_request_count,
            static_cast<unsigned>(simulator_weapon),
            headset_m1garand_gl_test_requested ? 1 : 0);
    }
    if (environment_value_contains(
            L"XR_RUNTIME_JSON", L"openxr_simulator")) {
        append_simulator_kar98_probe(
            simulator_weapon_request_count > 1U ?
                "equip-flag-conflict-rejected" :
            simulator_magazine_weapon_requested ?
                "equip-magazine-weapon-enabled" :
            simulator_colt_probe_requested ? "probe-colt-flag-enabled" :
            (simulator_m1carbine_requested ||
             headset_m1carbine_test_requested) ?
                "equip-m1carbine-flag-enabled" :
            simulator_mosin_requested ? "equip-mosin-flag-enabled" :
            simulator_springfield_requested ?
                "equip-springfield-flag-enabled" :
            headset_scoped_zombie_kar98_test_requested ?
                "equip-scoped-zombie-kar98-headset-flag-enabled" :
            headset_scoped_mosin_test_requested ?
                "equip-scoped-mosin-headset-flag-enabled" :
            headset_svt40_test_requested ?
                "equip-svt40-headset-flag-enabled" :
            headset_m1garand_gl_test_requested ?
                "equip-m1garand-gl-headset-flag-enabled" :
            headset_ppsh_test_requested ?
                "equip-ppsh-headset-flag-enabled" :
            simulator_kar98_requested ? "equip-kar98-flag-enabled" :
                                        "equip-flag-disabled");
    }

    // Mouse/key injection is the mandatory menu path. Weapon cycling is a
    // separate optional convenience built on Cbuf_AddText and its command
    // identity; an executable variant that lacks those extra sentinels must
    // not lose native menu interaction.
    constexpr wawvr::t4::HookSiteId optional_command_sites[] = {
        wawvr::t4::HookSiteId::cbuf_add_text_entry_sentinel,
        wawvr::t4::HookSiteId::winmain_cbuf_add_text_call_context_sentinel,
        wawvr::t4::HookSiteId::weapnext_command_identity_sentinel,
        wawvr::t4::HookSiteId::weapnext_command_registration_sentinel,
        wawvr::t4::HookSiteId::weapnext_handler_entry_sentinel,
    };
    bool command_sites_match = true;
    for (const auto site : optional_command_sites) {
        command_sites_match = command_sites_match &&
            bindings.site_bytes_still_match(site);
    }
    if (command_sites_match) {
        const auto cbuf_add_text = bindings.site_address(
            wawvr::t4::HookSiteId::cbuf_add_text_entry_sentinel);
        if (cbuf_add_text.has_value() &&
            executable_address(*cbuf_add_text)) {
            g_cbuf_add_text_address.store(
                *cbuf_add_text, std::memory_order_release);
            configure_performance_command_service(
                executable_variant == wawvr::t4::ExecutableVariant::single_player);
            if (simulator_kar98_enabled) {
                append_simulator_kar98_probe("cbuf-bound");
            }
            g_pezbot_autofill_enabled.store(
                bindings.profile().variant ==
                        wawvr::t4::ExecutableVariant::multiplayer &&
                    launch_requests_pezbot_autofill(),
                std::memory_order_release);
        }
    }
    return true;
#endif
}

void clear_t4_menu_input() noexcept {
    clear_performance_command_service();
    g_simulator_weapon.store(
        SimulatorBoltActionWeapon::None, std::memory_order_release);
    g_headset_zombie_magazine_command.fill('\0');
    g_headset_zombie_magazine_uses_nacht_m1garand = false;
    restore_native_menu_mouse_gate();
    g_cl_key_event_address.store(0, std::memory_order_release);
    g_ui_mouse_event_address.store(0, std::memory_order_release);
    g_cbuf_add_text_address.store(0, std::memory_order_release);
    g_headset_test_local_player_entity.store(0, std::memory_order_release);
    g_headset_test_weapon_definition_table.store(
        0, std::memory_order_release);
    g_headset_test_last_weapon_index.store(0, std::memory_order_release);
    g_headset_test_select_weapon_index.store(0, std::memory_order_release);
    g_headset_test_give_player_weapon.store(0, std::memory_order_release);
    g_single_player_profile.store(false, std::memory_order_release);
    g_multiplayer_profile.store(false, std::memory_order_release);
    g_campaign_unlock_queued.store(false, std::memory_order_release);
    g_pezbot_autofill_enabled.store(false, std::memory_order_release);
    g_simulator_kar98_queued.store(false, std::memory_order_relaxed);
    g_simulator_weapon_equip_phase.store(
        SimulatorWeaponEquipPhase::Complete, std::memory_order_relaxed);
    g_simulator_map_transition_seen.store(
        false, std::memory_order_relaxed);
    g_simulator_map_command_queued_at.store(
        0, std::memory_order_relaxed);
    g_simulator_kar98_eligible_since.store(0, std::memory_order_relaxed);
    g_simulator_kar98_service_seen.store(false, std::memory_order_relaxed);
    g_simulator_kar98_start_trigger_seen.store(
        false, std::memory_order_relaxed);
    g_simulator_kar98_start_trigger_released.store(
        false, std::memory_order_relaxed);
    g_headset_svt40_selection_not_before.store(
        0, std::memory_order_relaxed);
    g_headset_svt40_next_cycle_at.store(0, std::memory_order_relaxed);
    g_headset_svt40_cycle_attempts.store(0, std::memory_order_relaxed);
    g_headset_svt40_selection_confirmed_logged.store(
        false, std::memory_order_relaxed);
    g_headset_svt40_selection_exhausted_logged.store(
        false, std::memory_order_relaxed);
    g_headset_m1garand_gl_selection_not_before.store(
        0, std::memory_order_relaxed);
    g_headset_m1garand_gl_next_cycle_at.store(
        0, std::memory_order_relaxed);
    g_headset_m1garand_gl_cycle_attempts.store(
        0, std::memory_order_relaxed);
    g_headset_m1garand_gl_pair_installed.store(
        false, std::memory_order_relaxed);
    g_headset_m1garand_gl_rifle_index.store(
        0, std::memory_order_relaxed);
    g_headset_m1garand_gl_selection_confirmed_logged.store(
        false, std::memory_order_relaxed);
    g_headset_m1garand_gl_selection_exhausted_logged.store(
        false, std::memory_order_relaxed);
    g_pezbot_autofill_state = {};
}

T4MenuInputServiceResult service_t4_menu_input_after_com_frame(
    const wawvr::xr::FrameState& frame,
    std::uint32_t full_backbuffer_width,
    std::uint32_t full_backbuffer_height,
    const ActiveUiMonoSource active_ui_source,
    const MenuPointerSurface* const visible_menu_surface,
    const bool menu_escape_tap_requested,
    const std::uint64_t now_milliseconds,
    T4MenuInputState* const state) noexcept {
    T4MenuInputServiceResult result{};
    if (state == nullptr) {
        g_simulator_kar98_eligible_since.store(
            0, std::memory_order_release);
        return result;
    }
    const auto note_invalid_map_handoff_frame = []() noexcept {
        g_simulator_kar98_eligible_since.store(
            0, std::memory_order_release);
        if (g_simulator_weapon_equip_phase.load(
                std::memory_order_acquire) !=
            SimulatorWeaponEquipPhase::WaitingForMapTransition) {
            return;
        }
        bool transition_unseen = false;
        if (g_simulator_map_transition_seen.compare_exchange_strong(
                transition_unseen, true, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            append_simulator_kar98_probe(
                "campaign-map-presentation-gap-seen");
        }
    };
    const auto key_event = reinterpret_cast<ClKeyEventFunction>(
        g_cl_key_event_address.load(std::memory_order_acquire));
    const auto mouse_event = reinterpret_cast<UiMouseEventFunction>(
        g_ui_mouse_event_address.load(std::memory_order_acquire));
    const std::uintptr_t cbuf_add_text =
        g_cbuf_add_text_address.load(std::memory_order_acquire);
    if (key_event == nullptr || mouse_event == nullptr) {
        *state = {};
        note_invalid_map_handoff_frame();
        return result;
    }

    T4PresentationState presentation = read_t4_presentation_state();
    result.state_valid = presentation.valid;
    if (!presentation.valid) {
        *state = {};
        note_invalid_map_handoff_frame();
        return result;
    }

    service_performance_command_file_after_com_frame(
        cbuf_add_text, &queue_console_command, now_milliseconds,
        presentation.valid, presentation.key_catchers,
        presentation.connection_state);

    const CampaignUnlockDisposition campaign_unlock =
        campaign_unlock_disposition(
            g_single_player_profile.load(std::memory_order_acquire),
            presentation.valid,
            cbuf_add_text != 0,
            g_campaign_unlock_queued.load(std::memory_order_acquire));
    if (campaign_unlock == CampaignUnlockDisposition::queue_now &&
        queue_console_command(
            cbuf_add_text, kCampaignUnlockCommand.data())) {
        g_campaign_unlock_queued.store(true, std::memory_order_release);
        result.campaign_unlock_queued = true;
    }

    if (menu_escape_tap_requested) {
        send_key_tap(key_event, kT4KeyEscape, now_milliseconds);
        result.menu_button_tapped = true;
        // Escape may synchronously open or close a native menu. Re-read the
        // exact state before deciding whether cursor/A/B own this frame.
        presentation = read_t4_presentation_state();
        result.state_valid = presentation.valid;
        if (!presentation.valid) {
            *state = {};
            note_invalid_map_handoff_frame();
            return result;
        }
    }

    result.ui_active =
        (presentation.key_catchers & kT4UiKeyCatcher) != 0;
    const bool active_gameplay_menu =
        presentation.connection_state ==
        presentation.active_connection_state;
    const bool multiplayer_profile =
        g_multiplayer_profile.load(std::memory_order_acquire);
    const bool pezbot_autofill_requested =
        should_queue_pezbot_autofill(
            {
                .enabled = g_pezbot_autofill_enabled.load(
                    std::memory_order_acquire),
                .multiplayer_profile = multiplayer_profile,
                .presentation_state_valid = presentation.valid,
                .connection_state = presentation.connection_state,
                .active_connection_state =
                    presentation.active_connection_state,
            },
            &g_pezbot_autofill_state);
    if (pezbot_autofill_requested) {
        if (queue_console_command(
                cbuf_add_text, kPezBotAutofillCommand)) {
            result.pezbot_autofill_queued = true;
        } else {
            // The exact x86 binding normally makes this impossible. If it is
            // ever unavailable, retry on the next frontend frame instead of
            // silently consuming the one-shot lifecycle edge.
            g_pezbot_autofill_state.disconnected_frontend_latched = false;
        }
    }
    const auto& simulator_right_hand = frame.actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Right)];
    const SimulatorBoltActionWeapon simulator_weapon =
        g_simulator_weapon.load(std::memory_order_acquire);
    const bool simulator_weapon_enabled =
        simulator_weapon != SimulatorBoltActionWeapon::None;
    SimulatorWeaponEquipPhase simulator_equip_phase =
        g_simulator_weapon_equip_phase.load(std::memory_order_acquire);
    const bool simulator_gameplay_active =
        frame.actions.focused &&
        controller_gameplay_input_allowed(
            presentation.key_catchers,
            presentation.connection_state,
            presentation.active_connection_state);
    const bool explicit_garand_gl_test_gameplay_active =
        (simulator_weapon ==
             SimulatorBoltActionWeapon::HeadsetM1GarandGrenadeLauncher ||
         simulator_weapon ==
             SimulatorBoltActionWeapon::M1GarandGrenadeLauncher) &&
        presentation.valid &&
        presentation.connection_state ==
            presentation.active_connection_state;
    const SimulatorMapHandoffPresentation simulator_map_presentation{
        g_simulator_map_transition_seen.load(std::memory_order_acquire),
        presentation.valid,
        presentation.connection_state,
        presentation.active_connection_state,
    };
    const bool simulator_map_connection_restored =
        simulator_map_handoff_connection_ready(simulator_map_presentation);
    if (simulator_weapon_enabled &&
        simulator_equip_phase ==
            SimulatorWeaponEquipPhase::WaitingForMapTransition) {
        const std::uint64_t map_command_queued_at =
            g_simulator_map_command_queued_at.load(std::memory_order_acquire);
        const bool map_handoff_timed_out =
            map_command_queued_at != 0 &&
            now_milliseconds >= map_command_queued_at &&
            now_milliseconds - map_command_queued_at >=
                kSimulatorMapHandoffTimeoutMilliseconds;
        if (map_handoff_timed_out) {
            SimulatorWeaponEquipPhase expected =
                SimulatorWeaponEquipPhase::WaitingForMapTransition;
            if (g_simulator_weapon_equip_phase.compare_exchange_strong(
                    expected, SimulatorWeaponEquipPhase::Queueing,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                g_simulator_kar98_queued.store(
                    true, std::memory_order_release);
                g_simulator_map_transition_seen.store(
                    false, std::memory_order_release);
                g_simulator_map_command_queued_at.store(
                    0, std::memory_order_release);
                g_simulator_kar98_eligible_since.store(
                    0, std::memory_order_release);
                g_simulator_weapon_equip_phase.store(
                    SimulatorWeaponEquipPhase::Complete,
                    std::memory_order_release);
                simulator_equip_phase = SimulatorWeaponEquipPhase::Complete;
                append_simulator_kar98_probe(
                    "campaign-map-handoff-timeout-aborted");
            }
        } else
        if (presentation.valid &&
            presentation.connection_state !=
                presentation.active_connection_state) {
            bool transition_unseen = false;
            if (g_simulator_map_transition_seen.compare_exchange_strong(
                    transition_unseen, true, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                append_simulator_kar98_probe(
                    "campaign-map-transition-seen");
            }
        } else if (simulator_map_connection_restored) {
            SimulatorWeaponEquipPhase expected =
                SimulatorWeaponEquipPhase::WaitingForMapTransition;
            if (g_simulator_weapon_equip_phase.compare_exchange_strong(
                    expected, SimulatorWeaponEquipPhase::Queueing,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                g_simulator_kar98_queued.store(
                    false, std::memory_order_release);
                g_simulator_map_command_queued_at.store(
                    0, std::memory_order_release);
                g_simulator_kar98_eligible_since.store(
                    0, std::memory_order_release);
                g_simulator_weapon_equip_phase.store(
                    SimulatorWeaponEquipPhase::NeedWeapon,
                    std::memory_order_release);
                simulator_equip_phase = SimulatorWeaponEquipPhase::NeedWeapon;
                append_simulator_kar98_probe(
                    "campaign-map-connection-restored");
            }
        }
    }
    const bool simulator_start_trigger_held =
        simulator_right_hand.trigger.active &&
        std::isfinite(simulator_right_hand.trigger.current) &&
        simulator_right_hand.trigger.current >= kControllerButtonThreshold;
    bool unset_latch = false;
    if (simulator_weapon_enabled && simulator_start_trigger_held &&
        g_simulator_kar98_start_trigger_seen.compare_exchange_strong(
            unset_latch, true, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        append_simulator_kar98_probe("start-trigger-seen");
    } else if (simulator_weapon_enabled &&
               g_simulator_kar98_start_trigger_seen.load(
                   std::memory_order_acquire) &&
               !simulator_start_trigger_held &&
               !g_simulator_kar98_start_trigger_released.load(
                   std::memory_order_acquire)) {
        bool unreleased = false;
        if (g_simulator_kar98_start_trigger_released.compare_exchange_strong(
                unreleased, true, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            append_simulator_kar98_probe("start-trigger-released");
        }
    }
    const bool simulator_kar98_eligible =
        simulator_weapon_enabled &&
        simulator_equip_phase != SimulatorWeaponEquipPhase::Complete &&
        simulator_equip_phase != SimulatorWeaponEquipPhase::Queueing &&
        simulator_equip_phase !=
            SimulatorWeaponEquipPhase::WaitingForMapTransition &&
        !g_simulator_kar98_queued.load(std::memory_order_acquire) &&
        g_simulator_kar98_start_trigger_released.load(
            std::memory_order_acquire) &&
        simulator_weapon_equip_gate_active(
            simulator_gameplay_active ||
                explicit_garand_gl_test_gameplay_active,
            simulator_equip_phase == SimulatorWeaponEquipPhase::NeedWeapon,
            simulator_map_presentation);
    bool unseen_service = false;
    if (simulator_weapon_enabled &&
        g_simulator_kar98_service_seen.compare_exchange_strong(
            unseen_service, true, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        append_simulator_kar98_probe("service-frame");
        append_simulator_kar98_probe(
            frame.actions.focused ? "actions-focused" :
                                    "actions-not-focused");
    }
    if (!simulator_kar98_eligible) {
        g_simulator_kar98_eligible_since.store(
            0, std::memory_order_release);
    } else {
        std::uint64_t no_eligible_time = 0;
        if (g_simulator_kar98_eligible_since.compare_exchange_strong(
                no_eligible_time, now_milliseconds,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            append_simulator_kar98_probe(
                simulator_gameplay_active ?
                    "gameplay-eligible" :
                    "post-map-connection-eligible");
        }
    }
    const std::uint64_t simulator_kar98_eligible_since =
        g_simulator_kar98_eligible_since.load(std::memory_order_acquire);
    const bool simulator_kar98_requested =
        simulator_kar98_eligible && simulator_kar98_eligible_since != 0 &&
        now_milliseconds >= simulator_kar98_eligible_since &&
        now_milliseconds - simulator_kar98_eligible_since >=
            kSimulatorDirectEquipStabilityMilliseconds;
    const char* const simulator_weapon_command =
        command_for_simulator_weapon(simulator_weapon, simulator_equip_phase);
    if (simulator_kar98_requested &&
        simulator_weapon_command != nullptr) {
        SimulatorWeaponEquipPhase expected = simulator_equip_phase;
        if (g_simulator_weapon_equip_phase.compare_exchange_strong(
                expected, SimulatorWeaponEquipPhase::Queueing,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            const bool map_handoff = simulator_equip_phase ==
                SimulatorWeaponEquipPhase::NeedMap;
            if (map_handoff) {
                // Establish the complete transition latch before the command
                // can be consumed by the engine on another frame.
                g_simulator_map_transition_seen.store(
                    false, std::memory_order_release);
                g_simulator_map_command_queued_at.store(
                    now_milliseconds, std::memory_order_release);
            }
            const bool command_queued = queue_console_command(
                cbuf_add_text, simulator_weapon_command);
            if (!command_queued) {
                if (map_handoff) {
                    g_simulator_map_command_queued_at.store(
                        0, std::memory_order_release);
                }
                g_simulator_kar98_eligible_since.store(
                    0, std::memory_order_release);
                g_simulator_weapon_equip_phase.store(
                    simulator_equip_phase, std::memory_order_release);
            } else {
                g_simulator_kar98_queued.store(
                    true, std::memory_order_release);
                g_simulator_weapon_equip_phase.store(
                    map_handoff
                        ? SimulatorWeaponEquipPhase::WaitingForMapTransition
                        : SimulatorWeaponEquipPhase::Complete,
                    std::memory_order_release);
                if (!map_handoff &&
                    simulator_weapon ==
                        SimulatorBoltActionWeapon::HeadsetSvt40) {
                    g_headset_svt40_selection_not_before.store(
                        now_milliseconds +
                            kHeadsetSvt40SelectionSettleMilliseconds,
                        std::memory_order_release);
                }
                if (!map_handoff &&
                    (simulator_weapon == SimulatorBoltActionWeapon::
                         HeadsetM1GarandGrenadeLauncher ||
                     simulator_weapon == SimulatorBoltActionWeapon::
                         M1GarandGrenadeLauncher)) {
                    g_headset_m1garand_gl_selection_not_before.store(
                        now_milliseconds +
                            kHeadsetM1GarandGlSelectionSettleMilliseconds,
                        std::memory_order_release);
                }
                result.simulator_kar98_queued = true;
                if (map_handoff) {
                    append_simulator_kar98_probe(
                        "campaign-map-command-queued");
                    append_simulator_kar98_probe(
                        "map-handoff-stage-queued");
                } else if (simulator_weapon ==
                           SimulatorBoltActionWeapon::ColtProbe) {
                    append_simulator_kar98_probe(
                        "god-queued-for-colt-probe");
                    WAWVR_STEREO_DIAG_ONCE(
                        "SimulatorDiag protected the disposable starting-Colt reload probe without changing weapons");
                } else if (simulator_weapon ==
                           SimulatorBoltActionWeapon::M1Carbine) {
                    append_simulator_kar98_probe("give-m1carbine-queued");
                    WAWVR_STEREO_DIAG_ONCE(
                        "SimulatorDiag visibly equipping the Nacht M1A1 Carbine for detachable-magazine inventory");
                } else if (simulator_weapon ==
                           SimulatorBoltActionWeapon::Mosin) {
                    append_simulator_kar98_probe("devmap-ber1-queued-for-mosin");
                    WAWVR_STEREO_DIAG_ONCE(
                        "SimulatorDiag switching the disposable simulator process to ber1 so the exact campaign Mosin asset can load");
                } else if (simulator_weapon ==
                           SimulatorBoltActionWeapon::Springfield) {
                    append_simulator_kar98_probe(
                        "give-springfield-queued");
                    WAWVR_STEREO_DIAG_ONCE(
                        "SimulatorDiag visibly equipping the Springfield for rifle-specific controller testing");
                } else if (simulator_weapon ==
                           SimulatorBoltActionWeapon::Kar98) {
                    append_simulator_kar98_probe("give-kar98k-queued");
                    WAWVR_STEREO_DIAG_ONCE(
                        "SimulatorDiag visibly equipping the Zombies Kar98 for rifle-specific controller testing");
                } else {
                    append_simulator_kar98_probe(
                        "give-magazine-weapon-queued");
                    WAWVR_STEREO_DIAG_ONCE(
                        "SimulatorDiag visibly equipping an exact allowlisted magazine-fed weapon for controller testing");
                }
            }
        }
    }

    if (simulator_weapon == SimulatorBoltActionWeapon::HeadsetSvt40 &&
        g_simulator_weapon_equip_phase.load(std::memory_order_acquire) ==
            SimulatorWeaponEquipPhase::Complete) {
        const HeadsetSvt40SelectionState selection =
            headset_svt40_selection_state();
        if (selection == HeadsetSvt40SelectionState::svt40_selected) {
            bool unlogged = false;
            if (g_headset_svt40_selection_confirmed_logged
                    .compare_exchange_strong(
                        unlogged, true, std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                append_simulator_kar98_probe(
                    "svt40-selection-confirmed");
                WAWVR_STEREO_DIAG_ONCE(
                    "SimulatorDiag exact SVT-40 selection confirmed from the rendered held-weapon identity");
            }
        } else if (selection ==
                       HeadsetSvt40SelectionState::other_weapon) {
            const std::uint64_t not_before =
                g_headset_svt40_selection_not_before.load(
                    std::memory_order_acquire);
            const std::uint64_t next_cycle_at =
                g_headset_svt40_next_cycle_at.load(
                    std::memory_order_acquire);
            const std::uint32_t attempts =
                g_headset_svt40_cycle_attempts.load(
                    std::memory_order_acquire);
            if (not_before != 0 && now_milliseconds >= not_before &&
                now_milliseconds >= next_cycle_at &&
                attempts < kHeadsetSvt40MaximumCycleAttempts &&
                queue_console_command(cbuf_add_text, kWeaponNextCommand)) {
                g_headset_svt40_cycle_attempts.fetch_add(
                    1, std::memory_order_acq_rel);
                g_headset_svt40_next_cycle_at.store(
                    now_milliseconds +
                        kHeadsetSvt40CycleCooldownMilliseconds,
                    std::memory_order_release);
                append_simulator_kar98_probe(
                    "svt40-selection-cycle-queued");
            } else if (
                attempts >= kHeadsetSvt40MaximumCycleAttempts) {
                bool unlogged = false;
                if (g_headset_svt40_selection_exhausted_logged
                        .compare_exchange_strong(
                            unlogged, true, std::memory_order_acq_rel,
                            std::memory_order_acquire)) {
                    append_simulator_kar98_probe(
                        "svt40-selection-cycle-exhausted");
                    WAWVR_STEREO_DIAG_ONCE(
                        "SimulatorDiag SVT-40 selector exhausted its bounded weapon-cycle attempts without observing the exact target identity");
                }
            }
        }
    }

    if ((simulator_weapon ==
             SimulatorBoltActionWeapon::HeadsetM1GarandGrenadeLauncher ||
         simulator_weapon ==
             SimulatorBoltActionWeapon::M1GarandGrenadeLauncher) &&
        g_simulator_weapon_equip_phase.load(std::memory_order_acquire) ==
            SimulatorWeaponEquipPhase::Complete) {
        const HeadsetM1GarandGlSelectionState selection =
            headset_m1garand_gl_selection_state();
        if (selection ==
            HeadsetM1GarandGlSelectionState::m1garand_gl_selected) {
            bool unlogged = false;
            if (g_headset_m1garand_gl_selection_confirmed_logged
                    .compare_exchange_strong(
                        unlogged, true, std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                append_simulator_kar98_probe(
                    "m1garand-gl-selection-confirmed");
                WAWVR_STEREO_DIAG_ONCE(
                    "SimulatorDiag exact M1 Garand rifle-grenade selection confirmed from the rendered held-weapon identity");
            }
        } else {
            const std::uint64_t not_before =
                g_headset_m1garand_gl_selection_not_before.load(
                    std::memory_order_acquire);
            const std::uint64_t next_cycle_at =
                g_headset_m1garand_gl_next_cycle_at.load(
                    std::memory_order_acquire);
            const std::uint32_t attempts =
                g_headset_m1garand_gl_cycle_attempts.load(
                    std::memory_order_acquire);
            if (not_before != 0 && now_milliseconds >= not_before &&
                now_milliseconds >= next_cycle_at &&
                attempts < kHeadsetM1GarandGlMaximumCycleAttempts) {
                const bool pair_installed =
                    g_headset_m1garand_gl_pair_installed.load(
                        std::memory_order_acquire);
                g_headset_m1garand_gl_cycle_attempts.fetch_add(
                    1, std::memory_order_acq_rel);
                g_headset_m1garand_gl_next_cycle_at.store(
                    now_milliseconds +
                        kHeadsetM1GarandGlCycleCooldownMilliseconds,
                    std::memory_order_release);
                if (pair_installed) {
                    // Do not rewrite ps->weapon before every selection attempt.
                    // That erased the native old/new weapon edge and left the
                    // script-owned rifle without a rebuilt held DObj.  Once the
                    // pair is installed, bounded native cycles alternate the
                    // two owned definitions and create the same selection edge
                    // as an authored campaign pickup.
                    if (queue_console_command(
                            cbuf_add_text, kWeaponNextCommand)) {
                        append_simulator_kar98_probe(
                            "m1garand-gl-selection-cycle-queued");
                    }
                } else {
                    const HeadsetM1GarandGlDirectEquipResult equip_result =
                        directly_equip_headset_m1garand_gl();
                    if (equip_result ==
                        HeadsetM1GarandGlDirectEquipResult::applied) {
                        g_headset_m1garand_gl_pair_installed.store(
                            true, std::memory_order_release);
                        // Commit the selection during this same service frame.
                        // A queued Cbuf command runs after the next snapshot
                        // has already restored the prior inventory and loses
                        // the edge that constructs the held viewmodel.
                        const std::uint32_t rifle_index =
                            g_headset_m1garand_gl_rifle_index.load(
                                std::memory_order_acquire);
                        const bool selection_committed =
                            select_headset_test_weapon_index(rifle_index);
                        append_simulator_kar98_probe(
                            "m1garand-gl-direct-equip-applied");
                        append_simulator_kar98_probe(
                            selection_committed ?
                                "m1garand-gl-initial-selection-committed" :
                                "m1garand-gl-initial-selection-unavailable");
                        WAWVR_STEREO_DIAG_ONCE(
                            "SimulatorDiag installed the exact loaded m1garand_gl and m7_launcher pair into the disposable test inventory");
                    } else if (
                        equip_result == HeadsetM1GarandGlDirectEquipResult::
                                            target_not_loaded) {
                        append_simulator_kar98_probe(
                            "m1garand-gl-direct-equip-target-not-loaded");
                    } else {
                        append_simulator_kar98_probe(
                            "m1garand-gl-direct-equip-player-unavailable");
                    }
                }
            } else if (
                attempts >= kHeadsetM1GarandGlMaximumCycleAttempts) {
                bool unlogged = false;
                if (g_headset_m1garand_gl_selection_exhausted_logged
                        .compare_exchange_strong(
                            unlogged, true, std::memory_order_acq_rel,
                            std::memory_order_acquire)) {
                    append_simulator_kar98_probe(
                        "m1garand-gl-direct-equip-exhausted");
                    WAWVR_STEREO_DIAG_ONCE(
                        "SimulatorDiag M1 Garand rifle-grenade direct test equip exhausted its bounded attempts without observing the exact target identity");
                }
            }
        }
    }

    // Use conservative virtual dimensions until the first captured T4
    // dimensions are available. Normal operation replaces these after one XR
    // frame.
    if (full_backbuffer_width == 0) {
        full_backbuffer_width = 640;
    }
    if (full_backbuffer_height == 0) {
        full_backbuffer_height = 480;
    }
    const auto& left = frame.actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Left)];
    const auto& right = frame.actions.hands[
        static_cast<std::uint32_t>(wawvr::xr::Hand::Right)];
    const bool focused = frame.actions.focused;
    // Mission-only bindings are authorized by the fully validated executable
    // profile. Connection state 10 is shared by SP and MP and is therefore not
    // an executable-identity discriminator.
    const bool single_player_profile =
        g_single_player_profile.load(std::memory_order_acquire);
    const bool gameplay_input_owned =
        focused && controller_gameplay_input_allowed(
                       presentation.key_catchers,
                       presentation.connection_state,
                       presentation.active_connection_state);
    const bool tank_controls = single_player_profile && controller_tank_controls_active();
    const bool aircraft_controls = single_player_profile && controller_aircraft_controls_active();
    const NativeGameplayCommandUpdate native_commands =
        update_native_gameplay_commands(
            {
                .input_owned = gameplay_input_owned && !tank_controls,
                .single_player_profile = single_player_profile && !aircraft_controls,
                .weapon_next_held = bool_held(left.secondary),
                .mission_stick_valid = left.stick.active,
                .mission_stick_x = left.stick.current.x,
                .mission_stick_y = left.stick.current.y,
            },
            &state->native_commands);
    const DirectionalActionState directional_state_before =
        state->directional_actions;
    const DirectionalActionUpdate directional_action =
        update_directional_actions(
            gameplay_input_owned && !tank_controls && !aircraft_controls,
            frame.actions.sequence,
            right.stick,
            &state->directional_actions);
    result.stance_action = directional_action.action;
    if (directional_action.action != DirectionalAction::none) {
        const std::string_view command =
            directional_action_console_command(directional_action.action);
        if (!command.empty() &&
            queue_console_command(cbuf_add_text, command.data())) {
            result.stance_command_queued = true;
            stereo_diagnostic_log(
                "InputDiag right-stick stance ladder queued native %s action",
                directional_action_name(directional_action.action));
        } else {
            // Consume this deflection but do not advance the logical stance
            // when the exact native command capability was unavailable.
            state->directional_actions.stance =
                directional_state_before.stance;
        }
    }
    if (native_commands.weapon_next_tap) {
        const M1GarandGlToggleResult garand_toggle =
            toggle_m1garand_gl_alternate();
        if (garand_toggle ==
                M1GarandGlToggleResult::rifle_to_launcher ||
            garand_toggle ==
                M1GarandGlToggleResult::launcher_to_rifle) {
            result.weapon_next_queued = true;
            stereo_diagnostic_log(
                "InputDiag plain Y toggled exact M1 Garand rifle-grenade pair from %s to %s",
                garand_toggle ==
                        M1GarandGlToggleResult::rifle_to_launcher
                    ? "m1garand_gl"
                    : "m7_launcher",
                garand_toggle ==
                        M1GarandGlToggleResult::rifle_to_launcher
                    ? "m7_launcher"
                    : "m1garand_gl");
        } else if (queue_console_command(
                       cbuf_add_text, kWeaponNextCommand)) {
            // If the exact pair is not loaded or its native selector is not
            // available, retain stock Y behavior rather than swallowing input.
            result.weapon_next_queued = true;
            if (garand_toggle == M1GarandGlToggleResult::unavailable) {
                stereo_diagnostic_log(
                    "InputDiag exact M1 Garand rifle-grenade toggle unavailable; plain Y fell back to native weapnext");
            }
        }
    }
    if (native_commands.mission_key_tap != 0) {
        send_key_tap(
            key_event, native_commands.mission_key_tap, now_milliseconds);
        result.mission_key_tapped = native_commands.mission_key_tap;
        if (native_commands.mission_key_tap == kMissionDpadLeftKey) {
            WAWVR_STEREO_DIAG_ONCE(
                "CampaignDiag rocket-barrage selection emitted: nativeKey=%d modifier=left-secondary exactSpProfile=1",
                native_commands.mission_key_tap);
        }
    }
    const MenuCursorRegion current_cursor_region = cursor_region(
        full_backbuffer_width, full_backbuffer_height,
        active_gameplay_menu, active_ui_source);
    MenuCursorRegion submitted_cursor_region = current_cursor_region;
    MenuPointerHit pointer_hit{};
    const bool submitted_ui_surface_is_current =
        result.ui_active && menu_pointer_surface_allows_interaction(
            visible_menu_surface, presentation.connection_state,
            presentation.key_catchers, focused,
            menu_escape_tap_requested);
    if (submitted_ui_surface_is_current) {
        pointer_hit = point_at_world_menu_panel(
            right.aim, visible_menu_surface->panel);
        if (pointer_hit.valid) {
            pointer_hit = remap_menu_pointer_to_content(
                pointer_hit, visible_menu_surface->content_viewport);
        }
        if (pointer_hit.valid) {
            submitted_cursor_region =
                visible_menu_surface->cursor_region;
        }
    }
    const bool pointer_trigger_held = update_menu_pointer_trigger(
        focused,
        right.trigger_click.active,
        right.trigger_click.current,
        right.trigger.active,
        right.trigger.current,
        &state->pointer_trigger);
    const MenuNavigationUpdate update = update_menu_navigation(
        {
            // Native stick/A/B fallback follows the freshly read UI catcher
            // even while a captured surface is temporarily unavailable. The
            // exact surface gate remains mandatory for ray position and
            // trigger confirmation, so a stale or invisible panel can never
            // receive a pointer click. Edge latches still prevent a held
            // button from repeating across a synchronous page change.
            .ui_active = result.ui_active,
            .input_focused = focused,
            .active_gameplay_menu = active_gameplay_menu,
            .stick_valid = focused && left.stick.active,
            .stick_x = left.stick.current.x,
            .stick_y = left.stick.current.y,
            .confirm_held = focused && bool_held(right.primary),
            .pointer_valid = pointer_hit.valid,
            .pointer_u = pointer_hit.u,
            .pointer_v = pointer_hit.v,
            .pointer_confirm_held = pointer_trigger_held,
            // If OpenXR Menu is unavailable, Present owns B as the complete
            // pause/recenter gesture. Feeding the same press into navigation
            // would dispatch Escape on press and again on gesture release,
            // immediately reopening the pause menu.
            .back_held = focused && frame.actions.menu.active &&
                bool_held(right.secondary),
            .now_milliseconds = now_milliseconds,
            .region = submitted_cursor_region,
        },
        &state->navigation);

    if (update.cursor_position_valid) {
        mouse_event(update.cursor_x, update.cursor_y);
        result.cursor_submitted = true;
        result.pointer_submitted = update.pointer_position_used;
    }
    if (update.back_tap && !menu_escape_tap_requested) {
        send_key_tap(key_event, kT4KeyEscape, now_milliseconds);
        result.back_tapped = true;
    } else if (update.confirm_tap) {
        // Defense in depth: even if the pure navigation policy regresses,
        // never dispatch two synchronous native page mutations in one call.
        const NativeMenuConfirmKey confirm_key =
            choose_native_menu_confirm_key(
                g_multiplayer_profile.load(std::memory_order_acquire),
                active_gameplay_menu,
                result.ui_active);
        result.confirm_via_enter =
            confirm_key == NativeMenuConfirmKey::enter;
        send_key_tap(
            key_event,
            result.confirm_via_enter ? kT4KeyEnter : kT4KeyMouse1,
            now_milliseconds);
        result.confirm_tapped = true;
        result.pointer_confirm_tapped = update.pointer_confirm_tap;
    }
    return result;
}

}  // namespace wawvr::mod
