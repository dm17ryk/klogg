#!/usr/bin/env python3
"""
fix_preview_ehcp_v3.py

Enhanced fixer that:
 - Normalizes enum keys according to field width:
     * width==1 -> "0".."9"
     * width==2 -> "00".."96"
 - Expands ranges that appear either as enumMap keys ("4-9") or
   as range directives inside enumMap values ("5-20 – seconds") into
   discrete entries.
 - Adds endianness: "little" heuristically for multi-byte hexString fields.
"""

import json, sys, re
from copy import deepcopy

RANGE_KEY_RE = re.compile(r'^\s*(\d{1,2})\s*[-–]\s*(\d{1,2})\s*$')   # key == "4-9"
RANGE_IN_VALUE_RE = re.compile(r'^\s*(\d{1,2})\s*[-–]\s*(\d{1,2})\s*(?:[–\-]\s*)?(.*)$')  # value starts with "5-20 – text"
TIME_MAX = 96

MULTIBYTE_TOKENS = [
    "batch_id", "r0_register", "r1_register", "r2_register", "r3_register",
    "link_register", "program_counter", "task_id", "tamper_sequence",
    "register_value", "test_id", "call_id", "imei", "sim_icc_id"
]

def zero_pad(n):
    return f"{int(n):02d}"

def normalize_numeric_key(k, width):
    ks = str(k).strip()
    if re.fullmatch(r'\d{1,2}', ks):
        n = int(ks)
        if width == 1:
            return str(n)
        elif width == 2:
            return zero_pad(n)
        else:
            return str(n)
    return ks

def expand_range(a, b, width):
    out = []
    a, b = int(a), int(b)
    if a <= b:
        rng = range(a, b+1)
    else:
        rng = range(a, b-1, -1)
    for i in rng:
        if width == 2:
            out.append(zero_pad(i))
        else:
            out.append(str(i))
    return out

def process_enum_map(enumMap, width):
    """
    Processes an enumMap dict:
     - Expands range-like keys like "4-9"
     - Expands range directives inside values like "5-20 – seconds"
     - Normalizes numeric keys according to width
     - Preserves explicit keys if present (do not override)
    """
    new = {}

    # First: handle explicit keys that themselves are ranges
    for raw_k, raw_v in list(enumMap.items()):
        k = str(raw_k).strip()
        if RANGE_KEY_RE.match(k):
            m = RANGE_KEY_RE.match(k)
            a, b = m.group(1), m.group(2)
            for ek in expand_range(a, b, width if width else 0):
                # if existing explicit entry exists, do not overwrite
                if ek not in new:
                    new[ek] = raw_v
            continue
        # Next: if key is normal, but value *starts with* a range directive, expand
        v = raw_v
        if isinstance(v, str):
            vm = RANGE_IN_VALUE_RE.match(v)
            if vm:
                a, b, remainder = vm.group(1), vm.group(2), vm.group(3).strip()
                label = remainder if remainder else v  # prefer remainder text as label
                for ek in expand_range(a, b, width if width else 0):
                    if ek not in new:
                        new[ek] = label
                continue
        # Otherwise keep the explicit key (normalized)
        nk = normalize_numeric_key(k, width if width else 0)
        if nk not in new:
            new[nk] = v

    # Second pass: ensure keys are normalized numeric or left as-is (no duplicates)
    cleaned = {}
    for k, v in new.items():
        ks = str(k)
        if width == 2 and re.fullmatch(r'\d{1,2}', ks):
            nk = zero_pad(ks)
        elif width == 1 and re.fullmatch(r'\d{1,2}', ks):
            # ensure single-digit form if possible for width==1
            n = int(ks)
            nk = str(n)
        else:
            nk = ks
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
    return False

def visit_field(field):
    if not isinstance(field, dict):
        return
    width_val = None
    if "width" in field:
        try:
            width_val = int(field["width"])
        except Exception:
            width_val = None

    # process enum maps
    if field.get("format") == "enum" and "enumMap" in field and isinstance(field["enumMap"], dict):
        field["enumMap"] = process_enum_map(field["enumMap"], width_val if width_val is not None else 0)

    # add endianness heuristically
    if should_add_endianness(field):
        field["endianness"] = "little"

    # Recurse
    if "fields" in field and isinstance(field["fields"], list):
        for f in field["fields"]:
            visit_field(f)
    if "bitfieldMap" in field and isinstance(field["bitfieldMap"], list):
        for b in field["bitfieldMap"]:
            visit_field(b)

def walk(obj):
    if isinstance(obj, dict):
        # top-level blocks
        if "blocks" in obj and isinstance(obj["blocks"], list):
            for b in obj["blocks"]:
                walk(b)
        if "previews" in obj and isinstance(obj["previews"], list):
            for p in obj["previews"]:
                walk(p)
        if "fields" in obj and isinstance(obj["fields"], list):
            for f in obj["fields"]:
                walk(f)
        # also process this dict as field-like when appropriate
        visit_field(obj)
    elif isinstance(obj, list):
        for it in obj:
            walk(it)

def main():
    if len(sys.argv) != 3:
        print("Usage: fix_preview_ehcp_v3.py <input.json> <output.json>")
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
