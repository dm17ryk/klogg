// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch.hpp>

#include <QMap>
#include <QVariantMap>

#include "analysisruns.h"

namespace {
// A stand-in for the analysis service: admits `capacity` unfinished jobs.
struct FakeService {
    int capacity = 10;
    int next = 0;
    QMap<QString, QString> states;
    QStringList started;
    QStringList released;
    QStringList cancelled;

    int unfinished() const
    {
        int count = 0;
        for ( const auto& state : states )
            count += isTerminalAnalysisState( state ) ? 0 : 1;
        return count;
    }
    CommanderResult start( const QString& tabId )
    {
        if ( tabId == "broken" )
            return { CommanderResultCode::InvalidRequest,
                     "source_not_ready",
                     { { "reason", "source_not_ready" } } };
        if ( unfinished() >= capacity )
            return { CommanderResultCode::InvalidRequest,
                     "queue_limit",
                     { { "reason", "queue_limit" } } };
        const auto id = QString( "job%1" ).arg( ++next );
        states[ id ] = "running";
        started << tabId;
        return commanderSuccess( {}, { { "jobId", id }, { "state", "queued" } } );
    }
    CommanderResult status( const QString& id ) const
    {
        if ( !states.contains( id ) )
            return { CommanderResultCode::InvalidRequest,
                     "job_expired",
                     { { "reason", "job_expired" } } };
        return commanderSuccess( {}, { { "state", states.value( id ) } } );
    }
    CommanderResult release( const QString& id )
    {
        released << id;
        states.remove( id );
        return commanderSuccess();
    }
    CommanderResult cancel( const QString& id )
    {
        cancelled << id;
        states[ id ] = "cancelled";
        return commanderSuccess();
    }
    void finishAll()
    {
        for ( auto& state : states )
            if ( !isTerminalAnalysisState( state ) )
                state = "completed";
    }
};

QStringList tabs( int count )
{
    QStringList result;
    for ( int index = 0; index < count; ++index )
        result << QString( "t%1" ).arg( index );
    return result;
}

struct Harness {
    FakeService service;
    AnalysisRuns runs;

    CommanderResult start()
    {
        return runs.startWaiting( [ this ]( const QString& tabId ) { return service.start( tabId ); } );
    }
    void update( int shown )
    {
        runs.update( [ this ]( const QString& id ) { return service.status( id ); },
                     [ this ]( const QString& id ) { return service.release( id ); }, shown );
    }
};
} // namespace

TEST_CASE( "all_open_sources_start_as_the_service_admits_them", "[workbench][U15]" )
{
    Harness harness;
    harness.runs.reset( tabs( 40 ), { QVariantMap{ { "tabId", "t1" }, { "label", "1: b.log" } } } );
    REQUIRE( harness.runs.size() == 40 );
    REQUIRE( harness.runs.at( 1 ).label == "1: b.log" );
    REQUIRE( harness.runs.at( 2 ).label == "t2" );

    // One Run keeps at most MaxActiveAnalysisRuns unfinished jobs, leaving room for others.
    REQUIRE( harness.start().ok() );
    REQUIRE( harness.runs.started() == MaxActiveAnalysisRuns );
    REQUIRE( harness.runs.at( MaxActiveAnalysisRuns ).state == "waiting" );
    REQUIRE( harness.runs.summary( 0 ).startsWith( "40 sources: " ) );
    REQUIRE( harness.runs.summary( 0 ).contains( "32 waiting" ) );

    // A full service (queue_limit) is not a failure: the runs keep waiting.
    harness.service.capacity = 0;
    harness.service.finishAll();
    harness.update( 0 );
    REQUIRE( harness.start().ok() );
    REQUIRE( harness.runs.started() == MaxActiveAnalysisRuns );

    // Every source runs eventually, in order.
    harness.service.capacity = 10;
    for ( int round = 0; round < 10 && harness.runs.active(); ++round ) {
        REQUIRE( harness.start().ok() );
        harness.service.finishAll();
        harness.update( 0 );
    }
    REQUIRE_FALSE( harness.runs.active() );
    REQUIRE( harness.service.started == tabs( 40 ) );
    REQUIRE( harness.runs.summary( 0 ).contains( "40 completed" ) );
}

