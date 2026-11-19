// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "commander.h"
#include "pluginmanager.h"
#include "pluginmenuspec.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QShortcut>
#include <functional>
#include <map>
#include <optional>
#include <vector>

class QAction;
class QMenu;
class QWidget;
class PluginViewHost;

// What an opened menu is about (docs/plugin-sdk-advanced.md 7.2). Only stable ids, so an entry
// picked after the menu closed still finds its tab.
struct PluginMenuContext {
    QString location;
    QString windowId;
    int windowIndex = 0;
    QString tabId;
    QString filePath;
    QString tabKind; // "file", "serial" or "plugin_stream"
    QString portName;
    int paneIndex = -1;
    QStringList paneTabIds;
    QString currentTabId;
    QString view;               // log: "main" or "filtered"
    std::optional<qint64> line; // log: the 1-based file line under the pointer
    QList<QPair<qint64, qint64>> ranges; // log: selected file lines (first, last), 1-based
    qint64 selectedLines = 0;
    bool portion = false;           // a part of one line is selected
    std::function<QString()> text;  // log: the selected text, read only when an entry is picked
    QString pickedText;             // the text read when the entry was picked
    bool textRead = false;
    // element (E17d, section 10.4): the view element whose context menu opened. `selectedLines`
    // counts its selected rows, so `when` conditions on the selection apply to rows.
    QString viewId;
    QString elementId;
    QString elementMenuId; // the element's `context_menu`: the only menu its entries come from
    QString rowId;         // the clicked row; empty when the click was not on a row
    QString columnId;
    QStringList selectedIds; // at most pluginview::MaxTableIds

    cilogg::plugins::MenuConditionContext conditions() const;
    // The `context` of menu/populate and ui/command. The selected text is added only with
    // `withText` (an entry was picked), at most 64 KiB, with `truncated`.
    QJsonObject toJson( bool withText ) const;
    // Reads the selected text now (when the entry is picked, before the menu closes).
    void readText();
};

// Process-wide state of plugin menus, shared by every window: menu/update changes and check marks,
// dynamic answers, and the shortcut assignment.
class PluginMenuRegistry final : public QObject {
    Q_OBJECT
public:
    static PluginMenuRegistry& instance();
    // Applies the manager's menu/update notifications; a manager is connected once.
    void attach( cilogg::plugins::PluginManager* manager );

    // The entry as it is now: the declared keys with menu/update changes and check marks.
    QJsonObject entryState( const QString& pluginId, const QString& menuId,
                            const QJsonObject& declared ) const;
    void applyUpdate( const QString& pluginId, const QString& menuId, const QJsonArray& changes );
    // A check mark the user set; the other entries of its group in that menu lose theirs.
    void setChecked( const QString& pluginId, const QJsonObject& menu, const QString& itemId,
                     bool checked );
    // menu/update changes last until the plugin stops; check marks last for the session.
    void pluginStopped( const QString& pluginId );
    // The plugin was disabled or removed: all of its menu state goes.
    void forgetPlugin( const QString& pluginId );

    // Dynamic answers by entry and context. A valid cached answer, or with `lastAnswer` the last
    // one of this session (native menu bars that cannot change while open).
    std::optional<QJsonArray> answer( const QString& key, bool lastAnswer ) const;
    void storeAnswer( const QString& key, const QJsonArray& items, qint64 cacheMs );

    // Shortcut assignment (section 7.6) for every enabled plugin; `host` are CILogg's keys with
    // what owns them, `user` the user's choices. Stable: a shortcut stays with the plugin that got
    // it first.
    void assignShortcuts( const std::vector<cilogg::plugins::PluginShortcut>& shortcuts,
                          const std::map<QString, QString>& host,
                          const std::map<QString, QStringList>& user );
    const std::vector<cilogg::plugins::PluginShortcut>& shortcuts() const
    {
        return shortcuts_;
    }
    const std::vector<cilogg::plugins::ShortcutAssignment>& assignments() const
    {
        return assignments_;
    }
    // The keys in effect for one shortcut key (empty when refused, cleared or unknown).
    QStringList effectiveKeys( const QString& shortcutKey ) const;
    // get_plugins / Plugins and Packs window: the assignment of each plugin's shortcuts.
    std::map<QString, QVariantList> shortcutReport() const;

Q_SIGNALS:
    void changed( const QString& pluginId );

private:
    PluginMenuRegistry() = default;
    struct Answer {
        QJsonArray items;
        qint64 expires = 0; // milliseconds since the epoch
    };
    QSet<cilogg::plugins::PluginManager*> attached_;
    std::map<QString, QJsonObject> overrides_; // plugin|menu|item -> changed keys
    std::map<QString, Answer> answers_;
    std::vector<cilogg::plugins::PluginShortcut> shortcuts_;
    std::vector<cilogg::plugins::ShortcutAssignment> assignments_;
    std::map<QString, QString> holders_; // keys -> shortcut key that held them
};

// The plugin menus of one window: Plugins and menu bar menus, the sections of the context menus,
// dynamic submenus, picked entries, shortcuts, get_plugin_menus and trigger_plugin_menu.
class PluginMenus final : public QObject {
    Q_OBJECT
public:
    using Source = cilogg::plugins::PluginManager::MenuSource;
    // Runs a plugin action from a menu entry: the plugin, the action definition, the tab, the
    // job's `menu` context, the entry's arguments, and whether a form may be shown.
    using ActionRunner
        = std::function<CommanderResult( const QString& pluginId, const QJsonObject& action,
                                         const PluginMenuContext& context,
                                         const QVariantMap& menu, const QJsonObject& arguments,
                                         bool interactive )>;
    // The context a shortcut or an automation call acts on: the focused tab (and, for `log`,
    // the focused log view's selection).
    using ContextProvider = std::function<PluginMenuContext( const QString& location )>;

