// SPDX-License-Identifier: GPL-3.0-or-later
// A trusted test interpreter substitute; no external engine is launched.
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QtEndian>
#include <cstdio>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif
namespace {
QByteArray exact( QFile& input, int count )
{
    QByteArray result;
    while ( result.size() < count ) {
        const auto part = input.read( count - result.size() );
        if ( part.isEmpty() )
            return {};
        result += part;
    }
    return result;
}
QByteArray frame( const QJsonObject& value )
{
    const auto bytes = QJsonDocument( value ).toJson( QJsonDocument::Compact );
    char header[ 4 ];
    qToBigEndian<quint32>( static_cast<quint32>( bytes.size() ), header );
    return QByteArray( header, 4 ) + bytes;
}
// A fake TShark for the decoder settings dialog (issue #180): `-G folders` reports the folders the
// environment gives (or a global plugin folder outside it, CILOGG_FAKE_TSHARK_MODE=unisolated),
// `-G protocols` a short list (slowly: "slow", or failing: "fail"). Each call is appended to
// CILOGG_FAKE_TSHARK_LOG.
int fakeTshark( const QByteArray& report )
{
    const auto mode = qEnvironmentVariable( "CILOGG_FAKE_TSHARK_MODE" );
    const auto log = qEnvironmentVariable( "CILOGG_FAKE_TSHARK_LOG" );
    if ( !log.isEmpty() ) {
        QFile file( log );
        if ( file.open( QIODevice::Append ) )
            file.write( report + '\n' );
    }
    QByteArray output;
    if ( report == "folders" ) {
        const auto config = qEnvironmentVariable( "WIRESHARK_CONFIG_DIR" );
        const auto plugins = mode == "unisolated" ? QStringLiteral( "/usr/lib/wireshark/plugins" )
                                                  : qEnvironmentVariable( "WIRESHARK_PLUGIN_DIR" );
        output = QStringLiteral( "Temp:\t/tmp\n"
                                 "Personal configuration:\t%1\n"
                                 "Personal Plugins:\t%1/plugins/4.6\n"
                                 "Global Plugins:\t%2/4.6\n"
                                 "Personal Lua Plugins:\t%1/plugins\n"
                                 "Global Lua Plugins:\t%2\n" )
                     .arg( config, plugins )
                     .toUtf8();
    }
    else if ( report == "protocols" ) {
        if ( mode == "fail" )
            return 3;
        if ( mode == "slow" )
            QThread::msleep( 1500 );
        output = "Data\tData\tdata\tT\tT\tF\n"
                 "Fake protocol\tFAKE\tfake_tshark_proto\tT\tT\tT\n";
    }
    else
        return 1;
    std::fwrite( output.constData(), 1, static_cast<size_t>( output.size() ), stdout );
    std::fflush( stdout );
    return 0;
}
} // namespace
int main( int argc, char** argv )
{
#ifdef Q_OS_WIN
    _setmode( _fileno( stdin ), _O_BINARY );
    _setmode( _fileno( stdout ), _O_BINARY );
#endif
    QCoreApplication app( argc, argv );
    if ( argc >= 3 && QByteArray( argv[ 1 ] ) == "-G" )
        return fakeTshark( QByteArray( argv[ 2 ] ) );
    // Opt-in cold-start simulation for supervision tests; keep normal fixtures fast.
    if ( qEnvironmentVariableIsSet( "CILOGG_PREVIEW_FIXTURE_COLD_START" ) )
        QThread::msleep( 1200 );
    QFile input, output;
    if ( !input.open( stdin, QIODevice::ReadOnly | QIODevice::Unbuffered )
         || !output.open( stdout, QIODevice::WriteOnly ) )
        return 2;
    while ( true ) {
        const auto header = exact( input, 4 );
        if ( header.size() != 4 )
            return 0;
        const auto size = qFromBigEndian<quint32>( header.constData() );
        if ( size > 256 * 1024 )
            return 2;
        const auto request
            = QJsonDocument::fromJson( exact( input, static_cast<int>( size ) ) ).object();
        const auto mode = qEnvironmentVariable( "CILOGG_PREVIEW_FIXTURE_MODE" );
        if ( mode == "crash" )
            return 42;
        if ( mode == "stall" ) {
            QThread::sleep( 30 );
            return 0;
        }
        if ( mode == "stderr" ) {
            const auto flood = QByteArray( 65537, 'x' );
            std::fwrite( flood.constData(), 1, static_cast<size_t>( flood.size() ), stderr );
            std::fflush( stderr );
            QThread::sleep( 30 );
            return 0;
        }
        QJsonObject attributes{ { "name", "test.item" },
                                { "show", "" },
                                { "value", "01" },
                                { "pos", "9007199254740993" },
                                { "size", "1" } };
        if ( mode == "budget" )
            attributes[ "show" ] = QString::number( request.value( "timeoutMs" ).toInt() );
        QJsonObject field{ { "id", "0/0" },
                           { "depth", 0 },
                           { "positionOrigin", "engine_data_source_unresolved" },
                           { "columns", QJsonArray{ "test.item", attributes.value( "show" ), "01",
                                                    "9007199254740993", "1" } },
                           { "attributes", attributes } };
        if ( mode == "attributes" ) {
            attributes[ "show" ] = 1;
            field[ "attributes" ] = attributes;
        }
        if ( mode == "depth" )
            field[ "depth" ] = 33;
        QJsonArray fields;
        for ( int i = 0; i < ( mode == "count" ? 513 : 1 ); ++i )
            fields.append( field );
        QJsonObject response{
            { "version", 1 },
            { "id", request.value( "id" ) },
            { "backend", "tshark_lua" },
            { "state", "completed" },
            { "reason", "" },
            { "available", true },
            { "complete", true },
            { "limited", false },
            { "previewName", "test" },
            { "offsetSpace", "engine_data_source_unresolved" },
            { "messageCompleteness", "not_reported" },
            { "messageIntegrity", "not_reported" },
            { "scriptSha256", QJsonArray{} },
            { "fields", fields },
            // Intentionally unrelated to this fixture process: must remain metadata only.
            { "enginePid", qEnvironmentVariable( "CILOGG_PREVIEW_FIXTURE_PID" ).toDouble() }
        };
        if ( mode == "version" )
            response[ "version" ] = true;
        if ( mode == "id" )
            response[ "id" ] = "wrong";
        if ( mode == "metadata" )
            response.remove( "enginePid" );
        if ( mode == "flags" )
            response[ "complete" ] = false;
        if ( mode == "hash" )
            response[ "scriptSha256" ] = QJsonArray{ "not-a-hash" };
        if ( mode == "hash_newline" )
            response[ "scriptSha256" ] = QJsonArray{ QString( 64, 'a' ) + "\n" };
        if ( mode == "reported_timeout" ) {
            response[ "state" ] = "failed";
            response[ "reason" ] = "timeout";
            response[ "available" ] = false;
            response[ "complete" ] = false;
            response[ "fields" ] = QJsonArray{};
        }
        auto bytes = frame( response );
        if ( mode == "surplus" )
            bytes += bytes;
        if ( mode == "large" ) {
            char prefix[ 4 ];
            qToBigEndian<quint32>( 128 * 1024 + 1, prefix );
            bytes = QByteArray( prefix, 4 );
        }
        if ( output.write( bytes ) != bytes.size() || !output.flush() )
            return 3;
    }
}
