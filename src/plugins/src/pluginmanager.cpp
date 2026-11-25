#include "pluginmanager.h"
#include "configpack.h"
#include "packstore.h"
#include "klogg_version.h"
#include "log.h"
#include "pluginfeatures.h"
#include "pluginmenuspec.h"
#include "pythonruntime.h"
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
bool repositoryOperation( const QString& operation )
{
    return operation == "repositories" || operation == "repository_add"
           || operation == "repository_update" || operation == "repository_remove"
           || operation == "repository_move" || operation == "repository_sign_in"
           || operation == "repository_sign_out";
}

bool pending( const QString& state )
{
    // `cancelling`: cancel was requested; progress, panels and the final answer still count.
    return state == "pending" || state == "running" || state == "cancelling";
}

constexpr int MaxOpenMessages = 8;
constexpr int MaxMessageText = 4096;
constexpr int MaxEventBytes = 512 * 1024; // a view/event, well below the 1 MiB message limit
const QStringList MessageButtons{
    "ok", "cancel", "yes", "no", "retry", "abort", "ignore", "close"
};

QString settingsSafe( QString text )
{
    static const QRegularExpression unsafe( "[^A-Za-z0-9._-]" );
    return text.replace( unsafe, "_" );
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
    , repositories_( settings_ )
    , storage_( QDir( dataRoot_ ).filePath( "captures" ) )
    , networkAccess_( network ? network : &network_ )
    , instanceId_( QStringLiteral( "pid-%1" ).arg( QCoreApplication::applicationPid() ) )
    , hostVersion_( QString( kloggVersion() ) )
    , queries_( std::make_unique<PluginQueries>(
          [ this ]( const QString& pluginId, const QString& id, const QString& method,
                    const QJsonObject& params ) {
              return sendQueryFrame( pluginId, id, method, params );
          },
          this ) )
{
    connect( queries_.get(), &PluginQueries::breakerChanged, this, &PluginManager::changed );
    QDir().mkpath( dataRoot_ );
    QDir().mkpath( QDir( dataRoot_ ).filePath( "captures" ) );
    repositories_.migrate();
    if ( !persistSettings().isEmpty() ) {
        LOG_WARNING << "Plugin repositories could not be persisted.";
    }
    loadCatalogCache();
    // Tokens of repositories removed earlier whose deletion did not work are tried again.
    QTimer::singleShot( 0, this, [ this ] { deletePendingTokens(); } );
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
        if ( isConfigPack( entry->manifest.json ) ) {
            // The counts of the effective content (with the local edits, design 4.6); a damaged
            // store is rebuilt here, before anything reads the pack.
            if ( entry->manifest.valid() ) {
                if ( const auto storeError = ensurePackStore( id, entry->manifest, false );
                     !storeError.isEmpty() ) {
                    LOG_WARNING << "Pack store not ready id=" << id.toStdString() << ": "
                                << storeError.toStdString();
                }
            }
            // The effective content folder whenever there is one, also while its repository
            // does not open: the installed folder would lose the edits for this session. Not
            // while it may hold what an unfinished update wrote, a version never installed.
            const PackStore store( packStoreRoot(), id );
            const auto content = packContentPending( id ) ? QString{} : store.contentDirectory();
            entry->pack = packSummary( entry->manifest.json,
                                       content.isEmpty() ? entry->manifest.directory : content );
            entry->pack[ "localEdits" ]
                = store.exists() ? static_cast<int>( store.localEdits().size() ) : 0;
        }
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
        value[ "kind" ] = packageKind( entry->manifest.json );
        // Pack fields (design 4.1): `contents` and the counts of what the pack delivers;
        // `localEdits` and `needsMerge` (the version an update could not merge, 4.6).
        for ( auto field = entry->pack.cbegin(); field != entry->pack.cend(); ++field ) {
            value[ field.key() ] = field.value();
        }
        if ( !entry->pack.isEmpty() ) {
            value[ "needsMerge" ] = settings_.value( configurationKey( id ) + "needsMerge" ).toString();
        }
        value[ "source" ] = sourceInfo( id, *entry );
        // The newest version from the source repository, if it is newer (design 3.8).
        const auto update = updateInfo( id, *entry );
        value[ "update" ] = update.isEmpty() ? QVariant() : QVariant( update );
        value[ "enabled" ] = settings_.value( configurationKey( id ) + "enabled", false ).toBool();
        value[ "state" ] = entry->process ? entry->process->state() : "stopped";
        value[ "error" ] = !entry->manifest.error.isEmpty() ? entry->manifest.error
                           : entry->process                 ? entry->process->error()
                                                            : QString{};
        value[ "diagnostics" ] = entry->diagnostics;
        if ( !entry->pack.isEmpty() ) {
            // MD03b: a pack's Python decoders run while the pack is enabled, on a host with the
            // Python decoder backend (the `python_decoders` host feature, design 4.4).
            value[ "pythonDecodersActive" ]
                = entry->pack.value( "pythonDecoders" ).toInt() > 0
                  && value.value( "enabled" ).toBool() && value.value( "error" ).toString().isEmpty()
                  && hostFeatureVersions().contains( QStringLiteral( "python_decoders" ) );
        }
        const auto config = QJsonDocument::fromJson(
            settings_.value( configurationKey( id ) + "configuration", "{}" ).toByteArray() );
        auto configValues = config.object().toVariantMap();
        // E17 (docs/plugin-sdk-advanced.md sections 4 and 5): unmet requirements, the plugin's
        // own announcement and host query statistics.
        value[ "missingFeatures" ] = entry->manifest.missingFeatures;
        value[ "features" ] = QStringList{};
        value[ "sdk" ] = QVariantMap{};
        value[ "queries" ] = queries_->statistics( id );
        // E17b: menu locations the user hides and the plugin's shortcuts as the windows assigned
        // them (refused ones with the reason).
        value[ "hiddenMenuLocations" ] = hiddenMenuLocations( id );
        const auto shortcuts = shortcutReport_.find( id );
        value[ "shortcuts" ]
            = shortcuts == shortcutReport_.end() ? QVariantList{} : shortcuts->second;
        if ( entry->process ) {
            value[ "pid" ] = entry->process->pid();
            const auto descriptor = entry->process->descriptor();
            if ( !descriptor.isEmpty() ) {
                value[ "actions" ] = descriptor.value( "actions" ).toArray().toVariantList();
                if ( descriptor.contains( "configuration" ) ) {
                    value[ "configuration" ]
                        = descriptor.value( "configuration" ).toArray().toVariantList();
                }
                value[ "features" ] = entry->process->features();
                value[ "sdk" ] = pluginSdk( descriptor.value( "sdk" ) ).toVariantMap();
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
        const auto job = jobs_.find( jobId );
        if ( entry->second->process && job != jobs_.end() && !job->second.outputPath.isEmpty() ) {
            // A converter writes independently; stop its process once a violation is observed.
            // fail() adds the error to the diagnostics.
            entry->second->process->fail( error );
        }
        else {
            entry->second->diagnostics
                = ( entry->second->diagnostics + error + '\n' ).right( 32768 );
            if ( entry->second->process ) {
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
bool PluginManager::managementRunning( const QString& id ) const
{
    return std::any_of( jobs_.cbegin(), jobs_.cend(), [ &id ]( const auto& pair ) {
        const auto& j = pair.second;
        return !j.invocation && j.pluginId == id && pending( j.state )
               && packageOperation( j.action );
    } );
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
    if ( j.context.value( "automatic" ).toBool() ) {
        const auto finished = j; // the signals it emits may add jobs and drop finished ones
        autoUpdateFinished( finished );
    }
    else if ( j.action == QLatin1String( "check_updates" )
              && j.state == QLatin1String( "cancelled" ) ) {
        updateCheckCancelled( id );
    }
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
    process->setHostVersion( hostVersion_ );
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
            // Views stay open across restarts; tell the new process about each of them.
            const auto views = openViews_;
            for ( const auto& view : views ) {
                if ( view.pluginId == id && readyProcess->state() == "ready" ) {
                    sendViewOpened( readyProcess, view );
                }
            }
            // E17b: menu commands that started the plugin.
            const auto commands = pendingCommands_.find( id );
            if ( commands != pendingCommands_.end() ) {
                const auto queued = std::move( commands->second );
                pendingCommands_.erase( commands );
                for ( const auto& command : queued ) {
                    if ( readyProcess->state() == "ready" ) {
                        deliverCommand( id, command );
                    }
                }
            }
            Q_EMIT pluginReady( id );
        }
    } );
    connect( process, &PluginProcess::frameTransferred, this,
             [ this, id ]( bool outgoing, const QJsonObject& frame ) {
                 Q_EMIT protocolFrame( id, outgoing, frame );
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
    for ( const auto& candidate : cilogg::pythonruntime::bundledInterpreterCandidates(
              QCoreApplication::applicationDirPath() ) ) {
        if ( python.isEmpty() && QFileInfo( candidate ).isFile() ) {
            python = candidate;
        }
    }
    if ( python.isEmpty() ) {
        python = cilogg::pythonruntime::pathInterpreterCandidates().value( 0 );
    }
    // Lets a plugin call Commander on its own instance (docs/plugin-sdk.md).
    process->start( config, configDir, python,
                    { { "CILOGG_EXECUTABLE", QCoreApplication::applicationFilePath() },
                      { "CILOGG_INSTANCE_ID", instanceId_ } } );
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
    if ( isConfigPack( e.manifest.json ) ) {
        return { "A configuration pack has no process or plugin actions.", {} };
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
    const QJsonObject invoke{ { "job_id", jobId },
                              { "action", j.action },
                              { "arguments", params },
                              { "context", QJsonObject::fromVariantMap( j.context ) } };
    // Oversized arguments (for example from a view button) fail the job, never the transport.
    if ( QJsonDocument( invoke ).toJson( QJsonDocument::Compact ).size() + 128 > MaxMessageBytes ) {
        LOG_WARNING << "Plugin job arguments exceed the message limit plugin="
                    << j.pluginId.toStdString() << " job=" << jobId.toStdString();
        finishJob( jobId, "Plugin job arguments exceed the 1 MiB message limit." );
        return;
    }
    j.state = "running";
    process->request( jobId, "invoke", invoke );
    Q_EMIT changed();
}

Result PluginManager::cancel( const QString& id )
{
    return cancelJob( id, true );
}

Result PluginManager::cancelJob( const QString& id, bool grace )
{
    const auto it = jobs_.find( id );
    if ( it == jobs_.end() ) {
        return { "Plugin job not found.", {} };
    }
    if ( !pending( it->second.state ) ) {
        return job( id );
    }
    if ( it->second.state == "cancelling" ) {
        // A closed window or file ends the grace period at once.
        if ( !grace ) {
            finishJob( id, {} );
        }
        return job( id );
    }
    it->second.cancelRequested = true;
    const auto plugin = entries_.find( it->second.pluginId );
    const bool delivered = it->second.invocation && it->second.state == "running" && !shuttingDown_
                           && plugin != entries_.end() && plugin->second->process
                           && plugin->second->process->state() == "ready";
    if ( plugin != entries_.end() && plugin->second->process ) {
        plugin->second->process->cancelRequest( id );
    }
    if ( !delivered || !grace ) {
        finishJob( id, {} );
        return job( id );
    }
    // The plugin may still report cleanup progress, panels and its final answer.
    it->second.state = "cancelling";
    LOG_DEBUG << "Plugin job cancelling plugin=" << it->second.pluginId.toStdString()
              << " job=" << id.toStdString() << " grace_ms=" << cancelGraceMs_;
    QTimer::singleShot( cancelGraceMs_, this, [ this, id ] {
        const auto cancelling = jobs_.find( id );
        if ( cancelling != jobs_.end() && cancelling->second.state == "cancelling" ) {
            LOG_DEBUG << "Plugin job cancel grace expired job=" << id.toStdString();
            finishJob( id, {} );
        }
    } );
    Q_EMIT changed();
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
    // Pending host queries end at once; their targets fall back (design 5.2).
    queries_->dropPlugin( id );
    const auto commands = pendingCommands_.find( id );
    if ( commands != pendingCommands_.end() ) {
        LOG_WARNING << "Plugin stopped before its menu commands were delivered plugin="
                    << id.toStdString() << " commands=" << commands->second.size();
        pendingCommands_.erase( commands );
    }
    for ( auto& [ key, j ] : jobs_ ) {
        if ( j.pluginId == id ) {
            j.outputSupervised = false;
        }
        if ( j.pluginId == id && j.invocation && pending( j.state ) ) {
            finishJob( key, terminalError( it->second->process->error() ) );
        }
    }
    // Open message boxes close without a result; views stay open in a stopped state.
    messages_.erase(
        std::remove_if( messages_.begin(), messages_.end(),
                        [ &id ]( const OpenMessage& message ) { return message.pluginId == id; } ),
        messages_.end() );
    Q_EMIT pluginStopped( id );
    // An automatic update waits for its plugin to stop (design 3.7); install it now. Later, so
    // the process that just stopped has finished its own signal handling. Between the two the
    // commit is no longer listed: a cancel or an exit in that moment is still seen by
    // commitWhenStopped (cancelRequested, shuttingDown_), which then removes the staged folder.
    const auto waiting = waitingCommits_.find( id );
    if ( waiting != waitingCommits_.end() ) {
        const auto commit = waiting->second;
        waitingCommits_.erase( waiting );
        QTimer::singleShot( 0, this, [ this, commit ] {
            commitWhenStopped( commit.staged, commit.jobId, commit.source );
        } );
    }
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
            cancelJob( id, false ); // the target is gone: no grace period
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
        cancelJob( id, false ); // the target window is gone: no grace period
    }
    QList<OpenView> views;
    for ( const auto& view : openViews_ ) {
        if ( view.windowId == windowId ) {
            views.append( view );
        }
    }
    for ( const auto& view : views ) {
        closeView( view.pluginId, view.viewId, view.windowId );
    }
    QList<OpenMessage> boxes;
    for ( const auto& message : messages_ ) {
        if ( message.windowId == windowId ) {
            boxes.append( message );
        }
    }
    for ( const auto& message : boxes ) {
        finishMessage( message.pluginId, message.messageId, "closed", false );
    }
    activeWindows_.removeAll( windowId );
}
void PluginManager::shutdown()
{
    shuttingDown_ = true;
    // Automatic updates still waiting for their plugin are dropped; the next start's check
    // downloads them again.
    for ( const auto& [ id, commit ] : waitingCommits_ ) {
        Q_UNUSED( id );
        QDir( commit.staged.directory ).removeRecursively();
    }
    waitingCommits_.clear();
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
    // Answers to host queries (`q:` ids) are checked against the pending queries first; jobs use
    // UUIDs, so the two never collide (design 5.2).
    if ( frame.contains( "id" ) && !frame.contains( "method" )
         && queries_->handleResponse( pluginId, frame ) ) {
        return;
    }
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
        // A late answer to a cancelled job is kept for diagnostics only: it never opens the
        // converter output or changes storage state.
        if ( error.isEmpty() && !it->second.outputPath.isEmpty() && !it->second.cancelRequested ) {
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
    else if ( method == "panel/update" && ownsJob
              && ( !jobIt->second.cancelRequested || jobIt->second.state == "cancelling" ) ) {
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
    else if ( !frame.contains( "id" )
              && ( method == "view/update" || method == "view/show" || method == "view/hide"
                   || method == "ui/message" || method == "ui/close_message"
                   || method == "tooltip/invalidate" || method == "ui/icons" ) ) {
        handleUiMessage( pluginId, method, params );
    }
    else if ( !frame.contains( "id" ) && method == "menu/update" ) {
        handleMenuUpdate( pluginId, params );
    }
    else {
        LOG_WARNING << "Plugin unknown message id=" << pluginId.toStdString()
                    << " method=" << method.toStdString();
        if ( frame.contains( "id" ) ) {
            process->fail( "Unsupported plugin-to-host request." );
        }
    }
}

void PluginManager::setPatStore( std::unique_ptr<PatStore> store )
{
    if ( store ) {
        patStore_ = std::move( store );
    }
}

Result PluginManager::manage( const QString& operation, const QString& id,
                              const QVariantMap& options, const QString& origin )
{
    // Option values are never logged: a sign-in request carries a token.
    LOG_DEBUG << "Plugin manage id=" << id.toStdString() << " operation=" << operation.toStdString()
              << " origin=" << origin.toStdString();
    if ( shuttingDown_ ) {
        return { "CILogg is shutting down.", {} };
    }
    if ( options.contains( "token" ) && operation != "repository_sign_in" ) {
        return { "token applies only to repository_sign_in.", {} };
    }
    // How a pack install or update merges with the local edits: refused elsewhere rather than
    // ignored, so a call never reports success without the merge it asked for.
    if ( operation != "install" && operation != "update" && operation != "reinstall" ) {
        for ( const auto* key : { "strategy", "resolutions", "revision" } ) {
            if ( options.contains( QLatin1String( key ) ) ) {
                return { QStringLiteral( "%1 applies only to install and update." )
                             .arg( QLatin1String( key ) ),
                         {} };
            }
        }
    }
    if ( repositoryOperation( operation ) ) {
        return manageRepositories( operation, options, origin );
    }
    if ( operation == "catalog" ) {
        return startCatalog( options, origin );
    }
    if ( operation == "check_updates" ) {
        if ( !id.isEmpty() ) {
            return { "check_updates takes no plugin ID; name a repository instead.", {} };
        }
        return checkUpdates( options, origin );
    }
    if ( operation == "skip_update" ) {
        return skipUpdate( id, options );
    }
    if ( operation == "reinstall" ) {
        // Installs the plugin again from the folder or ZIP it was installed from (set_source
        // repoints it), with the same checks as an install from that path.
        const auto reinstalled = entries_.find( id );
        if ( reinstalled == entries_.end() ) {
            return { "Plugin not found.", {} };
        }
        const auto type = sourceInfo( id, *reinstalled->second ).value( "type" ).toString();
        if ( type == QLatin1String( "repository" ) ) {
            return { "This plugin was installed from a repository: update it, or install it "
                     "again from the repository.",
                     {} };
        }
        if ( type == QLatin1String( "shipped" ) ) {
            return { "This plugin ships with CILogg and has no folder or ZIP to reinstall from.",
                     {} };
        }
        const auto path = repositories_.localSource( id );
        if ( path.isEmpty() ) {
            return { "The folder or ZIP this plugin was installed from is not known: choose it "
                     "with set_source, or install the plugin again from it.",
                     {} };
        }
        auto installOptions = options;
        installOptions.insert( "path", path );
        return manage( "install", id, installOptions, origin );
    }
    // Configuration packs edited in place (design 4.6).
    if ( packOperation( operation ) ) {
        return managePack( operation, id, options );
    }
    // How a pack update resolves conflicts with local edits outside the merge window.
    const auto strategy = options.value( "strategy" ).toString();
    if ( options.contains( "strategy" ) && strategy != QLatin1String( "fail" )
         && strategy != QLatin1String( "keep_mine" ) && strategy != QLatin1String( "take_theirs" ) ) {
        return { "strategy is fail, keep_mine or take_theirs.", {} };
    }
    if ( options.contains( "resolutions" ) && !options.value( "resolutions" ).canConvert<QVariantList>() ) {
        return { "resolutions is a list.", {} };
    }
    // The pack's edits the resolutions were chosen for (the failed job's result `revision`).
    if ( options.contains( "revision" ) && options.value( "revision" ).typeId() != QMetaType::QString ) {
        return { "revision is the revision from the failed update's result.", {} };
    }
    QVariantMap mergeContext;
    for ( const auto* key : { "strategy", "resolutions", "revision" } ) {
        if ( options.contains( QLatin1String( key ) ) ) {
            mergeContext.insert( QLatin1String( key ), options.value( QLatin1String( key ) ) );
        }
    }
    const auto installRoot = QDir( dataRoot_ ).filePath( "plugins" );
    // A catalog install or update first resolves its entry, so a call that names only the
    // entry still knows its plugin ID.
    const bool fromCatalog
        = ( operation == "install" && !options.contains( "path" ) ) || operation == "update";
    QJsonObject package;
    auto target = id;
    if ( fromCatalog ) {
        const auto selection = selectPackage( operation, id, options, &package );
        if ( !selection.ok() ) {
            return selection;
        }
        target = package.value( "id" ).toString();
    }
    if ( packageOperation( operation ) ) {
        const bool unscopedInstall
            = operation == "install" && options.contains( "path" ) && target.isEmpty();
        for ( const auto& [ jobId, j ] : jobs_ ) {
            Q_UNUSED( jobId );
            // An automatic update gives way to an operation the user starts (below).
            if ( !j.invocation && pending( j.state ) && packageOperation( j.action )
                 && !j.context.value( "automatic" ).toBool()
                 && ( unscopedInstall || ( j.action == "install" && j.pluginId.isEmpty() )
                      || ( !target.isEmpty() && j.pluginId == target ) ) ) {
                LOG_DEBUG << "Plugin management overlap rejected plugin=" << target.toStdString()
                          << " active_job=" << j.id.toStdString();
                return { "A plugin management operation is already running.", {} };
            }
        }
        // The user's operation replaces an automatic update of the same plugin (of every plugin
        // for a package whose ID is known only after staging). A restart does not: the waiting
        // update is installed while the plugin is stopped.
        if ( unscopedInstall ) {
            cancelAutoUpdate( {} );
        }
        else if ( !target.isEmpty() && operation != "restart" ) {
            cancelAutoUpdate( target );
        }
    }
    if ( operation == "install" && options.contains( "path" ) ) {
        const auto path = options.value( "path" ).toString();
        // Recorded as the plugin's source once installed, for Reinstall (set_source repoints it).
        auto context = mergeContext;
        context.insert( "sourcePath", QDir::cleanPath( QFileInfo( path ).absoluteFilePath() ) );
        const auto jobId = addJob( id, "install", context, true );
        if ( jobId.isEmpty() ) {
            return { "Too many jobs.", {} };
        }
        install( path, jobId, id );
        return job( jobId );
    }
    if ( fromCatalog ) {
        auto context = mergeContext;
        context.insert( "repository", package.value( "repository" ).toString() );
        context.insert( "version", package.value( "version" ).toString() );
        context.insert( "entryId", package.value( "entry_id" ).toString() );
        const auto jobId = addJob( target, operation, context, true );
        if ( jobId.isEmpty() ) {
            return { "Too many jobs.", {} };
        }
        LOG_INFO << "Plugin " << operation.toStdString() << " id=" << target.toStdString()
                 << " version=" << package.value( "version" ).toString().toStdString()
                 << " repository=" << package.value( "repository" ).toString().toStdString();
        fetchPackage( package, jobId, origin == RequestOriginGui );
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
    const bool pack = isConfigPack( e.manifest.json );
    if ( pack && ( operation == "restart" || operation == "configure" ) ) {
        return { QStringLiteral( "A configuration pack has no process or settings: %1 does not "
                                 "apply." )
                     .arg( operation ),
                 {} };
    }
    if ( pack && operation == "enable" && e.manifest.valid() && packCheck_ ) {
        // What loads is the effective content, with the local edits (design 4.6).
        if ( const auto storeError = ensurePackStore( id, e.manifest, false ); !storeError.isEmpty() ) {
            return { storeError, {} };
        }
        const auto refusal = packCheck_( id, packContentDirectory( id ), e.manifest.json );
        if ( !refusal.isEmpty() ) {
            LOG_INFO << "Configuration pack enable refused id=" << id.toStdString();
            return { refusal, {} };
        }
    }
    if ( operation == "remove" || operation == "disable" ) {
        revokeViews( id );
    }
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
            repositories_.clearSource( id );
            forgetUpdates( id );
            removePackStore( id ); // a pack's local edits go with it
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
    else if ( operation == "menus" ) {
        // E17b: the menu locations whose plugin entries the user hides (design 7.7); a hidden
        // menubar menu moves back under Plugins.
        QStringList hidden;
        for ( const auto& value : options.value( "hidden_locations" ).toList() ) {
            const auto location = value.toString();
            if ( !renderedMenuLocations().contains( location ) ) {
                return { QStringLiteral( "Unknown menu location: %1." ).arg( location.left( 64 ) ),
                         {} };
            }
            if ( !hidden.contains( location ) ) {
                hidden.append( location );
            }
        }
        settings_.setValue( configurationKey( id ) + "menus/hidden", hidden );
        LOG_DEBUG << "Plugin menu locations hidden plugin=" << id.toStdString()
                  << " locations=" << hidden.join( ',' ).toStdString();
    }
    else if ( operation == "set_source" ) {
        // The folder or ZIP a plugin installed without a repository is reinstalled from.
        if ( sourceInfo( id, e ).value( "type" ) != "local" ) {
            return { "set_source applies only to a plugin installed from a folder or ZIP.", {} };
        }
        const auto path = options.value( "path" ).toString();
        // Absolute only: a relative path would be read against CILogg's working folder, which a
        // CLI or MCP caller does not see.
        if ( path.isEmpty() || QDir::isRelativePath( path ) ) {
            return { "set_source needs the absolute path of the plugin's folder or ZIP.", {} };
        }
        // An install or update of this plugin, or a local install whose plugin is not known yet,
        // records its own source when it finishes: it would overwrite this one.
        const bool installing = std::any_of(
            jobs_.cbegin(), jobs_.cend(), [ &id ]( const auto& pair ) {
                const auto& j = pair.second;
                return !j.invocation && pending( j.state ) && packageOperation( j.action )
                       && ( j.pluginId == id || ( j.action == "install" && j.pluginId.isEmpty() ) );
            } );
        if ( installing ) {
            return { "A plugin management operation is already running.", {} };
        }
        const auto absolute = QDir::cleanPath( path );
        // As an install from it: a link is refused there (PluginRepository::stage).
        if ( QFileInfo( absolute ).isSymLink() ) {
            return { "Plugin source cannot be a symbolic link.", {} };
        }
        const auto found = PluginRepository::packageManifest( absolute ).value( "id" ).toString();
        if ( found.isEmpty() ) {
            return { QStringLiteral( "No plugin.json in %1." )
                         .arg( QDir::toNativeSeparators( absolute ) ),
                     {} };
        }
        if ( found != id ) {
            return { QStringLiteral( "%1 holds plugin %2, not %3." )
                         .arg( QDir::toNativeSeparators( absolute ), found, id ),
                     {} };
        }
        repositories_.setLocalSource( id, absolute );
        LOG_INFO << "Plugin source set id=" << id.toStdString();
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
            repositories_.clearSource( id );
            forgetUpdates( id );
            removePackStore( id ); // a pack's local edits go with it
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
                 // A local folder or ZIP has no source repository.
                 commitWhenStopped( staged, jobId, InstallSource{} );
             } );
    const auto root = QDir( dataRoot_ ).filePath( "plugins" );
    watcher->setFuture(
        QtConcurrent::run( [ source, root ] { return PluginRepository::stage( source, root ); } ) );
}

void PluginManager::commitWhenStopped( const StagedPlugin& staged, const QString& jobId,
                                       const InstallSource& source )
{
    const auto jobIt = jobs_.find( jobId );
    if ( shuttingDown_ || jobIt == jobs_.end() || jobIt->second.cancelRequested ) {
        QDir( staged.directory ).removeRecursively();
        return;
    }
    // A plugin that is not a pack merges nothing: choices made for a pack merge are refused, not
    // ignored, before the installed plugin is stopped (a strategy is only a policy and has nothing
    // to act on).
    const bool pack = isConfigPack( staged.manifest.json );
    const auto& context = jobIt->second.context;
    if ( !pack && ( context.contains( "resolutions" ) || context.contains( "revision" ) ) ) {
        QDir( staged.directory ).removeRecursively();
        finishJob( jobId, QStringLiteral( "Resolutions and a merge revision apply only to "
                                          "configuration packs." ) );
        return;
    }
    const auto existing = entries_.find( staged.manifest.id );
    if ( existing != entries_.end() && existing->second->process
         && existing->second->process->isRunning() ) {
        // An automatic update never stops a running plugin: it is installed when the plugin
        // stops (processStopped), and dropped if CILogg exits first (design 3.7).
        if ( jobIt->second.context.value( "automatic" ).toBool() ) {
            LOG_INFO << "Plugin automatic update waits for the plugin to stop id="
                     << staged.manifest.id.toStdString();
            discardWaitingCommit( staged.manifest.id );
            waitingCommits_[ staged.manifest.id ] = WaitingCommit{ staged, jobId, source };
            jobIt->second.result = { { "waiting", true } };
            Q_EMIT changed();
            return;
        }
        if ( existing->second->process->state() != "stopping" ) {
            existing->second->process->stop();
        }
        QTimer::singleShot( 50, this, [ this, staged, jobId, source ] {
            commitWhenStopped( staged, jobId, source );
        } );
        return;
    }
    QVariantMap result{ { "pluginId", staged.manifest.id },
                        { "version", staged.manifest.version },
                        { "repository", source.repository } };
    // A pack's local edits merge with the new version first (design 4.6): conflicts the job's
    // strategy leaves open keep the installed version and are listed in the result.
    PackFiles packShipped;
    std::optional<PackFiles> packMerged;
    auto error = pack ? preparePackInstall( staged, context, &packShipped, &packMerged, &result )
                      : QString{};
    if ( result.value( "needsMerge" ).toBool() ) {
        // Shown as "Update to X needs a merge" until an install or update of the pack succeeds.
        settings_.setValue( configurationKey( staged.manifest.id ) + "needsMerge",
                            staged.manifest.version );
        if ( !persistSettings().isEmpty() ) {
            LOG_WARNING << "Pack merge state could not be saved id="
                        << staged.manifest.id.toStdString();
        }
    }
    // A new version of an enabled pack must still fit with the user's items and the other
    // enabled packs; otherwise the installed version stays (design 4.2). What loads is checked:
    // the merge with the user's edits when there is one (design 4.6), which may lack an item the
    // new version ships and an edit deleted, or else the new version as shipped.
    if ( error.isEmpty() && pack && packCheck_
         && settings_.value( configurationKey( staged.manifest.id ) + "enabled", false ).toBool() ) {
        error = packMerged && *packMerged != packShipped
                    ? checkPackFiles( staged.manifest.id, *packMerged, staged.manifest.json )
                    : packCheck_( staged.manifest.id, staged.manifest.directory,
                                  staged.manifest.json );
    }
    // A pack's store moves before the package folder: it can be put back, the folder swap
    // cannot. A store that fails leaves the installed version as it is, and a package that
    // fails puts the store back, so the version and the content never disagree. The journal
    // written first covers CILogg ending in between: the next read of the pack puts it back.
    PackTips packBefore;
    bool journal = false; // this install's journal is still needed (the store was not put back)
    bool began = false;   // this install wrote its journal
    const auto putBack = [ this, &staged, &packBefore ] {
        const auto restored = PackStore( packStoreRoot(), staged.manifest.id ).rollback( packBefore );
        if ( !restored.isEmpty() ) {
            LOG_WARNING << "Pack store not put back id=" << staged.manifest.id.toStdString();
        }
        return restored.isEmpty();
    };
    if ( error.isEmpty() && pack ) {
        PackStore store( packStoreRoot(), staged.manifest.id );
        packBefore = store.tips();
        // A new journal is not settled until this install or update is done or put back.
        settledJournals_.remove( staged.manifest.id );
        // A store that is there but whose tips cannot be read now could not be put back.
        error = packBefore.existed && ( packBefore.upstream.isEmpty() || packBefore.local.isEmpty() )
                    ? QStringLiteral( "The pack's store cannot be opened now (another program may "
                                      "be using it). Try again." )
                    : store.beginUpdate( packBefore );
        journal = error.isEmpty();
        began = journal;
        if ( journal ) {
            error = finishPackInstall( staged, packShipped, packMerged, source );
        }
        if ( !error.isEmpty() ) {
            error = QStringLiteral( "The pack's local store could not be updated, so the "
                                    "installed version stays: %1" )
                        .arg( error );
            // Partly written: put it back (a store that moved nothing is unchanged anyway).
            journal = journal && !putBack();
        }
    }
    if ( error.isEmpty() ) {
        error = PluginRepository::commit( staged, QDir( dataRoot_ ).filePath( "plugins" ) );
        journal = journal && !error.isEmpty() && pack && !putBack();
    }
    // Done, or put back: this install's journal goes. One that stays (a store not put back) is
    // put back by the next read of the pack, and so is an earlier journal this install never
    // replaced (it failed before writing its own, perhaps recovering that one): never removed here.
    if ( pack && began && !journal ) {
        PackStore store( packStoreRoot(), staged.manifest.id );
        store.endUpdate();
        // Done or put back, but the journal could not be removed: the content folder is current
        // all the same, so the pack loads it (the next read of the pack settles it again).
        if ( store.hasJournal() ) {
            settledJournals_.insert( staged.manifest.id );
        }
    }
    if ( error.isEmpty() && pack ) {
        // Installed with its store: no update waits for a merge any more.
        settings_.remove( configurationKey( staged.manifest.id ) + "needsMerge" );
    }
    if ( error.isEmpty() ) {
        // The install source lives in the plugin settings, not in the plugin folder. The plugin
        // is installed at this point, so a settings write failure is a warning, not a failure:
        // retrying would only install the same package again.
        repositories_.setSource( staged.manifest.id, source );
        const auto localPath = jobIt->second.context.value( "sourcePath" ).toString();
        if ( source.repository.isEmpty() && !localPath.isEmpty() ) {
            repositories_.setLocalSource( staged.manifest.id, localPath );
        }
        if ( !jobIt->second.context.value( "automatic" ).toBool() ) {
            clearAutoUpdateFailure( staged.manifest.id ); // reported again if it fails again
        }
        if ( !persistSettings().isEmpty() ) {
            LOG_WARNING << "Plugin installed but its source record could not be saved id="
                        << staged.manifest.id.toStdString();
            // Added to a warning the install may already have (dropped pack edits).
            const auto earlier = result.value( "warning" ).toString();
            result[ "warning" ]
                = ( earlier.isEmpty() ? QString{} : earlier + QLatin1Char( ' ' ) )
                  + QStringLiteral( "The plugin was installed, but its source repository could "
                                    "not be saved in the plugin settings." );
        }
        entries_.erase( staged.manifest.id );
        discover();
    }
    else {
        QDir( staged.directory ).removeRecursively();
    }
    finishJob( jobId, error, result );
}

// Plugin views and message boxes (docs/plugin-views.md).
QJsonArray PluginManager::viewDefinitions( const QString& pluginId ) const
{
    const auto it = entries_.find( pluginId );
    return it == entries_.end() || !it->second->manifest.valid()
               ? QJsonArray{}
               : it->second->manifest.json.value( "views" ).toArray();
}

bool PluginManager::declaresView( const QString& pluginId, const QString& viewId ) const
{
    const auto views = viewDefinitions( pluginId );
    return std::any_of( views.begin(), views.end(), [ &viewId ]( const QJsonValue& view ) {
        return view.toObject().value( "id" ).toString() == viewId;
    } );
}

void PluginManager::sendViewOpened( PluginProcess* process, const OpenView& view )
{
    LOG_DEBUG << "Plugin view opened plugin=" << view.pluginId.toStdString()
              << " view=" << view.viewId.toStdString() << " window=" << view.windowId.toStdString();
    process->notify( "view/opened",
                     { { "view_id", view.viewId },
                       { "window_id", view.windowId },
                       { "context", QJsonObject::fromVariantMap( view.context ) } } );
}

Result PluginManager::openView( const QString& pluginId, const QString& viewId,
                                const QVariantMap& context )
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
    if ( !declaresView( pluginId, viewId ) ) {
        return { "Plugin view not declared in plugin.json.", {} };
    }
    if ( e.process && ( e.process->state() == "failed" || e.process->state() == "stopping" ) ) {
        return { "Plugin failed or is stopping; restart it explicitly.", {} };
    }
    const auto windowId = context.value( "windowId" ).toString();
    auto open = std::find_if( openViews_.begin(), openViews_.end(), [ & ]( const OpenView& view ) {
        return view.pluginId == pluginId && view.viewId == viewId && view.windowId == windowId;
    } );
    if ( open == openViews_.end() ) {
        openViews_.push_back( { pluginId, viewId, windowId, context } );
        open = std::prev( openViews_.end() );
    }
    else {
        open->context = context;
    }
    const auto view = *open;
    auto* process = ensureProcess( e );
    if ( process->state() == "failed" ) {
        openViews_.erase( std::remove_if( openViews_.begin(), openViews_.end(),
                                          [ & ]( const OpenView& candidate ) {
                                              return candidate.pluginId == pluginId
                                                     && candidate.viewId == viewId
                                                     && candidate.windowId == windowId;
                                          } ),
                          openViews_.end() );
        return { terminalError( process->error() ), {} };
    }
    if ( process->state() == "ready" ) {
        sendViewOpened( process, view );
    }
    Q_EMIT changed();
    return { {},
             { { "pluginId", pluginId },
               { "viewId", viewId },
               { "windowId", windowId },
               { "state", process->state() } } };
}

void PluginManager::closeView( const QString& pluginId, const QString& viewId,
                               const QString& windowId )
{
    closeView( pluginId, viewId, windowId, {} );
}

void PluginManager::closeView( const QString& pluginId, const QString& viewId,
                               const QString& windowId, const QJsonObject& extra )
{
    const auto open = std::find_if(
        openViews_.begin(), openViews_.end(), [ & ]( const OpenView& view ) {
            return view.pluginId == pluginId && view.viewId == viewId && view.windowId == windowId;
        } );
    if ( open == openViews_.end() ) {
        return;
    }
    openViews_.erase( open );
    LOG_DEBUG << "Plugin view closed plugin=" << pluginId.toStdString()
              << " view=" << viewId.toStdString() << " window=" << windowId.toStdString();
    queries_->dropScope( pluginId, viewScope( windowId, viewId ) );
    const auto entry = entries_.find( pluginId );
    if ( entry != entries_.end() && entry->second->process
         && entry->second->process->state() == "ready" ) {
        QJsonObject params{ { "view_id", viewId }, { "window_id", windowId } };
        for ( auto it = extra.begin(); it != extra.end(); ++it ) {
            params.insert( it.key(), it.value() );
        }
        // The inputs are bounded by the view; still never pass the transport limit.
        if ( QJsonDocument( params ).toJson( QJsonDocument::Compact ).size() + 128
             > MaxEventBytes ) {
            params.insert( "inputs", QJsonObject{} );
            params.insert( "truncated", true );
        }
        entry->second->process->notify( "view/closed", params );
    }
}

bool PluginManager::sendViewEvent( const QString& pluginId, const QString& viewId,
                                   const QString& windowId, const QJsonObject& event )
{
    const auto open = std::find_if(
        openViews_.begin(), openViews_.end(), [ & ]( const OpenView& view ) {
            return view.pluginId == pluginId && view.viewId == viewId && view.windowId == windowId;
        } );
    const auto entry = entries_.find( pluginId );
    if ( open == openViews_.end() || entry == entries_.end() || !entry->second->process
         || entry->second->process->state() != "ready" ) {
        return false;
    }
    auto params = event;
    params.insert( "view_id", viewId );
    params.insert( "window_id", windowId );
    // User input must never exceed the transport limit (which would fail the plugin): drop
    // the inputs, then the value, and mark the event as truncated.
    const auto size = [ &params ] {
        return QJsonDocument( params ).toJson( QJsonDocument::Compact ).size() + 128;
    };
    if ( size() > MaxEventBytes ) {
        params.insert( "inputs", QJsonObject{} );
        params.insert( "truncated", true );
        if ( size() > MaxEventBytes ) {
            params.insert( "value", QJsonValue::Null );
        }
        if ( size() > MaxEventBytes ) {
            LOG_WARNING << "Plugin view event dropped: too large plugin=" << pluginId.toStdString()
                        << " view=" << viewId.toStdString();
            return false;
        }
        LOG_WARNING << "Plugin view event truncated plugin=" << pluginId.toStdString()
                    << " view=" << viewId.toStdString();
    }
    if ( entry->second->process->pendingOutputBytes() + size() > MaxPendingBytes / 2 ) {
        // The plugin is not reading its input: drop user events rather than fail the transport.
        LOG_WARNING << "Plugin view event dropped: plugin input backlog plugin="
                    << pluginId.toStdString();
        return false;
    }
    LOG_DEBUG << "Plugin view event plugin=" << pluginId.toStdString()
              << " view=" << viewId.toStdString()
              << " element=" << event.value( "element_id" ).toString().left( 128 ).toStdString()
              << " type=" << event.value( "type" ).toString().left( 16 ).toStdString();
    entry->second->process->notify( "view/event", params );
    return true;
}

void PluginManager::rejectMessage( const QString& pluginId, const QString& messageId,
                                   const QString& error )
{
    LOG_WARNING << "Plugin message rejected plugin=" << pluginId.toStdString()
                << " message=" << messageId.left( 128 ).toStdString()
                << " reason=" << error.toStdString();
    const auto entry = entries_.find( pluginId );
    if ( entry != entries_.end() && entry->second->process ) {
        entry->second->process->notify( "ui/message_result", { { "message_id", messageId },
                                                               { "button", "" },
                                                               { "timed_out", false },
                                                               { "error", error } } );
    }
}

void PluginManager::finishMessage( const QString& pluginId, const QString& messageId,
                                   const QString& button, bool timedOut )
{
    const auto open
        = std::find_if( messages_.begin(), messages_.end(), [ & ]( const OpenMessage& message ) {
              return message.pluginId == pluginId && message.messageId == messageId;
          } );
    if ( open == messages_.end() ) {
        return;
    }
    messages_.erase( open );
    LOG_DEBUG << "Plugin message finished plugin=" << pluginId.toStdString()
              << " message=" << messageId.toStdString() << " button=" << button.toStdString()
              << " timed_out=" << timedOut;
    const auto entry = entries_.find( pluginId );
    if ( entry != entries_.end() && entry->second->process
         && entry->second->process->state() == "ready" ) {
        entry->second->process->notify(
            "ui/message_result",
            { { "message_id", messageId }, { "button", button }, { "timed_out", timedOut } } );
    }
}

void PluginManager::noteActiveWindow( const QString& windowId )
{
    if ( windowId.isEmpty() ) {
        return;
    }
    activeWindows_.removeAll( windowId );
    activeWindows_.prepend( windowId );
}

QString PluginManager::resolveWindow( const QString& requested ) const
{
    if ( !requested.isEmpty() ) {
        return activeWindows_.contains( requested ) ? requested : QString{};
    }
    return activeWindows_.isEmpty() ? QString{} : activeWindows_.front();
}

void PluginManager::revokeViews( const QString& pluginId )
{
    const auto before = openViews_.size() + messages_.size();
    openViews_.erase( std::remove_if( openViews_.begin(), openViews_.end(),
                                      [ &pluginId ]( const OpenView& view ) {
                                          return view.pluginId == pluginId;
                                      } ),
                      openViews_.end() );
    messages_.erase( std::remove_if( messages_.begin(), messages_.end(),
                                     [ &pluginId ]( const OpenMessage& message ) {
                                         return message.pluginId == pluginId;
                                     } ),
                     messages_.end() );
    LOG_DEBUG << "Plugin views revoked plugin=" << pluginId.toStdString()
              << " entries=" << before - openViews_.size() - messages_.size();
    Q_EMIT viewsRevoked( pluginId );
}

void PluginManager::handleUiMessage( const QString& pluginId, const QString& method,
                                     const QJsonObject& params )
{
    const auto size
        = QJsonDocument( params ).toJson( QJsonDocument::Compact ).size(); // ids and sizes only
    if ( method == "ui/icons" ) {
        // Registered images (docs/plugin-sdk-advanced.md section 6), checked and decoded by the
        // UI's icon registry; they need no view.
        if ( !params.value( "icons" ).isObject() && !params.value( "remove" ).isArray() ) {
            LOG_WARNING << "Plugin ui/icons needs icons or remove plugin=" << pluginId.toStdString();
            return;
        }
        LOG_DEBUG << "Plugin icons plugin=" << pluginId.toStdString() << " bytes=" << size;
        Q_EMIT viewRequest( pluginId, method, params );
        return;
    }
    if ( method == "ui/message" || method == "ui/close_message" ) {
        const auto messageId = params.value( "message_id" ).toString();
        if ( messageId.isEmpty() || messageId.size() > 128 ) {
            LOG_WARNING << "Plugin message without a valid message_id plugin="
                        << pluginId.toStdString();
            return;
        }
        const auto open = std::find_if(
            messages_.begin(), messages_.end(), [ & ]( const OpenMessage& message ) {
                return message.pluginId == pluginId && message.messageId == messageId;
            } );
        if ( method == "ui/close_message" ) {
            if ( open != messages_.end() ) {
                Q_EMIT viewRequest(
                    pluginId, method,
                    { { "message_id", messageId }, { "window_id", open->windowId } } );
            }
            return;
        }
        if ( open != messages_.end() ) {
            rejectMessage( pluginId, messageId, "A message box with this id is already open." );
            return;
        }
        const auto count = std::count_if(
            messages_.begin(), messages_.end(),
            [ & ]( const OpenMessage& message ) { return message.pluginId == pluginId; } );
        if ( count >= MaxOpenMessages ) {
            rejectMessage( pluginId, messageId,
                           QStringLiteral( "Too many open message boxes (maximum %1)." )
                               .arg( MaxOpenMessages ) );
            return;
        }
        // Boxes take the focus: a plugin may show only a few within a sliding window.
        if ( !clock_.isValid() ) {
            clock_.start();
        }
        auto& shown = messageTimes_[ pluginId ];
        const auto now = clock_.elapsed();
        while ( !shown.empty() && now - shown.front() >= messageRateWindowMs_ ) {
            shown.pop_front();
        }
        if ( static_cast<int>( shown.size() ) >= messageRateCount_ ) {
            rejectMessage( pluginId, messageId,
                           QStringLiteral( "Message box rate limit: at most %1 per %2 ms." )
                               .arg( messageRateCount_ )
                               .arg( messageRateWindowMs_ ) );
            return;
        }
        QJsonArray buttons;
        for ( const auto& value : params.value( "buttons" ).toArray( { "ok" } ) ) {
            if ( !MessageButtons.contains( value.toString() ) || buttons.contains( value ) ) {
                rejectMessage( pluginId, messageId, "Invalid message box button." );
                return;
            }
            buttons.append( value );
        }
        const auto icon = params.value( "icon" ).toString( "info" );
        const auto timeout = params.value( "timeout_ms" );
        if ( buttons.isEmpty()
             || !QStringList{ "info", "warning", "error", "question" }.contains( icon )
             || ( params.contains( "default" ) && !buttons.contains( params.value( "default" ) ) )
             || ( !timeout.isUndefined() && !timeout.isNull()
                  && ( !timeout.isDouble() || timeout.toDouble() < 1
                       || timeout.toDouble() > 24.0 * 3600 * 1000 ) )
             || ( params.contains( "modal" ) && !params.value( "modal" ).isBool() ) ) {
            rejectMessage( pluginId, messageId, "Invalid message box parameters." );
            return;
        }
        const auto windowId = resolveWindow( params.value( "window_id" ).toString() );
        if ( windowId.isEmpty() ) {
            rejectMessage( pluginId, messageId, "No CILogg window can show the message box." );
            return;
        }
        QJsonObject message{
            { "message_id", messageId },
            { "window_id", windowId },
            { "title", params.value( "title" ).toString().left( 256 ) },
            { "text", params.value( "text" ).toString().left( MaxMessageText ) },
            { "icon", icon },
            { "buttons", buttons },
            { "default", params.value( "default" ).toString( buttons.first().toString() ) },
            { "modal", params.value( "modal" ).toBool( true ) },
        };
        if ( timeout.isDouble() ) {
            message.insert( "timeout_ms", static_cast<qint64>( timeout.toDouble() ) );
        }
        messages_.push_back( { pluginId, messageId, windowId } );
        shown.push_back( now );
        LOG_DEBUG << "Plugin message shown plugin=" << pluginId.toStdString()
                  << " message=" << messageId.toStdString() << " window=" << windowId.toStdString()
                  << " bytes=" << size;
        Q_EMIT viewRequest( pluginId, method, message );
        return;
    }
    const auto viewId = params.value( "view_id" ).toString();
    if ( !declaresView( pluginId, viewId ) ) {
        LOG_WARNING << "Plugin message for an undeclared view plugin=" << pluginId.toStdString()
                    << " method=" << method.toStdString()
                    << " view=" << viewId.left( 128 ).toStdString();
        return;
    }
    if ( params.contains( "window_id" ) && !params.value( "window_id" ).isString() ) {
        LOG_WARNING << "Plugin view message with an invalid window_id plugin="
                    << pluginId.toStdString();
        return;
    }
    if ( method == "view/update" ) {
        if ( !params.value( "root" ).isObject() && !params.value( "patch" ).isArray() ) {
            LOG_WARNING << "Plugin view/update needs root or patch plugin="
                        << pluginId.toStdString() << " view=" << viewId.toStdString();
            return;
        }
        LOG_DEBUG << "Plugin view update plugin=" << pluginId.toStdString()
                  << " view=" << viewId.toStdString()
                  << " kind=" << ( params.value( "root" ).isObject() ? "root" : "patch" )
                  << " bytes=" << size;
        Q_EMIT viewRequest( pluginId, method, params );
    }
    else if ( method == "view/show" ) {
        auto resolved = params;
        const auto windowId = resolveWindow( params.value( "window_id" ).toString() );
        if ( windowId.isEmpty() ) {
            LOG_WARNING << "Plugin view/show has no target window plugin="
                        << pluginId.toStdString();
            return;
        }
        resolved.insert( "window_id", windowId );
        LOG_DEBUG << "Plugin view show plugin=" << pluginId.toStdString()
                  << " view=" << viewId.toStdString() << " window=" << windowId.toStdString();
        Q_EMIT viewRequest( pluginId, method, resolved );
    }
    else if ( method == "tooltip/invalidate" ) {
        LOG_DEBUG << "Plugin tooltips invalidated plugin=" << pluginId.toStdString()
                  << " view=" << viewId.toStdString()
                  << " element=" << params.value( "element_id" ).toString().left( 128 ).toStdString();
        Q_EMIT viewRequest( pluginId, method, params );
    }
    else {
        LOG_DEBUG << "Plugin view hide plugin=" << pluginId.toStdString()
                  << " view=" << viewId.toStdString();
        Q_EMIT viewRequest( pluginId, method, params );
    }
}

QVariantMap PluginManager::viewState( const QString& pluginId, const QString& viewId ) const
{
    return QJsonDocument::fromJson(
               settings_.value( configurationKey( pluginId ) + "views/" + settingsSafe( viewId ) )
                   .toByteArray() )
        .object()
        .toVariantMap();
}

void PluginManager::setViewState( const QString& pluginId, const QString& viewId,
                                  const QVariantMap& state )
{
    settings_.setValue(
        configurationKey( pluginId ) + "views/" + settingsSafe( viewId ),
        QJsonDocument( QJsonObject::fromVariantMap( state ) ).toJson( QJsonDocument::Compact ) );
}

void PluginManager::setInstanceId( const QString& id )
{
    if ( !id.isEmpty() ) {
        instanceId_ = id;
    }
}

void PluginManager::setCancelGraceMs( int milliseconds )
{
    cancelGraceMs_ = qBound( 0, milliseconds, 600000 );
}

void PluginManager::setMessageRateLimit( int count, int windowMs )
{
    messageRateCount_ = qMax( 1, count );
    messageRateWindowMs_ = qMax( 1, windowMs );
}

QString PluginManager::pluginState( const QString& pluginId ) const
{
    const auto it = entries_.find( pluginId );
    if ( it == entries_.end() ) {
        return {};
    }
    return it->second->process ? it->second->process->state() : QStringLiteral( "stopped" );
}

QString PluginManager::viewScope( const QString& windowId, const QString& viewId )
{
    return QStringLiteral( "view:" ) + windowId + '/' + viewId;
}

bool PluginManager::declaresHandler( const QString& pluginId, const QString& method ) const
{
    const auto it = entries_.find( pluginId );
    return it != entries_.end() && it->second->manifest.valid()
           && handledMethods( it->second->manifest.json.value( "handles" ) ).contains( method );
}

bool PluginManager::declaresHandles( const QString& pluginId ) const
{
    const auto it = entries_.find( pluginId );
    return it != entries_.end() && it->second->manifest.valid()
           && !handledMethods( it->second->manifest.json.value( "handles" ) ).isEmpty();
}

bool PluginManager::handles( const QString& pluginId, const QString& method ) const
{
    const auto it = entries_.find( pluginId );
    if ( it == entries_.end() || !it->second->process
         || it->second->process->state() != "ready" ) {
        return false;
    }
    const bool declared = declaresHandler( pluginId, method );
    const bool announced = it->second->process->features().contains( method );
    if ( declared != announced ) {
        // The initialize result is the authority while the plugin runs (design 4.2).
        const auto key = pluginId + '|' + method;
        if ( !featureWarnings_.contains( key ) ) {
            featureWarnings_.insert( key );
            LOG_WARNING << "Plugin method listed in only one of handles and features, not sent "
                        << "plugin=" << pluginId.toStdString() << " method=" << method.toStdString()
                        << " handles=" << declared << " features=" << announced;
        }
    }
    return declared && announced;
}

bool PluginManager::sendQueryFrame( const QString& pluginId, const QString& id,
                                    const QString& method, const QJsonObject& params )
{
    const auto it = entries_.find( pluginId );
    if ( it == entries_.end() || !it->second->process
         || it->second->process->state() != "ready" ) {
        return false; // never through the start-up queue (design 5.2)
    }
    if ( id.isEmpty() && !handles( pluginId, method ) ) {
        return false; // query/cancel only to plugins that declared it
    }
    auto* process = it->second->process.get();
    const auto size = QJsonDocument( params ).toJson( QJsonDocument::Compact ).size() + 128;
    if ( process->pendingOutputBytes() + size > MaxPendingBytes / 2 ) {
        // The plugin is not reading its input: the target falls back (design 5.2).
        LOG_DEBUG << "Plugin query not sent: plugin input backlog plugin="
                  << pluginId.toStdString() << " method=" << method.toStdString();
        return false;
    }
    if ( id.isEmpty() ) {
        process->notify( method, params );
    }
    else {
        process->request( id, method, params );
    }
    return process->state() == "ready";
}

QString PluginManager::query( const QString& pluginId, const QString& method,
                              const QJsonObject& params, int timeoutMs,
                              const QString& exclusiveKey, const QString& scope,
                              PluginQueries::Callback callback, QString* reason )
{
    const auto refuse = [ reason ]( const QString& text ) {
        if ( reason ) {
            *reason = text;
        }
        return QString{};
    };
    if ( shuttingDown_ ) {
        return refuse( QStringLiteral( "shutting down" ) );
    }
    if ( !isQueryMethod( method ) ) {
        return refuse( QStringLiteral( "not a query method" ) );
    }
    if ( !handles( pluginId, method ) ) {
        return refuse( pluginState( pluginId ) == "ready" ? QStringLiteral( "not handled" )
                                                          : QStringLiteral( "not ready" ) );
    }
    auto bounded = params;
    const auto timeout = boundedQueryTimeout( method, timeoutMs );
    bounded.insert( "timeout_ms", timeout );
    PluginQueries::Validator validator;
    if ( method == QLatin1String( "tooltip/resolve" ) ) {
        validator = []( const QJsonValue& result, QString* error ) -> QJsonValue {
            return sanitizeTooltip( result, error );
        };
    }
    else if ( method == QLatin1String( "field/complete" ) ) {
        validator = []( const QJsonValue& result, QString* error ) -> QJsonValue {
            return sanitizeCompletion( result, error );
        };
    }
    else if ( method == QLatin1String( "tree/children" ) ) {
        validator = []( const QJsonValue& result, QString* error ) -> QJsonValue {
            return sanitizeTreeChildren( result, error );
        };
    }
    else if ( method == QLatin1String( "menu/populate" ) ) {
        // Entries may name only the actions and views the manifest declares (design 7.4).
        const auto& manifest = entries_.at( pluginId )->manifest.json;
        validator = [ actions = declaredIds( manifest.value( "actions" ) ),
                      views = declaredIds( manifest.value( "views" ) ) ](
                        const QJsonValue& result, QString* error ) -> QJsonValue {
            return sanitizeMenuPopulate( result, actions, views, error );
        };
    }
    PluginQueries::Request request;
    request.pluginId = pluginId;
    request.method = method;
    request.params = bounded;
    request.timeoutMs = timeout;
    request.exclusiveKey = exclusiveKey;
    request.scope = scope;
    return queries_->send( request, std::move( validator ), std::move( callback ), reason );
}

void PluginManager::cancelQuery( const QString& id )
{
    queries_->cancel( id );
}

Result PluginManager::usablePlugin( const QString& pluginId ) const
{
    if ( shuttingDown_ ) {
        return { "CILogg is shutting down.", {} };
    }
    const auto it = entries_.find( pluginId );
    if ( it == entries_.end() ) {
        return { "Plugin not found.", {} };
    }
    const auto& e = *it->second;
    if ( !e.manifest.valid() ) {
        return { e.manifest.error, {} };
    }
    if ( !settings_.value( configurationKey( pluginId ) + "enabled", false ).toBool() ) {
        return { "Plugin is disabled.", {} };
    }
    if ( e.process && ( e.process->state() == "failed" || e.process->state() == "stopping" ) ) {
        return { "Plugin failed or is stopping; restart it explicitly.", {} };
    }
    return {};
}

std::vector<PluginManager::MenuSource> PluginManager::menuSources() const
{
    std::vector<MenuSource> sources;
    for ( const auto& [ id, entry ] : entries_ ) {
        if ( !entry->manifest.valid()
             || !settings_.value( configurationKey( id ) + "enabled", false ).toBool() ) {
            continue;
        }
        sources.push_back( { id, entry->manifest.name, entry->manifest.directory,
                             entry->manifest.json, hiddenMenuLocations( id ) } );
    }
    std::sort( sources.begin(), sources.end(), []( const MenuSource& a, const MenuSource& b ) {
        const auto order = QString::compare( a.name, b.name, Qt::CaseInsensitive );
        return order != 0 ? order < 0 : a.id < b.id;
    } );
    return sources;
}

Result PluginManager::startPlugin( const QString& pluginId )
{
    const auto usable = usablePlugin( pluginId );
    if ( !usable.ok() ) {
        return usable;
    }
    auto* process = ensureProcess( *entries_.at( pluginId ) );
    if ( process->state() == "failed" ) {
        return { terminalError( process->error() ), {} };
    }
    return { {}, { { "state", process->state() } } };
}

Result PluginManager::sendCommand( const QString& pluginId, const QJsonObject& params )
{
    const auto usable = usablePlugin( pluginId );
    if ( !usable.ok() ) {
        return usable;
    }
    if ( !declaresHandler( pluginId, QStringLiteral( "ui/command" ) ) ) {
        return { "This plugin needs a newer SDK (ui/command is not in handles).", {} };
    }
    if ( QJsonDocument( params ).toJson( QJsonDocument::Compact ).size() + 128 > MaxEventBytes ) {
        return { "The menu command is too large.", {} };
    }
    auto* process = ensureProcess( *entries_.at( pluginId ) );
    if ( process->state() == "failed" ) {
        return { terminalError( process->error() ), {} };
    }
    if ( process->state() != "ready" ) {
        // Delivered when the plugin is ready (design 7.5), a few at most.
        auto& waiting = pendingCommands_[ pluginId ];
        if ( static_cast<int>( waiting.size() ) >= MaxQueuedCommands ) {
            return { "Too many menu commands are waiting for the plugin to start.", {} };
        }
        waiting.push_back( params );
        LOG_DEBUG << "Plugin menu command queued until ready plugin=" << pluginId.toStdString()
                  << " item=" << params.value( "item_id" ).toString().left( 128 ).toStdString();
        return { {}, { { "state", process->state() }, { "queued", true } } };
    }
    if ( !deliverCommand( pluginId, params ) ) {
        return { "The plugin is not reading its input.", {} };
    }
    return { {}, { { "state", process->state() }, { "queued", false } } };
}

bool PluginManager::deliverCommand( const QString& pluginId, const QJsonObject& params )
{
    auto* process = entries_.at( pluginId )->process.get();
    const auto size = QJsonDocument( params ).toJson( QJsonDocument::Compact ).size() + 128;
    if ( process->pendingOutputBytes() + size > MaxPendingBytes / 2 ) {
        LOG_WARNING << "Plugin menu command dropped: plugin input backlog plugin="
                    << pluginId.toStdString();
        return false;
    }
    // Ids and sizes only: the selected text never enters the log.
    LOG_DEBUG << "Plugin menu command plugin=" << pluginId.toStdString()
              << " origin=" << params.value( "origin" ).toString().left( 16 ).toStdString()
              << " menu=" << params.value( "menu_id" ).toString().left( 128 ).toStdString()
              << " item=" << params.value( "item_id" ).toString().left( 128 ).toStdString()
              << " bytes=" << size;
    process->notify( "ui/command", params );
    return true;
}

void PluginManager::handleMenuUpdate( const QString& pluginId, const QJsonObject& params )
{
    const auto it = entries_.find( pluginId );
    if ( it == entries_.end() ) {
        return;
    }
    QString error;
    const auto changes = sanitizeMenuUpdate( it->second->manifest.json, params, &error );
    if ( changes.isEmpty() ) {
        LOG_WARNING << "Plugin menu/update ignored plugin=" << pluginId.toStdString()
                    << " menu=" << params.value( "menu_id" ).toString().left( 128 ).toStdString()
                    << " reason=" << error.toStdString();
        return;
    }
    LOG_DEBUG << "Plugin menu update plugin=" << pluginId.toStdString()
              << " menu=" << params.value( "menu_id" ).toString().toStdString()
              << " entries=" << changes.size();
    Q_EMIT menuUpdated( pluginId, params.value( "menu_id" ).toString(), changes );
}

QStringList PluginManager::hiddenMenuLocations( const QString& pluginId ) const
{
    return settings_.value( configurationKey( pluginId ) + "menus/hidden" ).toStringList();
}

namespace {
// A view id as a settings key part: hex, so no id can reach another key.
QString viewStateKey( const QString& viewId )
{
    return QStringLiteral( "views/%1/state" ).arg( QString::fromLatin1( viewId.toUtf8().toHex() ) );
}
} // namespace

QJsonObject PluginManager::keptViewState( const QString& pluginId, const QString& viewId ) const
{
    const auto bytes
        = settings_.value( configurationKey( pluginId ) + viewStateKey( viewId ) ).toByteArray();
    return bytes.isEmpty() ? QJsonObject{} : QJsonDocument::fromJson( bytes ).object();
}

bool PluginManager::setKeptViewState( const QString& pluginId, const QString& viewId,
                                      const QJsonObject& state )
{
    const auto bytes = QJsonDocument( state ).toJson( QJsonDocument::Compact );
    if ( bytes.size() > MaxViewStateBytes ) {
        return false;
    }
    settings_.setValue( configurationKey( pluginId ) + viewStateKey( viewId ), bytes );
    return true;
}

void PluginManager::setShortcutReport( const std::map<QString, QVariantList>& report )
{
    if ( report == shortcutReport_ ) {
        return;
    }
    shortcutReport_ = report;
    Q_EMIT changed();
}

bool PluginManager::isPluginCapture( const QString& path ) const
{
    if ( path.isEmpty() || dataRoot_.isEmpty() ) {
        return false;
    }
    const auto captures = QDir::cleanPath( QDir( dataRoot_ ).absoluteFilePath( "captures" ) ) + '/';
    return QDir::cleanPath( QFileInfo( path ).absoluteFilePath() )
        .startsWith( captures, Qt::CaseInsensitive );
}

void PluginManager::setPackCheck( PackCheck check )
{
    packCheck_ = std::move( check );
}

QJsonObject PluginManager::manifest( const QString& pluginId ) const
{
    const auto it = entries_.find( pluginId );
    return it == entries_.end() ? QJsonObject{} : it->second->manifest.json;
}

QString PluginManager::pluginDirectory( const QString& pluginId ) const
{
    const auto it = entries_.find( pluginId );
    return it == entries_.end() ? QString{} : it->second->manifest.directory;
}

QStringList PluginManager::enabledPacks() const
{
    QStringList ids;
    for ( const auto& [ id, entry ] : entries_ ) {
        if ( entry->manifest.valid() && isConfigPack( entry->manifest.json )
             && settings_.value( configurationKey( id ) + "enabled", false ).toBool() ) {
            ids.append( id );
        }
    }
    return ids; // entries_ is ordered by ID
}
} // namespace cilogg::plugins
