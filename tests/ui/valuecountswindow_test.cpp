// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysiscommands.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "sessioninfo.h"
#include "valuecountdock.h"
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <catch2/catch.hpp>

namespace {
CommanderResult countsCommand( MainWindow& window, CommanderAction action,
                               QVariantMap arguments = {}, const QString& tab = {} )
{
    CommanderRequest request;
    request.action = action;
    request.tabId = tab;
    request.definitionPayload = arguments;
    request.analysisArguments = std::move( arguments );
    return window.executeCommanderRequest( request );
}
} // namespace

TEST_CASE( "Value Counts window commands preserve literal filters and source ownership",
           "[ui][valuecounts][valuecountswindow]" )
{
    auto session = std::make_shared<Session>();
    auto& settings = SessionInfo::getSynced();
    settings.add( "ValueCountsFirst" );
    settings.add( "ValueCountsSecond" );
    settings.save();
    MainWindow first( WindowSession{ session, "ValueCountsFirst", 0 } );
    MainWindow second( WindowSession{ session, "ValueCountsSecond", 1 } );
    first.show();
    second.show();
    auto* dock = second.findChild<ValueCountDock*>( "valueCountsDock" );
    auto* action = second.findChild<QAction*>( "showValueCountsAction" );
    REQUIRE( dock );
    REQUIRE( action );
    CHECK_FALSE( action->icon().isNull() );
    bool inMenu = false, inToolbar = false;
    for ( auto* menu : second.findChildren<QMenu*>() )
        inMenu = inMenu || menu->actions().contains( action );
    for ( auto* toolbar : second.findChildren<QToolBar*>() )
        inToolbar = inToolbar || toolbar->actions().contains( action );
    CHECK( inMenu );
    CHECK( inToolbar );
    QTemporaryDir directory;
    const auto path = directory.filePath( "values.log" );
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( "a.a\naxb\na.a\nb+b\n" ) > 0 );
    }
    CommanderRequest open;
    open.action = CommanderAction::OpenFile;
    open.filePath = path;
    REQUIRE( second.executeCommanderRequest( open ).ok() );
    auto* crawler = second.findChild<CrawlerWidget*>();
    REQUIRE( crawler );
    REQUIRE( QTest::qWaitFor( [ & ] { return !crawler->isLoadingInProgress(); }, 10000 ) );
    const auto tab = second.findChild<TabbedCrawlerWidget*>()->tabIdAt( 0 );
    for ( auto operation : { CommanderAction::ShowValueCounts, CommanderAction::GetValueCounts,
                             CommanderAction::SetValueCounts } ) {
        CHECK( countsCommand( first, operation, {}, tab ).code == CommanderResultCode::NotFound );
        REQUIRE( countsCommand( second, operation, {}, tab ).ok() );
    }
    REQUIRE( countsCommand( second, CommanderAction::SetValueCounts,
                            { { "scope", "whole_file" }, { "pattern", "a\\.a|b\\+b" } }, tab )
                 .ok() );
    dock->findChild<QPushButton*>( "valueCountsRun" )->click();
    REQUIRE( QTest::qWaitFor(
        [ & ] { return dock->state().value( "job" ).toMap().value( "state" ) == "completed"; },
        15000 ) );
    const auto state = dock->state();
    REQUIRE( state.value( "job" ).toMap().value( "complete" ).toBool() );
    const auto rows = state.value( "page" ).toMap().value( "rows" ).toList();
    REQUIRE( rows.size() == 2 );
    CHECK( rows.front().toMap().value( "value" ) == "a.a" );
    CHECK( rows.front().toMap().value( "count" ) == "2" );
    const QVariantMap arguments{ { "jobId", state.value( "jobId" ) }, { "rowIndex", 0 } };
    CHECK( countsCommand( first, CommanderAction::ApplyValueCount, arguments, tab ).code
           == CommanderResultCode::NotFound );
    CHECK( countsCommand( first, CommanderAction::OpenValueCount, arguments, tab ).code
           == CommanderResultCode::NotFound );
    CommanderRequest search;
    search.action = CommanderAction::Search;
    search.searchText = "a";
    search.beforeContext = 2;
    search.afterContext = 3;
    search.maxMatches = 7;
    REQUIRE( second.executeCommanderRequest( search ).ok() );
    REQUIRE( countsCommand( second, CommanderAction::ApplyValueCount, arguments, tab ).ok() );
    const auto applied = crawler->analysisSearchRequest( CommanderRequest{} );
    CHECK( crawler->searchText() == "a\\.a" );
    CHECK( crawler->findChild<QToolButton*>( "useRegexpButton" )->isChecked() );
    CHECK_FALSE( crawler->isBooleanSearchEnabled() );
    CHECK( applied.beforeContext == 2 );
    CHECK( applied.afterContext == 3 );
    CHECK( applied.maxMatches == 7 );
    REQUIRE( countsCommand( second, CommanderAction::OpenValueCount, arguments, tab ).ok() );
    CHECK( crawler->findChild<LogMainView*>()->getSelectedText() == "a.a" );
    const auto screenshot = qEnvironmentVariable( "CILOGG_VALUE_COUNTS_CAPTURE" );
    if ( !screenshot.isEmpty() ) {
        second.resize( 1300, 1100 );
        QApplication::processEvents();
        CHECK( second.grab().save( screenshot ) );
    }
    auto* service = applicationAnalysisService();
    const auto id = state.value( "jobId" ).toString();
    QString error;
    const auto frozen = crawler->analysisRequest( "whole_file", &error );
    REQUIRE( frozen );
    service->cancelSource( frozen->source.get() );
    CHECK_FALSE( countsCommand( second, CommanderAction::OpenValueCount, arguments, tab ).ok() );
    CHECK( service->status( id ).value( "historical" ).toBool() );
    CHECK( service->results( id ).value( "rows" ).toList().size() == 2 );
}
