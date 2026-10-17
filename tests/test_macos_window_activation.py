"""Check native Mac startup restores hidden/minimized windows in a disposable copy."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="loganalyqt-window-activation-") as directory:
        root = Path(directory)
        bundle = root / "LogAnalyQt.app"
        shutil.copytree(binary.parents[2], bundle, symlinks=True)
        executable = bundle / "Contents/MacOS" / binary.name
        for state in ("hidden", "minimized"):
            marker = root / f"{state}.json"
            home = root / state
            environment = dict(os.environ, LOGANALYQT_UPDATE_TEST_RESULT=str(marker),
                               LOGANALYQT_TEST_HOME=str(home))
            result = subprocess.run(
                [str(executable), "-platform", "cocoa", "--ui-smoke", "--ui-start-visibility", state],
                cwd=root, env=environment, capture_output=True, text=True, timeout=30,
            )
            log_path = home / "Library/Logs/LogAnalyQt/log.txt"
            log = log_path.read_text(encoding="utf-8") if log_path.exists() else ""
            warnings = [line for line in log.splitlines()
                        if any(level in line for level in (" WARN ", " CRIT ", " FATAL "))
                        and "Populating font family aliases" not in line]
            assert result.returncode == 0 and not warnings, (result.stderr, warnings)
            assert marker.exists(), f"{state}: main window did not become rendered, visible and active"
            observed = json.loads(marker.read_text())
            assert observed["visible"] is True, (state, observed)
            assert observed["active"] is True, (state, observed)
            assert Path(observed["executable"]).resolve() == executable.resolve()
            print(f"PASS: {state} main window restored, rendered and activated")


if __name__ == "__main__":
    main()
