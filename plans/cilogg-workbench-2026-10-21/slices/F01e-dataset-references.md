# F01e — Revision-bound dataset references

Spec: [F01](../tasks/F01.md), [shared data contracts](../02-data.md).
Base: merged master `080b504c184796ad3942383149d0385503dd9643`. Independent of open PRs #90/#91/#92.

## Global constraints

Preserve C++17, Qt5/Qt6, current public operations and AnalysisJobService as sole job/storage/lifecycle authority. No widgets, token registry, source reader, token minting or scheduling. Local Qt6.10.1; hosted Qt6.12 and Qt5/backend/release acceptance remain separate. Full F01/W0/MD01 remain open.

## Task 1: Checked revision stamps, tagged lineage/cursor bindings and owner validation

Create datasetreference.h/.cpp and datasetreference_test.cpp; register in existing logdata/unit targets. A checked immutable stamp binds job ID, dataset/request/source-manifest revisions and opaque schema ID. A checked reference has one cursor/lineage kind and bounded opaque token. Strict v1 codecs preserve all 64-bit counters as canonical decimal strings. Tokens never become physical lines, byte offsets or source identities.

1. Write Catch2 tests and build an empty API using a private QtCore-only runner and real logging source.
   Expected: assertions fail for absent behavior; baseline scalar tests remain green.
2. Implement factories/codecs and validation requiring BOTH equality with the current retained stamp and explicit bounded job-owner lookup of kind/token/issued binding. Reject stale tuples before lookup; propagate unknown/expired/owner errors, refuse inconsistent lookup outcomes, and never swallow lookup exceptions.
   Expected: real contracts pass for wide revisions, malformed types/versions, kind separation, stale metadata, forged current tuples using old tokens, expiry/release and honest errors. No standalone registry is introduced.
3. Run focused tests, strict Clang, fresh native ci_build, and all seven registered local CTest groups in a private deployed/config-isolated runtime. Refresh available code databases.
   Expected: observed green results; unavailable graph/platform/analyzer evidence recorded explicitly.
4. Update scoped progress/evidence and canonical package hashes preserving all historical records/hard edges. Obtain fresh C++ and general whole-branch reviews, address important findings and publish a separate PR.
   Expected: plan/package checks pass, no unrelated source changes, no service/full-F01 claim.

Session full-suite wrapper: build_root/f01e-validate.py ctest (private orchestration). Repository equivalent: ctest --test-dir build_root -C RelWithDebInfo --verbose with all seven registered groups in a fresh deployed/config-isolated runtime.

## Boundaries and review focus

The tagged reference prepares lineage/cursor binding; actual LineageHandle/DatasetRow/DatasetPage adapters, normalized selectors, cursor issuance/retention/limits, expiry integration, source resolution, query AST and GUI/CLI/native-MCP operations remain later owners. Lookup must be a bounded nonblocking check of retained job-owned state. Decoding/matching metadata alone never grants use or navigation. Audit exact integer/Unicode/key handling, every tuple component, kind/token reuse, release/unknown errors, inconsistent owner status and exception propagation. Do not log tokens, selectors, row bodies or arbitrary owner error messages.

## Regression fixture scope

Repeated full runs exposed existing preview protocol fixtures whose 1000 ms deadline included cold process startup. A test-only opt-in 1200 ms startup delay reproduced two failures (timeout instead of protocol rejection and failed recovery). Semantic protocol/crash/metadata fixtures now allow 5000 ms; explicit stall/cancellation deadlines remain unchanged. The local callback state is retained safely, guarded by a QPointer event loop, and the outer completion guard cancels the exact request ID. Controlled delayed startup passes 76 assertions; related preview/reference fixtures pass 408 assertions in 34 cases. No production deadline changes. Cost: semantic failures may take longer to report. Review these three test-only files alongside the new contract.

## Publication

Implementation: `1ff3af07d58b055802b286e4ef24a3f683cd67e6`.
PR: [#93](https://github.com/dm17ryk/klogg/pull/93).
Both independent C++ and general source reviews approved with no findings.
Full isolated local CTest passed 7/7; hosted checks remain pending.
