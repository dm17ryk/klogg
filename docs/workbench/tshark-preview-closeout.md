# TShark Preview recovery and default-runtime closeout

This work starts from `43e00df689d3ccdb8fe918fb91aa36333ff18d71`.
It keeps the existing 100–10000 ms request range, 3000 ms default and fixed
3000 ms hover budget. A visible cold timeout remains a valid outcome; successful
warm decodes do not establish cold performance acceptance.

## Native Windows fallback discovery

Windows PATH discovery could select a pyenv `python3.bat` before an available
native `python.exe`. Qt's own `qtpaths --find-exe` confirmed the selected shim.
A held-open-input public frame probe returned the same exact frame through both
programs, but the shim took 3345 ms and the native executable took 124 ms. This
is launcher overhead evidence, not evidence of corrupted or unsupported batch
framing. A normal hover timed out while the same 3000 ms request decoded using
the explicit native interpreter. An unchanged warm repeat also passed, so a
wall-clock GUI failure is not the deterministic regression oracle.

The Windows PATH fallback now tries `python.exe`, then `python3.exe`, before
the generic `python3` and `python` names that can select PATHEXT batch shims.
Explicit and bundled runtime choices retain precedence. Unix keeps its original
`python3`/`python` order. Executable names do not validate Python versions; the
existing supported-runtime requirements and worker error handling still apply.
Decision diagnostics contain fixed candidate names, origins, counters and
booleans rather than paths or payloads.

A Windows PATH fixture places a real native fixture executable after a batch
shim and checks native preference plus shim-only compatibility. It failed with
one assertion under the old order, then passed all 17 assertions after the fix.
The real hover fixture now requires rendered decoded field rows and rejects the
unavailable result, instead of accepting the assembly header alone.

The native-first combined build passed `ci_build` in 502.18 seconds. A fresh
normal-selection hover passed with decoded EHCI fields under the unchanged
3000 ms budget. Its independent observer saw the actual `python.exe` worker,
excluded the bootstrap PID, and recorded zero active job processes or live
observed handles before outer cleanup. The normal AB CLI/MCP pair returned exact
TShark values 65/66 without fallback in 1771/248 ms; full normal parity then
passed 54 strict comparisons. These are local observations, not cold-install
performance guarantees or bundled-runtime acceptance.

## Exact field prefixes at the response limit

The retained private fixture assembled successfully and decoded as available,
but hit `response_limit` and retained zero fields. The failing assertion was
the nonempty-field check, not the availability check. Previously, any aggregate
response overflow cleared every field, even when an exact prefix would fit.

The worker now retains a whole preorder field prefix within the unchanged
131072-byte compact ASCII JSON body cap, excluding its four-byte header. It
counts final limited-result metadata, ASCII escaping and commas with the
original encoder and performs a final cap check. It stops at the first field
that does not fit; it does not truncate values, skip a large field, or mutate
the input. A first field that cannot fit still produces an empty limited result.
Overflow results remain `completed`, `available=true`, `complete=false`,
`limited=true`, with reason `response_limit`. IDs, depths, attributes and engine
positions are retained. Their `engine_data_source_unresolved` origin remains
explicit: these positions do not establish physical carrier or decoded-payload
offset mappings. This follows the existing JSON backend's whole-row-prefix
policy. No request option, response cap or execution deadline was raised.

Eight portable boundary cases produced five expected failures against the old
all-empty policy, then all passed. They cover exact-cap metadata and commas,
one-byte overflow, Unicode/supplementary/NUL escaping, oversized-first-field
behavior, unchanged full-fit results, input immutability and linear encoding.
Full portable discovery passed 140 cases: 92 executed passes and 48 explicit
external-input skips. Python 3.9 grammar checks passed; no local 3.9 runtime was
available, so runtime compatibility is still a hosted-CI check.

