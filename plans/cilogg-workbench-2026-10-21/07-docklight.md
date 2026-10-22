# Docklight capability implementation plan for CILogg

> Planning and research only. This section defines 18 independently reviewable implementation tasks, D01–D18. It changes no application code. Execute the cards task by task through the repository's normal implementation workflow; the parent plan owns shared interface names and integration order.

**Goal:** Make CILogg useful for developers who need to observe, generate, simulate, test, replay, and automate communication with devices and local or network services, while retaining its general log analysis strengths.

**Architecture:** Extend `StreamSession`, Actions/Responses, the Python runtime, ScenarioRunner/lab services, Commander/CLI/native MCP, and the existing bounded `AnalysisJobService`. Add byte-oriented transport adapters, codecs, and models around those owners. Do not build another action engine, test runner, plugin host, remote lab controller, analysis job scheduler, or result retention service.

**Baseline:** Repository `dm17ryk/klogg`, `master` at `bd74c338424845a2caac03d62fdff5187e7e8f0a`, inspected 2026-10-21. P05 work at `01cb637be7c96f2c9e3925247510c162380736b7` is a separate integration input, not presumed merged. Earlier research snapshots are not sufficient evidence of current behavior; the anchors below were fetched from the immutable master commit through GitHub. `plan_build/baseline/tree.json` confirms existing paths. All paths labeled **proposed** below are new design, not claims about files already in the repository.

**Stack and constraints:** Qt/C++17; Windows VS2022 and repository-selected Qt6 SDK; retain macOS/Linux compatibility. Catch2 plus Qt Test utilities such as QSignalSpy. Existing serial, ADB, and syslog capture remain supported. Follow AGENTS.md, run required GitNexus impact analysis before symbol edits and change detection before any commit at execution time, keep builds in `build_root`, and use the existing Windows automation scripts. Do not execute those implementation steps during plan delivery.

**Spec:** `00-product-spec.md` and `MASTER_GUIDE.md`; source evidence below supplements that specification.

## Verified baseline anchors

All links below are immutable. A card's anchor references identify the existing extension points; a proposed type with a similar name must not be mistaken for current code. Source anchors use DLB01–DLB18; A05/A08 elsewhere identify parent automation tasks.

