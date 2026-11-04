# CILogg Unified Integrated Terminal Implementation Plan

> **For agentic workers:** Use `superpowers:subagent-driven-development` or `superpowers:executing-plans`, when available, to implement this plan task by task. Checkboxes track work, not evidence of successful implementation. Follow the repository's current AGENTS.md and canonical Workbench execution ledger.

**Goal:** Make a connected CILogg log window an interactive terminal for local system shells, serial device consoles, and SSH while retaining CILogg's searchable log history, Actions, automatic responses, and automation interfaces.

**Architecture:** Keep `StreamSession` as the connection, generation, Actions, and publication owner. Extend the Workbench D01 transport contracts, attach a terminal emulator/controller to the same raw stream, and expose terminal and log presentations within the existing session tab. Never create a second connection just to display a terminal.

**Tech stack:** Existing C++17, Qt Widgets, Qt Core, Qt SerialPort, CMake, Catch2 and Qt Test; proposed upstream libvterm with a native Qt renderer; Windows ConPTY; Unix PTYs; system OpenSSH for SSH v1. Preserve Qt5/Qt6 core compatibility.

**Spec:** Sections 1-9 of this document are the proposed terminal specification. This is an addendum to `plans/cilogg-workbench-2026-10-21/`, not a second execution ledger.

**Baseline inspected:** `dm17ryk/klogg`, `master`, commit `90f10548fd038995034514283e5c74b197fa06e0`, inspected 2026-11-04. Source inspection only: no product builds, hardware tests, or platform compatibility tests were performed for this planning document. Reconcile with the actual implementation branch before editing.

**Status:** Proposed implementation task. The user selected BOTH system and device terminals and explicitly requires typing directly in the log window. Dependency choices, budgets, and detailed UI behavior below are design recommendations, not claims that the user individually approved every detail.

## Global constraints

- The existing log window must become interactive. A separate terminal dock alone does not satisfy the requirement.
- Deliver local shells, serial consoles, and SSH as first-release scope; do not label a serial-only or local-shell-only implementation complete.
- Preserve the large-file viewer, existing capture files, saved configurations, IDs, plugin protocol v1, Actions/Responses coupling, and public serial APIs.
- Preserve C++17 and Qt5/Qt6 core support. Do not raise the Qt minimum just to use a convenient newer API.
- Build only under `build_root`; use repository build/test scripts and current validated SDK settings. The actual root CMake test option is `KLOGG_BUILD_TESTS`.
- Reuse F01 canonical event/provenance types, D01 transport/write/retained-byte contracts, existing analysis services, and existing Commander/CLI/MCP/Python dispatch. Do not introduce parallel services with equivalent responsibilities.
- Shared MainWindow, Commander, schemas, CMake, resources, and ledger changes are integrated serially, even when independent backend tasks are developed in parallel.
- All incoming bytes are untrusted. No terminal sequence may execute a local command, access a secret, or modify the clipboard without an explicit supported permission.
- Input, capture, terminal emulation, and GUI repainting have independent lifecycles. Pausing display must not pause I/O or change captured data.
- Gate incremental integration behind proposed `KLOGG_ENABLE_TERMINAL` (off until release gates pass). Final validated production builds enable the feature; an off build remains a rollback/regression configuration. Live intermediate prototypes must reject automated writes while manual input is armed, even before the full UT09 policy UI exists.

## Review focus

1. A prompt without a newline and UTF-8/control sequences split at every byte boundary must remain immediately usable; tests belong to UT01/UT02/UT03.
2. A disconnect during a partial write must not replay the command into a new connection; tests belong to UT02/UT04/UT05/UT06.
3. Password input, terminal replies, synthetic TX annotations, and replay must not become raw device records or trigger automatic responses; tests belong to UT02/UT03/UT09/UT12.
4. Focus changes, global shortcuts, filtered results, and IME composition must never silently send text to the wrong session; tests belong to UT03/UT10/UT12.
5. Continuous logs while typing, including full-screen programs and CR-only progress output, must not corrupt input state or grow memory without bounds; tests belong to UT01/UT02/UT03/UT12.

---

## 1. Product requirement and acceptance scenario

The user should be able to connect CILogg to an ESP32, router, remote SSH host, or local shell; click inside that session's main content area; type commands; and see command echo, responses, prompts, and asynchronous logs in that same window. Creating an Action must not be a prerequisite.

### Required behavior

A serial session can show a device prompt such as `device> ` with no trailing newline. Typing `status` sends input to that device, not to CILogg search. Enter submits according to the active input policy. New device log messages continue to arrive. The user can inspect and filter captured output without disconnecting the device, and then return to the still-running terminal.

For a local or SSH shell, Tab completion, history arrows, cursor movement, Ctrl+C, interactive password entry, and supported full-screen applications must work. A command-input field that launches a new process for every Enter is not a shell session.

### Non-goals for this delivery

Do not implement another shell, SSH protocol, terminal escape-sequence engine, Actions runtime, credential manager, plugin host, or event database. Do not add Telnet, RDP, SFTP UI, graphical applications, terminal image protocols, multi-user collaborative sessions, or a full IDE. A dock/pop-out view is optional after the main-window requirement works.

Opening a file remains read-only. An ADB/syslog source remains read-only unless its source explicitly negotiates a bidirectional byte capability; text-source plugin v1 is not implicitly a terminal.

## 2. Verified repository anchors and integration rules

The inspected code already supplies useful foundations:

| Existing anchor | Observed responsibility | Required treatment |
|---|---|---|
| `src/ui/include/streamsession.h` | Serial lifecycle, generations, `sendBytes(data, sensitive)`, raw/line observation, Actions/Responses | Retain this owner and compatible entry points. |
| `src/ui/src/serialcaptureworker.cpp` | QSerialPort ownership, capture formatting, RX/TX publication | Feed emulation from raw RX, before timestamp/TX formatting. |
| `src/ui/include/streamsourceregistry.h` | Active serial-port tracking | Upgrade/reuse D01 atomic leasing; never open a second port for terminal display. |
| `src/ui/include/crawlerwidget.h` | Central main/filtered views, search, navigation, signals | Host the interactive presentation in the existing tab. |
| `src/ui/CMakeLists.txt` | Qt Widgets/SerialPort and session/UI integration | Wire focused additions; avoid a broad module rewrite. |
| `src/logdata/` | File-backed log data and analysis | Reuse for append-only derived transcript analysis. |
| `tests/unit/CMakeLists.txt` | `cilogg_tests`, Catch2 and Qt Test | Register focused tests and preserve existing regression suites. |
| `docs/workbench/decisions.md` | One ledger, shared owners, compatibility and Actions coupling | This is an integration constraint, not optional background reading. |
| Workbench `tasks/D01.md` | Planned `IStreamTransport`, `EndpointSpec`, receipts and retained bytes | Implement/reuse this foundation, not a terminal-specific duplicate. |

Important source observations: `onReadyRead()` emits the original `QByteArray`, while capture-file data may receive timestamps. `sendData()` currently uses `waitForBytesWritten(1000)` and does not establish complete peer delivery. Existing sensitive sends publish redaction text rather than their private payload. Do not interpret the redaction text as actual transmitted bytes. [R2-R5]

D01 is a planned contract in the inspected roadmap, not presumed fully implemented. Its declared prerequisites include G01/G02/G03/F01. UT00 must reconcile actual progress and approved ordering; do not mark those dependencies done or silently supersede the existing decoder/workbench priority. [R7-R9]

