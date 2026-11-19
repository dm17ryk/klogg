// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginmenuspec.h"

#include "pluginfeatures.h"
#include <QKeySequence>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace cilogg::plugins {
namespace {
struct EntryRules {
    QStringList actions;
    QStringList views;
    bool allowShortcut = true;
    bool allowDynamic = true;
    int maxEntries = MaxMenuEntries;
};

struct EntryCount {
    int entries = 0;
    QSet<QString> ids;
};

bool optionalBool( const QJsonObject& object, const char* key )
{
    const auto value = object.value( QLatin1String( key ) );
    return value.isUndefined() || value.isBool();
}

bool optionalText( const QJsonObject& object, const char* key, int maximum )
{
    const auto value = object.value( QLatin1String( key ) );
    return value.isUndefined() || ( value.isString() && value.toString().size() <= maximum );
}

// Ids name shortcuts (`plugin/<plugin>/<menu>/<item>`), so they may not contain `/`.
bool validId( const QJsonValue& value )
{
    return value.isString() && !value.toString().isEmpty()
           && value.toString().size() <= MaxMenuIdChars
           && !value.toString().contains( QLatin1Char( '/' ) );
}

bool nonNegativeInteger( const QJsonValue& value )
{
    return value.isDouble() && value.toDouble() >= 0 && value.toDouble() <= 1e12
           && std::floor( value.toDouble() ) == value.toDouble();
}

QString validateWhen( const QJsonValue& when )
{
    if ( when.isUndefined() ) {
        return {};
    }
    if ( !when.isObject() ) {
        return QStringLiteral( "Invalid when conditions." );
    }
    const auto conditions = when.toObject();
    for ( const auto* key : { "views", "tab_kinds", "file_patterns" } ) {
        const auto value = conditions.value( QLatin1String( key ) );
        if ( value.isUndefined() ) {
            continue;
        }
        if ( !value.isArray() || value.toArray().size() > MaxMenuConditionValues ) {
            return QStringLiteral( "Invalid when.%1." ).arg( QLatin1String( key ) );
        }
        for ( const auto& item : value.toArray() ) {
            if ( !item.isString() || item.toString().isEmpty() || item.toString().size() > 256 ) {
                return QStringLiteral( "Invalid when.%1." ).arg( QLatin1String( key ) );
            }
        }
    }
    if ( conditions.contains( "selection" ) && !conditions.value( "selection" ).isString() ) {
        return QStringLiteral( "Invalid when.selection." );
    }
    for ( const auto* key : { "min_lines", "max_lines" } ) {
        const auto value = conditions.value( QLatin1String( key ) );
        if ( !value.isUndefined() && !nonNegativeInteger( value ) ) {
            return QStringLiteral( "Invalid when.%1." ).arg( QLatin1String( key ) );
        }
    }
    return {};
}

QString validateEntries( const QJsonArray& items, int depth, const EntryRules& rules,
                         EntryCount* count )
{
    if ( depth > MaxMenuDepth ) {
        return QStringLiteral( "Menu nested deeper than %1 levels." ).arg( MaxMenuDepth );
    }
    for ( const auto& value : items ) {
        if ( !value.isObject() ) {
            return QStringLiteral( "Invalid menu entry." );
        }
        if ( ++count->entries > rules.maxEntries ) {
            return QStringLiteral( "Too many menu entries (maximum %1)." ).arg( rules.maxEntries );
        }
        const auto entry = value.toObject();
        if ( entry.contains( "type" ) && !entry.value( "type" ).isString() ) {
            return QStringLiteral( "Invalid menu entry type." );
        }
        const auto type = entry.value( "type" ).toString( QStringLiteral( "item" ) );
        if ( type == QLatin1String( "separator" ) ) {
            continue;
        }
        if ( type == QLatin1String( "section" ) ) {
            if ( !optionalText( entry, "title", MaxMenuTitleChars ) ) {
                return QStringLiteral( "Invalid menu section title." );
            }
            continue;
        }
        if ( type != QLatin1String( "item" ) ) {
            return QStringLiteral( "Unknown menu entry type: %1" ).arg( type.left( 32 ) );
        }
        const auto id = entry.value( "id" ).toString();
        if ( !validId( entry.value( "id" ) ) || count->ids.contains( id ) ) {
            return QStringLiteral( "Invalid or duplicate menu entry id: %1" ).arg( id.left( 128 ) );
        }
        count->ids.insert( id );
        if ( !entry.value( "title" ).isString() || entry.value( "title" ).toString().isEmpty()
             || entry.value( "title" ).toString().size() > MaxMenuTitleChars ) {
            return QStringLiteral( "Invalid menu entry title: %1" ).arg( id );
        }
        if ( !optionalText( entry, "tooltip", MaxMenuTooltipChars )
             || !optionalText( entry, "group", MaxMenuIdChars ) ) {
            return QStringLiteral( "Invalid menu entry tooltip or group: %1" ).arg( id );
        }
        for ( const auto* key : { "enabled", "visible", "checkable", "checked", "dynamic", "event" } ) {
            if ( !optionalBool( entry, key ) ) {
                return QStringLiteral( "Invalid menu entry flag %1: %2" )
                    .arg( QLatin1String( key ), id );
            }
        }
        if ( entry.contains( "icon" ) ) {
            const auto error = validateIconReference( entry.value( "icon" ) );
            if ( !error.isEmpty() ) {
                return QStringLiteral( "Invalid menu entry icon: %1 (%2)" ).arg( id, error );
            }
        }
        const auto whenError = validateWhen( entry.value( "when" ) );
        if ( !whenError.isEmpty() ) {
            return QStringLiteral( "%1 (%2)" ).arg( whenError, id );
        }
        const bool checkable = entry.value( "checkable" ).toBool();
        if ( ( entry.contains( "group" ) || entry.value( "checked" ).toBool() ) && !checkable ) {
            return QStringLiteral( "A checked or grouped menu entry must be checkable: %1" ).arg( id );
        }
        const bool submenu = entry.contains( "items" );
        const bool dynamic = entry.value( "dynamic" ).toBool();
        const int targets = ( entry.contains( "action" ) ? 1 : 0 )
                            + ( entry.contains( "view" ) ? 1 : 0 )
                            + ( entry.value( "event" ).toBool() ? 1 : 0 );
        if ( submenu || dynamic ) {
            if ( targets != 0 || ( submenu && dynamic ) || entry.contains( "shortcut" ) || checkable ) {
                return QStringLiteral( "A submenu has no action, view, event, shortcut or check "
                                       "mark: %1" )
                    .arg( id );
            }
            if ( dynamic && !rules.allowDynamic ) {
                return QStringLiteral( "Dynamic entries are not allowed here: %1" ).arg( id );
            }
            if ( submenu ) {
                if ( !entry.value( "items" ).isArray() ) {
                    return QStringLiteral( "Invalid submenu items: %1" ).arg( id );
                }
                const auto error
                    = validateEntries( entry.value( "items" ).toArray(), depth + 1, rules, count );
                if ( !error.isEmpty() ) {
                    return error;
                }
            }
            continue;
        }
        if ( targets != 1 ) {
            return QStringLiteral( "A menu entry needs exactly one of action, view and event: %1" )
                .arg( id );
        }
        if ( entry.contains( "action" ) && !rules.actions.contains( entry.value( "action" ).toString() ) ) {
            return QStringLiteral( "Menu entry %1 names an undeclared action." ).arg( id );
        }
        if ( entry.contains( "view" ) && !rules.views.contains( entry.value( "view" ).toString() ) ) {
            return QStringLiteral( "Menu entry %1 names an undeclared view." ).arg( id );
        }
        if ( entry.contains( "arguments" )
             && ( !entry.value( "arguments" ).isObject() || !entry.contains( "action" ) ) ) {
            return QStringLiteral( "Menu entry arguments need an action: %1" ).arg( id );
        }
        if ( entry.contains( "shortcut" ) ) {
            if ( !rules.allowShortcut ) {
                return QStringLiteral( "Shortcuts are not allowed here: %1" ).arg( id );
            }
            const auto error = validateShortcut( entry.value( "shortcut" ) );
            if ( !error.isEmpty() ) {
                return QStringLiteral( "%1 (%2)" ).arg( error, id );
            }
        }
    }
    return {};
}

void collectShortcuts( const QJsonArray& items, const PluginShortcut& base,
                       std::vector<PluginShortcut>* shortcuts )
{
    for ( const auto& value : items ) {
        const auto entry = value.toObject();
        if ( entry.contains( "items" ) ) {
            collectShortcuts( entry.value( "items" ).toArray(), base, shortcuts );
            continue;
        }
        if ( !entry.contains( "shortcut" ) ) {
            continue;
        }
        auto shortcut = base;
        shortcut.itemId = entry.value( "id" ).toString();
        shortcut.key = QStringLiteral( "plugin/%1/%2/%3" )
                           .arg( base.pluginId, base.menuId, shortcut.itemId );
        shortcut.title = entry.value( "title" ).toString();
        shortcut.declared = shortcutKeys( entry.value( "shortcut" ) );
        shortcuts->push_back( shortcut );
    }
}

QJsonObject findEntry( const QJsonArray& items, const QString& itemId )
{
    for ( const auto& value : items ) {
        const auto entry = value.toObject();
        if ( entry.value( "id" ).toString() == itemId && entry.value( "type" ).toString( "item" )
                                                             == QLatin1String( "item" ) ) {
            return entry;
        }
        if ( entry.value( "items" ).isArray() ) {
            const auto found = findEntry( entry.value( "items" ).toArray(), itemId );
            if ( !found.isEmpty() ) {
                return found;
            }
        }
    }
    return {};
}
} // namespace

