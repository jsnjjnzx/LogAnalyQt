"""Run actual legacy/Next framing QML with temporary import metadata, without an app build."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def module(directory, name, source):
    directory.mkdir(parents=True)
    declarations = [f"module {name}"]
    for file in sorted(source.glob("*.qml")):
        if file.stem == "Main":
            continue
        (directory / file.name).symlink_to(file)
        singleton = "singleton " if file.stem in {"Icons", "PlatformFonts"} else ""
        declarations.append(f"{singleton}{file.stem} 1.0 {file.name}")
    if (source / "SmartFraming").is_dir():
        (directory / "SmartFraming").symlink_to(source / "SmartFraming", target_is_directory=True)
        for file in sorted((source / "SmartFraming").glob("*.qml")):
            declarations.append(f"{file.stem} 1.0 SmartFraming/{file.name}")
    (directory / "qmldir").write_text("\n".join(declarations) + "\n")


def main():
    source = Path(__file__).resolve().parents[1]
    runner = sys.argv[1] if len(sys.argv) > 1 else shutil.which("qmltestrunner")
    if not runner:
        fallback = Path("/opt/homebrew/opt/qtdeclarative/bin/qmltestrunner")
        if fallback.is_file():
            runner = str(fallback)
    if not runner:
        raise SystemExit("qmltestrunner unavailable; provide its path as the first argument")
    with tempfile.TemporaryDirectory(prefix="prot698-qml-imports-") as temporary:
        imports = Path(temporary)
        module(imports / "LogAnalyQt", "LogAnalyQt", source / "qml")
        module(imports / "LogAnalyQt" / "Next", "LogAnalyQt.Next", source / "qml" / "Next")
        environment = os.environ.copy()
        environment["QT_QUICK_CONTROLS_STYLE"] = "Basic"
        result = subprocess.run([
            runner, "-input", str(source / "tests/qml-next"),
            "-import", str(imports), "-platform", "offscreen",
        ], env=environment, cwd=source, timeout=90, check=False, capture_output=True, text=True)
        output = result.stdout + result.stderr
        print(output, end="")
        warnings = [line for line in output.splitlines()
                    if any(level in line for level in ("QWARN", "QFATAL", "QCRITICAL"))
                    and "Populating font family aliases" not in line]
        return result.returncode or (1 if warnings else 0)


if __name__ == "__main__":
    raise SystemExit(main())
