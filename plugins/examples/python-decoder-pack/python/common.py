"""Shared names of the synthetic demo record. A helper module is listed in the pack's
`python_decoders` too: only listed modules are part of the pack."""

RECORD_TYPES = {1: "Hello", 2: "Reading", 3: "Alarm"}


def type_name(value):
    return RECORD_TYPES.get(value, "Unknown (%d)" % value)
