// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewservice.h"
#include "log.h"
#include "previewdecodersettings.h"
#include "previewmanager.h"
#include "previewtree.h"
#include "pythondecoderclient.h"
#include "tsharkpreviewclient.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
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
// The least time Automatic mode gives its JSON fallback after a TShark/Lua failure, beyond the
// request's budget when the engine used it all (#182).
// The JSON preview is another selector run, a process start that a busy machine can slow down.
constexpr int FallbackGraceMs = 3000;
int activeRequests = 0; // All entry points are confined to the application thread.
QVariantMap failure( const QString& reason )
{
    return { { "version", 1 },     { "backend", "json_preview" }, { "state", "failed" },
             { "reason", reason }, { "available", false },        { "complete", false },
             { "limited", false }, { "fields", QVariantList{} } };
}
QByteArray frame( const QVariantMap& value )
{
    const auto bytes
        = QJsonDocument( QJsonObject::fromVariantMap( value ) ).toJson( QJsonDocument::Compact );
    char header[ 4 ];
    qToBigEndian<quint32>( static_cast<quint32>( bytes.size() ), header );
    return QByteArray( header, 4 ) + bytes;
}
// The input encoding of a preview's buffer type, as a Python decoder's context reports it.
QString carrierEncoding( PreviewBufferType type )
{
    switch ( type ) {
    case PreviewBufferType::HexString:
        return QStringLiteral( "hex" );
    case PreviewBufferType::Base64:
        return QStringLiteral( "base64" );
    case PreviewBufferType::Bin:
        return QStringLiteral( "bin" );
    case PreviewBufferType::Bytes:
        return QStringLiteral( "bytes" );
    case PreviewBufferType::String:
        break;
    }
    return QStringLiteral( "utf8" );
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
    static QByteArray cached;
    static bool dirty = true;
    static const auto invalidator = QObject::connect(
        &PreviewManager::instance(), &PreviewManager::previewsChanged, [] { dirty = true; } );
    Q_UNUSED( invalidator );
    if ( !dirty )
        return cached;
    dirty = false;
    const auto& manager = PreviewManager::instance();
    const auto revision = PreviewDecoderSettings::instance().revision();
    cached = definitionsFor( manager.effective(), manager.effectiveBlocks(), revision );
    // The pack check keeps enabled packs within the limits, but the user's own previews can
    // grow later: then the packs' previews give way, never the user's (design 4.2).
    if ( cached.isEmpty() && !manager.packPreviews().isEmpty() ) {
        LOG_WARNING << "Preview definitions over the limits with configuration packs; "
                       "using the user's previews only";
        cached = definitionsFor( manager.all(), manager.blocks(), revision );
    }
    return cached;
}

