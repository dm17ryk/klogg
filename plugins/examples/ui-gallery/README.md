# UI Gallery

A small tour of the plugin view, menu, form and table features of the advanced plugin SDK (see
`docs/plugin-sdk.md` and `docs/plugin-sdk-advanced.md`). It grows with each E17 slice. It is
disabled by default; enable it in **Plugins → Manage Plugins…**, then open **Plugins → UI
Gallery → Open gallery** (or press **Ctrl+Alt+Shift+G**).

## What it shows

### Tooltips, icons and accessible names (E17a)

- **Static tooltips** on a label, a field, a button and the table column headers
  (`table_column(..., tooltip=...)`), and the existing row tooltip.
- **Advanced tooltips** that CILogg asks the plugin for when the pointer rests on an element
  with `tooltip_provider=True`:
  - the **Fleet summary** button: a title, text, name/value rows, a tone, a footer and a
    registered icon (`@plugin.tooltip("gallery", "summary")`);
  - every cell of the **Boards** table: the query carries `row_id` and `column_id`
    (`@plugin.tooltip("gallery", "boards")`); each row has a `tooltip_key` that changes with its
    data, so CILogg never shows a stale cached tooltip;
  - the status label, answered by the view-wide handler (`@plugin.tooltip("gallery")`) with a
    plain string.
- **A registered icon:** an SVG chip registered with `register_icons` and `svg_icon(...,
  mask=True)` when the view opens, used in tooltips as `image_ref("chip")`.
- **Invalidation:** **Change data** changes the boards, patches the rows and the status label
  (a patch drops their cached tooltips) and calls `plugin.invalidate_tooltips("gallery",
  "summary")` for the summary button, which it does not patch.
- **Accessible names:** `accessible_name` and `accessible_description` on the summary button,
  the field and the table.

### Menus, context menus, commands and a shortcut (E17b)

