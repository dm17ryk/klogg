# Complete Feature Coverage

187 individually mapped requirements. Every owner is a task in TASK_INDEX.json. Feature implementation remains planned; G01 reconciliation is recorded in the ledger. Existing delivered functionality is credited in MERGE_MAP.md and the execution baseline; this does not mark the broader requirements complete.

## Exploration, fields, time and data

| Requirement | Feature | Owning tasks |
|---|---|---|
| DATA01 | Preserve raw bytes and exact source navigation | F01, F10, D01 |
| DATA02 | Exact signed/unsigned integers, decimals and typed units | F01, F03, F08 |
| DATA03 | Regex, JSON, logfmt, CSV/TSV and syslog parsing | F02, F03 |
| DATA04 | Bounded XML parser extension | F03, F04 |
| DATA05 | Explainable format detection and versioned profiles | F02 |
| DATA06 | Multiline events, stack traces and cause chains | F04, I03, U04 |
| DATA07 | Provisional tail replacement and late/chunked input | F04, F11, D04 |
| DATA08 | Time zones, ambiguity, precision and unknown time | F05 |
| DATA09 | Device uptime, reboot epochs and separate clock domains | F05, F06 |
| DATA10 | Time seeking, delta/elapsed time and range navigation | F06 |
| DATA11 | Explicit cross-source clock alignment and chronological merge | F06, F07 |
| DATA12 | Recursive folder search, manifest limits and per-file errors | F16 |
| DATA13 | Deferred-file and managed-byte provenance with validated reopening | F01, F10, F16, D01 |
| DATA14 | Virtual structured event tables and JSON detail | F07, U04, V08 |
| DATA15 | Field queries, projection, formulas and deterministic sorting | F08 |
| DATA16 | Grouping, exact counts, sums, averages and unique values | F09 |
| DATA17 | Rates, ratios, percentiles and window calculations | F09, F14 |
| DATA18 | Top N plus Other, denominators and visible coverage | F09, F10, V05 |
| DATA19 | Pivot tables and multi-field Value Counts | F09, V08 |
| DATA20 | Distinguish missing, null, empty, invalid and zero | F01, F03, F08, F09 |
| DATA21 | Shared scans, cancellation, backpressure and bounded results | F07, F11 |
| DATA22 | Incremental live datasets and append/reload invalidation | F11, V17 |
| DATA23 | Lookup enrichment, enum/error catalogs and Value Names | F12 |
| DATA24 | Disposable persistent indexes and derived caches | F13 |
| DATA25 | Streaming zstd/lz4 inputs and compressed folder integration | F13, F16 |
| DATA26 | Typed CSV/JSON export and explicit raw/display export | F15, V18 |
| DATA27 | Optional OpenTelemetry field-role mapping | F15, E06 |
| DATA28 | Inspect query, dataset, precision, completeness and lineage | F10, V08, V15 |

## Charts and linked dashboards

