// SPDX-License-Identifier: GPL-3.0-or-later
#include "tsharkpreviewclient.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

namespace {
const QString Origin = QStringLiteral( "engine_data_source_unresolved" );
bool boundedString( const QJsonValue& value, int maximum = 65536 )
{
    return value.isString() && value.toString().size() <= maximum;
}
bool integer( const QJsonValue& value, double minimum, double maximum )
{
    const auto number = value.toDouble( -1 );
    return value.isDouble() && std::isfinite( number ) && std::floor( number ) == number
           && number >= minimum && number <= maximum;
}
// What a limited result says about its limit (#183): `rows` (the row limit) or `bytes` (the body
// limit, with the bytes the whole result needed), its maximum, and the rows shown and in the tree.
bool validLimit( const QJsonObject& value )
{
    const auto limit = value.value( "limit" ).toObject();
    const bool bytes = limit.value( "name" ) == "bytes";
    const QStringList keys{ "name", "maximum", "shownRows", "totalRows" };
    if ( !value.value( "limit" ).isObject() || value.value( "state" ) != "completed"
         || !value.value( "limited" ).toBool()
         || ( !bytes && limit.value( "name" ) != "rows" )
         || limit.size() != keys.size() + ( bytes ? 1 : 0 ) )
        return false;
    for ( const auto& key : keys.mid( 1 ) )
        if ( !limit.contains( key ) )
            return false;
    const auto shown = limit.value( "shownRows" ).toDouble();
    return integer( limit.value( "maximum" ), 1, TsharkPreviewClient::ResultLimitMax )
           && integer( limit.value( "shownRows" ), 0, TsharkPreviewClient::FieldLimit )
           && shown == static_cast<double>( value.value( "fields" ).toArray().size() )
           && integer( limit.value( "totalRows" ), shown, 9007199254740991.0 )
           && ( !bytes
                || integer( limit.value( "neededBytes" ), limit.value( "maximum" ).toDouble(),
                            9007199254740991.0 ) );
}
bool validResponse( const QJsonObject& value, const QString& id )
{
    if ( !integer( value.value( "version" ), 1, 1 ) || value.value( "id" ) != id
         || value.value( "backend" ) != "tshark_lua"
         || ( value.value( "state" ) != "completed" && value.value( "state" ) != "failed" )
         || !boundedString( value.value( "reason" ), 128 )
         || !boundedString( value.value( "previewName" ), 128 )
         || value.value( "offsetSpace" ) != Origin
         || value.value( "messageCompleteness" ) != "not_reported"
         || value.value( "messageIntegrity" ) != "not_reported" )
        return false;
    for ( const auto* key : { "available", "complete", "limited" } )
        if ( !value.value( key ).isBool() )
            return false;
    const bool completed = value.value( "state" ) == "completed";
    if ( value.value( "available" ).toBool() != completed
         || ( !completed && value.value( "complete" ).toBool() )
         || ( value.value( "limited" ).toBool() && value.value( "complete" ).toBool() )
         || ( completed && value.value( "complete" ).toBool() == value.value( "limited" ).toBool() )
         || ( !completed && value.value( "reason" ).toString().isEmpty() )
         || ( completed
              && ( !value.contains( "enginePid" ) || !value.contains( "scriptSha256" ) ) ) )
        return false;
    const QStringList allowed{ "version",
                               "id",
                               "backend",
                               "state",
                               "reason",
                               "available",
                               "complete",
                               "limited",
                               "previewName",
                               "offsetSpace",
                               "messageCompleteness",
                               "messageIntegrity",
                               "fields",
                               "enginePid",
                               "scriptSha256",
                               "limit" };
    for ( auto item = value.begin(); item != value.end(); ++item )
        if ( !allowed.contains( item.key() ) )
            return false;
    // A completed result cut off names its limit; a failure never does (#183).
    if ( value.contains( "limit" ) ? !validLimit( value )
                                   : completed && value.value( "limited" ).toBool() )
        return false;
    if ( value.contains( "enginePid" )
         && !integer( value.value( "enginePid" ), 1, 9007199254740991.0 ) )
        return false; // Metadata only; never used to find/terminate a process.
    if ( value.contains( "scriptSha256" ) ) {
        // The packs' files and the user's scripts (PreviewDecoderSettings::ProfileLuaLimit).
        if ( !value.value( "scriptSha256" ).isArray()
             || value.value( "scriptSha256" ).toArray().size() > 32 )
            return false;
        static const QRegularExpression hash( "\\A[0-9a-f]{64}\\z" );
        for ( const auto& digest : value.value( "scriptSha256" ).toArray() )
            if ( !digest.isString() || !hash.match( digest.toString() ).hasMatch() )
                return false;
    }
    if ( !value.value( "fields" ).isArray() )
        return false;
    const auto fields = value.value( "fields" ).toArray();
    if ( fields.size() > TsharkPreviewClient::FieldLimit || ( !completed && !fields.isEmpty() ) )
        return false;
    int previousDepth = -1;
    for ( const auto& field : fields ) {
        if ( !field.isObject() )
            return false;
        const auto entry = field.toObject();
        if ( entry.size() != 5 || !integer( entry.value( "depth" ), 0, 32 )
             || !boundedString( entry.value( "id" ), 512 )
             || entry.value( "positionOrigin" ) != Origin || !entry.value( "attributes" ).isObject()
             || !entry.value( "columns" ).isArray()
             || entry.value( "columns" ).toArray().size() != 5 )
            return false;
        const int depth = entry.value( "depth" ).toInt();
        if ( depth > previousDepth + 1 )
            return false;
        previousDepth = depth;
        for ( const auto& column : entry.value( "columns" ).toArray() )
            if ( !boundedString( column ) )
                return false;
        const auto attributes = entry.value( "attributes" ).toObject();
        if ( attributes.size() > 64 )
            return false;
        for ( auto attr = attributes.begin(); attr != attributes.end(); ++attr )
            if ( attr.key().size() > 128 || !boundedString( attr.value() ) )
                return false;
        const auto columns = entry.value( "columns" ).toArray();
        const QList<QJsonValue> expected{
            attributes.value( "name" ).isUndefined() ? QJsonValue( "" )
                                                     : attributes.value( "name" ),
            attributes.contains( "show" )                  ? attributes.value( "show" )
            : attributes.value( "showname" ).isUndefined() ? QJsonValue( "" )
                                                           : attributes.value( "showname" ),
            attributes.value( "value" ).isUndefined() ? QJsonValue( "" )
                                                      : attributes.value( "value" ),
            attributes.value( "pos" ).isUndefined() ? QJsonValue( "" ) : attributes.value( "pos" ),
            attributes.value( "size" ).isUndefined() ? QJsonValue( "" ) : attributes.value( "size" )
        };
        for ( int i = 0; i < 5; ++i )
            if ( columns.at( i ) != expected.at( i ) )
                return false;
    }
    return true;
}
} // namespace

TsharkPreviewClient& TsharkPreviewClient::instance()
{
    static auto* client = new TsharkPreviewClient( QCoreApplication::instance() );
    return *client;
}
TsharkPreviewClient::TsharkPreviewClient( QObject* parent )
    : PreviewWorkerClient( parent,
                           { QStringLiteral( "tshark_lua" ),
                             QStringLiteral( "evaluate" ),
                             Origin,
                             QStringLiteral( "message_decoder_preview_worker.py" ),
                             { QStringLiteral( "message_decoder_preview_worker.py" ),
                               QStringLiteral( "message_decoder_wire.py" ),
                               QStringLiteral( "workbench_process.py" ) },
                             validResponse,
                             {},
                             ResultLimitMax } )
{
}
void TsharkPreviewClient::evaluate( QObject* owner, const QVariantMap& profile,
                                    const QByteArray& payload, int timeoutMs, const QString& id,
                                    Completion done, int resultLimit )
{
    submit( owner,
            { { "profile", profile },
              { "responseLimit", qBound( ResultLimitMin, resultLimit, ResultLimitMax ) } },
            payload, timeoutMs, id, std::move( done ) );
}
