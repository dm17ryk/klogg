// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sharedwriter.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <catch2/catch.hpp>
#include <limits>

using namespace cilogg::communication;

namespace {
StreamLimits writerLimits()
{
    StreamLimitsDefinition definition;
    definition.maximumWriteBytes = 32;
    definition.maximumChunkBytes = 16;
    definition.maximumPendingTxBytes = 64;
    definition.maximumReceipts = 3;
    auto result = StreamLimits::fromDefinition( definition );
    REQUIRE( result.value );
    return *result.value;
}

EndpointCapabilities writerCapabilities()
{
    EndpointCapabilitiesDefinition definition;
    definition.canWrite = true;
    auto result = EndpointCapabilities::fromDefinition( definition );
    REQUIRE( result.value );
    return *result.value;
}
std::unique_ptr<SharedWriter> writer( qint64 timeout = 100 )
{
    auto result
        = SharedWriter::create( { "ep", 7 }, writerLimits(), writerCapabilities(), timeout );
    REQUIRE( result.value );
    return std::move( *result.value );
}
WriteRequest request( const QString& id, const QByteArray& bytes = "abc", bool sensitive = false,
                      quint64 generation = 7, std::optional<qint64> deadline = {} )
{
    auto result = WriteRequest::fromDefinition( { id, bytes, sensitive, generation, deadline },
                                                writerLimits() );
    REQUIRE( result.value );
    return *result.value;
}
const WriteEpoch epoch{ "ep", 7 };
WriteOffer offer( SharedWriter& w, const WriteEpoch& e = epoch, qint64 now = 0 )
{
    auto result = w.take( e, now );
    REQUIRE( result.value );
    REQUIRE( result.value->offer );
    return *result.value->offer;
}
WriteObservation observation( SharedWriter& w, const QString& id )
{
    auto result = w.getWrite( id );
    REQUIRE( result.value );
    return *result.value;
}
void idle( SharedWriter& w, const WriteEpoch& e = epoch, qint64 now = 0 )
{
    auto result = w.take( e, now );
    REQUIRE( result.value );
    CHECK_FALSE( result.value->offer );
}
void complete( SharedWriter& w )
{
    auto part = offer( w );
    REQUIRE( w.accepted( part.ticket(), part.bytes().size(), 0 ).ok );
    REQUIRE( w.written( part.ticket(), part.bytes().size(), 0 ).ok );
}
QStringList diagnosticMessages;
QLoggingCategory::CategoryFilter originalCategoryFilter = nullptr;
void enableWriterDiagnostics( QLoggingCategory* category )
{
    if ( originalCategoryFilter )
        originalCategoryFilter( category );
    if ( qstrcmp( category->categoryName(), "cilogg.communication.writer" ) == 0 )
        category->setEnabled( QtDebugMsg, true );
}
void captureDiagnostics( QtMsgType, const QMessageLogContext& context, const QString& message )
{
    if ( QString::fromLatin1( context.category ) == "cilogg.communication.writer" )
        diagnosticMessages.push_back( message );
}
struct DiagnosticCapture {
    QtMessageHandler previous = qInstallMessageHandler( captureDiagnostics );
    QLoggingCategory::CategoryFilter previousFilter = nullptr;
    DiagnosticCapture()
    {
        diagnosticMessages.clear();
        previousFilter = QLoggingCategory::installFilter( enableWriterDiagnostics );
        originalCategoryFilter = previousFilter;
    }
    ~DiagnosticCapture()
    {
        QLoggingCategory::installFilter( previousFilter );
        originalCategoryFilter = nullptr;
        qInstallMessageHandler( previous );
    }
};
} // namespace

TEST_CASE( "Shared writer owns borrowed endpoint identity", "[communication][D01b-factory]" )
{
    QChar borrowed[] = { QChar( 'e' ), QChar( 'p' ) };
    auto result = SharedWriter::create( { QString::fromRawData( borrowed, 2 ), 7 }, writerLimits(),
                                        writerCapabilities(), 100 );
    REQUIRE( result.value );
    REQUIRE( *result.value );
    borrowed[ 0 ] = QChar( 'x' );
    const auto state = ( *result.value )->state();
    CHECK( state.epoch.endpointId == "ep" );
    CHECK( state.epoch.generation == 7 );
    CHECK( state.mode == WriteMode::Automatic );
    CHECK_FALSE( state.pendingMode );
    CHECK( state.residentBytes == 0 );
    CHECK( state.pendingRequests == 0 );
    CHECK_FALSE( state.resetRequired );
}

