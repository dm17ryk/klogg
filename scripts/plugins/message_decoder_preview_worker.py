"""Private framed-stdio adapter for a persistent real TShark/Lua preview session.

Only the native host supplies trusted tool/script configuration. Capture frame
timestamps and offsets describe an engine buffer, never a log source or event.
The host must own a process group/job before writing the first request.
"""

import argparse
import base64
import binascii
import hashlib
import json
import logging
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import threading
import time

# -I -S deliberately omits the script directory. Resources are materialized
# together by the native host; source-tree execution retains the same imports.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from message_decoder_wire import (
    ProofError,
    fields_named,
    hexdump,
    normalize_pdml,
    validate_dissector,
)

if not (Path(__file__).resolve().parent / "workbench_process.py").is_file():
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "benchmarks"))
from workbench_process import ProcessOwner

LOG = logging.getLogger("cilogg.decoder.preview")
REQUEST_LIMIT = 256 * 1024
RESPONSE_LIMIT = 128 * 1024
PAYLOAD_LIMIT = 64 * 1024
PDML_LIMIT = 1024 * 1024
STDERR_LIMIT = 64 * 1024
ORIGIN = "engine_data_source_unresolved"


def remaining(deadline):
    value = deadline - time.monotonic()
    if value <= 0:
        raise ProofError("execution_timeout")
    return value


def operation_before(operation, deadline):
    """A blocked pipe cannot hold the protocol thread beyond its deadline.

    A timed-out operation is fatal to the channel. The native host then reaps
    its group/job, releasing blocked readers/writers and any Lua descendants.
    """
    done = threading.Event()
    result = []

    def run():
        try:
            result.append((True, operation()))
        except (ProofError, OSError, ValueError) as error:
            result.append((False, error))
        finally:
            done.set()

    threading.Thread(target=run, daemon=True).start()
    try:
        wait_seconds = remaining(deadline)
    except ProofError:
        # Starting the writer can exhaust the budget on a busy host. Keep the
        # same explicit timeout diagnostic as a pipe that expires during wait.
        LOG.warning("preview_pipe_timeout")
        raise
    if not done.wait(wait_seconds):
        LOG.warning("preview_pipe_timeout")
        raise ProofError("execution_timeout")
    if not result[0][0]:
        if isinstance(result[0][1], ProofError):
            raise result[0][1]
        raise ProofError("engine_io_error") from result[0][1]
    return result[0][1]


def write_before(stream, data, deadline):
    def write():
        view = memoryview(data)
        while view:
            count = stream.write(view)
            if count is None or count <= 0:
                raise OSError("pipe write made no progress")
            view = view[count:]
        stream.flush()

    operation_before(write, deadline)


def read_exact(stream, size):
    chunks = bytearray()
    while len(chunks) < size:
        block = stream.read(size - len(chunks))
        if not block:
            raise ProofError("invalid_frame")
        chunks.extend(block)
    return bytes(chunks)


def unique_object(pairs):
    result = {}
    for name, value in pairs:
        if name in result:
            raise ProofError("invalid_json")
        result[name] = value
    return result


def invalid_constant(_):
    raise ProofError("invalid_json")


def read_frame(stream):
    first = stream.read(1)  # Idle warm sessions are allowed to wait for input.
    if not first:
        return None
    deadline = time.monotonic() + 10
    header = first + operation_before(lambda: read_exact(stream, 3), deadline)
    size = struct.unpack(">I", header)[0]
    if not 0 < size <= REQUEST_LIMIT:
        raise ProofError("request_limit")
    data = operation_before(lambda: read_exact(stream, size), deadline)
    try:
        value = json.loads(
            data.decode("utf-8"), object_pairs_hook=unique_object, parse_constant=invalid_constant
        )
    except (ValueError, UnicodeError, RecursionError) as error:
        raise ProofError("invalid_json") from error
    if type(value) is not dict:
        raise ProofError("invalid_request")
    return value


def encode_frame(value, limit=RESPONSE_LIMIT):
    data = json.dumps(value, ensure_ascii=True, separators=(",", ":"), allow_nan=False).encode(
        "ascii"
    )
    if not 0 < len(data) <= limit:
        raise ProofError("response_limit")
    return struct.pack(">I", len(data)) + data