`plugin.json` declares three `menus` for these (and the table's context menu, see E17d):

- **Plugins → UI Gallery** (`location: "plugins"`, which replaces the plugin's flat entries in
  the Plugins menu):
  - **Open gallery**, a `view` entry that CILogg runs itself, with the shortcut
    **Ctrl+Alt+Shift+G** (rebind it in **Options → Shortcuts**);
  - **Highlight offline boards**, a checkable `event` entry: the plugin gets `ui/command` with
    `checked` and mutes the other rows of the table
    (`@plugin.on_command("main", "highlight_offline")`);
  - **Sort by name** and **Sort by state**, radio entries (`group: "sort"`) under a section
    title; the table is sorted again (`@plugin.on_command("main", "sort_state")`);
  - **Boards**, a `dynamic` submenu that CILogg fills each time it opens by asking
    `menu/populate`; `@plugin.menu_populate("main", "boards")` answers one entry per board with
    a status icon and `cache_ms: 2000`. Picking a board is a `ui/command` that the menu-wide
    handler (`@plugin.on_command("main")`) shows in the status label. **Change data** renames
    the entry to "Boards (n offline)" with `plugin.update_menu("main", [menu_set("boards",
    title=...)])`.
- **Count selected lines** in the context menu of the log views (`location: "log"`, `when:
  {"selection": "lines"}`): the command carries the selected line ranges and their count
  (`command.ranges`, `command.line_count`).
- **Describe tab** in the tab context menu (`location: "tab"`): the command's context names the
  tab (`tab_id`), its kind (`tab_kind`) and its file (`file_path`) or port (`port_name`).

The commands put their result in the gallery's status label. The view need not be open: the
plugin keeps the last result and shows it when the gallery opens. Command handlers run on the
same worker thread as the view callbacks, so they share the gallery state with them.

### Choices, check boxes, radio buttons, field kinds and validation (E17c)

The **Choices and fields** group has:

- **Speed**, a drop-down whose choices have icons and a tooltip, a separator, a section title
  ("Fixed speeds") and a disabled choice (`choice(...)`, `choice_separator()`,
  `choice_section(...)`);
- **Parity**, a drop-down with radio marks (`indicator="radio"`);
- **Direction**, an inline row of radio buttons (`style="radio"`,
  `orientation="horizontal"`), and **Channels**, a `multichoice` shown as check boxes in two
  columns (`style="checkboxes"`, `columns=2`);
- **Firmware**, a searchable drop-down of 30 versions (`searchable=True`);
- **Target board**, an editable drop-down with completion (`editable=True`,
  `completion=True`): when typing pauses, CILogg asks `field/complete` and
  `@plugin.complete("gallery", "target_board")` answers the board names that start with the
  typed text, with their state and a status icon;
- a tri-state **Verbose logging** check box that starts partly checked (`checkbox(...,
  value=None, tristate=True)`), a **Mode** `radio_group` and a **Logs** `check_group` with
  All and None links (`all_none=True`);
- one field of each new kind: `multiline` (**Notes**, 3 rows, monospace), `secret`, `slider`
  (**Log level**), `color`, `date`, `time` and `datetime`, and the `integer` **Retries**
  (0 to 10);
- **Board label**, a text field with `required=True` and `pattern="[A-Za-z0-9_-]+"`, and
  **Apply**, an event button with `requires_valid=["board_label", "retries"]`: CILogg keeps it
  disabled until both fields are valid.

The label next to **Apply** shows the last `changed` event as `changed <element>=<value>
valid=<true|false>` (text as typed, other values as JSON; an event without `valid`, such as a
check box's, counts as valid) or the click as `clicked apply board_label=<value>
retries=<value>`.

### Rich table and its context menu (E17d)

The **Boards** table of the "Advanced tooltips" group uses the rich table keys:

- **Cell objects** (`cell(...)`): the state cell has a status icon, the uptime cell a bar that
  compares the uptime with the longest one (`progress`), a static cell tooltip and, at 0
  minutes, the `muted` tone, which wins over the row's tone. With **Highlight offline boards**
  the names of offline boards are bold.
- **A check column:** the Board column has `check=True`, so each name has a check box bound to
  the row's `checked`. The column also has a header icon and a minimum width; the Uptime
  column is right-aligned.
- **Table keys:** `selection="multi"`, `export=True` (Export CSV... next to Copy and Copy All
  in the context menu), `reorderable=True` (drag rows) and `context_menu="board_actions"`.
- **Events:** the plugin shows each table event in the label under the table
  (`table_status`):
  - `activated <row> <column>` after a double-click or Enter on a cell;
  - `checked <rows> <true|false>` after a check box changed (the rows joined by `,`); the
    plugin keeps the state, so a new window shows it;
  - `moved <rows> before <row|end>` after rows were dragged: CILogg does not move them, so the
    plugin replaces the rows in the new order and selects the moved rows again
    (`select_rows`). The dragged order stays until **Sort by name** or **Sort by state**.
- **Context menu** (`plugin.json` menu `board_actions`, `location: "element"`): **Restart
  Board** and **Remove from List** (shown with a selection, `when: {"selection": "any"}`) act
  on the selected rows, and **Restore Removed Boards** puts removed boards back in their place.
  The label shows each command as `menu <entry> <clicked row|none> <selected count>`.

`plugin.json` lists the host-to-plugin methods the plugin handles in `handles`
(`tooltip/resolve`, `menu/populate`, `field/complete`, `ui/command`, `query/cancel`); without
them CILogg never sends them (an `event` entry is then disabled), and the SDK warns on stderr
at start.

## On an older CILogg

The gallery still loads: the previous CILogg ignores `handles` and `menus` and shows its flat
**Plugins → UI Gallery** view entry. The SDK leaves out what the host does not know:
`tooltip_provider`, `tooltip_key` and column tooltips without the `tooltips` feature,
accessible names without `views` version 2; `register_icons`, `invalidate_tooltips` and
`update_menu` send nothing. The intro label then says that advanced tooltips need a newer
CILogg.

The choices group becomes plain fields: without `choices`, separators and section titles are
left out and choices lose their icons, tooltips and `enabled`, and the drop-down styles and
completion go; without `toggles`, the check box is a boolean field (partly checked becomes
unchecked), the radio group a choice field and the check group a multichoice field; without
`field_kinds`, the new kinds are text fields (integer and slider: number fields); without
`validation`, `required`, `pattern`, the bounds and `requires_valid` are left out. The group's
first label then says that these controls need a newer CILogg.

Without `rich_tables`, the table's cells are plain text and the check column, icons,
alignment, row dragging, Export CSV and the selection patch are left out; without
`element_menus`, the table has no plugin context menu. The label under the table then says
that these need a newer CILogg.

## Testing without CILogg

```python
from cilogg_plugin_testing import FakeHost, log_selection

with FakeHost.start_package("plugins/examples/ui-gallery") as host:  # with cilogg_plugin.py copied in
    host.initialize()
    host.open_view("gallery")
    host.wait_for("view/update")
    print(host.resolve_tooltip("gallery", "boards", row_id="b2", column_id="state"))
    print(host.populate_menu("main", "boards"))                     # the dynamic submenu
    host.send_command("lines", "count_lines", "log", selection=log_selection([(5, 9)]))
    print(host.wait_for("view/update", lambda params: "patch" in params))  # the status label
    print(host.complete_field("gallery", "target_board", "Bo"))      # completion
    host.change_field("gallery", "board_label", "bad name", valid=False)
    print(host.wait_for("view/update", lambda params: "patch" in params))  # "changed board_label=..."
    host.move_rows("gallery", "boards", ["b3"], before="b1")         # a dragged row
    print(host.wait_for("view/update", lambda params: "patch" in params))  # rows, selection, "moved b3 before b1"
    host.send_element_command("board_actions", "restart", "gallery", "boards", row_id="b3",
                              selected_ids=["b3"])                   # the table's context menu
```

`tests/plugins/test_ui_gallery.py` does this for every tooltip, menu entry, command, control,
completion and table event, on a current, an E17c, an E17b, an E17a and an older host.
