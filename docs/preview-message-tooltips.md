# Message reassembly and line Preview

Hover over a physical log row in the main or filtered viewer to show its first
matching, enabled JSON Preview. The tooltip escapes log values, preserves field
order and repeated fields, and abbreviates long values. Use the existing **Send
to Preview** action to inspect the full view. Hover does not change selection.

![Decoded line tooltip from the integration fixture](images/preview-line-tooltip.png)

For one whole selected row, Send to Preview also uses a complete assembled EHCI
body when available. Partial text selections and multiple selected rows keep
their existing behavior.

## Supported fragmented logs

The first adapter supports the observed CP NET UART dump format: a `PUT` request
with `Host: CARE-PANELINFO`, one positive `Content-Length`, and an EHCI body,
followed by task-tagged `NET Send message command` and `NET SendTCPData Len(...)`
diagnostics. CILogg timestamp and RX/TX wrappers are supported. Continuations use
the same direction, task and port. Unrelated diagnostics and other ports are
excluded; reconnects and new messages end unfinished assemblies.

Each fragment must reproduce its logged byte count exactly. HTTP headers use
CRLF; multiline fragment separators are accepted only when byte counts select
one unique reconstruction. Completion requires the advertised body length and
a `CRC` plus four hexadecimal digits trailer. This checks framing, **not the
checksum or successful network delivery**. Missing or ambiguous dumps remain
incomplete. This is reconstruction of decoded UTF-8 log text, not TCP sequence
reassembly or recovery of bytes omitted by UART logging. Other carrier formats
and new TShark decoders remain subsequent work.

For continuations, a unique suffix immediately preceding the send diagnostic
may exclude unrelated raw replies such as `OK`. Exclusions are counted in
`excludedPrefixRows` and logged without their text. Ambiguous boundaries remain
incomplete; HTTP requests always start a fresh payload.

## CLI and native MCP

Both use the Commander operation `preview_message`:

```json
{"text":"HELLO example","reassemble":false,"timeoutMs":3000}
```

```text
cilogg command --action preview_message --json-file request.json
```

Native MCP: call `cilogg_execute` with `action: "preview_message"` and
`arguments: {"analysis_json": { ...request... }}`. Discovery includes this action.

For an open tab, omit `text`, specify a positive one-based physical `line`, and
use the existing window/tab selectors. With inline text, `line` optionally
chooses one physical row; without it, ordinary multiline input is preserved.
`previewName` optionally selects an enabled definition. `reassemble` defaults
to true. No request can select a worker executable or import definitions.

Results contain `backend: "json_preview"`, state/reason, availability, completion
and limit status, definition hash, ordered field `columns` and depth, assembly
metadata, input text and tooltip HTML. Columns are Field, Value, Raw, Offset and
Width from the existing renderer. `offsetSpace: "legacy_preview_buffer"` labels
those offsets: they are not original log byte spans. Source-backed requests
include source identity, generation, revision and indexed byte watermark.
Assembly row numbers are explicitly one-based.

## Bounds and validity

- Reassembly: 1,024 context rows, 1 MiB raw/decoded context, 64 KiB body and
  64 message contexts. The source context starts up to 511 rows before the target.
- Worker input: 64 KiB UTF-8 text; definitions snapshot: 1 MiB with structural
  limits. Two workers run concurrently; excess requests report `preview_busy`.
- A separate internal mode of the existing executable compiles and evaluates
  user patterns. The worker uses a data-only tree and needs no GUI platform
  plugin. Default deadline is three seconds, configurable from 100 to 10,000 ms.
  Cancellation or timeout kills and reaps the child.
- Workers retain up to 512 fields and 128 KiB response data, with an explicit
  limit status. Tooltips show up to 20 fields; names and values are abbreviated
  at 128 and 256 characters respectively.
- Source reads use bounded snapshot reads before presentation transformations.
  Stateful encodings without decoder checkpoints and ambiguous generic
  UTF-16/32 random access report an unsupported-encoding reason.
- New hover/scroll/leave, tab changes, profile changes, rewrites, encoding changes
  and closure suppress obsolete tooltips. Completed-prefix appends can report
  `newerDataAvailable`; changes to a selected provisional tail invalidate it.
- Diagnostics log identities, counters and termination reasons, not source
  bodies. JSON command responses intentionally include the requested text/values.

The adapter reuses the existing JSON decoder; it does not overwrite profiles or
claim MD01/TShark integration acceptance.

Windows qualification is recorded in
[preview-windows-observations.json](workbench/preview-windows-observations.json).
