// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "datasetschema.h"
#include "log.h"
#include <QCryptographicHash>
#include <QMetaType>
#include <QSet>
#include <QtEndian>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace {
using namespace DatasetSchemaLimits;
constexpr FieldType types[]
    = { FieldType::Missing, FieldType::Null,      FieldType::Bool,     FieldType::Int64,
        FieldType::UInt64,  FieldType::Decimal,   FieldType::Float64,  FieldType::String,
        FieldType::Bytes,   FieldType::Timestamp, FieldType::Duration, FieldType::Array,
        FieldType::Object,  FieldType::Invalid };
constexpr DatasetRole roles[]
    = { DatasetRole::Unspecified,   DatasetRole::Dimension,   DatasetRole::Measure,
        DatasetRole::Time,          DatasetRole::Source,      DatasetRole::Event,
        DatasetRole::LowerBound,    DatasetRole::UpperBound,  DatasetRole::Count,
        DatasetRole::Numerator,     DatasetRole::Denominator, DatasetRole::Duration,
        DatasetRole::HierarchyPath, DatasetRole::EdgeSource,  DatasetRole::EdgeTarget,
        DatasetRole::Lineage };
DataResult<DatasetSchema> failure( const char* code, int column = -1, const QString& fieldId = {} )
{
    const auto reason = QString::fromLatin1( code );
    LOG_DEBUG << "F01 schema rejected reason=" << reason << " column=" << column;
    return { {}, { reason, reason, fieldId, {} } };
}
bool text( const QString& value, int maximum, bool emptyAllowed )
{
    if ( value.size() > maximum || ( !emptyAllowed && value.isEmpty() ) )
        return false;
    for ( int i = 0; i < value.size(); ++i ) {
        const auto ch = value.at( i );
        if ( ch.unicode() < 32 || ch.unicode() == 127 )
            return false;
        if ( ch.isHighSurrogate() ) {
            if ( ++i >= value.size() || !value.at( i ).isLowSurrogate() )
                return false;
        }
        else if ( ch.isLowSurrogate() )
            return false;
    }
    return true;
}
bool knownType( FieldType type )
{
    for ( const auto candidate : types )
        if ( type == candidate )
            return true;
    return false;
}
void frame( QCryptographicHash& hash, const QByteArray& bytes )
{
    // All callers use bounded validated text or small fixed tokens.
    const auto length = qToBigEndian<quint32>( static_cast<quint32>( bytes.size() ) );
    hash.addData( QByteArray( reinterpret_cast<const char*>( &length ),
                              static_cast<int>( sizeof( length ) ) ) );
    hash.addData( bytes );
}
QString fingerprint( const QVector<DatasetColumn>& columns )
{
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    frame( hash, QByteArrayLiteral( "cilogg.dataset-schema.v1" ) );
    frame( hash, QByteArrayLiteral( "table" ) );
    frame( hash, QByteArrayLiteral( "1" ) );
    frame( hash, QByteArray::number( static_cast<int>( columns.size() ) ) );
    for ( const auto& column : columns ) {
        for ( const auto& value :
              { column.fieldId, column.name, fieldTypeName( column.type ), column.unit,
                datasetRoleName( column.semanticRole ), column.clockDomain } )
            frame( hash, value.toUtf8() );
        frame( hash, column.nullable ? QByteArrayLiteral( "1" ) : QByteArrayLiteral( "0" ) );
        frame( hash, column.repeated ? QByteArrayLiteral( "1" ) : QByteArrayLiteral( "0" ) );
    }
    return QStringLiteral( "schema1:" ) + QString::fromLatin1( hash.result().toHex() );
}
bool keys( const QVariantMap& map, std::initializer_list<const char*> required )
{
    if ( map.size() != static_cast<int>( required.size() ) )
        return false;
    for ( const auto* key : required )
        if ( !map.contains( QString::fromLatin1( key ) ) )
            return false;
    return true;
}
bool mapValue( const QVariant& value, QVariantMap& result )
{
    if ( value.userType() != QMetaType::QVariantMap )
        return false;
    result = value.toMap();
    return true;
}
bool stringValue( const QVariantMap& map, const char* key, QString& result )
{
    const auto value = map.value( QString::fromLatin1( key ) );
    if ( value.userType() != QMetaType::QString )
        return false;
    result = value.toString();
    return true;
}
bool versionOne( const QVariant& value )
{
    switch ( value.userType() ) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return value.toString() == QStringLiteral( "1" );
    case QMetaType::Double: {
        const auto number = value.toDouble();
        quint64 bits = 0;
        static_assert( sizeof( bits ) == sizeof( number ), "64-bit JSON double required" );
        std::memcpy( &bits, &number, sizeof( bits ) );
        return bits == 0x3ff0000000000000ULL;
    }
    default:
        return false;
    }
}
} // namespace
QString datasetRoleName( DatasetRole role )
{
    switch ( role ) {
    case DatasetRole::Unspecified:
        return QStringLiteral( "unspecified" );
    case DatasetRole::Dimension:
        return QStringLiteral( "dimension" );
    case DatasetRole::Measure:
        return QStringLiteral( "measure" );
    case DatasetRole::Time:
        return QStringLiteral( "time" );
    case DatasetRole::Source:
        return QStringLiteral( "source" );
    case DatasetRole::Event:
        return QStringLiteral( "event" );
    case DatasetRole::LowerBound:
        return QStringLiteral( "lower_bound" );
    case DatasetRole::UpperBound:
        return QStringLiteral( "upper_bound" );
    case DatasetRole::Count:
        return QStringLiteral( "count" );
    case DatasetRole::Numerator:
        return QStringLiteral( "numerator" );
    case DatasetRole::Denominator:
        return QStringLiteral( "denominator" );
    case DatasetRole::Duration:
        return QStringLiteral( "duration" );
    case DatasetRole::HierarchyPath:
        return QStringLiteral( "hierarchy_path" );
    case DatasetRole::EdgeSource:
        return QStringLiteral( "edge_source" );
    case DatasetRole::EdgeTarget:
        return QStringLiteral( "edge_target" );
    case DatasetRole::Lineage:
        return QStringLiteral( "lineage" );
    }
    return QStringLiteral( "unsupported" );
}
DatasetSchema::DatasetSchema( QVector<DatasetColumn> columns, QString id )
    : columns_( std::move( columns ) )
    , schemaId_( std::move( id ) )
{
}
DataResult<DatasetSchema> makeDatasetSchema( QVector<DatasetColumn> columns, DatasetShape shape,
                                             quint32 shapeVersion )
{
    LOG_DEBUG << "F01 schema requested columns=" << columns.size()
              << " shapeVersion=" << shapeVersion;
    if ( shape != DatasetShape::Table )
        return failure( "unsupported_dataset_shape" );
    if ( shapeVersion != 1 )
        return failure( "unsupported_shape_version" );
    if ( columns.size() > MaxColumns )
        return failure( "schema_column_limit" );
    QSet<QString> ids;
    // Fixed shape/version text plus all column text and two Boolean bytes per column.
    int used = 6;
    for ( int i = 0; i < columns.size(); ++i ) {
        auto& column = columns[ i ];
        if ( !text( column.fieldId, MaxFieldIdUnits, false ) )
            return failure( "invalid_field_id", i );
        if ( ids.contains( column.fieldId ) )
            return failure( "duplicate_field_id", i, column.fieldId );
        ids.insert( column.fieldId );
        if ( !text( column.name, MaxNameUnits, false ) )
            return failure( "invalid_column_name", i, column.fieldId );
        if ( !text( column.unit, MaxUnitUnits, true ) )
            return failure( "invalid_column_unit", i, column.fieldId );
        if ( !text( column.clockDomain, MaxClockDomainUnits, true ) )
            return failure( "invalid_clock_domain", i, column.fieldId );
        if ( !knownType( column.type ) )
            return failure( "unsupported_value_type", i, column.fieldId );
        const auto role = datasetRoleName( column.semanticRole );
        if ( role == QLatin1String( "unsupported" ) )
            return failure( "unsupported_semantic_role", i, column.fieldId );
        if ( column.semanticRole == DatasetRole::Time && column.type != FieldType::Timestamp )
            return failure( "invalid_time_column", i, column.fieldId );
        if ( ( column.type == FieldType::Timestamp || column.semanticRole == DatasetRole::Time )
             && column.clockDomain.isEmpty() )
            return failure( "missing_clock_domain", i, column.fieldId );
        // Null QString would become JSON null on some Qt runtimes; optional text is always a
        // string.
        if ( column.unit.isNull() )
            column.unit = QStringLiteral( "" );
        if ( column.clockDomain.isNull() )
            column.clockDomain = QStringLiteral( "" );
        for ( const auto& value : { column.fieldId, column.name, fieldTypeName( column.type ),
                                    column.unit, role, column.clockDomain } )
            used += static_cast<int>( value.toUtf8().size() );
        used += 2;
        if ( used > MaxMetadataUtf8Bytes )
            return failure( "schema_metadata_limit", i, column.fieldId );
        LOG_DEBUG << "F01 schema column accepted ordinal=" << i
                  << " type=" << fieldTypeName( column.type ) << " role=" << role
                  << " clockBound=" << !column.clockDomain.isEmpty()
                  << " nullable=" << column.nullable << " repeated=" << column.repeated
                  << " metadataBytes=" << used;
    }
    const auto id = fingerprint( columns );
    LOG_DEBUG << "F01 schema accepted id=" << id << " columns=" << columns.size()
              << " metadataBytes=" << used;
    return { DatasetSchema( std::move( columns ), id ), {} };
}
QVariantMap encodeDatasetSchema( const DatasetSchema& schema )
{
    QVariantList columns;
    for ( const auto& column : schema.columns() ) {
        columns.push_back( QVariantMap{ { "fieldId", column.fieldId },
                                        { "name", column.name },
                                        { "type", fieldTypeName( column.type ) },
                                        { "unit", column.unit },
                                        { "semanticRole", datasetRoleName( column.semanticRole ) },
                                        { "clockDomain", column.clockDomain },
                                        { "nullable", column.nullable },
                                        { "repeated", column.repeated } } );
    }
    return { { "schemaVersion", 1 },
             { "schemaId", schema.schemaId() },
             { "shape", QStringLiteral( "table" ) },
             { "shapeMetadata", QVariantMap{ { "schemaVersion", 1 } } },
             { "columns", columns } };
}
DataResult<DatasetSchema> decodeDatasetSchema( const QVariantMap& wire )
{
    LOG_DEBUG << "F01 schema decoding fields=" << wire.size();
    if ( !keys( wire, { "schemaVersion", "schemaId", "shape", "shapeMetadata", "columns" } ) )
        return failure( "invalid_dataset_schema" );
    if ( !versionOne( wire.value( "schemaVersion" ) ) )
        return failure( "unsupported_dataset_schema_version" );
    QString shape, id;
    if ( !stringValue( wire, "shape", shape ) || shape != QLatin1String( "table" ) )
        return failure( "unsupported_dataset_shape" );
    QVariantMap metadata;
    if ( !mapValue( wire.value( "shapeMetadata" ), metadata )
         || !keys( metadata, { "schemaVersion" } ) )
        return failure( "invalid_shape_metadata" );
    if ( !versionOne( metadata.value( "schemaVersion" ) ) )
        return failure( "unsupported_shape_version" );
    if ( !stringValue( wire, "schemaId", id ) || id.size() != 72 || !id.startsWith( "schema1:" ) )
        return failure( "invalid_schema_identity" );
    const auto array = wire.value( "columns" );
    if ( array.userType() != QMetaType::QVariantList )
        return failure( "invalid_schema_columns" );
    const auto input = array.toList();
    if ( input.size() > MaxColumns )
        return failure( "schema_column_limit" );
    QVector<DatasetColumn> columns;
    columns.reserve( static_cast<int>( input.size() ) );
    for ( int i = 0; i < input.size(); ++i ) {
        QVariantMap map;
        DatasetColumn column;
        QString type, role;
        if ( !mapValue( input[ i ], map )
             || !keys( map, { "fieldId", "name", "type", "unit", "semanticRole", "clockDomain",
                              "nullable", "repeated" } )
             || !stringValue( map, "fieldId", column.fieldId )
             || !stringValue( map, "name", column.name ) || !stringValue( map, "type", type )
             || !stringValue( map, "unit", column.unit )
             || !stringValue( map, "semanticRole", role )
             || !stringValue( map, "clockDomain", column.clockDomain ) )
            return failure( "invalid_dataset_column", i );
        if ( map.value( "nullable" ).userType() != QMetaType::Bool
             || map.value( "repeated" ).userType() != QMetaType::Bool )
            return failure( "invalid_column_flags", i );
        bool found = false;
        for ( const auto candidate : types )
            if ( fieldTypeName( candidate ) == type ) {
                column.type = candidate;
                found = true;
                break;
            }
        if ( !found )
            return failure( "unsupported_value_type", i );
        found = false;
        for ( const auto candidate : roles )
            if ( datasetRoleName( candidate ) == role ) {
                column.semanticRole = candidate;
                found = true;
                break;
            }
        if ( !found )
            return failure( "unsupported_semantic_role", i );
        column.nullable = map.value( "nullable" ).toBool();
        column.repeated = map.value( "repeated" ).toBool();
        columns.push_back( std::move( column ) );
    }
    auto result = makeDatasetSchema( std::move( columns ) );
    if ( result.value && result.value->schemaId() != id )
        return failure( "schema_identity_mismatch" );
    return result;
}
