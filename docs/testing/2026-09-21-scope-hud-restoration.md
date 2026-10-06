# Scope-active compass and ammo HUD restoration

## Boundary

The user reported the optic and surrounding world visible but the compass/map
and ammo display missing while both grips activate the scope. After their
`done`, no CoDWaW process remained. No physical-headset launch is authorized
until another fresh `ready`. Use the disposable scoped-Mosin test home; do
not replace the user's campaign profile or checkpoint.

Accepted baseline remains intact at `out/scope-mosin-zero`, SHA256
`817B5B7FE12B01B6F522197126FF3A6EC411083408CAB3F30A726449CF3C7D4B`.

## Change

- Preserve scope,left,right rendering and the accepted visibility-bank
  isolation and optical/ballistic alignment. No grip or projectile changes.
- Send native 2D HUD commands to both normal eyes, not the magnified source.
  Previously all three command pointers were null when scoped.
- Suppress only three validated native scope-overlay/HUD-visibility predicate
  calls. Native weapon zoom/motion callers and ADS state are untouched. See
  `2026-09-21-scope-hud-native-guard-review.md` for binary evidence and ABI audit.
- Share optical-strip dimensions with the HUD placement planner. At 6016x2688
  with an active optic, normal eyes are 2496 pixels wide, not 3008.
- Share one game-thread scope snapshot/decision keyed by XR frame ID,
  predicted display time and viewport, including negative decisions. This
  prevents a 150ms snapshot expiry midway through HUD generation from
  disagreeing with scene layout. Scene visibility memory remains revalidated.
- Menu/unavailable-XR placement stays full-width. New broker queries catch
  exceptions and fail closed. Required retail-SP bootstrap rejects partial
  installation of the three guards; teardown preserves foreign patches.

## Offline verification

Win32 Release candidate is `out/scope-hud/src/mod/Release/WorldWarVR.dll`.
Final SHA256:
`D2015614577BE2719092B36AF2FEA82BBF9D28ED2752141024A88434669DFC57`.
All 70 CTests passed after the final rebuild (5.53 seconds). Added HUD tests
cover scoped and ordinary width, dimension thresholds, invalid reservations,
menu/XR fallback, and exact per-view command routing. Targeted diff checks
passed. Native sentinels and compiled 0x39-byte bridge were independently
reviewed.

## Simulator verification

Initial simulator PID48228 ran Vendetta (`maps/sniper.d3dbsp`) with the scoped
Mosin, 6016x2688, and the exact 32-bit OpenXR Simulator runtime. Its DLL was
`1A2AB56783F3B6357A323BF0321D2B5517E393B9923C1B90147AB94630F41348`.
The final rebuild above removes a redundant readiness check and formats the
new queries; it does not alter the checked behavior.

At 17:16:37, logs confirmed HUD width2496, execution of the native scope
guard, and command routing `(null, sharedHUD, sharedHUD)`. The read-only probe
confirmed three backend views. Computer-use inspection showed compass and
ammo in both eyes after firing while the scope view was active; near-eye
optic and reticle remained confined to the rifle lens with surrounding world
visible. Native timed HUD fading still applies; this is not an always-on HUD
change. Scoped Mosin shot and manual-rechamber receipts remained present.
Left-grip release/reapply changed backend view count 3 -> 2 -> 3, with the
scope and both-eye ammo HUD visible again. Console and Escape pause/resume
returned to stereo without crashing. The simulator's projection preview did
not provide a readable pause panel, so pause-panel appearance is not marked
as visually accepted by this test.

Only PID48228 was stopped after verifying its loaded simulator DLL path.
Its log is preserved at `artifacts/scope-hud-20260921/WorldWarVR-simulator-48228.log`.
The final build was then launched as simulator PID24396 for a final runtime
smoke check. It installed all three guards, initialized the OpenXR Simulator,
and reached Vendetta gameplay stereo at 17:26:53. That run did not exercise
the scoped HUD route, so the visual acceptance evidence remains the earlier
PID48228 check above, not a second final-binary visual pass. The last receipt
at 17:57:57 is `Shutting down XR path: OpenXR runtime requested exit`.
On resuming after the user's `done`, no CoDWaW process remained; no process
was terminated and no further game was launched. The final SHA256 still
matches the candidate above. Its appended log is preserved at
`artifacts/scope-hud-20260921/WorldWarVR-simulator-final-24396.log`.

## Headset handoff

Accepted locally. The user completed the physical-headset checklist and
reported: `It's fixed. Done.` This confirms the scope-active compass/ammo HUD,
optic and surrounding-world visibility, grip transitions, firing/cycling, and
pause/resume for this scoped-Mosin test on the user's headset. No CoDWaW
process remained afterward. This does not by itself close vrnut1234's
unidentified rifle/runtime report or Yangyoung's Pimax acceptance.

### Physical retest launched

After the user's fresh `ready`, warned before launch, verified no CoDWaW
process existed and verified the exact final candidate hash above. Launched
PID23312 using Virtual Desktop's `virtualdesktop-openxr-32.json`, 6016x2688,
the disposable `out/scoped-mosin-direct-headset-home` and
`WAWVR_HEADSET_TEST_EQUIP_SCOPED_MOSIN=1`. Read-only inspection confirmed
`maps/sniper.d3dbsp`, CA_ACTIVE, after the automatic transition. Left running
for the user to inspect one/two-grip HUD, scope/world/enemy visibility,
release/regrip, firing/cycling and pause/resume. Feedback is pending; do not
close the game or launch another test without the established launch gate.
