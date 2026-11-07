# D01c — Existing-owner serial adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect the checked writer to the existing serial owner, first behind an opt-in build, with acknowledged readiness/progress/retirement and atomic endpoint leases.

**Architecture:** StreamSession remains the session, policy, writer and publication owner on its existing thread; SerialCaptureWorker remains the sole QSerialPort/capture-file owner on its existing I/O thread. A private SerialWriteDriver borrows that device and executes one bounded offer at a time, returning metadata acknowledgments. It never opens a second port or creates a thread; the checked writer never crosses threads.

**Tech Stack:** C++17, existing Qt5/Qt6 Core/SerialPort, Catch2/QtTest, existing Commander and native-MCP regression harnesses.

**Spec:** [D01](../tasks/D01.md), [shared communication contracts](../07-docklight.md), [terminal integration](../11-unified-terminal.md), [D01b](D01b-shared-writer.md), [ownership baseline](../../../docs/workbench/ut00-terminal-baseline.md).

## Entry, scope and authority

- Plan baseline: PR [#98](https://github.com/dm17ryk/klogg/pull/98), head `07ae670877f65b6505bfe5fd2ff3241029841c5a`; its parent master is `6de6a3b941483fc26cb2e37509c6662199f68d55`. PR98 is still open when this plan is prepared. Dependent implementation may proceed on its frozen, reconciled head after plan review; never mix D01c edits into PR98. Merge/reconcile the parent before merging or activating D01c, and revalidate if its code changes.
- This change is a plan only. Review the production-consumer boundary before implementation. The current preparation remains D01b until its parent integration is accepted; planning does not consume its services or change formal task states.
- Proposed implementation starts with `KLOGG_SHARED_SERIAL_WRITER=OFF` by default. Test targets always compile the adapter. Opt-in builds exercise the existing serial path; default activation is a separate reviewed decision after both build modes pass. The flag is a build gate, never an authorization mechanism.
- Proposed placement refinement: D01b's plan anticipated an I/O-thread model owner, but its implementation is a QObject-independent single-owner value. Keep that value under StreamSession to validate/bound requests before Qt queues any payload and to retain history/clock across workers. Only device operations run on the I/O thread, as D01 requires. Review this explicit refinement before implementation; do not silently retain an unbounded GUI-to-worker request queue.
- Do not add terminal input, a shell, PTY, SSH, endpoint type, plugin protocol, recorder or source identity. Formal F01/D01/UT01/UT02/MD01–MD06/W0 gates remain open. No physical-port, peer-delivery or installed-package acceptance follows from a fake-device fixture.
- Preserve constructor/settings identities, `sendBytes`, connection signals and existing command names. GUI, Commander/CLI, native MCP and Python must observe equivalent existing communication behavior in the opt-in build. Future public write/terminal operations remain separately gated and must appear on all surfaces together.
- Preserve Actions/Responses coupling: disabling Actions disables Responses; enabling Responses enables Actions; the other two transitions preserve the independent flag. Created Actions tabs enable Responses; other tabs disable both. Neither saved flag arms terminal input.

## Verified problem and source impact

| Existing symbol | Current behavior | Planned change |
| --- | --- | --- |
| StreamSession::startInternal | Sets connectionOpen_ and emits connectionOpened before the worker opens the port | Hold Opening until a current-epoch backend-ready acknowledgment; publish readiness once. |
| StreamSession::sendBytes | Queues bare QByteArray/bool without connection or policy identity | Preserve the method, enqueue a bounded checked request and immutable authorization stamp; one existing worker route. |
| SerialCaptureWorker::sendData | One write; partial-write error; waitForBytesWritten(1000); accepted prefix may be called transmitted after one progress event | Opt-in path submits to the driver, continues only the unsent suffix and observes bytesWritten independently. No blocking wait or full-payload replay. |
| SerialCaptureWorker::sendData capture branch | Logs the requested body after partial/error; ordinary payload hex is diagnostic output | Project acknowledged TX only; preserve annotation/settings format, redact sensitive output and omit body diagnostics by default. Legacy capture remains presentation, not canonical recording. |
| StreamSession::setupWorker | Fences error/completion, but RX/TX callbacks and queued writes have no epoch | Fence readiness, RX, TX, error, write, stop, completion and file-switch callbacks. Reject stale work before counters, Actions, capture or public signals. |
| StreamSession::setConnectionClosed | Unregisters the port while asynchronous stop can still be running | Invalidate admission immediately; release the lease only after backend closure and worker shutdown are acknowledged. |
| StreamSourceRegistry | Check/register are separate mutex operations on a QSet | Add token-bearing atomic acquire/release under the same registry/mutex; old releases cannot release another owner. |
| actionruntime.cpp::sendActionDefinition | Emits actionSent intent, then checks policy again before queueing; delayed repeats carry Actions generations | Preserve intent/reentrancy behavior and both policy checks; carry that policy identity through the final worker offer. |

This reaches existing opening, pause/resume, delayed Actions, capture rotation and communication status flows. Treat production migration as high-impact until current GitNexus impact and direct callers are inspected before edits. A stale/empty graph is not a low-risk finding. CodeGraph reports this checkout unindexed and must not be initialized implicitly.

## Shared contracts and limits

- Reuse checked WriteRequest/WriteReceipt/WriteObservation, StreamLimits, WriteEpoch and WriteTicket; do not add a receipt codec. Preserve unmatched I/O flags. Backend-written is not peer-delivered.
- Carry D01b's 8 MiB resident private bytes across active AND retained identities, 256 entries, ten-minute retention, 1 MiB requests and 64 KiB offers. One driver may retain one additional offer of at most 64 KiB and a bounded numeric early-progress accumulator. No unbounded event/notification queue or extra full-payload history is permitted.
- Preserve one session monotonic clock origin and the original default/explicit deadline across pause and reconnect. Queue, readiness waiting and disconnected retention time count. Timers are wakeups, not clock authority; check actual elapsed time on each owner-thread event. Share only the immutable clock origin with the driver for its final pre-write deadline check.
- Zero acceptance keeps endpoint backpressure. Retry probes use one owner-thread timer: 10 ms initially, double to at most 100 ms; progress resets the delay. Each probe explicitly calls ready then attempts at most one offer. No immediate retry loop. Deadline wakeup takes priority; cancellation does not wait for a device drain.
- Only this driver writes the borrowed device. QIODevice progress is untagged; attribute it against the single accepted credit, under Qt's ordered progress contract. Reject progress exceeding available credit; do not claim arbitrary duplicate Qt events can be distinguished after attribution has advanced.
- Admit/deduplicate on the session thread before queueing. Queue at most one private offer (64 KiB), never a whole request. Worker progress uses one coalesced cumulative metadata snapshot in flight; an explicit acknowledgment permits another delivery. The next offer cannot bypass that credit. Public TX bytes are projected from the session's retained current offer, rather than queueing another private body.
- A write/error callback can arrive during the backend write call. Stage its bounded progress/error decision until the return value has established acceptance. Never enter the event loop recursively to obtain progress. Commit state before publishing metadata; guard deletion/reentry after every observer callback.
- Both user-triggered preset Actions and automatic Responses are WriteProducer::Automation. Manual means future armed terminal input, not an Actions button click. No manual producer route is enabled here.
- Preserve the existing distinction in actionContextIsCurrent: explicit preset Actions require the open/current Actions generation; they do not acquire a new Actions-enabled flag requirement. Automatic Responses additionally require current Responses generation, both tab flags and the global switch. Toggling a flag invalidates old scheduled work, but must not silently redefine a fresh explicit Action's eligibility.
- The worker checks a trusted immutable policy snapshot before each offer, not only at GUI scheduling time. Publish connection/Actions/Responses/global-auto revisions atomically as one shared snapshot; stamped queued commands cannot bypass a newer disable/close revision. This is internal stale-work fencing, not external caller authorization.
- All logs contain bounded opaque IDs, generations, ticket/count/deadline/lease decisions and reasons; omit source bodies, plaintext/hex/base64/hash payloads and unrestricted backend diagnostic text. Preserve the sensitive marker only in compatibility TX presentation, never as synthetic received/device bytes.

## Files and interfaces

New private files: `src/ui/include/serialwritedriver.h`, `src/ui/src/serialwritedriver.cpp`, `src/ui/include/serialwritebridge.h`. Keep the adapter out of klogg_ui when the opt-in flag is OFF.

New tests: `tests/communication/serialwritedriver_test.cpp`, `tests/communication/endpointlease_test.cpp`. Extend `tests/unit/streamsession_actions_test.cpp` and `tests/streamsession_testaccess.h`; use existing UI/Commander/native-MCP communication fixtures. Add the device fixture alongside communication tests; it opens no real port.

Exact interface decisions:

- SharedWriter adds `retireAfterBackendClosed(const WriteEpoch&, qint64 nowNs) -> DataStatus`: requires core closed; resolves pending acceptance/drain as ioUnconfirmed, preserves acknowledged prefixes, epoch and closed state; repeated retirement is safe. `resetAfterBackendClosed` delegates retirement before changing generation. A final close must not fabricate a next connection merely to retire unknown I/O.
- SharedWriter adds `withdrawUnsubmitted(const WriteTicket&, qint64 nowNs) -> DataStatus`: the request must already be terminal and awaiting acceptance. A trusted backend attestation that write was never entered resolves this offer to zero without inventing endpoint backpressure or an I/O fault. It never clears an existing backpressure latch. Never use it merely because an arbitrary error occurred.
- `SerialWriteContext` contains WriteEpoch, `std::chrono::steady_clock::time_point` origin and a shared immutable-policy publication handle. `SerialWriteEnvelope` contains a private WriteOffer, policy revision/origin stamp and frozen request deadline. Neither has a public serializer.
- `SerialWriteDriver::create(QIODevice&, const SerialWriteContext&) -> DataResult<std::unique_ptr<SerialWriteDriver>>`; driver and device must share the calling I/O thread. The driver borrows the already-open device and cannot open or close it.
- Driver methods: `offer(QSharedPointer<const SerialWriteEnvelope>) -> DataStatus`, `acknowledgeProgress(const WriteEpoch&, quint64 deliverySequence) -> DataStatus`, `beginClose(const WriteEpoch&) -> DataStatus`, `backendClosed(const WriteEpoch&) -> DataStatus`. After closure, disconnect backend callbacks and reject all offers.
- `BackendWriteSnapshot` contains ticket, delivery sequence, optional signed acceptance return, cumulative written count, known-not-submitted reason, fault and retirement flags; no body. Use `QSharedPointer<const BackendWriteSnapshot>` for default-constructible Qt5 queued metadata delivery. Session converts cumulative increments into existing core accepted/written calls exactly once.
- Publish `StreamWritePolicySnapshot{quint64 connectionGeneration, quint64 actionsGeneration, quint64 autoResponsesGeneration, bool connectionReady, bool actionsEnabled, bool autoResponsesEnabled, bool globalAutoResponsesEnabled}` through C++17 atomic std::shared_ptr load/store. Only the session publishes; the worker reads before entering write. Observe ActionsManager::autoResponsesEnabledChanged(bool) as well as per-session changes. A write whose final policy check already completed is in flight, not retractable cancellation proof.
- Registry adds checked `tryAcquireSerialPort(const QString&, const StreamLimits&) -> DataResult<SerialEndpointLease>` and `releaseSerialPort(const SerialEndpointLease&) -> DataStatus`. The token contains a canonical endpoint key and unique monotonic lease ID; construction is private. StreamSession retains the token until shutdown acknowledgment. Repeated release of the same retired token is safe; a foreign/stale token cannot remove a current lease.
- Resolve configured port-name/system-location aliases to one registry key before acquisition. Preserve Windows COM case/prefix equivalence; preserve case-sensitive Unix device identity and resolve existing device symlinks. Inject the resolver's port inventory in tests; do not enumerate hardware in unit tests or change stored selectors. Reject ambiguous resolution explicitly.
- Internal queued writes carry endpoint epoch, request ID, sensitive bit and policy revision/automatic-origin stamp. Add `ActionWriteStamp{bool automaticResponse, quint64 actionsGeneration, quint64 autoResponsesGeneration}` and `StreamSession::submitActionWrite(const QByteArray&, bool sensitive, const ActionWriteStamp&) -> DataResult<WriteObservation>` for actionruntime's checked enqueue. StreamSession::sendBytes remains void compatibility API; submitting/queueing is not reported as written completion.

## Task 1: Explicit retirement and undispatched-offer resolution

**Files:** `src/ui/include/sharedwriter.h`, `src/ui/src/sharedwriter.cpp`, new `src/ui/include/serialwritebridge.h`, `tests/communication/sharedwriter_test.cpp`.

- [ ] Add `[D01c-retirement]` fixtures before behavior. Close after issue, accepted/partial drain, fully drained success, repeated retirement, stale epoch and backwards clock. Assert exact counts, closed state unchanged, ioUnconfirmed only for unresolved I/O, no synthetic generation and no replay.
- [ ] Add `[D01c-withdraw]` fixtures: cancel/expire an issued but never submitted offer, attest no backend call, preserve previous drained prefix, release its barrier without latching backpressure; reject nonterminal requests, accepted/draining offers, duplicate/wrong tickets and stale clocks without mutation.
- [ ] Run the communication executable with `[D01c-retirement]`; expected RED at absent retirement behavior, not a compile/link failure. Add the compiling seam first if required.
- [ ] Implement both checked APIs and delegate the existing reset path. Define bridge metadata/envelope types; no private payload serializer. No new I/O or application linkage.
- [ ] Run `[communication]`; expected GREEN including all existing D01a/D01b fixtures. Commit `feat(terminal): acknowledge backend retirement without reconnect` after impact/scope checks.

## Task 2: Owner-thread QIODevice driver

**Files:** new driver header/source, `serialwritedriver_test.cpp`, communication test main/CMake and `src/ui/CMakeLists.txt`.

- [ ] Add `[D01c-driver]` fake-device fixtures with exact assertions: ABCDE acceptance 2/0/3 plus independently partial bytesWritten yields ABCDE once; a second request never interleaves. Capture all 256 byte values over 16-byte offers. Zero return makes no repeated call before a timer/readiness event.
- [ ] Add synchronous early progress/error, destroyed device, wrong-thread invocation, invalid progress, callback deletion/reentry, no-progress deadline and cancellation-acknowledged-within-one-second fixtures. Use observable waits and a fake monotonic clock; no short fixed sleeps or physical COM ports.
- [ ] Run new fixtures to genuine RED. Implement the factory, guarded single-offer execution, accepted-credit attribution, coalesced cumulative progress/acknowledgment and explicit retirement. Session-side tests drive retry/deadline wakeups. No waitForBytesWritten, processEvents, second device or second thread.
- [ ] Add `[D01c-driver-privacy]`: private backend sees exact secret/NUL/high-bit bytes; captured diagnostics and public metadata contain none of plaintext, hex, base64 or fingerprint forms. Require actual diagnostic messages. Record an already-passing privacy baseline honestly.
- [ ] Build a test-only `cilogg_serial_write_driver` linked to existing contracts/writer and Qt Core. Ensure QCoreApplication exists in the communication test main for timer tests. Both test and opt-in production modes use this same implementation.
- [ ] Run `[communication]`; expected GREEN and no leaked timers/queued driver work after retirement. Commit the driver deliverable.

## Task 3: Atomic leases in the existing registry

**Files:** `streamsourceregistry.h/.cpp`, `endpointlease_test.cpp`, minimal existing CMake registration.

- [ ] Add `[D01c-lease]` RED fixtures: two competing acquisitions have one winner; whitespace/Windows aliases collide; distinct case-sensitive Unix paths stay distinct; symlink aliases collide; ambiguous selectors fail; foreign and old tokens cannot release a replacement lease; sixteen endpoints fill the default cap and the seventeenth fails before growth.
- [ ] Add legacy compatibility tests: isSerialPortActive sees leased endpoints; legacy register/unregister cannot steal or release a token lease. Existing legacy-only behavior remains covered. Explicitly release fixture leases, including failure paths.
- [ ] Implement acquisition/release under the existing mutex and central registry, using immutable owned tokens and checked counter overflow. Resolve aliases outside the lock, then atomically reserve. No terminal-specific reservation registry.
- [ ] If needed for isolated tests, extract only this existing registry translation unit into one shared static target; remove its duplicate klogg_ui source entry and link the same target from app/tests. Do not compile two registry implementations into one process.
- [ ] Run `[D01c-lease]` and `[communication]`; expected GREEN, deterministic bounded concurrency and no live port opens. Commit the lease deliverable.

## Task 4: Opt-in migration under StreamSession/SerialCaptureWorker

**Files:** `src/ui/include/streamsession.h`, `src/ui/src/streamsession.cpp`, `src/ui/include/serialcaptureworker.h`, `src/ui/src/serialcaptureworker.cpp`, `src/ui/src/actionruntime.cpp`, `src/ui/src/mainwindow.cpp` communication readiness/enablement callers, `tests/unit/streamsession_actions_test.cpp`, `tests/streamsession_testaccess.h`, `tests/communication/serialwritedriver_test.cpp`, `CMakeLists.txt`, `src/ui/CMakeLists.txt` and `tests/communication/CMakeLists.txt`. No new public command catalog.

- [ ] Before editing, refresh available indexes, run impact on each changed runtime symbol and report actual callers/risk. Add source-mapped `[D01c-owner]` RED fixtures: Opening is not Ready; failed open never emits opened; close during open does not publish late readiness; close emits once; RX/TX/error/write/file-switch callbacks from an older epoch cannot affect the replacement.
- [ ] Add stale-policy fixtures for immediate and delayed Actions/Responses, including disable after queueing but before offer. Preserve existing actionSent intent and its reentrant-observer cancellation checks; atomically publish current policy before callbacks. Do not classify explicit preset Actions as armed Manual input.
- [ ] Pin fresh explicit preset Actions on a logically open tab with flags disabled: preserve current generation-based eligibility, while automatic Responses remain rejected. Verify both GUI and send_action selectors retain their existing target/eligibility semantics.
- [ ] Add lease lifecycle tests: hold during Opening/Closing; no reacquire after the public close notification until backend/worker shutdown; release on failed open and final shutdown exactly once. No open while a predecessor can still produce bytes.
- [ ] Implement backend-ready and backend-closed epoch acknowledgments in the existing worker. Create the driver after real open; close admission/cancel unsent work first, close the device on its I/O thread, then emit final metadata/retirement before worker completion. Never access the session-owned checked writer from the worker. Connect every callback with its immutable epoch/backend identity.
- [ ] StreamSession obtains the lease before starting a worker, retains its own checked writer across workers and waits for current readiness to publish connectionOpened. All core methods and admission run on the session thread. Only one bounded offer is queued to the existing worker; policy-stale/expired offers report known-not-submitted so the session cancels/expires then withdraws them. No queued payload exists before admission and no writer-state handoff occurs.
- [ ] Drive one retry/deadline timer on the session thread; acknowledge cumulative backend progress before queueing another offer. Bound/coalesce metadata delivery, preserve the single clock/budget across reconnect and guard observer deletion. Destroy the QObject driver on its I/O thread after actual backend retirement; retain only the session's existing value-state cache.
- [ ] Replace only opt-in serial TX with driver submission; keep legacy constructor, public methods, settings and OFF path. Introduce `KLOGG_SHARED_SERIAL_WRITER` default OFF; build support libraries when tests OR this flag are enabled. Link support into klogg_ui only for ON; no dependency on BUILD_TESTS in an ON production build.
- [ ] Project actual acknowledged TX prefixes into existing TX display/capture, with timestamps/annotation formatting and one sensitive marker per logical request. Failed/unknown suffixes are never captured as sent; close/clear never invent completion. Never feed TX annotations/redaction to RX/Responses/decoder source records. No durable recorder or manufactured SourceLocation.
- [ ] Capture writes acknowledged prefixes on the existing worker thread. Rotation during an unfinished TX record returns a bounded busy error without switching files or blocking for GUI progress; once drained it retains existing rotation behavior. Toggling logging preserves only the actually recorded prefixes, closes any partial TX presentation cleanly and never reconstructs a skipped suffix.
- [ ] Run existing serial/action and GUI communication regressions, shared CLI/Python/native-MCP status/action requests, callback deletion/rotation/pause/resume cases, and default OFF vs opt-in ON builds. ON must pass before proposing default activation; OFF behavior remains covered.
- [ ] Run full local CTest with discovered registrations, isolated packaged GUI smoke, relevant Qt-only/accelerated Windows/macOS/Linux CI and available Qt5 compatibility checks. Record actual skipped/unavailable gates, timing/RSS limitations and no hardware proof. Fresh C++/general review, staged impact/scope and exact package hashes precede publication. Commit only the reviewed migration.

## Review Focus

1. Reentrant progress/errors during write: Task2 proves acceptance is established before attribution and bounded early events cannot over-credit output.
2. Close/pause/reconnect with queued policy-stale writes: Task4 must fence commands and callbacks before Actions, capture, counters or readiness publication.
3. Early lease release or alias collisions: Task3/Task4 prove one canonical owner through acknowledged shutdown and stale-release rejection.
4. Retained writer state and unbounded queued bodies: Task1/Task4 preserve session-only ownership, admission before dispatch, one clock/budget and uncertainty without fabricating reconnect or delivery.
5. Observer deletion or sensitive TX becoming a fake received record: Task2/Task4 prove guarded delivery, compatible redaction and separation from RX/Responses/decoder records.

## Execution and rollback gates

Self-review this plan against D01 and the source audit before execution. Obtain review of the opt-in production-consumer boundary and reconcile the frozen PR98 head first; a ready plan is not implemented behavior. Private dependent implementation can progress while parent checks run; parent merge remains a D01c merge/activation gate. Preserve Native execution as used for D01b unless the user changes it.

Rollback disables the opt-in consumer before removing driver support. Retain established settings, serial methods, capture readability and plugin protocols. Only after D01c's implementation/proof/review may the next independently scoped preparation attach the existing terminal model to this same session; it must include GUI/Commander/CLI/native-MCP/Python behavior together and retain formal F01/D01/UT02 gates.

**Documentation checked:** Qt's [QIODevice progress contract](https://doc.qt.io/qt-6.8/qiodevice.html) and [QSerialPort waiting contract](https://doc.qt.io/qt-6.8/qserialport.html) distinguish buffered acceptance and partial written progress. Use the Qt5/Qt6 common API; do not depend on Qt6.10-only write-buffer sizing to enforce these bounds.
