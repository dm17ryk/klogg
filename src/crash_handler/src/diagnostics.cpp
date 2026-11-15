// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "diagnostics.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <deque>
#include <iterator>
#include <memory>

#include <kzip.h>

#include "klogg_version.h"
#include "log.h"
#include "logger.h"

#if defined( Q_OS_WIN )
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <psapi.h>
#elif defined( Q_OS_MACOS )
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <unistd.h>
#else
#include <QTextStream>
#endif

namespace cilogg::diagnostics {

namespace {

const QDateTime ProcessStart = QDateTime::currentDateTimeUtc();

constexpr int DefaultJournalCapacity = 2000;
constexpr std::size_t FoldLookBack = 4;
constexpr int MemorySampleCapacity = 60;
constexpr int DefaultKeep = 20;
constexpr int StepsInSummary = 60;
constexpr quint64 GiB = 1024ull * 1024ull * 1024ull;

QString threadLabel()
{
    auto* thread = QThread::currentThread();
    if ( thread != nullptr && !thread->objectName().isEmpty() ) {
        return thread->objectName();
    }
    if ( QCoreApplication::instance() != nullptr
         && thread == QCoreApplication::instance()->thread() ) {
        return QStringLiteral( "main" );
    }
    return QStringLiteral( "0x%1" ).arg( reinterpret_cast<quintptr>( QThread::currentThreadId() ),
                                         0, 16 );
}

QString gib( quint64 bytes )
{
    return QString::number( static_cast<double>( bytes ) / static_cast<double>( GiB ), 'f', 2 )
           + QStringLiteral( " GB" );
}

QString isoUtc( const QDateTime& time )
{
    return time.toUTC().toString( Qt::ISODateWithMs );
}

QString localClock( const QDateTime& time )
{
    return time.toLocalTime().toString( QStringLiteral( "HH:mm:ss.zzz" ) );
}

struct JournalState {
    QMutex mutex;
    std::deque<JournalEntry> entries;
    int capacity = DefaultJournalCapacity;
};

JournalState& journal()
{
    static JournalState state;
    return state;
}

struct MonitorState {
    QMutex mutex;
    std::deque<MemoryStatus> samples;
    QPointer<QTimer> timer; // a child of the application object, so never outlives it
    bool lowReported = false;
    quint64 journaledPrivate = 0;
};

MonitorState& monitor()
{
    static MonitorState state;
    return state;
}

struct ReportsState {
    QMutex mutex;
    QString root;
    DiagnosticReports::ContextProvider provider;
    int keep = DefaultKeep;
};

ReportsState& reports()
{
    static ReportsState state;
    return state;
}

void pushSample( const MemoryStatus& status )
{
    auto& state = monitor();
    QMutexLocker locker( &state.mutex );
    state.samples.push_back( status );
    while ( static_cast<int>( state.samples.size() ) > MemorySampleCapacity ) {
        state.samples.pop_front();
    }
}

#if !defined( Q_OS_WIN ) && !defined( Q_OS_MACOS )
QMap<QString, quint64> readKibFields( const QString& path )
{
    QMap<QString, quint64> fields;
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        return fields;
    }
    QTextStream stream( &file );
    while ( !stream.atEnd() ) {
        const auto line = stream.readLine();
        const auto colon = line.indexOf( ':' );
        if ( colon <= 0 ) {
            continue;
        }
        const auto value = line.mid( colon + 1 ).trimmed().section( ' ', 0, 0 );
        bool ok = false;
        const auto number = value.toLongLong( &ok );
        if ( ok ) {
            fields.insert( line.left( colon ), static_cast<quint64>( qMax( 0LL, number ) ) * 1024ull );
        }
    }
    return fields;
}
#endif

bool writeText( const QString& path, const QByteArray& bytes )
{
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) ) {
        return false;
    }
    file.write( bytes );
    return file.commit();
}

bool writeJson( const QString& path, const QVariant& value )
{
    return writeText( path, QJsonDocument::fromVariant( value ).toJson( QJsonDocument::Indented ) );
}

QString entryLine( const JournalEntry& entry )
{
    auto line = QStringLiteral( "%1 [%2] %3: %4" )
                    .arg( isoUtc( entry.time ), entry.thread, entry.category, entry.text );
    if ( entry.repeats > 1 ) {
        line += QStringLiteral( " (x%1, last %2)" ).arg( entry.repeats ).arg( isoUtc( entry.lastTime ) );
    }
    return line;
}

QVariantMap entryMap( const JournalEntry& entry )
{
    QVariantMap map{ { QStringLiteral( "time" ), isoUtc( entry.time ) },
                     { QStringLiteral( "thread" ), entry.thread },
                     { QStringLiteral( "category" ), entry.category },
                     { QStringLiteral( "text" ), entry.text },
                     { QStringLiteral( "repeats" ), entry.repeats } };
    if ( entry.repeats > 1 ) {
        map.insert( QStringLiteral( "lastTime" ), isoUtc( entry.lastTime ) );
    }
    if ( !entry.data.isEmpty() ) {
        map.insert( QStringLiteral( "data" ), entry.data );
    }
    return map;
}

