// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysisjob.h"
#include "logdata.h"
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <catch2/catch.hpp>
#include <limits>
#include <mutex>

namespace {
void writeAnalysisFixture( const QString& path, const QByteArray& bytes, bool append = false )
{
    QFile file( path );
    REQUIRE(
        file.open( QIODevice::WriteOnly | ( append ? QIODevice::Append : QIODevice::Truncate ) ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
}
std::shared_ptr<LogData> openAnalysisFixture( const QString& path )
{
    auto data = std::make_shared<LogData>();
    data->attachFile( path );
    REQUIRE( QTest::qWaitFor( [ & ] { return data->sourceSnapshot().available; }, 10000 ) );
    REQUIRE( data->getDetectedEncoding() != nullptr );
    data->setDisplayEncoding( data->getDetectedEncoding()->name().constData() );
    return data;
}
QVariantMap finishAnalysis( AnalysisJobService& service, const QString& id )
{
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto state = service.status( id ).value( "state" ).toString();
            return state == "completed" || state == "failed" || state == "cancelled";
        },
        10000 ) );
    return service.status( id );
}
} // namespace

namespace {
std::vector<AnalysisLine> collectProvenanceLines( AnalysisRequest request )
{
    AnalysisJobService service;
    auto lines = std::make_shared<std::vector<AnalysisLine>>();
    auto mutex = std::make_shared<std::mutex>();
    const auto requestedBatchLines = request.limits.batchLines;
    QString error;
    const auto id = service.start(
        std::move( request ),
        [ lines, mutex ] {
            return [ lines, mutex ]( const AnalysisBatch& batch, const std::atomic_bool&, int ) {
                const std::lock_guard<std::mutex> lock( *mutex );
                lines->insert( lines->end(), batch.lines.begin(), batch.lines.end() );
                return AnalysisOutput{};
            };
        },
        &error );
    INFO( "Analysis admission batchLines=" << requestedBatchLines
                                           << " reason=" << error.toStdString() );
    REQUIRE_FALSE( id.isEmpty() );
    REQUIRE( finishAnalysis( service, id ).value( "complete" ).toBool() );
    const std::lock_guard<std::mutex> lock( *mutex );
    return *lines;
}
} // namespace

