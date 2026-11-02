# MD01b: genuine-capture comparison

This is proof preparation for MD01, dependent on PR80's real-engine harness.
It compares selected protocol trees from a genuine capture with trees produced
by sending the exact engine-extracted application bytes through text2pcap's
Exported PDU bridge. It does not add a product decoder, inspector, Commander
command or GUI/CLI/MCP feature. MD01 and the broader W0 acceptance remain open.

## Run the comparison

Use Python 3.9+, the Windows Wireshark tools and explicitly trusted Lua packs:

```powershell
python scripts/plugins/message_decoder_reference.py `
  --tshark C:/Essence_SC/Wireshark/tshark.exe `
  --text2pcap C:/Essence_SC/Wireshark/text2pcap.exe `
  --lua <path-to-essence_ehcp.lua> `
  --lua <path-to-essence_ehci.lua> `
  --capture <private-reference.pcapng> `
  --sha256 <expected-capture-sha256> `
  --dissector ehcp `
  --timeout-seconds 60 `
  --output build_root/md01b-reference
```

`result.json` contains hashes, counts, engine identities, source metadata,
command diagnostics metadata and termination reasons. Packet bodies and field
values are excluded. Local command paths can identify the machine; review even
summary artifacts before sharing them. No private Lua or capture body is
bundled. Explicit scripts execute with the user's permissions, not in an OS
permission sandbox.

Exit 0 means this selected comparison passed. Exit 1 means a comparison,
engine, I/O, timeout or resource failure; exit 2 means invalid/missing input.
Every output keeps `productDecoderAccepted: false`. An old result is removed
before starting; a failed run cannot reuse an earlier successful report.
If the result path aliases an explicit input, exit 2 and a diagnostic preserve
that input; no report is written at the unsafe destination.

## What is compared

The capture is copied once with a required SHA-256, bounded reads and checks
against concurrent file replacement/change. All subsequent reads use that
snapshot. The existing engine harness similarly snapshots explicit Lua packs
and checks private plugin-directory isolation. Capture reads use `-n -2` with
the same engine and scripts, selected protocol filter, `-T jsonraw -j` for
application bytes and `-T pdml -J` for expanded protocol children.
`--no-duplicate-keys` preserves repeated raw protocol occurrences as arrays.
The CLI switches follow the
[Wireshark TShark manual](https://gitlab.com/wireshark/wireshark/-/blob/master/doc/man_pages/tshark.adoc).
The actual 4.6.9 raw metadata layout is also exercised by native tests.

Packet number, occurrence order, protocol count, raw size and protocol-root
range must agree before comparing. Each occurrence is decoded as one named
Exported PDU; no TCP/IP stack is fabricated for that application payload.
Comparison preserves ordered children and exact PDML attribute strings,
including repeated fields, sizes, raw values, Unicode and empty versus absent
attributes. Only the engine's `pos` attributes and generated tree-path IDs are
excluded. A different field, size, child order or carrier-dependent value fails.

Engine raw metadata and any data-source index are retained separately.
`originalCaptureByteOffset` is explicitly null, including when an engine
offset happens to look like a packet offset. A reassembled/transformed buffer
must not become an invented original capture or log byte span. This comparison
does not qualify portable generated-field flags or source sidecars.

## Bounds and observed outcomes

Capture admission is 64 MiB, selected protocol occurrences are capped at 32,
and each raw/PDML child output is capped at 16 MiB. The inherited bridge limit
is 262,144 bytes including its Exported PDU tags; XML node/depth limits and
owned child-process cleanup remain in force. JSON duplicate keys, malformed
metadata, excessive nesting, non-finite constants and oversized integers fail
explicitly. There is no silent sampling. A run exceeding a limit is failed,
even if earlier comparison records are retained.

The default overall budget is 60 seconds, configurable up to 600; each child
also retains its 30-second ceiling. Setup, capture reads, bridge execution and
delivery share the overall deadline. These are admission/execution bounds,
not a measured peak-RSS or production throughput claim.

The [local observation summary](md01b-windows-observations.json) binds reports
to source, engine and input hashes. With TShark/text2pcap 4.6.9 and Lua 5.4.6:

| Reference | Selected comparison |
| --- | --- |
| Private EHCP capture, SHA `4d06cf6278140ef219603305a5eec2cd2d66f4ae4f65346e9b881343e36fc7c8` | 23 packets / 23 protocol occurrences equal |
| Private EHCI capture, SHA `9618b6f3e433085015687ba6cbe484470f8a1fe0908b7ec5e2c266f3e716bc9f` | Frame 19883: one 3281-byte PDU equal; engine data-source index 1 |
| Invented UDP fixture | Two packets / three protocol occurrences equal; Unicode and transformed value exactness checked separately |
| Invented carrier-dependent fixture | Two explicit mismatches; lost UDP port context cannot pass |
| Expired overall budget | Failed result replaces an earlier successful report |

The full EHCI capture reads emit stderr diagnostics. Their byte counts/hashes
are retained; equality of the selected tree is not a warning-free full-capture
claim. Diagnostics plaintext can contain private data and is not published.
Native tests run only when both tool paths are explicitly supplied. The
Windows native CI job and focused CTest registration include both proof suites;
portable tests also run in the existing plugin matrix. Hosted outcomes belong
to their exact PR head and remain separate from local evidence.

Local final verification passed 104 plugin cases on each of Python 3.14 and
3.9, and all seven CTest registrations in 225.58 seconds using a byte-verified
private copy of the deployed runtime. Commands, arguments and properties were
preserved, with only the runtime prefix relocated. No fresh C++ build is
claimed. Two earlier standard-runtime runs passed 6/7 and failed at different
existing action/preview repository saves; both failures and their restored
portable-profile identities are retained in the observation. The isolated unit
binary also passed all 207 cases separately. This does not prove the cause of
the original intermittent settings-save failures.

## Remaining gates and rollback

Same-engine equivalence is not an independent validation of Lua semantics or
complete capture coverage. The selected genuine EHCI carrier works, while
MD01a's bare partial and direct embedded-NUL EHCI capabilities remain failed.
The private counter-9/D50D oracle, licensing/distribution, broader framing
coverage, Unix native isolation, original-log mapping and MD03/MD04 product
contracts remain open. No existing JSON preview engine is replaced.

Revert this utility, synthetic reference fixture, tests and registration
extension to remove MD01b. The optional timeout argument added to the shared
proof command defaults to its previous 30 seconds. Installed packs, live
captures, application settings and the Commander catalog need no migration.
