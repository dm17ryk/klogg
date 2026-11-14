# Plugin views: design

Status: **implemented** (2026-11-11), protocol version 1 notifications. Plan task:
[E13](../plans/cilogg-workbench-2026-10-21/tasks/E13.md). Editable cells, the `multichoice` field and the
`toolbar` and `icon` keys were added by [E14](../plans/cilogg-workbench-2026-10-21/tasks/E14.md); the
protocol version stays 1. Section 8 lists the details that the
implementation fixed or changed relative to the original draft.

A plugin view is an interactive window that a process plugin builds and drives: input fields,
buttons, a progress bar and a large table, kept alive while the user works. A view can be any
window kind CILogg itself uses:

- a **dock** on any side, or floating, like Regex Lab or Value Counts;
- a top-level **window**, like Script Runner or Preview;
- a **dialog**, modal or modeless.

Plugins can also show **message boxes**, with an optional auto-close timeout. Views extend today's
plugin panels; existing plugins keep working unchanged.

## 1. Why panels are not enough

Today's panels (`src/ui/src/pluginuicontroller.cpp:644-735`) have these limits:

- They always open in the bottom dock area.
- Every `panel/update` rebuilds the whole widget tree, so scroll position, selection and focus are
  lost on each progress tick.
- There are no input fields. File paths, options and choices can only come from the action form,
  before the job starts.
- Buttons are stacked one per row, with no enabled state, no confirmation and no file dialog.
- Tables are read-only and limited to 4,096 cells (1,000 rows), with no sorting, filter, row
  colours or selection.
- Panels belong to a job. After the job ends or is cancelled, its updates are ignored
  (`src/plugins/src/pluginmanager.cpp:931`).
- A plugin cannot open a window or ask the user anything during a job.

## 2. Model

- **Declaring views.** A plugin declares views in `plugin.json`. Each view with `menu` gets a
  **Plugins → <title>** entry.
- **Opening a view.** The view is created in the current CILogg window, the plugin is started if
  needed, and `view/opened` is sent. A view opens from its menu entry, or when the plugin asks with
  `view/show`, for example a dialog popped up by a running job.
- **Content.** The plugin answers with the element tree in `view/update`. Later updates are
  **patches** by element id, so the host keeps its widgets and their state.
- **Input.** User input goes to the plugin as `view/event` notifications. A button can instead
  start a plugin **action** as a normal job, with the current input values attached. CLI and MCP
  callers run the same actions, so they get the same behaviour as the GUI.
- **Message boxes** need no declaration: `ui/message` shows one, and `ui/message_result` returns
  the answer.
- **Protocol.** It stays version 1. Every new message is a notification, and the plugin still
  never sends requests to the host, so the current security model is unchanged.

### 2.1 Manifest

```json
"views": [
  {"id": "main", "title": "My Tool", "kind": "dock", "area": "right", "menu": true},
  {"id": "editor", "title": "Editor", "kind": "window", "width": 900, "height": 600},
  {"id": "confirm", "title": "Confirm write", "kind": "dialog", "modal": true, "menu": false}
]
```

| Key | Values | Default | Notes |
| --- | --- | --- | --- |
| `id`, `title` | as for actions | — | |
| `kind` | `dock`, `window`, `dialog` | `dock` | |
| `area` | `left`, `right`, `top`, `bottom` | `right` | Docks only: the starting side. The user can move the dock; the host remembers its side, size and floating state per view. |
| `floating` | bool | false | Docks only: start as a floating dock window. |
| `width`, `height` | pixels | the content's own size | Starting size for windows, dialogs and floating docks. The host remembers the user's size. |
| `modal` | bool | false | Dialogs only: modal to the CILogg window. |
| `menu` | bool | true | Adds the **Plugins → <title>** entry. |
| `toolbar` | bool | false | Adds a button for the view to the main toolbar. |
| `icon` | relative path | the generic icon | Icon of the menu entry and the toolbar button. See below. |

