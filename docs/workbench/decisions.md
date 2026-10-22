# Workbench integration decisions

Accepted 2026-10-22 following the user's instruction to merge the existing
log-analysis roadmap with the supplied `START_IN_CODEX.md` execution package.

## D01 — One execution ledger

Use [the Workbench package](../../plans/cilogg-workbench-2026-10-21/README.md) and
its `PROGRESS.json`/`TASK_INDEX.json` for execution. Keep the prior P00-P24 roadmap
and P05 specification as linked feature history and regression contracts. Install
the package under `plans/cilogg-workbench-2026-10-21`, matching the plan location
already advertised by the repository README. Preserve the user's source package
under `doc/` in the original checkout; it is an import source, not a second ledger.

## D02 — Reuse completed foundations

Credit P00's existing baseline work, P01/P02, P03a-d, P04, merged P05 and P19b with
their actual evidence. P00's unmeasured gates remain open in G02/R02. Reuse existing
plugin, action, automatic-response, Python, scenario and lab foundations. Their
presence does not complete the broader A/D/E requirements.

## D03 — Early charts stay early

Execute G02/G03, then F01's Qt Core contracts, V01 and V02. Preserve native numeric
capture charts, physical-line axes, counts/relative-frequency buckets, min/max
envelopes, bounded data tables and validated source navigation. V02 must deliver
its GUI/CLI/MCP surface. Full structured parsing, typed time/query execution,
multi-panel live reuse and optional WebEngine/ECharts remain later capabilities.
No timestamp rate is inferred from line numbers or ingestion speed.

## D04 — Ownership and compatibility

`AnalysisJobService` retains scheduling, source leases, deadlines, backpressure,
retention, pagination and cancellation. F owns typed values, events, queries and
datasets; V owns panel bindings, rendering and interactions; U owns shared shell,
query builder, navigation and persistence; A adapts current actions/scenarios;
D owns binary transport/recording semantics; E owns connectors, SDKs and packs;
I owns investigation analysis; R owns release evidence. Shared Commander,
MainWindow, CMake, schemas and resources are integrated serially.

Use the exact existing signatures recorded in the baseline and the proposed F/V
contracts in the package. No rename or duplicate service is introduced here.
Preserve C++17, Qt5/Qt6 core support, stored IDs and plugin protocol compatibility.
All public features include menus/toolbars as appropriate plus discoverable
Commander/CLI/MCP configuration, state and operations. Python uses that same route.

## D05 — Actions and automatic responses remain coupled

Disabling Actions disables automatic responses. Enabling responses enables
Actions. Disabling responses leaves Actions unchanged; enabling Actions leaves
responses unchanged. New action-enabled tabs start with responses enabled, and
new tabs without Actions start with both disabled. Preserve explicit restored
response disables and the existing global response gate. A/D tasks must maintain
these semantics while extending protocol behavior.

## D06 — Platform work is independent

Keep PR #47 separate and based on its own CI evidence. Qt 6.12 is selected only
for supported/validated targets; retain legacy macOS compatibility, postpone
Ubuntu 26.04, and describe Rocky/Alma packages as RHEL-compatible. Local validation
uses the approved installed Qt 6.10.1 SDK. Core Qt compatibility is not raised by
the packaging matrix. No release workflow or merge is triggered by this plan.

## D07 — Evidence and rollback

G01 is documentation/source reconciliation. G02 supplies numerical benchmarks;
G03 supplies the reusable parity harness. Historical successes, source inspection,
new local runs, hosted checks, skipped checks and hardware tests remain distinct.
Task completion cannot be inferred from a plan checkbox. This integration can be
reverted without changing product code, settings, sources, captures or protocols.
