// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "captureevaluator.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <cstdio>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#else
#include <sys/resource.h>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#endif
#endif

namespace {
constexpr qint64 FrameLimit = 8 * 1024 * 1024;
QByteArray readExact( QFile& file, qint64 length )
{
    QByteArray bytes;
    while ( bytes.size() < length ) {
        const auto part = file.read( length - bytes.size() );
        if ( part.isEmpty() ) {
            return {};
        }
        bytes += part;
    }
    return bytes;
}
bool writeFrame( QFile& file, QVariantMap message )
{
    message[ "version" ] = 1;
    const auto bytes
        = QJsonDocument( QJsonObject::fromVariantMap( message ) ).toJson( QJsonDocument::Compact );
    if ( bytes.size() > FrameLimit ) {
        return false;
    }
    char header[ 4 ];
    qToBigEndian<quint32>( static_cast<quint32>( bytes.size() ), header );
    return file.write( header, 4 ) == 4 && file.write( bytes ) == bytes.size() && file.flush();
}
} // namespace

int runAnalysisWorker( int argc, char* argv[] )
{
#ifdef Q_OS_WIN
    _setmode( _fileno( stdin ), _O_BINARY );
    _setmode( _fileno( stdout ), _O_BINARY );
#else
    // Limit additional allocation by arbitrary compilation/matching in this child.
    struct rlimit memory{};
    if ( getrlimit( RLIMIT_AS, &memory ) != 0 ) {
        return 2;
    }
    rlim_t workerAddressSpace = 1024ULL * 1024 * 1024;
#ifdef Q_OS_MACOS
    // Mach-O's page-zero reservation and the shared cache already occupy address
    // space before main(). XNU rejects a limit below the existing mapped size.
    mach_task_basic_info_data_t baseline{};
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if ( task_info( mach_task_self(), MACH_TASK_BASIC_INFO,
                    reinterpret_cast<task_info_t>( &baseline ), &infoCount )
             != KERN_SUCCESS
         || baseline.virtual_size > RLIM_INFINITY - 512ULL * 1024 * 1024 ) {
        return 2;
    }
    workerAddressSpace = static_cast<rlim_t>( baseline.virtual_size ) + 512ULL * 1024 * 1024;
#endif
    memory.rlim_cur = qMin( memory.rlim_max, workerAddressSpace );
    if ( setrlimit( RLIMIT_AS, &memory ) != 0 ) {
        return 2;
    }
#endif
    qInstallMessageHandler( []( QtMsgType, const QMessageLogContext&, const QString& message ) {
        const auto bytes = message.toUtf8();
        std::fwrite( bytes.constData(), 1, static_cast<size_t>( bytes.size() ), stderr );
        std::fputc( '\n', stderr );
    } );
    QCoreApplication app( argc, argv );
    QFile input;
    QFile output;
    if ( !input.open( stdin, QIODevice::ReadOnly | QIODevice::Unbuffered )
         || !output.open( stdout, QIODevice::WriteOnly ) ) {
        return 2;
    }
    std::unique_ptr<CaptureEvaluator> evaluator;
    if ( !writeFrame( output,
                      { { "operation", "ready" }, { "backend", "Qt QRegularExpression" } } ) ) {
        return 4;
    }
    for ( ;; ) {
        const auto header = readExact( input, 4 );
        if ( header.isEmpty() ) {
            return 0;
        }
        const qint64 length = qFromBigEndian<quint32>( header.constData() );
        if ( length == 0 || length > FrameLimit ) {
            return 3;
        }
        const auto bytes = readExact( input, length );
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson( bytes, &parseError );
        if ( bytes.size() != length || parseError.error != QJsonParseError::NoError
             || !document.isObject() ) {
            return 3;
        }
        const auto message = document.object().toVariantMap();
        if ( message.value( "version" ).toInt() != 1
             || !document.object().value( "requestId" ).isDouble()
             || document.object().value( "requestId" ).toDouble() <= 0 ) {
            return 3;
        }
        QVariantMap response;
        try {
            if ( message.value( "operation" ) == "compile" && !evaluator ) {
                evaluator
                    = std::make_unique<CaptureEvaluator>( message.value( "options" ).toMap() );
                response = evaluator->compilation();
            }
            else if ( message.value( "operation" ) == "match" && evaluator ) {
                const auto lines = message.value( "lines" ).toList();
                if ( lines.size() > 256 ) {
                    return 3;
                }
                response = evaluator->evaluate( lines );
            }
            else {
                return 3;
            }
        } catch ( const std::bad_alloc& ) {
            response = { { "reason", "worker_resource_limit" },
                         { "backend", "Qt QRegularExpression" },
                         { "metrics", QVariantMap{ { evaluator ? "matchUs" : "compileUs", 0 } } } };
        } catch ( const std::exception& ) {
            response = { { "reason", "worker_failure" },
                         { "backend", "Qt QRegularExpression" },
                         { "metrics", QVariantMap{ { evaluator ? "matchUs" : "compileUs", 0 } } } };
        }
        response[ "requestId" ] = message.value( "requestId" );
        if ( !writeFrame( output, response ) ) {
            return 4;
        }
    }
}
