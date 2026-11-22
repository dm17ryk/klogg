"""Python SDK trees and file trees (E17e): the tree, tree_node and file_tree builders, the node
patch helpers, @plugin.tree_children, node ids in element menu contexts, what update() and
patch() keep for trees on older hosts, and the fake host's tree helpers
(docs/plugin-sdk-advanced.md, sections 11 and 28)."""
import copy
import importlib.util
import pathlib
import queue
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = ROOT / "plugins/sdk/python"
HANDLES = ["tree/children", "ui/command", "menu/populate", "query/cancel"]
E17E_FEATURES = ("tree", "file_tree")


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, SDK / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def chain(levels, prefix="n"):
    """One node with `levels` levels of nested children (the top node is level 1)."""
    node = {"id": f"{prefix}{levels}"}
    for level in range(levels - 1, 0, -1):
        node = {"id": f"{prefix}{level}", "children": [node]}
    return node


def by_id(root):
    found, pending = {}, [root]
    while pending:
        element = pending.pop()
        if "id" in element:
            found[element["id"]] = element
        pending.extend(element.get("children", []))
    return found


class TreeCase(unittest.TestCase):
    def setUp(self):
        self.sdk = load("trees_test_sdk", "cilogg_plugin.py")
        self.testing = load("trees_test_host", "cilogg_plugin_testing.py")

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


