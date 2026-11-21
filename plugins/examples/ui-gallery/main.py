"""UI gallery (README.md): static and advanced tooltips, a registered icon and accessible names
(E17a); a Plugins submenu with a check box, radio items, a dynamic submenu and a shortcut, and
entries in the log and tab context menus (E17b); choices with icons, separators and styles,
check boxes, radio buttons, the new field kinds, validation and completion (E17c); a rich table
with cell objects, a check column, row dragging, Export CSV and a context menu (E17d); and the
explorer view: a lazy tree with check boxes, dragging and a context menu, and a file tree of
the gallery's own folder (E17e)."""
from __future__ import annotations

import json
import os
import threading
from typing import Dict, List, Optional, Set, Tuple

from cilogg_plugin import (
    CompleteQuery, CompletionItem, JsonObject, MenuPopulateResult, MenuQuery, Plugin, Tooltip,
    TooltipQuery, TreeChildrenQuery, TreeNode, UiCommand, builtin_ref, button, cell,
    check_group, checkbox, choice, choice_section, choice_separator, collapse_nodes, column,
    completion_item, field, file_tree, group, image_ref, label, menu_item, menu_set,
    radio_group, replace_nodes, replace_rows, row, select_nodes, select_rows, set_props,
    status_ref, svg_icon, table, table_column, table_row, tooltip_content, tree, tree_node,
    update_nodes, update_rows,
)

plugin = Plugin()
VIEW = "gallery"
MENU = "main"  # The plugin's submenu of the Plugins menu (plugin.json "menus").
BOARD_MENU = "board_actions"  # The context menu of the boards table (location "element").
TABLE = "boards"
CHIP_ICON = "chip"
# A monochrome chip (mask: the host recolours it with the theme's text colour).
CHIP_SVG = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">'
            '<rect x="4" y="4" width="8" height="8" rx="1" fill="#000"/>'
            '<path d="M2 6h2M2 10h2M12 6h2M12 10h2M6 2v2M10 2v2M6 12v2M10 12v2" stroke="#000"/>'
            '</svg>')
STATE_TONES = {"connected": "ok", "idle": "warn", "offline": "error"}
NEXT_STATE = {"connected": "idle", "idle": "offline", "offline": "connected"}
STATE_ORDER = {"offline": 0, "idle": 1, "connected": 2}  # Sort by state: offline boards first.

# Shared state. View callbacks and menu commands run on one worker thread, query handlers on
# their own lanes, so the state is read and changed under state_lock.
state_lock = threading.Lock()
revision = 0
icons_registered = False
highlight_offline = False
sort_key = "name"  # name, state, or manual (the rows were dragged into board_order)
board_order: List[str] = ["b1", "b2", "b3"]  # Every known board, in the dragged order.
last_command: Optional[str] = None  # What the last menu command did, for the status label.
# The last `changed` event or Apply click of the "Choices and fields" group.
last_control_event = "Change a control: its event shows here."
# The last table event or board menu command, for the table status label.
last_table_event: Optional[str] = None
open_windows: Set[str] = set()
boards: Dict[str, Dict[str, object]] = {
    "b1": {"name": "Board 1", "state": "connected", "uptime": 12, "firmware": "3.2.1", "resets": 0,
           "checked": False},
    "b2": {"name": "Board 2", "state": "idle", "uptime": 340, "firmware": "3.2.0", "resets": 2,
           "checked": False},
    "b3": {"name": "Board 3", "state": "offline", "uptime": 0, "firmware": "3.1.9", "resets": 7,
           "checked": False},
}
removed_boards: Dict[str, Dict[str, object]] = {}  # "Remove from List", until "Restore".

# The explorer view (E17e): a tree of benches, their boards (loaded when a bench expands) and
# the boards' modules, and a file tree of the gallery's own folder. Node ids: benches
# "bench1", boards "board1", modules "board1.modem".
EXPLORER = "explorer"
TREE = "benches"
FILES = "files"
NODE_MENU = "node_actions"  # The context menu of the tree (location "element").
FILE_MENU = "file_actions"  # The context menu of the file tree.
GALLERY_DIR = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/")
BENCH_NAMES = {"bench1": "Bench 1", "bench2": "Bench 2"}
MODULE_NAMES = {"modem": "Modem", "radio": "Radio"}
CHILD_KIND = {"top": "bench", "bench": "board", "board": "module"}  # What a parent may hold.
EXPLORER_HINT = "Expand a bench, check, drag or right-click a node, or pick a file: it shows here."
explorer_boards: Dict[str, Dict[str, str]] = {
    "board1": {"name": "Board 1", "state": "connected", "firmware": "3.2.1"},
    "board2": {"name": "Board 2", "state": "idle", "firmware": "3.2.0"},
    "board3": {"name": "Board 3", "state": "offline", "firmware": "3.1.9"},
}
# The plugin's own node order: the children of every node (None: the top level). The host
# never moves nodes; the plugin changes this order and patches the tree.
explorer_children: Dict[Optional[str], List[str]] = {
    None: ["bench1", "bench2"], "bench1": ["board1", "board2"], "bench2": ["board3"],
    **{board_id: [f"{board_id}.{module}" for module in MODULE_NAMES]
       for board_id in explorer_boards},
}
loaded_benches: Set[str] = set()  # Benches whose boards the host has.
checked_nodes: Set[str] = set()
selected_files: List[str] = []  # The file tree's last selection, shown again when it opens.
last_explorer_event: Optional[str] = None  # The explorer's status label.
explorer_windows: Set[str] = set()


