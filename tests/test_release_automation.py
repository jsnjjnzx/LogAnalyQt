import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RELEASE_SCRIPTS = ROOT / "scripts" / "release"
sys.path.insert(0, str(RELEASE_SCRIPTS))

from release_metadata import (  # noqa: E402
    expected_asset_names,
    normalize_tag,
    project_version,
    render_release_notes,
)
from gitee_release import GiteeReleaseClient, ReleaseError  # noqa: E402


class _FakeGiteeHandler(BaseHTTPRequestHandler):
    release = None
    assets = []
    requests = []

    def log_message(self, format_, *args):
        pass

    def _json(self, status, payload):
        import json

        data = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        type(self).requests.append(("GET", self.path, self.headers.get("Authorization")))
        if "/releases/tags/" in self.path:
            if type(self).release is None:
                self._json(404, {"message": "Not Found"})
            else:
                self._json(200, type(self).release)
            return
        if self.path.endswith("/attach_files"):
            self._json(200, type(self).assets)
            return
        self._json(404, {"message": "Not Found"})

    def do_POST(self):
        import json
        from urllib.parse import parse_qs

        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        type(self).requests.append(("POST", self.path, self.headers.get("Authorization")))
        if self.path.endswith("/releases"):
            values = parse_qs(body.decode("utf-8"))
            type(self).release = {
                "id": 7,
                "tag_name": values["tag_name"][0],
                "name": values["name"][0],
                "body": values["body"][0],
                "prerelease": True,
            }
            self._json(201, type(self).release)
            return
        if self.path.endswith("/attach_files"):
            marker = b'filename="'
            start = body.index(marker) + len(marker)
            end = body.index(b'"', start)
            name = body[start:end].decode("utf-8")
            asset = {"id": 100 + len(type(self).assets), "name": name}
            type(self).assets.append(asset)
            self._json(201, asset)
            return
        self._json(404, {"message": "Not Found"})

    def do_PATCH(self):
        import json
        from urllib.parse import parse_qs

        length = int(self.headers.get("Content-Length", "0"))
        values = parse_qs(self.rfile.read(length).decode("utf-8"))
        type(self).requests.append(("PATCH", self.path, self.headers.get("Authorization")))
        if type(self).release is None:
            self._json(404, {"message": "Not Found"})
            return
        for key in ("name", "body"):
            if key in values:
                type(self).release[key] = values[key][0]
        if "prerelease" in values:
            type(self).release["prerelease"] = values["prerelease"][0] == "true"
        self._json(200, type(self).release)

    def do_DELETE(self):
        asset_id = int(self.path.rsplit("/", 1)[1])
        type(self).requests.append(("DELETE", self.path, self.headers.get("Authorization")))
        type(self).assets = [asset for asset in type(self).assets if asset["id"] != asset_id]
        self.send_response(204)
        self.end_headers()


class ReleaseMetadataTests(unittest.TestCase):
    def test_normalize_tag_accepts_semantic_release_tag(self):
        self.assertEqual(normalize_tag("v3.1.4"), "3.1.4")

    def test_normalize_tag_rejects_non_release_tag(self):
        with self.assertRaisesRegex(ValueError, "vX.Y.Z"):
            normalize_tag("release-3.1")

    def test_project_version_reads_cmake_project_declaration(self):
        with tempfile.TemporaryDirectory() as directory:
            cmake_file = Path(directory) / "CMakeLists.txt"
            cmake_file.write_text(
                "project(LogAnalyQt VERSION 4.2.0 LANGUAGES CXX)\n",
                encoding="utf-8",
            )
            self.assertEqual(project_version(cmake_file), "4.2.0")

    def test_expected_assets_cover_windows_dmg_and_macos_updater(self):
        self.assertEqual(
            expected_asset_names("3.1.4", "arm64"),
            [
                "LogAnalyQt_v3.1.4_Windows_x64.exe",
                "LogAnalyQt_v3.1.4_Windows_x64.exe.sha256",
                "LogAnalyQt_v3.1.4_macOS_arm64.dmg",
                "LogAnalyQt_v3.1.4_macOS_arm64.dmg.sha256",
                "LogAnalyQt_v3.1.4_macOS_arm64_update.zip",
                "LogAnalyQt_v3.1.4_macOS_arm64_update.zip.sha256",
            ],
        )

    def test_release_notes_group_conventional_commits(self):
        notes = render_release_notes(
            "v3.1.4",
            "v3.1.3",
            [
                ("1111111", "feat: 新增南网协议", ""),
                ("2222222", "fix(updater): 修复下载校验", ""),
                ("3333333", "更新说明文字", ""),
            ],
        )
        self.assertIn("## 通联工坊 v3.1.4", notes)
        self.assertIn("v3.1.3...v3.1.4", notes)
        self.assertIn("### 新增功能", notes)
        self.assertIn("新增南网协议 (`1111111`)", notes)
        self.assertIn("### 问题修复", notes)
        self.assertIn("修复下载校验 (`2222222`)", notes)
        self.assertIn("### 其他变更", notes)
        self.assertIn("更新说明文字 (`3333333`)", notes)


