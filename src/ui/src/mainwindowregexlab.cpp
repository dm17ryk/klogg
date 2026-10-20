// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysiscommands.h"
#include "log.h"
#include "mainwindow.h"
#include "regexlabdock.h"
#include <QAction>

namespace {
CommanderResult labFailure( const QString& reason, const QString& message )
{
    LOG_DEBUG << "Regex Lab window operation rejected reason=" << reason;
    return { CommanderResultCode::InvalidRequest, message, { { "reason", reason } } };
}
} // namespace

void MainWindow::createRegexLab()
{
    regexLabDock_ = new RegexLabDock(
        this,
        [ this ]( const CommanderRequest& input ) {
            auto request = input;
            if ( request.action == CommanderAction::Search ) {
                auto* crawler = currentCrawlerWidget();
                if ( !crawler )
                    return labFailure( "source_not_found",
                                       tr( "No current search tab is available." ) );
                request = crawler->analysisSearchRequest( request );
            }
            return executeCommanderRequest( request );
        },
        [ this ] {
            QVariantList sources;
            for ( int index = 0; index < mainTabWidget_.count(); ++index ) {
                auto* crawler = crawlerWidgetByIndex( index );
                if ( !crawler )
                    continue;
                sources.push_back(
                    QVariantMap{ { "tabId", mainTabWidget_.tabIdAt( index ) },
                                 { "label", QString::number( index + 1 ) + ": "
                                                + session_.getFilename( crawler ) } } );
            }
            return sources;
        },
        [ this ]( const QString& id, quint64 physicalLine ) {
            auto* service = applicationAnalysisService( false );
            if ( !service )
                return labFailure( "job_expired", tr( "The analysis job has expired." ) );
            const auto status = service->status( id );
            if ( status.value( "scope" ) == "text" )
                return labFailure( "inline_text_location", tr( "Pasted text has no source tab." ) );
            const auto position = service->location( id, physicalLine );
            QPointer<CrawlerWidget> crawler = crawlerWidgetByTabId(
                status.value( "sourceDescription" ).toMap().value( "tabId" ).toString() );
            if ( !position || !crawler )
                return labFailure( "stale_location",
                                   tr( "This source location has changed or closed." ) );
            mainTabWidget_.setCurrentWidget( crawler );
            if ( !crawler || !crawler->navigateToAnalysisResult( *position ) )
                return labFailure( "stale_location",
                                   tr( "This source location has changed or closed." ) );
            LOG_DEBUG << "Regex Lab activated result id=" << id
                      << " source=" << position->sourceId()
                      << " generation=" << position->generation()
                      << " revision=" << position->revision() << " physicalLine=" << physicalLine;
            return commanderSuccess();
        } );
    addDockWidget( Qt::RightDockWidgetArea, regexLabDock_ );
    regexLabDock_->hide();
}

CommanderResult MainWindow::commanderRegexLab( const CommanderRequest& request )
{
    QPointer<CrawlerWidget> target;
    if ( !request.tabId.isEmpty() || request.tabIndex ) {
        target = commanderTargetCrawler( request );
        if ( !target )
            return { CommanderResultCode::NotFound,
                     tr( "Requested tab was not found." ),
                     { { "reason", "source_not_found" } } };
    }
    LOG_DEBUG << "Regex Lab window command=" << commanderActionToString( request.action )
              << " window=" << session_.windowId() << " tab=" << request.tabId;
    switch ( request.action ) {
    case CommanderAction::ShowRegexLab:
        regexLabDock_->show();
        regexLabDock_->raise();
        break;
    case CommanderAction::GetRegexLab:
        break;
    case CommanderAction::SetRegexLab: {
        auto definition = request.definitionPayload;
        const bool configuresDraft
            = definition.contains( "pattern" ) || definition.contains( "scope" )
              || definition.contains( "text" ) || definition.contains( "options" )
              || definition.contains( "limits" ) || definition.value( "operation" ) == "run";
        if ( target && configuresDraft && !definition.contains( "sourceTabId" ) )
            definition[ "sourceTabId" ]
                = mainTabWidget_.tabIdAt( mainTabWidget_.indexOf( target ) );
        return regexLabDock_->setDefinition( definition );
    }
    case CommanderAction::ApplyRegexLabPattern:
        if ( target )
            mainTabWidget_.setCurrentWidget( target );
        return regexLabDock_->apply();
    default:
        return labFailure( "unsupported_operation", tr( "Unsupported Regex Lab operation." ) );
    }
    return commanderSuccess( {}, regexLabDock_->state() );
}

void MainWindow::showRegexLabForSelection()
{
    auto* crawler = currentCrawlerWidget();
    if ( !crawler )
        return;
    regexLabDock_->prepareSelection( mainTabWidget_.tabIdAt( mainTabWidget_.indexOf( crawler ) ) );
}