# The helpers below read the shared state: call them with state_lock held.
def sorted_board_ids() -> List[str]:
    if sort_key == "state":
        return sorted(boards, key=lambda board_id: (STATE_ORDER[str(boards[board_id]["state"])],
                                                    board_id))
    if sort_key == "manual":
        return [board_id for board_id in board_order if board_id in boards]
    return sorted(boards, key=lambda board_id: str(boards[board_id]["name"]))


def row_tone(state: str) -> str:
    if highlight_offline:
        return "error" if state == "offline" else "muted"
    return STATE_TONES[state]


def uptime(board: Dict[str, object]) -> int:
    return int(str(board["uptime"]))


def board_row(board_id: str) -> JsonObject:
    """A row with cell objects (on an older CILogg the SDK sends their text): the name with a
    check box (the name column is a check column), the state with a status icon, and the uptime
    with a bar that compares it with the longest uptime."""
    board = boards[board_id]
    name, state, minutes = str(board["name"]), str(board["state"]), uptime(board)
    longest = max(uptime(other) for other in boards.values())
    highlighted = highlight_offline and state == "offline"
    # tooltip_key changes with the row's data, so the host never shows a stale cached tooltip.
    return table_row(
        board_id,
        [cell(name, bold=True) if highlighted else name,
         cell(state, icon=status_ref(STATE_TONES[state])),
         cell(f"{minutes} min", progress=minutes / longest if longest else 0.0,
              tone="muted" if minutes == 0 else None,  # a cell tone wins over the row's
              tooltip="The bar compares the uptime with the longest one.")],
        tone=row_tone(state), tooltip=f"{name} (static row tooltip)",
        tooltip_key=f"{board_id}-{revision}", checked=board["checked"] is True)


def offline_count() -> int:
    return sum(1 for board in boards.values() if board["state"] == "offline")


def boards_table(rows: List[JsonObject]) -> JsonObject:
    """E17d: a check column, a header icon, a right-aligned column, row dragging, Copy and
    Export CSV, and the plugin's context menu. On an older CILogg the SDK leaves these out."""
    return table(TABLE,
                 [table_column("name", "Board", 120, tooltip="The board name", check=True,
                               min_width=80, icon=builtin_ref("device")),
                  table_column("state", "State", 100, tooltip="The connection state"),
                  table_column("uptime", "Uptime", 90, tooltip="Minutes since the last reset",
                               align="right")],
                 rows, selection="multi", export=True, reorderable=True,
                 context_menu=BOARD_MENU, tooltip_provider=True, accessible_name="Boards")


def table_note() -> str:
    if plugin.supports("rich_tables") and plugin.supports("element_menus"):
        return "Double-click, check, drag or right-click a board: the event shows here."
    return ("This CILogg shows plain table cells; check boxes, row dragging and the board menu "
            "need a newer CILogg.")


def status_text() -> str:
    text = f"Data revision {revision}"
    return text if last_command is None else f"{text}. {last_command}"


SPEEDS = [
    choice("auto", "Automatic", icon=builtin_ref("refresh"), tooltip="Detect the speed"),
    choice_separator(),
    choice_section("Fixed speeds"),
    choice(115200, "115200", icon=status_ref("ok")),
    choice(57600, "57600", icon=status_ref("ok")),
    choice(9600, "9600", icon=status_ref("muted"), tooltip="Too slow for the boards",
           enabled=False),
]
FIRMWARE = [f"3.{minor}.{patch}" for minor in range(3) for patch in range(10)]
E17C_FEATURES = ("choices", "toggles", "field_kinds", "validation")


