// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "datasetschema.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <catch2/catch.hpp>
#include <limits>

namespace {
QVector<DatasetColumn> columns()
{
    return { { QStringLiteral( "line" ),
               QStringLiteral( "Physical line" ),
               FieldType::UInt64,
               QStringLiteral( "line" ),
               DatasetRole::Dimension,
               {},
               false,
               false },
             { QStringLiteral( "latency" ),
               QStringLiteral( "Latency" ),
               FieldType::Decimal,
               QStringLiteral( "ms" ),
               DatasetRole::Measure,
               {},
               true,
               false } };
}
QVariantMap jsonRoundTrip( const QVariantMap& value )
{
    return QJsonDocument::fromJson( QJsonDocument( QJsonObject::fromVariantMap( value ) ).toJson() )
        .object()
        .toVariantMap();
}
void roundTrip( const DatasetSchema& schema )
{
    const auto encoded = encodeDatasetSchema( schema );
    const auto result = decodeDatasetSchema( jsonRoundTrip( encoded ) );
    REQUIRE( result.value );
    CHECK( encodeDatasetSchema( *result.value ) == encoded );
    CHECK( result.value->schemaId() == schema.schemaId() );
    REQUIRE( result.value->columns().size() == schema.columns().size() );
    for ( int i = 0; i < schema.columns().size(); ++i ) {
        CHECK( result.value->columns()[ i ].type == schema.columns()[ i ].type );
        CHECK( result.value->columns()[ i ].semanticRole == schema.columns()[ i ].semanticRole );
        CHECK( result.value->columns()[ i ].clockDomain == schema.columns()[ i ].clockDomain );
    }
}
} // namespace
TEST_CASE( "F01 schema preserves ordered exact column metadata", "[data][F01][datasetschema]" )
{
    auto result = makeDatasetSchema( columns() );
    REQUIRE( result.value );
    roundTrip( *result.value );
    const auto wire = encodeDatasetSchema( *result.value );
    CHECK( wire.value( "schemaVersion" ).toInt() == 1 );
    CHECK( wire.value( "shape" ).toString() == "table" );
    const auto values = wire.value( "columns" ).toList();
    REQUIRE( values.size() == 2 );
    CHECK( values[ 0 ].toMap().value( "fieldId" ).toString() == "line" );
    CHECK( values[ 0 ].toMap().value( "type" ).toString() == "uint64" );
    CHECK( values[ 1 ].toMap().value( "type" ).toString() == "decimal" );
    CHECK( values[ 1 ].toMap().value( "nullable" ).toBool() );
    CHECK( values[ 1 ].toMap().value( "unit" ).toString() == "ms" );
}
TEST_CASE( "F01 schema identity frames order and interpretation without changing field IDs",
           "[data][F01][datasetschema]" )
{
    const auto original = makeDatasetSchema( columns() );
    REQUIRE( original.value );
    for ( int change = 0; change < 7; ++change ) {
        auto items = columns();
        if ( change == 0 )
            items[ 1 ].name = QStringLiteral( "Elapsed" );
        if ( change == 1 )
            items[ 1 ].unit = QStringLiteral( "us" );
        if ( change == 2 )
            items[ 1 ].type = FieldType::Int64;
        if ( change == 3 )
            items[ 1 ].nullable = false;
        if ( change == 4 )
            items[ 1 ].repeated = true;
        if ( change == 5 )
            items[ 1 ].semanticRole = DatasetRole::UpperBound;
        if ( change == 6 )
            std::swap( items[ 0 ], items[ 1 ] );
        auto modified = makeDatasetSchema( items );
        REQUIRE( modified.value );
        CHECK( modified.value->schemaId() != original.value->schemaId() );
        CHECK( original.value->columns()[ 1 ].fieldId == "latency" );
    }
    auto items = columns();
    const auto frozen = makeDatasetSchema( items );
    REQUIRE( frozen.value );
    items[ 0 ].name = QStringLiteral( "Changed outside" );
    CHECK( frozen.value->columns()[ 0 ].name == "Physical line" );
}
TEST_CASE( "F01 optional QString metadata round trips as empty strings",
           "[data][F01][datasetschema]" )
{
    auto items = columns();
    items[ 0 ].unit = QString();
    auto result = makeDatasetSchema( items );
    REQUIRE( result.value );
    roundTrip( *result.value );
    const auto col = jsonRoundTrip( encodeDatasetSchema( *result.value ) )
                         .value( "columns" )
                         .toList()[ 0 ]
                         .toMap();
    CHECK( col.value( "unit" ).userType() == QMetaType::QString );
    CHECK( col.value( "unit" ).toString().isEmpty() );
    CHECK( col.value( "clockDomain" ).userType() == QMetaType::QString );
}
TEST_CASE( "F01 schema preserves known declared types without granting payload capability",
           "[data][F01][datasetschema]" )
{
    const FieldType types[]
        = { FieldType::Missing, FieldType::Null,      FieldType::Bool,     FieldType::Int64,
            FieldType::UInt64,  FieldType::Decimal,   FieldType::Float64,  FieldType::String,
            FieldType::Bytes,   FieldType::Timestamp, FieldType::Duration, FieldType::Array,
            FieldType::Object,  FieldType::Invalid };
    for ( const auto type : types ) {
        auto items = columns();
        items[ 0 ].type = type;
        if ( type == FieldType::Timestamp )
            items[ 0 ].clockDomain = QStringLiteral( "device-boot-A" );
        auto result = makeDatasetSchema( items );
        REQUIRE( result.value );
        roundTrip( *result.value );
    }
    FieldSchema reserved;
    reserved.type = FieldType::Timestamp;
    CHECK( parseTypedValue( QStringView( QStringLiteral( "123" ) ), reserved ).error.code
           == "unsupported_value_type" );
}
TEST_CASE( "F01 schema retains every declared semantic role", "[data][F01][datasetschema]" )
{
    const std::pair<DatasetRole, const char*> roles[]
        = { { DatasetRole::Unspecified, "unspecified" },
            { DatasetRole::Dimension, "dimension" },
            { DatasetRole::Measure, "measure" },
            { DatasetRole::Time, "time" },
            { DatasetRole::Source, "source" },
            { DatasetRole::Event, "event" },
            { DatasetRole::LowerBound, "lower_bound" },
            { DatasetRole::UpperBound, "upper_bound" },
            { DatasetRole::Count, "count" },
            { DatasetRole::Numerator, "numerator" },
            { DatasetRole::Denominator, "denominator" },
            { DatasetRole::Duration, "duration" },
            { DatasetRole::HierarchyPath, "hierarchy_path" },
            { DatasetRole::EdgeSource, "edge_source" },
            { DatasetRole::EdgeTarget, "edge_target" },
            { DatasetRole::Lineage, "lineage" } };
    for ( const auto& entry : roles ) {
        auto items = columns();
        items[ 0 ].semanticRole = entry.first;
        if ( entry.first == DatasetRole::Time ) {
            items[ 0 ].type = FieldType::Timestamp;
            items[ 0 ].clockDomain = QStringLiteral( "UTC" );
        }
        auto result = makeDatasetSchema( items );
        REQUIRE( result.value );
        roundTrip( *result.value );
        CHECK( encodeDatasetSchema( *result.value )
                   .value( "columns" )
                   .toList()[ 0 ]
                   .toMap()
                   .value( "semanticRole" )
                   .toString()
               == QString::fromLatin1( entry.second ) );
    }
}

