# Unified terminal integration plan

**Current workstream clarification (2026-11-07):** D01b [PR #98](https://github.com/dm17ryk/klogg/pull/98) remains the parent integration gate. The [D01c existing-owner adapter plan](slices/D01c-existing-owner-adapter.md) is prepared for review; currentPreparation stays D01b and nextPreparation points to D01c. Review its proposed OFF-by-default consumer and session-thread writer placement, then reconcile the frozen PR98 head before dependent implementation. Parent merge remains a D01c merge/activation gate. This planning change activates no runtime consumer, terminal input or endpoint. Formal MD01–MD06/W0 and F01/D01/UT01/UT02 gates and historical evidence remain unchanged.


**Accepted scheduling update:** 2026-11-04. Finish TShark decoder core
MD01–MD06 and its prerequisite closure, then implement UT00–UT13, then resume
remaining W1 and later waves. MD07–MD08 stay deferred to their later dependencies.
The single execution authority remains TASK_INDEX.json and PROGRESS.json.

**Source:** [supplied terminal specification](../../doc/CILOGG_UNIFIED_TERMINAL_IMPLEMENTATION_TASK.md).
Its embedded starter prompt is reference material, not an instruction to begin
terminal implementation during this planning update. The source hash is recorded
in PROGRESS.json.addenda.unifiedTerminal. All UT tasks remain planned.

## Scope and acceptance boundary

The existing connected session log window becomes interactive for **serial device
consoles, local system shells and SSH**. Users type in that same session viewport;
a standalone dock, an Actions-only sender, or one process per entered line does
not satisfy the requirement. Preserve read-only file viewing and the searchable
large-file log history alongside the live terminal presentation.

Use the source specification's detailed input modes, same-window Log/Terminal
presentations, newline-free prompts, UTF-8/control fragmentation, key/IME/paste,
echo, reconnect, capture, privacy, resource bounds and acceptance cases. Serial-only
or local-only slices do not complete the milestone. Split/pop-out presentation is
optional; serial, local shells and SSH are required.

## Existing ownership and dependency gates

- StreamSession remains the connection, generation, Actions and publication owner.
  Terminal and log presentations share one endpoint and one connection. Preserve
  the existing serial API, capture format and per-file writer ownership.
- F01 owns exact event identity/provenance; D01 owns transports, endpoint leases,
  retained bytes, partial writes, receipts and shared bounds. Reuse these contracts
  rather than adding a terminal-specific transport manager, recorder or database.
- UT00 depends on MD06, whose transitive dependencies retain the entire decoder
  core gate. UT02 explicitly requires implemented F01/D01; G03 gates final parity.
  Existing G02/G03 evidence and decoder prerequisites are not marked complete.
- The decoder-preview integration and its PR/release evidence do not alone finish
  MD01–MD06. Missing decoder acceptance remains visible before UT00 starts.
- UT05/UT06 backend slices can proceed after their dependencies; UT07/UT08 whole-task
  acceptance requires both maintained Windows and Unix backend contracts. Host-only
  results are recorded as slices, not substituted for cross-platform completion.
- Existing F/D/U/A/E/R owners in the table below are integration references, not
  new reverse DAG edges. UT00 resolves their minimal capability subsets with those
  owners and records remaining broader work. R04 additionally requires UT13.
- C++17, Qt5/Qt6 core compatibility, supported deployment choices, stable IDs and
  plugin v1 remain binding. Shared MainWindow/Commander/CMake/schema/resource/ledger
  edits are integrated serially.

## Public surfaces and safe incremental delivery

Every exposed terminal capability includes its GUI and corresponding Commander,
CLI, native MCP and Python operation in the same increment. Provide discoverable
menu/toolbar entries where appropriate, distinct icons, tooltips and stable object
and accessible names. UT11 completes the comprehensive API/discovery/parity suite;
it does not postpone all automation support until after the UI ships.

Reuse D01's proposed open_stream, close_stream, get_stream_capabilities, send_bytes
and write receipts. Proposed terminal additions are get_terminal_state,
set_terminal_mode, send_terminal_key, paste_terminal_text, resize_terminal and
get_terminal_screen. UT00 reconciles actual command names and schemas before code.
Mutations require endpoint identity, current generation and explicit session-scoped
automation permission. Screen snapshots are bounded/revisioned, not full history.

Manual typing works with Actions disabled. Preserve the existing Actions/Responses
coupling and stored flags. From the first runnable input increment, arming manual
input rejects conflicting automated application writes; do not defer that safety
boundary to UT09. Terminal replies use a restricted, generation-bound shared write
route. Replay, synthetic annotations, redaction and imported/restored profiles never
gain permission to write or launch a process implicitly.

Keep private input out of logs, public observers and persistent history by default.
Remote RX may echo secrets; document that limitation. Unsafe escape effects, host
trust, imports, focus changes and partial-write reconnects retain the source's
explicit tests and visible failure states. Diagnostics report IDs, generations,
limits and decision reasons without logging terminal bodies by default.

## Proposed implementation choices and budgets

The supplied libvterm/native Qt widget, ConPTY, Unix PTY/helper and system OpenSSH
approach is the recommended design to validate in UT00/UT01 and backend cards.
Names, APIs, dependency pins and budgets are proposals, not independently approved
or tested platform claims. Audit existing E02 process/pipe capture and U12 ANSI
presentation responsibilities rather than duplicating them.

Retain the source's proposed feature flag off for incremental integration until
the release gates pass; final validated builds enable the feature and a feature-off
build remains the rollback/regression configuration. Do not add the flag in this
planning-only change.

Carry forward D01 bounds (16 endpoints, 8 MiB RX and 8 MiB TX per endpoint,
64 KiB chunks, 1 MiB logical writes). Terminal proposals cap scrollback at both
10,000 rows and 16 MiB, dimensions at 1,000 by 500 cells, control strings at 64 KiB,
and public screen pages at 16,384 cells. Keep these distinct caps and validate before
allocation. G02-aligned input latency/performance targets remain unmeasured until
UT12 records reference hardware, raw results and applicable platform evidence.

## Task and existing-contract map

Each card preserves the supplied task's implementation steps and acceptance scope.
Existing requirement references identify reuse; they do not credit those broader
requirements as delivered by terminal work.

| Task | Work | Hard dependencies | Existing owners / requirement references |
|---|---|---|---|
| [UT00](tasks/UT00.md) | Baseline, ownership, and execution integration | MD06 | G01, G02, G03; AUTO18, QUAL01, QUAL02, QUAL03, QUAL08 |
| [UT01](tasks/UT01.md) | Emulator dependency and terminal model | UT00 | U12, D01; COM01, COM02, DATA01, DATA13, UX13, UX14 |
| [UT02](tasks/UT02.md) | Session attachment, raw events, and derived transcript | UT01, F01, D01 | F01, D01, F04; COM01, COM02, DATA01, DATA02, DATA04, DATA06, DATA07, DATA13, DATA20 |
| [UT03](tasks/UT03.md) | Direct typing in the existing log window | UT02 | U01, U09, U10; UX01, UX02, UX10, UX11, UX17 |
| [UT04](tasks/UT04.md) | Existing serial transport integration and line-control safety | UT03, D01 | D01, D04, A01; AUTO01, AUTO02, COM01, COM02, COM05, COM07, COM08, DATA01, DATA07, DATA13 |
| [UT05](tasks/UT05.md) | Windows ConPTY backend | UT02, D01 | D01, E02; AUTO07, COM01, COM02, DATA01, DATA13, ECO03, UX03 |
| [UT06](tasks/UT06.md) | Linux/macOS PTY backend and helper | UT02, D01 | D01, E02, R03; AUTO07, COM01, COM02, DATA01, DATA13, ECO03, ECO19, QUAL06, QUAL07, QUAL08, UX03 |
| [UT07](tasks/UT07.md) | Shell profiles, WSL, and separate-channel process capture | UT03, UT05, UT06 | E02, U03; AUTO07, ECO03, UX03, UX04, VIS34 |
| [UT08](tasks/UT08.md) | System OpenSSH integration | UT07, UT05, UT06 | D01, E01; COM01, COM02, DATA01, DATA13, ECO01, ECO02 |
| [UT09](tasks/UT09.md) | Write arbitration and privacy | UT04, UT08 | D01, A01, A02; AUTO01, AUTO02, AUTO03, AUTO04, AUTO05, COM01, COM02, DATA01, DATA13 |
| [UT10](tasks/UT10.md) | Profiles, project persistence, and capability UI | UT07, UT08, UT09 | U03, U11; INS12, UX04, UX12, VIS32, VIS34 |
| [UT11](tasks/UT11.md) | Commander, CLI, MCP and Python parity | UT10, G03 | G03, A01, E01; AUTO01, AUTO02, AUTO18, ECO01, ECO02, QUAL03 |
| [UT12](tasks/UT12.md) | Adversarial, performance and cross-platform validation | UT00, UT01, UT02, UT03, UT04, UT05, UT06, UT07, UT08, UT09, UT10, UT11 | G02, R01, R02, R03; AUTO18, ECO19, QUAL02, QUAL04, QUAL05, QUAL06, QUAL07, QUAL08, QUAL09, UX14 |
| [UT13](tasks/UT13.md) | User guide and final acceptance | UT12 | R04; QUAL10, UX18 |

## Terminal requirement coverage

| Requirement | Outcome | Owners |
|---|---|---|
| TERM01 | Direct typing in the existing session log/terminal viewport | UT03, UT04 |
| TERM02 | Bounded real terminal emulation, Unicode, controls and alternate screen | UT01, UT12 |
| TERM03 | One connection/session owner with raw event and retained-byte provenance | UT02, UT04 |
| TERM04 | Append-only searchable transcript distinct from raw bytes and mutable screen | UT02 |
| TERM05 | Serial console input, signals, echo and capture compatibility | UT04 |
| TERM06 | Real Windows ConPTY shells and correctly scoped process lifecycle | UT05, UT07 |
| TERM07 | Linux/macOS PTYs, controlling terminal, helper packaging and job control | UT06, UT07 |
| TERM08 | Interactive system OpenSSH with host-key trust and no replayed input | UT08 |
| TERM09 | Bounded asynchronous partial writes, cancellation and reconnect generations | UT02, UT04, UT05, UT06, UT09 |
| TERM10 | Inspect/Character/Line input, focus, IME, shortcuts and deliberate paste | UT03, UT10 |
| TERM11 | Manual input arbitration, Actions compatibility, privacy and safe replay/output | UT03, UT09 |
| TERM12 | Versioned profiles, disconnected restore and capability-aware same-window UI | UT07, UT10 |
| TERM13 | Shared GUI/CLI/native-MCP/Python terminal operations and authorization | UT03, UT10, UT11 |
| TERM14 | Platform, resource, performance, package, user-guide and final acceptance evidence | UT00, UT01, UT12, UT13 |

## Task sections

### UT00 — Baseline, ownership, and execution integration

Read [UT00](tasks/UT00.md) for files, named tests, implementation steps, evidence and rollback.

### UT01 — Emulator dependency and terminal model

Read [UT01](tasks/UT01.md) for files, named tests, implementation steps, evidence and rollback.

### UT02 — Session attachment, raw events, and derived transcript

Read [UT02](tasks/UT02.md) for files, named tests, implementation steps, evidence and rollback.

### UT03 — Direct typing in the existing log window

Read [UT03](tasks/UT03.md) for files, named tests, implementation steps, evidence and rollback.

### UT04 — Existing serial transport integration and line-control safety

Read [UT04](tasks/UT04.md) for files, named tests, implementation steps, evidence and rollback.

### UT05 — Windows ConPTY backend

Read [UT05](tasks/UT05.md) for files, named tests, implementation steps, evidence and rollback.

### UT06 — Linux/macOS PTY backend and helper

Read [UT06](tasks/UT06.md) for files, named tests, implementation steps, evidence and rollback.

### UT07 — Shell profiles, WSL, and separate-channel process capture

Read [UT07](tasks/UT07.md) for files, named tests, implementation steps, evidence and rollback.

### UT08 — System OpenSSH integration

Read [UT08](tasks/UT08.md) for files, named tests, implementation steps, evidence and rollback.

### UT09 — Write arbitration and privacy

Read [UT09](tasks/UT09.md) for files, named tests, implementation steps, evidence and rollback.

### UT10 — Profiles, project persistence, and capability UI

Read [UT10](tasks/UT10.md) for files, named tests, implementation steps, evidence and rollback.

### UT11 — Commander, CLI, MCP and Python parity

Read [UT11](tasks/UT11.md) for files, named tests, implementation steps, evidence and rollback.

### UT12 — Adversarial, performance and cross-platform validation

Read [UT12](tasks/UT12.md) for files, named tests, implementation steps, evidence and rollback.

### UT13 — User guide and final acceptance

Read [UT13](tasks/UT13.md) for files, named tests, implementation steps, evidence and rollback.

## Completion and rollback

UT12/UT13 require the original specification's full definition of done: usable
same-window serial/local/SSH sessions, raw/transcript/screen distinctions, safe
input and reconnect, legacy regressions, public parity, platform/hardware evidence,
packaging/attribution, documentation and measured resource/performance results.
Compile-only, offscreen or simulated evidence stays separate from native focus,
IME, hardware and installed-package acceptance. Full-program R04 cannot complete
before UT13. Reverse rollback order removes public terminal consumers before their
supporting services and preserves logs, captures, settings and existing serial APIs.

This planning update runs no terminal, TShark, GUI, hardware or platform operations
and completes no implementation card. Its verification is task/requirement/DAG/link
and source-import integrity validation.