TEST_CASE( "Endpoint backpressure survives cancellation and expiry of its request",
           "[communication][D01b-backpressure]" )
{
    for ( bool cancelled : { true, false } ) {
        auto w = writer();
        REQUIRE( w->submit( epoch, request( "a", "a", false, 7, 10 ), WriteProducer::Automation, 0 )
                     .value );
        REQUIRE(
            w->submit( epoch, request( "b", "b", false, 7, 1000 ), WriteProducer::Automation, 0 )
                .value );
        const auto a = offer( *w );
        REQUIRE( w->accepted( a.ticket(), 0, 0 ).ok );
        const qint64 now = cancelled ? 0 : 10;
        if ( cancelled )
            REQUIRE( w->cancel( epoch, "a", now ).ok );
        else
            REQUIRE( w->advance( epoch, now ).ok );
        REQUIRE( w->state().backpressured );
        idle( *w, epoch, now );
        REQUIRE( w->ready( epoch, now ).ok );
        CHECK( offer( *w, epoch, now ).requestId() == "b" );
    }
}

TEST_CASE( "Invalid producer and one-entry policy remain bounded",
           "[communication][D01b-admission]" )
{
    auto w = writer();
    CHECK( w->submit( epoch, request( "a" ), static_cast<WriteProducer>( 99 ), 90 ).error.code
           == "invalid_producer" );
    REQUIRE( w->submit( epoch, request( "a" ), WriteProducer::Automation, 0 ).value );
    StreamLimitsDefinition definition;
    definition.maximumReceipts = 1;
    auto limits = StreamLimits::fromDefinition( definition );
    REQUIRE( limits.value );
    auto one = SharedWriter::create( epoch, *limits.value, writerCapabilities(), 100 );
    REQUIRE( one.value );
    REQUIRE( ( *one.value )->submit( epoch, request( "a" ), WriteProducer::Automation, 0 ).value );
    CHECK( ( *one.value )->submit( epoch, request( "b" ), WriteProducer::Automation, 0 ).error.code
           == "queue_full" );
    CHECK( ( *one.value )->state().pendingRequests == 1 );
}

TEST_CASE( "Cancellation preserves issued and accepted drain barriers",
           "[communication][D01b-barriers]" )
{
    SECTION( "queued cancellation is final and idempotent" )
    {
        auto w = writer();
        REQUIRE( w->submit( epoch, request( "a" ), WriteProducer::Automation, 0 ).value );
        REQUIRE( w->cancel( epoch, "a", 0 ).ok );
        REQUIRE( w->cancel( epoch, "a", 0 ).ok );
        CHECK( observation( *w, "a" ).receipt.stage() == WriteStage::Cancelled );
        CHECK( w->state().pendingRequests == 0 );
        CHECK( w->state().retainedBytes == 3 );
        auto retry = w->submit( epoch, request( "a" ), WriteProducer::Automation, 0 );
        REQUIRE( retry.value );
        CHECK( retry.value->receipt.stage() == WriteStage::Cancelled );
        idle( *w );
    }
    for ( bool acceptedFirst : { false, true } ) {
        auto w = writer();
        REQUIRE( w->submit( epoch, request( "a", "ABCDE" ), WriteProducer::Automation, 0 ).value );
        REQUIRE( w->submit( epoch, request( "z", "Z" ), WriteProducer::Automation, 0 ).value );
        auto a = offer( *w );
        if ( acceptedFirst )
            REQUIRE( w->accepted( a.ticket(), 2, 0 ).ok );
        REQUIRE( w->cancel( epoch, "a", 0 ).ok );
        idle( *w );
        if ( !acceptedFirst )
            REQUIRE( w->accepted( a.ticket(), 2, 0 ).ok );
        idle( *w );
        REQUIRE( w->written( a.ticket(), 1, 0 ).ok );
        idle( *w );
        REQUIRE( w->written( a.ticket(), 1, 0 ).ok );
        const auto final = observation( *w, "a" );
        CHECK( final.receipt.stage() == WriteStage::Cancelled );
        CHECK( final.receipt.acceptedBytes() == 2 );
        CHECK( final.receipt.writtenBytes() == 2 );
        CHECK_FALSE( final.awaitingDrain );
        CHECK( offer( *w ).requestId() == "z" );
    }
}

