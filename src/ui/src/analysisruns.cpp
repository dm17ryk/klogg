// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysisruns.h"

#include "log.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace {
QString tr( const char* text )
{
    return QCoreApplication::translate( "AnalysisRuns", text );
}
} // namespace

bool isTerminalAnalysisState( const QString& state )
{
    return state == QLatin1String( "completed" ) || state == QLatin1String( "failed" )
           || state == QLatin1String( "cancelled" );
}

std::optional<int> analysisRunIndex( const QVariant& value )
{
    const auto type = value.userType();
    const bool numeric = type == QMetaType::Int || type == QMetaType::UInt
                         || type == QMetaType::LongLong || type == QMetaType::ULongLong
                         || type == QMetaType::Double || type == QMetaType::Float;
    bool converted = false;
    const auto number = value.toDouble( &converted );
    if ( !numeric || !converted || !std::isfinite( number ) || number != std::floor( number )
         || number < 0 || number > static_cast<double>( std::numeric_limits<int>::max() ) ) {
        return std::nullopt;
    }
    return static_cast<int>( number );
}

void AnalysisRuns::reset( const QStringList& tabIds, const QVariantList& sources )
{
    clear();
    for ( const auto& tabId : tabIds ) {
        Run run;
        run.tabId = tabId;
        run.label = tabId.isEmpty() ? tr( "Focused tab" ) : tabId;
        for ( const auto& value : sources ) {
            const auto source = value.toMap();
            if ( source.value( QStringLiteral( "tabId" ) ).toString() == tabId ) {
                run.label = source.value( QStringLiteral( "label" ) ).toString();
                break;
            }
        }
        runs_.push_back( run );
    }
}

void AnalysisRuns::clear()
{
    runs_.clear();
    finishedCount_ = 0;
}

bool AnalysisRuns::empty() const
{
    return runs_.empty();
}

int AnalysisRuns::size() const
{
    return static_cast<int>( runs_.size() );
}

const AnalysisRuns::Run& AnalysisRuns::at( int index ) const
{
    return runs_.at( static_cast<size_t>( index ) );
}

int AnalysisRuns::firstStarted() const
{
    for ( size_t index = 0; index < runs_.size(); ++index ) {
        if ( !runs_[ index ].jobId.isEmpty() ) {
            return static_cast<int>( index );
        }
    }
    return 0;
}

bool AnalysisRuns::active() const
{
    return std::any_of( runs_.cbegin(), runs_.cend(),
                        []( const Run& run ) { return !run.finished; } );
}

int AnalysisRuns::started() const
{
    return static_cast<int>( std::count_if( runs_.cbegin(), runs_.cend(), []( const Run& run ) {
        return !run.jobId.isEmpty();
    } ) );
}

void AnalysisRuns::finish( Run& run, const QString& state )
{
    run.state = state;
    run.finished = true;
    run.finishedOrder = ++finishedCount_;
}

CommanderResult AnalysisRuns::startWaiting( const Start& start )
{
    CommanderResult failure;
    auto unfinished
        = static_cast<int>( std::count_if( runs_.cbegin(), runs_.cend(), []( const Run& run ) {
              return !run.finished && !run.jobId.isEmpty();
          } ) );
    for ( auto& run : runs_ ) {
        if ( run.finished || !run.jobId.isEmpty() ) {
            continue;
        }
        if ( unfinished >= MaxActiveAnalysisRuns ) {
            break;
        }
        const auto result = start( run.tabId );
        const auto jobId = result.payload.value( QStringLiteral( "jobId" ) ).toString();
        if ( result.ok() && !jobId.isEmpty() ) {
            run.jobId = jobId;
            run.state = result.payload.value( QStringLiteral( "state" ), QStringLiteral( "queued" ) )
                            .toString();
            ++unfinished;
            continue;
        }
        const auto reason = result.payload.value( QStringLiteral( "reason" ) ).toString();
        if ( reason == QLatin1String( "queue_limit" ) ) {
            // The service is full; the next update tries again.
            break;
        }
        LOG_DEBUG << "Analysis source could not start tab=" << run.tabId << " reason=" << reason;
        finish( run, reason.isEmpty() ? QStringLiteral( "failed" ) : reason );
        if ( failure.ok() ) {
            failure = result.ok() ? CommanderResult{ CommanderResultCode::InvalidRequest,
                                                     tr( "The analysis job did not start." ),
                                                     { { QStringLiteral( "reason" ),
                                                         QStringLiteral( "failed" ) } } }
                                  : result;
        }
    }
    return failure;
}

