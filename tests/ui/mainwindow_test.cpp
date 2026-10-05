/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <catch2/catch.hpp>

#include <QAction>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
#include <QTranslator>
#include <QVariantMap>
#include <QWidget>
#include <algorithm>
#include <iterator>
#include <mutex>

#include <QToolBar>

#include "test_utils.h"

#include "configuration.h"
#include "log.h"
#include "mainwindow.h"
#include "predefinedfilters.h"
#include "serialcaptureworker.h"
#include "session.h"
#include "sessioninfo.h"
#include "startupprogress.h"
#include "streamsession.h"
#include "updateuicontroller.h"
#include "versionchecker.h"

namespace {
struct SessionFilesRestoreGuard {
    SessionInfo& sessionInfo;
    QString windowId;
    std::vector<SessionInfo::OpenFile> openFiles;

    ~SessionFilesRestoreGuard()
    {
        sessionInfo.setOpenFiles( windowId, openFiles );
        sessionInfo.save();
    }
};

struct MinimizeToTrayRestoreGuard {
    bool previousValue = false;

    explicit MinimizeToTrayRestoreGuard( bool value )
        : previousValue( Configuration::get().minimizeToTray() )
    {
        Configuration::getSynced().setMinimizeToTray( value );
    }

    ~MinimizeToTrayRestoreGuard()
    {
        Configuration::getSynced().setMinimizeToTray( previousValue );
    }
};

struct StartupProgressCallbackGuard {
    ~StartupProgressCallbackGuard() { StartupProgress::clearCallback(); }
};

struct PredefinedFiltersRestoreGuard {
    PredefinedFiltersCollection::Collection filters
        = PredefinedFiltersCollection::getSynced().getFilters();

    ~PredefinedFiltersRestoreGuard()
    {
        PredefinedFiltersCollection::getSynced().saveToStorage( filters );
    }
};

class StartupProgressRecorder {
  public:
    void record( const StartupProgressState& state )
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        states_.push_back( state );
    }

    std::vector<StartupProgressState> snapshot() const
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        return states_;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<StartupProgressState> states_;
};

bool hasProgressStatus( const std::vector<StartupProgressState>& states, const QString& text )
{
    return std::any_of( states.cbegin(), states.cend(), [ &text ]( const auto& state ) {
        return state.status.contains( text, Qt::CaseInsensitive );
    } );
}

bool hasProgressDetail( const std::vector<StartupProgressState>& states, const QString& text )
{
    return std::any_of( states.cbegin(), states.cend(), [ &text ]( const auto& state ) {
        return state.detail.contains( text, Qt::CaseInsensitive );
    } );
}

int firstProgressStatusIndex( const std::vector<StartupProgressState>& states, const QString& text )
{
    const auto it = std::find_if( states.cbegin(), states.cend(), [ &text ]( const auto& state ) {
        return state.status.contains( text, Qt::CaseInsensitive );
    } );
    if ( it == states.cend() ) {
        return -1;
    }
    return static_cast<int>( std::distance( states.cbegin(), it ) );
}

bool hasVisibleTopLevelWindowWithTitle( const QString& titlePart )
{
    const auto widgets = QApplication::topLevelWidgets();
    return std::any_of( widgets.cbegin(), widgets.cend(), [ &titlePart ]( const QWidget* widget ) {
        return widget != nullptr && widget->isVisible()
               && widget->windowTitle().contains( titlePart, Qt::CaseInsensitive );
    } );
}

bool automationTreeContainsObjectName( const QVariantMap& node, const QString& objectName )
{
    if ( node.value( "objectName" ).toString() == objectName ) {
        return true;
    }

    const auto children = node.value( "children" ).toList();
    return std::any_of( children.cbegin(), children.cend(), [ &objectName ]( const auto& child ) {
        return automationTreeContainsObjectName( child.toMap(), objectName );
    } );
}
} // namespace

SCENARIO( "Main window tests", "[ui]" )
{
    auto appSession = std::make_shared<Session>();
    WindowSession windowSession{ appSession, "Main", 0 };

    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<SafeQSignalSpy> activateSpy;
    std::unique_ptr<SafeQSignalSpy> exitSpy;
    QTimer::singleShot( 0, [&] {
        LOG_INFO << "Initialize main window";
        mainWindow.reset( new MainWindow( windowSession ) );
        exitSpy.reset( new SafeQSignalSpy( mainWindow.get(), SIGNAL( exitRequested() ) ) );
        activateSpy.reset( new SafeQSignalSpy( mainWindow.get(), SIGNAL( windowActivated() ) ) );
    } );

    QTest::qWait( 100 );
    mainWindow->show();
    QTest::qWait( 100 );
    REQUIRE( activateSpy->safeWait() );

    auto runInUiThread = [uiObject = mainWindow.get()]( auto&& func ) {
        QTimer::singleShot( 0, Qt::VeryCoarseTimer, uiObject,
                            std::forward<decltype( func )>( func ) );
        QTest::qWait( 100 );
    };

    GIVEN( "Opened main window" )
    {
        auto toolBar = mainWindow->findChild<QToolBar*>();
        REQUIRE( toolBar != nullptr );

        auto filePathLabel = toolBar->findChild<PathLine*>();
        REQUIRE( filePathLabel != nullptr );

        auto tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabArea != nullptr );

        auto fileMenu = mainWindow->findChild<QMenu*>( QStringLiteral( "fileMenu" ) );
        REQUIRE( fileMenu != nullptr );

        auto comPortsMenu = mainWindow->findChild<QMenu*>( QStringLiteral( "comPortsMenu" ) );
        REQUIRE( comPortsMenu != nullptr );
        REQUIRE( comPortsMenu->accessibleName() == QStringLiteral( "COM port" ) );

        THEN( "Has no tabs" )
        {
            REQUIRE( tabArea->count() == 0 );
            REQUIRE( QMetaObject::invokeMethod( fileMenu, "aboutToShow", Qt::DirectConnection ) );
            REQUIRE_FALSE( comPortsMenu->menuAction()->isVisible() );
            AND_THEN( "Path label empty" )
            {
                REQUIRE( filePathLabel->text().isEmpty() );
            }
        }

        WHEN( "Exit hotkey pressed" )
        {
            runInUiThread( [&mainWindow] {
                LOG_INFO << "ExitFromMainMenu";
                QTest::keyPress( mainWindow.get(), Qt::Key_Q, Qt::ControlModifier );
            } );

            THEN( "Exit signalled" )
            {
                REQUIRE( exitSpy->safeWait() );
            }
        }

        WHEN( "Load file" )
        {
            runInUiThread( [&mainWindow] {
                LOG_INFO << "Load file";
                mainWindow->loadInitialFile( "klogg.conf", false );
            } );

            THEN( "Path line has file name" )
            {
                REQUIRE(
                    waitUiState( [&] { return filePathLabel->text().contains( "klogg.conf" ); } ) );

                AND_THEN( "Has one tab" )
                {
                    REQUIRE( waitUiState( [&] { return tabArea->count() == 1; } ) );
                }
            }

            AND_WHEN( "Close tab hotkey pressed" )
            {
                runInUiThread( [&mainWindow] {
                    LOG_INFO << "Close tab";
                    QTest::keyPress( mainWindow.get(), Qt::Key_W, Qt::ControlModifier );
                } );

                THEN( "Has no tabs" )
                {
                    REQUIRE( waitUiState( [&] { return tabArea->count() == 0; } ) );

                    AND_THEN( "Path label empty" )
                    {
                        REQUIRE( waitUiState( [&] { return filePathLabel->text().isEmpty(); } ) );
                    }
                }
            }
        }
    }
}

