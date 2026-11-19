// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plugin menus, context menus and shortcuts (E17b, docs/plugin-sdk-advanced.md section 7): the
// manifest rules, `when`, dynamic answers, menu/update, shortcut assignment, ui/command and the
// Commander request forms.
#include "commander.h"
#include "pluginmanager.h"
#include "pluginmenuspec.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <catch2/catch.hpp>
#include <functional>
#include <memory>

using namespace cilogg::plugins;

namespace {
bool waitUntil( const std::function<bool()>& predicate, int timeout = 5000 )
{
    QElapsedTimer timer;
    timer.start();
    while ( !predicate() && timer.elapsed() < timeout ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 2 );
    }
    return predicate();
}

QJsonObject entry( const QString& id, const QString& title, const QJsonObject& extra = {} )
{
    QJsonObject result{ { "id", id }, { "title", title } };
    for ( auto it = extra.begin(); it != extra.end(); ++it ) {
        result.insert( it.key(), it.value() );
    }
    return result;
}

// The fixture plugin (tests/helpers/plugin_fixture.cpp) with menus in two locations.
QJsonObject menuManifest( const QString& id, bool withHandles = true )
{
    QJsonObject json{
        { "id", id },
        { "name", "Fixture " + id },
        { "version", "1.0.0" },
        { "protocol_version", 1 },
        { "executable", QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName() },
        { "arguments", QJsonArray{} },
        { "capabilities", QJsonArray{ "tool" } },
        { "configuration", QJsonArray{} },
        { "actions", QJsonArray{ QJsonObject{ { "id", "summary" }, { "capability", "tool" } } } },
        { "views", QJsonArray{ QJsonObject{ { "id", "main" }, { "title", "Main" } } } },
        { "menus",
          QJsonArray{
              QJsonObject{
                  { "id", "main" },
                  { "location", "plugins" },
                  { "title", "Fixture Tools" },
                  { "items",
                    QJsonArray{
                        entry( "rename", "Rename me",
                               { { "event", true }, { "shortcut", "Ctrl+Alt+Shift+F11" } } ),
                        entry( "toggle", "Toggle", { { "event", true }, { "checkable", true } } ),
                        entry( "run", "Run summary", { { "action", "summary" } } ),
                        entry( "open", "Open main", { { "view", "main" } } ),
                        QJsonObject{ { "type", "separator" } },
                        entry( "recent", "Recent", { { "dynamic", true } } ),
                        entry( "cached", "Cached", { { "dynamic", true } } ),
                        entry( "badmenu", "Bad", { { "dynamic", true } } ) } } },
              QJsonObject{
                  { "id", "lines" },
                  { "location", "log" },
                  { "items", QJsonArray{ entry( "explain", "Explain",
                                                { { "event", true },
                                                  { "when", QJsonObject{ { "selection", "lines" },
                                                                         { "max_lines", 1000 } } } } ) } } } } } };
    if ( withHandles ) {
        json[ "handles" ]
            = QJsonArray{ "tooltip/resolve", "menu/populate", "query/cancel", "ui/command" };
    }
    return json;
}

QString package( const QString& root, const QJsonObject& json )
{
    const auto directory = QDir( root ).filePath( json.value( "id" ).toString() );
    REQUIRE( QDir().mkpath( directory ) );
    QFile file( QDir( directory ).filePath( "plugin.json" ) );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( QJsonDocument( json ).toJson() );
    file.close();
    REQUIRE( QFile::copy( QString::fromUtf8( PLUGIN_FIXTURE_PATH ),
                          QDir( directory ).filePath( json[ "executable" ].toString() ) ) );
    return directory;
}

QList<QJsonObject> recorded( const QString& folder, const QString& method )
{
    QList<QJsonObject> result;
    QFile log( QDir( folder ).filePath( "host-messages.jsonl" ) );
    if ( !log.open( QIODevice::ReadOnly ) ) {
        return result;
    }
    for ( const auto& line : log.readAll().split( '\n' ) ) {
        const auto frame = QJsonDocument::fromJson( line ).object();
        if ( frame.value( "method" ) == method ) {
            result.append( frame.value( "params" ).toObject() );
        }
    }
    return result;
}

