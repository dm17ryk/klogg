"""Reference/Exported-PDU equivalence must keep order, multiplicity and honest origins."""

import hashlib
import importlib.util
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
UTILITY = ROOT / "scripts/plugins/message_decoder_reference.py"
FIXTURES = ROOT / "test_data/message_decoders"


def protocol_tree(position="42", value="שלום 😀"):
    return {
        "id": "0/4",
        "kind": "proto",
        "attributes": {"name": "proof", "pos": position, "size": "2"},
        "children": [
            {
                "id": "0/4/0",
                "kind": "field",
                "attributes": {
                    "name": "proof.text",
                    "show": value,
                    "pos": position,
                    "size": "0",
                },
                "children": [],
            }
        ],
    }


def write_udp_capture(path, port=45679):
    """Invented Ethernet/IPv4/UDP traffic, not copied protocol semantics."""
    data = bytearray(struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))
    for payload in (bytes.fromhex("01020304"), bytes.fromhex("00ff")):
        udp = struct.pack("!HHHH", port, port, len(payload) + 8, 0) + payload
        ip = bytearray(
            struct.pack(
                "!BBHHHBBH4s4s",
                0x45,
                0,
                20 + len(udp),
                0,
                0,
                64,
                17,
                0,
                bytes([192, 0, 2, 1]),
                bytes([192, 0, 2, 2]),
            )
        )
        checksum = sum(struct.unpack("!10H", ip))
        while checksum >> 16:
            checksum = (checksum & 65535) + (checksum >> 16)
        ip[10:12] = struct.pack("!H", (~checksum) & 65535)
        packet = bytes.fromhex("0200000000020200000000010800") + ip + udp
        data.extend(struct.pack("<IIII", 1700000000, 0, len(packet), len(packet)))
        data.extend(packet)
    path.write_bytes(data)


class ReferenceCase(unittest.TestCase):
    def setUp(self):
        self.assertTrue(UTILITY.is_file(), "MD01b reference comparison is missing")
        sys.path.insert(0, str(ROOT / "scripts/plugins"))
        spec = importlib.util.spec_from_file_location("reference_proof", UTILITY)
        self.proof = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.proof)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)


