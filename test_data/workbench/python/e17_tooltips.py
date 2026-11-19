"""E17a parity fixture: get_plugin_tooltip through klogg.app.command.

Run with run_global_script (through u15_chain.py). It reads the ui-gallery summary tooltip and a
table cell tooltip, the same reads the CLI and native MCP make (tests/mcp/workbench_tooltips.py).
"""

import json

from klogg import app


def tooltip(plugin, view, element, **cell):
    return app.command("get_plugin_tooltip", plugin_id=plugin, view_id=view, element_id=element,
                       no_cache=True, **cell)


def main():
    arguments = app.script_args()
    report = {"errors": []}
    try:
        report["summary"] = tooltip(arguments["plugin"], arguments["view"], "summary")
        report["cell"] = tooltip(arguments["plugin"], arguments["view"], "boards", row_id="b2",
                                 column_id="state")
    except Exception as error:  # reported to the parity driver, which fails the case
        report["errors"].append(f"{type(error).__name__}: {error}")
    with open(arguments["output"], "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False)


main()
