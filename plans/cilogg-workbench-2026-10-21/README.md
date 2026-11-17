# CILogg Codex Implementation Plan

**Current workstream clarification (2026-11-09):** D01c [PR #99](https://github.com/dm17ryk/klogg/pull/99) is merged; its source/test trees match tested head `7e811f03`, and all 25 jobs in its final build run passed. The next preparation is the [UT02a terminal attachment implementation plan](slices/UT02a-terminal-owner-attachment.md): bounded raw-RX delivery into the existing emulator, same-window read-only inspection and shared Commander/CLI/native-MCP/Python operations. This change prepares that plan; runtime implementation is not started. Both terminal/shared-writer flags remain default OFF. Formal MD01–MD06/W0 and F01/D01/UT01/UT02 gates and historical evidence remain unchanged.


Complete implementation package for a general developer logger, visualizer and automation workbench, including Docklight communication testing.

**Scope:** 128 implementation tasks and 209 mapped requirements. Imported and reconciled on 2026-10-22; terminal planning integrated on 2026-11-04; E13 added on 2026-11-11 (decision D11); R05, U14, U15 and U16 with requirements UX19–UX21 and QUAL11 added on 2026-11-13 (decisions D12 and D13); E14 added on 2026-11-13 (decision D14); R06 with requirement QUAL12 added on 2026-11-15 (decision D15); E15 and E16 with requirements ECO20–ECO22 proposed on 2026-11-17 (decision D16). G01 is complete as documentation/source reconciliation. Product features are not implemented by importing this package.

**User requests of 2026-11-13:** R05 (updater hand-off), U14 (filtered view "Matched by" and clear results), U15 (one source picker) and U16 (split panes, design approved) depend only on the completed G01, each as its own PR; U03 depends on U16 and R04 on all four. See [decisions D12 and D13](../../docs/workbench/decisions.md). Status on 2026-11-17: R05 ([PR #105](https://github.com/dm17ryk/klogg/pull/105)) and U14 ([PR #106](https://github.com/dm17ryk/klogg/pull/106)) are completed; U15a ([PR #107](https://github.com/dm17ryk/klogg/pull/107)) is merged and U15b is in [PR #114](https://github.com/dm17ryk/klogg/pull/114); U16a ([PR #108](https://github.com/dm17ryk/klogg/pull/108)) and U16b ([PR #111](https://github.com/dm17ryk/klogg/pull/111)) are merged and U16c (drag and drop) is planned.

**User request of 2026-11-15:** R06 (debuggable problem reports: activity journal, memory status, local report bundle, crash dumps) depends only on G01, in slices R06a and R06b; R04 depends on it. See [decision D15](../../docs/workbench/decisions.md). R06a is merged ([PR #112](https://github.com/dm17ryk/klogg/pull/112)); R06b (crash minidumps) is planned.

**User request of 2026-11-17 (proposed, awaiting approval):** E15 (plugin repositories: several catalogs, private Azure DevOps sign-in through the Git Credential Manager or a personal access token, updates from the install source, a daily silent update check) and E16 (configuration packs: previews, Lua and Python decoders, actions, highlighters and filters delivered and updated as plugins). The [design](../../docs/plugin-repositories.md) lists the open decisions. E15 depends only on G01 and E16 on E15; E09 depends on E16 and R04 on both. See [decision D16](../../docs/workbench/decisions.md).

**Execution status:** P05 is merged through PR #45; the formal runnable gate owner is G02. See [merged roadmap coverage](MERGE_MAP.md), [current execution baseline](../../docs/workbench/baseline.md), [integration decisions](../../docs/workbench/decisions.md), and `PROGRESS.json`. This directory is the canonical execution package; the original P00-P24 roadmap remains a linked specification.

## Start here

**Execution priority (2026-11-04):** Finish W0, then implement decoder core
MD01–MD06 with its F01–F04/D01/E01 prerequisites, then implement the
[unified terminal UT00–UT13](11-unified-terminal.md), then resume remaining W1 and
later waves. MD07–MD08 integration is deferred until its later-wave dependencies
are available. See the dated policy in [the execution guide](MASTER_GUIDE.md)
and `PROGRESS.json.executionPolicy`. Existing acceptance and hard edges remain; R04 additionally requires UT13.
G02/W0 remain open; G03 harness acceptance is recorded separately from future terminal parity.

1. [Current reconciliation and prior-roadmap coverage](MERGE_MAP.md)
2. [Codex App starter and resume prompts](START_IN_CODEX.md)
3. [Master execution guide, release waves and prior-roadmap mapping](MASTER_GUIDE.md)
4. [Product and architecture specification](00-product-spec.md)
5. [Verified repository baseline and integration anchors](BASELINE.md)
6. [All feature coverage](FEATURE_COVERAGE.md), [task DAG](TASK_INDEX.json), [progress ledger](PROGRESS.json)

## Subsystem implementation plans

| File | Focus |
|---|---|
| [01-foundation.md](01-foundation.md) | Execution foundation |
| [02-data.md](02-data.md) | Events, fields, time, queries and folders |
| [03-visualization.md](03-visualization.md) | Charts and linked dashboards |
| [04-workspace.md](04-workspace.md) | Workspace and usability |
| [05-insights.md](05-insights.md) | Patterns, comparisons and correlation |
| [06-automation.md](06-automation.md) | Automation, assertions, replay and CI |
| [07-docklight.md](07-docklight.md) | Docklight features and Protocol Lab |
| [08-ecosystem.md](08-ecosystem.md) | Sources, integrations, packs and sharing |
| [09-release.md](09-release.md) | Release and product validation |
| [10-message-decoders.md](10-message-decoders.md) | Real TShark/Lua decoders, nested streams and message inspector |
| [11-unified-terminal.md](11-unified-terminal.md) | Direct input in the existing log window: serial consoles, local shells and SSH |

[Primary sources and reuse references](REFERENCES.md). Each task also carries exact file targets, interfaces, behavioral checks, public automation operations and rollback. Read individual cards from `tasks/TASK_ID.md` after their shared contracts to keep Codex context focused.

Run `python validate_plan.py .` to validate packaged task IDs, dependency order, requirement ownership, local links and document structure. This validates the plan, not the product implementation.

## Decoder addendum and public tracking

Revision 2 is integrated as MD01–MD08 with 14 DEC requirements. All are planned; [the decoder contract](10-message-decoders.md) records dependencies, compatibility and missing runtime inputs. [Public tracking](PUBLIC_TRACKING.md) maps GitHub milestone issues without replacing the execution ledger.

## Unified terminal addendum

The [supplied terminal task](../../doc/CILOGG_UNIFIED_TERMINAL_IMPLEMENTATION_TASK.md)
is registered as UT00–UT13 and TERM01–TERM14 in the same ledger. Start UT00 only
after decoder-core acceptance; a preview-only slice does not complete that gate.
All terminal tasks remain planned. Embedded implementation prompts and proposed
dependencies/budgets are specification material, not work executed by this update.
