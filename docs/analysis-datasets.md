# Analysis data contracts

## F01a: exact scalar values

This is an internal prerequisite of F01, prepared independently while the W0
and decoder acceptance gates remain open. No dataset API, decoder integration,
viewer control or Commander command consumes it yet. F01, MD01 and W0 remain
incomplete. Existing analysis/search results keep their current wire format.

`FieldValue` has a private payload and derives its tag from the variant. The
checked `makeFieldValue` factory is the only construction path. A caller cannot
publish a mismatched tag/payload or bypass bounds by mutating a returned payload.
`DataResult<T>` functions return either a value with an empty error, or no value
with a nonempty error code. `DataError` can carry a field ID and an existing
`SourceLocation`; this scalar slice does not fabricate source positions.

| Type | Version 1 wire payload |
| --- | --- |
| `missing` | No payload member; an absent field |
| `null` | No payload member; an explicitly null field |
| `bool` | Boolean `value` |
| `int64`, `uint64` | Canonical base-10 string `value`, checked against the native range |
| `decimal` | `value: {coefficient: string, scale: integer}`; coefficient × 10^-scale |
| `float64` | `bits`: 16 lowercase hexadecimal digits representing finite IEEE 754 binary64 bits |
| `string` | String `value`, including the empty string and embedded NUL |
| `bytes` | Canonical padded base64 string `value` |
| `invalid` | `value: {reason: string}` and mandatory `rawLexeme` |

Each envelope includes `schemaVersion: 1` and `type`. Optional string
`rawLexeme` preserves the original input separately from the typed value.
For example, UInt64 maximum is serialized as:

```json
{"schemaVersion":1,"type":"uint64","value":"18446744073709551615","rawLexeme":"18446744073709551615"}
```

Large integers and decimal coefficients never pass through a JSON floating-point
number. Float bit strings preserve negative zero and subnormals across Qt JSON
implementations and machine endianness. Nonfinite floats are rejected.
Missing, Null, empty String, false, integer zero and Invalid remain distinct.
Null `QString` inputs to a string value are normalized to an empty string.

`parseTypedValue` uses the requested schema type, without type guessing or locale
conversion. Integers accept an optional plus sign and leading zeros in source
text, then encode canonically; unsigned integers reject a minus sign. Boolean
lexemes are exactly `true` and `false`. Null is exactly `null`. Decimal accepts
an optional sign, decimal point and base-10 exponent; trailing coefficient zeros
and scale remain exact. Float64 uses Qt's finite, complete numeric conversion
without surrounding whitespace. Malformed source lexemes become Invalid with
their original text. Malformed wire envelopes instead return an error, never a
successful Invalid value. Bytes have no implied source-text codec.

Bounds apply before publication: strings and raw lexemes at most 1,048,576 UTF-16
code units each; bytes at most 1,048,576 decoded bytes; decimal coefficients at
most 256 digits (excluding sign), scale −6144…6144; Invalid reasons 1…256 UTF-16
code units. Unpaired UTF-16 surrogates are rejected. Transport-level request and
aggregate dataset limits will be owned by their later consumers; this is a
single-value codec, not a JSON parser or a result-retention service.

Unknown wire members, coercible but wrong QVariant types, unknown versions,
noncanonical numbers/base64, and unknown tags fail explicitly. `timestamp`,
`duration`, `array`, and `object` names are reserved and currently return
`unsupported_value_type`; their clock, recursive-bound and schema contracts
are deferred. `FieldSchema` already carries field ID, name/path/unit, nullable,
repeated, semantic role and profile revision metadata. Only its requested scalar
type drives parsing in F01a; nullable/repeated policies belong to later dataset
validation. Passing Null explicitly is distinct from interpreting the text
`null` as a nullable numeric value.

Diagnostics contain type, bounded counters and static reason codes. They do not
log values or raw lexemes. `[data][F01]` exercises real JSON round trips,
boundary numbers, Unicode/NUL, malformed envelopes and resource bounds. Local
Qt 6.10.1 validation does not establish the planned Qt5/Qt6 cross-runtime gate.

The codec and its tests opt out of the project's fast floating-point assumptions
at source-file scope: `/fp:precise` on MSVC/clang-cl and `-fno-fast-math` on
GCC/Clang/Apple Clang. NaN/infinity admission checks and signed-zero inspection
require those semantics. Future consumers that inspect or compute floating-point
values must also choose the appropriate precise compilation policy. Existing
search and unrelated source files keep their current optimization settings.

Remaining F01 work includes event identity and discriminated origins, physical
byte/UTF-16 spans, dataset schemas/revisions/cursors, source invalidation and
GUI/CLI/native-MCP consumer parity. Rollback of F01a removes this unused codec
and its tests; no existing public result shape needs migration.

## F01b: physical analysis-line provenance

