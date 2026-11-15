"""View demo: an interactive dock, a window, a dialog and message boxes (docs/plugin-views.md)."""
import csv
import random
import threading
import time

from cilogg_plugin import (
    Job, JsonObject, Plugin, badge, button, column, editor, field, group, label, progress,
    replace_rows, row, set_props, table, table_column, table_row, update_rows,
)

plugin = Plugin()
ROW_COUNT = 10000
TICK_SECONDS = 0.2
state_lock = threading.Lock()
open_views: set = set()  # (view_id, window_id)
ticker = None


def initial_rows() -> list:
    return [table_row(f"r{index}", [f"item-{index:05d}", "idle", 0]) for index in range(ROW_COUNT)]


rows = initial_rows()

# The editable settings table: one row per editor kind. Cells are strings; `settings` holds the
# typed values the plugin accepted.
LEVELS = ["Low", "Medium", "High"]
DAYS = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
settings: dict = {"level": "Medium", "days": ["Mon", "Fri"], "count": 10, "enabled": True}
SETTING_NAMES = {"level": "Level (choice)", "days": "Days (multichoice)",
                 "count": "Count 1-100 (number)", "enabled": "Enabled (boolean)"}
SETTING_EDITORS = {
    "level": editor("choice", choices=[{"value": item, "label": item} for item in LEVELS]),
    "days": editor("multichoice", choices=[{"value": item, "label": item} for item in DAYS]),
    "count": editor("number", min=1, max=100),
    "enabled": editor("boolean"),
}


def setting_text(row_id: str, value) -> str:
    if row_id == "days":
        return ", ".join(value)
    if row_id == "enabled":
        return "on" if value else "off"
    return str(value)


def settings_row(row_id: str, tone=None, tooltip=None) -> JsonObject:
    return table_row(row_id, [SETTING_NAMES[row_id], setting_text(row_id, settings[row_id])],
                     tone=tone, tooltip=tooltip, editors={"value": SETTING_EDITORS[row_id]})


def parse_setting(row_id: str, value):
    """Return the typed value, or raise ValueError with the reason the edit is rejected."""
    if row_id == "level":
        if value not in LEVELS:
            raise ValueError(f"'{value}' is not one of {', '.join(LEVELS)}")
        return value
    if row_id == "days":
        picked = [item for item in DAYS if item in (value or [])]
        if not picked:
            raise ValueError("Pick at least one day")
        return picked
    if row_id == "count":
        try:
            number = int(value)
        except (TypeError, ValueError):
            raise ValueError(f"'{value}' is not a whole number") from None
        if not 1 <= number <= 100:
            raise ValueError("Count must be between 1 and 100")
        return number
    return bool(value)


def main_tree() -> JsonObject:
    with state_lock:
        snapshot = list(rows)
        settings_rows = [settings_row(row_id) for row_id in settings]
    return column(
        group(
            "Input",
            field("name", "text", label="Name", placeholder="Device name"),
            field("mode", "choice", label="Mode", choices=["safe", "fast"], value="safe"),
            field("verbose", "boolean", label="Verbose output", value=False),
            field("source", "file_open", label="Source file", filter="Log files (*.log *.txt)"),
            field("folder", "directory", label="Work folder"),
            id="input",
        ),
        row(
            button("process", "Process", action="process", tone="primary"),
            button("reset", "Reset table", action="reset", tone="danger",
                   confirm={"title": "Reset table", "text": "Reset all 10,000 rows?",
                            "ok_label": "Reset"}),
            button("export", "Export CSV...", action="export",
                   save_dialog={"filter": "CSV files (*.csv)", "suggested_name": "view-demo.csv"}),
            id="actions",
        ),
        row(
            button("ask", "Ask", event=True),
            button("notice", "Timed notice", event=True),
            button("open_editor", "Open editor", event=True),
            button("open_dialog", "Open dialog", event=True),
            id="events",
        ),
        row(badge("state", "idle", tone="muted"), label("status", "Ready."), id="status_row"),
        progress("progress", value=0.0, text="Idle"),
        label("settings_hint", "Editable cells: double-click a value (needs host feature view_edit)."),
        table("settings", [table_column("name", "Setting", 180),
                           table_column("value", "Value", 180, editable=True)], settings_rows),
        table("rows", [table_column("name", "Name", 140), table_column("state", "State", 80),
                       table_column("count", "Count", 70)],
              snapshot, sortable=True, filter_box=True, selection="multi"),
        id="body",
    )


def editor_tree() -> JsonObject:
    return column(
        label("hint", "A top-level window owned by the CILogg window."),
        field("notes", "text", label="Notes", placeholder="Type a note"),
        button("apply", "Apply and close", event=True, tone="primary"),
        id="editor_body",
    )


def dialog_tree() -> JsonObject:
    return column(
        label("question", "Proceed with the demo operation?", bold=True),
        row(button("yes", "Yes", event=True, tone="primary"), button("no", "No", event=True),
            id="answers"),
        id="dialog_body",
    )


