"""MD01 proof boundaries; real-engine cases are explicit, optional dependencies."""

import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
UTILITY = ROOT / "scripts/plugins/message_decoder_pdu.py"
FIXTURES = ROOT / "test_data/message_decoders"


class ProofCase(unittest.TestCase):
    def setUp(self):
        self.assertTrue(UTILITY.is_file(), "MD01 real-engine proof harness is missing")
        spec = importlib.util.spec_from_file_location("message_decoder_pdu", UTILITY)
        self.proof = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.proof)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def run_child(self, code, **limits):
        return self.proof.run_bounded(
            [sys.executable, "-c", code], cwd=self.root, env=os.environ.copy(), **limits
        )


class ProofTests(ProofCase):
    def test_process_preserves_both_streams_and_exit_failure(self):
        result = self.run_child(
            "import sys; print('out'); print('err',file=sys.stderr); sys.exit(7)"
        )
        self.assertEqual(result["returnCode"], 7)
        self.assertEqual(result["stdout"].strip(), b"out")
        self.assertEqual(result["stderr"].strip(), b"err")
        self.assertTrue(result["ownedProcessesReaped"])

    def test_stalled_process_is_reaped_at_deadline(self):
        with self.assertRaisesRegex(self.proof.ProofError, "execution_timeout"):
            self.run_child("import time; time.sleep(60)", timeout=0.15)

    def test_stdout_flood_is_limited(self):
        with self.assertRaisesRegex(self.proof.ProofError, "output_limit"):
            self.run_child(
                "import sys; sys.stdout.buffer.write(b'x'*1000000)", output_limit=1024
            )

    def test_stderr_flood_is_limited(self):
        with self.assertRaisesRegex(self.proof.ProofError, "output_limit"):
            self.run_child(
                "import sys; sys.stderr.buffer.write(b'x'*1000000)", output_limit=1024
            )

    def test_zero_budget_is_rejected_before_start(self):
        with self.assertRaisesRegex(self.proof.ProofError, "invalid_limits"):
            self.run_child("raise Exception('should not run')", timeout=0)

    def cleanup_failure(self):
        original = self.proof.ProcessOwner.close

        def failing_close(owner):
            original(owner)
            raise RuntimeError("owned_process_reap_failed")

        return patch.object(self.proof.ProcessOwner, "close", failing_close)

    def test_cleanup_failure_replaces_an_old_qualified_report(self):
        output = self.root / "report"
        output.mkdir()
        (output / "result.json").write_text(
            '{"passed":true,"state":"qualified"}', encoding="utf-8"
        )
        # A failed OS reap cannot safely be produced on demand; real cleanup runs
        # first, then the documented failure is injected at the ownership seam.
        with (
            self.cleanup_failure(),
            patch.object(
                self.proof.PduEngine,
                "capabilities",
                lambda _: self.run_child("print('actual child')"),
            ),
        ):
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
        report = json.loads((output / "result.json").read_text())
        self.assertFalse(report["passed"])
        self.assertEqual(report["reason"], "process_cleanup_failed")

    def test_timeout_remains_primary_when_cleanup_also_fails(self):
        with self.cleanup_failure():
            with self.assertRaises(self.proof.ProofError) as caught:
                self.run_child("import time; time.sleep(60)", timeout=0.15)
        self.assertEqual(str(caught.exception), "execution_timeout")
        self.assertEqual(caught.exception.cleanup_reason, "process_cleanup_failed")

    def test_missing_engine_is_an_input_error(self):
        with self.assertRaisesRegex(self.proof.ProofError, "input_missing"):
            self.proof.PduEngine(self.root / "absent", self.root / "absent2", self.root)

    def test_hexdump_preserves_nul_and_record_offsets(self):
        dump = self.proof.hexdump(bytes(range(18)))
        self.assertEqual(
            dump,
            "000000 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\n000010 10 11\n",
        )
        with self.assertRaisesRegex(self.proof.ProofError, "input_limit"):
            self.proof.hexdump(b"x" * (1024 * 1024 + 1))

    def test_pdml_preserves_repeated_empty_generated_and_nested_values(self):
        data = b"""<pdml><packet><proto name="test" pos="12" size="2">
        <field name="test.v" show="9007199254740993" pos="12" size="1"/>
        <field name="test.v" show="" pos="13" size="0" generated="true"/>
        <field name="test.tree"><field name="test.v" show="child" pos="0" size="1"/></field>
        </proto></packet></pdml>"""
        packets = self.proof.normalize_pdml(data)
        fields = self.proof.fields_named(packets, "test.v")
        self.assertEqual(
            [x["attributes"]["show"] for x in fields], ["9007199254740993", "", "child"]
        )
        self.assertEqual(fields[1]["attributes"]["generated"], "true")
        self.assertEqual(len({x["id"] for x in fields}), 3)
        self.assertEqual(fields[2]["positionOrigin"], "engine_data_source_unresolved")
        self.assertEqual(fields[0]["attributes"]["pos"], "12")

    def test_pdml_rejects_entities_malformed_and_excessive_depth(self):
        for data in (b'<!DOCTYPE pdml [<!ENTITY x "body">]><pdml/>', b"<broken>"):
            with self.subTest(data=data):
                with self.assertRaisesRegex(self.proof.ProofError, "invalid_pdml"):
                    self.proof.normalize_pdml(data)
        with self.assertRaisesRegex(self.proof.ProofError, "invalid_pdml"):
            self.proof.normalize_pdml(
                '<!DOCTYPE pdml [<!ENTITY x "body">]><pdml><packet/></pdml>'.encode(
                    "utf-16"
                )
            )
        with self.assertRaisesRegex(self.proof.ProofError, "field_limit"):
            self.proof.normalize_pdml(
                b"<pdml><packet>"
                + b"<field>" * 140
                + b"</field>" * 140
                + b"</packet></pdml>"
            )

    def test_pdml_empty_or_wrong_document_cannot_look_successful(self):
        for data in (b"<html/>", b"<pdml/>"):
            with self.subTest(data=data):
                with self.assertRaisesRegex(self.proof.ProofError, "invalid_pdml"):
                    self.proof.normalize_pdml(data)

    def test_dissector_name_is_validated_before_tool_execution(self):
        for name in ("", "-x", "ehcp\nother", "a" * 129):
            with self.subTest(name=name):
                with self.assertRaisesRegex(self.proof.ProofError, "invalid_dissector"):
                    self.proof.validate_dissector(name)

    def test_bridge_limit_includes_padded_dissector_name_before_launch(self):
        engine = self.proof.PduEngine(sys.executable, sys.executable, self.root)
        for name, size in (("data", 262133), ("a" * 128, 262009)):
            with self.subTest(name=name):
                with self.assertRaisesRegex(
                    self.proof.ProofError, "bridge_input_limit"
                ):
                    engine.decode(b"x" * size, name)

    def test_missing_fixture_is_not_a_decode_failure(self):
        with self.assertRaisesRegex(self.proof.ProofError, "input_missing"):
            self.proof.load_manifest(self.root / "absent.json")

    def test_script_count_is_bounded_before_copy_or_engine_start(self):
        with self.assertRaisesRegex(self.proof.ProofError, "input_limit"):
            self.proof.PduEngine(
                sys.executable, sys.executable, self.root, scripts=[UTILITY] * 9
            )

    def test_cli_missing_inputs_writes_structured_nonzero_result(self):
        code = self.proof.main(
            [
                "--tshark",
                str(self.root / "missing-tshark"),
                "--text2pcap",
                str(self.root / "missing-text2pcap"),
                "--manifest",
                str(FIXTURES / "probe.json"),
                "--output",
                str(self.root / "report"),
            ]
        )
        self.assertEqual(code, 2)
        report = json.loads((self.root / "report/result.json").read_text())
        self.assertFalse(report["passed"])
        self.assertEqual(report["reason"], "input_missing")

    def test_manifest_rejects_escape_and_altered_payload(self):
        (self.root / "bytes.hex").write_text("0001", encoding="ascii")
        item = {
            "id": "one",
            "dissector": "data",
            "hex": "../bytes.hex",
            "sha256": "0" * 64,
            "expect": {},
        }
        path = self.root / "fixtures.json"
        path.write_text(json.dumps({"cases": [item]}), encoding="utf-8")
        with self.assertRaisesRegex(self.proof.ProofError, "invalid_fixture"):
            self.proof.load_manifest(path)
        item["hex"] = "bytes.hex"
        path.write_text(json.dumps({"cases": [item]}), encoding="utf-8")
        with self.assertRaisesRegex(self.proof.ProofError, "fixture_hash_mismatch"):
            self.proof.load_manifest(path)