QVariantMap environmentMap()
{
    return { { QStringLiteral( "version" ), QString::fromLatin1( kloggVersion().data() ) },
             { QStringLiteral( "buildDate" ), QString::fromLatin1( kloggBuildDate().data() ) },
             { QStringLiteral( "commit" ), QString::fromLatin1( kloggCommit().data() ) },
             { QStringLiteral( "os" ), QSysInfo::prettyProductName() },
             { QStringLiteral( "kernel" ),
               QSysInfo::kernelType() + QLatin1Char( ' ' ) + QSysInfo::kernelVersion() },
             { QStringLiteral( "cpu" ), QSysInfo::currentCpuArchitecture() },
             { QStringLiteral( "abi" ), QSysInfo::buildAbi() },
             { QStringLiteral( "qt" ), QString::fromLatin1( qVersion() ) },
             { QStringLiteral( "concurrency" ), QThreadPool::globalInstance()->maxThreadCount() },
             { QStringLiteral( "processStart" ), isoUtc( ProcessStart ) },
             { QStringLiteral( "pid" ), QCoreApplication::applicationPid() } };
}

QString kindTitle( const QString& kind )
{
    if ( kind == QLatin1String( "exception" ) ) {
        return QStringLiteral( "Exception report" );
    }
    if ( kind == QLatin1String( "bug" ) ) {
        return QStringLiteral( "Problem report" );
    }
    return QStringLiteral( "Diagnostic report" );
}

QString firstLine( const QString& text, int maximum )
{
    auto line = text.section( QLatin1Char( '\n' ), 0, 0 ).trimmed();
    if ( line.size() > maximum ) {
        line = line.left( maximum - 3 ) + QStringLiteral( "..." );
    }
    return line;
}

QString summaryFor( const ReportRequest& request, const MemoryStatus& memory )
{
    QString summary;
    const auto fileName = QFileInfo( request.filePath ).fileName();
    if ( !request.operation.isEmpty() && request.error.isEmpty() ) {
        summary = QStringLiteral( "%1: CILogg saved a diagnostic report." ).arg( request.operation );
    }
    else if ( !request.operation.isEmpty() ) {
        summary = QStringLiteral( "While %1" ).arg( request.operation.left( 1 ).toLower()
                                                    + request.operation.mid( 1 ) );
        if ( !fileName.isEmpty() ) {
            summary += QStringLiteral( " (%1)" ).arg( fileName );
        }
        summary += request.error.isEmpty() ? QStringLiteral( ", CILogg saved a report." )
                                           : QStringLiteral( ", CILogg hit an error: %1." )
                                                 .arg( firstLine( request.error, 200 ) );
    }
    else if ( !request.error.isEmpty() ) {
        summary = QStringLiteral( "CILogg hit an error: %1." ).arg( firstLine( request.error, 200 ) );
    }
    else {
        summary = QStringLiteral( "CILogg saved a diagnostic report." );
    }
    if ( memory.systemLow() ) {
        summary += QStringLiteral( " The computer was almost out of memory: %1." ).arg( memory.describe() );
    }
    return summary;
}

// Report ids: UTC time, a per-process counter and the process id, so names sort in creation order
// and two processes never collide. Only names of this form are ever listed or pruned.
const QRegularExpression& reportIdPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral( "^\\d{8}-\\d{6}-\\d{3}-\\d{4}-\\d+-[a-z]+$" ) );
    return pattern;
}

QStringList reportNames( const QDir& dir )
{
    QStringList names;
    for ( const auto& name : dir.entryList( QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name ) ) {
        if ( reportIdPattern().match( name ).hasMatch() ) {
            names.push_back( name );
        }
    }
    return names;
}

void pruneReports( const QString& root, int keep )
{
    QDir dir( root );
    auto names = reportNames( dir );
    while ( names.size() > keep ) {
        const auto oldest = names.takeFirst();
        QDir( dir.filePath( oldest ) ).removeRecursively();
        QFile::remove( dir.filePath( oldest + QStringLiteral( ".zip" ) ) );
    }
    // Archives whose report folder is gone (for example removed by hand).
    for ( const auto& zip : dir.entryList( { QStringLiteral( "*.zip" ) }, QDir::Files ) ) {
        const auto name = zip.chopped( 4 );
        if ( reportIdPattern().match( name ).hasMatch() && !dir.exists( name ) ) {
            QFile::remove( dir.filePath( zip ) );
        }
    }
}

// Absolute paths in error texts become their last component: the public issue must not show
// folders (user names, project names).
QString withoutFolderPatterns( const QString& text );

QString withoutFolders( const QString& text, const QString& knownFile = {} )
{
    auto scrubbed = text;
    if ( !knownFile.isEmpty() ) {
        // The file of the report, even with spaces in its folders, becomes its name.
        const QFileInfo info( knownFile );
        for ( const auto& form :
              { knownFile, QString( knownFile ).replace( QLatin1Char( '/' ), QLatin1Char( '\\' ) ),
                QString( knownFile ).replace( QLatin1Char( '\\' ), QLatin1Char( '/' ) ),
                QDir::toNativeSeparators( info.absoluteFilePath() ) } ) {
            if ( form.size() > info.fileName().size() ) {
                scrubbed.replace( form, info.fileName(), Qt::CaseInsensitive );
            }
        }
    }
    return withoutFolderPatterns( scrubbed );
}

