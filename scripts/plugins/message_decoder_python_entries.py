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
NOWHERE = frozenset()  # no path gets there

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
COMPREHENSIONS = (ast.ListComp, ast.SetComp, ast.DictComp, ast.GeneratorExp)
# Statements of newer Pythons than the oldest one CILogg runs with (3.9).
TRY_STAR = getattr(ast, "TryStar", None)
MATCH = getattr(ast, "Match", None)
TYPE_ALIAS = getattr(ast, "TypeAlias", None)
TRIES = (ast.Try,) + ((TRY_STAR,) if TRY_STAR else ())


class Flow:
    """How control may leave a block, with what `decode` may be then: falling through
    (`normal`), at a `break` or a `continue`; and at the start of every statement it ran (`seen`),
    where an exception may leave it. An empty set is a way no path takes."""

    __slots__ = ("normal", "breaks", "continues", "seen")

    def __init__(self, normal=NOWHERE, breaks=NOWHERE, continues=NOWHERE, seen=NOWHERE):
        self.normal = frozenset(normal)
        self.breaks = frozenset(breaks)
        self.continues = frozenset(continues)
        self.seen = frozenset(seen)

    def __or__(self, other):
        return Flow(
            self.normal | other.normal,
            self.breaks | other.breaks,
            self.continues | other.continues,
            self.seen | other.seen,
        )


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
    """`target = value`; `value` None is a value not known here."""
    if isinstance(target, (ast.Tuple, ast.List)):
        return unpack(target.elts, value, states)
    if isinstance(target, ast.Starred):
        return assign(target.value, A_LIST, states)
    if isinstance(target, ast.Name) and target.id == NAME:
        return kind(value)
    return states  # an attribute or a subscript binds no name


def unpack(targets, value, states):
    """`a, *b, c = value`: each target takes its element when `value` is a display Python unpacks
    the same way; a starred target takes a list. Targets bind from left to right."""
    items = None
    if isinstance(value, (ast.Tuple, ast.List)) and not any(
        isinstance(item, ast.Starred) for item in value.elts
    ):
        items = value.elts
    stars = [index for index, node in enumerate(targets) if isinstance(node, ast.Starred)]
    if items is not None and not stars and len(items) == len(targets):
        pairs = list(zip(targets, items))
    elif items is not None and len(stars) == 1 and len(items) >= len(targets) - 1:
        star = stars[0]
        tail = len(targets) - star - 1
        pairs = (
            list(zip(targets[:star], items[:star]))
            + [(targets[star], A_LIST)]
            + list(zip(targets[star + 1 :], items[len(items) - tail :]))
        )
    else:
        pairs = [(node, None) for node in targets]
    for node, item in pairs:
        states = assign(node, item, states)
    return states


def deletes(target):
    if isinstance(target, ast.Name):
        return target.id == NAME
    if isinstance(target, (ast.Tuple, ast.List)):
        return any(deletes(element) for element in target.elts)
    return False  # `del holder.decode` or `del items[decode]` leaves the name


def binds(node):
    """Whether `node` holds a `:=` (most expressions do not: they are not walked)."""
    return node is not None and any(isinstance(item, ast.NamedExpr) for item in ast.walk(node))


def expression(node, states):
    """What `decode` may be after Python evaluates `node`: its `:=` in evaluation order."""
    if not binds(node):
        return states
    if isinstance(node, ast.NamedExpr):
        return assign(node.target, node.value, expression(node.value, states))
    if isinstance(node, ast.Lambda):
        # The defaults run now, in the module's scope; the body when the lambda is called.
        for default in node.args.defaults + [item for item in node.args.kw_defaults if item]:
            states = expression(default, states)
        return states
    if isinstance(node, ast.IfExp):
        states = expression(node.test, states)
        return expression(node.body, states) | expression(node.orelse, states)
    if isinstance(node, ast.BoolOp):
        # Each operand after the first may be skipped.
        states = expression(node.values[0], states)
        result = states
        for value in node.values[1:]:
            states = expression(value, states)
            result = result | states
        return result
    if isinstance(node, COMPREHENSIONS):
        states = expression(node.generators[0].iter, states)
        if isinstance(node, ast.GeneratorExp):
            return states  # the rest runs when the generator is consumed
        # The rest runs once per item, maybe never, and a condition may stop it at any point.
        rest = list(node.generators[0].ifs)
        for generator in node.generators[1:]:
            rest += [generator.iter] + list(generator.ifs)
        rest += [node.key, node.value] if isinstance(node, ast.DictComp) else [node.elt]
        result = current = states
        for item in rest:
            current = expression(item, current)
            result = result | current
        return result
    if isinstance(node, ast.Dict):
        for key, value in zip(node.keys, node.values):
            states = expression(value, expression(key, states))
        return states
    for child in ast.iter_child_nodes(node):
        states = expression(child, states)
    return states


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


def truth(test):
    """True or False when a test always has that value at import, else None."""
    if isinstance(test, ast.Constant):
        return bool(test.value)
    # A module imported by the worker is not `__main__`.
    if (
        isinstance(test, ast.Compare)
        and len(test.ops) == 1
        and isinstance(test.ops[0], (ast.Eq, ast.NotEq))
    ):
        sides = [test.left, test.comparators[0]]
        names = [side for side in sides if isinstance(side, ast.Name) and side.id == "__name__"]
        main = [side for side in sides if isinstance(side, ast.Constant) and side.value == "__main__"]
        if names and main:
            return isinstance(test.ops[0], ast.NotEq)
    return None


def run(body, states):
    """The flow of a block entered with `states`; a statement no path reaches is skipped."""
    flow = Flow(normal=states)
    for node in body:
        if not flow.normal:
            break
        step = statement(node, flow.normal)
        flow = Flow(
            step.normal,
            flow.breaks | step.breaks,
            flow.continues | step.continues,
            flow.seen | step.seen,
        )
    return flow


