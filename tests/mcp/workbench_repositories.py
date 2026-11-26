"""E15a parity: plugin repositories through the GUI, the CLI, native MCP and Python.

The private host starts with the migrated `default` repository. Each surface adds, moves,
updates and fetches repositories through `plugin_manage`; every surface must then report the
same ordered list and the same per-repository catalog results. The catalog addresses point at a
closed local port, so no case depends on the network and every fetch fails the same way
(docs/plugin-repositories.md sections 3.3 and 3.8).
"""

import json
import logging
import time
from pathlib import Path

from workbench_contracts import ParityError, compare_json

LOG = logging.getLogger("workbench.repositories")
TERMINAL = ("completed", "failed", "cancelled")
# A closed port: HTTPS requests fail at once with "connection refused".
CLOSED = "https://127.0.0.1:9"
FIELDS = ("position", "id", "name", "url", "signIn", "enabled", "autoUpdate")
GUI_CONTROLS = ("pluginsAndPacksTabs", "pluginRepositoriesTable", "pluginRepositoryAdd",
                "pluginRepositoryEdit", "pluginRepositoryRemove", "pluginRepositoryUp",
                "pluginRepositoryDown", "pluginRepositoriesHowTo")


def wait_until(predicate, timeout, reason):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise ParityError(reason)


def ui_object(command, name):
    return next((item for item in command("get_ui", {"window_index": 0})["objects"]
                 if item.get("objectName") == name), None)


def listed(repositories):
    """The fields every surface must agree on (lastCheck and lastError hold times and texts)."""
    return [{key: repository.get(key) for key in FIELDS} for repository in repositories]


def catalog_states(job):
    return [{"repository": item["repository"], "state": item["state"], "entries": item["entries"]}
            for item in job.get("result", {}).get("repositories", [])]


def update_check(job):
    """What every surface must agree on for a check_updates job (E15c)."""
    result = job.get("result", {})
    return {"state": job.get("state"), "updates": result.get("updates"),
            "checked": result.get("checked")}


# E16a1: the public example configuration pack, installed from its folder.
PACK_ID = "org.cilogg.example.protocol-pack"
PACK_FOLDER = Path(__file__).resolve().parents[2] / "plugins/examples/protocol-pack"
PACK_FIELDS = ("id", "kind", "version", "enabled", "error", "contents", "previews", "filters",
               "packActions", "packResponses", "luaFiles", "pythonDecoders")
# E16a2: the example pack's preview in the store's canonical form ("Demo frame", encoded).
PACK_PREVIEW = "previews/%44emo%20frame.json"
PACK_EDITED = [{"path": PACK_PREVIEW, "kind": "preview", "item": "Demo frame", "state": "edited"}]


def pack_fields(plugins, pack_id=PACK_ID):
    """The get_plugins fields of a configuration pack that every surface must agree on."""
    for plugin in plugins:
        if plugin.get("id") == pack_id:
            return {key: plugin.get(key) for key in PACK_FIELDS}
    return None