TEST_CASE( "evicted_source_reruns_on_select", "[workbench][U15]" )
{
    Harness harness;
    harness.runs.reset( tabs( 30 ), {} );
    for ( int round = 0; round < 10 && harness.runs.active(); ++round ) {
        harness.start();
        harness.service.finishAll();
        harness.update( 0 );
    }
    REQUIRE_FALSE( harness.runs.active() );
    // The shown run and the newest MaxRetainedAnalysisRuns keep their jobs.
    int retained = 0;
    for ( int index = 0; index < harness.runs.size(); ++index )
        retained += harness.runs.at( index ).jobId.isEmpty() ? 0 : 1;
    REQUIRE( retained == MaxRetainedAnalysisRuns + 1 );
    REQUIRE_FALSE( harness.runs.at( 0 ).jobId.isEmpty() );
    REQUIRE( harness.runs.at( 1 ).jobId.isEmpty() );
    REQUIRE( harness.runs.at( 1 ).state == "completed" );

    // A dropped run is computed again when shown.
    REQUIRE( harness.runs.restart( 1 ) );
    REQUIRE( harness.runs.at( 1 ).state == "waiting" );
    REQUIRE( harness.start().ok() );
    REQUIRE_FALSE( harness.runs.at( 1 ).jobId.isEmpty() );
    // A run that still has its results is not restarted.
    REQUIRE_FALSE( harness.runs.restart( 0 ) );

    // A job evicted by the service is dropped too.
    harness.service.states.clear();
    harness.update( 1 );
    REQUIRE( harness.runs.at( 1 ).state == "job_expired" );
    REQUIRE( harness.runs.restart( 1 ) );
}

TEST_CASE( "sources_that_cannot_start_and_cancel", "[workbench][U15]" )
{
    Harness harness;
    harness.runs.reset( { "t0", "broken", "t2" }, {} );
    const auto failure = harness.start();
    REQUIRE_FALSE( failure.ok() );
    REQUIRE( failure.payload.value( "reason" ) == "source_not_ready" );
    REQUIRE( harness.runs.at( 1 ).finished );
    REQUIRE( harness.runs.at( 1 ).state == "source_not_ready" );
    // A source that never started is not "dropped": it does not restart by itself.
    REQUIRE_FALSE( harness.runs.restart( 1 ) );
    REQUIRE( harness.runs.started() == 2 );
    REQUIRE( harness.runs.firstStarted() == 0 );

    Harness waiting;
    waiting.service.capacity = 1;
    waiting.runs.reset( tabs( 3 ), {} );
    REQUIRE( waiting.start().ok() );
    REQUIRE( waiting.runs.started() == 1 );
    REQUIRE( waiting.runs.cancel( [ &waiting ]( const QString& id ) {
                                   return waiting.service.cancel( id );
                               } )
                 .ok() );
    REQUIRE( waiting.service.cancelled == QStringList{ "job1" } );
    REQUIRE( waiting.runs.at( 1 ).state == "cancelled" );
    waiting.update( 0 );
    REQUIRE_FALSE( waiting.runs.active() );

    waiting.runs.releaseAll( [ &waiting ]( const QString& id ) {
        return waiting.service.release( id );
    } );
    REQUIRE( waiting.runs.empty() );
    REQUIRE( waiting.service.released == QStringList{ "job1" } );
}

TEST_CASE( "result_source_index_must_be_a_whole_number", "[workbench][U15]" )
{
    REQUIRE( analysisRunIndex( 0 ) == 0 );
    REQUIRE( analysisRunIndex( 3.0 ) == 3 );
    REQUIRE( analysisRunIndex( QVariant::fromValue<qlonglong>( 2 ) ) == 2 );
    REQUIRE_FALSE( analysisRunIndex( 0.5 ) );
    REQUIRE_FALSE( analysisRunIndex( -1 ) );
    REQUIRE_FALSE( analysisRunIndex( true ) );
    REQUIRE_FALSE( analysisRunIndex( QString( "1" ) ) );
    REQUIRE_FALSE( analysisRunIndex( QVariant{} ) );
    // Out of the int range. (No infinity here: some builds use fast-math, where it is undefined.)
    REQUIRE_FALSE( analysisRunIndex( 1e20 ) );
}
