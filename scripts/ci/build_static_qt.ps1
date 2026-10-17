param(
    [Parameter(Mandatory)][string]$InstallDir,
    [Parameter(Mandatory)][string]$MingwDir
)

$ErrorActionPreference = "Stop"
$env:PATH = "$MingwDir/bin;$env:PATH"
$WorkDir = Join-Path $env:RUNNER_TEMP "qt-static-build"
$SourceDir = Join-Path $WorkDir "source"
$BuildDir = Join-Path $WorkDir "build"
New-Item -ItemType Directory -Force $BuildDir | Out-Null

function Check-Exit([string]$Operation) {
    if ($LASTEXITCODE -ne 0) { throw "$Operation failed ($LASTEXITCODE)" }
}

git clone --depth 1 --branch v6.5.3 https://github.com/qt/qt5.git $SourceDir
Check-Exit "Clone Qt"
git -C $SourceDir submodule update --init --depth 1 qtbase qtshadertools qtdeclarative qtsvg
Check-Exit "Fetch Qt modules"

Push-Location $BuildDir
try {
    & "$SourceDir/configure.bat" -prefix $InstallDir -platform win32-g++ `
        -release -static -opensource -confirm-license -nomake examples -nomake tests `
        -submodules qtbase,qtshadertools,qtdeclarative,qtsvg -no-openssl -schannel
    Check-Exit "Configure static Qt"
    cmake --build . --parallel 4
    Check-Exit "Build static Qt"
    cmake --install .
    Check-Exit "Install static Qt"
}
finally { Pop-Location }
if (-not (Test-Path "$InstallDir/lib/cmake/Qt6/Qt6Config.cmake")) {
    throw "Static Qt installation is incomplete"
}
