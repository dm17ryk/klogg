"""E17f parity: the ui-gallery layout view through every surface.

The private host gets the ui-gallery example (docs/plugin-sdk-advanced.md section 29). The GUI
renders the layout view (tabs, a splitter, text views, a chart); a Python global script switches its
tabs through set_ui; the CLI and native MCP read the menu button's drop-down entries and pick them
through trigger_plugin_menu; the CLI toggles the checkable pin button through set_ui; every surface
reads the plugin's answer in the layout view's status label, and the log text view through get_ui.
"""

import json
import logging
import re
import time

from workbench_contracts import ParityError

LOG = logging.getLogger("workbench.layout")
PLUGIN = "org.cilogg.ui-gallery"
VIEW = "layout"
MENU = "layout_menu"
BUTTON = "menu_button"
# A host runs one global script per run: the E17a tooltips case runs the layout fixture in its
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


def exercise_layout(cli, command, catalog, artifacts, run_python):
    del catalog  # the operations were checked by the runner
    cli("plugin_manage", "--plugin-operation", "enable", "--plugin-id", PLUGIN)
    # Python switched the tab in the tooltips case's script, which opened the view.
    python = PYTHON_RESULT or run_python([("e17_layout", {"plugin": PLUGIN, "view": VIEW})])[0]
    if python.get("errors") or python.get("status") != python.get("expected"):
        raise ParityError(f"python_fixture_failed: {python}")
    cli("open_plugin_view", "--plugin-id", PLUGIN, "--view-id", VIEW)

    def objects():
        return {item.get("objectName"): item
                for item in command("get_ui", {"window_index": 0})["objects"]}

    def prop(element, name):
        return objects().get(element_name(element), {}).get("properties", {}).get(name)

    def answered(pattern):
        return wait_until(lambda: (lambda text: text if text and re.fullmatch(pattern, text)
                                   else None)(prop("layout_status", "text")), 20,
                          f"no_answer: {pattern}")

    # GUI: the containers and content elements render with their widgets.
    found = wait_until(lambda: (lambda items: items if element_name("chart") in items
                                else None)(objects()), 40, "layout_not_rendered")
    expected = {"layout_tabs": "QTabWidget", "charts_split": "QSplitter",
                "log": "PluginTextBrowser", "notes": "PluginTextBrowser", "chart": "PluginChart",
                "spark": "PluginSparkline", "picture": "PluginImage"}
    for element, class_name in expected.items():
        if found.get(element_name(element), {}).get("className") != class_name:
            raise ParityError(f"{element}_not_{class_name}: {found.get(element_name(element))}")
    if found[element_name("layout_tabs")].get("properties", {}).get("currentIndex") != 1:
        raise ParityError("python_tab_not_current")

    # The menu button's drop-down reads the same on the CLI and native MCP.
    cli_entries = cli("get_plugin_menus", "--location", "element", "--plugin-id", PLUGIN,
                      "--view-id", VIEW, "--element-id", BUTTON)
    mcp_entries = command("get_plugin_menus", {"location": "element", "plugin_id": PLUGIN,
                                               "view_id": VIEW, "element_id": BUTTON})

    def comparable(result):
        return {entry["itemId"]: {key: entry.get(key) for key in STATE_KEYS}
                for entry in result["entries"]}

    if comparable(cli_entries) != comparable(mcp_entries):
        raise ParityError(f"button_menus_differ: {comparable(cli_entries)} "
                          f"{comparable(mcp_entries)}")
    entries = comparable(cli_entries)
    if not {"add_line", "clear_log"} <= set(entries):
        raise ParityError(f"button_menu_entries: {entries}")
    context = cli_entries["context"]
    if context.get("element_id") != BUTTON or "row_id" in context or "node_id" in context:
        raise ParityError(f"button_context: {context}")

    # The CLI adds a line to the log, native MCP clears it; get_ui reads the text view.
    cli("trigger_plugin_menu", "--plugin-id", PLUGIN, "--menu-id", MENU, "--item-id", "add_line",
        "--view-id", VIEW, "--element-id", BUTTON)
    answered(re.escape(f"menu add_line {BUTTON}"))
    wait_until(lambda: "line " in (prop("log", "plainText") or ""), 20, "log_line_missing")
    command("trigger_plugin_menu", {"plugin_id": PLUGIN, "menu_id": MENU, "item_id": "clear_log",
                                    "view_id": VIEW, "element_id": BUTTON})
    answered(re.escape(f"menu clear_log {BUTTON}"))
    wait_until(lambda: prop("log", "plainText") == "", 20, "log_not_cleared")

    # The checkable pin button through set_ui on the CLI, and back through native MCP.
    request = artifacts / "e17f-pin.json"
    request.write_text(json.dumps({"checked": True}), encoding="utf-8")
    cli("set_ui", "--object-name", element_name("pin_button"), "--json-file", str(request))
    answered("pinned true")
    command("set_ui", {"window_index": 0, "object_name": element_name("pin_button"),
                       "definition": {"checked": False}})
    answered("pinned false")
    LOG.info("layout_completed %s", json.dumps(sorted(entries)))
    return {"id": "e17_layout", "passed": True, "python": python["status"],
            "entries": sorted(entries)}
