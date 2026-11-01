# MD01a: real-engine PDU proof

This independently reviewable MD01 preparation slice runs the installed
TShark/text2pcap processes and explicitly supplied Lua scripts. It adds no
CILogg decoder command or preview UI. The existing JSON Previewer remains a
separate engine. MD01 acceptance and the original W0 dependency gate stay open.

The test-only bridge is [message_decoder_pdu.py](../../scripts/plugins/message_decoder_pdu.py).
It converts exact application bytes to an Exported PDU naming the target
dissector, runs TShark, and preserves its ordered PDML tree. It does not implement
EHCP/EHCI semantics or emulate Wireshark's Lua API. No invented network addresses,
wire times or TCP reassembly are added. Named dispatch follows the official
[text2pcap contract](https://www.wireshark.org/docs/man-pages/text2pcap.html).

## Reproduce the portable engine probe

Use Python 3.9+ and explicit paths to installed Wireshark tools. From the repo,
for example in PowerShell:

```powershell
python scripts/plugins/message_decoder_pdu.py `
  --tshark C:/Essence_SC/Wireshark/tshark.exe `
  --text2pcap C:/Essence_SC/Wireshark/text2pcap.exe `
  --lua test_data/message_decoders/pdu_probe.lua `
  --manifest test_data/message_decoders/probe.json `
  --output build_root/md01-probe
```

Read `result.json` in that output directory. Exit 0 means the selected fixture
oracles matched; exit 1 means an engine/oracle/resource failure, and exit 2 means
invalid or missing input. Every report explicitly keeps `productDecoderAccepted`
false. A failed or undispatched case cannot be hidden by a `knownGap` annotation.
The complete/partial pack profile deliberately returns exit 1 with the pinned
local scripts because its incomplete-input capability fails.

The [synthetic fixtures](../../test_data/message_decoders/README.md) include a
48-byte EHCP frame with counter 9, invented account `TEST0001` and checksum CFDC;
a complete EHCI frame with repeated keys and an empty value; bad checksum,
partial and embedded-NUL cases; and a narrow synthetic SRING wrapper calling the
real EHCP dissector. The original private SRING example's D50D checksum is a
different oracle and has not been runtime-qualified by this slice.

To run the supplied packs locally, repeat `--lua` for their explicitly chosen
paths and select `packs.json`, `binary-input.json` or `nested.json`. The nested
profile additionally needs `pdu_probe.lua`. These profiles do not bundle the
private packs. Copying/distributing them still requires license verification.
Explicit Lua files run with the user's permissions; process ownership is not an
OS permission sandbox.

## What the proof retains

- Exact versions and executable hashes, immutable script snapshots and hashes,
  the manifest hash and each decoded input's byte count/hash.
- Literal argv, bounded diagnostics, timings and process-reaping outcomes in
  local result artifacts. Temporary bridge files are removed after the run.
- Ordered protocol/field children and unchanged PDML attribute strings, including
  repeated occurrences, empty strings, raw hex and large exact integer text.
- Exact per-case expected and observed display-value lists. Required protocol
  presence is checked independently of field assertions.

Each node has a unique tree-path ID and
`positionOrigin: engine_data_source_unresolved`. PDML positions are **not**
original-log offsets. The synthetic probe emits repeated bytes 1/2, a generated
string `9007199254740993` and a transformed `4142` source through real Lua.
Its values are observed; portable generated-field flags and transformed-buffer
to original-log mappings remain unproven. An absent PDML attribute is not
converted into a false/empty attribute. This proof model is not the future
shared product result schema.

## Bounds and isolation

The proof permits 32 cases, 8 explicitly selected scripts of at most 1 MiB each,
and a 1 MiB manifest-payload admission ceiling. The actual text2pcap bridge has
a stricter 262,144-byte frame ceiling including its padded dissector-name TLV
and end tag: for `data` this permits 262,132 application bytes. Larger PDUs fail
before launching a tool with `bridge_input_limit`; one PDU is never split.
Each child has a 30-second wall deadline,
16 MiB per output stream, and an owned process tree. XML input is UTF-8 only;
DTD/entities, malformed XML, more than 50,000 nodes and depth over 128 are
rejected. Retained case records are capped at 16 MiB of compact serialized JSON;
exhaustion stops the profile with an explicit `report_storage_limit` failure.
This is not a calibrated peak-RSS claim or the MD03 production supervisor.
Cleanup failures produce structured failed reports; an existing report is
removed before a new invocation. If cleanup also fails during a timeout, the
timeout remains primary and the cleanup failure is recorded separately.

Private configuration/plugin directories are checked using the actual engine's
folder report before explicit scripts run. Unexpected Lua folders fail closed
as `plugin_isolation_unverified`. Windows 4.6.9 is locally qualified. The native
CI job uses Windows; portable unit tests also run on Linux/macOS. Unix native
engine isolation remains pending: the
[4.2 filesystem implementation](https://gitlab.com/wireshark/wireshark/-/blob/wireshark-4.2.2/wsutil/filesystem.c)
derives its personal plugin folder from the user's home independently of the
configuration override. This slice neither changes the user's home nor claims
that the Windows isolation profile proves Unix isolation.

## Observed local outcomes

The [machine-readable observation summary](md01a-windows-observations.json)
records source-coupled raw-result hashes and actual checks. The installed
TShark/text2pcap 4.6.9 report Lua 5.4.6. The local external pack identities remain:

| Input | SHA-256 |
| --- | --- |
| essence_ehcp.lua | `8c58aa088b8d151f0aa053e86d4de8aebe3f5e280771157fb8841f8276737d35` |
| essence_ehci.lua | `7cf4bef218f2d48b0dc229c950d91a82a52bb29c3b2de50cdc6411914ffddaea` |

| Case | Actual result |
| --- | --- |
| Synthetic named probe | Repeated/generated/transformed value oracles pass |
| Complete EHCP | 48 bytes; counter 9; checksum CFDC valid |
| Complete EHCI | 75 bytes; repeated A keys and empty E value; checksum ABBF valid |
| Corrupt EHCI | Explicit checksum validity false |
| Bare partial EHCI | **Failed capability:** no EHCI field tree emitted |
| Direct EHCI containing NUL | **Failed capability:** no EHCI field tree emitted |
| Synthetic SRING → EHCP | Real child dissector emits counter 9 and valid CFDC |

The two failed capabilities are retained as failures in the reports. Tests that
observe these pinned-pack gaps prevent accidental success claims; they are not
partial-decoder acceptance tests. Source inspection suggests the direct EHCI
entry point's whole-buffer string scan occurs before its binary-safe INF parser;
fixing or qualifying the actual pack is separate work, not a second host parser.

## Remaining MD01 gates and rollback

Compare normalized output with genuine reference captures using the same pinned
engine/scripts; qualify complete/partial framing, NUL behavior and the private
48-byte/counter-9/D50D oracle; resolve licenses; and prove exact source sidecars
and unsupported mappings. MD03/MD04/MD07 then own supervised product integration,
typed provenance and the new inspector with shared GUI/CLI/Python/MCP behavior.
PR79 is now merged and its successful hosted first-result/native reports have
been downloaded and source-qualified. G02's broader baseline acceptance remains
in progress; none of the decoder tasks is marked complete by this proof slice.

Revert the test registration/workflow, proof script and synthetic fixtures to
remove this slice. No application setting, installed pack, live capture,
Commander catalog or existing JSON preview definition needs migration.
