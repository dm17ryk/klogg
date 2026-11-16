"""U15 parity: one source selection (focused, all, tab:<id>, file:<path>) on every surface.

`u15-docks` runs the U15a docks with each source through the CLI, native MCP and the public
Python SDK (klogg.app); `u15b-sources` runs search, Preview, plugin actions and views and Script
Runner through the GUI controls, the CLI, native MCP and Python (docs: plans/.../tasks/U15.md).
"""

import json
import logging
import shutil
import time
from pathlib import Path

from workbench_contracts import ParityError, compare_json

LOG = logging.getLogger("workbench.sources")
ROOT = Path(__file__).resolve().parents[2]
PYTHON_FIXTURES = ROOT / "test_data/workbench/python"
PLUGIN = "org.cilogg.view-demo"
TERMINAL = ("completed", "failed", "cancelled")
LOGS = {
    "two.log": "ERROR one\nWARN a\nERROR two\nok\n",
    "one.log": "fine\nERROR once\nWARN b\n",
    "none.log": "nothing here\nstill nothing\n",
}
REGEX = {"pattern": "(?<level>ERROR|WARN) (?<rest>\\w+)", "scope": "whole_file"}
COUNTS = {"pattern": "(?<level>ERROR|WARN)", "scope": "whole_file", "counts": {"capture": "level"}}


def rounded(value):
    """The CLI prints doubles with fewer digits than the script RPC's JSON; compare 9 decimals."""
    if isinstance(value, float):
        return round(value, 9)
    if isinstance(value, dict):
        return {key: rounded(item) for key, item in value.items()}
    if isinstance(value, list):
        return [rounded(item) for item in value]
    return value


def install_python_runtime(payload: Path) -> None:
    """The scripts of the private host use this checkout's klogg SDK."""
    shutil.copytree(ROOT / "src/app/python_runtime", payload / "python_runtime",
                    ignore=shutil.ignore_patterns("__pycache__"), dirs_exist_ok=True)


def wait_until(predicate, timeout, reason):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise ParityError(reason)


def open_logs(cli, artifacts):
    folder = artifacts / "u15-logs"
    folder.mkdir(exist_ok=True)
    paths = {}
    for name, text in LOGS.items():
        path = folder / name
        path.write_text(text, encoding="utf-8", newline="\n")
        paths[name] = path
        cli("open_file", "--file", str(path))

    def tabs():
        window = cli("get_info")["windows"][0]
        found = {Path(tab["filePath"]).name: tab["tabId"] for tab in window["tabs"]
                 if Path(tab["filePath"]).parent == folder.resolve()
                 or Path(tab["filePath"]).parent == folder}
        return found if len(found) == len(LOGS) else None

    return paths, wait_until(tabs, 30, "u15_logs_not_open")


def ui_object(command, name):
    return next((item for item in command("get_ui", {"window_index": 0})["objects"]
                 if item.get("objectName") == name), None)


def require_control(command, name):
    found = wait_until(lambda: ui_object(command, name), 20, f"gui_control_missing: {name}")
    if not found.get("accessibleName"):
        raise ParityError(f"gui_control_unnamed: {name}")
    return {"objectName": name, "accessibleName": found["accessibleName"]}


def run_python(cli, artifacts, fixture, arguments):
    output = artifacts / (fixture + ".result.json")
    args = artifacts / (fixture + ".args.json")
    args.write_text(json.dumps(dict(arguments, output=str(output))), encoding="utf-8")
    cli("run_global_script", "--script-file", str(PYTHON_FIXTURES / (fixture + ".py")),
        "--args-json-file", str(args))
    try:
        wait_until(output.exists, 120, f"python_fixture_timeout: {fixture}")
        time.sleep(0.2)
        return json.loads(output.read_text(encoding="utf-8"))
    finally:
        status = cli("get_global_script_status", success=None)
        (artifacts / (fixture + ".status.json")).write_text(json.dumps(status, indent=2), encoding="utf-8")
        cli("stop_global_script", success=None)
        wait_until(lambda: cli("get_global_script_status", success=None).get("state")
                   not in ("starting", "running", "stopping"), 20, "global_script_not_stopped")


