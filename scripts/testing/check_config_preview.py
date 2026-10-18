"""Relocation and real-window checks of the delivered preview; synthetic data only."""
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile


def fingerprint(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    bundle = Path(sys.argv[1]).resolve()
    evidence = Path(sys.argv[2]).resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    info = plistlib.loads((bundle / "Contents/Info.plist").read_bytes())
    assert info["CFBundleIdentifier"] == "com.loganalyqt.configpreview"
    assert info["CFBundleExecutable"] == "ConfigPreview"
    with tempfile.TemporaryDirectory(prefix="loganalyqt-preview-relocation-") as temporary:
        root = Path(temporary)
        relocated = root / "移动目录" / bundle.name
        shutil.copytree(bundle, relocated, symlinks=True)
        subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(relocated)], check=True)
        launcher = relocated / "Contents/MacOS/ConfigPreview"
        environment = {key: value for key, value in os.environ.items() if not key.startswith(("DYLD_", "QT_", "QML"))}
        environment.update(PATH="/usr/bin:/bin:/usr/sbin:/sbin", LOGANALYQT_PREVIEW_ROOT=str(root / "profile"))
        data = root / "profile/home/Library/Application Support/LogAnalyQt"
        sample = relocated / "Contents/Resources/DemoData"
        expected = {str(path.relative_to(sample)): fingerprint(path) for path in sample.rglob("*") if path.is_file()}
        captures = [("cancel", "migration-window.png"), ("confirm", "configuration-page.png"), (None, "restart-page.png")]
        for action, name in captures:
            args = [str(launcher), "--package-smoke"]
            if action:
                args += ["--migration-action", action]
            if action == "cancel":
                args += ["--migration-screenshot", str(evidence / name)]
            else:
                args += ["--personal-config", "--screenshot", str(evidence / name)]
            result = subprocess.run(args, cwd=root, env=environment, capture_output=True, text=True, timeout=35)
            assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
            assert (evidence / name).stat().st_size > 20000, "real window capture is empty"
            if action == "cancel":
                assert not (data / "settings.json").exists()
                assert all(fingerprint(data / name) == checksum for name, checksum in expected.items())
            else:
                assert (data / "settings.json").is_file()
                assert not any((data / name).exists() for name in expected)
                config = json.loads((data / "settings.json").read_text())
                assert config["schemaVersion"] == 1
                assert len(config["mqtt"]["connections"]) == 1 and len(config["mqtt"]["commonSubscriptions"]) == 1
                assert len(config["messages"]["presets"]) == 1 and len(config["framingTemplates"]) == 1
                assert len(config["workspace"]["parse"]["tabs"]) == 1
            assert all(fingerprint(sample / name) == checksum for name, checksum in expected.items())
        (evidence / "relocation-checks.json").write_text(json.dumps({"relocated_bundle_launch": True, "development_environment_removed": True, "cancel_preserves_all_legacy_samples": True, "confirmed_migration_verified": True, "restart_preserves_configuration": True, "bundled_samples_unchanged": True, "sample_config_files": len(expected)}, indent=2) + "\n")
    print("PASS: relocated preview, real confirmation/cancel, verified migration, configuration page and restart")


if __name__ == "__main__":
    main()
