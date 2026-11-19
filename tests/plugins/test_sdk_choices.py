"""Python SDK choices (E17c): choice builders, check boxes and radio buttons, new field kinds,
validation, their downgrades on older hosts, field/complete handlers and the fake host's helpers
(docs/plugin-sdk-advanced.md, sections 4, 5 and 8)."""
import contextlib
import copy
import importlib.util
import io
import pathlib
import queue
import threading
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = ROOT / "plugins/sdk/python"
HANDLES = ["field/complete", "query/cancel"]
E17C_FEATURES = ("choices", "toggles", "field_kinds", "validation")
E14_FIELD_KINDS = {"text", "number", "choice", "multichoice", "boolean", "file_open", "file_save",
                   "directory"}


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


class ChoiceCase(unittest.TestCase):
    def setUp(self):
        self.sdk = load("choices_test_sdk", "cilogg_plugin.py")
        self.testing = load("choices_test_host", "cilogg_plugin_testing.py")

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

    def sent(self, host, method):
        frames = [frame for frame in host.frames
                  if frame.direction == self.testing.TO_PLUGIN and frame.method == method]
        return frames[-1].message["params"]


class Builders(ChoiceCase):
    def test_choice_objects_separators_and_sections(self):
        sdk = self.sdk
        self.assertEqual(sdk.choice("auto", "Automatic", icon="builtin:refresh",
                                    tooltip="Detect the speed", enabled=True),
                         {"value": "auto", "label": "Automatic", "icon": "builtin:refresh",
                          "tooltip": "Detect the speed", "enabled": True})
        self.assertEqual(sdk.choice(9600, "9600", enabled=False),
                         {"value": 9600, "label": "9600", "enabled": False})
        self.assertEqual(sdk.choice(1.5), {"value": 1.5})
        self.assertEqual(sdk.choice_separator(), {"type": "separator"})
        self.assertEqual(sdk.choice_section("Fixed speeds"), {"type": "section", "label": "Fixed speeds"})
        for value in (True, None, [1], {"a": 1}):
            with self.subTest(value=value), self.assertRaises(ValueError):
                sdk.choice(value)
        with self.assertRaises(ValueError):
            sdk.choice_section("")

    def test_field_keeps_its_earlier_signature(self):
        sdk = self.sdk
        self.assertEqual(sdk.field("a", "text", "A", "v", None, None, "ph", tooltip="t"),
                         {"type": "field", "id": "a", "kind": "text", "label": "A", "value": "v",
                          "placeholder": "ph", "tooltip": "t"})
        self.assertEqual(sdk.field("f", "file_open", filter="Logs (*.log)", future_key=1),
                         {"type": "field", "id": "f", "kind": "file_open",
                          "filter": "Logs (*.log)", "future_key": 1})

    def test_choice_field_keys(self):
        sdk = self.sdk
        choices = [sdk.choice("a", "A"), sdk.choice_separator(), "b"]
        self.assertEqual(
            sdk.field("speed", "choice", label="Speed", value="a", choices=choices,
                      indicator="radio", searchable=True),
            {"type": "field", "id": "speed", "kind": "choice", "label": "Speed", "value": "a",
             "choices": choices, "indicator": "radio", "searchable": True})
        self.assertEqual(sdk.field("d", "choice", choices=["x"], style="radio",
                                   orientation="horizontal", columns=3)["style"], "radio")
        self.assertEqual(sdk.field("m", "multichoice", choices=["x"], style="checkboxes",
                                   value=["x"])["value"], ["x"])
        editable = sdk.field("b", "choice", choices=["x"], editable=True, completion=True)
        self.assertEqual((editable["editable"], editable["completion"]), (True, True))
        self.assertTrue(sdk.field("t", "text", completion=True)["completion"])
        self.assertIsNot(sdk.field("c", "choice", choices=choices)["choices"], choices)

    def test_new_field_kinds_and_validation_keys(self):
        sdk = self.sdk
        self.assertEqual(sdk.field("notes", "multiline", rows=4, monospace=True),
                         {"type": "field", "id": "notes", "kind": "multiline", "rows": 4,
                          "monospace": True})
        self.assertEqual(sdk.field("n", "integer", value=3, min=0, max=10, step=1),
                         {"type": "field", "id": "n", "kind": "integer", "value": 3, "min": 0,
                          "max": 10, "step": 1})
        self.assertEqual(sdk.field("l", "text", required=True, pattern="[a-z]+", min_length=1,
                                   max_length=8, error=""),
                         {"type": "field", "id": "l", "kind": "text", "required": True,
                          "pattern": "[a-z]+", "min_length": 1, "max_length": 8, "error": ""})
        self.assertEqual(sorted(sdk.FIELD_KIND_FALLBACKS),
                         ["color", "date", "datetime", "integer", "multiline", "secret", "slider",
                          "time"])
        self.assertEqual(set(sdk.E14_FIELD_KINDS), E14_FIELD_KINDS)
        self.assertEqual(sdk.FIELD_KINDS, sdk.E14_FIELD_KINDS + tuple(sdk.FIELD_KIND_FALLBACKS))

    def test_field_refuses_the_shapes_the_host_refuses(self):
        sdk = self.sdk
        cases = (
            ("text", dict(indicator="radio")), ("choice", dict(indicator="check")),
            ("choice", dict(style="checkboxes")), ("multichoice", dict(style="radio")),
            ("text", dict(style="dropdown")), ("text", dict(searchable=True)),
            ("multichoice", dict(editable=True)), ("choice", dict(completion=True)),
            ("number", dict(completion=True)), ("choice", dict(orientation="diagonal")),
            ("choice", dict(columns=0)), ("choice", dict(columns=13)),
            ("choice", dict(columns=True)), ("choice", dict(columns=2.5)),
            ("text", dict(pattern="x" * 257)), ("integer", dict(min=5, max=1)),
            ("text", dict(min_length=4, max_length=2)), ("text", dict(min_length=-1)),
            ("slider", dict(step=0)), ("multiline", dict(rows=0)),
            # Integer and slider bounds and steps, rows and length rules are whole numbers.
            ("slider", dict(step=0.5)), ("integer", dict(min=0.5)), ("integer", dict(max=9.9)),
            ("integer", dict(step=True)), ("multiline", dict(rows=2.5)),
            ("text", dict(min_length=0.5)), ("text", dict(max_length=1.5)),
            ("number", dict(min=float("nan"))), ("number", dict(max=float("inf"))),
            ("number", dict(min="1")), ("integer", dict(max=float("inf"))),
        )
        for kind, keywords in cases:
            with self.subTest(kind=kind, keywords=keywords), self.assertRaises(ValueError):
                sdk.field("x", kind, **keywords)
        self.assertEqual(sdk.field("x", "text", pattern="x" * 256)["pattern"], "x" * 256)
        # Fractions stay allowed on number fields; whole floats on integer fields.
        self.assertEqual(sdk.field("x", "number", min=0.5, max=1.5, step=0.25)["step"], 0.25)
        self.assertEqual(sdk.field("x", "integer", min=0.0, max=10.0, step=2.0)["max"], 10.0)
        self.assertNotIn("editable", sdk.field("x", "text", editable=False, searchable=False))

    def test_checkbox_radio_group_and_check_group(self):
        sdk = self.sdk
        self.assertEqual(sdk.checkbox("verbose", "Verbose"),
                         {"type": "checkbox", "id": "verbose", "text": "Verbose", "value": False})
        partly = sdk.checkbox("all", "All", value=None, tristate=True, tone="warn", tooltip="t")
        self.assertEqual(partly, {"type": "checkbox", "id": "all", "text": "All", "tristate": True,
                                  "tone": "warn", "tooltip": "t", "value": None})
        with self.assertRaises(ValueError):
            sdk.checkbox("x", "X", value=None)
        choices = [sdk.choice("auto", "Automatic"), sdk.choice("manual", "Manual")]
        self.assertEqual(sdk.radio_group("mode", choices, "auto", title="Mode",
                                         orientation="horizontal", columns=2),
                         {"type": "radio_group", "id": "mode", "title": "Mode", "choices": choices,
                          "value": "auto", "orientation": "horizontal", "columns": 2})
        self.assertEqual(sdk.check_group("logs", ["boot", "radio"], ("boot",), all_none=True),
                         {"type": "check_group", "id": "logs", "choices": ["boot", "radio"],
                          "value": ["boot"], "all_none": True})
        self.assertEqual(sdk.check_group("none", ["a"]),
                         {"type": "check_group", "id": "none", "choices": ["a"]})
        for build in (lambda: sdk.radio_group("r", ["a"], orientation="up"),
                      lambda: sdk.check_group("c", ["a"], columns=20),
                      lambda: sdk.check_group("c", ["a"], value="a")):
            with self.assertRaises(ValueError):
                build()

    def test_button_requires_valid(self):
        sdk = self.sdk
        self.assertTrue(sdk.button("go", "Go", event=True, requires_valid=True)["requires_valid"])
        self.assertEqual(sdk.button("go", "Go", event=True,
                                    requires_valid=("label", "retries"))["requires_valid"],
                         ["label", "retries"])
        self.assertNotIn("requires_valid", sdk.button("go", "Go", event=True, requires_valid=False))
        for value in ("label", ["label", ""], [1]):
            with self.subTest(value=value), self.assertRaises(ValueError):
                sdk.button("go", "Go", requires_valid=value)

    def test_completion_item(self):
        sdk = self.sdk
        self.assertEqual(sdk.completion_item("Board 1"), {"value": "Board 1"})
        self.assertEqual(sdk.completion_item("b1", "Board 1", icon="status:ok"),
                         {"value": "b1", "label": "Board 1", "icon": "status:ok"})
        with self.assertRaises(ValueError):
            sdk.completion_item(3)


