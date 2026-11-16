# Diagnostic reports

When something goes wrong in the background (indexing, checking a file for changes, searching),
CILogg writes a **diagnostic report**: a folder (and a zip of it) with what happened, the steps that
led there and the state of the application and the computer at that moment. It is meant to make a
problem debuggable after the fact, also when it happened during unattended automation and nobody
saw it. Task [R06](../plans/cilogg-workbench-2026-10-21/tasks/R06.md), decision D15.

## What triggers a report

- An error in a background operation (`Indexing a file`, `Indexing new lines of a file`,
  `Checking a file for changes`, `Searching a file`, `Searching new lines of a file`).
  The same problem writes at most one report every 10 minutes; repeats in between are counted
  and the next report says how many there were.
- **Help > Report a problem**: writes a report, opens its folder and a prefilled GitHub issue.
- `cilogg command --action create_report [--text <note>]` (also through MCP and Python).

A background error shows a notice that does not block CILogg, with **Create GitHub issue**,
**Open report folder** and **Close**. In automation mode (`CILOGG_AUTOMATION`, offscreen runs)
the report is written and no notice is shown.

## Where reports are

In `reports` next to `plugin-data`: beside `cilogg.conf` for a portable installation, otherwise
in the application data folder that also holds `plugin-data`. The 20 newest reports are kept.
`cilogg command --action list_reports` lists them, newest first, with the folder.

## What a report contains

| File | Content |
|---|---|
| `report.md` | Plain-language summary: what happened, the file involved, memory now and over the last minutes, the last 60 steps, how to reproduce, the application state and versions. |
| `report.json` | The same as data. |
| `journal.log` | The full activity journal (up to 2,000 entries). |
| `repro.json` | The Commander requests, actions and responses in order, to replay the scenario. |
| `cilogg.log` | Recent log lines: warnings and errors always, lower levels when logging is enabled. |
| `memory.csv` | Memory samples every 10 seconds (the last 10 minutes): CILogg's own memory and the system's free RAM and commit. |
| `context.json` | Windows and tabs (with file sizes), the running scenario and global script, active plugin jobs, the automatic-response switch. |

The **activity journal** is always on and costs little: it keeps the last 2,000 steps in memory.
It records every Commander request that changes something (from the GUI's scripts, the CLI, MCP,
Python and scenarios; read-only `get_*` queries are skipped), every action sent (manual or
automatic) and response fired, COM connections opened, closed, paused and resumed and their
errors, files indexed and reloaded, low-memory warnings and the errors themselves. A step that
repeats (for example a response and its action firing on every line) is kept once with a count.

Payloads are not recorded: action bytes, action parameter values, plugin and analysis arguments
and definitions appear only as their key names, because they may hold credentials.

## Privacy

The report stays on your computer. The GitHub issue (the repository is public) gets only the
summary: the operation, the error, the file **name** (no folder), the memory numbers, the
versions and the report id. It asks you to review the zip and attach it yourself if you want to.

## Memory

A report says whether the computer, not only CILogg, was short of memory: "commit" is the memory
Windows has promised to all processes, limited by RAM plus the page file. When it is almost used
up, any allocation can fail in any program, and CILogg is often only the one that noticed. The
journal records when the system runs low and when CILogg grows by more than 1 GB.

## Not yet included

Crash dumps for real crashes, the stack where an error was thrown, and published Windows debug
symbols are the second slice of R06 (R06b).
