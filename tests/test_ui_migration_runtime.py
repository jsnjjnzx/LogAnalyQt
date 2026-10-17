"""Run UI/backend checks from a disposable portable app root, never the user's data."""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


def main():
    binary = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="loganalyqt-ui-") as directory:
        runtime = pathlib.Path(directory)
        if binary.parent.name == "MacOS":
            bundle = binary.parents[2]
            shutil.copytree(bundle, runtime / bundle.name, symlinks=True)
            executable = runtime / bundle.name / "Contents" / "MacOS" / binary.name
        else:
            executable = runtime / binary.name
            shutil.copy2(binary, executable)
        environment = dict(os.environ, LOGANALYQT_TEST_HOME=str(runtime / "home"))
        data_root = runtime / "home/Library/Application Support/LogAnalyQt" if sys.platform == "darwin" else runtime
        log_path = runtime / "home/Library/Logs/LogAnalyQt/log.txt" if sys.platform == "darwin" else runtime / "log/log.txt"

        def run(*arguments):
            log_path.unlink(missing_ok=True)
            result = subprocess.run(
                [str(executable), "-platform", os.environ.get("QT_QPA_PLATFORM", "offscreen"), "--ui-smoke", *arguments],
                cwd=runtime, env=environment, capture_output=True, text=True, timeout=90,
            )
            log = log_path.read_text(encoding="utf-8") if log_path.exists() else ""
            errors = [line for line in log.splitlines()
                      if (" WARN " in line or " CRIT " in line or " FATAL " in line)
                      and "Populating font family aliases" not in line]
            if result.returncode or errors:
                diagnostics = [line for line in log.splitlines() if line in errors or "UI_CHECK_" in line]
                print(arguments, result.stdout, result.stderr, "\n".join(dict.fromkeys(diagnostics)), sep="\n")
                raise SystemExit(result.returncode or 1)

        run("--ui-checks")
        prompt_path = data_root / "prompts" / "ai_698_framing.md"
        assert prompt_path.is_file() and "DL/T 698" in prompt_path.read_text(encoding="utf-8"), "prompt not extracted to application data directory"
        prompt_path.write_text("User edited prompt survives app restart.\n", encoding="utf-8")
        (data_root / "LogAnalyQt.ini").unlink(missing_ok=True)
        for phase in ["write", "read", "empty", "corrupt-write", "corrupt-read", "write-failure"]:
            run("--workspace-phase", phase)
        assert prompt_path.read_text(encoding="utf-8") == "User edited prompt survives app restart.\n", "restart overwrote the user's prompt"
        if sys.platform == "darwin":
            assert not any((runtime / name).exists() for name in ("LogAnalyQt.ini", "protocol_xml", "prompts", "preset", "log")), "Mac app generated data beside its bundle"
        print("PASS: platform data directories, prompt extraction and restart preservation")
        print("PASS: migrated routes/parsers, 11 isolated tabs, close/add, restart restoration, empty workspace, corrupt-state backup and save-failure protection")


if __name__ == "__main__":
    main()
