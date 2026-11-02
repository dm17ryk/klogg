// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewservice.h"
#include "log.h"
#include "previewmanager.h"
#include "previewtree.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <cstdio>
#include <memory>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace {
constexpr int TextLimit = 64 * 1024;
constexpr int DefinitionsLimit = 1024 * 1024;
constexpr int RequestLimit = 2 * 1024 * 1024;
constexpr int ResponseLimit = 128 * 1024;
int activeRequests = 0; // All entry points are confined to the application thread.
QVariantMap failure( const QString& reason )
{
    return { { "version", 1 },     { "backend", "json_preview" }, { "state", "failed" },
             { "reason", reason }, { "available", false },        { "complete", false } };
}
QByteArray frame( const QVariantMap& value )
{
    const auto bytes
        = QJsonDocument( QJsonObject::fromVariantMap( value ) ).toJson( QJsonDocument::Compact );
    char header[ 4 ];
    qToBigEndian<quint32>( static_cast<quint32>( bytes.size() ), header );
    return QByteArray( header, 4 ) + bytes;
}
QByteArray readExact( QFile& input, int count )
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
} // namespace

QByteArray PreviewService::definitions()
{
    const auto& manager = PreviewManager::instance();
    static QByteArray cached;
    static bool dirty = true;
    static const auto invalidator = QObject::connect(
        &PreviewManager::instance(), &PreviewManager::previewsChanged, [] { dirty = true; } );
    Q_UNUSED( invalidator );
    if ( !dirty )
        return cached;
    dirty = false;
    cached.clear();
    if ( manager.all().size() > 128 || manager.blocks().size() > 4096 )
        return {};
    QVector<QPair<const PreviewFieldSpec*, int>> pending;
    qint64 characters = 0;
    int entries = 0;
    const auto stringFits = [ & ]( const QString& text ) {
        characters += text.size();
        return characters <= 256 * 1024;
    };
    const auto enqueue = [ & ]( const QVector<PreviewFieldSpec>& fields, int depth ) {
        if ( fields.isEmpty() )
            return true;
        if ( pending.size() + fields.size() > 10000 || depth > 32 )
            return false;
        for ( const auto& field : fields )
            pending.push_back( { &field, depth } );
        return true;
    };
    for ( const auto& preview : manager.all() ) {
        if ( !stringFits( preview.name ) || !stringFits( preview.regex )
             || !stringFits( preview.bufferCapture.name )
             || !stringFits( preview.offset.expression ) || !enqueue( preview.fields, 0 ) )
            return {};
    }
    for ( auto block = manager.blocks().cbegin(); block != manager.blocks().cend(); ++block ) {
        if ( !stringFits( block.key() ) )
            return {};
        pending.push_back( { &block.value(), 0 } );
    }
    while ( !pending.isEmpty() ) {
        const auto current = pending.takeLast();
        const auto& field = *current.first;
        if ( ++entries > 10000 || !stringFits( field.name ) || !stringFits( field.regex )
             || !stringFits( field.capture.name ) || !stringFits( field.bufferCapture.name )
             || !stringFits( field.offset.expression ) || !stringFits( field.width.expression )
             || !stringFits( field.blockTemplate ) || !stringFits( field.endianness ) )
            return {};
        for ( const auto* mapping : { &field.enumMap, &field.flagMap } ) {
            if ( mapping->size() > 10000 - entries )
                return {};
            entries += static_cast<int>( mapping->size() );
            for ( auto item = mapping->cbegin(); item != mapping->cend(); ++item )
                if ( !stringFits( item.key() ) || !stringFits( item.value() ) )
                    return {};
        }
        if ( !enqueue( field.fields, current.second + 1 )
             || !enqueue( field.bitfieldMap, current.second + 1 ) )
            return {};
    }
    const auto bytes = PreviewRepository::serialize( manager.all(), manager.blocks() );
    if ( bytes.size() <= DefinitionsLimit )
        cached = bytes;
    return cached;
}

