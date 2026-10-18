/*
 * Copyright (C) 2026 CILogg contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch.hpp>

#include <limits>
#include <type_traits>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "logdata.h"
#include "test_utils.h"

namespace {
enum class ReadFault { Error, Short, Eof };

class FailingReadFile : public QFile {
public:
    FailingReadFile( const QString& path, int failOnRead, ReadFault fault,
                     std::shared_ptr<qint64> observedRead )
        : QFile( path )
        , failOnRead_( failOnRead )
        , fault_( fault )
        , observedRead_( std::move( observedRead ) )
    {
    }

protected:
    qint64 readData( char* bytes, qint64 size ) override
    {
        if ( ++readCount_ == failOnRead_ ) {
            switch ( fault_ ) {
            case ReadFault::Error:
                setErrorString( "injected read failure" );
                *observedRead_ = -1;
                break;
            case ReadFault::Short:
                *observedRead_ = QFile::readData( bytes, size / 2 );
                break;
            case ReadFault::Eof:
                *observedRead_ = 0;
                break;
            }
            return *observedRead_;
        }
        return QFile::readData( bytes, size );
    }

private:
    int failOnRead_;
    ReadFault fault_;
    std::shared_ptr<qint64> observedRead_;
    int readCount_ = 0;
};

template <typename Operation>
class FailingReadOperation : public Operation {
public:
    FailingReadOperation( const QString& path, const std::shared_ptr<IndexingData>& data,
                          AtomicFlag& interrupt, int failOnRead, ReadFault fault,
                          std::shared_ptr<qint64> observedRead )
        : Operation( path, data, interrupt )
        , failOnRead_( failOnRead )
        , fault_( fault )
        , observedRead_( std::move( observedRead ) )
    {
    }

protected:
    std::unique_ptr<QFile> createFile() const override
    {
        return std::make_unique<FailingReadFile>( this->fileName_, failOnRead_, fault_,
                                                  observedRead_ );
    }

private:
    int failOnRead_;
    ReadFault fault_;
    std::shared_ptr<qint64> observedRead_;
};

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
    auto futureSnapshot = snapshot;
    ++futureSnapshot.revision;
    const auto future = futureSnapshot.location( 1_lnum );
    REQUIRE( future );
    CHECK_FALSE( snapshot.accepts( *future ) );

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

TEST_CASE( "Short source reads discard incomplete indexed batches", "[logdata][source][review]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "short.log" );
    writeSourceData( path, "first\nsecond\n" );
    LogData data;
    SafeQSignalSpy finished( &data, SIGNAL( loadingFinished( LoadingStatus ) ) );
    data.attachFile( path );
    REQUIRE( finished.safeWait() );
    // Do not process watcher events: the line offsets still describe the old bytes.
    writeSourceData( path, "fir" );
    const auto raw = data.getLinesRaw( 0_lnum, 2_lcount );
    CHECK( raw.buffer.empty() );
    CHECK( raw.endOfLines.empty() );
    CHECK( raw.decodeLines().empty() );
    CHECK( raw.buildUtf8View().empty() );
}

TEST_CASE( "Locations reject malformed spans and inconsistent completion status",
           "[logdata][source][review]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "locations.log" );
    writeSourceData( path, "first\npartial" );
    LogData data;
    SafeQSignalSpy finished( &data, SIGNAL( loadingFinished( LoadingStatus ) ) );
    data.attachFile( path );
    REQUIRE( finished.safeWait() );
    const auto snapshot = data.sourceSnapshot();
    auto location = snapshot.location( 0_lnum, SourceSpan{ 0, 1 } );
    REQUIRE( location );
    location->span = SourceSpan{ std::numeric_limits<quint64>::max(), 1 };
    CHECK_FALSE( snapshot.accepts( *location ) );
    location->span = SourceSpan{ 0, 1 };
    auto inconsistent = snapshot;
    inconsistent.completeLineCount = 0_lcount;
    CHECK_FALSE( inconsistent.accepts( *location ) );
    // Only the snapshot factory may create provenance; consumers can edit validated spans.
    CHECK_FALSE( std::is_default_constructible_v<SourceLocation> );
}

TEST_CASE( "A late file check cannot replace an explicit forced reload",
           "[logdata][source][review]" )
{
    const auto newestUtf8 = GENERATE( false, true );
    CAPTURE( newestUtf8 );
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "reload.log" );
    writeSourceData( path, "a\nb\n" );
    LogData data;
    SafeQSignalSpy finished( &data, SIGNAL( loadingFinished( LoadingStatus ) ) );
    data.attachFile( path );
    REQUIRE( finished.safeWait() );
    REQUIRE( data.getNbLine() == 2_lcount );
    finished.clear();
    writeSourceData( path, "c\n", QIODevice::WriteOnly | QIODevice::Append );
    // The queued check completion cannot run until the GUI event loop resumes below.
    notifySourceChanged( data, path );
    data.reload( QTextCodec::codecForName( "UTF-16LE" ) );
    if ( newestUtf8 ) {
        data.reload( QTextCodec::codecForName( "UTF-8" ) );
    }
    notifySourceChanged( data, path );
    REQUIRE( finished.safeWait() );
    // UTF-16LE has no LF code point in these byte pairs; a stale ASCII partial index has 3.
    CHECK( data.getNbLine() == LinesCount{ newestUtf8 ? 3u : 1u } );
    CHECK( data.sourceSnapshot().available );
}

TEST_CASE( "Decoding reindex never exposes an available old-layout snapshot",
           "[logdata][source][review]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "decode-reindex.log" );
    writeSourceData( path, "a\nb\n" );
    LogData data;
    SafeQSignalSpy finished( &data, SIGNAL( loadingFinished( LoadingStatus ) ) );
    data.attachFile( path );
    REQUIRE( finished.safeWait() );
    const auto before = data.sourceSnapshot();
    finished.clear();
    data.setDisplayEncoding( "UTF-16LE" );
    const auto pending = data.sourceSnapshot();
    CHECK_FALSE( data.isSourceSnapshotCurrent( before ) );
    if ( pending.available ) {
        CHECK( pending.lineCount == 1_lcount );
    }
    REQUIRE( finished.safeWait() );
    const auto indexed = data.sourceSnapshot();
    REQUIRE( indexed.available );
    CHECK( indexed.lineCount == 1_lcount );
    CHECK( indexed.generation > before.generation );
}

TEST_CASE( "Read failures never publish successful full or partial indexes",
           "[logdata][source][review]" )
{
    const auto partial = GENERATE( false, true );
    // First/later data blocks, header hash, tail hash.
    const auto faultCase
        = GENERATE( std::make_pair( 1, ReadFault::Error ), std::make_pair( 2, ReadFault::Error ),
                    std::make_pair( 3, ReadFault::Error ), std::make_pair( 4, ReadFault::Error ),
                    std::make_pair( 3, ReadFault::Short ), std::make_pair( 4, ReadFault::Short ),
                    std::make_pair( 3, ReadFault::Eof ), std::make_pair( 4, ReadFault::Eof ) );
    const auto failOnRead = faultCase.first;
    const auto fault = faultCase.second;
    CAPTURE( partial, failOnRead, static_cast<int>( fault ) );
    auto observedRead = std::make_shared<qint64>( -2 );
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "read-error.log" );
    writeSourceData( path, "initial\n" );
    auto data = std::make_shared<IndexingData>();
    AtomicFlag interrupt;
    if ( partial ) {
        FullIndexOperation initial( path, data, interrupt );
        REQUIRE( std::get<bool>( initial.run() ) );
        REQUIRE( IndexingData::ConstAccessor{ data.get() }.sourceSnapshot().available );
    }
    writeSourceData( path, QByteArray( 6 * 1024 * 1024, 'x' ),
                     QIODevice::WriteOnly | ( partial ? QIODevice::Append : QIODevice::Truncate ) );
    std::unique_ptr<IndexOperation> operation;
    if ( partial ) {
        operation = std::make_unique<FailingReadOperation<PartialIndexOperation>>(
            path, data, interrupt, failOnRead, fault, observedRead );
    }
    else {
        operation = std::make_unique<FailingReadOperation<FullIndexOperation>>(
            path, data, interrupt, failOnRead, fault, observedRead );
    }
    QSignalSpy finished( operation.get(), &IndexOperation::indexingFinished );
    CHECK_FALSE( std::get<bool>( operation->run() ) );
    if ( fault == ReadFault::Error ) {
        CHECK( *observedRead == -1 );
    }
    else if ( fault == ReadFault::Short ) {
        CHECK( *observedRead > 0 );
    }
    else {
        CHECK( *observedRead == 0 );
    }
    REQUIRE( finished.count() == 1 );
    CHECK_FALSE( finished.takeFirst().front().toBool() );
    const auto snapshot = IndexingData::ConstAccessor{ data.get() }.sourceSnapshot();
    CHECK_FALSE( snapshot.available );
    CHECK( snapshot.indexedBytes == 0 );
    CHECK( snapshot.lineCount == 0_lcount );
}
