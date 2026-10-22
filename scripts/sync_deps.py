#!/usr/bin/env python3
"""Synchronize external SDKs (Qt6, CEF, ONNX Runtime) and AI models.

Reads declarative specifications from `deps/manifest.json`, resolves template
variables (with environment variable overrides), detects local Qt6 or installs
it on-demand via `aqtinstall` into `~/.cache/sono_deps/qt6`, verifies SHA-256
checksums, and extracts platform-specific archives into `.deps/`.
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
DEFAULT_MANIFEST = ROOT_DIR / "deps" / "manifest.json"
CACHE_DIR = ROOT_DIR / ".cache" / "downloads"

ENV_VAR_MAPPING = {
    "QT_VERSION": "SONO_QT_VERSION",
    "QT_MIRROR_URL": "SONO_QT_MIRROR_URL",
    "CEF_BASE_URL": "SONO_CEF_BASE_URL",
    "ORT_BASE_URL": "SONO_ORT_BASE_URL",
    "MODEL_REPO_URL": "SONO_MODEL_REPO_URL",
    "MODEL_RELEASE_TAG": "SONO_MODEL_RELEASE_TAG",
}


def detect_host_target() -> str:
    """Detect current OS and CPU architecture in '<os>-<arch>' format."""
    sys_name = platform.system().lower()
    machine = platform.machine().lower()

    if sys_name == "darwin":
        os_key = "darwin"
    elif sys_name == "windows":
        os_key = "windows"
    elif sys_name == "linux":
        os_key = "linux"
    else:
        raise RuntimeError(f"Unsupported operating system: {sys_name}")

    if machine in ("arm64", "aarch64"):
        arch_key = "arm64"
    elif machine in ("x86_64", "amd64"):
        arch_key = "x64"
    else:
        raise RuntimeError(f"Unsupported CPU architecture: {machine}")

    return f"{os_key}-{arch_key}"


def resolve_variables(manifest: dict, cli_overrides: dict[str, str | None]) -> dict[str, str]:
    """Merge manifest default variables with environment and CLI overrides."""
    variables: dict[str, str] = dict(manifest.get("variables", {}))
    for var_key, env_name in ENV_VAR_MAPPING.items():
        env_val = os.environ.get(env_name)
        if env_val:
            variables[var_key] = env_val.rstrip("/")

    for var_key, cli_val in cli_overrides.items():
        if cli_val:
            variables[var_key] = cli_val.rstrip("/")

    return variables


def interpolate(template: str, variables: dict[str, str]) -> str:
    """Replace ${VAR_NAME} placeholders in a string."""

    def replacer(match: re.Match[str]) -> str:
        key = match.group(1)
        if key not in variables:
            raise KeyError(f"Undefined template variable '${{{key}}}' in manifest")
        return variables[key]

    return re.sub(r"\$\{([A-Za-z0-9_]+)\}", replacer, template)


def has_qt6_with_multimedia(prefix: Path) -> bool:
    """Check if a directory prefix contains both Qt6Config.cmake and Qt6MultimediaConfig.cmake."""
    if not prefix.exists():
        return False

    # If pointing directly to .../lib/cmake/Qt6
    if (prefix / "Qt6Config.cmake").is_file():
        cmake_root = prefix.parent
        return (cmake_root / "Qt6Multimedia" / "Qt6MultimediaConfig.cmake").is_file()

    cmake_subdirs = [
        "lib/cmake",
        "lib64/cmake",
        "lib/x86_64-linux-gnu/cmake",
        "lib/aarch64-linux-gnu/cmake",
    ]
    for rel in cmake_subdirs:
        cmake_root = prefix / rel
        if (
            (cmake_root / "Qt6" / "Qt6Config.cmake").is_file()
            and (cmake_root / "Qt6Multimedia" / "Qt6MultimediaConfig.cmake").is_file()
        ):
            return True
    return False


def find_local_qt6(manifest: dict, variables: dict[str, str], target_platform: str) -> Path | None:
    """Discover an existing Qt6 installation with Multimedia module on the machine."""
    qt_cfg = manifest.get("qt6", {})
    cache_base = Path(qt_cfg.get("cache_dir", "~/.cache/sono_deps/qt6")).expanduser()
    version = interpolate(qt_cfg.get("version", "6.8.2"), variables)
    target_spec = qt_cfg.get("targets", {}).get(target_platform, {})
    subdir = target_spec.get("subdir", "")

    candidates: list[Path] = []

    # 1. Explicit environment variables
    for env_key in ("QTDIR", "Qt6_DIR", "CMAKE_PREFIX_PATH"):
        val = os.environ.get(env_key)
        if val:
            for part in val.split(os.pathsep):
                if part.strip():
                    candidates.append(Path(part.strip()).expanduser())

    # 2. Shared aqtinstall user cache (~/.cache/sono_deps/qt6)
    if subdir:
        candidates.append(cache_base / version / subdir)
    if cache_base.is_dir():
        for match in sorted(glob.glob(str(cache_base / "6.*" / "*")), reverse=True):
            candidates.append(Path(match))

    # 3. Standard platform locations
    if target_platform.startswith("darwin"):
        candidates.extend(
            [
                Path("/opt/homebrew"),
                Path("/opt/homebrew/opt/qt"),
                Path("/usr/local"),
                Path("/usr/local/opt/qt"),
            ]
        )
        for match in sorted(glob.glob(str(Path.home() / "Qt" / "6.*" / "macos")), reverse=True):
            candidates.append(Path(match))
    elif target_platform.startswith("windows"):
        for match in sorted(glob.glob("C:/Qt/6.*/msvc2022_64"), reverse=True):
            candidates.append(Path(match))
    elif target_platform.startswith("linux"):
        candidates.extend([Path("/usr"), Path("/usr/local")])
        for match in sorted(glob.glob(str(Path.home() / "Qt" / "6.*" / "gcc_*")), reverse=True):
            candidates.append(Path(match))

    for cand in candidates:
        if has_qt6_with_multimedia(cand):
            return cand.resolve()

    return None


def sync_qt6(
    manifest: dict,
    variables: dict[str, str],
    target_platform: str,
    dry_run: bool = False,
    force_qt: bool = False,
) -> Path | None:
    """Ensure Qt6 (with Multimedia) is available locally, or install via aqtinstall."""
    qt_cfg = manifest.get("qt6")
    if not qt_cfg:
        return None

    if not force_qt:
        existing = find_local_qt6(manifest, variables, target_platform)
        if existing:
            print(f"✅ [qt6] ({target_platform}) Found local Qt6 at {existing}")
            return existing

    targets = qt_cfg.get("targets", {})
    if target_platform not in targets:
        print(f"⚠️  [qt6] No aqtinstall target mapping for {target_platform}")
        return None

    spec = targets[target_platform]
    version = interpolate(qt_cfg["version"], variables)
    mirror = interpolate(qt_cfg.get("mirror", "https://download.qt.io"), variables)
    cache_dir = Path(qt_cfg.get("cache_dir", "~/.cache/sono_deps/qt6")).expanduser()
    install_prefix = cache_dir / version / spec["subdir"]

    modules: list[str] = qt_cfg.get("modules", ["qtmultimedia"])
    archives: list[str] = qt_cfg.get("archives", ["qtbase", "qtmultimedia"])

    uvx_bin = shutil.which("uvx")
    aqt_bin = shutil.which("aqt")
    if uvx_bin:
        base_cmd = [uvx_bin, "--from", "aqtinstall", "aqt"]
    elif aqt_bin:
        base_cmd = [aqt_bin]
    else:
        base_cmd = [sys.executable, "-m", "aqt"]

    cmd = [
        *base_cmd,
        "install-qt",
        spec["host"],
        spec["target"],
        version,
        spec["arch"],
        "-O",
        str(cache_dir),
        "-b",
        mirror,
    ]
    if modules:
        cmd.extend(["-m", *modules])
    if archives:
        cmd.extend(["--archives", *archives])

    print(f"🔄 [qt6] ({target_platform}) Local Qt6 not found; preparing aqtinstall -> {install_prefix}")
    if dry_run:
        print(f"  [Dry-Run] Would execute: {' '.join(cmd)}")
        return install_prefix

    cache_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run(cmd, check=True)
    print(f"✅ [qt6] Installed Qt {version} to {install_prefix}")
    return install_prefix


def compute_sha256(file_path: Path) -> str:
    """Compute SHA-256 hex digest of a file."""
    hasher = hashlib.sha256()
    with file_path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def is_git_lfs_pointer(file_path: Path) -> bool:
    """Check whether a file is an unpulled Git LFS text pointer."""
    if not file_path.is_file():
        return False
    try:
        if file_path.stat().st_size > 1024:
            return False
        head = file_path.read_bytes()[:64]
        return head.startswith(b"version https://git-lfs.github.com/spec/")
    except OSError:
        return False


def download_file(url: str, dest_path: Path, expected_sha256: str | None = None) -> None:
    """Download a file from URL with optional GitHub Token authentication and SHA-256 check."""
    dest_path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = dest_path.with_suffix(dest_path.suffix + ".part")

    headers = {"User-Agent": "SonoWorkstation-DepsSync/1.0"}
    gh_token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if gh_token and "github.com" in url:
        headers["Authorization"] = f"Bearer {gh_token}"

    print(f"  ⬇️  Downloading: {url}")
    req = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=120) as response, tmp_path.open("wb") as out_file:
            shutil.copyfileobj(response, out_file)
    except Exception as exc:
        if tmp_path.exists():
            tmp_path.unlink()
        raise RuntimeError(f"Failed to download {url}: {exc}") from exc

    if expected_sha256 and expected_sha256 != "SKIP_IF_LOCAL_EXISTS":
        actual_sha256 = compute_sha256(tmp_path)
        if actual_sha256.lower() != expected_sha256.lower():
            tmp_path.unlink()
            raise RuntimeError(
                f"SHA-256 mismatch for {dest_path.name}!\n"
                f"  Expected: {expected_sha256}\n"
                f"  Actual:   {actual_sha256}"
            )

    tmp_path.replace(dest_path)


def extract_archive(archive_path: Path, dest_dir: Path, strip_components: int = 0) -> None:
    """Extract .zip, .tar.gz, .tar.bz2, or .tar.xz archive into dest_dir.

    Supports stripping `strip_components` leading path segments (like `tar --strip-components=N`)
    so official upstream archives with a top-level versioned folder extract cleanly into `dest_dir`.
    """
    dest_dir.mkdir(parents=True, exist_ok=True)
    name = archive_path.name.lower()
    print(
        f"  📦 Extracting {archive_path.name} -> {dest_dir.relative_to(ROOT_DIR) if dest_dir.is_relative_to(ROOT_DIR) else dest_dir}"
        f" (strip_components={strip_components})"
    )
    if name.endswith(".zip"):
        with zipfile.ZipFile(archive_path, "r") as zf:
            if strip_components <= 0:
                zf.extractall(dest_dir)
            else:
                for member in zf.infolist():
                    parts = Path(member.filename).parts
                    if len(parts) <= strip_components:
                        continue
                    rel_target = dest_dir.joinpath(*parts[strip_components:])
                    if member.is_dir():
                        rel_target.mkdir(parents=True, exist_ok=True)
                    else:
                        rel_target.parent.mkdir(parents=True, exist_ok=True)
                        with zf.open(member, "r") as src, rel_target.open("wb") as dst:
                            shutil.copyfileobj(src, dst)
    elif name.endswith((".tar.gz", ".tgz", ".tar.bz2", ".tbz2", ".tar.xz", ".txz")):
        with tarfile.open(archive_path, "r:*") as tf:
            if strip_components <= 0:
                tf.extractall(dest_dir)
            else:
                members_to_extract: list[tarfile.TarInfo] = []
                for member in tf.getmembers():
                    parts = Path(member.name).parts
                    if len(parts) <= strip_components:
                        continue
                    member.name = str(Path(*parts[strip_components:]))
                    if member.islnk() or member.issym():
                        link_parts = Path(member.linkname).parts
                        if len(link_parts) > strip_components and link_parts[0] == parts[0]:
                            member.linkname = str(Path(*link_parts[strip_components:]))
                    members_to_extract.append(member)
                tf.extractall(dest_dir, members=members_to_extract)
    else:
        raise RuntimeError(f"Unsupported archive format: {archive_path.name}")


def sync_sdks(
    manifest: dict,
    variables: dict[str, str],
    target_platform: str,
    dry_run: bool = False,
    force: bool = False,
) -> None:
    """Sync platform-specific SDKs (CEF, ONNX Runtime)."""
    sdks = manifest.get("sdks", {})
    for sdk_name, sdk_info in sdks.items():
        targets = sdk_info.get("targets", {})
        if target_platform not in targets:
            print(f"ℹ️  [{sdk_name}] No external archive needed for {target_platform} (using system/default).")
            continue

        spec = targets[target_platform]
        url = interpolate(spec["url"], variables)
        dest_dir = ROOT_DIR / spec["dest"]
        verify_rel = spec.get("verify_path", "")
        verify_file = dest_dir / verify_rel if verify_rel else dest_dir
        expected_sha = spec.get("sha256", "")

        if not force and verify_file.exists() and not is_git_lfs_pointer(verify_file):
            print(f"✅ [{sdk_name}] ({target_platform}) Ready at {dest_dir.relative_to(ROOT_DIR)}")
            continue

        print(f"🔄 [{sdk_name}] ({target_platform}) Missing or incomplete at {dest_dir.relative_to(ROOT_DIR)}")
        if dry_run:
            print(f"  [Dry-Run] Would download {url} -> {dest_dir.relative_to(ROOT_DIR)}")
            continue

        archive_name = urllib.parse.unquote(url.split("/")[-1])
        archive_path = CACHE_DIR / archive_name
        strip_components = int(spec.get("strip_components", 0))
        download_file(url, archive_path, expected_sha)
        extract_archive(archive_path, dest_dir, strip_components=strip_components)
        print(f"✅ [{sdk_name}] Installed to {dest_dir.relative_to(ROOT_DIR)}")


def sync_models(
    manifest: dict,
    variables: dict[str, str],
    dry_run: bool = False,
    force: bool = False,
) -> None:
    """Sync cross-platform ONNX model files."""
    models = manifest.get("models", {})
    for model_name, spec in models.items():
        url = interpolate(spec["url"], variables)
        dest_file = ROOT_DIR / spec["dest"]
        expected_sha = spec.get("sha256", "")

        if not force and dest_file.is_file() and not is_git_lfs_pointer(dest_file):
            if expected_sha and expected_sha != "SKIP_IF_LOCAL_EXISTS":
                actual_sha = compute_sha256(dest_file)
                if actual_sha.lower() == expected_sha.lower():
                    print(
                        f"✅ [model:{model_name}] v{spec.get('version')} verified "
                        f"({dest_file.relative_to(ROOT_DIR)})"
                    )
                    continue
                print(f"⚠️  [model:{model_name}] SHA-256 changed, re-syncing...")
            else:
                print(f"✅ [model:{model_name}] Ready at {dest_file.relative_to(ROOT_DIR)}")
                continue

        if dry_run:
            print(f"  [Dry-Run] Would download {url} -> {dest_file.relative_to(ROOT_DIR)}")
            continue

        download_file(url, dest_file, expected_sha)
        print(f"✅ [model:{model_name}] Synced to {dest_file.relative_to(ROOT_DIR)}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Synchronize SonoWorkstation SDKs, Qt6, and AI Models")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST, help="Path to manifest.json")
    parser.add_argument("--platform", type=str, default=None, help="Override target platform (e.g. darwin-arm64)")
    parser.add_argument("--qt-mirror", type=str, default=None, help="Override Qt mirror URL (e.g. https://mirror.nju.edu.cn/qt)")
    parser.add_argument("--model-repo-url", type=str, default=None, help="Override model repository URL")
    parser.add_argument("--dry-run", action="store_true", help="Print actions without downloading")
    parser.add_argument("--force", action="store_true", help="Force re-download SDKs and models")
    parser.add_argument("--force-qt", action="store_true", help="Force install Qt6 into shared cache via aqtinstall")
    args = parser.parse_args()

    if not args.manifest.is_file():
        print(f"❌ Manifest file not found: {args.manifest}", file=sys.stderr)
        return 1

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    target_platform = args.platform or detect_host_target()
    variables = resolve_variables(
        manifest,
        {
            "QT_MIRROR_URL": args.qt_mirror,
            "MODEL_REPO_URL": args.model_repo_url,
        },
    )

    print("============================================================")
    print(f"🚀 SonoWorkstation Dependency Sync | Target: {target_platform}")
    print(f"🔗 Qt Mirror:    {variables.get('QT_MIRROR_URL')}")
    print(f"🔗 CEF Source:   {variables.get('CEF_BASE_URL')}")
    print(f"🔗 ORT Source:   {variables.get('ORT_BASE_URL')}")
    print(f"🔗 Model Repo:   {variables.get('MODEL_REPO_URL')}")
    print("============================================================")

    sync_qt6(manifest, variables, target_platform, dry_run=args.dry_run, force_qt=args.force_qt)
    sync_sdks(manifest, variables, target_platform, dry_run=args.dry_run, force=args.force)
    sync_models(manifest, variables, dry_run=args.dry_run, force=args.force)
    return 0


if __name__ == "__main__":
    sys.exit(main())
