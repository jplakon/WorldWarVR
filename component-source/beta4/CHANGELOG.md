# Changelog

All notable public changes to World War VR are recorded here.

The project follows semantic versioning where practical. Versions with an `alpha`, `beta`, or release-candidate suffix are pre-release builds and can still change incompatibly.

## v0.4.0-beta.4

Chest holstering and reload-setting reliability update.

- Released weapons now sit close to an upright chest anchor that follows
  tracked headset position and horizontal direction, rather than an old native
  body-camera direction. Leaning, crouching and turning keep the weapon with
  the player; head pitch/roll do not tilt the holstered weapon. This is an
  HMD-based torso estimate, not full-body tracking. Held aiming remains separate.
- Prevented early launcher interactions from being overwritten while saved
  settings and game discovery are still loading. Initialization failures leave
  editing/launch disabled instead of saving partially initialized controls.
- Made child-process gameplay settings explicit and improved effective
  reload-setting diagnostics.
- Allowed retained manual-reload magazine assets to recover after delayed
  validation without recreating engine-owned objects.

The exact released gameplay DLL passed 72 native tests and focused Garand,
Carbine and Colt simulator checks. Chest placement received creator headset
acceptance on VirtualDesktopXR at 90 Hz. Reload work received five simulator
and five physical Garand cases covering manual reload, snap/smooth switching,
automatic reload and restoration to manual. Launcher tests and a fresh-settings
UI check also passed. One simulator screenshot receipt timed out; the repeat
coverage succeeded. These are focused tests, not a full beta.4 Campaign replay.

The original customer's intermittent first-launch automatic reload was not
reproduced; affected-user confirmation remains pending. Index/Pimax startup,
Rift S aiming, Custom Zombies and regional-executable reports are not declared
fixed. No rendering/performance change is included in this update.

## v0.4.0-beta.3

Weapon aiming, scopes, mounted-gun controls, and rifle-grenade Garand update.

- Made ordinary single-player firearm shots, predicted impacts, and tracers
  use the final visible barrel origin and direction. Focused headset tests
  covered the M1 Garand, unscoped Mosin-Nagant, and SVT-40 across grip modes
  and controller aim directions.
- Corrected the PPSh-41 and SVT-40's remaining sight offsets using their
  evaluated muzzle forward axes. Both received separate headset acceptance.
- Prevented headset/body-yaw catch-up and ordinary snap or smooth turning
  from redirecting a controller-aimed mounted gun. Right-controller aiming,
  right-trigger fire, and barrel-aligned impacts received headset acceptance.
- Fixed enemies and vehicles disappearing when two-hand gripping activates
  the scoped Mosin. The physical optic now isolates its camera's visibility
  state from the normal eye views, and its optical axis matches the shot ray.
- Restored the compass and ammunition HUD in both normal eyes while a physical
  scope is active, preserving the optic and surrounding world. Native timed
  HUD fading still applies.
- Added the exact Campaign rifle-grenade Garand variant to the eight-round
  manual en-bloc reload path. A plain Y tap while the Garand/M7 pair is active
  now toggles between rifle and grenade-launcher modes; other weapons retain
  ordinary next-weapon switching. Rifle reload, switching, and firing received
  headset acceptance. The M7 grenade mode retains its native reload behavior.
- Hardened physical melee against headset-only movement, controller tracking
  relocation, and grip transitions without lowering the deliberate-motion
  thresholds. Source-labelled diagnostics distinguish physical gestures,
  right-stick clicks, and melee already requested by the native game.
- Added a forward melee jab while a supported pistol stays in the right hand.
  Firing, reload interactions, and raising/sweeping the pistol remain excluded.
  The held-pistol gesture was confirmed working by the user in a physical
  headset through VirtualDesktopXR. Broader customer random-melee reports
  still require reporter retesting; simulator safeguards are not that sign-off.
- Made launcher folder-picker failures recoverable, with guidance to paste
  the installation folder into the existing path box.

These changes received focused automated/simulator checks and the physical
headset acceptance described above on the creator's Quest/Virtual Desktop
setup. The final beta.3 release has not received another complete Campaign
playthrough. Pimax and other customer-specific reports still need affected
user retests; the local scoped-Mosin result does not close every unidentified
blank-scope report. Custom Zombies crash reports remain under investigation.

## v0.4.0-beta.2

SteamVR orientation, tracking, and pause-control compatibility update.

- Fixed the VR world appearing upside down or backward through SteamVR by
  removing the runtime-specific 180-degree image flips and using SteamVR's
  stage reference space when it is available.
- Applied the same reference-space choice to both eyes, the headset, both
  controller grip/aim poses, the native-menu panel, and submitted projection
  layers. Head and hand motion now follow their real-world directions, and
  weapons retain normal 6DOF tracking on the tested SteamVR path.
