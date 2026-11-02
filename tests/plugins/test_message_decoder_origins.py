"""Field origins must never turn an engine data source into a log-file offset."""

import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
UTILITY = ROOT / "scripts/plugins/message_decoder_origins.py"
FIXTURES = ROOT / "test_data/message_decoders"


def raw_document(fields=None):
    # Invented four-byte bridge header; offsets are independently hand checked.
    return [
        {
            "_source": {
                "layers": {
                    "frame_raw": ["000000000102", 0, 6, 0, 1, 0],
                    "exported_pdu": {
                        "exported_pdu.exported_pdu_raw": ["0102", 4, 2, 0, 30, 0]
                    },
                    "proof_raw": ["0102", 4, 2, 0, 1, 0],
                    "proof": fields or {"proof.byte_raw": ["01", 4, 1, 0, 4, 0]},
                }
            }
        }
    ]


class OriginCase(unittest.TestCase):
    def setUp(self):
        self.assertTrue(UTILITY.is_file(), "MD01c field-origin proof is missing")
        sys.path.insert(0, str(ROOT / "scripts/plugins"))
        spec = importlib.util.spec_from_file_location("origin_proof", UTILITY)
        self.proof = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.proof)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def parse(self, document):
        return self.proof.resolve_origins(
            json.dumps(document).encode(), b"\x01\x02", "proof"
        )