def choices_group(board_names: List[str], status: str) -> JsonObject:
    """E17c: every choice style, the check box and radio elements, one field of each new kind,
    validation and completion. On an older CILogg the SDK sends the fields it knows."""
    if all(plugin.supports(feature) for feature in E17C_FEATURES):
        note = "Each control reports its changes below; Apply needs a valid label and retries."
    else:
        note = ("This CILogg shows these controls as plain fields; check boxes, radio buttons "
                "and validation need a newer CILogg.")
    return group(
        "Choices and fields",
        label("choices_note", note),
        field("speed", "choice", label="Speed", value="auto", choices=SPEEDS,
              tooltip="Icons, a separator, a section title and a disabled choice."),
        field("parity", "choice", label="Parity", value="none", indicator="radio",
              choices=[choice("none", "None"), choice("even", "Even"), choice("odd", "Odd")]),
        field("direction", "choice", label="Direction", value="both", style="radio",
              orientation="horizontal",
              choices=[choice("rx", "Receive"), choice("tx", "Send"), choice("both", "Both")]),
        field("channels", "multichoice", label="Channels", value=["debug"], style="checkboxes",
              columns=2, choices=[choice("debug", "Debug"), choice("modem", "Modem"),
                                  choice("radio", "Radio"), choice("gps", "GPS")]),
        field("firmware", "choice", label="Firmware", value="3.2.1", choices=FIRMWARE,
              searchable=True, tooltip="Type in the open list to filter it."),
        field("target_board", "choice", label="Target board",
              value=board_names[0] if board_names else None,
              choices=board_names, editable=True, completion=True,
              tooltip="Type a board name: the plugin completes it."),
        checkbox("verbose", "Verbose logging", value=None, tristate=True,
                 tooltip="Partly checked: some boards log verbosely."),
        radio_group("mode", [choice("auto", "Automatic"), choice("manual", "Manual"),
                             choice("off", "Off")], "auto", title="Mode",
                    orientation="horizontal"),
        check_group("logs", [choice("boot", "Boot"), choice("radio", "Radio"),
                             choice("modem", "Modem"), choice("power", "Power")], ["boot"],
                    title="Logs", columns=2, all_none=True),
        field("notes", "multiline", label="Notes", rows=3, monospace=True,
              placeholder="One note per line"),
        field("token", "secret", label="Access token"),
        field("level", "slider", label="Log level", value=3, min=0, max=5, step=1),
        field("led_color", "color", label="LED color", value="#1565c0"),
        field("start_date", "date", label="Start date", value="2026-11-19"),
        field("start_time", "time", label="Start time", value="08:30:00"),
        field("start_at", "datetime", label="Start at", value="2026-11-19T08:30:00"),
        field("board_label", "text", label="Board label", required=True,
              pattern="[A-Za-z0-9_-]+", placeholder="board_7",
              tooltip="Letters, digits, _ and - only."),
        field("retries", "integer", label="Retries", value=3, min=0, max=10, step=1),
        row(button("apply", "Apply", event=True, requires_valid=["board_label", "retries"],
                   tooltip="Enabled while the board label and the retries are valid."),
            label("choices_status", status),
            id="apply_row"),
        id="choices",
    )


def gallery_tree() -> JsonObject:
    with state_lock:
        rows = [board_row(board_id) for board_id in sorted_board_ids()]
        current = revision
        status = status_text()
        board_names = sorted(str(board["name"]) for board in boards.values())
        control_event = last_control_event
        table_event = last_table_event
    if plugin.supports("tooltips"):
        note = "Rest the pointer on the controls to see their tooltips."
    else:
        note = "This CILogg shows static tooltips only; advanced tooltips need a newer CILogg."
    return column(
        group(
            "Static tooltips",
            label("intro", note, tooltip="A static tooltip on a label."),
            field("board_name", "text", label="Board name", placeholder="Board 7",
                  tooltip="A static tooltip on a field.",
                  accessible_description="The board name used in reports."),
            button("hello", "Static", event=True, tooltip="A static tooltip on a button."),
            id="static",
        ),
        group(
            "Advanced tooltips",
            button("summary", "Fleet summary", event=True,
                   tooltip="Fleet summary (shown until the plugin answers).",
                   tooltip_provider=True, tooltip_key=f"summary-{current}",
                   accessible_name="Fleet summary",
                   accessible_description="Its tooltip summarizes every board."),
            boards_table(rows),
            row(button("change", "Change data", event=True,
                       tooltip="Changes the board data; the tooltips follow."),
                label("status", status, tooltip_provider=True),
                id="change_row"),
            label("table_status", table_event if table_event is not None else table_note()),
            id="advanced",
        ),
        choices_group(board_names, control_event),
        id="body",
    )


def icon(fallback_tone: str) -> str:
    return image_ref(CHIP_ICON) if icons_registered else status_ref(fallback_tone)


@plugin.tooltip(VIEW, "summary")
def summary_tooltip(_query: TooltipQuery) -> Tooltip:
    with state_lock:
        rows = [(board["name"], board["state"]) for board in boards.values()]
        offline = sum(1 for board in boards.values() if board["state"] == "offline")
        current = revision
    return tooltip_content(f"{len(rows)} boards, {offline} offline.", title="Fleet summary",
                           rows=rows, tone="warn" if offline else "ok", icon=icon("ok"),
                           footer=f"Data revision {current}", ttl_ms=10000)


@plugin.tooltip(VIEW, TABLE)
def cell_tooltip(query: TooltipQuery) -> Optional[Tooltip]:
    """One tooltip per cell: the query carries the row and the column."""
    with state_lock:
        board = dict(boards.get(query.row_id or "", {}))
    if not board:
        return None
    name, state = str(board["name"]), str(board["state"])
    if query.column_id == "state":
        return tooltip_content(f"{name} is {state}.", title=state.capitalize(),
                               tone=STATE_TONES[state], icon=status_ref(STATE_TONES[state]))
    if query.column_id == "uptime":
        return tooltip_content(f"Up for {board['uptime']} min.",
                               rows=[("Resets", board["resets"])], tone="muted")
    return tooltip_content(title=name, rows=[("Firmware", board["firmware"]), ("Row", query.row_id)],
                           icon=icon("normal"), footer="Advanced cell tooltip")


@plugin.tooltip(VIEW)
def other_tooltips(query: TooltipQuery) -> Optional[str]:
    """Any other element of the view that asks (here the status label): a plain string."""
    if query.element_id == "status":
        with state_lock:
            return f"The data changed {revision} times."
    return None


