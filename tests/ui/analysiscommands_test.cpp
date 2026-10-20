// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysiscommands.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <catch2/catch.hpp>
#include <limits>

namespace {
QVariantMap finishCommandJob( AnalysisJobService& service, const QString& id )
{
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto state = service.status( id ).value( "state" ).toString();
            return state == "completed" || state == "failed" || state == "cancelled";
        },
        10000 ) );
    return service.status( id );
}
std::shared_ptr<LogData> commandSource( const QString& path )
{
    auto source = std::make_shared<LogData>();
    source->attachFile( path );
    REQUIRE( QTest::qWaitFor( [ & ] { return source->sourceSnapshot().available; }, 10000 ) );
    source->setDisplayEncoding( "UTF-8" );
    return source;
}
} // namespace

TEST_CASE( "Analysis JSON operations preserve identity captures and paging",
           "[logdata][analysiscommands]" )
{
    AnalysisJobService service;
    const QVariantMap arguments{ { "scope", "text" },
                                 { "pattern", "(?<value>\\d)" },
                                 { "text", "one1\ntwo2\r\nthree3" },
                                 { "requestRevision", "9007199254740993" } };
    const auto started = executeAnalysisOperation( service, "regex_evaluate", arguments, {},
                                                   ANALYSIS_WORKER_PATH );
    REQUIRE( started.ok );
    const auto id = started.payload.value( "jobId" ).toString();
    const auto status = finishCommandJob( service, id );
    REQUIRE( status.value( "complete" ).toBool() );
    CHECK( status.value( "physicalLineCount" ).toInt() == 3 );
    CHECK( status.value( "requestRevision" ) == "9007199254740993" );
    CHECK( status.value( "captureBackend" ) == "Qt QRegularExpression" );
    CHECK( executeAnalysisOperation( service, "get_analysis_job", { { "jobId", id } } )
               .payload.value( "state" )
           == status.value( "state" ) );
    const auto first = executeAnalysisOperation( service, "get_analysis_results",
                                                 { { "jobId", id }, { "pageSize", 1 } } );
    REQUIRE( first.ok );
    const auto cursor = first.payload.value( "nextCursor" ).toString();
    REQUIRE_FALSE( cursor.isEmpty() );
    const auto row = first.payload.value( "rows" ).toList().front().toMap();
    CHECK( row.value( "jobId" ) == id );
    CHECK( row.value( "requestRevision" ) == "9007199254740993" );
    CHECK( row.value( "source" ).toMap().value( "sourceId" ) == status.value( "sourceId" ) );
    const auto second
        = executeAnalysisOperation( service, "get_analysis_results",
                                    { { "jobId", id }, { "cursor", cursor }, { "pageSize", 1 } } );
    REQUIRE( second.ok );
    CHECK( second.payload.value( "rows" ).toList().front().toMap().value( "lineNumber" ).toInt()
           == 2 );
    CHECK( second.payload.value( "rows" ).toList().front().toMap().value( "text" ) == "two2\r" );
    CHECK( QJsonDocument::fromVariant( second.payload ).toJson( QJsonDocument::Compact ).size()
           <= 8 * 1024 * 1024 );
    CHECK_FALSE( executeAnalysisOperation( service, "get_analysis_results",
                                           { { "jobId", id }, { "cursor", "other-job" } } )
                     .ok );
    REQUIRE( executeAnalysisOperation( service, "release_analysis_job", { { "jobId", id } } ).ok );
    CHECK( executeAnalysisOperation( service, "get_analysis_job", { { "jobId", id } } )
               .payload.value( "reason" )
           == "job_expired" );
}

TEST_CASE( "Analysis operation validation and cancellation remain deterministic",
           "[logdata][analysiscommands]" )
{
    AnalysisJobService service;
    QVariantMap request{ { "scope", "text" }, { "pattern", "x" }, { "text", "x" } };
    auto malicious = request;
    malicious[ "workerExecutable" ] = "arbitrary.exe";
    CHECK(
        executeAnalysisOperation( service, "regex_evaluate", malicious ).payload.value( "reason" )
        == "invalid_request" );
    request[ "limits" ] = QVariantMap{ { "timeoutMs", true } };
    CHECK( executeAnalysisOperation( service, "regex_evaluate", request ).payload.value( "reason" )
           == "invalid_limits" );
    request.remove( "limits" );
    request[ "requestRevision" ] = "-1";
    CHECK( executeAnalysisOperation( service, "regex_evaluate", request ).payload.value( "reason" )
           == "invalid_revision" );
    request.remove( "requestRevision" );
    const auto start
        = executeAnalysisOperation( service, "regex_evaluate", request, {}, ANALYSIS_WORKER_PATH );
    REQUIRE( start.ok );
    const auto id = start.payload.value( "jobId" ).toString();
    REQUIRE( executeAnalysisOperation( service, "cancel_analysis_job", { { "jobId", id } } ).ok );
    CHECK( executeAnalysisOperation( service, "cancel_analysis_job", { { "jobId", id } } ).ok );
    CHECK( finishCommandJob( service, id ).value( "state" ) == "cancelled" );
    CHECK( executeAnalysisOperation( service, "get_analysis_results",
                                     { { "jobId", id }, { "pageSize", 1.5 } } )
               .payload.value( "reason" )
           == "invalid_page_size" );
}

