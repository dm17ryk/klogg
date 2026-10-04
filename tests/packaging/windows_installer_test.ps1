param(
    [string]$NsisPath,
    [string]$OutputDirectory
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
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'build_root\reports\installer-processes' }
$runToken = [guid]::NewGuid().ToString('N').Substring(0, 12)
$workDirectory = [IO.Path]::GetFullPath((Join-Path $OutputDirectory $runToken))
New-Item -ItemType Directory -Path $workDirectory -Force | Out-Null

$source = Get-Content -LiteralPath (Join-Path $repoRoot 'packaging\windows\cilogg.nsi') -Raw
$functions = ''
foreach ($functionName in @('StopCILoggProcessesByImage', 'StopRunningCILogg')) {
    $match = [regex]::Match($source, '(?ms)^Function ' + $functionName + '\r?\n.*?^FunctionEnd')
    if (!$match.Success) { throw "Production NSIS function missing: $functionName" }
    $functions += $match.Value + "`r`n"
}
# Remap executable names so these real shutdown probes cannot touch installed CILogg instances.
$imageMap = [ordered]@{}
foreach ($image in @('cilogg.exe', 'cilogg_portable.exe', 'cilogg_grep.exe',
        'cilogg_crashpad_handler.exe', 'cilogg_minidump_dump.exe', 'cilogg_updater.exe')) {
    $alias = "CiloggProbe$runToken$($imageMap.Count).exe"
    $imageMap[$image] = $alias
    $functions = $functions.Replace('"' + $image + '"', '"' + $alias + '"')
}
foreach ($push in [regex]::Matches($functions, 'Push "([^\"]+\.exe)"')) {
    if (!$push.Groups[1].Value.StartsWith("CiloggProbe$runToken")) {
        throw 'The shutdown probe contains an executable name outside its isolated fixtures'
    }
}
$functions = [regex]::Replace($functions, '(?m)^(\s*)DetailPrint "([^\r\n]*)"',
    '$1DetailPrint "$2"' + "`r`n" + '$1FileWrite $ProbeLog "$2$\r$\n"')

$workerExe = Join-Path $workDirectory 'worker.exe'
Add-Type -OutputAssembly $workerExe -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
public static class InstallerProcessProbeWorker {
    public static int Main(string[] args) {
        if (args.Length == 3 && args[0] == "launch") {
            var info = new ProcessStartInfo(args[1], "/S");
            info.UseShellExecute = false;
            info.CreateNoWindow = true;
            using (var child = Process.Start(info)) {
                File.WriteAllText(args[2], child.Id.ToString());
            }
        }
        Thread.Sleep(120000);
        return 0;
    }
}
'@
$failureExe = Join-Path $workDirectory 'failure.exe'
Add-Type -OutputAssembly $failureExe -OutputType ConsoleApplication -TypeDefinition @'
using System;
public static class InstallerProcessProbeFailure {
    public static int Main() {
        Console.WriteLine("Simulated process command failure");
        return 5;
    }
}
'@
$payload = Join-Path $workDirectory 'replacement.txt'
[IO.File]::WriteAllText($payload, 'installer replacement probe')
$results = [Collections.Generic.List[object]]::new()

function ConvertTo-NsisLiteral([string]$value) {
    return $value.Replace('$', '$$').Replace('"', '$\"')
}

function Start-OwnedProcess([string]$path, [string]$arguments,
        [Collections.Generic.List[Diagnostics.Process]]$owned) {
    $info = [Diagnostics.ProcessStartInfo]::new($path, $arguments)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $process = [Diagnostics.Process]::Start($info)
    $owned.Add($process)
    return $process
}

