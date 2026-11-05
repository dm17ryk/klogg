# F01c — Checked event identities and discriminated origins

Spec: [F01](../tasks/F01.md), [shared data contracts](../02-data.md).
Parent F01, W0, MD01 and generic decoder integration remain incomplete. This additive internal contract is independent of pending PR #90.

## Global constraints

Preserve C++17, Qt5/Qt6, physical source identity/lifetime rules, P03–P05 and current public operations. No file I/O, reader ownership, resolver registration, UI or new public command in this slice. Future consumers must use versioned contracts across GUI/CLI/native MCP. Local validation uses Qt 6.10.1; hosted Qt 6.12 remains pending until observed.

## Task 1: Event origins and exact versioned codec

Create eventrecord.h/.cpp and datasetcontracts_test.cpp, register in existing logdata/unit targets. Add checked physical/inline/deferred-file/managed-byte origins, stable domain-separated identity derived from origin/range and format/assembly/occurrence policy. Physical events validate both endpoints, preserve provisional-revision rules and weak lifetime; closed/replaced refs remain inspectable. Deferred references bind to immutable manifest verification metadata without claiming filesystem verification or open-source identity. Managed references have segment-relative bytes, no text anchors. Decode historical physical metadata without granting live navigation.

1. Write focused Catch2 tests; run before implementation.
   Expected: assertions fail because event contracts are absent.
2. Implement checked factories, structural validation, source acceptance and a strict schema-version-1 QVariantMap/JSON codec. Serialize wide counters/offsets as canonical decimal strings; bound identifiers and verification metadata; reject malformed shapes, ranges, unknown versions and multiple origins. Log decision reasons and IDs, never bodies/paths.
   Expected: focused cases pass, including append/provisional tail, closure/replacement, exact u64 values, identity stability/separation, inline row ranges, manifest mismatch, missing verification and managed anchors.
3. Refresh graph databases where available; run focused and full registered local CTest from an isolated portable runtime.
   Expected: native build and all registered tests pass; unavailable graph/backend/platform checks are explicitly recorded, not claimed green.
4. Update docs, scoped progress and package hashes without altering task dependencies or claiming full F01 completion. Obtain fresh C++ and general whole-branch review; address important findings. Commit and publish an independent PR against master.
   Expected: package validator/checks pass and PR contains only this slice.

Session completion command: build_root/f01c-validate.py ctest (private orchestration wrapper, not a shipped script). Repository equivalent: ctest --test-dir build_root -C RelWithDebInfo --verbose, using all seven registered tests in a fresh deployed/config-isolated runtime.

## Review Focus

Audit hostile wire shapes and integer precision, identity collisions across origin/policy changes, snapshot metadata versus live authority, provisional first/last handling, weak lifetime closure and manifest verification binding. Verify no synthetic source UUID, physical row or UTF-16 anchor is assigned to inline/deferred/managed origins. No resolver or public transport completion is claimed.
