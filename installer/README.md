# World War VR installer

This directory retains the optional guided, per-user Setup build. It is not part
of the paid Patreon release unless the maintainer has licensed the commercial
installer toolchain. The current Patreon contract is documented in
`docs/MANUAL_RELEASE.md`.

When appropriately licensed, the installer build produces:

```text
WorldWarVR-v<version>-Setup.exe
WorldWarVR-v<version>-Setup.exe.sha256
```

`scripts/package-patreon.ps1` intentionally skips this installer and produces
the portable binary plus complete matching source instead.

The installer consumes a clean, already-built payload directory. It never reads a game installation, and its payload must not contain game files, optional bot assets, local logs, build outputs, or nested release files.

## Required payload layout

The payload root contains the complete self-contained launcher plus these branded and native files:

```text
WorldWarVR.exe
WorldWarVR.Launcher.dll
WorldWarVR.Launcher.Core.dll
WorldWarVR.Launcher.deps.json
WorldWarVR.Launcher.runtimeconfig.json
WorldWarVR.pri
WorldWarVR.ico
wawvr-launcher.exe
WorldWarVR.dll
WaWVR-PeZBOT-Import.ps1
LICENSE
THIRD-PARTY-NOTICES.txt
INSTALL.txt
CONTROLS.txt
KNOWN-ISSUES.txt
REQUIREMENTS.txt
OFFLINE-MULTIPLAYER.txt
PROVENANCE.txt
ASSET-PROVENANCE.txt
CHANGELOG.txt
SOURCE.txt
BUILD-INFO.txt
PAYLOAD-SHA256.txt
licenses\OpenXR-SDK\LICENSE.txt
licenses\JsonCpp\LICENSE.txt
licenses\nuget\...vendor license and notice files...
licenses\dotnet\...runtime license and notice files...
...self-contained WinUI and .NET runtime files...
```

Only `WorldWarVR.exe` receives a **World War VR Community** Start menu shortcut. The managed assemblies, native launcher, VR DLL, importer, runtime dependencies, and applicable license files remain beside it as application support files. The player package excludes research-only material and source-development files.

## Build the installer directly

Install the pinned Inno Setup 6.7.3 compiler, then run:

```powershell
.\installer\build-installer.ps1 `
  -PayloadDir .\artifacts\standalone-payload `
  -OutputDir .\artifacts\installer `
  -Version 0.4.0-alpha.2 `
  -InnoCompiler 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
```

`-InnoCompiler` is optional when that compiler is available through `PATH` or a standard installation location. The script:

- validates the exact payload shape;
- rejects known game, third-party-mod, debug, and nested-release files;
- verifies the pinned compiler SHA-256;
- creates `WorldWarVR-v<version>-Setup.exe`;
- creates the matching `.sha256` sidecar; and
- reports Authenticode status.

Pass `-RequireSignature` only when code signing is part of the release gate.

For local non-commercial installer validation only, use:

```powershell
.\scripts\package-standalone.ps1 `
  -Configuration Release `
  -Version 0.4.0-alpha.2 `
  -InnoCompiler 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
```

Use a fresh output directory. Packaging refuses to overwrite an existing
artifact. Do not upload the resulting Setup to a paid channel without an
appropriate Inno Setup commercial license.

## Installation behavior

- Scope: current Windows user
- Default location: `%LOCALAPPDATA%\Programs\World War VR Community`
- Administrator access: not required for the normal installation
- Shortcuts: World War VR Community Start menu shortcut and optional desktop shortcut
- Uninstall: standard Windows uninstall entry
- Game installation: not copied into, overwritten, or removed

The stable, fork-specific installer identity allows an older **World War VR Community** installation to upgrade in place without sharing ownership with the original project's installer. Setup never treats a writable manifest as permission to delete files, and it does not enumerate or remove unknown user files.

## Tool license

The pinned Inno Setup 6.7.3 compiler identifies itself as **Non-commercial use only**. It is suitable only while the release and its distribution satisfy that license. Before enabling paid distribution, sponsorship, donation-point monetization, or another commercial use, confirm the tool's license, obtain the required license, or move the setup definition to an appropriate toolchain.