void AnalysisRuns::update( const JobAction& status, const JobAction& release, int shown )
{
    for ( auto& run : runs_ ) {
        if ( run.finished || run.jobId.isEmpty() ) {
            continue;
        }
        const auto result = status( run.jobId );
        if ( !result.ok() ) {
            // Released or evicted elsewhere: the source runs again when it is shown.
            const auto reason = result.payload.value( QStringLiteral( "reason" ) ).toString();
            run.jobId.clear();
            run.dropped = true;
            finish( run, reason.isEmpty() ? QStringLiteral( "job_expired" ) : reason );
            continue;
        }
        run.state = result.payload.value( QStringLiteral( "state" ) ).toString();
        if ( isTerminalAnalysisState( run.state ) ) {
            finish( run, run.state );
        }
    }
    // Keep the newest finished jobs; the shown source keeps its job.
    std::vector<size_t> retained;
    for ( size_t index = 0; index < runs_.size(); ++index ) {
        const auto& run = runs_[ index ];
        if ( run.finished && !run.jobId.isEmpty() && static_cast<int>( index ) != shown ) {
            retained.push_back( index );
        }
    }
    if ( retained.size() <= static_cast<size_t>( MaxRetainedAnalysisRuns ) ) {
        return;
    }
    std::sort( retained.begin(), retained.end(), [ this ]( size_t left, size_t right ) {
        return runs_[ left ].finishedOrder < runs_[ right ].finishedOrder;
    } );
    const auto excess = retained.size() - static_cast<size_t>( MaxRetainedAnalysisRuns );
    for ( size_t position = 0; position < excess; ++position ) {
        auto& run = runs_[ retained[ position ] ];
        LOG_DEBUG << "Releasing finished analysis job " << run.jobId << " of tab " << run.tabId;
        release( run.jobId );
        run.jobId.clear();
        run.dropped = true;
    }
}

CommanderResult AnalysisRuns::cancel( const JobAction& cancelJob )
{
    CommanderResult result;
    for ( auto& run : runs_ ) {
        if ( run.finished ) {
            continue;
        }
        if ( run.jobId.isEmpty() ) {
            finish( run, QStringLiteral( "cancelled" ) );
            continue;
        }
        const auto cancelled = cancelJob( run.jobId );
        if ( result.ok() && !cancelled.ok() ) {
            result = cancelled;
        }
    }
    return result;
}

void AnalysisRuns::releaseAll( const JobAction& release )
{
    for ( const auto& run : runs_ ) {
        if ( !run.jobId.isEmpty() ) {
            release( run.jobId );
        }
    }
    clear();
}

bool AnalysisRuns::restart( int index )
{
    if ( index < 0 || index >= size() ) {
        return false;
    }
    auto& run = runs_[ static_cast<size_t>( index ) ];
    if ( !run.dropped || !run.jobId.isEmpty() ) {
        return false;
    }
    LOG_DEBUG << "Analysis source runs again tab=" << run.tabId << " after " << run.state;
    run.state = QStringLiteral( "waiting" );
    run.finished = false;
    run.dropped = false;
    run.finishedOrder = 0;
    return true;
}

QString AnalysisRuns::summary( int shown ) const
{
    if ( runs_.size() < 2 ) {
        return {};
    }
    std::map<QString, int> states;
    for ( const auto& run : runs_ ) {
        ++states[ run.state ];
    }
    QStringList parts;
    for ( const auto& entry : states ) {
        parts << tr( "%1 %2" ).arg( entry.second ).arg( entry.first );
    }
    const auto label = shown >= 0 && shown < size() ? at( shown ).label : QString{};
    return tr( "%1 sources: %2. Showing %3." )
        .arg( size() )
        .arg( parts.join( QStringLiteral( ", " ) ) )
        .arg( label );
}

QVariantList AnalysisRuns::toVariantList() const
{
    QVariantList result;
    for ( const auto& run : runs_ ) {
        result.push_back( QVariantMap{ { QStringLiteral( "tabId" ), run.tabId },
                                       { QStringLiteral( "label" ), run.label },
                                       { QStringLiteral( "jobId" ), run.jobId },
                                       { QStringLiteral( "state" ), run.state } } );
    }
    return result;
}
