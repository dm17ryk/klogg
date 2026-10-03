# Built-in CILogg MCP and CLI coverage

The MCP server and automatic client setup are native parts of `cilogg` and
`cilogg_portable`. They start before GUI initialization. No Python, Node.js,
PowerShell installer, or separate MCP executable is required.

## Automatic setup

```text
cilogg mcp install
cilogg mcp install --client codex --client claude-code
cilogg mcp install --client all --dry-run
cilogg mcp uninstall --client codex
cilogg mcp skill --output /path/to/cilogg-mcp
cilogg mcp --help
```

`install` detects available clients, merges the CILogg entry into their user
configuration, and creates `cilogg-mcp/SKILL.md` for Codex, Claude Code, and Cursor.
Supported registrations: Codex, Claude Code, Cursor, VS Code's default profile,
and Claude Desktop. Repeat `--client` to select clients explicitly.
`--client all` also creates configuration for clients not installed yet.
`--home <directory>` isolates setup for testing. `--no-skills` skips skill files.
`skill` without `--output` prints the generated skill.

All configurations are parsed before writes begin. Files use atomic replacement
and existing files receive `.cilogg-backup-<timestamp>-<id>` backups. Repeating
setup is idempotent. Existing servers, unrelated settings, environment variables,
and CILogg server tuning are preserved. Codex's ordinary TOML table syntax also
preserves comments; unusual inline/dotted table syntax is serialized by a TOML
parser to preserve its settings. Invalid configuration stops setup with an error.
Uninstall removes registration and retains skills and backups.
VS Code JSONC comments and trailing commas are accepted; writing its configuration
uses ordinary JSON and preserves all settings.

Restart or reload the client after installation. Move/install CILogg first and
then run setup from that binary: the registration uses its absolute path.

## Any MCP client

Run `cilogg mcp config` to print the exact local entry:

```json
{"command":"/absolute/path/to/cilogg","args":["mcp","serve"]}
```

Put it under the client's server name `cilogg` (`mcp_servers` for Codex,
`mcpServers` for Claude/Cursor/Desktop, `servers` plus `type: "stdio"` for VS Code).
`cilogg mcp serve` uses newline-delimited UTF-8 JSON-RPC over standard input and
output. Diagnostics go to standard error. It negotiates MCP versions 2025-11-25,
2025-06-18, 2025-03-26, and 2024-11-05. It offers tools, a command catalog resource,
and an inspection prompt. Any MCP client supporting local stdio can launch it;
remote HTTP transport is not implemented.

## Coverage

| Capability | CLI | MCP |
|---|---|---|
| Logs, URLs, tabs, windows, search, follow, filters, screenshots, selection/export | `command --help` lists all actions | `cilogg_execute` discovers and executes every Commander action |
| Serial ports, capture settings, pause/resume, actions, responses, scripts | Existing Commander commands | Same action names and arguments |
| Preferences, menus, toolbars, dialogs, editors, font/color/file pickers | `command --action get_ui/set_ui/activate_ui/invoke_action` | Same commands through `cilogg_execute` |
| Custom panes, context menus, docking, splitters, mouse selection | `activate_ui --text` or `--json-file` mouse gesture | Keyboard or inline `definition` mouse gesture |
| GUI startup and normal application arguments | `cilogg --help` | `cilogg_cli`, mode `application` |
| Batch scenario run/validate/list-devices | `cilogg scenario --help` | `cilogg_cli`, mode `scenario` |
| Remote lab submit/queue/status/cancel/agents/artifacts | `cilogg lab --help` | `cilogg_cli`, mode `lab` |
| Controller and agent services | `cilogg lab-controller/lab-agent --help` | Corresponding `cilogg_cli` modes and background jobs |
| Command-line log search | `cilogg_grep --help` | `cilogg_cli`, mode `grep` |
| MCP setup and skill generation | `cilogg mcp --help` | `cilogg_cli`, mode `mcp` for setup commands |

