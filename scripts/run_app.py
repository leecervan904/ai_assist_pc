#!/usr/bin/env python3
"""Cross-platform application launcher for SonoWorkstation."""

from __future__ import annotations

import os
import platform
import subprocess
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT_DIR = Path(__file__).resolve().parent.parent
BUILD_DIR = Path(os.environ.get("SONO_BUILD_DIR", ROOT_DIR / "build"))


def main() -> int:
    sys_name = platform.system()
    if sys_name == "Darwin":
        app_path = BUILD_DIR / "SonoWorkstation.app"
        if not app_path.exists():
            print(f"❌ Application bundle not found: {app_path}", file=sys.stderr)
            return 1
        return subprocess.call(["open", str(app_path)])
    elif sys_name == "Linux":
        exe_path = BUILD_DIR / "SonoWorkstation"
        if not exe_path.is_file():
            print(f"❌ Binary not found: {exe_path}", file=sys.stderr)
            return 1
        return subprocess.call([str(exe_path)])
    elif sys_name == "Windows":
        candidates = [
            BUILD_DIR / "SonoWorkstation.exe",
            BUILD_DIR / "Release" / "SonoWorkstation.exe",
            BUILD_DIR / "Debug" / "SonoWorkstation.exe",
        ]
        exe_path = next((p for p in candidates if p.is_file()), None)
        if not exe_path:
            print(f"❌ Binary not found in any of: {[str(p) for p in candidates]}", file=sys.stderr)
            return 1
        return subprocess.call([str(exe_path)])
    else:
        print(f"❌ Unsupported operating system: {sys_name}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