QStringList declaredIds( const QJsonValue& list )
{
    QStringList ids;
    for ( const auto& value : list.toArray() ) {
        ids.append( value.toObject().value( "id" ).toString() );
    }
    return ids;
}

const QStringList& menuLocations()
{
    static const QStringList locations{ "plugins", "menubar", "tab",    "pane",
                                        "log",     "element", "toolbar" };
    return locations;
}

const QStringList& renderedMenuLocations()
{
    static const QStringList locations{ "plugins", "menubar", "tab", "pane", "log" };
    return locations;
}

QString normalizedShortcut( const QString& keys )
{
    return QKeySequence::fromString( keys, QKeySequence::PortableText )
        .toString( QKeySequence::PortableText );
}

const QStringList& reservedShortcuts()
{
    static const QStringList reserved = [] {
        QStringList keys;
        for ( const auto* text : { "Ctrl+C", "Ctrl+V", "Ctrl+X", "Ctrl+A", "Ctrl+F", "Ctrl+Z", "F2",
                                   "Return", "Enter", "Esc", "Tab", "Del" } ) {
            keys.append( normalizedShortcut( QLatin1String( text ) ) );
        }
        return keys;
    }();
    return reserved;
}

QString validateShortcut( const QJsonValue& shortcut )
{
    QJsonArray list;
    if ( shortcut.isString() ) {
        list.append( shortcut );
    }
    else if ( shortcut.isArray() ) {
        list = shortcut.toArray();
    }
    if ( list.isEmpty() || list.size() > 2 ) {
        return QStringLiteral( "A shortcut is a key sequence or a list of two." );
    }
    for ( const auto& value : list ) {
        const auto text = value.toString();
        const auto sequence = QKeySequence::fromString( text, QKeySequence::PortableText );
        if ( !value.isString() || text.isEmpty() || text.size() > 64 || sequence.isEmpty()
             || sequence.count() != 1 ) {
            return QStringLiteral( "Invalid shortcut: %1" ).arg( text.left( 64 ) );
        }
#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
        const auto combination = sequence[ 0 ];
        const auto key = combination.key();
        const auto modifiers = combination.keyboardModifiers();
#else
        const auto combination = static_cast<int>( sequence[ 0 ] );
        const auto key = static_cast<Qt::Key>( combination & ~int( Qt::KeyboardModifierMask ) );
        const auto modifiers
            = Qt::KeyboardModifiers( combination & int( Qt::KeyboardModifierMask ) );
#endif
        const bool functionKey = key >= Qt::Key_F1 && key <= Qt::Key_F35;
        const bool modified = modifiers.testFlag( Qt::ControlModifier )
                              || modifiers.testFlag( Qt::AltModifier )
                              || modifiers.testFlag( Qt::MetaModifier );
        if ( key == Qt::Key_unknown || ( !functionKey && !modified ) ) {
            return QStringLiteral( "A shortcut needs Ctrl, Alt or Meta, or a function key: %1" )
                .arg( text.left( 64 ) );
        }
    }
    return {};
}

