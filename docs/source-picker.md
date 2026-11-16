# Choosing what a tool works on

Every CILogg tool that works on "a tab" offers the same choice of source:

| Choice | Text form | Meaning |
|---|---|---|
| Focused tab | `focused` | The current tab of the window (of its active pane). |
| All open files | `all` | Every open tab of every CILogg window, however many there are. |
| A tab | `tab:<tab id>` | One open tab of any window. Tab ids are listed by `get_info`. |
| File… | `file:<path>` | The tab that shows this file. A file that is not open is opened first. |

The picker lists every open tab of every window. A source a tool cannot use is
listed disabled; hover over it to see why. The text form is what the CLI
(`--source`), native MCP (`source`) and Python (`source=`) take. An explicit
Commander tab selector (`--tab-id`, or `--window-index` with `--tab-index`) wins
over `--source`.

## Where the picker is

| Tool | Picker | Sources | Automation |
|---|---|---|---|
| Regex Lab, Value Counts | **Source** list of the dock | all four | `set_regex_lab` / `set_value_counts` with `"source"` ([Regex Lab](regex-lab.md#choosing-the-source)) |
| Plugin actions | **Plugins → Run actions on**, and the **Run on** row of an action's form | all four; **All open files** runs the action once per tab | `invoke_plugin --source …` |
| Plugin views | **Source** row at the top of the view | focused, a tab, a file | `open_plugin_view --plugin-id <id> --view-id <view> --source …` |
| Script Runner | The list beside **Run On** | focused, a COM tab, all COM tabs (one run per COM tab); a file cannot run a script | `run_script --source …` |
| Preview | **Source** and **Line** at the top of the Preview window, then **Preview line** | focused, a tab, a file | `preview_message --source … --analysis-json '{"line": N}'` |
| Search | **All Tabs** button of the search line | every open tab | `search --source all\|tab:<id>\|file:<path> [--wait]` |

- **Plugin actions on all open files** start one job per tab. At most eight of
  them run at a time; the others wait and start as jobs finish, so every tab
  gets its job. `invoke_plugin --source all` returns the tabs and their job ids
  so far (`sources`); each job's `context` names its tab.
- **Plugin views** keep the source you choose: the plugin is told again
  (`view/opened` with the new tab's `context`), and the view's buttons run on that
  tab. See [plugin views](plugin-views.md).
- **Search in all open tabs** runs the search in each tab's filtered view
  without switching tabs. The search line then shows the total, and its tooltip
  the matches of each tab. `search --source all` returns `tabs` (each tab's
  `matches` and `complete`), `matches` and `complete`; with `--wait` it returns
  once every tab has finished (`--timeout-ms`, default 20000, at most 25000 so
  the reply arrives within the client's 30 s deadline). If a tab is still
  searching when the wait runs out, the command fails with reason `timeout`; its
  payload keeps the counts so far, and the search goes on in the window.
- **Plugin actions on all open tabs**: if no tab can start (an unknown action,
  a disabled plugin), the command fails with that reason, as it does for one
  tab. A full plugin manager is not a failure: the tabs wait their turn.
- From **Python** (`klogg.app.command`), relative paths (`file`, `script_file`,
  `args_json_file`, ..., and the path of a `file:` source) are made absolute in
  the script's working directory, as the CLI does in its own.
- **Preview** reads one line, so it needs one source: `--source all` is refused
  with `invalid_source`.
- **Script Runner** needs a live session: tabs that are not COM captures are
  listed disabled, `--source tab:<id>` of such a tab is refused with
  `not_com_tab`, and `--source all` without COM tabs with `no_open_sources`.
- A file that is not open is opened in a tab first; the command then returns
  `opened` and you run it again once the file has loaded. Plugin actions and
  views use the new tab right away.

Search, filters, follow, Quick Find, encodings, save and file operations keep
working on the tab you are looking at. The Actions window lists the COM tabs that
can send actions.

## Python

Scripts reach the same operations through `klogg.app.command(action, **request)`,
with the CLI / native MCP argument names:

```python
from klogg import app

app.command("search", text="ERROR", source="all", wait=True)
app.command("invoke_plugin", plugin_id="org.example.tool", plugin_action="run",
            source="tab:" + tab_id)
app.set_regex_lab({"pattern": r"(?<code>E\d+)", "scope": "whole_file",
                   "source": "all", "operation": "run"}, window_index=0)
```

`app.set_regex_lab(definition)` and `app.set_value_counts(definition)` set the
docks of a window (`window_index`, default the active window).
