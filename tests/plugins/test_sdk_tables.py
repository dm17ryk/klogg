"""Python SDK rich tables and element menus (E17d): cell objects, the new row, column and table
keys, the selection patch, their downgrades on older hosts, the table events, element menu
commands and the fake host's helpers (docs/plugin-sdk-advanced.md, sections 7.2 and 10)."""
import copy
import importlib.util
import json
import pathlib
import queue
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = ROOT / "plugins/sdk/python"
HANDLES = ["ui/command", "menu/populate", "query/cancel"]
E17D_FEATURES = ("rich_tables", "element_menus")


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, SDK / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def walk(element):
    """Every element of a view tree."""
    pending = [element]
    while pending:
        current = pending.pop()
        yield current
        pending.extend(current.get("children", []))


def by_id(root):
    return {element["id"]: element for element in walk(root) if "id" in element}


class TableCase(unittest.TestCase):
    def setUp(self):
        self.sdk = load("tables_test_sdk", "cilogg_plugin.py")
        self.testing = load("tables_test_host", "cilogg_plugin_testing.py")

    def plugin(self, handles=HANDLES):
        manifest = {"id": "test", "capabilities": ["tool"]}
        if handles is not None:
            manifest["handles"] = list(handles)
        return self.sdk.Plugin(manifest)

    def host(self, plugin, older=False, **initialize):
        host = self.testing.FakeHost.in_process(plugin, timeout=5)
        self.addCleanup(host.close)
        if older:
            host.initialize_older_host(**initialize)
        else:
            host.initialize(**initialize)
        return host

    def without(self, *features):
        """initialize() keywords for a current host that lacks `features`."""
        return dict(host_features=[name for name in self.testing.HOST_FEATURES if name not in features],
                    host_feature_versions={name: version for name, version
                                           in self.testing.HOST_FEATURE_VERSIONS.items()
                                           if name not in features})

    def sync(self, host):
        """Return once the plugin has read every frame sent so far (it refuses this request)."""
        with self.assertRaises(self.testing.RpcError):
            host.request("sync/point")


