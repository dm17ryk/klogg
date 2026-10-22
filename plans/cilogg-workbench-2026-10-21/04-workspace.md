# Workspace and User Experience Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make advanced capabilities discoverable through a consistent, readable developer workflow.

**Architecture:** Extend the existing CILogg services through the canonical contracts in the product specification. Existing filenames are anchors; proposed filenames are implementation targets to reconcile at G01.

**Tech Stack:** C++17, Qt5/Qt6, existing Catch2/QtTest, Commander/CLI/MCP and Python runtime.

**Spec:** `00-product-spec.md` and `MASTER_GUIDE.md`.

## Global Constraints

Apply every exact constraint in `00-product-spec.md`. Preserve current analysis bounds, C++17/Qt5/Qt6, stable source/config identities, headless parity, and out-of-tree builds.

## Review Focus

Exercise stale source/state; malformed/large input; cancellation/backpressure; restored or unavailable capabilities; and GUI/headless disagreement in the named task cases below. The global Review Focus ownership matrix is in the master execution guide.

---

### U01 — Introduce a coherent Qt design system

**Integrated hard dependencies:** G01, G02, G03. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Consistent readable controls, icons, spacing, themes and status semantics throughout the workbench.

**Depends on:** G01. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Modify existing `src/ui/CMakeLists.txt`, `src/ui/src/mainwindow.cpp` and existing resource/theme entry points discovered at G01.
- Create proposed `src/ui/include/workbenchtheme.h`, `src/ui/src/workbenchtheme.cpp`, `Resources/workbench/` and theme guidance.
- Test: `tests/ui/workbench_u01_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `WorkbenchTheme::apply(QApplication&, const QString& themeId) -> bool`; theme IDs `system`, `light`, `dark`.
- `WorkbenchTheme::tokens() -> QJsonObject` exports shared colors/font/spacing and semantic severity/completeness styles to optional web panels.

**Required behavior:**
- Use system font/scale defaults and semantic palette tokens; retain user font/color preferences. High contrast and color-vision differences require symbols/text as well as color.
- Scope theme changes to presentation; keep startup usable before optional panels initialize. Reuse a consistent icon family with accessible labels and translated strings.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_u01_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][U01]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - theme_change_preserves_source_and_selection: apply all three themes; assert source UUID, selected physical lines, and search remain unchanged.
  - dpi_palette_controls: inspect standard and custom widgets at 100/150/200 percent; no clipped labels or unreachable primary actions; high-contrast states have text/symbol equivalents.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][U01]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `U01` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `set_workbench_theme`, `get_workbench_theme`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://doc.qt.io/qt-6.10/qaccessiblewidget.html

### U02 — Create the welcoming open/connect/run workflow

**Integrated hard dependencies:** G01, G02, G03, U01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** A new developer gets useful logs without mandatory project setup.

**Depends on:** U01. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Modify existing `src/ui/src/mainwindow.cpp`, `src/ui/src/streamsourceregistry.cpp` and plugin UI registration.
- Create proposed `src/ui/include/workbenchstartwidget.h`, `src/ui/src/workbenchstartwidget.cpp` and small bundled demo manifests.
- Test: `tests/ui/workbench_u02_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `WorkbenchStartController::open(const QJsonObject& request) -> QJsonObject`; mode is `file|folder|connect|run|sample`.
- Source choices come from existing registry/plugin capabilities; return existing stable tab/source identifiers.

**Required behavior:**
- Offer Open File, Open Folder, Connect Source, Run and Capture, recent investigations, and examples with concise explanations. File drop still opens directly.
- Show source capabilities, dependencies and setup errors inline; unavailable Docker/ADB/SSH tools do not block ordinary file use.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_u02_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][U02]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - first_launch_no_project: fresh settings + dropped UTF-8 file opens searchable viewer with no project creation.
  - source_choice_failure: unavailable connector displays install/configure guidance and preserves existing tabs; sample opens without network.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][U02]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `U02` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `workbench_open`, `workbench_list_sources`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/streamsourceregistry.cpp

### U03 — Add the investigation workspace shell

**Integrated hard dependencies:** G01, G02, G03, U01, F01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Sources, events, fields, details, analysis and automation share a coherent workspace.

