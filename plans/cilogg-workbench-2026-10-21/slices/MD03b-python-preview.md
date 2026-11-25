# MD03b — Python preview backend for pack decoders

Plan change approved by Dmitry on 2026-11-24 (issue https://github.com/dm17ryk/klogg/issues/159):
instead of waiting for the full MD03 decoder contract, which stays planned after MD01, a narrow
slice runs configuration packs' Python decoders in the preview, as MD03a did for TShark and Lua.
It unblocks E16b (issue https://github.com/dm17ryk/klogg/issues/146). It reuses PreviewService
result version 1 and the MD03a worker transport and adds no generic dataset contract.

## Fixed contracts

- **Entry point.** A pack module's `decode(payload: bytes, context: dict)`. `context` holds
  `preview`, `protocol`, `inputEncoding` (`utf8`, `hex`, `base64`, `bin`, `bytes`), `direction`
  (`unknown`; `rx` and `tx` reserved) and `decoder` (`<pack-id>/<decoder-id>`). It returns rows
  `{name, value, raw, pos, size, depth, attributes}` or `{fields, messageCompleteness,
  messageIntegrity}`; `None` means not dispatched. Rows become PreviewService result v1 rows
  (`columns`, `depth`, `attributes`, `positionOrigin: decoder_payload`, `id`).
- **States.** `complete` and `limited` describe processing; `messageCompleteness`
  (`complete`, `incomplete`, `not_reported`) and `messageIntegrity` (`valid`, `invalid`,
  `not_reported`) come only from the decoder. Failures: `decoder_failed`, `decoder_load_failed`,
  `invalid_decoder_result`, with `detail` naming the exception type and pack file:line only.
- **Worker.** `scripts/plugins/message_decoder_python_worker.py`, framed JSON over stdio (request
  256 KiB, payload 64 KiB, response 128 KiB), one per preview owner, at most two warm, started with
  `-I -S` in a process group or job object that ends descendants. Preview deadlines (3 s default,
  10 s maximum); deadline, cancellation, crash and protocol errors end the worker, the next request
  starts a new one; decoder failures keep it. 512 rows, depth 32, 64 Ki UTF-16 units per text.
  Real stdin and stdout are kept for frames only; decoder output goes to bounded diagnostics. The
  interpreter follows the plugins' `python.exe` rules (#141).
- **Routing.** A route target `{python: "<pack-id>/<decoder-id>"}` in `previewRoutes` or
  `protocolRoutes` (with `inputEncoding`); a pack names only its own decoders, user routes any
  (`python:` in the settings dialog). Automatic keeps the MD03a order with Python routes in a TShark
  route's place; Python routes apply while their pack is enabled, with or without TShark; JSON
  fallback only on `decoder_not_dispatched`; explicit TShark ignores Python routes.
- **Loading and isolation.** Modules of enabled packs (`packPython` in the session snapshot) are
  copied and hashed at session start (1 MiB each, 64 per session, refused at enable beyond that);
  one import namespace per pack; every enable, disable or update starts new sessions.
- **Trust.** `confirmPackLua` asks for packs with Python decoders too (install, enable, update);
  auto-update is the same trust decision as for process plugins.
- **Capability.** Host feature `python_decoders` 1; `get_plugins` `hostFeatures` and per pack
  `pythonDecodersActive`; the Installed tab follows them.

## Tasks

1. Python worker and its tests (`tests/plugins/test_message_decoder_python_worker.py`).
2. Shared worker transport (`previewworkerclient.*`), TShark and Python clients, PreviewService
   routing and fallback, settings and pack route validation, the pack registry's snapshot and limit.
3. Trust prompts, capability flag, settings dialog, preview pane and tooltip labels.
4. Tests first (`tests/unit/pythondecoder_test.cpp`, `[pythondecoder]`), the public example
   `plugins/examples/python-decoder-pack`, docs, ledger, review and one PR.

Not in this slice: the generic MD03 dataset contract, decoder scans, conversations, embedded
payloads, Python decoders outside previews, an OS sandbox.
