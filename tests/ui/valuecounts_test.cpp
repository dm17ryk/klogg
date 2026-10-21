// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysiscommands.h"
#include "logdata.h"
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch.hpp>

namespace {
QVariantMap countFinished( AnalysisJobService& service, const QString& id )
{
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto state = service.status( id ).value( "state" ).toString();
            return state == "completed" || state == "failed" || state == "cancelled";
        },
        45000 ) );
    return service.status( id );
}
QString startCounts( AnalysisJobService& service, const QString& text, const QString& pattern,
                     const QVariantMap& counts = {}, const QVariantMap& limits = {} )
{
    const auto result = executeAnalysisOperation( service, "value_counts",
                                                  { { "scope", "text" },
                                                    { "text", text },
                                                    { "pattern", pattern },
                                                    { "counts", counts },
                                                    { "limits", limits } },
                                                  {}, ANALYSIS_WORKER_PATH );
    INFO( result.payload.value( "reason" ).toString().toStdString() );
    REQUIRE( result.ok );
    return result.payload.value( "jobId" ).toString();
}
QVariantList countRows( AnalysisJobService& service, const QString& id )
{
    return service.results( id ).value( "rows" ).toList();
}
} // namespace

TEST_CASE( "Exact value counts aggregate occurrences with deterministic ties and literal filters",
           "[logdata][valuecounts]" )
{
    AnalysisJobService service;
    const auto id = startCounts( service, "a.a b+b a.a\nb+b c", "[^\\s]+" );
    const auto status = countFinished( service, id );
    REQUIRE( status.value( "complete" ).toBool() );
    CHECK( status.value( "kind" ) == "value_counts" );
    CHECK( status.value( "aggregation" ).toMap().value( "countedOccurrences" ) == "5" );
    const auto rows = countRows( service, id );
    REQUIRE( rows.size() == 3 );
    CHECK( rows.at( 0 ).toMap().value( "value" ) == "a.a" );
    CHECK( rows.at( 1 ).toMap().value( "value" ) == "b+b" );
    CHECK( rows.at( 0 ).toMap().value( "count" ) == "2" );
    CHECK( rows.at( 0 ).toMap().value( "percentage" ).toDouble() == Approx( 40 ) );
    CHECK( rows.at( 2 ).toMap().value( "denominator" ) == "5" );
    CHECK( rows.at( 0 ).toMap().value( "filterPattern" ) == "a\\.a" );
    CHECK( rows.at( 1 ).toMap().value( "filterPattern" ) == "b\\+b" );
    const auto page = service.results( id, {}, 1 );
    const auto cursor = page.value( "nextCursor" ).toString();
    REQUIRE_FALSE( cursor.isEmpty() );
    CHECK(
        service.results( id, cursor, 1 ).value( "rows" ).toList().front().toMap().value( "value" )
        == "b+b" );
    CHECK( service.results( id, cursor, 1 ) == service.results( id, cursor, 1 ) );
}

