"""Test-only MD01 bridge to real TShark/Lua. This is not a product decoder."""

import argparse
import hashlib
import json
import logging
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "benchmarks"))
from workbench_process import ProcessOwner

LOG = logging.getLogger("cilogg.decoder.proof")
INPUT_LIMIT = 1024 * 1024
OUTPUT_LIMIT = 16 * 1024 * 1024
BRIDGE_FRAME_LIMIT = 262144


class ProofError(RuntimeError):
    """Stable reason separate from private engine diagnostics."""

    def __init__(self, reason):
        super().__init__(reason)
        self.cleanup_reason = None


def run_bounded(arguments, *, cwd, env, timeout=30.0, output_limit=OUTPUT_LIMIT):
    """Bound both output streams and wall time; reap only this call's owned tree."""
    if not 0 < timeout <= 120 or not 0 < output_limit <= OUTPUT_LIMIT:
        raise ProofError("invalid_limits")
    started = time.monotonic()
    owner = ProcessOwner()
    buffers = [bytearray(), bytearray()]
    exceeded = threading.Event()
    read_failed = threading.Event()
    readers = []

    def drain(stream, buffer):
        try:
            while True:
                block = stream.read(16384)
                if not block:
                    return
                if len(buffer) + len(block) > output_limit:
                    exceeded.set()
                    return
                buffer.extend(block)
        except (OSError, ValueError):
            read_failed.set()

    LOG.debug(
        "proof_process_start tool=%s timeout=%s output_limit=%d",
        Path(arguments[0]).name,
        timeout,
        output_limit,
    )
    try:
        process = owner.spawn(
            arguments,
            cwd=cwd,
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )
        for stream, buffer in zip((process.stdout, process.stderr), buffers):
            reader = threading.Thread(target=drain, args=(stream, buffer), daemon=True)
            reader.start()
            readers.append(reader)
        while True:
            if exceeded.is_set():
                LOG.warning("proof_process_output_limit pid=%d", process.pid)
                raise ProofError("output_limit")
            if time.monotonic() - started >= timeout:
                LOG.warning("proof_process_timeout pid=%d", process.pid)
                raise ProofError("execution_timeout")
            if process.poll() is not None and all(
                not reader.is_alive() for reader in readers
            ):
                break
            exceeded.wait(min(0.02, max(0, timeout - (time.monotonic() - started))))
    except OSError as error:
        LOG.warning("proof_process_io_failed errno=%s", error.errno)
        raise ProofError("engine_io_error") from error
    finally:
        # Closing the owner also releases descendants which inherited output pipes.
        primary = sys.exc_info()[1]
        cleanup_error = None
        try:
            owner.close()
        except (OSError, RuntimeError) as error:
            cleanup_error = error
            LOG.error("proof_process_cleanup_failed type=%s", type(error).__name__)
        finally:
            for reader in readers:
                reader.join(timeout=5)
            for process in owner.processes:
                if process.stderr is not None:
                    process.stderr.close()
        if cleanup_error is not None:
            if isinstance(primary, ProofError):
                primary.cleanup_reason = "process_cleanup_failed"
            elif primary is None:
                raise ProofError("process_cleanup_failed") from cleanup_error
    if any(reader.is_alive() for reader in readers):
        raise ProofError("reader_cleanup_failed")
    if exceeded.is_set():
        raise ProofError("output_limit")
    if read_failed.is_set():
        raise ProofError("engine_read_error")
    LOG.info(
        "proof_process_finished code=%d stdout_bytes=%d stderr_bytes=%d",
        process.returncode,
        len(buffers[0]),
        len(buffers[1]),
    )
    return {
        "argv": [str(arg) for arg in arguments],
        "returnCode": process.returncode,
        "stdout": bytes(buffers[0]),
        "stderr": bytes(buffers[1]),
        "elapsedSeconds": time.monotonic() - started,
        "ownedProcessesReaped": True,
    }


