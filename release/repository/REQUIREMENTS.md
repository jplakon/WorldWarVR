# Requirements

## Required

- **Operating system:** 64-bit Windows 10 version 2004 (build 19041) or newer, or Windows 11
- **Game:** a legitimate, compatible Windows installation of Call of Duty: World at War updated to version 1.7
- **VR:** an OpenXR-compatible PC VR headset with its active OpenXR runtime
- **Input:** two tracked motion controllers for the complete control scheme
- **Graphics:** a DirectX 11-capable VR system

The launcher detects a normal Steam installation when possible. If detection fails, select **Browse** and choose the folder containing `CoDWaW.exe`. If Browse fails, paste the installation path into the existing path box. Modified, mismatched, or unrecognized game executables may be rejected; language detection does not establish regional/uncut executable compatibility.

World War VR does not include the game executable, maps, archives, audio, video, or other game assets.

## Performance

VR performance depends on GPU, headset resolution, connection method, runtime reprojection, and map load. Use the presets in this order:

| Preset | Packed size | Per-eye view | Guidance |
| --- | ---: | ---: | --- |
| Performance | 3744 x 2016 | 1872 x 2016 | Recommended starting point |
| Native | 6016 x 2688 | 2496 x 2688, plus a 1024 x 1024 scope view | Use after Performance is stable |
| Recovery | 2560 x 1440 | 1280 x 1440 | Use for black screens, stutter, or limited GPU headroom |

A precise minimum CPU/GPU specification has not yet been established across enough systems. Bug reports should include CPU, GPU, RAM, driver, headset, connection/OpenXR runtime, and preset.

## Tested runtimes and connections

- Quest 2 and Quest 3 through Virtual Desktop are working test paths.
- Quest 2 Air Link is working through the launcher's **Quest Air Link compatibility** option with SteamVR as the active OpenXR runtime; the tested configuration used SteamVR Beta.
- Quest 3 Link/Air Link through the same SteamVR compatibility path received physical-headset acceptance in beta.2 for upright stereo, correct head/controller tracking, weapon motion, and pause/resume.
- Quest 2 through Steam Link is not supported.
- Rift S has previously been tested through Meta Horizon Link and SteamVR; a newer-build aiming regression remains under investigation.
- Index no-headset-image startup reports remain unresolved.
- Some Pimax working sessions have been reported, but 32-bit runtime discovery and affected-user compatibility retests remain open.

These are observations from tested systems, not a guarantee that every software or driver version will behave identically.

World at War is a 32-bit process and requires a usable 32-bit OpenXR runtime.
The compatibility switch selects SteamVR's x86 route. Leave it off for a
native Pimax runtime test or normal Virtual Desktop operation; a working
64-bit VR application alone does not establish this game's runtime compatibility.

## Optional bots

Bots are optional and intended only for local/offline Multiplayer. World War VR does not include or download the PeZBOT archive.

To use them, download [PeZBOT 005p for World at War](https://www.moddb.com/mods/pezbot/downloads/pezbot-005p-for-world-at-war), leave the exact `PeZBOTWAW_005p.zip` archive unextracted in the current user's Downloads folder, enable **Automatic Bots**, and launch Multiplayer. World War VR verifies the archive and imports it into a private multiplayer profile. Do not extract it into the Call of Duty: World at War folder.

When creating the local game, keep **Dedicated** set to **No** so the headset player can join. See the repository's offline Multiplayer guide for the accepted archive identity and recovery steps.

## Not supported

- Online Multiplayer and public or protected servers
- Non-Windows game builds
- Modified or unrecognized game executables
- Headsets without a working PC OpenXR runtime
- Quest 2 through Steam Link