QString withoutFolderPatterns( const QString& text )
{
    static const QRegularExpression windowsPath( QStringLiteral(
        R"((?:[A-Za-z]:|\\\\[^\\/\s]+)[\\/](?:[^\\/\s"'<>|:*?]+[\\/])*([^\\/\s"'<>|:*?]*))" ) );
    // What a space left of a Windows path ("C:\Program Files\x\y.log" -> "Files\x\y.log").
    static const QRegularExpression backslashPath(
        QStringLiteral( R"((?:[^\s\\/"'<>|]+\\)+([^\s\\/"'<>|]*))" ) );
    static const QRegularExpression posixPath(
        QStringLiteral( R"((?<![\w.~])~?/(?:[^/\s"'<>]+/)+([^/\s"'<>]*))" ) );
    auto result = text;
    for ( const auto& home : { QDir::homePath(), QDir::toNativeSeparators( QDir::homePath() ) } ) {
        if ( home.size() > 1 ) {
            result.replace( home, QStringLiteral( "~" ), Qt::CaseInsensitive );
        }
    }
    result.replace( windowsPath, QStringLiteral( "\\1" ) );
    result.replace( backslashPath, QStringLiteral( "\\1" ) );
    result.replace( posixPath, QStringLiteral( "\\1" ) );
    return result;
}

QString markdownFor( const ReportResult& result, const ReportRequest& request,
                     const QVariantMap& fileInfo, const MemoryStatus& memory,
                     const QVector<MemoryStatus>& samples, const QVector<JournalEntry>& entries,
                     const QVariantMap& context )
{
    QString md;
    md += QStringLiteral( "# %1\n\n" ).arg( result.title );
    md += result.summary + QStringLiteral( "\n\n" );

    md += QStringLiteral( "## What happened\n\n" );
    md += QStringLiteral( "- Report id: `%1`\n" ).arg( result.id );
    md += QStringLiteral( "- Time: %1 (local), %2 (UTC)\n" )
              .arg( QDateTime::currentDateTime().toString( Qt::ISODate ),
                    isoUtc( QDateTime::currentDateTimeUtc() ) );
    if ( !request.operation.isEmpty() ) {
        md += QStringLiteral( "- CILogg was: %1\n" ).arg( request.operation );
    }
    if ( !request.error.isEmpty() ) {
        md += QStringLiteral( "- Error: `%1`\n" ).arg( request.error );
    }
    if ( !request.filePath.isEmpty() ) {
        md += QStringLiteral( "- File: `%1`" ).arg( request.filePath );
        if ( fileInfo.value( QStringLiteral( "exists" ) ).toBool() ) {
            md += QStringLiteral( " (%1 bytes, modified %2)" )
                      .arg( fileInfo.value( QStringLiteral( "size" ) ).toLongLong() )
                      .arg( fileInfo.value( QStringLiteral( "modified" ) ).toString() );
        }
        md += QLatin1Char( '\n' );
    }
    for ( auto it = request.details.cbegin(); it != request.details.cend(); ++it ) {
        md += QStringLiteral( "- %1: `%2`\n" ).arg( it.key(), it.value().toString() );
    }
    md += QStringLiteral( "- Process uptime: %1 s\n\n" )
              .arg( ProcessStart.secsTo( QDateTime::currentDateTimeUtc() ) );

    md += QStringLiteral( "## Memory\n\n- Now: %1\n" ).arg( memory.describe() );
    if ( !samples.isEmpty() ) {
        const auto& first = samples.front();
        md += QStringLiteral( "- %1 samples from %2 to now; CILogg private memory %3 -> %4\n" )
                  .arg( samples.size() )
                  .arg( localClock( first.time ) )
                  .arg( gib( first.processPrivateBytes ), gib( memory.processPrivateBytes ) );
    }
    md += QStringLiteral( "- All samples: `memory.csv`\n\n" );

    md += QStringLiteral( "## Steps that led here\n\n" );
    md += QStringLiteral( "The activity journal, oldest first (the last %1 of %2 entries; all of them "
                          "are in `journal.log`, and the commands in `repro.json`).\n\n" )
              .arg( qMin<qsizetype>( StepsInSummary, entries.size() ) )
              .arg( entries.size() );
    const auto firstStep = qMax<qsizetype>( 0, entries.size() - StepsInSummary );
    for ( auto index = firstStep; index < entries.size(); ++index ) {
        const auto& entry = entries.at( index );
        md += QStringLiteral( "%1. %2 %3: %4" )
                  .arg( index - firstStep + 1 )
                  .arg( localClock( entry.time ), entry.category, entry.text );
        if ( entry.repeats > 1 ) {
            md += QStringLiteral( " (x%1, last %2)" ).arg( entry.repeats ).arg( localClock( entry.lastTime ) );
        }
        md += QLatin1Char( '\n' );
    }
    if ( entries.isEmpty() ) {
        md += QStringLiteral( "No activity was recorded.\n" );
    }
    md += QLatin1Char( '\n' );

    md += QStringLiteral( "## How to reproduce\n\n" );
    md += QStringLiteral(
        "1. Start CILogg %1 with the tabs listed in `context.json` (same files, ports and "
        "filters).\n"
        "2. Replay the commands of `repro.json` in order (each entry is a Commander request, the "
        "same fields `cilogg command` takes), including the automatic actions and responses that "
        "fired between them.\n"
        "3. Watch for the error above; the memory state in `memory.csv` shows whether the computer "
        "was short of memory at the time.\n\n" )
              .arg( QString::fromLatin1( kloggVersion().data() ) );

    md += QStringLiteral( "## State when the report was written\n\n```json\n%1```\n\n" )
              .arg( QString::fromUtf8(
                  QJsonDocument::fromVariant( context ).toJson( QJsonDocument::Indented ) ) );

    md += QStringLiteral( "## Environment\n\n%1\n" ).arg( environmentSummary() );
    md += QStringLiteral( "## Files\n\n"
                          "- `report.json`: this report as data\n"
                          "- `journal.log`: the full activity journal\n"
                          "- `repro.json`: the Commander requests in order\n"
                          "- `cilogg.log`: recent CILogg log lines (warnings and errors always)\n"
                          "- `memory.csv`: memory samples\n"
                          "- `context.json`: windows, tabs and running automation\n" );
    return md;
}

