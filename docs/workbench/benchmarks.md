# Workbench benchmarks (G02)

G02a supplies deterministic domain fixtures, a bounded benchmark runner and real
frozen-analysis observations. G02b adds native event dispatch, scroll CPU painting
and direct service cancellation measurements. [G02c](g02c-memory-gates.md) adds
Windows process-tree peak committed memory, including exited regex workers.
[G02d](g02d-resident-memory.md) adds opt-in Windows host/worker working-set
sampling, with explicit sampled-peak and non-atomic accounting boundaries.
Full G02 remains in progress:
presentation latency, worker peak resident memory, sustained memory slope,
OS-cache cold runs, native Linux/macOS and optional renderer overhead need later
measurement slices. A passing scenario is not a full release-performance pass.

## Generate and verify

Requires Python 3.9+ and its standard library. No application, network connection,
device, parser profile or project is needed to generate fixtures.

```powershell
python scripts/benchmarks/workbench-benchmark.py --generate-corpus build_root/workbench-corpus --rows 100000 --seed 42
python scripts/benchmarks/workbench-benchmark.py --verify-corpus build_root/workbench-corpus
```

Destinations must be new. Generation never overwrites an existing directory.
The manifest includes generator version, seed, row counts, encoding, byte sizes,
SHA-256 hashes and explicit correctness oracles. Validation limits manifest size
to 2 MiB, 1,000 file entries and 128 MiB total input; scalable fixtures allow up to
1,000,000 rows, subject to that run-time byte cap. Larger future workloads need an
explicitly reviewed limit/profile change. File paths are verified inside the
corpus; the measured ASCII fixture must be listed and hashed. The working copy is
hashed again before opening it, so a changed corpus cannot acquire a false hash.

Fixtures cover ASCII, Hebrew/emoji, UTF-16LE/BOM/CRLF, UTF-8 BOM/CRLF, long records,
dense/high-cardinality values, exact large JSON integers, logfmt, quoted multiline
CSV, Java/.NET/Python/JavaScript stack traces, web requests, build/tests, ETL,
frame-time spikes/heap/RSSI, boot/clock resets, binary RX bytes and malformed or
unterminated records. These are future parser/transport acceptance inputs;
generating them does not implement those product capabilities.

Tiny references live in `test_data/workbench`. Its local `.gitattributes` preserves
exact bytes across Git checkouts and excludes only these tiny fixtures from LFS
filters. Large generated corpora and run artifacts stay in ignored `build_root`.

The original PowerShell generator remains compatible. Its opt-in bridge adds a
`workbench` child corpus without changing the original files or manifest:

```powershell
./scripts/benchmarks/generate-log-analysis-corpus.ps1 -OutputDirectory build_root/combined-corpus -Lines 10000 -LongLineKiB 256 -FileCount 8 -IncludeWorkbench -Seed 42
```

`-PythonExecutable` selects a particular interpreter; default is `python`.
Without `-IncludeWorkbench`, no Python invocation or Workbench output is added.

## Run frozen-analysis measurements

Use a complete, flat deployed application payload. Current native measurement
evidence is Windows; Linux/macOS packaging and native performance remain pending.
The CI matrix runs protocol doubles and corpus/gate tests on all three platforms.

```powershell
python scripts/benchmarks/workbench-benchmark.py --executable PATH_TO_CILOGG_EXE --corpus build_root/workbench-corpus --scenario analysis --repetitions 3 --output build_root/workbench-run --metadata build_root/build-metadata.json --verbose
```

Metadata is an optional bounded JSON object recording independently verified
build/SDK/backend details and how they were obtained. Missing SDK and accelerated
search-backend information is labelled unreported, never inferred from a path.
The report records actual executable/hash/version, harness module hashes, machine
architecture/processor count, Python version, manifest hash/seed, sample timings,
service metrics, source provenance, capture backend and the evaluated budgets.
Unknown build details remain distinct from supplied metadata. Python executables
used as protocol doubles are always labelled `evidenceKind: test_double`.