SCENARIO( "Closing main window closes auxiliary windows", "[ui]" )
{
    MinimizeToTrayRestoreGuard minimizeToTrayGuard{ false };

    auto appSession = std::make_shared<Session>();
    WindowSession windowSession{ appSession, "Main", 0 };

    std::unique_ptr<MainWindow> mainWindow{ new MainWindow( windowSession ) };
    mainWindow->show();

    REQUIRE( QMetaObject::invokeMethod( mainWindow.get(), "showScratchPad" ) );
    REQUIRE( QMetaObject::invokeMethod( mainWindow.get(), "showPreviewer" ) );
    REQUIRE( QMetaObject::invokeMethod( mainWindow.get(), "showActionsResponses" ) );
    REQUIRE( QMetaObject::invokeMethod( mainWindow.get(), "showResponses" ) );

    REQUIRE( waitUiState( [] { return hasVisibleTopLevelWindowWithTitle( "scratchpad" ); } ) );
    REQUIRE( waitUiState( [] { return hasVisibleTopLevelWindowWithTitle( "previewer" ); } ) );
    REQUIRE( waitUiState( [] { return hasVisibleTopLevelWindowWithTitle( " - actions" ); } ) );
    REQUIRE( waitUiState( [] { return hasVisibleTopLevelWindowWithTitle( " - responses" ); } ) );

    mainWindow->close();

    REQUIRE( waitUiState( [] { return !hasVisibleTopLevelWindowWithTitle( "scratchpad" ); } ) );
    REQUIRE( waitUiState( [] { return !hasVisibleTopLevelWindowWithTitle( "previewer" ); } ) );
    REQUIRE( waitUiState( [] { return !hasVisibleTopLevelWindowWithTitle( " - actions" ); } ) );
    REQUIRE( waitUiState( [] { return !hasVisibleTopLevelWindowWithTitle( " - responses" ); } ) );

}

