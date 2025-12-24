# Klogg Structured Previewer config guide

This guide explains how to create a `previews.json` file for Klogg’s **Structured Previewer**.

The goal: take a **log line**, match it with a **regex**, extract a **payload** (often hex or base64), decode it into bytes, then parse those bytes into **named fields** (including nested structures, enums, flags, and bitfields).

---

## Quick start

### Minimal working example

Save as `previews.json`:

```json
{
  "version": 1,
  "previews": [
    {
      "name": "MyPayload",
      "regex": "^PAYLOAD=(?<payload>[0-9A-F]+)$",
      "bufferCapture": "payload",
      "type": "hexString",
      "format": "fields",
      "fields": [
        { "name": "msgType", "width": 1, "format": "hex" },
        { "name": "size", "width": 2, "endianness": "little", "format": "dig" },
        { "name": "data", "width": "{size}", "format": "hex" }
      ]
    }
  ]
}
```

What it does:

* Matches lines like: `PAYLOAD=0A1000DEADBEEF`
* Captures the `payload` hex text
* Decodes hex → bytes
* Parses:

  * `msgType` = 1 byte
  * `size` = 2 bytes little-endian integer
  * `data` = `size` bytes

---

## Important rules

### JSON must be strict JSON

No comments, no trailing commas.

✅ Valid:

```json
{ "name": "X", "enabled": true }
```

❌ Invalid:

```json
{ "name": "X", // comment
  "enabled": true,
}
```

---

## File structure

### Recommended (schema-friendly) layout

```json
{
  "$schema": "./schemas/klogg-previews.schema.json",
  "version": 1,
  "previews": [ ... ]
}
```

* `version`: schema/config version (start with `1`)
* `previews`: array of preview definitions

### Legacy layout (also acceptable if supported)

Some loaders allow a root array:

```json
[
  { "name": "...", "regex": "...", "fields": [ ... ] }
]
```

---

## What happens at runtime

For each preview definition:

1. Klogg tries to match the log line with `regex`.
2. If it matches, Klogg extracts the **payload** string (usually from a regex capture group).
3. Klogg decodes that payload based on `type` into a **byte buffer**.
4. Klogg parses `fields` sequentially from that byte buffer.
5. The result is shown in the Preview tab (Field → Value), including nested children.

---

## Preview definition reference

Each object inside `previews[]` is a **Preview Definition**.

### `name` (required)

Friendly name shown in UI (menus, combobox).

```json
{ "name": "EHCP Frame", ... }
```

Rules:

* Must be unique (recommended)
* Keep it short and stable (users will see it often)

---

### `enabled` (optional, default: true)

Allows enabling/disabling previews in the Import Previews window.

```json
{ "enabled": false }
```

Effect:

* Disabled previews won’t be used for **Auto detection**
* Disabled previews won’t appear (or appear grayed) in “Send to Preview” menus

---

### `regex` (required)

Regex used to decide if this preview applies to a log line.

```json
{ "regex": "^MSG\\[(\\d+)\\]: Payload=([0-9A-F]+)$" }
```

Recommended: use **named capture groups** so configs are easier to maintain:

```json
{
  "regex": "^MSG\\[(?<id>\\d+)\\]: Payload=(?<payload>[0-9A-F]+)$"
}
```

Notes:

* Regex is evaluated against the full log line text.
* Use `^` and `$` where possible to avoid accidental matches.

---

### `bufferCapture` (recommended)

Tells Klogg **which regex capture group is the payload**.

Why you need it:

* Many log lines capture multiple things: id, status, payload, etc.
* Without `bufferCapture`, Klogg would have to guess.

Examples:

Using named capture:

```json
{
  "regex": "^ID=(?<id>\\d+) PAYLOAD=(?<payload>[0-9A-F]+)$",
  "bufferCapture": "payload"
}
```

Using capture index (1-based):

```json
{
  "regex": "^ID=(\\d+) PAYLOAD=([0-9A-F]+)$",
  "bufferCapture": 2
}
```

If omitted:

* You should assume Klogg will use **capture group 1** if present, otherwise the entire match.
  (If you rely on this, configs become fragile; `bufferCapture` is strongly recommended.)

---

### `type` (optional, default: `"string"`)

