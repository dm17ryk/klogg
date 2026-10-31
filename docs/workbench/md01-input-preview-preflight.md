# MD01 inputs and legacy preview preflight — 2026-10-31

This is read-only preparation for MD01. G02/W0 remain open; no TShark packet
dissection, Exported-PDU dispatch or Lua field-tree acceptance is claimed.
The [W0 audit](w0-acceptance-audit.json) pins script/capture identities and the
evidence boundaries. Local account, network and message bodies stay outside Git.

## Located log shapes

The supplied log folders and other text/log files beneath the supplied firmware
project were searched with ignore rules disabled. Searches explicitly treated
NUL-containing files as text. Three recent COM1 captures contain both EHCP and
EHCI; two also contain modem `SRING` wrappers. Their first matching physical
rows, and references in older logs, are recorded in the private local inventory.

Observed forms include quoted EHCP, unquoted EHCP, direct EHCI and
`SRING: <socket>,<length>,<hex>`. One observed wrapper declares 48 bytes; decoding
its hex yields exactly 48 bytes beginning with `EHCP`. This is a wrapper/byte
check, not full protocol validation or a substitute for the Lua oracle.

Literal `STRING` was searched separately. Its sampled hits include diagnostics
and ordinary text. It has not been established as an EHCP wrapper. Do not
silently treat it as an alias for `SRING` or change a user's profile accordingly.

Discovery is bounded: at most three rows per marker/file and files up to 1 GiB,
with logs/text/ASCII/CSV extensions. Build, managed-component and manual folders
were excluded from the broader search. Candidate substring hits can include
reports rather than actual messages. These are discovery results, not total
message counts or a complete corpus qualification. Current captures can append;
freeze the selected bytes and recheck identities before a reproducible test.

## Current implementation

1. Viewer **Send to Preview** emits selected text through
   `CrawlerWidget::sendToPreview`; `MainWindow::sendToPreview` opens a
   `PreviewMessageTab` in the preview window. This is a selected-text route,
   separate from the analysis service's frozen physical-row scopes.
2. `PreviewRepository::storagePath` places `previews.json` beside the active
   application settings. `PreviewManager` imports definitions by name, retains
   enable state when unspecified, and selects the first enabled matching
   definition for Auto. `PreviewConfigParser` compiles JSON regex strings
   directly with `QRegularExpression`.
3. `PreviewMessageTab::renderPreview` matches the supplied text, extracts
   `bufferCapture` (or uses the supplied text), decodes the configured buffer,
   evaluates offsets and recursively adds field items.
4. `decodeBytesFromText` supports hex and base64. Hex normalization accepts
   whitespace/underscores and a leading `0x`; odd digit counts are padded on the
   left. Preserve that legacy behavior in its adapter; a strict decoder input
   contract must be specified separately rather than inferred from it.
5. `applyMatchStage` can decode a field, run a nested regex and create a child
   capture/buffer context. Fields support offsets/width expressions, enum/flag
   values, bitfields and named reusable blocks. Block templates can depend on
   prior values; missing blocks and cycles have explicit error paths. Parsing
   and error presentation currently create `QTreeWidgetItem` directly.

Relevant sources:

- [Viewer wiring](../../src/ui/src/crawlerwidget.cpp),
  [window entry](../../src/ui/src/mainwindow.cpp),
  [message renderer and parser](../../src/ui/src/previewmessagetab.cpp).
- [Definition parser](../../src/ui/src/previewconfigparser.cpp),
  [repository](../../src/ui/src/previewrepository.cpp),
  [manager](../../src/ui/src/previewmanager.cpp),
  [decode helpers](../../src/ui/src/previewdecodeutils.cpp).
- [Existing nested SRING/direct-EHCP fixtures](../../tests/unit/previewmatchstage_test.cpp).

## Local profile findings and later migration

The user settings and a saved profile copy each contain ten preview definitions
and 198 blocks. They include direct quoted/unquoted EHCP and SRING-to-EHCP
definitions; no dedicated EHCI preview is present in those profiles. The SRING
definition captures hex text, then references a hex-decoding block which shares
the ordinary EHCP field block. The saved regex also contains doubled backslashes
before whitespace/digit tokens; keep this as a profile-compatibility diagnostic
and verify actual match outcomes separately from per-line result-record counts.
An isolated `regex_evaluate` diagnostic matched the sample with both the stored
pattern and a single-escape variant. That public analysis path does not directly
exercise `PreviewConfigParser`/`PreviewMessageTab`; no legacy-renderer escaping
bug or full preview success is established by this diagnostic.
Neither profile was modified. These inventories do not prove which profile an
arbitrary running portable instance loaded.

Commander discovery did not advertise a preview-specific semantic command for
the connected application. Generic GUI automation is a separate surface. MD03
should extract QWidget-independent parsing and retain a legacy JSON adapter;
the decoder tasks must use the existing Commander/CLI/native-MCP route. Do not
add another command catalog or replace saved previews with Lua definitions.

The MD01 proof must still pin licensed Lua sources and complete sanitized
captures, preserve wrapper-to-decoded-byte provenance, qualify malformed and
truncated records, compare complete/partial field trees and verify TShark
dispatch, cancellation and process cleanup. MD07/MD08 remain deferred.