SCENARIO( "Commander requests open and close files", "[ui][commander]" )
{
    auto appSession = std::make_shared<Session>();
    WindowSession windowSession{ appSession, "Commander", 0 };

    std::unique_ptr<MainWindow> mainWindow{ new MainWindow( windowSession ) };
    mainWindow->show();

    auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );

    QTemporaryFile file{ "mainwindow_commander_XXXXXX.log" };
    REQUIRE( file.open() );
    REQUIRE( file.write( "line one\nline two\n" ) > 0 );
    file.flush();

    CommanderRequest openRequest;
    openRequest.action = CommanderAction::OpenFile;
    openRequest.filePath = file.fileName();
    CommanderResult openResult{ CommanderResultCode::ExecutionFailed, {}, {} };

    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { openResult = mainWindow->executeCommanderRequest( openRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return openResult.ok(); } ) );
    REQUIRE( waitUiState( [&] { return tabArea->count() == 1; } ) );

    CommanderRequest getInfoRequest;
    getInfoRequest.action = CommanderAction::GetInfo;
    CommanderResult getInfoResult{ CommanderResultCode::ExecutionFailed, {}, {} };

    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { getInfoResult = mainWindow->executeCommanderRequest( getInfoRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return getInfoResult.ok() && getInfoResult.hasPayload(); } ) );

    const auto windowInfo = getInfoResult.payload;
    REQUIRE( windowInfo.value( "windowIndex" ).toInt() == 0 );
    REQUIRE_FALSE( windowInfo.value( "windowId" ).toString().isEmpty() );

    const auto tabs = windowInfo.value( "tabs" ).toList();
    REQUIRE( tabs.size() == 1 );
    const auto tabInfo = tabs.front().toMap();
    REQUIRE( tabInfo.value( "tabIndex" ).toInt() == 0 );
    REQUIRE_FALSE( tabInfo.value( "tabId" ).toString().isEmpty() );
    REQUIRE( tabInfo.value( "sourceType" ).toString() == "file" );
    REQUIRE( tabInfo.value( "filePath" ).toString() == file.fileName() );

    for ( const auto action : { CommanderAction::PlayComm, CommanderAction::PauseComm,
                                CommanderAction::StartNewCommFile } ) {
        CommanderRequest streamRequest;
        streamRequest.action = action;
        streamRequest.tabId = tabInfo.value( "tabId" ).toString();
        CommanderResult streamResult{ CommanderResultCode::Success, {}, {} };

        REQUIRE( QMetaObject::invokeMethod(
            mainWindow.get(),
            [ & ] { streamResult = mainWindow->executeCommanderRequest( streamRequest ); },
            Qt::QueuedConnection ) );
        REQUIRE( waitUiState( [&] { return !streamResult.ok(); } ) );
        REQUIRE( streamResult.code == CommanderResultCode::NotFound );
    }

    CommanderRequest closeByIdRequest;
    closeByIdRequest.action = CommanderAction::CloseTab;
    closeByIdRequest.tabId = tabInfo.value( "tabId" ).toString();
    CommanderResult closeByIdResult{ CommanderResultCode::ExecutionFailed, {}, {} };

    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { closeByIdResult = mainWindow->executeCommanderRequest( closeByIdRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return closeByIdResult.ok(); } ) );
    REQUIRE( waitUiState( [&] { return tabArea->count() == 0; } ) );

    CommanderResult reopenResult{ CommanderResultCode::ExecutionFailed, {}, {} };
    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { reopenResult = mainWindow->executeCommanderRequest( openRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return reopenResult.ok(); } ) );
    REQUIRE( waitUiState( [&] { return tabArea->count() == 1; } ) );

    CommanderRequest closeByIndexRequest;
    closeByIndexRequest.action = CommanderAction::CloseTab;
    closeByIndexRequest.tabIndex = 0;
    CommanderResult closeByIndexResult{ CommanderResultCode::ExecutionFailed, {}, {} };

    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { closeByIndexResult = mainWindow->executeCommanderRequest( closeByIndexRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return closeByIndexResult.ok(); } ) );
    REQUIRE( waitUiState( [&] { return tabArea->count() == 0; } ) );

    CommanderResult reopenForCloseResult{ CommanderResultCode::ExecutionFailed, {}, {} };
    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { reopenForCloseResult = mainWindow->executeCommanderRequest( openRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return reopenForCloseResult.ok(); } ) );
    REQUIRE( waitUiState( [&] { return tabArea->count() == 1; } ) );

    CommanderRequest closeRequest;
    closeRequest.action = CommanderAction::CloseFile;
    closeRequest.filePath = file.fileName();
    CommanderResult closeResult{ CommanderResultCode::ExecutionFailed, {}, {} };

    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { closeResult = mainWindow->executeCommanderRequest( closeRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return closeResult.ok(); } ) );
    REQUIRE( waitUiState( [&] { return tabArea->count() == 0; } ) );
}

SCENARIO( "Commander focuses tabs, reports filters, and closes all tabs", "[ui][commander]" )
{
    PredefinedFiltersRestoreGuard restoreFilters;
    const auto errorFilterId = createPredefinedFilterId();
    const auto warnFilterId = createPredefinedFilterId();
    PredefinedFiltersCollection::getSynced().saveToStorage(
        { { errorFilterId, "Errors", "ERROR", false },
          { warnFilterId, "Warnings", "WARN", false } } );

    auto appSession = std::make_shared<Session>();
    WindowSession windowSession{ appSession, "CommanderFilters", 0 };
    std::unique_ptr<MainWindow> mainWindow{ new MainWindow( windowSession ) };
    mainWindow->show();

    auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );

    QTemporaryFile firstFile{ "mainwindow_filters_first_XXXXXX.log" };
    QTemporaryFile secondFile{ "mainwindow_filters_second_XXXXXX.log" };
    REQUIRE( firstFile.open() );
    REQUIRE( secondFile.open() );
    REQUIRE( firstFile.write( "ERROR line\nWARN line\n" ) > 0 );
    REQUIRE( secondFile.write( "INFO line\n" ) > 0 );
    firstFile.flush();
    secondFile.flush();

    CommanderRequest openFirst;
    openFirst.action = CommanderAction::OpenFile;
    openFirst.filePath = firstFile.fileName();
    CommanderRequest openSecond = openFirst;
    openSecond.filePath = secondFile.fileName();

    CommanderResult openResult{ CommanderResultCode::ExecutionFailed, {}, {} };
    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(), [ & ] { openResult = mainWindow->executeCommanderRequest( openFirst ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return openResult.ok(); } ) );

    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(), [ & ] { openResult = mainWindow->executeCommanderRequest( openSecond ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return openResult.ok(); } ) );
    REQUIRE( waitUiState( [&] { return tabArea->count() == 2; } ) );

    CommanderRequest getInfoRequest;
    getInfoRequest.action = CommanderAction::GetInfo;
    CommanderResult getInfoResult{ CommanderResultCode::ExecutionFailed, {}, {} };
    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { getInfoResult = mainWindow->executeCommanderRequest( getInfoRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return getInfoResult.ok() && getInfoResult.hasPayload(); } ) );

    const auto tabs = getInfoResult.payload.value( "tabs" ).toList();
    REQUIRE( tabs.size() == 2 );
    const auto firstTabId = tabs.at( 0 ).toMap().value( "tabId" ).toString();
    const auto secondTabId = tabs.at( 1 ).toMap().value( "tabId" ).toString();
    REQUIRE_FALSE( firstTabId.isEmpty() );
    REQUIRE_FALSE( secondTabId.isEmpty() );

    CommanderRequest focusRequest;
    focusRequest.action = CommanderAction::FocusTab;
    focusRequest.tabId = firstTabId;
    CommanderResult focusResult{ CommanderResultCode::ExecutionFailed, {}, {} };
    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { focusResult = mainWindow->executeCommanderRequest( focusRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return focusResult.ok(); } ) );
    REQUIRE( waitUiState( [&] { return tabArea->currentIndex() == 0; } ) );

    CommanderRequest setFilterRequest;
    setFilterRequest.action = CommanderAction::SetFilter;
    setFilterRequest.tabId = firstTabId;
    setFilterRequest.filterId = errorFilterId;
    setFilterRequest.predefinedFilters = true;
    setFilterRequest.runSearch = true;
    CommanderResult setFilterResult{ CommanderResultCode::ExecutionFailed, {}, {} };
    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { setFilterResult = mainWindow->executeCommanderRequest( setFilterRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return setFilterResult.ok(); } ) );

    CommanderRequest getFiltersRequest;
    getFiltersRequest.action = CommanderAction::GetFilters;
    getFiltersRequest.tabId = firstTabId;
    CommanderResult getFiltersResult{ CommanderResultCode::ExecutionFailed, {}, {} };
    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { getFiltersResult = mainWindow->executeCommanderRequest( getFiltersRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return getFiltersResult.ok() && getFiltersResult.hasPayload(); } ) );

    const auto filters = getFiltersResult.payload.value( "filters" ).toList();
    REQUIRE( getFiltersResult.payload.value( "source" ).toString() == "history" );
    REQUIRE_FALSE( filters.isEmpty() );
    const auto selectedHistoryFilter
        = std::find_if( filters.cbegin(), filters.cend(), []( const auto& value ) {
              return value.toMap().value( "selected" ).toBool();
          } );
    REQUIRE( selectedHistoryFilter != filters.cend() );
    const auto selectedHistoryFilterString
        = selectedHistoryFilter->toMap().value( "filterString" ).toString();
    REQUIRE( selectedHistoryFilterString.contains( "ERROR" ) );

    CommanderRequest getPredefinedFiltersRequest;
    getPredefinedFiltersRequest.action = CommanderAction::GetFilters;
    getPredefinedFiltersRequest.tabId = firstTabId;
    getPredefinedFiltersRequest.predefinedFilters = true;
    CommanderResult getPredefinedFiltersResult{ CommanderResultCode::ExecutionFailed, {}, {} };
    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] {
            getPredefinedFiltersResult
                = mainWindow->executeCommanderRequest( getPredefinedFiltersRequest );
        },
        Qt::QueuedConnection ) );
    REQUIRE(
        waitUiState( [&] { return getPredefinedFiltersResult.ok() && getPredefinedFiltersResult.hasPayload(); } ) );
    REQUIRE( getPredefinedFiltersResult.payload.value( "source" ).toString() == "predefined" );
    REQUIRE( getPredefinedFiltersResult.payload.value( "filters" ).toList().size() == 2 );

    CommanderRequest closeAllRequest;
    closeAllRequest.action = CommanderAction::CloseAll;
    CommanderResult closeAllResult{ CommanderResultCode::ExecutionFailed, {}, {} };
    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { closeAllResult = mainWindow->executeCommanderRequest( closeAllRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return closeAllResult.ok(); } ) );
    REQUIRE( waitUiState( [&] { return tabArea->count() == 0; } ) );
}

