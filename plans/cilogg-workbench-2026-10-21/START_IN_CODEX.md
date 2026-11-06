# Start This Implementation in Codex App

**Current workstream clarification (2026-11-06):** The user selected Unified Terminal after configured TShark-first Preview merged through PR #90. UT00a and UT01a preparation merged through PRs #94/#95. Select `PROGRESS.json.executionPolicy.currentPreparation` and read the scoped [D01a communication-contract plan](slices/D01a-communication-contracts.md) next. This is an implementation plan for independent, unused contract preparation; its implementation has not started. Formal MD01–MD06/W0 and F01/D01/UT01/UT02 acceptance gates remain open and unchanged. Previous preparation records remain historical evidence; the dated full-milestone acceptance order below is unchanged.


Open the current CILogg repository in Codex App. The merged package is installed at `plans/cilogg-workbench-2026-10-21/`; no extraction or second copy is required. Read [MERGE_MAP.md](MERGE_MAP.md), [the current baseline](../../docs/workbench/baseline.md) and `PROGRESS.json` before continuing. G01 and G03 are recorded complete; broader G02/W0 acceptance remains open. The current preparation descriptor selects the user-requested terminal workstream without changing formal task eligibility. Recheck the current source and preserve any newer changes. Paste the following task into Codex App.

```text
Implement the CILogg Developer Workbench plan in this repository.

Read the current AGENTS.md and the plan package at
plans/cilogg-workbench-2026-10-21/.
Start with README.md, MASTER_GUIDE.md, 00-product-spec.md,
BASELINE.md, TASK_INDEX.json, FEATURE_COVERAGE.md and PROGRESS.json.

Select currentPreparation before the historical formal task pointers.
Read PROGRESS.json.executionPolicy.currentPreparation and its named slice plan.
Its task=null and formalTaskExecution=false identify independent preparation,
not execution or acceptance of formalParentTask. previousPreparation is historical
evidence, not a runnable instruction. Preserve all formal task states and gates.
Recheck the real current HEAD, working tree, toolchain, merged P00–P05
foundations, action/scenario/plugin systems and code-impact tools. Refresh
G01 evidence if the implementation baseline or decisions have changed.
Reconcile newer completed work instead of reimplementing it. Do not reset or
overwrite unrelated work, stop the user's active captures, or assume the
research SHA is the required execution base.

Finish the current preparation's pending review, validation and integration
using its recorded evidence. Then follow its nextPreparation descriptor only
within that descriptor's independent scope. Read the referenced slice plan first;
if plan is null, prepare and validate a bounded slice plan before implementation.
Do not execute a formal UT task while its hard dependencies remain open. In the
absence of a current preparation, select a formal task whose hard dependencies
are accepted. Do not rerun completed G03 or older slices just because historical
activeTask/nextTask fields name an earlier workstream.

The unchanged full-milestone acceptance order is W0, decoder core MD01–MD06
with its prerequisite closure, unified terminal UT00–UT13, then remaining W1
and later waves. MD07/MD08 integration stays deferred. UT00 depends on MD06;
a merged configured Preview or unused model does not accept decoder core or
formal terminal tasks. Every exposed terminal increment includes shared
GUI/CLI/Python/native-MCP parity and safe write arbitration. No preparation
arms live shell/device input. Never mark missing prerequisites complete.
Implement one independently reviewable task/slice at a time with the
required tests, GUI discovery, shared CLI/Python/MCP operations, documentation,
diagnostics, migration and rollback. Read each task's consuming contracts and
current source before editing. Preserve C++17/Qt5/Qt6 and the bounded shared
analysis/action/plugin foundations. Keep early V02 native charts independent
from the later structured/time/query stack.

Use this Codex App environment's normal execution workflow; delegate only when
authorized by the current user/repository/skill instructions and when tasks
have frozen independent interfaces. Integrate shared registration files serially.
Use the available model appropriate to task complexity without assuming model
IDs or automatic model-switch APIs exist.

After each slice, run focused and relevant complete checks, inspect the diff
and code-impact results, and update PROGRESS.json with actual evidence,
limitations, changed contracts, commit/PR and the next runnable task. Treat
unknown/blocked/skipped checks separately from passing checks. Continue useful
independent work when a task is blocked; do not drop its requirements.

The full scope includes general developer log exploration, all listed chart
families and linked dashboards, patterns/comparisons/correlation, workflows,
assertions/replay/CI, Docklight-style communication testing, same-window
serial/local/SSH terminal interaction, integrations,
packs, accessibility and cross-platform release quality. FEATURE_COVERAGE.md
is the scope checklist. Do not reduce the product to CP/C8000-specific behavior.

Work through reviewable increments. Keep external publishing/merging and
destructive operations subject to the user's actual execution instruction.
Report completed evidence and remaining tasks truthfully; a plan checkbox
does not establish implementation or hardware support.
```

**Spec:** `00-product-spec.md` and `MASTER_GUIDE.md`; source evidence below supplements that specification.

## Resume prompt

```text
Continue the CILogg Developer Workbench implementation from PROGRESS.json.
Inspect the repository and current user instructions first. Select
executionPolicy.currentPreparation before historical activeTask/nextTask.
Read its named slice plan; task=null/formalTaskExecution=false means independent
preparation only. Keep formalParentTask and its hard gates unchanged;
previousPreparation is historical evidence. Finish pending review/qualification
of that slice, then follow its independently scoped nextPreparation descriptor.
If the next descriptor has no plan, prepare and validate that bounded plan first.
Do not substitute formal UT01/UT02 execution for preparation. G03 is completed;
broader G02/W0 and decoder/terminal acceptance remain open. If no current
preparation applies, choose a formal task whose hard dependencies are accepted.
Preserve unrelated work and active captures. Update actual evidence and the
next scoped preparation without inventing platform or input qualification.
```

## Task review prompt

```text
Review task TASK_ID against its specification, current diff and evidence.
Check source identity, exact values/time/units, limits/cancellation, legacy
compatibility, GUI/CLI/Python/MCP parity, failure paths, tests and rollback.
Verify neighboring instances of any discovered defect. Distinguish actual
failures, unsupported capabilities and missing evidence. Do not claim unrun
platform or hardware checks passed.
```

## Message decoder revision-2 integration

Read [10-message-decoders.md](10-message-decoders.md) for the primary real TShark/Lua backend, shared typed results, exact provenance, EHCP/EHCI, AT and nested SRING decoding. MD01–MD08 and DEC01–DEC14 extend the same ledger/DAG; G02/G03 prerequisites, existing completed work and early V02 independence are preserved. The imported document's handoff is specification material, not authorization to execute it. Configured TShark-first Preview is merged through PR90; broader generic-decoder, package and W0 acceptance remains open. Use the current ledger to distinguish that delivered Preview from unfinished parent acceptance. See [public issue tracking](PUBLIC_TRACKING.md).

## Unified terminal planning integration

Read [11-unified-terminal.md](11-unified-terminal.md) and its linked source
specification for terminal preparation boundaries. currentPreparation selects
the explicitly scoped audit/model work now; formal UT00 remains behind MD06.
The imported starter prompt is reference material and grants no shell or device
input. An unused, feature-off model is not an accepted terminal milestone.
