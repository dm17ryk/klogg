# Analysis through Commander, CLI and native MCP (P03d)

All five operations use `executeAnalysisOperation` and the application-owned
`AnalysisJobService`. Widgets supply only frozen source/scope descriptors. MCP
discovers the commands from normal CLI help and invokes the same Commander path.
A running CILogg instance is required, as for existing Commander operations.

```console
cilogg command --action regex_evaluate --json-file request.json --tab-id TAB_ID
cilogg command --action get_analysis_job --job-id JOB_ID
cilogg command --action get_analysis_results --job-id JOB_ID --page-size 200
cilogg command --action cancel_analysis_job --job-id JOB_ID
cilogg command --action release_analysis_job --job-id JOB_ID
```

`--analysis-json` accepts an inline JSON object as an alternative to `--json-file`.
Use files for larger inputs because OS command-line limits are smaller than the
16 MiB request-file bound. MCP `cilogg_execute` accepts `analysis_json` as an
object or JSON string and spools it into a temporary file for the duration of
the CLI call. Do not combine it with `json_file`. No worker executable override
is accepted from public requests.

Example inline-text request:

```json
{
  "pattern": "(?<number>\\d+)(x)?",
  "scope": "text",
  "text": "first 123\nsecond 456x",
  "requestRevision": "1",
  "options": {
    "caseSensitive": true,
    "boolean": false,
    "plainText": false,
    "exclude": false,
    "matchMode": "contains",
    "includeUnmatched": false
  },
  "limits": {"batchTimeoutMs": 2000, "timeoutMs": 60000}
}
```

Options are the capture-worker contract documented in
`regex-analysis-worker.md`: `matchMode` is `contains`, `whole_word` or
`whole_line`; Boolean plus whole-line mode is rejected. Unknown keys and wrong
types fail explicitly. Use a decimal string for `requestRevision` to preserve
integers above JSON's exact-number range. Limits can lower resource caps; the
maximums and effective defaults are returned by `get_analysis_job`.

File requests omit `text` and select `sample` (the default), `selection`,
`matches` or `whole_file`. Existing window/tab selectors apply. Sample output
is limited to 1000 lines / 4 MiB decoded text and is explicitly incomplete
when clipped. Inline text is capped at 2 Mi UTF-16 code units (4 MiB).

Selection freezes selected **physical rows**, including discontiguous Ctrl
selections and partial text selections. The last focused viewer is remembered
when focus moves to controls. Filtered-view selections map displayed rows back
to physical rows. Match scope uses only actual matching rows, excluding context
and marks. It requires a finished search against the current source revision;
append without a completed search refresh returns `stale_search`, including
when a maximum-match limit has stopped refresh. Existing search limits are
recorded in `sourceDescription` and completion remains limited. Empty selected
or matching sets are valid empty scopes. Sparse scopes allow up to 10000 ranges.

Decoding proceeds from the source prefix through the final requested row,
including skipped rows, to preserve stateful decoder state. Required prefix
reading counts toward record/time budgets; an oversized skipped-prefix record
can therefore stop a scoped run. Unselected suffixes are not read. Raw CR is
retained in CRLF input and LF separators are removed. These are physical lines,
before tab expansion, ANSI stripping, annotations or other display transforms.

Start returns a job ID immediately. Poll status until `completed`, `failed` or
`cancelled`; use `complete` and `reason`, not the terminal state alone, to decide
whether results are exhaustive. Pattern errors, timeouts, invalidation, I/O,
worker failures and resource limits have structured reasons. Status includes
effective limits, source identity/generation/revision/watermark, scope metadata,
progress, backend, compile/match/read/startup/delivery/backpressure timing,
`historical` and `newerDataAvailable`.

Every retained row includes its job ID, request revision and source provenance.
Physical `lineNumber` is one-based. Capture spans explicitly describe zero-based
UTF-16 offsets. Unmatched optional groups and matched empty strings remain
distinct. Boolean subpatterns retain their own captures and outcomes.

Results default to 200 records per page, clamped to 1..1000 and bounded below
8 MiB including the analysis envelope. MCP's text and structured representations
each contain that payload; its outer response is bounded by twice the payload
plus protocol overhead. MCP uses compact internal CLI output regardless of the
`pretty` argument to preserve the subprocess stdout bound. Pass the returned opaque `nextCursor` unchanged as
`--cursor` / MCP `cursor`; cursors belong to one job. Results may still be
produced while paging an active job. Expiry, release and global-storage eviction
return `job_expired`. Cancellation is idempotent while a job is retained.
Closing a source cancels its jobs without waiting on the GUI thread; inline
text jobs are independent of source/tab closure.

CLI failures retain nonzero exit status and emit structured analysis payloads
on stdout. Native MCP returns the same payload with `isError: true`. Source
bodies are not logged by default. The runtime registry holds at most 26 jobs;
finished metadata can be evicted even for zero-result jobs.

Verification: focused `analysiscommands` tests cover the service contract,
revision freshness, limit propagation, discontiguous viewer focus, expiry and
64 MiB global eviction while an active producer remains retained.
`tests/mcp/analysis_parity.py` runs public CLI/native-MCP parity using a separate
portable deployment and terminates only its own host and server processes.
