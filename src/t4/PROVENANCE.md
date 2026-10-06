# Research and provenance record

Date of verification: 2026-08-11
Purpose: local interoperability between a lawfully controlled World at War copy
and an independently implemented OpenXR mod.

## Directly observed evidence

The executable identities, PE fields, RVAs, and instruction bytes in
`profile.cpp` were read directly from the four user-owned files identified in
`README.md`. Each SHA-256 was independently recomputed over the complete file.
Every recorded plain-image code site was then read again at its RVA-backed file
offset.

The untouched Steam SP and MP files came from Steam AppID 10090 public Build
252004. Their local sizes and complete SHA-1 values matched depot 10091 manifest
5566849738478017518 in Steam's local depot-manifest cache: SP
`c582d8961f47b0320ede17ad5266d4dc7731961a`, MP
`ff8b4d31bd5f08d43bf812606db4882d3cc93049`. The wrapper adds exactly 352,256
bytes and a `.bind` section to each plain-layout counterpart. Read-only live
sampling after Steam restoration matched the existing SP/MP engine sentinels;
the completed implementation then validated every mapped profile sentinel
before installing hooks. On 2026-08-11, the staged Steam SP profile also
validated in the injected DLL and reached initialized OpenXR swapchains.

## Interoperability fingerprint boundary

The repository retains only the minimum native bytes needed to identify and
fail closed on the exact supported binaries. These are data constants, not
complete functions or redistributable game assets. The authoritative T4
profiles currently contain 87 bounded instruction sentinels totalling 2,269
bytes; no one sentinel exceeds 48 bytes. The profile tests intentionally repeat
their expected values so an accidental profile change cannot silently bless a
different executable.

Several hook owners keep a second, narrowly scoped copy of the bytes they must
compare immediately before writing or restoring a patch:

- `src/mod/direct_boot_patch.hpp`: startup conditional-branch context;
- `src/mod/input_hook.hpp`: displaced post-`BuildUsercmd` instruction;
- `src/mod/weapon_camera_patch.hpp`: weapon-camera and auto-melee call/branch
  contexts plus independently authored replacement bytes;
- `src/mod/present_hook.cpp`: SP/MP `RB_SwapBuffers` entry sentinels;
- `src/mod/stereo_scene_hook.cpp`: frontend and scene-render call contexts;
- `src/mod/stereo_backend_hook.cpp`: backend draw/clear call contexts;
- `src/mod/weapon_hook.cpp`: tracked-viewmodel, muzzle/ballistics, and campaign
  targeting call contexts.

Every original instruction sequence in those sites was measured read-only from
the same complete-file-hash-pinned SP or MP executable recorded by the profile.
Replacement calls, jumps, NOPs, and bridge instructions are WorldAtWarVR code.
Adding or lengthening a native sentinel requires a new read-only measurement,
an exact executable fingerprint, a documented hook purpose, and review that a
shorter complete-instruction boundary cannot provide the same safety.

The input and firing bindings were independently established from that same
exact image rather than imported from another mod:

- WinMain begins at preferred VA `0x005FF600`. Its sole direct call whose
  relative target is `0x0059E330` occurs at `0x005FF7BD` (RVA `0x001FF7BD`)
  with bytes `E8 6E EB F9 FF`; execution resumes at `0x005FF7C2`. The target
  has a conventional no-argument cdecl prologue and plain `ret` exits, matching
  `Com_Frame(void)`. The exact profile records both the call and its 29-byte
  WinMain context, plus the target entry, before allowing a call replacement.
- The validated integration services its VR frame immediately after
  `Com_Frame` returns. WaW's validated callsite wrapper preserves that
  ordering without importing an IW3 address: it invokes the original T4
  function first, then services D3D11/OpenXR. T4's Present hook remains a
  pre-Present D3D9 capture/result handoff only.
- T4's exact `ScrPlace_SetupFloatViewport` entry is preferred VA
  `0x0047A1C0` (RVA `0x0007A1C0`). Its first 47 complete instruction bytes
  read the real width/height from the caller-owned stack, write them at
  `ScreenPlacement+0x28/+0x2C`, and use `EDI` as the placement pointer. The
  independent renderer-initialization context at preferred VA `0x00644CFB`
  loads `EDI=0x00957318`, supplies zero origins and the full display
  dimensions, calls that entry directly, and cleans 16 argument bytes. This
  ties the validated register/stack ABI to `scrPlaceView[0]` rather than merely
  relying on an address label.
- The main SP refdef viewport begins at preferred VA `0x03520338`; only its
  four directly observed x/y/width/height integers are exposed. Stereo scene
  construction restores the stock full refdef after its two packed calls. On
  the validated WinMain call boundary, immediately before the original
  `Com_Frame`, the mod therefore refreshes `scrPlaceView[0]` from that stock
  width/height every valid frame so renderer Reset and `vid_restart` cannot
  strand stale dimensions.