struct MenuFixture {
    QTemporaryDir temp;
    QString shipped;
    QList<QJsonObject> outgoing; // declared before the manager, which sends frames at shutdown
    std::unique_ptr<PluginManager> manager;
    QString folder;

    explicit MenuFixture( const QJsonObject& json )
    {
        shipped = QDir( temp.path() ).filePath( "shipped" );
        folder = package( shipped, json );
        manager = std::make_unique<PluginManager>( QDir( temp.path() ).filePath( "data" ), shipped );
        QObject::connect( manager.get(), &PluginManager::protocolFrame,
                          [ this ]( const QString&, bool toPlugin, const QJsonObject& frame ) {
                              if ( toPlugin ) {
                                  outgoing.append( frame );
                              }
                          } );
        REQUIRE( manager->manage( "enable", json.value( "id" ).toString(), {} ).ok() );
    }
    int sent( const QString& method ) const
    {
        return static_cast<int>( std::count_if( outgoing.begin(), outgoing.end(),
                                                [ & ]( const QJsonObject& frame ) {
                                                    return frame.value( "method" ) == method;
                                                } ) );
    }
};

QJsonObject command( const QString& item, const QJsonObject& extra = {} )
{
    QJsonObject params{ { "origin", "menu" },     { "menu_id", "main" },
                        { "item_id", item },      { "location", "plugins" },
                        { "window_id", "w1" },    { "context", QJsonObject{ { "location", "plugins" } } } };
    for ( auto it = extra.begin(); it != extra.end(); ++it ) {
        params.insert( it.key(), it.value() );
    }
    return params;
}
} // namespace