### Actions compatibility

Preserve the existing rules: disabling Actions disables automatic responses; enabling responses enables Actions; disabling responses leaves Actions enabled; enabling Actions does not overwrite an explicit responses choice. Direct human typing is a separate permission and must work with Actions disabled. Temporarily suspending automated writes during manual control must not rewrite persisted Actions settings. [R7]

## 3. User interaction: the log window is the terminal

### 3.1 One session tab, two models

Add a small per-session toolbar with presentation (`Log`, `Terminal`, optional `Split`), input state (`Inspect`, `Character`, `Line`), endpoint identity, connection state, recording state, and a return-to-live control.

The main content region belongs to the existing tab. `Terminal` replaces the main log presentation in that region; it does not open an unrelated shell panel. `Log` retains the existing large-file/filtered analysis experience. Both refer to the same `StreamSession`, endpoint, capture, and connection generation.

When `Line` input is enabled in the Log presentation, typing in the main viewport focuses an inline draft editor anchored inside that viewport. This is a presentation overlay, not an editable log line, modal dialog, or Actions editor. Incoming logs cannot overwrite the draft.

`Character` input requires a visible live terminal surface. Selecting it from the Log presentation switches that same surface to Terminal. Split presentation, when enabled, has one live terminal input owner and one read-only analysis pane.

### 3.2 Input states

| State | Key handling | Recommended default |
|---|---|---|
| Inspect | Navigation, selection and search only; transmit nothing | Files, restored sessions, ordinary serial log capture |
| Character | Encode and send committed text/keys immediately; remote shell owns editing and history | Newly opened local/SSH shells and explicit serial-shell profiles |
| Line | Local draft/history; submit one encoded command on Enter | Explicit line-command/AT-style device profiles |

Do not infer these states by recognizing `$`, `#`, `>` or a password prompt. Configuration and explicit session capabilities control input.

Character-mode Enter uses the terminal key encoder; serial console default is CR with configurable CR/LF/CRLF policy. Line mode appends the configured terminator exactly once. Backspace is configurable for device profiles (DEL or BS). Encoding defaults to UTF-8; legacy encodings are explicit per profile and reject unmappable input unless the user enables replacement.

Local echo is off for shell sessions. Device profiles may enable it explicitly. Never combine local echo and remote echo through an unreliable deduplication heuristic.

### 3.3 Focus and shortcuts

A conspicuous input indicator shows the exact endpoint and whether input is armed. The terminal cursor appears only in the live input surface. Search boxes, filtered results, dialogs, and another tab never forward keys to the connection.

In terminal input focus, Ctrl+C is an interrupt key, not Copy; Ctrl+Shift+C and Ctrl+Shift+V provide copy/paste on Windows/Linux. On macOS, Command+C/V remain copy/paste and Control combinations go to the terminal. Inspect mode keeps the existing viewer shortcuts. An explicit input lock is always available in the toolbar and through a configurable shortcut.

Handle `QEvent::ShortcutOverride`, key press/release as needed, `inputMethodEvent`, and `inputMethodQuery`. Send committed IME text once; never send pre-edit composition. Test dead keys, AltGr, non-Latin text, surrogate pairs and combining characters. Do not hard-code Windows scan codes as portable text.

Scrolling away from live content freezes only the viewport. Before sending while scrolled back, return to the live surface visibly; do not send invisibly behind historical output. Selection persists during output. Resizing, font changes, DPI changes, and tab switches do not create another connection.

### 3.4 Paste, reconnect, and pause

Honor bracketed-paste mode when advertised by the remote application. Preview and confirm multiline or control-character paste; never silently strip a user-confirmed newline or synthesize an extra Enter. Bound each paste request by the shared write limits and support serial pacing without blocking RX.

A reconnect increments the session generation, invalidates queued input and leases, creates a clear transcript boundary, and resets terminal state. Reconnection never replays a partially typed or partially sent command. Keep any unsent local line draft visibly unsent; resubmission requires a new user action.

Expose separate actions for Pause Display, Pause Recording, and Pause/Close Connection. Preserve the meaning of existing serial connection-pause controls. Closing a view is not implicitly detaching a persistent process; initially warn and close the owned session deterministically. Restoring a workspace restores disconnected profiles, not automatically executed shells or device input.

## 4. Architecture

```text
Existing CILogg session tab / CrawlerWidget
    |
    +-- SessionTerminalView
    |     +-- Existing log/filtered presentation
    |     +-- Native TerminalWidget + inline draft overlay
    |     +-- Input routing / selection / inspect controls
    |
    +-- TerminalController (terminal state, not connection ownership)
          +-- TerminalEmulator wrapper (libvterm)
          +-- TranscriptProjector -> append-only LogData-compatible text
          +-- TerminalInputRouter -> existing shared write arbitration
          |
          +-- Existing StreamSession
                +-- F01 events/provenance + D01 receipts/retained bytes
                +-- Existing Actions/Responses/Commander/Python
                +-- IStreamTransport
                      +-- Existing serial backend / QSerialPort
                      +-- ConPtyTransport (Windows)
                      +-- UnixPtyTransport (Linux/macOS)
                      +-- SshProcessTransport (OpenSSH on local PTY)
                      +-- ProcessPipeTransport (explicit non-TTY capture)
```

There is one session owner and one byte-ingress path. Feed unmodified RX bytes into the terminal model and canonical recording path. GUI repainting consumes immutable/coalesced screen snapshots; it does not own the transport. The transcript is a derived consumer, not the source for terminal rendering.

Terminal state continues to process output while the Log presentation is visible. Disabling rendering does not lose cursor state or terminal replies. Keep emulator mutation on one designated thread. Worker I/O and recording queues are bounded; never emit an unbounded queued Qt signal per byte. Share immutable rows or damage snapshots rather than copying the entire screen for every received chunk.

### 4.1 Reuse transport interfaces

D01 already proposes these interfaces; keep their names and semantics:

```cpp
IStreamTransport::open(const EndpointSpec&);
IStreamTransport::write(const WriteRequest&);
IStreamTransport::close();
IStreamTransport::capabilities() const -> EndpointCapabilities;
// eventReady(EventRecord), writeProgress(WriteReceipt), closed(QString)
```

Add endpoint kinds for local PTY, SSH process, and process pipes without changing serialized values of existing kinds. Prefer stable string serialization and explicit unknown-kind errors. Extend the canonical schema under its owner rather than inventing `ITerminalTransport`, a second EventRef, or another write-receipt format.

Terminal-only control is a small companion interface, not a replacement transport:

```cpp
struct TerminalSize { int columns; int rows; };
struct TerminalCapabilities {
    bool canResize;
    bool isTty;
    bool stdoutStderrSeparated;
};
class ITerminalControl {
public:
    virtual ~ITerminalControl() = default;
    virtual TerminalCapabilities terminalCapabilities() const = 0;
    virtual void resizeTerminal(TerminalSize size) = 0;
};
```

Calls execute on the transport's owning thread through the session dispatcher. Serial has no standard window-size negotiation: update the local display only unless an explicit device profile defines a resize command. Do not send an invented resize sequence to arbitrary devices.

Use encoded control-key bytes for ordinary terminal interrupts. Close, process termination, serial BREAK, DTR, and RTS are distinct operations, with distinct capabilities and permissions.

### 4.2 Terminal-facing contracts

Proposed names below are terminal-specific and do not replace canonical workbench types:

```cpp
enum class TerminalInputMode { Inspect, Character, Line };
enum class TerminalPresentation { Log, Terminal, Split };
enum class InputOrigin { HumanKey, HumanPaste, HumanLine, Automation, TerminalReply };

// Existing endpoint ID, generation, EventRecord, EventRef, DataResult,
// WriteRequest, and WriteReceipt types remain authoritative.
TerminalController::attach(StreamSession* session);
TerminalController::detach();
TerminalController::setInputMode(TerminalInputMode mode);
TerminalController::setPresentation(TerminalPresentation presentation);
TerminalController::resize(TerminalSize size);
TerminalController::feed(const EventRecord& event);

TerminalEmulator::feed(const QByteArray& rawRx);
TerminalEmulator::resize(TerminalSize size);
TerminalEmulator::reset();
TerminalEmulator::snapshot() const -> TerminalScreenSnapshot;
TerminalEmulator::encodeKey(const TerminalKeyEvent&) const -> QByteArray;
TerminalEmulator::encodePaste(const QString&) const -> QByteArray;
```

Define `TerminalKeyEvent` in UT01 as a terminal key enum, modifier flags, and committed Unicode text, independent of QWidget ownership. Define `TerminalScreenSnapshot` as a revisioned, bounded cell grid, cursor state, modes, and bounded scrollback view. Snapshot cells carry Unicode text, display width, attributes, and optional source references; do not assume one code point equals one cell.

Define `TerminalProfile` in UT07 with a versioned ID, typed endpoint configuration, presentation/input defaults, terminal type, encoding, Enter/Backspace policy, local echo, cwd, environment overrides, capture policy, and limits. Private keys/passwords are not profile fields.

Terminal-generated replies (for example cursor-position reports) use `InputOrigin::TerminalReply`, a tightly scoped reply route, and the current generation. They are not Actions or user commands. Recorded-session playback has no live write route, including for these replies.

## 5. Terminal output, searchable logs, and provenance

### 5.1 Three different representations

| Representation | Purpose | Constraint |
|---|---|---|
| Raw events/recording | Preserve received bytes, directions, time, resize and lifecycle events | Canonical F01/D01/D recording owners; privacy policies may intentionally omit TX |
| Terminal screen/scrollback | Cursor addressing, echo, colors, alternate screen, interactive programs | Mutable and bounded; not the original log file |
| Derived transcript | CILogg regex search, filters, highlighters, export, source navigation | Append-only with provenance and explicit projection limitations |

Do not insert timestamps, `[RX]`, `[TX]`, or metadata into the bytes passed to the emulator. Put them in transcript annotations or gutters. Never send display annotations back to the device.

Record host monotonic and wall-clock timing at the I/O boundary through D01. Label these host-observed times, not device execution times. Keep direction, actual channel, generation, event identity and raw-byte range. Across independent stdout/stderr pipes, sequence numbers represent host observation order, not a provable original total order.

### 5.2 stdout/stderr distinction

A conventional interactive PTY attaches stdin/stdout/stderr to a terminal and presents a combined output stream. Serial has RX/TX bytes, not intrinsic stdout/stderr metadata. Do not infer stderr from red text, prefixes, or message severity. [R10-R12]

An explicit process-pipe profile uses QProcess separate channels and can accurately label the pipe from which bytes arrived. It is not advertised as a full interactive TTY. With system `ssh` without a PTY, its local stderr may include both client diagnostics and remote stderr; do not label every byte as proven remote stderr. Native SSH channel attribution is a separate future capability. [R11-R13]

### 5.3 Transcript projection rules

Use the emulator's parsed state/events or a narrow instrumented wrapper; do not build a second ad hoc ANSI parser based on regex removal.

- Normal newline-terminated output becomes committed transcript records. Soft screen wrapping does not create artificial logical newlines.
- `10%\r20%\r100%\r\n` projects to one completed `100%` line; the raw record preserves every update.
- A prompt without LF appears immediately as live/uncommitted text, not after a line timeout. On disconnect, flush it as an explicitly partial transcript record.
- Backspace and line erase affect the live line. Revisiting earlier committed content creates a revision/annotation record rather than changing historical source IDs.
- Alternate-screen applications render correctly in Terminal. Default transcript policy emits enter/exit markers instead of pretending every repaint is a new log line. Preserve their raw output subject to recording limits.
- A full-screen program using the primary screen can also produce lossy text projection. Mark screen-rewrite activity and keep raw provenance; do not promise a perfect linear transcript for arbitrary TUIs.
- Bound unterminated lines using the shared limits. Emit explicit partial/continuation records or an explicit gap according to policy; never silently truncate.

Search and filtering apply to the transcript. They must not filter the live VT stream before emulation. Preserve legacy line-observation and response-matching semantics on their existing raw-line path; do not silently retarget saved response patterns to a newly normalized transcript or synthetic terminal annotations. Highlighters are an overlay/analysis feature, not a rewrite of terminal colors or bytes. Freeze source snapshots and use existing SourceLocation/EventRef mechanisms for navigation; do not invent original file line numbers for managed terminal output.

### 5.4 Capture ownership and replay

Keep exactly one writer per capture file. Initially preserve legacy serial capture format and let the new terminal subscribe to raw observations; do not also append a second copy of serial bytes to that file. A normalized transcript can be a separately identified derived file. New PTY/SSH sessions use the common recording path and a derived transcript from inception.

Durable binary recording and time-based replay integrate with their existing D/A roadmap owners when available. Baseline terminal delivery must include live log capture and read-only transcript reopening; full timed replay remains capability-gated rather than requiring a new recorder/replay framework.

Read-only replay feeds recorded RX and resize events into an isolated emulator. It never sends historical TX or emulator replies to a live endpoint. Redacted recordings advertise their gaps; do not claim byte-exact bidirectional replay when outgoing content was intentionally omitted.

## 6. Dependencies and platform implementation

### 6.1 Recommended dependency decisions

| Component | Decision | Rationale / gate |
|---|---|---|
| Terminal emulator | Upstream libvterm, pinned release/commit and checksum | Toolkit-independent emulator; verify MSVC and Qt integration in UT01. Qt Creator documents libvterm use and MIT attribution. [R14-R15] |
| Rendering/input | Native Qt widget | Shares CILogg fonts, theme, accessibility and deployment; no WebEngine requirement. |
| Serial | Existing Qt SerialPort backend | Already linked; one exclusive owner. [R3, R16] |
| Windows PTY | Win32 ConPTY | Real console hosting; API requires Windows 10 version 1809 or later. [R10] |
| Linux/macOS PTY | Native PTY APIs plus a small Unix launch helper | Correct controlling-terminal/job-control behavior without unsafe post-fork Qt work. [R12] |
| SSH v1 | Installed system OpenSSH client on the local PTY backend | Reuses SSH config, keys/agent, known_hosts, jump hosts, password prompts and resizing. [R13, R17] |
| Non-TTY capture | QProcess with SeparateChannels | Preserve actual process stdout/stderr when TTY behavior is unnecessary. [R11] |
| Tests | Existing Catch2/Qt Test; synthetic PTY helper; disposable SSH server | Deterministic tests, not dependency on a production router or SSH account. |

QTermWidget is not the default: its documented compatibility lists BSD/Linux/OS X, not Windows. A port or fork would need its own Windows proof. xterm.js plus QtWebEngine is a legitimate alternative but adds a browser runtime and bridge, so it is not the recommended initial architecture. Native libssh is optional later, not needed to meet SSH scope through OpenSSH. Its benefit would be structured authentication/channel control, at the cost of another SSH/crypto integration and packaging surface. [R18-R19]

