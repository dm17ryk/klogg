# W0 acceptance audit — 2026-10-31

Audit baseline: merged master `567a47741e04e18fdfc655d5bc0242ff4fd1288f`.
This is an evidence/plan update, with no product or harness implementation change.
The [machine-readable audit](w0-acceptance-audit.json) records exact identities,
artifact hashes, current checks and the dispositions below.

**G03's reusable parity-harness acceptance is satisfied. G02 and W0 remain open.**
This does not declare the whole product or any platform release accepted.

## Current W0 blockers

1. Integrate [G02h / PR #77](https://github.com/dm17ryk/klogg/pull/77) and record its
   required checks. Its viewer baseline exists on `fe8e0f4b`, not this master.
2. Record a distinct time-to-first-useful-result observation using the existing
   bounded result API. Current analysis and viewer reports measure submission,
   indexing or completed-result observation. They must not be relabelled as the
   first available row/page. The next G02 slice owns this measurement and its
   reproducible command, fixture, timing boundary and failure test.
3. Resolve the fresh sustained-profile qualification failure. Repetition one
   produced 11 released samples and passed its measured guards; repetition two
   produced only 9, below the required 10. The run failed with
   `sustained_points_missing_or_excessive` before repetition three. This is
   insufficient evidence, not an observed memory-budget breach. Preserve the
   failure and qualify a reproducible profile without weakening its guards.

No decoder runtime acceptance starts before these W0 gates pass. Locating its
inputs and installed tools is preparatory inventory, not a decoder proof.

## G02 acceptance matrix

| Criterion | Disposition | Evidence / remaining boundary |
| --- | --- | --- |
| Deterministic representative fixtures and exact oracles | Satisfied | Nineteen committed fixtures cover text/Unicode/BOM/long lines, high cardinality, dense matches, JSON/logfmt/CSV, four stack languages, web/build/test/ETL/frame data, two clock domains, bytes, malformed data and an incomplete tail. Generator/manifest checks reproduce the same bytes. |
| Current Windows open/index/search/scroll, memory, cancellation and append baseline | Partial | G02a–G02g provide frozen scanning, append, GUI/service and memory evidence. PR #77 adds the normal-viewer/idle-dock comparison and awaits integration. A separate first-useful-result timing remains missing. |
| Numeric budgets with explicit workload/build/hardware scope | Satisfied for the measured profiles | Recorded native GUI-event/CPU-paint p95 target is 100 ms and local service cancellation target is 1,000 ms. Host/tree memory guards and their sampled/commit/high-water scopes remain distinct. A public multi-process cancellation bound is not a service CPU bound. |
| Incremental panels/live scenarios and renderer comparisons | Partial; future capabilities conditional | Native panels and append coexistence have runnable scenarios. The current sustained refresh failed sample qualification and remains open. Rich-renderer startup/memory/disabled comparisons belong to V03/R02 when available. |
| Reproducibility, process cold/warm labels and failure guards | Satisfied | Seed/hash identities, repeated runs, negative timing/memory guards and retained failure reports are recorded. Process state is separate from the uncontrolled OS cache. |

The current sustained-memory refresh uses the approved Qt 6.10.1 Qt-only
deployment whose product tree equals this master. Its outcome is recorded in
the JSON audit. Earlier G02f measurements remain historical evidence from their
original source epoch rather than being silently reattributed to current code.

## G03 acceptance matrix

| Criterion | Disposition | Evidence |
| --- | --- | --- |
| Negative harness tests and observed RED/GREEN history | Satisfied | G03a/b records unknown/missing/empty cases, child exit/timeout, precision and semantic mismatch tests; later source, navigation, payload and restart regressions extend that history. |
| Isolation, literal argv, bounded waits, cancel/release/paging and exact JSON | Satisfied | Existing `workbench_parity.py`, `analysis_parity.py`, runtime/owner helpers and deterministic self-tests remain the common command route. No second MCP catalog was added. |
| P03/P04/P05 plus file navigation through public surfaces | Satisfied | All four registered G03 cases pass on current-master Windows hyperscan and macOS vectorscan builds, and Linux Qt-only; the current local Qt-only run also passes. |
| Accelerated and Qt-only builds, Python/native MCP interoperability | Satisfied | Hosted build metadata independently verifies backend flags; both Windows MCP SDK profiles 1.30.0 and 2.3.0 pass. Six current-master harness jobs pass across Windows/Linux/macOS and Python 3.9/3.12. |
| Complete relevant suites and separate GUI presence/usability evidence | Satisfied for harness acceptance | Local complete CTest and Python suites are recorded separately from current-master hosted product jobs. Native Windows dock/MainWindow/restart artifacts pass. Headless controls, Qt event injection and screenshots do not prove compositor, physical-input or every-platform usability. |

Current-master representative product artifacts come from
[run 41146843423](https://github.com/dm17ryk/klogg/actions/runs/41146843423).
Its overall matrix was still running at the audit snapshot; individual completed
jobs are recorded separately. The earlier source-equivalent
[run 41049268421](https://github.com/dm17ryk/klogg/actions/runs/41049268421)
completed all 25 jobs. Its tested synthetic merge `1fda60b1` has parents
`493124be` and `696857c7`; its product tree equals current master. Those two
source epochs and their binary hashes are not conflated.

## Remaining release and capability gates

These remain owned work; none is reported as passed by this audit:

- Full current CI matrix and PR #77's remaining checks.
- Native Linux/macOS performance/working-set acceptance and broader platform UI
  coverage. Headless parity success supplies functional evidence only.
- Controlled OS-cache comparisons, calibrated idle-dock overhead, true compositor
  presentation and longer/hardware-specific stress profiles under G02/R02.
- Rich-renderer startup, memory and enabled/disabled comparisons under V03/R02
  once that optional renderer exists.
- Full-lifetime/simultaneous tree RSS claims beyond the explicitly measured
  sampled and through-observation scopes.

## MD01 input preflight

The user supplied local Lua-plugin and capture directories. The actual
`essence_ehcp.lua` and `essence_ehci.lua` are present, with 7,831 and 867 lines.
Four local PCAPNG files were located. Only names/lengths/hashes and installed
tool versions are inventoried; no packet bodies or Lua source are copied into
tracked files. Private path mappings remain in ignored local artifacts.

The actual installed TShark and text2pcap binaries report **4.6.9**, despite an
older 4.6.8 uninstall-registry entry. TShark reports Lua **5.4.6** support.
Tool/script/capture identities must be rechecked before runtime proof. Licensing,
sanitization, complete/partial field-tree fidelity, Exported-PDU dispatch and the
nested SRING oracle remain MD01 work. An installed tool or matching line count
does not establish any of those capabilities.

[The log and legacy-preview preflight](md01-input-preview-preflight.md) records
the additional read-only investigation requested by the user. Private file/line
locations remain in ignored local artifacts. NUL-containing logs require text
search mode; literal `STRING` and modem `SRING` are separate findings.

The approved order remains W0, then MD01 and the decoder-core prerequisite
sequence in `PROGRESS.json.executionPolicy`, then remaining W1 and later waves.
MD07/MD08 integration remains deferred. Revert this audit's ledger entries and
documents to roll back its bookkeeping; runtime behavior does not depend on it.
