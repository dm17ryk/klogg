# P05 — bounded exact value counts

Status: merged through PR #45 (`ed2d70ba`). This remains the existing feature/regression contract. The [merged Workbench plan](cilogg-workbench-2026-10-21/README.md) reuses it through F09/V05; it is not a request to rebuild Value Counts.

Baseline: merged PR #44, `bd74c338`. Deliver P05 before P06 charts. Preserve C++17,
Qt compatibility and the Commander execution route used by GUI, CLI and native MCP.

## Contract

- `value_counts` accepts the existing analysis JSON/source selectors and a `counts`
  object: `capture` (number or name, default 0), `subpattern` (default 0),
  `caseSensitive` (default true), `missing` (`skip` or `include`, default skip),
  and `maxValues` (1..10000, default 10000).
- Count each occurrence of the selected subpattern on each emitted physical row.
  Missing/absent captures and absent subpattern matches follow the missing policy;
  a matched empty string is a separate value. Existing combined Boolean and scope
  semantics decide which rows are emitted. No implicit nonmatching-row inclusion.
- Case-insensitive aggregation uses Qt `QString::toCaseFolded()` and displays the folded
  key. Sort count descending, then ordinal UTF-16 value ascending, then missing.
- Percentages use accepted counted occurrences as denominator; skipped missing
  values are reported separately. Counts/denominators are decimal strings.
- Exact counting stops at the first rejected occurrence; no sampling, eviction of
  keys, or approximate counts. Partial status/reason accompanies every result page.
- Reuse frozen source watermarks, final-line bytes, worker deadlines, queue limits,
  source leases, global result storage, expiry, paging and cancellation. Runs are
  manual snapshots; appends become available to the next run.

## Implementation

1. `src/logdata/valuecountjob`: bounded reducer independent of QWidget and regex
   compilation; consume capture batches on the scan thread, retaining counts and
   the first source line/span. Account keys/output conservatively within the smaller
   pending/result byte budget and enforce the unique/result record cap.
2. `analysisjob`: opt-in reducer in the existing worker pipeline. Suppress raw row
   retention for count jobs; publish sorted aggregate rows on completion/limits.
   Keep per-batch aggregate progress under the job mutex. Cancelled jobs discard
   unpublished aggregates using existing cancellation semantics.
3. `analysiscommands`: strict count options, shared lifecycle/results, validated
   retained-row lookup and bounded atomic CSV export. Incomplete export requires
   explicit `allowPartial`; never present a partial result as a full-file count.
4. Commander/CLI/native MCP: `value_counts`, shared analysis lifecycle operations,
   `export_value_counts`, `apply_value_count`, and semantic count-dock operations.
   Derive literal filters from retained values, with regex escaping and Boolean
   mode disabled. Missing values have no literal text filter.
5. One Value Counts dock per MainWindow: pattern/capture/scope controls, Run/Cancel,
   bounded pages, exact counts/percentages, status/history, source navigation,
   apply-to-current-search and export. Tools/menu and distinct toolbar icon;
   stable object/accessibility names. Persist layout only; drafts/results runtime-only.

## Gates

Tests first: service fixtures for repeated/Unicode/optional/empty/Boolean captures,
deterministic ties, limits, paging, >10000 matching input rows, append/provisional
tails, cancellation and export. GUI/CLI/native MCP use the same fixtures and service.
Validate both accelerated and Qt-only builds, full CTest, packaged parity, C++ and
general review, refreshed code graphs and staged impact detection before PR.
P00 unmeasured performance budgets remain open; no automatic live aggregation or charts.
