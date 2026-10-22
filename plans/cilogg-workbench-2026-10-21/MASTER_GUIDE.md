# CILogg Developer Workbench Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver every feature discussed for a polished developer logger, visualizer and automation workbench, including Docklight-style communication testing and the earlier fork roadmap.

**Architecture:** Extend the current CILogg Qt application and its shared bounded analysis/source/action/scenario/plugin services. Keep original evidence separate from parsing, typed datasets, rendering and automation. Deliver small vertical slices with stable schemas, source lineage, resource bounds and GUI/CLI/Python/MCP parity.

**Tech Stack:** C++17; existing Qt5/Qt6/CMake/Catch2/QtTest; Python plugin/scenario SDK; optional Qt WebEngine and locally bundled Apache ECharts; optional connector/import dependencies isolated from the core.

**Spec:** `00-product-spec.md`. All subsystem cards implement that specification; reconcile exact source interfaces in G01 before feature edits.

## Global Constraints

The exact values in the specification apply to every task: existing source generations/watermarks, 2 running/8 queued analysis jobs, capped batches/results/pages, C++17/Qt5/Qt6, out-of-tree builds, compatible project/config IDs, explicit unsupported capabilities, and public command parity. This plan does not reset branches, replace working subsystems, require an AI provider, or claim tests have already run.

## Review Focus

| Risk | Owning task/tests to review |
|---|---|
| Source replacement, appending a provisional final line, and stale aggregate navigation | F01/F03/F11 provenance/retraction fixtures; V14 source validation; R01/R02 combined runs |
| Unicode/large integers/multiline CSV/unknown time zone/clock reset changes meaning | F02–F06 parsing/time fixtures; U04 raw copy; V03 precision bridge; E06 timestamp mapping |
| Dense/high-cardinality live workloads, slow consumers and unavailable renderer/transport | F09/F11 bounds; V03/V06/V17; A02/A03; D transport/capture bounds; R02 stress |
| Replayed or late events trigger duplicate effects or recursive TX/RX rules | A02 rule feedback, A03 cleanup, A05 seek/repeatability, A08 acknowledgements; D04/D07/D15–D16 as applicable |
| Old/missing project source, schema, plugin or hardware causes unintended startup | U03/U11 restore fixtures; A01 migration; E01/E09/E10; D capabilities/import; R03 packaging/migration |

## What to open in Codex App

1. Open the actual local `dm17ryk/klogg` repository in Codex App. On the recorded Windows setup this has been `D:\Essence_SC\lsrc\klogg`; use the user's actual checkout, not an assumed path.
2. Use the installed package at `plans/cilogg-workbench-2026-10-21/`. The supplied `doc/CILogg_Codex_Implementation_Plan` copy was the import source; do not create another active ledger. Keep newer planning changes and reconcile them without overwriting unrelated work.
3. Start with `START_IN_CODEX.md`, this guide, `00-product-spec.md`, `BASELINE.md` and `PROGRESS.json`.
4. Read the recorded G01 reconciliation, then complete G02 and G03 before public feature work. For each next task read its owning subsystem section, shared contract definitions it consumes, and current source/tests. Read only the relevant sections instead of repeatedly loading the entire combined plan.

Execution reconciliation dated 2026-10-22 is recorded in [MERGE_MAP.md](MERGE_MAP.md) and [the current baseline](../../docs/workbench/baseline.md). G01 is complete as a documentation/source task; G02 and G03 remain planned. P05 is merged through PR #45 and must not be reimplemented. The following research pins remain historical evidence.

The package is a complete program plan. Implementing the whole program is a sequence of reviewed increments, not one giant patch or one fragile conversation. Each task may be split into smaller PR slices if their interfaces and tests remain explicit; record the split without dropping any requirement.

## Source baseline and evidence

The repository default branch inspected on 2026-10-21 was master `bd74c338424845a2caac03d62fdff5187e7e8f0a` (Regex Lab P04). Value Counts P05 was implemented on `codex/value-counts-p05`, SHA `01cb637be7c96f2c9e3925247510c162380736b7`. Existing P00–P04 services and P19b selection work are described in the prior roadmap; P06–P24 remained planned there. G01 must refresh this status at execution time.

Existing communication/action/Python/plugin features also predate this plan. The D and A cards explicitly audit and reuse them. Planned filenames and methods in this package are targets, not claims that the current repository already contains those APIs.

