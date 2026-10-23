#!/usr/bin/env python3
"""Diagnostic script to verify development environment and required assets."""

from __future__ import annotations

import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT_DIR / "scripts"))
import sync_deps  # noqa: E402

MINIMUM_VERSIONS = {
    "cmake": (3, 28, 0),
    "ninja": (1, 11, 0),
}


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
        return False, f"Failed to execute {path}"


def parse_version(text: str) -> tuple[int, ...] | None:
    match = re.search(r"(\d+)\.(\d+)(?:\.(\d+))?", text)
    if not match:
        return None
    return tuple(int(part) for part in match.groups(default="0"))


def meets_minimum(text: str, minimum: tuple[int, ...]) -> bool:
    version = parse_version(text)
    return version is not None and version >= minimum


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
    toolchain_ok = True
    for name, arg in tools:
        ok, info = check_binary(name, arg)
        mark = "✅" if ok else "❌"
        minimum = MINIMUM_VERSIONS.get(name)
        if not ok:
            toolchain_ok = False
        if ok and minimum and not meets_minimum(info, minimum):
            toolchain_ok = False
            mark = "❌"
            info += f" | requires >= {'.'.join(map(str, minimum))}"
        print(f"{mark} {name:<10}: {info}")

    python_minimum = (3, 12, 0)
    python_ok = sys.version_info >= python_minimum
    if not python_ok:
        toolchain_ok = False
    print(
        f"{'✅' if python_ok else '❌'} {'python':<10}: "
        f"{platform.python_version()} | requires >= {'.'.join(map(str, python_minimum))}"
    )

    manifest_path = ROOT_DIR / "deps" / "manifest.json"
    if manifest_path.is_file():
        target_platform = sync_deps.detect_host_target()
        qt_prefix = sync_deps.find_local_qt6(target_platform)
        qt_mark = "✅" if qt_prefix else "❌"
        if not qt_prefix:
            toolchain_ok = False
        qt_info = qt_prefix or (
            "Not found (install Qt 6.8.2+ with Multimedia; configure Qt6_DIR or CMAKE_PREFIX_PATH)"
        )
        print(f"{qt_mark} {'qt6':<10}: {qt_info}")
    else:
        toolchain_ok = False
        print(f"❌ {'manifest':<10}: deps/manifest.json not found")

    print("------------------------------------------------------------")
    required_assets = [
        ROOT_DIR / "assets" / "models" / "thyroid.onnx",
        ROOT_DIR / "assets" / "models" / "thyroid.ini",
        ROOT_DIR / "assets" / "models" / "XT-7-640.onnx",
        ROOT_DIR / "assets" / "models" / "XT-7-640.ini",
        ROOT_DIR / "assets" / "media" / "test-1080p120.mp4",
        ROOT_DIR / "assets" / "media" / "XT.mp4",
        ROOT_DIR / "web" / "workstation.html",
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
    return 0 if toolchain_ok and all_assets_ok else 1


if __name__ == "__main__":
    sys.exit(main())
