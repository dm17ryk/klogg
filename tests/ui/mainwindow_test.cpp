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

#include <algorithm>
#include <iterator>
#include <mutex>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>
#include <QWidget>

#include <QToolBar>

#include "test_utils.h"

#include "configuration.h"
#include "log.h"
#include "mainwindow.h"
#include "session.h"
#include "sessioninfo.h"
#include "startupprogress.h"

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

        THEN( "Has no tabs" )
        {
            REQUIRE( tabArea->count() == 0 );
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

    REQUIRE( waitUiState( [] { return hasVisibleTopLevelWindowWithTitle( "scratchpad" ); } ) );
    REQUIRE( waitUiState( [] { return hasVisibleTopLevelWindowWithTitle( "previewer" ); } ) );
    REQUIRE(
        waitUiState( [] { return hasVisibleTopLevelWindowWithTitle( "actions/responses" ); } ) );

    mainWindow->close();

    REQUIRE( waitUiState( [] { return !hasVisibleTopLevelWindowWithTitle( "scratchpad" ); } ) );
    REQUIRE( waitUiState( [] { return !hasVisibleTopLevelWindowWithTitle( "previewer" ); } ) );
    REQUIRE(
        waitUiState( [] { return !hasVisibleTopLevelWindowWithTitle( "actions/responses" ); } ) );

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
    CommanderResult openResult{ CommanderResultCode::ExecutionFailed, {} };

    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { openResult = mainWindow->executeCommanderRequest( openRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return openResult.ok(); } ) );
    REQUIRE( waitUiState( [&] { return tabArea->count() == 1; } ) );

    CommanderRequest getInfoRequest;
    getInfoRequest.action = CommanderAction::GetInfo;
    CommanderResult getInfoResult{ CommanderResultCode::ExecutionFailed, {} };

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

    CommanderRequest closeByIdRequest;
    closeByIdRequest.action = CommanderAction::CloseTab;
    closeByIdRequest.tabId = tabInfo.value( "tabId" ).toString();
    CommanderResult closeByIdResult{ CommanderResultCode::ExecutionFailed, {} };

    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { closeByIdResult = mainWindow->executeCommanderRequest( closeByIdRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return closeByIdResult.ok(); } ) );
    REQUIRE( waitUiState( [&] { return tabArea->count() == 0; } ) );

    CommanderResult reopenResult{ CommanderResultCode::ExecutionFailed, {} };
    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { reopenResult = mainWindow->executeCommanderRequest( openRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return reopenResult.ok(); } ) );
    REQUIRE( waitUiState( [&] { return tabArea->count() == 1; } ) );

    CommanderRequest closeByIndexRequest;
    closeByIndexRequest.action = CommanderAction::CloseTab;
    closeByIndexRequest.tabIndex = 0;
    CommanderResult closeByIndexResult{ CommanderResultCode::ExecutionFailed, {} };

    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { closeByIndexResult = mainWindow->executeCommanderRequest( closeByIndexRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return closeByIndexResult.ok(); } ) );
    REQUIRE( waitUiState( [&] { return tabArea->count() == 0; } ) );

    CommanderResult reopenForCloseResult{ CommanderResultCode::ExecutionFailed, {} };
    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { reopenForCloseResult = mainWindow->executeCommanderRequest( openRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return reopenForCloseResult.ok(); } ) );
    REQUIRE( waitUiState( [&] { return tabArea->count() == 1; } ) );

    CommanderRequest closeRequest;
    closeRequest.action = CommanderAction::CloseFile;
    closeRequest.filePath = file.fileName();
    CommanderResult closeResult{ CommanderResultCode::ExecutionFailed, {} };

    REQUIRE( QMetaObject::invokeMethod(
        mainWindow.get(),
        [ & ] { closeResult = mainWindow->executeCommanderRequest( closeRequest ); },
        Qt::QueuedConnection ) );
    REQUIRE( waitUiState( [&] { return closeResult.ok(); } ) );
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