def change_data() -> Tuple[List[JsonObject], str, int, bool]:
    """Advance every board; returns the rows, the status text, the offline count and whether
    the row order stayed the same."""
    global revision
    with state_lock:
        revision += 1
        for board in boards.values():
            board["state"] = NEXT_STATE[str(board["state"])]
            board["uptime"] = 0 if board["state"] == "offline" else int(str(board["uptime"])) + 5
            if board["state"] == "offline":
                board["resets"] = int(str(board["resets"])) + 1
        rows = [board_row(board_id) for board_id in sorted_board_ids()]
        return rows, status_text(), offline_count(), sort_key != "state"


@plugin.on_view_opened
def opened(params: JsonObject) -> None:
    global icons_registered
    window_id = params.get("window_id")
    if params.get("view_id") == EXPLORER:
        explorer_opened(window_id if isinstance(window_id, str) else None)
        return
    with state_lock:
        if isinstance(window_id, str):
            open_windows.add(window_id)
    if not icons_registered:  # Registered images live until the plugin stops.
        icons_registered = plugin.register_icons({CHIP_ICON: svg_icon(CHIP_SVG, mask=True)})
    plugin.update(VIEW, gallery_tree(), window_id)


@plugin.on_view_closed
def closed(params: JsonObject) -> None:
    windows = explorer_windows if params.get("view_id") == EXPLORER else open_windows
    with state_lock:
        windows.discard(str(params.get("window_id")))


def event_value(value: object) -> str:
    """A value as the choices status label shows it: text as typed, anything else as JSON."""
    return value if isinstance(value, str) else json.dumps(value)


def control_event_text(params: JsonObject) -> Optional[str]:
    """The status of a `changed` event ("changed <element>=<value> valid=<true|false>"; an
    event without `valid`, such as a check box's, counts as valid) or of the Apply click."""
    element_id, event_type = params.get("element_id"), params.get("type")
    if event_type == "changed":
        valid = "false" if params.get("valid") is False else "true"
        return f"changed {element_id}={event_value(params.get('value'))} valid={valid}"
    if element_id == "apply" and event_type == "clicked":
        inputs = params.get("inputs")
        values = inputs if isinstance(inputs, dict) else {}
        return ("clicked apply " + " ".join(f"{name}={event_value(values.get(name))}"
                                             for name in ("board_label", "retries")))
    return None


def show_control_event(text: str) -> None:
    global last_control_event
    with state_lock:
        last_control_event = text
    plugin.patch(VIEW, [set_props("choices_status", text=text)])


@plugin.complete(VIEW, "target_board")
def complete_board(query: CompleteQuery) -> List[CompletionItem]:
    """The editable Target board choice: board names that start with the typed text (any
    case), with their state."""
    typed = query.text.strip().casefold()
    with state_lock:
        found = sorted((str(board["name"]), str(board["state"])) for board in boards.values())
    return [completion_item(name, f"{name} ({state})", icon=status_ref(STATE_TONES[state]))
            for name, state in found if name.casefold().startswith(typed)]


def fleet_changed(offline: int) -> None:
    """The summary tooltip and the Boards entry of the Plugins menu follow the board data (a
    CILogg without tooltips or menus gets nothing)."""
    plugin.invalidate_tooltips(VIEW, "summary")
    plugin.update_menu(MENU, [menu_set("boards", title=f"Boards ({offline} offline)")])


@plugin.on_view_event
def event(params: JsonObject) -> None:
    if params.get("view_id") == EXPLORER:
        explorer_event(params)
        return
    if params.get("element_id") == TABLE:
        table_event(params)
        return
    if params.get("element_id") != "change" or params.get("type") != "clicked":
        text = control_event_text(params)
        if text is not None:
            show_control_event(text)
        return
    rows, status, offline, same_order = change_data()
    # Patching rows and the status label drops their cached tooltips on the host. The summary
    # button is not patched, so its cached tooltip is invalidated explicitly. Sorted by state,
    # the order changes too, so the rows are replaced.
    plugin.patch(VIEW, [(update_rows if same_order else replace_rows)(TABLE, rows),
                        set_props("status", text=status)])
    fleet_changed(offline)


# The boards table (E17d). Its events and the commands of its context menu show in the
# table_status label as "activated <row> <column>", "checked <rows> <true|false>",
# "moved <rows> before <row|end>" and "menu <entry> <row|none> <selected count>".
def id_list(value: object) -> List[str]:
    return [item for item in value if isinstance(item, str)] if isinstance(value, list) else []


def show_table_event(text: str, rows: bool = False, select: Optional[List[str]] = None) -> None:
    """Show a table event or a board menu command in the table status label, with the rows
    when they changed and the selection to restore. A closed gallery shows the text when it
    opens."""
    global last_table_event
    with state_lock:
        last_table_event = text
        if not open_windows:
            return
        changes: List[JsonObject] = []
        if rows:
            changes.append(replace_rows(TABLE, [board_row(board_id)
                                                for board_id in sorted_board_ids()]))
        if select:
            changes.append(select_rows(TABLE, select, current=select[0], scroll_to=select[0]))
        changes.append(set_props("table_status", text=text))
    plugin.patch(VIEW, changes)


