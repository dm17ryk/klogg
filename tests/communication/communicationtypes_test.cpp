// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch.hpp>

#include "communicationtypes.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <limits>

namespace {
StreamLimits limits()
{
    auto result = StreamLimits::fromDefinition( {} );
    REQUIRE( result.value );
    return *result.value;
}

WriteRequestDefinition request()
{
    return { QStringLiteral( "request-a" ), QByteArrayLiteral( "hello" ), false, 7, {} };
}
} // namespace

TEST_CASE( "Limit ceilings and cross-field bounds are checked",
           "[communication][D01a][communication_limits]" )
{
    REQUIRE( StreamLimits::fromDefinition( {} ).value );
    StreamLimitsDefinition definition;
    SECTION( "endpoint count" )
    {
        definition.maximumEndpoints = 17;
    }
    SECTION( "receipt count" )
    {
        definition.maximumReceipts = 257;
    }
    SECTION( "RX bytes" )
    {
        definition.maximumRxBytes = 8 * 1024 * 1024 + 1;
    }
    SECTION( "TX bytes" )
    {
        definition.maximumPendingTxBytes = 8 * 1024 * 1024 + 1;
    }
    SECTION( "chunk bytes" )
    {
        definition.maximumChunkBytes = 64 * 1024 + 1;
    }
    SECTION( "write bytes" )
    {
        definition.maximumWriteBytes = 1024 * 1024 + 1;
    }
    SECTION( "retention" )
    {
        definition.receiptRetentionMs = 600001;
    }
    auto result = StreamLimits::fromDefinition( definition );
    REQUIRE_FALSE( result.value );
    CHECK( result.error.code == QStringLiteral( "resource_limit" ) );
    CHECK_FALSE( result.error.fieldId.isEmpty() );
    CHECK_FALSE( result.error.location );
}

TEST_CASE( "Zero or incompatible stream policy cannot be admitted",
           "[communication][D01a][communication_limits]" )
{
    StreamLimitsDefinition definition;
    SECTION( "zero endpoints" )
    {
        definition.maximumEndpoints = 0;
    }
    SECTION( "zero receipts" )
    {
        definition.maximumReceipts = 0;
    }
    SECTION( "zero RX bytes" )
    {
        definition.maximumRxBytes = 0;
    }
    SECTION( "zero TX bytes" )
    {
        definition.maximumPendingTxBytes = 0;
    }
    SECTION( "zero chunk" )
    {
        definition.maximumChunkBytes = 0;
    }
    SECTION( "zero write" )
    {
        definition.maximumWriteBytes = 0;
    }
    SECTION( "zero retention" )
    {
        definition.receiptRetentionMs = 0;
    }
    SECTION( "negative retention" )
    {
        definition.receiptRetentionMs = -1;
    }
    SECTION( "chunk exceeds write" )
    {
        definition.maximumWriteBytes = 10;
    }
    SECTION( "write exceeds pending TX" )
    {
        definition.maximumPendingTxBytes = 10;
    }
    auto result = StreamLimits::fromDefinition( definition );
    REQUIRE_FALSE( result.value );
    CHECK( result.error.code == QStringLiteral( "invalid_definition" ) );
}

TEST_CASE( "Lower positive policy bounds constrain real requests",
           "[communication][D01a][communication_request]" )
{
    StreamLimitsDefinition definition;
    definition.maximumChunkBytes = 3;
    definition.maximumWriteBytes = 5;
    definition.maximumPendingTxBytes = 7;
    auto policy = StreamLimits::fromDefinition( definition );
    REQUIRE( policy.value );
    REQUIRE( WriteRequest::fromDefinition( request(), *policy.value ).value );
    auto input = request();
    input.bytes = QByteArrayLiteral( "123456" );
    auto result = WriteRequest::fromDefinition( input, *policy.value );
    REQUIRE_FALSE( result.value );
    CHECK( result.error.code == QStringLiteral( "resource_limit" ) );
}

