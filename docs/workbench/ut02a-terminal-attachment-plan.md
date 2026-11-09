# UT02a — Existing-owner terminal attachment preparation

Date: 2026-11-09. Baseline: merged master `88cb3aaf3f3af37b18fc280f1ccbe0019fdfeece`.

The user requested the next step after merging [PR #99](https://github.com/dm17ryk/klogg/pull/99). D01c's canonical nextPreparation explicitly calls for a separately scoped terminal-model attachment plan. The [UT02a implementation plan](../../plans/cilogg-workbench-2026-10-21/slices/UT02a-terminal-owner-attachment.md) supplies that next preparation. This PR changes planning and evidence only; no terminal consumer, input operation, dependency, build option or runtime behavior is implemented here.

## Reconciled evidence

- GitHub confirms PR99 merged as `88cb3aaf3f3af37b18fc280f1ccbe0019fdfeece`, from head `7e811f03cfc3feb150dd2a372292a03243337983`.
- Direct Git tree comparison confirms identical production source tree `db470dc650b52af32d11e98c5e4a257acca8f926` and test tree `7f43e0e49eb6e500d1d9c87c31cbe57703e50104` between that head and the merged baseline.
- The [final PR99 CI Build run](https://github.com/dm17ryk/klogg/actions/runs/42521981116) has 25 completed successful jobs and no failed, cancelled or unfinished jobs when observed on 2026-11-09. This is that build workflow's result, not a claim that every external review approved. Sourcery skipped the oversized diff. Windows ARM64 compile success is not native ARM64 test execution.
- D01c local ON/OFF builds, full CTest, public parity and reviewer evidence remain historical in its retained preparation descriptor and [D01c evidence](d01c-serial-owner-plan.md). This documentation-only change runs no new product build, CTest, device, GUI or performance qualification.

Exact canonical Git-blob SHA256 source pins and the formal-ledger audit are recorded in [plan observations](ut02a-plan-observations.json).

## Current source findings

| Source owner | Finding | Planned attachment decision |
| --- | --- | --- |
| [TerminalEmulator](../../src/terminal/terminalemulator.h) | Pure model already supports bounded feed, Yielded/consumedBytes, revisioned cell pages and generated replies | Reuse it, retain exact unconsumed suffixes and suppress replies in inspection. No replacement parser. |
| [StreamSession checked owner](../../src/ui/src/streamsessionwriter.cpp) | Existing worker epoch and checked readiness/close fencing; one serial owner | Attach presentation to this identity; never acquire a second endpoint or writer. |
| [SerialCaptureWorker](../../src/ui/src/serialcaptureworker.cpp) | onReadyRead reads raw bytes, formats legacy capture separately and emits dataReceived | Admit a bounded terminal RX tap at the I/O boundary before terminal-specific queued delivery; capture decoration and TX remain separate. Existing readAll/legacy queues are not thereby proven globally bounded. |
| [Session raw processing](../../src/ui/src/streamsession.cpp) | handleDataReceived exposes raw observation then performs line/Actions processing | Do not treat LF buffering or a public observer as the terminal's admission/ownership contract; fence observers and all continuations. |
| [MainWindow](../../src/ui/src/mainwindow.cpp) and [crawler](../../src/ui/include/crawlerwidget.h) | Existing target/session association and same-tab content | Use a Log/Terminal inspection presentation in that session; no unrelated terminal dock. |
| [Commander](../../src/ui/include/commander.h), [CLI](../../src/app/cli.h), [KloggApp](../../src/app/kloggapp.h) | Shared typed requests, conversion/help and application/window routing already serve automation | Add semantic presentation operations here and reuse their result envelope/selector behavior. |
| [Native MCP runtime](../../src/app/mcp/runtime.cpp) | commandCatalog derives discovery from shared command-mode help | Verify generated discovery after updating CLI help; avoid a second hand-maintained command catalog. |
| [CLI result output](../../src/app/main.cpp) and [MCP transport](../../src/app/mcp/runtime.cpp) | Structured failure payloads are currently retained specially for analysis operations | Extend the structured-error path for terminal actions so limits, permission failures and stale pages retain their reasons/state across CLI/MCP/Python; do not classify terminal operations as analysis. |
| [Terminal test registration](../../tests/terminal/CMakeLists.txt) | cilogg_terminal_tests deploys separately under build_root/terminal_tests/configuration | Use that actual runtime in the future RED/GREEN steps; do not test an installed older executable. |

## Scope and following work

Four dependent implementation tasks cover the pure controller, bounded existing-owner integration, same-window GUI/public parity, and qualification. Both existing feature flags retain OFF defaults. Read-only attachment is explicit and starts at a labelled RX cut, not at the beginning of historical capture. It supplies terminal coordinates and attachment-local coverage, not canonical EventRefs or physical log-line navigation.

The first public increment is inspection only. Raw RX can contain remotely echoed secrets; the display must not promise otherwise. The controller never transmits terminal-generated replies, Actions, sensitive TX or annotations. Later typing must use the shared writer with explicit arming and immediate automation exclusion. Serial inspection alone does not complete the required serial/local-shell/SSH terminal.

Automated presentation mutations require an explicit runtime-only per-session grant, default denied and revoked on detach/close/reconnect. The planned permission operation and visible control share GUI/CLI/native-MCP/Python semantics; they never grant input authority. The plan also includes the currently analysis-only CLI/MCP structured-failure transport paths. These changes address both Important findings from fresh C++ review.

Formal task states, the 119-task dependency graph, the 201 requirements, W0/decoder gates and existing acceptance limitations stay unchanged. UT02's canonical raw recorder and mapped derived transcript remain open. The prepared plan is the next independent slice, not a waiver of those dependencies.

## Verification boundary

Before publication, validate the complete planning package, exact staged-blob hashes, preserved formal task/DAG/requirement objects and unchanged product trees; obtain fresh whole-scope C++/general review of the plan against current source. Record current index availability separately. No function/class is edited by this planning-only change, so symbol impact is a required implementation preflight rather than a claimed runtime blast-radius result.

The Code Review Graph was refreshed on the merged baseline and reports no source changes from PR99. CodeGraph remains unindexed and is not initialized implicitly. GitNexus staged change detection completed with four documentation symbols, zero affected processes and LOW graph risk. Its FTS extension is unavailable; this is not a claim of a fresh full source-index rebuild. Direct Git tree/diff checks and formal-ledger comparisons provide the documentation boundary.

Planning validation passed: 119 tasks, 201 requirements, 150 Markdown files and 574 local package links. All 119 formal task records and prior preparation evidence are preserved; 16 local evidence-document links resolve. Fresh C++ and general whole-scope reviews approve the amended plan with no remaining Important/Critical findings. Package hashes are verified against exact staged Git blobs before commit.
