# T4 1.7 binding layer

This directory contains the fail-closed boundary between WorldAtWarVR and four
exact 32-bit World at War 1.7 executable identities: SP and MP in both the
previously tested plain form and untouched Steam Build 252004 form. It does not contain a
complete game routine, decompiled function, copied t4-rtx implementation, or
game asset. It does contain short, non-executable instruction fingerprints
needed to reject incompatible binaries before any hook is installed; their
scope and provenance are recorded in `PROVENANCE.md`.

## Supported images

The profiles describe exact user-owned test bytes. Their original
machine path and filename are deliberately omitted because neither is a trusted
identity signal. A launcher may point at or stage the executable under another
name only if its complete file hash and all other profile checks still match.

| Distribution / target | Observed filename | Size | SHA-256 | Code state / layout |
|---|---|---:|---|---|
| Plain SP | `t4sp.exe` | 5,550,080 | `F26D45524BFFF7E44C8EBAB4D758CA524EDFB0FB7D52352B6C95E1E908799361` | Plain / T4 SP 1.7.1263 |
| Plain MP | `t4mp.exe` | 5,505,024 | `943BB93001AD2ED465B6652C27FB649B5F0C5B24097E18A27A588AC35B3457A0` | Plain / T4 MP 1.7.1263 |
| Steam Build 252004 SP | `CoDWaW.exe` | 5,902,336 | `732900D158982C33E3121F0B86D22230BE79839BBCBFE3BDFC1238F408A7D64D` | Steam-wrapped / T4 SP 1.7.1263 |
| Steam Build 252004 MP | `CoDWaWmp.exe` | 5,857,280 | `7D0B518A4BD267FFDB6D0203AD8F3721603B172AC13BA2ABCDB32584F759D36C` | Steam-wrapped / T4 MP 1.7.1263 |

All four are PE32/i386 images with preferred base `0x00400000`. The untouched
Steam files add a `.bind` wrapper section and keep their engine code encrypted
on disk. They share an existing SP or MP layout only after the complete wrapped
file identity and PE metadata match and every mapped instruction sentinel
matches the code restored by Steam at runtime.

## Validation flow

1. `validate_executable_file` hashes every on-disk byte and parses PE metadata
   with explicit bounds checks. Plain images also validate instruction bytes
   through the section table; exact Steam-wrapped images defer only those
   encrypted instruction checks.
2. `capture_current_process` reads the executable path and mapped-image size
   through Windows APIs. Call it from a bootstrap worker, never under loader lock.
3. A Steam-wrapped profile receives a bounded 30-second readiness window for
   Steam to restore its code, after which `validate_loaded_module` verifies the
   fixed load base, PE identity, and every hook byte in memory.
4. Only `validate_and_bind` can produce `ValidatedBindings`.
5. `prepare_inline_hook` rechecks the target bytes. The eventual patch backend
   must compare them one final time while applying its thread/write-protection
   protocol.

Any discrepancy is fatal for hooking. There is intentionally no debug-only
assertion or "best effort" fallback.

## Current hook surface

| Role | Preferred VA | RVA | Expected bytes | Status |
|---|---:|---:|---|---|
| Render backend entry (`RB_Draw3DInternal`) | `0x006E8B96` | `0x002E8B96` | `83 E8 00 55 8B 6C 24 0C` | Verified detour candidate |
| Diagnostic-print body sentinel | `0x0059A21F` | `0x0019A21F` | `8B 75 08 83 FE 06` | Validation only |
| Post-build `usercmd` mutation | `0x0063E96C` | `0x0023E96C` | `B9 0E 00 00 00 8B F0 8D 7C 24 10 F3 A5` | Verified detour candidate |
| Authoritative gun-angle copy | `0x004E8D10` | `0x000E8D10` | `0F BF 47 1C F3 0F 10 05 FC F4 8A 00 F3 0F 2A C8` | Validation only |
| `CalcMuzzlePoints` entry | `0x00551380` | `0x00151380` | `83 EC 3C 53 8B 5C 24 44 83 BB 80 01 00 00 00 56` | Validation only |
| Weapon `tag_camera` refdef call | `0x0042DD2D` | `0x0002DD2D` | `E8 5E 52 01 00` | Verified exact patch candidate |

