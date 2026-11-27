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

- **U14:** the recorded interpretation is confirmed. The "Matched by" column names the predefined filter(s) that matched; a single pattern shows the matched text. (Changed on 2026-11-26 by [#184](https://github.com/dm17ryk/klogg/issues/184): the column shows the matched text in every case, and the cell's tooltip names the filters and alternatives.) "Clear results" empties the view while the search keeps running, new matches appear, and searching again restores all results. Clear removes search matches only: marked lines stay visible because marks are user data.
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

## D16 — Plugin repositories and configuration packs (E15, E16), accepted

On 2026-11-17 the user asked whether CILogg can install and update plugins from a private Azure DevOps repository, because an internal tool plugin must not be published publicly, and whether message previews (JSON definitions, Lua and Python decoders) can be delivered and updated the same way. Licensed protocol Lua sources cannot be public either (the MD01 input gate). The design, accepted on 2026-11-17, is [plugin repositories and configuration packs](../plugin-repositories.md), recorded as two tasks:

- [E15](../../plans/cilogg-workbench-2026-10-21/tasks/E15.md): several plugin catalogs (`plugins.json`, schema 1) managed in the Plugins window and through Commander, CLI, native MCP and Python; sign-in none or Azure DevOps, through the Git Credential Manager (`git credential fill`) with a personal access token in the operating system credential store as the fallback; one merged list with each entry's source; updates only from the install source; a silent update check at startup and once a day, with installing left to the user unless auto-update is on for that repository. Slices E15a (repositories and merged catalog), E15b (Azure DevOps sign-in) and E15c (update checks).
- [E16](../../plans/cilogg-workbench-2026-10-21/tasks/E16.md): configuration packs, a plugin kind that carries preview definitions, Lua for TShark, Python decoders, actions with automatic responses, highlighters and predefined filters, versioned and updated from the same repositories; pack items can be edited in place and an update merges the local edits (section 4.6 of the design), pack automatic responses start switched off, and the pack's Lua is a trusted TShark script while the pack is enabled. Slices E16a (previews and Lua), E16b (Python decoders, activated with MD03's decoder contract; since the plan change of 2026-11-24, with the narrower MD03b Python preview backend) and E16c (actions, highlighters and filters).

Later on 2026-11-17 the user removed the private working configuration from this public repository (previews, actions, highlighters, predefined filters and related task material) and asked to deliver it, with the TShark Lua, as a plugin from the private Azure DevOps repository. That is why E16 covers the whole configuration, not only previews; the content itself is kept only in the private repository.

The user also asked (2026-11-17) for a table of repositories with add, remove and ordering buttons, so plugins can be loaded from several repositories, and for an in-app description of how to pack plugins and how to create and use a repository of your own. E15a therefore includes the ordered Repositories table (the order is a priority: Browse groups by it and a new install of a plugin offered twice takes the higher repository, while installed plugins stay pinned to their source), the in-app guide in `DOCUMENTATION.md` and plain status texts such as "No plugins in this repository" for the empty public catalog.

Later the same day the user asked for the same repository handling for highlighters, actions, responses, previewers and filters, so all of them can be shared through public and private repositories, and pointed out that, unlike code plugins, these text files are edited locally, so an update must merge local changes. The design therefore has one Plugins and Packs window (Installed, Available with a Kind filter, Repositories) for plugins and packs, lists a single-kind pack as that kind, lets pack items be edited in place with the installed version kept as the merge base and a three-way merge (with a dialog for conflicts and strategies for automation) on update, and adds Export as Pack. The window is in E15a; the merge in E16a (previews and Lua or Python) and E16c (actions, responses, highlighters, filters); Export as Pack in the new slice E16d.

The user then decided the editing model, "edit in place with merge", and approved the proposed merge UI: an item merge window for structured items (a tree of pack, item and field with Base, Mine, Theirs and an editable Result), a three-pane text merge for Lua and Python, and an optional external merge tool configured like git mergetool. He also asked to use Git internally for automatic merging and to let users post their additions and changes back. The design proposes a local Git store per pack (an `upstream` branch with each installed version, a `local` branch with the user's edits, a canonical one-item-per-file form and a CILogg merge driver; libgit2 built local-only) in E16a and E16c, and Contribute: View Changes and Export Changes in E16d, and Propose Changes, a pull request through the GitHub or Azure DevOps REST API after an explicit confirmation, in the new slice E16e. The user then accepted the whole design: the Git store with libgit2 built without network support (decisions 8 and 9) and Propose Changes (decision 10) are decided, with the other decisions listed in section 10 of the design.

Credentials are never written to settings, exports, logs, the activity journal or problem reports, are sent only to the repository's own host and organization, and are never forwarded across redirects. This repository stays generic: an organization keeps its private plugins, packs and the pipeline that builds its catalog in its own repository.

The user accepted the design on 2026-11-17 and approved every decision in its section 10 ("yes, approve all 11 recommended"). E15 depends only on the completed G01 and is authorized ahead of G02/G03, like E13a and R06, so it is listed in `PROGRESS.json.runnableTasks`; E16 follows E15. Both are planned and not started. E16 depends on E15. Two existing tasks gain hard edges: E09 depends on E16 (the later general packs extend this pack format instead of duplicating it), and R04, the full-program completion gate, depends on E15 and E16. Requirements ECO20–ECO22 in the [feature coverage](../../plans/cilogg-workbench-2026-10-21/FEATURE_COVERAGE.md) record the outcomes. MD03 activates the packs' Python decoders as a coordination note, not a hard edge (since the plan change of 2026-11-24, the narrower MD03b slice runs them and E16b follows it). No other task's order, hard edges or acceptance changes.

## D17 — Advanced plugin SDK (E17), accepted

On 2026-11-19 the user asked to extend the plugin system: plugins can create their own menu and submenus under the Plugins menu, and add menus and submenus to the tab, pane and log window context menus; combo boxes, in forms and in table cells, can be a plain drop-down, a drop-down with icons, with check boxes or with radio buttons; tables can hold icons; table cells, and any other element, can have a tooltip and an advanced tooltip filled by a plugin handler; tables keep multiple selection with Shift and Ctrl; a folder and file tree browser element with Shift and Ctrl multiple selection; check box and radio button elements; and "think what is missing for very advanced plugin SDK".

The proposed design is the [advanced plugin SDK](../plugin-sdk-advanced.md), recorded as task [E17](../../plans/cilogg-workbench-2026-10-21/tasks/E17.md) in slices E17a–E17i. It keeps plugin protocol version 1 and announces every new capability as a versioned host feature. The advanced tooltip needs the host to ask the plugin and wait; the design adds **host queries**, requests from the host to the plugin like `invoke`, with timeouts, cancellation, a cache, concurrency limits and a circuit breaker, and the same mechanism fills dynamic submenus, lazy tree children and field completion. A plugin gets the new host-to-plugin messages only when it declares them, in `handles` in its manifest and in `features` in its initialize result, and only while it is ready; a plugin that does not opt in gets the same messages with the same keys as today (only `initialize` gains additive keys, which both SDKs ignore). Manifests only gain new keys, never new values of existing enumerations (the E14 `icon` key of actions and views stays a file path), so older hosts still load them, and the SDK builders fall back to the older forms on older hosts. The plugin still never sends requests to the host in E17a–E17h; the optional slice E17i (a bounded command channel from plugins to the host) would change that statement of D11 and D14 and therefore needs its own explicit approval.

The design lists 22 open questions with recommended answers (section 23). E17 is design only and not started: implementation begins after the user approves the design, one branch and pull request per slice. Until then E17 is not in `PROGRESS.json.runnableTasks`. E17 depends on E14 (completed), the view SDK it extends; once approved it would be authorized ahead of G02/G03 like E13a and E15. R04, the full-program completion gate, gains E17 as a hard edge. E01 later absorbs the versioned host features and the plugin's `features` into its negotiated capabilities, as a coordination note without a hard edge, as E13's `host_features` under D11. E17 and E16 touch some of the same files (the manifest parser and the Plugins and Packs window), so each slice merges master first; configuration packs refuse the new process-only manifest keys. Requirements ECO23–ECO25 in the [feature coverage](../../plans/cilogg-workbench-2026-10-21/FEATURE_COVERAGE.md) record the outcomes. No other task's order, hard edges or acceptance changes.

After the design merged ([PR #127](https://github.com/dm17ryk/klogg/pull/127)), the user accepted it on 2026-11-19 with every recommended answer of section 23, gave the separate yes to question 13 (E17i, the host-command channel, built as the last slice) and asked for the slices in order, each as its own branch and pull request, opened one after the other. E17 is therefore authorized ahead of G02/G03 and runnable; E17a is the first slice. The statement of D11 and D14 that plugins never send requests to the host holds until E17i lands.

## D18 — Documentation-only changes skip the build workflows

On 2026-11-19 the user asked why a pull request that changes only design documents and this ledger needs a build: each one started the whole `CI Build` matrix (about 25 jobs on Windows, Linux and macOS) and CodeQL. `CI Build` ignored only `website/**` and a few root files, and CodeQL had no path filter on pull requests.

`.github/workflows/ci-build.yml` now runs, for pushes to master and for pull requests, only when a change touches a file outside `docs/**`, `plans/**`, `doc/**`, `screenshots/**`, `CHANGELOG.md`, `SECURITY.md`, `CONFIGURATION_EXPORTS.md`, `AGENTS.md`, `CLAUDE.md` and the paths it already ignored. The filter is an ordered `paths` list: `'**'` keeps every file, each `!` line drops one of those paths, and a later plain line keeps a file again. GitHub documents this negation and re-inclusion for `paths`, where the last matching pattern decides; it does not document `!` inside `paths-ignore`. The CodeQL pull request trigger uses the same list; its weekly schedule and manual runs are unchanged. A workflow is skipped only when every changed file is dropped, so a pull request that also touches code, tests, CMake or a workflow still builds.

Nothing in the build, packaging or tests reads the dropped paths: on 2026-11-19 their mentions in sources, CMake files, scripts, tests and workflows were all comments or Markdown links. The one exception, found in review, is `docs/plugin-sdk.md`: the Windows package installs it (`packaging/windows/prepare_release.cmd`, `packaging/windows/cilogg.nsi`), so the list keeps it. `DOCUMENTATION.md` (compiled into the application as the in-app guide) and `MCP.md` (installed with it) are never dropped. The other workflows were already limited to their own paths. The master ruleset blocks only deletion and force pushes and requires no status checks, so a skipped workflow cannot leave a pull request waiting. For a one-off skip, GitHub also honours `[skip ci]` in the head commit message of a push or a pull request.

A release started without a CI run id takes the artifacts of the last successful `CI Build`, which after a documentation-only commit were built from an earlier commit. Also found in review: `.github/workflows/ci-release.yml` tagged the release at the current default branch, so the tag and the commit embedded in the binaries could differ. The release job now reads the commit of the downloaded artifacts (`workflow_run.head_sha`) and creates the tag there with `gh release create --target`; it fails when the artifacts do not name exactly one commit.

This changes no task, hard edge, requirement or acceptance; `PLAN_VALIDATION.json` records it as `d18Checks`.

## D19 — Python's parser picks the pack decoders (E16b)

E16b first used a C++ text scan to choose which pack Python modules the **Add Python decoder** menu offers. Ten review rounds on [PR #179](https://github.com/dm17ryk/klogg/pull/179) each found Python syntax the scan misread, such as multi-line f-string fields, `del` target lists and conditional bindings, and each fix restarted the review and CI. On 2026-11-26 the user chose to let Python decide: a supervised worker gives each module to `ast.parse` and `compile`, so nothing in it runs. It reports the modules where `decode` may be callable after the top level, and the last binding wins. It runs once per pack version and edit, with the preview's interpreter rules (MD03b), a 10-second deadline and the MD03a size limits. Without an interpreter the menu stays empty and says why. The C++ scan and its edge-case tests are gone, and so are the limits documented for it (tuple assignments, `if` and `try`). What only running a module would show (a star import, `exec`) stays unseen, and such a decoder can still be routed by typing its target. Design section 4.4 of [plugin repositories](../plugin-repositories.md) records the rules.

This changes no task, hard edge, requirement or acceptance; E16b's ledger entry records it.
