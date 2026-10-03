# SPDX-License-Identifier: MIT
"""Verify the native component's provenance; not combined-app legal compliance."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_files(root, records):
    paths = set()
    for record in records:
        name = record["path"]
        path = root / name
        if (name in paths or Path(name).is_absolute() or ".." in Path(name).parts
                or path.is_symlink() or not path.is_file()
                or not path.resolve().is_relative_to(root.resolve())):
            raise ValueError("Invalid source path: " + name)
        paths.add(name)
        if path.stat().st_size != record["bytes"] or sha(path) != record["sha256"]:
            raise ValueError("Source bytes changed: " + name)
    return paths


def verify(root=ROOT):
    manifest = json.loads((root / "component-source-manifest.json").read_text(encoding="utf-8"))
    records = manifest["exact_application_inputs"] + [manifest["native_interface_export"]]
    allowed = verify_files(root, records)
    fixed = {"README.md", "LICENSE", "NOTICES.md", ".gitignore", ".gitattributes", ".gitmodules",
             "component-source-manifest.json", "verification.json",
             "scripts/build-public-core.ps1", "scripts/verify-public-source.py",
             "scripts/test-public-source.py"}
    modules = {item["path"]: item for item in manifest["upstream_gitlinks"]}
    for name, item in modules.items():
        module = root / name
        if (git(module, "rev-parse", "HEAD") != item["revision"]
                or git(module, "rev-parse", "HEAD^{tree}") != item["tree"]
                or git(module, "status", "--porcelain", "--untracked-files=all")
                or git(root, "config", "-f", ".gitmodules", "submodule." + name + ".url") != item["repository"]):
            raise ValueError("Unverified upstream source: " + name)
    paths = set(git(root, "ls-files", "--cached", "--others", "--exclude-standard").splitlines())
    unexpected = paths - allowed - fixed - set(modules)
    if unexpected:
        raise ValueError("Outside public source allowlist: " + ", ".join(sorted(unexpected)))
    pin = json.loads((root / "config/ps2-armsx2-candidate.json").read_text())
    if sha(root / "LICENSE") != pin["license_sha256"]:
        raise ValueError("Upstream GPL notice changed")
    return {"status": "PASS_NATIVE_SOURCE_PROVENANCE_NOT_LEGAL_VERDICT",
            "application_inputs": len(manifest["exact_application_inputs"]),
            "jni_declarations": 9, "upstream_gitlinks": len(modules)}


if __name__ == "__main__":
    print(json.dumps(verify()))
