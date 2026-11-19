"""Python SDK menus (E17b): menu builders, menu/populate and ui/command handlers, update_menu
and the fake host's menu helpers (docs/plugin-sdk-advanced.md, sections 4, 5 and 7)."""
import contextlib
import copy
import importlib.util
import io
import pathlib
import queue
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = ROOT / "plugins/sdk/python"
HANDLES = ["tooltip/resolve", "menu/populate", "ui/command", "query/cancel", "tree/children"]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, SDK / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class MenuCase(unittest.TestCase):
    def setUp(self):
        self.sdk = load("menus_test_sdk", "cilogg_plugin.py")
        self.testing = load("menus_test_host", "cilogg_plugin_testing.py")

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

    def sync(self, host):
        """Return once the plugin has read every frame sent so far (it refuses this request)."""
        with self.assertRaises(self.testing.RpcError):
            host.request("sync/point")

    def sent(self, host, method):
        """The params of the last frame `method` the host sent."""
        frames = [frame for frame in host.frames
                  if frame.direction == self.testing.TO_PLUGIN and frame.method == method]
        return frames[-1].message["params"]


class Builders(MenuCase):
    def test_menu_item_keeps_the_given_keys_and_copies_mappings(self):
        sdk = self.sdk
        self.assertEqual(sdk.menu_item("open", "Open", view="main"),
                         {"id": "open", "title": "Open", "view": "main"})
        arguments = {"mode": "fast"}
        when = {"selection": "lines", "max_lines": 10}
        entry = sdk.menu_item("write", "Write", icon="builtin:upload", tooltip="Writes", enabled=False,
                              visible=True, action="write", arguments=arguments, when=when,
                              shortcut="Ctrl+Alt+W")
        self.assertEqual(entry, {"id": "write", "title": "Write", "icon": "builtin:upload",
                                 "tooltip": "Writes", "enabled": False, "visible": True,
                                 "action": "write", "arguments": {"mode": "fast"},
                                 "when": {"selection": "lines", "max_lines": 10},
                                 "shortcut": "Ctrl+Alt+W"})
        arguments["mode"] = "slow"
        when["selection"] = "any"
        self.assertEqual(entry["arguments"], {"mode": "fast"})
        self.assertEqual(entry["when"]["selection"], "lines")

    def test_radio_items_submenus_separators_and_sections(self):
        sdk = self.sdk
        fast = sdk.menu_item("fast", "Fast", event=True, checkable=True, checked=True, group="mode")
        self.assertEqual(fast, {"id": "fast", "title": "Fast", "event": True, "checkable": True,
                                "checked": True, "group": "mode"})
        submenu = sdk.menu_item("mode", "Mode", items=(fast, sdk.menu_separator(),
                                                       sdk.menu_section("More")))
        self.assertEqual(submenu["items"], [fast, {"type": "separator"},
                                            {"type": "section", "title": "More"}])
        self.assertEqual(sdk.menu_item("recent", "Recent", dynamic=True),
                         {"id": "recent", "title": "Recent", "dynamic": True})
        self.assertEqual(sdk.menu_item("v", "View", view="main", shortcut=("Ctrl+1", "Ctrl+2")),
                         {"id": "v", "title": "View", "view": "main", "shortcut": ["Ctrl+1", "Ctrl+2"]})
        self.assertEqual(sdk.menu_item("x", "X", event=False, action="run"),
                         {"id": "x", "title": "X", "action": "run"})  # event=False: no event
        self.assertEqual(sdk.menu_separator(id="s1"), {"type": "separator", "id": "s1"})
        self.assertEqual(sdk.menu_section("T", id="s2"), {"type": "section", "title": "T", "id": "s2"})

    def test_menu_item_refuses_the_shapes_the_host_refuses(self):
        """The host's rules of section 7.3: exactly one target or a submenu, arguments only
        with an action, check marks only on checkable entries, valid shortcut lists."""
        sdk = self.sdk
        child = [sdk.menu_item("c", "C", event=True)]
        for keywords in (
                {}, dict(event=False), dict(action="a", view="v"), dict(view="v", event=True),
                dict(action="a", event=True), dict(arguments={"x": 1}),
                dict(event=True, arguments={}),
                dict(items=child, event=True), dict(dynamic=True, action="a"),
                dict(items=child, dynamic=True), dict(dynamic=True, shortcut="Ctrl+Alt+R"),
                dict(items=child, checkable=True),
                dict(event=True, group="mode"), dict(event=True, checked=True),
                dict(event=True, shortcut=[]), dict(event=True, shortcut=""),
                dict(event=True, shortcut=["A", "B", "C"]), dict(event=True, shortcut=["Ctrl+A", 3])):
            with self.subTest(keywords=keywords), self.assertRaises(ValueError):
                sdk.menu_item("x", "X", **keywords)
        for identifier, title in (("", "X"), ("x", "")):
            with self.subTest(identifier=identifier, title=title), self.assertRaises(ValueError):
                sdk.menu_item(identifier, title, event=True)
        self.assertEqual(sdk.menu_item("x", "X", event=True, checkable=True, checked=False),
                         {"id": "x", "title": "X", "event": True, "checkable": True, "checked": False})
        self.assertEqual(sdk.menu_item("sub", "Sub", items=[], enabled=False),
                         {"id": "sub", "title": "Sub", "items": [], "enabled": False})

    def test_menu_and_menu_set(self):
        sdk = self.sdk
        items = [sdk.menu_item("a", "A", event=True)]
        declared = sdk.menu("main", "plugins", items, title="Tools", icon="builtin:chip")
        self.assertEqual(declared, {"id": "main", "location": "plugins", "title": "Tools",
                                    "icon": "builtin:chip",
                                    "items": [{"id": "a", "title": "A", "event": True}]})
        self.assertIsNot(declared["items"][0], items[0])
        self.assertEqual(sdk.MENU_LOCATIONS,
                         ("plugins", "menubar", "tab", "pane", "log", "element", "toolbar"))
        for location in sdk.MENU_LOCATIONS:
            self.assertEqual(sdk.menu("m", location, [])["location"], location)
        for identifier, location in (("m", "statusbar"), ("", "log")):
            with self.subTest(location=location), self.assertRaises(ValueError):
                sdk.menu(identifier, location, [])
        self.assertEqual(sdk.menu_set("a", title="B", enabled=False, checked=True),
                         {"id": "a", "set": {"title": "B", "enabled": False, "checked": True}})
        self.assertEqual(sdk.menu_set("a", icon="status:ok", tooltip="t", visible=False),
                         {"id": "a", "set": {"icon": "status:ok", "tooltip": "t", "visible": False}})
        self.assertEqual(sdk.menu_set("a"), {"id": "a", "set": {}})
        with self.assertRaises(ValueError):
            sdk.menu_set("")


