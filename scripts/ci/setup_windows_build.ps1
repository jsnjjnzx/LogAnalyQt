param(
    [string]$Root = 'C:\LogAnalyQtBuild',
    [switch]$CheckOnly,
    [switch]$Install,
    [string]$QtStaticDir = $env:QT_STATIC_DIR,
    [string]$QtTestDir = $env:QT_ROOT_DIR,
    [string]$MingwDir = $env:MINGW_DIR,
    [string]$MsysDir = $env:LOGANALYQT_MSYS2_DIR
)

$ErrorActionPreference = 'Stop'
$AqtCommit = '076e1659807d0b362a3ed684d54c2e9c775eb9c7'

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    & $File @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$File failed (exit $LASTEXITCODE). See the log; existing files were retained." }
}

function Find-Program([string]$Name, [string[]]$Candidates) {
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command -and $command.Source -notmatch 'WindowsApps') { return $command.Source }
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) { return $candidate }
    }
}

function Test-Version([string]$Program, [string[]]$Arguments, [string]$Pattern) {
    if (-not $Program) { return $false }
    try {
        $output = & $Program @Arguments 2>$null | Out-String
        return ($LASTEXITCODE -eq 0 -and $output -match $Pattern)
    } catch { return $false }
}

function Ensure-Program([string]$Name, [scriptblock]$Probe, [string]$Package, [string]$Location, [string[]]$ExtraArguments = @()) {
    $program = & $Probe
    if ($program) { Write-Host "[REUSE] $Name : $program"; return $program }
    if ($CheckOnly) { Write-Host "[MISSING] $Name"; return $null }
    if (-not $script:ApprovedPackages[$Package]) { throw "$Name was not on the approved installation list. Run the check again." }
    if (-not (Get-Command winget.exe -ErrorAction SilentlyContinue)) {
        throw 'WinGet is missing. Install/update Microsoft App Installer from Microsoft Store, then run this script again.'
    }
    Write-Host "[INSTALL] $Name -> $Location"
    & winget.exe install --id $Package --exact --source winget --architecture x64 --location $Location `
        --silent --no-upgrade --accept-source-agreements --accept-package-agreements @ExtraArguments | Out-Host
    $installCode = $LASTEXITCODE
    $program = & $Probe
    if (-not $program) {
        throw "$Name is not usable after WinGet (exit $installCode). An existing installation may need to be added to PATH. No upgrade/uninstall was requested."
    }
    Write-Host "[READY] $Name : $program"
    return $program
}

function Find-Python {
    $candidates = @(
        (Join-Path $Root 'tools\Python312\python.exe'),
        "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe",
        "$env:ProgramFiles\Python312\python.exe"
    )
    $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($launcher) {
        try {
            $found = & $launcher.Source -3.12 -c 'import sys; print(sys.executable)' 2>$null
            if ($LASTEXITCODE -eq 0) { $candidates = @($found) + $candidates }
        } catch { }
    }
    $command = Find-Program 'python.exe' @()
    foreach ($candidate in (@($command) + $candidates)) {
        if (Test-Version $candidate @('-c', 'import sys,struct; print("%d.%d %d" % (*sys.version_info[:2],struct.calcsize("P")*8))') '^3\.12 64\s*$') {
            return $candidate
        }
    }
}

function Test-QtKit([string]$Directory, [switch]$Static) {
    if (-not $Directory -or -not (Test-Path -LiteralPath $Directory -PathType Container)) { return $false }
    $versionFile = Join-Path $Directory 'lib\cmake\Qt6Core\Qt6CoreConfigVersionImpl.cmake'
    if (-not (Test-Path -LiteralPath $versionFile)) {
        $versionFile = Join-Path $Directory 'lib\cmake\Qt6Core\Qt6CoreConfigVersion.cmake'
    }
    if (-not (Test-Path -LiteralPath $versionFile)) { return $false }
    if ((Get-Content -LiteralPath $versionFile -Raw) -notmatch 'PACKAGE_VERSION\s+"6\.11\.1"') { return $false }
    $required = if ($Static) {
        @('lib\libQt6Core.a', 'lib\libQt6Qml.a', 'lib\libQt6Quick.a', 'lib\cmake\Qt6\Qt6Config.cmake')
    } else {
        @('bin\qmltestrunner.exe', 'bin\Qt6Core.dll', 'bin\Qt6Qml.dll', 'bin\Qt6Quick.dll')
    }
    foreach ($relative in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $Directory $relative))) { return $false }
    }
    return $true
}

function Select-QtKit([string[]]$Candidates, [switch]$Static) {
    foreach ($candidate in $Candidates) {
        if (Test-QtKit $candidate -Static:$Static) { return $candidate }
    }
}

function Assert-EmptyInstallDirectory([string]$Directory) {
    if ((Test-Path -LiteralPath $Directory) -and (Get-ChildItem -LiteralPath $Directory -Force | Select-Object -First 1)) {
        throw "Incomplete or incompatible installation retained at $Directory. Choose another empty -Root or provide the correct existing tool directory; nothing will be deleted."
    }
}

function Write-BuildEnvironment([hashtable]$Values, [string]$File) {
    $lines = @('# Generated build environment; applies only to this PowerShell session.')
    foreach ($name in ($Values.Keys | Sort-Object)) {
        $escaped = $Values[$name].Replace("'", "''")
        $lines += "`$env:${name} = '$escaped'"
    }
    $lines += '$env:PATH = "$env:LOGANALYQT_BUILD_PATH;$env:PATH"'
    $lines += '$env:PYTHONUTF8 = ''1'''
    Set-Content -LiteralPath $File -Value $lines -Encoding UTF8
}

