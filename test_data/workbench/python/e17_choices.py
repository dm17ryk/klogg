"""E17c parity fixture: a ui-gallery control changed through klogg.app.command.

Run with run_global_script (through u15_chain.py, in the same global script as e17_tooltips: a host
runs one global script). It moves the gallery's Log level slider with set_ui, as the CLI and native
MCP change other controls (tests/mcp/workbench_choices.py), and reads the plugin's answer in the
choices status label.
"""

import json
import time

from klogg import app

EXPECTED = "changed level=4 valid=true"


def status(name):
    for item in app.command("get_ui", window_index=0)["objects"]:
        if item.get("objectName") == name:
            return item.get("properties", {}).get("text")
    return None


def main():
    arguments = app.script_args()
    base = f"pluginView_{arguments['plugin']}_{arguments['view']}_"
    report = {"errors": []}
    try:
        app.command("set_ui", window_index=0, object_name=base + "level",
                    definition={"value": 4})
        deadline = time.monotonic() + 20
        text = status(base + "choices_status")
        while text != EXPECTED and time.monotonic() < deadline:
            time.sleep(0.05)
            text = status(base + "choices_status")
        report["status"] = text
    except Exception as error:  # reported to the parity driver, which fails the case
        report["errors"].append(f"{type(error).__name__}: {error}")
    with open(arguments["output"], "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False)


main()
