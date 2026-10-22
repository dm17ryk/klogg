# CILogg visual analysis and dashboards implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give backend, web, desktop, mobile, DevOps, QA, data, game and embedded developers a common path from selected log records to trustworthy charts, linked investigations and reproducible dashboards.

**Architecture:** Keep extraction, parsing, query evaluation, aggregation, sampling and provenance in the shared bounded C++ analysis service. Add a native Qt painter renderer first; optionally render richer panels using bundled Apache ECharts inside Qt WebEngine. Both renderers consume the same versioned datasets and invoke the same typed selection and automation operations.

**Tech stack:** C++17, existing Qt5/Qt6 Widgets and analysis worker infrastructure; optional Qt WebEngine Widgets/WebChannel and a pinned bundled ECharts build. No Qt Charts requirement, browser query engine, mandatory server, CDN or remote log upload.

**Spec and evidence:** `plans/log-analysis-roadmap.md` P03–P06 and P09–P13, `docs/analysis-source-identity.md`, `docs/analysis-scan-jobs.md`, `docs/value-counts.md`, supplied dashboard/chart requirements and the pinned repository tree documented in `BASELINE.md`. Baseline repository: `dm17ryk/klogg`, master `bd74c338424845a2caac03d62fdff5187e7e8f0a`; P05 branch snapshot `01cb637be7c96f2c9e3925247510c162380736b7`. Public documentation checked 2026-10-21.

**Spec:** `00-product-spec.md` and `MASTER_GUIDE.md`; source evidence below supplements that specification.

## Scope, baseline and integration ownership

P03 source identity, bounded jobs, capture extraction, shared public analysis commands and P04 Regex Lab are merged. P05 Value Counts is merged through PR #45; reuse its existing count-result adapter and published semantics. The current execution baseline is master `177f73861b3bc58ddf5c8b15021471a814673a1d`; see [the reconciliation](../../docs/workbench/baseline.md). P06 charts and P09–P13 structured fields, time and table/query work remain planned. A roadmap filename or a name proposed below is not evidence that its API exists.

The original research used a selected documentation/CMake snapshot; G01 has now inspected the actual checkout and relevant current headers. Paths marked **verified** exist in the execution baseline. Exact source symbols and insertion points must still be inspected with impact analysis before implementation edits. No compilation, runtime measurement or product-code modification is claimed by this reconciliation.

External task dependencies are named for integration into the parent plan:

- **F01** owns canonical `EventRecord`, `EventRef`, `FieldSchema`, field values, `DatasetSchema`, `DatasetRow`, `AggregateDataset`, `LineageHandle` and dataset serialization. Its proposed canonical headers are `src/logdata/include/fieldvalue.h`, `src/logdata/include/eventrecord.h`, `src/logdata/include/datasetschema.h` and `src/logdata/include/queryspec.h`. This is a minimal Qt Core + current `SourceLocation` contract inside `klogg_logdata`; it compiles without parsers, logformat, structured tables, typed-query execution or a new analysis target. V01/V02 consume only this contract subset, not later F services.
- **F07** owns the canonical virtual structured-event table/field explorer and the later `AnalysisDatasetService` facade; V08 adds aggregate adapters rather than another table engine. F07 does not block V01/V02: initial capture result inspection uses the bounded P06 series model/table, which V08 later adapts to the canonical virtual table.
- **F09** owns exact bounded aggregates, quantile policy, numeric/event-time/2D buckets, windows, pivot, Top N, Other and Pareto calculations.
- **F11** extends the F07 shared dataset facade with query planning, compatible-scan reuse/live delivery through the existing `AnalysisJobService`. The later proposed `AnalysisDatasetService` lives in `src/analysis/include/analysisdatasetservice.h` and `src/analysis/src/analysisdatasetservice.cpp`; the new `klogg_analysis` target links `klogg_logformat` and `klogg_logdata`. Only later structured-query/advanced UI slices depend on `klogg_analysis`; V01/V02 and their capture command adapter remain buildable without that target. Neither logdata nor logformat depends on UI/visualization, and logdata must not depend back on logformat. Existing `src/logdata/src/analysiscommands.cpp` is an integration pattern to inspect, not permission to include `AnalysisDatasetService` there; service-dependent dashboard command handlers live in `klogg_analysis` and are wired by the higher-level shared Commander dispatcher.
- **F14** owns `DatasetTransformRegistry`: V09–V13 provide their typed transform implementations there, with one worker/query engine and common limits, not a second analysis subsystem.
- **GUI shell tasks** own the application workspace, shared navigation/history, action catalog and persistence integration. **U03** supplies the shared source/scope/query-context adapters; **U05** owns the field/aggregation/query builder. V15 supplies chart suggestions, gallery, preview and presentation settings around those adapters; it never creates a second field/query builder. V tasks provide dashboard-specific widgets/specifications and use shell services.
- **Comparison service task** owns run identity, matching/cohort selection, baseline/current joins and delta datasets; V17 renders its supplied datasets.
- **E08 import tasks** own validated Speedscope, ChromeTrace, OTLP and folded input adapters and their provenance. V13 consumes profile-shaped datasets after these adapters land; V10 consumes trace/span-shaped datasets. An imported trace is not automatically a statistical profile.
- **Automation tasks** own shared command-envelope/schema integration; each V card supplies its actions, state and parity fixtures before exposing a feature.