class Builders(TreeCase):
    def test_constants(self):
        sdk = self.sdk
        self.assertEqual(sdk.TREE_CHILDREN, "tree/children")
        self.assertEqual((sdk.MAX_TREE_NODES, sdk.MAX_TREE_DEPTH, sdk.MAX_TREE_CHILDREN,
                          sdk.MAX_FILE_TREE_ROOTS), (50000, 64, 1000, 16))
        self.assertEqual(sdk.FILE_TREE_COLUMNS, ("name", "size", "type", "modified"))
        self.assertEqual(sdk.FILE_TREE_SELECT, ("files", "dirs", "both"))
        self.assertEqual(sdk.TREE_EVENTS, ("activated", "checked", "edited", "moved", "expanded",
                                           "collapsed"))
        self.assertEqual(sdk.QUERY_LANES["tree/children"], (2, False))
        self.assertEqual(sdk.QUERY_DEFAULT_TIMEOUT_MS["tree/children"], 10000)

    def test_tree_node(self):
        sdk = self.sdk
        self.assertEqual(sdk.tree_node("n1"), {"id": "n1"})
        self.assertEqual(sdk.tree_node("n1", "Images"), {"id": "n1", "text": "Images"})
        self.assertEqual(sdk.tree_node("n1", 42), {"id": "n1", "text": "42"})
        state = sdk.cell("idle", icon="status:warn")
        drag = {"path": "C:/logs/a.log"}
        editors = {"name": sdk.editor("text")}
        child = sdk.tree_node("n2", "Child")
        node = sdk.tree_node("n1", cells=["Board 1", state, 7], icon="builtin:device",
                             tooltip="t", tooltip_key="k1", tone="warn", children=[child],
                             has_children=True, expanded=True, checkable=False, checked=True,
                             editors=editors, drag=drag)
        self.assertEqual(node, {
            "id": "n1", "cells": ["Board 1", {"text": "idle", "icon": "status:warn"}, "7"],
            "icon": "builtin:device", "tooltip": "t", "tooltip_key": "k1", "tone": "warn",
            "children": [{"id": "n2", "text": "Child"}], "has_children": True, "expanded": True,
            "checkable": False, "checked": True, "editors": {"name": {"kind": "text"}},
            "drag": {"path": "C:/logs/a.log"}})
        # Cell objects, children, editors and drag are copied.
        self.assertIsNot(node["cells"][1], state)
        self.assertIsNot(node["children"][0], child)
        self.assertIsNot(node["editors"], editors)
        self.assertIsNot(node["drag"], drag)
        self.assertEqual(sdk.tree_node("n", "x", drag=True)["drag"], True)
        # checked: True, False or None (partly), left out unless given.
        self.assertNotIn("checked", sdk.tree_node("n", "x"))
        self.assertIsNone(sdk.tree_node("n", "x", checked=None)["checked"])
        self.assertIs(sdk.tree_node("n", "x", checked=False)["checked"], False)
        self.assertEqual(sdk.tree_node("n", children=[], has_children=False),
                         {"id": "n", "children": [], "has_children": False})
        for arguments, keywords in (
                (("",), {}), ((3,), {}), ((None,), {}), (("n", "x"), dict(cells=["y"])),
                (("n",), dict(tone="red")), (("n",), dict(checked="yes")),
                (("n",), dict(checked=1)), (("n",), dict(children="n2")),
                (("n",), dict(children=["n2"]))):
            with self.subTest(arguments=arguments, keywords=keywords), \
                    self.assertRaises(ValueError):
                sdk.tree_node(*arguments, **keywords)

    def test_tree(self):
        sdk = self.sdk
        nodes = [sdk.tree_node("b1", "Bench 1", has_children=True)]
        columns = [sdk.table_column("name", "Name", 200, icon="builtin:device", align="left"),
                   "state"]
        element = sdk.tree("t", nodes, columns, "multi", toggle_selection=True, checkboxes=True,
                           auto_tristate=True, lazy=True, expand_events=True, header=False,
                           context_menu="nodes", copy=False, export=True, reorderable=True,
                           tooltip_provider=True, accessible_name="Benches")
        self.assertEqual(element, {
            "type": "tree", "id": "t",
            "columns": [{"id": "name", "title": "Name", "width": 200, "align": "left",
                         "icon": "builtin:device"}, "state"],
            "nodes": [{"id": "b1", "text": "Bench 1", "has_children": True}],
            "selection": "multi", "toggle_selection": True, "checkboxes": True,
            "auto_tristate": True, "lazy": True, "expand_events": True, "header": False,
            "context_menu": "nodes", "copy": False, "export": True, "reorderable": True,
            "tooltip_provider": True, "accessible_name": "Benches"})
        self.assertIsNot(element["nodes"][0], nodes[0])
        self.assertIsNot(element["columns"][0], columns[0])
        self.assertEqual(sdk.tree("t"), {"type": "tree", "id": "t"})
        self.assertEqual(sdk.tree("t", [], selection="none")["nodes"], [])
        # The limits: 64 levels, 50,000 nodes.
        self.assertEqual(len(sdk.tree("t", [chain(64)])["nodes"]), 1)
        sdk.tree("t", [{"id": "x", "children": [chain(63)]}])
        sdk.tree("t", [{"id": f"n{n}"} for n in range(sdk.MAX_TREE_NODES)])
        for arguments, keywords in (
                ((), dict(toggle_selection=True)),
                ((None, None, "single"), dict(toggle_selection=True)),
                ((None, None, "extended"), {}),
                ((), dict(context_menu="")), ((), dict(context_menu="a/b")),
                ((), dict(context_menu=3)),
                (([chain(65)],), {}),
                (([{"id": f"n{n}"} for n in range(sdk.MAX_TREE_NODES + 1)],), {}),
                (([{"id": "a", "children": [{"id": f"n{n}"} for n in range(sdk.MAX_TREE_NODES)]}],),
                 {}),
                ((["n1"],), {}), ((None, "name"), {}), ((None, [""]), {}), ((None, [3]), {})):
            with self.subTest(arguments=arguments[:1] if arguments else (), keywords=keywords), \
                    self.assertRaises(ValueError):
                sdk.tree("t", *arguments, **keywords)

    def test_file_tree(self):
        sdk = self.sdk
        element = sdk.file_tree("files", ["C:/Firmware", "D:\\Logs", "/var/log", "//server/share",
                                          "\\\\server\\share"],
                                filters=["*.bin", "*.hex"], hide_filtered=False, show_files=True,
                                show_hidden=True, columns=["name", "type"], selection="multi",
                                toggle_selection=True, select="files", checkboxes=True,
                                value=["C:/Firmware/a.bin"], context_menu="files_menu",
                                copy=False, tooltip_provider=True)
        self.assertEqual(element, {
            "type": "file_tree", "id": "files",
            "roots": ["C:/Firmware", "D:\\Logs", "/var/log", "//server/share", "\\\\server\\share"],
            "filters": ["*.bin", "*.hex"], "hide_filtered": False, "show_files": True,
            "show_hidden": True, "columns": ["name", "type"], "selection": "multi",
            "toggle_selection": True, "select": "files", "checkboxes": True,
            "value": ["C:/Firmware/a.bin"], "context_menu": "files_menu", "copy": False,
            "tooltip_provider": True})
        self.assertEqual(sdk.file_tree("f", ("C:/a",)), {"type": "file_tree", "id": "f",
                                                         "roots": ["C:/a"]})
        self.assertEqual(sdk.file_tree("f", [], value=[])["value"], [])
        sdk.file_tree("f", [f"C:/r{n}" for n in range(sdk.MAX_FILE_TREE_ROOTS)])
        for roots, keywords in (
                ("C:/a", {}), (["relative/dir"], {}), (["C:"], {}), (["C:x"], {}),
                (["\\x"], {}), ([""], {}), ([3], {}),
                ([f"C:/r{n}" for n in range(sdk.MAX_FILE_TREE_ROOTS + 1)], {}),
                (["C:/a"], dict(columns=["name", "owner"])), (["C:/a"], dict(columns="name")),
                (["C:/a"], dict(select="links")), (["C:/a"], dict(selection="extended")),
                (["C:/a"], dict(toggle_selection=True)),
                (["C:/a"], dict(selection="single", toggle_selection=True)),
                (["C:/a"], dict(context_menu="a/b")), (["C:/a"], dict(filters="*.bin")),
                (["C:/a"], dict(value="C:/a/x.bin"))):
            with self.subTest(roots=roots, keywords=keywords), self.assertRaises(ValueError):
                sdk.file_tree("f", roots, **keywords)

    def test_patch_helpers(self):
        sdk = self.sdk
        node = sdk.tree_node("n1", "One")
        self.assertEqual(sdk.replace_nodes("t", [node]), {"id": "t", "replace_nodes": [node]})
        self.assertIsNot(sdk.replace_nodes("t", [node])["replace_nodes"][0], node)
        self.assertEqual(sdk.replace_nodes("t", [], parent="p"),
                         {"id": "t", "replace_nodes": [], "parent": "p"})
        self.assertEqual(sdk.update_nodes("t", [{"id": "n1", "text": "x", "children": []}]),
                         {"id": "t", "update_nodes": [{"id": "n1", "text": "x", "children": []}]})
        self.assertEqual(sdk.insert_nodes("t", [node]), {"id": "t", "insert_nodes": [node]})
        self.assertEqual(sdk.insert_nodes("t", (node,), parent="p", before="n0"),
                         {"id": "t", "insert_nodes": [node], "parent": "p", "before": "n0"})
        self.assertEqual(sdk.insert_nodes("t", [node], before="n0"),
                         {"id": "t", "insert_nodes": [node], "before": "n0"})
        self.assertEqual(sdk.remove_nodes("t", ("n1", "n2")),
                         {"id": "t", "remove_nodes": ["n1", "n2"]})
        self.assertEqual(sdk.expand_nodes("files", ["C:/fw", "C:/fw/old"]),
                         {"id": "files", "expand": ["C:/fw", "C:/fw/old"]})
        self.assertEqual(sdk.collapse_nodes("t", ["n1"]), {"id": "t", "collapse": ["n1"]})
        self.assertEqual(sdk.select_nodes("t", ["n1", "n2"], current="n2", scroll_to="n1"),
                         {"id": "t", "select": ["n1", "n2"], "current": "n2", "scroll_to": "n1"})
        self.assertEqual(sdk.select_nodes("t", []), {"id": "t", "select": []})
        self.assertEqual(sdk.select_nodes("t", current="n1"), {"id": "t", "current": "n1"})
        self.assertEqual(sdk.select_nodes("t", scroll_to="n9"), {"id": "t", "scroll_to": "n9"})
        # A file tree's value is a property.
        self.assertEqual(sdk.set_props("files", value=["C:/fw/a.bin"]),
                         {"id": "files", "set": {"value": ["C:/fw/a.bin"]}})
        for call in (lambda: sdk.replace_nodes("t", [{"text": "no id"}]),
                     lambda: sdk.update_nodes("t", [{"id": ""}]),
                     lambda: sdk.insert_nodes("t", ["n1"]),
                     lambda: sdk.insert_nodes("t", "n1"),
                     lambda: sdk.remove_nodes("t", "n1"),
                     lambda: sdk.remove_nodes("t", [1]),
                     lambda: sdk.expand_nodes("t", "n1"),
                     lambda: sdk.collapse_nodes("t", [""]),
                     lambda: sdk.select_nodes("t"),
                     lambda: sdk.select_nodes("t", "n1")):
            with self.assertRaises(ValueError):
                call()

    def test_element_menus_on_trees(self):
        sdk = self.sdk
        items = [sdk.menu_item("reset", "Reset node", event=True, when={"selection": "any"})]
        self.assertEqual(sdk.menu("nodes", "element", items),
                         {"id": "nodes", "location": "element", "items": items})


