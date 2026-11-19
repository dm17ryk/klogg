// Update checks, Skip this version, automatic updates and the saved catalogs of PluginManager
// (docs/plugin-repositories.md sections 3.7 and 3.8, task E15c).
#include "log.h"
#include "pluginmanager.h"
#include "semanticversion.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace cilogg::plugins {
namespace {
// A repository checked less than this long ago is skipped by the silent check (design 3.7).
constexpr qint64 UpdateCheckIntervalSecs = 24 * 60 * 60;
// A last check this far in the future means the clock was set back: the repository is due.
constexpr qint64 ClockSkewSecs = 5 * 60;
const QString CatalogCacheFolder = QStringLiteral( "catalog-cache" );
// Per plugin, in the plugin settings: the version Skip this version hid, the newest version
// announced in a notice, and the version whose automatic update failed (reported once).
const QString SkippedKey = QStringLiteral( "skippedUpdate" );
const QString AnnouncedKey = QStringLiteral( "announcedUpdate" );
const QString FailedKey = QStringLiteral( "autoUpdateFailed" );

bool pendingState( const QString& state )
{
    return state == QLatin1String( "pending" ) || state == QLatin1String( "running" )
           || state == QLatin1String( "cancelling" );
}

// A new major version is never installed automatically; unreadable versions count as one.
bool majorChange( const QString& installed, const QString& available )
{
    const auto from = SemanticVersion::parse( installed );
    const auto to = SemanticVersion::parse( available );
    return !from || !to || from->major != to->major;
}

// `available` is at or below the skipped version.
bool skippedVersion( const QString& available, const QString& skipped )
{
    return !skipped.isEmpty() && compareSemanticVersions( available, skipped ).value_or( 1 ) <= 0;
}
} // namespace

QList<QJsonObject> PluginManager::newerFromSource( const QString& id, const Entry& entry,
                                                  std::optional<RepositoryRecord>* source ) const
{
    const auto info = sourceInfo( id, entry );
    if ( info.value( "type" ) != QLatin1String( "repository" ) ) {
        return {};
    }
    // Updates come only from the install source, and not while it is disabled or removed.
    const auto record = repositories_.find( info.value( "repository" ).toString() );
    if ( !record || !record->enabled ) {
        return {};
    }
    const auto cached = catalogs_.find( record->id );
    if ( cached == catalogs_.end() ) {
        return {};
    }
    const auto kind = packageKind( entry.manifest.json );
    QList<QJsonObject> candidates;
    for ( const auto& value : cached->second ) {
        const auto candidate = value.toObject();
        if ( candidate.value( "id" ).toString() == id
             && candidate.value( "kind" ).toString() == kind
             && compareSemanticVersions( candidate.value( "version" ).toString(),
                                         entry.manifest.version )
                        .value_or( 0 )
                    > 0 ) {
            candidates.append( candidate );
        }
    }
    if ( source ) {
        *source = record;
    }
    return candidates;
}

QJsonObject PluginManager::sameMajorUpdate( const QString& id ) const
{
    const auto installed = entries_.find( id );
    if ( installed == entries_.end() ) {
        return {};
    }
    QList<QJsonObject> sameMajor;
    for ( const auto& candidate : newerFromSource( id, *installed->second, nullptr ) ) {
        if ( !majorChange( installed->second->manifest.version,
                           candidate.value( "version" ).toString() ) ) {
            sameMajor.append( candidate );
        }
    }
    return newestEntry( sameMajor );
}

QVariantMap PluginManager::updateInfo( const QString& id, const Entry& entry ) const
{
    std::optional<RepositoryRecord> record;
    const auto newest = newestEntry( newerFromSource( id, entry, &record ) );
    if ( newest.isEmpty() || !record ) {
        return {};
    }
    const auto available = newest.value( "version" ).toString();
    return { { "version", available },
             { "entry_id", newest.value( "entry_id" ).toString() },
             { "repository", record->id },
             { "repositoryName", record->name },
             { "skipped", skippedVersion( available, settings_.value( configurationKey( id )
                                                                      + SkippedKey )
                                                          .toString() ) },
             { "majorChange", majorChange( entry.manifest.version, available ) } };
}

QVariantList PluginManager::availableUpdates() const
{
    QVariantList result;
    for ( const auto& [ id, entry ] : entries_ ) {
        auto update = updateInfo( id, *entry );
        if ( update.isEmpty() ) {
            continue;
        }
        update.insert( "id", id );
        update.insert( "name", entry->manifest.json.value( "name" ).toString( id ) );
        update.insert( "installed", entry->manifest.version );
        update.insert( "available", update.take( "version" ) );
        result.append( update );
    }
    return result;
}

