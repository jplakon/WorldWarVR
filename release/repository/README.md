# World War VR

World War VR is a pre-release OpenXR mod for the Windows retail version of **Call of Duty: World at War**. It adds stereo rendering, 6DOF headset tracking, two tracked hands, motion-controller firearms, manual reload interactions, physical scopes and grenades, comfort controls, and a standalone launcher.

> **Beta status:** The creator completed the Campaign in VR during development leading to beta.1 using evolving builds. Beta.2 and beta.3 received focused physical-headset tests, not another full Campaign replay on the final beta.3 binary. Zombies remains a primary test target. Local/offline Multiplayer remains experimental, and Online Multiplayer is unsupported.

World War VR does not include Call of Duty: World at War files. You must provide your own legitimate, compatible World at War 1.7 installation.

## Download and install

Download a free pre-release from [GitHub Releases](https://github.com/jplakon/WorldWarVR/releases). No subscription is required. The release provides:

```text
WorldWarVR-v<version>-Portable.zip
WorldWarVR-v<version>-Source.zip
SHA256SUMS.txt
GPL-SOURCE-NOTICE.txt
```

The Portable ZIP is the player package; extract it into a new, empty folder,
not into the World at War folder. Keep every extracted file together. Verify
the download against SHA256SUMS.txt or its adjacent `.zip.sha256` sidecar.
The Source ZIP contains complete matching GPL-3.0-only source and pinned
dependencies; it is optional to play. GitHub's automatically generated
source-code download is not a playable package. No installer is included.
Updates are manual: download a new release into a fresh folder rather than
mixing versions. The launcher does not automatically fetch GitHub updates.

After installation:

1. Connect the headset and controllers and start the intended OpenXR runtime.
2. Open **World War VR**.
3. Let the launcher find World at War, or browse to the folder containing `CoDWaW.exe`.
4. Choose **Main Menu** for Zombies/Campaign or **Multiplayer** for experimental local/offline play.
5. Start with **Performance**, keep **Launch in VR** enabled, and select **Launch**.

The launcher's **Zombies & Campaign Controls** section also offers **Automatic Reload** and
**Button Grenades**. Both are optional and disabled by default. Automatic
Reload makes A perform WaW's complete native reload; Button Grenades makes the
left index trigger use WaW's conventional frag-grenade control without a belt
reach. These two options apply to Zombies and Campaign.

## Quality presets

| Preset | Packed size | Per-eye view | Purpose |
| --- | ---: | ---: | --- |
| Native | 6016 x 2688 | 2496 x 2688, plus a 1024 x 1024 physical-scope view | Highest clarity; demanding |
| Performance | 3744 x 2016 | 1872 x 2016 | Recommended starting point |
| Recovery | 2560 x 1440 | 1280 x 1440 | Black-screen and performance recovery |

## Tested PC VR paths

| Headset | Connection | Status |
| --- | --- | --- |
| Quest 3 | Virtual Desktop | Working |
| Quest 2 | Virtual Desktop | Working |
| Quest 2 | Air Link through the launcher SteamVR compatibility option | Working in the tested SteamVR Beta configuration |
| Quest 3 | Link/Air Link through the launcher SteamVR compatibility option | Working in the physically tested SteamVR configuration |
| Quest 2 | Steam Link | Not supported |
| Rift S | Meta Horizon Link / SteamVR | Previously tested; a newer-build aiming regression is under investigation |
| Valve Index | SteamVR | Audio/no-headset-image startup reports remain unresolved |
| Pimax | Native OpenXR / SteamVR | Some working sessions reported; runtime discovery and affected-user retests remain open |

Other OpenXR headsets may work but have not all been physically verified. Always include the connection method and active OpenXR runtime in a bug report.

WaW needs a usable 32-bit OpenXR runtime. **Quest Air Link compatibility**
selects SteamVR's x86 route; leave it off when testing a native Pimax runtime
or the normal Virtual Desktop route. It is not a universal Index/Pimax fix.

## Mode status

| Mode | Status |
| --- | --- |
| Zombies | Primary test target |
| Campaign | Completed in VR during beta development; mission and hardware coverage can still vary |
| Local/offline Multiplayer | Experimental; some maps and modes can fail |
| Online Multiplayer | Unsupported; do not use on public or protected servers |

Optional local Multiplayer bots require the exact user-supplied PeZBOT archive documented by the project. The archive and extracted bot files are not distributed with World War VR.

## Main controls

- Right grip draws and retains the firearm; left grip adds the support hand and firing/ADS pose.
- Right trigger fires; the visible barrel controls shot direction.
- Either hand can retain a supported weapon while the other hand's index trigger operates its bolt or charging handle.
- A begins the weapon's manual reload sequence.
- Left index trigger at the left hip grabs a frag grenade; at the right hip it grabs the secondary tactical grenade. Release to throw or drop it.
- Left stick moves; clicking it latches sprint until the stick returns to neutral.
- Right stick left/right snap-turns. Down steps stand to crouch to prone; up rises one step or jumps while standing.
- X interacts; Y switches weapon or toggles the Garand/M7 pair's grenade mode.
- Right-stick click, a supported free-hand strike, held-pistol jab, or deliberate two-hand rifle thrust melees.
- Tap left Menu for the native menu; hold it for one second to recenter. On the SteamVR fallback path, B performs those actions; right-stick down remains the stance control.

If **Automatic Reload** is enabled, A completes the normal game reload instead
of starting the physical reload sequence. If **Button Grenades** is enabled,
the left index trigger uses the conventional frag-grenade button from any hand
position instead of the physical belt-grab and motion-throw interaction.

See [CONTROLS.md](CONTROLS.md), [REQUIREMENTS.md](REQUIREMENTS.md), and [KNOWN-ISSUES.md](KNOWN-ISSUES.md) before testing.

The first free GitHub release retains the beta.3 binaries. Beta.3 includes
visible-barrel aiming, mounted-gun input ownership, scoped-Mosin visibility
and optical-zero fixes, scope-active HUD, Garand/M7 rifle-mode manual reload,
melee safeguards, and Browse recovery. It also includes the earlier aircraft,
satchel, airstrike, tank, pistol, and firing-vibration features. The M7 grenade
mode intentionally retains native reload. Free distribution does not establish
additional gameplay fixes or new hardware compatibility.
The Source ZIP's root README includes native rebuild instructions; rebuilding
is optional and is not needed to play.

## Reporting a problem

Report problems through [GitHub Issues](https://github.com/jplakon/WorldWarVR/issues). Include the exact release filename, mode/map/weapon,
headset/controllers, connection and OpenXR runtime, PC and Windows version,
quality preset, reproducible steps, and `WorldWarVR.log` if generated.

Remove personal information from public logs. Do not upload game executables,
proprietary map files, keys, or saves. The mod is GPL-3.0-only, with matching
source provided without an additional fee. Copyright (c) 2026 Ryan Craighead;
modifications copyright (c) 2026 John Plakon and contributors. Preserve the
packaged LICENSE, PROVENANCE.md, and THIRD-PARTY-NOTICES.md.

World War VR is an independent fan-made project and is not affiliated with, endorsed by, or sponsored by Activision. Call of Duty and Call of Duty: World at War are trademarks of Activision Publishing, Inc.
