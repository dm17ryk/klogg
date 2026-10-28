# G03d: file-backed MainWindow navigation parity

The registered `file-navigation` case runs inside the existing private application
host. It is included in `--task G03`, and can be selected independently:

```text
python tests/mcp/workbench_parity.py --executable APP --task G03d --artifacts NEW_DIRECTORY
```

The runner uses the existing Commander CLI, `cilogg_commands` discovery and
`cilogg_execute` native MCP route. It adds no application commands, service,
settings identity or MCP catalog. Existing CI gates selecting G03 automatically
include this case on their selected accelerated and Qt-only builds.

The fixture has three physical UTF-8 rows, Hebrew, an emoji and an unterminated
tail. Its bytes use LF on every platform. Two files are opened in the private
MainWindow. CLI and MCP analyze the explicitly selected first tab while the second
tab is current. The case compares exact results, decimal-string request revisions,
the source identity/generation/revision/watermark, physical lines and raw text.
Provenance is checked separately before the existing normalization removes it.

The case then activates the actual Regex Lab Run control, compares its job with
both transports, toggles the dock's floating state, and activates result indices
through MCP and CLI. Each activation must return to the original source and
place the viewer on the expected one-based physical row. After closing that
source tab, navigation must return `stale_location`, preserve the other viewer's
location and retain the completed results. Owned jobs and tabs are released;
cleanup failures fail an otherwise successful case. Logs identify jobs/tabs,
decisions, timeouts and cleanup operations without logging source bodies.

The existing supervisor bounds the entire suite and reaps its process tree.
Observation polling uses a 15-second deadline with the existing Commander call
timeouts. These are test harness bounds; product worker/job budgets are unchanged.
Fixture and snapshot artifacts are retained on failure. They contain only the
small deterministic test body. Portable test doubles exercise rejected outcomes
and cleanup, and do not establish application behavior.

## Unicode serialization regression

The first application-level case returned `שלום 20` for the second row of
`שלום 😀20`, while its numeric capture still started at UTF-16 offset 7. The file
bytes, UTF-8 decoder and regex worker retained the emoji. The loss happened in
the CLI JSON serializer, which converted each `QChar` to UTF-8 separately.
Supplementary characters occupy two UTF-16 code units; converting each half in
isolation loses the character.

`jsonString` now escapes surrogate code units as JSON `\uXXXX` sequences, using
the same existing escape path as control characters. JSON consumers reconstruct
the pair. Object keys and string values share this helper. The integer-preserving
serializer, UTF-16 offsets, ordinary BMP text and public command schemas are
retained. The real file-backed case failed before this fix and passes afterward
through CLI, native MCP and the GUI Run operation. Its source and match offsets
are compared rather than accepting agreement between two equally lossy paths.

## Evidence boundaries and rollback

This is integration of the real MainWindow and docks using their semantic
automation controls under the offscreen Qt platform. It does not establish
physical input, compositor timing, native desktop exposure, screenshots, menu
pointer interaction, saved-layout restoration, packaged installer correctness,
or complete G02/G03 acceptance. G03c retains its separate native component probe.
Full G02/G03 remain open. Local validation uses the approved Qt 6.10.1 SDK; hosted
Qt 6.12 and other-platform results must be assessed separately.

Revert the registry case and its helper to remove the additional integration
gate. The independent Unicode fix can be retained; reverting it restores the
lossy output behavior. No user settings or source files are migrated.

Exact local evidence and remaining limits are recorded in
[the observations](g03d-windows-observations.json) and the canonical progress
ledger. Earlier unsuccessful harness attempts remain separate from passing runs.

The complete local CTest run passed 6/6, and MCP SDK 1.30.0 and 2.3.0 gates passed.
Review then strengthened the Python oracle to validate every raw row's source,
job ID and request revision before normalization, and to observe the other active
tab before analysis and navigation. All 77 Workbench self-tests pass on Python
3.9 and 3.14 (one platform skip each); the registered harness and a fresh complete
four-case application parity run pass after those changes. One preceding full
parity attempt lost an IPC reply in Value Counts before entering G03d. The request
executed according to the host log. It was not retried in place; the failure is
retained, the fresh isolated run passed, and the underlying cause remains open.
