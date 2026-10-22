# Execution Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Start from the current repository, retain completed work, and establish reproducible execution and evidence before feature delivery.

**Architecture:** Reuse existing sources, jobs, commands, actions/scenarios, and plugin ownership. New subsystem interfaces are additive adapters with versioned public schemas.

**Tech Stack:** Existing C++17, Qt5/Qt6, CMake, Catch2/QtTest, PowerShell and Python.

**Spec:** sibling `00-product-spec.md` after unpacking; exact constraints apply to all tasks.

## Global Constraints

Use out-of-tree build_root, KLOGG_BUILD_TESTS, the installed Qt SDK, the existing code-impact checks, and existing Commander/CLI/MCP discovery. No product implementation is claimed by this plan.

## Review Focus

Changing repository HEAD, unmerged P05, missing local toolchains, stale graph indexes, and historical measurements must be visible before implementation. A stale local research snapshot is not the current source tree.

### G01 — Reconcile current source, prior roadmap, and task ownership

**Integrated hard dependencies:** none. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Execution record (2026-10-22):** Reconciled in [the current baseline](../../docs/workbench/baseline.md) and PROGRESS.json. The steps below remain the reusable acceptance contract; G02/G03 supply fresh measurements and parity evidence.

**Outcome:** A trustworthy current baseline and executable dependency ledger.

**Depends on:** none.

**Files:** Read current `AGENTS.md`, `plans/log-analysis-roadmap.md`, `plans/value-counts-p05.md`, `docs/analysis-*.md`, `docs/value-counts.md`, top-level/source/test CMake files, and relevant source anchors. Update proposed `docs/workbench/baseline.md`, `docs/workbench/decisions.md`, and this package's `PROGRESS.json`.

**Steps:**

- [ ] Inspect `git status --short`, current branch/remotes/HEAD, latest default branch and P05 state. Preserve unrelated work. Use an isolated checkout/worktree when actual feature execution needs it.
- [ ] Confirm existing P00–P05 work against source and tests. At research time master is `bd74c338424845a2caac03d62fdff5187e7e8f0a`, P05 branch is `01cb637be7c96f2c9e3925247510c162380736b7`. These are evidence pins, not a command to reset the user's repository.
- [ ] Record implemented, implemented-on-branch, planned, replaced, and blocked states separately. Integrate existing P05 through the normal reviewed branch process when required; never rebuild Value Counts independently.
- [ ] Reconcile F/V/U/I/A/D/E/R file ownership and exact signatures before dependent edits. Record any renamed existing symbol and update every dependent card. The canonical F service owns typed query datasets; V owns presentation; existing job service owns scheduling.
- [ ] Read applicable nested repository instructions and refresh available graph indexes. Run upstream symbol impact before code edits and change detection before commits, as current AGENTS requires. If an essential tool is unavailable, record the exact limitation and complete safe read-only preparation; do not claim compliance or request blanket approval.
- [ ] Adopt the previous P06–P24 coverage table in the master guide. Existing implementation is credited only with current evidence. Create/update an implementation tracking issue when starting the major feature work, following repository guidance; this planning deliverable itself does not create external issues.

**Acceptance:** Baseline identifies actual HEAD/toolchain/current functionality, every requirement has a task owner, and no completed subsystem is duplicated. Documentation-only reconciliation is checked by review/link validation rather than synthetic product tests.

**Rollback:** Revert planning metadata; no user settings or source data change.