Defines how the extracted payload text is converted into bytes for parsing.

Supported values:

| `type`      | Meaning (input)                          | Output buffer     |
| ----------- | ---------------------------------------- | ----------------- |
| `string`    | Plain text                               | UTF‑8 bytes       |
| `hexString` | ASCII hex like `"0A1BFF"`                | decoded bytes     |
| `base64`    | Base64 text                              | decoded bytes     |
| `bin`       | Bit string like `"01010101..."`          | packed bits/bytes |
| `bytes`     | Already bytes (rare in logs; future use) | unchanged         |

Example:

```json
{ "type": "hexString" }
```

**Units reminder**
Once the payload is decoded, **offset/width are in bytes** (except bitfields, which use bits — see below).

---

### `offset` (optional, default: 0)

Skips initial bytes at the beginning of the decoded payload before parsing fields.

```json
{ "offset": 9 }
```

Typical uses:

* Ignore protocol header bytes you don’t care about
* Start parsing at a magic marker inside a long payload

**Example**
If payload is hex and you want to skip 4 bytes (8 hex chars):

```json
{
  "type": "hexString",
  "offset": 4,
  "fields": [
    { "name": "afterHeader", "width": 2, "format": "hex" }
  ]
}
```

---

### `format` (required)

Preview-level `format` is typically `"fields"`.

```json
{ "format": "fields" }
```

Meaning:

* Parse the decoded payload using the `fields` array.

---

### `fields` (required when `format: "fields"`)

Array of field definitions that describe how to slice and interpret the payload bytes.

```json
{
  "fields": [
    { "name": "msgType", "width": 1, "format": "hex" }
  ]
}
```

---

## Field definition reference

Each entry in a `fields[]` array is a **FieldSpec**.

### Field parsing model

Fields are parsed sequentially using a **cursor**:

* Start cursor = `preview.offset` bytes into payload
* For each field:

  * cursor += `field.offset` (default 0)
  * read `field.width` bytes (or “rest” if missing)
  * cursor advances past what was read

This is why offsets are usually **relative** (as you requested).

---

### `name` (required)

Field label shown in preview output.

```json
{ "name": "checksum", ... }
```

Also: fields can become variables for later expressions (width/offset), e.g. `{size}`.

---

### `offset` (optional, default: 0)

Relative bytes to skip from the current cursor before reading this field.

```json
{ "name": "checksum", "offset": 1, "width": 2, "format": "hex" }
```

Meaning: skip 1 byte, then read 2 bytes for checksum.

---

### `width` (optional)

Number of bytes to read for this field.

```json
{ "name": "Code", "width": 4, "format": "hex" }
```

If `width` is omitted:

* the field consumes “the rest” of the current buffer (very useful for trailing strings)

Example:

```json
{ "name": "url", "format": "string" }
```

---

### `width` can reference variables

If earlier fields contain sizes, you can use them:

```json
{ "name": "payload", "width": "{size}", "format": "hex" }
```

#### Nested variable paths

If you support nested scope, you can allow dotted names:

```json
{ "width": "{header.size}" }
```

Best practice:

* Make sure the referenced field is parsed **before** it’s used.

---

### Expressions in `{...}`

To keep configs readable, treat expressions as strings:

```json
{ "width": "{size}" }
```

If your build supports arithmetic, you can also do:

```json
{ "width": "{size} - 2" }
```

If arithmetic is **not** supported yet:

* add another field that computes the value you need, or
* structure fields so you don’t need math

---

### `type` (optional, default: inherit/bytes)

Field-level `type` defines how the extracted slice is decoded **before** formatting.

Common use: nested base64 inside a hex payload.

Example:

```json
{
  "name": "value",
  "width": "{size}",
  "type": "base64",
  "format": "fields",
  "fields": [
    { "name": "sub1", "width": 2, "format": "dig" }
  ]
}
```

Pipeline:

1. slice `{size}` bytes (these bytes are base64 text)
2. decode base64 → raw bytes
3. parse nested fields from those raw bytes

---

### `endianness` (optional)

Controls byte order when interpreting an integer.

```json
{ "name": "size", "width": 2, "endianness": "little", "format": "dig" }
```

Supported:

* `little`
* `big`

Used by:

* `format: dig/dec`
* `format: hex` (when shown as integer value; for pure hex bytes display you can ignore endianness)

---

### `format` (optional, default: `"string"` or inferred)

Defines how Klogg displays/interprets the field.

Supported formats and examples:

---

## Formats

### 1) `string`

Shows bytes as UTF‑8 text.

```json
{ "name": "statusText", "width": 8, "format": "string" }
```

Use when:

* payload contains text fragments
* you want to show a URL, name, etc.

---

### 2) `dig` / `dec`

Shows an integer in decimal.

```json
{ "name": "size", "width": 2, "endianness": "little", "format": "dig" }
```

Recommended:

* Use `dig` for numeric fields that will later be referenced as variables (`{size}`).

---

### 3) `hex`

Shows bytes/number in hex.

```json
{ "name": "crc", "width": 2, "format": "hex" }
```

Useful for:

* IDs
* checksums
* opaque codes

---

### 4) `bin`

Shows bits in binary.

```json
{ "name": "rawFlagsByte", "width": 1, "format": "bin" }
```

---

### 5) `enum`

Maps a numeric field to a label via `enumMap`.

```json
{
  "name": "state",
  "width": 1,
  "format": "enum",
  "enumMap": {
    "0": "OFF",
    "1": "ON",
    "2": "UNKNOWN"
  }
}
```

Notes:

* Keys are strings in JSON. Use `"0"`, `"1"`, etc.
* If a value is missing, Klogg should show fallback (e.g., `"Unknown(5)"`).

**Hex enum keys**
If your field is naturally hex-coded, you can also use:

```json
"enumMap": { "0x10": "HELLO" }
```

(Implementations typically normalize the numeric value then compare against parsed key values.)

---

### 6) `flags`

Decodes a numeric field into a set of flags using `flagMap`.

```json
{
  "name": "permissions",
  "width": 1,
  "format": "flags",
  "flagMap": {
    "0x1": "READ",
    "0x2": "WRITE",
    "0x4": "EXECUTE",
    "0x8": "DELETE"
  }
}
```

Meaning:

* Field value is treated as a bitmask
* Output is a list like: `READ | EXECUTE`

---

### 7) `fields` (nested structure)

Treats this field as a container and parses subfields from it.

```json
{
  "name": "header",
  "width": 6,
  "format": "fields",
  "fields": [
    { "name": "ver", "width": 1, "format": "dig" },
    { "name": "len", "width": 2, "endianness": "little", "format": "dig" }
  ]
}
```

---

### 8) `bitfield`

Treats the field as a sequence of bits and parses subfields from `bitfieldMap`.

```json
{
  "name": "desc",
  "width": 8,
  "format": "bitfield",
  "bitfieldMap": [
    { "name": "isActive", "width": 1, "format": "dig" },
    { "name": "isVerified", "width": 1, "format": "dig" },
    { "name": "status", "width": 4, "format": "enum",
      "enumMap": { "0": "OFF", "1": "ON", "2": "UNKNOWN" }
    },
    { "name": "reserved", "width": 2, "format": "bin" }
  ]
}
```

**Important: width units**

* For `bitfield`, `width` is **bits**, not bytes.
* `bitfieldMap[].width` is also **bits**.

So `width: 8` means “1 byte of bits”.

---

## Advanced: show regex captures as fields

This is extremely handy when the log line already contains useful values outside of the binary payload (IDs, statuses, timestamps).

### `source: "capture"` + `capture`

A field can come from a regex capture group instead of the decoded buffer.

```json
{
  "name": "msgId",
  "source": "capture",
  "capture": "id",
  "format": "dig"
}
```

* `capture` can be:

  * a named group (like `"id"`)
  * or a numeric index (like `1`)

**Rules**

* Capture fields do **not** advance the buffer cursor.
* They are “metadata fields” displayed in the preview.

---

## Step-by-step: building a preview definition

### Step 1 — collect a real log sample

Example:

```
12/7/2025 10:06:11.116 [RX] - #4 "EHCP091020801005104108004078251207080605E#INN000000NNNICE0Y8D8FNIVS00001061912310100400000000!4303"
```

### Step 2 — write a regex that isolates the payload

You usually want to capture only the relevant part. Example:

```json
{
  "regex": "EHCP(?<payload>[0-9A-F]+)!"
}
```

### Step 3 — tell Klogg which capture is the payload

```json
{ "bufferCapture": "payload" }
```

### Step 4 — set payload type (`hexString` here)

```json
{ "type": "hexString" }
```

### Step 5 — define fields from the decoded bytes

Decide the protocol layout and write sequential fields.

```json
{
  "format": "fields",
  "fields": [
    { "name": "magic", "width": 2, "format": "hex" },
    { "name": "msgType", "width": 1, "format": "hex" },
    { "name": "size", "width": 2, "endianness": "little", "format": "dig" },
    { "name": "payload", "width": "{size}", "format": "hex" },
    { "name": "crc", "width": 2, "format": "hex" }
  ]
}
```

### Step 6 — import, test, iterate

* Tools → **Import previews…**
* Enable preview
* Right-click a matching line → Send to Preview → Auto
* Switch preview type in the tab if needed

---

## Full example: hex payload + nested base64 + trailing checksum

```json
{
  "version": 1,
  "previews": [
    {
      "name": "ExampleFormat",
      "enabled": true,
      "regex": "^MSG\\[(?<msgId>\\d+)\\]: Payload=(?<payload>[0-9A-F]+) Status=(?<status>\\w+)$",
      "bufferCapture": "payload",
      "type": "hexString",
      "format": "fields",
      "fields": [
        { "name": "msgId", "source": "capture", "capture": "msgId", "format": "dig" },
        { "name": "status", "source": "capture", "capture": "status", "format": "string" },

        { "name": "Code", "width": 4, "format": "hex" },
        { "name": "size", "width": 2, "endianness": "little", "format": "dig" },

        {
          "name": "value",
          "width": "{size}",
          "type": "base64",
          "format": "fields",
          "fields": [
            { "name": "subValue1", "width": 2, "endianness": "little", "format": "dig" },
            { "name": "subValue2", "width": 2, "endianness": "little", "format": "dig" },
            { "name": "url", "format": "string" }
          ]
        },

        { "name": "checksum", "width": 2, "format": "hex" }
      ]
    }
  ]
}
```

---

## Best practices

* Prefer **named captures** in regex (`(?<payload>...)`) + `bufferCapture`.
* Keep parsing **sequential**; use `offset` only when you truly need to skip bytes.
* For dynamic widths, make sure the size field is parsed with a numeric format (`dig`/`hex`) so it can be used as `{size}`.
* Start with a minimal parser (a few fields), then extend once you verify decoding is correct.
* Add a `bitfield` only after you confirm the underlying bytes are correct (hex view first, then bit breakdown).

---

## Troubleshooting checklist

### “Auto” doesn’t pick my preview

* Your `regex` doesn’t match the line exactly.
* Use a quick simplified regex first, then make it stricter.
* Ensure `enabled: true`.

### Preview shows “decode error”

* `type: hexString` but payload contains non-hex characters (spaces, `#`, etc.).

  * Fix regex to capture only hex digits
  * Or (future enhancement) add `stripSpaces: true`

### Fields “shift” / incorrect values

* Wrong `endianness` on numeric fields
* Incorrect `width` sizes
* Missing `offset` skip between fields

### Variable width doesn’t work

* Ensure referenced field appears earlier
* Ensure referenced field resolves to a numeric value (use `format: dig` or `hex`)
* If your expression system doesn’t support arithmetic, don’t use `{size}-2`

---

## Autocomplete and “intelligence” for authors (recommended)

To get autocomplete / validation:

1. ship a JSON Schema: `schemas/klogg-previews.schema.json`
2. add `$schema` to the file as shown above

In VS Code, you can also map schema to file names:

`.vscode/settings.json`:

```json
{
  "json.schemas": [
    {
      "fileMatch": ["previews.json", "previews.*.json"],
      "url": "./schemas/klogg-previews.schema.json"
    }
  ]
}
```

This will:

* autocomplete `type`, `format`, `endianness`
* warn if `enum` is missing `enumMap`, etc.
* validate nested `fields` and `bitfieldMap`

---

If you want, paste one of your real payload log lines and the byte layout (even rough), and I’ll write a complete preview definition for it using this format (including a regex that extracts the exact payload range).