TEST_CASE( "E17b manifests validate menus and shortcuts", "[plugins][E17]" )
{
    const auto parse = []( const std::function<void( QJsonObject& )>& change ) {
        auto json = menuManifest( "org.test.menus" );
        change( json );
        return Manifest::parse( QJsonDocument( json ).toJson(), "folder" );
    };
    const auto withItems = []( QJsonObject& json, const QJsonArray& items ) {
        auto menus = json[ "menus" ].toArray();
        auto menu = menus[ 0 ].toObject();
        menu[ "items" ] = items;
        menus[ 0 ] = menu;
        json[ "menus" ] = menus;
    };
    REQUIRE( parse( []( QJsonObject& ) {} ).valid() );
    // A location this host does not render is kept (a newer plugin), never shown.
    REQUIRE( parse( []( QJsonObject& json ) {
                 auto menus = json[ "menus" ].toArray();
                 menus.append( QJsonObject{ { "id", "later" },
                                            { "location", "status_bar" },
                                            { "items", QJsonArray{} } } );
                 json[ "menus" ] = menus;
             } ).valid() );
    const std::vector<std::pair<const char*, QJsonArray>> refused{
        { "undeclared action", QJsonArray{ entry( "a", "A", { { "action", "nope" } } ) } },
        { "undeclared view", QJsonArray{ entry( "a", "A", { { "view", "nope" } } ) } },
        { "two targets", QJsonArray{ entry( "a", "A", { { "event", true }, { "view", "main" } } ) } },
        { "no target", QJsonArray{ entry( "a", "A" ) } },
        { "submenu with a target",
          QJsonArray{ entry( "a", "A", { { "event", true }, { "items", QJsonArray{} } } ) } },
        { "dynamic with items",
          QJsonArray{ entry( "a", "A", { { "dynamic", true }, { "items", QJsonArray{} } } ) } },
        { "duplicate id", QJsonArray{ entry( "a", "A", { { "event", true } } ),
                                      entry( "a", "B", { { "event", true } } ) } },
        { "checked without checkable",
          QJsonArray{ entry( "a", "A", { { "event", true }, { "checked", true } } ) } },
        { "group without checkable",
          QJsonArray{ entry( "a", "A", { { "event", true }, { "group", "g" } } ) } },
        { "arguments without action",
          QJsonArray{ entry( "a", "A", { { "event", true }, { "arguments", QJsonObject{} } } ) } },
        { "shortcut without modifier",
          QJsonArray{ entry( "a", "A", { { "event", true }, { "shortcut", "K" } } ) } },
        { "three shortcuts",
          QJsonArray{ entry( "a", "A", { { "event", true },
                                         { "shortcut", QJsonArray{ "Ctrl+1", "Ctrl+2", "Ctrl+3" } } } ) } },
        { "shortcut on a submenu",
          QJsonArray{ entry( "a", "A", { { "items", QJsonArray{} }, { "shortcut", "Ctrl+1" } } ) } },
        { "invalid when", QJsonArray{ entry( "a", "A", { { "event", true },
                                                         { "when", QJsonObject{ { "views", "main" } } } } ) } },
        { "long title", QJsonArray{ entry( "a", QString( 129, 'x' ), { { "event", true } } ) } },
        { "bad icon", QJsonArray{ entry( "a", "A", { { "event", true }, { "icon", "../x.png" } } ) } },
        { "slash in an id", QJsonArray{ entry( "a/b", "A", { { "event", true } } ) } },
    };
    for ( const auto& refusal : refused ) {
        INFO( refusal.first );
        const auto& items = refusal.second;
        REQUIRE_FALSE( parse( [ & ]( QJsonObject& json ) { withItems( json, items ); } ).valid() );
    }
    // Depth: four levels are allowed, five are not.
    const auto nested = []( int depth ) {
        QJsonArray items{ entry( "leaf", "Leaf", { { "event", true } } ) };
        for ( int level = 1; level < depth; ++level ) {
            items = QJsonArray{ entry( QString( "sub%1" ).arg( level ), "Sub", { { "items", items } } ) };
        }
        return items;
    };
    REQUIRE( parse( [ & ]( QJsonObject& json ) { withItems( json, nested( 4 ) ); } ).valid() );
    REQUIRE_FALSE( parse( [ & ]( QJsonObject& json ) { withItems( json, nested( 5 ) ); } ).valid() );
    // Limits: 256 entries, 16 menus, one menu bar menu, 32 shortcuts.
    REQUIRE_FALSE( parse( [ & ]( QJsonObject& json ) {
                       QJsonArray items;
                       for ( int index = 0; index < 257; ++index ) {
                           items.append( entry( QString( "e%1" ).arg( index ), "E", { { "event", true } } ) );
                       }
                       withItems( json, items );
                   } ).valid() );
    REQUIRE_FALSE( parse( []( QJsonObject& json ) {
                       QJsonArray menus;
                       for ( int index = 0; index < 17; ++index ) {
                           menus.append( QJsonObject{ { "id", QString( "m%1" ).arg( index ) },
                                                      { "location", "tab" },
                                                      { "items", QJsonArray{} } } );
                       }
                       json[ "menus" ] = menus;
                   } ).valid() );
    REQUIRE_FALSE( parse( []( QJsonObject& json ) {
                       json[ "menus" ] = QJsonArray{
                           QJsonObject{ { "id", "a" }, { "location", "menubar" }, { "items", QJsonArray{} } },
                           QJsonObject{ { "id", "b" }, { "location", "menubar" }, { "items", QJsonArray{} } } };
                   } ).valid() );
    // Menu ids name shortcuts: `view` and `action` would share the names of view and action
    // shortcuts.
    for ( const auto* reserved : { "view", "action", "a/b" } ) {
        INFO( reserved );
        REQUIRE_FALSE( parse( [ reserved ]( QJsonObject& json ) {
                           json[ "menus" ] = QJsonArray{ QJsonObject{ { "id", reserved },
                                                                      { "location", "tab" },
                                                                      { "items", QJsonArray{} } } };
                       } ).valid() );
    }
    REQUIRE_FALSE( parse( [ & ]( QJsonObject& json ) {
                       QJsonArray items;
                       for ( int index = 0; index < 33; ++index ) {
                           items.append( entry( QString( "e%1" ).arg( index ), "E",
                                                { { "event", true },
                                                  { "shortcut", QString( "Ctrl+Alt+F%1" ).arg( index % 12 + 1 ) } } ) );
                       }
                       withItems( json, items );
                   } ).valid() );
    // Shortcuts on actions and views use the same rules.
    REQUIRE( parse( []( QJsonObject& json ) {
                 json[ "actions" ] = QJsonArray{ QJsonObject{
                     { "id", "summary" }, { "capability", "tool" }, { "shortcut", "Ctrl+Alt+S" } } };
             } ).valid() );
    REQUIRE_FALSE( parse( []( QJsonObject& json ) {
                       json[ "views" ] = QJsonArray{
                           QJsonObject{ { "id", "main" }, { "shortcut", QJsonArray{} } } };
                   } ).valid() );
    // The declared shortcuts, named as the shortcut list names them.
    const auto shortcuts
        = manifestShortcuts( "org.test.menus", "Menus", menuManifest( "org.test.menus" ) );
    REQUIRE( shortcuts.size() == 1 );
    REQUIRE( shortcuts.front().key == "plugin/org.test.menus/main/rename" );
    REQUIRE( shortcuts.front().declared == QStringList{ "Ctrl+Alt+Shift+F11" } );
    REQUIRE( shortcuts.front().location == "plugins" );
    // A menu at a location this host does not show gets no shortcuts (Codex review of #131).
    auto future = menuManifest( "org.test.menus" );
    auto menus = future.value( "menus" ).toArray();
    menus.append( QJsonObject{
        { "id", "later" },
        { "location", "toolbar" },
        { "items", QJsonArray{ QJsonObject{ { "id", "go" },
                                            { "title", "Go" },
                                            { "event", true },
                                            { "shortcut", "Ctrl+Alt+Shift+F10" } } } } } );
    future.insert( "menus", menus );
    REQUIRE( manifestShortcuts( "org.test.menus", "Menus", future ).size() == 1 );
}

