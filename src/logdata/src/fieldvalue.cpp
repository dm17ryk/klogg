// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fieldvalue.h"
#include "log.h"
#include <QMetaType>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>

namespace {
using namespace FieldValueLimits;

DataResult<FieldValue> failure( const char* reason, const QString& fieldId = {} )
{
    const auto code = QString::fromLatin1( reason );
    LOG_DEBUG << "F01 value rejected reason=" << code;
    return { {}, { code, code, fieldId, {} } };
}

bool validText( const QString& text )
{
    for ( int i = 0; i < text.size(); ++i ) {
        const auto ch = text.at( i );
        if ( ch.isHighSurrogate() ) {
            if ( i + 1 >= text.size() || !text.at( ++i ).isLowSurrogate() ) {
                return false;
            }
        }
        else if ( ch.isLowSurrogate() ) {
            return false;
        }
    }
    return true;
}

bool digits( QStringView text )
{
    if ( text.isEmpty() ) {
        return false;
    }
    for ( const auto ch : text ) {
        if ( ch < QLatin1Char( '0' ) || ch > QLatin1Char( '9' ) ) {
            return false;
        }
    }
    return true;
}

bool integerToken( const QString& text, bool allowNegative )
{
    int first = 0;
    if ( text.startsWith( QLatin1Char( '+' ) )
         || ( allowNegative && text.startsWith( QLatin1Char( '-' ) ) ) ) {
        first = 1;
    }
    return digits( QStringView( text ).mid( first ) );
}

bool canonicalCoefficient( const QString& coefficient )
{
    const int first = coefficient.startsWith( QLatin1Char( '-' ) ) ? 1 : 0;
    const auto value = QStringView( coefficient ).mid( first );
    return value.size() <= MaxCoefficientDigits && digits( value )
           && ( value.front() != QLatin1Char( '0' ) || value.size() == 1 )
           && coefficient != QStringLiteral( "-0" );
}

bool smallInteger( const QVariant& variant, int minimum, int maximum, int& result )
{
    const auto type = variant.userType();
    if ( type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::LongLong
         && type != QMetaType::ULongLong && type != QMetaType::Double ) {
        return false;
    }
    bool ok = false;
    const double number = variant.toDouble( &ok );
    if ( !ok || !std::isfinite( number ) || number < minimum || number > maximum
         || std::floor( number ) != number ) {
        return false;
    }
    result = static_cast<int>( number );
    return true;
}

DataResult<FieldValue> parsedDecimal( const QString& text )
{
    int exponentAt = -1;
    const int count = static_cast<int>( text.size() ); // admission already bounded to 1 Mi units
    for ( int i = 0; i < count; ++i ) {
        if ( text.at( i ) == QLatin1Char( 'e' ) || text.at( i ) == QLatin1Char( 'E' ) ) {
            if ( exponentAt >= 0 ) {
                return failure( "invalid_decimal" );
            }
            exponentAt = i;
        }
    }
    const QString mantissa = exponentAt < 0 ? text : text.left( exponentAt );
    qint64 exponent = 0;
    if ( exponentAt >= 0 ) {
        const auto token = text.mid( exponentAt + 1 );
        bool ok = false;
        exponent = token.toLongLong( &ok, 10 );
        if ( !integerToken( token, true ) || !ok ) {
            return failure( "invalid_decimal" );
        }
        // Bound before subtraction, including LLONG_MIN; no double conversion.
        if ( exponent < -MaxDecimalScale || exponent > MaxTextUnits + MaxDecimalScale ) {
            return failure( "decimal_limit" );
        }
    }
    int first = 0;
    const bool negative = mantissa.startsWith( QLatin1Char( '-' ) );
    if ( negative || mantissa.startsWith( QLatin1Char( '+' ) ) ) {
        first = 1;
    }
    QString coefficient;
    int fraction = 0;
    bool dot = false;
    bool seenDigit = false;
    for ( int i = first; i < mantissa.size(); ++i ) {
        const auto ch = mantissa.at( i );
        if ( ch == QLatin1Char( '.' ) && !dot ) {
            dot = true;
            continue;
        }
        if ( ch < QLatin1Char( '0' ) || ch > QLatin1Char( '9' ) ) {
            return failure( "invalid_decimal" );
        }
        seenDigit = true;
        if ( dot ) {
            ++fraction;
        }
        if ( !coefficient.isEmpty() || ch != QLatin1Char( '0' ) ) {
            coefficient.append( ch );
            if ( coefficient.size() > MaxCoefficientDigits ) {
                return failure( "decimal_limit" );
            }
        }
    }
    if ( !seenDigit ) {
        return failure( "invalid_decimal" );
    }
    if ( coefficient.isEmpty() ) {
        coefficient = QStringLiteral( "0" );
    }
    else if ( negative ) {
        coefficient.prepend( QLatin1Char( '-' ) );
    }
    const qint64 scale = static_cast<qint64>( fraction ) - exponent;
    if ( scale < -MaxDecimalScale || scale > MaxDecimalScale ) {
        return failure( "decimal_limit" );
    }
    return makeFieldValue( DecimalValue{ coefficient, static_cast<qint32>( scale ) }, text );
}
} // namespace

