# Known issues

World War VR is beta software. The creator completed the Campaign in VR during development leading to beta.1 using evolving builds. Later updates, including beta.4's chest and reload-setting work, received focused headset tests rather than another full Campaign replay on the final package. Local/offline Multiplayer remains experimental.

## Zombies

- Not every map state, upgraded weapon, rare weapon variant, headset, GPU, or runtime has received equal physical-headset testing.
- If rendering becomes unstable or performance falls, close the game and retry with **Performance**, then **Recovery**.

## Campaign

- Campaign completion covers the creator's playthrough and setup; other mission states, weapon variants, and hardware combinations can still expose issues.
- Scripted camera events, support prompts, cutscenes, and mission-specific interactions may behave incorrectly in VR or need keyboard input.
- Shuri Castle airstrike aiming follows the right controller, but valid strike areas and bomb runs remain controlled by the mission.

## Multiplayer

- Local/offline Multiplayer is experimental. Some maps or modes can fail to start or run correctly.
- Optional bots require the separately downloaded exact `PeZBOTWAW_005p.zip` archive. World War VR does not include it.
- The launcher does not currently show a bot-import status. If no bots appear, verify the archive name, restart the launcher, and keep **Dedicated** set to **No** when creating the local game.
- Online Multiplayer is unsupported and untested. Do not use World War VR on public or protected servers.

## Headsets and runtimes

- Quest 2 through Steam Link is not supported.
- Quest Link/Air Link uses the launcher's **Quest Air Link compatibility** option and the 32-bit SteamVR OpenXR path. Beta.2 received Quest 3 physical-headset acceptance for upright stereo, tracking, weapon motion, and pause/resume on the tested SteamVR configuration; other builds may differ.
- **Quest 3 through Meta Quest Link:** startup/menu crashes were followed by one successful short SteamVR session. Reliable compatibility is not established; Quest 3 through Virtual Desktop remains the verified configuration.
- **Pimax:** earlier desktop-only output involved 32-bit runtime discovery. Some working sessions were subsequently reported, but compatibility across runtimes and versions is not established and affected-user retests remain open. Native Pimax and forced SteamVR x86 are distinct runtime routes.
- **Valve Index / SteamVR:** audio with no headset image remains unresolved; supplied evidence includes OpenXR instance creation failing before frames. Image-orientation changes are not a confirmed fix for this startup failure.
- **Rift S:** previously tested through Meta Horizon Link and SteamVR. A newer-build above-sight aiming regression remains under investigation; creator headset checks do not establish acceptance on this affected configuration.
- Previously working installations that now launch flat need a fresh startup log and exact runtime/build. This regression is separate from the Index and Pimax reports.
- Restarting or disconnecting a headset while the game is running can leave the headset black. Close and relaunch the game after the runtime connection is stable.
- Controllers that sleep during a run may not recover until the game or headset runtime is restarted.

## Compatibility, presentation, and performance

- Only recognized compatible World at War 1.7 executables are accepted.
- **Native** is substantially more demanding than **Performance** and also renders a dedicated physical-scope view. Use **Recovery** for black-screen or low-headroom diagnosis.
- Native menus and cinematics are presented on a VR panel rather than rebuilt as spatial interfaces.
- Weapon alignment is generalized across the arsenal, but uncommon and upgraded variants may still need individual polish.
- Scoped-Mosin visibility, optical-zero, and scope-active HUD fixes passed
  local headset checks. An unidentified blank-scope report still needs affected
  user confirmation. Hard Landing smoke flicker and pause-panel clipping also
  remain open, distinct from the fixed left-eye-only smoke and B-toggle issues.
- Beta.4 hardens first-launch settings initialization and handoff. Local Garand
  manual/automatic and snap/smooth switching tests passed, but the original
  intermittent customer reload report was not reproduced and needs their retest.
- Chest placement uses a headset-based torso estimate, not a separate body tracker.
- Specific clip-insertion pose jumps, pistol support-hand placement, and missing manual reload
  sound/feedback remain open reports.
- Melee safeguards and held-pistol jabs passed local checks, but original
  unintended-melee reports still require affected-user retests.
- The Garand/M7 variant has manual en-bloc reloads in rifle mode. Tap Y to
  toggle grenade mode, which intentionally retains native reload behavior.
- Custom Zombies DirectX crash reports remain under investigation. Report one
  affected map, its version/download source, and a fresh error/log; universal
  custom-map compatibility is not established.
- Regional/uncut executables require validation of the exact legitimate binary.
  Do not bypass launcher validation or treat language detection as a binary fix.
- Right-controller firing vibration has been checked on Quest 3 through
  Virtual Desktop. Other controllers and runtimes may feel different.
- Campaign completion and targeted weapon tests do not cover every PTRS-41,
  Panzerschreck, tank, aircraft, or upgraded-weapon situation on every system.
- Dense encounters, recording tools, runtime reprojection, and wireless
  streaming conditions can still affect smoothness. The beta's campaign CPU
  improvements do not guarantee a particular frame rate or eliminate every hitch.
- A desktop-only view, duplicated mirror view in the headset, incoherent stereo, frozen head tracking, or a black headset with a moving desktop mirror is a fault. Include the exact launch sequence and runtime in the bug report.

World War VR packages do not contain game assets or optional PeZBOT files.
Report problems through [GitHub Issues](https://github.com/jplakon/WorldWarVR/issues)
with the exact release filename, headset/connection/OpenXR runtime, preset,
map/mission/weapon, reproduction steps, and a fresh WorldWarVR.log if generated.
Remove personal information before posting logs publicly. Do not upload game
executables, proprietary map files, keys, or saves.
