# MD03a — TShark/Lua preview integration

User priority (2026-11-03): deliver the Lua/Python decoder through message
assembly, message preview, hover tooltip, CLI and native MCP. Keep PR87/F01b
independent. Base: merged master 7a8889066665746f63430585a057dcee716529f1.

This slice reuses the existing bounded PreviewService/result version1 and its
existing assembly/source validation. It does not complete the generic MD03
dataset/plugin contract, MD01 partial/NUL qualification, or remaining F01/W0
gates. It delivers an opt-in preview backend while retaining legacy JSON.

## Fixed contracts

- TShark and Lua own protocol semantics. Python supplies bounded process/PCAP
  transport and exact PDML import, without an EHCP/EHCI semantic duplicate.
- Internal Python stdio uses four-byte big-endian JSON lengths. Request256KiB,
  response128KiB; input payload64KiB; raw PDML1MiB; normalized nodes2048/depth32;
  rendered fields512 with explicit limited/incomplete reasons. Strict types,
  IDs, versions and canonical base64. Diagnostics on stderr, metadata only.
- Evaluate request: `{version:1,id,op:"evaluate",profile:{tshark,text2pcap,lua,
  dissector,protocol},payloadBase64,timeoutMs}`. Profile tools/scripts are
  supplied by trusted host settings, not imported preview definitions or input
  log contents. Lua at most8files/1MiB each, snapshotted for the session.
- Response echoes version/id and PreviewService fields: backend`tshark_lua`,
  state/reason/available/complete/limited, previewName, offsetSpace
  `engine_data_source_unresolved`, fields`[{columns:[name,value,raw,pos,size],
  depth,attributes,positionOrigin,id}]`. Preserve exact raw attributes,
  repetitions, empty values and large integers as strings. `complete` describes
  processing, never inferred protocol integrity; report message completeness
  and integrity as not_reported unless the engine explicitly supplies evidence.
- Persistent TShark session for repeated requests per preview owner/channel;
  at most2 native active requests and2 warm sessions globally. No whole-file
  scanner or new independently retained dataset store. Preview deadlines include
  startup/selection/queue/delivery, default3s/max10s. Cancellation/reset kills
  and reaps the owned group/job; no user process enumeration or termination.
- Native parent creates the worker's process group/job before sending input.
  Python child processes inherit that group on Unix; Windows jobs kill descendants.
  Worker failures/timeouts discard the session and allow the next request to
  recover. No OS sandbox claim. Isolate Wireshark config/Lua folders and verify
  the engine folder report before loading explicitly selected Lua.
- Host settings: `enabled`, `allowExternalLua`, `tshark`, `text2pcap`, `lua`,
  `defaultDissector`, `inputEncoding`(utf8/hex/base64), `previewRoutes`(existing
  preview name->dissector), `protocolRoutes`(assembly protocol->{dissector,
  inputEncoding}). Disabled by default. No unapproved script execution from
  imported JSON definitions. Settings revisions invalidate sessions/cache and
  pending delivery; watch configured inputs for invalidation.
- New operations through Commander/catalog/CLI/native MCP:
  `get_preview_decoder`, `set_preview_decoder`, `show_preview_decoder_settings`.
  `preview_message` adds decoder(auto/json/tshark), dissector,inputEncoding.
  Auto prioritizes applicable TShark routes over JSON as detailed below;
  explicit TShark permits direct raw/hex/base64 input without legacy definitions.
  Reuse existing carrier extraction inside the supervised native worker.
- GUI settings in Tools/menu and toolbar, stable accessible names. Preview pane
  has independent Automatic (TShark first), TShark/Lua and JSON modes, mapped
  definitions and direct encoding/budget controls, async evaluation,
  explicit backend/status and engine-buffer offset labeling. Hover uses the same
  service. Preserve existing assembly metadata and historical/source checks.

## Tasks

1. **Python worker:** shared wire/PDML helpers extracted compatibly from proof;
   persistent real-engine worker, bounds/isolation/cleanup, portable and optional
   actual-engine tests. Own scripts/plugins/{message_decoder_preview_worker.py,
   message_decoder_wire.py,message_decoder_pdu.py}, optional compatible
   ProcessOwner inherit-group support and its tests, new tests/plugins worker tests.
2. **Qt backend:** settings store, persistent client/resource extraction/owned
   process group, PreviewService routing and native input extraction, focused
   service tests. Own new previewdecodersettings and tsharkpreviewclient files,
   PreviewService, relevant extraction helper, UI resource/CMake/unit registration.
3. **GUI/Commander:** configuration dialog/menu/toolbar/icon; async manual preview,
   existing hover/assembly integration; semantic get/set/show and preview request
   options across app/Commander/MCP/CLI. Own these UI/app files and native tests;
   consume task2 API rather than duplicating backend logic.