class ReferenceTests(ReferenceCase):
    def test_cli_report_cannot_replace_any_explicit_input(self):
        protected = self.root / "result.json"
        original = b"private input bytes"
        for option in ("--capture", "--lua", "--tshark", "--text2pcap"):
            with self.subTest(option=option):
                protected.write_bytes(original)
                arguments = {
                    "--tshark": sys.executable,
                    "--text2pcap": sys.executable,
                    "--capture": str(self.root / "absent.pcap"),
                    "--sha256": "0" * 64,
                    "--dissector": "proof",
                    "--output": str(self.root),
                }
                arguments[option] = str(protected)
                code = self.proof.main(
                    [part for pair in arguments.items() for part in pair]
                )
                self.assertEqual(protected.read_bytes(), original)
                self.assertEqual(code, 2)

    def test_oversized_pdml_frame_number_returns_structured_failure(self):
        source = self.root / "capture.pcap"
        source.write_bytes(b"capture")
        engine = self.proof.ReferenceEngine(
            sys.executable, sys.executable, self.root / "engine"
        )
        raw = self.raw(["0102", 0, 2, 0, 1])
        pdml = (
            '<pdml><packet><proto name="geninfo"><field name="num" show="'
            + "1" * 5000
            + '"/></proto><proto name="proof" pos="0" size="2"/></packet></pdml>'
        ).encode()
        with (
            patch.object(engine, "capabilities", return_value={}),
            patch.object(
                engine, "command", side_effect=[{"stdout": raw}, {"stdout": pdml}]
            ),
        ):
            report = self.proof.compare_reference_capture(
                engine, source, hashlib.sha256(source.read_bytes()).hexdigest(), "proof"
            )
        self.assertFalse(report["passed"])
        self.assertEqual(report["reason"], "reference_frame_identity_missing")

    def test_snapshot_requires_exact_supplied_digest_and_copies_bytes(self):
        source = self.root / "original.pcap"
        source.write_bytes(b"capture\x00bytes")
        target = self.root / "snapshot.pcap"
        result = self.proof.snapshot_capture(
            source,
            target,
            "736764fae1b4a653c5bd6115f2a06ec1fa81849d8fc22a9c8c5ecc31b5f2e1d7",
        )
        self.assertEqual(result["bytes"], 13)
        self.assertEqual(target.read_bytes(), b"capture\x00bytes")
        source.write_bytes(b"changed")
        self.assertEqual(target.read_bytes(), b"capture\x00bytes")

    def test_snapshot_hash_mismatch_is_not_decode_failure(self):
        source = self.root / "source.pcap"
        source.write_bytes(b"capture")
        with self.assertRaisesRegex(self.proof.ProofError, "reference_hash_mismatch"):
            self.proof.snapshot_capture(source, self.root / "copy.pcap", "0" * 64)

    def test_snapshot_missing_and_oversize_are_explicit(self):
        with self.assertRaisesRegex(self.proof.ProofError, "input_missing"):
            self.proof.snapshot_capture(
                self.root / "missing", self.root / "copy", "0" * 64
            )
        source = self.root / "large"
        with source.open("wb") as stream:
            stream.truncate(64 * 1024 * 1024 + 1)
        with self.assertRaisesRegex(self.proof.ProofError, "reference_input_limit"):
            self.proof.snapshot_capture(source, self.root / "copy", "0" * 64)

    def raw(self, value):
        return json.dumps([{"_source": {"layers": {"proof_raw": value}}}]).encode()

    def test_raw_occurrences_preserve_multiplicity_order_and_metadata(self):
        data = self.raw([["0102", 42, 2, 0, 1, 0], ["0304", 44, 2, 0, 1, 0]])
        packets = self.proof.parse_raw_packets(data, "proof")
        self.assertEqual([p["payload"] for p in packets[0]], [b"\x01\x02", b"\x03\x04"])
        self.assertEqual(packets[0][1]["engineRawMetadata"], [44, 2, 0, 1, 0])

    def test_raw_single_occurrence_and_old_shape_remain_labelled(self):
        packets = self.proof.parse_raw_packets(self.raw(["00ff", 0, 2, 0, 1]), "proof")
        self.assertEqual(packets[0][0]["payload"], b"\x00\xff")
        self.assertIsNone(packets[0][0]["engineDataSourceIndex"])

    def test_raw_duplicate_json_keys_cannot_silently_drop_an_occurrence(self):
        data = b'[{"_source":{"layers":{"proof_raw":["01",0,1,0,1],"proof_raw":["02",1,1,0,1]}}}]'
        with self.assertRaisesRegex(self.proof.ProofError, "duplicate_json_keys"):
            self.proof.parse_raw_packets(data, "proof")

    def test_raw_deep_json_and_oversized_integer_fail_structurally(self):
        for data in (
            b"[" * 1200 + b"0" + b"]" * 1200,
            b'[{"_source":{"layers":{"proof_raw":["00",' + b"1" * 1000 + b",1,0,1]}}}]",
        ):
            with self.subTest(bytes=len(data)):
                with self.assertRaisesRegex(
                    self.proof.ProofError, "invalid_raw_protocol"
                ):
                    self.proof.parse_raw_packets(data, "proof")

    def test_snapshot_cannot_overwrite_its_original(self):
        source = self.root / "original"
        source.write_bytes(b"capture")
        with self.assertRaisesRegex(
            self.proof.ProofError, "invalid_reference_destination"
        ):
            self.proof.snapshot_capture(source, source, "0" * 64)
        self.assertEqual(source.read_bytes(), b"capture")

    def test_raw_invalid_length_metadata_and_hex_are_rejected(self):
        for value in (
            ["00", 0, 2, 0, 1, 0],
            ["zz", 0, 1, 0, 1],
            ["00", True, 1, 0, 1],
            ["00", 0, 1],
        ):
            with self.subTest(value=value):
                with self.assertRaisesRegex(
                    self.proof.ProofError, "invalid_raw_protocol"
                ):
                    self.proof.parse_raw_packets(self.raw(value), "proof")

    def test_no_protocol_and_record_limit_are_never_empty_success(self):
        with self.assertRaisesRegex(self.proof.ProofError, "reference_no_protocol"):
            self.proof.parse_raw_packets(b"[]", "proof")
        with self.assertRaisesRegex(self.proof.ProofError, "reference_record_limit"):
            self.proof.parse_raw_packets(self.raw([["00", 0, 1, 0, 1]] * 33), "proof")

    def test_tree_projection_excludes_only_ids_and_engine_position(self):
        a = self.proof.comparable_tree(protocol_tree("42"))
        b = self.proof.comparable_tree(protocol_tree("12"))
        self.assertEqual(a, b)
        self.assertEqual(a["children"][0]["attributes"]["show"], "שלום 😀")
        self.assertNotIn("pos", a["attributes"])
        other = protocol_tree()
        other["children"][0]["attributes"]["size"] = "1"
        self.assertNotEqual(a, self.proof.comparable_tree(other))

    def test_tree_projection_preserves_empty_missing_and_child_order(self):
        a = protocol_tree(value="")
        b = protocol_tree(value="")
        del b["children"][0]["attributes"]["show"]
        self.assertNotEqual(
            self.proof.comparable_tree(a), self.proof.comparable_tree(b)
        )
        a["children"].append(protocol_tree(value="second"))
        b = dict(a, children=list(reversed(a["children"])))
        self.assertNotEqual(
            self.proof.comparable_tree(a), self.proof.comparable_tree(b)
        )

    def test_overall_deadline_applies_to_child_execution(self):
        engine = self.proof.ReferenceEngine(
            sys.executable, sys.executable, self.root, timeout=0.15
        )
        with self.assertRaisesRegex(self.proof.ProofError, "reference_timeout"):
            engine.command([sys.executable, "-c", "import time; time.sleep(60)"])
        with self.assertRaisesRegex(self.proof.ProofError, "reference_timeout"):
            engine.command([sys.executable, "-c", "print('late')"])

    def test_cli_invalid_capture_writes_failed_summary(self):
        code = self.proof.main(
            [
                "--tshark",
                sys.executable,
                "--text2pcap",
                sys.executable,
                "--capture",
                str(self.root / "missing.pcap"),
                "--sha256",
                "0" * 64,
                "--dissector",
                "proof",
                "--output",
                str(self.root / "out"),
            ]
        )
        self.assertEqual(code, 2)
        result = json.loads((self.root / "out/result.json").read_text())
        self.assertFalse(result["passed"])
        self.assertEqual(result["reason"], "input_missing")


