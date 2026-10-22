# Developer Automation and CI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unify existing actions, Python scenarios and new analysis rules into reproducible workflows.

**Architecture:** Extend the existing CILogg services through the canonical contracts in the product specification. Existing filenames are anchors; proposed filenames are implementation targets to reconcile at G01.

**Tech Stack:** C++17, Qt5/Qt6, existing Catch2/QtTest, Commander/CLI/MCP and Python runtime.

**Spec:** `00-product-spec.md` and `MASTER_GUIDE.md`.

## Global Constraints

Apply every exact constraint in `00-product-spec.md`. Preserve current analysis bounds, C++17/Qt5/Qt6, stable source/config identities, headless parity, and out-of-tree builds.

## Review Focus

Exercise stale source/state; malformed/large input; cancellation/backpressure; restored or unavailable capabilities; and GUI/headless disagreement in the named task cases below. The global Review Focus ownership matrix is in the master execution guide.

---

### A01 — Define workflows as adapters over existing actions and scenarios

**Integrated hard dependencies:** G01, G02, G03, F01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** One versioned automation definition coordinates capture, analysis, assertions and existing actions.

**Depends on:** G01, F01. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Modify existing `src/ui/include/actionsconfig.h`, `src/ui/src/actionsconfigparser.cpp`, `src/ui/src/scenariorunner.cpp`, `src/ui/src/scriptsupervisor.cpp` only through localized adapters.
- Create proposed `src/automation/include/automationtypes.h`, `src/automation/src/workflowdefinition.cpp`, `src/app/workbenchautomationadapter.cpp`, `Resources/schemas/workflow-v1.json` and module CMake.
- Test: `tests/ui/workbench_a01_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `WorkflowDefinitionService::validate(const QJsonObject&) -> QJsonObject`; `migrate(const QJsonObject&) -> QJsonObject`.
- Define `AutomationClock` with `nowNanos() const -> qint64`; modes `observed|event|virtual`. Define `OperationDispatcher = std::function<QJsonObject(const QString&,const QJsonObject&)>` in the core module; the app adapter maps it to existing Commander requests/results.

**Required behavior:**
- Definitions contain schemaVersion, stable IDs, variables, typed inputs, bounded steps/branches/repeats, triggers, timeout, error policy and artifact outputs. Core automation depends on QtCore/data contracts; app adapters own UI/service dispatch to avoid UI-library cycles.
- Preserve old action/response/scenario definitions and native Python syntax. Migrating or opening a definition validates and previews it; activation is a separate explicit operation.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_a01_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][A01]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - workflow_legacy_roundtrip: old typed parameters, automatic replies and repeat settings retain existing behavior and IDs after migration.
  - workflow_invalid_graph: unknown operation, unbounded cycle, duplicate step ID and invalid value types return field-specific errors before any action is dispatched.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][A01]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `A01` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `workflow_validate`, `workflow_migrate`, `workflow_describe`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/include/actionsconfig.h
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/include/scenariorunner.h
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/include/scriptsupervisor.h

### A02 — Add bounded conditions, windows and missing-event rules

**Integrated hard dependencies:** G01, G02, G03, A01, F08, F09. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Use log and communication evidence to detect meaningful live or replay conditions.

**Depends on:** A01, F08, F09. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `src/automation/include/ruleengine.h`, `src/automation/src/ruleengine.cpp`, `Resources/schemas/rule-v1.json`.
- Adapt existing auto-response event publication through `src/app/workbenchautomationadapter.cpp`; D byte matches remain their own explicit dialect.
- Test: `tests/ui/workbench_a02_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `RuleEngine::configure(const QJsonObject&) -> QJsonObject`; `consume(const QJsonObject& event) -> QJsonArray`; `advance(qint64 clockNanos) -> QJsonArray`; `reset() -> void`.

**Required behavior:**
- Support count/rate/threshold, sustained condition, sequence A then B, absence/heartbeat, new pattern and AND/OR correlation. Define observation/event/virtual clocks, late-event policy, per-key state, debounce, cooldown, hysteresis and reset-on-reload.
- Actions include bookmark/annotation, snapshot, desktop notification, script and workflow dispatch; external notifications can be configured as plugin actions. Limit active correlation keys and queued actions; report overflow and incomplete input. Tag generated events with rule/run ancestry and suppress configured recursion/duplicate action IDs.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_a02_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][A02]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - rule_windows_and_absence: virtual-clock boundary cases fire exactly once; late-event policy and missing input are distinguishable from zero matching events.
  - rule_feedback_backpressure: a TX-triggered RX loop hits depth/rate limits; stalled dispatcher stays bounded and cancellable with visible dropped/overflow state.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][A02]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `A02` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `rule_validate`, `rule_start`, `rule_stop`, `rule_status`, `rule_simulate`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://docs.logz.io/docs/user-guide/log-management/log-alerts/correlated-alert/

