# Analysis source identity (P03a)

This internal foundation implements the first P03 slice from
`plans/log-analysis-roadmap.md`, tracked in issue #37. It adds no public operation.
P03b adds bounded scan jobs, P03c adds capture extraction and execution budgets,
and P03d adds discoverable Commander/CLI/MCP operations before public consumers.

## Contract

`LogData::sourceSnapshot()` returns provenance and indexed watermarks under the
same shared lock used by the index. It does not copy log text or pin file bytes.

- `sourceId` is a UUID for one open `LogData`, independent of its path. Two opens
  of the same path have different identities.
- `generation` increases when the index is cleared, a reload is requested,
  content changes are detected, or display decoding/prefilter interpretation
  changes. It can advance more than once during one reload. Compare equality,
  not an assumed increment of one.
- `revision` increases during indexed updates and publication. An append keeps
  the generation but changes the revision. A snapshot is available only after
  a successful indexing operation; progress batches are not complete snapshots.
- `indexedBytes` is the raw byte watermark, not a character count.
  `lineCount` includes the provisional final line. `completeLineCount` excludes
  it, and `provisionalLineStart` records its starting byte offset when present.
  The existing index's synthetic EOF+1 terminator never becomes a byte watermark.
- Physical line numbers are zero-based. Optional raw-text spans use UTF-16 code
  units in the decoded `QString`, before tab expansion or presentation changes.
  The extractor must validate spans against its decoded line; this contract
  rejects arithmetic overflow but does not read text to validate span bounds.
- Each update has a publication token. A superseded operation cannot publish
  an available watermark for a newer reload. Closing explicitly marks the
  lifetime token closed, even if a consumer retained a strong token reference.

Create locations with `snapshot.location(line, optionalSpan)`. A **current**
snapshot accepts locations from completed lines of an earlier revision in the
same generation. Locations in a provisional line require the exact revision:
when bytes are appended, consumers must replace that line's contributions
starting at the previous provisional byte offset, rather than count it twice.
Generation changes reject all previous locations. Closed sources reject locations.
Location provenance is private and exposed through read-only accessors; callers
can adjust the optional span, but cannot relabel an old provisional result as a
completed-line result. Acceptance also checks current completion status and span
overflow.

Use `LogData::isSourceSnapshotCurrent(snapshot)` before and after reading a scan
batch; reject/retry the batch on a revision mismatch. For navigation, obtain a
fresh snapshot and call its `accepts(location)`. An old snapshot is a value and
does not update itself when the source changes. Checks do not provide a locked
read transaction: P03b must bracket reads and validate batches. Filesystem changes
are detected through the existing watcher/content-check policy, including its
optional sampled fast-modification mode; this is not an immutable filesystem
snapshot or a guarantee of detecting unnotified writes.

## Integration and diagnostics

`IndexingData` owns identity, lifetime, generation, revision and publication state.
`IndexOperation::doIndex()` starts an update and publishes only its own token
after successful completion. Interruptions, failed opens and invalid indexes
remain unavailable. `LogData` invalidates synchronously on explicit reload and
decoding/prefilter changes. A changed file ID forces full reindexing even when
replacement bytes are identical; normal same-file appends keep their generation.
The file-change status now has an explicit initial `Unchanged` value.
Data-read failures and incomplete header/tail hash reads discard the index and
report unsuccessful completion. Hashes cover only the consumed byte watermark.
Encoding changes that require a new line layout mark the source unavailable
inside the same lock as the codec change. File checks carry a reload epoch;
superseded callbacks cannot schedule partial indexing, and pending full reloads
take priority over watcher checks and partial updates. A newer explicit full
reload can still replace an older pending full reload.

Debug diagnostics record source UUID, generation/revision, update token,
publication/rejection reason, byte/line watermarks and provisional status without
logging record bodies. No settings, plugin protocol or legacy search-generation
schema changes are introduced.

The provisional-line fixture exposed an existing UTF-16 tail decoding error:
the index uses a one-byte synthetic end marker, but decoding subtracted the
codec's two-byte newline width and lost the last code unit. Raw batches now
identify provisional tails, subtract only the synthetic marker in that case,
and read no bytes past the indexed watermark. A short source read discards the
entire raw batch, including its line offsets, so text decoding and search cannot
use stale offsets or unread padding. Decoding also validates each buffer range.

## Validation and rollback

`tests/ui/logdatasource_test.cpp`, tagged `[logdata][source]`, covers complete and
unterminated appends, repeated partial-line extension, UTF-16 byte watermarks and
Unicode span units, same-size rewrites, truncation, identical-byte replacement,
forced reindex/decoding changes, prefilters, distinct opens, retained lifetime
tokens after close, empty/interrupted loads and superseded publication.
The `[review]` cases add short raw batches, invalid locations, queued stale
file-check callbacks, repeated forced encodings, and injected data/hash failures
for both full and partial indexing, including short-positive and EOF hash reads.

Run `cilogg_itests.exe '[source]' -platform offscreen`, then the complete local
CTest suite. This internal slice can be reverted independently while no analysis
consumer depends on it. No latency or memory target is claimed by these metadata
correctness tests; the remaining P00 measurement gates are unchanged.

Evidence (2026-10-18, based on merged master `6c09d654`):

- MSVC 19.42 / installed Qt 6.10.1 / Windows x64 `RelWithDebInfo`: application,
  portable application, grep, unit and integration binaries build successfully.
  Hosted Windows Qt 6.12 and retained macOS/Linux validation remain separate.
- Focused `[source]` tests pass: 223 assertions across six cases. The initial
  UTF-16 tail check failed with two UTF-16 units instead of three before the
  reader fix; final UTF-16 and UTF-32 fixtures preserve all text and search bytes.
- Full CTest passes 4/4, 324.08 seconds (`build_root/p03a-full-ctest.txt`).
- Isolated native MCP smoke and CLI/MCP wrap, BOM detection and Unicode grep
  parity pass (`build_root/p03a-smoke`); the isolated application closed cleanly.
- GCC 13 and Clang 23 syntax checks accept the changed C++ with CI warning sets;
  the local checker retains its existing nonfatal Clang literal-operator spelling
  warning in unchanged `linetypes.h`.
- Independent C++ and general reviews have no remaining findings. CodeGraph,
  code-review-graph and GitNexus were refreshed. GitNexus reports 11,399 nodes and
  49,616 edges; full-text search remains unavailable. Its change mapping
  conservatively attributes shared symbols to all 300 flows, so direct review
  and complete runtime validation supplement that attribution.

This slice was implemented locally without copying donor source or adding dependencies.

Sourcery review follow-up (2026-10-18):

- Before the fixes, the short-read, malformed-location and queued-reload
  regressions failed eight assertions across three cases.
- Final `[source]` coverage passes 524 assertions across 11 cases, including
  injected negative data reads and negative/short-positive/EOF hash reads in
  full and partial indexing.
- Full local CTest passes 4/4 in 420.32 seconds
  (`build_root/pr38-review-full-ctest.txt`). The application, portable application,
  grep and both test binaries build with MSVC/installed Qt 6.10.1.
- Isolated native MCP smoke and CLI/MCP wrap, encoding detection and Unicode
  grep parity pass; the isolated application closes cleanly. Independent C++
  and general reviews have no remaining findings.