TEST_CASE( "Deadlines include queue and backpressure without renewing retries",
           "[communication][D01b-barriers]" )
{
    auto w = writer();
    REQUIRE( w->submit( epoch, request( "a" ), WriteProducer::Automation, 10 ).value );
    REQUIRE( w->submit( epoch, request( "a" ), WriteProducer::Automation, 50 ).value );
    CHECK_FALSE( w->advance( { "ep", 6 }, 1000000 ).ok );
    REQUIRE( w->advance( epoch, 109 ).ok );
    CHECK( observation( *w, "a" ).receipt.stage() == WriteStage::Queued );
    REQUIRE( w->advance( epoch, 110 ).ok );
    CHECK( observation( *w, "a" ).receipt.errorCode() == "deadline_exceeded" );
    auto blocked = writer();
    REQUIRE( blocked->submit( epoch, request( "a" ), WriteProducer::Automation, 0 ).value );
    auto a = offer( *blocked );
    REQUIRE( blocked->accepted( a.ticket(), 0, 0 ).ok );
    REQUIRE( blocked->ready( epoch, 100 ).ok );
    idle( *blocked, epoch, 100 );
    CHECK( observation( *blocked, "a" ).receipt.errorCode() == "deadline_exceeded" );
    auto issued = writer();
    REQUIRE( issued->submit( epoch, request( "a" ), WriteProducer::Automation, 0 ).value );
    REQUIRE(
        issued->submit( epoch, request( "b", "b", false, 7, 1000 ), WriteProducer::Automation, 0 )
            .value );
    auto held = offer( *issued );
    REQUIRE( issued->advance( epoch, 100 ).ok );
    CHECK( issued->state().resetRequired );
    CHECK( issued->state().nextWakeNs == 1000 );
    idle( *issued, epoch, 100 );
    REQUIRE( issued->accepted( held.ticket(), 1, 100 ).ok );
    REQUIRE( issued->written( held.ticket(), 1, 100 ).ok );
    CHECK( offer( *issued, epoch, 100 ).requestId() == "b" );
    auto overflow = writer( std::numeric_limits<qint64>::max() );
    CHECK( overflow->submit( epoch, request( "a" ), WriteProducer::Automation, 1 ).error.code
           == "invalid_deadline" );
    REQUIRE( overflow
                 ->submit( epoch, request( "b", "b", false, 7, std::numeric_limits<qint64>::max() ),
                           WriteProducer::Automation, 1 )
                 .value );
}

TEST_CASE( "Backend rejection never retries a failed continuation",
           "[communication][D01b-barriers]" )
{
    auto w = writer();
    REQUIRE( w->submit( epoch, request( "a", "ABCDE" ), WriteProducer::Automation, 0 ).value );
    REQUIRE( w->submit( epoch, request( "z", "Z" ), WriteProducer::Automation, 0 ).value );
    auto first = offer( *w );
    REQUIRE( w->accepted( first.ticket(), 2, 0 ).ok );
    REQUIRE( w->written( first.ticket(), 2, 0 ).ok );
    auto rest = offer( *w );
    CHECK( rest.bytes() == "CDE" );
    REQUIRE( w->accepted( rest.ticket(), -1, 0 ).ok );
    auto final = observation( *w, "a" );
    CHECK( final.receipt.stage() == WriteStage::Failed );
    CHECK( final.receipt.errorCode() == "io_error" );
    CHECK( final.receipt.acceptedBytes() == 2 );
    CHECK( final.receipt.writtenBytes() == 2 );
    CHECK_FALSE( final.awaitingAcceptance );
    CHECK_FALSE( final.ioUnconfirmed );
    CHECK( w->state().faulted );
    CHECK( observation( *w, "z" ).receipt.stage() == WriteStage::Failed );
    idle( *w );
}

