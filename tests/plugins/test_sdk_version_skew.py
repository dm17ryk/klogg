"""Version skew (docs/plugin-sdk-advanced.md, section 20): plugins ship their own SDK copy, so
an old copy must keep working against the new host, and every new example manifest must still
load in the CILogg released before E17."""
import importlib.util
import json
import pathlib
import shutil
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
SDK = ROOT / "plugins/sdk/python"
PINNED_SDK = HERE / "fixtures/sdk_pinned/cilogg_plugin.py"
FIXTURE = HERE / "fixtures/skew-plugin"
# Manifest keys added by E17 (docs/plugin-sdk-advanced.md, sections 4, 6 and 7).
E17_MANIFEST_KEYS = ("handles", "requires", "menus")
E17_ITEM_KEYS = ("icon_ref", "shortcut")  # on actions and views

sys.path.insert(0, str(HERE))
import e14_manifest_rules  # noqa: E402


def load_testing():
    spec = importlib.util.spec_from_file_location("skew_test_host", SDK / "cilogg_plugin_testing.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PinnedSdkOnTheNewHost(unittest.TestCase):
    def setUp(self):
        self.testing = load_testing()
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)

    def start(self, sdk_file):
        package = self.root / sdk_file.parent.name
        shutil.copytree(FIXTURE, package, ignore=shutil.ignore_patterns("__pycache__"))
        shutil.copy2(sdk_file, package / "cilogg_plugin.py")
        host = self.testing.FakeHost.start_package(package, timeout=10)
        self.addCleanup(host.close)
        return host

    def test_pinned_copy_is_the_sdk_before_2_0_0(self):
        first_line, rest = PINNED_SDK.read_text(encoding="utf-8").split("\n", 1)
        self.assertTrue(first_line.startswith("# Pinned copy of plugins/sdk/python/cilogg_plugin.py"))
        self.assertNotIn("SDK_VERSION", rest)
        self.assertIn('raise ValueError(f"Unknown host method: {method}")', rest)

    def run_session(self, host):
        result = host.initialize({"mode": "x"})  # every new initialize key
        self.assertEqual(result["plugin_id"], "org.cilogg.test.skew")
        self.assertEqual(result["actions"][0]["id"], "echo")
        echoed = host.invoke("echo", {"text": "hi"})
        self.assertEqual(echoed["echo"], "hi")
        self.assertEqual(echoed["features"], list(self.testing.HOST_FEATURES))
        self.assertEqual(host.progress[0]["value"], 1.0)
        host.open_view("main", window_id="w1")
        root = host.wait_for("view/update", lambda params: "root" in params)["root"]
        self.assertEqual(root["children"][0], {"type": "label", "id": "status", "text": "ready",
                                               "tooltip": "Status"})
        # E17c adds `valid` to field `changed` events: an additive key both copies ignore.
        host.change_field("main", "status", "typed", valid=False)
        host.view_event("main", "go", "clicked")
        patch = host.wait_for("view/update", lambda params: "patch" in params)
        self.assertEqual(patch["patch"], [{"id": "status", "set": {"text": "clicked"}}])
        host.close_view("main", window_id="w1")
        self.assertEqual(host.shutdown(), 0)
        self.assertEqual(host.errors, [])
        self.assertNotIn("Plugin protocol failed", host.stderr)
        return result

    def test_pinned_sdk_accepts_the_new_initialize_keys(self):
        result = self.run_session(self.start(PINNED_SDK))
        self.assertNotIn("sdk", result)
        self.assertNotIn("features", result)

    def test_current_sdk_runs_the_same_plugin(self):
        result = self.run_session(self.start(SDK / "cilogg_plugin.py"))
        self.assertEqual(result["sdk"], {"name": "cilogg_plugin.py", "version": "2.0.0"})
        self.assertNotIn("features", result)