function Test-StaticToolchain([string]$Cmake, [string]$QtDirectory, [string]$CompilerDirectory) {
    $smoke = Join-Path $Root ('smoke\' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $smoke | Out-Null
    @'
cmake_minimum_required(VERSION 3.24)
project(LogAnalyQtSetupSmoke LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core)
add_executable(qt_setup_smoke main.cpp)
target_link_libraries(qt_setup_smoke PRIVATE Qt6::Core)
target_link_options(qt_setup_smoke PRIVATE -static -static-libgcc -static-libstdc++)
'@ | Set-Content -LiteralPath "$smoke\CMakeLists.txt" -Encoding ASCII
    @'
#include <QtCore/QString>
#include <iostream>
int main() {
    const auto version = QString::fromLatin1(qVersion());
    std::cout << "Static Qt smoke test: " << version.toStdString() << std::endl;
    return version == QStringLiteral("6.11.1") ? 0 : 1;
}
'@ | Set-Content -LiteralPath "$smoke\main.cpp" -Encoding ASCII
    Invoke-Checked $Cmake @('-S', $smoke, '-B', "$smoke\build", '-G', 'MinGW Makefiles', '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_PREFIX_PATH=$QtDirectory", "-DCMAKE_CXX_COMPILER=$CompilerDirectory/bin/g++.exe")
    Invoke-Checked $Cmake @('--build', "$smoke\build", '--parallel', '2')
    $executable = "$smoke\build\qt_setup_smoke.exe"
    $imports = & "$CompilerDirectory\bin\objdump.exe" -p $executable | Out-String
    if ($LASTEXITCODE -ne 0 -or $imports -match 'DLL Name:\s+(Qt6|libgcc|libstdc|libwinpthread)') {
        throw 'Static Qt smoke test still requires external Qt/MinGW DLLs.'
    }
    Invoke-Checked $executable @()
}

function Main {
    if ($env:OS -ne 'Windows_NT') { throw 'Run this setup on Windows 11 x64.' }
    $architecture = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
    if ($architecture -ne 'AMD64') { throw 'This setup requires an Intel/AMD x64 Windows computer.' }
    if ($Root -notmatch '^[A-Za-z]:\\[A-Za-z0-9_\\-]+$' -or $Root.TrimEnd('\') -match '^[A-Za-z]:$') {
        throw 'Use a short local directory without spaces/non-ASCII characters, such as C:\LogAnalyQtBuild.'
    }
    $Root = $Root.TrimEnd('\')
    if ($Install -and $CheckOnly) { throw 'Use either -Install or -CheckOnly, not both.' }
    # Discovery must finish before any directory creation, download or installation.
    $CheckOnly = $true
    $marker = Join-Path $Root '.loganalyqt-build-root'
    Write-Host "LogAnalyQt Windows setup. Root: $Root"
    Write-Host 'CHECK FIRST. No automatic restart, runner registration, or GitHub publishing.'
    $drive = Get-PSDrive ($Root.Substring(0, 1))
    Write-Host ("[INFO] Free disk space: {0:N1} GiB" -f ($drive.Free / 1GB))
    if (Get-Command winget.exe -ErrorAction SilentlyContinue) { Write-Host '[REUSE] WinGet' }
    else { Write-Host '[MISSING] WinGet: Microsoft App Installer from Microsoft Store is needed for missing base tools.' }
    $programs = @(
    @{ Name = 'Git'; Package = 'Git.Git'; Location = "$Root\tools\Git"; Extra = @(); Probe = {
        $candidate = Find-Program 'git.exe' @("$Root\tools\Git\cmd\git.exe", "$env:ProgramFiles\Git\cmd\git.exe", "$env:LOCALAPPDATA\Programs\Git\cmd\git.exe")
        if (Test-Version $candidate @('--version') '^git version ') { $candidate }
    } },
    @{ Name = 'Python 3.12 x64'; Package = 'Python.Python.3.12'; Location = "$Root\tools\Python312"; Extra = @('--scope', 'user'); Probe = { Find-Python } },
    @{ Name = 'PowerShell 7'; Package = 'Microsoft.PowerShell'; Location = "$Root\tools\PowerShell"; Extra = @(); Probe = {
        $candidate = Find-Program 'pwsh.exe' @("$Root\tools\PowerShell\pwsh.exe", "$env:ProgramFiles\PowerShell\7\pwsh.exe")
        if (Test-Version $candidate @('-NoProfile', '-Command', '$PSVersionTable.PSVersion.Major') '^7\s*$') { $candidate }
    } },
    @{ Name = 'GitHub CLI'; Package = 'GitHub.cli'; Location = "$Root\tools\GitHubCLI"; Extra = @(); Probe = {
        $candidate = Find-Program 'gh.exe' @("$Root\tools\GitHubCLI\bin\gh.exe", "$Root\tools\GitHubCLI\gh.exe", "$env:ProgramFiles\GitHub CLI\gh.exe")
        if (Test-Version $candidate @('--version') '^gh version ') { $candidate }
    } },
    @{ Name = 'MSYS2'; Package = 'MSYS2.MSYS2'; Location = "$Root\tools\msys64"; Extra = @(); Probe = {
        foreach ($directory in @($MsysDir, "$Root\tools\msys64", 'C:\msys64', 'D:\msys64')) {
            if ($directory -and (Test-Path -LiteralPath "$directory\usr\bin\pacman.exe") -and (Test-Path -LiteralPath "$directory\usr\bin\bash.exe")) {
                return "$directory\usr\bin\bash.exe"
            }
        }
    } }
    )
    $detected = @{}
    $planned = @()
    $script:ApprovedPackages = @{}
    foreach ($program in $programs) {
        $detected[$program.Name] = Ensure-Program $program.Name $program.Probe $program.Package $program.Location $program.Extra
        if (-not $detected[$program.Name]) { $planned += "$($program.Name) -> $($program.Location)"; $script:ApprovedPackages[$program.Package] = $true }
    }
    $git = $detected['Git']; $python = $detected['Python 3.12 x64']; $pwsh = $detected['PowerShell 7']
    $gh = $detected['GitHub CLI']; $bash = $detected['MSYS2']
    if ($bash) { $MsysDir = Split-Path (Split-Path (Split-Path $bash -Parent) -Parent) -Parent }

    $cmake = $null
    $cmakeCandidates = @((Find-Program 'cmake.exe' @()), "$Root\python-env\Scripts\cmake.exe", "$env:ProgramFiles\CMake\bin\cmake.exe")
    foreach ($candidate in $cmakeCandidates) {
        if (-not $candidate -or -not (Test-Path -LiteralPath $candidate)) { continue }
        try { $output = & $candidate --version 2>$null | Out-String }
        catch { Write-Host "[UNVERIFIED] CMake at $candidate"; continue }
        if ($LASTEXITCODE -ne 0 -or $output -notmatch 'cmake version (\d+\.\d+\.\d+)' -or
            [version]$Matches[1] -lt [version]'3.24.0') { continue }
        $cmake = $candidate; break
    }
    $ninja = Find-Program 'ninja.exe' @("$Root\python-env\Scripts\ninja.exe")
    if (-not (Test-Version $ninja @('--version') '^\d+\.\d+')) { $ninja = $null }
    $qt = Select-QtKit @($QtTestDir, "$Root\Qt\6.11.1\mingw_64", 'C:\Qt\6.11.1\mingw_64', 'D:\Qt\6.11.1\mingw_64')
    $staticQt = Select-QtKit @($QtStaticDir, "$Root\qt-static\6.11.1", 'D:\qt-static\6.11.1', 'D:\DevTool\QtStaticLib') -Static
    if ($QtTestDir -and -not (Test-QtKit $QtTestDir)) { Write-Host "[INCOMPATIBLE/UNVERIFIED] Qt test kit at $QtTestDir will be retained." }
    if ($QtStaticDir -and -not (Test-QtKit $QtStaticDir -Static)) { Write-Host "[INCOMPATIBLE/UNVERIFIED] Static Qt at $QtStaticDir will be retained." }
    $mingwCandidates = @($MingwDir, "$Root\Qt\Tools\mingw1310_64", 'C:\Qt\Tools\mingw1310_64', 'D:\Qt\Tools\mingw1310_64')
    $MingwDir = $null
    foreach ($candidate in $mingwCandidates) {
        if ($candidate -and (Test-Version "$candidate\bin\gcc.exe" @('-dumpfullversion') '^13\.1\.0\s*$') -and
            (Test-Path -LiteralPath "$candidate\bin\g++.exe") -and (Test-Path -LiteralPath "$candidate\bin\mingw32-make.exe")) {
            $MingwDir = $candidate; break
        }
    }
    if (-not ($MingwDir -and (Test-Version "$MingwDir\bin\gcc.exe" @('-dumpfullversion') '^13\.1\.0\s*$'))) { $MingwDir = $null }
    foreach ($item in @(@('CMake >= 3.24', $cmake), @('Ninja', $ninja), @('Qt 6.11.1 test kit', $qt), @('MinGW 13.1', $MingwDir), @('Qt 6.11.1 static kit', $staticQt))) {
        if ($item[1]) { Write-Host "[REUSE] $($item[0]) : $($item[1])" } else { Write-Host "[MISSING] $($item[0])" }
    }
    $needsMsysPackages = -not ($MsysDir -and (Test-Path -LiteralPath "$MsysDir\usr\bin\make.exe") -and (Test-Path -LiteralPath "$MsysDir\usr\bin\perl.exe"))
    if ($MsysDir) {
        foreach ($name in @('make', 'perl')) {
            if (Test-Path -LiteralPath "$MsysDir\usr\bin\$name.exe") { Write-Host "[REUSE] MSYS2 $name" }
            else { Write-Host "[MISSING] MSYS2 $name" }
        }
    }
    $openssl = if ($env:LOGANALYQT_MQTT_OPENSSL_DIR) { $env:LOGANALYQT_MQTT_OPENSSL_DIR } else { "$Root\mqtt-openssl\3.5.9" }
    $opensslStamp = 'OpenSSL 3.5.9 / MinGW 13.1.0 / 603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a'
    $opensslReady = (Test-Path -LiteralPath "$openssl\lib\libssl.a") -and (Test-Path -LiteralPath "$openssl\lib\libcrypto.a") -and
        (Test-Path -LiteralPath "$openssl\loganalyqt-build.txt") -and ((Get-Content -LiteralPath "$openssl\loganalyqt-build.txt" -Raw).Trim() -eq $opensslStamp)
    if ($opensslReady) { Write-Host "[REUSE] OpenSSL 3.5.9 : $openssl" }
    else { Write-Host "[MISSING/INCOMPATIBLE] OpenSSL 3.5.9 : $openssl" }
    if (-not (Test-Path -LiteralPath "$Root\python-env\Scripts\python.exe")) { $planned += "Create isolated Python environment -> $Root\python-env" }
    if (-not $cmake) { $planned += 'Install CMake >= 3.24 in the isolated Python environment' }
    if (-not $ninja) { $planned += 'Install Ninja in the isolated Python environment' }
    if (-not $qt -or -not $MingwDir) { $planned += "Prepare aqtinstall at commit $AqtCommit in the isolated Python environment" }
    if (-not $MingwDir) { $planned += "Install MinGW 13.1 -> $Root\Qt\Tools\mingw1310_64" }
    if (-not $qt) { $planned += "Install Qt 6.11.1 QML test kit -> $Root\Qt\6.11.1\mingw_64" }
    if ($needsMsysPackages) { $planned += 'Install missing MSYS2 make/perl packages (no full upgrade)' }
    if (-not $staticQt) { $planned += "Build static Qt 6.11.1 -> $Root\qt-static\6.11.1 (first build may take hours)" }
    if (-not $opensslReady) { $planned += "Build static OpenSSL 3.5.9 -> $openssl" }
    $planned += "Compile/run a small static Qt test; create $Root\environment.ps1 and setup logs"
    Write-Host "`nFULL PLAN (existing compatible tools above will be reused):"
    for ($index = 0; $index -lt $planned.Count; $index++) { Write-Host ("  {0}. {1}" -f ($index + 1), $planned[$index]) }
    Write-Host 'Incompatible versions are retained; no automatic upgrade, overwrite or deletion of an existing kit.'
    if (-not $Install) { Write-Host 'CHECK ONLY: nothing changed. Run Install-Windows-Build.cmd to review and approve installation.'; return }

    if ((Test-Path -LiteralPath $Root) -and -not (Test-Path -LiteralPath $marker)) { Assert-EmptyInstallDirectory $Root }
    if (-not $MingwDir) { Assert-EmptyInstallDirectory "$Root\Qt\Tools\mingw1310_64" }
    if (-not $qt) { Assert-EmptyInstallDirectory "$Root\Qt\6.11.1\mingw_64" }
    if (-not $staticQt) { Assert-EmptyInstallDirectory "$Root\qt-static\6.11.1" }
    if (-not $opensslReady) { Assert-EmptyInstallDirectory $openssl }
    $requiredSpace = if ($staticQt) { 10GB } else { 60GB }
    if ($drive.Free -lt $requiredSpace) { throw "Insufficient free space. This plan needs at least $($requiredSpace / 1GB) GiB available." }
    if ($script:ApprovedPackages.Count -gt 0 -and -not (Get-Command winget.exe -ErrorAction SilentlyContinue)) {
        throw 'Install/update Microsoft App Installer (WinGet) first. No installation has started.'
    }
    $answer = Read-Host 'Review the FULL PLAN above. Type INSTALL to approve these items and their package dependencies, or press Enter to cancel'
    if ($answer -cne 'INSTALL') { Write-Host 'Cancelled. No files, downloads or packages were changed.'; return }
    $CheckOnly = $false
    New-Item -ItemType Directory -Force $Root | Out-Null
    Set-Content -LiteralPath $marker -Value 'LogAnalyQt Windows build environment' -Encoding ASCII
    New-Item -ItemType Directory -Force (Join-Path $Root 'logs') | Out-Null
    $script:SetupLog = Join-Path $Root ('logs\setup-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.txt')
    Start-Transcript -Path $script:SetupLog | Out-Null
    Write-Host 'Approved plan:'
    $planned | ForEach-Object { Write-Host $_ }
    foreach ($program in $programs) {
        $detected[$program.Name] = Ensure-Program $program.Name $program.Probe $program.Package $program.Location $program.Extra
    }
    $git = $detected['Git']; $python = $detected['Python 3.12 x64']; $pwsh = $detected['PowerShell 7']
    $gh = $detected['GitHub CLI']; $bash = $detected['MSYS2']
    $MsysDir = Split-Path (Split-Path (Split-Path $bash -Parent) -Parent) -Parent

    $env:PATH = "$(Split-Path $git);$(Split-Path $pwsh);$(Split-Path $gh);$env:PATH"
    $venvPython = "$Root\python-env\Scripts\python.exe"
    if (-not (Test-Path -LiteralPath $venvPython)) { Invoke-Checked $python @('-m', 'venv', "$Root\python-env") }
    if (-not $cmake) {
        Invoke-Checked $venvPython @('-m', 'pip', 'install', 'cmake>=3.24')
        $cmake = "$Root\python-env\Scripts\cmake.exe"
    }
    if (-not $ninja) {
        Invoke-Checked $venvPython @('-m', 'pip', 'install', 'ninja')
        $ninja = "$Root\python-env\Scripts\ninja.exe"
    }
    $env:PATH = "$(Split-Path $cmake);$(Split-Path $ninja);$env:PATH"
    if (-not $qt -or -not $MingwDir) {
        $aqtReady = Test-Version $venvPython @('-c', "import importlib.metadata,json; d=json.loads(importlib.metadata.distribution('aqtinstall').read_text('direct_url.json') or '{}'); print(d.get('vcs_info',{}).get('commit_id',''))") "^$AqtCommit\s*$"
        if (-not $aqtReady) { Invoke-Checked $venvPython @('-m', 'pip', 'install', "git+https://github.com/miurahr/aqtinstall.git@$AqtCommit") }
        if (-not $MingwDir) {
            Assert-EmptyInstallDirectory "$Root\Qt\Tools\mingw1310_64"
            Invoke-Checked $venvPython @('-m', 'aqt', 'install-tool', '-O', "$Root\Qt", 'windows', 'desktop', 'tools_mingw1310', 'qt.tools.win64_mingw1310')
            $MingwDir = "$Root\Qt\Tools\mingw1310_64"
        }
        if (-not $qt) {
            Assert-EmptyInstallDirectory "$Root\Qt\6.11.1\mingw_64"
            Invoke-Checked $venvPython @('-m', 'aqt', 'install-qt', '-O', "$Root\Qt", 'windows', 'desktop', '6.11.1', 'win64_mingw')
            $qt = "$Root\Qt\6.11.1\mingw_64"
        }
    }
    if (-not (Test-QtKit $qt)) { throw 'Qt test kit validation failed.' }
    if (-not (Test-Version "$MingwDir\bin\gcc.exe" @('-dumpfullversion') '^13\.1\.0\s*$')) { throw 'MinGW 13.1 validation failed.' }
    $env:PATH = "$MingwDir\bin;$env:PATH"
    if (-not ((Test-Path -LiteralPath "$MsysDir\usr\bin\make.exe") -and (Test-Path -LiteralPath "$MsysDir\usr\bin\perl.exe"))) {
        # shortcut: reuse an existing MSYS2 package database; stop on stale mirrors rather than upgrading the user's environment.
        $missingPackages = @('make', 'perl') | Where-Object { -not (Test-Path -LiteralPath "$MsysDir\usr\bin\$_.exe") }
        Invoke-Checked $bash @('--login', '-c', ('pacman -S --needed --noconfirm ' + ($missingPackages -join ' ')))
    }
    if (-not $staticQt) {
        $staticQt = "$Root\qt-static\6.11.1"
        Assert-EmptyInstallDirectory $staticQt
        $env:RUNNER_TEMP = Join-Path $Root ('work\' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Force $env:RUNNER_TEMP | Out-Null
        Write-Host '[BUILD] Static Qt. The first run can take a long time; keep this window open.'
        Invoke-Checked $pwsh @('-NoProfile', '-File', "$PSScriptRoot\build_static_qt.ps1", '-InstallDir', $staticQt, '-MingwDir', $MingwDir)
    }
    if (-not (Test-QtKit $staticQt -Static)) { throw 'Static Qt validation failed.' }
    Invoke-Checked $pwsh @('-NoProfile', '-File', "$PSScriptRoot\build_mqtt_openssl.ps1", '-InstallDir', $openssl, '-MingwDir', $MingwDir, '-MsysDir', $MsysDir)
    Test-StaticToolchain $cmake $staticQt $MingwDir

    $environment = Join-Path $Root 'environment.ps1'
    Write-BuildEnvironment @{
        QT_STATIC_DIR = $staticQt
        QT_ROOT_DIR = $qt
        MINGW_DIR = $MingwDir
        LOGANALYQT_MSYS2_DIR = $MsysDir
        LOGANALYQT_MQTT_OPENSSL_DIR = $openssl
        LOGANALYQT_BUILD_PATH = "$MingwDir\bin;$qt\bin;$(Split-Path $cmake);$(Split-Path $ninja);$(Split-Path $python);$(Split-Path $git);$(Split-Path $pwsh);$(Split-Path $gh)"
    } $environment
    Write-Host "[READY] Environment created: $environment"
    Write-Host 'Next: open PowerShell 7, dot-source environment.ps1, then build a separate LogAnalyQt checkout.'
    Write-Host 'GitHub runner registration and publishing have not been performed.'
}

if ($MyInvocation.InvocationName -ne '.') {
    try { Main }
    catch { Write-Host "SETUP FAILED: $($_.Exception.Message)" -ForegroundColor Red; exit 1 }
    finally { if ($script:SetupLog) { Stop-Transcript | Out-Null } }
}
