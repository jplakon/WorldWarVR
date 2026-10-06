# WaWVR standalone launcher

This directory contains a Windows launcher for the inspected English Call of
Duty: World at War 1.7 SP/Zombies and MP identities, including untouched Steam
Build 252004. It deliberately does not invoke the
Plutonium launcher or bootstrapper.

`wawvr-launcher-cli.exe` keeps read-only diagnosis as its no-argument default.
`wawvr-launcher.exe` is the x86 GUI-subsystem helper invoked by the packaged
settings app and copied into the isolated runtime for the later SP -> MP
handoff. The packaged `WorldWarVR.exe` is the user-facing settings app.
Its Zombies/Campaign **Automatic Reload** and **Button Grenades** options are persistent,
opt-in, and disabled by default. Automatic Reload maps A to the game's
conventional full reload. Button Grenades maps the left index trigger to the
game's conventional grenade throw. These choices are passed only to the child
launch as `WAWVR_AUTOMATIC_RELOAD=1|0` and `WAWVR_BUTTON_GRENADES=1|0`.
`WorldWarVR-Multiplayer.exe` is the Explorer-friendly offline multiplayer
entry point: with no arguments it selects the isolated MP runtime and the same
adjacent VR DLL. Branded failures use a persistent Windows message box rather
than a disappearing console.

Every single-player launch advances the active profile's stock `mis_01`
mission-progression counter to WaW's unlock-all value. The injected runtime
reapplies that command after profile initialization so Mission Select cannot
remain locked because startup processing restored an older value. This does
not enable developer mode, replace save files, or alter `mis_difficulty`.

The MP target uses its exact pinned 1.7 multiplayer identity in separate
identity-specific runtime and home roots. Launch it directly with
`WorldWarVR-Multiplayer.exe` (or `WorldWarVR.exe --launch --multiplayer`);
the stock SP frontend's Launch
Multiplayer action enters the same runtime through a verified runtime-only
handoff shim. SP preparation writes a canonical
`WaWVR-Multiplayer-Handoff.v1` beside that shim inside the isolated SP runtime.
The retained filename is backward-compatible; current V3 contents pin the
exact SP and MP executable identities plus the explicit bot policy. Canonical
V1/V2 content is read with the historical automatic bot policy and atomically
upgraded during the next preparation.
The file also pins the resolved game root, selected MP executable, derived MP
runtime/home paths, and source resolution. `--bots disabled` prevents archive
discovery, import, and autofill both immediately and after the stock frontend
handoff, and explicitly clears bot/mod activation archived by an earlier run;
`--bots enabled` retains exact hash-gated optional import. The
no-argument shim validates the
exact parent, waits for it to exit, and then consumes only that file; it has no
ambient drive, LocalAppData, environment, or Plutonium fallback for those core
paths. It disables PeZBOT archive discovery/import and only inspects the
already prepared MP home.

### Optional offline PeZBOT bots

WaWVR never downloads or redistributes PeZBOT. To enable bots, provide the
exact supported user-supplied `PeZBOTWAW_005p.zip` in one of these ways:

```text
wawvr-launcher-cli.exe --launch --multiplayer --bots enabled --pezbot-archive C:\path\PeZBOTWAW_005p.zip
```

Alternatively, place that exact filename beside the WaWVR launcher or in the
current user's Downloads folder. The launcher requires exactly 1,138,246 bytes
with MD5 `4defeab88624baf05d28bbebf6c86c01` and SHA-256
`b7958b96cbe3a8c316290df7148c63ca601d1de2f96f6166d2c67fe069500fdf`
before a fixed, hashed helper may open the ZIP. Extraction accepts only the four required mod files and constrained
ReadMe documents; rooted paths, traversal, duplicate paths, links/reparse
entries, unexpected files, unsafe compression, and overwrites are rejected.
Files are written with create-new semantics into a unique temporary child of
the isolated MP `mods` directory, rehashed into a receipt, and atomically moved
to `home-mp\mods\mp_PeZBOTWAW`.