- A **window** is a top-level window owned by its CILogg window, with its own title, like Script
  Runner.
- A **dialog** gets the standard dialog frame. Closing it sends `view/closed`.
- All kinds use the same elements, patches and events.
- **Actions** accept the same presentation keys: `toolbar` (default true), `menu` (default true) and
  `icon`. An action with neither `toolbar` nor `menu` gets no button, but it stays callable from
  Commander, the CLI and MCP.
- `icon` is a safe relative path to a `.png`, `.ico` or `.svg` file inside the plugin folder, at most
  256 characters; anything else is a manifest error. A missing or unreadable file falls back to the
  generic icon and is logged once per plugin and path. `.svg` icons need Qt's SVG image plugin in
  the deployment; without it the generic icon is used. A plugin update that replaces an icon file
  shows the new icon.
- The host announces support in `initialize` with `"host_features": ["views", "view_edit"]`.
  `views` covers the views described above; `view_edit` covers editable cells (section 2.5) and the
  `multichoice` field (section 2.3). On an older host, a plugin falls back to actions and panels, or
  to read-only tables.

### 2.2 Messages

| Direction | Method | Parameters |
| --- | --- | --- |
| host → plugin | `view/opened` | `view_id`, `window_id`, `context` (as in `invoke`) |
| host → plugin | `view/closed` | `view_id`, `window_id` |
| host → plugin | `view/event` | `view_id`, `window_id`, `element_id`, `type` (`changed`, `clicked`, `selected`, `edited`), `value`, `inputs` (all field values of the view). An `edited` event also carries `row_id`, `column_id` and `previous` (section 2.5). |
| plugin → host | `view/update` | `view_id`, `window_id` (optional), and either `root` (a full element tree) or `patch` (a list of `{id, set: {...}}`, plus table row operations) |
| plugin → host | `view/show` | `view_id`, `window_id` (optional; default is the most recently active CILogg window), and optional overrides `area`, `floating`, `title`, `focus` |
| plugin → host | `view/hide` | `view_id`, `window_id` (optional; default is every window) |
| plugin → host | `ui/message` | `message_id`, `window_id` (optional), `title`, `text`, `icon` (`info`, `warning`, `error`, `question`), `buttons` (from `ok`, `cancel`, `yes`, `no`, `retry`, `abort`, `ignore`, `close`), `default`, `timeout_ms` (optional), `modal` (default true) |
| host → plugin | `ui/message_result` | `message_id`, `button`, `timed_out` |
| plugin → host | `ui/close_message` | `message_id`. It closes an open box; the result reports `button: "closed"`. |

- `view/update` is accepted at any time while the plugin runs; it is not tied to a job. Without a
  `window_id`, it applies to the view in every window where it is open.
- `view/show` for a view that is already open raises and focuses it.

### 2.3 Elements

Common properties: `id` (unique within the view), `enabled`, `visible`, `tooltip`.

| Type | Properties | Events |
| --- | --- | --- |
| `column`, `row` | `children` | — |
| `group` | `title`, `collapsible`, `collapsed`, `children` | — |
| `label` | `text`, `tone` (`normal`, `ok`, `warn`, `error`, `muted`), `bold` | — |
| `badge` | `text`, `tone` | — |
| `field` | `kind` (`text`, `number`, `choice`, `multichoice`, `boolean`, `file_open`, `file_save`, `directory`), `label`, `value`, `choices`, `filter` (for example `CPF files (*.cpf)`), `placeholder` | `changed` (text is debounced by 300 ms). File kinds get a built-in **Browse…** button. `multichoice` is a check-dropdown whose `value` is a list of the checked choice values. |
| `button` | `label`, `tone` (`normal`, `primary`, `danger`), `action` + `arguments`, `event` (true = send `clicked` instead of starting an action), `confirm` (`title`, `text`, `ok_label`), `save_dialog` (`filter`, `suggested_name`) | `clicked`, or an action invocation. The invocation's arguments carry `inputs` (all field values) and `save_path` when a save dialog ran. |
| `progress` | `value` (0..1) or `busy`, `text` | — |
| `table` | `columns` (`id`, `title`, `width`, `editable`), `rows` (`id`, `cells`, `tone`, `tooltip`, `editors`), `sortable`, `filter_box`, `selection` (`none`, `single`, `multi`) | `selected` (row ids) |

