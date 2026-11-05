# Synthetic decoder proof fixtures

These files contain invented test data, not copied captures or customer log
bodies. The Lua fixture is newly authored under GPL-3.0-or-later; it exercises
real Wireshark APIs and dispatches to an explicitly supplied EHCP pack. It does
not contain an EHCP/EHCI semantic parser. Private packs are not included.

Each manifest binds decoded application bytes by SHA-256. `.hex` files are
literal byte encodings; whitespace is formatting only. They are never parsed
as ordinary log strings. `probe.json` needs only the synthetic Lua fixture;
`packs.json` and `binary-input.json` require the supplied external packs;
`nested.json` requires both the external EHCP pack and `pdu_probe.lua`.

| Fixture | Purpose |
| --- | --- |
| probe | Bytes 01/02; repeated fields, generated exact text and transformed 4142 source |
| ehcp-complete | Invented account TEST0001; counter 9; 48 bytes; recalculated CFDC |
| ehci-complete | Repeated map keys, empty value, 75-byte complete message; ABBF |
| ehci-bad-checksum | Same complete message with deliberately incorrect 0000 |
| ehci-partial | Complete fixture without its seven-byte CRC trailer |
| ehci-nul | NUL inside a map value, with a following map and valid recalculated 3B8B |
| sring-nested | Explicit synthetic SRING: 1,48 dialect carrying the synthetic EHCP bytes |

The original supplied 48-byte/counter-9/D50D message is private and different.
These replacements have their own independently recalculated checksums, tested
against the actual engine; do not substitute their checksums for its oracle.

`sring_probe.lua` is a separate strict single-envelope proof fixture with named
data/EHCP/missing-child variants. It validates an explicit ASCII SRING dialect,
reports independent envelope/child invocation statuses, and invokes the real
registered child. `nested-strict.json` / `.hex` reproduce its synthetic data
case without private packs. Header spaces/tabs are explicit; payload whitespace,
odd hex, mismatched lengths, empty data and out-of-budget values are rejected.
The private D50D reference is qualified separately by MD01d, with hashes and
metadata only. This fixture is not an AT conversation parser or product decoder.

`reference_probe.lua` is another newly authored GPL-3.0-or-later fixture. Tests
generate two invented UDP packets: its two-byte PDUs expose repeated protocol
roots, exact Unicode and transformed values. A second UDP-port dissector
deliberately depends on carrier context so the comparison must reject unequal
trees. No generated PCAP or private packet body is committed. See the
[reference comparison guide](../../docs/workbench/md01b-reference-captures.md).

The current pinned EHCI script emits no protocol tree for bare partial and
direct embedded-NUL inputs. Their manifests demand the missing capability and
therefore remain failed; the unit suite separately observes that known failure.
See [the proof guide](../../docs/workbench/md01a-engine-proof.md) for limits,
commands, evidence and remaining acceptance gates.

## Corrected external-pack capability profile (MD01e)

`ehci-capabilities.json` contains nine synthetic cases for a separately qualified
external EHCI pack. `byte_api_probe.lua` probes real byte/NUL and reassembly APIs
without protocol semantics. Optional `expectRaw` entries compare exact engine raw
attributes. The historical failures above refer to the original pinned pack;
they are preserved. See [MD01e](../../docs/workbench/md01e-ehci-capabilities.md)
for the corrected private-pack hash, opt-in tests and remaining acceptance gates.