SCENARIO( "Main window restores invalid session filter safely", "[ui][startup]" )
{
    QTemporaryFile file{ "mainwindow_restore_XXXXXX.log" };
    REQUIRE( file.open() );
    REQUIRE( file.write( "line one\nline two\n" ) > 0 );
    file.flush();

    auto& sessionInfo = SessionInfo::getSynced();
    sessionInfo.add( "Main" );

    SessionFilesRestoreGuard restoreGuard{ sessionInfo, "Main", sessionInfo.openFiles( "Main" ) };

    sessionInfo.setOpenFiles(
        "Main", { SessionInfo::OpenFile{
                    file.fileName(), 0,
                    "{\"S\":[400,100],\"IC\":false,\"AR\":true,\"FF\":false,\"RE\":true,\"IR\":false,\"BC\":false,\"SP\":\"((VOICE COMMAND)|(VOICE CMD)|(VPD Voice Commands)\"}" } } );
    sessionInfo.save();

    auto appSession = std::make_shared<Session>();
    WindowSession windowSession{ appSession, "Main", 0 };

    std::unique_ptr<MainWindow> mainWindow{ new MainWindow( windowSession ) };
    auto tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );

    StartupProgressRecorder startupRecorder;
    StartupProgressCallbackGuard callbackGuard;
    StartupProgress::setCallback(
        [ &startupRecorder ]( const StartupProgressState& state ) { startupRecorder.record( state ); } );
    mainWindow->reloadSession();
    mainWindow->show();

    REQUIRE( waitUiState( [ & ] { return tabArea->count() == 1; } ) );
    const auto startupStates = startupRecorder.snapshot();

    auto* crawler = qobject_cast<CrawlerWidget*>( tabArea->widget( 0 ) );
    REQUIRE( crawler != nullptr );
    REQUIRE_FALSE( crawler->isStartupPreparationPending() );
    REQUIRE( mainWindow->isStartupReadyForDisplay() );
    REQUIRE( hasProgressStatus( startupStates, "Restoring session" ) );
    REQUIRE( hasProgressStatus( startupStates, "Preparing tab" ) );
    REQUIRE( hasProgressStatus( startupStates, "Tab ready" ) );
    REQUIRE( hasProgressStatus( startupStates, "Session restored" ) );
    REQUIRE( hasProgressStatus( startupStates, "Restoring filter expression" ) );
    REQUIRE( hasProgressDetail( startupStates, "VOICE COMMAND" ) );
}

