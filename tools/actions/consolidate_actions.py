"""Consolidate firmware command variants into typed action definitions.

The source catalog is JSONC, while the generated catalog is deliberately plain
JSON so it can be validated directly by the installed JSON schema.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

from analyze_action_candidates import load_jsonc


def choice(label: str, value: object) -> dict[str, object]:
    return {"label": label, "value": value}


def field(
    name: str,
    label: str,
    parameter_type: str,
    *,
    presentation: str = "auto",
    required: bool = False,
    default: object | None = None,
    choices: list[dict[str, object]] | None = None,
    multi_value_mode: str | None = None,
    expression: str | None = None,
) -> dict[str, object]:
    result: dict[str, object] = {
        "name": name,
        "label": label,
        "type": parameter_type,
        "presentation": presentation,
        "required": required,
    }
    if default is not None:
        result["default"] = default
    if choices is not None:
        result["choices"] = choices
    if multi_value_mode is not None:
        result["multi_value_mode"] = multi_value_mode
    if expression is not None:
        result["expression"] = expression
    return result


def typed_action(
    action: dict[str, object],
    *,
    name: str,
    description: str,
    sequence: str,
    fields: list[dict[str, object]],
    expression: str | None = None,
) -> None:
    action["name"] = name
    action["description"] = description
    action["sequence"] = {"type": "string", "value": sequence}
    parameters = dict(action.get("parameters", {}))
    parameters["fields"] = fields
    action["parameters"] = parameters
    if expression is not None:
        action["expression"] = expression
    else:
        action.pop("expression", None)


def consolidate(path: Path) -> None:
    document = load_jsonc(path)
    actions = {int(action["id"]): action for action in document["actions"]}

    debug_levels = [choice(label, label) for label in ("E", "W", "I", "D", "V", "0")]
    typed_action(
        actions[68],
        name="ESP SET DEBUG LEVEL",
        description="Set the ESP debug level for a tag. Use * to apply to every tag.",
        sequence="ESP SET DEBUG LEVEL:${tag},${level};\r\n",
        fields=[
            field(
                "tag",
                "Tag",
                "text",
                required=True,
                default="ADC_ReadChannel",
            ),
            field(
                "level",
                "Level",
                "choice",
                presentation="combo_box",
                required=True,
                default="0",
                choices=debug_levels,
            ),
        ],
    )
    for action_id in range(69, 78):
        actions[action_id]["hidden"] = True

    interfaces = [
        choice("GSM (B0)", 0x01),
        choice("PSTN (B1)", 0x02),
        choice("Ethernet (B2)", 0x04),
        choice("BLE (B3)", 0x08),
    ]
    typed_action(
        actions[88],
        name="EEPROM WRITE: ComboCommInterface",
        description="Write the ComboCommInterface bit mask at EEPROM address FF0B.",
        sequence="EEPROM WRITE:FF0B,${interfaces};\r\n",
        fields=[
            field(
                "interfaces",
                "Communication interfaces",
                "multi_choice",
                presentation="check_boxes",
                multi_value_mode="bitwise_or",
                default=[],
                choices=interfaces,
                expression="hex(value, 2)",
            )
        ],
    )
    actions[664]["hidden"] = True

    networks = [
        choice("Default", 0),
        choice("2G only", 12),
        choice("3G only", 22),
        choice("4G/3G/2G", 25),
        choice("4G only", 28),
        choice("3G/2G only", 29),
        choice("4G preferred and 2G", 30),
        choice("4G preferred, 3G and 2G", 31),
        choice("5G/4G only", 37),
    ]
    typed_action(
        actions[459],
        name="SELECT CELLULAR DATA NETWORKS",
        description="Select the cellular data network preference.",
        sequence="SELECT CELLULAR DATA NETWORKS:${network};\r\n",
        fields=[
            field(
                "network",
                "Network preference",
                "choice",
                presentation="combo_box",
                required=True,
                default=31,
                choices=networks,
            )
        ],
    )
    for action_id in range(460, 467):
        actions[action_id]["hidden"] = True

    # The command accepts both the empty form and an explicit mode. The choice
    # is therefore optional, matching the firmware parser's two valid forms.
    if not any(int(action["id"]) == 665 for action in document["actions"]):
        document["actions"].append(
            {
                "id": 665,
                "enabled": True,
                "hidden": False,
                "name": "SCHED STATS",
                "description": "Request scheduler statistics, optionally for a specific mode.",
                "sequence": {"type": "string", "value": "SCHED STATS:;\r\n"},
                "expression": 'concat("SCHED STATS:", mode == null ? "" : str(mode), ";\\r\\n")',
                "parameters": {
                    "repeat": False,
                    "delay": 30,
                    "fields": [
                        field(
                            "mode",
                            "Mode",
                            "choice",
                            presentation="combo_box",
                            choices=[
                                choice("Queued", 0),
                                choice("Active", 1),
                                choice("Active + reset", 2),
                                choice("Full", 3),
                            ],
                        )
                    ],
                },
            }
        )

    response_parameters = {
        5: {"tag": "*", "level": "I"},
        6: {"tag": "*", "level": "W"},
        13: {"tag": "*", "level": "0"},
        19: {"tag": "*", "level": "0"},
    }
    for response in document["responses"]:
        response_id = int(response["id"])
        if response_id not in response_parameters:
            continue
        response_data = response.setdefault("response", {})
        response_data["action_id"] = 68
        response_data["steps"] = [
            {"action_id": 68, "delay_ms": 0, "parameters": response_parameters[response_id]}
        ]

    document["version"] = 4
    path.write_text(json.dumps(document, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    consolidate(Path(sys.argv[1] if len(sys.argv) > 1 else "config/actions.json"))
