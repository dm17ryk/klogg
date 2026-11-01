"""MD01 reference-capture proof. Summary output excludes private packet/field bodies."""

import argparse
import hashlib
import json
import logging
import math
import os
import re
import tempfile
import time
from pathlib import Path

from message_decoder_pdu import (
    BRIDGE_FRAME_LIMIT,
    OUTPUT_LIMIT,
    PduEngine,
    ProofError,
    fields_named,
    normalize_pdml,
    validate_dissector,
)

LOG = logging.getLogger("cilogg.decoder.reference")
CAPTURE_LIMIT = 64 * 1024 * 1024
RECORD_LIMIT = 32


def snapshot_capture(source, destination, expected_sha256):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if not source.is_file():
        raise ProofError("input_missing")
    if not isinstance(expected_sha256, str) or not re.fullmatch(
        r"[0-9A-Fa-f]{64}", expected_sha256
    ):
        raise ProofError("invalid_reference_hash")
    if source == destination:
        raise ProofError("invalid_reference_destination")
    if not 0 < source.stat().st_size <= CAPTURE_LIMIT:
        raise ProofError("reference_input_limit")
    digest, count = hashlib.sha256(), 0
    with source.open("rb") as reader, destination.open("xb") as writer:
        before = os.fstat(reader.fileno())
        while True:
            block = reader.read(65536)
            if not block:
                break
            count += len(block)
            if count > CAPTURE_LIMIT:
                raise ProofError("reference_input_limit")
            digest.update(block)
            writer.write(block)
        after = os.fstat(reader.fileno())
    current = source.stat()
    signature = lambda stat: (stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns)
    if signature(before) != signature(after) or signature(after) != signature(current):
        raise ProofError("reference_changed")
    if digest.hexdigest() != expected_sha256.lower():
        raise ProofError("reference_hash_mismatch")
    LOG.info(
        "reference_snapshot_validated bytes=%d sha256=%s", count, digest.hexdigest()
    )
    return {"bytes": count, "sha256": digest.hexdigest()}


def parse_raw_packets(data, dissector):
    validate_dissector(dissector)
    if len(data) > OUTPUT_LIMIT:
        raise ProofError("output_limit")

    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ProofError("duplicate_json_keys")
            result[key] = value
        return result

    def bounded_integer(value):
        if len(value.lstrip("-")) > 20:
            raise ValueError("raw metadata integer exceeds limit")
        return int(value)

    def invalid_constant(value):
        raise ValueError("non-finite JSON value")

    try:
        packets = json.loads(
            data.decode("utf-8"),
            object_pairs_hook=unique_object,
            parse_int=bounded_integer,
            parse_constant=invalid_constant,
        )
        if not isinstance(packets, list):
            raise ProofError("invalid_raw_protocol")
        if not packets:
            raise ProofError("reference_no_protocol")
        result, count = [], 0
        for packet in packets:
            raw = packet["_source"]["layers"][dissector + "_raw"]
            if not isinstance(raw, list) or not raw:
                raise ProofError("invalid_raw_protocol")
            occurrences = [raw] if isinstance(raw[0], str) else raw
            records = []
            for occurrence in occurrences:
                count += 1
                if count > RECORD_LIMIT:
                    raise ProofError("reference_record_limit")
                if (
                    not isinstance(occurrence, list)
                    or len(occurrence) not in (5, 6)
                    or not isinstance(occurrence[0], str)
                    or not re.fullmatch(r"[0-9a-fA-F]+", occurrence[0])
                    or not all(
                        type(item) is int and item >= 0 for item in occurrence[1:]
                    )
                    or occurrence[4] != 1
                ):
                    raise ProofError("invalid_raw_protocol")
                payload = bytes.fromhex(occurrence[0])
                if (
                    len(payload) != occurrence[2]
                    or not 0 < len(payload) <= BRIDGE_FRAME_LIMIT
                ):
                    raise ProofError("invalid_raw_protocol")
                records.append(
                    {
                        "payload": payload,
                        "engineRawMetadata": occurrence[1:],
                        "engineDataSourceIndex": occurrence[5]
                        if len(occurrence) == 6
                        else None,
                    }
                )
            result.append(records)
        LOG.info(
            "reference_raw_validated packets=%d protocol_occurrences=%d",
            len(result),
            count,
        )
        return result
    except (KeyError, TypeError, ValueError, UnicodeError, RecursionError) as error:
        raise ProofError("invalid_raw_protocol") from error