- Added a conditional SteamVR controller fallback for runtimes that reserve
  the left Menu button for the system dashboard: tap B once to pause or resume,
  and hold B for one second to recenter. B's ordinary crouch action is disabled
  only while this fallback is required; right-stick stance controls remain
  available.
- Removed duplicate menu input from the fallback path so a single B press no
  longer opens and immediately closes the pause menu or requires a long hold
  to resume.
- Expanded compositor and input regression coverage for two-eye orientation,
  asymmetric viewports, overlays, controller-space policy, and menu gestures.

The final beta.2 candidate passed 69 automated checks and received physical
Quest 3 headset acceptance through SteamVR for upright stereo, depth, head and
controller tracking, weapon motion, and clean pause/resume behavior. Other
OpenXR runtimes keep their previous reference-space and Menu-button behavior.
Pimax compatibility remains unconfirmed and is not claimed as fixed.

## v0.4.0-beta.1

First beta following the creator's complete Campaign playthrough in VR.

- Added right-controller aiming and right-trigger fire for the Black Cats
  aircraft gunner stations. Headset look stays independent, native gun limits
  remain active, and handheld snap/stance controls no longer steer the guns.
- Added the missing Campaign satchel throw binding: select with Y + left-stick
  up, hold with right grip, throw with left trigger, and detonate with right
  trigger. The native satchel path also works with Button Grenades enabled.
- Fixed right-controller airstrike targeting in Okinawa/Shuri Castle, whose
  radio asset differs from the Little Resistance rocket-barrage designator.
- Reduced campaign CPU overhead from repeated memory checks, vehicle-context
  queries, draw-list validation, and diagnostic-only weapon observers.
- The normal launcher now enables the physically tested deferred frame-begin
  setting for Campaign/Zombies. Its existing runtime allowlist restricts this
  behavior to VirtualDesktopXR; Multiplayer keeps its previous timing policy.
- Added closed support-hand presentation around the pistol grip. Close or
  coincident controller positions now retain stable right-controller aiming
  instead of triggering the rifle separation guard and dropping the pistol.
- Fixed the Zombies Colt incorrectly falling back to native automatic reload
  when multiple valid model profiles shared its internal weapon name. Manual
  magazine reloads and the empty-reload slide action use the matched profile.
- Updated Campaign status, mission controls, installation references, known
  limitations, and extracted-source rebuild instructions for the beta.

The Campaign was completed using evolving development builds, not replayed
in full on the final beta binary. The latest pistol close grip and manual
reload correction received separate user headset acceptance on Quest 3 through
Virtual Desktop. Performance improved on the tested setup; remaining noticeable
streaming choppiness cleared after the user restarted the router with the mod
binary unchanged. No universal frame-rate, all-weapon, Pimax, or Meta Quest Link
compatibility claim is made.

## v0.4.0-alpha.5

Campaign special weapons, tank controls, and firing feedback update.

- Added a physical scope and five-round manual clip reload to the PTRS-41,
  including its loading hatch and either-hand charging interaction. The PTRS-41
  remains semi-automatic; it does not require cycling between individual shots.
- Corrected Panzerschreck weapon alignment and native rocket direction so
  projectiles follow the launcher tube instead of slanting upward/right.
- Added dedicated Campaign tank controls: left-stick driving, proportional
  right-stick cannon traverse/elevation, retained cannon target when the stick
  is released, right-trigger cannon fire, and left-trigger OT-34 flamethrower.
  Headset look remains independent of cannon aim.
- Replaced the headset-following tank crosshair with a binocular marker at the
  cannon's native target. This is an aim marker, not a moving-target lead aid.
- Added COD4-style right-controller firing vibration for confirmed local
  firearm, mounted-gun, and successful launcher/tank-cannon shots. Empty-trigger
  pulls, AI shots, and weapon handling do not generate firing pulses; stale
  feedback is discarded instead of replayed after a focus or session gap.
- Updated player controls, release notes, and matching-source packaging.

Firing vibration has received Quest 3 / Virtual Desktop headset acceptance.
PTRS-41, Panzerschreck, and tank changes include targeted simulator checks;
physical coverage of the full Campaign remains incomplete. Pimax and Meta
Quest Link compatibility are not claimed fixed by this update.

## v0.4.0-alpha.4

Campaign progression, mission-weapon, and stereo-effects update.

- Added controller-directed campaign mounted machine guns. While the local
  player is using an ordinary fixed machine gun, the visible gun, native
  mechanical limits, authoritative firing direction, and right-trigger fire
  now share the live right-controller ray while headset look remains
  independent. Fixed scoped turrets retain their native control path.
