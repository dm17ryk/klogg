# MD01c: real-engine field-origin qualification

This proof extends MD01a/b without adding a product decoder or public operation.
It qualifies **payload-relative byte ranges** for one immutable application PDU
passed through the named Exported-PDU bridge. MD01 and G02/W0 remain open.

```powershell
python scripts/plugins/message_decoder_origins.py `
  --tshark C:/Essence_SC/Wireshark/tshark.exe `
  --text2pcap C:/Essence_SC/Wireshark/text2pcap.exe `
  --lua test_data/message_decoders/pdu_probe.lua `
  --manifest test_data/message_decoders/probe.json `
  --output build_root/decoder-origin-proof
```

The manifest format is the existing MD01a format. SHA-256 validation, ordered
field-value oracles and the real Lua-error checks run before origin qualification.
An explicitly supplied script executes with the caller's permissions. This is
process supervision, not an OS permission sandbox.

## Binding and identity

The proof retains PDML for the existing semantic oracle, and separately reads
raw metadata with `-T jsonraw --no-duplicate-keys`. These formats and the grouping
option are documented in the [official TShark manual](https://github.com/wireshark/wireshark/blob/master/doc/man_pages/tshark.adoc).
The native tests qualify the installed engine's actual metadata layout.

Exactly one bridge packet is required. Its frame, exported payload and named
protocol root must agree on bytes, bounds and original frame data-source index
zero. The payload offset comes from verified engine output; it is not a guessed
fixed header size. Each unmasked field in that same data source must fit wholly
inside the payload and equal its exact corresponding bytes before receiving a
`payloadByteSpan`. All offsets are explicitly zero based and measured in bytes.

Repeated fields have distinct ordinal `rawPath` identities in JSON's grouped tree.
Those paths preserve grouped occurrences; they do not reconstruct PDML sibling
order. Container labels are excluded: native dissectors can put payload display
text in those keys. No name-only cross-format field pairing is performed.

| Observation | Outcome |
| --- | --- |
| Exact unmasked bytes in the verified original frame source | Payload-relative span |
| Different data-source index, even with identical bytes | Unmapped: `transformed_data_source` |
| Missing or explicit null data-source index | Unmapped: `data_source_unavailable` |
| Zero-size range | Unmapped: `no_occupied_bytes` |
| Bitmasked value, out-of-payload range or unequal bytes | Explicit unmapped reason |
| Generated-field flag | `generated: null`; raw JSON does not expose it |
| Original log/capture location | `originalSourceSpan: null` |

A payload span proves byte-range evidence in the supplied buffer. It does not
establish whether a field was marked generated, or where a reassembled payload
originally appeared in a capture/log. In the real synthetic fixture, a transformed
field's PDML position looks like a packet position while raw JSON identifies
data source 1. This is why the two coordinate systems cannot be conflated.

## Limits, diagnostics and errors

The existing bridge frame admission remains 262,144 bytes including tags.
Raw output and retained metadata reports are limited to 16 MiB. Traversal is
capped at 50,000 nodes / 128 levels and 10,000 field records. Field metadata is
charged incrementally, including repeated ordinal path prefixes, before retention.
Malformed JSON, duplicate keys, non-finite values and invalid integer metadata
fail explicitly. Five-element metadata remains readable but cannot prove a
data-source identity. Explicit null source indexes are valid unknowns.

The inherited overall budget is 60 seconds, configurable up to 600, with a
30-second child ceiling and owned process cleanup. The report includes engine,
script and manifest identities plus command/termination metadata. Debug logs
record paths, source indexes, sizes, counters and decision reasons; field display
values and raw bodies are excluded from the returned metadata. Paths and field
names can still identify a private environment; review artifacts before sharing.

Manifest and fixture admission uses bounded regular-file reads before report
publication. Nonblocking opens and descriptor checks reject FIFOs/devices,
including a path replaced between the initial stat and open. The manifest is
resolved once for relative fixture paths; its frozen bytes and frozen fixture
bytes are passed to the shared validator without rereading them. Device/inode,
size and modification-time checks reject observed source changes before each
publication (Unix also checks metadata-change time; Windows stat/fstat ctime
interpretations differ).

An OS-backed nonblocking lease serializes invocations sharing an output
directory: `msvcrt.locking` on Windows and `flock` on Unix. A competing invocation
returns exit 1 / `output_busy` without changing the active report. The persistent
`.decoder-origins.lock` inode is not deleted on release; process exit releases
ownership. An admitted run atomically publishes a `running`, non-success marker
and then its complete report with the same `invocationId`. Temporary report
files are owned and removed on failure. These are cooperative local-filesystem
guards, not an OS sandbox or a network-filesystem locking qualification.

Exit 0 qualifies this bounded proof only. Exit 1 records an execution,
origin-binding, semantic-oracle, I/O or resource failure. Exit 2 identifies
invalid/missing inputs, including `bridge_input_limit`. Admitted failed runs replace old success reports. A report
path aliasing an explicit input or declared fixture payload is rejected without
writing over that input. Rejected input admission (including oversized, malformed,
missing or unreadable inputs) leaves the destination untouched. The preserved
destination could be an input or an old report: callers must check this invocation's
exit status before accepting any report. Output write failures return exit 1 and
a diagnostic. No private Lua, capture or log body is committed.

## Evidence and remaining work

The [Windows observation](md01c-windows-observations.json) records source hashes,
input/tool identities, test outcomes and the selected real-capture qualification:

- 23 EHCP PDUs: 980 fields, 760 verified payload ranges, 220 zero-byte fields.
- One reassembled 3,281-byte EHCI PDU: 1,620 fields, 1,142 verified payload ranges,
  473 zero-byte fields and five expert fields with unavailable data sources.
- Synthetic repeated, generated and transformed fields, including an equal-byte
  transformed buffer, preserve their separate origin outcomes.
- A real native DNS query confirms payload-derived subtree labels are excluded
  from both ordinal paths and decision logs.

The input capture hashes match MD01b. These are selected actual-engine
observations, not independent protocol-semantic validation, complete-capture
acceptance, measured peak memory or cross-platform release acceptance.
Original-log/file-byte/UTF-16 sidecars, portable generated flags, partial/NUL
capabilities, the private nested oracle, licensing/distribution and product
GUI/CLI/MCP integration remain open under their existing owners.

The existing CTest and plugin discovery patterns automatically include the new
suite. Windows native CI also publishes the synthetic origin report; hosted
results belong to their exact PR head and remain separate from local evidence.

Rollback removes this helper, its tests, workflow artifact and ledger evidence.
The existing engine proof, JSON Preview, Commander catalog and user settings
need no migration.
