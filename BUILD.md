# How to Build Klogg

## Overview

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes.
Local builds can be faster because code can be optimized for current CPU instead of generic x86-64. Support for wider SIMD code paths
will be enabled when the selected backend and toolchain support them.

## Getting the Source

This project is [hosted on GitHub](https://github.com/dm17ryk/klogg). You can clone this project directly using this command:

```
git clone https://github.com/dm17ryk/klogg
```

## Dependencies

To build Klogg:

- cmake 3.12 or later to generate build files
- C++ compiler with decent C++17 support (at least gcc 7.5, clang 7, msvc 19.14)
- Qt libraries 5.9 or later (CI builds use Qt 5.9.5/5.12.5/5.15.2):
  - QtCore
  - QtGui
  - QtWidgets
  - QtConcurrent
  - QtNetwork
  - QtXml
  - QtTools
  - QtSerialPort

To build accelerated regular expression backends:

- CPU with support for [SSSE3](https://en.wikipedia.org/wiki/SSSE3) instructions (for Hyperscan backend)
- Boost (1.58 or later, header-only part)
- Ragel (6.8 or later; precompiled binary is provided for Windows; has to be installed from package managers on Linux or Homebrew on Mac)

To build installer for Windows:

- nsis to build installer for Windows
- Precompiled OpenSSl library to enable https support on Windows

Building tests:

- QtTest

All other dependencies are provided by [CPM](https://github.com/cpm-cmake/CPM.cmake) during cmake configuration stage (see 3rdparty directory).

CPM will try to find Hyperscan or Vectorscan, TBB, uchardet and xxhash installed on build host.
If a library can't be found, the one provided by CPM will be used.

## Building

### Configuration options

By default Klogg is built without support for reporting crash dumps. This can be enabled via cmake option `-DKLOGG_USE_SENTRY=ON`.

Klogg selects the accelerated regex backend by target OS and architecture:

- Windows x64: Hyperscan
- Linux x64: Hyperscan
- Linux arm64: Vectorscan
- macOS x64: Hyperscan
- macOS arm64: Vectorscan
- Windows arm64: Qt-only for now

Fat runtime is requested only on Linux accelerated builds:

- Linux x64 + Hyperscan: fat runtime requested, with AVX-512 and AVX-512VBMI enabled only when the toolchain supports them
- Linux arm64 + Vectorscan: fat runtime requested, with SVE/SVE2/SVE2_BITPERM requested and left to Vectorscan to degrade gracefully if the compiler does not support the strongest variant
- Windows and macOS accelerated builds: non-fat by design

Backend selection remains build-time. Manual overrides are supported with:

- `-DKLOGG_USE_HYPERSCAN=ON|OFF`
- `-DKLOGG_USE_VECTORSCAN=ON|OFF`

Only one accelerated backend can be enabled at a time. If both are disabled, klogg uses Qt regular expressions only. Windows arm64 intentionally keeps both accelerated backends disabled by default in this task.

Klogg can use custom memory allocator. By default it uses TBB memory allocator for Windows, mimalloc on Linux and default system allocator on MacOS.
Memory allocator override can be turned off by passing `-DKLOGG_OVERRIDE_MALLOC`. If you want to use TBB allocator on Linux then pass
`-DKLOGG_USE_MIMALLOC=OFF`.

### Building on Linux

Here is how to build klogg on Ubuntu.

Install dependencies:

```
sudo apt-get install build-essential cmake qtbase5-dev libboost-all-dev ragel
```

Configure and build klogg:

```
cd <path_to_klogg_repository_clone>
mkdir build_root
cd build_root
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
cmake --build .
```

**_If cmake gives error about missing "Qt5LinguistTools" configuration files, try running:_**

```bash
sudo apt-get install qttools5-dev
```

Binaries are placed into `build_root/output`.

See `.github/workflows/ci-build.yml` for more information on build process.

### Building on Windows

Install Microsoft Visual Studio 2017 or 2019 with C++ support.
Community edition can be downloaded from [Microsoft](https://visualstudio.microsoft.com/vs/).

Intall latest Qt version using [online installer](https://www.qt.io/download-qt-installer).
Make sure to select version matching Visual Studio installation. 64-bit libraries are recommended.

Install CMake from [Kitware](https://cmake.org/download/).
Use version 3.14 or later for Visual Studio 2019 support.

Download the Boost source code from http://www.boost.org/users/download/.
Extract to some folder. Directory structure should be something like `C:\Boost\boost_1_63_0`.
Then add `BOOST_ROOT` environment variable pointing to main directory of Boost sources so CMake is able to fine it.

Prepare build environment for CMake. Open command prompt window and depending on version of Visual Studio run either

```
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\Common7\Tools\vsdevcmd" -arch=x64
```

or

```
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\Common7\Tools\vsdevcmd" -arch=x64
```

Next setup Qt paths:

```
<path_to_qt_installation>\bin\qtenv2.bat
```

Then add CMake to PATH:

```
set PATH=<path_to_cmake_bin>:$PATH
```

Configure klogg solution (use CMake generator matching Visual Studio version):

```
cd <path_to_project_root>
md build_root
cd build_root
cmake -G "Visual Studio 16 2019 Win64" -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
```

CMake should generate `klogg.sln` file in `<path_to_project_root>\build_root` directory. Open solution and build it.

Binaries are placed into `build_root/output`.

For https network urls support download precompiled openssl library https://mirror.firedaemon.com/OpenSSL/openssl-1.1.1l-dev.zip.
Put libcrypto-1_1 and libssl-1_1 for desired architecture near klogg binaries.

### Building on Mac OS

Klogg requires macOS High Sierra (10.13) or higher.

Install [Homebrew](https://brew.sh/) using terminal:

```
/usr/bin/ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

Homebrew installer should also install xcode command line tools.

Download and install build dependencies:

```
brew install cmake ninja qt boost ragel
```

Usually path to qt installation looks like `/usr/local/Cellar/qt/5.14.0/lib/cmake/Qt5`

Configure and build klogg:

```
cd <path_to_klogg_repository_clone>
mkdir build_root
cd build_root
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DQt5_DIR=<path_to_qt_install> ..
cmake --build .
```

Binaries are placed into `build_root/output`.

By default, klogg will rely on cmake to figure out target MacOS version. Usually it uses build host version.
To override default cmake value pass an option `-DKLOGG_OSX_DEPLOYMENT_TARGET=<target>` to cmake during configuration step,
`<target>` is one of `10.14`, `10.15`, `11`, `12`. Klogg's target must be greater or equal to target used by Qt libraries.

## Deploy QT libs

From build_root:

```bash
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Release\klogg.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\RelWithDebInfo\klogg.exe"
"%QTDIR%\bin\windeployqt.exe" "%CD%\output\Debug\klogg.exe"
```

## Running tests

Tests are built by default. To turn them off pass `-DBUILD_TESTS:BOOL=OFF` to cmake.
Tests use catch2 (bundled with klogg sources) and require Qt5Test module. Tests can be run using ctest tool provider by CMake:

```
cd <path_to_klogg_repository_clone>
cd build_root
ctest --build-config RelWithDebInfo --verbose
```