def comparable_tree(node):
    return {
        "kind": node["kind"],
        "attributes": {
            key: value for key, value in node["attributes"].items() if key != "pos"
        },
        "children": [comparable_tree(child) for child in node["children"]],
    }


def tree_digest(tree):
    return hashlib.sha256(
        json.dumps(
            tree, sort_keys=True, ensure_ascii=True, separators=(",", ":")
        ).encode("ascii")
    ).hexdigest()


class ReferenceEngine(PduEngine):
    def __init__(self, tshark, text2pcap, workspace, scripts=(), timeout=60.0):
        if (
            isinstance(timeout, bool)
            or not isinstance(timeout, (float, int))
            or not math.isfinite(timeout)
            or not 0 < timeout <= 600
        ):
            raise ProofError("invalid_limits")
        self.started = time.monotonic()
        self.deadline = self.started + timeout
        self.commands = []
        super().__init__(tshark, text2pcap, workspace, scripts)

    def remaining(self):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise ProofError("reference_timeout")
        return remaining

    def command(self, arguments, timeout=30.0):
        remaining = min(timeout, self.remaining())
        record = {"argv": [str(arg) for arg in arguments], "state": "running"}
        self.commands.append(record)
        started = time.monotonic()
        try:
            result = super().command(arguments, timeout=remaining)
            record.update(
                state="completed",
                returnCode=result["returnCode"],
                stdoutBytes=len(result["stdout"]),
                stderrBytes=len(result["stderr"]),
                stderrSha256=hashlib.sha256(result["stderr"]).hexdigest(),
                ownedProcessesReaped=result["ownedProcessesReaped"],
            )
            self.remaining()
            return result
        except ProofError as error:
            if str(error) == "execution_timeout" and time.monotonic() >= self.deadline:
                converted = ProofError("reference_timeout")
                converted.cleanup_reason = error.cleanup_reason
                error = converted
            record.update(state="failed", reason=str(error))
            raise error
        finally:
            record["elapsedSeconds"] = time.monotonic() - started