TEST_CASE( "F01_analysis_lines_keep_indexed_bytes_and_UTF16_spans",
           "[logdata][analysis][data][F01]" )
{
    struct Fixture {
        const char* codec;
        const char* hex;
        quint64 firstEnd;
        quint64 secondEnd;
        const char* terminatorHex;
    };
    const auto fixture = GENERATE(
        Fixture{ "UTF-8", "efbbbf410d0af09f98810d0a74", 6, 12, "0d0a" },
        Fixture{ "UTF-16LE", "fffe41000d000a003dd801de0d000a007400", 8, 16, "0d000a00" },
        Fixture{ "UTF-16BE", "feff0041000d000ad83dde01000d000a0074", 8, 16, "000d000a" },
        Fixture{ "UTF-32LE", "fffe0000410000000d0000000a00000001f601000d0000000a00000074000000", 16,
                 28, "0d0000000a000000" },
        Fixture{ "UTF-32BE", "0000feff000000410000000d0000000a0001f6010000000d0000000a00000074", 16,
                 28, "0000000d0000000a" },
        Fixture{ "UTF-16LE", "41000d000a003dd801de0d000a007400", 6, 14, "0d000a00" } );
    CAPTURE( fixture.codec, fixture.hex );
    QTemporaryDir directory;
    const auto path = directory.filePath( "provenance.log" );
    const auto bytes = QByteArray::fromHex( fixture.hex );
    writeAnalysisFixture( path, bytes );
    auto data = openAnalysisFixture( path );
    data->setDisplayEncoding( fixture.codec );
    REQUIRE( QTest::qWaitFor( [ & ] { return data->sourceSnapshot().available; }, 10000 ) );
    AnalysisRequest request;
    request.source = data;
    request.snapshot = data->sourceSnapshot();
    request.scope = "selection";
    request.ranges = { { 1, 1 }, { 2, 2 } };
    request.limits.batchLines = 1;

    const auto first = data->readAnalysisBlock( request.snapshot, 0, request.limits );
    REQUIRE( first.error.isEmpty() );
    REQUIRE( first.records.size() == 1 );
    CHECK( first.records.front().bytes.begin == 0 );
    CHECK( first.records.front().bytes.endExclusive == fixture.firstEnd );
    CHECK( first.records.front().terminatorBytes == QByteArray::fromHex( fixture.terminatorHex ) );
    CHECK_FALSE( first.records.front().provisional );
    const auto lines = collectProvenanceLines( request );
    REQUIRE( lines.size() == 2 );
    CHECK( lines[ 0 ].line == 1 );
    CHECK( lines[ 0 ].text == QString::fromUtf8( "😁\r" ) );
    REQUIRE( lines[ 0 ].provenance );
    const auto& origin = *lines[ 0 ].provenance;
    CHECK( origin.snapshot.sameRevision( request.snapshot ) );
    CHECK( origin.encoding == QByteArray( fixture.codec ) );
    CHECK( origin.bytes.begin == fixture.firstEnd );
    CHECK( origin.bytes.endExclusive == fixture.secondEnd );
    CHECK( origin.terminatorBytes == QByteArray::fromHex( fixture.terminatorHex ) );
    CHECK_FALSE( origin.provisional );
    const auto location = lines[ 0 ].sourceLocation( SourceSpan{ 0, 2 } );
    REQUIRE( location );
    CHECK( location->line() == 1_lnum );
    CHECK( location->span->length == 2 );
    CHECK( request.snapshot.accepts( *location ) );
    CHECK_FALSE( lines[ 0 ].sourceLocation( SourceSpan{ 3, 1 } ) );
    CHECK_FALSE(
        lines[ 0 ].sourceLocation( SourceSpan{ std::numeric_limits<quint64>::max(), 1 } ) );
    CHECK( lines[ 0 ].sourceLocation( SourceSpan{ 3, 0 } ) );
    REQUIRE( lines[ 1 ].provenance );
    CHECK( lines[ 1 ].text == "t" );
    CHECK( lines[ 1 ].provenance->bytes.begin == fixture.secondEnd );
    CHECK( lines[ 1 ].provenance->bytes.endExclusive == static_cast<quint64>( bytes.size() ) );
    CHECK( lines[ 1 ].provenance->terminatorBytes.isEmpty() );
    CHECK( lines[ 1 ].provenance->provisional );
}

TEST_CASE( "F01_analysis_provenance_keeps_frozen_tail_and_rejects_stale_navigation",
           "[logdata][analysis][data][F01]" )
{
    QTemporaryDir directory;
    const auto path = directory.filePath( "tail-provenance.log" );
    writeAnalysisFixture( path, "A\r\nt" );
    auto data = openAnalysisFixture( path );
    AnalysisRequest request;
    request.source = data;
    request.snapshot = data->sourceSnapshot();
    request.scope = "whole_file";
    request.limits.batchLines = 1;
    writeAnalysisFixture( path, "ail\r\nnew\n", true );
    REQUIRE( QMetaObject::invokeMethod( data.get(), "fileChangedOnDisk", Qt::DirectConnection,
                                        Q_ARG( QString, path ) ) );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto now = data->sourceSnapshot();
            return now.available && now.revision > request.snapshot.revision;
        },
        10000 ) );
    const auto lines = collectProvenanceLines( request );
    REQUIRE( lines.size() == 2 );
    REQUIRE( lines[ 1 ].provenance );
    CHECK( lines[ 1 ].text == "t" );
    CHECK( lines[ 1 ].provenance->bytes.begin == 3 );
    CHECK( lines[ 1 ].provenance->bytes.endExclusive == 4 );
    CHECK( lines[ 1 ].provenance->terminatorBytes.isEmpty() );
    CHECK( lines[ 1 ].provenance->provisional );
    const auto completed = lines[ 0 ].sourceLocation();
    const auto provisional = lines[ 1 ].sourceLocation();
    REQUIRE( completed );
    REQUIRE( provisional );
    CHECK( data->sourceSnapshot().accepts( *completed ) );
    CHECK_FALSE( data->sourceSnapshot().accepts( *provisional ) );
    data->reload();
    REQUIRE( QTest::qWaitFor( [ & ] { return data->sourceSnapshot().available; }, 10000 ) );
    CHECK_FALSE( data->sourceSnapshot().accepts( *completed ) );
    const auto stale = data->readAnalysisBlock( request.snapshot, 0, request.limits );
    CHECK( stale.error == "stale_source" );
    CHECK( stale.records.empty() );
    request.source.reset();
    data.reset();
    CHECK_FALSE( lines[ 0 ].sourceLocation() );
    CHECK( lines[ 1 ].provenance->bytes.endExclusive == 4 );
}

