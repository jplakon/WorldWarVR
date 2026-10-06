# Third-party notices and source provenance

World War VR first-party code is licensed under **GPL-3.0-only** as stated in the root `LICENSE`. The following upstream work, libraries, tools, and research references remain subject to their own terms. Release packages include the applicable runtime license and notice files beside the application.

## WorldWarVR handoff source

- Repository: <https://github.com/RyanCraighead/WorldWarVR-Project>
- Pinned handoff commit: `3beac3f7bb1485be14743b92495656a1e5c0acad`
- Author/maintainer at handoff: Ryan Craighead
- License: GPL-3.0-only for first-party code, with upstream third-party terms
- Role: retail World at War profiles, T4 validation and ABI research, injection, launcher/staging, renderer, and gameplay-hook foundation

Copyright and license notices retained from the handoff source continue to apply to retained or adapted files. See `PROVENANCE.md` for the contribution boundary.

## KisakCOD / Call of Duty 4 VR product reference

- Repository: <https://github.com/jplakon/CallOfDuty4_VR>
- Pinned product commit: `271e4ac8f8a55433a46aeaf4423c4127198ab02d`
- Maintainer: John Plakon
- License: GPL-3.0
- Role: product behavior and implementation reference for frame semantics, controls, hands, two-hand support, physical reloads, grenades, scopes, HUD/menu behavior, installer, and release workflow

World War VR adapts compatible behavior to retail World at War interfaces. COD4 engine structures and ownership assumptions are not treated as interchangeable with World at War.

## Khronos OpenXR SDK and loader

- Source: <https://github.com/KhronosGroup/OpenXR-SDK>
- Pinned revision: `64f2b37c8c6da3d83c9b4d11865ba1fb752cb8ec`
- SDK version: 1.1.60
- Upstream license expression for the relevant loader files: Apache-2.0 OR MIT
- Selected distribution path: Apache-2.0
- Source terms: `third_party/openxr-sdk/LICENSE` and `third_party/openxr-sdk/LICENSES/Apache-2.0.txt`

The x86 OpenXR loader is statically linked into `WorldWarVR.dll`. The player package includes the selected OpenXR SDK license text.

## JsonCpp

- Source: <https://github.com/open-source-parsers/jsoncpp>
- Vendored through the pinned OpenXR SDK under `src/external/jsoncpp`
- OpenXR SDK tree object: `ee75252fe4102ad31f0a0aecd9c4cfba6adc2dcf`
- Version: 1.9.6
- License: Public Domain and/or MIT, as described by the upstream authors
- Source terms: `third_party/openxr-sdk/src/external/jsoncpp/LICENSE`

JsonCpp implementation objects are statically linked through the OpenXR loader. The player package includes the JsonCpp license text.

## Microsoft .NET, Windows App SDK, and NuGet components

The self-contained Windows launcher includes Microsoft .NET 8 runtime files and Windows App SDK 1.8 runtime files. Its locked build dependency graph includes:

- `Microsoft.WindowsAppSDK` 1.8.260317003
- `Microsoft.Windows.SDK.BuildTools` 10.0.28000.2526
- `Microsoft.Windows.SDK.BuildTools.WinApp` 0.5.0

Some entries above are build-time packages rather than shipped runtime files. Their Microsoft license and notice terms apply independently of the World War VR GPL license. The release packager preserves the vendor notices collected under the package's `licenses/dotnet` and `licenses/nuget` directories for components actually delivered. Consult those packaged files for the exact contents of a particular release.

## Microsoft C/C++ toolchain and Windows SDK

Native release binaries use Microsoft Visual C++ and Windows SDK headers, libraries, and runtime components. Release maintainers must build under and comply with a qualifying Visual Studio/MSVC license and the applicable Windows SDK redistribution terms. Qualifying compiled Distributable Code is governed by those product licenses; no Microsoft `.lib` file is shipped separately.

- Visual Studio 2022 redistribution: <https://learn.microsoft.com/visualstudio/releases/2022/redistribution>
- Visual Studio Community 2022 terms: <https://visualstudio.microsoft.com/license-terms/vs2022-ga-community/>
- Windows SDK redistributable code: <https://learn.microsoft.com/en-us/legal/windows-sdk/redist>

Windows, Direct3D 9, Direct3D 11, DXGI, D3DCompiler, and other Win32 runtime DLLs are supplied by Windows and are not packaged as World War VR files.

## Inno Setup

- Product: Inno Setup 6.7.3
- Role: optional build-time compiler for the guided Setup executable
- Patreon release role: none; the paid portable bundle does not contain a Setup executable or compiler binaries

The pinned compiler requests a commercial license for commercial use. Release
maintainers must obtain the appropriate license before putting its output behind
a paywall, or use a toolchain whose terms cover that distribution. This notice
does not replace the compiler's own license terms.

## t4-rtx research reference

- Source: <https://github.com/xoxor4d/t4-rtx>
- Audited revision: `ade9b2fe9d4101c093169bcc4fee86e88bef7296`
- Repository license at the audited revision: none found
- Release role: none; factual reverse-engineering research only

No t4-rtx source, shader, asset, object, or binary is copied or packaged.

## Original launcher artwork

The launcher background and VR tile were created specifically for World War VR and are first-party project assets, not extracted game assets. Their source and derivative record is maintained in `launcher-ui/WorldAtWarVR.Launcher/Assets/Launcher/ASSET-PROVENANCE.md`.

## Call of Duty: World at War and optional PeZBOT

Call of Duty: World at War is proprietary third-party software external to this project. World War VR does not include its executable, DLLs, fastfiles, IWD archives, maps, textures, audio, videos, saves, keys, or other game content. Users must supply a legitimate compatible copy.

PeZBOT is optional and user-supplied. World War VR does not distribute its archive or extracted files; the first-party importer only validates and stages a compatible archive supplied by the user.

Call of Duty and Call of Duty: World at War are trademarks of Activision Publishing, Inc. World War VR is not affiliated with, endorsed by, or sponsored by Activision. Microsoft, Windows, OpenXR, SteamVR, Meta, Quest, and other names and marks belong to their respective owners; their mention does not imply endorsement.
