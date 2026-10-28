# Message Decoder Implementation Plan

> **For agentic workers:** Use the existing execution guide and its implementation/review gates; this document integrates planned work only. MD01–MD08 are additional task IDs in the same TASK_INDEX.json and PROGRESS.json.

**Goal:** Decode ordinary, multiline and live developer logs with a Wireshark-style inspector and shared typed analytics/automation.

**Architecture:** Supervised real TShark/native/Lua dissectors are the primary protocol engine. CILogg owns extraction, bounded flow routing, provenance, presentation and automation; it does not emulate Proto/Tvb/TreeItem or duplicate EHCP/EHCI semantic parsers. Preserve JSON previews, plugin v1 and the existing analysis lifecycle.

**Spec:** This integrated contract, [the product specification](00-product-spec.md), and the supplied revision-2 addendum recorded by hash in PROGRESS.json. The source Markdown's handoff prompt is reference material; this change starts no decoder implementation.

## Global constraints and reconciliation

**Execution priority updated 2026-10-28:** The user authorized decoder core
MD01–MD06 immediately after W0, bringing forward its F01–F04/D01/E01 prerequisites,
then resuming the remaining W1 and later waves. MD07 inspector/analytics and MD08
distribution/release integration are explicitly deferred until their dependencies
land in later waves. See [the execution guide](MASTER_GUIDE.md) and
`PROGRESS.json.executionPolicy`. The addendum's embedded handoff remains reference
material; this later user instruction supplies the execution priority. No task
or runtime capability is completed by this policy update.

- Preserve C++17, Qt5/Qt6 core support, existing deployment/settings identities, plugin protocol v1 and all original hard dependency edges. G02/G03 remain prerequisites; early V01/V02 charts gain no decoder dependency.
- Reuse AnalysisJobService for finite source scopes, snapshots, leases, jobs, status, retention, bounded pages, cancellation and release. Live processing uses bounded incremental derived state without rewriting captures.
- Existing analysis admission/read/page/deadline caps remain binding. Plugin-v1 envelopes remain 1 MiB with 8 MiB pending stream traffic; retained segments/pages carry larger byte streams. A reviewed schema/measurement change is required to alter any cap.
- Add explicit tested limits for frames, flows, pending bytes, expansion, nesting depth/cycles, fields, output and session windows. Their numeric profiles are frozen with G02/MD08 evidence before release; an unmeasured allocation bound is not a performance pass.
- Owners describe responsibility, not reverse hard edges. MD01 is a proof using existing tools; it does not wait for E09's complete Git Team Folder feature. D07 live transports, D17 Protocol Lab and R01 decoder-enabled release paths are conditional integration gates, not prerequisites for historical text inspection or early charts.
- Preserve all previous completed work. MD tasks are planned; importing this document does not pass runtime, package, performance or hardware checks. G02a PR59 is merged; its evidence is preserved, while full G02 remains in progress.

## Shared pipeline and result contract

Envelope extraction -> explicit literal/hex/base64/escape codec -> bounded flow/fragment assembly -> real-engine decoding -> one typed result model -> inspector, queries, counts, charts, exports and assertions.

The proposed model comprises ExtractedFragment, DecoderFeedResult, DecodedMessage, DecodedField, ConversationResult and EmbeddedPayload. Reconcile exact declarations with F01/F02/F04/F07/E01 before coding. Preserve stable IDs/revisions, exact integer types across JSON, named/repeated field occurrences, summary/display text and units, supporting evidence, independent completeness/integrity/assembly/fidelity states, and transformation lineage.

Use `dd/MM/yyyy HH:mm:ss.zzz [RX|TX] - payload` only when that envelope profile is selected; locale/date format and representation are configurable. Unknown timezone stays unknown; device timestamps stay separate, and EHCP UTC offset 999 is unknown. Do not infer an offset from examples. Preserve literal backslashes and payload whitespace; never silently strip escapes, append line endings, repair hex nibbles or treat presentation separators as bytes.

For text-derived application PDUs, use Exported PDU with a named dissector and a bounded pcapng/sidecar bridge. Sidecars map generated record and logical buffer ranges to immutable source spans, envelope metadata, transformation lineage and completeness. Unknown network addresses, TCP sequence numbers and wire timings stay unknown. TShark reads genuine PCAP/PCAPNG directly and retains real transport/reassembly; application-PDU encapsulation does not recreate TCP reassembly for text logs.

Import PDML field trees/field metadata and useful JSON/field output while checking actual engine output for repeated/generated fields and transformed data sources. PDML positions are not automatically original-log byte offsets. Keep logical bytes, encoded file bytes and UTF-16 character offsets labelled separately. A generated field may have evidence and no occupied byte range. Unknown mappings are explicit; chart values come from typed fields, never display-label parsing.