SCENARIO( "Main window startup waits for restored filter completion", "[ui][startup]" )
{
    QTemporaryFile file{ "mainwindow_restore_search_XXXXXX.log" };
    REQUIRE( file.open() );
    for ( int i = 0; i < 1000; ++i ) {
        REQUIRE( file.write( "alpha beta gamma\n" ) > 0 );
    }
    file.flush();

    auto& sessionInfo = SessionInfo::getSynced();
    sessionInfo.add( "Main" );
    SessionFilesRestoreGuard restoreGuard{ sessionInfo, "Main", sessionInfo.openFiles( "Main" ) };

    sessionInfo.setOpenFiles(
        "Main", { SessionInfo::OpenFile{
                    file.fileName(), 0,
                    "{\"S\":[400,100],\"IC\":false,\"AR\":true,\"FF\":false,\"RE\":true,\"IR\":false,\"BC\":false,\"SP\":\"alpha\"}" } } );
    sessionInfo.save();

    auto appSession = std::make_shared<Session>();
    WindowSession windowSession{ appSession, "Main", 0 };

    std::unique_ptr<MainWindow> mainWindow{ new MainWindow( windowSession ) };
    auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );

    StartupProgressRecorder startupRecorder;
    StartupProgressCallbackGuard callbackGuard;
    StartupProgress::setCallback(
        [ &startupRecorder ]( const StartupProgressState& state ) { startupRecorder.record( state ); } );

    mainWindow->reloadSession();
    mainWindow->show();

    REQUIRE( waitUiState( [ & ] { return tabArea->count() == 1; } ) );
    const auto startupStates = startupRecorder.snapshot();
    auto* crawler = qobject_cast<CrawlerWidget*>( tabArea->widget( 0 ) );
    REQUIRE( crawler != nullptr );
    REQUIRE_FALSE( crawler->isStartupPreparationPending() );
    REQUIRE( mainWindow->isStartupReadyForDisplay() );
    REQUIRE( hasProgressStatus( startupStates, "Compiling filter expression" ) );
    REQUIRE( hasProgressStatus( startupStates, "Filter ready" ) );
    REQUIRE( hasProgressStatus( startupStates, "Tab ready" ) );

    const auto compileIndex = firstProgressStatusIndex( startupStates, "Compiling filter expression" );
    const auto readyIndex = firstProgressStatusIndex( startupStates, "Filter ready" );
    const auto tabReadyIndex = firstProgressStatusIndex( startupStates, "Tab ready" );

    REQUIRE( compileIndex >= 0 );
    REQUIRE( readyIndex >= 0 );
    REQUIRE( tabReadyIndex >= 0 );
    REQUIRE( compileIndex <= readyIndex );
    REQUIRE( readyIndex <= tabReadyIndex );
}

SCENARIO( "Main window remains responsive after startup restore", "[ui][startup]" )
{
    QTemporaryFile file{ "mainwindow_restore_responsive_XXXXXX.log" };
    REQUIRE( file.open() );
    for ( int i = 0; i < 20000; ++i ) {
        REQUIRE( file.write( "alpha beta gamma delta epsilon\n" ) > 0 );
    }
    file.flush();

    auto& sessionInfo = SessionInfo::getSynced();
    sessionInfo.add( "Main" );
    SessionFilesRestoreGuard restoreGuard{ sessionInfo, "Main", sessionInfo.openFiles( "Main" ) };

    sessionInfo.setOpenFiles(
        "Main", { SessionInfo::OpenFile{
                    file.fileName(), 0,
                    "{\"S\":[400,100],\"IC\":false,\"AR\":true,\"FF\":false,\"RE\":true,\"IR\":false,\"BC\":false,\"SP\":\"alpha|epsilon\"}" } } );
    sessionInfo.save();

    auto appSession = std::make_shared<Session>();
    WindowSession windowSession{ appSession, "Main", 0 };

    std::unique_ptr<MainWindow> mainWindow{ new MainWindow( windowSession ) };
    auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );

    mainWindow->reloadSession();
    mainWindow->show();

    REQUIRE( waitUiState( [ & ] { return tabArea->count() == 1; } ) );
    REQUIRE( waitUiState( [ & ] { return mainWindow->isStartupReadyForDisplay(); } ) );

    int responsivenessTicks = 0;
    QTimer responsivenessTimer;
    responsivenessTimer.setInterval( 50 );
    QObject::connect( &responsivenessTimer, &QTimer::timeout,
                      [ &responsivenessTicks ]() { ++responsivenessTicks; } );
    responsivenessTimer.start();

    REQUIRE( waitUiState( [ & ] { return responsivenessTicks >= 15; } ) );
}

SCENARIO( "Main window startup reports initialization stages", "[ui][startup]" )
{
    auto appSession = std::make_shared<Session>();
    WindowSession windowSession{ appSession, "Main", 0 };

    StartupProgressRecorder startupRecorder;
    StartupProgressCallbackGuard callbackGuard;
    StartupProgress::setCallback(
        [ &startupRecorder ]( const StartupProgressState& state ) { startupRecorder.record( state ); } );
    std::unique_ptr<MainWindow> mainWindow{ new MainWindow( windowSession ) };

    const auto startupStates = startupRecorder.snapshot();
    REQUIRE( mainWindow != nullptr );
    REQUIRE( hasProgressStatus( startupStates, "Loading previews" ) );
    REQUIRE( hasProgressStatus( startupStates, "Loading actions" ) );
    REQUIRE( hasProgressStatus( startupStates, "Loading highlighters" ) );
    if ( hasProgressStatus( startupStates, "Loading highlighter set" ) ) {
        REQUIRE( hasProgressStatus( startupStates, "Compiling highlighter" ) );
    }
    REQUIRE( hasProgressStatus( startupStates, "Loading predefined filters" ) );
}

SCENARIO( "Main window restores session with missing and empty files safely", "[ui][startup]" )
{
    QTemporaryFile validFile{ "mainwindow_restore_valid_XXXXXX.log" };
    REQUIRE( validFile.open() );
    REQUIRE( validFile.write( "line one\nline two\n" ) > 0 );
    validFile.flush();

    const QString missingFilePath = validFile.fileName() + ".does_not_exist";
    QFile::remove( missingFilePath );

    auto& sessionInfo = SessionInfo::getSynced();
    sessionInfo.add( "Main" );
    SessionFilesRestoreGuard restoreGuard{ sessionInfo, "Main", sessionInfo.openFiles( "Main" ) };

    sessionInfo.setOpenFiles(
        "Main", { SessionInfo::OpenFile{ missingFilePath, 0, {} },
                  SessionInfo::OpenFile{ QString{}, 0, {} },
                  SessionInfo::OpenFile{ validFile.fileName(), 0, {} } } );
    sessionInfo.save();

    auto appSession = std::make_shared<Session>();
    WindowSession windowSession{ appSession, "Main", 0 };

    std::unique_ptr<MainWindow> mainWindow{ new MainWindow( windowSession ) };
    auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );

    mainWindow->reloadSession();
    mainWindow->show();

    REQUIRE( waitUiState( [ & ] { return tabArea->count() == 1; } ) );
    REQUIRE( tabArea->currentIndex() >= 0 );
    REQUIRE( waitUiState( [ & ] { return mainWindow->isStartupReadyForDisplay(); } ) );
}

