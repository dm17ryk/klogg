// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysiscommands.h"
#include "logger.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "regexlabdock.h"
#include "sessioninfo.h"
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <catch2/catch.hpp>

namespace {
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
void activateWindow( QWidget* window )
{
    QApplication::setActiveWindow( window );
}
QT_WARNING_POP

CommanderResult labCommand( MainWindow& window, CommanderAction action, QVariantMap definition = {},
                            const QString& tabId = {} )
{
    CommanderRequest request;
    request.action = action;
    request.definitionPayload = std::move( definition );
    request.tabId = tabId;
    return window.executeCommanderRequest( request );
}
QVariantMap finished( MainWindow& window )
{
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto state = labCommand( window, CommanderAction::GetRegexLab )
                                   .payload.value( "job" )
                                   .toMap()
                                   .value( "state" )
                                   .toString();
            return state == "completed" || state == "failed" || state == "cancelled";
        },
        15000 ) );
    return labCommand( window, CommanderAction::GetRegexLab ).payload;
}
} // namespace

TEST_CASE(
    "Main window Regex Lab shares commands, validates locations and retains historical output",
    "[ui][regexlab][regexlabwindow]" )
{
    auto session = std::make_shared<Session>();
    auto& settings = SessionInfo::getSynced();
    const QString id = "RegexLabIntegration";
    settings.add( id );
    settings.save();
    MainWindow window( WindowSession{ session, id, 0 } );
    window.show();
    activateWindow( &window );
    REQUIRE( window.findChildren<RegexLabDock*>().size() == 1 );
    auto* dock = window.findChild<RegexLabDock*>( "regexLabDock" );
    auto* action = window.findChild<QAction*>( "showRegexLabAction" );
    REQUIRE( dock );
    REQUIRE( action );
    CHECK_FALSE( action->icon().isNull() );
    bool inToolbar = false;
    for ( auto* toolbar : window.findChildren<QToolBar*>() )
        inToolbar = inToolbar || toolbar->actions().contains( action );
    CHECK( inToolbar );
    bool inMenu = false;
    for ( auto* menu : window.findChildren<QMenu*>() )
        inMenu = inMenu || menu->actions().contains( action );
    CHECK( inMenu );
    REQUIRE( labCommand( window, CommanderAction::ShowRegexLab ).ok() );
    CHECK( dock->isVisible() );
    CHECK( labCommand( window, CommanderAction::GetRegexLab )
               .payload.value( "draft" )
               .toMap()
               .value( "scope" )
           == "sample" );

    QTemporaryDir directory;
    const auto path = directory.filePath( "source.log" );
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( "row1\nrow2\nrow3\n" ) > 0 );
    }
    CommanderRequest open;
    open.action = CommanderAction::OpenFile;
    open.filePath = path;
    REQUIRE( window.executeCommanderRequest( open ).ok() );
    auto* crawler = window.findChild<CrawlerWidget*>();
    REQUIRE( crawler );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            QString error;
            const auto request = crawler->analysisRequest( "whole_file", &error );
            return request && request->snapshot.lineCount.get() == 3;
        },
        10000 ) );
    auto* contextAction = crawler->findChild<QAction*>( "analyzeInRegexLabAction" );
    REQUIRE( contextAction );
    contextAction->trigger();
    CHECK( dock->state().value( "draft" ).toMap().value( "scope" ) == "selection" );
    REQUIRE( labCommand( window, CommanderAction::SetRegexLab,
                         { { "pattern", "(?<n>\\d)" }, { "scope", "whole_file" } } )
                 .ok() );
    auto* run = dock->findChild<QPushButton*>( "regexLabRun" );
    REQUIRE( run );
    const auto otherPath = directory.filePath( "other.log" );
    {
        QFile other( otherPath );
        REQUIRE( other.open( QIODevice::WriteOnly ) );
        REQUIRE( other.write( "unrelated9\n" ) > 0 );
    }
    open.filePath = otherPath;
    REQUIRE( window.executeCommanderRequest( open ).ok() );
    auto* tabs = window.findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabs->currentIndex() == 1 );
    run->click();
    const auto result = finished( window );
    REQUIRE( result.value( "job" ).toMap().value( "complete" ).toBool() );
    CHECK( result.value( "page" ).toMap().value( "rows" ).toInt() == 3 );
    const auto tabId = window.findChild<TabbedCrawlerWidget*>()->tabIdAt( 0 );
    REQUIRE(
        labCommand( window, CommanderAction::SetRegexLab, { { "operation", "first_page" } }, tabId )
            .ok() );
    REQUIRE(
        labCommand( window, CommanderAction::SetRegexLab, { { "activateResultIndex", 1 } }, tabId )
            .ok() );
    auto* view = crawler->findChild<LogMainView*>();
    REQUIRE( view );
    CHECK( view->getSelectedText() == "row2" );
    CHECK( tabs->currentIndex() == 0 );
    const auto capture = qEnvironmentVariable( "CILOGG_REGEX_LAB_CAPTURE" );
    if ( !capture.isEmpty() ) {
        window.resize( 1280, 900 );
        QApplication::processEvents();
        CHECK( window.grab().save( capture ) );
    }
    CommanderRequest search;
    search.action = CommanderAction::Search;
    search.searchText = "row";
    search.beforeContext = 2;
    search.afterContext = 3;
    search.maxMatches = 4;
    REQUIRE( window.executeCommanderRequest( search ).ok() );
    REQUIRE( labCommand( window, CommanderAction::ApplyRegexLabPattern ).ok() );
    const auto preserved = crawler->analysisSearchRequest( CommanderRequest{} );
    CHECK( preserved.beforeContext == 2 );
    CHECK( preserved.afterContext == 3 );
    CHECK( preserved.maxMatches == 4 );
    const auto jobId = result.value( "jobId" ).toString();
    QString error;
    const auto source = crawler->analysisRequest( "whole_file", &error );
    REQUIRE( source );
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Append ) );
        REQUIRE( file.write( "row4\n" ) > 0 );
    }
    REQUIRE( QMetaObject::invokeMethod( source->source.get(), "fileChangedOnDisk",
                                        Qt::DirectConnection, Q_ARG( QString, path ) ) );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            return labCommand( window, CommanderAction::GetRegexLab )
                .payload.value( "job" )
                .toMap()
                .value( "newerDataAvailable" )
                .toBool();
        },
        10000 ) );
    CHECK( dock->state().value( "jobId" ) == jobId );
    CHECK( dock->state().value( "page" ).toMap().value( "rows" ).toInt() == 3 );
    CommanderRequest close;
    close.action = CommanderAction::CloseTab;
    close.tabIndex = 0;
    REQUIRE( window.executeCommanderRequest( close ).ok() );
    REQUIRE( QTest::qWaitFor(
        [ & ] { return dock->state().value( "job" ).toMap().value( "historical" ).toBool(); },
        10000 ) );
    CHECK( dock->state().value( "page" ).toMap().value( "rows" ).toInt() == 3 );
    CHECK_FALSE(
        labCommand( window, CommanderAction::SetRegexLab, { { "activateResultIndex", 0 } } ).ok() );
}