| Anchor | Exact existing file/symbols | Verified evidence |
|---|---|---|
| DLB01 | `src/ui/include/streamsession.h`: `StreamSession`, `sendBytes`, RX/TX signals, counters, logging and generation methods | [Lines 14–106](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/include/streamsession.h#L14-L106) |
| DLB02 | `src/ui/src/streamsession.cpp`: `handleDataReceived`, `handleIncomingLine`, eligibility lineage and Latin-1 line matching | [Lines 453–655](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/streamsession.cpp#L453-L655) |
| DLB03 | `src/ui/include/actionsconfig.h`: typed action fields, repeat/delay, checksum placeholder, response steps/bindings/effects | [Lines 17–186](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/include/actionsconfig.h#L17-L186) |
| DLB04 | `src/ui/src/actionruntime.cpp`: `matchResponseDefinition`, `sendActionDefinition`, `executeResponseDefinition` | [Lines 250–445](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/actionruntime.cpp#L250-L445) |
| DLB05 | `src/ui/src/actionexpression.cpp`: existing endian packers and sum/XOR/CRC builtins | [Lines 1366–1519](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/actionexpression.cpp#L1366-L1519); CRC helpers [205–237](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/actionexpression.cpp#L205-L237) |
| DLB06 | `src/ui/src/actionsconfig.cpp`: legacy declarative `sum8` / `crc16_ccitt`; existing Qt wildcard dialect | [Lines 126–176](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/actionsconfig.cpp#L126-L176); [1452–1457](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/actionsconfig.cpp#L1452-L1457) |
| DLB07 | `src/ui/include/serialcaptureworker.h`: `SerialCaptureSettings`, `SerialCaptureWorker`; `src/ui/src/serialcaptureworker.cpp`: serial setup, send, capture and error handling | [Header 9–58](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/include/serialcaptureworker.h#L9-L58); [setup 160–215](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/serialcaptureworker.cpp#L160-L215); [I/O 242–388](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/serialcaptureworker.cpp#L242-L388) |
| DLB08 | `src/ui/src/streamsourceregistry.cpp`: active serial-port ownership | [`StreamSourceRegistry::isSerialPortActive/registerSerialPort/unregisterSerialPort`](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/streamsourceregistry.cpp#L11-L45) |
| DLB09 | `src/ui/include/commander.h`: existing public operations and request selectors | [Lines 10–158](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/include/commander.h#L10-L158) |
| DLB10 | `src/ui/src/scriptsupervisor.cpp`: existing Python process/RPC supervisor, script lifecycle, subscriptions | [`runScript` 152](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/scriptsupervisor.cpp#L152); [`publishEvent` 383](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/scriptsupervisor.cpp#L383); [`handleRpcMessage` 1252](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/scriptsupervisor.cpp#L1252) |
| DLB11 | `src/app/python_runtime/klogg/client.py`: byte-carrying events, timers, `TabRef`, actions, waits, logging and callbacks | [Events 30–96](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/app/python_runtime/klogg/client.py#L30-L96); [367–399](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/app/python_runtime/klogg/client.py#L367-L399); [688–813](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/app/python_runtime/klogg/client.py#L688-L813) |
| DLB12 | `src/ui/include/scenariorunner.h`: existing `runScenario/runSuite` and reports; `klogg/test.py`: steps, assertions, artifacts, metrics and bound devices | [Header 26–40](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/include/scenariorunner.h#L26-L40); [test API 172–285](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/app/python_runtime/klogg/test.py#L172-L285) |
| DLB13 | `src/app/labcontrollerservice.cpp`: existing authenticated lab job, cancellation, binding and artifact service | [`submitBundle` 163–185](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/app/labcontrollerservice.cpp#L163-L185); [`artifactsPayload` 217](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/app/labcontrollerservice.cpp#L217) |
| DLB14 | `docs/plugin-sdk.md`: existing process protocol v1, text records, ACK/backpressure; ADB/syslog shipped | [29–41](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/docs/plugin-sdk.md#L29-L41); [85–115](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/docs/plugin-sdk.md#L85-L115) |
| DLB15 | Existing native project persistence: `MainWindow::saveProject/loadProject` and `ConfigurationExport::saveProject/readProject`; action parser accepts versions 1,3,4 | [mainwindowproject.cpp 144–184](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/mainwindowproject.cpp#L144-L184); [configurationexport.cpp 398–533](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/configurationexport.cpp#L398-L533); [actionsconfigparser.cpp 634–650](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/ui/src/actionsconfigparser.cpp#L634-L650) |
| DLB16 | `src/logdata/include/analysisjob.h`: bounded line analysis and one shared `AnalysisJobService`, paging, cancellation and source locations | [Lines 16–89](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/logdata/include/analysisjob.h#L16-L89) |
| DLB17 | `src/app/mcp/protocol.cpp`: discovery and generic Commander execution, CLI modes/jobs | [Lines 97–143](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/src/app/mcp/protocol.cpp#L97-L143) |
| DLB18 | Existing regression test anchors: `streamsession_actions_test.cpp`, `actionsconfig_test.cpp`, `commander_test.cpp` | [Lineage/reentrancy/timer tests](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/tests/unit/streamsession_actions_test.cpp#L306-L700); [action expression tests](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/tests/unit/actionsconfig_test.cpp#L78-L337); [Commander parity tests](https://github.com/dm17ryk/klogg/blob/bd74c338424845a2caac03d62fdff5187e7e8f0a/tests/unit/commander_test.cpp#L21-L130) |

### Baseline implications

- Serial RX and TX are already byte arrays. Current text capture may insert timestamps, TX labels and newlines; it is not a lossless bidirectional binary recording. `dataTransmitted` reports an observation after `waitForBytesWritten`, not proof that a peer received a complete sequence. Sensitive sends deliberately expose redaction text instead of their original payload. New binary paths must preserve that policy and distinguish redaction from actual device bytes.
- Existing action parameters include hex bytes, integers, choices and expressions; existing responses already include comments, timestamps, snapshots, stop, linked steps, captures and counters. Extend and unify those features.
- Existing `Wildcard` uses Qt's text wildcard conversion and case-insensitive matching. `handleIncomingLine` uses complete lines and Latin-1 decoding. Docklight byte `?/#/mask` semantics require a separately named `BytePattern` mode; changing the meaning of existing wildcard or regex configurations would be a regression.
- `ActionChecksumDefinition` exposes two legacy algorithms, but the expression registry already provides `sum8`, `xor8`, `crc16_modbus`, `crc16_ccitt`, `crc32`, endian packing and byte transformations. Reuse their implementations before adding algorithm variants.
- Plugin protocol v1 transports arrays of UTF-8 text records. It does not already provide a bidirectional raw-byte device transport. Binary support must be an optional versioned capability, preserving existing ADB/syslog behavior.
- Regex Lab and Value Counts remain text-analysis tools on the existing service. The protocol workbench can share job lifecycle and retention without pretending decoded text is a lossless binary source.

## Shared proposed contracts and operating rules

These names are local to this section and must be reconciled with the parent plan before implementation. All are **proposed**, except the explicitly named existing owners.

- External dependencies are the parent F01 `EventRecord`, `EventRef`, `SourceSnapshot` and exact raw-byte provenance contracts; F07 `AnalysisDatasetService` and its bounded `SourceSetHandle`/reader leases; A05 `ReplayEngine`/virtual clock; A08 `ExecutionJournal`; E01 negotiated plugin binary/event capabilities. D01 depends only on F01, not these later services. A byte event includes source/session/channel IDs, connection generation, stable event identity and per-channel byte offsets. A text line is a derived presentation. Byte-only managed sources use F01 `ManagedByteOrigin{QString resolverKind,recordingId,segmentId; quint64 generation; RawByteRange bytes;}` in the canonical EventRef origin variant; no fabricated line/UTF-16 locations or parallel EventRef.
- D01 owns `EndpointSpec`, `EndpointCapabilities`, `StreamLimits`, `WriteRequest`, `WriteReceipt`, `IStreamTransport`, `EventByteSpan`, `RecordingSnapshot` and `IRecordingByteReader`. D01 also owns the complete early `ClockStamp` schema and host I/O timestamp capture in `communicationtypes.h/.cpp`; no runtime task depends on D18. D18 only validates/certifies these contracts and integrates later F06 alignment where available. Use Qt value types/`std::optional`, C++17 and explicit enums. `EndpointSpec` has kind plus a validated typed configuration, not a parser that guesses whether a string is a COM name, URL, pipe name or HID selector.
- Proposed initial policy, subject to measured parent budgets: at most 16 active endpoints per workspace; at most 8 MiB RX staging and 8 MiB pending TX per endpoint; chunks at most 64 KiB; individual logical messages at most 1 MiB. All limits are discoverable. Reject before allocation where possible. For an unthrottleable input, close or mark an explicit capture gap when a bound is exceeded; never silently present an incomplete trace as lossless. D04 additionally caps patterns at 128 per endpoint and 1,024 tokens per pattern. These are implementation requirements to test, not current performance claims.
- Use the parent F01 `DataResult<T>`/`DataError`; `CommResult<T>` in the signatures below is only a local alias for `DataResult<T>`, not a second result/error envelope. Errors include `unsupported_capability`, `invalid_definition`, `endpoint_busy`, `stale_generation`, `queue_full`, `deadline_exceeded`, `cancelled`, `partial_write`, `recording_gap`. JSON integers that could exceed JavaScript precision, including byte offsets, counters and nanoseconds, are decimal strings.
- New public operations use existing `cilogg command --action OP --json-file request.json` parsing into a validated definition payload. Native MCP uses existing `cilogg_execute` with `{"action":"OP","arguments":{"definition":{...}}}`. Existing operations retain existing argument names. Register every added operation and schema with `cilogg_commands`; do not create a second native MCP server. Stable `tab_id`/endpoint IDs take precedence over current focus.
- A completed write means the backend's documented completion stage, never remote delivery unless the protocol supplies an acknowledgement. Expose `queued`, `accepted`, `written_to_backend`, `failed`, `cancelled`, with requested/accepted/written byte counts and the connection generation. Keep raw TX bytes private when sensitive and publish `payloadDisposition=redacted` plus true length, not a fake byte sequence that could match a device response.
- Every card requires UI, CLI and MCP parity, capability errors and cancellation; a button-only implementation is incomplete. Set Qt `objectName` and `accessibleName` on new controls. New workbench features are additive and can be disabled with a proposed `communicationWorkbench` feature switch; mature legacy serial/action paths stay available until conformance passes.

The minimum shared C++ value schema is **proposed** and belongs to D01 (the parent plan may supply equivalent named types):

```cpp
enum class ByteOrder { LittleEndian, BigEndian };
enum class TimestampOrigin { HostRead, HostWrite, Driver, Hardware };
struct ClockStamp {
    qint64 monotonicNs = 0;
    std::optional<qint64> wallUtcNs;
    QString clockDomain;
    TimestampOrigin origin = TimestampOrigin::HostRead;
    qint64 nominalResolutionNs = 0;
    std::optional<qint64> accuracyBoundNs, uncertaintyNs;
    QString calibrationId;
};
struct EventByteSpan {
    EventRef event;
    quint64 offset = 0, length = 0; // relative to EventRef.origin raw range
};
class IRecordingByteReader;
struct RecordingSnapshot {
    QString snapshotId, providerId, recordingId;
    std::optional<SourceSnapshot> physicalSourceSnapshot;
    quint64 generation = 0, firstSequence = 0, lastSequence = 0, byteWatermark = 0;
    std::shared_ptr<const IRecordingByteReader> readerLease;
};
enum class WriteStage { Queued, Accepted, WrittenToBackend, Failed, Cancelled };
struct WriteRequest {
    QString requestId;
    QByteArray bytes; // private transport input; never copied into diagnostics
    bool sensitive = false;
    quint64 connectionGeneration = 0;
    std::optional<qint64> deadlineNs;
};
struct WriteReceipt {
    QString requestId, endpointId;
    quint64 connectionGeneration = 0;
    WriteStage stage = WriteStage::Queued;
    quint64 requestedBytes = 0, acceptedBytes = 0, writtenBytes = 0;
    QString errorCode, errorMessage;
};
struct ByteChunk {
    QString endpointId, channelId;
    quint64 connectionGeneration = 0, firstOffset = 0;
    QByteArray bytes;
    ClockStamp timestamp;
    QVector<EventByteSpan> provenance; // D01 spans against canonical F01 EventRef
};
struct CaptureOptions {
    QString filePath, timestampFormat;
    bool addTimestamps = false, logTransmits = false, loggingEnabled = true;
    bool actionsEnabled = false, autoResponsesEnabled = true;
};
```

`ClockStamp`, `EventByteSpan`, `RecordingSnapshot` and their reader contract are D01-owned additions. F01 supplies `EventRecord`, `EventRef`, `SourceSnapshot`, `RawByteRange` and generic data/error types. `RecordingSnapshot` uses a frozen optional SourceSnapshot only for a physical-backed source, and otherwise canonical ManagedByteOrigin segment identities. Its explicit immutable byte-reader lease pins validated backing; SourceSnapshot alone does not pin filesystem bytes. D01 registers `resolverKind="communication"`; recordingId/segmentId are opaque QString IDs and generation is a quint64 equality epoch. RawByteRange is segment-relative [begin,endExclusive), serialized as decimal strings. D01 retained-segment descriptors carry length/content hash/backing kind; timestamps/precision belong in ClockStamp, not the origin. Expired, unavailable or hash-mismatching backing returns an error, never replacement bytes. All unspecified handles in this section are opaque QString IDs backed by bounded lifecycle registries, never raw worker pointers.

**Execution checklist for each card:** write the named behavioral fixtures and confirm the intended failure; perform required symbol impact analysis; implement only that card's interface; run its focused Catch2 and Qt integration tags, then the affected existing suites; update discovery/help/examples; obtain review before a focused commit. Suggested commands, from `build_root` after repository setup and deployment: `output\RelWithDebInfo\cilogg_tests.exe "[communication][DNN]"` and, when UI/threads are involved, `output\RelWithDebInfo\cilogg_itests.exe "[communication][DNN]" -platform offscreen`. Use `scripts/codex/run-tests.ps1` for the full required CTest gate. These are planned commands, not tests run for this document.

## D01 — Binary-safe transport lifecycle, observation and write receipts

**Integrated hard dependencies:** G01, G02, G03, F01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight treats communication as 8-bit data and distinguishes channels; CILogg needs a transport-independent byte contract before adding more endpoints. The receipt and capability model is a CILogg extension. Reference: [How serial data is processed](https://docklight.de/manual/howserialdataisprocessed.html).

**Dependencies:** F01. **Integration notes:** F01 supplies canonical event/managed-byte identity; D01 supplies retained bytes and early clock quality independently of replay, plugins and final certification. **Verified anchors:** DLB01, DLB02, DLB07, DLB08, DLB14. Existing serial constructor, start/stop and public signals must remain compatible.

**Files:** Modify `src/ui/include/streamsession.h`, `src/ui/src/streamsession.cpp`, `src/ui/include/serialcaptureworker.h`, `src/ui/src/serialcaptureworker.cpp`, `src/ui/include/streamsourceregistry.h`, `src/ui/src/streamsourceregistry.cpp`; add **proposed** `src/ui/include/streamtransport.h`, `src/ui/include/communicationtypes.h`, `src/ui/src/communicationtypes.cpp`, `src/ui/include/recordingbytesource.h`, `src/ui/src/recordingbytesource.cpp`. Wire sources through existing `src/ui/CMakeLists.txt` and tests through `tests/CMakeLists.txt`. Do not move the whole application into a new module in this card.

**Interfaces and implementation:**

- [ ] Define `enum class EndpointKind { Serial, TcpClient, TcpServer, Udp, NamedPipe, Hid, Replay };` and `EndpointCapabilities{bool canRead, canWrite, messageBoundaries, canControlSignals, canSendBreak, canObserveBreak, lineErrors, hardwareTimestamps; QStringList limitations;}`. Configuration subtypes arrive in their owning cards.
- [ ] Define asynchronous `virtual void IStreamTransport::open(const EndpointSpec&)`, `virtual void write(const WriteRequest&)`, `virtual void close()` and `virtual EndpointCapabilities capabilities() const`; signals `eventReady(const EventRecord&)`, `writeProgress(const WriteReceipt&)`, `closed(const QString&)`. All transport methods execute on their owning I/O thread.
- [ ] Add a `StreamSession(EndpointSpec, CaptureOptions)` overload and keep `StreamSession(SerialCaptureSettings)`. The existing session owns actions, generations and publication for every transport. Reserve/lease an endpoint atomically before open; release only after worker shutdown. Reject stale worker callbacks following reconnect.
- [ ] Implement `IRecordingByteReader::read(const RecordingSnapshot&, const QVector<EventByteSpan>&, quint64 maximumBytes) -> DataResult<RecordingReadBatch>` and `RecordingByteSource::freeze(const SourceSnapshot&, const RawByteRange&) -> DataResult<RecordingSnapshot>` for physical-backed sources plus `freeze(const QVector<ManagedByteOrigin>&) -> DataResult<RecordingSnapshot>` for retained segments. Cap a snapshot at 1,024 segment descriptors. `RecordingReadBatch` contains bounded `QVector<ByteChunk>`, complete/reason and diagnostics. D01 owns a transient retained-byte provider with at most 64 MiB pinned snapshots globally and a ten-minute default lease; reject unavailable/evicted ranges explicitly. It may delegate backing to an existing managed capture file, but only through identity/hash/watermark-validated reads or immutable retained segments. It is not a new durable event database, replay service or execution journal. Capture host monotonic/wall time and unknown accuracy at the I/O boundary now; late D18 certification is not needed to use basic timestamps.
- [ ] Bound queues and implement partial write continuation. A failed continuation reports an exact prefix count and never retries the entire payload automatically. Keep a separate private payload for writing and redacted event metadata for observers. Retain at most 256 receipt metadata entries per endpoint for at most ten minutes; report cache expiry and document that retry deduplication is limited to retained requests. Reject reuse of a retained request ID with different request data.

**UI + CLI + MCP:** Endpoint chooser and capability/status inspector; proposed `open_stream`, `close_stream`, `get_stream_capabilities`, `send_bytes`, `get_write`. `send_bytes` accepts `{endpointId,bytesBase64,sensitive,requestId,deadlineMs}`; retries with the same request ID return the original receipt while retained rather than transmitting twice. Existing `open_com`/`get_comm_status` still work.

**Tests:** Add **proposed** `tests/unit/streamtransport_test.cpp`, `tests/unit/streamsession_bytes_test.cpp`, `tests/ui/communicationlifecycle_test.cpp`, tags `[communication][D01]`. Cases: `All 256 byte values survive RX and TX` asserts byte equality including NUL, CR/LF, 0x80 and 0xff; `Partial writes resume only the unsent suffix` scripts returns 2/0/3 for five bytes and asserts one ordered five-byte write plus correct receipts; `Reconnect rejects an old worker callback` asserts no byte/counter/action from a prior generation; `Sensitive payload is not a synthetic device record` asserts no public raw body and no match on literal redaction text; `Queue caps fail before growth` asserts bounds and a terminal reason; `Duplicate send request returns the retained receipt` asserts exactly one backend write. QSignalSpy checks that close emits once and deletion during observation cannot access a freed session.

**Acceptance and rollback:** Legacy serial tests DLB18 pass; no queue or payload loss is silent; open/close ownership is deterministic. Disable nonlegacy endpoint creation and revert adapters if regression occurs; do not rewrite stored serial settings or remove established APIs. No claim of physical TX completion without a backend capability.

## D02 — ASCII/HEX/DEC/BIN workbench, control characters and keyboard console

**Integrated hard dependencies:** G01, G02, G03, D01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight offers four representations, control-character visibility and a keyboard console. CILogg should render the same immutable bytes in those forms, including a clear RX/TX/channel distinction. References: [Options](https://docklight.de/manual/options.html), [Keyboard Console](https://docklight.de/manual/keyboardconsole.html).

**Dependencies:** D01. **Verified anchors:** DLB01 raw signals, DLB07 presentation-bearing legacy file capture, DLB17 GUI automation.

**Files:** Add **proposed** `src/ui/include/byteviewmodel.h`, `src/ui/src/byteviewmodel.cpp`, `src/ui/include/communicationdock.h`, `src/ui/src/communicationdock.cpp`, `tests/unit/byteviewmodel_test.cpp`, `tests/ui/communicationdock_test.cpp`; integrate with existing `src/ui/src/mainwindow.cpp` after impact analysis. Keep formatting separate from transport and from the text viewer's regex input.

**Interfaces and implementation:**

- [ ] Define `enum class ByteRepresentation { Ascii, Hex, Decimal, Binary };`, `ByteViewOptions{representation,showControlNames,bytesPerRow,showOffsets,showTimestamps}` and pure `renderByteCells(const QByteArray&, const ByteViewOptions&) -> QVector<ByteCell>`, where each cell includes original byte offset. ASCII names such as NUL/CR/LF and printable 8-bit text are labeled projections, not a conversion of stored bytes.
- [ ] Define `parseByteInput(QStringView, ByteRepresentation) -> CommResult<QByteArray>` with strict token widths/ranges; ASCII input chooses an explicit encoding. Switching representations parses once then renders, avoiding lossy round trips through Unicode.
- [ ] Virtualize rows and support linked selections across all four views. Copy offers raw binary file, exact HEX, escaped text and display text as distinct formats. Filter by direction/channel/event type, including comments-only display, without changing capture or matching input. Keyboard console has explicit CR/LF policies and sends by the same D01 path, with separate raw key versus line-send modes.

**UI + CLI + MCP:** Workbench tabs and small split-view toggles, direction colors supplemented by labels, selectable control mnemonics, offsets and exact copy. Proposed `set_stream_view`, `get_stream_view`, `format_bytes`, `parse_bytes`; `send_bytes` is shared with the console. Store view preferences without altering any device bytes or capture content.

**Tests:** `[communication][D02]`: `Byte render parse roundtrip preserves all values` covers all 256 values in HEX/DEC/BIN; `Control names are display only` asserts NUL/CR/LF data unchanged; `Invalid decimal and binary cells report exact offsets` rejects 256, -1 and 9-bit binary; `Selection survives representation changes` asserts the same byte provenance and navigation span; Qt/QSignalSpy `Console applies one chosen terminator` asserts bytes for none/CR/LF/CRLF and no duplicate send from Enter plus button activation; `Virtual rows do not allocate the full recording` checks a fixed-size viewport over a million-row fake store.

**Acceptance and rollback:** Every supported input round-trips and selections remain anchored to bytes. Legacy file/log text rendering remains unchanged. Hide the new dock to roll back; it must not become the only way to inspect or send on an existing serial tab.

## D03 — Named sequences and explicit byte parameters

**Integrated hard dependencies:** G01, G02, G03, D01, D02. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight Send Sequences have names, documentation and parameter areas; `?` requires one byte and `#` allows zero or one byte. Its GUI automatic answer cannot directly use wildcard parameters; scripted paths can supply them. CILogg already has richer typed action parameters, so extend those fields rather than build a new sequence repository. References: [Send Sequence editor](https://docklight.de/manual/dialogeditsendsequence.html), [Parameters](https://docklight.de/manual/sendingsequencewildcard.html), [SendSequence scripting](https://docklight.de/manual/dl_sendsequence.html).

**Dependencies:** D01, D02. **Verified anchors:** DLB03, DLB05, DLB06, DLB18 action parameter/serialization tests.

**Files:** Modify existing `actionsconfig.h/.cpp`, `actionsconfigparser.cpp`, `actioneditdialog.cpp`, `actionruntime.cpp`; add **proposed** `src/ui/include/sequencebyteeditor.h`, `src/ui/src/sequencebyteeditor.cpp`, `tests/unit/sequencebytes_test.cpp`; extend existing `tests/unit/actionsconfig_test.cpp` and UI action-dialog fixtures.

**Interfaces and implementation:**

- [ ] Extend `ActionParameterDefinition` with an optional byte field specification `ByteFieldSpec{qsizetype minBytes,maxBytes; ByteRepresentation input; std::optional<ByteOrder> order;}` and explicit encoding for text parameters. Existing `HexBytes`, integer/choice fields and expression packers remain authoritative.
- [ ] Add `validateActionParameters(const ActionDefinition&, const QVariantMap&) -> CommResult<QVariantMap>` and `previewActionBytes(...) -> CommResult<SequencePreview>`, returning resolved bytes, field spans and checksum/framing annotations. Sending continues through `actionDefinitionToBytesWithParameters` and `sendActionDefinition`.
- [ ] Represent imported wildcard regions as parameter slots with exact min/max byte counts. Literal 0x3f/0x23 remain ordinary bytes; only explicit slot tokens are parameters. Reject missing/too-long parameters, unknown encodings and ambiguous duplicate names before send. Do not silently truncate, pad or guess values.
- [ ] Keep sequence names/documentation, order, enabled state and existing IDs in ActionsManager/ActionsRepository. D16 owns cross-project ID remapping.

**UI + CLI + MCP:** Reuse Actions editor and typed parameter dialog, add four byte-entry modes and a resolved-wire preview. Existing `create_action`, `update_action`, `get_actions`, `send_action` gain schema fields and return the same preview diagnostics; proposed `preview_action` returns bytes without opening a connection. GUI and command sends use the same validator, including sensitive parameter handling.

**Tests:** `[communication][D03]`: `Required byte slot accepts exactly one byte` checks min=max=1, including NUL; `Optional three byte region accepts zero through three bytes` rejects four; `Byte count differs from Unicode character count` checks UTF-8 e.g. one multibyte character versus one Latin-1 byte; `Literal question and hash are not parameters`; `All action creation surfaces produce identical payloads`; `Action v1 v3 v4 fixtures retain their previous bytes`; `Sensitive preview omits payload and remembered value`. Assert no transport write after any validation failure.

**Acceptance and rollback:** Previously saved actions keep byte-identical output. New parameter schemas have an explicit version and can be rejected by older code without corrupting old definitions. Disable new byte field editing and retain the previous repository file on rollback; no bulk automatic migration in this task.

## D04 — Incremental byte receive patterns, optional bytes and bitmasks

**Integrated hard dependencies:** G01, G02, G03, D01, D03. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight matches byte sequences using exactly-one `?`, zero-or-one `#`, and mask/value bit tests. The dedicated handshake page identifies `^` as bit comparison and `!` as a signal function; the glossary contains an apparent contradictory character label. Use typed tokens internally, not undocumented punctuation guesses. References: [Wildcards](https://docklight.de/manual/wildcard.html), [Receive wildcards](https://docklight.de/manual/receivesequencewildcard.html), [Bit comparison and handshake functions](https://docklight.de/manual/setandreadhandshake.html).

**Dependencies:** D01, D03. **Integration notes:** Signal predicate execution is added by the later serial backend without changing the byte matcher contract. **Verified anchors:** DLB02 complete-line loop; DLB03 `ResponseMatchType`; DLB04/DLB06 existing text wildcard; DLB18 generation/reentrancy cases.

**Files:** Modify existing `actionsconfig.h/.cpp`, `actionsconfigparser.cpp`, `actionruntime.cpp`, `streamsession.cpp`, `responseeditdialog.cpp`; add **proposed** `src/ui/include/bytepattern.h`, `src/ui/src/bytepattern.cpp`, `tests/unit/bytepattern_test.cpp`, `tests/unit/streamsession_bytepatterns_test.cpp`.

**Interfaces and implementation:**

- [ ] Add separate `ResponseMatchType::BytePattern`; keep `Wildcard` as its current text dialect. Define `ByteToken = variant<LiteralByte,AnyByte,OptionalByte,MaskedByte>`, `MaskedByte{quint8 mask,value;}` with comparison `(byte & mask)==(value & mask)`. Define `BytePatternDefinition{tokens,channelSelector,overlapPolicy,captureRegions}` and `ByteMatch{ruleId,generation,startOffset,endOffset,eventSpans,captures}`.
- [ ] Implement `BytePatternMachine::compile(const BytePatternDefinition&) -> CommResult<CompiledBytePattern>` and `feed(const ByteChunk&, quint64 eligibleGeneration) -> QVector<ByteMatch>`. Use a bounded finite-state machine with state retained across driver chunks and no text decoding. Reject empty or all-optional zero-length patterns, more than 1,024 tokens and more than 128 endpoint patterns; no unbounded star wildcard in this dialect.
- [ ] Define matches by earliest completion, then start offset, then rule order. Deduplicate equivalent optional-token paths with identical spans; capture ambiguity resolves by taking the longest earlier optional region. Expose `overlapPolicy=all|nonoverlapping` explicitly. Every input byte has one channel offset, independent of chunk partitioning.
- [ ] Reuse existing automatic-response eligibility and reentrancy guards. A partial pattern spanning disabled or older-generation data cannot become eligible after re-enable. Observe data while disabled without carrying its partial match into a later enabled generation.

**UI + CLI + MCP:** Add an explicitly named Byte Pattern receive mode, literal/token colors, mask editor, byte offsets, captured bytes and match explanation. Existing response CRUD operations accept the typed definition; proposed `test_byte_pattern` performs bounded inline evaluation, promoted to D17 shared jobs for recordings. No automatic interpretation of ordinary regex or wildcard input as a byte pattern.

**Tests:** `[communication][D04]`: `Every partition of a byte stream yields identical matches` enumerates partitions of a small NUL/high-byte example; `Question is one byte and optional is at most one` checks token AST versus literal 0x3f/0x23; `Masked byte matches only selected bits` loops all 256 values; `Overlaps and optional ambiguity have stable spans`; `Cross chunk disabled segment stays observation only`; `Reentrant callback invalidates the rest of an old generation`; `Pattern complexity rejects before allocation`; `Legacy wildcard still uses its saved text dialect`. Assert exact ordered `{start,end,captures}` and one counter increment per emitted match.

**Acceptance and rollback:** Results are chunk-partition invariant and bounded; no line ending is required to match. Disable BytePattern registrations to roll back. Do not reinterpret or rewrite preexisting responses.

## D05 — Receive effects, counters, logging control, stops and triggered snapshots

**Integrated hard dependencies:** G01, G02, G03, D01, D04. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight receive actions can answer, comment, trigger a before/after snapshot or stop communication; scripting exposes receive counters and logging. CILogg already implements these concepts. Complete them for byte events and make snapshot/logging behavior precise. References: [Actions](https://docklight.de/manual/action.html), [Snapshot](https://docklight.de/manual/snapshot.html), [Trigger](https://docklight.de/manual/trigger.html), [StartLogging](https://docklight.de/manual/dl_startlogging.html).

**Dependencies:** D01, D04. **Integration notes:** Snapshot export/persistence is a later optional recording integration; matching and snapshot retention use the early byte-reader contract. **Verified anchors:** DLB01 counters/logging, DLB03 response effects, DLB04 existing execution/finalization, DLB11 logging/counter API.

**Files:** Extend existing `streamsession.h/.cpp`, `actionruntime.cpp`, `actionsconfig.h/.cpp`, `responseeditdialog.cpp`; add **proposed** `src/ui/include/communicationsnapshot.h`, `src/ui/src/communicationsnapshot.cpp`, `tests/unit/responseeffects_bytes_test.cpp`, `tests/ui/communicationsnapshot_test.cpp`.

**Interfaces and implementation:**

- [ ] Normalize line/byte matches into `ResponseObservation{ruleId,matchSpan,generation,captures}` consumed by one `executeResponseDefinition` path. Preserve existing signal/API compatibility through adapters; avoid matching both a derived line and its underlying BytePattern rule twice.
- [ ] Define `SnapshotPolicy{quint64 preBytes,postBytes; int postDeadlineMs; bool rearm;}` and `SnapshotHandle triggerSnapshot(const ResponseObservation&, const SnapshotPolicy&)`. Keep a bounded pre-trigger ring, pin exact event spans on trigger, collect post-trigger bytes until byte/deadline/stop limits, and report partial reason on close. This is a communication event snapshot, not a screenshot or the source metadata snapshot.
- [ ] Add explicit response effects for start/stop logging and counter reset if absent from the current schema. Counter storage becomes saturating/checked `quint64`; JSON exposes strings. State resets are per session/rule unless an explicit all-sessions operation is requested.
- [ ] Order effects deterministically: publish/count match; resolve and validate reply steps; schedule sends; append comment/marker; trigger snapshot; apply final logging/stop effects. Stop cancels remaining automatic work by generation. Return partial execution with completed effect IDs if a later effect fails; never claim a rolled-back physical send.

**UI + CLI + MCP:** Reuse Responses editor and existing `start_logging`, `stop_logging`, `get_response_counter`, `reset_response_counter`, `add_comment`, `stop_comm`; extend their schema to byte streams. Proposed `arm_comm_snapshot`, `get_comm_snapshot`, `release_comm_snapshot`; inspector shows matched byte span, counters, partial status and pre/post limits.

**Tests:** `[communication][D05]`: `Cross chunk match increments once`; `Two endpoints have independent counters`; `Stop invalidates a queued reply and does not revive on reconnect`; `Pre post snapshot spans the exact trigger` verifies wraparound, simultaneous directions, prehistory shortfall and post timeout; `Logging toggle preserves observations but controls persistence`; `Saturated counter never wraps negative`; `Observer deletion prevents subsequent effects`; `Failed second effect reports first effect as completed`. Existing DLB18 observer/timer regressions remain mandatory.

**Acceptance and rollback:** The same response definition behaves identically on legacy lines and its explicit byte-mode counterpart, except documented scope differences. Release snapshot pins/rings on disable. New effects have versioned serialization; preserve old response files and existing counters API.

## D06 — One checksum/CRC implementation for TX placement, RX validation and previews

**Integrated hard dependencies:** G01, G02, G03, D03, D04. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight supports generated send checksums and receive rules that select correct, incorrect or either checksum; its checksum specification describes ranges, byte order/format and standard CRC variants. CILogg should expose its existing algorithms consistently. References: [Calculating/validating](https://docklight.de/manual/generatingchecksums.html), [Checksum specification](https://docklight.de/manual/checksum_specification.html), [Receive checksum modes](https://docklight.de/manual/dialogeditreceivesequence.html).

**Dependencies:** D03, D04. **Verified anchors:** DLB03 legacy `ActionChecksumDefinition`, DLB05 existing sum/XOR/CRC and packers, DLB06 legacy two-algorithm adapter.

**Files:** Extend existing `actionsconfig.h/.cpp`, `actionexpression.cpp`, action/response editors; add **proposed** `src/ui/include/protocolchecksum.h`, `src/ui/src/protocolchecksum.cpp`, `tests/unit/protocolchecksum_test.cpp`. Consolidate existing helper bodies into this shared unit without changing their named expression results.

**Interfaces and implementation:**

- [ ] Define `ChecksumSpec{algorithm,width,polynomial,initial,xorOut,reflectIn,reflectOut,rangeStart,rangeLength,targetOffset,ByteOrder,encoding}`; nullable length means to end of the logical payload. `calculateChecksum(QByteArrayView, const ChecksumSpec&) -> CommResult<QByteArray>` and `verifyChecksum(...) -> CommResult<ChecksumResult>` return calculated/observed bytes, ranges and `valid|invalid|unavailable`.
- [ ] Start with byte-identical adapters for existing sum8/XOR8/Modbus CRC16/CCITT-FALSE/CRC32 functions, then add declarative parameterized CRC8/16/32 only against golden vectors. Keep algorithm identity explicit: “CCITT” alone is insufficient to select initialization/reflection.
- [ ] Extend response checks to `accept=valid|invalid|either` and expose the actual result/captured bytes. RX validation never mutates stored data into textual 0/1 markers. TX placement is binary or explicitly selected ASCII encoding, with validated ranges and placeholder exclusions. D07 supplies raw/logical/wire stage selection.
- [ ] Adapt legacy placeholder definitions through the old semantics, including their existing text versus hex rendering; do not silently replace ASCII checksum text with raw bytes.

**UI + CLI + MCP:** Checksum pane shows covered bytes, target span, endianness, calculated value and outcome. Existing action/response CRUD gains typed checksum fields; proposed `calculate_checksum`, `verify_checksum` use the same implementation and are callable without an endpoint. Resolved action preview includes the checksum stage.

**Tests:** `[communication][D06]`: `Named CRC vectors for 123456789` asserts Modbus=0x4B37, CCITT-FALSE=0x29B1, CRC32=0xCBF43926, and sum8=0xDD; `Endian encoding changes bytes not the numeric CRC`; `RX valid invalid either modes classify correctly`; `Range and target overflows reject before slicing`; `Empty payload has specified initial result`; `Chunked verification equals contiguous verification`; `Legacy expressions and checksum placeholders keep exact output`. Include mutations at every protected byte and fixtures with checksum bytes inside versus outside coverage.

**Acceptance and rollback:** All existing action expression tests pass and every path calls the same implementation. Version new range/placement fields; retain legacy checksum adapters and saved fixtures on rollback. Unsupported imported checksum syntax is a diagnostic, never a guessed CRC flavor.

## D07 — Framing, escaped payloads and byte stuffing

**Integrated hard dependencies:** G01, G02, G03, D01, D04, D06. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight's official examples include byte stuffing and its OnSend callback permits stream transformations. A general declarative framing pipeline and protocol inspector are CILogg extensions, not claims of a Docklight native SLIP/COBS editor. References: [Examples](https://docklight.de/examples/), [OnSend data manipulation](https://docklight.de/manual/subdl_onsend%28%29-sendsequenc.html).

**Dependencies:** D01, D04, D06. **Verified anchors:** DLB02 line-only assembly, DLB05 existing pure byte-expression helpers, DLB11 event callbacks.

**Files:** Add **proposed** `src/ui/include/protocolframing.h`, `src/ui/src/protocolframing.cpp`, `tests/unit/protocolframing_test.cpp`; extend response/action schema, stream session derived-event publication and D02 inspection views. Do not change the raw event store to store only decoded frames.

**Interfaces and implementation:**

- [ ] Define `FrameSpec` variants `DelimiterFrame`, `FixedLengthFrame`, `LengthFieldFrame`, `IdleGapFrame`; `EscapeSpec` variants none/escape-map/SLIP/COBS. Specify delimiter inclusion, endian length field, length adjustment, maximum frame bytes, resync rule and truncated-tail policy.
- [ ] Implement `FrameDecoder::feed(const ByteChunk&) -> QVector<FrameEvent>` and `flush(FrameFlushReason)`. `FrameEvent` contains raw event spans, logical bytes, decode status and timing range. Keep malformed, truncated and resynchronization events visible rather than discarding them.
- [ ] Define TX pipeline `parameters/expression -> logical payload -> checksum at selected logical stage -> framing/escaping -> wire bytes`. Provide an explicit wire-stage checksum option for protocols that require it and reject impossible self-covering ranges. RX reverses escaping/framing and verifies against the selected stage; show both representations and provenance mapping.
- [ ] Idle gaps use D01 ClockStamp quality metadata; a host-chunk timestamp cannot prove per-byte idle boundaries. Return `timing_insufficient` or an explicitly labeled heuristic result rather than claim precise RTU framing.

**UI + CLI + MCP:** Frame profile editor, escaped/raw/logical tabs and error markers. Proposed `set_stream_framing`, `get_stream_framing`, `encode_frame`, `decode_frames`; action preview and byte-pattern editor select raw or logical frame scope. Default remains raw stream.

**Tests:** `[communication][D07]`: `SLIP and COBS roundtrip all byte values`; `Escape pair split across chunks decodes once`; `Length field overflow and oversize frames resynchronize at the documented boundary`; `Delimiter inside escaped payload does not end a frame`; `Checksum order differs before and after stuffing exactly as specified`; `Truncated final frame stays visible`; `Idle framing rejects an insufficient clock`; exhaustive split positions and fuzz/property tests assert bounded memory and source-span roundtrips.

**Acceptance and rollback:** Raw bytes remain authoritative and all transformations are reproducible with an exported profile. Disable derived frame processing without interrupting capture. Profile upgrades never replace a saved raw capture or reinterpret a legacy line response.

## D08 — Periodic sends, inter-byte delays, deadlines and cancellation

**Integrated hard dependencies:** G01, G02, G03, D01, D03, D05. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight supports periodic send intervals and inter-character delays; its documentation cautions that repeat intervals below approximately 30 ms are usually inaccurate. CILogg already schedules delayed/repeated actions; replace excessive timer fan-out with bounded scheduling inside the existing action runtime. References: [Send editor timing](https://docklight.de/manual/dialogeditsendsequence.html), [Inter-character delays](https://docklight.de/manual/creatingintercharacterdelays.html).

**Dependencies:** D01, D03, D05. **Integration notes:** Typed signal/parity/break steps initially return unsupported_capability; the serial backend adds their execution later. **Verified anchors:** DLB03 `ActionParameters`, DLB04 single-shot repeat loop/generation checks, DLB11 Python timers, DLB18 delayed-action regressions.

**Files:** Modify existing `actionruntime.cpp`, `actionsconfig.h/.cpp`; add **proposed** `src/ui/include/actiondispatchqueue.h`, `src/ui/src/actiondispatchqueue.cpp`, `tests/unit/actiondispatchqueue_test.cpp`. This queue is an implementation component of the existing runtime, not another automation service.

**Interfaces and implementation:**

- [ ] Define `ActionSchedule{initialDelayMs,periodMs,std::optional<quint64> count,std::optional<qint64> deadlineNs,latePolicy}` with `latePolicy=skip|delay|fail`; default finite schedules preserve old semantics. `ScheduledActionHandle scheduleAction(StreamSession*, const ActionDefinition&, const QVariantMap&, const ActionSchedule&)` and `cancelScheduledAction(handle)` report attempts, receipts and measured lateness.
- [ ] Use one next-deadline timer per active endpoint/runtime queue and monotonic time; cap outstanding schedules at 256 per endpoint. Avoid preallocating one QTimer/captured payload for every repeat. Do not burst all missed iterations after a stalled GUI or reconnect.
- [ ] Define `TransmitPlan=QVector<variant<WriteBytes,Delay,SetSignals,SetParity,SendBreak>>`; execute plans serially on one endpoint with cancellation boundaries and completion receipts. Inter-byte delay is a best-effort transport schedule unless hardware says otherwise; no UI thread sleeps or hidden realtime guarantee.
- [ ] Timeout includes queue time. Stop/disable/close invalidates the captured generation. Keep manual versus automatic eligibility separate, matching current DLB18 semantics. A sent prefix cannot be undone.

**UI + CLI + MCP:** Reuse action repeat/delay editor, add finite/until-stopped scheduling and visible lateness/deadline fields. Proposed `schedule_action`, `get_action_schedule`, `cancel_action_schedule`; existing `send_action` continues finite dispatch. All return a handle; waits use cancellable status queries rather than blocking the GUI.

**Tests:** `[communication][D08]`: fake clock `A million requested repeats retain one next dispatch` asserts constant scheduling memory and the limit; `Late skip sends no catch up burst`; `Deadline expires while queued without a write`; `Disable cancels automatic but preserves allowed manual work`; `Reconnect cannot replay a stale timer`; `Interbyte and signal steps preserve exact order`; `Cancellation reports an already sent prefix`. Retain all DLB18 tests. A separate HIL test measures jitter; fake-clock success is not a precision claim.

**Acceptance and rollback:** No timer fan-out, checked duration arithmetic, deterministic fake-clock results and visible best-effort timing. Disable new schedule modes and use legacy finite actions on rollback; stop active schedules before switching implementations.

## D09 — Serial handshake, break, parity control and honest diagnostics

**Integrated hard dependencies:** G01, G02, G03, D01, D08. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight can control RTS/DTR, observe CTS/DSR/DCD/RI changes, send/detect break states and change parity within a sequence. Its break documentation cautions that a detected break's position can precede its actual byte position. These are capability-dependent observations, not universal properties of a USB adapter. References: [Handshake/signals](https://docklight.de/manual/setandreadhandshake.html), [Break](https://docklight.de/manual/settinganddetectingabreak.html), [Adapter limitations](https://docklight.de/dl_prb009/), [Qt SerialPort](https://doc.qt.io/qt-6.12/qserialport.html), [Windows serial events](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-waitcommevent).

**Dependencies:** D01, D08. **Integration notes:** Timestamp metadata already exists in the early byte foundation; later certification is evidence, not a runtime prerequisite. **Verified anchors:** DLB07 Qt serial setup/error handling; DLB08 exclusive ownership. Existing `errorOccurred` is not a portable per-byte parity/framing/error event API.

**Files:** Extend `serialcaptureworker.h/.cpp`, serial settings serializer and COM dialog; add **proposed** `src/ui/include/serialdiagnostics.h`, `src/ui/src/serialdiagnostics.cpp`, `src/ui/src/serialtransport_win.cpp`, `src/ui/src/serialtransport_posix.cpp`, `tests/unit/serialdiagnostics_test.cpp`, `tests/ui/serialcontrols_test.cpp`. Native backend files are capability-gated and own the serial handle when selected; do not attach a competing native event wait/read loop to QSerialPort's handle.

**Interfaces and implementation:**

- [ ] Define `SerialSignals{optional<bool> rts,dtr,cts,dsr,dcd,ri;}` and `SerialDiagnosticEvent{kind,nativeCode,observedRange,optional<byteSpan>,clockStamp}` with kinds break, parity, framing, overrun, driver/resource error. Unknown signal/error state remains unknown, never false/zero.
- [ ] `getSerialSignals(endpointId)`, `setSerialSignals(endpointId, SignalUpdate)`, `sendBreak(endpointId, durationMs)` and `applySerialSettings(endpointId, settings, boundaryPolicy)` return capability/result types. Verify setter/open results; retain requested and accepted settings separately. Reject manual RTS control when hardware flow control owns RTS.
- [ ] Portable backend uses supported Qt control/read APIs. Advanced diagnostic backend is selected only where native driver/event handling has been implemented and tested. Windows EV_ERR/EV_BREAK plus detailed line status can identify conditions, but byte association and ordering remain uncertain unless the backend supplies evidence. POSIX and adapter support vary; capability detection, not OS-name guessing, decides availability.
- [ ] Parity switching and break enter/leave are ordered D08 transmit-plan operations. Capture signal/error events out of band so binary payload bytes remain intact. Include basic connect/disconnect, requested/applied baud, flow mode, error counts and lost-data markers in status.

**UI + CLI + MCP:** Signal panel with supported/unknown indicators, manual controls, break duration and diagnostic timeline. Proposed `get_serial_signals`, `set_serial_signals`, `send_break`, `set_serial_settings`, `get_serial_diagnostics`; existing `get_comm_status` includes capabilities and last error. Byte-pattern rules can reference typed signal predicates without reserving payload byte values.

**Tests:** `[communication][D09]`: `Unsupported input signal remains unknown`; `RTS write conflicts with hardware flow control`; `Setter failure does not report requested baud as applied`; `Break is a sideband event and never inserts a percent byte`; `Parity step waits behind prior TX completion`; `Driver overrun creates an explicit loss marker`; `Break event with uncertain position cannot claim an exact byte span`; cancellation during break restores a non-break state when the port is still available. Native-event fixtures plus HIL loopback/wired-signal cases verify actual capability badges.

**Acceptance and rollback:** Portable serial remains usable without native diagnostics. Show no parity/framing/break-RX support badge until evidence exists for that backend/driver. Disable the native backend and advanced controls on rollback, keeping diagnostic records readable and ordinary Qt serial available.

## D10 — Two-port passive communication monitoring

**Integrated hard dependencies:** G01, G02, G03, D01, D02, D09. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight can receive each direction of an existing device-to-device link through two COM inputs using a monitoring cable/tap. Its expert options separately describe software data forwarding, enabled by default in Docklight Monitoring mode. CILogg must make passive observation and forwarding distinct choices, with passive mode performing no software writes. References: [Two-device monitoring](https://docklight.de/manual/monitortheserialcommunicationb.html), [Expert options/forwarding](https://docklight.de/manual/dialog_expert_options.html).

**Dependencies:** D01, D02, D09. **Verified anchors:** DLB01 session lifecycle, DLB08 serial reservations; no existing two-channel monitor engine is established by those anchors.

**Files:** Add **proposed** `src/ui/include/communicationgroup.h`, `src/ui/src/communicationgroup.cpp`, `src/ui/include/monitorsetupdialog.h`, `src/ui/src/monitorsetupdialog.cpp`, `tests/unit/communicationgroup_test.cpp`, `tests/ui/passivemonitor_test.cpp`; extend the existing registry and session metadata.

**Interfaces and implementation:**

- [ ] Define `MonitorDefinition{QString id; EndpointSpec channelA,channelB; QString labelA,labelB; MonitorMode mode=Passive;}` and `openMonitor(const MonitorDefinition&) -> CommResult<MonitorHandle>`, `closeMonitor(handle)`. Reserve both serial endpoints atomically, then open; unwind both on partial failure.
- [ ] Capture independent channel sequences/offsets and share a group ID. RX on monitor A is data from device A, not host TX; label directions by the user's endpoints. `canWrite=false` for passive monitor routes even if the underlying COM handle could write.
- [ ] Provide wiring diagram/help for read taps and required electrical interface. Software cannot passively attach to an arbitrary port already opened exclusively by another process; advise choosing a real tap, a second receive path, or the explicit D12 bridge architecture in the product's setup flow.
- [ ] Preserve each port's timing quality. Merge host observations by timestamp and stable tie break, retaining uncertainty and per-channel order. Pause view and pause capture are separate controls; one must not accidentally modify the observed link.

**UI + CLI + MCP:** Monitor setup shows two inputs, endpoint names and a persistent Passive badge. Proposed `open_monitor`, `get_monitor`, `close_monitor`, `set_monitor_view`; shared endpoint/read APIs work by group/channel ID. Any `send_bytes` to a passive group returns `unsupported_capability`.

**Tests:** `[communication][D10]`: `Second open failure releases the first reservation`; `Same physical port cannot occupy both monitor inputs`; `Both directions retain exact NUL and high bytes`; `Passive monitor performs zero transport writes` runs receive, rules, scripts and send attempts against spies; `Channel close marks an incomplete group and preserves captured bytes`; `Equal timestamps preserve per channel order`; `Port release waits for worker termination`. Qt UI test asserts channel labels and passive badge survive project restore.

**Acceptance and rollback:** No software forwarding or hidden writes occur in passive mode, and channel ownership is transactional. Close the group and return to independent serial tabs on rollback; recordings retain both source identities.

## D11 — TCP client/server, UDP peer/listener and named pipes

**Integrated hard dependencies:** G01, G02, G03, D01, D02. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight Scripting offers TCP client, single-client server, UDP peer/listener and named-pipe client channels; its UDP listener can send back to the last sender. CILogg should expose typed equivalents. IPv6, explicit peer selection and local-pipe server support are CILogg extensions. References: [Communication settings](https://docklight.de/manual/projectsettingscommunication.html), [Named pipes](https://docklight.de/manual/named-pipe.html).

**Dependencies:** D01, D02. **Integration notes:** Pattern matching/framing are opt-in integrations after the endpoint transport is working. **Verified anchors:** DLB01 generalized session owner, DLB09 Commander, DLB14 existing syslog UDP source. A syslog parser is not a raw TCP/UDP transport and must remain intact.

**Files:** Add **proposed** `src/ui/include/networktransport.h`, `src/ui/src/networktransport.cpp`, `src/ui/include/namedpipetransport.h`, `src/ui/src/namedpipetransport.cpp`, `src/ui/include/endpointsettingsdialog.h`, `src/ui/src/endpointsettingsdialog.cpp`, `tests/unit/networktransport_test.cpp`, `tests/ui/namedpipetransport_test.cpp`. Use Qt Network and the existing worker/lifecycle structure.

**Interfaces and implementation:**

- [ ] Add typed specs `TcpClientSpec{host,port,connectDeadlineMs}`, `TcpServerSpec{bindAddress,port,maxClients=1}`, `UdpSpec{bindAddress,localPort,optional<Peer> destination,ReplyPolicy}`, `NamedPipeSpec{name,ReadWriteMode,PipeRole}`. Initially support one TCP accepted stream per endpoint; multi-client child streams require explicit endpoint IDs and bounded counts if subsequently enabled.
- [ ] Preserve TCP as an ordered byte stream: one read is not a packet or application message. Define connect, peer-accepted, half-close, disconnect and timeout lifecycle events. No queued auto-send may spill into a replacement connection generation.
- [ ] Preserve UDP datagram boundaries, source/destination addresses, zero-length datagrams and truncation/error status. Reply policies are `pinned_peer`, `selected_datagram`, or explicit `docklight_last_peer`; do not silently select the latest arbitrary sender. Maximum accepted datagram and socket-buffer behavior are exposed.
- [ ] Windows named-pipe client supports read-only/write-only/duplex and records byte versus message mode when the server exposes it. POSIX local-domain sockets/FIFOs or a named-pipe server are separately named CILogg extensions, not silently interchangeable pipe implementations. Unsupported pipe mode/platform returns a clear capability error.

**UI + CLI + MCP:** Endpoint form, server waiting/accepted-peer state, UDP peer table and pipe role/access fields. D01 `open_stream` accepts these typed specs; shared get/status/send/close commands retain parity. Proposed `select_udp_peer` and `get_stream_peers` operate on stable endpoint/peer IDs. All definitions and errors appear in discovery.

**Tests:** `[communication][D11]`: Qt loopback `TCP stream survives arbitrary split and coalesced reads`; `Server rejects a second peer in single client mode`; `Reconnect discards old queued sends`; `UDP preserves empty datagram and peer metadata`; `UDP last peer behavior requires its explicit policy`; `Oversize datagram reports truncation without presenting valid full data`; `Pipe client preserves byte payload and access mode`; `Peer exit cancels pending writes once`. Include IPv4/IPv6 where the runner supports them and native Windows pipe fixtures.

**Acceptance and rollback:** Serial, ADB and syslog regressions remain green; raw transports use existing sessions and response runtime. Disable new endpoint kinds on rollback; keep saved definitions/recordings readable and no background listener orphaned.

## D12 — Explicit bridges, forwarding monitors and additional channels

**Integrated hard dependencies:** G01, G02, G03, D01, D07, D08, D10, D11. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight Monitoring can forward data between COM/network channels; `PROXY` couples connection lifecycle, and scripting supports up to eight additional side channels. Its `DirectSend` can inject bytes without normal communication-window output. CILogg should log routed and injected bytes explicitly and use the same source/session registry for every channel. References: [Communication channel combinations](https://docklight.de/manual/projectsettingscommunication.html), [Side channels](https://docklight.de/manual/side-channels---using-multiple.html), [DirectSend](https://docklight.de/manual/dl_directsend.html).

**Dependencies:** D01, D07, D08, D10, D11. **Integration notes:** The later transport fault adapter attaches to these route hooks; bridge implementation has no backwards dependency. **Verified anchors:** DLB01 common send/observe path; DLB10–DLB13 global scripts and lab; no new side-channel runtime is needed.

**Files:** Extend **proposed** `communicationgroup.h/.cpp`; add **proposed** `src/ui/include/communicationbridge.h`, `src/ui/src/communicationbridge.cpp`, `tests/unit/communicationbridge_test.cpp`, `tests/ui/bridgeworkbench_test.cpp`; extend endpoint chooser and Python `TabRef`/Application wrappers.

**Interfaces and implementation:**

- [ ] Define `BridgeDefinition{leftEndpoint,rightEndpoint,direction,transformProfile,disconnectPolicy,failurePolicy,maxPendingBytes}` and `createBridge(...) -> CommResult<BridgeHandle>`, `pauseBridge`, `closeBridge`. Default route is bidirectional raw passthrough with transforms off and `failurePolicy=stop_bridge`; `continue_with_recording_gap` is an explicit alternative.
- [ ] Publish original ingress and forwarded egress as separate correlated observations `{routeId,causalEventId,writeReceiptId}` without counting the same physical data twice in a selected view. Preserve UDP datagrams and HID report boundaries rather than flattening them through a byte-stream bridge by accident.
- [ ] Detect cycles in the route graph and reject by default. Any explicitly enabled cyclic test route has a finite hop/byte budget and marks origin; observing a forwarded event must not recursively forward the same observation.
- [ ] Use bounded per-direction queues and backpressure/stop policy. Peer disconnect handling distinguishes independent channels from proxy-coupled closure. Capturing/pausing the display does not implicitly suspend forwarding; route pause has its own visible state. Scripts can use any registered extra endpoint through ordinary Application/TabRef IDs, within D01 endpoint limits.

**UI + CLI + MCP:** Bridge diagram with two endpoints, directions, active/paused labels, queued bytes and recording state; persistent Active forwarding badge. Proposed `create_bridge`, `get_bridge`, `pause_bridge`, `close_bridge`, `list_streams`. All side-channel writes call ordinary `send_bytes`; there is no unrecorded injection shortcut.

**Tests:** `[communication][D12]`: `Serial TCP bridge forwards once and correlates both observations`; `Bidirectional burst keeps per direction order`; `Paused display does not pause route`; `Route pause has explicit resume queue policy`; `Loopback cycle is rejected`; `Coupled disconnect closes its partner once`; `Sink failure cannot grow the opposite queue beyond its cap`; `UDP bridge preserves each datagram`; `Bridge cannot claim a passive-only hardware input is writable`. Stress interleavings with QSignalSpy and fake transports before real sockets.

**Acceptance and rollback:** No echo loop or silent message-boundary loss; forwarding status is visible in GUI and command status. Stop routes and release their endpoint leases on disable; keep passive D10 monitor and ordinary sessions available. Do not silently fall back from a failed bridge to a live unrecorded link.

## D13 — USB/Bluetooth HID reports and Bluetooth SPP

**Integrated hard dependencies:** G01, G02, G03, D01, D02, D03, D09, E01. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight Scripting can exchange HID input/output and get/set feature reports, select VID/PID/usage or device path, and access Bluetooth HID. It supports Bluetooth SPP via COM connections. HID access does not imply generic USB endpoint access or BLE GATT support; normal keyboard/mouse interfaces may not be available to an application. References: [HID testing](https://docklight.de/manual/testing-a-hid-device-usb-or-bl.html), [Connection support](https://docklight.de/), [HID report options](https://docklight.de/manual/projectsettingscommunication.html).

**Dependencies:** D01, D02, D03, D09, E01. **Integration notes:** E01 owns generic wire negotiation and SDK/schema changes; this card consumes its capability to implement the HID adapter. Existing serial provides SPP. **Verified anchors:** DLB14 process protocol v1/text limit and SDK; DLB01 transport-facing session; DLB07 serial path reused by SPP.

**Files:** Add **proposed** `src/ui/include/pluginbytetransport.h`, `src/ui/src/pluginbytetransport.cpp`, `plugins/examples/hid-device/plugin.json`, `plugins/examples/hid-device/CMakeLists.txt`, `plugins/examples/hid-device/src/main.cpp`, `tests/unit/hidtransport_test.cpp`, `tests/ui/hidselection_test.cpp`; consume the E01-owned existing `src/plugins/src/pluginprocess.cpp`, manifest/protocol validation and SDK negotiation changes. D13 edits HID-specific adapters/examples/documentation only; E01 remains the sole writer for generic plugin wire/schema changes. Use one optional native HID helper behind the existing process plugin host; do not create a second plugin daemon. Pin and package any hidapi/runtime dependency with the helper rather than requiring a developer's global Python installation.

**Interfaces and implementation:**

- [ ] Consume E01 PluginCapabilityRegistry and its negotiated binary/event RPC/message schema verbatim. Implement the D13-owned `HidTransportAdapter::open(const HidSelector&) -> CommResult<EndpointHandle>`, `write(const HidReportRequest&) -> CommResult<WriteReceipt>` and `close()` through that interface. E01 owns capability/version negotiation, Base64/managed-segment wire encoding, generation/sequence metadata and acknowledged credits; D13 supplies HID report-kind/ID metadata mapping. Apply existing process message/traffic limits and D01 queue limits; split transport chunks without losing report boundaries. Do not introduce a second protocol version, SDK or negotiation engine here.
- [ ] Define `HidSelector{vendorId,productId,optional<usagePage>,optional<usageId>,optional<serial>,optional<devicePath>}` and `HidReportRequest{kind=Output|SetFeature|GetFeature,reportId,payload}`. Preserve the report ID separately, exact length and trailing zeros; protocol-only padding removal is an explicitly named projection. Never embed magic control bytes into the user's real report payload to select an operation.
- [ ] Enumerate matching interfaces and require a stable explicit choice when multiple devices match. Report unavailable/busy/permission-denied interfaces honestly. Hotplug closes the old generation; an automatic reopen requires the same stable device identity and a saved policy.
- [ ] Expose SPP devices from ordinary OS serial enumeration after pairing. It uses existing serial settings/Actions/monitor paths; do not implement a new Bluetooth stack in this card. Mark arbitrary BLE GATT, USB bulk/control transfers and vendor-specific tap protocols as separate future adapters.

**UI + CLI + MCP:** HID device/interface selector, report-type and ID controls, byte preview and Input/Output/Feature badges. Proposed `list_hid_devices`, `send_hid_report`, `get_hid_feature`; D01 open/status/close remain shared. SPP appears in the existing serial chooser with transport metadata where available. Plugin status and errors use current plugin management UI/commands.

**Tests:** `[communication][D13]`: `Report ID and trailing zeros survive a D01 snapshot reader roundtrip`; `Get feature is not a zero length output write`; `Duplicate VID PID requires explicit selection`; `Device unplug invalidates queued writes`; `Old text plugin continues protocol v1 unchanged`; `Binary plugin respects message credit and cancellation`; `Plugin restart cannot publish old-generation reports`; `SPP follows the serial capability path`. HIL fixtures cover a vendor-defined USB HID interface and Bluetooth HID only when available; missing hardware is recorded as not certified, not passed.

**Acceptance and rollback:** Input/output/feature requests are typed and traces retain original reports; ADB/syslog packaging/tests still pass. Disable the optional HID package/capability to roll back without changing base plugin defaults or serial SPP support.

## D14 — Dynamic replies, script console and file-driven communication

**Integrated hard dependencies:** G01, G02, G03, D01, D03, D04, D05, D06, D07, D08, D11, D12, D13. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight scripts expose sending, waits, counters, logging, file I/O and application control; OnSend/OnReceive can compute dynamic replies. Callbacks are queued and may be deferred while script flow is blocked. CILogg already has Python scripts/events/test APIs, so provide an integrated console and richer byte wrappers around them. References: [DL object](https://docklight.de/manual/dlobject.html), [Dynamic answer FAQ](https://docklight.de/dl_faq022/), [Callback timing](https://docklight.de/manual/timing_and_program_flow.html), [Command-line startup](https://docklight.de/manual/docklightscriptingcommand-li.html).

**Dependencies:** D01, D03, D04, D05, D06, D07, D08, D11, D12, D13. **Verified anchors:** DLB10 existing supervisor; DLB11 byte events/timers/TabRef; DLB12 scenario APIs. Existing worker bootstrap path is `src/app/python_runtime/worker_bootstrap.py`; existing script window is `src/ui/src/scriptrunnerwindow.cpp`.

**Files:** Extend `scriptsupervisor.h/.cpp`, `scriptrunnerwindow.cpp`, `worker_bootstrap.py`, `klogg/client.py`, `klogg/__init__.py`; add **proposed** `src/ui/include/scriptconsoledock.h`, `src/ui/src/scriptconsoledock.cpp`, `tests/unit/scriptconsole_test.cpp`, `tests/ui/scriptconsolewindow_test.cpp`, `tests/python/communication_client_test.py`.

**Interfaces and implementation:**

- [ ] Add Python `TabRef.send_bytes(data: bytes, *, sensitive=False, deadline_ms=1000) -> WriteReceipt`, `wait_bytes(pattern: BytePattern, *, timeout_ms: int) -> ByteMatch`, `on_byte_match(handler, *, rule_id=None)`, `capabilities() -> dict`, `snapshot(policy) -> SnapshotHandle`. Wrap shared Commander operations and retain current callbacks, waits and `send_action` API.
- [ ] Add `ScriptSupervisor::submitConsoleCell(const QString& runId, const QString& source) -> CommResult<ConsoleCellHandle>` plus status/cancel. Cells execute in the existing per-run Python process and namespace, serialize execution, retain bounded stdout/stderr, and tag output by cell. Continuations and syntax errors are explicit. Stopping a blocked cell uses the existing process lifecycle; it must not leave transport writes/timers from the abandoned generation.
- [ ] Provide versioned dynamic-response examples using captured bytes, current counters, deterministic expressions or Python state. Document external process automation through Python subprocess calls with literal argv, explicit timeouts, exit-status assertions and attached stdout/stderr using the existing scenario artifact API. Keep fixed/declarative response effects fast; do not run arbitrary Python in the GUI/I/O thread or imply process plugins/scripts are an OS sandbox.
- [ ] Define `sendFile(endpointId,path,FileSendOptions{chunkBytes=65536,rateLimit,deadline}) -> TransferHandle` through the same queue/receipts. Raw file send preserves bytes and is distinct from a negotiated protocol such as XMODEM; no unsupported transfer protocol is advertised. File receive is a recording/export operation, not a text decoder.

**UI + CLI + MCP:** Console cells with output, stop/clear/history and selected endpoint; reuse Run Script and Scenario windows for saved programs. Proposed `console_execute`, `get_console_cell`, `cancel_console_cell`, `send_file`, `get_transfer`, `cancel_transfer`; existing `run_script`, `run_global_script`, status/subscription controls remain authoritative. Scripts and console can invoke the same commands on serial/network/HID endpoints subject to capability checks.

**Tests:** `[communication][D14]`: `Dynamic binary reply depends on captured payload` checks exact bytes including NUL; `Blocked script does not block capture or another endpoint`; `Console cells share state and serialize execution`; `Cancel cell clears old handlers and timers`; `Callback queue overflow is visible and bounded`; `Raw file send handles a final short chunk and partial write`; `Cancel file transfer reports exact sent prefix`; `Script exceptions preserve stderr and terminate the right run`. Existing Python SDK and supervisor tests remain green.

**Acceptance and rollback:** No duplicate script interpreter/controller, no UI blocking and no hidden byte transcoding. Disable console/byte wrappers while retaining file-based scripts and old APIs; cancel cells/transfers before rollback. Saved console history is optional and excludes sensitive payloads by default.

## D15 — Lossless recording, timing-aware replay and deliberate fault injection

**Integrated hard dependencies:** G01, G02, G03, D01, D07, D08, D12, A05, A08. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight logs communication representations and can replay timestamped logs, preserving original timing or using an override. Its scripting hooks can manipulate/inject data. A native event archive, deterministic replay controls and an explicit fault-plan editor are CILogg extensions. References: [Logging](https://docklight.de/manual/loggingandanalyzingatest.html), [PlaybackLogFile](https://docklight.de/manual/dl_playbacklogfile.html), [Stream manipulation](https://docklight.de/manual/processingincomingdatausing.html), [DirectSend](https://docklight.de/manual/dl_directsend.html).

**Dependencies:** D01, D07, D08, D12, A05, A08. **Integration notes:** A05 owns recording manifest, generic ReplayEngine and virtual clock; A08 owns the execution journal. This card owns transport routes/fault policies and consumes early RecordingSnapshot/ClockStamp. **Verified anchors:** DLB07 existing presentation capture; DLB10–DLB12 script/scenario lifecycle; DLB16 retained source identity principles.

**Files:** Add **proposed** `src/ui/include/communicationrecording.h`, `src/ui/src/communicationrecording.cpp`, `src/ui/include/replaytransport.h`, `src/ui/src/replaytransport.cpp`, `src/ui/include/communicationfaultplan.h`, `src/ui/src/communicationfaultplan.cpp`, `tests/unit/communicationrecording_test.cpp`, `tests/unit/replaytransport_test.cpp`, `tests/ui/replayworkbench_test.cpp`. The recorder is a transport adapter over D01 RecordingSnapshot plus the A05 recording manifest and A08 ExecutionJournal. It creates no second permanent event database, generic replay clock or execution journal.

**Interfaces and implementation:**

- [ ] Define `exportRecording(const RecordingSnapshot&, const QString& path, const RecordingOptions&) -> JobHandle`, `openRecording(path) -> CommResult<RecordingSource>`. Extend A05 `recording-manifest-v1` with transport channel/report metadata, D01 retained-byte segment references/content hashes, source/generation/watermark, ClockStamp quality, framing profiles, redaction and gaps; A05 owns the manifest serializer/version, D01 owns validated byte reads, and A08 stores execution/fault/write outcome annotations. Do not introduce a competing native archive/index format. Validate offsets/hash/size before exposing payload; retained archive source leases remain readable after tab closure.
- [ ] Add derived UTF-8 text, escaped HTML, CSV/JSONL and per-channel raw-binary exports from the same frozen recording. Text/HTML export can select ASCII/HEX/DEC/BIN, channel labels, timestamps, comments and gap/redaction markers; exported presentation never becomes the authoritative replay source. HTML escapes all captured content. Version logging rotation/size-limit options and include an explicit final incomplete marker when a limit stops capture.
- [ ] Define `ReplayPlan{mode=ViewOnly|SimulatedRx|TransmitRoutes,speed,routeBindings,channelFilter,range,loopCount,latePolicy}`. Default ViewOnly never writes or activates response actions. SimulatedRx and TransmitRoutes require explicit command/UI choices; synthetic events retain original provenance plus replay/run IDs. Implement `TransportReplayAdapter::bind(ReplayEngine&, const ReplayPlan&) -> CommResult<ReplayHandle>`; delegate timing, speed, pause/step/seek/stop and deterministic seed/run state to A05 ReplayEngine. D08 executes actual routed transmit plans only; D15 owns physical destination policies and transport fault hooks, not another virtual clock or replay scheduler. Seeking rebuilds transport framing/fault state from A05 checkpoints.
- [ ] Import only documented timestamped Docklight ASCII/HEX/DEC/BIN text/HTML logs that pass a strict dialect parser. Preserve uncertainty where original logs omitted timestamps/directions or escaped information; never label lossy text as a complete binary recording. D16 handles project files separately.
- [ ] Define deterministic `FaultPlan{seed,selectors,rules}` with rules drop/duplicate/delay/reorder chunks or messages, replace/flip payload bits, truncate, corrupt checksum, insert noise, disconnect. Show pre-fault and transmitted bytes with causal IDs, append decisions/attempts/receipts through A08 ExecutionJournal::append, and seed mutations from A05 run state. Byte corruption is a software fault; physical parity/framing/break injection is available only through a supporting D09 backend and is labeled separately.

**UI + CLI + MCP:** Record/export controls, gap/redaction markers, timeline scrub, speed/step and explicit replay destination review. Proposed `export_comm_recording`, `open_comm_recording`, `set_replay_routes`, `set_fault_plan`, `preview_fault_plan`; existing planned A05 `replay_open`, `replay_control`, `replay_status` own the replay lifecycle. UI buttons and transport adapters call those operations rather than register parallel play/pause/step/cancel commands. Dry-run previews give exact modified bytes and scheduling; they perform no transport writes.

**Tests:** `[communication][D15]`: `Archive roundtrip preserves bytes reports channels and gaps`; `Corrupt index or payload hash is rejected`; `Recording tail survives interrupted finalization with incomplete marker`; `View only replay has zero writes and zero actions`; `Replay preserves relative order with deterministic fake clock`; `Step emits exactly one selected event`; `Same fault seed yields identical mutation transcript`; `Sensitive bytes never reappear in archive or mutation preview`; `Formatted HTML escapes payload and raw export retains byte values`; `Rotation limit terminates with an explicit marker`; `Cancel replay cannot revive on reconnect`; `Docklight import flags missing timing as unknown`. Test source/destination alias rejection and atomic export failure recovery.

**Acceptance and rollback:** Native recordings are byte-exact subject to explicit redaction/gap markers; replay is reproducible and routing is visible. Stop replay/fault plans and retain immutable archives on disable. Ordinary text capture remains readable and available; never rewrite originals during import/export.

## D16 — Native project schema, Docklight import/export and reproducible workspace setup

**Integrated hard dependencies:** G01, G02, G03, D03, D04, D05, D06, D07, D08, D09, D10, D11, D12, D13, D14, D15. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight stores named sequences and communication settings in `.ptp` projects; scripts use `.pts`, and its CLI can load/run them. CILogg already saves projects/configuration. Add a constrained, transparent interchange adapter, not a speculative full-format promise. References: [OpenProject](https://docklight.de/manual/dl_openproject.html), [Command-line syntax](https://docklight.de/manual/docklightscriptingcommand-li.html), [Project-based Modbus example](https://docklight.de/manual/sendingmodbuscommandswithc.html).

**Dependencies:** D03, D04, D05, D06, D07, D08, D09, D10, D11, D12, D13, D14, D15. **Verified anchors:** DLB15 project save/load/config exports and action versions; DLB03 action/response definitions; DLB10 saved script bindings. Existing `ActionsConfigParser` parses CILogg JSON, not verified Docklight `.ptp` support.

**Files:** Extend existing `configurationexport.h/.cpp`, `mainwindowproject.cpp`, actions/schema serialization and project tests; add **proposed** `src/ui/include/docklightprojectadapter.h`, `src/ui/src/docklightprojectadapter.cpp`, `src/ui/include/projectimportpreviewdialog.h`, `src/ui/src/projectimportpreviewdialog.cpp`, `tests/unit/docklightprojectadapter_test.cpp`, `test_data/communication/docklight/`, `docs/communication-projects.md`.

**Interfaces and implementation:**

- [ ] Add a versioned communications section to native projects containing endpoints, channel labels, sequence/rule IDs, framing/checksum profiles, schedule definitions, bridge/monitor definitions, script references and recorded artifacts. Save immutable definitions, not active worker handles, pending writes, credential values or ephemeral jobs. Migrate old projects transactionally and preserve a recoverable original.
- [ ] Define `previewDocklightImport(path, ImportOptions) -> CommResult<ProjectImportReport>` and `applyProjectImport(reportId, expectedHash)`. Report each imported/transformed/unsupported item with source location and target ID. Merge/replace choice is explicit; remap IDs and every linked reference atomically.
- [ ] Build the `.ptp` parser from a fixture corpus of official public samples and user-supplied projects during implementation. There is no verified complete public machine-readable schema in this research. First supported subset: serial/network endpoint settings, named literal sequences, explicit wildcard parameter regions, byte receive patterns, supported checksum definitions, comments/stop/snapshot effects and repeat settings. Unsupported special functions remain disabled with diagnostics; no silent text conversion.
- [ ] `.pts` VBScript is retained as source/reference with a migration report and equivalent Python API links; it is not executed or automatically translated as if equivalent. Export a Docklight-compatible subset only when every item is representable, otherwise return a detailed rejection report or an explicitly selected reduced export. Imported projects start disconnected with automatic responses disabled until an explicit run/connect action.

**UI + CLI + MCP:** Project import preview lists supported/changed/unsupported fields and exact byte previews; native export includes portable relative references where allowed. Proposed `preview_project_import`, `apply_project_import`, `export_communication_project`, `validate_communication_project`; existing project Save/Open stay the native entry points. CLI dry run and MCP return the same report digest and item IDs shown in UI.

**Tests:** `[communication][D16]`: `Native project roundtrip preserves all communication definitions`; `Old project versions keep previous settings and actions`; `Import remaps linked response and route references atomically`; `Unknown function or checksum stays disabled and visible`; `Changed file invalidates an earlier preview hash`; `Import parse failure leaves live workspace unchanged`; `VBScript source never executes during import`; `Subset export rejects an unrepresentable feature`; `Relative path traversal cannot escape the declared artifact root`. Golden official-sample fixtures assert exact bytes. A Windows compatibility run must reopen exported subset fixtures in Docklight before claiming interchange compatibility; self-roundtrip alone is insufficient.

**Acceptance and rollback:** Document a tested support matrix by source version and feature. Unverified `.ptp` variants are supported through an actionable import report, not guessed. Keep old project backup and explicit schema versions; rollback can open old projects without overwriting new-schema files.

## D17 — Protocol Lab, shared byte analysis jobs, scenario and automation parity

**Integrated hard dependencies:** G01, G02, G03, D01, D02, D03, D04, D05, D06, D07, D08, D09, D10, D11, D12, D13, D14, D15, D16, F01, F07. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight scripting combines communication primitives into automated test jobs and command-line runs. CILogg already has the runner, lab service and native MCP; the missing piece is shared byte-analysis input, protocol-focused inspection and full parity. References: [Scripting overview](https://docklight.de/manual/docklightscripting-overview.html), [DL methods](https://docklight.de/manual/dlobject.html), [CLI](https://docklight.de/manual/docklightscriptingcommand-li.html).

**Dependencies:** D01, D02, D03, D04, D05, D06, D07, D08, D09, D10, D11, D12, D13, D14, D15, D16, F01, F07. **Integration notes:** Use F07 AnalysisDatasetService and the existing AnalysisJobService lifecycle; reconcile P05 before shared-source edits. **Verified anchors:** DLB09–DLB13, DLB16–DLB18. `AnalysisJobService` currently processes decoded line batches: add a typed adapter, not binary-through-Latin-1 reconstruction and not a new job system.

**Files:** Extend existing `src/logdata/include/analysisjob.h`, `src/logdata/src/analysisjob.cpp`, Commander schemas, CLI parsing, native MCP discovery, scenario/lab typed bindings and Python client; add **proposed** `src/ui/include/protocollabdock.h`, `src/ui/src/protocollabdock.cpp`, `src/ui/include/communicationanalysis.h`, `src/ui/src/communicationanalysis.cpp`, `tests/ui/protocollabdock_test.cpp`, `tests/ui/communicationanalysis_test.cpp`, `tests/mcp/communication_parity.py`, `test_data/communication/scenarios/`.

**Interfaces and implementation:**

- [ ] Define internal `AnalysisByteRequest{RecordingSnapshot snapshot; ByteSpanSelection scope; quint64 requestRevision; AnalysisLimits limits;}` and `AnalysisByteBatch{QVector<EventRecord> events; QVector<ByteChunk> chunks; int jobRemainingMs;}`. Add `CommunicationAnalysisDatasetAdapter::freeze(const RecordingSnapshot&) -> DataResult<SourceSetHandle>` and `submit(AnalysisDatasetService&, const ProtocolAnalysisRequest&, QString* error) -> QString`. Adapt the D01 reader lease, ManagedByteOrigin manifest and optional physical SourceSnapshot into F07 source-set/AnalysisPipelineFactory hooks; submit through AnalysisDatasetService::submit(QuerySpec, DatasetRequest), then describe/page via that same service. Extend AnalysisJobService internals only where typed byte batches are needed, using its existing job registry/admission/cancel/deadline/result retention, not an independently scheduled byte-job overload. Keep line-based start adapters and Regex Lab/Value Counts untouched.
- [ ] `startProtocolAnalysis(ProtocolAnalysisRequest{input,patterns,frameSpec,checksumSpec,metrics}) -> AnalysisJobId` supports byte pattern explanations, frame/checksum diagnostics, direction counts, byte histograms and request/response timing where clocks permit. Each result carries byte provenance, profile revision, complete/partial reason and source freshness; no unbounded body embedded in JSON pages.
- [ ] Protocol Lab draft edits invalidate apply/navigation for old results, as current Regex Lab/value-count workflows do. Live capture analysis starts from an explicit finite D01 RecordingSnapshot containing canonical managed-byte identity, optional physical SourceSnapshot and a retained-byte reader lease; newer events show “new data available.” Offline analysis never sends or triggers live response actions.
- [ ] Extend existing Python scenario wrappers to bind endpoints by logical device/capability tags and to assert bytes/matches/checksums/write receipts. Extend the existing lab inventory/binding schema from COM settings to typed endpoint descriptors without replacing LabControllerService, authentication, artifact/job APIs or ScenarioRunner. A physical endpoint lease belongs to one running job unless explicitly passive/shareable.
- [ ] Add common Commander operation validation/discovery for every D-card command. Export JSON/JUnit-style scenario reports through the existing report/artifact path, attaching recordings, profiles, fault seeds, measured timing quality and run IDs. Do not build another test result database.

**UI + CLI + MCP:** Protocol Lab with inline bytes or recording/current-stream snapshot, explicit sample/selection/whole-snapshot scope, shared cancel/status/page controls and byte navigation. Proposed `protocol_evaluate`, `show_protocol_lab`, `get_protocol_lab`, `set_protocol_lab`; reuse existing `get_analysis_job/results`, cancel/release operations. Scenario `run_scenario/run_suite`, reports and lab CLI stay the public automation surfaces. Native MCP uses existing `cilogg_execute`/`cilogg_cli`/`cilogg_jobs`.

**Tests:** `[communication][D17]`: `Byte analysis pages preserve raw provenance and decimal offsets`; `Snapshot append does not extend an in flight scan`; `Rewrite or source close disables navigation without corrupting retained output`; `Analysis cancel under stalled credit releases all pins`; `Inline bytes and file recording yield identical pattern and CRC rows`; `Analysis never emits a live send`; `Scenario response wait handles binary NUL`; `Two lab jobs cannot lease the same exclusive endpoint`; `Every advertised communication operation has CLI MCP GUI parity`. The packaged Python parity test enumerates required commands, runs the same requests through all surfaces and compares normalized data/errors; execute against accelerated and Qt-only builds where applicable.

**Acceptance and rollback:** One bounded analysis service and one test/lab runtime serve all new views/surfaces. Existing analysis limits/tests and P05 parity stay green. Disable Protocol Lab/byte-analysis entry points before reverting its typed adapter; preserve existing text-analysis and scenario commands.

## D18 — Timestamp provenance, adapter certification and release conformance

**Integrated hard dependencies:** G01, G02, G03, D01, D08, D09, D10, D11, D12, D13, D14, D15, D17, F06. Capability-conditional dependencies described below are additional runtime gates; they do not reverse these edges.


**Purpose and Docklight evidence:** Docklight host timestamp accuracy depends on hardware, drivers, scheduling and buffering. Tap Pro/485 add a hardware time base; official information distinguishes internal 1 µs accuracy from a visible 1 ms timestamp resolution, and an application note says the full internal microsecond resolution is not exposed. CILogg must distinguish resolution, accuracy and observation location. References: [Timestamp FAQ](https://docklight.de/dl_faq011/), [Best timing accuracy](https://docklight.de/manual/how_to_obtain_best_timing_accu.html), [Hardware timing FAQ](https://docklight.de/dl_faq041/), [Tap Pro/485 note](https://docklight.de/pdf/Docklight_Application_Note_TapPro_Tap485.pdf), [Product information](https://docklight.de/information/).

**Dependencies:** D01, D08, D09, D10, D11, D12, D13, D14, D15, D17, F06. **Integration notes:** Final calibration/conformance evidence only. Early runtime cards never require this task; F06 supplies optional clock alignment rather than a new clock engine. **Verified anchors:** DLB07 timestamps inserted during receive/log writing rather than hardware byte measurement, DLB13 remote agents, DLB16 source freshness. Support for proprietary TAP/VTP hardware protocols is not established by current CILogg source or this research.

**Files:** Add **proposed** `src/ui/include/communicationclockcertification.h`, `src/ui/src/communicationclockcertification.cpp`, `tests/unit/communicationclock_test.cpp`, `tests/hil/communication_conformance.py`, `docs/communication-capabilities.md`, `docs/communication-benchmarks.md`; consume the D01 EventRecord timestamp adapter and schema, add certification evidence to lab inventory/capability inspector, and use F06 clock anchors/alignment for optional calibration integration. Basic I/O timestamp capture and ClockStamp declaration already landed in D01. Native/hardware adapters remain isolated behind D01.

**Interfaces and implementation:**

- [ ] Validate the already implemented D01 ClockStamp schema and observation points against measured hardware/software evidence; do not introduce or delay the basic timestamp contract here. Nanosecond storage precision does not imply nanosecond measurement accuracy. Unknown bounds remain null. Keep source ticks/domain and calibrated mapping when a hardware backend supplies them.
- [ ] Verify D01 captures host time at the I/O observation point, before GUI delivery; certify optional first/last byte times only if the backend supplies them. Per-byte interpolation from baud is explicitly estimated, not raw evidence. Wall-clock adjustments cannot reorder a monotonic session. Separate process/machine clock domains require calibration before numeric latency comparison; otherwise show incomparable/uncertain timing.
- [ ] Certify the D01 `get_stream_timing`/`get_adapter_capabilities` metadata, delegate any calibration/alignment to F06 ClockAnchor/clock-alignment services, and add `run_adapter_conformance`/`get_adapter_conformance` with a conformance result `{backend,os,driver,deviceId,firmware,settings,testId,measuredResults,limitations}`. A nominal baud setting or generic USB-serial label never upgrades capability badges automatically. Dedicated tap support is a separate adapter investigation: verify a usable SDK/protocol, event semantics and hardware availability before committing support.
- [ ] Run a deterministic software conformance corpus for all endpoint kinds and measured HIL for available UART, representative USB-serial/CDC, RS485 direction control, paired SPP, HID and dedicated taps when supported. Record unsupported/not-run separately from pass. Log drops, signal transition visibility, break/error limitations, loopback byte accuracy, requested/applied baud, write completion stage and jitter distributions.
- [ ] Proposed initial performance gate for the workbench, to reconcile with parent P00 budgets: fake transport plus SSD recording at 1 MiB/s for 60 seconds, two directions, two byte views and 128 bounded short patterns; zero unexplained loss, queues within D01 limits, post-warmup additional resident memory <=64 MiB excluding the parent's measured store/index cache, and p95 user-input response <=100 ms. Record exact hardware/build/options. Basic serial HIL checks zero corruption at 115200 and at the highest tested supported baud; it does not assert every adapter supports 921600 or realtime 10 ms scheduling.

**UI + CLI + MCP:** Timing tooltip/inspector shows origin, nominal resolution, measured/unknown accuracy and clock domain. Latency views omit unjustified decimal places and mark estimates; scripts/CLI/MCP receive the same metadata and no fabricated precision. Advanced timing-dependent framing/actions require suitable capability or an explicit heuristic mode.

**Tests:** `[communication][D18]`: `Wall clock jump never reverses session order`; `Different uncalibrated clocks cannot produce a numeric latency`; `Nanosecond storage keeps unknown accuracy unknown`; `Equal timestamps retain deterministic per channel sequence`; `Hardware ticks wrap through a documented mapping`; `Uncertain break event cannot claim byte exact ordering`; `Low quality timestamps reject precise idle framing`; `Capability record is invalidated by backend driver or firmware change`; `HIL skip cannot satisfy a certification gate`. Run cross-platform packaged D17 parity, existing serial/analysis/scenario/plugin tests, native MCP smoke and full CTest before release.

**Acceptance and rollback:** Publish a measured capability matrix, not an unsupported “Docklight hardware compatible” or precision claim. If a backend fails conformance, disable only its unsupported advanced features and keep ordinary capture/analysis usable. Preserve original raw timestamps/domains so recalibration or rollback never destroys evidence. Hardware-specific features remain visibly uncertified until the corresponding HIL result exists.

## Hard dependency DAG

This table is authoritative for hard edges; integration notes about later optional capabilities do not add edges. Parent G01–G03 execution/verification gates apply to public tasks. F01 is independent of D; A05 and E01 may consume D01, while D01 never depends on them. A05 must never acquire a hard dependency on D15.

| Card | Hard prerequisites |
|---|---|
| D01 | F01 |
| D02 | D01 |
| D03 | D01, D02 |
| D04 | D01, D03 |
| D05 | D01, D04 |
| D06 | D03, D04 |
| D07 | D01, D04, D06 |
| D08 | D01, D03, D05 |
| D09 | D01, D08 |
| D10 | D01, D02, D09 |
| D11 | D01, D02 |
| D12 | D01, D07, D08, D10, D11 |
| D13 | D01, D02, D03, D09, E01 |
| D14 | D01, D03, D04, D05, D06, D07, D08, D11, D12, D13 |
| D15 | D01, D07, D08, D12, A05, A08 |
| D16 | D03, D04, D05, D06, D07, D08, D09, D10, D11, D12, D13, D14, D15 |
| D17 | D01, D02, D03, D04, D05, D06, D07, D08, D09, D10, D11, D12, D13, D14, D15, D16, F01, F07 |
| D18 | D01, D08, D09, D10, D11, D12, D13, D14, D15, D17, F06 |

The D-only graph is acyclic. One valid order is D01, D02, D03, D04, D05, D06, D07, D08, D09, D10, D11, D12, D13, D14, D15, D16, D17, D18, provided the listed parent prerequisites have landed. In particular, D15 consumes early timing from D01 and does not depend on D18; D18 consumes D15/D17 only as final conformance subjects.

## Dependency and delivery map

| Delivery slice | Cards | Reviewable outcome |
|---|---|---|
| Byte foundation and manual workbench | D01–D03 | Binary-safe capture/send receipts, four byte views and parameterized existing Actions |
| Protocol detection and deterministic actions | D04–D08 | Incremental patterns, effects/snapshots, shared CRC, framing and bounded scheduling |
| Communication coverage | D09–D13 | Honest serial capabilities, passive monitor, network/pipes, explicit bridges, HID/SPP |
| Automation and portability | D14–D17 | Existing Python console/scenarios, recording/replay/faults, projects and shared Protocol Lab |
| Hardware/release evidence | D18 | Timing provenance, measured adapter matrix and packaged regression/performance evidence |

Do not ship an unfinished endpoint kind because its setup form exists. Each slice can expose only the completed/certified capabilities. D01 implements ClockStamp, host observation timing, RecordingSnapshot and its bounded retained-byte reader before any consumer; D18 only certifies them later. A05 consumes D01 byte-source descriptors and owns generic recording/replay/virtual time; A08 owns execution annotations. D15 consumes those services and adds transport routing/fault policies. E01 consumes D01 and owns negotiated binary plugin wire/schema; D13 is the HID adapter consumer. D17 maps RecordingSnapshot into F07 AnalysisDatasetService and keeps the existing AnalysisJobService lifecycle. No early runtime feature depends on D18.

## Research coverage and unresolved implementation inputs

| Required capability | Owning cards | Evidence boundary |
|---|---|---|
| Binary-safe RX/TX and ASCII/HEX/DEC/BIN/control characters | D01–D03 | Docklight behavior verified; CILogg receipt/privacy/resource contracts proposed |
| Named parameters, byte wildcard/optional/mask across chunks | D03–D04 | Docklight byte tokens verified; CILogg incremental algorithm and chunk invariance specified |
| Answers, comments, counters, logging, snapshots, stop | D05 | Existing CILogg features extended; exact effect ordering proposed |
| Checksum/CRC and framing/stuffing | D06–D07 | Docklight checksum behavior and stuffing examples verified; general declarative codecs proposed |
| Periodic/inter-byte timing and deadlines | D08, D18 | Docklight best-effort timing verified; bounded queue/deadline policies proposed |
| Serial signals, break/errors and adapter limits | D09, D18 | Docklight and Qt/native limits referenced; advanced CILogg backends need implementation/HIL |
| Two-port passive monitoring and bridging | D10, D12 | Docklight modes/forwarding verified; explicit CILogg passive/active distinction proposed |
| TCP client/server, UDP, named pipes | D11 | Docklight modes verified; platform extensions labeled separately |
| HID input/output/features, Bluetooth HID/SPP | D13 | Docklight capability verified; CILogg optional plugin transport/HID packaging proposed |
| Dynamic replies, script console and file sends | D14 | Docklight scripting verified; CILogg existing Python runtime extended |
| Recording/replay/error injection | D15 | Docklight playback/manipulation verified; native archive/fault editor proposed |
| External automation and scenario/lab integration | D14, D17 | Existing CILogg APIs reused; byte adapters and parity fixtures proposed |
| Project import/export | D16 | File types verified; complete undocumented `.ptp` grammar and compatibility remain open |
| Timestamp precision and hardware-specific support | D18 | Resolution versus accuracy verified; CILogg adapter certification explicitly pending |

Before implementing D09/D13/D16/D18, obtain the concrete hardware/device report descriptors and representative `.ptp`/log fixture corpus needed by those cards. This is an execution prerequisite, not a reason to stop delivering this plan. Do not infer generic USB/BLE support, proprietary tap support, full VBScript compatibility or guaranteed sub-millisecond ordering from the official Docklight feature list. No hardware throughput or precision measurement has been performed for this research document.

## Official source index

The required [Docklight information page](https://docklight.de/information/) and [Docklight Scripting manual PDF](https://docklight.de/pdf/docklight_scripting_manual.pdf) were requested directly; the browsing service timed out opening them. Official indexed search results returned the information page, manual PDF identity/content summary and the individual live manual chapters used above. The indexed PDF is labeled V2.4 01/2025; the live manual pages are labeled 04/2025 with 2026 site copyright. Feature statements here use the specific cited live chapters rather than assuming the two publications are identical. A future executor should download/archive the exact manual revision with the interoperability fixtures.

Additional official references used in the cards are linked next to the corresponding claims. Platform references are primary Qt and Microsoft documentation. No third-party feature comparison was used to establish Docklight behavior. All algorithms, limits, file paths and CILogg UI/API additions described as proposed are implementation decisions for this plan, not claims that Docklight or current CILogg already implements them.
