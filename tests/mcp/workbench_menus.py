"""E17b parity: the ui-gallery menus through every surface.

The private host gets the ui-gallery example (docs/plugin-sdk-advanced.md section 7). The GUI
shows its own Plugins submenu; the CLI, native MCP and a Python global script read the same
entries through get_plugin_menus and pick entries through trigger_plugin_menu, which acts like
a click (check marks, radio groups, opening a view).
"""

import logging
import time

from workbench_contracts import ParityError

LOG = logging.getLogger("workbench.menus")
PLUGIN = "org.cilogg.ui-gallery"
# A host runs one global script per run: the E17a tooltips case runs the menus fixture in its
# script and leaves the result here.
PYTHON_RESULT = None


def wait_until(predicate, timeout, reason):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise ParityError(reason)


def entries_by_id(result):
    return {entry["itemId"]: entry for entry in result["entries"]}


STATE_KEYS = ("menuId", "kind", "enabled", "shown", "checkable", "checked", "group", "shortcut")


def comparable(entries, keys=STATE_KEYS + ("title", "path")):
    """What the surfaces must agree on, per entry."""
    return {item: {key: entry.get(key) for key in keys} for item, entry in entries.items()}


def exercise_menus(cli, command, catalog, artifacts, run_python):
    del catalog, artifacts  # the operations were checked by the runner
    cli("plugin_manage", "--plugin-operation", "enable", "--plugin-id", PLUGIN)
    gallery = next((p for p in command("get_plugins")["plugins"] if p["id"] == PLUGIN), None)
    if not gallery or "ui/command" not in gallery.get("handles", []):
        raise ParityError("ui_gallery_manifest_missing")

    # GUI: the declared Plugins menu replaces the gallery's flat entries.
    def ui_names():
        return {item.get("objectName") for item in command("get_ui", {"window_index": 0})["objects"]}

    names = wait_until(lambda: (lambda found: found if f"pluginMenu_{PLUGIN}_main" in found
                                else None)(ui_names()), 20, "gallery_menu_missing")
    if f"pluginViewAction_{PLUGIN}_gallery" in names:
        raise ParityError("flat_entry_kept")

    # The same entries on every surface. Python read them in the tooltips case's script, before
    # its "Change data" renamed the Boards entry through menu/update: titles are compared between
    # the CLI and native MCP only.
    surfaces = {
        "cli": entries_by_id(cli("get_plugin_menus", "--location", "plugins", "--plugin-id",
                                 PLUGIN)),
        "mcp": entries_by_id(command("get_plugin_menus", {"location": "plugins",
                                                          "plugin_id": PLUGIN})),
    }
    python = PYTHON_RESULT or run_python([("e17_menus", {"plugin": PLUGIN})])[0]
    if python.get("errors"):
        raise ParityError(f"python_fixture_failed: {python['errors']}")
    if comparable(surfaces["cli"]) != comparable(surfaces["mcp"]):
        raise ParityError(f"menus_differ: cli={comparable(surfaces['cli'])} "
                          f"mcp={comparable(surfaces['mcp'])}")
    states = {name: comparable(entries, STATE_KEYS) for name, entries in
              dict(surfaces, python=entries_by_id(python["before"])).items()}
    if len({repr(sorted(view.items())) for view in states.values()}) != 1:
        raise ParityError(f"menu_states_differ: {states}")
    expected = comparable(surfaces["cli"])
    if expected["open_gallery"]["shortcut"] != ["Ctrl+Alt+Shift+G"]:
        raise ParityError(f"shortcut: {expected['open_gallery']}")
    if not expected["sort_name"]["checked"] or expected["boards"]["kind"] != "dynamic":
        raise ParityError(f"declared_state: {expected}")

    # Picks: the CLI opens the view and checks the highlight, native MCP sorts by state.
    cli("trigger_plugin_menu", "--plugin-id", PLUGIN, "--menu-id", "main", "--item-id",
        "open_gallery")
    wait_until(lambda: f"pluginView_{PLUGIN}_gallery_summary" in ui_names(), 40,
               "gallery_not_opened")
    picked = command("trigger_plugin_menu", {"plugin_id": PLUGIN, "menu_id": "main",
                                             "item_id": "sort_state"})
    if picked.get("kind") != "event":
        raise ParityError(f"sort_state_trigger: {picked}")
    cli("trigger_plugin_menu", "--plugin-id", PLUGIN, "--menu-id", "main", "--item-id",
        "highlight_offline", "--checked", "true")
    after = entries_by_id(command("get_plugin_menus", {"location": "plugins",
                                                       "plugin_id": PLUGIN}))
    if not after["sort_state"]["checked"] or after["sort_name"]["checked"]:
        raise ParityError(f"radio_group: {after['sort_state']} {after['sort_name']}")
    if not after["highlight_offline"]["checked"]:
        raise ParityError(f"check_mark: {after['highlight_offline']}")
    LOG.info("menus_completed surfaces=%s", sorted(surfaces))
    return {"id": "e17_menus", "passed": True, "entries": expected}
