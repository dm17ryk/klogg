"""E17b parity fixture: get_plugin_menus through klogg.app.command.

Run with run_global_script (through u15_chain.py, in the same global script as e17_tooltips: a host
runs one global script). It reads the ui-gallery Plugins menu, the same read the CLI and native
MCP make (tests/mcp/workbench_menus.py).
"""

import json

from klogg import app


def main():
    arguments = app.script_args()
    report = {"errors": []}
    try:
        report["before"] = app.command("get_plugin_menus", location="plugins",
                                       plugin_id=arguments["plugin"])
    except Exception as error:  # reported to the parity driver, which fails the case
        report["errors"].append(f"{type(error).__name__}: {error}")
    with open(arguments["output"], "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False)


main()
