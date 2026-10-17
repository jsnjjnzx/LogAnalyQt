import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/release"))
import github_release


class GitHubReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.tag = "v3.1.1"
        self.repository = "jsnjjnzx/LogAnalyQt"
        for name in github_release.expected_asset_names("3.1.1", "arm64"):
            path = self.directory / name
            if name.endswith(".sha256"):
                package = self.directory / name.removesuffix(".sha256")
                path.write_text(hashlib.sha256(package.read_bytes()).hexdigest() + "  " + package.name)
            elif name.endswith(".exe"):
                data = bytearray(128)
                data[:2] = b"MZ"
                struct.pack_into("<I", data, 0x3C, 64)
                data[64:70] = b"PE\0\0\x64\x86"
                path.write_bytes(data)
            else:
                path.write_bytes(b"test package")

    def fake_gh(self, published=False, broken_digest=False):
        def call(*arguments):
            if arguments[0] == "api" and "commits" in arguments[1]:
                return "tested-commit\n"
            if arguments[0] == "api" and "--paginate" in arguments:
                return json.dumps([[{"tag_name": self.tag, "draft": False}]] if published else [[]])
            if arguments[0] == "api" and "/releases/tags/" in arguments[1]:
                raise RuntimeError("GitHub does not return draft releases by tag: HTTP 404")
            if arguments[:2] == ("release", "view"):
                return json.dumps({"apiUrl": f"https://api.github.com/repos/{self.repository}/releases/123"})
            if arguments[0] == "api" and arguments[1].endswith("/releases/123"):
                paths = [self.directory / name for name in github_release.expected_asset_names("3.1.1", "arm64")]
                paths.append(self.directory / "update.json")
                return json.dumps({"draft": True, "tag_name": self.tag, "assets": [{"name": path.name, "size": path.stat().st_size,
                    "digest": "sha256:" + ("0" * 64 if broken_digest else hashlib.sha256(path.read_bytes()).hexdigest())}
                    for path in paths]})
            return ""
        return call

    def test_manifest_records_exact_files_and_hashes(self):
        metadata = github_release.manifest(self.directory, self.tag, "更新说明", self.repository)
        self.assertEqual(len(metadata["assets"]), 6)
        self.assertEqual(metadata["tag_name"], self.tag)
        for asset in metadata["assets"]:
            self.assertEqual(asset["sha256"], hashlib.sha256((self.directory / asset["name"]).read_bytes()).hexdigest())
            self.assertIn(f"/download/{self.tag}/", asset["browser_download_url"])

    def test_publishes_only_after_seven_uploaded_digests_match(self):
        with patch.object(github_release, "gh", side_effect=self.fake_gh()) as command:
            github_release.publish(self.directory, self.tag, "notes", self.repository, "tested-commit")
        self.assertIn("--draft", command.call_args_list[2].args)
        self.assertEqual(command.call_args.args[-2:], ("--draft=false", "--latest"))

    def test_damaged_uploaded_asset_leaves_draft(self):
        with patch.object(github_release, "gh", side_effect=self.fake_gh(broken_digest=True)) as command:
            with self.assertRaisesRegex(ValueError, "verification"):
                github_release.publish(self.directory, self.tag, "notes", self.repository, "tested-commit")
        self.assertFalse(any("--draft=false" in call.args for call in command.call_args_list))

    def test_never_overwrites_published_release(self):
        with patch.object(github_release, "gh", side_effect=self.fake_gh(published=True)) as command:
            with self.assertRaisesRegex(ValueError, "overwrite"):
                github_release.publish(self.directory, self.tag, "notes", self.repository, "tested-commit")
        self.assertFalse(any("upload" in call.args for call in command.call_args_list))

    def test_missing_platform_or_corrupt_hash_cannot_create_draft(self):
        (self.directory / "LogAnalyQt_v3.1.1_macOS_arm64.dmg").write_bytes(b"corrupt")
        with patch.object(github_release, "gh") as command:
            with self.assertRaisesRegex(ValueError, "SHA256"):
                github_release.publish(self.directory, self.tag, "notes", self.repository, "tested-commit")
        command.assert_not_called()

    def test_moved_tag_cannot_create_draft(self):
        with patch.object(github_release, "gh", return_value="other-commit") as command:
            with self.assertRaisesRegex(ValueError, "tested commit"):
                github_release.publish(self.directory, self.tag, "notes", self.repository, "tested-commit")
        self.assertEqual(command.call_count, 1)

    def test_main_push_and_manual_dispatch_cannot_publish(self):
        for event, ref in (("push", "refs/heads/main"), ("workflow_dispatch", "refs/tags/v3.1.1")):
            with patch.dict(os.environ, {"GITHUB_EVENT_NAME": event, "GITHUB_REF": ref}), \
                 patch.object(sys, "argv", ["github_release.py", "--directory", str(self.directory), "--tag", self.tag]), \
                 patch.object(github_release, "publish") as publish:
                with self.assertRaisesRegex(ValueError, "Only a pushed"):
                    github_release.main()
                publish.assert_not_called()


if __name__ == "__main__":
    unittest.main()
