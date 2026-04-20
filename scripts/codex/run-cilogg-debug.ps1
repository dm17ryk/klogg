param(
    [string]$Config = "RelWithDebInfo",
    [string]$LogFile = ""
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

function Get-QtDir {
    if ($env:CILOGG_QT_DIR) { return $env:CILOGG_QT_DIR }
    if ($env:QTDIR) { return $env:QTDIR }
    throw "Set CILOGG_QT_DIR or QTDIR before launching cilogg."
}

$repoRoot = Get-RepoRoot
$buildDir = Join-Path $repoRoot "build_root"
$qtDir = Get-QtDir
$windeployqt = Join-Path $qtDir "bin\windeployqt.exe"
$exe = Join-Path $buildDir "output\$Config\cilogg.exe"

if (-not (Test-Path $exe)) {
    throw "cilogg.exe not found: $exe"
}

if ([string]::IsNullOrWhiteSpace($LogFile)) {
    $LogFile = Join-Path $repoRoot "test_data\random_block_100byte.txt"
}
elseif (-not [System.IO.Path]::IsPathRooted($LogFile)) {
    $LogFile = Join-Path $repoRoot $LogFile
}

if (-not (Test-Path $LogFile)) {
    throw "Log file not found: $LogFile"
}

& $windeployqt $exe | Out-Host
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$arguments = @("-n", "-m", "-l", "-dddd", $LogFile)
$previousAutomation = $env:CILOGG_AUTOMATION
$env:CILOGG_AUTOMATION = "1"

try {
    Start-Process -FilePath $exe -WorkingDirectory $repoRoot -ArgumentList $arguments
}
finally {
    $env:CILOGG_AUTOMATION = $previousAutomation
}
