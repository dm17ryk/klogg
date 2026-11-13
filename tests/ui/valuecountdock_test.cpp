// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysiscommands.h"
#include "analysistextedit.h"
#include "valuecountdock.h"
#include <QApplication>
#include <QClipboard>
#include <QMainWindow>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <catch2/catch.hpp>

TEST_CASE( "Value Counts dock uses shared jobs raw text and revision guards", "[ui][valuecounts]" )
{
    QMainWindow window;
    auto* service = applicationAnalysisService();
    CommanderRequest applied;
    int appliedCount = 0;
    auto* dock = new ValueCountDock(
        &window,
        [ & ]( const CommanderRequest& request ) {
            if ( request.action == CommanderAction::ApplyValueCount
                 || request.action == CommanderAction::OpenValueCount ) {
                applied = request;
                ++appliedCount;
                return commanderSuccess();
            }
            auto result
                = executeAnalysisOperation( *service, commanderActionToString( request.action ),
                                            request.analysisArguments, {}, ANALYSIS_WORKER_PATH );
            return CommanderResult{ result.ok ? CommanderResultCode::Success
                                              : CommanderResultCode::InvalidRequest,
                                    result.payload.value( "reason" ).toString(), result.payload };
        },
        [] { return QVariantList{}; } );
    window.addDockWidget( Qt::RightDockWidgetArea, dock );
    window.show();
    CHECK( dock->objectName() == "valueCountsDock" );
    CHECK( dock->features().testFlag( QDockWidget::DockWidgetFloatable ) );
    CHECK( dock->state().value( "draft" ).toMap().value( "scope" ) == "sample" );
    CHECK_FALSE( dock->findChild<QPushButton*>( "valueCountsApply" )->isEnabled() );
    const auto raw = QString::fromUtf8( "שלום 😀a.a\r\nsecond a.a\nlast b+b" );
    REQUIRE( dock->setDefinition( { { "pattern", "a\\.a|b\\+b" },
                                    { "scope", "text" },
                                    { "text", raw },
                                    { "operation", "run" } } )
                 .ok() );
    REQUIRE( QTest::qWaitFor(
        [ & ] { return dock->state().value( "job" ).toMap().value( "state" ) == "completed"; },
        15000 ) );
    const auto state = dock->state();
    REQUIRE( state.value( "job" ).toMap().value( "complete" ).toBool() );
    CHECK( state.value( "draft" ).toMap().value( "text" ) == raw );
    const auto rows = state.value( "page" ).toMap().value( "rows" ).toList();
    REQUIRE( rows.size() == 2 );
    CHECK( rows.front().toMap().value( "count" ) == "2" );
    auto* table = dock->findChild<QTableWidget*>( "valueCountsResults" );
    REQUIRE( table );
    CHECK( table->rowCount() == 2 );
    table->setCurrentCell( 0, 0 );
    auto* apply = dock->findChild<QPushButton*>( "valueCountsApply" );
    REQUIRE( apply );
    REQUIRE( apply->isEnabled() );
    table->clearSelection();
    CHECK( table->currentRow() == 0 );
    CHECK_FALSE( apply->isEnabled() );
    REQUIRE(
        QMetaObject::invokeMethod( apply, "clicked", Qt::DirectConnection, Q_ARG( bool, false ) ) );
    CHECK( appliedCount == 0 );
    table->selectRow( 0 );
    REQUIRE( apply->isEnabled() );
    apply->click();
    CHECK( appliedCount == 1 );
    CHECK( applied.action == CommanderAction::ApplyValueCount );
    CHECK( applied.analysisArguments.value( "jobId" ) == state.value( "jobId" ) );
    CHECK( applied.analysisArguments.value( "rowIndex" ).toInt() == 0 );
    REQUIRE( dock->setDefinition( { { "pattern", "changed" } } ).ok() );
    CHECK( dock->state().value( "draftChanged" ).toBool() );
    CHECK_FALSE( apply->isEnabled() );
    CHECK_FALSE( dock->setDefinition( { { "operation", "apply" }, { "rowIndex", 0 } } ).ok() );
    CHECK_FALSE( dock->setDefinition( { { "counts", QVariantMap{ { "maxValues", 0 } } } } ).ok() );
    CHECK_FALSE( dock->setDefinition( { { "operation", "guess" } } ).ok() );
    REQUIRE( dock->setDefinition( { { "floating", true } } ).ok() );
    CHECK( dock->isFloating() );
    REQUIRE( dock->setDefinition( { { "operation", "release" } } ).ok() );
    CHECK( dock->state().value( "jobId" ).toString().isEmpty() );
    CHECK( dock->state().value( "job" ).toMap().isEmpty() );
    CHECK_FALSE( dock->findChild<QPushButton*>( "valueCountsCancel" )->isEnabled() );
    CHECK_FALSE( dock->setDefinition( { { "rowIndex", QVariant{} } } ).ok() );
    REQUIRE( dock->setDefinition(
                     { { "pattern", "(a)" },
                       { "text", "a" },
                       { "counts", QVariantMap{ { "capture", "0" }, { "missing", "include" } } },
                       { "operation", "run" } } )
                 .ok() );
    REQUIRE( QTest::qWaitFor(
        [ & ] { return dock->state().value( "job" ).toMap().value( "state" ) == "completed"; },
        15000 ) );
    CHECK( dock->state()
               .value( "draft" )
               .toMap()
               .value( "counts" )
               .toMap()
               .value( "capture" )
               .userType()
           == QMetaType::QString );
    CHECK( dock->state()
               .value( "page" )
               .toMap()
               .value( "rows" )
               .toList()
               .front()
               .toMap()
               .value( "missing" )
               .toBool() );
}