UT01 must record the selected libvterm source, version, hash, license text, build flags, portability patches and supported escape-sequence subset. Do not assume the archived Neovim mirror is the maintained upstream. Do not claim that libvterm supplies PTY hosting or rendering.

### 6.2 Windows

Use `CreatePseudoConsole`, `ResizePseudoConsole`, `STARTUPINFOEX` with `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE`, and `CreateProcessW`. Resolve an absolute executable path and implement tested Windows argument quoting. Separate the per-session environment and cwd from CILogg's own environment.

ConPTY transports UTF-8/VT sequences. Its attached communication handles use synchronous I/O; service read/write paths off the GUI thread with independently progressable queues and a tested shutdown order. Drain final output, cancel blocked I/O safely, and close handles exactly once. Do not put `ClosePseudoConsole` on a path that waits for the same blocked reader. [R10, R20]

Provide profiles for PowerShell 7, Windows PowerShell when present, CMD, and WSL. A clean PowerShell profile uses `pwsh.exe -NoLogo -NoProfile`; the normal profile remains available. CMD may use `/D` to avoid AutoRun. Missing executables produce actionable capability errors, not hidden fallbacks.

WSL is a launch profile using `wsl.exe` under ConPTY, with explicit distribution/user selection and correctly handled Linux cwd. Do not require WSL for native Windows shells. Never close a tab by running `wsl --shutdown` or terminating the entire distribution. Test installed WSL behavior rather than assuming every local ConPTY child behaves identically. [R21]

Use scoped process ownership and a documented termination policy for descendants. Closing CILogg must not terminate unrelated processes. Do not advertise unsupported Windows builds as compatible merely because the source compiles.

### 6.3 Linux and macOS

Allocate a PTY, establish the child session/controlling terminal, attach its standard streams, set dimensions, and launch the selected shell with correct argv/env/cwd. Support resize notifications, foreground-process job control, EOF/HUP, exit status, and child reaping.

Recommended launch design: a small bundled Unix helper launched with `posix_spawn` performs session/TTY setup and then `exec`s the target. Keep the helper single-threaded and avoid post-fork Qt/C++ allocation in the multithreaded GUI. Establish a close-on-exec error-reporting channel so failed chdir/exec/setup cannot look like a healthy shell. Verify platform-specific PTY headers and linking with CMake checks rather than assuming `-lutil` everywhere.

Use nonblocking master I/O and readiness notifications. Retain current supported Linux/macOS versions and architecture matrix; include Intel and ARM64 where the repository already ships them. Package/sign the helper with the application. Test Bash and Zsh where installed, plus the user's configured shell. Do not set `TERM=xterm-256color` until the advertised subset passes the compatibility suite; allow a documented conservative profile for limited devices.

### 6.4 SSH

`SshProcessTransport` is a configured local PTY child running OpenSSH, not a second SSH implementation. Supply validated argv for host/alias, port, user, identity path, terminal request, and optional jump host. Do not build a local `sh -c` or `cmd /c` string from these fields. Validate option-like host values and argument boundaries.

Interactive SSH requests a remote PTY (normally `-tt`). Use an explicit host-key policy: default `StrictHostKeyChecking=ask`, preserve known_hosts, never silently accept a changed key. Show native prompts in the terminal and require real user approval. Do not auto-type acceptance or passwords. Keep agent forwarding off by default; using a local agent for authentication is not agent forwarding. Disable unnecessary local SSH escape handling or document it; a transparent profile can use `-e none`.

System OpenSSH is a runtime prerequisite, not assumed installed everywhere. Discovery failure disables SSH creation with a precise diagnostic; other terminal types continue to work. Do not silently install software. Native OpenSSH and its user's config may support behavior beyond the CILogg form; distinguish trusted user-local configuration from imported project profiles.

A started `ssh` process does not prove authentication succeeded. Expose transport/process state accurately and report remote authentication status as unknown unless there is a reliable structured source. Do not parse localized prompt text to claim a verified authenticated state. Network loss ends the generation; reconnect is a new shell and may not preserve the prior remote process.

## 7. Security, privacy and arbitration

### 7.1 Safe output handling

Disable OSC 52 clipboard reads/writes by default, OSC-based local file operations, automatic downloads, and external-program launch. Sanitize terminal titles; they cannot replace a trusted endpoint badge. Hyperlinks require an explicit user click and safe scheme handling. Limit control-sequence/title lengths; bell and title storms are rate-limited. Fuzz malformed, oversized, and fragmented escape sequences.

Trust is not inherited from a capture file. Replay cannot execute output sequences, create live sessions, or transmit input. Imported projects/profiles may describe commands but cannot launch them on import or restore without explicit user initiation. Secrets, paths, and terminal payloads must not leak to crash reports or debug logging by default.

### 7.2 Input privacy

Default interactive TX payload recording and persistent local input history are off. Live private payloads still reach the chosen transport, but public events, logs, receipts, and telemetry contain only permitted metadata. Keep local line history in memory, per session; a sensitive-input action bypasses history and local echo.

This cannot prevent the remote program from echoing a secret into RX. Explain that limitation and provide Pause Recording/Sensitive Input controls; do not promise perfect password detection from a generic byte stream. Backend-specific echo-state information may improve behavior, but prompt heuristics are not a security boundary.

### 7.3 One write arbiter

Manual keys, paste, line submission, Actions, automatic responses, scripts and terminal replies share the canonical D01 write path. Carry endpoint ID, generation, origin and privacy flags. Continue only the unsent suffix after partial writes. Receipts distinguish accepted/queued/written/error states and never claim peer execution merely because an OS write succeeded.

While manual input is armed, hold a session-scoped manual lease and suspend/reject automated application writes with a visible `endpoint_busy` reason. Do not silently buffer old auto-responses for later release. Disarming manual input restores the previously configured automation permission. An explicit controlled mixed-input mode can be added later, not guessed from prompts or idle timeouts.

Safe terminal protocol replies remain allowed through a restricted path, but cannot interleave inside a logical key/paste/write transaction. Unknown or disallowed terminal commands never receive a general-purpose execution capability. Disconnect invalidates leases and pending writes immediately.

All automated terminal mutation is opt-in per session and passes existing authorization. Opening a live terminal expands automation power; a model or script reading a log must not gain arbitrary shell input automatically.

## 8. Resource and performance requirements

Reuse the D01 planning limits unless the canonical measured budget changes: 16 active endpoints, 8 MiB RX staging and 8 MiB pending TX per endpoint, maximum 64 KiB chunks, and 1 MiB logical messages. Retained-byte snapshot ownership and limits remain with D01; do not allocate another unbounded terminal copy. [R8-R9]

Proposed terminal-specific defaults: at most 10,000 scrollback rows AND 16 MiB scrollback storage, whichever limit is reached first; at most 1,000 columns by 500 rows; at most 64 KiB for a single control string. Validate bounds before allocation. Limit font-size/resize-induced allocations and coalesce redundant paint/resize requests. A disk-backed transcript can grow subject to configured rotation/retention, not RAM growth.

Raw capture and terminal parsing must remain ordered even when repainting is throttled. If an unthrottleable serial source exceeds capacity, report a gap/disconnect according to shared policy; never describe the trace as lossless. A disk-full condition must be visible and must not deadlock input. Do not report strict losslessness across deliberate retention rotation or privacy omission.