class GiteeReleaseClientTests(unittest.TestCase):
    def setUp(self):
        _FakeGiteeHandler.release = None
        _FakeGiteeHandler.assets = []
        _FakeGiteeHandler.requests = []
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), _FakeGiteeHandler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.client = GiteeReleaseClient(
            "owner",
            "repo",
            "secret-token",
            base_url=f"http://127.0.0.1:{self.server.server_port}/api/v5",
        )

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def test_prepare_creates_prerelease_without_token_in_url(self):
        release = self.client.prepare("v3.1.4", "LogAnalyQt v3.1.4", "notes", "main")
        self.assertEqual(release["id"], 7)
        self.assertTrue(release["prerelease"])
        self.assertTrue(all("secret-token" not in path for _, path, _ in _FakeGiteeHandler.requests))
        self.assertTrue(
            all(auth == "Bearer secret-token" for _, _, auth in _FakeGiteeHandler.requests)
        )

    def test_upload_replaces_same_name_and_finalize_requires_all_assets(self):
        self.client.prepare("v3.1.4", "LogAnalyQt v3.1.4", "notes", "main")
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory) / "package.zip"
            package.write_bytes(b"first")
            self.client.upload("v3.1.4", package)
            package.write_bytes(b"second")
            self.client.upload("v3.1.4", package)

        self.assertEqual([asset["name"] for asset in _FakeGiteeHandler.assets], ["package.zip"])
        self.assertTrue(any(method == "DELETE" for method, _, _ in _FakeGiteeHandler.requests))
        with self.assertRaisesRegex(ReleaseError, "缺少附件"):
            self.client.finalize("v3.1.4", ["package.zip", "package.zip.sha256"])

        _FakeGiteeHandler.assets.append({"id": 200, "name": "package.zip.sha256"})
        release = self.client.finalize(
            "v3.1.4", ["package.zip", "package.zip.sha256"]
        )
        self.assertFalse(release["prerelease"])

    def test_upload_rejects_formal_release_when_api_returns_string_flag(self):
        self.client.prepare("v3.1.4", "LogAnalyQt v3.1.4", "notes", "main")
        _FakeGiteeHandler.release["prerelease"] = "false"
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory) / "package.zip"
            package.write_bytes(b"content")
            with self.assertRaisesRegex(ReleaseError, "正式 Release"):
                self.client.upload("v3.1.4", package)


class WindowsReleaseScriptTests(unittest.TestCase):
    def test_windows_packager_uses_explicit_version_and_ci_toolchain_environment(self):
        script = (ROOT / "scripts" / "package_single.ps1").read_text(encoding="utf-8-sig")
        self.assertIn("param(", script)
        self.assertIn("[string]$Version", script)
        self.assertIn("$env:QT_STATIC_DIR", script)
        self.assertIn("$env:MINGW_DIR", script)
        self.assertIn('-DCMAKE_BUILD_TYPE=Release', script)
        self.assertIn('"${AppName}_v${Version}_Windows_x64.exe"', script)
        self.assertNotIn("Extract Version from CHANGELOG.md", script)
        self.assertNotIn('-DCMAKE_BUILD_TYPE=Debug', script)

    def test_windows_ci_entry_validates_builds_and_uploads_release_assets(self):
        script_path = ROOT / "scripts" / "ci" / "run_windows_ci.ps1"
        self.assertTrue(script_path.is_file())
        script = script_path.read_text(encoding="utf-8-sig")
        self.assertIn("generate_release_notes.py", script)
        self.assertIn("package_single.ps1", script)
        self.assertIn('gitee_release.py', script)
        self.assertIn('prepare', script)
        self.assertGreaterEqual(script.count('upload'), 2)


