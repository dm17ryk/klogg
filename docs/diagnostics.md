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

## Crashes and dumps

When CILogg itself crashes, it writes a **crash record** into `reports/crashes`. A crash is:

- an access violation or another unhandled exception;
- a fatal signal (`SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL`, `SIGABRT`);
- an uncaught C++ exception (`std::terminate`);
- `abort()`.

The record is a text file, `crash-<time>-<pid>.txt`, holding:

- the reason (with the uncaught exception's text, when there is one);
- the version and commit;
- the stack of the crashing thread;
- the stack where that thread last threw a C++ exception.

On Windows CILogg also writes a **minidump** next to the record (`.dmp`). By default it is small: every thread's stack, the loaded modules and the thread data. CILogg writes the record before the dump, so a dump that takes too long or is cut short still leaves a record. A dump the record does not call written is removed at the next start. The exception is a dump that is still being written: while the record has no result and the crashed process still runs (for at most an hour), a starting CILogg leaves it alone. DbgHelp, which writes the dump, is not thread-safe. If another thread holds it for more than 5 seconds, the crash leaves its record without a dump. On Linux and macOS there is no minidump. The record says whether the system keeps a core dump (the core dump limit), and CILogg does not change that limit.

Nothing is uploaded. The 10 newest crash records are kept, and their dumps are kept within 2 GiB in all (the newest dump is always kept).

The crash handlers work from buffers prepared in advance and avoid the heap and locks where they can. That is best effort, not a guarantee, in a process that is already broken. On Linux and macOS a stack overflow on a thread other than the main thread ends CILogg without a record.

The next start turns each new crash record into a **crash report**, a report folder like the ones above:

- The report holds the crash record.
- It names the dump file, but it does not copy the dump, and the zip never contains it. The notice shows the dump's full path. The zipped report gives only the file name, since a full path holds the user name.
- At most the 3 newest crashes get a report at one start. Older ones are only marked, and their records stay in `reports/crashes`.
- A record written in the last 10 seconds may belong to another CILogg that is still writing it, so it waits for a later start.
- A notice shows the newest crash. In automation only the report is written.

**Full-memory crash dumps** (*Options > Advanced > Logging*) make the Windows crash dump hold all of CILogg's memory. Such a dump is much larger and can hold anything that was in memory. The setting is off by default, and it applies only to crashes.

### Where an error was thrown

CILogg records, for every thread, the stack where its latest C++ exception was thrown. A background error report gets a **Where the error was thrown** section with those frames, newest first.

On Windows a report of a background error also gets a small minidump of the whole process, `private/process.dmp`. It sits in a subfolder of the report folder and is never in the zip.

A second CILogg process writes it (CILogg started with an internal option). A dump written from inside the running process would suspend all its threads, the waiting one included, and could block on a lock one of them holds. If the helper takes more than 30 seconds, it is ended, its partial dump is removed and the report goes on without it.

Platform limits:

- **Windows:** frames are `module+offset`, plus the function name where a `.pdb` is next to the binary (a development build).
- **Linux:** every throw is recorded, also those inside Qt and the C++ library.
- **macOS:** only the throws compiled into CILogg itself are recorded, and function names stay mangled.
- On Windows, a `throw;` that rethrows a caught exception records the rethrow's site; on Linux and macOS the original site stays.

### Debug symbols

Every release publishes the symbols that turn `module+offset` frames and dumps into function names:

- Windows: `cilogg-<version>-<arch>-Qt6-pdb.zip`. Release builds write a `.pdb` for each binary, and the Windows package step fails if `cilogg.pdb` is missing.
- Linux: `cilogg-<version>-symbols.tar.xz`.

To symbolize a Windows dump, unpack the matching `-pdb.zip` next to the dump and open the dump in WinDbg or Visual Studio. The `.pdb` files must come from the same release as the dump.

### Privacy of dumps

A dump is an image of CILogg's memory. It can hold passwords, tokens (for example a plugin repository token, decision D16) and log contents that were in use. That is why dumps stay out of every report zip, and why the notice and `report.md` say where the dump is and that it must be shared only privately with the maintainer, never in a public issue. The crash record and the throw-site frames hold no memory contents. The public GitHub issue text gets neither of them.

### Sentry builds

A build with `KLOGG_USE_SENTRY=ON` still starts Sentry (no DSN is configured). CILogg then sets its own crash handlers again, so the local crash record and dump are written as in every other build.
