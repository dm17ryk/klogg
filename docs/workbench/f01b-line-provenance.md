# F01b — original analysis-line provenance

Base: merged master `7a8889066665746f63430585a057dcee716529f1`, including
PR84/85/86. Branch: `codex/f01-line-provenance`.

The bounded reader already knew physical byte boundaries; this slice carries
them through the analysis processor input. See [the contract](../analysis-datasets.md).
It prepares source mapping for the future decoder and event/dataset contracts.
The shipping preview remains the JSON engine; this does not complete F01,
MD01 or W0, and adds no public result format or Commander operation.

`AnalysisRawBlock.records` contains at most 256 original record ranges. The
record end comes from the published index or frozen provisional watermark,
never the synthetic EOF+1 marker. Ranges include BOM/terminator bytes where
present. Exact terminators are separately copied from the bounded read; line
content and decoder state retain their existing behavior. UTF-8, UTF-16LE/BE,
UTF-32LE/BE and BOM-less forced UTF-16LE fixtures establish independent known
byte offsets, including a two-unit astral-character span. Other cases exercise
sparse selection, empty rows, skipped embedded NUL, forced single-byte decoding,
an appended frozen tail, rewrite/closure and record/batch bounds.

Source-backed lines receive the run's starting snapshot, codec, bytes,
terminator and provisional flag. Inline lines have absent physical provenance.
These immutable-by-convention published values hold weak source lifetime only;
they do not retain raw bodies or file leases. No result-store limit is bypassed.
At most eight terminator bytes accompany a descriptor. Unsupported delimiter
widths or contradictory raw delimiters fail explicitly without publishing
descriptors for the failed read.

`AnalysisLine::sourceLocation` bounds UTF-16 spans against the preserved text
and validates metadata before creating an origin anchor. It prevents creation
after source closure. A current snapshot's `accepts` check remains mandatory
before navigation; an origin anchor is allowed to describe historical data.
No per-character raw-byte mapping is inferred from a decoded offset.

| Check | Observed local result |
| --- | --- |
| Test-first API check | Failed because records/provenance/sourceLocation did not exist |
| VS2022 / Qt 6.10.1 / LTO `ci_build` | Passed |
| Strict Clang conversion/sign-conversion/shortening checks | Four translation units passed |
| New `[data][F01]` integration cases | 315 assertions / 5 cases passed |
| Full CTest | 7/7 groups passed in 264.56 seconds |
| Final general/C++ review | Approved; no actionable findings |
| Clang-tidy | Four translation units completed; only two pre-existing parameter-name warnings outside changed lines |

The full run includes smoke, benchmark harness, decoder proof, unit,
analysis/preview/regex/value-count integration, GUI and parity self-tests.
Runs used private SHA-256-verified deployed copies and private settings, without
changing installed CILogg sessions or private logs. An empty-file test setup
initially used a batch limit above the admission ceiling. Editing that fixture
during compilation left a stale object newer than its source, so an incremental
build incorrectly reused it. A later diagnostic edit forced fresh compilation;
the corrected fixture passed. Final logs are `build_root/f01b-focused.log`,
`f01b-full-ctest.log`, `f01b-final-syntax.log`, `f01b-final-native-build.log`,
`f01b-itests-diagnostic-build.log` and `f01b-runtime.json` (latest full-run copy).

Graph limitations persist: bounded GitNexus refresh timed out after 90 seconds;
its primary index is stale. Struct lookups returned LOW/zero callers, and the
reader/run functions were absent. Direct source review identified the scanner
and preview context reader, and the full regressions exercise both. CodeGraph
reports the worktree unindexed; code-review-graph is unavailable. These tools
do not establish a verified empty impact.
Cppcheck could not run because the installed tool's standard-library
configuration lookup is broken; no cppcheck-clean claim is made.

Qt5/cross-platform/backend CI and calibrated throughput/peak-memory/UI budgets
remain separate gates. Rollback removes consumers and optional metadata first,
then this reader metadata and anchor helper; existing source/location and public
analysis protocols remain the compatibility baseline.