TEST_CASE( "All 256 byte values survive request construction",
           "[communication][D01a][communication_request]" )
{
    auto input = request();
    input.bytes.clear();
    for ( int byte = 0; byte != 256; ++byte ) {
        input.bytes.append( static_cast<char>( byte ) );
    }
    input.connectionGeneration = std::numeric_limits<quint64>::max();
    auto result = WriteRequest::fromDefinition( input, limits() );
    REQUIRE( result.value );
    CHECK( result.value->bytes() == input.bytes );
    CHECK( result.value->bytes().size() == 256 );
    CHECK( result.value->connectionGeneration() == std::numeric_limits<quint64>::max() );
    CHECK_FALSE( result.value->deadlineNs() );
}

TEST_CASE( "Borrowed byte and QString storage is owned",
           "[communication][D01a][communication_request]" )
{
    char bytes[] = { '\0', 'A', '\xff', '\0' };
    QChar id[] = { QLatin1Char( 'r' ), QLatin1Char( 'a' ), QLatin1Char( 'w' ) };
    auto input = request();
    input.bytes = QByteArray::fromRawData( bytes, 4 );
    input.requestId = QString::fromRawData( id, 3 );
    auto result = WriteRequest::fromDefinition( input, limits() );
    REQUIRE( result.value );
    bytes[ 1 ] = 'B';
    id[ 0 ] = QLatin1Char( 'x' );
    CHECK( result.value->bytes() == QByteArray::fromHex( "0041ff00" ) );
    CHECK( result.value->requestId() == QStringLiteral( "raw" ) );
}

TEST_CASE( "Mutating a definition cannot alter a checked request",
           "[communication][D01a][communication_request]" )
{
    auto input = request();
    input.sensitive = true;
    input.deadlineNs = 123;
    auto result = WriteRequest::fromDefinition( input, limits() );
    REQUIRE( result.value );
    input.bytes.fill( 'x' );
    input.requestId = QStringLiteral( "other" );
    input.sensitive = false;
    input.connectionGeneration = 8;
    input.deadlineNs.reset();
    auto copy = *result.value;
    CHECK( copy.bytes() == QByteArrayLiteral( "hello" ) );
    CHECK( copy.requestId() == QStringLiteral( "request-a" ) );
    CHECK( copy.sensitive() );
    CHECK( copy.connectionGeneration() == 7 );
    REQUIRE( copy.deadlineNs() );
    CHECK( *copy.deadlineNs() == 123 );
}

TEST_CASE( "Request bounds reject before payload copy",
           "[communication][D01a][communication_request]" )
{
    auto input = request();
    input.bytes = QByteArray( 1024 * 1024, 'a' );
    REQUIRE( WriteRequest::fromDefinition( input, limits() ).value );
    input.bytes.append( 'b' );
    auto oversized = WriteRequest::fromDefinition( input, limits() );
    REQUIRE_FALSE( oversized.value );
    CHECK( oversized.error.code == QStringLiteral( "resource_limit" ) );
    CHECK( oversized.error.fieldId == QStringLiteral( "bytes" ) );
}

TEST_CASE( "Invalid request identity and deadlines fail without a location",
           "[communication][D01a][communication_request]" )
{
    auto input = request();
    SECTION( "empty bytes" )
    {
        input.bytes.clear();
    }
    SECTION( "empty ID" )
    {
        input.requestId.clear();
    }
    SECTION( "NUL ID" )
    {
        input.requestId.append( QChar( 0 ) );
    }
    SECTION( "CR ID" )
    {
        input.requestId.append( QLatin1Char( '\r' ) );
    }
    SECTION( "LF ID" )
    {
        input.requestId.append( QLatin1Char( '\n' ) );
    }
    SECTION( "high surrogate" )
    {
        input.requestId.append( QChar( 0xd800 ) );
    }
    SECTION( "low surrogate" )
    {
        input.requestId.append( QChar( 0xdc00 ) );
    }
    SECTION( "zero generation" )
    {
        input.connectionGeneration = 0;
    }
    SECTION( "negative deadline" )
    {
        input.deadlineNs = -1;
    }
    auto result = WriteRequest::fromDefinition( input, limits() );
    REQUIRE_FALSE( result.value );
    CHECK( result.error.code == QStringLiteral( "invalid_definition" ) );
    CHECK_FALSE( result.error.location );
}

