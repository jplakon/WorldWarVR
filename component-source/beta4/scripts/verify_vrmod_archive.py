#!/usr/bin/env python3
"""Verify the finished deterministic .vrmod ZIP against policy and inventory."""

from __future__ import annotations

import argparse
import hashlib
import re
import stat
import sys
import zipfile
from pathlib import Path
from typing import Any

from audit_vrmod_payload import (
    AuditError,
    SHA256_RE,
    canonical_json_bytes,
    load_json,
    normal_existing_file,
    require_exact_keys,
    require_int,
    require_string,
    sha256_file,
    validate_archive_path,
    validate_policy,
)


def validate_inventory(inventory: Any, policy: dict[str, Any]) -> list[dict[str, Any]]:
    inventory = require_exact_keys(
        inventory,
        {"schemaVersion", "productId", "fileCount", "expandedBytes", "files"},
        "audited inventory",
    )
    if require_int(inventory["schemaVersion"], "inventory schemaVersion", 1) != 1:
        raise AuditError("unsupported audited inventory schemaVersion")
    if require_string(inventory["productId"], "inventory productId") != policy["product_id"]:
        raise AuditError("inventory product identity differs from policy")
    files = inventory["files"]
    if not isinstance(files, list) or not files:
        raise AuditError("inventory files must be a nonempty array")

    normalized: list[dict[str, Any]] = []
    windows_keys: set[str] = set()
    for index, raw_file in enumerate(files):
        item = require_exact_keys(raw_file, {"path", "size", "sha256"}, f"inventory file[{index}]")
        path = validate_archive_path(item["path"], policy["path_rules"])
        windows_key = path.lower()
        if windows_key in windows_keys:
            raise AuditError(f"case-insensitive duplicate inventory path: {path}")
        windows_keys.add(windows_key)
        size = require_int(item["size"], f"inventory size for {path}")
        if size > policy["limits"]["max_file_bytes"]:
            raise AuditError(f"inventory file exceeds max_file_bytes: {path}")
        digest = require_string(item["sha256"], f"inventory SHA-256 for {path}")
        if not SHA256_RE.fullmatch(digest):
            raise AuditError(f"inventory SHA-256 is not canonical lowercase hex: {path}")
        normalized.append({"path": path, "size": size, "sha256": digest})

    if normalized != sorted(normalized, key=lambda item: item["path"]):
        raise AuditError("inventory files must be ordinal path sorted")
    file_count = require_int(inventory["fileCount"], "inventory fileCount", 1)
    expanded_bytes = require_int(inventory["expandedBytes"], "inventory expandedBytes", 1)
    if file_count != len(normalized):
        raise AuditError("inventory fileCount differs from its files array")
    if file_count > policy["limits"]["max_files"]:
        raise AuditError("inventory fileCount exceeds limits.max_files")
    if expanded_bytes != sum(item["size"] for item in normalized):
        raise AuditError("inventory expandedBytes differs from its files array")
    if expanded_bytes > policy["limits"]["max_expanded_bytes"]:
        raise AuditError("inventory expandedBytes exceeds limits.max_expanded_bytes")

    required: list[dict[str, Any]] = []
    for index, raw_required in enumerate(policy["required_files"]):
        item = require_exact_keys(
            raw_required,
            {"path", "size", "sha256"},
            f"required_files[{index}]",
        )
        path = validate_archive_path(item["path"], policy["path_rules"])
        size = require_int(item["size"], f"required file size for {path}")
        digest = require_string(item["sha256"], f"required file SHA-256 for {path}")
        if not SHA256_RE.fullmatch(digest):
            raise AuditError(f"required file SHA-256 is not canonical lowercase hex: {path}")
        required.append({"path": path, "size": size, "sha256": digest})
    if normalized != sorted(required, key=lambda item: item["path"]):
        raise AuditError("inventory does not exactly match policy required_files")
    return normalized


