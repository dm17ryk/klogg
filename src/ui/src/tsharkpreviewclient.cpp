// SPDX-License-Identifier: GPL-3.0-or-later
#include "tsharkpreviewclient.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <algorithm>
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
constexpr double JsonIntegerMax = 9007199254740991.0;
// What a limited result says about its limit (#183, #198): `rows` (the row limit), `bytes` (the
// body limit, with the bytes the whole result needed), `pdmlItems` or `pdmlBytes` (what the worker
// reads of TShark's output, with the rows in the part read); its maximum and the rows shown.
bool validLimit( const QJsonObject& value )
{
    struct Kind {
        const char* reason;
        QStringList keys;
    };
    static const QMap<QString, Kind> kinds{
        { "rows", { "rendered_field_limit", { "totalRows" } } },
        { "bytes", { "response_limit", { "totalRows", "neededBytes" } } },
        { "pdmlItems", { "pdml_item_limit", { "readRows" } } },
        { "pdmlBytes", { "pdml_byte_limit", { "readRows" } } },
    };
    const auto limit = value.value( "limit" ).toObject();
    const auto name = limit.value( "name" ).toString();
    // The limit agrees with the result's reason (#183 review).
    if ( !value.value( "limit" ).isObject() || value.value( "state" ) != "completed"
         || !value.value( "limited" ).toBool() || !kinds.contains( name )
         || value.value( "reason" ) != kinds[ name ].reason )
        return false;
    // A byte cut of a tree cut at a PDML limit names that limit too (#198).
    const bool tree = name == "bytes" && limit.contains( "tree" );
    const auto keys = QStringList{ "maximum", "shownRows" } + kinds[ name ].keys;
    if ( limit.size() != 1 + keys.size() + ( tree ? 1 : 0 )
         || ( tree && limit.value( "tree" ) != "pdmlItems" && limit.value( "tree" ) != "pdmlBytes" ) )
        return false;
    for ( const auto& key : keys )
        if ( !limit.contains( key ) )
            return false;
    const auto shown = limit.value( "shownRows" ).toDouble();
    const auto maximum = limit.value( "maximum" ).toDouble();
    const bool rows = integer( limit.value( "shownRows" ), 0, TsharkPreviewClient::FieldLimit )
                      && shown == static_cast<double>( value.value( "fields" ).toArray().size() );
    if ( name == "pdmlItems" || name == "pdmlBytes" ) {
        // The rows shown are the part read up to the row limit; the items read include the
        // packet's own, so they hold fewer rows than the item limit.
        const bool items = name == "pdmlItems";
        const auto read = limit.value( "readRows" ).toDouble();
        return rows && shown >= 1
               && maximum
                      == ( items ? TsharkPreviewClient::PdmlItemLimit
                                 : TsharkPreviewClient::PdmlByteLimit )
               && integer( limit.value( "readRows" ), shown,
                           items ? TsharkPreviewClient::PdmlItemLimit - 1 : JsonIntegerMax )
               && shown == std::min( read, static_cast<double>( TsharkPreviewClient::FieldLimit ) );
    }
    // A cut shows fewer rows than the tree has. A row cut shows exactly the row limit; a byte
    // cut has a limit a request may choose, and the result needed more than it.
    const bool cut = rows && integer( limit.value( "totalRows" ), shown + 1, JsonIntegerMax );
    if ( name == "rows" )
        return cut && maximum == TsharkPreviewClient::FieldLimit && shown == maximum;
    return cut
           && integer( limit.value( "maximum" ), TsharkPreviewClient::ResultLimitMin,
                       TsharkPreviewClient::ResultLimitMax )
           && integer( limit.value( "neededBytes" ), maximum + 1, JsonIntegerMax );
}
// A row's columns as the worker derives them from the item's PDML attributes (#181, see
// display_columns in message_decoder_preview_worker.py): Name, the display text (showname after
// "label: ") as Value, Raw, Pos and Size. Text-only items are named by their text, a protocol
// keeps its showname, and a label or value Wireshark cut off keeps the raw show value.
QJsonArray displayColumns( const QJsonObject& attributes, bool protocol )
{
    static const QStringList textOnlyNames{ "", "_ws.lua.text", "text" };
    static const QString truncatedLabel = QStringLiteral( " [" ) + QChar( 0x2026 ) + ']';
    const auto text = [ & ]( const char* key ) { return attributes.value( key ).toString(); };
    auto name = text( "name" );
    const auto show = text( "show" );
    const auto showname = text( "showname" );
    QString value;
    const bool textOnly
        = !protocol && textOnlyNames.contains( name ) && !( showname + show ).isEmpty();
    if ( textOnly )
        name = showname.isEmpty() ? show : showname;
    else if ( protocol || !attributes.contains( "showname" ) || !showname.contains( ": " ) )
        value = attributes.contains( "show" ) ? show : showname;
    else {
        // Wireshark marks a cut label with " […]" right after the field's name ("Name […]: ...")
        // and a cut value with "…" after a prefix of the value; the same text elsewhere is the
        // dissector's own (appended text).
        const auto separator = showname.indexOf( ": " );
        value = showname.mid( separator + 2 );
        const bool cutValue = value.endsWith( QChar( 0x2026 ) ) && show != value
                              && show.startsWith( value.chopped( 1 ) );
        if ( !show.isEmpty()
             && ( showname.left( separator ).endsWith( truncatedLabel ) || cutValue ) )
            value = show;
    }
    // The raw bytes, or else the raw show value; a text-only item's text is its name already.
    const auto raw = attributes.contains( "value" ) ? text( "value" )
                     : textOnly                     ? QString()
                                                    : show;
    return { name, value, raw, text( "pos" ), text( "size" ) };
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
         && !integer( value.value( "enginePid" ), 1, JsonIntegerMax ) )
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
        // The row carries no item kind, so a field's or a protocol's derivation is accepted.
        const auto columns = entry.value( "columns" ).toArray();
        if ( columns != displayColumns( attributes, false )
             && columns != displayColumns( attributes, true ) )
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
