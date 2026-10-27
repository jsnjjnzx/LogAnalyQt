"""Exercise real Next sync/terminal QML views with isolated backend substitutes only."""
import os
from pathlib import Path
import shutil
import tempfile
from prot698_qml_tests import arguments, module, run_logged


def main():
    source = Path(__file__).resolve().parents[1]
    args = arguments()
    runner = args.runner or shutil.which("qmltestrunner")
    runner = runner or "/opt/homebrew/opt/qtdeclarative/bin/qmltestrunner"
    with tempfile.TemporaryDirectory(prefix="loganalyqt-stage10-qml-") as temporary:
        imports = Path(temporary)
        backend = imports / "LogAnalyQt"
        backend.mkdir()
        (backend / "qmldir").write_text("module LogAnalyQt\n")
        module(backend / "Next", "LogAnalyQt.Next", source / "qml/Next")
        (backend / "SshTerminal.qml").symlink_to(source / "tests/qml-stage10/MockSshTerminal.qml")
        with (backend / "qmldir").open("a") as stream:
            stream.write("SshTerminal 1.0 SshTerminal.qml\n")
        icon_directory = imports / "assets"
        icon_directory.mkdir()
        (icon_directory / "logo.ico").symlink_to(source / "assets/logo.ico")
        environment = os.environ.copy()
        environment["QT_QUICK_CONTROLS_STYLE"] = "WorkshopStyle"
        environment["QML_DISABLE_DISK_CACHE"] = "1"
        for number in range(args.repeat):
            print(f"SSH QML iteration {number + 1}/{args.repeat}", flush=True)
            command = [runner, "-input", str(source / "tests/qml-stage10"), "-import", str(imports), "-import", str(source / "qml"), "-platform", "offscreen"]
            if args.filter:
                command.append(args.filter)
            result = run_logged(command, cwd=source, env=environment, log_name=f"qml-stage10-{number + 1}.log")
            warnings = [line for line in result.stdout.splitlines()
                        if any(level in line for level in ("QWARN", "QFATAL", "QCRITICAL"))
                        and "Populating font family aliases" not in line
                        and not line.endswith("This plugin does not support raise()")]
            if result.returncode or warnings:
                return result.returncode or 1
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
