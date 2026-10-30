# CILogg in pictures

**Explore ten views of the normal installed Windows application.** These screenshots
come from CILogg **26.04.2.1078**, captured on 28 October 2026. Click an image to
open it at full resolution.

[Project overview](../README.md) · [User guide](../DOCUMENTATION.md) ·
[Sample investigation](../DOCUMENTATION.md#your-first-investigation)

## Log investigation

### Full context, focused results

[![Normal CILogg viewer with COM tabs, highlighted errors, and filtered results](images/workspace.jpg)](images/workspace.jpg)

Browse the full log above the matching lines. The sample contains 30 warnings and
errors; the existing COM capture tabs remain visible beside it.

### Regex Lab: see what your pattern captures

[![Installed CILogg Regex Lab with completed results and the named level capture](images/regex-lab-showcase.jpg)](images/regex-lab-showcase.jpg)

Inspect named groups, values, and source positions before applying a pattern.
[Regex Lab guide →](regex-lab.md)

### Value Counts: find the values that dominate

[![Installed CILogg Value Counts showing severity frequencies and percentages](images/value-counts-showcase.jpg)](images/value-counts-showcase.jpg)

Rank captured values, apply a selected value as a filter, navigate to its first
occurrence, or export CSV. [Value Counts guide →](value-counts.md)

## Device workflows

### Actions: keep commands ready to use

[![Actions window showing a COM destination, reusable device commands, and transmit sequences](images/actions-showcase.jpg)](images/actions-showcase.jpg)

Select an action destination and browse reusable command definitions.
[Actions and routing guide →](multiple-action-tabs.md)

### Responses: react to the log

[![Responses window with enabled match rules and an automatic-response switch](images/responses-showcase.jpg)](images/responses-showcase.jpg)

Configure which incoming patterns should trigger a response.
[Automatic responses guide →](automatic-responses.md)

### Response editor: connect a trigger to a sequence

[![Response editor with the Panel Ready match, linked actions, delays, and snapshot option](images/response-editor-showcase.jpg)](images/response-editor-showcase.jpg)

This configured response matches `Panel Ready`, then runs linked actions with
delays. The editor also exposes comments, timestamps, snapshots, and communication
controls. It was opened for inspection and cancelled without saving changes.

## Sources and automation

### Plugins: expand your sources and tools

[![Plugin manager with bundled ADB Logcat, Syslog Server, and example plugins](images/plugins-showcase.jpg)](images/plugins-showcase.jpg)

Manage installed plugins, enable or disable them, inspect details, and configure
capture storage. [Plugin guide →](plugin-sdk.md)

### Syslog: choose how network logs appear

[![Syslog configuration with encoding, output format, severity, and allowed sender settings](images/syslog-settings-showcase.jpg)](images/syslog-settings-showcase.jpg)

Configure decoding, annotated output, severity filtering, and sender restrictions.
The listener address and UDP port are selected when starting a capture.
[Syslog guide →](../plugins/examples/syslog-server/README.md)

### Script Runner: global and per-tab workflows

[![Script Runner with script and JSON-argument pickers plus global and per-tab controls](images/script-runner-showcase.jpg)](images/script-runner-showcase.jpg)

Choose a Python script and optional JSON arguments, then run it globally or on the
active tab. This screenshot shows the controls before a script is selected.
[CLI and MCP automation →](../MCP.md)

## Visual triage

### Highlighters: make important events stand out

[![Highlighter editor showing reusable sets, colored patterns, and color controls](images/highlighters-showcase.jpg)](images/highlighters-showcase.jpg)

Organize patterns into sets and choose whole-line or match-only colors.
[Highlighter guide →](../DOCUMENTATION.md#using-highlighters)

[Capture details and reproduction notes](images/README.md) ·
[Back to the user guide](../DOCUMENTATION.md)
