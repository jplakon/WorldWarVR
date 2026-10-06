# Install World War VR

World War VR is a pre-release OpenXR mod for the Windows retail version of **Call of Duty: World at War**. You must supply your own legitimate, unmodified game installation. The mod and its releases do not contain game executables, maps, textures, audio, or other Activision game content.

## Requirements

- Windows 10 version 2004 (build 19041) or newer, or Windows 11, 64-bit
- Call of Duty: World at War updated to version 1.7
- A PC VR headset with an active OpenXR runtime
- Two tracked motion controllers
- A DirectX 11-capable VR system

See [release/repository/REQUIREMENTS.md](release/repository/REQUIREMENTS.md) for the currently tested headset and connection combinations.

## Download one release

Download version-matched assets from
[GitHub Releases](https://github.com/jplakon/WorldWarVR/releases). The mod is
free; no Patreon subscription is required:

- `WorldWarVR-v<version>-Portable.zip` — required to play
- `WorldWarVR-v<version>-Source.zip` — complete matching source, optional to play
- `SHA256SUMS.txt` or the corresponding `.zip.sha256` sidecar — download verification
- `GPL-SOURCE-NOTICE.txt` — source and license information

The release is portable and includes no installer. Do not download GitHub's
automatically generated source-code archive expecting the game launcher. The
separately attached Source ZIP includes all pinned dependency contents needed
for rebuilding; it is provided without an additional fee.

To verify a download in PowerShell, run:

```powershell
Get-FileHash .\WorldWarVR-v0.4.0-beta.3-Portable.zip -Algorithm SHA256
Get-Content .\SHA256SUMS.txt
```

The computed hash must match the line for that exact filename in SHA256SUMS.txt.
Use the same check for the Source ZIP if downloaded. Each ZIP also has an
adjacent `.zip.sha256` sidecar. Use the actual release filename for later versions.

## Portable ZIP

1. Download and verify `WorldWarVR-v<version>-Portable.zip`.
2. Extract it into its own new, empty folder.
3. Do **not** extract it into the Call of Duty: World at War folder.
4. Run `WorldWarVR.exe` from the portable folder.
5. Browse to the folder containing `CoDWaW.exe` if automatic detection does not find it.

Keep all files from the portable ZIP together. To update, extract the new
release into a new folder rather than mixing versions. The adjacent
`WorldWarVR-v<version>-Source.zip` is the corresponding source for the
binary release; it is not required merely to play the mod. The launcher does
not automatically download updates. Download each update manually from Releases.

## First launch

1. Start and connect the headset before launching the game.
2. Make sure the OpenXR runtime for the connection you intend to use is active.
3. Select a launch target:
   - **Main Menu** for Zombies and Campaign
   - **Multiplayer** for experimental local/offline multiplayer
4. Select a quality preset:

   | Preset | Packed render size | Per-eye view | Use it when |
   | --- | ---: | ---: | --- |
   | Native | 6016 x 2688 | 2496 x 2688, plus a 1024 x 1024 physical-scope view | Maximum image quality on a fast GPU |
   | Performance | 3744 x 2016 | 1872 x 2016 | Best starting point for most systems |
   | Recovery | 2560 x 1440 | 1280 x 1440 | Diagnosing black screens, stutter, or limited GPU headroom |

5. Leave **Launch in VR** enabled and select **Launch**.

Settings are saved automatically.

Every standard campaign mission is automatically available in Mission Select
when World War VR starts the single-player game. This permanently advances the
active profile's stock `mis_01` mission-progression counter to WaW's unlock-all
value. It does not enable developer mode, replace campaign save files, or alter
saved mission difficulty records.

### Quest Air Link

For the currently tested Quest Link/Air Link path, install SteamVR, make SteamVR the active OpenXR runtime, and enable **Quest Air Link compatibility** in the launcher. This option selects the 32-bit SteamVR OpenXR path used by World at War. Quest 3 physical-headset testing confirmed upright stereo, correct head/controller motion, weapon tracking, and pause/resume on the tested SteamVR configuration; other SteamVR builds may behave differently.

Virtual Desktop should normally use its selected OpenXR runtime without the Air Link compatibility option.

World at War is a 32-bit process. A working 64-bit OpenXR application does not
by itself confirm a usable 32-bit runtime. Native Pimax tests use Pimax as the
active runtime with the compatibility switch off; SteamVR tests use its x86
route with the switch on. Index startup/no-image and some Pimax runtime reports
remain unresolved. Do not repeatedly switch runtimes without recording which
route and error occurred.

## Optional offline bots

Local multiplayer bots are experimental and require a user-supplied PeZBOT package. World War VR does not redistribute PeZBOT. Follow [docs/OFFLINE_MULTIPLAYER.md](docs/OFFLINE_MULTIPLAYER.md) and use only the exact supported archive named there.

## Troubleshooting

- **The headset is black but the desktop mirror moves:** close the game, reconnect or restart the headset runtime, then relaunch from World War VR. Start with the Recovery preset.
- **The game opens only on the desktop:** verify that **Launch in VR** is enabled and that the intended OpenXR runtime is active before launch.
- **The image stutters or slows while holding a weapon:** retry with Performance, then Recovery, and close GPU-heavy overlays or recording tools.
- **Controllers are not detected:** wake both controllers before launch, confirm they are visible in the headset runtime, then restart the game.
- **The launcher rejects the game:** confirm that the selected folder contains a compatible World at War 1.7 executable. Modified or unsupported executables may not be accepted.
- **A menu appears blank in VR:** use the native-menu panel and right-controller pointer. If the panel remains blank, return to the main menu and relaunch.

Before reporting a problem, read [KNOWN-ISSUES.md](KNOWN-ISSUES.md). Include the exact release version, mode and map, headset and connection method, quality preset, reproducible steps, and `WorldWarVR.log` if one was generated.

Report through [GitHub Issues](https://github.com/jplakon/WorldWarVR/issues).
Review logs for personal information before posting. Do not upload proprietary
game executables, map files, keys, or saves publicly.
