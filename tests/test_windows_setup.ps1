$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../scripts/ci/setup_windows_build.ps1"

function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Assert-Throws([scriptblock]$Action, [string]$Pattern) {
    $caught = $false
    try { & $Action } catch {
        $caught = $true
        Assert ($_.Exception.Message -match $Pattern) "Unexpected error: $_"
    }
    Assert $caught 'Expected an error.'
}

$temporary = Join-Path ([IO.Path]::GetTempPath()) ('loganalyqt-setup-test-' + [guid]::NewGuid())
New-Item -ItemType Directory $temporary | Out-Null
try {
    $tokens = $null
    $errors = $null
    [System.Management.Automation.Language.Parser]::ParseFile(
        "$PSScriptRoot/../scripts/ci/setup_windows_build.ps1", [ref]$tokens, [ref]$errors) | Out-Null
    Assert ($errors.Count -eq 0) "PowerShell parse failed: $errors"

    $script:InstallCount = 0
    $script:Installed = $false
    $script:InstallSucceeds = $true
    $script:ApprovedPackages = @{ 'Fixture.Package' = $true }
    function winget.exe {
        $script:InstallCount++
        Assert ($args -contains '--no-upgrade') 'Installer must preserve an existing installation.'
        Assert (-not ($args -contains '--allow-reboot')) 'Installer must not allow reboot.'
        $script:Installed = $script:InstallSucceeds
        $global:LASTEXITCODE = if ($script:InstallSucceeds) { 0 } else { 9 }
    }
    $CheckOnly = $true
    $result = Ensure-Program 'fixture' { $null } 'Fixture.Package' $temporary
    Assert (-not $result -and $script:InstallCount -eq 0) 'Check-only must never install.'
    $CheckOnly = $false
    Assert-Throws { Ensure-Program 'unapproved' { $null } 'Unapproved.Package' $temporary } 'not on the approved'
    Assert ($script:InstallCount -eq 0) 'An unapproved package reached the installer.'
    $probe = { if ($script:Installed) { 'fixture.exe' } }
    $result = Ensure-Program 'fixture' $probe 'Fixture.Package' $temporary
    $result = Ensure-Program 'fixture' $probe 'Fixture.Package' $temporary
    Assert ($result -eq 'fixture.exe' -and $script:InstallCount -eq 1) 'A repeat run must reuse the detected tool.'
    $script:Installed = $false
    $script:InstallSucceeds = $false
    Assert-Throws { Ensure-Program 'fixture' $probe 'Fixture.Package' $temporary } 'not usable'

    $protected = Join-Path $temporary 'existing'
    New-Item -ItemType Directory $protected | Out-Null
    $sentinel = Join-Path $protected 'keep.txt'
    Set-Content $sentinel 'keep'
    Assert-Throws { Assert-EmptyInstallDirectory $protected } 'nothing will be deleted'
    Assert ((Get-Content $sentinel -Raw).Trim() -eq 'keep') 'Existing files changed.'

    $kit = Join-Path $temporary 'qt'
    foreach ($file in @('lib/cmake/Qt6Core/Qt6CoreConfigVersion.cmake', 'lib/libQt6Core.a',
            'lib/libQt6Qml.a', 'lib/libQt6Quick.a', 'lib/cmake/Qt6/Qt6Config.cmake')) {
        $path = Join-Path $kit $file
        New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
        Set-Content $path 'fixture'
    }
    $versionFile = Join-Path $kit 'lib/cmake/Qt6Core/Qt6CoreConfigVersion.cmake'
    Set-Content $versionFile 'set(PACKAGE_VERSION "6.10.0")'
    Assert (-not (Test-QtKit $kit -Static)) 'An incompatible Qt must not be reused.'
    Set-Content $versionFile 'set(PACKAGE_VERSION "6.11.1")'
    Assert (Test-QtKit $kit -Static) 'The matching static kit was not accepted.'
    Set-Content $versionFile 'include("${CMAKE_CURRENT_LIST_DIR}/Qt6CoreConfigVersionImpl.cmake")'
    $versionImplementation = Join-Path $kit 'lib/cmake/Qt6Core/Qt6CoreConfigVersionImpl.cmake'
    Set-Content $versionImplementation 'set(PACKAGE_VERSION "6.11.1")'
    Assert (Test-QtKit $kit -Static) 'The actual Qt 6.11 wrapper/implementation layout was not accepted.'
    Set-Content $versionImplementation 'set(PACKAGE_VERSION "6.10.0")'
    Assert (-not (Test-QtKit $kit -Static)) 'The Qt version implementation file was ignored.'
    Assert (-not (Test-QtKit $kit)) 'A static kit cannot replace the dynamic QML test kit.'

    $envFile = Join-Path $temporary 'environment.ps1'
    $literal = 'C:\build''s\$literal;value'
    Write-BuildEnvironment @{ QT_STATIC_DIR = $literal; LOGANALYQT_BUILD_PATH = $temporary } $envFile
    $oldQt = $env:QT_STATIC_DIR
    $oldPath = $env:PATH
    try {
        . $envFile
        Assert ($env:QT_STATIC_DIR -ceq $literal) 'Generated environment executed/interpolated a path.'
    } finally { $env:QT_STATIC_DIR = $oldQt; $env:PATH = $oldPath }

    if ($env:OS -ne 'Windows_NT') {
        New-PSDrive -Name C -PSProvider FileSystem -Root $temporary | Out-Null
        $oldOs = $env:OS
        $oldArchitecture = $env:PROCESSOR_ARCHITECTURE
        try {
            $env:OS = 'Windows_NT'
            $env:PROCESSOR_ARCHITECTURE = 'AMD64'
            $Root = 'C:\CheckOnlyMustNotCreateThis'
            $CheckOnly = $true
            $countBefore = $script:InstallCount
            Main
            Assert (-not (Test-Path $Root)) 'Check-only created its root directory.'
            Assert ($script:InstallCount -eq $countBefore) 'Check-only called WinGet.'
            function Read-Host { return '' }
            $Install = $true
            $CheckOnly = $false
            Main
            Assert (-not (Test-Path $Root)) 'Declined approval created its root directory.'
            Assert ($script:InstallCount -eq $countBefore) 'Declined approval called WinGet.'
        } finally {
            $env:OS = $oldOs
            $env:PROCESSOR_ARCHITECTURE = $oldArchitecture
            Remove-PSDrive C
        }
    }
    Write-Host 'PASS: parse, check-only, reuse, installation failure, file preservation, Qt compatibility, literal environment paths.'
} finally {
    Remove-Item -Recurse -Force -LiteralPath $temporary
}
