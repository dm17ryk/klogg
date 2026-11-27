"""Bounded preview transport and exact real-engine import contracts."""

import base64
import copy
import io
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import threading
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

    def test_rows_show_the_dissectors_display_text(self):
        # Issue #181: Value is the display text (showname after its "label: "), Raw keeps the
        # raw value, and text-only items are labelled by their text.
        packets = wire.normalize_pdml(
            """<pdml><packet><proto name="probe" showname="Probe, Version: 1" size="5" pos="0">
        <field name="_ws.lua.text" showname="Header: two bytes" size="2" pos="0" show="" value="">
        <field name="probe.status" showname="Status: Battery OK (0)" size="1" pos="0" show="0" value="00"/>
        <field name="probe.model" showname="Model: 13 (Synthetic model 13)" size="1" pos="1" show="13" value="0d"/>
        </field>
        <field name="probe.flag" showname=".... ...1 = Flag: True" size="1" pos="1" show="True" value="1"/>
        <field name="probe.name" showname="Name […]: abc" size="3" pos="2" show="abcdef" value="616263"/>
        <field name="probe.more" showname="More: Battery OK (0) […]" size="1" pos="2" show="0" value="00"/>
        <field name="probe.generated" showname="Generated: Ready (7)" size="0" pos="0" show="7"/>
        <field name="probe.count" showname="Count: 3" size="0" pos="0" show="3"/>
        <field name="probe.long" showname="Long: abcde…" size="10" pos="0" show="abcdefghij" value="6162636465666768696a"/>
        <field show="Plain legacy text" size="1" pos="4" value="00"/>
        <field show="Bare legacy text" size="0" pos="4"/>
        </proto></packet></pdml>""".encode("utf-8"),
            node_limit=2048,
            depth_limit=32,
        )
        fields, limited = worker.render_fields(packets)
        self.assertFalse(limited)
        self.assertEqual(
            [field["columns"] for field in fields],
            [
                # A protocol keeps its whole display text.
                ["probe", "Probe, Version: 1", "", "0", "5"],
                ["Header: two bytes", "", "", "0", "2"],
                ["probe.status", "Battery OK (0)", "00", "0", "1"],
                ["probe.model", "13 (Synthetic model 13)", "0d", "1", "1"],
                ["probe.flag", "True", "1", "1", "1"],
                # A label Wireshark cut off keeps the whole raw text.
                ["probe.name", "abcdef", "616263", "2", "3"],
                # The same marker in a value (appended text) is part of the display text.
                ["probe.more", "Battery OK (0) […]", "00", "2", "1"],
                # Without raw bytes, Raw keeps the raw show value, also when Value reads the same.
                ["probe.generated", "Ready (7)", "7", "0", "0"],
                ["probe.count", "3", "3", "0", "0"],
                # A value Wireshark cut off ("…" after a prefix of show) shows the whole show.
                ["probe.long", "abcdefghij", "6162636465666768696a", "0", "10"],
                ["Plain legacy text", "", "00", "4", "1"],
                # A text-only item's text is its name; Raw holds raw bytes only.
                ["Bare legacy text", "", "", "4", "0"],
            ],
        )
        self.assertEqual(fields[1]["attributes"]["name"], "_ws.lua.text")
        self.assertEqual([field["depth"] for field in fields], [0, 1, 2, 2] + [1] * 8)

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

    def test_pdml_prefix_keeps_the_first_items_in_preorder(self):
        # Issue #198: a tree past the item limit keeps its first items instead of failing.
        data = (b'<pdml><packet><proto name="p"><field name="a"><field name="b"/></field>'
                b'<field name="c"/><field name="d"/></proto></packet></pdml>')
        whole, cut = wire.normalize_pdml_prefix(data, node_limit=6, depth_limit=32)
        self.assertFalse(cut)
        self.assertEqual(whole, wire.normalize_pdml(data))
        packets, cut = wire.normalize_pdml_prefix(data, node_limit=4, depth_limit=32)
        self.assertTrue(cut)
        names = lambda node: [node["attributes"].get("name")] + [
            name for child in node["children"] for name in names(child)
        ]
        self.assertEqual(names(packets[0]), [None, "p", "a", "b"])
        self.assertEqual(packets[0]["children"][0]["children"][0]["children"][0]["id"], "0/0/0/0")

    def test_pdml_prefix_cut_at_a_byte_limit_drops_its_incomplete_end(self):
        data = '<pdml><packet><proto name="p"><field name="a" show="é"/><field name="b" show="é"/>'
        data = data.encode("utf-8")
        # Each prefix keeps the items whose start tags it holds whole, and no more.
        starts = [data.index(tag) + len(tag) for tag in
                  (b'<proto name="p">', b'show="\xc3\xa9"/>', b'<field name="b" show="\xc3\xa9"/>')]
        for end in range(len(b"<pdml><packet>"), len(data) + 1):
            with self.subTest(end=end):
                packets, cut = wire.normalize_pdml_prefix(
                    data[:end], node_limit=8192, depth_limit=32, whole=False
                )
                self.assertFalse(cut)
                self.assertEqual(len(packets), 1)
                self.assertEqual(worker.count_rows(packets),
                                 sum(1 for start in starts if start <= end))
        packets, _ = wire.normalize_pdml_prefix(data, node_limit=8192, depth_limit=32, whole=False)
        self.assertEqual([c["attributes"]["show"] for c in packets[0]["children"][0]["children"]],
                         ["é", "é"])

    def test_pdml_prefix_errors(self):
        for data, changes, reason in [
            (b"<!DOCTYPE pdml><pdml><packet/></pdml>", {}, "invalid_pdml"),
            (b"<other><packet/></other>", {}, "invalid_pdml"),
            (b"<pdml></pdml>", {}, "invalid_pdml"),
            (b"<pdml><packet>", {}, "invalid_pdml"),  # A whole document must be complete.
            (b"<pdml><packet><a></b></packet></pdml>", {"whole": False}, "invalid_pdml"),
            # Invalid UTF-8 is a PDML error, whole or cut (review of #204).
            (b'<pdml><packet><f a="\xff"/></packet></pdml>', {}, "invalid_pdml"),
            (b'<pdml><packet><f a="\xff"/></packet></pdml>', {"whole": False}, "invalid_pdml"),
            (b"<pdml><packet><field><field/></field></packet></pdml>", {"depth_limit": 1},
             "depth_limit"),
        ]:
            with self.subTest(data=data):
                limits = {"node_limit": 8192, "depth_limit": 32}
                limits.update(changes)
                with self.assertRaisesRegex(wire.ProofError, reason):
                    wire.normalize_pdml_prefix(data, **limits)

    def test_host_mirrors_the_workers_limits(self):
        # Review of #192 (#198): the host's copies of the worker's limits must not drift.
        header = (ROOT / "src/ui/include/tsharkpreviewclient.h").read_text(encoding="utf-8")
        expected = {
            "FieldLimit": worker.FIELD_LIMIT,
            "PdmlItemLimit": worker.PDML_NODE_LIMIT,
            "PdmlByteLimit": worker.PDML_LIMIT,
            "PdmlDepthLimit": worker.PDML_DEPTH_LIMIT,
            "DiagnosticByteLimit": worker.STDERR_LIMIT,
            "ResultLimitMin": worker.RESPONSE_LIMIT_MIN,
            "ResultLimitDefault": worker.RESPONSE_LIMIT,
            "ResultLimitMax": worker.RESPONSE_LIMIT_MAX,
        }
        for name, value in expected.items():
            with self.subTest(name=name):
                match = re.search(r"static constexpr int %s = ([0-9 *]+);" % name, header)
                self.assertIsNotNone(match, name)
                product = 1
                for factor in match.group(1).split("*"):
                    product *= int(factor)
                self.assertEqual(product, value)

    def test_render_limit_is_reported(self):
        packets = wire.normalize_pdml(
            b"<pdml><packet>" + b'<field name="x"/>' * 2049 + b"</packet></pdml>"
        )
        fields, limited = worker.render_fields(packets)
        self.assertEqual(len(fields), 2048)
        self.assertTrue(limited)

    def test_blocked_pipe_write_has_deadline(self):
        entered = threading.Event()
        release = threading.Event()

        class Stalled:
            def write(self, value):
                entered.set()
                release.wait()
                return len(value)

            def flush(self):
                pass

        try:
            with self.assertLogs(worker.LOG, level="WARNING") as warnings:
                with self.assertRaisesRegex(wire.ProofError, "execution_timeout"):
                    worker.write_before(Stalled(), b"x", time.monotonic() + 0.1)
            self.assertTrue(entered.wait(2), 'the blocked writer actually started')
        finally:
            release.set()
        self.assertEqual([record.getMessage() for record in warnings.records], ["preview_pipe_timeout"])

    def test_pipe_deadline_expired_before_wait_still_logs_timeout(self):
        # Scheduler delay can exhaust the budget between thread start and wait.
        # Keep this race deterministic without depending on host load or sleep.
        with patch.object(worker, 'remaining', side_effect=wire.ProofError('execution_timeout')):
            with self.assertLogs(worker.LOG, level='WARNING') as warnings:
                with self.assertRaisesRegex(wire.ProofError, 'execution_timeout'):
                    worker.operation_before(lambda: None, 1)
        self.assertEqual([record.getMessage() for record in warnings.records], ['preview_pipe_timeout'])

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


