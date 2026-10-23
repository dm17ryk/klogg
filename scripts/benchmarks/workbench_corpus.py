"""Portable synthetic fixtures; binary writes make hashes independent of locale."""

import hashlib
import itertools
import json
import logging
from pathlib import Path

LOG = logging.getLogger("workbench.corpus")


def json_bytes(value):
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False)
        + "\n"
    ).encode("utf-8")


def generate_corpus(destination, rows=10000, seed=42):
    destination = Path(destination)
    if not 1 <= rows <= 1000000 or not 0 <= seed <= 2147483647:
        raise ValueError("invalid_generator_limits")
    if destination.exists() or destination.is_symlink():
        raise ValueError("corpus_destination_exists")
    destination.mkdir(parents=True)
    entries = []

    def write(name, chunks, expected, encoding="utf-8"):
        digest = hashlib.sha256()
        size = 0
        with (destination / name).open("xb") as stream:
            for chunk in chunks:
                # BOM is emitted once by the fixture iterator, never per record.
                codec = "utf-8" if encoding == "utf-8-sig" else encoding
                data = chunk.encode(codec) if isinstance(chunk, str) else chunk
                stream.write(data)
                digest.update(data)
                size += len(data)
        entries.append(
            {
                "path": name,
                "bytes": size,
                "sha256": digest.hexdigest(),
                "encoding": encoding,
                "expected": expected,
            }
        )
        LOG.debug("fixture_written path=%s bytes=%d", name, size)

    def records():
        for number in range(rows):
            level = "ERROR" if number % 10 == 0 else "INFO"
            yield f"{number:010d} {level} seed={seed} value={(seed + number * 17) % 997}\n"

    write(
        "ascii.log",
        records(),
        {"physicalLines": rows, "errorLines": (rows + 9) // 10},
        "ascii",
    )
    write(
        "unicode.log",
        (f"{i:010d} INFO שלום 😀 seed={seed}\n" for i in range(rows)),
        {"physicalLines": rows},
    )
    write(
        "utf16le.log",
        itertools.chain(
            [b"\xff\xfe"],
            (line.replace("\n", "\r\n").encode("utf-16-le") for line in records()),
        ),
        {"physicalLines": rows},
        "utf-16-le-bom",
    )
    # Avoid building the scalable UTF-8/JSON/high-cardinality files in memory.
    write(
        "utf8bom.log", utf8_bom_records(records()), {"physicalLines": rows}, "utf-8-sig"
    )
    write(
        "long-lines.log",
        (f"LONG {i} " + "0123456789abcdef" * 256 + "\n" for i in range(8)),
        {"physicalLines": 8, "payloadBytesPerLine": 4096},
    )
    write(
        "high-cardinality.log",
        (f"key={seed}-{i:010d}\n" for i in range(rows)),
        {"physicalLines": rows, "uniqueKeys": rows},
    )
    write(
        "dense-matches.log",
        ("value=1 value=2\n" for _ in range(rows)),
        {"physicalLines": rows, "occurrences": rows * 2},
    )
    write(
        "structured.jsonl",
        (
            json.dumps(
                {
                    "seq": i,
                    "seed": seed,
                    "exactInteger": 9007199254740993,
                    "unsignedMaximum": 18446744073709551615,
                },
                separators=(",", ":"),
            )
            + "\n"
            for i in range(rows)
        ),
        {"physicalLines": rows, "jsonRecords": rows},
    )
    write(
        "structured.logfmt",
        (
            f'level=INFO seq={i} seed={seed} message="request complete"\n'
            for i in range(rows)
        ),
        {"physicalLines": rows},
    )
    write(
        "quoted.csv",
        csv_records(rows, seed),
        {"logicalRecords": rows, "physicalLines": 1 + rows + (rows + 1) // 2},
    )
    stacks = (
        "java.lang.IllegalStateException: fixture\n\tat service.Worker.run(Worker.java:12)\n"
        "Caused by: java.io.IOException: fixture\n\tat service.IO.read(IO.java:8)\n"
        "System.InvalidOperationException: fixture\n   at Worker.Run() in Worker.cs:line 12\n"
        'Traceback (most recent call last):\n  File "worker.py", line 12, in run\n'
        '    raise ValueError("fixture")\nValueError: fixture\n'
        "TypeError: fixture\n    at run (worker.js:12:3)\n"
    )
    write(
        "stacktraces.log",
        [stacks],
        {
            "physicalLines": 12,
            "languageExamples": ["java", "dotnet", "python", "javascript"],
        },
    )
    write(
        "web.log",
        (
            f"GET /api/items status={500 if i % 10 == 0 else 200} duration_ms={i % 100}.125 seed={seed}\n"
            for i in range(rows)
        ),
        {"physicalLines": rows, "serverErrors": (rows + 9) // 10},
    )
    write(
        "build-tests.log",
        [
            "BUILD start\nTEST alpha PASS duration_ms=2\nTEST beta FAIL duration_ms=3\nBUILD failed\n"
        ],
        {"physicalLines": 4, "passedTests": 1, "failedTests": 1},
    )
    write(
        "etl.log",
        (
            f"job={seed} stage={stage} partition={part} rows={100 + part} bytes={2048 + part}\n"
            for stage in ("extract", "transform", "load")
            for part in range(3)
        ),
        {"physicalLines": 9},
    )
    write(
        "frames.log",
        (
            f"frame={i} cpu_ms={1000 if i % 37 == 0 else 1} heap={65536 - i % 4096} rssi={-40 - i % 40}\n"
            for i in range(rows)
        ),
        {
            "physicalLines": rows,
            "spikeLinesOneBased": [i + 1 for i in range(0, rows, 37)],
        },
    )
    write(
        "clock-resets.jsonl",
        (
            json.dumps(
                {
                    "source": "device",
                    "boot": "A" if i < 3 else "B",
                    "uptimeMs": i if i < 3 else i - 3,
                }
            )
            + "\n"
            for i in range(5)
        ),
        {"physicalLines": 5, "clockDomains": 2, "resetLineOneBased": 4},
    )
    write(
        "protocol.rx.bin",
        [b"\x00\xff\x41\x41\x7e\x00\x7d\x5e\r\n"],
        {"bytes": 10, "segments": [[0, 4], [4, 10]]},
        "binary",
    )
    write(
        "partial-final-line.log",
        ["INFO complete\nERROR incom"],
        {"physicalLines": 2, "unterminatedFinalLine": True},
    )
    write(
        "malformed.jsonl",
        ['{"valid":true}\n{"seq":'],
        {"physicalLines": 2, "validJsonRecords": 1, "malformedRecords": 1},
    )
    manifest = {
        "schemaVersion": 1,
        "generator": "cilogg-workbench-corpus-v1",
        "seed": seed,
        "rows": rows,
        "files": entries,
    }
    (destination / "manifest.json").write_bytes(json_bytes(manifest))
    LOG.info("corpus_generated files=%d rows=%d seed=%d", len(entries), rows, seed)
    return manifest


def utf8_bom_records(records):
    yield b"\xef\xbb\xbf"
    for line in records:
        yield line.replace("\n", "\r\n")


def csv_records(rows, seed):
    yield "seq,message,seed\n"
    for number in range(rows):
        message = 'quoted "value"\nsecond line' if number % 2 == 0 else "comma,value"
        yield f'{number},"{message.replace(chr(34), chr(34) * 2)}",{seed}\n'


def validate_corpus(directory, maximum_bytes=128 * 1024 * 1024):
    directory = Path(directory).resolve()
    manifest_path = directory / "manifest.json"
    if not manifest_path.is_file() or manifest_path.stat().st_size > 2 * 1024 * 1024:
        raise ValueError("invalid_corpus_manifest")
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes)
    if (
        not isinstance(manifest, dict)
        or manifest.get("schemaVersion") != 1
        or not isinstance(manifest.get("files"), list)
    ):
        raise ValueError("invalid_corpus_manifest")
    if type(manifest.get("rows")) is not int or not 1 <= manifest["rows"] <= 1000000:
        raise ValueError("invalid_corpus_rows")
    if type(manifest.get("seed")) is not int or not 0 <= manifest["seed"] <= 2147483647:
        raise ValueError("invalid_corpus_seed")
    if not 1 <= len(manifest["files"]) <= 1000:
        raise ValueError("invalid_corpus_manifest")
    seen = set()
    total = 0
    for entry in manifest["files"]:
        if not isinstance(entry, dict):
            raise ValueError("invalid_corpus_manifest")
        relative = entry.get("path")
        if (
            not isinstance(relative, str)
            or not relative
            or relative in seen
            or Path(relative).is_absolute()
        ):
            raise ValueError("invalid_corpus_path")
        path = (directory / relative).resolve()
        if (
            path == directory
            or not path.is_relative_to(directory)
            or not path.is_file()
        ):
            raise ValueError("invalid_corpus_path")
        seen.add(relative)
        size = path.stat().st_size
        total += size
        if total > maximum_bytes:
            raise ValueError("corpus_byte_limit")
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
        if size != entry.get("bytes") or digest.hexdigest() != entry.get("sha256"):
            raise ValueError("corpus_hash_mismatch")
    if "ascii.log" not in seen:
        raise ValueError("missing_measured_fixture")
    LOG.info("corpus_validated files=%d bytes=%d", len(seen), total)
    return manifest, hashlib.sha256(manifest_bytes).hexdigest()