bool PluginManager::updateCheckDue( const RepositoryRecord& record, const QDateTime& now ) const
{
    // A repository the silent check tried in the last day waits, whether or not it worked.
    const auto attempted = lastScheduledAttempt_.find( record.id );
    if ( attempted != lastScheduledAttempt_.end() ) {
        const auto since = attempted->second.secsTo( now );
        if ( since >= 0 && since < UpdateCheckIntervalSecs ) {
            return false;
        }
    }
    if ( !catalogs_.count( record.id ) ) {
        return true;
    }
    const auto last = QDateTime::fromString( record.lastCheck, Qt::ISODate );
    if ( !last.isValid() ) {
        return true;
    }
    const auto age = last.secsTo( now );
    return age >= UpdateCheckIntervalSecs || age < -ClockSkewSecs;
}

bool PluginManager::updateCheckRunning() const
{
    if ( !updateRun_ || updateRun_->finished ) {
        return false;
    }
    const auto running = jobs_.find( updateRun_->jobId );
    return running != jobs_.end() && pendingState( running->second.state );
}

Result PluginManager::runScheduledUpdateCheck()
{
    if ( shuttingDown_ ) {
        return { "CILogg is shutting down.", {} };
    }
    if ( updateCheckRunning() ) {
        return { {}, { { "jobId", updateRun_->jobId }, { "running", true } } };
    }
    const auto now = QDateTime::currentDateTimeUtc();
    QStringList due;
    QStringList notDue;
    for ( const auto& record : repositories_.list() ) {
        if ( record.enabled ) {
            ( updateCheckDue( record, now ) ? due : notDue ).append( record.id );
        }
    }
    LOG_INFO << "Plugin update check due=" << due.size() << " not_due=" << notDue.size();
    if ( due.isEmpty() ) {
        // Nothing to fetch: the saved catalogs may still hold an update that is not announced
        // or installed yet (for example one dropped when CILogg exited before its plugin stopped).
        processUpdates( {} );
        return { {}, { { "jobId", QString{} }, { "due", QStringList{} }, { "notDue", notDue } } };
    }
    for ( const auto& id : due ) {
        lastScheduledAttempt_[ id ] = now;
    }
    // Silent: no sign-in window and no `git credential reject` (design 3.5, 3.6).
    auto started = startUpdateCheck( due, notDue, {}, false );
    if ( !started.ok() ) {
        for ( const auto& id : due ) {
            lastScheduledAttempt_.erase( id ); // not tried: due again at the next run
        }
        return started;
    }
    started.payload.insert( "due", due );
    started.payload.insert( "notDue", notDue );
    return started;
}

Result PluginManager::checkUpdates( const QVariantMap& options, const QString& origin )
{
    for ( auto it = options.cbegin(); it != options.cend(); ++it ) {
        if ( it.key() != "repository" ) {
            return { QStringLiteral( "Unknown check_updates option: %1." )
                         .arg( it.key().left( 64 ) ),
                     {} };
        }
    }
    QStringList ids;
    QString scope;
    if ( options.contains( "repository" ) ) {
        scope = options.value( "repository" ).toString();
        const auto record = repositories_.find( scope );
        if ( !record ) {
            return { "Repository not found.", {} };
        }
        if ( !record->enabled ) {
            return { QStringLiteral( "The repository \"%1\" is disabled." ).arg( record->name ),
                     {} };
        }
        ids.append( scope );
    }
    else {
        for ( const auto& record : repositories_.list() ) {
            if ( record.enabled ) {
                ids.append( record.id );
            }
        }
    }
    if ( updateCheckRunning() ) {
        return { QStringLiteral( "An update check is already running (job %1)." )
                     .arg( updateRun_->jobId ),
                 {} };
    }
    // A check the user starts fetches every repository it names, whenever it was last checked.
    return startUpdateCheck( ids, {}, scope, origin == RequestOriginGui );
}

Result PluginManager::startUpdateCheck( const QStringList& ids, const QStringList& notDue,
                                        const QString& scope, bool interactive )
{
    const auto jobId = addJob( {}, "check_updates", {}, true );
    if ( jobId.isEmpty() ) {
        return { "Too many jobs.", {} };
    }
    auto run = std::make_shared<CatalogRun>();
    run->jobId = jobId;
    run->pending = ids;
    run->requested = ids;
    run->interactive = interactive;
    run->updateCheck = true;
    run->scope = scope;
    run->notDue = notDue;
    updateRun_ = run;
    LOG_DEBUG << "Plugin update check job=" << jobId.toStdString()
              << " repositories=" << ids.size();
    fetchNextCatalogs( run );
    return job( jobId );
}

