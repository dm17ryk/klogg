// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Crash dumps and throw-site stacks (task R06, slice R06b). See docs/diagnostics.md.
//
// - A crash (an unhandled exception or a fatal signal, std::terminate, abort) writes a crash
//   record (`crash-<time>-<pid>.txt`) into a local folder and, on Windows, a minidump next to it
//   (`.dmp`). Nothing is uploaded. The next start turns each new record into a diagnostic report.
// - The stack where the most recent C++ exception of a thread was thrown is kept per thread, so a
//   caught background error can say where it came from.
// - A dump is a memory image and can hold secrets that were in use (decision D16): dumps never go
//   into a report's zip.

#ifndef KLOGG_CRASHDUMPS_H
#define KLOGG_CRASHDUMPS_H

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace cilogg::diagnostics {

class CrashDumps {
public:
    // Installs the crash handlers and sets the folder (created when needed). Every call sets the
    // handlers again, so calling it after a library that installs its own (Sentry) restores
    // CILogg's. Returns false, and installs nothing, when the folder cannot be created.
    static bool install( const QString& folder );
    // Only sets (and creates) the folder, without installing handlers: for tests, which keep
    // their own crash handlers. An empty folder clears it (a crash then writes nothing). Setting
    // the same folder again changes nothing.
    static bool setFolder( const QString& folder );
    static bool installed();
    static QString folder();

    // Full-memory dumps are an opt-in setting (large, and they hold everything in memory). The
    // default is a small dump: all threads' stacks and the loaded modules.
    static void setFullMemory( bool fullMemory );
    static bool fullMemory();

    // True where CILogg writes minidumps (Windows). Elsewhere a crash leaves the text record
    // (with the stack) and, when the system allows it, a core dump.
    static bool minidumpsSupported();

    // Writes a minidump of this process now (all threads, no crash). False and *error where
    // minidumps are not supported or the file cannot be written.
    // It waits at most 30 s (MiniDumpWriteDump suspends every other thread, and one holding a lock
    // can block it). A dump that finishes after that is removed: no report names it.
    static bool writeProcessDump( const QString& path, bool fullMemory, QString* error = nullptr );
    // Test hook: the wait above in milliseconds (negative: back to 30 s).
    static void setProcessDumpTimeoutForTest( int milliseconds );

    // Crash records left in folder() by earlier runs that were not turned into a report yet,
    // oldest first: {record, dump (may be empty), time, summary, text}. A record younger than 10 s
    // may still be being written and is skipped; a dump the record does not call written (cut
    // short) is removed.
    static QVariantList pendingCrashes();
    // Marks a record as reported, so pendingCrashes() no longer returns it.
    static void markReported( const QString& record );
    // Keeps the newest `keep` crash records (and their dumps and marks), and the dumps within
    // 2 GiB in all (the newest dump is always kept).
    static void prune( int keep = 10 );

    // Test hook: write the crash record (and dump) the handler would write for a crash with this
    // reason, without crashing. Returns the record path.
    static QString writeCrashForTest( const QString& reason );
};

// Starts recording, per thread, the stack where each C++ exception is thrown. Cheap: only the
// return addresses are kept; they are turned into text when asked for.
void installThrowSiteCapture();

// The stack where the most recent C++ exception of the calling thread was thrown, as text frames
// ("module+0xoffset", with the function name when symbols are at hand). Call it in the catch
// block of the thread that caught the exception. Empty when nothing was recorded.
QStringList currentThrowStack();

} // namespace cilogg::diagnostics

#endif