class Builders(TableCase):
    def test_cell(self):
        sdk = self.sdk
        self.assertEqual(sdk.cell("Board 7"), {"text": "Board 7"})
        self.assertEqual(sdk.cell(42), {"text": "42"})
        self.assertEqual(
            sdk.cell("Writing", icon="builtin:device", tooltip="Bench 2", tooltip_key="k7",
                     tone="accent", bold=True, progress=0.42, sort=7),
            {"text": "Writing", "icon": "builtin:device", "tooltip": "Bench 2",
             "tooltip_key": "k7", "tone": "accent", "bold": True, "progress": 0.42, "sort": 7})
        self.assertEqual(sdk.cell("x", progress=0, sort="b", bold=False),
                         {"text": "x", "progress": 0, "sort": "b", "bold": False})
        self.assertEqual(sdk.cell("x", progress=1)["progress"], 1)
        self.assertEqual(sdk.CELL_TONES, ("normal", "ok", "warn", "error", "muted", "accent"))
        for keywords in (dict(tone="red"), dict(progress=-0.1), dict(progress=1.01),
                         dict(progress=True), dict(progress="0.5"), dict(progress=float("nan")),
                         dict(sort=True), dict(sort=[1]), dict(sort={"a": 1})):
            with self.subTest(keywords=keywords), self.assertRaises(ValueError):
                sdk.cell("x", **keywords)

    def test_table_column_keeps_its_signature_and_takes_the_new_keys(self):
        sdk = self.sdk
        self.assertEqual(sdk.table_column("a", "A", 80, True, "header", True, "k"),
                         {"id": "a", "title": "A", "width": 80, "editable": True,
                          "tooltip": "header", "tooltip_provider": True, "tooltip_key": "k"})
        self.assertEqual(
            sdk.table_column("name", "Board", align="center", hidden=True, sortable=False,
                             sort_as="natural", min_width=0, check=True, icon="builtin:device"),
            {"id": "name", "title": "Board", "align": "center", "hidden": True,
             "sortable": False, "sort_as": "natural", "min_width": 0, "check": True,
             "icon": "builtin:device"})
        self.assertEqual(sdk.COLUMN_ALIGNMENTS, ("left", "center", "right"))
        self.assertEqual(sdk.SORT_AS, ("text", "number", "natural"))
        for keywords in (dict(align="middle"), dict(sort_as="date"), dict(min_width=-1),
                         dict(min_width=True), dict(min_width=1.5)):
            with self.subTest(keywords=keywords), self.assertRaises(ValueError):
                sdk.table_column("a", **keywords)

    def test_table_row_keeps_cell_objects_and_takes_the_new_keys(self):
        sdk = self.sdk
        # The earlier signature: every cell becomes text.
        self.assertEqual(sdk.table_row("r1", [1, "x", None], "warn", "t", None, True, "k"),
                         {"id": "r1", "cells": ["1", "x", "None"], "tone": "warn",
                          "tooltip": "t", "tooltip_provider": True, "tooltip_key": "k"})
        state = sdk.cell("Writing", tone="accent", progress=0.5)
        drag = {"path": "C:/logs/a.log"}
        row = sdk.table_row("r7", [sdk.cell("Board 7", icon="builtin:device"), state, 3],
                            icon="status:ok", checked=False, drag=drag)
        self.assertEqual(row, {"id": "r7", "cells": [
            {"text": "Board 7", "icon": "builtin:device"},
            {"text": "Writing", "tone": "accent", "progress": 0.5}, "3"],
            "icon": "status:ok", "checked": False, "drag": {"path": "C:/logs/a.log"}})
        self.assertIsNot(row["cells"][1], state)  # cell objects and drag are copied
        self.assertIsNot(row["drag"], drag)
        self.assertEqual(sdk.table_row("r", ["a"], drag=True)["drag"], True)
        # A mapping is a cell object when it has a cell key; any other mapping is text, as
        # before rich tables.
        self.assertEqual(sdk.table_row("r", [{"status": "ok"}, {"icon": "status:ok"}, {}])["cells"],
                         ["{'status': 'ok'}", {"icon": "status:ok"}, "{}"])

    def test_table_keeps_its_signature_and_takes_the_new_keys(self):
        sdk = self.sdk
        columns = [sdk.table_column("a", "A")]
        self.assertEqual(sdk.table("t", columns, [], True, False, "single", tooltip="x"),
                         {"type": "table", "id": "t", "columns": [{"id": "a", "title": "A"}],
                          "rows": [], "sortable": True, "filter_box": False,
                          "selection": "single", "tooltip": "x"})
        self.assertEqual(
            sdk.table("t", columns, selection="multi", header=False, toggle_selection=True,
                      context_menu="rows", copy=False, export=True, reorderable=True,
                      keep_columns=False),
            {"type": "table", "id": "t", "columns": [{"id": "a", "title": "A"}],
             "selection": "multi", "header": False, "toggle_selection": True,
             "context_menu": "rows", "copy": False, "export": True, "reorderable": True,
             "keep_columns": False})
        self.assertNotIn("toggle_selection", sdk.table("t", columns, selection="single",
                                                       toggle_selection=None))
        self.assertFalse(sdk.table("t", columns, toggle_selection=False)["toggle_selection"])
        for keywords in (dict(toggle_selection=True), dict(selection="single", toggle_selection=True),
                         dict(context_menu=""), dict(context_menu="a/b"), dict(context_menu=3)):
            with self.subTest(keywords=keywords), self.assertRaises(ValueError):
                sdk.table("t", columns, **keywords)

    def test_select_rows(self):
        sdk = self.sdk
        self.assertEqual(sdk.select_rows("t", ["r1", "r2"], current="r1", scroll_to="r2"),
                         {"id": "t", "select": ["r1", "r2"], "current": "r1", "scroll_to": "r2"})
        self.assertEqual(sdk.select_rows("t", []), {"id": "t", "select": []})
        self.assertEqual(sdk.select_rows("t", ("r3",)), {"id": "t", "select": ["r3"]})
        self.assertEqual(sdk.select_rows("t", current="r1"), {"id": "t", "current": "r1"})
        self.assertEqual(sdk.select_rows("t", scroll_to="r9"), {"id": "t", "scroll_to": "r9"})
        for arguments in ((), ("r1",)):
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                sdk.select_rows("t", *arguments)

    def test_element_menus(self):
        sdk = self.sdk
        self.assertIn("element", sdk.MENU_LOCATIONS)
        items = [sdk.menu_item("forget", "Remove from List", event=True,
                               when={"selection": "any"}),
                 sdk.menu_separator(), sdk.menu_item("info", "Info", view="details")]
        self.assertEqual(sdk.menu("rows", "element", items),
                         {"id": "rows", "location": "element", "items": items})