QByteArray PreviewService::definitionsFor( const QVector<PreviewDefinition>& previews,
                                           const QMap<QString, PreviewFieldSpec>& blocks,
                                           const QString& revision )
{
    if ( previews.size() > 128 || blocks.size() > 4096 )
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
    for ( const auto& preview : previews ) {
        if ( !stringFits( preview.name ) || !stringFits( preview.regex )
             || !stringFits( preview.bufferCapture.name )
             || !stringFits( preview.offset.expression ) || !enqueue( preview.fields, 0 ) )
            return {};
    }
    for ( auto block = blocks.cbegin(); block != blocks.cend(); ++block ) {
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
    auto object
        = QJsonDocument::fromJson( PreviewRepository::serialize( previews, blocks ) ).object();
    object[ "_decoderRuntimeRevision" ] = revision;
    const auto bytes = QJsonDocument( object ).toJson( QJsonDocument::Compact );
    return bytes.size() <= DefinitionsLimit ? bytes : QByteArray{};
}

namespace {
class EvaluationState;
QHash<QProcess*, EvaluationState*> evaluations;
const QString PythonBackend = QStringLiteral( "python_decoder" );
const QString TsharkBackend = QStringLiteral( "tshark_lua" );

// The routes that can decode now (MD03b): TShark routes while the TShark decoder is enabled, and
// Python routes whose decoder is in an enabled pack (the snapshot's `packPython`). A route to a
// decoder that is not loaded does not apply, so its preview decodes as if it had no route.
QVariantMap applicableRoutes( const QVariantMap& config )
{
    const bool tshark = config.value( "enabled" ).toBool();
    QSet<QString> loaded;
    for ( const auto& pack : config.value( "packPython" ).toList() ) {
        const auto entry = pack.toMap();
        for ( const auto& decoder : entry.value( "decoders" ).toList() )
            loaded.insert( entry.value( "id" ).toString() + '/'
                           + decoder.toMap().value( "id" ).toString() );
    }
    auto result = config;
    for ( const auto* key : { "previewRoutes", "protocolRoutes" } ) {
        QVariantMap kept;
        const auto routes = config.value( key ).toMap();
        for ( auto route = routes.cbegin(); route != routes.cend(); ++route ) {
            const auto entry = route.value().toMap();
            if ( entry.contains( "python" ) ? loaded.contains( entry.value( "python" ).toString() )
                                            : tshark )
                kept.insert( route.key(), route.value() );
        }
        result[ key ] = kept;
    }
    return result;
}
// Whether the request resolves its decoder from the routes: Automatic mode, and explicit TShark
// without a named dissector (#182).
bool resolvesRoutes( const QVariantMap& options )
{
    const auto decoder = options.value( "decoder", "auto" ).toString();
    return decoder == "auto"
           || ( decoder == "tshark" && options.value( "dissector" ).toString().isEmpty() );
}
// The assembled protocol's route that applies to this request, or empty: explicit TShark never
// takes a Python route.
QVariantMap protocolRouteFor( const QVariantMap& config, const QVariantMap& options )
{
    if ( !options.value( "previewName" ).toString().isEmpty() || !resolvesRoutes( options ) )
        return {};
    const auto routes = config.value( "protocolRoutes" ).toMap();
    const auto protocol = options.value( "assemblyProtocol" ).toString();
    if ( !routes.contains( protocol ) )
        return {};
    const auto route = routes.value( protocol ).toMap();
    if ( route.contains( "python" ) && options.value( "decoder" ) == "tshark" )
        return {};
    return route;
}
// The route for preview `name` (or the assembled protocol): `{python, inputEncoding}` for a pack's
// Python decoder, `{dissector, protocol, inputEncoding}` for TShark, or empty for JSON.
// `config` holds the applicable routes only.
QVariantMap routeFor( const QVariantMap& config, const QVariantMap& options, const QString& name )
{
    const auto decoder = options.value( "decoder", "auto" ).toString();
    if ( decoder == "json" )
        return {};
    auto route = protocolRouteFor( config, options );
    if ( route.isEmpty() )
        route = config.value( "previewRoutes" ).toMap().value( name ).toMap();
    // Explicit TShark, or a dissector named in the request, never uses a Python route.
    if ( route.contains( "python" )
         && ( decoder == "tshark" || !options.value( "dissector" ).toString().isEmpty() ) )
        route.clear();
    if ( route.contains( "python" ) ) {
        route[ "inputEncoding" ] = options.value( "inputEncoding" ).toString().isEmpty()
                                       ? route.value( "inputEncoding", config.value( "inputEncoding" ) )
                                       : options.value( "inputEncoding" );
        return route;
    }
    // The default dissector, and so any remaining route, is TShark's.
    if ( decoder == "auto" && !config.value( "enabled" ).toBool() )
        return {};
    if ( route.isEmpty() && decoder != "tshark"
         && config.value( "defaultDissector" ).toString().isEmpty() )
        return {};
    route[ "dissector" ] = options.value( "dissector" ).toString().isEmpty()
                               ? route.value( "dissector", config.value( "defaultDissector" ) )
                               : options.value( "dissector" );
    route[ "protocol" ] = route.value( "protocol", "" );
    route[ "inputEncoding" ] = options.value( "inputEncoding" ).toString().isEmpty()
                                   ? route.value( "inputEncoding", config.value( "inputEncoding" ) )
                                   : options.value( "inputEncoding" );
    return route;
}
QVariantMap optionsFor( const PreviewEvaluationRequest& request )
{
    return { { "decoder", request.decoder },
             { "previewName", request.previewName },
             { "dissector", request.dissector },
             { "inputEncoding", request.inputEncoding },
             { "assemblyProtocol", request.assemblyProtocol } };
}
class EvaluationState : public QObject {
public:
    EvaluationState( QObject* receiver, PreviewEvaluationRequest request,
                     PreviewService::Completion done )
        : QObject( QCoreApplication::instance() )
        , request_( std::move( request ) )
        , receiver_( receiver )
        , sessionOwner_( request_.sessionOwner ? request_.sessionOwner : receiver )
        , snapshot_( PreviewDecoderSettings::instance().snapshot() )
        , config_( applicableRoutes( snapshot_.configuration ) )
        , done_( std::move( done ) )
        , process_( new QProcess( QCoreApplication::instance() ) )
    {
        ++activeRequests;
        selectedPreviewName_ = request_.previewName;
        id_ = QUuid::createUuid().toString( QUuid::WithoutBraces );
        digest_ = QString::fromLatin1(
            QCryptographicHash::hash( request_.definitions, QCryptographicHash::Sha256 ).toHex() );
        elapsed_.start();
        deadlineMs_ = request_.timeoutMs;
        evaluations.insert( process_, this );
        timer_.setSingleShot( true );
        connect( &timer_, &QTimer::timeout, this, [ this ] { expire(); } );
        connect( receiver, &QObject::destroyed, this, [ this ] {
            receiver_.clear();
            stop( "cancelled" );
        } );
        if ( sessionOwner_ != receiver_ )
            connect( sessionOwner_, &QObject::destroyed, this, [ this ] {
                sessionOwner_.clear();
                stop( "cancelled" );
            } );
        connect( &PreviewDecoderSettings::instance(), &PreviewDecoderSettings::changed, this,
                 [ this ] { stop( "configuration_changed" ); } );
        connect( QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
                 [ this ] { stop( "cancelled" ); } );
        auto* token = process_.data();
        connect( token, &QObject::destroyed, this, [ this, token ] {
            evaluations.remove( token );
            process_.clear();
            stop( "cancelled" );
        } );
        connect( token, &QProcess::readyReadStandardOutput, this, [ this ] {
            if ( !process_ || completed_ )
                return;
            if ( process_->bytesAvailable() > ResponseLimit + 4 - output_.size() ) {
                stop( "result_limit" );
                return;
            }
            output_ += process_->read( ResponseLimit + 5 - output_.size() );
            if ( output_.size() >= 4
                 && qFromBigEndian<quint32>( output_.constData() ) > ResponseLimit )
                stop( "result_limit" );
        } );
        connect( token, &QProcess::readyReadStandardError, this, [ this ] {
            if ( !process_ )
                return;
            drainDiagnostics();
        } );
        connect( token, &QProcess::errorOccurred, this, [ this ]( QProcess::ProcessError error ) {
            if ( error == QProcess::FailedToStart )
                finish( failure( "worker_start_failed" ) );
        } );
        connect( token, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ), this,
                 [ this ]( int code, QProcess::ExitStatus status ) { selected( code, status ); } );
    }
    ~EvaluationState() override
    {
        if ( !completed_ ) {
            completed_ = true;
            --activeRequests;
            // Only application teardown can destroy an unfinished state. The app-owned
            // client destroys its own process trees; it may already have been destroyed.
        }
        if ( process_ ) {
            evaluations.remove( process_ );
            process_->disconnect( this );
            process_->kill();
            process_->deleteLater();
        }
    }
    QProcess* begin()
    {
        output_.clear();
        diagnostics_ = 0;
        const auto& config = config_;
        QVariantMap descriptor{ { "enabled", config.value( "enabled" ) },
                                { "defaultDissector", config.value( "defaultDissector" ) },
                                { "inputEncoding", config.value( "inputEncoding" ) },
                                { "previewRoutes", config.value( "previewRoutes" ) },
                                { "protocolRoutes", config.value( "protocolRoutes" ) } };
        auto options = optionsFor( request_ );
        auto previewName = request_.previewName;
        if ( fallback_ ) {
            options[ "decoder" ] = "json";
            options[ "dissector" ] = "";
            options[ "inputEncoding" ] = "";
            // The JSON preview of the carrier the failed route was for (#182).
            if ( previewName.isEmpty() )
                previewName = fallbackCarrier_;
        }
        const auto input = frame(
            { { "version", 1 },
              { "text", request_.text },
              { "previewName", previewName },
              { "options", options },
              { "decoderConfiguration", descriptor },
              { "definitionsBase64", QString::fromLatin1( request_.definitions.toBase64() ) } } );
        QObject::disconnect( started_ );
        started_ = connect( process_, &QProcess::started, this, [ this, input ] {
            if ( completed_ || !stopReason_.isEmpty() ) {
                if ( process_ )
                    process_->kill();
                return;
            }
            if ( process_->write( input ) != input.size() ) {
                stop( "worker_io_error" );
                return;
            }
            process_->closeWriteChannel();
        } );
        if ( !fallback_ )
            timer_.start( request_.timeoutMs );
        LOG_DEBUG << "Preview selector starting id=" << id_ << " decoder=" << request_.decoder
                  << " input_bytes=" << request_.text.toUtf8().size()
                  << " timeout_ms=" << request_.timeoutMs;
        process_->start( request_.executable.isEmpty() ? QCoreApplication::applicationFilePath()
                                                       : request_.executable,
                         { "--internal-preview-worker" } );
        return process_;
    }
    // The request's deadline. In Automatic mode a TShark/Lua engine still running gives way to the
    // JSON fallback (#182); anything else stops.
    void expire()
    {
        if ( external_ && externalBackend_ == TsharkBackend && fallsBack() ) {
            LOG_DEBUG << "Preview engine deadline id=" << id_ << " elapsed_ms=" << elapsed_.elapsed();
            // Retiring the engine's channel delivers its timeout, which starts the fallback.
            client()->cancel( id_, "timeout" );
            if ( !fallback_ && !completed_ )
                fallBack( "timeout" );
            return;
        }
        stop( "timeout" );
    }
    void stop( const QString& reason )
    {
        if ( completed_ )
            return;
        if ( stopReason_.isEmpty() )
            stopReason_ = reason;
        LOG_DEBUG << "Preview request stop id=" << id_ << " reason=" << stopReason_
                  << " external=" << external_;
        if ( external_ )
            client()->cancel( id_, stopReason_ );
        if ( completed_ )
            return;
        if ( process_ && process_->state() != QProcess::NotRunning )
            process_->kill();
        else
            finish( failure( stopReason_ ) );
    }
    void finish( QVariantMap result )
    {
        if ( completed_ )
            return;
        if ( snapshot_.revision != PreviewDecoderSettings::instance().revision()
             && stopReason_.isEmpty() )
            stopReason_ = "configuration_changed";
        if ( stopReason_.isEmpty() && result.value( "state" ) == "completed"
             && elapsed_.elapsed() >= deadlineMs_ ) {
            // Automatic mode: an engine result after the deadline gives way to the JSON fallback,
            // as an engine still running at it does (#182).
            if ( external_ && externalBackend_ == TsharkBackend && fallsBack() ) {
                client()->cancel( id_, "timeout" );
                fallBack( "timeout" );
                return;
            }
            stopReason_ = "timeout";
            LOG_DEBUG << "Preview delivery deadline expired id=" << id_
                      << " elapsed_ms=" << elapsed_.elapsed() << " deadline_ms=" << deadlineMs_
                      << " external=" << external_;
            // A native success callback has already consumed its pending completion.
            // Retire by the channel's last request identity, including that idle interval.
            if ( external_ )
                client()->cancel( id_, stopReason_ );
        }
        // The engine of this result: the worker's, or the selector's for a carrier it could not
        // prepare for a route; explicit TShark reports TShark.
        auto engine = external_ ? externalBackend_ : result.value( "backend" ).toString();
        if ( engine != PythonBackend && request_.decoder == "tshark" )
            engine = TsharkBackend;
        if ( !stopReason_.isEmpty() ) {
            // Apply authoritative failure flags without discarding already-known metadata.
            const auto stopped = failure( stopReason_ );
            for ( auto item = stopped.cbegin(); item != stopped.cend(); ++item )
                result[ item.key() ] = item.value();
            // A stopped result shows no rows, so no limit cut them (#183).
            result.remove( "limit" );
        }
        if ( engine == TsharkBackend || engine == PythonBackend ) {
            const bool python = engine == PythonBackend;
            result[ "backend" ] = engine;
            result[ "offsetSpace" ] = python ? "decoder_payload" : "engine_data_source_unresolved";
            // TShark never reports message states; a Python decoder does on a completed result.
            if ( !python || result.value( "state" ) != "completed" ) {
                result[ "messageCompleteness" ] = "not_reported";
                result[ "messageIntegrity" ] = "not_reported";
            }
            if ( result.value( "state" ) == "failed" ) {
                result[ "fields" ] = QVariantList{};
                result[ "previewName" ] = selectedPreviewName_.isEmpty()
                                              ? result.value( "previewName" ).toString()
                                              : selectedPreviewName_;
                // The worker's transport echo is redundant with the public requestId.
                result.remove( "id" );
            }
        }
        completed_ = true;
        timer_.stop();
        --activeRequests;
        result[ "requestId" ] = id_;
        result[ "definitionsSha256" ] = digest_;
        result[ "decoderRevision" ] = snapshot_.revision;
        result[ "elapsedMs" ] = elapsed_.elapsed();
        if ( fallback_ ) {
            result[ "preferredBackend" ] = fallbackFrom_;
            result[ "decoderAttempts" ] = QVariantList{ fallbackFrom_, "json_preview" };
            result[ "fallbackReason" ] = fallbackReason_;
        }
        else if ( request_.decoder == "auto" && snapshot_.configuration.value( "enabled" ).toBool()
                  && result.value( "backend" ) == "json_preview"
                  && result.value( "state" ) == "completed" ) {
            result[ "preferredBackend" ] = "tshark_lua";
            result[ "decoderAttempts" ] = QVariantList{ "json_preview" };
            result[ "fallbackReason" ] = "no_applicable_decoder";
        }
        LOG_DEBUG << "Preview request finished id=" << id_
                  << " state=" << result.value( "state" ).toString()
                  << " reason=" << result.value( "reason" ).toString()
                  << " elapsed_ms=" << elapsed_.elapsed();
        if ( process_ ) {
            auto* token = process_.data();
            evaluations.remove( token );
            process_.clear();
            token->disconnect( this );
            if ( token->state() == QProcess::NotRunning )
                token->deleteLater();
            else {
                connect( token, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ),
                         token, &QObject::deleteLater );
                token->kill();
            }
        }
        auto callback = std::move( done_ );
        if ( receiver_ && callback )
            callback( std::move( result ) );
        deleteLater();
    }
    void selected( int code, QProcess::ExitStatus status )
    {
        if ( completed_ || !process_ )
            return;
        if ( !stopReason_.isEmpty() ) {
            finish( failure( stopReason_ ) );
            return;
        }
        if ( process_->bytesAvailable() > ResponseLimit + 4 - output_.size() ) {
            finish( failure( "result_limit" ) );
            return;
        }
        output_ += process_->readAllStandardOutput();
        drainDiagnostics();
        if ( completed_ )
            return;
        if ( diagnostics_ > 65536 ) {
            finish( failure( "diagnostic_limit" ) );
            return;
        }
        if ( status != QProcess::NormalExit || code != 0 || output_.size() < 4 ) {
            finish( failure( "worker_failure" ) );
            return;
        }
        const auto size = qFromBigEndian<quint32>( output_.constData() );
        if ( size > ResponseLimit || output_.size() != static_cast<qint64>( size ) + 4 ) {
            finish( failure( "worker_protocol_error" ) );
            return;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson( output_.mid( 4 ), &error );
        if ( error.error != QJsonParseError::NoError || !document.isObject()
             || !document.object().value( "version" ).isDouble()
             || document.object().value( "version" ).toDouble() != 1 ) {
            finish( failure( "worker_protocol_error" ) );
            return;
        }
        const auto result = document.object().toVariantMap();
        if ( result.value( "backend" ) != "tshark_carrier" ) {
            auto failed = result;
            // The carrier of a route whose input the selector could not prepare; internal only.
            const auto carrier = failed.take( "carrierName" ).toString();
            // A TShark route whose input the selector could not prepare is a TShark/Lua failure:
            // the JSON preview of that carrier follows, not of any earlier match.
            if ( fallsBack() && failed.value( "backend" ) == TsharkBackend
                 && failed.value( "state" ) == "failed" ) {
                externalBackend_ = TsharkBackend;
                fallbackCarrier_ = carrier;
                fallBack( failed.value( "reason" ).toString() );
                return;
            }
            finish( failed );
            return;
        }
        external_ = true;
        const auto carrierName = result.value( "carrierName" );
        fallbackCarrier_ = carrierName.toString();
        const auto route = routeFor( config_, optionsFor( request_ ), carrierName.toString() );
        // The route's engine names every result from here on, an early timeout's too.
        const bool python = route.contains( "python" );
        externalBackend_ = python ? PythonBackend : TsharkBackend;
        const auto encoded = result.value( "payloadBase64" );
        const auto payload = QByteArray::fromBase64( encoded.toString().toLatin1() );
        if ( carrierName.userType() != QMetaType::QString || route.isEmpty()
             || route != result.value( "route" ).toMap() || encoded.userType() != QMetaType::QString
             || payload.isEmpty() || payload.size() > 65536
             || QString::fromLatin1( payload.toBase64() ) != encoded.toString() ) {
            finish( failure( "worker_protocol_error" ) );
            return;
        }
        selectedPreviewName_ = result.value( "previewName" ).toString();
        const int remaining = request_.timeoutMs - static_cast<int>( elapsed_.elapsed() );
        if ( remaining <= 0 ) {
            // A TShark route whose carrier came after the deadline: Automatic shows JSON (#182).
            if ( !python && fallsBack() ) {
                fallbackCarrier_ = carrierName.toString();
                fallBack( "timeout" );
                return;
            }
            finish( failure( "timeout" ) );
            return;
        }
        const auto name = result.value( "previewName" ).toString();
        // Automatic mode falls back to JSON after any TShark/Lua failure (#182), and after a
        // Python decoder only when it says the bytes are not its message (MD03b).
        const auto notDispatched = QStringLiteral( "decoder_not_dispatched" );
        QPointer<EvaluationState> guard( this );
        const int stage = stage_;
        LOG_DEBUG << "Preview selector completed id=" << id_ << " carrier_bytes=" << payload.size()
                  << " direct=" << carrierName.toString().isEmpty()
                  << " backend=" << externalBackend_ << " remaining_ms=" << remaining;
        auto delivered = [ guard, name, python, notDispatched, stage ]( QVariantMap response ) {
            // A stage the fallback replaced has nothing left to deliver.
            if ( !guard || guard->stage_ != stage )
                return;
            if ( !name.isEmpty() )
                response[ "previewName" ] = name;
            // A channel ended by a cancellation or a settings change (the client's own reset can
            // run before this request's stop) is not a decoder failure.
            static const QStringList stops{ "cancelled", "configuration_changed" };
            const bool failed = python ? response.value( "reason" ) == notDispatched
                                       : response.value( "state" ) != "completed"
                                             && !stops.contains( response.value( "reason" ).toString() );
            if ( guard->fallsBack() && failed ) {
                guard->fallBack( response.value( "reason" ).toString() );
                return;
            }
            guard->finish( std::move( response ) );
        };
        if ( python ) {
            // MD03b: the routed pack decoder with the session's pack modules (design 4.4).
            // `direction` is reserved: the preview does not know it yet.
            static const QStringList encodings{ "utf8", "hex", "base64", "bin", "bytes" };
            const auto encoding = result.value( "inputEncoding" ).toString();
            if ( !encodings.contains( encoding ) ) {
                finish( failure( "worker_protocol_error" ) );
                return;
            }
            const QVariantMap context{ { "preview", carrierName.toString() },
                                       { "protocol", request_.assemblyProtocol },
                                       { "inputEncoding", encoding },
                                       { "direction", "unknown" } };
            PythonDecoderClient::instance().decode(
                sessionOwner_, { { "packs", snapshot_.configuration.value( "packPython" ) } },
                route.value( "python" ).toString(), context, payload, remaining, id_,
                std::move( delivered ) );
            return;
        }
        QVariantMap profile;
        // `packLua`: the enabled packs' Lua, loaded before the user's scripts (design 4.3).
        for ( const auto* key : { "tshark", "text2pcap", "lua", "packLua" } )
            profile[ key ] = snapshot_.configuration.value( key );
        profile[ "dissector" ] = route.value( "dissector" );
        profile[ "protocol" ] = route.value( "protocol" );
        TsharkPreviewClient::instance().evaluate( sessionOwner_, profile, payload, remaining, id_,
                                                  std::move( delivered ),
                                                  request_.resultLimitKiB * 1024 );
    }

private:
    // Whether a failure now still falls back to JSON: Automatic mode, once, and not after a stop
    // (cancellation, a configuration change or the request's own deadline) or with settings that
    // changed since the request started.
    bool fallsBack() const
    {
        return request_.decoder == "auto" && !fallback_ && stopReason_.isEmpty() && !completed_
               && snapshot_.revision == PreviewDecoderSettings::instance().revision();
    }
    // Automatic mode's JSON fallback (#182): the JSON preview of the same carrier, in what is left
    // of the request's budget and at least FallbackGraceMs, so a failure at the deadline still
    // shows it. The failure stays in the result as fallbackReason.
    void fallBack( const QString& reason )
    {
        fallback_ = true;
        fallbackFrom_ = externalBackend_;
        fallbackReason_ = reason.isEmpty() ? QStringLiteral( "engine_failure" ) : reason;
        external_ = false;
        ++stage_;
        deadlineMs_ = qMax( deadlineMs_, elapsed_.elapsed() + FallbackGraceMs );
        timer_.start( static_cast<int>( deadlineMs_ - elapsed_.elapsed() ) );
        LOG_DEBUG << "Preview automatic JSON fallback id=" << id_ << " from=" << fallbackFrom_
                  << " reason=" << fallbackReason_
                  << " remaining_ms=" << deadlineMs_ - elapsed_.elapsed();
        // Not from inside the selector's or the engine's own signal handlers.
        QTimer::singleShot( 0, this, [ this ] {
            if ( !completed_ && stopReason_.isEmpty() )
                begin();
        } );
    }
    void drainDiagnostics()
    {
        if ( !process_ )
            return;
        process_->setReadChannel( QProcess::StandardError );
        const bool overflow = process_->bytesAvailable() > 65536 - diagnostics_;
        diagnostics_ += process_->read( qMax<qint64>( 0, 65537 - diagnostics_ ) ).size();
        process_->setReadChannel( QProcess::StandardOutput );
        if ( overflow || diagnostics_ > 65536 )
            stop( "diagnostic_limit" );
    }
    // The worker client of the engine stage, once the selector chose a route.
    PreviewWorkerClient* client() const
    {
        if ( externalBackend_ == PythonBackend )
            return &PythonDecoderClient::instance();
        return &TsharkPreviewClient::instance();
    }
    PreviewEvaluationRequest request_;
    QPointer<QObject> receiver_, sessionOwner_;
    PreviewDecoderSnapshot snapshot_;
    // The snapshot's configuration with the routes that can decode now (applicableRoutes).
    QVariantMap config_;
    PreviewService::Completion done_;
    QPointer<QProcess> process_;
    QTimer timer_;
    QMetaObject::Connection started_;
    QElapsedTimer elapsed_;
    QString id_, digest_, stopReason_, selectedPreviewName_;
    // The engine that decoded or was tried (tshark_lua or python_decoder) and, after an
    // automatic JSON fallback, the engine's reason; the carrier the route was for.
    QString externalBackend_, fallbackFrom_, fallbackReason_, fallbackCarrier_;
    QByteArray output_;
    qint64 diagnostics_ = 0;
    // Counts the stages (selection and engine, then the JSON fallback).
    int stage_ = 0;
    // The request's deadline, from its start: its budget, or later for a JSON fallback.
    qint64 deadlineMs_ = 0;
    bool completed_ = false, external_ = false, fallback_ = false;
};
} // namespace

