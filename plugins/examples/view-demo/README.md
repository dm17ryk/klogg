# View Demo

A demonstration of plugin views (see `docs/plugin-views.md`). It is disabled by default; enable it
in **Plugins → Manage Plugins…**.

- **Plugins → View Demo** opens a dock with a form (text, choice, boolean, file and folder
  fields), job buttons, a confirmation button, a save-dialog button and a 10,000-row table that the
  plugin patches one row at a time.
- **Process** starts a normal plugin job with the form values as `arguments.inputs` and reports
  progress in the dock.
- **Reset table** asks for confirmation first; **Export CSV…** asks for a file name and receives it
  as `arguments.save_path`.
- **Ask** and **Timed notice** show host message boxes; the notice closes itself after five
  seconds unless you interact with it.
- **Open editor** and **Open dialog** show the window and dialog views with `view/show`.

## Editable cells

The small **Setting / Value** table shows editable table cells (host feature `view_edit`, check
`plugin.supports_view_edit`). The Value column is declared with `table_column(..., editable=True)`
and each row carries its own `editor(...)` for that cell: a choice, a multichoice (checked values
are a list), a number (1 to 100) and a boolean.

When the user edits a cell the host sends a `view/event` of type `edited` with `row_id`,
`column_id`, the new `value` and the `previous` text. The cell stays pending until the plugin
patches the row back with `update_rows`: an accepted edit comes back with tone `accent` and a
`was <previous>` tooltip, a rejected one with the old text, tone `error` and the reason as tooltip.

The same actions run from the CLI and MCP, for example:

```text
cilogg command --action invoke_plugin --plugin-id org.cilogg.view-demo --plugin-action process
```