@unittest.skipUnless(
    os.environ.get("CILOGG_TSHARK") and os.environ.get("CILOGG_TEXT2PCAP"),
    "real TShark/text2pcap paths were not explicitly supplied",
)
class NativePduTests(ProofCase):
    def setUp(self):
        super().setUp()
        self.engine = self.proof.PduEngine(
            os.environ["CILOGG_TSHARK"],
            os.environ["CILOGG_TEXT2PCAP"],
            self.root,
            scripts=[FIXTURES / "pdu_probe.lua"],
        )

    def test_actual_named_dispatch_preserves_repeated_and_transformed_fields(self):
        result = self.engine.decode(bytes.fromhex("0102"), "cilogg_md01_probe")
        fields = self.proof.fields_named(result["packets"], "cilogg_md01_probe.item")
        self.assertEqual([f["attributes"]["show"] for f in fields], ["1", "2"])
        generated = self.proof.fields_named(
            result["packets"], "cilogg_md01_probe.generated"
        )
        self.assertEqual(generated[0]["attributes"]["show"], "9007199254740993")
        transformed = self.proof.fields_named(
            result["packets"], "cilogg_md01_probe.transformed"
        )
        self.assertEqual(transformed[0]["attributes"]["value"], "4142")
        self.assertEqual(
            transformed[0]["positionOrigin"], "engine_data_source_unresolved"
        )
        self.assertEqual(result["inputBytes"], 2)
        self.assertTrue(all(x["ownedProcessesReaped"] for x in result["commands"]))

    def test_actual_unknown_dissector_never_proves_dispatch(self):
        result = self.engine.decode(b"hello", "cilogg_missing_dissector")
        self.assertEqual(
            self.proof.fields_named(result["packets"], "cilogg_missing_dissector"), []
        )

    def test_actual_largest_data_pdu_remains_one_exported_frame(self):
        result = self.engine.decode(b"a" * 262132, "data")
        self.assertEqual(len(result["packets"]), 1)
        self.assertEqual(result["inputBytes"], 262132)
        frame = self.proof.fields_named(result["packets"], "frame.len")
        self.assertEqual(frame[0]["attributes"]["show"], "262144")

    def test_actual_tools_have_versions_and_isolated_lua_folders(self):
        capabilities = self.engine.capabilities()
        self.assertIn("TShark", capabilities["tsharkVersion"])
        self.assertIn("Text2pcap", capabilities["text2pcapVersion"])
        self.assertTrue(capabilities["luaAvailable"])
        self.assertTrue(capabilities["luaFoldersIsolated"])

    def test_actual_report_requires_dispatch_and_checks_exact_occurrences(self):
        self.assertTrue(
            hasattr(self.proof, "run_pdu_proof"),
            "proof report orchestration is missing",
        )
        report = self.proof.run_pdu_proof(self.engine, FIXTURES / "probe.json")
        self.assertTrue(report["passed"])
        self.assertEqual(report["state"], "qualified")
        self.assertFalse(report["productDecoderAccepted"])
        case = report["cases"][0]
        self.assertEqual(case["id"], "named-probe")
        self.assertEqual(case["observed"]["cilogg_md01_probe.item"], ["1", "2"])

    def test_actual_nonmatching_oracle_cannot_pass(self):
        self.assertTrue(
            hasattr(self.proof, "run_pdu_proof"),
            "proof report orchestration is missing",
        )
        manifest = json.loads((FIXTURES / "probe.json").read_text())
        manifest["cases"][0]["expect"]["cilogg_md01_probe.item"] = ["2", "1"]
        (self.root / "probe.hex").write_text("0102", encoding="ascii")
        path = self.root / "bad.json"
        path.write_text(json.dumps(manifest), encoding="utf-8")
        report = self.proof.run_pdu_proof(self.engine, path)
        self.assertFalse(report["passed"])
        self.assertEqual(report["cases"][0]["reason"], "field_oracle_mismatch")

    def test_actual_report_storage_exhaustion_stays_failed(self):
        report = self.proof.run_pdu_proof(
            self.engine, FIXTURES / "probe.json", report_limit=1
        )
        self.assertFalse(report["passed"])
        self.assertEqual(report["cases"][0]["reason"], "report_storage_limit")

    def test_lua_exception_cannot_be_success_with_zero_process_exit(self):
        script = self.root / "broken.lua"
        script.write_text(
            'local p=Proto("broken_probe","Broken probe")\n'
            'function p.dissector(tvb,pinfo,tree) tree:add(p,tvb()); error("deliberate probe failure") end\n',
            encoding="utf-8",
        )
        engine = self.proof.PduEngine(
            os.environ["CILOGG_TSHARK"],
            os.environ["CILOGG_TEXT2PCAP"],
            self.root / "broken",
            [script],
        )
        with self.assertRaisesRegex(self.proof.ProofError, "lua_failure"):
            engine.decode(b"ab", "broken_probe")

    def test_script_is_pinned_before_a_later_source_edit(self):
        script = self.root / "copy.lua"
        script.write_bytes((FIXTURES / "pdu_probe.lua").read_bytes())
        engine = self.proof.PduEngine(
            os.environ["CILOGG_TSHARK"],
            os.environ["CILOGG_TEXT2PCAP"],
            self.root / "snapshot",
            [script],
        )
        script.write_text('error("must not run changed source")', encoding="utf-8")
        result = engine.decode(bytes.fromhex("0102"), "cilogg_md01_probe")
        self.assertEqual(
            len(self.proof.fields_named(result["packets"], "cilogg_md01_probe.item")), 2
        )


