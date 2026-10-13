# Call of Duty: World at War VR convergence plan

## Decision

World War VR is a World at War adaptation of KisakCOD VR maintained by John
Plakon as a Patreon-distributed community project. Ryan
Craighead's WorldWarVR source is the authoritative retail-WaW/T4 foundation,
not the final product design.

The practical starting point is Ryan's injected-DLL repository because it
already owns the exact retail executable profiles, launch/injection boundary,
T4 hook validation, SP/MP separation, and D3D9 renderer seams. Starting from
the COD4 engine fork would require reconstructing the unavailable World at War
engine before any VR feature could run.

KisakCOD VR remains the product and interaction specification: visual quality,
OpenXR/OpenVR policy, controller semantics, settings, calibration, hands,
two-hand aiming, physical reload, grenades, HUD, menus, installer behavior,
support diagnostics, and release expectations.

Archived experimental evidence informed selected implementations. It is not a
build input, and no external checkout is required to build, test, package, or
release this repository.

## Pinned source snapshots

| Source | Snapshot | Authority |
| --- | --- | --- |
| KisakCOD VR | `jplakon/CallOfDuty4_VR` main, `271e4ac8f8a55433a46aeaf4423c4127198ab02d` (`v0.10.0-beta.15`) | Product behavior, user experience, controller interactions, settings, calibration, release model |
| WorldWarVR | `RyanCraighead/WorldWarVR-Project` main, `3beac3f7bb1485be14743b92495656a1e5c0acad` | Exact WaW profiles, retail hook ABIs, injection/launcher boundary, renderer and game bindings |
| Archived WaW experimental evidence | `waw-retail-vr-gate3-stereo-probe`, baseline `b169595c1a447c25422bece220bb55ef0cb4e9db` | Historical headset observations and interoperability research; not a release dependency |

Both upstream source lines and the new first-party integration work are
GPL-3.0. This repository is GPL-3.0-only, retains original copyright notices,
and records file-level provenance whenever code is copied or substantially
adapted.

## Target architecture

```text
launcher/
  exact game discovery and validation
  isolated runtime staging and DLL injection
  recovery switches and support bundle generation

src/platform/waw/
  exact SP and MP executable profiles
  mapped-byte validation and rollback-safe hook transactions
  T4 ABI wrappers, usercmd bridge, renderer hooks, DObj/tag access

src/vr/
  OpenXR/OpenVR lifecycle and action profiles
  frame timing, pose publication, compositor, scaler and swapchains
  no direct T4 memory access

src/gameplay/
  portable controller and interaction state machines
  movement, turn, hands, support grip, weapon pose, reload, grenades
  no OpenXR calls and no retail addresses

src/integration/waw/
  translates gameplay state into validated T4 operations
  weapon/tag placement, ammo commits, HUD/menu presentation

src/settings/
  shared settings schema, migration, validation and runtime receipts

tools/configurator/
  KisakCOD-style setup, calibration, interaction and compatibility UI

tests/
  pure interaction/math tests
  profile and byte-negative tests
  source/ownership contracts
  receipt and package tests

docs/provenance/
  Ryan-derived files and commit
  KisakCOD-derived files and commit
  independently written WaW integration work
```

The most important boundary is that `src/vr` publishes immutable frame and
controller snapshots, while `src/platform/waw` and `src/integration/waw`
consume them on the correct game/render thread. Game hooks never call OpenXR.

## Module migration map

