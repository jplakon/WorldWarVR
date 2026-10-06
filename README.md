# World War VR

World War VR brings tracked, room-scale OpenXR play to the Windows retail version of **Call of Duty: World at War**. It provides stereo rendering, 6DOF headset tracking, motion-controller weapons, two-hand handling, manual reload interactions, physical scopes, grenades, and VR-native comfort controls through a standalone launcher.

The first free GitHub release is **v0.4.0-beta.3**, retaining the existing
beta.3 binaries. It includes the weapon-aiming, mounted-gun, scoped-Mosin,
scope-HUD, Garand/M7, melee-safeguard, and launcher Browse improvements described
in [CHANGELOG.md](CHANGELOG.md). Making the release free does not introduce
additional gameplay fixes or establish new headset compatibility.

> **Beta status:** The creator completed the Campaign in VR during development leading to beta.1. Later releases received focused physical-headset tests, not another full Campaign replay on the final beta.3 binary. Zombies remains a primary test target. Local/offline Multiplayer remains experimental, and Online Multiplayer is unsupported.

World War VR is a fan-made open-source project. It does not include Call of Duty: World at War executables, maps, textures, audio, or other Activision game content. You need your own legitimate, compatible World at War 1.7 installation.

## Free download

Download the playable package from [GitHub Releases](https://github.com/jplakon/WorldWarVR/releases). No subscription is required. Each release provides:

- `WorldWarVR-v<version>-Portable.zip` — the Windows player package
- `WorldWarVR-v<version>-Source.zip` — complete matching source, including pinned dependencies
- `SHA256SUMS.txt` and adjacent `.zip.sha256` sidecars — release checksums
- `GPL-SOURCE-NOTICE.txt` — matching-source and license information

Use the Portable ZIP to play; GitHub's automatically generated source-code
downloads are not the playable package. The release is portable and does not
include an installer. Updates are manual: download a new version into a new
folder. The launcher does not automatically fetch GitHub updates.

World War VR is GPL-3.0-only software. Recipients retain the rights to inspect,
modify, and redistribute the GPL-covered work. Complete matching source is
available alongside every binary release without an additional fee.

See [INSTALL.md](INSTALL.md) for prerequisites, checksum verification, portable
installation, OpenXR runtime notes, and troubleshooting.

## Quick start

1. Download the Portable ZIP and its release checksums.
2. Verify and extract `WorldWarVR-v<version>-Portable.zip` into a new, empty folder, not the World at War installation folder.
3. Connect the headset and controllers and start the intended OpenXR runtime.
4. Run `WorldWarVR.exe` from the extracted portable folder.
5. Let the launcher find World at War, or browse to the folder containing `CoDWaW.exe`.
6. Choose **Main Menu** for Zombies/Campaign or **Multiplayer** for experimental local/offline play.
7. Start with **Performance**, leave **Launch in VR** enabled, and select **Launch**.

## Quality presets

| Preset | Packed render size | Per-eye view | Purpose |
| --- | ---: | ---: | --- |
| Native | 6016 x 2688 | 2496 x 2688, plus a 1024 x 1024 physical-scope view | Highest clarity; demanding |
| Performance | 3744 x 2016 | 1872 x 2016 | Recommended starting point |
| Recovery | 2560 x 1440 | 1280 x 1440 | Compatibility and performance recovery |

Settings are saved automatically. Under **Zombies & Campaign Controls**, players can opt
into **Automatic Reload** (A performs WaW's complete native reload) and
**Button Grenades** (the left index trigger uses WaW's conventional frag
grenade control without a belt reach). Both options are disabled by default,
so the physical reload and grenade interactions remain the standard controls.
Every standard campaign mission is available in Mission Select whenever World
War VR starts the single-player game. The launcher advances the active
profile's stock `mis_01` mission-progression counter to WaW's unlock-all value;
it does not enable developer mode, replace save files, or alter saved mission
difficulty records.
For Quest Air Link, the launcher also offers a compatibility option for the
32-bit SteamVR OpenXR path. Quest 3 physical-headset testing confirmed upright
stereo, correct head/controller motion, weapon tracking, and pause/resume on
the tested SteamVR configuration.

## Mode status

| Mode | Status |
| --- | --- |
| Zombies | Primary test target |
| Campaign | Completed in VR during beta development; mission and hardware coverage can still vary |
| Local/offline Multiplayer | Experimental; some maps and modes can fail |
| Online Multiplayer | Unsupported; do not use on public or protected servers |

Optional local Multiplayer bots require a user-supplied PeZBOT package. PeZBOT is not included. See [docs/OFFLINE_MULTIPLAYER.md](docs/OFFLINE_MULTIPLAYER.md).

## Tested PC VR paths

| Headset | Connection | Current status |
| --- | --- | --- |
| Quest 3 | Virtual Desktop | Working |
| Quest 2 | Virtual Desktop | Working |
| Quest 2 | Air Link through the launcher's SteamVR compatibility path | Working in the tested SteamVR Beta configuration |
| Quest 3 | Link/Air Link through the launcher's SteamVR compatibility path | Working in the physically tested SteamVR configuration |
| Quest 2 | Steam Link | Not supported |
| Rift S | Meta Horizon Link / SteamVR | Previously tested; a newer-build aiming regression remains under investigation |
| Valve Index | SteamVR | Audio/no-headset-image startup reports remain unresolved |
| Pimax | Native OpenXR / SteamVR | Some working sessions reported; runtime discovery and affected-user retests remain open |

These are tested combinations, not a complete compatibility list. Runtime, driver, headset-software, and wireless-network changes can affect behavior.

WaW is a 32-bit game and needs a usable 32-bit OpenXR runtime. The compatibility
switch explicitly selects SteamVR's route; leave it off for a native Pimax
runtime test. It is not a universal Pimax or Index fix. See
[KNOWN-ISSUES.md](KNOWN-ISSUES.md) for current limitations.

## Controls

| Action | Control |
| --- | --- |
| Move | Left stick, relative to headset heading |
| Sprint | Click left stick; remains latched until the stick returns to neutral |
| Turn | Right stick left/right; one 45-degree snap per deflection by default, or proportional smooth turning when enabled in the launcher |
| Crouch / prone | Right stick down, one stance step per deflection |
| Rise / jump | Right stick up; rises one stance step, or jumps while standing |
| Draw and retain firearm | Right grip |
| Add support hand / enter firing pose | Left grip |
| Fire | Right index trigger |
| Aim/fire an ordinary mounted machine gun | Aim with the right controller; fire with the right index trigger |
| Operate bolt or charging handle | Retain the weapon with either hand; use the free hand's index trigger at the action |
| Begin manual reload | A |
| Use / interact | X |
| Switch weapon | Y; while holding the M1 Garand/M7 pair, Y toggles rifle-grenade mode on or off |
| Crouch | B; on SteamVR paths where the left Menu action is unavailable, use right-stick down because B becomes the pause/recenter fallback |
| Melee | Right-stick click, a fast outward free-right-hand swing, or a deliberate forward rifle thrust while both grips are held |
| Grab frag grenade | Left index trigger while the left hand is at the left hip |
| Grab tactical grenade | Left index trigger while the left hand is at the right hip |
| Throw grenade | Release the left index trigger |
| Menu | Tap the left Menu button; tap B on the SteamVR fallback path |
| Recenter | Hold the left Menu button for one second; hold B for one second on the SteamVR fallback path |

The barrel controls shot direction. Bolt-action and magazine-fed weapons use contextual physical cycling and reload interactions; either hand can retain a supported weapon while the other hand operates its bolt or charging handle. Ordinary mounted machine guns follow the right controller within the gun's native traverse limits while headset look remains independent; fixed scoped turrets retain their native controls. Physical scopes work by bringing the mounted scope to your eye. A deliberate two-hand rifle thrust invokes the weapon's native melee behavior, so a bayonet-equipped rifle uses its bayonet while another firearm uses its ordinary melee attack. Native menus and cinematics appear on a VR panel controlled by the right-controller pointer.

The Garand/M7 variant uses manual en-bloc reloads in rifle mode; its M7 grenade
mode intentionally retains native reload behavior.

When **Automatic Reload** is enabled in the launcher, A instead completes the
game's normal reload animation and no physical clip, magazine, bolt, or charging
action is required. When **Button Grenades** is enabled, the left index trigger
uses the game's conventional frag-grenade button from any hand position rather
than the physical hip-grab interaction.

Snap turning remains the default: each horizontal right-stick deflection turns
45 degrees and returning the stick to center rearms the next turn. Enable
**Smooth Turning** in the launcher for proportional continuous right-stick yaw
at up to 120 degrees per second. Right-stick up/down stance and jump gestures
are unchanged in either mode.

Read the full [controls guide](docs/CONTROLS.md) before testing manual weapons, stance controls, campaign support actions, or menus.

## Rebuild the supplied source

Clone the tagged repository, including its pinned OpenXR submodule:

```powershell
git clone --recurse-submodules --branch v0.4.0-beta.3 https://github.com/jplakon/WorldWarVR.git
cd WorldWarVR
```

Alternatively, extract the release's complete matching Source ZIP and open
PowerShell 7 in its source root. That archive includes the pinned OpenXR source;
GitHub's automatically generated tag archives do not include submodule contents.
Install Visual Studio 2022 with the C++ desktop workload, x86 build tools,
Windows SDK, and CMake tools. The release build script requires Visual
Studio's bundled CMake **3.31.6-msvc6** and checks that exact version; a
different CMake on PATH is not used. The complete pinned OpenXR source is
already included, so this native rebuild does not require Git metadata:

```powershell
.\scripts\build.ps1 -Configuration Release -BuildRoot .\rebuild-beta3
```

Use a fresh build folder. The script builds the Win32 native components and
runs their tests. The mod DLL is written to
`rebuild-beta3\vs-release\src\mod\Release\WorldWarVR.dll`.
To rebuild the separate graphical launcher, install the .NET 8 SDK and follow
the locked restore and publish commands in
[launcher publishing instructions](launcher-ui/WorldAtWarVR.Launcher/PUBLISHING.md).
The release packaging scripts require a Git checkout for provenance checks;
they cannot run directly from the metadata-free Source ZIP.

## Reporting bugs

Check [KNOWN-ISSUES.md](KNOWN-ISSUES.md), then report the problem through
[GitHub Issues](https://github.com/jplakon/WorldWarVR/issues). Reports are most
useful when they include the exact release, mode and map, weapon, headset and
controllers, connection/OpenXR runtime, quality preset, reproducible steps,
and `WorldWarVR.log` if generated.

Review logs and screenshots for personal information before posting publicly.
Do not upload proprietary game executables, map files, keys, or campaign saves.

## Source, license, and attribution

World War VR is distributed under the [GNU General Public License v3.0 only](LICENSE). Build provenance and third-party license information are in [PROVENANCE.md](PROVENANCE.md) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

Copyright (c) 2026 Ryan Craighead. Modifications copyright (c) 2026 John
Plakon and contributors. This version substantially modifies the upstream
runtime, launcher, rendering, input, interaction, packaging, and documentation
for the releases described in `CHANGELOG.md`, originally distributed through
Patreon and now also available free on GitHub.

This project builds on Ryan Craighead's World War VR work and uses the KisakCOD/John-deep COD4 VR project as an implementation and interaction reference. See the provenance documents for exact pinned revisions and component boundaries.

Call of Duty and Call of Duty: World at War are trademarks of Activision Publishing, Inc. This project is not affiliated with, endorsed by, or sponsored by Activision.