TEST_CASE( "Analysis retention expiry releases empty and nonempty jobs",
           "[logdata][analysiscommands]" )
{
    auto clock = std::make_shared<qint64>( 0 );
    AnalysisJobService service( nullptr, [ clock ] { return *clock; } );
    AnalysisRequest request;
    request.scope = "text";
    request.text = "retained";
    const auto id = service.start( request );
    REQUIRE( finishCommandJob( service, id ).value( "complete" ).toBool() );
    *clock = 600001;
    REQUIRE( QTest::qWaitFor(
        [ & ] { return service.status( id ).value( "reason" ) == "job_expired"; }, 2000 ) );
    CHECK( service.results( id ).value( "reason" ) == "job_expired" );
}

TEST_CASE( "Scope freezing skips long continuous runs and bounds sparse intervals",
           "[logdata][analysiscommands]" )
{
    quint64 calls = 0;
    QString error;
    const auto continuous = freezeAnalysisRanges(
        { { 0, 1000000000 } },
        [ &calls ]( quint64 row ) {
            ++calls;
            return row + 100;
        },
        &error );
    REQUIRE( continuous );
    REQUIRE( continuous->size() == 1 );
    CHECK( continuous->front().first == 100 );
    CHECK( continuous->front().second == 1000000100 );
    CHECK( calls < 100 );
    const auto sparse
        = freezeAnalysisRanges( { { 0, 10000 } }, []( quint64 row ) { return row * 2; }, &error );
    CHECK_FALSE( sparse );
    CHECK( error == "scope_limit" );
}

TEST_CASE( "Match scopes exclude context and marks and preserve search limits",
           "[logdata][analysiscommands]" )
{
    QTemporaryDir directory;
    const auto path = directory.filePath( "matches.log" );
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( "before\nhit\ncontext\nmark\nhit\n" ) > 0 );
    }
    auto source = commandSource( path );
    auto filtered = std::make_unique<LogFilteredData>( source.get() );
    SearchOptions options;
    options.contextBefore = 1;
    options.contextAfter = 1;
    filtered->addMark( 3_lnum );
    filtered->runSearch( RegularExpressionPattern( "hit" ), 0_lnum,
                         LineNumber{ source->getNbLine().get() }, options );
    REQUIRE( QTest::qWaitFor( [ & ] { return !filtered->isSearchRunning(); }, 10000 ) );
    QString error;
    const auto ranges = filtered->analysisMatchRanges( &error );
    REQUIRE( ranges );
    REQUIRE( ranges->size() == 2 );
    CHECK( ranges->at( 0 ).first == 1 );
    CHECK( ranges->at( 1 ).first == 4 );
    options.maxMatches = 1;
    filtered->runSearch( RegularExpressionPattern( "hit" ), 0_lnum,
                         LineNumber{ source->getNbLine().get() }, options );
    REQUIRE( QTest::qWaitFor( [ & ] { return !filtered->isSearchRunning(); }, 10000 ) );
    REQUIRE( filtered->limitReached() );
    const auto limited = filtered->analysisMatchRanges( &error );
    REQUIRE( limited );
    REQUIRE( limited->size() == 1 );
    AnalysisRequest request;
    request.source = source;
    request.snapshot = source->sourceSnapshot();
    request.scope = "matches";
    request.ranges = *limited;
    request.scopeLimited = true;
    AnalysisJobService service;
    const auto id = service.start( request );
    const auto result = finishCommandJob( service, id );
    CHECK_FALSE( result.value( "complete" ).toBool() );
    CHECK( result.value( "reason" ) == "scope_limit" );
    source->reload();
    CHECK_FALSE( filtered->analysisMatchRanges( &error ) );
    CHECK( error == "stale_search" );
}

