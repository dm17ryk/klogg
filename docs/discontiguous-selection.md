# Discontiguous row selection (P19b)

The main and filtered log views support these gestures:

| Gesture | Result |
| --- | --- |
| Click | Replace the selection with one whole row |
| Drag text | Replace the selection with the existing text portion or continuous row range |
| Ctrl-click | Add an unselected row or remove a selected row |
| Shift-click | Replace the selection with the range from the anchor to the clicked row |
| Ctrl+Shift-click | Add the anchored range while retaining other selected rows |

Normal clicks and Ctrl-clicks set the anchor, including when a Ctrl-click removes
a row. Repeated Shift operations retain that anchor. Shift+Up/Down continues
from the same model anchor, including after Quick Find selects a match. A Ctrl-click
made during partial-text selection
starts a whole-row selection; partial text and row sets are separate modes.
Clicks in the mark gutter retain the existing marking behavior, including with
Shift or Ctrl+Shift.

Clicking any wrapped continuation acts on its physical source row. Copy emits
selected rows once in source-file order, preserving empty rows and the existing
platform line separators. Copy with line numbers uses physical source numbers,
including in a filtered view. Save selected exports only selected ranges, never
the unselected gaps between them. Marking and scratchpad commands use the same
selection. Closing/truncating a source and selecting an empty source do not
leave a row-zero selection behind.

Whole-row selection stores normalized inclusive intervals. A million-row range
does not require a million selection entries. Counting rows and ordinary arrow
navigation operate on intervals. Drag handlers continue to avoid reading or
assembling selected text. Ordinary text drags calculate multiline symbol counts
on release. Ctrl/Shift row gestures and Shift keyboard extension report row counts
only and do not materialize text or the X11 primary selection; explicit Copy and
Save still operate on the complete selection. Context-menu mark inspection walks
compact ranges and stops at the first unmarked row.

## CLI and MCP

The existing `activate_ui` operation accepts the same modifiers. Discover the
current view and its visible viewport child object path with `get_ui`, then
send this JSON gesture to that viewport:

```json
{"event":"click","x":100,"y":40,"modifiers":["Ctrl"]}
```

```powershell
cilogg command --action get_ui --require-running
cilogg command --action activate_ui --object-name '<viewport-path>' --json-file ctrl-click.json --require-running
```

The equivalent MCP request uses `cilogg_execute`:

```json
{
  "action": "activate_ui",
  "arguments": {
    "object_name": "<viewport-path>",
    "definition": {"event":"click","x":100,"y":40,"modifiers":["Ctrl"]}
  }
}
```

Use `["Shift"]` or `["Ctrl","Shift"]` for anchored ranges. Coordinates are
local to the viewport. Existing copy actions and clipboard inspection remain
available through CLI/MCP; no separate selection implementation is introduced.

## Verification

`tests/ui/logviewperformance_test.cpp` covers Ctrl toggles, anchored combinations,
keyboard continuation, empty sources, wrapped rows, compact interval operations,
blank-row/Unicode copy, mapped source numbering, and actual selected-file exports
with trailing ranges of 4,999, 5,000 and 5,001 rows. Existing drag regressions
continue to count source reads rather than assert timing-dependent thresholds.

The baseline Ctrl-click regression failed five assertions before implementation.
The focused `[selection]` suite passes 219 assertions across 13 cases. No measured
interactive latency or strict per-view memory claim is made by these tests.

Final local evidence (2026-10-18): MSVC/installed Qt 6.10.1 builds the application,
portable application, grep and both test binaries. Full CTest passes 4/4 in
514.47 seconds (`build_root/selection-review-full-ctest.txt`). An isolated native MCP
session and CLI commands verify Ctrl toggle/add/remove and Ctrl+Shift union with
real main/filtered-view clipboard text (`build_root/p03a-smoke/selection-parity`).
GCC 13 and Clang 23 accept changed sources/tests with CI warning flags; the local
checker retains its existing nonfatal Clang literal-operator spelling warning in
unchanged `linetypes.h`. Hosted CI remains separate from this local evidence.

Review follow-up covers completed Ctrl/Shift gestures without selected-text reads,
compact context-menu inspection, modified gutter marking, and Quick Find followed
by mouse or direct keyboard range extension. The export fixture enters its filename
in the visible dialog edit because Qt intentionally ignores selectFile() when that
edit has focus; a scoped watchdog prevents late rejection of subsequent dialogs.
