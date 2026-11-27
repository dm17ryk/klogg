# TShark/Lua message previews

Open **Tools > Preview Decoder Settings** or use its toolbar button. The decoder
is disabled by default. Select your own `tshark` and `text2pcap` executables and
Lua scripts, then explicitly enable external Lua and the decoder. Apply validates
the complete configuration atomically; a rejected change leaves the previous
settings active. Lua is executable code: select only scripts you trust. Neither
Wireshark tools nor private protocol packs are shipped with CILogg, and this
feature does not claim an OS sandbox. CILogg resolves its bundled Python runtime
when available, otherwise a suitable system Python; a trusted
`CILOGG_SCRIPT_PYTHON` can select an interpreter for controlled deployments.

**Configuration packs** (plugins with `"kind": "config-pack"`, see
[plugin repositories and configuration packs](../plugin-repositories.md) section 4.3) can add
Lua and routes. While a pack is enabled, its Lua files load before your own scripts, without the
external Lua approval (installing and enabling the pack is that decision), each pack in a Lua
environment of its own. A session holds at most 32 Lua files, your 8 included. Pack routes sit
below yours: a route of yours with the same key wins. Pack Lua and routes are added when a
session starts and are never written into these settings, their exports or projects. Enabling,
disabling or updating a pack starts new TShark sessions. Without TShark, pack previews use
their JSON definitions.

