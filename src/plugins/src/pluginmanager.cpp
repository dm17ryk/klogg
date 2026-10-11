#include "pluginmanager.h"
#include "log.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryFile>
#include <QUrl>
#include <QtConcurrent>

namespace cilogg::plugins {
namespace {
QString terminalError( const QString& text )
{
    return text.isEmpty() ? QStringLiteral( "Plugin stopped." ) : text;
}
QString platformName()
{
#ifdef Q_OS_WIN
    return "windows";
#elif defined( Q_OS_MAC )
    return "macos";
#else
    return "linux";
#endif
}
bool pending( const QString& state )
{
    return state == "pending" || state == "running";
}
} // namespace

PluginManager::PluginManager( const QString& dataRoot, const QString& shippedRoot, QObject* parent,
                              QNetworkAccessManager* network )
    : QObject( parent )
    , dataRoot_( dataRoot.isEmpty() ? PluginRepository::defaultDataRoot() : dataRoot )
    , shippedRoot_( shippedRoot.isEmpty() ? PluginRepository::shippedRoot() : shippedRoot )
    , settings_( QDir( dataRoot_ ).filePath( "settings.ini" ), QSettings::IniFormat )
    , networkAccess_( network ? network : &network_ )
{
    QDir().mkpath( dataRoot_ );
    QDir().mkpath( QDir( dataRoot_ ).filePath( "captures" ) );
    if ( auto* app = QCoreApplication::instance() ) {
        connect( app, &QCoreApplication::aboutToQuit, this, &PluginManager::shutdown );
    }
    discover();
}

PluginManager::~PluginManager()
{
    shutdown();
    QElapsedTimer grace;
    grace.start();
    for ( auto& [ id, entry ] : entries_ ) {
        Q_UNUSED( id );
        if ( entry->process ) {
            entry->process->waitForStopped( qMax( 0, 3000 - int( grace.elapsed() ) ) );
        }
    }
    // Prevent callbacks into a partially destroyed manager.
    for ( auto& [ id, entry ] : entries_ ) {
        Q_UNUSED( id );
        if ( entry->process ) {
            disconnect( entry->process.get(), nullptr, this, nullptr );
        }
    }
    streams_.clear();
    entries_.clear();
}

QString PluginManager::configurationKey( const QString& id ) const
{
    return "plugins/" + id + "/";
}
QString PluginManager::catalogUrl() const
{
    return settings_
        .value( "catalogUrl",
                "https://raw.githubusercontent.com/dm17ryk/klogg/master/plugins.json" )
        .toString();
}

void PluginManager::discover()
{
    std::map<QString, Manifest> found;
    const QStringList roots{ QDir( dataRoot_ ).filePath( "plugins" ), shippedRoot_ };
    for ( const auto& root : roots ) {
        for ( const auto& dir :
              QDir( root ).entryInfoList( QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name ) ) {
            if ( dir.fileName().startsWith( '.' ) ) {
                continue;
            }
            auto manifest = Manifest::read( dir.absoluteFilePath() );
            if ( dir.isSymLink() ) {
                manifest.error = "Plugin directory cannot be a symbolic link.";
            }
            if ( manifest.id.isEmpty() ) {
                manifest.id = dir.fileName();
            }
            if ( found.count( manifest.id ) ) {
                LOG_DEBUG << "Plugin discovery shadowed id=" << manifest.id.toStdString();
                continue;
            }
            found.emplace( manifest.id, std::move( manifest ) );
        }
    }
    for ( auto& [ id, manifest ] : found ) {
        const auto existing = entries_.find( id );
        if ( existing != entries_.end() && existing->second->process ) {
            continue;
        }
        auto entry = std::make_unique<Entry>();
        entry->manifest = std::move( manifest );
        entries_[ id ] = std::move( entry );
    }
    for ( auto it = entries_.begin(); it != entries_.end(); ) {
        if ( !found.count( it->first ) && !it->second->process ) {
            it = entries_.erase( it );
        }
        else {
            ++it;
        }
    }
    LOG_DEBUG << "Plugin discovery entries=" << entries_.size();
    Q_EMIT changed();
}

QVariantList PluginManager::plugins() const
{
    QVariantList result;
    for ( const auto& [ id, entry ] : entries_ ) {
        auto value = entry->manifest.json.toVariantMap();
        value[ "id" ] = id;
        value[ "directory" ] = entry->manifest.directory;
        value[ "enabled" ] = settings_.value( configurationKey( id ) + "enabled", false ).toBool();
        value[ "state" ] = entry->process ? entry->process->state() : "stopped";
        value[ "error" ] = !entry->manifest.error.isEmpty() ? entry->manifest.error
                           : entry->process                 ? entry->process->error()
                                                            : QString{};
        value[ "diagnostics" ] = entry->diagnostics;
        const auto config = QJsonDocument::fromJson(
            settings_.value( configurationKey( id ) + "configuration", "{}" ).toByteArray() );
        value[ "configurationValues" ] = config.object().toVariantMap();
        if ( entry->process ) {
            value[ "pid" ] = entry->process->pid();
            const auto descriptor = entry->process->descriptor();
            if ( !descriptor.isEmpty() ) {
                value[ "actions" ] = descriptor.value( "actions" ).toArray().toVariantList();
                if ( descriptor.contains( "configuration" ) ) {
                    value[ "configuration" ]
                        = descriptor.value( "configuration" ).toArray().toVariantList();
                }
            }
        }
        result.append( value );
    }
    return result;
}

QVariantMap PluginManager::jobPayload( const Job& j ) const
{
    return { { "jobId", j.id },          { "pluginId", j.pluginId }, { "action", j.action },
             { "state", j.state },       { "error", j.error },       { "result", j.result },
             { "progress", j.progress }, { "context", j.context } };
}
QVariantList PluginManager::jobs() const
{
    QVariantList result;
    for ( const auto& [ id, j ] : jobs_ ) {
        Q_UNUSED( id );
        result.append( jobPayload( j ) );
    }
    return result;
}
QString PluginManager::addJob( const QString& pluginId, const QString& action,
                               const QVariantMap& context )
{
    if ( jobs_.size() >= 256 ) {
        const auto it = std::find_if( jobs_.begin(), jobs_.end(), []( const auto& pair ) {
            return !pending( pair.second.state );
        } );
        if ( it == jobs_.end() ) {
            return {};
        }
        jobs_.erase( it );
    }
    Job j;
    j.id = newId();
    j.pluginId = pluginId;
    j.action = action;
    j.context = context;
    const auto id = j.id;
    jobs_.emplace( id, std::move( j ) );
    LOG_DEBUG << "Plugin job created plugin=" << pluginId.toStdString()
              << " job=" << id.toStdString() << " action=" << action.toStdString();
    return id;
}
Result PluginManager::job( const QString& id ) const
{
    const auto it = jobs_.find( id );
    return it == jobs_.end() ? Result{ "Plugin job not found.", {} }
                             : Result{ {}, jobPayload( it->second ) };
}
void PluginManager::finishJob( const QString& id, const QString& error, const QVariantMap& result )
{
    const auto it = jobs_.find( id );
    if ( it == jobs_.end() || !pending( it->second.state ) ) {
        return;
    }
    auto& j = it->second;
    j.error = error;
    j.result = result;
    j.state = j.cancelRequested ? "cancelled" : error.isEmpty() ? "completed" : "failed";
    LOG_DEBUG << "Plugin job finished plugin=" << j.pluginId.toStdString()
              << " job=" << id.toStdString() << " state=" << j.state.toStdString()
              << " error=" << error.toStdString();
    Q_EMIT changed();
}

PluginProcess* PluginManager::ensureProcess( Entry& entry )
{
    if ( entry.process && entry.process->state() != "stopped" ) {
        return entry.process.get();
    }
    entry.process.reset();
    const auto id = entry.manifest.id;
    entry.process = std::make_unique<PluginProcess>( entry.manifest );
    auto* process = entry.process.get();
    connect( process, &PluginProcess::changed, this, &PluginManager::changed );
    connect( process, &PluginProcess::changed, this, [ this, id ] {
        const auto foundEntry = entries_.find( id );
        if ( foundEntry != entries_.end() && foundEntry->second->process->state() == "ready" ) {
            auto* readyProcess = foundEntry->second->process.get();
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(
                settings_.value( configurationKey( id ) + "configuration", "{}" ).toByteArray(),
                &parseError );
            auto config = document.object();
            const auto fields
                = readyProcess->descriptor().contains( "configuration" )
                      ? readyProcess->descriptor()[ "configuration" ].toArray()
                      : foundEntry->second->manifest.json[ "configuration" ].toArray();
            const auto error = validateValues( fields, &config );
            if ( parseError.error != QJsonParseError::NoError || !document.isObject()
                 || !error.isEmpty() ) {
                LOG_WARNING << "Plugin initialization configuration rejected plugin="
                            << id.toStdString() << " error=" << error.toStdString();
                readyProcess->fail( error.isEmpty() ? "Invalid persisted plugin configuration."
                                                    : error );
                return;
            }
            LOG_DEBUG << "Plugin initialization configuration applied plugin=" << id.toStdString()
                      << " fields=" << fields.size();
            readyProcess->notify( "configure", { { "configuration", config } } );
            if ( readyProcess->state() != "ready" ) {
                return;
            }
            QStringList waiting;
            for ( const auto& [ jobId, j ] : jobs_ ) {
                if ( j.pluginId == id && j.invocation && j.state == "pending" ) {
                    waiting.append( jobId );
                }
            }
            for ( const auto& jobId : waiting ) {
                dispatchJob( jobId );
            }
        }
    } );
    connect( process, &PluginProcess::message, this,
             [ this, id ]( const QJsonObject& frame ) { handleMessage( id, frame ); } );
    connect( process, &PluginProcess::stopped, this, [ this, id ] { processStopped( id ); } );
    connect( process, &PluginProcess::diagnostic, this, [ this, id ]( const QString& text ) {
        auto& diagnostic = entries_.at( id )->diagnostics;
        diagnostic.append( text );
        if ( diagnostic.size() > 32768 ) {
            diagnostic = diagnostic.right( 32768 );
        }
    } );
    QJsonParseError parseError;
    const auto configDocument = QJsonDocument::fromJson(
        settings_.value( configurationKey( id ) + "configuration", "{}" ).toByteArray(),
        &parseError );
    auto config = configDocument.object();
    const auto configError
        = validateValues( entry.manifest.json[ "configuration" ].toArray(), &config );
    if ( parseError.error != QJsonParseError::NoError || !configDocument.isObject()
         || !configError.isEmpty() ) {
        process->fail( configError.isEmpty() ? "Invalid persisted plugin configuration."
                                             : configError );
        return process;
    }
    const auto configDir = QDir( dataRoot_ ).filePath( "config/" + id );
    if ( !QDir().mkpath( configDir ) ) {
        process->fail( "Cannot create plugin configuration directory." );
        return process;
    }
    auto python = settings_.value( "pythonExecutable" ).toString();
    if ( python.isEmpty() ) {
        python = qEnvironmentVariable( "CILOGG_SCRIPT_PYTHON" );
    }
    const auto appDir = QCoreApplication::applicationDirPath();
    for ( const auto& relative : { "python_runtime/python.exe", "python_runtime/python/python.exe",
                                   "python_runtime/bin/python3", "python_runtime/bin/python",
                                   "python_runtime/python/bin/python3", "python/python.exe",
                                   "bin/python3", "bin/python", "python/bin/python3" } ) {
        if ( python.isEmpty() && QFileInfo( QDir( appDir ).filePath( relative ) ).isFile() ) {
            python = QDir( appDir ).filePath( relative );
        }
    }
    if ( python.isEmpty() ) {
        python = QStandardPaths::findExecutable( "python3" );
    }
    if ( python.isEmpty() ) {
        python = QStandardPaths::findExecutable( "python" );
    }
    process->start( config, configDir, python );
    return process;
}

Result PluginManager::invoke( const QString& pluginId, const QString& action,
                              const QVariantMap& arguments, const QVariantMap& context )
{
    if ( shuttingDown_ ) {
        return { "CILogg is shutting down.", {} };
    }
    const auto it = entries_.find( pluginId );
    if ( it == entries_.end() ) {
        return { "Plugin not found.", {} };
    }
    auto& e = *it->second;
    if ( !e.manifest.valid() ) {
        return { e.manifest.error, {} };
    }
    if ( !settings_.value( configurationKey( pluginId ) + "enabled", false ).toBool() ) {
        return { "Plugin is disabled.", {} };
    }
    if ( e.process && ( e.process->state() == "failed" || e.process->state() == "stopping" ) ) {
        return { "Plugin failed or is stopping; restart it explicitly.", {} };
    }
    const auto jobId = addJob( pluginId, action, context );
    if ( jobId.isEmpty() ) {
        return { "Too many active plugin jobs.", {} };
    }
    auto& j = jobs_.at( jobId );
    j.invocation = true;
    j.params = QJsonObject::fromVariantMap( arguments );
    auto* process = ensureProcess( e );
    if ( process->state() == "failed" ) {
        finishJob( jobId, process->error() );
    }
    else if ( process->state() == "ready" ) {
        dispatchJob( jobId );
    }
    Q_EMIT changed();
    return job( jobId );
}

void PluginManager::dispatchJob( const QString& jobId )
{
    auto& j = jobs_.at( jobId );
    if ( j.state != "pending" || j.cancelRequested || shuttingDown_ ) {
        return;
    }
    auto* process = entries_.at( j.pluginId )->process.get();
    QJsonObject selected;
    for ( const auto& item : process->descriptor()[ "actions" ].toArray() ) {
        if ( item.toObject().value( "id" ) == j.action ) {
            selected = item.toObject();
            break;
        }
    }
    if ( selected.isEmpty() ) {
        finishJob( jobId, "Unknown plugin action." );
        return;
    }
    auto params = j.params;
    const auto validation = validateValues( selected.value( "fields" ).toArray(), &params );
    if ( !validation.isEmpty() ) {
        finishJob( jobId, validation );
        return;
    }
    if ( selected.value( "capability" ) == "converter" ) {
        const auto input
            = params.value( "input_path" ).toString( j.context.value( "filePath" ).toString() );
        if ( !QFileInfo( input ).isFile() ) {
            finishJob( jobId, "Converter input file not found." );
            return;
        }
        j.outputPath = QDir( dataRoot_ ).filePath( "captures/converted-" + jobId + ".log" );
        params.insert( "input_path", QFileInfo( input ).absoluteFilePath() );
        params.insert( "output_path", j.outputPath );
    }
    j.state = "running";
    process->request( jobId, "invoke",
                      { { "job_id", jobId },
                        { "action", j.action },
                        { "arguments", params },
                        { "context", QJsonObject::fromVariantMap( j.context ) } } );
    Q_EMIT changed();
}

Result PluginManager::cancel( const QString& id )
{
    const auto it = jobs_.find( id );
    if ( it == jobs_.end() ) {
        return { "Plugin job not found.", {} };
    }
    if ( !pending( it->second.state ) ) {
        return job( id );
    }
    it->second.cancelRequested = true;
    const auto plugin = entries_.find( it->second.pluginId );
    if ( plugin != entries_.end() && plugin->second->process ) {
        plugin->second->process->cancelRequest( id );
    }
    finishJob( id, {} );
    return job( id );
}

void PluginManager::processStopped( const QString& id )
{
    const auto it = entries_.find( id );
    if ( it == entries_.end() ) {
        return;
    }
    QStringList streams;
    for ( const auto& [ key, stream ] : streams_ ) {
        if ( stream.pluginId == id ) {
            streams.append( key );
        }
    }
    for ( const auto& key : streams ) {
        stopStream( key, false );
    }
    for ( const auto& [ key, j ] : jobs_ ) {
        if ( j.pluginId == id && j.invocation && pending( j.state ) ) {
            finishJob( key, terminalError( it->second->process->error() ) );
        }
    }
    Q_EMIT pluginStopped( id );
    Q_EMIT changed();
}

void PluginManager::stopStream( const QString& id, bool notify )
{
    const auto it = streams_.find( id );
    if ( it == streams_.end() ) {
        return;
    }
    const auto pluginId = it->second.pluginId;
    const auto streamId = id.mid( pluginId.size() + 1 );
    LOG_DEBUG << "Plugin stream stopped plugin=" << pluginId.toStdString()
              << " stream=" << streamId.toStdString();
    streams_.erase( it );
    const auto entry = entries_.find( pluginId );
    if ( notify && entry != entries_.end() && entry->second->process ) {
        entry->second->process->notify( "stream/stop", { { "stream_id", streamId } } );
    }
    Q_EMIT changed();
}
void PluginManager::closeFile( const QString& path, const QString& windowId )
{
    QStringList ids;
    for ( const auto& [ id, stream ] : streams_ ) {
        if ( stream.path == path ) {
            ids.append( id );
        }
    }
    for ( const auto& id : ids ) {
        stopStream( id, true );
    }
    if ( !path.isEmpty() ) {
        QStringList pendingIds;
        for ( auto& [ id, j ] : jobs_ ) {
            if ( j.invocation && j.context.value( "filePath" ).toString() == path
                 && ( windowId.isEmpty()
                      || j.context.value( "windowId" ).toString() == windowId ) ) {
                j.cancelRequested = true;
                if ( pending( j.state ) ) {
                    pendingIds.append( id );
                }
            }
        }
        for ( const auto& id : pendingIds ) {
            cancel( id );
        }
    }
}
void PluginManager::closeWindow( const QString& windowId )
{
    QStringList ids;
    for ( const auto& [ id, stream ] : streams_ ) {
        if ( stream.context.value( "windowId" ).toString() == windowId ) {
            ids.append( id );
        }
    }
    for ( const auto& id : ids ) {
        stopStream( id, true );
    }
    QStringList jobIds;
    for ( auto& [ id, j ] : jobs_ ) {
        if ( j.invocation && j.context.value( "windowId" ).toString() == windowId ) {
            j.cancelRequested = true;
            if ( pending( j.state ) ) {
                jobIds.append( id );
            }
        }
    }
    for ( const auto& id : jobIds ) {
        cancel( id );
    }
}
void PluginManager::shutdown()
{
    shuttingDown_ = true;
    for ( auto& [ id, entry ] : entries_ ) {
        Q_UNUSED( id );
        if ( entry->process ) {
            entry->process->stop();
        }
    }
}

void PluginManager::handleMessage( const QString& pluginId, const QJsonObject& frame )
{
    auto* process = entries_.at( pluginId )->process.get();
    if ( frame.contains( "id" ) && !frame.contains( "method" ) ) {
        const auto id = frame.value( "id" ).toString();
        const auto it = jobs_.find( id );
        if ( it == jobs_.end() || it->second.pluginId != pluginId
             || !pending( it->second.state ) ) {
            return;
        }
        const auto error = frame.contains( "error" ) ? frame.value( "error" )
                                                           .toObject()
                                                           .value( "message" )
                                                           .toString( "Plugin action failed." )
                                                     : QString{};
        if ( error.isEmpty() && !it->second.outputPath.isEmpty() ) {
            if ( !QFileInfo( it->second.outputPath ).isFile() ) {
                finishJob( id, "Converter produced no output." );
                return;
            }
            Q_EMIT fileReady( it->second.outputPath, false, it->second.context );
        }
        finishJob( id, error, frame.value( "result" ).toObject().toVariantMap() );
        return;
    }
    const auto method = frame.value( "method" ).toString();
    const auto params = frame.value( "params" ).toObject();
    const auto jobId = params.value( "job_id" ).toString();
    const auto jobIt = jobs_.find( jobId );
    const bool ownsJob = jobIt != jobs_.end() && jobIt->second.pluginId == pluginId;
    const auto streamId = params.value( "stream_id" ).toString();
    const auto key = pluginId + ":" + streamId;
    if ( method == "progress" && ownsJob && pending( jobIt->second.state ) ) {
        jobIt->second.progress = qBound( 0.0, params.value( "value" ).toDouble(), 1.0 );
        Q_EMIT changed();
    }
    else if ( method == "panel/update" && ownsJob && !jobIt->second.cancelRequested ) {
        Q_EMIT panelUpdated( pluginId, params.value( "panel" ).toObject(), jobIt->second.context );
    }
    else if ( method == "stream/open" ) {
        if ( ownsJob && jobIt->second.cancelRequested ) {
            process->notify( "stream/stop", { { "stream_id", streamId } } );
            return;
        }
        if ( !ownsJob || jobIt->second.cancelRequested || streamId.isEmpty()
             || streamId.size() > 128 || streams_.count( key ) || streams_.size() >= 128
             || !entries_.at( pluginId )
                     ->manifest.json[ "capabilities" ]
                     .toArray()
                     .contains( "datasource" ) ) {
            process->fail( "Invalid plugin stream/open." );
            return;
        }
        Stream stream;
        stream.pluginId = pluginId;
        stream.jobId = jobId;
        stream.context = jobIt->second.context;
        stream.path = QDir( dataRoot_ ).filePath( "captures/stream-" + newId() + ".log" );
        stream.file = std::make_unique<QFile>( stream.path );
        if ( !stream.file->open( QIODevice::WriteOnly ) ) {
            process->fail( "Cannot open stream capture." );
            return;
        }
        const auto path = stream.path;
        streams_.emplace( key, std::move( stream ) );
        Q_EMIT fileReady( path, true, jobIt->second.context );
        process->notify( "stream/opened", { { "stream_id", streamId } } );
        LOG_DEBUG << "Plugin stream opened plugin=" << pluginId.toStdString()
                  << " stream=" << streamId.toStdString();
    }
    else if ( method == "stream/data" ) {
        const auto it = streams_.find( key );
        if ( it == streams_.end() ) {
            process->notify( "stream/stop", { { "stream_id", streamId } } );
            return;
        }
        auto& stream = it->second;
        const auto sequence = params.value( "sequence" ).toDouble( -1 );
        if ( sequence != static_cast<double>( stream.nextSequence )
             || !params.value( "records" ).isArray() ) {
            process->fail( "Invalid plugin stream sequence or records." );
            return;
        }
        for ( const auto& record : params.value( "records" ).toArray() ) {
            if ( !record.isString() ) {
                process->fail( "Plugin log record must be text." );
                return;
            }
            auto bytes = record.toString().toUtf8();
            bytes.append( '\n' );
            if ( stream.file->write( bytes ) != bytes.size() ) {
                process->fail( "Plugin capture write failed." );
                return;
            }
        }
        if ( !stream.file->flush() ) {
            process->fail( "Plugin capture flush failed." );
            return;
        }
        ++stream.nextSequence;
        process->notify( "stream/ack", { { "stream_id", streamId }, { "sequence", sequence } } );
        if ( stream.nextSequence % 100 == 0 ) {
            LOG_DEBUG << "Plugin stream checkpoint plugin=" << pluginId.toStdString()
                      << " stream=" << streamId.toStdString() << " batches=" << stream.nextSequence;
        }
    }
    else if ( method == "stream/close" ) {
        stopStream( key, false );
    }
    else if ( method == "log" ) {
        LOG_DEBUG << "Plugin log id=" << pluginId.toStdString() << " "
                  << params.value( "message" ).toString().toStdString();
    }
    else {
        LOG_WARNING << "Plugin unknown message id=" << pluginId.toStdString()
                    << " method=" << method.toStdString();
        if ( frame.contains( "id" ) ) {
            process->fail( "Unsupported plugin-to-host request." );
        }
    }
}

Result PluginManager::manage( const QString& operation, const QString& id,
                              const QVariantMap& options )
{
    LOG_DEBUG << "Plugin manage id=" << id.toStdString()
              << " operation=" << operation.toStdString();
    if ( shuttingDown_ ) {
        return { "CILogg is shutting down.", {} };
    }
    const auto installRoot = QDir( dataRoot_ ).filePath( "plugins" );
    if ( operation == "install" || operation == "update" || operation == "restart"
         || operation == "remove" ) {
        for ( const auto& [ jobId, j ] : jobs_ ) {
            Q_UNUSED( jobId );
            if ( !id.isEmpty() && j.pluginId == id && !j.invocation && pending( j.state ) ) {
                return { "A plugin management operation is already running.", {} };
            }
        }
    }
    if ( operation == "catalog" ) {
        const auto url = options.value( "url", catalogUrl() ).toString();
        if ( QUrl( url ).scheme() != "https" ) {
            return { "Catalog requires HTTPS.", {} };
        }
        settings_.setValue( "catalogUrl", url );
        const auto jobId = addJob( {}, "catalog" );
        if ( jobId.isEmpty() ) {
            return { "Too many jobs.", {} };
        }
        fetch( url, jobId );
        return job( jobId );
    }
    if ( operation == "install" && options.contains( "path" ) ) {
        const auto jobId = addJob( id, "install" );
        if ( jobId.isEmpty() ) {
            return { "Too many jobs.", {} };
        }
        install( options.value( "path" ).toString(), jobId, id );
        return job( jobId );
    }
    if ( operation == "install" || operation == "update" ) {
        QJsonObject package;
        for ( const auto& value : catalog_ ) {
            if ( value.toObject().value( "id" ) == id ) {
                package = value.toObject();
                break;
            }
        }
        if ( package.isEmpty() ) {
            return { "Plugin not in the loaded catalog for this platform.", {} };
        }
        const auto jobId = addJob( id, operation );
        if ( jobId.isEmpty() ) {
            return { "Too many jobs.", {} };
        }
        fetch( package.value( "url" ).toString(), jobId, package.value( "sha256" ).toString(),
               package.value( "version" ).toString() );
        return job( jobId );
    }
    const auto it = entries_.find( id );
    if ( it == entries_.end() ) {
        return { "Plugin not found.", {} };
    }
    auto& e = *it->second;
    if ( operation == "remove" || operation == "restart" ) {
        if ( e.process && e.process->isRunning() ) {
            const auto jobId = addJob( id, operation );
            if ( jobId.isEmpty() ) {
                return { "Too many jobs.", {} };
            }
            e.process->stop();
            QTimer::singleShot( 50, this, [ this, operation, id, jobId ] {
                completeManagement( operation, id, jobId );
            } );
            return job( jobId );
        }
        e.process.reset();
        if ( operation == "remove" ) {
            const auto error = PluginRepository::remove( id, installRoot );
            if ( !error.isEmpty() ) {
                return { error, {} };
            }
            settings_.setValue( configurationKey( id ) + "enabled", false );
            entries_.erase( it );
            discover();
        }
        else if ( settings_.value( configurationKey( id ) + "enabled", false ).toBool()
                  && e.manifest.valid() ) {
            ensureProcess( e );
        }
    }
    else if ( operation == "enable" || operation == "disable" ) {
        if ( operation == "enable" && !e.manifest.valid() ) {
            return { e.manifest.error, {} };
        }
        settings_.setValue( configurationKey( id ) + "enabled", operation == "enable" );
        if ( operation == "disable" && e.process ) {
            e.process->stop();
        }
    }
    else if ( operation == "configure" ) {
        auto config = QJsonObject::fromVariantMap( options.value( "configuration" ).toMap() );
        const auto fields = e.process && e.process->descriptor().contains( "configuration" )
                                ? e.process->descriptor()[ "configuration" ].toArray()
                                : e.manifest.json[ "configuration" ].toArray();
        const auto error = validateValues( fields, &config );
        if ( !error.isEmpty() ) {
            return { error, {} };
        }
        settings_.setValue( configurationKey( id ) + "configuration",
                            QJsonDocument( config ).toJson( QJsonDocument::Compact ) );
        if ( e.process && e.process->state() == "ready" ) {
            e.process->notify( "configure", { { "configuration", config } } );
        }
    }
    else {
        return { "Unknown plugin management operation.", {} };
    }
    settings_.sync();
    if ( settings_.status() != QSettings::NoError ) {
        return { "Cannot persist plugin settings.", {} };
    }
    Q_EMIT changed();
    return { {}, { { "plugins", plugins() } } };
}

void PluginManager::completeManagement( const QString& operation, const QString& id,
                                        const QString& jobId )
{
    const auto j = jobs_.find( jobId );
    if ( shuttingDown_ || j == jobs_.end() || !pending( j->second.state ) ) {
        return;
    }
    const auto entry = entries_.find( id );
    if ( entry != entries_.end() && entry->second->process
         && entry->second->process->isRunning() ) {
        QTimer::singleShot( 50, this, [ this, operation, id, jobId ] {
            completeManagement( operation, id, jobId );
        } );
        return;
    }
    if ( entry == entries_.end() ) {
        finishJob( jobId, "Plugin not found." );
        return;
    }
    auto& e = *entry->second;
    e.process.reset();
    QString error;
    if ( operation == "remove" ) {
        error = PluginRepository::remove( id, QDir( dataRoot_ ).filePath( "plugins" ) );
        if ( error.isEmpty() ) {
            settings_.setValue( configurationKey( id ) + "enabled", false );
            entries_.erase( entry );
            discover();
        }
    }
    else if ( settings_.value( configurationKey( id ) + "enabled", false ).toBool()
              && e.manifest.valid() ) {
        auto* process = ensureProcess( e );
        if ( process->state() == "failed" ) {
            error = process->error();
        }
    }
    settings_.sync();
    if ( settings_.status() != QSettings::NoError ) {
        error = "Cannot persist plugin settings.";
    }
    finishJob( jobId, error, { { "plugins", plugins() } } );
}

void PluginManager::install( const QString& source, const QString& jobId, const QString& expectedId,
                             const QString& expectedVersion )
{
    auto* watcher = new QFutureWatcher<StagedPlugin>( this );
    connect( watcher, &QFutureWatcher<StagedPlugin>::finished, this,
             [ this, watcher, jobId, expectedId, expectedVersion ] {
                 const auto staged = watcher->result();
                 watcher->deleteLater();
                 const auto it = jobs_.find( jobId );
                 QString error = staged.error;
                 if ( !expectedId.isEmpty() && staged.manifest.id != expectedId ) {
                     error = "Package ID differs from catalog/selected plugin.";
                 }
                 if ( !expectedVersion.isEmpty() && staged.manifest.version != expectedVersion ) {
                     error = "Package version differs from catalog.";
                 }
                 if ( it == jobs_.end() || it->second.cancelRequested || shuttingDown_
                      || !error.isEmpty() ) {
                     if ( !staged.directory.isEmpty() ) {
                         QDir( staged.directory ).removeRecursively();
                     }
                     finishJob( jobId, error );
                     return;
                 }
                 commitWhenStopped( staged, jobId );
             } );
    const auto root = QDir( dataRoot_ ).filePath( "plugins" );
    watcher->setFuture(
        QtConcurrent::run( [ source, root ] { return PluginRepository::stage( source, root ); } ) );
}

void PluginManager::commitWhenStopped( const StagedPlugin& staged, const QString& jobId )
{
    const auto jobIt = jobs_.find( jobId );
    if ( shuttingDown_ || jobIt == jobs_.end() || jobIt->second.cancelRequested ) {
        QDir( staged.directory ).removeRecursively();
        return;
    }
    const auto existing = entries_.find( staged.manifest.id );
    if ( existing != entries_.end() && existing->second->process
         && existing->second->process->isRunning() ) {
        if ( existing->second->process->state() != "stopping" ) {
            existing->second->process->stop();
        }
        QTimer::singleShot( 50, this,
                            [ this, staged, jobId ] { commitWhenStopped( staged, jobId ); } );
        return;
    }
    const auto error = PluginRepository::commit( staged, QDir( dataRoot_ ).filePath( "plugins" ) );
    if ( error.isEmpty() ) {
        entries_.erase( staged.manifest.id );
        discover();
    }
    else {
        QDir( staged.directory ).removeRecursively();
    }
    finishJob( jobId, error,
               { { "pluginId", staged.manifest.id }, { "version", staged.manifest.version } } );
}

void PluginManager::fetch( const QString& url, const QString& jobId, const QString& sha256,
                           const QString& expectedVersion )
{
    const QUrl target( url );
    if ( target.scheme() != "https" || !target.isValid() || target.host().isEmpty() ) {
        finishJob( jobId, "Download requires HTTPS." );
        return;
    }
    QNetworkRequest request( target );
    request.setAttribute( QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::NoLessSafeRedirectPolicy );
    auto* reply = networkAccess_->get( request );
    auto bytes = std::make_shared<QByteArray>();
    auto oversized = std::make_shared<bool>( false );
    const auto limit = sha256.isEmpty() ? MaxMessageBytes : 128 * MaxMessageBytes;
    connect( reply, &QNetworkReply::readyRead, this, [ reply, bytes, oversized, limit ] {
        bytes->append( reply->read( limit + 1 - bytes->size() ) );
        if ( bytes->size() > limit ) {
            *oversized = true;
            reply->abort();
        }
    } );
    QTimer::singleShot( 30000, reply, [ reply ] {
        if ( !reply->isFinished() ) {
            reply->abort();
        }
    } );
    connect( reply, &QNetworkReply::finished, this,
             [ this, reply, bytes, oversized, jobId, sha256, expectedVersion ] {
                 reply->deleteLater();
                 const auto it = jobs_.find( jobId );
                 if ( it == jobs_.end() || it->second.cancelRequested || shuttingDown_ ) {
                     return;
                 }
                 if ( *oversized || reply->error() != QNetworkReply::NoError ) {
                     finishJob( jobId, *oversized ? "Download exceeds size limit."
                                                  : reply->errorString() );
                     return;
                 }
                 if ( sha256.isEmpty() ) {
                     loadCatalog( *bytes, jobId );
                     return;
                 }
                 if ( QString::fromLatin1(
                          QCryptographicHash::hash( *bytes, QCryptographicHash::Sha256 ).toHex() )
                          .compare( sha256, Qt::CaseInsensitive )
                      != 0 ) {
                     finishJob( jobId, "Plugin package SHA-256 mismatch." );
                     return;
                 }
                 auto package = std::make_shared<QTemporaryFile>(
                     QDir( dataRoot_ ).filePath( ".download-XXXXXX.zip" ) );
                 if ( !package->open() || package->write( *bytes ) != bytes->size()
                      || !package->flush() ) {
                     finishJob( jobId, "Cannot stage plugin download." );
                     return;
                 }
                 const auto path = package->fileName();
                 package->close();
                 auto* watcher = new QFutureWatcher<StagedPlugin>( this );
                 const auto id = it->second.pluginId;
                 const auto root = QDir( dataRoot_ ).filePath( "plugins" );
                 connect( watcher, &QFutureWatcher<StagedPlugin>::finished, this,
                          [ this, watcher, package, jobId, id, expectedVersion ] {
                              const auto staged = watcher->result();
                              watcher->deleteLater();
                              if ( !staged.error.isEmpty() || staged.manifest.id != id
                                   || staged.manifest.version != expectedVersion ) {
                                  if ( !staged.directory.isEmpty() ) {
                                      QDir( staged.directory ).removeRecursively();
                                  }
                                  finishJob( jobId, staged.error.isEmpty()
                                                        ? "Package identity differs from catalog."
                                                        : staged.error );
                                  return;
                              }
                              commitWhenStopped( staged, jobId );
                          } );
                 watcher->setFuture( QtConcurrent::run(
                     [ path, root, package ] { return PluginRepository::stage( path, root ); } ) );
             } );
}

void PluginManager::loadCatalog( const QByteArray& bytes, const QString& jobId )
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson( bytes, &error );
    if ( error.error != QJsonParseError::NoError || !document.isObject()
         || document.object().value( "schema_version" ).toInt() != 1
         || !document.object().value( "plugins" ).isArray() ) {
        finishJob( jobId, "Invalid plugin catalog." );
        return;
    }
    QJsonArray selected;
    const auto packages = document.object().value( "plugins" ).toArray();
    if ( packages.size() > 1000 ) {
        finishJob( jobId, "Too many catalog entries." );
        return;
    }
    static const QRegularExpression shaPattern( "^[a-fA-F0-9]{64}$" );
    for ( const auto& value : packages ) {
        const auto p = value.toObject();
        if ( p.value( "os" ).toString() != platformName()
             || p.value( "arch" ).toString() != QSysInfo::buildCpuArchitecture() ) {
            continue;
        }
        if ( p.value( "protocol_version" ).toInt() != ProtocolVersion
             || p.value( "id" ).toString().isEmpty() || p.value( "version" ).toString().isEmpty()
             || QUrl( p.value( "url" ).toString() ).scheme() != "https"
             || !shaPattern.match( p.value( "sha256" ).toString() ).hasMatch() ) {
            finishJob( jobId, "Invalid catalog package or missing SHA-256." );
            return;
        }
        selected.append( p );
    }
    catalog_ = selected;
    finishJob( jobId, {}, { { "plugins", catalog_.toVariantList() } } );
}
} // namespace cilogg::plugins
