"""Version-skew fixture: a plugin that uses only the SDK API of before E17, so it runs on the
pinned SDK copy (tests/plugins/fixtures/sdk_pinned) and on the current one."""
from cilogg_plugin import Plugin, button, column, label, set_props

plugin = Plugin()


@plugin.action("echo")
def echo(job, arguments):
    job.progress(1.0)
    return {"echo": arguments.get("text", ""), "features": plugin.host_features}


@plugin.on_view_opened
def opened(params):
    plugin.update(params["view_id"],
                  column(label("status", "ready", tooltip="Status"), button("go", "Go", event=True),
                         id="body"),
                  params.get("window_id"))


@plugin.on_view_event
def clicked(params):
    if params.get("element_id") == "go":
        plugin.patch(params["view_id"], [set_props("status", text="clicked")], params.get("window_id"))


if __name__ == "__main__":
    plugin.serve()
