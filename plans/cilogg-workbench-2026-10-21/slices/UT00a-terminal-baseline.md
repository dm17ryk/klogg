# UT00a — Unified Terminal baseline reconciliation

Spec: [UT00](../tasks/UT00.md), [terminal contract](../11-unified-terminal.md) and [supplied specification](../../../doc/CILOGG_UNIFIED_TERMINAL_IMPLEMENTATION_TASK.md).
Base: merged master b1f820c28778f65f35401a285a9fa683970cd0f1 (PR90).

## Global constraints

Current workstream is Unified Terminal per the user's2026-11-05 clarification. This preparation does not accept MD01–MD06, alter hard DAG edges, create a terminal runtime or replace F01/D01 owners. Preserve all other tasks/history, C++17, Qt5/Qt6, deployment, plugin v1, existing commands, capture format and Actions/Responses coupling.

## Task 1: Pinned baseline, ownership map and execution reconciliation

1. Read the merged source/session/write/observer/shortcut/automation/persistence paths and current UT/F/D/U/A/E contracts.
   Expected: an exact map distinguishes raw RX from formatted capture and sensitive redaction; planned capabilities are not reported as implemented.
2. Record the current requested terminal workstream, remaining decoder/gate boundaries, existing command catalog, proposed additions, capability/privacy rules, platform matrix and dependency decision.
   Expected: no second connection/recorder/arbiter; input cannot ship before current-generation write arbitration and transport receipts; libvterm is only a candidate until license/platform/bounds proof.
3. Add the UT00a slice/evidence to the single ledger, preserving all task status/commit/PR history and dependency edges. Validate119tasks/201requirements, local links and canonical package hashes; refresh available code databases and attempt staged change detection. Obtain fresh general/C++ source-map review.
   Expected: plan validation and exact preservation checks pass; documentation-only evidence is not labeled product validation; no unrelated code edits or hidden decoder acceptance claim.
4. Publish a focused PR with the actual baseline, limitations and next UT01 increment. Keep PR91/92/93 CI supervision independent.
   Expected: pushed clean branch; no merge performed, no terminal/public command claimed available.

## Review focus

Check each implementation claim against pinned source; review raw-versus-redacted TX, partial-write limits, generation/focus/authorization boundaries, plugin/source capability distinctions, preserved historical evidence and explicit formal-gate limits. No product test coverage is inferred from plan validation.

## Publication

Implementation audit: c1feeee27c5556f67ae4c8998c43e85871edac00.
PR: [#94](https://github.com/dm17ryk/klogg/pull/94).
Independent C++/general reviews approved. One minor audit wording qualification
(when configured) is deferred; no blocking findings. Documentation only; platform
builds intentionally skipped, no terminal runtime acceptance claimed.
