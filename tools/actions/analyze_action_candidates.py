#!/usr/bin/env python3
"""Report action families that may be consolidated into typed parameterized actions.

This tool is intentionally read-only. It never rewrites the source catalog.
"""

from __future__ import annotations

import argparse
import json
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


@dataclass(frozen=True)
class DecodedAction:
    action_id: int
    name: str
    command: str
    family: str
    arguments: tuple[str, ...]


def strip_json_comments(text: str) -> str:
    output: list[str] = []
    index = 0
    in_string = False
    escaped = False
    while index < len(text):
        char = text[index]
        if in_string:
            output.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            index += 1
            continue
        if char == '"':
            in_string = True
            output.append(char)
            index += 1
            continue
        if char == "/" and index + 1 < len(text) and text[index + 1] == "/":
            index += 2
            while index < len(text) and text[index] not in "\r\n":
                index += 1
            continue
        if char == "/" and index + 1 < len(text) and text[index + 1] == "*":
            index += 2
            while index + 1 < len(text) and text[index : index + 2] != "*/":
                if text[index] in "\r\n":
                    output.append(text[index])
                index += 1
            index = min(len(text), index + 2)
            continue
        output.append(char)
        index += 1
    return "".join(output)


def strip_trailing_commas(text: str) -> str:
    output: list[str] = []
    index = 0
    in_string = False
    escaped = False
    while index < len(text):
        char = text[index]
        if in_string:
            output.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            index += 1
            continue
        if char == '"':
            in_string = True
        if char == ",":
            lookahead = index + 1
            while lookahead < len(text) and text[lookahead].isspace():
                lookahead += 1
            if lookahead < len(text) and text[lookahead] in "}]":
                index += 1
                continue
        output.append(char)
        index += 1
    return "".join(output)


def load_jsonc(path: Path) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8-sig")
    root = json.loads(strip_trailing_commas(strip_json_comments(text)))
    if not isinstance(root, dict):
        raise SystemExit("Catalog root must be an object")
    return root


def decode_sequence(action: dict[str, Any]) -> str | None:
    sequence = action.get("sequence")
    if not isinstance(sequence, dict):
        return None
    value = sequence.get("value")
    if not isinstance(value, str):
        return None
    sequence_type = str(sequence.get("type", "string")).lower()
    if sequence_type == "hexstring":
        compact = re.sub(r"[\s,:;_-]", "", value)
        if len(compact) % 2 or not re.fullmatch(r"[0-9a-fA-F]*", compact):
            return None
        try:
            return bytes.fromhex(compact).decode("latin-1")
        except ValueError:
            return None
    return value


def decode_action(action: dict[str, Any]) -> DecodedAction | None:
    command = decode_sequence(action)
    if command is None:
        return None
    normalized = command.rstrip("\r\n").rstrip(";").strip()
    if ":" not in normalized:
        return None
    head, tail = normalized.split(":", 1)
    family = re.sub(r"\s+", " ", head.strip()).upper() + ":"
    arguments = tuple(part.strip() for part in tail.split(",")) if tail else ()
    return DecodedAction(
        action_id=int(action.get("id", -1)),
        name=str(action.get("name", "")),
        command=normalized,
        family=family,
        arguments=arguments,
    )


def parse_integer(value: str) -> int | None:
    if not value:
        return None
    try:
        if value.lower().startswith("0x"):
            return int(value, 16)
        if re.fullmatch(r"[0-9]+", value):
            return int(value, 10)
        if re.fullmatch(r"[0-9a-fA-F]+", value) and any(c.isalpha() for c in value):
            return int(value, 16)
    except ValueError:
        return None
    return None


def is_bitmask_candidate(values: Iterable[str]) -> bool:
    parsed = [parse_integer(value) for value in values if value]
    if len(parsed) < 3 or any(value is None for value in parsed):
        return False
    nonzero = [value for value in parsed if value]
    powers = [value for value in nonzero if value & (value - 1) == 0]
    combinations = [value for value in nonzero if value & (value - 1) != 0]
    return len(set(powers)) >= 2 and bool(combinations)


