# Free GitHub release checklist

Use this checklist for World War VR public pre-releases. A release is not
complete until the portable binary and complete matching source are available
together without a subscription at https://github.com/jplakon/WorldWarVR/releases.
For beta.4, retain the accepted chest-test native DLL and production native
launcher helper. The WinUI launcher is republished with beta.4 version metadata;
document that boundary separately from physical-headset acceptance.

## Version and source

- [ ] Choose one semantic version and use it in every filename and document.
- [ ] Confirm the source snapshot contains only intended source, documentation,
      build metadata, and redistributable project assets.
- [ ] Confirm the exact pinned OpenXR submodule source is included.
- [ ] Run the native and managed builds and tests from the exact snapshot.
- [ ] Review `LICENSE`, `PROVENANCE.md`, and `THIRD-PARTY-NOTICES.md`.
- [ ] Confirm the binary package and source package were produced together.
      If retaining accepted native binaries, verify their exact identity and
      compare the compiled source inputs against the accepted source snapshot.

## Required GitHub release assets

- [ ] `WorldWarVR-v<version>-Portable.zip` and adjacent `.zip.sha256`
- [ ] `WorldWarVR-v<version>-Source.zip` and adjacent `.zip.sha256`
- [ ] `SHA256SUMS.txt` and `GPL-SOURCE-NOTICE.txt`
- [ ] Confirm the Source ZIP includes complete pinned OpenXR contents, not
      merely a Git submodule reference.
- [ ] Recompute every SHA-256 hash from the final byte stream.
- [ ] Confirm the source is available in the same public release with no additional fee.

## Content safety

- [ ] Confirm no Call of Duty executable, DLL, map, texture, audio, key, save,
      or other game file is present.
- [ ] Confirm no PeZBOT archive or extracted bot file is present.
- [ ] Confirm no Git metadata, build output, local log, credential, private
      record, machine-specific path, or reparse point is present.
- [ ] Confirm the portable ZIP extracts into one self-contained folder.
- [ ] Confirm the source ZIP contains all build scripts, project files,
      dependency pins, and license notices needed to rebuild the release.
- [ ] Confirm no unlicensed commercial-use installer is included.

## Runtime acceptance

Record what was actually tested and on which binary/runtime/headset. The prior
representative-weapon and grenade checks below are historical acceptance, not
a claim that beta.4 received a new full Campaign playthrough. Record beta.4's
focused chest, grip, and reload checks separately; do not inherit every box as
newly verified on this release or on every headset.

- [ ] Run the portable launcher from a fresh folder and verify it accepts a
      legitimate World at War 1.7 installation without modifying game files.
- [ ] Confirm coherent stereo, correct aspect ratio, comfortable scale,
      rotational tracking, and positional tracking in a headset.
- [ ] Confirm both controllers, right-hand firing, two-hand weapon support,
      grip handoff, snap turn, sprint, stance controls, melee, and menus.
- [x] Confirm representative bolt-action, magazine-fed, scope, manual reload,
      and empty-magazine charging interactions.
- [x] Confirm grenade belt grab, throw, ground pickup, and throwback.
- [ ] From a profile with locked campaign progression, open **Solo > Mission
      Select** and confirm that all 15 campaign missions are visible and
      selectable.
- [ ] Smoke-test Zombies and Campaign; record all limitations.
- [ ] Confirm online Multiplayer is not requested or tested.

## GitHub publication

- [ ] State that World War VR is free and GPL-3.0-only; recipients may redistribute it.
- [ ] State that the matching source is included and no game files are included.
- [ ] Publish approved source and tag; create a GitHub pre-release, not a stable release claim.
- [ ] Attach both ZIPs, checksums, source notice, and truthful release notes.
- [ ] Download all release assets and verify their SHA-256 hashes again.
- [ ] Keep the source available wherever and whenever the binary is offered.
- [ ] Archive exact release text, asset identities, hashes, and test evidence.
- [ ] Confirm no Index/Pimax/Rift S-specific fix is promised without affected-user acceptance.
- [ ] Confirm updates are documented as manual and no credentials/private reports were published.
