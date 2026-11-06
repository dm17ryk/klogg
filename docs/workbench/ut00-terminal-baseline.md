# UT00a — Unified Terminal baseline and ownership

Audit baseline: merged master `b1f820c28778f65f35401a285a9fa683970cd0f1` (PR #90), 2026-11-05. This is source/plan reconciliation, not a running terminal or product/platform acceptance.

## Priority and preview status

The user selected Unified Terminal as the next workstream after merging PR #90. Do not continue unrelated F01 expansion ahead of terminal preparation. The shipping TShark-first Preview already has GUI, tooltip and shared preview_message routing. PR #90 qualifies the repaired external EHCI pack with partial/binary synthetic fixtures. The qualified private pack is external and not redistributed.

This is not completion of MD01–MD06: generic typed decoder integration, original-log mappings/generated flags, source-incomplete assembly, licensing/distribution and release/performance gates remain open. Preserve those historical limitations. UT00a is an independently reviewable preparation slice; UT00's MD06 edge and UT02's F01/D01 dependencies remain unchanged. Implement needed session/write foundations under their existing owners, not a new terminal-specific service.

## Existing source map

All anchors below were read at the pinned audit baseline.

| Path / symbol | Current behavior | Terminal integration decision |
|---|---|---|
| [streamsession.h](../../src/ui/include/streamsession.h), StreamSession | Owns serial worker/thread, connection state, Actions/Responses generations and raw/line observers | Keep one session/connection. TerminalController owns emulation/input state only; it must not open another port. |
| [serialcaptureworker.cpp](../../src/ui/src/serialcaptureworker.cpp), onReadyRead, line328 | Reads original bytes; timestamp formatting affects a separate capture buffer; emits dataReceived(data), line373 | Feed emulation and canonical recording from raw bytes, never the timestamped capture. Current readAll/queued publication is not proof of D01 bounded ingestion. |
| [streamsession.cpp](../../src/ui/src/streamsession.cpp), setupWorker327; handleDataReceived453 | Worker RX reaches the session; dataObserved is emitted before LF line assembly; workerGeneration fences worker completion/errors | A prompt without LF can use raw RX. Add a generation-bound, bounded ingress attachment under UT02/D01; existing LF buffering is not terminal state. |
| [serialcaptureworker.cpp](../../src/ui/src/serialcaptureworker.cpp), sendData242 | Calls write once, reports partial writes, then waitForBytesWritten(1000). Sensitive observers receive redaction text; capture may add a TX annotation | D01/UT04 must add asynchronous partial-write receipts and no replay across generations. Neither redaction nor TX annotations are transmitted raw bytes. Sensitive manual input must avoid the existing ordinary payload hex diagnostics. |
| [streamsession.cpp](../../src/ui/src/streamsession.cpp), sendBytes301; [actionruntime.cpp](../../src/ui/src/actionruntime.cpp), sendBytes callers | Legacy sends enqueue the worker call; Actions and delayed action sends share this path | Preserve this API, but route it through the D01 arbiter before arming manual input. Reject conflicting automated writes immediately from the first input prototype. Emulation replies have a restricted, current-generation route. |
| [mainwindow.cpp](../../src/ui/src/mainwindow.cpp), open capture2559; commanderTargetStreamSession3649 | A shared StreamSession is attached to capture-path tabs; Commander resolves the target crawler/session and optional open requirement | Attach terminal/log presentations to the same tab/session. New operations retain explicit endpoint/session identity and generation; never infer destination from the last Actions tab or current keyboard focus. |
| [streamsourceregistry.h](../../src/ui/include/streamsourceregistry.h) | Active-port QSet with separate check/register operations | Extend the existing registry for D01 atomic endpoint leases; current API is not an atomic reservation contract. |
| [crawlerwidget.h](../../src/ui/include/crawlerwidget.h); [crawlerwidget.cpp](../../src/ui/src/crawlerwidget.cpp), registerShortcuts2236 | Central main/filtered views and child-wide viewer shortcuts | UT03 adds the live Terminal surface/inline Line draft within this existing tab. ShortcutOverride/IME routing must prevent search, filtered rows, dialogs or another tab from transmitting. Read-only files stay read-only. |
| [kloggapp.h](../../src/app/kloggapp.h), executeCommanderRequest542; [commander.cpp](../../src/ui/src/commander.cpp); [runtime.cpp](../../src/app/mcp/runtime.cpp) | Application dispatch and shared command catalog already serve scripts/scenarios, CLI and native MCP | Every exposed terminal capability uses this route in the same increment. No direct MCP-to-device path. Preserve tab/window selectors, request errors and result envelopes. |
| [plugin-sdk.md](../plugin-sdk.md); [pluginprocess.cpp](../../src/plugins/src/pluginprocess.cpp) | Plugin v1 declares datasource/converter/tool capabilities and separate process channels; acknowledged source streams exist | A text datasource or plugin process pipe is not a negotiated bidirectional terminal. Preserve protocol v1. E02 owns process/stdin capture; PTY and SSH use D01 transports. |
| [sessioninfo.cpp](../../src/ui/src/sessioninfo.cpp); [multiple-action-tabs.md](../multiple-action-tabs.md) | Existing session/view persistence and Actions/Responses coupling | UT10 persists disconnected profiles/settings only. No startup shell execution, reconnect command replay or input arming from restored state. Runtime arbitration must not rewrite saved Actions flags. |

D01 IStreamTransport/WriteRequest/WriteReceipt, immutable retained-byte backing, an atomic write arbiter, local PTY/ConPTY and system-SSH transports are planned contracts, not implemented baseline capabilities. Existing plugin QProcess wrappers do not satisfy those contracts. No new recorder, source resolver or scheduler is justified by this audit.

## Command and authority map

Existing operations include open_com, close_com, get_comm_status, start_comm, play_comm, pause_comm, stop_comm, start_logging, stop_logging and send_action. These retain their meanings; pause_comm concerns the connection and must not become Pause Display.

D01 owns proposed open_stream, close_stream, get_stream_capabilities, send_bytes and write receipts. Terminal additions remain proposed: get_terminal_state, set_terminal_mode, send_terminal_key, paste_terminal_text, resize_terminal and get_terminal_screen. They are not catalog entries yet. Mutations require the explicit endpoint/session generation and session-scoped authorization. Python uses Commander, just like CLI/native MCP. Screen pages remain bounded/revisioned.

## Dependency decision

Recommend an isolated, optional libvterm/native Qt model and renderer, not a home-written VT engine or another terminal widget owning a PTY. The [upstream](https://www.leonerd.org.uk/code/libvterm/) publishes C99 release0.3.3; its 80,024-byte official source archive was downloaded privately for inspection, SHA256 `09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0`. This identifies a candidate, not an adopted dependency. No vendored source, CMake target or feature flag is added here.

UT01 must verify source/license notices, build on MSVC and a Unix toolchain, review relevant upstream/downstream fixes, and prove the wrapper's limits before adoption. In particular, screen snapshots must bound the fixed cell character array even when every slot contains a combining character; [Vim's published snapshot advisory](https://github.com/vim/vim/security/advisories/GHSA-47gw-8gc3-mgcm) is a concrete consumer-side regression fixture, not a claim that the same bug is present in CILogg. Resize, fragmented UTF8/CSI, alternate screen, OSC52 refusal and long control strings remain named UT01 tests. Qt6-only donor widget interfaces cannot raise this project's Qt5 minimum.

## Platform and resource matrix

| Target | Baseline / intended owner | Evidence still required |
|---|---|---|
| Windows x64/ARM64 | Preserve current Qt6.12 CI choices; UT05 ConPTY | Native PTY lifecycle, partial writes, resize, password/IME, installer/runtime checks |
| macOS Intel/ARM64 | Preserve legacy-compatible Qt6.10.1 and modern Qt6.12 packages; UT06 Unix PTY/helper | Both maintained deployment profiles, helper descriptor/environment safety, native packaging |
| Linux | Preserve the current distro/backend/Qt matrix; UT06 Unix PTY/helper | Qt-only/accelerated builds, PTY descriptor lifecycle, package launch and input checks |
| Qt5/Qt6 core | C++17 model contracts and existing build scripts | Real compatibility fixtures; local Qt6 evidence does not prove Qt5 behavior |

Carry D01's proposed16 endpoints,8MiB RX and8MiB TX per endpoint,64KiB chunks and1MiB logical writes. Terminal caps remain independent:10000 scrollback rows AND16MiB,1000 columns by500 rows,64KiB control strings,16384 cells per public screen page. Reject before allocation; partial/limit outcomes remain visible. These are configured starting ceilings, not measured RSS/latency claims.

## Evidence and next increment

Source checkout is the existing linked worktree, branch codex/terminal-baseline-ut00. The entry source tree was clean. The installed authorized local SDK is Qt6.10.1; VS2022 build/test scripts remain the repository entry points. The existing build cache belongs to earlier work and is not a fresh build of this baseline.

PR #90 had37 successful checks before merge. PR91/92/93 CI remains independently monitored; their unfinished checks are not terminal evidence. No fresh product build, CTest, GUI, physical-device, PTY, SSH or package test is claimed for this documentation-only slice. G02/W0 performance, MD01–MD06 formal acceptance and UT02 integration gates remain open. Validate the complete plan package and canonical Git-blob hashes before publication; use whole-branch review to check the source map and scope.

Next implementation increment: UT01 emulator dependency/model proof behind the proposed feature-off boundary, followed by the minimal D01 write/lease/recording foundations required for UT02. Keep serial/local shell/SSH in the delivery scope. A model, dependency proof or serial-only demo is not completion of the terminal milestone.
