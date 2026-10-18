/*
 * Copyright (C) 2026 CILogg contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch.hpp>

#include <limits>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "logdata.h"
#include "test_utils.h"

namespace {
void writeSourceData( const QString& path, const QByteArray& bytes,
                      QIODevice::OpenMode mode = QIODevice::WriteOnly | QIODevice::Truncate )
{
    QFile file( path );
    REQUIRE( file.open( mode ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
    REQUIRE( file.flush() );
}

// Drive the existing watcher slot without depending on platform notification timing.
void notifySourceChanged( LogData& data, const QString& path )
{
    REQUIRE( QMetaObject::invokeMethod( &data, "fileChangedOnDisk", Qt::DirectConnection,
                                        Q_ARG( QString, path ) ) );
}
} // namespace

TEST_CASE( "Source snapshots distinguish completed and provisional appended lines",
           "[logdata][source]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "append.log" );
    writeSourceData( path, "first\npar" );

    LogData data;
    REQUIRE_FALSE( data.sourceSnapshot().available );
    SafeQSignalSpy finished( &data, SIGNAL( loadingFinished( LoadingStatus ) ) );
    data.attachFile( path );
    REQUIRE( finished.safeWait() );

    const auto before = data.sourceSnapshot();
    REQUIRE( before.available );
    REQUIRE( before.lineCount == 2_lcount );
    REQUIRE( before.completeLineCount == 1_lcount );
    REQUIRE( before.indexedBytes == 9 );
    REQUIRE( before.provisionalLineStart == std::optional<qint64>{ 6 } );
    const auto complete = before.location( 0_lnum );
    const auto partial = before.location( 1_lnum, SourceSpan{ 0, 3 } );
    REQUIRE( complete );
    REQUIRE( partial );
    REQUIRE_FALSE( before.location( 2_lnum ) );
    REQUIRE( data.isSourceSnapshotCurrent( before ) );

    finished.clear();
    writeSourceData( path, "tial\nnext\n", QIODevice::WriteOnly | QIODevice::Append );
    notifySourceChanged( data, path );
    REQUIRE( waitUiState( [ & ] {
        const auto snapshot = data.sourceSnapshot();
        return snapshot.available && snapshot.indexedBytes == 19;
    } ) );
    REQUIRE( finished.safeWait() );
    const auto after = data.sourceSnapshot();
    CHECK( after.sourceId == before.sourceId );
    CHECK( after.generation == before.generation );
    CHECK( after.revision > before.revision );
    CHECK( after.lineCount == 3_lcount );
    CHECK( after.completeLineCount == 3_lcount );
    CHECK_FALSE( after.provisionalLineStart );
    CHECK( after.accepts( *complete ) );
    CHECK_FALSE( after.accepts( *partial ) );
    CHECK_FALSE( data.isSourceSnapshotCurrent( before ) );
    CHECK( data.getLineString( 1_lnum ) == "partial" );

    SECTION( "Appending without a newline replaces the provisional watermark" )
    {
        writeSourceData( path, "unfinished", QIODevice::WriteOnly | QIODevice::Append );
        notifySourceChanged( data, path );
        REQUIRE( waitUiState( [ & ] {
            const auto snapshot = data.sourceSnapshot();
            return snapshot.available && snapshot.indexedBytes == 29;
        } ) );
        const auto unfinished = data.sourceSnapshot();
        const auto location = unfinished.location( 3_lnum );
        REQUIRE( location );
        CHECK( unfinished.provisionalLineStart == std::optional<qint64>{ 19 } );
        writeSourceData( path, " tail", QIODevice::WriteOnly | QIODevice::Append );
        notifySourceChanged( data, path );
        REQUIRE( waitUiState( [ & ] {
            const auto snapshot = data.sourceSnapshot();
            return snapshot.available && snapshot.indexedBytes == 34;
        } ) );
        const auto extended = data.sourceSnapshot();
        CHECK( extended.generation == unfinished.generation );
        CHECK( extended.lineCount == 4_lcount );
        CHECK( extended.provisionalLineStart == unfinished.provisionalLineStart );
        CHECK_FALSE( extended.accepts( *location ) );
        CHECK( data.getLineString( 3_lnum ) == "unfinished tail" );
    }
}

TEST_CASE( "Source generations invalidate changed content and interpretation", "[logdata][source]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "source.log" );
    writeSourceData( path, "first\nsecond\n" );
    LogData data;
    SafeQSignalSpy finished( &data, SIGNAL( loadingFinished( LoadingStatus ) ) );
    data.attachFile( path );
    REQUIRE( finished.safeWait() );
    const auto before = data.sourceSnapshot();
    const auto location = before.location( 0_lnum );
    REQUIRE( location );

    SECTION( "Truncation" )
    {
        writeSourceData( path, "x\n" );
        notifySourceChanged( data, path );
        REQUIRE( waitUiState( [ & ] {
            const auto snapshot = data.sourceSnapshot();
            return snapshot.available && snapshot.indexedBytes == 2;
        } ) );
    }
    SECTION( "Rewrite without a size change" )
    {
        writeSourceData( path, "other\nrecord\n" );
        notifySourceChanged( data, path );
        REQUIRE( waitUiState( [ & ] {
            const auto snapshot = data.sourceSnapshot();
            return snapshot.available && snapshot.generation != before.generation;
        } ) );
    }
    SECTION( "Replacement with identical contents" )
    {
        REQUIRE( QFile::rename( path, directory.filePath( "old.log" ) ) );
        writeSourceData( path, "first\nsecond\n" );
        notifySourceChanged( data, path );
        REQUIRE_FALSE( data.isSourceSnapshotCurrent( before ) );
        REQUIRE( waitUiState( [ & ] {
            const auto snapshot = data.sourceSnapshot();
            return snapshot.available && snapshot.generation != before.generation;
        } ) );
    }
    SECTION( "Explicit reload invalidates immediately" )
    {
        finished.clear();
        data.reload( QTextCodec::codecForName( "UTF-8" ) );
        REQUIRE_FALSE( data.isSourceSnapshotCurrent( before ) );
        REQUIRE( finished.safeWait() );
    }
    SECTION( "Decoding change without reindexing" )
    {
        data.setDisplayEncoding( "ISO-8859-2" );
        REQUIRE( data.sourceSnapshot().available );
        const auto changed = data.sourceSnapshot();
        data.setDisplayEncoding( "ISO-8859-2" );
        CHECK( data.isSourceSnapshotCurrent( changed ) );
    }
    SECTION( "Prefilter interpretation change" )
    {
        data.setPrefilter( "first" );
        const auto changed = data.sourceSnapshot();
        data.setPrefilter( "first" );
        CHECK( data.isSourceSnapshotCurrent( changed ) );
    }
    const auto after = data.sourceSnapshot();
    CHECK( after.sourceId == before.sourceId );
    CHECK( after.generation > before.generation );
    CHECK_FALSE( after.accepts( *location ) );
    CHECK_FALSE( data.isSourceSnapshotCurrent( before ) );
}

TEST_CASE( "Source identity is per open source and expires on close", "[logdata][source]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "source.log" );
    writeSourceData( path, "line\n" );
    SourceSnapshot closed;
    std::shared_ptr<const SourceLifetime> retainedLifetime;
    {
        LogData first;
        LogData second;
        SafeQSignalSpy firstFinished( &first, SIGNAL( loadingFinished( LoadingStatus ) ) );
        SafeQSignalSpy secondFinished( &second, SIGNAL( loadingFinished( LoadingStatus ) ) );
        first.attachFile( path );
        second.attachFile( path );
        REQUIRE( firstFinished.safeWait() );
        REQUIRE( secondFinished.safeWait() );
        closed = first.sourceSnapshot();
        retainedLifetime = closed.lifetime.lock();
        REQUIRE( closed.isOpen() );
        REQUIRE( closed.sourceId != second.sourceSnapshot().sourceId );
        REQUIRE_FALSE( second.isSourceSnapshotCurrent( closed ) );
    }
    REQUIRE( retainedLifetime );
    CHECK_FALSE( closed.isOpen() );
    CHECK_FALSE( closed.location( 0_lnum ) );
}

TEST_CASE( "Source watermarks use bytes and spans use UTF-16 units", "[logdata][source]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "unicode.log" );
    const auto utf32 = GENERATE( false, true );
    CAPTURE( utf32 );
    // BOM, a, LF, Hebrew aleph, U+1F680; no final LF.
    const auto bytes = utf32 ? QByteArray::fromHex( "fffe0000610000000a000000d005000080f60100" )
                             : QByteArray::fromHex( "fffe61000a00d0053dd880de" );
    writeSourceData( path, bytes );
    LogData data;
    SafeQSignalSpy finished( &data, SIGNAL( loadingFinished( LoadingStatus ) ) );
    data.attachFile( path );
    REQUIRE( finished.safeWait() );
    data.setDisplayEncoding( utf32 ? "UTF-32LE" : "UTF-16LE" );
    const auto snapshot = data.sourceSnapshot();
    REQUIRE( snapshot.available );
    CHECK( snapshot.indexedBytes == bytes.size() );
    CHECK( snapshot.lineCount == 2_lcount );
    CHECK( snapshot.completeLineCount == 1_lcount );
    CHECK( snapshot.provisionalLineStart == std::optional<qint64>{ utf32 ? 12 : 6 } );
    CHECK( data.getLineString( 1_lnum ).size() == 3 );
    const auto raw = data.getLinesRaw( 1_lnum, 1_lcount );
    REQUIRE( raw.finalLineProvisional );
    REQUIRE( raw.buffer.size() == ( utf32 ? 8 : 6 ) );
    const auto utf8 = raw.buildUtf8View();
    REQUIRE( utf8.size() == 1 );
    const auto expected = QString::fromUtf8( "\xd7\x90\xf0\x9f\x9a\x80" ).toUtf8();
    const std::string_view expectedView{ expected.constData(),
                                         static_cast<size_t>( expected.size() ) };
    CHECK( utf8.front() == expectedView );
    const auto decoded = data.getLinesRaw( 0_lnum, 2_lcount ).decodeLines();
    REQUIRE( decoded.size() == 2 );
    CHECK( decoded[ 0 ] == "a" );
    CHECK( decoded[ 1 ].toUtf8() == expected );
    const auto location = snapshot.location( 1_lnum, SourceSpan{ 1, 2 } );
    REQUIRE( location );
    REQUIRE( location->span );
    CHECK( location->span->start == 1 );
    CHECK( location->span->length == 2 );
    CHECK( snapshot.accepts( *location ) );
    CHECK_FALSE(
        snapshot.location( 0_lnum, SourceSpan{ std::numeric_limits<quint64>::max(), 1 } ) );
    auto future = *location;
    ++future.revision;
    CHECK_FALSE( snapshot.accepts( future ) );

    finished.clear();
    data.reload( QTextCodec::codecForName( "UTF-8" ) );
    REQUIRE_FALSE( data.isSourceSnapshotCurrent( snapshot ) );
    REQUIRE( finished.safeWait() );
    CHECK( data.sourceSnapshot().generation > snapshot.generation );
    CHECK_FALSE( data.sourceSnapshot().accepts( *location ) );
}

TEST_CASE( "Empty and interrupted sources do not fabricate analysis locations",
           "[logdata][source]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "source.log" );
    LogData data;
    SafeQSignalSpy finished( &data, SIGNAL( loadingFinished( LoadingStatus ) ) );
    SECTION( "Empty indexed file has an available empty watermark" )
    {
        writeSourceData( path, {} );
        data.attachFile( path );
        REQUIRE( finished.safeWait() );
        const auto snapshot = data.sourceSnapshot();
        CHECK( snapshot.available );
        CHECK( snapshot.indexedBytes == 0 );
        CHECK( snapshot.lineCount == 0_lcount );
        CHECK_FALSE( snapshot.provisionalLineStart );
        CHECK_FALSE( snapshot.location( 0_lnum ) );
    }
    SECTION( "Interrupted indexing does not publish a usable snapshot" )
    {
        writeSourceData( path, QByteArray( 8 * 1024 * 1024, 'x' ) );
        data.attachFile( path );
        data.interruptLoading();
        REQUIRE( finished.safeWait() );
        CHECK_FALSE( data.sourceSnapshot().available );
        CHECK_FALSE( data.sourceSnapshot().location( 0_lnum ) );
    }
}

TEST_CASE( "Superseded indexing cannot publish an old source watermark", "[logdata][source]" )
{
    IndexingData data;
    IndexingData::MutateAccessor access{ &data };
    access.clear();
    const auto oldUpdate = access.beginSourceUpdate();
    access.invalidateSource( "test-reload-requested" );
    const auto newUpdate = access.beginSourceUpdate();
    REQUIRE_FALSE( access.finishSourceUpdate( oldUpdate ) );
    REQUIRE_FALSE( access.sourceSnapshot().available );
    REQUIRE( access.finishSourceUpdate( newUpdate ) );
    REQUIRE( access.sourceSnapshot().available );
    const auto closedUpdate = access.beginSourceUpdate();
    access.closeSource();
    REQUIRE_FALSE( access.finishSourceUpdate( closedUpdate ) );
    REQUIRE_FALSE( access.sourceSnapshot().available );
}
