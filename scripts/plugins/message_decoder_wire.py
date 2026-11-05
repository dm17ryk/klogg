"""Exact, bounded PDML helpers shared by proof tooling and preview transport."""

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


def fields_named(nodes, name):
    result = []
    for node in nodes:
        if node["attributes"].get("name") == name:
            result.append(node)
        result.extend(fields_named(node["children"], name))
    return result