Measure against G02's reference hardware/build: idle CPU, memory after scrollback caps, sustained RX with simultaneous typing, write latency, shutdown latency, search responsiveness, and file-only viewer regressions. Proposed interactive target is p95 enqueue-to-backend-accept latency below 100 ms under a synthetic four-session, 1 MiB/s-per-session workload on the recorded reference machine. This is a target to validate, not a measured result or a device response-time promise. Record achievable platform budgets before a release claim.

## 9. File map

Paths marked new below are proposed; UT00 must reconcile them with branch changes.

**Existing files to extend:** `src/ui/include/streamsession.h`, `src/ui/src/streamsession.cpp`, `src/ui/include/serialcaptureworker.h`, `src/ui/src/serialcaptureworker.cpp`, `src/ui/include/streamsourceregistry.h`, `src/ui/src/streamsourceregistry.cpp`, `src/ui/include/crawlerwidget.h`, `src/ui/src/crawlerwidget.cpp`, `src/ui/src/mainwindow.cpp`, `src/ui/src/mainwindowproject.cpp`, `src/ui/src/configurationexport.cpp`, `src/ui/CMakeLists.txt`, top-level `CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `tests/ui/CMakeLists.txt`, `tests/helpers/CMakeLists.txt`, `DOCUMENTATION.md`, `MCP.md`, and the canonical Workbench ledger.

**New terminal-specific headers under `src/ui/include/`:** `terminaltypes.h`, `terminalemulator.h`, `terminalcontroller.h`, `terminaltranscript.h`, `terminalinputrouter.h`, `terminalwidget.h`, `sessionterminalview.h`, `terminalprofile.h`, `terminalprofiledialog.h`, `terminaltransportcontrol.h`, `conptytransport.h`, `unixptytransport.h`, `sshprocesstransport.h`, `processpipetransport.h`.

**Matching implementations under `src/ui/src/`:** one `.cpp` per responsibility above as needed. Platform transport sources are selected by CMake. Add `mainwindowterminal.cpp` for terminal integration rather than expanding MainWindow into another monolith. Existing large-file paint code remains a log renderer; do not turn its immutable line storage into an editable terminal screen.

**Other new paths:** `3rdparty/libvterm/` and its project-consistent CMake integration; `src/app/ptyhost/main.cpp` for the Unix helper; `tests/helpers/terminal_fixture.cpp`; unit tests named in the cards; `tests/ui/terminalview_test.cpp`; `test_data/terminal/` fixtures; `docs/terminal.md`.

D01's `streamtransport.h`, `communicationtypes.h/.cpp`, and `recordingbytesource.h/.cpp` remain D01-owned. Commander/MCP/Python exact current implementation paths are resolved in UT00 from the dispatcher, not guessed from older roadmap paths. No new terminal RPC server is created.

---

## 10. Implementation phases and task cards

Every code card follows the same reviewable cycle: add its specified failing tests; run the focused tests and confirm the intended failure; implement the narrow change; run focused plus affected legacy tests; record evidence and commit. Keep platform work independent until interfaces are stable. Never replace runtime evidence with a completed checkbox.

### Phase 0 - Reconcile the branch and shared foundations

#### UT00 - Baseline, ownership, and execution integration

**Depends on:** Existing Workbench ordering; D01/F01 prerequisites remain authoritative.

**Files:** Canonical Workbench README, task index, progress ledger and a new terminal addendum; current AGENTS.md/BUILD.md and relevant code are read-only inputs initially.

**Produces:** Exact integration map, resolved shared API owners, platform matrix, dependency lock decision, and terminal task registration.

- [ ] Record HEAD, dirty worktree, branch, SDK/compiler, Qt matrix, build scripts and current test state without overwriting local changes.
- [ ] Trace serial RX/TX, session/tab association, global shortcuts, Actions generations, Commander dispatch, plugin capabilities, project persistence and existing recording/replay status.
- [ ] Map each UT card to existing F/D/U/A/E requirements. Register UT IDs in the one canonical ledger; preserve approved priority and dependency edges.
- [ ] Run required repository impact analysis before later symbol edits; document unavailable tools rather than pretending the analysis ran.
- [ ] Establish G02/G03 baseline/parity evidence or explicitly leave those prerequisite gates open. Commit only the reconciled plan/ledger slice.

**Acceptance:** No parallel transport, recorder, authorization path, or execution ledger is introduced. The plan validator passes after registration; its result is not labeled product validation.

### Phase 1 - Terminal engine, transcript, and same-window interaction

#### UT01 - Emulator dependency and terminal model

**Depends on:** UT00. A fake-stream model test can run before live D01 integration.

**Files:** New libvterm integration, `terminaltypes.*`, `terminalemulator.*`, `tests/unit/terminalemulator_test.cpp`, `test_data/terminal/`, relevant CMake.

**Consumes/produces:** Raw RX bytes plus TerminalSize -> bounded TerminalScreenSnapshot; TerminalKeyEvent -> encoded bytes; safe terminal replies -> callback, not direct transport access.

- [ ] Add `[terminal][UT01]` tests: `Split UTF8 is decoded once`; `CSI split at every boundary is invariant`; `Prompt without LF is visible`; `CR and backspace update live cells`; `Alternate screen restores primary`; `Resize preserves valid cursor`; `Huge OSC is bounded`; `OSC52 cannot touch clipboard`.
- [ ] Run the test registration and confirm the absent wrapper fails; build the pinned dependency on MSVC and one Unix toolchain before adopting it.
- [ ] Implement the wrapper, screen snapshot, safe reply callback, supported mode/key/paste encoding, Unicode-cell behavior and explicit sequence limits.
- [ ] Run the compatibility fixtures on Windows/Linux/macOS configurations available in CI; document unsupported sequences and any portability patches.
- [ ] Commit the dependency lock, notices, wrapper and passing tests together.

**Acceptance:** No hand-written replacement VT engine. An unsupported platform or unbounded parser path is a blocker, not hidden by falling back to plain-text display.

#### UT02 - Session attachment, raw events, and derived transcript

**Depends on:** UT01 and implemented F01/D01 contracts.

**Files:** New `terminalcontroller.*`, `terminaltranscript.*`; extend StreamSession integration; new `tests/unit/terminalcontroller_test.cpp`, `tests/unit/terminaltranscript_test.cpp`.

**Consumes/produces:** Canonical EventRecord/EventRef and recording reader -> emulator feed plus append-only transcript with stable source mapping.

- [ ] Add failing tests: `Projection never consumes timestamp decorations`; `CR progress commits final line`; `Soft wrap is not a logical newline`; `Disconnect flushes a partial prompt`; `Old generation is ignored`; `Sensitive TX is not RX`; `Alternate screen emits boundaries`; `Replay never writes to live transport`.
- [ ] Run `[terminal][UT02]`, confirming these behaviors are not yet supplied.
- [ ] Attach the controller without taking connection ownership. Preserve one ingress path, raw provenance and one writer per file. Use existing legacy capture plus a separate derived transcript where needed.
- [ ] Verify all source IDs remain stable under repaint/resize, recording pause and file rotation; verify line buffers/queues obey shared caps.
- [ ] Run existing serial/Actions regressions and commit with capture-format compatibility evidence.

#### UT03 - Direct typing in the existing log window

**Depends on:** UT02.

**Files:** New `terminalwidget.*`, `sessionterminalview.*`, `terminalinputrouter.*`, `mainwindowterminal.cpp`; extend CrawlerWidget and shortcut wiring; `tests/ui/terminalview_test.cpp`.

**Consumes/produces:** Controller snapshots and capabilities -> native interactive surface; human key/paste/line intent -> canonical write requests.

- [ ] Add UI tests: `Main viewport types to selected endpoint`; `Log mode has inline draft`; `Search never transmits`; `Inspect mode never transmits`; `Switching presentation preserves generation`; `IME commit sends once`; `CtrlC reaches terminal`; `Selection survives RX`; `Scrolled input returns to live`; `Multiline paste requires approval`.
- [ ] Run the focused UI tests against a fake endpoint; confirm current viewer behavior fails the new expectations.
- [ ] Implement presentation/input-state controls, keyboard focus/ShortcutOverride, IME, copy/paste, local draft, live cursor, selection, resize, theme, DPI and accessibility names.
- [ ] Verify a newline-free prompt can be edited while logs arrive, without launching an Actions dialog or a separate dock.
- [ ] Run existing viewer/quick-find/filtered-view tests and commit with screenshots and exact automation selectors.

**Acceptance:** A user can click the existing session viewport and type. Merely adding a bottom terminal dock is a failed UT03.

### Phase 2 - Serial device console

#### UT04 - Existing serial transport integration and line-control safety

**Depends on:** UT03, D01 partial-write/lifecycle foundation.

**Files:** SerialCaptureWorker, StreamSession, StreamSourceRegistry, profile integration; `tests/unit/terminalserial_test.cpp`; extend serial lifecycle UI tests.

**Consumes/produces:** Existing QSerialPort endpoint -> same session's terminal and log presentations; human input -> existing byte transport.

- [ ] Add tests: `One serial owner serves log and terminal`; `Actions disabled still allows manual typing`; `Partial write sends unsent suffix only`; `Unplug invalidates pending input`; `Mode switch does not toggle DTR`; `Break is not CtrlC`; `Local echo is explicit`; `All 256 RX byte values remain available in raw provenance`.
- [ ] Confirm failures and identify whether D01 already supplies each write/lifecycle fix; do not duplicate it.
- [ ] Connect raw observation and input; remove per-keystroke blocking write behavior through the shared asynchronous continuation path. Implement explicit serial Enter/Backspace/encoding/flow-control and line-signal controls.
- [ ] Run virtual-port tests plus manual ESP32/UART/router-console checks when hardware is available. Mark unavailable hardware tests skipped, not passed.
- [ ] Commit only after serial capture formatting, response counters/generations and existing reconnect behavior regressions pass.

**Acceptance:** Existing serial capture is not reopened for terminal use; no unexpected ESP32 reset occurs from changing presentation. Native serial capture and a separate `idf.py monitor` cannot both own the same port; surface the conflict rather than auto-closing unrelated sessions.

### Phase 3 - Local shells on all supported operating systems

#### UT05 - Windows ConPTY backend

**Depends on:** UT02, D01; integrates with UT03.

**Files:** New `conptytransport.*`, platform factory/CMake, terminal fixture helper and `tests/unit/conptytransport_test.cpp`.

**Consumes/produces:** Typed local-process endpoint + argv/env/cwd -> canonical events/receipts; ITerminalControl -> native resize.

- [ ] Add Windows tests: `Fixture sees a terminal`; `Interactive prompt needs no LF`; `UTF8 survives boundaries`; `Resize updates child dimensions`; `CtrlC interrupts child not GUI`; `Spaces and quotes preserve argv`; `Final output drains`; `Blocked writer close does not hang`; `No handles leak after 100 lifecycles`.
- [ ] Confirm expected failures and register Windows-only cases without marking other hosts passed.
- [ ] Implement native ConPTY creation/process attachment, independent I/O progress, cancellation/close ordering and scoped process ownership. Runtime-check API availability where older application targets remain supported.
- [ ] Exercise CMD, PowerShell and an interactive fixture under output load; deploy all test runtime dependencies through existing scripts.
- [ ] Commit backend plus lifecycle evidence. Do not replace a failing real-PTY test with a QProcess-pipe test.

#### UT06 - Linux/macOS PTY backend and helper

**Depends on:** UT02, D01; can run alongside UT05 after contracts stabilize.

**Files:** New `unixptytransport.*`, `src/app/ptyhost/main.cpp`, helper CMake/install wiring; `tests/unit/unixptytransport_test.cpp`.

**Consumes/produces:** Same typed local-process endpoint and ITerminalControl as UT05; no Windows assumptions in public contracts.

- [ ] Add tests: `isatty succeeds on stdio`; `Child owns controlling terminal`; `Foreground job receives CtrlC`; `Resize delivers new dimensions`; `Exec failure reports errno`; `EOF and child exit complete once`; `No zombie or FD leak after 100 lifecycles`; `Unrelated process survives close`.
- [ ] Run on Linux and macOS separately and confirm current missing backend fails.
- [ ] Implement native PTY allocation, helper spawn/exec, terminal setup, nonblocking master I/O, resize, process exit and bounded cleanup. Compile-check platform headers/libraries.
- [ ] Test Bash/Zsh and available foreground job-control cases; verify helper installation and macOS signing/package inclusion.
- [ ] Commit with per-platform evidence and explicit skipped configurations.

#### UT07 - Shell profiles, WSL, and separate-channel process capture

**Depends on:** UT05/UT06 for relevant hosts, UT03.

**Files:** New `terminalprofile.*`, `terminalprofiledialog.*`, `processpipetransport.*`; `tests/unit/terminalprofile_test.cpp`, `tests/unit/processpipetransport_test.cpp`.

**Consumes/produces:** Versioned TerminalProfile -> validated EndpointSpec, selected backend and initial view/input policy.

- [ ] Add tests: `Normal and clean PowerShell are distinct`; `Missing shell is actionable`; `Environment override is session-local`; `Cwd preserves spaces`; `WSL distribution is an argv item`; `Pipe stdout stderr remain distinct`; `Pipe mode is not advertised as TTY`; `Malformed imported profile never starts a process`.
- [ ] Confirm failures; resolve executables to trusted absolute paths and test discovery without auto-installation.
- [ ] Add launch profiles for CMD/PowerShell/Bash/Zsh/WSL where supported. Add explicit pipe-capture mode and document its limitations. Store no secrets.
- [ ] Verify WSL-specific resize, cwd, Unicode and close behavior on a machine with WSL; test that no shutdown/distribution-terminate command is issued.
- [ ] Commit profile UI, migrations and launch fixtures together.

### Phase 4 - SSH in the same terminal/log session

#### UT08 - System OpenSSH integration

**Depends on:** UT07 and platform PTY backend.

**Files:** New `sshprocesstransport.*`; profile UI; `tests/unit/sshterminal_test.cpp`; disposable SSH fixture configuration under test helpers.

**Consumes/produces:** Typed SSH profile -> local PTY running absolute OpenSSH executable; same terminal/log/events contract as serial and local shells.

- [ ] Add tests: `Unknown host requires user acceptance`; `Changed host key fails`; `Key and agent auth work`; `Password is not in TX recording`; `Remote resize works`; `Option-like host cannot inject arguments`; `Missing ssh leaves other backends working`; `Disconnect does not replay input`.
- [ ] Run unit argv/security tests first, then a loopback isolated SSH server with temporary keys and known_hosts, never the user's production credentials.
- [ ] Implement host/user/port/identity/config-alias and jump-host fields with explicit PTY request, strict host verification and no plaintext password handling. Preserve accurate process-vs-authentication state.
- [ ] Test Linux/OpenWrt-compatible shells where available, key/passphrase/password prompts, interactive command cancellation, sustained remote output, and reconnection boundaries.
- [ ] Commit with OpenSSH version and platform evidence. An ordinary non-PTY remote command is not sufficient acceptance for interactive SSH.

### Phase 5 - Automation compatibility, trust, and persistence

#### UT09 - Write arbitration and privacy

**Depends on:** UT04 and UT08 for end-to-end coverage; unit logic may start after UT02.

**Files:** Shared session/D01 arbitration extension, `terminalinputrouter.*`, relevant action dispatch; `tests/unit/terminalarbitration_test.cpp`.

**Consumes/produces:** All input origins -> one generation-bound write path with manual lease and privacy-aware receipts.

- [ ] Add tests: `Action cannot splice into manual input`; `Manual lease does not alter persisted Actions flags`; `Automation does not queue stale responses`; `Reply cannot interleave inside a logical write`; `Disconnect rejects old lease`; `Sensitive input reaches backend but not observers`; `Redaction never matches an auto-response`.
- [ ] Confirm failures while retaining the existing Actions/Responses coupling tests unchanged.
- [ ] Implement explicit manual lease/suspension, restricted terminal replies and public/private payload separation. Do not use prompt detection or idle timeout to decide command completion.
- [ ] Run concurrent input, paste, response, cancellation and reconnect fixtures on fake and available real transports.
- [ ] Commit arbitration only when both manual typing with Actions disabled and legacy scripted serial flows pass.

#### UT10 - Profiles, project persistence, and capability UI

**Depends on:** UT07/UT08/UT09.

**Files:** Profile implementation/UI, session/project persistence, `mainwindowterminal.cpp`; `tests/unit/terminalpersistence_test.cpp` and UI tests.

**Consumes/produces:** Validated profile/session state -> versioned saved settings; restored state -> disconnected inspect-mode view.

- [ ] Add tests: `Legacy serial settings load unchanged`; `Unknown fields survive or fail explicitly by schema`; `Restore does not connect`; `Imported command requires user initiation`; `Secrets are absent`; `Tab focus never changes endpoint binding`; `Feature disabled still opens existing logs`.
- [ ] Confirm expected failures using golden legacy configurations and malicious imported examples.
- [ ] Implement profile selection/editor, endpoint badge, capture controls, capability/error inspector, manual-lock state, explicit connect/reconnect and migrations. Preserve stable IDs.
- [ ] Verify normal log-only tabs expose no misleading input controls; include accessibility names and keyboard-only navigation.
- [ ] Commit migration fixtures and user-visible behavior evidence.

#### UT11 - Commander, CLI, MCP and Python parity

**Depends on:** UT10 and existing public API owners/parity harness.

**Files:** Current dispatcher/schema/discovery paths resolved by UT00, existing Python client, `MCP.md`; new `tests/unit/terminalcommands_test.cpp` and MCP/parity cases.

**Consumes/produces:** Existing authorized command requests -> the same session services used by GUI; no separate terminal RPC server.

Reuse `open_stream`, `close_stream`, `get_stream_capabilities`, `send_bytes` and write receipts as D01 defines them. Proposed additions: `get_terminal_state`, `set_terminal_mode`, `send_terminal_key`, `paste_terminal_text`, `resize_terminal`, and `get_terminal_screen`. Include endpoint ID and generation on mutating requests. Screen queries are bounded, revisioned snapshots; never return the full unbounded transcript.

- [ ] Add tests: `GUI CLI MCP Python use the same endpoint`; `Unknown generation cannot type`; `Read-only client cannot send`; `Automation permission is session-scoped`; `Multiline automated paste requires policy`; `Screen result is bounded`; `Capability discovery describes limitations`; `Large counters serialize exactly`.
- [ ] Confirm failures using the existing parity harness and real dispatch rather than mocked duplicate handlers.
- [ ] Extend discovery/validation/authorization and implement operations through the controller/session. Cap a screen response at 16,384 cells and provide a stable revision plus explicit truncation/rectangle information.
- [ ] Test cancellation, invalid keys, zero/oversized dimensions, stale session IDs, repeated request IDs and restricted credential visibility. Encode counters/offsets beyond JSON-safe precision as decimal strings following workbench conventions.
- [ ] Commit public documentation and parity evidence with the operations.

### Phase 6 - Release hardening and documentation

#### UT12 - Adversarial, performance and cross-platform validation

**Depends on:** UT00-UT11 for applicable features.

**Files:** Terminal fixtures/tests/benchmarks, existing CI setup and packaging, notices; no unrelated release-workflow redesign.

- [ ] Add failing regression cases for the five Review Focus classes not already fully covered, plus disk full, control-string floods, endless non-newline output, large paste, Unicode width and closing during callback publication.
- [ ] Run sanitizer/fuzz configurations where supported, the deterministic emulator suite, and real PTY lifecycle/SSH tests separately from unit-only checks.
- [ ] Fix only issues exposed by those gates; preserve caps and observable gap/error semantics rather than hiding load failures with unlimited buffers.
- [ ] Measure G02-aligned throughput, input latency, memory, shutdown and file-only viewer regressions; record hardware/build/Qt/backend/SSH versions, raw results and skipped cases.
- [ ] Verify packaged installation (including Unix helper and libvterm notices), feature-on/off builds, existing serial/action/plugin suites and all maintained platform configurations before committing release evidence.

**Acceptance:** No unsupported platform is described as tested. Simulated serial/SSH evidence is labeled separately from hardware/manual evidence.

#### UT13 - User guide and final acceptance

**Depends on:** UT12.

**Files:** New `docs/terminal.md`; update DOCUMENTATION.md, MCP.md, README feature entry and canonical progress/coverage records.

- [ ] Document opening serial/local/SSH sessions; direct input; Inspect/Character/Line differences; shortcuts; profiles; capture/transcript/raw distinctions; host-key prompts; password/privacy limitations; conflicts with other serial tools; supported terminal features.
- [ ] Include practical walkthroughs: ESP32 console while logs arrive; router serial shell; router SSH; clean PowerShell build shell; WSL shell; regex filtering captured output without killing the shell.
- [ ] Run the final acceptance script below on available targets and attach reproducible results/screenshots. Record deferred optional features explicitly.
- [ ] Validate plan links/ledger, public operation discovery, dependency attribution, migration documentation and rollback instructions.
- [ ] Commit documentation/evidence and request review. Do not mark complete merely because code merged or a PR bot approved it.

## 11. Verification commands and evidence

Use the repository's current environment setup rather than copying the inspected machine's absolute SDK paths. From an initialized developer environment at the repository root, the existing Windows helper entry points are:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\codex\build-windows.ps1 -Config RelWithDebInfo
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\codex\run-tests.ps1 -Config RelWithDebInfo
```