TEST_CASE( "Request IDs count UTF16 units and preserve valid surrogate pairs",
           "[communication][D01a][communication_request]" )
{
    auto input = request();
    input.requestId = QString( 126, QLatin1Char( 'a' ) );
    input.requestId.append( QChar( 0xd83d ) ).append( QChar( 0xde00 ) );
    input.deadlineNs = 0;
    REQUIRE( WriteRequest::fromDefinition( input, limits() ).value );
    input.requestId.prepend( QLatin1Char( 'a' ) );
    auto result = WriteRequest::fromDefinition( input, limits() );
    REQUIRE_FALSE( result.value );
    CHECK( result.error.code == QStringLiteral( "resource_limit" ) );
}

TEST_CASE( "Disconnected capabilities do not grant authority",
           "[communication][D01a][communication_capabilities]" )
{
    auto result = EndpointCapabilities::fromDefinition( {} );
    REQUIRE( result.value );
    CHECK_FALSE( result.value->canRead() );
    CHECK_FALSE( result.value->canWrite() );
    CHECK_FALSE( result.value->messageBoundaries() );
    CHECK_FALSE( result.value->canControlSignals() );
    CHECK_FALSE( result.value->canSendBreak() );
    CHECK_FALSE( result.value->canObserveBreak() );
    CHECK_FALSE( result.value->lineErrors() );
    CHECK_FALSE( result.value->hardwareTimestamps() );
}

TEST_CASE( "Capability limitations are bounded and owned",
           "[communication][D01a][communication_capabilities]" )
{
    QChar raw[] = { QLatin1Char( 'o' ), QLatin1Char( 'l' ), QLatin1Char( 'd' ) };
    EndpointCapabilitiesDefinition input;
    input.canRead = true;
    input.limitations.append( QString::fromRawData( raw, 3 ) );
    auto result = EndpointCapabilities::fromDefinition( input );
    REQUIRE( result.value );
    raw[ 0 ] = QLatin1Char( 'x' );
    CHECK( result.value->limitations().first() == QStringLiteral( "old" ) );
    CHECK( result.value->canRead() );
    input.limitations.clear();
    for ( int index = 0; index != 16; ++index ) {
        input.limitations.append( QString( 256, QLatin1Char( 'a' ) ) );
    }
    REQUIRE( EndpointCapabilities::fromDefinition( input ).value );
    SECTION( "too many" )
    {
        input.limitations.append( QStringLiteral( "extra" ) );
    }
    SECTION( "too long" )
    {
        input.limitations[ 0 ].append( QLatin1Char( 'a' ) );
    }
    auto oversized = EndpointCapabilities::fromDefinition( input );
    REQUIRE_FALSE( oversized.value );
    CHECK( oversized.error.code == QStringLiteral( "resource_limit" ) );
}

TEST_CASE( "Malformed capability text is rejected",
           "[communication][D01a][communication_capabilities]" )
{
    EndpointCapabilitiesDefinition input;
    input.limitations.append( QString( QChar( 0xd800 ) ) );
    auto result = EndpointCapabilities::fromDefinition( input );
    REQUIRE_FALSE( result.value );
    CHECK( result.error.code == QStringLiteral( "invalid_definition" ) );
}