The runner copies the executable and deployment files into a uniquely named
private payload, isolates settings, disables session restoration/update checks,
and starts its own host/MCP server. It never connects to, stops or alters the
user's existing monitor or ports. Windows uses a kill-on-close Job Object; POSIX
uses owned process groups. Timeout/failure cleanup kills and reaps only owned
processes. Logs and the copied payload remain available in the run directory.

Each repetition opens a private copy of the verified ASCII fixture, freezes its
analysis snapshot, appends to the copy, checks scanned rows/byte watermark and
newer-data publication, checks exact ERROR counts with Value Counts, and cancels
a running pathological regex workload. Jobs are released after use. A source
that never becomes ready, a partial result, count mismatch, protocol failure or
missed deadline fails the scenario. Protocol messages are capped at 8 MiB and the
reply/write queues at eight messages each. A supervised writer keeps blocking
pipe writes inside the request deadline; cleanup reaps owned processes even when
the peer stops consuming requests. A snapshot already terminal before append
fails the coexistence gate; choose a larger corpus rather than accepting an
unmeasured active-scan check. Routine logs record IDs, counters, limits and
termination reasons, not source bodies or arbitrary expressions.

## Metric boundaries and initial operational gates

| Field | Meaning / gate |
|---|---|
| `jobSeconds` | 120-second upper bound on a public job observation phase; configurable up to 600. Native analysis retains its own stricter limits. |
| `publicCancellationSeconds` | 10-second end-to-end cancellation/terminal-observation bound, matching existing public analysis parity tests. |
| `appendSeconds` | 5-second bound on observing newer-data publication, including protocol round trips. |
| `hostPeakBytes` | 512 MiB host-only peak working-set cap for this synthetic profile. Worker/harness RSS is not included. Unavailable platform measurements are labelled unavailable. |
| `indexAndSubmissionSeconds` | Includes opening/readiness observation, worker startup and protocol overhead; not a pure index benchmark. |
| `scanSecondsIncludingProtocol` | Frozen scan wall time including readiness/protocol work; service read/match/startup metrics are reported separately. |
| `commanderRoundTripSeconds` | Public MCP→CLI→GUI response samples; not GUI event or paint latency. |
| `processState` | First repetition is a cold host process; subsequent repetitions share the warm host. This says nothing about the OS file cache. |

`--budgets FILE` may override known numeric gates. Unknown, Boolean, negative,
nonfinite and out-of-range values are invalid setup. A deliberately slowed
protocol fixture must fail the execution gate with exit 1. Changes to budget
values are recorded in every report, not hidden in the runner.

Ruling from the first native run: MCP starts a CLI process per command. Applying
the service's one-second cancellation target to several such round trips was an
incorrect measurement boundary. The harness therefore uses the published
ten-second public bound and keeps **direct service cancellation <1 second** and
**GUI p95 <100 ms** open. Neither target has been weakened or declared passed.
The failed initial experiment is retained locally with its structured report.

Exit codes are **0** for accepted scenario gates, **1** for runtime/gate failure
or interruption, and **2** for invalid setup. `result.json` has explicit
state/reason and `releaseGateStatus: pending`, including successful scenarios.
No throughput regression threshold is claimed by these observations; G02 must
calibrate and freeze the remaining performance gates before feature-release claims.

## Verification and next slices

Timing uses Python's monotonic high-resolution `perf_counter`, including request
deadlines. Older Windows Python versions can use a coarse `monotonic` clock,
which reported zero elapsed time for fast protocol fixtures. If a duration still
cannot be resolved, throughput is `null` with `scanTimingStatus:
below_clock_resolution`; the harness does not invent an epsilon duration or rate.
Tests force both coarse and unresolved clocks in isolated subprocesses. Historical
observations below retain the hashes and timings of the harness actually used.

