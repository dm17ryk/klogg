# Log-analysis coverage and donor map

Implementation baseline: `origin/master` at `fec85fdf` (2026-10-15). Its product source matches the previously inspected `d980d1c`; only release metadata differs.

Every public addition requires documented, discoverable command-line and MCP operations, structured state/results, and parity tests. Presentation/workspace operations follow this rule too. Internal performance changes benefit the existing entry points and do not require a redundant public command.

This matrix distinguishes inspection from implementation. A proposed module is not evidence that a feature has shipped. Recheck the row when its work package begins.

| Capability | Current local evidence / gap | Proposed source | Package |
|---|---|---|---|
| Selection/wrapping performance | Eager selected-text reads in `AbstractLogView::mouseMoveEvent`; column-count wrapping; per-hit coordinate-vector allocation | ZEACENT design reference; adapt local code | P01 |
| Encoding detection | Per-call uchardet detector under a singleton-wide lock; simdutf already linked | ZEACENT design reference; local SIMD validation | P02 |
| Regex Lab | Regex backend exists; interactive capture/timing workbench needs a new service/UI | LogSquirl | P03-P04 |
| Value counts | Requires scoped capture aggregation and explicit cardinality/partial-result limits | LogSquirl | P05 |
| Linked charts | Requires extraction/aggregation, bounded rendering and source-linked points | LogSquirl | P06 |
| Recursive folder search | Requires streaming traversal and grouped source-linked results | ZEACENT | P07-P08 |
| Regex/JSON/logfmt tables | Preview decoding already exists; preserve it and add reusable typed format definitions | LogSquirl | P09/P12 |
| Timestamp/delta navigation | Requires explicit wall-time/uptime policies and ordered-range metadata | LogSquirl | P10-P11 |
| Field queries/sorting/CSV | Requires table source identity and bounded query/export jobs | LogSquirl | P13 |
| Value Names | Preserve raw search/copy semantics while adding annotations | LogSquirl | P14 |
| Retained searches | Retained result tabs and advanced-filter snapshot tests already exist; audit worker independence, live updates and automation before adding more | LogSquirl reference, extend current behavior | P15 |
| Filter management | Stable predefined filter IDs and versioned settings already exist | Both; preserve local schema | P16 |
| Tab groups | Tabs already have UUIDs and stream status; add group identities/persistence | ZEACENT | P17 |
| ANSI rendering | Needs raw/display span mapping and bounded state/checkpoints | ZEACENT / LogSquirl | P18 |
| QuickFind / discontiguous selection | Reuse current pattern/history/selection infrastructure | ZEACENT | P19 |
| Command palette | Reuse QActions and Commander metadata | LogSquirl | P20 |
| Command/stdin capture | Supervised process plugins and acknowledged host-managed capture files already exist | Adapt current SDK; LogSquirl behavior reference | P21 |
| zstd/lz4 | Audit current archive-library support before adding dependencies | LogSquirl | P22 |
| Persistent index cache | Existing filtered-search result cache is a different capability | LogSquirl | P23 |
| Git Team Folder | Existing configuration import/export is the integration point | LogSquirl | P24 |

Pinned reference snapshots from the supplied analysis: [LogSquirl `a88c4a4d`](https://github.com/64x-lunicorn/LogSquirl/tree/a88c4a4d) and [ZEACENT `61d16a58`](https://github.com/ZEACENT/klogg/tree/61d16a58). Rendering reference: [`be4bc966`](https://github.com/ZEACENT/klogg/commit/be4bc966971ab1827fa3acd3eeaa85bd96ac7c6a); encoding reference: [`225bb174`](https://github.com/ZEACENT/klogg/commit/225bb1747d8780932b0ec26bebd7902295f4b12d).

The first batch changes existing CILogg code directly; it does not copy donor source or introduce donor dependencies. Before copying code in a later package, record the exact source revision, file-level license/attribution, local adaptation, language/API requirements and redistribution notices in that PR. Beta labels and master-only features from the attachment remain unverified until their package starts.
