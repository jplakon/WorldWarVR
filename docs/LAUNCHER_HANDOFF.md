# Launcher architecture and historical managed-platform handoff

> **Archived managed-launcher record:** This document preserves the useful
> technical boundaries from an earlier RCVR integration proposal. It is not
> the current standalone release contract and does not block a Patreon World
> War VR tester release.

The current release authority is `README.md`, `INSTALL.md`,
`RELEASE-CHECKLIST.md`, `docs/MANUAL_RELEASE.md`, and the checked-in Patreon
packaging script. The former
machine-readable handoff manifests and historical candidate hashes were
internal planning artifacts and are no longer build, package, or release
inputs.

Every binary artifact must be accompanied by the complete matching GPLv3
source snapshot in the same Patreon release. The package validators enforce
the redistributable allowlist and reject game
files, Plutonium files, PeZBOT content, developer outputs, logs, and symbols.

## Historical managed-platform launch contract

The following design notes are retained for a possible future managed-platform
adapter. They do not govern the standalone launcher or installer.

The initial platform variant is only the stock frontend. The trusted launcher
main process resolves all paths and starts the x86 wrapper from
`<stage>/g/wawvr`; renderer input supplies no path or argument. SP and MP
runtime/home directories live under `<stage>/x` and `<stage>/h`, never under
the read-only managed game root `<stage>/g`.

The wrapper receives the exact SP and MP executable paths. During SP prepare it
atomically writes identity-bound V2 content to the historical
`WaWVR-Multiplayer-Handoff.v1` filename inside the SP runtime. The
stock menu's no-argument `CoDWaWmp.exe` shim requires the exact sibling
`CoDWaW.exe` parent, waits for it to exit, and reconstructs MP only from that
canonical config. It performs no fallback discovery for game/source/runtime/
home paths and disables PeZBOT archive discovery/import; it only inspects the
already prepared MP home.

The proposed trusted platform used a separate fixed prepare plan: `--prepare`
plus the same resolution, game, SP source, MP source, SP runtime, SP home, and
DLL paths as frontend launch, with `--launch`, `--menu`, and `--wait` omitted.
Install/update/verify would run only that non-launching plan. A future adapter
must enforce that every resolved path is under the intended stage, record the
prepared config identity, and revalidate it before explicit launch. Those
same-stage and receipt-integrity checks remain managed-platform concerns;
standalone World War VR supports game and isolated runtime/home paths on
different drives.

This is a 32-bit OpenXR product. The launcher needs a new strict x86 preflight
using `/reg:32`, `oculus_openxr_32.json`, an x86 signed Meta runtime library,
current-session `OVRServer_x64` and `OculusDash`, and an explicit SteamVR
process reject set. Existing x64 product variants must not be weakened or
optionalized.

## Current relevance

The standalone Patreon release is governed exclusively by the Patreon release
checklist and bundled-source packager. It does not depend on an RCVR
catalog, commerce, managed adapter, runtime guard, or managed-platform signing.

Any future RCVR or other managed-launcher integration is a separate project
with its own adapter, x86 OpenXR preflight, catalog and entitlement policy,
release metadata, runtime guard, signing, and publication authorization. None
of that work is a prerequisite for the standalone community mod.
