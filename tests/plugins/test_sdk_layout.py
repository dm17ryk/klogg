"""Python SDK layout, content and dialogs (E17f): the tabs, tab, splitter, form, grid,
separator, spacer, text_view, image, sparkline, chart and chart_series builders, the new button
keys, the content and focus patch helpers, what update() and patch() convert for older hosts,
and the fake host's layout and dialog helpers (docs/plugin-sdk-advanced.md, sections 12 and
29)."""
import copy
import importlib.util
import pathlib
import queue
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = ROOT / "plugins/sdk/python"
E17F_FEATURES = ("layout", "text_view", "images", "charts", "view_dialogs")


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, SDK / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def by_id(root):
    found, pending = {}, [root]
    while pending:
        element = pending.pop()
        if "id" in element:
            found[element["id"]] = element
        pending.extend(element.get("children", []))
    return found


class LayoutCase(unittest.TestCase):
    def setUp(self):
        self.sdk = load("layout_test_sdk", "cilogg_plugin.py")
        self.testing = load("layout_test_host", "cilogg_plugin_testing.py")

    def plugin(self):
        return self.sdk.Plugin({"id": "test", "capabilities": ["tool"]})

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

    def e17e(self):
        return dict(host_features=self.testing.E17E_HOST_FEATURES,
                    host_feature_versions=self.testing.E17E_HOST_FEATURE_VERSIONS)