Verified integration anchors: `src/logdata/include/analysisjob.h`, `src/logdata/src/analysisjob.cpp`, `src/logdata/include/analysiscommands.h`, `src/logdata/src/analysiscommands.cpp`, `src/logdata/include/analysisregex.h`, `src/logdata/src/analysisregex.cpp`, `src/logdata/include/sourcelocation.h`, `src/logdata/src/sourcelocation.cpp`, `src/regex/src/analysisworker.cpp`; `src/ui/include/commander.h`, `src/ui/src/commander.cpp`, `src/ui/include/mainwindow.h`, `src/ui/src/mainwindow.cpp`, `src/ui/src/mainwindowregexlab.cpp`, `src/ui/include/regexlabdock.h`, `src/ui/src/regexlabdock.cpp`, `src/ui/include/crawlerwidget.h`, `src/ui/src/crawlerwidget.cpp`, `src/ui/include/tabbedcrawlerwidget.h`, `src/ui/src/tabbedcrawlerwidget.cpp`; `src/app/mcp/runtime.cpp`, `src/app/mcp/protocol.cpp`, `src/app/klogg.qrc`; top-level `CMakeLists.txt`, `src/logdata/CMakeLists.txt`, `src/ui/CMakeLists.txt`, `src/app/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/ui/CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `tests/mcp/analysis_parity.py`.

All chart-specific C++/web/schema/test paths in the cards are **proposed new paths** unless explicitly marked verified. After earlier cards create a file, later cards modify that proposed file. Keep chart adapters in focused files; do not grow `mainwindow.cpp` into a chart engine.

## Non-negotiable data and display contracts

1. A renderer receives an `AggregateDataset` descriptor and bounded `DatasetPage` payloads, never a `LogData*`, file path reader, regex executor or unbounded event array. It may format values and arrange marks. It must not recalculate counts, percentiles, histograms, joins, rates or source predicates in JavaScript.
2. Use the same F01 dataset/page/lineage contract through two explicitly staged adapters. **Early P06 route (V02):** keep submission, paging, cancellation, release and source-location navigation on the existing `AnalysisJobService` / regex processor / P05 paths. The data agent's proposed pure adapter in `src/logdata/include/capturedatasetadapter.h` and `src/logdata/src/capturedatasetadapter.cpp` declares:

   ```cpp
   DataResult<DatasetDescriptor> describeCaptureDataset(
       const QVariantMap& jobStatus, const CaptureDatasetSpec& spec);
   DataResult<DatasetPage> decodeCaptureDatasetPage(
       const QVariantMap& page, const CaptureDatasetSpec& spec);
   ```

   `DataResult` is the shared F01 result/error type. Declare proposed `CaptureDatasetSpec` in that header with selected subpattern/capture selectors, exact numeric conversion and unit policy, physical-line x role, source manifest and request revision. This adapter converts bounded existing job payloads; it neither submits, scans, schedules nor parses a structured query. V02 calls the existing service's verified-at-execution `start` / regex-processor / pager methods directly, never a placeholder F07/F11 function. It retains the service's original job IDs and location-validation path.

   **Later typed route (F07, then F11):** reuse these agreed proposed signatures; the same V01 `RenderFrame`/`PanelSpec` renderer boundary does not change:

   ```cpp
   QString AnalysisDatasetService::submit(QuerySpec query,
       DatasetRequest request, QString* error = nullptr);
   DatasetPage AnalysisDatasetService::page(const QString& jobId,
       const QString& cursor = {}, int pageSize = 200);
   DatasetDescriptor AnalysisDatasetService::describe(const QString& jobId) const;
   QString AnalysisDatasetService::drilldown(const QString& jobId,
       const LineageHandle& lineage, const DrilldownRequest& request,
       QString* error = nullptr);
   ```

   Returned IDs use the existing analysis-job lifecycle. `QuerySpec`, `DatasetRequest`, `DatasetPage`, `DatasetDescriptor` and `DrilldownRequest` are defined in the F tasks, not invented as existing baseline APIs. `DatasetRequest` freezes sources/scope, `AnalysisLimits`, live mode, schema version and request revision. `DatasetPage` carries job ID, dataset/request revisions, bounded rows, opaque next cursor, completion/reason and source manifest/historical/newer-data state. A schema is available from `describe()` and the first page. F07 adds this facade after structured-field work; F11 adds reuse/live query planning. Neither is a prerequisite for native capture charts. Migrate the submission/description adapter once, preserving job/source/revision semantics and bridge payloads; do not keep a second regex/query engine.
3. F01 defines `EventRef` using an event key and a discriminated `EventOrigin`: physical, inline, deferred-file, or managed-bytes. Only physical origins carry real first/last `SourceLocation` anchors. Deferred files materialize/rebind asynchronously through F07/F10/F16; managed byte-only events resolve through D01 recording/segment provenance without fabricated lines or UTF-16 spans. Format/assembly revisions remain explicit. `DatasetRow` carries stable `rowId`, typed `FieldValue` cells and opaque `LineageHandle`. `DatasetSchema` columns declare field/type/unit/role and clock domain. `AggregateDataset` is retained metadata, not all rows in a vector. `LineageHandle` resolves job + dataset revision + row identity to a normalized query predicate and frozen source scope; it never stores every constituent event in a chart point. In the early capture route, that retained predicate is the existing capture definition plus physical-line range/occurrence identity, interpreted by the existing analysis processor/P05 action; no field-query parser is needed for point or line-bucket navigation.
4. Physical lines are zero-based internally and one-based publicly; source spans use zero-based UTF-16 units before display transformations. Append changes revision, not generation; provisional final-record contributions must be replaced. Replacement, truncation, close, incompatible decoding/format changes invalidate navigation. An aggregate opens a matching child result/range, not an invented representative source line.
5. Axis domain is explicit: physical line, ordinal sample, numeric measurement, offset-aware event time, device uptime or another declared clock domain. Line-bucket counts cannot be labeled messages/second. Event-time rate denominators and half-open bucket edges are part of dataset metadata. Incompatible clock domains cannot share a synchronized cursor or subtraction without an explicit alignment transform.
6. Missing, null, empty string, invalid numeric and zero remain distinct. Reject NaN/infinity from plotting while retaining invalid counters and source inspectability. Preserve integer/count precision with tagged decimal strings in bridge/JSON payloads; do not coerce 64-bit IDs or nanosecond timestamps through JavaScript doubles.
7. Retain existing service caps: two running jobs plus eight admitted queued jobs; 10,000 retained records/16 MiB per job and 64 MiB globally; pages default 200, maximum 1,000/8 MiB; 26 retained jobs and ten-minute finished expiry. Do not increase them just to fill a chart. A larger scan can still aggregate all accepted input into a bounded output; completeness describes input scan coverage, not the number of pixels.
8. Proposed extra display defaults to measure and freeze with P00: maximum 16 panels/dashboard, 16 series/panel, 2,048 buckets/axis, 8,192 rendered primitives/panel, 2 MiB serialized bridge frame and 32 MiB total native/web dataset staging buffers per process. Aggregate-cell and existing retained-row caps apply first. These are initial choices, not measured performance claims. Report cap/partial/evicted/expired state visibly and in automation; never silently trim a chart into a complete-looking result.
9. Keep query output independent from view reduction. A min/max envelope or explicit representative sample is a rendering dataset with reduction metadata and exact underlying inspect/export lineage. A screen export records that reduction. Query-result export identifies its own full/sampled/partial scope.
10. Every panel has a keyboard-accessible data table and an inspect action. Native fallback includes core painter plots and the canonical Qt table; advanced rich-only drawings report `native_table_fallback` and display their exact data/lineage with a clear capability message. Never silently replace a Sankey or flame graph with an unrelated chart.

## Proposed visualization types owned by V01

Create declarations in proposed `src/logdata/include/dashboardspec.h`, independent of QWidget. These names are design decisions to implement, not existing APIs:

```cpp
struct PanelSpec {
    quint32 schemaVersion = 1;
    QString panelId, title, kind;
    QString dataProvider = "capture";
    QJsonObject captureDefinition;
    QuerySpec query;
    QJsonObject fieldRoles, presentation, interaction, extensions;
};
struct DashboardSpec {
    quint32 schemaVersion = 1;
    QString dashboardId, title;
    QVector<PanelSpec> panels;
    QJsonObject layout, variables, refresh, annotations, extensions;
};
struct SelectionContext {
    QString selectionId, originPanelId, datasetJobId, action;
    quint64 datasetRevision = 0, requestRevision = 0, eventSequence = 0;
    std::optional<LineageHandle> lineage;
    QJsonObject bounds, variableValues;
};
struct RendererCapabilities {
    QString rendererId;
    QStringList nativeKinds, richKinds, fallbackKinds, exportFormats;
    QJsonObject limits;
};
struct ChartBinding {
    QString kind;
    QHash<QString, QString> roleToColumnId;
    QJsonObject validatedOptions;
};
struct RenderFrame {
    QString panelId, datasetJobId;
    quint64 datasetRevision = 0, requestRevision = 0;
    DatasetSchema schema;
    DatasetPage page;
    ChartBinding binding;
};
struct ChartSuggestion {
    QString kind, title, reason;
    ChartBinding binding;
    bool requiresMoreFields = false;
};
struct PanelExportRequest {
    QString format, absolutePath, valueMode;
    QSize pixelSize;
    bool allowPartial = false;
    bool spreadsheetSafe = false;
};
```

`dataProvider` is `capture` for the existing Regex Lab/P03 capture request schema or `typed_query` after F07. Exactly one corresponding definition is active: `captureDefinition` preserves the existing capture-request object and is not a new grammar; `query` uses the pure F01 declaration and does not require a query evaluator to construct/serialize a capture panel. Both adapt into identical F01 dataset descriptors/pages.

`fieldRoles` maps stable column/field IDs to roles such as x/y/category/series/weight/start/end/entity/parent/lat/lon; it does not identify fields by translated label. `kind` is a registered token listed in the coverage table. Registry validation supplies a versioned JSON schema for each kind. `bounds` has a versioned discriminant (`x_range`, `xy_rect`, `category_set`, `edge`, `map_region`, `stack_path`) and tagged typed endpoints; it is not executable query text. The shared query service binds values to typed predicates. `eventSequence` plus origin/revisions prevents stale gestures and feedback loops.

Any helper class introduced in a later card is declared in that card's proposed header; types not listed here come from Qt, C++17 or the named F contracts. Use `bool` + `QString* error` for synchronous validation and `QString` job IDs for asynchronous work; command results carry structured error codes, not only an error string.

## Chart coverage and release path

`N` = native Qt painter/table drawing; `R` = optional rich renderer; `T` = exact native table fallback for a rich drawing. All use identical datasets and drilldown predicates. Every required chart has an owning card; V14–V18 add shared behavior to every supported kind.

| Panel family / kind tokens | Required fields or semantics | Owner | Rendering / prerequisite |
|---|---|---|---|
| Line, step, point/trend (`line`, `step`, `points`) | Numeric capture vs physical line first; event time only after typed time | V02, V04 | N/R; P06 first |
| Min/max envelope and percentile bands (`band`) | Lower/upper with exact or declared approximate aggregate policy | V02, V04 | N/R; native capture envelope in V02; typed percentile bands after F09 |
| Area, stacked area, 100% area (`area`, `stacked_area`, `percent_area`) | Aligned bins and explicit denominator | V04 | N/R; F09 + typed time for time axes |
| Grouped, stacked and 100% bars (`bar`, `stacked_bar`, `percent_bar`) | Categories, series, measure; signed policy | V05 | N/R; P05 adapter/F09 |
| Top N, Other and Pareto (`top_n`, `pareto`) | Exact totals, deterministic ties, cumulative denominator | V05 | N/R; F09 |
| Pie/donut (`pie`, `donut`) | Small nonnegative part-to-whole categories | V05 | N/R; F09 |
| Stat, gauge/bar gauge and sparkline (`stat`, `gauge`, `bar_gauge`, `sparkline`) | Reducer, unit, thresholds, range and trend domain | V05 | N/R; F09 |
| Numeric histogram (`histogram`) | Numeric value ranges/counts, not event-time counts | V06 | N/R; F09 |
| Event-time count/rate buckets (`event_bars`) | Event timestamps, explicit duration denominator | V04 | N/R; P10/F09 |
| Time × value distribution heatmap (`distribution_heatmap`) | Event-time bins × numeric bins → counts | V06 | N/R; typed time/F09 |
| Category/time or category/category matrix heatmap (`matrix_heatmap`) | Two categorical/time axes → aggregate | V06 | N/R; F09 |
| Scatter and bubble (`scatter`, `bubble`) | Numeric x/y; nonnegative size; optional series | V07 | N/R; F09 |
| Boxplot (`boxplot`) | q1/median/q3, whiskers, outlier policy and sample count | V07 | N/R; F09 |
| Virtual table, pivot and JSON/raw inspector (`table`, `pivot`, `json`) | Typed values, stable identity and bounded cells | V08 | N; optional rich table shares adapter; F07/F09 |
| Treemap and sunburst (`treemap`, `sunburst`) | Explicit hierarchy paths, nonnegative weights | V09 | R/T; F14 |
| Sankey and node graph (`sankey`, `node_graph`) | Explicit edge/source/target/weight; different graph constraints | V09 | R/T; F14 |
| State timeline and status history (`state_timeline`, `status_history`) | Entity + typed time + state; merge intervals vs separate observations | V10 | N/R; typed time/F14 |
| Event lanes and request/span waterfall (`event_lanes`, `waterfall`) | Event points; explicit span start/end, correlation and parent IDs | V10 | N/R; typed time/F14 |
| Funnel, parallel coordinates and radar (`funnel`, `parallel`, `radar`) | Cohort stages; numeric dimension rows; comparable scaled metrics | V11 | R/T; F14 |
| Geographic maps and equipment/floor layouts (`map`, `equipment_layout`) | Explicit coordinate or region/element mapping | V12 | R/T; native equipment layer optional; F14 |
| Flame graph/icicle + profile top table (`flamegraph`, `icicle`) | Real stack-profile data, units and sample weights | V13 | R/T; E08 profile adapters/F14 |
| Small multiples and comparison overlays (`small_multiples`, `comparison`) | Facets or supplied aligned baseline/current datasets | V17 | N/R using child panel capabilities; comparison service |
| Custom chart (`custom`) | Bounded declared dataset roles and allowed render primitives/options | V16 | R/T; no second query language |
| Markdown and text (`markdown`, `text`) | Human notes and declared scalar bindings | V16 | N/R; no script required |

The minimal shipping DAG is **merged P03/P04 + F01 Core-only contracts → V01 → V02 native capture charts**; merged P05 adds reusable frequency counting, not a structured-parser dependency. V02 includes its bounded capture table, source navigation, commands and parity, so it does not wait for V08/V14/V15. V03 can then add optional rich rendering to this same capture dataset. In parallel, the F parser/time/table/query stack proceeds; **F07 → typed-query adapter, F09 → richer aggregates, F11 → shared reuse/live** unlock V04 onward without changing renderer contracts. V15 consumes U03/U05 rather than providing their builder. V13 consumes E08 profile datasets. Do not postpone P06 for the full gallery or pending P09–P13 work.

## Review focus

- Completed-looking drawings from capped scans or biased view samples: V01/V02/V17 must expose input completeness and reduction separately.
- Out-of-order times, incomparable device clocks, daylight-saving boundaries and provisional tails: V04/V10/V14 must preserve domains and revisions.
- Count/ID/timestamp precision through JSON/JavaScript: V01/V03/V18 must preserve tagged 64-bit values and exact drilldown.
- Cross-filtering an aggregate or synthetic Other/total row: V05/V08/V14 must resolve normalized lineage, not choose an arbitrary event.
- Rich renderer failure, disabled JavaScript or absent WebEngine package: V03/V16/V18 must keep the native investigation and data views usable.

## Common execution and verification rule for each card

- [ ] Rebase against the then-current default branch and confirm dependency APIs and proposed paths. Read applicable `AGENTS.md`. Run GitNexus upstream impact analysis before editing an existing symbol; record any unavailable/stale graph tool and use inspected direct callers. Do not claim graph verification from this planning snapshot.
- [ ] Add the named focused tests and confirm the intended feature fails before implementation. Keep fixtures tiny and exact for semantic tests; use deterministic generated corpora for boundedness/performance rather than screenshot-only assertions.
- [ ] Implement the named contract, register sources/tests in the verified CMake files (and F11/F14’s proposed `src/analysis/CMakeLists.txt` for service transforms) and add GUI/Commander schemas/execution/parity in the same public feature slice. A pure internal type/adapter slice may land first with no unfinished public action.
- [ ] Run the focused tags plus the shared analysis/source tests. On Windows use the verified developer environment and `scripts/codex/run-tests.ps1`; builds/configuration stay inside `build_root`. The actual root test option is `KLOGG_BUILD_TESTS`, as read from `CMakeLists.txt`, not an assumed `BUILD_TESTS` alias. The verification commands below are proposed future checks, not runs performed for this plan.
- [ ] Before integration, run the full CTest suite, native/rich capability tests that apply, accelerated/Qt-only capture parity, packaged GUI/CLI/MCP parity, graph change-scope detection and review of the focused diff. Record evidence/rollback switch; commit only the completed card and shared wiring it owns.

Suggested focused commands after building/deploying that card (select the named executable for the platform): `cilogg_tests '[visualization]'`; `cilogg_itests '[visualization]' -platform offscreen`; `cilogg_itests '[analysis],[source]' -platform offscreen`; `ctest --build-config RelWithDebInfo --output-on-failure` from `build_root`. Rich WebEngine rendering additionally needs a real supported display/backend in CI; an offscreen Widgets pass alone is not proof of packaged WebEngine operation.

## V01 — Freeze panel, dataset binding and capability contracts

**Integrated hard dependencies:** G01, G02, G03, F01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Establish renderer-independent meaning and automation before chart families diverge. Depends only on the minimal F01 Core-only value/dataset declarations and merged P03 job lifecycle. It does not depend on F07/F11 execution APIs, P09/P10 parsers/time or `klogg_analysis`. No chart worker, second scheduler or new query language.

**Files:** Verified anchors to inspect/extend: `src/logdata/CMakeLists.txt`, `src/ui/CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `src/logdata/include/analysiscommands.h`, `src/logdata/src/analysiscommands.cpp`. Proposed: `src/logdata/include/dashboardspec.h`, `src/logdata/src/dashboardspec.cpp`, `src/ui/include/chartdatasetadapter.h`, `src/ui/src/chartdatasetadapter.cpp`, `src/ui/include/chartregistry.h`, `src/ui/src/chartregistry.cpp`, `src/ui/include/chartcommands.h`, `src/ui/src/chartcommands.cpp`, `schemas/dashboard.schema.json`, `schemas/panel.schema.json`, `tests/unit/dashboardspec_test.cpp`, `tests/ui/chartdatasetadapter_test.cpp`.