TEST_CASE( "E17b when conditions decide which entries a menu shows", "[plugins][E17]" )
{
    MenuConditionContext log;
    log.view = "filtered";
    log.tabKind = "serial";
    log.fileName = "COM5.log";
    log.hasSelection = true;
    log.wholeLines = true;
    log.selectedLines = 12;
    const auto holds = []( const QJsonObject& when, const MenuConditionContext& context ) {
        return menuConditionsHold( when, context );
    };
    REQUIRE( holds( {}, log ) );
    REQUIRE( holds( { { "views", QJsonArray{ "main", "filtered" } } }, log ) );
    REQUIRE_FALSE( holds( { { "views", QJsonArray{ "main" } } }, log ) );
    REQUIRE( holds( { { "tab_kinds", QJsonArray{ "serial" } } }, log ) );
    REQUIRE_FALSE( holds( { { "tab_kinds", QJsonArray{ "file" } } }, log ) );
    REQUIRE( holds( { { "file_patterns", QJsonArray{ "*.txt", "com*.LOG" } } }, log ) );
    REQUIRE_FALSE( holds( { { "file_patterns", QJsonArray{ "*.txt" } } }, log ) );
    REQUIRE( holds( { { "selection", "lines" }, { "min_lines", 10 }, { "max_lines", 12 } }, log ) );
    REQUIRE_FALSE( holds( { { "max_lines", 11 } }, log ) );
    REQUIRE_FALSE( holds( { { "min_lines", 13 } }, log ) );
    REQUIRE_FALSE( holds( { { "selection", "none" } }, log ) );
    // A condition this host does not know hides the entry.
    REQUIRE_FALSE( holds( { { "process_running", true } }, log ) );
    // Outside the log views there is no view and no selection.
    MenuConditionContext tab;
    tab.tabKind = "file";
    tab.fileName = "app.log";
    REQUIRE_FALSE( holds( { { "views", QJsonArray{ "main" } } }, tab ) );
    REQUIRE( holds( { { "selection", "none" } }, tab ) );
    REQUIRE_FALSE( holds( { { "selection", "any" } }, tab ) );
    // A part of one line is "any" but not "lines".
    auto portion = log;
    portion.wholeLines = false;
    portion.selectedLines = 1;
    REQUIRE( holds( { { "selection", "any" } }, portion ) );
    REQUIRE_FALSE( holds( { { "selection", "lines" } }, portion ) );
}

