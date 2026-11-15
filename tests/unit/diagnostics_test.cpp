// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Task R06: debuggable problem reports (activity journal, log tail, memory status, report
// bundles, issue text, throttling and the Commander commands).

#include <catch2/catch.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <thread>
#include <vector>

#include "cli.h"
#include "commander.h"
#include "diagnostics.h"
#include "issuereporter.h"
#include "log.h"
#include "logger.h"
#include "mcp/mcp.h"

using namespace cilogg::diagnostics;

namespace {
QString readAll( const QString& path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return QString::fromUtf8( file.readAll() );
}

QJsonObject readJson( const QString& path )
{
    return QJsonDocument::fromJson( readAll( path ).toUtf8() ).object();
}

struct JournalCapacityGuard {
    int previous = ActivityJournal::capacity();
    ~JournalCapacityGuard()
    {
        ActivityJournal::setCapacity( previous );
    }
};

// Restores what tests_main.cpp configured, also when an assertion ends the test early.
struct LoggingGuard {
    ~LoggingGuard()
    {
        logging::setRecentLogCapacity( 2000 );
        logging::enableLogging( true, qEnvironmentVariableIsSet( "CILOGG_TEST_DEBUG" )
                                          ? logging::LogLevel::Debug
                                          : logging::LogLevel::Info );
    }
};

MemoryStatus lowMemory()
{
    MemoryStatus status;
    status.time = QDateTime::currentDateTimeUtc();
    status.processPrivateBytes = 830ull * 1024 * 1024;
    status.processWorkingSetBytes = 140ull * 1024 * 1024;
    status.processPeakPrivateBytes = 860ull * 1024 * 1024;
    status.systemTotalPhysicalBytes = 32ull * 1024 * 1024 * 1024;
    status.systemAvailablePhysicalBytes = 2ull * 1024 * 1024 * 1024;
    status.systemCommitLimitBytes = 86ull * 1024 * 1024 * 1024;
    status.systemCommitAvailableBytes = 19ull * 1024 * 1024 * 1024 / 10;
    return status;
}
} // namespace

TEST_CASE( "Activity journal folds repeats and keeps a bounded window", "[diagnostics]" )
{
    JournalCapacityGuard guard;
    ActivityJournal::clear();
    ActivityJournal::setCapacity( 5 );

    ActivityJournal::record( "response", "Response 1 fired" );
    ActivityJournal::record( "action", "Automatic action 7 sent" );
    ActivityJournal::record( "response", "Response 1 fired" );
    ActivityJournal::record( "action", "Automatic action 7 sent" );
    ActivityJournal::record( "com", "COM4: paused" );

    auto entries = ActivityJournal::entries();
    REQUIRE( entries.size() == 3 );
    CHECK( entries[ 0 ].text == "Response 1 fired" );
    CHECK( entries[ 0 ].repeats == 2 );
    CHECK( entries[ 1 ].repeats == 2 );
    CHECK( entries[ 2 ].category == "com" );
    CHECK( entries[ 0 ].lastTime >= entries[ 0 ].time );

    // Same text with different data is a different step.
    ActivityJournal::record( "command", "send_action", { { "entityId", 1 } } );
    ActivityJournal::record( "command", "send_action", { { "entityId", 2 } } );
    CHECK( ActivityJournal::entries().size() == 5 );

    for ( int index = 0; index < 10; ++index ) {
        ActivityJournal::record( "file", QStringLiteral( "step %1" ).arg( index ) );
    }
    entries = ActivityJournal::entries();
    REQUIRE( entries.size() == 5 );
    CHECK( entries.back().text == "step 9" );
    CHECK( entries.front().text == "step 5" );
}

TEST_CASE( "Activity journal records from several threads", "[diagnostics]" )
{
    JournalCapacityGuard guard;
    ActivityJournal::clear();
    ActivityJournal::setCapacity( 10000 );

    std::vector<std::thread> threads;
    for ( int thread = 0; thread < 4; ++thread ) {
        threads.emplace_back( [ thread ] {
            for ( int index = 0; index < 500; ++index ) {
                ActivityJournal::record( "file", QStringLiteral( "t%1 #%2" ).arg( thread ).arg( index ) );
            }
        } );
    }
    for ( auto& thread : threads ) {
        thread.join();
    }
    CHECK( ActivityJournal::entries().size() == 2000 );
}