**Depends on:** U01, F01. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Modify existing `src/ui/src/mainwindow.cpp`, `src/ui/src/tabbedcrawlerwidget.cpp`, `src/ui/src/mainwindowproject.cpp`, `src/ui/include/sessioninfo.h`.
- Create proposed `src/ui/include/investigationworkspace.h`, `src/ui/src/investigationworkspace.cpp`.
- Test: `tests/ui/workbench_u03_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `InvestigationWorkspace::snapshot() const -> QJsonObject`; `restore(const QJsonObject&) -> QJsonObject`.
- Workspace state references stable window/tab/source IDs and shared SelectionContext, never tab indices or displayed row numbers.

**Required behavior:**
- Use resizable dock/pane layouts with one-click return to raw logs. Keep source list, selected event, shared filters and active job state understandable.
- Version layout migration and restore missing/closed sources as explicit unavailable entries. Restore definitions without restarting captures/actions.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_u03_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][U03]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - restore_missing_sources: round-trip old/new project, removed source and disabled renderer; assert readable layout and zero started processes/transmissions.
  - multiwindow_move: move/reorder source tabs; raw and chart links still target the same source UUID; independent follow/pause states survive.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][U03]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `U03` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `workspace_get`, `workspace_set`, `workspace_restore`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/mainwindowproject.cpp

### U04 — Build the structured event and exception inspector

**Integrated hard dependencies:** G01, G02, G03, F04, F07, U03. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Readable event detail and surrounding context alongside the original data.

**Depends on:** F04, F07, U03. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Modify existing `src/ui/src/crawlerwidget.cpp` and source navigation wiring.
- Create proposed `src/ui/include/eventinspectordock.h`, `src/ui/src/eventinspectordock.cpp`, `src/ui/src/eventtablemodel.cpp`.
- Test: `tests/ui/workbench_u04_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `EventInspectorController::inspect(const EventRef&, const QJsonObject& options) -> QJsonObject`; consumes F event/page APIs.
- Modes: `raw|decoded|structured`; surrounding context is an explicit bounded source/query range.

**Required behavior:**
- Render nested JSON lazily, multiline stack traces with cause chains, typed attributes/units, parse errors and source metadata. Expand/collapse never changes event identity.
- Provide copy raw/copy displayed/copy selected fields and open-source actions. Keep full uint64 values and byte/character offset units visible where relevant.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_u04_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][U04]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - inspector_multiline_navigation: expand exception and open middle frame; assert correct physical source lines after sorting/filtering.
  - raw_copy_fidelity: Unicode, CRLF, embedded escapes and 18446744073709551615 survive structured display and raw export without numeric rounding or transformed-byte substitution.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][U04]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `U04` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `inspect_event`, `get_event_context`, `copy_event_fields`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/crawlerwidget.cpp

### U05 — Add facets and a visual field-query builder

**Integrated hard dependencies:** G01, G02, G03, F07, F08, F09, U04, V01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Filter and visualize fields without requiring a query language.

**Depends on:** F07, F08, F09, U04, V01. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `src/ui/include/fieldfacetsdock.h`, `src/ui/src/fieldfacetsdock.cpp`, `src/ui/src/querybuilderwidget.cpp`.
- Modify source context menus and shared command-schema registration, plus existing `src/ui/CMakeLists.txt`.
- Test: `tests/ui/workbench_u05_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `QueryBuilderController::compile(const QJsonObject& visualDefinition) -> QuerySpec`; `describe(const QuerySpec&) -> QJsonObject`.
- Field menus emit the same QuerySpec and panel request consumed by V02; include/exclude/exists/missing are separate operators.

**Required behavior:**
- Show types, units, top values, null/missing coverage and bounded facet counts. Add reversible filter chips and query history with undo/redo.
- Provide contextual actions Visualize, Group By, Statistics and Compare. Text query editing and visual builder report unsupported round-trip portions without silently changing the query.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_u05_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][U05]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - builder_equivalence: visual duration>100 and level=ERROR matches the same EventRefs as the textual AST and headless command.
  - facet_partial_scope: cap/high-cardinality results show coverage and Other; selecting missing differs from empty text and null; undo restores the exact previous query.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][U05]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `U05` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `query_build`, `query_explain`, `facets_get`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://www.elastic.co/docs/explore-analyze/visualize/lens

### U06 — Retain independent searches and investigation checkpoints

**Integrated hard dependencies:** G01, G02, G03, F01, U03. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Keep several search results and return to exactly what was investigated.

**Depends on:** F01, U03. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Modify existing `src/ui/include/savedsearches.h`, `src/ui/src/savedsearches.cpp` and search-tab wiring.
- Create proposed `src/ui/src/retainedsearchmodel.cpp` and versioned retained-search schema.
- Test: `tests/ui/workbench_u06_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `RetainedSearchService::retain(const QJsonObject& request) -> QJsonObject`; `list() const -> QJsonArray`; `release(const QString& id) -> bool`.

