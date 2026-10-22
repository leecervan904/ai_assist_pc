#!/usr/bin/env python3
"""Diagnostic script to verify development environment and required assets."""

from __future__ import annotations

import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT_DIR / "scripts"))
import sync_deps  # noqa: E402


def check_binary(name: str, version_arg: str = "--version") -> tuple[bool, str]:
    path = shutil.which(name)
    if not path:
        return False, "Not found in PATH"
    try:
        env = dict(os.environ, MISE_YES="1")
        out = subprocess.check_output(
            [path, version_arg],
            stderr=subprocess.STDOUT,
            text=True,
            timeout=5,
            stdin=subprocess.DEVNULL,
            env=env,
        )
        first_line = out.strip().splitlines()[0] if out.strip() else "Found"
        return True, f"{first_line} ({path})"
    except Exception:
        return True, path


def main() -> int:
    print("============================================================")
    print("🔍 SonoWorkstation Environment & Asset Diagnostic")
    print("============================================================")
    print(f"OS / Arch : {platform.system()} {platform.release()} ({platform.machine()})")
    print(f"Python    : {sys.version.split()[0]} ({sys.executable})")
    print("------------------------------------------------------------")

    tools = [
        ("cmake", "--version"),
        ("ninja", "--version"),
        ("git", "--version"),
        ("git-lfs", "--version"),
        ("mise", "--version"),
        ("uv", "--version"),
        ("ruff", "--version"),
        ("gh", "--version"),
    ]
    for name, arg in tools:
        ok, info = check_binary(name, arg)
        mark = "✅" if ok else "⚪"
        print(f"{mark} {name:<10}: {info}")

    manifest_path = ROOT_DIR / "deps" / "manifest.json"
    if manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        target_platform = sync_deps.detect_host_target()
        variables = sync_deps.resolve_variables(manifest, {})
        qt_prefix = sync_deps.find_local_qt6(manifest, variables, target_platform)
        qt_mark = "✅" if qt_prefix else "❌"
        print(f"{qt_mark} {'qt6':<10}: {qt_prefix or 'Not found (run `mise run deps:sync` to auto-install via aqtinstall)'}")

    print("------------------------------------------------------------")
    required_assets = [
        ROOT_DIR / "assets" / "models" / "thyroid.onnx",
        ROOT_DIR / "assets" / "models" / "thyroid.ini",
        ROOT_DIR / "assets" / "models" / "XT-7-640.onnx",
        ROOT_DIR / "assets" / "models" / "XT-7-640.ini",
        ROOT_DIR / "assets" / "media" / "test-1080p120.mp4",
        ROOT_DIR / "assets" / "media" / "XT.mp4",
        ROOT_DIR / "workstation.html",
    ]

    all_assets_ok = True
    for asset in required_assets:
        exists = asset.is_file()
        mark = "✅" if exists else "❌"
        if not exists:
            all_assets_ok = False
        rel = asset.relative_to(ROOT_DIR)
        size_mb = asset.stat().st_size / (1024 * 1024) if exists else 0.0
        print(f"{mark} Asset      : {rel} ({size_mb:.2f} MB)")

    print("============================================================")
    return 0 if all_assets_ok else 1


if __name__ == "__main__":
    sys.exit(main())
