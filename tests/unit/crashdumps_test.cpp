// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Task R06, slice R06b: crash records and minidumps, throw-site stacks, crash reports at the next
// start, and dumps kept out of every report zip (decision D16).

#include <catch2/catch.hpp>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <stdexcept>
#include <thread>

#include <karchive.h>
#include <kzip.h>

#include "crashdumps.h"
#include "diagnostics.h"

using namespace cilogg::diagnostics;

namespace {
#if defined( _MSC_VER )
#define R06B_NOINLINE __declspec( noinline )
#else
#define R06B_NOINLINE __attribute__( ( noinline ) )
#endif

R06B_NOINLINE void throwForThrowSiteTest()
{
    throw std::runtime_error( "throw-site test" );
}

QString readText( const QString& path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return QString::fromUtf8( file.readAll() );
}

bool isMinidump( const QString& path )
{
    QFile file( path );
    return file.open( QIODevice::ReadOnly ) && file.read( 4 ) == "MDMP";
}

// The file names in a report zip (they sit in one folder named after the report id).
QStringList zipEntries( const QString& archive, const QString& id )
{
    KZip zip( archive );
    REQUIRE( zip.open( QIODevice::ReadOnly ) );
    const auto* folder = dynamic_cast<const KArchiveDirectory*>( zip.directory()->entry( id ) );
    REQUIRE( folder != nullptr );
    return folder->entries();
}

bool anyDump( const QStringList& names )
{
    return std::any_of( names.begin(), names.end(), []( const QString& name ) {
        return name.endsWith( QLatin1String( ".dmp" ), Qt::CaseInsensitive );
    } );
}

struct ReportRootGuard {
    ~ReportRootGuard()
    {
        DiagnosticReports::setRoot( {} );
        DiagnosticReports::setContextProvider( {} );
        CrashDumps::setFolder( {} ); // never left pointing at a deleted folder
    }
};

// A record younger than 10 s may still be being written: make it older.
void age( const QString& path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadWrite ) );
    REQUIRE( file.setFileTime( QDateTime::currentDateTime().addSecs( -60 ),
                               QFileDevice::FileModificationTime ) );
}

void writeFile( const QString& path, const QByteArray& content )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( content );
}
} // namespace

TEST_CASE( "The stack where an exception was thrown is kept for the thread that catches it",
           "[diagnostics]" )
{
    installThrowSiteCapture();
    QStringList stack;
    try {
        throwForThrowSiteTest();
    } catch ( const std::exception& ) {
        stack = currentThrowStack();
    }
    INFO( stack.join( QLatin1Char( '\n' ) ).toStdString() );
    REQUIRE_FALSE( stack.isEmpty() );
    // The frames name the binary the throw came from (as module+offset, or its path).
    CHECK( std::any_of( stack.begin(), stack.end(), []( const QString& frame ) {
        return frame.contains( QLatin1String( "cilogg_tests" ), Qt::CaseInsensitive );
    } ) );

    // Another thread has its own record: it threw nothing.
    QStringList other{ QStringLiteral( "not read" ) };
    std::thread( [ &other ] { other = currentThrowStack(); } ).join();
    CHECK( other.isEmpty() );
}

TEST_CASE( "A minidump of the running process is written where minidumps are supported",
           "[diagnostics]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto path = dir.filePath( "process.dmp" );
    QString error;
    const auto written = CrashDumps::writeProcessDump( path, false, &error );
    if ( CrashDumps::minidumpsSupported() ) {
        INFO( error.toStdString() );
        REQUIRE( written );
        CHECK( isMinidump( path ) );
    }
    else {
        CHECK_FALSE( written );
        CHECK_FALSE( error.isEmpty() );
    }
}

