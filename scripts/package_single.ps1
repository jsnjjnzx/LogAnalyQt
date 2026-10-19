param(
    [string]$Version = "",
    [string]$QtDir = $env:QT_STATIC_DIR,
    [string]$MingwDir = $env:MINGW_DIR,
    [string]$OpenSSLDir = $env:LOGANALYQT_MQTT_OPENSSL_DIR,
    [string]$BuildDir = "build_release",
    [string]$ReleaseDir = "release_package",
    [switch]$BuildTests
)

$ErrorActionPreference = "Stop"

# 设置控制台编码为 UTF-8，确保中文正确显示
chcp 65001 > $null
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

# Configuration
$RootDir = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $RootDir  # 切换到根目录，确保后续操作路径一致

if ([string]::IsNullOrWhiteSpace($QtDir)) {
    $QtDir = "D:/DevTool/QtStaticLib"
}
if ([string]::IsNullOrWhiteSpace($MingwDir)) {
    $MingwDir = "C:/Qt/Tools/mingw1120_64"
}
if (-not (Test-Path "$QtDir/lib/libQt6Core.a")) {
    throw "Static Qt library missing: $QtDir/lib/libQt6Core.a"
}
if (-not (Test-Path "$MingwDir/bin/g++.exe")) {
    throw "MinGW compiler missing: $MingwDir/bin/g++.exe"
}
$AppName = "TonglianWorkshop"
$OriginalAppName = "appLogAnalyQt"
$MaxThreads = 8 # 最大编译线程数

# 未显式传入版本时，以 CMake 项目版本为唯一来源。
if ([string]::IsNullOrWhiteSpace($Version)) {
    $CMakeContent = Get-Content "CMakeLists.txt" -Raw
    if ($CMakeContent -match 'project\s*\(\s*LogAnalyQt\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
        $Version = $matches[1]
    }
    else {
        throw "Unable to read LogAnalyQt version from CMakeLists.txt"
    }
}
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
    throw "Version must use X.Y.Z format: $Version"
}

# Add MinGW to PATH
$env:PATH = "$MingwDir/bin;$env:PATH"

if ([string]::IsNullOrWhiteSpace($OpenSSLDir)) {
    $OpenSSLDir = Join-Path $RootDir "build/mqtt-openssl-3.5.9"
}
& "$RootDir/scripts/ci/build_mqtt_openssl.ps1" -InstallDir $OpenSSLDir -MingwDir $MingwDir
if ($LASTEXITCODE -ne 0) { throw "MQTT OpenSSL preparation failed" }

Write-Host "Starting static packaging process for $AppName v$Version..."
$TestsOption = if ($BuildTests) { "ON" } else { "OFF" }

# Clean Build Directory
if (Test-Path $BuildDir) {
    Write-Host "Cleaning build directory..."
    Remove-Item -Recurse -Force $BuildDir
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

# Configure
Write-Host "Configuring CMake (Release) with Static Qt..."
# 使用 CMakeLists.txt 中新定义的 QT_STATIC_DIR 变量
cmake -S "$RootDir" -B $BuildDir -G "MinGW Makefiles" `
    -DCMAKE_BUILD_TYPE=Release `
    -DQT_STATIC_DIR="$QtDir" `
    -DOPENSSL_ROOT_DIR="$OpenSSLDir" `
    -DAPP_VERSION_STR="$Version" `
    -DLOGANALYQT_BUILD_PROTOCOL_TESTS="$TestsOption" `
    -DCMAKE_CXX_COMPILER="$MingwDir/bin/g++.exe" `
    -DCMAKE_C_COMPILER="$MingwDir/bin/gcc.exe"

if ($LASTEXITCODE -ne 0) { Write-Error "Configuration failed"; exit 1 }

# Build
Write-Host "Building project with $MaxThreads threads..."
cmake --build $BuildDir --parallel $MaxThreads

if ($LASTEXITCODE -ne 0) { Write-Error "Build failed"; exit 1 }

if ($BuildTests) {
    ctest --test-dir $BuildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { Write-Error "Tests failed"; exit 1 }
}

# Prepare Release Package Directory
if (-not (Test-Path $ReleaseDir)) {
    New-Item -ItemType Directory -Force -Path $ReleaseDir | Out-Null
}

# Copy and Rename Executable
$OutputName = "${AppName}_v${Version}_Windows_x64.exe"
$TargetPath = Join-Path $ReleaseDir $OutputName

Write-Host "Copying standalone static executable to $TargetPath..."
if (Test-Path "$BuildDir/$OriginalAppName.exe") {
    Copy-Item "$BuildDir/$OriginalAppName.exe" $TargetPath
    Write-Host "------------------------------------------------------------"
    Write-Host "SUCCESS: Standalone executable created at $TargetPath"

    # 尝试进行 UPX 压缩
    if (Get-Command "upx" -ErrorAction SilentlyContinue) {
        Write-Host "Compressing with UPX..."
        & upx --best "$TargetPath"
    }
    else {
        Write-Warning "UPX not found. Skipping compression."
    }

    Write-Host "You can now send this SINGLE .exe file to anyone!"
    Write-Host "------------------------------------------------------------"
}
else {
    Write-Error "Executable not found at $BuildDir/$OriginalAppName.exe"
    exit 1
}

# 输出独立 SHA256 文件，与 EXE 一同上传到 Release。
if (Test-Path $TargetPath) {
    $Hash = (Get-FileHash $TargetPath -Algorithm SHA256).Hash.ToLower()
    $HashPath = "$TargetPath.sha256"
    "$Hash  $OutputName" | Set-Content -Path $HashPath -Encoding ascii
    Write-Host "------------------------------------------------------------"
    Write-Host "SHA256: $Hash"
    Write-Host "SHA256 file: $HashPath"
    Write-Host "------------------------------------------------------------"
}

Write-Host "Packaging complete!"
