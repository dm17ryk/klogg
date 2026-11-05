// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewservice.h"
#include "log.h"
#include "previewdecodersettings.h"
#include "previewmanager.h"
#include "previewtree.h"
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
    const auto revision = PreviewDecoderSettings::instance().revision();
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
    auto object
        = QJsonDocument::fromJson( PreviewRepository::serialize( manager.all(), manager.blocks() ) )
              .object();
    object[ "_decoderRuntimeRevision" ] = revision;
    const auto bytes = QJsonDocument( object ).toJson( QJsonDocument::Compact );
    if ( bytes.size() <= DefinitionsLimit )
        cached = bytes;
    return cached;
}

namespace {
class EvaluationState;
QHash<QProcess*, EvaluationState*> evaluations;
QVariantMap routeFor( const QVariantMap& config, const QVariantMap& options, const QString& name )
{
    const auto decoder = options.value( "decoder", "auto" ).toString();
    if ( decoder == "json" || ( decoder == "auto" && !config.value( "enabled" ).toBool() ) )
        return {};
    QVariantMap route;
    if ( options.value( "previewName" ).toString().isEmpty() && decoder == "auto"
         && config.value( "protocolRoutes" )
                .toMap()
                .contains( options.value( "assemblyProtocol" ).toString() ) )
        route = config.value( "protocolRoutes" )
                    .toMap()
                    .value( options.value( "assemblyProtocol" ).toString() )
                    .toMap();
    else
        route = config.value( "previewRoutes" ).toMap().value( name ).toMap();
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
        , done_( std::move( done ) )
        , process_( new QProcess( QCoreApplication::instance() ) )
    {
        ++activeRequests;
        selectedPreviewName_ = request_.previewName;
        id_ = QUuid::createUuid().toString( QUuid::WithoutBraces );
        digest_ = QString::fromLatin1(
            QCryptographicHash::hash( request_.definitions, QCryptographicHash::Sha256 ).toHex() );
        elapsed_.start();
        evaluations.insert( process_, this );
        timer_.setSingleShot( true );
        connect( &timer_, &QTimer::timeout, this, [ this ] { stop( "timeout" ); } );
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
        const auto config = snapshot_.configuration;
        QVariantMap descriptor{ { "enabled", config.value( "enabled" ) },
                                { "defaultDissector", config.value( "defaultDissector" ) },
                                { "inputEncoding", config.value( "inputEncoding" ) },
                                { "previewRoutes", config.value( "previewRoutes" ) },
                                { "protocolRoutes", config.value( "protocolRoutes" ) } };
        auto options = optionsFor( request_ );
        if ( fallback_ ) {
            options[ "decoder" ] = "json";
            options[ "dissector" ] = "";
            options[ "inputEncoding" ] = "";
        }
        const auto input = frame(
            { { "version", 1 },
              { "text", request_.text },
              { "previewName", request_.previewName },
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
    void stop( const QString& reason )
    {
        if ( completed_ )
            return;
        if ( stopReason_.isEmpty() )
            stopReason_ = reason;
        LOG_DEBUG << "Preview request stop id=" << id_ << " reason=" << stopReason_
                  << " external=" << external_;
        if ( external_ )
            TsharkPreviewClient::instance().cancel( id_, stopReason_ );
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
             && elapsed_.elapsed() >= request_.timeoutMs ) {
            stopReason_ = "timeout";
            LOG_DEBUG << "Preview delivery deadline expired id=" << id_
                      << " elapsed_ms=" << elapsed_.elapsed()
                      << " timeout_ms=" << request_.timeoutMs << " external=" << external_;
            // A native success callback has already consumed its pending completion.
            // Retire by the channel's last request identity, including that idle interval.
            if ( external_ )
                TsharkPreviewClient::instance().cancel( id_, stopReason_ );
        }
        if ( !stopReason_.isEmpty() ) {
            // Apply authoritative failure flags without discarding already-known metadata.
            const auto stopped = failure( stopReason_ );
            for ( auto item = stopped.cbegin(); item != stopped.cend(); ++item )
                result[ item.key() ] = item.value();
        }
        if ( external_ || request_.decoder == "tshark"
             || result.value( "backend" ) == "tshark_lua" ) {
            result[ "backend" ] = "tshark_lua";
            result[ "offsetSpace" ] = "engine_data_source_unresolved";
            result[ "messageCompleteness" ] = "not_reported";
            result[ "messageIntegrity" ] = "not_reported";
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
            result[ "preferredBackend" ] = "tshark_lua";
            result[ "decoderAttempts" ] = QVariantList{ "tshark_lua", "json_preview" };
            result[ "fallbackReason" ] = "dissector_not_dispatched";
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
            finish( result );
            return;
        }
        external_ = true;
        const auto carrierName = result.value( "carrierName" );
        const auto route
            = routeFor( snapshot_.configuration, optionsFor( request_ ), carrierName.toString() );
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
            finish( failure( "timeout" ) );
            return;
        }
        QVariantMap profile;
        for ( const auto* key : { "tshark", "text2pcap", "lua" } )
            profile[ key ] = snapshot_.configuration.value( key );
        profile[ "dissector" ] = route.value( "dissector" );
        profile[ "protocol" ] = route.value( "protocol" );
        const auto name = result.value( "previewName" ).toString();
        QPointer<EvaluationState> guard( this );
        LOG_DEBUG << "Preview selector completed id=" << id_ << " carrier_bytes=" << payload.size()
                  << " direct=" << carrierName.toString().isEmpty()
                  << " remaining_ms=" << remaining;
        TsharkPreviewClient::instance().evaluate(
            sessionOwner_, profile, payload, remaining, id_,
            [ guard, name ]( QVariantMap response ) {
                if ( !guard )
                    return;
                if ( !name.isEmpty() )
                    response[ "previewName" ] = name;
                if ( guard->request_.decoder == "auto" && !guard->fallback_
                     && response.value( "reason" ) == "dissector_not_dispatched"
                     && guard->stopReason_.isEmpty() ) {
                    guard->fallback_ = true;
                    guard->external_ = false;
                    LOG_DEBUG << "Preview automatic JSON fallback id=" << guard->id_
                              << " reason=dissector_not_dispatched remaining_ms="
                              << guard->request_.timeoutMs - guard->elapsed_.elapsed();
                    if ( guard->elapsed_.elapsed() >= guard->request_.timeoutMs )
                        guard->stop( "timeout" );
                    else
                        guard->begin();
                    return;
                }
                guard->finish( std::move( response ) );
            } );
    }

private:
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
    PreviewEvaluationRequest request_;
    QPointer<QObject> receiver_, sessionOwner_;
    PreviewDecoderSnapshot snapshot_;
    PreviewService::Completion done_;
    QPointer<QProcess> process_;
    QTimer timer_;
    QMetaObject::Connection started_;
    QElapsedTimer elapsed_;
    QString id_, digest_, stopReason_, selectedPreviewName_;
    QByteArray output_;
    qint64 diagnostics_ = 0;
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
    // Direct external decoding has no dependency on any legacy carrier definitions.
    if ( request.decoder == "tshark" && request.previewName.isEmpty() )
        request.definitions = R"({"version":1,"previews":[]})";
    if ( invalid.isEmpty() && request.definitions.isEmpty() )
        request.definitions = definitions();
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

QString PreviewService::tooltipHtml( const QVariantMap& result )
{
    const auto assembly = result.value( "assembly" ).toMap();
    QString html;
    if ( result.value( "backend" ) == "tshark_lua" )
        html = "<b>TShark / Lua</b><br>";
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
            auto options = object.value( "options" ).toObject().toVariantMap();
            options[ "previewName" ] = name;
            const auto decoderConfig
                = object.value( "decoderConfiguration" ).toObject().toVariantMap();
            bool direct
                = name.isEmpty()
                  && ( options.value( "decoder" ) == "tshark"
                       || ( options.value( "decoder", "auto" ) == "auto"
                            && decoderConfig.value( "enabled" ).toBool()
                            && decoderConfig.value( "protocolRoutes" )
                                   .toMap()
                                   .contains( options.value( "assemblyProtocol" ).toString() ) ) );
            const auto matches = [ & ]( const PreviewDefinition& preview ) {
                return preview.enabled && ( name.isEmpty() || preview.name == name )
                       && preview.compiled.match( text ).hasMatch();
            };
            auto definition = parsed.previews.cend();
            if ( !direct && name.isEmpty() && options.value( "decoder", "auto" ) == "auto"
                 && decoderConfig.value( "enabled" ).toBool() ) {
                const auto mapped = decoderConfig.value( "previewRoutes" ).toMap();
                definition = std::find_if( parsed.previews.cbegin(), parsed.previews.cend(),
                                           [ & ]( const PreviewDefinition& preview ) {
                                               return mapped.contains( preview.name )
                                                      && matches( preview );
                                           } );
                LOG_DEBUG << "Preview automatic mapped carrier selected="
                          << ( definition != parsed.previews.cend() );
            }
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
            if ( !route.isEmpty() && route.value( "dissector" ).toString().isEmpty() ) {
                result = failure( "invalid_dissector" );
                result[ "backend" ] = "tshark_lua";
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
                    result[ "backend" ] = "tshark_lua";
                    LOG_DEBUG << "Preview carrier failed reason="
                              << result.value( "reason" ).toString();
                }
                else {
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