```powershell
python -m unittest discover -s tests/benchmarks -v
python scripts/benchmarks/workbench-benchmark.py --verify-corpus test_data/workbench
```

Tests cover reproducibility/oracles, overwrite refusal, byte tampering, manifest
scope/types, invalid budgets, structured failures, slow fixtures, malformed
protocol (including wrong-shaped valid JSON), a nonreading peer, active-scan
requirements, timing boundaries, cold/warm labels and the PowerShell compatibility bridge. CTest registers
the suite when Python 3.9+ exists. The dedicated workflow checks Windows, Ubuntu
24.04 and macOS 15, plus Python 3.9 on Ubuntu. These self-tests do not substitute
for native platform measurements or current-source C++ CTest results.

G02b adds the opt-in GUI/service scenario below. Worker and long-run memory,
native Linux/macOS evidence, and renderer comparisons remain later slices.
G03 can independently consume the fixture/report contract.

## GUI and direct service measurements (G02b)

Build and deploy both the application and `cilogg_itests` from the same source
and configuration into one flat directory. The runner copies that deployment to
a private directory and owns the integration process and its worker descendants.
Existing application instances and their settings are not used.

```powershell
python scripts/benchmarks/workbench-benchmark.py --scenario ui-service --executable build_root/output/RelWithDebInfo/cilogg.exe --test-executable build_root/output/RelWithDebInfo/cilogg_itests.exe --corpus build_root/workbench-corpus --output build_root/workbench-ui-run --samples 100 --repetitions 3 --metadata build_root/build-metadata.json
```

The existing integration executable has a hidden `[workbench-native]` case,
explicitly excluded from ordinary Windows widget CTest selection. Timing gates
are opt-in; ordinary CI compiles the probe and tests the harness with protocol
doubles. These doubles are labelled `evidenceKind: test_double`.

The probe opens the fingerprinted ASCII fixture through real `LogData` and paints
an `AbstractLogView`. In each of two contexts (idle and supervised analysis active)
it discards ten warmups, then collects 100–1,000 samples. Each sample times a
posted Qt callback and completion of a changed scroll position's synchronous
viewport repaint. These are **event dispatch** and **queued-to-CPU-paint completion**
times, respectively; offscreen painting does not measure compositor presentation
or hardware input. `--platform native` uses the platform plugin instead of the
default offscreen plugin; it still does not measure presentation.

The worker evaluates a fixed synthetic input (100,000 `a` characters followed by
`!`, with optimization disabled for the `a+b` pattern). The viewport continues
to read the verified ASCII fixture; this scenario measures coexistence, not
whole-file analysis throughput. An initial viewport paint must finish before
warmups begin.

The analysis context must remain active throughout sampling; completion before
the last sample fails the scenario. Cancellation is measured directly from
`AnalysisJobService::cancel` through its terminal `cancelled` state, including
supervisor cleanup, followed by successful real-worker recovery. Acknowledgement
latency is reported separately. Reading/indexing, worker startup, process launch
and JSON delivery are outside these timing boundaries.

| Gate | Default and interpretation |
|---|---|
| `guiEventP95Ms` | Strictly less than 100 ms, worst of the two contexts. |
| `scrollPaintP95Ms` | Strictly less than 100 ms, worst of the two contexts. |
| `serviceCancellationMs` | Strictly less than 1,000 ms to terminal state. |
| `nativeSuiteSeconds` | 120-second process deadline, including setup and recovery. |

Percentiles use nearest rank `ceil(0.95 * N)` on all retained measured samples.
Raw samples, Qt compile/runtime/platform identity, source hash/size/physical rows,
test binary hash and per-context summaries remain in the report. Reports with
missing, nonfinite, mismatched or incomplete measurements fail explicitly.
Every repetition uses a new integration process; the OS cache is uncontrolled.
Custom budgets are recorded, as in the analysis scenario. Deliberately slowed
and timed-out protocol fixtures verify failure gates, not native performance.