def dock_rows(cli, command, through_mcp, setter, getter, definition, source):
    request = dict(definition, source=source, operation="run")
    if through_mcp:
        command(setter, {"window_index": 0, "definition": request})
    else:
        cli(setter, "--window-index", "0", "--analysis-json", json.dumps(request))

    def finished():
        state = command(getter, {"window_index": 0}) if through_mcp else cli(getter, "--window-index", "0")
        runs = state.get("runs", [])
        return runs if runs and all(run["state"] in TERMINAL for run in runs) else None

    runs = wait_until(finished, 60, f"dock_run_timeout: {getter} {source}")
    result = {}
    for run in runs:
        rows, cursor, seen = [], "", set()
        while True:
            if cursor in seen:
                raise ParityError("pagination_stalled")
            seen.add(cursor)
            page = (command("get_analysis_results", {"job_id": run["jobId"], "page_size": 200, "cursor": cursor})
                    if through_mcp else
                    cli("get_analysis_results", "--job-id", run["jobId"], "--page-size", "200", "--cursor", cursor))
            rows.extend({k: v for k, v in row.items() if k not in ("jobId", "requestRevision", "source")}
                        for row in page["rows"])
            cursor = page.get("nextCursor", "")
            if not cursor:
                break
        result[run["tabId"]] = rows
    if through_mcp:
        command(setter, {"window_index": 0, "definition": {"operation": "release"}})
    else:
        cli(setter, "--window-index", "0", "--analysis-json", json.dumps({"operation": "release"}))
    return result


def exercise_docks(cli, command, catalog, artifacts):
    """u15_python_source_parity: the U15a docks give the same rows on every surface."""
    paths, tabs = open_logs(cli, artifacts)
    cli("focus_tab", "--tab-id", tabs["one.log"])
    sources = [f"tab:{tabs['two.log']}", f"file:{paths['none.log']}", "focused", "all"]
    surfaces = {"cli": {"regexLab": {}, "valueCounts": {}}, "mcp": {"regexLab": {}, "valueCounts": {}}}
    for name, through_mcp in (("cli", False), ("mcp", True)):
        for source in sources:
            surfaces[name]["regexLab"][source] = dock_rows(
                cli, command, through_mcp, "set_regex_lab", "get_regex_lab", REGEX, source)
            surfaces[name]["valueCounts"][source] = dock_rows(
                cli, command, through_mcp, "set_value_counts", "get_value_counts", COUNTS, source)
    python = run_python(cli, artifacts, "u15_sources",
                        {"sources": sources, "regex": REGEX, "counts": COUNTS})
    if python.get("errors"):
        raise ParityError(f"python_fixture_failed: {python['errors']}")
    compare_json(rounded(surfaces["cli"]), rounded(surfaces["mcp"]), "$.mcp")
    compare_json(rounded(surfaces["cli"]),
                 rounded({key: python[key] for key in ("regexLab", "valueCounts")}), "$.python")
    expected_tabs = {
        sources[0]: [tabs["two.log"]], sources[1]: [tabs["none.log"]],
        "focused": [tabs["one.log"]], "all": sorted(tabs.values()),
    }
    for source, tab_ids in expected_tabs.items():
        compare_json(tab_ids, sorted(surfaces["cli"]["regexLab"][source]), f"$.tabs[{source}]")
    lines = [row["lineNumber"] for row in surfaces["cli"]["regexLab"][sources[0]][tabs["two.log"]]]
    compare_json([1, 2, 3], lines, "$.regexLab.two.lines")
    LOG.info("u15_docks_completed sources=%d", len(sources))
    return {"id": "u15-docks", "passed": True, "sources": sources}


