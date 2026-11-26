# Installs CILogg with the real installer script while `cilogg.exe mcp serve` instances run from
# the installation folder and keep being restarted, the way AI agents run them (issue #191).
#
# The installer is built from packaging\windows\cilogg.nsi with CILOGG_TEST_INSTALL: it runs
# without administrator rights and leaves the registry, Start menu and Send To alone. Only
# processes this script starts are stopped, and only by their process ID.
param(
    [string]$NsisPath,
    [string]$OutputDirectory,
    # The folder cilogg.nsi takes its files from: release\ plus COPYING, NOTICE, README.md,
    # DOCUMENTATION.md and MCP.md. In CI, the workspace root after prepare_release.cmd.
    [string]$PackageRoot,
    [string]$Platform = $(if ($env:CILOGG_ARCH) { $env:CILOGG_ARCH } else { 'x64' }),
    [int]$StableServers = 4,
    [int]$RestartedServers = 4,
    [int]$RestartIntervalMs = 150
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (!$NsisPath) {
    $nsisCommand = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($nsisCommand) { $NsisPath = $nsisCommand.Source }
    else { $NsisPath = Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe' }
}
if (!(Test-Path -LiteralPath $NsisPath)) { throw "NSIS compiler not found: $NsisPath" }
if (!$PackageRoot) { $PackageRoot = $repoRoot }
$PackageRoot = (Resolve-Path $PackageRoot).Path
$releaseExe = Join-Path $PackageRoot 'release\cilogg.exe'
if (!(Test-Path -LiteralPath $releaseExe)) { throw "Packaged CILogg not found: $releaseExe" }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'build_root\reports\installer-processes' }
$runToken = [guid]::NewGuid().ToString('N').Substring(0, 12)
$workDirectory = [IO.Path]::GetFullPath((Join-Path $OutputDirectory $runToken))
New-Item -ItemType Directory -Path $workDirectory -Force | Out-Null

# A server that starts while its DLLs are being moved would look for them on PATH: keep every
# installed CILogg out of reach, so the test never loads files of another installation.
$env:PATH = (($env:PATH -split ';') | Where-Object {
        if (!$_) { return $false }
        try { return !(Test-Path -LiteralPath (Join-Path $_ 'cilogg.exe')) } catch { return $true }
    }) -join ';'

# Spaces and characters outside the ANSI code page, as in real user profiles.
$installDirectory = Join-Path $workDirectory ('Install folder ' + [char]0x6d4b + [char]0x8bd5)
$otherDirectory = Join-Path $workDirectory 'Other folder'
New-Item -ItemType Directory -Path $otherDirectory -Force | Out-Null

$helperSource = @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Threading;
public static class InstallerTestHelper {
    static StreamWriter log;
    static void Log(string text) {
        lock (log) { log.WriteLine(DateTime.Now.ToString("HH:mm:ss.fff") + " " + text); log.Flush(); }
    }
    static Process StartServer(string exe) {
        var info = new ProcessStartInfo(exe, "mcp serve");
        info.UseShellExecute = false;
        info.CreateNoWindow = true;
        // An open stdin keeps the MCP server waiting for requests, like an agent's.
        info.RedirectStandardInput = true;
        info.RedirectStandardOutput = true;
        info.RedirectStandardError = true;
        info.WorkingDirectory = Path.GetDirectoryName(exe);
        try {
            var process = Process.Start(info);
            Log("started " + process.Id);
            return process;
        } catch (Exception error) {
            Log("start-failed " + error.Message);
            return null;
        }
    }
    // supervise <cilogg.exe> <count> <interval ms> <stop file> <log file>: keeps <count> MCP
    // servers running and replaces the oldest one every interval.
    // sleep: an unrelated process for the "other folder" check.
    public static int Main(string[] args) {
        if (args.Length == 1 && args[0] == "sleep") {
            Thread.Sleep(Timeout.Infinite);
            return 0;
        }
        if (args.Length != 6 || args[0] != "supervise") return 2;
        string exe = args[1];
        int count = int.Parse(args[2]);
        int interval = int.Parse(args[3]);
        string stopFile = args[4];
        log = new StreamWriter(args[5], true);
        var servers = new List<Process>();
        while (!File.Exists(stopFile)) {
            servers.RemoveAll(server => {
                if (!server.HasExited) return false;
                Log("exited " + server.Id + " code " + server.ExitCode);
                return true;
            });
            while (servers.Count < count) {
                var server = StartServer(exe);
                if (server == null) break;
                servers.Add(server);
            }
            if (servers.Count == count) {
                var oldest = servers[0];
                servers.RemoveAt(0);
                Log("restarting " + oldest.Id);
                try { oldest.Kill(); oldest.WaitForExit(5000); } catch (Exception) { }
            }
            Thread.Sleep(interval);
        }
        foreach (var server in servers) {
            try { if (!server.HasExited) { server.Kill(); server.WaitForExit(5000); } } catch (Exception) { }
        }
        Log("stopped");
        log.Dispose();
        return 0;
    }
}
'@
$helperExe = Join-Path $workDirectory 'installer-test-helper.exe'
Add-Type -OutputAssembly $helperExe -OutputType ConsoleApplication -TypeDefinition $helperSource

$owned = [Collections.Generic.List[Diagnostics.Process]]::new()
function Start-Owned([string]$path, [string]$arguments, [switch]$KeepInputOpen) {
    $info = [Diagnostics.ProcessStartInfo]::new($path, $arguments)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WorkingDirectory = Split-Path -Parent $path
    if ($KeepInputOpen) {
        $info.RedirectStandardInput = $true
        $info.RedirectStandardOutput = $true
        $info.RedirectStandardError = $true
    }
    $process = [Diagnostics.Process]::Start($info)
    $owned.Add($process)
    return $process
}

function Build-Installer {
    $outFile = Join-Path $workDirectory 'cilogg-installer-test-setup.exe'
    $script = Join-Path $repoRoot 'packaging\windows\cilogg.nsi'
    Push-Location $PackageRoot
    try {
        # /NOCD: the File paths resolve against the package folder, as in CI.
        & $NsisPath /V2 /NOCD /DCILOGG_TEST_INSTALL "/DVERSION=installer-test" "/DPLATFORM=$Platform" `
            "/DQT_MAJOR=Qt6" "/DOUTFILE=$outFile" $script | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "NSIS compilation failed ($LASTEXITCODE)" }
    } finally {
        Pop-Location
    }
    return $outFile
}

function Invoke-Installer([string]$setup, [string]$name) {
    $log = Join-Path $workDirectory "$name-installer.log"
    $report = Join-Path $workDirectory "$name-report.txt"
    # The arguments cilogg_updater passes; /D= comes last and unquoted.
    $arguments = "/S /CILOGGLOG=`"$log`" /CILOGGREPORT=`"$report`" /D=$installDirectory"
    $started = [DateTime]::UtcNow
    $process = Start-Process -FilePath $setup -ArgumentList $arguments -PassThru -WindowStyle Hidden
    if (!$process.WaitForExit(900000)) { throw "$name installer timed out" }
    $process.Refresh()
    $seconds = [int]([DateTime]::UtcNow - $started).TotalSeconds
    Write-Host "$name installer exit $($process.ExitCode) after $seconds s; log $log"
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Log = if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Raw -Encoding Unicode } else { '' }
        Report = if (Test-Path -LiteralPath $report) { Get-Content -LiteralPath $report -Raw -Encoding UTF8 } else { '' }
    }
}

