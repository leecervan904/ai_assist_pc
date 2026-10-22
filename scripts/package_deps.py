#!/usr/bin/env python3
"""Package local SDKs and models into release-ready archives and print SHA-256 digests.

Usage:
  python3 scripts/package_deps.py --models-only
  python3 scripts/package_deps.py --all
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import tarfile
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
DIST_DIR = ROOT_DIR / "dist" / "releases"


def compute_sha256(file_path: Path) -> str:
    hasher = hashlib.sha256()
    with file_path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def pack_directory_xz(source_dir: Path, output_archive: Path) -> None:
    output_archive.parent.mkdir(parents=True, exist_ok=True)
    print(f"📦 Packing {source_dir.relative_to(ROOT_DIR)} -> {output_archive.relative_to(ROOT_DIR)} ...")
    with tarfile.open(output_archive, "w:xz") as tf:
        for item in source_dir.iterdir():
            tf.add(item, arcname=item.name)


def main() -> int:
    parser = argparse.ArgumentParser(description="Package local dependencies for GitHub Releases")
    parser.add_argument("--models-only", action="store_true", help="Only stage and hash ONNX models")
    parser.add_argument("--pack-mac-cef", action="store_true", help="Create cef-darwin-arm64.tar.xz from third_party/mac/cef")
    args = parser.parse_args()

    DIST_DIR.mkdir(parents=True, exist_ok=True)
    print(f"📁 Output directory: {DIST_DIR.relative_to(ROOT_DIR)}\n")

    models_dir = ROOT_DIR / "assets" / "models"
    if models_dir.is_dir():
        print("--- ONNX Models ---")
        for onnx_file in sorted(models_dir.glob("*.onnx")):
            dest = DIST_DIR / onnx_file.name
            shutil.copy2(onnx_file, dest)
            sha = compute_sha256(dest)
            size_mb = dest.stat().st_size / (1024 * 1024)
            print(f"  • {dest.name} ({size_mb:.1f} MB)")
            print(f"    SHA-256: {sha}")

    if args.pack_mac_cef and not args.models_only:
        mac_cef = ROOT_DIR / ".deps" / "cef"
        if mac_cef.is_dir():
            print("\n--- macOS CEF SDK ---")
            out_tar = DIST_DIR / "cef-darwin-arm64.tar.xz"
            pack_directory_xz(mac_cef, out_tar)
            sha = compute_sha256(out_tar)
            size_mb = out_tar.stat().st_size / (1024 * 1024)
            print(f"  • {out_tar.name} ({size_mb:.1f} MB)")
            print(f"    SHA-256: {sha}")

    print("\n💡 Tip: Upload to GitHub Release via GitHub CLI:")
    print("   gh release create models-v1.0.0 dist/releases/*.onnx --repo leecervan904/ai_assist_pc")
    return 0


if __name__ == "__main__":
    sys.exit(main())
