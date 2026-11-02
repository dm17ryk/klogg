// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fieldvalue.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <catch2/catch.hpp>
#include <cmath>
#include <limits>

namespace {
FieldValue make( ValuePayload payload, std::optional<QString> raw = {} )
{
    auto result = makeFieldValue( std::move( payload ), std::move( raw ) );
    REQUIRE( result.value );
    REQUIRE( result.error.code.isEmpty() );
    return *result.value;
}

FieldValue parse( const QString& raw, FieldType type )
{
    FieldSchema schema;
    schema.fieldId = QStringLiteral( "test" );
    schema.type = type;
    auto result = parseTypedValue( QStringView( raw ), schema );
    REQUIRE( result.value );
    REQUIRE( result.error.code.isEmpty() );
    return *result.value;
}

QVariantMap roundTrip( const FieldValue& value )
{
    const auto encoded = encodeFieldValue( value );
    const auto json = QJsonDocument::fromVariant( encoded ).toJson( QJsonDocument::Compact );
    const auto decoded = decodeFieldValue( QJsonDocument::fromJson( json ).toVariant().toMap() );
    REQUIRE( decoded.value );
    REQUIRE( decoded.error.code.isEmpty() );
    CHECK( decoded.value->tag() == value.tag() );
    CHECK( encodeFieldValue( *decoded.value ) == encoded );
    return encoded;
}
} // namespace

TEST_CASE( "F01_values_roundtrip_without_loss", "[data][F01]" )
{
    const auto unsignedWire
        = roundTrip( parse( QStringLiteral( "18446744073709551615" ), FieldType::UInt64 ) );
    CHECK( unsignedWire.value( "value" ).toString() == QStringLiteral( "18446744073709551615" ) );
    const auto signedWire
        = roundTrip( parse( QStringLiteral( "9007199254740993" ), FieldType::Int64 ) );
    CHECK( signedWire.value( "value" ).toString() == QStringLiteral( "9007199254740993" ) );
    roundTrip( parse( QStringLiteral( "-9223372036854775808" ), FieldType::Int64 ) );
    const auto decimal
        = roundTrip( parse( QStringLiteral( "12345678901234567890.00100" ), FieldType::Decimal ) );
    CHECK( decimal.value( "value" ).toMap().value( "coefficient" ).toString()
           == QStringLiteral( "1234567890123456789000100" ) );
    CHECK( decimal.value( "value" ).toMap().value( "scale" ).toInt() == 5 );
    CHECK( decimal.value( "rawLexeme" ).toString()
           == QStringLiteral( "12345678901234567890.00100" ) );

    const auto missing = roundTrip( make( MissingValue{} ) );
    const auto null = roundTrip( make( NullValue{} ) );
    const auto empty = roundTrip( make( QString{} ) );
    const auto boolean = roundTrip( make( false ) );
    const auto zero = roundTrip( make( qint64{ 0 } ) );
    const auto invalid = roundTrip(
        make( InvalidValue{ QStringLiteral( "invalid_int64" ) }, QStringLiteral( "bad" ) ) );
    CHECK( missing.value( "type" ).toString() == QStringLiteral( "missing" ) );
    CHECK( null.value( "type" ).toString() == QStringLiteral( "null" ) );
    CHECK_FALSE( missing.contains( "value" ) );
    CHECK_FALSE( null.contains( "value" ) );
    CHECK( empty.value( "type" ).toString() == QStringLiteral( "string" ) );
    CHECK( QJsonDocument::fromVariant( empty ).object().value( "value" ).isString() );
    CHECK( boolean.value( "value" ).userType() == QMetaType::Bool );
    CHECK_FALSE( boolean.value( "value" ).toBool() );
    CHECK( zero.value( "value" ).toString() == QStringLiteral( "0" ) );
    CHECK( invalid.value( "type" ).toString() == QStringLiteral( "invalid" ) );
}

