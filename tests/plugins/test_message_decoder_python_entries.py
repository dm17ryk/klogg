"""E16b: the worker that finds the decoders among configuration packs' Python modules.

Python's own parser reads each module and nothing in it runs: these cases cover what makes a
module a decoder (the last top-level binding of a callable `decode`, through branches and loops),
what the parser or compiler refuses, the size and time limits, and the framed process protocol.
"""

import base64
import json
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
import message_decoder_python_entries as entries  # noqa: E402
from message_decoder_wire import ProofError  # noqa: E402

WORKER = ROOT / "scripts/plugins/message_decoder_python_entries.py"


def source(text):
    return textwrap.dedent(text).encode("utf-8")


def verdict(text):
    """(entry, reason) of a module's source."""
    entry, reason, _line, _detail = entries.check_source(
        text if isinstance(text, bytes) else source(text), "module.py"
    )
    return entry, reason


def request(packs, **changes):
    payload = json.dumps({"packs": packs}).encode("utf-8")
    value = {
        "version": 1,
        "id": "check-1",
        "op": "entries",
        "payloadBase64": base64.b64encode(payload).decode("ascii"),
        "timeoutMs": 10000,
    }
    value.update(changes)
    return value


def make_pack(base, pack_id, modules):
    """A pack folder with `modules` ({decoder id: source}), each in python/<id>.py."""
    root = Path(base) / pack_id
    decoders = []
    for decoder_id, text in modules.items():
        path = root / "python" / (decoder_id + ".py")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text if isinstance(text, bytes) else source(text))
        decoders.append({"id": decoder_id, "module": "python/%s.py" % decoder_id})
    return {"id": pack_id, "root": str(root), "decoders": decoders}


# Modules that are decoders: a `decode` that may be callable after the module's top level.
DECODERS = {
    "plain": "def decode(payload, context):\n    return None\n",
    "lambda": "decode = lambda payload, context: []\n",
    "assigned": "def _impl(payload, context):\n    return None\ndecode = _impl\n",
    "annotated": "from typing import Callable\ndecode: Callable = lambda p, c: None\n",
    "imported": "from .plain import decode\n",
    "parenthesized": "from .plain import (\n    decode,\n)\n",
    "rebound": "from .plain import decode as _plain, _impl as decode\n",
    "made": "def make():\n    return print\ndecode = make()\n",
    # Decorators: a decorated function, and an async one a decorator may make a plain function.
    "decorated": "import functools\n@functools.lru_cache(None)\ndef decode(payload, context):\n"
    "    return None\n",
    "decoratedasync": "def to_sync(function):\n    return function\n@to_sync\n"
    "async def decode(payload, context):\n    return None\n",
    # The last binding wins.
    "syncredefined": "async def decode(payload, context):\n    return None\n"
    "def decode(payload, context):\n    return None\n",
    # Bindings in branches count when some path keeps a callable decode.
    "conditional": "import sys\nif sys.version_info >= (3, 10):\n"
    "    def decode(payload, context):\n        return None\nelse:\n    decode = None\n",
    "tryimport": "try:\n    from .fast import decode\nexcept ImportError:\n"
    "    def decode(payload, context):\n        return None\n",
    "maybedeleted": "DEBUG = False\ndef decode(payload, context):\n    return None\n"
    "if DEBUG:\n    del decode\n",
    "loop": "for decode in (print,):\n    pass\n",
    "walrus": "if (decode := print):\n    pass\n",
    "tuple": "def make():\n    return print\ndecode, other = make(), 1\n",
    # Soft keywords are names here.
    "softkeyword": "match = 1\ncase = 2\ntype = 3\n_ = 4\ndef decode(payload, context):\n"
    "    return match\n",
    # Deleting something else that holds the name keeps the decoder.
    "delattribute": "def decode(payload, context):\n    return None\nclass Holder:\n"
    "    decode = 1\ndel Holder.decode\n",
    "delsubscript": "def decode(payload, context):\n    return None\nitems = {decode: 1}\n"
    "del (items[decode],)\n",
    # Line ends, a byte order mark and form feeds as Python reads them.
    "crlf": b"import struct\r\n\r\ndef decode(payload, context):\r\n    return None\r\n",
    "cr": b"import struct\rdef decode(payload, context):\r    return None\r",
    "bom": b"\xef\xbb\xbfdef decode(payload, context):\n    return None\n",
    "formfeed": b"\x0cdef decode\x0c(payload, context):\n    return None\n",
    "semicolon": "VERSION = 1; from .plain import decode\n",
    # `:=` in Python's evaluation order: the outer one binds last; a lambda's default runs now.
    "nestedwalrus": "marker = (decode := ((decode := None) or print))\n",
    "lambdadefault": "helper = lambda value=(decode := print): value\n",
    # A loop left by `break` after the binding; a branch that raises does not end the module.
    "whilebreak": "while True:\n    decode = print\n    break\n",
    "raisebranch": "import sys\nif sys.version_info < (3, 0):\n    raise ImportError('old')\n"
    "def decode(payload, context):\n    return None\n",
    # An imported module is not `__main__`.
    "notmain": "if __name__ == '__main__':\n    pass\nelse:\n"
    "    def decode(payload, context):\n        return None\n",
    "starredsuffix": "*rest, decode = (None, print)\n",
}

