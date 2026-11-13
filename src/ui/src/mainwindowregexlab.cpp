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

void MainWindow::setApplicationSources(
    std::function<QVariantList()> sources,
    std::function<CommanderResult( const CommanderRequest& )> executor,
    std::function<MainWindow*( const QString& tabId )> windowForTab )
{
    applicationSources_ = std::move( sources );
    applicationExecutor_ = std::move( executor );
    windowForTab_ = std::move( windowForTab );
}

QVariantList MainWindow::analysisSources( int windowIndex, bool activeWindow ) const
{
    QVariantList sources;
    const auto* current = currentCrawlerWidget();
    for ( int index = 0; index < mainTabWidget_.count(); ++index ) {
        auto* crawler = crawlerWidgetByIndex( index );
        if ( !crawler )
            continue;
        sources.push_back( QVariantMap{ { "tabId", mainTabWidget_.tabIdAt( index ) },
                                        { "filePath", session_.getFilename( crawler ) },
                                        { "label", QString::number( index + 1 ) + ": "
                                                       + session_.getFilename( crawler ) },
                                        { "windowIndex", windowIndex },
                                        { "tabIndex", index },
                                        { "focused", activeWindow && crawler == current } } );
    }
    return sources;
}

bool MainWindow::hasTab( const QString& tabId ) const
{
    return !tabId.isEmpty() && crawlerWidgetByTabId( tabId ) != nullptr;
}

QVariantList MainWindow::dockAnalysisSources() const
{
    if ( !applicationSources_ )
        return analysisSources( static_cast<int>( session_.windowIndex() ), true );
    // "Focused tab" in this window's docks is this window's current tab, whichever window
    // was activated last.
    QString currentTabId;
    const auto* current = currentCrawlerWidget();
    for ( int index = 0; current && index < mainTabWidget_.count(); ++index ) {
        if ( crawlerWidgetByIndex( index ) == current )
            currentTabId = mainTabWidget_.tabIdAt( index );
    }
    auto sources = applicationSources_();
    for ( auto& value : sources ) {
        auto source = value.toMap();
        source[ "focused" ]
            = !currentTabId.isEmpty() && source.value( "tabId" ).toString() == currentTabId;
        value = source;
    }
    return sources;
}

CommanderResult MainWindow::executeDockRequest( const CommanderRequest& request )
{
    // A tab of another window is analyzed, searched or shown by that window.
    if ( !request.tabId.isEmpty() && applicationExecutor_
         && !crawlerWidgetByTabId( request.tabId ) ) {
        LOG_DEBUG << "Routing dock request " << commanderActionToString( request.action )
                  << " to the window of tab " << request.tabId;
        const auto result = applicationExecutor_( request );
        // Opening a value shows its line, so bring the window that shows it forward.
        if ( result.ok() && request.action == CommanderAction::OpenValueCount && windowForTab_ ) {
            if ( auto* owner = windowForTab_( request.tabId ); owner && owner != this ) {
                owner->raise();
                owner->activateWindow();
            }
        }
        return result;
    }
    return executeCommanderRequest( request );
}

CommanderResult MainWindow::navigateToAnalysisResult( const QString& id, quint64 physicalLine )
{
    auto* service = applicationAnalysisService( false );
    if ( !service )
        return labFailure( "job_expired", tr( "The analysis job has expired." ) );
    const auto status = service->status( id );
    if ( status.value( "scope" ) == "text" )
        return labFailure( "inline_text_location", tr( "Pasted text has no source tab." ) );
    const auto tabId = status.value( "sourceDescription" ).toMap().value( "tabId" ).toString();
    QPointer<CrawlerWidget> crawler = crawlerWidgetByTabId( tabId );
    if ( !crawler && windowForTab_ ) {
        if ( auto* owner = windowForTab_( tabId ); owner && owner != this ) {
            const auto result = owner->navigateToAnalysisResult( id, physicalLine );
            if ( result.ok() ) {
                owner->raise();
                owner->activateWindow();
            }
            return result;
        }
    }
    const auto position = service->location( id, physicalLine );
    if ( !position || !crawler )
        return labFailure( "stale_location", tr( "This source location has changed or closed." ) );
    mainTabWidget_.setCurrentWidget( crawler );
    if ( !crawler || !crawler->navigateToAnalysisResult( *position ) )
        return labFailure( "stale_location", tr( "This source location has changed or closed." ) );
    LOG_DEBUG << "Analysis result activated id=" << id << " source=" << position->sourceId()
              << " generation=" << position->generation() << " revision=" << position->revision()
              << " physicalLine=" << physicalLine;
    return commanderSuccess();
}

CommanderResult MainWindow::applyAnalysisSearch( const CommanderRequest& request )
{
    auto* crawler = request.tabId.isEmpty() ? currentCrawlerWidget()
                                            : crawlerWidgetByTabId( request.tabId );
    if ( !crawler && windowForTab_ ) {
        // The owner window applies it with that viewer's context, limit and refresh options.
        if ( auto* owner = windowForTab_( request.tabId ); owner && owner != this )
            return owner->applyAnalysisSearch( request );
    }
    if ( !crawler )
        return labFailure( "source_not_found", tr( "No current search tab is available." ) );
    return executeCommanderRequest( crawler->analysisSearchRequest( request ) );
}

void MainWindow::createRegexLab()
{
    regexLabDock_ = new RegexLabDock(
        this,
        [ this ]( const CommanderRequest& request ) {
            if ( request.action == CommanderAction::Search )
                return applyAnalysisSearch( request );
            return executeDockRequest( request );
        },
        [ this ] { return dockAnalysisSources(); },
        [ this ]( const QString& id, quint64 physicalLine ) {
            return navigateToAnalysisResult( id, physicalLine );
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
        // An explicit --tab-id / --tab-index wins over the tab of the shown results.
        if ( !target )
            return regexLabDock_->apply();
        mainTabWidget_.setCurrentWidget( target );
        return regexLabDock_->apply( mainTabWidget_.tabIdAt( mainTabWidget_.indexOf( target ) ) );
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