TEST_CASE( "Value counts distinguish optional missing groups from matched empty strings",
           "[logdata][valuecounts]" )
{
    AnalysisJobService service;
    const auto pattern = QStringLiteral( "(?<value>a)?(?<empty>)x" );
    const auto id = startCounts( service, "ax x", pattern,
                                 { { "capture", "value" }, { "missing", "include" } } );
    REQUIRE( countFinished( service, id ).value( "complete" ).toBool() );
    const auto rows = countRows( service, id );
    REQUIRE( rows.size() == 2 );
    CHECK( rows.front().toMap().value( "value" ) == "a" );
    CHECK_FALSE( rows.front().toMap().value( "missing" ).toBool() );
    CHECK( rows.back().toMap().value( "missing" ).toBool() );
    CHECK( rows.back().toMap().value( "value" ).isNull() );
    CHECK( rows.front().toMap().value( "percentage" ).toDouble() == Approx( 50 ) );
    const auto empty = startCounts( service, "ax x", pattern, { { "capture", "empty" } } );
    REQUIRE( countFinished( service, empty ).value( "complete" ).toBool() );
    const auto emptyRows = countRows( service, empty );
    REQUIRE( emptyRows.size() == 1 );
    CHECK_FALSE( emptyRows.front().toMap().value( "missing" ).toBool() );
    CHECK( emptyRows.front().toMap().value( "value" ).toString().isEmpty() );
    CHECK( emptyRows.front().toMap().value( "count" ) == "2" );
    const auto skipped = startCounts( service, "ax x", pattern, { { "capture", 1 } } );
    const auto skippedStatus = countFinished( service, skipped );
    CHECK( skippedStatus.value( "aggregation" ).toMap().value( "skippedMissing" ) == "1" );
    CHECK( countRows( service, skipped ).front().toMap().value( "percentage" ).toDouble()
           == Approx( 100 ) );
}

TEST_CASE( "Value counts use Unicode case folding and preserve explicit limits",
           "[logdata][valuecounts]" )
{
    AnalysisJobService service;
    const auto folded = startCounts( service, QString::fromUtf8( "Ä ä שלום שלום" ), "[^\\s]+",
                                     { { "caseSensitive", false } } );
    REQUIRE( countFinished( service, folded ).value( "complete" ).toBool() );
    CHECK( countRows( service, folded ).front().toMap().value( "value" )
           == QString::fromUtf8( "ä" ) );
    CHECK( countRows( service, folded ).front().toMap().value( "count" ) == "2" );
    const auto capped = startCounts( service, "a a b a", "\\w+", { { "maxValues", 1 } } );
    const auto status = countFinished( service, capped );
    CHECK( status.value( "state" ) == "completed" );
    CHECK_FALSE( status.value( "complete" ).toBool() );
    CHECK( status.value( "reason" ) == "unique_value_limit" );
    const auto rows = countRows( service, capped );
    REQUIRE( rows.size() == 1 );
    CHECK( rows.front().toMap().value( "count" ) == "2" );
    CHECK( rows.front().toMap().value( "denominator" ) == "2" );
    const auto many = startCounts( service, QString( "x\n" ).repeated( 20050 ), "x",
                                   { { "maxValues", 1 } }, { { "resultRecords", 1 } } );
    REQUIRE( countFinished( service, many ).value( "complete" ).toBool() );
    REQUIRE( countRows( service, many ).size() == 1 );
    CHECK( countRows( service, many ).front().toMap().value( "count" ) == "20050" );
}

TEST_CASE( "Value count option errors, cancellation, and empty inputs are structured",
           "[logdata][valuecounts]" )
{
    AnalysisJobService service;
    for ( const auto& counts : QVariantList{
              QVariantMap{ { "maxValues", 0 } }, QVariantMap{ { "subpattern", -1 } },
              QVariantMap{ { "missing", "guess" } }, QVariantMap{ { "capture", true } },
              QVariantMap{ { "caseSensitive", "false" } }, QVariantMap{ { "unknown", 1 } } } ) {
        const auto rejected = executeAnalysisOperation(
            service, "value_counts",
            { { "scope", "text" }, { "text", "x" }, { "pattern", "x" }, { "counts", counts } } );
        CHECK_FALSE( rejected.ok );
        CHECK( rejected.payload.value( "reason" ) == "invalid_count_options" );
    }
    const auto empty = startCounts( service, {}, "x" );
    CHECK( countFinished( service, empty ).value( "complete" ).toBool() );
    CHECK( countRows( service, empty ).isEmpty() );
    const auto bad = startCounts( service, "x", "(" );
    CHECK( countFinished( service, bad ).value( "reason" ) == "invalid_pattern" );
    const auto cancel = startCounts( service, "x", "x" );
    REQUIRE( service.cancel( cancel ) );
    REQUIRE( service.cancel( cancel ) );
    CHECK( countFinished( service, cancel ).value( "state" ) == "cancelled" );
    REQUIRE( service.release( cancel ) );
    CHECK( service.status( cancel ).value( "reason" ) == "job_expired" );
}