- T4's sole static `CG_Draw2D` call is preferred VA `0x004628AB` (RVA
  `0x000628AB`) with bytes `E8 F0 5F FD FF`, targeting preferred VA
  `0x004388A0`. The independent 35-byte caller context begins with the exact
  projection-set-2D call and reloads `EAX` from the local-client register
  immediately before `CG_Draw2D`. The same validated caller reaches
  `CG_DrawActive` later at preferred VA `0x00462918`; that path reaches the
  hooked `R_RenderScene` call at preferred VA `0x00438C57`. A transparent
  five-byte call replacement
  preserves flags and all general registers, refreshes `scrPlaceView[0]` from
  the current same-frame catcher/state, and tail-jumps to the stock target with
  its original return address and EAX argument. Half-width placement additionally
  requires the exact frontend scene patch and backend hook to revalidate their
  ownership plus a non-consuming, valid OpenXR frame publication newer than the
  last frame consumed by the scene hook. This exact draw order preserves the
  current frame before scene consumption while rejecting a stale valid-looking
  publication retained across a Reset/recovery early return. CA_ACTIVE by itself
  can never shrink desktop or failed-XR output.
  Unpaused active gameplay then uses `width/2` by full height so the one native
  `CG_Draw2D` stream is eye-local when replayed; every catcher, unavailable-XR,
  frontend, loading, and cinematic state restores full width by full height.
- Placement binding, state reads, dimension caching, and every proprietary setup
  call are serialized under one process-lifetime exclusive lock. The first
  pre-`Com_Frame` service captures the validated WinMain thread ID; the CG bridge
  cannot establish or bypass it. Teardown requests quiescence while those game-
  thread boundaries remain active. The next boundary drains any older call,
  forces full dimensions (falling back to the last valid full refdef size during
  a transient Reset), then acknowledges completion. Only an acknowledged full
  write permits the service gates, patches, and binding to be disabled/restored/
  cleared. A bounded timeout leaves the request and transparent hooks active so
  a later game frame can still correct half-width state; engine setup is never
  called by the bootstrap thread. The validated original CG target remains
  published for the process lifetime so any in-flight bridge can safely finish
  its stock tail jump. `scrPlaceFull` and `scrPlaceFullUnsafe` are never modified.
- The exact T4 `CL_KeyEvent` entry is preferred VA `0x004780F0` (RVA
  `0x000780F0`). Its prologue independently establishes the cdecl arguments
  `(localClientNum, key, down, time)`. The 42-byte entry sentinel ends on a
  complete instruction, and the separate native dispatch context beginning at
  RVA `0x0019B67F` validates T4's own four-argument call ordering.
- The exact T4 `UI_MouseEvent` entry is preferred VA `0x005BB4F0` (RVA
  `0x001BB4F0`). Its code reads two integer stack arguments, converts them to
  floats, divides by the native display width and height, and stores the
  resulting UI cursor coordinates. T4 SP therefore has the two-argument cdecl
  ABI `(x, y)`, not a guessed local-client parameter. Its 43 observed entry
  bytes and the native `CL_MouseEvent` caller context at RVA `0x0023D9A0` are
  both validation-only sentinels.
- Direct inspection of the exact key table and dispatch comparisons establishes
  T4 key numbers Enter `0x0D`, Escape `0x1B`, arrows `0x9A`-`0x9D`, and Mouse1
  `0xC8`. Controller A and B use balanced down/up pairs at the same engine time;
  the left Menu button emits Escape only after a sub-second release. A one-second
  hold is consumed by recenter and cannot emit Escape on release.
- Native UI cursor coordinates are supplied in complete D3D9 backbuffer space.
  The exact T4 gameplay pause UI is queued after both packed scene-eye draws and
  remains full-backbuffer by default. Explicit left/right presentation-source
  probes add the selected half's x origin and clamp within that source.
- These engine UI calls run only from the already validated WinMain
  post-`Com_Frame` service. They are never made by the D3D9 Present callback or
  an OpenXR/controller polling worker. UI catcher `0x10` owns the controller
  cursor and A/B while active; gameplay usercmd injection is suppressed until
  native active gameplay resumes with no engine key catcher.
- The exact `Cbuf_AddText` entry is preferred VA `0x00594200` (RVA
  `0x00194200`). Its first complete instructions copy `EAX` into the preserved
  command-text register and `ECX` into the command-buffer index register before
  entering the native command-buffer critical section. An immutable WinMain
  call context at RVA `0x001FF78D` loads the text pointer into `EAX`, clears
  `ECX` for buffer zero, and directly calls that entry immediately before the
  same main-loop `Com_Frame` boundary used by the mod.
- The `weapnext` string is directly referenced at preferred VA `0x0084D130`.
  The identity context at RVA `0x00036269` pairs it with handler
  `0x00469DE0`; the adjacent registration record context at RVA `0x00036298`
  stores that name and handler in T4's native command list. A separate 47-byte
  sentinel validates the handler entry. The mod therefore queues the owned
  newline-terminated text `weapnext\n` through buffer zero instead of guessing
  or incrementing usercmd byte `+0x14`.