TEST_CASE( "apply_targets_result_tab", "[workbench][U15][ui][valuecounts]" )
{
    QMainWindow window;
    auto* service = applicationAnalysisService();
    QStringList counted;
    CommanderRequest applied;
    auto* dock = new ValueCountDock(
        &window,
        [ & ]( const CommanderRequest& request ) {
            if ( request.action == CommanderAction::ApplyValueCount
                 || request.action == CommanderAction::OpenValueCount ) {
                applied = request;
                return commanderSuccess();
            }
            auto arguments = request.analysisArguments;
            if ( request.action == CommanderAction::ValueCounts ) {
                counted << request.tabId;
                arguments[ "scope" ] = "text";
                arguments[ "text" ] = "id=" + request.tabId + "\nid=" + request.tabId + "\n";
            }
            auto result
                = executeAnalysisOperation( *service, commanderActionToString( request.action ),
                                            arguments, {}, ANALYSIS_WORKER_PATH );
            return CommanderResult{ result.ok ? CommanderResultCode::Success
                                              : CommanderResultCode::InvalidRequest,
                                    result.payload.value( "reason" ).toString(), result.payload };
        },
        [] {
            return QVariantList{
                QVariantMap{ { "tabId", "t1" }, { "label", "1: a.log" }, { "filePath", "/a.log" } },
                QVariantMap{ { "tabId", "t2" }, { "label", "2: b.log" }, { "filePath", "/b.log" } } };
        } );
    window.addDockWidget( Qt::RightDockWidgetArea, dock );
    window.show();
    const auto completed = [ & ] {
        return QTest::qWaitFor(
            [ & ] { return dock->state().value( "job" ).toMap().value( "state" ) == "completed"; },
            15000 );
    };

    REQUIRE( dock->setDefinition( { { "pattern", "id=\\w+" },
                                    { "scope", "whole_file" },
                                    { "source", "all" },
                                    { "operation", "run" } } )
                 .ok() );
    REQUIRE( counted == QStringList{ "t1", "t2" } );
    REQUIRE( completed() );
    REQUIRE( dock->state().value( "runs" ).toList().size() == 2 );
    REQUIRE( dock->setDefinition( { { "resultSourceIndex", 1 } } ).ok() );
    REQUIRE( completed() );
    const auto rows = dock->state().value( "page" ).toMap().value( "rows" ).toList();
    REQUIRE( rows.size() == 1 );
    REQUIRE( rows.front().toMap().value( "value" ) == "id=t2" );
    REQUIRE( dock->setDefinition( { { "operation", "apply" }, { "rowIndex", 0 } } ).ok() );
    REQUIRE( applied.action == CommanderAction::ApplyValueCount );
    REQUIRE( applied.tabId == "t2" );
    REQUIRE( dock->setDefinition( { { "operation", "release" } } ).ok() );
    REQUIRE( dock->state().value( "runs" ).toList().isEmpty() );
}

namespace {
// A Value Counts dock whose tabs stand in for log files; `sources` can change between runs.
struct CountsHarness {
    QMainWindow window;
    QVariantList sources;
    QStringList counted;
    QStringList opened;
    CommanderRequest applied;
    ValueCountDock* dock = nullptr;

