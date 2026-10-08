# Free GitHub release procedure

World War VR public pre-releases are distributed free from
https://github.com/jplakon/WorldWarVR/releases. No subscription is required.
The GPL-3.0-only license, upstream attribution, and third-party notices remain
in force; complete matching source must remain available alongside each binary.

This is a release-engineering checklist, not legal advice.

## Release contract

Each version provides these GitHub release assets:

```text
WorldWarVR-v<version>-Portable.zip
WorldWarVR-v<version>-Portable.zip.sha256
WorldWarVR-v<version>-Source.zip
WorldWarVR-v<version>-Source.zip.sha256
SHA256SUMS.txt
GPL-SOURCE-NOTICE.txt
```

The source ZIP is a complete snapshot of the matching superproject files and
pinned OpenXR submodule source. It includes build
and installation scripts, project files, notices, and license texts, while
excluding Git metadata, build outputs, game files, optional bot files, local
logs, and credentials.

## Prepare the exact payload

1. Close World at War and the launcher.
2. Review the intended source snapshot and version metadata. Never publish a
   developer worktree's private logs, credentials, saves, or unrelated files.
3. For a new binary build, follow the root README's native build instructions
   and the launcher publishing contract. Beta.4 retains the exact headset-
   accepted native gameplay DLL and production helper; the managed launcher is
   republished with beta.4 metadata from the tested implementation. Never ship
   a private simulator/profile-isolation helper as the production helper.

   ```powershell
   .\scripts\build.ps1 -Configuration Release -BuildRoot .\rebuild-beta4
   ```

4. Record native and managed build/test evidence for the binary being shipped.
   Do not present historical headset acceptance as new testing of changed code.
5. Confirm package checks report no unsafe paths, links, case collisions,
   prohibited game/bot content, unexpected binaries in source, or checksum
   mismatches.
6. Extract both release ZIPs into fresh folders and verify their SHA-256 hashes.
7. Test the portable launcher from a fresh folder and confirm the source ZIP
   contains the exact release source and build instructions.

## GitHub publication

1. Publish the approved source snapshot with license/provenance records intact.
   The repository retains the pinned OpenXR submodule. The complete Source ZIP
   also includes its contents; GitHub's automatically generated tag archive
   alone is not a complete submodule-inclusive source delivery.
2. Create the matching version tag and a GitHub **pre-release**. For this
   release, use `v0.4.0-beta.4` and the approved public snapshot.
3. Attach the Portable and complete Source ZIPs, their checksum sidecars,
   `SHA256SUMS.txt`, and `GPL-SOURCE-NOTICE.txt` to the same public release.
4. Use `release/GITHUB-POST-v0.4.0-beta.4.md` as the release notes. State that
   this is free and portable, no game files are included, and describe the
   exact local acceptance scope without closing affected-user reports.
5. Download the final assets and verify their hashes after upload. Keep the
   matching source available wherever and whenever that binary is offered.

## Patreon mirror

Publish a free-access release post linking to the same GitHub release. Attach
the release bundle containing the identical Portable and Source ZIPs, checksums,
release notes and GPL notice. No additional fee applies to matching source.
The historical paid-release script remains available for older workflows;
it does not make this public version a subscription-only download.

## Installer limitation

This release contains no Inno Setup executable. Keep the portable delivery
independent of installer tooling. Any later installer requires review of the
actual toolchain's applicable license and redistribution terms; free GitHub
hosting alone is not a blanket licensing determination.

## Final checks

- Confirm the binary and source share the exact same version.
- Recompute and compare all SHA-256 sidecars after the final upload/download.
- Confirm neither ZIP contains a World at War executable, DLL, map, texture,
  audio file, key, save, or PeZBOT content.
- Confirm the launcher does not contact a nonexistent GitHub update endpoint.
- Document manual updates rather than promising automatic GitHub updates.
- Record headset smoke-test results and known limitations in the post.
- Never impose an NDA or a no-redistribution term on the GPL-covered package.
