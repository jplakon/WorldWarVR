# T4 SP HUD, menu, and Zombies interaction map

This note maps the smallest practical path from same-frame stereo rendering to
a headset-testable World at War Zombies build. It is intentionally limited to
the one executable profile below. It does not claim that these addresses apply
to another retail, Steam, disc, or Plutonium image.

## Evidence convention

- **Exact T4** means independently checked in the user's local executable.
- **Recommendation** means a clean-room T4 adaptation or a runtime test that is
  still required; it is not presented as a recovered T4 implementation.

The supported T4 SP image is version `1.7.1263 CL(350073)`, SHA-256
`F26D45524BFFF7E44C8EBAB4D758CA524EDFB0FB7D52352B6C95E1E908799361`,
with image base `0x00400000` and stripped relocations.

## Result in one paragraph

Two calls to T4's `R_RenderScene` do **not** inherently duplicate the HUD.
`CG_Draw2D` builds one 2D command stream before the first scene call, and T4
attaches a command-stream pointer to each `GfxViewInfo`. A second view therefore
needs an explicit policy: set its pointer to null for a world-only diagnostic,
or deliberately copy the first view's pointer to replay the same HUD in both
eye viewports. Never leave the second slot untouched, because front-end buffers
are reused and it may contain a stale pointer. The headset implementation uses
the controlled-replay policy: it clears the pending slot and then copies the
first view's 2D pointer into the second view during scene construction. It also
uses centered symmetric source FOVs and
remap those sources into the runtime's asymmetric eye FOVs in the compositor.

## Exact T4 draw order

The T4 equivalent of `CL_CGameRendering` begins at `0x00478D20`. Before calling
cgame it associates the current point in the render-command buffer with the
next view-info slot:

```text
0x00478D2A  A1 C4 B4 DC 03          mov eax, [0x03DCB4C4]       ; s_cmdList
0x00478D2F  8B 48 04                mov ecx, [eax+4]
0x00478D32  03 08                   add ecx, [eax]              ; cmds + used
0x00478D34  A1 98 B4 DC 03          mov eax, [0x03DCB498]       ; frontEndDataOut
0x00478D39  8B 90 D0 4D 14 00       mov edx, [eax+0x144DD0]     ; viewInfoCount
0x00478D3F  8B 80 D4 4D 14 00       mov eax, [eax+0x144DD4]     ; viewInfo base
0x00478D45  69 D2 80 6D 00 00       imul edx, edx, 0x6D80
0x00478D4B  89 8C 02 CC 58 00 00    mov [eax+edx+0x58CC], ecx  ; viewInfo.cmds
0x00478D71  E8 6A 94 FE FF          call 0x004621E0
```

`0x004621E0` is the exact `CG_DrawActiveFrame` entry. Near its end it queues the
2D projection, builds the HUD command stream, and only then renders the scene:

```text
0x0046289D  E8 EE 43 29 00          call 0x006F6C90  ; projection set 2D
0x004628A2  8B C5                   mov eax, ebp
0x004628AB  E8 F0 5F FD FF          call 0x004388A0  ; CG_Draw2D
0x00462918  E8 F3 61 FD FF          call 0x00438B10  ; CG_DrawActive
```

`CG_Draw2D` at `0x004388A0` has one static caller, `0x004628AB`, and returns at
`0x00438B00`. It does not call `R_RenderScene`. `CG_DrawActive` at `0x00438B10`
also has one static caller and performs the gameplay scene call using the main
SP refdef at `0x03520338`:

```text
0x00438C03  50                       push dword ptr [0x0351DF30]
0x00438C14  68 38 03 52 03          push 0x03520338
0x00438C57  E8 14 60 2A 00          call 0x006DEC70  ; R_RenderScene
0x00438C5C  83 C4 08                add esp, 8
```

The function at `0x006F6C90` is the exact T4 equivalent of
`R_AddCmdProjectionSet2D`. Its command payload proves that this is a 2D
projection command rather than another scene submission:

```text
0x006F6CC7  66 C7 00 15 00          mov word ptr [eax], 0x15
0x006F6CCC  66 C7 40 02 08 00       mov word ptr [eax+2], 8
0x006F6CD2  C7 40 04 00 00 00 00    mov dword ptr [eax+4], 0  ; projection 2D
```

The implication is important: simulation, cgame work, HUD generation, and
Zombies scripts should still run once. Stereo repeats only the completed
`R_RenderScene` portion with two cloned refdefs.

## Exact 2D command ownership

T4's current front-end output is reached through `0x03DCB498`. The fields used
by the supported binary are:

| Item | Exact value |
| --- | ---: |
| `frontEndDataOut->viewInfoCount` | `frontEndDataOut + 0x144DD0` |
| `frontEndDataOut->viewInfo` | `frontEndDataOut + 0x144DD4` |
| `sizeof(GfxViewInfo)` | `0x6D80` |
| `GfxViewInfo::cmds` | `viewInfo + 0x58CC` |

The view allocation path at `0x006DCEC0` increments the count and selects the
current `GfxViewInfo` with that exact stride:

```text
0x006DCEC6  mov ecx, [0x03BF8C28]
0x006DCEDC  mov [0x03BF8C28], ecx+1
0x006DCEE2  mov [frontEndDataOut+0x144DCC], old_index
0x006DCEEB  imul ebx, ebx, 0x6D80
0x006DCEF1  add ebx, [frontEndDataOut+0x144DD4]
0x006DCEF7  mov [frontEndDataOut+0x144DD0], ecx
```

The exact T4 equivalent of `R_ClearClientCmdList2D` is `0x006F56B0`. It writes
null to `viewInfo[viewInfoCount].cmds`:

```text
A1 98 B4 DC 03
8B 88 D0 4D 14 00
8B 90 D4 4D 14 00
69 C9 80 6D 00 00
C7 84 11 CC 58 00 00 00 00 00 00
C3
```

The stock executable calls this helper at `0x00479734`. The backend proves the
pointer is per view. In one standard path it does this:

```text
0x006E694D  8B 9B CC 58 00 00       mov ebx, [ebx+0x58CC]
0x006E6953  85 DB                   test ebx, ebx
0x006E6955  74 07                   je no_2d_commands
0x006E6959  E8 52 57 01 00          call 0x006FC0B0
```

An alternate backend path performs the same null test at `0x006E8289` and
calls the same command executor at `0x006E8293`.

### What the second scene call must do

The second slot must never be implicit. The hook should save the first pending
2D pointer, render the left eye, clear the now-current slot, and then choose one
of these explicit policies before rendering the right eye:

```text
saved2D = viewInfo[startCount].cmds
RenderScene(leftRefdef)

assert viewInfoCount == startCount + 1
R_ClearClientCmdList2D()             // eliminates stale reused-frame data

if binocularHud:
    viewInfo[viewInfoCount].cmds = saved2D

RenderScene(rightRefdef)
assert viewInfoCount == startCount + 2
```

- **World-only diagnostic:** leave the second pointer null. This proves stereo
  world rendering without risking duplicate or stale UI.
- **Headset MVP:** copy `saved2D`. The same immutable 2D command stream then
  executes under both views' display viewports. This is intentional replay,
  not a second `CG_Draw2D` pass.

Do not call `CG_Draw2D` twice, do not queue a second projection command list,
and do not permit the second scene call to advance game simulation.

### Controlled HUD replay

The stereo implementation contains both sides of the policy. Its
`CG_DrawActive` clears the pending right-eye slot before the second
`CL_RenderScene`. Its `R_RenderScene` path then detects view-info index 1 and
copies `viewInfo[0].cmds` into that newly selected view. Thus the final behavior
is controlled shared replay in both eye viewports. Looking only at the clear in
`CG_DrawActive` would incorrectly conclude that the pinned reference
suppresses the right-eye HUD.

