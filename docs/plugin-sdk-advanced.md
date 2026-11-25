# Advanced plugin SDK (E17): menus, rich elements, tooltips with handlers and window integration — design

**Status:** accepted on 2026-11-19 (decision D17 in
[docs/workbench/decisions.md](workbench/decisions.md), task
[E17](../plans/cilogg-workbench-2026-10-21/tasks/E17.md)). The user accepted every recommended
answer of section 23, including a separate yes to question 13 (E17i, built last), and asked for the
slices in order, one branch and pull request each (section 21). **E17a to E17f are
implemented** (sections 24 to 29); E17g to E17i are planned.

**Request (Dmitry, 2026-11-19), verbatim:**

> let also extend plugins system so that:
> * it can create its own  menu in menu and submenus under plugins menu
> * it can add its menus and submenus in right click tab menu
> * it can add its menus and submenus in right click pane and log window menu
> * combobox could be regular drop down, drop down with icons, with checkboxes and radio buttons
> * same with table view, it can have combobox could be regular drop down, drop down with icons, with checkboxes and radio buttons
> * table also can hold icons
> * table cell can have tooltip and advanced tooltip with handler that will fill tooltip
> * same with any element it can have tooltip and advanced tooltip with handler that will fill tooltip
> * table can have multiple selection with shift and ctrl mode
> * also add folder/file tree browser element with file/folder multiple selection with shift and ctrl mode
> * add checkboxes and radio buttons elements
> * think what is missing for very advanced plugin SDK

This design extends the [plugin views](plugin-views.md) of E13 and E14 and the
[plugin SDK](plugin-sdk.md). The plugin protocol version stays 1.

## 1. Summary

- **Menus.** A plugin declares menus in `plugin.json`: its own submenu (with nested submenus) under
  **Plugins**, optionally one top-level menu, and entries in the context menus of tabs, panes, the
  log view and the filtered view. Entries run an action, open a view or send an event to the
  plugin; they can be checkable, grouped as radio items, shown only when conditions match, changed
  at run time, and filled on demand (dynamic submenus). Entries can carry keyboard shortcuts that
  the user can change in Options.
- **Choices.** Choice fields and table cell editors offer the four drop-downs of the request:
  plain, with icons, with check boxes (the existing `multichoice`) and with radio marks; inline
  radio and check box styles too. New `checkbox`, `radio_group` and `check_group` elements.
- **Tables.** Cells can be objects with an icon, a tooltip, a tone and a progress bar; rows can have
  an icon and a check box. The plugin can set the selection; Shift and Ctrl selection already exists
  and gains a toggle mode. Double-click, check and move events; row context menus; Copy and Export.
- **Tooltips.** Every element, table cell, tree node and status item can have a static tooltip, and
  an **advanced tooltip** that the host asks the plugin for when the pointer rests on it. The answer
  is cached, and the host never waits for it on the GUI thread.
- **Trees.** A `tree` element (plugin data, lazy children) and a `file_tree` element (a host file
  and folder browser), both with Shift and Ctrl multiple selection.
- **What was missing** (section 1.2): capability negotiation with versioned host features, host
  queries with timeouts (the mechanism behind advanced tooltips, dynamic menus, lazy trees and
  completion), icons from files, a built-in set and plugin-registered images, light and dark icons,
  layout containers (tabs, splitter, form, grid), a rich text pane with Markdown, images,
  sparklines and charts, field validation and more field kinds, dialog buttons with a result, a busy
  overlay, toolbars, status-bar items, notifications, clipboard, drag and drop, view state and
  session restore, theme events, host events (focused tab, tabs, log selection), settings pages in
  Options, richer message boxes, file dialogs on demand, accessibility names, typed SDKs with a test
  harness, JSON Schemas and a protocol log, and, if approved, a bounded command channel from plugins
  to the host.
- **Compatibility.** A plugin that does not opt in gets the same messages with the same keys as
  today; only `initialize` gains additive keys, which both SDKs ignore. The host sends a new message
  to a plugin only when the plugin declared that it handles it. Manifests only gain new keys, never
  new values of existing enumerations, so older hosts still accept them, and the SDK builders fall
  back on older hosts (section 3).

### 1.1 The request, item by item

| Request item | Design | Slice |
| --- | --- | --- |
| Own menu and submenus under Plugins | Section 7, location `plugins` (and optional `menubar`) | E17b |
| Menus and submenus in the tab context menu | Section 7, location `tab` | E17b |
| Menus and submenus in the pane and log window context menus | Section 7, locations `pane` and `log` | E17b |
| Combo box: plain, with icons, with check boxes, with radio buttons | Section 8 | E17c |
| The same in table cell editors | Section 8.4 | E17c |
| Icons in tables | Sections 6 and 10.1 | E17a (icons), E17d (cells) |
| Table cell tooltip and advanced tooltip with a handler | Sections 9 and 10.1 | E17a, E17d |
| Any element: tooltip and advanced tooltip with a handler | Section 9 | E17a |
| Table multiple selection with Shift and Ctrl | Section 10.2 (exists; gains a toggle mode and plugin-set selection) | E17d |
| Folder and file tree browser with Shift and Ctrl multiple selection | Section 11.2 | E17e |
| Check box and radio button elements | Section 8.3 | E17c |
| What is missing for a very advanced SDK | Sections 4, 5, 6, 12, 13, 14, 16 and 22 | E17a, E17f–E17i |

### 1.2 What a very advanced plugin SDK still lacked

Beyond the request, these are proposed (each with its section):

| Gap | Proposal |
| --- | --- |
| Hosts and plugins cannot negotiate versions of features | Versioned host features, plugin `handles` and `features`, manifest `requires` (section 4) |
| The host can never ask the plugin anything at run time | Host queries with timeouts, cancellation and a circuit breaker (section 5) |
| Icons only from files, never dynamic | Built-in icons, status dots, registered images, light/dark variants (section 6) |
| No keyboard shortcuts | `shortcut` on menu items, actions and views, editable in Options (section 7.6) |
| Only vertical and horizontal boxes | `tabs`, `splitter`, `form`, `grid`, `separator`, `spacer`, size hints (section 12.1) |
| No rich text, images or charts | `text_view` with Markdown, `image`, `sparkline`, `chart` (section 12.2) |
| No input validation | `required`, `pattern`, `min`/`max`, `error`, buttons that need valid input (section 8.6) |
| Few field kinds | `multiline`, `secret`, `integer`, `slider`, `color`, `date`, `time`, `datetime` (section 8.5) |
| Dialogs have no OK/Cancel result | Dialog buttons and a result in `view/closed` (section 12.3) |
| No busy state or unsaved-changes guard | `busy` overlay, `modified` with a close confirmation (section 12.3) |
| No toolbars or status-bar items | Plugin toolbars and status items (sections 13.1 and 13.2) |
| No notifications | Non-modal notifications with actions (section 13.3) |
| No clipboard or drag and drop | Clipboard write, drag and drop between views, files and panes (sections 13.4 and 13.5) |
| UI state lost between sessions | Host-kept view state, plugin state blob, session restore (section 13.6) |
| No theme awareness | Theme in `initialize`, theme events, palette-aware tones (section 13.7) |
| A view never learns that the focused tab or the log selection changed | Opt-in host events (section 13.10) |
| Settings only in the Plugins window | Settings pages in Options (section 13.8) |
| Message boxes cannot ask for input | Input, details and "Don't ask again" in `ui/message`; file dialogs on demand (section 13.9) |
| Screen readers hear element ids | Accessible names from captions, `accessible_name`/`accessible_description` (section 12.4) |
| Untyped SDKs, no way to test a plugin without CILogg | Typed SDKs, a fake host test harness, JSON Schemas, a validator, a protocol log (section 16) |
| Plugins must spawn `cilogg command` to control CILogg | Optional bounded host-command channel (section 14, needs explicit approval) |

## 2. What exists today

- **Plugins menu.** `PluginUiController::refreshActions` (`src/ui/src/pluginuicontroller.cpp:396-501`)
  adds one flat entry per view (its title) and per action (`<plugin name>: <label>`) to the
  **Plugins** menu, and optional main-toolbar buttons (E14 keys `menu`, `toolbar`, `icon`). There
  are no submenus, separators, check marks or shortcuts.
- **Context menus.** The log and filtered views share `AbstractLogView::createMenu`
  (`src/ui/src/abstractlogview.cpp:2317-2441`), built once. Each time it opens, the view refills the
  Highlighters, Color labels and **Panes** submenus and updates entry texts and states
  (`abstractlogview.cpp:540-672`); the Panes submenu comes from `updatePaneMenu`, through
  `PaneMenuHost`, implemented by `MainWindow::populatePaneMenu` (`src/ui/src/mainwindowpanes.cpp:256`).
  The menu runs with a blocking `exec()` inside the mouse press handler (line 672). The tab menu
  (`TabbedCrawlerWidget::showContextMenu`, `src/ui/src/tabbedcrawlerwidget.cpp:484-647`) is built per
  click around a tab index and ends with an extension hook through which MainWindow appends **Move to
  Pane** and the pane commands inline (`extendTabContextMenu` and `addPaneCommands`,
  `mainwindowpanes.cpp:201-254`). An empty pane has its own menu (`populateEmptyPaneMenu`). Plugins
  cannot add to any of these menus.
- **Elements.** `column`, `row`, `group`, `label`, `badge`, `field` (`text`, `number`, `choice`,
  `multichoice`, `boolean`, `file_open`, `file_save`, `directory`), `button`, `progress`, `table`
  (`src/ui/src/pluginview.cpp`). `choice` is a plain `QComboBox`; `multichoice` is the
  `CheckComboBox` of E14; choices are values or `{value, label}` without icons.
- **Tooltips.** Every element has a static `tooltip`. A table row has one `tooltip` shown on every
  cell of the row (`pluginview.cpp:264-265`); cells, choices and menu entries have none. Nothing asks
  the plugin at hover time. The views design promises plain text, but element tooltips reach Qt
  unchanged (`pluginview.cpp:1685`), so a tooltip that looks like HTML is rendered as HTML; only row
  tooltips go through the plain-text conversion (`plainToolTip`, lines 207-210). E17a fixes this.
- **Tables.** A `QAbstractTableModel` with a sort and filter proxy. `selection: "multi"` is already
  Qt's extended selection: click, Ctrl+click, Shift+click and Ctrl+A (`pluginview.cpp:1840-1842`).
  The plugin cannot set the selection; there are no icons, no cell objects, no event for a
  double-click on a read-only cell, no check boxes and no context menu.
- **Missing elements.** No tree, file browser, tabs, splitter, form, grid, rich text, image or chart.
- **Protocol.** The host sends two requests, `initialize` and `invoke`, and matches every response to
  a job (`PluginManager::handleMessage`, `src/plugins/src/pluginmanager.cpp:1013-1046`). A request
  from the plugin fails the plugin ("Unsupported plugin-to-host request", lines 1164-1169).
  `host_features` is a list of names without versions (`src/plugins/src/pluginprocess.cpp:113`).
- **SDKs.** The Python SDK raises on any unknown host method (`plugins/sdk/python/cilogg_plugin.py:547-548`),
  which ends its protocol loop. The C++ SDK passes every `view/*` and `ui/*` method to the plugin's
  notification callback (`plugins/sdk/cpp/cilogg_plugin.h:338-342`), ignores other unknown methods
  and never answers an unknown request. Plugins ship their own copy of the SDK, so old copies stay in
  use for a long time. The host must therefore never send a new method to a plugin that did not ask
  for it.
- **Start-up queue.** While a plugin starts, `PluginProcess::request` queues frames (at most 128;
  one more fails the plugin) and sends them unchecked once it is ready
  (`src/plugins/src/pluginprocess.cpp:123-138`).
- **Manifest validation.** Unknown top-level keys and unknown view keys are ignored, but an unknown
  view `kind` or `area`, action field `type` or capability makes the whole manifest invalid
  (`src/plugins/src/pluginmanifest.cpp:172-217`, `281-292`). An older host tolerates new keys, not
  new values of existing enumerations.
- **Shortcuts.** CILogg's shortcuts come from a list compiled into the program
  (`ShortcutAction::defaultShortcutList`, `src/settings/src/shortcuts.cpp:97`), from which Options
  builds its shortcut table (`src/ui/src/optionsdialog.cpp:809`); plugins have no entries in it.
- **Accessibility.** Groups, fields and buttons use their caption as accessible name
  (`pluginview.cpp:1692`, `1729`, `1773`); labels, badges, tables and progress bars use their id
  (`pluginview.cpp:1393-1395`).
- **Size.** `pluginview.cpp` is 2,165 lines; the new elements go into their own files (section 19).

## 3. Principles

1. **Protocol version 1, additive only.** No message changes meaning; new messages and keys are
   added.
2. **Named, versioned host features.** Each new host capability is announced in `host_features`
   with a version (section 4).
3. **Opt-in traffic.** The host sends a new host-to-plugin message only to a plugin that declared
   that method, in `handles` in `plugin.json` and in `features` in its initialize result (section
   4.2), only for things the plugin declared (a menu entry with `event`, an element with
   `tooltip_provider`, a lazy tree), and only while the plugin is ready, never through the start-up
   queue.
4. **The plugin still never sends requests to the host** in E17a to E17h. Host queries (section 5)
   travel from the host to the plugin, like `invoke`. Section 14 proposes the only exception, as a
   separate slice that needs explicit approval (question 13).
5. **Declarative first.** Menus, shortcuts, toolbars, status items and settings pages are declared
   in `plugin.json`, so they appear before the plugin starts; the plugin starts on first use, as
   for actions today. Run-time changes are notifications.
6. **Plain text stays the rule.** Two exceptions, both rendered by the host: structured advanced
   tooltips built from escaped plain-text parts (section 9), and Markdown with HTML disabled in
   `text_view` (section 12.2).
7. **New manifest keys, never new enumeration values.** View `kind` and `area`, action and
   configuration field `type` and `capabilities` keep their values, and the E14 `icon` key on actions
   and views stays a plugin-file path; new behaviour gets new keys, so an older host still loads the
   manifest and ignores what it does not know.
8. **Parity.** Every new GUI command has a Commander operation, so the GUI, the CLI, native MCP and
   Python behave the same (section 15).
9. **Bounded and non-blocking.** Every list, text and rate has a limit (section 18); the GUI thread
   never waits for a plugin.
10. **Graceful degradation.** Unknown element types and properties are ignored and logged once, as
    since E13. Unknown values of existing view properties degrade badly on an older host (a new
    `selection` value means no selection, a cell object shows an empty cell, a separator choice
    becomes an empty item, an unknown container drops its whole subtree and an unknown root rejects
    the update), so new behaviour uses new keys and new element types, and the SDK builders produce
    the older form when the host lacks the feature (section 16).
11. **What exactly stays the same.** For a plugin without `handles`, the host sends the same methods
    with the same keys as before E17, except additive keys in `initialize`, which both SDKs read
    with `get` / `value` and therefore ignore. Existing events gain new keys (such as `current` in
    `selected`, `valid` in `changed`) only for plugins that declare `handles`. A golden-frame test
    (section 20) checks this.

## 4. Capability negotiation

### 4.1 What the host announces

`initialize` gains keys; the existing ones are unchanged:

```json
{
  "protocol_version": 1,
  "plugin_id": "org.example.tools",
  "host_features": ["views", "view_edit", "queries", "tooltips", "icons"],
  "host_feature_versions": {"views": 1, "view_edit": 1, "queries": 1, "tooltips": 1, "icons": 1},
  "host_version": "26.05.0",
  "host_limits": {"max_elements": 512, "max_table_cells": 50000, "max_text_chars": 4096,
                  "max_message_bytes": 1048576}
}
```