class Downgrades(ChoiceCase):
    """update() and patch() send what the host knows (docs/plugin-sdk-advanced.md, section 8)."""

    def tree(self):
        sdk = self.sdk
        speeds = [sdk.choice("auto", "Automatic", icon="builtin:refresh", tooltip="Detect"),
                  sdk.choice_separator(), sdk.choice_section("Fixed speeds"),
                  sdk.choice(115200, "115200", icon="status:ok"), sdk.choice(9600, enabled=False),
                  "300"]
        return sdk.column(
            sdk.field("speed", "choice", label="Speed", value="auto", choices=speeds,
                      indicator="radio", searchable=True, tooltip="speed"),
            sdk.field("dir", "choice", choices=["in", "out"], style="radio",
                      orientation="horizontal", columns=2),
            sdk.field("chan", "multichoice", choices=["a", "b"], value=["a"], style="checkboxes"),
            sdk.field("board", "choice", choices=["B1"], editable=True, completion=True),
            sdk.field("find", "text", completion=True),
            sdk.checkbox("verbose", "Verbose", value=None, tristate=True, tone="warn",
                         tooltip="partly"),
            sdk.checkbox("quiet", "Quiet", value=True),
            sdk.radio_group("mode", [sdk.choice("a", "A", icon="status:ok"), sdk.choice_separator(),
                                     "b"], "a", title="Mode", orientation="horizontal", columns=2),
            sdk.check_group("logs", ["boot", sdk.choice_section("More"), "radio"], ["boot"],
                            title="Logs", columns=3, all_none=True),
            sdk.field("notes", "multiline", rows=3, monospace=True, value="x"),
            sdk.field("token", "secret"),
            sdk.field("retries", "integer", value=3, min=0, max=10, step=1),
            sdk.field("level", "slider", value=50, min=0, max=100, step=5),
            sdk.field("led", "color", value="#1565c0"),
            sdk.field("day", "date", value="2026-11-19"),
            sdk.field("at", "time", value="08:30:00"),
            sdk.field("when", "datetime", value="2026-11-19T08:30:00"),
            sdk.field("count", "number", value=1, min=0, max=5),
            sdk.field("label", "text", required=True, pattern="[A-Za-z0-9_-]+", min_length=1,
                      max_length=32, error=""),
            sdk.button("apply", "Apply", event=True, requires_valid=["label", "retries"]),
            sdk.table("t", [sdk.table_column("v", "V", editable=True)],
                      [sdk.table_row("r1", ["a"], editors={"v": sdk.editor(
                          "choice", choices=[sdk.choice("a", "A", icon="status:ok"),
                                             sdk.choice_separator(), "b"])})]),
            id="body")

    def sent_root(self, **initialize):
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
        for feature in E17C_FEATURES:
            self.assertTrue(plugin.supports(feature), feature)
        tree = self.tree()
        plugin.update("main", tree)
        self.assertEqual(host.wait_for("view/update")["root"], tree)

    def test_host_without_choices(self):
        _plugin, _host, elements = self.sent_root(**self.without("choices"))
        self.assertEqual(elements["speed"], {
            "type": "field", "id": "speed", "kind": "choice", "label": "Speed", "value": "auto",
            "tooltip": "speed",
            "choices": [{"value": "auto", "label": "Automatic"},
                        {"value": 115200, "label": "115200"}, 9600, "300"]})
        self.assertEqual(elements["dir"], {"type": "field", "id": "dir", "kind": "choice",
                                           "choices": ["in", "out"]})
        self.assertEqual(elements["chan"], {"type": "field", "id": "chan", "kind": "multichoice",
                                            "choices": ["a", "b"], "value": ["a"]})
        self.assertNotIn("editable", elements["board"])
        self.assertNotIn("completion", elements["board"])
        self.assertNotIn("completion", elements["find"])
        # Check boxes, radio buttons, field kinds and validation stay: only choice objects change.
        self.assertEqual(elements["mode"]["choices"], [{"value": "a", "label": "A"}, "b"])
        self.assertEqual((elements["mode"]["orientation"], elements["mode"]["columns"]),
                         ("horizontal", 2))
        self.assertEqual(elements["logs"]["choices"], ["boot", "radio"])
        self.assertTrue(elements["logs"]["all_none"])
        self.assertEqual(elements["verbose"]["type"], "checkbox")
        self.assertEqual(elements["notes"]["rows"], 3)
        self.assertEqual(elements["label"]["pattern"], "[A-Za-z0-9_-]+")
        self.assertEqual(elements["t"]["rows"][0]["editors"]["v"]["choices"],
                         [{"value": "a", "label": "A"}, "b"])
        self.assertEqual(elements["t"]["columns"], [{"id": "v", "title": "V", "editable": True}])

    def test_host_without_toggles(self):
        _plugin, _host, elements = self.sent_root(**self.without("toggles"))
        self.assertEqual(elements["verbose"], {"type": "field", "id": "verbose", "kind": "boolean",
                                               "label": "Verbose", "value": False, "tooltip": "partly"})
        self.assertEqual(elements["quiet"]["value"], True)
        self.assertEqual(elements["mode"], {
            "type": "field", "id": "mode", "kind": "choice", "label": "Mode", "style": "radio",
            "choices": [{"value": "a", "label": "A", "icon": "status:ok"}, {"type": "separator"}, "b"],
            "value": "a", "orientation": "horizontal", "columns": 2})
        self.assertEqual(elements["logs"], {
            "type": "field", "id": "logs", "kind": "multichoice", "label": "Logs",
            "style": "checkboxes", "choices": ["boot", {"type": "section", "label": "More"}, "radio"],
            "value": ["boot"], "columns": 3})

    def test_host_without_field_kinds(self):
        _plugin, _host, elements = self.sent_root(**self.without("field_kinds"))
        kinds = {element_id: element["kind"] for element_id, element in elements.items()
                 if element["type"] == "field"}
        self.assertEqual([kinds[name] for name in ("notes", "token", "retries", "level", "led",
                                                   "day", "at", "when")],
                         ["text", "text", "number", "number", "text", "text", "text", "text"])
        self.assertEqual(elements["notes"], {"type": "field", "id": "notes", "kind": "text",
                                             "value": "x"})
        # With validation, min and max stay as validation bounds of the number field.
        self.assertEqual(elements["retries"], {"type": "field", "id": "retries", "kind": "number",
                                               "value": 3, "min": 0, "max": 10})
        self.assertEqual(elements["day"]["value"], "2026-11-19")

    def test_host_without_validation(self):
        _plugin, _host, elements = self.sent_root(**self.without("validation"))
        self.assertEqual(elements["label"], {"type": "field", "id": "label", "kind": "text"})
        self.assertNotIn("requires_valid", elements["apply"])
        self.assertEqual(elements["count"], {"type": "field", "id": "count", "kind": "number",
                                             "value": 1})
        # Integer and slider bounds belong to the field kinds and stay.
        self.assertEqual((elements["retries"]["min"], elements["retries"]["max"],
                          elements["retries"]["step"]), (0, 10, 1))
        self.assertEqual(elements["level"]["max"], 100)

    def assert_e14_forms(self, elements):
        for element_id, element in elements.items():
            with self.subTest(element=element_id):
                self.assertIn(element["type"], ("column", "field", "button", "table"))
                # rows and columns are also table keys.
                field_keys = ("rows", "columns") if element["type"] == "field" else ()
                for key in ("indicator", "style", "searchable", "editable", "completion",
                            "orientation", "monospace", "step", "required", "pattern", "min",
                            "max", "min_length", "max_length", "error", "requires_valid",
                            "tristate", "tone", "all_none", "text", "title") + field_keys:
                    self.assertNotIn(key, element)
                if element["type"] != "field":
                    continue
                self.assertIn(element["kind"], E14_FIELD_KINDS)
                for item in element.get("choices", []):
                    self.assertTrue(not isinstance(item, dict) or set(item) == {"value", "label"},
                                    item)
        self.assertEqual(elements["verbose"]["value"], False)
        self.assertEqual(elements["logs"], {"type": "field", "id": "logs", "kind": "multichoice",
                                            "label": "Logs", "choices": ["boot", "radio"],
                                            "value": ["boot"]})
        self.assertEqual(elements["t"]["rows"][0]["editors"]["v"]["choices"],
                         [{"value": "a", "label": "A"}, "b"])

    def test_e17b_and_e14_hosts_get_the_e14_forms(self):
        for name, initialize in (
                ("E17b", dict(host_features=self.testing.E17B_HOST_FEATURES,
                              host_feature_versions=self.testing.E17B_HOST_FEATURE_VERSIONS)),
                ("E14", dict(older=True))):
            with self.subTest(host=name):
                plugin, _host, elements = self.sent_root(**initialize)
                for feature in E17C_FEATURES:
                    self.assertFalse(plugin.supports(feature), feature)
                self.assert_e14_forms(elements)

    def test_patches_follow_the_elements_of_the_last_update(self):
        sdk = self.sdk
        plugin = self.plugin(handles=None)
        host = self.host(plugin, host_features=self.testing.E17B_HOST_FEATURES,
                         host_feature_versions=self.testing.E17B_HOST_FEATURE_VERSIONS)
        plugin.update("main", self.tree())
        host.wait_for("view/update")
        columns = [sdk.table_column("v", "V")]
        changes = [
            sdk.set_props("verbose", value=None, text="Everything", tone="ok", tristate=False),
            sdk.set_props("logs", title="Sources", all_none=False, columns=2,
                          choices=["a", sdk.choice_separator(), sdk.choice("b", "B", icon="x")]),
            sdk.set_props("notes", rows=8, monospace=False, value="y"),
            sdk.set_props("retries", min=1, max=5, step=2, value=4),
            sdk.set_props("label", required=False, error="Too short", enabled=True),
            sdk.set_props("apply", requires_valid=True, label="Apply now"),
            sdk.set_props("t", columns=columns, rows=[sdk.table_row("r2", ["b"])]),
            sdk.set_props("unknown", choices=[sdk.choice_separator(), "z"], searchable=True,
                          error="x", text="kept"),
        ]
        original = copy.deepcopy(changes)
        plugin.patch("main", changes)
        patch = host.wait_for("view/update")["patch"]
        self.assertEqual(changes, original)
        self.assertEqual([entry["set"] for entry in patch], [
            {"value": False, "label": "Everything"},
            {"label": "Sources", "choices": ["a", {"value": "b", "label": "B"}]},
            {"value": "y"},
            {"value": 4},
            {"enabled": True},
            {"label": "Apply now"},
            {"columns": [{"id": "v", "title": "V"}], "rows": [{"id": "r2", "cells": ["b"]}]},
            {"choices": ["z"], "text": "kept"},
        ])

    def test_a_new_root_replaces_the_remembered_elements(self):
        sdk = self.sdk
        plugin = self.plugin(handles=None)
        host = self.host(plugin, **self.without("toggles"))
        plugin.update("main", sdk.column(sdk.checkbox("x", "X"), id="body"))
        host.wait_for("view/update")
        plugin.update("main", sdk.column(sdk.label("x", "Now a label"), id="body"))
        host.wait_for("view/update")
        plugin.patch("main", [sdk.set_props("x", text="still a label")])
        self.assertEqual(host.wait_for("view/update")["patch"],
                         [{"id": "x", "set": {"text": "still a label"}}])

    def test_each_window_keeps_the_elements_of_its_own_update(self):
        sdk = self.sdk
        plugin = self.plugin(handles=None)
        host = self.host(plugin, **self.without("toggles"))

        def patched(element_id, window_id=None):
            plugin.patch("main", [sdk.set_props(element_id, text="T")], window_id)
            return [entry["set"] for entry in host.wait_for("view/update")["patch"]]

        plugin.update("main", sdk.column(sdk.checkbox("x", "X"), sdk.checkbox("a", "A"),
                                         id="body"), "w1")
        host.wait_for("view/update")
        plugin.update("main", sdk.column(sdk.label("x", "L"), sdk.checkbox("b", "B"), id="body"),
                      "w2")
        host.wait_for("view/update")
        self.assertEqual(patched("x", "w1"), [{"label": "T"}])  # still w1's check box
        self.assertEqual(patched("x", "w2"), [{"text": "T"}])
        self.assertEqual(patched("a", "w3"), [{"text": "T"}])  # no tree known for w3
        # A patch for every window converts by the elements of every window's tree.
        self.assertEqual(patched("a") + patched("b"), [{"label": "T"}, {"label": "T"}])
        # An update for every window replaces the windows' own trees.
        plugin.update("main", sdk.column(sdk.label("x", "L"), id="body"))
        host.wait_for("view/update")
        self.assertEqual(patched("x", "w1"), [{"text": "T"}])
        self.assertEqual(patched("a"), [{"text": "T"}])


