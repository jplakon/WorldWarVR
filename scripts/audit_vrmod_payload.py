#!/usr/bin/env python3
"""Strictly audit an uncompressed WorldAtWarVR launcher release root."""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import re
import stat
import sys
from pathlib import Path
from typing import Any, Iterable


SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
WINDOWS_RESERVED = {
    "CON", "PRN", "AUX", "NUL",
    *(f"COM{index}" for index in range(1, 10)),
    *(f"LPT{index}" for index in range(1, 10)),
}
INVALID_WINDOWS_CHARS = set('<>:"\\|?*')
POLICY_KEYS = {
    "schema_version",
    "product_id",
    "component_inventory_path",
    "artifact",
    "limits",
    "path_rules",
    "required_files",
    "forbidden",
}
COMPONENT_KEYS = {
    "component_id",
    "name",
    "version",
    "revision",
    "origin_url",
    "role",
    "packaged_paths",
    "packaged_sha256",
    "license_spdx",
    "license_file",
    "copyright",
    "official_release_bytes",
    "corresponding_source_url",
    "corresponding_source_sha256",
    "redistribution_status",
    "notes",
}


class AuditError(RuntimeError):
    pass


REPARSE_POINT_ATTRIBUTE = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)


def _lexical_absolute(path: Path) -> Path:
    """Return an absolute path without resolving links or junctions."""
    return Path(os.path.abspath(os.fspath(path)))


def _stat_is_reparse_point(info: os.stat_result) -> bool:
    return stat.S_ISLNK(info.st_mode) or bool(
        getattr(info, "st_file_attributes", 0) & REPARSE_POINT_ATTRIBUTE
    )


def reject_reparse_ancestors(path: Path, description: str) -> Path:
    """Reject existing reparse points before any call to Path.resolve()."""
    lexical = _lexical_absolute(path)
    for candidate in [*reversed(lexical.parents), lexical]:
        try:
            info = os.lstat(candidate)
        except FileNotFoundError:
            continue
        if _stat_is_reparse_point(info):
            raise AuditError(f"{description} must not traverse a link or junction: {candidate}")
    return lexical


def normal_existing_file(path: Path, description: str) -> Path:
    lexical = reject_reparse_ancestors(path, description)
    try:
        info = os.lstat(lexical)
    except FileNotFoundError as exc:
        raise AuditError(f"{description} does not exist: {lexical}") from exc
    if _stat_is_reparse_point(info) or not stat.S_ISREG(info.st_mode):
        raise AuditError(f"{description} must be a normal file: {lexical}")
    resolved = lexical.resolve(strict=True)
    reject_reparse_ancestors(resolved, description)
    return resolved


def normal_existing_directory(path: Path, description: str) -> Path:
    lexical = reject_reparse_ancestors(path, description)
    try:
        info = os.lstat(lexical)
    except FileNotFoundError as exc:
        raise AuditError(f"{description} does not exist: {lexical}") from exc
    if _stat_is_reparse_point(info) or not stat.S_ISDIR(info.st_mode):
        raise AuditError(f"{description} must be a normal directory: {lexical}")
    resolved = lexical.resolve(strict=True)
    reject_reparse_ancestors(resolved, description)
    return resolved


def prepare_output_file(path: Path, description: str, *, must_not_exist: bool = False) -> Path:
    lexical = reject_reparse_ancestors(path, description)
    try:
        info = os.lstat(lexical)
    except FileNotFoundError:
        info = None
    if info is not None:
        if must_not_exist:
            raise AuditError(f"{description} already exists; use a new output path: {lexical}")
        if _stat_is_reparse_point(info) or not stat.S_ISREG(info.st_mode):
            raise AuditError(f"{description} must be a normal file: {lexical}")
    lexical.parent.mkdir(parents=True, exist_ok=True)
    reject_reparse_ancestors(lexical.parent, f"{description} parent")
    reject_reparse_ancestors(lexical, description)
    return lexical


def _strict_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise AuditError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_json_constant(value: str) -> None:
    raise AuditError(f"non-finite JSON number is forbidden: {value}")