void PluginManager::finishUpdateCheck( const std::shared_ptr<CatalogRun>& run,
                                       const QVariantList& summaries, const QString& error )
{
    QStringList fetched;
    for ( const auto& [ id, read ] : run->results ) {
        if ( read.error.isEmpty() ) {
            fetched.append( id );
        }
    }
    const auto automatic = processUpdates( fetched );
    QVariantList updates;
    for ( const auto& value : availableUpdates() ) {
        auto update = value.toMap();
        if ( run->scope.isEmpty() || update.value( "repository" ) == run->scope ) {
            update.insert( "automatic", automatic.contains( update.value( "id" ).toString() ) );
            updates.append( update );
        }
    }
    finishJob( run->jobId, error,
               { { "updates", updates },
                 { "repositories", summaries },
                 { "checked", run->requested },
                 { "notDue", run->notDue } } );
}

QStringList PluginManager::processUpdates( const QStringList& fetched )
{
    QVariantList found;
    QStringList started;
    bool announced = false;
    for ( const auto& value : availableUpdates() ) {
        const auto update = value.toMap();
        const auto id = update.value( "id" ).toString();
        const auto available = update.value( "available" ).toString();
        const auto repository = update.value( "repository" ).toString();
        const auto record = repositories_.find( repository );
        // Auto-update installs the newest version within the installed major version; a new
        // major version is announced and waits for a click (design 3.7). Skip this version
        // applies to each on its own: skipping 2.0.0 does not stop 1.5.0.
        const auto automatic
            = record && record->autoUpdate ? sameMajorUpdate( id ) : QJsonObject{};
        const auto target = automatic.value( "version" ).toString();
        const auto skipped = settings_.value( configurationKey( id ) + SkippedKey ).toString();
        // A skip covers older versions of its own major version only.
        const bool targetSkipped
            = skippedVersion( target, skipped ) && !majorChange( target, skipped );
        if ( !automatic.isEmpty() && !targetSkipped ) {
            // A version tried in this session is tried again only after a new fetch.
            const auto tried = autoUpdateTried_.find( id );
            const bool retry = tried == autoUpdateTried_.end() || tried->second != target
                               || fetched.contains( repository );
            if ( retry && !managementRunning( id ) ) {
                autoUpdateTried_[ id ] = target;
                auto request = update;
                request.insert( "entry_id", automatic.value( "entry_id" ).toString() );
                request.insert( "available", target );
                const auto error = startAutoUpdate( request );
                // A download that failed at once has already been reported.
                if ( error.isEmpty() && managementRunning( id ) ) {
                    started.append( id );
                }
                else if ( !error.isEmpty() ) {
                    reportAutoUpdateFailure( { { "id", id },
                                               { "name", update.value( "name" ) },
                                               { "from", update.value( "installed" ) },
                                               { "to", target },
                                               { "repository", repository },
                                               { "repositoryName",
                                                 update.value( "repositoryName" ) } },
                                             error );
                }
            }
            if ( !update.value( "majorChange" ).toBool() ) {
                continue; // the automatic update brings the newest version: no notice
            }
        }
        if ( update.value( "skipped" ).toBool() ) {
            continue; // Skip this version: no notice
        }
        // One notice per new version.
        const auto key = configurationKey( id ) + AnnouncedKey;
        const auto previous = settings_.value( key ).toString();
        if ( !previous.isEmpty()
             && compareSemanticVersions( available, previous ).value_or( 1 ) <= 0 ) {
            continue;
        }
        settings_.setValue( key, available );
        announced = true;
        found.append( update );
    }
    if ( announced && !persistSettings().isEmpty() ) {
        LOG_WARNING << "Announced plugin updates could not be saved in the plugin settings.";
    }
    if ( !found.isEmpty() ) {
        LOG_INFO << "Plugin updates found count=" << found.size();
        Q_EMIT updatesFound( found );
    }
    return started;
}