class Features(MenuCase):
    def test_populate_handler_announces_menu_populate_and_query_cancel(self):
        plugin = self.plugin()
        plugin.menu_populate("main", "recent")(lambda _query: [])
        host = self.host(plugin)
        self.assertEqual(host.initialization["features"], ["menu/populate", "query/cancel"])

    def test_command_handler_alone_announces_only_ui_command(self):
        plugin = self.plugin()
        plugin.on_command()(lambda _command: None)
        host = self.host(plugin)
        self.assertEqual(host.initialization["features"], ["ui/command"])

    def test_every_handler_kind(self):
        plugin = self.plugin()
        plugin.tooltip("main")(lambda _query: None)
        plugin.menu_populate("main")(lambda _query: [])
        plugin.on_command("main", "go")(lambda _command: None)
        plugin.query("tree/children")(lambda _query: {"nodes": []})
        host = self.host(plugin)
        self.assertEqual(host.initialization["features"],
                         ["menu/populate", "query/cancel", "tooltip/resolve", "tree/children",
                          "ui/command"])
        self.assertEqual(plugin.handled_methods(), host.initialization["features"])

    def test_a_generic_menu_populate_handler_also_announces_it(self):
        plugin = self.plugin()
        plugin.query("menu/populate")(lambda _query: None)
        self.assertEqual(plugin.handled_methods(), ["menu/populate", "query/cancel"])

    def test_registration_refuses_bad_ids_and_ui_command_as_a_query(self):
        plugin = self.plugin()
        with self.assertRaises(ValueError):
            plugin.query("ui/command")
        for menu_id, item_id in (("", None), ("main", "")):
            with self.subTest(menu_id=menu_id, item_id=item_id), self.assertRaises(ValueError):
                plugin.menu_populate(menu_id, item_id)
        for menu_id, item_id in (("", None), (None, "")):
            with self.subTest(menu_id=menu_id, item_id=item_id), self.assertRaises(ValueError):
                plugin.on_command(menu_id, item_id)

    def stderr_of(self, plugin):
        output = io.StringIO()
        with contextlib.redirect_stderr(output):
            host = self.testing.FakeHost.in_process(plugin, timeout=5)
            try:
                host.initialize()
            finally:
                host.close()
        return output.getvalue()

    def test_handles_warning_covers_the_menu_methods(self):
        plugin = self.plugin(handles=["query/cancel"])
        plugin.menu_populate("main")(lambda _query: [])
        plugin.on_command()(lambda _command: None)
        text = self.stderr_of(plugin)
        self.assertIn("warning", text)
        self.assertIn("menu/populate, ui/command", text)
        listed = self.plugin(handles=["menu/populate", "ui/command", "query/cancel"])
        listed.menu_populate("main")(lambda _query: [])
        listed.on_command()(lambda _command: None)
        self.assertEqual(self.stderr_of(listed), "")