namespace {
WriteReceiptDefinition receipt()
{
    return { QStringLiteral( "request-a" ),
             QStringLiteral( "endpoint-a" ),
             7,
             WriteStage::Queued,
             5,
             0,
             0,
             PayloadDisposition::Omitted,
             {},
             {} };
}

QJsonObject encodedReceipt()
{
    auto checked = WriteReceipt::fromDefinition( receipt(), limits() );
    REQUIRE( checked.value );
    auto encoded = encodeWriteReceipt( *checked.value, limits() );
    REQUIRE( encoded.value );
    return *encoded.value;
}

QByteArray secret()
{
    auto bytes = QByteArrayLiteral( "D01a-private-71a7-not-metadata" );
    bytes.append( '\0' ).append( "\r\n" ).append( '\xff' );
    return bytes;
}

QStringList* capturedMessages = nullptr;
void captureDiagnostic( QtMsgType, const QMessageLogContext&, const QString& message )
{
    if ( capturedMessages ) {
        capturedMessages->append( message );
    }
}
void enableContractDiagnostics( QLoggingCategory* category )
{
    if ( QByteArray( category->categoryName() )
         == QByteArrayLiteral( "cilogg.communication.contracts" ) ) {
        category->setEnabled( QtDebugMsg, true );
        category->setEnabled( QtWarningMsg, true );
    }
}
class DiagnosticCapture {
public:
    DiagnosticCapture()
    {
        capturedMessages = &messages;
        previousHandler_ = qInstallMessageHandler( captureDiagnostic );
        previousFilter_ = QLoggingCategory::installFilter( enableContractDiagnostics );
    }
    ~DiagnosticCapture()
    {
        QLoggingCategory::installFilter( previousFilter_ );
        qInstallMessageHandler( previousHandler_ );
        capturedMessages = nullptr;
    }
    DiagnosticCapture( const DiagnosticCapture& ) = delete;
    DiagnosticCapture& operator=( const DiagnosticCapture& ) = delete;
    QStringList messages;

private:
    QtMessageHandler previousHandler_ = nullptr;
    QLoggingCategory::CategoryFilter previousFilter_ = nullptr;
};
} // namespace

TEST_CASE( "Receipt prefix counts and stages are checked",
           "[communication][D01a][communication_receipt][communication_receipt_core]" )
{
    auto input = receipt();
    bool valid = true;
    SECTION( "queued" ) {}
    SECTION( "accepted prefix" )
    {
        input.stage = WriteStage::Accepted;
        input.acceptedBytes = 3;
    }
    SECTION( "backend written" )
    {
        input.stage = WriteStage::WrittenToBackend;
        input.acceptedBytes = input.writtenBytes = 5;
    }
    SECTION( "failed prefix" )
    {
        input.stage = WriteStage::Failed;
        input.acceptedBytes = 3;
        input.writtenBytes = 2;
        input.errorCode = QStringLiteral( "io_error" );
    }
    SECTION( "cancelled prefix" )
    {
        input.stage = WriteStage::Cancelled;
        input.acceptedBytes = 3;
        input.writtenBytes = 2;
        input.errorCode = QStringLiteral( "cancelled" );
    }
    SECTION( "written exceeds accepted" )
    {
        input.stage = WriteStage::Failed;
        input.acceptedBytes = 3;
        input.writtenBytes = 4;
        input.errorCode = QStringLiteral( "io_error" );
        valid = false;
    }
    SECTION( "accepted exceeds requested" )
    {
        input.acceptedBytes = 6;
        valid = false;
    }
    SECTION( "queued with bytes" )
    {
        input.acceptedBytes = 1;
        valid = false;
    }
    SECTION( "empty accepted prefix" )
    {
        input.stage = WriteStage::Accepted;
        valid = false;
    }
    SECTION( "incomplete backend written" )
    {
        input.stage = WriteStage::WrittenToBackend;
        input.acceptedBytes = 5;
        input.writtenBytes = 4;
        valid = false;
    }
    SECTION( "failed without error" )
    {
        input.stage = WriteStage::Failed;
        valid = false;
    }
    SECTION( "cancelled with wrong error" )
    {
        input.stage = WriteStage::Cancelled;
        input.errorCode = QStringLiteral( "io_error" );
        valid = false;
    }
    SECTION( "success with error" )
    {
        input.errorMessage = QStringLiteral( "unexpected" );
        valid = false;
    }
    SECTION( "invalid stage" )
    {
        input.stage = static_cast<WriteStage>( 99 );
        valid = false;
    }
    SECTION( "invalid disposition" )
    {
        input.payloadDisposition = static_cast<PayloadDisposition>( 99 );
        valid = false;
    }
    SECTION( "zero requested" )
    {
        input.requestedBytes = 0;
        valid = false;
    }
    SECTION( "zero generation" )
    {
        input.connectionGeneration = 0;
        valid = false;
    }
    auto checked = WriteReceipt::fromDefinition( input, limits() );
    CHECK( checked.value.has_value() == valid );
    if ( !valid ) {
        CHECK( checked.error.code == QStringLiteral( "invalid_definition" ) );
        CHECK_FALSE( checked.error.location );
    }
}