TEST_CASE( "Match scopes reject appended provisional tails until a fresh search",
           "[logdata][analysiscommands]" )
{
    const bool limited = GENERATE( false, true );
    QTemporaryDir directory;
    const auto path = directory.filePath( "tail.log" );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "hit" );
    file.close();
    auto source = commandSource( path );
    LogFilteredData filtered( source.get() );
    SearchOptions options;
    if ( limited )
        options.maxMatches = 1;
    filtered.runSearch( RegularExpressionPattern( "hit$" ), 0_lnum, 1_lnum, options );
    REQUIRE( QTest::qWaitFor( [ & ] { return !filtered.isSearchRunning(); }, 10000 ) );
    QString error;
    REQUIRE( filtered.analysisMatchRanges( &error ) );
    const auto revision = source->sourceSnapshot().revision;
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Append ) );
    file.write( "gone\nnext\n" );
    file.close();
    REQUIRE( QMetaObject::invokeMethod( source.get(), "fileChangedOnDisk", Qt::DirectConnection,
                                        Q_ARG( QString, path ) ) );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            return source->sourceSnapshot().available
                   && source->sourceSnapshot().revision != revision;
        },
        10000 ) );
    CHECK_FALSE( filtered.analysisMatchRanges( &error ) );
    CHECK( error == "stale_search" );
    if ( limited ) {
        filtered.updateSearch( 0_lnum, 2_lnum );
        CHECK_FALSE( filtered.analysisMatchRanges( &error ) );
        CHECK( error == "stale_search" );
    }
    filtered.runSearch( RegularExpressionPattern( "hit$" ), 0_lnum, 2_lnum, options );
    REQUIRE( QTest::qWaitFor( [ & ] { return !filtered.isSearchRunning(); }, 10000 ) );
    const auto fresh = filtered.analysisMatchRanges( &error );
    REQUIRE( fresh );
    CHECK( fresh->empty() );
}

TEST_CASE( "Analysis row scopes stop at the final requested row", "[logdata][analysiscommands]" )
{
    QTemporaryDir directory;
    const auto path = directory.filePath( "suffix.log" );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "selected\n" );
    file.write( QByteArray( 1024 * 1024 + 1, 'x' ) );
    file.close();
    auto source = commandSource( path );
    AnalysisRequest request;
    request.source = source;
    request.snapshot = source->sourceSnapshot();
    request.scope = "selection";
    request.ranges = { { 0, 0 } };
    AnalysisJobService service;
    const auto id = service.start( request );
    CHECK( finishCommandJob( service, id ).value( "complete" ).toBool() );
    CHECK( service.results( id ).value( "rows" ).toList().size() == 1 );
}

TEST_CASE( "An empty source-close signal does not cancel pasted text",
           "[logdata][analysiscommands]" )
{
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = "retained";
    const auto id = service.start( request );
    service.cancelSource( nullptr );
    CHECK( finishCommandJob( service, id ).value( "complete" ).toBool() );
}

TEST_CASE( "Direct analysis admission rejects invalid row ranges", "[logdata][analysiscommands]" )
{
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = "first\nsecond";
    const std::vector<std::vector<std::pair<quint64, quint64>>> invalid{
        { { 0, std::numeric_limits<quint64>::max() } },
        { { 2, 2 } },
        { { 1, 0 } },
        { { 1, 1 }, { 0, 0 } },
        { { 0, 1 }, { 1, 1 } }
    };
    for ( const auto& ranges : invalid ) {
        request.ranges = ranges;
        QString error;
        CHECK( service.start( request, {}, &error ).isEmpty() );
        CHECK( error == "invalid_scope" );
    }
}

TEST_CASE( "Global analysis storage evicts finished jobs and preserves an active producer",
           "[logdata][analysiscommands]" )
{
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = QString( "x\n" ).repeated( 16 );
    request.limits.batchLines = 1;
    auto hold = std::make_shared<std::atomic_bool>( true );
    const auto factory = [ hold ]( bool pause ) -> AnalysisProcessorFactory {
        return [ hold, pause ] {
            return [ hold, pause ]( const AnalysisBatch& batch, const std::atomic_bool& cancelled,
                                    int ) {
                AnalysisOutput output;
                for ( const auto& line : batch.lines ) {
                    if ( pause && line.line == 15 ) {
                        while ( *hold && !cancelled )
                            QThread::msleep( 1 );
                    }
                    output.rows.push_back(
                        QVariantMap{ { "lineNumber", static_cast<int>( line.line + 1 ) },
                                     { "text", QString( 1024 * 1024, 'x' ) } } );
                }
                return output;
            };
        };
    };
    const auto active = service.start( request, factory( true ) );
    REQUIRE( QTest::qWaitFor(
        [ & ] { return service.status( active ).value( "resultRecords" ).toInt() == 15; },
        10000 ) );
    QStringList finished;
    for ( int index = 0; index < 4; ++index ) {
        const auto id = service.start( request, factory( false ) );
        finishCommandJob( service, id );
        finished.push_back( id );
        qint64 bytes = service.status( active ).value( "retainedBytes" ).toLongLong();
        for ( const auto& job : finished )
            bytes += service.status( job ).value( "retainedBytes" ).toLongLong();
        CHECK( bytes <= 64 * 1024 * 1024 );
        CHECK( service.status( active ).value( "state" ) == "running" );
    }
    CHECK( service.status( finished.front() ).value( "reason" ) == "job_expired" );
    CHECK( service.status( active ).value( "resultRecords" ).toInt() == 15 );
    *hold = false;
    CHECK( finishCommandJob( service, active ).value( "reason" ) == "result_limit" );
}