QString PluginManager::startAutoUpdate( const QVariantMap& update )
{
    const auto id = update.value( "id" ).toString();
    QJsonObject package;
    const auto selection = selectPackage(
        "update", id, { { "catalog_entry_id", update.value( "entry_id" ) } }, &package );
    if ( !selection.ok() ) {
        return selection.error;
    }
    const auto jobId = addJob( id, "update",
                               { { "repository", package.value( "repository" ).toString() },
                                 { "version", package.value( "version" ).toString() },
                                 { "entryId", package.value( "entry_id" ).toString() },
                                 { "installed", update.value( "installed" ) },
                                 { "automatic", true } },
                               true );
    if ( jobId.isEmpty() ) {
        return QStringLiteral( "Too many jobs." );
    }
    LOG_INFO << "Plugin automatic update id=" << id.toStdString()
             << " from=" << update.value( "installed" ).toString().toStdString()
             << " to=" << package.value( "version" ).toString().toStdString()
             << " repository=" << package.value( "repository" ).toString().toStdString();
    // Never interactive: a credential manager is asked silently and never rejected.
    fetchPackage( package, jobId, false );
    return {};
}

void PluginManager::autoUpdateFinished( const Job& finished )
{
    if ( finished.state == QLatin1String( "cancelled" ) ) {
        // A cancelled update that was waiting for its plugin leaves no staged package behind.
        const auto waiting = waitingCommits_.find( finished.pluginId );
        if ( waiting != waitingCommits_.end() && waiting->second.jobId == finished.id ) {
            discardWaitingCommit( finished.pluginId );
        }
        return;
    }
    const auto id = finished.pluginId;
    const auto to = finished.context.value( "version" ).toString();
    const auto repository = finished.context.value( "repository" ).toString();
    const auto record = repositories_.find( repository );
    const auto installed = entries_.find( id );
    QVariantMap update{ { "id", id },
                        { "name", installed != entries_.end()
                                      ? installed->second->manifest.json.value( "name" ).toString(
                                            id )
                                      : id },
                        { "from", finished.context.value( "installed" ) },
                        { "to", to },
                        { "repository", repository },
                        { "repositoryName", record ? record->name : QString{} } };
    if ( finished.state != QLatin1String( "completed" ) ) {
        reportAutoUpdateFailure( update, finished.error );
        return;
    }
    settings_.remove( configurationKey( id ) + FailedKey );
    // Installed: no "new version" notice for it either. The announced version only goes up,
    // so a new major version announced earlier is not announced again.
    const auto announcedKey = configurationKey( id ) + AnnouncedKey;
    const auto announced = settings_.value( announcedKey ).toString();
    if ( announced.isEmpty() || compareSemanticVersions( to, announced ).value_or( 1 ) > 0 ) {
        settings_.setValue( announcedKey, to );
    }
    if ( !persistSettings().isEmpty() ) {
        LOG_WARNING << "Plugin automatic update state could not be saved id=" << id.toStdString();
    }
    LOG_INFO << "Plugin automatically updated id=" << id.toStdString() << " to=" << to.toStdString();
    Q_EMIT autoUpdated( update );
}

void PluginManager::reportAutoUpdateFailure( QVariantMap update, const QString& error )
{
    const auto id = update.value( "id" ).toString();
    const auto to = update.value( "to" ).toString();
    // The error text is not logged: a download error can name an address.
    LOG_WARNING << "Plugin automatic update failed id=" << id.toStdString()
                << " version=" << to.toStdString();
    const auto key = configurationKey( id ) + FailedKey;
    if ( settings_.value( key ).toString() == to ) {
        return; // reported once; tried again at the next check
    }
    settings_.setValue( key, to );
    if ( !persistSettings().isEmpty() ) {
        LOG_WARNING << "Plugin automatic update state could not be saved id=" << id.toStdString();
    }
    update.insert( "error", error );
    Q_EMIT autoUpdateFailed( update );
}

Result PluginManager::skipUpdate( const QString& id, const QVariantMap& options )
{
    for ( auto it = options.cbegin(); it != options.cend(); ++it ) {
        if ( it.key() != "version" ) {
            return { QStringLiteral( "Unknown skip_update option: %1." ).arg( it.key().left( 64 ) ),
                     {} };
        }
    }
    const auto entry = entries_.find( id );
    if ( id.isEmpty() || entry == entries_.end() ) {
        return { "Plugin not found.", {} };
    }
    const auto version = options.value( "version" ).toString().trimmed();
    if ( !isSemanticVersion( version ) ) {
        return { "version must be the version to skip (Semantic Versioning).", {} };
    }
    settings_.setValue( configurationKey( id ) + SkippedKey, version );
    const auto error = persistSettings();
    if ( !error.isEmpty() ) {
        return { error, {} };
    }
    LOG_INFO << "Plugin update skipped id=" << id.toStdString()
             << " version=" << version.toStdString();
    Q_EMIT changed();
    const auto update = updateInfo( id, *entry->second );
    return { {},
             { { "plugin", id },
               { "skippedVersion", version },
               { "update", update.isEmpty() ? QVariant() : QVariant( update ) } } };
}