TEST_CASE( "Receipt codec preserves exact generation",
           "[communication][D01a][communication_codec]" )
{
    for ( const auto generation :
          { quint64( 9007199254740993ULL ), std::numeric_limits<quint64>::max() } ) {
        auto input = receipt();
        input.connectionGeneration = generation;
        input.stage = WriteStage::Failed;
        input.acceptedBytes = 3;
        input.writtenBytes = 2;
        input.errorCode = QStringLiteral( "io_error" );
        input.errorMessage = QStringLiteral( "Write failed" );
        auto checked = WriteReceipt::fromDefinition( input, limits() );
        REQUIRE( checked.value );
        auto encoded = encodeWriteReceipt( *checked.value, limits() );
        REQUIRE( encoded.value );
        CHECK( encoded.value->size() == 11 );
        CHECK( encoded.value->value( QStringLiteral( "schemaVersion" ) ).toDouble() == 1 );
        CHECK( encoded.value->value( QStringLiteral( "connectionGeneration" ) ).toString()
               == QString::number( generation ) );
        CHECK( encoded.value->value( QStringLiteral( "requestedBytes" ) ).toString()
               == QStringLiteral( "5" ) );
        auto decoded = decodeWriteReceipt( *encoded.value, limits() );
        REQUIRE( decoded.value );
        CHECK( decoded.value->connectionGeneration() == generation );
        CHECK( decoded.value->requestId() == input.requestId );
        CHECK( decoded.value->endpointId() == input.endpointId );
        CHECK( decoded.value->stage() == WriteStage::Failed );
        CHECK( decoded.value->requestedBytes() == 5 );
        CHECK( decoded.value->acceptedBytes() == 3 );
        CHECK( decoded.value->writtenBytes() == 2 );
        CHECK( decoded.value->payloadDisposition() == PayloadDisposition::Omitted );
        CHECK( decoded.value->errorCode() == input.errorCode );
        CHECK( decoded.value->errorMessage() == input.errorMessage );
    }
}

TEST_CASE( "Receipt codec rejects coercions and extra fields",
           "[communication][D01a][communication_codec]" )
{
    const auto original = encodedReceipt();
    for ( const auto& field :
          { "connectionGeneration", "requestedBytes", "acceptedBytes", "writtenBytes" } ) {
        for ( const auto& text :
              { "", "01", "+1", "-1", " 1", "1 ", "1e3", "1.0", "18446744073709551616" } ) {
            auto object = original;
            object[ QLatin1String( field ) ] = QLatin1String( text );
            CHECK_FALSE( decodeWriteReceipt( object, limits() ).value );
        }
        for ( const auto& value :
              { QJsonValue( 1 ), QJsonValue( true ), QJsonValue( QJsonObject{} ) } ) {
            auto object = original;
            object[ QLatin1String( field ) ] = value;
            CHECK_FALSE( decodeWriteReceipt( object, limits() ).value );
        }
    }
    for ( const auto& version : { QJsonValue( QStringLiteral( "1" ) ), QJsonValue( true ),
                                  QJsonValue( 0 ), QJsonValue( 2 ), QJsonValue( 1.5 ) } ) {
        auto object = original;
        object[ QStringLiteral( "schemaVersion" ) ] = version;
        CHECK_FALSE( decodeWriteReceipt( object, limits() ).value );
    }
    for ( auto iterator = original.begin(); iterator != original.end(); ++iterator ) {
        auto object = original;
        object.remove( iterator.key() );
        CHECK_FALSE( decodeWriteReceipt( object, limits() ).value );
        object = original;
        object[ iterator.key() ] = QJsonObject{};
        CHECK_FALSE( decodeWriteReceipt( object, limits() ).value );
    }
    for ( const auto& key : { "bytes", "base64", "unknown" } ) {
        auto object = original;
        object[ QLatin1String( key ) ] = QStringLiteral( "data" );
        CHECK_FALSE( decodeWriteReceipt( object, limits() ).value );
        object.remove( QStringLiteral( "errorMessage" ) );
        CHECK_FALSE( decodeWriteReceipt( object, limits() ).value );
    }
    for ( const auto& field : { "connectionGeneration", "requestedBytes" } ) {
        auto object = original;
        object[ QLatin1String( field ) ] = QStringLiteral( "0" );
        CHECK_FALSE( decodeWriteReceipt( object, limits() ).value );
    }
    auto object = original;
    object[ QStringLiteral( "stage" ) ] = QStringLiteral( "complete" );
    CHECK_FALSE( decodeWriteReceipt( object, limits() ).value );
    object = original;
    object[ QStringLiteral( "payloadDisposition" ) ] = QStringLiteral( "included" );
    CHECK_FALSE( decodeWriteReceipt( object, limits() ).value );
}

