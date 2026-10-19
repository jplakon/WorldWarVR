# Offline multiplayer and bots

World War VR launches the exact supported World at War 1.7 multiplayer
executable directly. It does not start the Plutonium client or bootstrapper,
does not use a public matchmaking service, and keeps the multiplayer runtime
and profile separate from Zombies.

## Enter multiplayer

Either select **Launch Multiplayer** from WaW's single-player frontend or
double-click the standalone multiplayer launcher:

```text
WorldWarVR-Multiplayer.exe
```

The command-line equivalent is `WorldWarVR.exe --launch --multiplayer`.
After the isolated runtime has been prepared once, the multiplayer launcher can
recover the game root from its paired, validated `main` and `zone` junctions;
no Plutonium launcher or machine-specific packaged path is required.

The stock frontend always asks Windows to start a file named
`CoDWaWmp.exe`. World War VR supplies that name only inside its isolated SP
runtime as a copy of the WaWVR launcher. The handoff validates the exact parent
SP process and waits for it to exit and release OpenXR. During SP preparation,
the launcher writes a strict handoff configuration beside that runtime-only
shim. It records the already resolved game directory, selected/explicit MP
source executable, SP-derived MP runtime and home paths, and the same source
resolution. The no-argument shim consumes only that file: absent, malformed,
non-canonical, path-escaped, or wrong-stage configurations are rejected rather
than falling back to an E-drive, LocalAppData, environment, or Plutonium path
for game/source/runtime/home. It also skips PeZBOT archive discovery/import and
uses only a verified install already present under the configured MP home.
It then starts the separately validated MP runtime with the VR DLL. No
proprietary executable is packaged under that name and no file in the game
directory is changed.

Managed frontend launchers should provide the exact user-owned MP executable
while constructing the SP plan:

```text
WorldWarVR.exe --launch --menu --mp-source-exe C:\path\to\CoDWaWmp.exe
```

This SP-only option takes priority over `WAWVR_MP_SOURCE_EXE` and automatic
discovery. Direct `--multiplayer` uses `--source-exe` instead. MP runtime and
home paths are intentionally not separately overridable by the handoff; they
remain derived siblings of the validated SP stage and home.

The MP launch is forced to local/offline settings (`onlinegame 0`, listen
server, and PunkBuster disabled). From the MP frontend, create a local game and
choose a stock map and mode normally. On **Start New Server**, leave
**Dedicated** set to **No**. Before every offline-MP process launch, WaWVR also
normalizes that one archived selector in only its launcher-managed isolated
active profile. Explicit `--homepath` trees remain caller-owned and are never
rewritten. If Dedicated is changed to **Yes**, WaW turns the sole process into
a server console: bots can play, but the headset player cannot join.

Team selection, class selection, and the in-game pause menu are presented as a
finite flat panel in the room so their native cursor remains usable. After a
team and class are selected and the menu closes, presentation switches
automatically to immersive stereo VR. A panel containing two side-by-side eye
views is a renderer fault, not the expected multiplayer view.

Controller confirmation on these active in-match MP panels uses WaW's native
Enter route after the ray has focused an item. This is intentional: the exact
MP executable can bypass Mouse1-down while catcher `0x10` owns the team/class
UI. The MP frontend retains ordinary Mouse1 confirmation.

During immersive MP gameplay, the local tracer and predicted impact must begin
at the tracked barrel and agree with the authoritative fixed-ADS shot. The
launcher explicitly enables first-person tracers for this exact build.

## Add PeZBOT

The 2009 PC game does not provide useful built-in offline multiplayer bots.
World War VR therefore supports the user-supplied **PeZBOT 005p for World at
War 1.7** archive. The mod is not bundled or downloaded by this project.

Obtain `PeZBOTWAW_005p.zip`, then do one of the following:

Recorded source page:
[PeZBOT 005p for World at War](https://www.moddb.com/mods/pezbot/downloads/pezbot-005p-for-world-at-war).

- place it beside `WorldWarVR.exe`;
- leave it in the current Windows Downloads folder; or
- pass `--pezbot-archive C:\path\to\PeZBOTWAW_005p.zip`.

The launcher accepts only this supported archive identity:

- file size: `1,138,246` bytes;
- MD5: `4defeab88624baf05d28bbebf6c86c01`;
- SHA-256:
  `b7958b96cbe3a8c316290df7148c63ca601d1de2f96f6166d2c67fe069500fdf`.

On the first explicit frontend/direct-MP preparation or launch, a fixed,
hash-verified helper extracts only PeZBOT's allowlisted files into the resolved
MP home. Default standalone paths use
`%LOCALAPPDATA%\WaWVR\home-mp\mods\mp_PeZBOTWAW`; an explicit SP
`--homepath <root>\home` instead uses sibling
`<root>\home-mp\mods\mp_PeZBOTWAW`. Archive traversal, links,
unexpected files, duplicate paths, unsafe compression, and changed output are
rejected. The launcher records and rechecks every extracted file hash. It
never overwrites or deletes an existing unrecognized/custom
`mp_PeZBOTWAW` folder.

When the verified mod is present, the launcher selects it with `fs_game`, loads
its configuration, and requests nine authentic-weapon bots at a moderate
skill. Start a local server from the normal MP menu; PeZBOT then supplies the
players. PeZBOT targets stock competitive multiplayer maps and modes, not
Zombies or online co-op.

PeZBOT consumes its bot-count dvar and resets it to zero after populating one
map. WaWVR therefore reissues the nine-bot request once for every new MP server
lifecycle: either in the fully disconnected frontend or on the first loading
frame after a previously active server exits. A latch spans the complete
`6 -> 7 -> 6 -> 8` connected map-change path. Remaining in menus, opening
pause/team/class panels, and later loading frames cannot add another roster.
During automatic rotation, PeZBOT's restored team counts and WaWVR's main
request are consumed and cleared together by the same one-shot script pass, so
they cannot create two rosters. All stock 1.7 multiplayer maps and
standard competitive modes are covered by the supplied PeZBOT waypoints and
gametype scripts; the listen server still needs at least ten player slots for
one human plus all nine bots.

World at War MP resolves loose mod archives and the mod fastfile through
different legacy roots. The launcher sets both `fs_homepath` and
`fs_localAppData` to the same isolated MP home so `PeZBOTWaW.iwd` and `mod.ff`
are loaded as one mod without writing into the normal Activision profile.

If the archive is missing or rejected, ordinary offline MP still opens. The
launcher diagnostic reports that bots are unavailable and explains which
identity or install check failed.

## Read-only check and stage-only preparation

```text
wawvr-launcher.exe --diagnose --multiplayer --mod-dll WorldWarVR.dll
wawvr-launcher.exe --prepare --multiplayer --mod-dll WorldWarVR.dll
```

Diagnosis does not create directories, extract files, or start a process.
Preparation stages the isolated runtime and, when the exact archive is
available, imports PeZBOT without starting the game.
Use the console-subsystem `wawvr-launcher.exe` for these commands so output and
exit status remain visible; the branded GUI executables are intended for
Explorer double-click launch and display failures in a Windows message box.

Do not use this legacy executable on public servers. World War VR's MP target
is a local listen-server session with bots only.
