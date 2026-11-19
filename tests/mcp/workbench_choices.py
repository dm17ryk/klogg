"""E17c parity: the ui-gallery choices, check boxes, radio buttons, field kinds and validation
through every surface.

The private host gets the ui-gallery example (docs/plugin-sdk-advanced.md section 8). The GUI
renders the controls; the CLI, native MCP and a Python global script change them through set_ui
and activate_ui, as a user would, and read through get_ui what the plugin answered (the gallery's
status label) and what the host shows (the validation message, Apply following
`requires_valid`).
"""

import json
import logging
import time

from workbench_contracts import ParityError

LOG = logging.getLogger("workbench.choices")
PLUGIN = "org.cilogg.ui-gallery"
VIEW = "gallery"
# A host runs one global script per run: the E17a tooltips case runs the choices fixture in its
# script and leaves the result here.
PYTHON_RESULT = None


def wait_until(predicate, timeout, reason):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise ParityError(reason)


def element_name(element):
    return f"pluginView_{PLUGIN}_{VIEW}_{element}"


def exercise_choices(cli, command, catalog, artifacts, run_python):
    del catalog  # the operations were checked by the runner
    cli("plugin_manage", "--plugin-operation", "enable", "--plugin-id", PLUGIN)
    gallery = next((p for p in command("get_plugins")["plugins"] if p["id"] == PLUGIN), None)
    if not gallery or "field/complete" not in gallery.get("handles", []):
        raise ParityError("ui_gallery_manifest_missing")
    cli("open_plugin_view", "--plugin-id", PLUGIN, "--view-id", VIEW)

    def objects():
        return {item.get("objectName"): item
                for item in command("get_ui", {"window_index": 0})["objects"]}

    def prop(found, element, name):
        return found.get(element_name(element), {}).get("properties", {}).get(name)

    def answered(text, apply_enabled=None):
        """Waits until the plugin shows `text` and Apply is enabled as expected."""
        def check():
            found = objects()
            if prop(found, "choices_status", "text") != text:
                return None
            if apply_enabled is not None and \
                    found.get(element_name("apply"), {}).get("enabled") != apply_enabled:
                return None
            return found
        return wait_until(check, 20, f"no_answer: {text}")

    found = wait_until(lambda: (lambda items: items if element_name("apply") in items else None)(
        objects()), 40, "choices_not_rendered")

    # Python moved the Log level slider in the tooltips case's script; the GUI shows the value.
    python = PYTHON_RESULT or run_python([("e17_choices", {"plugin": PLUGIN, "view": VIEW})])[0]
    if python.get("errors") or python.get("status") != "changed level=4 valid=true":
        raise ParityError(f"python_fixture_failed: {python}")
    if prop(found, "level", "value") != 4:
        raise ParityError(f"slider_value: {prop(found, 'level', 'value')}")

    # GUI: the declared state. The board label is required and empty, so Apply is disabled; a
    # secret field never shows its text.
    if prop(found, "mode_choice_0", "checked") is not True or \
            prop(found, "logs_choice_0", "checked") is not True or \
            prop(found, "logs_choice_1", "checked") is not False:
        raise ParityError("toggle_state")
    if found.get(element_name("apply"), {}).get("enabled") is not False:
        raise ParityError("apply_enabled_while_invalid")
    if prop(found, "token", "text") != "<redacted>":
        raise ParityError("secret_disclosed")

    # CLI: a valid label enables Apply.
    request = artifacts / "e17c-board-label.json"
    request.write_text(json.dumps({"text": "Board_7"}), encoding="utf-8")
    cli("set_ui", "--object-name", element_name("board_label"), "--json-file", str(request))
    answered("changed board_label=Board_7 valid=true", apply_enabled=True)

    # Native MCP: an invalid label shows the host's message and disables Apply again.
    def mcp_set(element, definition):
        command("set_ui", {"window_index": 0, "object_name": element_name(element),
                           "definition": definition})

    mcp_set("board_label", {"text": "bad label!"})
    invalid = answered("changed board_label=bad label! valid=false", apply_enabled=False)
    if prop(invalid, "board_label_error", "text") != "Not in the expected form":
        raise ParityError(f"validation_message: {prop(invalid, 'board_label_error', 'text')}")
    mcp_set("board_label", {"text": "Board_8"})
    answered("changed board_label=Board_8 valid=true", apply_enabled=True)
    mcp_set("retries", {"value": 5})
    answered("changed retries=5 valid=true")

    # GUI and CLI clicks: a radio button, the All link of a check group, then Apply with the
    # values the other surfaces set.
    command("activate_ui", {"window_index": 0, "object_name": element_name("mode_choice_1")})
    answered("changed mode=manual valid=true")
    cli("activate_ui", "--object-name", element_name("logs_all"))
    answered('changed logs=["boot", "radio", "modem", "power"] valid=true')
    command("activate_ui", {"window_index": 0, "object_name": element_name("apply")})
    answered("clicked apply board_label=Board_8 retries=5")
    LOG.info("choices_completed")
    return {"id": "e17_choices", "passed": True, "python": python["status"]}
