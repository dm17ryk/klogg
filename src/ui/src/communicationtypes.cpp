// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "communicationtypes.h"

#include <QJsonDocument>
#include <QLoggingCategory>
#include <array>
#include <limits>

namespace {
Q_LOGGING_CATEGORY( communicationContracts, "cilogg.communication.contracts" )
DataError invalid( const QString& field, const QString& reason, bool limited = false )
{
    const auto code
        = limited ? QStringLiteral( "resource_limit" ) : QStringLiteral( "invalid_definition" );
    qCWarning( communicationContracts )
        << "decision=rejected" << "field=" << field << "code=" << code << "reason=" << reason;
    return { code, reason, field, {} };
}

template <typename T>
DataError boundedPositive( T value, T ceiling, const QString& field )
{
    if ( value <= 0 ) {
        return invalid( field, QStringLiteral( "A positive limit is required" ) );
    }
    if ( value > ceiling ) {
        return invalid( field, QStringLiteral( "The limit exceeds the supported ceiling" ), true );
    }
    qCDebug( communicationContracts ) << "decision=limit_checked" << "field=" << field
                                      << "value=" << value << "ceiling=" << ceiling;
    return {};
}

DataError checkedText( const QString& text, int maximumUnits, const QString& field,
                       bool id = false )
{
    if ( text.size() > maximumUnits ) {
        return invalid( field, QStringLiteral( "Text exceeds the supported UTF-16 length" ), true );
    }
    if ( id && text.isEmpty() ) {
        return invalid( field, QStringLiteral( "An identifier is required" ) );
    }
    for ( int index = 0; index < text.size(); ++index ) {
        const auto character = text.at( index );
        if ( id
             && ( character.isNull() || character == QLatin1Char( '\r' )
                  || character == QLatin1Char( '\n' ) ) ) {
            return invalid( field, QStringLiteral( "Identifier contains a forbidden character" ) );
        }
        if ( character.isHighSurrogate() ) {
            if ( index + 1 == text.size() || !text.at( index + 1 ).isLowSurrogate() ) {
                return invalid( field, QStringLiteral( "Text contains invalid Unicode" ) );
            }
            ++index;
        }
        else if ( character.isLowSurrogate() ) {
            return invalid( field, QStringLiteral( "Text contains invalid Unicode" ) );
        }
    }
    qCDebug( communicationContracts )
        << "decision=text_checked" << "field=" << field << "units=" << text.size();
    return {};
}

QString ownedText( const QString& text )
{
    // An implicit QString copy can retain fromRawData()'s borrowed storage.
    return text.isEmpty() ? QStringLiteral( "" ) : QString( text.constData(), text.size() );
}

DataError checkedReceipt( const WriteReceiptDefinition& definition, const StreamLimits& limits )
{
    const DataError textChecks[] = {
        checkedText( definition.requestId, 128, QStringLiteral( "requestId" ), true ),
        checkedText( definition.endpointId, 128, QStringLiteral( "endpointId" ), true ),
        checkedText( definition.errorCode, 64, QStringLiteral( "errorCode" ) ),
        checkedText( definition.errorMessage, 512, QStringLiteral( "errorMessage" ) ),
    };
    for ( const auto& error : textChecks ) {
        if ( !error.code.isEmpty() ) {
            return error;
        }
    }
    if ( definition.connectionGeneration == 0 ) {
        return invalid( QStringLiteral( "connectionGeneration" ),
                        QStringLiteral( "A nonzero connection generation is required" ) );
    }
    if ( definition.requestedBytes > limits.maximumWriteBytes() ) {
        return invalid( QStringLiteral( "requestedBytes" ),
                        QStringLiteral( "Requested bytes exceed the write limit" ), true );
    }
    if ( definition.requestedBytes == 0 || definition.acceptedBytes > definition.requestedBytes
         || definition.writtenBytes > definition.acceptedBytes ) {
        return invalid( QStringLiteral( "byteCounts" ),
                        QStringLiteral( "Receipt prefix counts are inconsistent" ) );
    }
    if ( definition.payloadDisposition != PayloadDisposition::Omitted
         && definition.payloadDisposition != PayloadDisposition::Redacted ) {
        return invalid( QStringLiteral( "payloadDisposition" ),
                        QStringLiteral( "Unsupported payload disposition" ) );
    }
    bool successful = false;
    switch ( definition.stage ) {
    case WriteStage::Queued:
        if ( definition.acceptedBytes != 0 || definition.writtenBytes != 0 ) {
            return invalid( QStringLiteral( "byteCounts" ),
                            QStringLiteral( "Queued receipts cannot contain accepted bytes" ) );
        }
        successful = true;
        break;
    case WriteStage::Accepted:
        if ( definition.acceptedBytes == 0 ) {
            return invalid( QStringLiteral( "acceptedBytes" ),
                            QStringLiteral( "Accepted receipts require a positive prefix" ) );
        }
        successful = true;
        break;
    case WriteStage::WrittenToBackend:
        if ( definition.acceptedBytes != definition.requestedBytes
             || definition.writtenBytes != definition.requestedBytes ) {
            return invalid(
                QStringLiteral( "byteCounts" ),
                QStringLiteral( "Backend-written receipts require the entire prefix" ) );
        }
        successful = true;
        break;
    case WriteStage::Failed:
        if ( definition.errorCode.isEmpty() ) {
            return invalid( QStringLiteral( "errorCode" ),
                            QStringLiteral( "Failed receipts require an error code" ) );
        }
        if ( definition.errorCode == QStringLiteral( "cancelled" ) ) {
            return invalid( QStringLiteral( "stage" ),
                            QStringLiteral( "Cancellation requires the cancelled receipt stage" ) );
        }
        break;
    case WriteStage::Cancelled:
        if ( definition.errorCode != QStringLiteral( "cancelled" ) ) {
            return invalid(
                QStringLiteral( "errorCode" ),
                QStringLiteral( "Cancelled receipts require the cancelled error code" ) );
        }
        break;
    default:
        return invalid( QStringLiteral( "stage" ), QStringLiteral( "Unsupported receipt stage" ) );
    }
    if ( successful && ( !definition.errorCode.isEmpty() || !definition.errorMessage.isEmpty() ) ) {
        return invalid( QStringLiteral( "errorCode" ),
                        QStringLiteral( "Successful receipt stages cannot contain errors" ) );
    }
    qCDebug( communicationContracts )
        << "decision=receipt_checked"
        << "requested=" << definition.requestedBytes << "accepted=" << definition.acceptedBytes
        << "written=" << definition.writtenBytes;
    return {};
}

WriteReceiptDefinition receiptDefinition( const WriteReceipt& receipt )
{
    return { receipt.requestId(),    receipt.endpointId(),         receipt.connectionGeneration(),
             receipt.stage(),        receipt.requestedBytes(),     receipt.acceptedBytes(),
             receipt.writtenBytes(), receipt.payloadDisposition(), receipt.errorCode(),
             receipt.errorMessage() };
}

QString stageName( WriteStage stage )
{
    switch ( stage ) {
    case WriteStage::Queued:
        return QStringLiteral( "queued" );
    case WriteStage::Accepted:
        return QStringLiteral( "accepted" );
    case WriteStage::WrittenToBackend:
        return QStringLiteral( "written_to_backend" );
    case WriteStage::Failed:
        return QStringLiteral( "failed" );
    case WriteStage::Cancelled:
        return QStringLiteral( "cancelled" );
    }
    return {};
}

QJsonObject receiptObject( const WriteReceiptDefinition& definition )
{
    return {
        { QStringLiteral( "schemaVersion" ), 1 },
        { QStringLiteral( "requestId" ), definition.requestId },
        { QStringLiteral( "endpointId" ), definition.endpointId },
        { QStringLiteral( "connectionGeneration" ),
          QString::number( definition.connectionGeneration ) },
        { QStringLiteral( "stage" ), stageName( definition.stage ) },
        { QStringLiteral( "requestedBytes" ), QString::number( definition.requestedBytes ) },
        { QStringLiteral( "acceptedBytes" ), QString::number( definition.acceptedBytes ) },
        { QStringLiteral( "writtenBytes" ), QString::number( definition.writtenBytes ) },
        { QStringLiteral( "payloadDisposition" ),
          definition.payloadDisposition == PayloadDisposition::Redacted
              ? QStringLiteral( "redacted" )
              : QStringLiteral( "omitted" ) },
        { QStringLiteral( "errorCode" ), definition.errorCode },
        { QStringLiteral( "errorMessage" ), definition.errorMessage },
    };
}

DataError checkedEncodingSize( const QJsonObject& boundedObject )
{
    // Call only after field and domain validation, bounding this serialization too.
    const auto bytes = QJsonDocument( boundedObject ).toJson( QJsonDocument::Compact ).size();
    if ( bytes > 4096 ) {
        return invalid( QStringLiteral( "receipt" ),
                        QStringLiteral( "Compact receipt encoding exceeds 4096 bytes" ), true );
    }
    qCDebug( communicationContracts ) << "decision=receipt_size_checked" << "bytes=" << bytes;
    return {};
}

DataResult<quint64> unsignedDecimal( const QString& text, const QString& field )
{
    if ( text.isEmpty() || ( text.size() > 1 && text.front() == QLatin1Char( '0' ) ) ) {
        return { {},
                 invalid( field, QStringLiteral( "Expected canonical unsigned decimal text" ) ) };
    }
    quint64 value = 0;
    for ( const auto character : text ) {
        const auto unit = character.unicode();
        if ( unit < '0' || unit > '9' ) {
            return {
                {}, invalid( field, QStringLiteral( "Expected canonical unsigned decimal text" ) )
            };
        }
        const auto digit = static_cast<quint64>( unit - '0' );
        if ( value > ( std::numeric_limits<quint64>::max() - digit ) / 10 ) {
            return { {}, invalid( field, QStringLiteral( "Unsigned decimal value overflows" ) ) };
        }
        value = value * 10 + digit;
    }
    qCDebug( communicationContracts ) << "decision=decimal_checked" << "field=" << field;
    return { value, {} };
}
} // namespace