TEST_CASE( "Sensitive receipt contains metadata only",
           "[communication][D01a][communication_receipt][communication_codec]" )
{
    auto definition = request();
    definition.bytes = secret();
    definition.sensitive = true;
    auto checked = WriteRequest::fromDefinition( definition, limits() );
    REQUIRE( checked.value );
    auto result = WriteReceipt::forRequest( *checked.value, QStringLiteral( "endpoint-a" ),
                                            WriteStage::Failed, 3, 2, WriteFailure::PartialWrite,
                                            limits() );
    REQUIRE( result.value );
    CHECK( result.value->requestId() == definition.requestId );
    CHECK( result.value->connectionGeneration() == definition.connectionGeneration );
    CHECK( result.value->requestedBytes() == static_cast<quint64>( secret().size() ) );
    CHECK( result.value->payloadDisposition() == PayloadDisposition::Redacted );
    CHECK( result.value->errorCode() == QStringLiteral( "partial_write" ) );
    auto encoded = encodeWriteReceipt( *result.value, limits() );
    REQUIRE( encoded.value );
    const auto bytes = QJsonDocument( *encoded.value ).toJson( QJsonDocument::Compact );
    CHECK_FALSE( bytes.contains( secret() ) );
    CHECK_FALSE( bytes.contains( secret().toHex() ) );
    CHECK_FALSE( bytes.contains( secret().toBase64() ) );
    CHECK_FALSE( encoded.value->contains( QStringLiteral( "bytes" ) ) );
    CHECK_FALSE( encoded.value->contains( QStringLiteral( "base64" ) ) );
    CHECK_FALSE( encoded.value->contains( QStringLiteral( "hex" ) ) );
    CHECK( encoded.value->value( QStringLiteral( "payloadDisposition" ) ).toString()
           == QStringLiteral( "redacted" ) );
}

TEST_CASE( "Factory diagnostics exclude bodies", "[communication][D01a][communication_receipt]" )
{
    DiagnosticCapture capture;
    auto input = request();
    input.bytes = secret();
    input.sensitive = true;
    auto checked = WriteRequest::fromDefinition( input, limits() );
    REQUIRE( checked.value );
    auto result
        = WriteReceipt::forRequest( *checked.value, QStringLiteral( "endpoint-a" ),
                                    WriteStage::Failed, 3, 2, WriteFailure::IoError, limits() );
    REQUIRE( result.value );
    REQUIRE( encodeWriteReceipt( *result.value, limits() ).value );
    input.requestId.append( QLatin1Char( '\n' ) );
    CHECK_FALSE( WriteRequest::fromDefinition( input, limits() ).value );
    const auto log = capture.messages.join( QLatin1Char( '\n' ) ).toUtf8();
    REQUIRE_FALSE( log.isEmpty() );
    CHECK( log.contains( "decision=request_owned" ) );
    CHECK( log.contains( "decision=receipt_owned" ) );
    CHECK( log.contains( "decision=receipt_encoded" ) );
    CHECK( log.contains( "decision=rejected" ) );
    CHECK_FALSE( log.contains( secret() ) );
    CHECK_FALSE( log.contains( "D01a-private-71a7-not-metadata" ) );
    CHECK_FALSE( log.contains( secret().toHex() ) );
    CHECK_FALSE( log.contains( secret().toHex( ' ' ) ) );
    CHECK_FALSE( log.contains( secret().toBase64() ) );
    CHECK_FALSE(
        log.contains( QCryptographicHash::hash( secret(), QCryptographicHash::Sha256 ).toHex() ) );
}

