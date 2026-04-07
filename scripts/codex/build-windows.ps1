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
    throw "Set KLOGG_QT_DIR or QTDIR before building."
}

function Get-VsDevCmd {
    if ($env:VSINSTALLDIR) {
        $candidate = Join-Path $env:VSINSTALLDIR "Common7\Tools\VsDevCmd.bat"
        if (Test-Path $candidate) { return $candidate }
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Run from a VS 2022 Developer Command Prompt or install Visual Studio Build Tools."
    }

    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installPath) {
        throw "Visual Studio 2022 with C++ tools was not found."
    }

    $candidate = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path $candidate)) {
        throw "VsDevCmd.bat not found under $installPath."
    }

    return $candidate
}

$repoRoot = Get-RepoRoot
$buildDir = Join-Path $repoRoot "build_root"
$qtDir = Get-QtDir
$vsDevCmd = Get-VsDevCmd

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

$quotedRepoRoot = '"' + $repoRoot + '"'
$quotedBuildDir = '"' + $buildDir + '"'
$quotedQtDir = '"' + $qtDir + '"'
$quotedVsDevCmd = '"' + $vsDevCmd + '"'

$command = @(
    "call $quotedVsDevCmd -startdir=none -arch=x64 -host_arch=x64",
    "set QTDIR=$quotedQtDir",
    "set KLOGG_QT_DIR=$quotedQtDir",
    "set CMAKE_PREFIX_PATH=$quotedQtDir",
    "set PATH=%QTDIR%\bin;%PATH%",
    "cd /d $quotedBuildDir",
    "cmake -G ""Visual Studio 17 2022"" -A x64 -DCMAKE_BUILD_TYPE=$Config ..",
    "cmake --build . --config $Config"
) -join " && "

& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
