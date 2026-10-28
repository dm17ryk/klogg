# Value Counts

Open **Tools → Value Counts** or its frequency-bars toolbar button. The dock floats
and saves its layout with the window. Drafts, pasted text and results stay in memory.
GUI, CLI and native MCP use the shared bounded analysis service and Qt capture worker.

![Value Counts ranking severity values with frequencies and percentages beside the log viewer](images/value-counts-showcase.jpg)

Try the [sample investigation](../DOCUMENTATION.md#your-first-investigation) to
reproduce this workflow.

Choose a pattern, capture number (`0` means whole match) or named group, subpattern,
source and scope. The default sample is 1,000 physical lines / 4 MiB decoded text.
Selection uses selected physical rows, including discontiguous rows. Matches uses
actual rows from a completed search, excluding context and marks. Whole file is an
explicit snapshot. Pasted text preserves raw line separators.

Each occurrence of the chosen subpattern contributes a value. Boolean expressions
first decide whether a physical row qualifies. Captures stay separate by subpattern.
Missing optional captures and absent subpattern matches follow the missing policy;
matched empty strings are distinct. Nonmatching rows contribute only when explicitly
included. Aggregation case handling is independent of pattern case handling: Combine
case variants uses Qt `QString::toCaseFolded()` and displays the folded key.

Percentages divide by **accepted counted occurrences**, excluding skipped missing
captures. Counts and denominators are decimal strings in JSON. Sort order is count
descending, then ordinal UTF-16 value ascending; missing follows text at equal counts.
Counting stops exactly at the first rejected occurrence when a unique-value, memory,
execution or result cap is reached. Partial status/reason is visible; no estimates
or silent sampling are substituted.

Runs freeze generation, encoding, byte watermark and provisional final-line bytes.
Appends show newer data available; Run again to include it. Rewrites and closure
mark output historical and disable navigation. Editing the draft blocks applying
or opening older results. Apply selected value creates an escaped literal regex in
the current search tab, disables Boolean/inverse modes and preserves search context
and limits. Missing/empty values have no literal filter. Go to first occurrence
validates source provenance before navigating.

## CLI and native MCP

Save this request as `counts.json`:

```json
{"scope":"text","text":"level=INFO level=ERROR\nlevel=INFO","pattern":"level=(?<level>\\w+)","counts":{"capture":"level","subpattern":0,"caseSensitive":true,"missing":"skip","maxValues":10000}}
```

```text
cilogg command --action value_counts --json-file counts.json
cilogg command --action get_analysis_job --job-id JOB_ID
cilogg command --action get_analysis_results --job-id JOB_ID --page-size 200
```

Wait for terminal state before reading final counts. Pages carry job/revision,
provenance, completion/reason and opaque `nextCursor`; continue with `--cursor`.
Rows expose stable zero-based rowIndex, value/null, missing, count, percentage,
denominator, first one-based lineNumber and zero-based UTF-16 spans.
`cancel_analysis_job` is idempotent; `release_analysis_job` frees results.

`get_value_count`, `apply_value_count` and `open_value_count` accept analysis JSON
`{"jobId":"JOB_ID","rowIndex":0}`. Apply targets the current/selected search tab;
Open navigates the retained source tab. An explicit selector must belong to the
source's window for Open. Inline text has no viewer location.

`export_value_counts` accepts
`{"jobId":"JOB_ID","filePath":"ABSOLUTE_PATH.csv","allowPartial":false}`.
UTF-8 CSV export is atomic and bounded, quotes fields, and includes source identity
and completion/reason. It rejects the source file as destination. Partial export
requires explicit `allowPartial:true`. Values are preserved verbatim, including
formula-looking strings; import as text in spreadsheet applications.

Native MCP discovery exposes every operation through `cilogg_commands`. Call
`cilogg_execute` with action `value_counts` and arguments `{"analysis_json":REQUEST}`.
The same transport serves retained-row/export operations. Dock operations are
`show_value_counts`, `get_value_counts`, `set_value_counts`; set takes a `definition`
with pattern, scope, text, sourceTabId, options, counts, limits, visible/floating,
and operation `run/cancel/release/first_page/next_page/apply/open/export`.
Apply/Open accept global rowIndex; export takes filePath/allowPartial. All accept
existing window/tab selectors. Numeric capture selectors mean indexes; strings
mean exact names, even `"0"`. Editing the GUI field interprets numeric text as an index.

## Limits and verification

Default maximum: 10,000 unique records with a conservative aggregate-byte budget
within the smaller pending/result cap (normally 4 MiB). Existing queue, source-read,
worker deadline, global 64 MiB result storage and ten-minute expiry limits apply.
Cancelled jobs discard unpublished aggregates. Source bodies/values are not logged.

Service/window fixtures cover repeated/Unicode values, missing/empty groups,
Boolean subpatterns, zero-width matches, exact caps, pages, more than 10,000 input
matches, provisional tails, CSV, literal filters and stale-source navigation.
Packaged parity runs use both accelerated and Qt-only builds:

```text
python tests/mcp/analysis_parity.py --executable PATH_TO_CILOGG_PORTABLE --artifacts OUTPUT_DIRECTORY --auto-responses --regex-lab --value-counts
```

Unmeasured P00 budgets remain open. Automatic live aggregation and P06 charts follow.
