"""Derive the current repository identity without hardcoding an organization."""

from __future__ import annotations

import os
import re
import subprocess
from pathlib import Path
from urllib.parse import urlparse

_SCP_REMOTE = re.compile(r"^(?:[^@/]+@)?(?P<host>[^:/]+):(?P<path>.+)$")
_WINDOWS_PATH = re.compile(r"^[A-Za-z]:[\\/]")


def repo_slug_from_remote(remote: str) -> str | None:
    """Convert HTTPS, SSH or SCP-style Git remotes to ``owner/repo``."""
    remote = remote.strip().rstrip("/")
    if not remote or _WINDOWS_PATH.match(remote):
        return None

    if "://" in remote:
        parsed = urlparse(remote)
        if parsed.scheme not in {"http", "https", "ssh", "git"} or not parsed.hostname:
            return None
        host = parsed.hostname
        path = parsed.path.lstrip("/")
    else:
        match = _SCP_REMOTE.match(remote)
        if not match:
            return None
        host = match.group("host")
        path = match.group("path")

    path = path.removesuffix(".git").strip("/")
    parts = [part for part in path.split("/") if part]
    if len(parts) < 2:
        return None

    slug = "/".join(parts[-2:])
    return slug if host == "github.com" else f"{host}/{slug}"


def detect_repo_slug(root: Path) -> str | None:
    """Prefer CI's GITHUB_REPOSITORY, then fall back to the origin remote."""
    ci_repo = os.environ.get("GITHUB_REPOSITORY", "").strip().strip("/")
    if ci_repo.count("/") == 1:
        return ci_repo

    try:
        result = subprocess.run(
            ["git", "-C", str(root), "config", "--get", "remote.origin.url"],
            check=False,
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (OSError, subprocess.SubprocessError):
        return None

    if result.returncode != 0:
        return None
    return repo_slug_from_remote(result.stdout)


def repo_url_from_slug(slug: str) -> str | None:
    """Convert ``owner/repo`` or ``host/owner/repo`` to an HTTPS repository URL."""
    slug = slug.strip().strip("/")
    if "://" in slug:
        return None
    parts = slug.split("/")
    if len(parts) < 2 or any(not part for part in parts):
        return None
    if len(parts) == 2:
        return f"https://github.com/{slug}"
    return f"https://{slug}"


def detect_repo_url(root: Path) -> str | None:
    """Resolve the current repository URL for same-repository model releases."""
    ci_repo = os.environ.get("GITHUB_REPOSITORY", "").strip().strip("/")
    if ci_repo.count("/") == 1:
        server = (os.environ.get("GITHUB_SERVER_URL") or "https://github.com").strip().rstrip("/")
        return f"{server}/{ci_repo}"

    slug = detect_repo_slug(root)
    return repo_url_from_slug(slug) if slug else None
