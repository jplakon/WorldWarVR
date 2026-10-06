# Launcher publishing contract

Publish the launcher as an unpackaged, self-contained Windows x64 application:

```powershell
dotnet restore .\WorldAtWarVR.Launcher.csproj -r win-x64 --locked-mode
dotnet publish .\WorldAtWarVR.Launcher.csproj -c Release -r win-x64 --self-contained true -p:Platform=x64 --no-restore -o .\artifacts\publish\win-x64
```

The `BrandPublishedAppHost` target renames the generated native apphost to `WorldWarVR.exe` and its WinUI resource index to `WorldWarVR.pri`. Those basenames must remain identical for unpackaged WinUI resource lookup.

The managed entry assembly is `WorldWarVR.Launcher.dll`, and the shared core assembly is `WorldWarVR.Launcher.Core.dll`. The `.Launcher` suffix reserves the public `WorldWarVR.dll` filename for the native x86 VR mod.

## Complete payload

The release packager copies the complete publish directory, then stages these native/support files beside `WorldWarVR.exe`:

- `wawvr-launcher.exe`
- `WorldWarVR.dll`
- `WaWVR-PeZBOT-Import.ps1`
- `WorldWarVR.ico`
- first-party and third-party license files

The UI resolves its support files through `AppContext.BaseDirectory`; published binaries must not depend on a repository checkout or developer-machine path. Never add World at War game files or optional PeZBOT bytes.

## Free GitHub packaging

Follow `docs/MANUAL_RELEASE.md` and the release checklist. Keep one version in
launcher metadata, release filenames, source snapshot, and tag. The first
free GitHub pre-release is `v0.4.0-beta.3` and retains the existing beta.3
binaries; documentation and distribution changes do not require claiming a
new gameplay build or hardware acceptance.

The public GitHub release contains:

```text
WorldWarVR-v<version>-Portable.zip
WorldWarVR-v<version>-Portable.zip.sha256
WorldWarVR-v<version>-Source.zip
WorldWarVR-v<version>-Source.zip.sha256
SHA256SUMS.txt
GPL-SOURCE-NOTICE.txt
```

The Source ZIP includes complete matching first-party source and the pinned
OpenXR submodule contents. GitHub's automatically generated source archives
do not include ordinary submodule contents and do not replace that asset.
The historical `scripts/package-patreon.ps1` workflow remains in source for
earlier releases; it does not make a public download require a subscription.

The launcher does not contact a GitHub update endpoint. Updates are manual
downloads from https://github.com/jplakon/WorldWarVR/releases and support
reports use https://github.com/jplakon/WorldWarVR/issues. No installer is included.

## Runtime packaging notes

- The launcher remains unpackaged because `WindowsPackageType` is `None`.
- `EnableMsixTooling` remains enabled only so the WinUI PRI resource index is generated.
- Windows App SDK and .NET are self-contained and their applicable notices must remain in the package.
- The player package is portable and does not require administrator access.
- Use a fresh output directory; release packaging must refuse to overwrite prior artifacts.
