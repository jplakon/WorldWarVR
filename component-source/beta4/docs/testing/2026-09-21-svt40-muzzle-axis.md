# SVT-40 residual high-left trajectory: headset candidate

## User feedback and scope

The user confirmed the PPSh-41 correction is perfect. The other semi-auto has
a smaller high-left offset. The latest headset session identifies that weapon
as exact `svt40`, map-local index 19 in `ber3` (PPSh is 17).

Only exact `svt40` was added to the existing `hitscan_uses_tag_flash_forward`
allowlist. PPSh math, M1 Garand, Mosin, other variants, weapon placement,
grips, recoil, spread and reload behavior are unchanged. Both client impacts
and authoritative bullets use the published evaluated muzzle basis.

## Validation

- Release build passed; all 69 CTest checks passed.
- Independent source review found no blocking regression.
- Simulator PID 46012, session starting 2026-09-21 01:08 local time.
- Confirmed actual weapon identity `svt40` before collecting its shots.
- Tested centered and leftward aim with right-only and two-hand grips, then
  rightward/upward aim with roll in both grips. Head pose was unchanged.
- SVT shot records 5-11 report `profile=exact-tag-flash`, weapon 19,
  `applied=1`, one-generation-old publications, 0-16 ms age. Client impact
  basis responds to the same controller direction. These records do not mean
  client/server samples occur at precisely the same time during recoil.
- Switched back to PPSh: shot records 12-16 confirm weapon 17 still uses the
  accepted flash basis. No global sight pitch/yaw adjustment was introduced.

Paired SVT geometry at 01:11:21:

```
grip-to-muzzle = (-0.696227, 0.652947, -0.298208)
flash forward = (-0.682341, 0.647175, -0.339964)
flash left    = (-0.697290,-0.715844,  0.036806)
flash up      = (-0.219541, 0.262168,  0.939718)
```

Projection of the old segment into the flash frame gives approximately
2.510 degrees up and 0.407 degrees left (2.543 degrees total). This supports
the grip-anchor offset diagnosis. The capture was during a weapon change;
it is NOT a measured through-iron-sights zero at a known target distance.

Evidence: `C:\Users\jplak\Downloads\test\trajectory-svt40-simulator-artifacts\2026-09-21-svt-flash-validation.log`
(append log, includes older sessions; use the timestamps above). Screenshots
12-15 are SVT; 16 is PPSh regression. Screenshot 11 is PPSh before selection,
despite its filename, and must not be used as SVT evidence.

## Headset acceptance

Accepted by the user on 2026-09-21 in Quest 3 through Virtual Desktop. The
exact SVT-40 was selected by the test preset and its trajectory was reported
as good after firing through the iron sights. The user then closed the game.
The PPSh-41 remained on its previously accepted muzzle-axis path.

## Headset launch procedure used

User closed the previous game and confirmed done. Simulator was closed and
no CoDWaW process remained. Wait for fresh `ready` before headset launch.

Build: `out\trajectory-evaluated-barrel`.
Use `launcher\Release\wawvr-launcher-cli.exe --launch --mod-dll` with this
build's `src\mod\Release\WorldWarVR.dll`.
Child environment: `XR_RUNTIME_JSON=C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr-32.json`,
`WAWVR_HEADSET_TEST_EQUIP_SVT40=1`. Clear other simulator/headset weapon
preset flags, especially `WAWVR_HEADSET_TEST_EQUIP_PPSH` and
`WAWVR_SIMULATOR_EQUIP_MAGAZINE_WEAPON`, to avoid ambiguous preset selection.
The SVT preset hands off Nacht to `ber3`, grants god/notarget/ammo and cycles
to exact SVT identity. Verify the fresh-session selection log before telling
the user to shoot. Do not ask them to cycle weapons for this SVT-only test.

The acceptance test requested near/mid/far surface shots with right-only and
two-hand grips and aim away from the head-forward direction. The reported
result was: `It's good.`