def loop(node, states):
    """`for` and `while`: the body runs any number of times; `else` only when the loop ends
    without `break`."""
    if isinstance(node, ast.While):
        always = truth(node.test)
    else:
        always, states = None, expression(node.iter, states)
    entered = states
    seen = NOWHERE
    breaks = NOWHERE
    while True:  # a few rounds at most: the states only grow
        if isinstance(node, ast.While):
            tested = expression(node.test, entered)
            start = NOWHERE if always is False else tested
        else:
            tested = entered
            start = assign(node.target, None, entered)
        body = run(node.body, start)
        seen = seen | body.seen | entered
        breaks = breaks | body.breaks
        grown = entered | body.normal | body.continues
        if grown == entered:
            break
        entered = grown
    ended = NOWHERE if always is True else tested
    orelse = run(node.orelse, ended)
    return Flow(orelse.normal | breaks, orelse.breaks, orelse.continues, seen | orelse.seen)


def try_statement(node, states):
    body = run(node.body, states)
    # An exception may leave the body at any statement it ran.
    outcome = run(node.orelse, body.normal)
    outcome = Flow(outcome.normal, body.breaks | outcome.breaks,
                   body.continues | outcome.continues, body.seen | outcome.seen)
    for handler in node.handlers:
        start = expression(handler.type, body.seen)
        if handler.name == NAME:
            start = frozenset({OTHER})  # the exception
        handled = run(handler.body, start)
        if handler.name == NAME:
            # `except ... as decode`: the name is deleted when the handler ends.
            handled = Flow(
                frozenset({UNBOUND}) if handled.normal else NOWHERE,
                frozenset({UNBOUND}) if handled.breaks else NOWHERE,
                frozenset({UNBOUND}) if handled.continues else NOWHERE,
                handled.seen,
            )
        outcome = outcome | handled
    if not node.finalbody:
        return outcome
    # `finally` runs on each way out.
    final = run(node.finalbody, outcome.normal)
    after_breaks = run(node.finalbody, outcome.breaks)
    after_continues = run(node.finalbody, outcome.continues)
    return Flow(
        final.normal,
        final.breaks | after_breaks.normal | after_breaks.breaks,
        final.continues | after_continues.normal | after_continues.continues,
        outcome.seen | final.seen,
    )


def match_statement(node, states):
    current = expression(node.subject, states)
    outcome = Flow(seen=states)
    for case in node.cases:
        start = current
        for name, value in captures(case.pattern):
            if name == NAME:
                start = value
        # The guard runs after the pattern matched, before the body; when it fails, what it
        # bound stays for the next case.
        guarded = expression(case.guard, start)
        outcome = outcome | run(case.body, guarded)
        current = current | guarded
    return outcome | Flow(normal=current)  # no case matched


def statement(node, states):
    """The flow of `node` entered with `states`."""
    seen = states
    if isinstance(node, ast.If):
        states = expression(node.test, states)
        always = truth(node.test)
        body = run(node.body, states) if always is not False else Flow()
        orelse = run(node.orelse, states) if always is not True else Flow()
        return body | orelse | Flow(seen=seen)
    if isinstance(node, (ast.For, ast.AsyncFor, ast.While)):
        return loop(node, states) | Flow(seen=seen)
    if isinstance(node, TRIES):
        return try_statement(node, states) | Flow(seen=seen)
    if MATCH and isinstance(node, MATCH):
        return match_statement(node, states) | Flow(seen=seen)
    if isinstance(node, (ast.With, ast.AsyncWith)):
        for item in node.items:
            states = expression(item.context_expr, states)
            if item.optional_vars is not None:
                states = assign(item.optional_vars, None, states)
        return run(node.body, states) | Flow(seen=seen)
    if isinstance(node, ast.Break):
        return Flow(breaks=states, seen=seen)
    if isinstance(node, ast.Continue):
        return Flow(continues=states, seen=seen)
    if isinstance(node, ast.Raise):
        return Flow(seen=seen)  # the module stops here unless a handler takes it
    return Flow(normal=simple(node, states), seen=seen)


def simple(node, states):
    """What `decode` may be after a statement without a block of its own."""
    if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
        # Decorators, defaults, annotations and bases run before the name is bound.
        for field, value in ast.iter_fields(node):
            if field != "body":
                for item in value if isinstance(value, list) else [value]:
                    if isinstance(item, ast.AST):
                        states = expression(item, states)
        if node.name != NAME:
            return states
        if isinstance(node, ast.ClassDef):
            return frozenset({OTHER})
        if isinstance(node, ast.AsyncFunctionDef) and not node.decorator_list:
            return frozenset({ASYNC})
        return frozenset({CALLABLE})  # a decorator may also make a plain function of an async one
    if isinstance(node, ast.Assign):
        states = expression(node.value, states)
        for target in node.targets:
            states = assign(target, node.value, expression(target, states))
        return states
    if isinstance(node, ast.AnnAssign):
        states = expression(node.annotation, expression(node.value, states))
        return assign(node.target, node.value, states) if node.value is not None else states
    if isinstance(node, ast.AugAssign):
        states = expression(node.target, expression(node.value, states))
        return assign(node.target, None, states)  # what the operator returns is not known
    if isinstance(node, ast.Delete):
        for target in node.targets:
            states = expression(target, states)
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
    for child in ast.iter_child_nodes(node):
        states = expression(child, states)
    return states


def verdict(states):
    """(entry, reason) from what `decode` may be where the module's top level ends."""
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
        entry, reason = verdict(run(tree.body, frozenset({UNBOUND})).normal)
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