def move_boards(row_ids: List[str], before: Optional[str]) -> List[str]:
    """Move the dragged rows before `before` (None: to the end) in the shown order, which
    becomes the manual order; returns the moved rows that exist."""
    global board_order, sort_key
    with state_lock:
        shown = sorted_board_ids()
        moved = [board_id for board_id in dict.fromkeys(row_ids) if board_id in boards]
        if before is not None and before in moved and before in shown:
            # Dropped on one of the dragged rows: before the next row that stays.
            after = shown[shown.index(before) + 1:]
            before = next((board_id for board_id in after if board_id not in moved), None)
        rest = [board_id for board_id in shown if board_id not in moved]
        position = rest.index(before) if before is not None and before in rest else len(rest)
        order = rest[:position] + moved + rest[position:]
        # Removed boards keep their place for "Restore Removed Boards".
        board_order = order + [board_id for board_id in board_order if board_id not in order]
        sort_key = "manual"
    return moved


def table_event(params: JsonObject) -> None:
    """activated (a double-click or Enter on a cell), checked (the name column's check boxes;
    the plugin keeps the state) and moved (dragged rows: the plugin confirms the new order by
    replacing the rows and selects them again). `selected` needs nothing here."""
    event_type, value = params.get("type"), params.get("value")
    fields: Dict[str, object] = value if isinstance(value, dict) else {}
    if event_type == "activated":
        show_table_event(f"activated {fields.get('row_id')} {fields.get('column_id')}")
    elif event_type == "checked":
        row_ids = id_list(fields.get("row_ids")) or id_list([fields.get("row_id")])
        checked = fields.get("checked") is True
        with state_lock:
            for board_id in row_ids:
                if board_id in boards:
                    boards[board_id]["checked"] = checked
        show_table_event(f"checked {','.join(row_ids)} {'true' if checked else 'false'}")
    elif event_type == "moved":
        row_ids = id_list(fields.get("row_ids"))
        before = fields.get("before")
        moved = move_boards(row_ids, before if isinstance(before, str) else None)
        show_table_event(f"moved {','.join(row_ids)} before "
                         f"{before if isinstance(before, str) else 'end'}",
                         rows=True, select=moved)


@plugin.on_command(BOARD_MENU)
def board_action(command: UiCommand) -> None:
    """The entries of the table's context menu (plugin.json menu "board_actions", location
    "element"). The command carries the clicked row and the selected rows; Restart Board and
    Remove from List act on the selected rows (CILogg shows them only with a selection)."""
    targets = command.selected_ids or ([command.row_id] if command.row_id else [])
    changed = False
    with state_lock:
        if command.item_id == "restart":
            for board_id in targets:
                board = boards.get(board_id)
                if board is not None:
                    board.update(state="connected", uptime=0, resets=int(str(board["resets"])) + 1)
                    changed = True
        elif command.item_id == "forget":
            for board_id in targets:
                if board_id in boards:
                    removed_boards[board_id] = boards.pop(board_id)
                    changed = True
        elif command.item_id == "restore" and removed_boards:
            boards.update(removed_boards)
            removed_boards.clear()
            changed = True
        offline = offline_count()
    show_table_event(f"menu {command.item_id} {command.row_id or 'none'} "
                     f"{command.selected_count}", rows=changed)
    if changed:
        fleet_changed(offline)


# Menus (plugin.json "menus"). "Open gallery" is a view entry that CILogg runs itself, with
# the shortcut Ctrl+Alt+Shift+G; the event entries below reach the plugin as ui/command.
def show_changes(rows: bool = False) -> None:
    """Show a command's effect in the open gallery. A closed gallery shows it when it opens,
    since gallery_tree() reads the same state."""
    with state_lock:
        if not open_windows:
            return
        changes = [set_props("status", text=status_text())]
        if rows:
            changes.insert(0, replace_rows(TABLE, [board_row(board_id)
                                                   for board_id in sorted_board_ids()]))
    plugin.patch(VIEW, changes)


@plugin.menu_populate(MENU, "boards")
def boards_menu(_query: MenuQuery) -> MenuPopulateResult:
    """The dynamic Boards submenu: one entry per board, in the gallery's order."""
    with state_lock:
        entries = [menu_item(f"board_{board_id}",
                             f"{boards[board_id]['name']} ({boards[board_id]['state']})",
                             icon=status_ref(STATE_TONES[str(boards[board_id]["state"])]),
                             tooltip=f"Firmware {boards[board_id]['firmware']}", event=True)
                   for board_id in sorted_board_ids()]
    return {"items": entries, "cache_ms": 2000}  # CILogg may show it again for 2 s unasked.


@plugin.on_command(MENU, "highlight_offline")
def highlight(command: UiCommand) -> None:
    """A checkable entry: the command carries its new state."""
    global highlight_offline, last_command
    with state_lock:
        highlight_offline = command.checked is True
        last_command = "Offline boards highlighted." if highlight_offline else "Highlighting is off."
    show_changes(rows=True)