def exercise_tools(cli, command, catalog, artifacts):
    """The U15b tools: search, Preview, plugin actions and views, Script Runner."""
    options = catalog.get("commander", {}).get("options", {})
    for name in ("source", "view_id"):
        if name not in options or options[name].get("type") != "value":
            raise ParityError(f"commander_option_missing: {name}")
    if "open_plugin_view" not in catalog.get("commander", {}).get("commands", {}):
        raise ParityError("open_plugin_view_missing")
    paths, tabs = open_logs(cli, artifacts)
    first, second, empty = tabs["two.log"], tabs["one.log"], tabs["none.log"]
    report = {"gui": []}

    # Search in all open tabs: per-tab match counts, the same on the CLI and native MCP.
    expected = {first: 2, second: 1, empty: 0}
    cli_search = cli("search", "--text", "ERROR", "--source", "all", "--wait", "--timeout-ms", "20000")
    mcp_search = command("search", {"text": "ERROR", "source": "all", "wait": True, "timeout_ms": 20000})
    for name, payload in (("cli", cli_search), ("mcp", mcp_search)):
        if not payload.get("complete"):
            raise ParityError(f"search_incomplete: {name}")
        compare_json(expected, {tab["tabId"]: tab["matches"] for tab in payload["tabs"]}, f"$.search.{name}")
    report["gui"].append(require_control(command, "searchAllTabsButton"))

    # Preview: a line names one source; "all" is refused with invalid_source.
    for name, payload in (
        ("cli", cli("preview_message", "--analysis-json", json.dumps({"line": 2}),
                    "--source", f"tab:{first}", success=None)),
        ("mcp", command("preview_message", {"analysis_json": {"line": 2}, "source": f"tab:{first}"},
                        success=None)),
    ):
        compare_json(first, payload.get("source", {}).get("tabId"), f"$.preview.{name}.tabId")
        if "WARN a" not in payload.get("inputText", ""):
            raise ParityError(f"preview_wrong_line: {name}")
    refused = cli("preview_message", "--analysis-json", json.dumps({"line": 2}), "--source", "all",
                  success=False)
    compare_json("invalid_source", refused.get("reason"), "$.preview.all")
    refused = command("preview_message", {"analysis_json": {"line": 2}, "source": "all"}, success=None)
    compare_json("invalid_source", refused.get("reason"), "$.preview.all.mcp")
    report["gui"].append(require_control(command, "previewOpenLine"))

    # Plugin actions: one tab, and every open tab (staged).
    cli("plugin_manage", "--plugin-operation", "enable", "--plugin-id", PLUGIN)
    job = cli("invoke_plugin", "--plugin-id", PLUGIN, "--plugin-action", "reset", "--source", f"tab:{second}")
    done = wait_until(lambda: (lambda j: j if j["state"] in TERMINAL else None)(
        cli("get_plugin_job", "--plugin-job-id", job["jobId"])), 40, "plugin_job_timeout")
    compare_json(second, done["context"].get("tabId"), "$.plugin.tab")
    staged = command("invoke_plugin", {"plugin_id": PLUGIN, "plugin_action": "reset", "source": "all"})
    compare_json(sorted(tabs.values()), sorted(source["tabId"] for source in staged["sources"]), "$.plugin.all")

    def ran_everywhere():
        jobs = [j for j in command("get_plugins")["jobs"]
                if j.get("pluginId") == PLUGIN and j.get("action") == "reset" and j["state"] in TERMINAL]
        covered = {j.get("context", {}).get("tabId") for j in jobs}
        return covered >= set(tabs.values())

    wait_until(ran_everywhere, 60, "plugin_all_not_run_everywhere")
    report["gui"].append(require_control(command, "pluginSourcePicker"))

    # Plugin views follow the chosen tab.
    view = cli("open_plugin_view", "--plugin-id", PLUGIN, "--view-id", "main", "--source", f"tab:{first}")
    compare_json(f"tab:{first}", view.get("source"), "$.view.cli")
    view = command("open_plugin_view", {"plugin_id": PLUGIN, "view_id": "main", "source": f"tab:{second}"})
    compare_json(f"tab:{second}", view.get("source"), "$.view.mcp")
    report["gui"].append(require_control(command, f"pluginViewSource_{PLUGIN}_main"))

    # Script Runner: a script needs a live COM tab; the private host has none.
    script = artifacts / "u15-noop.py"
    script.write_text("pass\n", encoding="utf-8")
    refused = cli("run_script", "--script-file", str(script), "--source", f"tab:{first}", success=False)
    compare_json("not_com_tab", refused.get("reason"), "$.script.tab")
    refused = cli("run_script", "--script-file", str(script), "--source", "all", success=False)
    compare_json("no_open_sources", refused.get("reason"), "$.script.all")
    try:
        command("run_script", {"script_file": str(script), "source": "all"}, success=False)
    except (KeyError, TypeError):
        pass  # failures without a payload object are still reported as tool errors

    # Python: klogg.app.command reaches the same operations.
    python = run_python(cli, artifacts, "u15b_sources", {
        "tabs": [first, second, empty], "plugin": PLUGIN, "search": "ERROR", "script": str(script)})
    compare_json(expected, python["search"], "$.python.search")
    compare_json(first, python["preview"]["tabId"], "$.python.preview")
    compare_json("invalid_source", python["previewAll"], "$.python.previewAll")
    compare_json(second, python["pluginJobTab"], "$.python.plugin")
    compare_json(sorted(tabs.values()), sorted(python["pluginAllSources"]), "$.python.pluginAll")
    compare_json(f"tab:{first}", python["viewSource"], "$.python.view")
    compare_json("not_com_tab", python["scriptOnFile"], "$.python.scriptOnFile")
    compare_json("no_open_sources", python["scriptOnAll"], "$.python.scriptOnAll")
    LOG.info("u15b_tools_completed gui=%s", report["gui"])
    return {"id": "u15b-sources", "passed": True, "guiControls": report["gui"]}