- Fixed Campaign Mission Select remaining locked despite the launcher startup
  option. The launcher now uses WaW's actual `mis_01` mission-progression
  control instead of COD4's ineffective `mis_cheat` flag, and the injected
  runtime reapplies `seta mis_01 50` after profile initialization. This updates
  the active profile's mission high-water mark without replacing save files,
  enabling developer mode, or changing mission difficulty records.
- Fixed bazooka aiming so the rocket, visible trail, impact, and explosion
  follow the physical barrel instead of traveling upward and off-angle.
- Added physical manual reload support to the Campaign's scoped Mosin-Nagant,
  matching the other supported bolt-action rifles.
- Added physical manual reload support to the bayonetted M1 Garand.
- Fixed smoke and other soft-particle effects appearing in only the left eye.
  The packed right-eye FloatZ clear now uses viewport-local geometry, restoring
  the depth data those effects need in both eyes.
- Expanded 32-bit OpenXR runtime selection and diagnostics for Pimax testing.
  Pimax Crystal Light compatibility remains unconfirmed and is not claimed as
  fixed in this release.

## v0.4.0-alpha.3

Left-support-grip alignment hotfix.

- Fixed rifles shifting up and to the left when the left support hand was added
  after drawing the weapon with the right hand. The stable VR two-hand pose now
  suppresses World at War's overlapping native support/ADS weapon animation for
  every valid single-player weapon identity, including the Mosin-Nagant, rather
  than only the previously tested M1 Carbine and Kar98k.
- Preserved the accepted one-hand and two-hand pose solver, grip handoffs,
  controller-directed aiming, physical scopes, manual reloads, and manual
  weapon actions.

## v0.4.0-alpha.2

Weapon-stability, campaign-control, accessibility, and language-support update.

- Fixed the major weapon float and wobble affecting two-hand aiming. Rifles now
  remain aligned through one-hand and two-hand use, manual actions, handoffs,
  and regrips instead of hovering around the controller aim line.
- Fixed weapon jumps and angle changes when releasing either grip, retaining a
  rifle in the other hand, operating its action, and returning to two hands.
- Added either-hand bolt and charging-handle operation: a supported weapon can
  be retained in either hand while the other hand performs its physical action.
- Completed the M1 Garand manual reload interaction and tightened manual
  reload, empty-chamber, bolt, and charging state transitions.
- Added deliberate two-hand rifle-thrust melee, including native bayonet attacks
  on bayonet-equipped rifles, while retaining right-stick and free-hand melee.
- Disabled native movement and weapon view bob for steadier, more comfortable
  VR locomotion and aiming.
- Added optional launcher-controlled smooth turning at up to 120 degrees per
  second. The existing 45-degree snap turn remains the default.
- Added default-off launcher options for A-button automatic reloads and
  conventional left-trigger grenade throws, while preserving the existing
  physical interactions as the default.
- Added the initial Campaign Mission Select startup option. Its COD4-derived
  `mis_cheat` flag did not unlock missions in WaW and is corrected in the next
  release.
- Added safe controller tracking-loss and relocalization recovery. A held
  weapon pose is briefly frozen through a transient loss, while a timed-out or
  relocated controller must be released before it can reacquire the weapon,
  preventing detached weapons from floating or violently jumping.
- Improved controller-pose/frame matching, stereo camera construction around a
  stable head-centre pose, and the nonblocking shared-frame handoff used by the
  VR presentation path.
- Added Steam-installed language detection for English, French, Italian,
  German, and Spanish (Spain). The launcher displays the detected language,
  validates the matching `zone\\<language>` data, and atomically refreshes a
  stale cached language selector before launch.
- Revalidates the installation immediately before launch and reports incomplete
  language data instead of starting with mismatched cached files.
- Added the reported Quest 3 Meta Quest Link crash and Pimax Crystal Light
  desktop-only output paths to the tester-facing known-issues documentation.

## v0.4.0-alpha.1

First public alpha baseline for the current launcher and retail World at War VR runtime.

- Added OpenXR stereo rendering with rotational and positional headset tracking.
- Added tracked controllers, two visible hands, right-hand weapon aiming, two-hand weapon support, physical muzzle direction, snap turning, sprint, stance changes, melee, and native-menu interaction.
- Added manual bolt-action cycling, stripper-clip and magazine interactions, empty-magazine charging behavior, physical scopes, and motion-controlled grenades.
- Added a standalone Windows launcher with game detection, Zombies/Campaign and experimental Multiplayer targets, saved settings, and Native, Performance, and Recovery quality presets.
- Added a portable-package layout that excludes World at War game files and is paired with complete corresponding source.
- Marked Zombies as the primary test target; Campaign and local/offline Multiplayer remain experimental, and online Multiplayer remains unsupported.