class TreeChildren(TreeCase):
    def nodes(self, *ids):
        return [self.sdk.tree_node(node_id, node_id.upper()) for node_id in ids]

    def test_specific_view_wide_and_query_fallback(self):
        plugin = self.plugin()
        seen = queue.Queue()

        @plugin.tree_children("main", "benches")
        def benches(query):
            seen.put(("benches", query.view_id, query.element_id, query.node_id, query.window_id))
            return self.nodes(f"{query.node_id}.a", f"{query.node_id}.b")

        @plugin.tree_children("main")
        def any_tree(query):
            seen.put(("view", query.element_id, query.node_id))
            return {"nodes": self.nodes("x")}

        @plugin.query("tree/children")
        def fallback(query):
            seen.put(("fallback", query.params["view_id"]))
            return [{"id": "raw", "text": "unchanged"}]  # a list: the host accepts it

        self.assertIs(plugin.tree_children_handlers[("main", "benches")], benches)
        self.assertIs(plugin.tree_children_handlers[("main", None)], any_tree)
        host = self.host(plugin)
        self.assertEqual(host.request_children("main", "benches", "bench1", window_id="w2"),
                         {"nodes": [{"id": "bench1.a", "text": "BENCH1.A"},
                                    {"id": "bench1.b", "text": "BENCH1.B"}]})
        self.assertEqual(seen.get(timeout=2), ("benches", "main", "benches", "bench1", "w2"))
        self.assertEqual(host.request_children("main", "other", "n1"),
                         {"nodes": [{"id": "x", "text": "X"}]})
        self.assertEqual(seen.get(timeout=2), ("view", "other", "n1"))
        self.assertEqual(host.request_children("second", "benches", "n1"),
                         [{"id": "raw", "text": "unchanged"}])
        self.assertEqual(seen.get(timeout=2), ("fallback", "second"))
        self.assertEqual(host.errors, [])

    def test_without_a_matching_handler_the_answer_is_null(self):
        plugin = self.plugin()
        plugin.tree_children("main", "benches")(lambda query: self.nodes("a"))
        host = self.host(plugin)
        self.assertIsNone(host.request_children("other", "benches", "n1"))
        self.assertIsNone(host.request_children("main", "files", "n1"))

    def test_answers_are_normalized(self):
        plugin = self.plugin()
        answers = {
            "list": [self.sdk.tree_node("a", "A", children=[self.sdk.tree_node("a1", "A1")])],
            "tuple": (self.sdk.tree_node("b", "B"),),
            "dict": {"nodes": [{"id": "c", "children": ({"id": "c1"},)}], "extra": 1},
            "none": None,
            "empty": {"nodes": []},
            "odd_children": [{"id": "d", "children": "not a list"}],
        }
        plugin.tree_children("main")(lambda query: answers[query.node_id])
        host = self.host(plugin)
        self.assertEqual(host.request_children("main", "t", "list"), {"nodes": [
            {"id": "a", "text": "A", "children": [{"id": "a1", "text": "A1"}]}]})
        self.assertEqual(host.request_children("main", "t", "tuple"),
                         {"nodes": [{"id": "b", "text": "B"}]})
        self.assertEqual(host.request_children("main", "t", "dict"),
                         {"nodes": [{"id": "c", "children": [{"id": "c1"}]}]})
        self.assertEqual(host.request_children("main", "t", "none"), {"nodes": []})
        self.assertEqual(host.request_children("main", "t", "empty"), {"nodes": []})
        # Children that are no list are not counted (the host checks them when it uses them).
        self.assertEqual(host.request_children("main", "t", "odd_children"),
                         {"nodes": [{"id": "d", "children": "not a list"}]})

    def test_the_node_limit_counts_nested_children(self):
        sdk = self.sdk
        plugin = self.plugin()
        # Like the host, an answer whose node at level 64 has a children list (even an empty
        # one) is too deep.
        empty_level_65 = chain(64)
        leaf = empty_level_65
        while "children" in leaf:
            leaf = leaf["children"][0]
        leaf["children"] = []
        answers = {
            "flat_1000": [{"id": f"n{n}"} for n in range(sdk.MAX_TREE_CHILDREN)],
            "flat_1001": [{"id": f"n{n}"} for n in range(sdk.MAX_TREE_CHILDREN + 1)],
            # 10 parents with 99 children each: 1,000 nodes; one child more passes the limit.
            "nested_1000": [{"id": f"p{p}", "children": [{"id": f"p{p}.{c}"} for c in range(99)]}
                            for p in range(10)],
            "nested_1001": [{"id": f"p{p}", "children": [{"id": f"p{p}.{c}"}
                                                         for c in range(100 if p == 9 else 99)]}
                            for p in range(10)],
            "depth_64": [chain(64)],
            "depth_65": [chain(65)],
            "empty_level_65": [empty_level_65],
        }
        plugin.tree_children("main")(lambda query: answers[query.node_id])
        host = self.host(plugin)
        self.assertEqual(len(host.request_children("main", "t", "flat_1000")["nodes"]), 1000)
        self.assertEqual(len(host.request_children("main", "t", "nested_1000")["nodes"]), 10)
        self.assertEqual(host.request_children("main", "t", "depth_64")["nodes"][0]["id"], "n1")
        for node_id in ("flat_1001", "nested_1001", "depth_65", "empty_level_65"):
            with self.subTest(answer=node_id), self.assertRaises(self.testing.RpcError) as caught:
                host.request_children("main", "t", node_id)
            self.assertEqual(caught.exception.code, -32000)
        self.assertIn("1000", self.error_message(host, "flat_1001"))

    def error_message(self, host, node_id):
        try:
            host.request_children("main", "t", node_id)
        except self.testing.RpcError as error:
            return error.message
        self.fail("no error")

    def test_other_answers_are_errors(self):
        plugin = self.plugin()
        answers = {"number": 5, "text": "n1", "bytes": b"n1", "no_nodes": {}, "null_nodes": {"nodes": None},
                   "text_nodes": {"nodes": "n1"}, "node_text": ["n1"], "nested_text": [
                       {"id": "a", "children": ["a1"]}]}

        @plugin.tree_children("main")
        def children(query):
            if query.node_id == "raise":
                raise RuntimeError("bench offline")
            return answers[query.node_id]

        host = self.host(plugin)
        for node_id in list(answers) + ["raise"]:
            with self.subTest(answer=node_id), self.assertRaises(self.testing.RpcError) as caught:
                host.request_children("main", "t", node_id)
            self.assertEqual(caught.exception.code, -32000)
        self.assertEqual(self.error_message(host, "raise"), "bench offline")
        self.assertEqual(host.errors, [])

    def test_query_properties(self):
        sdk = self.sdk
        self.assertIs(sdk._QUERY_CLASSES["tree/children"], sdk.TreeChildrenQuery)
        plugin = self.plugin()
        seen = queue.Queue()
        plugin.tree_children("main")(lambda query: seen.put(query))
        host = self.host(plugin)
        host.request_children("main", "t", "n1", context={"tab_id": "t1"})
        query = seen.get(timeout=2)
        self.assertIsInstance(query, sdk.TreeChildrenQuery)
        self.assertEqual((query.method, query.view_id, query.element_id, query.node_id,
                          query.window_id, query.context, query.timeout_ms),
                         ("tree/children", "main", "t", "n1", "w1", {"tab_id": "t1"}, 10000.0))
        host.query("tree/children", {"view_id": "main", "node_id": 7})
        odd = seen.get(timeout=2)
        self.assertEqual((odd.element_id, odd.node_id, odd.window_id), ("", "", None))

    def test_handled_methods_and_registration(self):
        plugin = self.plugin()
        self.assertEqual(plugin.handled_methods(), [])
        plugin.tree_children("main", "benches")(lambda query: None)
        self.assertEqual(plugin.handled_methods(), ["query/cancel", "tree/children"])
        plugin.on_command("nodes")(lambda command: None)
        self.assertEqual(plugin.handled_methods(), ["query/cancel", "tree/children", "ui/command"])
        result = self.host(plugin).initialization
        self.assertEqual(result["features"], ["query/cancel", "tree/children", "ui/command"])
        for arguments in (("",), ("", "t"), ("main", "")):
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                plugin.tree_children(*arguments)

    def test_a_plugin_without_tree_handlers_refuses_the_query(self):
        host = self.host(self.plugin())
        with self.assertRaises(self.testing.RpcError) as caught:
            host.request_children("main", "t", "n1")
        self.assertEqual(caught.exception.code, -32601)


