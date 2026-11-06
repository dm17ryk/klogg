# F01d: checked dataset schema metadata

This internal prerequisite adds `DatasetColumn` and checked `DatasetSchema` in
`klogg_logdata`. It uses merged exact-value type names and compiles with QtCore
alone. It introduces no reader, scheduler, public operation or widget and does
not depend on PR #91's event references.

`makeDatasetSchema` freezes an ordered vector of columns. A column carries an
explicit stable `fieldId`, display `name`, `FieldType`, `unit`, `semanticRole`,
`clockDomain`, `nullable` and `repeated`. IDs are opaque and case-sensitive;
duplicate IDs fail, while duplicate display labels are allowed. Renaming a label
does not change its field ID. An empty table schema is valid and creates no
synthetic columns, rows, locations or source identities.

The initial supported shape is `table`, with schema version 1 and shape-metadata
version 1. Unsupported versions, shapes, roles and type tags fail explicitly.
Known reserved FieldType declarations (timestamp, duration, array and object)
remain descriptive metadata: existing value parsing/decoding still rejects
unsupported payloads. Future publishers must check payload capabilities before
exposing a dataset. Other roles are descriptive; this codec does not compile
operators, interpret arbitrary unit strings or infer actual row values.

Timestamp columns require an explicit clock-domain ID. A `time` role requires
Timestamp and a clock domain; the codec never assumes UTC, local time, a boot
epoch or floating-point milliseconds. Different domains remain distinct metadata.
Actual time parsing, alignment and comparison remain owned by F05/F06.

## Wire and identity

`encodeDatasetSchema` and `decodeDatasetSchema` use exactly these top-level keys:
`schemaVersion`, `schemaId`, `shape`, `shapeMetadata` and `columns`. Shape metadata
contains `schemaVersion: 1`. Each column supplies all eight fields above, including
explicit Boolean flags and empty strings for optional text. Unknown/missing fields,
map/list substitutions, JSON nulls where strings are required, Boolean coercion and
unsupported version representations fail. Null C++ QString values for optional
text normalize to non-null empty strings before publication, preserving Qt5/Qt6
JSON behavior. Unicode is preserved, while unpaired surrogates and
ASCII control characters (C0 and DEL) are rejected. No source bodies or label/domain values are logged.

`schemaId` is a consistency fingerprint, not authentication, a source identity,
a dataset revision or an opaque cursor. SHA-256 consumes length-framed UTF-8 tokens:
the domain `cilogg.dataset-schema.v1`, shape `table`, shape version `1`, decimal
column count, then each column's ID/name/type/unit/role/domain and flags `0`/`1` in
order. Every token is prefixed with its unsigned 32-bit big-endian byte length.
The public fingerprint is `schema1:` plus lowercase hex. Framing avoids ambiguous
concatenations; explicit order prevents silently reinterpreting positional values.
Changing names, types, roles, units, domains, flags or order changes the fingerprint.
Source/parser/query revisions belong in the future descriptor/page contract.

The fixed two-column fixture (line/Physical line/UInt64/line/dimension/empty/false/false;
latency/Latency/Decimal/ms/measure/empty/true/false) has fingerprint
`schema1:59c22156205b96fa10172686495f1a157b0138c0e14093dab98b1f843723adf2`.
The expected value was computed independently of the C++ implementation.

## Bounds and errors

The initial hard ceilings are 64 output columns; 128 UTF-16 units per field ID,
256 per name, 64 per unit and 256 per clock-domain ID. The aggregate metadata
budget is 32 KiB of UTF-8 shape/version text and column text plus two flag bytes
per column. This measures decoded metadata, not serialized JSON length. Input
fields and column counts are bounded before UTF-8 hashing; excess metadata fails
without dropping or truncating columns. Readers/services can impose lower budgets.

Structured errors include `schema_column_limit`, `schema_metadata_limit`,
`duplicate_field_id`, invalid field/name/unit/domain metadata, unsupported
type/role/shape/version, `missing_clock_domain`, `invalid_time_column`, malformed
wire fields/flags and `schema_identity_mismatch`. Decision logs contain reasons,
column ordinals, known type/role tags, flags, counters and the computed fingerprint.

## Remaining integration

Dataset rows/pages, revision-bound cursors, query AST, the complete dataset JSON
Schema, richer source manifests/resolvers and GUI/CLI/native-MCP consumers remain
later slices. They must use versioned codecs and must not treat a matching schema
fingerprint as proof of current data or navigable provenance. F01/W0/MD01 and the
unmeasured P00 performance/release gates remain open. Local Qt6 validation does not
establish Qt5 or hosted platform/backend acceptance.
