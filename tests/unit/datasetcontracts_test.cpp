// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "eventrecord.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <catch2/catch.hpp>
#include <limits>

namespace {
const EventIdentityPolicy policy{ QStringLiteral( "ehci" ), 2, 3, 0 };
struct Source {
    std::shared_ptr<SourceLifetime> life = std::make_shared<SourceLifetime>();
    SourceSnapshot snapshot;
    Source()
    {
        snapshot.sourceId = QUuid( "813d99f8-c241-450f-9023-e891f2414c83" );
        snapshot.generation = 7;
        snapshot.revision = 9;
        snapshot.indexedBytes = 20;
        snapshot.lineCount = 3_lcount;
        snapshot.completeLineCount = 2_lcount;
        snapshot.provisionalLineStart = 16;
        snapshot.available = true;
        snapshot.lifetime = life;
    }
};
SourceManifestEntry entry()
{
    return { QStringLiteral( "manifest" ),
             QStringLiteral( "member" ),
             4,
             5,
             { QStringLiteral( "/logs/member.log" ), QByteArray( "file-id" ), 20, 20, 2, 16,
               QByteArray( 32, 'x' ), QStringLiteral( "UTF-16LE" ), 8 } };
}
DeferredFileOrigin deferred()
{
    return { QStringLiteral( "manifest" ), QStringLiteral( "member" ), 4, 5, 0, 2,
             RawByteRange{ 0, 20 } };
}
ManagedByteOrigin managed()
{
    return { QStringLiteral( "communication" ),
             QStringLiteral( "recording" ),
             QStringLiteral( "segment" ),
             17,
             { 2, 10 } };
}
QVariantMap jsonRoundTrip( const QVariantMap& map )
{
    return QJsonDocument::fromJson( QJsonDocument( QJsonObject::fromVariantMap( map ) ).toJson() )
        .object()
        .toVariantMap();
}
void roundTrip( const EventRef& ref )
{
    const auto wire = encodeEventRef( ref );
    const auto decoded = decodeEventRef( jsonRoundTrip( wire ) );
    REQUIRE( decoded.value );
    CHECK( encodeEventRef( *decoded.value ) == wire );
    CHECK( decoded.value->eventKey() == ref.eventKey() );
    CHECK( validateEventOrigin( *decoded.value ).ok );
}
} // namespace

TEST_CASE( "F01 physical events validate both endpoints after append", "[data][F01][eventorigin]" )
{
    Source source;
    const auto completed
        = makeEventRef( source.snapshot, 0_lnum, 1_lnum, RawByteRange{ 0, 16 }, policy );
    const auto tail
        = makeEventRef( source.snapshot, 0_lnum, 2_lnum, RawByteRange{ 0, 20 }, policy );
    REQUIRE( completed.value );
    REQUIRE( tail.value );
    CHECK( acceptsEvent( source.snapshot, *tail.value ) );
    auto appended = source.snapshot;
    appended.revision = 10;
    appended.indexedBytes = 25;
    appended.lineCount = 4_lcount;
    appended.completeLineCount = 3_lcount;
    appended.provisionalLineStart = 24;
    CHECK( acceptsEvent( appended, *completed.value ) );
    CHECK_FALSE( acceptsEvent( appended, *tail.value ) );
    CHECK( validateEventSource( appended, *tail.value ).error.code == "stale_source" );
    const auto same = makeEventRef( appended, 0_lnum, 1_lnum, RawByteRange{ 0, 16 }, policy );
    REQUIRE( same.value );
    CHECK( same.value->eventKey() == completed.value->eventKey() );
    const auto changedTail
        = makeEventRef( appended, 0_lnum, 2_lnum, RawByteRange{ 0, 20 }, policy );
    REQUIRE( changedTail.value );
    CHECK( changedTail.value->eventKey() != tail.value->eventKey() );
}

