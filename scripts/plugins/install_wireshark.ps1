<#
.SYNOPSIS
Installs the Wireshark command-line tools for the decoder-pdu CI job (#177).

.DESCRIPTION
Tries Chocolatey first, retrying with a growing delay. If the community feed
stays down, installs the pinned official Wireshark MSI after checking it
against the SHA-256 that wireshark.org publishes. Fails with a clear message
when tshark.exe or text2pcap.exe is still missing, so an infrastructure
failure shows up as an install failure, not as decoder test failures.

.PARAMETER InstallDir
Where the tools are expected. Both installers use the default folder.

.PARAMETER ChocoAttempts
How many times to try Chocolatey. 0 goes straight to the official MSI.

.PARAMETER FirstDelaySeconds
The wait after the first failed Chocolatey attempt; it doubles each time.

.PARAMETER VerifyOnly
Install nothing; only check that the tools are in InstallDir.
#>
[CmdletBinding()]
param(
    [string]$InstallDir = (Join-Path $env:ProgramFiles 'Wireshark'),
    [ValidateRange(0, 10)][int]$ChocoAttempts = 3,
    [ValidateRange(0, 600)][int]$FirstDelaySeconds = 20,
    [switch]$VerifyOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Official MSI. Size and SHA-256 come from
# https://www.wireshark.org/download/SIGNATURES-4.6.10.txt; the all-versions
# folders keep the file after newer releases.
$FallbackVersion = '4.6.10'
$FallbackSha256 = 'DBCFC374215C48F12F16BF059943BE59D1F17C68A22E58F7AB6D84C131B42006'
$FallbackUrls = @(
    "https://2.na.dl.wireshark.org/win64/all-versions/Wireshark-$FallbackVersion-x64.msi",
    "https://1.eu.dl.wireshark.org/win64/all-versions/Wireshark-$FallbackVersion-x64.msi",
    "https://www.wireshark.org/download/win64/all-versions/Wireshark-$FallbackVersion-x64.msi"
)
$RequiredTools = @('tshark.exe', 'text2pcap.exe')

function Get-MissingTool {
    # Callers wrap the result in @(): one missing tool would otherwise come back as a string.
    $RequiredTools | Where-Object { -not (Test-Path -LiteralPath (Join-Path $InstallDir $_) -PathType Leaf) }
}

function Install-WithChocolatey {
    if (-not (Get-Command choco -ErrorAction SilentlyContinue)) {
        Write-Warning 'Chocolatey is not available on this runner.'
        return $false
    }
    for ($attempt = 1; $attempt -le $ChocoAttempts; $attempt++) {
        Write-Host "Installing Wireshark with Chocolatey (attempt $attempt of $ChocoAttempts)"
        # choco can exit 0 after "installed 0/0 packages" when the feed fails,
        # so the tools on disk decide whether this attempt worked.
        & choco install wireshark --yes --no-progress | Out-Host
        $exitCode = $LASTEXITCODE
        $missing = @(Get-MissingTool)
        if ($missing.Count -eq 0) {
            return $true
        }
        Write-Warning "choco exited with $exitCode and $($missing -join ', ') is still missing from $InstallDir."
        if ($attempt -lt $ChocoAttempts) {
            $delay = $FirstDelaySeconds * [math]::Pow(2, $attempt - 1)
            Write-Host "Waiting $delay s before the next attempt"
            Start-Sleep -Seconds $delay
        }
    }
    return $false
}

function Install-FromOfficialMsi {
    $msi = Join-Path ([IO.Path]::GetTempPath()) "Wireshark-$FallbackVersion-x64.msi"
    $log = Join-Path ([IO.Path]::GetTempPath()) "Wireshark-$FallbackVersion-x64-msi.log"
    foreach ($url in $FallbackUrls) {
        Write-Host "Downloading $url"
        & curl.exe --fail --location --silent --show-error --retry 3 --retry-delay 10 --output $msi $url
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Download failed (curl exit code $LASTEXITCODE)."
            continue
        }
        $hash = (Get-FileHash -LiteralPath $msi -Algorithm SHA256).Hash
        if ($hash -ne $FallbackSha256) {
            Write-Warning "Checksum mismatch: got $hash, expected $FallbackSha256."
            Remove-Item -LiteralPath $msi -Force
            continue
        }
        Write-Host "Checksum verified ($hash); installing Wireshark $FallbackVersion"
        $process = Start-Process -FilePath msiexec.exe -Wait -PassThru -ArgumentList @(
            '/i', "`"$msi`"", '/qn', '/norestart', '/l*v', "`"$log`"")
        # 3010: installed, restart required (not needed for the command-line tools).
        if ($process.ExitCode -notin @(0, 3010)) {
            throw "msiexec exited with $($process.ExitCode) installing Wireshark $FallbackVersion; see $log"
        }
        return
    }
    throw "Could not download Wireshark $FallbackVersion with SHA-256 $FallbackSha256 from any official mirror."
}

if (-not $VerifyOnly -and @(Get-MissingTool).Count -gt 0) {
    if (-not (Install-WithChocolatey)) {
        Write-Warning "Chocolatey did not install Wireshark; using the official $FallbackVersion MSI instead."
        try {
            Install-FromOfficialMsi
        } catch {
            # The check below reports the missing tools.
            Write-Warning "Official MSI install failed: $($_.Exception.Message)"
        }
    }
}

$missing = @(Get-MissingTool)
if ($missing.Count -gt 0) {
    $message = "Wireshark install failed: $($missing -join ', ') not found in $InstallDir. " +
        'The decoder tests need these tools and must not run without them.'
    Write-Host "::error title=Wireshark install failed::$message"
    exit 1
}

$tshark = Join-Path $InstallDir 'tshark.exe'
try {
    $versionOutput = @(& $tshark --version 2>&1)
    $versionExitCode = $LASTEXITCODE
} catch {
    $versionOutput = @($_.Exception.Message)
    $versionExitCode = -1
}
if ($versionExitCode -ne 0) {
    Write-Host "::error title=Wireshark install failed::$tshark does not run (exit code $versionExitCode): $($versionOutput | Select-Object -First 1)"
    exit 1
}
Write-Host "Wireshark tools ready in ${InstallDir}: $($versionOutput | Select-Object -First 1)"