TEST_CASE( "A crash of an earlier run becomes a report; its dump stays out of the zip",
           "[diagnostics]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    ReportRootGuard guard;
    DiagnosticReports::setRoot( root.filePath( "reports" ) );
    REQUIRE( CrashDumps::setFolder( root.filePath( "crashes" ) ) );

    // A reason can carry a path (an exception's text): the public issue must not show it.
    const auto record = CrashDumps::writeCrashForTest(
        QStringLiteral( "test crash (R06b) reading C:\\Users\\someone\\secret\\x.log" ) );
    REQUIRE( QFileInfo::exists( record ) );
    const auto text = readText( record );
    CHECK( text.contains( "CILogg crash" ) );
    CHECK( text.contains( "reason: test crash (R06b)" ) );
    CHECK( text.contains( "version: " ) );
    CHECK( text.contains( "stack:" ) );
    const auto dump = QFileInfo( record ).absolutePath() + QLatin1Char( '/' )
                      + QFileInfo( record ).completeBaseName() + QStringLiteral( ".dmp" );
    if ( CrashDumps::minidumpsSupported() ) {
        REQUIRE( QFileInfo::exists( dump ) );
        CHECK( isMinidump( dump ) );
        CHECK( text.contains( "dump: small" ) );
        CHECK( text.contains( "dump result: written" ) );
    }

    // A record younger than 10 s may still be being written, so it waits for a later start.
    CHECK( CrashDumps::pendingCrashes().isEmpty() );
    age( record );
    const auto pending = CrashDumps::pendingCrashes();
    REQUIRE( pending.size() == 1 );
    CHECK(
        pending.front().toMap().value( "summary" ).toString().startsWith( "test crash (R06b)" ) );

    const auto reports = DiagnosticReports::reportPendingCrashes();
    REQUIRE( reports.size() == 1 );
    const auto& crash = reports.front();
    REQUIRE( crash.result.ok );
    CHECK( crash.request.kind == "crash" );
    CHECK( crash.result.title.startsWith( "Crash report" ) );
    CHECK( crash.result.summary.contains( "test crash (R06b)" ) );

    // The record is in the report and in its zip; a dump is only named, never copied or zipped.
    const auto recordName = QFileInfo( record ).fileName();
    CHECK( QFileInfo::exists( QDir( crash.result.directory ).filePath( recordName ) ) );
    const auto entries = zipEntries( crash.result.archive, crash.result.id );
    CHECK( entries.contains( recordName ) );
    CHECK_FALSE( anyDump( entries ) );
    CHECK_FALSE( anyDump( QDir( crash.result.directory ).entryList( QDir::Files ) ) );
    const auto markdown = readText( QDir( crash.result.directory ).filePath( "report.md" ) );
    if ( CrashDumps::minidumpsSupported() ) {
        CHECK( crash.dump == dump );
        // The zipped report names the dump file, never its folder (the user name).
        CHECK( markdown.contains( QFileInfo( dump ).fileName() ) );
        CHECK( markdown.contains( "only privately" ) );
        const auto report
            = QJsonDocument::fromJson(
                  readText( QDir( crash.result.directory ).filePath( "report.json" ) ).toUtf8() )
                  .object();
        CHECK( report.value( "privateFiles" ).toArray()
               == QJsonArray{ QFileInfo( dump ).fileName() } );
    }
    CHECK_FALSE( markdown.contains( QDir::toNativeSeparators( root.path() ) ) );

    // One report per crash: the record is marked.
    CHECK( CrashDumps::pendingCrashes().isEmpty() );
    CHECK( DiagnosticReports::reportPendingCrashes().isEmpty() );

    // The public issue names the crash, never a folder.
    const auto body = DiagnosticReports::issueBody( crash.result, crash.request );
    CHECK( body.contains( "test crash (R06b)" ) );
    CHECK( body.contains( "x.log" ) );
    CHECK_FALSE( body.contains( "someone" ) );
    CHECK_FALSE( body.contains( root.path() ) );
    CHECK_FALSE( body.contains( QDir::toNativeSeparators( root.path() ) ) );
    CHECK_FALSE( body.contains( ".dmp" ) );
}

