# G02i — First useful analysis results

This slice measures client-observed useful data separately from submission and
completed-job observation. It adds an opt-in benchmark, not a product command or
a second MCP catalog. [Recorded observations](g02i-windows-observations.json)
retain source/build identities, measured values, checks and remaining gates.

```powershell
python scripts/benchmarks/workbench-benchmark.py --scenario first-results --platform native --executable build_root/output/RelWithDebInfo/cilogg.exe --corpus build_root/g02-final-corpus --repetitions 3 --metadata build_root/g02i-build-metadata.json --output build_root/g02i-first-verified
```

These are the recorded local paths; choose a fresh output directory for another
run. Generate matching input bytes with the common runner's `--generate-corpus
DIR --rows 100000 --seed 42` if absent. `--metadata` is optional; supply your own
deployment's build/SDK metadata rather than assuming the recorded local file
exists. Hosted CI prepares it through the existing configured-build verifier.

The ordinary entry point retains exit codes 0 (measured guards passed), 1
(runtime/oracle/budget failure) and 2 (invalid setup). Existing analysis,
ui-service and viewer scenarios retain their contracts. Hosted Windows native
benchmark opt-in runs this scenario and archives its normal reports; benchmark
self-tests run through the existing CTest and platform/Python workflow discovery.

## Measurement boundary and oracle

- Each repetition uses a private, uniquely named executable/payload, private
  settings/IPC and a fresh application host with bounded, owned-process cleanup. Existing ownership
  and bounded cleanup helpers supervise only those processes.
- The immutable fixture is one `G02I_USEFUL` line followed by the validated ASCII
  corpus. Input corpus and derived-source hashes, bytes and physical rows are
  retained. There must be exactly one match at physical line 1, with the exact
  whole-match value and zero-based UTF-16 span. Source reading/indexing finishes
  before the timed analysis request.
- Each host makes a first and a repeated analysis request against that indexed
  source. Those are host/request states, not controlled OS-cache cold/warm runs.
  Each analysis job still owns its supervised capture worker.
- Time starts before `regex_evaluate` submission through Commander/native MCP.
  `submissionAcknowledgedMs` is only acknowledgement.
  `firstUsefulPageObservedMs` requires a delivered page and validated match,
  job ID, exact decimal request revision, source identity, generation and frozen
  watermark. Pages request one record; empty responses cannot establish success.
  Its timestamp is decoded-response receipt, qualified by the subsequent oracle;
  it is not a measurement of local oracle execution cost.
- `completedJobStatusObservedMs` records the first completed job-status response.
  It can precede first useful delivery when completion was already observable.
  Completed/limited, failed, cancelled and missing-result jobs cannot pass the
  full oracle. Exact scanned-line and final result counts are checked.
- Query start/duration, empty-page counts and last-empty-response observation
  describe polling. They do not establish the server's exact first-ready time.
  Both measured endpoints include transport, worker startup/compilation,
  source reading, matching and polling; application startup, open/index and
  cleanup are recorded separately or excluded. No number is pure CPU time or
  compositor presentation latency.

## Bounds, diagnostics and failure proof

Initial operational guards are 10,000 ms through first useful delivery,
60,000 ms through completed-job observation, and 30 seconds for open/index.
Each host has an additional 180-second outer deadline. Phase budgets can be
overridden with the existing bounded JSON budget file; the outer deadline still
caps measured work; cleanup has a separate allowance. These are operational guards, not calibrated product release
budgets. Every blocking response is checked against the remaining phase time.
The startup info read uses the remainder of the existing 30-second startup
allowance, instead of an unrelated 15-second request cap. It is never retried on
a transport timeout, and that allowance does not extend the startup deadline.
Once useful data is validated, status polling uses the completion budget.

Logs identify jobs, sources, generations, watermarks, poll counts, phase changes
and failure reasons without logging source bodies. Failed runs retain partial
observations. Cleanup cancels/releases submitted jobs, checks `job_expired` and
uses its own bounded allowance. Cleanup failure is retained and cannot hide the
original measurement failure; process ownership supplies the final stop boundary.

A budget JSON file containing `{"firstResultMs": 1}` supplies a negative run
through the same command using `--budgets FILE` and a fresh output directory.
It must fail rather than turn a late acknowledgement/page into a pass. Unit
fixtures also cover stale provenance, incorrect data/spans, empty completion,
limited/failed workers, late delivery, completion timeouts and cleanup errors.

## Acceptance boundaries

The final-source native run passed three fresh hosts and six requests. All
scanned exactly 100,001 physical rows, retained one exact match, and released the
job with `job_expired` verification. Timings below are seconds including public
protocol/observation latency, not p95 estimates or engine throughput.

| Host | First request: useful / completed status | Repeated request: useful / completed status |
| --- | --- | --- |
| 1 | 1.410 / 3.344 | 1.317 / 3.090 |
| 2 | 1.199 / 3.247 | 0.844 / 2.301 |
| 3 | 1.016 / 3.010 | 0.799 / 2.262 |

The real 1 ms negative failed in `regex_evaluate` with `transport_timeout`; it
reached the timed submission phase and accepted no useful result. Owned roots
were reaped. An earlier native run is retained separately: two hosts/four jobs
passed, then startup `get_info` timed out before that host's analysis requests.
The startup allowance alignment does not diagnose that underlying delay.

Both Python 3.14 and 3.9 complete benchmark suites passed 111 tests (four expected
platform skips each); 19 focused cases and Ruff E9/F/I checks pass. Full CTest
passed 6/6 in 205.40 s before the last Boolean-oracle tightening; the final complete
Python suites cover that refinement. No C++ source changed. General/Python
reviews found no code blockers and corrected unsupported normal-exit wording.

## Sustained-profile follow-up

The unchanged sustained runner qualified a separate 240-second profile over
three repetitions. Its executable, harness, 512 MiB sampled-tree and 32 MiB
post-release host-growth ceilings match the failed 120-second audit profile.
The minimum remains ten released samples; no sample gate or limit was loosened.

| Repetition | Released span (seconds) | Released samples | Memory guards |
| --- | ---: | ---: | --- |
| 1 | 247.039 | 31 | Passed |
| 2 | 244.163 | 24 | Passed |
| 3 | 241.577 | 29 | Passed |

Each repetition passed exact scan/count/source oracles and owned-process
cleanup. Sampled tree maxima were below 117 MiB; post-release host growth was
negative in these runs. Sampling remains non-atomic, with shared pages counted
per process and gaps recorded. This is not a lifetime peak or a calibrated
slope/long-term leak guarantee. The original 120-second result remains failed
with 11/9 samples; the longer profile is separately qualified.

```powershell
python scripts/benchmarks/workbench_sustained.py --executable build_root/output/RelWithDebInfo/cilogg.exe --corpus build_root/g02-final-corpus --duration-seconds 240 --repetitions 3 --output build_root/g02i-sustained-240
```

## Remaining gates

Local measurements reuse the approved Qt 6.10.1 Qt-only deployment whose
application source tree matches the merged baseline. Input and executed binary
hashes remain separate. This slice claims no fresh C++ build. Hosted Qt 6.12,
accelerated-backend results, other native platforms, compositor/hardware input,
OS-cache control and broader release/P00 acceptance remain separately gated.
The local current-capability measurements are supplied; G02/W0 still require
this slice's integration and relevant hosted checks before MD01 acceptance.

Rollback removes the new scenario/CI invocation and its module/self-tests while
retaining existing public analysis APIs and benchmark scenarios.
