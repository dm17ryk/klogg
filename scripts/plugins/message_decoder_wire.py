"""Exact, bounded PDML helpers shared by proof tooling and preview transport."""

import codecs
import logging
import re
import xml.etree.ElementTree as ET

LOG = logging.getLogger("cilogg.decoder.wire")
INPUT_LIMIT = 1024 * 1024
OUTPUT_LIMIT = 16 * 1024 * 1024


class ProofError(RuntimeError):
    """Stable public reason; private engine diagnostics never become the message."""

    def __init__(self, reason):
        super().__init__(reason)
        self.cleanup_reason = None


def validate_dissector(name):
    if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_.-]{0,127}", name):
        raise ProofError("invalid_dissector")


def hexdump(payload, input_limit=INPUT_LIMIT):
    if not 0 < len(payload) <= input_limit:
        raise ProofError("input_limit")
    return "".join(
        f"{offset:06x} {payload[offset:offset + 16].hex(' ')}\n"
        for offset in range(0, len(payload), 16)
    )


def normalize_pdml(data, *, output_limit=OUTPUT_LIMIT, node_limit=50000, depth_limit=128):
    """Preserve order and every attribute as strings, including empty values."""
    if len(data) > output_limit:
        raise ProofError("output_limit")
    try:
        text = data.decode("utf-8")
    except UnicodeError as error:
        raise ProofError("invalid_pdml") from error
    if "<!DOCTYPE" in text.upper() or "<!ENTITY" in text.upper() or "\x00" in text:
        raise ProofError("invalid_pdml")
    # Enforce limits while parsing, before building the recursive normalized tree.
    parser = ET.XMLPullParser(events=("start", "end"))
    depth, count = -2, 0  # pdml=-1, packet=0
    try:
        for offset in range(0, len(text), 4096):
            parser.feed(text[offset : offset + 4096])
            for event, element in parser.read_events():
                if event == "start":
                    depth += 1
                    if depth >= 0:
                        count += 1
                    if count > node_limit or depth > depth_limit:
                        raise ProofError("field_limit")
                else:
                    depth -= 1
        parser.close()
        document = ET.fromstring(text)
    except ET.ParseError as error:
        raise ProofError("invalid_pdml") from error
    if document.tag != "pdml" or not document.findall("packet"):
        raise ProofError("invalid_pdml")

    def node(element, identity):
        return {
            "id": identity,
            "kind": element.tag,
            "attributes": dict(element.attrib),
            "positionOrigin": "engine_data_source_unresolved",
            "children": [node(child, f"{identity}/{i}") for i, child in enumerate(element)],
        }

    packets = [node(packet, str(i)) for i, packet in enumerate(document.findall("packet"))]
    LOG.debug("pdml_normalized packets=%d nodes=%d", len(packets), count)
    return packets


def normalize_pdml_prefix(data, *, node_limit, depth_limit, whole=True):
    """The packets of PDML read up to `node_limit` items, in preorder (#198).

    Returns (packets, items_cut): with more items than `node_limit`, the tree keeps its first
    `node_limit` items and `items_cut` is true. `whole` false means `data` is a prefix cut off at
    a byte limit: its incomplete end is dropped instead of failing. Nodes are as normalize_pdml
    builds them. An item deeper than `depth_limit` fails (`depth_limit`)."""
    text = codecs.getincrementaldecoder("utf-8")().decode(data, final=whole)
    if "<!DOCTYPE" in text.upper() or "<!ENTITY" in text.upper() or "\x00" in text:
        raise ProofError("invalid_pdml")
    parser = ET.XMLPullParser(events=("start", "end"))
    packets, stack = [], []
    depth, count, items_cut, root = -2, 0, False, None  # pdml=-1, packet=0
    try:
        for offset in range(0, len(text), 4096):
            parser.feed(text[offset : offset + 4096])
            for event, element in parser.read_events():
                if event == "end":
                    depth -= 1
                    stack.pop()
                    continue
                depth += 1
                if depth < 0:
                    root = element.tag
                    stack.append(None)
                    continue
                count += 1
                if count > node_limit:
                    items_cut = True
                    break
                if depth > depth_limit:
                    raise ProofError("depth_limit")
                parent = stack[-1]
                siblings = packets if parent is None else parent["children"]
                node = {
                    "id": str(len(siblings)) if parent is None else f"{parent['id']}/{len(siblings)}",
                    "kind": element.tag,
                    "attributes": dict(element.attrib),
                    "positionOrigin": "engine_data_source_unresolved",
                    "children": [],
                }
                # A top-level element other than a packet is not part of a result.
                if parent is not None or element.tag == "packet":
                    siblings.append(node)
                stack.append(node)
            if items_cut:
                break
        if whole and not items_cut:
            parser.close()
    except ET.ParseError as error:
        raise ProofError("invalid_pdml") from error
    if root != "pdml" or not packets:
        raise ProofError("invalid_pdml")
    LOG.debug(
        "pdml_prefix_normalized packets=%d nodes=%d items_cut=%s whole=%s",
        len(packets), min(count, node_limit), items_cut, whole,
    )
    return packets, items_cut


def fields_named(nodes, name):
    result = []
    for node in nodes:
        if node["attributes"].get("name") == name:
            result.append(node)
        result.extend(fields_named(node["children"], name))
    return result