## HUD convergence and projection

Replaying the command stream is necessary but not sufficient on headsets whose
OpenXR eye frusta are asymmetric. The reference implementation solves this in two
steps that can be adapted without copying proprietary engine material:

1. Render each packed T4 eye from a centered symmetric source frustum. For an
   OpenXR eye, use
   `symmetricX = max(-tan(angleLeft), tan(angleRight))` and
   `symmetricY = max(-tan(angleDown), tan(angleUp))` as T4's positive
   `tanHalfFovX/Y` values.
2. When copying that source half into the OpenXR eye image, use an oversized,
   offset destination viewport to map centered source NDC to the runtime's
   exact asymmetric frustum.

For the horizontal component, the reference comparison computes:

```text
spanX   = tanRight - tanLeft
scaleX  = 2 * symmetricX / spanX
offsetX = -(tanRight + tanLeft) / spanX

viewport.x     = 0.5 * (1 + offsetX - scaleX) * targetWidth
viewport.width = scaleX * targetWidth
```

The vertical calculation is analogous. This keeps a HUD element generated on
the centered source ray at the same perceived ray in both eyes, while the
OpenXR projection metadata remains the runtime-provided per-eye FOV.

For the first live test, controlled command replay is the required correctness
gate. The symmetric-to-asymmetric compositor mapping should be enabled before
judging crosshair convergence or long-session comfort; otherwise a working HUD
may look horizontally offset on a headset with strongly asymmetric frusta.

## Exact ScreenPlacement map

T4's `ScrPlace_SetupFloatViewport` is at `0x0047A1C0`. It writes the real
viewport width and height at `ScreenPlacement + 0x28/+0x2C`, uses the exact
4:3 constant at `0x008AFA84`, and uses the 640 and 480 virtual-screen constants
at `0x008AF58C` and `0x008AF520`. The object size is `0x48` bytes.

The contiguous placement globals are:

| Placement | Exact address |
| --- | ---: |
| `scrPlaceView[0]` | `0x00957318` |
| `scrPlaceFull` | `0x00957360` |
| `cg_hudSplitscreenScale` | `0x009573A4` |
| `scrPlaceFullUnsafe` | `0x009573A8` |

Renderer initialization calls the setup function for all three placements:

```text
0x00644CA0  mov edi, 0x009573A8
0x00644CCB  call 0x0047A1C0
0x00644CD8  mov edi, 0x00957360
0x00644CEE  call 0x0047A1C0
0x00644CFB  mov edi, 0x00957318
0x00644D11  call 0x0047A1C0
```

`ScrPlace_ApplyX` is exactly `0x0047A300`, and the full rectangle alignment
switch is exactly `ScrPlace_ApplyRect` at `0x0047A450`.

**Recommendation:** do not change placement globals for the first controlled
HUD-replay test. First capture where the stock HUD lands in each half. If the
gameplay HUD is scaled to the combined backbuffer instead of each eye-local
viewport, rerun the validated setup path for `scrPlaceView[0]` using the tested
HUD source region and refresh it after renderer initialization and every
`vid_restart`. Leave `scrPlaceFull` and `scrPlaceFullUnsafe` stock until menu,
fade, loading-screen, and cinematic call sites are tested. The pinned reference
updates all three for its own custom packed layout, including a dedicated scope
panel; that layout-specific choice should not be copied blindly into T4.

## Menu detection and comfort presentation

T4 exposes an exact, inexpensive menu predicate:

| State | Exact address/value |
| --- | ---: |
| `clientUIActives[0].keyCatchers` | `0x03058424` |
| UI catcher bit | `0x10` |
| connection state | `0x0305842C` |
| active gameplay state | `10` (`0xA`) |

The executable directly tests the UI bit at several sites, including
`0x004389FF`, and compares the connection state with `0xA` at `0x00478D21`.
The comfort-mode predicate is therefore `keyCatchers & 0x10`; an in-game pause
menu is the same predicate plus `connectionState == 10`.

