# Sources, Integrations and Ecosystem Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Support general developer environments with compatible plugins, reusable packs and portable evidence.

**Architecture:** Extend the existing CILogg services through the canonical contracts in the product specification. Existing filenames are anchors; proposed filenames are implementation targets to reconcile at G01.

**Tech Stack:** C++17, Qt5/Qt6, existing Catch2/QtTest, Commander/CLI/MCP and Python runtime.

**Spec:** `00-product-spec.md` and `MASTER_GUIDE.md`.

## Global Constraints

Apply every exact constraint in `00-product-spec.md`. Preserve current analysis bounds, C++17/Qt5/Qt6, stable source/config identities, headless parity, and out-of-tree builds.

## Review Focus

Exercise stale source/state; malformed/large input; cancellation/backpressure; restored or unavailable capabilities; and GUI/headless disagreement in the named task cases below. The global Review Focus ownership matrix is in the master execution guide.

---

### E01 — Extend the plugin SDK with explicit developer-source capabilities

**Integrated hard dependencies:** G01, G02, G03, F01, D01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** A compatible extension model for new sources, parsers, analyses and actions.

**Depends on:** G01, F01, D01. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Modify existing `src/plugins/include/pluginmanifest.h`, `src/plugins/src/pluginprocess.cpp`, `src/plugins/include/pluginmanager.h`, `docs/plugin-sdk.md` and Python SDK.
- Create proposed capability/schema registries under `Resources/schemas/plugins/`; reconcile binary wire additions with D13.
- Test: `tests/ui/workbench_e01_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- Preserve existing protocol v1 text-record messages. Add negotiated version/capabilities for event metadata, binary segment references, parser/analysis/action requests and paged results.
- `PluginCapabilityRegistry::describe(const QString& pluginId) -> QJsonObject`; public operations remain registered through Commander.

**Required behavior:**
- Keep process isolation, acknowledged batches, backpressure, cancellation, ownership and timeout handling. Binary data is explicit base64/managed-segment references, never misrepresented as UTF-8 text.
- Expose declared platforms/dependencies and capability errors. Do not load a donor native plugin ABI or execute imported scripts as configuration.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_e01_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][E01]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - sdk_v1_compatibility: existing ADB/syslog fixtures operate byte-for-byte as before with no new capability requirements.
  - sdk_negotiation_backpressure: unsupported binary version fails readably; slow acknowledgement and plugin death release credits/resources without losing visible gap/error state.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][E01]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `E01` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `plugins_capabilities`, `plugins_validate`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/docs/plugin-sdk.md
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/plugins/src/pluginprocess.cpp

### E02 — Add arbitrary command-output and stdin capture

**Integrated hard dependencies:** G01, G02, G03, E01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Capture everyday developer tools and processes into the normal viewer.

**Depends on:** E01. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `plugins/examples/command-output/` manifest and implementation using existing Python SDK.
- Modify existing app CLI/startup handling and both build/install plugin payload lists; map to previous P21.
- Test: `tests/ui/workbench_e02_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- Capture definition: `program`, literal `argv`, `workingDirectory`, selected environment overrides, encoding, stdout/stderr policy and retention.
- Proposed CLI `cilogg --stdin` is explicit and mutually exclusive with native MCP stdio; command capture uses registered source operations.

**Required behavior:**
- Keep stdout/stderr identity, process start/exit metadata, partial lines and raw bytes where supported. Use literal argv by default; explicit shell mode is labelled.
- Handle high output, EOF, stderr-only failure, encoding boundaries, cancellation and process-tree cleanup. Segment rotation supports configured size/time and quota action pause/stop/drop-oldest; report coverage gaps and never delete user-opened input files. Session restore retains definition without restarting commands.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_e02_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][E02]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - process_argv_and_streams: spaces/metacharacters remain literal arguments; split Unicode and stderr content retain correct source identity.
  - stdin_mcp_exclusion: native MCP stdin is never consumed as log input; high-rate output stays bounded; cancel leaves no descendants.
  - capture_rotation_disk_full: configured quota policy rotates/stops capture with explicit gap/error metadata; original input logs remain untouched.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][E02]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `E02` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `capture_process`, `capture_process_status`, `capture_process_stop`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/docs/plugin-sdk.md

