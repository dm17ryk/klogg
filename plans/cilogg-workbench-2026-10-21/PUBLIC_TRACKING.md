# Public issue tracking

GitHub issues summarize milestones; TASK_INDEX.json and PROGRESS.json remain the
execution authority. This proposal extends the 97-task/173-requirement merged
package with eight decoder tasks and fourteen acceptance requirements: 105 tasks
and 187 requirements. Decoder implementation is planned, not delivered.

| Tracker | Execution scope |
|---|---|
| [#37](https://github.com/dm17ryk/klogg/issues/37) | Prior P00–P24 roadmap and cross-surface umbrella |
| [#48 M00](https://github.com/dm17ryk/klogg/issues/48) | G01 reconciliation and retained public-readiness backlog |
| [#49 M01](https://github.com/dm17ryk/klogg/issues/49) | G02/G03, F01, V01/V02 and early usability |
| [#50 M02](https://github.com/dm17ryk/klogg/issues/50) | Structured data, sources and inspector foundations |
| [#51 M03](https://github.com/dm17ryk/klogg/issues/51) | Linked visualizations and dashboards |
| [#52 M04](https://github.com/dm17ryk/klogg/issues/52) | Investigations, workflows, assertions and CI |
| [#53 M05](https://github.com/dm17ryk/klogg/issues/53) | Advanced data and visualization families |
| [#54 M06](https://github.com/dm17ryk/klogg/issues/54) | Protocol Lab, Docklight and MD01–MD08 decoder delivery |
| [#61 decoders](https://github.com/dm17ryk/klogg/issues/61) | Dedicated MD01–MD08 checklist and decoder acceptance |
| [#55 M07](https://github.com/dm17ryk/klogg/issues/55) | Sources, integrations and reusable packs |
| [#56 M08](https://github.com/dm17ryk/klogg/issues/56) | Cross-platform and decoder-enabled release evidence |

The dedicated [message-decoder tracker #61](https://github.com/dm17ryk/klogg/issues/61)
is linked from #54. MD02/MD03/MD04 share
F/E/U foundations with #50, MD05/MD06/MD08 reuse E09 packs in #55, and MD08 supplies
capability-conditional release evidence to #56. Cross-links do not duplicate
implementation ownership or add reverse hard dependency edges.

## Status and reconciliation rules

- G01 is complete through merged PR58. Credit existing P01/P02/P03a–d/P04/P05 and
  P19b work through MERGE_MAP.md; P00's unmeasured performance gates stay open.
- G02a PR59 remains independently open at this integration. Its fixture/harness
  evidence is not full G02 completion. Preserve that evidence when both branches
  merge. G02/G03 remain the next execution work; decoder tasks do not jump them.
- The existing M00 issue's H01–H10 publication/readiness checklist is retained as
  a separate issue-level backlog. Those IDs are not in the reconciled task DAG;
  do not silently count them as canonical task cards or delete their obligations.
  Publication-only evidence does not complete broader product readiness.
- Old milestone bodies linked an unavailable plan path on the publication
  branch and claimed 107 cards/193 requirements. The branch itself still exists;
  its linked canonical path returns 404. Replace those links/count claims with
  the merged canonical package and this explicitly proposed decoder extension.
- Change checkboxes only with recorded task acceptance evidence. Preserve
  unrelated issue discussion, labels and milestone ownership. Do not close the
  umbrella or a milestone because a plan was imported.
- Public issue bodies and fixtures contain no supplied account/network payloads
  or raw logs; the source addendum is recorded privately by filename/SHA-256.

This documentation integration does not run decoder commands, TShark, GUI tests,
hardware operations or release publishing. Runtime and platform acceptance remain
the future task owners' responsibility.
