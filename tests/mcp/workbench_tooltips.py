"""E17a parity: advanced tooltips of the ui-gallery example through every surface.

The private host gets the ui-gallery example (docs/plugin-sdk-advanced.md). The GUI controls,
the CLI, native MCP and a Python global script read the same tooltips through
get_plugin_tooltip, which runs the same query path as a hover.
"""

import logging
import shutil
import time
from pathlib import Path

from workbench_contracts import ParityError

LOG = logging.getLogger("workbench.tooltips")
ROOT = Path(__file__).resolve().parents[2]
PLUGIN = "org.cilogg.ui-gallery"
VIEW = "gallery"


def install_ui_gallery(payload: Path) -> None:
    """Copy the ui-gallery package (and its SDK) into the private runtime's plugin folder."""
    target = payload / "plugins" / "ui-gallery"
    shutil.copytree(ROOT / "plugins/examples/ui-gallery", target,
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


def element_name(element):
    return f"pluginView_{PLUGIN}_{VIEW}_{element}"


def cli_tooltip(cli, element, row=None, column=None, cache=False):
    arguments = ["--plugin-id", PLUGIN, "--view-id", VIEW, "--element-id", element]
    if row:
        arguments += ["--row-id", row, "--column-id", column]
    if not cache:
        arguments.append("--no-cache")
    return cli("get_plugin_tooltip", *arguments)


def mcp_tooltip(command, element, row=None, column=None):
    request = {"plugin_id": PLUGIN, "view_id": VIEW, "element_id": element, "no_cache": True}
    if row:
        request.update(row_id=row, column_id=column)
    return command("get_plugin_tooltip", request)


def exercise_tooltips(cli, command, catalog, artifacts, run_python):
    del catalog, artifacts  # the operations were checked by the runner
    cli("plugin_manage", "--plugin-operation", "enable", "--plugin-id", PLUGIN)
    gallery = next((p for p in command("get_plugins")["plugins"] if p["id"] == PLUGIN), None)
    if not gallery or "tooltip/resolve" not in gallery.get("handles", []):
        raise ParityError("ui_gallery_manifest_missing")
    cli("open_plugin_view", "--plugin-id", PLUGIN, "--view-id", VIEW)

    # GUI: the view renders with its accessible names and static tooltips as descriptions.
    summary = wait_until(lambda: (lambda item: item if item and item.get("enabled") else None)(
        ui_object(command, element_name("summary"))), 40, "gallery_not_rendered")
    if summary.get("accessibleName") != "Fleet summary":
        raise ParityError(f"accessible_name: {summary.get('accessibleName')}")
    name_field = ui_object(command, element_name("board_name"))
    if not name_field or name_field.get("accessibleName") != "Board name":
        raise ParityError("field_caption_not_accessible_name")

    # The plugin answers the same tooltip on every surface.
    surfaces = {
        "cli": cli_tooltip(cli, "summary"),
        "mcp": mcp_tooltip(command, "summary"),
    }
    python = run_python([("e17_tooltips", {"plugin": PLUGIN, "view": VIEW})])[0]
    if python.get("errors"):
        raise ParityError(f"python_fixture_failed: {python['errors']}")
    surfaces["python"] = python["summary"]
    for name, result in surfaces.items():
        if result.get("source") != "plugin":
            raise ParityError(f"tooltip_source: {name} {result.get('source')}")
    tooltips = {name: result["tooltip"] for name, result in surfaces.items()}
    if len({repr(sorted(tooltip.items())) for tooltip in tooltips.values()}) != 1:
        raise ParityError(f"tooltips_differ: {tooltips}")
    if tooltips["cli"].get("title") != "Fleet summary" or "Data revision 0" != tooltips["cli"].get(
            "footer"):
        raise ParityError(f"summary_tooltip: {tooltips['cli']}")

    # Table cells ask per cell; a cached answer is reported as such.
    cells = {"cli": cli_tooltip(cli, "boards", "b2", "state"),
             "mcp": mcp_tooltip(command, "boards", "b2", "state"),
             "python": python["cell"]}
    for name, result in cells.items():
        if result.get("tooltip", {}).get("title") != "Idle":
            raise ParityError(f"cell_tooltip: {name} {result}")
    if cli_tooltip(cli, "summary", cache=True).get("source") != "cache":
        raise ParityError("summary_not_cached")

    # GUI: Change data invalidates the summary; the next read asks the plugin again.
    command("activate_ui", {"window_index": 0, "object_name": element_name("change")})
    changed = wait_until(lambda: (lambda result: result if result.get("source") == "plugin"
                                  else None)(cli_tooltip(cli, "summary", cache=True)),
                         20, "summary_not_invalidated")
    if changed["tooltip"].get("footer") != "Data revision 1":
        raise ParityError(f"summary_after_change: {changed}")
    LOG.info("tooltips_completed surfaces=%s", sorted(surfaces))
    return {"id": "e17_tooltips", "passed": True, "summary": tooltips["cli"],
            "cell": cells["cli"]["tooltip"]}