QProcess* PreviewService::evaluate( QObject* owner, PreviewEvaluationRequest request,
                                    Completion done )
{
    if ( !owner )
        owner = QCoreApplication::instance();
    if ( !owner ) {
        done( failure( "application_unavailable" ) );
        return nullptr;
    }
    const auto snapshot = PreviewDecoderSettings::instance().snapshot();
    QString invalid;
    if ( request.text.size() > TextLimit || request.text.toUtf8().size() > TextLimit
         || request.previewName.size() > 4096 )
        invalid = "input_limit";
    else if ( request.timeoutMs < 100 || request.timeoutMs > 10000 )
        invalid = "invalid_timeout";
    else if ( request.resultLimitKiB < TsharkPreviewClient::ResultLimitMin / 1024
              || request.resultLimitKiB > TsharkPreviewClient::ResultLimitMax / 1024 )
        invalid = "invalid_result_limit";
    else if ( request.decoder != "auto" && request.decoder != "json"
              && request.decoder != "tshark" )
        invalid = "invalid_decoder";
    else if ( !request.dissector.isEmpty()
              && !QRegularExpression( "\\A[A-Za-z][A-Za-z0-9_.-]{0,127}\\z" )
                      .match( request.dissector )
                      .hasMatch() )
        invalid = "invalid_dissector";
    else if ( !request.inputEncoding.isEmpty() && request.inputEncoding != "utf8"
              && request.inputEncoding != "hex" && request.inputEncoding != "base64" )
        invalid = "invalid_encoding";
    else if ( request.assemblyProtocol.size() > 128 )
        invalid = "input_limit";
    else if ( request.decoder == "tshark" && !snapshot.configuration.value( "enabled" ).toBool() )
        invalid = "decoder_disabled";
    // A named dissector decodes the input directly, without any carrier definitions; without one,
    // explicit TShark looks for the line's preview route (#182).
    if ( request.decoder == "tshark" && request.previewName.isEmpty()
         && !request.dissector.isEmpty() )
        request.definitions = R"({"version":1,"previews":[]})";
    if ( invalid.isEmpty() && request.definitions.isEmpty() )
        request.definitions = definitions();
    // Definitions explicit TShark cannot use leave it the default dissector on the input, as
    // before routes were looked up (#182 review).
    if ( invalid.isEmpty() && request.decoder == "tshark" && request.previewName.isEmpty()
         && ( request.definitions.isEmpty() || request.definitions.size() > DefinitionsLimit ) )
        request.definitions = R"({"version":1,"previews":[]})";
    if ( invalid.isEmpty()
         && ( request.definitions.isEmpty() || request.definitions.size() > DefinitionsLimit ) )
        invalid = "definitions_limit";
    if ( invalid.isEmpty() && activeRequests >= 2 )
        invalid = "preview_busy";
    if ( !invalid.isEmpty() ) {
        LOG_DEBUG << "Preview evaluation rejected reason=" << invalid;
        auto result = failure( invalid );
        result[ "requestId" ] = QUuid::createUuid().toString( QUuid::WithoutBraces );
        result[ "decoderRevision" ] = snapshot.revision;
        result[ "definitionsSha256" ] = QString::fromLatin1(
            QCryptographicHash::hash( request.definitions, QCryptographicHash::Sha256 ).toHex() );
        result[ "elapsedMs" ] = 0;
        if ( request.decoder == "tshark" ) {
            result[ "backend" ] = "tshark_lua";
            result[ "offsetSpace" ] = "engine_data_source_unresolved";
            result[ "messageCompleteness" ] = "not_reported";
            result[ "messageIntegrity" ] = "not_reported";
        }
        QTimer::singleShot( 0, owner, [ done = std::move( done ), result ] { done( result ); } );
        return nullptr;
    }
    // Runtime revision is internal metadata only; repository exports remain untouched.
    auto document = QJsonDocument::fromJson( request.definitions );
    if ( document.isObject() ) {
        auto object = document.object();
        object[ "_decoderRuntimeRevision" ] = snapshot.revision;
        request.definitions = QJsonDocument( object ).toJson( QJsonDocument::Compact );
    }
    return ( new EvaluationState( owner, std::move( request ), std::move( done ) ) )->begin();
}

