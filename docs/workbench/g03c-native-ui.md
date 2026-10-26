# G03c: native dock interaction evidence

G03b verifies GUI/CLI/MCP behavior and discovers accessible controls. G03c adds
an opt-in native desktop probe for the actual Regex Lab and Value Counts widgets.
This is component interaction evidence, not a complete packaged-MainWindow or
physical-input usability pass. Full G02/G03 and broader conformance remain open.

```powershell
python tests/mcp/workbench_usability.py --executable build_root/output/RelWithDebInfo/cilogg.exe --test-executable build_root/output/RelWithDebInfo/cilogg_itests.exe --artifacts build_root/reports/workbench-ui
```

Provide a freshly built application and its deployed integration-test executable.
The probe's compiled worker path must identify the supplied application bytes;
an unrelated installed application cannot stand in for the worker actually used.
The runner records both executable hashes and checks the native worker hash.
The initial measured environment is Windows with the approved local Qt 6.10.1
SDK. CI selects the representative Windows Qt 6.12 accelerated build; hosted
confirmation and native Linux/macOS measurements remain separate evidence.

## Interactions and oracles

The hidden `[workbench-usability]` Catch2 case hosts each real dock in a native
`QMainWindow`, routes analysis requests through existing Commander operations,
and checks the intended Search/ApplyValueCount callback. It does not exercise a
packaged CILogg MainWindow's menus, tabs, navigation, or saved-layout restoration.

The case uses Qt mouse/key events to choose pasted-text scope and enter a pattern,
then commits a fixed Hebrew/emoji/multiline fixture through Qt's input-method
event. It clicks Run, waits for observable completed jobs, checks exact physical
source rows/counts, selects a result and clicks Apply. It verifies draft edits
invalidate Apply, releases its retained jobs, and checks docked/floating layouts.
The real input method, hardware keyboard, and compositor latency are not measured.
No clipboard, communication device, or existing user's application is used.

Both modes require a native exposed window and observed paint events. Four PNGs
capture their client contents through `QScreen::grabWindow`; the native component
samples a 64x64 grid and rejects a uniform capture. The runner verifies finite
JSON, exactly two cases, all required interaction outcomes, fixed source-fixture
and worker hashes, PNG headers/dimensions/hashes, safe filenames, and owned-job
release. A native platform, semantic results, exposure, and painting are separate
checks; none alone substitutes for the others.

## Isolation and failure behavior

Private payloads/settings use the existing G03 helpers. The start handshake assigns
the test process to its owned process tree before workers can start. Windows Job
Object/POSIX group cleanup stops and reaps owned processes before releasing the
private IPC directory lease. Workers dispatch the existing executable's internal
worker mode; they do not start or attach to its interactive application instance.

`--timeout` bounds the native child (120 seconds by default, 1–600 seconds allowed).
The report is limited to 1 MiB and each of four PNGs to 8 MiB. Captures must have
dimensions 200–8192 pixels on each axis. Existing artifact directories are rejected
and preserved. Logs and captures survive a child crash, timeout, missing report,
invalid interaction, or failed oracle. Exit codes are 0 for this probe's accepted
evidence, 1 for a failed run, and 2 for invalid setup.

Portable test doubles are explicitly `evidenceKind=test_double`; their tiny PNGs
test the validator and never establish native rendering. `releaseGateStatus` stays
`pending` even after an accepted native component run. Offscreen/minimal platforms
and uniform captures fail rather than masquerading as visible UI evidence.

The native case intentionally lacks the `[ui]` tag, so ordinary offscreen CTest
does not select it. Portable self-tests remain in the existing Workbench harness
workflow and CTest glob. `native-ui: 'true'` enables the native probe in the
representative Windows parity action, deploys its QtTest runtime, and retains its
diagnostics through the existing artifact upload. Other CI platforms retain their
existing parity gates without claiming native desktop interaction evidence.

## Regression found by the probe

The initial native run accepted input, produced correct results, and saved all four
captures, then failed because Value Counts left **Apply selected value** enabled
after editing its draft. `selected()` already rejected that stale revision.

The button now follows the same revision/selection availability: it starts
disabled, enables for a selected current result, and disables immediately on draft
edits. Selection changes and the existing refresh timer keep it synchronized with
retained rows. State-transition diagnostics include job ID, draft/job revisions,
selected row, and retained-row count, without source bodies. Public command guards,
names, schemas, and matching behavior are unchanged. The state is also visible to
CLI/MCP clients through existing `get_ui` discovery.

The ordinary Value Counts widget regression covers initial, selected, and edited
states. Native and self-test failure history is retained in
[g03c-windows-observations.json](g03c-windows-observations.json).

## Automation reliability

One public-parity run stopped during host startup before loading the requested
MCP SDK: a single read-only Commander probe exceeded its 10-second subprocess
timeout even though the existing 40-second startup deadline had time remaining.
The shared readiness helper now retries that timed-out probe within the original
absolute deadline, checks host exit before every attempt, and clamps each probe
to the remaining time. It does not restart the host or extend startup budgets.
Focused tests cover recovery, host exit, and successive 10/10/5-second probes
exhausting a 25-second fixture deadline exactly.

The aggregate portable self-test CTest timeout is 300 seconds. A measured
Python 3.9 run took 160.066 seconds, exceeding its previous 120-second cap.
This aggregate cap is separate from the native child's timeout and all product
worker/job budgets. Earlier failed attempts remain recorded alongside reruns.
