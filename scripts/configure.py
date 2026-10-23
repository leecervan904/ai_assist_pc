#!/usr/bin/env python3
"""Cross-platform CMake configuration script for SonoWorkstation."""

from __future__ import annotations

import argparse
import os
import platform
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
    parser = argparse.ArgumentParser(description="Configure SonoWorkstation CMake build")
    parser.add_argument(
        "--type", choices=["Release", "Debug"], default="Release", help="CMake build type"
    )
    parser.add_argument("--clean", action="store_true", help="Clean stale cache before configuring")
    parser.add_argument("--tests", action="store_true", help="Build the C++ test targets")
    args = parser.parse_args()

    generator = "Ninja"
    extra_args: list[str] = []

    if platform.system() == "Windows":
        has_cl = shutil.which("cl.exe") is not None
        if not has_cl:
            # When cl.exe is not in PATH (e.g. running in standard PowerShell rather than Native Tools Prompt),
            # use Visual Studio 17 2022 generator to automatically find MSVC and avoid picking up incompatible clang++
            generator = "Visual Studio 17 2022"
            extra_args = ["-A", "x64"]
            print(
                "ℹ️  MSVC cl.exe not in PATH; using 'Visual Studio 17 2022' generator to auto-locate MSVC."
            )
        else:
            print("✅ MSVC cl.exe found in PATH; using Ninja generator.")

    cache_file = BUILD_DIR / "CMakeCache.txt"
    if cache_file.is_file():
        try:
            content = cache_file.read_text(encoding="utf-8", errors="ignore")
            expected_gen = f"CMAKE_GENERATOR:INTERNAL={generator}"
            if expected_gen not in content or args.clean:
                print(
                    f"🔄 Cleaning stale CMake cache (switching generator to {generator} or clean requested)..."
                )
                shutil.rmtree(BUILD_DIR / "CMakeFiles", ignore_errors=True)
                cache_file.unlink(missing_ok=True)
        except OSError as e:
            print(f"⚠️ Warning cleaning cache: {e}", file=sys.stderr)

    cmd = [
        "cmake",
        "-S",
        str(ROOT_DIR),
        "-B",
        str(BUILD_DIR),
        "-G",
        generator,
        *extra_args,
        f"-DCMAKE_BUILD_TYPE={args.type}",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    ]
    if args.tests:
        cmd.append("-DSONO_BUILD_TESTS=ON")
    print(f"🛠️  Configuring CMake ({args.type}) with {generator} -> {BUILD_DIR.name}")
    return subprocess.call(cmd)


if __name__ == "__main__":
    sys.exit(main())
