---
title: "About"
type: docs
---

## Follow the logs. Find the signal. Automate the next step.

**CILogg brings log investigation and device automation into one workspace.**
Built on [klogg](https://github.com/variar/klogg), it combines fast large-file
exploration with live capture, interactive analysis, and repeatable automation.

{{< button href="https://github.com/dm17ryk/klogg/releases" >}}Download CILogg{{< /button >}}
{{< button href="https://github.com/dm17ryk/klogg/blob/master/DOCUMENTATION.md" >}}Start exploring{{< /button >}}

![CILogg dark workspace with highlighted logs and separate search results](screenshots/workspace.jpg)

*Captured from the normal installed Windows application, build 26.04.2.1078.
Log views use synthetic demo records included in the repository.*

## More than a log viewer

| Workflow | What CILogg adds |
| --- | --- |
| **🔌 Capture live sources** | Serial / COM logs plus bundled ADB Logcat and UDP Syslog plugins. |
| **⚡ React to device output** | Reusable commands with typed parameters, automatic responses, and response counters. |
| **🔬 Inspect patterns** | Regex Lab with samples, selections, file snapshots, and named capture inspection. |
| **📊 Spot recurring values** | Value Counts with frequencies, percentages, source navigation, and CSV export. |
| **🐍 Repeat an investigation** | Python scripts, headless scenarios, and remote lab controller/agent workflows. |
| **🤖 Automate your workspace** | Commander CLI control and a native stdio MCP server for compatible assistants. |
| **🧩 Extend your tools** | Sources, converters, and tools running as separate plugin processes. |

## See the details

### Regex Lab: inspect every capture

![Regex Lab evaluating named severity and service captures beside the log](screenshots/regex-lab-showcase.jpg)

Test a pattern against a bounded sample before applying it to your search.
[Explore Regex Lab →](https://github.com/dm17ryk/klogg/blob/master/docs/regex-lab.md)

### Value Counts: turn repetition into answers

![Value Counts ranking log severity values with counts and percentages](screenshots/value-counts-showcase.jpg)

Discover the values that dominate your log, then filter, navigate, or export CSV.
[Explore Value Counts →](https://github.com/dm17ryk/klogg/blob/master/docs/value-counts.md)

## Devices, responses, and visual triage

| Reusable device commands | Automatic responses |
| --- | --- |
| [![Actions with COM routing and command sequences](screenshots/actions-showcase.jpg)](screenshots/actions-showcase.jpg) | [![Responses with configured match rules](screenshots/responses-showcase.jpg)](screenshots/responses-showcase.jpg) |

| Bundled sources and plugins | Highlighter sets |
| --- | --- |
| [![Plugin manager with ADB Logcat and Syslog Server](screenshots/plugins-showcase.jpg)](screenshots/plugins-showcase.jpg) | [![Highlighter configuration with colored match rules](screenshots/highlighters-showcase.jpg)](screenshots/highlighters-showcase.jpg) |

**[Browse all ten screenshots →](https://github.com/dm17ryk/klogg/blob/master/docs/screenshots.md)**
including the response editor, Syslog settings, and Script Runner.

## Comparing with klogg

CILogg inherits klogg's fast large-file browsing, accelerated regex and Boolean
search, file following, highlighters, encoding detection, dark mode, and scratchpad.
It adds the capture, analysis, automation, and plugin workflows above.

[See the comparison table →](https://github.com/dm17ryk/klogg#comparing-with-klogg)

## Find your next step

- [User guide and sample investigation](https://github.com/dm17ryk/klogg/blob/master/DOCUMENTATION.md)
- [CLI, MCP client setup, and generated skills](https://github.com/dm17ryk/klogg/blob/master/MCP.md)
- [Plugin setup and SDK](https://github.com/dm17ryk/klogg/blob/master/docs/plugin-sdk.md)

## Downloads

Download Windows, Linux, and macOS assets from
[CILogg Releases](https://github.com/dm17ryk/klogg/releases). Choose the release
and CPU architecture that match your machine, read the release notes, and verify
the download against its published `*-sha256.txt` file. Testing builds use
versioned `continuous-*` tags.

