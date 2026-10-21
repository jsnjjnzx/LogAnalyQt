"""Run UI/backend checks from a disposable portable app root, never the user's data."""
import os
import json
import pathlib
import plistlib
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
            info = plistlib.loads((bundle / "Contents/Info.plist").read_bytes())
            assert info["CFBundleName"] == info["CFBundleDisplayName"] == "通联工坊", "Mac app and Launchpad display names are incorrect"
            assert info["CFBundleIdentifier"] == "com.loganalyqt.LogAnalyQt", "rename must preserve the bundle identity"
            # Old debug bundles can contain real portable data; use only synthetic test data.
            portable_data = {"settings.json", "mqtt", "LogAnalyQt.ini", "protocol_xml", "prompts", "preset", "log"}
            shutil.copytree(bundle, runtime / bundle.name, symlinks=True,
                            ignore=lambda path, names: portable_data.intersection(names) if pathlib.Path(path) == binary.parent else set())
            executable = runtime / bundle.name / "Contents" / "MacOS" / binary.name
        else:
            executable = runtime / binary.name
            shutil.copy2(binary, executable)
        environment = dict(os.environ, LOGANALYQT_TEST_HOME=str(runtime / "home"), QML_DISABLE_DISK_CACHE="1")
        data_root = runtime / "home/Library/Application Support/LogAnalyQt" if sys.platform == "darwin" else runtime
        log_path = runtime / "home/Library/Logs/LogAnalyQt/log.txt" if sys.platform == "darwin" else runtime / "log/log.txt"
        data_root.mkdir(parents=True, exist_ok=True)
        (data_root / "LogAnalyQt.ini").write_text(
            "[ui]\nnext\\appearance\\dark=false\nnext\\appearance\\followSystem=true\n",
            encoding="utf-8",
        )

        def run(*arguments):
            log_path.unlink(missing_ok=True)
            result = subprocess.run(
                [str(executable), "-platform", os.environ.get("QT_QPA_PLATFORM", "offscreen"), "--ui-smoke", *arguments],
                cwd=runtime, env=environment, capture_output=True, text=True, timeout=90,
            )
            log = log_path.read_text(encoding="utf-8") if log_path.exists() else ""
            errors = [line for line in log.splitlines()
                      if (" WARN " in line or " CRIT " in line or " FATAL " in line)
                      and "Populating font family aliases" not in line
                      and not (os.environ.get("QT_QPA_PLATFORM", "offscreen") == "offscreen"
                               and line.endswith("This plugin does not support raise()"))]
            if "--mqtt-checks" in arguments and "MQTT_UI_CHECK_PASSED" not in log:
                errors.append("Packaged executable did not complete MQTT UI/backend checks")
            if result.returncode or errors:
                diagnostics = errors + [line for line in log.splitlines() if "UI_CHECK_" in line]
                print(arguments, result.stdout, result.stderr, "\n".join(dict.fromkeys(diagnostics)), sep="\n")
                raise SystemExit(result.returncode or 1)

        original = (data_root / "LogAnalyQt.ini").read_bytes()
        run("--migration-action", "cancel")
        assert (data_root / "LogAnalyQt.ini").read_bytes() == original and not (data_root / "settings.json").exists(), "cancel must not write or delete configuration"
        run("--migration-action", "confirm", "--ui-checks")
        assert not (data_root / "LogAnalyQt.ini").exists() and (data_root / "settings.json").exists(), "only confirmed, verified migration deletes old INI"
        def monitor_preferences():
            saved = json.loads((data_root / "settings.json").read_text())
            state = saved["ui"]["next"]["sshMonitor"]["workspace"]
            state = json.loads(state) if isinstance(state, str) else state
            return state["monitorRules"], [(entry["config"]["monitorEnabled"], entry["config"]["monitorRuleIds"]) for entry in state["sessions"]]
        monitor_before_restart = monitor_preferences()
        rules, selections = monitor_before_restart
        assert len(rules) == 3 and rules[0]["color"] == "#bb9cf5" and rules[0]["pauseOnMatch"] is True
        assert rules[2]["pauseOnMatch"] is False and rules[2]["color"] == "#7ed6a0", "mark-only action/color were not persisted"
        assert selections == [(True, [rules[0]["id"], rules[2]["id"]]), (True, [rules[1]["id"]])], "session rule selections were not persisted independently"
        conflict_home = runtime / "conflict-home"
        conflict_data = conflict_home / "Library/Application Support/LogAnalyQt" if sys.platform == "darwin" else runtime
        if sys.platform == "darwin":
            conflict_data.mkdir(parents=True)
            (conflict_data / "settings.json").write_text(json.dumps({"schemaVersion": 1, "parser": {"protocolType": 1}}))
            (conflict_data / "LogAnalyQt.ini").write_text("[General]\nprotocolType=2\n")
            conflict_run = subprocess.run([str(executable), "-platform", os.environ.get("QT_QPA_PLATFORM", "offscreen"), "--ui-smoke", "--migration-action", "retry-conflict"], cwd=runtime, env=dict(environment, LOGANALYQT_TEST_HOME=str(conflict_home)), capture_output=True, text=True, timeout=30)
            assert conflict_run.returncode == 0, conflict_run.stderr
            assert json.loads((conflict_data / "settings.json").read_text())["parser"]["protocolType"] == 2 and not (conflict_data / "LogAnalyQt.ini").exists(), "unresolved conflict must allow selection and retry through the same confirmation dialog"
        run("--mqtt-checks", "--size", "1200", "800")
        assert monitor_preferences() == monitor_before_restart, "application restart overwrote monitor rules, colors, actions or selections"
        prompt_path = data_root / "prompts" / "ai_698_framing.md"
        assert prompt_path.is_file() and "DL/T 698" in prompt_path.read_text(encoding="utf-8"), "prompt not extracted to application data directory"
        prompt_path.write_text("User edited prompt survives app restart.\n", encoding="utf-8")
        (data_root / "settings.json").unlink(missing_ok=True)
        for phase in ["write", "read", "empty", "write-failure"]:
            run("--workspace-phase", phase)
        assert prompt_path.read_text(encoding="utf-8") == "User edited prompt survives app restart.\n", "restart overwrote the user's prompt"
        if sys.platform == "darwin":
            assert not any((runtime / name).exists() for name in ("settings.json", "device-state.ini", "mqtt", "LogAnalyQt.ini", "protocol_xml", "prompts", "preset", "log")), "Mac app generated data beside its bundle"
        print("PASS: platform data directories, prompt extraction and restart preservation")
        print("PASS: shared monitor rules, rule colors/actions and independent session selections persist across application restart")
        print("PASS: packaged MQTT broker, loopback publish/subscribe, V2 layout, editor controls and helper cleanup")
        print("PASS: migrated routes/parsers, 11 isolated tabs, close/add, restart restoration, empty workspace, corrupt-state backup and save-failure protection")


if __name__ == "__main__":
    main()
