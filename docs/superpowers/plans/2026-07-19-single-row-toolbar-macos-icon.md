# Single-Row Toolbar and macOS Icon Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Keep the complete top toolbar on one line with a content-derived minimum window width, and give the packaged macOS application the existing project icon.

**Architecture:** Use one `RowLayout` for every visible toolbar control. Let the path field consume surplus width but stop at 80 logical pixels, then bind `Window.minimumWidth` to the row's complete implicit width. Package a generated `logo.icns` only on Apple platforms while retaining the existing Windows `.ico` resource path.

**Tech Stack:** Qt 6.11, QML `RowLayout`, CMake, macOS `sips`, `iconutil`, `macdeployqt`, `codesign`.

## Global Constraints

- Keep every currently visible toolbar control on one row.
- Do not truncate button labels or icons.
- The path field shrinks first and stops at 80 logical pixels.
- The window cannot shrink below the width required to show the full row.
- Reuse `assets/logo.ico`; do not redesign the logo.
- Do not change the Windows icon or updater build paths.
- Do not commit because the working tree already contains unrelated uncommitted work.

---

### Task 1: Restore the single-row toolbar

**Files:**
- Modify: `qml/Main.qml`

**Interfaces:**
- Consumes: existing `StyledButton`, `StyledComboBox`, `theme`, and `backend` QML objects.
- Produces: `folderLayout.implicitWidth` as the source for `mainWindow.minimumWidth`.

- [ ] **Step 1: Run the failing structure check**

```bash
rg -n 'ColumnLayout \{|id: actionFlow|Flow \{' qml/Main.qml
```

Expected: the current two-row `ColumnLayout` and `Flow` are found.

- [ ] **Step 2: Replace the two-row structure with one `RowLayout`**

Use one layout with these sizing rules:

```qml
RowLayout {
    id: folderLayout
    anchors.fill: parent
    anchors.topMargin: 25
    anchors.leftMargin: 10
    anchors.rightMargin: 10
    anchors.bottomMargin: 10
    spacing: 10

    StyledButton { /* existing folder button */ }

    TextField {
        id: folderPathField
        Layout.fillWidth: true
        Layout.minimumWidth: 80
        Layout.preferredWidth: 80
        // Preserve the existing text, colors, read-only state, and background.
    }

    // Preserve every existing label, action button, combo box, shortcut button,
    // timer, and UpdateChecker in their current order and with current handlers.
}
```

- [ ] **Step 3: Bind and enforce the content-derived minimum width**

Add at the root `Window`:

```qml
minimumWidth: Math.max(800, folderLayout.implicitWidth + 40)
```

Replace all four resize checks:

```qml
if (newWidth >= mainWindow.minimumWidth) {
```

- [ ] **Step 4: Verify the QML structure and build**

```bash
test "$(rg -c 'id: folderLayout' qml/Main.qml)" -eq 1
test "$(rg -c 'id: actionFlow' qml/Main.qml)" -eq 0
rg -n 'Layout.minimumWidth: 80|minimumWidth: Math.max\(800, folderLayout.implicitWidth \+ 40\)' qml/Main.qml
cmake --build --preset macos-debug
git diff --check
```

Expected: all checks and the Debug build exit with status 0.

---

### Task 2: Add the macOS application icon

**Files:**
- Create: `assets/logo.icns`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `assets/logo.ico` at 256 by 256 pixels.
- Produces: `Contents/Resources/logo.icns` and `CFBundleIconFile=logo.icns` in macOS bundles.

- [ ] **Step 1: Run the failing icon checks**

```bash
test -f assets/logo.icns
plutil -extract CFBundleIconFile raw release_package/LogAnalyQt.app/Contents/Info.plist | rg '^logo\.icns$'
```

Expected: the source icon is missing and the bundle value is empty.

- [ ] **Step 2: Generate the standard icon set and `.icns` file**

Create PNG files named `icon_16x16.png`, `icon_16x16@2x.png`, `icon_32x32.png`, `icon_32x32@2x.png`, `icon_128x128.png`, `icon_128x128@2x.png`, `icon_256x256.png`, `icon_256x256@2x.png`, `icon_512x512.png`, and `icon_512x512@2x.png` with `sips`, then run:

```bash
iconutil -c icns /tmp/LogAnalyQt-20260719.iconset -o assets/logo.icns
```

- [ ] **Step 3: Register the icon only for Apple bundles**

Add to the existing Apple platform block in `CMakeLists.txt`:

```cmake
set_source_files_properties(assets/logo.icns PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
)
target_sources(appLogAnalyQt PRIVATE assets/logo.icns)
set_target_properties(appLogAnalyQt PROPERTIES
    MACOSX_BUNDLE_ICON_FILE "logo.icns"
)
```

- [ ] **Step 4: Verify the source asset and configure**

```bash
file assets/logo.icns
cmake --preset macos-release
cmake --build --preset macos-release
```

Expected: `file` reports an Apple Icon Image and the Release build exits 0.

---

### Task 3: Repackage and verify the deliverables

**Files:**
- Replace generated artifact: `release_package/LogAnalyQt.app`
- Replace generated artifact: `release_package/LogAnalyQt_v3.0.1_macOS_arm64.zip`
- Replace generated artifact: `release_package/LogAnalyQt_v3.0.1_macOS_arm64.zip.sha256`

**Interfaces:**
- Consumes: signed app from `build/macos-release/package/LogAnalyQt.app`.
- Produces: a double-clickable app and update archive in `release_package`.

- [ ] **Step 1: Package and copy the app**

```bash
./scripts/package_macos.sh
/usr/bin/ditto build/macos-release/package/LogAnalyQt.app release_package/LogAnalyQt.app
```

- [ ] **Step 2: Verify layout source, icon metadata, signature, and archive**

```bash
set -e
test "$(rg -c 'id: actionFlow' qml/Main.qml)" -eq 0
test -f release_package/LogAnalyQt.app/Contents/Resources/logo.icns
test "$(plutil -extract CFBundleIconFile raw release_package/LogAnalyQt.app/Contents/Info.plist)" = "logo.icns"
codesign --verify --deep --strict release_package/LogAnalyQt.app
(cd release_package && shasum -a 256 -c LogAnalyQt_v3.0.1_macOS_arm64.zip.sha256)
git diff --check
```

Expected: every command exits 0.

- [ ] **Step 3: Verify double-click-equivalent startup**

```bash
open -n release_package/LogAnalyQt.app
ps -ax -o pid=,etime=,command= | rg '[r]elease_package/LogAnalyQt.app/Contents/MacOS/appLogAnalyQt'
```

Expected: the release-directory process remains present after startup.