class Populate(MenuCase):
    def test_list_answer_and_the_query_object(self):
        sdk = self.sdk
        seen = {}
        plugin = self.plugin()

        @plugin.menu_populate("main", "recent")
        def recent(query):
            seen.update(kind=type(query).__name__, menu=query.menu_id, item=query.item_id,
                        location=query.location, window=query.window_id, context=query.context,
                        ranges=query.ranges, count=query.line_count, line=query.line,
                        timeout=query.timeout_ms, thread=threading.current_thread().name)
            return [sdk.menu_item("a.log", "a.log", event=True), sdk.menu_separator()]

        host = self.host(plugin)
        context = {"tab_id": "t1", "view": "main", "line": 4,
                   "selection": self.testing.log_selection([(3, 5), (9, 9)])}
        answer = host.populate_menu("main", "recent", "log", context, timeout_ms=800, window_id="w2")
        self.assertEqual(answer, {"items": [{"id": "a.log", "title": "a.log", "event": True},
                                            {"type": "separator"}]})
        self.assertEqual(self.sent(host, "menu/populate"),
                         {"menu_id": "main", "item_id": "recent", "location": "log",
                          "window_id": "w2", "timeout_ms": 800, "context": context})
        self.assertEqual((seen["kind"], seen["menu"], seen["item"], seen["location"], seen["window"]),
                         ("MenuQuery", "main", "recent", "log", "w2"))
        self.assertEqual(seen["context"], context)
        self.assertEqual((seen["ranges"], seen["count"], seen["line"], seen["timeout"]),
                         ([(3, 5), (9, 9)], 4, 4, 800))
        self.assertTrue(seen["thread"].startswith("plugin-query-menu/populate"))

    def test_dict_answers_and_cache_ms_bounds(self):
        entry = {"id": "e", "title": "E", "event": True}
        answers = {
            "plain": ({"items": [entry], "cache_ms": 1500}, {"items": [entry], "cache_ms": 1500}),
            "zero": ({"items": [], "cache_ms": 0}, {"items": [], "cache_ms": 0}),
            "max": ({"items": [], "cache_ms": 60000}, {"items": [], "cache_ms": 60000}),
            "long": ({"items": [], "cache_ms": 120000}, {"items": [], "cache_ms": 60000}),
            "negative": ({"items": [], "cache_ms": -5}, {"items": [], "cache_ms": 0}),
            "fraction": ({"items": [], "cache_ms": 2500.9}, {"items": [], "cache_ms": 2500}),
            "unset": ({"items": [], "cache_ms": None}, {"items": []}),
            "future": ({"items": [], "future": 1}, {"items": [], "future": 1}),
            "tuple": ((entry,), {"items": [entry]}),
            "empty": ([], {"items": []}),
            "null": (None, None),
        }
        failures = {"no_items": {"cache_ms": 5}, "text_items": {"items": "abc"},
                    "text_cache": {"items": [], "cache_ms": "soon"},
                    "bool_cache": {"items": [], "cache_ms": True}, "text": "Recent", "number": 42}
        plugin = self.plugin()

        @plugin.menu_populate("main")
        def any_entry(query):
            if query.item_id in failures:
                return failures[query.item_id]
            return answers[query.item_id][0]

        host = self.host(plugin)
        for item_id, (_returned, expected) in answers.items():
            with self.subTest(item_id=item_id):
                self.assertEqual(host.populate_menu("main", item_id), expected)
        for item_id in failures:
            with self.subTest(item_id=item_id):
                with self.assertRaises(self.testing.RpcError) as caught:
                    host.populate_menu("main", item_id)
                self.assertEqual(caught.exception.code, -32000)
        self.assertEqual(host.errors, [])

    def test_shortcuts_are_removed_without_changing_the_entries(self):
        sdk = self.sdk
        entries = [sdk.menu_item("a", "A", view="main", shortcut="Ctrl+Alt+A"),
                   sdk.menu_item("sub", "Sub", items=[
                       sdk.menu_item("deep", "Deep", event=True, shortcut=["Ctrl+D", "Ctrl+E"]),
                       sdk.menu_separator()]),
                   sdk.menu_item("plain", "Plain", event=True)]
        original = copy.deepcopy(entries)
        plugin = self.plugin()
        plugin.menu_populate("main", "dyn")(lambda _query: {"items": entries, "cache_ms": 10})
        host = self.host(plugin)
        self.assertEqual(host.populate_menu("main", "dyn"), {"cache_ms": 10, "items": [
            {"id": "a", "title": "A", "view": "main"},
            {"id": "sub", "title": "Sub", "items": [{"id": "deep", "title": "Deep", "event": True},
                                                    {"type": "separator"}]},
            {"id": "plain", "title": "Plain", "event": True}]})
        self.assertEqual(entries, original)

    def test_entry_handler_wins_over_the_menu_handler_and_the_generic_one(self):
        plugin = self.plugin()
        plugin.query("menu/populate")(lambda query: {"items": [{"id": "generic"}], "shortcut": "kept"})
        plugin.menu_populate("main")(lambda _query: [{"id": "menu"}])
        plugin.menu_populate("main", "special")(lambda _query: [{"id": "entry"}])
        host = self.host(plugin)
        self.assertEqual(host.populate_menu("main", "special"), {"items": [{"id": "entry"}]})
        self.assertEqual(host.populate_menu("main", "other"), {"items": [{"id": "menu"}]})
        # The generic @plugin.query handler's result goes out as it is.
        self.assertEqual(host.populate_menu("tabs", "x"),
                         {"items": [{"id": "generic"}], "shortcut": "kept"})

    def test_unmatched_menu_answers_null_and_without_handlers_method_not_found(self):
        plugin = self.plugin()
        plugin.menu_populate("main", "recent")(lambda _query: [])
        host = self.host(plugin)
        self.assertIsNone(host.populate_menu("tabs", "recent"))
        self.assertIsNone(host.populate_menu("main", "other"))
        commands_only = self.plugin()
        commands_only.on_command()(lambda _command: None)
        other = self.host(commands_only)
        with self.assertRaises(self.testing.RpcError) as caught:
            other.populate_menu("main", "recent")
        self.assertEqual(caught.exception.code, -32601)

    def test_timeout_sends_query_cancel_and_suppresses_the_late_answer(self):
        gate = threading.Event()
        self.addCleanup(gate.set)
        plugin = self.plugin()

        @plugin.menu_populate("main", "slow")
        def slow(_query):
            gate.wait(5)
            return []

        host = self.host(plugin)
        with self.assertRaises(self.testing.QueryTimeout) as caught:
            host.populate_menu("main", "slow", timeout=0.2)
        self.assertEqual((caught.exception.method, host.frames[-1].method),
                         ("menu/populate", "query/cancel"))
        self.sync(host)
        gate.set()
        self.assertEqual(host.populate_menu("main", "slow"), {"items": []})
        self.assertEqual(host.answers(caught.exception.id), [])

    def test_fake_host_refuses_answers_that_are_not_objects(self):
        plugin = self.plugin()
        plugin.query("menu/populate")(lambda _query: [1, 2])
        host = self.host(plugin)
        with self.assertRaises(self.testing.RpcError) as caught:
            host.populate_menu("main", "x")
        self.assertEqual(caught.exception.code, -32603)


