"""Exercise native update replacement in disposable installations."""
import hashlib
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import time


def main():
    helper, fixture = map(lambda value: Path(value).resolve(), sys.argv[1:3])
    windows = sys.platform == "win32"
    with tempfile.TemporaryDirectory(prefix="loganalyqt-updater-") as directory:
        root = Path(directory)
        marker = root / "restarted.txt"
        data = root / "LogAnalyQt.ini"
        data.write_text("user settings stay intact")
        data_root = root / "home/Library/Application Support/LogAnalyQt"
        log_root = root / "home/Library/Logs/LogAnalyQt"
        if windows:
            target, package = root / "old.exe", root / "new.exe"
            target.write_bytes(b"old version")
            shutil.copy2(fixture, package)
        else:
            target = root / "LogAnalyQt.app"
            executable = target / "Contents/MacOS/appLogAnalyQt"
            executable.parent.mkdir(parents=True)
            shutil.copy2(fixture, executable)
            info = {"CFBundleExecutable": "appLogAnalyQt", "CFBundleIdentifier": "com.loganalyqt.updater-test",
                    "CFBundleName": "LogAnalyQt", "CFBundlePackageType": "APPL", "CFBundleVersion": "3.1.1"}
            (target / "Contents/Info.plist").write_bytes(plistlib.dumps(info))
            subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(target)], check=True, capture_output=True)
            package = root / "new.zip"
            subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(target), str(package)], check=True)
            (target / "old-version-marker").write_text("old installation")
            (target / "Contents/MacOS/LogAnalyQt.ini").write_text("older bundle settings must not overwrite portable settings")
            legacy_prompt = target / "Contents/MacOS/prompts/user.md"
            legacy_prompt.parent.mkdir()
            legacy_prompt.write_text("preserve legacy edited prompt")

        def snapshot():
            paths = [target] if windows else [path for path in target.rglob("*") if path.is_file()]
            return {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}

        original = snapshot()
        environment = dict(os.environ, LOGANALYQT_RESTART_MARKER=str(marker))

        def run(source=package, *extra, fail=False, launch=False):
            child_environment = dict(environment)
            if fail: child_environment["LOGANALYQT_TEST_REPLACE_FAIL"] = "1"
            result = subprocess.run([str(helper), "--source", str(source), "--target", str(target),
                                     *([] if windows else ["--data-root", str(data_root), "--log-root", str(log_root)]),
                                     *( ["--launch"] if launch else []), *extra],
                                    env=child_environment, capture_output=True, timeout=20)
            assert data.read_text() == "user settings stay intact", "updater changed portable data"
            return result

        assert run(root / "missing.zip").returncode != 0
        assert snapshot() == original, "missing source changed installation"
        sleeper = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(20)"])
        try:
            assert run(package, "--pid", str(sleeper.pid), "--timeout", "100").returncode != 0
        finally:
            sleeper.terminate()
            sleeper.wait()
        assert snapshot() == original, "timeout changed installation"
        if windows:
            import ctypes
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                          ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
            kernel.CreateFileW.restype = ctypes.c_void_p
            handle = kernel.CreateFileW(str(target), 0x80000000, 3, None, 3, 0, None)
            assert handle not in (None, ctypes.c_void_p(-1).value), "cannot lock test installation"
            try:
                assert run().returncode != 0, "locked old executable must block replacement"
            finally:
                kernel.CloseHandle(ctypes.c_void_p(handle))
            assert snapshot() == original, "file lock changed old executable"
        else:
            legacy_directory = target / "Contents/MacOS"
            legacy_directory.chmod(0)
            try:
                assert run().returncode != 0, "unreadable legacy data must block replacement"
            finally:
                legacy_directory.chmod(0o755)
            assert snapshot() == original, "unreadable legacy data changed installation"
        assert run(fail=True).returncode != 0
        assert snapshot() == original, "replacement failure did not restore installation"
        assert not marker.exists(), "failed replacement restarted application"
        assert run(launch=True).returncode == 0
        deadline = time.monotonic() + 10
        while not marker.exists() and time.monotonic() < deadline:
            time.sleep(0.1)
        assert marker.read_text().startswith("restarted "), "new application did not restart"
        assert not package.exists(), "successful update did not clean downloaded file"
        if windows:
            assert target.read_bytes() == fixture.read_bytes()
        else:
            assert not (target / "old-version-marker").exists(), "old bundle was not replaced"
            assert (data_root / "prompts/user.md").read_text() == "preserve legacy edited prompt"
            assert (data_root / "LogAnalyQt.ini").read_text() == "older bundle settings must not overwrite portable settings"
            assert not (root / "prompts").exists(), "updater wrote data next to the application"
            subprocess.run(["codesign", "--verify", "--deep", "--strict", str(target)], check=True)
        print("PASS: missing package, parent timeout, legacy permissions, forced replacement rollback, portable data and native restart")


if __name__ == "__main__":
    main()
