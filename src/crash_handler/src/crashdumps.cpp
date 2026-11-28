// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// R06b: crash records and minidumps, and throw-site stacks. See crashdumps.h and
// docs/diagnostics.md. The platform crash paths are in crashdumps_windows.cpp and
// crashdumps_posix.cpp.

#include "crashdumps.h"

#include "crashdumps_platform.h"
#include "klogg_version.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>

#include <atomic>
#include <cstdlib>
#include <exception>

namespace cilogg::diagnostics {

namespace detail {
std::atomic_bool fullMemoryFlag{ false };
std::atomic_bool crashing{ false };
std::atomic_bool folderReady{ false };
char header[ 512 ] = {};
} // namespace detail

namespace {

using namespace detail;

constexpr int MaxRecordBytes = 256 * 1024;
constexpr qint64 MinRecordAgeMs = 10 * 1000;              // younger: maybe still being written
constexpr qint64 MaxDumpWriteMs = 60 * 60 * 1000;         // a dump still pending after that is not
constexpr qint64 MaxDumpBytes = 2LL * 1024 * 1024 * 1024; // all dumps but the newest

std::atomic_bool installedFlag{ false };
char terminateReason[ 512 ] = {};
constexpr int DefaultProcessDumpWaitMs = 30 * 1000;
std::atomic_int processDumpWaitMs{ DefaultProcessDumpWaitMs };

QString& dumpHelperPath()
{
    static QString path;
    return path;
}

QMutex& stateMutex()
{
    static QMutex mutex;
    return mutex;
}

QString& folderPath()
{
    static QString path;
    return path;
}

QString recordField( const QString& text, const QString& name )
{
    const auto prefix = name + QStringLiteral( ": " );
    for ( const auto& line : text.split( QLatin1Char( '\n' ) ) ) {
        if ( line.startsWith( prefix ) ) {
            return line.mid( prefix.size() ).trimmed();
        }
    }
    return {};
}

[[noreturn]] void onTerminate()
{
    // A failure inside this handler (say, an allocation while rethrowing) calls std::terminate
    // again: then end at once.
    static std::atomic_bool entered{ false };
    if ( entered.exchange( true ) ) {
        std::abort();
    }
    // The uncaught exception's text, when there is one; this thread's latest throw is its site.
    // Keep that site first: on Windows the rethrow below records a throw of its own.
    freezeThrowSite();
    terminateReason[ 0 ] = 0;
    append( terminateReason, sizeof( terminateReason ), "std::terminate" );
    if ( const auto current = std::current_exception() ) {
        try {
            std::rethrow_exception( current );
        } catch ( const std::exception& error ) {
            append( terminateReason, sizeof( terminateReason ), ": uncaught exception: " );
            append( terminateReason, sizeof( terminateReason ), error.what() );
        } catch ( ... ) {
            append( terminateReason, sizeof( terminateReason ), ": uncaught exception" );
        }
    }
    writeCrashNow( terminateReason );
    std::abort(); // the abort handler sees the record is written and writes no second one
}

} // namespace

bool CrashDumps::install( const QString& folder )
{
    if ( folder.isEmpty() || !setFolder( folder ) ) {
        return false;
    }
    // Set again on every call: a library initialized later (Sentry, when built in) may have put its
    // own handlers in place, and CILogg's local crash record must win.
    installPlatformHandlers();
    std::set_terminate( onTerminate );
    installedFlag = true;
    return true;
}

bool CrashDumps::setFolder( const QString& folder )
{
    if ( folder.isEmpty() ) {
        QMutexLocker locker( &stateMutex() );
        folderReady = false; // a crash now writes nothing
        folderPath().clear();
        return true;
    }
    if ( !QDir().mkpath( folder ) ) {
        return false;
    }
    {
        QMutexLocker locker( &stateMutex() );
        const auto absolute = QDir( folder ).absolutePath();
        if ( folderReady.load() && absolute == folderPath() ) {
            return true; // unchanged: never rewrite the buffers a crash may be reading
        }
        folderPath() = absolute;
        setNativeFolder( folderPath() );
        const auto text = QStringLiteral( "version: %1\ncommit: %2\n" )
                              .arg( QString( kloggVersion() ), QString( kloggCommit() ) )
                              .toUtf8();
        header[ 0 ] = 0;
        append( header, sizeof( header ), text.constData() );
        folderReady = true;
    }
    return true;
}

bool CrashDumps::installed()
{
    return installedFlag.load();
}

QString CrashDumps::folder()
{
    QMutexLocker locker( &stateMutex() );
    return folderPath();
}

void CrashDumps::setFullMemory( bool fullMemory )
{
    fullMemoryFlag = fullMemory;
}

bool CrashDumps::fullMemory()
{
    return fullMemoryFlag.load();
}

bool CrashDumps::minidumpsSupported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool CrashDumps::writeProcessDump( const QString& path, bool fullMemory, QString* error )
{
    const auto fail = [ error ]( const QString& text ) {
        if ( error != nullptr ) {
            *error = text;
        }
        return false;
    };
    if ( !minidumpsSupported() ) {
        return fail( QStringLiteral( "CILogg writes minidumps on Windows only." ) );
    }
    const auto helper = dumpHelper();
    if ( helper.isEmpty() ) {
        return fail( QStringLiteral( "No minidump helper is set." ) );
    }
    // Another process writes the dump: MiniDumpWriteDump in this process would suspend every other
    // thread, this one included, and could wait forever for a lock one of them holds. The helper
    // can be ended when it takes too long.
    QProcess process;
    process.setProgram( helper );
    process.setArguments( { QString::fromLatin1( DumpHelperFlag ),
                            QString::number( QCoreApplication::applicationPid() ),
                            QDir::toNativeSeparators( path ),
                            fullMemory ? QStringLiteral( "full" ) : QStringLiteral( "small" ) } );
    process.start();
    if ( !process.waitForStarted( 10000 ) ) {
        return fail(
            QStringLiteral( "Cannot start the minidump helper: %1" ).arg( process.errorString() ) );
    }
    const auto waitMs = processDumpWaitMs.load();
    if ( !process.waitForFinished( waitMs ) ) {
        process.kill();
        process.waitForFinished( 5000 );
        QFile::remove( path ); // a dump cut short is no use, and no report names it
        return fail( QStringLiteral( "The minidump was not written within %1 ms." ).arg( waitMs ) );
    }
    if ( process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0
         || !QFileInfo::exists( path ) ) {
        QFile::remove( path );
        return fail( QStringLiteral( "The minidump helper failed (exit code %1)." )
                         .arg( process.exitCode() ) );
    }
    return true;
}

void CrashDumps::setDumpHelper( const QString& program )
{
    QMutexLocker locker( &stateMutex() );
    dumpHelperPath() = program;
}

QString CrashDumps::dumpHelper()
{
    QMutexLocker locker( &stateMutex() );
    return dumpHelperPath();
}

int CrashDumps::runDumpHelper( int argc, char** argv )
{
    return detail::runDumpHelper( argc, argv );
}

void CrashDumps::setProcessDumpTimeoutForTest( int milliseconds )
{
    processDumpWaitMs = milliseconds < 0 ? DefaultProcessDumpWaitMs : milliseconds;
}

QVariantList CrashDumps::pendingCrashes()
{
    QVariantList crashes;
    const auto path = folder();
    // QDir( "" ) is the working directory: never read crash records from there.
    if ( path.isEmpty() ) {
        return crashes;
    }
    const QDir dir( path );
    if ( !dir.exists() ) {
        return crashes;
    }
    const auto records
        = dir.entryList( { QStringLiteral( "crash-*.txt" ) }, QDir::Files, QDir::Name );
    for ( const auto& name : records ) {
        const auto record = dir.filePath( name );
        const auto age = QFileInfo( record ).lastModified().msecsTo( QDateTime::currentDateTime() );
        if ( QFileInfo::exists( record + QStringLiteral( ".reported" ) ) || age < MinRecordAgeMs ) {
            continue;
        }
        QFile file( record );
        if ( !file.open( QIODevice::ReadOnly ) ) {
            continue;
        }
        const auto text = QString::fromUtf8( file.read( MaxRecordBytes ) );
        // A dump still being written (a full-memory dump can take minutes): the record names a
        // dump but no result yet, and the crashing process still runs. A later start reports it
        // (after at most an hour, the process id may belong to another program by then).
        const bool dumpPending = text.contains( QLatin1String( "\ndump: " ) )
                                 && !text.contains( QLatin1String( "\ndump: none" ) )
                                 && !text.contains( QLatin1String( "dump result:" ) );
        if ( dumpPending && age < MaxDumpWriteMs
             && processAlive( recordField( text, QStringLiteral( "pid" ) ).toLongLong() ) ) {
            continue;
        }
        // A dump counts only when the record says it was written; one cut short is removed.
        auto dump = dir.filePath( QFileInfo( name ).completeBaseName() + QStringLiteral( ".dmp" ) );
        if ( !text.contains( QLatin1String( "dump result: written" ) ) ) {
            QFile::remove( dump );
            dump.clear();
        }
        else if ( !QFileInfo::exists( dump ) ) {
            dump.clear(); // pruned to keep the dumps within their size cap
        }
        crashes.push_back( QVariantMap{
            { QStringLiteral( "record" ), record },
            { QStringLiteral( "dump" ), dump },
            { QStringLiteral( "time" ), recordField( text, QStringLiteral( "time" ) ) },
            { QStringLiteral( "summary" ), recordField( text, QStringLiteral( "reason" ) ) },
            { QStringLiteral( "text" ), text } } );
    }
    return crashes;
}

void CrashDumps::markReported( const QString& record )
{
    QFile marker( record + QStringLiteral( ".reported" ) );
    if ( marker.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        marker.write( "reported\n" );
    }
}

void CrashDumps::prune( int keep )
{
    const auto path = folder();
    // QDir( "" ) is the working directory: never delete anything there.
    if ( path.isEmpty() ) {
        return;
    }
    const QDir dir( path );
    if ( !dir.exists() ) {
        return;
    }
    auto records = dir.entryList( { QStringLiteral( "crash-*.txt" ) }, QDir::Files, QDir::Name );
    while ( records.size() > qMax( 1, keep ) ) {
        const auto base = QFileInfo( records.takeFirst() ).completeBaseName();
        for ( const auto& suffix : { QStringLiteral( ".txt" ), QStringLiteral( ".txt.reported" ),
                                     QStringLiteral( ".dmp" ) } ) {
            QFile::remove( dir.filePath( base + suffix ) );
        }
    }
    // Full-memory dumps can be as large as the memory: past the cap the older dumps go (their
    // records stay); the newest dump is always kept.
    auto dumps = dir.entryInfoList( { QStringLiteral( "crash-*.dmp" ) }, QDir::Files, QDir::Name );
    qint64 total = 0;
    for ( auto index = dumps.size() - 1; index >= 0; --index ) {
        total += dumps.at( index ).size();
        if ( index < dumps.size() - 1 && total > MaxDumpBytes ) {
            QFile::remove( dumps.at( index ).absoluteFilePath() );
        }
    }
}

QString CrashDumps::writeCrashForTest( const QString& reason )
{
    const auto text = reason.toUtf8();
    resetCrashStateForTest();
    writeCrashNow( text.constData() );
    resetCrashStateForTest();
    return lastRecordPath();
}

void installThrowSiteCapture()
{
    detail::enableThrowSiteCapture();
}

QStringList currentThrowStack()
{
    return formatThrowFrames( detail::throwSite() );
}

} // namespace cilogg::diagnostics