TEST_CASE( "Value counts freeze provisional tails and export bounded exact CSV",
           "[logdata][valuecounts]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "append.log" );
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( "key=A\r\nkey=B" ) > 0 );
    }
    auto source = std::make_shared<LogData>();
    source->attachFile( path );
    REQUIRE( QTest::qWaitFor( [ & ] { return source->sourceSnapshot().available; }, 10000 ) );
    source->setDisplayEncoding( "UTF-8" );
    AnalysisJobService service;
    AnalysisRequest frozen;
    frozen.source = source;
    frozen.snapshot = source->sourceSnapshot();
    frozen.sourceDescription = { { "filePath", path } };
    const QVariantMap definition{ { "scope", "whole_file" },
                                  { "pattern", "key=(?<v>[^\\s]+)" },
                                  { "counts", QVariantMap{ { "capture", "v" } } } };
    const auto started = executeAnalysisOperation( service, "value_counts", definition, frozen,
                                                   ANALYSIS_WORKER_PATH );
    REQUIRE( started.ok );
    const auto id = started.payload.value( "jobId" ).toString();
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::Append ) );
        REQUIRE( file.write( "B\r\nkey=C\n" ) > 0 );
    }
    REQUIRE( QMetaObject::invokeMethod( source.get(), "fileChangedOnDisk", Qt::DirectConnection,
                                        Q_ARG( QString, path ) ) );
    REQUIRE( countFinished( service, id ).value( "complete" ).toBool() );
    const auto original = countRows( service, id );
    REQUIRE( original.size() == 2 );
    CHECK( original.at( 0 ).toMap().value( "value" ) == "A" );
    CHECK( original.at( 1 ).toMap().value( "value" ) == "B" );
    REQUIRE(
        QTest::qWaitFor( [ & ] { return source->sourceSnapshot().lineCount.get() == 3; }, 10000 ) );
    frozen.snapshot = source->sourceSnapshot();
    const auto rerun = executeAnalysisOperation( service, "value_counts", definition, frozen,
                                                 ANALYSIS_WORKER_PATH );
    REQUIRE( rerun.ok );
    const auto next = rerun.payload.value( "jobId" ).toString();
    REQUIRE( countFinished( service, next ).value( "complete" ).toBool() );
    const auto updated = countRows( service, next );
    REQUIRE( updated.size() == 3 );
    CHECK( updated.at( 1 ).toMap().value( "value" ) == "BB" );
    CHECK( updated.at( 2 ).toMap().value( "value" ) == "C" );
    const auto csv = directory.filePath( "counts.csv" );
    const auto exported = executeAnalysisOperation( service, "export_value_counts",
                                                    { { "jobId", next }, { "filePath", csv } } );
    REQUIRE( exported.ok );
    CHECK( exported.payload.value( "exportedRows" ).toInt() == 3 );
    QFile file( csv );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const auto content = file.readAll();
    CHECK( content.startsWith( "value,is_missing,count,percentage" ) );
    CHECK( content.contains( "\"BB\",\"false\",\"1\"" ) );
    CHECK( executeAnalysisOperation( service, "export_value_counts",
                                     { { "jobId", next }, { "filePath", path } } )
               .payload.value( "reason" )
           == "source_is_export_target" );
#ifdef Q_OS_WIN
    CHECK( executeAnalysisOperation( service, "export_value_counts",
                                     { { "jobId", next }, { "filePath", path.toUpper() } } )
               .payload.value( "reason" )
           == "source_is_export_target" );
#endif
    service.cancelSource( source.get() );
    CHECK_FALSE( service.location( next, 0 ) );
    CHECK( service.status( next ).value( "historical" ).toBool() );
    CHECK( countRows( service, next ).size() == 3 );
}

