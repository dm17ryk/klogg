# Workbench automation parity

G03a provides a reusable runner for the existing analysis (P03), Regex Lab (P04)
and Value Counts (P05) contracts. G03 remains in progress until fresh accelerated
and Qt-only builds, the complete relevant CTest/MCP suite and platform gates have
recorded evidence. Headless control activation does not establish interactive UI
usability. G02 continues to own performance measurements.

Run from the repository root, using Python 3.9 or newer and a deployed executable:

```powershell
python tests/mcp/workbench_parity.py --executable C:/Essence_SC/cilogg/cilogg.exe --task G03 --artifacts build_root/workbench-parity/G03
python tests/mcp/workbench_parity.py --executable build_root/output/RelWithDebInfo/cilogg_portable.exe --task P04 --artifacts build_root/workbench-parity/P04 --backend accelerated
python -m unittest discover -s tests/mcp -p test_workbench_parity.py -v
```

`G03` selects all three cases. `P03`, `P03b`, `P03c`, `P03d`, `P04` and `P05`
select their registered feature cases. A future task such as V02 fails explicitly
until its adapter and cases land. Unknown tasks, empty selections, missing cases
and missing discovered operations fail; they never pass as skipped work.

`--timeout` bounds the complete child suite (default 300 seconds, maximum 1800).
Use a fresh artifacts directory, or `--new-run` to create a unique directory under
it. Existing reports are preserved and rejected as a destination for a new run.
`--backend accelerated|qt` records the caller's build declaration, not an inferred
capability. The report labels it unverified; retain the actual CMake configuration
and build provenance when demonstrating both variants. The capture backend alone
does not identify the accelerated search backend.

## Execution and ownership

The runner extends `analysis_parity.py`; its original command-line options and
fixtures remain available. It copies the application/runtime into a unique payload
directory, uses private portable settings and APPDATA/LOCALAPPDATA/XDG paths, and
starts its own offscreen host. It never attaches to an existing user's monitor or
opens a communication device. Existing native MCP discovery remains authoritative:
`cilogg_commands` lists operations and `cilogg_execute` invokes Commander. CLI
requests use literal arguments and `command --action ACTION --json-file PATH`.
The JSON registry describes acceptance cases, not another product command catalog.

An outer process supervisor captures stdout/stderr and imposes a suite deadline.
A startup handshake assigns the child to its Windows kill-on-close job before it
can spawn the host or workers; POSIX uses an owned process group. Cleanup reaps
owned roots and terminates the owned descendants even after timeout or failure.
The test-only `parity_process.py` owner is adapted from the separately reviewed
G02a benchmark owner to keep the two unmerged PRs independent. A shared helper can
replace the duplication after both foundations merge.

## Cases and comparisons

`test_data/workbench/command-cases/registry.json` maps task IDs to named cases,
required discovered operations, small JSON requests and expected semantic values.
Each case starts equivalent CLI and native MCP jobs, polls observable completion,
requires `complete: true`, reads all pages through opaque cursors, and releases
retained jobs. Existing analysis fixtures additionally cover invalid cursors,
semantic errors, a large MCP JSON request, repeat cancellation, release and source
body log suppression. P04/P05 also run their established GUI and error fixtures.

Results compare exact JSON types and values: booleans differ from integers, large
integers retain Python's exact representation, and decimal strings remain strings.
Only the existing result envelope's top-level job ID, request revision and source
provenance are excluded from row comparison across separately created jobs.
Nested capture fields are retained. Request revisions are checked separately,
including `9007199254740993`. Mismatch diagnostics identify the JSON path without
logging capture or source contents.

For dock cases the runner requires a unique Run `objectName` and a nonempty
`accessibleName` from `get_ui`, invokes the real control through `activate_ui`,
waits for a new job ID, then compares its completed results to CLI/MCP results.
This is a control presence and functional parity check; record packaged, visible
UI navigation and responsiveness separately.

## Evidence and integration

`result.json` is the suite verdict. Nonzero child exit or timeout fails the suite
even if an earlier case passed. `stdout.log`, `stderr.log`, `native/host.log`,
`native/mcp.log` and `native/commander-discovery.json` retain diagnostics.
`native/case-results.json` retains completed cases; requests and result values are
small synthetic fixtures. Do not use private log text in committed acceptance
cases or upload an arbitrary user's artifacts.

CTest registers `workbench_parity_self_tests` when Python 3.9+ is available.
Configure `-DKLOGG_TEST_WORKBENCH_PARITY=ON` to also register
`workbench_parity_native`; this option requires Python and a deployed Qt runtime,
including the offscreen plugin. The native test uses the current portable target
and records the configured backend declaration. Reruns use unique artifacts.
The standalone `Workbench parity harness` workflow tests the runner on Windows,
Linux and macOS with Python 3.9/3.12. Existing product CTest and MCP smoke tests
remain in place. Self-test CI is not a native product or backend pass.

The runtime checks share discovery/error assertions with this harness. They accept
the Python attribute names used by MCP SDK 1.30 and 2.3 (`isError`/`is_error` and
`structuredContent`/`structured_content`); the protocol JSON is unchanged. Both
versions passed the installed-package job/cancellation/transport checks locally.
The older `smoke_stdio.py` client and complete product suite remain separate gates.

G03a local evidence uses installed Windows CILogg 26.04.2.1043 (source `96bf481`,
SHA-256 `f06a010d41c34010c353bedc233e9ea32a8dde65e858aef4391c3dbd6f380bcd`).
Its Qt runtime reports 6.12.0.0. This is package behavior evidence, not a fresh
build of the harness branch. The machine's previously built C++ test binaries are
stale and are not used as evidence for a full current CTest pass.

Rollback removes the new runner, registry, workflow and CTest entries, then removes
the optional analysis hook/shared discovery assertion. Existing Commander/MCP
operations, legacy smoke tests and feature implementations remain unchanged.