FieldType FieldValue::tag() const
{
    static constexpr FieldType types[]
        = { FieldType::Missing, FieldType::Null,    FieldType::Bool,    FieldType::Int64,
            FieldType::UInt64,  FieldType::Decimal, FieldType::Float64, FieldType::String,
            FieldType::Bytes,   FieldType::Invalid };
    static_assert( std::size( types ) == std::variant_size_v<ValuePayload> );
    return types[ payload_.index() ];
}

QString fieldTypeName( FieldType type )
{
    switch ( type ) {
    case FieldType::Missing:
        return QStringLiteral( "missing" );
    case FieldType::Null:
        return QStringLiteral( "null" );
    case FieldType::Bool:
        return QStringLiteral( "bool" );
    case FieldType::Int64:
        return QStringLiteral( "int64" );
    case FieldType::UInt64:
        return QStringLiteral( "uint64" );
    case FieldType::Decimal:
        return QStringLiteral( "decimal" );
    case FieldType::Float64:
        return QStringLiteral( "float64" );
    case FieldType::String:
        return QStringLiteral( "string" );
    case FieldType::Bytes:
        return QStringLiteral( "bytes" );
    case FieldType::Timestamp:
        return QStringLiteral( "timestamp" );
    case FieldType::Duration:
        return QStringLiteral( "duration" );
    case FieldType::Array:
        return QStringLiteral( "array" );
    case FieldType::Object:
        return QStringLiteral( "object" );
    case FieldType::Invalid:
        return QStringLiteral( "invalid" );
    }
    return QStringLiteral( "unsupported" );
}

DataResult<FieldValue> makeFieldValue( ValuePayload payload, std::optional<QString> raw )
{
    if ( raw ) {
        if ( raw->size() > FieldValueLimits::MaxTextUnits ) {
            return failure( "value_limit" );
        }
        if ( !validText( *raw ) ) {
            return failure( "invalid_utf16" );
        }
        if ( raw->isNull() ) {
            *raw = QStringLiteral( "" );
        }
    }
    if ( const auto* decimal = std::get_if<DecimalValue>( &payload ) ) {
        if ( !canonicalCoefficient( decimal->coefficient )
             || decimal->scale < -FieldValueLimits::MaxDecimalScale
             || decimal->scale > FieldValueLimits::MaxDecimalScale ) {
            return failure( "invalid_decimal" );
        }
    }
    else if ( const auto* number = std::get_if<double>( &payload ) ) {
        if ( !std::isfinite( *number ) ) {
            return failure( "invalid_float64" );
        }
    }
    else if ( auto* text = std::get_if<QString>( &payload ) ) {
        if ( text->size() > FieldValueLimits::MaxTextUnits ) {
            return failure( "value_limit" );
        }
        if ( !validText( *text ) ) {
            return failure( "invalid_utf16" );
        }
        if ( text->isNull() ) {
            *text = QStringLiteral( "" );
        }
    }
    else if ( const auto* bytes = std::get_if<QByteArray>( &payload ) ) {
        if ( bytes->size() > FieldValueLimits::MaxBytes ) {
            return failure( "value_limit" );
        }
    }
    else if ( const auto* invalid = std::get_if<InvalidValue>( &payload ) ) {
        if ( !raw || invalid->reason.isEmpty()
             || invalid->reason.size() > FieldValueLimits::MaxReasonUnits
             || !validText( invalid->reason ) ) {
            return failure( "invalid_value_envelope" );
        }
    }
    FieldValue value( std::move( payload ), std::move( raw ) );
    LOG_DEBUG << "F01 value created tag=" << fieldTypeName( value.tag() )
              << " rawPresent=" << value.rawLexeme().has_value();
    return { std::move( value ), {} };
}

