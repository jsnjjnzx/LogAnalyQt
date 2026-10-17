import hashlib
import importlib.util
import struct
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "check_build", Path(__file__).resolve().parents[1] / "scripts/ci/check_build.py"
)
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)


class PackageChecks(unittest.TestCase):
    def test_windows_architecture_and_checksum(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / "LogAnalyQt_v3.1.0_Windows_x64.exe"
            data = bytearray(256)
            data[:2] = b"MZ"
            struct.pack_into("<I", data, 0x3C, 128)
            data[128:134] = b"PE\0\0\x64\x86"
            package.write_bytes(data)
            checksum = Path(str(package) + ".sha256")
            checksum.write_text(hashlib.sha256(data).hexdigest() + "  " + package.name)
            ci.check_packages(root, "3.1.0", "windows")
            checksum.write_text("0" * 64)
            with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
                ci.check_packages(root, "3.1.0", "windows")
            data[132:134] = b"\x64\xaa"
            package.write_bytes(data)
            checksum.write_text(hashlib.sha256(data).hexdigest())
            with self.assertRaisesRegex(ValueError, "must target x64"):
                ci.check_packages(root, "3.1.0", "windows")

    def test_missing_macos_package_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "Missing or empty"):
                ci.check_packages(Path(temporary), "3.1.0", "macos")

    def test_macos_bundle_rejects_external_libraries_and_wrong_architecture(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = Path(temporary)
            (bundle / "app").write_bytes(b"\xcf\xfa\xed\xfe")
            with patch.object(ci.subprocess, "check_output", side_effect=[
                "x86_64 arm64", "app:\n\t@rpath/QtCore (compatibility version 6.0.0)\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)",
            ]):
                ci.check_macos_bundle(bundle)
            with patch.object(ci.subprocess, "check_output", return_value="x86_64"):
                with self.assertRaisesRegex(ValueError, "Missing arm64"):
                    ci.check_macos_bundle(bundle)
            with patch.object(ci.subprocess, "check_output", side_effect=[
                "arm64", "app:\n\t/opt/homebrew/lib/libiodbc.dylib (compatibility version 1.0.0)",
            ]):
                with self.assertRaisesRegex(ValueError, "External dependency"):
                    ci.check_macos_bundle(bundle)


if __name__ == "__main__":
    unittest.main()