The smallest comfortable presentation policy is:

1. On entry to UI-catcher state, stop presenting the menu with normal stereo
   head parallax.
2. Capture the menu-bearing source region once and submit that same region to
   both eyes with a centered pose and identical modest FOV. This creates a
   stable, zero-disparity comfort screen.
3. Restore normal per-eye poses/FOVs when the UI catcher clears.

When an active SP pause UI is painted into the right half of the packed frame,
the compositor crops the right half and presents it monoscopically. A T4
capture probe should identify which half is populated
before hard-coding the crop. Front-end and loading menus that occupy the full
backbuffer should use the full-frame mono path.

The reference controller-menu pattern is also suitable as a second pass: left
stick moves the native UI cursor, the confirm action emits a short native
primary click, and the back action emits Escape. Until T4's exact key-event ABI
is validated, the headset MVP can retain mouse/keyboard menu navigation while
the VR menu button uses the exact high-level `toggleMenu` command described
below.

## Exact T4 gameplay command path

The supported executable's SP user command has size `0x38`:

| Offset | Field |
| ---: | --- |
| `+0x04` | buttons |
| `+0x14` | selected weapon |
| `+0x16/+0x17` | forward/right movement |
| `+0x1C/+0x1E` | gun pitch/yaw |

The exact command-ring and builder facts are:

| Item | Exact address |
| --- | ---: |
| `CL_GetUserCmd` | `0x0063A430` |
| usercmd ring | `0x030FD700` |
| command number | `0x030FF300` |
| `CL_CreateNewCommands` | `0x0063E940` |
| completed-command hook point | `0x0063E96C` |
| command builder | `0x0063E850` |
| button builder | `0x0063E270` |
| `Cbuf_AddText` | `0x00594200` |

At `0x0063E96C`, the command has been built but has not yet left the client
input path. This is the safest place to preserve stock input and OR VR gameplay
buttons into the completed command.

The exact physical button masks emitted by T4 are:

| Action | Usercmd button |
| --- | ---: |
| attack | `0x0001` |
| sprint | `0x0002` |
| melee | `0x0004` |
| use / activate | `0x0008` |
| reload | `0x0010` |
| combined use-reload | `0x0020` |
| prone | `0x0100` |
| crouch | `0x0200` |
| jump | `0x0400` |
| ADS | `0x0800` |

`use` and `reload` must remain distinct in Zombies. The stock `0x0008` use bit
is the correct interaction path for doors, wall buys, mystery-box use, pickups,
revives, and other script-owned prompts. No entity scanner or custom purchase
logic is needed. The `0x0010` reload bit remains weapon reload.

### Direct usercmd actions versus native command injection

Use completed-usercmd mutation for continuous physical gameplay state:

- movement axes;
- attack, ADS, sprint, melee, use, and reload;
- controller-derived gun pitch/yaw where enabled.

Use edge-triggered native commands where the engine owns higher-level state:

- **Jump:** direct `0x0400` moves the player, but `+gostand` also passes through
  T4's stance state and script-sensitive native key path. Queue `+gostand` on
  press and `-gostand` on release for the mission-safe version.
- **Crouch/prone:** raw `0x0200/0x0100` are physical bits, but T4 has native
  tap/hold and stance-transition behavior. For the first controller binding,
  queue `togglecrouch` once per rising edge. `gocrouch`, `goprone`,
  `raisestance`, `lowerstance`, and `+stance/-stance` remain available when a
  fuller stance model is added.
- **Weapon switching:** never increment or otherwise synthesize the byte at
  usercmd `+0x14`; it is the authoritative selected-weapon value. Queue
  `weapnext` or `weapprev` once per controller edge.
- **Pause/menu:** there is no usercmd pause bit. Queue `toggleMenu` once on the
  menu-button rising edge.

