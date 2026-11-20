// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginmenus.h"

#include "log.h"
#include "pluginfeatures.h"
#include "pluginicons.h"
#include "plugintablemodel.h"
#include "pluginviewhost.h"
#include <QAction>
#include <QActionGroup>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QWidget>
#include <memory>

namespace cp = cilogg::plugins;

namespace {
// Ids may contain any character: every part is length-prefixed.
QString menuKey( std::initializer_list<QString> parts )
{
    QString key;
    for ( const auto& part : parts ) {
        key += QString::number( part.size() ) + QLatin1Char( ':' ) + part;
    }
    return key;
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

// The cache key of a dynamic submenu's answer (cache_ms). An element menu's answer is about its
// target: another window, view, element, row, column or selection asks the plugin again.
QString populateKey( const QString& pluginId, const QString& menuId, const QString& itemId,
                     const PluginMenuContext& context )
{
    auto key = menuKey( { pluginId, menuId, itemId, context.location, context.tabId } );
    if ( context.location == QLatin1String( "element" ) ) {
        key += menuKey( { context.windowId, context.viewId, context.elementId, context.rowId,
                          context.columnId, context.selectedIds.join( QChar( 0x1f ) ) } );
    }
    return key;
}

void collectEntries( const QJsonArray& items, QList<QJsonObject>* entries )
{
    for ( const auto& value : items ) {
        const auto entry = value.toObject();
        entries->append( entry );
        if ( entry.value( "items" ).isArray() ) {
            collectEntries( entry.value( "items" ).toArray(), entries );
        }
    }
}

// The submenus above entry `itemId`, outermost first; false when the entry is not there.
bool entryPath( const QJsonArray& items, const QString& itemId, QList<QJsonObject>* path )
{
    for ( const auto& value : items ) {
        const auto entry = value.toObject();
        if ( entry.value( "id" ).toString() == itemId ) {
            return true;
        }
        if ( entry.value( "items" ).isArray() ) {
            path->append( entry );
            if ( entryPath( entry.value( "items" ).toArray(), itemId, path ) ) {
                return true;
            }
            path->removeLast();
        }
    }
    return false;
}

// The menus of `location` of one plugin, with their top-level entries in declaration order.
std::vector<std::pair<QJsonObject, QJsonObject>> topLevelEntries( const QJsonObject& manifest,
                                                                  const QString& location )
{
    std::vector<std::pair<QJsonObject, QJsonObject>> entries;
    for ( const auto& value : manifest.value( "menus" ).toArray() ) {
        const auto menu = value.toObject();
        if ( menu.value( "location" ).toString() != location ) {
            continue;
        }
        for ( const auto& item : menu.value( "items" ).toArray() ) {
            entries.emplace_back( menu, item.toObject() );
        }
    }
    return entries;
}

// QMenu::clear() deletes the menu's own actions but not its submenus.
void clearMenu( QMenu* menu )
{
    for ( auto* submenu : menu->findChildren<QMenu*>( QString{}, Qt::FindDirectChildrenOnly ) ) {
        if ( submenu->property( "ciloggPluginMenu" ).toBool() ) {
            submenu->deleteLater();
        }
    }
    menu->clear();
}

QString safeName( QString text )
{
    return text.replace( QLatin1Char( '/' ), QLatin1Char( '_' ) );
}
} // namespace

cp::MenuConditionContext PluginMenuContext::conditions() const
{
    cp::MenuConditionContext context;
    context.view = view;
    context.tabKind = tabKind;
    context.fileName = filePath.isEmpty() ? QString{} : QFileInfo( filePath ).fileName();
    context.hasSelection = selectedLines > 0;
    context.wholeLines = !portion;
    context.selectedLines = selectedLines;
    return context;
}

void PluginMenuContext::readText()
{
    if ( !textRead && text ) {
        pickedText = text();
    }
    textRead = true;
}

QJsonObject PluginMenuContext::toJson( bool withText ) const
{
    QJsonObject json{ { "location", location }, { "window_id", windowId } };
    if ( !tabId.isEmpty() ) {
        json.insert( "tab_id", tabId );
    }
    if ( !filePath.isEmpty() ) {
        json.insert( "file_path", filePath );
    }
    if ( !tabKind.isEmpty() ) {
        json.insert( "tab_kind", tabKind );
    }
    if ( !portName.isEmpty() ) {
        json.insert( "port_name", portName );
    }
    if ( paneIndex >= 0 ) {
        json.insert( "pane_index", paneIndex );
    }
    if ( !paneTabIds.isEmpty() ) {
        json.insert( "pane_tab_ids", QJsonArray::fromStringList( paneTabIds ) );
    }
    if ( !currentTabId.isEmpty() ) {
        json.insert( "current_tab_id", currentTabId );
    }
    if ( !view.isEmpty() ) {
        json.insert( "view", view );
    }
    if ( line ) {
        json.insert( "line", *line );
    }
    if ( location == QLatin1String( "log" ) ) {
        QJsonArray rangeList;
        for ( const auto& range : ranges ) {
            rangeList.append( QJsonArray{ range.first, range.second } );
        }
        QJsonObject selection{ { "ranges", rangeList }, { "count", selectedLines } };
        if ( withText ) {
            // At most 64 KiB of UTF-8, never a broken character at the cut.
            const auto bytes = pickedText.toUtf8();
            const bool truncated = bytes.size() > cp::MaxMenuSelectionTextBytes;
            auto sent = truncated ? QString::fromUtf8( bytes.left( cp::MaxMenuSelectionTextBytes ) )
                                  : pickedText;
            while ( truncated && sent.endsWith( QChar::ReplacementCharacter ) ) {
                sent.chop( 1 );
            }
            selection.insert( "text", sent );
            selection.insert( "truncated", truncated );
        }
        json.insert( "selection", selection );
    }
    if ( location == QLatin1String( "element" ) ) {
        // E17d (section 10.4): the view, the element, the clicked row and the selected rows.
        const auto orNull = []( const QString& value ) {
            return value.isEmpty() ? QJsonValue() : QJsonValue( value );
        };
        json.insert( "view_id", viewId );
        json.insert( "element_id", elementId );
        if ( tree ) {
            json.insert( "node_id", orNull( nodeId ) ); // E17e (section 28.3)
        }
        else {
            json.insert( "row_id", orNull( rowId ) );
            json.insert( "column_id", orNull( columnId ) );
        }
        json.insert( "selected_ids", QJsonArray::fromStringList( selectedIds ) );
        json.insert( "selected_count", selectedLines );
    }
    return json;
}

PluginMenuRegistry& PluginMenuRegistry::instance()
{
    static PluginMenuRegistry registry;
    return registry;
}

void PluginMenuRegistry::attach( cp::PluginManager* manager )
{
    if ( manager == nullptr || attached_.contains( manager ) ) {
        return;
    }
    attached_.insert( manager );
    connect( manager, &cp::PluginManager::menuUpdated, this, &PluginMenuRegistry::applyUpdate );
    connect( manager, &cp::PluginManager::pluginStopped, this, &PluginMenuRegistry::pluginStopped );
    connect( manager, &cp::PluginManager::viewsRevoked, this, &PluginMenuRegistry::forgetPlugin );
    connect( manager, &QObject::destroyed, this, [ this, manager ] { attached_.remove( manager ); } );
}

QJsonObject PluginMenuRegistry::entryState( const QString& pluginId, const QString& menuId,
                                            const QJsonObject& declared ) const
{
    auto state = declared;
    const auto changed
        = overrides_.find( menuKey( { pluginId, menuId, declared.value( "id" ).toString() } ) );
    if ( changed != overrides_.end() ) {
        for ( auto it = changed->second.begin(); it != changed->second.end(); ++it ) {
            state.insert( it.key(), it.value() );
        }
    }
    return state;
}

void PluginMenuRegistry::applyUpdate( const QString& pluginId, const QString& menuId,
                                      const QJsonArray& changes )
{
    // The declared menu, for the radio groups of checked entries.
    QJsonObject definition;
    for ( auto* manager : std::as_const( attached_ ) ) {
        for ( const auto& source : manager->menuSources() ) {
            if ( source.id == pluginId ) {
                definition = cp::findMenu( source.manifest, menuId );
            }
        }
    }
    for ( const auto& value : changes ) {
        const auto change = value.toObject();
        const auto itemId = change.value( "id" ).toString();
        auto& stored = overrides_[ menuKey( { pluginId, menuId, itemId } ) ];
        const auto set = change.value( "set" ).toObject();
        for ( auto it = set.begin(); it != set.end(); ++it ) {
            stored.insert( it.key(), it.value() );
        }
        // A radio entry checked by the plugin unchecks the rest of its group, as a click does.
        const auto group = cp::findMenuEntry( definition, itemId ).value( "group" ).toString();
        if ( set.value( "checked" ).toBool() && !group.isEmpty() ) {
            QList<QJsonObject> entries;
            collectEntries( definition.value( "items" ).toArray(), &entries );
            for ( const auto& other : entries ) {
                if ( other.value( "group" ).toString() == group
                     && other.value( "id" ).toString() != itemId ) {
                    overrides_[ menuKey( { pluginId, menuId, other.value( "id" ).toString() } ) ]
                        .insert( "checked", false );
                }
            }
        }
    }
    Q_EMIT changed( pluginId );
}

void PluginMenuRegistry::setChecked( const QString& pluginId, const QJsonObject& menu,
                                     const QString& itemId, bool checked )
{
    const auto menuId = menu.value( "id" ).toString();
    const auto entry = cp::findMenuEntry( menu, itemId );
    const auto group = entry.value( "group" ).toString();
    if ( checked && !group.isEmpty() ) {
        QList<QJsonObject> entries;
        collectEntries( menu.value( "items" ).toArray(), &entries );
        for ( const auto& other : entries ) {
            if ( other.value( "group" ).toString() == group
                 && other.value( "id" ).toString() != itemId ) {
                overrides_[ menuKey( { pluginId, menuId, other.value( "id" ).toString() } ) ].insert(
                    "checked", false );
            }
        }
    }
    overrides_[ menuKey( { pluginId, menuId, itemId } ) ].insert( "checked", checked );
    Q_EMIT changed( pluginId );
}

void PluginMenuRegistry::pluginStopped( const QString& pluginId )
{
    const auto prefix = menuKey( { pluginId } );
    for ( auto it = overrides_.begin(); it != overrides_.end(); ) {
        if ( !it->first.startsWith( prefix ) ) {
            ++it;
            continue;
        }
        const auto checked = it->second.value( "checked" );
        it->second = checked.isUndefined() ? QJsonObject{} : QJsonObject{ { "checked", checked } };
        it = it->second.isEmpty() ? overrides_.erase( it ) : std::next( it );
    }
    for ( auto& [ key, answer ] : answers_ ) {
        if ( key.startsWith( prefix ) ) {
            answer.expires = 0; // the last answer stays for native menu bars
        }
    }
    Q_EMIT changed( pluginId );
}

void PluginMenuRegistry::forgetPlugin( const QString& pluginId )
{
    const auto prefix = menuKey( { pluginId } );
    for ( auto it = overrides_.begin(); it != overrides_.end(); ) {
        it = it->first.startsWith( prefix ) ? overrides_.erase( it ) : std::next( it );
    }
    for ( auto it = answers_.begin(); it != answers_.end(); ) {
        it = it->first.startsWith( prefix ) ? answers_.erase( it ) : std::next( it );
    }
    Q_EMIT changed( pluginId );
}

std::optional<QJsonArray> PluginMenuRegistry::answer( const QString& key, bool lastAnswer ) const
{
    const auto it = answers_.find( key );
    if ( it == answers_.end() || ( !lastAnswer && it->second.expires <= nowMs() ) ) {
        return std::nullopt;
    }
    return it->second.items;
}

void PluginMenuRegistry::storeAnswer( const QString& key, const QJsonArray& items, qint64 cacheMs )
{
    constexpr size_t MaxAnswers = 1024;
    if ( answers_.size() >= MaxAnswers && answers_.count( key ) == 0 ) {
        answers_.erase( answers_.begin() );
    }
    answers_[ key ] = { items, nowMs() + qBound( qint64( 0 ), cacheMs,
                                                 qint64( cp::MaxDynamicMenuCacheMs ) ) };
}

void PluginMenuRegistry::assignShortcuts( const std::vector<cp::PluginShortcut>& shortcuts,
                                          const std::map<QString, QString>& host,
                                          const std::map<QString, QStringList>& user )
{
    assignments_ = cp::assignShortcuts( shortcuts, host, user, holders_ );
    shortcuts_ = shortcuts;
    holders_.clear();
    for ( const auto& assignment : assignments_ ) {
        for ( const auto& keys : assignment.keys ) {
            holders_[ keys ] = assignment.key;
        }
    }
}

QStringList PluginMenuRegistry::effectiveKeys( const QString& shortcutKey ) const
{
    for ( const auto& assignment : assignments_ ) {
        if ( assignment.key == shortcutKey ) {
            return assignment.keys;
        }
    }
    return {};
}

std::map<QString, QVariantList> PluginMenuRegistry::shortcutReport() const
{
    std::map<QString, QVariantList> report;
    for ( size_t index = 0; index < shortcuts_.size() && index < assignments_.size(); ++index ) {
        const auto& shortcut = shortcuts_[ index ];
        const auto& assignment = assignments_[ index ];
        report[ shortcut.pluginId ].append( QVariantMap{ { "key", shortcut.key },
                                                         { "kind", shortcut.kind },
                                                         { "menuId", shortcut.menuId },
                                                         { "itemId", shortcut.itemId },
                                                         { "title", shortcut.title },
                                                         { "declared", shortcut.declared },
                                                         { "keys", assignment.keys },
                                                         { "state", assignment.state },
                                                         { "reason", assignment.reason },
                                                         { "userSet", assignment.userSet } } );
    }
    return report;
}

PluginMenus::PluginMenus( QWidget* window, cp::PluginManager* manager, PluginViewHost* views,
                          ActionRunner runAction, ContextProvider currentContext )
    : QObject( window )
    , window_( window )
    , manager_( manager )
    , views_( views )
    , runAction_( std::move( runAction ) )
    , currentContext_( std::move( currentContext ) )
{
    PluginMenuRegistry::instance().attach( manager );
}

QString PluginMenus::menuText( QString text )
{
    // `&` would be a mnemonic and a tab a fake shortcut column; control characters go too.
    for ( auto& character : text ) {
        if ( character.unicode() < 0x20 || character.unicode() == 0x7f ) {
            character = QLatin1Char( ' ' );
        }
    }
    return text.replace( QLatin1Char( '&' ), QStringLiteral( "&&" ) );
}

QString PluginMenus::entryObjectName( const QString& pluginId, const QString& menuId,
                                      const QString& itemId )
{
    return QStringLiteral( "pluginMenuItem_%1_%2_%3" ).arg( pluginId, menuId, itemId );
}

QString PluginMenus::menuObjectName( const QString& pluginId, const QString& menuId,
                                     const QString& itemId )
{
    return itemId.isEmpty() ? QStringLiteral( "pluginMenu_%1_%2" ).arg( pluginId, menuId )
                            : QStringLiteral( "pluginMenu_%1_%2_%3" ).arg( pluginId, menuId, itemId );
}

std::optional<PluginMenus::Source> PluginMenus::source( const QString& pluginId ) const
{
    if ( !manager_ ) {
        return std::nullopt;
    }
    for ( const auto& plugin : manager_->menuSources() ) {
        if ( plugin.id == pluginId ) {
            return plugin;
        }
    }
    return std::nullopt;
}

bool PluginMenus::hidden( const Source& plugin, const QString& location ) const
{
    return plugin.hidden.contains( location );
}

QIcon PluginMenus::iconFor( const Source& plugin, const QJsonValue& reference ) const
{
    return PluginIconRegistry::instance().icon( plugin.id, plugin.directory, reference );
}

QString PluginMenus::unavailableReason( const Source& plugin, const QJsonObject& entry ) const
{
    if ( cp::menuEntryKind( entry ) == QLatin1String( "event" )
         && !cp::handledMethods( plugin.manifest.value( "handles" ) )
                 .contains( QStringLiteral( "ui/command" ) ) ) {
        return tr( "This plugin needs a newer SDK" );
    }
    return {};
}

QList<QAction*> PluginMenus::addSection( QMenu* menu, QAction* before,
                                         const PluginMenuContext& context )
{
    QList<QAction*> added;
    if ( !manager_ || menu == nullptr ) {
        return added;
    }
    // QMenu::clear() leaves submenus of an earlier opening behind: delete them now.
    for ( auto* stale : menu->findChildren<QMenu*>( QString{}, Qt::FindDirectChildrenOnly ) ) {
        if ( stale->property( "ciloggPluginMenu" ).toBool()
             && !menu->actions().contains( stale->menuAction() ) ) {
            stale->deleteLater();
        }
    }
    const auto separator = [ &added, menu, before ] {
        auto* line = new QAction( menu );
        line->setSeparator( true );
        menu->insertAction( before, line );
        added.append( line );
    };
    for ( const auto& plugin : manager_->menuSources() ) {
        if ( hidden( plugin, context.location ) ) {
            continue;
        }
        const auto entries = topLevelEntries( plugin.manifest, context.location );
        if ( entries.empty() ) {
            continue;
        }
        separator();
        QMenu* more = nullptr;
        int shown = 0;
        std::map<QString, QActionGroup*> groups;
        for ( const auto& [ definition, item ] : entries ) {
            QMenu* target = menu;
            QAction* position = before;
            if ( shown >= cp::MaxMenuTopLevelEntries && item.value( "type" ).toString( "item" )
                                                            == QLatin1String( "item" ) ) {
                if ( more == nullptr ) {
                    more = new QMenu( tr( "More" ), menu );
                    more->setProperty( "ciloggPluginMenu", true );
                    more->setObjectName( QStringLiteral( "pluginMenuMore_%1_%2" )
                                             .arg( plugin.id, context.location ) );
                    more->setToolTipsVisible( true );
                    menu->insertAction( before, more->menuAction() );
                    added.append( more->menuAction() );
                }
                target = more;
                position = nullptr;
            }
            auto* action = addEntry( target, position, plugin, definition, item, context, true, {},
                                     &groups );
            if ( action != nullptr && target == menu ) {
                added.append( action );
            }
            if ( action != nullptr && !action->isSeparator() ) {
                ++shown;
            }
        }
    }
    if ( !added.isEmpty() ) {
        menu->setToolTipsVisible( true );
    }
    LOG_DEBUG << "Plugin menu section location=" << context.location.toStdString()
              << " entries=" << added.size();
    return added;
}

void PluginMenus::removeSection( QMenu* menu, const QList<QAction*>& actions )
{
    for ( auto* action : actions ) {
        if ( action == nullptr ) {
            continue;
        }
        if ( menu != nullptr ) {
            menu->removeAction( action );
        }
        // A submenu owns its menu action: delete the submenu itself.
        if ( auto* submenu = action->menu(); submenu != nullptr && submenu->parent() == menu ) {
            submenu->deleteLater();
        }
        else {
            action->deleteLater();
        }
    }
}

void PluginMenus::fillOwnMenu( QMenu* menu, const Source& plugin, const QJsonObject& definition,
                               const PluginMenuContext& context )
{
    clearMenu( menu );
    menu->setToolTipsVisible( true );
    addEntries( menu, plugin, definition, definition.value( "items" ).toArray(), context, true,
                {} );
    if ( menu->isEmpty() ) {
        showUnavailable( menu, tr( "No entries" ) );
    }
}

void PluginMenus::addElementMenu( QMenu* menu, const QString& pluginId, const QString& menuId,
                                  const PluginMenuContext& context )
{
    const auto plugin = source( pluginId );
    if ( menu == nullptr || !plugin || hidden( *plugin, QStringLiteral( "element" ) ) ) {
        return;
    }
    const auto definition = cp::findMenu( plugin->manifest, menuId );
    if ( definition.value( "location" ) != QLatin1String( "element" ) ) {
        // Once per plugin and menu: the menu opens on every right-click. Bounded: a plugin that
        // keeps renaming its `context_menu` stops adding warnings after the first hundreds.
        static QSet<QString> warned;
        if ( warned.size() < 256 && !warned.contains( pluginId + '|' + menuId ) ) {
            warned.insert( pluginId + '|' + menuId );
            LOG_WARNING << "Plugin element menu not found or not an element menu plugin="
                        << pluginId.toStdString() << " menu=" << menuId.toStdString();
        }
        return;
    }
    // QMenu::clear() leaves submenus behind: an element menu is built anew each time.
    if ( !menu->isEmpty() ) {
        menu->addSeparator();
    }
    menu->setToolTipsVisible( true );
    addEntries( menu, *plugin, definition, definition.value( "items" ).toArray(), context, true,
                {} );
}

void PluginMenus::addEntries( QMenu* menu, const Source& plugin, const QJsonObject& definition,
                              const QJsonArray& items, const PluginMenuContext& context,
                              bool declared, const QString& idPrefix )
{
    std::map<QString, QActionGroup*> groups;
    for ( const auto& value : items ) {
        addEntry( menu, nullptr, plugin, definition, value.toObject(), context, declared, idPrefix,
                  &groups );
    }
}

QAction* PluginMenus::addEntry( QMenu* menu, QAction* before, const Source& plugin,
                                const QJsonObject& definition, const QJsonObject& item,
                                const PluginMenuContext& context, bool declared,
                                const QString& idPrefix, std::map<QString, QActionGroup*>* groups )
{
    const auto menuId = definition.value( "id" ).toString();
    auto& registry = PluginMenuRegistry::instance();
    const auto entry = declared ? registry.entryState( plugin.id, menuId, item ) : item;
    const auto kind = cp::menuEntryKind( entry );
    if ( kind == QLatin1String( "separator" ) || kind == QLatin1String( "section" ) ) {
        auto* line = new QAction( menu );
        line->setSeparator( true );
        line->setText( menuText( entry.value( "title" ).toString() ) );
        menu->insertAction( before, line );
        return line;
    }
    if ( !entry.value( "visible" ).toBool( true )
         || !cp::menuConditionsHold( entry.value( "when" ), context.conditions() ) ) {
        return nullptr;
    }
    const auto itemId = entry.value( "id" ).toString();
    const auto objectId = idPrefix + itemId;
    // Plugin strings are plain text: no mnemonics, no shortcut column, no rich text.
    const auto title = menuText( entry.value( "title" ).toString() );
    const auto icon = entry.contains( "icon" ) ? iconFor( plugin, entry.value( "icon" ) ) : QIcon{};
    const auto tooltip = pluginview::plainToolTip(
        entry.value( "tooltip" ).toString( entry.value( "title" ).toString() ) );
    if ( kind == QLatin1String( "submenu" ) || kind == QLatin1String( "dynamic" ) ) {
        auto* submenu = new QMenu( title, menu );
        submenu->setProperty( "ciloggPluginMenu", true );
        submenu->setObjectName( menuObjectName( plugin.id, menuId, objectId ) );
        submenu->setIcon( icon );
        submenu->setToolTipsVisible( true );
        submenu->menuAction()->setToolTip( tooltip );
        submenu->setEnabled( entry.value( "enabled" ).toBool( true ) );
        if ( declared ) {
            // menu/update while the menu is open changes the submenu in place (design 7.4).
            connect( &registry, &PluginMenuRegistry::changed, submenu,
                     [ guarded = QPointer<QMenu>( submenu ), pluginId = plugin.id, menuId,
                       item ]( const QString& changedPlugin ) {
                         if ( !guarded || changedPlugin != pluginId ) {
                             return;
                         }
                         const auto now
                             = PluginMenuRegistry::instance().entryState( pluginId, menuId, item );
                         guarded->setTitle( menuText( now.value( "title" ).toString() ) );
                         guarded->setEnabled( now.value( "enabled" ).toBool( true ) );
                         guarded->menuAction()->setVisible( now.value( "visible" ).toBool( true ) );
                     } );
        }
        menu->insertAction( before, submenu->menuAction() );
        if ( kind == QLatin1String( "submenu" ) ) {
            addEntries( submenu, plugin, definition, entry.value( "items" ).toArray(), context,
                        declared, idPrefix );
        }
        else {
            const QPointer<QMenu> guarded( submenu );
            connect( submenu, &QMenu::aboutToShow, this,
                     [ this, guarded, plugin, definition, itemId, context ] {
                         populate( guarded, plugin, definition, itemId, context );
                     } );
        }
        return submenu->menuAction();
    }
    auto* action = new QAction( icon, title, menu );
    action->setObjectName( entryObjectName( plugin.id, menuId, objectId ) );
    action->setToolTip( tooltip );
    action->setEnabled( entry.value( "enabled" ).toBool( true ) );
    const auto reason = unavailableReason( plugin, entry );
    if ( !reason.isEmpty() ) {
        action->setEnabled( false );
        action->setToolTip( reason );
    }
    const bool checkable = entry.value( "checkable" ).toBool();
    if ( checkable ) {
        action->setCheckable( true );
        action->setChecked( entry.value( "checked" ).toBool() );
        const auto group = entry.value( "group" ).toString();
        if ( !group.isEmpty() && groups != nullptr ) {
            auto& actionGroup = ( *groups )[ group ];
            if ( actionGroup == nullptr ) {
                // Owned by the first entry, so the group goes with the rebuilt entries.
                actionGroup = new QActionGroup( action );
            }
            actionGroup->addAction( action );
        }
    }
    if ( declared ) {
        // The shortcut is shown in the menu; it fires through the window's QShortcut.
        QList<QKeySequence> keys;
        const auto key = QStringLiteral( "plugin/%1/%2/%3" ).arg( plugin.id, menuId, itemId );
        for ( const auto& text : registry.effectiveKeys( key ) ) {
            keys.append( QKeySequence::fromString( text, QKeySequence::PortableText ) );
        }
        action->setShortcuts( keys );
        action->setShortcutContext( Qt::WidgetShortcut );
    }
    if ( declared ) {
        // menu/update while the menu is open changes the entry in place (design 7.4).
        connect( &registry, &PluginMenuRegistry::changed, action,
                 [ self = QPointer<PluginMenus>( this ), action, plugin, menuId,
                   item ]( const QString& changedPlugin ) {
                     if ( !self || changedPlugin != plugin.id ) {
                         return;
                     }
                     const auto now
                         = PluginMenuRegistry::instance().entryState( plugin.id, menuId, item );
                     const auto why = self->unavailableReason( plugin, now );
                     action->setText( menuText( now.value( "title" ).toString() ) );
                     action->setVisible( now.value( "visible" ).toBool( true ) );
                     action->setEnabled( now.value( "enabled" ).toBool( true ) && why.isEmpty() );
                     action->setToolTip( why.isEmpty()
                                             ? pluginview::plainToolTip( now.value( "tooltip" ).toString(
                                                   now.value( "title" ).toString() ) )
                                             : why );
                     if ( now.contains( "icon" ) ) {
                         action->setIcon( self->iconFor( plugin, now.value( "icon" ) ) );
                     }
                     if ( action->isCheckable() ) {
                         const QSignalBlocker blocker( action );
                         action->setChecked( now.value( "checked" ).toBool() );
                     }
                 } );
    }
    connect( action, &QAction::triggered, this,
             [ this, plugin, definition, menuId, item, entry, context, checkable,
               declared ]( bool checked ) {
                 // The entry as it is now: a menu/update may have disabled or hidden it.
                 const auto now = declared
                                      ? PluginMenuRegistry::instance().entryState( plugin.id,
                                                                                   menuId, item )
                                      : entry;
                 if ( !now.value( "enabled" ).toBool( true ) || !now.value( "visible" ).toBool( true ) ) {
                     return;
                 }
                 auto picked = context;
                 picked.readText(); // the view and its selection are still there
                 activate( plugin, definition, now, picked, QStringLiteral( "menu" ),
                           checkable ? std::optional<bool>( checked ) : std::nullopt );
             } );
    menu->insertAction( before, action );
    return action;
}

void PluginMenus::showUnavailable( QMenu* menu, const QString& text )
{
    if ( menu == nullptr ) {
        return;
    }
    clearMenu( menu );
    auto* entry = menu->addAction( text );
    entry->setObjectName( QStringLiteral( "pluginMenuUnavailable" ) );
    entry->setEnabled( false );
}

void PluginMenus::populate( QPointer<QMenu> menu, const Source& plugin,
                            const QJsonObject& definition, const QString& itemId,
                            const PluginMenuContext& context )
{
    if ( !menu || !manager_ ) {
        return;
    }
    clearMenu( menu );
    const auto menuId = definition.value( "id" ).toString();
    const auto key = populateKey( plugin.id, menuId, itemId, context );
    auto& registry = PluginMenuRegistry::instance();
    const auto generation = ++populateGeneration_;
    menu->setProperty( "pluginPopulateGeneration", generation );
    const auto fill = [ this, plugin, definition, itemId, context ]( QMenu* target,
                                                                     const QJsonArray& items ) {
        clearMenu( target );
        addEntries( target, plugin, definition, items, context, false, itemId + '_' );
        if ( target->isEmpty() ) {
            showUnavailable( target, tr( "No entries" ) );
        }
    };
    if ( const auto cached = registry.answer( key, false ) ) {
        fill( menu, *cached );
        return;
    }
    if ( !cp::handledMethods( plugin.manifest.value( "handles" ) )
              .contains( QStringLiteral( "menu/populate" ) ) ) {
        showUnavailable( menu, tr( "Not available" ) );
        return;
    }
    // Native menu bars do not change while open: the last answer now, a new one next time.
    const bool nativeBar = context.location == QLatin1String( "plugins" )
                           || context.location == QLatin1String( "menubar" );
    if ( const auto last = nativeBar ? registry.answer( key, true ) : std::nullopt ) {
        fill( menu, *last );
        sendPopulate( {}, plugin, definition, itemId, context, generation );
        return;
    }
    auto* loading = menu->addAction( tr( "Loading…" ) );
    loading->setObjectName( QStringLiteral( "pluginMenuLoading" ) );
    loading->setEnabled( false );
    sendPopulate( menu, plugin, definition, itemId, context, generation );
}

void PluginMenus::sendPopulate( QPointer<QMenu> menu, const Source& plugin,
                                const QJsonObject& definition, const QString& itemId,
                                const PluginMenuContext& context, quint64 generation )
{
    if ( !manager_ ) {
        return;
    }
    const auto menuId = definition.value( "id" ).toString();
    const auto key = populateKey( plugin.id, menuId, itemId, context );
    const QPointer<PluginMenus> self( this );
    const auto current = [ menu, generation ] {
        return menu && menu->property( "pluginPopulateGeneration" ).toULongLong() == generation;
    };
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    const auto send = [ self, menu, plugin, definition, itemId, context, key, current, clock ] {
        if ( !self || !self->manager_ ) {
            return;
        }
        const QJsonObject params{ { "menu_id", definition.value( "id" ).toString() },
                                  { "item_id", itemId },
                                  { "location", context.location },
                                  { "window_id", context.windowId },
                                  { "context", context.toJson( false ) } };
        // 10 seconds in all for a plugin that had to start (design 7.4).
        const auto remaining = static_cast<int>( cp::MaxDynamicStartMs - clock->elapsed() );
        const auto timeout
            = qBound( 1, remaining, cp::queryTimeouts( QStringLiteral( "menu/populate" ) ).defaultMs );
        QString reason;
        const auto id = self->manager_->query(
            plugin.id, QStringLiteral( "menu/populate" ), params, timeout,
            QStringLiteral( "menu:" ) + context.windowId, QStringLiteral( "menu:" ) + context.windowId,
            [ self, menu, key, current, plugin, definition, itemId, context ](
                const cp::PluginQueries::Outcome& outcome ) {
                const bool answered = outcome.status == cp::PluginQueries::Status::Answered
                                      || outcome.status == cp::PluginQueries::Status::Nothing;
                const auto answer = outcome.result.toObject();
                const auto items = answer.value( "items" ).toArray();
                if ( answered ) {
                    PluginMenuRegistry::instance().storeAnswer(
                        key, items, static_cast<qint64>( answer.value( "cache_ms" ).toDouble() ) );
                }
                if ( !self || !current() ) {
                    return;
                }
                if ( !answered ) {
                    showUnavailable( menu, tr( "Not available" ) );
                    return;
                }
                clearMenu( menu );
                self->addEntries( menu, plugin, definition, items, context, false, itemId + '_' );
                if ( menu->isEmpty() ) {
                    showUnavailable( menu, tr( "No entries" ) );
                }
            },
            &reason );
        if ( id.isEmpty() && current() ) {
            LOG_DEBUG << "Plugin menu/populate not sent plugin=" << plugin.id.toStdString()
                      << " reason=" << reason.toStdString();
            showUnavailable( menu, tr( "Not available" ) );
        }
    };
    if ( manager_->pluginState( plugin.id ) == QLatin1String( "ready" ) ) {
        send();
        return;
    }
    // Opening a dynamic submenu starts a stopped plugin, as clicking its actions does.
    const auto started = manager_->startPlugin( plugin.id );
    if ( !started.ok() ) {
        if ( current() ) {
            showUnavailable( menu, tr( "Not available" ) );
        }
        return;
    }
    if ( started.payload.value( "state" ).toString() == QLatin1String( "ready" ) ) {
        send();
        return;
    }
    auto done = std::make_shared<bool>( false );
    auto connection = std::make_shared<QMetaObject::Connection>();
    const auto pluginId = plugin.id;
    // A refresh for a native menu bar has no open menu and only fills the cache.
    const bool refreshOnly = menu.isNull();
    *connection = connect( manager_, &cp::PluginManager::pluginReady, this,
                           [ done, connection, pluginId, send, refreshOnly,
                             current ]( const QString& readyId ) {
                               if ( readyId != pluginId || *done ) {
                                   return;
                               }
                               *done = true;
                               QObject::disconnect( *connection );
                               if ( refreshOnly || current() ) {
                                   send(); // not for a menu that closed or opened again meanwhile
                               }
                           } );
    QTimer::singleShot( cp::MaxDynamicStartMs, this, [ done, connection, current, menu ] {
        if ( *done ) {
            return;
        }
        *done = true;
        QObject::disconnect( *connection );
        if ( current() ) {
            showUnavailable( menu, tr( "Not available" ) );
        }
    } );
}

void PluginMenus::activate( const Source& plugin, const QJsonObject& definition,
                            const QJsonObject& entry, PluginMenuContext context,
                            const QString& origin, std::optional<bool> checked )
{
    // After the menu closed: an action form is modal and the tab may move meanwhile (design 7.8).
    QTimer::singleShot( 0, this,
                        [ this, plugin, definition, entry, picked = std::move( context ), origin,
                          checked ] {
                            const auto result
                                = dispatch( plugin, definition, entry, picked, origin, checked, true );
                            if ( !result.ok() && window_ ) {
                                // The reason may come from the plugin: plain text only.
                                QMessageBox box( QMessageBox::Warning, tr( "Plugins" ),
                                                 result.message, QMessageBox::Ok, window_ );
                                box.setTextFormat( Qt::PlainText );
                                box.exec();
                            }
                        } );
}

CommanderResult PluginMenus::dispatch( const Source& plugin, const QJsonObject& definition,
                                       const QJsonObject& entry, const PluginMenuContext& context,
                                       const QString& origin, std::optional<bool> checked,
                                       bool interactive )
{
    if ( !manager_ ) {
        return commanderFailure( CommanderResultCode::ExecutionFailed,
                                 tr( "Plugins are not available." ) );
    }
    const auto menuId = definition.value( "id" ).toString();
    const auto itemId = entry.value( "id" ).toString();
    const auto kind = cp::menuEntryKind( entry );
    const auto reason = unavailableReason( plugin, entry );
    if ( !reason.isEmpty() ) {
        return commanderFailure( CommanderResultCode::InvalidRequest, reason );
    }
    if ( !entry.value( "enabled" ).toBool( true ) ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "The menu entry is disabled." ) );
    }
    // The check mark changes only when the entry did its work (a cancelled form keeps it).
    const auto commit = [ & ]( CommanderResult result ) {
        if ( result.ok() && !result.payload.value( "cancelled" ).toBool()
             && entry.value( "checkable" ).toBool() && checked
             && !cp::findMenuEntry( definition, itemId ).isEmpty() ) {
            PluginMenuRegistry::instance().setChecked( plugin.id, definition, itemId, *checked );
        }
        return result;
    };
    LOG_DEBUG << "Plugin menu entry plugin=" << plugin.id.toStdString()
              << " menu=" << menuId.toStdString() << " item=" << itemId.toStdString()
              << " kind=" << kind.toStdString() << " origin=" << origin.toStdString()
              << " location=" << context.location.toStdString();
    const QVariantMap ids{ { "pluginId", plugin.id },
                           { "menuId", menuId },
                           { "itemId", itemId },
                           { "kind", kind },
                           { "location", context.location } };
    if ( kind == QLatin1String( "action" ) ) {
        QJsonObject action;
        for ( const auto& value : plugin.manifest.value( "actions" ).toArray() ) {
            if ( value.toObject().value( "id" ) == entry.value( "action" ) ) {
                action = value.toObject();
            }
        }
        QVariantMap menu{ { "location", context.location },
                          { "menu_id", menuId },
                          { "item_id", itemId } };
        if ( context.line ) {
            menu.insert( "line", *context.line );
        }
        const auto json = context.toJson( context.location == QLatin1String( "log" ) );
        if ( json.contains( "selection" ) ) {
            menu.insert( "selection", json.value( "selection" ).toVariant() );
        }
        if ( context.location == QLatin1String( "element" ) ) {
            // E17d: the job's `menu` context names the view element and its rows.
            for ( const auto* key : { "view_id", "element_id", "row_id", "column_id",
                                      "selected_ids", "selected_count" } ) {
                menu.insert( QLatin1String( key ), json.value( QLatin1String( key ) ).toVariant() );
            }
        }
        if ( menuId.isEmpty() ) {
            menu.clear(); // an action's own shortcut: no menu entry
        }
        auto result = runAction_( plugin.id, action, context, menu,
                                  entry.value( "arguments" ).toObject(), interactive );
        if ( result.ok() ) {
            result.payload.insert( ids );
        }
        return commit( result );
    }
    if ( kind == QLatin1String( "view" ) ) {
        if ( !views_ ) {
            return commanderFailure( CommanderResultCode::ExecutionFailed,
                                     tr( "Plugin views are not available in this window." ) );
        }
        QJsonObject overrides;
        if ( !context.tabId.isEmpty() ) {
            overrides.insert( "source", QStringLiteral( "tab:" ) + context.tabId );
        }
        QString error;
        if ( !views_->openView( plugin.id, entry.value( "view" ).toString(), overrides, &error ) ) {
            return commanderFailure( CommanderResultCode::ExecutionFailed, error );
        }
        auto payload = ids;
        payload.insert( "viewId", entry.value( "view" ).toString() );
        return commit( commanderSuccess( {}, payload ) );
    }
    if ( kind == QLatin1String( "event" ) ) {
        QJsonObject params{ { "origin", origin },
                            { "menu_id", menuId },
                            { "item_id", itemId },
                            { "location", context.location },
                            { "window_id", context.windowId },
                            { "context", context.toJson( true ) } };
        if ( entry.value( "checkable" ).toBool() ) {
            params.insert( "checked", checked.value_or( entry.value( "checked" ).toBool() ) );
        }
        const auto sent = manager_->sendCommand( plugin.id, params );
        if ( !sent.ok() ) {
            return commanderFailure( CommanderResultCode::ExecutionFailed, sent.error );
        }
        auto payload = ids;
        payload.insert( sent.payload );
        return commit( commanderSuccess( {}, payload ) );
    }
    return commanderFailure( CommanderResultCode::InvalidRequest,
                             tr( "This menu entry opens a submenu." ) );
}