No product build or physical hardware validation was performed while writing this plan. The baseline source links and task instructions are source-level evidence. Future Codex execution records actual local/CI/hardware results.

## Release waves and dependency policy

The machine-readable task index lists exact hard dependencies. A later task that needs only a capability subset must state that subset; optional capabilities must not create a circular prerequisite. V02 can ship initial numeric-versus-line charts using existing extraction before full structured/time work.

| Wave | User-visible goal | Task families / exit gate |
|---|---|---|
| W0 | Reconcile and measure the current product | G01–G03; baseline, task ledger and runnable parity harness |
| W1 | Better everyday exploration and first linked charts | F01 common contracts, V01/V02, U01/U02/U09/U10, existing P05 integration; native first-chart/source navigation |
| W2 | Structured events, source workflows and byte integrity | F02–F08, U03/U04, E01/E02, D01–D03 foundation and available transport primitives; multiline/time/raw recording parity |
| W3 | Useful analytical dashboards | F09–F11/F14, V03–V08/V14–V18, U05–U08/U11/U12; linked source exploration and saved dashboard |
| W4 | Reproducible investigations and automation | I01–I05, A01–A08, applicable D sequence/trigger/framing/replay cards, E10/E11, U13; compare two runs and automate a tested check |
| W5 | Complete communication and specialist visualization | Remaining D cards, V09–V13, F12/F13/F15 and E08; transport capability tests, graph/state/profile/geo datasets and graceful fallbacks |
| W6 | Wider developer ecosystem and full-product release | E03–E09/E12, all remaining cards, R01–R04; domain packs, integrations, stress, migrations, packages, docs and complete coverage |

Waves communicate release goals; they do not override the task DAG. If a card depends on a task mentioned in a later wave, move that prerequisite earlier or split its explicitly independent capability subset in the ledger. Never implement against a missing contract to preserve an arbitrary calendar order.

Useful independent tracks after their contracts land: data, native/rich renderer, communication adapters, workspace, insights, automation and connector packs. MainWindow/Commander/CMake/resource registration, shared schemas and source lifetime are shared files: serialize their integration and assign one owner. Parallel work uses isolated branches/worktrees and frozen interfaces; do not run competing source edits in the same checkout.

## Standard implementation cycle

- [ ] Read current instructions, task, spec and directly consumed interface cards. Inspect current source and call graph; state meaningful HIGH/CRITICAL impact results before edits.
- [ ] Choose one independently reviewable task or recorded slice and mark it `in_progress`. Preserve unrelated changes and the user's active captures.
- [ ] Write meaningful failing behavior/regression tests with named assertions from the card; run them and record the failure. Documentation-only tasks use document/schema/link validation rather than artificial unit tests.
- [ ] Implement the smallest complete vertical slice, including error/completeness states, limits, diagnostics, migration, GUI discovery, public command schemas and documentation. No duplicate scheduler/query/parser/action catalog.
- [ ] Run focused tests, then relevant complete CTest and packaged parity/UI flows. Tests changing after a failure run again; avoid repeating passing full suites without a new reason.
- [ ] Review C++/Qt ownership, concurrency, integer/span/time semantics, cancellation and persisted schemas. Review sibling occurrences of a discovered defect. Update code indexes and required change-scope checks.
- [ ] Update `PROGRESS.json`: actual commit, tests, artifact paths, platform/hardware evidence and next runnable task. Keep blocked/pending/skipped distinct from complete.
- [ ] Commit a focused change and prepare the normal reviewable PR when authorized by the execution request. Include behavior, validation, screenshots for UI changes and rollback. Do not merge/publish merely because the plan exists.

Respect existing user authorization during actual execution: continue routine implementation and reversible validation without repeatedly asking the same question. A missing prerequisite is handled concretely; a real external/destructive decision is surfaced with its exact reason.

## Verified Windows commands and new harness commands

Use a clean PowerShell session (`pwsh -NoProfile`) and the installed VS2022/Qt environment. Read current AGENTS and scripts first. The recorded AGENTS names a Qt 6.12 path; prior local evidence used Qt 6.10.1, so do not blindly paste either SDK path. The build script accepts `CILOGG_QT_DIR` or `QTDIR` and discovers VS2022.

