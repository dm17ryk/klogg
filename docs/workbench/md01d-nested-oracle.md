# MD01d: strict nested-wrapper and reference oracle proof

This independent MD01 preparation slice starts from merged master `fae1374d`.
It adds a newly authored Lua proof fixture and real-engine tests, without a
dependency on PR84. It adds no product decoder or public Commander operation.
Full MD01 and G02/W0 acceptance remain open.

## Run the synthetic proof

```powershell
python scripts/plugins/message_decoder_pdu.py `
  --tshark C:/Essence_SC/Wireshark/tshark.exe `
  --text2pcap C:/Essence_SC/Wireshark/text2pcap.exe `
  --lua test_data/message_decoders/sring_probe.lua `
  --manifest test_data/message_decoders/nested-strict.json `
  --output build_root/strict-nested-proof
```

The existing bounded PDU proof harness supplies process ownership, tool/script
identities and ordered PDML oracles. The synthetic `data` child needs no private
pack. The EHCP variant requires an explicitly supplied trusted EHCP Lua file.
Such scripts execute with the caller's permissions; this is not an OS sandbox.

The fixture uses real Wireshark `ByteArray`, `Tvb`, tree and named-dissector APIs,
following the [official Lua dissector guide](https://github.com/wireshark/wireshark/blob/master/doc/wsdg_src/wsdg_lua_support.adoc).
It contains no EHCP/EHCI semantic parser and copies no private pack source.

## Scope and outcomes

The explicit dialect is one ASCII `SRING: socket,length,hex` envelope. Spaces and
tabs are allowed only at the named header boundaries. Hex payload whitespace,
CRLF, odd nibbles, concatenated envelopes, missing/excess bytes and `STRING`
aliases are rejected. It does not normalize or repair the input. A length check
also rejects a prefix-only string conversion: the real engine's `string()` can
stop at an embedded NUL. NUL bytes *decoded from valid hex* remain valid child
bytes, as the native 0..255 byte-pattern test demonstrates.

Envelope `status` and `child_status` are independent. Malformed envelopes report
a reason and `not_invoked`; a valid envelope can report a missing named child.
`invoked` records a real dissector call, not semantic/integrity acceptance.
Tests show the same valid envelope around EHCP children with valid and invalid
checksums. The caller must inspect the child's own protocol results. There is
no fallback dissector and no host interpretation of EHCP fields.

The proof caps a child at 65,536 bytes and the entire wrapper at 131,104 bytes,
before hex-to-byte conversion. Socket/count tokens are bounded before numeric
conversion. It tests the exact child limit and rejects wrapper overflow. These
admission bounds do not establish calibrated peak memory or production throughput.
The inherited bridge/output/tree/process caps remain unchanged. Fixed decision
diagnostics on stderr contain protocol name, byte count and statuses, never
wrapper or child bodies. Parent hex offsets are explicitly zero-based byte
offsets within this ASCII wrapper; they are not original-log or UTF-16 locations.

## Private reference qualification

The supplied reference document is pinned by SHA-256
`feabb255e56a59b363af5f68580c24e321f07e808f432dd8da9a7d9bc4379650`.
Its exact 108-byte SRING example was frozen and decoded through the real external
EHCP pack. The observed child is 48 bytes, counter **9**, received checksum
**D50D**, with checksum validity **True**. Its complete child tree equals direct
decoding after excluding engine `pos` attributes and generated tree-path IDs.

This is a **reference-document example**, not a sampled log-row qualification.
The earlier bounded log inventory did not locate this exact D50D example. No
original-log span or unknown source mapping is invented. The public synthetic
EHCP fixture still uses invented `TEST0001` data and its own CFDC checksum;
it is not substituted for the private D50D oracle.

The [machine-readable observation](md01d-windows-observations.json) records
source/input/engine hashes, metadata-only outcomes and actual validation. Private
Lua, reference bodies and full decoded trees remain outside Git. Same-engine
direct/nested equality is not independent validation of the external pack's
protocol semantics or complete reference coverage.

## Registration, remaining gates and rollback

Existing CTest and native Windows plugin CI discovery already include
`test_message_decoder_*.py`. No workflow/catalog duplication is added. Local
external-pack tests use explicitly supplied paths; hosted CI can qualify the
synthetic child without distributing that pack.

Partial/direct-NUL EHCI capability repair, licenses/distribution, generated-field
flags, original-log sidecars, Unix native isolation and product integration retain
their existing owners and acceptance gates. Streaming/multiple notifications,
AT dialect selection and conversation assembly remain MD02/MD06 work.

Rollback removes this synthetic fixture, manifest, tests and evidence. Existing
PDU/JSON previews, Commander commands, active captures and settings need no migration.
