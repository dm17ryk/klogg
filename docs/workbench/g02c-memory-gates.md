# G02c: Windows process-tree committed memory

The `ui-service` benchmark now records `processTreePeakCommitBytes` on Windows.
This complements G02b's GUI/service timings and G02a's host-only working set.
It does not complete G02 or measure worker resident memory.

Each repetition has a new Windows Job Object. The native test host is assigned
before the existing `start` handshake permits analysis workers to start. Windows
tracks the job's committed-memory high-water mark, including short-lived workers
that exit before the final query. The query runs after the native host exits and
before closing the job. The Python benchmark runner, the version probe, and
earlier repetitions are excluded. Startup activity before job assignment is not
claimed as measured.

This is **committed virtual memory**, not resident memory or the sum of individual
process peaks. See Microsoft's [extended job limit information](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_extended_limit_information).
The report labels backend, accounting boundaries, scope, and unsupported targets.
Windows query errors and missing counters fail the run; they cannot become zero
usage or an unsupported-platform pass.

The initial Windows regression budget is strictly below **512 MiB**, configurable
through the existing `--budgets` JSON:

```json
{"processTreePeakCommitBytes": 536870912}
```

Overrides must be positive integral bytes, at most 16 GiB. This is an initial
regression ceiling for the existing frozen workload, not a calibrated product
capacity guarantee. An overrun retains the measured value and a failed gate.
Linux/macOS record `memory.status=unsupported`, omit this gate, retain
`processTreePeakCommitBytes` in `unmeasured`, and reject an explicit override for
this unavailable metric. Other existing timing gates still run there.

Run the existing command; no additional opt-in is needed:

```text
python scripts/benchmarks/workbench-benchmark.py --scenario ui-service --platform native --executable APP --test-executable ITESTS --corpus CORPUS --output NEW_DIRECTORY --repetitions 3 --metadata BUILD_METADATA.json
```

Regression coverage includes an exited descendant allocating 64 MiB, a fresh
peak per repetition, one-byte budget failure, native API failure, a missing
counter, querying a closed owner, and unsupported-platform reporting. The
portable benchmark workflow runs these tests on Windows, Linux, and macOS;
platform-specific cases are explicitly skipped elsewhere.

Local measurements and failure history are in
[g02c-windows-observations.json](g02c-windows-observations.json). They use the
existing local Qt 6.10.1 Qt-only binaries, identified by hashes and their reported
build identity. G02c changes Python harnesses, tests, and documentation; it does
not claim a fresh C++ build. Hosted current-build measurements, worker RSS,
sustained-memory slope, compositor presentation, controlled OS-cache runs,
accelerated-search measurements, and renderer comparisons remain pending.
