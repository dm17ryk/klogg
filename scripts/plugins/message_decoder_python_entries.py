"""Private framed-stdio worker that finds the decoders among configuration packs' Python modules
(E16b, docs/plugin-repositories.md 4.4).

When the enabled packs change, the native host starts it once, in a process group or job object
of its own, with the packs' modules as the request's payload. For each module it reads the file
(at most 1 MiB) and gives it to Python's own parser and compiler: nothing in the module runs and
nothing is imported. A module is a decoder when, after its top level, `decode` may be bound to
something callable: the last binding wins, and a binding in a branch of an `if`, `try`, `for`,
`while`, `with` or `match` counts when some path through them keeps it. It answers once and exits.

Diagnostics go to stderr and carry metadata only (ids, counts, reasons), never module text.
"""

import argparse
import ast
import base64
import binascii
import json
import logging
import sys
import time
import warnings
from pathlib import Path

# -I -S deliberately omits the script directory. Resources are materialized together by the
# native host; source-tree execution retains the same imports.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from message_decoder_wire import ProofError  # noqa: E402
from message_decoder_preview_worker import (  # noqa: E402
    PAYLOAD_LIMIT,
    encode_frame,
    invalid_constant,
    read_frame,
    unique_object,
    write_before,
)
from message_decoder_python_worker import (  # noqa: E402
    MODULE_FILE_LIMIT,
    REQUEST_ID,
    claim_stdio,
    clean_text,
    utf16_prefix,
    validate_profile,
)

LOG = logging.getLogger("cilogg.decoder.entries")
BACKEND = "python_entries"
NAME = "decode"
DETAIL_LIMIT = 128
# Time kept for the answer: the host ends the worker at the request's deadline.
ANSWER_MARGIN = 0.5

# What `decode` is at some point of a path through the module's top level.
UNBOUND = "unbound"
CALLABLE = "callable"  # a function, or a value that may be callable
ASYNC = "async"  # an async def: the preview calls decode() and never awaits it
OTHER = "other"  # a class, a module or a value that is never callable

# Values that are never callable: literals, displays, comprehensions, f-strings and comparisons.
NOT_CALLABLE = (
    ast.Constant,
    ast.JoinedStr,
    ast.List,
    ast.Tuple,
    ast.Set,
    ast.Dict,
    ast.ListComp,
    ast.SetComp,
    ast.DictComp,
    ast.GeneratorExp,
    ast.Compare,
)
# What a starred target takes: a list.
A_LIST = ast.List(elts=[], ctx=ast.Load())
# Statement fields that hold statements, not this statement's own expressions.
BODIES = {"body", "orelse", "finalbody", "handlers", "cases"}
# Statements of newer Pythons than the oldest one CILogg runs with (3.9).
TRY_STAR = getattr(ast, "TryStar", None)
MATCH = getattr(ast, "Match", None)
TYPE_ALIAS = getattr(ast, "TypeAlias", None)
TRIES = (ast.Try,) + ((TRY_STAR,) if TRY_STAR else ())


def kind(value):
    """What `decode = value` makes `decode`: callable unless the value surely is not."""
    if value is None or isinstance(value, ast.Lambda):
        return frozenset({CALLABLE})
    if isinstance(value, ast.IfExp):
        return kind(value.body) | kind(value.orelse)
    if isinstance(value, ast.NamedExpr):
        return kind(value.value)
    if isinstance(value, NOT_CALLABLE) or (
        isinstance(value, ast.UnaryOp) and isinstance(value.op, ast.Not)
    ):
        return frozenset({OTHER})
    # A name, an attribute, a call, a subscript...: what it holds is not known here.
    return frozenset({CALLABLE})


def assign(target, value, states):
    """`target = value`, element by element when both are displays of one length; `value` None is
    a value not known here."""
    if isinstance(target, (ast.Tuple, ast.List)):
        items = value.elts if isinstance(value, (ast.Tuple, ast.List)) else None
        if (
            items is None
            or len(items) != len(target.elts)
            or any(isinstance(node, ast.Starred) for node in items + target.elts)
        ):
            items = [None] * len(target.elts)
        for element, item in zip(target.elts, items):
            states = assign(element, item, states)
        return states
    if isinstance(target, ast.Starred):
        return assign(target.value, A_LIST, states)
    if isinstance(target, ast.Name) and target.id == NAME:
        return kind(value)
    return states  # an attribute or a subscript binds no name


def deletes(target):
    if isinstance(target, ast.Name):
        return target.id == NAME
    if isinstance(target, (ast.Tuple, ast.List)):
        return any(deletes(element) for element in target.elts)
    return False  # `del holder.decode` or `del items[decode]` leaves the name


def named_expressions(node):
    """The `:=` expressions in `node` that bind in the module's scope (a lambda has its own), in
    source order."""
    stack = [node]
    while stack:
        current = stack.pop()
        if isinstance(current, ast.Lambda):
            continue
        if isinstance(current, ast.NamedExpr):
            yield current
        stack.extend(reversed(list(ast.iter_child_nodes(current))))