- Tables are model-based (`QAbstractTableModel`), with up to 50,000 cells.
- Table patches can replace all rows, or update, insert and remove rows by id. A large table is
  therefore sent once and then changed row by row.
- Row `tone` is one of `normal`, `ok`, `warn`, `error`, `muted` or `accent` (blue, `#1565c0`).
- Text is always plain text, never HTML. This includes tooltips.

#### `multichoice` fields

A `multichoice` field is a check-dropdown built from the same `choices` as a `choice` field. Its
`value` is a list of the checked choice values, in `inputs` and in `changed` events. Plugin patches
that set the value never echo back, as for other fields.

- Closed, it shows the labels of the checked items, `None` when nothing is checked, or `All` when
  every item is.
- Clicking an item toggles it and keeps the list open. Space toggles the current item.
- Esc closes the list and restores the ticks the list had when it was opened. Any other way of
  closing it keeps the ticks.
- The field sends `changed` on each toggle while the list is open, and again when Esc restores
  different ticks. Opening and closing the list without a toggle sends nothing.
- List values count toward the bound on `inputs` (section 8).

### 2.4 Message boxes

- The host shows a standard `QMessageBox` with the given icon, text and buttons, and returns the
  pressed button in `ui/message_result`.
- **Timeout.** With `timeout_ms`, the default button counts down in its label (`OK (5)`). At zero
  the box closes with the default button, and the result reports `timed_out: true`. Any user input
  on the box (a key press or a click) stops the countdown.
- `modal: false` shows a non-blocking box. A plugin may have up to 8 boxes open at once.
- Boxes still open when the plugin stops are closed without a result.
- Text is plain text, at most 4,096 characters.

### 2.5 Editable cells

A plugin can make table cells editable. The host never changes data on its own: it reports the
edit, shows it as pending, and waits for the plugin to confirm or reject it. The feature is announced
by the host feature `view_edit`.

- **Declaring.** Mark a column with `"editable": true`. Each row that should be editable in that
  column carries `editors`, a map from column id to an editor:
  `{"kind", "choices", "min", "max", "max_length", "placeholder", "value"}`. A cell is editable only
  when its column is `editable` and its row has an editor for that column, so rows may use
  different editors in one column.

  ```json
  {"id": "r1", "cells": ["Mode", "Fast"],
   "editors": {"value": {"kind": "choice", "choices": ["Fast", "Slow"], "value": "Fast"}}}
  ```

- **Editor kinds and value types.**

  | `kind` | Editor | `value` |
  | --- | --- | --- |
  | `text` | line edit (`max_length`, `placeholder`) | string |
  | `number` | number edit (`min`, `max`) | string |
  | `choice` | drop-down list | the choice's own value as given in `choices` (a string in practice, as for form fields) |
  | `multichoice` | check-dropdown | list of the checked choice values |
  | `boolean` | Yes/No drop-down list | `true` or `false` |

  `previous` is always the cell's text before the edit, a string, for every kind.
  A number editor accepts standard notation only, without group separators, within `min` and `max`.
  The `value` key of an editor is its starting value (a list for `multichoice`).
- **Opening and closing.** Double-click or F2 opens the editor. `choice`, `boolean` and `multichoice`
  editors open their list at once. Picking an item in a `choice` or `boolean` list commits it;
  closing that list without picking (Esc or a click outside) ends the edit unchanged. If the cell
  text matches none of a `choice` editor's choices (for example an out-of-range value) and the editor
  has no `value`, the list opens with nothing selected, and any pick commits. Closing a
  `multichoice` list with Enter or a click outside commits its ticks; Esc restores the ticks from
  before it opened and ends the edit unchanged. For text and number editors, Enter or leaving the
  cell commits and Esc cancels. Opening and leaving a cell without changing it sends nothing.
  Whether a commit is a change is judged by value, not by the text the cell shows, so choices may
  share a label. A cell whose edit the plugin has not confirmed yet reopens on that edit's value.