### E03 — Deliver Docker and Compose capture packs

**Integrated hard dependencies:** G01, G02, G03, E02. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Capture container/service output using the developer's existing Docker environment.

**Depends on:** E02. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `plugins/examples/docker-logs/` and `Resources/workbench/packs/docker/`; register packaging and examples.
- Test: `tests/ui/workbench_e03_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- Connector configuration: Docker context, container/service selection, follow, since/tail, timestamps and label metadata; emit canonical event/source fields.

**Required behavior:**
- Use documented Docker CLI operations with explicit argv and existing credentials/context; preserve stdout/stderr and container/service identities. Capture restart/disconnect and resume coverage visibly.
- Offer web-service request/error dashboards and capture recipes without requiring a bundled Docker daemon.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_e03_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][E03]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - docker_fixture_multiplex: fixture CLI emits stdout/stderr and two container identities; records map correctly and timestamp precision survives.
  - docker_missing_and_restart: missing CLI/permission failure preserves other sources; reconnect reports possible gaps/duplicates instead of claiming exactly-once coverage.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][E03]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `E03` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `docker_sources`, `docker_capture`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://docs.docker.com/reference/cli/docker/container/logs/
- https://docs.docker.com/reference/cli/docker/compose/logs/

### E04 — Deliver Kubernetes log capture packs

**Integrated hard dependencies:** G01, G02, G03, E02. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Investigate selected pods/containers with stable contextual metadata.

**Depends on:** E02. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `plugins/examples/kubernetes-logs/` and container investigation templates; update packaged plugin lists.
- Test: `tests/ui/workbench_e04_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- Configuration: existing kube context, namespace, pod/label selection, container, follow, previous instance, since, timestamps, and bounded concurrent streams.

**Required behavior:**
- Use documented kubectl operations, retain pod/container/namespace identifiers, and handle pod replacement and partial source failures.
- Expose target selection and current context clearly; adopt existing credential handling and explicit per-source coverage.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_e04_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][E04]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - kube_prefix_and_clock: two pods with identical messages remain separate sources; prefixed timestamps parse without losing message bytes.
  - kube_rollover_limits: more selected pods than concurrency limit are queued/rejected visibly; deleted pod and expired credentials stop/reconnect with explicit coverage state.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][E04]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `E04` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `kubernetes_sources`, `kubernetes_capture`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://kubernetes.io/docs/reference/kubectl/generated/kubectl_logs/

### E05 — Add SSH and OS log-source recipes

**Integrated hard dependencies:** G01, G02, G03, E02. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Use remote process output and native system logs through the same source workflow.

**Depends on:** E02. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `plugins/examples/ssh-command/`, `plugins/examples/system-journal/` and platform capability manifests.
- Retain existing ADB logcat and syslog-server plugins; extend their structured metadata mapping and examples.
- Test: `tests/ui/workbench_e05_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- SSH uses installed ssh with literal argument separation and configured host aliases; journal configuration selects supported OS source and filters.
- Supported recipes initially include Linux journalctl JSON, Windows Event Log exported/streamed records via a platform adapter, ADB logcat and RFC syslog inputs.

**Required behavior:**
- Preserve host/service/process/resource and original timestamps; report clock and reconnect uncertainty. Use existing OS/tool authentication rather than copying secrets into projects.
- Platform-only capabilities are discoverable and unavailable states do not prevent other connectors from loading.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_e05_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][E05]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - remote_source_disconnect: fixture SSH exits mid-record; source is partial/disconnected and repeated chunks are accounted for.
  - platform_source_mapping: journal/Event Log/ADB/syslog samples map severity/time/source consistently; unsupported OS returns capability error.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][E05]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `E05` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `remote_capture`, `system_log_sources`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/plugins/examples/adb-logcat/README.md
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/plugins/examples/syslog-server/README.md
- https://www.freedesktop.org/software/systemd/man/latest/journalctl.html

### E06 — Add OpenTelemetry log, metric and trace ingestion

**Integrated hard dependencies:** G01, G02, G03, E01, F01, F05. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Interoperate with instrumented developer applications and collectors.

**Depends on:** E01, F01, F05. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `plugins/examples/otlp-receiver/`, OTLP mapping tests and resource/metric/span import adapters.
- Integrate shared trace/profile import contracts with E08 and avoid a second aggregation store.
- Test: `tests/ui/workbench_e06_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- Versioned adapter accepts OTLP/HTTP JSON and protobuf through optional dependencies; emits EventRecord/resource attributes, typed metric points and explicit span records.
- Map Timestamp separately from ObservedTimestamp; TraceId/SpanId/parent links remain optional and validated.