QProcess* PreviewService::evaluate( QObject* owner, PreviewEvaluationRequest request,
                                    Completion done )
{
    if ( !owner )
        owner = QCoreApplication::instance();
    if ( !owner ) {
        done( failure( "application_unavailable" ) );
        return nullptr;
    }
    QString invalid;
    if ( request.text.size() > TextLimit || request.text.toUtf8().size() > TextLimit )
        invalid = "input_limit";
    else if ( request.previewName.size() > 4096 )
        invalid = "input_limit";
    else if ( request.timeoutMs < 100 || request.timeoutMs > 10000 )
        invalid = "invalid_timeout";
    if ( invalid.isEmpty() && request.definitions.isEmpty() )
        request.definitions = definitions();
    if ( invalid.isEmpty()
         && ( request.definitions.isEmpty() || request.definitions.size() > DefinitionsLimit ) )
        invalid = "definitions_limit";
    if ( invalid.isEmpty() && activeRequests >= 2 )
        invalid = "preview_busy";
    if ( !invalid.isEmpty() ) {
        LOG_DEBUG << "Preview evaluation rejected reason=" << invalid;
        QTimer::singleShot( 0, owner,
                            [ done = std::move( done ), invalid ] { done( failure( invalid ) ); } );
        return nullptr;
    }
    struct State {
        bool done = false;
        QByteArray output;
        qint64 diagnostics = 0;
        QElapsedTimer elapsed;
        QString id;
        QString digest;
    };
    auto state = std::make_shared<State>();
    state->id = QUuid::createUuid().toString( QUuid::WithoutBraces );
    state->digest = QString::fromLatin1(
        QCryptographicHash::hash( request.definitions, QCryptographicHash::Sha256 ).toHex() );
    state->elapsed.start();
    auto* process = new QProcess( QCoreApplication::instance() );
    auto* timer = new QTimer( process );
    timer->setSingleShot( true );
    QPointer<QObject> receiver( owner );
    const auto finish
        = [ process, timer, state, receiver, done = std::move( done ) ]( QVariantMap result ) {
              if ( state->done )
                  return;
              state->done = true;
              timer->stop();
              --activeRequests;
              result[ "requestId" ] = state->id;
              result[ "definitionsSha256" ] = state->digest;
              result[ "elapsedMs" ] = state->elapsed.elapsed();
              LOG_DEBUG << "Preview evaluation finished id=" << state->id
                        << " state=" << result.value( "state" ).toString()
                        << " reason=" << result.value( "reason" ).toString()
                        << " elapsed_ms=" << state->elapsed.elapsed()
                        << " output_bytes=" << state->output.size()
                        << " diagnostic_bytes=" << state->diagnostics;
              if ( receiver )
                  done( std::move( result ) );
              process->deleteLater();
          };
    ++activeRequests;
    QObject::connect( process, &QObject::destroyed, [ state ] {
        if ( !state->done ) {
            state->done = true;
            --activeRequests;
        }
    } );
    QObject::connect( owner, &QObject::destroyed, process, [ process ] { cancel( process ); } );
    QObject::connect( QCoreApplication::instance(), &QCoreApplication::aboutToQuit, process,
                      [ process ] { cancel( process ); } );
    const auto stop = [ process ]( const QString& reason ) {
        if ( process->property( "previewReason" ).toString().isEmpty() )
            process->setProperty( "previewReason", reason );
        process->kill();
    };
    QObject::connect( timer, &QTimer::timeout, process, [ stop ] { stop( "timeout" ); } );
    QObject::connect(
        process, &QProcess::readyReadStandardOutput, process, [ process, state, stop ] {
            const auto bytes = process->readAllStandardOutput();
            if ( bytes.size() > ResponseLimit + 4 - state->output.size() ) {
                stop( "result_limit" );
                return;
            }
            state->output += bytes;
            if ( state->output.size() >= 4
                 && qFromBigEndian<quint32>( state->output.constData() ) > ResponseLimit )
                stop( "result_limit" );
        } );
    QObject::connect( process, &QProcess::readyReadStandardError, process,
                      [ process, state, stop ] {
                          state->diagnostics += process->readAllStandardError().size();
                          if ( state->diagnostics > 65536 )
                              stop( "diagnostic_limit" );
                      } );
    QObject::connect( process, &QProcess::errorOccurred, process,
                      [ finish ]( QProcess::ProcessError error ) {
                          if ( error == QProcess::FailedToStart )
                              finish( failure( "worker_start_failed" ) );
                      } );
    QObject::connect(
        process, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ), process,
        [ process, state, finish ]( int code, QProcess::ExitStatus status ) {
            const auto tail = process->readAllStandardOutput();
            state->diagnostics += process->readAllStandardError().size();
            const auto reason = process->property( "previewReason" ).toString();
            if ( !reason.isEmpty() ) {
                finish( failure( reason ) );
                return;
            }
            if ( tail.size() > ResponseLimit + 4 - state->output.size() ) {
                finish( failure( "result_limit" ) );
                return;
            }
            state->output += tail;
            if ( state->diagnostics > 65536 ) {
                finish( failure( "diagnostic_limit" ) );
                return;
            }
            if ( status != QProcess::NormalExit || code != 0 || state->output.size() < 4 ) {
                finish( failure( "worker_failure" ) );
                return;
            }
            const auto size = qFromBigEndian<quint32>( state->output.constData() );
            if ( size > ResponseLimit || state->output.size() != static_cast<qint64>( size ) + 4 ) {
                finish( failure( "worker_protocol_error" ) );
                return;
            }
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson( state->output.mid( 4 ), &error );
            if ( error.error != QJsonParseError::NoError || !document.isObject()
                 || document.object().value( "version" ).toInt() != 1 ) {
                finish( failure( "worker_protocol_error" ) );
                return;
            }
            finish( document.object().toVariantMap() );
        } );
    const auto input = frame(
        { { "version", 1 },
          { "text", request.text },
          { "previewName", request.previewName },
          { "definitionsBase64", QString::fromLatin1( request.definitions.toBase64() ) } } );
    QObject::connect( process, &QProcess::started, process, [ process, input ] {
        if ( !process->property( "previewReason" ).toString().isEmpty() ) {
            process->kill();
            return;
        }
        process->write( input );
        process->closeWriteChannel();
    } );
    LOG_DEBUG << "Preview evaluation starting id=" << state->id
              << " text_bytes=" << request.text.toUtf8().size()
              << " definitions_bytes=" << request.definitions.size()
              << " timeout_ms=" << request.timeoutMs;
    timer->start( request.timeoutMs );
    process->start( request.executable.isEmpty() ? QCoreApplication::applicationFilePath()
                                                 : request.executable,
                    { "--internal-preview-worker" } );
    return process;
}

