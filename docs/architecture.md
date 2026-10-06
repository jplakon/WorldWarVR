# Architecture

WorldAtWarVR deliberately separates reusable OpenXR code from build-specific
game bindings.

```text
launcher/
  executable discovery, validation, clean homepath, process creation,
  DLL injection, diagnostics

src/mod/
  injected DLL lifetime, logging, fail-closed feature activation,
  exact T4 swap-chain Present ownership and D3D9 Reset recovery

src/t4/
  exact WaW 1.7 executable profile, verified addresses/bytes, hooks,
  reconstructed ABI boundaries

src/xr/
  OpenXR lifecycle, frame state, actions, D3D11 swapchains,
  D3D9Ex/D3D11 interop boundary
```

The OpenXR layer must not include WaW engine headers. It communicates through
small interfaces such as `render_eye`, `capture_frame`, `mutate_usercmd`,
`query_pose`, `query_action`, and `emit_haptic`.

Game simulation must execute once per engine frame. Stereo rendering repeats
only the scene-render portion with per-eye view/projection data. All hooks are
installed only after the complete executable profile has been validated.

The first Zombies build preserves native scripts and logic. Points, rounds,
doors, wall buys, barriers, spawning, damage, and game-over behavior remain
owned by WaW.

## Exact executable variants and offline handoff

SP/Zombies and multiplayer are separate exact-profile variants, selected only
after the current process size, SHA-256, PE identity, complete hook bytes, and
data bindings validate. Variant-specific renderer addresses, connection-state
values, usercmd size, weapon-call ABI, entity layout, and rollback snapshots
never share inferred constants. The MP usercmd's common prefix is mutated while
its trailing opaque bytes are preserved.

Untouched Steam Build 252004 adds independently pinned SP/MP file identities.
Its wrapper code is exact-hash/PE validated on disk, receives a bounded runtime
restoration window, and must then pass the same complete mapped SP/MP sentinel
set before an existing layout family can be selected.

The stock SP frontend hard-codes `CoDWaWmp.exe`. Only inside WaWVR's isolated
SP runtime, the launcher copies itself under that name. This shim accepts no
foreign arguments, validates the exact sibling SP parent, and waits for that
process to exit and release OpenXR. SP preparation writes identity-bound V2
content to the historical `WaWVR-Multiplayer-Handoff.v1` filename beside the
shim, and only inside the validated isolated SP runtime. It captures the
resolved game directory, exact selected SP/MP identities, and MP source
executable (with trusted SP `--mp-source-exe` taking
priority over environment/discovery), SP-derived sibling MP runtime/home paths, and
source resolution. After the mandatory parent wait, the shim requires that
exact runtime-local file and rejects missing, malformed, non-canonical,
stage-mismatched, or unsafe paths; it never repeats environment, E-drive,
LocalAppData, or Plutonium fallback for game/source/runtime/home paths. The
managed shim also disables PeZBOT archive discovery/import and only verifies
an already prepared mod under the config-derived MP home. Neither Plutonium
nor a packaged proprietary MP executable participates in the handoff. The MP command line
forces a local listen-server posture and supplies that isolated MP home as
both `fs_homepath` and `fs_localAppData`; T4 MP otherwise mounts the bot IWD
without finding the adjacent `mod.ff`. Immediately before process creation,
the launcher also atomically normalizes only the isolated active MP profile's
archived `ui_dedicated` selector to `0`. This prevents a stale Start Server
choice from converting the sole VR process into a server-only console while
preserving the ordinary Activision profile and installed game. This write is
restricted to a default launcher-managed home or a validated stock handoff;
explicit caller-owned `--homepath` trees are not normalized. A
separately hashed import helper can install only an exact user-supplied PeZBOT
archive into the isolated MP home path; the package contains no bot or game
assets.