**Required behavior:**
- Keep body types, severity number/text, resource/scope attributes, time precision and counters/histogram temporality. Bound request/message sizes and report receiver backpressure/loss.
- Start/stop receiver explicitly through registered source capabilities. Use collectors as bridges for other transports and show actual accepted formats.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_e06_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][E06]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - otel_exact_mapping: large integer attributes, nanosecond timestamps, body arrays and missing trace context survive mapping.
  - otel_partial_batch: invalid/oversized record yields correct partial rejection and bounded memory; no fabricated spans or conversion of cumulative counters into gauges.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][E06]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `E06` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `otlp_receiver_start`, `otlp_receiver_status`, `otlp_receiver_stop`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://opentelemetry.io/docs/specs/otel/logs/data-model/
- https://opentelemetry.io/docs/specs/otlp/

### E07 — Add remote query connectors for existing log backends

**Integrated hard dependencies:** G01, G02, G03, E01, F07, F08. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Bring selected remote evidence into the local investigation workspace.

**Depends on:** E01, F07, F08. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed backend connector plugins for Loki, OpenSearch/Elasticsearch and OpenObserve, plus versioned backend capability descriptors.
- Use existing plugin process supervision and source/result paging.
- Test: `tests/ui/workbench_e07_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- Backend adapter operations `validate_connection`, `describe_fields`, `query`, `page`, `cancel`; carry remote query text/dialect, time range, sort and server truncation metadata.

**Required behavior:**
- Support server-side filtering/aggregation where the backend declares it, while keeping native remote query text inspectable. Import results as bounded provenance-bearing sources/datasets with actual scope and remote IDs.
- Do not claim arbitrary QuerySpec translation is equivalent across dialects. Unsupported translation asks the user to edit the query or uses explicit fetched-subset analysis with a subset label.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_e07_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][E07]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - backend_paging_partial: changing server cursor/time window and server result limits remain visible; local chart totals never silently claim whole-backend coverage.
  - backend_cancel_auth: credentials stay outside portable exports and diagnostics; cancel/timeout terminates pending page retrieval and preserves earlier historical results.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][E07]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `E07` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `backend_query`, `backend_page`, `backend_cancel`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://grafana.com/docs/loki/latest/reference/loki-http-api/
- https://docs.opensearch.org/latest/api-reference/search-apis/search/
- https://openobserve.ai/docs/reference/api/search/

### E08 — Import real traces and performance profiles

**Integrated hard dependencies:** G01, G02, G03, E01, F05, F07. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Support developer execution timelines and profiling charts from actual measurements.

**Depends on:** E01, F05, F07. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `src/analysis/src/traceimport.cpp`, `src/analysis/src/profileimport.cpp` and schema-aware adapters.
- Consume Chrome Trace Event JSON, OTLP spans, Speedscope JSON and collapsed stacks with documented per-format support.
- Test: `tests/ui/workbench_e08_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `TraceProfileImportService::submit(const QJsonObject& request, QString* error) -> QString`; returns datasets with kind `spans|slices|samples|stacks` and explicit units.

**Required behavior:**
- Preserve process/thread/resource identity, span parents, sample weights, stack IDs and timing units. Validate malformed nesting, negative/unknown durations and incomplete spans.
- Provide V waterfall/flame graph datasets and source cross-links where evidence exists; stack-trace exceptions remain distinct from sampled performance profiles.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_e08_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][E08]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - trace_profile_units: equivalent second/ms/us/ns fixtures produce equal durations; weighted samples preserve totals and self/inclusive metrics.
  - trace_invalid_and_large: cyclic parents, unmatched begin/end, missing stack frames and import cap yield explicit status without unbounded recursion or invented relationships.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][E08]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `E08` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `trace_import`, `profile_import`, `import_status`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://perfetto.dev/docs/getting-started/other-formats
