"""Private framed-stdio worker that runs configuration pack Python decoders (MD03b).

The native host starts one worker per preview session inside a process group or job object of
its own, and supplies the trusted profile: the enabled packs' decoder modules. The worker copies
and hashes those modules when the session starts, gives each pack an import namespace of its own,
and calls `decode(payload, context)` of the routed decoder. Results become PreviewService result
v1 rows within the MD03a limits. Pack Python is local code with the user's permissions: the
process boundary makes failures and deadlines recoverable, it is not a sandbox.

Diagnostics go to stderr and carry metadata only (ids, counts, reasons), never payload bytes or
decoded values.
"""

import argparse
import base64
import binascii
import hashlib
import importlib.machinery
import importlib.util
import itertools
import json
import logging
import os
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile
import time
import types

# -I -S deliberately omits the script directory. Resources are materialized together by the
# native host; source-tree execution retains the same imports.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from message_decoder_wire import ProofError  # noqa: E402
from message_decoder_preview_worker import (  # noqa: E402
    PAYLOAD_LIMIT,
    _fit_response,
    encode_frame,
    read_frame,
    write_before,
)

LOG = logging.getLogger("cilogg.decoder.python")
# A Python decoder's response body (MD03b): 128 KiB, apart from the TShark worker's result limit.
RESPONSE_LIMIT = 128 * 1024
ORIGIN = "decoder_payload"
REQUEST_ID = re.compile(r"[A-Za-z0-9_.:-]{1,128}")
PACK_ID = re.compile(r"[a-z0-9][a-z0-9._-]{0,127}")
DECODER_ID = re.compile(r"[a-z0-9][a-z0-9._-]{0,63}")
# A pack lists at most 32 modules (MaxPackPythonDecoders); a session holds at most 64 in all.
PACK_DECODER_LIMIT = 32
PROFILE_DECODER_LIMIT = 64
MODULE_FILE_LIMIT = 1024 * 1024
MODULE_PATH_LIMIT = 256
# The MD03a result limits: 512 rows, depth 32, 64 Ki characters per text, 64 attributes.
FIELD_LIMIT = 512
DEPTH_LIMIT = 32
TEXT_LIMIT = 64 * 1024
ATTRIBUTE_LIMIT = 64
ATTRIBUTE_KEY_LIMIT = 128
DETAIL_LIMIT = 128
# How the log text became the payload: the route's or request's encoding, or the buffer type.
ENCODINGS = {"utf8", "hex", "base64", "bin", "bytes"}
DIRECTIONS = {"unknown", "rx", "tx"}
COMPLETENESS = {"complete", "incomplete", "not_reported"}
INTEGRITY = {"valid", "invalid", "not_reported"}
ROW_KEYS = {"name", "value", "raw", "pos", "size", "depth", "attributes"}
RESULT_KEYS = {"fields", "messageCompleteness", "messageIntegrity"}
_SESSIONS = itertools.count()


class InvalidResult(Exception):
    """A decoder result outside the contract; the message names the place, not the data."""


def utf16_length(text):
    """The length the native host measures (QString): UTF-16 code units."""
    return len(text.encode("utf-16-le", "surrogatepass")) // 2


def utf16_prefix(text, limit):
    """The longest prefix of at most `limit` UTF-16 code units, never half a character."""
    if utf16_length(text) <= limit:
        return text
    return text.encode("utf-16-le", "surrogatepass")[: limit * 2].decode("utf-16-le", "ignore")


def clean_text(text):
    """Text the host's JSON parser takes: a surrogate pair written as two code points becomes one
    character, and a lone surrogate (from surrogateescape decoding, say) U+FFFD."""
    return text.encode("utf-16-le", "surrogatepass").decode("utf-16-le", "replace")


def bounded_text(value, limit):
    return type(value) is str and utf16_length(value) <= limit and "\x00" not in value