Supervise a worker per bounded session/window rather than per physical record. Pin/discover engine version, Lua support, dissector names, script dependencies/hashes, preferences and profile revision; include them with source generation/scope in cache identity. Expose partial conversation state at a window boundary. Imported scripts never auto-run because a log matches a name; process separation is not an OS permission sandbox.

AT assembly keeps per-direction input separate while correlating a direction-independent device/channel conversation. Track echo, ATE/ATV modes, request/result/URC schemas, prompts, SMS/PDU states and data mode. RX-only input remains unpaired; competing requests remain ambiguous. An observed duration needs comparable clocks/coverage and is labelled log time; timeout requires an explicit rule. Historical decoding sends no modem commands or live actions.

SRING is a selectable dialect, not a universal layout. Validate parameter meanings and wrapper/hex lengths, then invoke the registered child dissector inside the real Wireshark Lua environment. Retain parent/child IDs and exact hex-pair mappings. Child streams can span notifications or emit several messages; unresolved connection identity stays ambiguous. Parent/child query scopes prevent double counting. This composition also applies to explicit JSON/text/hex/base64 envelopes, with expansion/depth/cycle bounds.

## Public surfaces and proposed commands

Proposed Commander operations: `list_message_decoders`, `validate_message_decoder`, `decode_messages`, `inspect_message`, `get_message_context`, `get_conversation`. A finite decode returns an existing analysis job ID and uses the shared status/results/cancel/release operations. All advertised capabilities require equivalent GUI, CLI, MCP and Python data/errors plus scenario assertions. Names are implementation targets, not currently available commands.

Extend the current preview with menu/toolbar/context discovery, stable accessible/object names, lazy summary/tree/raw/hex/text views, included/excluded records and warnings. Any contributing fragment can open the reconstructed message. Validate source locations before navigation; retain visibly historical output after rewrite/closure. Correlation changes, profile/script edits and appends follow existing revision/generation rules.

## Review focus

1. Missing or non-dispatchable Lua scripts/complete captures: MD01 must prove actual complete/partial Exported-PDU calls before compatibility claims.
2. Unknown or lossy byte origins: MD02/MD04 retain explicit fidelity and never invent bytes or transformed-buffer mappings.
3. Interleaved devices/directions and ambiguous AT response/URCs: MD02/MD06 preserve flow isolation and correlation uncertainty.
4. Oversized or stalled worker/consumer and malicious scripts: MD03/MD08 enforce host caps, deadlines and process cleanup without claiming a sandbox.
5. Nested SRING containers and child messages: MD04/MD06/MD07 preserve independent statuses and prevent doubled counts.

## Acceptance matrix

The 14 DEC requirements preserve the source addendum's numbered acceptance cases.

| Requirement | Required outcome | Owning slices |
|---|---|---|
| DEC01 | Eleven EHCP reference frame/checksum outcomes and exhaustive split provenance | MD01, MD02, MD05 |
| DEC02 | Multiple/split messages with source/device/direction isolation | MD02, MD05 |
| DEC03 | Incomplete EHCI split CNT, interleaved diagnostics, length mismatch and missing CRC | MD05 |
| DEC04 | EHCI tag-length framing, repeated maps, empty/NUL data and honest recovery/fidelity | MD02, MD05 |
| DEC05 | Explicit payload codecs, Unicode/file-byte/logical offsets and unknown bytes | MD02, MD04 |
| DEC06 | AT CSQ transaction/URC with 40 ms log duration, -67 dBm and unknown BER | MD06 |
| DEC07 | Contextual AT grammar, echo/result modes, schemas and ambiguous correlation | MD06 |
| DEC08 | AT SMS/prompts/data mode and truncated observations without false transactions | MD06 |
| DEC09 | Supervised bounded decoder state/output, crash/deadline/cancellation/backpressure | MD03, MD08 |
| DEC10 | Legacy JSON/v1 plugins and GUI/CLI/MCP/Python typed-data/error parity | MD03, MD07 |
| DEC11 | Nested SRING hex-to-EHCP with independent wrapper/child diagnostics and exact maps | MD01, MD04, MD06 |
| DEC12 | Cross-notification child assembly, parent/child query scopes and nesting/cycle limits | MD02, MD06, MD07 |
| DEC13 | Actual supplied Lua execution, engine/script provenance and reference comparison | MD01, MD05, MD08 |
| DEC14 | Byte-exact Exported-PDU sidecars versus genuine PCAP transport/reassembly semantics | MD01, MD04, MD08 |

Reference EHCP frame byte lengths/checksums, for actual-engine fixtures once inputs are available: 99/EB52, 79/530E, 152/8C4D, 99/0F57, 73/05C1, 158/35D8, 99/D95B, 158/4ACB, 151/2CFC, 99/AD5C, 158/0BD5. The addendum reports local length/checksum calculations; this integration has not executed those protocols.