class Commands(MenuCase):
    def test_command_object_runs_on_the_view_callback_worker(self):
        received = queue.Queue()
        plugin = self.plugin()

        @plugin.on_command("lines", "explain")
        def explain(command):
            received.put((command, threading.current_thread().name))

        host = self.host(plugin)
        selection = self.testing.log_selection([(10, 12), (20, 20)], count=4, text="a\nb",
                                               truncated=True)
        host.send_command("lines", "explain", "log", origin="shortcut", window_id="w3",
                          context={"tab_id": "t2", "view": "filtered", "line": 11},
                          selection=selection)
        command, thread = received.get(timeout=2)
        self.assertEqual(self.sent(host, "ui/command"), {
            "origin": "shortcut", "menu_id": "lines", "item_id": "explain", "location": "log",
            "window_id": "w3", "context": {"tab_id": "t2", "view": "filtered", "line": 11,
                                           "selection": selection}})
        self.assertEqual((command.origin, command.menu_id, command.item_id, command.location,
                          command.window_id), ("shortcut", "lines", "explain", "log", "w3"))
        self.assertIsNone(command.checked)
        self.assertEqual((command.context["view"], command.line), ("filtered", 11))
        self.assertEqual(command.selection, selection)
        self.assertEqual(command.ranges, [(10, 12), (20, 20)])
        self.assertEqual((command.line_count, command.text, command.truncated), (4, "a\nb", True))
        self.assertIs(command.plugin, plugin)
        self.assertEqual(thread, "plugin-view-callbacks")  # never the reader (fake-host-plugin)

    def test_checked_state_and_missing_selection(self):
        received = queue.Queue()
        plugin = self.plugin()
        plugin.on_command("main")(received.put)
        host = self.host(plugin)
        host.send_command("main", "fast", checked=True)
        host.send_command("main", "fast", checked=False)
        host.notify("ui/command", {"origin": "menu", "menu_id": "main", "item_id": "odd",
                                   "checked": "yes", "context": "not an object"})
        first, second, odd = (received.get(timeout=2) for _ in range(3))
        self.assertEqual((first.checked, second.checked, odd.checked), (True, False, None))
        self.assertEqual(self.sent(host, "ui/command")["item_id"], "odd")
        self.assertEqual((odd.context, odd.selection, odd.ranges, odd.line_count, odd.line,
                          odd.text, odd.truncated), ({}, None, [], 0, None, None, False))
        self.assertEqual((first.location, odd.location, odd.window_id), ("plugins", None, None))

    def test_ranges_without_count_and_malformed_ranges(self):
        received = queue.Queue()
        plugin = self.plugin()
        plugin.on_command()(received.put)
        host = self.host(plugin)
        host.send_command("lines", "x", "log", selection={
            "ranges": [[1, 3], [5, 5], [9, 7], [0, 2], ["a", 4], [True, 2], [6], "x"]})
        command = received.get(timeout=2)
        self.assertEqual(command.ranges, [(1, 3), (5, 5)])
        self.assertEqual(command.line_count, 4)

    def test_most_specific_handler_wins(self):
        calls = queue.Queue()
        plugin = self.plugin()
        plugin.on_command()(lambda command: calls.put(("any", command.menu_id, command.item_id)))
        plugin.on_command("main")(lambda command: calls.put(("menu", command.menu_id, command.item_id)))
        plugin.on_command(None, "refresh")(
            lambda command: calls.put(("entry", command.menu_id, command.item_id)))
        plugin.on_command("main", "refresh")(
            lambda command: calls.put(("exact", command.menu_id, command.item_id)))
        host = self.host(plugin)
        for menu_id, item_id in (("main", "refresh"), ("tabs", "refresh"), ("main", "other"),
                                 ("tabs", "other")):
            host.send_command(menu_id, item_id)
        self.assertEqual([calls.get(timeout=2) for _ in range(4)],
                         [("exact", "main", "refresh"), ("entry", "tabs", "refresh"),
                          ("menu", "main", "other"), ("any", "tabs", "other")])

    def test_unmatched_and_failing_commands_are_reported_on_stderr(self):
        handled = threading.Event()
        plugin = self.plugin()
        plugin.on_command("main", "ok")(lambda _command: handled.set())

        @plugin.on_command("main", "boom")
        def boom(_command):
            raise RuntimeError("no device")

        output = io.StringIO()
        with contextlib.redirect_stderr(output):
            host = self.testing.FakeHost.in_process(plugin, timeout=5)
            try:
                host.initialize()
                host.send_command("tabs", "x", "tab")
                host.send_command("main", "boom")
                host.send_command("main", "ok")
                self.assertTrue(handled.wait(2))
                self.sync(host)
                self.assertEqual(host.errors, [])
            finally:
                host.close()
        text = output.getvalue()
        self.assertIn("no ui/command handler for menu 'tabs' entry 'x'", text)
        self.assertIn("Plugin command main/boom failed: no device", text)

    def test_commands_and_view_events_share_one_ordered_worker(self):
        order = queue.Queue()
        plugin = self.plugin()
        plugin.on_view_event(lambda params: order.put(("event", params["element_id"])))
        plugin.on_command()(lambda command: order.put(("command", command.item_id)))
        host = self.host(plugin)
        host.view_event("main", "e1", "clicked")
        host.send_command("main", "c1")
        host.view_event("main", "e2", "clicked")
        host.send_command("main", "c2")
        self.assertEqual([order.get(timeout=2) for _ in range(4)],
                         [("event", "e1"), ("command", "c1"), ("event", "e2"), ("command", "c2")])

    def test_a_slow_command_handler_does_not_block_queries(self):
        gate = threading.Event()
        started = threading.Event()
        self.addCleanup(gate.set)
        plugin = self.plugin()
        plugin.on_command()(lambda _command: (started.set(), gate.wait(5)))
        plugin.tooltip("main")(lambda _query: "quick")
        plugin.menu_populate("main")(lambda _query: [{"id": "fresh"}])
        host = self.host(plugin)
        host.send_command("main", "slow")
        self.assertTrue(started.wait(2))
        began = time.monotonic()
        self.assertEqual(host.resolve_tooltip("main", "a", timeout=1), {"text": "quick"})
        self.assertEqual(host.populate_menu("main", "dyn", timeout=1), {"items": [{"id": "fresh"}]})
        self.assertLess(time.monotonic() - began, 1)
        gate.set()

    def test_ui_command_without_handlers_or_as_a_request(self):
        plugin = self.plugin(handles=None)
        output = io.StringIO()
        with contextlib.redirect_stderr(output):
            host = self.testing.FakeHost.in_process(plugin, timeout=5)
            try:
                host.initialize()
                host.send_command("main", "x")  # not announced: ignored without a word
                with self.assertRaises(self.testing.RpcError) as caught:
                    host.request("ui/command", {"origin": "menu", "menu_id": "m", "item_id": "x"})
                self.assertEqual(caught.exception.code, -32601)
                self.assertEqual(host.errors, [])
            finally:
                host.close()
        self.assertEqual(output.getvalue(), "")