| Requirement | Feature | Owning tasks |
|---|---|---|
| VIS01 | Line, step and point charts | V02, V04 |
| VIS02 | Min/max envelopes that preserve spikes | V02, V04 |
| VIS03 | Average/percentile bands and reference lines | F09, V04 |
| VIS04 | Area, stacked and 100-percent stacked charts | V04 |
| VIS05 | Categorical bars, rankings and Top N | V05 |
| VIS06 | Pareto cumulative comparisons | F09, V05 |
| VIS07 | Pie and donut composition | V05 |
| VIS08 | Stat, gauge, bar-gauge and sparkline summaries | V05 |
| VIS09 | Numeric distributions and histograms | V06 |
| VIS10 | Event-time count/rate histograms | V04, V06 |
| VIS11 | Distribution-over-time heatmaps | V06 |
| VIS12 | Categorical matrix and density heatmaps | V06 |
| VIS13 | Scatter and bubble charts with explicit paired fields | V07 |
| VIS14 | Box plots, outliers and distribution comparison | V07 |
| VIS15 | Aggregate, pivot, structured table and JSON panels | V08 |
| VIS16 | Treemap and sunburst hierarchies | V09 |
| VIS17 | Sankey flows and state-transition weights | V09 |
| VIS18 | Node and directed relationship graphs | V09 |
| VIS19 | State timelines versus discrete status history | V10 |
| VIS20 | Event lanes, intervals, requests and trace waterfalls | V10, I04, E08 |
| VIS21 | Funnels with cohorts and ordered steps | V11 |
| VIS22 | Parallel coordinates and normalized radar views | V11 |
| VIS23 | Geographic points/regions/tracks and equipment layouts | V12 |
| VIS24 | Flame graphs from real samples/weighted stacks | V13, E08 |
| VIS25 | Custom chart definitions and bounded custom drawing | V16 |
| VIS26 | Markdown and explanatory text panels | V16 |
| VIS27 | Small multiples with common scales | V17 |
| VIS28 | A/B and shifted/aligned comparison overlays | I02, V17 |
| VIS29 | Shared brush, zoom, cursor and selected time range | V14 |
| VIS30 | Click chart point/bucket/cell to contributing logs | F10, V14 |
| VIS31 | Dashboard variables, dependencies and common filter controls | V14 |
| VIS32 | Bookmarks, deployment/scenario/action annotations | V14, U11, A08 |
| VIS33 | Field-to-chart suggestions and readable panel editor | U05, V15 |
| VIS34 | Dashboard layouts, tabs, save/restore and template gallery | U03, U11, V15 |
| VIS35 | Live refresh, pause/freeze, requery and stale state | F11, V17 |
| VIS36 | Native first charts and optional offline rich renderer | V01, V02, V03 |
| VIS37 | Keyboard-accessible data fallback for every chart | V03, V08, U13 |
| VIS38 | PNG/SVG/CSV/JSON/PDF panel and dashboard export | V18 |

## Insights and investigation workflows

| Requirement | Feature | Owning tasks |
|---|---|---|
| INS01 | Similar-message templates with variable masking | I01 |
| INS02 | Rarest/new/unmatched patterns and representative examples | I01 |
| INS03 | Pattern frequency and token distributions | I01, V04, V05 |
| INS04 | Compare files, runs, sources and shifted time windows | I02 |
| INS05 | New/gone patterns, count/share/rate and distribution deltas | I02 |
| INS06 | Elapsed/run/marker alignment and unequal-duration normalization | I02, F06 |
| INS07 | Exception groups, cause chains and first/last occurrence | I03 |
| INS08 | Requests, sessions and transactions with explicit correlation | I04 |
| INS09 | Duration, outcome, maximum severity and orphan events | I04 |
| INS10 | Moving baselines, anomalies and explainable bands | I05 |
| INS11 | Explicit forecast experiments with holdout and assumptions | I05 |
| INS12 | Saved findings, notes and preserved investigation scope | U11 |

## Usability, workspace and developer experience

| Requirement | Feature | Owning tasks |
|---|---|---|
| UX01 | Readable consistent design system and iconography | U01 |
| UX02 | System/light/dark themes, high DPI and high contrast | U01, U13 |
| UX03 | Open file/drop folder/connect/run without mandatory project | U02, E02 |
| UX04 | Source sidebar, resizable panes and contextual event details | U03, U04 |
| UX05 | Raw/decoded/structured inspection and exact copy modes | U04, F15 |
| UX06 | Facets, typed filter chips, visual query construction and undo | U05 |
| UX07 | Retained independent searches and historical state | U06 |
| UX08 | Reusable filter groups and virtual streams | U07 |
| UX09 | Persistent tab/source groups with stable IDs | U08 |
| UX10 | QuickFind literal/regex/whole-word flags and history | U09 |
| UX11 | Keyboard command palette from shared action metadata | U10 |
| UX12 | Projects, saved views, notebook findings and annotations | U11 |
| UX13 | ANSI rendering with raw/display offset maps | U12 |
| UX14 | Preserve discontiguous selection, wrapping and highlighting | U04, U12, R01 |
| UX15 | Progressive disclosure, inline validation and helpful empty states | U13, V15, A06 |
| UX16 | Keyboard-only use, accessible names and table alternatives | U13, V08 |
| UX17 | Translations and discoverable tooltips/shortcuts | U01, U13 |
| UX18 | Guided sample projects across all developer domains | U02, U13, E09, R04 |

## Automation, tests and reproducibility

