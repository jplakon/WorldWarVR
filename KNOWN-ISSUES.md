# Known issues

World War VR is beta software. The creator completed the Campaign in VR during development leading to beta.1. Subsequent changes received focused tests rather than another complete Campaign replay on the final beta.8 package. Local/offline Multiplayer remains experimental. Save progress and expect compatibility or presentation issues.

## Beta.8 acceptance scope

- The ammo-pouch reach/marker and expanded aiming build received positive local
  headset feedback. This is not an enumerated all-weapon, all-distance, scope or
  Quest 2 acceptance run, and affected reporters should retest their exact case.
- The retained gameplay DLL passed 114 native tests. Focused OpenXR Tester work
  correlated 203 native shot traces across 76 ordinary weapon/model pairs:
  68 iron-sight and 8 barrel-axis pairs. Barrel alignment is not an iron-sight
  zero; native recoil, spread and finite-distance parallax remain.
- Separate scope camera/lens policy was observed for 10 of 12 exact-model
  implementation profiles. That evidence is not authoritative scope-impact,
  near/mid/far-zero or physical-headset qualification. Base scoped Kar98k and
  scoped FG42 lack current policy observations; unusual and custom variants
  can still need individual work. Explosive/projectile weapons are separate.
- Personal right weapon-hand angles adjust fit, not sight/impact calibration;
  defaults remain zero. No automatic Quest 2 preset, positional adjustment or
  independent left-dominant calibration is provided.
- Low FPS/stutter remains open, enemy-grenade throwback remains disabled, and
  Regional Compatibility Test1's Campaign validation correction is not included.
- The accepted native and managed binaries are retained byte-for-byte. Internal
  launcher version/source labels can still say beta.7; use the beta.8 filenames,
  checksums and component map to identify the release.

## Beta.7 acceptance scope

- The reported jungle leaf/detail transition received a successful local headset
  check. Changes target audited grass, shrub and selected mangrove/birch profiles;
  they do not establish every foliage variant, map or viewing condition.
- Low FPS and stutter remain under investigation. Fuller distant foliage can
  be more demanding. This release does not contain the later private performance
  experiments or promise COD4 VR frame rates.
- These are historical beta.7 checks. Beta.8 adds the focused changes above;
  neither release has received another full Campaign or all-headset retest.
- Existing beta.6 control, weapon, runtime and regional limitations below remain.

## Beta.6 acceptance scope

- Historical beta.6 exact-profile aiming changes cover the Garand, unscoped Mosin and starting
  Nacht `zombie_colt`, not every pistol, upgraded variant or scope. Five corrected
  Colt native discharge/initial-hit samples followed the current sight geometry,
  including off-headset aiming and genuine two-hand mode; a later local headset
  check reported success. Static near/mid/far zero and every sight picture are
  not certified. Native recoil, spread and muzzle-to-sight parallax remain.
- Stick-click melee is disabled by default; deliberate physical attacks remain.
  Advanced opt-in `WAWVR_RIGHT_STICK_MELEE=1` is not a new launcher checkbox.
  Other hardware and affected-user physical-melee reports still need feedback.
- Sprint, reload, grip recovery and smoothing have focused Tester/headset work,
  not exhaustive coverage of all reload families, empty/tactical combinations,
  upgraded weapons, world-ammo pickups or motion-to-photon latency.
- Little Resistance's support objective completed through controllers in Tester.
  The mission still controls availability and valid targets; other support-tool
  missions are not all certified.
- Targeted VR hints and lower retrievable help are included, but literal or
  localized tutorials can retain keyboard text. Unknown/remapped bindings use
  generic labels. Holding/releasing Y/L-SECONDARY for help can still cycle the
  weapon; only a completed mission-direction chord consumes that release.
- The targeted Colt shared-surface missing-pieces defect is fixed; this is not a
  complete all-weapon/material/state/eye audit.
- Experimental practical enemy-grenade throwback remains unresolved and
  hard-disabled. Its new contextual shortcut is not enabled or advertised as
  playable. Existing normal grenade controls remain.
- The launcher adds beta.8's personal-angle controls while retaining its existing
  installation validation. Regional-Compatibility Test1's separate Campaign fix
  is **not included**. Keep a working Test1 folder intact and ask before replacing
  it. This release does not establish universal German/regional compatibility;
  do not replace game EXEs or buy another edition based on this update alone.

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
- **Valve Index / SteamVR:** a previously affected tester reported successful headset gameplay after targeted startup diagnostics. This does not certify every Index/runtime/layer configuration; new audio-only startups still require a fresh log. No universal runtime or graphics-injector cause is claimed.
- **Rift S:** Meta Horizon Link and SteamVR have previously been tested. A report that newer builds shoot above the sights while an older build works remains under investigation; beta.3's local aim tests do not establish acceptance on this affected headset.
- A computer that previously worked but now launches only flat needs a fresh startup log and the exact runtime/build. This regression is separate from the Index and Pimax reports.
- A runtime or headset restart can leave an already-running game black in the headset. Close the game and launch it again after the connection is stable.
- Controllers that sleep while the game is running may not recover cleanly until the game or headset runtime is restarted.

## Rendering and performance

- **Foliage visibility and detail:** beta.7 extends short grass/shrub cutoffs and
  retains fuller leaves/detail for selected audited profiles. The reported jungle
  transition passed a local headset check. Authored endpoints and ordinary
  visibility checks remain; other foliage transitions can still occur. This is
  not infinite draw distance, every-map coverage or a higher-FPS guarantee.
- **Native** is demanding: it renders two 2496 x 2688 eyes and a separate 1024 x 1024 physical-scope view inside a 6016 x 2688 packed surface.
- If the image is black, unstable, or too slow, try **Performance**, then **Recovery**.
- Dense encounters, overlays, recording software, wireless-streaming conditions, and runtime reprojection can introduce stutter independently of the mod.
- Native game menus and cinematics are presented on a VR panel rather than reconstructed as spatial interfaces.
- The scoped-Mosin enemy-visibility, optical-zero, and scope-active HUD fixes
  passed local headset checks. A separate blank-scope report without an exact
  rifle/runtime identity still needs reporter confirmation. Hard Landing smoke
  flicker remains open. Campaign pause-menu layout changes have simulator
  coverage, but the affected user's resolution/headset still needs a retest.

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
- Beta.4 hardens launcher initialization and gameplay-setting handoff; manual,
  snap/smooth, automatic and restored-manual Garand tests passed locally. The
  original intermittent first-launch automatic-reload report was not reproduced,
  so affected-user retesting is still required before declaring that report closed.
- Released weapons use an upright headset-based chest estimate, not a separate
  body tracker. Garand chest placement received local headset acceptance;
  different proportions and uncommon weapons may need further positioning polish.
- Mosin clip pickup and insertion stability received a local headset check.
  This does not establish every bolt-action variant or grip sequence; the
  original reporter should retest the exact sequence that jumped.
- Mechanical reload sound and vibration event changes have automated/simulator
  coverage. Actual controller strength and feel still need hardware feedback.
- The added trigger-release melee safeguard has simulator coverage; remaining
  real-world random-melee reports require affected-user confirmation.
- Close pistol support and grip recovery have focused improvements; uncommon
  proportions/variants still need reporting. Report the exact weapon, map, grip/action
  sequence, settings and build for remaining interaction problems.

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
