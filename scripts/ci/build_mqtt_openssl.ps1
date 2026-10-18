param(
    [Parameter(Mandatory)][string]$InstallDir,
    [Parameter(Mandatory)][string]$MingwDir,
    [string]$MsysDir = $env:LOGANALYQT_MSYS2_DIR
)

$ErrorActionPreference = "Stop"
$Version = "3.5.9"
$Sha256 = "603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a"
$Compiler = Join-Path $MingwDir "bin/gcc.exe"
if (-not (Test-Path $Compiler)) { throw "MQTT requires the configured MinGW toolchain" }
$CompilerVersion = (& $Compiler -dumpfullversion | Out-String).Trim()
$Stamp = "OpenSSL $Version / MinGW $CompilerVersion / $Sha256"
$StampFile = Join-Path $InstallDir "loganalyqt-build.txt"
if ((Test-Path "$InstallDir/lib/libssl.a") -and (Test-Path "$InstallDir/lib/libcrypto.a") -and
    (Test-Path $StampFile) -and ((Get-Content $StampFile -Raw).Trim() -eq $Stamp)) {
    Write-Host "Using cached MQTT OpenSSL $Version ($CompilerVersion)"
    exit 0
}
if ((Test-Path $InstallDir) -and (Get-ChildItem $InstallDir -Force | Select-Object -First 1)) {
    throw "OpenSSL cache does not match this build; choose a new empty InstallDir: $InstallDir"
}
if ([string]::IsNullOrWhiteSpace($MsysDir)) { $MsysDir = "C:/msys64" }
$Bash = Join-Path $MsysDir "usr/bin/bash.exe"
if (-not (Test-Path $Bash)) { throw "OpenSSL's MinGW build requires MSYS2 with make and perl; set LOGANALYQT_MSYS2_DIR" }
$WorkDir = Join-Path (Split-Path $InstallDir -Parent) ("openssl-work-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $WorkDir | Out-Null
$Archive = Join-Path $WorkDir "openssl.tar.gz"
Invoke-WebRequest "https://github.com/openssl/openssl/releases/download/openssl-$Version/openssl-$Version.tar.gz" -OutFile $Archive
if ((Get-FileHash $Archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Sha256) { throw "OpenSSL source checksum mismatch" }
tar -xzf $Archive -C $WorkDir
if ($LASTEXITCODE -ne 0) { throw "Unable to extract OpenSSL" }
$SourceDir = Join-Path $WorkDir "openssl-$Version"
$BuildScript = @'
set -euo pipefail
source_dir=$(/usr/bin/cygpath -u "$1")
install_dir=$(/usr/bin/cygpath -u "$2")
toolchain=$(/usr/bin/cygpath -u "$3")
export PATH="$toolchain/bin:/usr/bin:/bin:$PATH"
export CC=gcc AR=ar RANLIB=ranlib
cd "$source_dir"
/usr/bin/perl ./Configure mingw64 no-shared no-tests no-module no-dso no-apps "--prefix=$install_dir" --libdir=lib
/usr/bin/make -j 8
/usr/bin/make install_sw
'@
$ScriptPath = Join-Path $WorkDir "build.sh"
[System.IO.File]::WriteAllText($ScriptPath, $BuildScript.Replace("`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))
$ScriptUnixPath = $ScriptPath.Replace('\', '/')
& $Bash --noprofile --norc $ScriptUnixPath $SourceDir $InstallDir $MingwDir
if ($LASTEXITCODE -ne 0) { throw "Static OpenSSL build/install failed" }
if (-not (Test-Path "$InstallDir/lib/libssl.a") -or -not (Test-Path "$InstallDir/lib/libcrypto.a")) {
    throw "Static OpenSSL archives missing"
}
Copy-Item "$SourceDir/LICENSE.txt" "$InstallDir/LICENSE.txt"
Set-Content -Path $StampFile -Value $Stamp -Encoding utf8
Write-Host "Built static MQTT OpenSSL in $InstallDir"
