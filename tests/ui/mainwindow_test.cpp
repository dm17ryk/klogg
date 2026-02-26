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

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>

#include <QToolBar>

#include "test_utils.h"

#include "log.h"
#include "mainwindow.h"
#include "session.h"
#include "sessioninfo.h"

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

    mainWindow->reloadSession();
    mainWindow->show();

    REQUIRE( waitUiState( [ & ] { return tabArea->count() == 1; } ) );
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