TEST_CASE( "F01_inline_analysis_has_no_invented_file_bytes", "[logdata][analysis][data][F01]" )
{
    AnalysisRequest request;
    request.scope = "text";
    request.text = QString::fromUtf8( "α\nβ" );
    const auto lines = collectProvenanceLines( request );
    REQUIRE( lines.size() == 2 );
    for ( const auto& line : lines ) {
        CHECK_FALSE( line.provenance );
        CHECK_FALSE( line.sourceLocation() );
    }
}

TEST_CASE( "F01_sparse_rows_keep_empty_NUL_and_single_byte_source_ranges",
           "[logdata][analysis][data][F01]" )
{
    QTemporaryDir directory;
    const auto path = directory.filePath( "sparse-byte-provenance.log" );
    writeAnalysisFixture( path, QByteArray::fromHex( "410d0a0a7800790ae9" ) );
    auto data = openAnalysisFixture( path );
    data->setDisplayEncoding( "ISO-8859-1" );
    REQUIRE( QTest::qWaitFor( [ & ] { return data->sourceSnapshot().available; }, 10000 ) );
    AnalysisRequest request;
    request.source = data;
    request.snapshot = data->sourceSnapshot();
    request.scope = "selection";
    request.ranges = { { 1, 1 }, { 3, 3 } };
    request.limits.batchLines = 2;
    const auto lines = collectProvenanceLines( request );
    REQUIRE( lines.size() == 2 );
    CHECK( lines[ 0 ].line == 1 );
    CHECK( lines[ 0 ].text.isEmpty() );
    REQUIRE( lines[ 0 ].provenance );
    CHECK( lines[ 0 ].provenance->bytes.begin == 3 );
    CHECK( lines[ 0 ].provenance->bytes.endExclusive == 4 );
    CHECK( lines[ 0 ].provenance->terminatorBytes == "\n" );
    CHECK( lines[ 0 ].sourceLocation( SourceSpan{ 0, 0 } ) );
    CHECK_FALSE( lines[ 0 ].sourceLocation( SourceSpan{ 0, 1 } ) );
    CHECK( lines[ 1 ].line == 3 );
    CHECK( lines[ 1 ].text == QString::fromUtf8( "é" ) );
    REQUIRE( lines[ 1 ].provenance );
    CHECK( lines[ 1 ].provenance->bytes.begin == 8 );
    CHECK( lines[ 1 ].provenance->bytes.endExclusive == 9 );
    CHECK( lines[ 1 ].provenance->terminatorBytes.isEmpty() );
    CHECK( lines[ 1 ].provenance->provisional );
    auto invalid = lines[ 1 ];
    invalid.provenance->bytes.endExclusive = 8;
    CHECK_FALSE( invalid.sourceLocation() );
    invalid.provenance->bytes.endExclusive = 10;
    CHECK_FALSE( invalid.sourceLocation() );
    invalid = lines[ 1 ];
    invalid.provenance->provisional = false;
    CHECK_FALSE( invalid.sourceLocation() );
}