class PackLuaTests(unittest.TestCase):
    """Configuration pack Lua in a session profile (docs/plugin-repositories.md 4.3)."""

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.files = []
        for index in range(12):
            path = Path(self.folder.name) / f"{index}.lua"
            path.write_text("-- synthetic\n", encoding="ascii")
            self.files.append(str(path))

    def tearDown(self):
        self.folder.cleanup()

    def profile(self, packs, lua=()):
        return request(profile=dict(request()["profile"], lua=list(lua), packLua=packs))

    def test_pack_lua_within_the_limits_is_accepted(self):
        worker.validate_request(self.profile([{"id": "org.example.a", "files": self.files[:8]}]))
        packs = [
            {"id": "a", "files": self.files[:8]},
            {"id": "b", "files": self.files[:8]},
            {"id": "c", "files": self.files[:8]},
        ]
        # The user's 8 scripts and three packs of 8: 32 in total.
        worker.validate_request(self.profile(packs, lua=self.files[:8]))

    def test_pack_lua_limits_ids_and_shapes_are_rejected(self):
        missing = str(Path(self.folder.name) / "missing.lua")
        cases = [
            ([{"id": "a", "files": self.files[:9]}], "input_limit"),
            ([{"id": "a", "files": []}], "input_limit"),
            ([{"id": "Bad Id", "files": self.files[:1]}], "invalid_profile"),
            ([{"id": "a", "files": self.files[:1]}] * 2, "invalid_profile"),
            ([{"id": "a", "files": self.files[:1], "extra": 1}], "invalid_profile"),
            ([{"id": "a", "files": ["relative.lua"]}], "invalid_profile"),
            ([{"id": "a", "files": [missing]}], "input_missing"),
            ("not a list", "input_limit"),
        ]
        for packs, reason in cases:
            with self.subTest(reason=reason, packs=str(packs)[:60]):
                with self.assertRaisesRegex(wire.ProofError, reason):
                    worker.validate_request(self.profile(packs))
        over = [{"id": name, "files": self.files[:7]} for name in "abcd"]
        with self.assertRaisesRegex(wire.ProofError, "input_limit"):
            worker.validate_request(self.profile(over, lua=self.files[:8]))

    def test_lua_string_escapes_every_unsafe_byte(self):
        self.assertEqual(worker.lua_string('a"b\\c\né'), '"a\\034b\\092c\\010\\195\\169"')

    def test_loader_runs_each_pack_in_its_own_environment(self):
        copies = [(Path(self.files[0]), "org.example.a/common.lua"), (Path(self.files[1]), "x/b.lua")]
        loader = worker.pack_loader("org.example.a", copies)
        self.assertIn("setmetatable({}, { __index = _G })", loader)
        self.assertIn('load(source, "@" .. file[2], "t", environment)', loader)
        self.assertIn("setfenv(chunk, environment)", loader)
        self.assertIn(worker.lua_string(self.files[0]), loader)
        self.assertLess(loader.index("common.lua"), loader.index("x/b.lua"))


