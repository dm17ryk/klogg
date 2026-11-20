"""Python SDK 2.0.0: negotiation, host queries and their lanes, forward and backward
compatibility, tooltips and icons (docs/plugin-sdk-advanced.md, sections 4, 5, 6, 9 and 16)."""
import base64
import contextlib
import copy
import importlib.util
import io
import pathlib
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = ROOT / "plugins/sdk/python"
QUERY_METHODS = ["tooltip/resolve", "menu/populate", "tree/children", "field/complete",
                 "custom/thing", "query/cancel"]
PNG = (b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00"
       b"\x1f\x15\xc4\x89\x00\x00\x00\rIDATx\x9cc\xf8\xcf\xc0\xf0\x1f\x00\x05\x00\x01\xff\x89\x99"
       b"=\x1d\x00\x00\x00\x00IEND\xaeB`\x82")
SVG = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 4 4"><rect width="4" height="4"/></svg>'


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, SDK / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SdkCase(unittest.TestCase):
    def setUp(self):
        self.sdk = load("advanced_test_sdk", "cilogg_plugin.py")
        self.testing = load("advanced_test_host", "cilogg_plugin_testing.py")

    def plugin(self, handles=QUERY_METHODS):
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

    def no_answer(self, host, identifier, wait=0.3):
        with self.assertRaises(TimeoutError):
            host.wait_response(identifier, wait)
        self.assertEqual(host.answers(identifier), [])


class Negotiation(SdkCase):
    def test_initialize_result_announces_sdk_and_no_features_without_handlers(self):
        plugin = self.plugin(handles=None)
        host = self.host(plugin)
        self.assertEqual(host.initialization["sdk"], {"name": "cilogg_plugin.py", "version": "2.0.0"})
        self.assertNotIn("features", host.initialization)
        self.assertEqual(plugin.features, [])
        self.assertEqual(self.sdk.SDK_VERSION, "2.0.0")

    def test_features_list_registered_query_methods_and_query_cancel(self):
        plugin = self.plugin()
        plugin.tooltip("main")(lambda query: None)
        plugin.query("tree/children")(lambda query: {"nodes": []})
        plugin.query("custom/thing")(lambda query: 1)
        host = self.host(plugin)
        self.assertEqual(host.initialization["features"],
                         ["custom/thing", "query/cancel", "tooltip/resolve", "tree/children"])
        self.assertEqual(plugin.handled_methods(), host.initialization["features"])

    def test_registration_refuses_host_methods(self):
        plugin = self.plugin()
        for method in ("invoke", "query/cancel", "view/event", ""):
            with self.subTest(method=method), self.assertRaises(ValueError):
                plugin.query(method)
        with self.assertRaises(ValueError):
            plugin.tooltip("")

    def test_supports_limits_and_version_on_a_current_host(self):
        plugin = self.plugin(handles=None)
        self.host(plugin)
        self.assertTrue(plugin.supports("views", 2))
        self.assertFalse(plugin.supports("views", 3))
        for feature in ("view_edit", "queries", "tooltips", "icons", "menus", "shortcuts",
                        "choices", "toggles", "field_kinds", "validation"):
            self.assertTrue(plugin.supports(feature), feature)
        self.assertFalse(plugin.supports("menus", 2))
        self.assertFalse(plugin.supports("choices", 2))
        self.assertFalse(plugin.supports("tree"))
        self.assertEqual(plugin.host_limits, dict(self.testing.HOST_LIMITS))
        self.assertEqual(plugin.host_version, self.testing.HOST_VERSION)
        self.assertEqual(plugin.host_feature_versions, dict(self.testing.HOST_FEATURE_VERSIONS))
        self.assertTrue(plugin.supports_views and plugin.supports_view_edit)

    def test_older_host_has_only_its_listed_features_at_version_one(self):
        plugin = self.plugin(handles=None)
        self.host(plugin, older=True)
        self.assertTrue(plugin.supports("views") and plugin.supports("view_edit"))
        self.assertFalse(plugin.supports("views", 2))
        self.assertFalse(plugin.supports("tooltips") or plugin.supports("icons")
                         or plugin.supports("queries"))
        self.assertEqual(plugin.host_limits, self.sdk.DEFAULT_HOST_LIMITS)
        self.assertIsNone(plugin.host_version)

    def test_host_without_any_feature_list(self):
        plugin = self.plugin(handles=None)
        self.host(plugin, older=True, host_features=None)
        self.assertFalse(plugin.supports("views"))
        self.assertFalse(plugin.supports_views)
        self.assertEqual(plugin.host_features, [])

    def test_versions_count_only_for_listed_features_and_limits_merge(self):
        plugin = self.plugin(handles=None)
        self.host(plugin, host_features=["views", "icons"],
                  host_feature_versions={"views": 2, "tooltips": 1, "icons": "3", "x": True},
                  host_limits={"max_elements": 100, "max_text_chars": "big", "max_future": 7})
        self.assertTrue(plugin.supports("views", 2))
        self.assertFalse(plugin.supports("tooltips"))  # a version alone does not announce it
        self.assertTrue(plugin.supports("icons"))
        self.assertFalse(plugin.supports("icons", 2))  # an invalid version counts as 1
        self.assertEqual(plugin.host_limits["max_elements"], 100)
        self.assertEqual(plugin.host_limits["max_text_chars"], 4096)
        self.assertEqual(plugin.host_limits["max_future"], 7)