TEST_CASE( "Restoring an unavailable COM port preserves a retryable capture tab",
           "[ui][startup][serialfailure]" )
{
    QTemporaryDir captureDir;
    REQUIRE( captureDir.isValid() );
    QTemporaryFile file{ captureDir.filePath( "mainwindow_restore_com_XXXXXX.log" ) };
    REQUIRE( file.open() );
    REQUIRE( file.write( "previous capture\n" ) > 0 );
    file.flush();

    SerialCaptureSettings settings;
    settings.portName = "CILOGG_TEST_MISSING_PORT";
    settings.filePath = file.fileName();
    auto& sessionInfo = SessionInfo::getSynced();
    sessionInfo.add( "Main" );
    SessionFilesRestoreGuard restoreGuard{ sessionInfo, "Main", sessionInfo.openFiles( "Main" ) };
    sessionInfo.setOpenFiles( "Main", { SessionInfo::OpenFile{
                                         file.fileName(), 0, {},
                                         serializeSerialCaptureSettings( settings ) } } );
    sessionInfo.save();

    auto appSession = std::make_shared<Session>();
    WindowSession windowSession{ appSession, "Main", 0 };
    MainWindow window( windowSession );
    auto* tabs = window.findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabs != nullptr );
    window.reloadSession();
    window.show();
    REQUIRE( waitUiState( [ & ] { return tabs->count() == 1; } ) );
    auto* crawler = qobject_cast<CrawlerWidget*>( tabs->widget( 0 ) );
    REQUIRE( crawler != nullptr );
    const auto restoredPath = windowSession.getFilename( crawler );
    REQUIRE( waitUiState( [ & ] {
        auto* stream = tabs->streamSessionForPath( restoredPath );
        return stream != nullptr && stream->isPaused() && stream->canResume();
    } ) );
    REQUIRE( tabs->count() == 1 );
    REQUIRE( window.isStartupReadyForDisplay() );

    auto* stream = tabs->streamSessionForPath( restoredPath );
    REQUIRE_FALSE( stream->isConnectionOpen() );
    REQUIRE( stream->captureSettings().portName == settings.portName );
    QString error;
    REQUIRE_FALSE( stream->resumeConnection( &error ) );
    REQUIRE_FALSE( error.isEmpty() );
    REQUIRE( stream->isPaused() );
    REQUIRE( stream->canResume() );
    QTemporaryDir projectDir;
    REQUIRE( projectDir.isValid() );
    REQUIRE( window.saveProject( projectDir.filePath( "paused.cilogproj" ), &error ) );
    const auto savedFiles = SessionInfo::getSynced().openFiles( "Main" );
    REQUIRE( savedFiles.size() == 1 );
    const auto savedStream = deserializeSerialCaptureSettings( savedFiles.front().streamContext );
    REQUIRE( savedStream.has_value() );
    REQUIRE( savedStream->portName == settings.portName );
    // Dismiss only this test window's restore notification.
    const auto children = window.findChildren<QMessageBox*>();
    for ( auto* message : children ) {
        message->close();
    }
}

SCENARIO( "Main window skips fully invalid session entries", "[ui][startup]" )
{
    const QString missingFilePath
        = QDir::temp().filePath( "klogg_missing_session_file_for_test.log" );
    QFile::remove( missingFilePath );

    auto& sessionInfo = SessionInfo::getSynced();
    sessionInfo.add( "Main" );
    SessionFilesRestoreGuard restoreGuard{ sessionInfo, "Main", sessionInfo.openFiles( "Main" ) };

    sessionInfo.setOpenFiles(
        "Main", { SessionInfo::OpenFile{ QString{}, 0, {} },
                  SessionInfo::OpenFile{ missingFilePath, 0, {} } } );
    sessionInfo.save();

    auto appSession = std::make_shared<Session>();
    WindowSession windowSession{ appSession, "Main", 0 };

    std::unique_ptr<MainWindow> mainWindow{ new MainWindow( windowSession ) };
    auto* tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabArea != nullptr );

    mainWindow->reloadSession();
    mainWindow->show();

    REQUIRE( waitUiState( [ & ] { return tabArea->count() == 0; } ) );
    REQUIRE( tabArea->currentIndex() == -1 );
}