# The former response cap: the lowest result limit a request may choose (#183).
SMALL = 128 * 1024


class ResultLimitTests(unittest.TestCase):
    """Issue #183: real messages fit, and a limited result names its limit."""

    @staticmethod
    def packets(count):
        return wire.normalize_pdml(
            b"<pdml><packet>" + b'<field name="x" show="v"/>' * count + b"</packet></pdml>",
            node_limit=4096,
        )

    def test_defaults_cover_large_messages_within_bounded_maximums(self):
        self.assertEqual(worker.FIELD_LIMIT, 2048)
        self.assertEqual(worker.RESPONSE_LIMIT, 1024 * 1024)
        self.assertEqual(worker.RESPONSE_LIMIT_MIN, 128 * 1024)
        self.assertEqual(worker.RESPONSE_LIMIT_MAX, 4 * 1024 * 1024)
        fields, limited = worker.render_fields(self.packets(2048))
        self.assertEqual((len(fields), limited), (2048, False))

    def test_row_limit_names_the_rows_shown_and_in_the_tree(self):
        fields, limited = worker.render_fields(self.packets(2050))
        self.assertEqual((len(fields), limited), (2048, True))
        self.assertEqual(worker.count_rows(self.packets(2050)), 2050)

    def test_request_may_choose_the_result_limit_within_bounds(self):
        for value in (worker.RESPONSE_LIMIT_MIN, worker.RESPONSE_LIMIT_MAX):
            worker.validate_request(request(responseLimit=value))
        for value in (worker.RESPONSE_LIMIT_MIN - 1, worker.RESPONSE_LIMIT_MAX + 1, True, "1"):
            with self.subTest(value=value), self.assertRaises(wire.ProofError):
                worker.validate_request(request(responseLimit=value))

    def test_byte_limit_names_its_maximum_the_rows_and_the_bytes_needed(self):
        rows = [ResponseBudgetTests.row("a" * 1000, identity=str(i)) for i in range(300)]
        value = ResponseBudgetTests.response(rows)
        needed = len(worker.encode_frame(value, worker.RESPONSE_LIMIT_MAX)) - 4
        fitted = worker._fit_response(value, 128 * 1024)
        self.assertTrue(fitted["limited"])
        self.assertEqual(fitted["reason"], "response_limit")
        self.assertEqual(
            fitted["limit"],
            {"name": "bytes", "maximum": 128 * 1024, "shownRows": len(fitted["fields"]),
             "totalRows": 300, "neededBytes": needed},
        )
        self.assertLessEqual(len(worker.encode_frame(fitted, 128 * 1024)) - 4, 128 * 1024)
        # The same rows fit the default limit whole.
        self.assertEqual(worker._fit_response(value), value)

    def test_a_byte_cut_after_a_pdml_cut_keeps_the_pdml_limit(self):
        # Issue #198: the tree's size is not known; the byte limit counts the rows read and
        # names the PDML limit that cut the tree first.
        rows = [ResponseBudgetTests.row("a" * 1000, identity=str(i)) for i in range(300)]
        value = ResponseBudgetTests.response(
            rows, complete=False, limited=True, reason="pdml_item_limit",
            limit={"name": "pdmlItems", "maximum": 8192, "shownRows": 300, "readRows": 5000},
        )
        fitted = worker._fit_response(value, 128 * 1024)
        self.assertEqual(fitted["reason"], "response_limit")
        self.assertEqual(fitted["limit"]["tree"], "pdmlItems")
        self.assertEqual(fitted["limit"]["totalRows"], 5000)
        self.assertEqual(fitted["limit"]["shownRows"], len(fitted["fields"]))
        self.assertEqual(worker._fit_response(value), value)

    def test_limit_failures_are_limited_and_end_the_channel(self):
        # Review of #192 (#198): the host's limit text relies on `limited` for these failures.
        for reason, limited in (("pdml_item_limit", True), ("pdml_byte_limit", True),
                                ("depth_limit", True), ("diagnostic_limit", True),
                                ("lua_failure", False)):
            with self.subTest(reason=reason):
                stdin, stdout = io.BytesIO(), io.BytesIO()
                worker.write_frame(stdin, request())
                stdin.seek(0)
                with patch.object(worker.PreviewWorker, "evaluate",
                                  side_effect=wire.ProofError(reason)), \
                        patch.object(worker.sys, "stdin", type("In", (), {"buffer": stdin})), \
                        patch.object(worker.sys, "stdout", type("Out", (), {"buffer": stdout})), \
                        patch.object(worker.logging, "basicConfig"):
                    self.assertEqual(worker.main([]), 1)
                stdout.seek(0)
                response = worker.read_frame(stdout)
                self.assertEqual((response["state"], response["reason"], response["limited"]),
                                 ("failed", reason, limited))


