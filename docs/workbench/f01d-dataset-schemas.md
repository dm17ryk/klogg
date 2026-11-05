# F01d schema-contract preparation

Base: `080b504c184796ad3942383149d0385503dd9643` (merged master).
Branch: `codex/f01-dataset-schemas`; independent of open PRs #90 and #91.

Scope: checked immutable table-schema metadata, stable field IDs, explicit
types/roles/units/clocks, strict version-1 codec, bounded metadata and a framed
content fingerprint. Existing FieldValue and public analysis operations remain
unchanged. See [contract](../analysis-dataset-schemas.md) and
[scoped plan](../../plans/cilogg-workbench-2026-10-21/slices/F01d-dataset-schemas.md).

Test-first evidence: the QtCore-only runner compiled actual schema, value and
logging source; empty API assertions failed all nine initial cases (14 failed
assertions). The existing scalar baseline passed 332 assertions in six cases.
The implemented runner passed before final interoperability coverage was added.
Final QtCore-only and native focused tests passed 506 assertions in 12 cases. Fresh
MSVC Qt6.10.1 ci_build passed; strict Clang conversion/sign/width checks passed
both new TUs. Full isolated registered CTest passed 7/7 in 337.97s, including
12,743 unit, 3,172 integration and 22,721 GUI regression assertions.
See [structured observations](./f01d-observations.json) for frozen source hashes.
Fresh whole-branch C++ and general reviews approved with no actionable findings.
Reviewers inspected the supplied validation evidence without independently rerunning
builds/tests; Qt5/hosted/backend/release and complete analyzer gates remain open.

Decisions: table-shape-v1 metadata only; known reserved types do not enable their
payload codecs. Additional shapes, pages/cursors/query/full dataset schema and
consumer capability checks need their own versioned integration. Schema tests
use a separate file so event-origin PR #91 can be reviewed and built independently.

Index/platform boundaries: GitNexus's sibling index is stale and new targets are
absent; those results are not structural verification. CodeGraph previously
reported this linked checkout unindexed. Qt5, accelerated/backend and hosted
native platform acceptance remain separate from local Qt6.10.1 evidence.