TEST_CASE( "Help update checks share one updater and respect the selected channel",
           "[ui][updater][network]" )
{
    struct UpdatePreferencesGuard {
        bool enabled = Configuration::get().versionCheckingEnabled();
        UpdateChannel channel = Configuration::get().updateChannel();
        ~UpdatePreferencesGuard()
        {
            auto& config = Configuration::get();
            config.setVersionCheckingEnabled( enabled );
            config.setUpdateChannel( channel );
        }
    } preferencesGuard;
    Configuration::get().setVersionCheckingEnabled( false );
    Configuration::get().setUpdateChannel( UpdateChannel::Ci );

    QTcpServer server;
    REQUIRE( server.listen( QHostAddress::LocalHost ) );
    int requests = 0;
    const QByteArray body = R"json([
      {"tag_name":"v999.0.0","draft":false,"prerelease":false,
       "published_at":"2026-10-03T12:00:00Z",
       "html_url":"https://github.com/dm17ryk/klogg/releases/tag/v999.0.0"},
      {"tag_name":"continuous-999.0.1","draft":false,"prerelease":true,
       "published_at":"2026-10-04T12:00:00Z",
       "html_url":"https://github.com/dm17ryk/klogg/releases/tag/continuous-999.0.1"}
    ])json";
    const QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                + QByteArray::number( body.size() )
                                + "\r\nConnection: close\r\n\r\n" + body;
    QObject::connect( &server, &QTcpServer::newConnection, &server, [ & ] {
        auto* socket = server.nextPendingConnection();
        const auto sendResponse = [ &, socket ] {
            ++requests;
            socket->readAll();
            socket->write( response );
            socket->disconnectFromHost();
        };
        QObject::connect( socket, &QTcpSocket::readyRead, socket, sendResponse );
        if ( socket->bytesAvailable() > 0 ) {
            sendResponse();
        }
    } );
    VersionChecker checker(
        QUrl( QStringLiteral( "http://127.0.0.1:%1/releases" ).arg( server.serverPort() ) ) );
    const auto deadline = VersionCheckerConfig::getSynced().nextDeadline();
    QSignalSpy completed( &checker, &VersionChecker::checkFinished );
    QSignalSpy releaseFound( &checker, &VersionChecker::releaseFound );

    auto appSession = std::make_shared<Session>();
    MainWindow first( WindowSession{ appSession, "Updates", 0 } );
    MainWindow second( WindowSession{ appSession, "Updates", 1 } );
    first.setVersionChecker( checker );
    second.setVersionChecker( checker );
    auto* firstAction = first.findChild<QAction*>( "checkForUpdatesAction" );
    auto* secondAction = second.findChild<QAction*>( "checkForUpdatesAction" );
    REQUIRE( firstAction != nullptr );
    REQUIRE( secondAction != nullptr );
    REQUIRE( firstAction->isEnabled() );
    REQUIRE( secondAction->isEnabled() );
    REQUIRE( first.findChildren<VersionChecker*>().isEmpty() );
    REQUIRE( second.findChildren<VersionChecker*>().isEmpty() );
    firstAction->trigger();
    REQUIRE_FALSE( firstAction->isEnabled() );
    REQUIRE_FALSE( secondAction->isEnabled() );
    secondAction->trigger();
    REQUIRE( completed.wait( 3000 ) );
    REQUIRE( requests == 1 );
    REQUIRE( completed.at( 0 ).at( 0 ).toBool() );
    REQUIRE( releaseFound.size() == 1 );
    REQUIRE( qvariant_cast<ReleaseInfo>( releaseFound.at( 0 ).at( 0 ) ).prerelease );
    REQUIRE( firstAction->isEnabled() );
    REQUIRE( secondAction->isEnabled() );
    REQUIRE( VersionCheckerConfig::getSynced().nextDeadline() == deadline );
}

TEST_CASE( "Help and runner actions refresh every description after a language change",
           "[ui][translation]" )
{
    auto appSession = std::make_shared<Session>();
    MainWindow window( WindowSession{ appSession, "Translations", 0 } );
    struct ActionText {
        const char* name;
        const char* text;
        const char* statusTip;
        const char* toolTip;
    };
    const auto expectedActions
        = { ActionText{ "checkForUpdatesAction", "Check for Updates…",
                        "Check the selected update channel for a new version",
                        "Check for Updates…" },
            ActionText{ "showScriptRunnerAction", "Script Runner",
                        "Run Python automation on a tab or globally",
                        "Script Runner: tab and global Python automation" },
            ActionText{ "showScenarioRunnerAction", "Scenario Runner",
                        "Run Python test scenarios and suites with JSON/JUnit reports",
                        "Scenario Runner: test scenarios, suites and reports" } };
    const auto verifyDescriptions = [ & ]( const QString& prefix ) {
        for ( const auto& expected : expectedActions ) {
            auto* action = window.findChild<QAction*>( expected.name );
            REQUIRE( action != nullptr );
            REQUIRE( action->text() == prefix + QString::fromUtf8( expected.text ) );
            REQUIRE( action->statusTip() == prefix + QString::fromUtf8( expected.statusTip ) );
            REQUIRE( action->toolTip() == prefix + QString::fromUtf8( expected.toolTip ) );
        }
    };
    verifyDescriptions( {} );

    {
        class ActionTranslator final : public QTranslator {
        public:
            bool isEmpty() const override
            {
                return false;
            }
            QString translate( const char* context, const char* sourceText, const char* = nullptr,
                               int = -1 ) const override
            {
                return qstrcmp( context, "klogg::mainwindow::action" ) == 0
                           ? QStringLiteral( "translated: " ) + QString::fromUtf8( sourceText )
                           : QString{};
            }
        } translator;
        struct TranslatorGuard {
            QTranslator* translator;
            ~TranslatorGuard()
            {
                QApplication::removeTranslator( translator );
            }
        } guard{ &translator };
        REQUIRE( QApplication::installTranslator( &translator ) );
        QEvent languageChange( QEvent::LanguageChange );
        QApplication::sendEvent( &window, &languageChange );
        verifyDescriptions( QStringLiteral( "translated: " ) );
    }
    QEvent languageChange( QEvent::LanguageChange );
    QApplication::sendEvent( &window, &languageChange );
    verifyDescriptions( {} );
}

