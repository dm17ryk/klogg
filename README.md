<img width="1024" height="360" alt="logo" src="https://github.com/user-attachments/assets/c0ee8045-078a-4fe1-a422-dc3c8565664f" />

[![GitHub license](https://img.shields.io/github/license/dm17ryk/klogg.svg?style=flat)](https://github.com/dm17ryk/klogg/blob/master/COPYING)
[![C++](https://img.shields.io/github/languages/top/dm17ryk/klogg?style=flat)]()
[![GitHub contributors](https://img.shields.io/github/contributors/dm17ryk/klogg.svg?style=flat)](https://github.com/dm17ryk/klogg/graphs/contributors/)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat)](http://makeapullrequest.com)
[![Codacy Badge](https://api.codacy.com/project/badge/Grade/f6db6ef0be3a4a5abff94111a5291c45)](https://www.codacy.com/manual/dm17ryk/klogg?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=dm17ryk/klogg&amp;utm_campaign=Badge_Grade)


[![Github all releases](https://img.shields.io/github/downloads/dm17ryk/klogg/total?style=flat)](https://github.com/dm17ryk/klogg/releases/)
[ ![Github](https://img.shields.io/github/v/release/dm17ryk/klogg?style=flat&label=Stable%20release&)](https://github.com/dm17ryk/klogg/releases/latest)

[![Packaging status](https://repology.org/badge/vertical-allrepos/klogg.svg)](https://repology.org/project/klogg/versions)

Check [GitHub releases](https://github.com/dm17ryk/klogg/releases/latest) for Windows installers and Linux/Mac packages.

Development status

[![Next milestone](https://img.shields.io/github/milestones/progress-percent/dm17ryk/klogg/4?style=flat&)](https://github.com/dm17ryk/klogg/milestone/4)
[![Ready for testing](https://img.shields.io/github/issues-raw/dm17ryk/klogg/status:%20ready%20for%20testing?color=green&label=issues%20ready%20for%20testing&style=flat)](https://github.com/dm17ryk/klogg/issues?q=is%3Aopen+is%3Aissue+label%3A%22status%3A+ready+for+testing%22)
[![Need documentation](https://img.shields.io/github/issues-search/dm17ryk/klogg?color=yellow&label=features%20need%20documentation&query=is%3Aissue%20label%3A%22status%3A%20need%20documentation%22&style=flat)](https://github.com/dm17ryk/klogg/issues?q=is%3Aissue+label%3A%22status%3A+need+documentation%22)
[![GitHub commits](https://img.shields.io/github/commits-since/dm17ryk/klogg/v26.04.svg?style=flat)](https://github.com/dm17ryk/klogg/commits/)
[![CI Build and Release](https://github.com/dm17ryk/klogg/actions/workflows/ci-build.yml/badge.svg)](https://github.com/dm17ryk/klogg/actions/workflows/ci-build.yml)

[![Chat on Discord](https://img.shields.io/discord/838452586944266260?label=Discord&style=flat)](https://discord.gg/DruNyQftzB) [![Join the chat at https://gitter.im/klogg_log_viewer/community](https://badges.gitter.im/klogg_log_viewer/community.svg)](https://gitter.im/klogg_log_viewer/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

## Overview

Klogg is a multi-platform GUI application that helps browse and search
through long and complex log files. It is designed with programmers and
system administrators in mind and can be seen as a graphical, interactive
combination of grep, less, and tail.

![Klogg main window](website/static/screenshots/mainwindow.png)

Please refer to the
[documentation](DOCUMENTATION.md)
page for how to use Klogg.

### Latest testing builds

| Windows | Linux | Mac |
| ------------- |------------- | ------------- |
| [continuous-win](https://github.com/dm17ryk/klogg/releases/tag/continuous-win) | [continuous-linux](https://github.com/dm17ryk/klogg/releases/tag/continuous-linux) | [continuous-osx](https://github.com/dm17ryk/klogg/releases/tag/continuous-osx) |

I try to keep a [changelog](CHANGELOG.md) with monthly changes. 

## Table of Contents

- [Overview](#overview)
  - [Latest testing builds](#latest-testing-builds)
- [Table of Contents](#table-of-contents)
- [About the Project](#about-the-project)
  - [Comparing with glogg](#comparing-with-glogg)
  - [Current Functionality](#current-functionality)
- [Installation](#installation)
  - [Current stable release builds](#current-stable-release-builds)
    - [Windows](#windows)
    - [Mac OS](#mac-os)
    - [Linux](#linux)
  - [Testing builds](#testing-builds)
- [Building](#building)
- [How to Get Help](#how-to-get-help)
- [Contributing](#contributing)
- [License](#license)
- [Authors](#authors)

## About the Project

Klogg started as a fork of [glogg](https://github.com/nickbnf/glogg) - the fast, smart log explorer in 2016.

Since then it has evolved from fixing small annoying bugs to rewriting core components to
make it faster and smarter that predecessor.

Development of klogg is driven by features my colleagues and I need
to stay productive as well as feature requests from users on GitHub and in GitHub Discussions.

Latest development updates can be found in [GitHub Discussions](https://github.com/dm17ryk/klogg/discussions) and [GitHub Releases](https://github.com/dm17ryk/klogg/releases).

### Comparing with glogg

Klogg has all best features of glogg:

* Runs on Unix-like systems, Windows and Mac thanks to Qt5
* Is fast and reads the file directly from disk, without loading it into memory
* Can operate on huge text files (10+ Gb is not a problem)
* Search results are displayed separately from original file
* Supports Perl-compatible regular expressions
* Colorizes the log and search results
* Displays a context view of where in the log the lines of interest are
* Watches for file changes on disk and reloads it (kind of like tail)
* Is open source, released under the GPL

And on top of that klogg:

* Is heavily optimized using multi-threading and SIMD
* Supports files with more than 2147483647 lines
* Includes much faster regular expressions search (2-4 times)
* Allows combining regular expressions with boolean operators (AND, OR, NOT)
* Supports many common text encodings
* Detects file encoding automatically using [uchardet](https://www.freedesktop.org/wiki/Software/uchardet/) library (supports utf8, utf16, cp1251 and more) 
* Can limit search operations to some part of huge file
* Allows to configure several highlighters sets and switch between them
* Has a list of configurable predefined regular expression patterns
* Includes a dark mode
* Has configurable shortcuts
* Has a scratchpad window for taking notes and doing basic data transformations
* Provides lots of small features that make life easier (closing tabs, copying file paths, favorite files menu, etc.)

### Current Functionality

Current klogg builds include the following major capabilities:

* Open logs from files, clipboard contents, URLs, and COM port streams
* Follow actively changing files, reload files manually, wrap lines, and show line numbers independently in main and filtered views
* Search with accelerated regular expressions, combine expressions with boolean operators, and restrict filtering/search to selected file regions
* Maintain predefined filters, reusable highlighter sets, quick highlights, and color labels for visual triage
* Detect encodings automatically, switch encodings manually, and work with common UTF and legacy code pages
* Keep multiple documents open at once with recent files, favorites, and fast switching between tabs and windows
* Use the scratchpad, previewer, and actions/responses tools for local analysis workflows
* Run Python scripts, scenario runs, and remote lab workflows through the built-in scripting and lab tooling
* Generate diagnostic crash dumps and open prefilled GitHub issues for crash reporting
* Drive the app from automation-oriented CLI commands, including commander actions and UI/state dump output

Klogg is no longer just a desktop log viewer. In the current codebase it also provides:

* Headless scenario execution via `klogg scenario`
* Remote lab controller, agent, and operator flows via `klogg lab-controller`, `klogg lab-agent`, and `klogg lab`
* Automation inspection helpers such as `klogg --dump-ui-tree` and `klogg --dump-state-json`
* Commander actions for opening files, URLs, COM ports, managing scripts, filters, actions, responses, and tab state

Here is a small demo showing how much faster klogg is (searching in ~1Gb file stored on tmpfs):

https://user-images.githubusercontent.com/1620716/117588567-bea39100-b12c-11eb-990a-90a667bcaeaa.mp4

List of glogg issues that have been fixed/implemented in klogg can be found [here](https://github.com/dm17ryk/klogg/discussions/302).

List of all changes can be found [here](https://github.com/dm17ryk/klogg/milestone/8?closed=1).

**[Back to top](#table-of-contents)**

## Installation

This project uses [Calendar Versioning](https://calver.org/). For a list of available versions, see the [repository tag list](https://github.com/dm17ryk/klogg/tags).

### Current stable release builds

Binaries for all platforms can be downloaded from GitHub releases.

[ ![Release](https://img.shields.io/github/v/release/dm17ryk/klogg?style=flat)](https://github.com/dm17ryk/klogg/releases/latest)

#### Windows
Windows installer is also available from:

* [ ![Chocolatey](https://img.shields.io/chocolatey/v/klogg?style=flat)](https://chocolatey.org/packages/klogg)
* [ ![Scoop Extras bucket](https://img.shields.io/scoop/v/klogg?bucket=extras)](https://scoopsearch.github.io/#/apps?q=klogg)
* [Winget package](https://winget.run/pkg/dm17ryk/klogg) 

#### Mac OS
Package for Mac can be installed from Homebrew

[ ![homebrew cask](https://img.shields.io/homebrew/cask/v/klogg?style=flat)](https://formulae.brew.sh/cask/klogg)

#### Linux
It is recommended to use klogg package from distribution-specific [repositories](https://repology.org/project/klogg/versions).

Generic packages are published through [GitHub Releases](https://github.com/dm17ryk/klogg/releases/latest).
If your distribution does not already package klogg, use the release artifacts directly or the AppImage package described below.

There is also an AppImage package that can be used without installation. To run klogg from AppImage, download the package and make in executable with either a file manager or terminal command `chmod +x <path_to_klogg_AppImage>` and then run the AppImage file.

AppImage uses FUSE2 and Ubuntu 22.04 has moved away from FUSE2 into FUSE3 and therefore you need to install the necessary package to enable compatibility with FUSE2 `sudo apt install libfuse2`.

As indicated by this link from the official appimage documentation: https://docs.appimage.org/user-guide/troubleshooting/fuse.html#setting-up-fuse-2-x-alongside-of-fuse-3-x-on-recent-ubuntu-22-04-debian-and-their-derivatives

### Testing builds

![CI Build and Release](https://github.com/dm17ryk/klogg/workflows/CI%20Build%20and%20Release/badge.svg)

| Windows | Linux | Mac |
| ------------- |------------- | ------------- |
| [continuous-win](https://github.com/dm17ryk/klogg/releases/tag/continuous-win) | [continuous-linux](https://github.com/dm17ryk/klogg/releases/tag/continuous-linux) | [continuous-osx](https://github.com/dm17ryk/klogg/releases/tag/continuous-osx) |

**[Back to top](#table-of-contents)**

## Building

The current recommended development environment is Windows x64 with Visual Studio 2022 and Qt 6.10.1.
Please review [BUILD.md](BUILD.md) for the exact current setup, environment variables, `build_root`
workflow, and the repo helper scripts under `scripts/codex`.

Typical Windows local flow:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\codex\build-windows.ps1 -Config RelWithDebInfo
powershell -ExecutionPolicy Bypass -File .\scripts\codex\run-tests.ps1 -Config RelWithDebInfo
```

If you build manually, keep all CMake configure/build steps inside `build_root`, not the repo root.

## How to Get Help

First, please refer to the
[documentation](DOCUMENTATION.md)
page.

You can open issues using the [klogg issues page](https://github.com/dm17ryk/klogg/issues)
or ask questions in [GitHub Discussions](https://github.com/dm17ryk/klogg/discussions).

## Contributing

We encourage public contributions! Please review [CONTRIBUTING.md](CONTRIBUTING.md) for details on our code of conduct and development process.

## License

This project is licensed under the GPLv3 or later - see [COPYING](COPYING) file for details.

## Authors

* **[Dmitry Kokotov](https://github.com/dm17ryk)**
* *Initial work* - **[Anton Filimonov](https://github.com/variar)**
* *Initial work* - **[Nicolas Bonnefon](https://github.com/nickbnf)**

See also the list of [contributors](https://github.com/dm17ryk/klogg/graphs/contributors) who participated in this project.

**[Back to top](#table-of-contents)**
