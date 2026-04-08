param(
    [string]$Config = "RelWithDebInfo",
    [string]$CodexWinGuiRoot = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

$repoRoot = Get-RepoRoot
if ([string]::IsNullOrWhiteSpace($CodexWinGuiRoot)) {
    $CodexWinGuiRoot = (Resolve-Path (Join-Path $repoRoot "..\codex-win-gui-mcp")).Path
}

if (-not (Test-Path $CodexWinGuiRoot)) {
    throw "codex-win-gui-mcp repo not found: $CodexWinGuiRoot"
}

if (-not $SkipBuild) {
    & (Join-Path $repoRoot "scripts\codex\build-windows.ps1") -Config $Config
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$script = Join-Path $CodexWinGuiRoot "scripts\run-klogg-validation.ps1"
if (-not (Test-Path $script)) {
    throw "Validation script not found: $script"
}

& $script -KloggRoot $repoRoot -Config $Config
exit $LASTEXITCODE