TEST_CASE( "E17b dynamic answers and menu/update are checked", "[plugins][E17]" )
{
    const QStringList actions{ "summary" };
    const QStringList views{ "main" };
    QString error;
    // Null is an empty list; cache_ms is bounded by 60 seconds.
    auto answer = sanitizeMenuPopulate( QJsonValue::Null, actions, views, &error );
    REQUIRE( error.isEmpty() );
    REQUIRE( answer.value( "items" ).toArray().isEmpty() );
    answer = sanitizeMenuPopulate(
        QJsonObject{ { "items", QJsonArray{ entry( "a", "A", { { "event", true } } ) } },
                     { "cache_ms", 600000 } },
        actions, views, &error );
    REQUIRE( error.isEmpty() );
    REQUIRE( answer.value( "cache_ms" ).toInt() == MaxDynamicMenuCacheMs );
    for ( const auto& bad : { QJsonValue( QJsonArray{} ),
                              QJsonValue( QJsonObject{ { "items", QJsonArray{ entry( "a", "A", { { "event", true }, { "shortcut", "Ctrl+1" } } ) } } } ),
                              QJsonValue( QJsonObject{ { "items", QJsonArray{ entry( "a", "A", { { "dynamic", true } } ) } } } ),
                              QJsonValue( QJsonObject{ { "items", QJsonArray{ entry( "a", "A", { { "action", "other" } } ) } } } ),
                              QJsonValue( QJsonObject{ { "items", QJsonArray{} }, { "cache_ms", -1 } } ) } ) {
        error.clear();
        sanitizeMenuPopulate( bad, actions, views, &error );
        REQUIRE_FALSE( error.isEmpty() );
    }
    // menu/update: known keys of declared entries only; checked only on checkable entries.
    const auto manifest = menuManifest( "org.test.menus" );
    error.clear();
    const auto changes = sanitizeMenuUpdate(
        manifest,
        { { "menu_id", "main" },
          { "items", QJsonArray{
                         QJsonObject{ { "id", "rename" },
                                      { "set", QJsonObject{ { "title", "New" },
                                                            { "checked", true },
                                                            { "colour", "red" } } } },
                         QJsonObject{ { "id", "toggle" }, { "set", QJsonObject{ { "checked", true } } } },
                         QJsonObject{ { "id", "missing" }, { "set", QJsonObject{ { "title", "X" } } } } } } },
        &error );
    REQUIRE( changes.size() == 2 );
    REQUIRE( changes.at( 0 ).toObject().value( "set" ).toObject()
             == QJsonObject{ { "title", "New" } } );
    REQUIRE( changes.at( 1 ).toObject().value( "set" ).toObject()
             == QJsonObject{ { "checked", true } } );
    error.clear();
    REQUIRE( sanitizeMenuUpdate( manifest, { { "menu_id", "nope" }, { "items", QJsonArray{} } },
                                 &error )
                 .isEmpty() );
    REQUIRE_FALSE( error.isEmpty() );
    REQUIRE( findMenuEntry( findMenu( manifest, "main" ), "recent" ).value( "dynamic" ).toBool() );
    REQUIRE( menuEntryKind( findMenuEntry( findMenu( manifest, "main" ), "run" ) ) == "action" );
}