- Weapon-next is sampled on the validated post-`Com_Frame` WinMain thread and
  emitted only on a fresh left-secondary/Y rising edge during focused active
  gameplay with `keyCatchers == 0`. Entering gameplay while Y is already held
  baselines the hold; UI closure or focus regain cannot leak a weapon switch.

- Preferred VA `0x0063A430` is identified by the direct
  `CL_GetUserCmd: %i >= %i` string reference. Its index arithmetic masks with
  `0x7F`, multiplies by 56, addresses the ring at `0x030FD700`, and copies 14
  dwords. It reads the current command number at `0x030FF300`.
- `CL_CreateNewCommands` at `0x0063E940` increments that same command number,
  calls the 56-byte command builder at `0x0063E850`, and at `0x0063E96C`
  begins copying the completed command returned in `EAX`. The 13 profiled bytes
  cover complete instructions and the five-byte detour minimum is one complete
  instruction.
- The command-button builder at `0x0063E270` directly ORs the masks named in
  `usercmd.hpp` into dword offset `+0x04`; the movement builder independently
  contributes sprint and aim-down-sights. The finalizer at `0x0063E4C0`
  directly writes the pitch/yaw fixed-angle words at `+0x1C/+0x1E`.
- That exact button builder distinguishes frag `0x00004000` from
  smoke/tactical `0x00008000`. Nacht's right-controller squeeze/grip now ORs
  only the verified frag bit, preserving any native buttons already present.
- The completed SP command stores `meleeChargeYaw` as a float at `+0x28` and
  `meleeChargeDist` as a byte at `+0x2C`. `PM_MeleeChargeStart` at preferred
  VA `0x00419F20` starts target rotation/lunge only for a nonzero distance;
  zero enters the engine's native clear path. The post-build VR bridge zeros
  both fields on every enabled command while leaving melee button `0x4`
  untouched, so ordinary knife initialization/animation/damage still run.
- `AimAssist_UpdateGamePadInput` has the exact auto-melee dvar/current-value
  and target qualification context at RVA `0x0000331C`. Its six-byte branch at
  RVA `0x00003334` (`0F 84 03 02 00 00`) normally permits target pitch/yaw.
  Replacing it with `E9 04 02 00 00 90` jumps from the already-established
  prologue to the function's own clear/reset epilogue at preferred VA
  `0x0040353D`. This is validated by the complete 48-byte context sentinel and
  remains necessary because `aim_automelee_enabled` is a cheat dvar that map
  initialization can reset despite a launcher command-line value.
- Authoritative code at `0x004E8D10` reads those exact two command words,
  multiplies by the directly referenced `360/65536` float, and stores degrees
  at client offsets `+0x2258/+0x225C`. The profiled bytes cover that read and
  conversion start.
- The function at `0x00551380` obtains the entity's client, chooses those gun
  angles for its weapon axes, and calls the image's angle-vector routine before
  applying the native gun-origin offsets. Its entry bytes are an additional
  validation-only sentinel.
- `FireWeapon` at `0x00551590` constructs its 64-byte `weaponParms` on the
  stack. Its primary-fire call at `0x005515E3` has bytes `E8 98 FD FF FF` and
  targets `CalcMuzzlePoints` at `0x00551380` with `EAX=weaponParms*` and the
  firing entity at caller stack offset `+4`. A separate 48-byte context
  sentinel beginning at `0x005515CD` verifies that construction and ABI before
  allowing the call replacement. The other direct CalcMuzzlePoints callers are
  auxiliary/offhand paths and are deliberately untouched.
- The ordinary-bullet path calls `Bullet_Fire` at `0x004E6810`. Direct data-flow
  inspection shows that it takes its start/base from
  `weaponParms+0x24/+0x28/+0x2C` and its forward/right/up spread basis from
  `+0x00/+0x0C/+0x18`. Replacing the three origin floats after the original
  calculation is the default physical-muzzle handoff. The exact Mosin profile
  additionally replaces all three basis vectors from the same fresh rendered
  snapshot; the rendered IW forward/left/up contract is converted to
  WeaponParms forward/right/up by negating left. That opt-in is atomic and
  fail-closed, while Kar98, Springfield, Type 99, and non-profiled SP firearms
  preserve their native basis.
- `FireWeapon` calls that ordinary-bullet function at preferred VA
  `0x005516BE` (`E8 4D 51 F9 FF`) with `ESI=weaponParms*`, the attacker at
  caller stack offset `+4`, and the final cone scalar at `+8`. The adjacent
  44-byte context sentinel proves the bullet-only branch, argument construction,
  call, cleanup, and return. `Bullet_Fire` consumes its second argument at
  preferred VA `0x004E6904`; its profiled entry and 33-byte spread-consumer
  sentinels prove both sides of that ABI.