- **Pending state.** On a real change the cell shows the new text at once, in italic (a `choice` or
  `boolean` cell shows the choice label), and the host sends a `view/event` of type `edited` with
  `element_id` (the table), `row_id`, `column_id`, `value`, `previous` and `inputs`. If the host
  cannot deliver the event (the plugin is stopping or not reading its input), the cell shows what it
  showed before that edit again. An event over the size limit is still delivered, shortened as
  section 8 describes (`inputs` first, then `value`, with `truncated: true`).
- **The plugin answers with a patch.** To confirm, patch the row with the accepted cell text. To
  reject, patch it with the previous text, or whatever it should show. Any patch to the row ends its
  pending state. A patch to a row whose editor is open leaves the editor alone once the user has
  changed its value.
- **Editors in a patch.** A row patch with `editors` replaces the row's whole `editors` map, so it
  must carry the editor of every column that should stay editable, not only the changed one.
- **Editor value after a confirm.** If the confirming patch carries no `editors` and its cell text
  equals the pending text, the host updates that editor's `value` to the committed value itself.
  Otherwise a plugin whose editor carries `value` should send the row's `editors` again (all of
  them), with the new `value`, in its confirming patch.
- **Refused edits.** Edits are refused while the view is disabled or applying a plugin update.
- **Plugin stops.** If the plugin stops while an edit is pending, the host sets the cell back to its
  previous text.
- **Limits.** A cell editor has at most 512 choices (`PluginView::MaxElements`). Text and number
  editors take at most 4,096 characters, as many as a cell shows; a larger `max_length` is clamped.
  Tooltips are at most 4,096 characters.

## 3. Limits

- The host logs ids and sizes, not element contents, to keep logs small.
- Limits: 8 views per plugin, 512 elements per view, 8 open message boxes per plugin, 1 MiB per
  message, 512 choices per cell editor, and patches coalesced to at most 10 renders per second. Unknown element types and
  properties are ignored and logged once.
- A button with `confirm` always shows the host's modal dialog before anything is sent to the
  plugin.

## 4. Lifecycle

- **CILogg window closed:** its views close, and `view/closed` is sent for each.
- **Plugin stops or fails:** its views stay visible but disabled, with a "plugin stopped" note.
  Its message boxes close.
- **Plugin restarts:** `view/opened` is sent again for every view that is still open.
- **Plugin disabled or removed:** its views close.

## 5. Automation

Each view and element gets an object name, `pluginView_<plugin>_<view>` and
`pluginView_<plugin>_<view>_<element>`. Message boxes get `pluginMessage_<plugin>_<message>`.
The existing UI automation (`get_ui`, `set_ui`, `activate_ui`, also through MCP) can therefore
fill fields, press buttons and answer message boxes, as in native windows.

## 6. Related small host changes

- **Executable path for plugins.** Pass `CILOGG_EXECUTABLE` (the running `cilogg.exe`) and
  `CILOGG_INSTANCE_ID` in the plugin environment, so a plugin can call Commander on its own
  instance.
- **Cancel grace period.** After `cancel_plugin_job`, keep the job in a `cancelling` state for up
  to 10 s, and still accept its `progress`, panel and final result. A plugin can then report its
  cleanup.
- **Blocking pause.** `pause_comm --wait` returns only after the port is closed
  (`streamsession.cpp:119-190` queues the worker stop today).

## 7. SDK and tests

- **Python SDK:**
  - builder helpers for elements;
  - `update` / `patch`;
  - `show_view` / `hide_view`;
  - `message(...)`, with a result callback or a blocking wait that has a timeout;
  - `on_view_opened` / `on_view_closed` / `on_view_event` callbacks, run on worker threads like
    jobs.
