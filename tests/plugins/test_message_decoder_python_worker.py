"""MD03b: the supervised Python decoder worker for configuration pack decoders.

Covers the request contract, the session snapshot (modules copied and hashed, one import
namespace per pack), the result contract on PreviewService result v1 rows with its limits, and
the framed process protocol (stdout kept for frames, crashes visible to the host).
"""

import base64
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/plugins"))
import message_decoder_python_worker as worker  # noqa: E402
from message_decoder_wire import ProofError  # noqa: E402

WORKER = ROOT / "scripts/plugins/message_decoder_python_worker.py"


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(text), encoding="utf-8")
    return path


def make_pack(base, pack_id, modules):
    """A pack content folder with `modules` ({relative path: source}); returns the profile entry.
    Every module is listed, as a pack lists its helpers too (only listed modules are part of a
    pack's content); the id is the file name without a "decode_" prefix."""
    root = Path(base) / pack_id
    decoders = []
    for relative, source in modules.items():
        write(root / relative, source)
        stem = Path(relative).stem
        if stem.startswith("decode_"):
            stem = stem[len("decode_"):]
        decoders.append({"id": stem.replace("_", "-"), "module": relative})
    return {"id": pack_id, "root": str(root), "decoders": decoders}


def request(packs, decoder, payload=b"\x01\x02", **changes):
    value = {
        "version": 1,
        "id": "one",
        "op": "decode",
        "profile": {"packs": packs},
        "decoder": decoder,
        "context": {
            "preview": "Demo frame",
            "protocol": "",
            "inputEncoding": "hex",
            "direction": "unknown",
        },
        "payloadBase64": base64.b64encode(payload).decode("ascii"),
        "timeoutMs": 3000,
    }
    value.update(changes)
    return value


SIMPLE = """
    def decode(payload, context):
        return {"fields": [
            {"name": "length", "value": len(payload), "pos": 0, "size": len(payload)},
            {"name": "first", "value": payload[0], "raw": payload[:1], "depth": 1,
             "attributes": {"note": "synthetic"}},
            {"name": "preview", "value": context["preview"], "depth": 1},
        ]}
"""