class Surfaces:
    def __init__(self, cli, command, artifacts):
        self.cli = cli
        self.command = command
        self.artifacts = artifacts
        self.files = 0

    def manage_cli(self, operation, options=None):
        arguments = ["plugin_manage", "--plugin-operation", operation]
        if options is not None:
            self.files += 1
            path = self.artifacts / f"e15a-{self.files}-{operation}.json"
            path.write_text(json.dumps(options), encoding="utf-8")
            arguments += ["--json-file", str(path)]
        return self.cli(*arguments)

    def manage_mcp(self, operation, options=None):
        request = {"plugin_operation": operation}
        if options is not None:
            request["definition"] = options
        return self.command("plugin_manage", request)

    def pack_cli(self, operation, options=None):
        arguments = ["plugin_manage", "--plugin-operation", operation, "--plugin-id", PACK_ID]
        if options is not None:
            self.files += 1
            path = self.artifacts / f"e16a2-{self.files}-{operation}.json"
            path.write_text(json.dumps(options), encoding="utf-8")
            arguments += ["--json-file", str(path)]
        return self.cli(*arguments)

    def pack_mcp(self, operation, options=None):
        request = {"plugin_operation": operation, "plugin_id": PACK_ID}
        if options is not None:
            request["definition"] = options
        return self.command("plugin_manage", request)

    def repositories(self):
        cli = self.manage_cli("repositories")["repositories"]
        mcp = self.manage_mcp("repositories")["repositories"]
        compare_json(listed(cli), listed(mcp), "$.repositories.mcp")
        return cli

    def finished(self, job_id, through_mcp):
        def done():
            job = (self.command("get_plugin_job", {"plugin_job_id": job_id}) if through_mcp
                   else self.cli("get_plugin_job", "--plugin-job-id", job_id))
            return job if job["state"] in TERMINAL else None
        return wait_until(done, 60, f"catalog_job_timeout: {job_id}")


def gui_rows(command):
    table = wait_until(lambda: ui_object(command, "pluginRepositoriesTable"), 20,
                       "gui_repositories_table_missing")
    return [{"position": int(row[0]), "name": row[2], "url": row[3],
             "enabled": checks[1] == 2}
            for row, checks in zip(table["items"], table["checkStates"])]


def expect_gui(command, repositories, where):
    expected = [{"position": item["position"], "name": item["name"], "url": item["url"],
                 "enabled": item["enabled"]} for item in repositories]
    wait_until(lambda: gui_rows(command) == expected, 20, f"gui_table_differs: {where}")


def row_of(command, name):
    for index, row in enumerate(gui_rows(command)):
        if row["name"] == name:
            return index
    raise ParityError(f"gui_row_missing: {name}")


