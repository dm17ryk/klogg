"""MD01c proof of Exported-PDU field origins; not a product decoder or log mapper."""

import argparse
import hashlib
import json
import logging
import re
import tempfile
import time
from pathlib import Path

from message_decoder_pdu import (
    BRIDGE_FRAME_LIMIT,
    INPUT_LIMIT,
    OUTPUT_LIMIT,
    ProofError,
    fields_named,
    load_manifest,
    validate_dissector,
)
from message_decoder_reference import ReferenceEngine

LOG = logging.getLogger("cilogg.decoder.origins")
FIELD_LIMIT = 10000


def resolve_origins(data, payload, dissector, *, report_limit=OUTPUT_LIMIT):
    """Bind raw JSON positions to one verified bridge packet, never to a log file.

    JSON grouping is its own identity space. It cannot recover PDML sibling order
    or generated flags, so neither is inferred here. No display/raw body escapes.
    """
    validate_dissector(dissector)
    if type(report_limit) is not int or not 0 < report_limit <= OUTPUT_LIMIT:
        raise ProofError("invalid_limits")
    if type(payload) is not bytes or not 0 < len(payload) <= BRIDGE_FRAME_LIMIT:
        raise ProofError("bridge_input_limit")
    if len(data) > OUTPUT_LIMIT:
        raise ProofError("output_limit")

    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ProofError("duplicate_json_keys")
            result[key] = value
        return result

    def integer(value):
        if len(value.lstrip("-")) > 20:
            raise ValueError("integer limit")
        return int(value)

    def no_float(value):
        raise ValueError("non-integer metadata")

    def metadata(value):
        if (
            not isinstance(value, list)
            or len(value) not in (5, 6)
            or not isinstance(value[0], str)
            or not re.fullmatch(r"[0-9a-fA-F]*", value[0])
            or not all(
                type(item) is int and 0 <= item <= 2**64 - 1 for item in value[1:5]
            )
            or (
                len(value) == 6
                and value[5] is not None
                and (type(value[5]) is not int or not 0 <= value[5] <= 2**64 - 1)
            )
        ):
            raise ProofError("invalid_origin_metadata")
        encoded, offset, size, mask, kind = value[:5]
        # Bitmasked JSON values are numbers formatted as hex, not raw byte strings.
        if mask == 0 and (len(encoded) % 2 or len(encoded) // 2 != size):
            raise ProofError("invalid_origin_metadata")
        return encoded, offset, size, mask, kind, value[5] if len(value) == 6 else None

    try:
        document = json.loads(
            data.decode("utf-8"),
            object_pairs_hook=unique_object,
            parse_int=integer,
            parse_float=no_float,
            parse_constant=no_float,
        )
        if not isinstance(document, list) or len(document) != 1:
            raise ProofError("bridge_packet_count")
        layers = document[0]["_source"]["layers"]
        frame = metadata(layers["frame_raw"])
        exported = metadata(layers["exported_pdu"]["exported_pdu.exported_pdu_raw"])
        protocol = metadata(layers[dissector + "_raw"])
        raw, base, size, mask, _, source = exported
        if (
            frame[1] != 0
            or frame[3] != 0
            or frame[5] != 0
            or mask != 0
            or source != 0
            or bytes.fromhex(raw) != payload
            or base + size != frame[2]
            or bytes.fromhex(frame[0])[base : base + size] != payload
            or protocol[0] != raw
            or protocol[1:4] != exported[1:4]
            or protocol[5] != 0
        ):
            raise ProofError("bridge_origin_unverified")
        tree = layers[dissector]
        if not isinstance(tree, dict):
            raise ProofError("bridge_origin_unverified")
        fields, visited, retained_bytes = [], 0, 0

        def append_field(name, value, path):
            nonlocal retained_bytes
            encoded, offset, length, bitmask, kind, data_source = metadata(value)
            span = None
            if data_source is None:
                reason = "data_source_unavailable"
            elif data_source != 0:
                reason = "transformed_data_source"
            elif length == 0:
                reason = "no_occupied_bytes"
            elif bitmask:
                reason = "bitmasked_field"
            elif offset < base or offset + length > base + size:
                reason = "outside_payload"
            elif (
                bytes.fromhex(encoded)
                != payload[offset - base : offset - base + length]
            ):
                reason = "raw_bytes_mismatch"
            else:
                reason = "verified_payload_bytes"
                span = {"offsetZeroBased": offset - base, "lengthBytes": length}
            record = {
                "name": name,
                "rawPath": path,
                "engineOffsetZeroBased": offset,
                "engineLengthBytes": length,
                "engineDataSourceIndex": data_source,
                "engineFieldType": kind,
                "engineBitmask": bitmask,
                "generated": None,
                "generatedReason": "not_exposed_by_raw_json",
                "payloadByteSpan": span,
                "originalSourceSpan": None,
                "mappingReason": reason,
            }
            retained_bytes += len(json.dumps(record, ensure_ascii=True).encode("ascii"))
            if retained_bytes > report_limit:
                raise ProofError("origin_report_limit")
            fields.append(record)
            if len(fields) > FIELD_LIMIT:
                raise ProofError("origin_field_limit")
            LOG.debug(
                "origin_field name=%s path=%s source_index=%s offset=%d length=%d reason=%s",
                name,
                path,
                data_source,
                offset,
                length,
                reason,
            )

        def walk(node, path, depth):
            nonlocal visited
            visited += 1
            if visited > 50000 or depth > 128:
                raise ProofError("origin_field_limit")
            if isinstance(node, dict):
                for ordinal, (key, value) in enumerate(node.items()):
                    if key.endswith("_raw"):
                        # One metadata tuple versus a --no-duplicate-keys list.
                        if not isinstance(value, list) or not value:
                            raise ProofError("invalid_origin_metadata")
                        occurrences = [value] if isinstance(value[0], str) else value
                        for index, occurrence in enumerate(occurrences):
                            append_field(key[:-4], occurrence, path + [ordinal, index])
                    elif isinstance(value, (dict, list)):
                        # Native dissectors also use payload text as subtree keys.
                        # Ordinals identify structure without retaining display labels.
                        walk(value, path + [ordinal], depth + 1)
            elif isinstance(node, list):
                for index, value in enumerate(node):
                    walk(value, path + [index], depth + 1)

        walk(tree, [], 0)
        result = {
            "inputBytes": len(payload),
            "inputSha256": hashlib.sha256(payload).hexdigest(),
            "dissector": dissector,
            "payloadFrameOffsetZeroBased": base,
            "offsetUnit": "bytes",
            "offsetBase": "zero_based",
            "identitySpace": "jsonraw_grouped_ordinal_paths",
            "fields": fields,
            "mappedFields": sum(
                field["payloadByteSpan"] is not None for field in fields
            ),
            "originalSourceSpan": None,
        }
        if len(json.dumps(result, ensure_ascii=True).encode("ascii")) > report_limit:
            raise ProofError("origin_report_limit")
        LOG.info(
            "origin_packet_bound bytes=%d payload_offset=%d fields=%d mapped=%d",
            len(payload),
            base,
            len(fields),
            result["mappedFields"],
        )
        return result
    except (KeyError, TypeError, ValueError, UnicodeError, RecursionError) as error:
        raise ProofError("invalid_origin_metadata") from error


class OriginEngine(ReferenceEngine):
    def inspect(self, payload, dissector, expected=None):
        # Reuse named dispatch, PDML Lua-error checks, input caps and owned cleanup.
        decoded = self.decode(payload, dissector)
        if expected is not None and any(
            [
                field["attributes"].get("show")
                for field in fields_named(decoded["packets"], name)
            ]
            != values
            for name, values in expected.items()
        ):
            raise ProofError("field_oracle_mismatch")
        raw = self.command(
            [
                self.tshark,
                "-n",
                *self.extension,
                "-r",
                self.root / "input.pcapng",
                "-T",
                "jsonraw",
                "--no-duplicate-keys",
            ]
        )
        result = resolve_origins(raw["stdout"], payload, dissector)
        self.remaining()
        return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tshark", required=True, type=Path)
    parser.add_argument("--text2pcap", required=True, type=Path)
    parser.add_argument("--lua", action="append", default=[], type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--timeout-seconds", type=float, default=60.0)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    if argv is None:
        logging.basicConfig(
            level=logging.DEBUG, format="%(levelname)s %(name)s %(message)s"
        )
    destination = args.output / "result.json"
    inputs = [args.manifest, args.tshark, args.text2pcap, *args.lua]
    # Discover declared payload paths before deleting/writing any old report.
    # Even invalid hex is still an input which this command must preserve.
    try:
        with args.manifest.open("rb") as source:
            encoded = source.read(INPUT_LIMIT + 1)
        if len(encoded) > INPUT_LIMIT:
            raise ValueError("manifest size")
        manifest = json.loads(encoded.decode("utf-8"))
        if not isinstance(manifest, dict) or not isinstance(
            manifest.get("cases"), list
        ):
            raise TypeError("manifest structure")
        for case in manifest["cases"]:
            if (
                not isinstance(case, dict)
                or not isinstance(case.get("hex"), str)
                or "\x00" in case["hex"]
            ):
                raise ValueError("fixture path")
            inputs.append(args.manifest.parent / case["hex"])
    except FileNotFoundError:
        # There are no declared existing payloads. Emit input_missing below.
        pass
    except (OSError, TypeError, ValueError, UnicodeError, RecursionError):
        # Input discovery is incomplete: the destination could itself be a payload.
        # Preserve it, even if it looks like an old report. Exit status is authoritative.
        LOG.error(
            "origin_proof_failed reason=unsafe_manifest_discovery report_written=false"
        )
        return 2
    if any(
        destination.resolve() == path.resolve()
        or (destination.exists() and path.exists() and destination.samefile(path))
        for path in inputs
    ):
        LOG.error("origin_proof_failed reason=invalid_origin_destination")
        return 2
    report = {
        "schemaVersion": 1,
        "passed": False,
        "state": "failed",
        "productDecoderAccepted": False,
        "md01Acceptance": "pending",
        "cases": [],
    }
    engine = None
    try:
        args.output.mkdir(parents=True, exist_ok=True)
        destination.unlink(missing_ok=True)
        cases = load_manifest(args.manifest)
        report["manifestSha256"] = cases[0]["manifestSha256"]
        report["harnessSha256"] = hashlib.sha256(
            Path(__file__).read_bytes()
        ).hexdigest()
        with tempfile.TemporaryDirectory(
            prefix="origins-", dir=args.output
        ) as directory:
            engine = OriginEngine(
                args.tshark, args.text2pcap, directory, args.lua, args.timeout_seconds
            )
            report["capabilities"] = engine.capabilities()
            for case in cases:
                result = engine.inspect(
                    case["payload"], case["dissector"], case["expect"]
                )
                result["id"] = case["id"]
                report["cases"].append(result)
                if (
                    len(json.dumps(report, ensure_ascii=True).encode("ascii"))
                    > OUTPUT_LIMIT
                ):
                    report["cases"].pop()
                    raise ProofError("origin_report_limit")
            engine.remaining()
            report["elapsedSeconds"] = time.monotonic() - engine.started
        report.update(passed=True, state="qualified", reason=None)
    except (ProofError, OSError) as error:
        reason = str(error) if isinstance(error, ProofError) else "origin_io_error"
        report["reason"] = reason
        if isinstance(error, ProofError) and error.cleanup_reason:
            report["secondaryReasons"] = [error.cleanup_reason]
        LOG.warning(
            "origin_proof_failed reason=%s retained_cases=%d",
            reason,
            len(report["cases"]),
        )
    if engine is not None:
        report["commands"] = engine.commands
    serialized = json.dumps(report, ensure_ascii=True, separators=(",", ":")) + "\n"
    if len(serialized.encode("ascii")) > OUTPUT_LIMIT:
        # Retention is all-or-explicit-failure, including command/capability metadata.
        report = {
            "schemaVersion": 1,
            "passed": False,
            "state": "failed",
            "productDecoderAccepted": False,
            "md01Acceptance": "pending",
            "reason": "origin_report_limit",
            "cases": [],
        }
        serialized = json.dumps(report) + "\n"
    try:
        destination.write_bytes(serialized.encode("ascii"))
    except OSError:
        LOG.error(
            "origin_proof_failed reason=origin_report_io_error report_written=false"
        )
        return 1
    if report["passed"]:
        return 0
    return (
        2
        if report["reason"]
        in {
            "input_missing",
            "input_limit",
            "invalid_fixture",
            "fixture_hash_mismatch",
            "invalid_dissector",
            "invalid_limits",
        }
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