**References:** [Repository](https://github.com/dm17ryk/klogg), [pinned AGENTS](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/AGENTS.md), [prior roadmap](https://github.com/dm17ryk/klogg/blob/01cb637be7c96f2c9e3925247510c162380736b7/plans/log-analysis-roadmap.md).

### G02 — Freeze representative fixtures and measured release budgets

**Integrated hard dependencies:** G01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Performance and correctness checks represent several developer domains and live/binary workloads.

**Depends on:** G01.

**Files:** Extend existing `scripts/benchmarks/generate-log-analysis-corpus.ps1`, `docs/log-analysis-baseline.md`; create proposed `scripts/benchmarks/workbench-benchmark.py`, `test_data/workbench/manifest.json`, and small deterministic domain fixtures. Keep generated huge data outside Git.

**Interfaces:** `workbench-benchmark.py --executable PATH --corpus DIR --scenario NAME --output DIR` writes JSON containing machine/build/backend/SDK, cold/warm flags, corpus seed/hash, measured values, and gate comparisons. Return 0 for accepted measured gates, 1 for failed gates, 2 for invalid setup.

**Steps:**

- [ ] Generate deterministic plain/Unicode/long-line logs, JSON/logfmt/CSV, Java/.NET/Python/JS stack traces, web requests, build/test output, ETL stages, game frame measurements, multi-source clock resets, and byte captures. Include malformed, high-cardinality and incomplete tails with known expected results.
- [ ] Record baseline open/index/search/scroll behavior, peak memory, cancellation, append latency, and current feature-disabled overhead on the actual Windows environment. Add Linux/macOS evidence through CI and separate hardware results.
- [ ] Freeze numeric release budgets and their corpus/hardware conditions in the baseline. Measure proposed p95 GUI <100 ms and local CPU cancellation <1 s; document any calibrated replacement before features depend on it. Allocation bounds are not throughput measurements.
- [ ] Add benchmark scenarios incrementally with each feature, including multiple panels and sustained live streams. Compare native/rich renderer enabled and disabled independently.
- [ ] Validate reproducibility using the same seed/hash and repeated cold/warm runs; run a deliberately slowed fixture to demonstrate that the gate fails. Commit the generator and recorded baseline schema/evidence, not machine-generated huge logs.

**Acceptance:** An implementer can reproduce counts, byte streams, timing-order expectations, and measured gates. Missing platform/hardware evidence is labelled pending.

**Rollback:** Remove added benchmark scenarios without changing application behavior.

### G03 — Establish a reusable GUI/CLI/MCP parity harness

**Integrated hard dependencies:** G01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Outcome:** Every new task can demonstrate equivalent public behavior and cancellation through the existing command route.

**Depends on:** G01. G02 supplies performance evidence separately.

**Files:** Extend `tests/mcp/analysis_parity.py`, `tests/mcp/runtime_checks.py`; create proposed `tests/mcp/workbench_parity.py`, `test_data/workbench/command-cases/`, and `docs/workbench/automation-contract.md`. Register relevant CTest/CI entry points without replacing existing smoke tests.

**Interfaces:** `workbench_parity.py --executable PATH --task TASK_ID --artifacts DIR [--backend accelerated|qt]`; a case registry identifies task operations and expected structured results. It invokes existing `cilogg command --action ACTION --json-file REQUEST.json`, `cilogg_commands` discovery, and `cilogg_execute`; never invent a second MCP catalog.

**Steps:**

- [ ] Write harness tests for unknown task, missing operation, empty test selection, nonzero child exit, timeout, JSON precision loss, and mismatched results. Assert nonzero failures with captured diagnostics.
- [ ] Verify these cases fail before implementing the harness checks.
- [ ] Implement isolated application/settings directories, literal argv spawning, poll/cancel/release, opaque cursor paging, canonical JSON comparison preserving decimal strings, and artifact collection. Use existing published schemas rather than screenshots as the correctness oracle.
- [ ] Demonstrate parity on existing analysis and Regex Lab operations, plus P05 when available. Run accelerated and Qt-only paths. Add task cases as each public feature lands; missing selected cases fail rather than silently skip.
- [ ] Run the existing complete relevant CTest and MCP smoke suite. Commit the harness with its self-tests and documented invocation.

**Acceptance:** Task cards have a common command to prove interface parity. GUI presence is separately checked with objectName/accessibleName; headless success does not stand in for UI usability.

**Rollback:** Remove new harness entry points; retain existing tests and public commands.
