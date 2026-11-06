# Dataset revision references (F01e)

This internal QtCore/C++17 contract prepares revision-bound cursor and lineage
references. It adds no public operation, scheduler, registry or source reader.
AnalysisJobService remains the job, storage and lifetime authority.

`DatasetRevisionStamp` binds job ID, dataset revision, request revision,
source-manifest revision and an opaque schema ID. `DatasetReference` adds a
cursor/lineage kind and opaque token. Construction is checked; moved-from values
may only be destroyed or assigned. IDs and tokens are nonempty, at most 256
UTF-16 units, valid paired Unicode, and contain no C0 or DEL controls.
All revision counters accept the complete unsigned 64-bit range, including zero.

## Validation and ownership

Decoding checks structure only. It grants no access or navigation authority.
`validateDatasetReference` first compares all five stamp components with the
current retained stamp. A mismatch returns `stale_cursor` or `stale_lineage`
without calling the owner. Equal metadata still requires
`IDatasetReferenceLookup::check(kind, current, token)`.

The existing job owner must check bounded retained state for the issued token,
kind and original revision binding, plus job existence, release and expiry.
Replacing an old token's advertised stamp with current metadata cannot make it
valid. Owner errors propagate unchanged. Success carrying an error, or failure
without an error code, returns `invalid_lookup_result`. Exceptions propagate to
the service boundary rather than becoming successful empty results.

Validation is point-in-time. The job owner must pin the immutable retained epoch
through data consumption, or validate and read atomically under the same lock.
This helper does not synchronize reads, retain results or guarantee future
availability. A lineage consumer must also validate row/kind association before
resolving its normalized selector.

## Version 1 wire shapes

A stamp has exactly these keys:

```json
{"schemaVersion":1,"jobId":"job-A","datasetRevision":"7","requestRevision":"8","sourceManifestRevision":"9","schemaId":"schema-A"}
```

A reference has exactly `schemaVersion`, `kind`, `revision` and `token`.
`kind` is `cursor` or `lineage`; `revision` is the stamp above. Each counter is a
canonical decimal string: no sign, whitespace, leading zeros or overflow. JSON
numeric 1 is the only supported schema version; strings and booleans are rejected.
Unknown keys, malformed nesting and coercible-but-wrong types are rejected.
Tokens contain no defined physical line, byte offset or source identity.

Factory/codec errors include `invalid_job_id`, `invalid_schema_id`,
`invalid_reference_token`, `unsupported_reference_kind`,
`invalid_dataset_revision`, `invalid_dataset_reference`,
`unsupported_dataset_revision_version` and `unsupported_dataset_reference_version`.
Diagnostics include fixed rejection codes, kind, hashed job/schema IDs, revision
counters and token length. Tokens, selectors, bodies and arbitrary owner error
messages are not logged.

## Integration still required

DatasetRow/DatasetPage/LineageHandle adapters, schema descriptor association,
query AST, normalized selectors, token minting, retention, limits, expiry and
atomic publication remain F01/F07/F10/F11 work. GUI, CLI and native MCP will use
the same owner validation when those operations are introduced. This internal
slice does not complete F01 or establish source-navigation authority.

Local evidence: [F01e validation](workbench/f01e-dataset-references.md).
