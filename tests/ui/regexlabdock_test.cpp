// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysiscommands.h"
#include "regexlabdock.h"
#include <QApplication>
#include <QClipboard>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QTextCursor>
#include <QThread>
#include <QTimer>
#include <catch2/catch.hpp>

namespace {
RegexLabDock* createLab( QMainWindow& window, CommanderRequest* applied = nullptr )
{
    auto* service = applicationAnalysisService();
    auto* dock = new RegexLabDock(
        &window,
        [ service, applied ]( const CommanderRequest& request ) {
            if ( request.action == CommanderAction::Search ) {
                if ( applied )
                    *applied = request;
                return commanderSuccess();
            }
            auto result
                = executeAnalysisOperation( *service, commanderActionToString( request.action ),
                                            request.analysisArguments, {}, ANALYSIS_WORKER_PATH );
            return CommanderResult{ result.ok ? CommanderResultCode::Success
                                              : CommanderResultCode::InvalidRequest,
                                    result.payload.value( "reason" ).toString(), result.payload };
        },
        [] { return QVariantList{}; },
        []( const QString&, quint64 ) {
            return CommanderResult{ CommanderResultCode::InvalidRequest,
                                    "historical_source",
                                    { { "reason", "historical_source" } } };
        } );
    window.addDockWidget( Qt::RightDockWidgetArea, dock );
    window.resize( 1000, 900 );
    window.show();
    return dock;
}
QVariantMap finishLab( RegexLabDock* dock )
{
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto state = dock->state().value( "job" ).toMap().value( "state" ).toString();
            return state == "completed" || state == "failed" || state == "cancelled";
        },
        15000 ) );
    return dock->state();
}
} // namespace

TEST_CASE( "Regex Lab shows bounded captures and applies only validated drafts", "[ui][regexlab]" )
{
    QMainWindow window;
    CommanderRequest applied;
    auto* dock = createLab( window, &applied );
    REQUIRE( dock->objectName() == "regexLabDock" );
    CHECK( dock->features().testFlag( QDockWidget::DockWidgetFloatable ) );
    CHECK( dock->features().testFlag( QDockWidget::DockWidgetMovable ) );
    CHECK( dock->state().value( "draft" ).toMap().value( "scope" ) == "sample" );
    CHECK_FALSE( dock->apply().ok() );
    REQUIRE( dock->setDefinition( { { "pattern", "(?<value>\\d)(x)?" },
                                    { "scope", "text" },
                                    { "text", QString::fromUtf8( "שלום 😀1\r\nsecond2x\nblank" ) },
                                    { "options", QVariantMap{ { "includeUnmatched", true } } },
                                    { "operation", "run" } } )
                 .ok() );
    const auto state = finishLab( dock );
    REQUIRE( state.value( "job" ).toMap().value( "complete" ).toBool() );
    CHECK( state.value( "job" ).toMap().value( "captureBackend" ) == "Qt QRegularExpression" );
    CHECK( state.value( "page" ).toMap().value( "rows" ).toInt() == 3 );
    auto* table = dock->findChild<QTableWidget*>( "regexLabResults" );
    REQUIRE( table );
    REQUIRE( table->rowCount() == 3 );
    table->setCurrentCell( 0, 0 );
    auto* details = dock->findChild<QPlainTextEdit*>( "regexLabCaptures" );
    REQUIRE( details );
    CHECK( details->toPlainText().contains( "\"value\": null" ) );
    CHECK( details->toPlainText().contains( "startUtf16" ) );
    CHECK( details->toPlainText().contains( "zero-based UTF-16" ) );
    REQUIRE( dock->apply().ok() );
    CHECK( applied.action == CommanderAction::Search );
    CHECK( applied.searchText == "(?<value>\\d)(x)?" );
    CHECK( applied.searchUseRegex );
    const auto id = state.value( "jobId" ).toString();
    REQUIRE( dock->setDefinition( { { "pattern", "changed" } } ).ok() );
    CHECK( dock->state().value( "draftChanged" ).toBool() );
    CHECK_FALSE( dock->apply().ok() );
    CHECK( dock->findChild<QLabel*>( "regexLabStatus" )->text().contains( "Draft changed" ) );
    applicationAnalysisService()->release( id );
    REQUIRE( QTest::qWaitFor(
        [ & ] { return dock->state().value( "job" ).toMap().value( "reason" ) == "job_expired"; },
        3000 ) );
    CHECK( table->rowCount() == 0 );
    CHECK( details->toPlainText().isEmpty() );
}

