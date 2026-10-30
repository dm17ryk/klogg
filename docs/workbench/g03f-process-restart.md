# G03f: analysis docks across application restart

G03e closes and recreates MainWindow inside an integration process. G03f runs
the actual application in two sequential, normally closed processes, using one
private portable profile. This adds process-lifetime coverage for Regex Lab and
Value Counts without changing product commands, settings or existing runners.
It is independent of PR72's native benchmark wiring.

```powershell
python tests/mcp/workbench_restart.py --executable build_root/output/RelWithDebInfo/cilogg.exe --platform native --artifacts build_root/reports/workbench-restart
```

The artifact directory must be new. The default platform is `offscreen`; `native`
removes an inherited offscreen request. Windows CI selects the existing native-UI
opt-in and requests `QT_SCALE_FACTOR=0.75`, matching G03e's hosted desktop setup.
No host display mode is changed. Functional durations are not performance budgets.

## What the journey proves

| Phase | Required observation |
|---|---|
| First start | One private application window, native-MCP discovery succeeds, CLI opens the synthetic file and indexing completes. |
| Configure | CLI sets non-default patterns, text scope, Hebrew/emoji pasted text and visible docks; Regex Lab floats and Value Counts stays docked. |
| Run | Activate the actual accessible Run buttons through native MCP; await complete jobs with the draft revision. |
| Results | Verify both regex rows, full text, named/numbered captures and zero-based UTF-16 spans; verify the counted value and decimal-string count. |
| First close | Close through Commander, wait for host and MCP exit code zero, archive both saved settings files. |
| Second start | Launch the same private executable/profile with `--load-session`; require the same window identity and restored file. |
| Restored docks | Both remain visible with their chosen floating flags. Patterns return to factory defaults, scope to sample, text/jobs to empty, and Apply is disabled. |
| Old jobs | Both old job IDs return `job_expired`. Second host and MCP also close normally. |

The file fixture has ASCII physical lines and an unterminated tail. The separate
pasted-text fixtures exercise Hebrew and an emoji: the first capture starts at
UTF-16 offset 8, and the second at 0. Public line numbers remain JSON integers;
counts remain decimal strings. The test records text hashes and lengths instead
of retaining pasted bodies in its result snapshots.

## Ownership, bounds and provenance

The runner reuses `prepare_payload`, private IPC leasing, bounded MCP queues and
`BenchmarkHost` process ownership. Each epoch owns its application, MCP server and
CLI clients. Cleanup reaps only these processes. One absolute deadline bounds the
journey (default 180 seconds, configurable 1–600); read-only source readiness has
a fixed additional phase ceiling of 30 seconds. Only the recognized transient
`command_failed_dump_state` response is retried. Mutations and Run are never
retried. Diagnostics identify phases, actions, process IDs and failure reasons.

`CILOGG_AUTOMATION=0` permits real session restoration. Update checks are disabled
only in the private profile. User profiles, plugins and communication devices
are not used. The two settings archives must be confined regular files of at
most 2 MiB each, match their hashes and exclude runtime sentinels. The JSON report
also has a 2 MiB ceiling and always leaves `releaseGateStatus=pending`.

`binarySha256` identifies the original input executable. `executedBinary` and
`executedBinarySha256` identify and independently verify the confined, bounded
private copy actually executed. These hashes may differ after macOS ad-hoc
signing. Python fixtures, including uppercase `.PY`, remain `test_double` evidence.
Acceptance on macOS or Linux requires actual platform runs; simulated signing
fixtures establish only the report contract.

## Local evidence and remaining gates

[Recorded Windows observations](g03f-windows-observations.json) distinguish the
initial accepted source epoch, subsequent review fixes, final application runs
and their hashes. The final native and offscreen runs each passed both cases in
two real application processes. All six local CTest suites passed before the
final provenance-only harness refinement; the complete 106-test parity suite
then passed on Python 3.14 and 3.9, with one platform skip each. All 21 restart
self-tests and default Ruff checks pass. Actionlint adds no diagnostics to the
11 already present in master.

The application is the reused, approved Qt 6.10.1 Qt-only Windows deployment.
Its product source tree equals the merged master baseline; its embedded build
stamp and validated build-source epoch remain distinct. This is not a fresh
G03f C++ build or installer test. Hosted Qt 6.12 and accelerated-backend checks
remain pending until they actually finish.

This slice verifies floating/visible flags, pattern/scope/pasted-text defaults and
job disposal. It does not claim every option, pixel geometry, hardware input,
compositor timing, installer behavior or native Linux/macOS acceptance. Full
G02/G03 and W0 remain open. The approved order stays W0, then MD01–MD06 with their
prerequisites, then remaining W1; MD07–MD08 integration remains deferred.

Rollback removes the new runner, self-tests and optional CI invocation, then its
ledger/evidence references. Existing product behavior and benchmark wiring do
not depend on this test slice.