QVariantList PluginMenus::resolve( const QString& location, const PluginMenuContext& context,
                                   const QString& pluginId ) const
{
    QVariantList result;
    if ( !manager_ ) {
        return result;
    }
    auto& registry = PluginMenuRegistry::instance();
    const auto conditions = context.conditions();
    for ( const auto& plugin : manager_->menuSources() ) {
        if ( !pluginId.isEmpty() && plugin.id != pluginId ) {
            continue;
        }
        for ( const auto& value : plugin.manifest.value( "menus" ).toArray() ) {
            const auto definition = value.toObject();
            if ( definition.value( "location" ).toString() != location ) {
                continue;
            }
            const auto menuId = definition.value( "id" ).toString();
            // E17d: an element shows only the menu its `context_menu` names.
            if ( !context.elementMenuId.isEmpty() && menuId != context.elementMenuId ) {
                continue;
            }
            const bool hiddenHere = hidden( plugin, location );
            // Depth-first, with the titles of the submenus above each entry.
            std::function<void( const QJsonArray&, const QStringList&, bool )> walk;
            walk = [ & ]( const QJsonArray& items, const QStringList& path, bool parentShown ) {
                for ( const auto& itemValue : items ) {
                    const auto entry
                        = registry.entryState( plugin.id, menuId, itemValue.toObject() );
                    const auto kind = cp::menuEntryKind( entry );
                    if ( kind == QLatin1String( "separator" ) ) {
                        continue;
                    }
                    const bool conditionsHold
                        = cp::menuConditionsHold( entry.value( "when" ), conditions );
                    const bool visible = entry.value( "visible" ).toBool( true );
                    const bool shown = parentShown && visible && conditionsHold
                                       && ( location == QLatin1String( "menubar" ) || !hiddenHere );
                    const auto itemId = entry.value( "id" ).toString();
                    const auto shortcutKey
                        = QStringLiteral( "plugin/%1/%2/%3" ).arg( plugin.id, menuId, itemId );
                    QVariantMap row{
                        { "pluginId", plugin.id },
                        { "pluginName", plugin.name },
                        { "menuId", menuId },
                        { "menuTitle", definition.value( "title" ).toString( plugin.name ) },
                        { "itemId", itemId },
                        { "kind", kind },
                        { "title", entry.value( "title" ).toString() },
                        { "path", path },
                        { "tooltip", entry.value( "tooltip" ).toString() },
                        { "enabled", entry.value( "enabled" ).toBool( true )
                                         && unavailableReason( plugin, entry ).isEmpty() },
                        { "visible", visible },
                        { "checkable", entry.value( "checkable" ).toBool() },
                        { "checked", entry.value( "checked" ).toBool() },
                        { "group", entry.value( "group" ).toString() },
                        { "when", entry.value( "when" ).toObject().toVariantMap() },
                        { "conditionsHold", conditionsHold },
                        { "shown", shown },
                        { "unavailable", unavailableReason( plugin, entry ) },
                        { "shortcut", registry.effectiveKeys( shortcutKey ) },
                        { "declaredShortcut", cp::shortcutKeys( entry.value( "shortcut" ) ) },
                    };
                    if ( location == QLatin1String( "menubar" ) ) {
                        row.insert( "movedToPlugins", hiddenHere );
                    }
                    if ( kind != QLatin1String( "section" ) ) {
                        result.append( row );
                    }
                    if ( entry.value( "items" ).isArray() ) {
                        walk( entry.value( "items" ).toArray(),
                              path + QStringList{ entry.value( "title" ).toString() }, shown );
                    }
                }
            };
            walk( definition.value( "items" ).toArray(), {}, true );
        }
    }
    return result;
}

