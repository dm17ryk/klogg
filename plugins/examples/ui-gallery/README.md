# UI Gallery

A small tour of the plugin view features of the advanced plugin SDK (see `docs/plugin-sdk.md`
and `docs/plugin-sdk-advanced.md`). It grows with each E17 slice. It is disabled by default;
enable it in **Plugins → Manage Plugins…**, then open **Plugins → UI Gallery**.

## What it shows

- **Static tooltips** on a label, a field, a button and the table column headers
  (`table_column(..., tooltip=...)`), and the existing row tooltip.
- **Advanced tooltips** that CILogg asks the plugin for when the pointer rests on an element
  with `tooltip_provider=True`:
  - the **Fleet summary** button: a title, text, name/value rows, a tone, a footer and a
    registered icon (`@plugin.tooltip("gallery", "summary")`);
  - every cell of the **Boards** table: the query carries `row_id` and `column_id`
    (`@plugin.tooltip("gallery", "boards")`); each row has a `tooltip_key` that changes with its
    data, so CILogg never shows a stale cached tooltip;
  - the status label, answered by the view-wide handler (`@plugin.tooltip("gallery")`) with a
    plain string.
- **A registered icon:** an SVG chip registered with `register_icons` and `svg_icon(...,
  mask=True)` when the view opens, used in tooltips as `image_ref("chip")`.
- **Invalidation:** **Change data** changes the boards, patches the rows and the status label
  (a patch drops their cached tooltips) and calls `plugin.invalidate_tooltips("gallery",
  "summary")` for the summary button, which it does not patch.
- **Accessible names:** `accessible_name` and `accessible_description` on the summary button,
  the field and the table.

`plugin.json` lists the query methods the plugin answers in `handles`
(`tooltip/resolve`, `query/cancel`); without them CILogg never sends the queries, and the SDK
warns on stderr at start.

## On an older CILogg

The gallery still loads. The SDK leaves out what the host does not know: `tooltip_provider`,
`tooltip_key` and column tooltips without the `tooltips` feature, accessible names without
`views` version 2; `register_icons` and `invalidate_tooltips` send nothing. The intro label then
says that advanced tooltips need a newer CILogg.

## Testing without CILogg

```python
from cilogg_plugin_testing import FakeHost

with FakeHost.start_package("plugins/examples/ui-gallery") as host:  # with cilogg_plugin.py copied in
    host.initialize()
    host.open_view("gallery")
    host.wait_for("view/update")
    print(host.resolve_tooltip("gallery", "boards", row_id="b2", column_id="state"))
```

`tests/plugins/test_ui_gallery.py` does this for every tooltip, on a current and an older host.
