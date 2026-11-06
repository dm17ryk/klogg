# D01a — Checked communication contracts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prepare checked, bounded request/receipt value contracts needed by the existing session owner before adding asynchronous transport scheduling or terminal attachment.

**Architecture:** D01 owns these contracts under the existing UI source tree and reuses F01's `DataResult<T>`, `DataStatus` and `DataError`. Factories produce owned, immutable values. A separate test target exercises them without linking the new contracts into the application or changing the current serial path.

**Tech Stack:** C++17, existing Qt5/Qt6 Core, CMake and Catch2. No new dependency, endpoint backend or authorization service.

**Spec:** [D01](../tasks/D01.md), [shared communication contracts](../07-docklight.md), [terminal integration](../11-unified-terminal.md), and [UT00a ownership audit](../../../docs/workbench/ut00-terminal-baseline.md).

**Status:** Implementation plan only. No behavior, test execution, transport capability or formal task acceptance is credited by this document. Source audit baseline: merged master `43e00df689d3ccdb8fe918fb91aa36333ff18d71` (PR #95), 2026-11-06.

## Global Constraints

- Preserve C++17/Qt5/Qt6, current deployment choices, settings identities and plugin v1.
- Preserve `StreamSession` as the sole connection/generation/Actions/publication owner and `SerialCaptureWorker` as the existing serial I/O/file owner. Keep their constructors, public methods, signals and capture format compatible.
- This independent preparation has `task=null`, `formalParentTask=D01`, `formalTaskExecution=false` and `applicationConsumer=false`. Formal F01/D01/UT01/UT02/MD06/W0 gates and all task states remain unchanged.
- Carry parent ceilings: 16 active endpoints/workspace; 8 MiB RX staging and 8 MiB pending TX/endpoint; 64 KiB chunks; 1 MiB logical writes; 256 receipt metadata entries/endpoint, retained for at most ten minutes. D01a describes and validates policy values; enforcing queues, workspace allocation and retention belongs to subsequent slices.
- D01a metadata choices: IDs 1–128 UTF-16 units, at most 16 capability limitations of 256 units each, error codes at most 64 units and messages at most 512 units; reject invalid Unicode and embedded NUL/CR/LF in IDs. These are new bounded schema decisions, not measured performance budgets.
- `StreamLimits` accepts lower positive limits within these ceilings; chunk bytes must not exceed logical-write bytes, and logical-write bytes must not exceed pending-TX bytes. Raising a ceiling requires a separately reviewed policy change.
- Use the same frozen limits for a request and its retained receipts/codecs. A later policy reduction must not retroactively invalidate an already accepted request; its owner must retain that request's policy snapshot.
- Requests own all input text and payload storage after checking bounds. In particular, copying a `QByteArray::fromRawData` or `QString::fromRawData` view implicitly is insufficient: materialize owned storage before the factory returns.
- Requests carry private original bytes; public receipts carry metadata and true lengths. A redaction marker is never a byte event. Diagnostics contain static decisions, field names, IDs/generations, counts and limits, without source/request bodies or payload hashes.
- All future exposed increments include their GUI and shared Commander/CLI/native-MCP/Python routes together. D01a adds no public operation, persistence or live input. Manual input arming remains independent of saved Actions/Responses flags.
- Use out-of-tree `build_root`; use the authorized installed Qt 6.10.1 locally and existing CI Qt/deployment profiles. Qt5 and Windows ARM64 model evidence remain explicit until exercised.

## Current source and impact

The following are verified anchors at the audit baseline, not proposed APIs:

| Existing owner | Current behavior | Subsequent integration obligation |
|---|---|---|
| `src/ui/src/streamsession.cpp`, `sendBytes` at 301 | Queues `SerialCaptureWorker::sendData`; returns no write receipt | Keep the compatibility method and route all producers through one serialized D01 writer before enabling terminal input. |
| `src/ui/src/serialcaptureworker.cpp`, `sendData` at 242 | Calls `write` once, reports a short result and waits up to 1,000 ms; sensitive observers receive redaction text | Replace this only in a later reviewed adapter slice with suffix-only continuation and honest acceptance/completion counts. Preserve legacy capture formatting separately from canonical byte observations. |
| `src/ui/src/streamsession.cpp`, `setupWorker` at 327 | Fences completion/error callbacks with `workerGeneration_`; raw RX/TX connections at 377–380 do not capture that epoch | Later fence every ingress/write-progress callback and serialized I/O command, including queued sends; a model test is not proof that legacy callbacks are fixed. |
| `src/ui/include/streamsourceregistry.h` | Registry remains the endpoint ownership seam | Later implement atomic reservations through this owner and release after shutdown; do not introduce a terminal-specific connection registry. |
| `src/logdata/include/dataresult.h` | `DataResult<T>{optional<T> value; DataError error;}` and `DataStatus` already exist | Reuse them; do not instantiate `DataResult<void>` or add another error/result envelope. |

The code-review-graph was refreshed to the audit SHA. C++ relationship resolution has unresolved call sites, so an empty computed caller radius is not proof of isolation; these anchors were checked directly. CodeGraph could not query this checkout. No runtime symbol is edited by this planning slice. Before implementation, repeat the required symbol impact checks for actual edits and refresh relevant indexes.

## File structure for the implementation

| Path | Responsibility |
|---|---|
| Create `src/ui/include/communicationtypes.h` | Checked values/definitions, enums, limits and receipt codec declarations owned by D01. |
| Create `src/ui/src/communicationtypes.cpp` | Bounded validation, owned copies, stable stage/error mappings and metadata-only codec. |
| Modify `src/ui/CMakeLists.txt` | Add isolated `cilogg_communication_contracts` static target under `KLOGG_BUILD_TESTS`; use existing `klogg_logdata` usage requirements for F01 headers and Qt Core. Do not link it to `klogg_ui` or the app in D01a. |
| Create `tests/communication/CMakeLists.txt`, `tests_main.cpp`, `communicationtypes_test.cpp` | QCoreApplication-based Catch2 fixtures; test executable `cilogg_communication_tests` with output `communication_tests/$<CONFIG>`. |
| Modify `tests/CMakeLists.txt` | Register the new test directory; its executable is a dependency of `ci_build`. |
| Create `docs/workbench/d01a-communication-contracts.md` | Actual RED/GREEN, review and platform evidence, contract version and limitations, after implementation. |
| Update this plan's ledger/validation/manifest | Preserve dated history, formal states and hard edges; record actual evidence and the next bounded slice. |

The fixtures exercise Qt Core value behavior while inheriting the existing `klogg_logdata` transitive dependencies, including Qt Gui/Widgets/Network/SerialPort and Core5Compat where configured. Dependency isolation is not claimed by using QCoreApplication.

## Review Focus

1. Borrowed/raw storage or mutation of caller definitions after creation must not change a checked value: Task 1's ownership fixtures.
2. NUL/high-bit bytes, zero/over-limit lengths and incompatible policy values must fail or round-trip without truncation: Task 1's boundary fixtures.
3. JSON doubles/coercions, invalid stages, generation zero and inconsistent accepted/written prefixes must not become valid receipts: Task 2's negative fixtures.
4. Sensitive payloads must not enter receipt fields, error messages generated from a request, diagnostics or a synthetic byte event: Task 2's privacy fixture.
5. Value validation must not be mistaken for queue enforcement, request deduplication, physical TX completion, authority or source provenance: Task 2's evidence/surface audit and the follow-up gates below.

## Task 1: Owned request, capability and limit values

**Files:** `communicationtypes.h/.cpp`, the isolated CMake/test files above.

**Interfaces:**

- Reuse `DataResult<T>`, `DataStatus`, `DataError` from `dataresult.h`. Validation failures use `invalid_definition` or `resource_limit`, static messages and a field ID; do not echo invalid input.
- `enum class EndpointKind { Serial, TcpClient, TcpServer, Udp, NamedPipe, Hid, Replay };` Reserve local-shell/SSH configuration extensions for their owning backend cards; do not infer them from strings here.
- `StreamLimitsDefinition` contains `quint32 maximumEndpoints, maximumReceipts; quint64 maximumRxBytes, maximumPendingTxBytes, maximumChunkBytes, maximumWriteBytes; qint64 receiptRetentionMs;`, defaulted to the ceilings above (retention 600,000 ms). `StreamLimits::fromDefinition(const StreamLimitsDefinition&) -> DataResult<StreamLimits>` validates them; const accessors expose the same names.
- `EndpointCapabilitiesDefinition` contains the parent's eight flags `canRead`, `canWrite`, `messageBoundaries`, `canControlSignals`, `canSendBreak`, `canObserveBreak`, `lineErrors`, `hardwareTimestamps`, all false by default, plus `QStringList limitations`. `EndpointCapabilities::fromDefinition(const EndpointCapabilitiesDefinition&) -> DataResult<EndpointCapabilities>` owns/checks metadata. Both read/write false is valid unsupported/disconnected metadata; no capability is granted merely by choosing an endpoint kind.
- `WriteRequestDefinition` contains the parent's `QString requestId`, `QByteArray bytes`, `bool sensitive=false`, `quint64 connectionGeneration=0`, `optional<qint64> deadlineNs`. `WriteRequest::fromDefinition(const WriteRequestDefinition&, const StreamLimits&) -> DataResult<WriteRequest>` rejects an empty/oversized payload, invalid ID, generation zero or a negative supplied deadline; absent deadline remains absent. A nonnegative absolute deadline is interpretation metadata, not proof of freshness. Checking time, defaults and retry deadline preservation belongs to the scheduler/API boundary.
- Checked classes have private constructors/storage and const accessors, including `const QByteArray& WriteRequest::bytes() const`. Add no payload serializer or debug-stream operator. A copied value remains an owned immutable snapshot.

- [ ] Write the compiling absent-behavior factory/test-target seam, then the real fixtures tagged `[communication][D01a]`: `All 256 byte values survive request construction`; `Borrowed byte and QString storage is owned`; `Mutating a definition cannot alter a checked request`; `Request bounds reject before payload copy`; `Limit ceilings and cross-field bounds are checked`; `Disconnected capabilities do not grant authority`.
- [ ] Pin exact assertions: payload `00..ff` equals the original including NUL/CR/LF/0x80/0xff; original borrowed buffers can be overwritten after creation without changing the value; 1 MiB succeeds and 1 MiB+1 fails; empty payload, generation zero, negative deadline, 129-unit ID and invalid Unicode fail; capability limitation count 16 succeeds/17 fails; zero or one-over ceilings and chunk > write > TX combinations fail.
- [ ] Configure/build from `build_root` using the repository VS/Qt setup and run `communication_tests/RelWithDebInfo/cilogg_communication_tests.exe "[communication][D01a]"`. Require assertions to fail for absent behavior rather than compiler/linker/setup errors; retain the exact RED output.
- [ ] Implement the factories, owned-copy and enum/limit checks in the named files. Use a private Qt logging category `cilogg.communication.contracts` for bounded per-decision diagnostics, including success, invalid field, overflow/limit and ownership-copy outcomes; never log input values or bodies on rejection.
- [ ] Run the focused fixtures to GREEN, inspect warnings/source impact and record assertions/toolchain. Add native Windows deployment analogous to `tests/terminal/CMakeLists.txt` with `windeployqt --no-translations --no-plugins`; keep the executable outside packaged app output. Register `communication_contract_tests`, labels `communication;D01a`, timeout 60 seconds, and `ci_build` dependency. Commit the checked-value deliverable after review.

## Task 2: Checked receipts and strict metadata codec

**Files:** Extend `communicationtypes.h/.cpp` and `communicationtypes_test.cpp`; add the implementation evidence document and ledger records after verification.

**Interfaces:**

- `enum class WriteStage { Queued, Accepted, WrittenToBackend, Failed, Cancelled };` Wire names are `queued`, `accepted`, `written_to_backend`, `failed`, `cancelled`.
- `enum class PayloadDisposition { Omitted, Redacted };` Wire names are `omitted`, `redacted`; both receipt forms are metadata-only.
- `WriteReceiptDefinition` has `QString requestId, endpointId; quint64 connectionGeneration=0; WriteStage stage=Queued; quint64 requestedBytes=0, acceptedBytes=0, writtenBytes=0; PayloadDisposition payloadDisposition=Omitted; QString errorCode, errorMessage;`.
- `WriteReceipt::fromDefinition(const WriteReceiptDefinition&, const StreamLimits&) -> DataResult<WriteReceipt>` creates an owned immutable metadata snapshot. IDs/generation obey Task 1, `0 <= writtenBytes <= acceptedBytes <= requestedBytes <= maximumWriteBytes`, with requestedBytes > 0. Queued requires zero accepted/written; Accepted requires a positive accepted prefix; WrittenToBackend requires both counts equal requestedBytes. Failed requires a nonempty bounded error code; Cancelled requires `cancelled`; successful stages require empty errors. Reject invalid enum values. This validates a snapshot, not state transition order or actual backend completion evidence.
- `enum class WriteFailure { None, UnsupportedCapability, EndpointBusy, StaleGeneration, QueueFull, DeadlineExceeded, Cancelled, PartialWrite, IoError };` `WriteReceipt::forRequest(const WriteRequest&, const QString& endpointId, WriteStage, quint64 acceptedBytes, quint64 writtenBytes, WriteFailure, const StreamLimits&) -> DataResult<WriteReceipt>` derives request ID/generation/true requested length and redacted/omitted disposition from the request. Derive its error code/message from a static mapping; accept no backend text/payload argument. Feed the resulting definition through the same validator. Decode validation is not a scrubber for arbitrary externally supplied metadata text and grants no authority.
- `encodeWriteReceipt(const WriteReceipt&, const StreamLimits&) -> DataResult<QJsonObject>` and `decodeWriteReceipt(const QJsonObject&, const StreamLimits&) -> DataResult<WriteReceipt>` use a fixed version-1 object with exactly `schemaVersion`, `requestId`, `endpointId`, `connectionGeneration`, `stage`, `requestedBytes`, `acceptedBytes`, `writtenBytes`, `payloadDisposition`, `errorCode`, `errorMessage`. `schemaVersion` is numeric integer 1; generations/counts are canonical unsigned decimal strings, never JSON numbers. Require all keys; reject unknown keys/types/versions/stages, signs, whitespace, exponents, leading zeroes and overflow. Check key count, types and individual bounds before making owned domain copies, and check the validated object against a 4 KiB compact UTF-8 encoding ceiling. The object exists before this call and Qt string extraction may allocate; this codec does not guarantee bounded raw parsing. Raw-byte loaders must bound the receipt frame before Qt JSON parsing, and the shared public transport retains its existing frame bounds. No canonical JSON bytes, authenticity or duplicate-key lexical policy is claimed.

- [ ] Add fixtures `Receipt prefix counts and stages are checked`, `Receipt codec preserves exact generation`, `Receipt codec rejects coercions and extra fields`, `Sensitive receipt contains metadata only`, `Factory diagnostics exclude bodies` and `Metadata bounds reject without truncation`.
- [ ] Assert a five-byte failed write with accepted=3/written=2 is valid, while written=4/accepted=3 is rejected; queued nonzero counts and incomplete WrittenToBackend fail. Round-trip generations `9007199254740993` and `18446744073709551615` exactly. Reject numeric/bool generations, `"01"`, `"+1"`, `"1e3"`, zero/overflow generations, wrong versions/unknown keys, invalid enum values, oversized IDs/errors/encoded metadata and nested values in scalar fields.
- [ ] Use a unique synthetic secret in a private request; `forRequest` and encoded receipt preserve its true byte count and `redacted` disposition while containing neither secret nor any bytes/base64/hex field. Enable the diagnostic category in the isolated fixture, capture success and failure, and require the expected static decision records before checking privacy; an empty capture must fail. Assert absence of the fixture's plaintext, compact/spaced hex, base64 and payload-hash forms. Error text created by `forRequest` is static; decoded metadata is untrusted display data. Do not use real user logs or devices.
- [ ] Run these real fixtures to RED against compiling absent behavior, implement the checked receipt/mappings/codec, and rerun to GREEN. Check object validation occurs before serialization or owned copying of oversized input; do not implement a second `DataResult` or fabricated source location.
- [ ] Run strict warnings, native `ci_build`, focused tests and full registered CTest in an isolated deployed runtime. Confirm the new test group is registered, normal GUI/CLI/native-MCP suites still run and app/legacy serial consumers remain unchanged. Obtain fresh C++ and general/privacy review; fix findings before publication. Exercise relevant Windows/macOS/Linux CI and Qt-only/accelerated configurations; record Qt5/Windows ARM64 and other pending evidence honestly.
- [ ] Record exact baseline/RED/GREEN/build/test/CI evidence in `docs/workbench/d01a-communication-contracts.md`. Validate the canonical plan and staged Git-blob hashes, refresh code indexes and inspect staged change impact before a focused commit. Preserve formal task states and all existing historical records.

## Subsequent owner work and live-integration gates

D01a does not implement the following; keep each requirement in its owning task and scope the next independently reviewable slice before coding it:

1. **Shared asynchronous writer:** one I/O-serialized queue under the existing session lifecycle, bounded payload AND metadata count, accepted versus backend-written prefixes, 64 KiB dispatch chunks, zero-write backpressure, unsent-suffix continuation, deadlines, cancellation/drain/reset barriers, failure receipts, and no whole-request automatic retry. Deduplication is limited to retained request IDs; reject changed data under the same retained ID and preserve the original derived deadline on retry. Expiry/eviction cannot imply global exactly-once transmission.
2. **Arbitration and callback epochs:** every producer enters that same writer. Manual arming is acknowledged only after the I/O-side barrier; reject/cancel conflicting unsent automated writes and handle already-issued/accepted bytes explicitly. Arming must fail or wait visibly if a safe barrier cannot be established. A caller-supplied producer tag cannot authorize terminal replies or bypass automation permission. Fence RX, TX, write progress, queued commands, close and reconnect by endpoint identity and generation; never replay an unsent suffix after reconnect.
3. **Endpoint ownership and raw recording:** extend `StreamSourceRegistry` for atomic leases with release after reader/worker shutdown; preserve one per-file writer. Add D01 `ClockStamp`, canonical F01 `EventRef`/`EventByteSpan`, retained-byte readers and immutable snapshot backing (1,024 descriptors, 64 MiB global pinning, ten-minute default lease). Expose explicit unavailable/gap/eviction outcomes. A SourceSnapshot alone does not pin bytes; terminal rows and timestamped/annotated capture lines are not raw provenance.
4. **First public/terminal consumer:** implement the eligible capability subset through the existing `StreamSession`, Commander and catalog, with GUI/CLI/native-MCP/Python parity in that increment. Manual typing works with Actions disabled; preserve all existing Actions/Responses coupling and creation/persistence rules. Require the formal entry gates or a separately authorized independent preparation boundary; do not execute formal UT02 merely because these value types compile. Keep serial, maintained Windows/Unix shells and system SSH in full terminal acceptance.

The next preparation after D01a implementation is a bounded shared-writer/arbitration plan under D01, not automatic execution of formal UT02. Rollback removes unused contract/test targets before any consumer is introduced; later consumer rollbacks disable public mutations/attachment before removing shared contracts. D01a writes no settings, capture or device bytes.