class Downgrades(TableCase):
    """update() and patch() send what the host knows (docs/plugin-sdk-advanced.md, section 10)."""

    def tree(self):
        sdk = self.sdk
        rows = [
            sdk.table_row("r1", ["Board 1", sdk.cell("ok", icon="status:ok", tone="ok"),
                                 sdk.cell("12 min", progress=0.5, sort=12, tooltip="t")],
                          tone="warn", tooltip="row", icon="builtin:device", checked=True,
                          drag=True, tooltip_key="k1"),
            sdk.table_row("r2", [sdk.cell("Board 2", bold=True), "idle", "3 min"],
                          editors={"name": sdk.editor("choice", choices=[
                              sdk.choice("a", "A", icon="status:ok"), "b"])}),
        ]
        columns = [
            sdk.table_column("name", "Board", 120, editable=True, tooltip="name", check=True,
                             min_width=80, icon="builtin:device", tooltip_key="c"),
            sdk.table_column("state", "State", align="center", hidden=False, sortable=False,
                             sort_as="text"),
            sdk.table_column("uptime", "Uptime", align="right", sort_as="number"),
        ]
        return sdk.column(
            sdk.table("t", columns, rows, sortable=True, selection="multi", header=False,
                      toggle_selection=True, context_menu="rows", copy=False, export=True,
                      reorderable=True, keep_columns=False, tooltip_provider=True),
            sdk.label("copy_note", "Labels keep their keys", tone="ok"),
            id="body")

    def sent(self, **initialize):
        plugin = self.plugin(handles=None)
        older = initialize.pop("older", False)
        host = self.host(plugin, older=older, **initialize)
        tree = self.tree()
        original = copy.deepcopy(tree)
        plugin.update("main", tree, "w1")
        root = host.wait_for("view/update")["root"]
        self.assertEqual(tree, original)  # the caller's tree is not changed
        return plugin, host, by_id(root)

    def test_current_host_gets_everything(self):
        plugin = self.plugin(handles=None)
        host = self.host(plugin)
        for feature in E17D_FEATURES:
            self.assertTrue(plugin.supports(feature), feature)
        tree = self.tree()
        plugin.update("main", tree)
        self.assertEqual(host.wait_for("view/update")["root"], tree)
        entries = [self.sdk.select_rows("t", ["r2"], current="r2", scroll_to="r2"),
                   self.sdk.update_rows("t", [self.sdk.table_row("r1", [self.sdk.cell("x", bold=True)])])]
        plugin.patch("main", entries)
        self.assertEqual(host.wait_for("view/update")["patch"], entries)

    def test_host_without_rich_tables(self):
        _plugin, _host, elements = self.sent(**self.without("rich_tables"))
        table = elements["t"]
        self.assertEqual({key: value for key, value in table.items() if key not in ("columns", "rows")},
                         {"type": "table", "id": "t", "sortable": True, "selection": "multi",
                          "context_menu": "rows", "tooltip_provider": True})
        self.assertEqual(table["columns"], [
            {"id": "name", "title": "Board", "width": 120, "editable": True, "tooltip": "name",
             "tooltip_key": "c"},
            {"id": "state", "title": "State"}, {"id": "uptime", "title": "Uptime"}])
        self.assertEqual(table["rows"][0], {"id": "r1", "cells": ["Board 1", "ok", "12 min"],
                                            "tone": "warn", "tooltip": "row", "tooltip_key": "k1"})
        self.assertEqual(table["rows"][1]["cells"], ["Board 2", "idle", "3 min"])
        # Other features stay: the cell editor keeps its choice icons.
        self.assertEqual(table["rows"][1]["editors"]["name"]["choices"][0]["icon"], "status:ok")
        self.assertEqual(elements["copy_note"]["tone"], "ok")

    def test_host_without_element_menus(self):
        _plugin, _host, elements = self.sent(**self.without("element_menus"))
        table = elements["t"]
        self.assertNotIn("context_menu", table)
        expected = by_id(self.tree())["t"]
        del expected["context_menu"]
        self.assertEqual(table, expected)

    def assert_plain_table(self, table, tooltips):
        self.assertEqual(set(table), {"type", "id", "columns", "rows", "sortable", "selection"}
                         | ({"tooltip_provider"} if tooltips else set()))
        for column in table["columns"]:
            self.assertTrue(set(column) <= {"id", "title", "width", "editable", "tooltip",
                                            "tooltip_key"}, column)
        for row in table["rows"]:
            self.assertTrue(set(row) <= {"id", "cells", "tone", "tooltip", "editors",
                                         "tooltip_key"}, row)
            self.assertTrue(all(isinstance(item, str) for item in row["cells"]), row)

    def test_e17c_and_e14_hosts_get_plain_tables(self):
        for name, tooltips, initialize in (
                ("E17c", True, dict(host_features=self.testing.E17C_HOST_FEATURES,
                                    host_feature_versions=self.testing.E17C_HOST_FEATURE_VERSIONS)),
                ("E14", False, dict(older=True))):
            with self.subTest(host=name):
                plugin, _host, elements = self.sent(**initialize)
                for feature in E17D_FEATURES:
                    self.assertFalse(plugin.supports(feature), feature)
                self.assert_plain_table(elements["t"], tooltips)
                self.assertEqual(elements["t"]["rows"][0]["cells"], ["Board 1", "ok", "12 min"])

    def test_cell_objects_without_text(self):
        sdk = self.sdk
        plugin = self.plugin(handles=None)
        host = self.host(plugin, **self.without("rich_tables"))
        cells = [{"icon": "status:ok"}, {"text": None}, {"text": 12}, {"text": 1.5},
                 {"text": True}, {"text": "naïve"}, "plain", 7]
        plugin.update("main", sdk.table("t", [sdk.table_column("a")], [{"id": "r", "cells": cells}]))
        self.assertEqual(host.wait_for("view/update")["root"]["rows"][0]["cells"],
                         ["", "", "12", "1.5", "true", "naïve", "plain", 7])

    def test_patches_on_a_host_without_rich_tables(self):
        sdk = self.sdk
        plugin = self.plugin(handles=None)
        host = self.host(plugin, host_features=self.testing.E17C_HOST_FEATURES,
                         host_feature_versions=self.testing.E17C_HOST_FEATURE_VERSIONS)
        plugin.update("main", self.tree())
        host.wait_for("view/update")
        rich_row = sdk.table_row("r3", [sdk.cell("Board 3", bold=True), "x"], checked=True,
                                 icon="status:ok")
        changes = [
            sdk.replace_rows("t", [rich_row]),
            sdk.update_rows("t", [rich_row]),
            sdk.insert_rows("t", [rich_row], before="r1"),
            sdk.select_rows("t", ["r3"], current="r3"),
            {"id": "t", "set": {"export": False, "copy": True}, "select": ["r1"]},
            sdk.set_props("t", context_menu="other", header=True, reorderable=False,
                          rows=[rich_row], columns=[sdk.table_column("a", align="left", check=True)],
                          selection="single"),
            sdk.set_props("unknown", header=False, keep_columns=True, toggle_selection=True,
                          context_menu="x", text="kept"),
            sdk.set_props("copy_note", text="still a label"),
        ]
        original = copy.deepcopy(changes)
        plugin.patch("main", changes)
        patch = host.wait_for("view/update")["patch"]
        self.assertEqual(changes, original)
        plain_row = {"id": "r3", "cells": ["Board 3", "x"]}
        self.assertEqual(patch, [
            {"id": "t", "replace_rows": [plain_row]},
            {"id": "t", "update_rows": [plain_row]},
            {"id": "t", "insert_rows": [plain_row], "before": "r1"},
            {"id": "t", "set": {}},
            {"id": "t", "set": {"rows": [plain_row], "columns": [{"id": "a", "title": "a"}],
                                "selection": "single"}},
            {"id": "unknown", "set": {"text": "kept"}},
            {"id": "copy_note", "set": {"text": "still a label"}},
        ])

    def test_a_patch_of_only_selection_entries_sends_nothing_to_an_older_host(self):
        sdk = self.sdk
        plugin = self.plugin(handles=None)
        host = self.host(plugin, **self.without("rich_tables"))
        plugin.update("main", self.tree())
        host.wait_for("view/update")
        plugin.patch("main", [sdk.select_rows("t", ["r1"]), sdk.select_rows("t", scroll_to="r2")])
        plugin.patch("main", [])  # an empty patch goes out as before
        self.assertEqual(host.wait_for("view/update"), {"view_id": "main", "patch": []})
        self.sync(host)
        self.assertEqual(len(host.notifications("view/update")), 2)

    def test_patches_on_a_host_without_element_menus(self):
        sdk = self.sdk
        plugin = self.plugin(handles=None)
        host = self.host(plugin, **self.without("element_menus"))
        plugin.update("main", self.tree())
        host.wait_for("view/update")
        entries = [sdk.set_props("t", context_menu="other", export=True),
                   sdk.select_rows("t", ["r1"])]
        plugin.patch("main", entries)
        self.assertEqual(host.wait_for("view/update")["patch"],
                         [{"id": "t", "set": {"export": True}}, {"id": "t", "select": ["r1"]}])

    def test_the_selection_patch_carries_only_the_ids_cilogg_uses(self):
        sdk = self.sdk
        long_ids = [f"{index:04}" + "x" * 4092 for index in range(1000)]  # ~4 MiB of ids
        entry = sdk.select_rows("t", long_ids)
        self.assertEqual(entry["select"], sdk.bounded_ids(long_ids))
        self.assertLess(len(entry["select"]), 100)
        self.assertLessEqual(len(json.dumps(entry).encode("utf-8")), sdk.MAX_ID_BYTES + 100)
        self.assertEqual(len(sdk.select_rows("t", [str(n) for n in range(1500)])["select"]),
                         sdk.MAX_TABLE_IDS)
        self.assertEqual(sdk.select_rows("t", ["b", "a"])["select"], ["b", "a"])
        self.assertEqual(self.testing.MAX_ID_BYTES, sdk.MAX_ID_BYTES)
        # The patch reaches the host whole: the 1 MiB message limit is never hit.
        plugin = self.plugin(handles=None)
        host = self.host(plugin)
        plugin.update("main", self.tree())
        host.wait_for("view/update")
        plugin.patch("main", [entry])
        self.assertEqual(host.wait_for("view/update")["patch"][0]["select"], entry["select"])