The render address is a backend timing point, not yet proof of a safe stereo
render boundary. UI/key injection remains a separate optional boundary.

### Post-build command ABI

`CL_CreateNewCommands` begins at preferred VA `0x0063E940`. It increments the
command number, calls the command builder at `0x0063E850`, and reaches the
profiled boundary with `EAX` pointing at the completed mutable command. The
first original instruction is the complete five-byte `mov ecx, 14`; the
following instructions copy fourteen dwords (56 bytes) from `EAX` into the
stack copy that is then copied into the ring.

An x86 detour bridge at this boundary must therefore:

- capture `EAX` as `UsercmdSp*` before changing it;
- preserve the engine's registers and flags around any ordinary C++ call;
- invoke the trampoline so the displaced `mov ecx, 14` and the native copies
  still execute; and
- fail closed unless `prepare_inline_hook` accepts the exact profile and bytes.

This is a custom register contract, not a callable C or C++ function signature.
The binding layer intentionally does not pretend otherwise.

## SP command and controller weapon aim

`CL_GetUserCmd` at preferred VA `0x0063A430` independently establishes a
128-entry ring at `0x030FD700`, indexed by `command_number & 0x7F`, with a
stride of 56 bytes. The minimal `UsercmdSp` definition names only fields
consumed by this mod:

| Offset | Width | Meaning |
|---:|---:|---|
| `+0x00` | 4 | server time |
| `+0x04` | 4 | button mask |
| `+0x08` | 12 | three fixed-point view angles |
| `+0x14` | 1 | weapon index |
| `+0x15` | 1 | offhand index |
| `+0x16` | 1 | signed forward movement |
| `+0x17` | 1 | signed right movement |
| `+0x18` | 4 | deliberately opaque motion bytes |
| `+0x1C` | 2 | signed fixed-point gun pitch |
| `+0x1E` | 2 | signed fixed-point gun yaw |
| `+0x20` | 8 | deliberately opaque bytes |
| `+0x28` | 4 | melee-charge target yaw |
| `+0x2C` | 1 | melee-charge target distance; zero is native no-charge |
| `+0x2D` | 11 | deliberately opaque tail |

The command builder directly ORs the following independently observed masks
into `+0x04`: attack `0x1`, sprint `0x2`, melee `0x4`, use `0x8`, reload
`0x10`, combined use/reload `0x20`, lean left/right `0x40`/`0x80`, prone
`0x100`, crouch `0x200`, jump `0x400`, aim-down-sights `0x800`, hold breath
`0x2000`, frag `0x4000`, and smoke `0x8000`. `apply_vr_weapon_aim` only ORs
attack, so physical keyboard/mouse input is preserved.

The post-build VR bridge always zeros `+0x28/+0x2C` after native command
serialization. T4's `PM_MeleeChargeStart` treats nonzero distance as permission
to rotate/lunge toward a target and treats zero as its native clear sentinel.
This removes knife-driven HMD motion without erasing the melee button, damage,
or animation. The launcher also sets `aim_automelee_enabled 0` so aim-assist
cannot add target pitch/yaw before serialization.

The two gun-angle fields provide the minimal native controller-aim path:

1. `apply_vr_weapon_aim` converts finite controller pitch/yaw degrees into
   T4's 16-bit turn representation and writes command offsets `+0x1C/+0x1E`.
2. Authoritative code at `0x004E8D10` sign-extends those two words, multiplies
   by `360/65536`, and stores them at client offsets `+0x2258/+0x225C`.
3. `CalcMuzzlePoints` at `0x00551380` selects those client gun angles and calls
   the engine's `AngleVectors` path used for weapon firing.