The scenario always retains `releaseGateStatus: pending`. Full G02 still needs
worker RSS, sustained memory, cache conditions, platform and renderer comparisons;
CPU painting does not close the separate presentation-latency question.

## Windows GUI/service observations (2026-10-24)

[Raw measured report](g02b-windows-observations.json): fresh Qt-only MSVC2022 x64
RelWithDebInfo build with Qt compile/runtime 6.10.1. Product sources are from
`4deed897`; G02b adds the integration probe. Application and test binary hashes
pin the measured executables. The requested `KLOGG_USE_LTO=OFF` flag is recorded;
MSVC still used LTCG for inherited `/GL` objects, as noted in supplied metadata.

All three new integration processes used the native `windows` platform plugin
and the same 100,000-row, 3,398,964-byte viewport fixture. Each context retained
100 samples after ten warmups. All three gates passed in every repetition.

| Repetition | Event dispatch p95, ms | Scroll CPU paint p95, ms | Cancellation to terminal, ms |
|---|---:|---:|---:|
| 0 | 0.151 | 15.550 | 32.437 |
| 1 | 0.163 | 18.677 | 31.820 |
| 2 | 0.135 | 11.688 | 27.524 |

Each p95 is the worse of idle and analysis-active contexts. The matching batch
remained active during every sample; each cancellation was followed by successful
real-worker recovery. All four owned process roots were reaped with zero cleanup
failures. An additional offscreen run passed after fixing readiness: the first
experiment had correctly rejected a scroll sample taken before the initial
viewport paint. Its failed report and logs remain under `build_root`.

Offscreen Windows emitted a missing-font-directory warning, so the native Windows
runs above are the recorded baseline. No OS-cache, compositor presentation,
accelerated-search, worker-memory, sustained-load or cross-platform native result
is inferred from these measurements. `releaseGateStatus` remains `pending`.

## Initial Windows observations (2026-10-22)

For the separately measured long-lived analysis/release workload, see
[G02f sustained memory](g02f-sustained-memory.md). It uses the existing Commander
operations and records actual post-release host RSS trends and sampled-tree
observations; it does not replace the native UI/service scenario or complete W0.

[Raw measured report](g02a-windows-observations.json): deployed package 26.04.2.1043,
reported source 96bf481, Qt runtime DLL 6.12.0.0, capture backend Qt QRegularExpression.
This measures that deployed binary, not a fresh build of master. SDK and
accelerated-search backend were unreported. The report pins binary, harness
modules and the corrected seed-42 manifest hashes.

All three repetitions scanned exactly 100,000 original physical rows (3,398,964
bytes), validated 10,000 ERROR counts, and observed appended data while the frozen
run was active. Public scan wall times were **5.430 / 5.309 / 5.276 seconds**;
cancellation including MCP/CLI round trips was **1.935 / 1.376 / 1.716 seconds**.
Host peak working sets were **87.91 / 97.25 / 97.98 MiB**. These are observations
across a shared host, not evidence of a sustained-memory plateau. Service
read/match/startup counters are retained separately in the JSON.

One cold-host and two warm-host samples do not establish a p95 or a regression
threshold. The OS cache was uncontrolled. GUI latency, direct service cancellation,
worker memory, native Linux/macOS and renderer comparisons remain pending. All
three owned roots (version probe, host and MCP server) were reaped after the run;
detailed local logs/payload remain under `build_root/g02-final-run`.

General/Python reviews found five distinct issues (blocking writes, repeated BOMs,
opening excluded from timing, acceptance after terminal scan and malformed JSON
shapes). Failing regressions reproduced them before corrections. The final suite
passes 18 tests, including invalid metadata/oversized budgets and stalled-peer
cleanup. Workflow lint and exact fixture verification pass. Hosted CI and fresh
current-source C++ CTest are separate, pending evidence. Code Review Graph was
refreshed; GitNexus refresh failed after exhausting parser worker retries, and
CodeGraph reports the integration worktree unindexed.