QStringList shortcutKeys( const QJsonValue& shortcut )
{
    QStringList keys;
    const auto list = shortcut.isArray() ? shortcut.toArray() : QJsonArray{ shortcut };
    for ( const auto& value : list ) {
        const auto normalized = normalizedShortcut( value.toString() );
        if ( !normalized.isEmpty() && !keys.contains( normalized ) ) {
            keys.append( normalized );
        }
    }
    return keys;
}

QString validateMenus( const QJsonValue& menus, const QStringList& actions,
                       const QStringList& views )
{
    if ( menus.isUndefined() ) {
        return {};
    }
    if ( !menus.isArray() || menus.toArray().size() > MaxMenusPerPlugin ) {
        return QStringLiteral( "Invalid menus (at most %1)." ).arg( MaxMenusPerPlugin );
    }
    EntryRules rules;
    rules.actions = actions;
    rules.views = views;
    int entries = 0;
    QSet<QString> menuIds;
    bool menubar = false;
    static const QRegularExpression locationPattern(
        QRegularExpression::anchoredPattern( "[a-z][a-z0-9_]{0,63}" ) );
    for ( const auto& value : menus.toArray() ) {
        const auto menu = value.toObject();
        const auto id = menu.value( "id" ).toString();
        // `action` and `view` would share their names with the shortcuts of actions and views.
        if ( !value.isObject() || !validId( menu.value( "id" ) ) || menuIds.contains( id )
             || id == QLatin1String( "action" ) || id == QLatin1String( "view" ) ) {
            return QStringLiteral( "Invalid or duplicate menu id: %1" ).arg( id.left( 128 ) );
        }
        menuIds.insert( id );
        const auto location = menu.value( "location" ).toString();
        if ( !menu.value( "location" ).isString()
             || !locationPattern.match( location ).hasMatch() ) {
            return QStringLiteral( "Invalid menu location: %1" ).arg( id );
        }
        if ( location == QLatin1String( "menubar" ) ) {
            if ( menubar ) {
                return QStringLiteral( "At most one menubar menu per plugin." );
            }
            menubar = true;
        }
        if ( !optionalText( menu, "title", MaxMenuTitleChars ) || !menu.value( "items" ).isArray() ) {
            return QStringLiteral( "Invalid menu title or items: %1" ).arg( id );
        }
        if ( menu.contains( "icon" ) ) {
            const auto error = validateIconReference( menu.value( "icon" ) );
            if ( !error.isEmpty() ) {
                return QStringLiteral( "Invalid menu icon: %1 (%2)" ).arg( id, error );
            }
        }
        EntryCount count;
        count.entries = entries;
        const auto error = validateEntries( menu.value( "items" ).toArray(), 1, rules, &count );
        if ( !error.isEmpty() ) {
            return error;
        }
        entries = count.entries;
    }
    return {};
}

