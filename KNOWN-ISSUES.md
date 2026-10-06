# Known issues

World War VR is beta software. The creator completed the Campaign in VR during development leading to beta.1. Beta.2's SteamVR compatibility changes and beta.3's weapon, mounted-gun, and scope fixes received focused headset tests rather than another complete Campaign replay on the final release. Local/offline Multiplayer remains experimental. Save progress and expect compatibility or presentation issues.

## Modes

- **Zombies:** primary test target. Individual maps, scripted sequences, upgraded weapons, and uncommon weapon variants have not all received the same physical-headset coverage.
- **Campaign:** completed in VR on the creator's setup. Different mission states, scripted moments, menus, cutscenes, and progression triggers can still expose issues; completion does not establish every weapon variant or headset configuration.
- **Local/offline Multiplayer:** experimental. Some maps or modes may fail to start. Optional PeZBOT setup has no in-launcher status display.
- **Online Multiplayer:** unsupported. Do not use World War VR on public or protected servers.

## Headsets and runtimes

- Quest 2 through Steam Link is not a supported configuration.
- Quest Link/Air Link uses the launcher's **Quest Air Link compatibility** option and the 32-bit SteamVR OpenXR path. Beta.2 received Quest 3 physical-headset acceptance for upright stereo, tracking, weapon motion, and pause/resume on the tested SteamVR configuration; behavior can differ between SteamVR releases.
- **Quest 3 through Meta Quest Link:** an earlier tester reported startup/menu crashes, then a successful short SteamVR session. Later focused Quest 3 testing accepted the SteamVR compatibility path described above, but it does not establish reliable operation on every Meta/SteamVR version. Virtual Desktop is also a locally verified configuration.
- **Pimax Crystal Light:** earlier desktop-only output was linked to 32-bit OpenXR runtime discovery. The tester subsequently reported gameplay and weapon/rendering issues from working sessions. The latest fixes still require that tester's hardware retest; compatibility across Pimax runtimes and versions is not fully established.
- **Valve Index / SteamVR:** audio with no headset image remains an open startup report. Supplied evidence includes OpenXR instance creation failing before any frames. An orientation or render-quality change is not a confirmed fix for this startup failure.
- **Rift S:** Meta Horizon Link and SteamVR have previously been tested. A report that newer builds shoot above the sights while an older build works remains under investigation; beta.3's local aim tests do not establish acceptance on this affected headset.
- A computer that previously worked but now launches only flat needs a fresh startup log and the exact runtime/build. This regression is separate from the Index and Pimax reports.
- A runtime or headset restart can leave an already-running game black in the headset. Close the game and launch it again after the connection is stable.
- Controllers that sleep while the game is running may not recover cleanly until the game or headset runtime is restarted.

## Rendering and performance

- **Native** is demanding: it renders two 2496 x 2688 eyes and a separate 1024 x 1024 physical-scope view inside a 6016 x 2688 packed surface.
- If the image is black, unstable, or too slow, try **Performance**, then **Recovery**.
- Dense encounters, overlays, recording software, wireless-streaming conditions, and runtime reprojection can introduce stutter independently of the mod.
- Native game menus and cinematics are presented on a VR panel rather than reconstructed as spatial interfaces.
- The scoped-Mosin enemy-visibility, optical-zero, and scope-active HUD fixes
  passed local headset checks. A separate blank-scope report without an exact
  rifle/runtime identity still needs reporter confirmation. Hard Landing smoke
  flicker and pause-menu clipping reports also remain open.

## Motion controls and weapons

- Weapon and hand alignment is generalized across the arsenal, but uncommon and upgraded variants may still need per-weapon polish.
- Bolt-action and magazine-fed manual reload interactions depend on the weapon state. Follow the in-game reload state and complete the physical bolt, charging, clip, or magazine action before expecting the weapon to fire.
- Firing vibration is enabled in the right controller and has been checked on
  Quest 3 through Virtual Desktop. Other controllers/runtimes can feel different;
  there is no separate firing pulse in the left support hand.
- Campaign completion and targeted weapon tests do not cover every PTRS-41,
  Panzerschreck, tank, aircraft, or upgraded-weapon situation on every system.
- Shuri Castle airstrike targeting follows the right controller, but the
  mission still restricts valid strike areas and uses its authored bomb runs.
- Physical scopes require the scope to be brought to the eye; they are not a full-screen native scope overlay.
- The rifle-grenade Garand uses manual en-bloc reloads in rifle mode. Press Y
  to toggle its M7 grenade-launcher mode, which retains native reload behavior.
- Physical melee has additional tracking/grip safeguards, and a held-pistol
  forward jab passed a local headset test. Original customer reports of random
  melee still need retesting before they can be considered resolved.
- Specific bolt-rifle clip-insertion jumps, first-launch automatic-reload
  setting inconsistencies, pistol support-hand placement, and missing manual
  reload sound/feedback remain open reports. Report the exact weapon, map,
  grip/action sequence, launcher settings, and build rather than assuming the
  earlier general weapon-stability fix covers every interaction.

## Game and third-party files

World War VR releases do not include Call of Duty: World at War files or PeZBOT. You must provide a compatible game installation and any optional bot package yourself.

Custom Zombies DirectX crash reports require the affected maps and runtime
configuration to reproduce. Compatibility with every custom map is not
established. Regional/uncut executable reports also require validation of the
exact legitimate executable before additional address profiles can be added.

If a problem is not covered here, report it through
[GitHub Issues](https://github.com/jplakon/WorldWarVR/issues). Include the exact
release filename, headset, connection/OpenXR runtime, quality preset,
map/mission/weapon, reproduction steps, and a fresh `WorldWarVR.log` if available.
Remove personal information before posting logs publicly; do not upload game
executables, map files, keys, or campaign saves.
