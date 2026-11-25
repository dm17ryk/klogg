// SPDX-License-Identifier: GPL-3.0-or-later
#include "pythondecoderclient.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

namespace {
const QString Origin = QStringLiteral( "decoder_payload" );
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
bool oneOf( const QJsonValue& value, const QStringList& allowed )
{
    return value.isString() && allowed.contains( value.toString() );
}
bool validField( const QJsonValue& field, int* previousDepth )
{
    if ( !field.isObject() )
        return false;
    const auto entry = field.toObject();
    if ( entry.size() != 5 || !integer( entry.value( "depth" ), 0, 32 )
         || !boundedString( entry.value( "id" ), 512 )
         || entry.value( "positionOrigin" ) != Origin || !entry.value( "attributes" ).isObject()
         || !entry.value( "columns" ).isArray() || entry.value( "columns" ).toArray().size() != 5 )
        return false;
    const int depth = entry.value( "depth" ).toInt();
    if ( depth > *previousDepth + 1 )
        return false;
    *previousDepth = depth;
    for ( const auto& column : entry.value( "columns" ).toArray() )
        if ( !boundedString( column ) )
            return false;
    const auto attributes = entry.value( "attributes" ).toObject();
    if ( attributes.size() > 64 )
        return false;
    for ( auto attribute = attributes.begin(); attribute != attributes.end(); ++attribute )
        if ( attribute.key().size() > 128 || !boundedString( attribute.value() ) )
            return false;
    return true;
}
// The worker's response (scripts/plugins/message_decoder_python_worker.py), strictly.
bool validResponse( const QJsonObject& value, const QString& id )
{
    if ( !integer( value.value( "version" ), 1, 1 ) || value.value( "id" ) != id
         || value.value( "backend" ) != "python_decoder"
         || !oneOf( value.value( "state" ), { "completed", "failed" } )
         || !boundedString( value.value( "reason" ), 128 )
         || !boundedString( value.value( "previewName" ), 128 )
         || !boundedString( value.value( "decoder" ), 256 )
         || value.value( "offsetSpace" ) != Origin
         || !oneOf( value.value( "messageCompleteness" ),
                    { "complete", "incomplete", "not_reported" } )
         || !oneOf( value.value( "messageIntegrity" ), { "valid", "invalid", "not_reported" } ) )
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
              && ( !value.contains( "enginePid" ) || !value.contains( "moduleSha256" ) ) ) )
        return false;
    static const QStringList allowed{ "version",
                                      "id",
                                      "backend",
                                      "state",
                                      "reason",
                                      "available",
                                      "complete",
                                      "limited",
                                      "previewName",
                                      "decoder",
                                      "offsetSpace",
                                      "messageCompleteness",
                                      "messageIntegrity",
                                      "fields",
                                      "enginePid",
                                      "moduleSha256",
                                      "detail" };
    for ( auto item = value.begin(); item != value.end(); ++item )
        if ( !allowed.contains( item.key() ) )
            return false;
    if ( value.contains( "enginePid" )
         && !integer( value.value( "enginePid" ), 1, 9007199254740991.0 ) )
        return false; // Metadata only; never used to find/terminate a process.
    static const QRegularExpression hash( "\\A[0-9a-f]{64}\\z" );
    if ( value.contains( "moduleSha256" )
         && ( !value.value( "moduleSha256" ).isString()
              || !hash.match( value.value( "moduleSha256" ).toString() ).hasMatch() ) )
        return false;
    if ( value.contains( "detail" ) && !boundedString( value.value( "detail" ), 256 ) )
        return false;
    if ( !value.value( "fields" ).isArray() )
        return false;
    const auto fields = value.value( "fields" ).toArray();
    if ( fields.size() > 512 || ( !completed && !fields.isEmpty() ) )
        return false;
    int previousDepth = -1;
    for ( const auto& field : fields )
        if ( !validField( field, &previousDepth ) )
            return false;
    return true;
}
} // namespace

PythonDecoderClient& PythonDecoderClient::instance()
{
    static auto* client = new PythonDecoderClient( QCoreApplication::instance() );
    return *client;
}
PythonDecoderClient::PythonDecoderClient( QObject* parent )
    : PreviewWorkerClient( parent,
                           { QStringLiteral( "python_decoder" ),
                             QStringLiteral( "decode" ),
                             Origin,
                             QStringLiteral( "message_decoder_python_worker.py" ),
                             { QStringLiteral( "message_decoder_python_worker.py" ),
                               QStringLiteral( "message_decoder_preview_worker.py" ),
                               QStringLiteral( "message_decoder_wire.py" ),
                               QStringLiteral( "workbench_process.py" ) },
                             validResponse,
                             false } )
{
}
void PythonDecoderClient::decode( QObject* owner, const QVariantMap& profile,
                                  const QString& decoder, const QVariantMap& context,
                                  const QByteArray& payload, int timeoutMs, const QString& id,
                                  Completion done )
{
    submit( owner, { { "profile", profile }, { "decoder", decoder }, { "context", context } },
            payload, timeoutMs, id, std::move( done ) );
}
