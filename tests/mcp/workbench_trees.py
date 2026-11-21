"""E17e parity: the ui-gallery tree and file tree and their element menus through every surface.

The private host gets the ui-gallery example (docs/plugin-sdk-advanced.md section 28). The GUI
renders the explorer view; a Python global script selects a bench through set_ui, the CLI and
native MCP read a node's advanced tooltip and the tree's element menu by node id and pick its
entries through trigger_plugin_menu, the file tree's menu is picked with a path as the node id,
and every surface reads the plugin's answer in the explorer's status label.
"""

import json
import logging
import re
import time

from workbench_contracts import ParityError

LOG = logging.getLogger("workbench.trees")
PLUGIN = "org.cilogg.ui-gallery"
VIEW = "explorer"
NODE_MENU = "node_actions"
FILE_MENU = "file_actions"
# A host runs one global script per run: the E17a tooltips case runs the trees fixture in its
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


def exercise_trees(cli, command, catalog, artifacts, run_python):
    del catalog, artifacts  # the operations were checked by the runner
    cli("plugin_manage", "--plugin-operation", "enable", "--plugin-id", PLUGIN)
    # Python selected the second bench in the tooltips case's script, which opened the view.
    python = PYTHON_RESULT or run_python([("e17_trees", {"plugin": PLUGIN, "view": VIEW})])[0]
    if python.get("errors") or python.get("status") != python.get("expected"):
        raise ParityError(f"python_fixture_failed: {python}")
    cli("open_plugin_view", "--plugin-id", PLUGIN, "--view-id", VIEW)

    def objects():
        return {item.get("objectName"): item
                for item in command("get_ui", {"window_index": 0})["objects"]}

    def status():
        return objects().get(element_name("explorer_status"), {}).get("properties", {}).get("text")

    def answered(pattern):
        return wait_until(lambda: (lambda text: text if text and re.fullmatch(pattern, text)
                                   else None)(status()), 20, f"no_answer: {pattern}")

    # GUI: the tree and the file tree render.
    found = wait_until(lambda: (lambda items: items if element_name("files") in items
                                else None)(objects()), 40, "explorer_not_rendered")
    for element in ("benches", "files"):
        if found.get(element_name(element), {}).get("className") != "QTreeView":
            raise ParityError(f"{element}_not_a_tree")

    # A node's advanced tooltip reads the same on the CLI and native MCP.
    cli_tip = cli("get_plugin_tooltip", "--plugin-id", PLUGIN, "--view-id", VIEW,
                  "--element-id", "benches", "--node-id", "bench1", "--no-cache")
    mcp_tip = command("get_plugin_tooltip", {"plugin_id": PLUGIN, "view_id": VIEW,
                                             "element_id": "benches", "node_id": "bench1",
                                             "no_cache": True})
    if cli_tip.get("source") != "plugin" or cli_tip.get("tooltip") != mcp_tip.get("tooltip"):
        raise ParityError(f"node_tooltips_differ: {cli_tip} {mcp_tip}")
    if (cli_tip.get("tooltip") or {}).get("title") != "Bench 1":
        raise ParityError(f"node_tooltip: {cli_tip}")

    # The element menu of a node reads the same on the CLI and native MCP.
    cli_entries = cli("get_plugin_menus", "--location", "element", "--plugin-id", PLUGIN,
                      "--view-id", VIEW, "--element-id", "benches", "--node-id", "bench1")
    mcp_entries = command("get_plugin_menus", {"location": "element", "plugin_id": PLUGIN,
                                               "view_id": VIEW, "element_id": "benches",
                                               "node_id": "bench1"})

    def comparable(result):
        return {entry["itemId"]: {key: entry.get(key) for key in STATE_KEYS}
                for entry in result["entries"]}

    if comparable(cli_entries) != comparable(mcp_entries):
        raise ParityError(f"element_menus_differ: {comparable(cli_entries)} "
                          f"{comparable(mcp_entries)}")
    entries = comparable(cli_entries)
    if not {"reset_node", "collapse_all"} <= set(entries):
        raise ParityError(f"element_menu_entries: {entries}")
    context = cli_entries["context"]
    if context.get("node_id") != "bench1" or "row_id" in context:
        raise ParityError(f"element_context: {context}")

    # The CLI picks Reset Node on the second bench, native MCP Collapse All on the first.
    cli("trigger_plugin_menu", "--plugin-id", PLUGIN, "--menu-id", NODE_MENU, "--item-id",
        "reset_node", "--view-id", VIEW, "--element-id", "benches", "--node-id", "bench2")
    answered(r"menu reset_node bench2 \d+")
    command("trigger_plugin_menu", {"plugin_id": PLUGIN, "menu_id": NODE_MENU,
                                    "item_id": "collapse_all", "view_id": VIEW,
                                    "element_id": "benches", "node_id": "bench1"})
    answered(r"menu collapse_all bench1 \d+")

    # The file tree: its root item shows the gallery's folder; a path is the node id.
    items = objects().get(element_name("files"), {}).get("items") or []
    if not items or not items[0]:
        raise ParityError(f"file_tree_roots: {items}")
    folder = str(items[0][0]).replace("\\", "/")
    manifest = f"{folder}/plugin.json"
    cli("trigger_plugin_menu", "--plugin-id", PLUGIN, "--menu-id", FILE_MENU, "--item-id",
        "show_path", "--view-id", VIEW, "--element-id", "files", "--node-id", manifest)
    answered(re.escape(f"path {manifest} (") + r"\d+ selected\)")
    LOG.info("trees_completed %s", json.dumps(sorted(entries)))
    return {"id": "e17_trees", "passed": True, "python": python["status"],
            "entries": sorted(entries)}
