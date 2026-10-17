#!/bin/zsh

set -euo pipefail

SCRIPT_DIR=${0:A:h}
ROOT_DIR=${SCRIPT_DIR:h}
cd "$ROOT_DIR"

VERSION=${1:-$(sed -nE 's/^project\(LogAnalyQt VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)}
if [[ ! "$VERSION" =~ '^[0-9]+\.[0-9]+\.[0-9]+$' ]]; then
  print -u2 "版本号必须使用 X.Y.Z 格式：$VERSION"
  exit 1
fi

ARCH=$(uname -m)
BUILD_DIR="$ROOT_DIR/build/macos-release"
BUILD_APP="$BUILD_DIR/appLogAnalyQt.app"
PACKAGE_DIR="$BUILD_DIR/package"
APP_BUNDLE="$PACKAGE_DIR/LogAnalyQt.app"
DMG_ROOT="$BUILD_DIR/dmg-root"
RELEASE_DIR="$ROOT_DIR/release_package"
OUTPUT_UPDATE_NAME="LogAnalyQt_v${VERSION}_macOS_${ARCH}_update.zip"
OUTPUT_DMG_NAME="LogAnalyQt_v${VERSION}_macOS_${ARCH}.dmg"
OUTPUT_UPDATE_ZIP="$RELEASE_DIR/$OUTPUT_UPDATE_NAME"
OUTPUT_DMG="$RELEASE_DIR/$OUTPUT_DMG_NAME"
IDENTITY=${MACOS_CODESIGN_IDENTITY:--}
MACDEPLOYQT=${MACDEPLOYQT:-$(command -v macdeployqt || true)}
REQUIRE_DISTRIBUTION=${REQUIRE_MACOS_DISTRIBUTION:-0}

if [[ "$REQUIRE_DISTRIBUTION" == "1" ]]; then
  if [[ "$IDENTITY" == "-" ]]; then
    print -u2 "正式发布必须设置 MACOS_CODESIGN_IDENTITY=Developer ID Application: ..."
    exit 1
  fi
  if [[ -z "${MACOS_NOTARY_PROFILE:-}" ]]; then
    print -u2 "正式发布必须设置 MACOS_NOTARY_PROFILE"
    exit 1
  fi
fi

rm -rf "$BUILD_APP" "$PACKAGE_DIR" "$DMG_ROOT"
TESTS_OPTION=OFF
if [[ "${RUN_TESTS:-0}" == "1" ]]; then
  TESTS_OPTION=ON
fi
cmake --preset macos-release \
  -DCMAKE_PREFIX_PATH="${QT_ROOT_DIR:-/opt/homebrew/opt/qtbase}" \
  -DAPP_VERSION_STR="$VERSION" \
  -DLOGANALYQT_BUILD_PROTOCOL_TESTS="$TESTS_OPTION"
cmake --build --preset macos-release

if [[ "$TESTS_OPTION" == "ON" ]]; then
  ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

mkdir -p "$PACKAGE_DIR"
/usr/bin/ditto "$BUILD_APP" "$APP_BUNDLE"

if [[ ! -x "$MACDEPLOYQT" ]]; then
  print -u2 "未找到 macdeployqt: $MACDEPLOYQT"
  exit 1
fi

"$MACDEPLOYQT" "$APP_BUNDLE" \
  -qmldir="$ROOT_DIR/qml" \
  -always-overwrite \
  -verbose=0 \
  -no-codesign >/dev/null 2>&1

# Homebrew Qt 的 macdeployqt 可能把部署工作交给子进程；等待其释放目录。
sleep 1
while lsof -c macdeploy 2>/dev/null | grep -F "$ROOT_DIR" >/dev/null; do
  sleep 1
done

# 应用不使用这些数据库驱动，它们依赖构建机上未随包部署的数据库库。
rm -f "$APP_BUNDLE/Contents/PlugIns/sqldrivers/libqsqlodbc.dylib" \
  "$APP_BUNDLE/Contents/PlugIns/sqldrivers/libqsqlpsql.dylib" \
  "$APP_BUNDLE/Contents/PlugIns/sqldrivers/libqsqlmimer.dylib"

if [[ "$IDENTITY" == "-" ]]; then
  codesign --force --deep --sign - "$APP_BUNDLE"
else
  codesign --force --deep --options runtime --timestamp \
    --sign "$IDENTITY" "$APP_BUNDLE"
fi
codesign --verify --deep --strict --verbose=2 "$APP_BUNDLE"

mkdir -p "$RELEASE_DIR"
rm -f "$OUTPUT_UPDATE_ZIP" "$OUTPUT_UPDATE_ZIP.sha256" \
  "$OUTPUT_DMG" "$OUTPUT_DMG.sha256"

# ZIP 不能直接附加公证票据：先提交临时 ZIP，再把票据附加到 .app 后重新打包。
if [[ -n "${MACOS_NOTARY_PROFILE:-}" ]]; then
  NOTARY_ZIP="$RELEASE_DIR/.${OUTPUT_UPDATE_NAME}.notary.zip"
  rm -f "$NOTARY_ZIP"
  /usr/bin/ditto -c -k --sequesterRsrc --keepParent \
    "$APP_BUNDLE" "$NOTARY_ZIP"
  xcrun notarytool submit "$NOTARY_ZIP" \
    --keychain-profile "$MACOS_NOTARY_PROFILE" --wait
  xcrun stapler staple "$APP_BUNDLE"
  rm -f "$NOTARY_ZIP"
fi

/usr/bin/ditto -c -k --sequesterRsrc --keepParent \
  "$APP_BUNDLE" "$OUTPUT_UPDATE_ZIP"

# DMG 面向首次安装，包含应用和指向系统 Applications 目录的快捷方式。
mkdir -p "$DMG_ROOT"
/usr/bin/ditto "$APP_BUNDLE" "$DMG_ROOT/LogAnalyQt.app"
ln -s /Applications "$DMG_ROOT/Applications"
hdiutil create \
  -volname "LogAnalyQt $VERSION" \
  -srcfolder "$DMG_ROOT" \
  -ov -format UDZO \
  "$OUTPUT_DMG"

if [[ "$IDENTITY" != "-" ]]; then
  codesign --force --timestamp --sign "$IDENTITY" "$OUTPUT_DMG"
  codesign --verify --verbose=2 "$OUTPUT_DMG"
fi

if [[ -n "${MACOS_NOTARY_PROFILE:-}" ]]; then
  xcrun notarytool submit "$OUTPUT_DMG" \
    --keychain-profile "$MACOS_NOTARY_PROFILE" --wait
  xcrun stapler staple "$OUTPUT_DMG"
  xcrun stapler validate "$OUTPUT_DMG"
fi

UPDATE_HASH=$(shasum -a 256 "$OUTPUT_UPDATE_ZIP" | awk '{print $1}')
DMG_HASH=$(shasum -a 256 "$OUTPUT_DMG" | awk '{print $1}')
print "$UPDATE_HASH  $OUTPUT_UPDATE_NAME" > "$OUTPUT_UPDATE_ZIP.sha256"
print "$DMG_HASH  $OUTPUT_DMG_NAME" > "$OUTPUT_DMG.sha256"

print "macOS 发布包已生成："
print "  $OUTPUT_DMG"
print "  $OUTPUT_DMG.sha256"
print "  $OUTPUT_UPDATE_ZIP"
print "  $OUTPUT_UPDATE_ZIP.sha256"
