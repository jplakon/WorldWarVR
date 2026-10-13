# Project direction

This repository is being prepared as a World at War adaptation of KisakCOD VR.
John Plakon is the project maintainer. Ryan Craighead's WorldWarVR code is the
retail-WaW handoff and technical foundation for exact T4 integration.

## Product goal

Deliver the interaction model, comfort, calibration, visual-quality policy,
configuration experience, installer discipline, and campaign-quality support
expected from KisakCOD VR while running against the original 32-bit Windows
release of Call of Duty: World at War.

The game remains responsible for simulation, scripts, assets, saves, maps,
damage, ammo, animation, and content. The VR project supplies only validated
runtime integration and user-owned configuration.

## Source authority

- `RyanCraighead/WorldWarVR-Project` is authoritative for exact supported WaW
  identities, mapped instruction fingerprints, T4 layouts, hook ABIs, launcher
  staging, and the existing injected-DLL baseline.
- `jplakon/CallOfDuty4_VR` is authoritative for the intended VR product
  behavior and interaction semantics.
- The preserved `CallOfDutyWorldAtWar_Source` experimental checkout is
  evidence for headset-tested behavior and unfinished interaction research.
  It is not a source tree to merge wholesale.

## Engineering rules

1. Preserve the exact-profile, fail-closed WaW boundary.
2. Keep OpenXR/OpenVR calls on their owner thread and out of game hooks.
3. Publish immutable pose/input/frame snapshots across thread boundaries.
4. Put reusable controller and interaction state machines in pure, tested
   modules without retail addresses or renderer ownership.
5. Keep game memory access and ABI wrappers inside the WaW platform layer.
6. Introduce one reviewable feature tranche at a time with rollback and an
   explicit physical acceptance test.
7. Never infer headset success from initialization, focus, injection, or unit
   tests alone.
8. Warn the tester and obtain a fresh `ready` before every headset launch.

## Initial migration order

1. Preserve and continuously build the handed-off WaW baseline.
2. Reproduce stable stereo, head tracking, locomotion, snap turn, weapon aim,
   and barrel-directed shots under the new project identity.
3. Introduce the KisakCOD settings and compatibility model.
4. Port the free hand and two-hand support as portable interaction modules.
5. Port detachable-magazine reload and add a WaW internal-stripper-clip
   profile with clean transform ownership.
6. Complete the configurator, installer, packaging and support workflow.

See `docs/convergence/IMPLEMENTATION-PLAN.md` and
`docs/convergence/FEATURE-MATRIX.json` for the pinned sources and detailed
module plan.