DataResult<FieldValue> parseTypedValue( QStringView raw, const FieldSchema& schema )
{
    if ( raw.size() > FieldValueLimits::MaxTextUnits ) {
        return failure( "value_limit", schema.fieldId );
    }
    const QString text = raw.toString();
    if ( !validText( text ) ) {
        return failure( "invalid_utf16", schema.fieldId );
    }
    LOG_DEBUG << "F01 parsing tag=" << fieldTypeName( schema.type ) << " units=" << raw.size();
    DataResult<FieldValue> result;
    bool ok = false;
    switch ( schema.type ) {
    case FieldType::String:
        return makeFieldValue( text, text );
    case FieldType::Bool:
        if ( text == QStringLiteral( "true" ) || text == QStringLiteral( "false" ) ) {
            return makeFieldValue( text == QStringLiteral( "true" ), text );
        }
        return makeFieldValue( InvalidValue{ QStringLiteral( "invalid_bool" ) }, text );
    case FieldType::Int64: {
        const qint64 value = text.toLongLong( &ok, 10 );
        if ( ok && integerToken( text, true ) ) {
            return makeFieldValue( value, text );
        }
        return makeFieldValue( InvalidValue{ QStringLiteral( "invalid_int64" ) }, text );
    }
    case FieldType::UInt64: {
        const quint64 value = text.toULongLong( &ok, 10 );
        if ( ok && integerToken( text, false ) ) {
            return makeFieldValue( value, text );
        }
        return makeFieldValue( InvalidValue{ QStringLiteral( "invalid_uint64" ) }, text );
    }
    case FieldType::Decimal:
        result = parsedDecimal( text );
        if ( result.value ) {
            return result;
        }
        return makeFieldValue( InvalidValue{ result.error.code }, text );
    case FieldType::Float64: {
        const double value = text.toDouble( &ok );
        if ( ok && std::isfinite( value ) && text == text.trimmed() && !text.isEmpty() ) {
            return makeFieldValue( value, text );
        }
        return makeFieldValue( InvalidValue{ QStringLiteral( "invalid_float64" ) }, text );
    }
    case FieldType::Null:
        if ( text == QStringLiteral( "null" ) ) {
            return makeFieldValue( NullValue{}, text );
        }
        return makeFieldValue( InvalidValue{ QStringLiteral( "invalid_null" ) }, text );
    default:
        return failure( "unsupported_value_type", schema.fieldId );
    }
}

QVariantMap encodeFieldValue( const FieldValue& value )
{
    QVariantMap wire{ { QStringLiteral( "schemaVersion" ), 1 },
                      { QStringLiteral( "type" ), fieldTypeName( value.tag() ) } };
    if ( value.rawLexeme() ) {
        wire.insert( QStringLiteral( "rawLexeme" ), *value.rawLexeme() );
    }
    const auto& payload = value.payload();
    QVariant encoded;
    switch ( value.tag() ) {
    case FieldType::Missing:
    case FieldType::Null:
        break;
    case FieldType::Bool:
        encoded = std::get<bool>( payload );
        break;
    case FieldType::Int64:
        encoded = QString::number( std::get<qint64>( payload ) );
        break;
    case FieldType::UInt64:
        encoded = QString::number( std::get<quint64>( payload ) );
        break;
    case FieldType::Decimal: {
        const auto& decimal = std::get<DecimalValue>( payload );
        encoded = QVariantMap{ { QStringLiteral( "coefficient" ), decimal.coefficient },
                               { QStringLiteral( "scale" ), decimal.scale } };
        break;
    }
    case FieldType::Float64: {
        static_assert( sizeof( double ) == sizeof( quint64 )
                       && std::numeric_limits<double>::is_iec559 );
        quint64 bits = 0;
        const double number = std::get<double>( payload );
        std::memcpy( &bits, &number, sizeof( bits ) );
        wire.insert( QStringLiteral( "bits" ),
                     QString::number( bits, 16 ).rightJustified( 16, QLatin1Char( '0' ) ) );
        break;
    }
    case FieldType::String:
        encoded = std::get<QString>( payload );
        break;
    case FieldType::Bytes:
        encoded = QString::fromLatin1( std::get<QByteArray>( payload ).toBase64() );
        break;
    case FieldType::Invalid:
        encoded = QVariantMap{ { QStringLiteral( "reason" ),
                                 std::get<InvalidValue>( payload ).reason } };
        break;
    default:
        Q_UNREACHABLE();
    }
    if ( encoded.isValid() ) {
        wire.insert( QStringLiteral( "value" ), encoded );
    }
    LOG_DEBUG << "F01 encoded tag=" << fieldTypeName( value.tag() );
    return wire;
}