For an already configured build tree:

```text
cmake --build build_root --config RelWithDebInfo
ctest --test-dir build_root -C RelWithDebInfo --output-on-failure
```

Register focused terminal CTest entries/labels in the owning cards, then run:

```text
ctest --test-dir build_root -C RelWithDebInfo -L terminal --output-on-failure
```

On Linux/macOS, use the repository's supported configure/toolchain settings and a build directory under `build_root`; do not invent an unverified universal configure command. Runtime GUI/PTY tests need a usable display environment where applicable; offscreen unit tests are not evidence that native focus/IME/accessibility works.

For roadmap integration, run the existing validator from its package directory after adding the terminal tasks:

```text
python validate_plan.py .
```

Each card's evidence records its commit, exact commands, exit status, tests run/failed/skipped, platform, dependency versions, and artifacts. A compile-only cross-build does not prove runtime terminal compatibility.

## 12. Final definition of done

- [ ] In a connected serial log tab, click the main viewport and type without opening Actions or another terminal panel.
- [ ] A newline-free prompt is visible; Enter, Backspace, Tab, arrows and Ctrl+C work according to the selected profile.
- [ ] Local PowerShell/CMD, Linux/macOS shells, WSL where installed, and SSH meet their platform-specific acceptance gates.
- [ ] New logs arriving during input neither overwrite a local draft nor change which endpoint receives keys.
- [ ] Terminal, Log, and available Split presentations share one session and preserve connection/capture state.
- [ ] Existing regex search, filters, highlighters and large-file performance remain usable on the derived transcript.
- [ ] Terminal cursor movement never mutates the historical log file or corrupts provenance.
- [ ] PTY/serial output is not falsely attributed to stdout/stderr; separate-channel pipe capture is explicitly distinguished.
- [ ] Partial writes, disconnects, shutdown, disk pressure and reconnect produce no silent retransmission or loss claims.
- [ ] Password/TX privacy, host-key verification, unsafe escape sequences, imports and replay safety pass their tests.
- [ ] Manual typing and Actions/scripts cannot silently interleave; existing Actions/Responses coupling still passes.
- [ ] GUI, CLI, MCP and Python expose the same capability-checked session behavior.
- [ ] Packaging, license notices, platform evidence, migration/rollback, user guide and canonical ledger are complete.