TEST_CASE( "E17b shortcuts never take CILogg's keys; the first plugin keeps a shared key",
           "[plugins][E17]" )
{
    const auto shortcut = []( const QString& key, const QStringList& keys ) {
        PluginShortcut result;
        result.key = key;
        result.pluginId = key.section( '/', 1, 1 );
        result.declared = keys;
        return result;
    };
    const std::vector<PluginShortcut> shortcuts{
        shortcut( "plugin/a/m/one", { "Ctrl+Alt+D" } ),
        shortcut( "plugin/a/m/two", { "Ctrl+O", "Ctrl+Alt+T" } ),
        shortcut( "plugin/a/m/three", { "Ctrl+C" } ),
        shortcut( "plugin/b/m/one", { "Ctrl+Alt+D" } ),
        shortcut( "plugin/b/m/two", { "Ctrl+Alt+U" } ),
    };
    const std::map<QString, QString> host{ { "Ctrl+O", "Open file" } };
    auto result = assignShortcuts( shortcuts, host, {}, {} );
    REQUIRE( result[ 0 ].state == "active" );
    REQUIRE( result[ 0 ].keys == QStringList{ "Ctrl+Alt+D" } );
    // One key of two refused: the other stays.
    REQUIRE( result[ 1 ].keys == QStringList{ "Ctrl+Alt+T" } );
    REQUIRE( result[ 1 ].reason.contains( "Open file" ) );
    REQUIRE( result[ 2 ].state == "refused" );
    REQUIRE( result[ 2 ].reason.contains( "reserved" ) );
    REQUIRE( result[ 3 ].state == "refused" );
    REQUIRE( result[ 3 ].reason.contains( "plugin/a/m/one" ) );
    // The plugin that held a key before keeps it, whatever the order.
    std::map<QString, QString> previous{ { "Ctrl+Alt+D", "plugin/b/m/one" } };
    result = assignShortcuts( shortcuts, host, {}, previous );
    REQUIRE( result[ 3 ].state == "active" );
    REQUIRE( result[ 0 ].state == "refused" );
    // The user's choice wins over plugin defaults; an empty choice clears the shortcut.
    const std::map<QString, QStringList> user{ { "plugin/b/m/two", { "Ctrl+Alt+D", "" } },
                                               { "plugin/a/m/two", { "", "" } } };
    result = assignShortcuts( shortcuts, host, user, {} );
    REQUIRE( result[ 4 ].userSet );
    REQUIRE( result[ 4 ].keys == QStringList{ "Ctrl+Alt+D" } );
    REQUIRE( result[ 0 ].state == "refused" );
    REQUIRE( result[ 1 ].state == "cleared" );
    // The user's key is still checked against CILogg's.
    result = assignShortcuts( shortcuts, host, { { "plugin/a/m/one", { "Ctrl+O" } } }, {} );
    REQUIRE( result[ 0 ].state == "refused" );
}

TEST_CASE( "E17b ui/command starts the plugin, waits for it and reaches its handler",
           "[plugins][E17]" )
{
    MenuFixture fixture( menuManifest( "org.test.menus" ) );
    const auto id = QStringLiteral( "org.test.menus" );
    QSignalSpy updates( fixture.manager.get(), &PluginManager::menuUpdated );
    REQUIRE( fixture.manager->pluginState( id ) == "stopped" );
    // Stopped: the command waits until the plugin is ready.
    const auto first = fixture.manager->sendCommand( id, command( "toggle", { { "checked", true } } ) );
    REQUIRE( first.ok() );
    REQUIRE( first.payload.value( "queued" ).toBool() );
    REQUIRE( waitUntil( [ & ] { return recorded( fixture.folder, "ui/command" ).size() == 1; } ) );
    auto commands = recorded( fixture.folder, "ui/command" );
    REQUIRE( commands.front().value( "item_id" ) == "toggle" );
    REQUIRE( commands.front().value( "checked" ).toBool() );
    REQUIRE( commands.front().value( "origin" ) == "menu" );
    // Ready: sent at once; "rename" answers with menu/update, checked against the manifest.
    const auto second = fixture.manager->sendCommand( id, command( "rename" ) );
    REQUIRE( second.ok() );
    REQUIRE_FALSE( second.payload.value( "queued" ).toBool() );
    REQUIRE( waitUntil( [ & ] { return updates.size() == 1; } ) );
    REQUIRE( updates.at( 0 ).at( 1 ) == "main" );
    const auto changes = updates.at( 0 ).at( 2 ).toJsonArray();
    REQUIRE( changes.size() == 2 );
    REQUIRE( changes.at( 0 ).toObject().value( "set" ).toObject().value( "title" ) == "Renamed" );
    // The plugin announced both methods.
    const auto plugin = fixture.manager->plugins().front().toMap();
    REQUIRE( plugin.value( "features" ).toStringList().contains( "ui/command" ) );
    REQUIRE( plugin.value( "features" ).toStringList().contains( "menu/populate" ) );
    REQUIRE( plugin.value( "hiddenMenuLocations" ).toStringList().isEmpty() );
}