- **Example plugin** `plugins/examples/view-demo`, disabled by default:
  - a dock with a form, file fields, a job button with progress, a confirm button and a
    10,000-row table with patches;
  - a window and a dialog view;
  - message boxes with and without a timeout.
- **Tests** in `tests/ui/workbench_e13_test.cpp` (`[workbench][E13]`) and
  `tests/unit/plugins_test.cpp`:
  - manifest validation (kinds, areas, sizes);
  - rendering every element type in every view kind;
  - patches keeping scroll position, selection and field text;
  - events and inputs;
  - button jobs, confirm and save dialogs;
  - `view/show` and `view/hide`;
  - message boxes: result, timeout, closing, limit;
  - limits and unknown types;
  - the lifecycle;
  - the environment variables, cancel grace and `pause_comm --wait`.
- **Python SDK tests** under `tests/plugins/`. `tests/plugins/host_integration.py` drives
  `view-demo` through GUI automation, CLI and native MCP.

## 8. Implementation notes

Code: `src/plugins/src/pluginmanager.cpp` (protocol, open-view registry, message limits),
`src/ui/src/pluginview.cpp` (element renderer), `src/ui/src/pluginviewhost.cpp` (containers
and message boxes of one CILogg window) and `src/ui/src/pluginuicontroller.cpp` (menu entries).

- **Ids.** View and element ids follow the action id rules (non-empty, at most 128
  characters, unique). Elements without an `id` (for example a layout `column`) are rendered but
  cannot be patched and have no object name. Ids starting with `#` are reserved and treated as
  id-less. Duplicate ids are ignored and logged once.
- **`view/update` root.** When the new tree has the same types, ids, field kinds and nesting as
  the current one, it is applied in place, and id-less elements are matched by position. The
  root describes each element completely: a property it no longer sends returns to its default
  (`enabled`/`visible` true, empty text and tooltip, `normal` tone, not collapsed, no `action`,
  `arguments`, `event`, `confirm` or `save_dialog`). Three properties keep their state when
  absent: a field `value` (changed only when it differs from the value the plugin sent last, so
  typed text survives), and a table's `rows` and `columns`. Unchanged `rows` are not reloaded.
  Any other tree is rebuilt.
- **Patches.** `{"id": "x", "set": {...}}` changes properties; `children` and a field's `kind`
  cannot be patched. Table entries are `{"id": "t", "replace_rows": [...]}`,
  `{"id": "t", "update_rows": [{"id", "cells"?, "tone"?, "tooltip"?, "editors"?}]}`,
  `{"id": "t", "insert_rows": [...], "before": "<row id>"}` (default: append) and
  `{"id": "t", "remove_rows": ["<row id>", ...]}`. Row ids are unique: the first row with an id
  wins, also within one `replace_rows` or `insert_rows` batch. Removals use contiguous ranges;
  more than 512 rows reset the model. After a reset the host restores the selected row ids and
  the scroll position, and sends `selected` when selected rows were removed.
- **Events.** Field `changed` events also fire when UI automation (`set_ui`) writes a control;
  updates from the plugin never echo back. Text and number fields are debounced by 300 ms;
  Browse… sends `changed` at once. A button without `action` behaves like `event: true`. With a
  save dialog, an event button's `value` is the chosen path.
- **Bounded input.** User input can never push a host message over the transport limit. A field
  value is at most 65,536 characters, and `inputs` stay below 256 KiB (later values are
  shortened; a list value counts by its serialized size, and when the budget is spent its trailing
  elements are dropped and the truncated flag is set). `selected` carries at most the first 1,000 row ids plus `selected_count`. A
  shortened event or action carries `truncated: true` (`inputs_truncated: true` in action
  arguments). The host also truncates any `view/event` over 512 KiB (dropping `inputs`, then
  `value`) and drops events while the plugin is not reading its input. Job arguments over the
  1 MiB message limit fail the job, not the plugin.