| Subsystem | Primary source | Secondary source | New-project treatment |
| --- | --- | --- | --- |
| Launcher and exact game validation | WorldWarVR `launcher/` | KisakCOD installer/configurator | Retain the WaW validation/staging engine; replace product UX and support workflow incrementally |
| T4 executable profiles and ABIs | WorldWarVR `src/t4/` | Experimental fingerprint evidence | Import with history and tests; do not rewrite proven addresses from memory |
| Hook transactions and recovery | WorldWarVR `src/mod/` | Experimental rollback/receipt guards | Keep Ryan's transactional hook layer; add only focused guards that caught real regressions |
| OpenXR frame lifecycle | KisakCOD `src/vr/vr_openxr.*` behavior | WorldWarVR `src/xr/` implementation | Preserve Ryan's retail ownership boundary but converge on KisakCOD frame timing, runtime policy and diagnostics |
| OpenVR fallback | KisakCOD | none | Port only after the primary OpenXR WaW baseline is stable |
| Stereo scene generation | WorldWarVR stereo hooks | KisakCOD per-frame/per-eye semantics; experimental same-frame proof | Keep exact T4 hooks and command ownership; retain one simulation update and two eye renders |
| Projection and output quality | KisakCOD asymmetric projection/scaler policy | Experimental FSR1, full-extent crop and pixel proof | Benchmark on the same headset; adopt by measured quality and stability, not nominal dimensions |
| Input/action system | KisakCOD configurable semantic actions | WorldWarVR usercmd/input hooks | Use KisakCOD action schema and bindings; use Ryan's exact T4 command bridge |
| Locomotion and turning | KisakCOD | Experimental headset-accepted snap/movement math | Port pure math and preserve current physical acceptance criteria |
| Weapon placement and ballistics | KisakCOD final rendered weapon/muzzle semantics | WorldWarVR weapon/tag hooks; experimental retail proof | Use Ryan's exact callsites and DObj access to implement KisakCOD behavior |
| Right hand and muzzle | KisakCOD | WorldWarVR and experimental accepted behavior | Preserve controller grip, visible barrel, `tag_flash`, recoil/spread and obstruction semantics |
| Free left hand | KisakCOD tracked/palm/fallback behavior | Experimental WaW standalone glove hook | Port the KisakCOD pose model through the validated WaW render entity path |
| Two-hand weapon support | KisakCOD support modes and calibration | Experimental WaW two-hand solver | Port as a portable state/pose solver; keep weapon-specific calibration outside hook code |
| Manual reload | KisakCOD detachable-magazine state machine | Experimental WaW stripper-clip/bolt-action state and T4 commit bridge | Generalize into reload profiles; do not embed another state machine in a 10,000-line hook file |
| Grenades and melee | KisakCOD interactions | WorldWarVR native bridges; experimental physical throw | Use KisakCOD semantics with exact WaW native ownership |
| HUD, menus and prompts | KisakCOD settings/editor/prompt model | WorldWarVR finite menu panel and exact HUD bridge | Use KisakCOD UX while retaining WaW's safe renderer/UI injection points |
| Installer/configurator | KisakCOD | WorldWarVR exact WaW discovery/staging | Reuse KisakCOD workflow and settings discipline with WaW-specific compatibility probes |
| Diagnostics and acceptance | KisakCOD compatibility receipts | Experimental exact ownership/pixel/control receipts | Keep diagnostics that prove real state transitions; avoid enormous schemas for ordinary product flow |

## Manual reload design

Manual reload must be profile-driven because COD4's detachable-magazine flow
does not represent WaW's Kar98 stripper clip.

```text
ReloadProfile
  NativeOnly
  DetachableMagazine
  InternalStripperClip
  SingleRoundOrTube       (future)
```

### Portable state machine

The portable gameplay layer owns states and events only:

```text
Ready
  -> ArmedOrActionOpen
  -> FeedDeviceAvailable
  -> HoldingFeedDevice
  -> NearInsertionPoint
  -> Committing
  -> Ready
```

Each profile supplies permitted transitions, contact radii, orientation assist,
native animation policy, ammo-commit policy, and the model/tag description.
The state machine consumes current-frame controller pose and produces render
intent plus one-shot game events. It does not read a DObj, patch memory, invoke
OpenXR, or write ammo.

### Transform ownership

While a clip or magazine is held:

1. The current tracked off-hand/glove world pose is the positional and
   rotational authority every frame.
2. Near the weapon well, insertion assistance may align the feed device's
   orientation to the well while retaining the controller position.
3. The code must never mirror a hand transform, reconstruct current motion
   from a cached prior pose, or make the rifle pose the free-held authority.
4. A fixed hand-to-device attachment transform is calibrated once per device
   model and applied in controller-local space.
5. The authored in-weapon clip/magazine is hidden only while the detached copy
   is valid and owned by the reload state.

These rules directly address the experimental failures where the Kar98 clip
floated left of the glove, moved in the opposite direction, or stayed fixed to
the rifle.

