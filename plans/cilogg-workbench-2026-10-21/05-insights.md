# Patterns, Comparisons and Correlation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn shared event/query datasets into explainable investigations across developer domains.

**Architecture:** Extend the existing CILogg services through the canonical contracts in the product specification. Existing filenames are anchors; proposed filenames are implementation targets to reconcile at G01.

**Tech Stack:** C++17, Qt5/Qt6, existing Catch2/QtTest, Commander/CLI/MCP and Python runtime.

**Spec:** `00-product-spec.md` and `MASTER_GUIDE.md`.

## Global Constraints

Apply every exact constraint in `00-product-spec.md`. Preserve current analysis bounds, C++17/Qt5/Qt6, stable source/config identities, headless parity, and out-of-tree builds.

## Review Focus

Exercise stale source/state; malformed/large input; cancellation/backpressure; restored or unavailable capabilities; and GUI/headless disagreement in the named task cases below. The global Review Focus ownership matrix is in the master execution guide.

---

### I01 — Implement explainable message patterns and rarity

**Integrated hard dependencies:** G01, G02, G03, F03, F07, F08, F09. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Reduce repeated messages while preserving meaningful variable fields and source examples.

**Depends on:** F03, F07, F08, F09. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `src/analysis/include/patternanalysis.h`, `src/analysis/src/patternanalysis.cpp`, `Resources/schemas/pattern-analysis-v1.json`.
- Add proposed `src/ui/src/patternsdock.cpp` and corresponding command handlers.
- Test: `tests/ui/workbench_i01_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `PatternAnalysisService::submit(const QJsonObject& request, QString* error) -> QString`; returns existing analysis job ID.
- Request: `query`, `maskRules`, `maxPatterns`, `minCount`; rows include stable template ID, count/share, first/last occurrence, examples, token distributions and lineage.

**Required behavior:**
- Start deterministic configurable masking for dates/UUIDs/addresses/numeric fields; keep error/status codes unless the configured rule masks them. Exact dedup and similarity grouping are distinct operations.
- Allow split/merge/promote/ignore overrides as versioned profile rules; show unmatched events, rare templates and query-scoped frequency trends. Counts and coverage stay exact or explicitly partial.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_i01_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][I01]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - patterns_keep_semantics: two request IDs group; status 200 and 500 remain separate under the default profile; examples resolve to their own raw events.
  - patterns_capped_and_new: more unique templates than cap yields partial reason; a newly introduced rare template appears and is not silently lost to a sample.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][I01]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `I01` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `patterns_analyze`, `patterns_profile`, `patterns_examples`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://docs.newrelic.com/docs/logs/ui-data/find-unusual-logs-log-patterns/
- https://www.sumologic.com/help/docs/search/behavior-insights/logreduce/

### I02 — Compare files, runs, periods and distributions

**Integrated hard dependencies:** G01, G02, G03, I01, F09, F14. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** A reusable comparison dataset explains regressions and changed behavior.

**Depends on:** I01, F09, F14. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `src/analysis/include/runcomparison.h`, `src/analysis/src/runcomparison.cpp`, `Resources/schemas/run-comparison-v1.json`.
- Add proposed comparison controller/dock using shared F/V datasets.
- Test: `tests/ui/workbench_i02_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `RunComparisonService::submit(const QJsonObject& request, QString* error) -> QString`; request has `baseline`, `target`, `alignment`, `normalization`, `metrics`.

**Required behavior:**
- Support wall-time, elapsed start, selected marker and explicit offsets; preserve separate clocks. Compare template/category counts and shares, new/gone messages, rates, quantiles, distributions and per-field statistics.
- Report baseline=0 as new/undefined-relative-change with explicit values; do not serialize infinity/NaN as ordinary JSON numbers. Keep examples and lineage for both sides, unequal durations, and partial completeness.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_i02_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][I02]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - compare_unequal_runs: baseline 100 events/10 seconds, target 200/20 seconds gives equal rate despite doubled count; new and gone signatures are correct.
  - compare_alignment_and_zero: align different wall-clock starts to selected event; missing markers fail explicitly; zero baseline has no fabricated finite percentage; both-side source links validate.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][I02]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `I02` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `compare_runs`, `get_run_comparison`, `compare_examples`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://www.sumologic.com/help/docs/search/behavior-insights/logcompare/