TEST_CASE( "Editing a pending Regex Lab draft rejects old deliveries", "[ui][regexlab]" )
{
    QMainWindow window;
    auto* dock = createLab( window );
    REQUIRE( dock->setDefinition( { { "pattern", "(hit)" },
                                    { "scope", "text" },
                                    { "text", QString( "hit\n" ).repeated( 300 ) },
                                    { "operation", "run" } } )
                 .ok() );
    const auto id = dock->state().value( "jobId" ).toString();
    const auto jobRevision = dock->state().value( "jobRevision" );
    REQUIRE( dock->setDefinition( { { "pattern", "next draft" } } ).ok() );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            return applicationAnalysisService()->status( id ).value( "state" ) == "completed";
        },
        15000 ) );
    const auto state = dock->state();
    CHECK( state.value( "draftChanged" ).toBool() );
    CHECK( state.value( "jobRevision" ) == jobRevision );
    CHECK( state.value( "page" ).toMap().value( "rows" ).toInt() == 0 );
    CHECK( state.value( "draft" ).toMap().value( "pattern" ) == "next draft" );
    CHECK_FALSE( dock->apply().ok() );
}

TEST_CASE( "Regex Lab exposes cancellation invalid patterns and visible result limits",
           "[ui][regexlab]" )
{
    QMainWindow window;
    auto* dock = createLab( window );
    REQUIRE( dock->setDefinition( { { "pattern", "[" },
                                    { "scope", "text" },
                                    { "text", "x" },
                                    { "operation", "run" } } )
                 .ok() );
    CHECK( finishLab( dock ).value( "job" ).toMap().value( "reason" ) == "invalid_pattern" );
    CHECK_FALSE( dock->findChild<QPushButton*>( "regexLabApply" )->isEnabled() );
    REQUIRE( dock->setDefinition( { { "pattern", "x" },
                                    { "text", "x\nx\nx" },
                                    { "limits", QVariantMap{ { "resultRecords", 1 } } },
                                    { "operation", "run" } } )
                 .ok() );
    const auto limited = finishLab( dock ).value( "job" ).toMap();
    CHECK_FALSE( limited.value( "complete" ).toBool() );
    CHECK( limited.value( "reason" ) == "result_limit" );
    CHECK( dock->findChild<QLabel*>( "regexLabStatus" )->text().contains( "Incomplete" ) );
    REQUIRE( dock->setDefinition( { { "operation", "run" } } ).ok() );
    auto* cancel = dock->findChild<QPushButton*>( "regexLabCancel" );
    REQUIRE( cancel->isEnabled() );
    QTest::mouseClick( cancel, Qt::LeftButton );
    CHECK( finishLab( dock ).value( "job" ).toMap().value( "state" ) == "cancelled" );
}