def own_expressions(node):
    """A statement's expressions, without the statements of its blocks."""
    for field, value in ast.iter_fields(node):
        if field in BODIES:
            continue
        for item in value if isinstance(value, list) else [value]:
            if isinstance(item, ast.AST):
                yield item


def captures(pattern):
    """The names a `case` pattern binds, with what each takes."""
    for node in ast.walk(pattern):
        name = getattr(node, "name", None)
        if type(node).__name__ == "MatchAs" and name:
            yield name, frozenset({CALLABLE})  # the subject or a part of it
        elif type(node).__name__ == "MatchStar" and name:
            yield name, frozenset({OTHER})  # a list
        elif type(node).__name__ == "MatchMapping" and getattr(node, "rest", None):
            yield node.rest, frozenset({OTHER})  # a dict


def run(body, states):
    for node in body:
        states = statement(node, states)
    return states


def statement(node, states):
    """What `decode` may be after `node`, from what it may be before it."""
    for expression in own_expressions(node):
        for named in named_expressions(expression):
            states = assign(named.target, named.value, states)
    if isinstance(node, ast.FunctionDef):
        return frozenset({CALLABLE}) if node.name == NAME else states
    if isinstance(node, ast.AsyncFunctionDef):
        # A decorator may make a plain function of it.
        return (
            frozenset({CALLABLE if node.decorator_list else ASYNC}) if node.name == NAME else states
        )
    if isinstance(node, ast.ClassDef):
        return frozenset({OTHER}) if node.name == NAME else states
    if isinstance(node, ast.Assign):
        for target in node.targets:
            states = assign(target, node.value, states)
        return states
    if isinstance(node, ast.AnnAssign):
        return assign(node.target, node.value, states) if node.value is not None else states
    if isinstance(node, ast.AugAssign):
        return assign(node.target, None, states)  # what the operator returns is not known
    if isinstance(node, ast.Delete):
        return frozenset({UNBOUND}) if any(deletes(target) for target in node.targets) else states
    if isinstance(node, ast.Import):
        for alias in node.names:
            if (alias.asname or alias.name.partition(".")[0]) == NAME:
                states = frozenset({OTHER})  # a module
        return states
    if isinstance(node, ast.ImportFrom):
        # `from module import *` binds names this check cannot see without running the module.
        for alias in node.names:
            if (alias.asname or alias.name) == NAME:
                states = frozenset({CALLABLE})
        return states
    if TYPE_ALIAS and isinstance(node, TYPE_ALIAS):
        return assign(node.name, A_LIST, states)  # a type alias is not callable
    if isinstance(node, ast.If):
        return run(node.body, states) | run(node.orelse, states)
    if isinstance(node, (ast.For, ast.AsyncFor, ast.While)):
        seen = states
        while True:  # at most four rounds: the states only grow
            start = seen
            if not isinstance(node, ast.While):
                start = assign(node.target, None, start)
            joined = seen | run(node.body, start)
            if joined == seen:
                break
            seen = joined
        return seen | run(node.orelse, seen)
    if isinstance(node, (ast.With, ast.AsyncWith)):
        for item in node.items:
            if item.optional_vars is not None:
                states = assign(item.optional_vars, None, states)
        return run(node.body, states)
    if isinstance(node, TRIES):
        # An exception may leave the body after any of its statements.
        seen = states
        after = states
        for inner in node.body:
            after = statement(inner, after)
            seen = seen | after
        outcomes = run(node.orelse, after)
        for handler in node.handlers:
            if handler.name == NAME:
                # `except ... as decode`: the name is deleted when the handler ends.
                outcomes = outcomes | frozenset({UNBOUND})
            else:
                outcomes = outcomes | run(handler.body, seen)
        return run(node.finalbody, outcomes)
    if MATCH and isinstance(node, MATCH):
        outcomes = states  # no case matched
        for case in node.cases:
            start = states
            for name, value in captures(case.pattern):
                if name == NAME:
                    start = value
            outcomes = outcomes | run(case.body, start)
        return outcomes
    return states


def verdict(states):
    """(entry, reason) from what `decode` may be at the end of the module."""
    if CALLABLE in states:
        return True, ""
    if ASYNC in states:
        return False, "async_decode"
    if OTHER in states:
        return False, "not_callable"
    return False, "no_decode"


