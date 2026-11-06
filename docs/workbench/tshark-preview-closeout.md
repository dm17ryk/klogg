# TShark Preview recovery and default-runtime closeout

This work starts from `43e00df689d3ccdb8fe918fb91aa36333ff18d71`.
It keeps the existing 100–10000 ms request range, 3000 ms default and fixed
3000 ms hover budget. A visible cold timeout remains a valid outcome; successful
warm decodes do not establish cold performance acceptance.

## Observed startup cost

On the local Windows host, three fresh actual workers used the trusted configured
EHCI pack and public synthetic bytes. The first request timed out at 3062 ms;
the next fresh workers completed in 2295 and 2280 ms. Warm requests took 507 and
1032 ms. These are worker measurements, excluding native carrier selection and
delivery. The isolation probe alone took 1901, 1555 and 1289 ms; conversion took
415, 144 and 342 ms for those fresh requests. Each owned tree was reaped.

A candidate overlapped conversion with the folder probe. Its subsequent fresh
requests timed out at 3072 and 3053 ms and completed at 2542 ms. This did not
demonstrate improvement, so the candidate was removed. Production retains the
serial isolation/engine lifecycle. Neither increasing budgets nor removing
isolation checks was used to make this observation pass.

## Deterministic diagnostic regression

The portable suite exposed a real diagnostic race: scheduling the pipe writer
could exhaust the deadline before `Event.wait(remaining(deadline))`. The timeout
exception then bypassed the explicit `preview_pipe_timeout` warning. A new test
forces that exact branch and failed against unchanged production code. The
minimal fix gives both timeout branches the same warning and preserves their
failure result. The blocked-writer test now uses entry/release events instead
of assuming a sleeping writer stays blocked while the caller is scheduled.

Python 3.14.10 worker discovery then passed 22 cases, including 8 explicitly
skipped actual-engine opt-ins. This is 14 portable passes, not real-engine
acceptance. Private RED/GREEN and rejected-candidate logs remain in `build_root`.
Full portable decoder discovery passed 132 cases in 25.461 seconds: 84 passes
and 48 explicit external-input skips. The portable parity oracle passed all
three cases. These broader checks also exclude actual-engine qualification.

## Executable recovery qualification

The hidden native case `[previewehcirecovery]` loads explicitly supplied trusted
EHCI Lua with a public fault dissector. The fault publishes an entry marker
before stalling. Timeout and cancellation are tested separately; cancellation
waits for that marker. Each retirement is followed by two exact EHCI decodes,
including the public embedded-NUL raw-byte oracle, warm PID reuse and stopped
native worker checks. Optional bounded private fragmented-log input is assembled
and decoded before and after both retirements. Assertions suppress its field
contents; private input is not committed.

Supply `CILOGG_TSHARK`, `CILOGG_TEXT2PCAP`,
`CILOGG_EHCI_CAPABILITIES_LUA`, `CILOGG_EHCI_CAPABILITY_FIXTURES` and, for the
private-log check, `CILOGG_PREVIEW_PRIVATE_LOG_SAMPLE`. Run the built native unit
executable with `[previewehcirecovery]`. These new native checks still require a
source-matched build and execution; test source is not acceptance evidence.

The existing CLI/native-MCP parity harness now decodes external EHCI after its
real Lua stall, compares exact display/raw fields and repeats on each recovered
engine. Its `--default-python` switch removes the harness interpreter override
so the application's normal bundled/system interpreter resolver is exercised.
Use that switch with `--external-config` and `--ehci-capabilities` in an isolated
portable runtime. The ordinary harness mode retains explicit Python selection
for controlled tests. Its shared runtime copier excludes `python_runtime`, so
this isolated-copy mode qualifies system fallback only. A bundled-Python package
needs a separate deployment check that retains that runtime.

## Preserved default-runtime failures

Two functionality probes used the existing byte-identified master-equivalent
native runtime, before the new diagnostic fix was rebuilt. Its `cilogg.exe`
SHA-256 is `1e7a7c8c29334546ad91c331d053cef359b8db5d4a1bd9f9e902f1e9ccbb1b1b`.
Each probe made one public NUL-bearing EHCI request through CLI and native MCP
in a unique portable copy. Configuration/index work ran concurrently; these are
functionality observations, not isolated latency measurements.

| Interpreter condition | CLI | Native MCP | Owned wrapper |
| --- | --- | --- | --- |
| Normal system selection, no override | timeout, 9999 ms | timeout, 10010 ms | exit 1, 54.576 s |
| Explicit installed Python 3.14 control | timeout, 10028 ms | completed, 9579 ms | exit 1, 50.026 s |

Both discovery runs were healthy. The default run reached two completed
selectors, owned workers and sent worker requests, with zero worker responses
and two visible timeout retirements. The control reached the same stages; one
worker replied with 77 fields and 867 diagnostic bytes, and the other timed out.
Its selectors themselves took approximately 1.4 and 1.9 seconds. The failed
observations do not establish an interpreter-selection defect: explicit Python
also timed out. No resolver change was made from this evidence.

The harness closes MCP input, terminates the test-owned GUI host, waits/reaps
and removes its unique executable. This is cleanup evidence, not normal GUI
exit proof or independent OS observation of every TShark descendant. No cleanup
error was reported. Exact summaries and stage logs remain private in
`build_root/default-python-baseline` and `build_root/default-python-control`.

GUI, new native recovery, private-log, hosted, other-platform and release checks
remain pending until executed against the identified combined source and runtime.
The reviewed source has a successful exact-checkout incremental CodeReviewGraph
refresh; this is not a full flow/community rebuild. GitNexus is unregistered for
this checkout and its required exact-root impact/detection attempts cannot prove
scope. CodeGraph is unindexed and was not initialized. Direct staged diffs,
portable tests and the independent Python/C++ reviews supplement these limits.