class Events(TableCase):
    def received(self, handles=HANDLES):
        events = queue.Queue()
        plugin = self.plugin(handles=handles)
        plugin.on_view_event(events.put)
        return self.host(plugin), events

    def test_table_events_from_the_fake_host(self):
        host, events = self.received()
        host.activate_cell("main", "t", "r2", "state")
        host.check_rows("main", "t", ["r1", "r2", "r3"], True, row_id="r3")
        host.check_rows("main", "t", "r4", False, window_id="w2")
        host.move_rows("main", "t", ["r2", "r3"], before="r1")
        host.move_rows("main", "t", ("r1",))
        received = [events.get(timeout=2) for _ in range(5)]
        self.assertEqual([(event["type"], event["value"]) for event in received], [
            ("activated", {"row_id": "r2", "column_id": "state"}),
            ("checked", {"row_id": "r3", "row_ids": ["r1", "r2", "r3"], "checked": True}),
            ("checked", {"row_id": "r4", "row_ids": ["r4"], "checked": False}),
            ("moved", {"row_ids": ["r2", "r3"], "before": "r1"}),
            ("moved", {"row_ids": ["r1"], "before": None}),
        ])
        self.assertEqual(received[0], {"view_id": "main", "window_id": "w1", "element_id": "t",
                                       "type": "activated",
                                       "value": {"row_id": "r2", "column_id": "state"},
                                       "inputs": {}})
        self.assertEqual(received[2]["window_id"], "w2")
        self.assertEqual(self.sdk.TABLE_EVENTS, ("activated", "checked", "moved"))

    def test_the_fake_host_sends_activated_only_to_plugins_with_handles(self):
        host, events = self.received(handles=None)
        self.assertFalse(host.activate_cell("main", "t", "r2", "state"))
        host.check_rows("main", "t", "r1")
        self.assertEqual(events.get(timeout=2)["type"], "checked")  # nothing came before it

    def test_the_fake_host_bounds_ids_by_their_size_as_cilogg_does(self):
        testing = self.testing
        long_ids = [f"{index:04}" + "x" * 1096 for index in range(1000)]  # ~1.1 MB of ids
        sent = testing.bounded_ids(long_ids)
        self.assertLess(len(sent), len(long_ids))
        self.assertEqual(sent, long_ids[:len(sent)])
        self.assertLessEqual(len(json.dumps(sent).encode("utf-8")), testing.MAX_ID_BYTES)
        self.assertEqual(testing.bounded_ids(["a", "b"]), ["a", "b"])
        self.assertEqual(len(testing.bounded_ids([str(n) for n in range(1500)])),
                         testing.MAX_TABLE_IDS)
        # Every helper stays below the 1 MiB frame limit, so the plugin keeps running.
        host, events = self.received()
        host.select_rows("main", "t", long_ids)
        host.check_rows("main", "t", long_ids)
        host.move_rows("main", "t", long_ids)
        selected, checked, moved = (events.get(timeout=5) for _ in range(3))
        self.assertEqual(selected["value"], sent)
        self.assertTrue(selected["truncated"])
        self.assertEqual(selected["selected_count"], 1000)
        self.assertEqual(checked["value"]["row_ids"], sent)
        self.assertEqual(moved["value"]["row_ids"], sent)
        self.assertEqual(testing.element_context("main", "t", selected_ids=long_ids)["selected_ids"],
                         sent)
        self.assertEqual(host.errors, [])
        for call in (lambda: host.check_rows("main", "t", []), lambda: host.move_rows("main", "t", [])):
            with self.assertRaises(ValueError):
                call()

    def test_selected_carries_current_for_plugins_with_handles(self):
        host, events = self.received()
        host.select_rows("main", "t", ["r1", "r2"], current="r2")
        host.select_rows("main", "t", [])
        host.select_rows("main", "t", [f"r{n}" for n in range(1005)], current="r0")
        host.select_rows("main", "t", ["r1"], selected_count=3)
        first, empty, many, partial = (events.get(timeout=2) for _ in range(4))
        self.assertEqual(first, {"view_id": "main", "window_id": "w1", "element_id": "t",
                                 "type": "selected", "value": ["r1", "r2"], "inputs": {},
                                 "selected_count": 2, "current": "r2"})
        self.assertEqual((empty["value"], empty["selected_count"], empty["current"]), ([], 0, None))
        self.assertNotIn("truncated", empty)
        self.assertEqual((len(many["value"]), many["selected_count"], many["truncated"]),
                         (1000, 1005, True))
        self.assertEqual((partial["selected_count"], partial["truncated"]), (3, True))

    def test_selected_has_no_current_for_plugins_without_handles(self):
        host, events = self.received(handles=None)
        host.select_rows("main", "t", ["r1"], current="r1")
        self.assertEqual(events.get(timeout=2),
                         {"view_id": "main", "window_id": "w1", "element_id": "t",
                          "type": "selected", "value": ["r1"], "inputs": {},
                          "selected_count": 1})


