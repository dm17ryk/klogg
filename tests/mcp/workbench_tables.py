"""E17d parity: the ui-gallery rich table and its element menu through every surface.

The private host gets the ui-gallery example (docs/plugin-sdk-advanced.md section 10). The GUI
renders the boards table; a Python global script changes a check box through set_ui, the CLI and
native MCP select rows through set_ui and pick the table's element menu entries through
trigger_plugin_menu, and every surface reads the plugin's answer in the gallery's table status.
"""

import json
import logging
import re
import time

from workbench_contracts import ParityError

LOG = logging.getLogger("workbench.tables")
PLUGIN = "org.cilogg.ui-gallery"
VIEW = "gallery"
MENU = "board_actions"
# A host runs one global script per run: the E17a tooltips case runs the tables fixture in its
# script and leaves the result here.
PYTHON_RESULT = None
STATE_KEYS = ("menuId", "kind", "enabled", "shown", "title", "conditionsHold")


def wait_until(predicate, timeout, reason):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise ParityError(reason)


def element_name(element):
    return f"pluginView_{PLUGIN}_{VIEW}_{element}"


def exercise_tables(cli, command, catalog, artifacts, run_python):
    del catalog  # the operations were checked by the runner
    cli("plugin_manage", "--plugin-operation", "enable", "--plugin-id", PLUGIN)
    cli("open_plugin_view", "--plugin-id", PLUGIN, "--view-id", VIEW)

    def objects():
        return {item.get("objectName"): item
                for item in command("get_ui", {"window_index": 0})["objects"]}

    def status():
        return objects().get(element_name("table_status"), {}).get("properties", {}).get("text")

    def answered(pattern):
        return wait_until(lambda: (lambda text: text if text and re.fullmatch(pattern, text)
                                   else None)(status()), 20, f"no_answer: {pattern}")

    # GUI: the table and its status render.
    found = wait_until(lambda: (lambda items: items if element_name("table_status") in items
                                else None)(objects()), 40, "table_not_rendered")
    if found.get(element_name("boards"), {}).get("className") != "QTableView":
        raise ParityError("boards_not_a_table")

    # Python changed the third board's check box in the tooltips case's script.
    python = PYTHON_RESULT or run_python([("e17_tables", {"plugin": PLUGIN, "view": VIEW})])[0]
    if python.get("errors") or python.get("status") != python.get("expected"):
        raise ParityError(f"python_fixture_failed: {python}")

    # The element menu reads the same on the CLI and native MCP. The gallery orders the boards
    # by name or by state (the menus case sorts by state, and Restart changes a state), so a
    # board is selected by its shown row, read from get_ui ("Board 2" is b2).
    def shown_row(board_id):
        items = objects().get(element_name("boards"), {}).get("items") or []
        title = f"Board {board_id[1:]}"
        for row, cells in enumerate(items):
            if cells and cells[0] == title:
                return row
        raise ParityError(f"board_not_shown: {board_id} {items}")

    def select(board_id, surface):
        definition = {"indexPath": [shown_row(board_id)], "column": 1}
        if surface == "cli":
            path = artifacts / f"e17d-select-{board_id}.json"
            path.write_text(json.dumps(definition), encoding="utf-8")
            cli("set_ui", "--object-name", element_name("boards"), "--json-file", str(path))
        else:
            command("set_ui", {"window_index": 0, "object_name": element_name("boards"),
                               "definition": definition})

    select("b2", "cli")
    cli_entries = cli("get_plugin_menus", "--location", "element", "--plugin-id", PLUGIN,
                      "--view-id", VIEW, "--element-id", "boards")
    mcp_entries = command("get_plugin_menus", {"location": "element", "plugin_id": PLUGIN,
                                               "view_id": VIEW, "element_id": "boards"})

    def comparable(result):
        return {entry["itemId"]: {key: entry.get(key) for key in STATE_KEYS}
                for entry in result["entries"]}

    if comparable(cli_entries) != comparable(mcp_entries):
        raise ParityError(f"element_menus_differ: {comparable(cli_entries)} "
                          f"{comparable(mcp_entries)}")
    entries = comparable(cli_entries)
    if not {"restart", "forget", "restore"} <= set(entries) or not entries["restart"]["shown"]:
        raise ParityError(f"element_menu_entries: {entries}")
    if cli_entries["context"].get("selected_ids") != ["b2"]:
        raise ParityError(f"element_context: {cli_entries['context']}")

    # CLI picks Restart on the second board, native MCP Remove on the first, the CLI Restore.
    cli("trigger_plugin_menu", "--plugin-id", PLUGIN, "--menu-id", MENU, "--item-id", "restart",
        "--view-id", VIEW, "--element-id", "boards", "--row-id", "b2")
    answered(r"menu restart b2 1")
    select("b1", "mcp")
    command("trigger_plugin_menu", {"plugin_id": PLUGIN, "menu_id": MENU, "item_id": "forget",
                                    "view_id": VIEW, "element_id": "boards", "row_id": "b1"})
    answered(r"menu forget b1 1")
    cli("trigger_plugin_menu", "--plugin-id", PLUGIN, "--menu-id", MENU, "--item-id", "restore",
        "--view-id", VIEW, "--element-id", "boards")
    answered(r"menu restore none \d+")
    LOG.info("tables_completed")
    return {"id": "e17_tables", "passed": True, "python": python["status"],
            "entries": sorted(entries)}
