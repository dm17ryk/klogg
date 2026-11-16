"""Split-pane operations through CLI, native MCP and the Python API (U16b).

The case drives one private host: CLI and native MCP requests alternate, every
state change is read back through the other transport, and a global Python
script performs the same operations through ``klogg.app``.
"""

import json
import logging
import time
from pathlib import Path

from workbench_contracts import ParityError, compare_json

LOG = logging.getLogger("workbench.panes")
WINDOW = 0
OPERATIONS = ("split_pane", "focus_pane", "close_pane", "merge_panes", "move_tab")

# Runs inside CILogg as a global script; it reports what the Python API returned.
PYTHON_SCRIPT = '''
import json
from klogg import KloggError, app

args = app.script_args()
report = {}
try:
    window = args["windowIndex"]
    report["before"] = [pane["paneId"] for pane in app.panes(window_index=window)]
    created = app.split_pane("down", window_index=window, pane_index=0)
    report["split"] = created
    report["moved"] = app.move_tab(args["tabId"], pane_id=created["paneId"])
    report["movedBack"] = app.tab(tab_id=args["tabId"]).move_to_pane(pane_index=0)
    report["focused"] = app.focus_pane(pane_id=created["paneId"])
    app.move_tab(args["tabId"], pane_id=created["paneId"])
    try:
        app.close_pane(pane_id=created["paneId"])
        report["refusal"] = None
    except KloggError as error:
        report["refusal"] = {"code": error.code, "message": error.message}
    report["closed"] = app.close_pane(pane_id=created["paneId"], tabs="move")
    report["beforeMerge"] = app.panes(window_index=window)
    report["merged"] = app.merge_panes(window_index=window)
    report["after"] = app.panes(window_index=window)
except Exception as error:  # Reported to the parity runner, which fails the case.
    report["error"] = repr(error)
with open(args["output"], "w", encoding="utf-8") as handle:
    json.dump(report, handle)
'''


def wait_value(read, accept, reason, timeout=20):
    deadline = time.monotonic() + timeout
    while True:
        value = read()
        if accept(value):
            return value
        if time.monotonic() >= deadline:
            LOG.error("pane_wait_failed reason=%s", reason)
            raise ParityError(reason)
        time.sleep(0.05)


