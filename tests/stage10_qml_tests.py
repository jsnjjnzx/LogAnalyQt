"""Exercise both real sync/terminal QML views with isolated backend substitutes only."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from prot698_qml_tests import module


def main():
    source = Path(__file__).resolve().parents[1]
    runner = sys.argv[1] if len(sys.argv) > 1 else shutil.which("qmltestrunner")
    runner = runner or "/opt/homebrew/opt/qtdeclarative/bin/qmltestrunner"
    with tempfile.TemporaryDirectory(prefix="loganalyqt-stage10-qml-") as temporary:
        imports = Path(temporary)
        legacy = imports / "LogAnalyQt"
        module(legacy, "LogAnalyQt", source / "qml")
        module(legacy / "Next", "LogAnalyQt.Next", source / "qml/Next")
        (legacy / "SshTerminal.qml").symlink_to(source / "tests/qml-stage10/MockSshTerminal.qml")
        with (legacy / "qmldir").open("a") as stream:
            stream.write("SshTerminal 1.0 SshTerminal.qml\n")
        icon_directory = imports / "assets"
        icon_directory.mkdir()
        (icon_directory / "logo.ico").symlink_to(source / "assets/logo.ico")
        environment = os.environ.copy()
        environment["QT_QUICK_CONTROLS_STYLE"] = "Basic"
        result = subprocess.run([runner, "-input", str(source / "tests/qml-stage10"), "-import", str(imports), "-platform", "offscreen"],
                                cwd=source, env=environment, capture_output=True, text=True, timeout=90)
        output = result.stdout + result.stderr
        print(output, end="")
        warnings = [line for line in output.splitlines() if any(level in line for level in ("QWARN", "QFATAL", "QCRITICAL")) and "Populating font family aliases" not in line]
        return result.returncode or (1 if warnings else 0)


if __name__ == "__main__":
    raise SystemExit(main())
