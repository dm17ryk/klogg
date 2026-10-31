# G02h: viewer open/index/search and idle-dock baseline

The existing benchmark entry point now accepts `--scenario viewer`. It measures
the normal file viewer through existing Commander operations over native MCP.
The benchmark adds no product command, worker protocol, settings or GUI feature.

```text
python scripts/benchmarks/workbench-benchmark.py --scenario viewer --platform native --executable APP --corpus CORPUS --repetitions 3 --metadata BUILD.json --output NEW_DIRECTORY
```

Use the existing corpus generator with 100,000 rows and seed 42 for the recorded
profile. The runner verifies the manifest and copied ASCII bytes. Each repetition
uses two fresh application hosts: analysis docks hidden, and Regex Lab plus Value
Counts visible but idle. Neither profile starts an analysis job. Profile order
alternates across repetitions to reduce a fixed-order bias. Both profiles use
the same immutable bytes, exact row oracles and two-tab workload.

Each host opens two separate copies of the same fixture. The first is labelled
`cold_process`; the second is `warm_process`. The second path forces a new index
instead of selecting a previously opened tab. These labels describe process
state, not the OS file cache. OS cache remains uncontrolled.

## Observations and gates

The open/index interval starts before `open_file` and ends when `dump_state`
reports that the exact file is active and loading has finished. A literal ERROR
search and regex INFO search then require matching search text, a completed
search, no source loading/error, and the exact expected physical-row counts.
For 100,000 rows the expected counts are 10,000 and 90,000. Search commands use
both window and tab selectors. Malformed states, wrong counts, changed files,
transport errors and missed deadlines fail the run.

All timings include Commander submission, child CLI startup, IPC and completion
observation. They are not pure index/search CPU time, GUI presentation latency
or throughput calibration. Startup, payload copying, fixture copying/hash checks
and dock configuration are outside the measured phases. Read-only startup probes
wait for `startupReady` within the existing startup deadline; no timed operation
is retried after failure. Dock state is checked before and after each cycle.

Defaults are 30 seconds for each open/index or search phase, configured through
the existing `--budgets` object with `openIndexSeconds` and `searchSeconds`.
Transport and waiting consume those same phase budgets. Every host shares a
bounded suite deadline; at most three repetitions and two cycles/profile are
allowed. Existing owned-process cleanup and private profile/runtime handling
apply on success, failure and timeout. Error responses are retained separately
for diagnostics. Logs identify profiles, file names, states, counts, polls and
termination reasons, without printing source bodies.

`pairedOverhead` retains idle-minus-hidden elapsed-time deltas and ratios for
the same repetition/cycle. Negative deltas remain negative, and a zero baseline
has a null ratio. This is a raw comparison with **no calibrated overhead gate**.
Reports retain partial cycles on failure and always keep the full release gate
pending. A deliberately tiny phase budget must return nonzero and retain the
incomplete phase. Timing gates are operational regression ceilings, not promised
hardware-independent performance.

## Verification and boundaries

`tests/benchmarks/test_workbench_viewer.py` checks exact counts, source identity,
strict state types, startup readiness, elapsed command time, total deadlines,
negative deltas, zero denominators and the shared runner's failure report.
The normal benchmark self-test discovery and existing opt-in Windows native
benchmark CI selection include this scenario. The inherited payload handling
supports Windows/Linux/macOS; actual platform acceptance requires its real run.

See [recorded observations](g02h-windows-observations.json) for actual outcomes,
binary/source identities and resolved or retained failed attempts. The tested
deployment is reused only after its product source is compared with merged
master; no new C++ build is claimed for this Python/CI slice. Linux/macOS native
results, accelerated-backend performance, controlled-cache comparisons, rich
renderer overhead, compositor timing and full G02/G03/W0 remain open. Decoder
core retains its approved position after W0.

Rollback removes the viewer scenario dispatch/module, its CI invocation and
evidence references; the existing analysis/ui-service scenarios are independent.
