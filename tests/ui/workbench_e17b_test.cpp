// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// E17b: plugin menus, context menus, dynamic submenus, ui/command, shortcuts, get_plugin_menus
// and trigger_plugin_menu (docs/plugin-sdk-advanced.md sections 7 and 15).
#include "abstractlogview.h"
#include "crawlerwidget.h"
#include "mainwindow.h"
#include "pluginmanager.h"
#include "pluginmenus.h"
#include "session.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMenu>
#include <QMenuBar>
#include <QShortcut>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <catch2/catch.hpp>
#include <memory>

using cilogg::plugins::PluginManager;

namespace {
const QString PluginId = QStringLiteral( "org.test.e17b" );

bool waitFor( const std::function<bool()>& predicate, int timeout = 5000 )
{
    return QTest::qWaitFor( predicate, timeout );
}

QJsonObject item( const QString& id, const QString& title, const QJsonObject& extra = {} )
{
    QJsonObject result{ { "id", id }, { "title", title } };
    for ( auto it = extra.begin(); it != extra.end(); ++it ) {
        result.insert( it.key(), it.value() );
    }
    return result;
}

QJsonObject menu( const QString& id, const QString& location, const QJsonArray& items,
                  const QString& title = {} )
{
    QJsonObject result{ { "id", id }, { "location", location }, { "items", items } };
    if ( !title.isEmpty() ) {
        result.insert( "title", title );
    }
    return result;
}

QJsonObject e17bManifest( const QString& executable, bool withHandles )
{
    QJsonArray many;
    for ( int index = 0; index < 10; ++index ) {
        many.append( item( QString( "m%1" ).arg( index ), QString( "Many %1" ).arg( index ),
                           { { "event", true } } ) );
    }
    QJsonObject json{
        { "id", PluginId },
        { "name", "E17b fixture" },
        { "version", "1.0.0" },
        { "protocol_version", 1 },
        { "executable", executable },
        { "capabilities", QJsonArray{ "tool" } },
        { "actions", QJsonArray{ QJsonObject{ { "id", "summary" }, { "capability", "tool" } } } },
        { "views", QJsonArray{ QJsonObject{ { "id", "main" }, { "title", "Main" } } } },
        { "menus",
          QJsonArray{
              menu( "main", "plugins",
                    QJsonArray{
                        item( "rename", "Rename me",
                              { { "event", true }, { "shortcut", "Ctrl+Alt+Shift+F11" } } ),
                        item( "toggle", "Toggle", { { "event", true }, { "checkable", true } } ),
                        item( "run", "Run summary", { { "action", "summary" } } ),
                        item( "taken", "Taken", { { "event", true }, { "shortcut", "Ctrl+O" } } ),
                        item( "plain", "Save & Close\tCtrl+Q",
                              { { "event", true },
                                { "tooltip", "<img src=\"file:///x.png\"><b>bold</b>" } } ),
                        item( "mode", "Mode",
                              { { "items",
                                  QJsonArray{ item( "fast", "Fast",
                                                    { { "event", true },
                                                      { "checkable", true },
                                                      { "group", "mode" },
                                                      { "checked", true } } ),
                                              item( "safe", "Safe",
                                                    { { "event", true },
                                                      { "checkable", true },
                                                      { "group", "mode" } } ) } } } ) },
                    "Fixture Tools" ),
              menu( "bar", "menubar", QJsonArray{ item( "about", "About", { { "event", true } } ) },
                    "Fixture Bar" ),
              menu( "tabs", "tab",
                    QJsonArray{ item( "decode", "Decode",
                                      { { "event", true },
                                        { "when", QJsonObject{ { "file_patterns",
                                                                 QJsonArray{ "*.log" } } } } } ),
                                item( "serial", "Serial only",
                                      { { "event", true },
                                        { "when", QJsonObject{ { "tab_kinds",
                                                                 QJsonArray{ "serial" } } } } } ),
                                item( "recent", "Recent", { { "dynamic", true } } ),
                                item( "open", "Open main", { { "view", "main" } } ) } ),
              menu( "panes", "pane", QJsonArray{ item( "count", "Count", { { "event", true } } ) } ),
              menu( "lines", "log",
                    QJsonArray{ item( "explain", "Explain",
                                      { { "event", true },
                                        { "when", QJsonObject{ { "selection", "lines" } } } } ),
                                item( "summarize", "Summarize lines", { { "action", "summary" } } ) } ),
              menu( "many", "pane", many ) } } };
    if ( withHandles ) {
        json.insert( "handles", QJsonArray{ "menu/populate", "query/cancel", "ui/command" } );
    }
    return json;
}

QList<QJsonObject> commands( const QString& folder )
{
    QList<QJsonObject> result;
    QFile log( QDir( folder ).filePath( "host-messages.jsonl" ) );
    if ( !log.open( QIODevice::ReadOnly ) ) {
        return result;
    }
    for ( const auto& line : log.readAll().split( '\n' ) ) {
        const auto frame = QJsonDocument::fromJson( line ).object();
        if ( frame.value( "method" ) == "ui/command" ) {
            result.append( frame.value( "params" ).toObject() );
        }
    }
    return result;
}

// A MainWindow with the fixture plugin and a log file open; its executor runs plugin actions the
// way the application does (with the `menu` context of a menu entry).
struct MenuWindow {
    QTemporaryDir temp;
    QString folder;
    QString logPath;
    std::unique_ptr<PluginManager> manager;
    std::unique_ptr<MainWindow> window;