def exercise_panes(cli, command, catalog, artifacts):
    """Use only the files and panes this case creates; leave one pane behind."""
    commands = catalog["commander"]["commands"]
    options = catalog["commander"]["options"]
    for name in OPERATIONS:
        if name not in commands:
            raise ParityError(f"pane_operation_missing: {name}")
    for name in ("pane_id", "pane_index", "direction", "tabs"):
        if name not in options:
            raise ParityError(f"pane_option_missing: {name}")

    paths = []
    for name in ("pane-a.log", "pane-b.log", "pane-c.log"):
        path = artifacts / name
        path.write_bytes(f"{name} first\n{name} second\n".encode("utf-8"))
        paths.append(path)
    opened, cleanup_errors = [], []

    def window_via(cli_transport):
        info = cli("get_info") if cli_transport else command("get_info")
        windows = [w for w in info["windows"] if w["windowIndex"] == WINDOW]
        if len(windows) != 1:
            raise ParityError("pane_window_missing")
        return windows[0]

    def window():
        return window_via(False)

    def layout(info):
        """The pane facts both transports must agree on."""
        return {
            "activePaneId": info["activePaneId"],
            "panes": info["panes"],
            "tabs": [
                {key: t[key] for key in ("tabId", "tabIndex", "paneId", "paneIndex", "paneTabIndex")}
                for t in info["tabs"]
            ],
        }

    def same_layout(reason):
        through_cli, through_mcp = layout(window_via(True)), layout(window_via(False))
        compare_json(through_cli, through_mcp, f"$.{reason}")
        return through_mcp

    def tab_id(path):
        found = [t["tabId"] for t in window()["tabs"] if Path(t["filePath"]).resolve() == path.resolve()]
        return found[0] if len(found) == 1 else None

    def pane_of(info, tab):
        return next(t["paneId"] for t in info["tabs"] if t["tabId"] == tab)

    def rejected(cli_arguments, mcp_arguments, action, reason):
        # The CLI prints the Commander message as its last stderr line; native MCP
        # reports the same CLI output in its error text.
        lines = cli(action, *cli_arguments, success=False).get("error", "").splitlines()
        cli_error = lines[-1].strip() if lines else ""
        mcp_error = command(action, mcp_arguments, success=False).get("error", "")
        if not cli_error or cli_error not in mcp_error:
            LOG.error("pane_rejection_mismatch cli=%r mcp=%r", cli_error, mcp_error)
            raise ParityError(f"pane_rejection_mismatch: {reason}")
        return cli_error

    def cleanup(action, arguments):
        try:
            command(action, arguments)
        except (RuntimeError, AssertionError, OSError, KeyError) as error:
            LOG.error("pane_cleanup_failed action=%s reason=%s", action, error)
            cleanup_errors.append(action)

    result = None
    try:
        start = window()
        if len(start["panes"]) != 1:
            raise ParityError("pane_case_needs_one_pane")
        first = start["panes"][0]["paneId"]
        cli("open_file", "--file", str(paths[0]))
        command("open_file", {"file": str(paths[1])})
        cli("open_file", "--file", str(paths[2]))
        a, b, c = (
            wait_value(lambda path=path: tab_id(path), bool, "pane_tab_not_opened") for path in paths
        )
        opened.extend([a, b, c])
        info = same_layout("opened")
        if {pane_of(info, tab) for tab in (a, b, c)} != {first} or info["activePaneId"] != first:
            raise ParityError("pane_initial_layout")

        # CLI split, native MCP move, CLI focus: every change is read back through the other transport.
        split = cli("split_pane", "--window-index", str(WINDOW), "--direction", "right")
        right = split["paneId"]
        compare_json([first, right], [p["paneId"] for p in split["panes"]], "$.split.panes")
        compare_json(right, same_layout("split")["activePaneId"], "$.split.active")
        moved = command("move_tab", {"tab_id": a, "pane_id": right})
        compare_json([a, right, 1, 0], [moved["tabId"], moved["paneId"], moved["paneIndex"], moved["paneTabIndex"]], "$.move")
        info = same_layout("moved")
        compare_json(right, pane_of(info, a), "$.moved.pane")
        compare_json(a, info["tabs"][-1]["tabId"], "$.moved.order")
        focused = cli("focus_pane", "--window-index", str(WINDOW), "--pane-index", "0")
        compare_json(first, focused["paneId"], "$.focus.pane")
        state = command("dump_state", {"window_index": WINDOW})["ciloggState"]
        compare_json([first, 0, 2], [state["activePaneId"], state["activePaneIndex"], state["paneCount"]], "$.focus.state")
        compare_json(str(paths[2].resolve()), str(Path(state["activeFile"]).resolve()), "$.focus.file")

        # A new pane below an existing one, through both transports.
        below = command("split_pane", {"pane_id": right, "direction": "down"})["paneId"]
        lower = cli("move_tab", "--tab-id", b, "--direction", "down")["paneId"]
        info = same_layout("stacked")
        paths_by_pane = {p["paneId"]: p["orientationPath"] for p in info["panes"]}
        compare_json(
            {
                first: [{"orientation": "horizontal", "position": 0}, {"orientation": "vertical", "position": 0}],
                lower: [{"orientation": "horizontal", "position": 0}, {"orientation": "vertical", "position": 1}],
                right: [{"orientation": "horizontal", "position": 1}, {"orientation": "vertical", "position": 0}],
                below: [{"orientation": "horizontal", "position": 1}, {"orientation": "vertical", "position": 1}],
            },
            paths_by_pane,
            "$.stacked.orientationPath",
        )

        # Window-wide tab indices still count pane by pane in layout order.
        order = [t["tabId"] for t in info["tabs"]]
        compare_json([c, b, a], order[-3:], "$.stacked.order")
        cli("focus_tab", "--window-index", str(WINDOW), "--tab-index", str(order.index(b)))
        for through_cli in (True, False):
            focused_window = window_via(through_cli)
            compare_json(
                [lower, b, order.index(b)],
                [focused_window["activePaneId"], focused_window["currentTabId"], focused_window["currentTabIndex"]],
                "$.tab_index",
            )

        # A pane with tabs is never closed without tabs=close|move, and no dialog opens.
        message = rejected(["--pane-id", right], {"pane_id": right}, "close_pane", "busy_pane")
        if "tabs" not in message:
            raise ParityError("pane_refusal_reason")
        rejected(["--tab-id", a, "--pane-id", "missing-pane"], {"tab_id": a, "pane_id": "missing-pane"}, "move_tab", "missing_pane")
        compare_json(4, len(same_layout("refused")["panes"]), "$.refused.panes")
        command("close_pane", {"pane_id": below})
        closed = cli("close_pane", "--pane-id", right, "--tabs", "move")
        compare_json([right, [a]], [closed["closedPaneId"], closed["movedTabIds"]], "$.close")
        info = same_layout("closed")
        compare_json([first, lower], [p["paneId"] for p in info["panes"]], "$.closed.panes")
        if pane_of(info, a) not in (first, lower):
            raise ParityError("pane_moved_tab_lost")

        # The same operations through the Python API of a global script.
        script = artifacts / "pane-script.py"
        script.write_text(PYTHON_SCRIPT, encoding="utf-8")
        output = artifacts / "pane-script-result.json"
        script_args = artifacts / "pane-script-args.json"
        script_args.write_text(
            json.dumps({"windowIndex": WINDOW, "tabId": c, "output": str(output)}), encoding="utf-8"
        )
        cli("run_global_script", "--script-file", str(script), "--args-json-file", str(script_args))
        status = wait_value(
            lambda: command("get_global_script_status"),
            lambda value: value.get("state") in ("finished", "failed", "cancelled"),
            "pane_script_timeout",
            timeout=60,
        )
        if status["state"] != "finished" or not output.is_file():
            LOG.error("pane_script_failed status=%s", json.dumps(status))
            raise ParityError("pane_script_failed")
        report = json.loads(output.read_text(encoding="utf-8"))
        if "error" in report:
            LOG.error("pane_script_error error=%s", report["error"])
            raise ParityError("pane_script_error")
        created = report["split"]["paneId"]
        compare_json(c, report["moved"]["tabId"], "$.python.move")
        compare_json([created, 1], [report["moved"]["paneId"], report["moved"]["paneIndex"]], "$.python.move.pane")
        compare_json(0, report["movedBack"]["paneIndex"], "$.python.moveBack")
        compare_json(created, report["focused"]["paneId"], "$.python.focus")
        if not report["refusal"] or "tabs" not in report["refusal"]["message"]:
            raise ParityError("python_refusal_missing")
        compare_json([created, [c]], [report["closed"]["closedPaneId"], report["closed"]["movedTabIds"]], "$.python.close")
        compare_json(1, len(report["merged"]["panes"]), "$.python.merge")
        info = same_layout("python")
        compare_json(info["panes"], report["after"], "$.python.after")
        # Merging appends the panes' tabs pane by pane, keeping the window-wide order.
        expected_order = [tab for pane in report["beforeMerge"] for tab in pane["tabIds"]]
        compare_json(expected_order, [t["tabId"] for t in info["tabs"]], "$.python.order")
        compare_json(expected_order, report["after"][0]["tabIds"], "$.python.after.tabs")

        # merge_panes through both transports leaves one pane.
        cli("split_pane", "--window-index", str(WINDOW))
        command("merge_panes", {"window_index": WINDOW})
        compare_json(1, len(same_layout("merged")["panes"]), "$.merged.panes")
        result = {
            "id": "pane_operations_parity",
            "passed": True,
            "operations": list(OPERATIONS),
            "transports": ["cli", "native-mcp", "python"],
            "guiMode": "real MainWindow pane tree; offscreen, physical UI unmeasured",
        }
        LOG.info("pane_case_completed panes=%s", json.dumps(info["panes"]))
    finally:
        cleanup("merge_panes", {"window_index": WINDOW})
        for tab in reversed(opened):
            cleanup("close_tab", {"tab_id": tab})
    if cleanup_errors:
        raise ParityError("pane_cleanup_failed: " + ",".join(cleanup_errors))
    (artifacts / "pane-result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    return result
