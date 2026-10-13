# Scoped Mosin entity visibility isolation

## Status

Follow-up: the combined visibility + optical-zero build `out/scope-mosin-zero`
was accepted in the user's physical headset after PID47392 on 2026-09-21.
See `2026-09-21-scoped-mosin-optical-zero.md` for the exact build hash and
acceptance record. Yangyoung's own Pimax retest remains separate.

Enemy visibility accepted in the user's headset on 2026-09-21: "The enemies
appear, but the bullet trajectory is zeroed to a little up and left of the
crosshair center of the scope. done." Scope zero is a separate follow-up;
do not revert the visibility isolation while correcting it. The original report was that both grips hide
enemies and vehicles in Vendetta. The `scope-visibility-order-test` candidate
(`CEA2A1978F4B9BEA665A81A9F5CCC7DE2BBDB84B1DA54660B697E0090B06597D`)
failed: the user then reported all enemies disappearing. Do not reuse that
candidate or count its passing unit tests as visual acceptance.

## Native evidence and targeted change

The local reconstructed T4 renderer shows `R_FilterEntitiesIntoCells` writing
`scene.dpvs.entVisData[bank][entnum] = 2` for frustum misses without clearing
previous misses when an entity passes another camera. The portal walk only
reconsiders byte-zero entries. These banks are camera and shadow visibility,
not one bank per VR camera. The narrow optic can therefore hide actors from
the subsequent normal eyes.

Retail SP mapping was checked against the restored executable text:

- `R_FilterEntitiesIntoCells`: `0x006E3730`.
- Entity-visibility bank pointer array: `0x03DA8CF4`, seven 32-bit pointers.
- `gfxCfg.entCount`: `0x03BF67EC`.
- Visibility-only clear loop: `0x006E5971`, 36-byte signature.

The new adapter validates that exact signature, entity count, and every
writable bank before enabling the optional physical optic. It snapshots the
seven banks before the scope frontend call and restores the original bytes
immediately after that call. Native frontend work waits for its visibility
and scene-entity jobs before returning. Ordinary eye calls then recompute
their visibility without the scope's exclusions. The whole scene is NOT
cleared: scene mappings and completed skinning remain intact.

The failed camera-order change is reverted to scope, left eye, right eye,
including backend scope ownership index zero. This also restores the expected
matching left-eye mesh for the packed right eye's FloatZ far clear.

No weapon grip, aiming, reload, or saved campaign profile settings changed.

## Validation and continuation

- Regression `mod.scope_visibility_snapshot` reproduces sticky native culling
  and verifies exact restoration, all banks, bounded input, destructor cleanup,
  idempotence, and subsequent frames.
- Candidate output: `out/scope-visibility-isolation`.
- Win32 Release build passed; full CTest suite passed 70/70 (17.70 seconds).
  Existing unrelated compiler warnings remain. Targeted diff whitespace check
  passed. DLL SHA256:
  `9D4600FF0BAFDAC7C9AA6A87BF31AB02F0B4E5A0A45B34B34FC0C33EE9E1370D`.
- Runtime log receipt: `ScopeDiag visibility isolated` contains camera counts
  before scope, after scope, and after restoration. These are evidence that
  isolation executes, not proof of final headset imagery.
- Invalid guard/pointers omit the optional optic and log a rejection, while
  ordinary stereo remains enabled.
- Simulator runtime/limited visual validation completed below. Physical
  headset enemy/vehicle and lens-content acceptance still pending.

The user replied done, and physical PID 39644 was verified gone. Simulator
PIDs 46644 and 36756 were used, then stopped only after verifying the loaded
`openxr_simulator.dll`. All CoDWaW processes were gone at handoff. Before a new
headset test, warn and obtain a fresh ready.

### Physical retest launched

After a fresh user ready, verified no CoDWaW processes remained and launched
PID 37872 using Virtual Desktop's 32-bit runtime, the disposable headset home,
6016x2688, and the hash-verified isolation DLL above. At 16:01:59 the log
published `mosin_rifle_scoped`, with visibility isolation restoring
[2048,0,0] after the scope produced [1846,7,195]. Read-only process inspection
confirmed `maps/sniper.d3dbsp`, CA_ACTIVE. The user subsequently confirmed
enemies appear and closed the game; no CoDWaW process remained at the start
of the scope-zero follow-up. The earlier simulator-only limitations below
remain a record of what the automated test did and did not establish.

### Simulator evidence, 2026-09-21

- The initial 2560x1440 run does NOT validate this fix: scope snapshots publish
  at that size, but the third-camera renderer requires packed width >=6016.
- PID 36756 used the same DLL at 6016x2688 and loaded `maps/sniper.d3dbsp`
  with `mosin_rifle_scoped` through the existing event4 preset. The preset
  requires a right-trigger press/release after initial gameplay to advance
  from Nacht; do not assume launch alone finishes the transition.
- At 15:31:46 the physical optic published, all seven exact-retail visibility
  banks validated, and the visibility-isolation receipt reported 2048 entities:
  cameraBefore=[2048,0,0], afterScope=[1847,1,200], restored=[2048,0,0].
  Repeated receipts agreed. Backend read-only probe confirmed three views and
  dedicated scope draw-list filtering. This is runtime evidence that the
  intended path ran, not just a unit test.
- A nearby allied character remained visible in BOTH eyes with right-only
  and two-hand grips; the scope lens activated on two-hand grip. Captures:
  `artifacts/scope-visibility-20260921/one-grip-ally.bmp` and
  `two-grip-ally.bmp`. The same folder preserves the runtime and equip logs.
- Coverage is deliberately limited: this comparison is an ally, not a full
  enemy/vehicle retest or a close-to-eye lens inspection. Do not call the
  customer issue resolved from these screenshots.
- Event4 initially has seven allies and no enemies. The local retail
  `sniper.ff` trigger `officer_entourage` is at (5089,3527,370), radius256,
  height128; player start is (5862.7,3188.6,389). Reaching that trigger starts
  the enemy/vehicle encounter. The numbered sniper-5 save is not evidence of
  a specific map event; save numbering is sequential, not event-indexed.

For the physical test use the existing disposable home
`out/scoped-mosin-direct-headset-home`, Virtual Desktop's 32-bit runtime, and
`WAWVR_HEADSET_TEST_EQUIP_SCOPED_MOSIN=1`. This selects Vendetta directly with
the correct scoped Mosin; do not use Continue (it previously opened another
mission). Use the new candidate DLL, not the failed order-test DLL.

Retest actors/vehicles outside the lens with right-only and both grips, then
scope motion and grip transitions. Also verify actors inside the lens and
normal stereo/head tracking. Do not mark the issue resolved until that passes.