TEST_CASE( "An exception report keeps its throw site, and its minidump stays out of the zip",
           "[diagnostics]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    ReportRootGuard guard;
    DiagnosticReports::setRoot( root.path() );

    ReportRequest request( "exception", "Searching a file", "boom", {} );
    request.throwStack = QStringList{ "cilogg.exe+0x10 frameA", "cilogg.exe+0x20 frameB" };
    request.processDump = true;
    const auto result = DiagnosticReports::create( request );
    REQUIRE( result.ok );

    const auto markdown = readText( QDir( result.directory ).filePath( "report.md" ) );
    CHECK( markdown.contains( "Where the error was thrown" ) );
    CHECK( markdown.contains( "cilogg.exe+0x10 frameA" ) );
    const auto report
        = QJsonDocument::fromJson(
              readText( QDir( result.directory ).filePath( "report.json" ) ).toUtf8() )
              .object();
    CHECK( report.value( "throwStack" ).toArray().size() == 2 );

    const auto entries = zipEntries( result.archive, result.id );
    CHECK( entries.contains( "report.md" ) );
    CHECK_FALSE( anyDump( entries ) );
    if ( CrashDumps::minidumpsSupported() ) {
        REQUIRE( QFileInfo::exists( result.processDump ) );
        // In the report's private/ subfolder, which the zip never takes.
        CHECK( QFileInfo( result.processDump ).absolutePath()
               == QFileInfo( QDir( result.directory ).filePath( "private" ) ).absoluteFilePath() );
        CHECK( isMinidump( result.processDump ) );
        CHECK( report.value( "processDump" ).toString() == "private/process.dmp" );
        CHECK( markdown.contains( "only privately" ) );
    }
    else {
        CHECK( result.processDump.isEmpty() );
    }
}

TEST_CASE( "At most three crashes are reported at a start, and a dump cut short is removed",
           "[diagnostics]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    ReportRootGuard guard;
    DiagnosticReports::setRoot( root.filePath( "reports" ) );
    REQUIRE( CrashDumps::setFolder( root.filePath( "crashes" ) ) );
    const QDir crashes( root.filePath( "crashes" ) );
    for ( int index = 1; index <= 5; ++index ) {
        const auto base
            = crashes.filePath( QStringLiteral( "crash-2026010%1-000000-%1" ).arg( index ) );
        // Crash 5's dump was cut short: its record never says it was written.
        writeFile( base + ".txt", index == 5
                                      ? QByteArray( "reason: cut\ndump: small\n" )
                                      : QByteArray( "reason: old\ndump result: written\n" ) );
        writeFile( base + ".dmp", "MDMP" );
        age( base + ".txt" );
    }
    const auto pending = CrashDumps::pendingCrashes();
    REQUIRE( pending.size() == 5 );
    CHECK( pending.back().toMap().value( "dump" ).toString().isEmpty() );
    CHECK_FALSE( crashes.exists( "crash-20260105-000000-5.dmp" ) );
    CHECK( pending.front().toMap().value( "dump" ).toString().endsWith(
        "crash-20260101-000000-1.dmp" ) );

    const auto reports = DiagnosticReports::reportPendingCrashes();
    REQUIRE( reports.size() == 3 );
    CHECK( reports.back().request.error == "cut" );
    CHECK( CrashDumps::pendingCrashes().isEmpty() ); // the two oldest are marked, not reported
    CHECK( DiagnosticReports::list().size() == 3 );
}

TEST_CASE( "Without a crash folder nothing in the working directory is read or deleted",
           "[diagnostics]" )
{
    // QDir( "" ) is the working directory: an unset folder must not mean "here".
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto previous = QDir::currentPath();
    REQUIRE( QDir::setCurrent( root.path() ) );
    struct Restore {
        QString path;
        ~Restore()
        {
            QDir::setCurrent( path );
        }
    } restore{ previous };
    for ( const auto* name : { "crash-20260101-000000-1.txt", "crash-20260102-000000-2.txt" } ) {
        writeFile( root.filePath( name ), "reason: not CILogg's\ndump result: written\n" );
        age( root.filePath( name ) );
    }
    REQUIRE( CrashDumps::setFolder( {} ) );
    CHECK( CrashDumps::folder().isEmpty() );
    CHECK( CrashDumps::pendingCrashes().isEmpty() );
    CrashDumps::prune( 1 );
    CHECK( QDir( root.path() ).entryList( { "crash-*.txt" }, QDir::Files ).size() == 2 );
}