function Assert-Running([Diagnostics.Process[]]$processes, [string]$what) {
    foreach ($process in $processes) {
        $process.Refresh()
        if ($process.HasExited) { throw "$what (PID $($process.Id)) was stopped by the installer" }
    }
}

function Get-MovedAside {
    return @(Get-ChildItem -LiteralPath $installDirectory -Recurse -File -Filter '*.cilogg-old' -ErrorAction SilentlyContinue)
}

$stopFile = Join-Path $workDirectory 'stop-supervisor'
$supervisorLog = Join-Path $workDirectory 'supervisor.log'
$results = [Collections.Generic.List[object]]::new()
$heldFile = $null
try {
    $setup = Build-Installer
    $installedExe = Join-Path $installDirectory 'cilogg.exe'
    $expectedHash = (Get-FileHash -LiteralPath $releaseExe).Hash

    # 1. First installation into an empty folder.
    $first = Invoke-Installer $setup 'first'
    if ($first.ExitCode -ne 0) { throw "First installation failed with exit $($first.ExitCode)`n$($first.Log)" }
    if ((Get-FileHash -LiteralPath $installedExe).Hash -ne $expectedHash) { throw 'First installation wrote a different cilogg.exe' }
    $results.Add([pscustomobject]@{ case = 'first-install'; exitCode = $first.ExitCode })

    # 2. Update while MCP servers run from the folder and keep being restarted, and while a
    #    cilogg.exe from another folder runs.
    $otherExe = Join-Path $otherDirectory 'cilogg.exe'
    Copy-Item -LiteralPath $helperExe -Destination $otherExe
    $other = Start-Owned $otherExe 'sleep'
    $stable = @()
    for ($index = 0; $index -lt $StableServers; $index++) {
        $stable += Start-Owned $installedExe 'mcp serve' -KeepInputOpen
    }
    $supervisor = Start-Owned $helperExe ("supervise `"{0}`" {1} {2} `"{3}`" `"{4}`"" -f `
            $installedExe, $RestartedServers, $RestartIntervalMs, $stopFile, $supervisorLog)
    Start-Sleep -Seconds 3
    Assert-Running $stable 'An MCP server started before the update'

    $update = Invoke-Installer $setup 'update'
    if ($update.ExitCode -ne 0) { throw "Update failed with exit $($update.ExitCode)`n$($update.Log)`n$($update.Report)" }
    Assert-Running $stable 'An MCP server started before the update'
    Assert-Running @($other, $supervisor) 'A process outside the installation folder'
    if ((Get-FileHash -LiteralPath $installedExe).Hash -ne $expectedHash) { throw 'The update did not install cilogg.exe' }
    # The servers kept running from the files moved aside, which wait until no process uses them.
    if (@(Get-MovedAside).Count -eq 0) { throw 'No file in use was moved aside' }
    foreach ($expected in @('Running from the installation folder: cilogg.exe', 'Moved aside ', 'installed in ')) {
        if (!$update.Log.Contains($expected)) { throw "The installer log does not contain '$expected'`n$($update.Log)" }
    }
    $supervisorText = Get-Content -LiteralPath $supervisorLog -Raw
    $restarts = ([regex]::Matches($supervisorText, '(?m) started ')).Count
    $failedStarts = ([regex]::Matches($supervisorText, '(?m) start-failed ')).Count
    $results.Add([pscustomobject]@{ case = 'update-while-running'; exitCode = $update.ExitCode
            serverStarts = $restarts; failedStarts = $failedStarts; movedAside = @(Get-MovedAside).Count })

    # 3. A file kept open without sharing (an editor, a scanner) cannot be moved aside: the
    #    installation stops before replacing anything and names the process.
    $hashBefore = (Get-FileHash -LiteralPath $installedExe).Hash
    $heldPath = Join-Path $installDirectory 'README.md'
    $heldFile = [IO.File]::Open($heldPath, 'Open', 'Read', 'Read')
    $blocked = Invoke-Installer $setup 'blocked'
    $heldFile.Dispose()
    $heldFile = $null
    if ($blocked.ExitCode -ne 2) { throw "A held file should stop the installer with exit 2, got $($blocked.ExitCode)" }
    if (!$blocked.Report.Contains("blocker=$PID`t")) { throw "The report does not name process $PID`n$($blocked.Report)" }
    if (!$blocked.Report.Contains('README.md')) { throw "The report does not name the held file`n$($blocked.Report)" }
    if (!$blocked.Log.Contains('Setup cancelled before replacing files')) { throw "The installer log does not explain the failure`n$($blocked.Log)" }
    if ((Get-FileHash -LiteralPath $installedExe).Hash -ne $hashBefore -or !(Test-Path -LiteralPath $installedExe)) {
        throw 'A blocked installation changed cilogg.exe'
    }
    if (Test-Path -LiteralPath (Join-Path $installDirectory 'cilogg-update-moves.txt')) { throw 'A blocked installation left its manifest' }
    Assert-Running $stable 'An MCP server'
    $results.Add([pscustomobject]@{ case = 'blocked-by-held-file'; exitCode = $blocked.ExitCode })

    # 4. Once the old servers exit, the next update deletes what was moved aside.
    New-Item -ItemType File -Path $stopFile -Force | Out-Null
    if (!$supervisor.WaitForExit(30000)) { throw 'The supervisor did not stop' }
    foreach ($process in $stable) { $process.Kill(); $null = $process.WaitForExit(10000) }
    $again = Invoke-Installer $setup 'cleanup'
    if ($again.ExitCode -ne 0) { throw "Update after the servers exited failed with exit $($again.ExitCode)`n$($again.Log)" }
    if (@(Get-MovedAside).Count -ne 0) { throw "Files moved aside were not deleted: $(@(Get-MovedAside).FullName -join ', ')" }
    Assert-Running @($other) 'A process outside the installation folder'
    $results.Add([pscustomobject]@{ case = 'cleanup-after-exit'; exitCode = $again.ExitCode })

    foreach ($result in $results) { Write-Output "PASS: $($result.case) $($result | ConvertTo-Json -Compress)" }
} finally {
    if ($heldFile) { $heldFile.Dispose() }
    New-Item -ItemType File -Path $stopFile -Force -ErrorAction SilentlyContinue | Out-Null
    foreach ($process in $owned) {
        $process.Refresh()
        if (!$process.HasExited) { $process.Kill(); $null = $process.WaitForExit(10000) }
        $process.Dispose()
    }
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $workDirectory 'results.json') -Encoding utf8
Write-Output "All $($results.Count) Windows installer checks passed. Artifacts: $workDirectory"