TEST_CASE( "Recent log lines keep warnings and errors with logging off", "[diagnostics]" )
{
    LoggingGuard guard;
    logging::enableLogging( false );
    logging::clearRecentLogLines();

    LOG_INFO << "diagnostics-info-marker";
    LOG_WARNING << "diagnostics-warning-marker";
    LOG_ERROR << "diagnostics-error-marker";

    const auto lines = logging::recentLogLines().join( '\n' );
    CHECK( lines.contains( "diagnostics-warning-marker" ) );
    CHECK( lines.contains( "diagnostics-error-marker" ) );
    CHECK_FALSE( lines.contains( "diagnostics-info-marker" ) );

    logging::setRecentLogCapacity( 3 );
    for ( int index = 0; index < 5; ++index ) {
        LOG_WARNING << "bounded-" << index;
    }
    const auto bounded = logging::recentLogLines();
    CHECK( bounded.size() == 3 );
    CHECK( bounded.back().contains( "bounded-4" ) );
    logging::setRecentLogCapacity( 2000 );

    // A burst of one warning is kept once with a count.
    logging::clearRecentLogLines();
    for ( int index = 0; index < 50; ++index ) {
        LOG_WARNING << "diagnostics-burst";
    }
    LOG_WARNING << "diagnostics-after-burst";
    const auto burst = logging::recentLogLines();
    CHECK( burst.filter( "diagnostics-burst" ).size() == 1 );
    CHECK( burst.join( '\n' ).contains( "repeated 49 more times" ) );
    CHECK( burst.back().contains( "diagnostics-after-burst" ) );

    logging::enableLogging( true, logging::LogLevel::Info );
    LOG_INFO << "diagnostics-info-enabled-marker";
    CHECK( logging::recentLogLines().join( '\n' ).contains( "diagnostics-info-enabled-marker" ) );
}

TEST_CASE( "Memory status describes process and system memory", "[diagnostics]" )
{
    const auto status = currentMemoryStatus();
    CHECK( status.processWorkingSetBytes > 0 );
    CHECK( status.systemTotalPhysicalBytes > 0 );
    CHECK( status.describe().contains( "CILogg" ) );
    CHECK( status.toVariantMap().contains( "systemCommitAvailableBytes" ) );

    const auto low = lowMemory();
    CHECK( low.systemLow() );
    CHECK( low.describe().contains( "% used" ) );
    auto plenty = low;
    plenty.systemCommitAvailableBytes = 40ull * 1024 * 1024 * 1024;
    CHECK_FALSE( plenty.systemLow() );
}