TEST_CASE( "Only the newest crash records are kept", "[diagnostics]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    ReportRootGuard guard;
    REQUIRE( CrashDumps::setFolder( root.path() ) );
    for ( const auto* stamp : { "20260101-000000-1", "20260102-000000-2", "20260103-000000-3" } ) {
        for ( const auto* suffix : { ".txt", ".dmp" } ) {
            QFile file( root.filePath( QStringLiteral( "crash-%1%2" ).arg( stamp, suffix ) ) );
            REQUIRE( file.open( QIODevice::WriteOnly ) );
            file.write( "reason: old\n" );
        }
    }
    QFile other( root.filePath( "notes.txt" ) );
    REQUIRE( other.open( QIODevice::WriteOnly ) );
    other.close();

    CrashDumps::prune( 2 );
    const auto left = QDir( root.path() ).entryList( QDir::Files, QDir::Name );
    CHECK( left
           == QStringList{ "crash-20260102-000000-2.dmp", "crash-20260102-000000-2.txt",
                           "crash-20260103-000000-3.dmp", "crash-20260103-000000-3.txt",
                           "notes.txt" } );
}

TEST_CASE( "A real crash leaves a crash record (and on Windows a minidump)", "[diagnostics]" )
{
    struct Case {
        const char* mode;
        const char* reason; // part of the record's reason line
    };
#ifdef Q_OS_WIN
    const Case cases[] = { { "segv", "access violation" },
                           { "thread", "access violation" },
                           { "throw", "uncaught exception: fixture boom" },
                           { "noexcept", "std::terminate: uncaught exception: fixture boom" },
                           { "abort", "reason: abort" } };
#else
    const Case cases[] = { { "segv", "SIGSEGV" },
                           { "thread", "SIGSEGV" },
                           { "throw", "uncaught exception: fixture boom" },
                           { "noexcept", "std::terminate: uncaught exception: fixture boom" },
                           { "abort", "SIGABRT" } };
#endif
    for ( const auto& crashCase : cases ) {
        INFO( crashCase.mode );
        QTemporaryDir dir;
        REQUIRE( dir.isValid() );
        const auto folder = dir.filePath( "crashes" );
        QProcess process;
        process.setWorkingDirectory(
            dir.path() ); // a core dump, if the system writes one, lands here
        process.start( QString::fromUtf8( CRASH_FIXTURE_PATH ),
                       { folder, QString::fromLatin1( crashCase.mode ) } );
        REQUIRE( process.waitForFinished( 120000 ) );
        CHECK( ( process.exitStatus() == QProcess::CrashExit || process.exitCode() != 0 ) );

        const auto records
            = QDir( folder ).entryList( { QStringLiteral( "crash-*.txt" ) }, QDir::Files );
        REQUIRE( records.size() == 1 ); // one record per crash, also when abort() follows
        const auto text = readText( QDir( folder ).filePath( records.front() ) );
        INFO( text.toStdString() );
        CHECK( text.contains( QString::fromLatin1( crashCase.reason ) ) );
        CHECK( text.contains( "stack:" ) );
        const auto mode = QLatin1String( crashCase.mode );
        if ( mode == QLatin1String( "throw" ) || mode == QLatin1String( "noexcept" ) ) {
            // The site of the fixture's throw (not of a rethrow made to name the exception).
            const auto site = text.indexOf( "last C++ throw on this thread:" );
            REQUIRE( site >= 0 );
            CHECK( text.mid( site ).contains( "crash_fixture", Qt::CaseInsensitive ) );
        }
        const auto dumps
            = QDir( folder ).entryList( { QStringLiteral( "crash-*.dmp" ) }, QDir::Files );
        if ( CrashDumps::minidumpsSupported() ) {
            REQUIRE( dumps.size() == 1 );
            CHECK( isMinidump( QDir( folder ).filePath( dumps.front() ) ) );
        }
        else {
            CHECK( dumps.isEmpty() );
        }
    }
}

TEST_CASE( "A report whose attachment cannot be copied is incomplete, so a crash is retried",
           "[diagnostics]" )
{
    // reportPendingCrashes() marks a crash reported only when its report is complete; a report
    // without the crash record (a full disk, say) must not count as one.
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    ReportRootGuard guard;
    DiagnosticReports::setRoot( root.filePath( "reports" ) );
    ReportRequest request( "crash", "Crash in an earlier run", "boom", {} );
    request.attachments = { root.filePath( "missing-crash-record.txt" ) };
    const auto result = DiagnosticReports::create( request );
    CHECK_FALSE( result.ok );
    CHECK_FALSE( result.directory.isEmpty() ); // the partial folder stays as evidence
    CHECK_FALSE( result.error.isEmpty() );
}