TEST_CASE( "Value count memory limits remain partial and CSV requires explicit partial consent",
           "[logdata][valuecounts]" )
{
    AnalysisJobService service;
    const auto memory = startCounts( service, "a", "a", {}, { { "resultBytes", 1024 } } );
    const auto memoryStatus = countFinished( service, memory );
    CHECK_FALSE( memoryStatus.value( "complete" ).toBool() );
    CHECK( memoryStatus.value( "reason" ) == "aggregation_memory_limit" );
    CHECK( countRows( service, memory ).isEmpty() );
    const auto id = startCounts( service, "a b", "\\w+", { { "maxValues", 1 } } );
    REQUIRE_FALSE( countFinished( service, id ).value( "complete" ).toBool() );
    QTemporaryDir directory;
    const auto path = directory.filePath( "partial.csv" );
    CHECK( executeAnalysisOperation( service, "export_value_counts",
                                     { { "jobId", id }, { "filePath", path } } )
               .payload.value( "reason" )
           == "incomplete_job" );
    CHECK_FALSE( QFile::exists( path ) );
    REQUIRE( executeAnalysisOperation(
                 service, "export_value_counts",
                 { { "jobId", id }, { "filePath", path }, { "allowPartial", true } } )
                 .ok );
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    CHECK( file.readAll().contains( "\"false\",\"unique_value_limit\"" ) );
}

TEST_CASE( "Value count reducers retain the service processor-output boundary",
           "[logdata][valuecounts]" )
{
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = "x";
    request.valueCounts = ValueCountOptions{};
    const auto id = service.start( request, [] {
        return []( const AnalysisBatch& batch, const std::atomic_bool&, int ) {
            AnalysisOutput output;
            if ( !batch.lines.empty() )
                output.rows.push_back( 42 );
            return output;
        };
    } );
    const auto state = countFinished( service, id );
    CHECK( state.value( "state" ) == "failed" );
    CHECK( state.value( "reason" ) == "invalid_worker_output" );
    CHECK_FALSE( state.value( "complete" ).toBool() );
    CHECK( countRows( service, id ).isEmpty() );
    request.valueCounts->maxValues = 0;
    QString error;
    CHECK( service.start( request, {}, &error ).isEmpty() );
    CHECK( error == "invalid_count_options" );
}

TEST_CASE( "Value counts preserve Boolean subpattern isolation and zero-width occurrences",
           "[logdata][valuecounts]" )
{
    AnalysisJobService service;
    const auto result = executeAnalysisOperation(
        service, "value_counts",
        { { "scope", "text" },
          { "text", "aa b\nb\na" },
          { "pattern", "\"(?<v>a)\" and \"(?<v>b)\"" },
          { "options", QVariantMap{ { "boolean", true } } },
          { "counts", QVariantMap{ { "capture", "v" }, { "subpattern", 1 } } } },
        {}, ANALYSIS_WORKER_PATH );
    REQUIRE( result.ok );
    const auto id = result.payload.value( "jobId" ).toString();
    REQUIRE( countFinished( service, id ).value( "complete" ).toBool() );
    const auto values = countRows( service, id );
    REQUIRE( values.size() == 1 );
    CHECK( values.front().toMap().value( "value" ) == "b" );
    CHECK( values.front().toMap().value( "count" ) == "1" );
    const auto zero = startCounts( service, "aa", "(?=a)" );
    REQUIRE( countFinished( service, zero ).value( "complete" ).toBool() );
    const auto empty = countRows( service, zero );
    REQUIRE( empty.size() == 1 );
    CHECK_FALSE( empty.front().toMap().value( "missing" ).toBool() );
    CHECK( empty.front().toMap().value( "value" ).toString().isEmpty() );
    CHECK( empty.front().toMap().value( "count" ) == "2" );
}
