"""Unit tests for `deps/manifest.json` and `scripts/sync_deps.py`."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT_DIR / "scripts"))

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

    def test_variable_interpolation_and_override(self) -> None:
        variables = sync_deps.resolve_variables(
            self.manifest,
            {"MODEL_REPO_URL": "https://github.com/custom-org/custom-repo/"},
        )
        self.assertEqual(variables["MODEL_REPO_URL"], "https://github.com/custom-org/custom-repo")
        cef_url = sync_deps.interpolate(
            self.manifest["sdks"]["cef"]["targets"]["darwin-arm64"]["url"],
            variables,
        )
        self.assertTrue(cef_url.startswith("https://cef-builds.spotifycdn.com/cef_binary_154.0.34"))

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

    def test_qt6_manifest_and_detection(self) -> None:
        qt_cfg = self.manifest.get("qt6", {})
        self.assertEqual(qt_cfg.get("cache_dir"), "~/.cache/sono_deps/qt6")
        self.assertIn("qtmultimedia", qt_cfg.get("modules", []))
        for platform_key in ("darwin-arm64", "windows-x64", "linux-x64"):
            self.assertIn(platform_key, qt_cfg.get("targets", {}))

        with tempfile.TemporaryDirectory() as tmpdir:
            fake_prefix = Path(tmpdir) / "6.8.2" / "macos"
            (fake_prefix / "lib" / "cmake" / "Qt6").mkdir(parents=True)
            (fake_prefix / "lib" / "cmake" / "Qt6" / "Qt6Config.cmake").write_text("# mock\n")
            self.assertFalse(sync_deps.has_qt6_with_multimedia(fake_prefix))

            (fake_prefix / "lib" / "cmake" / "Qt6Multimedia").mkdir(parents=True)
            (fake_prefix / "lib" / "cmake" / "Qt6Multimedia" / "Qt6MultimediaConfig.cmake").write_text("# mock\n")
            self.assertTrue(sync_deps.has_qt6_with_multimedia(fake_prefix))


if __name__ == "__main__":
    unittest.main()