def exercise_repositories(cli, command, catalog, artifacts, run_python):
    """e15a_repositories: one repository list and one catalog result on every surface."""
    if "plugin_manage" not in catalog.get("commander", {}).get("commands", {}):
        raise ParityError("plugin_manage_missing")
    surfaces = Surfaces(cli, command, artifacts)
    start = surfaces.repositories()
    compare_json(["default"], [item["id"] for item in start], "$.start")
    # The default repository is on the internet: switch it off for the whole case.
    surfaces.manage_cli("repository_update", {"repository": "default", "enabled": False})
    added = surfaces.manage_cli("repository_add", {"name": "Parity CLI",
                                                   "url": f"{CLOSED}/cli/plugins.json"})
    compare_json("parity-cli", added["repository"]["id"], "$.add.cli")
    added = surfaces.manage_mcp("repository_add", {"name": "Parity MCP",
                                                   "url": f"{CLOSED}/mcp/plugins.json"})
    compare_json("parity-mcp", added["repository"]["id"], "$.add.mcp")
    refused = cli("plugin_manage", "--plugin-operation", "repository_update", success=False)
    LOG.info("repository_update_without_options refused=%s", bool(refused))
    moved = surfaces.manage_mcp("repository_move", {"repository": "parity-mcp", "position": 1})
    compare_json(["parity-mcp", "default", "parity-cli"],
                 [item["id"] for item in moved["repositories"]], "$.move.mcp")
    moved = surfaces.manage_cli("repository_move", {"repository": "parity-cli", "direction": "up"})
    compare_json(["parity-mcp", "parity-cli", "default"],
                 [item["id"] for item in moved["repositories"]], "$.move.cli")

    # GUI: the Repositories tab shows the same list; Move Up and Enabled change it.
    report = {"gui": []}
    command("activate_ui", {"window_index": 0, "object_name": "managePluginsAction"})
    command("set_ui", {"window_index": 0, "object_name": "pluginsAndPacksTabs",
                       "definition": {"currentIndex": 2}})
    for name in GUI_CONTROLS:
        found = wait_until(lambda name=name: ui_object(command, name), 20,
                           f"gui_control_missing: {name}")
        if not found.get("accessibleName"):
            raise ParityError(f"gui_control_unnamed: {name}")
        report["gui"].append({"objectName": name, "accessibleName": found["accessibleName"]})
    expect_gui(command, surfaces.repositories(), "after cli and mcp")
    command("set_ui", {"window_index": 0, "object_name": "pluginRepositoriesTable",
                       "definition": {"indexPath": [row_of(command, "Parity CLI")]}})
    command("activate_ui", {"window_index": 0, "object_name": "pluginRepositoryUp"})
    wait_until(lambda: [item["id"] for item in surfaces.repositories()]
               == ["parity-cli", "parity-mcp", "default"], 20, "gui_move_up_not_applied")
    command("set_ui", {"window_index": 0, "object_name": "pluginRepositoriesTable",
                       "definition": {"indexPath": [row_of(command, "Parity MCP")], "column": 1,
                                      "checkState": 0}})
    wait_until(lambda: not next(item for item in surfaces.repositories()
                                if item["id"] == "parity-mcp")["enabled"], 20,
               "gui_enabled_not_applied")
    expect_gui(command, surfaces.repositories(), "after gui")
    surfaces.manage_mcp("repository_update", {"repository": "parity-mcp", "enabled": True})

    # The merged catalog: every enabled repository fails the same way on every surface.
    jobs = {}
    jobs["cli"] = surfaces.finished(surfaces.manage_cli("catalog")["jobId"], False)
    jobs["mcp"] = surfaces.finished(surfaces.manage_mcp("catalog")["jobId"], True)
    expected_states = [{"repository": "parity-cli", "state": "failed", "entries": 0},
                       {"repository": "parity-mcp", "state": "failed", "entries": 0}]
    for name, job in jobs.items():
        compare_json("failed", job["state"], f"$.catalog.{name}.state")
        compare_json(expected_states, catalog_states(job), f"$.catalog.{name}.repositories")
    # One repository by its ID.
    single = surfaces.finished(surfaces.manage_cli("catalog", {"repository": "parity-mcp"})["jobId"],
                               False)
    compare_json([expected_states[1]], catalog_states(single), "$.catalog.single")
    # The GUI fetch reports the same repositories and says so in words.
    command("set_ui", {"window_index": 0, "object_name": "pluginsAndPacksTabs",
                       "definition": {"currentIndex": 1}})
    command("activate_ui", {"window_index": 0, "object_name": "fetchPluginCatalog"})
    status = wait_until(lambda: (lambda item: item if item and "could be loaded"
                                 in str(item.get("properties", {}).get("text", "")) else None)(
        ui_object(command, "pluginCatalogStatus")), 60, "gui_fetch_status_missing")
    if "completed" in str(status.get("properties", {}).get("text", "")):
        raise ParityError("gui_status_shows_job_state")
    available = wait_until(lambda: ui_object(command, "pluginCatalogTable"), 20,
                           "gui_available_table_missing")
    headers = [row[0] for row in available["items"]]
    if len(headers) != 2 or not all("Failed:" in header for header in headers):
        raise ParityError(f"gui_available_groups: {headers}")
    report["gui"].append({"objectName": "pluginCatalogTable", "groups": headers})

    # E15c: check_updates answers the same on every surface. Every repository fails, so the job
    # fails, lists no update and names the repositories it checked.
    expected_check = {"state": "failed", "updates": [], "checked": ["parity-cli", "parity-mcp"]}
    for name, through_mcp in (("cli", False), ("mcp", True)):
        manage = surfaces.manage_mcp if through_mcp else surfaces.manage_cli
        job = surfaces.finished(manage("check_updates")["jobId"], through_mcp)
        compare_json(expected_check, update_check(job), f"$.check_updates.{name}")
    # The Installed tab's Check for Updates reports the same failure in words.
    command("set_ui", {"window_index": 0, "object_name": "pluginsAndPacksTabs",
                       "definition": {"currentIndex": 0}})
    command("activate_ui", {"window_index": 0, "object_name": "pluginCheckUpdates"})
    wait_until(lambda: "Could not check for updates" in str(
        (ui_object(command, "pluginInstalledStatus") or {}).get("properties", {}).get("text", "")),
        60, "gui_check_updates_status_missing")
    report["gui"].append({"objectName": "pluginCheckUpdates", "state": "failed"})

    # E16a1: a configuration pack installs like a plugin and reports the same pack fields
    # everywhere; it has no process, so Restart is refused on every surface.
    installed = surfaces.finished(
        surfaces.manage_cli("install", {"path": str(PACK_FOLDER)})["jobId"], False)
    compare_json("completed", installed["state"], "$.pack.install")
    command("plugin_manage", {"plugin_operation": "enable", "plugin_id": PACK_ID})
    expected_pack = {"id": PACK_ID, "kind": "config-pack", "version": "1.0.0", "enabled": True,
                     "error": "", "contents": ["previews", "actions", "filters", "lua"],
                     "previews": 1, "filters": 2, "packActions": 1, "packResponses": 1,
                     "luaFiles": 2, "pythonDecoders": 0}
    compare_json(expected_pack, pack_fields(cli("get_plugins")["plugins"]), "$.pack.cli")
    compare_json(expected_pack, pack_fields(command("get_plugins", {})["plugins"]), "$.pack.mcp")
    for name, refused in (
            ("cli", cli("plugin_manage", "--plugin-operation", "restart", "--plugin-id", PACK_ID,
                        success=False)),
            ("mcp", command("plugin_manage", {"plugin_operation": "restart", "plugin_id": PACK_ID},
                            success=False))):
        if not refused:
            raise ParityError(f"pack_restart_not_refused: {name}")
    command("set_ui", {"window_index": 0, "object_name": "pluginsAndPacksTabs",
                       "definition": {"currentIndex": 0}})
    installed_table = wait_until(
        lambda: (lambda table: table if table and any(
            row[0] == "Example protocol pack" for row in table.get("items", [])) else None)(
            ui_object(command, "pluginManagerTable")), 20, "gui_pack_row_missing")
    pack_row = next(row for row in installed_table["items"] if row[0] == "Example protocol pack")
    # Previews and filters (E16c): more than one kind of content.
    compare_json("Configuration pack", pack_row[1], "$.pack.gui.kind")
    report["gui"].append({"objectName": "pluginManagerTable", "pack": pack_row[1]})

    # E16a2: pack items are edited in place through the same operations on every surface, and
    # the Installed tab shows the edit.
    files = surfaces.pack_cli("pack_files")["files"]
    compare_json(files, surfaces.pack_mcp("pack_files")["files"], "$.pack.files.mcp")
    if not any(item["path"] == PACK_PREVIEW and item["state"] == "pack" for item in files):
        raise ParityError(f"pack_preview_not_listed: {PACK_PREVIEW}")
    item = json.loads(surfaces.pack_mcp("pack_read", {"path": PACK_PREVIEW})["content"])
    item["regex"] = item["regex"].replace("^DEMO ", "^DEMO2 ")
    surfaces.pack_cli("pack_edit", {"path": PACK_PREVIEW, "content": json.dumps(item, indent=2)})
    compare_json(PACK_EDITED, surfaces.pack_mcp("pack_edits")["files"], "$.pack.edits.mcp")
    compare_json(PACK_EDITED, surfaces.pack_cli("pack_edits")["files"], "$.pack.edits.cli")
    history = surfaces.pack_mcp("pack_history", {"path": PACK_PREVIEW})["commits"]
    compare_json(["Edit preview 'Demo frame'", "Install 1.0.0 from a local package"],
                 [commit["message"] for commit in history], "$.pack.history")
    compare_json([False, True], [commit["packVersion"] for commit in history],
                 "$.pack.history.packVersion")
    # An option an operation does not take is refused, not ignored, on every surface.
    unknown = {"path": PACK_PREVIEW, "version": "upstream"}
    unknown_file = artifacts / "e16a2-unknown-option.json"
    unknown_file.write_text(json.dumps(unknown), encoding="utf-8")
    for name, refused in (
            ("cli", cli("plugin_manage", "--plugin-operation", "pack_read", "--plugin-id", PACK_ID,
                        "--json-file", str(unknown_file), success=False)),
            ("mcp", command("plugin_manage", {"plugin_operation": "pack_read", "plugin_id": PACK_ID,
                                              "definition": unknown}, success=False))):
        if not refused:
            raise ParityError(f"pack_unknown_option_not_refused: {name}")
    wait_until(lambda: any(row[0] == "Example protocol pack" and "Edited locally (1)" in row[4]
                           for row in (ui_object(command, "pluginManagerTable") or {}).get(
                               "items", [])), 20, "gui_pack_edit_state_missing")
    pack_files_button = wait_until(lambda: ui_object(command, "pluginManage_packFiles"), 20,
                                   "gui_pack_files_button_missing")
    if not pack_files_button.get("accessibleName"):
        raise ParityError("gui_control_unnamed: pluginManage_packFiles")
    report["gui"].append({"objectName": "pluginManage_packFiles",
                          "accessibleName": pack_files_button["accessibleName"]})

    # Python: klogg.app.command reaches the same operations.
    python = run_python([("e15a_repositories", {"closed": CLOSED})])[0]
    if python.get("errors"):
        raise ParityError(f"python_fixture_failed: {python['errors']}")
    final = surfaces.repositories()
    compare_json(listed(final), listed(python["repositories"]), "$.python.repositories")
    compare_json(["parity-python", "parity-cli", "parity-mcp", "default"],
                 [item["id"] for item in final], "$.python.order")
    compare_json([{"repository": "parity-python", "state": "failed", "entries": 0}],
                 python["catalog"], "$.python.catalog")
    compare_json("failed", python["catalogState"], "$.python.catalogState")
    compare_json(True, python["refusedHttp"], "$.python.refusedHttp")
    compare_json({"state": "failed", "updates": [], "checked": ["parity-python"]},
                 python["checkUpdates"], "$.python.checkUpdates")
    compare_json(expected_pack, pack_fields(python["plugins"]), "$.python.pack")
    compare_json(True, python["packRestartRefused"], "$.python.packRestartRefused")
    # E16a2: Python sees the CLI's edit and resets the pack to its version as shipped.
    compare_json(PACK_EDITED, python["packEdits"], "$.python.packEdits")
    compare_json([], python["packEditsAfterReset"], "$.python.packEditsAfterReset")
    compare_json([], surfaces.pack_cli("pack_edits")["files"], "$.pack.edits.afterReset")
    cli("plugin_manage", "--plugin-operation", "disable", "--plugin-id", PACK_ID)
    cli("plugin_manage", "--plugin-operation", "remove", "--plugin-id", PACK_ID)
    if pack_fields(cli("get_plugins")["plugins"]) is not None:
        raise ParityError("pack_not_removed")
    # Clean up through the CLI; removed IDs are never given again.
    for repository in ("parity-python", "parity-cli", "parity-mcp"):
        surfaces.manage_cli("repository_remove", {"repository": repository})
    again = surfaces.manage_mcp("repository_add", {"name": "Parity CLI",
                                                   "url": f"{CLOSED}/again/plugins.json"})
    compare_json("parity-cli-2", again["repository"]["id"], "$.reused")
    surfaces.manage_cli("repository_remove", {"repository": "parity-cli-2"})
    surfaces.manage_cli("repository_update", {"repository": "default", "enabled": True})
    compare_json(listed(start), listed(surfaces.repositories()), "$.restored")
    LOG.info("e15a_repositories_completed gui=%d", len(report["gui"]))
    return {"id": "e15a_repositories", "passed": True, "guiControls": report["gui"]}
