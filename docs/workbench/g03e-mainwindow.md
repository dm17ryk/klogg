# G03e: native MainWindow journeys

G03c exercises the real docks inside a component host. G03e adds a separate,
opt-in journey using the real CILogg MainWindow in the integration executable.
G03d's command/transport navigation work remains independently reviewable in PR70.
The default component probe and existing Commander/CLI/native MCP operations are
unchanged.

```powershell
python tests/mcp/workbench_usability.py --executable build_root/output/RelWithDebInfo/cilogg.exe --test-executable build_root/output/RelWithDebInfo/cilogg_itests.exe --probe mainwindow --artifacts build_root/reports/workbench-mainwindow
```

Use freshly built and deployed application/integration binaries. The MainWindow
journey is initially validated on Windows with the approved local Qt 6.10.1 SDK;
representative Windows Qt 6.12 CI runs it after the existing component probe.
macOS MainWindow integration is excluded consistently with the existing test
target; Linux native desktop execution requires separate evidence. Neither a
missing selected case nor an offscreen run can pass this probe.

The hosted Windows job's existing component captures are 984×688 pixels on its
1024×768 desktop. CI requests `QT_SCALE_FACTOR=0.75` for the MainWindow process
only so its minimum layout fits; the component profile is unchanged. The runner
records `qtScaleFactorRequested` separately from the observed PNG dimensions.
This is an application test setting, not a host display-mode change; see
[Qt's scaling test documentation](https://doc.qt.io/qt-6/highdpi.html#qt-scale-factor).

## Contracts exercised

- Click each actual toolbar action twice, observing visible/hidden state; open
  its actual menu and click its action. Icons and accessible controls must exist.
- Open fixed LF/UTF-8 file bytes containing Hebrew, an emoji and an unterminated
  third physical row. Wait for index publication and stable three-row readiness.
- Select whole-file scope and enter the regex through Qt input events. Click
  Run in each real dock, wait for completion and verify the frozen watermark,
  source identity against the viewer's pre-run snapshot and every raw row's
  job/revision/source before reading the UI. Require the pre-run draft revision;
  assert both Unicode match/capture spans at UTF-16 offset 7, length 1.
- Activate physical row three through Regex Lab and row two through Value Counts.
  Assert exact viewer selection, then click Apply and verify the actual search.
- Observe native exposure and painting for docked and floating views. Edit the
  draft, check Apply invalidation, commit runtime-only pasted text, release the
  retained job and close the window through its normal save path.
- Recreate MainWindow with the same session identity. Require the dock to remain
  floating and visible, with the factory-default pattern and empty text/job state,
  and capture the restored view. Value Counts' default `\S+` is retained.
  Saved dock bytes must exclude the runtime-only sentinels.

The MainWindow requests run the integration executable's internal worker mode.
Its report must identify that test binary's SHA-256, not the application worker
used by the older component probe. Both supplied binary hashes are retained.
Two required cases and six native PNGs pass the existing report/size/path/hash
bounds. MainWindow mode also requires its explicit probe kind, discovery,
navigation, layout/draft outcomes and exact file watermark/physical-line oracles.
A component report cannot substitute for it. Synthetic fixtures remain labelled
`test_double`; every report keeps `releaseGateStatus=pending`.

## Isolation and evidence boundaries

The existing private runtime/settings, literal argv, process ownership, startup
handshake, deadlines and IPC lease are reused. Jobs are released explicitly;
normal or failed child execution is followed by owned-process cleanup. The test
restores its private SessionInfo snapshot. No user's CILogg instance, captures,
communication devices or clipboard is used.

This establishes native Qt event-injected MainWindow behavior and close/recreate
layout persistence. It does not measure hardware input, accessibility assistive
technology, compositor latency, process-restart persistence, installer behavior
or the packaged launcher. Full W0 and release acceptance stay open until their
remaining independent gates have evidence. Diagnostics identify decisions,
job/source generations and termination outcomes without logging source bodies.

Rollback removes the second opt-in CI invocation, hidden MainWindow probe and
runner mode/fixtures while retaining the original component and public parity
tests. No product configuration or public schema migration is involved.

## Windows validation (2026-10-28)

The fresh Qt 6.10.1 Qt-only full target build and deployment pass; complete CTest
passes 6/6 in 250.62 seconds. Both Python 3.9 and 3.14 run all 67 Workbench
self-tests (66 passed, one platform skip). The scale-request telemetry field and
compact CI invocation were then added without changing C++; all eight focused
MainWindow self-tests and the complete native/public journeys pass afterward.

Two final MainWindow profiles pass: default scaling and requested scale 0.75.
Each executes 501 C++ assertions over the two feature cases and validates six
native captures. The compact profile's largest client capture is 817×638 pixels,
within the hosted runner's observed 984×688 component area. This local run uses
the same requested scaling profile; hosted MainWindow confirmation remains pending.
The original native component probe and all three current-master GUI/CLI/MCP
cases also pass. Both restored/active views were visually inspected.

The old binary correctly failed the new selection before implementation. An
initial native attempt passed Regex Lab but rejected the test's append to Value
Counts' default pattern; it was corrected to replace and restore factory defaults.
Review found independent-source/revision and Unicode-span oracle gaps. Three
false-green regressions were reproduced before strengthening the assertions;
they and the complete suites pass after the single review fix pass. These failed
attempts are retained separately from accepted runs.

Exact hashes, measured durations, capture metadata, reviews and tool limitations
are in [the observations](g03e-windows-observations.json). Ruff passes; actionlint
introduces no diagnostics against master, while its existing action/runner-label
findings remain. The post-edit GitNexus refresh crashed after an earlier timeout;
stale change reports establish no graph pass. Direct source/scope/hash audits and
independent reviews cover this test-only change. Full G02/G03 remain in progress.

## CI platform guard follow-up (2026-10-28)

The two PR71 macOS Qt 6.12 builds rejected the new test's `Q_OS_MAC` guard:
Qt deprecates even querying that macro, and CI treats the Clang pragma warning
as an error. `Q_OS_DARWIN` preserves the existing Apple exclusion and supports
Qt 5 and Qt 6. The CMake condition excludes MainWindow-specific test files on
Apple; the product MainWindow remains available there.

A local Clang reproducer fails with the original diagnostic and passes with the
new guard for Apple and non-Apple configurations. The Windows Qt 6.10.1 target
build, compact native MainWindow probe and five repetitions of the source
snapshot append test pass after the change. Hosted macOS Qt 6.12 confirmation
remains pending. The original full-suite observations retain their historical
source and binary hashes; `ciFollowUp` records the new validation epoch.

Master has a separate Fedora 41 ARM64 QEMU segmentation fault in the append
test, without a stack trace. Its single failed job was retried to check
reproducibility; its cause remains unresolved. Passing local Windows repetitions
do not establish a QEMU fix. No production change, test skip or warning-policy
relaxation is included. C++ and general reviews approve the narrow guard fix.
GitNexus refresh was retried and timed out; no current graph pass is claimed.
