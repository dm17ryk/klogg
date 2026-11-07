# D01c serial-owner planning evidence

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