def compare_reference_capture(engine, source, expected_sha256, dissector):
    validate_dissector(dissector)
    report = {
        "schemaVersion": 1,
        "passed": False,
        "state": "failed",
        "reason": None,
        "productDecoderAccepted": False,
        "dissector": dissector,
        "records": [],
        "comparedProtocols": 0,
        "comparisonExcludes": [
            "engine position attribute pos",
            "generated tree-path ids",
        ],
        "originPolicy": "engine data sources retained; original capture/log byte mapping unresolved",
        "harnessSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }
    try:
        capture = engine.root / "reference.pcap"
        report["reference"] = snapshot_capture(source, capture, expected_sha256)
        engine.remaining()
        report["capabilities"] = engine.capabilities()
        common = [
            engine.tshark,
            "-n",
            "-2",
            *engine.extension,
            "-r",
            capture,
            "-Y",
            dissector,
        ]
        raw_result = engine.command(
            common + ["-T", "jsonraw", "-j", dissector, "--no-duplicate-keys"]
        )
        raw_packets = parse_raw_packets(raw_result["stdout"], dissector)
        pdml_result = engine.command(common + ["-T", "pdml", "-J", dissector])
        tree_packets = normalize_pdml(pdml_result["stdout"])
        if len(raw_packets) != len(tree_packets):
            raise ProofError("reference_packet_count_mismatch")
        report["selectedPackets"] = len(raw_packets)
        for raw_occurrences, packet in zip(raw_packets, tree_packets):
            geninfo = [
                node
                for node in packet["children"]
                if node["attributes"].get("name") == "geninfo"
            ]
            number_fields = fields_named(geninfo, "num")
            if len(number_fields) != 1 or not re.fullmatch(
                r"[1-9][0-9]{0,19}", number_fields[0]["attributes"].get("show", "")
            ):
                raise ProofError("reference_frame_identity_missing")
            frame_number = int(number_fields[0]["attributes"]["show"])
            roots = [
                node
                for node in fields_named([packet], dissector)
                if node["kind"] == "proto"
            ]
            if len(roots) != len(raw_occurrences):
                raise ProofError("reference_protocol_count_mismatch")
            for ordinal, (raw, root) in enumerate(zip(raw_occurrences, roots)):
                engine.remaining()
                payload = raw["payload"]
                if root["attributes"].get("pos") != str(
                    raw["engineRawMetadata"][0]
                ) or root["attributes"].get("size") != str(len(payload)):
                    raise ProofError("reference_raw_tree_range_mismatch")
                expected = comparable_tree(root)
                record = {
                    "frameNumber": frame_number,
                    "protocolOccurrenceZeroBased": ordinal,
                    "inputBytes": len(payload),
                    "inputSha256": hashlib.sha256(payload).hexdigest(),
                    "engineRawMetadata": raw["engineRawMetadata"],
                    "engineDataSourceIndex": raw["engineDataSourceIndex"],
                    "originalCaptureByteOffset": None,
                    "fieldTreeEqual": False,
                    "referenceTreeSha256": tree_digest(expected),
                    "bridgeTreeSha256": None,
                }
                report["records"].append(record)
                decoded = engine.decode(payload, dissector)
                candidates = [
                    node
                    for node in fields_named(decoded["packets"], dissector)
                    if node["kind"] == "proto"
                ]
                if len(candidates) != 1:
                    record["reason"] = "bridge_protocol_count_mismatch"
                    continue
                actual = comparable_tree(candidates[0])
                record["bridgeTreeSha256"] = tree_digest(actual)
                record["fieldTreeEqual"] = expected == actual
                record["reason"] = (
                    None
                    if record["fieldTreeEqual"]
                    else "reference_field_tree_mismatch"
                )
                report["comparedProtocols"] += 1
                LOG.info(
                    "reference_pdu_compared frame=%d occurrence=%d bytes=%d equal=%s source_index=%s",
                    frame_number,
                    ordinal,
                    len(payload),
                    record["fieldTreeEqual"],
                    raw["engineDataSourceIndex"],
                )
        engine.remaining()
        report["passed"] = bool(report["records"]) and all(
            record["fieldTreeEqual"] for record in report["records"]
        )
        report["state"] = "qualified" if report["passed"] else "failed"
        report["reason"] = None if report["passed"] else "reference_field_tree_mismatch"
    except ProofError as error:
        report["reason"] = str(error)
        if error.cleanup_reason:
            report["secondaryReasons"] = [error.cleanup_reason]
        LOG.warning(
            "reference_proof_failed reason=%s compared=%d",
            error,
            report["comparedProtocols"],
        )
    report["commands"] = engine.commands
    report["elapsedSeconds"] = time.monotonic() - engine.started
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tshark", required=True, type=Path)
    parser.add_argument("--text2pcap", required=True, type=Path)
    parser.add_argument("--lua", action="append", default=[], type=Path)
    parser.add_argument("--capture", required=True, type=Path)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--dissector", required=True)
    parser.add_argument("--timeout-seconds", type=float, default=60.0)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    if argv is None:
        logging.basicConfig(
            level=logging.DEBUG, format="%(levelname)s %(name)s %(message)s"
        )
    report_path = args.output / "result.json"
    protected_inputs = [args.capture, args.tshark, args.text2pcap, *args.lua]
    if any(
        report_path.resolve() == path.resolve()
        or (report_path.exists() and path.exists() and report_path.samefile(path))
        for path in protected_inputs
    ):
        # A failure report at this location would destroy the input too.
        LOG.error("reference_proof_failed reason=invalid_reference_destination")
        return 2
    args.output.mkdir(parents=True, exist_ok=True)
    try:
        report_path.unlink(missing_ok=True)
        with tempfile.TemporaryDirectory(
            prefix="reference-", dir=args.output
        ) as directory:
            engine = ReferenceEngine(
                args.tshark, args.text2pcap, directory, args.lua, args.timeout_seconds
            )
            report = compare_reference_capture(
                engine, args.capture, args.sha256, args.dissector
            )
    except (ProofError, OSError) as error:
        report = {
            "schemaVersion": 1,
            "passed": False,
            "state": "failed",
            "productDecoderAccepted": False,
            "reason": str(error)
            if isinstance(error, ProofError)
            else "reference_io_error",
        }
    report_path.write_text(
        json.dumps(report, ensure_ascii=True, indent=2) + "\n", encoding="utf-8"
    )
    if report["passed"]:
        return 0
    return (
        2
        if report["reason"]
        in {
            "input_missing",
            "reference_hash_mismatch",
            "reference_input_limit",
            "invalid_reference_hash",
            "invalid_dissector",
            "invalid_limits",
        }
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
