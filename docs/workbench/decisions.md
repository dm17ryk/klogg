# Workbench integration decisions

Accepted 2026-10-22 following the user's instruction to merge the existing
log-analysis roadmap with the supplied `START_IN_CODEX.md` execution package.

## D01 — One execution ledger

Use [the Workbench package](../../plans/cilogg-workbench-2026-10-21/README.md) and
its `PROGRESS.json`/`TASK_INDEX.json` for execution. Keep the prior P00-P24 roadmap
and P05 specification as linked feature history and regression contracts. Install
the package under `plans/cilogg-workbench-2026-10-21`, matching the plan location
already advertised by the repository README. Preserve the user's source package
under `doc/` in the original checkout; it is an import source, not a second ledger.

## D02 — Reuse completed foundations

Credit P00's existing baseline work, P01/P02, P03a-d, P04, merged P05 and P19b with
their actual evidence. P00's unmeasured gates remain open in G02/R02. Reuse existing
plugin, action, automatic-response, Python, scenario and lab foundations. Their
presence does not complete the broader A/D/E requirements.

## D03 — Early charts stay early

Execute G02/G03, then F01's Qt Core contracts, V01 and V02. Preserve native numeric
capture charts, physical-line axes, counts/relative-frequency buckets, min/max
envelopes, bounded data tables and validated source navigation. V02 must deliver
its GUI/CLI/MCP surface. Full structured parsing, typed time/query execution,
multi-panel live reuse and optional WebEngine/ECharts remain later capabilities.
No timestamp rate is inferred from line numbers or ingestion speed. Current
execution priority is updated by D09; this decision preserves early-chart
independence rather than overriding the later user-approved schedule.

## D04 — Ownership and compatibility

`AnalysisJobService` retains scheduling, source leases, deadlines, backpressure,
retention, pagination and cancellation. F owns typed values, events, queries and
datasets; V owns panel bindings, rendering and interactions; U owns shared shell,
query builder, navigation and persistence; A adapts current actions/scenarios;
D owns binary transport/recording semantics; E owns connectors, SDKs and packs;
I owns investigation analysis; R owns release evidence. Shared Commander,
MainWindow, CMake, schemas and resources are integrated serially.

Use the exact existing signatures recorded in the baseline and the proposed F/V
contracts in the package. No rename or duplicate service is introduced here.
Preserve C++17, Qt5/Qt6 core support, stored IDs and plugin protocol compatibility.
All public features include menus/toolbars as appropriate plus discoverable
Commander/CLI/MCP configuration, state and operations. Python uses that same route.

## D05 — Actions and automatic responses remain coupled

Disabling Actions disables automatic responses. Enabling responses enables
Actions. Disabling responses leaves Actions unchanged; enabling Actions leaves
responses unchanged. New action-enabled tabs start with responses enabled, and
new tabs without Actions start with both disabled. Preserve explicit restored
response disables and the existing global response gate. A/D tasks must maintain
these semantics while extending protocol behavior.

## D06 — Platform work is independent

Keep PR #47 separate and based on its own CI evidence. Qt 6.12 is selected only
for supported/validated targets; retain legacy macOS compatibility, postpone
Ubuntu 26.04, and describe Rocky/Alma packages as RHEL-compatible. Local validation
uses the approved installed Qt 6.10.1 SDK. Core Qt compatibility is not raised by
the packaging matrix. No release workflow or merge is triggered by this plan.

## D07 — Evidence and rollback

G01 is documentation/source reconciliation. G02 supplies numerical benchmarks;
G03 supplies the reusable parity harness. Historical successes, source inspection,
new local runs, hosted checks, skipped checks and hardware tests remain distinct.
Task completion cannot be inferred from a plan checkbox. This integration can be
reverted without changing product code, settings, sources, captures or protocols.

## D08 — Message decoders reuse the real Wireshark engine

The revision-2 addendum is integrated as MD01–MD08 and DEC01–DEC14 in the canonical
package. Use supervised TShark/native/Lua dissectors, Exported-PDU application
records and explicit transformation sidecars. Do not implement a second Proto,
Tvb or TreeItem runtime or duplicate EHCP/EHCI semantic parsers. Preserve legacy
JSON previews, plugin v1, bounded analysis jobs and GUI/CLI/MCP/Python parity.