QVariantMap PreviewService::evaluateAndWait( PreviewEvaluationRequest request,
                                             QObject* cancellationOwner )
{
    QEventLoop loop;
    QVariantMap result;
    auto* process = evaluate( &loop, std::move( request ), [ & ]( QVariantMap response ) {
        result = std::move( response );
        loop.quit();
    } );
    if ( cancellationOwner && process )
        QObject::connect( cancellationOwner, &QObject::destroyed, process,
                          [ process ] { cancel( process ); } );
    if ( result.isEmpty() )
        loop.exec();
    return result;
}

void PreviewService::cancel( QProcess* process )
{
    if ( !process || process->state() == QProcess::NotRunning )
        return;
    if ( process->property( "previewReason" ).toString().isEmpty() )
        process->setProperty( "previewReason", "cancelled" );
    process->kill();
}

QString PreviewService::tooltipHtml( const QVariantMap& result )
{
    const auto assembly = result.value( "assembly" ).toMap();
    QString html;
    if ( !assembly.isEmpty() && assembly.value( "state" ) != "unavailable" ) {
        const auto protocol = assembly.value( "protocol" ).toString().isEmpty()
                                  ? QString( "Message" )
                                  : assembly.value( "protocol" ).toString();
        html = "<b>" + protocol.toHtmlEscaped() + " "
               + assembly.value( "state" ).toString().toHtmlEscaped() + "</b><br>"
               + QString( "%1 / %2 bytes" )
                     .arg( assembly.value( "receivedBytes" ).toLongLong() )
                     .arg( assembly.value( "expectedBytes" ).toLongLong() );
        if ( !assembly.value( "reason" ).toString().isEmpty() )
            html += "<br>" + assembly.value( "reason" ).toString().toHtmlEscaped();
        html += "<br>";
    }
    if ( !result.value( "available" ).toBool() ) {
        if ( assembly.value( "state" ) == "complete" )
            html += "<pre>" + result.value( "inputText" ).toString().left( 384 ).toHtmlEscaped()
                    + "</pre>Use Send to Preview to inspect the assembled text.";
        const auto reason = result.value( "reason" ).toString();
        if ( !reason.isEmpty() && reason != "no_match" )
            html += "<br>Preview unavailable: " + reason.toHtmlEscaped();
        return html;
    }
    html += "<b>" + result.value( "previewName" ).toString().toHtmlEscaped() + "</b><table>";
    const auto fields = result.value( "fields" ).toList();
    int shown = 0;
    bool abbreviated = false;
    for ( const auto& field : fields ) {
        if ( shown++ >= 20 ) {
            abbreviated = true;
            break;
        }
        const auto columns = field.toMap().value( "columns" ).toList();
        if ( columns.isEmpty() )
            continue;
        const auto name = columns.value( 0 ).toString();
        const auto value = columns.value( 1 ).toString();
        abbreviated = abbreviated || name.size() > 128 || value.size() > 256;
        html += "<tr><td>" + name.left( 128 ).toHtmlEscaped() + "</td><td><pre>"
                + value.left( 256 ).toHtmlEscaped() + "</pre></td></tr>";
    }
    html += "</table>";
    if ( result.value( "limited" ).toBool() )
        html += "<br><b>Limited preview results.</b>";
    if ( abbreviated )
        html += "<br>Preview abbreviated; use Send to Preview for the full view.";
    return html;
}