class MacosReleaseScriptTests(unittest.TestCase):
    def test_macos_packager_outputs_dmg_and_dedicated_update_zip(self):
        script = (ROOT / "scripts" / "package_macos.sh").read_text(encoding="utf-8")
        self.assertIn('OUTPUT_UPDATE_NAME="LogAnalyQt_v${VERSION}_macOS_${ARCH}_update.zip"', script)
        self.assertIn('OUTPUT_DMG_NAME="LogAnalyQt_v${VERSION}_macOS_${ARCH}.dmg"', script)
        self.assertIn('APP_BUNDLE="$PACKAGE_DIR/通联工坊.app"', script)
        self.assertIn('-volname "通联工坊 $VERSION"', script)
        self.assertIn("hdiutil create", script)
        self.assertIn('/Applications', script)
        self.assertIn("REQUIRE_MACOS_DISTRIBUTION", script)
        self.assertIn("MACOS_NOTARY_PROFILE", script)
        self.assertIn("stapler staple", script)
        self.assertIn("cmake --preset macos-release", script)
        self.assertIn("cmake --build --preset macos-release", script)
        self.assertNotIn("macos-debug", script)

    def test_macos_ci_uploads_four_assets_then_finalizes(self):
        script_path = ROOT / "scripts" / "ci" / "run_macos_ci.sh"
        self.assertTrue(script_path.is_file())
        script = script_path.read_text(encoding="utf-8")
        self.assertIn("generate_release_notes.py", script)
        self.assertIn("package_macos.sh", script)
        self.assertGreaterEqual(script.count("gitee_release.py upload"), 4)
        self.assertIn("gitee_release.py finalize", script)
        self.assertIn("LogAnalyQt_v${VERSION}_Windows_x64.exe", script)

    def test_existing_updater_keeps_dmg_for_manual_install_only(self):
        source = (ROOT / "src" / "core" / "UpdateChecker.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn('lower.endsWith(QStringLiteral(".zip"))', source)
        self.assertNotIn('lower.endsWith(QStringLiteral(".dmg"))', source)


class GiteePipelineTests(unittest.TestCase):
    def test_main_pipeline_builds_both_platforms_without_release_commands(self):
        path = ROOT / ".workflow" / "MainPipeline.yml"
        self.assertTrue(path.is_file())
        workflow = path.read_text(encoding="utf-8")
        self.assertIn("precise:\n        - main", workflow)
        self.assertIn("hostGroupID: loganalyqt-windows", workflow)
        self.assertIn("hostGroupID: loganalyqt-macos", workflow)
        self.assertGreaterEqual(workflow.count("step: shell@agent"), 2)
        self.assertIn("run_windows_ci.ps1", workflow)
        self.assertIn("run_macos_ci.sh", workflow)
        self.assertIn("stages:\n  - name:", workflow)
        self.assertNotIn("- stage:", workflow)
        self.assertNotIn("Debug", workflow)
        self.assertNotIn("debug", workflow)
        self.assertNotIn("ReleasePipeline", workflow)

    def test_release_pipeline_runs_windows_before_macos_for_version_tags(self):
        path = ROOT / ".workflow" / "ReleasePipeline.yml"
        self.assertTrue(path.is_file())
        workflow = path.read_text(encoding="utf-8")
        self.assertIn("tags:", workflow)
        self.assertIn("^v[0-9]+\\.[0-9]+\\.[0-9]+$", workflow)
        self.assertIn("hostGroupID: loganalyqt-windows", workflow)
        self.assertIn("hostGroupID: loganalyqt-macos", workflow)
        self.assertIn("strategy: naturally", workflow)
        self.assertIn("run_windows_ci.ps1 -ReleaseTag", workflow)
        self.assertIn("run_macos_ci.sh --release", workflow)
        self.assertIn("stages:\n  - name:", workflow)
        self.assertNotIn("- stage:", workflow)
        self.assertNotIn("Debug", workflow)
        self.assertNotIn("debug", workflow)


if __name__ == "__main__":
    unittest.main()