TEST_CASE( "F01 schema time columns require explicit clock identity", "[data][F01][datasetschema]" )
{
    auto items = columns();
    items[ 0 ].type = FieldType::Timestamp;
    items[ 0 ].semanticRole = DatasetRole::Time;
    CHECK( makeDatasetSchema( items ).error.code == "missing_clock_domain" );
    items[ 0 ].clockDomain = QStringLiteral( "clock-A" );
    auto a = makeDatasetSchema( items );
    REQUIRE( a.value );
    roundTrip( *a.value );
    items[ 0 ].clockDomain = QStringLiteral( "clock-B" );
    auto b = makeDatasetSchema( items );
    REQUIRE( b.value );
    CHECK( a.value->schemaId() != b.value->schemaId() );
    items[ 0 ].type = FieldType::Float64;
    CHECK( makeDatasetSchema( items ).error.code == "invalid_time_column" );
}
TEST_CASE( "F01 schema refuses ambiguous IDs and unsupported metadata",
           "[data][F01][datasetschema]" )
{
    auto items = columns();
    items[ 1 ].fieldId = items[ 0 ].fieldId;
    CHECK( makeDatasetSchema( items ).error.code == "duplicate_field_id" );
    items = columns();
    items[ 0 ].fieldId.clear();
    CHECK_FALSE( makeDatasetSchema( items ).value );
    items = columns();
    items[ 0 ].name.clear();
    CHECK_FALSE( makeDatasetSchema( items ).value );
    items = columns();
    items[ 0 ].type = static_cast<FieldType>( 99 );
    CHECK( makeDatasetSchema( items ).error.code == "unsupported_value_type" );
    items = columns();
    items[ 0 ].semanticRole = static_cast<DatasetRole>( 99 );
    CHECK( makeDatasetSchema( items ).error.code == "unsupported_semantic_role" );
    CHECK( makeDatasetSchema( columns(), static_cast<DatasetShape>( 99 ) ).error.code
           == "unsupported_dataset_shape" );
    CHECK( makeDatasetSchema( columns(), DatasetShape::Table, 2 ).error.code
           == "unsupported_shape_version" );
}
TEST_CASE( "F01 schema bounds field counts and Unicode metadata without dropping columns",
           "[data][F01][datasetschema]" )
{
    QVector<DatasetColumn> items;
    for ( int i = 0; i < 64; ++i )
        items.push_back( { QString::number( i ), QStringLiteral( "Value" ), FieldType::String } );
    auto accepted = makeDatasetSchema( items );
    REQUIRE( accepted.value );
    CHECK( accepted.value->columns().size() == 64 );
    items.push_back( { QStringLiteral( "extra" ), QStringLiteral( "Value" ), FieldType::String } );
    CHECK( makeDatasetSchema( items ).error.code == "schema_column_limit" );
    items = columns();
    items[ 0 ].fieldId = QString( 129, QLatin1Char( 'x' ) );
    CHECK_FALSE( makeDatasetSchema( items ).value );
    items = columns();
    items[ 0 ].name = QString( 257, QLatin1Char( 'x' ) );
    CHECK_FALSE( makeDatasetSchema( items ).value );
    items = columns();
    items[ 0 ].unit = QString( 65, QLatin1Char( 'x' ) );
    CHECK_FALSE( makeDatasetSchema( items ).value );
    items = columns();
    items[ 0 ].clockDomain = QString( 257, QLatin1Char( 'x' ) );
    CHECK_FALSE( makeDatasetSchema( items ).value );
    items = columns();
    items[ 0 ].name = QString( QChar( 0xd800 ) );
    CHECK_FALSE( makeDatasetSchema( items ).value );
    items = columns();
    items[ 0 ].name = QStringLiteral( "a\nb" );
    CHECK_FALSE( makeDatasetSchema( items ).value );
    items = columns();
    items[ 0 ].name = QString::fromUtf8( "Duration \xf0\x9f\x98\x80" );
    auto unicode = makeDatasetSchema( items );
    REQUIRE( unicode.value );
    roundTrip( *unicode.value );
    items.clear();
    for ( int i = 0; i < 64; ++i )
        items.push_back(
            { QString::number( i ), QString( 256, QChar( 0x20ac ) ), FieldType::String } );
    CHECK( makeDatasetSchema( items ).error.code == "schema_metadata_limit" );
}
TEST_CASE( "F01 schema decoder rejects loose types versions and tampered content",
           "[data][F01][datasetschema]" )
{
    auto result = makeDatasetSchema( columns() );
    REQUIRE( result.value );
    const auto good = encodeDatasetSchema( *result.value );
    for ( const QVariant v : { QVariant( true ), QVariant( QStringLiteral( "1" ) ), QVariant( 2 ),
                               QVariant( 1.0000000000000002 ),
                               QVariant( std::numeric_limits<double>::infinity() ) } ) {
        auto bad = good;
        bad.insert( "schemaVersion", v );
        CHECK_FALSE( decodeDatasetSchema( bad ).value );
    }
    auto bad = good;
    bad.insert( "extra", true );
    CHECK_FALSE( decodeDatasetSchema( bad ).value );
    bad = good;
    bad.remove( "columns" );
    CHECK_FALSE( decodeDatasetSchema( bad ).value );
    bad = good;
    bad.insert( "columns", QVariantMap{} );
    CHECK_FALSE( decodeDatasetSchema( bad ).value );
    bad = good;
    bad.insert( "shape", QStringLiteral( "unimplemented" ) );
    CHECK_FALSE( decodeDatasetSchema( bad ).value );
    bad = good;
    bad.insert( "shapeMetadata", QVariantMap{ { "schemaVersion", 2 } } );
    CHECK_FALSE( decodeDatasetSchema( bad ).value );
    auto cols = good.value( "columns" ).toList();
    auto col = cols[ 0 ].toMap();
    for ( const auto key : { "fieldId", "name", "type", "unit", "semanticRole", "clockDomain",
                             "nullable", "repeated" } ) {
        auto missing = col;
        missing.remove( key );
        auto list = cols;
        list[ 0 ] = missing;
        bad = good;
        bad.insert( "columns", list );
        CHECK_FALSE( decodeDatasetSchema( bad ).value );
    }
    for ( const QVariant v :
          { QVariant( 0 ), QVariant( QStringLiteral( "false" ) ), QVariant() } ) {
        auto wrong = col;
        wrong.insert( "nullable", v );
        auto list = cols;
        list[ 0 ] = wrong;
        bad = good;
        bad.insert( "columns", list );
        CHECK_FALSE( decodeDatasetSchema( bad ).value );
    }
    col.insert( "unit", QStringLiteral( "other" ) );
    cols[ 0 ] = col;
    bad = good;
    bad.insert( "columns", cols );
    CHECK( decodeDatasetSchema( bad ).error.code == "schema_identity_mismatch" );
}

