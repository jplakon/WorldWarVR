# Test plan

Testing advances through explicit safety gates.

1. **Offline validation:** launcher dry-run identifies data, executable,
   architecture, SHA-256, clean homepath, DLL, validated packed source
   resolution, and final command line. Exercise the exact-SP `6016x2688`
   default, the explicit `3744x2016` performance, `2560x1440` compatibility,
   and `1024x768` recovery settings, plus a rejected odd/out-of-range width.
   Confirm that MP retains its desktop-bound `2560x1440` default. Exercise
   direct Nacht, direct `--der-riese`, and frontend command generation,
   including the target-specific pinned Der Riese fastfile/video checks. For
   both SP and offline MP, require exactly one non-persistent `bg_bobMax 0` and
   `cg_bobWeaponAmplitude 0` launch override, placed before any `+devmap`.
2. **Direct-boot byte gate:** after the exact on-disk SHA-256 and mapped module
   validate, the DLL must match the complete 24-byte WaW 1.7.1263 instruction
   context at RVA `0x0019D678`. It atomically changes only the branch opcode at
   RVA `0x0019D682` from `JNE` (`75`) to `JMP` (`EB`), thereby skipping the
   independently disassembled `E8 70 6B FF FF` call at VA `0x0059D68B`.
   Immediately before that call, EAX is loaded with the verified local string
   `cinematic Treyarch\n`, ECX is cleared for command buffer 0, and the call
   target at `0x00594200` appends the text to that buffer.
   The log must say `status=applied` (or idempotently `already-applied`). A
   changed or truncated context must be rejected without a partial patch. The
   live acceptance check uses `WAWVR_DISABLE_XR=1`: an injected launch must
   reach the requested GfxWorld—`nazi_zombie_prototype` for plain `--launch`,
   and `nazi_zombie_factory` for `--launch --der-riese`—instead of stopping at
   the main menu. A successful byte-patch log by itself does not prove injection was
   early enough to preserve the launcher's `+devmap` command. Set
   `WAWVR_DISABLE_DIRECT_BOOT=1` to leave the startup code untouched for a
   recovery/control launch. The one-shot skip remains installed for the
   process lifetime; restoring it asynchronously would create a new race after
   initialization and provides no runtime benefit.
3. **Bootstrap:** game starts suspended, the DLL loads, writes a log, applies
   the direct-boot gate before renderer monitoring, and exits cleanly.
4. **Mono XR:** OpenXR session creation and a captured mono frame appear in the
   headset while the desktop mirror remains usable. Verify both eyes show the
   complete frame (not left/right halves), Alt-Tab and a D3D9 Reset rebuild the
   path, and `WAWVR_DISABLE_XR=1` leaves an unhooked desktop-only launch.
5. **Stereo/head:** both eyes render from one simulation tick with correct HMD
   rotation/translation, conservative culling, a level first-spawn anchor, and
   a manual recenter gesture.
6. **Input/weapon/HUD:** controller movement, default 45-degree snap turn and
   launcher-selected proportional smooth turn at up to 120 degrees per second,
   Use/reload, weapon
   cycling, click-to-sprint held until locomotion returns to neutral, frag,
   tracked gun, controller-derived shot origin/direction,
   fixed ADS-level local firearm accuracy while hip-firing and moving (without
   ADS slowdown/FOV), gun-only viewmodel, stable reload camera, sharp ADS,
   absent firearm crosshair, right-stick/free-hand/two-hand-thrust melee without
   camera rotation/lunge,
   level manual recenter, controller-directed frag release, and identical
   reduced/readable ammo/points/round/use-prompt HUD in each eye.
   Confirm a held horizontal stick produces continuous frame-rate-independent
   yaw only in Smooth Turning mode, while right-stick up/down continues to
   perform the same crouch, prone, rise, and jump gestures in both modes.
   Fire repeatedly while moving the controller: every own tracer must start at
   the visible barrel, follow the fixed-ADS shot, and end at the actual hit
   rather than an independent eye-origin/hip-fire ray. Confirm enemy blood and
   world impacts remain visible. In Zombies, fire the Ray Gun repeatedly while
   looking away from the body heading and require its trail and impact
   explosion on every shot.
   For each supported scoped rifle, shoulder it with both grips and require a
   sharp magnified world view only inside a circular weapon-attached lens. The
   two ordinary eye views must retain the rifle while the lens source contains
   neither the depth-hacked rifle nor WaW's full-screen 2D scope mask. Moving
   the rifle must move the lens in both eyes without changing stereo, head
   tracking, muzzle direction, grips, or manual cycling.
   With an ordinary rifle held in both hands, make one deliberate forward
   thrust and require exactly one native melee action; repeat with a
   bayonet-equipped rifle and require its native bayonet attack. Right-stick
   click and the free-right-hand outward swing must remain available. Slow or
   lateral two-hand aiming, firing, holding either grip steady, adding or
   releasing the support hand, and handing the rifle between hands must not
   trigger melee. A held or completed thrust must not repeat until the gesture
   is rearmed.
   For manual grenades, put the tracked left hand at the left hip and press the
   physical left index trigger: a frag must appear in that hand while the
   firearm remains visible in the other hand. Holding the trigger must retain
   native selection, pin/cook, fuse, sound, and ammo behavior; releasing a
   still hand must drop the grenade, while a physical throw must launch it in
   the sampled hand direction rather than the view direction. Repeat at the
   right hip for a tactical grenade when one is available. A trigger already
   held while entering either hip, loss of focus/tracking, empty inventory,
   and release/re-grab must not duplicate a throw or steal a rifle grip.
   Native menus additionally require a finite room-anchored panel: physical
   HMD translation must create parallax without moving the panel, the right aim
   ray must move the stock cursor, one trigger edge must click exactly once,
   off-panel/held trigger states must not click, and left-stick/A/B fallback
   must remain intact.
