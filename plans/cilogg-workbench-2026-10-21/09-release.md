# Cross-Platform Release and Product Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the full workbench in independently reviewable releases with trustworthy correctness, usability, scale, and capability evidence.

**Architecture:** Validate complete developer workflows across the existing application, new services, optional renderers, plugins, automation and recordings. Reuse current build/test/release infrastructure.

**Tech Stack:** Existing CMake/Catch2/QtTest, Windows/macOS/Linux CI, PowerShell/Python, real packaged applications and virtual/hardware transport fixtures.

**Spec:** `00-product-spec.md`; every exact constraint applies.

## Global Constraints

Do not claim a platform, transport, performance or migration gate passed unless it ran and its evidence is retained. Unsupported capabilities are explicit, not silently successful tests. A task can land before a release wave passes its combined gates.

## Review Focus

Cross-feature source lifetime; altered byte/field meaning; restored settings and packs; optional dependency failure; live operation under saturation. The five exact owning-test mappings are maintained in the master guide.

### R01 — Build end-to-end developer workflow acceptance suites

**Integrated hard dependencies:** G03, U13, V18, A07, D17, E10, E11. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Depends on:** G03 and the feature tasks in the release wave being certified.

**Files:** Proposed `tests/workbench/acceptance.py`, `test_data/workbench/journeys/`, `docs/workbench/release-evidence.md`; extend existing GUI object-name automation and MCP parity harness.

**Interfaces:** `acceptance.py --executable PATH --journey J1|J2|J3|J4|J5 --artifacts DIR --capabilities PATH` returns 0 only when required capabilities and assertions pass. The capabilities file is captured from the actual build, not handwritten to bypass missing features.

- [ ] Add failing fixture-backed journeys for: open/inspect/chart/drilldown; run/capture/compare; multi-source/clock alignment; protocol request/response/replay; save/CI/share. Add variants for backend, desktop/mobile, DevOps, QA/build, ETL, game/performance and embedded examples.
- [ ] Run against a build lacking the required feature and verify explicit failure with the missing operation/capability named.
- [ ] Wire real public operations, keyboard/object-name UI flows, expected EventRef/FrameRef results and report artifacts. Use independent reference totals for counts/distributions and recorded expected TX bytes for protocol tests.
- [ ] Run each applicable journey with fresh settings, native/rich build modes, accelerated/Qt-only matching, source append/replacement, incomplete results and cancellation. Hardware-specific checks use real adapters separately.
- [ ] Record actual command output, screenshots, completion metadata and per-capability pending/blocked states in the release evidence. Commit acceptance fixtures and update feature coverage without marking skipped required cases passed.

**Acceptance:** A developer unfamiliar with CILogg can follow each documented journey and reproduce its investigation through GUI and headless operations.

**Rollback:** Remove the new release claim/capability flag until its acceptance suite passes; keep older supported workflows intact.

### R02 — Close stress, performance and long-run resource gates

**Integrated hard dependencies:** G02, R01, F13, V18, A08, D18, E08, E10. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Depends on:** G02, R01 and the services included in the release wave.

**Files:** Extend benchmark scripts and source/job/transport/renderer diagnostics; add proposed `tests/workbench/stress.py` and recorded benchmark manifests.

- [ ] Define failing saturation cases: high-cardinality fields; 12 dashboard panels; dense samples; multiline overflow; stalled result consumer; long-running scripts; disconnected/blocked transport; queued jobs while sources close; repeated provisional tail replacement; unlimited-file input constrained by configured scope.
- [ ] Run a deliberately unbounded or delayed fixture and verify the harness reports the violated recorded memory/latency/queue budget.
- [ ] Complete fixes in the owning F/V/A/D/E task, not in a parallel release-only engine. Ensure all cache keys include source/parser/query/generation/version relevant to their meaning.
- [ ] Run repeated cold/warm measurements on the G02 corpus, plus a sustained live workload with periodic append/reload, job cancel/release and optional rich renderer restarts. Confirm retained allocations settle rather than growing with runtime/input length.
- [ ] Publish measured medians/p95, memory slopes, dropped/partial data, hardware/driver and package configuration. Compare feature-disabled and enabled overhead separately; commit evidence summaries and reproducible commands.