TEST_CASE( "F01 schema fingerprint matches the versioned framing fixture",
           "[data][F01][datasetschema]" )
{
    auto schema = makeDatasetSchema( columns() );
    REQUIRE( schema.value );
    CHECK( schema.value->schemaId()
           == "schema1:59c22156205b96fa10172686495f1a157b0138c0e14093dab98b1f843723adf2" );
    auto a = columns(), b = columns();
    a[ 0 ].fieldId = QStringLiteral( "a" );
    a[ 0 ].name = QStringLiteral( "bc" );
    b[ 0 ].fieldId = QStringLiteral( "ab" );
    b[ 0 ].name = QStringLiteral( "c" );
    auto left = makeDatasetSchema( a ), right = makeDatasetSchema( b );
    REQUIRE( left.value );
    REQUIRE( right.value );
    CHECK( left.value->schemaId() != right.value->schemaId() );
}
TEST_CASE( "F01 schemas allow empty tables and duplicate labels but never duplicate IDs",
           "[data][F01][datasetschema]" )
{
    auto empty = makeDatasetSchema( {} );
    REQUIRE( empty.value );
    roundTrip( *empty.value );
    CHECK( empty.value->columns().isEmpty() );
    auto items = columns();
    items[ 0 ].name = items[ 1 ].name;
    auto named = makeDatasetSchema( items );
    REQUIRE( named.value );
    roundTrip( *named.value );
    items = columns();
    items[ 0 ].name = QString::fromUtf8( "\xf0\x9f\x98\x80" ).repeated( 128 );
    REQUIRE( makeDatasetSchema( items ).value );
    items[ 0 ].name += QString::fromUtf8( "\xf0\x9f\x98\x80" );
    CHECK_FALSE( makeDatasetSchema( items ).value );
}