- `host_features` keeps its form (a list of names), so existing plugins read it as before. Both SDKs
  read `initialize` parameters by name, so the new keys reach old SDK copies harmlessly.
- `host_feature_versions` gives each feature's version. A feature's version increases when it gains
  properties or messages; SDK helpers compare with `>=`.
- `host_limits` lets a plugin size its updates instead of hard-coding the limits; `max_text_chars` is
  the limit of one text property where this design sets no other limit (section 18).
- E17g adds `theme` (section 13.7).

Features per slice:

| Feature | Slice | Covers |
| --- | --- | --- |
| `queries` | E17a | Host queries, `query/cancel` (section 5) |
| `tooltips` | E17a | Static tooltips everywhere, advanced tooltips (section 9) |
| `icons` | E17a | Icon references and registered images (section 6) |
| `menus` | E17b | Plugin menus, context menus, dynamic submenus, `ui/command` (section 7) |
| `shortcuts` | E17b | `shortcut` keys (section 7.6) |
| `choices` | E17c | Choice styles, icons and separators in choices, searchable and editable drop-downs, completion (section 8) |
| `toggles` | E17c | `checkbox`, `radio_group`, `check_group` (section 8.3) |
| `field_kinds` | E17c | New field kinds (section 8.5) |
| `validation` | E17c | Field validation (section 8.6) |
| `rich_tables` | E17d | Cell objects, row icons and check boxes, selection patch, table events, Copy and Export (section 10) |
| `element_menus` | E17d | `context_menu` on elements (section 10.4) |
| `tree`, `file_tree` | E17e | Section 11 |
| `layout`, `text_view`, `images`, `charts`, `view_dialogs` | E17f | Section 12 |
| `toolbars`, `status_items`, `notifications`, `clipboard`, `drag_drop`, `view_state`, `theme`, `settings_pages`, `message_input`, `file_dialogs`, `host_events` | E17g | Section 13 |
| `host_commands` | E17i | Section 14 (only if approved) |

### 4.2 What the plugin announces

The plugin announces the host-to-plugin methods it handles twice:

- **Before it starts**, in `plugin.json`:

  ```json
  "handles": ["tooltip/resolve", "menu/populate", "ui/command", "query/cancel"]
  ```

  The host needs this before the first start: to enable an `event` menu entry, to decide whether a
  dynamic submenu can be filled, and to ask a status item for its tooltip. Older hosts ignore the
  key.
- **When it runs**, in `features` in the initialize result (the SDKs fill it from the handlers the
  plugin registered). It is the authority while the plugin runs: a method that `handles` lists but
  `features` does not is treated as not handled until the next start and logged once. The validator
  of E17h checks that both agree.

The initialize result gains two optional keys:

```json
{
  "plugin_id": "org.example.tools",
  "protocol_version": 1,
  "capabilities": ["tool"],
  "actions": [],
  "configuration": [],
  "sdk": {"name": "cilogg_plugin.py", "version": "2.0.0"},
  "features": ["tooltip/resolve", "menu/populate", "ui/command", "query/cancel", "ui/theme_changed"]
}
```

- `features` lists the host-to-plugin methods the plugin handles. The host sends a method added by
  this design only when its name is in `handles` and in `features`. A plugin without them, which is
  every plugin today, gets the traffic it gets today (principle 11).
- `sdk` is informational: it appears in `get_plugins` and in problem reports.
- The host does not validate unknown keys of the initialize result today, so older hosts accept
  these keys.

### 4.3 What a manifest can require

```json
"requires": {"host_features": ["menus>=1", "tree"]}
```

A host that lacks a required feature (or version) lists the plugin as "Requires a newer CILogg
(missing: tree)" in the Plugins and Packs window, does not start it and offers none of its menus,
actions or views. An older host ignores `requires`, so a repository should also set the catalog's
`min_host_version` (E15) for plugins that cannot degrade.

## 5. Host queries: requests from the host with timeouts

The advanced tooltip needs the host to ask the plugin a question and wait, without ever blocking
the GUI. The same mechanism serves dynamic submenus, lazy tree children and field completion.

### 5.1 Methods

| Method | Sent when | Default timeout | Maximum | Result |
| --- | --- | --- | --- | --- |
| `tooltip/resolve` | The pointer rests on a target with `tooltip_provider` (section 9) | 1,500 ms | 5,000 ms | A tooltip object, or `null` |
| `menu/populate` | A `dynamic` submenu is about to show (section 7.4) | 1,000 ms | 3,000 ms | `{items}` |
| `tree/children` | A lazy tree node is expanded (section 11.1) | 10,000 ms | 30,000 ms | `{nodes}` |
| `field/complete` | Typing pauses for 150 ms in a field with `completion` (section 8.2) | 500 ms | 2,000 ms | `{items}` |

A declaration may shorten or lengthen a timeout up to the maximum (`tooltip_timeout_ms`,
`populate_timeout_ms`, `children_timeout_ms`, `completion_timeout_ms`).

A query is an ordinary JSON-RPC request from the host:

```json
{"jsonrpc": "2.0", "id": "q:42", "method": "tooltip/resolve",
 "params": {"view_id": "main", "window_id": "w1", "element_id": "devices",
            "row_id": "r7", "column_id": "state", "tooltip_key": "v3",
            "timeout_ms": 1500, "context": {"windowId": "w1", "tabId": "t3"}}}
```

and the plugin answers it like `invoke`:

```json
{"jsonrpc": "2.0", "id": "q:42", "result": {"title": "Board 7", "text": "Connected 12 s ago"}}
```

### 5.2 Rules

- **Ids and routing.** Query ids are `q:<number>`; job ids are UUIDs and the initialize request has
  the id `initialize`, so they never collide. Each pending query records its plugin, and
  `PluginManager::handleMessage` checks `q:` answers against the pending queries before the job
  lookup: an answer from another plugin, or to an id that is not pending, is dropped, as for jobs.
- **Only while ready.** Queries and `query/cancel` are sent only while the plugin is ready, never
  through the start-up queue of section 2. A query never starts a plugin, except a dynamic submenu
  (section 7.4).
- **Backlog.** Queries and `query/cancel` use the guard view events already use: while the plugin is
  not reading its input, they are not sent and the target falls back, so a busy plugin is never
  failed by the transport limit.
- **Cancellation.** When the host stops waiting (timeout, the pointer left the target, the menu
  closed, the node collapsed, the field changed) it forgets the query and sends the notification
  `query/cancel` `{id}`. Plugins that list any query method must accept `query/cancel`; the SDKs do.
- **Concurrency.** At most 8 queries are outstanding per plugin. Per view at most one
  `tooltip/resolve` and one `field/complete` (a new one cancels the old one), at most 4
  `tree/children`; per window at most one `menu/populate` and one `tooltip/resolve` for status items.
  Further `tree/children` queries wait in a queue of 16; other queries over the limit are not sent
  and the target falls back (static tooltip, "Not available" menu entry).
- **Circuit breaker.** After 5 consecutive timeouts, error answers or invalid answers of one method
  (a `null` answer is a valid "nothing" and does not count), the host stops sending that method to
  that plugin for 60 seconds, logs it once and shows it in the Plugins and Packs window.
  `get_plugins` reports per method: sent, answered, timed out, failed, median and 95th percentile
  latency, and the pause end time.
- **Errors.** A JSON-RPC error answer, a `null` result or an invalid result means "nothing": the
  target falls back. An invalid result is logged once per plugin and method.
- **Lifecycle.** Stopping, restarting, disabling or removing a plugin drops its pending queries at
  once; closing a view drops that view's queries.

### 5.3 Security model

- **Direction.** Queries go from the host to the plugin, like `invoke`. The plugin gains no way to
  make the host do anything: it only answers questions the host asks about things the plugin itself
  declared. A request sent by the plugin still fails the plugin.
- **What a query carries.** Ids the plugin defined (view, element, row, column, node, menu, item),
  the window id, and the same `context` the plugin already gets in `invoke` and `view/opened`. A
  `menu/populate` from the log view carries the line under the pointer and the selected line
  ranges, never the selected text (question 5); the text goes to the plugin only when the user picks
  one of its entries.
- **What an answer can do.** Answers are data. Each method's result is validated and capped
  (tooltip: 16 KiB of text and 32 rows; menu: 256 entries, depth 4; tree: 1,000 nodes per answer;
  completion: 100 items). Plugin strings are shown as plain text, or inside host-built rich text with
  every string escaped. No answer can open a dialog, take the focus or run anything.
- **Late answers.** An answer that arrives after the host stopped waiting is dropped.
- **Journal.** The activity journal (R06) records the method, target ids and latency of queries,
  never their contents.

### 5.4 Latency model

- A round trip over the plugin's pipes takes well under a millisecond; the budget is the plugin's
  handler. Handlers should answer within 100 ms.
- The GUI thread never waits: a query is sent, the target shows a placeholder or its fallback, and
  the answer updates the target in place if it is still relevant.
- The SDKs run query handlers apart from jobs and view callbacks, in one lane per method:
  `tooltip/resolve` and `field/complete` each on a single worker where a newer query replaces a
  waiting one (latest wins), `menu/populate` on its own worker and `tree/children` on two workers.
  A slow view callback or two long `tree/children` handlers therefore never delay a tooltip. Each
  handler gets its deadline; a query whose deadline passed before its handler started is dropped.
  The C++ SDK, whose view callbacks run on its reader thread, gets the same lanes.
- The circuit breaker protects the user from a plugin that is too slow; the statistics show plugin
  authors where they are.

## 6. Icons

An `icon` can appear on menu entries, toolbar and status items, buttons, choices, table cells, rows
and column headers, tree nodes, tabs, notifications, tooltips and the `image` element. An icon
reference is one of:

| Form | Meaning |
| --- | --- |
| `"icons/flash.svg"` | A `.png`, `.ico` or `.svg` file inside the plugin folder (as E14) |
| `"builtin:<name>"` | An icon of the host set (below) |
| `"status:<tone>"` | A coloured dot: `normal`, `ok`, `warn`, `error`, `muted`, `accent` |
| `"image:<id>"` | An image the plugin registered with `ui/icons` |
| `{"light": <ref>, "dark": <ref>}` | Different icons for light and dark themes |

- **Built-in set:** `info`, `warning`, `error`, `question`, `ok`, `cancel`, `add`, `remove`, `edit`,
  `delete`, `copy`, `paste`, `save`, `open`, `folder`, `file`, `search`, `filter`, `refresh`,
  `settings`, `play`, `pause`, `stop`, `record`, `upload`, `download`, `up`, `down`, `left`,
  `right`, `lock`, `unlock`, `link`, `device`, `port`, `chip`, `clock`, `star`, `bookmark`, `bell`.
  They follow the theme. An unknown name falls back to the generic icon and is logged once.
- **Actions and views.** The E14 `icon` key of actions and views stays a plugin-file path: an older
  host refuses any other value there, both when it reads the manifest and when it checks the actions
  of the initialize result. The other forms go into `icon_ref` on actions and views, which wins over
  `icon` on hosts that know it. Every key this design adds (menus, toolbars, status items, element
  properties) accepts all forms in `icon`.
- **Registered images.** `ui/icons` `{"icons": {"<id>": {"png": "<base64>"} | {"svg": "<svg text>"}},
  "remove": ["<id>"]}` registers images for the plugin's lifetime (until it stops). Icons: up to 256
  per plugin, 64 KiB each, 4 MiB in total, decoded at most 256 by 256 pixels. Pictures for the
  `image` element (an entry with `"picture": true`): up to 16 per plugin, 1 MiB each, decoded at most
  4,096 by 4,096 pixels. SVG text that refers to anything but `data:` URIs is refused.
  `"mask": true` next to an SVG marks it monochrome: the host recolours it with the theme's text
  colour.
- Icons work without views: menu entries, toolbars and status items use them too.

## 7. Menus, context menus and keyboard shortcuts

### 7.1 Declaring menus

```json
"menus": [
  {
    "id": "main",
    "location": "plugins",
    "title": "Device Tools",
    "icon": "icons/tool.svg",
    "items": [
      {"id": "connect", "title": "Connect…", "view": "main", "shortcut": "Ctrl+Alt+D"},
      {"id": "flash", "title": "Write Image…", "action": "write", "icon": "builtin:upload"},
      {"type": "separator"},
      {"id": "mode", "title": "Mode", "items": [
        {"id": "fast", "title": "Fast", "event": true, "checkable": true, "group": "mode",
         "checked": true},
        {"id": "safe", "title": "Safe", "event": true, "checkable": true, "group": "mode"}
      ]},
      {"id": "recent", "title": "Recent Images", "dynamic": true}
    ]
  },
  {
    "id": "tab",
    "location": "tab",
    "items": [
      {"id": "decode", "title": "Decode This Capture", "action": "decode",
       "when": {"tab_kinds": ["file", "serial"], "file_patterns": ["*.log", "*.txt"]}}
    ]
  },
  {
    "id": "lines",
    "location": "log",
    "items": [
      {"id": "explain", "title": "Explain Selected Lines", "event": true,
       "when": {"views": ["main", "filtered"], "selection": "lines", "max_lines": 1000}}
    ]
  }
]
```

### 7.2 Locations

| `location` | Where the entries appear | Context added for the plugin |
| --- | --- | --- |
| `plugins` | The plugin's own submenu in **Plugins** (title and icon from the menu) | Window, focused tab |
| `menubar` | One top-level menu after **Plugins** (question 1) | Window, focused tab |
| `tab` | The tab context menu of every pane | Tab id, file path, tab kind, port name of a serial tab, pane index |
| `pane` | The menu of an empty pane, the **Panes** submenu of the log and filtered views, and the pane commands at the end of the tab menu (question 4) | Pane index, the pane's tab ids, its current tab |
| `log` | The context menus of the main log view and of the filtered view | Tab, `view` (`main` or `filtered`), the line under the pointer, the selected line ranges and count; the selected text only when an entry is picked (question 5) |
| `element` | The context menus of view elements (`context_menu`, section 10.4) | View, element, the clicked row or node, the selected row or node ids |
| `toolbar` | Drop-down menus of plugin toolbar buttons (section 13.1) | Window, focused tab |

Line numbers are 1-based, as CILogg shows them; filtered-view lines are reported with their line
numbers in the file. Selections report at most 64 ranges and the total count.

### 7.3 Entries

| Key | Meaning |
| --- | --- |
| `id` | Unique within the menu, 128 characters, the action id rules |
| `type` | `item` (default), `separator` or `section` (a titled separator) |
| `title`, `icon`, `tooltip` | Plain text (128 characters for titles), icon reference (section 6) |
| `shortcut` | Section 7.6 |
| `enabled`, `visible` | Starting state; changed with `menu/update` |
| `checkable`, `checked`, `group` | A check mark; entries with the same `group` in one menu are exclusive (radio items) |
| `when` | Conditions evaluated by the host when the menu opens (below) |
| `items` | A submenu; depth at most 4 |
| `dynamic` | A submenu filled by `menu/populate` each time it opens (section 7.4) |
| One of `action` (+ `arguments`), `view`, `event` | What the entry does (section 7.5) |

`when` conditions, all of which must hold: `views` (`main`, `filtered`), `tab_kinds` (`file`,
`serial`, `plugin_stream`), `file_patterns` (globs on the file name), `selection` (`none`, `lines`,
`any`), `min_lines` and `max_lines` (selected line count). A condition the host does not know hides
the entry, so an older host never shows an entry out of place.

