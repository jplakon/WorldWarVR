#!/usr/bin/env python3
"""Build a deterministic, unsigned WorldAtWarVR .vrmod handoff artifact."""

from __future__ import annotations

import argparse
import os
import shutil
import sys
import zipfile
from pathlib import Path

from audit_vrmod_payload import (
    AuditError,
    audit_release_root,
    normal_existing_directory,
    normal_existing_file,
    prepare_output_file,
    reject_reparse_ancestors,
    write_canonical_json,
)
from verify_vrmod_archive import verify_archive


FIXED_EPOCH = 946684800
FIXED_ZIP_TIME = (2000, 1, 1, 0, 0, 0)
SOURCE_MAP = {
    "payload/wawvr/WorldAtWarVR.exe": "WorldAtWarVR.exe",
    "payload/wawvr/WorldAtWarVR.dll": "WorldAtWarVR.dll",
    "payload/wawvr/WaWVR-PeZBOT-Import.ps1": "WaWVR-PeZBOT-Import.ps1",
    "licenses/WORLDATWARVR-PROPRIETARY-1.0.txt": "LICENSE",
    "licenses/OPENXR-SDK-Apache-2.0.txt": "LICENSE-OPENXR-SDK.txt",
    "licenses/JSONCPP.txt": "LICENSE-JSONCPP.txt",
    "licenses/THIRD-PARTY-NOTICES.md": "THIRD-PARTY-NOTICES.md",
}


def _normal_new_path(path: Path, description: str) -> Path:
    lexical = reject_reparse_ancestors(path, description)
    try:
        os.lstat(lexical)
    except FileNotFoundError:
        pass
    else:
        raise AuditError(f"{description} already exists; use a new output path: {lexical}")
    lexical.parent.mkdir(parents=True, exist_ok=True)
    reject_reparse_ancestors(lexical.parent, f"{description} parent")
    reject_reparse_ancestors(lexical, description)
    return lexical


def _copy_file(source: Path, destination: Path) -> None:
    source = normal_existing_file(source, "release input")
    destination = prepare_output_file(
        destination, "release destination", must_not_exist=True
    )
    shutil.copyfile(source, destination)
    os.utime(destination, (FIXED_EPOCH, FIXED_EPOCH))


def build_release_root(package: Path, release_root: Path, repository: Path) -> None:
    package = normal_existing_directory(package, "standalone package")
    repository = normal_existing_directory(repository, "repository")
    release_root.mkdir()
    reject_reparse_ancestors(release_root, "release root")
    for destination, package_source in SOURCE_MAP.items():
        _copy_file(package / package_source, release_root / Path(destination))
    templates = repository / "distribution" / "world-at-war-vr" / "licenses"
    _copy_file(templates / "components.json", release_root / "licenses" / "components.json")
    for directory in sorted(
        (path for path in release_root.rglob("*") if path.is_dir()),
        key=lambda path: len(path.parts),
        reverse=True,
    ):
        os.utime(directory, (FIXED_EPOCH, FIXED_EPOCH))
    os.utime(release_root, (FIXED_EPOCH, FIXED_EPOCH))


def build_archive(release_root: Path, artifact: Path, inventory: dict) -> None:
    expected_paths = [item["path"] for item in inventory["files"]]
    if expected_paths != sorted(expected_paths):
        raise AuditError("audited inventory is not ordinal path sorted")
    with zipfile.ZipFile(
        artifact,
        mode="x",
        compression=zipfile.ZIP_STORED,
        allowZip64=False,
        strict_timestamps=True,
    ) as archive:
        archive.comment = b""
        for relative in expected_paths:
            source = normal_existing_file(
                release_root / Path(relative), "audited archive input"
            )
            data = source.read_bytes()
            info = zipfile.ZipInfo(relative, date_time=FIXED_ZIP_TIME)
            info.compress_type = zipfile.ZIP_STORED
            info.create_system = 0
            info.external_attr = 0
            info.internal_attr = 0
            info.extra = b""
            info.comment = b""
            archive.writestr(info, data, compress_type=zipfile.ZIP_STORED)
    os.utime(artifact, (FIXED_EPOCH, FIXED_EPOCH))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--release-root", required=True, type=Path)
    parser.add_argument("--policy", required=True, type=Path)
    parser.add_argument("--inventory", required=True, type=Path)
    parser.add_argument("--artifact", required=True, type=Path)
    args = parser.parse_args()
    try:
        repository = normal_existing_file(Path(__file__), "builder script").parent.parent
        release_root = _normal_new_path(args.release_root, "release root")
        inventory_path = _normal_new_path(args.inventory, "inventory output")
        artifact = _normal_new_path(args.artifact, "artifact output")
        if release_root in artifact.parents or release_root in inventory_path.parents:
            raise AuditError("artifact and inventory outputs must remain outside the release root")
        build_release_root(args.package, release_root, repository)
        inventory = audit_release_root(release_root, args.policy)
        write_canonical_json(inventory_path, inventory)
        build_archive(release_root, artifact, inventory)
        result = verify_archive(artifact, args.policy, inventory_path)
        print(
            f"built unsigned internal artifact: {artifact}\n"
            f"artifact size: {result['artifact']['size']}\n"
            f"artifact SHA-256: {result['artifact']['sha256']}\n"
            f"files: {result['fileCount']}\n"
            f"expanded bytes: {result['expandedBytes']}"
        )
        return 0
    except (AuditError, OSError, zipfile.BadZipFile) as exc:
        print(f"vrmod build failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