    explicit MenuWindow( bool withHandles = true )
    {
        // The registry is process-wide: start every test without earlier check marks.
        PluginMenuRegistry::instance().forgetPlugin( PluginId );
        const auto shipped = QDir( temp.path() ).filePath( "shipped" );
        folder = QDir( shipped ).filePath( PluginId );
        REQUIRE( QDir().mkpath( folder ) );
        const auto executable = QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName();
        QFile file( QDir( folder ).filePath( "plugin.json" ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( QJsonDocument( e17bManifest( executable, withHandles ) ).toJson() );
        file.close();
        REQUIRE( QFile::copy( QString::fromUtf8( PLUGIN_FIXTURE_PATH ),
                              QDir( folder ).filePath( executable ) ) );
        manager
            = std::make_unique<PluginManager>( QDir( temp.path() ).filePath( "data" ), shipped );
        REQUIRE( manager->manage( "enable", PluginId, {} ).ok() );
        logPath = QDir( temp.path() ).filePath( "device.log" );
        QFile log( logPath );
        REQUIRE( log.open( QIODevice::WriteOnly ) );
        for ( int line = 1; line <= 40; ++line ) {
            log.write( QString( "line %1\n" ).arg( line ).toUtf8() );
        }
        log.close();
        auto session = std::make_shared<Session>();
        window = std::make_unique<MainWindow>( WindowSession{ session, "PluginSdkE17b", 0 } );
        window->setPluginManager( *manager, [ this ]( const CommanderRequest& request ) {
            if ( request.action == CommanderAction::InvokePlugin ) {
                auto context = window->pluginContext( request );
                if ( !request.pluginMenuContext.isEmpty() ) {
                    context.insert( "menu", request.pluginMenuContext );
                }
                const auto result = manager->invoke( request.pluginId, request.pluginAction,
                                                     request.pluginArguments, context );
                return result.ok() ? commanderSuccess( {}, result.payload )
                                   : commanderFailure( CommanderResultCode::ExecutionFailed,
                                                       result.error );
            }
            const auto result = manager->manage( request.pluginOperation, request.pluginId,
                                                 request.pluginArguments );
            return result.ok()
                       ? commanderSuccess( {}, result.payload )
                       : commanderFailure( CommanderResultCode::ExecutionFailed, result.error );
        } );
        window->show();
        window->loadInitialFile( logPath, false );
        REQUIRE( waitFor( [ & ] { return !window->pluginMenuContext( "tab" ).tabId.isEmpty(); } ) );
        REQUIRE( waitFor( [ & ] { return menus() != nullptr; } ) );
    }
    ~MenuWindow()
    {
        // As closing the window does: views and queries end before the window is destroyed.
        Q_EMIT window->windowClosed();
        QCoreApplication::processEvents();
    }
    PluginMenus* menus() const
    {
        return window->findChild<PluginMenus*>();
    }
    QString tabId() const
    {
        return window->pluginMenuContext( "tab" ).tabId;
    }
    CommanderResult run( CommanderRequest request )
    {
        return window->executeCommanderRequest( request );
    }
    CommanderResult menusOf( const QString& location, const QString& lines = {} )
    {
        CommanderRequest request;
        request.action = CommanderAction::GetPluginMenus;
        request.pluginArguments = { { "location", location } };
        if ( !lines.isEmpty() ) {
            request.pluginArguments.insert( "lines", lines );
        }
        return run( request );
    }
    CommanderResult trigger( const QString& menuId, const QString& itemId,
                             const QVariantMap& extra = {} )
    {
        CommanderRequest request;
        request.action = CommanderAction::TriggerPluginMenu;
        request.pluginId = PluginId;
        request.pluginArguments = { { "menuId", menuId }, { "itemId", itemId } };
        request.pluginArguments.insert( extra );
        return run( request );
    }
};

QVariantMap entryOf( const CommanderResult& result, const QString& itemId )
{
    for ( const auto& value : result.payload.value( "entries" ).toList() ) {
        if ( value.toMap().value( "itemId" ) == itemId ) {
            return value.toMap();
        }
    }
    return {};
}

QStringList objectNames( const QMenu& menu )
{
    QStringList names;
    for ( const auto* action : menu.actions() ) {
        names.append( action->menu() ? action->menu()->objectName() : action->objectName() );
    }
    return names;
}
} // namespace

TEST_CASE( "E17b a declared Plugins menu replaces the flat entries; the menu bar menu follows "
           "Plugins",
           "[ui][workbench][E17]" )
{
    MenuWindow fixture;
    auto& window = *fixture.window;
    auto* own = window.findChild<QMenu*>( "pluginMenu_" + PluginId + "_main" );
    REQUIRE( own );
    REQUIRE( own->title() == "Fixture Tools" );
    auto* plugins = window.findChild<QMenu*>( "pluginsMenu" );
    REQUIRE( plugins->actions().contains( own->menuAction() ) );
    // No flat entry of this plugin in the Plugins menu (open question 2).
    for ( const auto* action : plugins->actions() ) {
        REQUIRE_FALSE( action->objectName().startsWith( "pluginAction_" + PluginId ) );
        REQUIRE_FALSE( action->objectName().startsWith( "pluginViewAction_" + PluginId ) );
    }
    // Filled each time it opens.
    Q_EMIT own->aboutToShow();
    REQUIRE( own->findChild<QAction*>( "pluginMenuItem_" + PluginId + "_main_rename" ) );
    auto* mode = own->findChild<QMenu*>( "pluginMenu_" + PluginId + "_main_mode" );
    REQUIRE( mode );
    auto* fast = mode->findChild<QAction*>( "pluginMenuItem_" + PluginId + "_main_fast" );
    REQUIRE( fast );
    REQUIRE( fast->isCheckable() );
    REQUIRE( fast->isChecked() );
    REQUIRE( fast->actionGroup() != nullptr );
    // Plugin titles and tooltips are plain text: no mnemonic, no fake shortcut column, no HTML.
    auto* plain = own->findChild<QAction*>( "pluginMenuItem_" + PluginId + "_main_plain" );
    REQUIRE( plain );
    REQUIRE( plain->text() == "Save && Close Ctrl+Q" );
    REQUIRE_FALSE( plain->toolTip().contains( "<img" ) );
    REQUIRE( plain->toolTip().contains( "&lt;b&gt;bold&lt;/b&gt;" ) );
    // The shortcut is shown on the entry.
    auto* rename = own->findChild<QAction*>( "pluginMenuItem_" + PluginId + "_main_rename" );
    REQUIRE( rename->shortcut().toString( QKeySequence::PortableText ) == "Ctrl+Alt+Shift+F11" );
    // The menu bar menu sits right after Plugins.
    auto* bar = window.findChild<QMenu*>( "pluginMenu_" + PluginId + "_bar" );
    REQUIRE( bar );
    const auto actions = window.menuBar()->actions();
    REQUIRE( actions.indexOf( bar->menuAction() ) == actions.indexOf( plugins->menuAction() ) + 1 );
    // Hiding the menu bar location moves the menu back under Plugins.
    REQUIRE( fixture.manager->manage( "menus", PluginId, { { "hidden_locations", QVariantList{ "menubar" } } } ).ok() );
    REQUIRE( waitFor( [ & ] {
        auto* moved = window.findChild<QMenu*>( "pluginMenu_" + PluginId + "_bar" );
        return moved && plugins->actions().contains( moved->menuAction() );
    } ) );
}

TEST_CASE( "E17b context menu sections follow the location, its conditions and the 8-entry rule",
           "[ui][workbench][E17]" )
{
    MenuWindow fixture;
    auto* menus = fixture.menus();
    QMenu tabMenu;
    auto host = tabMenu.addAction( "Host entry" );
    const auto added = menus->addSection( &tabMenu, nullptr,
                                          fixture.window->pluginMenuContext( "tab", fixture.tabId() ) );
    const auto names = objectNames( tabMenu );
    // After the host entries; `when` hides the serial-only entry for a file tab.
    REQUIRE( names.indexOf( "pluginMenuItem_" + PluginId + "_tabs_decode" ) > names.indexOf( host->objectName() ) );
    REQUIRE_FALSE( names.contains( "pluginMenuItem_" + PluginId + "_tabs_serial" ) );
    REQUIRE( names.contains( "pluginMenu_" + PluginId + "_tabs_recent" ) );
    PluginMenus::removeSection( &tabMenu, added );
    REQUIRE( tabMenu.actions().size() == 1 );
    // Pane: ten entries of one plugin become eight and "More".
    QMenu paneMenu;
    menus->addSection( &paneMenu, nullptr, fixture.window->pluginMenuContext( "pane", fixture.tabId() ) );
    auto* more = paneMenu.findChild<QMenu*>( "pluginMenuMore_" + PluginId + "_pane" );
    REQUIRE( more );
    REQUIRE( more->actions().size() == 3 ); // count, m0..m9: 8 shown, 3 more
    // Hidden locations add nothing.
    REQUIRE( fixture.manager->manage( "menus", PluginId, { { "hidden_locations", QVariantList{ "tab" } } } ).ok() );
    QMenu hidden;
    REQUIRE( menus->addSection( &hidden, nullptr, fixture.window->pluginMenuContext( "tab", fixture.tabId() ) )
                 .isEmpty() );
    const auto listed = fixture.menusOf( "tab" );
    REQUIRE( listed.ok() );
    REQUIRE_FALSE( entryOf( listed, "decode" ).value( "shown" ).toBool() );
}

TEST_CASE( "E17b picked entries send ui/command, run actions and open views after the menu closes",
           "[ui][workbench][E17]" )
{
    MenuWindow fixture;
    auto* menus = fixture.menus();
    QMenu tabMenu;
    menus->addSection( &tabMenu, nullptr, fixture.window->pluginMenuContext( "tab", fixture.tabId() ) );
    auto* decode = tabMenu.findChild<QAction*>( "pluginMenuItem_" + PluginId + "_tabs_decode" );
    REQUIRE( decode );
    decode->trigger();
    // Queued: nothing is sent while the menu is still open.
    REQUIRE( commands( fixture.folder ).isEmpty() );
    REQUIRE( waitFor( [ & ] { return commands( fixture.folder ).size() == 1; } ) );
    const auto sent = commands( fixture.folder ).front();
    REQUIRE( sent.value( "origin" ) == "menu" );
    REQUIRE( sent.value( "menu_id" ) == "tabs" );
    REQUIRE( sent.value( "location" ) == "tab" );
    const auto context = sent.value( "context" ).toObject();
    REQUIRE( context.value( "tab_id" ) == fixture.tabId() );
    REQUIRE( context.value( "tab_kind" ) == "file" );
    REQUIRE( QFileInfo( context.value( "file_path" ).toString() ) == QFileInfo( fixture.logPath ) );
    REQUIRE( context.value( "pane_index" ).toInt() == 0 );
    // An action entry runs the action with the job's `menu` context.
    const auto run = fixture.trigger( "main", "run" );
    INFO( run.message.toStdString() );
    REQUIRE( run.ok() );
    const auto jobId = run.payload.value( "jobId" ).toString();
    REQUIRE_FALSE( jobId.isEmpty() );
    const auto menuContext = fixture.manager->job( jobId ).payload.value( "context" ).toMap().value( "menu" ).toMap();
    REQUIRE( menuContext.value( "menu_id" ) == "main" );
    REQUIRE( menuContext.value( "item_id" ) == "run" );
    // A view entry opens the view in this window.
    const auto open = fixture.trigger( "tabs", "open" );
    REQUIRE( open.ok() );
    REQUIRE( open.payload.value( "viewId" ) == "main" );
}

TEST_CASE( "E17b log entries get the selection; its text only when an entry is picked",
           "[ui][workbench][E17]" )
{
    MenuWindow fixture;
    auto* crawler = fixture.window->findChild<CrawlerWidget*>();
    REQUIRE( crawler );
    auto* mainView = crawler->mainLogView();
    REQUIRE( waitFor( [ & ] { return mainView->pluginLinesText( { { 40, 40 } }, 100 ) == "line 40"; } ) );
    // get_plugin_menus with explicit lines evaluates `when` against them.
    auto listed = fixture.menusOf( "log", "3-5,9" );
    REQUIRE( listed.ok() );
    REQUIRE( entryOf( listed, "explain" ).value( "shown" ).toBool() );
    const auto context = listed.payload.value( "context" ).toMap();
    REQUIRE( context.value( "view" ) == "main" );
    REQUIRE( context.value( "selection" ).toMap().value( "count" ).toInt() == 4 );
    REQUIRE_FALSE( context.value( "selection" ).toMap().contains( "text" ) );
    // Lines are clamped to the file and merged; none left is refused.
    listed = fixture.menusOf( "log", "38-100,4-5,3-4" );
    REQUIRE( listed.ok() );
    REQUIRE( listed.payload.value( "context" ).toMap().value( "selection" ).toMap().value( "count" ).toInt()
             == 6 );
    REQUIRE( fixture.menusOf( "log", "500-600" ).code == CommanderResultCode::InvalidRequest );
    // trigger_plugin_menu sends the text of those lines.
    REQUIRE( fixture.trigger( "lines", "explain", { { "lines", "3-5,9" } } ).ok() );
    REQUIRE( waitFor( [ & ] { return !commands( fixture.folder ).isEmpty(); } ) );
    const auto selection = commands( fixture.folder ).back().value( "context" ).toObject().value( "selection" ).toObject();
    REQUIRE( selection.value( "text" ) == "line 3\nline 4\nline 5\nline 9" );
    REQUIRE_FALSE( selection.value( "truncated" ).toBool() );
    REQUIRE( selection.value( "ranges" ).toArray()
             == QJsonArray{ QJsonArray{ 3, 5 }, QJsonArray{ 9, 9 } } );
    // Invalid line ranges are refused.
    REQUIRE( fixture.trigger( "lines", "explain", { { "lines", "5-3" } } ).code
             == CommanderResultCode::InvalidRequest );
    // The log view menu: entries before the Panes submenu, removed when it closes.
    QMenu menu;
    auto* panes = menu.addMenu( "Panes" );
    PluginMenuHost::LogSelection logSelection;
    logSelection.view = "filtered";
    logSelection.ranges = { { 7, 7 } };
    logSelection.count = 1;
    panes->setObjectName( "panes" );
    const auto entries
        = fixture.window->addPluginLogEntries( menu, panes->menuAction(), mainView, logSelection );
    const auto names = objectNames( menu );
    REQUIRE( names.contains( "pluginMenuItem_" + PluginId + "_lines_explain" ) );
    REQUIRE( names.indexOf( "pluginMenuItem_" + PluginId + "_lines_explain" )
             < names.indexOf( "panes" ) );
    fixture.window->removePluginLogEntries( menu, entries );
    REQUIRE( menu.actions().size() == 1 );
    // The view's own description of its selection: file lines, 1-based.
    const auto own = mainView->pluginSelection( {} );
    REQUIRE( own.view == "main" );
}

TEST_CASE( "E17b dynamic submenus start the plugin, show Loading and the answer",
           "[ui][workbench][E17]" )
{
    MenuWindow fixture;
    REQUIRE( fixture.manager->pluginState( PluginId ) == "stopped" );
    QMenu tabMenu;
    fixture.menus()->addSection( &tabMenu, nullptr,
                                 fixture.window->pluginMenuContext( "tab", fixture.tabId() ) );
    auto* recent = tabMenu.findChild<QMenu*>( "pluginMenu_" + PluginId + "_tabs_recent" );
    REQUIRE( recent );
    Q_EMIT recent->aboutToShow();
    REQUIRE( recent->findChild<QAction*>( "pluginMenuLoading" ) );
    REQUIRE( waitFor( [ & ] {
        return recent->findChild<QAction*>( "pluginMenuItem_" + PluginId + "_tabs_recent_r1" ) != nullptr;
    } ) );
    REQUIRE( fixture.manager->pluginState( PluginId ) == "ready" );
    auto* second = recent->findChild<QAction*>( "pluginMenuItem_" + PluginId + "_tabs_recent_r2" );
    REQUIRE( second );
    REQUIRE( second->text() == "Tab " + fixture.tabId().left( 8 ) );
    // A picked dynamic entry sends its own id.
    second->trigger();
    REQUIRE( waitFor( [ & ] { return !commands( fixture.folder ).isEmpty(); } ) );
    REQUIRE( commands( fixture.folder ).back().value( "item_id" ) == "r2" );
}

TEST_CASE( "E17b menu/update and check marks change the entries", "[ui][workbench][E17]" )
{
    MenuWindow fixture;
    REQUIRE( fixture.trigger( "main", "rename" ).ok() );
    REQUIRE( waitFor( [ & ] {
        return entryOf( fixture.menusOf( "plugins" ), "rename" ).value( "title" ) == "Renamed";
    } ) );
    REQUIRE_FALSE( entryOf( fixture.menusOf( "plugins" ), "toggle" ).value( "enabled" ).toBool() );
    // Radio entries: picking one unchecks the others of its group.
    REQUIRE( fixture.trigger( "main", "safe" ).ok() );
    const auto listed = fixture.menusOf( "plugins" );
    REQUIRE( entryOf( listed, "safe" ).value( "checked" ).toBool() );
    REQUIRE_FALSE( entryOf( listed, "fast" ).value( "checked" ).toBool() );
    REQUIRE( waitFor( [ & ] { return commands( fixture.folder ).size() >= 2; } ) );
    REQUIRE( commands( fixture.folder ).back().value( "checked" ).toBool() );
    // A disabled entry cannot be triggered.
    REQUIRE( fixture.trigger( "main", "toggle" ).code == CommanderResultCode::InvalidRequest );
    // menu/update changes stop with the plugin; check marks stay for the session.
    REQUIRE( fixture.manager->manage( "restart", PluginId, {} ).ok() );
    REQUIRE( waitFor( [ & ] {
        return entryOf( fixture.menusOf( "plugins" ), "rename" ).value( "title" ) == "Rename me";
    } ) );
    REQUIRE( entryOf( fixture.menusOf( "plugins" ), "safe" ).value( "checked" ).toBool() );
}

TEST_CASE( "E17b menu/update reaches open entries, keeps radio groups exclusive and covers "
           "submenus",
           "[ui][workbench][E17]" )
{
    MenuWindow fixture;
    auto& window = *fixture.window;
    auto& registry = PluginMenuRegistry::instance();
    auto* own = window.findChild<QMenu*>( "pluginMenu_" + PluginId + "_main" );
    REQUIRE( own );
    Q_EMIT own->aboutToShow();
    auto* rename = own->findChild<QAction*>( "pluginMenuItem_" + PluginId + "_main_rename" );
    REQUIRE( rename );
    // An open entry changes in place, and a click uses its state now.
    registry.applyUpdate( PluginId, "main",
                          { QJsonObject{ { "id", "rename" },
                                         { "set", QJsonObject{ { "title", "Live" },
                                                               { "visible", false } } } } } );
    REQUIRE( rename->text() == "Live" );
    REQUIRE_FALSE( rename->isVisible() );
    const auto before = commands( fixture.folder ).size();
    rename->trigger(); // a click on the entry as it was: the hidden entry does nothing
    QTest::qWait( 100 );
    REQUIRE( commands( fixture.folder ).size() == before );
    registry.applyUpdate( PluginId, "main",
                          { QJsonObject{ { "id", "rename" },
                                         { "set", QJsonObject{ { "visible", true },
                                                               { "enabled", false } } } } } );
    REQUIRE( rename->isVisible() );
    REQUIRE_FALSE( rename->isEnabled() );
    // A plugin that checks a radio entry unchecks the rest of the group.
    registry.applyUpdate( PluginId, "main",
                          { QJsonObject{ { "id", "safe" }, { "set", QJsonObject{ { "checked", true } } } } } );
    const auto listed = fixture.menusOf( "plugins" );
    REQUIRE( entryOf( listed, "safe" ).value( "checked" ).toBool() );
    REQUIRE_FALSE( entryOf( listed, "fast" ).value( "checked" ).toBool() );
    // An open submenu changes in place too: its title, tooltip and icon.
    auto* mode = own->findChild<QMenu*>( "pluginMenu_" + PluginId + "_main_mode" );
    REQUIRE( mode );
    REQUIRE( mode->icon().isNull() );
    registry.applyUpdate( PluginId, "main",
                          { QJsonObject{ { "id", "mode" },
                                         { "set", QJsonObject{ { "title", "Modes" },
                                                               { "tooltip", "Pick a mode" },
                                                               { "icon", "builtin:info" } } } } } );
    REQUIRE( mode->title() == "Modes" );
    REQUIRE( mode->menuAction()->toolTip() == "Pick a mode" );
    REQUIRE_FALSE( mode->icon().isNull() );
    // An entry under a disabled or hidden submenu cannot be triggered.
    registry.applyUpdate( PluginId, "main",
                          { QJsonObject{ { "id", "mode" }, { "set", QJsonObject{ { "enabled", false } } } } } );
    REQUIRE( fixture.trigger( "main", "fast" ).code == CommanderResultCode::InvalidRequest );
    registry.applyUpdate( PluginId, "main",
                          { QJsonObject{ { "id", "mode" },
                                         { "set", QJsonObject{ { "enabled", true },
                                                               { "visible", false } } } } } );
    REQUIRE( fixture.trigger( "main", "fast" ).code == CommanderResultCode::InvalidRequest );
    registry.applyUpdate( PluginId, "main",
                          { QJsonObject{ { "id", "mode" }, { "set", QJsonObject{ { "visible", true } } } } } );
    REQUIRE( fixture.trigger( "main", "fast" ).ok() );
    // Hiding the Plugins location hides the plugin's Plugins entries: no flat entries come back.
    REQUIRE( fixture.manager->manage( "menus", PluginId,
                                      { { "hidden_locations", QVariantList{ "plugins" } } } )
                 .ok() );
    auto* plugins = window.findChild<QMenu*>( "pluginsMenu" );
    REQUIRE( waitFor( [ & ] {
        return window.findChild<QMenu*>( "pluginMenu_" + PluginId + "_main" ) == nullptr
               || !plugins->actions().contains(
                   window.findChild<QMenu*>( "pluginMenu_" + PluginId + "_main" )->menuAction() );
    } ) );
    for ( const auto* action : plugins->actions() ) {
        REQUIRE_FALSE( action->objectName().startsWith( "pluginAction_" + PluginId ) );
        REQUIRE_FALSE( action->objectName().startsWith( "pluginViewAction_" + PluginId ) );
    }
}

TEST_CASE( "E17b shortcuts: registered per window, refused on CILogg's keys, listed in Options",
           "[ui][workbench][E17]" )
{
    MenuWindow fixture;
    auto& window = *fixture.window;
    REQUIRE( waitFor( [ & ] {
        return window.findChild<QShortcut*>( "pluginShortcut_plugin_" + PluginId + "_main_rename" )
               != nullptr;
    } ) );
    auto* shortcut
        = window.findChild<QShortcut*>( "pluginShortcut_plugin_" + PluginId + "_main_rename" );
    REQUIRE( shortcut->key().toString( QKeySequence::PortableText ) == "Ctrl+Alt+Shift+F11" );
    REQUIRE_FALSE( window.findChild<QShortcut*>( "pluginShortcut_plugin_" + PluginId + "_main_taken" ) );
    // The assignment is reported with the plugin (get_plugins).
    const auto plugin = fixture.manager->plugins().front().toMap();
    QVariantMap taken;
    for ( const auto& value : plugin.value( "shortcuts" ).toList() ) {
        if ( value.toMap().value( "itemId" ) == "taken" ) {
            taken = value.toMap();
        }
    }
    REQUIRE( taken.value( "state" ) == "refused" );
    REQUIRE( taken.value( "reason" ).toString().contains( "Ctrl+O" ) );
    // A shortcut acts like a click with the current context.
    Q_EMIT shortcut->activated();
    REQUIRE( waitFor( [ & ] { return !commands( fixture.folder ).isEmpty(); } ) );
    REQUIRE( commands( fixture.folder ).back().value( "item_id" ) == "rename" );
    REQUIRE( commands( fixture.folder ).back().value( "origin" ) == "shortcut" );
    // The process-wide list that Options > Shortcuts shows.
    const auto& registry = PluginMenuRegistry::instance();
    const auto listed = std::any_of( registry.shortcuts().begin(), registry.shortcuts().end(),
                                     []( const auto& item ) {
                                         return item.key == "plugin/" + PluginId + "/main/rename"
                                                && item.pluginName == "E17b fixture";
                                     } );
    REQUIRE( listed );
    REQUIRE( registry.effectiveKeys( "plugin/" + PluginId + "/main/taken" ).isEmpty() );
}

TEST_CASE( "E17b event entries of a plugin without ui/command are disabled",
           "[ui][workbench][E17]" )
{
    MenuWindow fixture( false );
    const auto listed = fixture.menusOf( "plugins" );
    const auto rename = entryOf( listed, "rename" );
    REQUIRE_FALSE( rename.value( "enabled" ).toBool() );
    REQUIRE( rename.value( "unavailable" ).toString().contains( "newer SDK" ) );
    REQUIRE( fixture.trigger( "main", "rename" ).code == CommanderResultCode::InvalidRequest );
    // Its action entries still work.
    REQUIRE( entryOf( listed, "run" ).value( "enabled" ).toBool() );
}