def verify_archive(artifact: Path, policy_path: Path, inventory_path: Path) -> dict[str, Any]:
    artifact = normal_existing_file(artifact, "artifact")
    policy = validate_policy(load_json(policy_path))
    inventory = load_json(inventory_path)
    expected_files = validate_inventory(inventory, policy)
    artifact_policy = policy["artifact"]
    pattern = re.compile(artifact_policy["file_name_pattern"])
    if not pattern.fullmatch(artifact.name):
        raise AuditError(f"artifact file name is not allowed: {artifact.name}")
    max_artifact = require_int(policy["limits"]["max_artifact_bytes"], "max_artifact_bytes", 1)
    artifact_size, artifact_hash = sha256_file(artifact, max_artifact)
    expected_by_path = {item["path"]: item for item in expected_files}
    allowed_methods = set(artifact_policy["allowed_compression_methods"])
    fixed_timestamp = tuple(artifact_policy["fixed_zip_timestamp"])
    expanded = 0
    actual: list[dict[str, Any]] = []
    windows_keys: set[str] = set()

    with zipfile.ZipFile(artifact, "r") as archive:
        if artifact_policy["require_empty_archive_comment"] and archive.comment:
            raise AuditError("archive comment is forbidden")
        entries = archive.infolist()
        if len(entries) != len(expected_files):
            raise AuditError("archive entry count differs from inventory")
        for entry in entries:
            path = validate_archive_path(entry.filename, policy["path_rules"])
            if entry.is_dir() or path.endswith("/"):
                raise AuditError(f"directory entries are forbidden: {path}")
            if entry.flag_bits & 0x1:
                raise AuditError(f"encrypted entries are forbidden: {path}")
            if entry.compress_type not in allowed_methods:
                raise AuditError(f"compression method is not allowed for {path}: {entry.compress_type}")
            if artifact_policy["require_empty_entry_extra"] and entry.extra:
                raise AuditError(f"ZIP extra fields are forbidden: {path}")
            if artifact_policy["require_empty_entry_comment"] and entry.comment:
                raise AuditError(f"ZIP entry comments are forbidden: {path}")
            if entry.date_time != fixed_timestamp:
                raise AuditError(f"ZIP timestamp is not deterministic: {path}")
            unix_type = stat.S_IFMT(entry.external_attr >> 16)
            if unix_type == stat.S_IFLNK:
                raise AuditError(f"symbolic links are forbidden: {path}")
            if entry.create_system == 3 and unix_type not in {0, stat.S_IFREG}:
                raise AuditError(f"non-regular ZIP entries are forbidden: {path}")
            windows_key = path.lower()
            if windows_key in windows_keys:
                raise AuditError(f"case-insensitive duplicate ZIP path: {path}")
            windows_keys.add(windows_key)
            if path not in expected_by_path:
                raise AuditError(f"unexpected ZIP path: {path}")
            if entry.file_size > policy["limits"]["max_file_bytes"]:
                raise AuditError(f"ZIP entry exceeds the per-file limit: {path}")
            digest = hashlib.sha256()
            actual_size = 0
            with archive.open(entry, "r") as stream:
                while chunk := stream.read(1024 * 1024):
                    actual_size += len(chunk)
                    if actual_size > entry.file_size:
                        raise AuditError(f"ZIP entry expanded beyond its declared size: {path}")
                    digest.update(chunk)
            if actual_size != entry.file_size:
                raise AuditError(f"ZIP entry size changed while reading: {path}")
            expanded += actual_size
            if expanded > policy["limits"]["max_expanded_bytes"]:
                raise AuditError("ZIP exceeds the expanded-byte limit")
            actual.append({"path": path, "size": actual_size, "sha256": digest.hexdigest()})

    actual.sort(key=lambda item: item["path"])
    if actual != expected_files:
        raise AuditError("ZIP file inventory differs from audited release-root inventory")
    return {
        "schemaVersion": 1,
        "productId": policy["product_id"],
        "artifact": {
            "fileName": artifact.name,
            "format": artifact_policy["format"],
            "size": artifact_size,
            "sha256": artifact_hash,
        },
        "fileCount": len(actual),
        "expandedBytes": expanded,
        "files": actual,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifact", required=True, type=Path)
    parser.add_argument("--policy", required=True, type=Path)
    parser.add_argument("--inventory", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = verify_archive(args.artifact, args.policy, args.inventory)
        print(canonical_json_bytes(result).decode("utf-8"), end="")
        return 0
    except (AuditError, OSError, zipfile.BadZipFile) as exc:
        print(f"archive verification failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