DataResult<StreamLimits> StreamLimits::fromDefinition( const StreamLimitsDefinition& definition )
{
    const StreamLimitsDefinition ceilings;
    const DataError checks[] = {
        boundedPositive( definition.maximumEndpoints, ceilings.maximumEndpoints,
                         QStringLiteral( "maximumEndpoints" ) ),
        boundedPositive( definition.maximumReceipts, ceilings.maximumReceipts,
                         QStringLiteral( "maximumReceipts" ) ),
        boundedPositive( definition.maximumRxBytes, ceilings.maximumRxBytes,
                         QStringLiteral( "maximumRxBytes" ) ),
        boundedPositive( definition.maximumPendingTxBytes, ceilings.maximumPendingTxBytes,
                         QStringLiteral( "maximumPendingTxBytes" ) ),
        boundedPositive( definition.maximumChunkBytes, ceilings.maximumChunkBytes,
                         QStringLiteral( "maximumChunkBytes" ) ),
        boundedPositive( definition.maximumWriteBytes, ceilings.maximumWriteBytes,
                         QStringLiteral( "maximumWriteBytes" ) ),
        boundedPositive( definition.receiptRetentionMs, ceilings.receiptRetentionMs,
                         QStringLiteral( "receiptRetentionMs" ) ),
    };
    for ( const auto& error : checks ) {
        if ( !error.code.isEmpty() ) {
            return { {}, error };
        }
    }
    if ( definition.maximumChunkBytes > definition.maximumRxBytes ) {
        return { {},
                 invalid( QStringLiteral( "maximumChunkBytes" ),
                          QStringLiteral( "Chunk limit exceeds RX staging limit" ) ) };
    }
    if ( definition.maximumChunkBytes > definition.maximumWriteBytes ) {
        return { {},
                 invalid( QStringLiteral( "maximumChunkBytes" ),
                          QStringLiteral( "Chunk limit exceeds write limit" ) ) };
    }
    if ( definition.maximumWriteBytes > definition.maximumPendingTxBytes ) {
        return { {},
                 invalid( QStringLiteral( "maximumWriteBytes" ),
                          QStringLiteral( "Write limit exceeds pending TX limit" ) ) };
    }
    qCDebug( communicationContracts ) << "decision=limits_created";
    return { StreamLimits( definition ), {} };
}

