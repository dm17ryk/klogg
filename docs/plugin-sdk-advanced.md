# Advanced plugin SDK (E17): menus, rich elements, tooltips with handlers and window integration — proposed design

**Status:** proposed on 2026-11-19 for the user's approval (decision D17 in
[docs/workbench/decisions.md](workbench/decisions.md), task
[E17](../plans/cilogg-workbench-2026-10-21/tasks/E17.md)). Design only: nothing here is implemented.
Section 23 lists the open questions, each with a recommended answer. Implementation starts after
approval, one branch and pull request per slice (section 21).

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
  seconds) lets the host reuse it.
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