TEST_CASE( "F01 closed or replaced events remain historical", "[data][F01][eventorigin]" )
{
    Source source;
    auto ref = makeEventRef( source.snapshot, 0_lnum, 1_lnum, {}, policy );
    REQUIRE( ref.value );
    auto replacement = source.snapshot;
    ++replacement.generation;
    CHECK( validateEventSource( replacement, *ref.value ).error.code == "stale_source" );
    replacement = source.snapshot;
    replacement.sourceId = QUuid::createUuid();
    CHECK_FALSE( acceptsEvent( replacement, *ref.value ) );
    source.life->closed = true;
    CHECK( validateEventSource( source.snapshot, *ref.value ).error.code == "source_closed" );
    CHECK( validateEventOrigin( *ref.value ).ok );
    roundTrip( *ref.value );
    CHECK_FALSE( makeEventRef( source.snapshot, 0_lnum, 1_lnum, {}, policy ).value );
}

TEST_CASE( "F01 event references never retain source lifetime", "[data][F01][eventorigin]" )
{
    Source source;
    auto ref = makeEventRef( source.snapshot, 0_lnum, 0_lnum, {}, policy );
    REQUIRE( ref.value );
    source.life.reset();
    CHECK( validateEventSource( source.snapshot, *ref.value ).error.code == "source_closed" );
}

TEST_CASE( "F01 decoded physical metadata cannot grant live navigation",
           "[data][F01][eventorigin]" )
{
    Source source;
    auto ref = makeEventRef( source.snapshot, 0_lnum, 2_lnum, RawByteRange{ 0, 20 }, policy );
    REQUIRE( ref.value );
    roundTrip( *ref.value );
    auto decoded = decodeEventRef( jsonRoundTrip( encodeEventRef( *ref.value ) ) );
    REQUIRE( decoded.value );
    CHECK( validateEventSource( source.snapshot, *decoded.value ).error.code == "unbound_source" );
    CHECK_FALSE( acceptsEvent( source.snapshot, *decoded.value ) );
}

TEST_CASE( "F01 physical endpoints and raw bytes reject invalid ranges",
           "[data][F01][eventorigin]" )
{
    Source source;
    CHECK_FALSE( makeEventRef( source.snapshot, 2_lnum, 1_lnum, {}, policy ).value );
    CHECK_FALSE( makeEventRef( source.snapshot, 0_lnum, 3_lnum, {}, policy ).value );
    CHECK_FALSE(
        makeEventRef( source.snapshot, 0_lnum, 1_lnum, RawByteRange{ 16, 15 }, policy ).value );
    CHECK_FALSE(
        makeEventRef( source.snapshot, 0_lnum, 2_lnum, RawByteRange{ 0, 21 }, policy ).value );
    CHECK_FALSE( makeEventRef( source.snapshot, 0_lnum, 2_lnum,
                               RawByteRange{ 0, std::numeric_limits<quint64>::max() }, policy )
                     .value );
    source.snapshot.completeLineCount = 4_lcount;
    CHECK_FALSE( makeEventRef( source.snapshot, 0_lnum, 1_lnum, {}, policy ).value );
}

TEST_CASE( "F01 inline origins have rows without physical anchors", "[data][F01][eventorigin]" )
{
    auto ref = makeInlineEventRef( { QStringLiteral( "text-job" ), 0, 2 }, policy );
    REQUIRE( ref.value );
    Source source;
    CHECK_FALSE( acceptsEvent( source.snapshot, *ref.value ) );
    CHECK( validateEventSource( source.snapshot, *ref.value ).error.code
           == "unsupported_source_kind" );
    roundTrip( *ref.value );
    auto origin = encodeEventRef( *ref.value ).value( "origin" ).toMap();
    CHECK_FALSE( origin.contains( "sourceId" ) );
    CHECK_FALSE( origin.contains( "bytes" ) );
    CHECK( origin.value( "firstLineNumber" ).toString() == "1" );
    CHECK_FALSE( makeInlineEventRef( { QStringLiteral( "text-job" ), 3, 2 }, policy ).value );
    CHECK_FALSE(
        makeInlineEventRef(
            { QStringLiteral( "text-job" ), 0, std::numeric_limits<quint64>::max() }, policy )
            .value );
    CHECK_FALSE( makeInlineEventRef( {}, policy ).value );
}

