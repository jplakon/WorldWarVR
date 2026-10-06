# Mounted-gun headset acceptance - 2026-09-21

## Build under test

- DLL: `out/trajectory-evaluated-barrel/src/mod/Release/WorldWarVR.dll`
- SHA-256: `F85DAC343CEE227DC53E2B2DAC51A36C41B66CED006240A8EDE367FF10697529`
- Campaign checkpoint: isolated Pel1 mounted-machine-gun profile restored from
  `wawvr-checkpoint-backups/20260901-mounted-mg-pel1`
- Headset/runtime: the user's connected Quest headset through Virtual Desktop

The isolated test profile preserved the user's normal campaign profile and
progress.

## Pre-headset checks

The following focused Release tests passed before launch:

- `t4.profile_validation`
- `mod.mounted_gun_logic`
- `mod.dll_smoke`
- `mod.input_mapping`

## Physical-headset result

The user confirmed all focused acceptance checks:

1. Right-controller movement aimed the mounted gun.
2. Independent headset movement did not redirect the mounted gun.
3. Right-stick turning did not redirect the gun or turn the player while
   mounted.
4. The right trigger fired the mounted gun.
5. Impacts followed the visible barrel.

## Status

Accepted on physical hardware. Mounted-gun aiming has one controller-derived
owner; headset look and ordinary snap/smooth turning remain isolated while the
mounted route is active.
