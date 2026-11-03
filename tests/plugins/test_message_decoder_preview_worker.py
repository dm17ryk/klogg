"""Bounded preview transport and exact real-engine import contracts."""

import base64
import io
import json
import os
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/plugins"))
import message_decoder_wire as wire
import message_decoder_preview_worker as worker


def request(**changes):
    value = {
        "version": 1,
        "id": "one",
        "op": "evaluate",
        "profile": {
            "tshark": sys.executable,
            "text2pcap": sys.executable,
            "lua": [],
            "dissector": "data",
            "protocol": "",
        },
        "payloadBase64": "AQI=",
        "timeoutMs": 3000,
    }
    value.update(changes)
    return value


class WireTests(unittest.TestCase):
    def test_framing_roundtrip_and_clean_eof(self):
        stream = io.BytesIO()
        worker.write_frame(stream, request())
        stream.seek(0)
        self.assertEqual(worker.read_frame(stream), request())
        self.assertIsNone(worker.read_frame(stream))

    def test_bad_length_truncation_duplicate_keys_and_encoding(self):
        bodies = [b'{"id":1,"id":2}', b"\xff", b"NaN"]
        frames = [
            b"\0",
            struct.pack(">I", 262145),
            struct.pack(">I", 0),
            struct.pack(">I", 8) + b"{}",
        ]
        frames += [struct.pack(">I", len(body)) + body for body in bodies]
        for frame in frames:
            with self.subTest(frame=frame[:20]):
                with self.assertRaises(wire.ProofError):
                    worker.read_frame(io.BytesIO(frame))

    def test_request_rejects_unknown_types_noncanonical_payload_and_limits(self):
        invalid = [
            request(version=True),
            request(id=1),
            request(extra=1),
            request(timeoutMs=True),
            request(timeoutMs=10001),
            request(payloadBase64="AQI"),
            request(payloadBase64="AR=="),
            request(payloadBase64=base64.b64encode(b"x" * 65537).decode()),
            request(profile=dict(request()["profile"], extra="value")),
        ]
        for value in invalid:
            with self.subTest(value=list(value)):
                with self.assertRaises(wire.ProofError):
                    worker.validate_request(value)

    def test_exact_fields_repetitions_empty_attributes_and_synthetic_exclusion(self):
        packets = wire.normalize_pdml(
            b"""<pdml><packet><proto name="frame">
        <field name="frame.time" show="synthetic"/></proto><proto name="probe">
        <field name="probe.v" show="9007199254740993" value="00" pos="2" size="1"/>
        <field name="probe.v" show="" generated="true" pos="0" size="0"/>
        </proto></packet></pdml>""",
            node_limit=2048,
            depth_limit=32,
        )
        fields, limited = worker.render_fields(packets)
        self.assertFalse(limited)
        self.assertEqual(
            [field["columns"] for field in fields],
            [
                ["probe", "", "", "", ""],
                ["probe.v", "9007199254740993", "00", "2", "1"],
                ["probe.v", "", "", "0", "0"],
            ],
        )
        self.assertEqual(fields[2]["attributes"]["generated"], "true")
        self.assertEqual([field["depth"] for field in fields], [0, 1, 1])

    def test_pdml_limits_and_dtd_are_explicit_errors(self):
        for data, limits, reason in [
            (b"<!DOCTYPE pdml><pdml><packet/></pdml>", {}, "invalid_pdml"),
            (b"<pdml><packet/></pdml>", {"output_limit": 3}, "output_limit"),
            (b"<pdml><packet><field/></packet></pdml>", {"node_limit": 1}, "field_limit"),
            (b"<pdml><packet><field/></packet></pdml>", {"depth_limit": 0}, "field_limit"),
        ]:
            with self.subTest(reason=reason):
                with self.assertRaisesRegex(wire.ProofError, reason):
                    wire.normalize_pdml(data, **limits)

    def test_render_limit_is_reported(self):
        packets = wire.normalize_pdml(
            b"<pdml><packet>" + b'<field name="x"/>' * 513 + b"</packet></pdml>"
        )
        fields, limited = worker.render_fields(packets)
        self.assertEqual(len(fields), 512)
        self.assertTrue(limited)

    def test_blocked_pipe_write_has_deadline(self):
        class Stalled:
            def write(self, _):
                import time

                time.sleep(0.2)

            def flush(self):
                pass

        import time

        with self.assertRaisesRegex(wire.ProofError, "execution_timeout"):
            worker.write_before(Stalled(), b"x", time.monotonic() + 0.02)

    def test_bad_capture_headers_are_rejected(self):
        header = struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 262144, 252)
        valid = header + struct.pack("<IIII", 1, 2, 2, 2) + b"ab"
        self.assertEqual(worker.capture_record(valid)[1], struct.pack("<IIII", 0, 0, 2, 2) + b"ab")
        for data in [
            b"",
            valid[:-1],
            valid + b"extra",
            b"x" + valid[1:],
            header[:20] + struct.pack("<I", 1) + valid[24:],
        ]:
            with self.assertRaisesRegex(wire.ProofError, "invalid_capture"):
                worker.capture_record(data)


class ProcessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.owner = worker.ProcessOwner(inherit_group=True)
        self.children = []
        self.addCleanup(self.cleanup)

    def cleanup(self):
        self.owner.close()
        for child in self.children:
            child.close_streams()
            self.assertIsNotNone(child.process.returncode)

    def child(self, code):
        child = worker.EngineProcess(
            self.owner, [sys.executable, "-c", code], self.temp.name, os.environ.copy()
        )
        self.children.append(child)
        return child

    def test_read_stall_and_write_stall_are_bounded_and_reaped(self):
        child = self.child("import time; time.sleep(60)")
        with self.assertRaisesRegex(wire.ProofError, "execution_timeout"):
            child.packet(time.monotonic() + 0.1)
        with self.assertRaisesRegex(wire.ProofError, "execution_timeout"):
            worker.write_before(
                child.process.stdin, b"x" * worker.PDML_LIMIT, time.monotonic() + 0.1
            )

    def test_stdout_and_stderr_flood_are_bounded(self):
        for stream in ("stdout", "stderr"):
            with self.subTest(stream=stream):
                child = self.child(
                    "import sys,time; sys." + stream + '.buffer.write(b"x"*2000000); time.sleep(60)'
                )
                with self.assertRaisesRegex(wire.ProofError, "output_limit"):
                    child.packet(time.monotonic() + 5)
                self.assertLessEqual(len(child.buffers[0]), worker.PDML_LIMIT)
                self.assertLessEqual(len(child.buffers[1]), worker.STDERR_LIMIT)

    def test_lua_load_error_is_detected_even_with_exit_zero(self):
        child = self.child('import sys; print("Lua Error: private source",file=sys.stderr)')
        with self.assertRaisesRegex(wire.ProofError, "lua_failure"):
            child.finished(time.monotonic() + 5)

    def test_workspace_child_is_unique_and_does_not_remove_user_files(self):
        sentinel = Path(self.temp.name) / "keep.txt"
        sentinel.write_text("keep")
        session = worker.PreviewWorker(Path(self.temp.name))
        root = session.root
        self.assertNotEqual(root, Path(self.temp.name))
        session.close()
        self.assertFalse(root.exists())
        self.assertEqual(sentinel.read_text(), "keep")

    def test_unverified_plugin_folders_never_load_selected_lua(self):
        session = worker.PreviewWorker(Path(self.temp.name))
        self.addCleanup(session.close)
        with patch.object(session, "command", return_value=b"Personal Lua Plugins:\t/untrusted\n"):
            with self.assertRaisesRegex(wire.ProofError, "plugin_isolation_unverified"):
                session.start(request()["profile"], time.monotonic() + 5)
        self.assertIsNone(session.engine)