TEST_CASE( "Generic I/O failure does not invent an acceptance result",
           "[communication][D01b-barriers]" )
{
    auto w = writer();
    REQUIRE( w->submit( epoch, request( "a" ), WriteProducer::Automation, 0 ).value );
    auto a = offer( *w );
    REQUIRE( w->failed( a.ticket(), 0 ).ok );
    REQUIRE( w->failed( a.ticket(), 0 ).ok );
    CHECK( observation( *w, "a" ).awaitingAcceptance );
    CHECK( w->state().resetRequired );
    idle( *w );
    CHECK_FALSE( w->submit( epoch, request( "b" ), WriteProducer::Automation, 0 ).value );
    REQUIRE( w->accepted( a.ticket(), 2, 0 ).ok );
    REQUIRE( w->written( a.ticket(), 1, 0 ).ok );
    CHECK( observation( *w, "a" ).awaitingDrain );
    REQUIRE( w->close( epoch, 0 ).ok );
    REQUIRE( w->resetAfterBackendClosed( epoch, 8, writerCapabilities(), 0 ).ok );
    auto historical = observation( *w, "a" );
    CHECK( historical.receipt.acceptedBytes() == 2 );
    CHECK( historical.receipt.writtenBytes() == 1 );
    CHECK( historical.ioUnconfirmed );
    CHECK_FALSE( historical.awaitingDrain );
}

TEST_CASE( "Manual arming waits for old I/O and preserves producer arbitration",
           "[communication][D01b-barriers]" )
{
    auto w = writer();
    REQUIRE( w->submit( epoch, request( "a" ), WriteProducer::Automation, 0 ).value );
    auto a = offer( *w );
    REQUIRE( w->accepted( a.ticket(), 3, 0 ).ok );
    REQUIRE( w->requestMode( epoch, WriteMode::ManualArmed, 0 ).ok );
    REQUIRE( w->requestMode( epoch, WriteMode::ManualArmed, 0 ).ok );
    CHECK( w->state().mode == WriteMode::Automatic );
    CHECK( w->state().pendingMode == WriteMode::ManualArmed );
    CHECK_FALSE( w->requestMode( epoch, WriteMode::Automatic, 0 ).ok );
    CHECK_FALSE( w->submit( epoch, request( "b" ), WriteProducer::Manual, 0 ).value );
    idle( *w );
    REQUIRE( w->written( a.ticket(), 3, 0 ).ok );
    CHECK( w->state().mode == WriteMode::ManualArmed );
    CHECK_FALSE( w->state().pendingMode );
    CHECK_FALSE( w->submit( epoch, request( "b" ), WriteProducer::Automation, 0 ).value );
    REQUIRE( w->submit( epoch, request( "b" ), WriteProducer::Manual, 0 ).value );
    REQUIRE( w->requestMode( epoch, WriteMode::Automatic, 0 ).ok );
    CHECK( w->state().mode == WriteMode::Automatic );
    CHECK( observation( *w, "b" ).receipt.stage() == WriteStage::Cancelled );
    CHECK_FALSE( w->requestMode( epoch, static_cast<WriteMode>( 99 ), 90 ).ok );
    REQUIRE( w->advance( epoch, 0 ).ok );
}

TEST_CASE( "Confirmed retirement fences old tickets and preserves uncertainty",
           "[communication][D01b-barriers]" )
{
    for ( bool acceptedFirst : { false, true } ) {
        auto w = writer();
        REQUIRE( w->submit( epoch, request( "a" ), WriteProducer::Automation, 0 ).value );
        auto a = offer( *w );
        if ( acceptedFirst ) {
            REQUIRE( w->accepted( a.ticket(), 3, 0 ).ok );
            REQUIRE( w->written( a.ticket(), 1, 0 ).ok );
        }
        CHECK_FALSE( w->resetAfterBackendClosed( epoch, 8, writerCapabilities(), 90 ).ok );
        REQUIRE( w->close( epoch, 0 ).ok );
        REQUIRE( w->close( epoch, 0 ).ok );
        CHECK( w->state().closed );
        CHECK_FALSE( w->resetAfterBackendClosed( epoch, 7, writerCapabilities(), 90 ).ok );
        REQUIRE( w->resetAfterBackendClosed( epoch, 8, writerCapabilities(), 0 ).ok );
        const auto old = observation( *w, "a" );
        CHECK( old.receipt.stage() == WriteStage::Cancelled );
        CHECK( old.ioUnconfirmed );
        CHECK( old.receipt.acceptedBytes() == ( acceptedFirst ? 3 : 0 ) );
        CHECK( old.receipt.writtenBytes() == ( acceptedFirst ? 1 : 0 ) );
        CHECK( w->state().mode == WriteMode::Automatic );
        CHECK_FALSE( w->state().closed );
        const WriteEpoch next{ "ep", 8 };
        REQUIRE( w->submit( next, request( "new", "abc", false, 8 ), WriteProducer::Automation, 0 )
                     .value );
        const auto b = offer( *w, next );
        CHECK( b.ticket().sequence == a.ticket().sequence );
        CHECK_FALSE( w->accepted( a.ticket(), 3, 1000000 ).ok );
        CHECK_FALSE( w->written( a.ticket(), 1, 1000000 ).ok );
        CHECK_FALSE( w->ready( epoch, 1000000 ).ok );
        REQUIRE( w->accepted( b.ticket(), 3, 0 ).ok );
        REQUIRE( w->written( b.ticket(), 3, 0 ).ok );
    }
}

