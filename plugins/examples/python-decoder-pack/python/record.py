"""A Python decoder for the synthetic demo record: a type byte, a length byte, that many data
bytes and a checksum byte (the sum of the data bytes, modulo 256).

CILogg calls decode(payload, context) with the bytes of the "Demo record" preview
(content.decoder_routes in plugin.json). It returns PreviewService rows; positions are offsets in
`payload`. Returning None says "not mine": the preview then falls back to its JSON fields.
"""

from . import common


def decode(payload, context):
    if len(payload) < 2:
        return None
    kind, length = payload[0], payload[1]
    data = payload[2 : 2 + length]
    complete = len(payload) >= 3 + length
    rows = [
        {"name": "record", "value": common.type_name(kind), "pos": 0, "size": len(payload)},
        {"name": "type", "value": kind, "raw": payload[:1], "pos": 0, "size": 1, "depth": 1},
        {"name": "length", "value": length, "raw": payload[1:2], "pos": 1, "size": 1, "depth": 1},
        {"name": "data", "value": data.hex(), "raw": data, "pos": 2, "size": len(data), "depth": 1},
    ]
    integrity = "not_reported"
    if complete:
        checksum = payload[2 + length]
        integrity = "valid" if checksum == sum(data) % 256 else "invalid"
        rows.append(
            {"name": "checksum", "value": checksum, "raw": payload[2 + length : 3 + length],
             "pos": 2 + length, "size": 1, "depth": 1}
        )
    return {
        "fields": rows,
        "messageCompleteness": "complete" if complete else "incomplete",
        "messageIntegrity": integrity,
    }