def validate_dissector(name):
    if not isinstance(name, str) or not re.fullmatch(
        r"[A-Za-z][A-Za-z0-9_.-]{0,127}", name
    ):
        raise ProofError("invalid_dissector")


def hexdump(payload):
    if not 0 < len(payload) <= INPUT_LIMIT:
        raise ProofError("input_limit")
    return "".join(
        f"{offset:06x} {payload[offset : offset + 16].hex(' ')}\n"
        for offset in range(0, len(payload), 16)
    )


def normalize_pdml(data):
    """Preserve order, tree and exact attributes; engine positions are NOT log offsets."""
    if len(data) > OUTPUT_LIMIT:
        raise ProofError("output_limit")
    try:
        text = data.decode("utf-8")
    except UnicodeError as error:
        raise ProofError("invalid_pdml") from error
    if "<!DOCTYPE" in text.upper() or "<!ENTITY" in text.upper() or "\x00" in text:
        raise ProofError("invalid_pdml")
    try:
        document = ET.fromstring(text)
    except ET.ParseError as error:
        raise ProofError("invalid_pdml") from error
    if document.tag != "pdml" or not document.findall("packet"):
        raise ProofError("invalid_pdml")
    count = 0

    def node(element, identity, depth):
        nonlocal count
        count += 1
        if count > 50000 or depth > 128:
            raise ProofError("field_limit")
        return {
            "id": identity,
            "kind": element.tag,
            "attributes": dict(element.attrib),
            "positionOrigin": "engine_data_source_unresolved",
            "children": [
                node(child, f"{identity}/{i}", depth + 1)
                for i, child in enumerate(element)
            ],
        }

    packets = [
        node(packet, str(i), 0) for i, packet in enumerate(document.findall("packet"))
    ]
    LOG.info(
        "proof_pdml_normalized packets=%d nodes=%d origin=unresolved",
        len(packets),
        count,
    )
    return packets


def fields_named(nodes, name):
    result = []
    for node in nodes:
        if node["attributes"].get("name") == name:
            result.append(node)
        result.extend(fields_named(node["children"], name))
    return result


def load_manifest(path, *, manifest_bytes=None, read_source=None):
    """Validate fixtures; optional frozen bytes/reader avoid reopening mutable inputs."""
    path = Path(path).resolve()
    if not path.is_file():
        raise ProofError("input_missing")
    try:
        if manifest_bytes is None:
            if path.stat().st_size > INPUT_LIMIT:
                raise ProofError("input_limit")
            with path.open("rb") as source:
                manifest_bytes = source.read(INPUT_LIMIT + 1)
        if len(manifest_bytes) > INPUT_LIMIT:
            raise ProofError("input_limit")
        manifest = json.loads(manifest_bytes.decode("utf-8"))
        cases = manifest["cases"]
        if not isinstance(cases, list) or not 0 < len(cases) <= 32:
            raise ProofError("invalid_fixture")
        ids = set()
        for case in cases:
            validate_dissector(case["dissector"])
            identity = case["id"]
            if (
                not isinstance(identity, str)
                or not re.fullmatch(r"[a-z0-9_-]{1,64}", identity)
                or identity in ids
            ):
                raise ProofError("invalid_fixture")
            ids.add(identity)
            if not isinstance(case.get("expect"), dict) or len(case["expect"]) > 128:
                raise ProofError("invalid_fixture")
            for name, values in case["expect"].items():
                if (
                    not isinstance(name, str)
                    or not isinstance(values, list)
                    or not all(isinstance(value, str) for value in values)
                ):
                    raise ProofError("invalid_fixture")
            declared_source = path.parent / case["hex"]
            source = declared_source.resolve()
            if path.parent not in source.parents:
                raise ProofError("invalid_fixture")
            if read_source is not None:
                encoded = read_source(declared_source, source, INPUT_LIMIT * 3)
            else:
                if not source.is_file():
                    raise ProofError("input_missing")
                if source.stat().st_size > INPUT_LIMIT * 3:
                    raise ProofError("input_limit")
                with source.open("rb") as data_file:
                    encoded = data_file.read(INPUT_LIMIT * 3 + 1)
            if len(encoded) > INPUT_LIMIT * 3:
                raise ProofError("input_limit")
            payload = bytes.fromhex(encoded.decode("ascii"))
            if not 0 < len(payload) <= INPUT_LIMIT:
                raise ProofError("input_limit")
            if hashlib.sha256(payload).hexdigest() != case["sha256"]:
                raise ProofError("fixture_hash_mismatch")
            case["payload"] = payload
            case["manifestSha256"] = hashlib.sha256(manifest_bytes).hexdigest()
            LOG.debug(
                "proof_fixture_validated id=%s bytes=%d sha256=%s",
                identity,
                len(payload),
                case["sha256"],
            )
        return cases
    except (KeyError, TypeError, ValueError, UnicodeError, RecursionError) as error:
        raise ProofError("invalid_fixture") from error