Analysis blocks retain one bounded descriptor per indexed physical record.
`RawByteRange` is half-open and measured in original file bytes. It covers the
complete record, including a BOM on the first record and its terminator where
present. `terminatorBytes` separately records the exact LF or CRLF suffix from
the read bytes (up to eight bytes for UTF-32 CRLF). These ranges come from the
index and actual read, never from UTF-8 re-encoding of a decoded string.

An `AnalysisLine` from an open file carries optional `AnalysisLineProvenance`:
the starting `SourceSnapshot`, original byte range, raw terminator, codec name
and frozen provisional flag. Its existing text is preserved, including CR in
CRLF input. BOM handling and stateful decoding remain continuous across input
blocks and skipped rows. Inline text has no physical-file provenance or invented
file-byte position. Deferred/managed origins remain later F01 work.

`AnalysisLine::sourceLocation` can create an origin anchor with an optional
UTF-16 span bounded by that line's decoded text. It rejects overflowing or
out-of-line spans and invalid byte metadata. The returned anchor can be
historical: navigation must still call the current snapshot's `accepts` method.
Completed locations may survive appends; provisional locations need their exact
revision. Closure prevents anchor creation. Values and recorded ranges remain
inspectable without retaining a file lease.

Descriptors obey the existing 256-record / 1 MiB raw batch limits; oversized
records, stale generations and failed reads publish no record descriptors.
Provisional ranges end at the original indexed watermark, including after an
append, and never include the index's synthetic EOF+1 marker. Diagnostics log
source/job identities, generations/revisions, ranges, counters and reasons,
without decoded bodies.

This metadata is internal. Existing raw/regex/value-count result formats and
their Commander/CLI/native-MCP operations are unchanged. Exact per-character
byte mappings, event identity/assembly, dataset schemas and the new TShark
preview integration remain later steps; a record range alone does not establish
a decoded field's raw-byte span.
## F01c event references (internal prerequisite)

`eventrecord.h` adds checked, immutable `EventRef` values with exactly one
physical, inline, deferred-file or managed-byte origin. These contracts have
no reader, backing-store or resolver ownership and introduce no public command.
F01, the generic dataset model and decoder consumer parity remain incomplete.

Physical references freeze one weak `SourceSnapshot`, zero-based first/last
physical rows and optional original half-open byte bounds. Both endpoints are
validated through the existing source-location API before use. Completed ranges
survive valid appends; an event touching a provisional final row requires its
original revision. Closing the source releases its lifetime independently of
retained results. Closure, replacement and generation changes refuse navigation.
The snapshot/byte range is metadata, not retained or verified file contents;
callers obtain actual ranges from indexed reads (F01b), never re-encode text.

Inline references identify immutable inline job input and its row range, with no
physical source UUID or byte/UTF-16 anchor. Deferred references bind observed
rows/bytes to matching manifest/entry/revision/content-epoch metadata and a
bounded verification descriptor. The descriptor requires a 32-byte SHA-256
prefix digest, encoding, frozen byte boundary and final-line metadata. Its
presence does not verify a file: F16 must check actual identity, strong prefix
and exact mappings before materialization. Managed references identify a resolver,
recording, segment, generation and segment-relative byte range; they have no
physical rows or UTF-16 spans. D01/F07 will own backing availability/length/hash
validation. Neither kind can pass `acceptsEvent` as an open-source location.

All factories return `DataResult<EventRef>`, including the inline factory, so
invalid input cannot create an apparently valid reference. `validateEventOrigin`
checks structural metadata; `validateEventSource` returns structured
`source_closed`, `stale_source`, `unbound_source`,
`deferred_resolution_required` or `unsupported_source_kind` outcomes.
Reference fields are read-only after checked construction. Use moved-from
references only for destruction or assignment, as with other owning value types.

`encodeEventRef`/`decodeEventRef` use schema version 1 with `eventKey`,
`policy` and a single `origin.kind`. Unknown/missing/extra fields, unsupported
versions/types, noncanonical integers, reversed ranges and inconsistent metadata
fail explicitly. Wide counters, generations, revisions, byte offsets and public
one-based line numbers are decimal strings; internal row indices remain zero-based.
No line/byte conversions are inferred. IDs are SHA-256-derived, domain-separated
by schema/origin and include the format revision, assembly revision and occurrence
ordinal. Completed physical IDs exclude append revision/watermark; provisional
IDs include their revision. Wire-decoded physical references are historical and
unbound, even if a matching UUID is currently open. A future checked resolver must
supply live binding; the codec never manufactures a lifetime token.

Identifiers are limited to 256 UTF-16 units, canonical origin metadata to 4096,
encoding to 128 and optional platform-file identity to 1024 bytes. Invalid Unicode
and control characters fail. Exact record bodies are not copied or logged.
Qt5 and hosted platform/backend qualification remain release gates, separate from
local Qt6 tests. No GUI/CLI/native-MCP parity is claimed for this unused contract;
future consumers must publish its versioned shape through the shared service.
