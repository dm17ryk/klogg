"""UI gallery: static and advanced tooltips, a registered icon and accessible names (README.md)."""
from __future__ import annotations

import threading
from typing import Dict, List, Optional

from cilogg_plugin import (
    JsonObject, Plugin, Tooltip, TooltipQuery, button, column, field, group, image_ref, label,
    row, set_props, status_ref, svg_icon, table, table_column, table_row, tooltip_content,
    update_rows,
)

plugin = Plugin()
VIEW = "gallery"
CHIP_ICON = "chip"
# A monochrome chip (mask: the host recolours it with the theme's text colour).
CHIP_SVG = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">'
            '<rect x="4" y="4" width="8" height="8" rx="1" fill="#000"/>'
            '<path d="M2 6h2M2 10h2M12 6h2M12 10h2M6 2v2M10 2v2M6 12v2M10 12v2" stroke="#000"/>'
            '</svg>')
STATE_TONES = {"connected": "ok", "idle": "warn", "offline": "error"}
NEXT_STATE = {"connected": "idle", "idle": "offline", "offline": "connected"}

state_lock = threading.Lock()
revision = 0
icons_registered = False
boards: Dict[str, Dict[str, object]] = {
    "b1": {"name": "Board 1", "state": "connected", "uptime": 12, "firmware": "3.2.1", "resets": 0},
    "b2": {"name": "Board 2", "state": "idle", "uptime": 340, "firmware": "3.2.0", "resets": 2},
    "b3": {"name": "Board 3", "state": "offline", "uptime": 0, "firmware": "3.1.9", "resets": 7},
}


def board_row(board_id: str) -> JsonObject:
    board = boards[board_id]
    state = str(board["state"])
    # tooltip_key changes with the row's data, so the host never shows a stale cached tooltip.
    return table_row(board_id, [board["name"], state, f"{board['uptime']} min"],
                     tone=STATE_TONES[state], tooltip=f"{board['name']} (static row tooltip)",
                     tooltip_key=f"{board_id}-{revision}")


def gallery_tree() -> JsonObject:
    with state_lock:
        rows = [board_row(board_id) for board_id in boards]
        current = revision
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
            table("boards",
                  [table_column("name", "Board", 120, tooltip="The board name"),
                   table_column("state", "State", 100, tooltip="The connection state"),
                   table_column("uptime", "Uptime", 90, tooltip="Minutes since the last reset")],
                  rows, selection="single", tooltip_provider=True, accessible_name="Boards"),
            row(button("change", "Change data", event=True,
                       tooltip="Changes the board data; the tooltips follow."),
                label("status", f"Data revision {current}", tooltip_provider=True),
                id="change_row"),
            id="advanced",
        ),
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


@plugin.tooltip(VIEW, "boards")
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


def change_data() -> List[JsonObject]:
    global revision
    with state_lock:
        revision += 1
        for board in boards.values():
            board["state"] = NEXT_STATE[str(board["state"])]
            board["uptime"] = 0 if board["state"] == "offline" else int(str(board["uptime"])) + 5
            if board["state"] == "offline":
                board["resets"] = int(str(board["resets"])) + 1
        return [board_row(board_id) for board_id in boards]


@plugin.on_view_opened
def opened(params: JsonObject) -> None:
    global icons_registered
    if not icons_registered:  # Registered images live until the plugin stops.
        icons_registered = plugin.register_icons({CHIP_ICON: svg_icon(CHIP_SVG, mask=True)})
    plugin.update(VIEW, gallery_tree(), params.get("window_id"))


@plugin.on_view_event
def event(params: JsonObject) -> None:
    if params.get("element_id") != "change" or params.get("type") != "clicked":
        return
    rows = change_data()
    # Patching rows and the status label drops their cached tooltips on the host. The summary
    # button is not patched, so its cached tooltip is invalidated explicitly.
    plugin.patch(VIEW, [update_rows("boards", rows),
                        set_props("status", text=f"Data revision {revision}")])
    plugin.invalidate_tooltips(VIEW, "summary")


if __name__ == "__main__":
    plugin.serve()