class Downgrades(TreeCase):
    """Trees are never converted; their selection patch belongs to the tree feature."""

    def view(self):
        sdk = self.sdk
        nodes = [sdk.tree_node("b1", cells=[sdk.cell("Bench 1", bold=True), "2 boards"],
                               icon="builtin:folder", has_children=True, checked=None,
                               tooltip_key="b1-0"),
                 sdk.tree_node("b2", "Bench 2", children=[sdk.tree_node("x", "X")])]
        columns = [sdk.table_column("name", "Name", align="left", hidden=False, min_width=50,
                                    icon="builtin:device", tooltip="names"),
                   "state"]
        return sdk.column(
            sdk.tree("t", nodes, columns, "multi", toggle_selection=True, checkboxes=True,
                     auto_tristate=True, lazy=True, expand_events=True, header=True,
                     context_menu="nodes", copy=True, export=True, reorderable=True,
                     tooltip_provider=True),
            sdk.file_tree("files", ["C:/fw"], selection="multi", context_menu="files_menu",
                          tooltip_provider=True),
            sdk.table("rows", [sdk.table_column("a", "A")], [sdk.table_row("r1", ["x"])],
                      selection="multi", export=True),
            id="body")

    def patches(self):
        sdk = self.sdk
        node = sdk.tree_node("b3", cells=[sdk.cell("Bench 3", icon="builtin:folder")],
                             checked=True)
        return [
            sdk.replace_nodes("t", [node]),
            sdk.replace_nodes("t", [node], parent="b1"),
            sdk.update_nodes("t", [{"id": "b1", "children": [node]}]),
            sdk.insert_nodes("t", [node], parent="b2", before="x"),
            sdk.remove_nodes("t", ["x"]),
            sdk.expand_nodes("t", ["b1"]),
            sdk.collapse_nodes("t", ["b2"]),
            sdk.select_nodes("t", ["b1"], current="b1", scroll_to="b1"),
            sdk.expand_nodes("files", ["C:/fw/old"]),
            sdk.set_props("files", value=["C:/fw/a.bin"]),
            sdk.set_props("t", header=False, export=False, context_menu="other",
                          columns=[sdk.table_column("name", "Name", align="right")]),
            sdk.select_rows("rows", ["r1"]),
        ]

    def session(self, **initialize):
        plugin = self.plugin(handles=None)
        older = initialize.pop("older", False)
        host = self.host(plugin, older=older, **initialize)
        view, patches = self.view(), self.patches()
        originals = copy.deepcopy((view, patches))
        plugin.update("main", view)
        root = host.wait_for("view/update")["root"]
        plugin.patch("main", patches)
        patch = host.wait_for("view/update")["patch"]
        self.assertEqual((view, patches), originals)  # the caller's dicts are not changed
        return plugin, by_id(root), patch

    def test_current_host_gets_everything(self):
        plugin, elements, patch = self.session()
        for feature in E17E_FEATURES:
            self.assertTrue(plugin.supports(feature), feature)
        self.assertEqual(elements["body"], self.view())
        self.assertEqual(patch, self.patches())

    def test_e17d_host_gets_trees_unchanged(self):
        """A host without tree and file_tree skips the elements: the SDK does not convert
        them."""
        plugin, elements, patch = self.session(
            host_features=self.testing.E17D_HOST_FEATURES,
            host_feature_versions=self.testing.E17D_HOST_FEATURE_VERSIONS)
        for feature in E17E_FEATURES:
            self.assertFalse(plugin.supports(feature), feature)
        self.assertEqual(elements["body"], self.view())
        self.assertEqual(patch, self.patches())

    def test_host_without_rich_tables_keeps_the_tree_selection_patch(self):
        _plugin, elements, patch = self.session(**self.without("rich_tables"))
        expected = by_id(self.view())
        # The tree and the file tree keep every key, also those a table would lose.
        self.assertEqual(elements["t"], expected["t"])
        self.assertEqual(elements["files"], expected["files"])
        self.assertNotIn("export", elements["rows"])  # the table is converted as before
        self.assertEqual(patch, self.patches()[:-1])  # only the table's selection patch goes

    def test_host_without_rich_tables_and_tree_drops_the_tree_selection_patch(self):
        _plugin, elements, patch = self.session(**self.without("rich_tables", "tree"))
        self.assertEqual(elements["t"], by_id(self.view())["t"])  # not converted either
        expected = self.patches()
        del expected[-1]  # the table's selection patch
        del expected[7]  # the tree's
        self.assertEqual(patch, expected)

    def test_e17c_host(self):
        _plugin, elements, patch = self.session(
            host_features=self.testing.E17C_HOST_FEATURES,
            host_feature_versions=self.testing.E17C_HOST_FEATURE_VERSIONS)
        self.assertEqual(elements["t"], by_id(self.view())["t"])
        self.assertEqual(elements["files"], by_id(self.view())["files"])
        expected = self.patches()
        del expected[-1]
        del expected[7]
        self.assertEqual(patch, expected)

    def test_e14_host_loses_only_the_element_keys(self):
        _plugin, elements, patch = self.session(older=True)
        tree = dict(by_id(self.view())["t"])
        del tree["tooltip_provider"]
        self.assertEqual(elements["t"], tree)  # node tooltip keys, columns and cells stay
        files = dict(by_id(self.view())["files"])
        del files["tooltip_provider"]
        self.assertEqual(elements["files"], files)
        patches = self.patches()
        self.assertEqual(patch, patches[:7] + patches[8:11])  # no selection patches

    def test_patch_of_an_unknown_element_keeps_the_keys_trees_share(self):
        sdk = self.sdk
        for name, features, kept in (
                ("tree, no rich_tables", ("rich_tables",),
                 {"header": False, "toggle_selection": True, "copy": False, "export": True,
                  "reorderable": True, "context_menu": "m"}),
                ("E17c", ("rich_tables", "element_menus", "tree", "file_tree"), {})):
            with self.subTest(host=name):
                plugin = self.plugin(handles=None)
                host = self.host(plugin, **self.without(*features))
                plugin.update("main", sdk.column(sdk.label("l", "x"), id="body"))
                host.wait_for("view/update")
                plugin.patch("main", [sdk.set_props(
                    "later", header=False, toggle_selection=True, copy=False, export=True,
                    reorderable=True, context_menu="m", keep_columns=True, text="kept")])
                self.assertEqual(host.wait_for("view/update")["patch"],
                                 [{"id": "later", "set": {**kept, "text": "kept"}}])


