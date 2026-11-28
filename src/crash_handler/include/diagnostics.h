// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Debuggable problem reports (task R06): an always-on activity journal, memory status and
// sampling, report bundles written to a local folder, and the summary that may go to GitHub.
// See docs/diagnostics.md.

#ifndef KLOGG_DIAGNOSTICS_H
#define KLOGG_DIAGNOSTICS_H

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>
#include <utility>

namespace cilogg::diagnostics {

// ------------------------------------------------------------------ activity journal

struct JournalEntry {
    QDateTime time;     // UTC, first occurrence
    QDateTime lastTime; // UTC, last occurrence when repeated
    QString thread;
    QString category;   // command, action, response, com, file, plugin, memory, error, report
    QString text;       // one human-readable line
    QVariantMap data;   // optional structured detail (for example a Commander request)
    int repeats = 1;    // identical consecutive entries are folded into one
};

// A bounded, thread-safe record of what CILogg did recently. An entry identical (same category,
// text and data) to one of the last four is folded into it with a repeat count, so a response
// and its action firing on every line do not push everything else out.
class ActivityJournal {
  public:
    static void record( const QString& category, const QString& text, const QVariantMap& data = {} );
    static QVector<JournalEntry> entries();
    static void clear();
    static void setCapacity( int entries );
    static int capacity();
};

// ------------------------------------------------------------------ memory

struct MemoryStatus {
    QDateTime time; // UTC
    quint64 processPrivateBytes = 0;
    quint64 processWorkingSetBytes = 0;
    quint64 processPeakPrivateBytes = 0;
    quint64 systemTotalPhysicalBytes = 0;
    quint64 systemAvailablePhysicalBytes = 0;
    quint64 systemCommitLimitBytes = 0;     // physical memory plus page file / swap
    quint64 systemCommitAvailableBytes = 0;

    // True when the whole system is close to running out of committable memory.
    bool systemLow() const;
    QString describe() const;
    QVariantMap toVariantMap() const;
};

MemoryStatus currentMemoryStatus();

// Samples memory every interval on the thread that starts it (the GUI thread), keeps the last
// samples, and journals when the system runs low or the process grows sharply.
class MemoryMonitor {
  public:
    static void start( int intervalMs = 10000 );
    static void stop();
    static QVector<MemoryStatus> samples();
    static void addSampleForTest( const MemoryStatus& status );
    static void clearForTest();
};

// ------------------------------------------------------------------ reports

struct ReportRequest {
    ReportRequest() = default;
    // The R06a fields; the R06b ones are set by name.
    ReportRequest( QString kindValue, QString operationValue, QString errorValue,
                   QString filePathValue, QVariantMap detailsValue = {} )
        : kind( std::move( kindValue ) )
        , operation( std::move( operationValue ) )
        , error( std::move( errorValue ) )
        , filePath( std::move( filePathValue ) )
        , details( std::move( detailsValue ) )
    {
    }

    QString kind;      // "exception", "bug", "manual" or "crash"
    QString operation; // what CILogg was doing, e.g. "Checking a file for changes"
    QString error;     // the exception or error text
    QString filePath;  // the file involved, if any (full path stays in the local bundle only)
    QVariantMap details; // extra facts, e.g. sizes; kept in report.json and report.md
    // R06b: where the exception was thrown (currentThrowStack() in the catch block).
    QStringList throwStack;
    // R06b: also write a small all-threads minidump of this process into the report folder
    // (Windows). Like every dump it stays out of the zip.
    bool processDump = false;
    // Files copied into the report folder, and so into its zip (a crash record, say).
    QStringList attachments;
    // Files the report points to but never copies (a crash dump: share it only privately).
    QStringList privateFiles;
};

struct ReportResult {
    bool ok = false;
    QString error;
    QString id;
    QString directory;
    QString archive; // zip of the directory, when it could be written
    QString title;
    QString summary; // plain-language one-liner for the dialog
    QString processDump; // R06b: the minidump written with the report, if any (not in the zip)
};

// R06b: a crash of an earlier run, turned into a report at the next start.
struct CrashReport {
    ReportRequest request;
    ReportResult result;
    QString dump; // the crash dump next to the crash record, if any (never in the zip)
};

class DiagnosticReports {
  public:
    using ContextProvider = std::function<QVariantMap()>;

    static void setRoot( const QString& directory );
    static QString root();
    static void setContextProvider( ContextProvider provider );
    static void setKeep( int reports );

    // Writes a report folder (and zip) under root(). Call on the GUI thread: the context
    // provider reads windows and tabs.
    static ReportResult create( const ReportRequest& request );

    // R06b: a report for each crash record that earlier runs left in CrashDumps::folder() and
    // that has no report yet (the record is attached, the dump only named). Oldest first.
    static QVector<CrashReport> reportPendingCrashes();

    // R06b: what every notice and report says about a dump (decision D16).
    static QString dumpPrivacyNote();

    // Newest first: id, kind, created, title, summary, directory, archive.
    static QVariantList list();
    static QVariantMap find( const QString& id );

    // The text for a public GitHub issue: no paths, no log content, only the summary, versions
    // and the report id, plus a request to attach the zip after reviewing it.
    static QString issueTitle( const ReportResult& result, const ReportRequest& request );
    static QString issueBody( const ReportResult& result, const ReportRequest& request );
};

// Limits how often the same problem produces a report and a dialog.
class ReportThrottle {
  public:
    explicit ReportThrottle( qint64 intervalMs = 10 * 60 * 1000 );
    // True when a report should be written now; otherwise counts the suppressed repeat. When it
    // returns true, *suppressedBefore receives the repeats suppressed since the previous report.
    bool shouldReport( const QString& key, const QDateTime& now, int* suppressedBefore = nullptr );
    // Repeats suppressed since the last report for this key (reset by shouldReport() == true).
    int suppressed( const QString& key ) const;

  private:
    struct State {
        QDateTime last;
        int suppressed = 0;
    };
    qint64 intervalMs_;
    QMap<QString, State> states_;
};

QString environmentSummary();

} // namespace cilogg::diagnostics

#endif
