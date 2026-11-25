"""E17f parity fixture: the ui-gallery layout view's tabs changed through klogg.app.command.

Run with run_global_script (through u15_chain.py, in the same global script as e17_tooltips: a host
runs one global script). It opens the gallery's layout view, switches its tabs to the second tab
with set_ui currentIndex, as the CLI and native MCP pick the menu button's drop-down entries
(tests/mcp/workbench_layout.py), and reads the plugin's answer in the layout view's status label.
"""

import json
import time

from klogg import app


def find(name):
    for item in app.command("get_ui", window_index=0)["objects"]:
        if item.get("objectName") == name:
            return item
    return {}


def status(name):
    return find(name).get("properties", {}).get("text")


def main():
    arguments = app.script_args()
    base = f"pluginView_{arguments['plugin']}_{arguments['view']}_"
    report = {"errors": []}
    try:
        app.command("open_plugin_view", plugin_id=arguments["plugin"], view_id=arguments["view"])
        deadline = time.monotonic() + 30
        while not find(base + "layout_tabs") and time.monotonic() < deadline:
            time.sleep(0.05)
        # The second tab: the plugin reports the tab id and its index.
        app.command("set_ui", window_index=0, object_name=base + "layout_tabs",
                    definition={"currentIndex": 1})
        expected = "tab tab_log 1"
        deadline = time.monotonic() + 20
        text = status(base + "layout_status")
        while text != expected and time.monotonic() < deadline:
            time.sleep(0.05)
            text = status(base + "layout_status")
        report["status"] = text
        report["expected"] = expected
    except Exception as error:  # reported to the parity driver, which fails the case
        report["errors"].append(f"{type(error).__name__}: {error}")
    with open(arguments["output"], "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False)


main()
