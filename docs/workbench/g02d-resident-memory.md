# G02d: opt-in Windows resident-memory observations

The `ui-service` benchmark accepts `--sample-resident-memory`. It polls current
working sets for the native test host and its owned descendants. This supplements
[G02c's committed-memory high-water mark](g02c-memory-gates.md); the two metrics
have different meanings and remain separate in each report.

Sampling is off by default. Existing timing runs perform no RSS queries and do
not install an RSS budget. This is a benchmark-harness option, with no change to
application GUI, Commander, CLI or MCP operations or settings.

```text
python scripts/benchmarks/workbench-benchmark.py --scenario ui-service --platform native --sample-resident-memory --executable APP --test-executable ITESTS --corpus CORPUS --output NEW_DIRECTORY --repetitions 3 --metadata BUILD_METADATA.json
```

Use a complete deployed application/test payload from the same build. Keep the
machine, corpus, binaries and metadata fixed when comparing opt-in and default
runs. Large generated corpora and copied payloads stay outside Git.

## Accounting and limits

Each repetition keeps G02c's fresh Windows Job Object. The first poll occurs
after host assignment and before its `start` handshake. Subsequent polls query
only job members through
[QueryInformationJobObject](https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-queryinformationjobobject);
opening each handle is followed by an `IsProcessInJob` check.
The runner, version probe, unrelated applications and previous repetitions are
excluded. Every query handle closes immediately. `ProcessOwner` and its cleanup
behavior are unchanged; sampling has no background thread.

The sampler requests a 10 ms interval while waiting for host completion. Polling
counts against the original absolute native-suite deadline. The report records
actual sample counts, maximum gaps and poll durations; 10 ms is not a timing
guarantee. Member enumeration is bounded to 256 active and tracked process IDs,
with three bounded attempts for an interrupted list query. Results retain maxima
and counters, not an ever-growing list of samples.

`residentMemory` labels the backend, host PID, scope, start/end boundaries,
observed worker PIDs, empty polls and process-exit races. Workers means all owned
non-host descendants. A process that exits between enumeration and query is
counted as a race. Access denial, invalid membership, unavailable counters,
resource limits and API failures fail the run with a structured reason; they
cannot silently become zero usage. Diagnostic errors preserve the failing API's
Win32 code before checking for process exit. Timeout/error reports retain
`residentMemoryFailure` with incomplete/failed sampling diagnostics.

## Metric meanings

| Metric | Meaning |
| --- | --- |
| `processTreeSampledRssPeakBytes` | Maximum observed sum of current member working sets in one poll. |
| `hostSampledRssPeakBytes` | Maximum observed current host working set. |
| `workersSampledRssPeakBytes` | Maximum observed sum for non-host members; omitted if no worker was observed. |

The backend uses
[GetProcessMemoryInfo](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo)
and its current `WorkingSetSize` counter. Each poll queries members sequentially
and is explicitly **not atomic**. Shared
pages count in each process's
[working set](https://learn.microsoft.com/en-us/windows/win32/memory/working-set),
so their sum is not unique physical
memory. Short-lived workers and spikes between polls can be missed; exited-worker
RSS is not reconstructed. These are sampled observations, not exact process-tree
high-water marks or sums of individual process peaks. The exact
`workerPeakResidentMemory` gate remains in `unmeasured`, even after successful RSS
sampling. Sustained-memory slope, native Linux/macOS RSS, compositor timing and
full G02/G03 acceptance also remain open.

The Windows opt-in installs an initial observed-tree ceiling strictly below
512 MiB, overridable through the existing bounded `--budgets` JSON:

```json
{"processTreeSampledRssPeakBytes": 536870912}
```

This is a regression ceiling for the frozen benchmark workload, not a calibrated
capacity or an upper bound on unsampled spikes. Overrides require the opt-in and
must be positive integral bytes at most 16 GiB. A one-byte budget deliberately
fails while retaining measurements. RSS sampling is rejected for unrelated
scenarios and corpus generation/verification. Linux/macOS accept the opt-in with
an explicit unsupported result, no RSS gate, and retained `unmeasured` entries;
an explicit RSS budget there is invalid setup.

The expanded benchmark self-tests retain their individual process deadlines.
Their aggregate CTest cap is 900 seconds: the earlier 37-test suite took 480.28 s
locally and the new focused CLI/API suites add about 95s before shared setup.
This cap is separate from all application worker/job budgets.

Tests exercise held resident workers, exclusion of the harness, poll-total
aggregation, bounded history, actual selection of job members, closed owners,
query errors, access denial versus exit races, invalid counters, timeout and
one-byte budget failures. Portable aggregate/report contracts run everywhere;
native API cases are explicitly skipped elsewhere. Runtime observations and
their limitations are recorded separately; a passing harness test is not native
application performance evidence.

## Local evidence

The full local Qt 6.10.1 Qt-only target build and CTest passed: six suites in
249.07 s, including the expanded benchmark suite in 89.62 s. Python 3.9 separately
passed 58 tests in 108.835 s with four platform skips. All four pre-existing portable
configuration files were restored byte-for-byte after CTest.

Using the same binaries and a 100,000-row corpus with seed 42, three default native runs
and three opt-in runs passed the unchanged timing gates. The opt-in observations
were:

| Repetition | Observed tree peak (MiB) | Observed worker peak (MiB) | Polls |
| --- | ---: | ---: | ---: |
| 1 | 88.49 | 27.59 | 278 |
| 2 | 88.68 | 27.23 | 217 |
| 3 | 88.78 | 27.23 | 211 |

Every opt-in run observed worker members. The largest observed polling gap was
25.30 ms, illustrating why the requested 10 ms interval is not a guarantee. The
one-byte budget failed with its measured values retained. Sampling-off/on runs
are sequential observations on an uncontrolled live machine, not a statistical
overhead or capacity claim. Exact bytes, timings, backend/build identities,
source hashes and failure history are retained in
[the observations](g02d-windows-observations.json). Full G02/G03 remain open.