TEST_CASE( "Regex Lab validates tab ownership in multiple windows",
           "[ui][regexlab][regexlabwindow]" )
{
    auto session = std::make_shared<Session>();
    auto& settings = SessionInfo::getSynced();
    settings.add( "RegexLabA" );
    settings.add( "RegexLabB" );
    settings.save();
    MainWindow first( WindowSession{ session, "RegexLabA", 0 } );
    MainWindow second( WindowSession{ session, "RegexLabB", 1 } );
    QTemporaryDir directory;
    QFile file( directory.filePath( "second.log" ) );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( "a\nb\n" ) > 0 );
    file.close();
    CommanderRequest open;
    open.action = CommanderAction::OpenFile;
    open.filePath = file.fileName();
    REQUIRE( second.executeCommanderRequest( open ).ok() );
    auto* tabs = second.findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabs );
    REQUIRE( tabs->count() == 1 );
    const auto tab = tabs->tabIdAt( 0 );
    REQUIRE_FALSE( tab.isEmpty() );
    CommanderRequest request;
    request.tabId = tab;
    for ( const auto action :
          { CommanderAction::GetRegexLab, CommanderAction::ShowRegexLab,
            CommanderAction::SetRegexLab, CommanderAction::ApplyRegexLabPattern } ) {
        request.action = action;
        CHECK( first.executeCommanderRequest( request ).code == CommanderResultCode::NotFound );
    }
    request.action = CommanderAction::SetRegexLab;
    request.definitionPayload = { { "pattern", "second-only" } };
    REQUIRE( second.executeCommanderRequest( request ).ok() );
    CHECK( second.findChild<RegexLabDock*>()->state().value( "draft" ).toMap().value( "pattern" )
           == "second-only" );
    CHECK( first.findChild<RegexLabDock*>()
               ->state()
               .value( "draft" )
               .toMap()
               .value( "pattern" )
               .toString()
               .isEmpty() );
}

TEST_CASE( "Regex Lab layout persists without draft text", "[ui][regexlab][regexlabwindow]" )
{
    logging::enableLogging( true, logging::LogLevel::Info );
    CHECK_FALSE( logging::needLogging( QtDebugMsg ) );
    struct SessionRestore {
        SessionInfo previous = SessionInfo::getSynced();
        ~SessionRestore()
        {
            SessionInfo::get() = previous;
            SessionInfo::get().save();
        }
    } restore;
    auto session = std::make_shared<Session>();
    auto& settings = SessionInfo::getSynced();
    // Existing close semantics retain only the last window. Isolate this
    // session rather than inheriting other test windows from portable settings.
    settings = SessionInfo{};
    const QString id = "RegexLabLayout";
    settings.add( id );
    settings.save();
    {
        MainWindow window( WindowSession{ session, id, 0 } );
        window.show();
        REQUIRE( labCommand( window, CommanderAction::SetRegexLab,
                             { { "scope", "text" },
                               { "text", "runtime-only-secret" },
                               { "pattern", "runtime-pattern" },
                               { "floating", true },
                               { "visible", true } } )
                     .ok() );
        REQUIRE( window.close() );
    }
    const auto stored = SessionInfo::getSynced().dockState( id );
    REQUIRE_FALSE( stored.isEmpty() );
    CHECK_FALSE( stored.contains( "runtime-only-secret" ) );
    CHECK_FALSE( stored.contains( "runtime-pattern" ) );
    MainWindow restored( WindowSession{ session, id, 0 } );
    restored.show();
    auto* dock = restored.findChild<RegexLabDock*>();
    REQUIRE( dock );
    CHECK( dock->isFloating() );
    CHECK( dock->isVisible() );
    CHECK( dock->state().value( "draft" ).toMap().value( "pattern" ).toString().isEmpty() );
    CHECK( dock->findChild<QPlainTextEdit*>( "regexLabText" )->toPlainText().isEmpty() );
}