class ManifestsLoadInThePreviousHost(unittest.TestCase):
    def test_every_example_manifest_follows_the_e14_rules(self):
        manifests = sorted((ROOT / "plugins/examples").glob("*/plugin.json"))
        self.assertIn("ui-gallery", [path.parent.name for path in manifests])
        for path in manifests + [FIXTURE / "plugin.json"]:
            with self.subTest(plugin=path.parent.name):
                manifest = json.loads(path.read_text(encoding="utf-8"))
                if manifest.get("kind") == "config-pack":
                    # E16a1 configuration packs have no process (no executable, no
                    # capabilities); configpack_test.cpp checks the example against their rules.
                    continue
                self.assertEqual(e14_manifest_rules.validate_manifest(manifest), [])

    def test_the_rules_refuse_new_enumeration_values(self):
        base = json.loads((FIXTURE / "plugin.json").read_text(encoding="utf-8"))
        self.assertEqual(e14_manifest_rules.validate_manifest(base), [])
        new_keys = dict(base, handles=["tooltip/resolve"], requires={"host_features": ["menus"]},
                        menus=[{"id": "m"}])
        self.assertEqual(e14_manifest_rules.validate_manifest(new_keys), [])  # unknown keys load

        def changed(mutate):
            manifest = json.loads(json.dumps(base))
            mutate(manifest)
            return e14_manifest_rules.validate_manifest(manifest)

        cases = {
            "view kind": lambda m: m["views"][0].update(kind="panel"),
            "view icon from a menu": lambda m: m["views"][0].update(icon="builtin:chip"),
            "view menu flag": lambda m: m["views"][0].update(menu="main"),
            "view area": lambda m: m["views"][0].update(area="center"),
            "view icon": lambda m: m["views"][0].update(icon="builtin:play"),
            "action icon": lambda m: m["actions"][0].update(icon="../escape.png"),
            "field type": lambda m: m["actions"][0]["fields"][0].update(type="slider"),
            "capability": lambda m: m.update(capabilities=["tool", "menu"]),
            "kind": lambda m: m.update(kind="pack"),
            "version": lambda m: m.update(version="1.0"),
            "executable": lambda m: m.update(executable="C:/python.exe"),
            "configuration": lambda m: m.update(configuration=[{"id": "c", "type": "choice"}]),
        }
        for name, mutate in cases.items():
            with self.subTest(case=name):
                self.assertNotEqual(changed(mutate), [])
        self.assertEqual(changed(lambda m: m["views"][0].update(icon="icons/view.svg")), [])

    def test_e17_keys_only_add_to_the_previous_manifest(self):
        """E17a adds `handles`, `requires` and `icon_ref`, E17b `menus` and `shortcut` on
        actions and views: keys the previous CILogg ignores. E17c adds no manifest key, only
        `field/complete` in `handles`. The ui-gallery manifest without them gets the same
        verdict, and every E17b form loads in the previous CILogg."""
        gallery = json.loads((ROOT / "plugins/examples/ui-gallery/plugin.json").read_text(encoding="utf-8"))
        self.assertTrue({"handles", "menus"} <= set(gallery))
        self.assertIn("field/complete", gallery["handles"])
        stripped = {key: value for key, value in gallery.items() if key not in E17_MANIFEST_KEYS}
        for key in ("actions", "views"):
            if key in stripped:
                stripped[key] = [{name: value for name, value in item.items()
                                  if name not in E17_ITEM_KEYS} for item in stripped[key]]
        self.assertEqual(e14_manifest_rules.validate_manifest(stripped), [])
        self.assertEqual(e14_manifest_rules.validate_manifest(gallery), [])

        manifest = json.loads((FIXTURE / "plugin.json").read_text(encoding="utf-8"))
        manifest.update(handles=gallery["handles"], menus=gallery["menus"],
                        requires={"host_features": ["menus>=1", "shortcuts", "choices",
                                                    "toggles", "field_kinds", "validation>=1"]})
        manifest["views"][0].update(shortcut="Ctrl+Alt+V", icon_ref="builtin:play")
        manifest["actions"][0].update(shortcut=["Ctrl+Alt+E", "F9"], icon_ref={"light": "a.svg",
                                                                              "dark": "b.svg"})
        self.assertEqual(e14_manifest_rules.validate_manifest(manifest), [])


if __name__ == "__main__":
    unittest.main()
