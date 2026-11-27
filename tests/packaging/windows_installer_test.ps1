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
    [int]$RestartIntervalMs = 150,
    # Only compile the installer, for a package built for another architecture than the host's.
    [switch]$CompileOnly
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

function Get-PeMachine([string]$path) {
    # The Machine field of the PE header: 0x8664 x64, 0xAA64 arm64.
    $bytes = [IO.File]::ReadAllBytes($path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($bytes.Length -lt $peOffset + 6 -or [BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) {
        throw "Not a Windows program: $path"
    }
    return [int][BitConverter]::ToUInt16($bytes, $peOffset + 4)
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

function Invoke-Installer([string]$setup, [string]$name, [string]$directory = $installDirectory) {
    # The arguments cilogg_updater passes; /D= comes last and unquoted.
    $arguments = "/S /D=$directory"
    $started = [DateTime]::UtcNow
    $process = Start-Process -FilePath $setup -ArgumentList $arguments -PassThru -WindowStyle Hidden
    if (!$process.WaitForExit(900000)) { throw "$name installer timed out" }
    $process.Refresh()
    $seconds = [int]([DateTime]::UtcNow - $started).TotalSeconds
    # The installer writes its log and report into the installation folder; keep a copy per run.
    $log = Join-Path $workDirectory "$name-installer.log"
    $report = Join-Path $workDirectory "$name-report.txt"
    foreach ($pair in @(@('cilogg-installer.log', $log), @('cilogg-installer-report.txt', $report))) {
        $source = Join-Path $directory $pair[0]
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $pair[1] -Force }
    }
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
    return @(Get-ChildItem -LiteralPath $installDirectory -Recurse -File -Filter '*.cilogg-old*' -ErrorAction SilentlyContinue)
}

$stopFile = Join-Path $workDirectory 'stop-supervisor'
$supervisorLog = Join-Path $workDirectory 'supervisor.log'
$results = [Collections.Generic.List[object]]::new()
$heldFile = $null
try {
    # Windows on arm64 also runs x64 programs: check that the package holds $Platform programs.
    $expectedMachine = @{ 'x64' = 0x8664; 'arm64' = 0xAA64 }[$Platform.ToLowerInvariant()]
    if ($null -eq $expectedMachine) { throw "Unknown platform: $Platform" }
    foreach ($image in @('cilogg.exe', 'cilogg_portable.exe', 'cilogg_grep.exe', 'cilogg_updater.exe')) {
        $machine = Get-PeMachine (Join-Path $PackageRoot "release\$image")
        if ($machine -ne $expectedMachine) {
            throw ("release\{0} is built for machine 0x{1:X4}, not {2} (0x{3:X4})" -f $image, $machine, $Platform, $expectedMachine)
        }
    }
    if ($CompileOnly) {
        # The cross-compiling arm64 build job: its binaries cannot run on the x64 runner. CI runs
        # this whole test on the same package on a native arm64 runner (Windows arm64 installer).
        $null = Build-Installer
        Write-Host "The $Platform installer compiled; the runtime checks run on a $Platform host."
        return
    }
    # The operating system's architecture, also when this PowerShell runs emulated.
    $hostArch = (Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment').PROCESSOR_ARCHITECTURE
    if ($Platform -ieq 'arm64' -and $hostArch -ine 'ARM64') {
        throw "The $Platform package cannot run on this $hostArch host: run this test on an arm64 host, or pass -CompileOnly"
    }
    # The installer runs its own copy of the helper from its temporary folder: it must start
    # there on this platform, without the Visual C++ runtime next to it.
    $probeDirectory = Join-Path $workDirectory 'helper-probe'
    New-Item -ItemType Directory -Path $probeDirectory | Out-Null
    $probeHelper = Join-Path $probeDirectory 'cilogg_updater.exe'
    Copy-Item -LiteralPath (Join-Path $PackageRoot 'release\cilogg_updater.exe') -Destination $probeHelper
    $probeOutput = & $probeHelper --installer-prepare (Join-Path $probeDirectory 'not installed') 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw ("The packaged cilogg_updater.exe does not run on its own: exit 0x{0:X8}`n{1}" -f $LASTEXITCODE, ($probeOutput -join "`n"))
    }
    $setup = Build-Installer
    $installedExe = Join-Path $installDirectory 'cilogg.exe'
    $expectedHash = (Get-FileHash -LiteralPath $releaseExe).Hash

    # 1. First installation into an empty folder.
    $first = Invoke-Installer $setup 'first'
    if ($first.ExitCode -ne 0) { throw "First installation failed with exit $($first.ExitCode)`n$($first.Log)" }
    if ((Get-FileHash -LiteralPath $installedExe).Hash -ne $expectedHash) { throw 'First installation wrote a different cilogg.exe' }
    $results.Add([pscustomobject]@{ case = 'first-install'; exitCode = $first.ExitCode })

    # 2. Update while MCP servers run from the folder and keep being restarted, while a
    #    cilogg.exe from another folder runs, and while another program installed into the same
    #    folder runs.
    $otherExe = Join-Path $otherDirectory 'cilogg.exe'
    Copy-Item -LiteralPath $helperExe -Destination $otherExe
    $other = Start-Owned $otherExe 'sleep'
    $foreignExe = Join-Path $installDirectory 'tool.exe'
    Copy-Item -LiteralPath $helperExe -Destination $foreignExe
    $foreign = Start-Owned $foreignExe 'sleep'
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
    Assert-Running @($foreign) 'Another program in the installation folder'
    if (!(Test-Path -LiteralPath $foreignExe) -or (Test-Path -LiteralPath "$foreignExe.cilogg-old")) {
        throw "The installer moved another program's file"
    }
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

    # 4. A new file that cannot be written (a folder in its place): the previous version is put
    #    back before setup ends.
    $noticePath = Join-Path $installDirectory 'NOTICE'
    Remove-Item -LiteralPath $noticePath -Force
    New-Item -ItemType Directory -Path $noticePath | Out-Null
    $copyFailed = Invoke-Installer $setup 'copy-failed'
    Remove-Item -LiteralPath $noticePath -Force
    if ($copyFailed.ExitCode -ne 3) { throw "A failed copy should end with exit 3, got $($copyFailed.ExitCode)`n$($copyFailed.Log)" }
    if (!(Test-Path -LiteralPath $installedExe) -or (Get-FileHash -LiteralPath $installedExe).Hash -ne $hashBefore) {
        throw "A failed copy did not restore cilogg.exe`n$($copyFailed.Log)"
    }
    if (Test-Path -LiteralPath (Join-Path $installDirectory 'cilogg-update-moves.txt')) { throw 'A failed copy left its manifest' }
    if (!$copyFailed.Log.Contains('Restored ')) { throw "The installer log does not show the restore`n$($copyFailed.Log)" }
    Assert-Running $stable 'An MCP server'
    $results.Add([pscustomobject]@{ case = 'copy-failed-restored'; exitCode = $copyFailed.ExitCode })

    # 5. Once the old servers exit, the next update deletes what was moved aside.
    New-Item -ItemType File -Path $stopFile -Force | Out-Null
    if (!$supervisor.WaitForExit(30000)) { throw 'The supervisor did not stop' }
    foreach ($process in $stable) { $process.Kill(); $null = $process.WaitForExit(10000) }
    $again = Invoke-Installer $setup 'cleanup'
    if ($again.ExitCode -ne 0) { throw "Update after the servers exited failed with exit $($again.ExitCode)`n$($again.Log)" }
    if (@(Get-MovedAside).Count -ne 0) { throw "Files moved aside were not deleted: $(@(Get-MovedAside).FullName -join ', ')" }
    Assert-Running @($other) 'A process outside the installation folder'
    $results.Add([pscustomobject]@{ case = 'cleanup-after-exit'; exitCode = $again.ExitCode })

    # 6. A first installation into a folder another program uses moves none of its files.
    $sharedDirectory = Join-Path $workDirectory 'Shared tools'
    New-Item -ItemType Directory -Path (Join-Path $sharedDirectory 'plugins') -Force | Out-Null
    $sharedExe = Join-Path $sharedDirectory 'tool.exe'
    Copy-Item -LiteralPath $helperExe -Destination $sharedExe
    $sharedLibrary = Join-Path $sharedDirectory 'plugins\tool-plugin.dll'
    Set-Content -LiteralPath $sharedLibrary -Value 'another program' -Encoding ascii
    $shared = Start-Owned $sharedExe 'sleep'
    $sharedInstall = Invoke-Installer $setup 'shared-folder' $sharedDirectory
    if ($sharedInstall.ExitCode -ne 0) { throw "Installing into a shared folder failed with exit $($sharedInstall.ExitCode)`n$($sharedInstall.Log)" }
    Assert-Running @($shared) 'Another program in the shared folder'
    if ((Get-Content -LiteralPath $sharedLibrary -Raw).Trim() -ne 'another program') { throw "The installer changed another program's file" }
    if (@(Get-ChildItem -LiteralPath $sharedDirectory -Recurse -File -Filter '*.cilogg-old*').Count -ne 0) {
        throw 'A first installation moved files aside'
    }
    $results.Add([pscustomobject]@{ case = 'first-install-into-shared-folder'; exitCode = $sharedInstall.ExitCode })

    # 7. A first installation that fails (a folder in place of NOTICE) removes what it wrote and
    #    nothing else.
    $failedDirectory = Join-Path $workDirectory 'Failed first'
    New-Item -ItemType Directory -Path (Join-Path $failedDirectory 'plugins') -Force | Out-Null
    $failedLibrary = Join-Path $failedDirectory 'plugins\tool-plugin.dll'
    Set-Content -LiteralPath $failedLibrary -Value 'another program' -Encoding ascii
    New-Item -ItemType Directory -Path (Join-Path $failedDirectory 'NOTICE') | Out-Null
    $failedFirst = Invoke-Installer $setup 'failed-first' $failedDirectory
    if ($failedFirst.ExitCode -ne 3) { throw "A failed first installation should end with exit 3, got $($failedFirst.ExitCode)`n$($failedFirst.Log)" }
    foreach ($written in @('cilogg.exe', 'COPYING')) {
        if (Test-Path -LiteralPath (Join-Path $failedDirectory $written)) { throw "A failed first installation left $written`n$($failedFirst.Log)" }
    }
    if ((Get-Content -LiteralPath $failedLibrary -Raw).Trim() -ne 'another program') { throw "A failed first installation changed another program's file" }
    if (Test-Path -LiteralPath (Join-Path $failedDirectory 'cilogg-update-moves.txt')) { throw 'A failed first installation left its manifest' }
    $results.Add([pscustomobject]@{ case = 'failed-first-install-removed'; exitCode = $failedFirst.ExitCode })

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