def write_frame(stream, value):
    # Public helper also used by portable tests and native fixture drivers.
    frame = encode_frame(value, REQUEST_LIMIT)
    write_before(stream, frame, time.monotonic() + 1)


def validate_request(value):
    if type(value) is not dict or set(value) != {
        "version",
        "id",
        "op",
        "profile",
        "payloadBase64",
        "timeoutMs",
    }:
        raise ProofError("invalid_request")
    if type(value["version"]) is not int or value["version"] != 1:
        raise ProofError("unsupported_version")
    if type(value["id"]) is not str or not re.fullmatch(r"[A-Za-z0-9_.:-]{1,128}", value["id"]):
        raise ProofError("invalid_id")
    if value["op"] != "evaluate" or type(value["op"]) is not str:
        raise ProofError("invalid_operation")
    if type(value["timeoutMs"]) is not int or not 1 <= value["timeoutMs"] <= 10000:
        raise ProofError("invalid_limits")
    profile = value["profile"]
    if type(profile) is not dict or set(profile) != {
        "tshark",
        "text2pcap",
        "lua",
        "dissector",
        "protocol",
    }:
        raise ProofError("invalid_profile")
    if type(profile["lua"]) is not list or len(profile["lua"]) > 8:
        raise ProofError("input_limit")
    for path in [profile["tshark"], profile["text2pcap"]] + profile["lua"]:
        if (
            type(path) is not str
            or not path
            or len(path) > 32768
            or "\x00" in path
            or not Path(path).is_absolute()
        ):
            raise ProofError("invalid_profile")
        if not Path(path).is_file():
            raise ProofError("input_missing")
    validate_dissector(profile["dissector"])
    if (
        type(profile["protocol"]) is not str
        or len(profile["protocol"]) > 128
        or "\x00" in profile["protocol"]
    ):
        raise ProofError("invalid_profile")
    encoded = value["payloadBase64"]
    if type(encoded) is not str or len(encoded) > 4 * ((PAYLOAD_LIMIT + 2) // 3):
        raise ProofError("input_limit")
    try:
        payload = base64.b64decode(encoded, validate=True)
    except (ValueError, binascii.Error) as error:
        raise ProofError("invalid_payload") from error
    if base64.b64encode(payload).decode("ascii") != encoded:
        raise ProofError("invalid_payload")
    if not 0 < len(payload) <= PAYLOAD_LIMIT:
        raise ProofError("input_limit")
    return payload


def response_base(identity, profile):
    return {
        "version": 1,
        "id": identity,
        "backend": "tshark_lua",
        "state": "failed",
        "reason": "",
        "available": False,
        "complete": False,
        "limited": False,
        "previewName": profile.get("protocol") or profile.get("dissector", ""),
        "offsetSpace": ORIGIN,
        "messageCompleteness": "not_reported",
        "messageIntegrity": "not_reported",
        "fields": [],
    }


def render_fields(packets):
    result = []
    limited = False

    def visit(node, depth):
        nonlocal limited
        if node["attributes"].get("name") in {"frame", "geninfo", "exported_pdu"}:
            return  # Transport metadata includes synthetic capture timestamps.
        if node["kind"] != "packet":
            if len(result) == 512:
                limited = True
                return
            attr = node["attributes"]
            result.append(
                {
                    "columns": [
                        attr.get("name", ""),
                        attr.get("show", attr.get("showname", "")),
                        attr.get("value", ""),
                        attr.get("pos", ""),
                        attr.get("size", ""),
                    ],
                    "depth": depth,
                    "attributes": attr,
                    "positionOrigin": ORIGIN,
                    "id": node["id"],
                }
            )
            depth += 1
        for child in node["children"]:
            visit(child, depth)

    for packet in packets:
        visit(packet, 0)
    return result, limited


class EngineProcess:
    """Continuously drain two pipes with strict retained-byte bounds."""

    def __init__(self, owner, arguments, cwd, env):
        self.process = owner.spawn(
            [str(arg) for arg in arguments],
            cwd=cwd,
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )
        self.buffers = [bytearray(), bytearray()]
        self.eof = [False, False]
        self.failure = None
        self.condition = threading.Condition()
        self.readers = []
        for index, stream in enumerate((self.process.stdout, self.process.stderr)):
            reader = threading.Thread(target=self.drain, args=(index, stream), daemon=True)
            reader.start()
            self.readers.append(reader)
        LOG.debug(
            "preview_process_started pid=%d tool=%s", self.process.pid, Path(arguments[0]).name
        )

    def drain(self, index, stream):
        try:
            while True:
                block = stream.read(16384)
                with self.condition:
                    if not block:
                        self.eof[index] = True
                        self.condition.notify_all()
                        return
                    limit = PDML_LIMIT if index == 0 else STDERR_LIMIT
                    if len(self.buffers[index]) + len(block) > limit:
                        self.failure = "output_limit"
                        self.condition.notify_all()
                        return
                    self.buffers[index].extend(block)
                    self.condition.notify_all()
        except (OSError, ValueError):
            with self.condition:
                self.failure = "engine_read_error"
                self.condition.notify_all()

    def check(self):
        if self.failure:
            raise ProofError(self.failure)
        diagnostics = bytes(self.buffers[1]).lower()
        if b"lua error" in diagnostics or b"error during loading" in diagnostics:
            raise ProofError("lua_failure")

    def finished(self, deadline):
        self.process.stdin.close()
        with self.condition:
            while not all(self.eof):
                self.check()
                self.condition.wait(min(0.02, remaining(deadline)))
            self.check()
        try:
            code = self.process.wait(timeout=remaining(deadline))
        except subprocess.TimeoutExpired as error:
            raise ProofError("execution_timeout") from error
        if code:
            raise ProofError("engine_failure")
        return bytes(self.buffers[0])

    def packet(self, deadline):
        with self.condition:
            while True:
                self.check()
                buffer = self.buffers[0]
                start = buffer.find(b"<packet>")
                end = buffer.find(b"</packet>")
                if start >= 0 and end >= start:
                    prefix = bytes(buffer[:start])
                    if prefix.strip() and (
                        len(prefix) > 8192
                        or b"<pdml " not in prefix
                        or b"<!DOCTYPE" in prefix.upper()
                        or b"<!ENTITY" in prefix.upper()
                    ):
                        raise ProofError("invalid_pdml")
                    packet = bytes(buffer[start : end + 9])
                    del buffer[: end + 9]
                    return b"<pdml>" + packet + b"</pdml>"
                if self.eof[0] or self.process.poll() is not None:
                    raise ProofError("engine_failure")
                self.condition.wait(min(0.02, remaining(deadline)))

    def close_streams(self):
        for reader in self.readers:
            reader.join(timeout=1)
        for stream in (self.process.stdout, self.process.stderr):
            stream.close()


def capture_record(data):
    """Accept exactly one classic-PCAP named Exported-PDU record from text2pcap."""
    if len(data) < 40 or len(data) > PAYLOAD_LIMIT + 65536:
        raise ProofError("invalid_capture")
    endian = {b"\xd4\xc3\xb2\xa1": "<", b"\xa1\xb2\xc3\xd4": ">"}.get(data[:4])
    if not endian:
        raise ProofError("invalid_capture")
    major, minor, _, _, snaplen, linktype = struct.unpack(endian + "HHIIII", data[4:24])
    _, _, captured, original = struct.unpack(endian + "IIII", data[24:40])
    if (
        (major, minor) != (2, 4)
        or linktype != 252
        or captured != original
        or captured > snaplen
        or len(data) != 40 + captured
    ):
        raise ProofError("invalid_capture")
    # The engine stream has a fixed byte order. Convert only record framing;
    # the actual Exported-PDU bytes remain verbatim text2pcap output.
    record = struct.pack("<IIII", 0, 0, captured, original) + data[40:]
    header = struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 262144, 252)
    return header, record