**Python decoders** of enabled packs (MD03b, [section 4.4](../plugin-repositories.md)) use the
same routes: a route whose decoder column reads `python:<pack-id>/<decoder-id>` sends its preview
or assembled protocol to that decoder's `decode(payload, context)` in a supervised Python worker
(backend `python_decoder`). A Python route applies while its pack is enabled, also when TShark is
disabled or not configured, and has a TShark route's place in Automatic mode. Automatic falls back
to JSON only when the decoder returns `None` (`decoder_not_dispatched`); explicit TShark ignores
Python routes. **Add Python decoder**, on both route tabs, lists the enabled packs' decoders as
"<pack name>: <decoder id>" (with the pack's ID when two packs share a name) and adds a route to
the one you pick; name its preview or protocol in the first column. Python's own parser decides
what it lists: the modules where `decode` may be a function after the top level. It reads each
module with `ast` and runs nothing. Helper modules a pack also lists are left out, and so are
modules Python cannot compile; the button's tooltip lists the latter with their line. The check
runs with the preview's Python interpreter when the packs change; without an interpreter the menu
stays empty and says why. A route's decoder cell says which pack the decoder belongs to, why its
module is not offered, or that its pack is not enabled (the route then does not apply). A preview's
tooltip and status show the decoder with its pack's name (E16b).

Configure preview routes by existing preview definition name, and protocol
routes by assembled protocol name. Each selects a named dissector and may
specify its expected protocol; protocol routes also select input encoding.
`defaultDissector` starts empty. Set it deliberately, including `data` if a
catchall is wanted; otherwise configure selective routes. Existing JSON
definitions can extract carrier text for Lua, and can also provide a fallback.
TShark and the selected Lua own protocol semantics.

**Assembly HTTP hosts** (`assemblyHttpHosts`) lists the `Host` header values
that mark an HTTP request as a message carrier for
[reassembly](../preview-message-tooltips.md). It holds up to 16 names of
printable ASCII without spaces or commas (a port may follow a colon), and the
comparison ignores case. The empty default accepts any host. Message assembly
uses the list even while the decoder is disabled.

The preview pane offers **Automatic (TShark first)**, **TShark/Lua**, and
**JSON**. Auto first tries an assembly protocol route, then mapped matching
carriers even when an earlier JSON-only definition matches. It next tries the
configured default on a carrier or directly on input. Disabled Auto retains the
legacy JSON behavior. Explicit TShark with **Dissector from route / settings**
finds the dissector the same way, among TShark routes only: the assembly
protocol's route, then the route of a matching preview, then the default
dissector on the input. Unlike Automatic, a matching preview without a TShark
route does not lend its carrier: the default dissector then decodes the whole
input. When a TShark-mapped preview's carrier cannot be taken from the line,
that failure is the result; explicit mode no longer decodes the whole input
instead. A named dissector decodes the input directly, as UTF-8, hex or
base64. JSON explicitly selects the legacy renderer. The request budget
is available in every mode, defaults to 3000 ms and is bounded to 100-10000 ms.
Its displayed value covers selection, startup and delivery, including Automatic
routes; a JSON fallback after a TShark/Lua failure may add up to 3 s (see
below). A cold engine may need a larger budget; increase it and run
again while keeping Automatic selected. Hover keeps its fixed 3000 ms budget.

Auto falls back to JSON when no TShark route/default applies, and after any
TShark/Lua failure: a Lua error, a timeout, a dissector that is missing or
decodes nothing, a protocol it does not produce, input the route cannot carry,
or an engine failure, busy engines (`preview_busy`) included. The fallback
shows the JSON preview of the carrier the route was for, also after a Python
decoder returns `None`. A user's cancel or a settings change during the run is
not a failure. Neither is a failure of the selector process itself (the
worker that finds the carrier before any decoder runs: its timeout, crash or
protocol error), as opposed to a route's input it could not prepare: the JSON
preview needs that process as well, so its failure is the result. Results
expose the actual backend, `preferredBackend`, `decoderAttempts` and
`fallbackReason` (the TShark/Lua failure), and the preview pane's status names
it. The engine keeps the whole budget; the JSON fallback gets what is left of it, and at least 3 s, so after a TShark/Lua
timeout the JSON result arrives up to 3 s after the budget. Explicit TShark
does not fall back: its failure is the result; definitions it cannot use leave
it the default dissector on the input. A completed processing
result does not establish protocol integrity. Engine offsets refer to decoded
buffers and have unresolved source mapping, not guaranteed positions in the
original log.

A TShark/Lua result holds up to 2048 rows and up to its result limit in bytes:
1 MiB, or what the pane's
**Result limit** (128 to 4096 KiB) or a request's `resultLimitKiB` sets for one
run. A result cut off at either limit is `limited` (`reason`
`rendered_field_limit` or `response_limit`) and names it in `limit`: `name`
(`rows` or `bytes`), `maximum`, `shownRows`, `totalRows` and, for bytes,
`neededBytes`. The pane's status and the hover say the same, for example
"390 of 769 rows: result limit 128 KiB, the whole result needs 263 KiB"; raise
the result limit and run again to see the rest.

The worker reads up to 8192 items and 4 MiB of TShark's PDML for one message
(#198). A larger tree keeps the part read and shows its first rows, in
preorder, up to 2048. The result is `limited` (`reason` `pdml_item_limit` or
`pdml_byte_limit`) and its `limit` has `name` `pdmlItems` or `pdmlBytes`,
`maximum` (8192 items or 4194304 bytes), `shownRows` and `readRows`, the rows in
the part read: the whole tree has at least that many, but its size is not
known. The status says, for example, "2048 of at least 7900 rows: the worker
reads 8192 items of TShark's output". The worker reads and drops the rest of
TShark's output for that message, so the engine stays warm for the next one. If
the result limit then cuts the rows again, the `bytes` limit counts the rows
read as `totalRows` and names the PDML limit in `tree`. A message whose part
read holds no row to show (no decoded row, or not the selected protocol) fails
with that reason; like any failure, it restarts the worker for the next
message. A packet is cut when its bytes up to and including `</packet>` pass
4 MiB, so a tree cut within its last few bytes may already show every row. A Lua error item past the
part read is not seen; a Lua error TShark reports on its diagnostics still fails
the preview. A tree deeper than 32
items fails (`depth_limit`), and so do TShark diagnostics past 64 KiB
(`diagnostic_limit`); the status names the limit, and Automatic mode then shows
its JSON preview, as after any TShark/Lua failure (#182).

A TShark/Lua tree shows each item as Wireshark does. **Value** is the
dissector's display text: the PDML `showname` after its `label: `, so a Lua
value string or appended text reads as in Wireshark. **Raw** keeps the raw
bytes, or the raw `show` value when the item has none. A protocol keeps its
whole `showname`. An item Lua adds with only a text label (PDML name
`_ws.lua.text`) is named by that text and has no Value or Raw. When Wireshark
cut a label off (`[…]` after the field name) or cut a value off (a prefix of
the `show` value ending in `…`), Value keeps the whole raw `show` value
instead. These rules read only the text, so a label a dissector sets itself
(for example with `set_text`) that ends the same way is treated alike. PDML
does not carry a field's registered name, so Value starts after the first
`: `; a field whose name itself contains `: ` keeps the rest of its name in
Value. The
Name, Value and Raw tooltips show the cell's whole text and then the item's
PDML attributes, so a name or value the column cuts off can still be read.

The same configuration and preview service are available through Commander CLI
and native MCP. Supply configuration directly, not inside a wrapper:

```text
cilogg command --action get_preview_decoder
cilogg command --action set_preview_decoder --json-file decoder-settings.json
cilogg command --action show_preview_decoder_settings
cilogg command --action preview_message --analysis-json "{\"text\":\"4142\",\"decoder\":\"tshark\",\"dissector\":\"data\",\"inputEncoding\":\"hex\",\"timeoutMs\":10000}"
```

For native MCP call `cilogg_execute` with `action: "set_preview_decoder"` and
`arguments: {"analysis_json": <configuration object>}`. For a preview use
`action: "preview_message"` and put the same preview object in `analysis_json`.
Executable and Lua paths belong only in trusted settings, never preview request
arguments or imported JSON preview definitions.

The [local validation record](tshark-preview-validation.md) documents the tested
Windows/tool versions, result counts, preserved failures and qualification limits.
Other versions, hosted CI, platforms and release packages need separate checks.

The following captures show the actual native pane and hover for a public
synthetic EHCI message. Its `CRC0000` is intentionally invalid, as reported by
the external Lua dissector. The Automatic pane capture explicitly selects the
supported 10000-ms budget; hover retains its default 3000-ms budget. These
captures passed the final 29-assertion external EHCI GUI check.

![EHCI preview pane](images/md03a-ehci-pane.png)

![EHCI hover tooltip](images/md03a-ehci-hover.png)

For separately qualified external EHCI scripts, the [MD01e capability profile](md01e-ehci-capabilities.md)
checks partial observed fields and exact NUL bytes through this same service.
Use a matching trusted pack; the original pinned script retains its documented
failures. Direct partial-message support does not change source-incomplete
assembly handling. Raw hex is authoritative for embedded NUL values.
