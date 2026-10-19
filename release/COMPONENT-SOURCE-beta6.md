# Beta.6 corresponding-source and retained-binary map

This release retains the exact headset-tested gameplay DLL rather than
rebuilding it during packaging. The native helper is also retained unchanged.
The source ZIP includes the complete pinned OpenXR source and all first-party
build, test, launcher, license and provenance files; it contains no private
sessions, diagnostic result logs, game content, simulator or user profiles.

| Shipped component | Exact SHA-256 | Corresponding source |
| --- | --- | --- |
| `WorldWarVR.dll` | `B4E3A6455FC3219D780F2FFFDA63AEF9F34FBFE1FE072BC326512D79B4A8C1C5` | Root `src`, `CMakeLists.txt`, pinned `third_party/openxr-sdk`; native manifest below |
| `wawvr-launcher.exe` | `19008C93EFE9C0F93892B69CFF4D8466F7AF1AA102CF47810F275DDE4F8B6856` | Root `launcher`, `src/t4`, `CMakeLists.txt`; source32-based accepted GUI-subsystem helper, retained not rebuilt |
| Managed `WorldWarVR.exe` and launcher assemblies | Listed in the portable payload manifest | Root `launcher-ui`; beta.5 implementation, beta.6 version/source-commit metadata only |
| Third-party managed/runtime files | Listed in the portable payload manifest | Vendor packages pinned by launcher project/lock files; runtime licenses under portable `licenses` |

`release/NATIVE-SOURCE-SHA256.txt` pins the exact reviewed native source/build
inputs. The original reviewed set contained 832 source/dependency files;
player-facing documentation is updated separately for beta.6. Root native
`src`, native `launcher`, build configuration and dependency bytes retain the
tested candidate's source, not the mutable development checkout or deferred
grenade experiment. The original native build passed 102 CTests. Later native
Tester checks and the local starting-Colt headset success apply to this exact
DLL; they do not certify all weapons, a settled visual zero or the final ZIP's
entire Campaign playthrough.

The retained helper was built from the accepted source32 launcher implementation.
A new clean build can have different PE/COFF bytes; no claim of byte-for-byte
reproducibility is made. The rebuilt managed GUI does not incorporate the
separate Regional-Compatibility Test1 behavior change. Manual updates remain
the supported distribution model; no automatic GitHub updater is introduced.

The complete Source ZIP preserves all 812 reviewed native input bytes exactly.
First-party native inputs are also stored without Git text conversion. The
upstream OpenXR submodule remains pinned to its original revision; its Windows
checkout line endings may differ from canonical upstream Git blobs, with every
file checked through Git's canonical filters. Ordinary template text likewise
uses canonical Git normalization. Use the complete Source ZIP and its manifest
when exact reviewed checkout bytes are needed rather than a tag-only archive.

The historical `component-source/beta4` tree and its notices are preserved for
the earlier retained components and provenance. Its old version references are
intentional, not instructions to merge older binary files into this release.

Build native code using CMake's Visual Studio 2022 Win32 generator in a fresh
build folder, with `-DWAWVR_BUILD_TOOLS=OFF`, then `--config Release` and the
CTest Release suite. Build/publish the x64 self-contained managed launcher
separately using the pinned launcher project/dependency locks. Do not invoke a
private test runner or add an OpenXR simulator to the public payload. The
release's Portable ZIP keeps the retained native binaries even when rebuilding
from source produces different bytes.
