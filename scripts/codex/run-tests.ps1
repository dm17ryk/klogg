param(
    [string]$Config = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

function Get-QtDir {
    if ($env:KLOGG_QT_DIR) { return $env:KLOGG_QT_DIR }
    if ($env:QTDIR) { return $env:QTDIR }
    throw "Set KLOGG_QT_DIR or QTDIR before running tests."
}

function Copy-OffscreenPlatformPlugin {
    param(
        [Parameter(Mandatory = $true)]
        [string]$QtDir,
        [Parameter(Mandatory = $true)]
        [string]$TargetDir
    )

    $sourcePlugin = Join-Path $QtDir "plugins\platforms\qoffscreen.dll"
    if (-not (Test-Path $sourcePlugin)) {
        throw "qoffscreen.dll not found under $QtDir."
    }

    $platformDir = Join-Path $TargetDir "platforms"
    New-Item -ItemType Directory -Force -Path $platformDir | Out-Null
    Copy-Item -LiteralPath $sourcePlugin -Destination (Join-Path $platformDir "qoffscreen.dll") -Force
}

$repoRoot = Get-RepoRoot
$buildDir = Join-Path $repoRoot "build_root"
$qtDir = Get-QtDir
$windeployqt = Join-Path $qtDir "bin\windeployqt.exe"

if (-not (Test-Path $windeployqt)) {
    throw "windeployqt.exe not found under $qtDir."
}

$appExe = Join-Path $buildDir "output\$Config\klogg.exe"
$uiTests = Join-Path $buildDir "output\$Config\klogg_itests.exe"
$unitTests = Join-Path $buildDir "output\$Config\klogg_tests.exe"
$outputDir = Split-Path -Path $uiTests -Parent

foreach ($exe in @($appExe, $uiTests, $unitTests)) {
    if (-not (Test-Path $exe)) {
        throw "Executable not found: $exe"
    }

    & $windeployqt $exe | Out-Host
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Copy-OffscreenPlatformPlugin -QtDir $qtDir -TargetDir $outputDir

Push-Location $buildDir
try {
    & ctest --build-config $Config --verbose
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}