TEST_CASE( "E17b ui/command needs handles and is bounded while the plugin starts",
           "[plugins][E17]" )
{
    SECTION( "without ui/command in handles" )
    {
        MenuFixture fixture( menuManifest( "org.test.old", false ) );
        const auto result = fixture.manager->sendCommand( "org.test.old", command( "rename" ) );
        REQUIRE_FALSE( result.ok() );
        REQUIRE( result.error.contains( "newer SDK" ) );
        REQUIRE( fixture.manager->pluginState( "org.test.old" ) == "stopped" );
    }
    SECTION( "at most 16 wait" )
    {
        MenuFixture fixture( menuManifest( "org.test.queue" ) );
        int accepted = 0;
        for ( int index = 0; index < MaxQueuedCommands + 4; ++index ) {
            accepted += fixture.manager->sendCommand( "org.test.queue", command( "toggle" ) ).ok() ? 1 : 0;
        }
        REQUIRE( accepted == MaxQueuedCommands );
        REQUIRE( waitUntil( [ & ] {
            return recorded( fixture.folder, "ui/command" ).size() == MaxQueuedCommands;
        } ) );
    }
    SECTION( "a disabled plugin takes no command" )
    {
        MenuFixture fixture( menuManifest( "org.test.off" ) );
        REQUIRE( fixture.manager->manage( "disable", "org.test.off", {} ).ok() );
        REQUIRE_FALSE( fixture.manager->sendCommand( "org.test.off", command( "toggle" ) ).ok() );
        REQUIRE( fixture.manager->menuSources().empty() );
    }
}

TEST_CASE( "E17b menu/populate answers, caches and refuses invalid entries", "[plugins][E17]" )
{
    MenuFixture fixture( menuManifest( "org.test.menus" ) );
    const auto id = QStringLiteral( "org.test.menus" );
    REQUIRE( fixture.manager->startPlugin( id ).ok() );
    REQUIRE( waitUntil( [ & ] { return fixture.manager->pluginState( id ) == "ready"; } ) );
    const auto ask = [ & ]( const QString& item ) {
        auto result = std::make_shared<PluginQueries::Outcome>();
        auto done = std::make_shared<bool>( false );
        const auto queryId = fixture.manager->query(
            id, "menu/populate",
            { { "menu_id", "main" },
              { "item_id", item },
              { "location", "tab" },
              { "window_id", "w1" },
              { "context", QJsonObject{ { "location", "tab" }, { "tab_id", "tab-123456789" } } } },
            0, "menu:w1", "menu:w1",
            [ result, done ]( const PluginQueries::Outcome& outcome ) {
                *result = outcome;
                *done = true;
            } );
        REQUIRE_FALSE( queryId.isEmpty() );
        REQUIRE( waitUntil( [ & ] { return *done; } ) );
        return *result;
    };
    auto outcome = ask( "recent" );
    REQUIRE( outcome.status == PluginQueries::Status::Answered );
    const auto items = outcome.result.toObject().value( "items" ).toArray();
    REQUIRE( items.size() == 3 );
    REQUIRE( items.at( 2 ).toObject().value( "title" ) == "Tab tab-1234" );
    REQUIRE( outcome.result.toObject().value( "cache_ms" ).toInt() == 0 );
    REQUIRE( ask( "cached" ).result.toObject().value( "cache_ms" ).toInt() == 60000 );
    outcome = ask( "badmenu" );
    REQUIRE( outcome.status == PluginQueries::Status::Invalid );
    // The outgoing request carries the bounded timeout.
    const auto request = std::find_if( fixture.outgoing.begin(), fixture.outgoing.end(),
                                       []( const QJsonObject& frame ) {
                                           return frame.value( "method" ) == "menu/populate";
                                       } );
    REQUIRE( request != fixture.outgoing.end() );
    REQUIRE( request->value( "params" ).toObject().value( "timeout_ms" ).toInt() == 1000 );
}

