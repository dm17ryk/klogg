# Split panes in the main window (U16) — design for approval

**Status:** proposal, 2026-11-13. Nothing is implemented until the user approves it
(decision D12 in [docs/workbench/decisions.md](workbench/decisions.md)).

**Request (2026-11-13):** "can we make main window splittable, like we run 2 and more ciloggs in
parallel. so every splitted part will have log files tabs and filtered part".

## What the user gets

- One CILogg window can be split into two or more **panes**, side by side or stacked, in any
  nesting (for example two panes left and right, the right one split again top and bottom).
- Each pane is what a CILogg window shows today: its own row of file / COM / URL tabs, and for every
  tab its own log view, search line and filtered views. Panes never share a tab.
- Splitter handles resize the panes. The layout is saved with the session and the project.
- With a single pane, nothing changes compared with today.

## Using it

| Action | Where |
|---|---|
| Split the current pane right / down | View menu (`Split Right`, `Split Down`), pane tab-bar context menu, shortcuts proposed `Ctrl+Alt+Right` / `Ctrl+Alt+Down` |
| Move a tab to another pane or to a new pane | Tab context menu `Move to Pane ▸` (lists panes, plus `New Pane Right` / `New Pane Down`); dragging a tab onto another pane's tab bar |
| Open a file / COM port in a pane | Into the active pane; dropping files on a pane opens them there |
| Close a pane | View `Close Pane`: its tabs move to the neighbouring pane (nothing is closed or disconnected) |
| Equalize sizes, unsplit all | View `Equalize Panes`, `Merge All Panes` (all tabs back into one pane) |

Limit: 8 panes per window, to keep each pane usable.

## Focus: what "current tab" means

- Every window has one **active pane**: the pane where the user last clicked or typed. It gets a thin
  accent frame.
- Everything that acts on "the current tab" today acts on **the current tab of the active pane of
  the active window**: menus and toolbar, Quick Find, encoding, follow, save, Commander requests
  without a tab selector, and the "focused tab" choice of the U15 source picker.
- Tool docks (Regex Lab, Value Counts, Actions/Responses, plugin views) stay per window. They follow
  the active pane and, with U15, can be pointed at any tab of any pane.
- `Ctrl+Tab` and tab shortcuts work within the active pane.

## Commander, CLI and native MCP

Tab IDs are already UUIDs that are unique across windows, so every existing request keeps working:

- `--tab-id` is unchanged.
- `--window-index` with `--tab-index` keeps its meaning: an index into the window's tabs, counted
  pane by pane in layout order. Scripts written for one pane behave the same.
- New optional `--pane-index` (or `pane_id`): with it, `--tab-index` counts within that pane.
- `get_info` / `cilogg_state` add per window `panes: [{paneId, paneIndex, orientationPath,
  tabIds, currentTabId}]` and `activePaneId`; every tab entry gains `paneId` and `paneIndex`.
- New operations: `split_pane` (pane or tab, `right` | `down`), `move_tab` (tab id to a pane id or a
  new pane), `focus_pane`, `close_pane`, `get_layout` / `set_layout`. `focus_tab` also activates the
  tab's pane. `open_file`, `open_com` and `open_url` accept an optional `--pane-index`.
- The same operations are listed by `cilogg_commands` and reachable from Python, as D04 requires.

## How it is built

- `MainWindow`'s central widget becomes a small tree of `QSplitter`s whose leaves are `LogPane`
  widgets. Each `LogPane` owns one existing `TabbedCrawlerWidget` (unchanged) and a stable pane UUID.
- The 152 direct uses of `mainTabWidget_` move behind a few accessors: the active pane's tab widget,
  all tabs in layout order, and the pane that holds a tab id. Tab signals are connected per pane.
- Session and project files gain a versioned `layout` entry (pane tree, orientation, sizes, pane IDs,
  tab order). Files without it load into one pane; older CILogg ignores the entry and opens all tabs
  in one pane.
- U03 (investigation workspace) later embeds this layout in its workspace snapshot; U08 tab groups
  live inside a pane. Both keep addressing tabs by tab id.
- Moving a tab between panes reparents its `CrawlerWidget`; the log data, search, filtered views,
  COM capture, actions and scripts are not restarted.

## Delivery in slices

1. **U16a** — pane tree, split / close / merge, move tab through the menu, active pane, session and
   project restore. Tests: layout round-trip, moved tab keeps search, follow and capture, closing a
   pane keeps its tabs, old sessions load into one pane.
2. **U16b** — Commander / CLI / MCP / Python pane operations and the `get_info` additions, with
   parity cases in `tests/mcp/workbench_parity.py`.
3. **U16c** — drag a tab between panes and drop files on a pane.

## Alternatives considered

- **More windows** — already possible (File > New Window), but the request is one window with
  several views.
- **Each pane as a dock widget** — free-floating panes, but they would compete with the tool docks
  and make layout restore fragile.
- **QMdiArea** — overlapping sub-windows instead of a clean side-by-side layout.

## Questions for the user

1. When a pane is closed, should its tabs move to the neighbouring pane (proposed) or be closed?
2. Is "focused tab = current tab of the active pane" right for tools and CLI defaults?
3. Are 8 panes per window and the proposed shortcuts acceptable?