class UpdateMenu(MenuCase):
    def test_sends_menu_update_on_a_current_host(self):
        sdk = self.sdk
        plugin = self.plugin()
        host = self.host(plugin)
        self.assertTrue(plugin.supports("menus") and plugin.supports("shortcuts"))
        changes = [sdk.menu_set("boards", title="Boards (2)", enabled=True),
                   {"id": "mode", "set": {"checked": False, "future": 1}}]
        self.assertTrue(plugin.update_menu("main", changes))
        self.assertEqual(host.wait_for("menu/update"), {"menu_id": "main", "items": [
            {"id": "boards", "set": {"title": "Boards (2)", "enabled": True}},
            {"id": "mode", "set": {"checked": False, "future": 1}}]})
        self.assertTrue(plugin.update_menu("tabs", {"describe": {"visible": False}}))
        self.assertEqual(host.wait_for("menu/update"),
                         {"menu_id": "tabs", "items": [{"id": "describe", "set": {"visible": False}}]})
        self.assertEqual(host.errors, [])

    def test_is_a_no_op_on_older_hosts(self):
        sdk = self.sdk
        for name, older, initialize in (
                ("E17a", False, dict(host_features=self.testing.E17A_HOST_FEATURES,
                                     host_feature_versions=self.testing.E17A_HOST_FEATURE_VERSIONS)),
                ("E14", True, {})):
            with self.subTest(host=name):
                plugin = self.plugin()
                host = self.host(plugin, older=older, **initialize)
                self.assertFalse(plugin.supports("menus") or plugin.supports("shortcuts"))
                self.assertFalse(plugin.update_menu("main", [sdk.menu_set("a", title="x")]))
                self.sync(host)
                self.assertEqual(host.notifications("menu/update"), [])

    def test_refuses_malformed_updates_on_every_host(self):
        plugin = self.plugin()
        self.host(plugin, older=True)
        for menu_id, items in (("", []), ("main", [{"set": {}}]), ("main", [{"id": "a", "set": "x"}]),
                               ("main", ["a"]), ("main", {"a": None}), ("main", {"": {}})):
            with self.subTest(menu_id=menu_id, items=items), self.assertRaises(ValueError):
                plugin.update_menu(menu_id, items)


class FakeHostHelpers(MenuCase):
    def test_log_selection(self):
        log_selection = self.testing.log_selection
        self.assertEqual(log_selection([(1, 3), [7, 7]]), {"ranges": [[1, 3], [7, 7]], "count": 4})
        self.assertEqual(log_selection([(1, 2)], count=100, text="x"),
                         {"ranges": [[1, 2]], "count": 100, "text": "x", "truncated": False})
        self.assertEqual(log_selection([], truncated=True), {"ranges": [], "count": 0, "truncated": True})

    def test_feature_sets(self):
        testing = self.testing
        self.assertEqual(testing.HOST_FEATURES[-2:], ("menus", "shortcuts"))
        self.assertEqual(testing.HOST_FEATURE_VERSIONS["menus"], 1)
        self.assertEqual(testing.HOST_FEATURE_VERSIONS["shortcuts"], 1)
        self.assertNotIn("menus", testing.E17A_HOST_FEATURES)
        self.assertEqual(set(testing.E17A_HOST_FEATURE_VERSIONS), set(testing.E17A_HOST_FEATURES))


if __name__ == "__main__":
    unittest.main()
