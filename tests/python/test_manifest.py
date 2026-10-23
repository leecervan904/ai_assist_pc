"""Unit tests for `deps/manifest.json` and `scripts/sync_deps.py`."""

from __future__ import annotations

import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

ROOT_DIR = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT_DIR / "scripts"))

import repo_info  # noqa: E402
import sync_deps  # noqa: E402


class TestDependencyManifest(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest_path = ROOT_DIR / "deps" / "manifest.json"
        self.assertTrue(self.manifest_path.is_file(), "deps/manifest.json must exist")
        self.manifest = json.loads(self.manifest_path.read_text(encoding="utf-8"))

    def test_manifest_variables_exist(self) -> None:
        variables = self.manifest.get("variables", {})
        self.assertEqual(
            variables.get("CEF_BASE_URL"),
            "https://cef-builds.spotifycdn.com",
        )
        self.assertEqual(
            variables.get("ORT_BASE_URL"),
            "https://github.com/microsoft/onnxruntime/releases/download",
        )
        self.assertEqual(
            variables.get("MODEL_REPO_URL"),
            "https://github.com/leecervan904/ai_assist_pc",
        )
        self.assertEqual(variables.get("MODEL_RELEASE_TAG"), "models-v1.0.0")
        self.assertNotIn("QT_VERSION", variables)
        self.assertNotIn("QT_MIRROR_URL", variables)

    def test_variable_interpolation_and_model_repo_override(self) -> None:
        variables = sync_deps.resolve_variables(
            self.manifest,
            {
                "CEF_BASE_URL": "https://mirror.example.com/cef/",
                "MODEL_REPO_URL": "https://github.com/custom-org/custom-repo/",
            },
        )
        self.assertEqual(variables["CEF_BASE_URL"], "https://mirror.example.com/cef")
        self.assertEqual(variables["MODEL_REPO_URL"], "https://github.com/custom-org/custom-repo")
        cef_url = sync_deps.interpolate(
            self.manifest["sdks"]["cef"]["targets"]["darwin-arm64"]["url"],
            variables,
        )
        self.assertTrue(cef_url.startswith("https://mirror.example.com/cef/cef_binary_154.0.34"))
        model_url = sync_deps.interpolate(self.manifest["models"]["thyroid"]["url"], variables)
        self.assertEqual(
            model_url,
            "https://github.com/custom-org/custom-repo/releases/download/"
            "models-v1.0.0/thyroid.onnx",
        )

    def test_model_repo_url_is_derived_from_current_repository(self) -> None:
        manifest = {
            **self.manifest,
            "variables": {
                key: value
                for key, value in self.manifest["variables"].items()
                if key != "MODEL_REPO_URL"
            },
        }
        with (
            patch.dict(os.environ, {}, clear=True),
            patch(
                "sync_deps.detect_repo_url",
                return_value="https://github.com/new-org/new-repo",
            ),
        ):
            variables = sync_deps.resolve_variables(manifest, {})
        self.assertEqual(variables["MODEL_REPO_URL"], "https://github.com/new-org/new-repo")

    def test_model_repo_url_accepts_environment_override(self) -> None:
        with patch.dict(
            os.environ,
            {"SONO_MODEL_REPO_URL": "https://github.com/env-org/env-repo/"},
        ):
            variables = sync_deps.resolve_variables(self.manifest, {})
        self.assertEqual(variables["MODEL_REPO_URL"], "https://github.com/env-org/env-repo")

    def test_extract_archive_strip_components(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = Path(tmpdir)
            zip_path = tmp_path / "dummy.zip"
            with zipfile.ZipFile(zip_path, "w") as zf:
                zf.writestr("top_level_dir/include/header.h", "#pragma once\n")
                zf.writestr("top_level_dir/Release/libcef.dll", "binary")

            extract_dir = tmp_path / "out"
            sync_deps.extract_archive(zip_path, extract_dir, strip_components=1)
            self.assertTrue((extract_dir / "include" / "header.h").is_file())
            self.assertTrue((extract_dir / "Release" / "libcef.dll").is_file())
            self.assertFalse((extract_dir / "top_level_dir").exists())

    def test_models_have_valid_sha256_and_config(self) -> None:
        models = self.manifest.get("models", {})
        self.assertIn("thyroid", models)
        self.assertIn("XT-7-640", models)
        for name, spec in models.items():
            sha = spec.get("sha256", "")
            self.assertEqual(len(sha), 64, f"Model {name} must have a 64-char SHA-256 hex digest")
            cfg_path = ROOT_DIR / spec["config"]
            self.assertTrue(cfg_path.is_file(), f"Config {cfg_path} for model {name} must exist")

    def test_sync_models_plans_download_from_configured_repository(self) -> None:
        manifest = {
            "models": {
                "demo": {
                    "version": "1.0.0",
                    "url": "${MODEL_REPO_URL}/releases/download/${MODEL_RELEASE_TAG}/demo.onnx",
                    "sha256": "SKIP_IF_LOCAL_EXISTS",
                    "dest": "assets/models/demo.onnx",
                }
            }
        }
        variables = {
            "MODEL_REPO_URL": "https://github.com/acme/models",
            "MODEL_RELEASE_TAG": "models-v1",
        }

        with (
            tempfile.TemporaryDirectory() as tmpdir,
            redirect_stdout(io.StringIO()) as output,
            patch.object(sync_deps, "ROOT_DIR", Path(tmpdir)),
        ):
            sync_deps.sync_models(manifest, variables, dry_run=True)

        self.assertIn(
            "https://github.com/acme/models/releases/download/models-v1/demo.onnx",
            output.getvalue(),
        )

    def test_qt6_is_host_managed_and_not_in_manifest(self) -> None:
        self.assertNotIn("qt6", self.manifest)

    def test_qt6_detection_requires_multimedia(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            fake_prefix = Path(tmpdir) / "6.8.2" / "macos"
            (fake_prefix / "lib" / "cmake" / "Qt6").mkdir(parents=True)
            (fake_prefix / "lib" / "cmake" / "Qt6" / "Qt6Config.cmake").write_text("# mock\n")
            self.assertFalse(sync_deps.has_qt6_with_multimedia(fake_prefix))

            (fake_prefix / "lib" / "cmake" / "Qt6Multimedia").mkdir(parents=True)
            (
                fake_prefix / "lib" / "cmake" / "Qt6Multimedia" / "Qt6MultimediaConfig.cmake"
            ).write_text("# mock\n")
            self.assertTrue(sync_deps.has_qt6_with_multimedia(fake_prefix))

            with patch.dict(
                os.environ,
                {"CMAKE_PREFIX_PATH": str(fake_prefix)},
                clear=True,
            ):
                self.assertEqual(
                    sync_deps.find_local_qt6("linux-x64"),
                    fake_prefix.resolve(),
                )


class TestRepoInfo(unittest.TestCase):
    def test_repo_slug_from_https_remote(self) -> None:
        self.assertEqual(
            repo_info.repo_slug_from_remote("https://github.com/new-org/new-repo.git"),
            "new-org/new-repo",
        )

    def test_repo_slug_from_ssh_remote(self) -> None:
        self.assertEqual(
            repo_info.repo_slug_from_remote("git@github.com:new-org/new-repo.git"),
            "new-org/new-repo",
        )

    def test_repo_slug_for_enterprise_host(self) -> None:
        self.assertEqual(
            repo_info.repo_slug_from_remote("ssh://git@github.example.com/new-org/new-repo.git"),
            "github.example.com/new-org/new-repo",
        )

    def test_repo_url_from_github_slug(self) -> None:
        self.assertEqual(
            repo_info.repo_url_from_slug("new-org/new-repo"),
            "https://github.com/new-org/new-repo",
        )

    def test_repo_url_from_enterprise_slug(self) -> None:
        self.assertEqual(
            repo_info.repo_url_from_slug("github.example.com/new-org/new-repo"),
            "https://github.example.com/new-org/new-repo",
        )

    def test_detect_repo_url_uses_github_server(self) -> None:
        with patch.dict(
            os.environ,
            {
                "GITHUB_REPOSITORY": "new-org/new-repo",
                "GITHUB_SERVER_URL": "https://github.example.com",
            },
        ):
            self.assertEqual(
                repo_info.detect_repo_url(ROOT_DIR),
                "https://github.example.com/new-org/new-repo",
            )

    def test_detect_repo_slug_prefers_github_repository(self) -> None:
        with patch.dict(os.environ, {"GITHUB_REPOSITORY": "renamed-org/renamed-repo"}):
            self.assertEqual(
                repo_info.detect_repo_slug(ROOT_DIR),
                "renamed-org/renamed-repo",
            )

    def test_detect_repo_slug_falls_back_to_origin(self) -> None:
        completed = subprocess.CompletedProcess(
            [],
            0,
            stdout="git@github.com:renamed-org/renamed-repo.git\n",
            stderr="",
        )
        with (
            patch.dict(os.environ, {}, clear=True),
            patch("repo_info.subprocess.run", return_value=completed),
        ):
            self.assertEqual(
                repo_info.detect_repo_slug(ROOT_DIR),
                "renamed-org/renamed-repo",
            )


if __name__ == "__main__":
    unittest.main()
