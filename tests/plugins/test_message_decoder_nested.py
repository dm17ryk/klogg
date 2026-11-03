"""Real-engine SRING wrapper checks keep envelope and child outcomes separate."""

import os
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "test_data/message_decoders"
sys.path.insert(0, str(ROOT / "scripts/plugins"))
from message_decoder_pdu import PduEngine, fields_named, run_pdu_proof


@unittest.skipUnless(
    os.environ.get("CILOGG_TSHARK") and os.environ.get("CILOGG_TEXT2PCAP"),
    "real TShark tools were not explicitly supplied",
)
class NativeNestedTests(unittest.TestCase):
    def setUp(self):
        self.script = FIXTURES / "sring_probe.lua"
        self.assertTrue(
            self.script.is_file(), "Strict nested wrapper proof fixture is missing"
        )
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.engine = PduEngine(
            os.environ["CILOGG_TSHARK"],
            os.environ["CILOGG_TEXT2PCAP"],
            self.root / "engine",
            [self.script],
        )

    def values(self, result, name):
        return [
            node["attributes"].get("show")
            for node in fields_named(result["packets"], name)
        ]

    def test_exact_hex_child_bytes_and_parent_offsets_are_observed_in_real_engine(self):
        result = self.engine.decode(b"SRING: 1,4,00010203", "cilogg_md01_nested_data")
        prefix = "cilogg_md01_nested_data."
        self.assertEqual(self.values(result, prefix + "status"), ["valid"])
        self.assertEqual(self.values(result, prefix + "child_status"), ["invoked"])
        self.assertEqual(self.values(result, prefix + "declared_bytes"), ["4"])
        self.assertEqual(self.values(result, prefix + "actual_bytes"), ["4"])
        self.assertEqual(self.values(result, prefix + "hex_offset"), ["11"])
        self.assertEqual(
            [
                node["attributes"].get("value")
                for node in fields_named(result["packets"], "data.data")
            ],
            ["00010203"],
        )

    def test_committed_manifest_reproduces_the_strict_engine_proof(self):
        report = run_pdu_proof(self.engine, FIXTURES / "nested-strict.json")
        self.assertTrue(report["passed"])
        self.assertFalse(report["productDecoderAccepted"])
        self.assertEqual(report["cases"][0]["id"], "strict-sring-data")
        self.assertEqual(
            report["cases"][0]["observed"]["cilogg_md01_nested_data.status"], ["valid"]
        )

    def test_invalid_wrappers_never_dispatch_or_repair_a_child(self):
        for body, status in [
            (b"SRING: 1,2,0102F", "odd_hex"),
            (b"SRING: 1,3,0102", "length_mismatch"),
            (b"SRING: 1,1,0102", "length_mismatch"),
            (b"SRING: 1,0,", "empty_payload"),
            (b"SRING: 999999,2,0102", "socket_limit"),
            (b"SRING: 1,65537,0102", "declared_limit"),
            (b"SRING: 1,2,01 02", "invalid_envelope"),
            (b"SRING: 1,2,0102\r\n", "invalid_envelope"),
            (b"SRING: 1,2,0102\x00", "invalid_envelope"),
            (b"SRING: 1,2,0102SRING: 1,2,0304", "invalid_envelope"),
            (b"STRING: 1,2,0102", "invalid_envelope"),
        ]:
            with self.subTest(status=status, body=body):
                result = self.engine.decode(body, "cilogg_md01_nested_data")
                prefix = "cilogg_md01_nested_data."
                self.assertEqual(self.values(result, prefix + "status"), [status])
                self.assertEqual(
                    self.values(result, prefix + "child_status"), ["not_invoked"]
                )
                self.assertEqual(fields_named(result["packets"], "data.data"), [])

    def test_missing_named_child_does_not_fall_back_or_invalidate_the_envelope(self):
        result = self.engine.decode(b"SRING: 1,2,0102", "cilogg_md01_nested_missing")
        prefix = "cilogg_md01_nested_missing."
        self.assertEqual(self.values(result, prefix + "status"), ["valid"])
        self.assertEqual(self.values(result, prefix + "child_status"), ["missing"])
        self.assertEqual(fields_named(result["packets"], "data.data"), [])

    def test_repeated_calls_do_not_reuse_an_earlier_child(self):
        good = self.engine.decode(b"SRING: 1,2,0102", "cilogg_md01_nested_data")
        self.assertEqual(len(fields_named(good["packets"], "data.data")), 1)
        bad = self.engine.decode(b"SRING: 1,3,0102", "cilogg_md01_nested_data")
        self.assertEqual(fields_named(bad["packets"], "data.data"), [])
        good = self.engine.decode(b"SRING:\t1, 2,\tff00", "cilogg_md01_nested_data")
        self.assertEqual(
            [
                node["attributes"].get("value")
                for node in fields_named(good["packets"], "data.data")
            ],
            ["ff00"],
        )

    def test_wrapper_bound_is_checked_before_child_conversion(self):
        payload = bytes(range(256)) * 256
        valid = self.engine.decode(
            b"SRING: 1,65536," + payload.hex().encode("ascii"),
            "cilogg_md01_nested_data",
        )
        self.assertEqual(
            self.values(valid, "cilogg_md01_nested_data.status"), ["valid"]
        )
        self.assertEqual(
            self.values(valid, "cilogg_md01_nested_data.actual_bytes"), ["65536"]
        )
        self.assertEqual(
            [
                node["attributes"].get("value")
                for node in fields_named(valid["packets"], "data.data")
            ],
            [payload.hex()],
        )
        body = b"SRING: 1,65536," + b"00" * 65536 + b" " * 64
        result = self.engine.decode(body, "cilogg_md01_nested_data")
        self.assertEqual(
            self.values(result, "cilogg_md01_nested_data.status"), ["wrapper_limit"]
        )
        self.assertEqual(fields_named(result["packets"], "data.data"), [])

    @unittest.skipUnless(
        os.environ.get("CILOGG_EHCP_LUA"), "external EHCP pack not supplied"
    )
    def test_real_ehcp_crc_outcome_does_not_change_a_valid_wrapper(self):
        engine = PduEngine(
            os.environ["CILOGG_TSHARK"],
            os.environ["CILOGG_TEXT2PCAP"],
            self.root / "ehcp",
            [Path(os.environ["CILOGG_EHCP_LUA"]), self.script],
        )
        child = bytes.fromhex(
            (FIXTURES / "ehcp-complete.hex").read_text(encoding="ascii")
        )
        for payload, valid in [(child, "True"), (child[:-4] + b"0000", "False")]:
            with self.subTest(checksumValid=valid):
                wrapper = b"SRING: 1,48," + payload.hex().encode("ascii")
                result = engine.decode(wrapper, "cilogg_md01_nested_ehcp")
                self.assertEqual(
                    self.values(result, "cilogg_md01_nested_ehcp.status"), ["valid"]
                )
                self.assertEqual(
                    self.values(result, "cilogg_md01_nested_ehcp.child_status"),
                    ["invoked"],
                )
                self.assertEqual(self.values(result, "ehcp.message_counter"), ["9"])
                self.assertEqual(self.values(result, "ehcp.checksum.valid"), [valid])


if __name__ == "__main__":
    unittest.main()
