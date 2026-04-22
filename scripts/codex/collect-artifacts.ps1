param(
    [string]$Config = "RelWithDebInfo",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

$repoRoot = Get-RepoRoot
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $repoRoot "artifacts\$Config"
}
elseif (-not [System.IO.Path]::IsPathRooted($OutDir)) {
    $OutDir = Join-Path $repoRoot $OutDir
}

$sourceDirs = @(
    (Join-Path $repoRoot "build_root\output\$Config"),
    [System.IO.Path]::GetTempPath()
)

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

foreach ($sourceDir in $sourceDirs) {
    if (-not (Test-Path $sourceDir)) {
        continue
    }

    Get-ChildItem -Path $sourceDir -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in @(".log", ".dmp", ".txt") -or $_.Name -like "cilogg*" } |
        Copy-Item -Destination $OutDir -Force
}
