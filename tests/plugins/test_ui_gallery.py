"""The ui-gallery example under the fake host (plugins/examples/ui-gallery/README.md)."""
import base64
import importlib.util
import json
import os
import pathlib
import shutil
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = ROOT / "plugins/sdk/python"
EXAMPLE = ROOT / "plugins/examples/ui-gallery"


def load_module(name, filename):
    spec = importlib.util.spec_from_file_location(name, SDK / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_testing():
    return load_module("ui_gallery_test_host", "cilogg_plugin_testing.py")


def elements(root):
    """Every element of a view tree by id."""
    found = {}
    pending = [root]
    while pending:
        element = pending.pop()
        if element.get("id"):
            found[element["id"]] = element
        pending.extend(element.get("children", []))
    return found


class UiGallery(unittest.TestCase):
    def setUp(self):
        self.testing = load_testing()
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        package = pathlib.Path(temporary.name) / "ui-gallery"
        shutil.copytree(EXAMPLE, package, ignore=shutil.ignore_patterns("__pycache__"))
        shutil.copy2(SDK / "cilogg_plugin.py", package)
        self.package = package
        self.host = self.testing.FakeHost.start_package(package, timeout=10)
        self.addCleanup(self.host.close)

    def open_gallery(self, window_id="w1"):
        self.host.open_view("gallery", window_id=window_id)
        return elements(self.host.wait_for("view/update", lambda params: "root" in params)["root"])

    def next_patch(self):
        return self.host.wait_for("view/update", lambda params: "patch" in params)["patch"]

    def test_manifest_is_disabled_by_default_and_lists_its_queries(self):
        manifest = json.loads((EXAMPLE / "plugin.json").read_text(encoding="utf-8"))
        self.assertFalse(manifest.get("enabled_by_default", False))
        self.assertEqual(manifest["handles"],
                         ["tooltip/resolve", "menu/populate", "field/complete", "tree/children",
                          "ui/command", "query/cancel"])
        self.assertEqual(manifest["version"], "1.0.0")
        self.assertEqual([(view["id"], view["kind"]) for view in manifest["views"]],
                         [("gallery", "dock"), ("explorer", "dock"), ("layout", "window"),
                          ("ask", "dialog")])
        views = {view["id"]: view for view in manifest["views"]}
        self.assertEqual(views["layout"], {"id": "layout", "title": "UI Gallery Layout",
                                           "kind": "window", "width": 900, "height": 640})
        self.assertEqual(views["ask"], {"id": "ask", "title": "Ask", "kind": "dialog",
                                        "modal": True, "menu": False,
                                        "buttons": ["ok", "cancel", "apply"], "default": "ok"})

    def test_manifest_declares_its_menus(self):
        manifest = json.loads((EXAMPLE / "plugin.json").read_text(encoding="utf-8"))
        menus = {menu["id"]: menu for menu in manifest["menus"]}
        self.assertEqual({menu_id: menu["location"] for menu_id, menu in menus.items()},
                         {"main": "plugins", "lines": "log", "tabs": "tab",
                          "board_actions": "element", "node_actions": "element",
                          "file_actions": "element", "layout_menu": "element"})
        # E17f: the drop-down of the layout window's Log actions button.
        self.assertEqual(menus["layout_menu"]["items"],
                         [{"id": "add_line", "title": "Add Line", "event": True},
                          {"id": "clear_log", "title": "Clear Log", "event": True}])
        # E17e: the context menus of the explorer's tree and file tree.
        self.assertEqual([(entry.get("id", entry.get("type")), entry.get("title"), entry.get("event"))
                          for entry in menus["node_actions"]["items"]],
                         [("reset_node", "Reset Node", True), ("separator", None, None),
                          ("collapse_all", "Collapse All", True)])
        self.assertEqual([(entry["id"], entry["title"], entry["event"])
                          for entry in menus["file_actions"]["items"]],
                         [("show_path", "Show Path", True)])
        # E17d: the context menu of the boards table.
        board_items = menus["board_actions"]["items"]
        self.assertEqual([entry.get("id", entry.get("type")) for entry in board_items],
                         ["restart", "forget", "separator", "restore"])
        self.assertEqual([(entry["title"], entry["event"], entry.get("when"))
                          for entry in board_items if "id" in entry],
                         [("Restart Board", True, {"selection": "any"}),
                          ("Remove from List", True, {"selection": "any"}),
                          ("Restore Removed Boards", True, None)])
        self.assertEqual(menus["main"]["title"], "UI Gallery")
        entries = {entry["id"]: entry for entry in menus["main"]["items"] if "id" in entry}
        self.assertEqual([entry.get("type", "item") for entry in menus["main"]["items"]],
                         ["item", "item", "item", "separator", "item", "section", "item", "item",
                          "separator", "item"])
        self.assertEqual((entries["open_gallery"]["view"], entries["open_gallery"]["shortcut"]),
                         ("gallery", "Ctrl+Alt+Shift+G"))
        self.assertEqual(entries["open_explorer"]["view"], "explorer")
        self.assertEqual(entries["open_layout"]["view"], "layout")
        self.assertNotIn("shortcut", entries["open_layout"])  # Ctrl+Alt+Shift+L is the Apply button's
        self.assertTrue(entries["highlight_offline"]["event"] and entries["highlight_offline"]["checkable"])
        self.assertNotIn("group", entries["highlight_offline"])
        self.assertEqual([(entries[item]["group"], entries[item]["checkable"], entries[item].get("checked", False))
                          for item in ("sort_name", "sort_state")],
                         [("sort", True, True), ("sort", True, False)])
        self.assertTrue(entries["boards"]["dynamic"])
        self.assertEqual(menus["lines"]["items"][0]["when"], {"selection": "lines"})
        self.assertTrue(menus["lines"]["items"][0]["event"])
        self.assertTrue(menus["tabs"]["items"][0]["event"])
        # Every entry has a shape the host accepts: the SDK builders check the same rules.
        sdk = load_module("ui_gallery_test_sdk", "cilogg_plugin.py")
        for menu in manifest["menus"]:
            self.assertEqual(sdk.menu(menu["id"], menu["location"], menu["items"],
                                      title=menu.get("title"), icon=menu.get("icon")), menu)
            for entry in menu["items"]:
                if entry.get("type", "item") == "item":
                    keywords = {key: value for key, value in entry.items() if key not in ("id", "title")}
                    self.assertEqual(sdk.menu_item(entry["id"], entry["title"], **keywords), entry)

    def event_entries(self):
        """(menu_id, entry) for every event entry of plugin.json."""
        manifest = json.loads((EXAMPLE / "plugin.json").read_text(encoding="utf-8"))
        found = []
        for menu in manifest["menus"]:
            pending = list(menu["items"])
            while pending:
                entry = pending.pop(0)
                pending.extend(entry.get("items", []))
                if entry.get("event"):
                    found.append((menu["id"], menu["location"], entry))
        return found

    def test_menus_and_commands_on_a_current_host(self):
        host = self.host
        host.initialize()
        boards = host.populate_menu("main", "boards")
        self.assertEqual(boards["cache_ms"], 2000)
        self.assertEqual([(entry["id"], entry["title"], entry["icon"], entry["event"])
                          for entry in boards["items"]],
                         [("board_b1", "Board 1 (connected)", "status:ok", True),
                          ("board_b2", "Board 2 (idle)", "status:warn", True),
                          ("board_b3", "Board 3 (offline)", "status:error", True)])
        self.assertTrue(all("shortcut" not in entry for entry in boards["items"]))

        # A log command before the view opens: the view shows the count when it opens.
        host.send_command("lines", "count_lines", "log", context={"tab_id": "t1", "view": "main"},
                          selection=self.testing.log_selection([(5, 9), (20, 21)], text="x"))
        tree = self.open_gallery()
        self.assertEqual(tree["status"]["text"],
                         "Data revision 0. 7 selected lines in 2 ranges of the log.")

        host.send_command("main", "highlight_offline", checked=True)
        patch = self.next_patch()
        self.assertEqual([(row["id"], row["tone"]) for row in patch[0]["replace_rows"]],
                         [("b1", "muted"), ("b2", "muted"), ("b3", "error")])
        self.assertEqual(patch[1], {"id": "status", "set": {
            "text": "Data revision 0. Offline boards highlighted."}})

        host.send_command("main", "sort_state", checked=True)
        patch = self.next_patch()
        self.assertEqual([row["id"] for row in patch[0]["replace_rows"]], ["b3", "b2", "b1"])
        self.assertEqual(patch[1]["set"]["text"], "Data revision 0. Sorted by state.")
        self.assertEqual([entry["id"] for entry in host.populate_menu("main", "boards")["items"]],
                         ["board_b3", "board_b2", "board_b1"])

        host.send_command("main", "board_b2")
        self.assertEqual(self.next_patch(), [{"id": "status", "set": {
            "text": "Data revision 0. Board 2 picked from the Boards menu (idle)."}}])
        host.send_command("tabs", "describe_tab", "tab",
                          context={"tab_id": "t7", "file_path": "C:/logs/app.log", "tab_kind": "file"})
        self.assertEqual(self.next_patch()[0]["set"]["text"],
                         "Data revision 0. Tab t7 (file): C:/logs/app.log.")
        host.send_command("tabs", "describe_tab", "tab",
                          context={"tab_id": "t8", "port_name": "COM5", "tab_kind": "serial"})
        self.assertEqual(self.next_patch()[0]["set"]["text"],
                         "Data revision 0. Tab t8 (serial): COM5.")

        # Sorted by state, Change data replaces the rows in their new order and updates the
        # Boards entry of the Plugins menu.
        host.view_event("gallery", "change", "clicked")
        patch = self.next_patch()
        self.assertEqual([row["id"] for row in patch[0]["replace_rows"]], ["b2", "b1", "b3"])
        self.assertEqual(host.wait_for("menu/update"), {
            "menu_id": "main", "items": [{"id": "boards", "set": {"title": "Boards (1 offline)"}}]})

        # A closed gallery gets no patches; it shows the last command when it opens again.
        host.close_view("gallery")
        host.send_command("main", "highlight_offline", origin="shortcut", checked=False)
        host.send_command("lines", "count_lines", "log", context={"view": "filtered"},
                          selection=self.testing.log_selection([(3, 3)]))
        tree = self.open_gallery("w2")
        self.assertEqual(tree["status"]["text"],
                         "Data revision 1. 1 selected line in 1 range of the filtered view.")
        self.assertEqual([row["tone"] for row in tree["boards"]["rows"]], ["error", "warn", "ok"])
        with self.assertRaises(TimeoutError):
            host.wait_for("view/update", lambda params: "patch" in params, timeout=0.3)

        # Every event entry of plugin.json has a handler (an unhandled one is logged on stderr).
        for menu_id, location, entry in self.event_entries():
            host.send_command(menu_id, entry["id"], location,
                              checked=True if entry.get("checkable") else None)
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.errors, [])
        self.assertEqual(host.stderr, "")

    def test_current_host_gets_icons_properties_and_tooltip_answers(self):
        host = self.host
        result = host.initialize()
        self.assertEqual(result["features"],
                         ["field/complete", "menu/populate", "query/cancel", "tooltip/resolve",
                          "tree/children", "ui/command"])
        self.assertEqual(result["sdk"]["version"], "2.0.0")
        tree = self.open_gallery()
        icons = host.wait_for("ui/icons")  # registered when the view first opens
        self.assertTrue(icons["icons"]["chip"]["mask"])
        self.assertIn("<svg", icons["icons"]["chip"]["svg"])
        for element_id in ("intro", "board_name", "hello"):
            self.assertTrue(tree[element_id]["tooltip"], element_id)
        self.assertTrue(tree["summary"]["tooltip_provider"])
        self.assertEqual(tree["summary"]["accessible_name"], "Fleet summary")
        boards = tree["boards"]
        self.assertTrue(boards["tooltip_provider"])
        self.assertEqual([column["tooltip"] for column in boards["columns"]],
                         ["The board name", "The connection state", "Minutes since the last reset"])
        self.assertEqual([row["tooltip_key"] for row in boards["rows"]], ["b1-0", "b2-0", "b3-0"])

        summary = host.resolve_tooltip("gallery", "summary")
        self.assertEqual(summary["title"], "Fleet summary")
        self.assertEqual(summary["icon"], "image:chip")
        self.assertEqual(summary["rows"][2], ["Board 3", "offline"])
        self.assertEqual(summary["tone"], "warn")
        cells = {(row_id, column_id): host.resolve_tooltip("gallery", "boards", row_id=row_id,
                                                           column_id=column_id)
                 for row_id in ("b1", "b2", "b3") for column_id in ("name", "state", "uptime")}
        self.assertEqual(cells[("b1", "name")]["rows"], [["Firmware", "3.2.1"], ["Row", "b1"]])
        self.assertEqual(cells[("b2", "state")],
                         {"title": "Idle", "text": "Board 2 is idle.", "tone": "warn",
                          "icon": "status:warn"})
        self.assertEqual(cells[("b3", "uptime")]["rows"], [["Resets", "7"]])
        self.assertEqual(len({json.dumps(cell, sort_keys=True) for cell in cells.values()}), 9)
        self.assertIsNone(host.resolve_tooltip("gallery", "boards", row_id="nope", column_id="name"))
        self.assertEqual(host.resolve_tooltip("gallery", "status"), {"text": "The data changed 0 times."})
        self.assertIsNone(host.resolve_tooltip("gallery", "intro"))

        host.view_event("gallery", "change", "clicked")
        patch = host.wait_for("view/update", lambda params: "patch" in params)["patch"]
        self.assertEqual([row["tooltip_key"] for row in patch[0]["update_rows"]],
                         ["b1-1", "b2-1", "b3-1"])
        self.assertEqual(patch[1], {"id": "status", "set": {"text": "Data revision 1"}})
        self.assertEqual(host.wait_for("tooltip/invalidate"), {"view_id": "gallery", "element_id": "summary"})
        self.assertEqual(host.wait_for("menu/update")["items"],
                         [{"id": "boards", "set": {"title": "Boards (1 offline)"}}])
        self.assertEqual(host.resolve_tooltip("gallery", "boards", row_id="b1", column_id="state")["title"],
                         "Idle")
        self.assertEqual(host.resolve_tooltip("gallery", "summary")["footer"], "Data revision 1")
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.errors, [])
        self.assertEqual(host.stderr, "")  # handles lists every handled method

    def test_older_host_gets_static_tooltips_only(self):
        host = self.host
        host.initialize_older_host()
        tree = self.open_gallery()
        self.assertIn("newer CILogg", tree["intro"]["text"])
        self.assertEqual(tree["hello"]["tooltip"], "A static tooltip on a button.")
        for element in tree.values():
            for key in ("tooltip_provider", "tooltip_key", "accessible_name", "accessible_description"):
                self.assertNotIn(key, element, element["id"])
        boards = tree["boards"]
        self.assertEqual(boards["columns"][0], {"id": "name", "title": "Board", "width": 120})
        self.assertEqual(boards["rows"][0]["tooltip"], "Board 1 (static row tooltip)")
        self.assertNotIn("tooltip_key", boards["rows"][0])
        host.view_event("gallery", "change", "clicked")
        patch = host.wait_for("view/update", lambda params: "patch" in params)["patch"]
        self.assertNotIn("tooltip_key", patch[0]["update_rows"][0])
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.notifications("ui/icons"), [])
        self.assertEqual(host.notifications("tooltip/invalidate"), [])
        self.assertEqual(host.notifications("menu/update"), [])
        self.assertEqual(host.errors, [])

    def test_choices_group_on_a_current_host(self):
        host = self.host
        host.initialize()
        tree = self.open_gallery()
        self.assertIn("Apply needs", tree["choices_note"]["text"])
        speed = tree["speed"]
        self.assertEqual([item.get("type", "choice") for item in speed["choices"]],
                         ["choice", "separator", "section", "choice", "choice", "choice"])
        self.assertEqual(speed["choices"][0]["icon"], "builtin:refresh")
        self.assertFalse(speed["choices"][-1]["enabled"])
        self.assertEqual(tree["parity"]["indicator"], "radio")
        self.assertEqual((tree["direction"]["style"], tree["direction"]["orientation"]),
                         ("radio", "horizontal"))
        self.assertEqual((tree["channels"]["kind"], tree["channels"]["style"],
                          tree["channels"]["columns"]), ("multichoice", "checkboxes", 2))
        self.assertTrue(tree["firmware"]["searchable"])
        self.assertTrue(tree["target_board"]["editable"] and tree["target_board"]["completion"])
        self.assertEqual(tree["target_board"]["choices"], ["Board 1", "Board 2", "Board 3"])
        self.assertEqual((tree["verbose"]["type"], tree["verbose"]["value"],
                          tree["verbose"]["tristate"]), ("checkbox", None, True))
        self.assertEqual((tree["mode"]["type"], tree["mode"]["value"]), ("radio_group", "auto"))
        self.assertEqual((tree["logs"]["type"], tree["logs"]["value"], tree["logs"]["all_none"]),
                         ("check_group", ["boot"], True))
        kinds = {element_id: tree[element_id]["kind"] for element_id in (
            "notes", "token", "retries", "level", "led_color", "start_date", "start_time",
            "start_at")}
        self.assertEqual(sorted(kinds.values()), ["color", "date", "datetime", "integer",
                                                  "multiline", "secret", "slider", "time"])
        self.assertEqual((tree["notes"]["rows"], tree["notes"]["monospace"]), (3, True))
        self.assertEqual((tree["board_label"]["required"], tree["board_label"]["pattern"]),
                         (True, "[A-Za-z0-9_-]+"))
        self.assertEqual((tree["retries"]["min"], tree["retries"]["max"]), (0, 10))
        self.assertEqual(tree["apply"]["requires_valid"], ["board_label", "retries"])
        self.assertTrue(tree["apply"]["event"])
        self.assertEqual(tree["choices_status"]["text"], "Change a control: its event shows here.")

        # Completion: board names that start with the typed text, any case.
        self.assertEqual(host.complete_field("gallery", "target_board", "board 2"), {"items": [
            {"value": "Board 2", "label": "Board 2 (idle)", "icon": "status:warn"}]})
        self.assertEqual([item["value"] for item in
                          host.complete_field("gallery", "target_board", "Bo")["items"]],
                         ["Board 1", "Board 2", "Board 3"])
        self.assertEqual(host.complete_field("gallery", "target_board", "x"), {"items": []})
        self.assertIsNone(host.complete_field("gallery", "board_label", "x"))

        # The status label shows the last changed event or the Apply click.
        host.change_field("gallery", "board_label", "bad name", valid=False)
        self.assertEqual(self.next_patch(), [{"id": "choices_status", "set": {
            "text": "changed board_label=bad name valid=false"}}])
        for element_id, value, valid, text in (
                ("board_label", "board_7", True, "changed board_label=board_7 valid=true"),
                ("retries", 11, False, "changed retries=11 valid=false"),
                ("verbose", True, None, "changed verbose=true valid=true"),
                ("logs", ["boot", "radio"], None, 'changed logs=["boot", "radio"] valid=true'),
                ("mode", "manual", None, "changed mode=manual valid=true")):
            host.change_field("gallery", element_id, value, valid=valid)
            self.assertEqual(self.next_patch()[0]["set"]["text"], text, element_id)
        host.view_event("gallery", "apply", "clicked",
                        inputs={"board_label": "board_7", "retries": 3})
        self.assertEqual(self.next_patch()[0]["set"]["text"],
                         "clicked apply board_label=board_7 retries=3")
        tree = self.open_gallery("w2")  # a new window shows the last event
        self.assertEqual(tree["choices_status"]["text"], "clicked apply board_label=board_7 retries=3")
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.errors, [])
        self.assertEqual(host.stderr, "")

    def check_choices_on_an_older_host(self):
        """An E17b or E14 host gets the fields it knows: no check box or radio elements, no new
        field kinds, no choice icons, separators or sections, no validation keys."""
        e14_kinds = {"text", "number", "choice", "multichoice", "boolean", "file_open",
                     "file_save", "directory"}
        host = self.host
        tree = self.open_gallery()
        self.assertIn("need a newer CILogg", tree["choices_note"]["text"])
        for element_id, element in tree.items():
            self.assertIn(element["type"], ("column", "row", "group", "label", "field", "button",
                                            "table"), element_id)
            self.assertNotIn("requires_valid", element)
            if element["type"] != "field":
                continue
            self.assertIn(element["kind"], e14_kinds, element_id)
            for key in ("indicator", "style", "searchable", "editable", "completion",
                        "orientation", "columns", "rows", "monospace", "step", "min", "max",
                        "required", "pattern"):
                self.assertNotIn(key, element, element_id)
            for item in element.get("choices", []):
                self.assertTrue(not isinstance(item, dict) or set(item) == {"value", "label"},
                                (element_id, item))
        self.assertEqual(tree["verbose"], {"type": "field", "id": "verbose", "kind": "boolean",
                                           "label": "Verbose logging", "value": False,
                                           "tooltip": "Partly checked: some boards log verbosely."})
        self.assertEqual((tree["mode"]["kind"], tree["mode"]["label"]), ("choice", "Mode"))
        self.assertEqual((tree["logs"]["kind"], tree["logs"]["value"]), ("multichoice", ["boot"]))
        self.assertEqual([tree[element_id]["kind"] for element_id in ("retries", "level", "notes")],
                         ["number", "number", "text"])
        self.assertEqual([item["value"] if isinstance(item, dict) else item
                          for item in tree["speed"]["choices"]], ["auto", 115200, 57600, 9600])
        host.change_field("gallery", "board_label", "x y")  # an older host sends no `valid`
        self.assertEqual(self.next_patch(), [{"id": "choices_status", "set": {
            "text": "changed board_label=x y valid=true"}}])
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.errors, [])

    def test_choices_group_on_an_e17b_host(self):
        self.host.initialize(host_features=self.testing.E17B_HOST_FEATURES,
                             host_feature_versions=self.testing.E17B_HOST_FEATURE_VERSIONS)
        self.check_choices_on_an_older_host()

    def test_choices_group_on_an_e14_host(self):
        self.host.initialize_older_host()
        self.check_choices_on_an_older_host()

    def test_e17a_host_gets_tooltips_but_no_menu_updates(self):
        host = self.host
        host.initialize(host_features=self.testing.E17A_HOST_FEATURES,
                        host_feature_versions=self.testing.E17A_HOST_FEATURE_VERSIONS)
        tree = self.open_gallery()
        self.assertTrue(tree["summary"]["tooltip_provider"])
        self.assertEqual(host.resolve_tooltip("gallery", "summary")["title"], "Fleet summary")
        host.view_event("gallery", "change", "clicked")
        self.assertEqual(self.next_patch()[1], {"id": "status", "set": {"text": "Data revision 1"}})
        self.assertEqual(host.wait_for("tooltip/invalidate"), {"view_id": "gallery", "element_id": "summary"})
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.notifications("menu/update"), [])
        self.assertEqual(host.errors, [])
        self.assertEqual(host.stderr, "")

    def table_status(self, patch):
        """The text a patch sets on the table status label (always its last entry)."""
        self.assertEqual(patch[-1]["id"], "table_status")
        return patch[-1]["set"]["text"]

    def test_rich_table_on_a_current_host(self):
        host = self.host
        host.initialize()
        tree = self.open_gallery()
        boards = tree["boards"]
        self.assertEqual({key: boards[key] for key in ("selection", "export", "reorderable",
                                                       "context_menu")},
                         {"selection": "multi", "export": True, "reorderable": True,
                          "context_menu": "board_actions"})
        self.assertEqual([column["id"] for column in boards["columns"]], ["name", "state", "uptime"])
        self.assertEqual(boards["columns"][0], {"id": "name", "title": "Board", "width": 120,
                                                "tooltip": "The board name", "check": True,
                                                "min_width": 80, "icon": "builtin:device"})
        self.assertEqual(boards["columns"][2]["align"], "right")
        bar_tooltip = "The bar compares the uptime with the longest one."
        self.assertEqual(boards["rows"][0]["cells"], [
            "Board 1", {"text": "connected", "icon": "status:ok"},
            {"text": "12 min", "progress": 12 / 340, "tooltip": bar_tooltip}])
        self.assertEqual(boards["rows"][2]["cells"][2], {"text": "0 min", "progress": 0.0,
                                                         "tone": "muted", "tooltip": bar_tooltip})
        self.assertEqual([row["checked"] for row in boards["rows"]], [False, False, False])
        self.assertEqual(tree["table_status"]["text"],
                         "Double-click, check, drag or right-click a board: the event shows here.")

        # Table events: a selection changes nothing; the others show in the status label.
        host.select_rows("gallery", "boards", ["b1"], current="b1")
        host.activate_cell("gallery", "boards", "b2", "state")
        self.assertEqual(self.next_patch(), [{"id": "table_status",
                                              "set": {"text": "activated b2 state"}}])
        host.check_rows("gallery", "boards", ["b1", "b3"], True)
        self.assertEqual(self.next_patch(), [{"id": "table_status",
                                              "set": {"text": "checked b1,b3 true"}}])
        host.check_rows("gallery", "boards", "b3", False)
        self.assertEqual(self.table_status(self.next_patch()), "checked b3 false")

        # moved: the plugin replaces the rows in the new order and selects the moved rows.
        host.move_rows("gallery", "boards", ["b3"], before="b1")
        patch = self.next_patch()
        self.assertEqual([(row["id"], row["checked"]) for row in patch[0]["replace_rows"]],
                         [("b3", False), ("b1", True), ("b2", False)])
        self.assertEqual(patch[1:], [
            {"id": "boards", "select": ["b3"], "current": "b3", "scroll_to": "b3"},
            {"id": "table_status", "set": {"text": "moved b3 before b1"}}])
        host.move_rows("gallery", "boards", ["b1", "b3"])
        patch = self.next_patch()
        self.assertEqual([row["id"] for row in patch[0]["replace_rows"]], ["b2", "b1", "b3"])
        self.assertEqual(patch[1], {"id": "boards", "select": ["b1", "b3"], "current": "b1",
                                    "scroll_to": "b1"})
        self.assertEqual(self.table_status(patch), "moved b1,b3 before end")
        host.move_rows("gallery", "boards", ["b2", "b1"], before="b1")  # dropped on itself
        patch = self.next_patch()
        self.assertEqual([row["id"] for row in patch[0]["replace_rows"]], ["b2", "b1", "b3"])
        self.assertEqual(self.table_status(patch), "moved b2,b1 before b1")

        # Change data keeps the manual order.
        host.view_event("gallery", "change", "clicked")
        self.assertEqual([row["id"] for row in self.next_patch()[0]["update_rows"]],
                         ["b2", "b1", "b3"])
        self.assertEqual(host.wait_for("menu/update")["items"][0]["set"]["title"],
                         "Boards (1 offline)")
        host.wait_for("tooltip/invalidate")

        # The context menu: Restart Board on the clicked (and selected) offline board.
        host.send_element_command("board_actions", "restart", "gallery", "boards", row_id="b2",
                                  column_id="state", selected_ids=["b2"])
        patch = self.next_patch()
        restarted = {row["id"]: row for row in patch[0]["replace_rows"]}["b2"]
        self.assertEqual(restarted["cells"][1:], [
            {"text": "connected", "icon": "status:ok"},
            {"text": "0 min", "progress": 0.0, "tone": "muted", "tooltip": bar_tooltip}])
        self.assertEqual(self.table_status(patch), "menu restart b2 1")
        self.assertEqual(host.wait_for("menu/update")["items"],
                         [{"id": "boards", "set": {"title": "Boards (0 offline)"}}])
        self.assertEqual(host.wait_for("tooltip/invalidate"),
                         {"view_id": "gallery", "element_id": "summary"})
        self.assertEqual(host.resolve_tooltip("gallery", "boards", row_id="b2",
                                              column_id="uptime")["rows"], [["Resets", "4"]])

        # Remove from List acts on the selected rows; Restore puts them back in their place.
        host.send_element_command("board_actions", "forget", "gallery", "boards", row_id="b1",
                                  column_id="name", selected_ids=["b1", "b3"])
        patch = self.next_patch()
        self.assertEqual([row["id"] for row in patch[0]["replace_rows"]], ["b2"])
        self.assertEqual(self.table_status(patch), "menu forget b1 2")
        self.assertEqual([entry["id"] for entry in host.populate_menu("main", "boards")["items"]],
                         ["board_b2"])
        self.assertIsNone(host.resolve_tooltip("gallery", "boards", row_id="b1", column_id="name"))
        host.send_element_command("board_actions", "restore", "gallery", "boards")
        patch = self.next_patch()
        self.assertEqual([(row["id"], row["checked"]) for row in patch[0]["replace_rows"]],
                         [("b2", False), ("b1", True), ("b3", False)])
        self.assertEqual(self.table_status(patch), "menu restore none 0")
        host.send_element_command("board_actions", "restore", "gallery", "boards")
        self.assertEqual(self.next_patch(), [{"id": "table_status",
                                              "set": {"text": "menu restore none 0"}}])

        # Sorting again replaces the manual order; highlighting makes offline names bold.
        host.send_command("main", "sort_name", checked=True)
        self.assertEqual([row["id"] for row in self.next_patch()[0]["replace_rows"]],
                         ["b1", "b2", "b3"])
        host.send_command("main", "highlight_offline", checked=True)
        rows = self.next_patch()[0]["replace_rows"]
        self.assertEqual([row["cells"][0] for row in rows],
                         ["Board 1", "Board 2", "Board 3"])  # no board is offline now
        tree = self.open_gallery("w2")  # a new window shows the last table event and the checks
        self.assertEqual(tree["table_status"]["text"], "menu restore none 0")
        self.assertEqual([(row["id"], row["checked"]) for row in tree["boards"]["rows"]],
                         [("b1", True), ("b2", False), ("b3", False)])
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.errors, [])
        self.assertEqual(host.stderr, "")

    def test_highlighted_offline_board_has_a_bold_name(self):
        host = self.host
        host.initialize()
        self.open_gallery()
        host.send_command("main", "highlight_offline", checked=True)
        rows = self.next_patch()[0]["replace_rows"]
        self.assertEqual([row["cells"][0] for row in rows],
                         ["Board 1", "Board 2", {"text": "Board 3", "bold": True}])
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.errors, [])

    def check_plain_table(self):
        """An E17c or E14 host gets a table it knows: text cells, no check column, no row
        dragging, export or context menu."""
        host = self.host
        tree = self.open_gallery()
        boards = tree["boards"]
        self.assertIn("need a newer CILogg", tree["table_status"]["text"])
        self.assertEqual(boards["rows"][0]["cells"], ["Board 1", "connected", "12 min"])
        for key in ("export", "reorderable", "context_menu", "header", "copy", "keep_columns",
                    "toggle_selection"):
            self.assertNotIn(key, boards)
        for column in boards["columns"]:
            self.assertTrue(set(column) <= {"id", "title", "width", "tooltip"}, column)
        for board_row in boards["rows"]:
            self.assertTrue(all(isinstance(item, str) for item in board_row["cells"]), board_row)
            for key in ("checked", "icon", "drag"):
                self.assertNotIn(key, board_row)
        host.view_event("gallery", "change", "clicked")
        rows = self.next_patch()[0]["update_rows"]
        self.assertEqual(rows[2]["cells"], ["Board 3", "connected", "5 min"])
        self.assertTrue(all("checked" not in board_row for board_row in rows))
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.errors, [])

    def test_rich_table_on_an_e17c_host(self):
        self.host.initialize(host_features=self.testing.E17C_HOST_FEATURES,
                             host_feature_versions=self.testing.E17C_HOST_FEATURE_VERSIONS)
        self.check_plain_table()

    def test_rich_table_on_an_e14_host(self):
        self.host.initialize_older_host()
        self.check_plain_table()

    # The explorer view (E17e).
    def open_explorer(self, window_id="w1"):
        self.host.open_view("explorer", window_id=window_id)
        return elements(self.host.wait_for("view/update", lambda params: "root" in params
                                           and params["view_id"] == "explorer")["root"])

    def explorer_status(self, patch):
        """The text a patch sets on the explorer's status label (always its last entry)."""
        self.assertEqual(patch[-1]["id"], "explorer_status")
        return patch[-1]["set"]["text"]

    def test_explorer_tree_on_a_current_host(self):
        host = self.host
        host.initialize()
        tree = self.open_explorer()
        self.assertEqual(tree["explorer_note"]["text"],
                         "Boards load when their bench expands; drag nodes to reorder them.")
        self.assertEqual(tree["explorer_status"]["text"], "Expand a bench, check, drag or "
                         "right-click a node, or pick a file: it shows here.")
        benches = tree["benches"]
        self.assertEqual({key: value for key, value in benches.items() if key != "nodes"}, {
            "type": "tree", "id": "benches",
            "columns": [{"id": "name", "title": "Name", "width": 200,
                         "tooltip": "Benches, boards and modules"},
                        {"id": "state", "title": "State", "width": 110}],
            "selection": "multi", "checkboxes": True, "auto_tristate": True, "lazy": True,
            "expand_events": True, "context_menu": "node_actions", "reorderable": True,
            "tooltip_provider": True, "accessible_name": "Benches"})
        self.assertEqual(benches["nodes"], [
            {"id": "bench1", "cells": ["Bench 1", "2 boards"], "icon": "builtin:folder",
             "tooltip_key": "bench1-2", "has_children": True, "checked": False},
            {"id": "bench2", "cells": ["Bench 2", "1 board"], "icon": "builtin:folder",
             "tooltip_key": "bench2-1", "has_children": True, "checked": False}])

        # Lazy children: a bench's boards, each with its modules inline.
        boards = host.request_children("explorer", "benches", "bench1")["nodes"]
        self.assertEqual(boards[0], {
            "id": "board1", "cells": ["Board 1", {"text": "connected", "icon": "status:ok"}],
            "icon": "builtin:device", "tooltip_key": "board1-connected", "checked": False,
            "children": [
                {"id": "board1.modem", "cells": ["Modem", "on"], "icon": "builtin:chip",
                 "tooltip_key": "board1.modem-on", "checked": False},
                {"id": "board1.radio", "cells": ["Radio", "on"], "icon": "builtin:chip",
                 "tooltip_key": "board1.radio-on", "checked": False}]})
        self.assertEqual([board["id"] for board in boards], ["board1", "board2"])
        offline = host.request_children("explorer", "benches", "bench2")["nodes"][0]
        self.assertEqual(offline["children"][1], {
            "id": "board3.radio", "cells": ["Radio", "off"], "icon": "builtin:chip",
            "tone": "muted", "tooltip_key": "board3.radio-off", "checked": False})
        self.assertEqual(host.request_children("explorer", "benches", "board3.radio"),
                         {"nodes": []})
        self.assertEqual(host.request_children("explorer", "benches", "nope"), {"nodes": []})

        # Node tooltips: the query carries node_id.
        self.assertEqual(host.resolve_tooltip("explorer", "benches", node_id="board2"), {
            "title": "Board 2", "text": "Board 2 is idle.",
            "rows": [["Firmware", "3.2.0"], ["Bench", "Bench 1"]], "tone": "warn",
            "icon": "status:warn"})
        self.assertEqual(host.resolve_tooltip("explorer", "benches", node_id="bench2"), {
            "title": "Bench 2", "text": "1 board on this bench.", "rows": [["Boards", "Board 3"]],
            "icon": "builtin:folder"})
        self.assertEqual(host.resolve_tooltip("explorer", "benches", node_id="board3.radio"),
                         {"text": "The radio of Board 3.", "tone": "muted"})
        self.assertIsNone(host.resolve_tooltip("explorer", "benches", node_id="nope"))
        self.assertIsNone(host.resolve_tooltip("explorer", "benches"))

        # Events show in the status label.
        for send, text in (
                (lambda: host.select_nodes("explorer", "benches", ["board1", "board2"],
                                           current="board2"),
                 "selected board1,board2 current board2"),
                (lambda: host.select_nodes("explorer", "benches", []), "selected none current none"),
                (lambda: host.activate_node("explorer", "benches", "board1", "state"),
                 "activated board1 state"),
                (lambda: host.expand_node("explorer", "benches", "bench1"), "expanded bench1"),
                (lambda: host.collapse_node("explorer", "benches", "bench1"), "collapsed bench1"),
                (lambda: host.check_nodes("explorer", "benches", [
                    "bench1", "board1", "board1.modem", "board1.radio", "board2", "board2.modem",
                    "board2.radio"]),
                 "checked bench1,board1,board1.modem,board1.radio,board2,board2.modem,"
                 "board2.radio true"),
                (lambda: host.check_nodes("explorer", "benches", ["board2", "board2.modem",
                                                                  "board2.radio"], False),
                 "checked board2,board2.modem,board2.radio false")):
            send()
            self.assertEqual(self.next_patch(), [{"id": "explorer_status", "set": {"text": text}}])

        # moved: the plugin keeps its own order and patches both benches.
        host.move_nodes("explorer", "benches", ["board2"], parent="bench2", before="board3")
        patch = self.next_patch()
        updated = patch[0]["update_nodes"]
        self.assertEqual([(node["id"], node["cells"][1], node["checked"],
                           [child["id"] for child in node["children"]]) for node in updated],
                         [("bench1", "1 board", True, ["board1"]),
                          ("bench2", "2 boards", False, ["board2", "board3"])])
        self.assertEqual(patch[1:], [
            {"id": "benches", "select": ["board2"], "current": "board2", "scroll_to": "board2"},
            {"id": "explorer_status", "set": {"text": "moved board2 into bench2 before board3"}}])
        self.assertEqual(host.resolve_tooltip("explorer", "benches", node_id="board2")["rows"][1],
                         ["Bench", "Bench 2"])
        host.move_nodes("explorer", "benches", ["board1.modem"], parent="bench1")
        self.assertEqual(self.next_patch(), [{"id": "explorer_status", "set": {
            "text": "moved board1.modem into bench1 before end refused"}}])
        host.move_nodes("explorer", "benches", ["board1.radio"], parent="board1",
                        before="board1.modem")
        patch = self.next_patch()
        self.assertEqual([child["id"] for child in patch[0]["update_nodes"][0]["children"]],
                         ["board1.radio", "board1.modem"])
        host.move_nodes("explorer", "benches", ["bench2"], before="bench1")
        patch = self.next_patch()
        self.assertEqual([(node["id"], "children" in node, "has_children" in node)
                          for node in patch[0]["replace_nodes"]],
                         [("bench2", True, False), ("bench1", True, False)])
        self.assertEqual(self.explorer_status(patch), "moved bench2 into top before bench1")

        # The context menu: the command carries node_id.
        host.send_node_command("node_actions", "reset_node", "explorer", "benches",
                               node_id="board3", selected_ids=["board3"])
        patch = self.next_patch()
        self.assertEqual([(node["id"], node["cells"][1], [child["cells"][1] for child in node["children"]])
                          for node in patch[0]["update_nodes"]],
                         [("board3", {"text": "connected", "icon": "status:ok"}, ["on", "on"])])
        self.assertEqual(self.explorer_status(patch), "menu reset_node board3 1")
        self.assertEqual(host.resolve_tooltip("explorer", "benches", node_id="board3")["tone"], "ok")
        host.send_node_command("node_actions", "reset_node", "explorer", "benches", node_id="bench2")
        patch = self.next_patch()
        self.assertEqual([node["id"] for node in patch[0]["update_nodes"]], ["board2"])
        self.assertEqual(self.explorer_status(patch), "menu reset_node bench2 0")
        host.send_node_command("node_actions", "reset_node", "explorer", "benches", node_id="bench2")
        self.assertEqual(self.next_patch(), [{"id": "explorer_status",
                                              "set": {"text": "menu reset_node bench2 0"}}])
        host.send_node_command("node_actions", "collapse_all", "explorer", "benches")
        self.assertEqual(self.next_patch(), [
            {"id": "benches", "collapse": ["bench2", "bench1", "board2", "board3", "board1"]},
            {"id": "explorer_status", "set": {"text": "menu collapse_all none 0"}}])

        # A new window shows the plugin's order, the check boxes and the last event.
        tree = self.open_explorer("w2")
        nodes = tree["benches"]["nodes"]
        self.assertEqual([(node["id"], node["checked"], [child["id"] for child in node["children"]])
                          for node in nodes],
                         [("bench2", False, ["board2", "board3"]), ("bench1", True, ["board1"])])
        self.assertEqual(tree["explorer_status"]["text"], "menu collapse_all none 0")
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.errors, [])
        self.assertEqual(host.stderr, "")

    def test_explorer_file_tree_on_a_current_host(self):
        host = self.host
        host.initialize()
        tree = self.open_explorer()
        files = tree["files"]
        root = files["roots"][0]
        self.assertEqual({key: value for key, value in files.items() if key != "roots"}, {
            "type": "file_tree", "id": "files", "filters": ["*.py", "*.json", "*.md"],
            "selection": "multi", "context_menu": "file_actions",
            "accessible_name": "Gallery files"})
        self.assertEqual(len(files["roots"]), 1)
        self.assertNotIn("\\", root)  # forward slashes
        self.assertTrue(os.path.samefile(root, self.package))

        host.select_files("explorer", "files", [f"{root}/main.py", f"{root}/README.md"])
        self.assertEqual(self.next_patch(), [{"id": "explorer_status", "set": {
            "text": "selected files main.py,README.md"}}])
        host.activate_file("explorer", "files", f"{root}/plugin.json")
        self.assertEqual(self.explorer_status(self.next_patch()), "activated file plugin.json")
        host.send_node_command("file_actions", "show_path", "explorer", "files",
                               node_id=f"{root}/main.py",
                               selected_ids=[f"{root}/main.py", f"{root}/README.md"])
        self.assertEqual(self.explorer_status(self.next_patch()), f"path {root}/main.py (2 selected)")

        # A closed explorer gets no patches; it shows the selection and last event when it opens.
        host.close_view("explorer")
        host.send_node_command("file_actions", "show_path", "explorer", "files")
        with self.assertRaises(TimeoutError):
            host.wait_for("view/update", lambda params: "patch" in params, timeout=0.3)
        tree = self.open_explorer("w2")
        self.assertEqual(tree["files"]["value"], [f"{root}/main.py", f"{root}/README.md"])
        self.assertEqual(tree["explorer_status"]["text"], "path none (0 selected)")
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.errors, [])
        self.assertEqual(host.stderr, "")

    def check_explorer_on_an_older_host(self):
        """A host without trees skips tree elements: the explorer says so and leaves them out."""
        host = self.host
        tree = self.open_explorer()
        self.assertEqual(set(tree), {"explorer_body", "explorer_note", "explorer_status"})
        self.assertIn("needs a newer CILogg", tree["explorer_note"]["text"])
        host.send_node_command("node_actions", "collapse_all", "explorer", "benches")
        self.assertEqual(self.next_patch(), [{"id": "explorer_status",
                                              "set": {"text": "menu collapse_all none 0"}}])
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.errors, [])

    def test_explorer_on_an_e17d_host(self):
        self.host.initialize(host_features=self.testing.E17D_HOST_FEATURES,
                             host_feature_versions=self.testing.E17D_HOST_FEATURE_VERSIONS)
        self.check_explorer_on_an_older_host()

    def test_explorer_on_an_e14_host(self):
        self.host.initialize_older_host()
        self.check_explorer_on_an_older_host()

    # The layout window and its dialog (E17f).
    def open_layout(self, window_id="w1"):
        self.host.open_view("layout", window_id=window_id)
        return elements(self.host.wait_for("view/update", lambda params: "root" in params
                                           and params["view_id"] == "layout")["root"])

    def layout_status(self, patch):
        """The text a patch sets on the layout status label (always its last entry)."""
        self.assertEqual(patch[-1]["id"], "layout_status")
        return patch[-1]["set"]["text"]

    def layout_command(self, item_id):
        """Pick an entry of the Log actions drop-down: the element is the button."""
        self.host.send_command("layout_menu", item_id, "element", context={
            "window_id": "w1", "view_id": "layout", "element_id": "menu_button"})

    def check_layout_answers(self, sends):
        for send, text in sends:
            send()
            self.assertEqual(self.next_patch(), [{"id": "layout_status", "set": {"text": text}}],
                             text)

    def test_layout_window_on_a_current_host(self):
        host = self.host
        host.initialize()
        tree = self.open_layout()
        picture = host.wait_for("ui/icons")["icons"]["gallery-picture"]
        self.assertTrue(picture["picture"])
        self.assertTrue(base64.b64decode(picture["png"]).startswith(b"\x89PNG\r\n\x1a\n"))
        body = tree["layout_body"]
        self.assertEqual((body["type"], body["modified"], body["close_confirm"]),
                         ("column", False, "The layout window has unsaved changes. Discard them?"))
        self.assertEqual([child.get("id") for child in body["children"]],
                         ["layout_note", "layout_tabs", "layout_buttons", "layout_status"])
        self.assertEqual(tree["layout_status"]["text"],
                         "Switch tabs, use the buttons or Log actions: the result shows here.")
        tabs = tree["layout_tabs"]
        self.assertEqual(tabs["type"], "tabs")
        self.assertNotIn("current", tabs)
        self.assertEqual([(item["type"], item["id"], item["title"], item.get("badge"))
                          for item in tabs["children"]],
                         [("tab", "tab_form", "Form", None), ("tab", "tab_log", "Log", "0"),
                          ("tab", "tab_notes", "Notes", None), ("tab", "tab_charts", "Charts", None)])
        form = tree["layout_form"]
        self.assertEqual((form["type"], [child["id"] for child in form["children"]]),
                         ("form", ["form_name", "form_port", "form_check", "form_grid"]))
        self.assertEqual((tree["form_name"]["kind"], tree["form_name"]["label"],
                          tree["form_port"]["kind"], tree["form_port"]["label"]),
                         ("text", "Name", "choice", "Port"))
        self.assertEqual((tree["form_check"]["type"], tree["form_check"]["label"]),
                         ("checkbox", "Option"))
        self.assertEqual((tree["form_grid"]["type"], tree["form_grid"]["columns"],
                          [child["id"] for child in tree["form_grid"]["children"]]),
                         ("grid", 2, ["grid_a", "grid_b", "grid_c", "grid_d"]))
        self.assertEqual({key: tree["log"][key] for key in ("type", "text", "format", "monospace",
                                                            "follow", "max_lines")},
                         {"type": "text_view", "text": "", "format": "plain", "monospace": True,
                          "follow": True, "max_lines": 1000})
        self.assertEqual(tree["notes"]["format"], "markdown")
        for part in ("# Layout notes", "\n- tabs", "[docs](https://example.com/docs)",
                     "![info](builtin:info)"):
            self.assertIn(part, tree["notes"]["text"])
        self.assertEqual((tree["charts_split"]["type"],
                          [child["id"] for child in tree["charts_split"]["children"]]),
                         ("splitter", ["chart", "charts_side"]))
        chart = tree["chart"]
        self.assertEqual((chart["kind"], [(series["id"], len(series["points"]))
                                          for series in chart["series"]]),
                         ("line", [("rx", 12), ("tx", 12)]))
        self.assertEqual(chart["series"][0]["points"][:3], [[0, 50], [1, 64], [2, 75]])
        self.assertEqual(tree["spark"]["values"], [50, 64, 75, 80, 77, 68, 54, 39, 27, 21, 21, 29])
        self.assertEqual(tree["picture"]["source"], "image:gallery-picture")
        self.assertEqual([child.get("id", child["type"]) for child in tree["layout_buttons"]["children"]],
                         ["tool_button", "link_button", "menu_button", "pin_button", "separator",
                          "busy_button", "edit_button", "ask_button", "spacer", "apply_button"])
        self.assertEqual((tree["tool_button"]["style"], tree["tool_button"]["icon"]),
                         ("tool", "builtin:refresh"))
        self.assertEqual(tree["link_button"]["style"], "link")
        self.assertEqual((tree["menu_button"]["label"], tree["menu_button"]["menu"]),
                         ("Log actions", "layout_menu"))
        self.assertEqual((tree["pin_button"]["checkable"], tree["pin_button"]["checked"]),
                         (True, False))
        self.assertEqual((tree["apply_button"]["default"], tree["apply_button"]["shortcut"]),
                         (True, "Ctrl+Alt+Shift+L"))

        # Every answer shows in layout_status.
        self.check_layout_answers((
            (lambda: host.switch_tab("layout", "layout_tabs", "tab_log", 1), "tab tab_log 1"),
            (lambda: host.toggle_button("layout", "pin_button", True), "pinned true"),
            (lambda: host.click_link("layout", "notes", "https://example.com/docs"),
             "link https://example.com/docs"),
            (lambda: host.view_event("layout", "apply_button", "clicked"), "applied"),
            (lambda: host.view_event("layout", "grid_c", "clicked"), "clicked grid_c")))
        self.layout_command("add_line")
        self.assertEqual(self.next_patch(), [
            {"id": "log", "append": "line 1\n"}, {"id": "tab_log", "set": {"badge": "1"}},
            {"id": "layout_status", "set": {"text": "menu add_line menu_button"}}])
        self.layout_command("add_line")
        self.assertEqual(self.next_patch()[:2], [{"id": "log", "append": "line 2\n"},
                                                 {"id": "tab_log", "set": {"badge": "2"}}])
        self.layout_command("clear_log")
        self.assertEqual(self.next_patch(), [
            {"id": "log", "set": {"text": ""}}, {"id": "tab_log", "set": {"badge": "0"}},
            {"id": "layout_status", "set": {"text": "menu clear_log menu_button"}}])
        self.layout_command("add_line")
        self.assertEqual(self.next_patch()[0], {"id": "log", "append": "line 1\n"})
        host.view_event("layout", "tool_button", "clicked")
        self.assertEqual(self.next_patch(), [
            {"id": "chart", "append_points": {"series": "rx", "points": [[12, 42]]}},
            {"id": "chart", "append_points": {"series": "tx", "points": [[12, 27]]}},
            {"id": "spark", "set": {"values": [50, 64, 75, 80, 77, 68, 54, 39, 27, 21, 21, 29, 42]}},
            {"id": "layout_status", "set": {"text": "point 12"}}])
        host.view_event("layout", "link_button", "clicked")
        self.assertEqual(self.next_patch(), [{"id": "form_name", "focus": True},
                                             {"id": "layout_status", "set": {"text": "focus form_name"}}])
        host.view_event("layout", "edit_button", "clicked")
        self.assertEqual(self.next_patch(), [{"id": "layout_body", "set": {"modified": True}},
                                             {"id": "layout_status", "set": {"text": "modified true"}}])

        # The busy overlay: Cancel ends the simulated work at once.
        host.view_event("layout", "busy_button", "clicked")
        self.assertEqual(self.next_patch(), [
            {"id": "layout_body", "set": {"busy": True, "busy_text": "Simulating work...",
                                          "busy_cancel": True}},
            {"id": "layout_status", "set": {"text": "working"}}])
        host.cancel_busy("layout", "layout_body")
        self.assertEqual(self.next_patch(), [{"id": "layout_body", "set": {"busy": False}},
                                             {"id": "layout_status", "set": {"text": "cancelled"}}])

        # The dialog: a button click and the result it closed with.
        host.view_event("layout", "ask_button", "clicked")
        self.assertEqual(host.wait_for("view/show"), {"view_id": "ask", "window_id": "w1"})
        self.assertEqual(self.layout_status(self.next_patch()), "dialog opened")
        host.open_view("ask")
        ask = elements(host.wait_for("view/update", lambda params: "root" in params
                                     and params["view_id"] == "ask")["root"])
        self.assertEqual((ask["ask_body"]["type"], ask["ask_name"]["kind"], ask["ask_name"]["label"]),
                         ("column", "text", "Name"))
        self.check_layout_answers((
            (lambda: host.press_dialog_button("ask", "apply", inputs={"ask_name": "Ann"}),
             "dialog clicked apply"),
            (lambda: host.close_view("ask", result="ok", inputs={"ask_name": "Ann"}),
             "dialog closed ok Ann"),
            (lambda: host.close_view("ask", result=None, inputs={"ask_name": "Bo"}),
             "dialog closed none Bo"),
            (lambda: host.toggle_button("layout", "pin_button", False), "pinned false")))

        # A new window shows the plugin's state.
        tree = self.open_layout("w2")
        self.assertEqual(tree["layout_tabs"]["current"], "tab_log")
        self.assertEqual((tree["log"]["text"], tree["tab_log"]["badge"]), ("line 1\n", "1"))
        self.assertEqual(tree["pin_button"]["checked"], False)
        self.assertTrue(tree["layout_body"]["modified"])
        self.assertNotIn("busy", tree["layout_body"])
        self.assertEqual(tree["chart"]["series"][1]["points"][-1], [12, 27])
        self.assertEqual(tree["layout_status"]["text"], "pinned false")
        self.assertEqual(len(host.notifications("ui/icons")), 1)  # registered once
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.errors, [])
        self.assertEqual(host.stderr, "")

    def check_layout_on_an_older_host(self, picture):
        """A host without the E17f features gets the layout window as columns, rows and groups
        (the content elements go as they are) and the answers it can show."""
        host = self.host
        tree = self.open_layout()
        self.assertIn("need a newer CILogg", tree["layout_note"]["text"])
        for element in tree.values():
            self.assertNotIn(element["type"], ("tabs", "tab", "splitter", "form", "grid",
                                               "separator", "spacer"), element["id"])
            for key in ("stretch", "min_height", "modified", "close_confirm", "style", "menu",
                        "checkable", "checked", "shortcut", "default", "icon", "badge"):
                self.assertNotIn(key, element, element["id"])
        self.assertEqual([(child["type"], child["id"], child["title"])
                          for child in tree["layout_tabs"]["children"]],
                         [("group", "tab_form", "Form"), ("group", "tab_log", "Log"),
                          ("group", "tab_notes", "Notes"), ("group", "tab_charts", "Charts")])
        self.assertEqual(tree["layout_tabs"]["type"], "column")
        form_rows = tree["layout_form"]["children"]
        self.assertEqual([row_element["type"] for row_element in form_rows], ["row"] * 4)
        self.assertEqual(form_rows[2]["children"][0], {"type": "label", "text": "Option"})
        self.assertEqual(form_rows[3]["children"][0], {"type": "label", "text": "Buttons"})
        self.assertEqual([len(item["children"]) for item in tree["form_grid"]["children"]], [2, 2])
        self.assertEqual(tree["charts_split"]["children"][0]["type"], "row")
        self.assertEqual((tree["chart"]["type"], tree["log"]["type"]), ("chart", "text_view"))
        self.assertEqual(tree["picture"]["source"], picture)
        self.layout_command("add_line")
        self.assertEqual(self.next_patch(), [
            {"id": "tab_log", "set": {}},  # the group knows no badge
            {"id": "layout_status", "set": {"text": "menu add_line menu_button"}}])
        self.check_layout_answers((
            (lambda: host.view_event("layout", "tool_button", "clicked"), "point 12"),
            (lambda: host.view_event("layout", "link_button", "clicked"), "focus form_name"),
            (lambda: host.view_event("layout", "pin_button", "clicked"), "pinned true"),
            (lambda: host.switch_tab("layout", "layout_tabs", "nope", 7), "tab nope 7"),
            (lambda: host.close_view("ask"), "dialog closed none none")))
        tree = self.open_layout("w2")
        self.assertEqual(tree["log"]["text"], "line 1\n")
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.errors, [])
        self.assertEqual(host.stderr, "")

    def test_layout_window_on_an_e17e_host(self):
        self.host.initialize(host_features=self.testing.E17E_HOST_FEATURES,
                             host_feature_versions=self.testing.E17E_HOST_FEATURE_VERSIONS)
        self.check_layout_on_an_older_host("image:gallery-picture")

    def test_layout_window_on_an_e14_host(self):
        self.host.initialize_older_host()
        self.check_layout_on_an_older_host("builtin:info")


if __name__ == "__main__":
    unittest.main()