# Modules that are not, with the reason given.
NOT_DECODERS = {
    "helper": ("MARKER = 'x'\n", "no_decode"),
    "asynchronous": ("async def decode(payload, context):\n    return None\n", "async_decode"),
    "asyncredefined": (
        "def decode(payload, context):\n    return None\n"
        "async def decode(payload, context):\n    return None\n",
        "async_decode",
    ),
    "classdecode": ("class decode:\n    pass\n", "not_callable"),
    "decoratedclass": (
        "import dataclasses\n@dataclasses.dataclass\nclass decode:\n    value: int = 0\n",
        "not_callable",
    ),
    "value": ("decode = None\n", "not_callable"),
    "onlyvalue": ("FLAG = True\nif FLAG:\n    decode = 'text'\n", "not_callable"),
    "tupleother": ("decode, other = None, print\n", "not_callable"),
    "starred": ("*decode, = (print,)\n", "not_callable"),
    "moduleimport": ("import decode\n", "not_callable"),
    "aliasedmodule": ("import json as decode\n", "not_callable"),
    "deleted": ("def decode(payload, context):\n    return None\ndel decode\n", "no_decode"),
    "dellist": ("def decode(payload, context):\n    return None\ndel [decode]\n", "no_decode"),
    "delnested": (
        "def decode(payload, context):\n    return None\nitems = {}\ndel (items, [decode])\n",
        "no_decode",
    ),
    "exception": (
        "try:\n    pass\nexcept Exception as decode:\n    pass\n",
        "no_decode",
    ),
    "annotationonly": ("from typing import Callable\ndecode: Callable\n", "no_decode"),
    "lambdaannotation": ("decode: lambda x=1: x\n", "no_decode"),
    "method": (
        "class Codec:\n    def decode(self, payload, context):\n        return None\n",
        "no_decode",
    ),
    "nested": ("def outer():\n    def decode(payload, context):\n        return None\n", "no_decode"),
    "onelinebody": ("def helper(): value = 1; decode = value\n", "no_decode"),
    "lookalike": ("def decode_all(payload):\n    return None\ndecoded = 1\n# def decode(\n", "no_decode"),
    "docstring": (
        '"""Example:\n\ndef decode(payload, context):\n    return None\n"""\nMARKER = 1\n',
        "no_decode",
    ),
    "keyword": ("def make(**kw):\n    return kw\nCODEC = make(\ndecode = make,\n)\n", "no_decode"),
    # A replacement field over lines in an f-string: its keyword argument binds nothing.
    "fstring": (
        'def func(**kw):\n    return 1\nTEXT = f"""{func(\ndecode=lambda: None\n)}"""\n',
        "no_decode",
    ),
    # A star import binds names only running the module would show.
    "starimport": ("from .plain import *\n", "no_decode"),
    # Statements after `break` or `continue` never run.
    "afterbreak": ("for item in (1,):\n    break\n    decode = print\n", "no_decode"),
    "aftercontinue": ("for item in (1,):\n    continue\n    decode = print\n", "no_decode"),
    # A generator's body and a lambda's body run later, if ever.
    "generatorbody": ("items = ((decode := print) for _ in (1,))\n", "no_decode"),
    "lambdabody": ("helper = lambda: (decode := print)\n", "no_decode"),
    # Starred unpacking keeps the known values before and after the star.
    "starredprefix": ("decode, *rest = (None, 1)\n", "not_callable"),
    "starredbetween": ("first, *middle, decode = (print, 1, 2, None)\n", "not_callable"),
    # Only when run as a script; a module whose top level raises does not import.
    "mainonly": (
        "if __name__ == '__main__':\n    def decode(payload, context):\n        return None\n",
        "no_decode",
    ),
    "raises": ("def decode(payload, context):\n    return None\nraise ImportError('no')\n", "no_decode"),
}


