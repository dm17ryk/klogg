// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "datasetreference.h"
#include "log.h"
#include <QCryptographicHash>
#include <QMetaType>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace {
cilogg::DataStatus failureStatus( const char* code )
{
    const auto reason = QString::fromLatin1( code );
    LOG_DEBUG << "F01 dataset reference rejected reason=" << reason;
    return { false, { reason, reason, {}, {} } };
}
template <class T>
DataResult<T> failure( const char* code )
{
    return { {}, failureStatus( code ).error };
}
bool text( const QString& value, int maximum )
{
    if ( value.isEmpty() || value.size() > maximum )
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
const char* kindName( DatasetReferenceKind kind )
{
    switch ( kind ) {
    case DatasetReferenceKind::Cursor:
        return "cursor";
    case DatasetReferenceKind::Lineage:
        return "lineage";
    }
    return nullptr;
}
QString logIdentity( const QString& value )
{
    // Correlate decisions without logging tokens, selectors or caller-provided identifiers.
    return QString::fromLatin1(
        QCryptographicHash::hash( value.toUtf8(), QCryptographicHash::Sha256 ).toHex() );
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
bool stringValue( const QVariantMap& map, const char* key, QString& result )
{
    const auto value = map.value( QString::fromLatin1( key ) );
    if ( value.userType() != QMetaType::QString )
        return false;
    result = value.toString();
    return true;
}
bool u64( const QVariantMap& map, const char* key, quint64& result )
{
    const auto value = map.value( QString::fromLatin1( key ) );
    if ( value.userType() != QMetaType::QString )
        return false;
    const auto token = value.toString();
    if ( token.isEmpty() || token.size() > 20
         || ( token.size() > 1 && token.front() == QLatin1Char( '0' ) ) )
        return false;
    for ( const auto ch : token )
        if ( ch < QLatin1Char( '0' ) || ch > QLatin1Char( '9' ) )
            return false;
    bool ok = false;
    result = token.toULongLong( &ok );
    return ok;
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
bool emptyError( const DataError& error )
{
    return error.code.isEmpty() && error.message.isEmpty() && error.fieldId.isEmpty()
           && !error.location;
}
} // namespace
DatasetRevisionStamp::DatasetRevisionStamp( QString job, quint64 dataset, quint64 request,
                                            quint64 manifest, QString schema )
    : jobId_( std::move( job ) )
    , datasetRevision_( dataset )
    , requestRevision_( request )
    , sourceManifestRevision_( manifest )
    , schemaId_( std::move( schema ) )
{
}
DataResult<DatasetRevisionStamp> makeDatasetRevisionStamp( QString job, quint64 dataset,
                                                           quint64 request, quint64 manifest,
                                                           QString schema )
{
    if ( !text( job, DatasetReferenceLimits::MaxIdUnits ) )
        return failure<DatasetRevisionStamp>( "invalid_job_id" );
    if ( !text( schema, DatasetReferenceLimits::MaxIdUnits ) )
        return failure<DatasetRevisionStamp>( "invalid_schema_id" );
    LOG_DEBUG << "F01 dataset stamp accepted jobHash=" << logIdentity( job )
              << " datasetRevision=" << dataset << " requestRevision=" << request
              << " manifestRevision=" << manifest << " schemaHash=" << logIdentity( schema );
    return { DatasetRevisionStamp( std::move( job ), dataset, request, manifest,
                                   std::move( schema ) ),
             {} };
}
bool sameDatasetRevision( const DatasetRevisionStamp& first, const DatasetRevisionStamp& second )
{
    return first.jobId() == second.jobId() && first.datasetRevision() == second.datasetRevision()
           && first.requestRevision() == second.requestRevision()
           && first.sourceManifestRevision() == second.sourceManifestRevision()
           && first.schemaId() == second.schemaId();
}
DatasetReference::DatasetReference( DatasetReferenceKind kind, DatasetRevisionStamp revision,
                                    QString token )
    : kind_( kind )
    , revision_( std::move( revision ) )
    , token_( std::move( token ) )
{
}
DataResult<DatasetReference> makeDatasetReference( DatasetReferenceKind kind,
                                                   DatasetRevisionStamp revision, QString token )
{
    if ( !kindName( kind ) )
        return failure<DatasetReference>( "unsupported_reference_kind" );
    if ( !text( token, DatasetReferenceLimits::MaxTokenUnits ) )
        return failure<DatasetReference>( "invalid_reference_token" );
    LOG_DEBUG << "F01 dataset reference accepted kind=" << kindName( kind )
              << " jobHash=" << logIdentity( revision.jobId() )
              << " datasetRevision=" << revision.datasetRevision()
              << " tokenUnits=" << token.size();
    return { DatasetReference( kind, std::move( revision ), std::move( token ) ), {} };
}
cilogg::DataStatus validateDatasetReference( const DatasetReference& reference,
                                             const DatasetRevisionStamp& current,
                                             const IDatasetReferenceLookup& owner )
{
    const auto& original = reference.revision();
    if ( !sameDatasetRevision( original, current ) ) {
        LOG_DEBUG << "F01 stale dataset reference kind=" << kindName( reference.kind() )
                  << " currentJobHash=" << logIdentity( current.jobId() )
                  << " jobMismatch=" << ( original.jobId() != current.jobId() )
                  << " datasetRevision=" << original.datasetRevision()
                  << " currentDatasetRevision=" << current.datasetRevision() << " requestMismatch="
                  << ( original.requestRevision() != current.requestRevision() )
                  << " manifestMismatch="
                  << ( original.sourceManifestRevision() != current.sourceManifestRevision() )
                  << " schemaMismatch=" << ( original.schemaId() != current.schemaId() );
        return failureStatus( reference.kind() == DatasetReferenceKind::Cursor ? "stale_cursor"
                                                                               : "stale_lineage" );
    }
    LOG_DEBUG << "F01 checking dataset reference owner kind=" << kindName( reference.kind() )
              << " jobHash=" << logIdentity( current.jobId() )
              << " datasetRevision=" << current.datasetRevision();
    // No catch/fallback: owner failures and exceptions must reach the service boundary.
    const auto outcome = owner.check( reference.kind(), current, reference.token() );
    if ( ( outcome.ok && !emptyError( outcome.error ) )
         || ( !outcome.ok && outcome.error.code.isEmpty() ) )
        return failureStatus( "invalid_lookup_result" );
    LOG_DEBUG << "F01 dataset reference owner result kind=" << kindName( reference.kind() )
              << " jobHash=" << logIdentity( current.jobId() ) << " accepted=" << outcome.ok;
    return outcome;
}
QVariantMap encodeDatasetRevision( const DatasetRevisionStamp& revision )
{
    return { { "schemaVersion", 1 },
             { "jobId", revision.jobId() },
             { "datasetRevision", QString::number( revision.datasetRevision() ) },
             { "requestRevision", QString::number( revision.requestRevision() ) },
             { "sourceManifestRevision", QString::number( revision.sourceManifestRevision() ) },
             { "schemaId", revision.schemaId() } };
}
DataResult<DatasetRevisionStamp> decodeDatasetRevision( const QVariantMap& wire )
{
    if ( !keys( wire, { "schemaVersion", "jobId", "datasetRevision", "requestRevision",
                        "sourceManifestRevision", "schemaId" } ) )
        return failure<DatasetRevisionStamp>( "invalid_dataset_revision" );
    if ( !versionOne( wire.value( "schemaVersion" ) ) )
        return failure<DatasetRevisionStamp>( "unsupported_dataset_revision_version" );
    QString job, schema;
    quint64 dataset = 0, request = 0, manifest = 0;
    if ( !stringValue( wire, "jobId", job ) || !stringValue( wire, "schemaId", schema )
         || !u64( wire, "datasetRevision", dataset ) || !u64( wire, "requestRevision", request )
         || !u64( wire, "sourceManifestRevision", manifest ) )
        return failure<DatasetRevisionStamp>( "invalid_dataset_revision" );
    return makeDatasetRevisionStamp( std::move( job ), dataset, request, manifest,
                                     std::move( schema ) );
}
QVariantMap encodeDatasetReference( const DatasetReference& reference )
{
    return { { "schemaVersion", 1 },
             { "kind", QString::fromLatin1( kindName( reference.kind() ) ) },
             { "revision", encodeDatasetRevision( reference.revision() ) },
             { "token", reference.token() } };
}
DataResult<DatasetReference> decodeDatasetReference( const QVariantMap& wire )
{
    if ( !keys( wire, { "schemaVersion", "kind", "revision", "token" } ) )
        return failure<DatasetReference>( "invalid_dataset_reference" );
    if ( !versionOne( wire.value( "schemaVersion" ) ) )
        return failure<DatasetReference>( "unsupported_dataset_reference_version" );
    QString kind, token;
    if ( !stringValue( wire, "kind", kind ) || !stringValue( wire, "token", token ) )
        return failure<DatasetReference>( "invalid_dataset_reference" );
    DatasetReferenceKind tag;
    if ( kind == QLatin1String( "cursor" ) )
        tag = DatasetReferenceKind::Cursor;
    else if ( kind == QLatin1String( "lineage" ) )
        tag = DatasetReferenceKind::Lineage;
    else
        return failure<DatasetReference>( "unsupported_reference_kind" );
    const auto nested = wire.value( "revision" );
    if ( nested.userType() != QMetaType::QVariantMap )
        return failure<DatasetReference>( "invalid_dataset_revision" );
    auto revision = decodeDatasetRevision( nested.toMap() );
    if ( !revision.value )
        return { {}, revision.error };
    return makeDatasetReference( tag, std::move( *revision.value ), std::move( token ) );
}