TEST_CASE( "Retained identities expire at a fixed boundary without query renewal",
           "[communication][D01b-retention]" )
{
    auto w = writer();
    REQUIRE( w->submit( epoch, request( "a" ), WriteProducer::Automation, 0 ).value );
    complete( *w );
    REQUIRE( w->cancel( epoch, "a", 0 ).ok );
    CHECK( observation( *w, "a" ).receipt.stage() == WriteStage::WrittenToBackend );
    const qint64 expiry = writerLimits().receiptRetentionMs() * 1000000;
    REQUIRE( w->advance( epoch, expiry - 1 ).ok );
    CHECK( w->getWrite( "a" ).value );
    CHECK( w->state().nextWakeNs == expiry );
    REQUIRE( w->advance( epoch, expiry ).ok );
    CHECK( w->getWrite( "a" ).error.code == "not_found" );
    REQUIRE( w->submit( epoch, request( "a" ), WriteProducer::Automation, expiry ).value );
    CHECK( observation( *w, "a" ).receipt.stage() == WriteStage::Queued );
}

TEST_CASE( "Finished eviction is FIFO and never discards barrier-held state",
           "[communication][D01b-retention]" )
{
    auto w = writer();
    REQUIRE( w->submit( epoch, request( "a", QByteArray( 32, 'a' ) ), WriteProducer::Automation, 0 )
                 .value );
    REQUIRE( w->submit( epoch, request( "b", QByteArray( 32, 'b' ) ), WriteProducer::Automation, 0 )
                 .value );
    // Two chunks complete a; b remains active and cannot be discarded.
    complete( *w );
    complete( *w );
    REQUIRE( w->submit( epoch, request( "c", QByteArray( 32, 'c' ) ), WriteProducer::Automation, 0 )
                 .value );
    CHECK( w->getWrite( "a" ).error.code == "not_found" );
    CHECK( w->getWrite( "b" ).value );
    CHECK( w->state().residentBytes == 64 );
    auto b = offer( *w );
    REQUIRE( w->cancel( epoch, "b", 0 ).ok );
    CHECK( w->submit( epoch, request( "d" ), WriteProducer::Automation, 0 ).error.code
           == "queue_full" );
    CHECK( observation( *w, "b" ).awaitingAcceptance );
    REQUIRE( w->accepted( b.ticket(), 0, 0 ).ok );
    CHECK( w->state().residentBytes == 64 );
    REQUIRE( w->submit( epoch, request( "d" ), WriteProducer::Automation, 0 ).value );
    CHECK( w->getWrite( "b" ).error.code == "not_found" );
    auto schedule = writer();
    for ( int n = 0; n < 30; ++n ) {
        const auto id = QString::number( n );
        REQUIRE( schedule->submit( epoch, request( id ), WriteProducer::Automation, 0 ).value );
        complete( *schedule );
        const auto state = schedule->state();
        CHECK( state.residentBytes <= 64 );
        CHECK( state.pendingRequests + state.retainedRequests <= 3 );
        CHECK( state.pendingBytes + state.retainedBytes == state.residentBytes );
    }
    CHECK( schedule->getWrite( "0" ).error.code == "not_found" );
    CHECK( schedule->getWrite( "29" ).value );
}