For an SP/frontend plan, `home-mp` is derived beside the resolved SP home. The
ordinary default therefore remains `%LOCALAPPDATA%\WaWVR\home-mp`, while an
explicit `--homepath D:\managed\h\home` prepares bots only under
`D:\managed\h\home-mp`. The later no-argument stock-menu shim never imports an
archive; it can enable only that prepared, receipt-verified install.

A verified install points both `fs_homepath` and World at War MP's distinct
`fs_localAppData` root at the isolated MP home before selecting
`fs_game mods/mp_PeZBOTWAW`. This makes the mod IWD and `mod.ff` load together
without writing into the normal Activision profile. The launcher then executes
`pezbot.cfg` and applies nine bots, authentic faction weapons, perks off, and
skill 0.7. Map and game-mode selection remain in the stock MP frontend. If the
archive/helper is unavailable or invalid, multiplayer still launches without
bots. An existing unverified or customized `mp_PeZBOTWAW` folder is reported
and is never overwritten or deleted.

PeZBOT resets its one-shot bot-count dvar after filling a server. The injected
exact-profile service consequently rearms nine bots once per new server: in a
disconnected MP frontend visit, or on the first post-frame loading observation
after a previously active server exits without visiting that frontend. It
latches across the entire load, never polls during a match, and therefore
cannot duplicate bots from team/class/pause screens or automatic rotation.

Immediately before an offline-MP process is created, the launcher atomically
normalizes its managed active isolated profile's archived `ui_dedicated`
selector to `0`, while retaining command-line
`dedicated 0`/`ui_dedicated 0` safeguards. This keeps **Start New Server** in
listen-server mode so the local VR player joins alongside the bots. Missing
first-run profiles are left for WaW to create, unsafe/reparse/traversal profile
paths fail closed, and explicit `--homepath` trees remain caller-owned and are
never rewritten. Neither the ordinary Activision profile nor the installed
game is touched.

## Safety and ownership boundary

The launcher does not contain or download a game executable, game data, an ASI
loader, or Plutonium files. It accepts a locally owned executable, verifies the
exact supported build, and copies that file into a user-writable runtime as
`CoDWaW.exe`. The original installation is never patched.

Pinned executable identities:

- Plain SP: `F26D45524BFFF7E44C8EBAB4D758CA524EDFB0FB7D52352B6C95E1E908799361`
- Plain MP: `943BB93001AD2ED465B6652C27FB649B5F0C5B24097E18A27A588AC35B3457A0`
- Steam Build 252004 SP `CoDWaW.exe`: `732900D158982C33E3121F0B86D22230BE79839BBCBFE3BDFC1238F408A7D64D`
- Steam Build 252004 MP `CoDWaWmp.exe`: `7D0B518A4BD267FFDB6D0203AD8F3721603B172AC13BA2ABCDB32584F759D36C`
- All targets: World at War 1.7, x86 PE32, image base `0x00400000`
- Nacht map: `nazi_zombie_prototype`
- Der Riese map: `nazi_zombie_factory`

No machine-specific game directory is compiled into the launcher. Supply the
authorized game root with `--game-dir` or `WAWVR_GAME_DIR` for the first
preparation. Later runs can recover it from the matching paired `main` and
`zone` junctions in either launcher-managed runtime. The launcher derives
distinct standalone defaults from the selected exact identity:

- Plain SP: `runtime\waw-1.7.1263` and `home`
- Plain MP: `runtime\waw-mp-1.7.1263` and `home-mp`
- Steam SP: `runtime\waw-steam-252004-1.7` and `home-steam-252004`
- Steam MP: `runtime\waw-mp-steam-252004-1.7` and `home-mp-steam-252004`

Automatic executable discovery first hashes the retail-named `CoDWaW.exe` or
`CoDWaWmp.exe` in the supplied game directory, then checks identity-specific
managed stages, then the legacy local cache. A present file with an unknown or
wrong-kind hash fails closed. For Steam identities the launcher adds
`SteamAppId=10090` and `SteamGameId=10090` only to the child game's environment;
normal Steam ownership and client behavior remain in force. The legacy locally
cached `t4sp.exe`/`t4mp.exe` files are only first-stage migration fallbacks and
are never launched in place.
Once the managed copy has been validated and staged, normal launches have no
Plutonium process or file dependency. `--source-exe` remains available for an
explicit user-owned first-stage source. A managed SP/frontend launch can also
pass `--mp-source-exe FILE`; that exact user-owned MP source is captured in the
trusted SP plan and later handoff config. The option is deliberately rejected
with `--multiplayer`, where `--source-exe FILE` selects the process being
launched directly.