@plugin.on_command(MENU, "sort_name")
@plugin.on_command(MENU, "sort_state")
def sort(command: UiCommand) -> None:
    """Radio entries (group "sort"): CILogg checks the picked one and clears the other."""
    global sort_key, last_command
    if command.checked is False:
        return
    with state_lock:
        sort_key = "state" if command.item_id == "sort_state" else "name"
        last_command = f"Sorted by {sort_key}."
    show_changes(rows=True)


@plugin.on_command(MENU)
def board_picked(command: UiCommand) -> None:
    """The other event entries of the menu: those of the dynamic Boards submenu."""
    global last_command
    board_id = command.item_id[len("board_"):] if command.item_id.startswith("board_") else ""
    with state_lock:
        board = boards.get(board_id)
        if board is None:
            return
        last_command = f"{board['name']} picked from the Boards menu ({board['state']})."
    show_changes()


@plugin.on_command("lines", "count_lines")
def count_lines(command: UiCommand) -> None:
    """A log context menu entry; CILogg shows it only while lines are selected."""
    global last_command
    count, ranges = command.line_count, len(command.ranges)
    where = "filtered view" if command.context.get("view") == "filtered" else "log"
    with state_lock:
        last_command = (f"{count} selected line{'' if count == 1 else 's'} in {ranges} "
                        f"range{'' if ranges == 1 else 's'} of the {where}.")
    show_changes()


@plugin.on_command("tabs", "describe_tab")
def describe_tab(command: UiCommand) -> None:
    """A tab context menu entry; the context names the tab, its kind and its file or port."""
    global last_command
    context = command.context
    source = context.get("file_path") or context.get("port_name") or "no file"
    kind = f" ({context['tab_kind']})" if context.get("tab_kind") else ""
    with state_lock:
        last_command = f"Tab {context.get('tab_id', '?')}{kind}: {source}."
    show_changes()


# The explorer view (E17e). The helpers below read the shared state: call them with
# state_lock held.
def node_kind(node_id: Optional[str]) -> Optional[str]:
    """top (None), bench, board, module, or None for an unknown id."""
    if node_id is None:
        return "top"
    if node_id in BENCH_NAMES:
        return "bench"
    if node_id in explorer_boards:
        return "board"
    board_id, _, module = node_id.partition(".")
    return "module" if board_id in explorer_boards and module in MODULE_NAMES else None


def parent_of(node_id: str) -> Optional[str]:
    return next((parent for parent, children in explorer_children.items()
                 if node_id in children), None)


def plural(count: int, word: str) -> str:
    return f"{count} {word}{'' if count == 1 else 's'}"


def module_node(node_id: str) -> TreeNode:
    board_id, _, module = node_id.partition(".")
    power = "off" if explorer_boards[board_id]["state"] == "offline" else "on"
    return tree_node(node_id, cells=[MODULE_NAMES[module], power], icon=builtin_ref("chip"),
                     tone="muted" if power == "off" else None, tooltip_key=f"{node_id}-{power}",
                     checked=node_id in checked_nodes)


def board_node(board_id: str) -> TreeNode:
    """A board with its modules inline: the host never asks for them."""
    board = explorer_boards[board_id]
    state = board["state"]
    return tree_node(board_id,
                     cells=[board["name"], cell(state, icon=status_ref(STATE_TONES[state]))],
                     icon=builtin_ref("device"), tooltip_key=f"{board_id}-{state}",
                     checked=board_id in checked_nodes,
                     children=[module_node(module) for module in explorer_children[board_id]])


def bench_node(bench_id: str) -> TreeNode:
    """A bench: with its boards once the host has them, else `has_children`, so expanding it
    asks tree/children (the tree is `lazy`)."""
    boards = explorer_children[bench_id]
    loaded = bench_id in loaded_benches
    return tree_node(bench_id, cells=[BENCH_NAMES[bench_id], plural(len(boards), "board")],
                     icon=builtin_ref("folder"), tooltip_key=f"{bench_id}-{len(boards)}",
                     checked=bench_id in checked_nodes,
                     children=[board_node(board_id) for board_id in boards] if loaded else None,
                     has_children=True if boards and not loaded else None)


def explorer_node(node_id: str) -> TreeNode:
    kind = node_kind(node_id)
    if kind == "bench":
        return bench_node(node_id)
    return board_node(node_id) if kind == "board" else module_node(node_id)


def explorer_view() -> JsonObject:
    """A lazy tree with check boxes (parents follow their children), dragging, expand events,
    node tooltips and a context menu, and a file tree of the gallery's folder. A CILogg
    without trees skips these elements, so the plugin leaves them out and says why."""
    has_tree, has_files = plugin.supports("tree"), plugin.supports("file_tree")
    with state_lock:
        nodes = [bench_node(bench_id) for bench_id in explorer_children[None]]
        files = list(selected_files)
        status = last_explorer_event if last_explorer_event is not None else EXPLORER_HINT
    if has_tree and has_files:
        note = "Boards load when their bench expands; drag nodes to reorder them."
    else:
        note = "This CILogg cannot show trees: the explorer needs a newer CILogg."
    items = [label("explorer_note", note)]
    if has_tree:
        items.append(group("Benches", tree(
            TREE, nodes, [table_column("name", "Name", 200, tooltip="Benches, boards and modules"),
                          table_column("state", "State", 110)],
            "multi", lazy=True, checkboxes=True, auto_tristate=True, reorderable=True,
            expand_events=True, context_menu=NODE_MENU, tooltip_provider=True,
            accessible_name="Benches"), id="bench_group"))
    if has_files:
        items.append(group("Gallery files", file_tree(
            FILES, [GALLERY_DIR], filters=["*.py", "*.json", "*.md"], selection="multi",
            context_menu=FILE_MENU, value=files or None, accessible_name="Gallery files"),
            id="file_group"))
    items.append(label("explorer_status", status))
    return column(*items, id="explorer_body")