Standalone use intentionally permits game data and isolated runtime/home roots
on different local drives. For managed delivery, the trusted platform adapter
must anchor every fixed argument to its one stage, retain a receipt for the
prepared config, and recheck that receipt before the user's launch action. The
wrapper's portable cross-drive validation is not a replacement for that
platform-owned same-stage/integrity boundary.

Presentation ownership begins by validating all 40 bytes at T4's exact
`RB_SwapBuffers` entry (`0x006FBE50`), then follows its selected-window globals
to `DxGlobals.windows[target].swapChain`. It atomically replaces only
`IDirect3DSwapChain9::Present` vtable slot 3 and chains the original function.
It also replaces only `IDirect3DDevice9::Reset` slot 16 for reset recovery. It
never changes either live COM object's vtable pointer and never guesses or
clones a driver-specific complete vtable. Present captures the stock
backbuffer through the deliberately slow D3D9 CPU fallback, uploads it to the
OpenXR-owned D3D11 device, and samples the complete frame for both eyes. Reset
invalidates D3D9 capture data and rebuilds compositor resources on the next
Present. A missing/inactive OpenXR runtime is non-fatal; desktop Present always
calls the original game function. Set `WAWVR_DISABLE_XR=1` before launch as
the recovery switch for this gate.

## Frame and stereo path

The validated WinMain call to `Com_Frame` is the synchronization boundary.
WaW simulation and input run exactly once. During active gameplay, the exact
SP scene hook generates two `GfxViewInfo` records from the same immutable
frontend state, with per-eye OpenXR poses, projections, and half-width packed
viewports. The backend executes both records before the frame broker publishes
the packed image to the compositor. Renderer list ownership and every patched
callsite are checked against the exact supported executable before use.

Packed frames cross D3D9Ex/D3D11 through a four-slot, serial-keyed queue. The
post-`Com_Frame` owner polls producer completion once and consumes the oldest
ready image without sleeping or waiting on a GPU fence; an unfinished image
retains its exact pose, source layout, scope geometry, and presentation state
for a later boundary. The live OpenXR frame continues to own `xrEndFrame`,
while the consumed image's capture-time eye poses are submitted with its pixels
for coherent reprojection. Stereo/mono transitions abandon incompatible queued
captures before selecting a layer, and reset/shutdown retires the complete
shared generation.

When an exact scoped-weapon profile is held with both grips, the SP path follows
the accepted COD4 physical-optic design. It creates a third `1024x1024`
magnified camera beside the two `2496x2688` eye views, withholds the native 2D
scope mask, and projects that source into a weapon-attached circular lens in
each OpenXR eye. Only the third camera filters depth-hacked first-person XModel
surfaces through T4's audited `R_DrawSurfs` callsites; the normal eyes retain
the tracked rifle. Invalid identity, tracking, packed layout, draw-surface, or
entity data fails open to the ordinary two-eye renderer.

WaW queues `CG_Draw2D` once before scene rendering. WorldAtWarVR reuses that
same non-null command stream for both eye records. Immediately before the
stock draw queues it, an exact-profile bridge configures only
`scrPlaceView[0]` for one logical eye (`packedWidth / 2` by `height`). This
keeps ammo, points, round text, and interaction prompts inside each eye. Any
menu, console, cinematic, loading, invalid, or odd-width state restores the
full-frame placement; `scrPlaceFull` and `scrPlaceFullUnsafe` are untouched.
During valid unpaused packed gameplay, the same placement receives a
WorldAtWarVR binocular-safe rectangle. Ordinary HUD elements use a `0.42`
scale with `0.58` horizontal and `0.70` vertical safe-area fractions, pulling
the status clusters substantially inward for headset readability.
`scaleVirtualToFull` is preserved, so fades and damage overlays retain their
full extent. Menus and cinematics never receive the gameplay comfort transform.
MP additionally gates the packed scene call on its exact presentation state:
team/class/pause, console, loading, and other non-active/catcher states execute
one stock full-frame scene instead. This prevents an already packed two-eye
image from becoming the contents of the finite menu panel. Catcher-free
`CA_ACTIVE` MP gameplay resumes the two-eye path automatically. The established
SP/Zombies scene path is unchanged.