**Required behavior:**
- Retain query, scope, source generations, range, name/color and status. Independent results do not mutate when another tab searches or a source appends.
- Expose re-run/current versus historical states and explicit result release. Preserve prior saved-search IDs and settings migration.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_u06_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][U06]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - retained_independence: two queries retain different results; editing/re-running one leaves the other unchanged.
  - retained_stale_source: replace/redecode file; old result is historical and source navigation rejects stale provenance.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][U06]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `U06` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `retain_search`, `list_retained_searches`, `release_retained_search`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/savedsearches.cpp

### U07 — Add reusable filter groups and virtual streams

**Integrated hard dependencies:** G01, G02, G03, U05, U06. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Organize filters as reusable investigations across sources.

**Depends on:** U05, U06. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Modify existing `src/ui/src/predefinedfilters.cpp` and filter persistence/config import paths.
- Create proposed `src/ui/src/filtergroupmodel.cpp` and filter-group schema.
- Test: `tests/ui/workbench_u07_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `FilterGroupService::execute(const QString& operation, const QJsonObject& arguments) -> QJsonObject`; operations `list|create|update|move|apply|import|export`.

**Required behavior:**
- Use stable IDs, named groups, enable/disable, ordering and explicit source selection. A virtual stream is a saved filter over sources, without copying raw data.
- Import handles ID collisions and two-window edits predictably; preserve existing flat filter definitions and regex flags.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_u07_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][U07]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - filter_group_migration: import/rename/move existing filters; original IDs and expressions remain identical.
  - virtual_stream_provenance: group across two files returns exact EventRefs and visible per-source failures; disable only affects intended workspace.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][U07]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `U07` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `filter_groups`, `virtual_streams`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://go2docs.graylog.org/current/making_sense_of_your_log_data/streams.html

### U08 — Add persistent tab/source groups

**Integrated hard dependencies:** G01, G02, G03, U03. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Manage many files, captures and investigations without losing their state.

**Depends on:** U03. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Modify existing `src/ui/src/tabbedcrawlerwidget.cpp`, session serialization and existing tab-bar ownership.
- Create proposed `src/ui/src/tabgroupmodel.cpp`.
- Test: `tests/ui/workbench_u08_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `TabGroupService::execute(const QString& operation, const QJsonObject&) -> QJsonObject`; use existing tab/window IDs.

**Required behavior:**
- Name/color/collapse/reorder/move/ungroup source tabs and restore groups. Keep selected tab, independent follow/search state, capture indicators and close behavior.
- A collapsed group can still expose failing/disconnected sources and active jobs; closing a group uses existing per-source cleanup.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_u08_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][U08]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - tab_group_restore: reorder and move tabs between windows; restore IDs, selection, colors and independent follow states.
  - hidden_capture_cleanup: collapse group then close source with queued analysis and a running script; no leaked lease or orphan process.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][U08]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `U08` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `tab_groups`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/tabbedcrawlerwidget.cpp

### U09 — Finish visible QuickFind controls and history

**Integrated hard dependencies:** G01, G02, G03, U01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Discoverable literal/regex/whole-word search with consistent history.

**Depends on:** U01. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Modify existing QuickFind widget/pattern/history files identified in prior P19a; preserve current key bindings.
- Test: `tests/ui/workbench_u09_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- Extend the existing QuickFind command settings/state with `regex`, `wholeWord` and history operations; do not add a second search backend.

**Required behavior:**
- Show explicit switches and a history dropdown, correct literal escaping and saved flags, concise validation messages and keyboard focus behavior.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_u09_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][U09]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - quickfind_history_flags: restore literal text containing .* and Unicode whole-word search; flags and match locations remain consistent.
  - quickfind_keyboard: keyboard-only history selection and next/previous actions work in raw and filtered viewers.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][U09]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `U09` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `get_quickfind`, `set_quickfind`, `quickfind_history`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/plans/log-analysis-roadmap.md

### U10 — Add a command palette from existing action metadata

**Integrated hard dependencies:** G01, G02, G03, U01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** One searchable keyboard entry point for workbench capabilities.

**Depends on:** U01, G03. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Modify QAction/Commander metadata exposure and MainWindow action registration.
- Create proposed `src/ui/include/commandpalettedialog.h`, `src/ui/src/commandpalettedialog.cpp`.
- Test: `tests/ui/workbench_u10_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `CommandPaletteModel::entries(const QJsonObject& context) -> QJsonArray`; derive entries from existing registered actions/command schemas.

**Required behavior:**
- Display action title, category, shortcut and enabled reason; execute through existing dispatch. Support fuzzy text filtering and stable automation IDs.
- Keep context-sensitive actions disabled when sources/capabilities are unavailable; resolve translated duplicate titles using stable IDs.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_u10_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][U10]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - palette_dispatch_parity: palette and CLI invoke the same action/arguments and produce equivalent state.
  - palette_stale_context: close source with palette open; executing a formerly enabled action fails readably without dereferencing stale objects.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][U10]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `U10` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `list_palette_actions`, `execute_palette_action`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/include/commander.h

