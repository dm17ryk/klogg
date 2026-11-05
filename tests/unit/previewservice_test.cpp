// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewservice.h"
#include "previewtree.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QTreeWidgetItem>
#include <catch2/catch.hpp>
#include <memory>

TEST_CASE( "Line preview tooltips escape field contents and show limits", "[previewservice]" )
{
    const QVariantList columns{ "<img src=file:///secret>", "<b>value</b>", "", "0", "3" };
    const auto html = PreviewService::tooltipHtml(
        { { "state", "completed" },
          { "available", true },
          { "previewName", "<demo>" },
          { "fields", QVariantList{ QVariantMap{ { "columns", columns }, { "depth", 0 } } } },
          { "limited", true } } );
    CHECK_FALSE( html.contains( "<img" ) );
    CHECK( html.contains( "&lt;img" ) );
    CHECK( html.contains( "&lt;b&gt;value&lt;/b&gt;" ) );
    CHECK( html.contains( "&lt;demo&gt;" ) );
    CHECK( html.contains( "limited", Qt::CaseInsensitive ) );
    CHECK( PreviewService::tooltipHtml( { { "available", false } } ).isEmpty() );
}

TEST_CASE( "Preview evaluation rejects oversized text without launching a worker",
           "[previewservice]" )
{
    PreviewEvaluationRequest request;
    request.text = QString( 65537, QLatin1Char( 'x' ) );
    request.executable = "must-not-be-launched";
    const auto result = PreviewService::evaluateAndWait( request );
    CHECK( result.value( "state" ) == "failed" );
    CHECK( result.value( "reason" ) == "input_limit" );
}

TEST_CASE( "Preview child reuses the current JSON renderer without settings writes",
           "[previewservice]" )
{
    PreviewEvaluationRequest request;
    request.text = QString::fromUtf8( "HELLO שלום 😀" );
    request.timeoutMs = 10000;
    request.definitions = R"json({"version":1,"previews":[{
        "name":"Example","regex":"^(?<value>HELLO .*)$","format":"fields","fields":[
        {"name":"Message","source":"capture","capture":"value","type":"string","format":"string"}]}]})json";
    request.executable = QString::fromUtf8( PREVIEW_APP_PATH );
    REQUIRE( QFileInfo::exists( request.executable ) );
    const auto result = PreviewService::evaluateAndWait( request );
    INFO( result.value( "reason" ).toString().toStdString() );
    REQUIRE( result.value( "state" ) == "completed" );
    REQUIRE( result.value( "available" ).toBool() );
    CHECK( result.value( "backend" ) == "json_preview" );
    const auto fields = result.value( "fields" ).toList();
    REQUIRE( fields.size() == 1 );
    CHECK( fields.front().toMap().value( "columns" ).toList().at( 1 ).toString() == request.text );
}

TEST_CASE( "Preview supervision recovers after stalled, crashed and invalid workers",
           "[previewservice]" )
{
    const QMap<QString, QString> modes{ { "stall", "timeout" },
                                        { "crash", "worker_failure" },
                                        { "invalid", "worker_protocol_error" },
                                        { "large", "result_limit" } };
    for ( auto mode = modes.cbegin(); mode != modes.cend(); ++mode ) {
        INFO( mode.key().toStdString() );
        PreviewEvaluationRequest request;
        request.text = mode.key();
        request.definitions = R"({"version":1,"previews":[]})";
        request.executable = QString::fromUtf8( PREVIEW_FIXTURE_PATH );
        request.timeoutMs = mode.key() == "stall" ? 1000 : 5000;
        int heartbeats = 0;
        QTimer timer;
        QObject::connect( &timer, &QTimer::timeout, [ & ] { ++heartbeats; } );
        timer.start( 10 );
        const auto result = PreviewService::evaluateAndWait( request );
        INFO( result.value( "reason" ).toString().toStdString() );
        CHECK( result.value( "reason" ).toString() == mode.value() );
        if ( mode.key() == "stall" )
            CHECK( heartbeats > 1 );
        request.text = "ok";
        request.timeoutMs = 5000;
        CHECK( PreviewService::evaluateAndWait( request ).value( "state" ) == "completed" );
    }
}

TEST_CASE( "Preview cancellation reaps workers and releases concurrency slots", "[previewservice]" )
{
    PreviewEvaluationRequest request;
    request.text = "stall";
    request.definitions = R"({"version":1,"previews":[]})";
    request.executable = QString::fromUtf8( PREVIEW_FIXTURE_PATH );
    request.timeoutMs = 10000;
    QObject owner;
    QEventLoop loop;
    int completed = 0;
    QStringList reasons;
    const auto done = [ & ]( QVariantMap result ) {
        reasons << result.value( "reason" ).toString();
        if ( ++completed == 3 )
            loop.quit();
    };
    QPointer<QProcess> first = PreviewService::evaluate( &owner, request, done );
    QPointer<QProcess> second = PreviewService::evaluate( &owner, request, done );
    REQUIRE( first );
    REQUIRE( second );
    CHECK( PreviewService::evaluate( &owner, request, done ) == nullptr );
    QTimer::singleShot( 0, &owner, [ first, second ] {
        PreviewService::cancel( first );
        PreviewService::cancel( first );
        PreviewService::cancel( second );
    } );
    QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
    loop.exec();
    REQUIRE( completed == 3 );
    CHECK( reasons.count( "cancelled" ) == 2 );
    CHECK( reasons.count( "preview_busy" ) == 1 );
    CHECK( ( !first || first->state() == QProcess::NotRunning ) );
    CHECK( ( !second || second->state() == QProcess::NotRunning ) );
    request.text = "ok";
    CHECK( PreviewService::evaluateAndWait( request ).value( "state" ) == "completed" );
}

TEST_CASE( "A failed worker startup returns without leaving a nested wait", "[previewservice]" )
{
    PreviewEvaluationRequest request;
    request.text = "example";
    request.executable = QCoreApplication::applicationDirPath() + "/cilogg-no-such-preview-worker";
    const auto result = PreviewService::evaluateAndWait( request );
    CHECK( result.value( "state" ) == "failed" );
    CHECK( result.value( "reason" ) == "worker_start_failed" );
}

TEST_CASE( "Worker node budgets stop allocation before parsing another field", "[previewservice]" )
{
    PreviewDefinition definition;
    definition.name = "Budget";
    definition.compiled = QRegularExpression( "^(?<value>.*)$" );
    PreviewFieldSpec field;
    field.name = "Value";
    field.source = PreviewFieldSource::Capture;
    field.capture = { true, false, 0, "value" };
    field.type = PreviewBufferType::String;
    field.format = PreviewFormat::String;
    definition.fields << field;
    PreviewTreeBudget budget;
    budget.remaining = 0;
    QString error;
    std::unique_ptr<QTreeWidgetItem> tree(
        createLegacyPreviewTree( "X", definition, {}, &error, &budget ) );
    REQUIRE( tree );
    CHECK( tree->childCount() == 0 );
    CHECK( budget.limited );
}