TEST_CASE( "F01 schema wire rejects nested extras coercions and unknown capability tags",
           "[data][F01][datasetschema]" )
{
    auto schema = makeDatasetSchema( columns() );
    REQUIRE( schema.value );
    auto good = encodeDatasetSchema( *schema.value );
    const auto original = good.value( "columns" ).toList();
    const auto column = original[ 0 ].toMap();
    for ( const auto key : { "type", "semanticRole", "fieldId", "name", "unit", "clockDomain" } ) {
        auto col = column;
        col.insert( key, true );
        auto list = original;
        list[ 0 ] = col;
        auto bad = good;
        bad.insert( "columns", list );
        CHECK_FALSE( decodeDatasetSchema( bad ).value );
    }
    auto col = column;
    col.insert( "physicalLineNumber", QStringLiteral( "7" ) );
    auto list = original;
    list[ 0 ] = col;
    auto bad = good;
    bad.insert( "columns", list );
    CHECK_FALSE( decodeDatasetSchema( bad ).value );
    col = column;
    col.insert( "type", QStringLiteral( "mystery" ) );
    list = original;
    list[ 0 ] = col;
    bad = good;
    bad.insert( "columns", list );
    CHECK( decodeDatasetSchema( bad ).error.code == "unsupported_value_type" );
    col = column;
    col.insert( "semanticRole", QStringLiteral( "mystery" ) );
    list = original;
    list[ 0 ] = col;
    bad = good;
    bad.insert( "columns", list );
    CHECK( decodeDatasetSchema( bad ).error.code == "unsupported_semantic_role" );
    list.clear();
    for ( int i = 0; i < 65; ++i )
        list.push_back( column );
    bad = good;
    bad.insert( "columns", list );
    CHECK( decodeDatasetSchema( bad ).error.code == "schema_column_limit" );
    bad = good;
    bad.insert( "shapeMetadata", QVariantMap{ { "schemaVersion", 1 }, { "extra", false } } );
    CHECK_FALSE( decodeDatasetSchema( bad ).value );
}
