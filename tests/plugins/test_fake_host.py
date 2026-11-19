"""The fake host of cilogg_plugin_testing.py (docs/plugin-sdk.md, section "Testing plugins")."""
import importlib.util
import json
import pathlib
import shutil
import sys
import tempfile
import threading
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = ROOT / "plugins/sdk/python"


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, SDK / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PLUGIN_MAIN = '''import sys
from cilogg_plugin import Plugin

plugin = Plugin()


@plugin.action("hello")
def hello(job, arguments):
    print("hello on stderr", file=sys.stderr, flush=True)
    return {"greeting": "hi " + arguments.get("name", "")}


if __name__ == "__main__":
    plugin.serve()
'''


class Rogue:
    """Not an SDK plugin: writes frames CILogg would refuse."""

    def serve(self, stdin=None, stdout=None):
        for frame in (b'{"jsonrpc":"2.0","id":1,"result":{}}\n',
                      b'{"jsonrpc":"2.0","id":"x","method":"ask","params":{}}\n',
                      b'{"jsonrpc":"2.0","method":"note","params":[]}\n',
                      b'not json\n',
                      b'{"jsonrpc":"2.0","method":"fine","params":{}}\n'):
            stdout.write(frame)
        stdout.flush()
        stdin.readline()


class FakeHostTests(unittest.TestCase):
    def setUp(self):
        self.sdk = load("fake_host_test_sdk", "cilogg_plugin.py")
        self.testing = load("fake_host_test_host", "cilogg_plugin_testing.py")
        self.plugin = self.sdk.Plugin({"id": "test", "capabilities": ["tool"],
                                       "handles": ["tooltip/resolve", "query/cancel"]})

    def host(self, plugin=None):
        host = self.testing.FakeHost.in_process(plugin or self.plugin, timeout=5)
        self.addCleanup(host.close)
        return host

    def test_initialize_announces_a_current_host(self):
        host = self.host()
        result = host.initialize({"prefix": "x"})
        self.assertEqual(result["plugin_id"], "test")
        sent = host.frames[0]
        self.assertEqual((sent.direction, sent.method, sent.message["id"]),
                         (self.testing.TO_PLUGIN, "initialize", "initialize"))
        params = sent.message["params"]
        self.assertEqual(params["host_features"], ["views", "view_edit", "queries", "tooltips", "icons",
                                                   "menus", "shortcuts", "choices", "toggles",
                                                   "field_kinds", "validation", "rich_tables",
                                                   "element_menus"])
        self.assertEqual(params["host_feature_versions"],
                         {"views": 2, "view_edit": 1, "queries": 1, "tooltips": 1, "icons": 1,
                          "menus": 1, "shortcuts": 1, "choices": 1, "toggles": 1,
                          "field_kinds": 1, "validation": 1, "rich_tables": 1,
                          "element_menus": 1})
        self.assertEqual(params["host_limits"]["max_table_cells"], 50000)
        self.assertEqual(params["host_version"], self.testing.HOST_VERSION)
        self.assertEqual(params["configuration"], {"prefix": "x"})
        received = host.frames[1]
        self.assertEqual((received.direction, received.method, received.message["id"]),
                         (self.testing.FROM_PLUGIN, None, "initialize"))
        self.assertEqual(self.plugin.configuration, {"prefix": "x"})

    def test_initialize_as_an_older_host(self):
        host = self.host()
        host.initialize_older_host()
        params = host.frames[0].message["params"]
        self.assertEqual(set(params), {"protocol_version", "plugin_id", "configuration",
                                       "max_message_bytes", "max_pending_bytes", "host_features"})
        self.assertEqual(params["host_features"], ["views", "view_edit"])
        other = self.testing.FakeHost.in_process(
            self.sdk.Plugin({"id": "test", "capabilities": ["tool"]}), timeout=5)
        self.addCleanup(other.close)
        other.initialize_older_host(host_features=None)
        self.assertNotIn("host_features", other.frames[0].message["params"])

    def test_invoke_answers_streams_and_keeps_panels_and_progress(self):
        @self.plugin.action("produce")
        def produce(job, arguments):
            with job.stream() as stream:
                stream.write([f"line {n}" for n in range(arguments["count"])])
                stream.write(["last"])
            job.progress(0.5)
            job.panel("summary", "Summary", [{"type": "text", "text": "done"}])
            return {"written": arguments["count"] + 1}

        @self.plugin.action("fail")
        def fail(_job, _arguments):
            raise RuntimeError("no device")

        host = self.host()
        host.initialize()
        self.assertEqual(host.invoke("produce", {"count": 3}), {"written": 4})
        self.assertEqual(host.records, ["line 0", "line 1", "line 2", "last"])
        self.assertEqual(host.progress[0]["value"], 0.5)
        self.assertEqual(host.panels[0]["id"], "summary")
        self.assertEqual([params["sequence"] for params in host.notifications("stream/data")], [0, 1])
        acknowledgements = [frame.message["params"]["sequence"] for frame in host.frames
                            if frame.method == "stream/ack"]
        self.assertEqual(acknowledgements, [0, 1])
        with self.assertRaises(self.testing.RpcError) as caught:
            host.invoke("fail")
        self.assertEqual((caught.exception.code, caught.exception.message), (-32000, "no device"))
        self.assertEqual(host.errors, [])

    def test_e17c_helpers_complete_fields_and_change_fields(self):
        events = []
        changed = threading.Event()
        plugin = self.sdk.Plugin({"id": "test", "capabilities": ["tool"],
                                  "handles": ["field/complete", "query/cancel"]})

        @plugin.complete("main", "board")
        def board(query):
            return [name for name in ("Board 1", "Board 2", "Other") if name.startswith(query.text)]

        @plugin.on_view_event
        def event(params):
            events.append(params)
            changed.set()

        host = self.host(plugin)
        host.initialize()
        self.assertEqual(host.complete_field("main", "board", "Bo"),
                         {"items": [{"value": "Board 1"}, {"value": "Board 2"}]})
        self.assertIsNone(host.complete_field("main", "other", "Bo"))
        host.change_field("main", "label", "bad name", valid=False)
        self.assertTrue(changed.wait(2))
        self.assertEqual((events[0]["type"], events[0]["valid"], events[0]["inputs"]),
                         ("changed", False, {"label": "bad name"}))
        self.assertEqual(host.errors, [])

    def test_query_timeout_sends_query_cancel(self):
        gate = threading.Event()
        self.addCleanup(gate.set)

        @self.plugin.tooltip("main")
        def slow(_query):
            gate.wait(5)
            return "too late"

        host = self.host()
        host.initialize()
        with self.assertRaises(self.testing.QueryTimeout) as caught:
            host.resolve_tooltip("main", "a", timeout=0.2)
        self.assertTrue(isinstance(caught.exception, TimeoutError))
        last = host.frames[-1]
        self.assertEqual((last.method, last.message["params"]), ("query/cancel", {"id": caught.exception.id}))
        with self.assertRaises(self.testing.RpcError):
            host.request("sync/point")  # the plugin has read the cancellation
        gate.set()
        self.assertEqual(host.resolve_tooltip("main", "b"), {"text": "too late"})
        self.assertEqual(host.answers(caught.exception.id), [])  # the plugin suppressed it

    def test_views_messages_and_wait_for(self):
        @self.plugin.on_view_opened
        def opened(params):
            self.plugin.update(params["view_id"], self.sdk.column(id="body"), params["window_id"])

        @self.plugin.on_view_event
        def event(params):
            self.plugin.patch(params["view_id"], [self.sdk.set_props("s", text=params["type"])])
            self.plugin.message("Sure?", buttons=("yes", "no"), message_id="m1",
                                callback=lambda result: self.plugin.patch(
                                    "main", [self.sdk.set_props("s", text=result["button"])]))

        host = self.host()
        host.initialize()
        host.open_view("main", window_id="w9")
        host.view_event("main", "go", "clicked")
        first_patch = host.wait_for("view/update", lambda params: "patch" in params)
        self.assertEqual(first_patch["patch"][0]["set"]["text"], "clicked")
        self.assertEqual(host.wait_for("view/update")["window_id"], "w9")  # the older frame stayed
        self.assertEqual(host.wait_for("ui/message")["message_id"], "m1")
        host.message_result("m1", "yes")
        self.assertEqual(host.wait_for("view/update")["patch"][0]["set"]["text"], "yes")
        self.assertEqual(len(host.notifications("view/update")), 3)
        with self.assertRaises(TimeoutError):
            host.wait_for("view/update", timeout=0.1)
        host.close_view("main", window_id="w9")

    def test_send_frame_sends_anything_and_detects_a_failed_plugin(self):
        host = self.host()
        host.initialize()
        host.send_frame({"jsonrpc": "2.0", "method": "future/notification", "params": {}})
        with self.assertRaises(self.testing.RpcError):
            host.request("future/request")
        host.send_frame(b"not json\n")
        with self.assertRaises(self.testing.PluginExited):
            host.request("future/request", timeout=2)
        self.assertEqual(host.frames[-2].raw, b"not json\n")
        self.assertEqual(host.frames[-2].message, {})

    def test_refused_initialization_ends_the_plugin(self):
        host = self.host()
        with self.assertRaises(self.testing.PluginExited):
            host.initialize(plugin_id="someone.else")

    def test_envelope_violations_are_reported(self):
        host = self.host(Rogue())
        host.wait_for("fine")
        self.assertEqual(len(host.errors), 4, host.errors)
        self.assertIn("response", host.errors[0])
        self.assertIn("sent a request", host.errors[1])
        self.assertIn("notification", host.errors[2])
        self.assertIn("Malformed", host.errors[3])

    def test_subprocess_plugin_from_its_folder(self):
        with tempfile.TemporaryDirectory() as folder:
            package = pathlib.Path(folder) / "hello"
            package.mkdir()
            (package / "plugin.json").write_text(json.dumps({
                "id": "org.example.hello", "name": "Hello", "version": "1.0.0", "protocol_version": 1,
                "capabilities": ["tool"], "executable": "python", "arguments": ["main.py"],
                "actions": [{"id": "hello", "label": "Hello", "capability": "tool", "fields": []}]}),
                encoding="utf-8")
            (package / "main.py").write_text(PLUGIN_MAIN, encoding="utf-8")
            shutil.copy2(SDK / "cilogg_plugin.py", package)
            for start in (lambda: self.testing.FakeHost.start_package(package, timeout=10),
                          lambda: self.testing.FakeHost.start([sys.executable, "main.py"], package,
                                                              timeout=10)):
                with self.subTest(start=start), start() as host:
                    self.assertEqual(host.initialize()["actions"][0]["id"], "hello")
                    self.assertEqual(host.invoke("hello", {"name": "Ann"}), {"greeting": "hi Ann"})
                    self.assertEqual(host.shutdown(), 0)
                    self.assertIn("hello on stderr", host.stderr)
                    self.assertEqual(host.errors, [])


if __name__ == "__main__":
    unittest.main()
