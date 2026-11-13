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
    // Plugin views: record every view/ui notification and answer like a small view plugin.
    const auto record = []( const QString& method, const QJsonObject& params ) {
        QFile log( "host-messages.jsonl" );
        if ( log.open( QIODevice::WriteOnly | QIODevice::Append ) ) {
            log.write( QJsonDocument( QJsonObject{ { "method", method }, { "params", params } } )
                           .toJson( QJsonDocument::Compact ) );
            log.write( "\n" );
        }
    };
    const auto rootFor = []( const QString& viewId ) {
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
    plugin.action( "ask", [ &plugin ]( cilogg_plugin::Job&, const QJsonObject& ) {
        // Message boxes need no declared view.
        plugin.message( { { "message_id", "job-ask" },
                          { "title", "Fixture" },
                          { "text", "From a job" },
                          { "buttons", QJsonArray{ "ok" } } } );
        return QJsonObject{};
    } );
    plugin.action( "stubborn", [ &plugin ]( cilogg_plugin::Job&, const QJsonObject& ) {
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