class OriginTests(OriginCase):
    def test_deep_manifest_is_rejected_without_traceback_or_report_mutation(self):
        manifest = self.root / "deep.json"
        manifest.write_bytes(b"[" * 100000 + b"0" + b"]" * 100000)
        report = self.root / "result.json"
        report.write_bytes(b"keep")
        code = self.proof.main(
            [
                "--tshark",
                sys.executable,
                "--text2pcap",
                sys.executable,
                "--manifest",
                str(manifest),
                "--output",
                str(self.root),
            ]
        )
        self.assertEqual(code, 2)
        self.assertEqual(report.read_bytes(), b"keep")

    def test_symlink_manifest_resolves_fixtures_beside_its_target(self):
        target = self.root / "target"
        target.mkdir()
        payload = target / "result.json"
        payload.write_bytes(b"0102")
        manifest = target / "manifest.json"
        manifest.write_text(
            json.dumps(
                {
                    "cases": [
                        {
                            "id": "alias",
                            "hex": "result.json",
                            "dissector": "proof",
                            "sha256": "a12871fee210fb8619291eaea194581cbd2531e4b23759d225f6806923f63222",
                            "expect": {},
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        link = self.root / "link.json"
        try:
            link.symlink_to(manifest)
        except OSError as error:
            self.skipTest(str(error))
        code = self.proof.main(
            [
                "--tshark",
                sys.executable,
                "--text2pcap",
                sys.executable,
                "--manifest",
                str(link),
                "--output",
                str(target),
            ]
        )
        self.assertEqual(code, 2)
        self.assertEqual(payload.read_bytes(), b"0102")

    def test_bridge_input_limit_is_an_input_error_and_replaces_old_success(self):
        import hashlib

        payload = b"a" * (262144 + 1)
        (self.root / "large.hex").write_text(payload.hex(), encoding="ascii")
        manifest = self.root / "large.json"
        manifest.write_text(
            json.dumps(
                {
                    "cases": [
                        {
                            "id": "oversized",
                            "hex": "large.hex",
                            "dissector": "proof",
                            "sha256": hashlib.sha256(payload).hexdigest(),
                            "expect": {},
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        output = self.root / "output"
        output.mkdir()
        (output / "result.json").write_bytes(b'{"passed":true}')
        with patch.object(self.proof.OriginEngine, "capabilities", return_value={}):
            code = self.proof.main(
                [
                    "--tshark",
                    sys.executable,
                    "--text2pcap",
                    sys.executable,
                    "--manifest",
                    str(manifest),
                    "--output",
                    str(output),
                ]
            )
        self.assertEqual(code, 2)
        report = json.loads((output / "result.json").read_text(encoding="utf-8"))
        self.assertEqual(report["reason"], "bridge_input_limit")
        self.assertFalse(report["passed"])

    def test_incomplete_manifest_discovery_cannot_overwrite_an_existing_destination(
        self,
    ):
        payload = self.root / "result.json"
        manifest = self.root / "manifest.json"
        oversized = json.dumps(
            {"cases": [{"hex": "result.json"}], "padding": "x" * (1024 * 1024)}
        ).encode()
        for encoded in [
            oversized,
            b'{"cases":[{"hex":"result.json"}],"padding":',
            b'{"cases":[{"hex":"bad\\u0000path"}]}',
        ]:
            with self.subTest(manifestBytes=len(encoded)):
                payload.write_bytes(b"0102")
                manifest.write_bytes(encoded)
                code = self.proof.main(
                    [
                        "--tshark",
                        sys.executable,
                        "--text2pcap",
                        sys.executable,
                        "--manifest",
                        str(manifest),
                        "--output",
                        str(self.root),
                    ]
                )
                self.assertEqual(code, 2)
                self.assertEqual(payload.read_bytes(), b"0102")

    def test_unwritable_report_destination_returns_failure_without_traceback(self):
        output = self.root / "regular-file"
        output.write_bytes(b"keep")
        code = self.proof.main(
            [
                "--tshark",
                sys.executable,
                "--text2pcap",
                sys.executable,
                "--manifest",
                str(FIXTURES / "probe.json"),
                "--output",
                str(output),
            ]
        )
        self.assertEqual(code, 1)
        self.assertEqual(output.read_bytes(), b"keep")

    def test_direct_ranges_are_payload_relative_and_not_file_offsets(self):
        result = self.parse(
            raw_document(
                {"proof.byte_raw": [["01", 4, 1, 0, 4, 0], ["02", 5, 1, 0, 4, 0]]}
            )
        )
        self.assertEqual(
            [x["payloadByteSpan"] for x in result["fields"]],
            [
                {"offsetZeroBased": 0, "lengthBytes": 1},
                {"offsetZeroBased": 1, "lengthBytes": 1},
            ],
        )
        self.assertNotEqual(
            result["fields"][0]["rawPath"], result["fields"][1]["rawPath"]
        )
        self.assertTrue(all(x["originalSourceSpan"] is None for x in result["fields"]))
        self.assertEqual(result["payloadFrameOffsetZeroBased"], 4)

    def test_transformed_equal_bytes_do_not_map_by_content_or_pdml_position(self):
        field = self.parse(
            raw_document({"proof.transformed_raw": ["0102", 0, 2, 0, 30, 1]})
        )["fields"][0]
        self.assertIsNone(field["payloadByteSpan"])
        self.assertEqual(field["mappingReason"], "transformed_data_source")

    def test_zero_size_is_not_invented_generated_flag_or_occupied_span(self):
        field = self.parse(raw_document({"proof.empty_raw": ["", 4, 0, 0, 26, 0]}))[
            "fields"
        ][0]
        self.assertIsNone(field["generated"])
        self.assertIsNone(field["payloadByteSpan"])
        self.assertEqual(field["mappingReason"], "no_occupied_bytes")

    def test_missing_data_source_stays_unknown(self):
        for metadata in [["01", 4, 1, 0, 4], ["", 0, 0, 0, 1, None]]:
            with self.subTest(metadata=metadata):
                field = self.parse(raw_document({"proof.old_raw": metadata}))["fields"][
                    0
                ]
                self.assertIsNone(field["payloadByteSpan"])
                self.assertEqual(field["mappingReason"], "data_source_unavailable")

    def test_outside_payload_byte_mismatch_and_bitmask_stay_unmapped(self):
        for metadata, reason in [
            (["00", 0, 1, 0, 4, 0], "outside_payload"),
            (["ff", 4, 1, 0, 4, 0], "raw_bytes_mismatch"),
            (["1", 4, 1, 1, 4, 0], "bitmasked_field"),
        ]:
            with self.subTest(reason=reason):
                field = self.parse(raw_document({"proof.field_raw": metadata}))[
                    "fields"
                ][0]
                self.assertIsNone(field["payloadByteSpan"])
                self.assertEqual(field["mappingReason"], reason)

    def test_bridge_binding_rejects_other_packet_wrong_payload_and_unknown_source(self):
        for mutate in [
            lambda layers: layers["frame_raw"].__setitem__(0, "00000000ff02"),
            lambda layers: layers["exported_pdu"][
                "exported_pdu.exported_pdu_raw"
            ].__setitem__(0, "ff02"),
            lambda layers: layers["proof_raw"].__setitem__(5, 1),
            lambda layers: layers["exported_pdu"][
                "exported_pdu.exported_pdu_raw"
            ].pop(),
        ]:
            document = raw_document()
            mutate(document[0]["_source"]["layers"])
            with self.assertRaisesRegex(
                self.proof.ProofError, "bridge_origin_unverified"
            ):
                self.parse(document)
        with self.assertRaisesRegex(self.proof.ProofError, "bridge_packet_count"):
            self.parse(raw_document() * 2)

    def test_duplicate_keys_nonfinite_integer_and_invalid_metadata_fail_closed(self):
        valid = json.dumps(raw_document())
        # Everything else is valid; later structural validation cannot mask a missing guard.
        for extra, reason in [
            ('"unused":0,"unused":1,', "duplicate_json_keys"),
            ('"unused":NaN,', "invalid_origin_metadata"),
            ('"unused":Infinity,', "invalid_origin_metadata"),
            ('"unused":1e999,', "invalid_origin_metadata"),
            ('"unused":123456789012345678901,', "invalid_origin_metadata"),
        ]:
            data = valid.replace("[{", "[{" + extra, 1).encode()
            with (
                self.subTest(extra=extra),
                self.assertRaisesRegex(self.proof.ProofError, reason),
            ):
                self.proof.resolve_origins(data, b"\x01\x02", "proof")
        for metadata in [
            ["01", True, 1, 0, 4, 0],
            ["01", -1, 1, 0, 4, 0],
            ["01", 4, 2, 0, 4, 0],
            ["zz", 4, 1, 0, 4, 0],
        ]:
            with (
                self.subTest(metadata=metadata),
                self.assertRaisesRegex(
                    self.proof.ProofError, "invalid_origin_metadata"
                ),
            ):
                self.parse(raw_document({"proof.bad_raw": metadata}))

    def test_field_depth_output_and_record_limits_fail_instead_of_sampling(self):
        nested = {"proof.byte_raw": ["01", 4, 1, 0, 4, 0]}
        for _ in range(130):
            nested = {"nested": nested}
        with self.assertRaisesRegex(self.proof.ProofError, "origin_field_limit"):
            self.parse(raw_document(nested))
        with self.assertRaisesRegex(self.proof.ProofError, "origin_field_limit"):
            self.parse(
                raw_document({"proof.byte_raw": [["01", 4, 1, 0, 4, 0]] * 10001})
            )
        with self.assertRaisesRegex(self.proof.ProofError, "output_limit"):
            self.proof.resolve_origins(b" " * (16 * 1024 * 1024 + 1), b"a", "proof")

    def test_repeated_long_paths_stop_at_retention_budget(self):
        document = raw_document(
            {"nested" * 50: {"proof.byte_raw": [["01", 4, 1, 0, 4, 0]] * 50}}
        )
        with self.assertRaisesRegex(self.proof.ProofError, "origin_report_limit"):
            self.proof.resolve_origins(
                json.dumps(document).encode(), b"\x01\x02", "proof", report_limit=2048
            )

    def test_metadata_report_excludes_body_and_display_values(self):
        document = raw_document()
        document[0]["_source"]["layers"]["proof"]["proof.display"] = "PRIVATE BODY"
        result = self.parse(document)
        serialized = json.dumps(result)
        self.assertNotIn("PRIVATE BODY", serialized)
        self.assertNotIn("000000000102", serialized)
        self.assertNotIn('"rawHex"', serialized)

    def test_payload_derived_subtree_labels_are_excluded_from_results_and_logs(self):
        document = raw_document(
            {"PRIVATE BODY subtree": {"proof.byte_raw": ["01", 4, 1, 0, 4, 0]}}
        )
        with self.assertLogs(self.proof.LOG, level="DEBUG") as logs:
            result = self.parse(document)
        self.assertNotIn("PRIVATE BODY", json.dumps(result))
        self.assertNotIn("PRIVATE BODY", "\n".join(logs.output))

    def test_cli_rejected_input_admission_preserves_existing_destination(self):
        report = self.root / "result.json"
        report.write_bytes(b"private manifest")
        arguments = [
            "--tshark",
            sys.executable,
            "--text2pcap",
            sys.executable,
            "--manifest",
            str(report),
            "--output",
            str(self.root),
        ]
        self.assertEqual(self.proof.main(arguments), 2)
        self.assertEqual(report.read_bytes(), b"private manifest")
        arguments[arguments.index("--manifest") + 1] = str(self.root / "absent.json")
        self.assertEqual(self.proof.main(arguments), 2)
        self.assertEqual(report.read_bytes(), b"private manifest")

    def test_cli_cannot_overwrite_a_fixture_payload_even_when_invalid_hex(self):
        payload = self.root / "result.json"
        payload.write_bytes(b"not valid hex")
        manifest = self.root / "manifest.json"
        manifest.write_text(
            json.dumps(
                {
                    "cases": [
                        {
                            "id": "private",
                            "dissector": "proof",
                            "hex": "result.json",
                            "sha256": "0" * 64,
                            "expect": {},
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )
        code = self.proof.main(
            [
                "--tshark",
                sys.executable,
                "--text2pcap",
                sys.executable,
                "--manifest",
                str(manifest),
                "--output",
                str(self.root),
            ]
        )
        self.assertEqual(code, 2)
        self.assertEqual(payload.read_bytes(), b"not valid hex")


@unittest.skipUnless(
    os.environ.get("CILOGG_TSHARK") and os.environ.get("CILOGG_TEXT2PCAP"),
    "real TShark tools were not explicitly supplied",
)
class NativeOriginTests(OriginCase):
    def test_real_native_dns_labels_do_not_disclose_payload_in_metadata_or_logs(self):
        engine = self.proof.OriginEngine(
            os.environ["CILOGG_TSHARK"],
            os.environ["CILOGG_TEXT2PCAP"],
            self.root / "dns",
        )
        payload = bytes.fromhex(
            "1234010000010000000000000b50524956415445424f4459"
            "076578616d706c6503636f6d0000010001"
        )
        with self.assertLogs(self.proof.LOG, level="DEBUG") as logs:
            result = engine.inspect(payload, "dns")
        self.assertGreater(result["mappedFields"], 0)
        self.assertNotIn("PRIVATEBODY", json.dumps(result))
        self.assertNotIn("PRIVATEBODY", "\n".join(logs.output))

    def engine(self):
        return self.proof.OriginEngine(
            os.environ["CILOGG_TSHARK"],
            os.environ["CILOGG_TEXT2PCAP"],
            self.root / "engine",
            [FIXTURES / "pdu_probe.lua"],
        )

    def test_real_repeated_generated_and_transformed_sources(self):
        result = self.engine().inspect(b"\x01\x02", "cilogg_md01_probe")
        fields = result["fields"]
        items = [x for x in fields if x["name"] == "cilogg_md01_probe.item"]
        self.assertEqual(
            [x["payloadByteSpan"] for x in items],
            [
                {"offsetZeroBased": 0, "lengthBytes": 1},
                {"offsetZeroBased": 1, "lengthBytes": 1},
            ],
        )
        generated = next(x for x in fields if x["name"].endswith(".generated"))
        transformed = next(x for x in fields if x["name"].endswith(".transformed"))
        self.assertEqual(generated["mappingReason"], "no_occupied_bytes")
        self.assertIsNone(generated["generated"])
        self.assertEqual(transformed["mappingReason"], "transformed_data_source")
        # Identical transformed and original bytes still have separate origins.
        same = self.engine().inspect(b"AB", "cilogg_md01_probe")
        self.assertIsNone(
            next(x for x in same["fields"] if x["name"].endswith(".transformed"))[
                "payloadByteSpan"
            ]
        )

    def test_real_cli_returns_summary_and_expired_deadline_cannot_reuse_success(self):
        args = [
            "--tshark",
            os.environ["CILOGG_TSHARK"],
            "--text2pcap",
            os.environ["CILOGG_TEXT2PCAP"],
            "--lua",
            str(FIXTURES / "pdu_probe.lua"),
            "--manifest",
            str(FIXTURES / "probe.json"),
            "--output",
            str(self.root),
        ]
        self.assertEqual(self.proof.main(args), 0)
        report = json.loads((self.root / "result.json").read_text(encoding="utf-8"))
        self.assertEqual(report["cases"][0]["mappedFields"], 2)
        self.assertNotIn('"packets"', json.dumps(report))
        self.assertEqual(self.proof.main(args + ["--timeout-seconds", "0.000001"]), 1)
        report = json.loads((self.root / "result.json").read_text(encoding="utf-8"))
        self.assertFalse(report["passed"])
        self.assertEqual(report["reason"], "reference_timeout")

    def test_real_manifest_value_oracle_is_enforced_before_origins_qualify(self):
        manifest = self.root / "wrong.json"
        fixture = json.loads((FIXTURES / "probe.json").read_text(encoding="utf-8"))
        (self.root / "probe.hex").write_bytes((FIXTURES / "probe.hex").read_bytes())
        fixture["cases"][0]["expect"]["cilogg_md01_probe.item"] = ["2", "1"]
        manifest.write_text(json.dumps(fixture), encoding="utf-8")
        code = self.proof.main(
            [
                "--tshark",
                os.environ["CILOGG_TSHARK"],
                "--text2pcap",
                os.environ["CILOGG_TEXT2PCAP"],
                "--lua",
                str(FIXTURES / "pdu_probe.lua"),
                "--manifest",
                str(manifest),
                "--output",
                str(self.root / "output"),
            ]
        )
        self.assertEqual(code, 1)
        report = json.loads(
            (self.root / "output/result.json").read_text(encoding="utf-8")
        )
        self.assertEqual(report["reason"], "field_oracle_mismatch")


if __name__ == "__main__":
    unittest.main()
