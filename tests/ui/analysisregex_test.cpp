// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysisregex.h"
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch.hpp>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#endif

namespace {
qint64 fixturePid( const QString& path )
{
    QFile marker( path );
    return marker.open( QIODevice::ReadOnly ) ? marker.readAll().toLongLong() : 0;
}
bool processAlive( qint64 pid )
{
#ifdef Q_OS_WIN
    const auto process = OpenProcess( SYNCHRONIZE, FALSE, static_cast<DWORD>( pid ) );
    if ( !process ) {
        return false;
    }
    const bool alive = WaitForSingleObject( process, 0 ) == WAIT_TIMEOUT;
    CloseHandle( process );
    return alive;
#else
    return kill( static_cast<pid_t>( pid ), 0 ) == 0 || errno == EPERM;
#endif
}
QVariantMap awaitRegex( AnalysisJobService& service, const QString& id )
{
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto state = service.status( id ).value( "state" ).toString();
            return state == "completed" || state == "failed" || state == "cancelled";
        },
        10000 ) );
    return service.status( id );
}
} // namespace
TEST_CASE( "Supervised regex workers preserve captures and compile empty scopes",
           "[logdata][analysis][regexworker]" )
{
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = "a1\nb2\na3";
    const auto id = service.start(
        request, makeRegexProcessor( { { "pattern", "a(?<number>\\d)" } }, ANALYSIS_WORKER_PATH ) );
    const auto state = awaitRegex( service, id );
    INFO( state.value( "reason" ).toString().toStdString() );
    REQUIRE( state.value( "complete" ).toBool() );
    const auto rows = service.results( id ).value( "rows" ).toList();
    REQUIRE( rows.size() == 2 );
    CHECK( rows.at( 1 ).toMap().value( "lineNumber" ).toInt() == 3 );
    const auto metrics = state.value( "metrics" ).toMap();
    CHECK( metrics.contains( "compileUs" ) );
    CHECK( metrics.contains( "matchUs" ) );
    CHECK( metrics.contains( "startupUs" ) );
    request.text.clear();
    const auto invalid = service.start(
        request, makeRegexProcessor( { { "pattern", "(" } }, ANALYSIS_WORKER_PATH ) );
    CHECK( awaitRegex( service, invalid ).value( "reason" ) == "invalid_pattern" );
    CHECK( service.status( invalid ).value( "errorDetails" ).toMap().contains( "offsetUtf16" ) );
    const auto empty = service.start(
        request, makeRegexProcessor( { { "pattern", "x" } }, ANALYSIS_WORKER_PATH ) );
    CHECK( awaitRegex( service, empty ).value( "complete" ).toBool() );
}

TEST_CASE( "Supervised workers enforce deadlines cancellation and protocol recovery",
           "[logdata][analysis][regexworker]" )
{
    const auto mode = GENERATE(
        QString( "stall" ), QString( "crash" ), QString( "oversize" ), QString( "cancel" ),
        QString( "stderr_flood" ), QString( "bad_compile" ), QString( "bad_match" ),
        QString( "large_reason" ), QString( "bad_ready" ), QString( "preloaded_match" ) );
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = "x";
    QTemporaryDir directory;
    const auto marker = directory.filePath( "worker.pid" );
    request.limits.batchTimeoutMs = mode == "stall" ? 1000 : 10000;
    const auto id
        = service.start( request, makeRegexProcessor( { { "pattern", "x" } }, ANALYSIS_FIXTURE_PATH,
                                                      { mode, marker } ) );
    if ( mode == "cancel" ) {
        REQUIRE( QTest::qWaitFor( [ & ] { return fixturePid( marker ) > 0; }, 10000 ) );
        QElapsedTimer timer;
        timer.start();
        REQUIRE( service.cancel( id ) );
        CHECK( awaitRegex( service, id ).value( "state" ) == "cancelled" );
        CHECK( timer.elapsed() < 1000 );
    }
    else {
        const auto state = awaitRegex( service, id );
        CHECK_FALSE( state.value( "complete" ).toBool() );
        const auto expected = mode == "stall"   ? "timeout"
                              : mode == "crash" ? "worker_failure"
                                                : "invalid_worker_output";
        CHECK( state.value( "reason" ) == expected );
    }
    const auto pid = fixturePid( marker );
    if ( pid > 0 ) {
        CHECK_FALSE( processAlive( pid ) );
    }
    // A failed or killed child cannot poison the next job.
    request.limits.batchTimeoutMs = 2000;
    const auto recovery = service.start(
        request, makeRegexProcessor( { { "pattern", "x" } }, ANALYSIS_WORKER_PATH ) );
    CHECK( awaitRegex( service, recovery ).value( "complete" ).toBool() );
}

TEST_CASE( "Supervised real regexes cannot stall the analysis service",
           "[logdata][analysis][regexworker]" )
{
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = QString( 20000, 'a' ) + "!";
    const auto id = service.start(
        request, makeRegexProcessor( { { "pattern", "(*NO_JIT)(*LIMIT_MATCH=1000000000)(a+)+$" } },
                                     ANALYSIS_WORKER_PATH ) );
    const auto state = awaitRegex( service, id );
    INFO( state.value( "reason" ).toString().toStdString() );
    CHECK_FALSE( state.value( "complete" ).toBool() );
    CHECK( ( state.value( "reason" ) == "timeout"
             || state.value( "reason" ) == "match_resource_limit" ) );
    CHECK( state.value( "metrics" ).toMap().contains( "compileUs" ) );
    request.text = "ok";
    const auto next = service.start(
        request, makeRegexProcessor( { { "pattern", "ok" } }, ANALYSIS_WORKER_PATH ) );
    CHECK( awaitRegex( service, next ).value( "complete" ).toBool() );
}

TEST_CASE( "Worker startup is measured separately and still obeys the job deadline",
           "[logdata][analysis][regexworker]" )
{
    const auto shortJob = GENERATE( false, true );
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.limits.batchTimeoutMs = 100;
    request.limits.timeoutMs = shortJob ? 100 : 5000;
    const auto id
        = service.start( request, makeRegexProcessor( { { "pattern", "x" } }, ANALYSIS_FIXTURE_PATH,
                                                      { "slow_start" } ) );
    const auto state = awaitRegex( service, id );
    if ( shortJob ) {
        CHECK( state.value( "reason" ) == "timeout" );
        CHECK_FALSE( state.value( "complete" ).toBool() );
    }
    else {
        CHECK( state.value( "complete" ).toBool() );
        CHECK( state.value( "metrics" ).toMap().value( "startupUs" ).toLongLong() >= 250000 );
        CHECK( state.value( "metrics" ).toMap().value( "compileUs" ).toLongLong() == 1 );
    }
}

TEST_CASE( "Compilation starts inside its enforced execution budget",
           "[logdata][analysis][regexworker]" )
{
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.limits.batchTimeoutMs = 100;
    request.limits.timeoutMs = 5000;
    const auto id
        = service.start( request, makeRegexProcessor( { { "pattern", "x" } }, ANALYSIS_FIXTURE_PATH,
                                                      { "slow_compile" } ) );
    const auto state = awaitRegex( service, id );
    CHECK( state.value( "reason" ) == "timeout" );
    CHECK( state.value( "errorDetails" ).toMap().value( "phase" ) == "compilation" );
    CHECK_FALSE( state.value( "complete" ).toBool() );
}