- The exact asset-field table identifies `adsSpread` as a float at weaponDef
  offset `+0x830`. For a local firearm shot while the VR weapon hook is enabled,
  only the final stack scalar is replaced with this finite nonnegative
  per-weapon value. The
  engine's ADS fraction and aim-spread scale are not modified, so movement,
  FOV, sensitivity, animation, recoil, ammo, AI fire, and non-bullet weapons
  retain native behavior. A nonzero shotgun ADS cone is deliberately kept.
- `CG_DrawBulletImpacts` independently builds the local predicted shot. Its SP
  spread call at preferred VA `0x00468DCC` (`E8 4F 4D FB FF`) targets
  `0x0041DB20` with `ESI=playerState*`, `EDX=minSpread*`,
  `ECX=maxSpread*`, and `weaponDef*` at caller stack `+4`. The immediately
  following view-origin call at `0x00468DDF` (`E8 1C FC FF FF`) targets
  `0x00468A00` with `EAX=localClientNum`, `playerState*` at `+4`, and the
  output vector at `+8`. Exact call-context and helper-entry sentinels validate
  both custom ABIs. Each wrapper runs native first; only local client zero with
  a successful native origin may receive the fresh published `tag_flash`, and
  only finite type-zero `adsSpread` collapses the returned visual min/max.
  Pellet count, endpoint seeding, penetration, and authoritative server hit
  events remain native.
- The downstream SP-only diagnostic seam is preferred VA `0x00467E81`, the
  exact five bytes `83 C4 0C 84 C0` immediately following
  `FireBulletPenetrate`'s first native `BulletTrace` call at `0x00467E7C`.
  The surrounding 36-byte context, `FireBulletPenetrate` entry, client
  `BulletTrace` entry, and upstream `CG_DrawBulletImpacts` call/context are
  independent validation-only sentinels. The replacement observes the native
  AL result and preserved ESI/EDI/EBP/EBX context, logs no more than 24 exact
  Mosin traces, then replays the displaced cleanup/test byte-for-byte. It does
  not intercept `BulletTrace`, alter trace inputs/results, or exist in MP.
- The same mapped retail image reaches its impact selector through the exact
  call at preferred VA `0x0046B7A1` (`E8 9A FD FF FF`), targeting
  `0x0046B540`, and its final oriented-FX spawn through the exact call at
  `0x0046B99B` (`E8 10 1D 04 00`), targeting `0x004AD6B0`. Instruction-aligned
  caller contexts and both native entries independently pin the custom
  register/stack ABIs. Each five-byte replacement calls its decoded native
  target exactly once, snapshots only the post-native state, restores all
  registers/flags/x87/SIMD state, and resumes at the stock caller cleanup.
  The outer frame value at saved anchor `A+0x7C` is the map-local
  `weaponIndex`, matching the selector's native `EAX` table lookup; the
  diagnostic resolves it through the pinned SP weapon table rather than
  treating the integer as a `WeaponDef *`. Bounded raw-arrival logging retains
  the index and identity/profile result even when that resolution fails.
  In particular, the spawn wrapper preserves native `EAX` for the immediately
  following effect `DelRef` call.
- Exact-profile SP data symbols expose only the read-only FX no-marks byte at
  preferred VA `0x016CFD58` and allocation/freed counters at
  `0x016CFD5C/0x016CFD60`. Mapped text references at `0x004A862D`,
  `0x004A7E3A`, and `0x004A7CBC` independently identify those fields. The
  diagnostic records selector-before/spawn-after values but does not infer
  failure from an immediate zero delta: native mark elements may be allocated
  during a later FX update.
- This executable registers `cg_firstPersonTracerChance` with default `0.0`;
  the local branch reads it before emitting a first-person tracer. The launcher
  explicitly sets it to `1`, enabling the native tracer without replacing its
  renderer or event path.
- The `g_entities` array begins at `0x0176C6F0` with stride `0x378`; the solo SP
  local player is element zero. The runtime additionally requires
  `s.number==0`, a non-null client at `+0x180`, and ordinary-bullet weapon type
  zero or projectile type two at `weaponParms->weapDef+0x144` before using a
  fresh corrected viewmodel `tag_flash`. The Ray Gun dispatch follows this
  same profiled `CalcMuzzlePoints` construction and consumes the resulting
  `WeaponParms`; accepting type two therefore changes its physical origin
  without reimplementing projectile behavior. Fixed-spread overrides remain
  restricted to type zero.
- Client gun-degree globals at `0x0352B65C/0x0352B660` are read by local weapon
  calculations. A separate directly observed copy routine feeds the globals
  serialized by the command finalizer. Only these two four-byte locations are
  exposed; the surrounding client structures remain unsupported.