| Requirement | Feature | Owning tasks |
|---|---|---|
| AUTO01 | Versioned workflows, variables and schema validation | A01 |
| AUTO02 | Reuse existing parameterized actions and Python scenarios | A01, A03, D03, D14 |
| AUTO03 | Conditions, thresholds, rates and sustained windows | A02 |
| AUTO04 | Missing events/heartbeats and A-then-B deadlines | A02, A04 |
| AUTO05 | Debounce, cooldown, hysteresis and feedback suppression | A02 |
| AUTO06 | Run/capture/wait/analyze/action/report workflow composition | A03 |
| AUTO07 | Supervised cancellation and process/resource cleanup | A03, E02 |
| AUTO08 | Assertions on values, counts, latency and protocol outcomes | A04 |
| AUTO09 | Data-driven parameterized test suites | A04 |
| AUTO10 | Deterministic event recording and virtual-time replay | A05 |
| AUTO11 | Pause/step/seek/replay-speed controls and state reconstruction | A05 |
| AUTO12 | Friendly rule/recipe editor and previews | A06 |
| AUTO13 | Python escape hatch with existing supported API | A06, D14, E11 |
| AUTO14 | Headless CI, explicit exit codes and JSON/JUnit reports | A07 |
| AUTO15 | Schedules and bounded overlap policies | A07 |
| AUTO16 | Execution journal, attempted/acknowledged actions and evidence | A08 |
| AUTO17 | Crash recovery without automatically repeating effects | A03, A08 |
| AUTO18 | GUI/CLI/Python/MCP parity for every public capability | G03, E11, R01 |

## Docklight communication and Protocol Lab

| Requirement | Feature | Owning tasks |
|---|---|---|
| COM01 | Binary-safe RX/TX and immutable byte provenance | D01, D15 |
| COM02 | Write receipts, generations and actual completion stages | D01, A08 |
| COM03 | ASCII/HEX/DEC/BIN views and control-character visibility | D02 |
| COM04 | Keyboard communication console and echo policy | D02 |
| COM05 | Named send/receive sequence lists | D03, D04 |
| COM06 | Typed placeholders, byte parameters and fixed-width encodings | D03 |
| COM07 | Chunk-spanning exact/optional/wildcard byte matching | D04 |
| COM08 | Bitmask comparison and separate legacy text dialect | D04 |
| COM09 | Receive actions, replies, counters and descriptions | D05 |
| COM10 | Logging/display controls, stop conditions and trigger snapshots | D05 |
| COM11 | Shared checksum/CRC algorithms and TX insertion | D06 |
| COM12 | RX checksum validation and independent known vectors | D06 |
| COM13 | Delimited/fixed/length/gap framing and resynchronization | D07 |
| COM14 | Byte stuffing/escaping and raw-to-payload mapping | D07 |
| COM15 | Periodic/repeated sends, inter-byte delays and deadlines | D08 |
| COM16 | RTS/DTR and CTS/DSR/DCD/RI, break/parity capability controls | D09 |
| COM17 | Adapter-specific error/signal observations and limitations | D09, D18 |
| COM18 | Two-port passive monitoring and direction-labelled merge | D10 |
| COM19 | TCP client/server and UDP peer/listener | D11 |
| COM20 | Windows named pipes and platform alternatives | D11 |
| COM21 | Explicit forwarding bridges and monitor modes | D12 |
| COM22 | USB/Bluetooth HID reports and Bluetooth SPP serial | D13 |
| COM23 | Dynamic scripted replies and send/receive hooks | D14 |
| COM24 | Script console and file-driven communication | D14 |
| COM25 | Lossless segmented recordings and timing-aware active replay | D15, A05 |
| COM26 | Controlled delay/drop/duplicate/corruption fault simulation | D15 |
| COM27 | CILogg project persistence and Docklight project interchange | D16 |
| COM28 | Protocol Lab framing/field/chart/query integration | D17 |
| COM29 | Scenario scripting, lab and native automation integration | D17, A03, E11 |
| COM30 | Timestamp quality, hardware certification and TAP adapter path | D18 |

## Sources, integrations, sharing and extension