TEST_CASE( "F01 managed origins preserve exact segment bytes without rows",
           "[data][F01][eventorigin]" )
{
    auto origin = managed();
    origin.generation = std::numeric_limits<quint64>::max();
    origin.bytes = { 9007199254740993ULL, 18446744073709551615ULL };
    auto ref = makeManagedByteEventRef( origin, policy );
    REQUIRE( ref.value );
    roundTrip( *ref.value );
    auto wire = encodeEventRef( *ref.value ).value( "origin" ).toMap();
    CHECK( wire.value( "generation" ).toString() == "18446744073709551615" );
    CHECK( wire.value( "bytes" ).toMap().value( "begin" ).toString() == "9007199254740993" );
    CHECK_FALSE( wire.contains( "firstLineNumber" ) );
    CHECK_FALSE( wire.contains( "span" ) );
    Source source;
    CHECK_FALSE( acceptsEvent( source.snapshot, *ref.value ) );
    origin.bytes = { 10, 9 };
    CHECK_FALSE( makeManagedByteEventRef( origin, policy ).value );
    origin = managed();
    origin.segmentId.clear();
    CHECK_FALSE( makeManagedByteEventRef( origin, policy ).value );
}

TEST_CASE( "F01 deferred origins require matching immutable verification",
           "[data][F01][eventorigin]" )
{
    auto manifest = entry();
    auto origin = deferred();
    auto ref = makeDeferredEventRef( origin, manifest, policy );
    REQUIRE( ref.value );
    roundTrip( *ref.value );
    Source source;
    CHECK_FALSE( acceptsEvent( source.snapshot, *ref.value ) );
    CHECK( validateEventSource( source.snapshot, *ref.value ).error.code
           == "deferred_resolution_required" );
    ++manifest.contentEpoch;
    CHECK_FALSE( makeDeferredEventRef( origin, manifest, policy ).value );
    manifest = entry();
    manifest.verification.sha256Prefix.clear();
    CHECK_FALSE( makeDeferredEventRef( origin, manifest, policy ).value );
    manifest = entry();
    manifest.verification.frozenBytes = 21;
    CHECK_FALSE( makeDeferredEventRef( origin, manifest, policy ).value );
    manifest = entry();
    origin.lastLine = 3;
    CHECK_FALSE( makeDeferredEventRef( origin, manifest, policy ).value );
    origin = deferred();
    origin.bytes = RawByteRange{ 0, 21 };
    CHECK_FALSE( makeDeferredEventRef( origin, manifest, policy ).value );
}

TEST_CASE( "F01 identities distinguish origins ranges and interpretation policies",
           "[data][F01][eventorigin]" )
{
    auto origin = managed();
    const auto base = makeManagedByteEventRef( origin, policy );
    REQUIRE( base.value );
    for ( int change = 0; change < 7; ++change ) {
        auto candidate = origin;
        auto options = policy;
        if ( change == 0 )
            candidate.segmentId += "other";
        if ( change == 1 )
            ++candidate.generation;
        if ( change == 2 )
            ++candidate.bytes.begin;
        if ( change == 3 )
            ++options.formatRevision;
        if ( change == 4 )
            ++options.assemblyRevision;
        if ( change == 5 )
            ++options.occurrenceOrdinal;
        if ( change == 6 )
            options.formatId += "other";
        auto ref = makeManagedByteEventRef( candidate, options );
        REQUIRE( ref.value );
        CHECK( ref.value->eventKey() != base.value->eventKey() );
    }
    auto missingPolicy = policy;
    missingPolicy.formatId.clear();
    CHECK_FALSE( makeManagedByteEventRef( origin, missingPolicy ).value );
}