std::optional<PluginMenus::Picked> PluginMenus::pick( const QString& pluginId,
                                                     const QString& menuId,
                                                     const QString& itemId,
                                                     PluginMenuContext context,
                                                     std::optional<bool> checked,
                                                     CommanderResult* failure ) const
{
    const auto refuse = [ failure ]( CommanderResultCode code, const QString& message ) {
        *failure = commanderFailure( code, message );
        return std::nullopt;
    };
    const auto plugin = source( pluginId );
    if ( !plugin ) {
        return refuse( CommanderResultCode::NotFound,
                       tr( "The plugin is not installed, enabled and valid." ) );
    }
    const auto definition = cp::findMenu( plugin->manifest, menuId );
    const auto declared = cp::findMenuEntry( definition, itemId );
    if ( declared.isEmpty() ) {
        return refuse( CommanderResultCode::NotFound,
                       tr( "The plugin declares no such menu entry." ) );
    }
    const auto location = definition.value( "location" ).toString();
    if ( !context.location.isEmpty() && context.location != location ) {
        return refuse( CommanderResultCode::InvalidRequest,
                       tr( "The entry belongs to the %1 menus." ).arg( location ) );
    }
    if ( !context.elementMenuId.isEmpty() && menuId != context.elementMenuId ) {
        return refuse( CommanderResultCode::InvalidRequest,
                       tr( "The element's context menu is %1." ).arg( context.elementMenuId ) );
    }
    if ( location == QLatin1String( "element" ) && context.elementId.isEmpty() ) {
        return refuse( CommanderResultCode::InvalidRequest,
                       tr( "An element menu entry needs --view-id and --element-id." ) );
    }
    context.location = location;
    const auto entry = PluginMenuRegistry::instance().entryState( pluginId, menuId, declared );
    const auto kind = cp::menuEntryKind( entry );
    if ( kind == QLatin1String( "submenu" ) || kind == QLatin1String( "dynamic" ) ) {
        return refuse( CommanderResultCode::InvalidRequest, tr( "This menu entry opens a submenu." ) );
    }
    if ( !entry.value( "visible" ).toBool( true )
         || !cp::menuConditionsHold( entry.value( "when" ), context.conditions() ) ) {
        return refuse( CommanderResultCode::InvalidRequest,
                       tr( "The menu entry is not shown in this context." ) );
    }
    // The submenus above it too: a user can reach the entry only through every one of them.
    QList<QJsonObject> ancestors;
    entryPath( definition.value( "items" ).toArray(), itemId, &ancestors );
    for ( const auto& declaredParent : std::as_const( ancestors ) ) {
        const auto parent
            = PluginMenuRegistry::instance().entryState( pluginId, menuId, declaredParent );
        if ( !parent.value( "visible" ).toBool( true )
             || !cp::menuConditionsHold( parent.value( "when" ), context.conditions() ) ) {
            return refuse( CommanderResultCode::InvalidRequest,
                           tr( "The menu entry is not shown in this context." ) );
        }
        if ( !parent.value( "enabled" ).toBool( true ) ) {
            return refuse( CommanderResultCode::InvalidRequest,
                           tr( "The menu entry is disabled." ) );
        }
    }
    if ( entry.value( "checkable" ).toBool() && !checked ) {
        // A click: radio entries become checked, check boxes toggle.
        checked = !entry.value( "group" ).toString().isEmpty() || !entry.value( "checked" ).toBool();
    }
    context.readText();
    return Picked{ *plugin, definition, entry, std::move( context ), checked };
}

