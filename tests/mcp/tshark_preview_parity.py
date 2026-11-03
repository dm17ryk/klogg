"""Opt-in real TShark preview CLI/native-MCP parity in a private host.

External pack configuration is optional and remains outside the repository.
"""

import argparse
import copy
import json
import os
import sys
import time
from pathlib import Path

from analysis_parity import run
from workbench_contracts import compare_json

ROOT = Path(__file__).resolve().parents[2]
VOLATILE = {"requestId", "elapsedMs", "enginePid"}


def comparable(value):
    """Drop top-level transport/process observations, retaining the entire field tree."""
    return {k: v for k, v in value.items() if k not in VOLATILE and k != "id"}


def setup_preview(payload):
    definitions = {"version": 1, "previews": [
        {"name": name, "regex": "^(?<value>.*)$", "format": "fields",
         "fields": [{"name": "Message", "source": "capture", "capture": "value",
                     "type": "string", "format": "string"}]}
        for name in ("Earlier JSON", "Mapped carrier")
    ]}
    (payload / "previews.json").write_text(json.dumps(definitions), encoding="utf-8")


def exercise(cli, command, _finish, _rows, _normalized, catalog, artifacts):
    operations = ("get_preview_decoder", "set_preview_decoder",
                  "show_preview_decoder_settings", "preview_message")
    assert set(operations) <= set(catalog["commander"]["commands"])
    observations = []
    sequence = 0

    def pair(action, request=None, success=True, **selectors):
        nonlocal sequence
        sequence += 1
        path = artifacts / ("tshark-request-%03d.json" % sequence)
        args = []
        mcp = {}
        if request is not None:
            path.write_text(json.dumps(request, ensure_ascii=False), encoding="utf-8")
            args += ["--json-file", str(path)]
            mcp["analysis_json"] = request
        for key, value in selectors.items():
            args += ["--" + key.replace("_", "-"), str(value)]
            mcp[key] = value
        first = cli(action, *args, success=success)
        second = command(action, mcp, success=success)
        left, right = comparable(first), comparable(second)
        compare_json(left, right)
        observations.append({"action": action, "request": request,
                             "cli": first, "mcp": second})
        return first, second

    first, _ = pair("get_preview_decoder")
    assert first["status"] == "disabled", first
    pair("show_preview_decoder_settings")

    config = {"enabled": True, "allowExternalLua": True,
              "tshark": os.environ["CILOGG_TSHARK"],
              "text2pcap": os.environ["CILOGG_TEXT2PCAP"],
              "lua": [str(ROOT / "test_data/message_decoders/pdu_probe.lua")],
              "defaultDissector": "", "inputEncoding": "utf8",
              "previewRoutes": {"Mapped carrier": {"dissector": "cilogg_md01_probe",
                                                       "protocol": "cilogg_md01_probe"}},
              "protocolRoutes": {}}
    accepted, _ = pair("set_preview_decoder", config)
    request = {"text": "AB", "reassemble": False, "timeoutMs": 10000,
               "decoder": "auto", "requestRevision": "parity-revision"}
    a, b = pair("preview_message", request)
    c, d = pair("preview_message", request)
    assert a["backend"] == "tshark_lua" and a["available"], a
    assert a["enginePid"] == c["enginePid"] and b["enginePid"] == d["enginePid"]
    assert a["offsetSpace"] == "engine_data_source_unresolved", a
    assert a["messageCompleteness"] == a["messageIntegrity"] == "not_reported", a
    fields = a["fields"]
    values = lambda name: [f["columns"][1] for f in fields if f["attributes"].get("name") == name]
    assert values("cilogg_md01_probe.item") == ["65", "66"], fields
    assert values("cilogg_md01_probe.generated") == ["9007199254740993"], fields
    assert values("cilogg_md01_probe.transformed"), fields
    explicit, _ = pair("preview_message", dict(request, decoder="json"))
    assert explicit["backend"] == "json_preview", explicit
    direct = copy.deepcopy(config)
    direct["previewRoutes"] = {}
    direct["defaultDissector"] = "cilogg_md01_probe"
    pair("set_preview_decoder", direct)
    assert pair("preview_message", request)[0]["backend"] == "tshark_lua"
    no_route = copy.deepcopy(direct)
    no_route["defaultDissector"] = ""
    pair("set_preview_decoder", no_route)
    fallback = pair("preview_message", request)[0]
    assert fallback["backend"] == "json_preview" and fallback["fallbackReason"] == "no_applicable_decoder", fallback
    assert fallback["preferredBackend"] == "tshark_lua" and fallback["decoderAttempts"], fallback
    no_route["defaultDissector"] = "cilogg_missing_dissector"
    pair("set_preview_decoder", no_route)
    fallback = pair("preview_message", request)[0]
    assert fallback["backend"] == "json_preview" and fallback["fallbackReason"] == "dissector_not_dispatched", fallback
    assert fallback["preferredBackend"] == "tshark_lua" and fallback["decoderAttempts"], fallback
    failure = pair("preview_message", dict(request, decoder="tshark"), success=False)[0]
    assert failure["reason"] == "dissector_not_dispatched", failure
    for key in ("tshark", "lua", "text2pcap"):
        assert pair("preview_message", dict(request, **{key: "untrusted"}), success=False)[0]["reason"], key
    current = pair("get_preview_decoder")[0]
    invalid = copy.deepcopy(no_route)
    invalid["inputEncoding"] = "invalid"
    pair("set_preview_decoder", invalid, success=False)
    assert pair("get_preview_decoder")[0] == current
    disabled = copy.deepcopy(config)
    disabled["enabled"] = False
    pair("set_preview_decoder", disabled)
    assert pair("preview_message", request)[0]["backend"] == "json_preview"

    external = os.environ.get("CILOGG_PREVIEW_ENGINE_CONFIG")
    if external:
        external_config = json.loads(Path(external).read_text(encoding="utf-8"))
        pair("set_preview_decoder", external_config)
        manifest = json.loads((ROOT / "test_data/message_decoders/packs.json").read_text(encoding="utf-8"))
        for case in manifest["cases"]:
            if case.get("knownGap"):
                continue
            raw = bytes.fromhex((ROOT / "test_data/message_decoders" / case["hex"]).read_text())
            sample = {"text": raw.hex(), "reassemble": False, "decoder": "tshark",
                      "dissector": case["dissector"], "inputEncoding": "hex", "timeoutMs": 10000}
            result, _ = pair("preview_message", sample)
            assert result["backend"] == "tshark_lua" and result["available"], result
            for name, expected in case["expect"].items():
                actual = [f["columns"][1] for f in result["fields"] if f["attributes"].get("name") == name]
                assert actual == expected, (case["id"], name, actual, expected)
        body = "EHCI001INF00000004TESTCRC0000"
        headers = ["PUT fixture.dat HTTP/1.1", "Host: CARE-PANELINFO",
                   "Content-Length: %d" % len(body), "Content-Type: application/octet-stream", ""]
        header_bytes = len("\r\n".join(headers).encode()) + 2
        def chunk(parts, size):
            return parts + ["#12 1 INFO:NET Send message command: 5975",
                            "#12 2 DETAILS:NET SendTCPData Len(%d) Port(5975)" % size]
        logs = chunk(headers + [body[:15]], header_bytes + 15)
        logs += ["#22 3 INFO:unrelated diagnostic"]
        logs += chunk([body[15:]], len(body) - 15)
        text_request = {"text": "\n".join(logs), "timeoutMs": 10000}
        assembled = pair("preview_message", text_request)[0]
        assert assembled["backend"] == "tshark_lua" and assembled["inputText"] == body, assembled
        assert assembled["assembly"]["state"] == "complete", assembled
        incomplete = pair("preview_message", {"text": "\n".join(logs[:8]), "timeoutMs": 10000})[0]
        assert incomplete["state"] == "incomplete" and not incomplete["complete"], incomplete
        fixture = artifacts / "tshark-preview-source.log"
        fixture.write_text("unrelated\n" + "\n".join(logs) + "\n", encoding="utf-8")
        cli("open_file", "--file", str(fixture))
        source_request = {"line": 7, "timeoutMs": 10000}
        path = artifacts / "source-readiness.json"
        path.write_text(json.dumps(source_request), encoding="utf-8")
        deadline = time.monotonic() + 30
        while True:
            source = cli("preview_message", "--json-file", str(path), "--window-index", "0",
                         "--tab-index", "0", success=None)
            if source.get("available"):
                break
            assert source.get("reason") in ("source_not_ready", "source_busy"), source
            assert time.monotonic() < deadline, source
            time.sleep(0.02)
        source = pair("preview_message", source_request, window_index=0, tab_index=0)[0]
        assert source["inputText"] == body and source["source"]["lineOneBased"] == "7", source
    (artifacts / "tshark-preview-observations.json").write_text(
        json.dumps(observations, indent=2, ensure_ascii=False), encoding="utf-8")
    print("PASS: %d CLI/native-MCP real preview comparisons%s" %
          (len(observations), " including external packs" if external else ""))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--tshark", required=True, type=Path)
    parser.add_argument("--text2pcap", required=True, type=Path)
    parser.add_argument("--external-config", type=Path)
    options = parser.parse_args()
    os.environ.update(CILOGG_TSHARK=str(options.tshark.resolve()),
                      CILOGG_TEXT2PCAP=str(options.text2pcap.resolve()),
                      CILOGG_SCRIPT_PYTHON=sys.executable)
    if options.external_config:
        os.environ["CILOGG_PREVIEW_ENGINE_CONFIG"] = str(options.external_config.resolve())
    else:
        os.environ.pop("CILOGG_PREVIEW_ENGINE_CONFIG", None)
    run(options.executable.resolve(), options.artifacts.resolve(),
        required_operations=("get_preview_decoder", "set_preview_decoder",
                             "show_preview_decoder_settings", "preview_message"),
        verify=exercise, setup_payload=setup_preview)
