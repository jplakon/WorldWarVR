# Beta.8 corresponding source and retained binaries

Beta.8 is the Ammo Pouch & Aiming Update. It retains the accepted binaries
byte-for-byte, with updated public documentation and packaging. It does not
rebuild gameplay or the launcher merely to change version labels.

| Shipped component | SHA-256 | Corresponding source |
| --- | --- | --- |
| `WorldWarVR.dll` | `CD6719ECBE96B30989937B8EFF448CA9531FB960C5A8CC0653C42F41BF411A51` | Root `src`, `CMakeLists.txt`, pinned OpenXR SDK |
| `wawvr-launcher.exe` | `E4CC6A536434D73955B7858E3E0E4E3502C7FBD09AC6D7775D09F5EF9AC95ECF` | Root `launcher`, `src/t4`, `CMakeLists.txt`; GUI-subsystem helper |
| `WorldWarVR.exe` | `0E86BAFBB02274936F0F11EC2C58B252464164A1A2C7CD73A4DFB3BEB612E703` | Root `launcher-ui`; retained self-contained graphical launcher |
| `WorldWarVR.Launcher.dll` | `F79A9A2C64128B4AD6FA54FC1DF43C7C3E532CD1BFD2B91014411AC3D871D7FF` | Root `launcher-ui/WorldAtWarVR.Launcher` |
| `WorldWarVR.Launcher.Core.dll` | `9646AC07A23AAC658274D0FC1A8CA6BE1AB3C0AA287029B6D726E3AB4558FB34` | Root launcher core project |
| Managed runtime, resources and dependencies | Portable payload manifest | Included project/vendor packages, licenses and inherited assets |

The retained managed binaries/project metadata can still identify internal
version `0.4.0-beta.7` and baseline source commit
`27699926e9639c74dfe14bc61c8db5b1e9e63beb`. That commit alone is not the
corresponding source for these changed binaries. The beta.8 tag and complete
Source ZIP include the expanded aiming, pouch and personal-angle source. Use
public beta.8 filenames, checksums and this map for release identification.

The Source ZIP includes all current first-party source, historical retained
`component-source/beta4` source and notices, and the full pinned OpenXR SDK at
`64f2b37c8c6da3d83c9b4d11865ba1fb752cb8ec` from
https://github.com/KhronosGroup/OpenXR-SDK.git. GitHub's automatic tag archives
omit submodule contents; use the complete release Source ZIP. No gameplay model
assets, game executables, profiles, saves, private sessions or customer reports
are included. The source contains bounded numerical geometry witnesses used
for exact model validation, not redistributable game model/texture files.

The exact gameplay binary passed 114 native tests and focused OpenXR Tester
checks; positive local headset feedback does not certify the whole arsenal,
all scope impacts, all controllers or sustained performance. See KNOWN-ISSUES.md.
Test command routes in source require their explicit isolated simulator gates;
the player package configures no simulator, automated input or health cheats.

Build native code using the Visual Studio 2022 Win32 toolchain and the supplied
build script; rebuild the separate self-contained graphical launcher using its
locked project instructions. Preserve all original copyrights, GPL and third-party
notices. Exact compiler output across different toolchain paths is not promised.
