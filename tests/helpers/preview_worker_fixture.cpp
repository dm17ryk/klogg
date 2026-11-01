// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QtEndian>
#include <cstdio>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

int main( int argc, char** argv )
{
#ifdef Q_OS_WIN
    _setmode( _fileno( stdin ), _O_BINARY );
    _setmode( _fileno( stdout ), _O_BINARY );
#endif
    QCoreApplication app( argc, argv );
    QFile input, output;
    if ( !input.open( stdin, QIODevice::ReadOnly ) || !output.open( stdout, QIODevice::WriteOnly ) )
        return 2;
    const auto header = input.read( 4 );
    if ( header.size() != 4 )
        return 2;
    const auto size = qFromBigEndian<quint32>( header.constData() );
    if ( size > 2 * 1024 * 1024 )
        return 2;
    const auto request = QJsonDocument::fromJson( input.read( size ) ).object();
    const auto mode = request.value( "text" ).toString();
    if ( mode == "stall" ) {
        QThread::sleep( 30 );
        return 0;
    }
    if ( mode == "crash" )
        return 42;
    char prefix[ 4 ];
    const auto body = QJsonDocument( QJsonObject{ { "version", mode == "invalid" ? 99 : 1 },
                                                  { "state", "completed" },
                                                  { "available", false },
                                                  { "complete", true } } )
                          .toJson( QJsonDocument::Compact );
    qToBigEndian<quint32>( mode == "large" ? 256 * 1024 : static_cast<quint32>( body.size() ),
                           prefix );
    output.write( prefix, 4 );
    output.write( body );
    output.flush();
    return 0;
}