class Events(TreeCase):
    def received(self, handles=HANDLES):
        events = queue.Queue()
        plugin = self.plugin(handles=handles)
        plugin.on_view_event(events.put)
        return self.host(plugin), events

    def test_tree_events_from_the_fake_host(self):
        host, events = self.received(handles=None)  # every plugin gets every tree key
        host.select_nodes("main", "t", ["b1", "b1.x"], current="b1.x")
        host.activate_node("main", "t", "b1", "state")
        host.check_nodes("main", "t", ["b1", "b1.x", "b1.y"], True)
        host.check_nodes("main", "t", "b2", False, node_id="b2", window_id="w2")
        host.edit_node("main", "t", "b1", "name", "Bench A", previous="Bench 1")
        host.move_nodes("main", "t", ["b1.x"], parent="b2", before="b2.y")
        host.move_nodes("main", "t", ("b2",))
        host.expand_node("main", "t", "b1")
        host.collapse_node("main", "t", "b1")
        host.activate_node("main", "t", "b2")  # a tree without columns
        received = [events.get(timeout=2) for _ in range(10)]
        self.assertEqual([(event["type"], event["value"]) for event in received], [
            ("selected", ["b1", "b1.x"]),
            ("activated", {"node_id": "b1", "column_id": "state"}),
            ("checked", {"node_id": "b1", "checked": True, "node_ids": ["b1", "b1.x", "b1.y"]}),
            ("checked", {"node_id": "b2", "checked": False, "node_ids": ["b2"]}),
            ("edited", "Bench A"),
            ("moved", {"node_ids": ["b1.x"], "parent": "b2", "before": "b2.y"}),
            ("moved", {"node_ids": ["b2"], "parent": None, "before": None}),
            ("expanded", {"node_id": "b1"}),
            ("collapsed", {"node_id": "b1"}),
            ("activated", {"node_id": "b2", "column_id": None}),
        ])
        self.assertEqual(received[0], {"view_id": "main", "window_id": "w1", "element_id": "t",
                                       "type": "selected", "value": ["b1", "b1.x"], "inputs": {},
                                       "selected_count": 2, "current": "b1.x"})
        self.assertEqual(received[3]["window_id"], "w2")
        self.assertEqual({key: received[4][key] for key in ("node_id", "column_id", "previous")},
                         {"node_id": "b1", "column_id": "name", "previous": "Bench 1"})
        for call in (lambda: host.check_nodes("main", "t", []),
                     lambda: host.move_nodes("main", "t", [])):
            with self.assertRaises(ValueError):
                call()

    def test_selected_counts_and_truncation(self):
        host, events = self.received()
        host.select_nodes("main", "t", [])
        host.select_nodes("main", "t", [f"n{n}" for n in range(1005)], current="n0")
        host.select_nodes("main", "t", ["n1"], selected_count=4)
        empty, many, partial = (events.get(timeout=2) for _ in range(3))
        self.assertEqual((empty["value"], empty["selected_count"], empty["current"]), ([], 0, None))
        self.assertNotIn("truncated", empty)
        self.assertEqual((len(many["value"]), many["selected_count"], many["truncated"],
                          many["current"]), (1000, 1005, True, "n0"))
        self.assertEqual((partial["selected_count"], partial["truncated"]), (4, True))

    def test_file_tree_events_from_the_fake_host(self):
        host, events = self.received()
        host.select_files("main", "files", ["C:\\fw\\a.bin", "C:/fw/old"], inputs={"note": "x"})
        host.select_files("main", "files", "C:/fw/b.bin", window_id="w2")
        host.activate_file("main", "files", "C:\\fw\\a.bin")
        selected, single, activated = (events.get(timeout=2) for _ in range(3))
        self.assertEqual(selected, {"view_id": "main", "window_id": "w1", "element_id": "files",
                                    "type": "selected", "value": ["C:/fw/a.bin", "C:/fw/old"],
                                    "inputs": {"note": "x", "files": ["C:/fw/a.bin", "C:/fw/old"]},
                                    "selected_count": 2})
        self.assertEqual((single["value"], single["window_id"]), (["C:/fw/b.bin"], "w2"))
        self.assertEqual((activated["type"], activated["value"]),
                         ("activated", {"path": "C:/fw/a.bin"}))