The partial EHCI case splits CNT as CN/T, keeps the explicitly unrelated diagnostic visible, declares INF 2929 bytes, exposes the SEI 24-declared/22-observed discrepancy and has no terminal CRC. It remains incomplete with checksum unavailable/pending; separate complete fixtures test CRC behavior. Length-based boundaries, repeated maps/empty values/NULs and CRC-like substrings are covered without arbitrary brace/time-gap termination.

The sanitized synthetic AT sequence TX AT+CSQ at 0 ms, RX registration URC at 10 ms, RX CSQ 23,99 at 30 ms and RX OK at 40 ms yields one transaction, one URC, -67 dBm and unknown BER. The supplied nested example's local oracle is 48 bytes, counter 9 and checksum D50D; source payload/account details are not copied into public fixtures/issues. Sanitized replacements require their own regenerated expected checksums. Odd hex, missing bytes and wrapper/child integrity remain independent diagnostics.

## Implementation slices

## MD01 — Prove real TShark and Lua application-PDU decoding

[Full implementation card](tasks/MD01.md). Hard dependencies: G01, G02, G03. Owners: E01, E09.

## MD02 — Extract envelopes and assemble bounded nested message streams

[Full implementation card](tasks/MD02.md). Hard dependencies: G01, G02, G03, F02, F04, MD03. Owners: F02, F04, D07.

## MD03 — Negotiate the supervised decoder contract and preserve JSON previews

[Full implementation card](tasks/MD03.md). Hard dependencies: G01, G02, G03, F01, F02, E01, MD01. Owners: E01, F01, F02.

## MD04 — Import typed field trees with exact transformation provenance

[Full implementation card](tasks/MD04.md). Hard dependencies: G01, G02, G03, MD03. Owners: E01, U04.

## MD05 — Reuse EHCP and EHCI packs with bounded partial inspection

[Full implementation card](tasks/MD05.md). Hard dependencies: G01, G02, G03, MD01, MD02, MD04. Owners: E09, D07, D17.

## MD06 — Decode AT conversations and nested SRING payloads in Wireshark

[Full implementation card](tasks/MD06.md). Hard dependencies: G01, G02, G03, MD02, MD04, MD05. Owners: E09, F04, D17.

## MD07 — Integrate the inspector, analytics and shared automation

[Full implementation card](tasks/MD07.md). Hard dependencies: G01, G02, G03, MD04, MD05, MD06, U04, F07, V02. Owners: U04, F07, D17.

## MD08 — Validate engine distribution, performance and decoder release evidence

[Full implementation card](tasks/MD08.md). Hard dependencies: G01, G02, G03, MD07, E09. Owners: E01, E09, R01.

## Inputs, references and evidence limits

The supplied Markdown names 7,831-line EHCP and 867-line EHCI Lua sources and reference captures, but those files are not part of this attachment or the repository doc folder. MD01 obtains/pins permitted copies and complete fixtures; an unavailable input is an explicit blocker for its runtime proof. No compatibility interpreter is substituted. Preserve attribution/licensing if any source is copied. Public fixtures/issues use synthetic/sanitized data only.

Current source confirms PreviewDefinition/PreviewFieldSpec in src/ui/include/previewconfig.h and parsing coupled to QTreeWidgetItem in src/ui/src/previewmessagetab.cpp. The proposed src/logformat module does not currently exist; its build registration and ownership are explicit MD03 work, reconciled with F02's implementation before creating a duplicate module.

Planning documentation checked [text2pcap Exported-PDU dispatch](https://www.wireshark.org/docs/man-pages/text2pcap.html) through Context7's official Wireshark corpus. This supports the bridge design, not custom-script runtime compatibility. Further implementation references: [TShark](https://www.wireshark.org/docs/man-pages/tshark.html), [Lua dissectors](https://wiki.wireshark.org/lua/dissectors), [PDML structure](https://gitlab.com/wireshark/wireshark/-/blob/master/doc/README.xml-output), [developer API/ABI policy](https://gitlab.com/wireshark/wireshark/-/blob/master/doc/README.developer), [ITU-T V.250](https://www.itu.int/rec/T-REC-V.250/en), [3GPP TS 27.007](https://www.etsi.org/deliver/etsi_ts/127000_127099/127007/18.07.00_60/ts_127007v180700p.pdf) and [TS 27.005](https://www.etsi.org/deliver/etsi_ts/127000_127099/127005/17.00.00_60/ts_127005v170000p.pdf).

Direct EPAN/libwireshark is a later measurement-justified, version-pinned helper option. It is not a GUI dependency or part of MD01–MD08. Roll back public decoder/inspector consumers before shared contracts; ordinary logs, current JSON previews and existing plugins stay usable without the optional backend.