@unittest.skipUnless(
    os.environ.get("CILOGG_TSHARK") and os.environ.get("CILOGG_TEXT2PCAP"),
    "real TShark tools were not explicitly supplied",
)
class NativeReferenceTests(ReferenceCase):
    def test_real_transport_and_exported_pdus_match_with_repeated_protocols(self):
        capture = self.root / "synthetic.pcap"
        write_udp_capture(capture)
        engine = self.proof.ReferenceEngine(
            os.environ["CILOGG_TSHARK"],
            os.environ["CILOGG_TEXT2PCAP"],
            self.root / "engine",
            [FIXTURES / "reference_probe.lua"],
        )
        result = self.proof.compare_reference_capture(
            engine,
            capture,
            hashlib.sha256(capture.read_bytes()).hexdigest(),
            "cilogg_md01_reference",
        )
        self.assertTrue(result["passed"])
        self.assertFalse(result["productDecoderAccepted"])
        self.assertEqual(
            [
                (x["frameNumber"], x["protocolOccurrenceZeroBased"])
                for x in result["records"]
            ],
            [(1, 0), (1, 1), (2, 0)],
        )
        self.assertEqual(result["comparedProtocols"], 3)
        self.assertTrue(all(x["fieldTreeEqual"] for x in result["records"]))
        self.assertTrue(
            all(x["originalCaptureByteOffset"] is None for x in result["records"])
        )
        serialized = json.dumps(result)
        self.assertNotIn("\\u05e9", serialized)
        self.assertNotIn('"packets"', serialized)

    def engine(self, directory="engine"):
        return self.proof.ReferenceEngine(
            os.environ["CILOGG_TSHARK"],
            os.environ["CILOGG_TEXT2PCAP"],
            self.root / directory,
            [FIXTURES / "reference_probe.lua"],
        )

    def test_real_unicode_and_transformed_value_are_exact_before_comparison(self):
        engine = self.engine()
        result = engine.decode(bytes.fromhex("0102"), "cilogg_md01_reference")
        text = self.proof.fields_named(result["packets"], "cilogg_md01_reference.text")
        self.assertEqual(text[0]["attributes"]["show"], "שלום 😀")
        transformed = self.proof.fields_named(
            result["packets"], "cilogg_md01_reference.transformed"
        )
        self.assertEqual(transformed[0]["attributes"]["value"], "4142")

    def test_carrier_dependent_field_is_a_real_mismatch(self):
        capture = self.root / "context.pcap"
        write_udp_capture(capture, port=45680)
        result = self.proof.compare_reference_capture(
            self.engine(),
            capture,
            hashlib.sha256(capture.read_bytes()).hexdigest(),
            "cilogg_md01_context",
        )
        self.assertFalse(result["passed"])
        self.assertEqual(result["reason"], "reference_field_tree_mismatch")
        self.assertEqual(result["comparedProtocols"], 2)
        self.assertTrue(
            all(not record["fieldTreeEqual"] for record in result["records"])
        )

    def test_deadline_failure_replaces_a_previous_qualified_report(self):
        capture = self.root / "capture.pcap"
        write_udp_capture(capture)
        output = self.root / "out"
        output.mkdir()
        (output / "result.json").write_text('{"passed":true}', encoding="utf-8")
        code = self.proof.main(
            [
                "--tshark",
                os.environ["CILOGG_TSHARK"],
                "--text2pcap",
                os.environ["CILOGG_TEXT2PCAP"],
                "--capture",
                str(capture),
                "--sha256",
                hashlib.sha256(capture.read_bytes()).hexdigest(),
                "--dissector",
                "cilogg_md01_reference",
                "--timeout-seconds",
                "0.000001",
                "--output",
                str(output),
            ]
        )
        self.assertEqual(code, 1)
        report = json.loads((output / "result.json").read_text())
        self.assertFalse(report["passed"])
        self.assertEqual(report["reason"], "reference_timeout")


if __name__ == "__main__":
    unittest.main()
