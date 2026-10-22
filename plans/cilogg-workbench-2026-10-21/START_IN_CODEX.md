# Start This Implementation in Codex App

Open the current CILogg repository in Codex App. The merged package is installed at `plans/cilogg-workbench-2026-10-21/`; no extraction or second copy is required. Read [MERGE_MAP.md](MERGE_MAP.md), [the current baseline](../../docs/workbench/baseline.md) and `PROGRESS.json` before continuing. G01 reconciliation is recorded; G02 is next and G03 is also runnable. Recheck the current source and preserve any newer changes. Paste the following task into Codex App.

```text
Implement the CILogg Developer Workbench plan in this repository.

Read the current AGENTS.md and the plan package at
plans/cilogg-workbench-2026-10-21/.
Start with README.md, MASTER_GUIDE.md, 00-product-spec.md,
BASELINE.md, TASK_INDEX.json, FEATURE_COVERAGE.md and PROGRESS.json.

Resume from PROGRESS.json (G02 after the recorded G01 reconciliation).
Recheck the real current HEAD, working tree, toolchain, merged P00–P05
foundations, action/scenario/plugin systems and code-impact tools. Refresh
G01 evidence if the implementation baseline or decisions have changed.
Reconcile newer completed work instead of reimplementing it. Do not reset or
overwrite unrelated work, stop the user's active captures, or assume the
research SHA is the required execution base.

Complete G02/G03 and then the next runnable task in the dependency
graph. Implement one independently reviewable task/slice at a time with the
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
assertions/replay/CI, Docklight-style communication testing, integrations,
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
Inspect the current repository first. Select the next task whose hard
dependencies are complete, read its subsystem plan and required contracts,
and continue from the last verified evidence. Preserve unfinished unrelated
work and active captures. Update progress and the next runnable task when done.
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