TEST_CASE( "Regex Lab definition validation is atomic and result paging stays bounded",
           "[ui][regexlab]" )
{
    QMainWindow window;
    auto* dock = createLab( window );
    REQUIRE( dock->setDefinition( { { "pattern", "before" } } ).ok() );
    CHECK_FALSE(
        dock->setDefinition( { { "pattern", "after" },
                               { "options", QVariantMap{ { "caseSensitive", "false" } } } } )
            .ok() );
    CHECK( dock->state().value( "draft" ).toMap().value( "pattern" ) == "before" );
    CHECK_FALSE(
        dock->setDefinition( { { "limits", QVariantMap{ { "timeoutMs", true } } } } ).ok() );
    CHECK_FALSE( dock->setDefinition( { { "scope", "all" } } ).ok() );
    REQUIRE( dock->setDefinition( { { "pattern", "hit" },
                                    { "scope", "text" },
                                    { "text", QString( "hit\n" ).repeated( 450 ) },
                                    { "operation", "run" } } )
                 .ok() );
    const auto state = finishLab( dock );
    CHECK( state.value( "page" ).toMap().value( "rows" ).toInt() == 200 );
    REQUIRE( dock->setDefinition( { { "operation", "next_page" } } ).ok() );
    const auto second = dock->state().value( "page" ).toMap();
    CHECK( second.value( "lineNumbers" ).toList().front().toInt() == 201 );
    CHECK( second.value( "rows" ).toInt() == 200 );
    REQUIRE( dock->setDefinition( { { "operation", "next_page" } } ).ok() );
    CHECK( dock->state().value( "page" ).toMap().value( "rows" ).toInt() == 50 );
    CHECK( dock->setDefinition( { { "activateResultIndex", 0 } } ).payload.value( "reason" )
           == "historical_source" );
    for ( const auto& invalid :
          QVariantList{ QVariant{}, QVariantMap{}, QVariantList{}, true, QString{ "0" }, -1, 0.5 } )
        CHECK(
            dock->setDefinition( { { "activateResultIndex", invalid } } ).payload.value( "reason" )
            == "invalid_result_index" );
    CHECK_FALSE(
        dock->setDefinition( { { "activateResultIndex", 0 }, { "operation", "run" } } ).ok() );
}

TEST_CASE( "Regex Lab caches terminal pages and bounds input method commits", "[ui][regexlab]" )
{
    QMainWindow window;
    auto* service = applicationAnalysisService();
    int pageRequests = 0;
    auto* dock = new RegexLabDock(
        &window,
        [ & ]( const CommanderRequest& request ) {
            if ( request.action == CommanderAction::GetAnalysisResults )
                ++pageRequests;
            const auto result
                = executeAnalysisOperation( *service, commanderActionToString( request.action ),
                                            request.analysisArguments, {}, ANALYSIS_WORKER_PATH );
            return CommanderResult{ result.ok ? CommanderResultCode::Success
                                              : CommanderResultCode::InvalidRequest,
                                    {},
                                    result.payload };
        },
        [] { return QVariantList{}; },
        []( const QString&, quint64 ) { return commanderSuccess(); } );
    window.addDockWidget( Qt::RightDockWidgetArea, dock );
    REQUIRE( dock->setDefinition( { { "scope", "text" },
                                    { "pattern", "x" },
                                    { "text", "x" },
                                    { "operation", "run" } } )
                 .ok() );
    finishLab( dock );
    const auto fetched = pageRequests;
    for ( int attempt = 0; attempt < 10; ++attempt )
        dock->state();
    CHECK( pageRequests == fetched );
    auto* editor = dock->findChild<QPlainTextEdit*>( "regexLabText" );
    REQUIRE( editor );
    QInputMethodEvent input;
    input.setCommitString( QString( 2 * 1024 * 1024, QLatin1Char( 'a' ) ) );
    QApplication::sendEvent( editor, &input );
    CHECK( dock->state().value( "draft" ).toMap().value( "text" ) == "x" );
    // Programmatic document insertion bypasses user input handlers; Run still
    // validates the exact reconstructed text before starting another worker.
    editor->insertPlainText( QString( 2 * 1024 * 1024, QLatin1Char( 'a' ) ) );
    CHECK( dock->setDefinition( { { "operation", "run" } } ).payload.value( "reason" )
           == "invalid_input" );
}