### U11 — Save annotated investigations and notebook-style findings

**Integrated hard dependencies:** G01, G02, G03, U03, U06, V01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Reproducible debugging context with notes, selected evidence and reusable views.

**Depends on:** U03, U06, V01. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Modify existing `src/ui/src/mainwindowproject.cpp` and project serialization.
- Create proposed `src/ui/src/investigationdocument.cpp`, `src/ui/src/findingsdock.cpp`, `Resources/schemas/investigation-v1.json`.
- Test: `tests/ui/workbench_u11_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `InvestigationDocumentService::execute(const QString& operation, const QJsonObject&) -> QJsonObject`; operations `create|snapshot|restore|annotate|list|export`.

**Required behavior:**
- Store source references/manifests, extraction and query revisions, selection/time context, panel definitions/layouts, bookmarks, annotations and Markdown notes with stable IDs.
- Support a finding/checkpoint with before/after view state and source examples; reopening unavailable evidence shows the limitation. Preserve investigation context while broadening/resetting filters.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_u11_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][U11]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - finding_roundtrip: save chart selection+notes+raw examples, change workspace, restore; exact sources/query/time and completion state return.
  - project_migration_and_atomicity: malformed/old schema, interrupted save and missing sources recover without losing last valid project or running actions.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][U11]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `U11` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `investigation_create`, `investigation_save`, `investigation_open`, `finding_add`, `annotation_update`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://docs.dynatrace.com/docs/analyze-explore-automate/logs/lma-logs-app/problem-mode

### U12 — Add checkpointed ANSI rendering with raw/display maps

**Integrated hard dependencies:** G01, G02, G03, F01, U04. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Readable colored developer output with faithful search, copy and navigation.

**Depends on:** F01, U04. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Modify existing `src/ui/src/abstractlogview.cpp`, rendering and selection mapping.
- Create proposed `src/ui/src/ansicolorprocessor.cpp`, `src/ui/include/ansicolormap.h`; register tests alongside wrapped-selection fixtures.
- Test: `tests/ui/workbench_u12_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `AnsiColorProcessor::consume(const QByteArray&, bool endOfSource) -> QJsonObject`; output bounded style spans, raw/display offset mapping and checkpoint state.

**Required behavior:**
- Support SGR basic/bright/256/true color, split escape sequences and checkpointed state reconstruction. Unsupported controls are consumed or visibly escaped according to documented mode.
- Keep raw search and raw copy intact; clean-text copy/export is explicit. Preserve selection/highlight readability and Value Names/span mappings across wrapping.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_u12_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][U12]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - ansi_split_and_scrolled: split every escape boundary and scroll to checkpoint; styles and raw/display spans match full decode.
  - ansi_wrapped_copy: Unicode, ANSI and translated display values across discontiguous selections preserve raw copy exactly; long malformed escapes remain bounded.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][U12]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `U12` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `get_ansi_display`, `set_ansi_display`, `export_display_text`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/abstractlogview.cpp

### U13 — Polish onboarding, accessibility and progressive disclosure

**Integrated hard dependencies:** G01, G02, G03, U02, U04, U05, U10, V15, A06. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** The full tool remains approachable and operable without a mouse.

**Depends on:** U02, U04, U05, U10, V15, A06. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Modify workbench widgets/resources/translations and existing `tests/ui/uiautomation_test.cpp`.
- Create proposed `docs/workbench/quickstart.md`, `test_data/workbench/onboarding/` and reusable UI acceptance scenarios.
- Test: `tests/ui/workbench_u13_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- Expose stable `objectName`, `accessibleName`, keyboard actions and serializable UI state for all introduced workflows; use Qt accessibility interfaces for custom panes.

**Required behavior:**
- Offer compact/simple defaults and expandable advanced settings; explain aggregation/scope/units and errors near controls. Preserve user choices and avoid forced onboarding on ordinary file opening.
- Include guided examples for web requests, application exceptions, build/test logs, ETL, game frames, containers, mobile output and protocol communication. Explain unsupported chart data and partial results with corrective actions.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_u13_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][U13]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - first_chart_keyboard: fresh user opens bundled JSON sample, chooses duration distribution, filters bucket and opens source using keyboard and accessible controls.
  - accessibility_density: translated labels, 200 percent DPI and high-contrast colors preserve primary actions; raw/log table remains available when rich rendering fails.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][U13]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `U13` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `workbench_help`, `list_examples`, `open_example`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://doc.qt.io/qt-6.10/qaccessiblewidget.html
- https://www.elastic.co/docs/explore-analyze/visualize/lens