class ResponseBudgetTests(unittest.TestCase):
    @staticmethod
    def row(show, depth=0, identity="probe.value"):
        return {"columns": ["probe.value", show, "", "", ""], "depth": depth,
                "attributes": {"name": "probe.value", "show": show},
                "positionOrigin": worker.ORIGIN, "id": identity}

    @staticmethod
    def response(fields, **changes):
        value = worker.response_base("response-fit", {"dissector": "probe"})
        value.update(state="completed", available=True, complete=True, limited=False,
                     fields=fields, enginePid=123, scriptSha256=[])
        value.update(changes)
        return value

    @staticmethod
    def body_size(value):
        # The existing request limit is only a test oracle for oversized bodies;
        # the production response cap is never changed.
        return len(worker.encode_frame(value, worker.REQUEST_LIMIT)) - 4

    def sized_prefix(self, target, count=1, later=()):
        """Rows whose limited result body is exactly `target` bytes, the `later` rows cut off.
        The limited result names its limit (#183), whose numbers settle within a few passes."""
        fields = [self.row("small parent", identity="parent")] if count == 2 else []
        row = self.row("", depth=count - 1)
        fields.append(row)
        for _ in range(4):
            needed = self.body_size(self.response(fields + list(later)))
            limit = worker.byte_limit(SMALL, len(fields), len(fields) + len(later), needed)
            limited = self.response(fields, complete=False, limited=True,
                                    reason="response_limit", limit=limit)
            room = target - self.body_size(limited) + 2 * len(row["columns"][1])
            if room % 2:
                row["id"] = row["id"].rstrip("x") if row["id"].endswith("x") else row["id"] + "x"
                continue
            # The canonical show string occurs once in attributes and once in columns.
            row["attributes"]["show"] = row["columns"][1] = "a" * (room // 2)
        self.assertLessEqual(len(row["columns"][1]), 65536)
        self.assertEqual(self.body_size(limited), target)
        return fields

    def test_aggregate_overflow_retains_exact_first_whole_row(self):
        first = self.row("a" * 40000)
        second = self.row("b" * 40000, depth=1)
        value = self.response([first, second, self.row("small later sibling")])
        self.assertLess(len(worker.encode_frame(self.response([first]), SMALL)), SMALL)
        with self.assertRaisesRegex(wire.ProofError, "response_limit"):
            worker.encode_frame(value, SMALL)
        fitted = worker._fit_response(value, SMALL)
        self.assertEqual(len(fitted["fields"]), 1, "aggregate overflow discarded the fitting prefix")
        self.assertEqual(fitted["fields"], [first])
        self.assertTrue(fitted["available"])
        self.assertEqual(fitted["state"], "completed")
        self.assertFalse(fitted["complete"])
        self.assertTrue(fitted["limited"])
        self.assertEqual(fitted["reason"], "response_limit")
        self.assertLessEqual(len(worker.encode_frame(fitted, SMALL)) - 4, SMALL)

    def test_limited_metadata_and_two_row_comma_fit_exact_body_cap(self):
        later = [self.row("last row cannot fit")]
        fields = self.sized_prefix(SMALL, count=2, later=later)
        value = self.response(fields + later)
        fitted = worker._fit_response(value, SMALL)
        self.assertEqual(len(fitted["fields"]), 2)
        self.assertEqual(fitted["fields"], fields)
        self.assertEqual(self.body_size(fitted), SMALL)
        self.assertEqual(struct.unpack(">I", worker.encode_frame(fitted, SMALL)[:4])[0], SMALL)

    def test_second_row_one_byte_over_cap_stops_at_prior_row(self):
        later = [self.row("small row must not be sampled")]
        fields = self.sized_prefix(SMALL + 1, count=2, later=later)
        value = self.response(fields + later)
        fitted = worker._fit_response(value, SMALL)
        self.assertEqual(len(fitted["fields"]), 1)
        self.assertEqual(fitted["fields"], fields[:1])
        self.assertLessEqual(self.body_size(fitted), SMALL)

    def test_unicode_supplementary_and_nul_use_actual_ascii_escaping(self):
        text = "\u00e9\U0001f680\x00" * 2000
        first = self.row(text)
        value = self.response([first, self.row(text, depth=1)])
        utf8_size = len(json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))
        self.assertLess(utf8_size, SMALL)
        self.assertGreater(self.body_size(value), SMALL)
        fitted = worker._fit_response(value, SMALL)
        self.assertEqual(len(fitted["fields"]), 1)
        self.assertEqual(fitted["fields"], [first])
        self.assertEqual(fitted["fields"][0]["columns"][1], text)
        self.assertLessEqual(self.body_size(fitted), SMALL)

    def test_first_oversized_row_is_not_truncated_or_skipped(self):
        value = self.response([self.row("\U0001f680" * 10000), self.row("later small root")])
        fitted = worker._fit_response(value, SMALL)
        self.assertEqual(fitted["fields"], [])
        self.assertEqual(fitted["reason"], "response_limit")
        self.assertTrue(fitted["limited"])
        self.assertFalse(fitted["complete"])
        self.assertLessEqual(self.body_size(fitted), SMALL)

    def test_input_rows_and_flags_remain_immutable(self):
        value = self.response([self.row("a" * 40000), self.row("b" * 40000)])
        before = copy.deepcopy(value)
        worker._fit_response(value, SMALL)
        self.assertEqual(value, before)

    def test_full_fit_and_existing_other_limit_metadata_are_unchanged(self):
        for changes in ({}, {"complete": False, "limited": True, "reason": "rendered_field_limit"}):
            with self.subTest(changes=changes):
                value = self.response([self.row("9007199254740993"), self.row("")], **changes)
                before = worker.encode_frame(value, SMALL)
                fitted = worker._fit_response(value, SMALL)
                self.assertEqual(fitted, value)
                self.assertEqual(worker.encode_frame(fitted), before)

    def test_byte_overflow_replaces_other_limit_reason_and_serializes_linearly(self):
        value = self.response([self.row("a" * 40000), self.row("b" * 40000), self.row("small")],
                              complete=False, limited=True, reason="rendered_field_limit")
        original = worker.encode_frame
        with patch.object(worker, "encode_frame", wraps=original) as encoding:
            fitted = worker._fit_response(value, SMALL)
        self.assertEqual(len(fitted["fields"]), 1)
        self.assertEqual(fitted["reason"], "response_limit")
        self.assertLessEqual(encoding.call_count, len(value["fields"]) + 3)