class PduEngine:
    def __init__(self, tshark, text2pcap, workspace, scripts=()):
        self.tshark, self.text2pcap = Path(tshark).resolve(), Path(text2pcap).resolve()
        originals = [Path(script).resolve() for script in scripts]
        if len(originals) > 8:
            raise ProofError("input_limit")
        for path in [self.tshark, self.text2pcap] + originals:
            if not path.is_file():
                raise ProofError("input_missing")
        self.root = Path(workspace).resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        self.scripts = []
        self.script_metadata = []
        script_dir = self.root / "scripts"
        script_dir.mkdir(exist_ok=True)
        for i, original in enumerate(originals):
            if original.stat().st_size > INPUT_LIMIT:
                raise ProofError("input_limit")
            with original.open("rb") as source:
                data = source.read(INPUT_LIMIT + 1)
            if len(data) > INPUT_LIMIT:
                raise ProofError("input_limit")
            snapshot = script_dir / f"{i}.lua"
            snapshot.write_bytes(data)
            self.scripts.append(snapshot)
            self.script_metadata.append(
                {
                    "name": original.name,
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "license": "not_verified",
                }
            )
        self.isolated = False
        self.env = os.environ.copy()
        for variable, relative in (
            ("WIRESHARK_CONFIG_DIR", "config"),
            ("WIRESHARK_PLUGIN_DIR", "plugins"),
            ("APPDATA", "appdata"),
            ("XDG_CONFIG_HOME", "xdg-config"),
            ("XDG_DATA_HOME", "xdg-data"),
        ):
            directory = self.root / relative
            directory.mkdir(exist_ok=True)
            self.env[variable] = str(directory)
        self.extension = [
            part
            for script in self.scripts
            for part in ("-X", "lua_script:" + str(script))
        ]

    def command(self, arguments, timeout=30.0):
        result = run_bounded(
            [str(arg) for arg in arguments], cwd=self.root, env=self.env, timeout=timeout
        )
        if result["returnCode"]:
            LOG.warning(
                "proof_engine_nonzero code=%d stderr_bytes=%d",
                result["returnCode"],
                len(result["stderr"]),
            )
            raise ProofError("engine_failure")
        # Some Lua load/runtime errors are printed with exit code zero.
        if (
            b"lua error" in result["stderr"].lower()
            or b"error during loading" in result["stderr"].lower()
        ):
            raise ProofError("lua_failure")
        return result

    def capabilities(self):
        version = self.command([self.tshark, "-v"])
        text_version = self.command([self.text2pcap, "-v"])
        folders = self.command([self.tshark, "-G", "folders"])
        folder_text = folders["stdout"].decode("utf-8", errors="replace")
        lua_folders = [
            line.split("\t", 1)[1].strip()
            for line in folder_text.splitlines()
            if "Lua Plugins:" in line and "\t" in line
        ]
        isolated = len(lua_folders) == 2 and all(
            self.root in Path(folder).resolve().parents for folder in lua_folders
        )
        if not isolated:
            raise ProofError("plugin_isolation_unverified")
        self.isolated = True
        version_text = version["stdout"].decode("utf-8", errors="replace")
        lua_available = "+Lua " in version_text or "with Lua " in version_text
        if self.scripts and not lua_available:
            raise ProofError("lua_unavailable")
        LOG.info(
            "proof_capabilities lua=%s lua_folders_isolated=%s", lua_available, isolated
        )
        binaries = []
        for binary in (self.tshark, self.text2pcap):
            digest = hashlib.sha256()
            with binary.open("rb") as source:
                for block in iter(lambda: source.read(65536), b""):
                    digest.update(block)
            binaries.append(
                {
                    "name": binary.name,
                    "bytes": binary.stat().st_size,
                    "sha256": digest.hexdigest(),
                }
            )
        return {
            "tsharkVersion": version_text,
            "text2pcapVersion": text_version["stdout"].decode(
                "utf-8", errors="replace"
            ),
            "luaAvailable": lua_available,
            "luaFoldersIsolated": isolated,
            "scripts": self.script_metadata,
            "binaries": binaries,
        }

    def decode(self, payload, dissector):
        validate_dissector(dissector)
        dump_text = hexdump(payload)
        # DISSECTOR_NAME TLV (4 bytes), padded ASCII name, end-of-options (4).
        header_bytes = 8 + ((len(dissector) + 3) // 4) * 4
        if len(payload) + header_bytes > BRIDGE_FRAME_LIMIT:
            LOG.warning(
                "proof_bridge_input_limit bytes=%d header=%d frame_limit=%d",
                len(payload),
                header_bytes,
                BRIDGE_FRAME_LIMIT,
            )
            raise ProofError("bridge_input_limit")
        if not self.isolated:
            self.capabilities()
        # One immutable PDU per call; no invented TCP/IP endpoints or reassembly.
        dump = self.root / "input.hex"
        capture = self.root / "input.pcapng"
        dump.write_text(dump_text, encoding="ascii")
        if capture.exists():
            capture.unlink()
        bridge = self.command(
            [
                self.text2pcap,
                "-q",
                "-m",
                str(BRIDGE_FRAME_LIMIT),
                "-P",
                dissector,
                dump,
                capture,
            ]
        )
        if not capture.is_file() or capture.stat().st_size > BRIDGE_FRAME_LIMIT + 65536:
            raise ProofError("bridge_output_limit")
        result = self.command(
            [self.tshark, "-n", *self.extension, "-r", capture, "-T", "pdml"]
        )
        packets = normalize_pdml(result["stdout"])
        if fields_named(packets, "_ws.lua.error") or any(
            "lua error" in field["attributes"].get("show", "").lower()
            for field in fields_named(packets, "_ws.expert.message")
        ):
            raise ProofError("lua_failure")
        commands = [
            {key: value for key, value in command.items() if key != "stdout"}
            for command in (bridge, result)
        ]
        for command in commands:
            command["stderr"] = command["stderr"].decode("utf-8", errors="replace")
        return {
            "inputBytes": len(payload),
            "inputSha256": hashlib.sha256(payload).hexdigest(),
            "dissector": dissector,
            "packets": packets,
            "commands": commands,
            "positionOrigin": "engine_data_source_unresolved",
        }


def run_pdu_proof(engine, manifest_path, report_limit=OUTPUT_LIMIT):
    if type(report_limit) is not int or not 0 < report_limit <= OUTPUT_LIMIT:
        raise ProofError("invalid_limits")
    cases = load_manifest(manifest_path)
    report = {
        "schemaVersion": 1,
        "state": "failed",
        "passed": False,
        "productDecoderAccepted": False,
        "md01Acceptance": "pending",
        "manifestSha256": cases[0]["manifestSha256"],
        "harnessSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "capabilities": engine.capabilities(),
        "cases": [],
    }
    retained_bytes = 0
    for case in cases:
        record = {"id": case["id"], "passed": False, "reason": None}
        try:
            result = engine.decode(case["payload"], case["dissector"])
            record.update(result)
            protocol = fields_named(result["packets"], case["dissector"])
            observed = {
                name: [
                    field["attributes"].get("show")
                    for field in fields_named(result["packets"], name)
                ]
                for name in case["expect"]
            }
            record["observed"] = observed
            record["expected"] = case["expect"]
            if not protocol:
                record["reason"] = "dissector_not_dispatched"
            elif observed != case["expect"]:
                record["reason"] = "field_oracle_mismatch"
            else:
                record["passed"] = True
            # A declared gap is an observation, never a passing decoder capability.
            record["knownGap"] = case.get("knownGap")
            LOG.info(
                "proof_case_complete id=%s passed=%s reason=%s",
                case["id"],
                record["passed"],
                record["reason"],
            )
        except ProofError as error:
            record["reason"] = str(error)
            if error.cleanup_reason:
                record["secondaryReasons"] = [error.cleanup_reason]
            LOG.warning("proof_case_failed id=%s reason=%s", case["id"], error)
        record_bytes = len(json.dumps(record, ensure_ascii=True).encode("utf-8"))
        if retained_bytes + record_bytes > report_limit:
            report["cases"].append(
                {
                    "id": case["id"],
                    "passed": False,
                    "reason": "report_storage_limit",
                    "decodedOutputRetained": False,
                }
            )
            LOG.warning(
                "proof_report_limit id=%s retained_bytes=%d limit=%d",
                case["id"],
                retained_bytes,
                report_limit,
            )
            break
        report["cases"].append(record)
        retained_bytes += record_bytes
    report["passed"] = all(case["passed"] for case in report["cases"])
    report["retainedCaseBytes"] = retained_bytes
    report["state"] = "qualified" if report["passed"] else "failed"
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tshark", required=True, type=Path)
    parser.add_argument("--text2pcap", required=True, type=Path)
    parser.add_argument(
        "--lua",
        action="append",
        default=[],
        type=Path,
        help="Explicit trusted Lua pack; executes with current user's permissions",
    )
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    args.output.mkdir(parents=True, exist_ok=True)
    if argv is None:
        logging.basicConfig(
            level=logging.DEBUG, format="%(levelname)s %(name)s %(message)s"
        )
    try:
        # A failed/cancelled new invocation must not expose an old qualified run.
        (args.output / "result.json").unlink(missing_ok=True)
        # Validate input before starting an engine. No user's profile is modified.
        load_manifest(args.manifest)
        with tempfile.TemporaryDirectory(
            prefix="engine-", dir=args.output
        ) as directory:
            engine = PduEngine(args.tshark, args.text2pcap, directory, args.lua)
            report = run_pdu_proof(engine, args.manifest)
        code = 0 if report["passed"] else 1
    except (ProofError, OSError) as error:
        reason = str(error) if isinstance(error, ProofError) else "io_error"
        report = {
            "schemaVersion": 1,
            "passed": False,
            "state": "failed",
            "reason": reason,
            "productDecoderAccepted": False,
        }
        if isinstance(error, ProofError) and error.cleanup_reason:
            report["secondaryReasons"] = [error.cleanup_reason]
        code = (
            2
            if reason
            in {
                "input_missing",
                "input_limit",
                "invalid_fixture",
                "fixture_hash_mismatch",
                "invalid_dissector",
            }
            else 1
        )
        LOG.warning("proof_failed reason=%s", reason)
    (args.output / "result.json").write_text(
        json.dumps(report, ensure_ascii=True, indent=2) + "\n", encoding="utf-8"
    )
    return code


if __name__ == "__main__":
    raise SystemExit(main())