class PreviewWorker:
    def __init__(self, workspace=None):
        self.temporary = tempfile.TemporaryDirectory(prefix="preview-", dir=workspace)
        self.root = Path(self.temporary.name).resolve()
        self.owner = ProcessOwner(inherit_group=True)
        self.engine = None
        self.processes = []
        self.identity = None
        self.scripts = []
        self.script_hashes = []
        self.env = os.environ.copy()
        for name in list(self.env):
            if name.startswith(("WIRESHARK_", "LUA_")):
                del self.env[name]
        for name, relative in (
            ("WIRESHARK_CONFIG_DIR", "config"),
            ("WIRESHARK_PLUGIN_DIR", "plugins"),
            ("APPDATA", "appdata"),
            ("HOME", "home"),
            ("USERPROFILE", "home"),
            ("XDG_CONFIG_HOME", "xdg-config"),
            ("XDG_DATA_HOME", "xdg-data"),
            ("XDG_CACHE_HOME", "xdg-cache"),
        ):
            path = self.root / relative
            path.mkdir(exist_ok=True)
            self.env[name] = str(path)

    def command(self, arguments, deadline):
        child = EngineProcess(self.owner, arguments, self.root, self.env)
        self.processes.append(child)
        try:
            return child.finished(deadline)
        finally:
            if child.process.poll() is not None:
                child.close_streams()
                self.processes.remove(child)
                self.owner.processes.remove(child.process)

    def start(self, profile, deadline):
        identity = tuple([profile["tshark"], profile["text2pcap"]] + profile["lua"])
        if self.identity is not None:
            if identity != self.identity:
                raise ProofError("session_configuration_changed")
            LOG.debug("preview_session_reused pid=%d", self.engine.process.pid)
            return
        self.identity = identity
        for index, original in enumerate(profile["lua"]):
            remaining(deadline)
            with Path(original).open("rb") as source:
                body = source.read(1024 * 1024 + 1)
            if len(body) > 1024 * 1024:
                raise ProofError("input_limit")
            path = self.root / f"selected-{index}.lua"
            path.write_bytes(body)
            self.scripts.append(path)
            self.script_hashes.append(hashlib.sha256(body).hexdigest())
        folders = self.command([profile["tshark"], "-G", "folders"], deadline)
        reported = {
            line.split("\t", 1)[0].split(":", 1)[0].strip(): line.split("\t", 1)[1].strip()
            for line in folders.decode("utf-8").splitlines()
            if "\t" in line
        }
        isolated = {
            "Personal configuration",
            "Personal Plugins",
            "Global Plugins",
            "Personal Lua Plugins",
            "Global Lua Plugins",
        }
        if not isolated.issubset(reported) or not all(
            self.root in Path(reported[name]).resolve().parents for name in isolated
        ):
            raise ProofError("plugin_isolation_unverified")
        LOG.debug(
            "preview_plugin_isolation_verified folders=%d scripts=%d",
            len(isolated),
            len(self.scripts),
        )
        extension = [part for path in self.scripts for part in ("-X", "lua_script:" + str(path))]
        self.engine = EngineProcess(
            self.owner,
            [profile["tshark"], "-n", *extension, "-l", "-r", "-", "-T", "pdml"],
            self.root,
            self.env,
        )
        self.processes.append(self.engine)

    def evaluate(self, request):
        started = time.monotonic()
        payload = validate_request(request)
        deadline = started + request["timeoutMs"] / 1000
        profile = request["profile"]
        self.start(profile, deadline)
        dump = self.root / "input.hex"
        capture = self.root / "input.pcap"
        dump.write_text(hexdump(payload, PAYLOAD_LIMIT), encoding="ascii")
        capture.unlink(missing_ok=True)
        self.command(
            [
                profile["text2pcap"],
                "-q",
                "-F",
                "pcap",
                "-m",
                "262144",
                "-P",
                profile["dissector"],
                dump,
                capture,
            ],
            deadline,
        )
        if not capture.is_file() or capture.stat().st_size > PAYLOAD_LIMIT + 65536:
            raise ProofError("bridge_output_limit")
        header, record = capture_record(capture.read_bytes())
        if not getattr(self, "header_written", False):
            record = header + record
            self.header_written = True
        write_before(self.engine.process.stdin, record, deadline)
        data = self.engine.packet(deadline)
        packets = normalize_pdml(data, output_limit=PDML_LIMIT, node_limit=2048, depth_limit=32)
        if len(packets) != 1:
            raise ProofError("invalid_pdml")
        if fields_named(packets, "_ws.lua.error") or any(
            "lua error" in node["attributes"].get("show", "").lower()
            for node in fields_named(packets, "_ws.expert.message")
        ):
            raise ProofError("lua_failure")
        fields, limited = render_fields(packets)
        if not fields:
            # Missing entrypoints can leave only the real engine's transport
            # envelope. This is absence of decoded output, not a protocol-name
            # comparison: an alias may legitimately emit differently named nodes.
            LOG.warning("preview_no_decoded_fields id=%s", request["id"])
            raise ProofError("dissector_not_dispatched")
        if profile["protocol"]:

            def selected_protocols(nodes):
                selected = []
                for node in nodes:
                    if node["attributes"].get("name") in {"frame", "geninfo", "exported_pdu"}:
                        continue
                    if (
                        node["kind"] == "proto"
                        and node["attributes"].get("name") == profile["protocol"]
                    ):
                        # Keep each selected subtree once, including any nested
                        # occurrence of the same protocol already in that tree.
                        selected.append(node)
                    else:
                        selected.extend(selected_protocols(node["children"]))
                return selected

            selected = selected_protocols(packets)
            if not selected:
                LOG.warning("preview_protocol_not_found id=%s", request["id"])
                raise ProofError("protocol_not_found")
            fields, limited = render_fields(selected)
            LOG.debug("preview_protocol_selected id=%s subtrees=%d", request["id"], len(selected))
        else:
            LOG.debug("preview_all_decoded_fields_selected id=%s", request["id"])
        response = response_base(request["id"], profile)
        response.update(
            state="completed",
            available=True,
            complete=not limited,
            limited=limited,
            reason="rendered_field_limit" if limited else "",
            fields=fields,
            enginePid=self.engine.process.pid,
            scriptSha256=self.script_hashes,
        )
        # A response budget failure is explicit and carries no partially retained tree.
        try:
            encode_frame(response)
        except ProofError:
            response.update(complete=False, limited=True, reason="response_limit", fields=[])
        remaining(deadline)
        LOG.info(
            "preview_evaluated id=%s pid=%d payload_bytes=%d fields=%d limited=%s",
            request["id"],
            self.engine.process.pid,
            len(payload),
            len(response["fields"]),
            response["limited"],
        )
        return response

    def close(self):
        try:
            self.owner.close()
        finally:
            for child in self.processes:
                child.close_streams()
            self.temporary.cleanup()
        LOG.debug("preview_session_closed")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path)
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG, format="%(levelname)s %(name)s %(message)s")
    worker = PreviewWorker(args.workspace)
    try:
        while True:
            request = None
            try:
                request = read_frame(sys.stdin.buffer)
                if request is None:
                    return 0
                started = time.monotonic()
                response = worker.evaluate(request)
            except (ProofError, OSError, ValueError, RecursionError) as error:
                reason = str(error) if isinstance(error, ProofError) else "engine_io_error"
                LOG.warning("preview_channel_failed reason=%s", reason)
                identity = request.get("id", "") if type(request) is dict else ""
                if type(identity) is str and re.fullmatch(r"[A-Za-z0-9_.:-]{1,128}", identity):
                    response = response_base(identity, {})
                    response["reason"] = reason
                    response["limited"] = reason in {
                        "request_limit",
                        "response_limit",
                        "input_limit",
                        "output_limit",
                        "field_limit",
                        "bridge_output_limit",
                    }
                    write_before(sys.stdout.buffer, encode_frame(response), time.monotonic() + 1)
                return 1  # Fatal: host must reap/reset the entire channel group.
            write_before(
                sys.stdout.buffer, encode_frame(response), started + request["timeoutMs"] / 1000
            )
    finally:
        worker.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ProofError, OSError, ValueError) as error:
        LOG.error(
            "preview_worker_failed reason=%s",
            str(error) if isinstance(error, ProofError) else "worker_io_error",
        )
        raise SystemExit(1)
