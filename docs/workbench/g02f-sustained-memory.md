# G02f: sustained analysis and release memory

The earlier benchmarks measure individual scans and native UI/service bursts.
G02f adds a long-lived application host that repeatedly analyzes an appending
source and releases its results. It uses the existing Commander operations over
native MCP; it adds a developer benchmark CLI, not a new product command or
settings schema. The existing benchmark scenarios and their defaults stay intact.

```powershell
python scripts/benchmarks/workbench_sustained.py --executable build_root/output/RelWithDebInfo/cilogg.exe --corpus build_root/g02d-corpus --output build_root/reports/sustained-memory --debug
```

Generate the corpus through the existing `workbench-benchmark.py --generate-corpus`
entry point; record the same seed, row count and manifest hash for comparisons.
The measured local corpus has 100,000 ASCII rows, seed 42. Output must be a new
directory. Defaults are three independent hosts, each with three warmup cycles
and at least 120 seconds of measured work. `--duration-seconds` accepts 10–600
and `--repetitions` accepts 1–3. Short runs still require at least ten released
samples; a requested duration alone cannot turn insufficient observations into
a pass. The measurement can overrun its requested duration to finish its current
cycle; every run has a separate absolute duration-plus-120-second transport
deadline and the existing owned-process cleanup bounds.

## Workload and oracles

Each repetition copies and verifies the immutable corpus into its own file,
payload and profile. It starts the real application offscreen and waits until
the source finishes its initial index publication before appending anything.
This matters because file watching is registered after initial indexing.

Each cycle appends exactly sixteen LF-terminated `G02F_TOKEN` rows, then runs
both a no-match whole-file regex and a whole-file value count. Appends occur
between scans, not concurrently with them. The runner verifies:

- Complete terminal jobs, exact scanned physical rows and frozen byte watermarks.
- Stable source ID and generation across all cycles, with result-page provenance
  and request revision bound to the completed job.
- Empty regex results; exactly one counted value with its exact decimal-string
  count and no continuation cursor. Additional published count metadata is valid.
- Explicit release of both jobs, `job_expired` for each old ID and no jobs left
  in the host's tracked set before a released-memory observation.

A source-not-ready response or older published watermark is bounded by a fixed
15-second readiness phase per action, with submission, status and stale-job
cleanup requests capped by its remaining time. A stale admitted job is cancelled and released before
another snapshot is requested. Other failures stop the run. Per-job finishing is
bounded to thirty seconds and intersected with the run deadline. Source bodies
are not logged; debug diagnostics identify actions, jobs, source generations,
watermarks, expected counters, readiness decisions and termination reasons.

## Memory accounting

The existing Windows Job Object backend queries verified owned members. Samples
occur between Commander calls, during observable job polls and during paced idle
time. This is not a continuously running sampler. Actual gaps and query durations
are retained; Windows membership/working-set reads are sequential and non-atomic.
Shared pages count once per process, and inter-poll peaks or short-lived workers
can be missed. The process tree includes the application, native MCP server and
observed worker descendants, excluding the Python harness. Sampling starts during
the first warmup's matching observer, after initial source readiness, and ends
before cleanup. Opportunistic probes have a 100 ms minimum spacing; released and
final observations are forced. This is not a periodic 100 ms sampling guarantee.

The measured interval starts after the warmups, at the first baseline observation
with all results released. Each subsequent cycle adds another released observation;
the final observation must extend the interval to the requested duration.
The least-squares slope uses actual elapsed seconds, preserves decreases, and is
reported in bytes per minute. It is an observation, not a calibrated slope budget.

Two explicit regression guards apply independently:

| Guard | Initial default | Meaning |
|---|---:|---|
| Sampled owned-tree RSS | 512 MiB | Same starting ceiling as G02d, applied to this separately identified workload. |
| Final minus baseline released-host RSS | 32 MiB | Initial allowance for retained host growth while its growing source remains open. |

`--max-tree-rss-bytes` and `--max-host-growth-bytes` override these positive integer
ceilings, up to 16 GiB. They are regression guards, not capacity guarantees or
proof that the application cannot leak. In particular, working-set eviction,
allocator caching and the deliberately growing source affect the observations.
Hours-long growth, exact worker peak RSS and controlled cache runs remain open.

There are at most 100 measured cycles, 8,192 samples per repetition and a 4 MiB
result report. The reused sampler retains its 256 observed-PID ceiling. Exhausted
limits, missing host measurements, query errors, incomplete duration, invalid
numbers, incorrect results and surviving owned processes fail explicitly. Failed
runs retain partial observations and process-cleanup status. Unsupported operating
systems are setup failures, never zero-memory passes. Python executables are
labelled `test_double`, including uppercase `.PY`. Every report keeps
`releaseGateStatus=pending`.

## Evidence and rollback

[Windows observations](g02f-windows-observations.json) record final repetitions,
the deliberate one-byte RSS-budget failure, full-suite results and exact hashes.
They separate the initial 60-second accepted smoke epoch from later cleanup and
review refinements. The first failed smoke is retained: it appended before initial
indexing completed and correctly refused the old watermark. A readiness regression
test failed before the observable gate was added.

Local runs reuse the approved Qt 6.10.1 Qt-only deployment with its original
embedded build stamp and validated source epoch. Current master also contains
the later branding assets; this measurement is not a fresh branding rebuild,
installer check or hosted Qt 6.12/accelerated acceptance. No C++ or resource bytes
change in G02f. Standalone benchmark CI/CTest discovers its Python tests through
the existing suite; the sustained application workload remains opt-in.

Full G02/G03 and W0 remain open. The execution order remains W0, then decoder core
MD01–MD06 with its prerequisites, then remaining W1; MD07–MD08 stays deferred.
Rollback removes the new CLI module, discovered tests and evidence references.
Existing scenarios, public commands and product behavior have no dependency on it.