QVariantMap PreviewService::evaluateAndWait( PreviewEvaluationRequest request,
                                             QObject* cancellationOwner )
{
    QEventLoop loop;
    QVariantMap result;
    if ( !request.sessionOwner )
        request.sessionOwner = cancellationOwner ? cancellationOwner : QCoreApplication::instance();
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
    if ( auto* state = evaluations.value( process, nullptr ) )
        state->stop( "cancelled" );
}

QString PreviewService::pythonDecoderLabel( const QVariantMap& result )
{
    if ( result.value( "backend" ) != "python_decoder" )
        return {};
    const auto decoder = result.value( "decoder" ).toString();
    const auto pack = PreviewDecoderSettings::instance().pythonDecoderPack( decoder );
    return pack.isEmpty() ? decoder : QStringLiteral( "%1 (%2)" ).arg( decoder, pack );
}

QString PreviewService::tooltipHtml( const QVariantMap& result )
{
    const auto assembly = result.value( "assembly" ).toMap();
    QString html;
    if ( result.value( "backend" ) == "tshark_lua" )
        html = "<b>TShark / Lua</b><br>";
    else if ( result.value( "backend" ) == "python_decoder" )
        html = "<b>Python decoder</b> " + pythonDecoderLabel( result ).toHtmlEscaped() + "<br>";
    else if ( result.value( "backend" ) == "json_preview"
              && ( result.value( "available" ).toBool()
                   || ( !result.value( "reason" ).toString().isEmpty()
                        && result.value( "reason" ) != "no_match" ) ) )
        html = "<b>JSON preview</b><br>";
    if ( !html.isEmpty() && !result.value( "fallbackReason" ).toString().isEmpty() )
        html += "Fallback: " + result.value( "fallbackReason" ).toString().toHtmlEscaped() + "<br>";
    if ( !assembly.isEmpty() && assembly.value( "state" ) != "unavailable" ) {
        const auto protocol = assembly.value( "protocol" ).toString().isEmpty()
                                  ? QString( "Message" )
                                  : assembly.value( "protocol" ).toString();
        html += "<b>" + protocol.toHtmlEscaped() + " "
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
        // A Python decoder's failure names its place (exception type and file:line).
        if ( !result.value( "detail" ).toString().isEmpty() )
            html += " (" + result.value( "detail" ).toString().toHtmlEscaped() + ")";
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
    if ( result.value( "limited" ).toBool() ) {
        html += "<br><b>Limited preview results.</b>";
        const auto limit = limitText( result );
        if ( !limit.isEmpty() )
            html += " " + limit.toHtmlEscaped();
    }
    if ( abbreviated )
        html += "<br>Preview abbreviated; use Send to Preview for the full view.";
    return html;
}

QString PreviewService::limitText( const QVariantMap& result )
{
    if ( !result.value( "limited" ).toBool() )
        return {};
    // TShark's output past what the worker reads fails the whole preview (#183 review).
    if ( result.value( "backend" ) == "tshark_lua" && result.value( "state" ) == "failed" ) {
        if ( result.value( "reason" ) == "field_limit" )
            return QStringLiteral( "TShark's tree passed the worker's limit of %1 items." )
                .arg( TsharkPreviewClient::PdmlItemLimit );
        if ( result.value( "reason" ) == "output_limit" )
            return QStringLiteral(
                       "TShark's output passed the worker's limit: %1 MiB of PDML, %2 KiB of "
                       "diagnostics." )
                .arg( TsharkPreviewClient::PdmlByteLimit / ( 1024 * 1024 ) )
                .arg( TsharkPreviewClient::DiagnosticByteLimit / 1024 );
        return {};
    }
    const auto limit = result.value( "limit" ).toMap();
    const auto rows = QStringLiteral( "%1 of %2 rows" )
                          .arg( limit.value( "shownRows" ).toLongLong() )
                          .arg( limit.value( "totalRows" ).toLongLong() );
    if ( limit.value( "name" ) == "rows" )
        return QStringLiteral( "%1: the row limit is %2." )
            .arg( rows )
            .arg( limit.value( "maximum" ).toLongLong() );
    if ( limit.value( "name" ) == "bytes" ) {
        // A tree past the row limit was cut at its rows first: the bytes are those rows' bytes.
        const auto whole = limit.value( "totalRows" ).toLongLong() > TsharkPreviewClient::FieldLimit
                               ? QStringLiteral( "the first %1 rows need" )
                                     .arg( TsharkPreviewClient::FieldLimit )
                               : QStringLiteral( "the whole result needs" );
        return QStringLiteral( "%1: result limit %2 KiB, %3 %4 KiB." )
            .arg( rows )
            .arg( limit.value( "maximum" ).toLongLong() / 1024 )
            .arg( whole )
            .arg( ( limit.value( "neededBytes" ).toLongLong() + 1023 ) / 1024 );
    }
    return {};
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
        auto parsed = PreviewConfigParser().parseJson( config );
        // Explicit TShark without a preview name reads the line's route from the definitions;
        // ones it cannot parse leave it the default dissector on the input (#182 review).
        if ( !parsed.errors.isEmpty()
             && object.value( "options" ).toObject().value( "decoder" ) == "tshark"
             && object.value( "previewName" ).toString().isEmpty() )
            parsed = PreviewConfigParser().parseJson( R"({"version":1,"previews":[]})" );
        if ( !parsed.errors.isEmpty() )
            result = failure( "invalid_definitions" );
        else {
            auto name = object.value( "previewName" ).toString();
            auto options = object.value( "options" ).toObject().toVariantMap();
            options[ "previewName" ] = name;
            const auto decoderConfig
                = object.value( "decoderConfiguration" ).toObject().toVariantMap();
            // The configuration holds the applicable routes only (TShark ones while TShark is
            // enabled, Python ones whose decoder is loaded), so a route decides by being there.
            // Explicit TShark with a named dissector decodes the input directly; without one it
            // finds the route as Automatic does (#182): the assembled protocol's, then a matching
            // preview's (a TShark route), then the default dissector on the input.
            const bool explicitTshark = options.value( "decoder" ) == "tshark";
            const bool resolving = resolvesRoutes( options );
            bool direct = name.isEmpty()
                          && ( ( explicitTshark && !resolving )
                               || !protocolRouteFor( decoderConfig, options ).isEmpty() );
            const auto matches = [ & ]( const PreviewDefinition& preview ) {
                return preview.enabled && ( name.isEmpty() || preview.name == name )
                       && preview.compiled.match( text ).hasMatch();
            };
            auto definition = parsed.previews.cend();
            if ( !direct && name.isEmpty() && resolving
                 && !decoderConfig.value( "previewRoutes" ).toMap().isEmpty() ) {
                const auto mapped = decoderConfig.value( "previewRoutes" ).toMap();
                definition = std::find_if(
                    parsed.previews.cbegin(), parsed.previews.cend(),
                    [ & ]( const PreviewDefinition& preview ) {
                        return mapped.contains( preview.name )
                               && !( explicitTshark
                                     && mapped.value( preview.name ).toMap().contains( "python" ) )
                               && matches( preview );
                    } );
                LOG_DEBUG << "Preview mapped carrier selected="
                          << ( definition != parsed.previews.cend() )
                          << " explicit=" << explicitTshark;
            }
            // Explicit TShark with no route for the line: the default dissector on the input.
            if ( !direct && definition == parsed.previews.cend() && explicitTshark
                 && name.isEmpty() )
                direct = true;
            if ( !direct && definition == parsed.previews.cend() )
                definition
                    = std::find_if( parsed.previews.cbegin(), parsed.previews.cend(), matches );
            if ( !direct && definition == parsed.previews.cend() && name.isEmpty()
                 && options.value( "decoder", "auto" ) == "auto"
                 && decoderConfig.value( "enabled" ).toBool()
                 && !decoderConfig.value( "defaultDissector" ).toString().isEmpty() )
                direct = true;
            if ( definition != parsed.previews.cend() )
                name = definition->name;
            const auto route = routeFor( decoderConfig, options, name );
            const bool python = route.contains( "python" );
            if ( !route.isEmpty() && !python && route.value( "dissector" ).toString().isEmpty() ) {
                result = failure( "invalid_dissector" );
                result[ "backend" ] = "tshark_lua";
                // As for a carrier that cannot be prepared: the fallback's preview (#182 review).
                result[ "carrierName" ] = direct ? QString() : name;
            }
            else if ( !route.isEmpty() ) {
                const auto prepared
                    = direct
                          ? preparePreviewInput( text, route.value( "inputEncoding" ).toString() )
                      : definition != parsed.previews.cend()
                          ? preparePreviewBuffer( text, *definition,
                                                  options.value( "inputEncoding" ).toString() )
                          : PreviewPreparedBuffer{};
                const auto payload = prepared.bytes.mid( prepared.initialOffset );
                if ( !prepared.ok || payload.isEmpty() || payload.size() > 65536 ) {
                    result = failure(
                        !prepared.ok ? ( prepared.reason.isEmpty() ? "no_match" : prepared.reason )
                                     : "input_limit" );
                    result[ "backend" ] = python ? "python_decoder" : "tshark_lua";
                    // The carrier the failed route was for, so that Automatic's JSON fallback
                    // shows that preview (#182). The host removes it from the result.
                    result[ "carrierName" ] = direct ? QString() : name;
                    LOG_DEBUG << "Preview carrier failed reason="
                              << result.value( "reason" ).toString();
                }
                else {
                    // How the log text became the payload bytes (a Python decoder's context):
                    // the route's encoding for direct input, the request's override, or the
                    // preview's buffer type.
                    const auto encoding
                        = direct ? route.value( "inputEncoding" ).toString()
                          : !options.value( "inputEncoding" ).toString().isEmpty()
                              ? options.value( "inputEncoding" ).toString()
                              : carrierEncoding( definition->type );
                    result = {
                        { "version", 1 },
                        { "backend", "tshark_carrier" },
                        { "state", "processing" },
                        { "available", false },
                        { "complete", false },
                        // Identity stays empty for direct input; previewName is presentation only.
                        { "carrierName", direct ? QString{} : name },
                        { "previewName",
                          direct ? options.value( "assemblyProtocol" ).toString() : name },
                        { "route", route },
                        { "inputEncoding", encoding },
                        { "payloadBase64", QString::fromLatin1( payload.toBase64() ) }
                    };
                    LOG_DEBUG << "Preview carrier selected direct=" << direct
                              << " bytes=" << payload.size();
                }
            }
            else if ( !options.value( "dissector" ).toString().isEmpty()
                      || !options.value( "inputEncoding" ).toString().isEmpty() ) {
                result = failure( "unsupported_decoder_options" );
                LOG_DEBUG << "Preview legacy selection rejected decoder overrides";
            }
            else {
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
                            fields.push_back( QVariantMap{ { "columns", columns },
                                                           { "depth", current.second } } );
                            if ( QJsonDocument::fromVariant( fields )
                                     .toJson( QJsonDocument::Compact )
                                     .size()
                                 > ResponseLimit - 4096 ) {
                                fields.removeLast();
                                limited = true;
                                break;
                            }
                            for ( int i = current.first->childCount() - 1; i >= 0; --i )
                                stack.push_back(
                                    { current.first->child( i ), current.second + 1 } );
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
    } // Legacy rendering branch.
    const auto response = frame( result );
    if ( response.size() > ResponseLimit + 4 || output.write( response ) != response.size()
         || !output.flush() )
        return 3;
    return 0;
}