class HostQueries(SdkCase):
    def test_tooltip_answers_dict_string_and_null(self):
        sdk = self.sdk
        plugin = self.plugin()

        @plugin.tooltip("main", "devices")
        def devices(query):
            return sdk.tooltip_content(f"Row {query.row_id}", title="Device",
                                       rows=[("Column", query.column_id)], tone="ok",
                                       icon=sdk.status_ref("ok"), footer="f", ttl_ms=1000)

        @plugin.tooltip("main")
        def others(query):
            return "plain text" if query.element_id == "status" else None

        host = self.host(plugin)
        self.assertEqual(host.resolve_tooltip("main", "devices", row_id="r7", column_id="state"),
                         {"title": "Device", "text": "Row r7", "rows": [["Column", "state"]],
                          "tone": "ok", "icon": "status:ok", "footer": "f", "ttl_ms": 1000})
        self.assertEqual(host.resolve_tooltip("main", "status"), {"text": "plain text"})
        self.assertIsNone(host.resolve_tooltip("main", "other"))
        self.assertIsNone(host.resolve_tooltip("elsewhere", "devices"))  # no handler: null

    def test_element_handler_wins_over_the_view_handler(self):
        plugin = self.plugin()
        plugin.tooltip("main")(lambda query: "view")
        plugin.tooltip("main", "a")(lambda query: "element")
        host = self.host(plugin)
        self.assertEqual(host.resolve_tooltip("main", "a")["text"], "element")
        self.assertEqual(host.resolve_tooltip("main", "b")["text"], "view")

    def test_generic_handler_is_the_tooltip_fallback(self):
        plugin = self.plugin()
        plugin.tooltip("main", "a")(lambda query: "element")
        plugin.query("tooltip/resolve")(lambda query: {"text": f"generic {query.element_id}"})
        host = self.host(plugin)
        self.assertEqual(host.resolve_tooltip("other", "z")["text"], "generic z")

    def test_query_object_carries_ids_context_and_deadline(self):
        seen = {}
        plugin = self.plugin()

        @plugin.tooltip("main", "tree")
        def handler(query):
            seen.update(view=query.view_id, element=query.element_id, window=query.window_id,
                        row=query.row_id, column=query.column_id, node=query.node_id,
                        key=query.tooltip_key, context=query.context, timeout=query.timeout_ms,
                        remaining=query.remaining(), deadline=query.deadline,
                        received=query.received, method=query.method, id=query.id,
                        thread=threading.current_thread().name)
            return None

        host = self.host(plugin)
        host.resolve_tooltip("main", "tree", window_id="w2", node_id="n1", tooltip_key="v3",
                             timeout_ms=1200, context={"tabId": "t3"})
        self.assertEqual((seen["view"], seen["element"], seen["window"], seen["node"], seen["key"]),
                         ("main", "tree", "w2", "n1", "v3"))
        self.assertIsNone(seen["row"])
        self.assertIsNone(seen["column"])
        self.assertEqual(seen["context"], {"tabId": "t3"})
        self.assertEqual(seen["timeout"], 1200)
        # The deadline itself, not the time left: a slow runner may start the handler after it.
        self.assertAlmostEqual(seen["deadline"] - seen["received"], 1.2, places=6)
        self.assertTrue(0 <= seen["remaining"] <= 1.2)
        self.assertEqual(seen["method"], "tooltip/resolve")
        self.assertTrue(seen["id"].startswith("q:"))
        self.assertTrue(seen["thread"].startswith("plugin-query-tooltip/resolve"))

    def test_generic_query_handler_result(self):
        plugin = self.plugin()

        @plugin.query("tree/children")
        def children(query):
            return {"nodes": [{"id": f"{query.params['node_id']}/a", "text": "a"}]}

        host = self.host(plugin)
        self.assertEqual(host.query("tree/children", {"view_id": "v", "element_id": "t",
                                                      "node_id": "root"}),
                         {"nodes": [{"id": "root/a", "text": "a"}]})

    def test_handler_failures_answer_error_32000(self):
        plugin = self.plugin()

        @plugin.tooltip("main", "boom")
        def boom(_query):
            raise ValueError("boom")

        plugin.tooltip("main", "number")(lambda _query: 42)
        plugin.query("custom/thing")(lambda _query: object())
        host = self.host(plugin)
        for element, message in (("boom", "boom"), ("number", "tooltip handler")):
            with self.subTest(element=element):
                with self.assertRaises(self.testing.RpcError) as caught:
                    host.resolve_tooltip("main", element)
                self.assertEqual(caught.exception.code, -32000)
                self.assertIn(message, caught.exception.message)
        with self.assertRaises(self.testing.RpcError) as caught:
            host.query("custom/thing")
        self.assertEqual(caught.exception.code, -32000)

    def test_unknown_requests_are_refused_and_unknown_notifications_ignored(self):
        plugin = self.plugin()
        plugin.tooltip("main")(lambda _query: "still here")
        host = self.host(plugin)
        host.notify("future/event", {"anything": [1, 2]})
        host.notify("view/future_event", {})
        for method in ("future/request", "menu/populate"):  # menu/populate: no handler
            with self.subTest(method=method):
                with self.assertRaises(self.testing.RpcError) as caught:
                    host.request(method, {"x": 1})
                self.assertEqual((caught.exception.code, caught.exception.message),
                                 (-32601, "Method not found"))
        self.assertEqual(host.resolve_tooltip("main", "a"), {"text": "still here"})
        self.assertEqual(host.errors, [])

    def test_without_handlers_tooltip_resolve_is_method_not_found(self):
        host = self.host(self.plugin(handles=None))
        with self.assertRaises(self.testing.RpcError) as caught:
            host.resolve_tooltip("main", "a")
        self.assertEqual(caught.exception.code, -32601)

    def test_cancel_of_an_unknown_query_is_ignored(self):
        plugin = self.plugin()
        plugin.tooltip("main")(lambda _query: "ok")
        host = self.host(plugin)
        host.notify("query/cancel", {"id": "q:999"})
        host.notify("query/cancel", {})
        self.assertEqual(host.resolve_tooltip("main", "a"), {"text": "ok"})