TEST_CASE( "F01_raw_provenance_obeys_empty_record_and_batch_limits",
           "[logdata][analysis][data][F01]" )
{
    QTemporaryDir directory;
    const auto path = directory.filePath( "bounded-provenance.log" );
    const auto kind = GENERATE( 0, 1, 2 );
    writeAnalysisFixture( path, kind == 0   ? QByteArray{}
                                : kind == 1 ? QByteArray( 32, 'x' )
                                            : QByteArray( "x\n" ).repeated( 300 ) );
    auto data = std::make_shared<LogData>();
    data->attachFile( path );
    REQUIRE( QTest::qWaitFor( [ & ] { return data->sourceSnapshot().available; }, 10000 ) );
    data->setDisplayEncoding( "UTF-8" );
    REQUIRE( QTest::qWaitFor( [ & ] { return data->sourceSnapshot().available; }, 10000 ) );
    AnalysisRequest request;
    request.source = data;
    request.snapshot = data->sourceSnapshot();
    request.scope = "whole_file";
    request.limits.batchLines = 1000;
    request.limits.recordBytes = 16;
    const auto block = data->readAnalysisBlock( request.snapshot, 0, request.limits );
    if ( kind != 2 ) {
        CHECK( block.records.empty() );
        CHECK( block.bytes.isEmpty() );
        CHECK( block.error == ( kind == 0 ? "invalid_source_range" : "record_limit" ) );
        if ( kind == 0 ) {
            request.limits.batchLines = 256;
            CHECK( collectProvenanceLines( request ).empty() );
        }
    }
    else {
        REQUIRE( block.error.isEmpty() );
        REQUIRE( block.records.size() == 256 );
        CHECK( block.nextLine == 256 );
        CHECK( block.bytes.size() == 512 );
        CHECK( block.records.back().bytes.begin == 510 );
        CHECK( block.records.back().bytes.endExclusive == 512 );
        CHECK( block.records.back().terminatorBytes == "\n" );
        CHECK_FALSE( block.records.back().provisional );
    }
}

TEST_CASE( "Analysis sample never reads an oversized record beyond its line cap",
           "[logdata][analysis]" )
{
    const bool extraRecord = GENERATE( false, true );
    QTemporaryDir directory;
    const auto path = directory.filePath( "sample.log" );
    auto bytes = QByteArray( "small\n" ).repeated( 1000 );
    if ( extraRecord )
        bytes += QByteArray( 1024 * 1024 + 1, 'x' );
    writeAnalysisFixture( path, bytes );
    auto data = openAnalysisFixture( path );
    AnalysisJobService service;
    AnalysisRequest request;
    request.source = data;
    request.snapshot = data->sourceSnapshot();
    request.scope = "sample";
    const auto id = service.start( request );
    REQUIRE_FALSE( id.isEmpty() );
    const auto status = finishAnalysis( service, id );
    CHECK( status.value( "scannedLines" ).toInt() == 1000 );
    CHECK( status.value( "complete" ).toBool() == !extraRecord );
    CHECK( status.value( "reason" ).toString() == ( extraRecord ? "sample_limit" : "" ) );
}

TEST_CASE( "Analysis scans freeze the indexed prefix including an unterminated tail",
           "[logdata][analysis]" )
{
    const auto utf16 = GENERATE( false, true );
    QTemporaryDir directory;
    const auto path = directory.filePath( "prefix.log" );
    writeAnalysisFixture( path,
                          utf16 ? QByteArray::fromHex( "fffe660069007200730074000a00700061007200" )
                                : QByteArray( "first\npar" ) );
    auto data = openAnalysisFixture( path );
    AnalysisJobService service;
    AnalysisRequest request;
    request.source = data;
    request.snapshot = data->sourceSnapshot();
    request.scope = "whole_file";
    request.limits.batchLines = 1;
    // Append before reading: the original watermark must still bound the tail.
    writeAnalysisFixture( path,
                          utf16 ? QByteArray::fromHex( "7400690061006c000a006e00650077000a00" )
                                : QByteArray( "tial\nnew\n" ),
                          true );
    REQUIRE( QMetaObject::invokeMethod( data.get(), "fileChangedOnDisk", Qt::DirectConnection,
                                        Q_ARG( QString, path ) ) );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto now = data->sourceSnapshot();
            return now.available && now.revision != request.snapshot.revision;
        },
        10000 ) );
    const auto id = service.start( request );
    REQUIRE_FALSE( id.isEmpty() );
    const auto status = finishAnalysis( service, id );
    CHECK( status.value( "state" ).toString() == "completed" );
    CHECK( status.value( "complete" ).toBool() );
    const auto rows = service.results( id ).value( "rows" ).toList();
    REQUIRE( rows.size() == 2 );
    CHECK( rows.at( 0 ).toMap().value( "text" ).toString() == "first" );
    CHECK( rows.at( 1 ).toMap().value( "text" ).toString() == "par" );
}