TEST_CASE( "Help update failures show one warning through the shared update controller",
           "[ui][updater][network]" )
{
    QTcpServer server;
    REQUIRE( server.listen( QHostAddress::LocalHost ) );
    QObject::connect( &server, &QTcpServer::newConnection, &server, [ & ] {
        auto* socket = server.nextPendingConnection();
        const auto sendResponse = [ socket ] {
            socket->readAll();
            socket->write( "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n"
                           "Connection: close\r\n\r\n" );
            socket->disconnectFromHost();
        };
        QObject::connect( socket, &QTcpSocket::readyRead, socket, sendResponse );
        if ( socket->bytesAvailable() > 0 ) {
            sendResponse();
        }
    } );
    VersionChecker checker(
        QUrl( QStringLiteral( "http://127.0.0.1:%1/releases" ).arg( server.serverPort() ) ) );
    auto appSession = std::make_shared<Session>();
    MainWindow first( WindowSession{ appSession, "Update errors", 0 } );
    MainWindow second( WindowSession{ appSession, "Update errors", 1 } );
    UpdateUiController controller(
        checker, [ & ] { return &first; }, [] { return Configuration::get().updateAction(); },
        QStringLiteral( "background" ) );
    first.setVersionChecker( checker );
    second.setVersionChecker( checker );
    first.show();
    second.show();
    QSignalSpy completed( &checker, &VersionChecker::checkFinished );
    QSignalSpy errors( &checker, &VersionChecker::errorOccurred );

    int messageCount = 0;
    QString warningText;
    QMessageBox::Icon icon = QMessageBox::NoIcon;
    QTimer dismissMessages;
    dismissMessages.setInterval( 10 );
    QObject::connect( &dismissMessages, &QTimer::timeout, &dismissMessages, [ & ] {
        for ( auto* widget : QApplication::topLevelWidgets() ) {
            if ( auto* box = qobject_cast<QMessageBox*>( widget ); box && box->isVisible() ) {
                ++messageCount;
                warningText = box->text();
                icon = box->icon();
                box->accept();
            }
        }
    } );
    dismissMessages.start();
    auto* action = first.findChild<QAction*>( "checkForUpdatesAction" );
    REQUIRE( action != nullptr );
    action->trigger();
    REQUIRE( completed.wait( 3000 ) );
    REQUIRE( checker.state() == UpdateState::Error );
    REQUIRE_FALSE( completed.at( 0 ).at( 0 ).toBool() );
    REQUIRE( errors.size() == 1 );
    REQUIRE( messageCount == 1 );
    REQUIRE( icon == QMessageBox::Warning );
    REQUIRE( warningText == errors.at( 0 ).at( 0 ).toString() );
    REQUIRE_FALSE( warningText.isEmpty() );
    REQUIRE( action->isEnabled() );
}

TEST_CASE( "Main window exposes automation object names and UI tree", "[ui][automation]" )
{
    auto appSession = std::make_shared<Session>();
    WindowSession windowSession{ appSession, "Automation", 0 };

    std::unique_ptr<MainWindow> mainWindow{ new MainWindow( windowSession ) };
    mainWindow->show();

    REQUIRE( mainWindow->objectName() == "mainWindow" );
    REQUIRE( mainWindow->findChild<QMenu*>( "toolsMenu" ) != nullptr );
    REQUIRE( mainWindow->findChild<QMenu*>( "highlightersMenu" ) != nullptr );
    REQUIRE( mainWindow->findChild<QMenu*>( "favoritesMenu" ) != nullptr );
    REQUIRE( mainWindow->findChild<QMenu*>( "helpMenu" ) != nullptr );
    REQUIRE( mainWindow->findChild<QAction*>( "optionsAction" ) != nullptr );
    REQUIRE( mainWindow->findChild<QAction*>( "showDocumentationAction" ) != nullptr );
    REQUIRE( mainWindow->findChild<QAction*>( "generateDumpAction" ) != nullptr );
    REQUIRE( mainWindow->findChild<QAction*>( "showScratchPadAction" ) != nullptr );
    REQUIRE( mainWindow->findChild<QAction*>( "showPreviewerAction" ) != nullptr );
    REQUIRE( mainWindow->findChild<QAction*>( "showActionsResponsesAction" ) != nullptr );
    REQUIRE( mainWindow->findChild<QAction*>( "openClipboardAction" ) != nullptr );
    REQUIRE( mainWindow->findChild<QAction*>( "openUrlAction" ) != nullptr );
    REQUIRE( mainWindow->findChild<QAction*>( "textWrapAction" ) != nullptr );

    const auto uiTree = mainWindow->automationUiTree();
    const auto actions = uiTree.value( "actions" ).toList();
    const auto ciloggState = uiTree.value( "ciloggState" ).toMap();
    const auto windowInfo = uiTree.value( "windowInfo" ).toMap();
    REQUIRE( uiTree.value( "objectName" ).toString() == "mainWindow" );
    REQUIRE( uiTree.value( "schemaVersion" ).toInt() == 1 );
    REQUIRE( uiTree.contains( "windowTitle" ) );
    REQUIRE( windowInfo.value( "windowIndex" ).toInt() == 0 );
    REQUIRE( !actions.isEmpty() );
    REQUIRE( ciloggState.contains( "startupReady" ) );
    REQUIRE( ciloggState.contains( "activeTabTitle" ) );
    REQUIRE( ciloggState.contains( "visibleLineStart" ) );
    REQUIRE( ciloggState.contains( "followMode" ) );
    REQUIRE( ciloggState.contains( "textWrapEnabled" ) );
    REQUIRE( ciloggState.contains( "focusedViewObjectName" ) );
    REQUIRE( ciloggState.contains( "mainVisibleLineStart" ) );
    REQUIRE( ciloggState.contains( "mainVisibleLineEnd" ) );
    REQUIRE( ciloggState.contains( "filteredVisibleLineStart" ) );
    REQUIRE( ciloggState.contains( "filteredVisibleLineEnd" ) );
    REQUIRE( automationTreeContainsObjectName( uiTree, "toolsMenu" ) );
    REQUIRE( automationTreeContainsObjectName( uiTree, "helpMenu" ) );
    REQUIRE( automationTreeContainsObjectName( uiTree, "optionsAction" ) );
    REQUIRE( automationTreeContainsObjectName( uiTree, "showScratchPadAction" ) );
    REQUIRE( automationTreeContainsObjectName( uiTree, "followAction" ) );
    REQUIRE( automationTreeContainsObjectName( uiTree, "textWrapAction" ) );

    const auto followAction = std::find_if( actions.cbegin(), actions.cend(), []( const auto& actionValue ) {
        return actionValue.toMap().value( "objectName" ).toString() == "followAction";
    } );
    REQUIRE( followAction != actions.cend() );
    REQUIRE( followAction->toMap().value( "role" ).toString() == "action" );
    REQUIRE( uiTree.value( "role" ).toString() == "widget" );
    REQUIRE( uiTree.contains( "bounds" ) );
}
