# F01d — Checked dataset schema metadata

Spec: [F01](../tasks/F01.md), [shared contracts](../02-data.md).
Base: merged master 080b504c184796ad3942383149d0385503dd9643. Independent of pending PR90/91.

## Global constraints

C++17, Qt5/Qt6 and existing source/search/analysis/public operations remain unchanged. Compile in klogg_logdata without QWidget or a new service/scheduler. Local Qt6.10.1; hosted Qt6.12 and Qt5 qualification stay separate. Full F01/W0/MD01 remain open.

## Task 1: Checked dataset schemas and exact metadata codec

Create datasetschema.h/.cpp, tests/unit/datasetschema_test.cpp, and contract/evidence documentation. Register in current logdata/unit targets. Support table shape version1, all known FieldType declarations, stable explicit column IDs, units, semantic roles, clock domains, nullable/repeated flags and a deterministic framed UTF8 schema fingerprint. Preserve column order; reject duplicate field IDs, unsupported tags/roles/shapes/versions, malformed Unicode/types and resource limits. Timestamp/time-role declarations require explicit clock metadata; time role uses Timestamp, never an implicit floating-point clock.

1. Write real Catch2 tests and compile an empty API shell using a small private QtCore-only runner with actual production source/logging.
   Expected: baseline exact-value cases pass; schema assertions fail for absent implementation.
2. Implement checked immutable metadata, strict schema/shape-version1 codec and bounded diagnostics. Limits:64 output columns; fieldId128/name256/unit64/clockDomain256 UTF16 units;32KiB aggregate UTF8 metadata. Stop with an explicit error, never drop columns. Empty optional strings normalize for Qt5/Qt6 JSON round trips.
   Expected: focused tests pass, including a fixed independently computed fingerprint, Unicode, default empty labels, all roles/types, clocks, duplicate IDs and aggregate limits.
3. Run focused tests, strict Clang conversion checks, fresh native ci_build and all seven registered CTest groups in a private portable runtime. Refresh available graph databases.
   Expected: green observed results; unavailable graph/platform/analyzer checks recorded honestly.
4. Preserve historical plan records and hard edges; update scoped progress/evidence/canonical package hashes. Obtain fresh C++ and general whole-branch review, fix important findings and publish a separate PR against master.
   Expected: plan/package checks pass, no unrelated changes, no consumer/full-F01 claim.

Session full-suite wrapper: build_root/f01d-validate.py ctest (private orchestration). Repository equivalent: ctest --test-dir build_root -C RelWithDebInfo --verbose with a fresh deployed/config-isolated runtime.

## Boundaries and review focus

The schema describes types; it does not enable reserved FieldValue payloads. Future consumers must reject unsupported payload capabilities. Dataset pages/revision cursors, full dataset JSON Schema, query AST, resolvers and GUI/CLI/native-MCP consumers remain later slices. Review strict wire shapes, Unicode/size bounds, identifier uniqueness and ordering, deterministic hash framing, null QString normalization, explicit clock identity and claims versus actual capability. No SourceLocation or file navigation is created from a column.