class NodeMenus(TreeCase):
    def test_node_context(self):
        node_context = self.testing.node_context
        self.assertEqual(node_context("main", "t", node_id="b1", selected_ids=["b1", "b2"],
                                      window_id="w2"),
                         {"window_id": "w2", "view_id": "main", "element_id": "t",
                          "node_id": "b1", "selected_ids": ["b1", "b2"], "selected_count": 2})
        self.assertEqual(node_context("main", "t"),
                         {"window_id": "w1", "view_id": "main", "element_id": "t",
                          "node_id": None, "selected_ids": [], "selected_count": 0})
        many = node_context("main", "t", selected_ids=[f"n{n}" for n in range(1200)])
        self.assertEqual((len(many["selected_ids"]), many["selected_count"]), (1000, 1200))
        with self.assertRaises(ValueError):
            node_context("main", "t", selected_ids="b1")

    def test_node_command(self):
        received = queue.Queue()
        plugin = self.plugin()
        plugin.on_command("nodes")(received.put)
        host = self.host(plugin)
        host.send_node_command("nodes", "reset", "main", "t", node_id="b1.x",
                               selected_ids=["b1.x", "b2"])
        host.send_node_command("nodes", "path", "main", "files", node_id="C:/fw/a.bin",
                               selected_ids=["C:/fw/a.bin"], window_id="w2")
        host.send_node_command("nodes", "collapse_all", "main", "t")
        reset, path, collapse = (received.get(timeout=2) for _ in range(3))
        sent = [frame.message["params"] for frame in host.frames
                if frame.direction == self.testing.TO_PLUGIN and frame.method == "ui/command"]
        self.assertEqual(sent[0], {"origin": "menu", "menu_id": "nodes", "item_id": "reset",
                                   "location": "element", "window_id": "w1", "context": {
                                       "window_id": "w1", "view_id": "main", "element_id": "t",
                                       "node_id": "b1.x", "selected_ids": ["b1.x", "b2"],
                                       "selected_count": 2}})
        self.assertEqual((reset.location, reset.view_id, reset.element_id, reset.node_id,
                          reset.row_id, reset.column_id, reset.selected_ids,
                          reset.selected_count),
                         ("element", "main", "t", "b1.x", None, None, ["b1.x", "b2"], 2))
        self.assertEqual((path.window_id, path.element_id, path.node_id, path.selected_ids),
                         ("w2", "files", "C:/fw/a.bin", ["C:/fw/a.bin"]))
        self.assertEqual((collapse.node_id, collapse.selected_ids, collapse.selected_count),
                         (None, [], 0))

    def test_node_id_elsewhere(self):
        received = queue.Queue()
        plugin = self.plugin()
        plugin.on_command()(received.put)
        seen = {}

        @plugin.menu_populate("nodes", "recent")
        def recent(query):
            seen.update(node=query.node_id, row=query.row_id, selected=query.selected_ids)
            return [self.sdk.menu_item("one", "One", event=True)]

        host = self.host(plugin)
        host.send_command("main", "plain")
        host.send_command("nodes", "odd", "element", context={"node_id": 3})
        host.send_element_command("rows", "forget", "main", "table", row_id="r1")
        plain, odd, table = (received.get(timeout=2) for _ in range(3))
        self.assertEqual((plain.node_id, odd.node_id, table.node_id, table.row_id),
                         (None, None, None, "r1"))
        context = self.testing.node_context("main", "t", node_id="b1", selected_ids=["b1"])
        self.assertEqual(host.populate_menu("nodes", "recent", "element", context=context),
                         {"items": [{"id": "one", "title": "One", "event": True}]})
        self.assertEqual(seen, {"node": "b1", "row": None, "selected": ["b1"]})


