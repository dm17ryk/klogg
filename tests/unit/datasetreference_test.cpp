// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "datasetreference.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <catch2/catch.hpp>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
DatasetRevisionStamp stamp( QString job = QStringLiteral( "job-A" ), quint64 dataset = 7,
                            quint64 request = 8, quint64 manifest = 9,
                            QString schema = QStringLiteral( "schema-A" ) )
{
    auto value = makeDatasetRevisionStamp( job, dataset, request, manifest, schema );
    REQUIRE( value.value );
    return *value.value;
}
DatasetReference reference( DatasetReferenceKind kind = DatasetReferenceKind::Cursor,
                            QString token = QStringLiteral( "issued-token" ) )
{
    auto value = makeDatasetReference( kind, stamp(), token );
    REQUIRE( value.value );
    return *value.value;
}
QVariantMap jsonRoundTrip( const QVariantMap& value )
{
    return QJsonDocument::fromJson( QJsonDocument( QJsonObject::fromVariantMap( value ) ).toJson() )
        .object()
        .toVariantMap();
}
struct Owner final : IDatasetReferenceLookup {
    std::vector<DatasetReference> issued;
    bool expired = false;
    bool throwFailure = false;
    std::optional<DataStatus> forced;
    mutable int calls = 0;
    DataStatus check( DatasetReferenceKind kind, const DatasetRevisionStamp& revision,
                      const QString& token ) const override
    {
        ++calls;
        if ( throwFailure )
            throw std::runtime_error( "owner lookup failed" );
        if ( forced )
            return *forced;
        if ( expired )
            return { false,
                     { QStringLiteral( "expired_job" ), QStringLiteral( "Expired" ), {}, {} } };
        for ( const auto& item : issued )
            if ( item.kind() == kind && item.token() == token ) {
                if ( sameDatasetRevision( item.revision(), revision ) )
                    return { true, {} };
                return { false,
                         { kind == DatasetReferenceKind::Cursor ? QStringLiteral( "stale_cursor" )
                                                                : QStringLiteral( "stale_lineage" ),
                           {},
                           {},
                           {} } };
            }
        return { false,
                 { kind == DatasetReferenceKind::Cursor ? QStringLiteral( "invalid_cursor" )
                                                        : QStringLiteral( "invalid_lineage" ),
                   {},
                   {},
                   {} } };
    }
};
} // namespace
TEST_CASE( "F01 dataset stamps retain exact revision counters through JSON",
           "[data][F01][datasetreference]" )
{
    auto value = makeDatasetRevisionStamp( QStringLiteral( "job" ), 18446744073709551615ULL,
                                           9007199254740993ULL, 0, QStringLiteral( "schema" ) );
    REQUIRE( value.value );
    const auto wire = encodeDatasetRevision( *value.value );
    CHECK( wire.value( "datasetRevision" ).toString() == "18446744073709551615" );
    CHECK( wire.value( "requestRevision" ).toString() == "9007199254740993" );
    CHECK( wire.value( "sourceManifestRevision" ).toString() == "0" );
    auto decoded = decodeDatasetRevision( jsonRoundTrip( wire ) );
    REQUIRE( decoded.value );
    CHECK( sameDatasetRevision( *decoded.value, *value.value ) );
    CHECK( encodeDatasetRevision( *decoded.value ) == wire );
}
TEST_CASE( "F01 matching reference metadata still requires the issuing owner",
           "[data][F01][datasetreference]" )
{
    const auto ref = reference();
    Owner owner;
    CHECK( validateDatasetReference( ref, stamp(), owner ).error.code == "invalid_cursor" );
    owner.issued.push_back( ref );
    CHECK( validateDatasetReference( ref, stamp(), owner ).ok );
    auto decoded = decodeDatasetReference( jsonRoundTrip( encodeDatasetReference( ref ) ) );
    REQUIRE( decoded.value );
    CHECK( validateDatasetReference( *decoded.value, stamp(), owner ).ok );
    owner.issued.clear();
    CHECK_FALSE( validateDatasetReference( *decoded.value, stamp(), owner ).ok );
}
TEST_CASE( "F01 references reject every stale tuple component before lookup",
           "[data][F01][datasetreference]" )
{
    const auto ref = reference();
    Owner owner;
    owner.issued.push_back( ref );
    const DatasetRevisionStamp changed[]
        = { stamp( QStringLiteral( "job-B" ) ), stamp( QStringLiteral( "job-A" ), 10 ),
            stamp( QStringLiteral( "job-A" ), 7, 10 ), stamp( QStringLiteral( "job-A" ), 7, 8, 10 ),
            stamp( QStringLiteral( "job-A" ), 7, 8, 9, QStringLiteral( "schema-B" ) ) };
    for ( const auto& current : changed ) {
        CHECK_FALSE( sameDatasetRevision( current, ref.revision() ) );
        CHECK( validateDatasetReference( ref, current, owner ).error.code == "stale_cursor" );
    }
    CHECK( owner.calls == 0 );
    auto lineage = reference( DatasetReferenceKind::Lineage );
    CHECK( validateDatasetReference( lineage, changed[ 1 ], owner ).error.code == "stale_lineage" );
}
TEST_CASE( "F01 forged current metadata cannot reuse an old issued cursor",
           "[data][F01][datasetreference]" )
{
    const auto old = reference();
    Owner owner;
    owner.issued.push_back( old );
    const auto current = stamp( QStringLiteral( "job-A" ), 10 );
    auto wire = encodeDatasetReference( old );
    wire.insert( "revision", encodeDatasetRevision( current ) );
    auto forged = decodeDatasetReference( jsonRoundTrip( wire ) );
    REQUIRE( forged.value );
    CHECK( validateDatasetReference( *forged.value, current, owner ).error.code == "stale_cursor" );
}
TEST_CASE( "F01 cursor and lineage tokens remain distinct kinds", "[data][F01][datasetreference]" )
{
    Owner owner;
    const auto cursor = reference();
    owner.issued.push_back( cursor );
    const auto lineage = reference( DatasetReferenceKind::Lineage );
    CHECK( validateDatasetReference( lineage, stamp(), owner ).error.code == "invalid_lineage" );
    owner.issued.push_back( lineage );
    CHECK( validateDatasetReference( lineage, stamp(), owner ).ok );
    auto decoded = decodeDatasetReference( jsonRoundTrip( encodeDatasetReference( lineage ) ) );
    REQUIRE( decoded.value );
    CHECK( decoded.value->kind() == DatasetReferenceKind::Lineage );
    CHECK( decoded.value->token() == "issued-token" );
    CHECK( encodeDatasetReference( *decoded.value ) == encodeDatasetReference( lineage ) );
}
TEST_CASE( "F01 owner expiry and failure are errors rather than empty success",
           "[data][F01][datasetreference]" )
{
    const auto ref = reference();
    Owner owner;
    owner.issued.push_back( ref );
    owner.expired = true;
    CHECK( validateDatasetReference( ref, stamp(), owner ).error.code == "expired_job" );
    owner.expired = false;
    owner.forced = DataStatus{ false,
                               { QStringLiteral( "io_error" ),
                                 QStringLiteral( "Retained owner error" ),
                                 QStringLiteral( "field-3" ),
                                 {} } };
    auto error = validateDatasetReference( ref, stamp(), owner );
    CHECK_FALSE( error.ok );
    CHECK( error.error.code == "io_error" );
    CHECK( error.error.message == "Retained owner error" );
    CHECK( error.error.fieldId == "field-3" );
    owner.forced = DataStatus{};
    CHECK( validateDatasetReference( ref, stamp(), owner ).error.code == "invalid_lookup_result" );
    owner.forced = DataStatus{ true, { QStringLiteral( "expired_job" ), {}, {}, {} } };
    CHECK( validateDatasetReference( ref, stamp(), owner ).error.code == "invalid_lookup_result" );
    owner.forced.reset();
    owner.throwFailure = true;
    CHECK_THROWS_AS( validateDatasetReference( ref, stamp(), owner ), std::runtime_error );
}
TEST_CASE( "F01 reference codecs reject unversioned loose and mixed shapes",
           "[data][F01][datasetreference]" )
{
    const auto ref = reference();
    const auto good = encodeDatasetReference( ref );
    for ( const QVariant& version : { QVariant( true ), QVariant( QStringLiteral( "1" ) ),
                                      QVariant( 2 ), QVariant( 1.0000000000000002 ),
                                      QVariant( std::numeric_limits<double>::infinity() ) } ) {
        auto bad = good;
        bad.insert( "schemaVersion", version );
        CHECK_FALSE( decodeDatasetReference( bad ).value );
        auto revision = encodeDatasetRevision( stamp() );
        revision.insert( "schemaVersion", version );
        CHECK_FALSE( decodeDatasetRevision( revision ).value );
    }
    auto bad = good;
    bad.remove( "revision" );
    CHECK_FALSE( decodeDatasetReference( bad ).value );
    bad = good;
    bad.insert( "kind", QStringLiteral( "other" ) );
    CHECK_FALSE( decodeDatasetReference( bad ).value );
    bad = good;
    bad.insert( "lineNumber", QStringLiteral( "7" ) );
    CHECK_FALSE( decodeDatasetReference( bad ).value );
    bad = good;
    bad.insert( "events", QVariantList{} );
    CHECK_FALSE( decodeDatasetReference( bad ).value );
    bad = good;
    bad.insert( "revision", QVariantList{} );
    CHECK_FALSE( decodeDatasetReference( bad ).value );
    bad = good;
    bad.insert( "token", 7 );
    CHECK_FALSE( decodeDatasetReference( bad ).value );
}
TEST_CASE( "F01 revision wire counters are canonical decimal strings",
           "[data][F01][datasetreference]" )
{
    const auto good = encodeDatasetRevision( stamp() );
    for ( const auto key : { "datasetRevision", "requestRevision", "sourceManifestRevision" } ) {
        for ( const QVariant& token :
              { QVariant( 7 ), QVariant( true ), QVariant( QStringLiteral( "07" ) ),
                QVariant( QStringLiteral( "-1" ) ), QVariant( QStringLiteral( "+1" ) ),
                QVariant( QStringLiteral( "18446744073709551616" ) ),
                QVariant( QStringLiteral( " 7" ) ) } ) {
            auto bad = good;
            bad.insert( key, token );
            CHECK_FALSE( decodeDatasetRevision( bad ).value );
        }
    }
    auto bad = good;
    bad.insert( "sourceId", QStringLiteral( "invented" ) );
    CHECK_FALSE( decodeDatasetRevision( bad ).value );
    bad = good;
    bad.remove( "schemaId" );
    CHECK_FALSE( decodeDatasetRevision( bad ).value );
}
TEST_CASE( "F01 reference identifiers are bounded Unicode without source anchors",
           "[data][F01][datasetreference]" )
{
    CHECK_FALSE( makeDatasetRevisionStamp( {}, 1, 2, 3, QStringLiteral( "schema" ) ).value );
    CHECK_FALSE( makeDatasetRevisionStamp( QStringLiteral( "job" ), 1, 2, 3, {} ).value );
    CHECK_FALSE( makeDatasetRevisionStamp( QString( 257, QLatin1Char( 'x' ) ), 1, 2, 3,
                                           QStringLiteral( "schema" ) )
                     .value );
    CHECK_FALSE(
        makeDatasetRevisionStamp( QStringLiteral( "job" ), 1, 2, 3, QString( QChar( 0xd800 ) ) )
            .value );
    auto checked
        = makeDatasetRevisionStamp( QStringLiteral( "job" ), 1, 2, 3, QStringLiteral( "schema" ) );
    REQUIRE( checked.value );
    const auto valid = *checked.value;
    CHECK_FALSE( makeDatasetReference( DatasetReferenceKind::Cursor, valid, {} ).value );
    CHECK_FALSE( makeDatasetReference( DatasetReferenceKind::Cursor, valid,
                                       QString( 257, QLatin1Char( 'x' ) ) )
                     .value );
    CHECK_FALSE(
        makeDatasetReference( DatasetReferenceKind::Cursor, valid, QStringLiteral( "a\nb" ) )
            .value );
    CHECK_FALSE( makeDatasetReference( static_cast<DatasetReferenceKind>( 99 ), valid,
                                       QStringLiteral( "token" ) )
                     .value );
    auto unicode = makeDatasetReference( DatasetReferenceKind::Lineage, valid,
                                         QString::fromUtf8( "row-\xf0\x9f\x98\x80" ) );
    REQUIRE( unicode.value );
    auto decoded
        = decodeDatasetReference( jsonRoundTrip( encodeDatasetReference( *unicode.value ) ) );
    REQUIRE( decoded.value );
    CHECK( decoded.value->token() == unicode.value->token() );
    CHECK_FALSE( encodeDatasetReference( *unicode.value ).contains( "sourceId" ) );
    CHECK_FALSE( encodeDatasetReference( *unicode.value ).contains( "lineNumber" ) );
    CHECK_FALSE( encodeDatasetReference( *unicode.value ).contains( "events" ) );
}
