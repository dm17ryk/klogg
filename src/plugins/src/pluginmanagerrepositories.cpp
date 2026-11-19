// Plugin repositories, the merged catalog, package downloads and Azure DevOps sign-in of
// PluginManager (docs/plugin-repositories.md sections 3.1-3.6 and 3.8, tasks E15a and E15b).
#include "log.h"
#include "pluginmanager.h"
#include "semanticversion.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>

namespace cilogg::plugins {
namespace {
constexpr int MaxParallelCatalogs = 4;
constexpr int DownloadTimeoutMs = 30000;
constexpr int MaxRedirects = 5;
constexpr int MaxTokenLength = 1024;
constexpr qint64 MaxPackageDownloadBytes = 128LL * MaxMessageBytes;
const QString SignInNeededText = QStringLiteral( "Sign-in needed." );

bool isHttps( const QUrl& url )
{
    return url.isValid() && url.scheme() == QLatin1String( "https" ) && !url.host().isEmpty();
}

// A catalog address that answers with a web page (a sign-in or error page) instead of JSON.
bool looksLikeWebPage( const QByteArray& bytes )
{
    const auto start = bytes.trimmed().left( 64 ).toLower();
    return start.startsWith( '<' );
}

// The newest entry by Semantic Versioning; the first one listed wins a tie.
QJsonObject newest( const QList<QJsonObject>& entries )
{
    QJsonObject best;
    for ( const auto& entry : entries ) {
        if ( best.isEmpty()
             || compareSemanticVersions( entry.value( "version" ).toString(),
                                         best.value( "version" ).toString() )
                        .value_or( 0 )
                    > 0 ) {
            best = entry;
        }
    }
    return best;
}
} // namespace

QString PluginManager::catalogUrl() const
{
    return settings_.value( "catalogUrl", PluginRepositories::DefaultCatalogUrl ).toString();
}

QString PluginManager::persistSettings()
{
    settings_.sync();
    return settings_.status() == QSettings::NoError
               ? QString{}
               : QStringLiteral( "Cannot persist plugin settings." );
}

void PluginManager::setHostVersion( const QString& version )
{
    hostVersion_ = version;
}

QVariantList PluginManager::repositories() const
{
    QVariantList result;
    int position = 0;
    for ( const auto& record : repositories_.list() ) {
        result.append( record.toVariant( ++position ) );
    }
    return result;
}

QString PluginManager::installedSource( const QString& id ) const
{
    const auto entry = entries_.find( id );
    if ( entry == entries_.end() ) {
        return {};
    }
    const auto source = sourceInfo( id, *entry->second );
    return source.value( "type" ) == "repository" ? source.value( "repository" ).toString()
                                                  : QString{};
}

QVariantMap PluginManager::sourceInfo( const QString& id, const Entry& entry ) const
{
    const auto parent = QDir::cleanPath( QFileInfo( entry.manifest.directory ).absolutePath() );
    const auto userRoot = QDir::cleanPath(
        QFileInfo( QDir( dataRoot_ ).filePath( "plugins" ) ).absoluteFilePath() );
#ifdef Q_OS_WIN
    const auto sensitivity = Qt::CaseInsensitive;
#else
    const auto sensitivity = Qt::CaseSensitive;
#endif
    if ( parent.compare( userRoot, sensitivity ) != 0 ) {
        return { { "type", "shipped" } };
    }
    const auto source = repositories_.source( id );
    if ( source.repository.isEmpty() ) {
        return { { "type", "local" } };
    }
    const auto record = repositories_.find( source.repository );
    return { { "type", "repository" },
             { "repository", source.repository },
             { "repositoryName", record ? record->name : QString{} },
             { "version", source.version },
             { "available", record && record->enabled } };
}

QVariantList PluginManager::annotated( const QJsonArray& entries ) const
{
    QVariantList result;
    for ( const auto& value : entries ) {
        auto entry = value.toObject().toVariantMap();
        const auto id = entry.value( "id" ).toString();
        const auto record = repositories_.find( entry.value( "repository" ).toString() );
        entry[ "repositoryName" ] = record ? record->name : QString{};
        const auto installed = entries_.find( id );
        if ( installed != entries_.end() ) {
            const auto source = sourceInfo( id, *installed->second );
            entry[ "installed" ] = installed->second->manifest.version;
            entry[ "installedSource" ] = source.value( "type" ) == "repository"
                                             ? source.value( "repository" ).toString()
                                             : source.value( "type" ).toString();
        }
        else {
            entry[ "installed" ] = QString{};
            entry[ "installedSource" ] = QString{};
        }
        result.append( entry );
    }
    return result;
}

QVariantList PluginManager::catalogEntries() const
{
    QJsonArray merged;
    for ( const auto& record : repositories_.list() ) {
        // A disabled repository offers nothing, also from its last fetch.
        if ( !record.enabled ) {
            continue;
        }
        const auto cached = catalogs_.find( record.id );
        if ( cached != catalogs_.end() ) {
            for ( const auto& entry : cached->second ) {
                merged.append( entry );
            }
        }
    }
    return annotated( merged );
}

struct PluginManager::DownloadState {
    qint64 limit = 0;
    QString jobId;
    DownloadHandler handler;
    DownloadAuth auth;
    bool manualRedirects = false; // set once for the whole chain when a credential was given
    int hops = 0;
};

void PluginManager::download( const QString& url, qint64 limit, const QString& jobId,
                              DownloadHandler handler, DownloadAuth auth )
{
    const QUrl target( url );
    if ( !isHttps( target ) ) {
        // Asynchronous like every other outcome, so callers see one completion order.
        QTimer::singleShot( 0, this, [ this, jobId, handler ] {
            const auto it = jobs_.find( jobId );
            if ( it != jobs_.end() && !it->second.cancelRequested && !shuttingDown_ ) {
                handler( {}, { QStringLiteral( "Download requires HTTPS." ) } );
            }
        } );
        return;
    }
    auto state = std::make_shared<DownloadState>();
    state->limit = limit;
    state->jobId = jobId;
    state->handler = std::move( handler );
    state->auth = std::move( auth );
    state->manualRedirects = static_cast<bool>( state->auth.credential );
    downloadHop( target, state );
}

void PluginManager::downloadHop( const QUrl& target, const std::shared_ptr<DownloadState>& state )
{
    QNetworkRequest request( target );
    // Qt copies the request, hand-set headers included, when it follows a redirect itself: a
    // request that can carry a credential follows redirects here instead (design 3.6).
    request.setAttribute( QNetworkRequest::RedirectPolicyAttribute,
                          state->manualRedirects ? QNetworkRequest::ManualRedirectPolicy
                                                 : QNetworkRequest::NoLessSafeRedirectPolicy );
    const bool credentialAttached
        = state->auth.credential && state->auth.scope && state->auth.scope->contains( target );
    if ( credentialAttached ) {
        request.setRawHeader( "Authorization", state->auth.credential->authorization() );
    }
    if ( state->auth.azureDevOps
         && target.host().compare( AzureDevOpsHost, Qt::CaseInsensitive ) == 0 ) {
        // A missing or expired credential answers 401 instead of redirecting to a sign-in page.
        request.setRawHeader( "X-TFS-FedAuthRedirect", "Suppress" );
        if ( isAzureDevOpsItemsUrl( target ) ) {
            request.setRawHeader( "Accept", "application/octet-stream" );
        }
    }
    auto* reply = networkAccess_->get( request );
    auto bytes = std::make_shared<QByteArray>();
    auto oversized = std::make_shared<bool>( false );
    auto timedOut = std::make_shared<bool>( false );
    const auto limit = state->limit;
    connect( reply, &QNetworkReply::readyRead, this, [ reply, bytes, oversized, limit ] {
        bytes->append( reply->read( limit + 1 - bytes->size() ) );
        if ( bytes->size() > limit ) {
            *oversized = true;
            reply->abort();
        }
    } );
    QTimer::singleShot( DownloadTimeoutMs, reply, [ reply, timedOut ] {
        if ( !reply->isFinished() ) {
            *timedOut = true;
            reply->abort();
        }
    } );
    connect(
        reply, &QNetworkReply::finished, this,
        [ this, reply, bytes, oversized, timedOut, target, state, credentialAttached ] {
            reply->deleteLater();
            const auto it = jobs_.find( state->jobId );
            if ( it == jobs_.end() || it->second.cancelRequested || shuttingDown_ ) {
                return;
            }
            const auto& handler = state->handler;
            if ( *oversized ) {
                handler( {}, { QStringLiteral( "Download exceeds size limit." ) } );
                return;
            }
            if ( *timedOut ) {
                handler( {}, { QStringLiteral( "No answer within 30 seconds." ) } );
                return;
            }
            const auto status = reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt();
            const auto location = reply->attribute( QNetworkRequest::RedirectionTargetAttribute ).toUrl();
            if ( state->manualRedirects && status >= 300 && status < 400 && status != 304
                 && !location.isValid() ) {
                handler( {}, { QStringLiteral( "Unexpected redirect without a target." ) } );
                return;
            }
            if ( state->manualRedirects && status >= 300 && status < 400 && location.isValid() ) {
                const auto next = target.resolved( location );
                if ( ++state->hops > MaxRedirects ) {
                    handler( {}, { QStringLiteral( "Too many redirects." ) } );
                    return;
                }
                if ( !isHttps( next ) ) {
                    handler( {}, { QStringLiteral( "Download requires HTTPS." ) } );
                    return;
                }
                if ( state->auth.credential
                     && !( state->auth.scope && state->auth.scope->contains( next ) ) ) {
                    // Continues without the credential once a hop leaves the scope.
                    state->auth.credential.reset();
                }
                downloadHop( next, state );
                return;
            }
            if ( state->auth.azureDevOps && status == 401 ) {
                handler( {}, { SignInNeededText, true, credentialAttached } );
                return;
            }
            if ( reply->error() != QNetworkReply::NoError ) {
                // Redirect addresses can carry tokens: the error text never names them.
                handler(
                    {},
                    { status == 404 ? QStringLiteral( "Not found (HTTP 404)." )
                      : status >= 400
                          ? QStringLiteral( "HTTP %1 %2" )
                                .arg( status )
                                .arg( reply->attribute( QNetworkRequest::HttpReasonPhraseAttribute )
                                          .toString()
                                          .left( 128 ) )
                                .trimmed()
                          : reply->errorString() } );
                return;
            }
            // An HTML page or 203 from Azure DevOps is its sign-in page, never a catalog or a
            // package (design 3.5).
            if ( state->auth.azureDevOps
                 && ( status == 203
                      || reply->header( QNetworkRequest::ContentTypeHeader )
                             .toString()
                             .contains( QLatin1String( "text/html" ), Qt::CaseInsensitive )
                      || looksLikeWebPage( *bytes ) ) ) {
                handler( {}, { SignInNeededText, true, false } );
                return;
            }
            handler( *bytes, {} );
        } );
}

QString PluginManager::gitExecutable() const
{
    // `gitExecutable` in the plugin settings.ini is the trusted Git path (design 3.5).
    return GitCredentialHelper::resolveGit( settings_.value( "gitExecutable" ).toString() );
}

void PluginManager::acquireCredential( const RepositoryRecord& record,
                                       const AzureDevOpsLocation& location,
                                       const SignInRequest& request, CredentialDone done )
{
    const auto method = request.method.isEmpty() ? record.credentialSource : request.method;
    if ( method == CredentialSourcePersonalAccessToken ) {
        if ( !request.token.isEmpty() ) {
            done( std::make_shared<const Credential>(
                      Credential::Source::PersonalAccessToken,
                      authorizationForPersonalAccessToken( request.token ) ),
                  {} );
            return;
        }
        // The store answers later; the manager may be gone by then.
        QPointer<PluginManager> self( this );
        patStore_->read( record.id, [ self, done ]( std::optional<QByteArray> token,
                                                    PatStore::Status status ) {
            if ( !self ) {
                return;
            }
            if ( !token || token->isEmpty() ) {
                done( nullptr, status.error.isEmpty()
                                   ? QStringLiteral( "Sign in with a personal access token." )
                                   : status.error );
                return;
            }
            auto credential = std::make_shared<const Credential>(
                Credential::Source::PersonalAccessToken, authorizationForPersonalAccessToken( *token ) );
            wipeSecret( *token );
            done( credential, {} );
        } );
        return;
    }
    GitCredentialHelper::fill(
        gitExecutable(), location, request.interactive, this,
        [ done ]( CredentialPtr credential, GitCredentialHelper::Outcome outcome ) {
            done( credential,
                  credential ? QString{} : GitCredentialHelper::outcomeText( outcome ) );
        } );
}

void PluginManager::fetchRepositoryCatalog( const RepositoryRecord& record, const QString& jobId,
                                            const SignInRequest& request, DownloadHandler done,
                                            bool retried )
{
    if ( !record.isAzureDevOps() ) {
        download( record.url, MaxMessageBytes, jobId, std::move( done ) );
        return;
    }
    const auto location = record.azureDevOps();
    if ( !location ) {
        QTimer::singleShot( 0, this, [ done ] {
            done( {}, { QStringLiteral( "The repository address is not valid." ) } );
        } );
        return;
    }
    const auto method = request.method.isEmpty() ? record.credentialSource : request.method;
    acquireCredential(
        record, *location, request,
        [ this, record, location, jobId, request, done, retried,
          method ]( CredentialPtr credential, const QString& error ) {
            const auto it = jobs_.find( jobId );
            if ( it == jobs_.end() || it->second.cancelRequested || shuttingDown_ ) {
                return;
            }
            if ( !credential ) {
                done( {}, { SignInNeededText + ' ' + error, true, false } );
                return;
            }
            DownloadAuth auth{ credential, CredentialScope::forAzureDevOps( *location ), true };
            download(
                location->itemsUrl().toString( QUrl::FullyEncoded ), MaxMessageBytes, jobId,
                [ this, record, jobId, request, done, retried, method,
                  credential ]( const QByteArray& bytes, const DownloadOutcome& outcome ) {
                    const auto git = gitExecutable();
                    const bool manager = method == CredentialSourceGitCredentialManager;
                    if ( outcome.error.isEmpty() ) {
                        if ( manager ) {
                            GitCredentialHelper::approve( git, *credential, this );
                        }
                        done( bytes, outcome );
                        return;
                    }
                    // `git credential reject` deletes the credential Git itself uses: only after
                    // an in-scope 401 in an operation started in the GUI (design 3.5).
                    if ( manager && outcome.credentialRefused && request.interactive && !retried ) {
                        GitCredentialHelper::reject(
                            git, *credential, this,
                            [ this, record, jobId, request, done ]( GitCredentialHelper::Outcome ) {
                                fetchRepositoryCatalog( record, jobId, request, done, true );
                            } );
                        return;
                    }
                    done( bytes, outcome );
                },
                std::move( auth ) );
        } );
}

void PluginManager::fetchPackage( const QJsonObject& entry, const QString& jobId, bool interactive )
{
    const auto url = entry.value( "url" ).toString();
    const auto record = repositories_.find( entry.value( "repository" ).toString() );
    const auto location = record ? record->azureDevOps() : std::nullopt;
    if ( !location ) {
        downloadPackage( entry, jobId, url, {} );
        return;
    }
    // A package of an Azure DevOps catalog: a file link becomes the Items API address, and the
    // repository's credential goes only to addresses inside its scope (design 3.5, 3.6).
    const auto target = azureDevOpsDownloadUrl( QUrl( url ) );
    const auto scope = CredentialScope::forAzureDevOps( *location );
    if ( !scope.contains( target ) ) {
        downloadPackage( entry, jobId, target.toString( QUrl::FullyEncoded ),
                         DownloadAuth{ nullptr, scope, true } );
        return;
    }
    acquireCredential( *record, *location, SignInRequest{ interactive, {}, {} },
                       [ this, entry, jobId, target, scope ]( CredentialPtr credential,
                                                              const QString& error ) {
                           const auto it = jobs_.find( jobId );
                           if ( it == jobs_.end() || it->second.cancelRequested || shuttingDown_ ) {
                               return;
                           }
                           if ( !credential ) {
                               finishJob( jobId, SignInNeededText + ' ' + error );
                               return;
                           }
                           downloadPackage( entry, jobId, target.toString( QUrl::FullyEncoded ),
                                            DownloadAuth{ credential, scope, true } );
                       } );
}

void PluginManager::downloadPackage( const QJsonObject& entry, const QString& jobId,
                                     const QString& url, DownloadAuth auth )
{
    const auto id = entry.value( "id" ).toString();
    const auto version = entry.value( "version" ).toString();
    const auto kind = entry.value( "kind" ).toString();
    const auto sha256 = entry.value( "sha256" ).toString();
    // The record keeps the catalog's address, which never carries a credential.
    const InstallSource source{ entry.value( "repository" ).toString(), version,
                                entry.value( "url" ).toString() };
    const auto credential = auth.credential;
    download(
        url, MaxPackageDownloadBytes, jobId,
        [ this, jobId, id, version, kind, sha256, source,
          credential ]( const QByteArray& bytes, const DownloadOutcome& outcome ) {
            if ( !outcome.error.isEmpty() ) {
                finishJob( jobId, outcome.error );
                return;
            }
            if ( credential && credential->source() == Credential::Source::GitCredentialManager ) {
                GitCredentialHelper::approve( gitExecutable(), *credential, this );
            }
            if ( QString::fromLatin1(
                     QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() )
                     .compare( sha256, Qt::CaseInsensitive )
                 != 0 ) {
                finishJob( jobId, "Plugin package SHA-256 mismatch." );
                return;
            }
            auto package = std::make_shared<QTemporaryFile>(
                QDir( dataRoot_ ).filePath( ".download-XXXXXX.zip" ) );
            if ( !package->open() || package->write( bytes ) != bytes.size()
                 || !package->flush() ) {
                finishJob( jobId, "Cannot stage plugin download." );
                return;
            }
            const auto path = package->fileName();
            package->close();
            auto* watcher = new QFutureWatcher<StagedPlugin>( this );
            const auto root = QDir( dataRoot_ ).filePath( "plugins" );
            connect(
                watcher, &QFutureWatcher<StagedPlugin>::finished, this,
                [ this, watcher, package, jobId, id, version, kind, source ] {
                    const auto staged = watcher->result();
                    watcher->deleteLater();
                    QString problem = staged.error;
                    if ( problem.isEmpty()
                         && ( staged.manifest.id != id || staged.manifest.version != version ) ) {
                        problem = QStringLiteral( "Package identity differs from catalog." );
                    }
                    // An entry and its package agree on the kind, so an update can never
                    // turn a process plugin into a pack or the other way round.
                    if ( problem.isEmpty() && packageKind( staged.manifest.json ) != kind ) {
                        problem = QStringLiteral( "Package kind differs from catalog." );
                    }
                    if ( !problem.isEmpty() ) {
                        if ( !staged.directory.isEmpty() ) {
                            QDir( staged.directory ).removeRecursively();
                        }
                        finishJob( jobId, problem );
                        return;
                    }
                    commitWhenStopped( staged, jobId, source );
                } );
            watcher->setFuture( QtConcurrent::run(
                [ path, root, package ] { return PluginRepository::stage( path, root ); } ) );
        },
        std::move( auth ) );
}

Result PluginManager::manageRepositories( const QString& operation, const QVariantMap& options,
                                          const QString& origin )
{
    if ( operation == "repositories" ) {
        return { {}, { { "repositories", repositories() } } };
    }
    if ( operation == "repository_sign_in" ) {
        return signIn( options, origin );
    }
    if ( operation == "repository_sign_out" ) {
        return signOut( options );
    }
    if ( operation == "repository_add" ) {
        RepositoryRecord added;
        auto error = repositories_.add( options, &added );
        if ( error.isEmpty() ) {
            error = persistSettings();
        }
        if ( !error.isEmpty() ) {
            return { error, {} };
        }
        Q_EMIT changed();
        return { {},
                 { { "repository", added.toVariant( repositories_.position( added.id ) ) },
                   { "repositories", repositories() } } };
    }
    const auto id = options.value( "repository" ).toString();
    auto rest = options;
    rest.remove( "repository" );
    if ( id.isEmpty() ) {
        return { "Name the repository by its ID (repository).", {} };
    }
    QString error;
    QVariantMap payload;
    if ( operation == "repository_update" ) {
        RepositoryRecord updated;
        error = repositories_.update( id, rest, &updated );
        payload[ "repository" ] = updated.toVariant( repositories_.position( id ) );
    }
    else if ( operation == "repository_remove" ) {
        const auto record = repositories_.find( id );
        error = rest.isEmpty() ? repositories_.remove( id )
                               : QStringLiteral( "repository_remove takes only repository." );
        if ( error.isEmpty() ) {
            // Plugins installed from it keep running; their source becomes unavailable.
            catalogs_.erase( id );
            payload[ "removed" ] = id;
            // No token is left orphaned in the credential store (design 3.1). The ID is never
            // given again, so a later failure cannot leak it to another repository.
            if ( record && record->isAzureDevOps() ) {
                patStore_->remove( id, [ id ]( PatStore::Status status ) {
                    if ( !status.error.isEmpty() ) {
                        LOG_WARNING << "Plugin repository token of id=" << id.toStdString()
                                    << " could not be deleted";
                    }
                } );
            }
        }
    }
    else {
        error = repositories_.move( id, rest );
    }
    if ( error.isEmpty() ) {
        error = persistSettings();
    }
    if ( !error.isEmpty() ) {
        return { error, {} };
    }
    payload[ "repositories" ] = repositories();
    Q_EMIT changed();
    return { {}, payload };
}

Result PluginManager::signIn( const QVariantMap& options, const QString& origin )
{
    for ( auto it = options.cbegin(); it != options.cend(); ++it ) {
        if ( it.key() != "repository" && it.key() != "method" && it.key() != "token" ) {
            return { QStringLiteral( "Unknown repository_sign_in option: %1." )
                         .arg( it.key().left( 64 ) ),
                     {} };
        }
    }
    const auto id = options.value( "repository" ).toString();
    const auto record = repositories_.find( id );
    if ( !record ) {
        return { "Repository not found.", {} };
    }
    if ( !record->isAzureDevOps() ) {
        return { "repository_sign_in applies only to Azure DevOps repositories.", {} };
    }
    const auto method = options.value( "method", record->credentialSource ).toString();
    if ( method != CredentialSourceGitCredentialManager
         && method != CredentialSourcePersonalAccessToken ) {
        return { "method must be git-credential-manager or personal-access-token.", {} };
    }
    SignInRequest request{ origin == RequestOriginGui, method, {} };
    if ( options.contains( "token" ) ) {
        // A token never comes from a script, a scenario or an MCP client (design 3.6).
        if ( origin != RequestOriginGui && origin != RequestOriginCli ) {
            return { "A personal access token can be given only in the CILogg window or with the "
                     "CLI options --token-file and --token-stdin.",
                     {} };
        }
        if ( method != CredentialSourcePersonalAccessToken ) {
            return { "token applies only to method personal-access-token.", {} };
        }
        request.token = options.value( "token" ).toString().trimmed().toUtf8();
        if ( request.token.isEmpty() || request.token.size() > MaxTokenLength
             || request.token.contains( '\n' ) || request.token.contains( '\r' ) ) {
            return { "The personal access token is empty or not valid.", {} };
        }
    }
    const auto jobId
        = addJob( {}, "repository_sign_in", { { "repository", id }, { "method", method } }, true );
    if ( jobId.isEmpty() ) {
        return { "Too many jobs.", {} };
    }
    LOG_INFO << "Plugin repository sign-in id=" << id.toStdString()
             << " method=" << method.toStdString() << " interactive=" << request.interactive;
    // The test is a catalog fetch: a sign-in that works also refreshes the repository.
    fetchRepositoryCatalog(
        *record, jobId, request,
        [ this, id, jobId, method, token = request.token ]( const QByteArray& bytes,
                                                             const DownloadOutcome& outcome ) mutable {
            const auto now = QDateTime::currentDateTimeUtc();
            if ( outcome.signInNeeded ) {
                repositories_.recordCheck( id, SignInRequiredState, 0, 0, 0, outcome.error, now );
                persistSettings();
                Q_EMIT changed();
                wipeSecret( token );
                finishJob( jobId, {},
                           { { "repository", id },
                             { "signedIn", false },
                             { "state", SignInRequiredState },
                             { "error", outcome.error } } );
                return;
            }
            if ( !outcome.error.isEmpty() ) {
                repositories_.recordCheck( id, QStringLiteral( "failed" ), 0, 0, 0, outcome.error,
                                           now );
                persistSettings();
                Q_EMIT changed();
                wipeSecret( token );
                finishJob( jobId, outcome.error );
                return;
            }
            auto read = std::make_shared<CatalogRead>( readCatalog( bytes, id, hostVersion_ ) );
            const auto state = !read->error.isEmpty()    ? QStringLiteral( "failed" )
                               : read->entries.isEmpty() ? QStringLiteral( "empty" )
                                                         : QStringLiteral( "loaded" );
            // Records the sign-in only once it is kept: a token is stored first.
            const auto commit = [ this, id, jobId, method, read, state, now ]( bool sessionOnly ) {
                if ( !repositories_.find( id ) ) {
                    finishJob( jobId, QStringLiteral( "The repository was removed." ) );
                    return;
                }
                repositories_.recordCheck( id, state, int( read->entries.size() ),
                                           int( read->warnings.size() ), int( read->hidden.size() ),
                                           read->error, now );
                if ( read->error.isEmpty() ) {
                    catalogs_[ id ] = read->entries;
                }
                repositories_.setCredentialSource( id, method );
                persistSettings();
                Q_EMIT changed();
                QVariantMap result{ { "repository", id },
                                    { "signedIn", true },
                                    { "state", state },
                                    { "error", read->error },
                                    { "pluginCount", read->entries.size() } };
                if ( sessionOnly ) {
                    result[ "sessionOnly" ] = true;
                }
                finishJob( jobId, {}, result );
            };
            if ( token.isEmpty() ) {
                commit( false );
                return;
            }
            // A token is stored only after it worked, and never for a repository that was removed
            // while it was being tested: nothing would delete it (design 3.1).
            if ( !repositories_.find( id ) ) {
                wipeSecret( token );
                finishJob( jobId, QStringLiteral( "The repository was removed." ) );
                return;
            }
            QPointer<PluginManager> self( this );
            patStore_->write(
                id, token, [ self, id, jobId, commit, now ]( PatStore::Status status ) {
                    if ( !self ) {
                        return;
                    }
                    if ( !status.error.isEmpty() ) {
                        self->repositories_.recordCheck( id, SignInRequiredState, 0, 0, 0,
                                                         status.error, now );
                        self->persistSettings();
                        Q_EMIT self->changed();
                        self->finishJob( jobId, status.error );
                        return;
                    }
                    if ( !self->repositories_.find( id ) ) {
                        // Removed while the store was writing: delete the token again.
                        self->patStore_->remove( id, []( PatStore::Status ) {} );
                    }
                    commit( status.sessionOnly );
                } );
            wipeSecret( token );
        } );
    return job( jobId );
}

Result PluginManager::signOut( const QVariantMap& options )
{
    for ( auto it = options.cbegin(); it != options.cend(); ++it ) {
        if ( it.key() != "repository" ) {
            return { "repository_sign_out takes only repository.", {} };
        }
    }
    const auto id = options.value( "repository" ).toString();
    const auto record = repositories_.find( id );
    if ( !record ) {
        return { "Repository not found.", {} };
    }
    if ( !record->isAzureDevOps() ) {
        return { "repository_sign_out applies only to Azure DevOps repositories.", {} };
    }
    const auto jobId = addJob( {}, "repository_sign_out", { { "repository", id } }, true );
    if ( jobId.isEmpty() ) {
        return { "Too many jobs.", {} };
    }
    // Deletes the stored PAT; the Git credential manager, which Git also uses, is not touched.
    QPointer<PluginManager> self( this );
    const bool tokenSource = record->credentialSource == CredentialSourcePersonalAccessToken;
    patStore_->remove( id, [ self, id, jobId, tokenSource ]( PatStore::Status status ) {
        if ( !self ) {
            return;
        }
        if ( status.error.isEmpty() && tokenSource ) {
            self->repositories_.recordSignOut( id );
            self->persistSettings();
            Q_EMIT self->changed();
        }
        self->finishJob( jobId, status.error,
                         { { "repository", id }, { "signedOut", status.error.isEmpty() } } );
    } );
    return job( jobId );
}

Result PluginManager::startCatalog( const QVariantMap& options, const QString& origin )
{
    for ( auto it = options.cbegin(); it != options.cend(); ++it ) {
        if ( it.key() != "url" && it.key() != "repository" ) {
            return { QStringLiteral( "Unknown catalog option: %1." ).arg( it.key().left( 64 ) ),
                     {} };
        }
    }
    if ( options.contains( "url" ) && options.contains( "repository" ) ) {
        return { "Give either repository or url.", {} };
    }
    QStringList ids;
    if ( options.contains( "url" ) || options.contains( "repository" ) ) {
        auto id = options.value( "repository" ).toString();
        if ( options.contains( "url" ) ) {
            // The pre-E15 option: the address becomes a repository unless one already has it.
            const auto url = options.value( "url" ).toString().trimmed();
            const auto addressError = PluginRepositories::addressError( url );
            if ( !addressError.isEmpty() ) {
                return { addressError, {} };
            }
            id = repositories_.idForAddress( url );
            if ( id.isEmpty() ) {
                RepositoryRecord added;
                auto error = repositories_.add( { { "name", QUrl( url ).host() }, { "url", url } },
                                                &added );
                if ( error.isEmpty() ) {
                    error = persistSettings();
                }
                if ( !error.isEmpty() ) {
                    return { error, {} };
                }
                id = added.id;
                Q_EMIT changed();
            }
        }
        const auto record = repositories_.find( id );
        if ( !record ) {
            return { "Repository not found.", {} };
        }
        if ( !record->enabled ) {
            return { QStringLiteral( "The repository \"%1\" is disabled." ).arg( record->name ),
                     {} };
        }
        ids.append( id );
    }
    else {
        for ( const auto& record : repositories_.list() ) {
            if ( record.enabled ) {
                ids.append( record.id );
            }
        }
    }
    const auto jobId = addJob( {}, "catalog", {}, true );
    if ( jobId.isEmpty() ) {
        return { "Too many jobs.", {} };
    }
    // A newer catalog request supersedes the previous one.
    if ( !catalogJobId_.isEmpty() ) {
        cancel( catalogJobId_ );
    }
    catalogJobId_ = jobId;
    auto run = std::make_shared<CatalogRun>();
    run->jobId = jobId;
    run->pending = ids;
    run->interactive = origin == RequestOriginGui;
    catalogRun_ = run;
    LOG_DEBUG << "Plugin catalog job=" << jobId.toStdString() << " repositories=" << ids.size();
    fetchNextCatalogs( run );
    return job( jobId );
}

void PluginManager::fetchNextCatalogs( const std::shared_ptr<CatalogRun>& run )
{
    while ( run->active < MaxParallelCatalogs && !run->pending.isEmpty() ) {
        const auto id = run->pending.takeFirst();
        const auto record = repositories_.find( id );
        if ( !record ) {
            continue; // removed after the job started
        }
        ++run->active;
        fetchRepositoryCatalog(
            *record, run->jobId, SignInRequest{ run->interactive, {}, {} },
                  [ this, run, id ]( const QByteArray& bytes, const DownloadOutcome& outcome ) {
                      --run->active;
                      CatalogRead read;
                      if ( !outcome.error.isEmpty() ) {
                          read.error = outcome.error;
                          if ( outcome.signInNeeded ) {
                              run->signInNeeded.append( id );
                          }
                      }
                      else if ( looksLikeWebPage( bytes ) ) {
                          read.error = QStringLiteral(
                              "The address answered with a web page, not plugins.json." );
                      }
                      else {
                          read = readCatalog( bytes, id, hostVersion_ );
                      }
                      const auto state = outcome.signInNeeded    ? SignInRequiredState
                                         : !read.error.isEmpty()    ? QStringLiteral( "failed" )
                                         : read.entries.isEmpty() ? QStringLiteral( "empty" )
                                                                  : QStringLiteral( "loaded" );
                      LOG_INFO << "Plugin repository checked id=" << id.toStdString()
                               << " state=" << state.toStdString()
                               << " entries=" << read.entries.size()
                               << " warnings=" << read.warnings.size();
                      repositories_.recordCheck(
                          id, state, int( read.entries.size() ), int( read.warnings.size() ),
                          int( read.hidden.size() ), read.error, QDateTime::currentDateTimeUtc() );
                      run->results[ id ] = std::move( read );
                      fetchNextCatalogs( run );
                  } );
    }
    if ( run->active == 0 && run->pending.isEmpty() && !run->finished ) {
        finishCatalog( run );
    }
}

void PluginManager::finishCatalog( const std::shared_ptr<CatalogRun>& run )
{
    run->finished = true;
    QJsonArray merged;
    QVariantList summaries;
    QStringList errors;
    int fetched = 0;
    int position = 0;
    for ( const auto& record : repositories_.list() ) {
        ++position;
        const auto result = run->results.find( record.id );
        if ( result == run->results.end() ) {
            continue;
        }
        ++fetched;
        const auto& read = result->second;
        const auto state = run->signInNeeded.contains( record.id ) ? SignInRequiredState
                           : !read.error.isEmpty()                 ? QStringLiteral( "failed" )
                           : read.entries.isEmpty()                ? QStringLiteral( "empty" )
                                                                   : QStringLiteral( "loaded" );
        if ( read.error.isEmpty() ) {
            catalogs_[ record.id ] = read.entries;
            for ( const auto& entry : read.entries ) {
                merged.append( entry );
            }
        }
        else {
            // A failed refresh keeps the last successful catalog: its packages stay offered
            // (and their SHA-256 is still checked) until a fetch succeeds again.
            errors.append( record.name + ": " + read.error );
        }
        const auto cached = catalogs_.find( record.id );
        summaries.append(
            QVariantMap{ { "repository", record.id },
                         { "name", record.name },
                         { "position", position },
                         { "state", state },
                         { "signInNeeded", state == SignInRequiredState },
                         { "empty", state == QLatin1String( "empty" ) },
                         { "error", read.error },
                         { "entries", read.entries.size() },
                         { "cachedEntries", cached == catalogs_.end() ? 0 : cached->second.size() },
                         { "warnings", read.warnings },
                         { "hidden", read.hidden.toVariantList() } } );
    }
    const auto persisted = persistSettings();
    if ( !persisted.isEmpty() ) {
        LOG_WARNING << "Plugin repository check results could not be persisted.";
    }
    // A repository that fails does not hide the others; only all failing fails the job.
    const auto error = fetched > 0 && errors.size() == fetched ? errors.join( "; " ) : QString{};
    finishJob( run->jobId, error,
               { { "plugins", annotated( merged ) },
                 { "repositories", summaries },
                 { "packages", merged.size() },
                 { "failed", errors.size() } } );
    if ( catalogRun_ == run ) {
        catalogRun_.reset();
    }
    Q_EMIT changed();
}

Result PluginManager::selectPackage( const QString& operation, const QString& id,
                                     const QVariantMap& options, QJsonObject* selected ) const
{
    const auto selector = options.value( "catalog_entry_id" ).toString();
    if ( id.isEmpty() && selector.isEmpty() ) {
        return { "Name the plugin (plugin ID) or the catalog entry (catalog_entry_id).", {} };
    }
    // Candidates in repository order, each repository in catalog order.
    std::map<QString, QList<QJsonObject>> byRepository;
    QStringList order;
    for ( const auto& record : repositories_.list() ) {
        const auto cached = catalogs_.find( record.id );
        if ( !record.enabled || cached == catalogs_.end() ) {
            continue;
        }
        for ( const auto& value : cached->second ) {
            const auto entry = value.toObject();
            if ( ( id.isEmpty() || entry.value( "id" ).toString() == id )
                 && ( selector.isEmpty() || entry.value( "entry_id" ).toString() == selector ) ) {
                if ( !byRepository.count( record.id ) ) {
                    order.append( record.id );
                }
                byRepository[ record.id ].append( entry );
            }
        }
    }
    if ( order.isEmpty() ) {
        return { "Plugin not in the loaded catalog for this platform.", {} };
    }
    const auto pluginId
        = id.isEmpty() ? byRepository[ order.first() ].first().value( "id" ).toString() : id;
    const auto source = installedSource( pluginId );
    const auto repositoryName = [ this ]( const QString& repository ) {
        const auto record = repositories_.find( repository );
        return record ? record->name : repository + QStringLiteral( " (removed)" );
    };
    QJsonObject chosen;
    if ( !selector.isEmpty() ) {
        chosen = byRepository[ order.first() ].first(); // an entry ID names one entry
    }
    else if ( operation == "update" ) {
        // Updates come only from the repository the plugin was installed from.
        if ( source.isEmpty() ) {
            return { QStringLiteral( "%1 has no source repository: install it from a repository "
                                     "first, or name the entry (catalog_entry_id)." )
                         .arg( pluginId ),
                     {} };
        }
        if ( !byRepository.count( source ) ) {
            const auto record = repositories_.find( source );
            return { record && !record->enabled
                         ? QStringLiteral( "The source repository of %1, \"%2\", is disabled." )
                               .arg( pluginId, record->name )
                         : QStringLiteral( "%1 is not offered by its source repository \"%2\"." )
                               .arg( pluginId, repositoryName( source ) ),
                     {} };
        }
        chosen = newest( byRepository[ source ] );
    }
    else {
        // A new install takes the higher repository; an installed plugin stays with its source.
        const auto repository = byRepository.count( source ) ? source : order.first();
        chosen = newest( byRepository[ repository ] );
    }
    const auto from = chosen.value( "repository" ).toString();
    if ( !source.isEmpty() && from != source && !options.value( "switch_source" ).toBool() ) {
        return { QStringLiteral( "%1 was installed from \"%2\". Installing it from \"%3\" "
                                 "changes its source: confirm with switch_source." )
                     .arg( pluginId, repositoryName( source ), repositoryName( from ) ),
                 {} };
    }
    const auto installed = entries_.find( pluginId );
    if ( installed != entries_.end() ) {
        const auto installedKind = packageKind( installed->second->manifest.json );
        if ( installedKind != chosen.value( "kind" ).toString() ) {
            return { QStringLiteral( "An update cannot change the kind of %1 (%2 to %3)." )
                         .arg( pluginId, installedKind, chosen.value( "kind" ).toString() ),
                     {} };
        }
        // An older version, or the same one again, is installed only when its entry is named.
        const auto newer = compareSemanticVersions( chosen.value( "version" ).toString(),
                                                    installed->second->manifest.version )
                               .value_or( 0 );
        if ( selector.isEmpty() && newer < 0 ) {
            return { QStringLiteral( "The repository offers only older versions of %1 (%2); name "
                                     "the entry (catalog_entry_id) to install one." )
                         .arg( pluginId, chosen.value( "version" ).toString() ),
                     {} };
        }
        if ( selector.isEmpty() && newer == 0 && operation == "update" ) {
            return { QStringLiteral( "%1 %2 is already installed: it is the newest version in "
                                     "\"%3\"." )
                         .arg( pluginId, chosen.value( "version" ).toString(),
                               repositoryName( from ) ),
                     {} };
        }
    }
    *selected = chosen;
    return {};
}

} // namespace cilogg::plugins