### 7.4 Run-time changes and dynamic submenus

- `menu/update` `{menu_id, items: [{id, set: {title, icon, tooltip, enabled, visible, checked}}]}`
  changes declared entries in every window; open menus change in place.
- A `dynamic` entry is a submenu that shows a disabled "Loading…" entry, sends `menu/populate`
  `{menu_id, item_id, location, window_id, context}` and replaces the entry with the answer while
  the menu is still open. On a timeout or an error it shows a disabled "Not available". The answer
  `{"items": [...], "cache_ms": 0}` uses the entry schema without shortcuts; `cache_ms` (at most 60
  seconds) lets the host reuse it where the plugin would get the same request: the same entry and
  the same `context` (window, tab, pane, log view, line, selected ranges and their count, element,
  row, node, column and selected ids).
- Opening a dynamic submenu of a stopped plugin starts it, as clicking one of its actions does
  (question 22): the entry shows "Loading…" until the plugin is ready and the answer arrives, at
  most the populate timeout after the plugin is ready and 10 seconds in all. A plugin whose
  `handles` lacks `menu/populate` gets a disabled "Not available" entry instead.
- Native menu bars (macOS, Linux global menus) do not reliably change while open. In the `plugins`
  and `menubar` locations a dynamic submenu therefore shows the last answer of this session at once
  (or "Loading…" the first time) and uses a new answer the next time it opens.

### 7.5 What an entry does

- **`action`** runs the action as its toolbar button does today (with its form when it has fields),
  on the tab of the context. The job's `context` gains
  `menu: {location, menu_id, item_id, line, selection}`; for `log` entries `selection` also carries
  `text` (at most 64 KiB, with `truncated`).
- **`view`** opens or raises the view in this window, following the context's tab (as
  `open_plugin_view --source tab:<id>`).
- **`event`** starts the plugin if needed and sends `ui/command`
  `{origin, menu_id, item_id, location, window_id, checked, context}`, where `origin` is `menu`,
  `shortcut`, `toolbar` or `status`. The plugin must list `ui/command` in `handles`; otherwise the
  entry is disabled with the tooltip "This plugin needs a newer SDK".

### 7.6 Keyboard shortcuts

- `shortcut` is a portable key sequence (`"Ctrl+Alt+D"`), or a list of two, on menu entries, actions
  and views. A shortcut acts like a click with the current context (in a log view, its selection).
- CILogg's shortcut list is compiled into the program today; E17b adds a provider of plugin entries
  next to it, named `plugin/<plugin>/<menu>/<item>` (or `plugin/<plugin>/action/<id>`,
  `plugin/<plugin>/view/<id>`). **Options > Shortcuts** lists them under "Plugins: <name>", and the
  user can change or clear them; the choice is kept per plugin and entry and survives updates. Every
  CILogg window registers them again when plugins are enabled, disabled, updated or removed.
- Qt fires neither of two shortcuts with the same keys, so a conflicting plugin shortcut is refused,
  not shadowed. A plugin shortcut is refused when its keys equal any shortcut of CILogg (the shortcut
  list, the window's own actions such as the split-pane commands) or a reserved key (Ctrl+C, Ctrl+V,
  Ctrl+X, Ctrl+A, Ctrl+F, Ctrl+Z, F2, Enter, Esc, Tab, Delete), or a shortcut the user set. Between
  plugins, a shortcut stays with the plugin that got it first; the others get none. Refused
  shortcuts are listed with a warning in the Plugins and Packs window (question 6). At most 32
  shortcuts per plugin.

### 7.7 Placement

- **Plugins menu.** The host entries (Plugins and Packs, Browse, Updates, Run actions on) come
  first, then a separator, then one block per enabled plugin in name order. A plugin with a
  `plugins` menu gets one submenu; its actions and views no longer get flat entries of their own
  (question 2). A plugin without `menus` keeps today's flat entries unchanged.
- **Context menus.** Plugin entries form a section after the host's own entries (in the log views
  before the **Panes** submenu, in the tab menu before the pane commands), in plugin name order.
  More than 8 top-level entries of one plugin in one location go into a "More" submenu. The Plugins
  and Packs window can hide a plugin's entries per location (question 3).
- **Object names.** Submenus `pluginMenu_<plugin>_<menu>[_<item>]`, entries
  `pluginMenuItem_<plugin>_<menu>_<item>`; dynamic entries append their own id.

### 7.8 Implementation constraints

- Context menus run a blocking `exec()` (the log view inside its mouse press handler) and the tab
  menu is built around a tab index. When a menu opens, the host turns indexes into stable ids (tab,
  pane, window) for the context, and a picked plugin entry is handled after the menu has closed
  (queued), because an action form is itself modal and the tab may move or close meanwhile.
- Plugin sections are rebuilt each time a menu opens, like the Highlighters and Panes submenus
  today, so `when` conditions and `menu/update` states are always current.

## 8. Choices, check boxes and radio buttons

### 8.1 Choice entries

A choice is a value, or an object; separators and section titles can sit between choices:

```json
"choices": [
  {"value": "auto", "label": "Automatic", "icon": "builtin:refresh",
   "tooltip": "Detect the speed"},
  {"type": "separator"},
  {"type": "section", "label": "Fixed speeds"},
  {"value": 115200, "label": "115200", "icon": "status:ok"},
  {"value": 9600, "label": "9600", "enabled": false}
]
```

On an older host, separators and sections would become empty, selectable items, so the SDK builders
leave them out (and drop `icon`, `tooltip` and `enabled`) when the host lacks `choices`.

### 8.2 Drop-down styles

| Request | Declaration |
| --- | --- |
| Regular drop-down | `"kind": "choice"` |
| Drop-down with icons | `icon` on the choices (works with every style) |
| Drop-down with check boxes | `"kind": "multichoice"` (E14) |
| Drop-down with radio buttons | `"kind": "choice", "indicator": "radio"` |

More keys for `choice` and `multichoice` fields:

- `style`: `dropdown` (default) or an inline list: `radio` for `choice`, `checkboxes` for
  `multichoice`; with `orientation` (`vertical` default, `horizontal`) and `columns`.
- `searchable`: a filter line at the top of the open list.
- `editable` (`choice` only): the user may type a value that is not in the list; `changed` is
  debounced like text.
- `completion` (with `editable`, or on a `text` field): the host asks `field/complete`
  `{element_id, text, cursor}` and shows the answer `{"items": [{"value", "label", "icon"}]}` as a
  completion list.

### 8.3 Check box and radio button elements

| Type | Properties | Value in `inputs` | Events |
| --- | --- | --- | --- |
| `checkbox` | `text`, `value` (`true`, `false`, or `null` for partly checked with `tristate`), `tristate`, `tone` | the value | `changed` |
| `radio_group` | `title` (optional frame), `choices`, `value`, `orientation`, `columns` | the chosen value | `changed` |
| `check_group` | `title`, `choices`, `value` (list), `orientation`, `columns`, `all_none` (adds All and None links) | the list | `changed` |

Fields keep their caption and form layout; the elements fit free layouts. Both share one widget
implementation (question 11).

### 8.4 The same in table cell editors

Cell editors (E14) accept the same choice objects (icons, separators, sections), `indicator: "radio"`
for `choice` editors and `searchable`. A committed choice with an icon shows that icon in the cell
unless the cell sets its own.

### 8.5 More field kinds

`multiline` (`rows`, `monospace`), `secret` (masked), `integer` (`min`, `max`, `step`), `slider`
(`min`, `max`, `step`; shows the value), `color` (`"#rrggbb"`), `date` (`YYYY-MM-DD`), `time`
(`HH:MM:SS`), `datetime` (ISO 8601, local time). An older host shows an unknown kind as a text field
and logs it once; the SDK builder sends `text` instead when the host lacks `field_kinds`.

- **One vocabulary.** View field kinds reuse the names of the manifest field types where they exist
  (`secret`, `multiline`, `integer`); numeric bounds in views are `min` and `max`, as in E14 cell
  editors, and `settings_view` (section 13.8) maps the manifest's `minimum` and `maximum` onto them.
  The manifest field `type` list itself stays frozen (principle 7).

### 8.6 Validation

- Field keys `required`, `pattern` (a regular expression that must match the whole text, at most 256
  characters), `min`/`max`, `min_length`/`max_length`, and `error` (a message the plugin sets; an
  empty string clears it).
- An invalid field gets a red frame and its message below it; the message is also its accessible
  description. `changed` events carry `valid`.
- A button with `requires_valid` (`true` for every field of the view, or a list of field ids) stays
  disabled until those fields are valid. A button that starts an action never starts it with
  invalid fields.

## 9. Tooltips and advanced tooltips

### 9.1 Static tooltips

A plain-text `tooltip` (at most 4,096 characters) is accepted on every element (as today) and now
also on table columns (header), table cells (section 10.1), choices, menu entries, toolbar and
status items, tree nodes and tabs. For a table cell, the cell's tooltip wins over the row's, which
wins over the cell text. E17a sends every element tooltip through the same plain-text conversion as
row tooltips, so text that looks like HTML is shown as written (section 2).

### 9.2 Advanced tooltips

- `"tooltip_provider": true` on any element asks the plugin for that element's tooltip. On a table it
  asks per cell (the query carries `row_id` and `column_id`); on one column, only for that column's
  cells; on a row, only for that row; on a tree, per node; on a status item, for that item.
- `tooltip_key` (128 characters) on the element, row, cell or node is part of the cache key; the
  plugin changes it when the tooltip content changes.
- The plugin answers `tooltip/resolve` (section 5) with `null` (no tooltip: the static one is used)
  or:

```json
{
  "title": "Board 7",
  "text": "Connected 12 s ago.\nLast reset: power.",
  "rows": [["Firmware", "3.2.1"], ["Uptime", "12 min"]],
  "tone": "warn",
  "icon": "status:warn",
  "footer": "Updated 10:31:02",
  "ttl_ms": 10000
}
```

- The host renders it: the title in bold with the icon, the text with its line breaks, the rows as a
  two-column table (at most 32), the footer muted, at most 480 pixels wide, the tone colouring the
  title. Every part is plain text and escaped; the plugin never sends HTML (question 8).

### 9.3 What the user sees

