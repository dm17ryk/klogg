"""MD01e: strict, opt-in real-engine byte/partial EHCI qualification."""

import importlib.util
import hashlib
import json
import os
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "test_data/message_decoders"
TOOLS_AVAILABLE = all(
    os.environ.get(key) for key in ("CILOGG_TSHARK", "CILOGG_TEXT2PCAP")
)


def internet_checksum(data):
    """Checksum synthetic IP/TCP fixtures; this is not an EHCI parser."""
    if len(data) % 2:
        data += b"\x00"
    total = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


class EngineCase(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location(
            "message_decoder_pdu", ROOT / "scripts/plugins/message_decoder_pdu.py"
        )
        self.proof = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.proof)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def engine(self, scripts):
        return self.proof.PduEngine(
            os.environ["CILOGG_TSHARK"],
            os.environ["CILOGG_TEXT2PCAP"],
            self.root,
            scripts,
        )

    def payload(self, name):
        return bytes.fromhex((FIXTURES / name).read_text(encoding="ascii"))

    def attributes(self, result, name, attribute="show"):
        return [
            field["attributes"].get(attribute)
            for field in self.proof.fields_named(result["packets"], name)
        ]


class RawOracleTests(EngineCase):
    def manifest(self, raw_expect):
        payload = b"x\x00y"
        (self.root / "data.hex").write_text(payload.hex(), encoding="ascii")
        case = {
            "id": "binary",
            "dissector": "ehci",
            "hex": "data.hex",
            "sha256": hashlib.sha256(payload).hexdigest(),
            "expect": {},
            "expectRaw": raw_expect,
        }
        path = self.root / "manifest.json"
        path.write_text(json.dumps({"cases": [case]}), encoding="utf-8")
        return path

    def report(self, expected, attributes):
        packets = [
            {
                "tag": "packet",
                "attributes": {},
                "children": [
                    {
                        "tag": "proto",
                        "attributes": {"name": "ehci"},
                        "children": [
                            {
                                "tag": "field",
                                "attributes": {"name": "ehci.binary", **attributes},
                                "children": [],
                            }
                        ],
                    }
                ],
            }
        ]

        class EngineDouble:
            def capabilities(self):
                return {"fixture": "oracle double, not real-engine evidence"}

            def decode(self, _payload, _dissector):
                return {"packets": packets}

        return self.proof.run_pdu_proof(EngineDouble(), self.manifest(expected))

    def test_raw_mismatch_fails_when_the_display_value_is_identical(self):
        report = self.report(
            {"ehci.binary": ["780079"]}, {"show": "x", "value": "780078"}
        )
        self.assertFalse(report["passed"])
        self.assertEqual(report["cases"][0]["reason"], "field_oracle_mismatch")
        self.assertEqual(report["cases"][0]["observedRaw"], {"ehci.binary": ["780078"]})

    def test_exact_raw_bytes_qualify_without_claiming_product_acceptance(self):
        report = self.report(
            {"ehci.binary": ["780079"]}, {"show": "x", "value": "780079"}
        )
        self.assertTrue(report["passed"])
        self.assertFalse(report["productDecoderAccepted"])
        self.assertEqual(report["cases"][0]["expectedRaw"], {"ehci.binary": ["780079"]})

    def test_an_absent_raw_attribute_does_not_become_a_matched_empty_value(self):
        report = self.report({"ehci.binary": [""]}, {"show": ""})
        self.assertFalse(report["passed"])

    def test_invalid_raw_oracles_are_rejected_before_engine_start(self):
        for invalid in (
            None,
            [],
            {"field": "00"},
            {"field": [0]},
            {"field": ["0"]},
            {"field": ["zz"]},
            {str(index): ["00"] for index in range(129)},
        ):
            with self.subTest(invalid=invalid):
                with self.assertRaisesRegex(self.proof.ProofError, "invalid_fixture"):
                    self.proof.load_manifest(self.manifest(invalid))


