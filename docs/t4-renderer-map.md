# T4 SP renderer map for VR

This note records clean-room facts for the one supported executable. It is not
an assertion that these addresses apply to any other World at War build.

## Supported image

- File version: World at War SP `1.7.1263 CL(350073)`
- SHA-256: `F26D45524BFFF7E44C8EBAB4D758CA524EDFB0FB7D52352B6C95E1E908799361`
- PE image base: `0x00400000`
- Relocations: stripped

The addresses below were independently checked against the user's exact local
image with Microsoft's COFF/PE disassembler. Runtime code must still validate
the complete executable profile and the expected bytes at every patch site
before enabling a hook.

## D3D9 ownership

- `DxGlobals`: `0x03BF3B04`
- `DxGlobals::device`: `0x03BF3B08`
- `DxGlobals::targetWindowIndex`: `0x03BF6774`
- `DxGlobals::windowCount`: `0x03BF6778`
- `DxGlobals::windows[0].swapChain`: `0x03BF6780`
- `IDirect3DDevice9::Reset` vtable index: 16
- `IDirect3DSwapChain9::Present` vtable index: 3

At `0x006D598F`, the executable contains `A1 08 3B BF 03`, loading the device
pointer directly. The `DxGlobals` base and layout agree with the public factual
map in t4-rtx, but the instruction was independently verified in the supported
binary.

WaW does not submit the main window through `IDirect3DDevice9::Present`. The
exact executable's `RB_SwapBuffers` path begins at `0x006FBE50`, indexes the
16-byte window entry using `targetWindowIndex`, loads the swap chain from
`0x03BF6780 + index * 0x10`, and dispatches vtable slot `+0x0C`:

```text
0x006FBE51  mov eax, dword ptr [0x03BF6774]
0x006FBE5E  shl eax, 4
0x006FBE61  mov eax, dword ptr [eax + 0x03BF6780]
0x006FBE67  mov ecx, dword ptr [eax]
0x006FBE69  mov edx, dword ptr [ecx + 0x0C]
0x006FBE71  call edx
```

This is also an important lifecycle boundary: the device pointer can become
non-null while renderer initialization is still mutating the device. A hook
must wait for a valid target index, window count, and swap-chain object rather
than treating the first observed device pointer as proof that initialization
is complete.

## Windowed source resolution

The supported image registers `r_mode` as a string-valued enum at
`0x006D5EED`, with entries such as `640x480` and `1024x768`. It separately
registers `r_customMode` as a string dvar at `0x0070BB0D`. During windowed
renderer setup, `0x006D66F0` calls `0x006D6690`, which parses
`r_customMode` with `%ix%i`, checks it against the desktop bounds, and accepts
those dimensions. The enum `r_mode` path at `0x006D6708` is only the fallback
when that parse/validation fails. Fullscreen setup selects the enumerated mode
path directly.

This is why the standalone launcher uses `r_fullscreen 0` plus
`r_customMode WIDTHxHEIGHT`. The older IW3 convention `r_mode -1` is not used:
`-1` is not a member of this executable's `r_mode` enum. These facts were
checked in the same `1.7.1263 CL(350073)` image and SHA-256 identified above.

## Main gameplay refdef and scene call

The main SP `refdef` is at `0x03520338`. The partial layout required by the
first stereo prototype is:

| Offset | Field |
| ---: | --- |
| `0x00` | viewport x |
| `0x04` | viewport y |
| `0x08` | viewport width |
| `0x0C` | viewport height |
| `0x10` | `tanHalfFovX` |
| `0x14` | `tanHalfFovY` |
| `0x1C` | `vieworg[3]` |
| `0x2C` | `viewaxis[3][3]` |
| `0x60` | near clip |

Evidence:

- `0x00460B06` writes the four viewport integers beginning at `0x03520338`.
- `0x00460BEB` writes the three view-origin floats beginning at
  `0x03520354` (`refdef + 0x1C`).
- `0x00460DBA` passes `0x03520364` (`refdef + 0x2C`) as the camera axis.
- The renderer's view-parameter builder at `0x006DE290` reads FOV from
  `refdef + 0x10/+0x14`, origin from `+0x1C`, axis from `+0x2C`, and near clip
  from `+0x60`.

The main gameplay scene call is:

```text
0x00438C14  push 0x03520338
0x00438C57  E8 14 60 2A 00   call 0x006DEC70
```

The function at `0x006DEC70` is the T4 equivalent of `R_RenderScene`. Its entry
begins:

```text
81 EC E4 02 00 00 80 3D 61 69 BF 03 00
```

There is one other static caller, at `0x006EDA02`, which builds a temporary
refdef on the stack and is part of a non-gameplay renderer path. A first stereo
hook should therefore patch the gameplay call site at `0x00438C57`, not the
shared renderer entry, so probes/reflections are not accidentally doubled.

## Same-frame stereo implication

The stereo integration uses the following validated engine-family structure:
the SP path clones the completed refdef, applies per-eye origin/FOV, assigns
left/right packed viewports, and invokes its `CL_RenderScene` path twice before
the backend runs. T4 exposes the same core data and call shape above.

That does **not** make the port a blind address substitution. The COD4 source
also has explicit fixes for packed-target clears, draw-surface lifetime,
lighting/shadows, 2D command ownership, culling, and viewmodel handling. The
T4 adapter must reproduce only the fixes shown necessary by WaW runtime probes.

The intended staged implementation is:

1. Prove OpenXR and stock-D3D9 capture by submitting duplicate mono.
2. Start the OpenXR frame before the gameplay scene call and retain its two eye
   poses/FOVs until submission.
3. Clone only the validated T4 refdef, apply head pose and eye transforms, and
   invoke the original gameplay scene path twice in one simulation frame.
4. Preserve the left packed viewport when the backend clears for the right
   eye, then capture the packed frame before Present.
5. Add draw-surface/lighting/culling fixes only where live evidence requires
   them.

## Main-loop frame boundary

The exact SP image's WinMain loop contains one direct `Com_Frame(void)` call:

```text
0x005FF7BD  E8 6E EB F9 FF   call 0x0059E330
0x005FF7C2  A1 F4 B2 12 02   mov eax, dword ptr [0x0212B2F4]
```

The mod replaces only that five-byte call after validating its surrounding
29-byte WinMain context and the `Com_Frame` entry. Its cdecl wrapper calls the
original function and services OpenXR only after it returns. The swap-chain
Present callback captures D3D9 before native Present and records the result; it
does not initialize OpenXR, composite D3D11, call `xrEndFrame`, or wait for the
next XR frame inside T4's renderer/fence path.

## Provenance boundary

- t4-rtx was used only as a factual navigation aid. No t4-rtx implementation
  is copied here because its repository did not establish a reuse licence.
- Proprietary executable bytes and game data are never distributed.