- **Text.** Labels, badges, button labels, group titles, progress text, field captions,
  placeholders, tooltips, column titles, cells, choice labels and view titles are plain text of
  at most 4,096 characters. A row keeps at most one cell per column.
- **Values.** `number` fields send a number or `null`; `choice` sends the selected choice value
  (choices may be strings, numbers or `{value, label}`); `boolean` sends `true`/`false`.
- **Tables.** At most 64 columns and 50,000 cells; an operation that would exceed the limit is
  rejected and logged once, and leaves the table and an open cell editor as they were. A
  `sortable` table keeps the plugin's row order until the user clicks a column header; sorting
  uses numeric order for cells that parse as numbers.
- **Coalescing.** Updates for one CILogg window are applied together at most every 100 ms; a root
  replaces queued patches. When more than 4,096 patch entries are queued for a view, only that
  view renders early, at most once per interval (and always beyond 65,536 entries).
- **Warnings.** Each view logs at most 64 distinct problems, then one summary line.
- **Windows.** `view/show` and `ui/message` without `window_id` go to the most recently active
  CILogg window; an unknown `window_id` is ignored. `focus` defaults to true. `view/hide` closes
  the view and sends `view/closed`.
- **Placement.** Dock side, floating state, size and floating geometry, and window or dialog
  geometry are stored per view in the plugin settings. Without saved placement, a dock first
  tries the main window's saved dock state (stable object name `pluginView_<plugin>_<view>`).
  Modal dialogs are window-modal and shown without a blocking `exec()`.
- **Host dialogs.** Confirmation and file dialogs are parented to the CILogg window, not the
  view. When a view is rebuilt, closed, revoked or stopped while one is open, the dialog is
  rejected and its answer is discarded: nothing is sent to the plugin.
- **Message boxes.** Message boxes are a generic feature for every plugin; no view needs to be
  declared. The host rejects invalid boxes, a ninth open box and more than 10 new boxes per
  5 seconds per plugin at once with `ui/message_result` `{button: "", error: "..."}`. Buttons
  are named `pluginMessage_<plugin>_<message>_<button>`. The countdown uses whole seconds of
  `timeout_ms`. Closing the CILogg window reports `button: "closed"`; a stopping plugin's boxes
  close without a result.
- **Lifecycle.** A view is disabled until the plugin sends a root, after opening and again
  after every restart, so a stale view never looks live; patches for the stale tree are dropped.
  A stopped view shows the note `pluginViewStatus_<plugin>_<view>`. Actions from a stopped or
  stale view are ignored. Disabling or removing a plugin closes its views and boxes without
  further protocol messages, since the process is stopping.
- **Cancellation.** Only `cancel_plugin_job` gets the 10-second `cancelling` grace period.
  Closing a window or file cancels its jobs at once, so no panel reaches a closed context. A
  late answer during `cancelling` is kept for diagnostics only.
- **`pause_comm --wait`.** While the wait runs, other COM commands for the same stream
  (`start_comm`, `play_comm`, `pause_comm`, `stop_comm`, `close_com`) fail with a busy error.
- **Other object names.** Field captions `…_<element>_label`, Browse buttons `…_browse`, table
  filter boxes `…_filter`, confirmation boxes `…_confirm` with buttons `…_confirm_ok` and
  `…_confirm_cancel`, file dialogs `…_dialog` / `…_save`, and menu entries
  `pluginViewAction_<plugin>_<view>`.
- **SDKs.** The C++ SDK header gains `onNotification`, `updateView`, `patchView`, `showView`,
  `hideView`, `message`, `closeMessage` and `hostFeatures`. The Python SDK API is described in
  [the plugin SDK](plugin-sdk.md).
- **Tests.** `tests/ui/workbench_e13_test.cpp` (`[workbench][E13]`), the `[E13]` cases in
  `tests/unit/plugins_test.cpp`, `tests/plugins/test_view_sdk.py`, the `E13` case of
  `tests/mcp/workbench_parity.py`, and the view-demo section of
  `tests/plugins/host_integration.py`.
