"""Content-addressed provenance with exact nested source and result coverage."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


EXCLUDED_ANYWHERE = {
    ".git",
    ".venv",
    "__pycache__",
    ".pytest_cache",
}
EXCLUDED_ROOTS = {"artifacts", "results"}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def discover_sources(root: Path | str) -> dict[str, str]:
    base = Path(root).resolve()
    files: dict[str, str] = {}
    for path in sorted(base.rglob("*")):
        relative = path.relative_to(base)
        if not path.is_file() or any(part in EXCLUDED_ANYWHERE for part in relative.parts):
            continue
        if relative.parts[0] in EXCLUDED_ROOTS:
            continue
        if relative.parts[0] == "build" or relative.parts[0].startswith("build-"):
            continue
        if any(part.endswith(".egg-info") for part in relative.parts):
            continue
        if path.suffix in {".pyc", ".pyo"}:
            continue
        files[relative.as_posix()] = sha256_file(path)
    return files


def discover_results(root: Path | str) -> dict[str, str]:
    base = Path(root).resolve()
    return {
        path.relative_to(base).as_posix(): sha256_file(path)
        for path in sorted(base.rglob("*"))
        if path.is_file() and path.name != "provenance.json"
    }


def write_manifest(
    artifact_root: Path | str,
    source_root: Path | str,
    simulator_binary: Path | str,
) -> dict[str, object]:
    artifact = Path(artifact_root).resolve()
    binary = Path(simulator_binary).resolve()
    manifest: dict[str, object] = {
        "schema_version": "1.0",
        "hash_algorithm": "sha256",
        "generator": "wtsn-eval/0.1.0",
        "simulator_binary_sha256": sha256_file(binary),
        "source_files": discover_sources(source_root),
        "result_files": discover_results(artifact),
    }
    write_json(artifact / "provenance.json", manifest)
    return manifest


def verify_manifest(
    artifact_root: Path | str,
    source_root: Path | str,
    simulator_binary: Path | str | None = None,
) -> dict[str, int | bool]:
    artifact = Path(artifact_root).resolve()
    manifest_path = artifact / "provenance.json"
    with manifest_path.open(encoding="utf-8") as handle:
        manifest = json.load(handle)
    if manifest.get("schema_version") != "1.0" or manifest.get("hash_algorithm") != "sha256":
        raise ValueError("unsupported provenance manifest")
    expected_binary_hash = manifest.get("simulator_binary_sha256")
    if (
        not isinstance(expected_binary_hash, str)
        or len(expected_binary_hash) != 64
        or any(character not in "0123456789abcdef" for character in expected_binary_hash)
    ):
        raise ValueError("manifest simulator binary hash is malformed")
    expected_sources = manifest.get("source_files")
    expected_results = manifest.get("result_files")
    if not isinstance(expected_sources, dict) or not isinstance(expected_results, dict):
        raise ValueError("manifest file maps are malformed")
    actual_sources = discover_sources(source_root)
    actual_results = discover_results(artifact)
    _assert_exact("source", expected_sources, actual_sources)
    _assert_exact("result", expected_results, actual_results)
    binary_verified = simulator_binary is not None
    if simulator_binary is not None:
        binary = Path(simulator_binary).resolve()
        if not binary.is_file():
            raise ValueError(f"simulator binary does not exist: {binary}")
        if sha256_file(binary) != expected_binary_hash:
            raise ValueError("simulator binary hash does not match provenance manifest")
    return {
        "source_files": len(actual_sources),
        "result_files": len(actual_results),
        "binary_verified": binary_verified,
    }


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(value, handle, indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False)
        handle.write("\n")


def _assert_exact(label: str, expected: dict[str, object], actual: dict[str, str]) -> None:
    expected_keys = set(expected)
    actual_keys = set(actual)
    if expected_keys != actual_keys:
        missing = sorted(expected_keys - actual_keys)
        extra = sorted(actual_keys - expected_keys)
        raise ValueError(f"{label} file set changed; missing={missing}, extra={extra}")
    changed = sorted(path for path in actual if expected[path] != actual[path])
    if changed:
        raise ValueError(f"{label} hashes changed: {changed}")