def explorer_opened(window_id: Optional[str]) -> None:
    with state_lock:
        if window_id:
            explorer_windows.add(window_id)
    plugin.update(EXPLORER, explorer_view(), window_id)


def show_explorer_event(text: str, changes: Optional[List[JsonObject]] = None) -> None:
    """Show an event or a command in the explorer's status label, after the tree changes. A
    closed explorer shows the text when it opens, built from the same state."""
    global last_explorer_event
    with state_lock:
        last_explorer_event = text
        if not explorer_windows:
            return
    tree_changes = changes if changes and plugin.supports("tree") else []
    plugin.patch(EXPLORER, [*tree_changes, set_props("explorer_status", text=text)])


@plugin.tree_children(EXPLORER, TREE)
def load_boards(query: TreeChildrenQuery) -> Optional[List[TreeNode]]:
    """A bench expanded: its boards, each with its modules inline (None: no children)."""
    with state_lock:
        kind = node_kind(query.node_id) if query.node_id else None
        if kind not in ("bench", "board"):
            return None
        if kind == "bench":
            loaded_benches.add(query.node_id)
        return [explorer_node(child) for child in explorer_children[query.node_id]]


@plugin.tooltip(EXPLORER, TREE)
def node_tooltip(query: TooltipQuery) -> Optional[Tooltip]:
    """The advanced tooltip of a node: the query carries node_id."""
    node_id = query.node_id or ""
    with state_lock:
        kind = node_kind(node_id) if node_id else None
        if kind == "bench":
            names = [explorer_boards[board_id]["name"] for board_id in explorer_children[node_id]]
            return tooltip_content(f"{plural(len(names), 'board')} on this bench.",
                                   title=BENCH_NAMES[node_id],
                                   rows=[("Boards", ", ".join(names) or "none")],
                                   icon=builtin_ref("folder"))
        if kind == "board":
            board = explorer_boards[node_id]
            tone = STATE_TONES[board["state"]]
            bench = BENCH_NAMES.get(parent_of(node_id) or "", "none")
            return tooltip_content(f"{board['name']} is {board['state']}.", title=board["name"],
                                   rows=[("Firmware", board["firmware"]), ("Bench", bench)],
                                   tone=tone, icon=status_ref(tone))
        if kind == "module":
            board_id, _, module = node_id.partition(".")
            return tooltip_content(f"The {MODULE_NAMES[module].lower()} of "
                                   f"{explorer_boards[board_id]['name']}.", tone="muted")
    return None


def descendants(node_id: str) -> List[str]:
    found: List[str] = []
    pending = list(explorer_children.get(node_id, []))
    while pending:
        child = pending.pop()
        found.append(child)
        pending.extend(explorer_children.get(child, []))
    return found


def follow_children(parent: Optional[str]) -> None:
    """A parent and its ancestors are checked when all their children are (auto_tristate)."""
    while parent is not None:
        children = explorer_children[parent]
        if children and all(child in checked_nodes for child in children):
            checked_nodes.add(parent)
        else:
            checked_nodes.discard(parent)
        parent = parent_of(parent)


def set_checked(node_ids: List[str], checked: bool) -> None:
    """Keep the check boxes like the host shows them (auto_tristate): a node and every
    descendant, also the ones not loaded yet, and parents checked when all children are."""
    for node_id in node_ids:
        if node_kind(node_id) in (None, "top"):
            continue
        for item in [node_id, *descendants(node_id)]:
            if checked:
                checked_nodes.add(item)
            else:
                checked_nodes.discard(item)
        follow_children(parent_of(node_id))


def move_explorer_nodes(node_ids: List[str], parent: Optional[str],
                        before: Optional[str]) -> Optional[Tuple[List[str], List[JsonObject]]]:
    """Move dragged nodes under `parent` (None: the top level) before `before` (None: the
    end) in the plugin's order and return them with the patch entries that show the new
    order; None (nothing changes) when the drop makes no sense: benches stay at the top
    level, boards go into benches and modules into boards."""
    target = node_kind(parent)
    moved = [node_id for node_id in dict.fromkeys(node_ids) if node_kind(node_id) is not None]
    if target is None or not moved or any(node_kind(node_id) != CHILD_KIND.get(target)
                                          for node_id in moved):
        return None
    siblings = explorer_children[parent]
    if before is not None and before not in siblings:
        return None
    if before in moved:  # Dropped on one of the dragged nodes: before the next one that stays.
        after = siblings[siblings.index(before) + 1:]
        before = next((node_id for node_id in after if node_id not in moved), None)
    sources: List[str] = []
    for node_id in moved:
        source = parent_of(node_id)
        explorer_children[source].remove(node_id)
        if source is not None and source != parent and source not in sources:
            sources.append(source)
    rest = explorer_children[parent]
    position = rest.index(before) if before is not None else len(rest)
    explorer_children[parent] = rest[:position] + moved + rest[position:]
    for changed in [*sources, parent]:  # Their check boxes follow their new children.
        follow_children(changed)
    if parent in BENCH_NAMES:
        loaded_benches.add(parent)  # The patch below gives the host its boards.
    if parent is None:
        entry = replace_nodes(TREE, [bench_node(bench_id) for bench_id in explorer_children[None]])
    else:  # The sources first, so no node is in two places at once.
        entry = update_nodes(TREE, [explorer_node(node_id) for node_id in [*sources, parent]])
    return moved, [entry, select_nodes(TREE, moved, current=moved[0], scroll_to=moved[0])]


