// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginfeatures.h"

#include "pluginmanifest.h"
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace cilogg::plugins {
namespace {
bool isPlainName( const QString& name, int maximum )
{
    static const QRegularExpression pattern(
        QRegularExpression::anchoredPattern( "[a-z0-9][a-z0-9_]*" ) );
    return !name.isEmpty() && name.size() <= maximum && pattern.match( name ).hasMatch();
}

bool isMethodName( const QString& name )
{
    static const QRegularExpression pattern(
        QRegularExpression::anchoredPattern( "[a-z][a-z0-9_]*(/[a-z][a-z0-9_]*)*" ) );
    return !name.isEmpty() && name.size() <= MaxMethodNameChars
           && pattern.match( name ).hasMatch();
}

// One string form of an icon reference (no light/dark object).
QString validateIconForm( const QString& reference )
{
    if ( reference.isEmpty() || reference.size() > 256 ) {
        return QStringLiteral( "An icon reference is 1 to 256 characters." );
    }
    if ( reference.startsWith( QLatin1String( "builtin:" ) ) ) {
        return isPlainName( reference.mid( 8 ).replace( '-', '_' ), 32 )
                   ? QString{}
                   : QStringLiteral( "Invalid built-in icon name." );
    }
    if ( reference.startsWith( QLatin1String( "status:" ) ) ) {
        return isPlainName( reference.mid( 7 ), 16 ) ? QString{}
                                                     : QStringLiteral( "Invalid status tone." );
    }
    if ( reference.startsWith( QLatin1String( "image:" ) ) ) {
        static const QRegularExpression id(
            QRegularExpression::anchoredPattern( "[A-Za-z0-9._-]{1,64}" ) );
        return id.match( reference.mid( 6 ) ).hasMatch()
                   ? QString{}
                   : QStringLiteral( "Invalid registered image id." );
    }
    static const QRegularExpression imageSuffix( "\\.(png|ico|svg)$",
                                                 QRegularExpression::CaseInsensitiveOption );
    if ( !safeRelativePath( reference ) || !imageSuffix.match( reference ).hasMatch() ) {
        return QStringLiteral(
            "An icon is a .png, .ico or .svg path inside the plugin, builtin:, status: or image:." );
    }
    return {};
}

// Plain text of a tooltip row value.
bool rowText( const QJsonValue& value, QString* text )
{
    if ( value.isString() ) {
        *text = value.toString();
        return true;
    }
    if ( value.isDouble() ) {
        *text = QString::number( value.toDouble(), 'g', 15 );
        return true;
    }
    if ( value.isBool() ) {
        *text = value.toBool() ? QStringLiteral( "true" ) : QStringLiteral( "false" );
        return true;
    }
    return false;
}
} // namespace

const std::vector<std::pair<QString, int>>& hostFeatures()
{
    static const std::vector<std::pair<QString, int>> features{
        { QStringLiteral( "views" ), 2 },   { QStringLiteral( "view_edit" ), 1 },
        { QStringLiteral( "queries" ), 1 }, { QStringLiteral( "tooltips" ), 1 },
        { QStringLiteral( "icons" ), 1 },   { QStringLiteral( "menus" ), 1 },
        { QStringLiteral( "shortcuts" ), 1 }, { QStringLiteral( "choices" ), 1 },
        { QStringLiteral( "toggles" ), 1 }, { QStringLiteral( "field_kinds" ), 1 },
        { QStringLiteral( "validation" ), 1 }, { QStringLiteral( "rich_tables" ), 1 },
        { QStringLiteral( "element_menus" ), 1 }, { QStringLiteral( "tree" ), 1 },
        { QStringLiteral( "file_tree" ), 1 }, { QStringLiteral( "layout" ), 1 },
        { QStringLiteral( "text_view" ), 1 }, { QStringLiteral( "images" ), 1 },
        { QStringLiteral( "charts" ), 1 }, { QStringLiteral( "view_dialogs" ), 1 },
    };
    return features;
}

const QStringList& dialogButtonNames()
{
    static const QStringList names{ QStringLiteral( "ok" ),    QStringLiteral( "cancel" ),
                                    QStringLiteral( "apply" ), QStringLiteral( "close" ),
                                    QStringLiteral( "yes" ),   QStringLiteral( "no" ) };
    return names;
}

QJsonArray hostFeatureNames()
{
    QJsonArray names;
    for ( const auto& feature : hostFeatures() ) {
        names.append( feature.first );
    }
    return names;
}

QJsonObject hostFeatureVersions()
{
    QJsonObject versions;
    for ( const auto& feature : hostFeatures() ) {
        versions.insert( feature.first, feature.second );
    }
    return versions;
}

QJsonObject hostLimits()
{
    return { { "max_elements", MaxViewElements },
             { "max_table_cells", MaxViewTableCells },
             { "max_text_chars", MaxViewTextChars },
             { "max_message_bytes", MaxMessageBytes } };
}

QueryTimeouts queryTimeouts( const QString& method )
{
    if ( method == QLatin1String( "tooltip/resolve" ) ) {
        return { 1500, 5000 };
    }
    if ( method == QLatin1String( "menu/populate" ) ) {
        return { 1000, 3000 };
    }
    if ( method == QLatin1String( "tree/children" ) ) {
        return { 10000, 30000 };
    }
    if ( method == QLatin1String( "field/complete" ) ) {
        return { 500, 2000 };
    }
    return {};
}

bool isQueryMethod( const QString& method )
{
    return queryTimeouts( method ).maximumMs > 0;
}

int boundedQueryTimeout( const QString& method, int requested )
{
    const auto timeouts = queryTimeouts( method );
    return requested > 0 ? std::min( requested, timeouts.maximumMs ) : timeouts.defaultMs;
}

QString validateHandles( const QJsonValue& handles )
{
    if ( handles.isUndefined() ) {
        return {};
    }
    if ( !handles.isArray() || handles.toArray().size() > MaxHandledMethods ) {
        return QStringLiteral( "handles must be a list of at most %1 method names." )
            .arg( MaxHandledMethods );
    }
    for ( const auto& method : handles.toArray() ) {
        if ( !method.isString() || !isMethodName( method.toString() ) ) {
            return QStringLiteral( "Invalid method name in handles: %1" )
                .arg( method.toVariant().toString().left( MaxMethodNameChars ) );
        }
    }
    return {};
}

QStringList handledMethods( const QJsonValue& handles )
{
    QStringList methods;
    if ( !validateHandles( handles ).isEmpty() ) {
        return methods;
    }
    for ( const auto& method : handles.toArray() ) {
        if ( !methods.contains( method.toString() ) ) {
            methods.append( method.toString() );
        }
    }
    return methods;
}

QStringList pluginFeatures( const QJsonValue& features )
{
    QStringList methods;
    if ( !features.isArray() ) {
        return methods;
    }
    for ( const auto& method : features.toArray() ) {
        if ( methods.size() >= MaxHandledMethods ) {
            break;
        }
        if ( method.isString() && isMethodName( method.toString() )
             && !methods.contains( method.toString() ) ) {
            methods.append( method.toString() );
        }
    }
    return methods;
}

QJsonObject pluginSdk( const QJsonValue& sdk )
{
    const auto object = sdk.toObject();
    const auto name = object.value( "name" );
    const auto version = object.value( "version" );
    if ( !name.isString() || !version.isString() || name.toString().isEmpty()
         || name.toString().size() > 64 || version.toString().size() > 64 ) {
        return {};
    }
    return { { "name", name.toString() }, { "version", version.toString() } };
}

QString validateRequires( const QJsonValue& requirement, QStringList* missing )
{
    if ( requirement.isUndefined() ) {
        return {};
    }
    const auto features = requirement.toObject().value( "host_features" );
    if ( !requirement.isObject()
         || ( !features.isUndefined()
              && ( !features.isArray() || features.toArray().size() > MaxHandledMethods ) ) ) {
        return QStringLiteral( "requires must be an object with a host_features list." );
    }
    static const QRegularExpression pattern( QRegularExpression::anchoredPattern(
        "([a-z0-9][a-z0-9_]{0,63})(?:>=([1-9][0-9]{0,3}))?" ) );
    for ( const auto& value : features.toArray() ) {
        const auto match = pattern.match( value.toString() );
        if ( !value.isString() || !match.hasMatch() ) {
            return QStringLiteral( "Invalid required host feature: %1" )
                .arg( value.toVariant().toString().left( 64 ) );
        }
        const auto name = match.captured( 1 );
        const auto version = match.captured( 2 ).isEmpty() ? 1 : match.captured( 2 ).toInt();
        const auto& known = hostFeatures();
        const auto found = std::find_if( known.begin(), known.end(),
                                         [ &name ]( const auto& item ) { return item.first == name; } );
        if ( missing && ( found == known.end() || found->second < version ) ) {
            missing->append( value.toString() );
        }
    }
    return {};
}

const QStringList& builtinIconNames()
{
    static const QStringList names{
        "info",   "warning", "error",  "question", "ok",     "cancel",   "add",    "remove",
        "edit",   "delete",  "copy",   "paste",    "save",   "open",     "folder", "file",
        "search", "filter",  "refresh", "settings", "play",  "pause",    "stop",   "record",
        "upload", "download", "up",    "down",     "left",   "right",    "lock",   "unlock",
        "link",   "device",  "port",   "chip",     "clock",  "star",     "bookmark", "bell",
    };
    return names;
}

const QStringList& toneNames()
{
    static const QStringList tones{ "normal", "ok", "warn", "error", "muted", "accent" };
    return tones;
}

QString validateIconReference( const QJsonValue& reference )
{
    if ( reference.isString() ) {
        return validateIconForm( reference.toString() );
    }
    if ( !reference.isObject() ) {
        return QStringLiteral( "An icon reference is a string or {light, dark}." );
    }
    const auto object = reference.toObject();
    if ( !object.contains( "light" ) && !object.contains( "dark" ) ) {
        return QStringLiteral( "A themed icon needs light or dark." );
    }
    for ( const auto key : { "light", "dark" } ) {
        const auto value = object.value( QLatin1String( key ) );
        if ( value.isUndefined() ) {
            continue;
        }
        if ( !value.isString() ) {
            return QStringLiteral( "The %1 icon must be a string reference." )
                .arg( QLatin1String( key ) );
        }
        const auto error = validateIconForm( value.toString() );
        if ( !error.isEmpty() ) {
            return error;
        }
    }
    return {};
}

QJsonObject sanitizeCompletion( const QJsonValue& answer, QString* error )
{
    if ( answer.isNull() || answer.isUndefined() ) {
        return {};
    }
    const auto items = answer.isArray() ? answer.toArray() : answer.toObject().value( "items" );
    if ( !items.isArray() ) {
        *error = QStringLiteral( "A field/complete answer is {items: [...]}." );
        return {};
    }
    QJsonArray kept;
    for ( const auto& value : items.toArray() ) {
        if ( kept.size() >= MaxCompletionItems ) {
            break;
        }
        const auto item = value.toObject();
        if ( !value.isObject() || !item.value( "value" ).isString() ) {
            *error = QStringLiteral( "A completion item needs a string value." );
            return {};
        }
        QJsonObject entry{ { "value", item.value( "value" ).toString().left( 65536 ) } };
        if ( item.value( "label" ).isString() ) {
            entry.insert( "label", item.value( "label" ).toString().left( MaxViewTextChars ) );
        }
        if ( item.contains( "icon" ) && validateIconReference( item.value( "icon" ) ).isEmpty() ) {
            entry.insert( "icon", item.value( "icon" ) );
        }
        kept.append( entry );
    }
    return { { "items", kept } };
}

namespace {
// Counts the nodes of a tree/children answer, nested children included; false when one is no
// object or the count passes `budget`.
bool countTreeNodes( const QJsonArray& nodes, int depth, int* budget )
{
    if ( depth > MaxTreeDepth ) {
        return false;
    }
    for ( const auto& node : nodes ) {
        if ( !node.isObject() || --*budget < 0 ) {
            return false;
        }
        const auto children = node.toObject().value( "children" );
        if ( children.isArray() && !countTreeNodes( children.toArray(), depth + 1, budget ) ) {
            return false;
        }
    }
    return true;
}
} // namespace

QJsonObject sanitizeTreeChildren( const QJsonValue& answer, QString* error )
{
    if ( answer.isNull() || answer.isUndefined() ) {
        return {};
    }
    const auto nodes = answer.isArray() ? QJsonValue( answer ) : answer.toObject().value( "nodes" );
    if ( !nodes.isArray() ) {
        *error = QStringLiteral( "A tree/children answer is {nodes: [...]}." );
        return {};
    }
    int budget = MaxTreeChildrenNodes;
    if ( !countTreeNodes( nodes.toArray(), 1, &budget ) ) {
        *error = QStringLiteral( "A tree/children answer holds at most %1 node objects, depth %2." )
                     .arg( MaxTreeChildrenNodes )
                     .arg( MaxTreeDepth );
        return {};
    }
    if ( nodes.toArray().isEmpty() ) {
        return {};
    }
    return { { "nodes", nodes } };
}

QJsonObject sanitizeTooltip( const QJsonValue& answer, QString* error )
{
    if ( answer.isNull() || answer.isUndefined() ) {
        return {};
    }
    QJsonObject object;
    if ( answer.isString() ) {
        object.insert( "text", answer );
    }
    else if ( answer.isObject() ) {
        object = answer.toObject();
    }
    else {
        *error = QStringLiteral( "A tooltip answer is an object, a string or null." );
        return {};
    }
    for ( const auto key : { "title", "text", "footer", "tone" } ) {
        const auto value = object.value( QLatin1String( key ) );
        if ( !value.isUndefined() && !value.isNull() && !value.isString() ) {
            *error = QStringLiteral( "Tooltip %1 must be text." ).arg( QLatin1String( key ) );
            return {};
        }
    }
    const auto rows = object.value( "rows" );
    const auto ttl = object.value( "ttl_ms" );
    if ( ( !rows.isUndefined() && !rows.isNull() && !rows.isArray() )
         || ( !ttl.isUndefined() && !ttl.isNull()
              && ( !ttl.isDouble() || !std::isfinite( ttl.toDouble() ) ) ) ) {
        *error = QStringLiteral( "Tooltip rows must be a list and ttl_ms a number." );
        return {};
    }
    int budget = MaxTooltipTextChars;
    const auto take = [ &budget ]( const QString& text ) {
        const auto kept = text.left( std::max( 0, budget ) );
        budget -= static_cast<int>( kept.size() );
        return kept;
    };
    QJsonObject result;
    for ( const auto key : { "title", "text" } ) {
        const auto value = take( object.value( QLatin1String( key ) ).toString() );
        if ( !value.isEmpty() ) {
            result.insert( QLatin1String( key ), value );
        }
    }
    QJsonArray parsedRows;
    for ( const auto& row : rows.toArray() ) {
        const auto pair = row.toArray();
        QString name;
        QString value;
        if ( !row.isArray() || pair.size() != 2 || !rowText( pair.at( 0 ), &name )
             || !rowText( pair.at( 1 ), &value ) ) {
            *error = QStringLiteral( "A tooltip row is a pair of texts." );
            return {};
        }
        if ( parsedRows.size() >= MaxTooltipRows || budget <= 0 ) {
            continue; // capped
        }
        name = take( name );
        value = take( value );
        parsedRows.append( QJsonArray{ name, value } );
    }
    if ( !parsedRows.isEmpty() ) {
        result.insert( "rows", parsedRows );
    }
    const auto footer = take( object.value( "footer" ).toString() );
    if ( !footer.isEmpty() ) {
        result.insert( "footer", footer );
    }
    // An icon is something to show on its own (the renderer draws an icon without a title).
    if ( object.contains( "icon" ) && validateIconReference( object.value( "icon" ) ).isEmpty() ) {
        result.insert( "icon", object.value( "icon" ) );
    }
    if ( result.isEmpty() ) {
        return {}; // nothing to show: the static tooltip stays
    }
    if ( toneNames().contains( object.value( "tone" ).toString() ) ) {
        result.insert( "tone", object.value( "tone" ) );
    }
    const auto ttlMs = ttl.isDouble() ? ttl.toDouble() : double{ DefaultTooltipTtlMs };
    result.insert( "ttl_ms", static_cast<qint64>( std::clamp( ttlMs, 0.0,
                                                              double{ MaxTooltipTtlMs } ) ) );
    return result;
}
} // namespace cilogg::plugins