class ElementMenus(TableCase):
    def test_element_context(self):
        element_context = self.testing.element_context
        self.assertEqual(element_context("main", "t", row_id="r2", column_id="state",
                                         selected_ids=["r1", "r2"], window_id="w2"),
                         {"window_id": "w2", "view_id": "main", "element_id": "t", "row_id": "r2",
                          "column_id": "state", "selected_ids": ["r1", "r2"], "selected_count": 2})
        self.assertEqual(element_context("main", "t"),
                         {"window_id": "w1", "view_id": "main", "element_id": "t", "row_id": None,
                          "column_id": None, "selected_ids": [], "selected_count": 0})
        many = element_context("main", "t", selected_ids=[f"r{n}" for n in range(1200)])
        self.assertEqual((len(many["selected_ids"]), many["selected_count"]), (1000, 1200))
        with self.assertRaises(ValueError):
            element_context("main", "t", selected_ids="r1")

    def test_element_command(self):
        received = queue.Queue()
        plugin = self.plugin()
        plugin.on_command("rows")(received.put)
        host = self.host(plugin)
        host.send_element_command("rows", "forget", "main", "t", row_id="r2", column_id="name",
                                  selected_ids=["r1", "r2"])
        host.send_element_command("rows", "restore", "main", "t", window_id="w2")
        forget, restore = received.get(timeout=2), received.get(timeout=2)
        sent = [frame.message["params"] for frame in host.frames
                if frame.direction == self.testing.TO_PLUGIN and frame.method == "ui/command"]
        self.assertEqual(sent[0], {"origin": "menu", "menu_id": "rows", "item_id": "forget",
                                   "location": "element", "window_id": "w1", "context": {
                                       "window_id": "w1", "view_id": "main", "element_id": "t",
                                       "row_id": "r2", "column_id": "name",
                                       "selected_ids": ["r1", "r2"], "selected_count": 2}})
        self.assertEqual((forget.location, forget.view_id, forget.element_id, forget.row_id,
                          forget.column_id, forget.selected_ids, forget.selected_count),
                         ("element", "main", "t", "r2", "name", ["r1", "r2"], 2))
        self.assertEqual((restore.window_id, restore.row_id, restore.column_id,
                          restore.selected_ids, restore.selected_count),
                         ("w2", None, None, [], 0))

    def test_other_and_odd_contexts(self):
        received = queue.Queue()
        plugin = self.plugin()
        plugin.on_command()(received.put)
        host = self.host(plugin)
        host.send_command("main", "plain")
        host.send_command("rows", "odd", "element", context={
            "view_id": 3, "row_id": ["r1"], "selected_ids": ["r1", 2, None, "r3"],
            "selected_count": True})
        host.send_command("rows", "counted", "element",
                          context={"selected_ids": "r1", "selected_count": 1500})
        plain, odd, counted = (received.get(timeout=2) for _ in range(3))
        self.assertEqual((plain.view_id, plain.element_id, plain.row_id, plain.column_id,
                          plain.selected_ids, plain.selected_count),
                         (None, None, None, None, [], 0))
        self.assertEqual((odd.view_id, odd.row_id, odd.selected_ids, odd.selected_count),
                         (None, None, ["r1", "r3"], 2))
        self.assertEqual((counted.selected_ids, counted.selected_count), ([], 1500))

    def test_element_menu_populate_query(self):
        seen = {}
        plugin = self.plugin()

        @plugin.menu_populate("rows", "recent")
        def recent(query):
            seen.update(location=query.location, view=query.view_id, element=query.element_id,
                        row=query.row_id, selected=query.selected_ids, count=query.selected_count)
            return [self.sdk.menu_item("one", "One", event=True)]

        host = self.host(plugin)
        context = self.testing.element_context("main", "t", row_id="r1", selected_ids=["r1"])
        answer = host.populate_menu("rows", "recent", "element", context=context)
        self.assertEqual(answer, {"items": [{"id": "one", "title": "One", "event": True}]})
        self.assertEqual(seen, {"location": "element", "view": "main", "element": "t",
                                "row": "r1", "selected": ["r1"], "count": 1})


class FakeHostHelpers(TableCase):
    def test_feature_sets(self):
        testing = self.testing
        self.assertEqual(testing.HOST_FEATURES[-2:], E17D_FEATURES)
        self.assertEqual(testing.E17C_HOST_FEATURES, testing.HOST_FEATURES[:-2])
        self.assertEqual(dict(testing.E17C_HOST_FEATURE_VERSIONS),
                         {name: version for name, version in testing.HOST_FEATURE_VERSIONS.items()
                          if name not in E17D_FEATURES})
        for feature in E17D_FEATURES:
            self.assertEqual(testing.HOST_FEATURE_VERSIONS[feature], 1)
            self.assertNotIn(feature, testing.E17C_HOST_FEATURES)
        self.assertEqual(testing.MAX_TABLE_IDS, self.sdk.MAX_TABLE_IDS)


if __name__ == "__main__":
    unittest.main()