foreach ($case in @('none', 'multiple', 'installer-child', 'query-failure', 'kill-failure')) {
    $caseDirectory = Join-Path $workDirectory $case
    $firstFolder = Join-Path $caseDirectory 'Folder with spaces'
    $secondFolder = Join-Path $caseDirectory ('Unicode ' + [char]0x6d4b + [char]0x8bd5)
    New-Item -ItemType Directory -Path $firstFolder, $secondFolder -Force | Out-Null
    Copy-Item -LiteralPath $failureExe -Destination (Join-Path $caseDirectory 'failure.exe')
    $probeFunctions = $functions
    if ($case -eq 'query-failure') {
        $probeFunctions = $probeFunctions.Replace('$SYSDIR\tasklist.exe', '$EXEDIR\failure.exe')
    }
    if ($case -eq 'kill-failure') {
        $probeFunctions = $probeFunctions.Replace('$SYSDIR\taskkill.exe', '$EXEDIR\failure.exe')
    }
    $probeScript = Join-Path $caseDirectory 'probe.nsi'
    $probeExe = Join-Path $caseDirectory 'probe.exe'
    $exitFile = Join-Path $caseDirectory 'exit-code.txt'
    $traceFile = Join-Path $caseDirectory 'shutdown.log'
    $mainImage = $imageMap['cilogg.exe']
    $installedFile = Join-Path $firstFolder $mainImage
    $template = @'
Unicode true
Name "CILogg installer shutdown probe"
OutFile "@@EXE@@"
RequestExecutionLevel user
SilentInstall silent
!include "LogicLib.nsh"
!include "StrFunc.nsh"
${StrCase}
${StrStr}
Var ProbeLog
@@FUNCTIONS@@
Function .onInstSuccess
    FileClose $ProbeLog
    FileOpen $0 "@@EXIT@@" w
    FileWrite $0 "0"
    FileClose $0
FunctionEnd
Function .onInstFailed
    FileClose $ProbeLog
    FileOpen $0 "@@EXIT@@" w
    FileWrite $0 "2"
    FileClose $0
FunctionEnd
Section
    FileOpen $ProbeLog "@@TRACE@@" w
    Call StopRunningCILogg
    SetOutPath "@@INSTALL@@"
    SetOverwrite on
    File "/oname=@@IMAGE@@" "@@PAYLOAD@@"
SectionEnd
'@
    $template = $template.Replace('@@FUNCTIONS@@', $probeFunctions)
    foreach ($entry in @{
            '@@EXE@@' = $probeExe; '@@EXIT@@' = $exitFile; '@@TRACE@@' = $traceFile
            '@@INSTALL@@' = $firstFolder; '@@IMAGE@@' = $mainImage; '@@PAYLOAD@@' = $payload
        }.GetEnumerator()) {
        $template = $template.Replace($entry.Key, (ConvertTo-NsisLiteral $entry.Value))
    }
    [IO.File]::WriteAllText($probeScript, $template, [Text.UTF8Encoding]::new($false))
    & $NsisPath /V2 /WX $probeScript
    if ($LASTEXITCODE -ne 0) { throw "NSIS probe compilation failed: $case" }

    $owned = [Collections.Generic.List[Diagnostics.Process]]::new()
    $targets = [Collections.Generic.List[Diagnostics.Process]]::new()
    $child = $null
    try {
        $sentinelFile = Join-Path $caseDirectory $imageMap['cilogg_updater.exe']
        Copy-Item -LiteralPath $workerExe -Destination $sentinelFile
        $sentinel = Start-OwnedProcess $sentinelFile 'updater-sentinel' $owned
        if ($case -ne 'none') {
            foreach ($image in $imageMap.Keys | Where-Object { $_ -ne 'cilogg_updater.exe' }) {
                $count = if ($image -eq 'cilogg.exe' -and $case -eq 'multiple') { 6 } else { 1 }
                for ($index = 0; $index -lt $count; $index++) {
                    $folder = if ($index % 2 -eq 0) { $firstFolder } else { $secondFolder }
                    $path = Join-Path $folder $imageMap[$image]
                    if (!(Test-Path -LiteralPath $path)) {
                        Copy-Item -LiteralPath $workerExe -Destination $path
                    }
                    if ($case -eq 'installer-child' -and $image -eq 'cilogg.exe') { continue }
                    $targets.Add((Start-OwnedProcess $path '--mcp' $owned))
                }
            }
        }
        $initialHash = if (Test-Path -LiteralPath $installedFile) {
            (Get-FileHash -LiteralPath $installedFile).Hash
        } else { '' }
        if ($case -eq 'installer-child') {
            $pidFile = Join-Path $caseDirectory 'installer-pid.txt'
            $launcher = Start-OwnedProcess $installedFile ('launch "{0}" "{1}"' -f $probeExe, $pidFile) $owned
            $targets.Add($launcher)
            $deadline = [DateTime]::UtcNow.AddSeconds(60)
            while (!(Test-Path -LiteralPath $exitFile) -and [DateTime]::UtcNow -lt $deadline) {
                Start-Sleep -Milliseconds 100
            }
            if (Test-Path -LiteralPath $pidFile) {
                $childId = [int](Get-Content -LiteralPath $pidFile -Raw)
                $child = Get-Process -Id $childId -ErrorAction SilentlyContinue
            }
            if (!(Test-Path -LiteralPath $exitFile)) {
                throw 'The installer did not complete after shutting down its parent MCP process'
            }
            $exitCode = [int](Get-Content -LiteralPath $exitFile -Raw)
        } else {
            $installer = Start-OwnedProcess $probeExe '/S' $owned
            if (!$installer.WaitForExit(60000)) { throw "Installer probe timed out: $case" }
            $installer.Refresh()
            $exitCode = $installer.ExitCode
        }
        $expectedFailure = $case -in @('query-failure', 'kill-failure')
        if ($expectedFailure) {
            if ($exitCode -ne 2) { throw "$case expected installer failure 2, got $exitCode" }
            if ((Get-FileHash -LiteralPath $installedFile).Hash -ne $initialHash) {
                throw "$case replaced files despite an incomplete process shutdown"
            }
            foreach ($process in $targets) {
                $process.Refresh()
                if ($process.HasExited) { throw "$case unexpectedly terminated a fixture" }
            }
        } else {
            if ($exitCode -ne 0) { throw "$case expected installer success, got $exitCode" }
            foreach ($process in $targets) {
                if (!$process.WaitForExit(5000)) { throw "$case left process $($process.Id) running" }
            }
            if ([IO.File]::ReadAllText($installedFile) -ne [IO.File]::ReadAllText($payload)) {
                throw "$case did not replace the executable after stopping its instances"
            }
        }
        $sentinel.Refresh()
        if ($sentinel.HasExited) { throw "$case terminated the updater sentinel" }
        $results.Add([pscustomobject]@{case = $case; passed = $true; targets = $targets.Count; exitCode = $exitCode})
        Write-Output "PASS: $case ($($targets.Count) target processes, installer exit $exitCode)"
    } finally {
        if ($child) {
            $child.Refresh()
            if (!$child.HasExited -and $child.MainModule.FileName -eq $probeExe) { $child.Kill() }
            $child.Dispose()
        }
        foreach ($process in $owned) {
            $process.Refresh()
            if (!$process.HasExited) { $process.Kill(); $null = $process.WaitForExit(5000) }
            $process.Dispose()
        }
    }
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $workDirectory 'results.json') -Encoding utf8
Write-Output "All $($results.Count) Windows installer shutdown checks passed. Artifacts: $workDirectory"
