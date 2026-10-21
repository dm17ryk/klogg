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
#include <limits>

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
#elif defined( Q_OS_MACOS )
    return "macos";
#else
    return "linux";
#endif
}
bool pending( const QString& state )
{
    return state == "pending" || state == "running";
}

QStringList secretFieldNames( const QJsonArray& fields )
{
    QStringList result;
    for ( const auto& value : fields ) {
        if ( value.toObject().value( "type" ) == "secret" ) {
            result.append( value.toObject().value( "id" ).toString() );
        }
    }
    return result;
}

QJsonArray publicFields( const QJsonArray& fields, const QStringList& secrets = {} )
{
    QJsonArray result;
    for ( const auto& value : fields ) {
        auto field = value.toObject();
        field.remove( "keep_existing" );
        if ( field.value( "type" ) == "secret"
             || secrets.contains( field.value( "id" ).toString() ) ) {
            field.remove( "default" );
            field.insert( "type", "secret" );
        }
        result.append( field );
    }
    return result;
}

bool packageOperation( const QString& operation )
{
    return operation == "install" || operation == "update" || operation == "remove"
           || operation == "restart";
}
} // namespace

PluginManager::PluginManager( const QString& dataRoot, const QString& shippedRoot, QObject* parent,
                              QNetworkAccessManager* network )
    : QObject( parent )
    , dataRoot_( dataRoot.isEmpty() ? PluginRepository::defaultDataRoot() : dataRoot )
    , shippedRoot_( shippedRoot.isEmpty() ? PluginRepository::shippedRoot() : shippedRoot )
    , settings_( QDir( dataRoot_ ).filePath( "settings.ini" ), QSettings::IniFormat )
    , storage_( QDir( dataRoot_ ).filePath( "captures" ) )
    , networkAccess_( network ? network : &network_ )
{
    QDir().mkpath( dataRoot_ );
    QDir().mkpath( QDir( dataRoot_ ).filePath( "captures" ) );
    auto limits = storage_.limits();
    for ( auto it = limits.begin(); it != limits.end(); ++it ) {
        bool valid = false;
        const auto value
            = settings_.value( "storage/" + it.key(), it.value() ).toLongLong( &valid );
        if ( valid ) {
            it.value() = value;
        }
    }
    const auto storageError = storage_.setLimits( limits );
    if ( !storageError.isEmpty() ) {
        LOG_WARNING << "Plugin persisted storage limits rejected: " << storageError.toStdString();
    }
    refreshCaptureUsage();
    auto* storageTimer = new QTimer( this );
    storageTimer->setInterval( 100 );
    connect( storageTimer, &QTimer::timeout, this, &PluginManager::monitorStorage );
    storageTimer->start();
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
    QStringList enabledByDefault;
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
            if ( root == shippedRoot_ && manifest.valid()
                 && manifest.json.value( "enabled_by_default" ).toBool() ) {
                enabledByDefault.append( manifest.id );
                LOG_DEBUG << "Plugin shipped default declared id=" << manifest.id.toStdString();
            }
            if ( found.count( manifest.id ) ) {
                LOG_DEBUG << "Plugin discovery shadowed id=" << manifest.id.toStdString();
                continue;
            }
            found.emplace( manifest.id, std::move( manifest ) );
        }
    }
    bool settingsChanged = false;
    for ( auto& [ id, manifest ] : found ) {
        const auto existing = entries_.find( id );
        if ( existing != entries_.end() && existing->second->process ) {
            continue;
        }
        auto entry = std::make_unique<Entry>();
        entry->manifest = std::move( manifest );
        if ( enabledByDefault.contains( id ) ) {
            const auto key = configurationKey( id ) + "enabled";
            if ( !entry->manifest.valid() ) {
                LOG_DEBUG << "Plugin shipped default skipped for invalid override id="
                          << id.toStdString();
            }
            else if ( settings_.contains( key ) ) {
                LOG_DEBUG << "Plugin shipped default preserves saved choice id=" << id.toStdString()
                          << " enabled=" << settings_.value( key ).toBool();
            }
            else {
                settings_.setValue( key, true );
                settingsChanged = true;
                LOG_DEBUG << "Plugin preinstalled default enabled id=" << id.toStdString();
            }
        }
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
    if ( settingsChanged ) {
        settings_.sync();
        if ( settings_.status() != QSettings::NoError ) {
            LOG_WARNING << "Plugin preinstalled defaults could not be persisted.";
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
        auto configValues = config.object().toVariantMap();
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
        const auto fields = QJsonArray::fromVariantList( value.value( "configuration" ).toList() );
        auto secrets = settings_.value( configurationKey( id ) + "secretFields" ).toStringList();
        secrets.append(
            secretFieldNames( entry->manifest.json.value( "configuration" ).toArray() ) );
        secrets.append( secretFieldNames( fields ) );
        secrets.removeDuplicates();
        QVariantMap secretPresence;
        for ( const auto& key : secrets ) {
            auto secret = configValues.take( key );
            if ( !secret.isValid() ) {
                for ( const auto& field : fields ) {
                    if ( field.toObject().value( "id" ) == key ) {
                        secret = field.toObject().value( "default" ).toVariant();
                    }
                }
            }
            secretPresence.insert( key, secret.isValid() && !secret.toString().isEmpty() );
        }
        value[ "configurationValues" ] = configValues;
        value[ "configuredSecrets" ] = secretPresence;
        value[ "configuration" ] = publicFields( fields, secrets ).toVariantList();
        QJsonArray actions;
        for ( const auto& definition : value.value( "actions" ).toList() ) {
            auto action = QJsonObject::fromVariantMap( definition.toMap() );
            action[ "fields" ] = publicFields( action.value( "fields" ).toArray() );
            actions.append( action );
        }
        value[ "actions" ] = actions.toVariantList();
        result.append( value );
    }
    return result;
}

void PluginManager::rememberSecretFields( const QString& id, const QJsonArray& fields )
{
    auto names = settings_.value( configurationKey( id ) + "secretFields" ).toStringList();
    names.append( secretFieldNames( fields ) );
    names.removeDuplicates();
    settings_.setValue( configurationKey( id ) + "secretFields", names );
}

qint64 PluginManager::reservedCaptureBytes() const
{
    qint64 result = 0;
    for ( const auto& [ id, job ] : jobs_ ) {
        Q_UNUSED( id );
        if ( pending( job.state ) && !job.outputPath.isEmpty() ) {
            const auto remaining
                = qMax( qint64( 0 ), job.outputReservation - job.observedOutputBytes );
            result = remaining > std::numeric_limits<qint64>::max() - result
                         ? std::numeric_limits<qint64>::max()
                         : result + remaining;
        }
    }
    return result;
}

QVariantMap PluginManager::storageStatus() const
{
    return storage_.status( reservedCaptureBytes() );
}

void PluginManager::refreshCaptureUsage()
{
    ++storageScanGeneration_;
    QSet<QString> outputs;
    for ( const auto& [ id, stream ] : streams_ ) {
        Q_UNUSED( id );
        outputs.insert( QFileInfo( stream.path ).absoluteFilePath() );
    }
    for ( const auto& [ id, job ] : jobs_ ) {
        Q_UNUSED( id );
        if ( !job.outputPath.isEmpty() ) {
            outputs.insert( QFileInfo( job.outputPath ).absoluteFilePath() );
        }
    }
    auto total = CaptureStorage::directoryUsage( storage_.directory(), outputs );
    // Directory-entry sizes can lag open handles on Windows.
    for ( const auto& [ id, stream ] : streams_ ) {
        Q_UNUSED( id );
        const auto size = qMax( qint64( 0 ), stream.file->size() );
        total = size > std::numeric_limits<qint64>::max() - total
                    ? std::numeric_limits<qint64>::max()
                    : total + size;
    }
    // Measure each independent converter exactly once; its reservation uses the same snapshot.
    for ( auto& [ id, job ] : jobs_ ) {
        Q_UNUSED( id );
        if ( !job.outputPath.isEmpty() ) {
            job.observedOutputBytes = qMax( qint64( 0 ), QFileInfo( job.outputPath ).size() );
            total = job.observedOutputBytes > std::numeric_limits<qint64>::max() - total
                        ? std::numeric_limits<qint64>::max()
                        : total + job.observedOutputBytes;
        }
    }
    storage_.replaceUsage( total );
    storage_.refreshSpace();
}

void PluginManager::recordCaptureBytes( qint64 delta )
{
    storage_.addBytes( delta );
}

void PluginManager::failStorageJob( QString pluginId, QString jobId, QString error )
{
    LOG_WARNING << "Plugin storage stopped plugin=" << pluginId.toStdString()
                << " job=" << jobId.toStdString() << " reason=" << error.toStdString();
    finishJob( jobId, error );
    const auto stoppedJob = jobs_.find( jobId );
    if ( stoppedJob != jobs_.end() ) {
        stoppedJob->second.cancelRequested = true;
    }
    QStringList streams;
    for ( const auto& [ key, stream ] : streams_ ) {
        if ( stream.pluginId == pluginId && stream.jobId == jobId ) {
            streams.append( key );
        }
    }
    for ( const auto& key : streams ) {
        stopStream( key, true );
    }
    const auto entry = entries_.find( pluginId );
    if ( entry != entries_.end() ) {
        entry->second->diagnostics = ( entry->second->diagnostics + error + '\n' ).right( 32768 );
        if ( entry->second->process ) {
            const auto job = jobs_.find( jobId );
            if ( job != jobs_.end() && !job->second.outputPath.isEmpty() ) {
                // A converter writes independently; stop its process once a violation is observed.
                entry->second->process->fail( error );
            }
            else {
                entry->second->process->notify( "cancel", { { "job_id", jobId } } );
            }
        }
    }
    Q_EMIT changed();
}

void PluginManager::monitorStorage()
{
    if ( shuttingDown_ ) {
        return;
    }
    QStringList growingOutputs;
    for ( auto& [ id, job ] : jobs_ ) {
        if ( !job.outputPath.isEmpty() ) {
            const auto size = qMax( qint64( 0 ), QFileInfo( job.outputPath ).size() );
            if ( size > job.observedOutputBytes ) {
                growingOutputs.append( id );
            }
            recordCaptureBytes( size - job.observedOutputBytes );
            job.observedOutputBytes = size;
        }
    }
    storage_.refreshSpace();
    QStringList violations;
    for ( const auto& [ id, job ] : jobs_ ) {
        const auto entry = entries_.find( job.pluginId );
        if ( job.outputSupervised && ( pending( job.state ) || growingOutputs.contains( id ) )
             && !job.outputPath.isEmpty() && entry != entries_.end() && entry->second->process
             && entry->second->process->isRunning()
             && !storage_.checkWrite( job.observedOutputBytes, 0, reservedCaptureBytes() )
                     .isEmpty() ) {
            violations.append( id );
        }
    }
    for ( const auto& id : violations ) {
        const auto job = jobs_.find( id );
        if ( job != jobs_.end() ) {
            // Rescan before enforcing an aggregate/free-space limit to avoid conservative scan
            // overlap.
            refreshCaptureUsage();
            const auto error
                = storage_.checkWrite( job->second.observedOutputBytes, 0, reservedCaptureBytes() );
            if ( !error.isEmpty() ) {
                failStorageJob( job->second.pluginId, id, error );
            }
        }
    }
    if ( ++storageTicks_ >= 10 && !storageScanActive_ ) {
        storageTicks_ = 0;
        storageScanActive_ = true;
        const auto generation = ++storageScanGeneration_;
        QSet<QString> outputs;
        for ( const auto& [ id, stream ] : streams_ ) {
            Q_UNUSED( id );
            outputs.insert( QFileInfo( stream.path ).absoluteFilePath() );
        }
        for ( const auto& [ id, job ] : jobs_ ) {
            Q_UNUSED( id );
            if ( !job.outputPath.isEmpty() ) {
                outputs.insert( QFileInfo( job.outputPath ).absoluteFilePath() );
            }
        }
        auto* watcher = new QFutureWatcher<qint64>( this );
        connect( watcher, &QFutureWatcher<qint64>::finished, this, [ this, watcher, generation ] {
            const auto scanned = watcher->result();
            watcher->deleteLater();
            if ( generation == storageScanGeneration_ ) {
                auto total = scanned;
                for ( const auto& [ id, stream ] : streams_ ) {
                    Q_UNUSED( id );
                    const auto size = qMax( qint64( 0 ), stream.file->size() );
                    total = size > std::numeric_limits<qint64>::max() - total
                                ? std::numeric_limits<qint64>::max()
                                : total + size;
                }
                for ( auto& [ id, job ] : jobs_ ) {
                    Q_UNUSED( id );
                    if ( !job.outputPath.isEmpty() ) {
                        job.observedOutputBytes
                            = qMax( qint64( 0 ), QFileInfo( job.outputPath ).size() );
                        total = job.observedOutputBytes > std::numeric_limits<qint64>::max() - total
                                    ? std::numeric_limits<qint64>::max()
                                    : total + job.observedOutputBytes;
                    }
                }
                storage_.replaceUsage( total );
                storage_.refreshSpace();
            }
            storageScanActive_ = false;
        } );
        const auto directory = storage_.directory();
        watcher->setFuture( QtConcurrent::run( [ directory, outputs ] {
            return CaptureStorage::directoryUsage( directory, outputs );
        } ) );
    }
}

Result PluginManager::configureStorage( const QVariantMap& limits )
{
    const auto previous = storage_.limits();
    const auto error = storage_.setLimits( limits );
    if ( !error.isEmpty() ) {
        LOG_WARNING << "Plugin storage configuration rejected: " << error.toStdString();
        return { error, {} };
    }
    const auto updated = storage_.limits();
    for ( auto it = updated.cbegin(); it != updated.cend(); ++it ) {
        settings_.setValue( "storage/" + it.key(), it.value() );
    }
    settings_.sync();
    if ( settings_.status() != QSettings::NoError ) {
        storage_.setLimits( previous );
        for ( auto it = previous.cbegin(); it != previous.cend(); ++it ) {
            settings_.setValue( "storage/" + it.key(), it.value() );
        }
        return { "Cannot persist plugin storage limits.", {} };
    }
    refreshCaptureUsage();
    monitorStorage();
    Q_EMIT changed();
    return { {}, { { "storage", storageStatus() } } };
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
                               const QVariantMap& context, bool management )
{
    ++storageScanGeneration_;
    if ( jobs_.size() >= ( management ? 256u : 240u ) ) {
        const auto it = std::find_if( jobs_.begin(), jobs_.end(), []( const auto& pair ) {
            const auto& retained = pair.second;
            return !pending( retained.state ) && !retained.outputSupervised;
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
            rememberSecretFields( id, fields );
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
    if ( selected.value( "capability" ) == "converter" && !params.contains( "input_path" ) ) {
        params.insert( "input_path", QJsonValue::fromVariant( j.context.value( "filePath" ) ) );
        LOG_DEBUG << "Plugin converter input from target plugin=" << j.pluginId.toStdString()
                  << " job=" << jobId.toStdString();
    }
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
        refreshCaptureUsage();
        const auto reservation = storage_.converterReservation();
        const auto error
            = storage_.checkWrite( 0, qMax( qint64( 1 ), reservation ), reservedCaptureBytes() );
        if ( !error.isEmpty() ) {
            finishJob( jobId, error );
            return;
        }
        j.outputReservation = reservation;
        j.outputPath = QDir( dataRoot_ ).filePath( "captures/converted-" + jobId + ".log" );
        params.insert( "input_path", QFileInfo( input ).absoluteFilePath() );
        params.insert( "output_path", j.outputPath );
        j.outputSupervised = true;
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
    for ( auto& [ key, j ] : jobs_ ) {
        if ( j.pluginId == id ) {
            j.outputSupervised = false;
        }
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
    ++storageScanGeneration_;
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
            refreshCaptureUsage();
            const auto storageError = storage_.checkWrite(
                QFileInfo( it->second.outputPath ).size(), 0, reservedCaptureBytes() );
            if ( !storageError.isEmpty() ) {
                failStorageJob( pluginId, id, storageError );
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
        refreshCaptureUsage();
        const auto storageError = storage_.checkWrite( 0, 1, reservedCaptureBytes() );
        if ( !storageError.isEmpty() ) {
            process->notify( "stream/stop", { { "stream_id", streamId } } );
            failStorageJob( pluginId, jobId, storageError );
            return;
        }
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
        QByteArray batch;
        for ( const auto& record : params.value( "records" ).toArray() ) {
            if ( !record.isString() ) {
                process->fail( "Plugin log record must be text." );
                return;
            }
            batch.append( record.toString().toUtf8() );
            batch.append( '\n' );
        }
        auto storageError
            = storage_.checkWrite( stream.file->size(), batch.size(), reservedCaptureBytes() );
        if ( !storageError.isEmpty() ) {
            refreshCaptureUsage();
            storageError
                = storage_.checkWrite( stream.file->size(), batch.size(), reservedCaptureBytes() );
        }
        if ( !storageError.isEmpty() ) {
            failStorageJob( pluginId, stream.jobId, storageError );
            return;
        }
        const auto written = stream.file->write( batch );
        if ( written > 0 ) {
            recordCaptureBytes( written );
        }
        if ( written != batch.size() ) {
            process->fail( "Plugin capture write failed." );
            return;
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
    if ( packageOperation( operation ) ) {
        const bool unscopedInstall
            = operation == "install" && options.contains( "path" ) && id.isEmpty();
        for ( const auto& [ jobId, j ] : jobs_ ) {
            Q_UNUSED( jobId );
            if ( !j.invocation && pending( j.state ) && packageOperation( j.action )
                 && ( unscopedInstall || ( j.action == "install" && j.pluginId.isEmpty() )
                      || ( !id.isEmpty() && j.pluginId == id ) ) ) {
                LOG_DEBUG << "Plugin management overlap rejected plugin=" << id.toStdString()
                          << " active_job=" << j.id.toStdString();
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
        const auto jobId = addJob( {}, "catalog", {}, true );
        if ( jobId.isEmpty() ) {
            return { "Too many jobs.", {} };
        }
        if ( !catalogJobId_.isEmpty() ) {
            cancel( catalogJobId_ );
        }
        catalogJobId_ = jobId;
        fetch( url, jobId );
        return job( jobId );
    }
    if ( operation == "install" && options.contains( "path" ) ) {
        const auto jobId = addJob( id, "install", {}, true );
        if ( jobId.isEmpty() ) {
            return { "Too many jobs.", {} };
        }
        install( options.value( "path" ).toString(), jobId, id );
        return job( jobId );
    }
    if ( operation == "install" || operation == "update" ) {
        QJsonObject package;
        const auto selector = options.value( "catalog_entry_id" ).toString();
        for ( const auto& value : catalog_ ) {
            const auto candidate = value.toObject();
            if ( candidate.value( "id" ) == id
                 && ( selector.isEmpty() || candidate.value( "entry_id" ) == selector ) ) {
                if ( !package.isEmpty()
                     && package.value( "entry_id" ) != candidate.value( "entry_id" ) ) {
                    return { "Multiple catalog entries match; supply catalog_entry_id.", {} };
                }
                package = candidate;
            }
        }
        if ( package.isEmpty() ) {
            return { "Plugin not in the loaded catalog for this platform.", {} };
        }
        const auto jobId = addJob( id, operation, {}, true );
        if ( jobId.isEmpty() ) {
            return { "Too many jobs.", {} };
        }
        fetch( package.value( "url" ).toString(), jobId, package.value( "sha256" ).toString(),
               package.value( "version" ).toString() );
        return job( jobId );
    }
    if ( operation == "configure" && id.isEmpty() ) {
        const auto limits = QJsonValue::fromVariant( options.value( "storage_limits" ) );
        return limits.isObject()
                   ? configureStorage( limits.toObject().toVariantMap() )
                   : Result{ "Host configuration requires a storage_limits object.", {} };
    }
    const auto it = entries_.find( id );
    if ( it == entries_.end() ) {
        return { "Plugin not found.", {} };
    }
    auto& e = *it->second;
    if ( operation == "remove" || operation == "restart" ) {
        if ( operation == "restart" || ( e.process && e.process->isRunning() ) ) {
            const auto jobId = addJob( id, operation, {}, true );
            if ( jobId.isEmpty() ) {
                return { "Too many jobs.", {} };
            }
            if ( e.process && e.process->isRunning() ) {
                e.process->stop();
            }
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
        const auto previous
            = QJsonDocument::fromJson(
                  settings_.value( configurationKey( id ) + "configuration", "{}" ).toByteArray() )
                  .object();
        auto secrets = settings_.value( configurationKey( id ) + "secretFields" ).toStringList();
        secrets.append( secretFieldNames( fields ) );
        for ( const auto& key : secrets ) {
            if ( !config.contains( key ) && previous.contains( key ) ) {
                config.insert( key, previous.value( key ) );
            }
        }
        const auto error = validateValues( fields, &config );
        if ( !error.isEmpty() ) {
            return { error, {} };
        }
        settings_.setValue( configurationKey( id ) + "configuration",
                            QJsonDocument( config ).toJson( QJsonDocument::Compact ) );
        rememberSecretFields( id, fields );
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
    if ( operation == "restart" && j->second.restartStarted ) {
        const auto state = entry != entries_.end() && entry->second->process
                               ? entry->second->process->state()
                               : QStringLiteral( "failed" );
        if ( state == "starting" || state == "initializing" ) {
            QTimer::singleShot( 50, this, [ this, operation, id, jobId ] {
                completeManagement( operation, id, jobId );
            } );
            return;
        }
        const auto error = state == "ready" ? QString{}
                           : entry != entries_.end() && entry->second->process
                               ? terminalError( entry->second->process->error() )
                               : QStringLiteral( "Plugin not found." );
        finishJob( jobId, error, { { "plugins", plugins() } } );
        return;
    }
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
        j->second.restartStarted = true;
        auto* process = ensureProcess( e );
        Q_UNUSED( process );
        QTimer::singleShot( 50, this, [ this, operation, id, jobId ] {
            completeManagement( operation, id, jobId );
        } );
        return;
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
                 it->second.pluginId = staged.manifest.id;
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
    if ( jobId != catalogJobId_ ) {
        LOG_DEBUG << "Plugin stale catalog discarded job=" << jobId.toStdString();
        finishJob( jobId, "Catalog request was superseded." );
        return;
    }
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
        auto p = value.toObject();
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
        p.remove( "entry_id" );
        p[ "entry_id" ] = QString::fromLatin1(
            QCryptographicHash::hash( QJsonDocument( p ).toJson( QJsonDocument::Compact ),
                                      QCryptographicHash::Sha256 )
                .toHex() );
        selected.append( p );
    }
    catalog_ = selected;
    finishJob( jobId, {}, { { "plugins", catalog_.toVariantList() } } );
}
} // namespace cilogg::plugins
