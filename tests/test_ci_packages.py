import hashlib
import importlib.util
import struct
import tempfile
import unittest
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


if __name__ == "__main__":
    unittest.main()
