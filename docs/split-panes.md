# Split panes in the main window (U16) — approved design

**Status:** approved by the user on 2026-11-13 with the decisions below (decision D13 in
[docs/workbench/decisions.md](workbench/decisions.md)). Implementation is next, in slices U16a, U16b
and U16c; nothing is implemented yet.

**Request (2026-11-13):** "can we make main window splittable, like we run 2 and more ciloggs in
parallel. so every splitted part will have log files tabs and filtered part".

## Decisions (user, 2026-11-13)

1. **Closing a pane that has tabs** asks for confirmation with three choices: "Close all its tabs",
   "Move them to the neighbouring pane", or Cancel. A pane without tabs closes without asking.
2. **Focused tab** is the current tab of the active pane of the active window, for menus, tools, the
   U15 source picker and Commander/CLI defaults.
3. **Limits:** up to 8 panes per window; shortcuts `Ctrl+Alt+Right` (split right) and
   `Ctrl+Alt+Down` (split down). The number of tabs and files per pane is not limited, as today with
   one pane.

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
| Split the current pane right / down | View menu (`Split Right`, `Split Down`), pane tab-bar context menu, `Ctrl+Alt+Right` / `Ctrl+Alt+Down` |
| Move a tab to another pane or to a new pane | Tab context menu `Move to Pane ▸` (lists panes, plus `New Pane Right` / `New Pane Down`); dragging a tab onto another pane's tab bar |
| Open a file / COM port in a pane | Into the active pane; dropping files on a pane opens them there |
| Close a pane | View `Close Pane`: with tabs, a confirmation offers "Close all its tabs", "Move them to the neighbouring pane" or Cancel |
| Equalize sizes, unsplit all | View `Equalize Panes`, `Merge All Panes` (all tabs back into one pane) |

A ninth split in one window is refused with a status message; a pane holds any number of tabs.

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
  new pane), `focus_pane`, `close_pane` (`tabs: close | move`; without it a pane with tabs is
  refused, so automation never triggers the confirmation dialog), `get_layout` / `set_layout`.
  `focus_tab` also activates the tab's pane. `open_file`, `open_com` and `open_url` accept an optional
  `--pane-index`.
- The same operations are listed by `cilogg_commands` and reachable from Python, as D04 requires.

## How it is built

- `MainWindow`'s central widget becomes a small tree of `QSplitter`s whose leaves are `LogPane`
  widgets. Each `LogPane` owns one `TabbedCrawlerWidget` and a stable pane UUID.
- The 152 direct uses of `mainTabWidget_` move behind a few accessors: the active pane's tab widget,
  all tabs in layout order, and the pane that holds a tab id. Tab signals are connected per pane.
- **Moving a tab keeps everything it owns.** `TabbedCrawlerWidget::addCrawler` creates a new tab
  UUID and new signal connections, and `removeCrawler` drops the tab's `streamSessions_` entry and its
  tab-bar metadata (COM connection, Actions port, script activity), so they cannot move a tab. U16a
  adds a transfer API to `TabbedCrawlerWidget`:
  - `detachTab(index) -> DetachedTab` removes the tab without closing it and returns the
    `CrawlerWidget`, the complete tab-bar data (tab id/UUID, path, display name, COM, Actions-port and
    script-active metadata, data status) and the tab's `streamSessions_` entry, after disconnecting
    the tab's signals from the source pane;
  - `attachTab(DetachedTab, index)` inserts it into the target pane with the same tab id, the same
    metadata and stream session, and connects its signals there.
  The log data, search, filtered views, COM capture, actions and scripts are not restarted, and the
  tab's `--tab-id` stays valid.
- Session and project files gain a versioned `layout` entry (pane tree, orientation, sizes, pane IDs,
  tab order). `SessionInfo::saveToStorage` / `retrieveFromStorage` persist it with the session, and
  `ConfigurationExport::saveProject` / `readProject` write and validate it in projects. Files without
  it load into one pane; older CILogg ignores the entry and opens all tabs in one pane. Migration
  cases cover old sessions and old projects.
- U03 (investigation workspace) embeds this layout in its workspace snapshot, so U03 depends on U16;
  U08 tab groups live inside a pane. Both keep addressing tabs by tab id.

## Delivery in slices

1. **U16a** — pane tree, split / close (with the confirmation) / merge, move tab through the menu with
   the tab transfer API, active pane, session and project restore with migration. Tests: layout
   round-trip, a moved tab keeps its id, search, follow, capture, actions and scripts, closing a pane
   asks to close or move its tabs, old sessions and projects load into one pane, the ninth pane is
   refused.
2. **U16b** — Commander / CLI / MCP / Python pane operations and the `get_info` additions, with
   parity cases in `tests/mcp/workbench_parity.py`.
3. **U16c** — drag a tab between panes and drop files on a pane.

## Alternatives considered

- **More windows** — already possible (File > New Window), but the request is one window with
  several views.
- **Each pane as a dock widget** — free-floating panes, but they would compete with the tool docks
  and make layout restore fragile.
- **QMdiArea** — overlapping sub-windows instead of a clean side-by-side layout.
