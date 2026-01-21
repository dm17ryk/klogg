#!/usr/bin/env python3
"""
fix_preview_ehcp_v2.py

Conservative fixer for config/preview_EHCP.json that:
 - Normalizes enum keys according to field width:
     * width==1 -> single-digit keys ("0".."9")
     * width==2 -> zero-padded two-digit keys ("00".."96")
 - Expands range-like enum keys (e.g., "4-9" or "4–9") into discrete entries.
 - Adds endianness: "little" for certain hexString multi-byte fields (heuristic).
"""

import json, sys, re
from copy import deepcopy

RANGE_RE = re.compile(r'^\s*(\d{1,2})\s*[-–]\s*(\d{1,2})\s*$')  # "4-9" or "4–9"
TIME_MAX = 96

MULTIBYTE_TOKENS = [
    "batch_id", "r0_register", "r1_register", "r2_register", "r3_register",
    "link_register", "program_counter", "task_id", "tamper_sequence",
    "register_value", "test_id", "call_id", "imei", "sim_icc_id", "tamper_sequence"
]

def zero_pad(n):
    return f"{int(n):02d}"

def normalize_key_by_width(k, width):
    ks = str(k)
    if re.fullmatch(r'\d{1,2}', ks):
        n = int(ks)
        if width == 1:
            return str(n)
        elif width == 2:
            return zero_pad(n)
        else:
            return str(n)
    return ks

def expand_range_key(key, value, width):
    m = RANGE_RE.match(str(key))
    if not m:
        return None
    a = int(m.group(1))
    b = int(m.group(2))
    out = {}
    step = 1 if a <= b else -1
    for i in range(a, b + step, step):
        if 0 <= i <= TIME_MAX:
            k = zero_pad(i) if width == 2 else str(i)
            out[k] = value
    return out

def process_enum_map(enumMap, width):
    new = {}
    # First pass: expand any range-like keys
    for k_raw, v in list(enumMap.items()):
        k = str(k_raw)
        if RANGE_RE.match(k):
            expanded = expand_range_key(k, v, width)
            if expanded:
                for ek, ev in expanded.items():
                    if ek not in new:
                        new[ek] = ev
            continue
        # If the key contains a textual range like "4-9 – FFU" as a value, we can't auto-expand;
        # expansion only for keys that are ranges.
        nk = normalize_key_by_width(k, width)
        if nk in new:
            # do not override explicit keys
            continue
        new[nk] = v
    # Second pass: ensure no duplicates where both "0" and "00" exist (prefer explicit form)
    cleaned = {}
    for k, v in new.items():
        # If width == 2, ensure keys are two-digit zero-padded for numeric tokens
        if width == 2 and re.fullmatch(r'\d{1,2}', k):
            nk = zero_pad(k)
        else:
            nk = k
        if nk not in cleaned:
            cleaned[nk] = v
    return cleaned

def should_add_endianness(field):
    if not isinstance(field, dict):
        return False
    if field.get("type") != "hexString":
        return False
    if "endianness" in field:
        return False
    name = field.get("name", "").lower()
    for tok in MULTIBYTE_TOKENS:
        if tok in name:
            return True
    # Conservative: if width >=4 and numeric-sounding name, we could infer, but skip
    return False

def visit_field(field):
    if not isinstance(field, dict):
        return
    # If this field has enumMap and format enum, process
    width_val = None
    if "width" in field:
        try:
            width_val = int(field["width"])
        except Exception:
            width_val = None
    # Apply enum processing if needed
    if field.get("format") == "enum" and "enumMap" in field:
        if width_val is None:
            # if width unknown, attempt to infer from keys: if keys look zero-padded, keep as-is,
            # otherwise leave them, but still expand any explicit range-keys.
            field["enumMap"] = process_enum_map(field["enumMap"], width=width_val if width_val else 0)
        else:
            field["enumMap"] = process_enum_map(field["enumMap"], width=width_val)
    # Heuristic endianness
    if should_add_endianness(field):
        field["endianness"] = "little"
    # Recurse into nested fields if any
    if "fields" in field and isinstance(field["fields"], list):
        for f in field["fields"]:
            visit_field(f)
    # Also nested structures: bitfieldMap, etc.
    if "bitfieldMap" in field and isinstance(field["bitfieldMap"], list):
        for b in field["bitfieldMap"]:
            visit_field(b)

def walk(obj):
    if isinstance(obj, dict):
        visit_field(obj)
        # possible blocks: fields array
        if "blocks" in obj and isinstance(obj["blocks"], list):
            for b in obj["blocks"]:
                walk(b)
        if "fields" in obj and isinstance(obj["fields"], list):
            for f in obj["fields"]:
                walk(f)
        if "previews" in obj and isinstance(obj["previews"], list):
            for p in obj["previews"]:
                walk(p)
    elif isinstance(obj, list):
        for it in obj:
            walk(it)

def main():
    if len(sys.argv) != 3:
        print("Usage: fix_preview_ehcp_v2.py <input.json> <output.json>")
        sys.exit(2)
    inp = sys.argv[1]
    out = sys.argv[2]
    with open(inp, 'r', encoding='utf-8') as fh:
        data = json.load(fh)
    data2 = deepcopy(data)
    walk(data2)
    with open(out, 'w', encoding='utf-8') as fh:
        json.dump(data2, fh, indent=2, ensure_ascii=False)
    print("Wrote:", out)

if __name__ == "__main__":
    main()
