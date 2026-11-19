"""E17d parity fixture: a ui-gallery check box changed through klogg.app.command.

Run with run_global_script (through u15_chain.py, in the same global script as e17_tooltips: a host
runs one global script). It changes the check box of the third board with set_ui, as the CLI and
native MCP pick element menu entries (tests/mcp/workbench_tables.py), and reads the plugin's answer
in the table status label.
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


def shown_row(table, title):
    """The shown row of a board: the gallery orders boards by name or by state."""
    for row, cells in enumerate(find(table).get("items") or []):
        if cells and cells[0] == title:
            return row
    raise RuntimeError(f"{title} is not shown")


def main():
    arguments = app.script_args()
    base = f"pluginView_{arguments['plugin']}_{arguments['view']}_"
    report = {"errors": []}
    try:
        # The plugin keeps the check state: check the box, or clear it if it was checked.
        expected = None
        row = shown_row(base + "boards", "Board 3")
        for state, word in ((2, "true"), (0, "false")):
            try:
                app.command("set_ui", window_index=0, object_name=base + "boards",
                            definition={"indexPath": [row], "column": 0, "checkState": state})
                expected = f"checked b3 {word}"
                break
            except Exception:  # the model refuses a state the row already has
                continue
        if expected is None:
            raise RuntimeError("the check box did not change")
        deadline = time.monotonic() + 20
        text = status(base + "table_status")
        while text != expected and time.monotonic() < deadline:
            time.sleep(0.05)
            text = status(base + "table_status")
        report["status"] = text
        report["expected"] = expected
    except Exception as error:  # reported to the parity driver, which fails the case
        report["errors"].append(f"{type(error).__name__}: {error}")
    with open(arguments["output"], "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False)


main()