CommanderResult PluginMenus::trigger( const QString& pluginId, const QString& menuId,
                                      const QString& itemId, PluginMenuContext context,
                                      std::optional<bool> checked )
{
    CommanderResult failure;
    const auto picked = pick( pluginId, menuId, itemId, std::move( context ), checked, &failure );
    if ( !picked ) {
        return failure;
    }
    return dispatch( picked->plugin, picked->definition, picked->entry, picked->context,
                     QStringLiteral( "menu" ), picked->checked, false );
}

void PluginMenus::registerShortcuts( const std::map<QString, QString>& host,
                                     const std::map<QString, QStringList>& user )
{
    for ( const auto& shortcut : shortcuts_ ) {
        if ( shortcut ) {
            // Off at once: an old and a new shortcut with the same keys would make both ambiguous.
            shortcut->setEnabled( false );
            shortcut->deleteLater();
        }
    }
    shortcuts_.clear();
    if ( !manager_ || !window_ ) {
        return;
    }
    std::vector<cp::PluginShortcut> declared;
    for ( const auto& plugin : manager_->menuSources() ) {
        const auto list = cp::manifestShortcuts( plugin.id, plugin.name, plugin.manifest );
        declared.insert( declared.end(), list.begin(), list.end() );
    }
    auto& registry = PluginMenuRegistry::instance();
    registry.assignShortcuts( declared, host, user );
    manager_->setShortcutReport( registry.shortcutReport() );
    const auto& assignments = registry.assignments();
    for ( size_t index = 0; index < declared.size() && index < assignments.size(); ++index ) {
        const auto shortcut = declared[ index ];
        for ( const auto& keys : assignments[ index ].keys ) {
            auto* object = new QShortcut( window_ );
            object->setKey( QKeySequence::fromString( keys, QKeySequence::PortableText ) );
            object->setContext( Qt::WindowShortcut );
            object->setObjectName( QStringLiteral( "pluginShortcut_" ) + safeName( shortcut.key ) );
            connect( object, &QShortcut::activated, this, [ this, shortcut ] {
                const auto plugin = source( shortcut.pluginId );
                if ( !plugin ) {
                    return;
                }
                if ( shortcut.kind == QLatin1String( "menu" ) ) {
                    // Like a click: hidden and disabled entries do nothing, an action shows its
                    // form, and a failure is reported.
                    auto context = currentContext_ ? currentContext_( shortcut.location )
                                                   : PluginMenuContext{};
                    context.location = shortcut.location;
                    CommanderResult failure;
                    const auto picked = pick( shortcut.pluginId, shortcut.menuId,
                                              shortcut.itemId, std::move( context ), std::nullopt,
                                              &failure );
                    if ( !picked || !picked->entry.value( "enabled" ).toBool( true ) ) {
                        LOG_DEBUG << "Plugin shortcut not applicable key="
                                  << shortcut.key.toStdString() << " reason="
                                  << failure.message.toStdString();
                        return;
                    }
                    activate( picked->plugin, picked->definition, picked->entry, picked->context,
                              QStringLiteral( "shortcut" ), picked->checked );
                    return;
                }
                auto context = currentContext_ ? currentContext_( QStringLiteral( "plugins" ) )
                                               : PluginMenuContext{};
                if ( shortcut.kind == QLatin1String( "view" ) ) {
                    QJsonObject entry{ { "id", shortcut.itemId }, { "view", shortcut.itemId } };
                    activate( *plugin, QJsonObject{ { "id", QString{} } }, entry, context,
                              QStringLiteral( "shortcut" ), std::nullopt );
                    return;
                }
                QJsonObject entry{ { "id", shortcut.itemId }, { "action", shortcut.itemId } };
                activate( *plugin, QJsonObject{ { "id", QString{} } }, entry, context,
                          QStringLiteral( "shortcut" ), std::nullopt );
            } );
            shortcuts_.emplace_back( object );
        }
    }
    LOG_DEBUG << "Plugin shortcuts registered count=" << shortcuts_.size()
              << " declared=" << declared.size();
}
