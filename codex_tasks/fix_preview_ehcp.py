#!/usr/bin/env python3
"""
fix_preview_ehcp.py

Conservative automated fixer for config/preview_EHCP.json.

Usage:
    python3 fix_preview_ehcp.py <input.json> <output.json>

What it does:
 - Normalize activity_period_* enum keys to zero-padded two-character keys "00".."96".
 - Remove duplicate non-padded keys when their zero-padded counterpart exists.
 - Add endianness: "little" for hexString fields whose name clearly identifies
   multi-byte numeric fields (heuristic list).
 - Preserve all other structure.
"""

import json
import sys
import re
from copy import deepcopy

TIME_ENUM_NAMES = {
    "activity_period_start_time",
    "activity_period_end_time",
    # also other fields might be named similarly (we match substring)
}

MULTIBYTE_HEX_NAME_TOKENS = [
    "batch_id", "r0_register", "r1_register", "r2_register", "r3_register",
    "link_register", "program_counter", "task_id", "tamper_sequence",
    "register_value", "test_id", "call_id", "tamper_sequence", "imei", "sim_icc_id"
]

def normalize_time_enummap(enum_map):
    """
    Given an enumMap (dict), return a new dict whose keys are normalized to
    zero-padded two-character strings for 0..96 where applicable,
    removing duplicates. Non-time keys are preserved.
    """
    new_map = {}
    # Make a working map of transformed keys to values.
    for k, v in enum_map.items():
        # If key is numeric-like (e.g., "0", "00", "10", 0), normalize to two-digit string
        if isinstance(k, str) and re.fullmatch(r'\d{1,2}', k):
            n = int(k, 10)
            if 0 <= n <= 96:
                nk = f"{n:02d}"
                # prefer explicit existing nk over non-padded duplicates
                if nk in new_map and new_map[nk] != v:
                    # If values differ, keep the existing and also keep the other under original form
                    # but usually they will be same. We'll just keep nk.
                    pass
                new_map[nk] = v
                continue
        # otherwise keep as-is
        new_map[str(k)] = v
    return new_map

def field_name_matches_time(name):
    if not isinstance(name, str):
        return False
    for t in TIME_ENUM_NAMES:
        if t in name:
            return True
    return False

def should_add_endianness(field):
    # Only consider fields that have type hexString and no endianness already
    if not isinstance(field, dict):
        return False
    if field.get("type") != "hexString":
        return False
    if "endianness" in field:
        return False
    name = field.get("name", "").lower()
    for tok in MULTIBYTE_HEX_NAME_TOKENS:
        if tok in name:
            return True
    # also if width >=4 likely multi-byte numeric
    width = field.get("width")
    if isinstance(width, int) and width >= 4:
        # be conservative: only add if name suggests numeric register/batch id
        # but we already tested tokens; skip here.
        return False
    return False

def walk_fields(obj):
    """Recursively traverse and fix fields inside a block/field structure."""
    if isinstance(obj, dict):
        # If this dict looks like a field definition with format enumMap and a name,
        # apply time enum normalization
        if "name" in obj and "format" in obj and obj["format"] == "enum" and "enumMap" in obj:
            if field_name_matches_time(obj["name"]):
                obj["enumMap"] = normalize_time_enummap(obj["enumMap"])
            else:
                # also remove duplicate numeric keys if zero-padded exists
                # we will still normalize numeric keys to strings if ambiguous
                # but don't aggressive-modify non-time enums except dedup numeric keys:
                new_map = {}
                seen_numeric = {}
                for k, v in obj["enumMap"].items():
                    ks = str(k)
                    if re.fullmatch(r'\d{1,2}', ks):
                        nk = f"{int(ks):02d}"
                        # if already present prefer nk
                        new_map[nk] = v
                        seen_numeric[nk] = True
                    else:
                        # keep non numeric keys unchanged
                        if ks not in new_map:
                            new_map[ks] = v
                obj["enumMap"] = new_map
        # Add endianness heuristics
        if "name" in obj and should_add_endianness(obj):
            obj["endianness"] = "little"
        # Recurse into fields array if present
        if "fields" in obj and isinstance(obj["fields"], list):
            for f in obj["fields"]:
                walk_fields(f)
        # Recurse into bitfieldMap / enumMap etc. not necessary
        # Recurse into block reference if present (no change)
        # Recurse into nested dicts
        for k, v in obj.items():
            if isinstance(v, dict):
                walk_fields(v)
            elif isinstance(v, list):
                for el in v:
                    walk_fields(el)
    elif isinstance(obj, list):
        for item in obj:
            walk_fields(item)

def main():
    if len(sys.argv) != 3:
        print("Usage: python3 fix_preview_ehcp.py <input.json> <output.json>")
        sys.exit(2)

    inpath = sys.argv[1]
    outpath = sys.argv[2]
    with open(inpath, "r", encoding="utf-8") as f:
        data = json.load(f)

    data_fixed = deepcopy(data)

    # Top-level blocks
    if "blocks" in data_fixed and isinstance(data_fixed["blocks"], list):
        for block in data_fixed["blocks"]:
            walk_fields(block)

    # Previews section
    if "previews" in data_fixed and isinstance(data_fixed["previews"], list):
        for preview in data_fixed["previews"]:
            walk_fields(preview)

    # Also top-level fields if any (some configs place fields at root)
    walk_fields(data_fixed)

    # Dump result
    with open(outpath, "w", encoding="utf-8") as f:
        json.dump(data_fixed, f, indent=2, ensure_ascii=False)
    print(f"Wrote corrected file to {outpath}")

if __name__ == "__main__":
    main()