TEST_CASE( "A report bundle records what happened and the steps before it", "[diagnostics]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    DiagnosticReports::setRoot( root.filePath( "reports" ) );
    DiagnosticReports::setContextProvider( [] {
        return QVariantMap{ { "windows", QVariantList{ QVariantMap{
                                             { "tabs", QVariantList{ QVariantMap{
                                                           { "displayName", "com4.log" } } } } } } } };
    } );

    QDir().mkpath( root.filePath( "captures" ) );
    const auto capture = root.filePath( "captures/com4.log" );
    {
        QFile file( capture );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( QByteArray( 4096, 'x' ) );
    }

    ActivityJournal::clear();
    ActivityJournal::record( "command", "open_com portName=COM4",
                             { { "action", "open_com" }, { "portName", "COM4" } } );
    ActivityJournal::record( "response", "Response 3 \"Panel Ready\" fired on COM4" );
    ActivityJournal::record( "action", "Automatic action 48 \"MAGIC\" to COM4" );
    MemoryMonitor::clearForTest();
    MemoryMonitor::addSampleForTest( lowMemory() );

    ReportRequest request{ "exception", "Checking a file for changes",
                           "CheckFileChangesOperation failed: bad allocation", capture,
                           { { "indexedBytes", 123 } } };
    const auto result = DiagnosticReports::create( request );

    REQUIRE( result.ok );
    REQUIRE( QFileInfo( result.directory ).isDir() );
    for ( const auto* name : { "report.md", "report.json", "journal.log", "repro.json", "cilogg.log",
                               "memory.csv", "context.json" } ) {
        INFO( name );
        CHECK( QFileInfo::exists( QDir( result.directory ).filePath( name ) ) );
    }
    CHECK( QFileInfo::exists( result.archive ) );
    CHECK( result.summary.contains( "checking a file for changes" ) );
    CHECK( result.summary.contains( "com4.log" ) );
    CHECK( result.summary.contains( "bad allocation" ) );

    const auto markdown = readAll( QDir( result.directory ).filePath( "report.md" ) );
    CHECK( markdown.contains( "Checking a file for changes" ) );
    CHECK( markdown.contains( "CheckFileChangesOperation failed: bad allocation" ) );
    CHECK( markdown.contains( capture ) );
    CHECK( markdown.contains( "Steps that led here" ) );
    CHECK( markdown.contains( "open_com portName=COM4" ) );
    CHECK( markdown.contains( "How to reproduce" ) );

    const auto report = readJson( QDir( result.directory ).filePath( "report.json" ) );
    CHECK( report.value( "kind" ).toString() == "exception" );
    CHECK( report.value( "file" ).toObject().value( "name" ).toString() == "com4.log" );
    CHECK( report.value( "file" ).toObject().value( "size" ).toInt() == 4096 );
    CHECK( report.value( "details" ).toObject().value( "indexedBytes" ).toInt() == 123 );
    CHECK( report.value( "steps" ).toArray().size() >= 3 );
    CHECK( report.value( "context" ).toObject().contains( "windows" ) );
    CHECK( report.value( "memoryTrend" ).toArray().size() >= 1 );

    const auto repro = readJson( QDir( result.directory ).filePath( "repro.json" ) );
    const auto steps = repro.value( "steps" ).toArray();
    REQUIRE( steps.size() >= 3 );
    CHECK( steps.at( 0 ).toObject().value( "data" ).toObject().value( "portName" ).toString()
           == "COM4" );

    const auto listed = DiagnosticReports::list();
    REQUIRE( listed.size() == 1 );
    CHECK( listed.front().toMap().value( "id" ).toString() == result.id );
    CHECK( DiagnosticReports::find( result.id ).value( "kind" ).toString() == "exception" );

    // The public issue text carries the file name, never its folder, the journal or log lines.
    const auto body = DiagnosticReports::issueBody( result, request );
    CHECK( body.contains( "com4.log" ) );
    CHECK( body.contains( result.id ) );
    CHECK( body.contains( "bad allocation" ) );
    CHECK_FALSE( body.contains( QDir::toNativeSeparators( root.path() ) ) );
    CHECK_FALSE( body.contains( root.path() ) );
    CHECK_FALSE( body.contains( "MAGIC" ) );
    CHECK_FALSE( body.contains( "portName" ) );
    CHECK( DiagnosticReports::issueTitle( result, request ).contains( "Checking a file for changes" ) );

    // Folders inside the error text itself never reach the public issue either.
    // The report's own file is removed even with spaces in its folders; other paths without
    // spaces (Windows, UNC, POSIX) are reduced to their last component.
    ReportRequest withPaths{ "exception", "Indexing a file",
                             "cannot open C:\\Users\\someone\\Secret Project\\logs\\com4.log, "
                             "nor /home/someone/project/x.log or \\\\server\\share\\y.log",
                             "C:/Users/someone/Secret Project/logs/com4.log", {} };
    const auto scrubbed = DiagnosticReports::issueBody( result, withPaths )
                          + DiagnosticReports::issueTitle( result, withPaths );
    CHECK( scrubbed.contains( "com4.log" ) );
    CHECK( scrubbed.contains( "x.log" ) );
    CHECK( scrubbed.contains( "y.log" ) );
    CHECK_FALSE( scrubbed.contains( "someone" ) );
    CHECK_FALSE( scrubbed.contains( "Secret" ) );
    CHECK_FALSE( scrubbed.contains( "server" ) );

    DiagnosticReports::setContextProvider( {} );
    DiagnosticReports::setRoot( {} );
}