void PluginManager::cancelAutoUpdate( const QString& id )
{
    QList<QPair<QString, QString>> automatic; // job ID, plugin ID
    for ( const auto& [ jobId, j ] : jobs_ ) {
        if ( ( id.isEmpty() || j.pluginId == id ) && j.context.value( "automatic" ).toBool()
             && pendingState( j.state ) ) {
            automatic.append( { jobId, j.pluginId } );
        }
    }
    for ( const auto& [ jobId, pluginId ] : automatic ) {
        LOG_INFO << "Plugin automatic update replaced by a user operation id="
                 << pluginId.toStdString();
        cancelJob( jobId, false );
        discardWaitingCommit( pluginId );
        // Tried again at the next check if the user's operation does not install it.
        autoUpdateTried_.erase( pluginId );
    }
}

void PluginManager::updateCheckCancelled( const QString& jobId )
{
    if ( !updateRun_ || updateRun_->jobId != jobId ) {
        return;
    }
    // A cancelled check did not try its repositories: the next scheduled run may.
    for ( const auto& id : updateRun_->requested ) {
        lastScheduledAttempt_.erase( id );
    }
    updateRun_.reset();
}

void PluginManager::forgetUpdates( const QString& id )
{
    for ( const auto& key : { SkippedKey, AnnouncedKey, FailedKey } ) {
        settings_.remove( configurationKey( id ) + key );
    }
    autoUpdateTried_.erase( id );
}

void PluginManager::clearAutoUpdateFailure( const QString& id )
{
    settings_.remove( configurationKey( id ) + FailedKey );
}

void PluginManager::discardWaitingCommit( const QString& id )
{
    const auto waiting = waitingCommits_.find( id );
    if ( waiting != waitingCommits_.end() ) {
        QDir( waiting->second.staged.directory ).removeRecursively();
        waitingCommits_.erase( waiting );
    }
}

QString PluginManager::catalogCachePath( const RepositoryRecord& record ) const
{
    // The address is part of the name, so a file never serves a repository with another address.
    const auto address = QString::fromLatin1(
        QCryptographicHash::hash( PluginRepositories::identity( record ).toUtf8(),
                                  QCryptographicHash::Sha256 )
            .toHex()
            .left( 16 ) );
    return QDir( QDir( dataRoot_ ).filePath( CatalogCacheFolder ) )
        .filePath( record.id + '-' + address + QStringLiteral( ".json" ) );
}

void PluginManager::storeCatalog( const QString& repositoryId, const QByteArray& bytes )
{
    const auto record = repositories_.find( repositoryId );
    if ( !record ) {
        return; // removed while it was being fetched
    }
    // The catalog as published; it never holds a credential.
    const auto path = catalogCachePath( *record );
    QSaveFile file( path );
    if ( !QDir().mkpath( QFileInfo( path ).absolutePath() ) || !file.open( QIODevice::WriteOnly )
         || file.write( bytes ) != bytes.size() || !file.commit() ) {
        LOG_WARNING << "Plugin catalog could not be saved repository="
                    << repositoryId.toStdString();
    }
}

void PluginManager::loadCatalogCache()
{
    catalogs_.clear();
    for ( const auto& record : repositories_.list() ) {
        QFile file( catalogCachePath( record ) );
        if ( !file.exists() ) {
            continue;
        }
        if ( file.size() > MaxMessageBytes || !file.open( QIODevice::ReadOnly ) ) {
            LOG_WARNING << "Saved plugin catalog unreadable repository=" << record.id.toStdString();
            continue;
        }
        // Read again for this host: entries this version cannot use stay hidden.
        auto read = readCatalog( file.readAll(), record.id, hostVersion_ );
        if ( read.error.isEmpty() ) {
            catalogs_[ record.id ] = std::move( read.entries );
        }
        else {
            LOG_WARNING << "Saved plugin catalog rejected repository=" << record.id.toStdString();
        }
    }
    LOG_DEBUG << "Saved plugin catalogs loaded count=" << catalogs_.size();
}

void PluginManager::removeCatalogCache( const RepositoryRecord& record )
{
    QFile::remove( catalogCachePath( record ) );
}
} // namespace cilogg::plugins