def load_json(path: Path) -> Any:
    path = normal_existing_file(path, "JSON input")
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        raise AuditError(f"JSON must not contain a UTF-8 BOM: {path}")
    try:
        return json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_strict_object,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise AuditError(f"invalid UTF-8 JSON in {path}: {exc}") from exc


def canonical_json_bytes(value: Any) -> bytes:
    return (
        json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


def write_canonical_json(path: Path, value: Any) -> None:
    path = prepare_output_file(path, "JSON output")
    path.write_bytes(canonical_json_bytes(value))


def require_exact_keys(value: Any, expected: set[str], description: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise AuditError(f"{description} must be an object")
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        raise AuditError(f"{description} keys differ; missing={missing}, extra={extra}")
    return value


def require_int(value: Any, description: str, minimum: int = 0) -> int:
    if type(value) is not int or value < minimum:
        raise AuditError(f"{description} must be an integer >= {minimum}")
    return value


def require_string(value: Any, description: str) -> str:
    if not isinstance(value, str) or not value:
        raise AuditError(f"{description} must be a nonempty string")
    return value


def require_bool(value: Any, description: str) -> bool:
    if type(value) is not bool:
        raise AuditError(f"{description} must be a boolean")
    return value


def require_list(value: Any, description: str, *, nonempty: bool = False) -> list[Any]:
    if not isinstance(value, list) or (nonempty and not value):
        qualifier = "nonempty " if nonempty else ""
        raise AuditError(f"{description} must be a {qualifier}array")
    return value


def require_nullable_string(value: Any, description: str) -> str | None:
    if value is None:
        return None
    return require_string(value, description)


def validate_archive_path(path: str, rules: dict[str, Any]) -> str:
    require_string(path, "archive path")
    if path.startswith("/") or path.startswith("\\") or "\\" in path:
        raise AuditError(f"archive path must be relative and use '/': {path}")
    if re.match(r"^[A-Za-z]:", path):
        raise AuditError(f"drive-prefixed archive path is forbidden: {path}")
    if rules.get("printable_ascii_only") is not True or rules.get("case_insensitive_unique") is not True:
        raise AuditError("policy must require printable ASCII and case-insensitive uniqueness")
    if any(ord(character) < 0x20 or ord(character) > 0x7E for character in path):
        raise AuditError(f"archive path is not printable ASCII: {path!r}")
    max_path = require_int(rules.get("max_path_chars"), "max_path_chars", 1)
    max_segment = require_int(rules.get("max_segment_chars"), "max_segment_chars", 1)
    if len(path) > max_path:
        raise AuditError(f"archive path exceeds {max_path} characters: {path}")
    segments = path.split("/")
    if not segments or any(segment in {"", ".", ".."} for segment in segments):
        raise AuditError(f"archive path contains an unsafe segment: {path}")
    for segment in segments:
        if len(segment) > max_segment:
            raise AuditError(f"archive segment exceeds {max_segment} characters: {segment}")
        if segment.endswith((".", " ")) or any(char in INVALID_WINDOWS_CHARS for char in segment):
            raise AuditError(f"archive path is not Windows-portable: {path}")
        device_stem = segment.split(".", 1)[0].upper()
        if device_stem in WINDOWS_RESERVED:
            raise AuditError(f"reserved Windows device name in archive path: {path}")
    return path


def sha256_file(path: Path, maximum: int) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            size += len(chunk)
            if size > maximum:
                raise AuditError(f"file exceeds {maximum} bytes: {path}")
            digest.update(chunk)
    return size, digest.hexdigest()


def _is_link_or_junction(path: Path) -> bool:
    try:
        return _stat_is_reparse_point(os.lstat(path))
    except FileNotFoundError:
        return False


def iter_regular_files(
    root: Path,
    expected_files: set[str],
    allowed_directories: set[str],
    rules: dict[str, Any],
    max_files: int,
) -> Iterable[Path]:
    max_directories = len(allowed_directories)
    max_entries = max_files + max_directories
    file_count = 0
    directory_count = 0
    entry_count = 0
    seen_directory_keys: set[str] = set()
    stack = [root]
    while stack:
        directory = stack.pop()
        directory_info = os.lstat(directory)
        if _stat_is_reparse_point(directory_info) or not stat.S_ISDIR(directory_info.st_mode):
            raise AuditError(f"audited directory changed or became a reparse point: {directory}")
        with os.scandir(directory) as entries:
            for entry in entries:
                entry_count += 1
                if entry_count > max_entries:
                    raise AuditError(f"release root contains more than {max_entries} filesystem entries")
                path = Path(entry.path)
                info = entry.stat(follow_symlinks=False)
                if _stat_is_reparse_point(info):
                    raise AuditError(f"links and junctions are forbidden: {path}")
                relative = path.relative_to(root).as_posix()
                validate_archive_path(relative, rules)
                if stat.S_ISDIR(info.st_mode):
                    directory_count += 1
                    if directory_count > max_directories:
                        raise AuditError(
                            f"release root contains more than {max_directories} directories"
                        )
                    directory_key = relative.lower()
                    if directory_key in seen_directory_keys:
                        raise AuditError(f"case-insensitive duplicate release directory: {relative}")
                    seen_directory_keys.add(directory_key)
                    if relative not in allowed_directories:
                        raise AuditError(f"unexpected directory entered release root: {relative}")
                    stack.append(path)
                elif stat.S_ISREG(info.st_mode):
                    file_count += 1
                    if file_count > max_files:
                        raise AuditError(f"release root contains more than {max_files} files")
                    if relative not in expected_files:
                        raise AuditError(f"unexpected file entered release root: {relative}")
                    yield path
                else:
                    raise AuditError(f"non-regular filesystem entry is forbidden: {path}")


def _require_string_array(
    value: Any,
    description: str,
    *,
    nonempty: bool = False,
    case_insensitive_unique: bool = True,
) -> list[str]:
    raw_items = require_list(value, description, nonempty=nonempty)
    items: list[str] = []
    keys: set[str] = set()
    for index, raw_item in enumerate(raw_items):
        item = require_string(raw_item, f"{description}[{index}]")
        key = item.lower() if case_insensitive_unique else item
        if key in keys:
            raise AuditError(f"duplicate value in {description}: {item}")
        keys.add(key)
        items.append(item)
    return items


def validate_policy(policy: Any) -> dict[str, Any]:
    policy = require_exact_keys(policy, POLICY_KEYS, "payload policy")
    if require_int(policy["schema_version"], "schema_version", 1) != 1:
        raise AuditError("unsupported payload policy schema_version")
    require_string(policy["product_id"], "product_id")
    artifact = require_exact_keys(
        policy["artifact"],
        {
            "file_name_pattern", "format", "allowed_compression_methods",
            "fixed_zip_timestamp", "require_empty_archive_comment",
            "require_empty_entry_extra", "require_empty_entry_comment",
        },
        "artifact policy",
    )
    limits = require_exact_keys(
        policy["limits"],
        {"max_artifact_bytes", "max_expanded_bytes", "max_file_bytes", "max_files"},
        "limits",
    )
    path_rules = require_exact_keys(
        policy["path_rules"],
        {"max_path_chars", "max_segment_chars", "printable_ascii_only", "case_insensitive_unique"},
        "path_rules",
    )
    forbidden = require_exact_keys(
        policy["forbidden"],
        {"names", "extensions", "prefixes", "allowed_executable_paths", "allowed_dll_paths"},
        "forbidden policy",
    )

    pattern_text = require_string(artifact["file_name_pattern"], "artifact.file_name_pattern")
    if len(pattern_text) > 256 or not pattern_text.startswith("^") or not pattern_text.endswith("$"):
        raise AuditError("artifact.file_name_pattern must be anchored and at most 256 characters")
    try:
        re.compile(pattern_text)
    except re.error as exc:
        raise AuditError(f"artifact.file_name_pattern is invalid: {exc}") from exc
    if artifact["format"] != "vrmod-zip-v1":
        raise AuditError("artifact.format must be vrmod-zip-v1")
    compression_methods = require_list(
        artifact["allowed_compression_methods"],
        "artifact.allowed_compression_methods",
        nonempty=True,
    )
    normalized_methods: list[int] = []
    for index, method in enumerate(compression_methods):
        normalized_methods.append(
            require_int(method, f"artifact.allowed_compression_methods[{index}]")
        )
    if len(set(normalized_methods)) != len(normalized_methods):
        raise AuditError("artifact.allowed_compression_methods contains duplicates")
    if normalized_methods != [0]:
        raise AuditError("artifact.allowed_compression_methods must be [0] for deterministic storage")
    timestamp = require_list(
        artifact["fixed_zip_timestamp"], "artifact.fixed_zip_timestamp"
    )
    if len(timestamp) != 6:
        raise AuditError("artifact.fixed_zip_timestamp must contain six integers")
    timestamp_values = [
        require_int(value, f"artifact.fixed_zip_timestamp[{index}]")
        for index, value in enumerate(timestamp)
    ]
    try:
        parsed_timestamp = datetime.datetime(*timestamp_values)
    except (TypeError, ValueError) as exc:
        raise AuditError(f"artifact.fixed_zip_timestamp is invalid: {exc}") from exc
    if not 1980 <= parsed_timestamp.year <= 2107 or parsed_timestamp.second % 2:
        raise AuditError("artifact.fixed_zip_timestamp must be ZIP-representable with an even second")
    require_bool(artifact["require_empty_archive_comment"], "artifact.require_empty_archive_comment")
    require_bool(artifact["require_empty_entry_extra"], "artifact.require_empty_entry_extra")
    require_bool(artifact["require_empty_entry_comment"], "artifact.require_empty_entry_comment")

    max_artifact = require_int(limits["max_artifact_bytes"], "max_artifact_bytes", 1)
    max_expanded = require_int(limits["max_expanded_bytes"], "max_expanded_bytes", 1)
    max_file = require_int(limits["max_file_bytes"], "max_file_bytes", 1)
    max_files = require_int(limits["max_files"], "max_files", 1)
    if max_file > max_expanded:
        raise AuditError("max_file_bytes must not exceed max_expanded_bytes")
    if max_artifact < 1:
        raise AuditError("max_artifact_bytes must be positive")

    require_int(path_rules["max_path_chars"], "max_path_chars", 1)
    require_int(path_rules["max_segment_chars"], "max_segment_chars", 1)
    if require_bool(path_rules["printable_ascii_only"], "printable_ascii_only") is not True:
        raise AuditError("path_rules.printable_ascii_only must be true")
    if require_bool(path_rules["case_insensitive_unique"], "case_insensitive_unique") is not True:
        raise AuditError("path_rules.case_insensitive_unique must be true")

    component_inventory_path = validate_archive_path(
        policy["component_inventory_path"], path_rules
    )
    required_files = require_list(policy["required_files"], "required_files", nonempty=True)
    if len(required_files) > max_files:
        raise AuditError("required_files exceeds limits.max_files")
    required_paths: list[str] = []
    required_path_keys: set[str] = set()
    required_total = 0
    for index, raw_required in enumerate(required_files):
        required = require_exact_keys(
            raw_required, {"path", "size", "sha256"}, f"required_files[{index}]"
        )
        path = validate_archive_path(required["path"], path_rules)
        key = path.lower()
        if key in required_path_keys:
            raise AuditError(f"case-insensitive duplicate required file path: {path}")
        required_path_keys.add(key)
        required_paths.append(path)
        size = require_int(required["size"], f"required_files[{index}].size")
        if size > max_file:
            raise AuditError(f"required file exceeds max_file_bytes: {path}")
        required_total += size
        digest = require_string(required["sha256"], f"required_files[{index}].sha256")
        if not SHA256_RE.fullmatch(digest):
            raise AuditError(f"required file SHA-256 is not canonical lowercase hex: {path}")
    if required_total > max_expanded:
        raise AuditError("required_files exceeds limits.max_expanded_bytes")
    if component_inventory_path not in required_paths:
        raise AuditError("component_inventory_path must be present in required_files")

    names = _require_string_array(forbidden["names"], "forbidden.names")
    for name in names:
        if "/" in name or "\\" in name:
            raise AuditError(f"forbidden.names entries must be base names: {name}")
    extensions = _require_string_array(forbidden["extensions"], "forbidden.extensions")
    for extension in extensions:
        if not extension.startswith(".") or "/" in extension or "\\" in extension:
            raise AuditError(f"invalid forbidden extension: {extension}")
    prefixes = _require_string_array(forbidden["prefixes"], "forbidden.prefixes")
    for prefix in prefixes:
        if not prefix.endswith("/"):
            raise AuditError(f"forbidden prefix must end with '/': {prefix}")
        validate_archive_path(prefix[:-1], path_rules)
    allowed_executables = _require_string_array(
        forbidden["allowed_executable_paths"], "forbidden.allowed_executable_paths"
    )
    allowed_dlls = _require_string_array(
        forbidden["allowed_dll_paths"], "forbidden.allowed_dll_paths"
    )
    for path in allowed_executables:
        validate_archive_path(path, path_rules)
        if not path.lower().endswith(".exe"):
            raise AuditError(f"allowed executable path must end in .exe: {path}")
    for path in allowed_dlls:
        validate_archive_path(path, path_rules)
        if not path.lower().endswith(".dll"):
            raise AuditError(f"allowed DLL path must end in .dll: {path}")
    required_executables = {path for path in required_paths if path.lower().endswith(".exe")}
    required_dlls = {path for path in required_paths if path.lower().endswith(".dll")}
    if required_executables != set(allowed_executables):
        raise AuditError("allowed_executable_paths must exactly match required executable files")
    if required_dlls != set(allowed_dlls):
        raise AuditError("allowed_dll_paths must exactly match required DLL files")
    return policy


def _validate_forbidden(path: str, forbidden: dict[str, Any]) -> None:
    name = path.rsplit("/", 1)[-1]
    lower_path = path.lower()
    if name.lower() in {str(item).lower() for item in forbidden["names"]}:
        raise AuditError(f"forbidden file name entered release root: {path}")
    if any(lower_path.endswith(str(ext).lower()) for ext in forbidden["extensions"]):
        raise AuditError(f"forbidden extension entered release root: {path}")
    if any(lower_path.startswith(str(prefix).lower()) for prefix in forbidden["prefixes"]):
        raise AuditError(f"forbidden path prefix entered release root: {path}")
    if lower_path.endswith(".exe") and path not in forbidden["allowed_executable_paths"]:
        raise AuditError(f"unexpected executable entered release root: {path}")
    if lower_path.endswith(".dll") and path not in forbidden["allowed_dll_paths"]:
        raise AuditError(f"unexpected DLL entered release root: {path}")


def _validate_components(root: Path, policy: dict[str, Any], hashes: dict[str, str]) -> None:
    component_path = validate_archive_path(policy["component_inventory_path"], policy["path_rules"])
    inventory = require_exact_keys(
        load_json(root / Path(component_path)),
        {"schema_version", "product_id", "release_version", "publication_status", "components"},
        "component inventory",
    )
    if require_int(inventory["schema_version"], "component inventory schema_version", 1) != 1:
        raise AuditError("unsupported component inventory schema_version")
    if require_string(inventory["product_id"], "component inventory product_id") != policy["product_id"]:
        raise AuditError("component inventory identity differs from payload policy")
    require_string(inventory["release_version"], "component inventory release_version")
    if require_string(
        inventory["publication_status"], "component inventory publication_status"
    ) != "blocked":
        raise AuditError("this pre-publication component inventory must remain blocked")
    components = require_list(
        inventory["components"], "component inventory components", nonempty=True
    )
    seen_components: set[str] = set()
    covered_payload_binaries: set[str] = set()
    for index, raw_component in enumerate(components):
        component = require_exact_keys(raw_component, COMPONENT_KEYS, f"component[{index}]")
        component_id = require_string(component["component_id"], f"component[{index}].component_id")
        component_key = component_id.lower()
        if component_key in seen_components:
            raise AuditError(f"duplicate component_id: {component_id}")
        seen_components.add(component_key)
        require_string(component["name"], f"component[{index}].name")
        require_nullable_string(component["version"], f"component[{index}].version")
        require_nullable_string(component["revision"], f"component[{index}].revision")
        require_nullable_string(component["origin_url"], f"component[{index}].origin_url")
        require_string(component["role"], f"component[{index}].role")
        require_string(component["license_spdx"], f"component[{index}].license_spdx")
        license_file = require_nullable_string(
            component["license_file"], f"component[{index}].license_file"
        )
        require_string(component["copyright"], f"component[{index}].copyright")
        require_bool(
            component["official_release_bytes"], f"component[{index}].official_release_bytes"
        )
        require_nullable_string(
            component["corresponding_source_url"],
            f"component[{index}].corresponding_source_url",
        )
        source_digest = require_nullable_string(
            component["corresponding_source_sha256"],
            f"component[{index}].corresponding_source_sha256",
        )
        if source_digest is not None and not SHA256_RE.fullmatch(source_digest):
            raise AuditError(
                f"component corresponding-source SHA-256 is not canonical lowercase hex: {component_id}"
            )
        require_string(
            component["redistribution_status"], f"component[{index}].redistribution_status"
        )
        require_string(component["notes"], f"component[{index}].notes")

        raw_paths = require_list(component["packaged_paths"], f"component[{index}].packaged_paths")
        raw_path_hashes = component["packaged_sha256"]
        if not isinstance(raw_path_hashes, dict):
            raise AuditError(f"component[{index}].packaged_sha256 must be an object")
        paths: list[str] = []
        path_keys: set[str] = set()
        for path_index, raw_path in enumerate(raw_paths):
            packaged_path = require_string(
                raw_path, f"component[{index}].packaged_paths[{path_index}]"
            )
            validate_archive_path(packaged_path, policy["path_rules"])
            path_key = packaged_path.lower()
            if path_key in path_keys:
                raise AuditError(f"duplicate packaged path for component {component_id}: {packaged_path}")
            path_keys.add(path_key)
            paths.append(packaged_path)

        path_hashes: dict[str, str] = {}
        path_hash_keys: set[str] = set()
        for raw_path, raw_digest in raw_path_hashes.items():
            packaged_path = require_string(raw_path, f"component[{index}].packaged_sha256 key")
            validate_archive_path(packaged_path, policy["path_rules"])
            path_key = packaged_path.lower()
            if path_key in path_hash_keys:
                raise AuditError(
                    f"case-insensitive duplicate component hash path: {component_id}/{packaged_path}"
                )
            path_hash_keys.add(path_key)
            digest = require_string(
                raw_digest, f"component[{index}].packaged_sha256[{packaged_path}]"
            )
            if not SHA256_RE.fullmatch(digest):
                raise AuditError(
                    f"component packaged SHA-256 is not canonical lowercase hex: "
                    f"{component_id}/{packaged_path}"
                )
            path_hashes[packaged_path] = digest

        if set(paths) != set(path_hashes):
            raise AuditError(f"component packaged_paths and packaged_sha256 differ: {component_id}")
        for packaged_path in paths:
            expected_hash = path_hashes[packaged_path]
            if hashes.get(packaged_path) != expected_hash:
                raise AuditError(f"component hash does not match release file: {component_id}/{packaged_path}")
            if packaged_path.lower().endswith((".exe", ".dll")):
                covered_payload_binaries.add(packaged_path)
        if license_file is not None:
            validate_archive_path(license_file, policy["path_rules"])
            if license_file not in hashes:
                raise AuditError(f"component license_file is not packaged: {component_id}/{license_file}")

    payload_binaries = {
        path
        for path in hashes
        if path.lower().endswith((".exe", ".dll"))
    }
    uncovered = sorted(payload_binaries - covered_payload_binaries)
    if uncovered:
        raise AuditError(f"component inventory does not cover packaged payload binaries: {uncovered}")


def audit_release_root(root: Path, policy_path: Path) -> dict[str, Any]:
    root = normal_existing_directory(root, "release root")
    policy = validate_policy(load_json(policy_path))
    limits = policy["limits"]
    max_files = require_int(limits["max_files"], "max_files", 1)
    max_file_bytes = require_int(limits["max_file_bytes"], "max_file_bytes", 1)
    max_expanded = require_int(limits["max_expanded_bytes"], "max_expanded_bytes", 1)

    expected: dict[str, dict[str, Any]] = {}
    for index, required in enumerate(policy["required_files"]):
        required = require_exact_keys(required, {"path", "size", "sha256"}, f"required_files[{index}]")
        path = validate_archive_path(required["path"], policy["path_rules"])
        if path in expected or path.lower() in {item.lower() for item in expected}:
            raise AuditError(f"duplicate required file path: {path}")
        size = require_int(required["size"], f"required file size for {path}")
        digest = require_string(required["sha256"], f"required file SHA-256 for {path}")
        if not SHA256_RE.fullmatch(digest):
            raise AuditError(f"required file SHA-256 is not canonical lowercase hex: {path}")
        expected[path] = {"size": size, "sha256": digest}

    allowed_directories: set[str] = set()
    for required_path in expected:
        segments = required_path.split("/")[:-1]
        for end in range(1, len(segments) + 1):
            allowed_directories.add("/".join(segments[:end]))
    files = list(
        iter_regular_files(
            root,
            set(expected),
            allowed_directories,
            policy["path_rules"],
            max_files,
        )
    )
    actual_paths: dict[str, Path] = {}
    windows_keys: set[str] = set()
    for file_path in files:
        relative = file_path.relative_to(root).as_posix()
        validate_archive_path(relative, policy["path_rules"])
        windows_key = relative.lower()
        if windows_key in windows_keys:
            raise AuditError(f"case-insensitive duplicate release path: {relative}")
        windows_keys.add(windows_key)
        _validate_forbidden(relative, policy["forbidden"])
        actual_paths[relative] = file_path
    if set(actual_paths) != set(expected):
        raise AuditError(
            f"release inventory differs; missing={sorted(set(expected) - set(actual_paths))}, "
            f"unexpected={sorted(set(actual_paths) - set(expected))}"
        )

    inventory_files: list[dict[str, Any]] = []
    expanded = 0
    hashes: dict[str, str] = {}
    for relative in sorted(actual_paths):
        file_path = normal_existing_file(
            actual_paths[relative], f"release file {relative}"
        )
        size, digest = sha256_file(file_path, max_file_bytes)
        required = expected[relative]
        if size != required["size"] or digest != required["sha256"]:
            raise AuditError(f"size or SHA-256 mismatch: {relative}")
        expanded += size
        if expanded > max_expanded:
            raise AuditError(f"release root exceeds {max_expanded} expanded bytes")
        hashes[relative] = digest
        inventory_files.append({"path": relative, "size": size, "sha256": digest})

    _validate_components(root, policy, hashes)
    return {
        "schemaVersion": 1,
        "productId": policy["product_id"],
        "fileCount": len(inventory_files),
        "expandedBytes": expanded,
        "files": inventory_files,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--policy", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        root = normal_existing_directory(args.root, "release root")
        output_candidate = reject_reparse_ancestors(args.output, "inventory output")
        if output_candidate == root or root in output_candidate.parents:
            raise AuditError("inventory output must remain outside the audited release root")
        output = prepare_output_file(output_candidate, "inventory output")
        result = audit_release_root(root, args.policy)
        write_canonical_json(output, result)
        print(canonical_json_bytes(result).decode("utf-8"), end="")
        return 0
    except (AuditError, OSError) as exc:
        print(f"payload audit failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
