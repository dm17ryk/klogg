// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "eventrecord.h"
#include "log.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <cstring>
#include <limits>
#include <utility>

namespace {
constexpr auto MaxIdUnits = 256;
constexpr auto MaxPathUnits = 4096;
constexpr auto MaxFileIdBytes = 1024;
constexpr auto MaxU64 = std::numeric_limits<quint64>::max();

DataStatus status( const char* code = nullptr )
{
    if ( !code )
        return { true, {} };
    const auto reason = QString::fromLatin1( code );
    LOG_DEBUG << "F01 event rejected reason=" << reason;
    return { false, { reason, reason, {}, {} } };
}
template <class T>
DataResult<T> failure( const char* code )
{
    return { {}, status( code ).error };
}
bool text( const QString& value, int maximum = MaxIdUnits )
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
bool rows( quint64 first, quint64 last )
{
    // Public one-based line numbers must not overflow.
    return first <= last && last < MaxU64;
}
bool range( const RawByteRange& bytes, quint64 watermark = MaxU64 )
{
    return bytes.begin <= bytes.endExclusive && bytes.endExclusive <= watermark;
}
bool snapshotShape( const SourceSnapshot& source )
{
    const auto total = source.lineCount.get(), complete = source.completeLineCount.get();
    if ( source.sourceId.isNull() || source.indexedBytes < 0 || complete > total
         || total - complete > 1
         || source.provisionalLineStart.has_value() != ( total > complete ) )
        return false;
    return !source.provisionalLineStart
           || ( *source.provisionalLineStart >= 0
                && *source.provisionalLineStart < source.indexedBytes );
}
bool verificationShape( const DeferredSourceVerification& value )
{
    return text( value.canonicalOrigin, MaxPathUnits )
           && value.platformFileId.size() <= MaxFileIdBytes
           && value.frozenBytes <= value.capturedSize && value.sha256Prefix.size() == 32
           && text( value.encoding, 128 )
           && ( !value.provisionalLineStart
                || ( *value.provisionalLineStart < value.frozenBytes
                     && value.completeLineCount < MaxU64 ) );
}
DataStatus originStatus( const EventOrigin& origin,
                         const std::optional<SourceManifestEntry>& entry )
{
    if ( const auto* physical = std::get_if<PhysicalEventOrigin>( &origin ) ) {
        if ( entry || !snapshotShape( physical->snapshot )
             || !rows( physical->first.get(), physical->last.get() )
             || physical->last >= physical->snapshot.lineCount )
            return status( "invalid_physical_origin" );
        if ( physical->bytes
             && !range( *physical->bytes,
                        static_cast<quint64>( physical->snapshot.indexedBytes ) ) )
            return status( "invalid_byte_range" );
        return status();
    }
    if ( const auto* in = std::get_if<InlineEventOrigin>( &origin ) ) {
        return !entry && text( in->inlineSourceId ) && rows( in->firstLine, in->lastLine )
                   ? status()
                   : status( "invalid_inline_origin" );
    }
    if ( const auto* deferred = std::get_if<DeferredFileOrigin>( &origin ) ) {
        if ( !entry || !text( deferred->manifestId ) || !text( deferred->entryId )
             || deferred->manifestId != entry->manifestId || deferred->entryId != entry->entryId
             || deferred->manifestRevision != entry->manifestRevision
             || deferred->contentEpoch != entry->contentEpoch )
            return status( "manifest_binding_mismatch" );
        const auto& verification = entry->verification;
        if ( !verificationShape( verification ) )
            return status( "invalid_source_verification" );
        const auto count
            = verification.completeLineCount + ( verification.provisionalLineStart ? 1 : 0 );
        if ( !rows( deferred->firstLine, deferred->lastLine ) || deferred->lastLine >= count )
            return status( "invalid_line_range" );
        if ( deferred->bytes && !range( *deferred->bytes, verification.frozenBytes ) )
            return status( "invalid_byte_range" );
        return status();
    }
    const auto& managed = std::get<ManagedByteOrigin>( origin );
    if ( entry || !text( managed.resolverKind ) || !text( managed.recordingId )
         || !text( managed.segmentId ) )
        return status( "invalid_managed_origin" );
    return range( managed.bytes ) ? status() : status( "invalid_byte_range" );
}
QVariantMap byteMap( const RawByteRange& bytes )
{
    return { { "begin", QString::number( bytes.begin ) },
             { "endExclusive", QString::number( bytes.endExclusive ) } };
}
QVariantMap policyMap( const EventIdentityPolicy& policy )
{
    return { { "formatId", policy.formatId },
             { "formatRevision", QString::number( policy.formatRevision ) },
             { "assemblyRevision", QString::number( policy.assemblyRevision ) },
             { "occurrenceOrdinal", QString::number( policy.occurrenceOrdinal ) } };
}
QVariantMap verificationMap( const DeferredSourceVerification& value )
{
    QVariantMap map{ { "canonicalOrigin", value.canonicalOrigin },
                     { "platformFileId", QString::fromLatin1( value.platformFileId.toHex() ) },
                     { "capturedSize", QString::number( value.capturedSize ) },
                     { "frozenBytes", QString::number( value.frozenBytes ) },
                     { "completeLineCount", QString::number( value.completeLineCount ) },
                     { "sha256Prefix", QString::fromLatin1( value.sha256Prefix.toHex() ) },
                     { "encoding", value.encoding },
                     { "decodingRevision", QString::number( value.decodingRevision ) } };
    if ( value.provisionalLineStart )
        map.insert( "provisionalLineStart", QString::number( *value.provisionalLineStart ) );
    return map;
}
QVariantMap originMap( const EventOrigin& origin, const std::optional<SourceManifestEntry>& entry,
                       bool identity )
{
    QVariantMap map;
    std::optional<RawByteRange> bytes;
    if ( const auto* physical = std::get_if<PhysicalEventOrigin>( &origin ) ) {
        const auto& source = physical->snapshot;
        map = { { "kind", QStringLiteral( "physical" ) },
                { "sourceId", source.sourceId.toString( QUuid::WithoutBraces ) },
                { "generation", QString::number( source.generation ) },
                { "firstLineNumber", QString::number( physical->first.get() + 1 ) },
                { "lastLineNumber", QString::number( physical->last.get() + 1 ) } };
        // Completed locations survive append. Provisional events require the exact revision.
        if ( !identity || physical->last >= source.completeLineCount )
            map.insert( "revision", QString::number( source.revision ) );
        if ( !identity ) {
            map.insert( "indexedBytes", QString::number( source.indexedBytes ) );
            map.insert( "lineCount", QString::number( source.lineCount.get() ) );
            map.insert( "completeLineCount", QString::number( source.completeLineCount.get() ) );
            if ( source.provisionalLineStart )
                map.insert( "provisionalLineStart",
                            QString::number( *source.provisionalLineStart ) );
        }
        bytes = physical->bytes;
    }
    else if ( const auto* in = std::get_if<InlineEventOrigin>( &origin ) ) {
        map = { { "kind", QStringLiteral( "inline" ) },
                { "inlineSourceId", in->inlineSourceId },
                { "firstLineNumber", QString::number( in->firstLine + 1 ) },
                { "lastLineNumber", QString::number( in->lastLine + 1 ) } };
    }
    else if ( const auto* deferred = std::get_if<DeferredFileOrigin>( &origin ) ) {
        map = { { "kind", QStringLiteral( "deferred_file" ) },
                { "manifestId", deferred->manifestId },
                { "entryId", deferred->entryId },
                { "manifestRevision", QString::number( deferred->manifestRevision ) },
                { "contentEpoch", QString::number( deferred->contentEpoch ) },
                { "firstLineNumber", QString::number( deferred->firstLine + 1 ) },
                { "lastLineNumber", QString::number( deferred->lastLine + 1 ) } };
        // This binds immutable descriptors; no filesystem verification is performed here.
        map.insert( "verification", verificationMap( entry->verification ) );
        bytes = deferred->bytes;
    }
    else {
        const auto& managed = std::get<ManagedByteOrigin>( origin );
        map = { { "kind", QStringLiteral( "managed_bytes" ) },
                { "resolverKind", managed.resolverKind },
                { "recordingId", managed.recordingId },
                { "segmentId", managed.segmentId },
                { "generation", QString::number( managed.generation ) } };
        bytes = managed.bytes;
    }
    if ( bytes )
        map.insert( "bytes", byteMap( *bytes ) );
    return map;
}
QString identityKey( const EventOrigin& origin, const EventIdentityPolicy& policy,
                     const std::optional<SourceManifestEntry>& entry )
{
    // QVariantMap is ordered; exact numbers are strings and schema/domain are explicit.
    const QVariantMap key{ { "domain", QStringLiteral( "cilogg.event.v1" ) },
                           { "policy", policyMap( policy ) },
                           { "origin", originMap( origin, entry, true ) } };
    const auto json
        = QJsonDocument( QJsonObject::fromVariantMap( key ) ).toJson( QJsonDocument::Compact );
    return QStringLiteral( "evt1:" )
           + QString::fromLatin1(
               QCryptographicHash::hash( json, QCryptographicHash::Sha256 ).toHex() );
}
bool keys( const QVariantMap& map, std::initializer_list<const char*> required,
           std::initializer_list<const char*> optional = {} )
{
    if ( map.size() < static_cast<int>( required.size() )
         || map.size() > static_cast<int>( required.size() + optional.size() ) )
        return false;
    for ( const auto* key : required )
        if ( !map.contains( QString::fromLatin1( key ) ) )
            return false;
    for ( auto it = map.cbegin(); it != map.cend(); ++it ) {
        bool known = false;
        for ( const auto* key : required )
            known = known || it.key() == QLatin1String( key );
        for ( const auto* key : optional )
            known = known || it.key() == QLatin1String( key );
        if ( !known )
            return false;
    }
    return true;
}
bool mapValue( const QVariantMap& parent, const char* key, QVariantMap& result )
{
    const auto value = parent.value( QString::fromLatin1( key ) );
    if ( value.userType() != QMetaType::QVariantMap )
        return false;
    result = value.toMap();
    return true;
}
bool stringValue( const QVariantMap& map, const char* key, QString& result,
                  int maximum = MaxIdUnits )
{
    const auto value = map.value( QString::fromLatin1( key ) );
    if ( value.userType() != QMetaType::QString )
        return false;
    result = value.toString();
    return text( result, maximum );
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
bool optionalU64( const QVariantMap& map, const char* key, std::optional<quint64>& result )
{
    if ( !map.contains( QString::fromLatin1( key ) ) )
        return true;
    quint64 value = 0;
    if ( !u64( map, key, value ) )
        return false;
    result = value;
    return true;
}
bool lineRange( const QVariantMap& map, quint64& first, quint64& last )
{
    if ( !u64( map, "firstLineNumber", first ) || !u64( map, "lastLineNumber", last ) || first == 0
         || last == 0 )
        return false;
    --first;
    --last;
    return rows( first, last );
}
bool bytesValue( const QVariantMap& map, std::optional<RawByteRange>& bytes )
{
    if ( !map.contains( "bytes" ) )
        return true;
    QVariantMap value;
    RawByteRange result;
    if ( !mapValue( map, "bytes", value ) || !keys( value, { "begin", "endExclusive" } )
         || !u64( value, "begin", result.begin )
         || !u64( value, "endExclusive", result.endExclusive ) || !range( result ) )
        return false;
    bytes = result;
    return true;
}
bool hexValue( const QVariantMap& map, const char* key, QByteArray& result, int maximum,
               bool allowEmpty )
{
    const auto value = map.value( QString::fromLatin1( key ) );
    if ( value.userType() != QMetaType::QString )
        return false;
    const auto token = value.toString();
    if ( token.size() > maximum * 2 || token.size() % 2 || ( !allowEmpty && token.isEmpty() ) )
        return false;
    const auto ascii = token.toLatin1();
    result = QByteArray::fromHex( ascii );
    return QString::fromLatin1( result.toHex() ) == token;
}
bool verificationValue( const QVariantMap& map, DeferredSourceVerification& value )
{
    return keys( map,
                 { "canonicalOrigin", "platformFileId", "capturedSize", "frozenBytes",
                   "completeLineCount", "sha256Prefix", "encoding", "decodingRevision" },
                 { "provisionalLineStart" } )
           && stringValue( map, "canonicalOrigin", value.canonicalOrigin, MaxPathUnits )
           && hexValue( map, "platformFileId", value.platformFileId, MaxFileIdBytes, true )
           && u64( map, "capturedSize", value.capturedSize )
           && u64( map, "frozenBytes", value.frozenBytes )
           && u64( map, "completeLineCount", value.completeLineCount )
           && hexValue( map, "sha256Prefix", value.sha256Prefix, 32, false )
           && stringValue( map, "encoding", value.encoding, 128 )
           && u64( map, "decodingRevision", value.decodingRevision )
           && optionalU64( map, "provisionalLineStart", value.provisionalLineStart )
           && verificationShape( value );
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
        static_assert( sizeof( number ) == sizeof( bits ), "64-bit JSON double required" );
        std::memcpy( &bits, &number, sizeof( bits ) );
        return bits == 0x3ff0000000000000ULL;
    }
    default:
        return false;
    }
}
} // namespace

