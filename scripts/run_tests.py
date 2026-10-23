#!/usr/bin/env python3
"""Cross-platform test runner for SonoWorkstation (Python unittest + CTest)."""

from __future__ import annotations

import os
import shutil
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
    print("🧪 Running Python test suite...")
    res = subprocess.call(
        [
            sys.executable,
            "-m",
            "unittest",
            "discover",
            "-s",
            "tests/python",
            "-p",
            "test_*.py",
            "-v",
        ]
    )
    if res != 0:
        return res

    if BUILD_DIR.is_dir() and shutil.which("ctest"):
        print("\n🧪 Running CTest suite...")
        return subprocess.call(["ctest", "--test-dir", str(BUILD_DIR), "--output-on-failure"])

    return 0


if __name__ == "__main__":
    sys.exit(main())