The headline success condition is: **CILogg's connected log window itself is a usable terminal for both system and device sessions, while remaining a log-analysis workspace.**

## 13. Codex starter instruction

```text
Implement the CILogg unified terminal task in this document.

The user explicitly requires BOTH system terminals and device terminals,
with direct typing in the existing connected log window. A standalone dock,
an Actions-only command sender, or a new process per entered line does not
satisfy the requirement.

Start with UT00. Inspect the current checkout and reconcile this snapshot
with AGENTS.md, docs/workbench/decisions.md and the canonical Workbench plan.
Reuse StreamSession, F01 canonical events/provenance, D01 IStreamTransport
and write receipts, and the existing Commander/CLI/MCP/Python route.
Do not create another transport, recorder, action engine or execution ledger.

Implement in reviewable, tested increments. Keep serial compatibility and
Qt5/Qt6/C++17 support. Use real ConPTY/PTY for interactive shells, existing
QSerialPort ownership for serial, and system OpenSSH over a PTY for SSH v1.
Run the repository's required impact analysis before symbol edits, focused
red/green tests for each card, affected regressions, and final platform gates.
Do not silently alter the approved workbench priority or mark missing
prerequisite/platform/hardware evidence complete. Report concrete blockers
and the exact work/evidence completed instead of substituting a lesser feature.
```