- `CG_ApplyViewAnimation` calls `CG_UpdateViewModelPose` at preferred VA
  `0x0042DD10`, then passes the viewmodel pose, gameplay refdef axis, and
  gameplay refdef origin to the `tag_camera` matrix lookup at `0x0042DD2D`.
  Its caller cleans three arguments, tests `EAX`, and applies the returned
  matrix to the refdef only on success. The exact five-byte call and surrounding
  46-byte context are profiled. Replacing only that call with
  `xor eax,eax; nop; nop; nop` retains native reload/viewmodel animation while
  taking the function's existing no-camera-tag path and preserving the required
  physical-camera semantics.

This chain is why the initial controller-aim implementation uses the native gun
angle fields and native muzzle calculation. Candidate `tag_flash`, weapon-bone,
and proprietary structure addresses found elsewhere have not been promoted to
the exact profile.

## Campaign rocket-barrage and air-support targeting

The `pel1.ff` payload (SHA-256
`20120b64bf1e54b36653d54616f5c650a2baae3280d306230576f854f95db11e`)
contains the native `maps/_dpad_asset.gsc` logic behind the beach rocket-strike
prompt. Its targeting loop calls `self getPlayerAngles()`, converts those
angles with `anglesToForward`, and traces from `self getEye()` out 4000 units.
The fire watcher separately consumes `attackButtonPressed()`. This proves why
the ordinary tracked gun shots can fire the designator but cannot steer the
mission marker.

- Native keyboard `6` is `+actionslot 4`, which selects the
  `rocket_barrage` radio/designator. The controller mapping sends that exact
  key through the already validated `CL_KeyEvent` path; it does not guess a
  usercmd mask.
- `GScr_GetPlayerAngles` is registered at preferred VA `0x0083C0DC` and
  resolves the script entity in `ESI`. Its single call to `Scr_AddVector` is
  at preferred VA `0x004EE898` (`E8 A3 C0 1A 00`) and targets
  `0x0069A940`. Registration, handler-entry, call-context, and callee-entry
  sentinels all have to match before those five bytes are eligible.
- The bridge changes only the float-vector pointer passed into
  `Scr_AddVector`. It never writes player angles, view angles, camera state,
  or HMD state. Every rejected gate tail-calls the original function with the
  identical native vector pointer.
- The exact local entity must be `g_entities[0]`. Its client pointer is read at
  `+0x180`; the current weapon ID comes from client `+0x104`, with the native
  byte fallback at `+0x20F8` only when the primary is zero.
- Weapon definitions are read from the proven 256-pointer table at preferred
  VA `0x008F6770`. Registration code at `0x0041D360` increments the live count
  at `0x046DE3BC` and stores the new definition in that table, proving IDs are
  valid only in the inclusive range `1..count`. Runtime additionally requires
  `count<=255` and an exact NUL-terminated campaign designator name:
  `rocket_barrage` or `air_support`.
- Only a current, focused controller frame may supply `{pitch, yaw, 0}` using
  the same anchor-relative `controller_aim_degrees` path as SP weapon input.
  The designator does not need to publish a normal viewmodel pose; head motion
  remains independent of the target ray.

These sites and data symbols exist only in the exact SP profile. MP has explicit
absence tests, and shutdown disables the campaign bridge before the tracked
weapon bridge while leaving both proven calls patched as disabled pass-throughs
for process lifetime.

### Okinawa air-support extension (2026-09-05)

The local reviewed `WAW-GSC/maps/oki3_dpad_asset.gsc` selects `air_support`
and uses the same `getPlayerAngles -> anglesToForward -> getEye -> bulletTrace`
target loop. Its fire watcher separately checks `attackButtonPressed()` and
the mission's allowed strike targets. Live PID20420 on `maps/oki3.d3dbsp`
identified map-local weapon 20 as `air_support`; the old bridge rejected its
name before reaching the controller-angle gate.

The old reader required a NUL at byte 14, the length of `rocket_barrage`.
`air_support` instead terminates at byte 11. The fixed bounded 15-byte reader
resolves the first NUL, validates the actual name against the exact two-entry
allowlist, and passes that actual length to the existing gate. It does not
accept prefixes, multiplayer variants, or ordinary firearms. Native mission
target restrictions, entity/frame/focus checks, camera independence, and
trigger confirmation are unchanged. Unit tests include nonzero bytes after
the shorter name's terminator and both designators through all failure gates.

## Exact multiplayer executable observations

The separately supported multiplayer image is the local 1.7.1263 executable
with file size `5,505,024` bytes and SHA-256
`943bb93001ad2ed465b6652c27fb649b5f0c5b24097e18a27a588ac35b3457a0`.
It is selected only by exact identity and has its own immutable profile; no SP
address is inferred by applying a fixed delta.

- An instrumented exact-profile listen-server run on 2026-08-07 observed MP
  connection states `5`, `7`, `6`, and `8` while loading, then `10` for the
  team/class UI and the same `10` after its `0x10` catcher cleared. The prior
  assumed MP-active value `9` consequently blocked both stereo presentation
  and snap-turn; live transition telemetry corrected MP to the same active
  value `10` used by SP. MP's completed `usercmd` is independently established
  as `0x2C` bytes rather than `0x38`; the common view/button/movement prefix is
  validated independently, while the final `0x0C` bytes remain opaque and are
  always preserved.
