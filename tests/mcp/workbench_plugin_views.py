"""E13 parity: a plugin-view button, the cancelling job state and pause_comm --wait.

The private host gets the shipped view-demo example; GUI activation, the CLI and native MCP
must reach the same Commander operations (docs/plugin-views.md).
"""

import logging
import shutil
import time
from pathlib import Path

from workbench_contracts import ParityError

LOG = logging.getLogger("workbench.plugin_views")
ROOT = Path(__file__).resolve().parents[2]
PLUGIN = "org.cilogg.view-demo"
TERMINAL = ("completed", "failed", "cancelled")


def install_view_demo(payload: Path) -> None:
    """Copy the view-demo package (and its SDK) into the private runtime's plugin folder."""
    target = payload / "plugins" / "view-demo"
    shutil.copytree(ROOT / "plugins/examples/view-demo", target,
                    ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copy2(ROOT / "plugins/sdk/python/cilogg_plugin.py", target / "cilogg_plugin.py")


def wait_until(predicate, timeout, reason):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise ParityError(reason)


def ui_object(command, name):
    for item in command("get_ui", {"window_index": 0})["objects"]:
        if item.get("objectName") == name:
            return item
    return None


def finished_job(cli, job_id, timeout=30):
    return wait_until(
        lambda: (lambda job: job if job["state"] in TERMINAL else None)(
            cli("get_plugin_job", "--plugin-job-id", job_id)),
        timeout, f"plugin_job_timeout: {job_id}")


def exercise_plugin_views(cli, command, catalog, artifacts):
    options = catalog.get("commander", {}).get("options", {})
    if "wait" not in options or options["wait"].get("type") != "boolean":
        raise ParityError("pause_comm_wait_option_missing")
    cli("plugin_manage", "--plugin-operation", "enable", "--plugin-id", PLUGIN)
    demo = next((p for p in command("get_plugins")["plugins"] if p["id"] == PLUGIN), None)
    if not demo or {view["id"] for view in demo.get("views", [])} != {"main", "editor", "confirm"}:
        raise ParityError("view_demo_manifest_missing")

    # GUI: open the dock from Plugins -> View Demo, type a name and press the job button.
    command("activate_ui", {"window_index": 0, "object_name": f"pluginViewAction_{PLUGIN}_main"})
    button = f"pluginView_{PLUGIN}_main_process"
    found = wait_until(lambda: (lambda item: item if item and item.get("enabled") else None)(
        ui_object(command, button)), 40, "view_not_rendered")
    if not found.get("accessibleName"):
        raise ParityError("view_button_accessible_name_missing")
    command("set_ui", {"window_index": 0, "object_name": f"pluginView_{PLUGIN}_main_name",
                       "definition": {"text": "parity"}})
    before = {job["jobId"] for job in command("get_plugins")["jobs"]}
    command("activate_ui", {"window_index": 0, "object_name": button})
    gui_job = wait_until(lambda: next((job for job in command("get_plugins")["jobs"]
                                       if job["jobId"] not in before), None),
                         20, "view_button_job_missing")
    gui_result = finished_job(cli, gui_job["jobId"])
    if gui_result["state"] != "completed" or gui_result["result"]["inputs"].get("name") != "parity":
        raise ParityError(f"view_button_job_failed: {gui_result}")

    # CLI and native MCP run the same action; cancellation reports `cancelling` first.
    states = {}
    for surface in ("cli", "mcp"):
        if surface == "cli":
            started = cli("invoke_plugin", "--plugin-id", PLUGIN, "--plugin-action", "process")
        else:
            started = command("invoke_plugin", {"plugin_id": PLUGIN, "plugin_action": "process"})
        job_id = started["jobId"]
        wait_until(lambda: cli("get_plugin_job", "--plugin-job-id", job_id)["state"] == "running",
                   20, f"job_not_running: {surface}")
        cancelled = (cli("cancel_plugin_job", "--plugin-job-id", job_id) if surface == "cli"
                     else command("cancel_plugin_job", {"plugin_job_id": job_id}))
        final = finished_job(cli, job_id)
        states[surface] = [cancelled["state"], final["state"]]
        if states[surface] != ["cancelling", "cancelled"]:
            raise ParityError(f"cancel_grace_states: {surface} {states[surface]}")

    # pause_comm --wait parses on both surfaces and fails cleanly without a COM tab.
    cli("pause_comm", "--wait", "--timeout-ms", "500", success=False)
    try:
        command("pause_comm", {"wait": True, "timeout_ms": 500}, success=False)
    except KeyError:
        pass  # Failures without a payload object are still reported as tool errors.
    LOG.info("plugin_views_completed gui_job=%s states=%s", gui_job["jobId"], states)
    return {"id": "plugin-views", "passed": True, "guiJob": gui_job["jobId"], "cancelStates": states}
