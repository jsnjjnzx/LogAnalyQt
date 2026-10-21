"""Check LaunchServices startup readiness, delayed success and a real timeout."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--repeat", type=int, default=1)
    args = parser.parse_args()
    if not 1 <= args.repeat <= 20:
        parser.error("--repeat must be between 1 and 20")
    binary = args.binary.resolve()
    with tempfile.TemporaryDirectory(prefix="loganalyqt-window-activation-") as directory:
        root = Path(directory)
        bundle = root / "LogAnalyQt.app"
        shutil.copytree(binary.parents[2], bundle, symlinks=True)
        executable = bundle / "Contents/MacOS" / binary.name
        cases = [(f"{number}-{state}", state, 0, 10000, 0)
                 for number in range(args.repeat) for state in ("hidden", "minimized")]
        cases += [("delayed", "minimized", 2000, 10000, 0), ("expected-timeout", "hidden", 2000, 250, 8)]
        for name, state, delay, timeout, expected in cases:
            marker = root / f"{name}.json"
            exit_result = root / f"{name}-exit.json"
            home = root / name
            environment = dict(os.environ, LOGANALYQT_UPDATE_TEST_RESULT=str(marker),
                               LOGANALYQT_TEST_EXIT_RESULT=str(exit_result),
                               LOGANALYQT_TEST_HOME=str(home), LOGANALYQT_TEST_READY_DELAY_MS=str(delay),
                               LOGANALYQT_TEST_READY_TIMEOUT_MS=str(timeout))
            if environment.get("LOGANALYQT_DIAGNOSTICS_DIR"):
                environment["LOGANALYQT_DIAGNOSTICS_DIR"] = str(Path(environment["LOGANALYQT_DIAGNOSTICS_DIR"]) / f"activation-{name}")
            started = time.monotonic()
            log_path = home / "Library/Logs/LogAnalyQt/log.txt"
            stdout_path = root / f"{name}-stdout.txt"
            stderr_path = root / f"{name}-stderr.txt"
            command = ["/usr/bin/open", "-n", "-W", "--stdout", str(stdout_path), "--stderr", str(stderr_path)]
            for key, value in environment.items():
                if key.startswith(("LOGANALYQT_", "QT_", "QML", "DYLD_")):
                    command += ["--env", f"{key}={value}"]
            command += [str(bundle), "--args", "-platform", "cocoa", "--ui-smoke", "--ui-start-visibility", state]
            try:
                result = subprocess.run(
                    command,
                    cwd=root, env=environment, capture_output=True, text=True, timeout=30,
                )
            except subprocess.TimeoutExpired:
                evidence = environment.get("LOGANALYQT_DIAGNOSTICS_DIR")
                if evidence:
                    destination = Path(evidence)
                    destination.mkdir(parents=True, exist_ok=True)
                    if log_path.exists(): shutil.copy2(log_path, destination / "application.log")
                raise
            log = log_path.read_text(encoding="utf-8") if log_path.exists() else ""
            try:
                assert result.returncode == 0, (result.returncode, result.stderr)
                assert exit_result.exists(), f"{name}: application did not exit normally; {log}"
                assert json.loads(exit_result.read_text())["exitCode"] == expected, (name, expected, log)
                if expected:
                    assert not marker.exists() and "WINDOW_READY_TIMEOUT" in log, log
                    print("PASS: missing readiness fails explicitly without a success marker", flush=True)
                    continue
                warnings = [line for line in log.splitlines()
                            if any(level in line for level in (" WARN ", " CRIT ", " FATAL "))
                            and "Populating font family aliases" not in line]
                assert not warnings, warnings
                assert marker.exists(), f"{name}: main window did not become rendered, visible and active; {log}"
                observed = json.loads(marker.read_text())
                assert observed["visible"] is True and observed["active"] is True, (name, observed)
                assert observed["rendered"] is True and observed["readyElapsedMs"] >= delay, (name, observed)
                assert Path(observed["executable"]).resolve() == executable.resolve()
                assert (time.monotonic() - started) * 1000 >= delay
                print(f"PASS: {name} main window restored, rendered and activated", flush=True)
            except AssertionError:
                evidence = os.environ.get("LOGANALYQT_DIAGNOSTICS_DIR")
                if evidence:
                    destination = Path(evidence) / f"activation-{name}"
                    destination.mkdir(parents=True, exist_ok=True)
                    (destination / "application.log").write_text(log, encoding="utf-8")
                    (destination / "stderr.txt").write_text(result.stderr, encoding="utf-8")
                    if stderr_path.exists(): shutil.copy2(stderr_path, destination / "application-stderr.txt")
                    if stdout_path.exists(): shutil.copy2(stdout_path, destination / "application-stdout.txt")
                    if marker.exists(): shutil.copy2(marker, destination / marker.name)
                raise


if __name__ == "__main__":
    main()