TEST_CASE( "Analysis jobs expose limits cancellation and invalidation without blocking",
           "[logdata][analysis]" )
{
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = "one\ntwo\nthree";
    request.limits.resultRecords = 2;
    auto id = service.start( request );
    auto state = finishAnalysis( service, id );
    CHECK( state.value( "state" ).toString() == "completed" );
    CHECK_FALSE( state.value( "complete" ).toBool() );
    CHECK( state.value( "reason" ).toString() == "result_limit" );
    CHECK( service.results( id ).value( "rows" ).toList().size() == 2 );
    CHECK( service.release( id ) );
    CHECK( service.status( id ).value( "reason" ).toString() == "job_expired" );

    request.limits.resultRecords = 10000;
    id = service.start( request );
    CHECK( service.cancel( id ) );
    CHECK( service.cancel( id ) );
    state = finishAnalysis( service, id );
    CHECK( state.value( "state" ).toString() == "cancelled" );
}

TEST_CASE( "Analysis rejects oversized records before reading their bodies", "[logdata][analysis]" )
{
    QTemporaryDir directory;
    const auto path = directory.filePath( "oversized.log" );
    writeAnalysisFixture( path, QByteArray( 1025, 'x' ) + '\n' );
    auto data = openAnalysisFixture( path );
    AnalysisJobService service;
    AnalysisRequest request;
    request.source = data;
    request.snapshot = data->sourceSnapshot();
    request.scope = "whole_file";
    request.limits.recordBytes = 1024;
    const auto id = service.start( request );
    const auto state = finishAnalysis( service, id );
    CHECK( state.value( "reason" ).toString() == "record_limit" );
    CHECK_FALSE( state.value( "complete" ).toBool() );
    CHECK( service.results( id ).value( "rows" ).toList().isEmpty() );
}

TEST_CASE( "Analysis preserves Unicode decoding across sparse bounded batches",
           "[logdata][analysis]" )
{
    QTemporaryDir directory;
    const auto path = directory.filePath( "unicode.log" );
    // UTF-16LE BOM, Hebrew, LF, emoji surrogate pair, LF, unterminated final x.
    writeAnalysisFixture( path, QByteArray::fromHex( "fffee9050a003dd801de0a007800" ) );
    auto data = openAnalysisFixture( path );
    AnalysisJobService service;
    AnalysisRequest request;
    request.source = data;
    request.snapshot = data->sourceSnapshot();
    request.scope = "selection";
    request.ranges = { { 1, 2 } };
    request.limits.batchLines = 1;
    const auto id = service.start( request );
    REQUIRE( finishAnalysis( service, id ).value( "complete" ).toBool() );
    const auto rows = service.results( id ).value( "rows" ).toList();
    REQUIRE( rows.size() == 2 );
    CHECK( rows.at( 0 ).toMap().value( "text" ).toString() == QString::fromUtf8( "😁" ) );
    CHECK( rows.at( 0 ).toMap().value( "lineNumber" ).toULongLong() == 2 );
    CHECK( rows.at( 1 ).toMap().value( "text" ).toString() == "x" );
}