TEST_CASE( "Private bytes are exact while receipts and real diagnostics omit payloads",
           "[communication][D01b-privacy]" )
{
    DiagnosticCapture capture;
    QByteArray secret( "D01b_PRIVATE_probe_7f3a" );
    secret.append( '\0' );
    secret.append( static_cast<char>( 0xff ) );
    auto w = writer();
    REQUIRE( w->submit( epoch, request( "private", secret, true ), WriteProducer::Automation, 0 )
                 .value );
    QByteArray backend;
    while ( backend.size() < secret.size() ) {
        auto part = offer( *w );
        backend += part.bytes();
        REQUIRE( w->accepted( part.ticket(), part.bytes().size(), 0 ).ok );
        REQUIRE( w->written( part.ticket(), part.bytes().size(), 0 ).ok );
    }
    CHECK( backend == secret );
    const auto r = observation( *w, "private" );
    CHECK( r.receipt.payloadDisposition() == PayloadDisposition::Redacted );
    auto json = encodeWriteReceipt( r.receipt, writerLimits() );
    REQUIRE( json.value );
    const auto metadata = QJsonDocument( *json.value ).toJson( QJsonDocument::Compact );
    REQUIRE_FALSE( diagnosticMessages.isEmpty() );
    const auto logs = diagnosticMessages.join( '\n' ).toUtf8();
    for ( const auto& representation :
          { secret, QByteArray( "D01b_PRIVATE_probe_7f3a" ), secret.toHex(), secret.toHex( ' ' ),
            secret.toBase64(),
            QCryptographicHash::hash( secret, QCryptographicHash::Sha256 ).toHex() } ) {
        CHECK_FALSE( metadata.contains( representation ) );
        CHECK_FALSE( logs.contains( representation ) );
    }
}

TEST_CASE( "Partial acceptance and drain preserve exact FIFO bytes",
           "[communication][D01b-progress]" )
{
    auto w = writer();
    REQUIRE( w->submit( epoch, request( "a", "ABCDE" ), WriteProducer::Automation, 0 ).value );
    REQUIRE( w->submit( epoch, request( "z", "Z" ), WriteProducer::Automation, 0 ).value );
    auto a = offer( *w );
    CHECK( a.bytes() == "ABCDE" );
    CHECK( a.offset() == 0 );
    idle( *w );
    CHECK( observation( *w, "a" ).awaitingAcceptance );
    REQUIRE( w->accepted( a.ticket(), 2, 0 ).ok );
    CHECK( observation( *w, "a" ).awaitingDrain );
    CHECK( observation( *w, "a" ).receipt.writtenBytes() == 0 );
    idle( *w );
    QByteArray wire = a.bytes().left( 2 );
    REQUIRE( w->written( a.ticket(), 1, 0 ).ok );
    idle( *w );
    REQUIRE( w->written( a.ticket(), 1, 0 ).ok );
    auto b = offer( *w );
    CHECK( b.bytes() == "CDE" );
    CHECK( b.offset() == 2 );
    REQUIRE( w->accepted( b.ticket(), 0, 0 ).ok );
    CHECK( w->state().backpressured );
    idle( *w );
    idle( *w );
    REQUIRE( w->ready( epoch, 0 ).ok );
    auto c = offer( *w );
    CHECK( c.bytes() == "CDE" );
    CHECK( c.ticket().sequence != b.ticket().sequence );
    REQUIRE( w->accepted( c.ticket(), 3, 0 ).ok );
    wire += c.bytes();
    idle( *w );
    REQUIRE( w->written( c.ticket(), 3, 0 ).ok );
    CHECK( wire == "ABCDE" );
    CHECK( observation( *w, "a" ).receipt.stage() == WriteStage::WrittenToBackend );
    CHECK( w->state().retainedRequests == 1 );
    auto z = offer( *w );
    CHECK( z.requestId() == "z" );
    CHECK( z.bytes() == "Z" );
    REQUIRE( w->accepted( z.ticket(), 1, 0 ).ok );
    REQUIRE( w->written( z.ticket(), 1, 0 ).ok );
    idle( *w );
}