- Exact MP disassembly at VA `0x004950EA` shows catcher `0x10` routing
  Mouse1-down through `CL_MouseInputShouldBypassMenus` at `0x00494B00`; the
  down event can be swallowed while the later up event still reaches
  `UI_KeyEvent`. `UI_MouseEvent` at `0x0059BDD0` updates the focused item, and
  the exact `Menu_HandleKey` table maps Enter `0x0D` to its item-action path.
  Active MP team/class confirmation therefore uses a balanced Enter tap;
  frontend and SP confirmation keep balanced Mouse1.
- The viewmodel call at RVA `0x0007FC68` targets `CG_AddPlayerWeapon` at RVA
  `0x0007F410`. Unlike SP, MP passes `centity*` in `EAX` plus four cdecl stack
  arguments. Dedicated x86 entry/original-call adapters are therefore required;
  the SP five-stack-argument bridge is not ABI-compatible.
- `CG_UpdateViewModelPose` at RVA `0x0007D7D0` retains the EAX-only DObj ABI,
  while `CG_DObjGetWorldTagPos` at RVA `0x000464E0` retains the observed
  EDI/ECX plus two-stack-argument contract. Independent pose, composition,
  DObj/XModel-layout, renderer hide-mask, and hidden-surface-cull sentinels are
  validated before the gun-only viewmodel filter is enabled.
- MP gameplay refdef origin/axis are at RVAs `0x005E6788/0x005E6798`;
  viewmodel root origin and evaluated pose are at `0x005EEC28/0x0068C368`.
  The five tag-word locations are independently proven by their native string
  registration stores rather than copied from SP.
- MP `g_entities` begins at RVA `0x01B8E300`, has stride `0x330`, and keeps its
  client pointer at `+0x184`. The local ballistic handoff requires the exact
  element-zero address, entity number zero, and non-null client, so bot and
  remote entities never enter the VR override.
- `weaponParms` remains `0x40` bytes with basis vectors at `+0x00/+0x0C/+0x18`,
  muzzle origin at `+0x24`, and `weaponDef*` at `+0x3C`. Weapon type `+0x144`
  and ADS spread `+0x830` are separately observed in MP code.
- The primary CalcMuzzle call at RVA `0x00145D13` retains `EAX=weaponParms*`
  plus the firing entity at stack `+4`. The ordinary Bullet_Fire call at RVA
  `0x00145D9A` retains `ESI=weaponParms*`, attacker at `+4`, and spread at
  `+8`; the existing state-preserving bridge shapes are compatible only after
  the MP entity layout is selected.
- MP's independent local predicted-shot calls are at RVAs `0x0007EA42`
  (`GetSpreadForWeapon`, target RVA `0x00020620`) and `0x0007EA55`
  (view origin, target RVA `0x0007E6C0`). Their register/stack ABIs match the
  separately profiled SP helpers, but their call bytes and surrounding
  contexts are independently pinned to the MP hash. Native-first wrappers
  apply the same local-only fresh `tag_flash` origin and finite type-zero ADS
  min/max policy.
- MP also registers `cg_firstPersonTracerChance` at `0.0`; the shared launcher
  override to `1` is consequently required for visible native local tracers.
- MP server firing code does not consume the SP/Wii command gun-angle shorts.
  For the exact local listen-server shooter only, a fresh controller generation
  therefore supplies the completed forward/right/up basis as well as the
  corrected `tag_flash` origin. Native bases remain untouched for bots, remote
  players, stale input, non-firearms, and all rejected identity gates.
- The user-owned PeZBOT `StartNormal` script consumes `svr_pezbots`, creates
  that roster, resets all three count dvars to zero, and then waits for another
  nonzero request. Live MP telemetry proves both an initial/frontend path
  `0/5/7/6/8/10` and a connected map-change path
  `10/6/7/6/8/10`; the latter's corresponding game log records `ExitLevel`,
  the next supported map's `InitGame`, and only the human joining. WaWVR
  consequently reuses the already validated MP `Cbuf_AddText` binding to
  assign the verified launcher's nine-bot request once at state `0`, or once
  on the first post-Com_Frame departure from a previously observed active
  state. The departure latch spans the complete non-active sequence and never
  queues on entry to active state `10`, so late first-map bot creation cannot
  race a second roster. If PeZBOT also restores allies/axis counts during an
  automatic rotation, `StartNormal` consumes the main request first and clears
  all three counts in that same pass.

Every five-byte call instruction used for validation, patching, rollback, and
restoration is copied from the selected profile. An MP failure path must never
write SP call bytes back into the process.