TEST_CASE( "F01_float_bits_and_binary_text_survive_real_JSON", "[data][F01]" )
{
    const auto negativeZero = roundTrip( make( -0.0 ) );
    CHECK( negativeZero.value( "bits" ).toString() == QStringLiteral( "8000000000000000" ) );
    const auto decoded = decodeFieldValue( negativeZero );
    REQUIRE( decoded.value );
    CHECK( std::signbit( std::get<double>( decoded.value->payload() ) ) );
    CHECK( roundTrip( make( 1.5 ) ).value( "bits" ).toString()
           == QStringLiteral( "3ff8000000000000" ) );
    CHECK( roundTrip( make( std::numeric_limits<double>::denorm_min() ) ).value( "bits" ).toString()
           == QStringLiteral( "0000000000000001" ) );
    roundTrip( make( std::numeric_limits<double>::max() ) );
    QString text = QString::fromUtf8( u8"שלום 😀" );
    text.append( QChar( 0 ) );
    roundTrip( make( text, text ) );
    CHECK( roundTrip( make( QByteArray::fromHex( "00ff1020" ) ) ).value( "value" ).toString()
           == QStringLiteral( "AP8QIA==" ) );
    const auto emptyBytes = roundTrip( make( QByteArray{} ) );
    CHECK( QJsonDocument::fromVariant( emptyBytes ).object().value( "value" ).isString() );
    CHECK( emptyBytes.value( "value" ).toString().isEmpty() );
}

TEST_CASE( "F01_decimal_parsing_preserves_exact_scale_and_lexeme", "[data][F01]" )
{
    struct Example {
        const char* raw;
        const char* coefficient;
        int scale;
    };
    for ( const auto& example : { Example{ "1.2300e2", "12300", 2 }, Example{ ".5", "5", 1 },
                                  Example{ "-000.00", "0", 2 }, Example{ "1e-6144", "1", 6144 },
                                  Example{ "1e6144", "1", -6144 } } ) {
        const auto raw = QString::fromLatin1( example.raw );
        const auto value = parse( raw, FieldType::Decimal );
        REQUIRE( value.tag() == FieldType::Decimal );
        CHECK( std::get<DecimalValue>( value.payload() ).coefficient
               == QString::fromLatin1( example.coefficient ) );
        CHECK( std::get<DecimalValue>( value.payload() ).scale == example.scale );
        CHECK( value.rawLexeme() == std::optional<QString>( raw ) );
        roundTrip( value );
    }
    for ( const auto& raw :
          { QStringLiteral( "1e6145" ), QStringLiteral( "1e-6145" ), QStringLiteral( "1,2" ),
            QStringLiteral( "." ), QStringLiteral( "1e" ), QString( 257, QLatin1Char( '1' ) ) } ) {
        const auto value = parse( raw, FieldType::Decimal );
        CHECK( value.tag() == FieldType::Invalid );
        CHECK( value.rawLexeme() == std::optional<QString>( raw ) );
        roundTrip( value );
    }
    CHECK( parse( QStringLiteral( "false" ), FieldType::Bool ).tag() == FieldType::Bool );
    CHECK( parse( QStringLiteral( "FALSE" ), FieldType::Bool ).tag() == FieldType::Invalid );
    CHECK( parse( QStringLiteral( "" ), FieldType::Int64 ).tag() == FieldType::Invalid );
    CHECK( parse( QStringLiteral( "" ), FieldType::String ).tag() == FieldType::String );
    CHECK( parse( QStringLiteral( "18446744073709551616" ), FieldType::UInt64 ).tag()
           == FieldType::Invalid );
    CHECK( parse( QStringLiteral( "9223372036854775808" ), FieldType::Int64 ).tag()
           == FieldType::Invalid );
}

TEST_CASE( "F01_float_lexemes_are_consumed_completely", "[data][F01]" )
{
    CHECK(
        roundTrip( parse( QStringLiteral( "-0" ), FieldType::Float64 ) ).value( "bits" ).toString()
        == QStringLiteral( "8000000000000000" ) );
    CHECK( parse( QStringLiteral( ".5" ), FieldType::Float64 ).tag() == FieldType::Float64 );
    for ( const auto& raw :
          { QStringLiteral( "nan" ), QStringLiteral( "inf" ), QStringLiteral( "1e999" ),
            QStringLiteral( "0x1p0" ), QStringLiteral( "1,5" ), QStringLiteral( "1e" ),
            QStringLiteral( " 1" ), QString::fromLatin1( "1.5\0ignored", 11 ) } ) {
        const auto value = parse( raw, FieldType::Float64 );
        CHECK( value.tag() == FieldType::Invalid );
        CHECK( value.rawLexeme() == std::optional<QString>( raw ) );
    }
}