## Flat-screen presentation

The exact T4 connection state and key-catcher mask select presentation mode.
Frontend, loading, logo/video/cinematic, console, and active-menu frames sample
the complete native backbuffer identically for both eyes. Entering this mode
freezes a gravity-level position/yaw anchor and places an opaque core
`XrCompositionLayerQuad` two metres along its local `-Z`. The quad retains the
previously accepted `0.62` horizontal and `0.38` vertical half-angle, while its
finite Local-space pose supplies real binocular depth and translational 6DOF
parallax. Returning to unpaused `CA_ACTIVE` gameplay restores captured stereo
projection layers. Recovery caches the successfully accepted layer kind: a
static or temporarily unavailable D3D9 frame may resubmit the same released
quad during the same mono interval, or the prior projection during the same
stereo interval. A mono/stereo transition retires the complete prior interval.
Mono presentation is strictly quad-only; missing or invalid quad metadata
submits no visual layer for that frame instead of silently converting the menu
back to infinite depth. The runtime reports a layer as accepted only after a
successful `xrEndFrame`, and transition telemetry logs both requested and
accepted primitives.

## Input, weapon, and camera

OpenXR actions are sampled once and applied only while the runtime is focused.
During native UI ownership, the right aim pose's local `-Z` ray intersects the
visible front (`+Z`) of the same finite quad. Hits are rejected in compositor
pillar/letterbox bars, remapped through the selected full/packed source crop,
then sent through validated `UI_MouseEvent` coordinates. The compositor draws
an orange ring/dot procedurally into the same accepted quad swapchain at the raw
hit UV, so visibility does not depend on WaW or the Windows hardware cursor. A
fresh right-trigger edge sends one balanced native `K_MOUSE1` tap; off-panel or
already-held triggers cannot activate a stale cursor. Left stick/A/B follow the
fresh UI catcher even during a temporary capture gap, while ray position and
trigger confirmation still require the current accepted surface. Weapon-cycle
binding is optional and cannot disable mandatory menu input. Outside native UI,
controller actions require an active client and no key catcher; high-level
weapon cycling is queued through the exact `Cbuf_AddText` ABI only on a fresh Y
edge. Gameplay buttons retain each executable variant's native usercmd layout.

The completed T4 usercmd also contains melee-charge yaw/distance at
`+0x28/+0x2C`. The post-build bridge clears those dedicated fields on every
enabled command, including stale-controller frames; zero distance is T4's
native no-charge sentinel. Ordinary melee remains pressed, so the knife still
animates and damages without target-driven camera rotation/lunge. A separate
six-byte exact-profile branch replacement sends WaW's auto-melee aim path to
its own clear/reset epilogue, preventing target pitch/yaw even when map
initialization resets the cheat-gated dvar.

The gun viewmodel keeps WaW animation and required skeleton tags, hides only
hand/arm surfaces, and receives a controller-driven rigid transform from the
active owning controller. The corrected `tag_flash` position becomes the local
bullet origin while WaW still owns damage, penetration, recoil, and ammo.
Direction remains native by default; the exact Mosin profile atomically routes
its forward/right/up basis from the same fresh rendered-barrel snapshot to
cover the physically observed left-owner/right-bolt handoff. Kar98,
Springfield, and Type 99 remain on their native direction path.
At the exact local ordinary-bullet `FireWeapon -> Bullet_Fire` handoff, the
already-computed cone scalar alone is replaced with that weapon's authored ADS
spread for every local firearm shot while the VR weapon hook is enabled. This
removes the expanding desktop hip-fire cone without entering ADS state or changing
movement, FOV, sensitivity, animation, and shotgun pellet character. A
separate exact callsite suppression prevents reload/sprint/spawn
`tag_camera` animation from moving or rolling the HMD refdef.

