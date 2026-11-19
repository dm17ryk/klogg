"""The ui-gallery example under the fake host (plugins/examples/ui-gallery/README.md)."""
import importlib.util
import json
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
                         ["tooltip/resolve", "menu/populate", "field/complete", "ui/command",
                          "query/cancel"])
        self.assertEqual(manifest["version"], "1.0.0")
        self.assertEqual([(view["id"], view["kind"]) for view in manifest["views"]], [("gallery", "dock")])

    def test_manifest_declares_its_menus(self):
        manifest = json.loads((EXAMPLE / "plugin.json").read_text(encoding="utf-8"))
        menus = {menu["id"]: menu for menu in manifest["menus"]}
        self.assertEqual({menu_id: menu["location"] for menu_id, menu in menus.items()},
                         {"main": "plugins", "lines": "log", "tabs": "tab"})
        self.assertEqual(menus["main"]["title"], "UI Gallery")
        entries = {entry["id"]: entry for entry in menus["main"]["items"] if "id" in entry}
        self.assertEqual([entry.get("type", "item") for entry in menus["main"]["items"]],
                         ["item", "separator", "item", "section", "item", "item", "separator", "item"])
        self.assertEqual((entries["open_gallery"]["view"], entries["open_gallery"]["shortcut"]),
                         ("gallery", "Ctrl+Alt+Shift+G"))
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
                          "ui/command"])
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


if __name__ == "__main__":
    unittest.main()