1. The pointer rests on the target (Qt's tooltip delay).
2. A cached answer shows at once.
3. Otherwise the host sends the query and shows the static tooltip, if any. Without one it waits 250
   ms, then shows a muted "Loading…".
4. An answer that arrives while the pointer is still over the same target replaces the tooltip in
   place. If the pointer moved away, the query is cancelled.
5. On a timeout or an error, the static tooltip stays, or nothing is shown.

### 9.4 Cache

The key is the plugin, window, view, element, row, column, node and `tooltip_key`. An answer is kept
for its `ttl_ms` (default 30 seconds, at most 10 minutes; 0 means do not keep), at most 256 entries
per view (least recently used first out). Entries are dropped by any patch to that element, row, cell
or node, by `tooltip/invalidate` `{view_id, element_id, row_ids, node_ids}`, when the plugin restarts
and when the view closes (question 7).

### 9.5 Automation

`get_plugin_tooltip --plugin-id <id> --view-id <view> --element-id <element> [--row-id] [--column-id]
[--node-id] [--no-cache]` runs the same query path and returns the tooltip, its source (`static`,
`cache`, `plugin`) and the latency. Tests and MCP use it; nothing else in the GUI changes.

## 10. Tables

### 10.1 Cells, rows and columns

A cell is a string (as today) or an object:

```json
{"id": "r7",
 "icon": "status:ok",
 "checked": false,
 "cells": [
   {"text": "Board 7", "icon": "builtin:device", "tooltip": "Bench 2"},
   {"text": "Writing", "tone": "accent", "progress": 0.42},
   "3.2.1"
 ]}
```

| Level | New keys |
| --- | --- |
| Cell | `text`, `icon`, `tooltip`, `tooltip_key`, `tone`, `bold`, `progress` (0..1, drawn behind the text), `sort` (sort value) |
| Row | `icon` (in the first column), `checked` (with a check column), `tooltip_provider`, `tooltip_key`, `drag` (section 13.5) |
| Column | `align` (`left`, `center`, `right`), `hidden`, `sortable`, `sort_as` (`text`, `number`, `natural`), `min_width`, `check` (a check box column bound to the row's `checked`), `tooltip`, `tooltip_provider`, `icon` |
| Table | `header` (default true; false makes a list), `toggle_selection` (section 10.2), `context_menu`, `copy` (default true), `export` (default false), `reorderable`, `keep_columns` (default true) |

On an older host a cell object would show an empty cell, so the SDK's `cell()` builder sends the
plain text when the host lacks `rich_tables`.

### 10.2 Selection

- `multi` stays Qt's extended selection: click selects one row, Ctrl+click adds or removes a row,
  Shift+click selects a range, Ctrl+A selects all, and Shift with the arrow keys extends.
- New `toggle_selection: true` with `selection: "multi"`: every click adds or removes a row (no keys
  needed); Shift+click still selects a range. It is a new key, not a new `selection` value, because
  an older host turns an unknown `selection` value into no selection at all.
- The plugin sets the selection with a patch `{"id": "t", "select": ["r1", "r2"], "current": "r1",
  "scroll_to": "r1"}`; it does not echo back as an event.
- `selected` events also carry `current` (the row with the keyboard focus).

### 10.3 Events

| Event | When | Value |
| --- | --- | --- |
| `activated` | Double-click or Enter on a cell that is not editable | `{row_id, column_id}` |
| `checked` | A row's check box changed | `{row_id, checked}`; a click with Shift sets the range |
| `moved` | The user dragged rows of a `reorderable` table | `{row_ids, before}`; the plugin confirms with a patch, as for edits |

### 10.4 Context menus on elements

`"context_menu": "<menu id>"` on an element names a menu with `location: "element"`. Entries
receive the view, element and the clicked row or node with the selected ids. Tables and trees also
keep the host entries **Copy** (Ctrl+C, selected rows as tab-separated text), **Copy All** and,
with `export`, **Export CSV…**; header menus show and hide columns.

### 10.5 Kept state

With `keep_columns`, the host keeps column widths, order, hidden columns and the sort per view.
E17d introduces this per-view state store; E17g extends it (section 13.6).

## 11. Trees

### 11.1 `tree`: plugin data

```json
{"type": "tree", "id": "items", "selection": "multi", "checkboxes": true, "lazy": true,
 "columns": [{"id": "name", "title": "Name"}, {"id": "size", "title": "Size", "align": "right"}],
 "nodes": [
   {"id": "n1", "text": "Images", "icon": "builtin:folder", "has_children": true},
   {"id": "n2", "cells": ["readme.txt", "2 KiB"], "icon": "builtin:file", "checked": true}
 ]}
```

- **Nodes:** `id` (unique in the tree), `text` or `cells`, `icon`, `tooltip`, `tooltip_key`, `tone`,
  `children`, `has_children` (with `lazy`), `expanded`, `checkable`, `checked` (`true`, `false`,
  `null` for partly), `editors` (rename in place, like cell edits), `drag`.
- **Lazy children:** expanding a node with `has_children` and no children shows "Loading…" and sends
  `tree/children` `{view_id, element_id, node_id}`; the answer `{"nodes": [...]}` fills it. A
  failure shows "Could not load" and retries on the next expand.
- **Patches:** `replace_nodes`, `update_nodes`, `insert_nodes` (`parent`, `before`), `remove_nodes`,
  `expand`, `collapse`, `select` (with `current`, `scroll_to`).
- **Selection:** `none`, `single` or `multi` (Shift and Ctrl as in tables), and
  `toggle_selection`.
- **Events:** `selected` (node ids, count, `current`), `activated`, `checked`, `edited`, `moved`,
  and `expanded` / `collapsed` with `expand_events: true`.
- `auto_tristate: true` makes parent check boxes follow their children.
- Up to 50,000 nodes per tree and depth 64. A large tree arrives in batches (`insert_nodes`
  patches or lazy children), since one message carries at most 1 MiB.

### 11.2 `file_tree`: the folder and file browser

```json
{"type": "file_tree", "id": "images", "roots": ["C:/Firmware"], "filters": ["*.bin", "*.hex"],
 "selection": "multi", "select": "both", "columns": ["name", "size", "modified"]}
```

- The host shows the file system under the given absolute `roots` (at most 16) with the native
  file model: loading is lazy and changes on disk appear by themselves.
- `filters` (name globs), `hide_filtered` (hide or grey out non-matching files), `show_files`
  (default true), `show_hidden` (default false), `columns` (`name`, `size`, `type`, `modified`).
- `selection`: `none`, `single` or `multi` (Shift and Ctrl), and `toggle_selection`; `select`:
  `files`, `dirs` or `both` (what can be selected); `checkboxes` for a pick list.
- `value`: the selected paths, in `inputs` and in `selected` events (at most 1,000 paths, with the
  count); a patch that sets `value` selects and reveals them; `expand` opens folders.
- Events `selected` and `activated` (double-click on a file, `{path}`); `context_menu` entries
  receive the selected paths. Dragging files out works like dragging them from the system file
  manager (section 13.5).
- The host only lists names and sizes; it never reads file contents. Network paths are allowed but
  never expanded on their own (question 12).

## 12. Layout and content

### 12.1 Layout

| Type | Properties |
| --- | --- |
| `tabs` | `children` are `tab` elements; `current`; `position` (`top`, `bottom`, `left`, `right`); event `changed` |
| `tab` | `title`, `icon`, `badge` (a short count or text), `children` |
| `splitter` | `orientation`, `sizes` (kept per view), `children` |
| `form` | Children in two aligned columns: captions (the field `label`, or `label` on any child) and controls |
| `grid` | `columns`; children with `span` and `row_span` |
| `separator` | A horizontal or vertical line |
| `spacer` | `size` or `stretch` |

Every element may set `stretch`, `min_width`, `max_width`, `min_height`, `max_height` and `align`.

An older host drops an unknown container with everything inside it (and rejects an update whose
root it does not know), so the SDK builders fall back when the host lacks `layout`: `tabs` becomes a
`column` of `group`s titled like the tabs, `splitter`, `form` and `grid` become a `column` of `row`s,
and `separator` and `spacer` are left out.

### 12.2 Content

| Type | Properties |
| --- | --- |
| `text_view` | `text`, `format` (`plain`, `markdown`), `monospace`, `wrap`, `follow` (scroll to new text), `max_lines` (oldest lines dropped), find with Ctrl+F; patch `{"id", "append": "..."}`; event `link` `{href}` |
| `image` | `source` (a plugin-folder file or a registered picture, section 6), `width`, `height`, `scale` (`fit`, `fill`, `none`), `alt` |
| `sparkline` | `values` (at most 1,024), `min`, `max`, `kind` (`line`, `bar`), `tone` |
| `chart` | `kind` (`line`, `bar`, `scatter`, `area`), `series` (`id`, `title`, `points`, `tone`), `x_title`, `y_title`, `x_kind` (`number`, `time`), `legend`, `window` (keep the last N points); patches `append_points` `{series, points}` and `clear` |

- **Markdown** (question 9): CommonMark through Qt's Markdown reader with HTML turned off. Images
  only from icon references; links are never opened by the host: clicking one sends the `link` event.
  On Qt older than 5.14 the text is shown as plain text.
- **Charts** (question 16) are drawn by a small host widget: at most 8 series of 10,000 points,
  without a Qt Charts dependency. When the V01/V02 chart renderer exists, `chart` may move to it
  without changing the element.
- Large contents arrive in batches, since one message carries at most 1 MiB: `append` patches for
  `text_view`, `append_points` for charts.

### 12.3 Views and dialogs

- **Buttons** gain `icon`, `style` (`push`, `tool`, `link`), `menu` (a drop-down menu id),
  `checkable` and `checked` (event `changed`), `shortcut` and `default` (pressed by Enter).
- **Busy overlay:** `busy: true` on the root or a container covers it with a spinner and
  `busy_text`, and blocks input there; `busy_cancel: true` adds a Cancel button that sends a
  `cancel` event.
- **Unsaved changes:** `modified: true` on the root marks the title with `*`; closing the view then
  asks "Discard changes?" (or the view's `close_confirm` text). The plugin decides nothing at close
  time, so no query is needed.
- **Dialog buttons:** a dialog view declares `buttons` (`ok`, `cancel`, `apply`, `close`, `yes`, `no`)
  and `default`. A button sends `clicked` with `value` = the button name; `ok`, `yes`, `cancel`, `no`
  and `close` also close the dialog, and `view/closed` then carries `result` and `inputs`. With
  `confirm_by_plugin: true`, `ok` and `yes` only send the event; the plugin closes the dialog with
  `view/hide` when it accepts the input.
- **Focus:** a patch `{"id": "x", "focus": true}` moves the keyboard focus (only while the view is
  active, so a plugin cannot steal the focus from another window).

### 12.4 Accessibility

- The accessible name of an element becomes its caption or text (field `label`, button `label`,
  `checkbox` `text`, group `title`), falling back to the id; `accessible_name` and
  `accessible_description` override them. A static tooltip is the accessible description when none
  is given.
- Field captions become the label buddies of their controls; the focus order follows the element
  tree; validation messages are announced.
- Tones become palette-aware (section 13.7), so they keep their contrast in dark themes.

## 13. Window integration

### 13.1 Toolbars

```json
"toolbars": [
  {"id": "device", "title": "Device Tools", "items": [
    {"id": "connect", "view": "main", "icon": "builtin:port", "text": "Connect"},
    {"id": "write", "action": "write", "icon": "builtin:upload"},
    {"type": "separator"},
    {"id": "mode", "menu": "modes", "icon": "builtin:settings", "text": "Mode"}
  ]}
]
```

Each toolbar is a real toolbar (`pluginToolbar_<plugin>_<toolbar>`) that the user can move, hide and
show from the toolbar menu; its place is saved with the window. Items take the menu entry keys
(`action`, `view`, `event`, `checkable`, `shortcut`, `icon`, `text`, `tooltip`) or `menu` for a
drop-down. `toolbar/update` changes them at run time. At most 2 toolbars of 16 items per plugin.

### 13.2 Status-bar items

```json
"status_items": [
  {"id": "board", "text": "No board", "icon": "status:muted", "align": "right", "view": "main",
   "tooltip_provider": true}
]
```

A short text (64 characters) with an icon, left or right; clicking it runs its `action`, `view`,
`event` or `menu`. `status/update` `{id, set: {text, icon, tone, tooltip, visible}}` changes it. At
most 4 items per plugin.

### 13.3 Notifications

`ui/notify` `{notification_id, title, text, tone, icon, timeout_ms, actions: [{id, label}],
window_id}` shows a non-modal notification in the window's corner (at most 3 at once, newest on
top); when the window is minimized, the system tray shows it. `ui/notification_result`
`{notification_id, action | "closed" | "timed_out"}` answers. Default timeout 8 seconds, 0 keeps it
until closed; at most 3 actions; at most 5 new notifications per 10 seconds per plugin
(question 15).

### 13.4 Clipboard

`ui/clipboard` `{text}` puts plain text (at most 1 MiB) on the clipboard. Plugins cannot read the
clipboard through the host; the user pastes into fields.

### 13.5 Drag and drop

- `accept_drops` on an element (`files`, `text`, `rows`) receives drops as a `dropped` event
  `{files, text, rows, source_element, target, position}` (`target` is the row or node under the
  pointer, `position` `before`, `after` or `on`). At most 1,000 files and 1 MiB of text.
- Rows and nodes with `drag` (or `drag_rows: true` on the table) can be dragged as text (their cells,
  tab-separated), as row ids within the plugin, and, when they carry `path`, as files: dropping
  them on a pane opens the file there.
- `file_tree` items drag as files.

### 13.6 State and session restore

- The host keeps, per view: splitter sizes, the current tab, table columns, expanded tree nodes (at
  most 1,000) and the values of fields with `persist: true`.
- `view/state` `{view_id, state}` stores a plugin JSON value of at most 64 KiB per view; the host
  returns it as `state` in `view/opened`. This extends the per-view state store that E17d
  introduces for table columns.
- A view with `"restore": true` in the manifest is reopened at the next start if it was open at
  exit, with its placement; this starts the plugin. The user can turn restoring off per view
  (question 17).

### 13.7 Theme

`initialize` carries `theme` `{dark, palette}` (section 4.1). Plugins that list
`ui/theme_changed` in `handles` get it again when the theme changes. Tones (`ok`, `warn`, `error`,
`muted`, `accent`) get dark-theme variants, and `{"light", "dark"}` icons switch automatically.

### 13.8 Settings pages in Options

**Options** gains a **Plugins** page. Every enabled plugin with `configuration` fields gets a
sub-page built from them, the same form as Configure in the Plugins and Packs window today. A
plugin may name a view instead (`"settings_view": "settings"`): the host opens it inside the page
(starting the plugin), binds its fields whose ids match configuration keys, and on OK or Apply
sends `configure` with the merged values after checking them against the declared fields. Secrets
stay password fields and are never sent back to the view (question 14).

### 13.9 Message boxes and file dialogs

- `ui/message` gains `details` (expandable text, at most 64 KiB), `checkbox` `{text, checked}` (for
  example "Don't ask again"; the result carries `checked`) and `input`
  `{kind: text|secret|choice|multiline, value, choices, placeholder}` (the result carries
  `value`).
- `ui/file_dialog` `{request_id, mode: open|open_many|save|directory, title, filter, directory,
  suggested_name, window_id}` shows a native file dialog; `ui/file_dialog_result`
  `{request_id, paths, cancelled}` answers. At most one open per plugin and window.

### 13.10 Host events

A view that follows the focused tab learns its context only when it opens (U15b), and no plugin
learns about tabs opening, closing or the log selection. Plugins that list them in `handles` get,
debounced:

| Notification | When | Parameters |
| --- | --- | --- |
| `ui/focus_changed` | The focused tab of a window changed (200 ms) | `window_id`, `tab_id`, `context` |
| `tab/opened`, `tab/closed` | A tab opened or closed | `window_id`, `tab_id`, `context` |
| `log/selection` | The selection of a log or filtered view changed (300 ms) | `window_id`, `tab_id`, `view`, `ranges` (at most 64), `count`; never the text |

A view that follows the focused tab also gets `view/opened` again with the new context when the
focus moves, if the plugin lists `ui/focus_changed`.

## 14. Host commands from plugins (optional slice E17i)

Plugins control CILogg today by running `"$CILOGG_EXECUTABLE" command --action ...`, which starts a
process per command. A bounded channel would be faster and could be audited:

- The plugin sends the notification `host/command` `{request_id, action, options}`; the host
  answers with `host/command_result` `{request_id, ok, result, error}`. It is a notification pair,
  but it is a request in substance, so it changes the statement "the plugin never sends requests to
  the host" of decisions D11 and D14. It is therefore a separate slice that needs explicit approval
  (question 13).
- Only an allowlist runs: go to a line, select lines, add or remove marks, set the search, open a
  file, list tabs, and pause, play or stop a serial capture with `--wait`. Not allowed: plugin
  management, scripts, scenarios, sending data to devices, settings and credentials.
- Requests run through Commander with origin `plugin` and the plugin id, are journaled (without
  payloads) and limited to 20 per second per plugin. The authority is no larger than the CLI the
  plugin can already run.

## 15. Automation, CLI and MCP

| Operation | Purpose |
| --- | --- |
| `get_plugin_menus --location <location> [tab selector] [--plugin-id]` | The resolved entries with their state and conditions |
| `trigger_plugin_menu --plugin-id --menu-id --item-id [tab selector] [--lines 10-20,30] [--checked]` | Runs an entry like a click, with an explicit selection for `log` entries |
| `get_plugin_tooltip ...` | Section 9.5 |
| `get_plugins` | Gains `sdk`, `features`, `requires` results, query statistics and shortcut conflicts |

- New widgets have object names (`..._<element>` as today; tree and file tree selections, tabs,
  radio and check groups are readable and settable through `get_ui` / `set_ui`), so UI automation
  and MCP can drive them as they drive E13 views.
- Native MCP discovers the operations through the executable help, as before.

## 16. SDKs, typing and the test harness

- **Python SDK** (`plugins/sdk/python/cilogg_plugin.py`, standard library only):
  - builders for every new element, menu entry, toolbar item and tooltip;
  - decorators `@plugin.tooltip(view, element)`, `@plugin.menu_populate(menu, item)`,
    `@plugin.tree_children(view, element)`, `@plugin.complete(view, element)` and
    `@plugin.on_command(menu, item)`; the SDK lists the matching methods in `features` by itself;
  - `notify`, `status`, `clipboard`, `file_dialog`, `register_icons`, `save_view_state`;
  - `supports(feature, version=1)` and `host_limits`;
  - forward compatibility: an unknown host request is answered with the JSON-RPC error `-32601`
    (method not found) and an unknown notification is ignored, instead of ending the plugin;
  - backward compatibility: builders produce the older form when the host lacks a feature
    (sections 8, 10 and 12.1), and helpers that send a new plugin-to-host notification do nothing on
    a host without its feature, since an older host logs a warning for every unknown method;
  - at start, a warning on stderr when a registered handler's method is missing from `handles`;
  - full type hints (`TypedDict` for messages and elements), checked with `mypy --strict` in CI.
- **Test harness** `cilogg_plugin_testing.py`: a `FakeHost` that starts a plugin in-process or as a
  subprocess, performs `initialize`, invokes actions, opens views, sends events, clicks menu
  entries, asks queries with timeouts and records every frame, so a plugin author can test without
  CILogg.
- **C++ SDK** (`plugins/sdk/cpp/cilogg_plugin.h`): `onQuery(method, handler)` with a worker, element
  builders, `features` and `supports()`, and `cilogg_plugin_testing.h` with the same fake host; a
  C++ view example (an E13 limitation today).
- **JSON Schemas** for `plugin.json`, the view elements and every message, under
  `Resources/schemas/plugins/` (the place E01 plans for its capability schemas), and a validator:
  `python -m cilogg_plugin validate <folder>`.
- **Protocol log:** the Plugins and Packs window shows the last 200 frames per plugin (method, id,
  size, direction, time). Payloads are shown only after the user turns on "Show contents" for that
  plugin; the log lives in memory only and never enters problem reports.
- **Developer mode:** with a folder installed as a local plugin, "Restart on change" restarts the
  plugin when its files change.
- **Examples:** a new `plugins/examples/ui-gallery` (Python, disabled by default) that grows with
  each slice and shows every feature; `view-demo` stays as it is (question 21).

## 17. Security

- The trust boundary is unchanged: a plugin is a program the user installed, running with the
  user's permissions.
- No new way for a plugin to make the host act in E17a to E17h: queries go from the host to the
  plugin (section 5.3). Section 14 is the only exception, separately approved.
- Text is plain text; host-built rich text escapes every plugin string; Markdown has HTML off, no
  remote content and links that only send an event.
- Icons and images: plugin-folder files with safe relative paths, built-ins, or registered images
  with size, count and decoded-size limits; SVG references other than `data:` URIs are refused.
- The file tree lists only the roots the plugin names, lazily, with the user's permissions; the host
  never reads file contents for it.
- Context menus send the selected text only when the user picks a plugin entry.
- Shortcuts cannot take over CILogg's own shortcuts; focus can move only within an active view.
- Notifications, menus, queries and messages are rate-limited and size-limited (section 18).
- The activity journal records ids, sizes and timings, never payloads.

## 18. Limits

| Item | Limit |
| --- | --- |
| Menus per plugin | 16 menus, 256 entries in total, depth 4, titles 128 characters, 8 top-level entries per location before "More" |
| Dynamic menu answer | 256 entries |
| Shortcuts per plugin | 32 |
| Toolbars per plugin | 2, of 16 items |
| Status items per plugin | 4, text 64 characters |
| Notifications | 3 shown per window, 5 new per 10 seconds per plugin, 3 actions |
| Outstanding queries | 8 per plugin; per view 1 tooltip, 1 completion, 4 tree children (16 queued); per window 1 `menu/populate` and 1 status-item tooltip |
| Host events | `ui/focus_changed` debounced 200 ms, `log/selection` 300 ms with at most 64 ranges |
| Tooltip answer | 16 KiB of text, 32 rows; cache 256 entries per view |
| Registered icons | 256 per plugin, 64 KiB each, 4 MiB in total, decoded at most 256 × 256 |
| Registered pictures (`image` element) | 16 per plugin, 1 MiB each, decoded at most 4,096 × 4,096 |
| Trees | 50,000 nodes, depth 64, 1,000 nodes per `tree/children` answer |
| File tree | 16 roots, 1,000 selected paths reported (with the count) |
| `text_view` | 1 MiB of text, `max_lines` at most 100,000, 64 KiB per `append` |
| Charts | 8 series of 10,000 points; sparklines 1,024 values |
| View state | 64 KiB per view, 1,000 expanded nodes |
| Clipboard | 1 MiB |
| Drops | 1,000 files, 1 MiB of text |
| Validation pattern | 256 characters |
| Existing limits | Unchanged: 512 elements per view, 50,000 cells, 64 columns, 1 MiB per message, and 4,096 characters per text property except where this table sets another limit (`text_view`, tooltip answers, message box `details`) |

Contents larger than one message (trees, charts, `text_view`) arrive in batches through patches.

## 19. Implementation map

- **Protocol and queries:** `src/plugins/src/pluginprocess.cpp` (initialize keys, the plugin's
  `features`), a new `src/plugins/src/pluginqueries.{h,cpp}` (pending queries, timers, cancellation,
  concurrency, circuit breaker, statistics), `PluginManager::handleMessage` (routes `q:` responses
  to the queries before the job lookup; new notifications join the UI message list),
  `src/plugins/src/pluginmanifest.cpp` (`menus`, `toolbars`, `status_items`, `settings_view`,
  `requires`, `shortcut` validation).
- **View renderer:** `pluginview.cpp` first loses its table model and delegate to
  `plugintablemodel.{h,cpp}` without behaviour change (E17a), then new elements live in their own
  files: `plugintooltips.cpp` (queries, cache, rendering), `pluginchoicewidgets.cpp`,
  `plugintree.cpp`, `pluginfiletree.cpp`, `pluginlayout.cpp`, `plugintextview.cpp`,
  `pluginchart.cpp`.
- **Menus:** a new `src/ui/src/pluginmenus.{h,cpp}` (menu registry per window, `when` evaluation,
  dynamic submenus, shortcuts). Hooks: `PluginUiController::refreshActions` (Plugins menu and the
  menu bar), the tab menu through `MainWindow::extendTabContextMenu`, `populateEmptyPaneMenu` and
  `populatePaneMenu`, and `AbstractLogView` through a new `PluginMenuHost` interface (like
  `PaneMenuHost`) that MainWindow implements and that the log view calls when its menu opens.
- **Window integration:** MainWindow toolbars and status bar, a notification widget, the Options
  dialog (`src/ui/src/optionsdialog.cpp`), the shortcut registry (`src/settings/include/shortcuts.h`).
- **Commander:** the operations of section 15 in the command tables, CLI help and native MCP.
- **Coordination with E16:** E16a changes `Manifest::parse` and the Plugins and Packs window, which
  E17b also touches. Each slice merges master first. The new top-level keys are process keys, so
  configuration packs refuse them like `actions` and `views`.
- **Coordination with E01:** E01 later absorbs `host_feature_versions` and the plugin's `features`
  into its capability registry, without changing these messages.

## 20. Tests

- **Every slice:** Catch2 cases tagged `[workbench][E17]` in a new `tests/ui/workbench_e17_test.cpp`
  and `[plugins]` cases in `tests/unit/plugins_test.cpp`; Python SDK tests under `tests/plugins/`
  (builders, `features`, query handlers, forward compatibility, the fake host); an `E17` case in
  `tests/mcp/workbench_parity.py` with the GUI, CLI, native MCP and Python reaching the same
  results; `ui-gallery` driven by `tests/plugins/host_integration.py`. Existing E13, E14 and E15
  suites and the ADB, syslog and view-demo fixtures keep passing unchanged.
- **Golden frames:** a plugin without `handles` records every frame it receives and sends through a
  scripted session (actions, a view with every E13/E14 element, edits, message boxes); the frames
  must equal the recording made before E17 except for the additive `initialize` keys.
- **Version skew:** pinned copies of the released Python and C++ SDKs (as plugins ship them) run the
  fixtures against the new host; a test helper that keeps the E14 manifest rules (the enumerations
  and the `icon` rule) accepts every E17 example manifest, so the previous CILogg still loads them.
- **E17a:** initialize keys and versions; `handles` and `features` (a method listed in only one is
  never sent); `requires` refusing a plugin; queries: answer, error, `null` (not counted by the
  breaker), invalid result, timeout with `query/cancel`, late answer dropped, an answer from another
  plugin dropped, nothing sent before ready or while the plugin is not reading, concurrency limits,
  circuit breaker and statistics, stop and restart dropping queries; tooltips: static on every
  target, advanced with cache hit, expiry, invalidation by patch and by `tooltip/invalidate`, pointer
  leaving, the rendered parts escaped, element tooltips that look like HTML shown as plain text;
  icons: file, built-in, status, registered icons and pictures, light and dark, `icon_ref` on actions
  and views, refused SVG, limits; accessible names; the Python SDK answering unknown requests with
  `-32601` and its builders falling back on an older host; the golden-frame and version-skew tests;
  the table model extraction with the E13/E14 suites unchanged.
- **E17b:** manifest validation of menus; entries in each location with the right context; `when`
  conditions; checkable and radio entries; `menu/update`; dynamic submenus (answer, timeout, cache);
  `action`, `view` and `event` entries; the selected text only on trigger; placement, "More" and
  hiding; shortcuts (registry, rebinding in Options, conflicts); `get_plugin_menus` and
  `trigger_plugin_menu`.
- **E17c to E17g:** each element, event, patch and limit of sections 8 and 10 to 13, including
  Shift and Ctrl selection in tables, trees and the file tree, cell editors with icons and radio
  marks, validation blocking buttons, Markdown with HTML off, drag and drop between a table and a
  pane, state kept across a restart, and the settings page writing `configure`.
- **Build gates:** MSVC `/W4 /WX`, and GCC and Clang with `-Wshadow -Wconversion -Wsign-conversion
  -Werror` (logging macros braced, no default argument of a nested struct with member
  initializers).

## 21. Slices

One branch and pull request per slice, each with C++ host, Python and C++ SDK, tests, docs and the
ledger. The order follows the dependencies and the request:

| Slice | Content | Needs |
| --- | --- | --- |
| **E17a** | Negotiation and `handles` (section 4), host queries (section 5), icons (section 6), plain-text element tooltips, static tooltips on existing elements and column headers, advanced tooltips on every existing element and per table cell (section 9), accessibility names (section 12.4), table model extraction, SDK forward and backward compatibility, typing, the fake host, golden-frame and version-skew tests | — |
| **E17b** | Menus, context menus, dynamic submenus, `ui/command`, shortcuts, `get_plugin_menus`, `trigger_plugin_menu` (section 7), with tooltips on menu entries | E17a |
| **E17c** | Choice styles and icons, searchable and editable drop-downs, completion, `checkbox`, `radio_group`, `check_group`, cell editor choices, new field kinds, validation (section 8), with tooltips on choices | E17a |
| **E17d** | Rich tables: cell objects (with their static tooltips), row icons and check boxes, `toggle_selection`, selection patch, `activated`/`checked`, reordering rows inside the table (`moved`), element context menus, Copy and Export, the per-view state store with kept columns (section 10) | E17a, E17b |
| **E17e** | `tree` and `file_tree` (section 11), with node tooltips and reordering inside the tree | E17a, E17d |
| **E17f** | Layout, `text_view`, `image`, `sparkline`, `chart`, button styles with icons, drop-down menus and shortcuts, busy overlay, dialog buttons and results, focus (section 12) | E17a, E17b |
| **E17g** | Toolbars, status items (with their tooltips), notifications, clipboard, drag and drop between elements, files and panes (including dragging rows, nodes and files out), the rest of view state and restore, theme, settings pages, message box input, file dialogs, host events (section 13) | E17b, E17d, E17e |
| **E17h** | JSON Schemas, the validator, the protocol log, developer mode, the C++ view example (section 16) | E17a |
| **E17i** | Host commands (section 14), only if question 13 is approved | E17a |

## 22. Considered and not proposed in E17

- **Plugin views as tabs inside panes** (a plugin editor next to log tabs): it touches the tab model,
  sessions and the tab Commander operations of U16; better as its own task after U16c
  (question 18).
- **Tables beyond 50,000 cells** through paged queries: no current plugin needs it; the caps stay
  (question 19).
- **Log line annotations from plugins** (gutter marks, line decorations, extra columns in the log
  view): they belong with the typed event metadata of F01 and the V tasks.
- **Plugin-provided search and filter engines** and **custom preview renderers**: E16 and the MD tasks
  own previews and decoders.
- **Localization of plugin texts:** CILogg itself is not localized; plugins can choose texts from
  their configuration.
- **HTML anywhere, scripts in views, web views:** excluded by the plain-text rule.

## 23. Open questions

Each question has a recommended answer; approving the design approves the recommendations unless
the user says otherwise.

1. **Where a plugin's own menu goes.** *Recommendation:* its submenu under **Plugins** by default; a
   `menubar` menu is allowed, one per plugin, placed after **Plugins**, and the user can move it back
   under **Plugins** in the Plugins and Packs window.
2. **Today's flat entries when a plugin declares a Plugins menu.** *Recommendation:* the declared
   submenu replaces that plugin's flat entries; plugins without `menus` keep today's entries
   unchanged.
3. **Plugin entries in context menus.** *Recommendation:* one section after the host entries, in
   plugin name order, at most 8 top-level entries per plugin and location before "More", and a
   per-plugin, per-location switch to hide them.
4. **What "right click pane" covers.** *Recommendation:* the menu of an empty pane, the **Panes**
   submenu of the log and filtered views, and the pane commands at the end of the tab menu; "log
   window" is the main log view and the filtered view, which plugins can tell apart with
   `when.views`.
5. **Data sent when a menu opens.** *Recommendation:* `menu/populate` gets ids, the line under the
   pointer and the selected line ranges, never the selected text; the text (at most 64 KiB) goes to
   the plugin only when the user picks one of its entries.
6. **Shortcut conflicts.** *Recommendation:* CILogg's and the user's shortcuts win; between plugins
   the first plugin to claim a shortcut keeps it; the others get none and are listed with a warning;
   the user can rebind every plugin shortcut in **Options > Shortcuts**.
7. **Advanced tooltip defaults.** *Recommendation:* timeout 1,500 ms (at most 5,000), "Loading…"
   after 250 ms without a static tooltip, cache time 30 seconds (at most 10 minutes), 256 entries per
   view, circuit breaker after 5 failures for 60 seconds.
8. **Tooltip content.** *Recommendation:* plain text or the structured form of section 9.2 (title,
   text, rows, tone, icon, footer), rendered by the host; no HTML and no Markdown in tooltips.
9. **Rich text.** *Recommendation:* Markdown only in `text_view`, HTML off, images only from icon
   references, links sent to the plugin as events and never opened by the host; plain text on Qt
   older than 5.14.
10. **Plugin-registered images.** *Recommendation:* allowed, PNG and SVG, with the limits of
    section 6, and SVG limited to `data:` references.
11. **Check boxes and radio buttons as elements or as field styles.** *Recommendation:* both: the
    `checkbox`, `radio_group` and `check_group` elements for free layouts and the field `style` for
    forms, built on one widget implementation.
12. **File tree reach.** *Recommendation:* the plugin names absolute roots (at most 16); the host
    lists lazily with the user's permissions, never reads contents, shows hidden files only on
    request and never expands network paths on its own.
13. **Host commands from plugins (section 14).** *Recommendation:* yes, as the last slice E17i, with
    the allowlist, origin `plugin`, journaling and rate limit described there. Until it lands,
    plugins keep using `CILOGG_EXECUTABLE`. Because it changes what D11 and D14 promised, it needs
    an explicit yes, separate from the rest of the design.
14. **Settings pages.** *Recommendation:* an automatic Options page for every plugin with
    configuration, plus an optional `settings_view`; values still go through `configure` and secrets
    stay write-only.
15. **Notifications.** *Recommendation:* in-window notifications in the corner (3 at most), the
    system tray when the window is minimized, the rate limit of section 18; a notification history
    is not part of E17.
16. **Charts.** *Recommendation:* a small host widget without a Qt Charts dependency, bounded as in
    section 18, moved onto the V01/V02 renderer when that exists.
17. **Session restore of views.** *Recommendation:* opt-in per view with `restore: true`, a user
    switch to turn it off, and restoring starts the plugin.
18. **Plugin views as pane tabs.** *Recommendation:* not in E17; a separate task after U16c.
19. **Paged tables beyond 50,000 cells.** *Recommendation:* not in E17; revisit when a plugin needs
    it.
20. **Order and authorization.** *Recommendation:* after approval, E17 is authorized ahead of
    G02/G03 like E13a and E15, in the slice order of section 21 (E17a first), alongside E16 with
    master merged into each branch; E17i only with a yes to question 13.
21. **Examples.** *Recommendation:* a new `plugins/examples/ui-gallery` (Python, disabled by default)
    that grows with each slice, a C++ view example in E17h, and `view-demo` unchanged.
22. **Does opening a dynamic submenu start a stopped plugin?** *Recommendation:* yes, as clicking one
    of its actions does, with "Loading…" until the answer (10 seconds in all); tooltips, completion
    and every other query never start a plugin, and a stopped plugin's targets show their static
    content.

## 24. E17a as built

E17a implements sections 4 to 6, 9 (static and advanced tooltips on the existing elements, table
column headers and table cells), 12.4, the `get_plugin_tooltip` operation of section 15 and the
SDK parts of section 16 that do not depend on later slices. Where the design left a choice open,
E17a decided as follows.

- **Feature versions.** `views` is version 2: version 2 adds the element properties
  `accessible_name` and `accessible_description`. `view_edit`, `queries`, `tooltips` and `icons` are
  version 1. A plugin uses a feature only when `host_features` lists it (`supports()` in both
  SDKs); an older host that sends only `host_features` has version 1 of each feature it lists.
- **Where the code is.** `src/plugins/src/pluginfeatures.cpp` (feature table, limits, `handles`,
  `requires`, icon references, the tooltip answer check), `src/plugins/src/pluginqueries.cpp` (host
  queries), `src/ui/src/plugintablemodel.cpp` (the table model and cell delegate, moved out of
  `pluginview.cpp` without behaviour change), `src/ui/src/plugintooltips.cpp` (hover, cache,
  rendering) and `src/ui/src/pluginicons.cpp` (icon references and registered images).
- **Why a query is not sent.** `PluginManager::query` returns no id and a reason: `not ready` (the
  plugin is not running, or not reading its input), `not handled` (the method is missing from
  `handles` or from `features`), `paused` (circuit breaker), `busy` (eight outstanding) or `not a
  query method`. The target then falls back at once.
- **One tooltip query per view.** The exclusive key is the view in its window, so a new hover in
  the same view replaces the old query. `get_plugin_tooltip` uses the same path with its own key,
  so a hover never cancels it.
- **Images.** Besides the limits of section 6, the decoded images of one plugin (icons and
  pictures together) stay within 128 MiB, since a 1 MiB PNG can decode to 64 MiB. An SVG, whether
  registered or a plugin file, may embed only raster `data:` images (`image/png`, `image/jpeg`,
  `image/gif`) and fragments of itself; an embedded SVG is refused because it would be decoded
  without this check. `ui/icons` is applied once, whatever the number of windows.
- **Cache generations.** An answer that arrives after an invalidation of its target is shown but
  not kept, so a late answer never brings back stale content.
- **C++ SDK robustness.** A handler that returns an undefined value answers `null`, a handler
  that throws anything answers a JSON-RPC error, and at shutdown every pending query reports
  `expired()`, so handlers that poll it return at once.
- **What a tooltip query carries.** `view_id`, `window_id`, `element_id`, for a table cell
  `row_id` and `column_id`, the most specific `tooltip_key` (the row's, else the column's, else the
  element's), `timeout_ms` and the view's `context`. The cache key holds every key that applies.
- **Built-in icons** use the platform style's standard icons where one exists (for example
  `play`, `folder`, `warning`) and a symbol drawn in the palette's text colour otherwise, so all of
  them follow the theme. `record` is a red status dot.
- **Accessible names** of labels, badges and progress bars now come from their text (before E17a
  they used the element id), as section 12.4 asks.
- **Status in the Plugins and Packs window.** A plugin with unmet `requires` shows "Requires a newer
  CILogg (missing: …)"; a method paused by the circuit breaker adds "; tooltip/resolve paused until
  HH:MM:SS".
- **Journal.** Each finished tooltip query adds one activity journal entry with the plugin, view,
  element, row, column and latency, never the answer.
- **`get_plugin_tooltip`** returns `source` (`static`, `cache` or `plugin`), `tooltip` (the checked
  answer, or null), `html` (the rendered, escaped answer), `staticTooltip` and `latencyMs`.
  `--node-id` was refused until E17e added trees (section 28.3).
- **Frames.** `PluginProcess` emits every frame it sends and reads (`PluginManager::protocolFrame`);
  the golden-frame test uses it now and the protocol log of E17h will.
- **Left for later slices.** The C++ fake host (`cilogg_plugin_testing.h`) and the C++ view example
  come with E17h; the Python fake host is in E17a. `ui-gallery` shows E17a's tooltips, icons and
  accessible names and grows with each slice.

## 25. E17b as built

E17b implements section 7 (menus, context menus, dynamic submenus, `ui/command`, shortcuts) and
the `get_plugin_menus` and `trigger_plugin_menu` operations of section 15. Where the design left a
choice open, E17b decided as follows.

- **Feature versions.** `menus` and `shortcuts` are version 1.
- **Where the code is.** `src/plugins/src/pluginmenuspec.cpp` (manifest rules, `when`, the
  `menu/populate` and `menu/update` checks, shortcut assignment), `src/ui/src/pluginmenus.cpp` (the
  process-wide menu state, each window's menus, dynamic submenus, picked entries, shortcuts) and
  `src/ui/include/pluginmenuhost.h` (how a log view asks its window for plugin entries). The hooks
  are `PluginUiController::refreshActions` (Plugins menu and menu bar),
  `MainWindow::extendTabContextMenu` (location `tab`), `MainWindow::addPaneCommands` (location
  `pane`: the tab menu, the Panes submenu of the log views and the menu of an empty pane) and
  `AbstractLogView`'s context menu (location `log`).
- **Locations.** This host shows `plugins`, `menubar`, `tab`, `pane` and `log`. A manifest may
  name any other lowercase location (`element` and `toolbar` come with E17d and E17g); such a menu
  is checked like the others and not shown, so an older host never shows it out of place.
- **Entries.** A menu entry's `icon` takes every icon reference of section 6 (menus are a new key,
  so an older host never reads them). Separators and sections have no id. Menu and entry ids have
  no `/`, and no menu is called `action` or `view`, because ids name shortcuts. Titles are plain
  text: `&` shows as written (no mnemonic) and tabs and control characters show as spaces;
  tooltips are plain text like element tooltips. A shortcut is one key combination with Ctrl, Alt
  or Meta, or a function key; a list of two gives two combinations.
- **Conditions.** `views` holds only in the log views, `tab_kinds` and `file_patterns` only with a
  tab (the glob is matched against the file name, case-insensitively), and outside the log views
  there is no selection (`selection: none` holds). Tab kinds: `serial` for a COM capture,
  `plugin_stream` for a capture in the plugin data folder, `file` otherwise.
- **Plugins menu.** A plugin's submenus take the place of its flat entries where those used to
  be (plugin id order, as before). Its toolbar buttons stay.
- **Run-time state.** `menu/update` changes last until the plugin stops; check marks, whether set
  by the user or by `menu/update`, last for the session. A picked check box or radio entry changes
  its mark only when the command was sent or the action started (not after a refusal or a
  cancelled form). Disabling or removing a plugin forgets its menu state.
- **Dynamic submenus.** An answer is cached for its `cache_ms` per entry and menu/populate
  `context` (section 7.4), so a menu opened on another line, selection, row, node or window asks
  again. An answer without `cache_ms` is kept only as the last answer of the `plugins` and
  `menubar` locations; expired answers go, and all answers stay within 16 MiB, the oldest first. A
  `null` answer is an empty list and shows "No entries"; an error, a timeout or an invalid answer
  shows "Not available". There is one `menu/populate` at a time per window (the exclusive key of
  section 5.2). An entry of an answer sends its own id as `item_id`; its object name appends that
  id to the dynamic entry's.
- **`ui/command`.** It needs `ui/command` in `handles`. Up to 16 commands wait for a plugin that is
  starting; a plugin that stops before it is ready drops them with a warning in the log.
- **Selections.** Ranges hold file line numbers. The selection is described only when some
  enabled plugin shows `log` entries. A filtered view maps its rows to file lines one at a time,
  until the 64 ranges are full or 100,000 rows were mapped; `count` is always the total. The text
  is read when the entry is picked: whole lines joined with line feeds, or the selected part of
  one line, at most 64 KiB of UTF-8 with `truncated`.
- **Shortcuts.** Each window registers the assigned shortcuts as window shortcuts; the menu entries
  show their keys. A shortcut whose keys CILogg uses (its shortcut list, the window's actions and
  shortcuts, the reserved keys) is refused; with two combinations only the refused one is dropped.
  Between plugins the holder of a key keeps it while CILogg runs; at start the order is plugin
  name order. A menu entry's shortcut acts like a click with the current context: nothing when
  the entry is hidden or disabled there, the action's form when it has fields, `origin`
  `shortcut` in `ui/command`, and a message when it fails. Options > Shortcuts lists the plugin
  entries as "Plugins: <plugin>: <entry>" and stores a plugin shortcut only when the user changes
  it, so a plugin update can change its default. It offers each key once, and never a key that
  CILogg or another plugin holds (that would block the dialog as a duplicate); the row's tooltip
  says why a default was refused.
- **Hiding.** `plugin_manage --plugin-operation menus` with `{"hidden_locations": [...]}` (the
  Plugins and Packs window's "Menu Locations…" button) hides a plugin's entries per location;
  hiding `menubar` moves the menu back under Plugins. Shortcuts stay active.
- **`get_plugins`** gains `hiddenMenuLocations` and `shortcuts` (each with `key`, `title`,
  `declared`, `keys`, `state` (`active`, `refused`, `cleared` or `none`), `reason` and `userSet`).
  The Plugins and Packs window adds refused shortcuts to the plugin's state.
- **`get_plugin_menus`** returns `location`, `windowIndex`, `tabId`, the `context` a plugin would
  get (without the selected text) and `entries`: `pluginId`, `menuId`, `itemId`, `kind`, `title`,
  `path` (the submenu titles above it), `enabled`, `visible`, `checkable`, `checked`, `group`,
  `when`, `conditionsHold`, `shown`, `unavailable`, `shortcut` and `declaredShortcut`.
- **`trigger_plugin_menu`** acts like a click on a declared entry: it refuses a hidden, disabled or
  submenu entry, toggles a check box, checks a radio entry (`--checked true|false` sets the mark)
  and runs an action without its form (the entry's `arguments` and the field defaults). With
  `--lines` a `log` entry gets those file lines as its selection, clamped to the file, sorted and
  merged; `--lines` that names no line of the file is refused. A tab selector picks the window
  that holds the tab.
- **SDKs.** The C++ SDK gains `onCommand` (on the protocol reader thread, like `onNotification`)
  and `updateMenu`; `menu/populate` is an `onQuery` handler. The Python SDK gains
  `@plugin.menu_populate`, `@plugin.on_command`, `update_menu`, menu builders and `FakeHost`
  helpers (`populate_menu`, `send_command`). Both stay at SDK version 2.0.0: E17a and E17b ship
  together as the first E17 release.

## 26. E17c as built

E17c implements section 8 (choice entries, drop-down styles, check box and radio button elements,
more field kinds, validation) and the `field/complete` query of section 5.1. Where the design left
a choice open, E17c decided as follows.

- **Feature versions.** `choices`, `toggles`, `field_kinds` and `validation` are version 1.
- **Where the code is.** `src/ui/src/pluginchoicewidgets.cpp` (choice entries, filling a
  drop-down, the inline radio and check groups that fields and elements share, completion, the
  validation rules), `src/ui/src/pluginview.cpp` (the new kinds and elements, validity),
  `src/ui/src/checkcombobox.cpp` (choice entries and filtering in check-dropdowns),
  `src/ui/src/plugintablemodel.cpp` (cell editors and their icons),
  `src/plugins/src/pluginfeatures.cpp` (`sanitizeCompletion`) and
  `src/ui/src/pluginviewhost.cpp` (the `field/complete` query).
- **Choice entries.** A label is plain text; without one the value shows. Icons take every
  reference of section 6, tooltips are plain text. A disabled choice shows but cannot be picked.
  Separators and sections are never a value, and a check-dropdown's `All` summary does not count
  them. A list holds at most 512 entries.
- **Drop-down styles.** `indicator: "radio"` draws a radio mark before each entry, in fields and
  in cell editors. In a `searchable` drop-down (`choice` or `multichoice`, field or cell editor),
  typing while the list is open filters it by any part of a label, case-insensitively; the
  filter shows above the list, Backspace removes a character, and separators and sections hide
  while it is set. The keyboard selection moves to the first shown choice, and closing the list
  clears the filter. In a check-dropdown Space still toggles. Inline lists (`style` `radio` or
  `checkboxes`) get a filter line, `<element>_search`, above the buttons.
- **`editable`.** The typed text is the value, unless it equals the label of a choice that can
  be chosen, which gives that choice's value (a disabled entry's label stays text). Typing also
  filters the list. `changed` is debounced like text, whether
  the text was typed or picked, and new `choices` never replace typed text.
- **`completion`.** After 150 ms without typing, the host asks `field/complete`
  `{element_id, text, cursor}` (`cursor` counts characters, Unicode code points, as Python's
  `len()` does: an emoji before it counts once; one per view: a newer one cancels the older one)
  and shows up to
  100 items in a list under the field; picking one puts its `value` into the field. Each edit
  drops the answer still in flight, so only an answer for the current text is shown.
  `completion_timeout_ms` changes the 500 ms timeout, up to 2,000 ms. The answer is `{items}`, a
  bare list or `null`; an item needs a string `value`, its `label` is plain text, and an unusable
  `icon` is dropped. A timeout, an error or an invalid answer shows no list.
- **Patches.** `kind`, `style`, `editable`, `searchable`, `indicator` and `completion` decide which
  widgets a field gets: a root that changes one rebuilds the field, and a patch that changes one is
  refused with a warning, as for `children`.
- **Toggles.** A `checkbox`'s `text` is plain text (`&` shows as written); with `tristate`, `null`
  is partly checked. `radio_group` and `check_group` share the widget of the inline field styles:
  their buttons are `<element>_choice_<n>`, the All and None links `<element>_all` and
  `<element>_none` (accessible names "Check all" and "Uncheck all", with the title when there is
  one). `columns` is 1 to 12; without it the buttons form one column or one row.
- **Field kinds.**
  - `multiline`: `rows` (1 to 50, default 4) sets the height and `monospace` the system fixed
    font (a later root without them returns to four rows and the default font); Tab moves the
    focus on. Like a line field it holds at most 65,536 characters: a longer paste is cut in
    the editor.
  - `secret`: masked; UI inspection never shows its text.
  - `integer` (a spin box) and `slider` (its value shows in `<element>_value`): `min`, `max` and
    `step` are whole numbers; a bound that is not given is any `int` for a spin box, and 0 or 100
    for a slider.
  - `color`: a text line that takes `#rrggbb`, with a **Pick…** button (`<element>_pick`) that
    opens the color dialog.
  - `date` (`yyyy-MM-dd`), `time` (`HH:mm:ss`) and `datetime` (`yyyy-MM-ddTHH:mm:ss`, local time
    without a zone): the value is that text.
- **Validation.**
  - `required` treats `null`, an empty string and an empty list as missing. `pattern` must match
    the whole text; a pattern that does not compile checks nothing. Patterns run in the GUI, so
    each is compiled once, a value longer than 1,024 characters is left to the plugin, and a
    pattern that backtracks past PCRE2's match limit counts as no match. `min_length` and
    `max_length` (whole numbers of at least 0) count characters (Unicode code points: an emoji
    is one), `min` and `max` compare numbers.
    A non-empty `error` makes the field invalid with that message.
  - The messages are the host's own ("Required", "At least 3 characters", "Not in the expected
    form", "At most 10") unless the plugin sets `error`.
  - Validity is checked after each root, patch and change, and when a button is pressed. A rule
    counts by its value (`required: false` or an empty `pattern` is none, which is also what a
    later root that drops a rule leaves). `valid` is added only to the `changed` events of fields
    that have a rule, so a plugin without rules gets the frames it got before. A `color` field's
    own form is such a rule: it is blank or `#rrggbb`, a shorter `#abc` is only an editing state
    that is marked "Not a color (#rrggbb)", and its `changed` events always carry `valid`.
  - A same-shaped root that drops `min`, `max` or `step` returns the integer field or slider to
    the kind's default range (any int; 0 to 100) or a step of 1.
  - `requires_valid: true` means every field of the view. A button that starts an action never
    starts it while any field of the view is invalid: it is refused before its confirmation or
    save dialog, and the first invalid field gets the focus. An `event` button sends `clicked`
    unless its own `requires_valid` fails.
- **Cell editors.** `choice` and `multichoice` cell editors take the same entries and
  `searchable`, and a `choice` editor takes `indicator`. A committed choice's icon shows in the
  cell unless the cell sets its own.
- **Automation.** The new widgets are standard Qt widgets with object names, so `get_ui`,
  `set_ui` and `activate_ui` reach them (`value` for spin boxes and sliders, `date`, `time` and
  `dateTime` for the date edits, `checked` for check boxes and radio buttons).
- **SDKs.** The C++ SDK needs nothing new: `field/complete` is an `onQuery` handler. The Python SDK
  gains the choice, toggle and completion builders, the new `field` and `button` keywords,
  `@plugin.complete` and `FakeHost` helpers (`complete_field`, `change_field`); its builders
  downgrade a view for a host without these features. The SDK version stays 2.0.0.
- **Principle 11 (changed with E17d).** `valid` is added to `changed` events only for plugins that
  declare `handles` and only for fields with a rule.

## 27. E17d as built

E17d implements section 10 (cell objects, row icons and check boxes, column and table options,
the selection patch, `activated`, `checked` and `moved`, element context menus, Copy and Export,
kept columns) and the `element` location of section 7.2. Where the design left a choice open,
E17d decided as follows.

- **Feature versions.** `rich_tables` and `element_menus` are version 1.
- **Where the code is.** `src/ui/src/plugintablemodel.cpp` (cell objects, row and column options,
  check boxes, `PluginTableProxy` for `sort_as`), `src/ui/src/plugintablebehaviour.cpp` (toggle
  selection, check ranges, activation, reordering, the menus, Copy and Export, the selection patch,
  kept columns), `src/ui/src/pluginmenus.cpp` (`addElementMenu`, element contexts) and
  `PluginManager::keptViewState` (the per-view store, in the plugin settings).
- **Old plugins (principle 11).** `current` in `selected`, `valid` in `changed` and the
  `activated` event go only to plugins whose manifest declares `handles`; `checked` and `moved`
  come only from new keys.
- **Cells.** A cell object's `tooltip` wins over the row's, its `tone` over the row's, and its
  `sort` value replaces the text when sorting. `progress` is bounded to 0..1 and drawn behind the
  text (not under a selected row). The row's `icon` shows in model column 0; `drag` is accepted and
  ignored until E17g. The most specific `tooltip_key` goes into `tooltip/resolve`: the cell's, the
  row's, the column's, then the table's.
- **Icons.** Tables ask for icons on every paint, so the icon registry resolves a reference once
  per plugin, folder and palette (at most 512 per plugin, null results too) and again only after
  the plugin's registered images change or it stops (a stopped plugin may be updated on disk).
- **Columns.** `sort_as: text` compares case-insensitively, `number` puts text that is no finite
  number (`nan` too) last, `natural` compares digit runs as numbers, leading zeros of any length
  skipped without copying; without `sort_as` the order is as before E17d. The user can drag
  columns into another order.
  A header click on a column with `sortable: false` keeps the order. `min_width` is at most 2,000
  pixels. A `check` column shows its cell text next to the box.
- **Check boxes.** A click, Space or `set_ui` `checkState` asks the host; with Shift the rows from
  the last changed box to this one (in shown order, at most 1,000, counted from the clicked row so
  it is always among them) get the same state. The host
  shows the change at once and sends one `checked` event with `row_id` and `row_ids` (the rows
  that changed); when the event cannot be delivered, the boxes go back.
- **Selection.** `toggle_selection` uses Qt's multi-selection mode; Shift+click adds the range
  from the last clicked row (remembered by id, so a re-sort keeps it); a Shift+click on a check
  box sets a check range instead. A right-click or Shift+click makes the row current before the
  selection changes, so `selected` carries the new `current`. The selection patch ignores unknown ids, takes at most 1,000 and one
  row in a single-selection table, and never sends `selected`.
- **`activated`.** A double-click, or Enter outside an editor, on a cell that is not editable and
  not in a `check` column.
- **Reordering.** Only while the table is unsorted and unfiltered. A drop before the lower half
  of a row means after it; below the last row means the end. Dropping rows before one of
  themselves does nothing. The host never moves the rows.
- **Context menus.** Copy, Copy All and Export CSV… come first, then a separator and the plugin's
  entries. Copy takes the selected rows, or the current row, of the shown columns in shown order;
  Copy All adds a header line. Both stop at whole rows before 1 MiB of text (a warning is logged).
  Ctrl+C with no selected or current row leaves the clipboard alone. Export writes RFC 4180 CSV
  (UTF-8, CRLF) row by row through the host save dialog. In copied text and in CSV, text that a
  spreadsheet would run as a formula (`=`, `+`, `@`, or `-` not followed by a number) gets a
  leading apostrophe. The header menu (`<element>_columns`) shows and hides
  columns; the last shown column stays. Object names: `<element>_menu`, `_copy`, `_copy_all`,
  `_export`, `_column_<id>`.
- **Element menus.** Only the menu that `context_menu` names, and only if its location is
  `element` (otherwise a warning is logged once and only the host entries show). Hiding a
  plugin's `element` entries in the Plugins and Packs window removes them. `element` entries get
  no window shortcuts: they act on a clicked row. In `when`, `views`, `tab_kinds` and
  `file_patterns` never hold; `selection` and `min_lines`/`max_lines` count selected rows.
- **Kept columns.** Saved half a second after the user resized, moved, sorted (a header release)
  or showed or hid a column, never for the plugin's own changes; restored when the plugin sets
  the columns (a stored column the plugin no longer has is skipped, and never every column
  hidden, also without `keep_columns`). Stored per plugin and view in the plugin settings, at most
  64 KiB per view; each table's entry records when it was saved, and when the view's state would
  pass the limit the entries saved longest ago (tables the plugin no longer has) give way.
- **Automation.** `get_plugin_menus --location element` and `trigger_plugin_menu` take `--view-id`,
  `--element-id` and `--row-id` (native MCP and Python: `view_id`, `element_id`, `row_id`); the
  view must be open in that window, and the selection is the table's. `set_ui` with an
  `indexPath` makes a row current and `checkState` changes a check box.
- **SDKs.** The C++ SDK needs nothing new (element entries arrive through `onCommand`). The Python
  SDK gains `cell()`, the new `table_row`, `table_column` and `table` keywords, `select_rows()`,
  TypedDicts for the events and the element context, and `FakeHost` helpers (`select_rows`,
  `activate_cell`, `check_rows`, `move_rows`, `send_element_command`); its builders downgrade a
  view for a host without `rich_tables` or `element_menus`. The SDK version stays 2.0.0.

## 28. E17e as built

E17e implements section 11 (`tree` and `file_tree`), node tooltips, element menus on both, and
reordering inside a tree. Where the design left a choice open, E17e decided as follows.

- **Feature versions.** `tree` and `file_tree` are version 1. An older host skips an element type
  it does not know (with a warning), so a plugin that needs one checks `supports("tree")` or
  requires the feature (section 4.3); the SDK does not turn a tree into another element.
- **Where the code is.** `src/ui/src/plugintree.cpp` (the node model and the tree behaviour:
  selection, check boxes, lazy children, events, reordering, menus, Copy and Export, the
  selection patch), `src/ui/src/pluginfiletree.cpp` (the roots proxy over the native file model
  and the file tree behaviour), `src/ui/src/pluginviewtrees.cpp` (the view's part: building,
  properties, patches, tooltips) and `sanitizeTreeChildren` in `src/plugins/src/pluginfeatures.cpp`.

### 28.1 `tree`

- **Element keys.** `columns` (as table columns: a string id or `{id, title, width, align, hidden,
  min_width, editable, tooltip, tooltip_provider, tooltip_key, icon}`; without columns the tree has
  one column and no header, shown also when an earlier first column was `hidden`), `nodes`, `selection` (`none` by default, `single`, `multi`),
  `toggle_selection` (with `multi`), `checkboxes`, `auto_tristate`, `lazy`, `expand_events`,
  `header` (default true when columns are given), `context_menu`, `copy` (default true), `export`
  (default false) and `reorderable` (default false), besides the common keys.
- **Node keys.** `id` (unique in the tree), `text` (the first column; ignored when `cells` is
  given), `cells` (strings or cell objects `{text, icon, tooltip, tooltip_key, tone, bold}`),
  `icon` (in the first column), `tooltip`, `tooltip_key`, `tone`, `children`, `has_children`
  (with `lazy`), `expanded`, `checkable` (default true in a tree with `checkboxes`; without
  `checkboxes` no node has a box), `checked` (`true`, `false` or `null` for partly), `editors`
  (`{column_id: editor}`, as table rows; the column must be `editable`) and `drag` (accepted and
  ignored until E17g). A node without an id, or with the id of an earlier node, is skipped with a
  warning. Nodes keep the cells of the columns there are: after the columns change, nodes sent
  again are parsed again, also when they are the same as before.
- **Limits.** 50,000 nodes per tree and depth 64: an operation that would pass 50,000 nodes changes
  nothing and logs a warning (it is counted before anything changes; in `update_nodes`, children
  given to a node that an earlier entry of the same operation sets or removes count in full);
  nodes below depth 64 are dropped with a warning. Ids and texts are at most 4,096 characters, as
  in tables.
- **Lazy children.** In a tree with `lazy`, expanding a node with `has_children` and no children
  shows a "Loading…" child and sends `tree/children` `{view_id, window_id, element_id, node_id,
  context, timeout_ms}` (default 10 seconds, at most 30). The answer `{"nodes": [...]}` (a list
  alone is accepted; `null` or no nodes means no children) fills the node; it may hold nested
  `children`, 1,000 nodes in all, else it is invalid. A failed, invalid or timed-out query shows
  "Could not load" and the next expand asks again. At most 4 `tree/children` queries per view are
  outstanding and 16 wait; a node beyond that shows "Could not load" at once. Collapsing keeps the
  loaded children; collapsing a node that is still loading cancels its query (`query/cancel`; a
  late answer is dropped) and the next expand asks again. A patch that sets the node's children (`replace_nodes` with that `parent`, or
  `update_nodes` with `children`) ends its query. A stopped plugin never answers: its view is
  disabled and its loading nodes show "Could not load".
- **Patches.** One entry may hold, applied in this order: `replace_nodes` (the top-level nodes, or
  the children of `parent`), `update_nodes` (`[{id, ...}]`: only the keys given change; `children`
  replaces the node's children), `insert_nodes` (under `parent`, before `before`; at the end
  without `before`), `remove_nodes` (ids; descendants go too), `expand` and `collapse` (ids;
  expanding a lazy node loads it), and the selection patch `select`, `current`, `scroll_to`, which
  expands the ancestors of `current` and `scroll_to` and never sends `selected` (`current` moves the
  keyboard focus also in a tree with `selection` `none`, where Enter, Space and Copy act on it). `parent` applies to
  `replace_nodes` and `insert_nodes` of the entry. A root (`view/update`) with the same shape
  replaces the nodes only when they differ from the ones it last set; expanded nodes and the
  selection are kept by id across a replacement.
- **Selection.** As tables: `single`, `multi` with Shift and Ctrl, and `toggle_selection`.
- **Events.** `selected` (the selected ids in tree order, at most 1,000, `selected_count`,
  `truncated`, and `current`, the node with the keyboard focus or null), `activated` (`{node_id,
  column_id}`: a double-click or Enter on a cell that is not editable; a double-click on a node's
  check box checks it and does not activate, and Space checks the current node while Enter
  activates it),
  `checked` (`{node_id, checked, node_ids}`: `node_ids` are the nodes set to `checked`, the clicked
  one first, with `auto_tristate` its loaded descendants too, at most 1,000; the parents' derived
  states are not listed), `edited` (the value, with `node_id`, `column_id` and `previous`, as table
  edits), `moved` (`{node_ids, parent, before}`, with null for the top level and for the end) and,
  with `expand_events`, `expanded` and `collapsed` (`{node_id}`). Every key is sent to every plugin:
  principle 11 is about elements that existed before E17.
- **Check boxes.** A click, Space or `set_ui` `checkState` changes the box at once and sends
  `checked`; when the event cannot be delivered, the boxes go back. With `auto_tristate` a parent
  shows checked when all its checkable children are, unchecked when none is and partly otherwise,
  and checking a parent sets every loaded descendant; nodes loaded or inserted below a checked or
  unchecked parent without their own `checked` take its state. Without it every box is independent
  and a `checked: null` node shows partly checked.
- **Reordering.** With `reorderable`, nodes can be dragged inside the tree: a drop on the upper
  quarter of a node means before it, on the lower quarter after it, and in between into it as its
  last child. Dropping a node into itself or one of its descendants does nothing. The host does
  not move the nodes; the plugin confirms with a patch.
- **Context menus.** Copy (the selected nodes, or the current one, as tab-separated text of the
  shown columns), Copy All (a header line and every loaded node in tree order, the first column
  indented by two spaces per level) and, with `export`, Export CSV… (RFC 4180 as for tables, with a
  leading `level` column), then the plugin's `element` entries. As in tables, Copy and Copy All
  stop at whole lines before 1 MiB of text and copied text is formula-safe. Object names:
  `<element>_menu`, `_copy`, `_copy_all`, `_export`.
- **Tooltips.** A node's `tooltip` (else its text) is the static tooltip of its cells; a cell
  object's tooltip wins. With `tooltip_provider` on the tree, `tooltip/resolve` carries `node_id`,
  `column_id` and the most specific `tooltip_key` (the cell's, the node's, the column's, the
  tree's); patches that change or remove a node drop its cached answers.

### 28.2 `file_tree`

- **Element keys.** `roots` (absolute paths, at most 16; relative, repeated and nested roots are
  skipped with a warning), `filters` (name globs such as `*.bin`), `hide_filtered` (default true;
  false greys out files that do not match), `show_files` (default true), `show_hidden` (default
  false), `columns` (`name`, `size`, `type`, `modified` in the order given; `name` always comes
  first; default `name`, `size`, `modified`), `selection` (`single` by default, `none`, `multi`),
  `toggle_selection`, `select` (`files`, `dirs` or `both`, the default: what can be selected),
  `checkboxes` (a pick list), `value`, `context_menu` and `copy` (default true).
- **Roots.** Each root is a top-level item that shows the root's path. A root that does not exist
  shows greyed, without children; a local one shows its contents by itself once it appears (also
  when it comes back after it was deleted): the nearest existing folder above it is watched, never
  polled. A network root (`//server/share`) is not touched until the user
  expands it or a patch expands or reveals a path in it, so an unreachable server never stalls the
  window on its own. Once it could not be reached, it closes again and keeps its expander: only the
  user's expansion tries again. A symbolic link or junction below a root that leads out of the root
  (or nowhere) shows disabled: it is never opened, selected or checked, and paths through it are
  outside the roots. A link that stays inside its root behaves as its target. A link in a network
  root is never resolved (a share that went away could stall the window): it shows disabled. A
  local root that is itself a link is what it leads to now: when it leads elsewhere, the links below
  it are judged against the new place, the view starts over and the checked paths are looked at
  again. When it leads nowhere (its target was deleted), it is a root that does not exist: it shows
  greyed, what was checked in it is dropped (the plugin hears `selected`), and it shows its contents
  by itself once its target is back (the nearest existing folder above the target is watched too;
  the link is read, never followed).
- **The native model.** Folders load lazily in the background and changes on disk appear by
  themselves. Icons are the style's folder and file icons and the type column is "Folder" or the
  file's suffix: the host never reads file contents, not even to find an icon, and never resolves
  shortcuts.
- **`value`.** The selected paths (with `checkboxes`, the checked ones) with forward slashes, in
  `inputs` and in `selected` events (at most 1,000, with `selected_count` and `truncated`). A patch
  `set: {value: [...]}` selects (or checks) those paths and reveals the first; paths outside the
  roots, missing ones and those that cannot be selected (or checked) there, such as a folder with
  `select: "files"` or a greyed file, are ignored. A patch entry `expand: [paths]` opens folders.
  New `roots` and options that make a checked path ineligible (`select`, `filters`, hidden files)
  drop it without an event. A checked file deleted or renamed on disk, or reached through a link
  that now leads out of its root, is dropped too, and the plugin gets `selected` with the paths
  still checked.
- **Events.** `selected` (value: the paths) and `activated` (`{path}`: a double-click or Enter on
  a file; on a folder it expands). With `checkboxes`, checking sends `selected` with the checked
  paths and the row highlight only marks the current item.
- **Context menus and tooltips.** Copy Path (the selected paths, one per line, whole paths up to
  1 MiB of text; no path is built past that) then the plugin's `element` entries, whose context
  has `node_id` (the clicked path) and `selected_ids` (the selected paths). An item's static
  tooltip is its path; with `tooltip_provider`, `tooltip/resolve` carries the path as `node_id`.
  Tooltips and menus, also by `--node-id`, name only a root or an existing item of a root that
  was reached, its links resolved: never a missing path or one through a link that leads out of
  its root (a right-click on such a link gives a null `node_id`). In a network root they name only
  items already loaded, found without touching the network.

### 28.3 Automation and SDKs

- `get_plugin_tooltip`, `get_plugin_menus --location element` and `trigger_plugin_menu` take
  `--node-id` for a node or a path (native MCP and Python: `node_id`); `--node-id` and `--row-id`
  exclude each other. Element menu entries of trees get `node_id` instead of `row_id` and
  `column_id`, in `ui/command`, `menu/populate` and in the `menu` context of the action an entry
  runs. `get_ui` and `set_ui` reach nodes with a multi-level `indexPath`.
- The Python SDK gains `tree()`, `tree_node()`, `file_tree()`, the patch helpers `replace_nodes()`,
  `update_nodes()`, `insert_nodes()`, `remove_nodes()`, `expand_nodes()`, `collapse_nodes()` and
  `select_nodes()`, `@plugin.tree_children(view, element)` with `TreeChildrenQuery`, `node_id` in
  menu and command contexts, TypedDicts for the elements, events and answers, and `FakeHost`
  helpers. The C++ SDK needs nothing new: `onQuery("tree/children", ...)` answers the query.

## 29. E17f as built

E17f implements section 12: layout containers and size hints, `text_view`, `image`, `sparkline`
and `chart`, the new button keys, the busy overlay, `modified` with a close confirmation, dialog
buttons with a result, and focus patches. Where the design left a choice open, E17f decided as
follows.

- **Feature versions.** `layout`, `text_view`, `images`, `charts` and `view_dialogs` are version 1.
  `layout` covers the containers of section 12.1 and the size hints; `images` the `image` element;
  `charts` `sparkline` and `chart`; `view_dialogs` the button keys, the busy overlay, `modified`,
  dialog buttons and focus patches.
- **Where the code is.** `src/ui/src/pluginlayout.cpp` (the busy overlay and the container
  helpers), `src/ui/src/plugintextview.cpp`, `src/ui/src/pluginchart.cpp` (the image, sparkline and
  chart widgets), `src/ui/src/pluginviewlayout.cpp` (the view's part: building, properties,
  patches, buttons, focus) and `src/ui/src/pluginviewhost.cpp` (dialog buttons, the title mark and
  the close confirmation, `result` in `view/closed`).
- **Old plugins (principle 11).** Every key, patch and event here is new, so a plugin that uses
  none of them gets the same frames as before; `view/closed` gains `result` and `inputs` only for
  a dialog that declares `buttons`.

### 29.1 Layout

- **Containers.** Every container (`column`, `row`, `group` and the new ones) holds its children in
  an inner widget, so the busy overlay can cover them; object names are unchanged.
- **`tabs`.** Children are `tab` elements; any other child is skipped with a warning, and so is a
  `tab` outside `tabs`. `current` is a tab id (a number is an index), `position` is `top` (the
  default), `bottom`, `left` or `right`. When the user changes the tab (also through `set_ui`
  `currentIndex` on the `tabs` element), the plugin gets `changed` with `value` the tab's id (null
  for a tab without an id) and `index`. A `current` set by the plugin sends nothing.
- **`tab`.** `title`, `icon`, `badge` (at most 64 characters, shown after the title in
  parentheses) and `children`, laid out as a column.
- **`splitter`.** `orientation` is `horizontal` (children side by side, the default) or
  `vertical`; each child is a pane. `sizes` are pixels per pane. When the user drags a handle, the
  sizes are kept per plugin, view and element (half a second later, in the store of section 27's
  kept columns) and win over the plugin's `sizes` when the view is built again with the same number
  of panes; a later `set: {sizes}` of the plugin applies.
- **`form`.** Two aligned columns. A field's caption is its `label` (the field then shows no
  caption of its own); any other element inside a form may set `label` for its caption, except a
  button, whose `label` is its text. An element without a caption spans both columns. The caption
  cell is part of its row: it follows the element's `visible` and `enabled`, and shows `&` as
  written.
- **`grid`.** `columns` (1 to 12, default 2); children fill the rows from left to right, each
  taking `span` columns (at most `columns`) and `row_span` rows (at most 64).
- **`separator`.** `orientation` `horizontal` or `vertical`; the default is vertical inside a
  `row` and horizontal elsewhere.
- **`spacer`.** `size` (0 to 2,000 pixels along the parent's direction: a width in a row, a height
  elsewhere) or `stretch`; with neither it stretches (factor 1).
- **Size hints.** Every element takes `stretch` (0 to 100: its share of the free space in a
  `column`, `row`, `tab`, `group` or `splitter`), `min_width`, `max_width`, `min_height`,
  `max_height` (0 to 10,000 pixels; a maximum of 0 means none) and `align` (`left`, `center`,
  `right`, `top`, `bottom`, or `fill`, the default). A row whose children include one with
  `stretch`, or a spacer without a size, adds no stretch of its own at its end. A value out of
  range is clamped; a value of the
  wrong type is ignored with a warning.
- **Same-shaped roots.** The new containers are part of a view's shape, as `column` and `row` are:
  a root that changes their children rebuilds the view; one with the same shape applies in place.
  So are whether a form row has a caption cell, a grid's `columns` and its cells' `span` and
  `row_span`: patches cannot change them (a warning is logged). A chart's or sparkline's `kind`
  and a button's `style` are looks that patches change in place; only a field's `kind` and
  `style` stay structural, as before.

### 29.2 Content

- **`text_view`.** `text`, `format` (`plain`, the default, or `markdown`), `monospace` (default
  false), `wrap` (default true), `follow` (default false: scroll to the end after new text and
  when turned on; a root that sends it again leaves the reader's place) and
  `max_lines` (0, the default, is no limit; at most 100,000; plain text only: the oldest lines go).
  The whole text is at most 1 MiB (1,048,576 characters): when more arrives, the oldest lines go.
  The patch entry `{"id", "append": "..."}` adds text at the end (at most 65,536 characters; a
  longer append is refused with a warning). A same-shaped root replaces the text only when it
  differs from the text the plugin last set, so appended text stays; `set: {text}` always replaces
  it. Appended Markdown is rendered once per turn of the event loop, however many appends arrive
  in it. Ctrl+F opens a find bar (`<element>_find`): the text claims the key while it has the
  focus, so CILogg's own Find shortcut neither fires nor makes it ambiguous; Enter finds the next
  match, Shift+Enter the previous one, Esc closes it. `get_ui` reads the text as `plainText`
  (read-only).
- **Markdown** is CommonMark through Qt's reader with HTML off (`MarkdownNoHTML`). An image shows
  only when its source is an icon reference (section 6); other sources show nothing and are never
  fetched. Links are never opened by the host: a click sends `link` with `value` `{href}`. On Qt
  older than 5.14 the text shows as plain text.
- **`image`.** `source` (an icon reference: a plugin file, `builtin:`, `status:`, `image:<id>` of a
  registered picture, or the light/dark form), `width` and `height` (1 to 4,096 pixels), `scale`
  (`fit`, the default: inside the box with its aspect ratio; `fill`: stretched to the box; `none`:
  its own size, cut by the box) and `alt` (its accessible name, also shown when the source cannot be
  shown). Without `width` and `height` the image takes its own size (at most 4,096 pixels): a
  registered picture's, the largest size of a plugin file or built-in icon, and 256 pixels for a
  scalable (SVG) one. When the plugin's registered images change, every `image` element shows the
  new picture.
- **`sparkline`.** `values` (numbers; at most 1,024, the last ones kept; `null` is a gap), `min` and
  `max` (default: the range of the values), `kind` (`line`, the default, or `bar`) and `tone`. Its
  default size is 120 by 24 pixels; its accessible description names the count and the last value.
- **`chart`.** `kind` (`line`, the default, `bar`, `scatter` or `area`), `series` (at most 8:
  `{id, title, points, tone}`, each with an id of its own, else skipped with a warning; `points`
  are `[x, y]` pairs, a number alone being `y` at its position in the list, which in an
  `append_points` counts on from one after the series' last whole `x`; anything else is skipped with
  a warning; at most 10,000 per series, the last ones kept), `x_title`, `y_title`, `x_kind`
  (`number`, the default, or `time`: `x` in milliseconds since the epoch, shown as local time),
  `legend` (default: shown with more than one series) and `window` (1 to 10,000: each series keeps
  its last points). Patch entries: `append_points` (`{series, points}` or a list of them; an
  unknown series is skipped with a warning) and `clear` (`true` for every series, or a list of
  series ids). A series without `tone` takes the next colour of a fixed palette. Drawn by a host
  widget, without Qt Charts; its minimum height is 160 pixels.
- **Streaming content and roots.** As for `text_view`, a same-shaped root replaces a chart's
  `series` or a sparkline's `values` only when they differ from what the plugin last set, so
  appended points stay.

### 29.3 Buttons, busy overlay, unsaved changes, dialogs and focus

- **Buttons.** `icon` (an icon reference), `style` (`push`, the default; `tool`: flat; `link`:
  flat with link-coloured, underlined text), `menu` (the id of an `element` menu: the button opens
  it as a drop-down, rebuilt each time it opens, and does nothing else when clicked; its entries
  get the view and the button as the element, without a row or node), `checkable` and `checked`
  (a checkable button sends `changed` with the new state instead of `clicked` or its action;
  `set: {checked}` sends nothing, and a same-shaped root that sends the `checked` the plugin last
  set keeps the user's toggle), `shortcut` (a key sequence that presses the button while the
  focus is in the view; refused with a warning when it is a reserved key of section 7.6, a
  shortcut of the CILogg window, or the shortcut another button of the view has at that moment,
  in element order, so two buttons swap keys in two patches) and `default`
  (Enter in the view presses it, unless the focused control uses Enter itself: multi-line text,
  tables, trees, lists, buttons and open drop-downs).
- **Busy overlay.** `busy`, `busy_text` and `busy_cancel` apply to the root and to every container
  (`column`, `row`, `group`, `tabs`, `tab`, `splitter`, `form`, `grid`). While `busy` is true the
  container's contents are disabled (the keyboard focus leaves them) and covered by a busy
  indicator with the text (`<element>_busy`); `busy_cancel` adds Cancel (`<element>_busy_cancel`),
  which sends `cancel` (value null) and then shows "Cancelling…", disabled, until `busy` is set
  again (turned on, or set by a patch; a same-shaped root that still says busy leaves it). The
  container's own `enabled` is unaffected.
- **Unsaved changes.** `modified` and `close_confirm` are keys of the root element (elsewhere they
  are unknown properties). `modified: true` adds " *" to the title of the dock, window or dialog;
  the user closing it (the title bar's button, Esc, or a dialog's `cancel`, `no` or `close`) then
  asks "Discard changes?" with the `close_confirm` text (default "This view has unsaved changes.
  Discard them?"), Discard and Cancel (object name `pluginView_<plugin>_<view>_discard`). Closing
  by the plugin (`view/hide`), by the plugin stopping or with the CILogg window does not ask; the
  `ok` and `yes` buttons never ask.
- **Dialog buttons.** A view of `kind: dialog` may declare `buttons` (from `ok`, `cancel`, `apply`,
  `close`, `yes`, `no`, each once), `default` (one of them) and `confirm_by_plugin` in
  `plugin.json`; other kinds refuse them as a manifest error, and an older CILogg ignores them. The
  buttons sit in a button box below the view (`pluginView_<plugin>_<view>_button_<name>`). A click
  sends `view/event` `clicked` with `element_id` `"#buttons"` (a reserved id, never an element) and
  `value` the button's name, with the inputs. `ok`, `yes`, `cancel`, `no` and `close` then close the
  dialog; with `confirm_by_plugin`, `ok` and `yes` only send the event and the plugin closes the
  dialog with `view/hide`. Esc and the title bar's close button press `cancel` when the dialog has
  it, else `close`, else just close the dialog. The `view/closed` of a dialog with `buttons`
  carries `result` (the button that closed it; null when it closed otherwise) and `inputs` (the
  view's inputs at that moment). The buttons are disabled while the view is (before the plugin's
  first root and while it is stopped). `default` is the button Enter presses when no element button
  of the view is the default; without `default`, Enter presses none of the dialog buttons.
- **Focus.** The patch entry `{"id", "focus": true}` gives the element the keyboard focus only while
  the view is active: its own window (a window, a dialog or a floating dock) is the active window,
  or the focus already is inside the view. Otherwise it is ignored.

### 29.4 Automation and SDKs

- `get_ui` reports the `tabs` element's `currentIndex` (and `set_ui` changes it, sending
  `changed`), a `text_view`'s `plainText`, and a checkable button's `checked` (`set_ui` toggles it,
  sending `changed`); dialog buttons are buttons for `activate_ui`. `get_plugin_menus --location
  element` and `trigger_plugin_menu` take `--element-id` of a button with `menu` (without
  `--row-id` or `--node-id`).
- The Python SDK gains `tabs()`, `tab()`, `splitter()`, `form()`, `grid()`, `separator()`,
  `spacer()`, `text_view()`, `image()`, `sparkline()`, `chart()` and `chart_series()`, the button
  keywords, the patch helpers `append_text()`, `append_points()`, `clear_chart()` and
  `focus_element()`, TypedDicts for the elements, events and `view/closed`, and `FakeHost` helpers.
  Its `update()` and `patch()` convert for an older host: without `layout`, `tabs` become a
  `column` of `group`s titled like the tabs, `splitter`, `form` and `grid` a `column` of `row`s, and
  `separator` and `spacer` are left out, with the size hints removed (a form caption, on any
  element but a field or a button, becomes a label of its own, shown and enabled as its element
  is in the root; later `label` patches of that element are left out, and `visible` or `enabled`
  patches reach only the element); without `view_dialogs` the
  button keys, the busy keys, `modified`, `close_confirm` and focus patches are removed; `text_view`,
  `image`, `sparkline` and `chart` go as they are (an older host skips them with a warning), and
  their `append`, `append_points` and `clear` patches are left out. The builders raise
  `ValueError` for what the host would refuse or cut, such as more than 1,024 sparkline values,
  10,000 points in a series or 1 MiB of text, so a plugin learns of it where it builds the view;
  the host's own trimming covers the dicts a plugin writes itself. The C++ SDK needs nothing new.
