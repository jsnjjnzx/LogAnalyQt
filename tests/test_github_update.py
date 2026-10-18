"""Run the packaged app's real GitHub download/install/restart path in a temporary copy."""
import hashlib
import json
import os
import plistlib
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
            package_version = plistlib.loads((bundle / "Contents/Info.plist").read_bytes())["CFBundleShortVersionString"]
            standard_mac = tuple(map(int, package_version.split("."))) >= (3, 1, 3)
        else:
            standard_mac = False
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
        environment = dict(os.environ, LOGANALYQT_UPDATE_TEST_RESULT=str(result_path), LOGANALYQT_TEST_HOME=str(root / "home"))
        data_root = root / "home/Library/Application Support/LogAnalyQt" if standard_mac else root
        log_path = root / "home/Library/Logs/LogAnalyQt/log.txt" if standard_mac else root / "log/log.txt"
        for variable in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH",
                         "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
            environment.pop(variable, None)
        if standard_mac:
            data_prompt = data_root / "prompts/ai_698_framing.md"
            data_prompt.parent.mkdir(parents=True, exist_ok=True)
            data_prompt.write_bytes(prompt.read_bytes())
        workspace_environment = dict(environment)
        workspace_environment.pop("LOGANALYQT_UPDATE_TEST_RESULT")
        if not probe:
            subprocess.run([str(executable), "--ui-smoke", "--workspace-phase", "write"],
                           cwd=root, env=workspace_environment, check=True, timeout=90)
        stderr_path = root / "update-stderr.txt"
        with stderr_path.open("wb") as errors:
            process = subprocess.Popen([str(executable), "--ui-smoke", "--update-probe" if probe else "--update-test"],
                                       cwd=root, env=environment, stdout=subprocess.DEVNULL, stderr=errors)
        try:
            returncode = process.wait(timeout=900)
            assert returncode == 0, f"update client failed: {returncode}; {stderr_path.read_text(encoding='utf-8')}"
            if probe:
                print("PASS: packaged app reached the GitHub update endpoint over HTTPS; no installation attempted")
                return
            deadline = time.monotonic() + 90
            while not result_path.exists() and time.monotonic() < deadline:
                time.sleep(0.25)
            assert result_path.exists(), "updated application did not report a ready main window"
            result = json.loads(result_path.read_text())
            assert result["version"] == version, result
            assert Path(result["executable"]).resolve() == executable.resolve(), result
            if tuple(map(int, version.split("."))) >= (4, 0, 1):
                assert result.get("visible") is True, "updated application did not show its main window"
                if sys.platform == "darwin":
                    assert result.get("active") is True, "updated Mac application did not activate its main window"
            assert prompt.read_text() == "User prompt survives a real GitHub update.\n"
            assert (data_root / "prompts/ai_698_framing.md").read_text() == prompt.read_text(), "migration changed the user's prompt"
            assert workspace.read_text() == "keep all portable data"
            if sys.platform == "darwin":
                assert not (bundle / "old-update-test-marker").exists(), "bundle was not replaced"
                subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)
            else:
                assert hashlib.sha256(executable.read_bytes()).digest() == hashlib.sha256(binary.read_bytes()).digest()
            time.sleep(3)
            subprocess.run([str(executable), "--ui-smoke", "--workspace-phase", "read", "--page", "3"],
                           cwd=root, env=workspace_environment, check=True, timeout=90)
            if standard_mac:
                assert not any((root / name).exists() for name in ("settings.json", "device-state.ini", "mqtt", "LogAnalyQt.ini", "protocol_xml", "preset", "log")), "Mac update generated data beside its bundle"
                assert (data_root / ".legacy-data-imported-v1").is_file()
            print(f"PASS: simulated older client checked GitHub Release, downloaded, verified SHA256, saved, replaced, restarted {version} and preserved the workspace drafts and portable data")
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
            log = log_path
            if log.exists():
                for line in log.read_text(encoding="utf-8").splitlines():
                    if "UPDATE_TEST_" in line or " CRIT " in line or " FATAL " in line:
                        print(line)


if __name__ == "__main__":
    main()