This supplies a route to the full GUI/CLI surface. Discovery follows the actual
binary's help rather than a second manually maintained command list. Physical
serial hardware, external scripts, and every dialog workflow still depend on
their normal prerequisites; automated tests cover representative workflows.

Six tools keep discovery compact:

- `cilogg_commands(query?, area?)`: all Commander actions/options and full help for
  each CLI mode. `area: "gui"` inspects the currently running window's Qt controls.
- `cilogg_status()`: running windows, tabs, ports, and stable tab IDs.
- `cilogg_state(window_index?)`: GUI/search/follow/action snapshot.
- `cilogg_execute(action, arguments?)`: any Commander action. Argument names use
  underscores (`tab_id`, `window_index`, `object_name`). `definition` and
  `parameters` objects replace JSON file arguments.
- `cilogg_cli(mode, arguments?, background?, timeout_ms?)`: literal argument arrays,
  without a shell or executable prefix. Application mode launches a detached GUI.
- `cilogg_jobs(operation?, job_id?)`: list/read/cancel background jobs owned by this
  MCP session. Closing the MCP session stops those jobs and leaves detached GUIs.

CLI modes expose their original validation and exit status. Long foreground calls
accept MCP cancellation. Output is bounded to 8 MiB per stream/job, with an explicit
truncation flag. Use background jobs for long operations/services (maximum 32 jobs
per session). Commands targeting GUI state require a running CILogg; the server
adds `--require-running` so an accidental mutation does not create a new GUI.

## GUI controls

Inspect `get_ui` first. Select a unique `objectName` or the returned `objectPath`;
paths identify the current UI tree and must be rediscovered after dialog changes.
`set_ui` accepts writable properties such as `text`, `checked`, `value`,
`currentIndex`, `currentText`, `currentFont`, and `currentColor`. Models accept
`indexPath` (zero-based row path), `column`, `editText`, and `checkState` (0/1/2).
Use `@clipboard` for clipboard text. Password inspection is redacted, and disabled,
hidden, read-only, or modal-blocked controls reject mutations.

`activate_ui` clicks a button, triggers an action, or activates the selected model
item. `text: "Ctrl+A"` sends one portable Qt key sequence; `text: "Menu"` opens a
keyboard context menu. Mouse gestures target a widget using local coordinates:

```json
{"action":"activate_ui","arguments":{"object_name":"paneObjectName","definition":{"event":"drag","x":10,"y":20,"toX":200,"toY":100,"modifiers":["Shift"]}}}
```

Events: `click`, `double_click`, `drag`, `wheel`. Omitted start coordinates use the
widget center. `button` is `left` (default), `right`, or `middle`; `modifiers` is an
array of `Ctrl`, `Alt`, `Shift`, or `Meta`. Drag requires `toX`/`toY`; wheel accepts
`delta` (default 120). Inspect the viewport object for item-view mouse operations.

Activation is queued so modal dialogs can be controlled by subsequent commands.
Poll `get_ui`/state for completion. Actions launched this way use inspectable Qt
dialogs. GUI automation commands allow up to 30 seconds to contact the GUI while
a cold dialog initializes. Arbitrary operating-system or other application's UI
is outside CILogg.

Release serial ports with `pause_comm` and resume with `play_comm`. Never terminate
the user's monitor to free a COM port. A failed/timed-out mutation may have been
accepted: inspect state before retrying.

## Validation

Native Catch2 tests cover protocol lifecycle/input errors, TOML and client JSON
merges, isolated/idempotent installation, and GUI control/model/key/mouse events.
`tests/mcp/smoke_stdio.py` is an opt-in development test using the official Python
MCP SDK as a client against a separately launched isolated CILogg GUI. It is
not part of the shipped server or installation process.

References: [MCP lifecycle](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle),
[stdio transport](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports),
[tools](https://modelcontextprotocol.io/specification/2025-11-25/server/tools),
[VS Code configuration](https://code.visualstudio.com/docs/agent-customization/mcp-servers).
