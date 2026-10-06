# Physical-scope native HUD guard review

## Boundary

This review concerns the three native scope-overlay/HUD predicate callsites
and their x86 bridge. It does not claim a physical-headset HUD acceptance.
The accepted scoped-Mosin visibility isolation, optical zero, projectile ray,
and scope/left/right view order remain outside this hook's mutation scope.

## Independent native evidence

Read-only disassembly of the existing local
`C:\Users\jplak\Downloads\test\wawvr-live32-72468.dmp` identifies Steam
`CoDWaW.exe`, base `0x00400000`, image size `0x04B11000`, PE timestamp
`0x4AEA1F46`. The dump was already available; this audit launched no process.

`CG_GetWeapReticleZoom` at `0x0043D000` takes cg in EDX, the writable float
output in ESI, and returns a boolean in AL. It reads the predicted ADS
fraction at `[EDX+0xAADA8]`, initializes the output to zero, and returns false
when the fraction is zero. The positive zoom threshold is 0.01. The source
inspection and retail disassembly agree about this behavior.

Only these direct calls are replaced:

| Caller | Call address | Original bytes | Context address/size |
| --- | --- | --- | --- |
| Native weapon scope overlay | `0x0043D73D` | `E8 BE F8 FF FF` | `0x0043D730`, 35 bytes |
| Scope-sensitive HUD menu visibility | `0x0046BEB9` | `E8 42 11 FD FF` | `0x0046BEAF`, 18 bytes |
| UI expression's equivalent scope predicate | `0x005B158F` | `E8 6C BA E8 FF` | `0x005B1585`, 32 bytes |

The calls all decode to `0x0043D000`. Its 48-byte entry sentinel is:

```text
F6 82 A8 AC 0A 00 02 74 08 8B 82 94 AD 0A 00 EB 06
8B 82 9C AD 0A 00 8B 0C 85 70 67 8F 00 83 B9 1C 05
00 00 00 0F 57 C9 F3 0F 10 82 A8 AD 0A 00
```

A separate read-only comparison parsed the four sentinel arrays from the new
hook source and compared every byte with that saved dump: 48/35/18/32 bytes
all matched. The hook also requires exact supported SP profile identity,
hash and preferred module base. It does not patch the native zoom entry or
the six other direct zoom callers found in the retail code. In particular,
native weapon motion and viewmodel-visibility callers remain untouched.

Clearing the native ADS input alone cannot guard transition frames: native
ADS exit can wait and then interpolate the predicted fraction to zero.
The scissor flag cannot replace the zoom guard because the overlay can draw
above zoom 0.01 while scissor only activates above 0.99. Scope-sensitive menu
visibility also uses the native zoom predicate independently of mask drawing.

## Bridge verification

MSVC Win32 Release disassembly of
`out/scope-hud/src/mod/WorldAtWarVR.dir/Release/physical_scope_hud_hook.obj`
confirmed the bridge's exact span `0x00..0x38`, matching the `0x39`-byte
peer-thread patch guard. Both paths restore the saved x87/SSE image, general
registers, EFLAGS and caller ESP after evaluating the C++ predicate.

The suppressed path then executes:

```text
00000022: C7 06 00 00 00 00  mov dword ptr [esi],0
00000028: B0 00              mov al,0
0000002A: C3                 ret
```

Neither MOV changes restored EFLAGS. ESI remains the original output pointer;
only the documented output and low boolean return byte change. The native
path restores state and jumps through the retained original function pointer
at offset `0x33`. The 16-byte-aligned FXSAVE buffer is 528 bytes below the
saved register frame. No stack arguments are added or consumed by the bridge.

## Runtime gates and lifecycle

The predicate requires enabled complete installation, valid active gameplay
without UI catchers, and the current frame's prepared physical-scope decision.
That decision is selected from a fresh active snapshot at the HUD boundary
and shared by native mask suppression, HUD packing and scene construction.
The query also checks current-frame stereo readiness. This avoids a second
150 ms freshness test expiring midway through a long frame. Negative scope
decisions are preserved for that frame too; another thread cannot inherit
the game thread's prepared decision.

All rejected gates preserve the exact native call. Suppression is enabled
only after all three sites install. Partial installation rolls back with
suppression disabled; remaining owned sites are tracked if rollback must be
retried. Shutdown disables the predicate first, restores only calls still
owned by this hook, and preserves foreign replacements. The original target
remains valid for peers already inside the predicate during restoration.

The first successful suppression emits one `WorldWarVR.log` receipt beginning
`Physical scope native mask/HUD visibility suppressed: current-frame optic decision`.
This receipt proves the guard executed, not that a physical headset displayed
the intended compass/ammo HUD. The latter requires the user's retest.
