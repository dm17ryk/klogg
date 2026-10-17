# Log-analysis baseline and validation

Baseline source: `fec85fdf` (`origin/master`, 2026-10-15). Implementation branch: `codex/log-analysis-foundation`.

The first batch addresses existing viewer/indexing paths. Every subsequent public feature requires explicit command-line and MCP discovery, execution/state and regression coverage. Purely internal performance improvements apply to every existing caller of those paths.

## Reproduce the corpus

Run from the repository root in a clean PowerShell session:

```powershell
& .\scripts\benchmarks\generate-log-analysis-corpus.ps1 `
    -OutputDirectory .\build_root\analysis-corpus -Lines 100000 -LongLineKiB 256 -FileCount 64
```

Choose a fresh output directory on each generation. The script refuses to overwrite an existing directory. It generates ASCII, UTF-8 with Hebrew/emoji, BOM-prefixed UTF-16LE, long lines, JSON Lines, an incomplete final line, and a directory of small files. `manifest.json` records content hashes, byte sizes and expected ERROR/folder match counts; generated logs stay under the ignored build directory.

Smoke generation used 100 records, 1 KiB long-line payloads and two folder files. Verified: eight corpus files; ten ERROR lines; 100 JSON records; Hebrew/emoji round-trip; final incomplete record retained without a newline.

## First-batch regression and benchmark entry points

Use the Qt/VS2022 environment in AGENTS.md and the existing build/test scripts. Configure only inside `build_root`.

```powershell
& .\scripts\codex\build-windows.ps1 -Config RelWithDebInfo
& .\scripts\codex\run-tests.ps1 -Config RelWithDebInfo
```

Focused invocations from `build_root/output/RelWithDebInfo` after Qt deployment:

```powershell
.\cilogg_itests.exe '[selection][performance]' -platform offscreen
.\cilogg_itests.exe '[wrap][performance]' -platform offscreen
.\cilogg_tests.exe '[encoding]' -platform offscreen
.\cilogg_tests.exe '[wrap]' -platform offscreen
.\cilogg_tests.exe '[encoding][benchmark]' -platform offscreen
```

The hidden benchmark prints a `CILOGG_BENCHMARK` record with input bytes, sample count, median and p95 microseconds. It includes the same sample-copy and codec lookup costs in both baseline and updated runs; compare only the same machine/configuration and avoid measuring during a build. The ordinary regression suite has no timing-dependent pass/fail assertion.

The selection test uses an instrumented source and jumps during an active drag. It checks both individual-line and bulk source reads rather than relying on elapsed time. The updated behavior must issue zero source reads during that callback, preserve final text/counts, and normalize reverse partial-selection coordinates.

## Release budgets

- No selected-range reads or selected-text assembly in the mouse-move callback.
- Painted wrapping windows and hit-test caches contain only visible rows. Wrapping retains a resumable source offset and discovers additional rows in bounded slices, so the rest of a long record remains reachable. Width/font changes rebuild layout while restoring a source-column anchor; content updates preserve unchanged cached records.
- Extra viewport-layout/cache memory target: below 16 MiB per view on the generated corpus. Record a measured result before making a memory-performance claim.
- Interactive p95 target: below 100 ms on the recorded validation machine. Record hardware/build/backend and measurements before claiming this target passed.
- Encoding output must agree with legacy fallback for empty, ASCII, escape-based, NUL-bearing, invalid and truncated samples; BOMs take precedence; complete valid short UTF-8 must be recognized.
- Detection state must be independent across concurrent opens. Keep `TextCodecHolder` synchronization; only eliminate serialization that protects no shared mutable detector state.
- Whole-file indexing/search throughput and append-to-result latency must be recorded when new scan/aggregation services arrive in P03. Folder-search, chart-cardinality, parsed-cache and disk-cache limits are separate gates in their packages.

## Evidence log

Validation machine: Intel Core i7-10750H (12 logical processors), Windows x64, VS2022/MSVC 19.42, Qt 6.10.1, `RelWithDebInfo`, Qt offscreen platform. Unmeasured targets above are release criteria, not claimed results.

- Corpus hashes and manifest bytes match across `en-US` and `fi-FI` generation. No locale-dependent decimal formatting remains.
- Original short-UTF-8 and reverse-selection regressions failed against the baseline. The added strict huge-tail navigation test exposed logical-line centering that hid wrapped targets; the first batch checks a bounded centered layout and falls back to putting the target at the top.
- The first full run passed 172 unit cases / 6,777 assertions and 22 log-data cases / 1,649 assertions. Its sole widget failure was the strict wrapped-navigation assertion, subsequently fixed. Final rerun results follow below.
- Standard MCP smoke uses the existing `tests/mcp/smoke_stdio.py` with Python SDK 1.x (`uv run --with 'mcp<2' python ...`); SDK 2.x renamed result properties used by this existing harness. This validation does not change system Python or client configuration.
- Native CLI/MCP checks run against a uniquely named executable in `build_root/analysis-smoke-foundation` with separate portable configuration and `--multi --new-session`. The running user instance and COM captures are not used.

Final evidence (2026-10-15):

- Full application, grep, portable application and tests build: PASS. The first application post-build copy overlapped test processes and hit a locked `tbb12.dll`; the subsequent build completed after those processes exited.
- Full CTest: **4/4 PASS**, 391.19 seconds (`build_root/foundation-final-ctest.txt`). This includes all unit, log-data and widget tests, including strict huge-tail jump visibility. The subsequent localized grep decoding fix was separately rebuilt and verified by the Unicode protocol smoke below.
- Encoding benchmark, identical 280,000-byte UTF-8 sample / 32 calls:

  | Binary | Median | p95 |
  |---|---:|---:|
  | Baseline detector | 112,015 us | 173,893 us |
  | Updated detector | 341.2 us | 392.9 us |

  Raw records: `build_root/baseline-encoding-benchmark.txt` and `build_root/updated-encoding-benchmark.txt`. This measures detection including sample copy/codec lookup, not whole-file open/search throughput. It is not evidence that the interactive or memory budgets have passed.
- Existing native MCP smoke with its strengthened Unicode fixture: PASS (`build_root/analysis-smoke-foundation/mcp-verified/result.json`). Covers initialization, discovery, resources/prompts, open/search/follow/state, UI interaction, Unicode grep, installer dry-run and invalid action handling. Python MCP SDK 1.x was used; 2.x requires a separate harness API migration.
- Additional isolated parity check: PASS (`build_root/analysis-smoke-foundation/parity/result.json`). CLI toggles `textWrapAction`, MCP observes it and restores it, CLI confirms restoration; GUI detects UTF-8 and UTF-16LE BOM files; direct CLI and MCP grep preserve Hebrew/emoji. Compare line content with platform newline normalization, since Python subprocess text mode normalizes Windows CRLF.
- The Unicode check found a pre-existing grep display-codec mismatch: indexing detected UTF-8 but grep still decoded with its initial Latin-1 codec. Grep now applies the detected codec before search in Auto mode, matching GUI behavior; explicit configured encodings remain preserved. This is covered by the persistent Unicode MCP fixture.
- Independent general, C++ and Python smoke reviews: no outstanding blockers. `git diff --check` passed. The available clang-tidy setup lacks an MSVC compilation database and cppcheck lacks its standard configuration; no static-analyzer pass is claimed.
- CodeGraph synchronized after the final source changes. GitNexus refresh succeeded twice (latest successful index: 11,252 nodes / 32,241 edges); the final attempt timed out parsing the unrelated existing `codex_tasks/fix_preview_ehcp.py`, so it does not establish freshness for the last grep/Python-smoke changes. Full-text search remains unavailable, and no code-review-graph MCP is exposed. Initial class-impact results were conservative; refreshed EncodingDetector analysis is CRITICAL (four direct dependencies, 17 affected items across seven modules), while the localized grep `main` change is LOW (no upstream callers/processes). Final direct review and runtime parity cover those last changes.
- Hosted Windows/macOS/Linux CI and packaged visual inspection are not covered by these local offscreen results. Extra viewport memory and interactive p95 remain unmeasured P00 gates.

Sourcery follow-up (2026-10-17):

- The initial row cap hid long-record suffixes. The replacement uses incremental wrapping with continuation state, source-column navigation and a published scrollbar row snapshot. A 64 KiB record's suffix is reachable and selectable. Rapid Page Up/Down, filtered-view column reveal, Home during pending restoration, and append preservation have widget regressions.
- Each font-measurement probe examines at most 4,096 UTF-16 code units. Tests count all code units submitted to measurement and whitespace scans, including repeated probes; 1 MiB and 16 MiB inputs perform the same initial viewport work. A zero-width-character fixture verifies bounded work even when many characters fit in one pixel row.
- Background indexing extends 64 rows per timer callback and repaints only when the visible source anchor changes. A regression limits source rereads while the index grows. A pixel comparison protects horizontal scrolling in the shared drawing path.
- Inactive cached layouts are evicted above 64 records or an estimated 16 MiB. Current viewport and tail layouts are protected, so decoded source strings and their accumulated row indexes can exceed that budget for enormous active records. This is a best-effort cache policy, not a measured or strict per-view memory limit; the P00 memory gate remains open.
- The new navigation/drawing tests first reproduced four failures, then passed after the fixes: 67 assertions across eight focused widget cases. GCC 13 and Clang 23 accept the updated widget test with the complete CI warning sets; the existing literal-operator warning exemption described above is unchanged.
- Full application, grep, portable and test builds pass with MSVC/Qt 6.10.1. Full CTest passes 4/4 in 361.78 seconds (`build_root/pr36-sourcery-final-ctest.txt`). The updated wrapping unit test also passes the GCC warning check. Isolated native MCP/GUI smoke and CLI/MCP wrap, BOM-detection and Unicode grep parity pass (`build_root/pr36-sourcery-smoke`). Its first temporary deployment omitted `qoffscreen.dll`; deploying that plugin repaired the harness before the successful run.
- CodeGraph, code-review-graph and GitNexus were refreshed. GitNexus reports 11,316 nodes / 37,583 edges; full-text search remains unavailable. Staged change detection covers the expected six files, but conservatively attributes the shared-view change to all 300 indexed flows at critical risk. Independent C++/general reviews and the complete runtime suite supplement this imprecise flow attribution.

The feature controls used by the parity check are existing public routes, so this batch does not add a parallel API:

```powershell
cilogg command --action invoke_action --object-name textWrapAction --require-running
cilogg command --action get_ui --object-name textWrapAction --require-running
cilogg command --action open_file --file 'C:\logs\unicode.log' --require-running
cilogg_grep -e ERROR 'C:\logs\unicode.log'
```

Equivalent MCP calls:

```json
{"name":"cilogg_execute","arguments":{"action":"invoke_action","arguments":{"object_name":"textWrapAction"}}}
{"name":"cilogg_execute","arguments":{"action":"get_ui","arguments":{"object_name":"textWrapAction"}}}
{"name":"cilogg_cli","arguments":{"mode":"grep","arguments":["-e","ERROR","C:\\logs\\unicode.log"]}}
```

GitNexus method lookups were incomplete on the old index. Class-level analysis reported `AbstractLogView` CRITICAL (five direct dependencies, nine affected items) and `WrappedString` HIGH (one direct dependency, six affected items); no affected execution processes were resolved. EncodingDetector was LOW. CodeGraph and direct selection-signal consumers were inspected to supplement those results. Reindex/change-scope limitations must be reported separately from build/test evidence.
