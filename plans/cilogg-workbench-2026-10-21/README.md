# CILogg Codex Implementation Plan

**Current workstream clarification (2026-11-07):** D01b [PR #98](https://github.com/dm17ryk/klogg/pull/98) remains the parent integration gate. The [D01c existing-owner adapter plan](slices/D01c-existing-owner-adapter.md) is prepared for review; currentPreparation stays D01b and nextPreparation points to D01c. Review its proposed OFF-by-default consumer and session-thread writer placement, then reconcile the frozen PR98 head before dependent implementation. Parent merge remains a D01c merge/activation gate. This planning change activates no runtime consumer, terminal input or endpoint. Formal MD01–MD06/W0 and F01/D01/UT01/UT02 gates and historical evidence remain unchanged.


Complete implementation package for a general developer logger, visualizer and automation workbench, including Docklight communication testing.

**Scope:** 119 implementation tasks and 201 mapped requirements. Imported and reconciled on 2026-10-22; terminal planning integrated on 2026-11-04. G01 is complete as documentation/source reconciliation. Product features are not implemented by importing this package.

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
