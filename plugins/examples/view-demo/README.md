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

The same actions run from the CLI and MCP, for example:

```text
cilogg command --action invoke_plugin --plugin-id org.cilogg.view-demo --plugin-action process
```