Owner references do not add reverse DAG edges: MD01 proves actual script/PDU
dispatch first; G02/G03 remain prerequisites, while V02's early native charts
stay independent. D07 live adapters, D17 Protocol Lab and R01 decoder-enabled
release acceptance are capability-conditional integration gates. Missing Lua
sources/complete captures and all unrun runtime/platform evidence stay explicit.
The private source addendum is recorded by name/hash; account-bearing payloads
are not copied to public plans, fixtures or GitHub issues.

## D09 — Unified terminal follows TShark decoder core

Accepted scheduling instruction 2026-11-04: finish TShark decoder core MD01–MD06
and its prerequisite acceptance, implement unified terminal UT00–UT13, then resume
remaining W1 and later waves. Preserve the user's earlier MD07/MD08 deferral.
UT00 depends on MD06; live terminal attachment retains F01/D01 hard gates. R04
also requires UT13. Existing tasks, requirements and evidence remain credited;
none is marked complete by scheduling. See the
[terminal contract](../../plans/cilogg-workbench-2026-10-21/11-unified-terminal.md).

The [supplied task](../../doc/CILOGG_UNIFIED_TERMINAL_IMPLEMENTATION_TASK.md) defines
direct typing in the existing session viewport for serial consoles, local shells
and SSH. Reuse StreamSession ownership, F01 provenance, D01 transports/write
receipts and existing Commander/CLI/native-MCP/Python dispatch. Each exposed
increment includes its public surfaces; UT11 is comprehensive parity acceptance.
Manual arming rejects conflicting automated writes from the first input prototype;
UT09 does not defer that baseline protection or alter saved Actions flags.

The source's embedded implementation prompt is reference material for future
execution. Technical selections, names and budgets remain proposals to reconcile
in UT00/UT01 and backend cards. This documentation change implements no terminal,
executes no shell/device command and claims no runtime/platform/hardware evidence.

## D10 — Terminal preparation is the current workstream