QString validateShortcutCount( const QJsonObject& manifest )
{
    const auto shortcuts = manifestShortcuts( {}, {}, manifest );
    if ( static_cast<int>( shortcuts.size() ) > MaxPluginShortcuts ) {
        return QStringLiteral( "Too many shortcuts (maximum %1)." ).arg( MaxPluginShortcuts );
    }
    return {};
}

QJsonObject findMenu( const QJsonObject& manifest, const QString& menuId )
{
    for ( const auto& value : manifest.value( "menus" ).toArray() ) {
        if ( value.toObject().value( "id" ).toString() == menuId ) {
            return value.toObject();
        }
    }
    return {};
}

QJsonObject findMenuEntry( const QJsonObject& menu, const QString& itemId )
{
    return itemId.isEmpty() ? QJsonObject{} : findEntry( menu.value( "items" ).toArray(), itemId );
}

QString menuEntryKind( const QJsonObject& entry )
{
    const auto type = entry.value( "type" ).toString( QStringLiteral( "item" ) );
    if ( type != QLatin1String( "item" ) ) {
        return type;
    }
    if ( entry.contains( "items" ) ) {
        return QStringLiteral( "submenu" );
    }
    if ( entry.value( "dynamic" ).toBool() ) {
        return QStringLiteral( "dynamic" );
    }
    if ( entry.contains( "action" ) ) {
        return QStringLiteral( "action" );
    }
    if ( entry.contains( "view" ) ) {
        return QStringLiteral( "view" );
    }
    return QStringLiteral( "event" );
}

