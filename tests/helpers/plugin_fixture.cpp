#include "cilogg_plugin.h"
#include <QDir>
#include <QFileInfo>
#include <chrono>
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
    plugin.action( "source", [ &plugin ]( cilogg_plugin::Job& job, const QJsonObject& ) {
        const auto stream = plugin.openStream( job );
        try {
            plugin.writeStream(
                job, stream,
                { "one", "two",
                  QString::fromUtf8( "Unicode: \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D" ) } );
            plugin.closeStream( stream );
        } catch ( ... ) {
            plugin.closeStream( stream );
            throw;
        }
        return QJsonObject{ { "records", 3 } };
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
    plugin.action( "crash", []( cilogg_plugin::Job&, const QJsonObject& ) -> QJsonObject {
        std::exit( 17 );
    } );
    plugin.serve();
}