TEST_CASE(
    "Analysis discards results invalidated during processing and releases leases on owner thread",
    "[logdata][analysis]" )
{
    const auto close = GENERATE( false, true );
    QTemporaryDir directory;
    const auto path = directory.filePath( "lease.log" );
    writeAnalysisFixture( path, "first\nsecond\n" );
    auto data = openAnalysisFixture( path );
    const std::weak_ptr<LogData> weak = data;
    auto entered = std::make_shared<std::atomic_bool>( false );
    auto proceed = std::make_shared<std::atomic_bool>( false );
    auto destroyedOnOwner = std::make_shared<std::atomic_bool>( false );
    auto* owner = QThread::currentThread();
    QObject::connect( data.get(), &QObject::destroyed, [ destroyedOnOwner, owner ] {
        *destroyedOnOwner = QThread::currentThread() == owner;
    } );
    AnalysisJobService service;
    AnalysisRequest request;
    request.source = data;
    request.snapshot = data->sourceSnapshot();
    request.scope = "whole_file";
    const auto id = service.start( std::move( request ), [ entered, proceed ] {
        return [ entered, proceed ]( const AnalysisBatch& batch, const std::atomic_bool& cancelled,
                                     int ) {
            if ( batch.lines.empty() ) {
                return AnalysisOutput{};
            }
            *entered = true;
            while ( !*proceed && !cancelled ) {
                QThread::msleep( 1 );
            }
            AnalysisOutput result;
            result.rows = { QVariantMap{ { "lineNumber", 1 }, { "text", "stale" } } };
            return result;
        };
    } );
    REQUIRE( QTest::qWaitFor( [ & ] { return entered->load(); }, 5000 ) );
    if ( close ) {
        service.cancelSource( data.get() );
    }
    else {
        data->reload();
    }
    *proceed = true;
    data.reset();
    const auto state = finishAnalysis( service, id );
    CHECK_FALSE( state.value( "complete" ).toBool() );
    CHECK( service.results( id ).value( "rows" ).toList().isEmpty() );
    REQUIRE( QTest::qWaitFor( [ & ] { return weak.expired(); }, 5000 ) );
    CHECK( destroyedOnOwner->load() );
}

TEST_CASE( "Analysis bounds byte delivery paging metadata and empty-result scans",
           "[logdata][analysis]" )
{
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = QString( "abc\n" ).repeated( 10000 );
    request.limits.batchBytes = 32;
    request.limits.recordBytes = 16;
    auto largest = std::make_shared<std::atomic<qint64>>( 0 );
    const auto id = service.start( request, [ largest ] {
        return [ largest ]( const AnalysisBatch& batch, const std::atomic_bool&, int ) {
            qint64 bytes = 0;
            for ( const auto& line : batch.lines ) {
                bytes += line.text.toUtf8().size() + 1;
            }
            largest->store( std::max( largest->load(), bytes ) );
            return AnalysisOutput{};
        };
    } );
    CHECK( finishAnalysis( service, id ).value( "complete" ).toBool() );
    CHECK( largest->load() <= 32 );
    CHECK( service.status( id ).value( "pendingBytes" ).toLongLong() == 0 );

    request.text = "a\nb\nc";
    const auto pageId = service.start( request );
    finishAnalysis( service, pageId );
    const auto first = service.results( pageId, {}, 1 );
    const auto cursor = first.value( "nextCursor" ).toString();
    CHECK( service.results( pageId, {}, 1 ).value( "nextCursor" ).toString() == cursor );
    CHECK( service.results( pageId, cursor, 1 )
               .value( "rows" )
               .toList()
               .front()
               .toMap()
               .value( "text" )
           == "b" );
    CHECK( service.results( pageId, "invalid" ).value( "reason" ) == "invalid_cursor" );
    QString oldest;
    for ( int index = 0; index < 30; ++index ) {
        const auto next = service.start( request );
        if ( index == 0 ) {
            oldest = next;
        }
        finishAnalysis( service, next );
    }
    CHECK( service.status( oldest ).value( "reason" ) == "job_expired" );
}

TEST_CASE( "Analysis rejects incorrectly typed processor records", "[logdata][analysis]" )
{
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = "x";
    const auto id = service.start( request, [] {
        return []( const AnalysisBatch&, const std::atomic_bool&, int ) {
            AnalysisOutput output;
            output.rows = { QString( 1024 * 1024, 'x' ) };
            return output;
        };
    } );
    const auto state = finishAnalysis( service, id );
    CHECK( state.value( "state" ).toString() == "failed" );
    CHECK( state.value( "reason" ).toString() == "invalid_worker_output" );
    CHECK( state.value( "retainedBytes" ).toLongLong() == 0 );
}