### WaW internal stripper-clip sequence

1. A reload action arms the native bolt/reload state but blocks immediate ammo
   completion.
2. The validated T4 animation seam opens the bolt without replacing the
   tracked right-hand weapon pose.
3. Off-hand belt squeeze creates a separate stripper-clip render object at a
   calibrated transform relative to the current glove.
4. Contact with the top feed guide aligns orientation and commits the native
   ammo transfer once.
5. The clip is retired, the authored clip visibility is restored as required,
   and the native bolt-close segment completes.
6. Failure, focus loss, weapon change or controller loss cancels safely to a
   defined native state without leaving hidden bones or blocked firing.

## Historical convergence sequence

These phases record the staged integration design. Features present in the
current source have progressed beyond their original phase instructions;
current user-visible status belongs in `CHANGELOG.md` and `KNOWN-ISSUES.md`.

### Phase 0: preserve evidence

- Record relevant findings with stable source and evidence identifiers.
- Incorporate only reviewed, license-compatible source into this repository.
- Require no external workspace to reproduce a distributed release.

### Phase 1: establish the releasable source line

- Retain Ryan's handoff baseline in project history so provenance and solved T4
  work remain visible.
- Record Ryan's repository and KisakCOD VR as pinned, read-only references.
- Keep `PROJECT-DIRECTION.md`, `PROVENANCE.md`, the GPL-3.0-only license, and a
  contribution policy naming both source lines in every distributed source snapshot.
- Distribute the complete matching source beside each Patreon binary at the
  same access level and without an additional source fee.

### Phase 2: lock the physical baseline

- Reproduce stable stereo, 6DoF head tracking, locomotion, snap turn, tracked
  right weapon, barrel-directed shots and clean frontend presentation.
- Do not add left hand or reload until this baseline has automated and headset
  acceptance in the new repository.

### Phase 3: KisakCOD product layer

- Port the settings schema, controller semantics, compatibility report and
  configurator concepts as independent modules.
- Add OpenVR fallback only after OpenXR remains stable.
- Introduce output-quality presets and headset-specific validation.

### Phase 4: hands and two-hand weapons

- Port the tracked off-hand/palm fallback model.
- Add support-grip modes and weapon-specific calibration.
- Require clean release back to free tracking and preserve right-hand aim.

### Phase 5: physical interactions

- Port detachable magazines and grenades from KisakCOD.
- Add the `InternalStripperClip` profile using the experimental WaW evidence.
- Validate Kar98 visual clip ownership, bolt staging and ammo commit separately.

### Phase 6: packaging and polish

- Complete the KisakCOD-style configurator and guided installer.
- Add upgrade/uninstall restoration, support reports, deterministic packages,
  and a published compatibility matrix.

## Historical first implementation tranches

The first repository tranche was designed to contain only:

1. the imported Ryan handoff commit/history;
2. project naming, attribution and direction documents;
3. the target directory boundaries above;
4. CI that builds/tests the unchanged WaW baseline;
5. a machine-readable feature matrix showing `retained`, `port`, `rewrite`, or
   `deferred` for every module in this document.

The second tranche was designed to extract the portable controller/reload math
and tests without installing a new retail hook. Runtime feature ports followed
only after those structural changes were reviewable and reversible.

### Current convergence checkpoint

The repository now contains the integrated runtime, launcher, interaction
modules, tests, optional installer source, and Patreon bundle packager. Current
feature status belongs in `CHANGELOG.md` and `KNOWN-ISSUES.md`; this plan
continues to document the architectural provenance and acceptance boundaries.

The release build tests the source with the pinned OpenXR submodule. The
Patreon packager emits a versioned portable binary, complete matching source,
checksums, notices, and release notes inside one upload bundle.

## Acceptance policy

- Automated initialization or `FOCUSED` state is not headset acceptance.
- Every visual tranche requires coherent stereo, normal rotation and
  translation tracking, no black/double image, and the claimed physical
  interaction in the headset.
- Warn the tester and receive a fresh `ready` before every launch.
- One approved probe is single-use unless the tester explicitly authorizes
  another launch.
- Never discard source or evidence without review.