class Lanes(SdkCase):
    """Lanes of section 5.4: latest wins for tooltips and completion, FIFO for menus, two
    workers for tree children, all apart from jobs and view callbacks."""

    def gated(self, plugin, method):
        """Register a handler for `method` that blocks while the gate is closed."""
        gate = threading.Event()
        started = []
        started_event = threading.Event()

        def handler(query):
            started.append(query.params.get("n"))
            started_event.set()
            gate.wait(5)
            return {"n": query.params.get("n")}

        if method == "tooltip/resolve":
            plugin.tooltip("main")(handler)
        else:
            plugin.query(method)(handler)
        return gate, started, started_event

    def send(self, host, method, n, timeout_ms=5000):
        params = {"view_id": "main", "element_id": "e", "n": n}
        return host.send_query(method, params, timeout_ms=timeout_ms)

    def test_latest_wins_replaces_the_waiting_query(self):
        for method in ("tooltip/resolve", "field/complete"):
            with self.subTest(method=method):
                plugin = self.plugin()
                gate, started, started_event = self.gated(plugin, method)
                host = self.host(plugin)
                first = self.send(host, method, 1)
                self.assertTrue(started_event.wait(2))
                second = self.send(host, method, 2)
                third = self.send(host, method, 3)
                self.sync(host)  # the plugin has read both while the first one runs
                gate.set()
                self.assertEqual(host.wait_response(first, 2)["result"], {"n": 1})
                self.assertEqual(host.wait_response(third, 2)["result"], {"n": 3})
                self.no_answer(host, second)
                self.assertEqual(started, [1, 3])

    def test_fifo_lane_answers_every_query_in_order(self):
        plugin = self.plugin()
        gate, started, started_event = self.gated(plugin, "menu/populate")
        host = self.host(plugin)
        identifiers = [self.send(host, "menu/populate", n) for n in (1, 2, 3)]
        self.assertTrue(started_event.wait(2))
        gate.set()
        for n, identifier in enumerate(identifiers, 1):
            self.assertEqual(host.wait_response(identifier, 2)["result"], {"n": n})
        self.assertEqual(started, [1, 2, 3])

    def test_cancel_drops_a_waiting_query(self):
        plugin = self.plugin()
        gate, started, started_event = self.gated(plugin, "menu/populate")
        host = self.host(plugin)
        first = self.send(host, "menu/populate", 1)
        self.assertTrue(started_event.wait(2))
        second = self.send(host, "menu/populate", 2)
        host.notify("query/cancel", {"id": second})
        self.sync(host)
        gate.set()
        self.assertEqual(host.wait_response(first, 2)["result"], {"n": 1})
        self.no_answer(host, second)
        self.assertEqual(started, [1])

    def test_cancel_suppresses_the_answer_of_a_running_query(self):
        plugin = self.plugin()
        gate = threading.Event()
        running = threading.Event()
        observed = []

        @plugin.tooltip("main")
        def slow(query):
            running.set()
            gate.wait(5)
            try:
                query.check_cancelled()
                observed.append("running")
            except self.sdk.Cancelled:
                observed.append("cancelled")
            return "late"

        host = self.host(plugin)
        identifier = host.send_query("tooltip/resolve", {"view_id": "main", "element_id": "a"})
        self.assertTrue(running.wait(2))
        host.notify("query/cancel", {"id": identifier})
        self.sync(host)
        gate.set()
        self.no_answer(host, identifier)
        self.assertEqual(host.resolve_tooltip("main", "b"), {"text": "late"})
        self.assertEqual(observed, ["cancelled", "running"])

    def test_query_past_its_deadline_before_starting_is_dropped(self):
        plugin = self.plugin()
        gate, started, started_event = self.gated(plugin, "menu/populate")
        host = self.host(plugin)
        first = self.send(host, "menu/populate", 1)
        self.assertTrue(started_event.wait(2))
        late = self.send(host, "menu/populate", 2, timeout_ms=50)
        self.sync(host)
        time.sleep(0.2)  # the deadline passes while the first query runs
        gate.set()
        self.assertEqual(host.wait_response(first, 2)["result"], {"n": 1})
        self.no_answer(host, late)
        self.assertEqual(started, [1])

    def test_tree_children_run_on_two_workers(self):
        plugin = self.plugin()
        barrier = threading.Barrier(2, timeout=3)
        threads = []

        @plugin.query("tree/children")
        def children(query):
            threads.append(threading.current_thread().name)
            barrier.wait()  # only returns when two handlers run at the same time
            return {"nodes": [], "n": query.params["n"]}

        host = self.host(plugin)
        identifiers = [self.send(host, "tree/children", n) for n in (1, 2)]
        for n, identifier in enumerate(identifiers, 1):
            self.assertEqual(host.wait_response(identifier, 5)["result"]["n"], n)
        self.assertEqual(len(set(threads)), 2)

    def test_tooltips_are_not_delayed_by_tree_handlers_or_view_callbacks(self):
        plugin = self.plugin()
        gate = threading.Event()
        busy = threading.Semaphore(0)
        plugin.query("tree/children")(lambda _query: (busy.release(), gate.wait(5), {"nodes": []})[2])

        @plugin.on_view_event
        def blocked(_params):
            busy.release()
            gate.wait(5)

        plugin.tooltip("main")(lambda _query: "quick")
        host = self.host(plugin)
        self.addCleanup(gate.set)
        trees = [self.send(host, "tree/children", n) for n in (1, 2)]
        host.view_event("main", "button", "clicked")
        for _ in range(3):
            self.assertTrue(busy.acquire(timeout=2))
        started = time.monotonic()
        self.assertEqual(host.resolve_tooltip("main", "a", timeout=1), {"text": "quick"})
        self.assertLess(time.monotonic() - started, 1)
        gate.set()
        for identifier in trees:
            host.wait_response(identifier, 2)

    def test_shutdown_drops_running_queries(self):
        plugin = self.plugin()
        gate, _started, started_event = self.gated(plugin, "tooltip/resolve")
        host = self.host(plugin)
        self.addCleanup(gate.set)
        identifier = self.send(host, "tooltip/resolve", 1)
        self.assertTrue(started_event.wait(2))
        host.notify("shutdown", {})
        self.assertTrue(plugin.stopping.wait(2))
        gate.set()
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.answers(identifier), [])