int PreviewService::workerMain( int argc, char* argv[] )
{
#ifdef Q_OS_WIN
    _setmode( _fileno( stdin ), _O_BINARY );
    _setmode( _fileno( stdout ), _O_BINARY );
#endif
    qInstallMessageHandler( []( QtMsgType, const QMessageLogContext&, const QString& message ) {
        const auto bytes = message.toUtf8();
        std::fwrite( bytes.constData(), 1, static_cast<size_t>( bytes.size() ), stderr );
        std::fputc( '\n', stderr );
    } );
    QCoreApplication app( argc, argv );
    QFile input, output;
    if ( !input.open( stdin, QIODevice::ReadOnly | QIODevice::Unbuffered )
         || !output.open( stdout, QIODevice::WriteOnly ) )
        return 2;
    const auto header = readExact( input, 4 );
    if ( header.size() != 4 )
        return 2;
    const auto size = qFromBigEndian<quint32>( header.constData() );
    if ( size > RequestLimit )
        return 2;
    const auto bytes = readExact( input, static_cast<int>( size ) );
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson( bytes, &error );
    if ( error.error != QJsonParseError::NoError || !document.isObject()
         || document.object().value( "version" ).toInt() != 1 )
        return 2;
    const auto object = document.object();
    const auto text = object.value( "text" ).toString();
    const auto config
        = QByteArray::fromBase64( object.value( "definitionsBase64" ).toString().toLatin1() );
    QVariantMap result;
    if ( text.toUtf8().size() > TextLimit || config.size() > DefinitionsLimit )
        result = failure( "input_limit" );
    else {
        const auto parsed = PreviewConfigParser().parseJson( config );
        if ( !parsed.errors.isEmpty() )
            result = failure( "invalid_definitions" );
        else {
            auto name = object.value( "previewName" ).toString();
            const auto definition
                = std::find_if( parsed.previews.cbegin(), parsed.previews.cend(),
                                [ & ]( const PreviewDefinition& preview ) {
                                    return preview.enabled
                                           && ( name.isEmpty() || preview.name == name )
                                           && preview.compiled.match( text ).hasMatch();
                                } );
            if ( definition != parsed.previews.cend() )
                name = definition->name;
            result = { { "version", 1 },
                       { "backend", "json_preview" },
                       { "state", "completed" },
                       { "available", false },
                       { "reason", "no_match" },
                       { "previewName", name },
                       { "offsetSpace", "legacy_preview_buffer" },
                       { "complete", true } };
            if ( definition != parsed.previews.cend() ) {
                PreviewTreeBudget budget;
                QString decodeError;
                std::unique_ptr<QTreeWidgetItem> tree( createLegacyPreviewTree(
                    text, *definition, parsed.blocks, &decodeError, &budget ) );
                if ( !tree ) {
                    result[ "available" ] = true;
                    result[ "reason" ] = "decode_error";
                    result[ "fields" ] = QVariantList{ QVariantMap{
                        { "columns", QVariantList{ decodeError } }, { "depth", 0 } } };
                }
                else {
                    QVariantList fields;
                    QVector<QPair<QTreeWidgetItem*, int>> stack;
                    for ( int i = tree->childCount() - 1; i >= 0; --i )
                        stack.push_back( { tree->child( i ), 0 } );
                    bool limited = budget.limited;
                    while ( !stack.isEmpty() ) {
                        const auto current = stack.takeLast();
                        if ( fields.size() >= 512 || current.second > 32 ) {
                            limited = true;
                            break;
                        }
                        QVariantList columns;
                        for ( int column = 0; column < 5; ++column )
                            columns.push_back( current.first->text( column ) );
                        fields.push_back(
                            QVariantMap{ { "columns", columns }, { "depth", current.second } } );
                        if ( QJsonDocument::fromVariant( fields )
                                 .toJson( QJsonDocument::Compact )
                                 .size()
                             > ResponseLimit - 4096 ) {
                            fields.removeLast();
                            limited = true;
                            break;
                        }
                        for ( int i = current.first->childCount() - 1; i >= 0; --i )
                            stack.push_back( { current.first->child( i ), current.second + 1 } );
                    }
                    result[ "available" ] = true;
                    result[ "reason" ] = limited ? "result_limit" : "";
                    result[ "limited" ] = limited;
                    result[ "complete" ] = !limited;
                    result[ "fields" ] = fields;
                }
            }
        }
    }
    const auto response = frame( result );
    if ( response.size() > ResponseLimit + 4 || output.write( response ) != response.size()
         || !output.flush() )
        return 3;
    return 0;
}