class Case(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.session = worker.DecoderSession(self.base)
        self.addCleanup(self.session.close)

    def pack(self, pack_id, modules):
        return make_pack(self.base / "packs", pack_id, modules)

    def evaluate(self, packs, decoder, payload=b"\x01\x02", **changes):
        return self.session.evaluate(request(packs, decoder, payload, **changes))


class RequestTests(Case):
    def test_strict_request_contract(self):
        packs = [self.pack("org.example.a", {"decode_simple.py": SIMPLE})]
        good = request(packs, "org.example.a/simple")
        self.assertEqual(worker.validate_request(good), b"\x01\x02")
        bad = [
            (dict(good, extra=1), "invalid_request"),
            (dict(good, version=2), "unsupported_version"),
            (dict(good, id="bad id"), "invalid_id"),
            (dict(good, op="evaluate"), "invalid_operation"),
            (dict(good, timeoutMs=0), "invalid_limits"),
            (dict(good, timeoutMs=10001), "invalid_limits"),
            (dict(good, payloadBase64="AQI"), "invalid_payload"),
            (dict(good, payloadBase64=""), "input_limit"),
            (dict(good, decoder="org.example.a"), "invalid_decoder"),
            (dict(good, decoder="org.example.a/missing"), "invalid_decoder"),
            (dict(good, context=dict(good["context"], inputEncoding="ebcdic")), "invalid_context"),
            (dict(good, context=dict(good["context"], direction="sideways")), "invalid_context"),
            (dict(good, context={"preview": "x"}), "invalid_context"),
        ]
        for value, reason in bad:
            with self.subTest(reason=reason, value=value):
                with self.assertRaisesRegex(ProofError, reason):
                    worker.validate_request(value)

    def test_payload_limit(self):
        packs = [self.pack("org.example.a", {"decode_simple.py": SIMPLE})]
        big = request(packs, "org.example.a/simple", b"x" * (worker.PAYLOAD_LIMIT + 1))
        with self.assertRaisesRegex(ProofError, "input_limit"):
            worker.validate_request(big)
        exact = request(packs, "org.example.a/simple", b"x" * worker.PAYLOAD_LIMIT)
        self.assertEqual(len(worker.validate_request(exact)), worker.PAYLOAD_LIMIT)

    def test_profile_rules(self):
        pack = self.pack("org.example.a", {"decode_simple.py": SIMPLE})
        cases = {
            "relative root": dict(pack, root="relative/folder"),
            "parent module": dict(pack, decoders=[{"id": "x", "module": "../escape.py"}]),
            "absolute module": dict(pack, decoders=[{"id": "x", "module": str(self.base / "a.py")}]),
            "not python": dict(pack, decoders=[{"id": "x", "module": "decode_simple.txt"}]),
            "bad pack id": dict(pack, id="Org.Example"),
            "bad decoder id": dict(pack, decoders=[{"id": "-x", "module": "decode_simple.py"}]),
            "repeated decoder": dict(pack, decoders=pack["decoders"] * 2),
            "unknown key": dict(pack, extra=True),
        }
        for name, entry in cases.items():
            with self.subTest(name=name):
                value = request([entry], "org.example.a/simple")
                with self.assertRaisesRegex(ProofError, "invalid_profile|input_missing"):
                    worker.validate_request(value)
        repeated = request([pack, pack], "org.example.a/simple")
        with self.assertRaisesRegex(ProofError, "invalid_profile"):
            worker.validate_request(repeated)
        too_many = dict(pack, decoders=[
            {"id": "d%d" % index, "module": "decode_simple.py"}
            for index in range(worker.PACK_DECODER_LIMIT + 1)
        ])
        with self.assertRaisesRegex(ProofError, "input_limit"):
            worker.validate_request(request([too_many], "org.example.a/d0"))


class SnapshotTests(Case):
    def test_modules_are_copied_and_hashed_at_session_start(self):
        pack = self.pack("org.example.a", {"decode_simple.py": SIMPLE})
        original = Path(pack["root"]) / "decode_simple.py"
        response = self.evaluate([pack], "org.example.a/simple")
        self.assertEqual(response["state"], "completed", response)
        self.assertEqual(
            response["moduleSha256"], hashlib.sha256(original.read_bytes()).hexdigest()
        )
        # The session runs its copy: a later change of the pack file is not picked up.
        original.write_text("def decode(payload, context):\n    raise ValueError()\n")
        again = self.evaluate([pack], "org.example.a/simple")
        self.assertEqual(again["state"], "completed", again)
        self.assertEqual(again["moduleSha256"], response["moduleSha256"])

    def test_profile_change_ends_the_session(self):
        first = self.pack("org.example.a", {"decode_simple.py": SIMPLE})
        second = self.pack("org.example.b", {"decode_simple.py": SIMPLE})
        self.evaluate([first], "org.example.a/simple")
        with self.assertRaisesRegex(ProofError, "session_configuration_changed"):
            self.evaluate([first, second], "org.example.a/simple")

    def test_module_size_limit(self):
        pack = self.pack("org.example.a", {"decode_simple.py": SIMPLE})
        module = Path(pack["root"]) / "decode_simple.py"
        module.write_bytes(b"#" * (worker.MODULE_FILE_LIMIT + 1))
        with self.assertRaisesRegex(ProofError, "input_limit"):
            self.evaluate([pack], "org.example.a/simple")

    def test_each_pack_has_its_own_import_namespace(self):
        def pack(pack_id, marker):
            return self.pack(pack_id, {
                "python/common.py": "MARKER = %r\nCOUNT = []\n" % marker,
                "python/decode_marker.py": """
                    from . import common
                    import sys

                    def decode(payload, context):
                        common.COUNT.append(1)
                        return [{"name": "marker", "value": common.MARKER},
                                {"name": "calls", "value": len(common.COUNT)},
                                {"name": "package", "value": __package__}]
                """,
            })

        packs = [pack("org.example.a", "a"), pack("org.example.b", "b")]

        def values(response):
            self.assertEqual(response["state"], "completed", response)
            return {row["columns"][0]: row["columns"][1] for row in response["fields"]}

        first = values(self.evaluate(packs, "org.example.a/marker"))
        second = values(self.evaluate(packs, "org.example.b/marker"))
        again = values(self.evaluate(packs, "org.example.a/marker"))
        self.assertEqual((first["marker"], second["marker"]), ("a", "b"))
        self.assertNotEqual(first["package"], second["package"])
        # Module state lives as long as the session, separately per pack.
        self.assertEqual((first["calls"], second["calls"], again["calls"]), ("1", "1", "2"))

    def test_module_paths_that_sanitize_alike_stay_distinct(self):
        root = self.base / "packs" / "org.example.a"
        write(root / "python/a-b.py", "def decode(payload, context):\n    return [{'name': 'dash'}]\n")
        write(root / "python/a_b.py", "def decode(payload, context):\n    return [{'name': 'under'}]\n")
        write(root / "python/a.b.py", "def decode(payload, context):\n    return [{'name': 'dot'}]\n")
        pack = {"id": "org.example.a", "root": str(root), "decoders": [
            {"id": "dash", "module": "python/a-b.py"},
            {"id": "under", "module": "python/a_b.py"},
            {"id": "dot", "module": "python/a.b.py"},
        ]}
        names = {}
        for decoder in ("dash", "under", "dot"):
            response = self.evaluate([pack], "org.example.a/" + decoder)
            self.assertEqual(response["state"], "completed", response)
            names[decoder] = response["fields"][0]["columns"][0]
            source = (root / dict((d["id"], d["module"]) for d in pack["decoders"])[decoder]).read_bytes()
            self.assertEqual(response["moduleSha256"], hashlib.sha256(source).hexdigest())
        self.assertEqual(names, {"dash": "dash", "under": "under", "dot": "dot"})

    def test_a_module_suffix_in_any_case_is_accepted_as_the_pack_validator_does(self):
        root = self.base / "packs" / "org.example.a"
        write(root / "python/Upper.PY", "from . import helper\n"
                                        "def decode(payload, context):\n"
                                        "    return [{'name': helper.NAME}]\n")
        write(root / "python/helper.Py", "NAME = 'upper'\n")
        pack = {"id": "org.example.a", "root": str(root), "decoders": [
            {"id": "upper", "module": "python/Upper.PY"},
            {"id": "helper", "module": "python/helper.Py"},
        ]}
        response = self.evaluate([pack], "org.example.a/upper")
        self.assertEqual(response["state"], "completed", response)
        self.assertEqual(response["fields"][0]["columns"][0], "upper")
        with self.assertRaisesRegex(ProofError, "invalid_profile"):
            worker.validate_request(request([dict(pack, decoders=[
                {"id": "text", "module": "python/notes.txt"}])], "org.example.a/text"))

    def test_relative_imports_work_in_folders_whose_names_are_escaped(self):
        root = self.base / "packs" / "org.example.a"
        write(root / "v1.2/decode%x.py", "from . import helper\n"
                                         "def decode(payload, context):\n"
                                         "    return [{'name': helper.NAME}]\n")
        write(root / "v1.2/helper.py", "NAME = 'escaped'\n")
        pack = {"id": "org.example.a", "root": str(root), "decoders": [
            {"id": "main", "module": "v1.2/decode%x.py"},
            {"id": "helper", "module": "v1.2/helper.py"},
        ]}
        response = self.evaluate([pack], "org.example.a/main")
        self.assertEqual(response["state"], "completed", response)
        self.assertEqual(response["fields"][0]["columns"][0], "escaped")

    def test_paths_that_differ_only_in_case_are_refused_on_every_platform(self):
        root = self.base / "packs" / "org.example.a"
        write(root / "Sub/x.py", "def decode(payload, context):\n    return []\n")
        if not (root / "sub/x.py").exists():  # a case-sensitive file system: both exist
            write(root / "sub/x.py", "def decode(payload, context):\n    return []\n")
        pack = {"id": "org.example.a", "root": str(root), "decoders": [
            {"id": "upper", "module": "Sub/x.py"},
            {"id": "lower", "module": "sub/x.PY"},
        ]}
        with self.assertRaisesRegex(ProofError, "invalid_profile"):
            self.evaluate([pack], "org.example.a/upper")

    def test_a_module_and_a_folder_of_one_name_are_refused(self):
        pack = self.pack("org.example.a", {
            "python/sub.py": "def decode(payload, context):\n    return []\n",
            "python/sub/inner.py": "def decode(payload, context):\n    return []\n",
        })
        with self.assertRaisesRegex(ProofError, "invalid_profile"):
            self.evaluate([pack], "org.example.a/sub")

    def test_a_warm_session_runs_its_copies_after_the_pack_folder_goes(self):
        pack = self.pack("org.example.a", {"decode_simple.py": SIMPLE})
        self.assertEqual(self.evaluate([pack], "org.example.a/simple")["state"], "completed")
        shutil.rmtree(pack["root"])
        again = self.evaluate([pack], "org.example.a/simple")
        self.assertEqual(again["state"], "completed", again)
        fresh = worker.DecoderSession(self.base)
        self.addCleanup(fresh.close)
        with self.assertRaisesRegex(ProofError, "input_missing"):
            fresh.evaluate(request([pack], "org.example.a/simple"))

    def test_places_in_a_traceback_are_relative_to_their_own_pack_only(self):
        folder = self.base / "pack-1"
        self.assertEqual(
            worker.pack_relative(str(folder / "python" / "x.py"), folder), "python/x.py"
        )
        self.assertIsNone(worker.pack_relative(str(self.base / "pack-10" / "x.py"), folder))
        self.assertIsNone(worker.pack_relative(str(self.base / "elsewhere.py"), folder))

    def test_a_pack_module_is_not_importable_by_its_bare_name(self):
        packs = [self.pack("org.example.a", {
            "helper.py": "VALUE = 1\n",
            "decode_bare.py": """
                def decode(payload, context):
                    import helper
                    return []
            """,
        })]
        response = self.evaluate(packs, "org.example.a/bare")
        self.assertEqual(response["reason"], "decoder_failed")
        self.assertIn("ModuleNotFoundError", response["detail"])


class ResultTests(Case):
    def decode(self, body, payload=b"\x01\x02"):
        # A new session for each body: a session keeps the modules it copied at its start.
        self.session.close()
        self.session = worker.DecoderSession(self.base)
        self.addCleanup(self.session.close)
        pack = self.pack("org.example.a", {"decode_case.py": body})
        return self.evaluate([pack], "org.example.a/case", payload)

    def test_rows_follow_preview_result_v1(self):
        pack = self.pack("org.example.a", {"decode_simple.py": SIMPLE})
        response = self.evaluate([pack], "org.example.a/simple", b"\x07\x08\x09")
        self.assertEqual(response["backend"], "python_decoder")
        self.assertEqual(response["decoder"], "org.example.a/simple")
        self.assertEqual(response["previewName"], "Demo frame")
        self.assertEqual(response["offsetSpace"], "decoder_payload")
        self.assertEqual(
            (response["available"], response["complete"], response["limited"]),
            (True, True, False),
        )
        self.assertEqual(response["messageCompleteness"], "not_reported")
        self.assertEqual(response["messageIntegrity"], "not_reported")
        self.assertEqual(response["enginePid"], os.getpid())
        rows = response["fields"]
        self.assertEqual([row["columns"] for row in rows], [
            ["length", "3", "", "0", "3"],
            ["first", "7", "07", "", ""],
            ["preview", "Demo frame", "", "", ""],
        ])
        self.assertEqual([row["depth"] for row in rows], [0, 1, 1])
        self.assertEqual(rows[1]["attributes"], {"note": "synthetic"})
        self.assertEqual({row["positionOrigin"] for row in rows}, {"decoder_payload"})
        self.assertEqual(len({row["id"] for row in rows}), 3)

    def test_exact_values_and_states(self):
        response = self.decode("""
            def decode(payload, context):
                return {
                    "fields": [
                        {"name": "big", "value": 9007199254740993},
                        {"name": "flag", "value": True},
                        {"name": "none", "value": None},
                        {"name": "empty", "value": ""},
                        {"name": "empty", "value": ""},
                    ],
                    "messageCompleteness": "incomplete",
                    "messageIntegrity": "invalid",
                }
        """)
        self.assertEqual(response["state"], "completed", response)
        self.assertEqual(
            [row["columns"][1] for row in response["fields"]],
            ["9007199254740993", "true", "", "", ""],
        )
        self.assertEqual(response["messageCompleteness"], "incomplete")
        self.assertEqual(response["messageIntegrity"], "invalid")
        self.assertTrue(response["complete"])

    def test_row_limit(self):
        response = self.decode("""
            def decode(payload, context):
                return [{"name": "row", "value": index} for index in range(600)]
        """)
        self.assertEqual(response["state"], "completed", response)
        self.assertEqual(len(response["fields"]), worker.FIELD_LIMIT)
        self.assertEqual((response["complete"], response["limited"]), (False, True))
        self.assertEqual(response["reason"], "rendered_field_limit")

    def test_depth_limit(self):
        response = self.decode("""
            def decode(payload, context):
                return [{"name": "level", "value": depth, "depth": depth} for depth in range(40)]
        """)
        self.assertEqual(response["state"], "completed", response)
        self.assertEqual(max(row["depth"] for row in response["fields"]), worker.DEPTH_LIMIT)
        self.assertEqual(len(response["fields"]), worker.DEPTH_LIMIT + 1)
        self.assertTrue(response["limited"])
        self.assertEqual(response["reason"], "depth_limit")

    def test_text_limit(self):
        response = self.decode("""
            def decode(payload, context):
                return [{"name": "long", "value": "x" * 70000}]
        """)
        self.assertEqual(response["state"], "completed", response)
        self.assertEqual(len(response["fields"][0]["columns"][1]), worker.TEXT_LIMIT)
        self.assertEqual(response["reason"], "field_text_limit")
        self.assertTrue(response["limited"])

    def test_lone_surrogates_become_replacement_characters(self):
        # surrogateescape decoding leaves lone surrogates, which are not valid JSON text for the
        # host; a surrogate pair written as two code points is one character.
        response = self.decode("""
            def decode(payload, context):
                odd = b"a\\xff".decode("utf-8", "surrogateescape")
                pair = "\\ud83d\\ude00"
                return [{"name": odd, "value": pair, "attributes": {odd: odd}}]
        """)
        self.assertEqual(response["state"], "completed", response)
        row = response["fields"][0]
        self.assertEqual(row["columns"][:2], ["a�", "\U0001F600"])
        self.assertEqual(row["attributes"], {"a�": "a�"})
        json.dumps(response, ensure_ascii=False).encode("utf-8")  # strict: no lone surrogates

    def test_attribute_names_that_clean_alike_are_refused(self):
        response = self.decode("""
            def decode(payload, context):
                return [{"name": "a", "attributes": {"k\\udc80": 1, "k\\udc81": 2}}]
        """)
        self.assertEqual(response["reason"], "invalid_decoder_result", response)

    def test_text_limits_count_utf16_units_as_the_host_does(self):
        wide = "\U0001F600" * 3
        self.assertEqual(worker.utf16_length(wide), 6)
        # Never half a character: 5 units keep two of the three.
        self.assertEqual(worker.utf16_prefix(wide, 5), "\U0001F600" * 2)
        self.assertEqual(worker.utf16_prefix(wide, 6), wide)
        self.assertFalse(worker.bounded_text("\U0001F600" * 65, 128))
        self.assertTrue(worker.bounded_text("\U0001F600" * 64, 128))

    def test_response_limit_keeps_a_prefix(self):
        response = self.decode("""
            def decode(payload, context):
                return [{"name": "row", "value": "y" * 60000} for index in range(4)]
        """)
        self.assertEqual(response["state"], "completed", response)
        self.assertEqual(response["reason"], "response_limit")
        self.assertLess(len(response["fields"]), 4)
        self.assertLessEqual(len(worker.encode_frame(response)) - 4, worker.RESPONSE_LIMIT)

    def test_response_limit_keeps_every_row_that_fits(self):
        # A Python result carries no TShark `limit` object, so its fit budgets none (#183 review):
        # the row after the kept prefix would not fit either.
        response = self.decode("""
            def decode(payload, context):
                return [{"name": "big", "value": "y" * 1200} for index in range(100)] + [
                    {"name": "r", "value": "z"} for index in range(400)]
        """)
        self.assertEqual(response["reason"], "response_limit", response.get("reason"))
        self.assertNotIn("limit", response)
        fields = response["fields"]
        self.assertEqual(fields[-1]["columns"][:2], ["r", "z"])
        following = dict(fields[-1], id=str(len(fields)))
        extended = dict(response, fields=fields + [following])
        size = len(worker.encode_frame(extended, 2 * worker.RESPONSE_LIMIT)) - 4
        self.assertGreater(size, worker.RESPONSE_LIMIT)

    def test_not_dispatched(self):
        response = self.decode("def decode(payload, context):\n    return None\n")
        self.assertEqual((response["state"], response["reason"]), ("failed", "decoder_not_dispatched"))
        self.assertEqual(response["fields"], [])

    def test_invalid_results(self):
        bodies = {
            "a number": "return 3",
            "unknown key": "return {'fields': [], 'other': 1}",
            "no name": "return [{'value': 1}]",
            "bad depth": "return [{'name': 'a', 'depth': 2}]",
            "first row deep": "return [{'name': 'a', 'depth': 1}]",
            "negative depth": "return [{'name': 'a', 'depth': -1}]",
            "bad value": "return [{'name': 'a', 'value': [1]}]",
            "bad state": "return {'fields': [], 'messageIntegrity': 'perfect'}",
            "bad attributes": "return [{'name': 'a', 'attributes': {'k': [1]}}]",
            "too many attributes": "return [{'name': 'a', 'attributes': {str(i): '' for i in range(65)}}]",
        }
        for name, body in bodies.items():
            with self.subTest(name=name):
                response = self.decode("def decode(payload, context):\n    " + body + "\n")
                self.assertEqual(response["state"], "failed", response)
                self.assertEqual(response["reason"], "invalid_decoder_result")

    def test_decoder_failures_name_the_place_not_the_data(self):
        response = self.decode("""
            def decode(payload, context):
                raise ValueError("secret payload text")
        """)
        self.assertEqual((response["state"], response["reason"]), ("failed", "decoder_failed"))
        self.assertIn("ValueError", response["detail"])
        self.assertIn("decode_case.py:3", response["detail"])
        self.assertNotIn("secret", json.dumps(response))
        exited = self.decode("def decode(payload, context):\n    raise SystemExit(3)\n")
        self.assertEqual(exited["reason"], "decoder_failed")
        for name in ("KeyboardInterrupt", "GeneratorExit"):
            with self.subTest(name=name):
                raised = self.decode("def decode(payload, context):\n    raise %s()\n" % name)
                self.assertEqual(raised["reason"], "decoder_failed")
                self.assertTrue(raised["detail"].startswith(name), raised["detail"])
                at_import = self.decode("raise %s()\n" % name)
                self.assertEqual(at_import["reason"], "decoder_load_failed")

    def test_load_failures(self):
        for name, (body, detail) in {
            "syntax": ("def decode(:\n", "SyntaxError at decode_case.py:1"),
            "raises": ("raise RuntimeError('at import')\n", "RuntimeError at decode_case.py:1"),
            "no entry point": ("VALUE = 1\n", "the module has no decode(payload, context) function"),
            "not callable": ("decode = 3\n", "the module has no decode(payload, context) function"),
        }.items():
            with self.subTest(name=name):
                response = self.decode(body)
                self.assertEqual((response["state"], response["reason"]), ("failed", "decoder_load_failed"))
                self.assertEqual(response["detail"], detail)

    def test_a_failed_load_is_tried_again(self):
        pack = self.pack("org.example.a", {"decode_case.py": "raise RuntimeError()\n"})
        for _ in range(2):
            response = self.evaluate([pack], "org.example.a/case")
            self.assertEqual(response["reason"], "decoder_load_failed")


class ExamplePackTests(Case):
    """The public example pack (plugins/examples/python-decoder-pack) through the worker."""

    def setUp(self):
        super().setUp()
        folder = ROOT / "plugins/examples/python-decoder-pack"
        manifest = json.loads((folder / "plugin.json").read_text(encoding="utf-8"))
        content = manifest["content"]
        self.pack_entry = {
            "id": manifest["id"],
            "root": str(folder),
            "decoders": content["python_decoders"],
        }
        self.target = content["decoder_routes"]["previewRoutes"]["Demo record"]["python"]

    def test_a_record_decodes_with_its_states(self):
        response = self.evaluate([self.pack_entry], self.target, bytes.fromhex("020311223366"))
        self.assertEqual(response["state"], "completed", response)
        values = {row["columns"][0]: row["columns"][1] for row in response["fields"]}
        self.assertEqual(values["record"], "Reading")
        self.assertEqual(values["data"], "112233")
        self.assertEqual(response["messageCompleteness"], "complete")
        self.assertEqual(response["messageIntegrity"], "valid")

    def test_a_bad_checksum_and_a_cut_record(self):
        bad = self.evaluate([self.pack_entry], self.target, bytes.fromhex("020311223300"))
        self.assertEqual(bad["messageIntegrity"], "invalid")
        cut = self.evaluate([self.pack_entry], self.target, bytes.fromhex("02031122"))
        self.assertEqual(cut["messageCompleteness"], "incomplete")
        self.assertEqual(cut["messageIntegrity"], "not_reported")

    def test_bytes_that_are_not_a_record_are_not_dispatched(self):
        response = self.evaluate([self.pack_entry], self.target, b"\x01")
        self.assertEqual(response["reason"], "decoder_not_dispatched")


def frame(value):
    data = json.dumps(value).encode("ascii")
    return struct.pack(">I", len(data)) + data


def read_frame(stream):
    header = stream.read(4)
    if len(header) < 4:
        return None
    (size,) = struct.unpack(">I", header)
    return json.loads(stream.read(size))


class ProcessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)

    def start(self):
        process = subprocess.Popen(
            [sys.executable, "-I", "-S", "-u", str(WORKER), "--workspace", str(self.base)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.addCleanup(process.wait, 10)
        self.addCleanup(process.kill)
        self.addCleanup(process.stderr.close)
        self.addCleanup(process.stdout.close)
        self.addCleanup(lambda: process.stdin.closed or process.stdin.close())
        return process

    def exchange(self, process, value):
        process.stdin.write(frame(value))
        process.stdin.flush()
        return read_frame(process.stdout)

    def test_persistent_session_keeps_stdout_for_frames(self):
        pack = make_pack(self.base / "packs", "org.example.a", {"decode_noisy.py": """
            import os, subprocess, sys

            def decode(payload, context):
                print("printed noise")
                os.write(1, b"raw fd noise")
                subprocess.run([sys.executable, "-c", "print('child noise')"], check=True)
                return [{"name": "ok", "value": len(payload)}]
        """})
        process = self.start()
        first = self.exchange(process, request([pack], "org.example.a/noisy", b"\x01", id="r1"))
        second = self.exchange(process, request([pack], "org.example.a/noisy", b"\x01\x02", id="r2"))
        self.assertEqual((first["id"], first["state"]), ("r1", "completed"), first)
        self.assertEqual((second["id"], second["fields"][0]["columns"][1]), ("r2", "2"))
        # One worker answered both (a venv launcher may run the interpreter as its child).
        self.assertGreater(first["enginePid"], 0)
        self.assertEqual(first["enginePid"], second["enginePid"])
        process.stdin.close()
        self.assertEqual(process.wait(10), 0)
        self.assertIn(b"printed noise", process.stderr.read())

    def test_decoder_failure_keeps_the_session(self):
        pack = make_pack(self.base / "packs", "org.example.a", {"decode_flaky.py": """
            def decode(payload, context):
                if payload == b"!":
                    raise ValueError()
                return [{"name": "ok"}]
        """})
        process = self.start()
        failed = self.exchange(process, request([pack], "org.example.a/flaky", b"!", id="r1"))
        self.assertEqual(failed["reason"], "decoder_failed")
        ok = self.exchange(process, request([pack], "org.example.a/flaky", b"?", id="r2"))
        self.assertEqual(ok["state"], "completed")

    def test_a_decoder_past_its_deadline_is_answered_and_the_worker_stays(self):
        pack = make_pack(self.base / "packs", "org.example.a", {"decode_slow.py": """
            import time

            def decode(payload, context):
                if payload == b"slow":
                    time.sleep(0.4)
                return [{"name": "ok"}]
        """})
        process = self.start()
        late = self.exchange(
            process, request([pack], "org.example.a/slow", b"slow", id="r1", timeoutMs=100)
        )
        self.assertEqual((late["id"], late["reason"]), ("r1", "execution_timeout"), late)
        ok = self.exchange(process, request([pack], "org.example.a/slow", b"fast", id="r2"))
        self.assertEqual(ok["state"], "completed", ok)
        self.assertIsNone(process.poll())

    def test_a_decoder_that_exits_the_process_is_visible_to_the_host(self):
        pack = make_pack(self.base / "packs", "org.example.a", {"decode_crash.py": """
            import os

            def decode(payload, context):
                os._exit(7)
        """})
        process = self.start()
        process.stdin.write(frame(request([pack], "org.example.a/crash")))
        process.stdin.flush()
        self.assertIsNone(read_frame(process.stdout))
        self.assertEqual(process.wait(10), 7)

    def test_protocol_errors_answer_and_end_the_channel(self):
        process = self.start()
        response = self.exchange(process, {"version": 1, "id": "bad", "op": "decode"})
        self.assertEqual((response["id"], response["state"]), ("bad", "failed"))
        self.assertEqual(response["reason"], "invalid_request")
        self.assertEqual(process.wait(10), 1)


if __name__ == "__main__":
    unittest.main()