class Builders(LayoutCase):
    def test_constants(self):
        sdk = self.sdk
        self.assertEqual(sdk.TAB_POSITIONS, ("top", "bottom", "left", "right"))
        self.assertEqual(sdk.ELEMENT_ALIGNMENTS, ("left", "center", "right", "top", "bottom", "fill"))
        self.assertEqual(sdk.TEXT_FORMATS, ("plain", "markdown"))
        self.assertEqual(sdk.IMAGE_SCALES, ("fit", "fill", "none"))
        self.assertEqual(sdk.SPARKLINE_KINDS, ("line", "bar"))
        self.assertEqual(sdk.CHART_KINDS, ("line", "bar", "scatter", "area"))
        self.assertEqual(sdk.X_KINDS, ("number", "time"))
        self.assertEqual(sdk.BUTTON_STYLES, ("push", "tool", "link"))
        self.assertEqual(sdk.DIALOG_BUTTONS, ("ok", "cancel", "apply", "close", "yes", "no"))
        self.assertEqual(sdk.DIALOG_BUTTONS_ELEMENT, "#buttons")
        self.assertEqual((sdk.MAX_TEXT_VIEW_CHARS, sdk.MAX_TEXT_APPEND_CHARS, sdk.MAX_TEXT_VIEW_LINES),
                         (1048576, 65536, 100000))
        self.assertEqual((sdk.MAX_CHART_SERIES, sdk.MAX_CHART_POINTS, sdk.MAX_SPARKLINE_VALUES),
                         (8, 10000, 1024))
        self.assertEqual((sdk.MAX_GRID_COLUMNS, sdk.MAX_BADGE_CHARS, sdk.MAX_IMAGE_SIZE,
                          sdk.MAX_SPACER_SIZE, sdk.MAX_STRETCH), (12, 64, 4096, 2000, 100))
        self.assertEqual(sdk.SDK_VERSION, "2.0.0")
        for name in ("TabsElement", "TabElement", "SplitterElement", "FormElement", "GridElement",
                     "SeparatorElement", "SpacerElement", "TextViewElement", "ImageElement",
                     "SparklineElement", "ChartElement", "ChartSeries", "LinkValue",
                     "ButtonElement", "ContainerElement", "ContentPatch", "ViewClosedParams"):
            self.assertTrue(hasattr(sdk, name), name)
        self.assertIn("index", sdk.ViewEventParams.__annotations__)
        self.assertEqual(set(sdk.ViewClosedParams.__annotations__),
                         {"view_id", "window_id", "result", "inputs", "truncated"})

    def test_tabs_and_tab(self):
        sdk = self.sdk
        first = sdk.tab("Form", sdk.label("l", "x"), id="t1", icon="builtin:edit", badge=3,
                        stretch=1, busy=True)
        self.assertEqual(first, {"type": "tab", "id": "t1", "title": "Form", "icon": "builtin:edit",
                                 "badge": "3", "children": [{"type": "label", "id": "l", "text": "x"}],
                                 "stretch": 1, "busy": True})
        second = sdk.tab("Log", id="t2", badge="b" * sdk.MAX_BADGE_CHARS)
        self.assertEqual(sdk.tab("Plain"), {"type": "tab", "title": "Plain", "children": []})
        element = sdk.tabs("tabs", first, second, current="t2", position="left",
                           accessible_name="Pages")
        self.assertEqual(element, {"type": "tabs", "id": "tabs", "current": "t2", "position": "left",
                                   "children": [first, second], "accessible_name": "Pages"})
        self.assertEqual(sdk.tabs("tabs", first, second, current=1)["current"], 1)
        self.assertEqual(sdk.tabs("tabs"), {"type": "tabs", "id": "tabs", "children": []})
        for call in (lambda: sdk.tabs("", first), lambda: sdk.tabs("x", sdk.label("l", "x")),
                     lambda: sdk.tabs("x", "t1"), lambda: sdk.tabs("x", first, current="nope"),
                     lambda: sdk.tabs("x", first, current=1), lambda: sdk.tabs("x", first, current=-1),
                     lambda: sdk.tabs("x", first, current=True),
                     lambda: sdk.tabs("x", first, current=0.5),
                     lambda: sdk.tabs("x", first, position="middle"), lambda: sdk.tab(3),
                     lambda: sdk.tab("x", badge="b" * (sdk.MAX_BADGE_CHARS + 1)),
                     lambda: sdk.tab("x", "child")):
            with self.subTest(), self.assertRaises(ValueError):
                call()

    def test_splitter_form_and_grid(self):
        sdk = self.sdk
        a, b, c = sdk.label("a", "A"), sdk.label("b", "B"), sdk.label("c", "C")
        self.assertEqual(sdk.splitter(a, b, id="s", orientation="vertical", sizes=(100, 200)),
                         {"type": "splitter", "id": "s", "orientation": "vertical",
                          "sizes": [100, 200], "children": [a, b]})
        self.assertEqual(sdk.splitter(), {"type": "splitter", "children": []})
        self.assertEqual(sdk.form(a, b, id="f", busy_cancel=True),
                         {"type": "form", "id": "f", "children": [a, b], "busy_cancel": True})
        self.assertEqual(sdk.grid(a, b, c, columns=3, id="g"),
                         {"type": "grid", "id": "g", "columns": 3, "children": [a, b, c]})
        self.assertEqual(sdk.grid(), {"type": "grid", "children": []})
        self.assertEqual(sdk.grid(columns=12)["columns"], 12)
        for call in (lambda: sdk.splitter(orientation="diagonal"),
                     lambda: sdk.splitter(a, sizes="100"), lambda: sdk.splitter(a, sizes=[-1]),
                     lambda: sdk.splitter(a, sizes=[1.5]), lambda: sdk.splitter(a, sizes=[True]),
                     lambda: sdk.splitter("a"), lambda: sdk.form(3),
                     lambda: sdk.grid(columns=0), lambda: sdk.grid(columns=13),
                     lambda: sdk.grid(columns=2.5), lambda: sdk.grid(columns=True)):
            with self.subTest(), self.assertRaises(ValueError):
                call()

    def test_separator_and_spacer(self):
        sdk = self.sdk
        self.assertEqual(sdk.separator(), {"type": "separator"})
        self.assertEqual(sdk.separator("line", "vertical"),
                         {"type": "separator", "id": "line", "orientation": "vertical"})
        self.assertEqual(sdk.spacer(), {"type": "spacer"})
        self.assertEqual(sdk.spacer("gap", size=20), {"type": "spacer", "id": "gap", "size": 20})
        self.assertEqual(sdk.spacer(stretch=2), {"type": "spacer", "stretch": 2})
        self.assertEqual(sdk.spacer(size=0, stretch=0), {"type": "spacer", "size": 0, "stretch": 0})
        for call in (lambda: sdk.separator(orientation="x"), lambda: sdk.spacer(size=-1),
                     lambda: sdk.spacer(size=sdk.MAX_SPACER_SIZE + 1), lambda: sdk.spacer(size=1.5),
                     lambda: sdk.spacer(stretch=sdk.MAX_STRETCH + 1)):
            with self.subTest(), self.assertRaises(ValueError):
                call()

    def test_text_view(self):
        sdk = self.sdk
        self.assertEqual(sdk.text_view("log"), {"type": "text_view", "id": "log", "text": ""})
        self.assertEqual(sdk.text_view("notes", "# Hi", format="markdown", monospace=False,
                                       wrap=False, follow=True, max_lines=0, stretch=1),
                         {"type": "text_view", "id": "notes", "text": "# Hi", "format": "markdown",
                          "monospace": False, "wrap": False, "follow": True, "max_lines": 0,
                          "stretch": 1})
        self.assertEqual(len(sdk.text_view("t", "x" * sdk.MAX_TEXT_VIEW_CHARS)["text"]),
                         sdk.MAX_TEXT_VIEW_CHARS)
        self.assertEqual(sdk.text_view("t", max_lines=sdk.MAX_TEXT_VIEW_LINES)["max_lines"],
                         100000)
        for arguments, keywords in (
                (("",), {}), (("t", 3), {}), (("t", "x" * (sdk.MAX_TEXT_VIEW_CHARS + 1)), {}),
                (("t",), dict(format="html")), (("t",), dict(max_lines=-1)),
                (("t",), dict(max_lines=sdk.MAX_TEXT_VIEW_LINES + 1)),
                (("t",), dict(max_lines=1.5))):
            with self.subTest(keywords=keywords), self.assertRaises(ValueError):
                sdk.text_view(*arguments, **keywords)

    def test_image(self):
        sdk = self.sdk
        self.assertEqual(sdk.image("p", "image:pic", width=96, height=64, scale="fill", alt="A"),
                         {"type": "image", "id": "p", "source": "image:pic", "width": 96,
                          "height": 64, "scale": "fill", "alt": "A"})
        themed = sdk.themed_ref("icons/light.png", "icons/dark.png")
        element = sdk.image("p", themed)
        self.assertEqual(element["source"], themed)
        self.assertIsNot(element["source"], themed)
        self.assertEqual(sdk.image("p", "builtin:info", width=sdk.MAX_IMAGE_SIZE)["width"], 4096)
        for source, keywords in (
                ("", {}), (3, {}), ({}, {}), ({"light": ""}, {}), ({"other": "x"}, {}),
                ("x.png", dict(width=0)), ("x.png", dict(width=sdk.MAX_IMAGE_SIZE + 1)),
                ("x.png", dict(height=1.5)), ("x.png", dict(scale="stretch"))):
            with self.subTest(source=source, keywords=keywords), self.assertRaises(ValueError):
                sdk.image("p", source, **keywords)
        with self.assertRaises(ValueError):
            sdk.image("", "x.png")

    def test_sparkline(self):
        sdk = self.sdk
        self.assertEqual(sdk.sparkline("s", (1, None, 2.5), min=0, max=10, kind="bar",
                                       tone="accent", accessible_name="Rx"),
                         {"type": "sparkline", "id": "s", "values": [1, None, 2.5], "min": 0,
                          "max": 10, "kind": "bar", "tone": "accent", "accessible_name": "Rx"})
        self.assertEqual(sdk.sparkline("s", []), {"type": "sparkline", "id": "s", "values": []})
        self.assertEqual(len(sdk.sparkline("s", [1] * sdk.MAX_SPARKLINE_VALUES)["values"]), 1024)
        for values, keywords in (
                ([1] * (sdk.MAX_SPARKLINE_VALUES + 1), {}), ("123", {}), ([float("nan")], {}),
                (["x"], {}), ([True], {}), ([1], dict(min=5, max=1)), ([1], dict(min="0")),
                ([1], dict(kind="pie")), ([1], dict(tone="red"))):
            with self.subTest(keywords=keywords), self.assertRaises(ValueError):
                sdk.sparkline("s", values, **keywords)

    def test_chart_and_series(self):
        sdk = self.sdk
        series = sdk.chart_series("rx", [1, 2.5, (3, 4), [5, 6]], title="Received", tone="accent")
        self.assertEqual(series, {"id": "rx", "points": [1, 2.5, [3, 4], [5, 6]],
                                  "title": "Received", "tone": "accent"})
        self.assertEqual(sdk.chart_series("tx", []), {"id": "tx", "points": []})
        element = sdk.chart("c", [series, {"id": "tx", "points": [(0, 1)]}], kind="area",
                            x_title="Time", y_title="Bytes", x_kind="time", legend=False,
                            window=500, stretch=3)
        self.assertEqual(element, {
            "type": "chart", "id": "c", "kind": "area",
            "series": [series, {"id": "tx", "points": [[0, 1]]}], "x_title": "Time",
            "y_title": "Bytes", "x_kind": "time", "legend": False, "window": 500, "stretch": 3})
        self.assertIsNot(element["series"][0], series)
        self.assertEqual(sdk.chart("c", []), {"type": "chart", "id": "c", "series": []})
        self.assertEqual(len(sdk.chart("c", [sdk.chart_series(f"s{n}", []) for n in range(8)])
                             ["series"]), 8)
        self.assertEqual(len(sdk.chart_series("s", [1] * sdk.MAX_CHART_POINTS)["points"]), 10000)
        for call in (lambda: sdk.chart_series("", []), lambda: sdk.chart_series("s", "12"),
                     lambda: sdk.chart_series("s", [[1]]), lambda: sdk.chart_series("s", [[1, 2, 3]]),
                     lambda: sdk.chart_series("s", [None]), lambda: sdk.chart_series("s", [True]),
                     lambda: sdk.chart_series("s", [[1, "x"]]),
                     lambda: sdk.chart_series("s", [float("inf")]),
                     lambda: sdk.chart_series("s", [1] * (sdk.MAX_CHART_POINTS + 1)),
                     lambda: sdk.chart_series("s", [], tone="red"),
                     lambda: sdk.chart("c", [sdk.chart_series(f"s{n}", []) for n in range(9)]),
                     lambda: sdk.chart("c", [sdk.chart_series("s", []), sdk.chart_series("s", [])]),
                     lambda: sdk.chart("c", [], kind="pie"), lambda: sdk.chart("c", [], x_kind="date"),
                     lambda: sdk.chart("c", [], window=0),
                     lambda: sdk.chart("c", [], window=sdk.MAX_CHART_POINTS + 1),
                     lambda: sdk.chart("c", "s"), lambda: sdk.chart("c", [3]),
                     lambda: sdk.chart("c", [{"points": []}]), lambda: sdk.chart("", [])):
            with self.subTest(), self.assertRaises(ValueError):
                call()

    def test_button_keys(self):
        sdk = self.sdk
        self.assertEqual(sdk.button("go", "Go", "run", {"x": 1}, True, "danger"),
                         {"type": "button", "id": "go", "label": "Go", "action": "run",
                          "arguments": {"x": 1}, "event": True, "tone": "danger"})
        self.assertEqual(
            sdk.button("tool", "Add", event=True, icon="builtin:refresh", style="tool",
                       shortcut="Ctrl+Alt+L", default=True),
            {"type": "button", "id": "tool", "label": "Add", "event": True,
             "icon": "builtin:refresh", "style": "tool", "shortcut": "Ctrl+Alt+L", "default": True})
        self.assertEqual(sdk.button("menu", "Actions", menu="actions"),
                         {"type": "button", "id": "menu", "label": "Actions", "menu": "actions"})
        self.assertEqual(sdk.button("pin", "Pin", checkable=True, checked=False),
                         {"type": "button", "id": "pin", "label": "Pin", "checkable": True,
                          "checked": False})
        # An unchecked button needs no checkable: the host ignores `checked` without it.
        self.assertEqual(sdk.button("plain", "Plain", checkable=False, checked=False),
                         {"type": "button", "id": "plain", "label": "Plain", "checkable": False,
                          "checked": False})
        for keywords in (dict(style="flat"), dict(menu=""), dict(menu="a/b"), dict(menu=3),
                         dict(checked=True), dict(checkable=False, checked=True),
                         dict(shortcut=""), dict(shortcut=3)):
            with self.subTest(keywords=keywords), self.assertRaises(ValueError):
                sdk.button("b", "B", **keywords)


