param(
    [string]$ReleaseTag = ""
)

$ErrorActionPreference = "Stop"
$RootDir = (Get-Item $PSScriptRoot).Parent.Parent.FullName
Set-Location $RootDir

python -m unittest tests.test_release_automation -v
if ($LASTEXITCODE -ne 0) { throw "Release automation tests failed" }

$Version = ""
if ([string]::IsNullOrWhiteSpace($ReleaseTag)) {
    $CMakeContent = Get-Content "CMakeLists.txt" -Raw
    if ($CMakeContent -notmatch 'project\s*\(\s*LogAnalyQt\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
        throw "Unable to read LogAnalyQt version from CMakeLists.txt"
    }
    $Version = $matches[1]
}
else {
    if ($ReleaseTag -notmatch '^v([0-9]+\.[0-9]+\.[0-9]+)$') {
        throw "Release tag must use vX.Y.Z format: $ReleaseTag"
    }
    $Version = $matches[1]
}

$NotesFile = ""
if (-not [string]::IsNullOrWhiteSpace($ReleaseTag)) {
    $NotesFile = Join-Path $RootDir "release_package/release-notes.md"
    python scripts/release/generate_release_notes.py --tag $ReleaseTag --output $NotesFile
    if ($LASTEXITCODE -ne 0) { throw "Release notes generation failed" }
}

& "$RootDir/scripts/package_single.ps1" -Version $Version -BuildTests
if ($LASTEXITCODE -ne 0) { throw "Windows packaging failed" }

if ([string]::IsNullOrWhiteSpace($ReleaseTag)) {
    Write-Host "Windows CI build completed without publishing a Release."
    exit 0
}

$TargetCommit = if ($env:GITEE_COMMIT) { $env:GITEE_COMMIT } else { "main" }
python scripts/release/gitee_release.py prepare `
    --tag $ReleaseTag `
    --name "LogAnalyQt $ReleaseTag" `
    --notes-file $NotesFile `
    --target $TargetCommit
if ($LASTEXITCODE -ne 0) { throw "Gitee prerelease preparation failed" }

$Package = Join-Path $RootDir "release_package/TonglianWorkshop_v${Version}_Windows_x64.exe"
python scripts/release/gitee_release.py upload --tag $ReleaseTag --file $Package
if ($LASTEXITCODE -ne 0) { throw "Windows package upload failed" }
python scripts/release/gitee_release.py upload --tag $ReleaseTag --file "$Package.sha256"
if ($LASTEXITCODE -ne 0) { throw "Windows SHA256 upload failed" }

Write-Host "Windows Release assets uploaded to prerelease $ReleaseTag."