class Completion(ChoiceCase):
    def test_complete_handler_announces_field_complete_and_query_cancel(self):
        plugin = self.plugin()
        plugin.complete("main", "board")(lambda _query: [])
        host = self.host(plugin)
        self.assertEqual(host.initialization["features"], ["field/complete", "query/cancel"])
        self.assertEqual(plugin.handled_methods(), ["field/complete", "query/cancel"])

    def test_handles_warning_names_field_complete(self):
        plugin = self.plugin(handles=["query/cancel"])
        plugin.complete("main")(lambda _query: None)
        output = io.StringIO()
        with contextlib.redirect_stderr(output):
            host = self.testing.FakeHost.in_process(plugin, timeout=5)
            try:
                host.initialize()
            finally:
                host.close()
        self.assertIn("field/complete", output.getvalue())

    def test_registration_refuses_bad_ids(self):
        plugin = self.plugin()
        for view_id, element_id in (("", None), ("main", "")):
            with self.subTest(view_id=view_id, element_id=element_id), self.assertRaises(ValueError):
                plugin.complete(view_id, element_id)

    def test_the_query_object(self):
        seen = {}
        plugin = self.plugin()

        @plugin.complete("main", "board")
        def board(query):
            seen.update(kind=type(query).__name__, view=query.view_id, window=query.window_id,
                        element=query.element_id, text=query.text, cursor=query.cursor,
                        timeout=query.timeout_ms, context=query.context, method=query.method,
                        thread=threading.current_thread().name)
            return None

        host = self.host(plugin)
        self.assertIsNone(host.complete_field("main", "board", "Boa", 2, window_id="w2",
                                              context={"tab_id": "t1"}))
        self.assertEqual(self.sent(host, "field/complete"),
                         {"view_id": "main", "window_id": "w2", "element_id": "board",
                          "text": "Boa", "cursor": 2, "timeout_ms": 500,
                          "context": {"tab_id": "t1"}})
        self.assertEqual(seen, {"kind": "CompleteQuery", "view": "main", "window": "w2",
                                "element": "board", "text": "Boa", "cursor": 2, "timeout": 500,
                                "context": {"tab_id": "t1"}, "method": "field/complete",
                                "thread": seen["thread"]})
        self.assertTrue(seen["thread"].startswith("plugin-query-field/complete"))
        host.complete_field("main", "board", "Bo")  # the cursor defaults to the end of the text
        self.assertEqual((self.sent(host, "field/complete")["cursor"], seen["cursor"]), (2, 2))

    def test_odd_query_params(self):
        seen = queue.Queue()
        plugin = self.plugin()
        plugin.complete("main")(lambda query: seen.put((query.text, query.cursor, query.window_id)))
        host = self.host(plugin)
        for params in ({"view_id": "main", "element_id": "a", "text": "abc", "cursor": 9},
                       {"view_id": "main", "element_id": "a", "text": "abc", "cursor": -1},
                       {"view_id": "main", "element_id": "a", "text": 5, "cursor": True},
                       {"view_id": "main", "element_id": "a"}):
            host.query("field/complete", params)
        self.assertEqual([seen.get(timeout=2) for _ in range(4)],
                         [("abc", 3, None), ("abc", 0, None), ("", 0, None), ("", 0, None)])

    def test_answers(self):
        sdk = self.sdk
        answers = {
            "strings": (["Board 1", "Board 2"], {"items": [{"value": "Board 1"}, {"value": "Board 2"}]}),
            "items": ([sdk.completion_item("b1", "Board 1", icon="status:ok")],
                      {"items": [{"value": "b1", "label": "Board 1", "icon": "status:ok"}]}),
            "dict": ({"items": ("x",), "future": 1}, {"items": [{"value": "x"}], "future": 1}),
            "empty": ([], {"items": []}),
            "many": ([f"v{n}" for n in range(150)],
                     {"items": [{"value": f"v{n}"} for n in range(100)]}),
            "null": (None, None),
        }
        failures = {"number": 42, "text": "Board", "items_text": {"items": "abc"},
                    "no_items": {"future": 1}, "no_value": [{"label": "L"}],
                    "number_item": [3], "number_value": [{"value": 3}]}
        plugin = self.plugin()

        @plugin.complete("main")
        def answer(query):
            if query.element_id in failures:
                return failures[query.element_id]
            return answers[query.element_id][0]

        host = self.host(plugin)
        for element_id, (_returned, expected) in answers.items():
            with self.subTest(element_id=element_id):
                self.assertEqual(host.complete_field("main", element_id, "x"), expected)
        for element_id in failures:
            with self.subTest(element_id=element_id):
                with self.assertRaises(self.testing.RpcError) as caught:
                    host.complete_field("main", element_id, "x")
                self.assertEqual(caught.exception.code, -32000)
        self.assertEqual(host.errors, [])

    def test_element_handler_wins_and_the_generic_handler_is_the_fallback(self):
        plugin = self.plugin()
        plugin.query("field/complete")(lambda query: {"items": [{"value": "generic"}], "x": 1})
        plugin.complete("main")(lambda _query: ["view"])
        plugin.complete("main", "board")(lambda _query: ["element"])
        host = self.host(plugin)
        self.assertEqual(host.complete_field("main", "board", "")["items"], [{"value": "element"}])
        self.assertEqual(host.complete_field("main", "other", "")["items"], [{"value": "view"}])
        self.assertEqual(host.complete_field("dialog", "other", ""),
                         {"items": [{"value": "generic"}], "x": 1})

    def test_unmatched_answers_null_and_without_handlers_method_not_found(self):
        plugin = self.plugin()
        plugin.complete("main", "board")(lambda _query: ["x"])
        host = self.host(plugin)
        self.assertIsNone(host.complete_field("main", "other", "x"))
        self.assertIsNone(host.complete_field("dialog", "board", "x"))
        other = self.host(self.plugin(handles=None))
        with self.assertRaises(self.testing.RpcError) as caught:
            other.complete_field("main", "board", "x")
        self.assertEqual(caught.exception.code, -32601)

    def test_latest_wins_and_timeout(self):
        gate = threading.Event()
        started = threading.Event()
        self.addCleanup(gate.set)
        texts = []
        plugin = self.plugin()

        @plugin.complete("main")
        def slow(query):
            texts.append(query.text)
            started.set()
            gate.wait(5)
            return [query.text]

        host = self.host(plugin)
        first = host.send_query("field/complete", {"view_id": "main", "element_id": "a",
                                                   "text": "a", "cursor": 1}, timeout_ms=5000)
        self.assertTrue(started.wait(2))
        second = host.send_query("field/complete", {"view_id": "main", "element_id": "a",
                                                    "text": "ab", "cursor": 2}, timeout_ms=5000)
        third = host.send_query("field/complete", {"view_id": "main", "element_id": "a",
                                                   "text": "abc", "cursor": 3}, timeout_ms=5000)
        self.sync(host)
        gate.set()
        self.assertEqual(host.wait_response(first, 2)["result"], {"items": [{"value": "a"}]})
        self.assertEqual(host.wait_response(third, 2)["result"], {"items": [{"value": "abc"}]})
        self.assertEqual(host.answers(second), [])
        self.assertEqual(texts, ["a", "abc"])
        gate.clear()
        with self.assertRaises(self.testing.QueryTimeout) as caught:
            host.complete_field("main", "a", "late", timeout=0.2)
        self.assertEqual((caught.exception.method, host.frames[-1].method),
                         ("field/complete", "query/cancel"))
        self.sync(host)
        gate.set()
        self.assertEqual(host.complete_field("main", "a", "next"), {"items": [{"value": "next"}]})
        self.assertEqual(host.answers(caught.exception.id), [])

    def test_fake_host_refuses_answers_that_are_not_objects(self):
        plugin = self.plugin()
        plugin.query("field/complete")(lambda _query: ["x"])
        host = self.host(plugin)
        with self.assertRaises(self.testing.RpcError) as caught:
            host.complete_field("main", "a", "x")
        self.assertEqual(caught.exception.code, -32603)