7. **Zombies loop:** select a stock Zombies map from the frontend and launch
   both Nacht and Der Riese directly; buy/use one interaction, complete several
   rounds, die, restart, and exit without persisting Plutonium-specific state.
8. **Offline multiplayer:** use the single-player frontend's Launch Multiplayer
   action and the no-argument `WorldWarVR-Multiplayer.exe`; verify the SP
   process exits before the isolated MP runtime acquires
   OpenXR. Automated launcher tests must first prove canonical handoff-config
   serialization/parsing, SP-relative MP runtime/home derivation, exact option
   reconstruction, and fail-closed missing, malformed, non-canonical, unsafe,
   or wrong-stage configurations. Confirm the MP frontend is a finite world-fixed panel with a visible
   right-controller pointer, create a local listen server, enter a stock map in
   stereo VR, and verify head/controller/weapon input. Prove the isolated
   active profile has `ui_dedicated 0`, then require a human `J` record and
   subsequent allies/axis `JT` record in `games_mp.log`; bot-only join records
   are a failure. With the exact user-owned
   PeZBOT archive supplied, require `Loading fastfile 'mod'` before map init,
   confirm there is no `Unknown playerAnimType` error, and confirm nine bots
   join. Return to the MP frontend, select a different stock map and a
   different standard mode, and confirm one new nine-bot roster joins without
   restarting the process. Repeat once more and verify that remaining in the
   frontend or opening team/class/pause never creates a second roster. Exercise
   both observed paths (`10 -> 0 -> 5/7/6/8 -> 10` and
   `10 -> 6/7/6/8 -> 10`) and require one roster per server; automatic rotation
   must also produce one roster without duplicates. Team,
   class, and pause UI must remain a complete selectable mono panel
   rather than a panel containing two adjacent eye views. Ray/trigger and
   stick/A must activate the focused team/class item through the active-MP
   Enter route while the frontend continues to use Mouse1. After the class
   menu closes, require a logged `active=10`, `keyCatchers=0`, `mode=gameplay-stereo`
   transition and immediate immersive head-tracked stereo. Reopening and
   closing pause must repeat the mono-panel/stereo transition without losing
   the listen-server client. Prove an MP first run can recover the game root from an SP-only
   prepared runtime, while absent, ordinary, partial, or mismatched links still
   fail closed. Exit and relaunch after `games_mp.log` exists to prove the verified
   install remains enabled. Without PeZBOT, confirm the base local server still
   launches and no online service is contacted.

Every live stage has an inherited environment recovery switch that disables
the relevant hook so a bad renderer, input, weapon, or camera patch remains
recoverable.

## Live acceptance record

On 2026-08-01, the exact local build passed the first four desktop-side gates:

- `WAWVR_DISABLE_XR=1` launch, PID 30072: the direct-boot patch reported
  `status=applied`; the process probe advanced to `CA_ACTIVE`, identified
  `maps/nazi_zombie_prototype.d3dbsp`, and read a valid 1024x768 gameplay
  refdef. This proves the standalone launcher reached live Nacht rather than
  merely opening the main menu.
- XR-enabled launch, PID 50224: the same map reached `CA_ACTIVE`; the exact
  40-byte `RB_SwapBuffers` sentinel passed, swap-chain Present slot 3 and
  device Reset slot 16 installed without replacing either COM object's vptr,
  and the Meta runtime initialized OpenXR with D3D feature level 11.1 and sRGB
  swapchains. The process remained healthy after the previous initialization
  crash boundary and reported the duplicate-mono path ready.