Exact weapon profiles also own the physical reload transaction without
generalizing across unknown assets. Kar98, Springfield, Mosin, and Type 99
retain their physically accepted right-hand bolt cycling and feed behavior. The pending
`zombie_colt` vertical slice begins on A, blocks firing while open, reserves a
left-waist grip for a tracked magazine, and commits one native reload only when
that magazine is released in the exact `j_clip` well. Its detached render uses
the audited surface-0 prefix (162 vertices and 130 triangles): preceding gun
vertices collapse to zero-area geometry while the original CPU indices and GPU
index buffer stay aligned for both retail skin paths. Any asset, ownership,
tracking, focus, or renderer mismatch cancels the transaction and restores the
authored magazine visibility. The Colt slice remains headset-test-pending.

WaW independently constructs the local predicted tracer/impact ray inside
`CG_DrawBulletImpacts`; it does not reuse the authoritative server
`WeaponParms`. Two exact, local-player-only wrappers therefore run their native
helpers first, then replace the successful eye origin with the same fresh
published `tag_flash` and collapse the returned visual min/max cone to the
weapon's authored ADS spread. This keeps the visible tracer and predicted
impact on the authoritative tracked shot while preserving native penetration,
pellet count, damage, and server hit events. The physical-muzzle gate also
accepts projectile weapon type `2`, covering the Ray Gun paths that consume the
same `WeaponParms`.

Retained bounded Mosin diagnostics observe the native
first-trace result boundary and two later impact-effect calls. The trace seam
observes no more than 24 exact-Mosin shots after `BulletTrace` has returned,
records both the ray and the engine's hit/fraction/solid/surface result, and
then replays the displaced stack cleanup and AL test exactly. Native-first
wrappers around the impact selector and final oriented-FX spawn correlate no
more than 24 same-thread Mosin events, recording the selected effect/sound,
surface and contents, normal validity, returned FX handle, and read-only mark
allocation counters. Both downstream retail frames retain a map-local
`weaponIndex`, which the diagnostic resolves through the pinned SP weapon table
before applying the exact-Mosin filter; a separate bounded arrival record keeps
raw index and identity/profile-resolution failures observable. The selector,
spawn, trace, and effect outputs remain engine-owned. An immediate zero
mark-counter delta is observational only
because mark elements may be allocated during a later FX update. Multiplayer
has none of these diagnostic sites or patches.

FX visibility is prepared before the late stereo HMD camera is applied. Until
the FX camera itself is moved to a conservative head-centred frustum, the
launcher retains stock element spawn culling (`fx_cull_elem_spawn 1`) and
disables only element draw culling (`fx_cull_elem_draw 0`). The split policy
bounds the observed distant ambient-effect accumulation in WaW's fixed
1024-effect pool while preventing the body-camera frustum from discarding
already-spawned, HMD-visible blood, trails, and explosions. Effect simulation
and lifetime otherwise remain native.

Gameplay eye and controller composition use a gravity-level body basis: stock
body yaw is retained while legacy camera pitch/roll from recoil, melee, death,
and spawn animation is excluded. HMD pitch/roll remain physical OpenXR motion.
Both automatic first-gameplay capture and the one-second Menu recenter preserve
position/yaw while rebuilding a gravity-level tracking anchor, so recenter
always restores a level horizon.

Every invasive boundary has an inherited-process recovery gate. The launcher
and injected DLL recognize `WAWVR_DISABLE_XR`, `WAWVR_DISABLE_INPUT`,
`WAWVR_DISABLE_WEAPON`, `WAWVR_DISABLE_WEAPON_CAMERA_PATCH`,
`WAWVR_DISABLE_MELEE_CAMERA_PATCH`, and `WAWVR_DISABLE_DIRECT_BOOT`. Each gate
leaves the corresponding stock path intact or restores an owned patch during
shutdown; unsupported executable bytes still fail closed independently.
