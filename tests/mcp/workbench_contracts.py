"""Small shared assertions for native MCP acceptance harnesses."""

import math


class ParityError(RuntimeError):
    """A selected parity contract failed; never a successful skip."""


def sdk_tool_failed(result):
    """The SDK renamed Python fields in v2; protocol JSON remains camelCase."""
    flag = getattr(result, "is_error", getattr(result, "isError", None))
    if type(flag) is not bool:
        raise ParityError("invalid_sdk_error_flag")
    return flag


def require_operations(catalog, operations):
    commands = catalog.get("commander", {}).get("commands", {})
    missing = sorted(set(operations) - set(commands))
    if missing:
        raise ParityError("missing_operation: " + ", ".join(missing))


def compare_json(expected, actual, path="$"):
    """Compare JSON types and exact values, preserving decimal strings and integers."""
    if type(expected) is not type(actual):
        raise ParityError(f"result_mismatch at {path}: JSON type differs")
    if isinstance(expected, dict):
        if expected.keys() != actual.keys():
            raise ParityError(f"result_mismatch at {path}: object keys differ")
        for key in sorted(expected):
            compare_json(expected[key], actual[key], f"{path}.{key}")
    elif isinstance(expected, list):
        if len(expected) != len(actual):
            raise ParityError(f"result_mismatch at {path}: array lengths differ")
        for index, (left, right) in enumerate(zip(expected, actual)):
            compare_json(left, right, f"{path}[{index}]")
    elif (
        isinstance(expected, float) and not math.isfinite(expected)
    ) or expected != actual:
        # Describe the location, not a potentially private source/capture body.
        raise ParityError(f"result_mismatch at {path}: values differ")