| Requirement | Feature | Owning tasks |
|---|---|---|
| ECO01 | Compatible versioned SDK with explicit source capabilities | E01 |
| ECO02 | Parser, analysis and action extension contracts | E01, E09 |
| ECO03 | Arbitrary command/stdout/stderr capture and explicit stdin | E02 |
| ECO04 | Docker and Compose logs with container metadata | E03 |
| ECO05 | Kubernetes pod/container capture and rollover coverage | E04 |
| ECO06 | SSH and OS journal/Event Log recipes | E05 |
| ECO07 | Preserve and improve existing ADB and syslog sources | E05 |
| ECO08 | OTLP logs, metrics, explicit spans and resources | E06 |
| ECO09 | Loki/OpenSearch/Elasticsearch/OpenObserve query connectors | E07 |
| ECO10 | Chrome Trace Event, OTLP span, Speedscope and folded profile imports | E08 |
| ECO11 | Reusable domain packs with templates/parsers/recipes | E09 |
| ECO12 | Pack dependency/version validation and reversible import | E09 |
| ECO13 | Git Team Folder, diffs, conflicts and explicit synchronization | E09 |
| ECO14 | Portable evidence bundles with raw excerpts and manifests | E10 |
| ECO15 | Optional previewed redaction on exported copies | E10 |
| ECO16 | IDE file/line/column navigation and path mappings | E11 |
| ECO17 | Capability introspection and supported Python/MCP clients | E11 |
| ECO18 | Optional AI parser/query/chart/rule proposals with normal validation | E12 |
| ECO19 | Core operation without cloud account or AI provider | E12, R03 |

## Quality, compatibility and delivery

| Requirement | Feature | Owning tasks |
|---|---|---|
| QUAL01 | Current-source reconciliation and credit existing work | G01 |
| QUAL02 | Representative deterministic fixtures and recorded budgets | G02 |
| QUAL03 | Meaningful failing-first tests and public parity harness | G03 |
| QUAL04 | Developer journey acceptance across multiple domains | R01 |
| QUAL05 | Large-file/live saturation, cancellation and long-run memory | R02 |
| QUAL06 | Versioned migration, atomic saves and recoverable backups | R03 |
| QUAL07 | Windows/macOS/Linux and native/optional renderer packages | R03 |
| QUAL08 | C++17 and supported Qt5/Qt6 core compatibility | G01, R03 |
| QUAL09 | Useful diagnostics, capability errors and truthful partial results | A08, R01, R02 |
| QUAL10 | Documentation, examples, screenshots and traceable completion | R04 |

## Message decoding and protocol inspection

| Requirement | Feature | Owning tasks |
|---|---|---|
| DEC01 | Eleven EHCP reference frame/checksum outcomes and exhaustive split provenance | MD01, MD02, MD05 |
| DEC02 | Multiple/split messages with source/device/direction isolation | MD02, MD05 |
| DEC03 | Incomplete EHCI split CNT, interleaved diagnostics, length mismatch and missing CRC | MD05 |
| DEC04 | EHCI tag-length framing, repeated maps, empty/NUL data and honest recovery/fidelity | MD02, MD05 |
| DEC05 | Explicit payload codecs, Unicode/file-byte/logical offsets and unknown bytes | MD02, MD04 |
| DEC06 | AT CSQ transaction/URC with 40 ms log duration, -67 dBm and unknown BER | MD06 |
| DEC07 | Contextual AT grammar, echo/result modes, schemas and ambiguous correlation | MD06 |
| DEC08 | AT SMS/prompts/data mode and truncated observations without false transactions | MD06 |
| DEC09 | Supervised bounded decoder state/output, crash/deadline/cancellation/backpressure | MD03, MD08 |
| DEC10 | Legacy JSON/v1 plugins and GUI/CLI/MCP/Python typed-data/error parity | MD03, MD07 |
| DEC11 | Nested SRING hex-to-EHCP with independent wrapper/child diagnostics and exact maps | MD01, MD04, MD06 |
| DEC12 | Cross-notification child assembly, parent/child query scopes and nesting/cycle limits | MD02, MD06, MD07 |
| DEC13 | Actual supplied Lua execution, engine/script provenance and reference comparison | MD01, MD05, MD08 |
| DEC14 | Byte-exact Exported-PDU sidecars versus genuine PCAP transport/reassembly semantics | MD01, MD04, MD08 |
