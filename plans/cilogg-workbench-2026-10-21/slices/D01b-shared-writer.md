# D01b — Bounded shared writer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Implement the bounded, deterministic byte-stream write/arbitration core needed by the existing session owner, before migrating any live transport.

**Architecture:** An unused, single-owner `cilogg::communication::SharedWriter` consumes D01a checked values. Its owner supplies monotonic nanoseconds and acknowledged backend events; the core emits one private bounded write offer at a time. It owns no device, thread, timer, public operation or connection registry.

**Tech Stack:** C++17, existing Qt5/Qt6 Core value types, Catch2, existing communication test executable.

**Spec:** [D01](../tasks/D01.md), [shared contracts](../07-docklight.md), [terminal integration](../11-unified-terminal.md), [D01a](D01a-communication-contracts.md), and [ownership audit](../../../docs/workbench/ut00-terminal-baseline.md).

## Global Constraints

- Baseline: merged master `6de6a3b941483fc26cb2e37509c6662199f68d55`, including PR96 and PR97. Product source/tests equal the previously qualified `1074009a`; that is historical baseline proof, not a new run.
- Independent preparation: task=null, formalParentTask=D01, formalTaskExecution=false, applicationConsumer=false. Do not change formal task states, hard dependencies, settings identities, plugin v1 or deployment choices.
- Preserve StreamSession and SerialCaptureWorker ownership and all existing serial methods/signals/capture behavior. No application linkage, live input, endpoint opening, shell launch or public command is added here. The first exposed integration must include GUI/Commander/CLI/native-MCP/Python together.
- Reuse D01a `WriteRequest`, `WriteReceipt`, `StreamLimits`, `EndpointCapabilities`, `DataResult<T>` and `DataStatus`; never create a parallel receipt codec or source provenance.
- Carry 8 MiB pending TX, 64 KiB chunks, 1 MiB writes and 256 receipts at most ten minutes. Count every active AND retained private request against one resident budget equal to maximumPendingTxBytes and every entry against maximumReceipts. Evict only oldest finished entries when needed; active/barrier-held entries are never evicted. State explicitly limits deduplication to retained IDs. Workspace endpoint/RX bounds remain later owners' work.
- Limits remain frozen for the writer lifetime. A caller supplies a positive default timeout in nanoseconds; no public timeout default is introduced. Derive a missing request deadline exactly once at admission, check addition overflow and include queue/backpressure time. Clock values must be nonnegative and nondecreasing; the owner must drive advance/readiness. This core is not a real-time enforcement or device-completion proof.
- Offers contain private bytes only for the trusted backend owner; no serializer/debug stream/public observer is added. Receipts and state contain metadata. Log bounded static decisions, trusted IDs/generations, counters and limits, never payloads or payload hashes.
- Origin tags and mode state are not permission. Future adapters must authenticate/authorize the caller and restrict terminal replies before classifying writes. Saved Actions/Responses flags do not arm manual input.
- Build in build_root with approved local Qt6.10.1/MSVC, preserve Qt5/Qt6 and existing CI profiles. Never stop the user's captures for testing.

## Files and interfaces

Create `src/ui/include/sharedwriter.h`, `src/ui/src/sharedwriter.cpp`; create `tests/communication/sharedwriter_test.cpp`. Add an isolated KLOGG_BUILD_TESTS-only `cilogg_shared_writer` static target linked to `cilogg_communication_contracts`; link it into the existing communication tests and add label D01b. Do not link either preparation library into the app. Add actual evidence at `docs/workbench/d01b-shared-writer.md` and update canonical handoffs/PROGRESS/validation/manifest after proof.

All new names below are in `cilogg::communication`:

- `WriteEpoch { QString endpointId; quint64 generation=0; }`; `WriteTicket { WriteEpoch epoch; quint64 sequence=0; }`.
- `enum class WriteProducer { Automation, Manual }; enum class WriteMode { Automatic, ManualArmed };`.
- `WriteOffer` exposes const accessors `ticket()`, `requestId()`, `offset()`, `bytes()`; construction is private to SharedWriter. `WriteDispatch { std::optional<WriteOffer> offer; }` represents normal idle/backpressure without an error.
- `WriteObservation { WriteReceipt receipt; bool awaitingAcceptance=false, awaitingDrain=false, ioUnconfirmed=false; }`. Counts are acknowledged prefixes. A forced backend retirement with unresolved acceptance/drain sets ioUnconfirmed; it never invents completion. Future consumers must retain these flags alongside the existing receipt, not serialize the receipt alone as final I/O proof.
- `SharedWriterState` contains epoch, mode, optional pendingMode, closed/faulted/backpressured/awaitingAcceptance/awaitingDrain/resetRequired booleans, quint64 pendingBytes/retainedBytes/residentBytes, quint32 pendingRequests/retainedRequests, and optional qint64 nextWakeNs. closed means the core admission gate, not a physical endpoint assertion.
- Noncopyable/nonmovable `SharedWriter` uses a private implementation and checked factory `create(const WriteEpoch&, const StreamLimits&, const EndpointCapabilities&, qint64 defaultTimeoutNs) -> DataResult<std::unique_ptr<SharedWriter>>`.
- `submit(const WriteEpoch&, const WriteRequest&, WriteProducer, qint64 nowNs) -> DataResult<WriteObservation>`.
- `take(const WriteEpoch&, qint64 nowNs) -> DataResult<WriteDispatch>`.
- `accepted(const WriteTicket&, qint64 count, qint64 nowNs)`, `written(const WriteTicket&, qint64 count, qint64 nowNs)`, `failed(const WriteTicket&, qint64 nowNs)` return DataStatus. Acceptance -1 explicitly reports a rejected write with zero acceptance for that offer; lower values are invalid. A generic failed event does not resolve unknown acceptance or drain and accepts no backend text.
- `ready(const WriteEpoch&, qint64 nowNs)`, `cancel(const WriteEpoch&, const QString& requestId, qint64 nowNs)`, `requestMode(const WriteEpoch&, WriteMode, qint64 nowNs)`, `close(const WriteEpoch&, qint64 nowNs)`, `advance(const WriteEpoch&, qint64 nowNs)` return DataStatus.
- `resetAfterBackendClosed(const WriteEpoch& oldEpoch, quint64 nextGeneration, const EndpointCapabilities&, qint64 nowNs) -> DataStatus` requires the core closed and a strictly greater generation. The trusted owner must first acknowledge actual backend retirement; this method cannot prove it.
- `getWrite(const QString& requestId) const -> DataResult<WriteObservation>`; `state() const -> SharedWriterState`. Queries do not renew retention or drive clocks.

## State rules

1. Validate context before clock mutation; stale endpoint/generation/ticket callbacks and impossible counts change no state. Serialize all calls on the owning I/O thread; this value core is not thread-safe.
2. Validate IDs using D01a's 1–128 UTF-16/valid Unicode/no NUL/CR/LF rules and own endpoint text. Recheck requests against this writer's frozen bounds. Read-only and message-boundary transports cannot admit this byte-stream scheduler's writes.
3. One FIFO head owns output. take offers at most maximumChunkBytes from the accepted offset and records one ticket; another take cannot repeat an unresolved offer. Nonnegative acceptance is 0..offered bytes; -1 fails the write without inventing acceptance. Positive acceptance waits for independently acknowledged written progress before another slice. A zero acceptance latches endpoint backpressure until explicit ready or confirmed reset; cancellation, expiry, mode changes and history eviction do not clear it. Never busy-retry. written must be positive and no greater than the unacknowledged accepted prefix. Completion requires all requested bytes independently acknowledged; it is written_to_backend, never peer delivery.
4. Retained identity compares bytes, sensitive flag, generation, optional original deadline and producer. Same retained data returns the current original observation without enqueueing; changed data is request_conflict. New expired requests fail admission. Retried retained defaults never derive a fresh deadline. Expiry/eviction permits later ID reuse and is not global exactly-once delivery.
5. Cancellation stops unsent suffixes immediately. An issued offer waits for its acceptance result; accepted bytes wait for written progress. Neither cancellation nor deadline releases that barrier. Queued requests can finish without I/O; expired heads with pending I/O expose resetRequired. Backend I/O failure faults the core and fails queued work; no automatic retry or continued dispatch occurs until close/reset.
6. requestMode cancels outstanding old-mode work and blocks all new admission while draining. It acknowledges the target mode only after all issued/accepted work is resolved; if it cannot drain it remains visibly pending. Automation is rejected in ManualArmed; Manual is rejected in Automatic. Conflicting pending-mode requests fail busy; repeated identical requests are safe. Modes are trusted owner arbitration, not authorization.
7. close cancels work and closes admission without blocking or fabricating drain. A confirmed backend reset retires unresolved I/O with ioUnconfirmed, retains known prefixes, fences every old ticket and never replays a suffix. New generation resets mode to Automatic and clears pending arming. No old callback may alter new-generation state.
8. Finished entries retain private exact request identity only within the common byte/count bounds. Retention starts when a barrier-held entry can retire; oldest-finished eviction is deterministic. nextWakeNs excludes already exhausted drain deadlines to avoid timer spinning. Check all byte/time/ticket arithmetic before mutation.

## Review Focus

1. Hidden retained payload memory and borrowed endpoint text: Task1/Task3 bound and ownership fixtures.
2. Acceptance mistaken for written progress, zero-write spinning or duplicate offers: Task2 exact FIFO/backend-stream fixtures.
3. Cancel/timeout/arming racing with unresolved acceptance or drain: Task3 late-ack/barrier fixtures.
4. Old-generation callbacks and uncertain closure falsely completing/replaying output: Task3 reset and ioUnconfirmed fixtures.
5. Producer tags mistaken for authority, datagrams accidentally split, or secrets in diagnostics/metadata: Task1/Task3 capability, mode and privacy fixtures; actual authorization and protocol-reply policy remain explicit integration gates.

## Task 1: Checked owner, bounded admission and retained identity

**Files:** new header/source/test and isolated CMake registration above.