DataResult<EndpointCapabilities>
EndpointCapabilities::fromDefinition( const EndpointCapabilitiesDefinition& definition )
{
    if ( definition.limitations.size() > 16 ) {
        return { {},
                 invalid( QStringLiteral( "limitations" ),
                          QStringLiteral( "Too many capability limitations" ), true ) };
    }
    for ( const auto& text : definition.limitations ) {
        const auto error = checkedText( text, 256, QStringLiteral( "limitations" ) );
        if ( !error.code.isEmpty() ) {
            return { {}, error };
        }
    }
    auto owned = definition;
    owned.limitations.clear();
    owned.limitations.reserve( definition.limitations.size() );
    for ( const auto& text : definition.limitations ) {
        owned.limitations.append( ownedText( text ) );
    }
    qCDebug( communicationContracts )
        << "decision=capabilities_owned"
        << "can_read=" << owned.canRead << "can_write=" << owned.canWrite
        << "limitation_count=" << owned.limitations.size();
    return { EndpointCapabilities( std::move( owned ) ), {} };
}

DataResult<WriteRequest> WriteRequest::fromDefinition( const WriteRequestDefinition& definition,
                                                       const StreamLimits& limits )
{
    if ( definition.bytes.isEmpty() ) {
        return { {},
                 invalid( QStringLiteral( "bytes" ),
                          QStringLiteral( "A nonempty payload is required" ) ) };
    }
    if ( static_cast<quint64>( definition.bytes.size() ) > limits.maximumWriteBytes() ) {
        return { {},
                 invalid( QStringLiteral( "bytes" ),
                          QStringLiteral( "Payload exceeds the write limit" ), true ) };
    }
    const auto error
        = checkedText( definition.requestId, 128, QStringLiteral( "requestId" ), true );
    if ( !error.code.isEmpty() ) {
        return { {}, error };
    }
    if ( definition.connectionGeneration == 0 ) {
        return { {},
                 invalid( QStringLiteral( "connectionGeneration" ),
                          QStringLiteral( "A nonzero connection generation is required" ) ) };
    }
    if ( definition.deadlineNs && *definition.deadlineNs < 0 ) {
        return { {},
                 invalid( QStringLiteral( "deadlineNs" ),
                          QStringLiteral( "A supplied deadline must be nonnegative" ) ) };
    }
    WriteRequestDefinition owned{
        ownedText( definition.requestId ),
        QByteArray( definition.bytes.constData(), definition.bytes.size() ), definition.sensitive,
        definition.connectionGeneration, definition.deadlineNs
    };
    qCDebug( communicationContracts )
        << "decision=request_owned" << "request_id=" << owned.requestId
        << "generation=" << owned.connectionGeneration << "bytes=" << owned.bytes.size()
        << "maximum_write_bytes=" << limits.maximumWriteBytes() << "sensitive=" << owned.sensitive
        << "deadline_present=" << owned.deadlineNs.has_value();
    return { WriteRequest( std::move( owned ) ), {} };
}