QJsonObject sanitizeMenuPopulate( const QJsonValue& answer, const QStringList& actions,
                                  const QStringList& views, QString* error )
{
    if ( answer.isNull() || answer.isUndefined() ) {
        return { { "items", QJsonArray{} }, { "cache_ms", 0 } };
    }
    const auto object = answer.toObject();
    if ( !answer.isObject() || !object.value( "items" ).isArray() ) {
        *error = QStringLiteral( "A menu/populate answer is {items, cache_ms}." );
        return {};
    }
    const auto cache = object.value( "cache_ms" );
    if ( !cache.isUndefined() && !nonNegativeInteger( cache ) ) {
        *error = QStringLiteral( "Invalid cache_ms." );
        return {};
    }
    EntryRules rules;
    rules.actions = actions;
    rules.views = views;
    rules.allowShortcut = false;
    rules.allowDynamic = false;
    rules.maxEntries = MaxDynamicMenuEntries;
    EntryCount count;
    const auto items = object.value( "items" ).toArray();
    const auto entryError = validateEntries( items, 1, rules, &count );
    if ( !entryError.isEmpty() ) {
        *error = entryError;
        return {};
    }
    return { { "items", items },
             { "cache_ms", static_cast<qint64>( std::min( cache.toDouble( 0 ),
                                                          double( MaxDynamicMenuCacheMs ) ) ) } };
}

QJsonArray sanitizeMenuUpdate( const QJsonObject& manifest, const QJsonObject& params,
                               QString* error )
{
    const auto menu = findMenu( manifest, params.value( "menu_id" ).toString() );
    if ( menu.isEmpty() ) {
        *error = QStringLiteral( "menu/update names an undeclared menu." );
        return {};
    }
    const auto items = params.value( "items" );
    if ( !items.isArray() || items.toArray().size() > MaxMenuEntries ) {
        *error = QStringLiteral( "menu/update needs items (at most %1)." ).arg( MaxMenuEntries );
        return {};
    }
    QJsonArray changes;
    int ignored = 0;
    for ( const auto& value : items.toArray() ) {
        const auto item = value.toObject();
        const auto id = item.value( "id" ).toString();
        const auto declared = findMenuEntry( menu, id );
        if ( declared.isEmpty() || !item.value( "set" ).isObject() ) {
            ++ignored;
            continue;
        }
        const auto set = item.value( "set" ).toObject();
        QJsonObject applied;
        if ( set.value( "title" ).isString() && !set.value( "title" ).toString().isEmpty() ) {
            applied.insert( "title", set.value( "title" ).toString().left( MaxMenuTitleChars ) );
        }
        if ( set.value( "tooltip" ).isString() ) {
            applied.insert( "tooltip", set.value( "tooltip" ).toString().left( MaxMenuTooltipChars ) );
        }
        if ( set.contains( "icon" ) && validateIconReference( set.value( "icon" ) ).isEmpty() ) {
            applied.insert( "icon", set.value( "icon" ) );
        }
        for ( const auto* key : { "enabled", "visible" } ) {
            if ( set.value( QLatin1String( key ) ).isBool() ) {
                applied.insert( QLatin1String( key ), set.value( QLatin1String( key ) ) );
            }
        }
        if ( set.value( "checked" ).isBool() && declared.value( "checkable" ).toBool() ) {
            applied.insert( "checked", set.value( "checked" ) );
        }
        if ( applied.isEmpty() ) {
            ++ignored;
            continue;
        }
        changes.append( QJsonObject{ { "id", id }, { "set", applied } } );
    }
    if ( changes.isEmpty() ) {
        *error = QStringLiteral( "menu/update changes nothing this host knows (%1 ignored)." )
                     .arg( ignored );
    }
    return changes;
}