Both executables prepare FX visibility from the stock/body camera before the
late stereo HMD transform (`FX_SetNextUpdateCamera` calls at SP
`0x0046270B` and MP `0x00479FBD`). Element draw culling against that earlier
camera can therefore discard blood, projectile trails, or explosions that the
later headset view can see. The launcher keeps stock
`fx_cull_elem_spawn 1` and disables only `fx_cull_elem_draw`. The split policy
was selected after live SP inspection found the fixed effect pool exactly full
(`firstFreeEffect - firstActiveEffect == 1024`), including 604 repeated
`env/light/fx_light_god_rays_small` effects, while the valid Mosin impact
effect failed allocation. It does not replace FX simulation or force client
hit events, avoiding duplicate server-authoritative blood/impact effects.

## External comparison sources

The public t4-rtx project was used as a research index to identify candidate
subsystems worth checking, from locally inspected commit
`ade9b2fe9d4101c093169bcc4fee86e88bef7296`. No compatible licence was found
in that checkout. No source implementation, hook utility, structure definition,
bundled script, fastfile, IWD, or other asset from that repository is present
here. Candidate addresses became supported facts only after independent
instruction and data-flow verification against the exact local executable.

The validated implementation modifies a completed command and lets the
engine's muzzle calculation consume independent gun angles. It keeps generic
ADS/player state native and overrides only the completed local-firearm scalar
at the validated T4 boundary.
The matching T4 boundaries, sizes, offsets, and masks in this directory are
supported by the direct observations listed above.

## Retail tracked-tank command route (2026-09-04)

Verified against the allowlisted Steam SP executable SHA-256
`732900D158982C33E3121F0B86D22230BE79839BBCBFE3BDFC1238F408A7D64D`:

- Native camera mode selector at `0x42DB60`: predicted playerState
  `0x0351DF50`, vehicle flag `+0xCC & 0x4000`, vehicle entity `+0x83C`,
  seat index `+0x840 == 0` for the driver. The reconstructed CoD3 `0x20000`
  vehicle flag and vehicle-type interpretation of `+0x840` do not apply.
- Native CL pitch/yaw `0x0307D6D0/0x0307D6D4`, pitch writes at
  `0x42DB26/0x42E886`. Native delta angles start at playerState `+0x7C`;
  therefore client angles are not unconditionally world-space aim.
- Live See2 vehicle entity337 (`0x017B57E8`) had eType13 at `+4`, local
  owner `uint16 +0x178 == 1`, and scr_vehicle pointer at `+0x18C`.
  Vehicle definition index is int16 at scr_vehicle `+0x1CC`. The definition
  table is `0x04780D80`, stride `0x8FC`, type int16 `+0x40 == 1` for tanks.
  The live definition was `see2_ot34`, index7.
- `VEH_UpdateWeapon 0x53A750` calls native player view direction at
  `0x53A988 -> 0x411650`, then builds the target trace at vehicle `+0x400`.
  `VEH_UpdateAim 0x538E20` enforces mechanical limits at
  `0x5391F0..0x53928A`. Definition fields `+0xEC/+0xF0` are left/right yaw
  spans, `+0xF4/+0xF8` elevation/depression, and `+0xFC` rotation rate.
  Live values were 180/180/30/15/45 degrees (rate per second).
- Actual cannon angles are entity `+0x64/+0x68`, not mounted-gun
  `+0x54/+0x58` (vehicle jolt fields). Simulator assertions must inspect
  the real cannon and native projectile, not the unused handheld gun shorts.
- OT-34 flame uses frag bit `0x4000`, corroborated by native auxiliary
  weapon selection `0x53A21B` and command test `0x53A3DC`, plus the locally
  extracted `see2.gsc` `fragButtonPressed()` tutorial.

The mod integrates stick deltas into completed native usercmd view angles and
matching CL angles, leaving native turret aim, firing, and limits intact. It
does not substitute HMD or controller pose for vehicle aim. Tank identity is
validated before bypassing handheld actions and the independent stance/menu
command producer. All addresses remain disabled for multiplayer.

### Tank cannon reticle (2026-09-04)

The same exact Steam SP image's decrypted text independently establishes the
vehicle-only branch of `CG_DrawCrosshair` at `0x0043E0D0`:

- `0x0043E1BE` tests playerState `eFlags & 0x4000`. Its vehicle branch calls
  `0x0043D9C0` at `0x0043E1D9`, with exact bytes `E8 E2 F7 FF FF` and the
  native local-client argument in `EDI`. This is separate from stationary
  `CG_DrawTurretCrossHair` at `0x0043D880`.
- The vehicle function fetches `weaponDef +0x29C` (reticle material), checks
  `cg_crosshairAlpha` through the pointer at `0x034660F4`, and draws the
  reticle centered through `0x0063B630`. It does **not** test
  `cg_drawCrosshair` or `cg_drawTurretCrosshair`; disabling those firearm
  dvars therefore does not remove this head-centered vehicle HUD image.