TEST_CASE( "F01_wire_decode_rejects_wrong_shapes_types_and_versions", "[data][F01]" )
{
    auto integer = encodeFieldValue( make( qint64{ 9 } ) );
    for ( const auto& payload :
          { QVariant( 9007199254740992.0 ), QVariant( true ), QVariant( "09" ), QVariant( "+9" ),
            QVariant( "9223372036854775808" ) } ) {
        auto wire = integer;
        wire[ "value" ] = payload;
        const auto decoded = decodeFieldValue( wire );
        CHECK_FALSE( decoded.value );
        CHECK_FALSE( decoded.error.code.isEmpty() );
    }
    integer[ "type" ] = QStringLiteral( "future_type" );
    CHECK( decodeFieldValue( integer ).error.code == QStringLiteral( "unsupported_value_type" ) );
    integer[ "type" ] = QStringLiteral( "timestamp" );
    CHECK( decodeFieldValue( integer ).error.code == QStringLiteral( "unsupported_value_type" ) );
    integer[ "schemaVersion" ] = 2;
    CHECK( decodeFieldValue( integer ).error.code
           == QStringLiteral( "unsupported_value_version" ) );
    for ( const auto& version : { QVariant( true ), QVariant( "1" ), QVariant( 1.5 ) } ) {
        auto wire = encodeFieldValue( make( qint64{ 1 } ) );
        wire[ "schemaVersion" ] = version;
        CHECK_FALSE( decodeFieldValue( wire ).value );
    }
    auto wire = encodeFieldValue( make( qint64{ 1 } ) );
    wire[ "unexpected" ] = 1;
    CHECK_FALSE( decodeFieldValue( wire ).value );
    wire = encodeFieldValue( make( qint64{ 1 } ) );
    wire[ "rawLexeme" ] = 1;
    CHECK_FALSE( decodeFieldValue( wire ).value );
    wire = encodeFieldValue( make( 1.0 ) );
    wire[ "bits" ] = QStringLiteral( "7ff0000000000000" );
    CHECK_FALSE( decodeFieldValue( wire ).value );
    wire = encodeFieldValue( make( QByteArray( "x" ) ) );
    wire[ "value" ] = QStringLiteral( "eA== junk" );
    CHECK_FALSE( decodeFieldValue( wire ).value );
}

TEST_CASE( "F01_native_values_and_resource_limits_are_enforced", "[data][F01]" )
{
    CHECK_FALSE( makeFieldValue( std::numeric_limits<double>::infinity() ).value );
    CHECK_FALSE( makeFieldValue( std::numeric_limits<double>::quiet_NaN() ).value );
    CHECK_FALSE( makeFieldValue( DecimalValue{ QStringLiteral( "01" ), 0 } ).value );
    CHECK_FALSE( makeFieldValue( DecimalValue{ QStringLiteral( "-0" ), 0 } ).value );
    CHECK_FALSE( makeFieldValue( DecimalValue{ QStringLiteral( "1" ), 6145 } ).value );
    CHECK_FALSE( makeFieldValue( InvalidValue{ QStringLiteral( "bad" ) } ).value );
    QString broken;
    broken.append( QChar( 0xd800 ) );
    CHECK_FALSE( makeFieldValue( broken ).value );
    const QString boundary( FieldValueLimits::MaxTextUnits, QLatin1Char( 'a' ) );
    REQUIRE( makeFieldValue( boundary ).value );
    CHECK_FALSE( makeFieldValue( boundary + QLatin1Char( 'a' ) ).value );
    FieldSchema schema;
    schema.fieldId = QStringLiteral( "field" );
    const auto limit = parseTypedValue( QStringView( boundary + QLatin1Char( 'a' ) ), schema );
    CHECK_FALSE( limit.value );
    CHECK( limit.error.code == QStringLiteral( "value_limit" ) );
    CHECK( limit.error.fieldId == schema.fieldId );
    CHECK_FALSE( limit.error.location );
}