The runtime and home directories are rejected if they overlap either the game
data directory or the source executable directory. Staging uses a temporary
copy, verifies it again, and atomically replaces only the launcher-owned staged
file.

WaW performs its first fastfile lookup relative to the executable directory,
before `+set fs_basepath` has taken effect. Preparation therefore creates two
launcher-managed **directory junctions** in the isolated runtime:

- `runtime\\zone` -> the validated game `zone` directory (including
  the Steam-selected `zone\\<language>` fastfiles);
- `runtime\\main` -> the validated game `main` directory. This also exposes
  `main\\video`, so a separate `video` link is neither needed nor correct.

`redist` contains installers rather than runtime content and is intentionally
not linked. Required root files are copied into the isolated runtime after
verification. The language selector is detected from the installed
`localization.txt`, its matching `zone\\<language>` directory is required, and
the cached runtime selector is atomically refreshed when Steam's installed
language changes. The junctions do not modify proprietary game files and do
not require administrator rights. Every run checks that an existing path is
specifically a directory junction to the expected canonical source; a normal
directory, symlink, or junction to any other target blocks preparation instead
of being replaced. The runtime and home roots themselves may not be reparse
points.

This is interoperability tooling for a user-owned/licensed copy. Do not ship
the proprietary executable or data with WaWVR. In Canada, local compatibility
work may engage the Copyright Act's computer-program and interoperability
provisions (sections 30.6 and 41.12); that does not authorize redistribution.

## Build

The game and eventual mod DLL are 32-bit. Use a Win32 build for DLL injection:

```powershell
cmake -S launcher -B build/launcher-x86 -A Win32
cmake --build build/launcher-x86 --config Release
ctest --test-dir build/launcher-x86 -C Release --output-on-failure
```

A 64-bit build remains useful for diagnostics and staging, but the launcher
will reject `--mod-dll` because cross-bitness injection is intentionally not
implemented.

## Read-only diagnostic

```powershell
build\launcher-x86\Release\wawvr-launcher-cli.exe --diagnose
```

This checks:

- expected game-data markers;
- executable SHA-256, fixed file version, PE architecture, timestamp, image
  base, and entry point;
- the known `binkw32.dll` and Nacht fastfile hashes;
- runtime/home write isolation;
- optional mod DLL architecture;
- the selected packed side-by-side source resolution;
- the exact command line that would be used.

It creates no directories, copies no files, and starts no processes.
Every actual launch repeats this preflight. A direct `--der-riese` launch adds
checks for all four pinned Der Riese fastfiles and its pinned intro-video hash.

## Stage without launching

```powershell
build\launcher-x86\Release\wawvr-launcher-cli.exe --prepare
```

This creates the isolated runtime/home directories, stages the verified
user-owned executable as `CoDWaW.exe`, and creates the validated `zone` and
`main` directory junctions described above. It does not touch the E: game
directory and does not launch the game.

## Launch solo Nacht

Launching is intentionally an explicit mode:

```powershell
build\launcher-x86\Release\wawvr-launcher-cli.exe --launch `
  --mod-dll C:\path\to\WorldWarVR.dll
```

The generated command sets `fs_basepath` to the existing game-data directory,
sets `fs_homepath` to WaWVR's isolated profile directory, skips the intro, uses
windowed mode for the initial engineering build, and requests
`nazi_zombie_prototype` through `+devmap`.

Exact validated single-player Nacht, Der Riese, and frontend launches default
to a `6016x2688` packed source (two `2496x2688` eyes plus a `1024x1024`
physical-scope camera before OpenXR
remapping). The early D3D9Ex renderer bootstrap creates it as an off-screen back
buffer, so it does not need to fit the active desktop. Override it with
`--resolution WIDTHxHEIGHT`; the packed width must be even, with accepted bounds
of `640x480` through `6016x2688`. The lower-cost VR preset is:

```powershell
wawvr-launcher-cli.exe --launch --resolution 3744x2016 `
  --mod-dll C:\path\to\WorldWarVR.dll
```

