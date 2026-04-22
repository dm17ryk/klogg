$ErrorActionPreference = "Stop"

function Assert-PathExists {
    param(
        [string]$Path,
        [string]$Message
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw $Message
    }
}

function Expand-ArchiveWith7Zip {
    param(
        [string]$Archive,
        [string]$Destination
    )

    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }

    New-Item -ItemType Directory -Path $Destination | Out-Null
    & 7z x "-o$Destination" -y $Archive | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to extract archive: $Archive"
    }
}

function Assert-KloggDeployment {
    param(
        [string]$Root,
        [string]$Context
    )

    Assert-PathExists (Join-Path $Root "cilogg.exe") "$Context is missing cilogg.exe"
    Assert-PathExists (Join-Path $Root "Qt6Sql.dll") "$Context is missing Qt6Sql.dll"
    Assert-PathExists (Join-Path $Root "sqldrivers\\qsqlite.dll") "$Context is missing qsqlite.dll"
    Assert-PathExists (Join-Path $Root "python_runtime\\klogg\\__init__.py") "$Context is missing python_runtime"
    Assert-PathExists (Join-Path $Root "platforms\\qwindows.dll") "$Context is missing qwindows.dll"
}

function Resolve-KloggRoot {
    param(
        [string]$ExtractedRoot,
        [string]$Context
    )

    $candidates = @(
        $ExtractedRoot,
        (Join-Path $ExtractedRoot "release")
    ) | Where-Object { Test-Path -LiteralPath $_ }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate "cilogg.exe")) {
            return $candidate
        }
    }

    $nested = Get-ChildItem -Path $ExtractedRoot -Recurse -Filter "cilogg.exe" -File -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty DirectoryName
    if ($nested) {
        return $nested
    }

    throw "$Context is missing cilogg.exe"
}

$packageDirs = Get-ChildItem -Path "." -Directory -Filter "packages-windows-*"
if (-not $packageDirs) {
    throw "No Windows package directories found"
}

foreach ($packageDir in $packageDirs) {
    $portableZip = Get-ChildItem -Path $packageDir.FullName -Filter "*-portable.zip" | Select-Object -First 1
    $installer = Get-ChildItem -Path $packageDir.FullName -Filter "*-setup.exe" | Select-Object -First 1

    if (-not $portableZip) {
        throw "Portable archive not found in $($packageDir.FullName)"
    }
    if (-not $installer) {
        throw "Installer not found in $($packageDir.FullName)"
    }

    $portableExtractDir = Join-Path $env:RUNNER_TEMP ("portable_" + $packageDir.Name)
    Expand-ArchiveWith7Zip -Archive $portableZip.FullName -Destination $portableExtractDir
    $portableRoot = Resolve-KloggRoot -ExtractedRoot $portableExtractDir -Context "$($portableZip.Name) portable package"
    Assert-KloggDeployment -Root $portableRoot -Context "$($portableZip.Name) portable package"

    $installDir = Join-Path $env:RUNNER_TEMP ("install_" + $packageDir.Name)
    if (Test-Path -LiteralPath $installDir) {
        Remove-Item -LiteralPath $installDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $installDir | Out-Null

    $process = Start-Process -FilePath $installer.FullName `
                             -ArgumentList "/S", "/D=$installDir" `
                             -Wait `
                             -PassThru
    if ($process.ExitCode -ne 0) {
        throw "Silent install failed for $($installer.Name) with exit code $($process.ExitCode)"
    }

    Assert-KloggDeployment -Root $installDir -Context "$($installer.Name) installed image"

    $uninstaller = Join-Path $installDir "Uninstall.exe"
    if (Test-Path -LiteralPath $uninstaller) {
        Start-Process -FilePath $uninstaller -ArgumentList "/S" -Wait | Out-Null
    }
}

Write-Host "Windows release validation passed."
