"""E15a parity fixture: plugin repositories through klogg.app.command.

Run with run_global_script (through u15_chain.py). It adds a repository, moves it first, fetches
its catalog and records the repository list and the catalog result, the same operations the CLI
and native MCP run (tests/mcp/workbench_repositories.py).
"""

import json
import time

from klogg import KloggError, app

TERMINAL = ("completed", "failed", "cancelled")
PACK_ID = "org.cilogg.example.protocol-pack"


def manage(operation, **options):
    if options:
        return app.command("plugin_manage", plugin_operation=operation, json=options)
    return app.command("plugin_manage", plugin_operation=operation)


def finished(job_id):
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        job = app.command("get_plugin_job", plugin_job_id=job_id)
        if job["state"] in TERMINAL:
            return job
        time.sleep(0.05)
    raise RuntimeError(f"catalog job {job_id} did not finish")


def main():
    arguments = app.script_args()
    report = {"errors": []}
    try:
        added = manage("repository_add", name="Parity Python",
                       url=arguments["closed"] + "/python/plugins.json")
        identifier = added["repository"]["id"]
        manage("repository_move", repository=identifier, position=1)
        try:
            manage("repository_add", name="Plain", url="http://example.test/plugins.json")
            report["refusedHttp"] = False
        except KloggError:
            report["refusedHttp"] = True
        job = finished(manage("catalog", repository=identifier)["jobId"])
        report["catalogState"] = job["state"]
        report["catalog"] = [{"repository": item["repository"], "state": item["state"],
                              "entries": item["entries"]}
                             for item in job.get("result", {}).get("repositories", [])]
        # E15c: an update check of the same repository.
        checked = finished(manage("check_updates", repository=identifier)["jobId"])
        result = checked.get("result", {})
        report["checkUpdates"] = {"state": checked["state"], "updates": result.get("updates"),
                                  "checked": result.get("checked")}
        report["repositories"] = manage("repositories")["repositories"]
        # E16a1: the example configuration pack the CLI installed and MCP enabled.
        report["plugins"] = app.command("get_plugins")["plugins"]
        try:
            app.command("plugin_manage", plugin_operation="restart", plugin_id=PACK_ID)
            report["packRestartRefused"] = False
        except KloggError:
            report["packRestartRefused"] = True
        # E16a2: the pack edit the CLI saved, then Reset to Pack Version for the whole pack.
        report["packEdits"] = app.command("plugin_manage", plugin_operation="pack_edits",
                                          plugin_id=PACK_ID)["files"]
        app.command("plugin_manage", plugin_operation="pack_reset", plugin_id=PACK_ID)
        report["packEditsAfterReset"] = app.command("plugin_manage", plugin_operation="pack_edits",
                                                    plugin_id=PACK_ID)["files"]
    except Exception as error:  # reported to the parity driver, which fails the case
        report["errors"].append(f"{type(error).__name__}: {error}")
    with open(arguments["output"], "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False)


main()