bool menuConditionsHold( const QJsonValue& when, const MenuConditionContext& context )
{
    if ( when.isUndefined() || when.isNull() ) {
        return true;
    }
    const auto conditions = when.toObject();
    for ( auto it = conditions.begin(); it != conditions.end(); ++it ) {
        const auto key = it.key();
        const auto value = it.value();
        const auto list = value.toArray();
        if ( key == QLatin1String( "views" ) ) {
            if ( context.view.isEmpty() || !list.contains( context.view ) ) {
                return false;
            }
        }
        else if ( key == QLatin1String( "tab_kinds" ) ) {
            if ( context.tabKind.isEmpty() || !list.contains( context.tabKind ) ) {
                return false;
            }
        }
        else if ( key == QLatin1String( "file_patterns" ) ) {
            const bool matched
                = std::any_of( list.begin(), list.end(), [ &context ]( const auto& pattern ) {
                      // wildcardToRegularExpression (Qt 5.12 and 6) anchors the pattern.
                      return QRegularExpression( QRegularExpression::wildcardToRegularExpression(
                                                     pattern.toString() ),
                                                 QRegularExpression::CaseInsensitiveOption )
                          .match( context.fileName )
                          .hasMatch();
                  } );
            if ( context.fileName.isEmpty() || !matched ) {
                return false;
            }
        }
        else if ( key == QLatin1String( "selection" ) ) {
            const auto mode = value.toString();
            const bool holds = mode == QLatin1String( "none" )    ? !context.hasSelection
                               : mode == QLatin1String( "lines" ) ? context.hasSelection
                                                                        && context.wholeLines
                               : mode == QLatin1String( "any" )   ? context.hasSelection
                                                                  : false;
            if ( !holds ) {
                return false;
            }
        }
        else if ( key == QLatin1String( "min_lines" ) ) {
            if ( context.selectedLines < static_cast<qint64>( value.toDouble() ) ) {
                return false;
            }
        }
        else if ( key == QLatin1String( "max_lines" ) ) {
            if ( context.selectedLines > static_cast<qint64>( value.toDouble() ) ) {
                return false;
            }
        }
        else {
            return false; // unknown to this host: hidden, never shown out of place
        }
    }
    return true;
}

