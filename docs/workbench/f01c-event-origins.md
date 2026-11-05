# F01c — Event identity and discriminated origin preparation

Base: `080b504c184796ad3942383149d0385503dd9643` (merged master).
Branch: `codex/f01-event-origins`. Independent of PR #90.

Scope: internal checked physical/inline/deferred-file/managed-byte references,
weak physical lifetime validation, domain-separated event keys and an exact
version-1 metadata codec. No new UI, command, reader, backing store or resolver.
See [contract details](../analysis-datasets.md) and
[the scoped plan](../../plans/cilogg-workbench-2026-10-21/slices/F01c-event-origins.md).

Decision: all factories return checked results; physical origin holds one frozen
snapshot and row endpoints. This prevents malformed inline values and historical
wire metadata from receiving navigation authority. Future consumers must check
factory errors and obtain endpoint locations through the existing snapshot API.

Validation: baseline 332/332 scalar assertions; empty-API RED failed 10/11 cases;
GREEN passed 124 assertions / 12 event cases. Fresh MSVC Qt6.10.1 ci_build and
strict Clang conversion/sign/width/dangling-else syntax checks passed. Full isolated
registered CTest passed 7/7 in 285.77s, including 12,361 unit, 3,172 integration
and 22,721 GUI regression assertions. These are local regression results;
external-engine opt-ins and hosted platform gates remain separate. See
[structured observations](./f01c-observations.json) for frozen source hashes.

Reproduce with the existing ci_build target, cilogg_tests [eventorigin] and the
seven registered CTest groups (ctest --test-dir build_root -C RelWithDebInfo --verbose).
This session deployed the binaries to a fresh private portable runtime and
relocated only registered runtime paths/working directories before CTest.

The deferred descriptor is intentionally bounded metadata. The full manifest
registry and richer file-change identity remain owned by F07/F16; those owners
must extend/version this metadata before materialization or consumer integration.

Boundaries: parent F01/W0/MD01 and generic decoder integration remain open. Strong
filesystem verification, managed backing resolution, per-character byte mappings,
dataset/schema/cursor services and GUI/CLI/native-MCP consumer parity are later
slices. Qt5, accelerated backend and hosted platform acceptance are not established
by a local Qt6.10.1 build. GitNexus's sibling index is stale; refresh attempts are
bounded and stale results are not counted as structural verification. CodeGraph
reports this checkout unindexed.