class Patches(LayoutCase):
    def test_helpers(self):
        sdk = self.sdk
        self.assertEqual(sdk.append_text("log", "line 1\n"), {"id": "log", "append": "line 1\n"})
        self.assertEqual(len(sdk.append_text("log", "x" * sdk.MAX_TEXT_APPEND_CHARS)["append"]),
                         65536)
        self.assertEqual(sdk.append_points("c", "rx", [1, (2, 3)]),
                         {"id": "c", "append_points": {"series": "rx", "points": [1, [2, 3]]}})
        self.assertEqual(sdk.clear_chart("c"), {"id": "c", "clear": True})
        self.assertEqual(sdk.clear_chart("c", ("rx", "tx")), {"id": "c", "clear": ["rx", "tx"]})
        self.assertEqual(sdk.clear_chart("c", []), {"id": "c", "clear": []})
        self.assertEqual(sdk.focus_element("name"), {"id": "name", "focus": True})
        for call in (lambda: sdk.append_text("log", "x" * (sdk.MAX_TEXT_APPEND_CHARS + 1)),
                     lambda: sdk.append_text("log", 3), lambda: sdk.append_text("", "x"),
                     lambda: sdk.append_points("c", "", [1]), lambda: sdk.append_points("", "s", [1]),
                     lambda: sdk.append_points("c", "s", [[1]]),
                     lambda: sdk.append_points("c", "s", [1] * (sdk.MAX_CHART_POINTS + 1)),
                     lambda: sdk.clear_chart("c", "rx"), lambda: sdk.clear_chart("c", [""]),
                     lambda: sdk.clear_chart(""), lambda: sdk.focus_element(""),
                     lambda: sdk.focus_element(3)):
            with self.subTest(), self.assertRaises(ValueError):
                call()