The compatibility preset is `2560x1440`. The minimum-cost command-line recovery
setting is:

```powershell
wawvr-launcher-cli.exe --launch --resolution 1024x768 `
  --mod-dll C:\path\to\WorldWarVR.dll
```

`WAWVR_SOURCE_RESOLUTION=1024x768` applies the same setting to shortcuts and
the user-facing executable. An explicit `--resolution` takes precedence over
the environment. Offline multiplayer retains the desktop-bound `2560x1440`
default because it does not use the single-player renderer patch.

WaW's exact T4 renderer parses `r_customMode` on the launcher's windowed path
before falling back to its string-valued `r_mode` enum. Consequently the
launcher sets `r_customMode` directly and does not add the incompatible
IW3-style `r_mode -1` switch.

All launch targets also set `r_dof_enable 0`. WaW's stock depth-of-field focus is
camera-bound and otherwise turns ADS into an uncomfortable headset-wide blur.
This setting removes that blur without changing the ADS control. The launcher
sets `bg_bobMax 0` and `cg_bobWeaponAmplitude 0` once per process to keep stock
locomotion and viewmodel bob out of the headset. These use non-persistent
`+set` commands: they do not rewrite the isolated profile, and landing, lean,
damage, and authored scripted-shake behavior remain separate. The launcher
also disables the stock firearm crosshair because the tracked barrel is the VR
reticle. It requests native auto-melee off as defense in depth; because WaW can
reset that cheat dvar during map initialization, the exact-profile DLL also
bypasses target pitch/yaw and clears melee-charge yaw/distance while retaining
the knife action.

All launch targets set `cg_firstPersonTracerChance 1` because this exact WaW
build registers it at `0`, which otherwise suppresses every local-player
tracer. They retain stock `fx_cull_elem_spawn 1` while setting
`fx_cull_elem_draw 0`. WaW prepares FX visibility from the stock/body camera
before the mod applies the late HMD view, so draw culling can intermittently
remove blood, Ray Gun trails, and explosions that are visible from the
headset. Spawn culling remains enabled because disabling it allowed distant
map effects to fill WaW's fixed 1024-effect pool, after which valid weapon
impact effects returned no handle. Effect simulation and lifetime remain
native; already-spawned elements receive the headset-conservative draw policy.

## Launch the Zombies frontend/menu

To enter WaW's stock frontend instead of forcing a development map:

```powershell
build\launcher-x86\Release\wawvr-launcher-cli.exe --launch --menu `
  --mod-dll C:\path\to\WorldWarVR.dll
```

This uses the same validated executable, isolated data/profile paths, renderer
safety settings, injection path, and startup-dialog handling as direct Nacht,
but deliberately omits `+devmap`. The game therefore owns map selection and
can expose the Zombies maps installed in the user's data directory. Direct
Nacht remains the raw launcher's default map target. Its `--launch` mode
requires an explicit `--mod-dll`, preventing an accidental unmodded session.

## Launch Der Riese directly

The installed final DLC Zombies map can be launched without navigating the
frontend:

```powershell
build\launcher-x86\Release\wawvr-launcher-cli.exe --launch --der-riese `
  --mod-dll C:\path\to\WorldWarVR.dll
```

This fixed target validates the installed base, load, patch, and localized
Der Riese fastfiles plus `nazi_zombie_factory_load.bik` before issuing
`+devmap nazi_zombie_factory`. It uses the same isolated runtime, OpenXR
injection, controls, and renderer safety settings as direct Nacht.

## Developer-only Mosin campaign validation

The raw launcher exposes one fixed campaign probe for physical Mosin testing:

```powershell
wawvr-launcher-cli.exe --launch --campaign-ber1 `
  --mod-dll C:\path\to\WorldWarVR.dll
```

This issues exactly `+devmap ber1` while preserving the same isolated runtime,
OpenXR injection, controller, stereo, and renderer safety settings as the other
single-player targets. It is intentionally absent from the normal launcher UI
and does not accept arbitrary map text. Diagnosis requires `ber1.ff` to exist;
the campaign asset is presence-checked for this bounded developer probe rather
than promoted to a pinned supported-content target.