QByteArray memoryCsv( const QVector<MemoryStatus>& samples )
{
    QByteArray csv = "time_utc,process_private,process_working_set,process_peak_private,"
                     "system_total_physical,system_available_physical,system_commit_limit,"
                     "system_commit_available\n";
    for ( const auto& sample : samples ) {
        csv += QStringLiteral( "%1,%2,%3,%4,%5,%6,%7,%8\n" )
                   .arg( isoUtc( sample.time ) )
                   .arg( sample.processPrivateBytes )
                   .arg( sample.processWorkingSetBytes )
                   .arg( sample.processPeakPrivateBytes )
                   .arg( sample.systemTotalPhysicalBytes )
                   .arg( sample.systemAvailablePhysicalBytes )
                   .arg( sample.systemCommitLimitBytes )
                   .arg( sample.systemCommitAvailableBytes )
                   .toUtf8();
    }
    return csv;
}

} // namespace

// ------------------------------------------------------------------ journal

void ActivityJournal::record( const QString& category, const QString& text, const QVariantMap& data )
{
    const auto now = QDateTime::currentDateTimeUtc();
    auto& state = journal();
    QMutexLocker locker( &state.mutex );
    // Fold into one of the last few entries, so a cycle such as "response fired, action sent"
    // repeating on every line stays two entries with counts.
    const auto lookBack = qMin<std::size_t>( FoldLookBack, state.entries.size() );
    for ( std::size_t offset = 1; offset <= lookBack; ++offset ) {
        auto& previous = state.entries[ state.entries.size() - offset ];
        if ( previous.category == category && previous.text == text && previous.data == data ) {
            ++previous.repeats;
            previous.lastTime = now;
            return;
        }
    }
    state.entries.push_back( JournalEntry{ now, now, threadLabel(), category, text, data, 1 } );
    while ( static_cast<int>( state.entries.size() ) > state.capacity ) {
        state.entries.pop_front();
    }
}

QVector<JournalEntry> ActivityJournal::entries()
{
    auto& state = journal();
    QMutexLocker locker( &state.mutex );
    QVector<JournalEntry> copy;
    copy.reserve( static_cast<int>( state.entries.size() ) );
    for ( const auto& entry : state.entries ) {
        copy.push_back( entry );
    }
    return copy;
}

void ActivityJournal::clear()
{
    auto& state = journal();
    QMutexLocker locker( &state.mutex );
    state.entries.clear();
}

void ActivityJournal::setCapacity( int entries )
{
    auto& state = journal();
    QMutexLocker locker( &state.mutex );
    state.capacity = qMax( 1, entries );
    while ( static_cast<int>( state.entries.size() ) > state.capacity ) {
        state.entries.pop_front();
    }
}

int ActivityJournal::capacity()
{
    auto& state = journal();
    QMutexLocker locker( &state.mutex );
    return state.capacity;
}

// ------------------------------------------------------------------ memory

bool MemoryStatus::systemLow() const
{
    if ( systemCommitLimitBytes > 0 ) {
        return systemCommitAvailableBytes < systemCommitLimitBytes / 20
               || systemCommitAvailableBytes < GiB / 2;
    }
    if ( systemTotalPhysicalBytes > 0 ) {
        return systemAvailablePhysicalBytes < systemTotalPhysicalBytes / 33;
    }
    return false;
}

