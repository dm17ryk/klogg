// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysiscommands.h"
#include "log.h"
#include "mainwindow.h"
#include "valuecountdock.h"
#include <QAction>

namespace {
CommanderResult countFailure( const QString& reason,
                              CommanderResultCode code = CommanderResultCode::InvalidRequest )
{
    LOG_DEBUG << "Value Counts window operation rejected reason=" << reason;
    return { code, reason, { { "reason", reason } } };
}
} // namespace

void MainWindow::createValueCounts()
{
    valueCountDock_ = new ValueCountDock(
        this,
        [ this ]( const CommanderRequest& request ) { return executeCommanderRequest( request ); },
        [ this ] {
            QVariantList sources;
            for ( int index = 0; index < mainTabWidget_.count(); ++index ) {
                auto* crawler = crawlerWidgetByIndex( index );
                if ( crawler )
                    sources.push_back(
                        QVariantMap{ { "tabId", mainTabWidget_.tabIdAt( index ) },
                                     { "label", QString::number( index + 1 ) + ": "
                                                    + session_.getFilename( crawler ) } } );
            }
            return sources;
        } );
    addDockWidget( Qt::RightDockWidgetArea, valueCountDock_ );
    valueCountDock_->hide();
}

CommanderResult MainWindow::commanderValueCounts( const CommanderRequest& request )
{
    QPointer<CrawlerWidget> target;
    if ( !request.tabId.isEmpty() || request.tabIndex ) {
        target = commanderTargetCrawler( request );
        if ( !target )
            return countFailure( "source_not_found", CommanderResultCode::NotFound );
    }
    LOG_DEBUG << "Value Counts window command=" << commanderActionToString( request.action )
              << " window=" << session_.windowId() << " tab=" << request.tabId;
    if ( request.action == CommanderAction::ShowValueCounts ) {
        valueCountDock_->show();
        valueCountDock_->raise();
    }
    else if ( request.action == CommanderAction::SetValueCounts ) {
        auto definition = request.definitionPayload;
        const bool draft = definition.contains( "pattern" ) || definition.contains( "scope" )
                           || definition.contains( "text" ) || definition.contains( "counts" )
                           || definition.contains( "options" ) || definition.contains( "limits" )
                           || definition.value( "operation" ) == "run";
        if ( target && draft && !definition.contains( "sourceTabId" ) )
            definition[ "sourceTabId" ]
                = mainTabWidget_.tabIdAt( mainTabWidget_.indexOf( target ) );
        return valueCountDock_->setDefinition( definition );
    }
    return commanderSuccess( {}, valueCountDock_->state() );
}

CommanderResult MainWindow::commanderValueCountResult( const CommanderRequest& request )
{
    if ( ( !request.tabId.isEmpty() || request.tabIndex ) && !commanderTargetCrawler( request ) )
        return countFailure( "source_not_found", CommanderResultCode::NotFound );
    auto* service = applicationAnalysisService( false );
    if ( !service )
        return countFailure( "job_expired" );
    const auto found
        = executeAnalysisOperation( *service, "get_value_count", request.analysisArguments );
    if ( !found.ok )
        return countFailure( found.payload.value( "reason" ).toString() );
    const auto row = found.payload.value( "row" ).toMap();
    const auto id = found.payload.value( "jobId" ).toString();
    if ( request.action == CommanderAction::ApplyValueCount ) {
        if ( row.value( "missing" ).toBool() )
            return countFailure( "missing_value_filter" );
        if ( row.value( "value" ).toString().isEmpty() )
            return countFailure( "empty_value_filter" );
        auto* crawler = commanderTargetCrawler( request );
        if ( !crawler )
            return countFailure( "source_not_found", CommanderResultCode::NotFound );
        CommanderRequest search;
        search.action = CommanderAction::Search;
        search.tabId = mainTabWidget_.tabIdAt( mainTabWidget_.indexOf( crawler ) );
        search.searchText = row.value( "filterPattern" ).toString();
        search.searchUseRegex = true;
        search.searchUseBoolean = false;
        search.searchInverseMatch = false;
        search.searchCaseSensitive = row.value( "filterCaseSensitive" ).toBool();
        search.matchMode = "contains";
        search = crawler->analysisSearchRequest( search );
        LOG_DEBUG << "Applying retained value count literal id=" << id
                  << " rowIndex=" << found.payload.value( "rowIndex" ).toInt()
                  << " tab=" << search.tabId;
        return executeCommanderRequest( search );
    }
    const auto state = service->status( id );
    const auto number = row.value( "lineNumber" ).toULongLong();
    const auto position = number ? service->location( id, number - 1 ) : std::nullopt;
    const auto tabId = state.value( "sourceDescription" ).toMap().value( "tabId" ).toString();
    QPointer<CrawlerWidget> crawler = crawlerWidgetByTabId( tabId );
    if ( !tabId.isEmpty() && !crawler )
        return countFailure( "source_not_found", CommanderResultCode::NotFound );
    if ( !position || !crawler )
        return countFailure( "stale_location" );
    mainTabWidget_.setCurrentWidget( crawler );
    if ( !crawler || !crawler->navigateToAnalysisResult( *position ) )
        return countFailure( "stale_location" );
    LOG_DEBUG << "Value count source activated id=" << id << " source=" << position->sourceId()
              << " generation=" << position->generation() << " physicalLine=" << number - 1;
    return commanderSuccess();
}