The corrected viewmodel now publishes its evaluated `tag_flash` and final
visible basis with the controller generation and publication time. The exact
primary-fire call at preferred VA `0x005515E3` remains responsible for
invoking native `CalcMuzzlePoints`; a guarded wrapper then replaces
`weaponParms+0x24` with that fresh physical muzzle. Direction basis normally
stays on WaW's native path. The exact Mosin profile additionally opts into an
atomic forward/right/up replacement from the same fresh visible snapshot,
closing its physically observed left-owner/right-bolt direction handoff while
the Kar98, Springfield, and Type 99 paths remain native. A stale Mosin
snapshot preserves the complete native shot and predicted impact ray. Damage,
penetration, and effect generation remain native. At the independently validated
ordinary-bullet call, a second guarded bridge replaces only the final local
firearm spread scalar with the weapon definition's authored ADS spread. It
does not enter ADS state or alter movement, FOV, recoil, ammo, or shotgun pellet
count. `CG_DrawBulletImpacts` separately obtains its own eye origin and visual
min/max spread; native-first wrappers replace those successful local values
with the same fresh `tag_flash` and authored ADS cone so the tracer/predicted
impact agrees with the authoritative shot. The physical-muzzle wrapper accepts
only `g_entities[0]`, entity number zero, a non-null client, ordinary-bullet
weapon type zero or projectile type two, current focused controller tracking,
and a muzzle no more than four controller generations or 150 ms old. The
authoritative and client-visual spread overrides remain type-zero-only. Every
failed gate preserves native behavior.

For retained bounded Mosin impact diagnostics, the SP profile also pins the
complete `CG_DrawBulletImpacts -> FireBulletPenetrate -> BulletTrace` chain and
two downstream impact-effect calls. Only the five-byte cleanup/result sequence
immediately after the native first `BulletTrace` call is wrapped; the call
itself is untouched. The wrapper logs at most 24 exact-Mosin shots (start, end,
direction, hit result, fraction, solid flags, surface flags, contents, hit
type/id, and hit position), then replays `add esp,0x0C; test al,al` before
resuming native control flow. Two native-first call wrappers then record the
impact selector's effect, sound, surface/contents and normal plus the final FX
spawn's returned handle. Same-thread weapon/position correlation and read-only
mark counters bound this to 24 event pairs. Immediate counter deltas are
observational because mark allocation may happen in a later FX update. These
diagnostics consume the retail frame's map-local `weaponIndex`, resolve its
definition through the pinned SP table, and separately bound raw arrival logs
so a future index/identity mismatch cannot disappear before logging. They are
SP-only and absent from the MP profile.

The physical-origin handoff is validated against the local T4 command path.
The first T4 implementation intentionally does not yet implement
eye-to-muzzle obstruction trace, so firing while the tracked muzzle is pushed
through nearby geometry remains a documented follow-up rather than silently
claiming full parity.

## Verified data symbols

These data locations are available only through `ValidatedBindings`, after the
exact file and mapped module have passed validation:

| Symbol | Preferred VA | RVA | Known extent |
|---|---:|---:|---:|
| 128-entry SP command ring | `0x030FD700` | `0x02CFD700` | `0x1C00` bytes |
| current SP command number | `0x030FF300` | `0x02CFF300` | 4 bytes |
| client gun pitch (degrees) | `0x0352B65C` | `0x0312B65C` | 4 bytes |
| client gun yaw (degrees) | `0x0352B660` | `0x0312B660` | 4 bytes |
| SP local player (`g_entities[0]`) | `0x0176C6F0` | `0x0136C6F0` | `0x378` bytes |
| SP FX no-marks gate | `0x016CFD58` | `0x012CFD58` | 1 byte |
| SP FX mark allocation counter | `0x016CFD5C` | `0x012CFD5C` | 4 bytes |
| SP FX mark freed counter | `0x016CFD60` | `0x012CFD60` | 4 bytes |

The client gun-degree values feed local weapon calculations and are exposed for
future visual alignment. Merely exposing them does not authorize an arbitrary
write: the mod must establish the correct frame ownership/order before using
them. The completed-command fields are the supported authoritative firing path.

## Adding a profile or hook site

- Record the source executable's full SHA-256 and PE identity.
- Derive RVAs from that image rather than embedding process VAs throughout code.
- Disassemble through complete instruction boundaries and record enough original
  bytes for the chosen detour backend.
- Exercise positive and single-byte-negative validation tests.
- Do not add a guessed input, weapon, camera, or D3D layout to a supported profile.
- Keep engine-facing structures minimal and assert every consumed field offset
  against independent binary evidence.

`tests/profile_validation_tests.cpp` is a standalone pure-C++ test. Passing the
local executable as its sole argument additionally checks the real profile.