class Downgrades(LayoutCase):
    """update() and patch() convert for a host without the E17f features."""

    def view(self):
        sdk = self.sdk
        form = sdk.form(
            sdk.field("name", label="Name", stretch=2),
            sdk.checkbox("opt", "Enabled", label="Option"),
            sdk.separator(),
            sdk.grid(sdk.button("g1", "1"), sdk.button("g2", "2"), sdk.button("g3", "3", span=2),
                     columns=2, id="g", label="Grid"),
            sdk.button("go", "Go"),
            sdk.label("plain", "no caption"),
            id="f")
        return sdk.column(
            sdk.tabs("tabs",
                     sdk.tab("Form", form, id="t1", icon="builtin:edit", badge="2", busy=True),
                     sdk.tab("Log", sdk.text_view("log", "a\n", monospace=True, stretch=1), id="t2"),
                     current="t2", position="left", min_height=100),
            sdk.splitter(sdk.chart("c", [sdk.chart_series("s", [1, 2])], align="fill"),
                         sdk.column(sdk.sparkline("sp", [1, None, 3]), sdk.spacer(), id="side"),
                         id="hs", sizes=[300, 100]),
            sdk.splitter(sdk.image("img", "builtin:info"), sdk.label("cap", "Caption"), id="vs",
                         orientation="vertical"),
            sdk.row(sdk.button("tool", "Tool", event=True, style="tool", icon="builtin:refresh",
                               menu="m", shortcut="Ctrl+K", default=True),
                    sdk.button("pin", "Pin", checkable=True, checked=False),
                    sdk.spacer("gap", size=10), sdk.progress("p", busy=True), id="buttons"),
            id="body", modified=True, close_confirm="Discard?", busy=True, busy_text="Working",
            busy_cancel=True)

    def patches(self):
        sdk = self.sdk
        return [
            sdk.set_props("tabs", current="t1", position="top", tooltip="x"),  # 0
            sdk.set_props("t1", title="Form (3)", badge="3", icon="builtin:info"),  # 1
            sdk.set_props("hs", sizes=[10, 20], orientation="vertical"),  # 2
            sdk.set_props("g", columns=3),  # 3
            sdk.set_props("body", modified=False, busy=False, busy_text="x"),  # 4
            sdk.set_props("tool", style="link", checked=True, label="Tool 2"),  # 5
            sdk.set_props("name", stretch=1, min_width=10, value="x"),  # 6
            sdk.set_props("p", busy=False),  # 7: a progress bar keeps busy
            sdk.set_props("gap", size=20),  # 8
            sdk.append_text("log", "b\n"),  # 9
            sdk.append_points("c", "s", [3]),  # 10
            sdk.clear_chart("c", ["s"]),  # 11
            sdk.focus_element("name"),  # 12
            {"id": "log", "append": "c\n", "set": {"wrap": False}},  # 13
            sdk.set_props("log", text="", monospace=False),  # 14
            sdk.set_props("later", current="x", badge="y", sizes=[1], busy=True, busy_text="w",
                          menu="m", shortcut="s", default=True, checkable=True, checked=True,
                          icon="i", style="tool", monospace=True, text="kept"),  # 15
        ]

    def session(self, **initialize):
        plugin = self.plugin()
        older = initialize.pop("older", False)
        host = self.host(plugin, older=older, **initialize)
        view, patches = self.view(), self.patches()
        originals = copy.deepcopy((view, patches))
        plugin.update("main", view)
        root = host.wait_for("view/update")["root"]
        plugin.patch("main", patches)
        patch = host.wait_for("view/update")["patch"]
        self.assertEqual((view, patches), originals)  # the caller's dicts are not changed
        return plugin, root, patch

    def test_current_host_gets_everything(self):
        plugin, root, patch = self.session()
        for feature in E17F_FEATURES:
            self.assertTrue(plugin.supports(feature), feature)
        self.assertEqual(root, self.view())
        self.assertEqual(patch, self.patches())

    def test_host_without_images_gets_everything(self):
        _plugin, root, patch = self.session(**self.without("images"))
        self.assertEqual(root, self.view())
        self.assertEqual(patch, self.patches())

    def test_e17e_host(self):
        plugin, root, patch = self.session(**self.e17e())
        for feature in E17F_FEATURES:
            self.assertFalse(plugin.supports(feature), feature)
        sdk = self.sdk

        def row(*children):
            return {"type": "row", "children": list(children)}

        button = {"type": "button", "id": "g1", "label": "1"}
        form = {"type": "column", "id": "f", "children": [
            row({"type": "field", "id": "name", "kind": "text", "label": "Name"}),
            row({"type": "label", "text": "Option"},
                {"type": "checkbox", "id": "opt", "text": "Enabled", "value": False}),
            row({"type": "label", "text": "Grid"},
                {"type": "column", "id": "g", "children": [
                    row(button, {**button, "id": "g2", "label": "2"}),
                    row({**button, "id": "g3", "label": "3"})]}),
            row({"type": "button", "id": "go", "label": "Go"}),
            row({"type": "label", "id": "plain", "text": "no caption"})]}
        self.assertEqual(root, {"type": "column", "id": "body", "children": [
            {"type": "column", "id": "tabs", "children": [
                {"type": "group", "id": "t1", "title": "Form", "children": [form]},
                {"type": "group", "id": "t2", "title": "Log", "children": [
                    {"type": "text_view", "id": "log", "text": "a\n", "monospace": True}]}]},
            {"type": "column", "id": "hs", "children": [row(
                {"type": "chart", "id": "c", "series": [{"id": "s", "points": [1, 2]}]},
                {"type": "column", "id": "side", "children": [
                    {"type": "sparkline", "id": "sp", "values": [1, None, 3]}]})]},
            {"type": "column", "id": "vs", "children": [
                row({"type": "image", "id": "img", "source": "builtin:info"}),
                row({"type": "label", "id": "cap", "text": "Caption"})]},
            {"type": "row", "id": "buttons", "children": [
                {"type": "button", "id": "tool", "label": "Tool", "event": True},
                {"type": "button", "id": "pin", "label": "Pin"},
                sdk.progress("p", busy=True)]}]})
        self.assertEqual(patch, [
            {"id": "tabs", "set": {"tooltip": "x"}},
            {"id": "t1", "set": {"title": "Form (3)"}},  # the group keeps the tab's title
            {"id": "hs", "set": {}},
            {"id": "g", "set": {}},
            {"id": "body", "set": {}},
            {"id": "tool", "set": {"label": "Tool 2"}},
            {"id": "name", "set": {"value": "x"}},
            {"id": "p", "set": {"busy": False}},
            {"id": "log", "set": {"wrap": False}},
            {"id": "log", "set": {"text": "", "monospace": False}},
            {"id": "later", "set": {"busy": True, "icon": "i", "style": "tool", "monospace": True,
                                    "text": "kept"}},
        ])

    def test_host_without_layout_keeps_the_dialog_keys(self):
        _plugin, root, patch = self.session(**self.without("layout"))
        elements = by_id(root)
        self.assertEqual({key: elements["body"][key] for key in ("modified", "close_confirm",
                                                                 "busy", "busy_text", "busy_cancel")},
                         {"modified": True, "close_confirm": "Discard?", "busy": True,
                          "busy_text": "Working", "busy_cancel": True})
        self.assertEqual({key: value for key, value in elements["t1"].items() if key != "children"},
                         {"type": "group", "id": "t1", "title": "Form", "busy": True})
        self.assertEqual(elements["tool"], by_id(self.view())["tool"])
        self.assertEqual(elements["tabs"]["type"], "column")
        self.assertNotIn("gap", elements)
        self.assertNotIn("stretch", elements["log"])
        patches = self.patches()
        self.assertEqual(patch, [
            {"id": "tabs", "set": {"tooltip": "x"}}, {"id": "t1", "set": {"title": "Form (3)"}},
            {"id": "hs", "set": {}}, {"id": "g", "set": {}}, patches[4], patches[5],
            {"id": "name", "set": {"value": "x"}}, patches[7], *patches[9:15],
            {"id": "later", "set": {"busy": True, "busy_text": "w", "menu": "m", "shortcut": "s",
                                    "default": True, "checkable": True, "checked": True,
                                    "icon": "i", "style": "tool", "monospace": True,
                                    "text": "kept"}}])

    def test_host_without_view_dialogs_keeps_the_layout(self):
        _plugin, root, patch = self.session(**self.without("view_dialogs"))
        elements = by_id(root)
        expected = by_id(self.view())
        self.assertEqual(elements["tabs"]["type"], "tabs")
        self.assertEqual(elements["tabs"]["current"], "t2")
        self.assertEqual({key: value for key, value in elements["body"].items() if key != "children"},
                         {"type": "column", "id": "body"})
        self.assertEqual({key: value for key, value in elements["t1"].items() if key != "children"},
                         {"type": "tab", "id": "t1", "title": "Form", "icon": "builtin:edit",
                          "badge": "2"})
        self.assertEqual(elements["tool"], {"type": "button", "id": "tool", "label": "Tool",
                                            "event": True})
        self.assertEqual(elements["pin"], {"type": "button", "id": "pin", "label": "Pin"})
        self.assertEqual(elements["p"], expected["p"])
        self.assertEqual(elements["gap"], expected["gap"])
        patches = self.patches()
        self.assertEqual(patch, [
            *patches[:4], {"id": "body", "set": {}}, {"id": "tool", "set": {"label": "Tool 2"}},
            *patches[6:12], *patches[13:15],
            {"id": "later", "set": {"current": "x", "badge": "y", "sizes": [1], "busy": True,
                                    "icon": "i", "style": "tool", "monospace": True,
                                    "text": "kept"}}])

    def test_host_without_text_view_drops_the_append(self):
        _plugin, root, patch = self.session(**self.without("text_view"))
        self.assertEqual(root, self.view())  # text views go as they are
        patches = self.patches()
        self.assertEqual(patch, [*patches[:9], *patches[10:13], {"id": "log", "set": {"wrap": False}},
                                 *patches[14:]])

    def test_host_without_charts_drops_append_points_and_clear(self):
        _plugin, root, patch = self.session(**self.without("charts"))
        self.assertEqual(root, self.view())  # charts and sparklines go as they are
        patches = self.patches()
        self.assertEqual(patch, [*patches[:10], *patches[12:]])

    def test_form_caption_patch_on_a_host_without_layout(self):
        # The caption became a label element of its own (no id): a `label` patch of the
        # checkbox would only be warned about, so it goes; a field's and a button's label stay.
        for initialize, expected in ((self.without("layout"),
                                      [{"id": "opt", "set": {"text": "On"}},
                                       {"id": "name", "set": {"label": "Full name"}},
                                       {"id": "go", "set": {"label": "Run"}}]),
                                     ({}, None)):
            with self.subTest(layout=not initialize):
                plugin = self.plugin()
                host = self.host(plugin, **initialize)
                plugin.update("main", self.view())
                host.wait_for("view/update")
                changes = [self.sdk.set_props("opt", label="Other", text="On"),
                           self.sdk.set_props("name", label="Full name"),
                           self.sdk.set_props("go", label="Run")]
                plugin.patch("main", changes)
                self.assertEqual(host.wait_for("view/update")["patch"], expected or changes)

    def test_form_caption_patch_of_a_tree_on_a_host_without_layout(self):
        # Trees and content elements are passed through unconverted, but their form caption
        # became a label element too, so a `label` patch goes the same way.
        sdk = self.sdk
        plugin = self.plugin()
        host = self.host(plugin, **self.without("layout"))
        plugin.update("main", sdk.column(sdk.form(sdk.tree("t", [], label="Boards"), id="f"),
                                         id="body"))
        rows = by_id(host.wait_for("view/update")["root"])["f"]["children"]
        self.assertEqual(rows[0]["children"][0], {"type": "label", "text": "Boards"})
        self.assertNotIn("label", rows[0]["children"][1])
        plugin.patch("main", [sdk.set_props("t", label="Benches", selection="single")])
        self.assertEqual(host.wait_for("view/update")["patch"],
                         [{"id": "t", "set": {"selection": "single"}}])

    def test_form_caption_is_shown_and_enabled_as_its_element_on_a_host_without_layout(self):
        sdk = self.sdk
        plugin = self.plugin()
        host = self.host(plugin, **self.without("layout"))
        plugin.update("main", sdk.column(
            sdk.form(sdk.checkbox("opt", "Enabled", label="Option", visible=False, enabled=False),
                     sdk.label("note", "x", label="Note", enabled=True), id="f"), id="body"))
        rows = by_id(host.wait_for("view/update")["root"])["f"]["children"]
        self.assertEqual(rows[0]["children"][0],
                         {"type": "label", "text": "Option", "visible": False, "enabled": False})
        self.assertEqual(rows[0]["children"][1]["visible"], False)
        self.assertEqual(rows[1]["children"][0], {"type": "label", "text": "Note", "enabled": True})

    def test_host_without_layout_and_toggles(self):
        _plugin, root, _patch = self.session(**self.without("layout", "toggles"))
        option_row = by_id(root)["f"]["children"][1]
        self.assertEqual(option_row, {"type": "row", "children": [
            {"type": "label", "text": "Option"},
            {"type": "field", "id": "opt", "kind": "boolean", "label": "Enabled", "value": False}]})

    def test_e14_host(self):
        _plugin, root, patch = self.session(older=True)
        types = set()
        pending = [root]
        while pending:
            element = pending.pop()
            types.add(element["type"])
            pending.extend(element.get("children", []))
        self.assertEqual(types, {"column", "row", "group", "field", "label", "button", "progress",
                                 "text_view", "chart", "sparkline", "image"})
        self.assertEqual(len(patch), 11)

    def test_a_patch_of_only_dropped_entries_sends_nothing(self):
        sdk = self.sdk
        plugin = self.plugin()
        host = self.host(plugin, **self.e17e())
        plugin.update("main", self.view())
        host.wait_for("view/update")
        plugin.patch("main", [sdk.append_text("log", "x"), sdk.append_points("c", "s", [1]),
                              sdk.clear_chart("c"), sdk.focus_element("name"),
                              sdk.set_props("gap", size=3)])
        plugin.patch("main", [sdk.set_props("cap", text="next")])
        self.assertEqual(host.wait_for("view/update")["patch"],
                         [{"id": "cap", "set": {"text": "next"}}])

    def test_a_spacing_root_becomes_an_empty_column(self):
        sdk = self.sdk
        plugin = self.plugin()
        host = self.host(plugin, **self.e17e())
        plugin.update("main", sdk.spacer("gap", size=4))
        self.assertEqual(host.wait_for("view/update")["root"],
                         {"type": "column", "id": "gap", "children": []})
        plugin.update("main", sdk.separator())
        self.assertEqual(host.wait_for("view/update")["root"], {"type": "column", "children": []})

    def test_converted_containers_without_children(self):
        sdk = self.sdk
        plugin = self.plugin()
        host = self.host(plugin, **self.without("layout"))
        plugin.update("main", sdk.column(sdk.splitter(id="s"), sdk.grid(id="g"), sdk.tabs("t"),
                                         sdk.form(sdk.separator(), id="f"),
                                         {"type": "grid", "id": "odd", "columns": "x",
                                          "children": [sdk.label("a", "A")] * 3},
                                         id="body"))
        elements = by_id(host.wait_for("view/update")["root"])
        for element_id in ("s", "g", "t", "f"):
            self.assertEqual(elements[element_id], {"type": "column", "id": element_id,
                                                    "children": []}, element_id)
        self.assertEqual([len(item["children"]) for item in elements["odd"]["children"]], [2, 1])

    def test_patch_of_an_unknown_element_keeps_monospace_with_text_view(self):
        sdk = self.sdk
        for name, features, expected in (
                ("text_view", ("field_kinds",), {"monospace": True, "text": "x"}),
                ("neither", ("field_kinds", "text_view"), {"text": "x"})):
            with self.subTest(host=name):
                plugin = self.plugin()
                host = self.host(plugin, **self.without(*features))
                plugin.update("main", sdk.column(sdk.label("l", "x"), id="body"))
                host.wait_for("view/update")
                plugin.patch("main", [sdk.set_props("later", monospace=True, step=2, text="x")])
                self.assertEqual(host.wait_for("view/update")["patch"],
                                 [{"id": "later", "set": expected}])