TEST_CASE( "Only the newest reports are kept and other folders are left alone", "[diagnostics]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    QDir().mkpath( root.filePath( "notes" ) );
    QDir().mkpath( root.filePath( "00000000-old-user-folder" ) );
    DiagnosticReports::setRoot( root.path() );
    DiagnosticReports::setKeep( 3 );
    QStringList ids;
    for ( int index = 0; index < 5; ++index ) {
        const auto report = DiagnosticReports::create( ReportRequest{ "manual", "Test", {}, {}, {} } );
        REQUIRE( report.ok );
        ids.push_back( report.id );
    }
    const auto listed = DiagnosticReports::list();
    REQUIRE( listed.size() == 3 );
    // Newest first, and exactly the last three created.
    CHECK( listed.at( 0 ).toMap().value( "id" ).toString() == ids.at( 4 ) );
    CHECK( listed.at( 2 ).toMap().value( "id" ).toString() == ids.at( 2 ) );
    CHECK( QDir( root.path() ).entryList( { "*.zip" }, QDir::Files ).size() == 3 );
    CHECK( QFileInfo( root.filePath( "notes" ) ).isDir() );
    CHECK( QFileInfo( root.filePath( "00000000-old-user-folder" ) ).isDir() );
    DiagnosticReports::setKeep( 20 );
    DiagnosticReports::setRoot( {} );
}

TEST_CASE( "Report throttle limits repeats of the same problem", "[diagnostics]" )
{
    ReportThrottle throttle( 1000 );
    const auto now = QDateTime::currentDateTimeUtc();
    CHECK( throttle.shouldReport( "a", now ) );
    CHECK_FALSE( throttle.shouldReport( "a", now.addMSecs( 100 ) ) );
    CHECK_FALSE( throttle.shouldReport( "a", now.addMSecs( 200 ) ) );
    CHECK( throttle.suppressed( "a" ) == 2 );
    CHECK( throttle.shouldReport( "b", now.addMSecs( 200 ) ) );

    int before = -1;
    CHECK( throttle.shouldReport( "a", now.addMSecs( 1500 ), &before ) );
    CHECK( before == 2 );
    CHECK( throttle.suppressed( "a" ) == 0 );
}

TEST_CASE( "A repeated background error writes one report", "[diagnostics]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    DiagnosticReports::setRoot( root.path() );
    ActivityJournal::clear();
    IssueReporter::setInteractive( false );

    const ReportRequest request{ "exception", "Searching a file",
                                 QStringLiteral( "FullSearchOperation failed: test %1" )
                                     .arg( QDateTime::currentMSecsSinceEpoch() ),
                                 {}, {} };
    IssueReporter::reportException( request );
    IssueReporter::reportException( request );

    CHECK( DiagnosticReports::list().size() == 1 );
    int errors = 0;
    for ( const auto& entry : ActivityJournal::entries() ) {
        if ( entry.category == "error" ) {
            ++errors;
            CHECK( entry.repeats == 2 );
        }
    }
    CHECK( errors == 1 );
    IssueReporter::setInteractive( true );
    DiagnosticReports::setRoot( {} );
}

TEST_CASE( "Diagnostic report commands parse and appear in the catalog", "[diagnostics][commander]" )
{
    const CliParameters help( { "cilogg", "command", "--help" } );
    const auto catalog
        = cilogg::mcp::parseCommandHelp( help.exit_message ).value( "commands" ).toObject();
    CHECK( catalog.contains( "list_reports" ) );
    CHECK( catalog.contains( "create_report" ) );

    const CliParameters list( { "cilogg", "command", "--action", "list_reports" } );
    REQUIRE_FALSE( list.parse_error );
    REQUIRE( list.commander_request );
    CHECK( list.commander_request->action == CommanderAction::ListReports );

    const CliParameters create(
        { "cilogg", "command", "--action", "create_report", "--text", " panel stopped " } );
    REQUIRE_FALSE( create.parse_error );
    REQUIRE( create.commander_request );
    CHECK( create.commander_request->action == CommanderAction::CreateReport );
    CHECK( create.commander_request->commentText == "panel stopped" );
    const auto restored
        = commanderRequestFromVariantMap( commanderRequestToVariantMap( *create.commander_request ) );
    REQUIRE( restored );
    CHECK( commanderActionToString( restored->action ) == "create_report" );

    const CliParameters selector(
        { "cilogg", "command", "--action", "create_report", "--tab-id", "abc" } );
    CHECK( selector.parse_error );
}