**Acceptance:** Every release claim meets G02's recorded budgets on its declared environment. Allocation caps, lab throughput and supported hardware observations are separate evidence.

**Rollback:** Disable the offending optional capability or revert its consumer slice; retain raw capture integrity and source cleanup.

### R03 — Validate migrations, recoverability and platform packages

**Integrated hard dependencies:** U03, U11, E01, E09, E10, V03, D13, R01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Depends on:** U03/U11, E01/E09/E10 and the release wave's renderer/transports.

**Files:** Extend existing `.github/workflows/`, packaging and installer payload lists, plugin resources, native Qt deployment and optional WebEngine dependencies. Add proposed migration fixtures and package smoke scripts.

- [ ] Add cases for prior projects/actions/filter IDs/tab groups, unknown schema versions, malformed imports, same-name/different-ID packs, interrupted atomic writes, missing source/renderer/plugin and downgraded configurations. Assert recoverable previous versions and no unintended effects on restore.
- [ ] Verify failures before implementing missing migrations or packaging fixes.
- [ ] Package native-only core builds for supported Qt5/Qt6 Windows/macOS/Linux targets. Package optional rich-renderer builds only on supported SDK/compiler combinations. Preserve C++17; report optional dependency compiler/platform requirements instead of upgrading the core silently.
- [ ] Run installed/portable package smoke in clean environments: launch, open/search, native chart, optional rich chart, process capture, script cancellation, plugin capability discovery, JSON/JUnit export and correct uninstaller/reinstall preservation. Confirm resources work without fetching a CDN.
- [ ] Review code-impact/diffs, current complete CTest, MCP/CLI/Python parity and workflow matrix; attach build/package hashes and exact hosted/local status. Commit packaging and migration evidence.

**Acceptance:** Packages load their actual declared capabilities, old definitions remain readable, optional components fail gracefully, and clean-machine behavior matches development behavior.

**Rollback:** Keep prior schema backups and native-only installation path; revert new payload/capability declarations together with consumers.

### R04 — Publish developer documentation, examples and completion ledger

**Integrated hard dependencies:** U01, U02, U03, U04, U05, U06, U07, U08, U09, U10, U11, U12, U13, I01, I02, I03, I04, I05, A01, A02, A03, A04, A05, A06, A07, A08, E01, E02, E03, E04, E05, E06, E07, E08, E09, E10, E11, E12, G01, G02, G03, F01, F02, F03, F04, F05, F06, F07, F08, F09, F10, F11, F12, F13, F14, F15, F16, V01, V02, V03, V04, V05, V06, V07, V08, V09, V10, V11, V12, V13, V14, V15, V16, V17, V18, R01, R02, R03, D01, D02, D03, D04, D05, D06, D07, D08, D09, D10, D11, D12, D13, D14, D15, D16, D17, D18, MD08. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Depends on:** R01–R03 and MD08 for the release wave; ALL task owners must be accounted for before the full-program completion claim.

**Files:** Proposed `docs/workbench/`, bundled example packs, public command reference, plugin SDK additions, migration notes, screenshots, this plan's `PROGRESS.json` and `FEATURE_COVERAGE.md`.

- [ ] Generate user documentation from real discoverable commands/capabilities and verified screenshots. Include quick starts for each developer domain, data/clock/completeness explanations, custom parser/chart/rule recipes, headless CI, source setup and troubleshooting.
- [ ] Validate every local document link, schema example, command sample and packaged example. Use the provided plan validator for task dependencies and requirement ownership; do not add tests that only mirror documentation wording.
- [ ] Reconcile every coverage row with implementation commit/PR, relevant test evidence, GUI/CLI/MCP parity and platform/hardware status. Every originally discussed feature remains assigned; a dropped scope item requires an explicit recorded decision.
- [ ] Prepare focused release notes and a reviewable draft release/PR. Publishing or merging follows the user's actual instruction at execution time, not an assumption made by this plan.
- [ ] Mark a release/full program complete only after all its required tasks and gates pass. Preserve known limitations and next release tasks as separate entries.

**Acceptance:** The shipped product and its documentation agree; users can reproduce examples and understand limitations. A plan checkbox is never substituted for runtime evidence.

**Rollback:** Correct/retract inaccurate documentation or capability claims and keep prior validated example versions available.
