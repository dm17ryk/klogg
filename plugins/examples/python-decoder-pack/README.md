# Example Python decoder pack

A configuration pack (`"kind": "config-pack"`) whose preview is decoded by a Python decoder
(MD03b). It has no program: CILogg runs the decoder in its supervised Python decoder worker.

- `previews.json`: the "Demo record" preview for lines such as `RECORD 020311223366` (type 02,
  length 03, data 112233, checksum 66), in the format of the user's `previews.json`.
- `python/record.py`: `decode(payload, context)` turns the record's bytes into preview rows, with
  the message completeness and the checksum's integrity. It returns `None` for bytes that are not
  a record, and the preview then shows its JSON fields.
- `python/common.py`: a helper that `record.py` imports with `from . import common`. Every module
  of a pack, helpers included, is listed in `content.python_decoders`: only listed modules are part
  of the pack, and each pack's modules are imported in a namespace of their own.
- `content.decoder_routes`: sends the "Demo record" preview to the decoder
  `org.cilogg.example.python-decoder-pack/record`.
- `requires.host_features: ["python_decoders"]`: a CILogg without the Python decoder backend lists
  the pack as "Requires a newer CILogg".

Install it from **Plugins > Plugins and Packs > Installed > Install Folder…** and enable it. The
Python code runs on your computer with your permissions while the pack is enabled, so CILogg asks
first. The Installed tab then lists "Python decoders: record, common". To route a preview of your
own to the decoder, use **Add Python decoder** in Preview Decoder Settings ("Example Python decoder
pack: record"). **Pack Files…** on the Installed tab edits the modules in place, and an update
merges your edits with the new version. See `docs/plugin-repositories.md` section 4.4 and
`docs/plugin-sdk.md`.