class BackwardCompatibility(SdkCase):
    def tree(self):
        sdk = self.sdk
        return sdk.column(
            sdk.group("G", sdk.label("l", "Label", tooltip="static", tooltip_provider=True,
                                     tooltip_key="k1", accessible_name="Name",
                                     accessible_description="Description"), id="g"),
            sdk.table("t", [sdk.table_column("a", "A", tooltip="header", tooltip_provider=True,
                                             tooltip_key="c1")],
                      [sdk.table_row("r1", ["x"], tooltip="row", tooltip_provider=True,
                                     tooltip_key="r")],
                      tooltip_provider=True),
            id="body", accessible_name="Body")

    def sent_root(self, host, plugin, tree):
        plugin.update("main", tree, "w1")
        return host.wait_for("view/update")["root"]

    def test_older_host_gets_only_properties_it_knows(self):
        plugin = self.plugin(handles=None)
        host = self.host(plugin, older=True)
        tree = self.tree()
        original = copy.deepcopy(tree)
        root = self.sent_root(host, plugin, tree)
        self.assertEqual(tree, original)  # the caller's tree is not changed
        self.assertEqual(root["children"][0]["children"][0],
                         {"type": "label", "id": "l", "text": "Label", "tooltip": "static"})
        table = root["children"][1]
        self.assertNotIn("tooltip_provider", table)
        self.assertEqual(table["columns"], [{"id": "a", "title": "A"}])
        self.assertEqual(table["rows"], [{"id": "r1", "cells": ["x"], "tooltip": "row"}])
        self.assertNotIn("accessible_name", root)

    def test_older_host_patches_are_stripped_too(self):
        sdk = self.sdk
        plugin = self.plugin(handles=None)
        host = self.host(plugin, older=True)
        row = sdk.table_row("r1", ["x"], tooltip="row", tooltip_key="k")
        plugin.patch("main", [
            sdk.set_props("l", text="t", tooltip_key="k2", accessible_name="n"),
            sdk.set_props("t", columns=[sdk.table_column("a", tooltip="h")]),
            sdk.update_rows("t", [row]), sdk.insert_rows("t", [row], before="r0"),
            sdk.replace_rows("t", [row]), sdk.remove_rows("t", ["r9"]),
        ])
        patch = host.wait_for("view/update")["patch"]
        self.assertEqual(patch[0], {"id": "l", "set": {"text": "t"}})
        self.assertEqual(patch[1]["set"]["columns"], [{"id": "a", "title": "a"}])
        stripped = {"id": "r1", "cells": ["x"], "tooltip": "row"}
        self.assertEqual([patch[2]["update_rows"], patch[3]["insert_rows"], patch[4]["replace_rows"]],
                         [[stripped]] * 3)
        self.assertEqual(patch[3]["before"], "r0")
        self.assertEqual(patch[5], {"id": "t", "remove_rows": ["r9"]})
        self.assertEqual(row["tooltip_key"], "k")

    def test_current_host_gets_every_property(self):
        plugin = self.plugin(handles=None)
        host = self.host(plugin)
        tree = self.tree()
        self.assertEqual(self.sent_root(host, plugin, tree), tree)

    def test_tooltips_without_views_version_two(self):
        plugin = self.plugin(handles=None)
        host = self.host(plugin, host_features=["views", "view_edit", "tooltips"],
                         host_feature_versions={"views": 1, "tooltips": 1})
        root = self.sent_root(host, plugin, self.tree())
        label = root["children"][0]["children"][0]
        self.assertEqual(label["tooltip_key"], "k1")
        self.assertNotIn("accessible_name", label)
        self.assertNotIn("accessible_description", label)

    def test_new_notifications_are_no_ops_on_an_older_host(self):
        sdk = self.sdk
        plugin = self.plugin(handles=None)
        host = self.host(plugin, older=True)
        self.assertFalse(plugin.invalidate_tooltips("main", "t", row_ids=["r1"]))
        self.assertFalse(plugin.view("main").invalidate_tooltips())
        self.assertFalse(plugin.register_icons({"a": sdk.png_icon(PNG)}))
        self.sync(host)  # every earlier frame of the plugin has arrived
        self.assertEqual(host.notifications("tooltip/invalidate"), [])
        self.assertEqual(host.notifications("ui/icons"), [])

    def test_new_notifications_on_a_current_host(self):
        sdk = self.sdk
        plugin = self.plugin(handles=None)
        host = self.host(plugin)
        self.assertTrue(plugin.invalidate_tooltips("main", "t", row_ids=["r1"], window_id="w1"))
        self.assertEqual(host.wait_for("tooltip/invalidate"),
                         {"view_id": "main", "window_id": "w1", "element_id": "t", "row_ids": ["r1"]})
        self.assertTrue(plugin.view("main").invalidate_tooltips("tree", node_ids=["n1"]))
        self.assertEqual(host.wait_for("tooltip/invalidate"),
                         {"view_id": "main", "element_id": "tree", "node_ids": ["n1"]})
        self.assertTrue(plugin.register_icons({"a": sdk.png_icon(PNG, picture=True),
                                               "b": sdk.svg_icon(SVG, mask=True)}, remove=["old"]))
        icons = host.wait_for("ui/icons")
        self.assertEqual(base64.b64decode(icons["icons"]["a"]["png"]), PNG)
        self.assertTrue(icons["icons"]["a"]["picture"])
        self.assertEqual(icons["icons"]["b"], {"svg": SVG, "mask": True})
        self.assertEqual(icons["remove"], ["old"])


