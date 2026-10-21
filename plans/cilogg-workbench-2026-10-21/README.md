# CILogg Developer Workbench Roadmap

[Download the complete Codex implementation package](CILogg_Codex_Implementation_Plan.zip?raw=1) — 107 task cards, 193 requirements, source references, dependencies, acceptance checks and Codex starter prompts. Extract the ZIP before following its local links. Application code has not been changed by publishing this roadmap.

Publication-ready mapping of the full workbench plan, including project readiness, every chart family, automation and all Docklight tasks. Published on 2026-10-22: nine milestones and nine tracking issues. Their actual GitHub identities are recorded below.

**Keys versus GitHub IDs:** M00–M08 are stable plan keys. GitHub assigns its own milestone numbers on creation. Record the returned numbers/URLs; never assume M04 is GitHub milestone 4.

**Scheduling:** Start project-readiness work immediately after G01. Package-catalog review and optional Discord setup do not block independent feature implementation. Milestones group delivery goals; the task DAG determines valid execution order, including early Protocol Lab foundations. No calendar dates or unsupported version promises are assigned.

**Progress:** Initially publish one tracking issue per milestone, with the task checklists below. This shows nine tracking issues, not 107 independently weighted tasks. For granular GitHub percentages, create one issue per task as execution starts and assign it to the primary milestone. Checkbox completion alone does not update GitHub milestone percentages. Requirements remain tracked in FEATURE_COVERAGE.md.


## Published GitHub roadmap — 2026-10-22

The nine milestones and nine tracking issues below are live. The CILogg social preview is configured and visually verified in repository Settings. The product implementation tasks remain planned; publishing roadmap objects does not complete them.