class PdmlCutEvaluationTests(unittest.TestCase):
    """Issue #198 without a real engine: what evaluate() makes of a tree cut at a PDML limit."""

    class Engine:
        def __init__(self, data, cut):
            self.data = (data, cut)
            self.process = type("Process", (), {"pid": 4242, "stdin": io.BytesIO()})()

        def packet(self, deadline):
            return self.data

    def evaluate(self, data, cut=False, protocol=""):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        session = worker.PreviewWorker(Path(temp.name))
        self.addCleanup(session.close)
        engine = self.Engine(data, cut)
        value = request(profile=dict(request()["profile"], protocol=protocol))
        with patch.object(session, "start", lambda profile, deadline: setattr(session, "engine", engine)), \
                patch.object(session, "command",
                             lambda arguments, deadline: Path(arguments[-1]).write_bytes(b"pcap")), \
                patch.object(worker, "capture_record", return_value=(b"", b"")):
            return session.evaluate(value)

    @staticmethod
    def tree(fields, transport=True):
        frame = b'<proto name="frame"><field name="frame.len" show="1"/></proto>' if transport else b""
        return (b"<pdml><packet>" + frame + b'<proto name="p" showname="P">'
                + b'<field name="p.x" show="1" value="01"/>' * fields + b"</proto></packet></pdml>")

    def test_an_item_cut_shows_the_first_rows_and_counts_the_rows_read(self):
        result = self.evaluate(self.tree(9000))
        self.assertEqual((result["state"], result["reason"], result["limited"], result["complete"]),
                         ("completed", "pdml_item_limit", True, False))
        self.assertEqual(len(result["fields"]), worker.FIELD_LIMIT)
        # 8192 items: the packet, frame and its field, the protocol and 8188 fields.
        self.assertEqual(result["limit"], {"name": "pdmlItems", "maximum": 8192,
                                           "shownRows": 2048, "readRows": 8189})
        self.assertEqual(result["enginePid"], 4242)

    def test_a_byte_cut_shows_the_rows_read_and_names_the_byte_limit(self):
        data = self.tree(100)[:-len(b'"/></proto></packet></pdml>') - 30]
        result = self.evaluate(data, cut=True)
        self.assertEqual(result["reason"], "pdml_byte_limit")
        self.assertEqual(result["limit"]["name"], "pdmlBytes")
        self.assertEqual(result["limit"]["maximum"], worker.PDML_LIMIT)
        self.assertEqual(result["limit"]["shownRows"], len(result["fields"]))
        self.assertEqual(result["limit"]["readRows"], len(result["fields"]))
        self.assertLess(len(result["fields"]), 101)
        # The item limit wins when it falls within the bytes read.
        self.assertEqual(self.evaluate(self.tree(9000)[:-20], cut=True)["reason"],
                         "pdml_item_limit")

    def test_a_cut_before_any_row_or_the_protocol_fails_at_that_limit(self):
        transport_only = b'<pdml><packet><proto name="frame"><field name="frame.len" show="1"/>'
        with self.assertRaisesRegex(wire.ProofError, "pdml_byte_limit"):
            self.evaluate(transport_only, cut=True)
        with self.assertRaisesRegex(wire.ProofError, "pdml_item_limit"):
            self.evaluate(self.tree(9000), protocol="later")
        # Without a cut these keep their own reasons.
        with self.assertRaisesRegex(wire.ProofError, "protocol_not_found"):
            self.evaluate(self.tree(3), protocol="later")


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

    def child(self, code, framed=True):
        child = worker.EngineProcess(
            self.owner, [sys.executable, "-c", code], self.temp.name, os.environ.copy(), framed
        )
        self.children.append(child)
        return child

    def test_read_stall_and_write_stall_are_bounded_and_reaped(self):
        child = self.child("import time; time.sleep(60)")
        with self.assertRaisesRegex(wire.ProofError, "execution_timeout"):
            child.packet(time.monotonic() + 0.1)
        with self.assertLogs(worker.LOG, level="WARNING") as warnings:
            with self.assertRaisesRegex(wire.ProofError, "execution_timeout"):
                worker.write_before(
                    child.process.stdin, b"x" * worker.PDML_LIMIT, time.monotonic() + 0.1
                )
        self.assertEqual([record.getMessage() for record in warnings.records], ["preview_pipe_timeout"])

    def test_stdout_and_stderr_flood_are_bounded(self):
        # Issue #198: past the PDML limit the engine's output is read and dropped (the packet
        # never ends here, so the request times out); past the diagnostics limit it fails.
        # A tool's output (not the engine's PDML stream) past the limit still fails.
        for stream, framed, reason in (("stdout", True, "execution_timeout"),
                                       ("stdout", False, "output_limit"),
                                       ("stderr", True, "diagnostic_limit")):
            with self.subTest(stream=stream, framed=framed):
                child = self.child(
                    "import sys,time; sys." + stream
                    + '.buffer.write(b"x"*%d); time.sleep(60)' % (worker.PDML_LIMIT + 65536),
                    framed,
                )
                with self.assertRaisesRegex(wire.ProofError, reason):
                    child.packet(time.monotonic() + (2 if reason == "execution_timeout" else 5))
                self.assertLessEqual(len(child.buffers[0]), worker.PDML_LIMIT)
                self.assertLessEqual(len(child.buffers[1]), worker.STDERR_LIMIT)
                if reason == "execution_timeout":
                    # The engine's stream keeps the first PDML_LIMIT bytes and drops the rest.
                    self.assertEqual(len(child.cut), worker.PDML_LIMIT)
                    self.assertTrue(child.discarding)

    def test_a_packet_past_the_pdml_limit_is_cut_and_the_next_one_stays_in_step(self):
        # Issue #198: the engine is not failed; its next packet is read whole.
        child = self.child(
            "import sys,time\n"
            "out = sys.stdout.buffer\n"
            'out.write(b\'<?xml version="1.0"?>\\n<pdml version="0">\\n<packet><proto name="p">\')\n'
            "out.write((b'<field name=\"x\" show=\"' + b'a' * 1000 + b'\"/>') * 5000)\n"
            "out.write(b'</proto></packet>\\n<packet><proto name=\"next\"/></packet>\\n')\n"
            "out.flush(); time.sleep(60)\n"
        )
        data, cut = child.packet(time.monotonic() + 10)
        self.assertTrue(cut)
        # The limit counts the packet's bytes, not the session's PDML header (review of #204).
        self.assertEqual(len(data), len(b"<pdml>") + worker.PDML_LIMIT)
        packets, items_cut = wire.normalize_pdml_prefix(
            data, node_limit=worker.PDML_NODE_LIMIT, depth_limit=32, whole=False
        )
        self.assertFalse(items_cut)
        self.assertGreater(len(packets[0]["children"][0]["children"]), 4000)
        data, cut = child.packet(time.monotonic() + 10)
        self.assertFalse(cut)
        self.assertEqual(data, b'<pdml><packet><proto name="next"/></packet></pdml>')

    def test_the_end_of_a_cut_packet_is_found_across_reads(self):
        # The end tag of a dropped packet may arrive split over several reads.
        engine = object.__new__(worker.EngineProcess)
        engine.buffers, engine.cut, engine.discarding, engine.tail = [bytearray(), b""], None, False, b""
        stream = b"<packet>" + b"x" * 40 + b"</packet>" + b"<packet>n</packet>"
        with patch.object(worker, "PDML_LIMIT", 24):
            for byte in range(len(stream)):
                engine.take_pdml(stream[byte : byte + 1])
        self.assertEqual(engine.cut, b"<packet>" + b"x" * 16)
        self.assertFalse(engine.discarding)
        self.assertEqual(bytes(engine.buffers[0]), b"<packet>n</packet>")
        # A packet whose end lies just past the limit is cut too; what follows it is kept.
        engine.buffers[0], engine.cut = bytearray(), None
        with patch.object(worker, "PDML_LIMIT", 24):
            engine.take_pdml(b"<packet>" + b"x" * 10 + b"</packet>" + b"<packet>n</packet>")
        self.assertEqual(engine.cut, b"<packet>" + b"x" * 10 + b"</pack")
        self.assertFalse(engine.discarding)
        self.assertEqual(bytes(engine.buffers[0]), b"<packet>n</packet>")
        # The PDML header before the first packet does not count; a packet ending exactly at
        # the limit is whole.
        engine.buffers[0], engine.cut = bytearray(), None
        header = b'<?xml version="1.0"?>\n<pdml version="0">\n'
        with patch.object(worker, "PDML_LIMIT", 24):
            engine.take_pdml(header + b"<packet>" + b"x" * 7 + b"</packet>")
        self.assertIsNone(engine.cut)
        # One byte less: the end tag is cut.
        with patch.object(worker, "PDML_LIMIT", 23):
            engine.take_pdml(b"\n")
        self.assertEqual(engine.cut, header + b"<packet>" + b"x" * 7 + b"</packet")
        self.assertEqual(bytes(engine.buffers[0]), b"\n")

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
            "protocol": "cilogg_md01_probe",
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

    def test_a_tree_past_the_row_limit_is_cut_at_2048_rows_and_says_so(self):
        # Review of #192: the PDML node limit counted the transport nodes too, so a tree of
        # 2048 rows failed (field_limit) before the row limit could cut it.
        script = Path(self.temp.name) / "rows_probe.lua"
        script.write_text(
            'local p = Proto("cilogg_rows", "CILogg rows probe")\n'
            'local item = ProtoField.uint8("cilogg_rows.item", "Item")\n'
            "p.fields = { item }\n"
            "function p.dissector(tvb, pinfo, tree)\n"
            "    local root = tree:add(p, tvb())\n"
            "    for index = 1, 2100 do root:add(item, tvb(0, 1)) end\n"
            "    return tvb:len()\n"
            "end\n",
            encoding="utf-8",
        )
        self.profile.update(lua=[str(script)], dissector="cilogg_rows", protocol="cilogg_rows")
        result = self.evaluate(payloadBase64=base64.b64encode(b"\x07").decode())
        self.assertTrue(result["available"], result["reason"])
        self.assertTrue(result["limited"])
        self.assertEqual(result["reason"], "rendered_field_limit")
        self.assertEqual(len(result["fields"]), worker.FIELD_LIMIT)
        self.assertEqual(
            result["limit"],
            {"name": "rows", "maximum": worker.FIELD_LIMIT, "shownRows": worker.FIELD_LIMIT,
             "totalRows": 2101},
        )

    def items_probe(self, add):
        """A dissector adding as many items as the message's first two bytes say (#198)."""
        script = Path(self.temp.name) / "items_probe.lua"
        script.write_text(
            'local p = Proto("cilogg_items", "CILogg items probe")\n'
            'local item = ProtoField.uint8("cilogg_items.item", "Item")\n'
            'local text = ProtoField.string("cilogg_items.text", "Text")\n'
            "p.fields = { item, text }\n"
            "function p.dissector(tvb, pinfo, tree)\n"
            "    local root = tree:add(p, tvb())\n"
            "    for index = 1, tvb(0, 2):uint() do %s end\n"
            "    return tvb:len()\n"
            "end\n" % add,
            encoding="utf-8",
        )
        self.profile.update(lua=[str(script)], dissector="cilogg_items", protocol="cilogg_items")

    def test_a_tree_past_the_pdml_item_limit_shows_its_first_rows(self):
        # Issue #198: a tree past the 8192 items the worker reads shows its first 2048 rows, and
        # the worker stays warm for the next message.
        self.items_probe("root:add(item, tvb(0, 1))")
        result = self.evaluate(payloadBase64=base64.b64encode(struct.pack(">H", 9000)).decode())
        self.assertTrue(result["available"], result["reason"])
        self.assertTrue(result["limited"])
        self.assertFalse(result["complete"])
        self.assertEqual(result["reason"], "pdml_item_limit")
        self.assertEqual(len(result["fields"]), worker.FIELD_LIMIT)
        limit = result["limit"]
        self.assertEqual(sorted(limit), ["maximum", "name", "readRows", "shownRows"])
        self.assertEqual((limit["name"], limit["maximum"], limit["shownRows"]),
                         ("pdmlItems", worker.PDML_NODE_LIMIT, worker.FIELD_LIMIT))
        self.assertGreater(limit["readRows"], worker.FIELD_LIMIT)
        self.assertLess(limit["readRows"], worker.PDML_NODE_LIMIT)
        small = self.evaluate(id="small", payloadBase64=base64.b64encode(b"\x00\x03").decode())
        self.assertTrue(small["complete"], small["reason"])
        self.assertEqual(len(small["fields"]), 4)
        self.assertEqual(small["enginePid"], result["enginePid"])

    def test_a_tree_past_the_pdml_byte_limit_shows_its_first_rows(self):
        # Issue #198: fewer than 8192 items, but past the 4 MiB of PDML the worker reads.
        self.items_probe("root:add(text, tvb(2, 150))")
        payload = base64.b64encode(struct.pack(">H", 7000) + b"A" * 150).decode()
        result = self.evaluate(payloadBase64=payload, responseLimit=worker.RESPONSE_LIMIT_MAX)
        self.assertTrue(result["available"], result["reason"])
        self.assertEqual(result["reason"], "pdml_byte_limit")
        self.assertEqual(len(result["fields"]), worker.FIELD_LIMIT)
        limit = result["limit"]
        self.assertEqual((limit["name"], limit["maximum"], limit["shownRows"]),
                         ("pdmlBytes", worker.PDML_LIMIT, worker.FIELD_LIMIT))
        self.assertGreater(limit["readRows"], worker.FIELD_LIMIT)
        self.assertLess(limit["readRows"], 7001)
        # At the default result limit the rows are cut again by bytes, which keeps the PDML cut.
        fitted = self.evaluate(id="fitted", payloadBase64=payload)
        self.assertEqual(fitted["reason"], "response_limit")
        self.assertEqual(fitted["limit"]["tree"], "pdmlBytes")
        self.assertEqual(fitted["limit"]["totalRows"], limit["readRows"])
        # The dropped rest of the packet leaves the engine in step for the next message.
        small = self.evaluate(id="small", payloadBase64=base64.b64encode(b"\x00\x01" + b"A" * 150)
                              .decode())
        self.assertTrue(small["complete"], small["reason"])
        self.assertEqual(len(small["fields"]), 2)
        self.assertEqual(small["enginePid"], result["enginePid"])

    def test_large_map_tree_fits_whole_and_names_a_smaller_limit(self):
        # Issue #183 on a real engine: a 4 KiB message whose information map has 256 entries
        # (769 rows, past the former 512 rows and 128 KiB) fits the defaults whole.
        script = Path(self.temp.name) / "map_probe.lua"
        script.write_text(
            'local p = Proto("cilogg_map", "CILogg map probe")\n'
            'local entry = ProtoField.bytes("cilogg_map.entry", "Entry")\n'
            'local key = ProtoField.uint8("cilogg_map.key", "Key")\n'
            'local value = ProtoField.string("cilogg_map.value", "Value")\n'
            "p.fields = { entry, key, value }\n"
            "function p.dissector(tvb, pinfo, tree)\n"
            "    local map = tree:add(p, tvb()):add(tvb(), \"Information map\")\n"
            "    for offset = 0, tvb:len() - 16, 16 do\n"
            "        local item = map:add(entry, tvb(offset, 16))\n"
            "        item:add(key, tvb(offset, 1))\n"
            "        item:add(value, tvb(offset + 1, 15))\n"
            "    end\n"
            "    return tvb:len()\n"
            "end\n",
            encoding="utf-8",
        )
        self.profile.update(lua=[str(script)], dissector="cilogg_map", protocol="cilogg_map")
        payload = base64.b64encode(bytes(65 + i % 26 for i in range(4096))).decode()
        whole = self.evaluate(payloadBase64=payload)
        self.assertTrue(whole["complete"], {k: whole[k] for k in ("reason", "limited")})
        self.assertEqual(len(whole["fields"]), 2 + 3 * 256)
        self.assertNotIn("limit", whole)
        small = self.evaluate(id="small", payloadBase64=payload, responseLimit=128 * 1024)
        self.assertTrue(small["limited"])
        self.assertEqual(small["reason"], "response_limit")
        self.assertEqual(small["limit"]["name"], "bytes")
        self.assertEqual(small["limit"]["maximum"], 128 * 1024)
        self.assertEqual(small["limit"]["totalRows"], 2 + 3 * 256)
        self.assertEqual(small["limit"]["shownRows"], len(small["fields"]))
        self.assertGreater(small["limit"]["neededBytes"], 128 * 1024)

    def test_lua_display_text_reaches_the_value_column(self):
        # Issue #181 on a real engine: a value string, appended text and text-only subtrees.
        script = Path(self.temp.name) / "display_probe.lua"
        script.write_text(
            'local p = Proto("cilogg_display", "CILogg display probe")\n'
            'local status = ProtoField.uint8("cilogg_display.status", "Status", base.DEC,\n'
            '    { [0] = "Battery OK", [1] = "Battery low" })\n'
            'local model = ProtoField.uint8("cilogg_display.model", "Model", base.DEC)\n'
            "p.fields = { status, model }\n"
            "function p.dissector(tvb, pinfo, tree)\n"
            "    local root = tree:add(p, tvb())\n"
            '    local header = root:add(tvb(0, 2), "Header: two bytes")\n'
            "    header:add(status, tvb(0, 1))\n"
            '    header:add(model, tvb(1, 1)):append_text(" (Synthetic model 13)")\n'
            '    root:add(tvb(2), "Plain text label")\n'
            "    return tvb:len()\n"
            "end\n",
            encoding="utf-8",
        )
        self.profile.update(lua=[str(script)], dissector="cilogg_display", protocol="cilogg_display")
        result = self.evaluate(payloadBase64=base64.b64encode(bytes([0, 13, 0x61])).decode())
        self.assertTrue(result["available"], result)
        rows = [field["columns"][:3] for field in result["fields"]]
        self.assertIn(["Header: two bytes", "", ""], rows)
        self.assertIn(["cilogg_display.status", "Battery OK (0)", "00"], rows)
        self.assertIn(["cilogg_display.model", "13 (Synthetic model 13)", "0d"], rows)
        self.assertIn(["Plain text label", "", ""], rows)
        self.assertFalse(any(row[0] == "_ws.lua.text" for row in rows), rows)

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

    def test_real_named_alias_uses_expected_protocol_or_imports_all_without_selector(self):
        self.profile.update(dissector="eth_withoutfcs", protocol="eth")
        payload = base64.b64encode(
            bytes.fromhex("00112233445566778899aabb08004500001400000000400100007f0000017f000001")
        ).decode("ascii")
        selected = self.evaluate(payloadBase64=payload)
        self.assertTrue(selected["available"])
        selected_names = [field["attributes"].get("name") for field in selected["fields"]]
        self.assertIn("eth", selected_names)
        self.assertNotIn("ip", selected_names)
        self.profile["protocol"] = ""
        unselected = self.evaluate(id="all", payloadBase64=payload)
        self.assertEqual(selected["enginePid"], unselected["enginePid"])
        names = [field["attributes"].get("name") for field in unselected["fields"]]
        self.assertIn("eth", names)
        self.assertIn("ip", names)
        self.assertNotIn("eth_withoutfcs", names)
        self.assertNotIn("frame", names)

    def test_explicit_protocol_selector_absent_from_real_tree_fails(self):
        for protocol in ("cilogg_absent_protocol", "frame"):
            with self.subTest(protocol=protocol):
                self.profile["protocol"] = protocol
                with self.assertLogs(worker.LOG, level="WARNING") as warnings:
                    with self.assertRaisesRegex(wire.ProofError, "protocol_not_found"):
                        self.evaluate()
                self.assertEqual(
                    [record.getMessage() for record in warnings.records],
                    ["preview_protocol_not_found id=one"],
                )

    def test_missing_and_invalid_named_dissectors_are_errors(self):
        self.profile.update(dissector="cilogg_missing_dissector", protocol="")
        with self.assertLogs(worker.LOG, level="WARNING") as warnings:
            with self.assertRaisesRegex(wire.ProofError, "dissector_not_dispatched"):
                self.evaluate()
        self.assertEqual(
            [record.getMessage() for record in warnings.records],
            ["preview_no_decoded_fields id=one"],
        )
        self.profile["dissector"] = "--invalid"
        with self.assertRaisesRegex(wire.ProofError, "invalid_dissector"):
            self.evaluate()


if __name__ == "__main__":
    unittest.main()