A public routed Lua fixture isolates response-byte overflow with 384 UTF-8 bytes
and 385 rows, within the existing row and PDML bounds. Against the old embedded
worker, both CLI and MCP reported the expected limited metadata but zero fields;
the new exact-prefix assertion failed after 57 comparison pairs. An earlier
fixture run failed its setup oracle and is not counted as product RED.
The rebuilt embedded worker passed all 57 CLI/native-MCP comparison pairs under
normal system fallback selection, without a forced interpreter. Both transports
retained 291 exact whole rows from the 385-row overflow fixture and reported the
limited metadata above. Observed request times were 408/343 ms; these are local
observations, not performance guarantees.

## Final combined local qualification

The final `ci_build` passed with exit 0 in 510.04 seconds, including regeneration
and compilation of `preview_decoder.qrc`. Seven product/test file hashes were
frozen before the build at qualification head
`ed41f3f473841c08c7be88c72869f5f0e9b13577`; the qualification combined the
independent PR96 communication contracts with these PR97 changes.

In a fresh isolated runtime, native private-fragment recovery, the strengthened
3000 ms hover check and capability checks each exited normally with status 0.
Recovery decoded retained private fields before and after real Lua timeout and
cancellation. The public hover rendered assembled EHCI fields. Independent
observers recorded zero active jobs and zero surviving observed process handles
before outer cleanup in all three phases, observing 38, 23 and 26 processes
respectively. CLI/MCP parity success alone is not independent leak or normal GUI
exit evidence. No private logs, dissectors or screenshots are distributed.

The final registered CTest suite passed all 10 groups in 300.13 seconds
(wrapper exit 0, 300.34 seconds), including 1045 terminal-model assertions in
23 cases and 519 communication-contract assertions in 22 cases. The broad suite
used an explicit installed interpreter; the dedicated phases above exercised
normal system fallback without an override. Validation used Windows MSVC 19.44,
Qt 6.10.1, Python 3.14, and Hyperscan/Vectorscan disabled. The isolated runtime
contained no bundled Python candidate, so bundled deployment remains unqualified.

## Earlier worker startup observations

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
executable with `[previewehcirecovery]`. Qualification must use a source-matched
build; test source alone is not acceptance evidence.

The existing CLI/native-MCP parity harness now decodes external EHCI after its
real Lua stall, compares exact display/raw fields and repeats on each recovered
engine. Its `--default-python` switch removes the harness interpreter override
so the application's normal bundled/system interpreter resolver is exercised.
Use that switch with `--external-config` and `--ehci-capabilities` in an isolated
portable runtime. The ordinary harness mode retains explicit Python selection
for controlled tests. Its shared runtime copier excludes `python_runtime`, so
this isolated-copy mode qualifies system fallback only. A bundled-Python package
needs a separate deployment check that retains that runtime.

## Preserved early default-runtime failures

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
observations alone did not establish an interpreter-selection defect: explicit
Python also timed out. The later isolated controls and Qt lookup above supplied
the evidence for native-first discovery.

The harness closes MCP input, terminates the test-owned GUI host, waits/reaps
and removes its unique executable. This is cleanup evidence, not normal GUI
exit proof or independent OS observation of every TShark descendant. No cleanup
error was reported. Exact summaries and stage logs remain private in
`build_root/default-python-baseline` and `build_root/default-python-control`.

Hosted, other-platform, accelerated-search, bundled-runtime and release checks
remain open. Cold-start performance acceptance and broader decoder-core or
interactive-terminal integration are not established by these local results.
Independent C++, Python and general reviews approved the source changes.
Exact-checkout incremental CodeReviewGraph refreshes succeeded; these are not
full flow/community rebuilds. GitNexus is registered but stale on the primary
qualification checkout: its requested full analysis failed with native exit
3221226505 (0xC0000409) after 237.83 seconds following a parser file-budget
failure. The publication checkout is unregistered. Required exact-root impact
and staged detection attempts cannot establish complete scope from those indexes.
CodeGraph is unindexed and was not initialized. Direct staged diffs, frozen source
hashes and functional tests supplement these limits. No clean static-analyzer
pass is claimed.