class AnalysisTests(unittest.TestCase):
    def test_decoders(self):
        for name, text in DECODERS.items():
            with self.subTest(name):
                self.assertEqual(verdict(text), (True, ""))

    def test_not_decoders(self):
        for name, (text, reason) in NOT_DECODERS.items():
            with self.subTest(name):
                self.assertEqual(verdict(text), (False, reason))

    @unittest.skipIf(sys.version_info < (3, 10), "match statements are Python 3.10")
    def test_match_statement(self):
        self.assertEqual(
            verdict(
                "import sys\nmatch sys.platform:\n    case 'win32':\n"
                "        def decode(payload, context):\n            return None\n"
                "    case _:\n        decode = None\n"
            ),
            (True, ""),
        )
        # A case that captures `decode` binds what the subject holds; a star capture a list.
        self.assertEqual(
            verdict("match (print,):\n    case [decode]:\n        pass\n"), (True, "")
        )
        self.assertEqual(
            verdict("match (print,):\n    case [*decode]:\n        pass\n"),
            (False, "not_callable"),
        )
        # A guard runs before the case's body, in the module's scope.
        self.assertEqual(
            verdict("match 1:\n    case _ if (decode := print):\n        pass\n"), (True, "")
        )

    @unittest.skipIf(sys.version_info < (3, 12), "generic functions and f-strings are Python 3.12")
    def test_python_312_syntax(self):
        self.assertEqual(verdict("def decode[T](payload: T, context):\n    return None\n"), (True, ""))
        self.assertEqual(
            verdict(
                "def decode[T: tuple[int, str]](payload: T, context):\n    return None\n"
            ),
            (True, ""),
        )
        self.assertEqual(verdict("type decode = int\n"), (False, "not_callable"))
        # PEP 701: a replacement field over lines inside a one-quote f-string.
        self.assertEqual(
            verdict('def func(**kw):\n    return 1\nTEXT = f"{func(\ndecode=lambda: None\n)}"\n'),
            (False, "no_decode"),
        )

    def test_syntax_errors_name_their_line(self):
        entry, reason, line, detail = entries.check_source(
            source("MARKER = 1\ndef decode(:\n    return None\n"), "module.py"
        )
        self.assertEqual((entry, reason, line), (False, "syntax_error", 2))
        self.assertTrue(detail)
        self.assertLessEqual(len(detail), entries.DETAIL_LIMIT)
        # What the compiler refuses does not import either.
        entry, reason, line, _detail = entries.check_source(
            source("return 1\ndef decode(payload, context):\n    return None\n"), "module.py"
        )
        self.assertEqual((entry, reason, line), (False, "syntax_error", 1))
        # A null byte, a bad encoding cookie.
        self.assertEqual(verdict(b"def decode(p, c):\n    return None\n\x00\n")[1], "syntax_error")
        self.assertEqual(
            verdict(b"# -*- coding: nonesuch -*-\ndef decode(p, c):\n    return None\n")[1],
            "syntax_error",
        )

    def test_nothing_in_the_module_runs(self):
        with tempfile.TemporaryDirectory() as folder:
            marker = Path(folder) / "ran"
            text = "open(%r, 'w').close()\ndef decode(payload, context):\n    return None\n" % (
                str(marker),
            )
            self.assertEqual(verdict(text), (True, ""))
            self.assertFalse(marker.exists())


class ModuleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)

    def evaluate(self, packs, **changes):
        return entries.evaluate(request(packs, **changes), time.monotonic())

    def test_each_listed_module_in_order(self):
        pack = make_pack(
            self.base,
            "org.example.a",
            {"summary": "def decode(payload, context):\n    return None\n", "common": "X = 1\n"},
        )
        answer = self.evaluate([pack])
        self.assertEqual((answer["state"], answer["backend"]), ("completed", "python_entries"))
        self.assertEqual(
            [(module["decoder"], module["entry"], module["reason"]) for module in answer["modules"]],
            [("org.example.a/summary", True, ""), ("org.example.a/common", False, "no_decode")],
        )

    def test_a_large_module_within_the_deadline(self):
        # One line of 60,000 statements in a one-line body (about 300 KB), then a decode(): Python
        # parses and compiles it in about a second, well within the check's 10 s.
        text = "def helper(): " + "x=1; " * 60000 + "\ndef decode(payload, context):\n    return None\n"
        pack = make_pack(self.base, "org.example.a", {"long": text})
        started = time.monotonic()
        answer = self.evaluate([pack])
        self.assertLess(time.monotonic() - started, 10)
        self.assertTrue(answer["modules"][0]["entry"], answer)

    def test_a_module_over_the_size_limit(self):
        text = "X = 1\n" * (entries.MODULE_FILE_LIMIT // 6 + 1)
        pack = make_pack(self.base, "org.example.a", {"big": text})
        self.assertEqual(self.evaluate([pack])["modules"][0]["reason"], "too_large")

    def test_a_missing_module(self):
        pack = make_pack(self.base, "org.example.a", {"present": "X = 1\n"})
        pack["decoders"].append({"id": "missing", "module": "python/missing.py"})
        reasons = [module["reason"] for module in self.evaluate([pack])["modules"]]
        self.assertEqual(reasons, ["no_decode", "unreadable"])

    def test_past_the_deadline_the_rest_wait_for_another_check(self):
        pack = make_pack(self.base, "org.example.a", {"one": "X = 1\n", "two": "X = 2\n"})
        answer = entries.evaluate(request([pack], timeoutMs=1), time.monotonic() - 10)
        self.assertEqual([module["reason"] for module in answer["modules"]], ["timeout", "timeout"])

    def test_invalid_requests(self):
        pack = make_pack(self.base, "org.example.a", {"one": "X = 1\n"})
        for changes in (
            {"op": "decode"},
            {"version": 2},
            {"timeoutMs": 0},
            {"payloadBase64": "not base64"},
            {"payloadBase64": base64.b64encode(b'{"packs": 1}').decode("ascii")},
        ):
            with self.subTest(changes):
                with self.assertRaises(ProofError):
                    self.evaluate([pack], **changes)
        with self.assertRaises(ProofError):
            entries.evaluate({"version": 1}, time.monotonic())


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

    def exchange(self, value):
        process = subprocess.run(
            [sys.executable, "-I", "-S", "-u", str(WORKER), "--workspace", str(self.base)],
            input=frame(value),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
        )
        stream = __import__("io").BytesIO(process.stdout)
        answer = read_frame(stream)
        # One answer, nothing else on stdout.
        self.assertEqual(stream.read(), b"")
        return process.returncode, answer, process.stderr

    def test_answers_once_and_exits(self):
        pack = make_pack(
            self.base,
            "org.example.a",
            {"summary": "print('never')\ndef decode(payload, context):\n    return None\n"},
        )
        code, answer, diagnostics = self.exchange(request([pack]))
        self.assertEqual(code, 0, diagnostics)
        self.assertEqual(answer["id"], "check-1")
        self.assertEqual(answer["modules"][0]["entry"], True)
        # The module's code never ran, and its text is not in the diagnostics.
        self.assertNotIn(b"never", diagnostics)

    def test_a_refused_request_fails_with_its_reason(self):
        code, answer, _diagnostics = self.exchange(request([], op="decode"))
        self.assertEqual(code, 1)
        self.assertEqual((answer["state"], answer["reason"]), ("failed", "invalid_operation"))
        self.assertEqual(answer["modules"], [])


if __name__ == "__main__":
    unittest.main()
