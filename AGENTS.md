# Repository Guidelines for CILogg (Windows / VS2022 / Qt6) - Codex Instructions

## Environment

We run from: Windows `x64 Native Tools Command Prompt for VS 2022`.

Before any CMake/build steps, ensure these env vars are set:

```bash
set QTDIR=C:\qt6.10.1
set PATH=%QTDIR%\bin;%PATH%
set CMAKE_PREFIX_PATH=%QTDIR%
set CILOGG_WORKSPACE=D:\Essence_SC\lsrc\klogg
set CILOGG_BUILD_ROOT=build_root
set platform=x64
set CILOGG_QT=Qt6
set CILOGG_QT_DIR=%QTDIR%
set PATH=%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%
set PATH=%VSINSTALLDIR%VC\vcpkg;%PATH%
set PATH=%USERPROFILE%\.pyenv\pyenv-win\shims;%PATH%
set PATH=%ProgramFiles(x86)%\Windows Kits\10\Debuggers\x64;%PATH%
d:
cd %CILOGG_WORKSPACE%
```

## Agents Terminal Setup

To prevent errors from profile modules or missing tools, use a clean terminal session with the necessary environment variables set. Avoid running CMake or build commands in a terminal with custom profiles that may interfere with the build process.

## Use Developer Command Prompt for VS 2022

Use the "x64 Native Tools Command Prompt for VS 2022" to ensure all necessary environment variables for MSVC, CMake, and Qt are set correctly. This avoids issues with missing tools or libraries during the build process.

```bash
cmd.exe /d /k "d:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" -startdir=none -arch=x64 -host_arch=x64
```

or

```bash
pwsh.exe -NoExit -NoProfile -Command "&{Import-Module """d:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"""; Enter-VsDevShell 3487df70 -SkipAutomaticLocation -DevCmdArguments """-arch=x64 -host_arch=x64"""}"
```

## Project Structure & Module Organization

Source lives in `src/` (Qt/C++17 app code and UI). Tests are split into
`tests/unit/` and `tests/ui/`, with fixtures in `test_data/`. Assets and
translations are under `Resources/` and referenced by `.qrc` files. Build logic
is centralized in the top-level `CMakeLists.txt` plus helpers in `cmake/`.
`3rdparty/` and `tools/` contain vendored deps and utilities, while
`packaging/`, `docker/`, and `website/` cover release and CI assets. Use an
out-of-tree build directory like `build_root/` for local builds.

## Configure, Build, Test, Run, and Development Commands

Common CMake flow (see `BUILD.md` for OS-specific options and dependencies):
All builds happen inside: `%CILOGG_WORKSPACE%\%CILOGG_BUILD_ROOT%`
(i.e. build_root)

```bash
mkdir build_root
cd build_root
```

## Configure (CMake generate)

From build_root:

```bash
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release ..
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug ..
```

## Build

From build_root:

```bash
cmake --build . --config Release
cmake --build . --config RelWithDebInfo
cmake --build . --config Debug
```

## Deploy QT libs

From build_root:

```bash
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Release\cilogg.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\RelWithDebInfo\cilogg.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Debug\cilogg.exe"
```

## Test

Tests are enabled by default; to disable: `-DBUILD_TESTS:BOOL=OFF`.
Run tests from the build directory:

Before you can run cilogg.exe, need to deploy Qt dlls to same directory.

```bash
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Release\cilogg_itests.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\RelWithDebInfo\cilogg_itests.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Debug\cilogg_itests.exe"

"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Release\cilogg_tests.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\RelWithDebInfo\cilogg_tests.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Debug\cilogg_tests.exe"
```

From build_root:

```bash
ctest --build-config Release --verbose
ctest --build-config RelWithDebInfo --verbose
ctest --build-config Debug --verbose
```

## Run

Before you can run cilogg.exe, need to deploy Qt dlls to same directory.

```bash
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Release\cilogg.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\RelWithDebInfo\cilogg.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Debug\cilogg.exe"
```

## Coding Style & Naming Conventions

Follow the Qt-flavored `.clang-format` (LLVM-based, 4-space indent, 100-column
limit, C++17). Use `.clang-tidy` for static checks when practical. CMake files
use `.cmake-format` (2-space indent, 120-column limit, lowercase commands).
Match existing naming: classes in CamelCase, files mostly lowercase with
underscores (tests use `*_test.cpp`).

## Testing Guidelines

Tests use Catch2 (bundled) plus Qt5Test. Add new tests under `tests/unit/` or
`tests/ui/` and place sample logs in `test_data/` when needed. Ensure tests
pass via `ctest` before opening a PR.

## Commit & Pull Request Guidelines

Commit messages follow `prefix: message` (see `CONTRIBUTING.md`), with prefixes
like `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`,
`chore`, `revert`, `tr`. Keep PRs small and focused, ensure Windows/macOS/Linux
compatibility, and open an issue first for major changes. Include a clear
description, steps to test, and screenshots for UI changes.

## Security

Do not file public issues for vulnerabilities. Follow `SECURITY.md` and report
through GitHub Security Advisories:
`https://github.com/dm17ryk/klogg/security/advisories/new`

## Rules of engagement

- Never run CMake in the repo root; always in build_root.
- Prefer minimal diffs. Keep changes localized.
- If you need to run commands, show them first; don't guess paths.

## Codex GUI Automation

- Repo-scoped Codex defaults live in `.codex/config.toml`; Win GUI MCP server registration stays user-local and is not committed here.
- Use `scripts/codex/build-windows.ps1`, `scripts/codex/run-tests.ps1`, `scripts/codex/run-cilogg-debug.ps1`, and `scripts/codex/collect-artifacts.ps1` for repeatable automation flows.
- Prefer Qt `objectName` and `accessibleName` targets first; only fall back to UIA or screen coordinates for custom-rendered panes.
- For deterministic GUI runs, set `CILOGG_AUTOMATION=1` or use `run-cilogg-debug.ps1`.
- Collect screenshots, logs, and dumps after GUI repros before changing behavior.

## Debugging & Troubleshooting

```bash
cdb "output\Debug\cilogg.exe"
```
