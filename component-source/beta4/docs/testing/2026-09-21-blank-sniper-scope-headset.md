# Blank sniper scope: headset regression

## Report boundary

The September 19 support audit records vrnut1234's September 12 blank sniper
scope report in Vendetta/Stalingrad. Exact rifle, checkpoint, original build,
and runtime were not captured. This is separate from Yangyoung's scoped-Mosin
enemy-disappearance report. Do not claim the reporter's issue closed solely
from this local Mosin regression.

## Prepared test

Use the locally accepted combined visibility/zero build from
`out/scope-mosin-zero`, SHA256
`817B5B7FE12B01B6F522197126FF3A6EC411083408CAB3F30A726449CF3C7D4B`.
No gameplay code change made for this test. After the user's fresh ready,
confirmed no CoDWaW process existed and launched PID44680 with Virtual
Desktop's 32-bit OpenXR runtime, 6016x2688, the disposable
`out/scoped-mosin-direct-headset-home`, and
`WAWVR_HEADSET_TEST_EQUIP_SCOPED_MOSIN=1` to enter Vendetta and equip the Mosin.
The user's campaign save/profile was not replaced.

Checklist: magnified scene and reticle visible through the optic; neither
eye or surrounding world goes black; move optic toward/away from eye and aim
around; fire, cycle bolt, reload, and release/reapply each grip. Distinguish
blank lens, frozen lens, missing surroundings, and whole-eye blackout.

## Physical feedback

User: "When I grip with both hands and activate the scope, everything is
visible except the map thingy and ammo count stuff at the bottom."

Thus blank optic/world was not reproduced in this scoped-Mosin run, but
scope-active HUD loss is reported. Do not mark this as an unrestricted pass
or as closure of vrnut1234's unidentified rifle/runtime.

Read-only source inspection found the explicit suppression in
`stereo_scene_hook.cpp`: with `stereo.scope_active`, all three
`render_commands` entries are null. The comment explains it drops the native
2D stream from the eyes to avoid the flat sniper mask. This also discards the
ordinary compass/map and ammo HUD. Non-scoped eyes receive the shared native
2D command stream. Do not simply restore all commands and regress the
full-screen black scope-mask fix; isolate native scope overlay suppression
from normal HUD delivery, and account for narrower scope-active eye packing.

PID44680 still existed during the initial inspection. The user subsequently
replied `done`, and no CoDWaW process remained before implementation began.
The scoped HUD correction and simulator checks are recorded in
`2026-09-21-scope-hud-restoration.md`. The user subsequently accepted that
candidate in the physical headset with `It's fixed. Done.` No CoDWaW process
remained afterward. This local scoped-Mosin pass still does not identify or
close vrnut1234's separate rifle/runtime-specific customer report.