On 2026-11-05 the user clarified that Unified Terminal follows the merged TShark Preview work (PR #90), rather than unrelated F01 expansion. Prepare the [UT00a baseline](ut00-terminal-baseline.md) now. PROGRESS.json.executionPolicy.currentPreparation identifies this scoped work. Preserve the full MD01–MD06 acceptance boundary and the existing hard DAG: UT00 still depends on MD06 and live UT02 on F01/D01. A merged preview qualification is not full decoder acceptance. Subsequent terminal increments must include their necessary existing-owner foundations and the shared GUI/CLI/native-MCP/Python surface. No new connection, recorder or authorization path is approved by this source audit.

## D11 — Interactive plugin views (E13) ahead of the wave order

On 2026-11-11 the user asked for a generic plugin-engine extension: interactive plugin views in every window kind CILogg uses (docks on any side or floating, top-level windows, modal or modeless dialogs, and message boxes with an auto-close timeout), plus `CILOGG_EXECUTABLE` / `CILOGG_INSTANCE_ID` for plugin processes, a `cancelling` grace period for plugin jobs, and `pause_comm --wait`. It is delivered as a branch and PR in this repository. A proprietary plugin outside this repository is the first consumer. The work is recorded as task [E13](../../plans/cilogg-workbench-2026-10-21/tasks/E13.md), and its first slice E13a is authorized ahead of G02/G03 completion and the current terminal workstream. It does not change any other task's order or hard edges.

E13 coordinates with E01: views announce themselves only through `"host_features": ["views"]` in `initialize`, which E01's negotiated capabilities absorb later without changing the view messages. Plugins that declare no views keep byte-for-byte protocol-v1 behaviour. See the [plugin views design](../plugin-views.md).

## D12 — Updater fixes and workspace requests of 2026-11-13 ahead of the wave order

On 2026-11-13, after installing release `continuous-26.04.2.1203` through the in-app updater, the user reported two updater bugs and asked for four workspace features. They are recorded as four tasks:

- [R05](../../plans/cilogg-workbench-2026-10-21/tasks/R05.md): the exit-time install hand-off (a shell error box naming the downloaded installer, repeated installs) and the startup error when the update server is unreachable.
- [U14](../../plans/cilogg-workbench-2026-10-21/tasks/U14.md): the filtered view shows which filter matched each line, and its results can be cleared.
- [U15](../../plans/cilogg-workbench-2026-10-21/tasks/U15.md): one source picker (a tab, a file, the focused tab, or all opened files) for Value Counts, Regex Lab and every other tool that takes a tab.
- [U16](../../plans/cilogg-workbench-2026-10-21/tasks/U16.md): split the main window into panes, each with its own file tabs and filtered views.

R05, U14 and U15 are authorized ahead of G02/G03 completion and the current terminal workstream, like E13a under D11, and each is delivered as its own branch and PR. U16 stays design-only until the user approves the [split-pane design](../split-panes.md) (approved under decision D13). U16 is the first slice of the pane layout that U03 plans, and it keeps U03/U08 compatible by addressing panes, tabs and sources through stable IDs.

So that the authorization is selectable from the ledger, the four tasks have G01 (completed) as their only hard dependency; G02/G03 are not hard edges of them, and they are listed in `PROGRESS.json.runnableTasks`. Two existing tasks gain hard edges: U03 depends on U16 (U03's workspace snapshot embeds the pane layout, so the layout comes first), and R04, the full-program completion gate, depends on R05, U14, U15 and U16. The stored topological order in `TASK_INDEX.json` is regenerated accordingly. Requirements UX19–UX21 and QUAL11 in the [feature coverage](../../plans/cilogg-workbench-2026-10-21/FEATURE_COVERAGE.md) record the four outcomes. No other task's order, hard edges or acceptance changes.

## D13 — User answers of 2026-11-13 for U14, U15 and U16

On 2026-11-13 the user answered the open points of the D12 tasks:

- **U14:** the recorded interpretation is confirmed. The "Matched by" column names the predefined filter(s) that matched; a single pattern shows the matched text. "Clear results" empties the view while the search keeps running, new matches appear, and searching again restores all results. Clear removes search matches only: marked lines stay visible because marks are user data.
- **U15:** "don't limit to 8, all opened mean all". "All open files" runs on every open tab of every window. The tool stages the work through `AnalysisJobService` and keeps its limits (2 running, 8 queued, 26 retained) instead of capping the number of sources. Search, filters, follow, Quick Find, encoding, save and file operations stay on the current tab by design; searching several tabs is U15b's explicit operation.
- **U16:** the [split-pane design](../split-panes.md) is approved. Closing a pane with tabs asks whether to close all its tabs or move them to the neighbouring pane (or cancel); "focused tab" is the current tab of the active pane; up to 8 panes per window with `Ctrl+Alt+Right` / `Ctrl+Alt+Down`; tabs and files per pane are not limited. Implementation proceeds in slices U16a–U16c.

The task cards [U14](../../plans/cilogg-workbench-2026-10-21/tasks/U14.md), [U15](../../plans/cilogg-workbench-2026-10-21/tasks/U15.md) and [U16](../../plans/cilogg-workbench-2026-10-21/tasks/U16.md) record these answers. They change no hard edge beyond those recorded in D12.

## D14 — Editable plugin table cells and toolbar keys

On 2026-11-13 the user approved extending the plugin views SDK (E13) so that a plugin can make table cells editable, offer a `multichoice` check-dropdown field, and choose toolbar/menu presence and an icon for its actions and views. This is recorded as task [E14](../../plans/cilogg-workbench-2026-10-21/tasks/E14.md) and is the host-side groundwork for editing CPF files in a CILogg plugin.

Edits are never applied by the host on its own: a cell edit is sent to the plugin as an `edited` view event (`element_id`, `row_id`, `column_id`, `value`, `previous`, `inputs`) and is confirmed only when the plugin answers with a patch; until then the cell shows a pending state, and it reverts if the plugin stops. The defaults keep existing plugins unchanged: columns are read-only unless `editable` is set, and the new manifest keys default to the E13 behaviour (action `toolbar` and `menu` true, view `toolbar` false, view `menu` true, no `icon`). The protocol version stays 1. The host feature `view_edit`, announced in `initialize`, gates the whole feature, so a plugin can detect hosts without it and fall back to read-only tables. See the [plugin views design](../plugin-views.md).

## D15 — Debuggable problem reports (R06) ahead of the wave order

On 2026-11-15 the user reported that the exception dialog of release `continuous-26.04.2.1203` produced an issue with only `CheckFileChangesOperation failed: bad allocation` and the versions, during automatic actions nobody watched, and asked for "crash report that can be debugged and fixed, better with core dump and so on, and if possible some scenario how we got to this exception, how to reproduce it". The approved design is recorded as task [R06](../../plans/cilogg-workbench-2026-10-21/tasks/R06.md), described for users in [diagnostic reports](../diagnostics.md).

R06 depends only on the completed G01 and is authorized ahead of G02/G03 and the current terminal workstream, like R05 under D12; it is listed in `PROGRESS.json.runnableTasks`. R04, the full-program completion gate, gains R06 as a hard edge. Requirement QUAL12 in the [feature coverage](../../plans/cilogg-workbench-2026-10-21/FEATURE_COVERAGE.md) records the outcome. It is delivered in two slices: R06a (activity journal, memory status, report bundle, non-blocking notice, `list_reports` / `create_report`) and R06b (crash minidumps, throw-site stacks, published debug symbols).

Reports stay local. The public GitHub issue carries only the summary (operation, error, file name without its folder, memory numbers, versions, report id); the user reviews the zip and attaches it. The journal never records payload values (action bytes, parameter values, plugin and analysis arguments), only their key names. The always-on log tail keeps warnings and errors only, so ordinary info logging costs nothing when logging is off. No other task's order, hard edges or acceptance changes.

## D16 — Plugin repositories and configuration packs (E15, E16), proposed

On 2026-11-17 the user asked whether CILogg can install and update plugins from a private Azure DevOps repository, because an internal tool plugin must not be published publicly, and whether message previews (JSON definitions, Lua and Python decoders) can be delivered and updated the same way. Licensed protocol Lua sources cannot be public either (the MD01 input gate). The proposed design is [plugin repositories and configuration packs](../plugin-repositories.md), recorded as two tasks:

- [E15](../../plans/cilogg-workbench-2026-10-21/tasks/E15.md): several plugin catalogs (`plugins.json`, schema 1) managed in the Plugins window and through Commander, CLI, native MCP and Python; sign-in none or Azure DevOps, through the Git Credential Manager (`git credential fill`) with a personal access token in the operating system credential store as the fallback; one merged list with each entry's source; updates only from the install source; a silent update check at startup and once a day, with installing left to the user unless auto-update is on for that repository. Slices E15a (repositories and merged catalog), E15b (Azure DevOps sign-in) and E15c (update checks).
- [E16](../../plans/cilogg-workbench-2026-10-21/tasks/E16.md): configuration packs, a plugin kind that carries preview definitions, Lua for TShark, Python decoders, actions with automatic responses, highlighters and predefined filters, versioned and updated from the same repositories; pack items are read-only (copy to edit), pack automatic responses start switched off, and the pack's Lua is a trusted TShark script while the pack is enabled. Slices E16a (previews and Lua), E16b (Python decoders, activated with MD03's decoder contract) and E16c (actions, highlighters and filters).

Later on 2026-11-17 the user removed the private working configuration from this public repository (previews, actions, highlighters, predefined filters and related task material) and asked to deliver it, with the TShark Lua, as a plugin from the private Azure DevOps repository. That is why E16 covers the whole configuration, not only previews; the content itself is kept only in the private repository.

Credentials are never written to settings, exports, logs, the activity journal or problem reports, are sent only to the repository's own host and organization, and are never forwarded across redirects. This repository stays generic: an organization keeps its private plugins, packs and the pipeline that builds its catalog in its own repository.

The design is proposed, not approved: implementation starts only after the user approves it and answers its open decisions. E15 depends only on the completed G01, so the approval can authorize it ahead of G02/G03 like E13a and R06; until then it is not in `PROGRESS.json.runnableTasks`. E16 depends on E15. Two existing tasks gain hard edges: E09 depends on E16 (the later general packs extend this pack format instead of duplicating it), and R04, the full-program completion gate, depends on E15 and E16. Requirements ECO20–ECO22 in the [feature coverage](../../plans/cilogg-workbench-2026-10-21/FEATURE_COVERAGE.md) record the outcomes. MD03 activates the packs' Python decoders as a coordination note, not a hard edge. No other task's order, hard edges or acceptance changes.