- https://help.splunk.com/en/splunk-cloud-platform/search/spl2-search-reference/timewrap-command/timewrap-command-overview-syntax-and-usage

### I03 — Group exceptions and navigate stack traces

**Integrated hard dependencies:** G01, G02, G03, F04, I01, U04. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** See recurring failures across runtimes and inspect representative cause chains.

**Depends on:** F04, I01, U04. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `src/analysis/src/exceptionanalysis.cpp`, `src/analysis/include/exceptionanalysis.h` and versioned runtime exception profiles.
- Extend proposed patterns/event inspector rather than copying event rows into a second store.
- Test: `tests/ui/workbench_i03_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `ExceptionAnalysisService::submit(const QJsonObject&, QString* error) -> QString`; group fingerprint uses configured exception type/message/frame policy.

**Required behavior:**
- Provide Java, .NET, Python and JavaScript profiles, nested/caused-by chains, first/last occurrence, counts, affected sources and new-since-baseline state.
- Source frames may carry file/line references for configured IDE links; unresolved paths remain inspectable text. Never assert causal root cause solely from grouping.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_i03_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][I03]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - exception_languages: fixtures with cause chains and repeated stack frames produce correct physical event spans and configured stable grouping.
  - exception_fingerprint_policy: line-number-only changes group under the chosen policy while different failure types remain separate; representative events still navigate correctly.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][I03]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `I03` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `exceptions_analyze`, `exception_examples`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://docs.logz.io/docs/user-guide/log-management/insights/exceptions/

### I04 — Correlate requests, sessions and transactions

**Integrated hard dependencies:** G01, G02, G03, F05, F06, F07, F08. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Follow an operation across sources and calculate duration/outcome with visible unmatched events.

**Depends on:** F05, F06, F07, F08. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `src/analysis/include/transactionanalysis.h`, `src/analysis/src/transactionanalysis.cpp`, `Resources/schemas/transaction-v1.json`.
- Expose datasets to V timelines/graphs and A assertions.
- Test: `tests/ui/workbench_i04_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `TransactionAnalysisService::submit(const QJsonObject&, QString* error) -> QString`; request fields: keys, start/end predicates, maximum span/gap, time basis, sources and late-event policy.

**Required behavior:**
- Group explicit correlation IDs or configured start/end rules; compute duration/count/maximum severity/outcome, keep orphan starts/ends, duplicate IDs and late events visible.
- Label inferred transaction relationships; real trace ID/span/parent/duration imports retain their own semantics. Bound active keys and retained examples and expose overflow.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_i04_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][I04]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - transaction_out_of_order: delayed end event, duplicate request ID and orphan response yield deterministic groups and statuses under documented policy.
  - transaction_clock_reset: unrelated clock domains are not joined without configured alignment; cancellation clears bounded pending keys.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][I04]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `I04` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `transactions_analyze`, `transaction_events`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://docs.datadoghq.com/logs/explorer/analytics/transactions/

### I05 — Add explainable baselines, anomalies and forecast experiments

**Integrated hard dependencies:** G01, G02, G03, F09, F14, I02. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Highlight unusual numeric behavior and make optional predictions inspectable.

**Depends on:** F09, F14, I02. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `src/analysis/include/baselineanalysis.h`, `src/analysis/src/baselineanalysis.cpp`, `Resources/schemas/baseline-v1.json`.
- Register transforms in the F14 registry and expose annotated datasets to V/A.
- Test: `tests/ui/workbench_i05_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `BaselineAnalysisService::submit(const QJsonObject&, QString* error) -> QString`; methods `rolling_mean`, `ewma`, `median_mad`, `seasonal_baseline`, `linear_forecast`; all emit method/parameters/training window.

**Required behavior:**
- Define warm-up, empty buckets, denominator, window alignment and robust outlier scores. Explain each flagged interval with observed value and baseline band.
- Forecasting is explicit, uses ordered regular time data, separates training/holdout/future ranges, and reports error/assumptions; suppress unsupported intervals rather than claiming general prediction quality.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_i05_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][I05]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - anomaly_missing_zero: a zero-valued observation and absent bucket have different results; warm-up never flags an untrained baseline.
  - forecast_no_leakage: held-out target points do not affect fitted training coefficients; irregular/insufficient samples return a clear unsupported/insufficient-data result.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][I05]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `I05` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `baseline_analyze`, `forecast_analyze`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://www.sumologic.com/help/docs/search/search-cheat-sheets/log-operators/