4. **End-to-end:** real sanitized EHCP/EHCI, assembled source and tooltip fixture,
   CLI/native-MCP equivalence and GUI screenshot; focused/full tests, package
   resource checks, docs/PROGRESS, reviews and one complete integration PR.

Each task uses TDD and a fresh task review; final C++/Python/general/security
review covers the complete branch. No source edits during an active compile.
Use local Qt6.10.1, CI Qt6.12/retained platform choices, C++17/Qt5 compatibility.
Do not bundle private Lua packs or private log bodies; user-supplied external
packs may be used locally for qualification. Keep bounded-memory/performance
claims and hosted/native-platform acceptance distinct from local success.

## Integration rulings (2026-11-03)

- Keep trusted executable/Lua paths and approval in a separate host settings store;
  imported PreviewDefinition JSON remains unchanged. Normalized previewRoutes
  map names to `{dissector,protocol}`; protocolRoutes map assembly names to
  `{dissector,protocol,inputEncoding}`. Optional protocol defaults empty.
- Include a harmless runtime decoder revision in internal definition snapshots
  for delivery/cache validation; do not modify exported user definitions or add
  meaningless whitespace. Watch selected files and their parent directories.
- Direct TShark input is supported without a legacy definition. Carrier regex and
  capture/offset extraction execute in the existing supervised native worker.
- Preserve QProcess request-token callers across both native selection and the
  persistent Python stage; cancellation must work after selector exit.
- Qt resources embed trusted Python helpers so packaged executables and private
  test runtimes do not depend on source-tree paths. Private Lua stays external.

## User priority revision — 2026-11-03 (binding, supersedes route-only auto)

User: TShark is more flexible and must have higher priority than current JSON.
When enabled, auto tries applicable TShark routes before JSON. Search mapped
TShark carrier definitions before an earlier matching JSON-only definition.
Unnamed assembly protocol route remains first. Then use the configured default
TShark dissector on a selected/matching carrier buffer, or direct input when no
carrier matches. A default `data` dissector therefore takes priority for ordinary
raw input deliberately; explicit JSON remains available. Disabled auto is legacy.

Automatic JSON fallback is allowed when no TShark route/default applies or the
engine explicitly returns `dissector_not_dispatched` (no real nonwrapper output).
Record preferredBackend, decoderAttempts and fallbackReason in fallback results.
Timeout, Lua errors, worker/protocol failures, invalid input/config and resource
limits remain visible failures; do not hide them with a fallback. Explicit
TShark never falls back. All attempts share one request ID and original deadline;
legacy fallback runs through the supervised native worker too. Preserve the
original input for fallback and lifetime/cancellation through every stage.

## Integration delivery and outstanding gates

The Windows Qt6 integration exposes settings, pane, hover, Commander CLI and
native MCP. See [user setup](../../../docs/workbench/tshark-preview.md) and
`tests/mcp/tshark_preview_parity.py` for opt-in real-engine transport checks.
Local native tests and actual external-pack GUI qualification are recorded in
the task reports. The first external EHCI UI run failed pane availability after
about three seconds (21/22 assertions); the unchanged clean rerun passed 27/27.
The original reason was not captured, so neither a cause nor a performance gate
is established. The fixture uses public synthetic bytes and intentionally invalid
CRC. Full CTest, static/review results, hosted CI, platform and release gates
are recorded separately by the integration owner. MD01 partial/NUL, generic MD03
dataset contract, wider W0 and F01 remain open.


## Final local integration validation - 2026-11-04

The final budget/delivery correction is committed as
`42b81b659f5186d519a379803662d9711b7e9c9d`, with its frozen source hashes matching
the precommit local binaries. Final complete ci_build exited 0; registered CTest
passed all 7 groups in 293.52 seconds. Focused native/portable GUI checks passed
275 assertions/25 cases and 100 assertions/8 cases respectively; actual-engine
opt-ins were absent in that native cohort, not counted as actual-engine passes.
Four changed warning tests passed without skips on both Python 3.9.25 and
3.14.10 with real engine negatives enabled. All 42 strict real CLI/native-MCP
comparisons passed again, including three external fixtures, failures and two
post-stall public probes. The final external EHCI pane/hover check passed
29 assertions/1 case with an explicit supported 10000-ms Automatic pane budget
and unchanged default hover budget. The screenshots are refreshed accordingly.

Twelve strict native/GUI translation-unit checks returned 0 across two runs;
the first wrapper's third VS setup failed with a command-length/PATH error and
the remaining three units passed in a fresh shell. Scoped C++/Python fix reviews
approved the correction; the original integrated timer-producer Minor remains
nonblocking. See the [validation record](../../../docs/workbench/tshark-preview-validation.md)
for binary hashes, original failed combined actual-engine cohort and cold-start
observations, unproved failure causes, preserved failed post-stall external-pack
request and explicit skip/platform/performance/release boundaries. This local
slice does not complete MD01 partial/NUL, generic MD03, W0 or F01.
