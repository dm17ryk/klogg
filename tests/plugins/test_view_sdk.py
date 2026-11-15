"""Python SDK plugin-view helpers and the view-demo example (docs/plugin-views.md)."""
import importlib.util
import json
import pathlib
import queue
import shutil
import sys
import tempfile
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from test_python_plugins import Host  # noqa: E402


def load_sdk():
    spec = importlib.util.spec_from_file_location("view_test_sdk", ROOT / "plugins/sdk/python/cilogg_plugin.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ViewSdk(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.sdk = load_sdk()
        manifest = pathlib.Path(self.temporary.name) / "plugin.json"
        manifest.write_text(json.dumps({"id": "test", "capabilities": ["tool"]}), encoding="utf-8")
        self.plugin = self.sdk.Plugin(str(manifest))
        self.sent = []
        self.plugin.send = self.sent.append

    def test_builders_omit_unset_properties(self):
        sdk = self.sdk
        tree = sdk.column(
            sdk.label("title", "Hello", tone="ok"),
            sdk.field("path", "file_open", label="Path", filter="Logs (*.log)"),
            sdk.button("go", "Go", action="run", confirm={"text": "Sure?"}),
            sdk.progress("bar", busy=True),
            sdk.table("rows", [sdk.table_column("a", "A", 80)], [sdk.table_row("r1", [1, "x"], tone="warn")],
                      selection="single"),
            id="body")
        self.assertEqual(tree["type"], "column")
        self.assertEqual(tree["id"], "body")
        title, path, go, bar, table = tree["children"]
        self.assertEqual(title, {"type": "label", "id": "title", "text": "Hello", "tone": "ok"})
        self.assertEqual(path["kind"], "file_open")
        self.assertNotIn("value", path)
        self.assertEqual(go["confirm"], {"text": "Sure?"})
        self.assertNotIn("event", go)
        self.assertEqual(bar, {"type": "progress", "id": "bar", "busy": True})
        self.assertEqual(table["columns"], [{"id": "a", "title": "A", "width": 80}])
        self.assertEqual(table["rows"], [{"id": "r1", "cells": ["1", "x"], "tone": "warn"}])
        self.assertEqual(sdk.group("G", sdk.badge("b", "B"))["children"][0]["type"], "badge")

    def test_editable_cells_helpers(self):
        sdk = self.sdk
        col = sdk.table_column("file", "File", 150, editable=True)
        self.assertEqual(col, {"id": "file", "title": "File", "width": 150, "editable": True})
        ed = sdk.editor("choice", choices=[{"value": "1", "label": "On"}])
        row = sdk.table_row("r1", ["a", 1], tone="accent", tooltip="was 0", editors={"file": ed})
        self.assertEqual(row, {"id": "r1", "cells": ["a", "1"], "tone": "accent", "tooltip": "was 0",
                               "editors": {"file": {"kind": "choice",
                                                    "choices": [{"value": "1", "label": "On"}]}}})
        self.assertEqual(sdk.editor("number", min=1, max=9), {"kind": "number", "min": 1, "max": 9})
        self.assertEqual(sdk.editor("multichoice", choices=[{"value": "1", "label": "A"}], value=["1"]),
                         {"kind": "multichoice", "choices": [{"value": "1", "label": "A"}], "value": ["1"]})
        self.assertEqual(sdk.editor("text", max_length=5, placeholder="p"),
                         {"kind": "text", "max_length": 5, "placeholder": "p"})

    def test_supports_view_edit(self):
        plugin = self.sdk.Plugin.__new__(self.sdk.Plugin)
        plugin.host_features = ["views", "view_edit"]
        self.assertTrue(plugin.supports_view_edit)
        plugin.host_features = ["views"]
        self.assertFalse(plugin.supports_view_edit)

    def test_patch_entries(self):
        sdk = self.sdk
        self.assertEqual(sdk.set_props("x", text="t"), {"id": "x", "set": {"text": "t"}})
        self.assertEqual(sdk.insert_rows("t", [], before="r1"), {"id": "t", "insert_rows": [], "before": "r1"})
        self.assertEqual(sdk.remove_rows("t", ["a"]), {"id": "t", "remove_rows": ["a"]})
        self.assertEqual(sdk.update_rows("t", [])["update_rows"], [])
        self.assertEqual(sdk.replace_rows("t", [])["replace_rows"], [])

    def test_update_patch_show_hide_frames(self):
        sdk = self.sdk
        self.plugin.update("main", sdk.column(id="body"))
        self.plugin.patch("main", [sdk.set_props("status", text="x")], window_id="w2")
        self.plugin.show_view("dialog", area="left", focus=False)
        self.plugin.hide_view("dialog")
        self.plugin.view("main").patch("status", text="y")
        methods = [frame["method"] for frame in self.sent]
        self.assertEqual(methods, ["view/update", "view/update", "view/show", "view/hide", "view/update"])
        self.assertNotIn("window_id", self.sent[0]["params"])
        self.assertEqual(self.sent[1]["params"]["window_id"], "w2")
        self.assertEqual(self.sent[2]["params"], {"view_id": "dialog", "area": "left", "focus": False})
        self.assertEqual(self.sent[4]["params"]["patch"], [{"id": "status", "set": {"text": "y"}}])
        for frame in self.sent:
            self.assertNotIn("id", frame)  # notifications only; the plugin never sends requests

    def test_message_wait_and_callback(self):
        results = []
        done = threading.Event()

        def callback(result):
            results.append((result, threading.current_thread().name))
            done.set()

        handle = self.plugin.message("Proceed?", buttons=("yes", "no"), default="no",
                                     timeout_ms=5000, callback=callback, message_id="m1")
        frame = self.sent[-1]
        self.assertEqual(frame["method"], "ui/message")
        self.assertEqual(frame["params"]["buttons"], ["yes", "no"])
        self.assertEqual(frame["params"]["timeout_ms"], 5000)
        self.assertTrue(frame["params"]["modal"])
        self.assertIsNone(handle.wait(0.05))
        self.plugin.initialized = True
        self.plugin._dispatch({"jsonrpc": "2.0", "method": "ui/message_result",
                               "params": {"message_id": "m1", "button": "yes", "timed_out": False}})
        self.assertEqual(handle.wait(1)["button"], "yes")
        self.assertTrue(done.wait(2))
        self.assertEqual(results[0][1], "plugin-view-callbacks")
        handle.close()
        self.assertEqual(self.sent[-1], {"jsonrpc": "2.0", "method": "ui/close_message",
                                         "params": {"message_id": "m1"}})
        with self.assertRaises(ValueError):
            self.plugin.message("x", buttons=("maybe",))

    def test_view_callbacks_run_in_order_off_the_reader(self):
        seen = []
        finished = threading.Event()

        @self.plugin.on_view_opened
        def opened(params):
            seen.append(("opened", params["view_id"], threading.current_thread().name))

        @self.plugin.on_view_event
        def event(params):
            time.sleep(0.01)
            seen.append(("event", params["element_id"], threading.current_thread().name))

        @self.plugin.on_view_closed
        def closed(_params):
            seen.append(("closed", None, threading.current_thread().name))
            finished.set()

        self.plugin.initialized = True
        reader = threading.current_thread().name
        for frame in ({"method": "view/opened", "params": {"view_id": "main", "window_id": "w"}},
                      {"method": "view/event", "params": {"view_id": "main", "element_id": "a"}},
                      {"method": "view/event", "params": {"view_id": "main", "element_id": "b"}},
                      {"method": "view/closed", "params": {"view_id": "main", "window_id": "w"}}):
            self.plugin._dispatch(dict(frame, jsonrpc="2.0"))
        self.assertTrue(finished.wait(2))
        self.assertEqual([entry[:2] for entry in seen],
                         [("opened", "main"), ("event", "a"), ("event", "b"), ("closed", None)])
        self.assertTrue(all(entry[2] != reader for entry in seen))

    def test_initialize_records_host_features(self):
        self.plugin._dispatch({"jsonrpc": "2.0", "id": "initialize", "method": "initialize",
                               "params": {"protocol_version": 1, "plugin_id": "test",
                                          "host_features": ["views"]}})
        self.assertTrue(self.plugin.supports_views)
        self.assertEqual(self.sent[-1]["id"], "initialize")


class ViewDemoExample(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        package = self.root / "view-demo"
        shutil.copytree(ROOT / "plugins/examples/view-demo", package)
        shutil.copy2(ROOT / "plugins/sdk/python/cilogg_plugin.py", package)
        self.host = Host(package)
        self.addCleanup(self.host.close)

    def next_frame(self, method, predicate=lambda _params: True, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                frame = self.host.inbox.get(timeout=max(0.01, deadline - time.monotonic()))
            except queue.Empty:
                break
            if frame.get("method") == method and predicate(frame.get("params", {})):
                return frame["params"]
        self.fail(f"No {method} frame")

    def test_manifest_declares_views_and_is_disabled_by_default(self):
        manifest = json.loads((ROOT / "plugins/examples/view-demo/plugin.json").read_text(encoding="utf-8"))
        self.assertFalse(manifest.get("enabled_by_default", False))
        kinds = {view["id"]: view.get("kind", "dock") for view in manifest["views"]}
        self.assertEqual(kinds, {"main": "dock", "editor": "window", "confirm": "dialog"})

    def test_dock_tree_events_messages_and_jobs(self):
        host = self.host
        host.send("view/opened", {"view_id": "main", "window_id": "w1", "context": {}})
        update = self.next_frame("view/update", lambda params: "root" in params)
        self.assertEqual(update["window_id"], "w1")
        encoded = json.dumps(update)
        self.assertLess(len(encoded.encode("utf-8")), 1024 * 1024)
        tables = []

        def collect(element):
            if element.get("type") == "table":
                tables.append(element)
            for child in element.get("children", []):
                collect(child)

        collect(update["root"])
        big = next(item for item in tables if item["id"] == "rows")
        self.assertEqual(len(big["rows"]), 10000)
        self.assertLessEqual(len(big["rows"]) * len(big["columns"]), 50000)
        settings = next(item for item in tables if item["id"] == "settings")
        self.assertEqual([r["id"] for r in settings["rows"]], ["level", "days", "count", "enabled"])
        self.assertTrue(settings["columns"][1]["editable"])
        # The table is then patched one row at a time.
        patch = self.next_frame("view/update", lambda params: any(
            "update_rows" in entry for entry in params.get("patch", [])))
        self.assertEqual(len(patch["patch"][0]["update_rows"]), 1)
        def edit(row_id, value, previous="x"):
            host.send("view/event", {"view_id": "main", "window_id": "w1", "element_id": "settings",
                                     "type": "edited", "row_id": row_id, "column_id": "value",
                                     "value": value, "previous": previous, "inputs": {}})
            return self.next_frame("view/update", lambda params: any(
                entry.get("id") == "settings" for entry in params.get("patch", [])))["patch"][0]["update_rows"][0]

        accepted = edit("count", "42", "10")
        self.assertEqual((accepted["cells"][1], accepted["tone"], accepted["tooltip"]), ("42", "accent", "was 10"))
        rejected = edit("count", "500", "42")
        self.assertEqual((rejected["cells"][1], rejected["tone"]), ("42", "error"))
        self.assertIn("between 1 and 100", rejected["tooltip"])
        self.assertEqual(edit("days", ["Fri", "Mon"])["cells"][1], "Mon, Fri")
        self.assertEqual(edit("level", "Nope")["tone"], "error")
        self.assertEqual(edit("enabled", False)["cells"][1], "off")
        host.send("view/event", {"view_id": "main", "window_id": "w1", "element_id": "ask",
                                 "type": "clicked", "value": None, "inputs": {}})
        message = self.next_frame("ui/message")
        self.assertEqual(message["buttons"], ["yes", "no"])
        host.send("ui/message_result", {"message_id": message["message_id"], "button": "yes",
                                        "timed_out": False})
        self.next_frame("view/update", lambda params: any(
            entry.get("set", {}).get("text") == "Answer: yes" for entry in params.get("patch", [])))
        host.send("view/event", {"view_id": "main", "window_id": "w1", "element_id": "notice",
                                 "type": "clicked", "value": None, "inputs": {}})
        self.assertEqual(self.next_frame("ui/message")["timeout_ms"], 5000)
        host.send("view/event", {"view_id": "main", "window_id": "w1", "element_id": "open_dialog",
                                 "type": "clicked", "value": None, "inputs": {}})
        self.assertEqual(self.next_frame("view/show")["view_id"], "confirm")
        output = self.root / "rows.csv"
        result = host.invoke("export", {"save_path": str(output), "inputs": {}})
        self.assertEqual(result["rows"], 10000)
        self.assertEqual(len(output.read_text(encoding="utf-8").splitlines()), 10001)
        result = host.invoke("process", {"inputs": {"name": "dev", "mode": "fast"}})
        self.assertEqual(result["inputs"]["name"], "dev")
        host.send("view/closed", {"view_id": "main", "window_id": "w1"})


if __name__ == "__main__":
    unittest.main()
