// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
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
int main( int argc, char* argv[] )
{
    QCoreApplication app( argc, argv );
#ifdef Q_OS_WIN
    _setmode( _fileno( stdout ), _O_BINARY );
    _setmode( _fileno( stdin ), _O_BINARY );
#endif
    const auto mode = app.arguments().value( 1 );
    if ( app.arguments().size() > 2 ) {
        QFile marker( app.arguments().at( 2 ) );
        if ( !marker.open( QIODevice::WriteOnly ) ) {
            return 21;
        }
        marker.write( QByteArray::number( QCoreApplication::applicationPid() ) );
        marker.close();
    }
    if ( mode == "crash" ) {
        return 19;
    }
    if ( mode == "oversize" ) {
        QFile output;
        if ( !output.open( stdout, QIODevice::WriteOnly ) ) {
            return 20;
        }
        output.write( QByteArray( 4, '\xff' ) );
        output.flush();
    }
    if ( mode == "stderr_flood" ) {
        QFile errors;
        if ( !errors.open( stderr, QIODevice::WriteOnly ) ) {
            return 24;
        }
        for ( int index = 0; index < 1024; ++index ) {
            errors.write( QByteArray( 65536, 'x' ) );
            errors.flush();
        }
    }
    if ( mode == "slow_start" ) {
        QThread::msleep( 250 );
    }
    {
        QFile startupOutput;
        if ( !startupOutput.open( stdout, QIODevice::WriteOnly ) ) {
            return 25;
        }
        const auto ready = QJsonDocument( QJsonObject{ { "version", mode == "bad_ready" ? 2 : 1 },
                                                       { "operation", "ready" },
                                                       { "backend", "Qt QRegularExpression" } } )
                               .toJson( QJsonDocument::Compact );
        char header[ 4 ];
        qToBigEndian<quint32>( static_cast<quint32>( ready.size() ), header );
        startupOutput.write( header, 4 );
        startupOutput.write( ready );
        startupOutput.flush();
    }
    if ( mode == "bad_compile" || mode == "bad_match" || mode == "large_reason"
         || mode == "slow_start" || mode == "slow_compile" || mode == "preloaded_match" ) {
        QFile input;
        QFile output;
        if ( !input.open( stdin, QIODevice::ReadOnly | QIODevice::Unbuffered )
             || !output.open( stdout, QIODevice::WriteOnly ) ) {
            return 26;
        }
        QJsonObject request;
        const auto receive = [ &input, &request ] {
            const auto header = input.read( 4 );
            if ( header.size() != 4 ) {
                return false;
            }
            qint64 remaining = qFromBigEndian<quint32>( header.constData() );
            if ( remaining > 8 * 1024 * 1024 ) {
                return false;
            }
            QByteArray payload;
            while ( remaining > 0 ) {
                const auto bytes = input.read( remaining );
                if ( bytes.isEmpty() ) {
                    return false;
                }
                remaining -= bytes.size();
                payload += bytes;
            }
            request = QJsonDocument::fromJson( payload ).object();
            return true;
        };
        const auto send = [ &output, &request ]( QJsonObject message ) {
            message[ "requestId" ] = request.value( "requestId" );
            const auto bytes = QJsonDocument( message ).toJson( QJsonDocument::Compact );
            char header[ 4 ];
            qToBigEndian<quint32>( static_cast<quint32>( bytes.size() ), header );
            output.write( header, 4 );
            output.write( bytes );
            output.flush();
        };
        if ( !receive() ) {
            return 22;
        }
        if ( mode == "slow_start" || mode == "slow_compile" ) {
            if ( mode == "slow_compile" ) {
                QThread::msleep( 250 );
            }
            send( { { "version", 1 },
                    { "backend", "Qt QRegularExpression" },
                    { "metrics", QJsonObject{ { "compileUs", 1 } } } } );
            return 0;
        }
        if ( mode == "bad_match" || mode == "preloaded_match" ) {
            send( { { "version", 1 },
                    { "backend", "Qt QRegularExpression" },
                    { "metrics", QJsonObject{ { "compileUs", 1 } } } } );
            if ( mode == "preloaded_match" ) {
                send( { { "version", 1 },
                        { "backend", "Qt QRegularExpression" },
                        { "metrics", QJsonObject{ { "matchUs", 1 } } },
                        { "rows", QJsonArray{} } } );
                return 0;
            }
            if ( !receive() ) {
                return 23;
            }
        }
        if ( mode == "large_reason" ) {
            send( { { "version", 1 },
                    { "backend", "Qt QRegularExpression" },
                    { "metrics", QJsonObject{ { "compileUs", 1 } } },
                    { "reason", QString( 100000, 'x' ) } } );
        }
        else {
            send( { { "version", 1 }, { "rows", "invalid" } } );
        }
    }
    QThread::sleep( 60 );
    return 0;
}
