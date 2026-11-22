#include "cilogg_plugin.h"
#include <QDir>
#include <QFileInfo>
#include <chrono>
#include <cstdlib>
#include <thread>

int main( int argc, char** argv )
{
    const auto mode = argc > 1 ? QString::fromUtf8( argv[ 1 ] ) : QString{};
    if ( mode == "malformed" || mode == "oversize" || mode == "incompatible" || mode == "fragmented"
         || mode == "slow" || mode == "invalid-response" || mode == "silent"
         || mode == "hang-shutdown" || mode == "dynamic-config" ) {
        std::string line;
        std::getline( std::cin, line );
        QJsonObject configuration = QJsonDocument::fromJson( QByteArray::fromStdString( line ) )
                                        .object()[ "params" ]
                                        .toObject()[ "configuration" ]
                                        .toObject();
        if ( mode == "slow" || mode == "dynamic-config" ) {
            std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
        }
        if ( mode == "malformed" ) {
            std::cout << "not JSON\n" << std::flush;
        }
        else if ( mode == "oversize" ) {
            std::cout << std::string( 1024 * 1024 + 2, 'x' ) << '\n' << std::flush;
        }
        else if ( mode != "silent" ) {
            QFile file( "plugin.json" );
            if ( !file.open( QIODevice::ReadOnly ) ) {
                return 2;
            }
            const auto manifest = QJsonDocument::fromJson( file.readAll() ).object();
            const auto response
                = QJsonDocument(
                      QJsonObject{
                          { "jsonrpc", "2.0" },
                          { "id", "initialize" },
                          { "result",
                            QJsonObject{ { "plugin_id", manifest.value( "id" ) },
                                         { "protocol_version", mode == "incompatible" ? 99 : 1 },
                                         { "capabilities", manifest.value( "capabilities" ) },
                                         { "actions", manifest.value( "actions" ) },
                                         { "configuration",
                                           mode == "dynamic-config"
                                               ? QJsonArray{ QJsonObject{ { "id", "count" },
                                                                          { "type", "integer" },
                                                                          { "default", 7 } } }
                                               : QJsonArray{} } } } } )
                      .toJson( QJsonDocument::Compact );
            const auto split = response.size() / 2;
            std::cout.write( response.constData(), split );
            std::cout.flush();
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
            std::cout.write( response.constData() + split, response.size() - split );
            std::cout << '\n' << std::flush;
        }
        while ( std::getline( std::cin, line ) ) {
            const auto frame
                = QJsonDocument::fromJson( QByteArray::fromStdString( line ) ).object();
            if ( frame.value( "method" ) == "configure" ) {
                configuration = frame[ "params" ].toObject()[ "configuration" ].toObject();
            }
            if ( frame.value( "method" ) == "shutdown" ) {
                QFile marker( "shutdown.received" );
                if ( !marker.open( QIODevice::WriteOnly ) ) {
                    return 2;
                }
                marker.write( "shutdown" );
                marker.close();
                if ( mode == "hang-shutdown" ) {
                    std::this_thread::sleep_for( std::chrono::seconds( 10 ) );
                }
                return 0;
            }
            if ( frame.value( "method" ) == "invoke" ) {
                QFile marker( "invoked" );
                if ( !marker.open( QIODevice::WriteOnly ) ) {
                    return 2;
                }
                marker.close();
                if ( mode == "invalid-response" ) {
                    std::cout << QJsonDocument( QJsonObject{ { "jsonrpc", "2.0" },
                                                             { "id", frame.value( "id" ) } } )
                                     .toJson( QJsonDocument::Compact )
                                     .constData()
                              << '\n'
                              << std::flush;
                    continue;
                }
                std::cout << QJsonDocument(
                                 QJsonObject{ { "jsonrpc", "2.0" },
                                              { "id", frame.value( "id" ) },
                                              { "result", QJsonObject{ { "ok", true },
                                                                       { "configuration",
                                                                         configuration } } } } )
                                 .toJson( QJsonDocument::Compact )
                                 .constData()
                          << '\n'
                          << std::flush;
            }
        }
        return 0;
    }
    cilogg_plugin::Server plugin;
    // E17a host queries (docs/plugin-sdk-advanced.md sections 5 and 9): the answer depends on the
    // element. "no-queries" registers no handler, so the initialize result lists no features.
    std::atomic<int> counted{ 0 };
    std::atomic<int> populated{ 0 };
    if ( mode != "no-queries" ) {
        plugin.onQuery( "tooltip/resolve", [ & ]( const cilogg_plugin::Query& query ) -> QJsonValue {
            const auto params = query.params;
            const auto element = params.value( "element_id" ).toString();
            if ( element == "nulltip" ) {
                return QJsonValue::Null;
            }
            if ( element == "errortip" ) {
                throw std::runtime_error( "tooltip failed" );
            }
            if ( element == "invalidtip" ) {
                return QJsonArray{ 1, 2 };
            }
            if ( element == "undefinedtip" ) {
                return params.value( "no-such-key" ); // undefined: the SDK answers null
            }
            if ( element == "throwtip" ) {
                throw 42; // not a std::exception: the SDK answers an error, never terminates
            }
            if ( element == "slowtip" ) {
                for ( int tick = 0; tick < 3000 && !query.expired(); ++tick ) {
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
                }
                return QJsonObject{ { "text", "slow" } };
            }
            if ( element == "latetip" ) {
                // Ignores the cancellation: answers after the host stopped waiting.
                while ( !query.expired() ) {
                    std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
                }
                std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
                std::cout << QJsonDocument( QJsonObject{ { "jsonrpc", "2.0" },
                                                         { "id", query.id },
                                                         { "result",
                                                           QJsonObject{ { "text", "late" } } } } )
                                 .toJson( QJsonDocument::Compact )
                                 .constData()
                          << '\n'
                          << std::flush;
                return QJsonValue::Null;
            }
            if ( element == "counttip" ) {
                return QJsonObject{ { "text", QString( "count %1" ).arg( ++counted ) },
                                    { "ttl_ms", 60000 } };
            }
            if ( element == "rows" ) {
                return QJsonObject{ { "text", QString( "cell %1/%2 key %3" )
                                                  .arg( params.value( "row_id" ).toString(),
                                                        params.value( "column_id" ).toString(),
                                                        params.value( "tooltip_key" ).toString() ) } };
            }
            if ( element == "nodes" ) { // E17e: a tree node's cell
                return QJsonObject{ { "text", QString( "node %1/%2 key %3" )
                                                  .arg( params.value( "node_id" ).toString(),
                                                        params.value( "column_id" ).toString(),
                                                        params.value( "tooltip_key" ).toString() ) } };
            }
            // QJsonArray{ QJsonArray{ ... } } would copy the inner array instead of nesting it.
            QJsonArray rows;
            rows.append( QJsonArray{ "Firmware", "3.2.1" } );
            return QJsonObject{ { "title", "Title <b>bold</b>" },
                                { "text", "First line\nSecond <i>line</i>" },
                                { "rows", rows },
                                { "tone", "warn" },
                                { "icon", "image:dot" },
                                { "footer", "Updated" },
                                { "ttl_ms", 60000 } };
        } );
        // E17b dynamic submenus (docs/plugin-sdk-advanced.md 7.4), by entry id.
        plugin.onQuery( "menu/populate", [ & ]( const cilogg_plugin::Query& query ) -> QJsonValue {
            const auto item = query.params.value( "item_id" ).toString();
            if ( item == "slowmenu" ) {
                while ( !query.expired() ) {
                    std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
                }
                return QJsonValue::Null;
            }
            if ( item == "badmenu" ) {
                return QJsonObject{ { "items", QJsonArray{ QJsonObject{ { "id", "x" } } } } };
            }
            const auto context = query.params.value( "context" ).toObject();
            QJsonArray items;
            items.append( QJsonObject{ { "id", "r1" },
                                       { "title", QString( "First %1" ).arg( ++populated ) },
                                       { "event", true } } );
            items.append( QJsonObject{ { "type", "separator" } } );
            items.append( QJsonObject{
                { "id", "r2" },
                { "title", "Tab " + context.value( "tab_id" ).toString().left( 8 ) },
                { "event", true } } );
            return QJsonObject{ { "items", items },
                                { "cache_ms", item == "cached" ? 60000 : 0 } };
        } );
        // E17c completion (docs/plugin-sdk-advanced.md 8.2): names starting with the text.
        plugin.onQuery( "field/complete", []( const cilogg_plugin::Query& query ) -> QJsonValue {
            const auto typed = query.params.value( "text" ).toString();
            if ( typed == "bad" ) {
                return QJsonArray{ QJsonObject{ { "label", "no value" } } };
            }
            QJsonArray items;
            for ( const auto* name : { "alpha", "alps", "beta" } ) {
                if ( QString::fromLatin1( name ).startsWith( typed ) ) {
                    items.append( QJsonObject{ { "value", QString::fromLatin1( name ) },
                                               { "label", QString::fromLatin1( name ).toUpper() } } );
                }
            }
            return QJsonObject{ { "items", items } };
        } );
        // E17e lazy tree children (docs/plugin-sdk-advanced.md 28.1), by node id.
        plugin.onQuery( "tree/children", []( const cilogg_plugin::Query& query ) -> QJsonValue {
            const auto node = query.params.value( "node_id" ).toString();
            if ( node == "slow" ) {
                while ( !query.expired() ) {
                    std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
                }
                return QJsonValue::Null;
            }
            if ( node == "bad" ) {
                return QJsonObject{ { "nodes", 5 } };
            }
            if ( node == "empty" ) {
                return QJsonValue::Null;
            }
            if ( node == "b" ) { // a list alone is an answer too
                return QJsonArray{ QJsonObject{ { "id", "b1" },
                                                { "cells", QJsonArray{ "Board B1", "off" } } } };
            }
            const auto child = []( const QString& id, const QString& name, const QString& state,
                                   const QJsonArray& children = {} ) {
                QJsonObject result{ { "id", id }, { "cells", QJsonArray{ name, state } } };
                if ( !children.isEmpty() ) {
                    result.insert( "children", children );
                }
                return result;
            };
            return QJsonObject{
                { "nodes",
                  QJsonArray{ child( node + "1", "Board 1", "ok",
                                     QJsonArray{ child( node + "1m", "Module", "" ) } ),
                              child( node + "2", "Board 2", "off" ) } } };
        } );
        // ui/command: recorded, and "rename" changes the entry through menu/update.
        plugin.onCommand( [ &plugin ]( const QJsonObject& params ) {
            QFile log( "host-messages.jsonl" );
            if ( log.open( QIODevice::WriteOnly | QIODevice::Append ) ) {
                log.write( QJsonDocument( QJsonObject{ { "method", "ui/command" },
                                                       { "params", params } } )
                               .toJson( QJsonDocument::Compact ) );
                log.write( "\n" );
            }
            if ( params.value( "item_id" ).toString() == "rename" ) {
                plugin.updateMenu(
                    params.value( "menu_id" ).toString(),
                    QJsonArray{ QJsonObject{
                        { "id", "rename" },
                        { "set", QJsonObject{ { "title", "Renamed" }, { "enabled", true } } } },
                                QJsonObject{ { "id", "toggle" },
                                             { "set", QJsonObject{ { "enabled", false } } } } } );
            }
        } );
    }
    // Plugin views: record every view/ui notification and answer like a small view plugin.
    const auto record = []( const QString& method, const QJsonObject& params ) {
        QFile log( "host-messages.jsonl" );
        if ( log.open( QIODevice::WriteOnly | QIODevice::Append ) ) {
            log.write( QJsonDocument( QJsonObject{ { "method", method }, { "params", params } } )
                           .toJson( QJsonDocument::Compact ) );
            log.write( "\n" );
        }
    };
    // E17a: elements with static and advanced tooltips and accessibility overrides.
    const auto tipsRoot = [] {
        const auto label = []( const QString& id, const QJsonObject& extra ) {
            QJsonObject element{ { "type", "label" }, { "id", id }, { "text", id } };
            for ( auto it = extra.begin(); it != extra.end(); ++it ) {
                element.insert( it.key(), it.value() );
            }
            return element;
        };
        const QJsonObject provider{ { "tooltip_provider", true } };
        return QJsonObject{
            { "type", "column" },
            { "id", "body" },
            { "children",
              QJsonArray{
                  label( "status", { { "text", "ready tips" } } ),
                  label( "tip", { { "tooltip_provider", true }, { "tooltip", "static tip" } } ),
                  label( "nulltip", { { "tooltip_provider", true }, { "tooltip", "static null" } } ),
                  label( "errortip", provider ),
                  label( "invalidtip", provider ),
                  label( "slowtip", { { "tooltip_provider", true }, { "tooltip_timeout_ms", 200 } } ),
                  label( "counttip", { { "tooltip_provider", true }, { "tooltip_key", "k1" } } ),
                  label( "plain", { { "tooltip", "<b>not bold</b>" },
                                    { "accessible_description", "described" } } ),
                  label( "named", { { "accessible_name", "Custom name" } } ),
                  QJsonObject{ { "type", "button" }, { "id", "go" }, { "label", "Go" }, { "event", true },
                               { "tooltip", "Runs it" } },
                  QJsonObject{ { "type", "field" }, { "id", "name" }, { "kind", "text" },
                               { "label", "Name" }, { "value", "" } },
                  QJsonObject{ { "type", "group" }, { "id", "box" }, { "title", "Box" },
                               { "children", QJsonArray{} } },
                  QJsonObject{
                      { "type", "table" },
                      { "id", "rows" },
                      { "columns",
                        QJsonArray{ QJsonObject{ { "id", "c" },
                                                 { "title", "C" },
                                                 { "tooltip", "<b>Column C</b>" },
                                                 { "tooltip_provider", true },
                                                 { "tooltip_key", "ck" } },
                                    QJsonObject{ { "id", "d" }, { "title", "D" } } } },
                      { "rows",
                        QJsonArray{ QJsonObject{ { "id", "r1" },
                                                 { "cells", QJsonArray{ "1", "x" } },
                                                 { "tooltip_key", "rk1" } },
                                    QJsonObject{ { "id", "r2" },
                                                 { "cells", QJsonArray{ "2", "y" } } } } } } } }
        };
    };
    // E17d: a rich table with cell objects, a check column, an element menu, Export and
    // reordering (its events are recorded like every other notification).
    const auto tablesRoot = [] {
        const auto row = []( const QString& id, const QString& name, double progress,
                             bool checked ) {
            return QJsonObject{
                { "id", id },
                { "checked", checked },
                { "icon", "status:ok" },
                { "cells",
                  QJsonArray{ QJsonObject{ { "text", name }, { "tooltip", "Board " + name } },
                              QJsonObject{ { "text", QString::number( progress * 100 ) + "%" },
                                           { "progress", progress },
                                           { "sort", progress } },
                              "" } } };
        };
        const QJsonArray columns{
            QJsonObject{ { "id", "name" }, { "title", "Name" }, { "sort_as", "natural" } },
            QJsonObject{ { "id", "done" }, { "title", "Done" }, { "align", "right" } },
            QJsonObject{ { "id", "use" }, { "title", "Use" }, { "check", true } } };
        const QJsonObject table{
            { "type", "table" },        { "id", "boards" },          { "selection", "multi" },
            { "sortable", true },       { "context_menu", "rows_menu" }, { "export", true },
            { "reorderable", true },    { "columns", columns },
            { "rows", QJsonArray{ row( "r1", "board 10", 0.5, false ),
                                  row( "r2", "board 9", 1.0, true ),
                                  row( "r3", "board 1", 0.25, false ) } } };
        return QJsonObject{
            { "type", "column" },
            { "id", "body" },
            { "children", QJsonArray{ QJsonObject{ { "type", "label" },
                                                   { "id", "status" },
                                                   { "text", "ready tables" } },
                                      table } } };
    };
    // E17e: a lazy tree with check boxes, an element menu, tooltips and reordering.
    const auto treesRoot = [] {
        const auto lazy = []( const QString& id, const QString& name ) {
            return QJsonObject{ { "id", id },
                                { "cells", QJsonArray{ name, "" } },
                                { "has_children", true },
                                { "icon", "builtin:folder" } };
        };
        const QJsonObject tree{
            { "type", "tree" },
            { "id", "nodes" },
            { "columns", QJsonArray{ QJsonObject{ { "id", "name" }, { "title", "Name" } },
                                     QJsonObject{ { "id", "state" }, { "title", "State" } } } },
            { "lazy", true },
            { "checkboxes", true },
            { "auto_tristate", true },
            { "selection", "multi" },
            { "context_menu", "nodes_menu" },
            { "tooltip_provider", true },
            { "expand_events", true },
            { "reorderable", true },
            { "nodes", QJsonArray{ lazy( "a", "Bench A" ), lazy( "b", "Bench B" ),
                                   lazy( "slow", "Slow" ), lazy( "bad", "Bad" ),
                                   lazy( "empty", "Empty" ) } } };
        return QJsonObject{
            { "type", "column" },
            { "id", "body" },
            { "children", QJsonArray{ QJsonObject{ { "type", "label" },
                                                   { "id", "status" },
                                                   { "text", "ready trees" } },
                                      tree } } };
    };
    const auto rootFor = [ tipsRoot, tablesRoot, treesRoot ]( const QString& viewId ) {
        if ( viewId == "tips" ) {
            return tipsRoot();
        }
        if ( viewId == "tables" ) {
            return tablesRoot();
        }
        if ( viewId == "trees" ) {
            return treesRoot();
        }
        return QJsonObject{
            { "type", "column" },
            { "id", "body" },
            { "children",
              QJsonArray{
                  QJsonObject{
                      { "type", "label" }, { "id", "status" }, { "text", "ready " + viewId } },
                  QJsonObject{ { "type", "field" },
                               { "id", "name" },
                               { "kind", "text" },
                               { "label", "Name" },
                               { "value", "" } },
                  QJsonObject{ { "type", "field" },
                               { "id", "mode" },
                               { "kind", "choice" },
                               { "choices", QJsonArray{ "a", "b" } },
                               { "value", "a" } },
                  QJsonObject{ { "type", "button" },
                               { "id", "run" },
                               { "label", "Run" },
                               { "action", "summary" } },
                  QJsonObject{ { "type", "button" },
                               { "id", "ping" },
                               { "label", "Ping" },
                               { "event", true } },
                  QJsonObject{
                      { "type", "table" },
                      { "id", "rows" },
                      { "columns",
                        QJsonArray{ QJsonObject{
                            { "id", "c" }, { "title", "C" }, { "editable", true } } } },
                      { "rows",
                        QJsonArray{
                            QJsonObject{
                                { "id", "r1" },
                                { "cells", QJsonArray{ "1" } },
                                { "editors",
                                  QJsonObject{ { "c", QJsonObject{ { "kind", "text" } } } } } },
                            QJsonObject{ { "id", "r2" }, { "cells", QJsonArray{ "2" } } } } } } } }
        };
    };
    const auto status = [ &plugin ]( const QString& viewId, const QString& windowId,
                                     const QString& text ) {
        plugin.patchView( viewId,
                          QJsonArray{ QJsonObject{ { "id", "status" },
                                                   { "set", QJsonObject{ { "text", text } } } } },
                          windowId );
    };
    plugin.onNotification( [ & ]( const QString& method, const QJsonObject& params ) {
        record( method, params );
        const auto view = params.value( "view_id" ).toString();
        const auto window = params.value( "window_id" ).toString();
        if ( method == "view/opened" ) {
            plugin.updateView( view, rootFor( view ), window );
        }
        else if ( method == "ui/message_result" ) {
            plugin.patchView(
                "main",
                QJsonArray{ QJsonObject{
                    { "id", "status" },
                    { "set",
                      QJsonObject{ { "text", QString( "answer:%1:%2:%3" )
                                                 .arg( params.value( "message_id" ).toString(),
                                                       params.value( "button" ).toString(),
                                                       params.value( "timed_out" ).toBool()
                                                           ? "timeout"
                                                           : "user" ) } } } } } );
        }
        else if ( method == "view/event" ) {
            const auto element = params.value( "element_id" ).toString();
            if ( params.value( "type" ).toString() == "edited" ) {
                return; // never patched: the edit stays pending
            }
            const QJsonObject box{ { "title", "Fixture" },
                                   { "text", "Proceed?" },
                                   { "icon", "question" },
                                   { "buttons", QJsonArray{ "yes", "no" } },
                                   { "default", "no" } };
            if ( element == "ping" ) {
                status( view, window,
                        "pong:" + params.value( "inputs" ).toObject().value( "name" ).toString() );
            }
            else if ( element == "show_dialog" ) {
                plugin.showView( "dialog" );
            }
            else if ( element == "show_left" ) {
                plugin.showView( "side", { { "area", "left" }, { "title", "Shown side" } } );
            }
            else if ( element == "hide_dialog" ) {
                plugin.hideView( "dialog" );
            }
            else if ( element == "ask" ) {
                auto ask = box;
                ask.insert( "message_id", "ask" );
                plugin.message( ask );
            }
            else if ( element == "ask_timeout" ) {
                auto ask = box;
                ask.insert( "message_id", "timed" );
                ask.insert( "buttons", QJsonArray{ "ok", "cancel" } );
                ask.insert( "default", "ok" );
                ask.insert( "timeout_ms", 3000 );
                plugin.message( ask );
            }
            else if ( element == "ask_closable" ) {
                auto ask = box;
                ask.insert( "message_id", "closable" );
                ask.insert( "modal", false );
                plugin.message( ask );
            }
            else if ( element == "close_message" ) {
                plugin.closeMessage( "closable" );
            }
            else if ( element == "ask_many" ) {
                for ( int index = 0; index < 9; ++index ) {
                    auto ask = box;
                    ask.insert( "message_id", QString( "many-%1" ).arg( index ) );
                    ask.insert( "modal", false );
                    plugin.message( ask );
                }
            }
        }
    } );
    plugin.action( "env", [ &plugin ]( cilogg_plugin::Job&, const QJsonObject& ) {
        return QJsonObject{ { "executable", qEnvironmentVariable( "CILOGG_EXECUTABLE" ) },
                            { "instance", qEnvironmentVariable( "CILOGG_INSTANCE_ID" ) },
                            { "host_features", plugin.hostFeatures() } };
    } );
    plugin.action( "cleanup", [ &plugin ]( cilogg_plugin::Job& job, const QJsonObject& ) {
        // First progress: the action runs. The host shows a job as running once it sends it, and
        // the SDK answers a job cancelled before its action starts without running the action.
        plugin.notify( "progress", { { "job_id", job.id }, { "value", 0.1 } } );
        // Report cleanup progress after cancellation; the SDK then answers "Job cancelled".
        while ( !job.cancelled.load() && !plugin.stopping() ) {
            std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
        }
        plugin.notify( "progress", { { "job_id", job.id }, { "value", 0.9 } } );
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        return QJsonObject{ { "cleaned", true } };
    } );
    plugin.action( "convert-late", []( cilogg_plugin::Job& job, const QJsonObject& args ) {
        QFile output( args.value( "output_path" ).toString() );
        if ( !output.open( QIODevice::WriteOnly ) ) {
            throw std::runtime_error( "Cannot open converter output" );
        }
        output.write( "LATE\n" );
        output.close();
        while ( !job.cancelled.load() ) {
            std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
        }
        // A successful answer after the cancel (the SDK itself would report the cancellation).
        std::cout << QJsonDocument(
                         QJsonObject{ { "jsonrpc", "2.0" },
                                      { "id", job.id },
                                      { "result", QJsonObject{ { "converted", true } } } } )
                         .toJson( QJsonDocument::Compact )
                         .constData()
                  << '\n'
                  << std::flush;
        return QJsonObject{};
    } );
    plugin.action(
        "panel-after-cancel", [ &plugin ]( cilogg_plugin::Job& job, const QJsonObject& ) {
            while ( !job.cancelled.load() && !plugin.stopping() ) {
                std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
            }
            plugin.notify( "panel/update",
                           { { "job_id", job.id },
                             { "panel", QJsonObject{ { "id", "late" },
                                                     { "title", "Late" },
                                                     { "elements", QJsonArray{} } } } } );
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            return QJsonObject{};
        } );
    // E17a: a raw answer with a chosen id (as if this plugin answered another plugin's query),
    // registered icons and a tooltip invalidation.
    plugin.action( "respond", []( cilogg_plugin::Job&, const QJsonObject& args ) {
        std::cout << QJsonDocument( QJsonObject{ { "jsonrpc", "2.0" },
                                                 { "id", args.value( "id" ).toString() },
                                                 { "result", QJsonObject{ { "text", "foreign" } } } } )
                         .toJson( QJsonDocument::Compact )
                         .constData()
                  << '\n'
                  << std::flush;
        return QJsonObject{};
    } );
    plugin.action( "icons", [ &plugin ]( cilogg_plugin::Job&, const QJsonObject& ) {
        // A 1x1 PNG, a safe SVG and an SVG that refers to a remote file (refused by the host).
        const auto png = QByteArray::fromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==" );
        plugin.registerIcons(
            { { "dot", QJsonObject{ { "png", QString::fromLatin1( png.toBase64() ) } } },
              { "shape",
                QJsonObject{ { "svg", "<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8'>"
                                      "<rect width='8' height='8' fill='#00aa00'/></svg>" },
                             { "mask", true } } },
              { "remote",
                QJsonObject{ { "svg", "<svg xmlns='http://www.w3.org/2000/svg' "
                                      "xmlns:xlink='http://www.w3.org/1999/xlink'><image "
                                      "xlink:href='https://example.test/x.png'/></svg>" } } } } );
        return QJsonObject{};
    } );
    plugin.action( "invalidate", [ &plugin ]( cilogg_plugin::Job&, const QJsonObject& ) {
        plugin.invalidateTooltips( "tips", "counttip" );
        return QJsonObject{};
    } );
    plugin.action( "ask", [ &plugin ]( cilogg_plugin::Job&, const QJsonObject& ) {
        // Message boxes need no declared view.
        plugin.message( { { "message_id", "job-ask" },
                          { "title", "Fixture" },
                          { "text", "From a job" },
                          { "buttons", QJsonArray{ "ok" } } } );
        return QJsonObject{};
    } );
    plugin.action( "stubborn", [ &plugin ]( cilogg_plugin::Job& job, const QJsonObject& ) {
        // First progress: the action runs (see "cleanup").
        plugin.notify( "progress", { { "job_id", job.id }, { "value", 0.1 } } );
        for ( int tick = 0; tick < 3000 && !plugin.stopping(); ++tick ) {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        return QJsonObject{};
    } );
    plugin.action( "source", [ &plugin ]( cilogg_plugin::Job& job, const QJsonObject& args ) {
        const auto stream = plugin.openStream( job );
        const auto records
            = args.contains( "records" )
                  ? args.value( "records" ).toArray()
                  : QJsonArray{ "one", "two",
                                QString::fromUtf8( "Unicode: \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D" ) };
        const auto batches = qBound( 1, args.value( "batches" ).toInt( 1 ), 100 );
        try {
            for ( int batch = 0; batch < batches; ++batch ) {
                plugin.writeStream( job, stream, records );
            }
            plugin.closeStream( stream );
        } catch ( ... ) {
            plugin.closeStream( stream );
            throw;
        }
        return QJsonObject{ { "records", static_cast<double>( records.size() ) * batches } };
    } );
    plugin.action( "convert", []( cilogg_plugin::Job& job, const QJsonObject& args ) {
        job.check();
        QFile input( args.value( "input_path" ).toString() );
        QFile output( args.value( "output_path" ).toString() );
        if ( !input.open( QIODevice::ReadOnly ) || !output.open( QIODevice::WriteOnly ) ) {
            throw std::runtime_error( "Cannot open converter files" );
        }
        const auto data = input.readAll().toUpper();
        if ( output.write( data ) != data.size() ) {
            throw std::runtime_error( "Converter write failed" );
        }
        if ( args.value( "hold" ).toBool() ) {
            output.flush();
            std::cerr << "Fixture converter waiting job=" << job.id.toStdString() << std::endl;
            for ( int tick = 0; tick < 10000; ++tick ) {
                job.check();
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
        }
        return QJsonObject{ { "converted", true } };
    } );
    // What the C++ SDK's supports() says about the host this run was initialized by.
    plugin.action( "supports", [ &plugin ]( cilogg_plugin::Job&, const QJsonObject& arguments ) {
        QJsonObject result;
        for ( const auto& name : arguments.value( "features" ).toArray() ) {
            result.insert( name.toString(), plugin.supports( name.toString() ) );
            result.insert( name.toString() + "@2", plugin.supports( name.toString(), 2 ) );
        }
        return result;
    } );
    plugin.action( "summary", [ &plugin ]( cilogg_plugin::Job& job, const QJsonObject& ) {
        plugin.progress( job, 0.5 );
        plugin.panel(
            job, { { "id", "summary" },
                   { "title", "Summary" },
                   { "elements", QJsonArray{ QJsonObject{ { "type", "text" },
                                                          { "text", "Fixture summary" } } } } } );
        return QJsonObject{ { "ok", true } };
    } );
    plugin.action( "wait", []( cilogg_plugin::Job& job, const QJsonObject& ) {
        for ( int i = 0; i < 10000; ++i ) {
            job.check();
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        return QJsonObject{};
    } );
    plugin.action( "crash", []( cilogg_plugin::Job& job, const QJsonObject& ) -> QJsonObject {
        std::cerr << "Fixture abrupt exit job=" << job.id.toStdString() << std::endl;
        // Skip stream cleanup: the protocol reader can hold stdin's lock while waiting for input.
        std::_Exit( 17 );
    } );
    plugin.serve();
}