**Interfaces:** Declare the types above; `bool validatePanelSpec(const PanelSpec&, const DatasetSchema&, QString* error)`; `bool ChartDatasetAdapter::bind(const PanelSpec&, const DatasetSchema&, ChartBinding* out, QString* error)`; `bool ChartDatasetAdapter::makeFrame(const PanelSpec&, const DatasetDescriptor&, const DatasetPage&, RenderFrame* out, QString* error)`; `RendererCapabilities ChartRegistry::capabilities(const QString& rendererId) const`; `QJsonObject ChartRegistry::kindSchema(const QString& kind) const`. Declare `QJsonObject ChartCommandAdapter::execute(const QString& action, const QJsonObject& arguments)` in the proposed UI command header and integrate it through the existing Commander catalog/dispatcher. Its initial capture operations depend only on `klogg_logdata`; later typed-query handlers are added as an injected adapter from `klogg_analysis`, not a dependency of early capture contracts. The registry owns allowed roles/transforms/versioned presentation keys, not computation.

- [ ] Add `panel_binding_rejects_wrong_units_and_missing_roles`: `bind(line, categoricalX, numericY)` fails unless the selected x-domain explicitly accepts a category; binding a string-typed bubble-size column fails; negative numeric size is rejected during `makeFrame` value validation; missing fields return field IDs and permitted alternatives.
- [ ] Add `frame_revision_and_precision_are_preserved`: a page from request revision 4 cannot update revision 5; count `9007199254740993` and timestamp nanos round-trip exactly; null/missing/empty retain distinct tags.
- [ ] Add `dataset_pages_do_not_become_unbounded_frames`: a greater-than-2-MiB frame is split/rejected with `frame_limit`, not sent to WebEngine; active/stale/partial/expired states survive the adapter.
- [ ] Implement registry validation, strict version checks, unknown optional `extensions` round-trip and structured error codes (`invalid_panel`, `unsupported_kind`, `stale_dataset`, `incompatible_clock`, `frame_limit`). Make diagnostic counters body-free.

**GUI/CLI/MCP parity:** Supply `list_chart_kinds`, `get_chart_capabilities`, `validate_panel` and `get_panel_schema` actions to the shared Commander catalog. GUI choices come from this same registry. CLI uses `cilogg command --action ACTION --json-file REQUEST.json`; MCP discovers via `cilogg_commands` and calls `cilogg_execute` with the same action/request envelope agreed by automation tasks. Do not create a chart-specific MCP server.

**Acceptance/rollback:** Schema/capability results are deterministic across native/rich builds, malformed specs never start scans, and serialized precision tests pass. Internal type slice can revert before consumers; once public, unregister the feature family before removing contracts and preserve saved unknown panel records.

