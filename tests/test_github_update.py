"""Run the packaged app's real GitHub download/install/restart path in a temporary copy."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


def main():
    binary = Path(sys.argv[1]).resolve()
    version = sys.argv[2].removeprefix("v")
    probe = sys.argv[2] == "--probe"
    with tempfile.TemporaryDirectory(prefix="loganalyqt-github-update-") as directory:
        root = Path(directory)
        if sys.platform == "darwin":
            bundle = root / "LogAnalyQt.app"
            shutil.copytree(binary.parents[2], bundle, symlinks=True)
            executable = bundle / "Contents/MacOS" / binary.name
            (bundle / "old-update-test-marker").write_text("controlled older installation")
        else:
            executable = root / "LogAnalyQt.exe"
            shutil.copy2(binary, executable)
            with executable.open("ab") as output:
                output.write(b"controlled older installation")
        prompt = root / "prompts/ai_698_framing.md"
        prompt.parent.mkdir()
        prompt.write_text("User prompt survives a real GitHub update.\n")
        workspace = root / "unrelated-user-data.txt"
        workspace.write_text("keep all portable data")
        result_path = root / "restart-result.json"
        environment = dict(os.environ, LOGANALYQT_UPDATE_TEST_RESULT=str(result_path))
        for variable in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH",
                         "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
            environment.pop(variable, None)
        process = subprocess.Popen([str(executable), "--ui-smoke", "--update-probe" if probe else "--update-test"],
                                   cwd=root, env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            returncode = process.wait(timeout=900)
            assert returncode == 0, f"update client failed: {returncode}"
            if probe:
                print("PASS: packaged app reached the GitHub update endpoint over HTTPS; no installation attempted")
                return
            deadline = time.monotonic() + 90
            while not result_path.exists() and time.monotonic() < deadline:
                time.sleep(0.25)
            assert result_path.exists(), "replacement did not restart the new app"
            result = json.loads(result_path.read_text())
            assert result["version"] == version, result
            assert Path(result["executable"]).resolve() == executable, result
            assert prompt.read_text() == "User prompt survives a real GitHub update.\n"
            assert workspace.read_text() == "keep all portable data"
            if sys.platform == "darwin":
                assert not (bundle / "old-update-test-marker").exists(), "bundle was not replaced"
                subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)
            else:
                assert hashlib.sha256(executable.read_bytes()).digest() == hashlib.sha256(binary.read_bytes()).digest()
            time.sleep(3)
            print(f"PASS: simulated older client checked GitHub Release, downloaded, verified SHA256, saved, replaced, restarted {version} and preserved portable data")
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
            log = root / "log/log.txt"
            if log.exists():
                for line in log.read_text(encoding="utf-8").splitlines():
                    if "UPDATE_TEST_" in line or " CRIT " in line or " FATAL " in line:
                        print(line)


if __name__ == "__main__":
    main()
