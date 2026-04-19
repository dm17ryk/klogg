# How to Build Klogg

## Overview

Klogg is actively developed and validated on Windows with Visual Studio 2022 and Qt 6.
This document keeps the practical build flow in one place and aligns it with the repo-local
instructions in `AGENTS.md`.

Rules that matter:

- Run CMake from `build_root`, never from the repository root.
- Prefer a clean Visual Studio developer shell without custom PowerShell profile state.
- For repeatable local automation, use the scripts under `scripts/codex`.

## Current Recommended Windows Setup

The current repo workflow assumes:

- Windows x64
- Visual Studio 2022 with MSVC tools
- Qt `6.10.1`
- an out-of-tree build directory at `build_root`

Open a clean Visual Studio 2022 developer shell:

```bat
cmd.exe /d /k "d:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" -startdir=none -arch=x64 -host_arch=x64
```

Or from PowerShell:

```powershell
pwsh.exe -NoExit -NoProfile -Command "&{Import-Module 'd:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'; Enter-VsDevShell 3487df70 -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'}"
```

Set the environment expected by the repo:

```bat
set QTDIR=C:\qt6.10.1
set PATH=%QTDIR%\bin;%PATH%
set CMAKE_PREFIX_PATH=%QTDIR%
set KLOGG_WORKSPACE=D:\Essence_SC\lsrc\klogg
set KLOGG_BUILD_ROOT=build_root
set platform=x64
set KLOGG_QT=Qt6
set KLOGG_QT_DIR=%QTDIR%
set PATH=%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%
set PATH=%VSINSTALLDIR%VC\vcpkg;%PATH%
set PATH=%USERPROFILE%\.pyenv\pyenv-win\shims;%PATH%
set PATH=%ProgramFiles(x86)%\Windows Kits\10\Debuggers\x64;%PATH%
d:
cd %KLOGG_WORKSPACE%
```

## Fast Path: Repo Scripts

For the current Windows workflow, prefer the repo helper scripts:

- `scripts/codex/build-windows.ps1`
- `scripts/codex/run-tests.ps1`
- `scripts/codex/run-klogg-debug.ps1`
- `scripts/codex/collect-artifacts.ps1`

Examples:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\codex\build-windows.ps1 -Config RelWithDebInfo
powershell -ExecutionPolicy Bypass -File .\scripts\codex\run-tests.ps1 -Config RelWithDebInfo
powershell -ExecutionPolicy Bypass -File .\scripts\codex\run-klogg-debug.ps1 -Config RelWithDebInfo
```

The build script configures and builds from `build_root`.
The test and run scripts also deploy the required Qt runtime files.

## Manual Windows Build

Create the build directory once:

```bat
mkdir build_root
cd build_root
```

Configure with Visual Studio 2022:

```bat
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release ..
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug ..
```

Build:

```bat
cmake --build . --config Release
cmake --build . --config RelWithDebInfo
cmake --build . --config Debug
```

Output binaries are placed under `build_root/output/<Config>`.

## Deploy Qt Runtime on Windows

Before running `klogg.exe` or the test executables, deploy the Qt runtime next to them:

```bat
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Release\klogg.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\RelWithDebInfo\klogg.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Debug\klogg.exe"
```

For tests:

```bat
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Release\klogg_itests.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\RelWithDebInfo\klogg_itests.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Debug\klogg_itests.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Release\klogg_tests.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\RelWithDebInfo\klogg_tests.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Debug\klogg_tests.exe"
```

## Running Tests

Tests are enabled by default. To disable them, pass:

```bat
-DKLOGG_BUILD_TESTS=OFF
```

Run tests from `build_root`:

```bat
ctest --build-config Release --verbose
ctest --build-config RelWithDebInfo --verbose
ctest --build-config Debug --verbose
```

For the current Windows flow, `scripts/codex/run-tests.ps1` is the easiest option because it
deploys Qt first and copies the `qoffscreen` platform plugin required by the UI tests.

## Running Klogg

After deploying Qt:

```bat
output\RelWithDebInfo\klogg.exe
```

For deterministic GUI automation runs, set:

```bat
set KLOGG_AUTOMATION=1
```

Or use:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\codex\run-klogg-debug.ps1 -Config RelWithDebInfo
```

## Important CMake Options

Backend selection:

- `-DKLOGG_USE_HYPERSCAN=ON|OFF`
- `-DKLOGG_USE_VECTORSCAN=ON|OFF`

Only one accelerated backend can be enabled at a time. If both are disabled, klogg uses Qt
regular expressions only.

Current defaults by target:

- Windows x64: Hyperscan
- Windows arm64: Vectorscan
- Linux x64: Hyperscan
- Linux arm64: Vectorscan
- macOS x64: Hyperscan
- macOS arm64: Vectorscan

Other useful options:

- `-DKLOGG_BUILD_TESTS=OFF` to skip tests
- `-DKLOGG_BUILD_DOCUMENTATION=OFF` to skip generated docs
- `-DKLOGG_USE_SENTRY=ON` to enable crash dump collection support
- `-DKLOGG_USE_MIMALLOC=OFF` to disable mimalloc where applicable
- `-DKLOGG_USE_LTO=OFF` to disable link-time optimization
- `-DKLOGG_GENERIC_CPU=ON` for more conservative CPU targeting

## Linux and macOS Notes

Klogg still supports Linux and macOS builds, and CI covers those paths. The current repo
documentation and helper scripts are Windows-first because that is the active development
environment.

Common requirements across non-Windows platforms:

- CMake
- C++17-capable compiler
- Qt 5.9+ or Qt 6
- Ragel for accelerated regex builds
- optional system installs of Hyperscan or Vectorscan, TBB, uchardet, and xxHash

Typical non-Windows configure flow:

```bash
mkdir build_root
cd build_root
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
cmake --build .
ctest --build-config RelWithDebInfo --verbose
```

For exact platform matrices and package settings, use `.github/workflows/ci-build.yml` as the
source of truth.

## Project Layout

- `src/`: application, UI, scripting, crash handling, version, and lab features
- `tests/unit` and `tests/ui`: unit and UI test suites
- `test_data/`: fixtures and sample logs
- `Resources/`: icons, resources, translations
- `cmake/`: shared CMake helpers and version/resource generation
- `3rdparty/`: vendored/build-time dependencies
- `packaging/`: release packaging assets
- `scripts/codex/`: repeatable Windows build/test/run automation

## Troubleshooting

- If CMake or MSVC tools are missing, confirm you are running from a VS 2022 developer shell.
- If Qt DLLs are missing at runtime, run `windeployqt` again for the target executable.
- If GUI automation is flaky, use `KLOGG_AUTOMATION=1` and collect screenshots/logs/artifacts before changing behavior.
- If you need Windows crash investigation, use:

```bat
cdb "output\Debug\klogg.exe"
```
