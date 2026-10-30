// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysiscommands.h"
#include "analysisregex.h"
#include "analysisstatus.h"
#include "regexlabdock.h"
#include "valuecountdock.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch.hpp>

namespace {
QVariantMap memoryData( const QVariantMap& job );
QVariantMap awaitMemoryJob( AnalysisJobService& service, const QString& id )
{
    REQUIRE_FALSE( id.isEmpty() );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto state = service.status( id ).value( "state" ).toString();
            return state == "completed" || state == "failed" || state == "cancelled";
        },
        10000 ) );
    return service.status( id );
}

TEST_CASE( "Worker high-water aggregation preserves exact maxima and missing observations",
           "[logdata][analysis][workermemory]" )
{
    const auto partial = GENERATE( false, true );
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = "one\ntwo";
    request.limits.batchLines = 1;
    constexpr quint64 peak = 9007199254741009ULL;
    const auto id = service.start( request, [ partial ] {
        return [ index = 0, partial ]( const AnalysisBatch&, const std::atomic_bool&,
                                       int ) mutable {
            AnalysisOutput output;
            output.workerMemory.availability = AnalysisWorkerMemory::Availability::Measured;
            output.workerMemory.peakRssBytes = index == 0 ? peak : 1024;
            if ( partial && index == 2 ) {
                output.workerMemory.availability = AnalysisWorkerMemory::Availability::Unavailable;
                output.workerMemory.peakRssBytes = 0;
                output.workerMemory.nativeError = 5;
                output.workerMemory.terminationObservation = true;
            }
            ++index;
            return output;
        };
    } );
    const auto job = awaitMemoryJob( service, id );
    REQUIRE( job.value( "complete" ).toBool() );
    const auto memory = memoryData( job );
    CHECK( memory.value( "peakRssBytes" ) == QString::number( peak ) );
    CHECK( memory.value( "status" ) == ( partial ? "partial" : "measured" ) );
    CHECK( memory.value( "measuredObservations" ) == ( partial ? "2" : "3" ) );
    CHECK( memory.value( "missingObservations" ) == ( partial ? "1" : "0" ) );
    CHECK( memory.value( "lastObservation" ) == ( partial ? "termination_request" : "response" ) );
    CHECK( memory.value( "lastNativeError" ) == ( partial ? QVariant( "5" ) : QVariant{} ) );
    const auto json = QJsonDocument( QJsonObject::fromVariantMap( job ) ).toJson();
    CHECK( QJsonDocument::fromJson( json )
               .object()
               .value( "metrics" )
               .toObject()
               .value( "workerMemory" )
               .toObject()
               .value( "peakRssBytes" )
               .toString()
           == QString::number( peak ) );
}

TEST_CASE( "Invalid worker memory observations are rejected", "[logdata][analysis][workermemory]" )
{
    const auto mode = GENERATE( 0, 1, 2, 3 );
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    const auto id = service.start( request, [ mode ] {
        return [ mode ]( const AnalysisBatch&, const std::atomic_bool&, int ) {
            AnalysisOutput output;
            if ( mode == 0 )
                output.workerMemory.availability = AnalysisWorkerMemory::Availability::Measured;
            else if ( mode == 1 )
                output.workerMemory.availability
                    = static_cast<AnalysisWorkerMemory::Availability>( 99 );
            else if ( mode == 2 )
                output.workerMemory.peakRssBytes = 42;
            else
                output.workerMemory.nativeError = 5;
            return output;
        };
    } );
    const auto job = awaitMemoryJob( service, id );
    CHECK( job.value( "state" ) == "failed" );
    CHECK( job.value( "reason" ) == "invalid_worker_output" );
    CHECK_FALSE( job.value( "complete" ).toBool() );
}

TEST_CASE( "Analysis docks display the shared worker memory observation", "[ui][workermemory]" )
{
    const auto counts = GENERATE( false, true );
    QMainWindow window;
    AnalysisJobService service;
    auto execute = [ & ]( const CommanderRequest& request ) {
        const auto result
            = executeAnalysisOperation( service, commanderActionToString( request.action ),
                                        request.analysisArguments, {}, ANALYSIS_WORKER_PATH );
        return CommanderResult{ result.ok ? CommanderResultCode::Success
                                          : CommanderResultCode::InvalidRequest,
                                result.payload.value( "reason" ).toString(), result.payload };
    };
    const QVariantMap definition{
        { "pattern", "(\\d)" }, { "scope", "text" }, { "text", "a1\nb2" }, { "operation", "run" }
    };
    RegexLabDock* lab = nullptr;
    ValueCountDock* counter = nullptr;
    QDockWidget* dock = nullptr;
    if ( counts ) {
        counter = new ValueCountDock( &window, execute, [] { return QVariantList{}; } );
        dock = counter;
    }
    else {
        lab = new RegexLabDock(
            &window, execute, [] { return QVariantList{}; },
            []( const QString&, quint64 ) { return commanderSuccess(); } );
        dock = lab;
    }
    window.addDockWidget( Qt::RightDockWidgetArea, dock );
    window.show();
    REQUIRE(
        ( counts ? counter->setDefinition( definition ) : lab->setDefinition( definition ) ).ok() );
    auto state = [ & ] { return counts ? counter->state() : lab->state(); };
    REQUIRE( QTest::qWaitFor(
        [ & ] { return state().value( "job" ).toMap().value( "state" ) == "completed"; }, 10000 ) );
    const auto job = state().value( "job" ).toMap();
    const auto memory = memoryData( job );
    const auto* label = dock->findChild<QLabel*>( counts ? "valueCountsStatus" : "regexLabStatus" );
    REQUIRE( label );
    CHECK( label->text().contains( analysisWorkerMemoryText( job.value( "metrics" ).toMap() ) ) );
    CHECK( memory == memoryData( service.status( state().value( "jobId" ).toString() ) ) );
}
QVariantMap memoryData( const QVariantMap& job )
{
    const auto metrics = job.value( "metrics" ).toMap();
    REQUIRE( metrics.contains( "workerMemory" ) );
    return metrics.value( "workerMemory" ).toMap();
}
} // namespace