QString MemoryStatus::describe() const
{
    auto text = QStringLiteral( "CILogg %1 private (peak %2), %3 in RAM" )
                    .arg( gib( processPrivateBytes ), gib( processPeakPrivateBytes ),
                          gib( processWorkingSetBytes ) );
    if ( systemTotalPhysicalBytes > 0 ) {
        text += QStringLiteral( "; system RAM %1 free of %2" )
                    .arg( gib( systemAvailablePhysicalBytes ), gib( systemTotalPhysicalBytes ) );
    }
    if ( systemCommitLimitBytes > 0 ) {
        const auto used = systemCommitLimitBytes - qMin( systemCommitLimitBytes, systemCommitAvailableBytes );
        text += QStringLiteral( "; commit %1 free of %2 (%3% used)" )
                    .arg( gib( systemCommitAvailableBytes ), gib( systemCommitLimitBytes ) )
                    .arg( static_cast<int>( 100.0 * static_cast<double>( used )
                                            / static_cast<double>( systemCommitLimitBytes ) ) );
    }
    return text;
}

QVariantMap MemoryStatus::toVariantMap() const
{
    return { { QStringLiteral( "time" ), isoUtc( time ) },
             { QStringLiteral( "processPrivateBytes" ), processPrivateBytes },
             { QStringLiteral( "processWorkingSetBytes" ), processWorkingSetBytes },
             { QStringLiteral( "processPeakPrivateBytes" ), processPeakPrivateBytes },
             { QStringLiteral( "systemTotalPhysicalBytes" ), systemTotalPhysicalBytes },
             { QStringLiteral( "systemAvailablePhysicalBytes" ), systemAvailablePhysicalBytes },
             { QStringLiteral( "systemCommitLimitBytes" ), systemCommitLimitBytes },
             { QStringLiteral( "systemCommitAvailableBytes" ), systemCommitAvailableBytes },
             { QStringLiteral( "systemLow" ), systemLow() },
             { QStringLiteral( "text" ), describe() } };
}