- `tank_reticle_hook.cpp` replaces only that five-byte call after exact SP
  profile identity, a 33-byte branch-context sentinel, a 14-byte target-entry
  sentinel, and the decoded original call target all match. MP is excluded.
  Suppression requires the validated local tracked-tank context, active
  gameplay without UI catchers, and current-frame stereo readiness; every
  rejected gate preserves the native call. The bridge preserves GPRs, flags,
  stack, x87 and SSE state. It does not suppress the compass or other HUD.
- The cannon target is a world point at `scr_vehicle +0x400/+0x404/+0x408`.
  `VEH_UpdateWeapon` first writes the long aim-ray endpoint there at
  `0x0053AA3F..0x0053AA4F`, traces at `0x0053AA8F -> 0x005043D0`, and
  interpolates to the hit fraction at `0x0053AABC..0x0053AB02`. Thus the point
  has native trace depth, not merely screen-center coordinates. The stereo
  scene captures this validated finite target with its render-frame camera
  and projects it independently into both eyes for the replacement marker;
  head movement changes its screen location rather than the cannon target.

The integrated build and OpenXR Simulator showed the world marker in both
eyes with the native compass retained. This is simulator evidence, not a
claim of physical-headset comfort or final user acceptance.

### Physical-scope HUD replay (2026-09-21)

The local saved `wawvr-live32-72468.dmp` identifies the Steam SP
`CoDWaW.exe` module at `0x00400000`, image size `0x04B11000`, timestamp
`0x4AEA1F46`. Independent read-only x86 disassembly establishes the following
facts; no executable bytes or game assets are distributed with this change.

- `CG_GetWeapReticleZoom` begins at `0x0043D000`. Its register ABI takes
  `cg_s*` in EDX and a writable float output in ESI, and returns its boolean
  result in AL without consuming stack arguments. The code initializes the
  output to zero at `0x0043D030`, reads the predicted ADS fraction at
  `cg_s + 0xAADA8` (`0x0351E060` for local cg `0x034732B8`), and returns
  false when that fraction is zero. Its positive zoom threshold is 0.01.
- `CG_DrawWeapReticle` at `0x0043D730` calls that predicate at
  `0x0043D73D`, bytes `E8 BE F8 FF FF`; a false result returns crosshair
  alpha 1 without queuing `CG_DrawAdsOverlay`. The true branch calls the
  native overlay at `0x0043D7B5 -> 0x0043D3E0`.
- The handheld branch of `CG_ScopeIsOverlayed` at `0x0046BE80` calls the
  same predicate at `0x0046BEB9`, bytes `E8 42 11 FD FF`. Its scoped-turret
  branch returns true separately and is untouched. The UI expression's
  inlined equivalent calls it at `0x005B158F`, bytes `E8 6C BA E8 FF`.
  These two callers govern scope-sensitive HUD visibility, independently
  of the overlay drawing call.
- The reconstructed local source is a navigation/comparison aid:
  `cg_draw_reticles.cpp` identifies the zoom predicate and overlay caller;
  `cg_weapons.cpp` identifies `CG_ScopeIsOverlayed`; `ui_shared.cpp` shows
  scope-sensitive menus (`staticFlags & 0x20000000`) hiding through that
  predicate. `PM_UpdateAimDownSightLerp` delays and interpolates ADS exit:
  clearing the input bit does not guarantee a zero fraction that frame.
  Scissor only starts at overlay alpha above 0.99, so it cannot detect all
  partially faded native masks.

`physical_scope_hud_hook.cpp` replaces only the three five-byte calls listed
above, after the exact supported SP profile/hash/base, each decoded target,
a 48-byte target-entry sentinel, and 35/18/32-byte caller contexts match.
Other zoom callers, including weapon motion and viewmodel visibility, retain
the native function. The bridge returns false and writes zero only while a
fresh active physical-scope snapshot selected at the HUD boundary, valid active
gameplay without catchers, and current-frame stereo readiness all agree. HUD
placement, native predicate guards and scene construction share that frame's
optic decision; a frame hitch cannot expire one independently. Every rejected
gate tail-calls
the untouched native target. It preserves GPRs, EFLAGS, ESP and x87/SSE state,
apart from the documented AL/output result. Suppression is enabled only after
all three sites install; partial failure disables and rolls back owned sites,
and retains any failed rollback ownership for shutdown retry. Restoration
preserves foreign patches and leaves the native tail target valid for peers
already inside the bridge.

This permits the normal eyes to replay the native status HUD while the
dedicated magnified source remains overlay-free. The accepted physical lens,
optical zero, projectile basis, and scope visibility isolation are not changed
by this hook. Headset acceptance is a separate required retest.

## Distribution boundary

This repository must not distribute the examined game executable or Activision
assets. It should also not copy code from t4-rtx unless the copyright holder
provides a compatible licence and the provenance of its credited third-party
structures is resolved.

For Canadian users, Copyright Act sections 30.6 and 41.12 provide relevant
computer-program compatibility/interoperability context for local analysis and
adaptation. They are not a blanket licence to redistribute somebody else's game
binary, assets, or unlicensed source. This record is project provenance, not
individual legal advice.