TEST_CASE( "Analysis raw rows preserve existing CRLF and empty-line semantics",
           "[logdata][analysis]" )
{
    QTemporaryDir directory;
    const auto path = directory.filePath( "crlf.log" );
    writeAnalysisFixture( path, "one\r\n\nthree" );
    auto data = openAnalysisFixture( path );
    AnalysisJobService service;
    AnalysisRequest request;
    request.source = data;
    request.snapshot = data->sourceSnapshot();
    request.scope = "whole_file";
    request.limits.batchLines = 1;
    const auto id = service.start( request );
    REQUIRE( finishAnalysis( service, id ).value( "complete" ).toBool() );
    const auto rows = service.results( id ).value( "rows" ).toList();
    REQUIRE( rows.size() == 3 );
    for ( int line = 0; line < 3; ++line ) {
        const auto raw
            = data->getLinesRaw( LineNumber{ static_cast<LineNumber::UnderlyingType>( line ) },
                                 1_lcount )
                  .decodeLines();
        REQUIRE( raw.size() == 1 );
        CHECK( rows.at( line ).toMap().value( "text" ).toString() == raw.front() );
    }
}

TEST_CASE( "Analysis admission bounds active and queued jobs", "[logdata][analysis]" )
{
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = "x";
    for ( int index = 0; index < 10; ++index ) {
        REQUIRE_FALSE( service.start( request ).isEmpty() );
    }
    QString error;
    CHECK( service.start( request, {}, &error ).isEmpty() );
    CHECK( error == "queue_limit" );
}

TEST_CASE( "Analysis batch credit blocks producers and cancellation discards queued work",
           "[logdata][analysis]" )
{
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = ( QString( 1024, 'x' ) + '\n' ).repeated( 1000 );
    request.limits.batchLines = 1;
    const auto id = service.start( request );
    REQUIRE( QTest::qWaitFor( [ & ] { return service.status( id ).value( "state" ) == "running"; },
                              5000 ) );
    // Deliberately stop the delivery event loop while the background producer fills credit.
    QElapsedTimer deadline;
    deadline.start();
    while ( service.status( id ).value( "pendingBatches" ).toInt() < 8
            && deadline.elapsed() < 5000 ) {
        QTest::qSleep( 1 );
    }
    const auto stalled = service.status( id );
    CHECK( stalled.value( "pendingBatches" ).toInt() == 8 );
    CHECK( stalled.value( "pendingBytes" ).toLongLong() <= 4 * 1024 * 1024 );
    CHECK( stalled.value( "scannedLines" ).toULongLong() < 1000 );
    REQUIRE( service.cancel( id ) );
    CHECK( finishAnalysis( service, id ).value( "state" ) == "cancelled" );
    CHECK( service.status( id ).value( "pendingBytes" ).toLongLong() == 0 );
}

TEST_CASE( "Analysis rejects unbounded processor metric schemas", "[logdata][analysis]" )
{
    AnalysisJobService service;
    AnalysisRequest request;
    request.scope = "text";
    request.text = "x";
    const auto id = service.start( request, [] {
        return []( const AnalysisBatch&, const std::atomic_bool&, int ) {
            AnalysisOutput result;
            result.metrics[ QString( 1024 * 1024, 'x' ) ] = 1;
            return result;
        };
    } );
    const auto state = finishAnalysis( service, id );
    CHECK( state.value( "state" ) == "failed" );
    CHECK( state.value( "reason" ) == "invalid_worker_output" );
    CHECK( state.value( "metrics" ).toMap().isEmpty() );
}

TEST_CASE( "Analysis source destruction can reenter the job registry", "[logdata][analysis]" )
{
    const auto teardown = GENERATE( false, true );
    QTemporaryDir directory;
    const auto path = directory.filePath( "reentrant.log" );
    writeAnalysisFixture( path, "one\ntwo\n" );
    auto service = std::make_unique<AnalysisJobService>();
    auto data = openAnalysisFixture( path );
    auto destroyed = std::make_shared<bool>( false );
    AnalysisRequest request;
    request.snapshot = data->sourceSnapshot();
    request.scope = "whole_file";
    request.source = std::move( data );
    const auto source = request.source.get();
    const auto id = service->start( std::move( request ) );
    const auto owner = service.get();
    QObject::connect( source, &QObject::destroyed, owner, [ owner, id, destroyed ] {
        *destroyed = true;
        owner->release( id );
    } );
    if ( teardown ) {
        service.reset();
        CHECK( *destroyed );
    }
    else {
        REQUIRE( QTest::qWaitFor( [ destroyed ] { return *destroyed; }, 10000 ) );
        CHECK( service->status( id ).value( "reason" ) == "job_expired" );
    }
}
