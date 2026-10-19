"""Package a local, isolated macOS UI preview without touching installed apps."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[2]


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def seed_demo(root):
    (root / "mqtt").mkdir(parents=True)
    (root / "protocol_xml").mkdir()
    (root / "preset/smart_framing").mkdir(parents=True)
    workspace = {"version": 1, "tabs": [{"text": "", "parsedText": "", "protocol": 0, "parsedProtocol": 0, "encoding": 0, "parsedEncoding": 0, "locked": False, "r1": 0.3, "r2": 0.2}], "currentIndex": 0, "open": False}
    encoded = json.dumps(json.dumps(workspace, separators=(",", ":")))
    (root / "LogAnalyQt.ini").write_text("[General]\nprotocolType=2\n[ui]\nnext\\window\\width=1400\nnext\\window\\height=900\nnext\\multiParse\\workspace=" + encoded + "\n", encoding="utf-8")
    profile = {"id": str(uuid.uuid4()), "name": "演示连接（合成数据）", "host": "127.0.0.1", "port": 1883, "clientId": "loganalyqt_config_preview", "password": "", "subscriptions": [{"topic": "preview/devices/#", "qos": 1, "enabled": True, "visible": True, "color": "#60a5fa"}]}
    (root / "mqtt/connections.json").write_text(json.dumps({"schema": 1, "connections": [profile], "selectedConnectionId": profile["id"], "splitRatio": 0.6}, ensure_ascii=False, indent=2))
    (root / "mqtt/common-subscriptions.json").write_text(json.dumps({"schema": 1, "subscriptions": [{"topic": "preview/status/+", "qos": 0, "color": "#34d399"}]}, indent=2))
    (root / "protocol_xml/preset_messages.xml").write_text('<presets version="v1.0.0" date="2026-10-18"><message protocol="698" summary="示例数据（非业务报文）"><content>6816</content></message></presets>\n', encoding="utf-8")
    (root / "preset/smart_framing/演示模板.json").write_text('{"protocol":"698","tree":{}}\n', encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit("输出目录已存在，拒绝覆盖：" + str(args.output))
    cache = (args.build / "CMakeCache.txt").read_text()
    if "LOGANALYQT_BUILD_PROTOCOL_TESTS:BOOL=ON" not in cache:
        raise SystemExit("预览包必须来自启用隔离测试模式的构建，拒绝使用正式构建")
    source = args.build / "appLogAnalyQt.app"
    assert source.is_dir()
    with tempfile.TemporaryDirectory(prefix="loganalyqt-preview-package-") as temporary:
        bundle = Path(temporary) / "通联工坊 配置测试版.app"
        shutil.copytree(source, bundle, symlinks=True)
        run("macdeployqt", bundle, "-qmldir=" + str(ROOT / "qml"), "-always-overwrite", "-verbose=1", "-no-codesign")
        for name in ("libqsqlodbc.dylib", "libqsqlpsql.dylib", "libqsqlmimer.dylib"):
            (bundle / "Contents/PlugIns/sqldrivers" / name).unlink(missing_ok=True)
        run(sys.executable, ROOT / "scripts/fix_macos_bundle_ids.py", bundle)
        demo = bundle / "Contents/Resources/DemoData"
        seed_demo(demo)
        launcher = bundle / "Contents/MacOS/ConfigPreview"
        run("xcrun", "clang++", "-std=c++17", "-arch", "arm64", "-fobjc-arc", "-framework", "AppKit", ROOT / "scripts/testing/ConfigPreviewLauncher.mm", "-o", launcher)
        info_path = bundle / "Contents/Info.plist"
        info = plistlib.loads(info_path.read_bytes())
        info.update(CFBundleExecutable="ConfigPreview", CFBundleName="通联工坊 配置测试版", CFBundleDisplayName="通联工坊 配置测试版", CFBundleIdentifier="com.loganalyqt.configpreview", CFBundleGetInfoString="个人配置管理本地测试版；默认使用独立演示数据")
        info_path.chmod(0o644)
        info_path.write_bytes(plistlib.dumps(info))
        run("codesign", "--force", "--deep", "--sign", "-", bundle)
        run("codesign", "--verify", "--deep", "--strict", "--verbose=2", bundle)
        sys.path.insert(0, str(ROOT / "scripts/ci"))
        from check_build import check_macos_bundle
        check_macos_bundle(bundle)
        description = json.loads(subprocess.check_output([str(launcher), "--preview-info"], text=True))
        assert "LogAnalyQt Config Preview" in description["dataRoot"] and description["automaticUpdates"] is False
        assert description["credentialNamespace"] == "LogAnalyQt.ConfigPreview"
        args.output.mkdir(parents=True)
        delivered = args.output / bundle.name
        shutil.copytree(bundle, delivered, symlinks=True)
        run("codesign", "--verify", "--deep", "--strict", delivered)
    archive = args.output / "通联工坊-config-macOS-arm64-test.zip"
    run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", delivered, archive)
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    (args.output / (archive.name + ".sha256")).write_text(checksum + "  " + archive.name + "\n")
    (args.output / "使用说明.txt").write_text("通联工坊 macOS 配置测试版（Apple Silicon）\n\n双击“通联工坊 配置测试版.app”即可。无需拖到应用程序目录。\n\n首次启动显示旧配置迁移确认框，示例数据为合成数据。\n点击“迁移并继续”后，打开“系统设置 → 个人配置”查看迁移结果，并试用备份恢复。\n取消则退出，演示旧配置保持不变。以后再次打开会保留上一次测试状态。\n\n测试数据独立保存在：~/Library/Application Support/LogAnalyQt Config Preview/\n测试凭据使用独立的 LogAnalyQt.ConfigPreview 服务域。\n测试版关闭自动检查更新，不替换现有安装版，不迁移实际个人配置。\n\n如需再次体验首次迁移弹窗：先退出测试版，将上述独立测试目录重命名保留，再重新打开测试版。\n\n本包只做本地 ad-hoc 签名，未公证；没有发布到任何服务。\n", encoding="utf-8")
    (args.output / "package-checks.json").write_text(json.dumps({"architecture": "arm64", "signature": "ad-hoc", "notarized": False, "published": False, "installed_app_replaced": False, "default_data_root": description["dataRoot"], "credential_namespace": description["credentialNamespace"], "zip_sha256": checksum}, ensure_ascii=False, indent=2) + "\n")
    print("Delivered:", delivered)
    print("Archive:", archive)


if __name__ == "__main__":
    main()