DataResult<FieldValue> decodeFieldValue( const QVariantMap& wire )
{
    int version = 0;
    if ( !smallInteger( wire.value( QStringLiteral( "schemaVersion" ) ), 1, 1, version ) ) {
        return failure( "unsupported_value_version" );
    }
    const auto typeValue = wire.value( QStringLiteral( "type" ) );
    if ( typeValue.userType() != QMetaType::QString ) {
        return failure( "invalid_value_envelope" );
    }
    const auto type = typeValue.toString();
    const bool empty = type == QStringLiteral( "missing" ) || type == QStringLiteral( "null" );
    const bool floating = type == QStringLiteral( "float64" );
    const bool supported
        = empty || floating || type == QStringLiteral( "bool" ) || type == QStringLiteral( "int64" )
          || type == QStringLiteral( "uint64" ) || type == QStringLiteral( "decimal" )
          || type == QStringLiteral( "string" ) || type == QStringLiteral( "bytes" )
          || type == QStringLiteral( "invalid" );
    if ( !supported ) {
        return failure( "unsupported_value_type" );
    }
    for ( auto it = wire.cbegin(); it != wire.cend(); ++it ) {
        if ( it.key() != QStringLiteral( "schemaVersion" ) && it.key() != QStringLiteral( "type" )
             && it.key() != QStringLiteral( "rawLexeme" )
             && ( empty
                  || it.key()
                         != ( floating ? QStringLiteral( "bits" )
                                       : QStringLiteral( "value" ) ) ) ) {
            return failure( "invalid_value_envelope" );
        }
    }
    std::optional<QString> raw;
    if ( wire.contains( QStringLiteral( "rawLexeme" ) ) ) {
        if ( wire.value( QStringLiteral( "rawLexeme" ) ).userType() != QMetaType::QString ) {
            return failure( "invalid_value_envelope" );
        }
        raw = wire.value( QStringLiteral( "rawLexeme" ) ).toString();
    }
    if ( empty ) {
        return type == QStringLiteral( "missing" ) ? makeFieldValue( MissingValue{}, raw )
                                                   : makeFieldValue( NullValue{}, raw );
    }
    const auto value
        = wire.value( floating ? QStringLiteral( "bits" ) : QStringLiteral( "value" ) );
    LOG_DEBUG << "F01 decoding tag=" << type << " payloadMetaType=" << value.userType();
    if ( type == QStringLiteral( "bool" ) ) {
        return value.userType() == QMetaType::Bool ? makeFieldValue( value.toBool(), raw )
                                                   : failure( "invalid_value_envelope" );
    }
    if ( type == QStringLiteral( "decimal" ) || type == QStringLiteral( "invalid" ) ) {
        if ( value.userType() != QMetaType::QVariantMap ) {
            return failure( "invalid_value_envelope" );
        }
        const auto object = value.toMap();
        if ( type == QStringLiteral( "invalid" ) ) {
            if ( object.size() != 1
                 || object.value( QStringLiteral( "reason" ) ).userType() != QMetaType::QString ) {
                return failure( "invalid_value_envelope" );
            }
            return makeFieldValue(
                InvalidValue{ object.value( QStringLiteral( "reason" ) ).toString() }, raw );
        }
        int scale = 0;
        if ( object.size() != 2
             || object.value( QStringLiteral( "coefficient" ) ).userType() != QMetaType::QString
             || !smallInteger( object.value( QStringLiteral( "scale" ) ),
                               -FieldValueLimits::MaxDecimalScale,
                               FieldValueLimits::MaxDecimalScale, scale ) ) {
            return failure( "invalid_value_envelope" );
        }
        return makeFieldValue(
            DecimalValue{ object.value( QStringLiteral( "coefficient" ) ).toString(), scale },
            raw );
    }
    if ( value.userType() != QMetaType::QString ) {
        return failure( "invalid_value_envelope" );
    }
    const auto text = value.toString();
    bool ok = false;
    if ( type == QStringLiteral( "int64" ) ) {
        const qint64 number = text.toLongLong( &ok, 10 );
        return ok && QString::number( number ) == text ? makeFieldValue( number, raw )
                                                       : failure( "invalid_int64" );
    }
    if ( type == QStringLiteral( "uint64" ) ) {
        const quint64 number = text.toULongLong( &ok, 10 );
        return ok && QString::number( number ) == text ? makeFieldValue( number, raw )
                                                       : failure( "invalid_uint64" );
    }
    if ( floating ) {
        if ( text.size() != 16 ) {
            return failure( "invalid_float64" );
        }
        const quint64 bits = text.toULongLong( &ok, 16 );
        if ( !ok || QString::number( bits, 16 ).rightJustified( 16, QLatin1Char( '0' ) ) != text ) {
            return failure( "invalid_float64" );
        }
        double number = 0;
        std::memcpy( &number, &bits, sizeof( number ) );
        return makeFieldValue( number, raw );
    }
    if ( type == QStringLiteral( "bytes" ) ) {
        if ( text.size() > ( ( FieldValueLimits::MaxBytes + 2 ) / 3 ) * 4 ) {
            return failure( "value_limit" );
        }
        const auto bytes = QByteArray::fromBase64( text.toLatin1() );
        if ( QString::fromLatin1( bytes.toBase64() ) != text ) {
            return failure( "invalid_bytes" );
        }
        return makeFieldValue( bytes, raw );
    }
    return makeFieldValue( text, raw );
}
