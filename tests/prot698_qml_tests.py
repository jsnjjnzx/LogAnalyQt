"""Run actual Next framing QML with temporary import metadata, without an app build."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading


def module(directory, name, source):
    directory.mkdir(parents=True)
    declarations = [f"module {name}"]
    for file in sorted(source.glob("*.qml")):
        if file.stem == "Main":
            continue
        shutil.copy2(file, directory / file.name)
        singleton = "singleton " if file.stem in {"Icons", "PlatformFonts"} else ""
        declarations.append(f"{singleton}{file.stem} 1.0 {file.name}")
    if (source / "SmartFraming").is_dir():
        shutil.copytree(source / "SmartFraming", directory / "SmartFraming")
        for file in sorted((source / "SmartFraming").glob("*.qml")):
            declarations.append(f"{file.stem} 1.0 SmartFraming/{file.name}")
    (directory / "qmldir").write_text("\n".join(declarations) + "\n")


def run_logged(command, *, cwd, env, log_name, timeout=90):
    """Stream output while retaining the same full text for warning checks and CI evidence."""
    lines = []
    with subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True) as process:
        def pump():
            for line in process.stdout:
                lines.append(line)
                print(line, end="", flush=True)
        reader = threading.Thread(target=pump, daemon=True)
        reader.start()
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise
        finally:
            reader.join()
            evidence = env.get("LOGANALYQT_DIAGNOSTICS_DIR")
            if evidence:
                destination = Path(evidence)
                destination.mkdir(parents=True, exist_ok=True)
                (destination / log_name).write_text("".join(lines), encoding="utf-8")
    return subprocess.CompletedProcess(command, code, stdout="".join(lines), stderr="")


def arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("runner", nargs="?")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--filter", default="")
    args = parser.parse_args()
    if not 1 <= args.repeat <= 20:
        parser.error("--repeat must be between 1 and 20")
    return args


def main():
    source = Path(__file__).resolve().parents[1]
    args = arguments()
    runner = args.runner or shutil.which("qmltestrunner")
    if not runner:
        fallback = Path("/opt/homebrew/opt/qtdeclarative/bin/qmltestrunner")
        if fallback.is_file():
            runner = str(fallback)
    if not runner:
        raise SystemExit("qmltestrunner unavailable; provide its path as the first argument")
    print(f"QML test runner: {runner}", flush=True)
    with tempfile.TemporaryDirectory(prefix="prot698-qml-imports-") as temporary:
        imports = Path(temporary)
        backend = imports / "LogAnalyQt"
        backend.mkdir()
        shutil.copy2(source / "tests/qml-stage10/MockSshTerminal.qml", backend / "SshTerminal.qml")
        (backend / "qmldir").write_text("module LogAnalyQt\nSshTerminal 1.0 SshTerminal.qml\n")
        module(imports / "LogAnalyQt" / "Next", "LogAnalyQt.Next", source / "qml" / "Next")
        environment = os.environ.copy()
        environment["QT_QUICK_CONTROLS_STYLE"] = "WorkshopStyle"
        for number in range(args.repeat):
            print(f"QML iteration {number + 1}/{args.repeat}", flush=True)
            command = [runner, "-input", str(source / "tests/qml-next"), "-import", str(imports), "-import", str(source / "qml"),
                       "-platform", environment.get("QT_QPA_PLATFORM", "offscreen")]
            if args.filter:
                command.append(args.filter)
            result = run_logged(command, env=environment, cwd=source, log_name=f"qml-next-{number + 1}.log")
            warnings = [line for line in result.stdout.splitlines()
                        if any(level in line for level in ("QWARN", "QFATAL", "QCRITICAL"))
                        and "Populating font family aliases" not in line]
            if result.returncode or warnings:
                return result.returncode or 1
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