### A03 — Compose capture, command, analysis and action workflows

**Integrated hard dependencies:** G01, G02, G03, A01, A02, E02. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Run a repeatable developer debugging recipe with supervised cleanup.

**Depends on:** A01, A02, E02. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `src/automation/include/workflowrunner.h`, `src/automation/src/workflowrunner.cpp`.
- Adapt existing ScenarioRunner/ScriptSupervisor and registered capture/action commands; retain their process and source ownership.
- Test: `tests/ui/workbench_a03_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `WorkflowRunner::start(const QJsonObject& definition, const QJsonObject& inputs) -> QString`; `status(const QString&) const -> QJsonObject`; `cancel(const QString&) -> bool`.
- `WorkflowRunner::setDispatcher(OperationDispatcher) -> void`; `setClock(AutomationClock*) -> void` uses an owner-managed clock with lifetime covering the run.

**Required behavior:**
- Support sequential and bounded parallel branches: start capture/program, wait condition, run shared query, invoke named action, collect artifact, stop owned resources. Record stable run/step/action IDs and cancellation reasons.
- Deadline/cancel must stop spawned process trees and release run-owned sources/subscriptions. User-opened captures are retained unless the definition explicitly owns them. Completed analysis can resume; real effectful steps are not automatically repeated after crash.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_a03_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][A03]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - workflow_process_failure: child exits nonzero while another branch waits; error policy cancels siblings and cleans owned resources without stopping unrelated captures.
  - workflow_cancel_tree: cancel during blocked output/analysis/send; no child processes, queued sends or event subscriptions remain after acknowledgement.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][A03]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `A03` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `workflow_start`, `workflow_status`, `workflow_cancel`, `workflow_results`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/scenariorunner.cpp
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/scriptsupervisor.cpp

### A04 — Add developer assertions and data-driven tests

**Integrated hard dependencies:** G01, G02, G03, A01, A02, I04. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Turn observed behavior into reusable checks for logs, applications and protocols.

**Depends on:** A01, A02, I04. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Extend existing `src/app/python_runtime/klogg/test.py`, scenario bootstrap and report paths.
- Create proposed `src/automation/src/assertionengine.cpp`, `Resources/schemas/assertion-v1.json` and fixture-driven scenario examples.
- Test: `tests/ui/workbench_a04_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `AssertionEngine::evaluate(const QJsonObject& assertion, const QJsonObject& evidence) -> QJsonObject`; result states `passed|failed|pending|inconclusive|cancelled`.

**Required behavior:**
- Support presence/absence, counts/rates, field values/ranges, A→B deadlines, request duration, checksum/outcome and baseline regression. Define scope, clock, minimum coverage and expected event multiplicity.
- Parameterize cases from JSON/CSV datasets with typed inputs; collect exact supporting and contradicting EventRefs/FrameRefs. Partial or dropped input produces inconclusive status unless the assertion explicitly permits partial evidence.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_a04_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][A04]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - assert_partial_not_pass: truncated input containing no error cannot prove an absence assertion and returns inconclusive.
  - assert_deadline_and_types: event at deadline follows the documented inclusive boundary; missing/null/empty/zero and large integers are evaluated distinctly; parameterized cases retain individual evidence.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][A04]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `A04` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `assertion_validate`, `assertion_evaluate`, `run_assertion_suite`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/app/python_runtime/klogg/test.py

### A05 — Record and replay event-driven investigations

**Integrated hard dependencies:** G01, G02, G03, A01, F01, D01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Reproduce analysis, rules and scenarios with deterministic virtual time.

**Depends on:** A01, F01, D01. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `src/automation/include/replayengine.h`, `src/automation/src/replayengine.cpp`, `Resources/schemas/recording-manifest-v1.json`.
- Consume D01 byte recordings and source event manifests; integrate existing script/scenario event subscriptions.
- Test: `tests/ui/workbench_a05_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `ReplayEngine::open(const QString& manifestPath) -> QJsonObject`; `control(const QString& operation,const QJsonObject&) -> QJsonObject`; operations `play|pause|step|seek|stop`.

**Required behavior:**
- Preserve recorded source sequence, clock basis, timing quality, offsets and generation; support original/scaled/as-fast-as-possible playback. Analysis simulation routes all generated effects into virtual transports/journal.
- Seeking resets dependent parsers/rules/windows and replays bounded state from checkpoints. Active replay is a separately configured mode delegated to D transport policies; recordings alone never silently target a live device/process.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_a05_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][A05]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - replay_repeatability: identical seed, recording, parser and rule versions produce identical dataset/action-journal hashes at all playback speeds.
  - replay_seek_state: seek across a partial frame and absence timer; rebuilt state equals replay-from-start; simulation emits zero real transport writes.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][A05]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `A05` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `replay_open`, `replay_control`, `replay_status`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/include/scenariorunner.h

### A06 — Build a friendly automation recipe and rule editor

**Integrated hard dependencies:** G01, G02, G03, A01, A02, A04, U03. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Create useful workflows through forms and previews, with a Python escape hatch.

**Depends on:** A01, A02, A04, U03. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `src/ui/include/workfloweditordock.h`, `src/ui/src/workfloweditordock.cpp`, `src/ui/src/ruleeditorwidget.cpp`.
- Integrate existing actions manager, parameter dialogs, scenario/script windows and command palette metadata.
- Test: `tests/ui/workbench_a06_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `WorkflowEditorController::get() const -> QJsonObject`; `set(const QJsonObject&) -> QJsonObject`; `preview(const QJsonObject& sample) -> QJsonObject`.

