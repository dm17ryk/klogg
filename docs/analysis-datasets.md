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