class FakeHostHelpers(TreeCase):
    def test_feature_sets(self):
        testing = self.testing
        self.assertEqual(testing.E17E_HOST_FEATURES[-2:], E17E_FEATURES)
        self.assertEqual(testing.E17D_HOST_FEATURES, testing.E17E_HOST_FEATURES[:-2])
        self.assertEqual(testing.E17E_HOST_FEATURES,
                         testing.HOST_FEATURES[:len(testing.E17E_HOST_FEATURES)])
        self.assertEqual(dict(testing.E17D_HOST_FEATURE_VERSIONS),
                         {name: version for name, version in testing.E17E_HOST_FEATURE_VERSIONS.items()
                          if name not in E17E_FEATURES})
        for feature in E17E_FEATURES:
            self.assertEqual(testing.HOST_FEATURE_VERSIONS[feature], 1)
            self.assertEqual(testing.E17E_HOST_FEATURE_VERSIONS[feature], 1)
            self.assertNotIn(feature, testing.E17D_HOST_FEATURES)
        self.assertEqual(testing.MAX_TREE_IDS, 1000)
        self.assertEqual(testing.QUERY_DEFAULT_TIMEOUT_MS["tree/children"], 10000)

    def test_request_children_refuses_other_answers(self):
        plugin = self.plugin()
        plugin.query("tree/children")(lambda query: "text")
        host = self.host(plugin)
        with self.assertRaises(self.testing.RpcError) as caught:
            host.request_children("main", "t", "n1")
        self.assertEqual(caught.exception.code, -32603)


if __name__ == "__main__":
    unittest.main()
