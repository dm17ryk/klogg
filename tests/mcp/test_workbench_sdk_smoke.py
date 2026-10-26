"""Exercise the real smoke client with both SDK result object shapes."""

import importlib
import json
import sys
import tempfile
import types
import unittest
from contextlib import asynccontextmanager
from pathlib import Path
from unittest.mock import patch


class SdkSmokeTests(unittest.IsolatedAsyncioTestCase):
    async def exercise(self, version, fail_grep=False, environment=None, fail_clipboard=None):
        observed = {}
        checkbox = False
        clipboard = "original"

        def result(payload, error=False):
            return types.SimpleNamespace(
                **{("is_error" if version == 2 else "isError"): error},
                content=[types.SimpleNamespace(text=json.dumps(payload))],
            )

        class Session:
            async def __aenter__(self):
                return self

            async def __aexit__(self, *args):
                pass

            async def initialize(self):
                return None

            async def list_tools(self):
                return types.SimpleNamespace(
                    tools=[types.SimpleNamespace(name=str(i)) for i in range(6)]
                )

            async def list_resources(self):
                return types.SimpleNamespace(
                    resources=[types.SimpleNamespace(uri="cilogg://commands")]
                )

            async def list_prompts(self):
                return types.SimpleNamespace(
                    prompts=[types.SimpleNamespace(name="inspect_cilogg")]
                )

            async def read_resource(self, uri):
                return None

            async def call_tool(self, name, arguments):
                nonlocal checkbox, clipboard
                if name == "cilogg_commands":
                    return result({
                        "commander": {"commands": {str(i): {} for i in range(57)}},
                        "cli": {key: {} for key in (
                            "application", "scenario", "lab", "lab-controller",
                            "lab-agent", "grep", "mcp",
                        )},
                    })
                if name == "cilogg_state":
                    return result({})
                if name == "cilogg_cli":
                    return result({"stdout": "ERROR smoke שלום 😀"}, fail_grep and arguments["mode"] == "grep")
                action = arguments["action"]
                args = arguments.get("arguments", {})
                payload = {}
                if action == "invalid_action":
                    return result({}, True)
                if action == "get_info":
                    payload = {"windows": [{"tabs": [{"tabId": "sample"}]}]}
                if action == "get_ui":
                    target = args.get("object_name")
                    if target == "@clipboard":
                        if fail_clipboard == "read" and clipboard == "MCP smoke":
                            raise RuntimeError("injected clipboard read failure")
                        payload = {"text": clipboard}
                    elif target == "dialog/checkbox":
                        payload = {"properties": {"checked": checkbox}}
                    else:
                        payload = {"objects": [
                            {"className": "QCheckBox", "objectPath": "background", "visible": True,
                             "enabled": True, "properties": {"checked": False}},
                            {"className": "OptionsDialog", "objectPath": "dialog", "visible": True},
                            {"className": "QCheckBox", "objectPath": "dialog/checkbox", "visible": True,
                             "enabled": True, "properties": {"checked": checkbox}},
                            {"className": "QPushButton", "objectPath": "dialog/cancel", "visible": True,
                             "properties": {"text": "Cancel"}},
                        ]}
                if action == "set_ui":
                    if args["object_name"] == "@clipboard":
                        clipboard = args["definition"]["text"]
                        if fail_clipboard == "write" and clipboard == "MCP smoke":
                            raise RuntimeError("injected clipboard write failure")
                    elif args["object_name"] == "background":
                        return result({"error": "A modal dialog blocks this control"}, True)
                    else:
                        checkbox = args["definition"]["checked"]
                if action == "close_tab":
                    observed["closed"] = True
                return result({"payload": payload})

        @asynccontextmanager
        async def transport(parameters, **kwargs):
            observed["parameters"] = parameters
            yield None, None

        sdk = types.ModuleType("mcp")
        sdk.ClientSession = lambda *args: Session()
        sdk.StdioServerParameters = lambda **kwargs: types.SimpleNamespace(**kwargs)
        stdio = types.ModuleType("mcp.client.stdio")
        stdio.stdio_client = transport
        with tempfile.TemporaryDirectory() as directory, patch.dict(sys.modules, {
            "mcp": sdk, "mcp.client": types.ModuleType("mcp.client"), "mcp.client.stdio": stdio,
        }):
            sys.modules.pop("smoke_stdio", None)
            smoke = importlib.import_module("smoke_stdio")
            try:
                if environment is None:
                    await smoke.run("private-cilogg", Path(directory))
                else:
                    await smoke.run("private-cilogg", Path(directory), environment=environment)
                    self.assertEqual(observed["parameters"].env, environment)
                report = json.loads((Path(directory) / "result.json").read_text())
                self.assertTrue(report["passed"])
                self.assertTrue(observed["closed"])
                self.assertEqual(clipboard, "original")
            finally:
                sys.modules.pop("smoke_stdio", None)
                self.assertEqual(clipboard, "original")

    async def test_sdk_v1_result_fields(self):
        await self.exercise(1)

    async def test_sdk_v2_result_fields(self):
        await self.exercise(2)

    async def test_sdk_v2_error_never_passes(self):
        with self.assertRaises(AssertionError):
            await self.exercise(2, fail_grep=True)

    async def test_private_environment_forwarded(self):
        await self.exercise(2, environment={"PRIVATE_MARKER": "isolated"})

    async def test_clipboard_restored_when_verification_raises(self):
        with self.assertRaisesRegex(RuntimeError, "injected clipboard read failure"):
            await self.exercise(2, fail_clipboard="read")

    async def test_clipboard_restored_when_write_raises_after_side_effect(self):
        with self.assertRaisesRegex(RuntimeError, "injected clipboard write failure"):
            await self.exercise(1, fail_clipboard="write")


if __name__ == "__main__":
    unittest.main()