For this executable, `Cbuf_AddText` is called with local-client/buffer index 0
in `ECX`, the command text pointer in `EAX`, and a newline-terminated string.
Commands must be edge queued; submitting `weapnext` or `toggleMenu` every frame
would cycle continuously.

### Native command evidence

| Command | String address | Exact handler or registration evidence |
| --- | ---: | ---: |
| `weapnext` | `0x0084D130` | handler `0x00469DE0`, registered at `0x00436269` |
| `weapprev` | `0x0084D13C` | handler `0x00469E40`, registered at `0x004362B9` |
| `+attack` | `0x0084D448` | handler `0x0063D070` |
| `+melee` | `0x0084D430` | handler `0x0063D1D0` |
| `+activate` | `0x0084F208` | handler `0x0063D1F0` |
| `+reload` | `0x00851F88` | handler `0x0063D210` |
| `+usereload` | `0x0084F2F0` | handler `0x0063D230` |
| `+gostand` | `0x0084F158` | handler `0x0063D560` |
| `+sprint` | `0x00851F90` | handler `0x0063D5D0` |
| `togglecrouch` | `0x0084E680` | exact command string |
| `gocrouch` | `0x0084E674` | exact command string |
| `goprone` | `0x0084F148` | exact command string |
| `+stance` | `0x00889A54` | exact command string |
| `pause` | `0x0088B6C0` | handler `0x006455E0` |
| `toggleMenu` | `0x0088B6EC` | handler `0x00645470` |

The native key-state helpers are `0x0063CCB0` for down and `0x0063CD50` for up.
In particular, `+gostand` at `0x0063D560` updates the stance state and can also
drive the jump key state when standing, which is why it is preferable to a
permanent raw jump-bit shortcut.

## Smallest live-testable implementation

Implement the next headset gate in this order:

1. Validate the complete executable SHA/profile and all expected bytes before
   enabling any T4 hook.
2. At the one gameplay scene call `0x00438C57`, clone the completed refdef and
   call the original `R_RenderScene` twice with left/right packed viewports,
   eye offsets, and centered symmetric source FOVs. Do not mutate the global
   refdef or run cgame twice.
3. Save the pending 2D pointer, render the left eye, explicitly clear the next
   view slot, deliberately copy the saved pointer into it, and render the right
   eye. Add a debug switch that leaves the second pointer null to isolate HUD
   problems from world-stereo problems.
4. Crop the packed halves in the compositor and remap each centered source to
   its runtime asymmetric FOV. Verify HUD convergence before changing
   ScreenPlacement.
5. Keep the existing continuous usercmd mappings for Zombies interaction and
   add edge-queued `weapnext`, `+gostand/-gostand`, `togglecrouch`, and
   `toggleMenu` commands.
6. When `keyCatchers & 0x10`, switch to the monoscopic comfort-screen path.
   Probe the actual T4 menu-bearing region before selecting a half-frame crop.

Useful first-frame assertions are:

- the view-info count increases by exactly two around the stereo hook;
- the saved first-eye 2D pointer is non-null in normal active play;
- the second slot is null immediately after the clear and equals the saved
  pointer only when binocular HUD replay is enabled;
- the two render calls use distinct packed viewports but the same simulation
  time;
- one controller press queues exactly one weapon/menu/stance command edge;
- disabling the VR feature returns the unmodified single-scene path.

This is substantially smaller than a general engine port. Comparative research
identified a useful high-level architecture while the T4 implementation remains
limited to one exact scene-call hook, one explicit per-view 2D ownership
decision, compositor projection/cropping, and the already mapped native input
boundaries.

## Provenance boundary

- t4-rtx was used only as a factual navigation aid. Its `0x6D80` view-info size
  clue agrees with the independently verified T4 multiply instructions above,
  but no t4-rtx code, structures, shaders, or assets are copied because that
  repository did not establish a reuse license.
- Proprietary executable bytes above are short interoperability signatures and
  are not distributed as a patched game executable or as game data.
