# Scoped Mosin optical zero

## Current status: local headset accepted

After the PID47392 physical retest, the user replied: "Ok, that's good. Done"
to the checklist covering reticle/impact alignment, two-grip enemy visibility,
and bolt/regrip stability. Record this as user acceptance of this test on their
headset, not independent validation on Yangyoung's Pimax. Verified no CoDWaW
process remained and the accepted DLL still matches the SHA256 below.
The source changes and `out/scope-mosin-zero` build are the accepted local
candidate for the next release; no public/customer package was updated here.

## User observation and preserved baseline

The user accepted enemy visibility with `out/scope-visibility-isolation`
(DLL SHA256 `9D4600FF0BAFDAC7C9AA6A87BF31AB02F0B4E5A0A45B34B34FC0C33EE9E1370D`),
but reported shots slightly up and left of the scope center. They replied
done; no CoDWaW process remained. The visibility change is preserved.

## Targeted correction

The evaluated scoped Mosin projectile basis follows muzzle minus grip.
Previously, the physical scope camera used the weapon-placement basis.
These are different frames, despite the authoritative and predicted bullet
paths using the same published projectile basis. Reticle drawing is centered
in the dedicated scope image and has no fixed up-left bias.

For exact identity `mosin_rifle_scoped`, publish a separate optical camera
basis using the current evaluated projectile basis. Place that virtual camera
at the point on the projectile line nearest the physical lens. Thus a point
on the accepted shot ray projects to the optic center at all distances,
without imposing an arbitrary convergence distance. Physical glass/quad
placement, grips, shot direction, and other weapon profiles are unchanged.

Reject nonfinite, nonunit, nonorthogonal, reflected, or foreign-origin inputs.
Lens-to-muzzle separation must be <=128 IW; lens-to-camera displacement must
be <=16 IW. Rejection preserves the prior lens-camera behavior for that frame.

## Validation

Candidate build directory: `out/scope-mosin-zero`.
Tests cover three orientations and 64/512/8192 IW target depths, unchanged
physical lens and stereo eyes, separate camera selection, atomic rejection,
and boundary cases. Build, runtime, and headset outcomes recorded below when
completed. Do not infer headset zero acceptance from mathematical tests.

Headset relaunch requires a warning and a fresh ready. Use the disposable
`out/scoped-mosin-direct-headset-home`, the exact scoped-Mosin equip preset,
Virtual Desktop's 32-bit runtime, and 6016x2688. Do not use Continue or alter
the user's campaign saves.

### Completed offline/runtime checks

- Win32 Release build passed. Full CTest suite passed 70/70 in 17.68 seconds;
  targeted scope/stereo tests also passed. Existing unrelated compiler
  warnings remain. Targeted diff whitespace check passed.
- DLL SHA256:
  `817B5B7FE12B01B6F522197126FF3A6EC411083408CAB3F30A726449CF3C7D4B`.
- Simulator PID49128 loaded `maps/sniper.d3dbsp`, CA_ACTIVE, scoped Mosin
  index4 at 6016x2688. Read-only backend inspection confirmed three views.
- 16:18:48 diagnostic measured the old ray **0.7617 degrees left and 1.6312
  degrees up**, matching the direction reported by the user. New camera
  origin=(5870.6973,3199.5928,409.6988), muzzle=(5870.3555,3234.4517,406.2937),
  projectile forward=(-0.009757,0.995216,-0.097214). The new camera is on that
  projectile line; lens placement remains (5870.1211,3199.9150,413.0552).
- Visibility-isolation receipts still restored all 2048 entries to unknown
  after the scope's [1847,1,200] result. No visibility implementation changed.
- Computer-use inspection showed a live physical optic at simulated right-eye
  height. The native game must be focused for trigger firing (the simulator
  preview alone can move the tracked rifle but did not produce a shot).
- 16:21:49 actual firing receipt: `profile=Mosin-Nagant Scoped`, applied=1,
  visibleF=(-0.0046,1.0000,0.0047), ADS spread=0. This confirms the accepted
  projectile route still executes; it is not a screenshot-verified impact
  zero measurement. Final perceived reticle/impact alignment remains for the
  physical headset test.
- Logs preserved in `artifacts/scope-mosin-zero-20260921/`. Only PID49128 was
  stopped after verifying the loaded exact simulator DLL; no CoDWaW process
  remained at handoff. No headset launch was made in this turn.

### Headset retest launched

After the next fresh user ready, verified no CoDWaW processes remained,
verified the exact DLL hash above, and launched PID47392 with Virtual
Desktop's 32-bit runtime and the disposable headset home. At 16:25:23,
read-only inspection confirmed `maps/sniper.d3dbsp` loading after the preset
map transition. The game was left running for the user, who subsequently
accepted the result and closed it (see current status above).