    CountsHarness()
    {
        auto* service = applicationAnalysisService();
        dock = new ValueCountDock(
            &window,
            [ this, service ]( const CommanderRequest& request ) {
                if ( request.action == CommanderAction::OpenFile ) {
                    opened << request.filePath;
                    return commanderSuccess();
                }
                if ( request.action == CommanderAction::ApplyValueCount
                     || request.action == CommanderAction::OpenValueCount ) {
                    applied = request;
                    return commanderSuccess();
                }
                auto arguments = request.analysisArguments;
                if ( request.action == CommanderAction::ValueCounts ) {
                    counted << request.tabId;
                    arguments[ "scope" ] = "text";
                    arguments[ "text" ] = "id=" + request.tabId + "\nid=" + request.tabId + "\n";
                }
                auto result = executeAnalysisOperation(
                    *service, commanderActionToString( request.action ), arguments, {},
                    ANALYSIS_WORKER_PATH );
                return CommanderResult{ result.ok ? CommanderResultCode::Success
                                                  : CommanderResultCode::InvalidRequest,
                                        result.payload.value( "reason" ).toString(),
                                        result.payload };
            },
            [ this ] { return sources; } );
        window.addDockWidget( Qt::RightDockWidgetArea, dock );
        window.show();
    }
    bool completed()
    {
        return QTest::qWaitFor(
            [ this ] {
                return dock->state().value( "job" ).toMap().value( "state" ) == "completed";
            },
            15000 );
    }
};

QVariantMap openTab( const QString& tabId, const QString& path )
{
    return QVariantMap{ { "tabId", tabId }, { "label", path }, { "filePath", path } };
}
} // namespace

TEST_CASE( "explicit_tab_selector_wins_for_apply", "[workbench][U15][ui][valuecounts]" )
{
    CountsHarness harness;
    harness.sources = { openTab( "t1", "/logs/a.log" ), openTab( "t2", "/logs/b.log" ) };
    REQUIRE( harness.dock
                 ->setDefinition( { { "pattern", "id=\\w+" },
                                    { "scope", "whole_file" },
                                    { "source", "tab:t2" },
                                    { "operation", "run" } } )
                 .ok() );
    REQUIRE( harness.completed() );
    // An explicit Commander tab (--tab-id / --tab-index) receives "apply"...
    REQUIRE(
        harness.dock->setDefinition( { { "operation", "apply" }, { "rowIndex", 0 } }, "t1" ).ok() );
    REQUIRE( harness.applied.tabId == "t1" );
    // ...otherwise the counted tab does, and Open always goes to the counted tab.
    REQUIRE( harness.dock->setDefinition( { { "operation", "apply" }, { "rowIndex", 0 } } ).ok() );
    REQUIRE( harness.applied.tabId == "t2" );
    REQUIRE(
        harness.dock->setDefinition( { { "operation", "open" }, { "rowIndex", 0 } }, "t1" ).ok() );
    REQUIRE( harness.applied.action == CommanderAction::OpenValueCount );
    REQUIRE( harness.applied.tabId == "t2" );
    REQUIRE( harness.dock->setDefinition( { { "resultSourceIndex", 0.5 } } )
                 .payload.value( "reason" )
             == "invalid_result_source" );
    REQUIRE( harness.dock->setDefinition( { { "operation", "release" } } ).ok() );
}

TEST_CASE( "closed_file_opens_tab_for_apply_and_open", "[workbench][U15][ui][valuecounts]" )
{
    CountsHarness harness;
    harness.sources = { openTab( "t1", "/logs/a.log" ) };
    // A file that is not open is opened in a tab first; nothing is counted yet.
    REQUIRE( harness.dock
                 ->setDefinition( { { "pattern", "id=\\w+" },
                                    { "scope", "whole_file" },
                                    { "source", "file:/logs/c.log" },
                                    { "operation", "run" } } )
                 .ok() );
    REQUIRE( harness.opened == QStringList{ "/logs/c.log" } );
    REQUIRE( harness.counted.isEmpty() );

    // Once its tab is open, Run counts that tab and Apply/Open target it.
    harness.sources << openTab( "t3", "/logs/c.log" );
    REQUIRE( harness.dock->setDefinition( { { "operation", "run" } } ).ok() );
    REQUIRE( harness.counted == QStringList{ "t3" } );
    REQUIRE( harness.completed() );
    REQUIRE( harness.dock->setDefinition( { { "operation", "apply" }, { "rowIndex", 0 } } ).ok() );
    REQUIRE( harness.applied.tabId == "t3" );
    REQUIRE( harness.dock->setDefinition( { { "operation", "open" }, { "rowIndex", 0 } } ).ok() );
    REQUIRE( harness.applied.tabId == "t3" );

    // When that tab closes, Apply and Open are refused and say why; the buttons are disabled.
    harness.sources.removeLast();
    const auto refused
        = harness.dock->setDefinition( { { "operation", "apply" }, { "rowIndex", 0 } } );
    REQUIRE( refused.payload.value( "reason" ) == "source_closed" );
    REQUIRE( refused.message.contains( "closed" ) );
    REQUIRE( harness.dock->setDefinition( { { "operation", "open" }, { "rowIndex", 0 } } )
                 .payload.value( "reason" )
             == "source_closed" );
    auto* open = harness.dock->findChild<QPushButton*>( "valueCountsOpen" );
    REQUIRE( open );
    REQUIRE( QTest::qWaitFor( [ & ] { return !open->isEnabled(); }, 3000 ) );
    REQUIRE( open->toolTip().contains( "closed" ) );
    REQUIRE( harness.dock->setDefinition( { { "operation", "release" } } ).ok() );
}