TEST_CASE( "Invalid acknowledgments cannot advance bytes or clock",
           "[communication][D01b-progress]" )
{
    auto w = writer();
    REQUIRE( w->submit( epoch, request( "a" ), WriteProducer::Automation, 0 ).value );
    const auto a = offer( *w );
    auto wrong = a.ticket();
    ++wrong.sequence;
    CHECK_FALSE( w->accepted( wrong, 1, 90 ).ok );
    wrong = a.ticket();
    wrong.epoch.endpointId = "other";
    CHECK_FALSE( w->accepted( wrong, 1, 90 ).ok );
    wrong = a.ticket();
    --wrong.epoch.generation;
    CHECK_FALSE( w->accepted( wrong, 1, 90 ).ok );
    CHECK_FALSE( w->accepted( a.ticket(), 4, 90 ).ok );
    CHECK_FALSE( w->accepted( a.ticket(), -2, 90 ).ok );
    CHECK_FALSE( w->written( a.ticket(), 1, 90 ).ok );
    CHECK_FALSE( w->accepted( a.ticket(), 1, -1 ).ok );
    REQUIRE( w->accepted( a.ticket(), 3, 0 ).ok );
    CHECK_FALSE( w->accepted( a.ticket(), 3, 90 ).ok );
    CHECK_FALSE( w->written( a.ticket(), 0, 90 ).ok );
    CHECK_FALSE( w->written( a.ticket(), -1, 90 ).ok );
    CHECK_FALSE( w->written( a.ticket(), 4, 90 ).ok );
    REQUIRE( w->written( a.ticket(), 3, 0 ).ok );
    CHECK_FALSE( w->written( a.ticket(), 1, 90 ).ok );
    CHECK( observation( *w, "a" ).receipt.writtenBytes() == 3 );
}

TEST_CASE( "Every binary byte survives bounded multichunk offers",
           "[communication][D01b-progress]" )
{
    StreamLimitsDefinition definition;
    definition.maximumChunkBytes = 16;
    auto limits = StreamLimits::fromDefinition( definition );
    REQUIRE( limits.value );
    auto created = SharedWriter::create( epoch, *limits.value, writerCapabilities(), 100 );
    REQUIRE( created.value );
    auto& w = **created.value;
    QByteArray expected;
    for ( int n = 0; n < 256; ++n )
        expected.append( static_cast<char>( n ) );
    auto r = WriteRequest::fromDefinition( { "binary", expected, false, 7, {} }, *limits.value );
    REQUIRE( r.value );
    REQUIRE( w.submit( epoch, *r.value, WriteProducer::Automation, 0 ).value );
    QByteArray wire;
    while ( wire.size() < expected.size() ) {
        auto part = offer( w );
        REQUIRE( part.bytes().size() <= 16 );
        CHECK( part.offset() == static_cast<quint64>( wire.size() ) );
        REQUIRE( w.accepted( part.ticket(), part.bytes().size(), 0 ).ok );
        wire += part.bytes();
        REQUIRE( w.written( part.ticket(), part.bytes().size(), 0 ).ok );
    }
    CHECK( wire == expected );
    CHECK( observation( w, "binary" ).receipt.stage() == WriteStage::WrittenToBackend );
}

TEST_CASE( "Writer bounds admission and preserves idempotent request identity",
           "[communication][D01b-admission]" )
{
    auto w = writer();
    const auto a = request( "a", QByteArray( 32, 'a' ) );
    REQUIRE( w->submit( epoch, a, WriteProducer::Automation, 10 ).value );
    auto retry = w->submit( epoch, a, WriteProducer::Automation, 20 );
    REQUIRE( retry.value );
    CHECK( retry.value->receipt.stage() == WriteStage::Queued );
    CHECK( w->state().pendingRequests == 1 );
    CHECK( w->state().nextWakeNs == 110 );
    CHECK( w->submit( epoch, request( "a", "different" ), WriteProducer::Automation, 20 ).error.code
           == "request_conflict" );
    CHECK( w->submit( epoch, request( "a", a.bytes(), true ), WriteProducer::Automation, 20 )
               .error.code
           == "request_conflict" );
    CHECK(
        w->submit( epoch, request( "a", a.bytes(), false, 7, 110 ), WriteProducer::Automation, 20 )
            .error.code
        == "request_conflict" );
    CHECK( w->submit( epoch, a, WriteProducer::Manual, 20 ).error.code == "request_conflict" );
    REQUIRE(
        w->submit( epoch, request( "b", QByteArray( 32, 'b' ) ), WriteProducer::Automation, 20 )
            .value );
    CHECK( w->state().residentBytes == 64 );
    CHECK( w->submit( epoch, request( "c" ), WriteProducer::Automation, 20 ).error.code
           == "queue_full" );
    CHECK( w->state().pendingRequests == 2 );
    auto countBound = writer();
    for ( const auto* id : { "a", "b", "c" } )
        REQUIRE( countBound->submit( epoch, request( id ), WriteProducer::Automation, 0 ).value );
    CHECK( countBound->submit( epoch, request( "d" ), WriteProducer::Automation, 0 ).error.code
           == "queue_full" );
    CHECK( countBound->state().residentBytes == 9 );
}