@unittest.skipUnless(
    os.environ.get("CILOGG_TSHARK") and os.environ.get("CILOGG_TEXT2PCAP"),
    "explicit real-engine paths were not supplied",
)
class NativePreviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.session = worker.PreviewWorker(Path(self.temp.name))
        self.addCleanup(self.session.close)
        self.profile = {
            "tshark": os.environ["CILOGG_TSHARK"],
            "text2pcap": os.environ["CILOGG_TEXT2PCAP"],
            "lua": [str(ROOT / "test_data/message_decoders/pdu_probe.lua")],
            "dissector": "cilogg_md01_probe",
            "protocol": "probe",
        }

    def evaluate(self, **changes):
        value = request(profile=self.profile, timeoutMs=10000, **changes)
        return self.session.evaluate(value)

    def test_two_requests_reuse_real_pid_and_exact_lua_tree(self):
        first = self.evaluate()
        second = self.evaluate(id="two", payloadBase64="AwQ=")
        self.assertTrue(first["available"], first)
        self.assertEqual(first["enginePid"], second["enginePid"])
        values = lambda result: [
            field["columns"][1]
            for field in result["fields"]
            if field["attributes"].get("name") == "cilogg_md01_probe.item"
        ]
        self.assertEqual(values(first), ["1", "2"])
        self.assertEqual(values(second), ["3", "4"])
        self.assertFalse(
            any(
                field["attributes"].get("name", "").startswith("frame") for field in first["fields"]
            )
        )

    def test_route_change_keeps_session_and_scripts_are_frozen(self):
        snapshot = Path(self.temp.name) / "probe.lua"
        snapshot.write_bytes(Path(self.profile["lua"][0]).read_bytes())
        self.profile["lua"] = [str(snapshot)]
        first = self.evaluate()
        snapshot.write_text('error("changed selected source must not run")')
        frozen = self.evaluate(id="frozen")
        self.assertEqual(first["enginePid"], frozen["enginePid"])
        self.profile["dissector"] = "data"
        self.profile["protocol"] = ""
        second = self.evaluate(id="two")
        self.assertEqual(first["enginePid"], second["enginePid"])
        self.assertEqual(len(self.session.owner.processes), 1)
        self.assertEqual(len(self.session.processes), 1)

    def test_isolated_interpreter_framed_stdio_two_responses(self):
        # Match deployed trusted resources, including a Unicode install path.
        resources = Path(self.temp.name) / "\u8d44\u6e90 \u05e9\u05dc\u05d5\u05dd"
        resources.mkdir()
        for relative in (
            "plugins/message_decoder_preview_worker.py",
            "plugins/message_decoder_wire.py",
            "benchmarks/workbench_process.py",
        ):
            original = ROOT / "scripts" / relative
            (resources / original.name).write_bytes(original.read_bytes())
        lua = resources / "\u05d1\u05d3\u05d9\u05e7\u05d4.lua"
        lua.write_bytes(Path(self.profile["lua"][0]).read_bytes())
        self.profile["lua"] = [str(lua)]
        values = [
            request(profile=self.profile, timeoutMs=10000),
            request(profile=self.profile, id="two", timeoutMs=10000),
        ]
        frames = b"".join(worker.encode_frame(value, worker.REQUEST_LIMIT) for value in values)
        result = subprocess.run(
            [
                sys.executable,
                "-I",
                "-S",
                "-u",
                str(resources / "message_decoder_preview_worker.py"),
                "--workspace",
                self.temp.name,
            ],
            input=frames,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        stream = io.BytesIO(result.stdout)
        first, second = worker.read_frame(stream), worker.read_frame(stream)
        self.assertEqual(first["enginePid"], second["enginePid"])
        self.assertTrue(first["available"])
        self.assertEqual(first["state"], "completed")
        self.assertIsNone(worker.read_frame(stream))
        self.assertNotIn(b"9007199254740993", result.stderr)

    def test_fatal_timeout_reaps_then_new_session_recovers(self):
        limited = request(profile=self.profile, timeoutMs=1)
        with self.assertRaisesRegex(wire.ProofError, "execution_timeout"):
            self.session.evaluate(limited)
        children = list(self.session.owner.processes)
        self.session.close()
        self.assertTrue(all(child.poll() is not None for child in children))
        # Native host starts a new channel after a fatal timeout.
        recovered = worker.PreviewWorker(Path(self.temp.name))
        self.addCleanup(recovered.close)
        result = recovered.evaluate(request(profile=self.profile, timeoutMs=10000))
        self.assertTrue(result["available"])

    def test_zero_exit_lua_exception_fails(self):
        script = Path(self.temp.name) / "bad.lua"
        script.write_text(
            'local p=Proto("broken_probe","Broken probe")\n'
            'function p.dissector(tvb,pinfo,tree) tree:add(p,tvb()); error("test") end\n'
        )
        self.profile["lua"] = [str(script)]
        self.profile["dissector"] = "broken_probe"
        with self.assertRaisesRegex(wire.ProofError, "lua_failure"):
            self.evaluate()


if __name__ == "__main__":
    unittest.main()
