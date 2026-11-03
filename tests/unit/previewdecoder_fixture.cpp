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
} // namespace
int main( int argc, char** argv )
{
#ifdef Q_OS_WIN
    _setmode( _fileno( stdin ), _O_BINARY );
    _setmode( _fileno( stdout ), _O_BINARY );
#endif
    QCoreApplication app( argc, argv );
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
        QJsonObject field{ { "id", "0/0" },
                           { "depth", 0 },
                           { "positionOrigin", "engine_data_source_unresolved" },
                           { "columns",
                             QJsonArray{ "test.item", "", "01", "9007199254740993", "1" } },
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