def infer_fields(actions: list[DecodedAction]) -> list[str]:
    width = max((len(action.arguments) for action in actions), default=0)
    fields: list[str] = []
    for index in range(width):
        values = sorted(
            {action.arguments[index] if index < len(action.arguments) else "" for action in actions}
        )
        nonempty = [value for value in values if value]
        optional = len(nonempty) != len(values)
        if is_bitmask_candidate(nonempty):
            control = "checkboxes + bitwise_or"
        elif 1 < len(nonempty) <= 12:
            control = "combo_box or radio_buttons"
        elif len(nonempty) == 1:
            control = "constant"
        else:
            control = "typed text/integer/hex field"
        suffix = ", optional" if optional else ""
        preview = ", ".join(f"`{markdown_escape(value)}`" for value in values[:12])
        if len(values) > 12:
            preview += f", ... ({len(values)} values)"
        fields.append(f"arg{index + 1}: {control}{suffix}; observed {preview}")
    return fields


def markdown_escape(value: str) -> str:
    escaped: list[str] = []
    for character in value:
        if character == "|":
            escaped.append("\\|")
        elif character == "`":
            escaped.append("\\`")
        elif character == "\r":
            escaped.append("\\r")
        elif character == "\n":
            escaped.append("\\n")
        elif ord(character) < 0x20 or ord(character) == 0x7F:
            escaped.append(f"\\x{ord(character):02X}")
        else:
            escaped.append(character)
    return "".join(escaped)


def build_report(catalog_path: Path, actions: list[DecodedAction]) -> str:
    families: dict[str, list[DecodedAction]] = defaultdict(list)
    names: dict[str, list[DecodedAction]] = defaultdict(list)
    for action in actions:
        families[action.family].append(action)
        names[action.name.strip().casefold()].append(action)

    candidates = [items for items in families.values() if len(items) >= 2]
    candidates.sort(key=lambda items: (-len(items), items[0].family))
    duplicate_names = [items for items in names.values() if items[0].name and len(items) >= 2]
    duplicate_names.sort(key=lambda items: (-len(items), items[0].name.casefold()))

    lines = [
        "# Advanced action candidate report",
        "",
        f"Source: `{catalog_path.as_posix()}`",
        "",
        "> Analysis only: this report does not modify or migrate the actions catalog. "
        "Every suggestion requires protocol-owner review.",
        "",
        f"Decoded colon-command actions: **{len(actions)}**",
        f"Command families with variants: **{len(candidates)}**",
        f"Duplicate action-name groups: **{len(duplicate_names)}**",
        "",
        "## Command-family candidates",
        "",
    ]
    for items in candidates:
        lines.extend(
            [
                f"### {items[0].family} ({len(items)} actions)",
                "",
                "Suggested typed fields:",
                "",
            ]
        )
        lines.extend(f"- {field}" for field in infer_fields(items))
        lines.extend(
            [
                "",
                "| ID | Name | Decoded command |",
                "|---:|---|---|",
            ]
        )
        for action in items[:30]:
            lines.append(
                f"| {action.action_id} | {markdown_escape(action.name)} | "
                f"`{markdown_escape(action.command)}` |"
            )
        if len(items) > 30:
            lines.append(f"|  |  | ... {len(items) - 30} more |")
        lines.append("")

    lines.extend(["## Duplicate-name groups", ""])
    for items in duplicate_names:
        ids = ", ".join(str(item.action_id) for item in items)
        lines.append(f"- **{markdown_escape(items[0].name)}**: IDs {ids}")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path, help="actions.json to analyze")
    parser.add_argument("--output", type=Path, required=True, help="Markdown report path")
    args = parser.parse_args()

    root = load_jsonc(args.catalog)
    raw_actions = root.get("actions", [])
    if not isinstance(raw_actions, list):
        raise SystemExit("Catalog actions member must be an array")
    decoded = [item for item in (decode_action(action) for action in raw_actions) if item]

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(build_report(args.catalog, decoded), encoding="utf-8", newline="\n")
    print(
        f"Wrote {args.output} from {len(decoded)} decoded colon-command actions; "
        "source catalog was not modified."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
