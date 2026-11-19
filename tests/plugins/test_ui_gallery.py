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


def load_testing():
    spec = importlib.util.spec_from_file_location("ui_gallery_test_host", SDK / "cilogg_plugin_testing.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


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

    def open_gallery(self):
        self.host.open_view("gallery")
        return elements(self.host.wait_for("view/update", lambda params: "root" in params)["root"])

    def test_manifest_is_disabled_by_default_and_lists_its_queries(self):
        manifest = json.loads((EXAMPLE / "plugin.json").read_text(encoding="utf-8"))
        self.assertFalse(manifest.get("enabled_by_default", False))
        self.assertEqual(manifest["handles"], ["tooltip/resolve", "query/cancel"])
        self.assertEqual([(view["id"], view["kind"]) for view in manifest["views"]], [("gallery", "dock")])

    def test_current_host_gets_icons_properties_and_tooltip_answers(self):
        host = self.host
        result = host.initialize()
        self.assertEqual(result["features"], ["query/cancel", "tooltip/resolve"])
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
        self.assertEqual(host.errors, [])


if __name__ == "__main__":
    unittest.main()