TEST_CASE( "Metadata bounds reject without truncation",
           "[communication][D01a][communication_receipt][communication_receipt_core]" )
{
    auto input = receipt();
    input.stage = WriteStage::Failed;
    input.errorCode = QString( 64, QLatin1Char( 'a' ) );
    input.errorMessage = QString( 512, QLatin1Char( 'b' ) );
    REQUIRE( WriteReceipt::fromDefinition( input, limits() ).value );
    SECTION( "oversized request ID" )
    {
        input.requestId = QString( 129, QLatin1Char( 'r' ) );
    }
    SECTION( "oversized endpoint ID" )
    {
        input.endpointId = QString( 129, QLatin1Char( 'e' ) );
    }
    SECTION( "oversized error code" )
    {
        input.errorCode.append( QLatin1Char( 'a' ) );
    }
    SECTION( "oversized error message" )
    {
        input.errorMessage.append( QLatin1Char( 'b' ) );
    }
    SECTION( "oversized requested bytes" )
    {
        input.requestedBytes = 1024 * 1024 + 1;
    }
    auto result = WriteReceipt::fromDefinition( input, limits() );
    REQUIRE_FALSE( result.value );
    CHECK( result.error.code == QStringLiteral( "resource_limit" ) );
    CHECK_FALSE( result.error.location );
}

TEST_CASE( "Receipt text owns borrowed metadata",
           "[communication][D01a][communication_receipt][communication_receipt_core]" )
{
    QChar raw[] = { QLatin1Char( 'o' ), QLatin1Char( 'l' ), QLatin1Char( 'd' ) };
    auto input = receipt();
    input.stage = WriteStage::Failed;
    input.requestId = input.endpointId = input.errorCode = input.errorMessage
        = QString::fromRawData( raw, 3 );
    auto result = WriteReceipt::fromDefinition( input, limits() );
    REQUIRE( result.value );
    raw[ 0 ] = QLatin1Char( 'x' );
    CHECK( result.value->requestId() == QStringLiteral( "old" ) );
    CHECK( result.value->endpointId() == QStringLiteral( "old" ) );
    CHECK( result.value->errorCode() == QStringLiteral( "old" ) );
    CHECK( result.value->errorMessage() == QStringLiteral( "old" ) );
}

TEST_CASE( "Compact receipt encoding has an explicit byte bound",
           "[communication][D01a][communication_codec]" )
{
    auto input = receipt();
    input.stage = WriteStage::Failed;
    input.requestId = input.endpointId = QString( 128, QChar( 1 ) );
    input.errorCode = QString( 64, QChar( 1 ) );
    input.errorMessage = QString( 512, QChar( 1 ) );
    auto checked = WriteReceipt::fromDefinition( input, limits() );
    REQUIRE( checked.value );
    auto encoded = encodeWriteReceipt( *checked.value, limits() );
    REQUIRE_FALSE( encoded.value );
    CHECK( encoded.error.code == QStringLiteral( "resource_limit" ) );
    auto object = encodedReceipt();
    object[ QStringLiteral( "stage" ) ] = QStringLiteral( "failed" );
    object[ QStringLiteral( "requestId" ) ] = input.requestId;
    object[ QStringLiteral( "endpointId" ) ] = input.endpointId;
    object[ QStringLiteral( "errorCode" ) ] = input.errorCode;
    object[ QStringLiteral( "errorMessage" ) ] = input.errorMessage;
    auto decoded = decodeWriteReceipt( object, limits() );
    REQUIRE_FALSE( decoded.value );
    CHECK( decoded.error.code == QStringLiteral( "resource_limit" ) );
}