Later headset work on PID 59296 advanced the same exact executable substantially:

- The stock frontend was controlled with the left stick/A/B and entered
  `maps/nazi_zombie_factory.d3dbsp` (Der Riese), proving native Zombies map
  selection independently of either fixed direct-map target.
- The runtime logged exact two-eye scene submission from one simulation frame,
  and the tester directly reported working 6DOF, shooting without the earlier hitch,
  ADS, and horizontal right-stick turning. The tracked gun position was
  reported as good.
- Exact runtime diagnostics confirmed the gun-only viewmodel filter, corrected
  `tag_flash` publication, and replacement of the native bullet origin with
  that controller-aligned muzzle while preserving WaW's native firing path.
- Frontend/loading/cinematic states remained on the world-locked mono comfort
  path and active gameplay transitioned back to stereo. The desktop mirror
  visibly contained the native round counter, points, weapon/ammo text, and
  gun-only viewmodel.

The tester's next live run directly accepted ADS-level hip-fire accuracy, reload
camera stability, absence of ADS blur, gun-barrel/impact alignment, and Y weapon
cycling; right grip also dispatched a frag.

Direct `--der-riese` launch PID 54620 subsequently passed executable, base-map,
and injector validation, reached `CA_ACTIVE` at
`maps/nazi_zombie_factory.d3dbsp` with a `1600x900` refdef, and initialized the
Meta OpenXR path with D3D feature level 11.1 and sRGB swapchains. The tester then
accepted knife behavior without camera tilt/lunge, manual leveling, visible
controller-directed grenade release, pause-menu presentation, death/respawn,
continued stereo/6DOF, and the absence of black-screen or 2D fallback. The only
remaining headset gate was the replacement candidate's half-size gameplay HUD
and `2560x1440` packed-source clarity/performance/stability.

On 2026-08-02, the direct Der Riese candidate reached `CA_ACTIVE` with an
actual `2560x1440` refdef. The runtime selected the Meta-recommended
`1648x1776` swapchain for each eye, logged a `2560x1440`/14.1 MiB packed capture,
and submitted exact-frame stereo. The tester explicitly accepted the result as
sharper, with the HUD far enough away and the experience stable. This closes
the Zombies gameplay/HUD clarity gate; `1600x900` remains the documented
performance fallback. The later report that the frontend translated with the
head and lacked a usable visible pointer reopens native-menu acceptance. The
strict quad-only, baked-reticle revision and the new standalone offline-MP path
remain live headset gates and must not be treated as accepted from automated
tests alone.

## Little Resistance focused acceptance

1. Resume or start `pel1` and reach the beach rocket-strike prompt.
2. Hold Y/left-secondary and push the left stick left once. Confirm the native
   `6`/`+actionslot 4` path equips the radio/designator and that left-stick
   locomotion remains active rather than dropping the movement command.
   Confirm passive right-thumbrest contact alone never enters this path.
3. Move the right controller while moving the headset independently. The
   targeting marker must follow the controller; head motion must not steer the
   marker, roll the horizon, or move the camera.
4. Aim at a valid yellow point and pull the normal right trigger. Confirm the
   strike lands at that point. Confirm the mission's native red/invalid target
   refusal remains intact.
5. Release Y/left-secondary after the selection chord and confirm it does not
   also cycle the weapon. Then perform a plain Y/left-secondary tap and confirm
   weapon-next occurs exactly once on release.
6. Switch away from `rocket_barrage`. Ordinary gun aim, camera orientation,
   reload, melee, grenades, death/respawn, and recenter must remain unchanged.
7. Re-run a short Zombies check and the offline-MP menu/bot path. The SP-only
   callsite and action-slot modifier must not affect either mode.
8. Copy `WorldWarVR.log` and confirm the first rocket selection and the first
   accepted/rejected controller-angle substitution are each diagnostic
   one-shots rather than per-frame messages.

## Mounted machine-gun focused acceptance

1. In Campaign, reach an ordinary fixed machine gun and use it through the
   mission's native interaction.
2. Move the right controller across the gun's full traverse while moving the
   headset independently. Confirm the visible gun follows the controller up,
   down, left, and right, stops at the native mechanical limits, and is not
   steered by headset look.
3. Without requiring a handheld-weapon grip, pull the right trigger. Confirm
   the gun fires and its impacts follow the visible barrel throughout the
   allowed traverse.
4. Release the mounted gun and confirm ordinary one-hand/two-hand weapon aim,
   physical reloads, scopes, stereo, and head tracking remain unchanged.
5. If a fixed scoped turret is available, confirm it retains its native scoped
   control path and is not redirected to the ordinary mounted-gun hand ray.