TEST_CASE( "E17b plugin_manage menus hides locations", "[plugins][E17]" )
{
    MenuFixture fixture( menuManifest( "org.test.menus" ) );
    const auto id = QStringLiteral( "org.test.menus" );
    REQUIRE( fixture.manager
                 ->manage( "menus", id, { { "hidden_locations", QVariantList{ "log", "tab", "log" } } } )
                 .ok() );
    REQUIRE( fixture.manager->hiddenMenuLocations( id ) == QStringList{ "log", "tab" } );
    REQUIRE( fixture.manager->menuSources().front().hidden == QStringList{ "log", "tab" } );
    REQUIRE_FALSE(
        fixture.manager->manage( "menus", id, { { "hidden_locations", QVariantList{ "element" } } } )
            .ok() );
    REQUIRE( fixture.manager->manage( "menus", id, {} ).ok() );
    REQUIRE( fixture.manager->hiddenMenuLocations( id ).isEmpty() );
    // Plugin stream captures live in the data folder.
    REQUIRE( fixture.manager->isPluginCapture(
        QDir( fixture.temp.path() ).filePath( "data/captures/stream-1.log" ) ) );
    REQUIRE_FALSE( fixture.manager->isPluginCapture( QDir( fixture.temp.path() ).filePath( "x.log" ) ) );
}

TEST_CASE( "E17b get_plugin_menus and trigger_plugin_menu read request fields", "[plugins][E17]" )
{
    const auto menus = commanderRequestFromVariantMap(
        { { "action", "get_plugin_menus" }, { "location", "log" }, { "lines", QVariantList{ "10-20", 30 } } } );
    REQUIRE( menus );
    REQUIRE( menus->action == CommanderAction::GetPluginMenus );
    REQUIRE( menus->pluginArguments.value( "location" ) == "log" );
    REQUIRE( menus->pluginArguments.value( "lines" ) == "10-20,30" );
    const auto trigger = commanderRequestFromVariantMap( { { "action", "trigger_plugin_menu" },
                                                           { "pluginId", "org.test.menus" },
                                                           { "menuId", "main" },
                                                           { "itemId", "toggle" },
                                                           { "checked", false } } );
    REQUIRE( trigger );
    REQUIRE( trigger->action == CommanderAction::TriggerPluginMenu );
    REQUIRE( trigger->pluginArguments.value( "menuId" ) == "main" );
    REQUIRE( trigger->pluginArguments.value( "itemId" ) == "toggle" );
    REQUIRE( trigger->pluginArguments.contains( "checked" ) );
    REQUIRE_FALSE( trigger->pluginArguments.value( "checked" ).toBool() );
    REQUIRE( commanderActionToString( CommanderAction::TriggerPluginMenu ) == "trigger_plugin_menu" );
    const auto roundTrip = commanderRequestFromVariantMap( commanderRequestToVariantMap( *trigger ) );
    REQUIRE( roundTrip );
    REQUIRE( roundTrip->pluginArguments.value( "itemId" ) == "toggle" );
}
