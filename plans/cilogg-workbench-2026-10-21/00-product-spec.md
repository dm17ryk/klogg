# CILogg Developer Workbench — Product and Architecture Specification

Date: 2026-10-21. Status: implementation target, prepared for Codex App execution. Repository: [dm17ryk/klogg](https://github.com/dm17ryk/klogg).

**Spec:** `00-product-spec.md` and `MASTER_GUIDE.md`; source evidence below supplements that specification.

## Product goal

Build a friendly, polished developer application for collecting logs and communication streams, exploring events, visualizing measurements and behavior, comparing executions, and automating debugging and testing. Serve backend, web, desktop, mobile, DevOps, QA/CI, data, game/performance, and embedded/protocol developers through the same core. CP/C8000 is one optional domain pack.

Keep the fast desktop workflow: a user can open a file and search immediately without creating a project, running a server, or defining a schema. Projects add reproducible sources, extraction profiles, charts, annotations, rules, and reports. Remote services are optional connectors.

## Design decisions

1. Extend the existing C++17/Qt5/Qt6 application, Commander commands, AnalysisJobService, plugin process protocol, StreamSession actions, ScriptSupervisor, ScenarioRunner, and Python runtime. Preserve current behavior and stored IDs. Introduce adapters around existing services, not replacement engines.
2. Use explicit layers: original bytes and immutable recording segments; decoded physical lines or transport chunks; assembled events and typed fields; query/aggregate datasets; visual and automation consumers. Display transformations never rewrite originals.
3. Preserve source provenance through parsing, multiline assembly, sorting, aggregation, comparison, and graphs. A plotted value is not a line number. Dataset buckets retain a bounded lineage handle that can rerun the contributing predicate against validated source snapshots.
4. Use a shared typed query/analysis contract. The GUI, CLI, Python, MCP, dashboards, headless CI, and rules agree on scope, time, units, counts, completeness, and errors.
5. Start linked regex charts with the native painter described in existing P06. Provide a richer optional Qt WebEngine renderer using locally bundled Apache ECharts. Rich charts receive bounded datasets; C++ performs scanning, extraction, aggregation, and correlation. Keep a data-table alternative for every visualization and retain the core build when WebEngine is unavailable.
6. Treat plain text, structured logs, binary communication, traces, and profiles as related inputs with explicit capabilities. Text logging is not a byte-exact recording format. Log events alone do not create true distributed spans or profiling stacks.
7. Keep protocol and developer-domain knowledge in versioned packs: connectors, parsers, field semantics, visual templates, test recipes, and documentation.
8. Automation uses existing action/scenario/process supervision and new shared rule/workflow contracts. Hardware effects occur only in configured active execution modes; analysis/replay simulation uses virtual transports. Opening a project restores definitions and visible status without silently restarting processes or sending bytes.

## User journeys and acceptance outcomes

### J1 — Open, understand, visualize

Open a large log, inspect a structured event or multiline exception, click a field, create an appropriate chart, select a bucket, and open its contributing raw events. The file remains usable while analysis runs. Sampled or limited results have visible labels.

### J2 — Capture and compare applications

Run an arbitrary developer program with literal argv and working directory; capture stdout/stderr with source metadata. Compare two runs by wall-clock time, elapsed start, or selected marker. Explain changed counts, latency distributions, exception patterns, and new/disappeared events with examples from both runs.

### J3 — Investigate several sources

Combine application files, container/service output, a remote query, and test events. Preserve separate source identity and clock domains. Apply shared filters/time windows and inspect causal relationships only when real correlation metadata supports them.

### J4 — Build a protocol test

Connect serial/TCP/UDP/HID/pipe sources; inspect byte-exact RX/TX, define parameterized sends, detect byte patterns across input chunks, validate framing/checksums, and execute assertions with deadlines. Record the session and reproduce it through virtual transports, including scripted replies and controlled error injection.

### J5 — Automate and share

Save an investigation, define a condition or test recipe through a visual editor, run it in the GUI and CI, and export JSON/JUnit plus a readable report. Share a portable bundle containing selected evidence, configuration versions, annotations, and completeness information; optionally redact the exported copy.

## Scope coverage

All feature families discussed in the comparison and product-direction conversation are assigned implementation tasks. This includes lower-priority specialized charts and Docklight-inspired communication features. Each task has dependencies and a release wave; later waves remain scheduled work rather than disappearing into an unowned backlog.

- Exploration: raw/clean/structured views; multiline and stack traces; facets; value counts; typed fields; folder search; chronological merge; timestamp navigation; search/tab/filter groups; retained searches; command palette; ANSI and value-name presentation.
- Analysis: formulas; grouping; pivots; rates; percentiles; windows; numeric/event-time histograms; comparisons; pattern reduction; rare/new patterns; exceptions; transactions/sessions; baseline anomalies; explicit forecast experiments; enrichment; query inspection and export.
- Visuals: line/step/points, areas/stacking/100%, bars/top-N/Pareto, pie/donut, stat/gauge/sparkline, histograms, both heatmap forms, scatter/bubble, box plots, tables/JSON/pivots, small multiples, state timelines/status history, event lanes/waterfalls, treemap/sunburst, Sankey/network graphs, funnels/parallel coordinates/radar, maps/layouts, flame graphs, custom charts, text/Markdown panels.
- Dashboards: field-to-chart suggestions; panel builder; common filters/variables; shared cursor/zoom/brush; exact drilldown; annotations; layouts/tabs; live refresh/pause; saved views; portable configurations; report exports.
- Automation: capture recipes; conditions, deadlines and missing events; workflow composition; assertions; Python hooks; record/replay; deterministic simulation; scheduling; CI; cancellation; execution journal and resumable analysis.
- Communications: serial capabilities/signals; binary recording/display; send/receive lists; wildcards/bitmasks; dynamic replies; checksums/framing/stuffing; periodic traffic; monitoring/bridging; TCP client/server; UDP; HID; named pipes; SPP serial; traffic/error simulation; import/export.
- Ecosystem: process/stdin, existing ADB/syslog, Docker/Kubernetes, SSH/journal sources, OpenTelemetry, backend query connectors, trace/profile imports, IDE links, plugin SDK capabilities, shareable packs, team configurations, optional AI assistance.
- Product quality: consistent design system, keyboard operation, accessibility, translations, high DPI, useful diagnostics, bounded resources, migration/rollback, packaged cross-platform validation, user documentation and example projects.

## Canonical domain model

F tasks define EventRecord, EventRef, typed FieldValue/FieldSchema, QuerySpec, DatasetRequest, DatasetSchema, AggregateDataset, DatasetPage, LineageHandle, and AnalysisDatasetService. Use existing SourceSnapshot/SourceLocation internally. V tasks consume these and define PanelSpec, DashboardSpec, and SelectionContext. D tasks define byte-oriented TransportEvent/FrameRecord with immutable recording offsets. A tasks adapt events, transport events, and existing action/scenario events to the same execution journal.

Public JSON values carry schemaVersion. IDs are opaque strings. Counts and integers outside JavaScript's exact integer range remain decimal strings or tagged values, not floating-point numbers. Timestamp precision, offset/unknown-zone state, source/observed time, uptime and reboot epochs remain explicit. Missing, null, empty string, zero, NaN/invalid numeric input, and absent observations remain distinguishable.

Every retained result includes source manifest, parser/query revision, source generations/watermarks, completion state, limitations, and units. Large results use paging. Historical results can be inspected after invalidation, but navigation into a changed source requires revalidation. Cross-source datasets validate every contributing source, not only the active tab.

## Exact global constraints

- Preserve C++17 and existing Qt5/Qt6 core compatibility. Optional dependencies have build flags and capability discovery. Read version-pinned Qt documentation for the SDK actually selected.
- Build only out of tree in build_root. Existing local build/test scripts are the primary Windows entry points. The actual CMake test option is KLOGG_BUILD_TESTS.
- Preserve default analysis admission: 2 running jobs, 8 queued jobs; raw blocks 256 lines / 1 MiB; pending output 8 batches / 4 MiB per job; sample 1,000 physical lines / 4 MiB decoded text.
- Preserve retained output: 10,000 records / 16 MiB per job and 64 MiB globally; row 4 MiB; page default 200, maximum 1,000 records / 8 MiB; 26 retained jobs; finished-job expiry 10 minutes. A task changing a limit must update the specification, public schema, stress fixtures, measurements, and UI explanation together.
- Preserve current physical-record rejection above 1 MiB. Multiline and binary paths add their own bounded segmented representations; they do not bypass raw-reader or worker limits silently.
- Default analysis deadline remains 60 seconds, maximum 10 minutes, including queue and backpressure waits. Capture and arbitrary regex execution retain enforced cancellation/deadline mechanisms.
- No whole-source scan, expensive regex, large sort, or report rendering on the GUI thread. No unbounded raw-event transfer into JavaScript, Python callbacks, MCP responses, or retained widget models.
- Freeze snapshots for a request. Appends signal newer data; explicit live modes handle provisional replacement/retraction. Reload/truncate/reinterpret invalidates derived caches and lineage together.
- Every public feature includes GUI discovery plus Commander/CLI and MCP access; Python uses the shared command route. Presentation features expose configuration/state and meaningful operations without requiring screen coordinates.
- Keep projects/settings/packs versioned with stable identities, validation, migration, atomic writes, and recoverable backups. Imported definitions are data until explicitly activated.
- Preserve Windows/macOS/Linux behavior and platform-specific capability reporting. Unsupported serial/HID/signal features produce clear unsupported results.
- No mandatory cloud account, AI subscription, or telemetry backend for core exploration, charts, and automation. Credentials use existing OS/tool credential facilities and are excluded from saved portable evidence.
- Source text, payloads, secrets, and raw expression values are excluded from routine debug diagnostics. User-requested recordings/exports remain explicit artifacts.

## Review focus and required owning tests

1. A growing/replaced file or split final event must not create duplicate counts or stale navigation — F01/F03/F11 and cross-source release tests.
2. JSON uint64 values, Unicode spans, quoted multiline CSV, DST ambiguity, and clock reset must not silently change meaning — F02/F04/F05/F06.
3. A dense dashboard, high-cardinality query, slow script, or blocked transport must remain bounded and cancellable — F09/F11, V04/V16, A02/A03, D03/D13; confirm exact task assignments during task indexing.
4. A replayed/late event or trigger-generated TX must not duplicate real effects or recurse indefinitely — A02/A05/A07 and D trigger/scheduler tasks.
5. A restored project or imported pack with missing sources, old schema, or unavailable capabilities must open readably without starting unavailable work — U03/U10, E01/E06, R03.

## Delivery and validation policy

Each task delivers a tested vertical slice with configuration, documentation, automation discovery, diagnostics, migration behavior, and rollback. Test instructions in task cards are requirements to execute, not claims of tests already passing. Planning here used source inspection; no Windows build, hardware test, or implementation validation is claimed.

Freeze measured performance budgets in G02 before release claims. At minimum measure open/index/search throughput, time to first useful result, peak resident memory, GUI event latency, cancellation latency, append-to-result delay, source-disabled overhead, optional renderer startup/memory, and long-run memory slope. Use the same recorded machine/config/corpus for comparisons. Proposed interactive targets inherited from the prior roadmap are p95 below 100 ms and local CPU-work cancellation acknowledgement within 1 second; G02 records achievable accepted release budgets and their limits before dependent features claim compliance.

All software features get deterministic fixtures and virtual transports; hardware-specific features additionally record adapter/driver/platform and supported observations. UI acceptance includes fresh settings, keyboard-only use, high DPI, both themes, clear partial/stale states, and an inspectable table alternative.

## Reference approach

The comparison tools are functionality and interaction references. Reuse practical components and schemas where they fit. Primary documentation and verified repository paths are linked in the task sections and reference catalog. Licensing is not a prioritization dimension for this plan.