class Builders(SdkCase):
    def test_tooltip_content_and_references(self):
        sdk = self.sdk
        self.assertEqual(sdk.tooltip_content("t"), {"text": "t"})
        self.assertEqual(sdk.tooltip_content(rows=[("n", 1)], ttl_ms=0), {"rows": [["n", "1"]], "ttl_ms": 0})
        for arguments in ({"tone": "loud"}, {"ttl_ms": -1}, {"ttl_ms": 600001},
                          {"rows": [("a", "b")] * 33}, {"rows": [("a",)]}):
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                sdk.tooltip_content(**arguments)
        self.assertEqual(sdk.image_ref("chip"), "image:chip")
        self.assertEqual(sdk.builtin_ref("device"), "builtin:device")
        self.assertEqual(sdk.status_ref("warn"), "status:warn")
        self.assertEqual(sdk.themed_ref("a.svg", "b.svg"), {"light": "a.svg", "dark": "b.svg"})

    def test_icon_entries_are_validated(self):
        sdk = self.sdk
        plugin = self.plugin(handles=None)
        with self.assertRaises(ValueError):
            sdk.png_icon(b"GIF89a")
        with self.assertRaises(ValueError):
            sdk.svg_icon("<html/>")
        for entry in ({}, {"png": "x", "svg": SVG}):
            with self.subTest(entry=entry), self.assertRaises(ValueError):
                plugin.register_icons({"x": entry})
        with self.assertRaises(ValueError):
            plugin.register_icons({"": sdk.svg_icon(SVG)})
        with self.assertRaises(ValueError):
            plugin.invalidate_tooltips("main", row_ids=["r1"])

    def test_table_tooltip_properties(self):
        sdk = self.sdk
        self.assertEqual(sdk.table_column("a", tooltip="h", tooltip_provider=True, tooltip_key="k"),
                         {"id": "a", "title": "a", "tooltip": "h", "tooltip_provider": True,
                          "tooltip_key": "k"})
        self.assertEqual(sdk.table_row("r", [1], tooltip_provider=False, tooltip_key="k"),
                         {"id": "r", "cells": ["1"], "tooltip_provider": False, "tooltip_key": "k"})
        self.assertEqual(sdk.label("l", "x", accessible_name="n"),
                         {"type": "label", "id": "l", "text": "x", "accessible_name": "n"})