| Plan key | GitHub milestone | Tracking issue |
|---|---|---|
| M00 | [#1 — M00 · Project readiness and distribution](https://github.com/dm17ryk/klogg/milestone/1) | [#48](https://github.com/dm17ryk/klogg/issues/48) |
| M01 | [#2 — M01 · First linked charts and everyday usability](https://github.com/dm17ryk/klogg/milestone/2) | [#49](https://github.com/dm17ryk/klogg/issues/49) |
| M02 | [#3 — M02 · Structured logs and source workflows](https://github.com/dm17ryk/klogg/milestone/3) | [#50](https://github.com/dm17ryk/klogg/issues/50) |
| M03 | [#4 — M03 · Analytical dashboards and linked visualizations](https://github.com/dm17ryk/klogg/milestone/4) | [#51](https://github.com/dm17ryk/klogg/issues/51) |
| M04 | [#5 — M04 · Investigations, automation and CI](https://github.com/dm17ryk/klogg/milestone/5) | [#52](https://github.com/dm17ryk/klogg/issues/52) |
| M05 | [#6 — M05 · Advanced visualizations and data](https://github.com/dm17ryk/klogg/milestone/6) | [#53](https://github.com/dm17ryk/klogg/issues/53) |
| M06 | [#7 — M06 · Protocol Lab and Docklight capabilities](https://github.com/dm17ryk/klogg/milestone/7) | [#54](https://github.com/dm17ryk/klogg/issues/54) |
| M07 | [#8 — M07 · Developer integrations and reusable packs](https://github.com/dm17ryk/klogg/milestone/8) | [#55](https://github.com/dm17ryk/klogg/issues/55) |
| M08 | [#9 — M08 · Cross-platform release acceptance](https://github.com/dm17ryk/klogg/milestone/9) | [#56](https://github.com/dm17ryk/klogg/issues/56) |

README cleanup, package catalog submissions and remaining implementation work are tracked in M00 and the other milestones. They were not performed by creating these roadmap objects.


## Milestone summary

| Key | GitHub title | Tasks |
|---|---|---:|
| M00 | Project readiness and distribution | 11 |
| M01 | First linked charts and everyday usability | 9 |
| M02 | Structured logs and source workflows | 12 |
| M03 | Analytical dashboards and linked visualizations | 22 |
| M04 | Investigations, automation and CI | 15 |
| M05 | Advanced visualizations and data | 9 |
| M06 | Protocol Lab and Docklight capabilities | 18 |
| M07 | Developer integrations and reusable packs | 7 |
| M08 | Cross-platform release acceptance | 4 |

## Publish and reconcile

1. Inspect existing open and closed milestones and existing roadmap issues for duplicate titles or the stable marker in each payload. Reuse matching objects; do not overwrite unrelated descriptions.
2. Copy the versioned plan into repository documentation and link it from ROADMAP.md. Keep public links pointed at published GitHub files, never local sandbox paths.
3. Create/reuse milestones in the order below, with the exact title/description from MILESTONES.json. Leave due dates unset. Record the returned GitHub IDs and URLs in the publication ledger.
4. Create/reuse the nine tracking issues and assign each to its actual milestone number. Expand runnable work into task issues when needed; avoid duplicating existing merged work.
5. Update README’s roadmap link and, if wanted, a single next-milestone badge using its real number. Verify the rendered result.
6. Read back all written objects and update PROGRESS.json. Publishing planning objects does not complete their product implementation tasks.

Publication was completed after the user authorized the browser workflow. Milestones were created in GitHub, tracking issues were assigned through the GitHub connection, and the social-preview image was set in repository Settings. Preserve these real IDs during future updates.

## M00 · Project readiness and distribution

Make CILogg discoverable, accurately documented, reliably downloadable and ready for contributors.

Exit criteria:
- README badges, support links and download routes are verified for this fork.
- Release channels identify exact source/build provenance and validate required platform artifacts.
- The roadmap, current-feature documentation, package publication work and CILogg social preview have recorded evidence.
- GitHub Discussions is the durable support route; a separate Discord is optional and requires a verified owner/invite.

Plan tasks: G01, H01, H02, H03, H04, H05, H06, H07, H08, H09, H10.

Task dependencies and acceptance details remain authoritative in the versioned CILogg Developer Workbench implementation plan. Dates are intentionally unset.

<!-- cilogg-workbench-2026-10-21:M00 -->

**Tracking issue checklist:**

- [ ] G01 — Reconcile current source, prior roadmap, and task ownership
- [ ] H01 — Repair the public README and repository identity
- [ ] H02 — Make release channels and artifact provenance trustworthy
- [ ] H03 — Replace decorative quality badges with verifiable checks
- [ ] H04 — Publish the implementation roadmap as GitHub milestones
- [ ] H05 — Document current features and the path from first use
- [ ] H06 — Publish maintained Windows package manifests
- [ ] H07 — Publish maintained macOS and Linux distribution routes
- [ ] H08 — Establish CILogg-owned community and support routes
- [ ] H09 — Keep project links and publication metadata healthy
- [ ] H10 — Add a CILogg social preview and reusable project artwork

## M01 · First linked charts and everyday usability

Ship the first useful charts alongside a polished, immediate open-and-search workflow.

Exit criteria:
- Shared value/source contracts and a bounded parity harness are established.
- Native regex-derived charts support source navigation without waiting for the full query stack.
- Onboarding, themes, QuickFind and the command palette are discoverable and keyboard usable.

Plan tasks: G02, G03, F01, V01, V02, U01, U02, U09, U10.

Task dependencies and acceptance details remain authoritative in the versioned CILogg Developer Workbench implementation plan. Dates are intentionally unset.

<!-- cilogg-workbench-2026-10-21:M01 -->

**Tracking issue checklist:**

- [ ] G02 — Freeze representative fixtures and measured release budgets
- [ ] G03 — Establish a reusable GUI/CLI/MCP parity harness
- [ ] F01 — Canonical exact values, event identity, source spans, and dataset wire contract
- [ ] V01 — Freeze panel, dataset binding and capability contracts
- [ ] V02 — Ship P06 native capture charts and spike-preserving navigation
- [ ] U01 — Introduce a coherent Qt design system
- [ ] U02 — Create the welcoming open/connect/run workflow
- [ ] U09 — Finish visible QuickFind controls and history
- [ ] U10 — Add a command palette from existing action metadata

## M02 · Structured logs and source workflows

Explore structured and multiline logs, combine sources and capture arbitrary developer processes.

Exit criteria:
- Parsers, typed fields, multiline events, time domains and chronological navigation preserve raw provenance.
- Folder search, virtual event tables and queries remain bounded and cancellable.
- Source/plugin contracts and process stdout/stderr capture work through the shared application services.

Plan tasks: F02, F03, F04, F05, F06, F07, F08, F16, U03, U04, E01, E02.

Task dependencies and acceptance details remain authoritative in the versioned CILogg Developer Workbench implementation plan. Dates are intentionally unset.

<!-- cilogg-workbench-2026-10-21:M02 -->

**Tracking issue checklist:**

- [ ] F02 — Versioned format/profile registry and explainable bounded detection
- [ ] F03 — Shared regex, JSON, logfmt, CSV/TSV, syslog parsers and bounded XML extension
- [ ] F04 — Multiline event assembly, stack traces, exceptions, and provisional replacement
- [ ] F05 — Precise timestamps, timezone policies, device uptime, and resets
- [ ] F06 — Sparse time lookup, elapsed-time navigation, clock alignment, and explicit chronological merge
- [ ] F07 — Source-set datasets, shared service, virtual event table, and field exploration
- [ ] F08 — Typed query AST, filters, formulas, projection, and bounded deterministic sorting
- [ ] F16 — Recursive folder manifests/search, lazy bounded readers, grouped results, and validated reopen
- [ ] U03 — Add the investigation workspace shell
- [ ] U04 — Build the structured event and exception inspector
- [ ] E01 — Extend the plugin SDK with explicit developer-source capabilities
- [ ] E02 — Add arbitrary command-output and stdin capture

## M03 · Analytical dashboards and linked visualizations

Build and save rich dashboards that remain linked to exact contributing log evidence.

Exit criteria:
- Aggregations, windows, percentiles, comparisons and live datasets have explicit limits/completeness.
- Line/area/bar/stat/distribution/heatmap/scatter/box/table panels share filters, cursor and drilldown.
- Panel creation, retained searches, groups, dashboard persistence and exports support GUI and headless contracts.

Plan tasks: F09, F10, F11, F14, V03, V04, V05, V06, V07, V08, V14, V15, V16, V17, V18, U05, U06, U07, U08, U11, U12, I02.

Task dependencies and acceptance details remain authoritative in the versioned CILogg Developer Workbench implementation plan. Dates are intentionally unset.

<!-- cilogg-workbench-2026-10-21:M03 -->

**Tracking issue checklist:**

- [ ] F09 — Grouping, buckets, rates, windows, distributions, pivots, and exactness
- [ ] F10 — Dataset lineage, derived-value explanation, and bounded drilldown
- [ ] F11 — Compatible scan sharing, bounded multi-panel execution, and incremental live datasets
- [ ] F14 — Typed transform registry for advanced visual dataset shapes
- [ ] V03 — Add an optional, packaged, offline rich renderer
- [ ] V04 — Render time-aware trends, bands, areas and event rates
- [ ] V05 — Add categorical rankings, proportions, Pareto and summary cards
- [ ] V06 — Distinguish distributions, time buckets and both heatmaps
- [ ] V07 — Add scatter/bubble correlation and honest boxplots
- [ ] V08 — Reuse the virtual table for aggregate, pivot and JSON inspection
- [ ] V14 — Unify linked selection, filters, variables, annotations and drilldown
- [ ] V15 — Deliver simple chart creation, inspectable suggestions and dashboards
- [ ] V16 — Add custom charts and markdown/text without opening a second engine
- [ ] V17 — Add live/pause semantics, small multiples and comparison overlays
- [ ] V18 — Export reproducible images/data/reports and close release gates
- [ ] U05 — Add facets and a visual field-query builder
- [ ] U06 — Retain independent searches and investigation checkpoints
- [ ] U07 — Add reusable filter groups and virtual streams
- [ ] U08 — Add persistent tab/source groups
- [ ] U11 — Save annotated investigations and notebook-style findings
- [ ] U12 — Add checkpointed ANSI rendering with raw/display maps
- [ ] I02 — Compare files, runs, periods and distributions

## M04 · Investigations, automation and CI

Turn repeated investigations into reproducible, inspectable workflows and automated checks.

Exit criteria:
- Patterns, exceptions, transactions and baseline anomalies expose supporting evidence.
- Rules, workflows, assertions, deterministic replay, journaling and cancellation use shared services.
- CI reports, portable evidence, supported automation clients and accessibility checks cover real developer journeys.

Plan tasks: I01, I03, I04, I05, A01, A02, A03, A04, A05, A06, A07, A08, U13, E10, E11.

Task dependencies and acceptance details remain authoritative in the versioned CILogg Developer Workbench implementation plan. Dates are intentionally unset.

<!-- cilogg-workbench-2026-10-21:M04 -->

**Tracking issue checklist:**

- [ ] I01 — Implement explainable message patterns and rarity
- [ ] I03 — Group exceptions and navigate stack traces
- [ ] I04 — Correlate requests, sessions and transactions
- [ ] I05 — Add explainable baselines, anomalies and forecast experiments
- [ ] A01 — Define workflows as adapters over existing actions and scenarios
- [ ] A02 — Add bounded conditions, windows and missing-event rules
- [ ] A03 — Compose capture, command, analysis and action workflows
- [ ] A04 — Add developer assertions and data-driven tests
- [ ] A05 — Record and replay event-driven investigations
- [ ] A06 — Build a friendly automation recipe and rule editor
- [ ] A07 — Deliver headless CI execution, schedules and reports
- [ ] A08 — Add the execution journal and failure diagnostics
- [ ] U13 — Polish onboarding, accessibility and progressive disclosure
- [ ] E10 — Export portable evidence with optional redaction
- [ ] E11 — Integrate IDE navigation and developer command clients

## M05 · Advanced visualizations and data

Complete specialist visualizations and trace/profile imports without inventing unsupported semantics.

Exit criteria:
- Hierarchy, Sankey/network, state/waterfall, funnel/radar, map/layout and flame graph panels use valid datasets.
- Enrichment, caches, compression and typed exports preserve exact values and source provenance.
- Trace/profile panels distinguish actual span/sample inputs from ordinary log events and retain accessible data fallbacks.

Plan tasks: F12, F13, F15, V09, V10, V11, V12, V13, E08.

Task dependencies and acceptance details remain authoritative in the versioned CILogg Developer Workbench implementation plan. Dates are intentionally unset.

<!-- cilogg-workbench-2026-10-21:M05 -->

**Tracking issue checklist:**

- [ ] F12 — Lookup enrichment, Value Names, and stable raw/display separation
- [ ] F13 — Disposable indexes/caches and shared bounded zstd/lz4 inputs
- [ ] F15 — Snapshot dataset export and optional OpenTelemetry record mapping
- [ ] V09 — Implement hierarchy, Sankey and explicit relationship panels
- [ ] V10 — Distinguish state intervals, observations, event lanes and waterfalls
- [ ] V11 — Add funnels, parallel coordinates and normalized radar comparisons
- [ ] V12 — Add maps and equipment/layout overlays from explicit coordinates
- [ ] V13 — Gate flame graphs on actual profiling input
- [ ] E08 — Import real traces and performance profiles

## M06 · Protocol Lab and Docklight capabilities

Deliver communication capture, simulation and protocol testing alongside general log analysis.

Exit criteria:
- Binary-safe RX/TX, sequences, byte patterns, checksums, framing and recordings preserve wire evidence.
- Supported serial/network/USB/HID/pipe/monitoring capabilities are explicit and tested on their stated backends.
- Replay, device simulation, fault injection, project import and scripting share the automation services.
- Hardware/TAP-specific precision or compatibility claims include actual adapter/conformance evidence.

Plan tasks: D01, D02, D03, D04, D05, D06, D07, D08, D09, D10, D11, D12, D13, D14, D15, D16, D17, D18.

Task dependencies and acceptance details remain authoritative in the versioned CILogg Developer Workbench implementation plan. Dates are intentionally unset.

<!-- cilogg-workbench-2026-10-21:M06 -->

**Tracking issue checklist:**

- [ ] D01 — Binary-safe transport lifecycle, observation and write receipts
- [ ] D02 — ASCII/HEX/DEC/BIN workbench, control characters and keyboard console
- [ ] D03 — Named sequences and explicit byte parameters
- [ ] D04 — Incremental byte receive patterns, optional bytes and bitmasks
- [ ] D05 — Receive effects, counters, logging control, stops and triggered snapshots
- [ ] D06 — One checksum/CRC implementation for TX placement, RX validation and previews
- [ ] D07 — Framing, escaped payloads and byte stuffing
- [ ] D08 — Periodic sends, inter-byte delays, deadlines and cancellation
- [ ] D09 — Serial handshake, break, parity control and honest diagnostics
- [ ] D10 — Two-port passive communication monitoring
- [ ] D11 — TCP client/server, UDP peer/listener and named pipes
- [ ] D12 — Explicit bridges, forwarding monitors and additional channels
- [ ] D13 — USB/Bluetooth HID reports and Bluetooth SPP
- [ ] D14 — Dynamic replies, script console and file-driven communication
- [ ] D15 — Lossless recording, timing-aware replay and deliberate fault injection
- [ ] D16 — Native project schema, Docklight import/export and reproducible workspace setup
- [ ] D17 — Protocol Lab, shared byte analysis jobs, scenario and automation parity
- [ ] D18 — Timestamp provenance, adapter certification and release conformance

## M07 · Developer integrations and reusable packs

Connect CILogg to common development environments and share reusable investigation definitions.

Exit criteria:
- Container, Kubernetes, SSH/OS, OTLP and remote log-query connectors declare limits and capabilities.
- Versioned domain packs and Git Team Folder workflows preserve compatibility and explicit synchronization.
- Optional AI proposals pass normal schema/behavior validation; core work remains usable without AI or a cloud account.

Plan tasks: E03, E04, E05, E06, E07, E09, E12.

Task dependencies and acceptance details remain authoritative in the versioned CILogg Developer Workbench implementation plan. Dates are intentionally unset.

<!-- cilogg-workbench-2026-10-21:M07 -->

**Tracking issue checklist:**

- [ ] E03 — Deliver Docker and Compose capture packs
- [ ] E04 — Deliver Kubernetes log capture packs
- [ ] E05 — Add SSH and OS log-source recipes
- [ ] E06 — Add OpenTelemetry log, metric and trace ingestion
- [ ] E07 — Add remote query connectors for existing log backends
- [ ] E09 — Create shareable packs and Git-backed team configuration
- [ ] E12 — Add optional inspectable AI assistance

## M08 · Cross-platform release acceptance

Complete the full workbench program with measured reliability, migrations and documented packages.

Exit criteria:
- All developer-journey, saturation, cancellation, recovery and migration gates pass with recorded evidence.
- Windows/macOS/Linux package and optional-renderer behavior is verified on the claimed platforms.
- Every requirement has delivery evidence or an explicitly approved scope change; docs and examples match shipped behavior.

Plan tasks: R01, R02, R03, R04.

Task dependencies and acceptance details remain authoritative in the versioned CILogg Developer Workbench implementation plan. Dates are intentionally unset.

<!-- cilogg-workbench-2026-10-21:M08 -->

**Tracking issue checklist:**

- [ ] R01 — Build end-to-end developer workflow acceptance suites
- [ ] R02 — Close stress, performance and long-run resource gates
- [ ] R03 — Validate migrations, recoverability and platform packages
- [ ] R04 — Publish developer documentation, examples and completion ledger
