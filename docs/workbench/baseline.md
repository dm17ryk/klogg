# Workbench execution baseline

Reconciled 2026-10-22 for G01. This is repository and contract evidence. No new
product build, GUI session, performance measurement or hardware test was run for
this documentation change.

## Repository and scope

- Repository: `dm17ryk/klogg`; fetched default branch `master` at
  `177f73861b3bc58ddf5c8b15021471a814673a1d`.
- Integration branch: `codex/workbench-plan-integration`, based on that commit.
- The original checkout remains on `codex/value-counts-p05`. Its modified
  `.gitnexus/lbug` and supplied untracked plan were preserved. The supplied plan
  was copied into the isolated integration worktree before reconciliation.
- The canonical execution package is now
  [plans/cilogg-workbench-2026-10-21](../../plans/cilogg-workbench-2026-10-21/README.md).
  The older [log-analysis roadmap](../../plans/log-analysis-roadmap.md) remains
  the historical feature specification; it no longer drives a separate backlog.
- Existing implementation tracking remains [issue #37](https://github.com/dm17ryk/klogg/issues/37).
  Distribution/readiness work, including PR #47, proceeds independently.

P05 was merged through [PR #45](https://github.com/dm17ryk/klogg/pull/45), merge
commit `ed2d70baa067b1d9198618ba1430094487bf26f6`. Comparing `src/` and `tests/`
between the supplied P05 branch snapshot
`01cb637be7c96f2c9e3925247510c162380736b7` and the execution baseline produces no
diff. The subsequent master commits contain release metadata, documentation and
the release-actions change from PR #46. P05 must be reused rather than rebuilt.

## Existing capability disposition

| Previous work | Current disposition | Source/test evidence | Remaining owner |
|---|---|---|---|
| P00 baseline and donor inventory | Existing fixtures and partial measurements; release budgets still open | `docs/log-analysis-baseline.md`, `docs/log-analysis-donor-map.md`, `scripts/benchmarks/generate-log-analysis-corpus.ps1` | G02, then R02 |
| P01 wrapping/selection and P02 encoding | Merged implementation; no fresh runtime claim in this reconciliation | `src/ui/src/abstractlogview.cpp`, `tests/ui/logviewperformance_test.cpp`, `src/logdata/src/encodingdetector.cpp`, `tests/unit/encodingdetector_test.cpp` | G02 measures; regression coverage remains required |
| P03a source provenance | Merged | `src/logdata/include/sourcelocation.h`, `tests/ui/logdatasource_test.cpp`, `docs/analysis-source-identity.md` | F01/F10 extend existing types |
| P03b bounded jobs | PR #41 merged | `src/logdata/include/analysisjob.h`, `tests/ui/analysisjob_test.cpp`, `docs/analysis-scan-jobs.md` | Existing service retains lifecycle ownership |
| P03c capture worker | PR #42 merged | `src/logdata/include/analysisregex.h`, `src/regex/src/analysisworker.cpp`, `tests/ui/analysisregex_test.cpp` | Reuse supervised execution |
| P03d public operations | PR #43 merged | `src/logdata/include/analysiscommands.h`, `tests/ui/analysiscommands_test.cpp`, `tests/mcp/analysis_parity.py` | G03 extends parity; feature owners add shared operations |
| P04 Regex Lab | PR #44 merged | `src/ui/include/regexlabdock.h`, `src/ui/src/mainwindowregexlab.cpp`, `tests/ui/regexlabdock_test.cpp` | Preserve behavior and source validation |
| P05 Value Counts | PR #45 merged | `src/logdata/include/valuecountjob.h`, `tests/ui/valuecounts_test.cpp`, `docs/value-counts.md` | F09/V05 adapt existing exact counts |
| P19b Ctrl selection | Merged independently of QuickFind | `docs/discontiguous-selection.md`, `tests/ui/crawlerwidget_test.cpp` | Regressions in U09; do not repeat selection redesign |
| Multiple action tabs and automatic responses | Existing implementation | `docs/multiple-action-tabs.md`, `docs/automatic-responses.md`, `src/ui/include/streamsession.h`, `tests/unit/streamsession_actions_test.cpp` | A/D tasks audit and adapt; not all future protocol features are delivered |
| Process-isolated plugins and Python/scenario/lab systems | Existing foundations, not completion of E/A/D tasks | `docs/plugin-sdk.md`, `src/ui/include/scriptsupervisor.h`, `src/ui/include/scenariorunner.h`, `src/app/labcontrollerservice.h` | E/A/D own additive capabilities |
| P06-P24, excluding delivered P19b | Planned | [Merged coverage map](../../plans/cilogg-workbench-2026-10-21/MERGE_MAP.md) | Named Workbench tasks; no scope removed |

Historical test and measurement reports remain in their original documents and
PRs. Source presence and a past passing suite do not establish a fresh packaged
or performance pass. The ten-minute expiry and memory caps are implementation
limits, not measured throughput or latency budgets.

## Current interfaces to preserve

The following signatures were checked against the current headers. Defaults are
omitted here; callers must retain the declarations in the headers.

| Owner | Existing interface | Integration boundary |
|---|---|---|
| Source provenance | `SourceSnapshot::location(LineNumber, std::optional<SourceSpan>)`, `accepts(const SourceLocation&)`, `sameRevision`, `isOpen` | Physical rows are zero-based internally; spans count raw decoded UTF-16 units. Revalidate before navigation. |
| Job service | `AnalysisJobService::start(AnalysisRequest, AnalysisProcessorFactory, QString*)`, `status(const QString&)`, `results(const QString&, const QString&, int)`, `cancel`, `release`, `cancelSource`, `location` | Sole admission, cancellation, paging, expiry and retained-output authority. No second scheduler for charts. |
| Exact counts | `ValueCountJob::consume(const QVariantList&, const std::atomic_bool&)`, `results() const`, `status() const` | Preserve occurrence denominator, missing/empty distinction, decimal counts, deterministic ties and exact stop-on-cap behavior. |
| Shared analysis adapter | `executeAnalysisOperation(AnalysisJobService&, const QString&, const QVariantMap&, std::optional<AnalysisRequest>, QString)` | Widgets supply frozen source/scope; public requests pass through Commander. |
| Capture backend | `makeRegexProcessor(QVariantMap, QString, QStringList)` | Reuse the supervised Qt capture worker. Executable overrides remain internal test hooks. |
| Regex Lab | `RegexLabDock::state()`, `setDefinition(const QVariantMap&)` | Dock drafts stay runtime-only; preserve request-revision guards. |
| Communication | `StreamSession::sendBytes(const QByteArray&, bool)`, `setActionsEnabled(bool)`, `setAutoResponsesEnabled(bool)` | Keep existing action/response generations and connection ownership. |
| Process/scenario adapters | `ScriptSupervisor::setCommanderExecutor(...)`, `ScenarioRunner::setCommanderExecutor(...)`, `publishEvent(const QVariantMap&)` | Extend current supervised routes; do not invent another action or event dispatcher. |

The native MCP implementation is `src/app/mcp/{mcp.h,protocol.cpp,runtime.cpp}`.
Discovery and execution remain `cilogg_commands` and `cilogg_execute`. The CLI is
`cilogg command --action ACTION --json-file REQUEST.json`. Python consumes this
same command route. Every public feature must include GUI, CLI and MCP operations.

F01's typed values, event/dataset contracts and V01's panel/rendering contracts are
**proposed additions**. They are not present APIs. F07/F09/F11/F14 introduce later
typed-query/aggregate extensions; they must not become prerequisites of native
capture charts. Ownership and exact proposed signatures remain in the data and
visualization plans and must be verified again before dependent code changes.

## Toolchain and validation boundaries

- C++17 and existing Qt5/Qt6 core compatibility remain requirements.
- Local usable SDK: `C:\Essence_SC\qt6.10.1`; `bin\qmake.exe` exists. The configured
  Qt 6.12 directory has no `bin\qmake.exe`. The user authorized Qt 6.10.1 locally
  and Qt 6.12 in CI; do not silently change the local SDK to an incomplete folder.
- Build only in the worktree's `build_root`; use the repository's clean Windows
  build/test scripts. The actual CMake switch is `KLOGG_BUILD_TESTS`.
- Qt 6.12 platform expansion remains [PR #47](https://github.com/dm17ryk/klogg/pull/47),
  head `e918011280fa53a024f562bb57bb0e8d50a37adf` at reconciliation. It is not part
  of this baseline. Do not copy its code or claim it merged. Preserve older macOS
  packages, defer Ubuntu 26.04, and label Rocky/Alma validation RHEL-compatible.
- CodeGraph was used on the indexed primary checkout. It flagged pending entries;
  relevant current headers were read directly. The P05-to-baseline source/test
  comparison above establishes that the inspected implementation is unchanged.
- GitNexus reports the primary P05 index up to date at `01cb637`. The isolated
  integration worktree reports `Repository not indexed`. No function is edited by
  this integration; no symbol impact or fresh worktree graph pass is claimed.
- Code-review-graph was refreshed in the integration worktree: 453 files parsed,
  6,904 nodes and 64,430 edges, with no reported errors. Focused review context
  reports low impact and zero impacted nodes. GitNexus change detection cannot
  resolve this unindexed worktree; staged Git path checks establish the actual
  documentation-only scope instead. No GitNexus pass is claimed.
- Do not initialize CodeGraph in an unindexed worktree. Before product changes,
  refresh available relevant indexes and run upstream impact on each edited
  symbol; before committing run change-scope detection and report limitations.

## G01 result and next work

G01 reconciles implementation credit, task ownership, the current source and the
toolchain. It does not complete the 173 feature requirements or certify current
performance/hardware/platform behavior. The ledger records this distinction.

Next runnable tasks are **G02** (fixtures and measured budgets) and **G03** (shared
parity harness). Prefer G02 first, then G03; both depend only on G01. Next feature
path is **F01 → V01 → V02**, preserving early native P06 charts. No chart product
code was started during this integration.