- [x] Add compiling absent-behavior interfaces and factory fixtures. Valid endpoint creation, raw QString endpoint ownership, invalid Unicode/oversized IDs, zero generation and nonpositive default timeout must distinguish missing behavior from setup errors. Run to genuine RED, implement the minimal checked factory, rerun GREEN.
- [x] Add `[communication][D01b-admission]` fixtures: with write=32/chunk=16/pending=64/receipts=3, two 32-byte requests fill the bound and a further byte fails queue_full without growth. A one-entry metadata limit rejects the second request. Broad-policy requests still obey this writer's lower bounds. Wrong epochs/request generation/unknown producers/read-only/datagram writes fail without admission.
- [x] Same-ID identical requests remain one queued record; bytes/sensitive/deadline/producer conflicts reject and preserve the original. Exercise all invalid paths before owned retention and assert exact pending/resident/count state. Run RED, implement admission, rerun GREEN plus D01a's existing fixtures.
- [x] Run focused native communication executable with `[D01b-factory],[D01b-admission]`; require real assertions and no compile/link/setup errors. Commit the checked admission deliverable after scope inspection.

## Task 2: Ordered private offers and acknowledged progress

**Consumes:** Task1 class, admitted records and immutable limits.

- [x] Add `[D01b-progress]` fixtures. Five bytes ABCDE with acceptance 2/0/3 must expose AB then CDE then CDE, require explicit ready after zero, and produce exactly ABCDE once in a recording fake backend. Queue Z behind it and forbid interleaving. Independent partial written acknowledgments must not release the head early.
- [x] Add all-256-byte/multi-chunk equality, duplicate take, over-accept/over-write/zero-written, stale/wrong-endpoint ticket and negative/backwards clock fixtures. Invalid acknowledgments do not mutate counts or advance the clock. No backend completion is inferred from acceptance or clearing.
- [x] Run fixtures to genuine RED; implement take/accepted/written/ready and checked ticket/clock arithmetic, then GREEN. Offers carry private original bytes; receipt metadata does not.
- [x] Run `[communication]` to keep D01a and Task1 green; inspect diagnostics and commit the progress deliverable.

## Task 3: Deadlines, cancellation, barriers, retirement and arbitration

**Consumes:** Task2's issued-ticket and accepted/written prefix invariants.

- [x] Add `[D01b-barriers]` fixtures: cancel before dispatch; cancel after issue before acceptance; cancel after acceptance/partial drain; deadline while queued/blocked/issued; failed continuation after acknowledged prefix; no suffix retry after failure. A second request cannot pass a held barrier.
- [x] Pin derived deadline preservation: admit at now=10 with budget=100 and absent deadline; retry at 50; advance at 110 must fail the original rather than extend it. Explicit deadline and checked-overflow cases are separate. Invalid stale callbacks with a far-future time cannot expire new work.
- [x] Mode transition during issued/accepted I/O stays pending and rejects new admission; only final drain commits ManualArmed. Mode producer conflicts, repeated transition/cancel, pending-target conflict, close/reset generation fencing and no automatic rearming/replay are explicit fixtures.
- [x] Reset unresolved I/O only after close; observations keep exact acknowledged prefixes and ioUnconfirmed=true. A drained completion is never fabricated. Old tickets, including reused sequence numbers in a newer generation, cannot advance counts.
- [x] Add `[D01b-retention]` fixtures for oldest-finished byte/count eviction, expiry at the exact retention boundary, active/barrier entries never evicted, cache-miss after eviction and permitted later ID reuse. Use a bounded scripted schedule to assert resident<=64 and pending+retained counts<=3 throughout.
- [x] Add `[D01b-privacy]` with a printable synthetic secret plus NUL/high-bit bytes. Backend offers preserve it exactly; observations and enabled diagnostic captures exclude plaintext, compact/spaced hex, base64 and hash forms. Require real static decision logs before asserting absence.
- [x] Run new lifecycle/retention behavior RED before implementation and then GREEN; record the privacy regression's already-passing baseline without fabricating a failure. Run the complete communication group and full local registered CTest in an isolated deployed runtime. Preserve existing GUI/CLI/native-MCP and serial/action tests. No fake backend evidence is called physical-port, shell, SSH, installed-package or cross-platform acceptance.
- [x] Fresh whole-branch C++ and general/privacy review; address important findings, refresh available indexes, run staged change detection, and record actual commands/results/limits before publication. Validate canonical task/requirement DAG, links and staged package hashes. Update the D01a merged history and select D01b without marking any formal task complete.

## Next preparation and rollback

After actual D01b proof/review, prepare D01c's existing-owner adapter plan: real open/acceptance/progress/close acknowledgments, epoch-fenced commands/RX/TX, compatibility capture/privacy and endpoint lease integration. Source recording/provenance and public GUI/CLI/MCP/Python surfaces remain explicit gates. Do not infer an accepted formal D01/UT02 or enable terminal input from this unused core. Rollback removes unused writer/test linkage before its supporting contract library; future consumers must be disabled before their support is removed.
