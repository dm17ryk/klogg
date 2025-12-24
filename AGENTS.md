# Repository Guidelines for Klogg (Windows / VS2022 / Qt6) - Codex Instructions

## Environment

We run from: Windows `x64 Native Tools Command Prompt for VS 2022`.

Before any CMake/build steps, ensure these env vars are set:

```bash
set QTDIR=C:\qt6
set PATH=%QTDIR%\bin;%PATH%
set CMAKE_PREFIX_PATH=%QTDIR%
set KLOGG_WORKSPACE=C:\Essence_SC\lsrc\klogg
set KLOGG_BUILD_ROOT=build_root
set platform=x64
set KLOGG_QT=Qt6
set KLOGG_QT_DIR=C:\qt6
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
All builds happen inside: `%KLOGG_WORKSPACE%\%KLOGG_BUILD_ROOT%`
(i.e. build_root)

```bash
mkdir build_root
cd build_root
```

## Configure (CMake generate)

From build_root:

```bash
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
```

## Build

From build_root:

```bash
cmake --build . --config RelWithDebInfo
```

## Test

Tests are enabled by default; to disable: `-DBUILD_TESTS:BOOL=OFF`.
Run tests from the build directory:

From build_root:

```bash
ctest --build-config RelWithDebInfo --verbose
```

## Run

Before you can run klogg.exe, need to deploy Qt dlls to same directory.

```bash
C:\qt6\bin\windeployqt.exe output\RelWithDebInfo\klogg.exe
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
to `security@filimonov.dev`.

## Rules of engagement

- Never run CMake in the repo root; always in build_root.
- Prefer minimal diffs. Keep changes localized.
- If you need to run commands, show them first; don't guess paths.