- https://github.com/jlfwong/speedscope/wiki/Importing-from-custom-sources

### E09 — Create shareable packs and Git-backed team configuration

**Integrated hard dependencies:** G01, G02, G03, E01, U07, U11. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Reuse developer knowledge across projects and teams.

**Depends on:** E01, U07, U11. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Extend existing config import/export and plugin repository/storage services.
- Create proposed `src/workbench/packregistry.cpp`, `src/workbench/teamfolderservice.cpp`, `Resources/schemas/pack-v1.json` and examples for all eight developer domains.
- Test: `tests/ui/workbench_e09_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `PackService::execute(const QString& operation,const QJsonObject&) -> QJsonObject`; `TeamFolderService::preview(const QJsonObject&) -> QJsonObject`; apply uses the returned revision token.

**Required behavior:**
- Pack manifest declares schema/version/IDs, dependencies, parsers, field semantics, templates, visual assets, recipes and docs. Validate dependency cycles, duplicate IDs and platform capabilities before applying.
- Git workflow is explicit pull→diff/conflicts→apply, then explicit commit/push for publishing; preserve pre-apply backup. Imported actions/scripts are not activated by loading a pack. Reuse existing credential helpers.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_e09_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][E09]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - pack_collision_rollback: conflicting IDs and unsupported schema are previewed; partial apply failure restores last valid local configuration.
  - team_divergence: two clones edit the same stable ID; deterministic conflict preview preserves both sides and no hook/script runs during import.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][E09]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `E09` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `packs_list`, `pack_validate`, `pack_import`, `pack_export`, `team_preview`, `team_apply`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/plugins/include/pluginrepository.h
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/plans/log-analysis-roadmap.md

### E10 — Export portable evidence with optional redaction

**Integrated hard dependencies:** G01, G02, G03, U11, A08, V18. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Share a reproducible investigation with readable reports and preserved originals.

**Depends on:** U11, A08, V18. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed `src/workbench/evidencebundle.cpp`, `src/workbench/redactionservice.cpp`, `Resources/schemas/evidence-bundle-v1.json`.
- Reuse V18 chart/report export, existing config export and workflow report artifacts.
- Test: `tests/ui/workbench_e10_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `EvidenceBundleService::exportBundle(const QJsonObject&) -> QJsonObject`; `inspectBundle(const QString&) -> QJsonObject`; `importBundle(const QJsonObject&) -> QJsonObject`.

**Required behavior:**
- Bundle selected raw excerpts or immutable segments, source hashes/identities/locations, parser/query/panel versions, notes, journal/report links and completeness. Raw-unavailable historical sources remain explicitly unavailable.
- Preview scoped mask/hash replacements on exported copies; include a redaction manifest and preserve equality only where chosen. Relative paths are validated; portable bundles exclude credentials and do not autoexecute embedded recipes.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_e10_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][E10]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - bundle_reproduce: import on a machine with different original paths; bundled evidence reproduces selected counts/charts and indicates omitted sources.
  - bundle_redaction_atomic: cancel midway and reject path traversal; no partial destination or changes to originals; preview and final export substitutions agree.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][E10]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `E10` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `evidence_export`, `evidence_inspect`, `evidence_import`, `redact_preview`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://docs.newrelic.com/docs/logs/ui-data/obfuscation-ui/

### E11 — Integrate IDE navigation and developer command clients

**Integrated hard dependencies:** G01, G02, G03, U04. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Jump from evidence to source code and automate CILogg from existing tools.