TEST_CASE( "Writer rejects stale contexts and unsupported byte-stream submissions",
           "[communication][D01b-admission]" )
{
    auto w = writer();
    CHECK( w->submit( { "other", 7 }, request( "a" ), WriteProducer::Automation, 90 ).error.code
           == "stale_generation" );
    CHECK( w->submit( { "ep", 6 }, request( "a" ), WriteProducer::Automation, 90 ).error.code
           == "stale_generation" );
    CHECK( w->submit( epoch, request( "a", "abc", false, 6 ), WriteProducer::Automation, 90 )
               .error.code
           == "stale_generation" );
    CHECK( w->state().residentBytes == 0 );
    REQUIRE( w->submit( epoch, request( "a" ), WriteProducer::Automation, 1 ).value );
    CHECK( w->submit( epoch, request( "b" ), WriteProducer::Manual, 1 ).error.code
           == "endpoint_busy" );
    CHECK( w->submit( epoch, request( "b" ), WriteProducer::Automation, -1 ).error.code
           == "invalid_clock" );
    CHECK( w->submit( epoch, request( "b" ), WriteProducer::Automation, 0 ).error.code
           == "invalid_clock" );
    CHECK( w->submit( epoch, request( "b", "abc", false, 7, 1 ), WriteProducer::Automation, 1 )
               .error.code
           == "deadline_exceeded" );
    for ( bool datagram : { false, true } ) {
        EndpointCapabilitiesDefinition definition;
        definition.canWrite = datagram;
        definition.messageBoundaries = datagram;
        auto caps = EndpointCapabilities::fromDefinition( definition );
        REQUIRE( caps.value );
        auto unsupported = SharedWriter::create( epoch, writerLimits(), *caps.value, 100 );
        REQUIRE( unsupported.value );
        CHECK( ( *unsupported.value )
                   ->submit( epoch, request( "x" ), WriteProducer::Automation, 0 )
                   .error.code
               == "unsupported_capability" );
    }
    auto broader = StreamLimits::fromDefinition( StreamLimitsDefinition{} );
    REQUIRE( broader.value );
    auto oversized = WriteRequest::fromDefinition( { "large", QByteArray( 33, 'x' ), false, 7, {} },
                                                   *broader.value );
    REQUIRE( oversized.value );
    CHECK( w->submit( epoch, *oversized.value, WriteProducer::Automation, 1 ).error.code
           == "resource_limit" );
}

TEST_CASE( "Shared writer rejects invalid identity and timeout policy",
           "[communication][D01b-factory]" )
{
    const auto limits = writerLimits();
    const auto capabilities = writerCapabilities();
    const auto badSurrogate = QString( 1, QChar( 0xd800 ) );
    for ( const auto& endpoint : { QString{}, QString( 129, 'x' ), QString( "a\nb" ),
                                   QString( "a\rb" ), QString( 1, QChar::Null ), badSurrogate } ) {
        auto result = SharedWriter::create( { endpoint, 1 }, limits, capabilities, 100 );
        CHECK_FALSE( result.value );
        CHECK( result.error.code == "invalid_definition" );
        CHECK( result.error.fieldId == "endpointId" );
    }
    auto zeroGeneration = SharedWriter::create( { "ep", 0 }, limits, capabilities, 100 );
    CHECK_FALSE( zeroGeneration.value );
    CHECK( zeroGeneration.error.fieldId == "generation" );
    for ( const auto budget : { qint64( 0 ), qint64( -1 ) } ) {
        auto result = SharedWriter::create( { "ep", 1 }, limits, capabilities, budget );
        CHECK_FALSE( result.value );
        CHECK( result.error.code == "invalid_definition" );
        CHECK( result.error.fieldId == "defaultTimeoutNs" );
    }
}