class FakeHostHelpers(ChoiceCase):
    def test_feature_sets(self):
        testing = self.testing
        self.assertEqual(testing.HOST_FEATURES[-4:], E17C_FEATURES)
        self.assertEqual(testing.E17B_HOST_FEATURES, testing.HOST_FEATURES[:-4])
        self.assertEqual(testing.E17B_HOST_FEATURES[-2:], ("menus", "shortcuts"))
        for feature in E17C_FEATURES:
            self.assertEqual(testing.HOST_FEATURE_VERSIONS[feature], 1)
        self.assertEqual(set(testing.E17B_HOST_FEATURE_VERSIONS), set(testing.E17B_HOST_FEATURES))
        self.assertEqual(testing.QUERY_DEFAULT_TIMEOUT_MS["field/complete"], 500)

    def test_change_field_sends_changed_with_valid(self):
        received = queue.Queue()
        plugin = self.plugin(handles=None)
        plugin.on_view_event(received.put)
        host = self.host(plugin)
        host.change_field("main", "label", "bad name", valid=False, inputs={"retries": 3})
        host.change_field("main", "verbose", None, window_id="w2")
        first, second = received.get(timeout=2), received.get(timeout=2)
        self.assertEqual(first, {"view_id": "main", "window_id": "w1", "element_id": "label",
                                 "type": "changed", "value": "bad name", "valid": False,
                                 "inputs": {"retries": 3, "label": "bad name"}})
        self.assertEqual(second, {"view_id": "main", "window_id": "w2", "element_id": "verbose",
                                  "type": "changed", "value": None, "inputs": {"verbose": None}})


if __name__ == "__main__":
    unittest.main()