**Depends on:** U04, G03. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Extend existing Commander/MCP schema/docs and `src/app/python_runtime/klogg/client.py`.
- Create proposed `src/ui/src/editorlinkservice.cpp`, `docs/workbench/ide-integration.md`, sample VS Code tasks and CI wrappers.
- Test: `tests/ui/workbench_e11_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- `EditorLinkService::resolve(const QJsonObject& frame,const QJsonObject& mappings) -> QJsonObject`; `open(const QJsonObject&) -> QJsonObject`.
- Expose feature and capability introspection plus versioned Python client methods for newly registered operations; preserve current names/imports.

**Required behavior:**
- Support configured project-root/path mappings, editor URI/argv templates, file/line/column links from stack/build events and selected-event deep links back into an investigation.
- Treat unresolved/remote paths as text until a mapping exists; keep literal argument handling and prevent source text from becoming shell syntax.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_e11_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][E11]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - editor_mapping_literals: Windows drive paths, spaces, Unicode, colon-containing names and remote-root mapping resolve correct file/line/column.
  - client_schema_parity: generated/new Python calls produce the same typed result/error/cursor as CLI/MCP; unavailable capability fails clearly.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][E11]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `E11` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `editor_resolve`, `editor_open`, `workbench_capabilities`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/app/python_runtime/klogg/client.py
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/app/mcp/runtime.cpp

### E12 — Add optional inspectable AI assistance

**Integrated hard dependencies:** G01, G02, G03, U05, A06, E11. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Help users create parsers, queries, charts and recipes while retaining explicit reproducible definitions.

**Depends on:** U05, A06, E11. G01–G03 are common prerequisites for public feature tasks.

**Files and ownership:**
- Create proposed optional `plugins/examples/analysis-assistant/`, assistant proposal schema and UI review panel.
- Use existing command discovery/MCP/schema interfaces; provider setup is a separate optional configuration capability.
- Test: `tests/ui/workbench_e12_test.cpp`; register in the corresponding existing test CMakeLists.

**Interfaces (proposed additions unless labelled existing):**
- Assistant output is `AssistantProposal {schemaVersion,kind,definition,explanation,inputScope,limitations}`; kinds `parser|query|panel|rule|workflow|finding`.
- Operations `assistant_propose`, `assistant_validate`, `assistant_apply` route accepted definitions to normal validators, never directly mutate source bytes or bypass action dispatch.

**Required behavior:**
- Accept user-selected samples and explicit provider/local-model configuration. Show what data is included; minimize it using selected fields and redaction. Core features work without assistant configuration.
- Validate generated syntax against real schemas and run bounded previews; show errors and assumptions. Store accepted definitions so analysis/automation is reproducible without another model request.

**Implementation steps:**
- [ ] Write `tests/ui/workbench_e12_test.cpp` with the following named cases and exact assertions; use Catch2 tags `[workbench][E12]` and `[ui]` or `[logdata]` in the integration harness so Windows CTest discovers them.
  - assistant_invalid_output: malformed/prose-only/unsupported proposal cannot change workspace or run an action.
  - assistant_preview_reproducible: accepted parser/query proposal yields identical sample results through standard service; absent provider leaves normal charts/rules usable.
- [ ] Build with the existing Windows build script, then run `cilogg_itests.exe "[workbench][E12]" -platform offscreen`. Confirm the new tests fail for the missing behavior. An empty test selection or stale executable is a failed verification.
- [ ] Implement the interfaces and behavior above in the owned files. Add bounded diagnostics, public command schemas, and necessary migration/documentation changes with the feature.
- [ ] Rebuild and pass the focused cases; add `E12` cases to `tests/mcp/workbench_parity.py` (G03) and verify these operations through GUI/CLI/MCP: `assistant_propose`, `assistant_validate`, `assistant_apply`. Run the relevant complete CTest suite and packaged UI flow required by the execution guide.
- [ ] Inspect the diff and current code-impact results, update PROGRESS.json with evidence and remaining limitations, and commit the coherent slice. Use a focused PR; do not mark a release complete on unrun hardware or hosted checks.

**Acceptance:** The required behavior and failure cases pass through the shared service and public entry points, with visible scope/completeness and no new unbounded queues.

**Rollback:** Disable the new capability/action family; keep persisted definitions readable and original source data untouched. Revert consumers before shared contracts.

**References:**
- https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/app/mcp/runtime.cpp
