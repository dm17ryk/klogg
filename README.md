![media_small](https://github.com/dm17ryk/klogg/blob/master/Resources/logo.png)

[![GitHub license](https://img.shields.io/github/license/dm17ryk/klogg.svg?style=flat)](https://github.com/dm17ryk/klogg/blob/master/COPYING)
[![C++](https://img.shields.io/github/languages/top/dm17ryk/klogg?style=flat)]()
[![GitHub contributors](https://img.shields.io/github/contributors/dm17ryk/klogg.svg?style=flat)](https://github.com/dm17ryk/klogg/graphs/contributors/)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat)](http://makeapullrequest.com)
[![Codacy Badge](https://api.codacy.com/project/badge/Grade/f6db6ef0be3a4a5abff94111a5291c45)](https://www.codacy.com/manual/dm17ryk/klogg?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=dm17ryk/klogg&amp;utm_campaign=Badge_Grade)


[![Github all releases](https://img.shields.io/github/downloads/dm17ryk/klogg/total?style=flat)](https://github.com/dm17ryk/klogg/releases/)
[ ![Github](https://img.shields.io/github/v/release/dm17ryk/klogg?style=flat&label=Stable%20release&)](https://github.com/dm17ryk/klogg/releases/latest)

[![Packaging status](https://repology.org/badge/vertical-allrepos/cilogg.svg)](https://repology.org/project/cilogg/versions)

Check [GitHub releases](https://github.com/dm17ryk/klogg/releases/latest) for Windows installers and Linux/Mac packages.

Development status

[![Next milestone](https://img.shields.io/github/milestones/progress-percent/dm17ryk/klogg/4?style=flat&)](https://github.com/dm17ryk/klogg/milestone/4)
[![Ready for testing](https://img.shields.io/github/issues-raw/dm17ryk/klogg/status:%20ready%20for%20testing?color=green&label=issues%20ready%20for%20testing&style=flat)](https://github.com/dm17ryk/klogg/issues?q=is%3Aopen+is%3Aissue+label%3A%22status%3A+ready+for+testing%22)
[![Need documentation](https://img.shields.io/github/issues-search/dm17ryk/klogg?color=yellow&label=features%20need%20documentation&query=is%3Aissue%20label%3A%22status%3A%20need%20documentation%22&style=flat)](https://github.com/dm17ryk/klogg/issues?q=is%3Aissue+label%3A%22status%3A+need+documentation%22)
[![GitHub commits](https://img.shields.io/github/commits-since/dm17ryk/klogg/v26.04.svg?style=flat)](https://github.com/dm17ryk/klogg/commits/)
[![CI Build and Release](https://github.com/dm17ryk/klogg/actions/workflows/ci-build.yml/badge.svg)](https://github.com/dm17ryk/klogg/actions/workflows/ci-build.yml)

[![Chat on Discord](https://img.shields.io/discord/838452586944266260?label=Discord&style=flat)](https://discord.gg/DruNyQftzB) [![Join the chat at https://gitter.im/klogg_log_viewer/community](https://badges.gitter.im/klogg_log_viewer/community.svg)](https://gitter.im/klogg_log_viewer/community?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

## Overview

CILogg is a multi-platform GUI application that helps browse and search
through long and complex log files. It is designed with programmers and
system administrators in mind and can be seen as a graphical, interactive
combination of grep, less, and tail.

![CILogg main window](website/static/screenshots/mainwindow.png)

Please refer to the
[documentation](DOCUMENTATION.md)
page for how to use CILogg.

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
- [Command Line Usage](#command-line-usage)
  - [Running the CLI](#running-the-cli)
  - [Main Commands and Options](#main-commands-and-options)
  - [Commander Automation](#commander-automation)
  - [Parsing CLI Output](#parsing-cli-output)
- [How to Get Help](#how-to-get-help)
- [Contributing](#contributing)
- [License](#license)
- [Authors](#authors)

## About the Project

CILogg started as a fork of [glogg](https://github.com/nickbnf/glogg) - the fast, smart log explorer in 2016.

Since then it has evolved from fixing small annoying bugs to rewriting core components to
make it faster and smarter than its predecessor.

Development of CILogg is driven by features my colleagues and I need
to stay productive as well as feature requests from users on GitHub and in GitHub Discussions.

Latest development updates can be found in [GitHub Discussions](https://github.com/dm17ryk/klogg/discussions) and [GitHub Releases](https://github.com/dm17ryk/klogg/releases).

### Comparing with glogg

CILogg has all best features of glogg:

* Runs on Unix-like systems, Windows and Mac thanks to Qt5
* Is fast and reads the file directly from disk, without loading it into memory
* Can operate on huge text files (10+ Gb is not a problem)
* Search results are displayed separately from original file
* Supports Perl-compatible regular expressions
* Colorizes the log and search results
* Displays a context view of where in the log the lines of interest are
* Watches for file changes on disk and reloads it (kind of like tail)
* Is open source, released under the GPL

And on top of that CILogg:

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

Current CILogg builds include the following major capabilities:

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

CILogg is no longer just a desktop log viewer. In the current codebase it also provides:

* Headless scenario execution via `cilogg scenario`
* Remote lab controller, agent, and operator flows via `cilogg lab-controller`, `cilogg lab-agent`, and `cilogg lab`
* Automation inspection helpers such as `cilogg --dump-ui-tree` and `cilogg --dump-state-json`
* Commander actions for opening files, URLs, COM ports, managing scripts, filters, actions, responses, and tab state

Here is a small demo showing how much faster CILogg is (searching in ~1Gb file stored on tmpfs):

https://user-images.githubusercontent.com/1620716/117588567-bea39100-b12c-11eb-990a-90a667bcaeaa.mp4

List of glogg issues that have been fixed/implemented in CILogg can be found [here](https://github.com/dm17ryk/klogg/discussions/302).

List of all changes can be found [here](https://github.com/dm17ryk/klogg/milestone/8?closed=1).

**[Back to top](#table-of-contents)**

## Installation

This project uses [Calendar Versioning](https://calver.org/). For a list of available versions, see the [repository tag list](https://github.com/dm17ryk/klogg/tags).

### Current stable release builds

Binaries for all platforms can be downloaded from GitHub releases.

[ ![Release](https://img.shields.io/github/v/release/dm17ryk/klogg?style=flat)](https://github.com/dm17ryk/klogg/releases/latest)

#### Windows
Windows installer is also available from:

* [ ![Chocolatey](https://img.shields.io/chocolatey/v/cilogg?style=flat)](https://chocolatey.org/packages/cilogg)
* [ ![Scoop Extras bucket](https://img.shields.io/scoop/v/cilogg?bucket=extras)](https://scoopsearch.github.io/#/apps?q=cilogg)
* [Winget package](https://winget.run/pkg/dm17ryk/klogg) 

#### Mac OS
Package for Mac can be installed from Homebrew

[ ![homebrew cask](https://img.shields.io/homebrew/cask/v/cilogg?style=flat)](https://formulae.brew.sh/cask/cilogg)

#### Linux
It is recommended to use CILogg package from distribution-specific [repositories](https://repology.org/project/cilogg/versions).

Generic packages are published through [GitHub Releases](https://github.com/dm17ryk/klogg/releases/latest).
If your distribution does not already package CILogg, use the release artifacts directly or the AppImage package described below.

There is also an AppImage package that can be used without installation. To run CILogg from AppImage, download the package and make in executable with either a file manager or terminal command `chmod +x <path_to_cilogg_AppImage>` and then run the AppImage file.

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

## Command Line Usage

CILogg can be used as a normal desktop log viewer, a headless scenario runner,
a remote lab controller/agent/operator tool, and a commander automation client
for a running CILogg instance.

### Running the CLI

Installed packages usually put `cilogg` on `PATH`:

```powershell
cilogg --help
cilogg command --help
```

From a local Windows build tree, run the executable from `build_root`:

```powershell
cd D:\Essence_SC\lsrc\klogg\build_root
.\output\RelWithDebInfo\cilogg.exe --help
.\output\RelWithDebInfo\cilogg.exe command --help
```

Before running build-tree executables on Windows, deploy the Qt runtime DLLs
next to the executable:

```powershell
$env:QTDIR = 'C:\qt6.10.1'
& "$env:QTDIR\bin\windeployqt.exe" ".\output\RelWithDebInfo\cilogg.exe"
& "$env:QTDIR\bin\windeployqt.exe" ".\output\RelWithDebInfo\cilogg_tests.exe"
& "$env:QTDIR\bin\windeployqt.exe" ".\output\RelWithDebInfo\cilogg_itests.exe"
```

The repo helper script also performs the test executable deployment:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\codex\run-tests.ps1 -Config RelWithDebInfo
```

For deterministic automation runs, set `CILOGG_AUTOMATION=1` or use Qt offscreen
when the command does not need a visible desktop:

```powershell
$env:CILOGG_AUTOMATION = '1'
.\output\RelWithDebInfo\cilogg.exe --dump-ui-tree --window-width 1600 --window-height 1000
.\output\RelWithDebInfo\cilogg.exe -platform offscreen --version
```

### Main Commands and Options

| Command | Description |
| --- | --- |
| `cilogg [options] [file ...]` | Open CILogg normally. Positional file paths are opened as tabs. |
| `cilogg --help` | Print the main CLI help. |
| `cilogg --help-all` | Print CILogg and generic Qt options. |
| `cilogg --version` | Print the CILogg version, build date, commit, and license text. |
| `cilogg scenario --help` | Print detailed headless scenario runner help. |
| `cilogg lab --help` | Print remote lab operator help. |
| `cilogg lab-agent --help` | Print remote lab agent help. |
| `cilogg lab-controller --help` | Print remote lab controller help. |
| `cilogg command --help` | Print commander automation help and action syntax. |
| `cilogg --dump-ui-tree [--window-width <n> --window-height <n>]` | Start an automation-sized window, dump the Qt automation UI tree as JSON, and exit. |
| `cilogg --dump-state-json <path> [--window-width <n> --window-height <n>]` | Write an automation state snapshot JSON file and exit. |

Main viewer options:

| Option | Description |
| --- | --- |
| `-d, --debug <level>` | Increase debug verbosity. Larger numbers are more verbose. |
| `-m, --multi` | Allow another CILogg instance instead of sending work to the primary instance. Use with `-s` when restoring a separate session. |
| `-s, --load-session` | Load the previous session. This is the default when no file is passed. |
| `-n, --new-session` | Do not load the previous session. This is the default when files are passed. |
| `-l, --log` | Save CILogg logs to a file. |
| `-f, --follow` | Follow initially opened files, similar to `tail -f`. |
| `--window-width <n>` / `--window-height <n>` | Set the automation/startup window size for supported commands. |

Scenario batch commands:

| Command | Description |
| --- | --- |
| `cilogg scenario run --suite-file <path> [--device-map-file <path>] [--report-dir <path>]` | Run a scenario suite manifest. |
| `cilogg scenario run --scenario-file <path> [--args-json-file <path>] [--device-map-file <path>] [--report-dir <path>]` | Run one Python scenario file, optionally with JSON arguments. |
| `cilogg scenario validate --suite-file <path> [--device-map-file <path>]` | Validate a suite manifest and optional logical-device mapping without running it. |
| `cilogg scenario list-devices --suite-file <path>` | Print the logical devices declared by a suite. |

Remote lab commands:

| Command | Description |
| --- | --- |
| `cilogg lab-controller serve --listen <host:port> --state-dir <path> --token-file <path>` | Run the controller HTTP API and agent channel. The TCP agent channel uses the next port after the HTTP listen port. |
| `cilogg lab-agent run --controller-url <url> --agent-config <path> --token-file <path>` | Register local COM inventory with the controller and execute queued jobs. |
| `cilogg lab submit --controller-url <url> --token-file <path> (--suite-file <path> \| --scenario-file <path>) [--args-json-file <path>] [--agent-label <label>] [--report-dir <path>]` | Upload a scenario or suite job to the controller. |
| `cilogg lab queue --controller-url <url> --token-file <path> [--pretty]` | Print queued lab jobs as JSON. |
| `cilogg lab status --controller-url <url> --token-file <path> --job-id <id> [--pretty]` | Print one job status as JSON. |
| `cilogg lab cancel --controller-url <url> --token-file <path> --job-id <id> [--pretty]` | Cancel a queued or running job. |
| `cilogg lab agents --controller-url <url> --token-file <path> [--pretty]` | Print registered agents and inventory as JSON. |
| `cilogg lab artifacts --controller-url <url> --token-file <path> --job-id <id> --output-dir <path> [--pretty]` | Download job artifacts and print the artifact metadata as JSON. |

### Commander Automation

Commander mode uses:

```powershell
cilogg command --action <action> [action options]
```

If a primary CILogg instance is already running, commander requests are sent to
that instance. If no instance is running, only open actions (`open_file`,
`open_url`, and `open_com`) can start a new CILogg window. Other actions fail
with `No running CILogg instance.` and a non-zero exit code.

Tab-targeted actions accept one of these selectors:

```powershell
--tab-id <id>
--window-index <n> --tab-index <n>
```

Use `get_info --pretty` to discover window and tab identifiers:

```powershell
$info = cilogg command --action get_info --pretty | ConvertFrom-Json
$firstTab = $info.windows[0].tabs[0]
cilogg command --action focus_tab --tab-id $firstTab.tabId
```

COM live-stream examples:

```powershell
# Open COM7 and capture it to a chosen file.
cilogg command --action open_com --port COM7 --file D:\logs\com7.log --baud 115200 --timestamps

# Resume or start the selected live stream. start_comm is kept for compatibility.
cilogg command --action play_comm --tab-id $firstTab.tabId
cilogg command --action start_comm --tab-id $firstTab.tabId

# Pause without closing the port, resume later, then rotate to the next capture file.
cilogg command --action pause_comm --tab-id $firstTab.tabId
cilogg command --action play_comm --tab-id $firstTab.tabId
cilogg command --action start_new_comm_file --tab-id $firstTab.tabId

# Stop/close the stream and inspect status.
cilogg command --action get_comm_status --tab-id $firstTab.tabId --pretty
cilogg command --action stop_comm --tab-id $firstTab.tabId
```

Commander actions:

| Action | Syntax | Description |
| --- | --- | --- |
| `open_file` | `--file <path> [--follow]` | Open a local file. `--follow` starts file-follow mode for the opened tab. |
| `open_url` | `--url <url>` | Open a remote URL as a log source. |
| `open_com` | `--port <name> [--file <path>] [serial options]` | Open a COM capture tab. Omitted serial options inherit current Preferences values. |
| `close_file` | `--file <path>` | Close a file tab by normalized file path. |
| `close_url` | `--url <url>` | Close a URL-backed tab. |
| `close_com` | `--port <name>` | Close the COM stream for the named port. |
| `close_cilogg` | no extra options | Close the CILogg application. The legacy alias `close_klogg` is also accepted. |
| `close_all` | no extra options | Close all open tabs. |
| `get_info` | `[--pretty]` | Print JSON describing windows, tabs, active tab ids, source types, COM status, and script status. |
| `focus_tab` | `--tab-id <id>` or `--window-index <n> --tab-index <n>` | Activate a tab. |
| `close_tab` | `--tab-id <id>` or `--window-index <n> --tab-index <n>` | Close a tab by id or window/tab index. |
| `set_follow_mode` | `[tab selector] (--enabled \| --disabled)` | Enable or disable follow mode for a tab. |
| `search` | `[tab selector] --text <expr> [--regex] [--case-sensitive] [--inverse] [--boolean] [--auto-refresh] [--keep-results]` | Run a search/filter expression on a tab. |
| `get_filters` | `[tab selector] [--filter-id <id> \| --filter-index <n>] [--predefined] [--pretty]` | Print search-history or predefined filters as JSON. |
| `set_filter` | `[tab selector] (--filter-id <id> \| --filter-index <n> \| --filter-string <expr>) [--predefined] [--search] [--auto-refresh]` | Select or create a filter. `--search` runs it immediately; `--auto-refresh` also rearms automatic refresh. |
| `start_comm` | `[tab selector]` | Compatibility name for resuming or starting a live COM stream. Succeeds when the stream is already open. |
| `play_comm` | `[tab selector]` | Preferred alias for `start_comm`; resume if paused, start if stopped, succeed if already open. |
| `pause_comm` | `[tab selector]` | Pause an open COM stream without closing the tab or capture file. Succeeds if already paused. |
| `stop_comm` | `[tab selector]` | Stop/close a COM stream. |
| `start_new_comm_file` | `[tab selector]` | Rotate an active open COM stream to the next suggested capture file, matching the GUI `Start new file` command. |
| `get_comm_status` | `[tab selector] [--pretty]` | Print COM connection, paused, logging, actions-port, and response-counter status as JSON. |
| `start_logging` | `[tab selector]` | Enable logging for a live COM stream. |
| `stop_logging` | `[tab selector]` | Disable logging for a live COM stream. |
| `add_comment` | `--text <value> [--timestamp] [tab selector]` | Append a comment to the selected communication capture. |
| `clear_comm` | `[tab selector]` | Clear the selected communication view/capture display. |
| `get_response_counter` | `(--id <id> \| --name <name> \| --all) [tab selector] [--pretty]` | Read action/response counters from a COM stream. |
| `reset_response_counter` | `(--id <id> \| --name <name> \| --all) [tab selector]` | Reset one or all response counters. |
| `get_actions` | `[--pretty]` | Print configured action definitions as JSON. |
| `get_responses` | `[--pretty]` | Print configured response definitions as JSON. |
| `create_action` | `--json-file <path>` | Create an action definition from a JSON object file. |
| `update_action` | `--id <id> --json-file <path>` | Replace an existing action definition. |
| `delete_action` | `--id <id>` | Delete an action definition. |
| `create_response` | `--json-file <path>` | Create a response definition from a JSON object file. |
| `update_response` | `--id <id> --json-file <path>` | Replace an existing response definition. |
| `delete_response` | `--id <id>` | Delete a response definition. |
| `send_action` | `--id <id> [tab selector]` | Send a configured action through the selected or active actions COM port. |
| `wait_response` | `(--id <id> \| --name <name>) [tab selector] --timeout-ms <ms>` | Wait until a configured response is observed or the timeout expires. |
| `run_script` | `--script-file <path> [--args-json-file <path>] (tab selector)` | Run a Python script bound to a specific tab. |
| `run_global_script` | `--script-file <path> [--args-json-file <path>]` | Run a Python script without a tab binding. |
| `stop_script` | `(tab selector \| --all)` | Stop a tab-bound script or all tab-bound scripts. |
| `stop_global_script` | no extra options | Stop the global script. |
| `get_script_status` | `(tab selector \| --all) [--pretty]` | Print tab-bound script status as JSON. |
| `get_global_script_status` | `[--pretty]` | Print global script status as JSON. |
| `get_script_subscriptions` | `(tab selector \| --all) [--pretty]` | Print tab-bound script event subscriptions as JSON. |
| `get_global_script_subscriptions` | `[--pretty]` | Print global script event subscriptions as JSON. |
| `clear_script_subscriptions` | `(tab selector \| --all)` | Clear tab-bound script event subscriptions. |
| `clear_global_script_subscriptions` | no extra options | Clear global script event subscriptions. |
| `run_scenario` | `--scenario-file <path> [--args-json-file <path>]` | Start an interactive scenario run inside the GUI process. |
| `run_suite` | `--suite-file <path>` | Start an interactive scenario suite run inside the GUI process. |
| `stop_scenario_run` | no extra options | Stop the active interactive scenario run. |
| `get_scenario_status` | `[--pretty]` | Print interactive scenario runner status as JSON. |
| `get_scenario_report` | `[--pretty]` | Print the latest interactive scenario report payload as JSON. |
| `invoke_action` | `--object-name <name>` | Invoke a named Qt automation action/object. |
| `dump_state` | `[--pretty]` | Internal commander form of the automation state dump. Prefer `--dump-state-json <path>` for normal CLI usage. |

Serial options for `open_com`:

| Option | Description |
| --- | --- |
| `--baud <baud>` | Baud rate, for example `115200`. |
| `--data-bits <bits>` | Data bits: `5`, `6`, `7`, or `8`. |
| `--parity <parity>` | `none`, `even`, `odd`, `mark`, or `space`. |
| `--stop-bits <stop_bits>` | `1`, `1.5`, or `2`. |
| `--flow-control <flow_control>` | `none`, `hardware`, `rts/cts`, `software`, or `xon/xoff`. |
| `--timestamps` / `--no-timestamps` | Enable or disable receive timestamps. |
| `--timestamp-format <format>` | Timestamp format used for COM capture lines. |
| `--log-transmits` / `--no-log-transmits` | Enable or disable transmit logging. |
| `--use-for-actions` / `--no-use-for-actions` | Mark or unmark this COM stream as the actions port. |

### Parsing CLI Output

CILogg uses process exit codes for success/failure:

| Exit behavior | Meaning |
| --- | --- |
| Exit code `0` with JSON on stdout | Command succeeded and returned a payload. |
| Exit code `0` with empty stdout | Command succeeded but has no payload, for example `pause_comm` or `start_logging`. |
| Non-zero exit code with text on stderr | Command failed. The text is the user-facing error message. |

Commander stdout is the result payload only, not the internal result envelope.
For example, `get_info --pretty` prints a JSON object similar to:

```json
{
  "windows": [
    {
      "windowId": "window-uuid",
      "windowIndex": 0,
      "currentTabIndex": 0,
      "currentTabId": "tab-uuid",
      "isActiveWindow": true,
      "tabs": [
        {
          "tabId": "tab-uuid",
          "tabIndex": 0,
          "filePath": "D:/logs/com7.log",
          "displayName": "com7.log",
          "sourceType": "com",
          "com": {
            "portName": "COM7",
            "baudRate": 115200,
            "connected": true,
            "paused": false,
            "loggingEnabled": true,
            "isActionsPort": false,
            "responseCounters": []
          }
        }
      ]
    }
  ]
}
```

PowerShell parsing examples:

```powershell
# Capture stdout only. Do not merge stderr with `2>&1` when parsing JSON.
$json = cilogg command --action get_info --pretty
if ($LASTEXITCODE -ne 0) { throw "get_info failed" }
$info = $json | ConvertFrom-Json

$comTabs = $info.windows |
    ForEach-Object { $_.tabs } |
    Where-Object { $_.sourceType -eq 'com' }

$tabId = $comTabs[0].tabId
cilogg command --action pause_comm --tab-id $tabId
if ($LASTEXITCODE -ne 0) { throw "pause_comm failed" }
```

`jq` parsing examples:

```bash
tab_id="$(cilogg command --action get_info \
  | jq -r '.windows[].tabs[] | select(.sourceType == "com") | .tabId' | head -n 1)"

cilogg command --action get_comm_status --tab-id "$tab_id" --pretty |
  jq '.com.connected, .com.paused'
```

Python parsing example:

```python
import json
import subprocess

result = subprocess.run(
    ["cilogg", "command", "--action", "get_info"],
    check=True,
    text=True,
    capture_output=True,
)
info = json.loads(result.stdout) if result.stdout.strip() else {}
tabs = [
    tab
    for window in info.get("windows", [])
    for tab in window.get("tabs", [])
    if tab.get("sourceType") == "com"
]
```

On Windows, the application may mirror human-readable CLI output to stderr so a
launcher can see it. Automation should capture stdout separately and use the
process exit code instead of parsing merged stdout/stderr streams.

**[Back to top](#table-of-contents)**

## How to Get Help

First, please refer to the
[documentation](DOCUMENTATION.md)
page.

You can open issues using the [CILogg issues page](https://github.com/dm17ryk/klogg/issues)
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