TEST_CASE( "Regex Lab clears an active continuation when unchanged results finish",
           "[ui][regexlab]" )
{
    AnalysisJobService service;
    std::atomic_bool finish{ false };
    QMainWindow window;
    auto* dock = new RegexLabDock(
        &window,
        [ & ]( const CommanderRequest& request ) {
            if ( request.action == CommanderAction::RegexEvaluate ) {
                AnalysisRequest analysis;
                analysis.scope = "text";
                analysis.text = "first\nsecond";
                analysis.limits.batchLines = 1;
                const auto id = service.start( analysis, [ & ] {
                    return [ & ]( const AnalysisBatch& batch, const std::atomic_bool& cancelled,
                                  int ) {
                        AnalysisOutput output;
                        if ( batch.lines.empty() )
                            return output;
                        if ( batch.lines.front().line == 0 ) {
                            output.rows.push_back(
                                QVariantMap{ { "lineNumber", 1 }, { "text", "first" } } );
                        }
                        else {
                            while ( !finish && !cancelled )
                                QThread::msleep( 1 );
                        }
                        return output;
                    };
                } );
                return commanderSuccess( {}, { { "jobId", id } } );
            }
            const auto result = executeAnalysisOperation(
                service, commanderActionToString( request.action ), request.analysisArguments );
            return CommanderResult{ result.ok ? CommanderResultCode::Success
                                              : CommanderResultCode::InvalidRequest,
                                    {},
                                    result.payload };
        },
        [] { return QVariantList{}; },
        []( const QString&, quint64 ) { return commanderSuccess(); } );
    window.addDockWidget( Qt::RightDockWidgetArea, dock );
    REQUIRE( dock->setDefinition( { { "operation", "run" } } ).ok() );
    REQUIRE( QTest::qWaitFor(
        [ & ] { return dock->state().value( "page" ).toMap().value( "rows" ).toInt() == 1; },
        10000 ) );
    CHECK_FALSE( dock->state().value( "page" ).toMap().value( "nextCursor" ).toString().isEmpty() );
    finish = true;
    const auto state = finishLab( dock );
    CHECK( state.value( "page" ).toMap().value( "rows" ).toInt() == 1 );
    CHECK( state.value( "page" ).toMap().value( "nextCursor" ).toString().isEmpty() );
}

TEST_CASE( "Regex Lab preserves imported Unicode and separator spelling through editing and undo",
           "[ui][regexlab]" )
{
    QMainWindow window;
    auto* dock = createLab( window );
    const auto text = QString::fromUtf8( "a\u00a0😀\r\nb\rc\u2029d\u2028e\nend" );
    REQUIRE( dock->setDefinition( { { "scope", "text" }, { "text", text } } ).ok() );
    const auto raw
        = [ & ] { return dock->state().value( "draft" ).toMap().value( "text" ).toString(); };
    CHECK( raw() == text );
    auto* editor = dock->findChild<QPlainTextEdit*>( "regexLabText" );
    REQUIRE( editor );
    auto cursor = editor->textCursor();
    cursor.movePosition( QTextCursor::End );
    editor->setTextCursor( cursor );
    editor->insertPlainText( "!" );
    CHECK( raw() == text + '!' );
    editor->undo();
    CHECK( raw() == text );
    struct ClipboardRestore {
        QMimeData* saved = new QMimeData;
        ClipboardRestore()
        {
            const auto* current = QApplication::clipboard()->mimeData();
            if ( current )
                for ( const auto& format : current->formats() )
                    saved->setData( format, current->data( format ) );
        }
        ~ClipboardRestore()
        {
            QApplication::clipboard()->setMimeData( saved );
        }
    } restore;
    const auto pasted = QString::fromUtf8( "p\r\nq\n\u00a0tail" );
    QApplication::clipboard()->setText( pasted );
    editor->selectAll();
    editor->paste();
    CHECK( raw() == pasted );
    editor->undo();
    CHECK( raw() == text );
    editor->redo();
    CHECK( raw() == pasted );
    cursor = editor->textCursor();
    cursor.setPosition( 1 );
    editor->setTextCursor( cursor );
    const auto middle = QString::fromUtf8( "x\r\nnew\rstuff\u2029" );
    QApplication::clipboard()->setText( middle );
    editor->paste();
    CHECK( raw() == pasted.left( 1 ) + middle + pasted.mid( 1 ) );
    editor->undo();
    CHECK( raw() == pasted );
}