@unittest.skipUnless(
    TOOLS_AVAILABLE, "explicit real TShark/text2pcap paths are required"
)
class NativeByteApiTests(EngineCase):
    def test_raw_api_preserves_every_byte_including_nuls(self):
        payload = b"x\x00yCRC1234"
        result = self.engine([FIXTURES / "byte_api_probe.lua"]).decode(
            payload, "cilogg_byte_api_probe"
        )
        self.assertEqual(
            self.attributes(result, "cilogg_byte_api_probe.raw_length"),
            [str(len(payload))],
        )
        self.assertEqual(
            self.attributes(result, "cilogg_byte_api_probe.raw_hex"), [payload.hex()]
        )

    def test_exported_pdu_has_numeric_zero_desegmentation_capability(self):
        result = self.engine([FIXTURES / "byte_api_probe.lua"]).decode(
            b"known bytes", "cilogg_byte_api_probe"
        )
        self.assertEqual(
            self.attributes(result, "cilogg_byte_api_probe.can_type"), ["number"]
        )
        self.assertEqual(
            self.attributes(result, "cilogg_byte_api_probe.can_value"), ["0"]
        )


@unittest.skipUnless(
    TOOLS_AVAILABLE and os.environ.get("CILOGG_EHCI_CAPABILITIES_LUA"),
    "explicit EHCI capability-pack path is required; legacy characterization is separate",
)
class NativeEhciCapabilityTests(EngineCase):
    def decode(self, payload):
        return self.engine([Path(os.environ["CILOGG_EHCI_CAPABILITIES_LUA"])]).decode(
            payload, "ehci"
        )

    def test_partial_body_keeps_observed_tags_without_inventing_integrity(self):
        result = self.decode(self.payload("ehci-partial.hex"))
        self.assertEqual(self.attributes(result, "ehci.magic"), ["EHCI"])
        self.assertEqual(self.attributes(result, "ehci.tag.name"), ["CNT", "INF"])
        self.assertEqual(
            self.attributes(result, "ehci.map.key"), ["A", "A", "E", "N", "B"]
        )
        self.assertEqual(
            self.attributes(result, "ehci.map.value"), ["one", "two", "", "xy", "after"]
        )
        for name in ("marker", "received", "calculated", "valid"):
            self.assertEqual(self.attributes(result, "ehci.checksum." + name), [])
        self.assertTrue(self.attributes(result, "ehci.warning"))

    def test_nul_value_retains_exact_bytes_and_following_maps(self):
        result = self.decode(self.payload("ehci-nul.hex"))
        self.assertEqual(self.attributes(result, "ehci.magic"), ["EHCI"])
        self.assertEqual(
            self.attributes(result, "ehci.map.key"), ["A", "A", "E", "N", "B"]
        )
        self.assertEqual(
            self.attributes(result, "ehci.map.value_raw", "value"), ["780079"]
        )
        self.assertEqual(self.attributes(result, "ehci.map.value")[-1:], ["after"])
        self.assertEqual(self.attributes(result, "ehci.checksum.valid"), ["True"])

    def test_complete_and_corrupt_messages_keep_distinct_checksum_outcomes(self):
        for filename, expected in (
            ("ehci-complete.hex", "True"),
            ("ehci-bad-checksum.hex", "False"),
        ):
            with self.subTest(filename=filename):
                # Each workspace owns its engine script snapshot.
                self.root = self.root / filename
                result = self.decode(self.payload(filename))
                self.assertEqual(
                    self.attributes(result, "ehci.checksum.valid"), [expected]
                )

    def test_concatenated_messages_keep_separate_crc_boundaries(self):
        complete = self.payload("ehci-complete.hex")
        result = self.decode(complete + complete)
        self.assertEqual(self.attributes(result, "ehci.magic"), ["EHCI", "EHCI"])
        self.assertEqual(
            self.attributes(result, "ehci.checksum.valid"), ["True", "True"]
        )
        self.assertEqual(
            self.attributes(result, "ehci.tag.name"), ["CNT", "INF", "CNT", "INF"]
        )

    def test_complete_then_partial_preserves_only_observed_checksum(self):
        result = self.decode(
            self.payload("ehci-complete.hex") + self.payload("ehci-partial.hex")
        )
        self.assertEqual(self.attributes(result, "ehci.magic"), ["EHCI", "EHCI"])
        self.assertEqual(self.attributes(result, "ehci.checksum.valid"), ["True"])
        self.assertEqual(
            self.attributes(result, "ehci.tag.name"), ["CNT", "INF", "CNT", "INF"]
        )

    def test_truncated_checksum_is_not_a_bad_or_fabricated_checksum(self):
        result = self.decode(self.payload("ehci-complete.hex")[:-3])
        self.assertEqual(self.attributes(result, "ehci.magic"), ["EHCI"])
        self.assertEqual(self.attributes(result, "ehci.tag.name"), ["CNT", "INF"])
        self.assertEqual(self.attributes(result, "ehci.checksum.valid"), [])

    def test_strict_capability_manifest_covers_payload_crc_and_length_recovery(self):
        report = self.proof.run_pdu_proof(
            self.engine([Path(os.environ["CILOGG_EHCI_CAPABILITIES_LUA"])]),
            FIXTURES / "ehci-capabilities.json",
        )
        failures = [
            (case["id"], case["reason"])
            for case in report["cases"]
            if not case["passed"]
        ]
        self.assertTrue(report["passed"], failures)
        self.assertFalse(report["productDecoderAccepted"])
        self.assertEqual(len(report["cases"]), 9)

    def test_every_observed_prefix_has_a_bounded_tree_and_no_invented_checksum(self):
        complete = self.payload("ehci-complete.hex")
        engine = self.engine([Path(os.environ["CILOGG_EHCI_CAPABILITIES_LUA"])])
        for length in range(4, len(complete)):
            with self.subTest(prefix_bytes=length):
                result = engine.decode(complete[:length], "ehci")
                self.assertTrue(self.proof.fields_named(result["packets"], "ehci"))
                self.assertEqual(self.attributes(result, "ehci.checksum.valid"), [])
                self.assertEqual(result["inputBytes"], length)

    def test_real_tcp_segments_still_reassemble_before_decoding(self):
        """A genuine synthetic TCP capture is distinct from application-PDU wrapping."""
        payload = self.payload("ehci-complete.hex")
        source, destination = b"\x0a\x00\x00\x01", b"\x0a\x00\x00\x02"
        ethernet = b"\x00" * 12 + b"\x08\x00"
        capture = bytearray(struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))
        sequence = 1
        for ordinal, chunk in enumerate((payload[:35], payload[35:]), 1):
            tcp = struct.pack(
                "!HHIIBBHHH", 40000, 5666, sequence, 1, 0x50, 0x18, 65535, 0, 0
            )
            pseudo = (
                source + destination + struct.pack("!BBH", 0, 6, len(tcp) + len(chunk))
            )
            tcp = (
                tcp[:16]
                + struct.pack("!H", internet_checksum(pseudo + tcp + chunk))
                + tcp[18:]
            )
            ip = struct.pack(
                "!BBHHHBBH4s4s",
                0x45,
                0,
                40 + len(chunk),
                ordinal,
                0,
                64,
                6,
                0,
                source,
                destination,
            )
            ip = ip[:10] + struct.pack("!H", internet_checksum(ip)) + ip[12:]
            frame = ethernet + ip + tcp + chunk
            capture += struct.pack("<IIII", ordinal, 0, len(frame), len(frame)) + frame
            sequence += len(chunk)
        path = self.root / "segmented.pcap"
        path.write_bytes(capture)
        engine = self.engine([Path(os.environ["CILOGG_EHCI_CAPABILITIES_LUA"])])
        engine.capabilities()
        decoded = engine.command(
            [
                engine.tshark,
                "-n",
                *engine.extension,
                "-2",
                "-r",
                path,
                "-o",
                "tcp.desegment_tcp_streams:TRUE",
                "-d",
                "tcp.port==5666,ehci",
                "-T",
                "pdml",
            ]
        )
        packets = self.proof.normalize_pdml(decoded["stdout"])
        self.assertFalse(self.proof.fields_named(packets, "_ws.lua.error"))
        result = {"packets": packets}
        self.assertEqual(self.attributes(result, "ehci.magic"), ["EHCI"])
        self.assertEqual(self.attributes(result, "ehci.checksum.valid"), ["True"])
        self.assertEqual(self.attributes(result, "ehci.tag.name"), ["CNT", "INF"])


if __name__ == "__main__":
    unittest.main()