class Events(LayoutCase):
    def received(self):
        events, closed = queue.Queue(), queue.Queue()
        plugin = self.plugin()
        plugin.on_view_event(events.put)
        plugin.on_view_closed(closed.put)
        return self.host(plugin), events, closed

    def test_layout_and_dialog_events_from_the_fake_host(self):
        host, events, _closed = self.received()
        host.switch_tab("main", "tabs", "log", 1)
        host.switch_tab("main", "tabs", None, 2, "w2", inputs={"name": "x"})
        host.click_link("main", "notes", "https://example.com/docs")
        host.cancel_busy("main", "body")
        host.toggle_button("main", "pin", True)
        host.toggle_button("main", "pin", 0)
        host.press_dialog_button("ask", "apply", inputs={"ask_name": "Ann"})
        received = [events.get(timeout=2) for _ in range(7)]
        self.assertEqual(received[0], {"view_id": "main", "window_id": "w1", "element_id": "tabs",
                                       "type": "changed", "value": "log", "inputs": {},
                                       "index": 1})
        self.assertEqual(received[1], {"view_id": "main", "window_id": "w2", "element_id": "tabs",
                                       "type": "changed", "value": None,
                                       "inputs": {"name": "x"}, "index": 2})
        self.assertEqual([(event["element_id"], event["type"], event["value"])
                          for event in received[2:]], [
            ("notes", "link", {"href": "https://example.com/docs"}),
            ("body", "cancel", None),
            ("pin", "changed", True),
            ("pin", "changed", False),
            ("#buttons", "clicked", "apply")])
        self.assertEqual((received[6]["view_id"], received[6]["inputs"]), ("ask", {"ask_name": "Ann"}))
        self.assertTrue(all("inputs" in event for event in received))
        for name in ("help", "", "OK"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                host.press_dialog_button("ask", name)

    def test_close_view_with_result_and_inputs(self):
        host, _events, closed = self.received()
        host.close_view("main")
        host.close_view("ask", result="ok", inputs={"ask_name": "Ann"})
        host.close_view("ask", "w2", result=None, inputs={})
        host.close_view("ask", inputs={"ask_name": ""})
        self.assertEqual([closed.get(timeout=2) for _ in range(4)], [
            {"view_id": "main", "window_id": "w1"},
            {"view_id": "ask", "window_id": "w1", "result": "ok", "inputs": {"ask_name": "Ann"}},
            {"view_id": "ask", "window_id": "w2", "result": None, "inputs": {}},
            {"view_id": "ask", "window_id": "w1", "inputs": {"ask_name": ""}}])
        with self.assertRaises(ValueError):
            host.close_view("ask", result="help")
        with self.assertRaises(ValueError):
            host.close_view("ask", result="apply")  # `apply` never closes a dialog

    def test_dialog_events_follow_the_manifest_when_it_describes_the_view(self):
        # CILogg sends only the buttons a dialog declares, and a result only for such a dialog.
        host, events, closed = self.received()
        host.manifest = {"views": [
            {"id": "main", "kind": "window"},
            {"id": "ask", "kind": "dialog", "buttons": ["ok", "cancel", "apply"]},
            {"id": "plain", "kind": "dialog"},
            {"id": "confirmed", "kind": "dialog", "buttons": ["ok", "cancel"],
             "confirm_by_plugin": True}]}
        host.press_dialog_button("ask", "apply")
        host.close_view("ask", result="cancel")
        host.close_view("ask", result=None)
        host.close_view("plain")
        host.press_dialog_button("other", "close")  # not described: nothing to check against
        host.press_dialog_button("confirmed", "ok")  # the plugin decides, then hides it
        host.close_view("confirmed", result=None)
        # CILogg always sends result and inputs for a dialog with buttons; a host without
        # view_dialogs knows no buttons and sends neither.
        host.close_view("ask")
        self.assertEqual([(event["view_id"], event["value"]) for event in
                          (events.get(timeout=2) for _ in range(3))],
                         [("ask", "apply"), ("other", "close"), ("confirmed", "ok")])
        received = [closed.get(timeout=2) for _ in range(5)]
        self.assertEqual([params.get("result", "-") for params in received[:4]],
                         ["cancel", None, "-", None])
        self.assertEqual(received[4], {"view_id": "ask", "window_id": "w1", "result": None,
                                       "inputs": {}})
        host.offered_features = [name for name in host.offered_features or []
                                 if name != "view_dialogs"]
        host.close_view("ask")
        self.assertEqual(closed.get(timeout=2), {"view_id": "ask", "window_id": "w1"})
        # Without view_dialogs CILogg shows no dialog buttons, so even a declared one is never
        # clicked (#168).
        for call in (lambda: host.press_dialog_button("ask", "apply"),
                     lambda: host.press_dialog_button("confirmed", "ok"),
                     lambda: host.press_dialog_button("ask", "close"),
                     lambda: host.press_dialog_button("plain", "ok"),
                     lambda: host.press_dialog_button("main", "ok"),
                     lambda: host.close_view("ask", result="yes"),
                     lambda: host.close_view("plain", result=None),
                     lambda: host.close_view("main", result="ok"),
                     lambda: host.close_view("confirmed", result="ok")):
            with self.subTest(), self.assertRaises(ValueError):
                call()

    def test_feature_sets(self):
        testing = self.testing
        self.assertEqual(testing.HOST_FEATURES[-5:], E17F_FEATURES)
        self.assertEqual(testing.E17E_HOST_FEATURES, testing.HOST_FEATURES[:-5])
        self.assertEqual(testing.E17E_HOST_FEATURES[-2:], ("tree", "file_tree"))
        self.assertEqual(dict(testing.E17E_HOST_FEATURE_VERSIONS),
                         {name: version for name, version in testing.HOST_FEATURE_VERSIONS.items()
                          if name not in E17F_FEATURES})
        for feature in E17F_FEATURES:
            self.assertEqual(testing.HOST_FEATURE_VERSIONS[feature], 1)
            self.assertNotIn(feature, testing.E17E_HOST_FEATURES)
        self.assertEqual(testing.DIALOG_BUTTONS, self.sdk.DIALOG_BUTTONS)
        self.assertEqual(testing.DIALOG_BUTTONS_ELEMENT, self.sdk.DIALOG_BUTTONS_ELEMENT)
        plugin = self.plugin()
        self.host(plugin)
        self.assertTrue(all(plugin.supports(feature) for feature in E17F_FEATURES))
        self.assertFalse(plugin.supports("layout", 2))


if __name__ == "__main__":
    unittest.main()
