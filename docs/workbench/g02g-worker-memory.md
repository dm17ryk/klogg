# G02g: supervised worker RSS high-water diagnostics

Analysis jobs now publish `metrics.workerMemory` through the existing
`get_analysis_job` Commander operation, CLI and native MCP. Regex Lab and Value
Counts display the same observation in their status text. No additional public
command, worker frame, plugin protocol or persistent setting is introduced.

## Accounting

On Windows, the supervisor retains a query/synchronization handle after assigning
its child to the existing memory-limited Job Object. It verifies membership
before retaining that handle, avoiding later PID reopening and reuse. After
each accepted response and before supervisor termination it queries
[GetProcessMemoryInfo](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo).
`PeakWorkingSetSize` is the OS-maintained peak working set in bytes, including
shared pages. It can preserve a transient peak after those pages have been
freed; this differs from polling current working sets and from committed memory.
The existing 512 MiB process commit limit remains separate.

The scope is `largest_single_worker_high_water_through_observation`: the job
retains the **maximum**, never a sum of repeated observations. This is not a
total-process-tree peak or guaranteed full-lifetime/post-exit high-water mark.
Allocations after the last published observation can still be missed. Source
invalidation can discard a batch before its diagnostic observation is published.
The destructor's final observation is not published after the last callback.
Protocol-flood callbacks kill immediately and can also lose the final
observation; earlier peaks remain available with partial status when a later
callback reports unavailable memory.

The summary is constant-size and protected by the existing job mutex. Peaks,
observation counts and native error codes use canonical decimal strings to
preserve exact integers across JSON clients. Statuses are:

| Status | Meaning |
| --- | --- |
| `measured` | Every published callback has a positive OS measurement |
| `partial` | A peak exists, but some callbacks lack measurements |
| `unavailable` | Supported backend, but no successful query |
| `unsupported` | Native measurement is not implemented on this platform |

Absent measurement is JSON null, never a zero-byte measurement. Process exit,
query access failure and native errors do not prevent analysis results; they
reduce diagnostic availability. Malformed internal typed observations fail with
`invalid_worker_output`. Raw/custom processors reporting no memory observations
leave the existing metrics map unchanged; docks display "not reported".
`lastObservation` is `response` or `termination_request`, and `lastNativeError`
describes that last observation, rather than all historical failures.
The supervisor logs PID, availability, peak, native error and termination point;
the job service also logs the job ID. Source bodies are not logged.

## Verification

`tests/ui/workermemory_test.cpp` covers real workers, empty scopes, multiple
batches, maximum accounting, decimal precision above 2^53, partial availability,
malformed observations, raw processors and both dock status displays. Windows
fixtures allocate/touch/free 32 MiB before replying to prove an OS high-water
measurement, and hold an allocation during cancellation to verify the
termination observation and recovery. Existing timeout, protocol fault and
worker-reaping regressions remain mandatory.

Run the public-surface probe against a complete deployed runtime:

```text
python tests/mcp/worker_memory.py --executable APP --artifacts NEW_DIRECTORY
```

It starts jobs through CLI, MCP and each dock's actual Run control, and compares
the same completed job's memory diagnostics through CLI/MCP/dock state. Different
jobs are not expected to have identical peaks. Windows requires measured,
positive observations and checks a default 512 MiB observed-worker budget.
Other platforms must explicitly report unsupported. The probe owns a private
runtime and process tree and uses bounded waits and public discovery. Reports
survive failures; `--max-worker-bytes 1` is a negative budget check.

The existing opt-in Windows native benchmark CI selection runs this probe;
platform and Qt selections are unchanged. New C++ tests also run through normal
CTest on other platforms. See [local observations](g02g-windows-observations.json)
for actual outcomes and their limitations. Full G02/G03/W0, complete worker
lifetime RSS, Linux/macOS memory collection, native cross-platform presentation,
calibrated throughput and release gates remain open. Decoder implementation
retains its approved position after W0.