EventRef::EventRef( QString key, EventOrigin origin, EventIdentityPolicy policy,
                    std::optional<SourceManifestEntry> entry )
    : eventKey_( std::move( key ) )
    , origin_( std::move( origin ) )
    , policy_( std::move( policy ) )
    , manifestEntry_( std::move( entry ) )
{
}

DataResult<EventRef> EventRef::checked( EventOrigin origin, const EventIdentityPolicy& policy,
                                        std::optional<SourceManifestEntry> entry )
{
    if ( !text( policy.formatId ) )
        return failure<EventRef>( "invalid_event_policy" );
    const auto valid = originStatus( origin, entry );
    if ( !valid.ok )
        return { {}, valid.error };
    const auto key = identityKey( origin, policy, entry );
    LOG_DEBUG << "F01 event accepted id=" << key << " kind=" << origin.index()
              << " formatRevision=" << policy.formatRevision
              << " assemblyRevision=" << policy.assemblyRevision
              << " occurrence=" << policy.occurrenceOrdinal;
    return { EventRef( key, std::move( origin ), policy, std::move( entry ) ), {} };
}
DataResult<EventRef> makeEventRef( const SourceSnapshot& snapshot, LineNumber first,
                                   LineNumber last, std::optional<RawByteRange> bytes,
                                   const EventIdentityPolicy& policy )
{
    LOG_DEBUG << "F01 physical event source=" << snapshot.sourceId
              << " generation=" << snapshot.generation << " revision=" << snapshot.revision
              << " watermark=" << snapshot.indexedBytes << " first=" << first.get()
              << " last=" << last.get();
    if ( !snapshot.available || !snapshot.isOpen() )
        return failure<EventRef>( "source_closed" );
    return EventRef::checked( PhysicalEventOrigin{ snapshot, first, last, bytes }, policy );
}
DataResult<EventRef> makeInlineEventRef( const InlineEventOrigin& origin,
                                         const EventIdentityPolicy& policy )
{
    return EventRef::checked( origin, policy );
}
DataResult<EventRef> makeDeferredEventRef( const DeferredFileOrigin& origin,
                                           const SourceManifestEntry& entry,
                                           const EventIdentityPolicy& policy )
{
    return EventRef::checked( origin, policy, entry );
}
DataResult<EventRef> makeManagedByteEventRef( const ManagedByteOrigin& origin,
                                              const EventIdentityPolicy& policy )
{
    return EventRef::checked( origin, policy );
}
DataStatus validateEventOrigin( const EventRef& ref )
{
    return originStatus( ref.origin(), ref.manifestEntry() );
}
DataStatus validateEventSource( const SourceSnapshot& current, const EventRef& ref )
{
    const auto* physical = std::get_if<PhysicalEventOrigin>( &ref.origin() );
    if ( !physical )
        return status( std::holds_alternative<DeferredFileOrigin>( ref.origin() )
                           ? "deferred_resolution_required"
                           : "unsupported_source_kind" );
    const auto& original = physical->snapshot;
    if ( !original.available )
        return status( "unbound_source" );
    if ( !original.isOpen() || !current.available || !current.isOpen() )
        return status( "source_closed" );
    const auto first = original.location( physical->first ),
               last = original.location( physical->last );
    const bool sameLifetime = !original.lifetime.owner_before( current.lifetime )
                              && !current.lifetime.owner_before( original.lifetime );
    if ( !snapshotShape( current ) || !sameLifetime || !first || !last || !current.accepts( *first )
         || !current.accepts( *last ) )
        return status( "stale_source" );
    LOG_DEBUG << "F01 event source accepted id=" << ref.eventKey() << " source=" << current.sourceId
              << " generation=" << current.generation << " revision=" << current.revision;
    return status();
}
bool acceptsEvent( const SourceSnapshot& current, const EventRef& ref )
{
    return validateEventSource( current, ref ).ok;
}
QVariantMap encodeEventRef( const EventRef& ref )
{
    return { { "schemaVersion", 1 },
             { "eventKey", ref.eventKey() },
             { "policy", policyMap( ref.policy() ) },
             { "origin", originMap( ref.origin(), ref.manifestEntry(), false ) } };
}
DataResult<EventRef> decodeEventRef( const QVariantMap& map )
{
    if ( !keys( map, { "schemaVersion", "eventKey", "policy", "origin" } ) )
        return failure<EventRef>( "invalid_event_shape" );
    if ( !versionOne( map.value( "schemaVersion" ) ) )
        return failure<EventRef>( "unsupported_event_version" );
    QVariantMap options, origin;
    EventIdentityPolicy policy;
    QString key, kind;
    if ( !stringValue( map, "eventKey", key, 69 ) || key.size() != 69 || !key.startsWith( "evt1:" )
         || !mapValue( map, "policy", options )
         || !keys( options,
                   { "formatId", "formatRevision", "assemblyRevision", "occurrenceOrdinal" } )
         || !stringValue( options, "formatId", policy.formatId )
         || !u64( options, "formatRevision", policy.formatRevision )
         || !u64( options, "assemblyRevision", policy.assemblyRevision )
         || !u64( options, "occurrenceOrdinal", policy.occurrenceOrdinal )
         || !mapValue( map, "origin", origin ) || !stringValue( origin, "kind", kind ) )
        return failure<EventRef>( "invalid_event_shape" );
    EventOrigin decoded;
    std::optional<SourceManifestEntry> entry;
    std::optional<RawByteRange> bytes;
    if ( kind == QLatin1String( "physical" ) ) {
        SourceSnapshot source;
        QString id;
        quint64 first = 0, last = 0, watermark = 0, total = 0, complete = 0;
        std::optional<quint64> provisional;
        if ( !keys( origin,
                    { "kind", "sourceId", "generation", "revision", "indexedBytes", "lineCount",
                      "completeLineCount", "firstLineNumber", "lastLineNumber" },
                    { "provisionalLineStart", "bytes" } )
             || !stringValue( origin, "sourceId", id, 36 ) || id.size() != 36
             || ( source.sourceId = QUuid( id ) ).isNull()
             || source.sourceId.toString( QUuid::WithoutBraces ) != id
             || !u64( origin, "generation", source.generation )
             || !u64( origin, "revision", source.revision )
             || !u64( origin, "indexedBytes", watermark )
             || watermark > static_cast<quint64>( std::numeric_limits<qint64>::max() )
             || !u64( origin, "lineCount", total ) || !u64( origin, "completeLineCount", complete )
             || !optionalU64( origin, "provisionalLineStart", provisional )
             || ( provisional && *provisional > watermark ) || !lineRange( origin, first, last )
             || !bytesValue( origin, bytes ) )
            return failure<EventRef>( "invalid_physical_origin" );
        source.indexedBytes = static_cast<qint64>( watermark );
        source.lineCount = LinesCount( total );
        source.completeLineCount = LinesCount( complete );
        if ( provisional )
            source.provisionalLineStart = static_cast<qint64>( *provisional );
        // No lifetime token or availability is restored from wire data.
        decoded = PhysicalEventOrigin{ source, LineNumber( first ), LineNumber( last ), bytes };
    }
    else if ( kind == QLatin1String( "inline" ) ) {
        InlineEventOrigin value;
        if ( !keys( origin, { "kind", "inlineSourceId", "firstLineNumber", "lastLineNumber" } )
             || !stringValue( origin, "inlineSourceId", value.inlineSourceId )
             || !lineRange( origin, value.firstLine, value.lastLine ) )
            return failure<EventRef>( "invalid_inline_origin" );
        decoded = value;
    }
    else if ( kind == QLatin1String( "deferred_file" ) ) {
        DeferredFileOrigin value;
        SourceManifestEntry manifest;
        QVariantMap verification;
        if ( !keys( origin,
                    { "kind", "manifestId", "entryId", "manifestRevision", "contentEpoch",
                      "firstLineNumber", "lastLineNumber", "verification" },
                    { "bytes" } )
             || !stringValue( origin, "manifestId", value.manifestId )
             || !stringValue( origin, "entryId", value.entryId )
             || !u64( origin, "manifestRevision", value.manifestRevision )
             || !u64( origin, "contentEpoch", value.contentEpoch )
             || !lineRange( origin, value.firstLine, value.lastLine )
             || !bytesValue( origin, value.bytes )
             || !mapValue( origin, "verification", verification )
             || !verificationValue( verification, manifest.verification ) )
            return failure<EventRef>( "invalid_deferred_origin" );
        manifest.manifestId = value.manifestId;
        manifest.entryId = value.entryId;
        manifest.manifestRevision = value.manifestRevision;
        manifest.contentEpoch = value.contentEpoch;
        entry = manifest;
        decoded = value;
    }
    else if ( kind == QLatin1String( "managed_bytes" ) ) {
        ManagedByteOrigin value;
        if ( !keys( origin,
                    { "kind", "resolverKind", "recordingId", "segmentId", "generation", "bytes" } )
             || !stringValue( origin, "resolverKind", value.resolverKind )
             || !stringValue( origin, "recordingId", value.recordingId )
             || !stringValue( origin, "segmentId", value.segmentId )
             || !u64( origin, "generation", value.generation ) || !bytesValue( origin, bytes )
             || !bytes )
            return failure<EventRef>( "invalid_managed_origin" );
        value.bytes = *bytes;
        decoded = value;
    }
    else
        return failure<EventRef>( "unsupported_source_kind" );
    auto result = EventRef::checked( std::move( decoded ), policy, std::move( entry ) );
    if ( result.value && result.value->eventKey() != key )
        return failure<EventRef>( "event_identity_mismatch" );
    return result;
}