    PluginMenus( QWidget* window, cilogg::plugins::PluginManager* manager, PluginViewHost* views,
                 ActionRunner runAction, ContextProvider currentContext );

    // Adds this location's plugin entries to `menu`, before `before` (or at the end): one section
    // after a separator, plugins in name order, at most 8 top-level entries per plugin before
    // "More". Returns the actions it added, for menus that are rebuilt each time they open.
    QList<QAction*> addSection( QMenu* menu, QAction* before, const PluginMenuContext& context );
    // Fills a plugin's own `plugins` or `menubar` menu each time it opens.
    void fillOwnMenu( QMenu* menu, const Source& plugin, const QJsonObject& definition,
                      const PluginMenuContext& context );
    // E17d: adds the entries of a plugin's `element` menu `menuId` to an element's context menu,
    // after a separator when the menu already has entries. Nothing when the plugin hides its
    // `element` entries or the menu is not an `element` menu.
    void addElementMenu( QMenu* menu, const QString& pluginId, const QString& menuId,
                         const PluginMenuContext& context );

    // get_plugin_menus: the resolved entries of a location with their state and conditions.
    QVariantList resolve( const QString& location, const PluginMenuContext& context,
                          const QString& pluginId ) const;
    // trigger_plugin_menu: runs an entry as a click (without a form) on `context`.
    CommanderResult trigger( const QString& pluginId, const QString& menuId, const QString& itemId,
                             PluginMenuContext context, std::optional<bool> checked );

    // Registers this window's plugin shortcuts again; `host` are CILogg's keys with their owner.
    void registerShortcuts( const std::map<QString, QString>& host,
                            const std::map<QString, QStringList>& user );
    // The shortcut objects of this window (tests).
    int shortcutCount() const
    {
        return static_cast<int>( shortcuts_.size() );
    }

    // A plugin title as menu text: `&` shown as written, tabs and control characters as spaces.
    static QString menuText( QString text );
    static QString entryObjectName( const QString& pluginId, const QString& menuId,
                                    const QString& itemId );
    static QString menuObjectName( const QString& pluginId, const QString& menuId,
                                   const QString& itemId = {} );

    // Removes a section addSection() added and deletes it (submenus included).
    static void removeSection( QMenu* menu, const QList<QAction*>& actions );

private:
    // `declared` entries get menu/update changes and shortcuts; dynamic answers do not.
    // `idPrefix` makes the object names of dynamic entries unique (design 7.7).
    void addEntries( QMenu* menu, const Source& plugin, const QJsonObject& definition,
                     const QJsonArray& items, const PluginMenuContext& context, bool declared,
                     const QString& idPrefix );
    QAction* addEntry( QMenu* menu, QAction* before, const Source& plugin,
                       const QJsonObject& definition, const QJsonObject& item,
                       const PluginMenuContext& context, bool declared, const QString& idPrefix,
                       std::map<QString, class QActionGroup*>* groups );
    // Fills a dynamic submenu: a cached answer, or "Loading…" and menu/populate.
    void populate( QPointer<QMenu> menu, const Source& plugin, const QJsonObject& definition,
                   const QString& itemId, const PluginMenuContext& context );
    void sendPopulate( QPointer<QMenu> menu, const Source& plugin, const QJsonObject& definition,
                       const QString& itemId, const PluginMenuContext& context,
                       quint64 generation );
    static void showUnavailable( QMenu* menu, const QString& text );
    // A declared entry checked as a click would find it (shown, not a submenu), with the check
    // mark a click sets and the selected text read; nullopt and *failure otherwise.
    struct Picked {
        Source plugin;
        QJsonObject definition;
        QJsonObject entry;
        PluginMenuContext context;
        std::optional<bool> checked;
    };
    std::optional<Picked> pick( const QString& pluginId, const QString& menuId,
                                const QString& itemId, PluginMenuContext context,
                                std::optional<bool> checked, CommanderResult* failure ) const;
    // Handles a picked entry after its menu closed (design 7.8).
    void activate( const Source& plugin, const QJsonObject& definition, const QJsonObject& entry,
                   PluginMenuContext context, const QString& origin, std::optional<bool> checked );
    CommanderResult dispatch( const Source& plugin, const QJsonObject& definition,
                              const QJsonObject& entry, const PluginMenuContext& context,
                              const QString& origin, std::optional<bool> checked,
                              bool interactive );
    // Whether an entry is usable at all: event entries need `ui/command` in `handles`.
    QString unavailableReason( const Source& plugin, const QJsonObject& entry ) const;
    QIcon iconFor( const Source& plugin, const QJsonValue& reference ) const;
    std::optional<Source> source( const QString& pluginId ) const;
    bool hidden( const Source& plugin, const QString& location ) const;
    QPointer<QWidget> window_;
    QPointer<cilogg::plugins::PluginManager> manager_;
    QPointer<PluginViewHost> views_;
    ActionRunner runAction_;
    ContextProvider currentContext_;
    std::vector<QPointer<QShortcut>> shortcuts_;
    quint64 populateGeneration_ = 0;
};
