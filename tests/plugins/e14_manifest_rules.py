"""The plugin.json rules of the CILogg released before E17 (E14/E15), in Python.

A port of Manifest::parse, validateActions, validateFields, validateViews and
validatePresentation from src/plugins/src/pluginmanifest.cpp (and supportedKind,
isSemanticVersion) as of master e989f899. The version-skew test (docs/plugin-sdk-advanced.md,
section 20) runs every example manifest through it, so the previous CILogg still loads them.
Keep it frozen: it describes the old host, not the current one.
"""
from __future__ import annotations

import math
import re
from typing import Any, Dict, List

MAX_MESSAGE_BYTES = 1024 * 1024
MAX_VIEWS_PER_PLUGIN = 8
MAX_VIEW_SIZE = 10000
MAX_VERSION_LENGTH = 128
FIELD_TYPES = ("text", "secret", "multiline", "number", "integer", "boolean", "choice", "file",
               "directory")
CAPABILITIES = ("datasource", "converter", "tool")
VIEW_KINDS = ("dock", "window", "dialog")
VIEW_AREAS = ("left", "right", "top", "bottom")
SUPPORTED_KINDS = ("process",)

_ID = re.compile(r"[a-z0-9][a-z0-9._-]{0,127}")
_SEMVER = re.compile(
    r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-((?:0|[1-9][0-9]*|[0-9]*[a-zA-Z-][0-9a-zA-Z-]*)"
    r"(?:\.(?:0|[1-9][0-9]*|[0-9]*[a-zA-Z-][0-9a-zA-Z-]*))*))?"
    r"(?:\+([0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?")
_DEVICE = re.compile(r"(?:CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|COM[1-9]|LPT[1-9])(?:\..*)?",
                     re.IGNORECASE)
_IMAGE_SUFFIX = re.compile(r"\.(png|ico|svg)$", re.IGNORECASE)


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def safe_relative_path(path: Any) -> bool:
    if not isinstance(path, str) or not path or path.startswith("/") or "\\" in path \
            or ":" in path or "\0" in path:
        return False
    for part in path.split("/"):
        if (not part or part in (".", "..") or part.endswith(".") or part.endswith(" ")
                or _DEVICE.fullmatch(part)):
            return False
        if any(ord(character) < 32 or character in '<>"|?*' for character in part):
            return False
    return True


def is_semantic_version(text: Any) -> bool:
    return (isinstance(text, str) and len(text) <= MAX_VERSION_LENGTH
            and _SEMVER.fullmatch(text) is not None)


def validate_fields(fields: List[Any]) -> List[str]:
    if len(fields) > 64:
        return ["Too many form fields."]
    names = set()
    for item in fields:
        spec: Dict[str, Any] = item if isinstance(item, dict) else {}
        name = spec.get("id") if isinstance(spec.get("id"), str) else ""
        if not isinstance(item, dict) or not name or name in names \
                or spec.get("type") not in FIELD_TYPES:
            return [f"Invalid or duplicate form field: {name}"]
        names.add(name)
        if (len(name) > 128 or ("required" in spec and not isinstance(spec["required"], bool))
                or ("minimum" in spec and not _is_number(spec["minimum"]))
                or ("maximum" in spec and not _is_number(spec["maximum"]))
                or ("minimum" in spec and "maximum" in spec
                    and spec["minimum"] > spec["maximum"])):
            return [f"Invalid field bounds or required flag: {name}"]
        if spec["type"] == "choice" and (not isinstance(spec.get("choices"), list)
                                         or not spec["choices"]):
            return [f"Choice field {name} requires choices."]
    return []


def validate_presentation(item: Dict[str, Any], identifier: str, allow_menu: bool) -> List[str]:
    for key in ("toolbar", "menu"):
        if key == "menu" and not allow_menu:
            continue
        if key in item and not isinstance(item[key], bool):
            return [f"Invalid {key} flag: {identifier}"]
    if "icon" in item:
        icon = item["icon"]
        if (not isinstance(icon, str) or len(icon) > 256 or not safe_relative_path(icon)
                or not _IMAGE_SUFFIX.search(icon)):
            return [f"Invalid icon (a .png, .ico or .svg path inside the plugin): {identifier}"]
    return []


def validate_actions(actions: List[Any], capabilities: List[Any]) -> List[str]:
    if len(actions) > 128:
        return ["Too many plugin actions."]
    ids = set()
    for item in actions:
        action: Dict[str, Any] = item if isinstance(item, dict) else {}
        identifier = action.get("id") if isinstance(action.get("id"), str) else ""
        if (not isinstance(item, dict) or not identifier or identifier in ids
                or len(identifier) > 128 or action.get("capability") not in capabilities
                or ("fields" in action and not isinstance(action["fields"], list))):
            return [f"Invalid action: {identifier}"]
        errors = validate_fields(action.get("fields", []))
        errors = errors or validate_presentation(action, identifier, True)
        if errors:
            return errors
        ids.add(identifier)
    return []


def validate_views(views: List[Any]) -> List[str]:
    if len(views) > MAX_VIEWS_PER_PLUGIN:
        return [f"Too many plugin views (maximum {MAX_VIEWS_PER_PLUGIN})."]

    def optional_size(value: Any) -> bool:
        return value is None or (_is_number(value) and math.floor(value) == value
                                 and 1 <= value <= MAX_VIEW_SIZE)

    ids = set()
    for item in views:
        view: Dict[str, Any] = item if isinstance(item, dict) else {}
        identifier = view.get("id") if isinstance(view.get("id"), str) else ""
        if (not isinstance(item, dict) or not identifier or identifier in ids
                or len(identifier) > 128
                or ("title" in view and (not isinstance(view["title"], str)
                                         or len(view["title"]) > 256))):
            return [f"Invalid view: {identifier}"]
        kind = view.get("kind", "dock")
        area = view.get("area", "right")
        if kind not in VIEW_KINDS or area not in VIEW_AREAS:
            return [f"Invalid view kind or area: {identifier}"]
        if (any(key in view and not isinstance(view[key], bool)
                for key in ("menu", "floating", "modal"))
                or not optional_size(view.get("width")) or not optional_size(view.get("height"))):
            return [f"Invalid view flag or size: {identifier}"]
        errors = validate_presentation(view, identifier, False)
        if errors:
            return errors
        ids.add(identifier)
    return []


def validate_manifest(manifest: Any) -> List[str]:
    """Every reason the previous CILogg would refuse this plugin.json (empty: it loads)."""
    if not isinstance(manifest, dict):
        return ["Invalid manifest identity, version, JSON or protocol version."]
    errors: List[str] = []
    identifier = manifest.get("id")
    if (not isinstance(identifier, str) or not _ID.fullmatch(identifier)
            or not safe_relative_path(identifier)
            or not isinstance(manifest.get("name"), str) or not manifest["name"]
            or not is_semantic_version(manifest.get("version"))
            or manifest.get("protocol_version") != 1 or isinstance(manifest.get("protocol_version"), bool)
            or any(key in manifest and not isinstance(manifest[key], list)
                   for key in ("arguments", "actions", "configuration", "views"))):
        errors.append("Invalid manifest identity, version, JSON or protocol version.")
    capabilities = manifest.get("capabilities")
    capabilities = capabilities if isinstance(capabilities, list) else []
    if not capabilities:
        errors.append("No plugin capabilities.")
    if any(capability not in CAPABILITIES for capability in capabilities):
        errors.append("Unknown plugin capability.")
    if manifest.get("executable") != "python" and not safe_relative_path(manifest.get("executable")):
        errors.append("Executable must be a package-relative path or python.")
    arguments = manifest.get("arguments")
    if isinstance(arguments, list) and not all(isinstance(item, str) for item in arguments):
        errors.append("Arguments must be strings.")

    def array(key: str) -> List[Any]:
        value = manifest.get(key)
        return value if isinstance(value, list) else []

    errors += validate_actions(array("actions"), capabilities)
    errors += validate_views(array("views"))
    errors += validate_fields(array("configuration"))
    if "kind" in manifest and manifest["kind"] not in SUPPORTED_KINDS:
        errors.append(f"Unsupported plugin kind: {manifest['kind']}.")
    return errors
