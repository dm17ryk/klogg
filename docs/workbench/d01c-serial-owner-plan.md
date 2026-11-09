# D01c existing-owner serial adapter

Implementation and qualification record, 2026-11-08. The user approved the
four-task plan after [PR #98](https://github.com/dm17ryk/klogg/pull/98) merged as
`5a3eb1450965f854e127aa3cf8f3b2f04cf54235`. Its squash tree equals the frozen
`07ae670877f65b6505bfe5fd2ff3241029841c5a`; merge `5b11207` reconciles that baseline.

`KLOGG_SHARED_SERIAL_WRITER` defaults to **OFF**. The ON variant migrates the
existing serial Actions/Responses path. StreamSession owns admission, checked
writer, policy revisions, deadlines and one retry timer on its existing thread.
SerialCaptureWorker retains the sole existing device/capture-file ownership on
its I/O thread. SerialWriteDriver borrows the open device and creates no thread.
There is no terminal input, additional endpoint type or new public command.
Existing GUI, Commander/CLI, native MCP and Python routes are preserved.

Opening becomes Ready only after actual backend open. Admission closes immediately,
while a token lease remains held through backend closure and thread retirement.
Immutable epochs fence callbacks and queued work. A stale/expired offer known not
to have entered the backend is distinct from zero acceptance and I/O uncertainty.
Only one 64 KiB offer crosses the Qt queue, with acknowledged cumulative metadata.
Core request/resident/history limits remain enforced before dispatch. Queue/retry
time counts toward the preserved one-second budget; the monotonic clock survives
reconnect. Retry delays range from 10 to 100 ms.

Explicit preset Actions preserve their open/current-generation eligibility even
with the tab Actions flag disabled. Responses additionally require both tab flags
and the global switch. Policy publication precedes observers; neither producer
arms terminal input. TX display/capture project acknowledged written prefixes,
redact sensitive requests once and never fabricate RX or peer delivery. Rotation
rejects an unfinished TX record; logging toggles preserve only recorded prefixes.
File-only worker operations remain usable without granting serial authority.

Additional owner methods live in `streamsessionwriter.cpp` and
`serialcapturewriter.cpp` under the existing classes. The private
`backendthreadretirement.h/.cpp` retains one SYNCHRONIZE-only Windows thread handle
and polls actual termination without blocking GUI close. A blocked C++ TLS
destructor reproduced early lease release even after Qt6.10.1 wait(0) returned.
Capture failure stops before device open; shared RAII closes the handle. This
decision was checked against the official
[Qt 6.10.1 implementation](https://github.com/qt/qtbase/blob/v6.10.1/src/corelib/thread/qthread_win.cpp),
[DuplicateHandle](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-duplicatehandle)
and [wait contract](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject).
No reference source was copied. Common Qt5/Qt6 device APIs enforce the bounds.
The generic result struct is now `cilogg::DataStatus` so it coexists with the
established loading enum without changing public JSON or persisted identities.

## Current qualification

Local Windows MSVC / Qt6.10.1 / C++17. New fixtures use fake devices/inventories
and byte-verified private runtimes; no installed CILogg process or real COM port
was modified.

| Gate | Observed result |
| --- | --- |
| Task 1 retirement/withdrawal | Intended RED, then 1,738 assertions / 44 cases passed. |
| Task 2 driver | Intended RED and replay RED; 2,132 assertions / 54 cases passed. |
| Task 3 leases | Eight intended failures, then 2,201 assertions / 62 cases passed. |
| DataStatus coexistence | Compile failure reproduced; communication 2,203 assertions / 63 cases passed after repair. |
| Owner migration | Initial 15-case RED; TLS-retirement and zero-expiry successor regressions each failed before repair. After review fixes, broad isolated serial suite passed 1,089 assertions / 56 cases in 10.405 seconds. |
| Review regressions | Five cases reached RED (six failed assertions); six cases / 44 assertions pass after repair, including failure-observer deletion. |
| Existing communication CLI/native MCP | Two-window status, logging, pause, coupled flags and known-action rejection on paused tabs passed; shared analysis/transport suite also passed in 38.847 seconds. |
| CI/packaging helper | 36 tests passed, two platform skips; includes eight new gate tests. Initial four accompanied implementation; later source/final-mode/SDK regressions were observed RED. |
| Full ON build / discovered CTest | Fresh ci_build/deploy passed549.006s; all10 registrations passed215.125s in an isolated runtime. |
| ON GUI / CLI / native MCP / Python SDK | Current-byte public communication probe passed; pinned SDK1.30.0 passed84.872s and2.3.0 passed103.647s; native MainWindow Qt-event probe passed28.318s. |
| Default OFF build / full CTest | Fresh ci_build/deploy passed; all10 registrations passed218.177s; communication CLI/native-MCP probe passed. Cache restored OFF. |
| Fresh C++ / general / Python review | Completed. Four Important C++ findings repaired with regression proof; general review independently confirmed two; Python/CI review had no actionable findings. |
| Hosted CI | Pending at publication; inspect the PR for current checks. No hosted success or merge is inferred from local proof. |

One root-runtime run passed 49/50 serial cases but hit an Actions settings-restore
sharing failure. The private copied runtime passed 50/50; installed/user sessions
and profiles were untouched. Full qualification uses the isolated runtime.

CI qualifies ON on existing native Windows and Qt-only Linux targets, then restores
and rebuilds OFF before packaging. Windows also runs both existing pinned MCP SDK
profiles. Cache/source identity, initial OFF/tests, every phase and final OFF
rebuild must be confirmed; standard packages retain the existing default.

The single final review fix pass acknowledges worker progress before public TX
callbacks can submit another write, and rechecks the captured epoch/retirement
state after callbacks can stop/restart the session. Same-epoch logging controls
are accepted during Opening. Unexpected Failed/deadline results now publish one
bounded existing error notification with confirmed counts and outstanding-I/O
status; intentional cancellation remains distinct. No source body enters those
notifications, and failure observers can delete the owner safely.

The ON-to-OFF build gate also reproduced stale Qt metadata: generated worker
signals still described ON-only types while compiler definitions were OFF. The
OFF UI target now uses a separate AUTOGEN_BUILD_DIR; the existing ON path and
runtime C++ are unchanged. This follows the documented
[CMake autogen directory contract](https://cmake.org/cmake/help/latest/prop_tgt/AUTOGEN_BUILD_DIR.html)
and prevents generated metadata from crossing the mode boundary. No generated
files or existing build directories were deleted.

Code Review Graph refreshed 27 source files with no parse errors. GitNexus refresh
retries stalled; new methods are UNKNOWN. Direct callers were inspected manually;
MainWindow capture start was HIGH (GUI open, restore and Commander), reported
before its pending-owner guard changed. CodeGraph is unindexed and was not
initialized. The existing `.gitnexus/lbug` modification stays outside commits.

Formal F01/D01/UT01/UT02/MD01–MD06/W0 states, hard dependencies and historical
evidence remain unchanged. Hardware/peer delivery, installed-package acceptance,
native ARM64/Qt5 runtime coverage and unmeasured throughput/RSS budgets remain open.
Default activation and terminal-model attachment require later reviewed preparation
with all existing public surfaces together. Rollback switches the consumer OFF.

## Historical pre-implementation planning record

Prepared 2026-11-07 against PR98 head
`07ae670877f65b6505bfe5fd2ff3241029841c5a`. This is a proposed implementation
plan, not an adapter implementation, production activation or new acceptance.

The [D01c plan](../../plans/cilogg-workbench-2026-10-21/slices/D01c-existing-owner-adapter.md)
keeps the existing session and worker. It covers explicit backend retirement,
bounded device offers/progress, atomic leases and opt-in legacy serial migration.
PR98 remains its parent integration gate. No endpoint was opened, and no user
capture or application session changed for this planning work.

PR98's CI Build run [42383726416](https://github.com/dm17ryk/klogg/actions/runs/42383726416)
now has all 25 workflow jobs successful at that parent head. PR98 remains open.
The separate Sourcery check was skipped, and Windows ARM64 test steps were
skipped; neither is converted into runtime/review approval. This hosted evidence
qualifies the parent, not the unimplemented D01c plan or its formal gates.

## Current source inspected

- `src/ui/include/streamsession.h` and `src/ui/src/streamsession.cpp`:
  startInternal publishes optimistic readiness; sendBytes queues untagged bodies;
  setupWorker fences completion/errors but not RX/TX; setConnectionClosed releases
  the registry entry before asynchronous shutdown necessarily completes.
- `src/ui/include/serialcaptureworker.h` and its implementation: one existing
  QSerialPort/capture owner; sendData mixes accepted bytes, one blocking progress
  wait, requested-body TX capture and public redaction. Actual open/close
  acknowledgment signals do not exist yet.
- `src/ui/include/streamsourceregistry.h` and its implementation: a central QSet
  and mutex, with separate check/register calls and no lease token.
- `src/ui/src/actionruntime.cpp`: current-generation checks before and after
  actionSent observers, delayed repeats, separate manual/automatic Actions
  generations and global automatic-response policy. `ActionsManager` exposes
  autoResponsesEnabledChanged(bool) for the proposed policy publication.
- `tests/streamsession_testaccess.h` and
  `tests/unit/streamsession_actions_test.cpp`: existing logical fixtures can mark
  a connection open without hardware; they test scheduling rather than physical
  transport. Preserve that evidence boundary.
- D01, D01b, the UT00 ownership audit, current CMake registration and the
  existing checked writer API.

## Planning decisions and self-review

1. Keep SharedWriter on the existing session thread, so admission/deduplication
   precedes any Qt queued payload. Queue at most one 64 KiB offer to the worker;
   coalesce and acknowledge cumulative metadata progress. This explicitly refines
   D01b's anticipated I/O-thread model placement. Actual device calls still run
   only on the existing I/O thread. It avoids a new request mailbox, concurrent
   model access and retained-state handoff across dying workers.
2. Add explicit retirement without inventing a next connection. Add a distinct
   trusted acknowledgment for a terminal offer whose backend write was never
   entered; zero backend acceptance remains backpressure, while -1 remains an I/O
   rejection. These outcomes must not be conflated.
3. Keep one registry and a token lease through acknowledged shutdown. Resolve
   aliases consistently, reject stale release and test the 16-endpoint cap.
4. Preserve Actions/Responses flags and generation checks. Preset Actions remain
   Automation even when the user clicks them; they do not arm manual terminal
   input. The last worker-side policy check defines already-in-flight work.
   Fresh explicit Actions keep their existing open/generation eligibility even
   when the Actions tab flag is disabled; only automatic Responses add the tab
   flags/global-switch conditions. A regression fixture pins that distinction.
5. Begin production consumption behind an OFF-by-default build flag. Test both
   modes and preserve GUI/CLI/Python/native-MCP communication behavior before
   proposing activation. No formal task or default activation is accepted here.

Self-review checked spec coverage, exact core/driver bridge interfaces, ownership,
clock/budget preservation, all five Review Focus items and the proposed test
matrix. A blocking wait or full-request Qt queue would contradict the plan.
Existing receive buffering and canonical recording/provenance remain formal
F01/D01 work; this plan does not relabel presentation capture as recording proof.

Qt documentation was queried through Context7 from the official QIODevice and
QSerialPort references: successful waiting can establish partial written progress,
not completion of the entire accepted prefix. The plan uses the common Qt5/Qt6
API and does not rely on newer write-buffer-size settings.

CodeGraph reports this checkout unindexed and was not initialized. The fresh
GitNexus retry exhausted its mainwindow.cpp parser budget and stalled; its verified
owned process job was stopped after 568.27 seconds. No completed refresh is claimed.
Neither that result nor the prior native refresh failure is runtime impact evidence. Implementation
must run current symbol impact/direct-call checks before code edits; this planning
diff changes no runtime symbols. The existing modified `.gitnexus/lbug` remains
outside the commit.

## Verification boundary

Run the canonical package validator, compare formal task records and hard DAG
with the parent, validate local links and hash exact staged Git blobs. No product
build, native CTest, hardware, package or new hosted result is claimed by this
documentation-only work. Preserve PR98's separate local and hosted evidence.

Before implementation, reconcile the frozen PR98 head and review this proposed production
consumer/thread-placement boundary. Dependent private work need not wait for the
parent's CI; parent integration is required before D01c merge/activation.
Continue with the existing Native execution
method unless the user changes it. Default activation and public terminal
attachment remain subsequent explicit gates.

## Final qualification provenance

ON executable SHA256 `34d96086ad0b2701080554161f9af1c08085dfde388e3ac87160504a04075b33`; OFF executable SHA256 `3a6411d478da1512a7563ccef79fbf275c7574a80db4080904d7eeda7c392ea3`. Both complete CTest registrations/arguments/properties were discovered and preserved; only copied runtime prefixes were relocated, with copied bytes verified. Compiler/build version metadata still names the precommit HEAD; the staged product source tree is recorded below, and source equivalence is verified before commit.

The initial ON9/10 run found a fixture bug: environment logging rules overrode setFilterRules, suppressing all privacy diagnostics. The fixture now uses a scoped category filter and restores the prior filter/message handler. The original nonempty and payload-encoding assertions remain; all63 communication cases pass under the same suppression, followed by the complete10/10 rerun.

The test-disabled ON production configuration generated all required support/UI links without a unit-test target. Its first attempts timed out in dependency checks or hit an unrelated nested CPM download; using existing local sources and disabling third-party tests/nested bootstrap passed57.883s. No separately built production binary is claimed.

Qualified normalized staged product tree `a1393f21c3b709816e45b789891913283cb041b0`; test tree `4389408c6e373ec9c639bb618d4c71fdea33ee77`.