class HandlesWarning(SdkCase):
    def stderr_of(self, plugin):
        output = io.StringIO()
        with contextlib.redirect_stderr(output):
            host = self.testing.FakeHost.in_process(plugin, timeout=5)
            try:
                host.initialize()  # answered after the start-up check
            finally:
                host.close()
        return output.getvalue()

    def test_warns_about_handled_methods_missing_from_handles(self):
        plugin = self.plugin(handles=["tooltip/resolve"])
        plugin.tooltip("main")(lambda _query: None)
        plugin.query("tree/children")(lambda _query: None)
        text = self.stderr_of(plugin)
        self.assertIn("warning", text)
        self.assertIn("query/cancel, tree/children", text)
        self.assertNotIn("tooltip/resolve", text)

    def test_silent_when_handles_lists_everything(self):
        plugin = self.plugin(handles=["tooltip/resolve", "query/cancel"])
        plugin.tooltip("main")(lambda _query: None)
        self.assertEqual(self.stderr_of(plugin), "")
        self.assertEqual(self.stderr_of(self.plugin(handles=None)), "")

    def test_reads_handles_from_plugin_json(self):
        import json
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            manifest = pathlib.Path(folder) / "plugin.json"
            manifest.write_text(json.dumps({"id": "test", "capabilities": ["tool"],
                                            "handles": ["menu/populate"]}), encoding="utf-8")
            plugin = self.sdk.Plugin(str(manifest))
        plugin.query("menu/populate")(lambda _query: {"items": []})
        text = self.stderr_of(plugin)
        self.assertIn("query/cancel", text)
        self.assertNotIn("menu/populate", text)


if __name__ == "__main__":
    unittest.main()
