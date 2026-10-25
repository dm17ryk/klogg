"""Opt-in protocol/GUI integration test against an isolated, already running CILogg."""

from __future__ import annotations

import argparse
import asyncio
import json
import time
from pathlib import Path

from workbench_contracts import sdk_tool_failed


async def run(executable: str, artifacts: Path, environment=None) -> None:
    from mcp import ClientSession, StdioServerParameters
    from mcp.client.stdio import stdio_client

    artifacts.mkdir(parents=True, exist_ok=True)
    parameters = StdioServerParameters(
        command=executable,
        args=["mcp", "serve"],
        env=environment,
    )
    with (artifacts / "server-stderr.log").open("w", encoding="utf-8") as diagnostics:
        async with (
            stdio_client(parameters, errlog=diagnostics) as (reader, writer),
            ClientSession(reader, writer) as session,
        ):
            await session.initialize()
            tools = await session.list_tools()
            assert len(tools.tools) == 6
            assert (
                str((await session.list_resources()).resources[0].uri)
                == "cilogg://commands"
            )
            assert (await session.list_prompts()).prompts[0].name == "inspect_cilogg"
            await session.read_resource("cilogg://commands")
            catalog = await session.call_tool("cilogg_commands", {})
            assert not sdk_tool_failed(catalog)
            discovered = json.loads(catalog.content[0].text)
            assert len(discovered["commander"]["commands"]) >= 57
            assert set(discovered["cli"]) == {
                "application",
                "scenario",
                "lab",
                "lab-controller",
                "lab-agent",
                "grep",
                "mcp",
            }

            async def command(action: str, arguments: dict | None = None) -> dict:
                result = await session.call_tool(
                    "cilogg_execute", {"action": action, "arguments": arguments or {}}
                )
                assert not sdk_tool_failed(result), result.content
                return json.loads(result.content[0].text)["payload"]

            info = await command("get_info")
            assert info["windows"]
            snapshot = await command("dump_state", {"window_index": 0})
            (artifacts / "initial-state.json").write_text(
                json.dumps(snapshot, indent=2), encoding="utf-8"
            )
            sample = artifacts / "sample.log"
            sample.write_text("INFO ready\nERROR smoke שלום 😀\nINFO done\n", encoding="utf-8")
            await command("open_file", {"file": str(sample)})
            info = await command("get_info")
            tab = info["windows"][0]["tabs"][-1]
            tab_id = tab["tabId"]
            await command("search", {"tab_id": tab_id, "text": "ERROR"})
            await command("set_follow_mode", {"tab_id": tab_id, "disabled": True})
            assert not sdk_tool_failed(
                await session.call_tool("cilogg_state", {"window_index": 0})
            )

            clipboard = await command("get_ui", {"object_name": "@clipboard"})
            await command(
                "set_ui",
                {"object_name": "@clipboard", "definition": {"text": "MCP smoke"}},
            )
            assert (await command("get_ui", {"object_name": "@clipboard"}))[
                "text"
            ] == "MCP smoke"
            await command(
                "set_ui", {"object_name": "@clipboard", "definition": clipboard}
            )

            await command(
                "invoke_action", {"object_name": "optionsAction", "window_index": 0}
            )
            objects = []
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                objects = (await command("get_ui", {"window_index": 0}))["objects"]
                dialogs = [obj for obj in objects
                           if obj["className"] == "OptionsDialog" and obj.get("visible")]
                if len(dialogs) != 1:
                    await asyncio.sleep(0.05)
                    continue
                prefix = dialogs[0]["objectPath"] + "/"
                objects = [obj for obj in objects if obj["objectPath"].startswith(prefix)]
                if any(
                    obj["className"] == "QPushButton"
                    and "Cancel" in obj.get("properties", {}).get("text", "")
                    and obj.get("visible")
                    for obj in objects
                ):
                    break
                await asyncio.sleep(0.05)
            else:
                raise AssertionError("Preferences dialog did not become observable")
            (artifacts / "preferences-ui.json").write_text(
                json.dumps(objects, indent=2), encoding="utf-8"
            )
            field = next(
                obj
                for obj in objects
                if obj["className"] == "QCheckBox"
                and obj.get("visible")
                and obj.get("enabled")
            )
            await command(
                "set_ui",
                {
                    "object_name": field["objectPath"],
                    "definition": {"checked": not field["properties"]["checked"]},
                },
            )
            changed = await command("get_ui", {"object_name": field["objectPath"]})
            assert changed["properties"]["checked"] != field["properties"]["checked"]
            cancel = next(
                obj
                for obj in objects
                if obj["className"] == "QPushButton"
                and "Cancel" in obj.get("properties", {}).get("text", "")
                and obj.get("visible")
            )
            await command(
                "activate_ui",
                {"object_name": cancel["objectPath"], "definition": {"event": "click"}},
            )
            grep = await session.call_tool(
                "cilogg_cli",
                {"mode": "grep", "arguments": ["-e", "ERROR", str(sample)]},
            )
            assert not sdk_tool_failed(grep), grep.content
            assert "ERROR smoke שלום 😀" in json.loads(grep.content[0].text)["stdout"]
            dry_run = await session.call_tool(
                "cilogg_cli",
                {
                    "mode": "mcp",
                    "arguments": [
                        "install",
                        "--client",
                        "codex",
                        "--home",
                        str(artifacts / "setup-home"),
                        "--dry-run",
                    ],
                },
            )
            assert not sdk_tool_failed(dry_run), dry_run.content
            assert not (artifacts / "setup-home").exists()
            error = await session.call_tool(
                "cilogg_execute", {"action": "invalid_action"}
            )
            assert sdk_tool_failed(error)
            await command("close_tab", {"tab_id": tab_id})
            (artifacts / "result.json").write_text(
                json.dumps(
                    {
                        "passed": True,
                        "tools": [tool.name for tool in tools.tools],
                        "checks": [
                            "initialize",
                            "tools",
                            "resources",
                            "prompts",
                            "file",
                            "search",
                            "follow",
                            "clipboard",
                            "modal-inspect-edit-cancel",
                            "mouse-click",
                            "grep",
                            "native-setup-dry-run",
                            "tool-error",
                        ],
                    },
                    indent=2,
                ),
                encoding="utf-8",
            )
    print("MCP stdio and live GUI smoke checks passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    options = parser.parse_args()
    asyncio.run(run(options.executable, options.artifacts.resolve()))