@unittest.skipUnless(
    all(
        os.environ.get(name)
        for name in (
            "CILOGG_TSHARK",
            "CILOGG_TEXT2PCAP",
            "CILOGG_EHCP_LUA",
            "CILOGG_EHCI_LUA",
        )
    ),
    "explicit private EHCP/EHCI packs were not supplied",
)
class PackPduTests(ProofCase):
    def engine(self):
        return self.proof.PduEngine(
            os.environ["CILOGG_TSHARK"],
            os.environ["CILOGG_TEXT2PCAP"],
            self.root,
            [
                os.environ["CILOGG_EHCP_LUA"],
                os.environ["CILOGG_EHCI_LUA"],
                FIXTURES / "pdu_probe.lua",
            ],
        )

    def test_real_packs_complete_corrupt_and_partial_results_are_distinguished(self):
        report = self.proof.run_pdu_proof(self.engine(), FIXTURES / "packs.json")
        self.assertFalse(report["passed"])
        self.assertFalse(report["productDecoderAccepted"])
        self.assertEqual(
            [case["passed"] for case in report["cases"]], [True, True, True, False]
        )
        self.assertEqual(report["cases"][3]["reason"], "dissector_not_dispatched")
        self.assertEqual(report["cases"][3]["observed"]["ehci.magic"], [])

    def test_real_pack_nul_entrypoint_gap_stays_failed(self):
        report = self.proof.run_pdu_proof(self.engine(), FIXTURES / "binary-input.json")
        self.assertFalse(report["passed"])
        self.assertEqual(report["cases"][0]["reason"], "dissector_not_dispatched")

    def test_synthetic_nested_wrapper_calls_actual_ehcp_dissector(self):
        report = self.proof.run_pdu_proof(self.engine(), FIXTURES / "nested.json")
        self.assertTrue(report["passed"])
        self.assertEqual(report["cases"][0]["observed"]["ehcp.message_counter"], ["9"])


if __name__ == "__main__":
    unittest.main()