#ifdef Q_OS_WIN
TEST_CASE( "Worker high-water includes freed spikes and cancellation observations",
           "[logdata][analysis][workermemory]" )
{
    const auto cancel = GENERATE( false, true );
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = "one";
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto markerPath = directory.filePath( "allocated.pid" );
    const auto id = service.start(
        request, makeRegexProcessor( { { "pattern", "one" } }, ANALYSIS_FIXTURE_PATH,
                                     { cancel ? "memory_cancel" : "memory_pulse", markerPath } ) );
    QElapsedTimer elapsed;
    if ( cancel ) {
        REQUIRE( QTest::qWaitFor(
            [ & ] {
                QFile marker( markerPath );
                return marker.open( QIODevice::ReadOnly ) && marker.readAll().toLongLong() > 0;
            },
            10000 ) );
        elapsed.start();
        REQUIRE( service.cancel( id ) );
    }
    const auto job = awaitMemoryJob( service, id );
    const auto memory = memoryData( job );
    CHECK( memory.value( "status" ) == "measured" );
    CHECK( memory.value( "peakRssBytes" ).toString().toULongLong() >= 32ULL * 1024 * 1024 );
    if ( cancel ) {
        CHECK( job.value( "state" ) == "cancelled" );
        CHECK( elapsed.elapsed() < 1000 );
        CHECK( memory.value( "lastObservation" ) == "termination_request" );
    }
    else {
        REQUIRE( job.value( "complete" ).toBool() );
        const auto details = job.value( "errorDetails" ).toMap();
        const auto fixturePeak = details.value( "fixturePeakRssBytes" ).toString().toULongLong();
        REQUIRE( fixturePeak > 0 );
        CHECK( fixturePeak > details.value( "fixtureCurrentRssBytes" ).toString().toULongLong() );
        CHECK( memory.value( "peakRssBytes" ).toString().toULongLong() >= fixturePeak );
    }
    // Completion acknowledges destruction/reaping; a new real worker must still succeed.
    const auto recovery = service.start(
        request, makeRegexProcessor( { { "pattern", "one" } }, ANALYSIS_WORKER_PATH ) );
    CHECK( awaitMemoryJob( service, recovery ).value( "complete" ).toBool() );
}
#endif

TEST_CASE( "Regex jobs report worker high-water observations without summing response peaks",
           "[logdata][analysis][workermemory]" )
{
    const auto empty = GENERATE( false, true );
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = empty ? QString{} : QString( "a1\nb2" );
    request.limits.batchLines = 1;
    const auto id = service.start(
        request, makeRegexProcessor( { { "pattern", "\\d" } }, ANALYSIS_WORKER_PATH ) );
    const auto job = awaitMemoryJob( service, id );
    REQUIRE( job.value( "complete" ).toBool() );
    const auto memory = memoryData( job );
    CHECK( memory.value( "unit" ) == "bytes" );
    CHECK( memory.value( "scope" ) == "largest_single_worker_high_water_through_observation" );
#ifdef Q_OS_WIN
    CHECK( memory.value( "status" ) == "measured" );
    CHECK( memory.value( "backend" ) == "windows_peak_working_set" );
    REQUIRE( memory.value( "peakRssBytes" ).userType() == QMetaType::QString );
    CHECK( memory.value( "peakRssBytes" ).toString().toULongLong() > 0 );
    CHECK( memory.value( "measuredObservations" ) == ( empty ? "1" : "3" ) );
    CHECK( memory.value( "missingObservations" ) == "0" );
    CHECK( memory.value( "lastObservation" ) == "response" );
#else
    CHECK( memory.value( "status" ) == "unsupported" );
    CHECK( memory.value( "peakRssBytes" ).isNull() );
    CHECK( memory.value( "measuredObservations" ) == "0" );
    CHECK( memory.value( "missingObservations" ) == ( empty ? "1" : "3" ) );
#endif
    const auto json = QJsonDocument( QJsonObject::fromVariantMap( job ) ).toJson();
    const auto decoded = QJsonDocument::fromJson( json ).object();
    CHECK( decoded.value( "metrics" ).toObject().value( "workerMemory" ).toObject()
           == QJsonObject::fromVariantMap( memory ) );
    CHECK( service.release( id ) );
    CHECK( service.status( id ).value( "reason" ) == "job_expired" );
}

TEST_CASE( "Unavailable worker memory never becomes a zero-byte measurement",
           "[logdata][analysis][workermemory]" )
{
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = "one";
    const auto id = service.start( request );
    const auto job = awaitMemoryJob( service, id );
    REQUIRE( job.value( "complete" ).toBool() );
    CHECK_FALSE( job.value( "metrics" ).toMap().contains( "workerMemory" ) );
}
