"""U15b parity fixture: the remaining tab-bound tools through klogg.app.command.

Run with run_global_script. The arguments file names the output file, the open tabs and the
plugin; the fixture records what search, Preview, plugin actions and views, and Script Runner
return for each source, the same operations the CLI and native MCP run.
"""

import json

from klogg import KloggError, app


def refusal(action, **request):
    try:
        app.command(action, **request)
    except KloggError as error:
        return (error.payload or {}).get("reason") or str(error)
    return "accepted"


def main():
    arguments = app.script_args()
    first, second = arguments["tabs"][0], arguments["tabs"][1]
    plugin = arguments["plugin"]
    report = {}
    search = app.command("search", text=arguments["search"], source="all", wait=True,
                         timeout_ms=20000)
    report["search"] = {tab["tabId"]: tab["matches"] for tab in search["tabs"]}
    report["searchComplete"] = search["complete"]
    try:
        preview = app.command("preview_message", analysis_json={"line": 2}, source=f"tab:{first}")
    except KloggError as error:  # a decoder failure still names the source
        preview = error.payload or {}
    report["preview"] = {"tabId": preview.get("source", {}).get("tabId"),
                         "inputText": preview.get("inputText", "")}
    report["previewAll"] = refusal("preview_message", analysis_json={"line": 2}, source="all")
    job = app.command("invoke_plugin", plugin_id=plugin, plugin_action="reset",
                      source=f"tab:{second}")
    report["pluginJobTab"] = app.command("get_plugin_job", plugin_job_id=job["jobId"])["context"].get("tabId")
    staged = app.command("invoke_plugin", plugin_id=plugin, plugin_action="reset", source="all")
    report["pluginAllSources"] = [source["tabId"] for source in staged["sources"]]
    view = app.command("open_plugin_view", plugin_id=plugin, view_id="main", source=f"tab:{first}")
    report["viewSource"] = view["source"]
    report["scriptOnFile"] = refusal("run_script", script_file=arguments["script"],
                                     source=f"tab:{first}")
    report["scriptOnAll"] = refusal("run_script", script_file=arguments["script"], source="all")
    with open(arguments["output"], "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False)


main()