DataResult<WriteReceipt> WriteReceipt::fromDefinition( const WriteReceiptDefinition& definition,
                                                       const StreamLimits& limits )
{
    const auto error = checkedReceipt( definition, limits );
    if ( !error.code.isEmpty() ) {
        return { {}, error };
    }
    WriteReceiptDefinition owned{
        ownedText( definition.requestId ), ownedText( definition.endpointId ),
        definition.connectionGeneration,   definition.stage,
        definition.requestedBytes,         definition.acceptedBytes,
        definition.writtenBytes,           definition.payloadDisposition,
        ownedText( definition.errorCode ), ownedText( definition.errorMessage )
    };
    qCDebug( communicationContracts )
        << "decision=receipt_owned" << "request_id=" << owned.requestId
        << "endpoint_id=" << owned.endpointId << "generation=" << owned.connectionGeneration
        << "stage=" << static_cast<int>( owned.stage );
    return { WriteReceipt( std::move( owned ) ), {} };
}

DataResult<WriteReceipt> WriteReceipt::forRequest( const WriteRequest& request,
                                                   const QString& endpointId, WriteStage stage,
                                                   quint64 acceptedBytes, quint64 writtenBytes,
                                                   WriteFailure failure,
                                                   const StreamLimits& limits )
{
    QString code;
    QString message;
    switch ( failure ) {
    case WriteFailure::None:
        break;
    case WriteFailure::UnsupportedCapability:
        code = QStringLiteral( "unsupported_capability" );
        message = QStringLiteral( "Endpoint does not support this operation" );
        break;
    case WriteFailure::EndpointBusy:
        code = QStringLiteral( "endpoint_busy" );
        message = QStringLiteral( "Endpoint is busy" );
        break;
    case WriteFailure::StaleGeneration:
        code = QStringLiteral( "stale_generation" );
        message = QStringLiteral( "Connection generation is stale" );
        break;
    case WriteFailure::QueueFull:
        code = QStringLiteral( "queue_full" );
        message = QStringLiteral( "Write queue is full" );
        break;
    case WriteFailure::DeadlineExceeded:
        code = QStringLiteral( "deadline_exceeded" );
        message = QStringLiteral( "Write deadline was exceeded" );
        break;
    case WriteFailure::Cancelled:
        code = QStringLiteral( "cancelled" );
        message = QStringLiteral( "Write was cancelled" );
        break;
    case WriteFailure::PartialWrite:
        code = QStringLiteral( "partial_write" );
        message = QStringLiteral( "Only a prefix was written" );
        break;
    case WriteFailure::IoError:
        code = QStringLiteral( "io_error" );
        message = QStringLiteral( "Backend I/O failed" );
        break;
    default:
        return { {},
                 invalid( QStringLiteral( "failure" ),
                          QStringLiteral( "Unsupported write failure" ) ) };
    }
    qCDebug( communicationContracts )
        << "decision=receipt_derived" << "request_id=" << request.requestId()
        << "failure=" << static_cast<int>( failure );
    return fromDefinition(
        { request.requestId(), endpointId, request.connectionGeneration(), stage,
          static_cast<quint64>( request.bytes().size() ), acceptedBytes, writtenBytes,
          request.sensitive() ? PayloadDisposition::Redacted : PayloadDisposition::Omitted, code,
          message },
        limits );
}

