# World War VR backlog simulator fix pass - 2026-09-19

## Scope

This pass addressed backlog items that could be fixed or meaningfully checked
without changing the user's system OpenXR runtime or requiring a customer
headset, custom map, or regional executable. Existing uncommitted project work,
campaign profiles, and accepted control/rendering paths were preserved.

## Implemented fixes

### Launcher Browse recovery

The launcher now handles Windows folder-picker failures as a recoverable error.
It keeps running and tells the player to paste the World at War installation
path into the path box. COM, file-system, permissions, invalid-operation,
argument, unsupported-operation, and security failures are covered.

### Shared firearm trajectory basis

All ordinary single-player hitscan and projectile firearms now request the
final visible barrel basis, not only weapons in the explicit bolt-action
profile table. Local predicted impacts and tracers use that same current basis.
This fixes the common trajectory cause reported for the M1 Garand, unscoped
Mosin-Nagant, and SVT-40 without adding per-weapon aim offsets.

### Mounted-gun input ownership

While the mounted-gun controller route owns the weapon, ordinary HMD/body-yaw
catch-up and snap/smooth turning are suppressed. The mounted gun therefore has
one controller-derived steering owner instead of mixing controller, headset,
and locomotion-turn inputs.

## Automated validation

- Win32 release build: passed.
- Native test suite: 69 of 69 passed.
- Launcher core/source-contract suite: 35 of 35 passed.
- Full WinUI launcher Release build: passed with 0 warnings and 0 errors.

## OpenXR simulator evidence

### Rifle trajectories

- M1 Garand: exact weapon 17; every observed shot selected
  `shared-type0`, and the local bullet origin was rerouted from the native eye
  point to `tag_flash`.
- Unscoped Mosin-Nagant: exact weapon 20; the explicit Mosin profile selected
  the visible basis. The recorded native/visible forward dot was `0.999983`
  and the override was applied.
- SVT-40: exact weapon 19; observed shots selected `shared-type0`.

These checks prove that the three specifically reported weapons now feed the
same visible barrel origin and direction into the shot path. Final physical
aim alignment still requires headset confirmation.

### Scoped Mosin rendering

2026-09-21 follow-up: reproduced the two-grip entity disappearance in the
user's headset, isolated scope-camera entity visibility, then corrected the
separate scope/shot-ray mismatch. The user accepted the combined
`out/scope-mosin-zero` build in their headset. See
`2026-09-21-scoped-mosin-optical-zero.md` for build identity and test details.
Yangyoung/Pimax confirmation remains pending; the following describes only
the earlier limited simulator pass.

The exact `mosin_rifle_scoped` profile published its physical optic, rendered
a dedicated 77-surface scope view in both eyes, suppressed only eight
depth-hacked weapon surfaces, and retained the surrounding world in both eyes
during two-hand use. The whole-screen black/blank failure did not reproduce.
The simulator scene did not contain a controlled visible NPC, so the separate
enemy-disappearance report remains awaiting mission/customer confirmation.

Evidence: `wawvr-backlog-simulator-artifacts/scoped-mosin/02-two-hand.bmp`.

### Melee safeguards

Pistol stress covered 180 jitter samples, tracking loss/reacquisition, a shot
combined with forward motion, grip transitions, six repeated held-pistol jabs,
a rotated jab, and a free-hand swing. It produced exactly eight intended melee
events and zero false events.

Two-hand rifle stress covered 180 jitter samples, one free-hand swing, and one
intentional rifle thrust. It produced exactly two intended melee events and
zero false events.

Evidence directories:

- `wawvr-backlog-simulator-artifacts/melee-pistol`
- `wawvr-backlog-simulator-artifacts/melee-rifle`

## Not closed by this pass

- Valve Index, Pimax, and flat-launch reports require the affected runtime and
  customer hardware/logs.
- German/uncut executable support requires the exact legitimate binary before
  an address profile can be validated.
- Custom Zombies DirectX crashes require the affected custom maps and a
  reproducible customer configuration.
- The grenade-launcher Garand automatic-reload report still needs its exact
  internal weapon identity; it was not guessed from display text.
- Hard Landing smoke flicker needs an exact checkpoint or clip.
- Pause-menu clipping remains open. The simulator menu gesture began, but a
  trustworthy native pause-page transition was not observed, so the invalid
  capture was not used to make a speculative geometry change.
- Bolt-rifle clip insertion snapping, pistol support-hand placement, and
  reload feedback were not reproduced strongly enough to justify changing
  already accepted pose/haptic behavior.
- The Garand reload probe equipped the correct rifle and exercised its ammo
  state, but its telemetry stopped at the final round. That harness stall is
  not counted as a reload pass or a product failure.

## Acceptance boundary

This is simulator and automated validation only. It does not replace a final
physical-headset check for visual alignment, mounted-gun feel, or customer-
specific runtime compatibility.