**Required behavior:**
- Present When→Condition→Action, step lists, variable editors, input/output previews and validation near controls. Offer recipes for failed builds, exceptions, latency regressions, missing heartbeats and protocol request/response.
- Advanced Python hooks call the existing supported client API and supervisor. Explain what will run and which sources/actions are affected; preserve user edits after validation/runtime failure.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_a06_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][A06]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - recipe_no_code: create a rule from an error chart, preview against sample events, save and execute; matches shared headless definition.
  - recipe_validation_restore: bad field/type and unavailable connector highlight the relevant step; reopening preserves edits and performs no effects.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][A06]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `A06` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `show_workflow_editor`, `get_workflow_editor`, `set_workflow_editor`, `preview_workflow`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/actioneditdialog.cpp
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/scenariorunnerwindow.cpp

### A07 — Deliver headless CI execution, schedules and reports

**Integrated hard dependencies:** G01, G02, G03, A03, A04. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Run the same developer tests without the interactive workspace.

**Depends on:** A03, A04. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Extend existing app argument parsing, `src/app/python_runtime/klogg/test.py`, ScenarioRunner report output and lab execution entry points.
- Create proposed `src/automation/src/workflowschedule.cpp`, `tests/mcp/workflow_ci.py`, `docs/workbench/ci.md`.
- Test: `tests/ui/workbench_a07_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- Proposed CLI: `cilogg workflow run --definition PATH --inputs PATH --report-dir DIR`; flags `--headless`, `--timeout-seconds`, `--allow-partial`.
- Exit codes for this new verb only: 0 success, 1 assertion/workflow failure, 2 configuration/setup failure, 3 cancelled/timeout, 4 inconclusive input. Existing CLI exit contracts stay intact.

**Required behavior:**
- Reuse existing JSON/JUnit report generation; expose source/evidence links and per-step durations. Support configured interval schedules while the host is running and explicit single-shot CI jobs; disclose that a closed desktop app is not an always-on monitoring server.
- Make scheduled overlap policy `skip|queue_one|cancel_previous` explicit; use bounded journal retention. Hardware tests advertise required capabilities; unsupported hardware is blocked/skipped with reason and never counted as passed.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_a07_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][A07]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - ci_without_desktop: run a process-capture assertion using isolated settings and no user GUI; JSON/JUnit statuses and exit code agree.
  - schedule_overlap_timeout: virtual-time schedule does not pile up jobs; cancellation/timeout closes owned resources and writes a valid final report.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][A07]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `A07` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `workflow_schedule`, `workflow_unschedule`, `workflow_report`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/app/python_runtime/klogg/test.py
- https://docs.pytest.org/en/stable/how-to/output.html

### A08 — Add the execution journal and failure diagnostics

**Integrated hard dependencies:** G01, G02, G03, A03, A05. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Explain every automated observation, decision and action with reproducible context.

**Depends on:** A03, A05. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `src/automation/include/executionjournal.h`, `src/automation/src/executionjournal.cpp`, `src/ui/src/executionjournaldock.cpp`.
- Connect existing script/scenario output, lab run metadata, rule matches, action acknowledgements and artifact manifests.
- Test: `tests/ui/workbench_a08_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `ExecutionJournal::append(const QJsonObject& entry) -> QString`; `page(const QString& runId,const QString& cursor,int pageSize) -> QJsonObject`; `release(const QString& runId) -> bool`.

**Required behavior:**
- Entries carry run/step/source/action IDs, time basis, event sequence, input lineage, operation status and reason. Separate observations, decisions, attempted sends and acknowledged results.
- Bound memory and on-disk retention, redact diagnostic credentials, expose dropped events and incomplete runs, and recover the last valid journal segment after an interrupted write.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_a08_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][A08]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - journal_ack_semantics: attempted and accepted/failed writes are distinguishable; duplicate acknowledgement does not create a second action result.
  - journal_retention_crash: exceed cap and truncate final record; valid earlier entries recover, limits are reported, and no action is automatically replayed.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][A08]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `A08` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `journal_get`, `journal_export`, `journal_release`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/app/labcontrollerservice.cpp
