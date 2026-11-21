"""E17e parity fixture: a ui-gallery tree node selected through klogg.app.command.

Run with run_global_script (through u15_chain.py, in the same global script as e17_tooltips: a host
runs one global script). It opens the gallery's explorer view, selects the second bench with
set_ui (a multi-level indexPath reaches nodes; here the top level), as the CLI and native MCP pick
the tree's element menu entries by node id (tests/mcp/workbench_trees.py), and reads the plugin's
answer in the explorer's status label.
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
        while not find(base + "benches") and time.monotonic() < deadline:
            time.sleep(0.05)
        # The second bench: the plugin reports the selection and the current node.
        app.command("set_ui", window_index=0, object_name=base + "benches",
                    definition={"indexPath": [1], "column": 0})
        expected = "selected bench2 current bench2"
        deadline = time.monotonic() + 20
        text = status(base + "explorer_status")
        while text != expected and time.monotonic() < deadline:
            time.sleep(0.05)
            text = status(base + "explorer_status")
        report["status"] = text
        report["expected"] = expected
    except Exception as error:  # reported to the parity driver, which fails the case
        report["errors"].append(f"{type(error).__name__}: {error}")
    with open(arguments["output"], "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False)


main()