std::vector<PluginShortcut> manifestShortcuts( const QString& pluginId, const QString& pluginName,
                                               const QJsonObject& manifest )
{
    std::vector<PluginShortcut> shortcuts;
    PluginShortcut base;
    base.pluginId = pluginId;
    base.pluginName = pluginName;
    for ( const auto& value : manifest.value( "menus" ).toArray() ) {
        const auto menu = value.toObject();
        auto menuBase = base;
        menuBase.kind = QStringLiteral( "menu" );
        menuBase.menuId = menu.value( "id" ).toString();
        menuBase.location = menu.value( "location" ).toString();
        collectShortcuts( menu.value( "items" ).toArray(), menuBase, &shortcuts );
    }
    for ( const QString& kind : { QStringLiteral( "action" ), QStringLiteral( "view" ) } ) {
        const bool action = kind == QLatin1String( "action" );
        const auto list = manifest.value( action ? QStringLiteral( "actions" ) : QStringLiteral( "views" ) );
        for ( const auto& value : list.toArray() ) {
            const auto item = value.toObject();
            if ( !item.contains( "shortcut" ) ) {
                continue;
            }
            auto shortcut = base;
            shortcut.kind = kind;
            shortcut.itemId = item.value( "id" ).toString();
            shortcut.key = QStringLiteral( "plugin/%1/%2/%3" ).arg( pluginId, kind, shortcut.itemId );
            shortcut.title = item.value( action ? QStringLiteral( "label" ) : QStringLiteral( "title" ) )
                                 .toString( shortcut.itemId );
            shortcut.declared = shortcutKeys( item.value( "shortcut" ) );
            shortcuts.push_back( shortcut );
        }
    }
    return shortcuts;
}

std::vector<ShortcutAssignment> assignShortcuts( const std::vector<PluginShortcut>& shortcuts,
                                                 const std::map<QString, QString>& host,
                                                 const std::map<QString, QStringList>& user,
                                                 const std::map<QString, QString>& previous )
{
    std::vector<ShortcutAssignment> result( shortcuts.size() );
    std::map<QString, QString> claimed; // keys -> plugin shortcut that holds them
    const auto claim = [ & ]( size_t index, const QStringList& wanted ) {
        auto& assignment = result[ index ];
        QStringList refused;
        for ( const auto& keys : wanted ) {
            const auto owner = host.find( keys );
            if ( reservedShortcuts().contains( keys ) ) {
                refused.append( QStringLiteral( "%1 is reserved" ).arg( keys ) );
            }
            else if ( owner != host.end() ) {
                refused.append( QStringLiteral( "%1 is used by CILogg (%2)" ).arg( keys, owner->second ) );
            }
            else if ( claimed.count( keys ) != 0 ) {
                refused.append( QStringLiteral( "%1 is used by %2" ).arg( keys, claimed.at( keys ) ) );
            }
            else {
                claimed[ keys ] = shortcuts[ index ].key;
                assignment.keys.append( keys );
            }
        }
        assignment.reason = refused.join( QStringLiteral( "; " ) );
        assignment.state = !assignment.keys.isEmpty() ? QStringLiteral( "active" )
                           : !refused.isEmpty()       ? QStringLiteral( "refused" )
                           : assignment.userSet       ? QStringLiteral( "cleared" )
                                                      : QStringLiteral( "none" );
    };
    // The user's own choices first, then shortcuts that held their keys before, then the rest
    // in plugin name order.
    std::vector<size_t> order;
    for ( size_t index = 0; index < shortcuts.size(); ++index ) {
        result[ index ].key = shortcuts[ index ].key;
        const auto chosen = user.find( shortcuts[ index ].key );
        if ( chosen != user.end() ) {
            result[ index ].userSet = true;
            QStringList keys;
            for ( const auto& text : chosen->second ) {
                const auto normalized = normalizedShortcut( text );
                if ( !normalized.isEmpty() && !keys.contains( normalized ) ) {
                    keys.append( normalized );
                }
            }
            claim( index, keys );
        }
    }
    for ( int pass = 0; pass < 2; ++pass ) {
        for ( size_t index = 0; index < shortcuts.size(); ++index ) {
            if ( result[ index ].userSet ) {
                continue;
            }
            const auto& declared = shortcuts[ index ].declared;
            const bool heldBefore = std::any_of( declared.begin(), declared.end(), [ & ]( const auto& keys ) {
                const auto holder = previous.find( keys );
                return holder != previous.end() && holder->second == shortcuts[ index ].key;
            } );
            if ( heldBefore == ( pass == 0 ) ) {
                order.push_back( index );
            }
        }
    }
    for ( const auto index : order ) {
        claim( index, shortcuts[ index ].declared );
    }
    return result;
}
} // namespace cilogg::plugins