def module_path(value):
    """A pack-relative module path: forward slashes, no parent steps, a .py file."""
    if not bounded_text(value, MODULE_PATH_LIMIT) or "\\" in value or ":" in value:
        return False
    path = PurePosixPath(value)
    # The suffix in any case, as the pack validator (configpack.cpp) takes it.
    return (
        not path.is_absolute()
        and all(part not in ("", ".", "..") for part in value.split("/"))
        and path.suffix.lower() == ".py"
    )


def validate_profile(profile):
    if type(profile) is not dict or set(profile) != {"packs"} or type(profile["packs"]) is not list:
        raise ProofError("invalid_profile")
    targets = set()
    seen = set()
    count = 0
    for pack in profile["packs"]:
        if type(pack) is not dict or set(pack) != {"id", "root", "decoders"}:
            raise ProofError("invalid_profile")
        if type(pack["id"]) is not str or not PACK_ID.fullmatch(pack["id"]) or pack["id"] in seen:
            raise ProofError("invalid_profile")
        seen.add(pack["id"])
        if not bounded_text(pack["root"], 32768) or not Path(pack["root"]).is_absolute():
            raise ProofError("invalid_profile")
        decoders = pack["decoders"]
        if type(decoders) is not list or not 1 <= len(decoders) <= PACK_DECODER_LIMIT:
            raise ProofError("input_limit")
        count += len(decoders)
        if count > PROFILE_DECODER_LIMIT:
            raise ProofError("input_limit")
        ids = set()
        for decoder in decoders:
            if type(decoder) is not dict or set(decoder) != {"id", "module"}:
                raise ProofError("invalid_profile")
            if (
                type(decoder["id"]) is not str
                or not DECODER_ID.fullmatch(decoder["id"])
                or decoder["id"] in ids
                or not module_path(decoder["module"])
            ):
                raise ProofError("invalid_profile")
            ids.add(decoder["id"])
            targets.add(pack["id"] + "/" + decoder["id"])
    return targets


def package_name(package, parts):
    """The import name of a path below the pack's package, one name part per path part. Only `%`
    and `.` are escaped, so distinct paths never share a name."""
    return ".".join([package] + [part.replace("%", "%25").replace(".", "%2E") for part in parts])


def module_name(package, module):
    """The import name of a pack module (its folders are registered packages, see start())."""
    return package_name(package, PurePosixPath(module).with_suffix("").parts)


def pack_relative(filename, folder):
    """`filename` as a path inside the pack copy `folder` (forward slashes), or None."""
    try:
        return Path(filename).relative_to(folder).as_posix()
    except ValueError:
        return None


def validate_context(context):
    if type(context) is not dict or set(context) != {"preview", "protocol", "inputEncoding", "direction"}:
        raise ProofError("invalid_context")
    if (
        not bounded_text(context["preview"], 4096)
        or not bounded_text(context["protocol"], 128)
        or context["inputEncoding"] not in ENCODINGS
        or context["direction"] not in DIRECTIONS
    ):
        raise ProofError("invalid_context")


