"""Public CLI/native-MCP preview checks inside the existing private-host harness."""

import argparse
import json
import time
from pathlib import Path

from analysis_parity import run
from workbench_contracts import compare_json


def setup_preview(payload):
    definition = {"version": 1, "previews": [{
        "name": "Preview parity fixture", "regex": "^(?<value>.*)$", "format": "fields",
        "fields": [{"name": "Message", "source": "capture", "capture": "value",
                    "type": "string", "format": "string"}]}]}
    (payload / "previews.json").write_text(json.dumps(definition), encoding="utf-8")


def exercise_preview(cli, command, _finish, _rows, _normalized, catalog, artifacts):
    assert "preview_message" in catalog["commander"]["commands"]
    body = "EHCI001INF00000004TESTCRC0000"
    payload = ["PUT fixture.dat HTTP/1.1", "Host: CARE-PANELINFO",
               f"Content-Length: {len(body)}", "Content-Type: application/octet-stream", ""]
    header_bytes = len("\r\n".join(payload).encode()) + 2

    def chunk(parts, size):
        return parts + ["#12 1 INFO:NET Send message command: 5975",
                        f"#12 2 DETAILS:NET SendTCPData Len({size}) Port(5975)"]

    logs = chunk(payload + [body[:15]], header_bytes + 15)
    logs += ["#22 3 INFO:unrelated diagnostic"]
    logs += chunk([body[15:]], len(body) - 15)
    samples = [
        {"text": "PREVIEW_PARITY <b> שלום 😀", "reassemble": False},
        {"text": "\n".join(logs)},
        {"text": "\n".join(logs[:8])},
    ]
    observations = []
    for index, request in enumerate(samples):
        path = artifacts / f"preview-request-{index}.json"
        path.write_text(json.dumps(request, ensure_ascii=False), encoding="utf-8")
        first = cli("preview_message", "--json-file", str(path))
        second = command("preview_message", {"analysis_json": request})
        volatile = {"requestId", "elapsedMs"}
        compare_json({k: v for k, v in first.items() if k not in volatile},
                     {k: v for k, v in second.items() if k not in volatile})
        assert first["backend"] == "json_preview", first
        if index < 2:
            assert first["available"] and first["complete"], first
            assert first["fields"][0]["columns"][1] == (request["text"] if index == 0 else body)
        if index == 0:
            assert "&lt;b&gt;" in first["tooltipHtml"] and "<b> שלום" not in first["tooltipHtml"]
        if index == 1:
            assert first["assembly"]["state"] == "complete", first
            assert first["inputText"] == body
        if index == 2:
            assert first["state"] == "incomplete" and not first["complete"], first
        observations.append({"state": first["state"], "assembly": first["assembly"]})
    fixture = artifacts / "preview-source.log"
    fixture.write_text("unrelated\n" + "\n".join(logs) + "\n", encoding="utf-8")
    cli("open_file", "--file", str(fixture))
    path = artifacts / "preview-source-request.json"
    path.write_text(json.dumps({"line": 7}), encoding="utf-8")
    deadline = time.monotonic() + 30
    while True:
        first = cli("preview_message", "--json-file", str(path), "--window-index", "0", "--tab-index", "0", success=None)
        if first.get("available"):
            break
        assert first.get("reason") in ("source_not_ready", "source_busy"), first
        assert time.monotonic() < deadline, "Preview source never became ready"
        time.sleep(0.02)
    second = command("preview_message", {"analysis_json": {"line": 7}, "window_index": 0, "tab_index": 0})
    compare_json({k: v for k, v in first.items() if k not in {"requestId", "elapsedMs"}},
                 {k: v for k, v in second.items() if k not in {"requestId", "elapsedMs"}})
    assert first["inputText"] == body and first["source"]["lineOneBased"] == "7", first
    invalid = {"text": "x", "timeoutMs": 0}
    path = artifacts / "preview-invalid.json"
    path.write_text(json.dumps(invalid), encoding="utf-8")
    assert cli("preview_message", "--json-file", str(path), success=False)["reason"] == \
        command("preview_message", {"analysis_json": invalid}, success=False)["reason"] == "invalid_timeout"
    (artifacts / "preview-observations.json").write_text(
        json.dumps(observations, indent=2), encoding="utf-8")
    print("PASS: Preview CLI/native-MCP discovery, Unicode, reassembly, incomplete status and error parity")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    options = parser.parse_args()
    run(options.executable.resolve(), options.artifacts.resolve(),
        required_operations=("preview_message",), verify=exercise_preview, setup_payload=setup_preview)