## 14. Sources and evidence register

Repository sources below are pinned to the inspected commit. External documentation was consulted on 2026-11-04. These references establish current APIs and existing code, not successful execution of this proposed plan.

- **R1 - Repository baseline:** https://github.com/dm17ryk/klogg/commit/90f10548fd038995034514283e5c74b197fa06e0
- **R2 - Session API:** https://github.com/dm17ryk/klogg/blob/90f10548fd038995034514283e5c74b197fa06e0/src/ui/include/streamsession.h
- **R3 - Serial settings/worker:** https://github.com/dm17ryk/klogg/blob/90f10548fd038995034514283e5c74b197fa06e0/src/ui/include/serialcaptureworker.h
- **R4 - Serial raw RX, formatted capture, write behavior:** https://github.com/dm17ryk/klogg/blob/90f10548fd038995034514283e5c74b197fa06e0/src/ui/src/serialcaptureworker.cpp
- **R5 - UI build dependencies:** https://github.com/dm17ryk/klogg/blob/90f10548fd038995034514283e5c74b197fa06e0/src/ui/CMakeLists.txt
- **R6 - Main session view:** https://github.com/dm17ryk/klogg/blob/90f10548fd038995034514283e5c74b197fa06e0/src/ui/include/crawlerwidget.h
- **R7 - Accepted workbench integration constraints:** https://github.com/dm17ryk/klogg/blob/90f10548fd038995034514283e5c74b197fa06e0/docs/workbench/decisions.md
- **R8 - Existing transport foundation task:** https://github.com/dm17ryk/klogg/blob/90f10548fd038995034514283e5c74b197fa06e0/plans/cilogg-workbench-2026-10-21/tasks/D01.md
- **R9 - Shared communication/recording contracts:** https://github.com/dm17ryk/klogg/blob/90f10548fd038995034514283e5c74b197fa06e0/plans/cilogg-workbench-2026-10-21/07-docklight.md
- **R10 - ConPTY API, UTF-8/VT and OS requirements:** https://learn.microsoft.com/en-us/windows/console/createpseudoconsole
- **R11 - QProcess channels, launch and process behavior:** https://doc.qt.io/qt-6/qprocess.html
- **R12 - PTY and controlling-terminal semantics:** https://man.openbsd.org/openpty.3
- **R13 - OpenSSH client options/PTY/identity/jump hosts:** https://man.openbsd.org/ssh.1
- **R14 - Upstream libvterm architecture:** https://www.leonerd.org.uk/code/libvterm/
- **R15 - Qt Creator libvterm use and MIT attribution:** https://doc.qt.io/qtcreator/qtcreator-attribution-libvterm.html
- **R16 - Qt SerialPort exclusivity and buffering:** https://doc.qt.io/qt-6/qserialport.html
- **R17 - OpenSSH configuration and trust behavior:** https://man.openbsd.org/ssh_config.5
- **R18 - QTermWidget documented platform scope:** https://github.com/lxqt/qtermwidget
- **R19 - Optional native SSH shell/PTY API:** https://api.libssh.org/stable/libssh_tutor_shell.html
- **R20 - ConPTY communication threads/lifecycle guidance:** https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session
- **R21 - WSL launch/distribution and shutdown semantics:** https://learn.microsoft.com/en-us/windows/wsl/basic-commands
- **R22 - Existing Windows developer instructions:** https://github.com/dm17ryk/klogg/blob/90f10548fd038995034514283e5c74b197fa06e0/AGENTS.md
- **R23 - Root CMake options:** https://github.com/dm17ryk/klogg/blob/90f10548fd038995034514283e5c74b197fa06e0/CMakeLists.txt
- **R24 - Existing unit-test registration:** https://github.com/dm17ryk/klogg/blob/90f10548fd038995034514283e5c74b197fa06e0/tests/unit/CMakeLists.txt
- **R25 - Canonical roadmap and priority:** https://github.com/dm17ryk/klogg/blob/90f10548fd038995034514283e5c74b197fa06e0/plans/cilogg-workbench-2026-10-21/README.md

All new class names, file paths, task IDs, UI decisions and terminal-specific budgets in this document are proposals. Existing-code claims above are limited to the inspected sources; feature completeness and platform support require the implementation evidence described in the task cards.