def validate_request(value):
    """The decode request; returns the payload bytes or raises ProofError (fatal)."""
    keys = {"version", "id", "op", "profile", "decoder", "context", "payloadBase64", "timeoutMs"}
    if type(value) is not dict or set(value) != keys:
        raise ProofError("invalid_request")
    if type(value["version"]) is not int or value["version"] != 1:
        raise ProofError("unsupported_version")
    if type(value["id"]) is not str or not REQUEST_ID.fullmatch(value["id"]):
        raise ProofError("invalid_id")
    if type(value["op"]) is not str or value["op"] != "decode":
        raise ProofError("invalid_operation")
    if type(value["timeoutMs"]) is not int or not 1 <= value["timeoutMs"] <= 10000:
        raise ProofError("invalid_limits")
    targets = validate_profile(value["profile"])
    if type(value["decoder"]) is not str or value["decoder"] not in targets:
        raise ProofError("invalid_decoder")
    validate_context(value["context"])
    encoded = value["payloadBase64"]
    if type(encoded) is not str or len(encoded) > 4 * ((PAYLOAD_LIMIT + 2) // 3):
        raise ProofError("input_limit")
    try:
        payload = base64.b64decode(encoded, validate=True)
    except (ValueError, binascii.Error) as error:
        raise ProofError("invalid_payload") from error
    if base64.b64encode(payload).decode("ascii") != encoded:
        raise ProofError("invalid_payload")
    if not 0 < len(payload) <= PAYLOAD_LIMIT:
        raise ProofError("input_limit")
    return payload


def scalar_text(value, place):
    if value is None:
        return ""
    if type(value) is str:
        return value
    if type(value) is bool:
        return "true" if value else "false"
    if type(value) in (int, float):
        try:
            return str(value) if type(value) is int else repr(value)
        except ValueError as error:  # an integer beyond the interpreter's digit limit
            raise InvalidResult(place + ": number too long") from error
    raise InvalidResult(place + ": not text, a number, a boolean or None")


def raw_text(value, place):
    if type(value) in (bytes, bytearray, memoryview):
        return bytes(value).hex()
    if value is None or type(value) is str:
        return value or ""
    raise InvalidResult(place + ": raw is not bytes or text")


def position_text(value, place):
    if value is None or type(value) is str:
        return value or ""
    if type(value) is int:
        return str(value)
    raise InvalidResult(place + ": not an integer or text")


def normalize_result(result):
    """Rows of PreviewService result v1 from a decoder result: a list of rows, or an object with
    `fields` and the optional message states. Returns (fields, limit reason, completeness,
    integrity); raises InvalidResult."""
    if type(result) in (list, tuple):
        rows, states = result, {}
    elif type(result) is dict:
        unknown = set(result) - RESULT_KEYS
        if unknown or type(result.get("fields")) not in (list, tuple):
            raise InvalidResult("result: an object needs `fields` and only the message states")
        rows, states = result["fields"], result
    else:
        raise InvalidResult("result: not a list of rows or an object with fields")
    completeness = states.get("messageCompleteness", "not_reported")
    integrity = states.get("messageIntegrity", "not_reported")
    if completeness not in COMPLETENESS or integrity not in INTEGRITY:
        raise InvalidResult("result: unknown message state")
    fields = []
    reasons = []
    previous = -1

    def limit(reason):
        if reason not in reasons:
            reasons.append(reason)

    def cut(text):
        text = clean_text(text)
        prefix = utf16_prefix(text, TEXT_LIMIT)
        if prefix != text:
            limit("field_text_limit")
        return prefix

    for index, row in enumerate(rows):
        place = "row %d" % index
        if len(fields) == FIELD_LIMIT:
            limit("rendered_field_limit")
            break
        if type(row) is not dict or not set(row) <= ROW_KEYS or type(row.get("name")) is not str:
            raise InvalidResult(place + ": a row is an object with a text name")
        depth = row.get("depth", 0)
        if type(depth) is not int or depth < 0 or depth > previous + 1:
            raise InvalidResult(place + ": depth must be 0 to one more than the previous row's")
        previous = depth
        if depth > DEPTH_LIMIT:
            limit("depth_limit")
            continue
        attributes = row.get("attributes", {})
        if type(attributes) is not dict or len(attributes) > ATTRIBUTE_LIMIT:
            raise InvalidResult(place + ": attributes are an object of at most 64 entries")
        converted = {}
        for key, value in attributes.items():
            name = clean_text(key) if type(key) is str else None
            if name is None or not bounded_text(name, ATTRIBUTE_KEY_LIMIT):
                raise InvalidResult(place + ": attribute names are text of at most 128 characters")
            if name in converted:
                raise InvalidResult(place + ": two attribute names are the same text")
            converted[name] = cut(scalar_text(value, place + " attribute"))
        fields.append(
            {
                "columns": [
                    cut(row["name"]),
                    cut(scalar_text(row.get("value"), place + " value")),
                    cut(raw_text(row.get("raw"), place)),
                    cut(position_text(row.get("pos"), place + " pos")),
                    cut(position_text(row.get("size"), place + " size")),
                ],
                "depth": depth,
                "attributes": converted,
                "positionOrigin": ORIGIN,
                "id": str(index),
            }
        )
    order = ["rendered_field_limit", "depth_limit", "field_text_limit"]
    reason = min(reasons, key=order.index) if reasons else ""
    return fields, reason, completeness, integrity


def response_base(identity, request):
    context = request.get("context") if type(request) is dict else None
    context = context if type(context) is dict else {}
    decoder = request.get("decoder", "") if type(request) is dict else ""
    decoder = decoder if bounded_text(decoder, 256) else ""
    name = context.get("preview") or context.get("protocol") or decoder
    return {
        "version": 1,
        "id": identity,
        "backend": "python_decoder",
        "state": "failed",
        "reason": "",
        "available": False,
        "complete": False,
        "limited": False,
        "previewName": name if bounded_text(name, 128) else "",
        "decoder": decoder,
        "offsetSpace": ORIGIN,
        "messageCompleteness": "not_reported",
        "messageIntegrity": "not_reported",
        "fields": [],
    }


class DecoderSession:
    """One preview session: the copied pack modules and their import namespaces."""

    def __init__(self, workspace=None):
        self.temporary = tempfile.TemporaryDirectory(prefix="pydecoder-", dir=workspace)
        self.root = Path(self.temporary.name).resolve()
        # Unique per session, so sessions in one process (tests) never share modules.
        self.prefix = "cilogg_pack_%d_%d" % (os.getpid(), next(_SESSIONS))
        self.identity = None
        self.decoders = {}
        self.loaded = {}

    def copy_module(self, root, module, folder, deadline):
        if time.monotonic() > deadline:
            raise ProofError("execution_timeout")
        source = Path(root) / module
        if source.is_symlink() or not source.is_file():
            raise ProofError("input_missing")
        try:
            source.resolve().relative_to(Path(root).resolve())
        except ValueError as error:
            raise ProofError("invalid_profile") from error
        with source.open("rb") as handle:
            body = handle.read(MODULE_FILE_LIMIT + 1)
        if len(body) > MODULE_FILE_LIMIT:
            raise ProofError("input_limit")
        # The copy ends in .py, whatever the case of the pack's suffix, so the import system
        # finds it for a relative import too.
        copy = folder / PurePosixPath(module).with_suffix(".py")
        if copy.exists():
            raise ProofError("invalid_profile")  # two listed paths that differ only in case
        copy.parent.mkdir(parents=True, exist_ok=True)
        copy.write_bytes(body)
        return copy, hashlib.sha256(body).hexdigest()

    def start(self, profile, deadline):
        identity = json.dumps(profile, sort_keys=True)
        if self.identity is not None:
            if identity != self.identity:
                raise ProofError("session_configuration_changed")
            return
        decoders = {}
        for index, pack in enumerate(profile["packs"]):
            # Only a new session reads the pack's folder; a warm one runs its copies.
            if not Path(pack["root"]).is_dir():
                raise ProofError("input_missing")
            folder = self.root / ("pack-%d" % index)
            folder.mkdir()
            package = "%s_%d" % (self.prefix, index)
            copies = {}
            spellings = set()
            for decoder in pack["decoders"]:
                module = decoder["module"]
                if module not in copies:
                    # Paths that differ only in case are one file on Windows and macOS: refuse
                    # them everywhere, as the pack validator does.
                    spelling = PurePosixPath(module).with_suffix("").as_posix().lower()
                    if spelling in spellings:
                        raise ProofError("invalid_profile")
                    spellings.add(spelling)
                    copies[module] = self.copy_module(pack["root"], module, folder, deadline)
                copy, digest = copies[module]
                decoders[pack["id"] + "/" + decoder["id"]] = {
                    "name": module_name(package, module),
                    "module": module,
                    "copy": copy,
                    "folder": folder,
                    "sha256": digest,
                }
            # The pack's namespace: a package whose path is the pack's copy, so a module reaches
            # the pack's other listed modules by relative imports, and nothing of another pack.
            # Each folder of a listed module is a package of its own whose path is the real
            # folder, so a folder name with `.` or `%` (escaped in the import name) works too.
            folders = {package: folder}
            for module in copies:
                parts = PurePosixPath(module).parts[:-1]
                for depth in range(1, len(parts) + 1):
                    folders[package_name(package, parts[:depth])] = folder.joinpath(*parts[:depth])
            modules = {module_name(package, module) for module in copies}
            if modules & set(folders):
                raise ProofError("invalid_profile")  # a module and a folder of one name
            for name, path in folders.items():
                self.register_package(name, path)
        self.decoders = decoders
        self.identity = identity
        LOG.debug(
            "python_decoder_session_started packs=%d modules=%d",
            len(profile["packs"]),
            len({entry["copy"] for entry in decoders.values()}),
        )

    @staticmethod
    def register_package(name, path):
        package = types.ModuleType(name)
        package.__path__ = [str(path)]
        package.__package__ = name
        package.__spec__ = importlib.machinery.ModuleSpec(name, None, is_package=True)
        package.__spec__.submodule_search_locations = [str(path)]
        sys.modules[name] = package

    def describe(self, error, entry):
        """The exception type and the innermost place in the pack's code, never its message."""
        place = ""
        folder = entry["folder"]
        if isinstance(error, SyntaxError) and error.filename:
            relative = pack_relative(error.filename, folder)
            if relative is not None:
                place = "%s:%s" % (relative, error.lineno)
        trace = error.__traceback__
        while trace is not None:
            relative = pack_relative(trace.tb_frame.f_code.co_filename, folder)
            if relative is not None:
                place = "%s:%d" % (relative, trace.tb_lineno)
            trace = trace.tb_next
        text = type(error).__name__ + (" at " + place if place else "")
        return utf16_prefix(text, DETAIL_LIMIT)

    def load(self, target):
        """The decode function of `target`, or (None, detail) when its module cannot load."""
        if target in self.loaded:
            return self.loaded[target], ""
        entry = self.decoders[target]
        name = entry["name"]
        try:
            module = sys.modules.get(name)
            if module is None:
                spec = importlib.util.spec_from_file_location(name, str(entry["copy"]))
                module = importlib.util.module_from_spec(spec)
                sys.modules[name] = module
                try:
                    spec.loader.exec_module(module)
                except BaseException:
                    sys.modules.pop(name, None)
                    raise
        except BaseException as error:  # anything the module raises, KeyboardInterrupt too
            LOG.warning("python_decoder_load_failed decoder=%s error=%s", target, type(error).__name__)
            return None, self.describe(error, entry)
        function = getattr(module, "decode", None)
        if not callable(function):
            LOG.warning("python_decoder_without_entry_point decoder=%s", target)
            return None, "the module has no decode(payload, context) function"
        self.loaded[target] = function
        return function, ""

    def evaluate(self, request):
        """Decodes one request. Returns the response; raises ProofError for a channel failure."""
        started = time.monotonic()
        payload = validate_request(request)
        deadline = started + request["timeoutMs"] / 1000
        self.start(request["profile"], deadline)
        target = request["decoder"]
        entry = self.decoders[target]
        response = response_base(request["id"], request)
        response.update(enginePid=os.getpid(), moduleSha256=entry["sha256"])
        function, detail = self.load(target)
        if function is None:
            response.update(reason="decoder_load_failed", detail=detail)
            return response
        context = dict(request["context"], decoder=target)
        try:
            result = function(payload, context)
        except BaseException as error:  # a decoder failure, whatever it raises
            LOG.warning("python_decoder_failed id=%s decoder=%s error=%s",
                        request["id"], target, type(error).__name__)
            response.update(reason="decoder_failed", detail=self.describe(error, entry))
            return response
        if time.monotonic() > deadline:
            response["reason"] = "execution_timeout"
            return response
        if result is None:
            LOG.debug("python_decoder_not_dispatched id=%s decoder=%s", request["id"], target)
            response["reason"] = "decoder_not_dispatched"
            return response
        try:
            fields, reason, completeness, integrity = normalize_result(result)
        except InvalidResult as error:
            LOG.warning("python_decoder_invalid_result id=%s decoder=%s", request["id"], target)
            response.update(reason="invalid_decoder_result", detail=utf16_prefix(str(error), DETAIL_LIMIT))
            return response
        response.update(
            state="completed",
            available=True,
            complete=not reason,
            limited=bool(reason),
            reason=reason,
            fields=fields,
            messageCompleteness=completeness,
            messageIntegrity=integrity,
        )
        response = _fit_response(response, RESPONSE_LIMIT)
        # The result contract of MD03b names no limit; the reason says it was cut off.
        response.pop("limit", None)
        LOG.info(
            "python_decoder_evaluated id=%s decoder=%s payload_bytes=%d fields=%d limited=%s",
            request["id"],
            target,
            len(payload),
            len(response["fields"]),
            response["limited"],
        )
        return response

    def close(self):
        own = self.prefix + "_"  # the packages are <prefix>_<index>; another session's differ
        for name in [name for name in sys.modules if name.startswith(own)]:
            del sys.modules[name]
        try:
            self.temporary.cleanup()
        except OSError:
            LOG.warning("python_decoder_workspace_not_removed")
        LOG.debug("python_decoder_session_closed")


def claim_stdio():
    """Keeps the real stdin and stdout for frames only (non-inheritable duplicates). Decoder
    output, print() or a child process's, goes to stderr, and child processes read nothing."""
    sys.stdout.flush()
    protocol_in = os.fdopen(os.dup(0), "rb", buffering=0)
    protocol_out = os.fdopen(os.dup(1), "wb", buffering=0)
    os.dup2(2, 1)
    null = os.open(os.devnull, os.O_RDONLY)
    os.dup2(null, 0)
    os.close(null)
    if sys.platform == "win32":
        # Child processes take the Win32 standard handles; point them at the new descriptors.
        import ctypes
        import msvcrt

        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        set_std_handle = kernel32.SetStdHandle
        set_std_handle.argtypes = [wintypes.DWORD, wintypes.HANDLE]
        set_std_handle.restype = wintypes.BOOL
        for std, fd in ((0xFFFFFFF5, 1), (0xFFFFFFF6, 0)):  # STD_OUTPUT_HANDLE, STD_INPUT_HANDLE
            if not set_std_handle(std, msvcrt.get_osfhandle(fd)):
                raise OSError(ctypes.get_last_error(), "SetStdHandle failed")
    sys.stdout = sys.stderr
    return protocol_in, protocol_out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path)
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG, format="%(levelname)s %(name)s %(message)s")
    sys.dont_write_bytecode = True
    protocol_in, protocol_out = claim_stdio()
    session = DecoderSession(args.workspace)
    try:
        while True:
            request = None
            try:
                request = read_frame(protocol_in)
                if request is None:
                    return 0
                response = session.evaluate(request)
            except (ProofError, OSError, ValueError, RecursionError) as error:
                reason = str(error) if isinstance(error, ProofError) else "worker_io_error"
                LOG.warning("python_decoder_channel_failed reason=%s", reason)
                identity = request.get("id", "") if type(request) is dict else ""
                if type(identity) is str and REQUEST_ID.fullmatch(identity):
                    response = response_base(identity, request)
                    response["reason"] = reason
                    response["limited"] = reason in {"request_limit", "input_limit"}
                    write_before(protocol_out, encode_frame(response, RESPONSE_LIMIT),
                                 time.monotonic() + 1)
                return 1  # Fatal: the host reaps the channel's group or job.
            # The host enforces the request's deadline; a late answer (execution_timeout) is
            # still written whole, within a bound of its own, so the channel stays in step.
            write_before(protocol_out, encode_frame(response, RESPONSE_LIMIT), time.monotonic() + 1)
    finally:
        session.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ProofError, OSError, ValueError) as error:
        LOG.error(
            "python_decoder_worker_failed reason=%s",
            str(error) if isinstance(error, ProofError) else "worker_io_error",
        )
        raise SystemExit(1)
