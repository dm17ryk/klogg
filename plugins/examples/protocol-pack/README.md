# Example protocol pack

A configuration pack (`"kind": "config-pack"`) for a synthetic demo protocol. It has no program:
CILogg reads its content.

- `previews.json`: the "Demo frame" preview for lines such as `DEMO 0103414243` (type 01, length
  03, data 414243), in the format of the user's `previews.json`.
- `lua/common.lua` and `lua/demo.lua`: a TShark dissector, loaded in this order. Their globals
  (`DEMO_TYPES`) are shared with each other and with no other pack.
- `content.decoder_routes`: sends the "Demo frame" preview to the `cilogg_demo` dissector when the
  preview decoder uses TShark; without TShark the preview falls back to its JSON fields.

Install it from **Plugins > Plugins and Packs > Installed > Install Folder…** and enable it. The
Lua runs on your computer with your permissions while the pack is enabled, so CILogg asks first.
See `docs/plugin-repositories.md` section 4 and `docs/plugin-sdk.md`.
