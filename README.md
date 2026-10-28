<div align="center">

<img src="Resources/logo.png" alt="CILogg logo" width="104">

# CILogg

### Follow the logs. Find the signal. Automate the next step.

A fast log explorer with live capture, interactive analysis, and automation built in.

[![Download CILogg](https://img.shields.io/badge/Download-CILogg-2563eb?style=for-the-badge)](https://github.com/dm17ryk/klogg/releases)
[![Build](https://github.com/dm17ryk/klogg/actions/workflows/ci-build.yml/badge.svg)](https://github.com/dm17ryk/klogg/actions/workflows/ci-build.yml)
[![License](https://img.shields.io/github/license/dm17ryk/klogg?color=16a085)](COPYING)

**[Get started](DOCUMENTATION.md#getting-started)** · **[Screenshots](#see-it-in-action)** · **[Compare with klogg](#comparing-with-klogg)** · **[CLI & MCP](MCP.md)** · **[Plugins](docs/plugin-sdk.md)** · **[Roadmap](plans/cilogg-workbench-2026-10-21/README.md)**

Windows · Linux · macOS

</div>

## Overview

**CILogg brings log investigation and device automation into one workspace.** Built on
[klogg](https://github.com/variar/klogg), it keeps the fast large-file viewer and adds
serial capture, reusable device commands, automatic responses, Python scenarios,
remote labs, analysis tools, and a native MCP server.

Go from an error in a log to a repeatable investigation: filter the evidence, inspect
the regex captures, count recurring values, and automate the workflow from the GUI,
CLI, or an MCP client.

| | What CILogg adds | What you can do |
| --- | --- | --- |
| 🔌 | **Live device logs** | Capture COM ports; use the bundled ADB Logcat and UDP Syslog plugins for Android and network sources. |
| ⚡ | **Actions & automatic responses** | Send reusable commands with typed parameters, react to incoming lines, and monitor response counters. |
| 🔬 | **Regex Lab** | Try patterns against a sample, selection, search matches, pasted text, or a file snapshot; inspect named groups and exact captures. |
| 📊 | **Value Counts** | Rank recurring captured values, see counts and percentages, jump to an occurrence, and export CSV. |
| 🐍 | **Scripts, scenarios & remote labs** | Run Python workflows in the app, execute scenarios headlessly, and coordinate controller/agent lab jobs. |
| 🤖 | **CLI & native MCP** | Inspect and control a running session from scripts, Codex, Claude Code, or another local stdio MCP client. |
| 🧩 | **Process plugins** | Add live sources, converters, and tools through plugins running in separate processes. |

The familiar essentials are still here: accelerated regex and Boolean search, live
file following, multiple tabs, highlighters, encoding detection, archives, and a scratchpad.

Development is tracked in the [workbench roadmap](plans/cilogg-workbench-2026-10-21/README.md)
and [GitHub milestones](https://github.com/dm17ryk/klogg/milestones).

## See it in action

### Find the signal in a busy log

![CILogg dark workspace with highlighted warning and error lines and separate filtered results](docs/images/workspace.jpg)

*Captured from the normal installed Windows version of CILogg, build 26.04.2.1078.
Log views use the included [sample device log](docs/examples/device-session.txt).*

<table>
<tr>
<td width="50%"><strong>🔬 Inspect every capture</strong><br>Test a pattern and explore its groups in Regex Lab.</td>
<td width="50%"><strong>📊 Turn repetition into answers</strong><br>Count captured values and see which ones dominate.</td>
</tr>
<tr>
<td><a href="docs/images/regex-lab-showcase.jpg"><img src="docs/images/regex-lab-showcase.jpg" alt="CILogg Regex Lab with named severity and service captures" width="100%"></a></td>
<td><a href="docs/images/value-counts-showcase.jpg"><img src="docs/images/value-counts-showcase.jpg" alt="CILogg Value Counts ranking log severity values with counts and percentages" width="100%"></a></td>
</tr>
<tr>
<td><a href="docs/regex-lab.md">Explore Regex Lab →</a></td>
<td><a href="docs/value-counts.md">Explore Value Counts →</a></td>
</tr>
</table>

### Work with devices, responses, and plugins

<table>
<tr>
<td width="50%"><strong>⚡ Reuse your device commands</strong><br>Browse actions, parameters, and transmit sequences.</td>
<td width="50%"><strong>🔁 React to incoming logs</strong><br>Configure match rules and automatic responses.</td>
</tr>
<tr>
<td><a href="docs/images/actions-showcase.jpg"><img src="docs/images/actions-showcase.jpg" alt="Normal CILogg Actions window with COM routing and reusable command sequences" width="100%"></a></td>
<td><a href="docs/images/responses-showcase.jpg"><img src="docs/images/responses-showcase.jpg" alt="Normal CILogg Responses window with enabled match rules" width="100%"></a></td>
</tr>
<tr>
<td><strong>🧩 Connect more log sources</strong><br>Manage bundled ADB Logcat and Syslog plugins.</td>
<td><strong>🎨 Make important events stand out</strong><br>Organize reusable highlighter sets and colors.</td>
</tr>
<tr>
<td><a href="docs/images/plugins-showcase.jpg"><img src="docs/images/plugins-showcase.jpg" alt="Installed CILogg plugin manager showing ADB Logcat, Syslog Server, and examples" width="100%"></a></td>
<td><a href="docs/images/highlighters-showcase.jpg"><img src="docs/images/highlighters-showcase.jpg" alt="CILogg highlighter editor with colored rules and reusable sets" width="100%"></a></td>
</tr>
</table>

**[View all 10 screenshots →](docs/screenshots.md)** — including the response
editor, Syslog settings, and Script Runner. Click any image to see it at full size.

Start with the
[guided walkthrough](DOCUMENTATION.md#your-first-investigation), or browse the
[feature guides](DOCUMENTATION.md#feature-guides).

## Table of Contents

- [Overview](#overview)
- [See it in action](#see-it-in-action)
- [About the Project](#about-the-project)
  - [Comparing with klogg](#comparing-with-klogg)
- [Installation](#installation)
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

CILogg is a fork of [klogg](https://github.com/variar/klogg), which grew out of
[glogg](https://github.com/nickbnf/glogg). The upstream projects provide the fast log
exploration foundation; CILogg extends it for device debugging, automation, and
interactive analysis.

### Comparing with klogg

**klogg is an excellent fast log viewer. CILogg builds on that foundation with
capture, analysis, and automation workflows.**

| Capability | Upstream klogg | CILogg |
| --- | --- | --- |
| Large-file viewing, accelerated regex & Boolean search | Included | Inherited |
| Live file following, highlighters, dark mode & scratchpad | Included | Inherited |
| Serial / COM capture with transmit logging and pause/resume | — | **Added** |
| Reusable actions, typed parameters & automatic responses | — | **Added** |
| Regex Lab with capture inspection | — | **Added** |
| Value Counts with percentages & CSV export | — | **Added** |
| Embedded Python scripting & headless scenario execution | — | **Added** |
| Remote lab controller, agent & operator commands | — | **Added** |
| Commander control of a running GUI & native stdio MCP | — | **Added** |
| Process plugin system with bundled ADB Logcat & UDP Syslog sources | — | **Added** |

Comparison scope: the features described in the
[upstream klogg README](https://github.com/variar/klogg/blob/master/README.md) and this
repository's linked guides, reviewed October 2026. **—** means the CILogg workflow
is not documented as a built-in upstream feature. Shared viewer features are
credited to klogg; no speed advantage over klogg is claimed here.

See the [user guide](DOCUMENTATION.md), [automation reference](MCP.md), and
[changelog](CHANGELOG.md) for details.

**[Back to top](#table-of-contents)**

## Installation

Download the assets for your operating system from **[CILogg Releases](https://github.com/dm17ryk/klogg/releases)**.
Choose a Windows installer or portable archive, a Linux package/AppImage, or a
macOS package from the assets available for that release.

| Platform | Start here |
| --- | --- |
| **Windows** | Use the installer, or extract a portable archive and run `cilogg.exe`. |
| **Linux** | Choose the matching distribution package or AppImage. Make an AppImage executable before running it: `chmod +x <downloaded-file>.AppImage`. |
| **macOS** | Open the macOS package and follow the installation instructions included with the release. |

Check each release's notes for supported architectures and runtime requirements.
Use the release's `*-sha256.txt` file to verify downloaded artifacts before installing.
CILogg uses [Calendar Versioning](https://calver.org/); see the
[tags](https://github.com/dm17ryk/klogg/tags) and [changelog](CHANGELOG.md) for version history.

### Testing builds

Download testing builds from [GitHub Releases](https://github.com/dm17ryk/klogg/releases).
Current testing artifacts use versioned `continuous-*` tags. Choose the release
and assets for your operating system and CPU architecture, and read its release
notes before upgrading.

**[Back to top](#table-of-contents)**

## Building

The current recommended development environment is Windows x64 with Visual Studio 2022 and Qt 6.
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
$env:QTDIR = 'C:\Essence_SC\QtSDK\6.12.0\msvc2022_64'
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
| `run_script` | `--script-file <path> [--args-json-file <path>] (--tab-id <id> \| --window-index <n> --tab-index <n>)` | Run a Python script bound to a specific tab; a tab selector is required. |
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