After resuming the game, the launcher polls only top-level windows owned by the
PID it just spawned. It recognizes the exact World at War `#32770` dialogs for
both `Run In Safe Mode?` and `Set Optimal Settings?`, including their expected
WaW body text and `No` button control ID 7, then selects **No**. This keeps a
first run from blocking unattended direct-map startup or rewriting the
isolated profile's renderer settings. It does not click title-only matches or
windows owned by any other process, and it stops after a bounded polling
window when neither exact prompt appears.

To load the built VR DLL explicitly:

```powershell
build\launcher-x86\Release\wawvr-launcher-cli.exe `
  --launch `
  --mod-dll C:\path\to\WorldWarVR.dll
```

Packaged builds also include `WorldWarVR.exe`. Running that executable with
no arguments is the user-facing shortcut: it performs the equivalent of
`--launch --menu` and injects the adjacent `WorldWarVR.dll`. Supplying target,
diagnostic, or recovery arguments preserves that adjacent-DLL default unless an
explicit `--mod-dll` overrides it.
The separate GUI-subsystem `WorldWarVR-Multiplayer.exe` performs the
equivalent of `--launch --multiplayer` with no arguments. Once either runtime
has been prepared, the launcher can recover the game root only from matching
`main` and `zone` directory junctions that it previously created; explicit
`--game-dir` and `WAWVR_GAME_DIR` still take precedence.
The separate `wawvr-launcher-cli.exe` keeps read-only diagnosis as its
no-argument default; raw `--launch` requires `--mod-dll` and fails closed
without one. The packaged UI invokes the GUI-subsystem `wawvr-launcher.exe`
with explicit `--launch`, target, install path, resolution, DLL, and bot policy
arguments.

The DLL must be an x86 PE DLL. The launcher creates the verified game process
suspended and writes the absolute DLL path into it, then resumes the main
thread so the Windows loader can initialize. It polls for the target-side
module that actually owns `LoadLibraryW` (typically `KernelBase.dll`, rather
than assuming a matching `kernel32.dll` address), requires a stable module base
across successive observations, invokes the target-side function, and verifies
the remote thread's result. This avoids relying on a search-order proxy DLL or
writing an ASI loader into the game installation. If loader discovery or
injection fails, the child is terminated rather than continuing unmodded.

Use `--wait` to keep the launcher attached and return the game's exit code.

All paths can be overridden explicitly:

```text
--game-dir DIR
--source-exe FILE
--mp-source-exe FILE
--runtime-dir DIR
--homepath DIR
--mod-dll FILE
--pezbot-archive ZIP
--resolution WIDTHxHEIGHT
```

`WAWVR_GAME_DIR`, `WAWVR_SOURCE_EXE`, `WAWVR_MP_SOURCE_EXE`, and
`WAWVR_SOURCE_RESOLUTION` are also accepted for build/test automation and
shortcut configuration. For the stock frontend handoff,
explicit `--mp-source-exe` takes priority over `WAWVR_MP_SOURCE_EXE` and all
automatic discovery. The selected value is resolved while preparing the
trusted SP plan and stored in the runtime-local handoff config; the later shim
does not read either the command line or environment.

## Current limitations

- Only the exact inspected English 1.7 SP executable plus Nacht and the complete
  Der Riese fastfile/intro-video set are pinned by launcher validation. Frontend
  mode may display other locally installed Zombies maps, but their individual
  assets are not independently allowlisted or compatibility-tested.
- Automated tests cover the junction plan, native junction creation,
  idempotence, nested `main\\video` access, and fail-closed mismatched-target
  handling, plus the exact safe-mode-dialog identity gate. They do not launch
  the proprietary game.
- The launcher does not create firewall rules. Solo/offline operation should
  remain the initial target, and no public legacy multiplayer connection should
  be made from this unmodified 2009 executable.
- The separate home path is persistent, not erased on each run. It is isolated
  from both stock WaW and Plutonium profiles so tests are reproducible without
  deleting user data.
