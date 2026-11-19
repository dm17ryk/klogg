// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <map>
#include <optional>
#include <vector>

// Plugin menus, context menus and keyboard shortcuts (docs/plugin-sdk-advanced.md section 7).
// Pure functions, shared by the manifest, the plugin manager and the UI.
namespace cilogg::plugins {
constexpr int MaxMenusPerPlugin = 16;
constexpr int MaxMenuEntries = 256; // all entries of all menus of one plugin
constexpr int MaxMenuDepth = 4;
constexpr int MaxMenuTitleChars = 128;
constexpr int MaxMenuIdChars = 128;
constexpr int MaxMenuTooltipChars = 4096;
constexpr int MaxMenuTopLevelEntries = 8; // per plugin and location, before "More"
constexpr int MaxDynamicMenuEntries = 256;
constexpr int MaxDynamicMenuCacheMs = 60000;
constexpr int MaxPluginShortcuts = 32;
constexpr int MaxMenuSelectionRanges = 64;
constexpr int MaxMenuSelectionTextBytes = 64 * 1024;
constexpr int MaxQueuedCommands = 16; // ui/command waiting for a plugin to start
constexpr int MaxMenuConditionValues = 32;
constexpr int MaxDynamicStartMs = 10000; // a dynamic submenu of a stopped plugin, in all

// Every `location` of the design, and the ones this host renders (E17b). A menu with another
// location is kept in the manifest but not shown (an older host never shows it out of place).
const QStringList& menuLocations();
const QStringList& renderedMenuLocations();

// Keys no plugin shortcut may take (section 7.6), in portable text.
const QStringList& reservedShortcuts();
// `shortcut`: a portable key sequence ("Ctrl+Alt+D") or a list of one or two. Empty means valid.
QString validateShortcut( const QJsonValue& shortcut );
// The key sequences of a valid `shortcut`, normalized to portable text.
QStringList shortcutKeys( const QJsonValue& shortcut );
QString normalizedShortcut( const QString& keys );

// The `id` of every object of a manifest list (`actions`, `views`).
QStringList declaredIds( const QJsonValue& list );
// Manifest `menus`. `actions` and `views` are the declared ids that `action` and `view` entries
// may name. Empty means valid.
QString validateMenus( const QJsonValue& menus, const QStringList& actions,
                       const QStringList& views );
// Every shortcut a manifest declares (menu entries, actions and views) counts toward the limit.
QString validateShortcutCount( const QJsonObject& manifest );

// One menu entry, found by id anywhere in a menu (submenus included).
QJsonObject findMenuEntry( const QJsonObject& menu, const QString& itemId );
// The declared menu `menuId` of a manifest, or an empty object.
QJsonObject findMenu( const QJsonObject& manifest, const QString& menuId );
// What an entry is: "separator", "section", "submenu", "dynamic", "action", "view" or "event".
QString menuEntryKind( const QJsonObject& entry );

// A `menu/populate` answer (section 7.4): `{items, cache_ms}` with entries of the declared schema
// without shortcuts, submenus or dynamic entries of their own beyond the depth limit. Returns the
// normalized answer; `error` is set for an invalid one. A null answer is an empty list.
QJsonObject sanitizeMenuPopulate( const QJsonValue& answer, const QStringList& actions,
                                  const QStringList& views, QString* error );

// `menu/update` (section 7.4): `{menu_id, items: [{id, set: {...}}]}`. Returns the changes this
// host applies (known keys of declared entries only); `error` is set when nothing is usable.
QJsonArray sanitizeMenuUpdate( const QJsonObject& manifest, const QJsonObject& params,
                               QString* error );

// What a `when` condition sees when a menu opens (section 7.3).
struct MenuConditionContext {
    QString view;     // "main" or "filtered"; empty outside the log views
    QString tabKind;  // "file", "serial" or "plugin_stream"; empty without a tab
    QString fileName; // the file name of the tab, without its folder
    bool hasSelection = false;
    bool wholeLines = false; // the selection covers whole lines (not a part of one line)
    qint64 selectedLines = 0;
};
// Whether every condition holds. A condition this host does not know hides the entry.
bool menuConditionsHold( const QJsonValue& when, const MenuConditionContext& context );

// One shortcut a plugin declares: `plugin/<plugin>/<menu>/<item>`, `plugin/<plugin>/action/<id>`
// or `plugin/<plugin>/view/<id>`.
struct PluginShortcut {
    QString key;      // the name in the shortcut list
    QString pluginId;
    QString pluginName;
    QString kind;     // "menu", "action" or "view"
    QString menuId;   // menu entries
    QString itemId;   // menu entries; the action or view id otherwise
    QString title;
    QString location; // the menu's location for menu entries
    QStringList declared;
};
std::vector<PluginShortcut> manifestShortcuts( const QString& pluginId, const QString& pluginName,
                                               const QJsonObject& manifest );

// The result of matching plugin shortcuts against CILogg's (section 7.6).
struct ShortcutAssignment {
    QString key;
    QStringList keys;   // the keys in effect (empty when refused or cleared)
    QString state;      // "active", "refused", "cleared" (by the user) or "none" (no keys)
    QString reason;     // why keys were refused
    bool userSet = false;
};
// `shortcuts` in plugin name order; `host` CILogg's keys (portable text) with what owns them;
// `user` the user's choices by shortcut key; `previous` who held which keys before, so a
// shortcut stays with the plugin that got it first.
std::vector<ShortcutAssignment> assignShortcuts( const std::vector<PluginShortcut>& shortcuts,
                                                 const std::map<QString, QString>& host,
                                                 const std::map<QString, QStringList>& user,
                                                 const std::map<QString, QString>& previous );
} // namespace cilogg::plugins