TEST_CASE( "F01 wire decoding rejects tampered identities and origin shapes",
           "[data][F01][eventorigin]" )
{
    auto ref = makeManagedByteEventRef( managed(), policy );
    REQUIRE( ref.value );
    const auto original = encodeEventRef( *ref.value );
    auto wire = original;
    wire.remove( "origin" );
    CHECK_FALSE( decodeEventRef( wire ).value );
    wire = original;
    wire.insert( "inline", QVariantMap{} );
    CHECK_FALSE( decodeEventRef( wire ).value );
    wire = original;
    wire.insert( "schemaVersion", 2 );
    CHECK_FALSE( decodeEventRef( wire ).value );
    wire = original;
    wire.insert( "schemaVersion", QStringLiteral( "1" ) );
    CHECK_FALSE( decodeEventRef( wire ).value );
    wire = original;
    wire.insert( "schemaVersion", true );
    CHECK_FALSE( decodeEventRef( wire ).value );
    wire = original;
    wire.insert( "eventKey", QStringLiteral( "evt1:bogus" ) );
    CHECK_FALSE( decodeEventRef( wire ).value );
    auto origin = original.value( "origin" ).toMap();
    for ( const auto value :
          { QVariant( 17 ), QVariant( QStringLiteral( "017" ) ), QVariant( QStringLiteral( "-1" ) ),
            QVariant( QStringLiteral( "18446744073709551616" ) ) } ) {
        auto bad = origin;
        bad.insert( "generation", value );
        wire = original;
        wire.insert( "origin", bad );
        CHECK_FALSE( decodeEventRef( wire ).value );
    }
    for ( const auto key : { "firstLineNumber", "span", "sourceId", "inline" } ) {
        auto bad = origin;
        bad.insert( key, QStringLiteral( "1" ) );
        wire = original;
        wire.insert( "origin", bad );
        CHECK_FALSE( decodeEventRef( wire ).value );
    }
    auto bad = origin;
    bad.insert( "kind", QStringLiteral( "unknown" ) );
    wire = original;
    wire.insert( "origin", bad );
    CHECK( decodeEventRef( wire ).error.code == "unsupported_source_kind" );
}

TEST_CASE( "F01 wire rejects missing deferred binding and mixed generation endpoints",
           "[data][F01][eventorigin]" )
{
    auto ref = makeDeferredEventRef( deferred(), entry(), policy );
    REQUIRE( ref.value );
    auto wire = encodeEventRef( *ref.value );
    auto origin = wire.value( "origin" ).toMap();
    origin.remove( "verification" );
    wire.insert( "origin", origin );
    CHECK_FALSE( decodeEventRef( wire ).value );
    Source source;
    auto physical = makeEventRef( source.snapshot, 0_lnum, 2_lnum, {}, policy );
    REQUIRE( physical.value );
    wire = encodeEventRef( *physical.value );
    origin = wire.value( "origin" ).toMap();
    origin.insert( "lastGeneration", QStringLiteral( "8" ) );
    wire.insert( "origin", origin );
    CHECK_FALSE( decodeEventRef( wire ).value );
}

TEST_CASE( "F01 wire metadata is bounded canonical and Unicode safe", "[data][F01][eventorigin]" )
{
    auto in
        = makeInlineEventRef( { QString::fromUtf8( "sample-\xf0\x9f\x98\x80" ), 0, 0 }, policy );
    REQUIRE( in.value );
    roundTrip( *in.value );
    auto ref = makeManagedByteEventRef( managed(), policy );
    REQUIRE( ref.value );
    auto original = encodeEventRef( *ref.value );
    for ( const QVariant value :
          { QVariant( 1.0000000000000002 ), QVariant( std::numeric_limits<double>::infinity() ),
            QVariant( std::numeric_limits<double>::quiet_NaN() ) } ) {
        auto wire = original;
        wire.insert( "schemaVersion", value );
        CHECK_FALSE( decodeEventRef( wire ).value );
    }
    auto bad = managed();
    bad.recordingId = QString( 257, QLatin1Char( 'x' ) );
    CHECK_FALSE( makeManagedByteEventRef( bad, policy ).value );
    bad = managed();
    bad.recordingId = QString( QChar( 0xd800 ) );
    CHECK_FALSE( makeManagedByteEventRef( bad, policy ).value );
    auto manifest = entry();
    manifest.verification.provisionalLineStart = 20;
    CHECK_FALSE( makeDeferredEventRef( deferred(), manifest, policy ).value );
    manifest = entry();
    manifest.verification.completeLineCount = std::numeric_limits<quint64>::max();
    CHECK_FALSE( makeDeferredEventRef( deferred(), manifest, policy ).value );
    Source source;
    auto physical = makeEventRef( source.snapshot, 0_lnum, 0_lnum, {}, policy );
    REQUIRE( physical.value );
    auto otherLife = std::make_shared<SourceLifetime>();
    auto forged = source.snapshot;
    forged.lifetime = otherLife;
    CHECK_FALSE( acceptsEvent( forged, *physical.value ) );
}
