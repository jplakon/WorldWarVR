# Source provenance

Copyright (c) 2026 Ryan Craighead. Modifications copyright (c) 2026 John
Plakon and contributors.

The 2026 World War VR releases, originally distributed through Patreon and
now also available free at https://github.com/jplakon/WorldWarVR, substantially modify the
upstream launcher, OpenXR rendering path, controller input, two-hand weapon
handling, physical reload/scopes/grenades, performance behavior, packaging,
and documentation. Exact user-visible changes are recorded in `CHANGELOG.md`.

This project combines GPL-3.0-compatible work from two public projects and new
World at War integration work. Preserve this record when moving the repository
or reorganizing files.

## WorldWarVR handoff

- Repository: `https://github.com/RyanCraighead/WorldWarVR-Project`
- Pinned handoff commit: `3beac3f7bb1485be14743b92495656a1e5c0acad`
- Author/maintainer at handoff: Ryan Craighead
- License: GPL-3.0-only for first-party code, with third-party terms recorded
  by the source repository
- Role: exact retail WaW profiles, T4 validation and ABI research, injected
  runtime, launcher/staging, renderer and gameplay hook foundation

The development checkout originated from Ryan's handoff commit. This public
repository starts from a reviewed matching release snapshot rather than
claiming to reproduce the complete earlier development history. Files retained
or adapted from the handoff must keep its copyright and license notices; the
pinned handoff above remains the upstream attribution record.

## KisakCOD VR product reference

- Repository: `https://github.com/jplakon/CallOfDuty4_VR`
- Pinned product commit: `271e4ac8f8a55433a46aeaf4423c4127198ab02d`
- Maintainer: John Plakon
- License: GPL-3.0
- Role: VR product behavior, frame semantics, controller interactions,
  calibration, settings, configurator, hands, two-hand support, physical
  reload, grenades, HUD/menu behavior, installer and release workflow

KisakCOD is an engine-integrated COD4 project. Code must be adapted through a
defined interface rather than copied into retail hooks with COD4 structures or
ownership assumptions intact.

## Archived WaW experimental evidence

- Evidence branch: `waw-retail-vr-gate3-stereo-probe`
- Baseline commit: `b169595c1a447c25422bece220bb55ef0cb4e9db`
- Role: historical stereo, controller, two-hand, grenade, and manual-reload
  interoperability observations
- Release role: none; it is not a build, test, packaging, or runtime dependency

Only focused, reviewed, license-compatible source incorporated into this
repository is distributed. Every public build can be reproduced from its
matching tag without access to an external workspace. Each substantially
adapted change must identify its source repository and pinned commit,
destination, behavioral changes, retained copyright and license notices, and
required tests.

## Independently written convergence modules

- `src/mod/firing_haptic_logic.*` and `src/mod/firing_haptics.*` are new WaW
  integration code. The 0.78 amplitude / 0.050-second firing-hand pulse follows
  the COD4 product behavior at the pinned reference above. Native WaW shot
  producers enqueue bounded notifications; only the WaW XR lifecycle applies
  haptics. No COD4 hook addresses, runtime ownership, or assets were copied.
- `src/gameplay/manual_reload_logic.*` is new, engine-independent code written
  for this repository. Its profile and stage vocabulary follows the product
  behavior documented from KisakCOD VR, while its explicit current-hand pose
  ownership and internal-stripper-clip profile encode lessons from the local
  WaW experiment. It copies no retail addresses, engine structures, model
  assets, or hook implementation from either reference tree.
- `src/gameplay/manual_reload_logic_test.cpp` is new validation code for that
  module. It covers native fallback, detachable magazines, internal stripper
  clips, current tracked-hand ownership, insertion-only orientation assist,
  commit timing, and cancellation on focus or weapon-context loss.
- `src/gameplay/tracked_attachment_math.*` and its test are independently
  written rigid-transform code. They calibrate a fixed hand-local device
  transform and resolve it from the current tracked hand every frame. The
  interface deliberately permits an insertion guide to replace orientation
  only, preserving current-hand positional authority.

## Contribution record

Every pull request that substantially copies or adapts an existing module must
include:

1. source repository and pinned commit;
2. source file and relevant symbols;
3. destination file and behavioral changes;
4. retained copyright/license notice;
5. tests and physical acceptance needed for the port.

Third-party libraries and assets remain under their own licenses. Do not add
game executables, game data, map files, fastfiles, saves, or proprietary SDK
redistributions to this repository.