def tree_event(event_type: object, params: JsonObject) -> Optional[Tuple[str, List[JsonObject]]]:
    """The status text of a tree event and the patch entries it needs."""
    value = params.get("value")
    fields: Dict[str, object] = value if isinstance(value, dict) else {}
    if event_type == "selected":
        current = params.get("current")
        return (f"selected {','.join(id_list(value)) or 'none'} current "
                f"{current if isinstance(current, str) else 'none'}"), []
    if event_type == "activated":
        return f"activated {fields.get('node_id')} {fields.get('column_id')}", []
    if event_type in ("expanded", "collapsed"):
        return f"{event_type} {fields.get('node_id')}", []
    if event_type == "checked":
        node_ids = id_list(fields.get("node_ids")) or id_list([fields.get("node_id")])
        checked = fields.get("checked") is True
        with state_lock:
            set_checked(node_ids, checked)
        return f"checked {','.join(node_ids)} {'true' if checked else 'false'}", []
    if event_type == "moved":
        node_ids = id_list(fields.get("node_ids"))
        parent, before = fields.get("parent"), fields.get("before")
        with state_lock:
            result = move_explorer_nodes(node_ids, parent if isinstance(parent, str) else None,
                                         before if isinstance(before, str) else None)
        text = (f"moved {','.join(node_ids)} into {parent if isinstance(parent, str) else 'top'} "
                f"before {before if isinstance(before, str) else 'end'}")
        return (f"{text} refused", []) if result is None else (text, result[1])
    return None


def file_name(path: str) -> str:
    """A path of the file tree relative to the gallery's folder."""
    return path[len(GALLERY_DIR) + 1:] if path.startswith(GALLERY_DIR + "/") else path


def explorer_event(params: JsonObject) -> None:
    """Tree events change the plugin's model (checked, moved) and show in the status label;
    the file tree's selection is kept for the next time the explorer opens."""
    element_id, event_type = params.get("element_id"), params.get("type")
    value = params.get("value")
    if element_id == TREE:
        shown = tree_event(event_type, params)
        if shown is not None:
            show_explorer_event(*shown)
    elif element_id == FILES and event_type == "selected":
        paths = id_list(value)
        with state_lock:
            selected_files[:] = paths
        show_explorer_event(f"selected files {','.join(map(file_name, paths)) or 'none'}")
    elif element_id == FILES and event_type == "activated":
        path = value.get("path") if isinstance(value, dict) else None
        name = file_name(path) if isinstance(path, str) else "none"
        show_explorer_event(f"activated file {name}")


def boards_under(node_id: str) -> List[str]:
    kind = node_kind(node_id)
    if kind == "bench":
        return list(explorer_children[node_id])
    if kind == "board":
        return [node_id]
    return [node_id.partition(".")[0]] if kind == "module" else []


@plugin.on_command(NODE_MENU)
def node_action(command: UiCommand) -> None:
    """The tree's context menu (plugin.json menu "node_actions", location "element"): Reset
    Node connects the boards of the clicked or selected nodes again; Collapse All closes every
    bench and board. The command carries node_id (the clicked node) instead of row_id."""
    targets = command.selected_ids or ([command.node_id] if command.node_id else [])
    changes: List[JsonObject] = []
    with state_lock:
        if command.item_id == "reset_node":
            reset = [board_id for node_id in targets for board_id in boards_under(node_id)
                     if explorer_boards[board_id]["state"] != "connected"]
            for board_id in reset:
                explorer_boards[board_id]["state"] = "connected"
            shown = [board_id for board_id in dict.fromkeys(reset)
                     if parent_of(board_id) in loaded_benches]
            if shown:
                changes.append(update_nodes(TREE, [board_node(board_id) for board_id in shown]))
        elif command.item_id == "collapse_all":
            benches = explorer_children[None]
            changes.append(collapse_nodes(TREE, [*benches, *(
                board_id for bench_id in benches if bench_id in loaded_benches
                for board_id in explorer_children[bench_id])]))
    show_explorer_event(f"menu {command.item_id} {command.node_id or 'none'} "
                        f"{command.selected_count}", changes)


@plugin.on_command(FILE_MENU)
def file_action(command: UiCommand) -> None:
    """The file tree's context menu: Show Path shows the clicked path (node_id) and the number
    of selected paths."""
    show_explorer_event(f"path {command.node_id or 'none'} ({command.selected_count} selected)")


if __name__ == "__main__":
    plugin.serve()