```powershell
Get-Location
git status --short
git rev-parse HEAD
Get-Item Env:CILOGG_QT_DIR, Env:QTDIR -ErrorAction SilentlyContinue
& .\scripts\codex\build-windows.ps1 -Config RelWithDebInfo
& .\scripts\codex\run-tests.ps1 -Config RelWithDebInfo
```

Focused test command shape (replace the tag with the current task's exact tag):

```powershell
& .\build_root\output\RelWithDebInfo\cilogg_tests.exe "[workbench][A01]" -platform offscreen
& .\build_root\output\RelWithDebInfo\cilogg_itests.exe "[ui][workbench][U01]" -platform offscreen
ctest --test-dir build_root --build-config RelWithDebInfo --output-on-failure
```

New tests in the integration harness must include `[ui]` or `[logdata]` as appropriate, because Windows CTest partitions those tags. All new files are registered in their test CMakeLists. An empty selection, unbuilt test executable or unrun CI job is not passing evidence.

After G03 has implemented the new harness:

```powershell
python tests\mcp\workbench_parity.py --executable build_root\output\RelWithDebInfo\cilogg_portable.exe --task V02 --artifacts build_root\workbench-parity\V02
```

Existing analysis parity command (actual available switches depend on refreshed branch state):

```powershell
python tests\mcp\analysis_parity.py --executable build_root\output\RelWithDebInfo\cilogg_portable.exe --artifacts build_root\analysis-parity --auto-responses --regex-lab --value-counts
```

Linux/macOS use the actual configured output paths and the existing CI toolchain; do not present Windows tests as cross-platform validation. Build both accelerated and Qt-only matching variants where semantics differ. Optional rich rendering needs real renderer/package smoke in addition to offscreen data tests. Protocol tests use virtual transports first, then separately recorded real hardware capability tests. Never stop an existing CILogg capture merely to free its COM port; respect its pause/resume and ownership controls.

## Compatibility with the earlier P00–P24 roadmap

| Earlier step | Coverage / disposition in this plan |
|---|---|
| P00–P02, P03, P04 | Reuse completed source/performance/job/extraction/Regex Lab work; G01–G03 reconcile evidence and remaining performance gates |
| P05 Value Counts | Merged PR #45; reuse published bounded count semantics; V05/F09 consume it |
| P06 linked regex charts | V01/V02 early native slice; V03–V18 expand without blocking the first usable chart |
| P07–P08 folder search | F16 recursive manifest/search/lazy readers and grouped navigation; F06 owns separate chronological merge semantics |
| P09–P11 formats/time/navigation | F01–F06, typed records/fields/time domains and navigation |
| P12–P13 tables/query/sort/CSV | F07 table, F08 field queries and bounded typed sorting, F15 CSV; V08/U04/U05 adapters |
| P14 Value Names | F12 enrichment/presentation with raw-span provenance; U04/U12 integration |
| P15 retained searches | U06 |
| P16 filter groups | U07 |
| P17 tab groups | U08 |
| P18 ANSI | U12 |
| P19a QuickFind / P19b selection | U09; reuse completed discontiguous selection and regression-test its interactions |
| P20 command palette | U10 |
| P21 process/stdin capture | E02 |
| P22 zstd/lz4 | F13 |
| P23 index cache | F13; F10 owns dataset lineage rather than caches |
| P24 Git Team Folder | E09 |

The dated [merged coverage map](MERGE_MAP.md) records implementation status for every P00-P24 package/slice, including completed P19b and pending P19a. Preserve that coverage when ownership changes; proposed filenames in the older roadmap are not a second implementation plan.

## Full-program completion rule

A task is complete when its behavior, limits, tests, GUI/CLI/MCP/Python surface, documentation and rollback are delivered. A release is complete when the relevant R gates pass. The full program is complete when every coverage row has delivered evidence or an explicit user-approved scope change. Hardware-dependent claims state the exact supported backend/adapter; arbitrary platform or hardware precision is never inferred from a software timestamp field.

## How to resume

Use `PROGRESS.json` and the DAG to choose the next task whose hard dependencies are complete. Read its subsystem file and required contracts, inspect current HEAD, and continue from the last recorded evidence. Do not replay effectful automation as part of resuming development. If a task is blocked, record the exact blocker and continue another independent runnable task.