def on_edited(params: JsonObject) -> None:
    """Confirm or reject a cell edit by patching the row back; the host keeps the cell pending
    until it sees that patch."""
    row_id = params.get("row_id")
    if row_id not in settings:
        return
    with state_lock:
        try:
            settings[row_id] = parse_setting(row_id, params.get("value"))
            changed = settings_row(row_id, tone="accent", tooltip=f"was {params.get('previous')}")
            message = f"{SETTING_NAMES[row_id]} set to {setting_text(row_id, settings[row_id])}"
        except ValueError as error:
            changed = settings_row(row_id, tone="error", tooltip=str(error))
            message = f"Rejected: {error}"
    plugin.patch("main", [update_rows("settings", [changed])])
    status(message)


def status(text: str, tone: str = "normal") -> None:
    plugin.patch("main", [set_props("status", text=text, tone=tone)])


def tick() -> None:
    """Patch one random row at a time while the main view is open anywhere."""
    while not plugin.stopping.is_set():
        with state_lock:
            if not any(view == "main" for view, _window in open_views):
                return
            index = random.randrange(ROW_COUNT)
            current = rows[index]
            count = int(current["cells"][2]) + 1
            state = random.choice(["idle", "busy", "done"])
            tone = {"idle": None, "busy": "warn", "done": "ok"}[state]
            rows[index] = table_row(current["id"], [current["cells"][0], state, count], tone)
            changed = rows[index]
        try:
            plugin.patch("main", [update_rows("rows", [changed])])
        except Exception as error:  # The host may already be shutting down.
            print(f"view-demo tick stopped: {error}", flush=True)
            return
        time.sleep(TICK_SECONDS)


@plugin.on_view_opened
def opened(params: JsonObject) -> None:
    global ticker
    view, window = params["view_id"], params.get("window_id")
    with state_lock:
        open_views.add((view, window))
    tree = {"main": main_tree, "editor": editor_tree, "confirm": dialog_tree}[view]()
    plugin.update(view, tree, window)
    if view == "main" and (ticker is None or not ticker.is_alive()):
        ticker = threading.Thread(target=tick, name="view-demo-ticker", daemon=True)
        ticker.start()


@plugin.on_view_closed
def closed(params: JsonObject) -> None:
    with state_lock:
        open_views.discard((params["view_id"], params.get("window_id")))


@plugin.on_view_event
def event(params: JsonObject) -> None:
    element, kind = params.get("element_id"), params.get("type")
    inputs = params.get("inputs", {})
    if kind == "edited" and element == "settings":
        on_edited(params)
    elif kind == "changed" and element == "name":
        status(f"Name: {params.get('value')}")
    elif kind == "selected":
        status(f"{len(params.get('value') or [])} row(s) selected")
    elif element == "ask":
        plugin.message("Apply the demo settings?", title="View Demo", icon="question",
                       buttons=("yes", "no"), default="no",
                       callback=lambda result: status(f"Answer: {result.get('button')}"))
    elif element == "notice":
        plugin.message("This notice closes by itself.", title="View Demo", buttons=("ok",),
                       timeout_ms=5000, modal=False,
                       callback=lambda result: status(
                           "Notice timed out" if result.get("timed_out") else "Notice closed"))
    elif element == "open_editor":
        plugin.show_view("editor")
    elif element == "open_dialog":
        plugin.show_view("confirm")
    elif element == "apply":
        status(f"Editor note: {inputs.get('notes', '')}")
        plugin.hide_view("editor", params.get("window_id"))
    elif element in ("yes", "no"):
        status(f"Dialog answer: {element}")
        plugin.hide_view("confirm", params.get("window_id"))


@plugin.action("process")
def process(job: Job, arguments: JsonObject) -> JsonObject:
    inputs = arguments.get("inputs", {})
    steps = 20
    try:
        plugin.patch("main", [set_props("state", text="running", tone="warn")])
        for step in range(1, steps + 1):
            job.check_cancelled()
            time.sleep(0.1)
            job.progress(step / steps)
            plugin.patch("main", [set_props("progress", value=step / steps,
                                            text=f"Step {step}/{steps}")])
    finally:
        plugin.patch("main", [set_props("state", text="idle", tone="muted"),
                              set_props("progress", value=0.0, text="Idle")])
    status(f"Processed {inputs.get('name') or 'unnamed'} in {inputs.get('mode', 'safe')} mode", "ok")
    return {"steps": steps, "inputs": inputs}


@plugin.action("reset")
def reset(job: Job, _arguments: JsonObject) -> JsonObject:
    global rows
    job.check_cancelled()
    with state_lock:
        rows = initial_rows()
        snapshot = list(rows)
    plugin.patch("main", [replace_rows("rows", snapshot)])
    status("Table reset.")
    return {"rows": len(snapshot)}


@plugin.action("export")
def export(job: Job, arguments: JsonObject) -> JsonObject:
    path = arguments.get("save_path")
    if not path:
        raise ValueError("Use the Export button in the View Demo dock to choose a file")
    with state_lock:
        snapshot = list(rows)
    with open(path, "w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        writer.writerow(["id", "name", "state", "count"])
        for index, item in enumerate(snapshot):
            if index % 1000 == 0:
                job.check_cancelled()
            writer.writerow([item["id"], *item["cells"]])
    status(f"Exported {len(snapshot)} rows.", "ok")
    return {"path": path, "rows": len(snapshot)}


if __name__ == "__main__":
    plugin.serve()
