#!/bin/zsh

set -euo pipefail

SCRIPT_DIR=${0:A:h}
ROOT_DIR=${SCRIPT_DIR:h:h}
cd "$ROOT_DIR"

RELEASE_TAG=""
if [[ "${1:-}" == "--release" ]]; then
  RELEASE_TAG=${2:-}
  if [[ ! "$RELEASE_TAG" =~ '^v[0-9]+\.[0-9]+\.[0-9]+$' ]]; then
    print -u2 "发布标签必须使用 vX.Y.Z 格式：$RELEASE_TAG"
    exit 1
  fi
elif [[ $# -ne 0 ]]; then
  print -u2 "用法：$0 [--release vX.Y.Z]"
  exit 1
fi

python3 -m unittest tests.test_release_automation -v

VERSION=$(sed -nE 's/^project\(LogAnalyQt VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)
if [[ -n "$RELEASE_TAG" ]]; then
  TAG_VERSION=${RELEASE_TAG#v}
  if [[ "$TAG_VERSION" != "$VERSION" ]]; then
    print -u2 "标签版本 $TAG_VERSION 与 CMake 项目版本 $VERSION 不一致"
    exit 1
  fi
  NOTES_FILE="$ROOT_DIR/release_package/release-notes.md"
  python3 scripts/release/generate_release_notes.py \
    --tag "$RELEASE_TAG" --output "$NOTES_FILE"
  REQUIRE_MACOS_DISTRIBUTION=1 RUN_TESTS=1 \
    scripts/package_macos.sh "$VERSION"
else
  RUN_TESTS=1 scripts/package_macos.sh "$VERSION"
  print "macOS CI build completed without publishing a Release."
  exit 0
fi

ARCH=$(uname -m)
DMG="$ROOT_DIR/release_package/TonglianWorkshop_v${VERSION}_macOS_${ARCH}.dmg"
UPDATE_ZIP="$ROOT_DIR/release_package/TonglianWorkshop_v${VERSION}_macOS_${ARCH}_update.zip"

python3 scripts/release/gitee_release.py upload --tag "$RELEASE_TAG" --file "$DMG"
python3 scripts/release/gitee_release.py upload --tag "$RELEASE_TAG" --file "$DMG.sha256"
python3 scripts/release/gitee_release.py upload --tag "$RELEASE_TAG" --file "$UPDATE_ZIP"
python3 scripts/release/gitee_release.py upload --tag "$RELEASE_TAG" --file "$UPDATE_ZIP.sha256"

python3 scripts/release/gitee_release.py finalize \
  --tag "$RELEASE_TAG" \
  --expect "TonglianWorkshop_v${VERSION}_Windows_x64.exe" \
  --expect "TonglianWorkshop_v${VERSION}_Windows_x64.exe.sha256" \
  --expect "TonglianWorkshop_v${VERSION}_macOS_${ARCH}.dmg" \
  --expect "TonglianWorkshop_v${VERSION}_macOS_${ARCH}.dmg.sha256" \
  --expect "TonglianWorkshop_v${VERSION}_macOS_${ARCH}_update.zip" \
  --expect "TonglianWorkshop_v${VERSION}_macOS_${ARCH}_update.zip.sha256"

print "macOS assets uploaded and Gitee Release finalized: $RELEASE_TAG"