def check_source(body, filename):
    """The module's entry, reason, line and detail, from its bytes."""
    try:
        # Warnings (an invalid escape, say) would quote the module on stderr.
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            tree = ast.parse(body, filename=filename)
            # Also what the compiler refuses (`return` outside a function, say): such a module
            # does not import. Nothing runs.
            compile(tree, filename, "exec", dont_inherit=True)
    except SyntaxError as error:
        detail = clean_text(utf16_prefix(str(error.msg or "invalid syntax"), DETAIL_LIMIT))
        return False, "syntax_error", max(0, int(error.lineno or 0)), detail
    except ValueError as error:  # a null byte, before Python 3.12
        return False, "syntax_error", 0, clean_text(utf16_prefix(str(error), DETAIL_LIMIT))
    except (RecursionError, MemoryError):
        return False, "too_complex", 0, ""
    try:
        entry, reason = verdict(run(tree.body, frozenset({UNBOUND})))
    except RecursionError:
        return False, "too_complex", 0, ""
    return entry, reason, 0, ""


def check_module(root, module, deadline):
    if time.monotonic() > deadline:
        return False, "timeout", 0, ""
    source = Path(root) / module
    try:
        if source.is_symlink() or not source.is_file():
            return False, "unreadable", 0, ""
        source.resolve().relative_to(Path(root).resolve())
        with source.open("rb") as handle:
            body = handle.read(MODULE_FILE_LIMIT + 1)
    except (OSError, ValueError):
        return False, "unreadable", 0, ""
    if len(body) > MODULE_FILE_LIMIT:
        return False, "too_large", 0, ""
    return check_source(body, module)


def validate_request(value):
    """The request; returns the profile (`{packs}`) or raises ProofError (fatal)."""
    keys = {"version", "id", "op", "payloadBase64", "timeoutMs"}
    if type(value) is not dict or set(value) != keys:
        raise ProofError("invalid_request")
    if type(value["version"]) is not int or value["version"] != 1:
        raise ProofError("unsupported_version")
    if type(value["id"]) is not str or not REQUEST_ID.fullmatch(value["id"]):
        raise ProofError("invalid_id")
    if type(value["op"]) is not str or value["op"] != "entries":
        raise ProofError("invalid_operation")
    if type(value["timeoutMs"]) is not int or not 1 <= value["timeoutMs"] <= 10000:
        raise ProofError("invalid_limits")
    encoded = value["payloadBase64"]
    if type(encoded) is not str or len(encoded) > 4 * ((PAYLOAD_LIMIT + 2) // 3):
        raise ProofError("input_limit")
    try:
        payload = base64.b64decode(encoded, validate=True)
    except (ValueError, binascii.Error) as error:
        raise ProofError("invalid_payload") from error
    if base64.b64encode(payload).decode("ascii") != encoded or not 0 < len(payload) <= PAYLOAD_LIMIT:
        raise ProofError("invalid_payload")
    try:
        profile = json.loads(
            payload.decode("utf-8"), object_pairs_hook=unique_object, parse_constant=invalid_constant
        )
    except (ValueError, UnicodeError, RecursionError) as error:
        raise ProofError("invalid_payload") from error
    validate_profile(profile)
    return profile


def evaluate(request, started):
    profile = validate_request(request)
    deadline = started + request["timeoutMs"] / 1000 - ANSWER_MARGIN
    modules = []
    checked = {}
    for pack in profile["packs"]:
        for decoder in pack["decoders"]:
            key = (pack["root"], decoder["module"])
            if key not in checked:
                checked[key] = check_module(pack["root"], decoder["module"], deadline)
            entry, reason, line, detail = checked[key]
            modules.append(
                {
                    "decoder": pack["id"] + "/" + decoder["id"],
                    "entry": entry,
                    "reason": reason,
                    "line": line,
                    "detail": detail,
                }
            )
    LOG.info(
        "python_entries_checked id=%s modules=%d entries=%d",
        request["id"],
        len(modules),
        sum(1 for module in modules if module["entry"]),
    )
    return response(request["id"], "completed", "", modules)


def response(identity, state, reason, modules):
    return {
        "version": 1,
        "id": identity,
        "backend": BACKEND,
        "state": state,
        "reason": reason,
        "modules": modules,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path)  # as the other workers take it; not used
    parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(name)s %(message)s")
    sys.dont_write_bytecode = True
    protocol_in, protocol_out = claim_stdio()
    request = None
    try:
        request = read_frame(protocol_in)
        if request is None:
            return 0
        answer = evaluate(request, time.monotonic())
    except (ProofError, OSError, ValueError, RecursionError) as error:
        reason = str(error) if isinstance(error, ProofError) else "worker_io_error"
        LOG.warning("python_entries_failed reason=%s", reason)
        identity = request.get("id", "") if type(request) is dict else ""
        if type(identity) is str and REQUEST_ID.fullmatch(identity):
            write_before(
                protocol_out,
                encode_frame(response(identity, "failed", reason, [])),
                time.monotonic() + 1,
            )
        return 1
    write_before(protocol_out, encode_frame(answer), time.monotonic() + 1)
    return 0  # One check per worker: the host starts a new one when the packs change again.


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ProofError, OSError, ValueError) as error:
        LOG.error(
            "python_entries_worker_failed reason=%s",
            str(error) if isinstance(error, ProofError) else "worker_io_error",
        )