**Primary references:** [Grafana query/transformation separation](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/query-transform-data/transform-data/), [ECharts features](https://echarts.apache.org/en/feature.html). These inform the boundary; the CILogg contracts above are proposed design choices.

## V02 — Ship P06 native capture charts and spike-preserving navigation

**Integrated hard dependencies:** G01, G02, G03, V01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Deliver useful charts before structured parsing: numeric capture versus physical line, line/step/point styles, counts/relative-frequency line buckets and min/max envelopes. Depends on V01 + F01 Core-only contracts and merged P03/P04; reuse the P05 count adapter already merged through PR #45. No F07/F09/F11, P09/P10 or `klogg_analysis` prerequisite. A line axis is never labeled elapsed time.

**Files:** Verified anchors: `src/ui/src/mainwindowregexlab.cpp`, `src/ui/include/regexlabdock.h`, `src/ui/src/regexlabdock.cpp`, `src/ui/src/crawlerwidget.cpp`, logdata/UI/test CMake files and `tests/mcp/analysis_parity.py`. Proposed: `src/logdata/include/chartextractionjob.h`, `src/logdata/src/chartextractionjob.cpp`, `src/logdata/include/capturedatasetadapter.h`, `src/logdata/src/capturedatasetadapter.cpp`, `src/ui/include/chartseriesmodel.h`, `src/ui/src/chartseriesmodel.cpp`, `src/ui/include/chartwidget.h`, `src/ui/src/chartwidget.cpp`, `src/ui/include/chartdock.h`, `src/ui/src/chartdock.cpp`, `src/ui/src/mainwindowcharts.cpp`, `tests/ui/chartseriesmodel_test.cpp`, `tests/ui/chartwidget_test.cpp`, `test_data/analysis/charts/captures.log`.

**Interfaces:** `chartextractionjob` is a thin named adapter around the existing `AnalysisJobService` regex processor/pager, not a new worker/query service. Declare `QString submitCaptureChart(const QJsonObject& definition, QString* error)` with pattern/capture/subpattern/scope/options/limits/request revision. It invokes existing analysis submission and uses the agreed pure `describeCaptureDataset(...)` / `decodeCaptureDatasetPage(...)` signatures above to expose F01 schemas/pages; `void ChartSeriesModel::setFrame(const RenderFrame&)`; `bool ChartWidget::setPanel(const PanelSpec&, QString* error)`; `void ChartWidget::setFrame(const RenderFrame&)`; signal `selectionRequested(const SelectionContext&)`. Capture-number/name semantics, repeated matches and missing policy match Regex Lab/P05. Initial charts use the native bounded `ChartSeriesModel`/`QTableView` for exact data inspection; they do not wait for F07/P12. The only early reduction operators are bounded min/max envelopes and line-domain counts/relative frequencies applied through the existing shared analysis processor/P05 utilities, with the same job/output limits. F09 later registers/generalizes these in the typed aggregate path; no duplicate long-lived aggregation engine is created.

- [ ] Add `capture_chart_uses_physical_lines_and_every_occurrence`: discontiguous selected lines 2/8 and two captures on line 8 retain original physical locations and separate occurrence identities; no compressed selection ordinal is presented as source line.
- [ ] Add `minmax_envelope_preserves_single_sample_spike`: values `[1,1,1000,1]` in one rendered bucket preserve extrema 1/1000 and their lineage; mean-only output fails the test. Envelope reduction happens in the existing C++ bounded analysis processor (reusing P05 aggregation helpers where applicable); painter uses returned bounds. This requires neither timestamp parsing nor F09 typed aggregates.
- [ ] Add `capture_chart_replaces_provisional_tail`: extending `latency=1` to `latency=12\n` replaces the provisional value, producing one sample at 12. Frozen snapshots show newer-data state and do not change their result.
- [ ] Add `capture_chart_builds_without_structured_stack`: configure/build the V01/V02 slice with no logformat/new analysis target and rich dashboards OFF; capture submit/page/point-open/bucket-open/cancel/release operations pass using the existing analysis service. Repeat through CLI/MCP before any F07/F11 adapter is linked.
- [ ] Implement finite numeric parsing with declared locale/unit rules, bounded points, missing/invalid counters, explicit denominator, keyboard point/bucket activation and the Qt data-table alternative. Native painter must clip painting to the viewport and avoid per-source-line objects.

**Parity:** Add `show_chart`, `set_chart`, `get_chart`, `run_chart`, `get_chart_data`, `open_chart_selection` (plus existing get/cancel/release analysis actions). `get_chart` returns definition + active dataset ID/revisions/state; `get_chart_data` delegates bounded dataset pages. Add GUI targets `chartDock`, `chartCanvas`, `chartDataTable`, `chartRunButton` and accessible names. Test service/CLI/MCP equality for captures, caps, line axes and stale navigation using one fixture.

**Acceptance/rollback:** A build with no logformat, typed-query service or WebEngine dependencies can run this complete public P06 feature. Native-only Qt5/Qt6 builds show a responsive chart of latency, frame time, build/test duration, heap or RSSI with exact source navigation. Disable the chart action/renderer independently; leave shared extraction and P05 counts working. G02 measures and freezes the baseline budgets; V02 records feature-specific measurements and V18/R02 extend the evidence for dashboards and release. Bounded allocations alone do not establish a latency pass.

**Primary references:** [ECharts line/bar/scatter concepts and custom series catalog](https://echarts.apache.org/en/feature.html), [Grafana visualization categories including numeric trend](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/). P06 native-painter constraints come from the verified repository roadmap.

## V03 — Add an optional, packaged, offline rich renderer

**Integrated hard dependencies:** G01, G02, G03, V01, V02. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Host bundled ECharts for advanced panels while retaining native operation. Depends on V01/V02; no F09/F14 chart family is required just to validate the host. Keep the app usable with WebEngine missing, disabled or crashed.

**Files:** Verified: top-level `CMakeLists.txt`, `src/ui/CMakeLists.txt`, `src/app/CMakeLists.txt`, `src/app/klogg.qrc`, `scripts/codex/build-windows.ps1`. Proposed: `src/ui/include/chartrenderer.h`, `src/ui/src/chartrenderer.cpp`, `src/ui/include/echartshost.h`, `src/ui/src/echartshost.cpp`, `src/ui/include/chartbridge.h`, `src/ui/src/chartbridge.cpp`, `src/app/dashboard.qrc`, `Resources/dashboard/index.html`, `Resources/dashboard/renderer.js`, `Resources/dashboard/style.css`, `3rdparty/echarts/` pinned asset bundle/manifest, `tests/ui/echartshost_test.cpp`, `tests/mcp/dashboard_renderer_checks.py`.

**Interfaces:** Declare abstract `ChartRenderer` with `RendererCapabilities capabilities() const`, `bool setPanel(const PanelSpec&, QString*)`, `void setFrame(const RenderFrame&)`, `void setSelection(const SelectionContext&)`, `void clear()` and `selectionRequested(SelectionContext)` signal. `EChartsHost` implements it; native `ChartWidget` uses an adapter. `bool ChartBridge::acceptEvent(const QJsonObject&, SelectionContext*, QString*)` validates kind, IDs, bounds, field roles and revisions before forwarding.

- [ ] Add `rich_renderer_offline_roundtrip`: with network requests denied, bundled line/bar panels render and return click/zoom events containing the original row/lineage IDs; data visible in tooltip equals native/table output.
- [ ] Add `bridge_rejects_stale_and_forged_event`: old dataset revision, unknown panel/row ID, oversized payload and incompatible bounds fail; repeated echoed cursor events do not loop.
- [ ] Add `webengine_failure_returns_native_table`: simulate host startup/process failure; source viewer remains responsive, panel data/inspect/drilldown survives, and state says `native_table_fallback` for advanced kinds. Do not disable the WebEngine sandbox as a packaging workaround.
- [ ] Introduce proposed `CILOGG_ENABLE_RICH_DASHBOARDS=OFF` option with `Qt${QT_VERSION_MAJOR}` optional discovery guarded from the core target. When explicitly ON without matching SDK modules, configuration reports the missing component; the normal native build remains dependency-free. Pin ECharts and choose only APIs supported by the packaged Chromium engine. Ship no remote assets, telemetry or arbitrary file/network access through the bridge.

**Parity:** `set_chart_renderer` chooses `auto/native/rich`; `get_chart_capabilities` reports compiled/available/failed renderer, supported kinds and actual fallback. Tests call all state changes from GUI, CLI and MCP. Headless data queries work without constructing WebEngine; image export explicitly reports renderer availability and uses V18's native fallback where supported.

**Acceptance/rollback:** Packaged Windows/macOS/Linux Qt5/Qt6 native matrix passes; rich packages pass an actual display/backend smoke test and process-restart test. Measure startup/RSS/package-size cost in V18. Turning the build/runtime option off preserves schemas/datasets/saved panels and shows their tables. Qt's unversioned Qt6 docs currently redirect to a separate WebEngine snapshot, so use the actual supported SDK's versioned docs/build requirements rather than silently raising C++ or Qt floors.

**Primary references:** [Qt5.15 WebEngine overview](https://doc.qt.io/archives/qt-5.15/qtwebengine-overview.html), [Qt6.10 WebEngine overview](https://doc.qt.io/qt-6.10/qtwebengine-overview.html), [ECharts rendering features](https://echarts.apache.org/en/feature.html), [ECharts events/actions](https://echarts.apache.org/handbook/en/concepts/event/).

## V04 — Render time-aware trends, bands, areas and event rates

**Integrated hard dependencies:** G01, G02, G03, V01, V02, F05, F09, F11. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Add event-time charts with explicit temporal meaning, stacked/100% areas and aggregate bands. Depends on V01/V02, F01/F09/F11 and the typed timestamp/clock-domain work corresponding to P10/P11; V03 enables rich equivalence. Do not implement timestamp parsing in a chart widget.

**Files:** Verified anchors: existing source-location/data CMake integration and `src/ui/src/crawlerwidget.cpp`; actual P10/F file paths are proposed by their owner. Proposed: `src/ui/include/timeseriespanel.h`, `src/ui/src/timeseriespanel.cpp`, `Resources/dashboard/panels/timeseries.js`, `tests/ui/timeseriespanel_test.cpp`, `test_data/analysis/charts/timeseries.jsonl`; extend V01 registry/schema and F09 aggregate output tests by owner agreement.

**Interfaces:** Declare `bool validateTimeSeriesBinding(const PanelSpec&, const DatasetSchema&, QString*)`; `void TimeSeriesPanel::setFrame(const RenderFrame&)`. Required dataset roles: x + value + optional series, or x/lower/upper/central for bands; metadata declares domain/zone, unit, bin start/end, reducer, gap policy, rate denominator and quantile method. V card maps results; F09 computes aligned buckets, rate, reductions, quantiles and stacking inputs.

- [ ] Add `event_rate_uses_duration_not_ingest_speed`: 12 accepted events in a 2-second UTC bucket yield 6 events/s regardless of source append/read speed; a line-axis request for `per_second` fails without typed event time.
- [ ] Add `time_buckets_are_half_open_and_calendar_aware`: timestamp exactly at `end` enters the next bucket; DST calendar-day fixture retains its actual 23/25-hour duration. Distinct device-uptime domains are rejected until an explicit alignment is supplied.
- [ ] Add `bands_and_percent_areas_preserve_semantics`: lower ≤ central ≤ upper; min/max are not labeled p5/p95; exact percentile fixture `[1,2,3,4,100]` uses the F09-defined quantile method; zero-denominator 100% bucket is no-data, not 0% for every series. Approximate results show method/error policy and never masquerade as exact.
- [ ] Implement straight/step/point lines, area/stacked/100% area, gap/zero/connect choices, unit-aware axes, explicit dual-axis labels and log-scale handling of nonpositive values. Preserve source-order vs event-time ordering choice and show out-of-order counts. Shared cursor/interactions connect in V14.

**Parity:** `set_chart` accepts these registered kinds and complete axis/reducer/gap/time/rate settings; `get_chart` reports effective settings, domain and invalid counts. Native/rich/service/CLI/MCP fixture values, bucket predicates and units agree. A settings change increments request revision and invalidates old gestures.

**Acceptance/rollback:** Users can distinguish traffic volume, failure percentage, test durations and frame-time bands without inferring time from file order. Disable time kinds while keeping line-number P06 charts; older saved panels remain inspectable and identify the missing capability.

**Primary references:** [Grafana time-series catalog](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/), [Elastic Lens field-driven chart creation and missing-value choices](https://www.elastic.co/docs/explore-analyze/visualize/lens), [ECharts event/action linkage](https://echarts.apache.org/handbook/en/concepts/event/).

## V05 — Add categorical rankings, proportions, Pareto and summary cards

**Integrated hard dependencies:** G01, G02, G03, V01, V02, F09. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Reuse P05/F09 for categorical comparison and at-a-glance measures across services, endpoints, test suites, job stages, frame phases or devices. Depends on V01/V02, merged P05 adapter and F09; optional V03.

**Files:** Verified: `docs/value-counts.md`, `src/logdata/include/analysiscommands.h`, `src/logdata/src/analysiscommands.cpp`, `tests/mcp/analysis_parity.py`. Proposed: `src/ui/include/categorypanel.h`, `src/ui/src/categorypanel.cpp`, `src/ui/include/statpanel.h`, `src/ui/src/statpanel.cpp`, `Resources/dashboard/panels/categories.js`, `Resources/dashboard/panels/stat.js`, `tests/ui/categorypanel_test.cpp`, `tests/ui/statpanel_test.cpp`. Discover actual P05 implementation paths from its merged tree; do not invent them as already verified.

**Interfaces:** `bool validateCategoryBinding(const PanelSpec&, const DatasetSchema&, QString*)`; `bool validateStatBinding(const PanelSpec&, const DatasetSchema&, QString*)`; `void CategoryPanel::setFrame(const RenderFrame&)`; `void StatPanel::setFrame(const RenderFrame&)`. Required category columns: key, value, optional series/rank/denominator/cumulative; stat columns: reduced value, optional baseline/threshold bounds/sparkline. F09 supplies exact aggregates and stable tie order.

- [ ] Add `topn_other_and_pareto_use_full_denominator`: counts A=5/B=3/C=2, Top2 gives Other=2 and percentages 50/30/20; Pareto cumulative is 50/80/100 and never renormalizes displayed Top2 to 100. Other's lineage matches C, not all source events.
- [ ] Add `bar_stacks_handle_sign_and_empty_values`: grouped bars allow signed values; percent-stack/pie rejects negative measures; null/missing/empty categories keep distinct labels and underlying values. Zero total shows no-data. Preserve P05 exact occurrence denominator/case-fold semantics.
- [ ] Add `stat_last_is_domain_ordered_and_units_match`: physical-last, event-time-latest and maximum reducers produce deliberately different fixture values; gauge validates min < max and compatible units; thresholds do not convert missing to healthy. Sparkline uses the same reduced dataset as its inspect table.
- [ ] Implement grouped/stacked/100% bars, horizontal/vertical ordering, exact TopN/Other, Pareto combo line, small-category pie/donut, stat/gauge/bar gauge and sparkline. Render long category labels accessibly and cap legends. Keep row selection tied to stable IDs rather than bar index after sorting.

**Parity:** Existing chart set/get/run/data operations cover all kinds and sorting/threshold/denominator settings. Add `apply_chart_selection` through the common selection route; exact literal capture filtering uses the P05 escaped-literal path, field categories use typed F predicates. Test GUI/CLI/MCP Other, empty/missing and count precision.

**Acceptance/rollback:** Rankings and summary values match P05/reference data, including counts >2^53; exact totals remain visible when rows are capped/partial. Disable the added kinds without affecting P05 dock or shared count engine.

**Primary references:** [Grafana visualization catalog: bar, stat, gauge, pie](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/), [ECharts chart catalog](https://echarts.apache.org/en/feature.html), [Grafana transform semantics](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/query-transform-data/transform-data/).

## V06 — Distinguish distributions, time buckets and both heatmaps

**Integrated hard dependencies:** G01, G02, G03, V01, F09, F11, V04. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Show numeric distributions and dense two-dimensional patterns without confusing latency-value bins with event-time counts. Depends on V01/F09/F11; time distribution requires typed P10 time and V04; optional V03.

**Files:** Proposed: `src/ui/include/distributionpanel.h`, `src/ui/src/distributionpanel.cpp`, `src/ui/include/heatmappanel.h`, `src/ui/src/heatmappanel.cpp`, `Resources/dashboard/panels/distribution.js`, `Resources/dashboard/panels/heatmap.js`, `tests/ui/distributionpanel_test.cpp`, `tests/ui/heatmappanel_test.cpp`, `test_data/analysis/charts/distributions.jsonl`. Verified CMake anchors remain the UI/test build files; bucket implementation extends F09's proposed source, not widget code.

**Interfaces:** `bool validateDistributionBinding(const PanelSpec&, const DatasetSchema&, QString*)`; `bool validateHeatmapBinding(const PanelSpec&, const DatasetSchema&, QString*)`; both panels implement `setFrame(const RenderFrame&)`. Histogram roles: binLow/binHigh/count. Time distribution: timeLow/timeHigh/valueLow/valueHigh/count. Matrix: xKey/yKey/measure with optional time intervals. Metadata declares inclusive final edge, underflow/overflow, density-vs-count and color-domain policy.

- [ ] Add `numeric_histogram_edges_and_denominator`: values `[0,9,10,20]`, bins `[0,10),[10,20]` yield 2/2; last edge policy is explicit, null/invalid do not become a zero bin. Uniform/log/custom-bin validation is performed in F09.
- [ ] Add `heatmap_forms_have_distinct_schemas`: time×latency heatmap encodes duration/count axes; service×status matrix encodes category/category axes; swapping roles fails validation. Empty cell differs from exact zero; color scaling never changes aggregate values.
- [ ] Add `heatmap_click_uses_both_bin_predicates`: clicking time `[10,20)` and numeric `[100,200)` returns only source events satisfying both; sparse output respects the total cell cap rather than allocating xBins*yBins blindly.
- [ ] Implement native painter heatmap with clipped cell painting, bounded hit testing, accessible table and sequential/diverging palettes. Allow absolute/common color scale for comparisons; show clamping/log-zero treatment explicitly. Rich renderer receives precalculated sparse cells, not raw values to bin in JS.

**Parity:** `set_chart` persists numeric-bin edges/count, heatmap mode/color scale and density choice; `get_chart` exposes effective bins and excluded counts. Dataset export includes boundaries so external tools reproduce the chart. GUI/CLI/MCP clicks create equivalent typed predicates.

**Acceptance/rollback:** A 100-ms latency histogram, events per second and time×latency heatmap are visibly and structurally different; cell sums reconcile to accepted input. Disable these kinds while retaining dataset inspection and V02 plots.

**Primary references:** [Grafana histogram](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/histogram/), [Grafana histogram-over-time heatmap](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/heatmap/), [Elastic Lens two-dimensional heat map](https://www.elastic.co/docs/explore-analyze/visualize/lens).

## V07 — Add scatter/bubble correlation and honest boxplots

**Integrated hard dependencies:** G01, G02, G03, V01, F09, F11. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Explore relations between two numeric fields and compare distributions between runs/groups. Depends on V01/F09/F11; optional V03. Correlation visuals do not imply causation or invent a join between unrelated events.

**Files:** Proposed: `src/ui/include/xypanel.h`, `src/ui/src/xypanel.cpp`, `src/ui/include/boxplotpanel.h`, `src/ui/src/boxplotpanel.cpp`, `Resources/dashboard/panels/xy.js`, `Resources/dashboard/panels/boxplot.js`, `tests/ui/xypanel_test.cpp`, `tests/ui/boxplotpanel_test.cpp`; register via verified UI/test CMake anchors.

**Interfaces:** `bool validateXYBinding(const PanelSpec&, const DatasetSchema&, QString*)`; `bool validateBoxplotBinding(const PanelSpec&, const DatasetSchema&, QString*)`; `void XYPanel::setFrame(const RenderFrame&)`; `void BoxplotPanel::setFrame(const RenderFrame&)`. XY columns: x/y + optional size/color/series/event ID; boxplot columns: category,n,q1,median,q3,whiskerLow,whiskerHigh,outlier role and quantile method. Paired numeric values originate from the same event or an explicit shared join transform.

- [ ] Add `xy_missing_pairs_do_not_shift_alignment`: rows `(1,10),(missing,20),(3,30)` produce the two correct points, not zipped independent columns. Bubble size scales area from nonnegative input; invalid/negative sizes are rejected or counted, never absolute-valued silently.
- [ ] Add `scatter_view_reduction_is_declared_and_deterministic`: under the configured view cap, stable seeded selection returns identical row IDs and `sampled_view`; the full query count and exact brush drilldown remain available. Use shared bounded reduction, never `array.slice()` in JS.
- [ ] Add `boxplot_method_and_small_groups_are_explicit`: F09's exact quartile/whisker method yields expected five-number values for a fixed fixture; n=0/1/2 and all-equal groups are deterministic. Outlier marks and source predicates match the chosen Tukey/custom whisker policy. Capped outlier display remains labeled and drillable.
- [ ] Implement native axes, scatter/bubble/boxplot painting, series legend and rectangle selection; rich implementation consumes identical data. Unit/domain labels are retained on both axes; log transforms handle nonpositive values with visible excluded counts.

**Parity:** All settings and reduction policy round-trip via chart set/get; `apply_chart_selection` accepts a validated typed XY rectangle; row activation resolves lineage. GUI/CLI/MCP reference output agrees before and after plot sorting/view reduction.

**Acceptance/rollback:** Developers can compare payload size versus latency, work size versus task duration, heap versus frame time, or test distributions without accidentally pairing different records. Disable XY/boxplot registrations; preserve datasets and tables.

**Primary references:** [Grafana XY/scatter/bubble data requirements](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/xy-chart/), [ECharts scatter and boxplot support](https://echarts.apache.org/en/feature.html).

## V08 — Reuse the virtual table for aggregate, pivot and JSON inspection

**Integrated hard dependencies:** G01, G02, G03, F07, F09, V01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Make every chart's precise data inspectable and navigable, and support table/pivot/JSON as first-class panels. Depends on F07's canonical virtual model, F09 pivot and V01. Do not fork the P12 structured-event table or sort only the loaded page while claiming a global sort.

**Files:** Verified anchors: `src/ui/src/crawlerwidget.cpp`, `src/ui/src/tabbedcrawlerwidget.cpp`, `src/ui/CMakeLists.txt`, `tests/ui/CMakeLists.txt`. Proposed: `src/ui/include/aggregatedatasetmodel.h`, `src/ui/src/aggregatedatasetmodel.cpp`, `src/ui/include/datasetinspector.h`, `src/ui/src/datasetinspector.cpp`, `src/ui/include/jsoninspectormodel.h`, `src/ui/src/jsoninspectormodel.cpp`, `tests/ui/aggregatedatasetmodel_test.cpp`, `tests/ui/datasetinspector_test.cpp`; adapt F07 model through its declared interfaces after rebasing.

**Interfaces:** `void AggregateDatasetModel::attach(const QString& datasetJobId, const DatasetDescriptor&)`; `bool AggregateDatasetModel::acceptPage(const DatasetPage&, QString*)`; `LineageHandle AggregateDatasetModel::lineageForRow(const QString& rowId) const`; `QJsonObject DatasetInspector::describePanel(const PanelSpec&, const DatasetDescriptor&) const`. F07 owns lazy page/cache mechanics and field-value delegates; V adapter supplies aggregate schema, row IDs and total/missing cells.

- [ ] Add `pivot_totals_do_not_become_source_rows`: category×status pivot with subtotal/grand-total cells produces correct F09 values and lineage; activating a total opens a child result set, never line number equal to table row.
- [ ] Add `table_sort_requests_shared_query_and_preserves_identity`: changing numeric descending sort starts a new shared query/revision; unloaded records participate; equal values tie by stable provenance; selection remains by row ID and cap refusal is explicit.
- [ ] Add `json_inspector_preserves_types_and_limits_expansion`: nested arrays/null/missing/raw strings retain their values; expansion depth/bytes are bounded; truncated node offers a paged/raw inspect operation. Raw text stays available for malformed records.
- [ ] Implement dataset/query/stats/JSON/error tabs in the inspector, with before/after typed transforms and display formatting separately visible. Column pin/hide/order/width are presentation state; CSV/JSON exports choose raw versus display values explicitly.

**Parity:** `inspect_panel` returns resolved `QuerySpec`, normalized transform plan, schema, units/clock, limits/counters, source manifest, completeness, reduction and errors; `get_chart_data` remains paged. `set_panel_table_layout`, `get_panel_table_layout` and JSON node paging have matching GUI/CLI/MCP states.

**Acceptance/rollback:** Every supported panel, including advanced/native fallback, can display its underlying dataset and open original records. Memory grows with bounded cache, not source record count. Disable aggregate panel kinds/inspector UI independently while F07's event table remains available.

**Primary references:** [Grafana panel inspect view](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/panel-inspector/), [Grafana transformations/table fallback](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/query-transform-data/transform-data/).

## V09 — Implement hierarchy, Sankey and explicit relationship panels

**Integrated hard dependencies:** G01, G02, G03, V01, V03, V08, F14. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Support ownership/resource trees, transition volumes and explicit call/service/task relations. Depends on V01/V03/V08 and F14 shared transform registry. Do not infer causal edges merely because log lines are adjacent.

**Files:** Proposed shared-service transform files under F14 in `klogg_analysis` (never linked back into logdata): `src/analysis/include/hierarchydatasettransform.h`, `src/analysis/src/hierarchydatasettransform.cpp`, `src/analysis/include/graphdatasettransform.h`, `src/analysis/src/graphdatasettransform.cpp`. Proposed UI: `src/ui/include/relationshippanel.h`, `src/ui/src/relationshippanel.cpp`, `Resources/dashboard/panels/hierarchy.js`, `Resources/dashboard/panels/graph.js`; tests `tests/ui/hierarchydatasettransform_test.cpp`, `tests/ui/graphdatasettransform_test.cpp`, `tests/ui/relationshippanel_test.cpp`. Verified build anchors: UI/test CMake files; shared-transform registration additionally uses F11/F14’s proposed `src/analysis/CMakeLists.txt`.

**Interfaces:** Declare `bool registerHierarchyDatasetTransforms(DatasetTransformRegistry*, QString*)` and `bool registerGraphDatasetTransforms(DatasetTransformRegistry*, QString*)`, invoked by the F14 service registry. Versioned tokens `hierarchy@1`, `edges@1`, `sankey@1`. Hierarchy input: explicit ordered path fields + nonnegative measure → nodeId,parentId,label,value,depth,lineage; edge input: explicit source/target IDs + nonnegative weight → node/edge tables with shared lineage. `sankey@1` requires acyclic staged/declared directed flow; `node_graph` allows cycles and preserves direction. IDs are deterministic and independent of labels.

- [ ] Add `hierarchy_rollup_has_no_double_count_or_label_collision`: paths `a/b` and literal label `a/b` remain distinct IDs; child weights reconcile with root; duplicated aggregate inputs follow declared reducer rather than adding preaggregated totals twice.
- [ ] Add `sankey_rejects_cycle_while_node_graph_accepts`: A→B→A fails `sankey@1` with actionable cycle detail but produces a bounded two-node directed graph. Negative/nonfinite weights fail; disconnected nodes/zero weights are deterministic.
- [ ] Add `relationship_caps_and_lineage_are_exact`: node/edge/depth caps stop visibly with partial/resource limit; Other group or edge selects its real predicate. Graph layout is deterministic or seeded and has an iteration/time budget; layout never triggers a rescan.
- [ ] Implement ECharts treemap/sunburst/Sankey/node graph mappings, hierarchy drill-up breadcrumbs, focus neighbors and the same exact table fallback in native builds. Layout belongs to rendering; node weights/edge aggregation stay in F14's C++ transform.

**Parity:** Chart schemas expose path fields/measure/reducer/edge direction/layout seed and caps. `apply_chart_selection` accepts node/edge/path lineage; `inspect_panel` shows graph transform provenance. GUI/CLI/MCP transform values and drilldown subsets agree.

**Acceptance/rollback:** A memory-ownership tree, request routing volume or dependency graph remains faithful to declared input semantics; no invented trace/call tree from arbitrary logs. Unregister transform/kinds only after dependent panels are disabled; native tables and saved specs survive.

**Primary references:** [ECharts hierarchy/graph/custom series catalog](https://echarts.apache.org/en/feature.html), [Grafana explicit node-graph visualization](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/node-graph/), [OpenObserve nested-data custom charts](https://openobserve.ai/docs/user-guide/analytics/dashboards/custom-charts/custom-charts-nested-data/).

## V10 — Distinguish state intervals, observations, event lanes and waterfalls

**Integrated hard dependencies:** G01, G02, G03, V01, V04, V08, F14. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Investigate app/device lifecycle, worker/task state, builds/tests and request timing using honest temporal models. Depends on V01/V04/V08, typed time/clock domains and F14; V03 supplies optional rich drawing. Explicit interval rendering can be native.

**Files:** Proposed: `src/analysis/include/timelinedatasettransform.h`, `src/analysis/src/timelinedatasettransform.cpp`, `src/ui/include/timelinepanel.h`, `src/ui/src/timelinepanel.cpp`, `Resources/dashboard/panels/timeline.js`, `tests/ui/timelinedatasettransform_test.cpp`, `tests/ui/timelinepanel_test.cpp`, `test_data/analysis/charts/lifecycle.jsonl`.

**Interfaces:** `bool registerTimelineDatasetTransforms(DatasetTransformRegistry*, QString*)`; tokens `state_intervals@1`, `observations@1`, `event_lanes@1`, `spans@1`. State input `(entity,time,state[,end])` → explicit start/end/state/knownness/source lineage; observation history preserves each sample box. Events use entity/lane + timestamp + label/severity. Spans require explicit start/end or start+duration and correlation/span/parent IDs; optional configured begin/end matching must declare key/window/unmatched policy before constructing spans. Never call a generic duration bar a distributed trace.

- [ ] Add `timeline_merges_states_history_keeps_samples`: states `[A@0,A@1,B@2]` render one A interval in timeline, three observations in history; last state ends at the frozen range boundary as provisional/open, with unknown gaps governed by explicit max-hold policy.
- [ ] Add `waterfall_does_not_fabricate_correlations`: identical request labels with distinct IDs remain separate; orphan/missing-end/negative-duration spans show diagnostics; nested/overlapping spans preserve parentage and source refs. Pairing over caps reports unmatched records.
- [ ] Add `timeline_orders_late_events_in_clock_domain`: duplicate/out-of-order times have stable source tie-breaks; incompatible device clocks fail until aligned. Appended provisional span/state is replaced, not duplicated; an old selected interval cannot navigate after generation replacement.
- [ ] Implement bounded lane virtualization, zoom, overview, severity markers, hierarchical waterfall expansion and duration tooltips. Native painter clips to visible lanes/time range; advanced lineage is always accessible through the shared table.

**Parity:** `set_chart` exposes merge/max-hold/gap/order/span-match policies and lane/group fields; `get_chart` returns effective temporal domain and unmatched counts. `open_chart_selection` opens all matched events in the selected interval/span, not just its start row. GUI/CLI/MCP fixtures agree.

**Acceptance/rollback:** Users can clearly distinguish observed status from inferred duration and real correlated spans. Disable timeline kinds/transforms independently; typed timestamps and V04 time-series remain usable.

**Primary references:** [Grafana state timeline](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/state-timeline/), [Grafana status history and nonmerged observations](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/status-history/), [Grafana traces](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/traces/).

## V11 — Add funnels, parallel coordinates and normalized radar comparisons

**Integrated hard dependencies:** G01, G02, G03, V01, V03, V08, F14. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Offer deliberate advanced views for explicit cohort stages, many numeric dimensions and comparable metrics. Depends on V01/V03/V08/F14; typed time when funnel windows/order use timestamps. These are optional selections, never automatic defaults for a single generic number.

**Files:** Proposed: `src/analysis/include/cohortdatasettransform.h`, `src/analysis/src/cohortdatasettransform.cpp`, `src/ui/include/multidimensionalpanel.h`, `src/ui/src/multidimensionalpanel.cpp`, `Resources/dashboard/panels/multidimensional.js`, `tests/ui/cohortdatasettransform_test.cpp`, `tests/ui/multidimensionalpanel_test.cpp`.

**Interfaces:** `bool registerCohortDatasetTransforms(DatasetTransformRegistry*, QString*)`; `funnel@1` input: correlation/cohort ID + configured ordered stage predicates + declared time window/dedup/re-entry policy → stage,count,denominator,conversion,lineage. `dimensions@1` input: same-row numeric fields → stable entity rows with dimension units/scales/missing metadata. `void MultidimensionalPanel::setFrame(const RenderFrame&)`. Radar requires configured reference ranges/normalization and direction (`higher_better`/`lower_better`/neutral), retaining raw values in inspect/tooltips.

- [ ] Add `funnel_uses_cohorts_and_stage_order`: cohort X completes A→B→C, Y only A→C and Z repeats A; ordered funnel counts `[3,1,1]` under first-entry policy, with explicit denominator choices. A simple unordered stage-frequency chart must be labeled frequency, not conversion.
- [ ] Add `parallel_brush_uses_intersection_of_typed_ranges`: selecting latency≤10 and payload≥1000 returns only rows satisfying both; missing value policy is explicit. Bounded shared reduction retains stable IDs and announces any representative sample.
- [ ] Add `radar_never_compares_incompatible_raw_units`: raw ms and bytes cannot share an unlabeled common scale; configured references produce expected normalized ratios while tooltip/CSV retains raw values. Zero reference, negative-range policy and absent dimensions fail or show no-data deterministically.
- [ ] Implement ECharts funnel/parallel/radar mapping, capped dimensions/series, readable ordering and native typed-table fallback. Shared transforms perform cohort/dedup/normalization calculations once; JavaScript does not reinterpret stage SQL or filter log text.

**Parity:** Kind schemas expose all cohort/window/scale/direction settings. Typed stage/dimension selection uses common selection actions. GUI/CLI/MCP check the same stage counts, denominator and multi-axis brush results.

**Acceptance/rollback:** The editor asks for missing required cohort IDs/scale definitions and explains the unsupported mapping instead of drawing misleading shapes. Disable these advanced kinds/F14 transform registrations; simpler charts remain available.

**Primary references:** [ECharts funnel/parallel catalog](https://echarts.apache.org/en/feature.html), [Splunk Dashboard Studio visualization catalog](https://help.splunk.com/splunk-cloud-platform/create-dashboards-and-reports/dashboard-studio/10.1.2507/introduction-to-splunk-dashboard-studio/what-is-splunk-dashboard-studio), [ECharts examples](https://echarts.apache.org/examples/en/index.html).

## V12 — Add maps and equipment/layout overlays from explicit coordinates

**Integrated hard dependencies:** G01, G02, G03, V01, V03, V08, F14. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Support geographic events, spatial test/game traces and equipment/process layouts when logs contain real coordinate or region/element data. Depends on V01/V03/V08/F14. Maps are never inferred from IPs/hostnames unless the user adds an explicit enrichment dataset; local equipment coordinates are not latitude/longitude.

**Files:** Proposed: `src/analysis/include/spatialdatasettransform.h`, `src/analysis/src/spatialdatasettransform.cpp`, `src/ui/include/spatialpanel.h`, `src/ui/src/spatialpanel.cpp`, `Resources/dashboard/panels/spatial.js`, `schemas/layout-asset.schema.json`, `tests/ui/spatialdatasettransform_test.cpp`, `tests/ui/spatialpanel_test.cpp`, `test_data/analysis/charts/layout.svg`.

**Interfaces:** `bool registerSpatialDatasetTransforms(DatasetTransformRegistry*, QString*)`; `geo_points@1` input: explicit latitude/longitude or declared projected x/y/CRS + measure/time/entity → point/region/track dataset; `layout_overlay@1` input: stable elementId or local x/y + measure/state → asset-bound overlay. `bool SpatialPanel::setLayoutAsset(const QByteArray& bytes, const QJsonObject& metadata, QString* error)` validates size/hash/view box/allowed content before rendering. Assets are user-selected/bundled and referenced by content hash in dashboard bundles.

- [ ] Add `geo_domain_and_coordinate_validation`: lat ±90/lon ±180 endpoints are valid; out-of-range, swapped roles and nonfinite values are reported; custom x/y requires declared units/CRS. Antimeridian track segments and region IDs retain deterministic handling.
- [ ] Add `layout_overlay_uses_element_ids_not_label_text`: duplicate display labels never merge sensors/elements; pan/zoom/click resolves the actual element predicate; missing asset/element is a visible diagnostic with data table available.
- [ ] Add `spatial_assets_are_local_bounded_and_reproducible`: remote/active SVG resources and oversized assets are rejected; export works offline from hashed included assets. Geographic point clustering/density is a shared transform with explicit binning/selection, not silent browser-side loss of events.
- [ ] Implement point/bubble/region/track map modes, static offline basemaps and equipment/floor/process overlays. Default geographic background is an offline outline/grid; optional remote tiles require an explicit saved source setting and never receive raw logs. Native fallback is exact coordinate/element table; native simple layout drawing may be added under the same contract.

**Parity:** `set_chart`, `set_panel_asset`, `get_panel_asset` and capability state expose CRS/asset hash/region mapping and missing-data policy; map-region or equipment click feeds typed selection. GUI/CLI/MCP obtain the same rows and asset identity without a network dependency.

**Acceptance/rollback:** No map draws from undeclared geographic meaning; equipment overlays remain usable offline. Disable spatial rendering while preserving asset metadata, table and query output; do not remove shared field parsing.

**Primary references:** [Grafana geomap](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/geomap/), [Grafana canvas/layout overlays](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/canvas/), [ECharts maps](https://echarts.apache.org/en/feature.html).

## V13 — Gate flame graphs on actual profiling input

**Integrated hard dependencies:** G01, G02, G03, V01, V03, V08, F14, E08. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Visualize real stack samples/allocation or duration profiles for any supported developer domain. Depends on V01/V03/V08/F14 and E08’s separately owned validated input adapters (Speedscope, ChromeTrace, OTLP and folded input). Ordinary error stack traces or arbitrary message categories do not establish sample weight, nesting or CPU time.

**Files:** Proposed: `src/analysis/include/profiledatasettransform.h`, `src/analysis/src/profiledatasettransform.cpp`, `src/ui/include/profilepanel.h`, `src/ui/src/profilepanel.cpp`, `Resources/dashboard/panels/profile.js`, `tests/ui/profiledatasettransform_test.cpp`, `tests/ui/profilepanel_test.cpp`, `test_data/analysis/charts/profile.folded`; E08 owns import-adapter source paths and format fixtures, which V13 consumes rather than recreates.

**Interfaces:** `bool registerProfileDatasetTransforms(DatasetTransformRegistry*, QString*)`; E08 submits/imports supported input through the shared analysis facade with explicit profile/span shape, units, source references and limits; V13 does not add a competing importer. `profile_tree@1` consumes validated stack frames + nonnegative sample weight + unit/profile kind → node,parent,depth,selfWeight,totalWeight,label,lineage. `void ProfilePanel::setFrame(const RenderFrame&)`. Accept E08 profile-shaped folded/Speedscope datasets once available. ChromeTrace/OTLP datasets supply flame graphs only when the imported representation contains explicit valid stack/weight/profile semantics; ordinary spans route to V10’s waterfall instead. Additional formats such as pprof require their own explicit E08 adapter/tests.

- [ ] Add `profile_tree_self_and_total_weights_reconcile`: stacks `root;a 3`, `root;b 2`, `root;a;c 5` yield root total 10, a total 8/self 3, b 2 and c 5. Duplicate folded paths coalesce without losing source lineage.
- [ ] Add `ordinary_logs_cannot_be_labeled_cpu_profile`: a generic exception traceback without a profile declaration/weight returns `profile_input_required`; sample counts cannot be labeled milliseconds or bytes without explicit declared conversion/profile unit.
- [ ] Add `profile_depth_weight_and_cycle_limits`: negative/nonfinite weights, excessive frame depth and malformed input return bounded diagnostics; zero-sample profiles show no-data; excessive output is visibly capped. Imported profile rows resolve to their original profile file locations with the same historical checks.
- [ ] Implement flamegraph/icicle navigation, zoom to stack, search/highlight, self/total toggle and top-functions table. Width/weights are precomputed by shared transform; renderer arranges geometry without inventing weights. Differential profile display requires comparison service normalization and explicit units.

**Parity:** Reuse E08 import input/schema/status/cancel operations; add only standard chart/profile state and paged data operations. GUI/CLI/MCP produce identical totals and stable stack IDs; native fallback is a profile top/tree table with exact weights.

**Acceptance/rollback:** Real profile inputs render reproducibly with labeled units; generic logs cannot generate misleading CPU flame graphs. Disable profile kinds before unregistering their transform; E08 import/adapters remain independently available; leave normal source opening and tables intact.

**Primary references:** [Grafana flame graph and nested profile data requirements](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/flame-graph/), [ECharts custom-series facility](https://echarts.apache.org/en/feature.html).

## V14 — Unify linked selection, filters, variables, annotations and drilldown

**Integrated hard dependencies:** G01, G02, G03, V01, V02, V08, U03, F10. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Make charts investigative: synchronized zoom/brush/cursor, cross-filtering, point/bucket drilldown, shared variables and contextual event/deployment markers. Depends on V01/V02/V08 and the GUI shell's selection/navigation history; time synchronization also needs V04. Applies incrementally to every chart family.

**Files:** Verified: `src/ui/src/crawlerwidget.cpp`, `src/ui/src/tabbedcrawlerwidget.cpp`, `src/ui/src/commander.cpp`, `src/app/mcp/runtime.cpp`. Proposed: `src/ui/include/dashboardselectioncontroller.h`, `src/ui/src/dashboardselectioncontroller.cpp`, `src/logdata/include/dashboardvariables.h`, `src/logdata/src/dashboardvariables.cpp`, `src/logdata/include/dashboardannotation.h`, `src/logdata/src/dashboardannotation.cpp`, `tests/ui/dashboardselectioncontroller_test.cpp`, `tests/ui/dashboardannotation_test.cpp`, `tests/mcp/dashboard_interaction_parity.py`.

**Interfaces:** `bool DashboardSelectionController::apply(const SelectionContext&, QString* error)`; `SelectionContext DashboardSelectionController::current() const`; `void DashboardSelectionController::clear(const QString& dashboardId)`; `QString DashboardSelectionController::drilldown(const SelectionContext&, const DrilldownRequest&, QString*)`. Define versioned annotation JSON with id/title/text/kind/start/end/clockDomain/optional event lineage/style; deployment/build/test/run markers use the same type. Declare `bool bindDashboardVariables(const DashboardSpec&, const QJsonObject& values, QuerySpec* out, QString* error)` with typed binding, dependency DAG and bounded option-query results.

- [ ] Add `aggregate_selection_opens_exact_child_result`: point opens its event, a bucket opens its normalized matching child job, Other/total/edge selects constituent predicate, and a stale source returns `stale_source` without moving the raw viewer.
- [ ] Add `linked_cursor_and_brush_do_not_loop`: an origin+eventSequence echo is ignored, old revisions are rejected, a compatible time range updates linked panels once and an incompatible uptime clock is left unchanged with a reason. Brush preview does not rescan on every mouse move; commit/debounce coalesces.
- [ ] Add `variables_are_typed_bounded_and_cycle_checked`: quote/metacharacter values remain data; missing/null/multi-select values bind predictably; cyclic dependencies fail before scans; option pages honor source/scope/caps.
- [ ] Add `annotations_preserve_time_domain_and_provenance`: user note, deployment marker and duration region round-trip; event-bound marker invalidates navigation on rewrite; manual time marker remains labeled manual. Hover/pinned cursor and keyboard controls expose equivalent values in the table.
- [ ] Implement filter chips with AND/OR semantics from shared `QuerySpec`, include/exclude/clear, optional dashboard/local panel scope, zoom reset/back history, shared crosshair, source context opening and linked target dashboard navigation. Carry query/filter/time/variables intentionally, with visible target scope; no silent mutation of the legacy regex search.

**Parity:** Add `set_dashboard_selection`, `get_dashboard_selection`, `clear_dashboard_selection`, `drilldown_panel`, `set_dashboard_variables`, `get_dashboard_variables`, `list/add/update/remove_dashboard_annotation`. Every operation is discoverable and returns the effective scope/revisions. Test gestures, CLI and MCP against identical typed selections; transport does not accept executable JS predicates.

**Acceptance/rollback:** Any plotted datum can explain its provenance and continue to raw/structured evidence; aggregate drilldown is never mistaken for one line. Disable linked propagation while keeping local selection/data inspection; preserve annotation/variable definitions in saved specs.

**Primary references:** [ECharts events and actions](https://echarts.apache.org/handbook/en/concepts/event/), [Elastic dashboard drilldowns carrying context](https://www.elastic.co/docs/explore-analyze/dashboards/create-dashboard-drilldown), [OpenObserve dashboard variables overview](https://openobserve.ai/docs/user-guide/analytics/dashboards/).

## V15 — Deliver simple chart creation, inspectable suggestions and dashboards

**Integrated hard dependencies:** G01, G02, G03, V01, V02, V08, V14, U03, U05. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Turn the gallery into a usable daily workflow: select records/fields → choose a suggested chart → adjust → add to dashboard. Depends on V01/V02/V08/V14 plus U03 source/scope/query-context adapters and U05 field/aggregation-builder adapters, the GUI workspace/saved-view shell and whichever chart kinds are available. Does not require users to know SQL, ECharts or an AI service. U05 owns the field/query builder; this task owns visualization recommendations, preview and presentation authoring around it, with one canonical draft and no duplicated builder state.

**Files:** Verified: `src/ui/src/mainwindowregexlab.cpp`, `src/ui/src/crawlerwidget.cpp`, `src/ui/src/tabbedcrawlerwidget.cpp`, `src/ui/src/commander.cpp`, `src/app/klogg.qrc`. Proposed: `src/ui/include/charteditordock.h`, `src/ui/src/charteditordock.cpp`, `src/ui/include/chartsuggestionservice.h`, `src/ui/src/chartsuggestionservice.cpp`, `src/ui/include/dashboardwidget.h`, `src/ui/src/dashboardwidget.cpp`, `src/logdata/include/dashboardstore.h`, `src/logdata/src/dashboardstore.cpp`, `tests/ui/charteditordock_test.cpp`, `tests/ui/chartsuggestionservice_test.cpp`, `tests/ui/dashboardstore_test.cpp`. Use shell persistence APIs once declared, not another session system.

**Interfaces:** `QVector<ChartSuggestion> ChartSuggestionService::suggest(const DatasetSchema&, const QJsonObject& boundedProfile, const RendererCapabilities&) const`; `bool ChartEditorDock::setDraft(const PanelSpec&, QString*)`; `PanelSpec ChartEditorDock::draft() const`; `bool DashboardWidget::setDashboard(const DashboardSpec&, QString*)`; `DashboardSpec DashboardWidget::dashboard() const`; `bool DashboardStore::save(const DashboardSpec&, const QString& absolutePath, QString*)`; `bool DashboardStore::load(const QString& absolutePath, DashboardSpec* out, QString*)`. Suggestions inspect bounded schema/profile metadata, not rescan source text. Wire U03’s canonical scope/source/query-context state and U05’s canonical field-role/aggregation draft-change adapters to this editor; exact adapter names/signatures come from those owner cards at integration. V15 does not define an alternative field/query grammar, evaluator or builder widget. Keep the dependency acyclic: U05 may consume V01 role/schema contracts and this card’s behavioral specification, but must not depend on V15’s finished widgets; U03 + V01 → U05 adapters → V15.

- [ ] Add `suggestion_rules_are_simple_stable_and_explained`: one numeric field suggests histogram/stat; category+numeric suggests bar; time+numeric suggests line; two numeric fields suggest scatter; entity+state+time suggests state/history choices. Suggestion includes a reason, required fields, domain and native fallback capability; no timestamp is guessed from an arbitrary numeric column.
- [ ] Add `create_chart_from_selection_preserves_scope`: discontiguous selection/current completed matches/sample/whole-file choices freeze the selected source manifest; context lines/marks are not matches; preview shows sample/partial badge. User explicitly runs whole-file analysis and can inspect/query the result before saving.
- [ ] Add `dashboard_layout_roundtrip_is_versioned`: drag/resize/keyboard move, tabs/rows, panel duplicate/rename/remove and table layouts save with stable IDs; import preserves unknown optional extensions, rejects unsupported mandatory versions/duplicate IDs/path traversal and never auto-runs embedded custom code. Failed save is atomic; source text/pasted secrets are not saved by default.
- [ ] Compose the editor from U03’s source/scope context and U05’s existing field/aggregation builder, plus V15’s chart/style choices and optional normalized query/transform inspection. Add `chart_editor_uses_one_shared_builder_draft`: changing a field/aggregation in U05 updates the suggestion/preview once; editing chart style leaves that same canonical query draft unchanged; source/scope changes come from U03 without a second selector state. Provide search/browse chart gallery, preview, Apply/Add to dashboard, title/legend/unit/threshold controls and visible disabled reasons. Reuse shell tabs/layout/history. Saved dashboards store definitions/source bindings, not unbounded retained output; reopening asks for unresolved source mappings without opening every historical source automatically.

**Parity:** Add dashboard/panel CRUD and `suggest_charts`, `get/set_panel`, `get/set_dashboard_layout`, `list/get/save/load/import/export_dashboard`, with stable IDs and explicit no-auto-run import state. GUI uses these same operations. CLI/MCP can construct, validate, save and inspect a complete dashboard with no screen coordinates.

**Acceptance/rollback:** A developer can plot a selected latency capture or category count in a few clear choices and understand every suggestion. Required settings/state are usable through GUI, CLI and MCP; saved/imported dashboards reopen in native-only builds. Disable authoring UI while keeping loaded dashboards/table inspection and export of definitions.

**Primary references:** [Elastic Lens field selection/suggestions](https://www.elastic.co/docs/explore-analyze/visualize/lens), [Grafana suggestions](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/), [Splunk Studio tabs and gallery examples](https://www.splunk.com/en_us/blog/tips-and-tricks/dashboard-studio-tabbed-dashboards.html), [OpenObserve dashboards](https://openobserve.ai/docs/user-guide/analytics/dashboards/).

## V16 — Add custom charts and markdown/text without opening a second engine

**Integrated hard dependencies:** G01, G02, G03, V01, V03, V08, V15. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Cover domain-specific drawings and explanatory dashboards while preserving bounded shared computation. Depends on V01/V03/V08/V15. Markdown/text is native and can ship without WebEngine; rich custom drawing is an optional capability.

**Files:** Proposed: `src/logdata/include/custompanelspec.h`, `src/logdata/src/custompanelspec.cpp`, `src/ui/include/custompanelhost.h`, `src/ui/src/custompanelhost.cpp`, `src/ui/include/textpanel.h`, `src/ui/src/textpanel.cpp`, `Resources/dashboard/panels/custom.js`, `Resources/dashboard/panels/text.js`, `schemas/custom-panel.schema.json`, `tests/ui/custompanelhost_test.cpp`, `tests/ui/textpanel_test.cpp`. Verified anchors: `src/app/klogg.qrc`, UI/logdata CMake and command-catalog integration paths.

**Interfaces:** `bool validateCustomPanel(const PanelSpec&, const DatasetSchema&, QString*)`; `bool CustomPanelHost::setDefinition(const QJsonObject&, QString*)`; `void CustomPanelHost::setFrame(const RenderFrame&)`; `bool TextPanel::setContent(const QString& markdown, const QJsonObject& scalarBindings, QString*)`. Version 1 custom panels use an allowlisted declarative ECharts option/graphic-primitive schema with column-role bindings. Built-in audited custom-series adapters are registered by ID. Arbitrary imported JavaScript is not part of v1; a future script runtime requires its own process watchdog/resource contract and explicit trusted activation.

- [ ] Add `custom_options_cannot_query_or_open_files`: declarations cannot issue network/file requests, execute regex, evaluate JS callbacks, invoke Commander or mutate source state. Allowed selection callback IDs can emit only validated `SelectionContext` through V03/V14.
- [ ] Add `custom_chart_rejects_unbounded_geometry`: declared primitives/data bindings respect panel/frame/mark/depth limits; unknown callback/property fails clearly; renderer failure falls back to typed data and retains the definition for repair. A custom Gantt/diamond drawing uses the same supplied rows/lineage as built-ins.
- [ ] Add `markdown_is_local_sanitized_and_accessible`: raw HTML/scripts/remote images are rejected or escaped by the declared safe subset; code fences/lists/links/text render natively; scalar bindings format typed values but cannot concatenate executable query text. Missing scalar is visibly missing, not zero.
- [ ] Implement custom editor with schema validation/preview/reset-to-example and read-only inspect of dataset shape. Text panels support investigation notes, run/build metadata, captions and declared scalar substitutions; they do not invent generated conclusions. Save/export includes content and custom schema version/assets, not hidden executable code.

**Parity:** `set/get_custom_panel_definition`, `set/get_text_panel_content` and `validate_panel` return the same structured validation errors as GUI. Markdown/plain-text state and custom fallback status are usable through CLI/MCP. Imported definitions remain inactive until validated; declarative supported panels can run through ordinary `run_chart`.

**Acceptance/rollback:** Users can explain dashboards and add specialized visuals without shipping a hidden query language or unbounded browser loop. Disable `custom` kind at runtime while native text/data panels remain usable; preserve definitions in exports.

**Primary references:** [OpenObserve custom-chart data/renderer split](https://openobserve.ai/docs/user-guide/analytics/dashboards/custom-charts/what-are-custom-charts/), [ECharts custom series](https://echarts.apache.org/en/feature.html), [Grafana canvas](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/canvas/). CILogg intentionally starts with a narrower declarative extension contract; it does not copy OpenObserve's SQL/JavaScript execution model.

## V17 — Add live/pause semantics, small multiples and comparison overlays

**Integrated hard dependencies:** G01, G02, G03, V01, V02, V14, V15, F11, I02. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Keep dashboards useful during capture and across runs while obeying the existing bounded service. Depends on V01/V02/V14/V15, F11 compatible-query reuse, typed time for event windows and the external comparison-service task for aligned/delta datasets. Does not invent a second run-comparison implementation.

**Files:** Proposed: `src/ui/include/dashboardrefreshcontroller.h`, `src/ui/src/dashboardrefreshcontroller.cpp`, `src/ui/include/facetpanel.h`, `src/ui/src/facetpanel.cpp`, `src/ui/include/comparisonpanel.h`, `src/ui/src/comparisonpanel.cpp`, `Resources/dashboard/panels/compare.js`, `tests/ui/dashboardrefreshcontroller_test.cpp`, `tests/ui/facetpanel_test.cpp`, `tests/ui/comparisonpanel_test.cpp`; verified source-close/capture anchors remain `crawlerwidget.cpp`/`tabbedcrawlerwidget.cpp` plus the existing job service.

**Interfaces:** `bool DashboardRefreshController::setPolicy(const QString& dashboardId, const QJsonObject& policy, QString*)`; `void DashboardRefreshController::pause(const QString& dashboardId)`; `void DashboardRefreshController::resume(const QString& dashboardId)`; `QJsonObject DashboardRefreshController::state(const QString& dashboardId) const`; `bool FacetPanel::setChildren(const QVector<PanelSpec>&, QString*)`; `bool ComparisonPanel::attach(const QString& comparisonDatasetJobId, const QJsonObject& mapping, QString*)`. `policy` declares snapshot/manual/periodic/follow mode, interval, rolling window/cap and event-time lateness policy. Incremental updates only use a shared aggregate's declared append/retract support; unsupported transforms rerun a bounded snapshot or remain manual with an explanation.

- [ ] Add `dashboard_refresh_coalesces_shared_work`: 16 panels with the same query/scope reuse a compatible F11 dataset; rapid refresh changes coalesce, two-running/eight-queued limits remain enforced and hidden tabs stop unnecessary paint/refresh. A slow query cannot accumulate an unbounded refresh backlog.
- [ ] Add `pause_freezes_display_and_resume_replaces_tail`: while capture appends, paused panel keeps its dataset/revision and shows newer data; resume processes a valid new snapshot, replaces provisional-tail contributions and expires rolling-window data exactly. Snapshot mode never silently becomes live.
- [ ] Add `live_limits_and_late_events_are_visible`: retention/time/cell cap reached returns bounded visible state; late/out-of-order events follow a declared shared-service policy, not browser arrival order. Source rewrite/close marks historical and stops follow; no stale automatic navigation.
- [ ] Add `facets_and_overlays_use_comparable_scales`: facet ordering/colors and common y/color scales are stable; unmatched categories remain missing; baseline/current overlay uses supplied alignment (event time, elapsed-from-anchor, numeric/ordinal) and declares the method. Delta/ratio shows base zero and incompatible units correctly; view reduction does not recompute comparison statistics.
- [ ] Implement play/pause/follow/manual refresh controls, last-good-result retention with age/error badge and per-panel override, plus small-multiple layout/collapsed facets. Comparison overlays/side-by-side/shared cursor consume the comparison service's joined datasets; each series retains independent source lineage.

**Parity:** `set/get_dashboard_refresh`, `pause/resume/refresh_dashboard`, chart `facet`/`comparison` settings and last update/next due/stale/newer-data state are explicit through GUI/CLI/MCP. Tests compare counts across append/pause/resume and invoke the same comparison dataset from each surface.

**Acceptance/rollback:** Capture/search remains responsive while dashboards update under fixed budgets; charts never imply a live result after an error. Disable automatic refresh independently, returning to manual frozen snapshots. Disable compare/facet wrappers without deleting their child panel/query definitions.

**Primary references:** [OpenObserve historical/realtime dashboards and comparison entry points](https://openobserve.ai/docs/user-guide/analytics/dashboards/), [Grafana query/transform lifecycle](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/query-transform-data/transform-data/), [ECharts rendering capabilities](https://echarts.apache.org/en/feature.html).

## V18 — Export reproducible images/data/reports and close release gates

**Integrated hard dependencies:** G01, G02, G03, V08, V15, V17. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose/dependencies:** Deliver PNG/SVG/CSV/JSON/PDF outputs with truthful scope and verify the complete dashboard path. Depends on the released V families, V08/V15/V17 and shared atomic export/file-destination validation. Export is a snapshot job with progress/cancellation when it spans panels/data pages; it cannot freeze the GUI or silently export only the loaded page.

**Files:** Verified anchors: `src/app/klogg.qrc`, `tests/mcp/analysis_parity.py`, `scripts/codex/run-tests.ps1`, root/UI/test CMake. Proposed: `src/analysis/include/dashboardexportjob.h`, `src/analysis/src/dashboardexportjob.cpp`, `src/ui/include/panelexportservice.h`, `src/ui/src/panelexportservice.cpp`, `src/ui/include/dashboardreport.h`, `src/ui/src/dashboardreport.cpp`, `tests/ui/panelexportservice_test.cpp`, `tests/ui/dashboardreport_test.cpp`, `tests/mcp/dashboard_parity.py`, `scripts/benchmarks/dashboard-benchmark.py`, `docs/dashboards.md`, `docs/dashboard-benchmarks.md`.

**Interfaces:** `QString submitDashboardExport(const DashboardSpec&, const QJsonObject& request, QString* error)` uses the shared job lifecycle; `QString PanelExportService::submit(const PanelSpec&, const QString& datasetJobId, const PanelExportRequest&, QString*)`. `PanelExportRequest` was declared in V01. Dashboard request declares dashboard revision, fixed dataset IDs/revisions, panel/tab selection, output format/path, page size/orientation/theme, raw/display data choice, `allowPartial` and included provenance. Native image export uses painter/QImage and version-compatible Qt SVG/PDF APIs when built; optional modules are discovered explicitly. Rich renderer exports only after a settled-frame acknowledgement with matching revisions. SVG export is real vector output where supported; an embedded raster is labeled as such, never falsely advertised as vector.

- [ ] Add `export_rows_are_not_only_visible_page`: >200-row fixture CSV/JSON covers every retained result page in frozen order, quotes Unicode/delimiters/newlines and preserves tagged precision/missing/null. Full underlying-event export is a separate shared query/export job, not an implied expansion of sampled marks. `allowPartial=false` rejects incomplete input; true records reasons and scope.
- [ ] Add `image_and_pdf_freeze_all_revisions`: chart updates during export do not mix frames; PNG/SVG includes title/units/domain/time range/source manifest/query hash/partial+reduction status. PDF report includes chosen dashboard tabs/panels, readable legends and a dataset/query/provenance appendix; inaccessible rich-only drawing gets an explicit fallback table page. Validate page bounds with render-and-inspect, not a PDF file-exists assertion.
- [ ] Add `export_is_atomic_cancellable_and_not_source_file`: same-source destination, missing permissions, failed writer and cancellation leave source/destination intact with no published partial file. Oversized dimensions/page counts fail before allocating; spreadsheet-safe CSV is explicit while raw export preserves source values.
- [ ] Add `dashboard_release_parity_matrix`: the same deterministic fixture definitions produce matching schemas/values/lineage/errors via direct service, native/rich GUI, CLI and native MCP; cover malformed spec, absent WebEngine, retained-job expiry, close/rewrite, provisional append, partial limits, non-ASCII paths, accelerated/Qt-only capture and multiwindow addressing.
- [ ] Record actual cold/warm load, p50/p95 interactive latency, cancellation acknowledgement, throughput, peak RSS, bridge bytes/paint work, 16-panel shared-query and high-cardinality cases on named machines/SDKs. Compare feature-disabled native baseline and rich-enabled idle overhead. Calibrate and freeze the roadmap’s proposed P00 gates (p95 interactive event latency below 100 ms and local cancellation acknowledgement within 1 second) before claiming pass; report cancellation during stalled OS reads separately. Do not cite ECharts vendor large-data examples as CILogg performance evidence.

**Parity:** Add `export_panel`, `export_dashboard_report`, `export_chart_data` with structured output file metadata/format/revisions/scope; reuse job get/results/cancel/release. Supply documentation examples using `cilogg command --action export_panel --json-file export.json` and the same discovered action through `cilogg_execute`. Extend packaged parity harness only with explicit new flags; do not assume an unimplemented `--dashboard` switch already exists.

**Acceptance/rollback:** Exported values reconcile to inspect view; native Qt5/Qt6 builds pass on supported OSes, rich packages pass their optional matrix, docs include all commands and measured limits. Image/PDF rendering issues can disable that format independently of CSV/JSON and dashboards. Keep saved schema/readers and the last compatible native export path when backing out a renderer.

**Primary references:** [Grafana inspector/export scope](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/panel-inspector/), [ECharts Canvas/SVG rendering](https://echarts.apache.org/en/feature.html), [Qt versioned WebEngine deployment architecture](https://doc.qt.io/qt-6.10/qtwebengine-overview.html).

## Developer-domain acceptance fixtures

Use these as tiny deterministic schema/query/dashboard examples, not separate product modes. Every template is editable and maps to the same generic contracts; none is required to use every advanced chart.

| Developer workflow | Example selected fields / input | Useful panels | Evidence/drilldown gate |
|---|---|---|---|
| Backend/API | timestamp, route, status, duration, request/span IDs | Rate, percentile band, TopN errors, histogram, request waterfall | Route/status/time bucket opens exact requests; spans require IDs/start/end |
| Web/frontend | navigation ID, page, event, duration, payload size | Page latency line, payload scatter, event lanes, explicit navigation funnel | Funnel counts unique declared navigation cohorts, not raw event frequency |
| Desktop | thread, task, state, elapsed time, exception | Worker state/history, duration boxplot, event lanes | Observations and duration assumptions are visibly separate |
| Mobile | session, app state, network request, battery/memory, device clock | Lifecycle timeline, latency heatmap, memory trend, request comparison | Device uptime and wall-clock domains cannot align by accident |
| DevOps/platform | deployment ID, pod/service, severity, count, timestamp | Error-rate line, deploy markers, status matrix, explicit relationship graph | Deployment annotation preserves time and source; graph edges are explicit |
| QA/test | run/suite/test IDs, result, duration, worker | Pass/fail bars, duration boxplot, test waterfall, run overlay | Stable test keys/missing tests come from comparison service; source rows agree |
| Data/ETL | job/stage/partition, rows/bytes, duration, status | Throughput, stage waterfall, partition skew bars, explicit stage flow | Units/denominators are declared; flow volume does not imply unique records |
| Game/graphics | frame ID, timestamp, CPU/GPU/job durations, allocation | Frame-time bands, scatter, job lanes, true stack profile | Frame ordinal differs from elapsed time; flamegraph needs real profile input |
| Embedded/firmware | uptime/reset generation, task/state, RSSI, heap/temp | Line/step, state timeline, numeric histogram, equipment overlay | Reset/clock domains and physical source provenance survive every selection |

## Final self-review and handoff checks

- [ ] Every chart in the requested catalogue maps to V02–V13/V16/V17 and has an explicit dataset shape, native/rich/fallback behavior and owner. V14 interactions, V15 authoring/inspect/save, V17 live/compare and V18 export/parity apply consistently.
- [ ] Confirm the minimal DAG remains F01 Core-only + merged P03/P04 → V01 → V02; no F07/F11, logformat, typed time or WebEngine dependency may creep into that release. Later typed-query adapters preserve the dataset/rendering boundary.
- [ ] Match final F01/F07/F09/F11/F14 type definitions exactly. Remove any accidental duplicate query/aggregate/parser implementation or second scheduling limit from a visualization PR. Add any advanced operator only through the versioned shared transform registry.
- [ ] Inspect full source before replacing a proposed path or changing an existing symbol; preserve source identity, raw text, Regex Lab/P05 behavior, existing command discovery and Qt5/Qt6 compatibility. Recheck the P05 merge status rather than treating its branch files as master.
- [ ] Verify schemas list every setting and state that the GUI exposes, including layout/tabs/variables/annotations/pause/custom content/export. A chart that can only be controlled by coordinates is unfinished.
- [ ] Make failed/partial/historical/expired/native-fallback states clear in panel, inspect view, saved report and automation. No estimate, capped sample or interpolated duration may be silently presented as exact complete evidence.

This section is ready for interface/path reconciliation with the parent data, shell, comparison and automation plans. It specifies future work only; no repository implementation, remote write, deployment or performance result is implied.