DataResult<QJsonObject> encodeWriteReceipt( const WriteReceipt& receipt,
                                            const StreamLimits& limits )
{
    const auto definition = receiptDefinition( receipt );
    const auto error = checkedReceipt( definition, limits );
    if ( !error.code.isEmpty() ) {
        return { {}, error };
    }
    const auto object = receiptObject( definition );
    const auto sizeError = checkedEncodingSize( object );
    if ( !sizeError.code.isEmpty() ) {
        return { {}, sizeError };
    }
    qCDebug( communicationContracts )
        << "decision=receipt_encoded" << "request_id=" << receipt.requestId()
        << "generation=" << receipt.connectionGeneration();
    return { object, {} };
}

DataResult<WriteReceipt> decodeWriteReceipt( const QJsonObject& object, const StreamLimits& limits )
{
    if ( object.size() != 11 ) {
        return { {},
                 invalid( QStringLiteral( "receipt" ),
                          QStringLiteral( "Expected exactly eleven receipt fields" ) ) };
    }
    const auto version = object.value( QStringLiteral( "schemaVersion" ) );
    if ( !version.isDouble() || version.toDouble() != 1 ) {
        return { {},
                 invalid( QStringLiteral( "schemaVersion" ),
                          QStringLiteral( "Unsupported receipt schema version" ) ) };
    }
    struct TextField {
        const char* name;
        int maximumUnits;
    };
    const std::array<TextField, 10> fields = { {
        { "requestId", 128 },
        { "endpointId", 128 },
        { "connectionGeneration", 20 },
        { "stage", 18 },
        { "requestedBytes", 20 },
        { "acceptedBytes", 20 },
        { "writtenBytes", 20 },
        { "payloadDisposition", 8 },
        { "errorCode", 64 },
        { "errorMessage", 512 },
    } };
    std::array<QString, 10> text;
    for ( std::size_t index = 0; index < fields.size(); ++index ) {
        const auto key = QString::fromLatin1( fields[ index ].name );
        const auto value = object.value( key );
        // All required fields plus the fixed key count exclude unknown fields without
        // copying arbitrary incoming key names. Qt extraction itself can allocate.
        if ( !value.isString() ) {
            return { {},
                     invalid( key,
                              QStringLiteral(
                                  "A receipt string field is missing or has the wrong type" ) ) };
        }
        text[ index ] = value.toString();
        const auto error = checkedText( text[ index ], fields[ index ].maximumUnits, key );
        if ( !error.code.isEmpty() ) {
            return { {}, error };
        }
    }
    const std::array<std::size_t, 4> decimalFields = { 2, 4, 5, 6 };
    std::array<quint64, 4> numbers{};
    for ( std::size_t index = 0; index < decimalFields.size(); ++index ) {
        const auto fieldIndex = decimalFields[ index ];
        const auto parsed = unsignedDecimal( text[ fieldIndex ],
                                             QString::fromLatin1( fields[ fieldIndex ].name ) );
        if ( !parsed.value ) {
            return { {}, parsed.error };
        }
        numbers[ index ] = *parsed.value;
    }
    WriteStage stage;
    if ( text[ 3 ] == QStringLiteral( "queued" ) ) {
        stage = WriteStage::Queued;
    }
    else if ( text[ 3 ] == QStringLiteral( "accepted" ) ) {
        stage = WriteStage::Accepted;
    }
    else if ( text[ 3 ] == QStringLiteral( "written_to_backend" ) ) {
        stage = WriteStage::WrittenToBackend;
    }
    else if ( text[ 3 ] == QStringLiteral( "failed" ) ) {
        stage = WriteStage::Failed;
    }
    else if ( text[ 3 ] == QStringLiteral( "cancelled" ) ) {
        stage = WriteStage::Cancelled;
    }
    else {
        return {
            {}, invalid( QStringLiteral( "stage" ), QStringLiteral( "Unsupported receipt stage" ) )
        };
    }
    PayloadDisposition disposition;
    if ( text[ 7 ] == QStringLiteral( "omitted" ) ) {
        disposition = PayloadDisposition::Omitted;
    }
    else if ( text[ 7 ] == QStringLiteral( "redacted" ) ) {
        disposition = PayloadDisposition::Redacted;
    }
    else {
        return { {},
                 invalid( QStringLiteral( "payloadDisposition" ),
                          QStringLiteral( "Unsupported payload disposition" ) ) };
    }
    const WriteReceiptDefinition definition{ text[ 0 ],    text[ 1 ],    numbers[ 0 ], stage,
                                             numbers[ 1 ], numbers[ 2 ], numbers[ 3 ], disposition,
                                             text[ 8 ],    text[ 9 ] };
    const auto error = checkedReceipt( definition, limits );
    if ( !error.code.isEmpty() ) {
        return { {}, error };
    }
    const auto sizeError = checkedEncodingSize( object );
    if ( !sizeError.code.isEmpty() ) {
        return { {}, sizeError };
    }
    qCDebug( communicationContracts )
        << "decision=receipt_decoded" << "generation=" << definition.connectionGeneration;
    // Only a fully checked, byte-bounded object can reach owned domain copies.
    return WriteReceipt::fromDefinition( definition, limits );
}