MemoryStatus currentMemoryStatus()
{
    MemoryStatus status;
    status.time = QDateTime::currentDateTimeUtc();
#if defined( Q_OS_WIN )
    PROCESS_MEMORY_COUNTERS_EX process{};
    if ( GetProcessMemoryInfo( GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>( &process ),
                               sizeof( process ) ) ) {
        status.processPrivateBytes = process.PrivateUsage;
        status.processWorkingSetBytes = process.WorkingSetSize;
        status.processPeakPrivateBytes = process.PeakPagefileUsage;
    }
    MEMORYSTATUSEX system{};
    system.dwLength = sizeof( system );
    if ( GlobalMemoryStatusEx( &system ) ) {
        status.systemTotalPhysicalBytes = system.ullTotalPhys;
        status.systemAvailablePhysicalBytes = system.ullAvailPhys;
        status.systemCommitLimitBytes = system.ullTotalPageFile;
        status.systemCommitAvailableBytes = system.ullAvailPageFile;
    }
#elif defined( Q_OS_MACOS )
    task_vm_info_data_t vmInfo{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if ( task_info( mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>( &vmInfo ), &count )
         == KERN_SUCCESS ) {
        status.processPrivateBytes = vmInfo.phys_footprint;
        status.processWorkingSetBytes = vmInfo.resident_size;
        status.processPeakPrivateBytes = vmInfo.resident_size_peak;
    }
    quint64 total = 0;
    size_t length = sizeof( total );
    if ( sysctlbyname( "hw.memsize", &total, &length, nullptr, 0 ) == 0 ) {
        status.systemTotalPhysicalBytes = total;
    }
    vm_statistics64_data_t vm{};
    mach_msg_type_number_t vmCount = HOST_VM_INFO64_COUNT;
    if ( host_statistics64( mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>( &vm ),
                            &vmCount )
         == KERN_SUCCESS ) {
        const auto page = static_cast<quint64>( sysconf( _SC_PAGESIZE ) );
        status.systemAvailablePhysicalBytes
            = ( static_cast<quint64>( vm.free_count ) + vm.inactive_count ) * page;
    }
#else
    const auto self = readKibFields( QStringLiteral( "/proc/self/status" ) );
    status.processWorkingSetBytes = self.value( QStringLiteral( "VmRSS" ) );
    status.processPrivateBytes = self.value( QStringLiteral( "RssAnon" ) );
    status.processPeakPrivateBytes = self.value( QStringLiteral( "VmHWM" ) );
    const auto system = readKibFields( QStringLiteral( "/proc/meminfo" ) );
    status.systemTotalPhysicalBytes = system.value( QStringLiteral( "MemTotal" ) );
    status.systemAvailablePhysicalBytes = system.value( QStringLiteral( "MemAvailable" ) );
    status.systemCommitLimitBytes = system.value( QStringLiteral( "CommitLimit" ) );
    const auto committed = system.value( QStringLiteral( "Committed_AS" ) );
    status.systemCommitAvailableBytes
        = status.systemCommitLimitBytes > committed ? status.systemCommitLimitBytes - committed : 0;
#endif
    return status;
}

void MemoryMonitor::start( int intervalMs )
{
    const auto sample = []() {
        const auto status = currentMemoryStatus();
        pushSample( status );
        auto& monitorState = monitor();
        QString note;
        {
            QMutexLocker locker( &monitorState.mutex );
            if ( status.systemLow() && !monitorState.lowReported ) {
                monitorState.lowReported = true;
                note = QStringLiteral( "System memory is low: %1" ).arg( status.describe() );
            }
            else if ( !status.systemLow() ) {
                monitorState.lowReported = false;
            }
            if ( note.isEmpty() && status.processPrivateBytes > monitorState.journaledPrivate + GiB ) {
                note = QStringLiteral( "CILogg memory grew: %1" ).arg( status.describe() );
            }
            if ( !note.isEmpty() ) {
                monitorState.journaledPrivate = status.processPrivateBytes;
            }
        }
        if ( !note.isEmpty() ) {
            ActivityJournal::record( QStringLiteral( "memory" ), note );
        }
    };

    auto& state = monitor();
    {
        QMutexLocker locker( &state.mutex );
        if ( state.timer ) {
            return;
        }
        state.timer = new QTimer( QCoreApplication::instance() );
        QObject::connect( state.timer.data(), &QTimer::timeout, sample );
        state.timer->start( intervalMs );
    }
    sample();
}

void MemoryMonitor::stop()
{
    auto& state = monitor();
    QMutexLocker locker( &state.mutex );
    delete state.timer.data();
    state.timer = nullptr;
}

QVector<MemoryStatus> MemoryMonitor::samples()
{
    auto& state = monitor();
    QMutexLocker locker( &state.mutex );
    QVector<MemoryStatus> copy;
    copy.reserve( static_cast<int>( state.samples.size() ) );
    for ( const auto& sample : state.samples ) {
        copy.push_back( sample );
    }
    return copy;
}

void MemoryMonitor::addSampleForTest( const MemoryStatus& status )
{
    pushSample( status );
}

void MemoryMonitor::clearForTest()
{
    auto& state = monitor();
    QMutexLocker locker( &state.mutex );
    state.samples.clear();
    state.lowReported = false;
    state.journaledPrivate = 0;
}

// ------------------------------------------------------------------ reports

void DiagnosticReports::setRoot( const QString& directory )
{
    auto& state = reports();
    QMutexLocker locker( &state.mutex );
    state.root = directory;
}

QString DiagnosticReports::root()
{
    auto& state = reports();
    QMutexLocker locker( &state.mutex );
    return state.root;
}

void DiagnosticReports::setContextProvider( ContextProvider provider )
{
    auto& state = reports();
    QMutexLocker locker( &state.mutex );
    state.provider = std::move( provider );
}

void DiagnosticReports::setKeep( int count )
{
    auto& state = reports();
    QMutexLocker locker( &state.mutex );
    state.keep = qMax( 1, count );
}

ReportResult DiagnosticReports::create( const ReportRequest& request )
{
    ReportResult result;
    QString reportRoot;
    ContextProvider provider;
    int keepCount = DefaultKeep;
    {
        auto& state = reports();
        QMutexLocker locker( &state.mutex );
        reportRoot = state.root;
        provider = state.provider;
        keepCount = state.keep;
    }
    if ( reportRoot.isEmpty() ) {
        result.error = QStringLiteral( "No report folder is configured." );
        return result;
    }

    auto kind = request.kind.toLower();
    kind.remove( QRegularExpression( QStringLiteral( "[^a-z]" ) ) );
    if ( kind.isEmpty() ) {
        kind = QStringLiteral( "manual" );
    }
    static std::atomic_int counter{ 0 };
    const auto now = QDateTime::currentDateTime();
    result.id = QStringLiteral( "%1-%2-%3-%4" )
                    .arg( now.toUTC().toString( QStringLiteral( "yyyyMMdd-HHmmss-zzz" ) ),
                          QString::number( ++counter % 10000 ).rightJustified( 4, QLatin1Char( '0' ) ),
                          QString::number( QCoreApplication::applicationPid() ), kind );
    result.directory = QDir( reportRoot ).filePath( result.id );
    if ( !QDir().mkpath( result.directory ) ) {
        result.error = QStringLiteral( "Cannot create %1." ).arg( result.directory );
        return result;
    }

    const auto memory = currentMemoryStatus();
    pushSample( memory );
    const auto samples = MemoryMonitor::samples();
    ActivityJournal::record( QStringLiteral( "report" ),
                             QStringLiteral( "Report %1 written: %2" )
                                 .arg( result.id, firstLine( request.error.isEmpty()
                                                                 ? request.operation
                                                                 : request.error,
                                                             160 ) ) );
    const auto entries = ActivityJournal::entries();

    QVariantMap context;
    if ( provider ) {
        try {
            context = provider();
        } catch ( const std::exception& error ) {
            context.insert( QStringLiteral( "error" ), QString::fromLocal8Bit( error.what() ) );
        }
    }

    QVariantMap fileInfo;
    if ( !request.filePath.isEmpty() ) {
        const QFileInfo info( request.filePath );
        fileInfo = { { QStringLiteral( "name" ), info.fileName() },
                     { QStringLiteral( "path" ), info.absoluteFilePath() },
                     { QStringLiteral( "exists" ), info.exists() },
                     { QStringLiteral( "size" ), info.exists() ? info.size() : 0 },
                     { QStringLiteral( "modified" ),
                       info.exists() ? info.lastModified().toString( Qt::ISODate ) : QString{} } };
    }

    result.title = kindTitle( kind );
    if ( !request.operation.isEmpty() ) {
        result.title += QStringLiteral( ": " ) + request.operation;
    }
    result.summary = summaryFor( request, memory );

    QVariantList steps;
    QVariantList commands;
    QStringList journalLines;
    for ( const auto& entry : entries ) {
        steps.push_back( entryMap( entry ) );
        journalLines.push_back( entryLine( entry ) );
        if ( !entry.data.isEmpty() ) {
            journalLines.push_back( QStringLiteral( "    " )
                                    + QString::fromUtf8( QJsonDocument::fromVariant( entry.data )
                                                             .toJson( QJsonDocument::Compact ) ) );
        }
        if ( entry.category == QLatin1String( "command" ) || entry.category == QLatin1String( "action" )
             || entry.category == QLatin1String( "response" ) ) {
            auto command = entryMap( entry );
            commands.push_back( command );
        }
    }

    QVariantList trend;
    for ( const auto& sample : samples ) {
        trend.push_back( sample.toVariantMap() );
    }

    const QVariantMap report{
        { QStringLiteral( "id" ), result.id },
        { QStringLiteral( "kind" ), kind },
        { QStringLiteral( "created" ), now.toString( Qt::ISODateWithMs ) },
        { QStringLiteral( "createdUtc" ), isoUtc( now ) },
        { QStringLiteral( "title" ), result.title },
        { QStringLiteral( "summary" ), result.summary },
        { QStringLiteral( "operation" ), request.operation },
        { QStringLiteral( "error" ), request.error },
        { QStringLiteral( "file" ), fileInfo },
        { QStringLiteral( "details" ), request.details },
        { QStringLiteral( "memory" ), memory.toVariantMap() },
        { QStringLiteral( "memoryTrend" ), trend },
        { QStringLiteral( "environment" ), environmentMap() },
        { QStringLiteral( "context" ), context },
        { QStringLiteral( "steps" ), steps },
    };

    bool written = writeJson( QDir( result.directory ).filePath( QStringLiteral( "report.json" ) ), report );
    written = writeText( QDir( result.directory ).filePath( QStringLiteral( "report.md" ) ),
                         markdownFor( result, request, fileInfo, memory, samples, entries, context )
                             .toUtf8() )
              && written;
    written = writeText( QDir( result.directory ).filePath( QStringLiteral( "journal.log" ) ),
                         journalLines.join( QLatin1Char( '\n' ) ).toUtf8() + '\n' )
              && written;
    written = writeJson( QDir( result.directory ).filePath( QStringLiteral( "repro.json" ) ),
                         QVariantMap{ { QStringLiteral( "version" ),
                                        QString::fromLatin1( kloggVersion().data() ) },
                                      { QStringLiteral( "steps" ), commands } } )
              && written;
    written = writeText( QDir( result.directory ).filePath( QStringLiteral( "cilogg.log" ) ),
                         logging::recentLogLines().join( QLatin1Char( '\n' ) ).toUtf8() + '\n' )
              && written;
    written = writeText( QDir( result.directory ).filePath( QStringLiteral( "memory.csv" ) ),
                         memoryCsv( samples ) )
              && written;
    written = writeJson( QDir( result.directory ).filePath( QStringLiteral( "context.json" ) ), context )
              && written;
    if ( !written ) {
        result.error = QStringLiteral( "Some report files could not be written in %1." )
                           .arg( result.directory );
    }

    const auto archive = result.directory + QStringLiteral( ".zip" );
    {
        KZip zip( archive );
        bool zipped = false;
        if ( zip.open( QIODevice::WriteOnly ) ) {
            zipped = zip.addLocalDirectory( result.directory, result.id );
            zipped = zip.close() && zipped;
        }
        if ( zipped ) {
            result.archive = archive;
        }
    }
    if ( result.archive.isEmpty() ) {
        QFile::remove( archive );
    }

    pruneReports( reportRoot, keepCount );
    result.ok = written;
    LOG_WARNING << "Diagnostic report " << result.id << " written to " << result.directory;
    return result;
}

QVariantList DiagnosticReports::list()
{
    QVariantList items;
    const auto rootDirectory = root();
    if ( rootDirectory.isEmpty() ) {
        return items;
    }
    QDir dir( rootDirectory );
    auto names = reportNames( dir );
    std::reverse( names.begin(), names.end() );
    for ( const auto& name : names ) {
        QFile file( dir.filePath( name + QStringLiteral( "/report.json" ) ) );
        QVariantMap report;
        if ( file.open( QIODevice::ReadOnly ) ) {
            report = QJsonDocument::fromJson( file.readAll() ).toVariant().toMap();
        }
        const auto archive = dir.filePath( name + QStringLiteral( ".zip" ) );
        items.push_back( QVariantMap{
            { QStringLiteral( "id" ), name },
            { QStringLiteral( "kind" ), report.value( QStringLiteral( "kind" ) ) },
            { QStringLiteral( "created" ), report.value( QStringLiteral( "created" ) ) },
            { QStringLiteral( "title" ), report.value( QStringLiteral( "title" ) ) },
            { QStringLiteral( "summary" ), report.value( QStringLiteral( "summary" ) ) },
            { QStringLiteral( "directory" ), QDir::toNativeSeparators( dir.filePath( name ) ) },
            { QStringLiteral( "archive" ),
              QFileInfo::exists( archive ) ? QDir::toNativeSeparators( archive ) : QString{} } } );
    }
    return items;
}

QVariantMap DiagnosticReports::find( const QString& id )
{
    for ( const auto& item : list() ) {
        const auto map = item.toMap();
        if ( id.isEmpty() || map.value( QStringLiteral( "id" ) ).toString() == id ) {
            return map;
        }
    }
    return {};
}

QString DiagnosticReports::issueTitle( const ReportResult& result, const ReportRequest& request )
{
    Q_UNUSED( result );
    auto title = kindTitle( request.kind.isEmpty() ? QStringLiteral( "manual" ) : request.kind );
    if ( !request.operation.isEmpty() ) {
        title += QStringLiteral( ": " ) + withoutFolders( request.operation, request.filePath );
    }
    if ( !request.error.isEmpty() ) {
        title += QStringLiteral( " - " )
                 + firstLine( withoutFolders( request.error, request.filePath ), 80 );
    }
    return title;
}

QString DiagnosticReports::issueBody( const ReportResult& result, const ReportRequest& request )
{
    const auto memory = currentMemoryStatus();
    QString body;
    body += QStringLiteral( "#### What did you do?\n\n- \n\n" );
    body += QStringLiteral( "#### What happened?\n\n" );
    if ( !request.operation.isEmpty() ) {
        body += QStringLiteral( "- CILogg was: %1\n" )
                    .arg( withoutFolders( request.operation, request.filePath ) );
    }
    if ( !request.error.isEmpty() ) {
        body += QStringLiteral( "- Error: `%1`\n" )
                    .arg( firstLine( withoutFolders( request.error, request.filePath ), 300 ) );
    }
    const auto fileName = QFileInfo( request.filePath ).fileName();
    if ( !fileName.isEmpty() ) {
        body += QStringLiteral( "- File: `%1`\n" ).arg( fileName );
    }
    body += QStringLiteral( "- Memory: %1\n" ).arg( memory.describe() );
    if ( !result.id.isEmpty() ) {
        body += QStringLiteral( "- Local report: `%1`\n\n" ).arg( result.id );
        body += QStringLiteral(
            "Please review the report zip on your computer first (it holds the activity journal, "
            "recent log lines, file paths and tab names), then attach it here if you are happy to "
            "share it.\n\n" );
    }
    else {
        body += QLatin1Char( '\n' );
    }
    body += QStringLiteral( "#### Diagnostics\n\n" ) + environmentSummary();
    return body;
}

// ------------------------------------------------------------------ throttle

ReportThrottle::ReportThrottle( qint64 intervalMs )
    : intervalMs_( intervalMs )
{
}

bool ReportThrottle::shouldReport( const QString& key, const QDateTime& now, int* suppressedBefore )
{
    if ( states_.size() > 256 ) { // keys hold error texts: forget problems that went quiet
        for ( auto it = states_.begin(); it != states_.end(); ) {
            it = it.value().last.msecsTo( now ) >= intervalMs_ ? states_.erase( it ) : std::next( it );
        }
    }
    auto& state = states_[ key ];
    if ( !state.last.isValid() || state.last.msecsTo( now ) >= intervalMs_ ) {
        if ( suppressedBefore != nullptr ) {
            *suppressedBefore = state.suppressed;
        }
        state.last = now;
        state.suppressed = 0;
        return true;
    }
    ++state.suppressed;
    return false;
}

int ReportThrottle::suppressed( const QString& key ) const
{
    return states_.value( key ).suppressed;
}

QString environmentSummary()
{
    const auto environment = environmentMap();
    return QStringLiteral( "- CILogg version: `%1` (built on `%2` from commit `%3`)\n"
                           "- Build ABI: `%4`\n"
                           "- OS: `%5`\n"
                           "- Kernel: `%6`\n"
                           "- CPU architecture: `%7`\n"
                           "- Concurrency: `%8`\n"
                           "- Qt: `%9`\n" )
        .arg( environment.value( QStringLiteral( "version" ) ).toString(),
              environment.value( QStringLiteral( "buildDate" ) ).toString(),
              environment.value( QStringLiteral( "commit" ) ).toString(),
              environment.value( QStringLiteral( "abi" ) ).toString(),
              environment.value( QStringLiteral( "os" ) ).toString(),
              environment.value( QStringLiteral( "kernel" ) ).toString(),
              environment.value( QStringLiteral( "cpu" ) ).toString(),
              environment.value( QStringLiteral( "concurrency" ) ).toString(),
              environment.value( QStringLiteral( "qt" ) ).toString() );
}

} // namespace cilogg::diagnostics
