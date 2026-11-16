// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginsourceruns.h"

#include "analysissource.h"
#include "log.h"

#include <QVariantMap>

#include <algorithm>

namespace {
// Finished "all" Runs kept for runs(); older ones are forgotten.
constexpr size_t MaxKeptRuns = 8;
constexpr int AdvanceIntervalMs = 100;

CommanderResult sourceFailure( const QString& reason, const QString& message )
{
    LOG_DEBUG << "Plugin source rejected reason=" << reason;
    return { CommanderResultCode::InvalidRequest,
             message,
             { { QStringLiteral( "reason" ), reason } } };
}
} // namespace

PluginSourceRuns::PluginSourceRuns( Sources sources, Invoke invoke, JobStatus status,
                                    OpenFile openFile, QObject* parent )
    : QObject( parent )
    , sources_( std::move( sources ) )
    , invoke_( std::move( invoke ) )
    , status_( std::move( status ) )
    , openFile_( std::move( openFile ) )
{
    timer_.setInterval( AdvanceIntervalMs );
    connect( &timer_, &QTimer::timeout, this, &PluginSourceRuns::advance );
}

CommanderResult PluginSourceRuns::invoke( const CommanderRequest& request )
{
    if ( request.source.isEmpty() || hasCommanderTabSelector( request ) )
        return invoke_( request );
    QString error;
    const auto source = AnalysisSource::parse( request.source, &error );
    if ( !source )
        return sourceFailure( QStringLiteral( "invalid_source" ), error );
    auto single = request;
    single.source.clear();
    if ( source->kind == AnalysisSource::Kind::Focused )
        return invoke_( single ); // the current tab of the active (or given) window
    auto resolution = resolveAnalysisSource( *source, OpenSource::fromVariantList( sources_() ) );
    if ( !resolution.ok() )
        return sourceFailure( resolution.reason, resolution.message );
    if ( !resolution.fileToOpen.isEmpty() ) {
        const auto path = resolution.fileToOpen;
        const auto opened = openFile_ ? openFile_( path )
                                      : sourceFailure( QStringLiteral( "source_not_found" ),
                                                       tr( "The file is not open." ) );
        if ( !opened.ok() )
            return opened;
        resolution = resolveAnalysisSource( *source, OpenSource::fromVariantList( sources_() ) );
        if ( !resolution.ok() || resolution.tabIds.isEmpty() ) {
            LOG_DEBUG << "Plugin source file opened, its tab is not listed yet path=" << path;
            return commanderSuccess(
                tr( "Opened %1. Run the action again once it has loaded." ).arg( path ),
                { { QStringLiteral( "opened" ), path } } );
        }
    }
    single.windowIndex.reset();
    if ( source->kind != AnalysisSource::Kind::AllOpen ) {
        single.tabId = resolution.tabIds.front();
        return invoke_( single );
    }

    const auto id = ++nextRun_;
    auto& run = runs_[ id ];
    run.request = single;
    run.sources.reset( resolution.tabIds, sources_() );
    LOG_DEBUG << "Plugin action on all open tabs run=" << id << " plugin=" << single.pluginId
              << " action=" << single.pluginAction << " sources=" << resolution.tabIds.size();
    // A failure that is not queue_limit (an unknown action, a disabled plugin) fails the
    // request when no source could start, as it does for one tab.
    const auto first = startSources( run );
    if ( !first.ok() && run.sources.started() == 0 ) {
        LOG_DEBUG << "Plugin action on all open tabs failed to start run=" << id;
        runs_.erase( id );
        return first;
    }
    advance();
    const auto& started = runs_.at( id );
    return commanderSuccess( {},
                             { { QStringLiteral( "source" ), QStringLiteral( "all" ) },
                               { QStringLiteral( "runId" ), id },
                               { QStringLiteral( "sources" ), started.sources.toVariantList() },
                               { QStringLiteral( "summary" ), started.sources.summary( -1 ) } } );
}

void PluginSourceRuns::advance()
{
    const auto keep = []( const QString& ) { return commanderSuccess(); };
    bool active = false;
    for ( auto& [ id, run ] : runs_ ) {
        if ( !run.sources.active() )
            continue;
        run.sources.update( status_, keep, -1 );
        // Later failures are recorded as the state of their source.
        startSources( run );
        if ( run.sources.active() )
            active = true;
        else
            LOG_DEBUG << "Plugin action on all open tabs finished run=" << id;
    }
    // Forget the oldest finished Runs.
    std::vector<quint64> finished;
    for ( const auto& [ id, run ] : runs_ ) {
        if ( !run.sources.active() )
            finished.push_back( id );
    }
    for ( size_t index = 0; finished.size() > MaxKeptRuns + index; ++index )
        runs_.erase( finished[ index ] );
    if ( active && !timer_.isActive() )
        timer_.start();
    else if ( !active )
        timer_.stop();
}

CommanderResult PluginSourceRuns::startSources( Run& run )
{
    const auto request = run.request;
    return run.sources.startWaiting( [ this, request ]( const QString& tabId ) {
        auto one = request;
        one.tabId = tabId;
        return invoke_( one );
    } );
}

QVariantList PluginSourceRuns::runs() const
{
    QVariantList result;
    for ( const auto& [ id, run ] : runs_ ) {
        result.push_back(
            QVariantMap{ { QStringLiteral( "runId" ), id },
                         { QStringLiteral( "pluginId" ), run.request.pluginId },
                         { QStringLiteral( "action" ), run.request.pluginAction },
                         { QStringLiteral( "active" ), run.sources.active() },
                         { QStringLiteral( "sources" ), run.sources.toVariantList() } } );
    }
    return result;
}
