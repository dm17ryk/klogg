#pragma once
#include "plugincatalog.h"
#include "pluginprocess.h"
#include "pluginrepositories.h"
#include "pluginrepository.h"
#include "pluginstorage.h"
#include "repositorycredentials.h"
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QVariantMap>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace cilogg::plugins {
struct Result {
    QString error;
    QVariantMap payload;
    bool ok() const
    {
        return error.isEmpty();
    }
};

// Who started a request (docs/plugin-repositories.md 3.6). Set by the transport, never by a
// payload. Only `gui` may show a sign-in window; only `gui` and `cli` may submit a personal access
// token. An empty origin is treated like an automation origin.
inline const QString RequestOriginGui = QStringLiteral( "gui" );
inline const QString RequestOriginCli = QStringLiteral( "cli" );

class PluginManager final : public QObject {
    Q_OBJECT
public:
    explicit PluginManager( const QString& dataRoot = {}, const QString& shippedRoot = {},
                            QObject* parent = nullptr, QNetworkAccessManager* network = nullptr );
    ~PluginManager() override;
    void discover();
    QVariantList plugins() const;
    QVariantList jobs() const;
    Result manage( const QString& operation, const QString& pluginId, const QVariantMap& options,
                   const QString& origin = {} );
    // Where personal access tokens are kept (default: the operating system credential store).
    void setPatStore( std::unique_ptr<PatStore> store );
    // The key the personal access token of an Azure DevOps repository is kept under: the CILogg
    // profile (the plugin data folder), the repository ID and its address, so neither another
    // CILogg copy nor a later repository with another address can read it. Empty when there is
    // no such repository.
    QString tokenKey( const QString& repositoryId ) const;
    Result invoke( const QString& pluginId, const QString& action, const QVariantMap& arguments,
                   const QVariantMap& context );
    Result job( const QString& id ) const;
    Result cancel( const QString& id );
    void closeFile( const QString& path, const QString& windowId = {} );
    void closeWindow( const QString& windowId );
    void shutdown();
    // The pre-E15 single catalog address; kept readable for older copies that share the folder.
    QString catalogUrl() const;
    // Plugin repositories in order (docs/plugin-repositories.md 3.1, 3.8).
    QVariantList repositories() const;
    // The entries of each repository's last successful fetch, in repository order, with the
    // installed version of each plugin.
    QVariantList catalogEntries() const;
    // The CILogg version that catalog `min_host_version` keys are compared with.
    void setHostVersion( const QString& version );
    QString hostVersion() const
    {
        return hostVersion_;
    }
    QVariantMap storageStatus() const;
    QString dataRoot() const
    {
        return dataRoot_;
    }

    // Plugin views and message boxes (docs/plugin-views.md). The UI owns the widgets; the
    // manager owns the protocol, the open-view registry and the open-message limit.
    QJsonArray viewDefinitions( const QString& pluginId ) const;
    Result openView( const QString& pluginId, const QString& viewId, const QVariantMap& context );
    void closeView( const QString& pluginId, const QString& viewId, const QString& windowId );
    bool sendViewEvent( const QString& pluginId, const QString& viewId, const QString& windowId,
                        const QJsonObject& event );
    void finishMessage( const QString& pluginId, const QString& messageId, const QString& button,
                        bool timedOut );
    void noteActiveWindow( const QString& windowId );
    QVariantMap viewState( const QString& pluginId, const QString& viewId ) const;
    void setViewState( const QString& pluginId, const QString& viewId, const QVariantMap& state );
    QString instanceId() const
    {
        return instanceId_;
    }
    void setInstanceId( const QString& id );
    // Grace period between cancel_plugin_job and the forced `cancelled` state (default 10 s).
    void setCancelGraceMs( int milliseconds );
    // At most `count` new message boxes per plugin within `windowMs` (default 10 per 5 s).
    void setMessageRateLimit( int count, int windowMs );
    // Process state of a plugin ("stopped" when it has no process); empty when unknown.
    QString pluginState( const QString& pluginId ) const;

Q_SIGNALS:
    void changed();
    void fileReady( const QString& path, bool follow, const QVariantMap& context );
    void panelUpdated( const QString& pluginId, const QJsonObject& panel,
                       const QVariantMap& context );
    void pluginStopped( const QString& pluginId );
    // view/update, view/show, view/hide, ui/message and ui/close_message from a plugin.
    // view/show and ui/message always carry the resolved target `window_id`.
    void viewRequest( const QString& pluginId, const QString& method, const QJsonObject& params );
    // The plugin (re)initialized and was sent view/opened for its open views.
    void pluginReady( const QString& pluginId );
    // The plugin was disabled or removed: close its views without a result.
    void viewsRevoked( const QString& pluginId );

private:
    struct Entry {
        Manifest manifest;
        std::unique_ptr<PluginProcess> process;
        QString diagnostics;
    };
    struct Job {
        QString id;
        QString pluginId;
        QString action;
        QString state = QStringLiteral( "pending" );
        QString error;
        QJsonObject params;
        QVariantMap context;
        QVariantMap result;
        QString outputPath;
        double progress = 0;
        bool cancelRequested = false;
        bool invocation = false;
        bool restartStarted = false;
        bool outputSupervised = false;
        qint64 outputReservation = 0;
        qint64 observedOutputBytes = 0;
    };
    struct Stream {
        QString pluginId;
        QString jobId;
        QString path;
        QVariantMap context;
        std::unique_ptr<QFile> file;
        quint64 nextSequence = 0;
    };
    struct OpenView {
        QString pluginId;
        QString viewId;
        QString windowId;
        QVariantMap context;
    };
    struct OpenMessage {
        QString pluginId;
        QString messageId;
        QString windowId;
    };
    // One `catalog` job: every repository it fetches, at most MaxParallelCatalogs at a time.
    struct CatalogRun {
        QString jobId;
        QStringList pending;
        int active = 0;
        bool finished = false;
        bool interactive = false; // started in the GUI: the credential manager may prompt
        std::map<QString, CatalogRead> results;
        QStringList signInNeeded; // repositories that answered 401, 203 or a web page
    };
    // The credential a download may attach, and only inside `scope` (design 3.6).
    struct DownloadAuth {
        CredentialPtr credential;
        std::optional<CredentialScope> scope;
        bool azureDevOps = false; // X-TFS-FedAuthRedirect: Suppress; 203 or HTML is a sign-in page
    };
    struct DownloadOutcome {
        QString error;
        bool signInNeeded = false;
        bool credentialRefused = false; // 401 to a request that carried the credential
    };
    using DownloadHandler
        = std::function<void( const QByteArray& bytes, const DownloadOutcome& outcome )>;
    struct DownloadState;
    // How a repository's catalog is fetched: whether the credential manager may prompt, and the
    // method and token of a sign-in test.
    struct SignInRequest {
        bool interactive = false;
        QString method; // empty: the repository's credential source
        QByteArray token;
    };
    using CredentialDone = std::function<void( CredentialPtr credential, const QString& error )>;
    void handleUiMessage( const QString& pluginId, const QString& method,
                          const QJsonObject& params );
    Result cancelJob( const QString& id, bool grace );
    void rejectMessage( const QString& pluginId, const QString& messageId, const QString& error );
    void sendViewOpened( PluginProcess* process, const OpenView& view );
    void revokeViews( const QString& pluginId );
    QString resolveWindow( const QString& requested ) const;
    bool declaresView( const QString& pluginId, const QString& viewId ) const;
    QString addJob( const QString& pluginId, const QString& action, const QVariantMap& context = {},
                    bool management = false );
    QVariantMap jobPayload( const Job& job ) const;
    void finishJob( const QString& id, const QString& error, const QVariantMap& result = {} );
    PluginProcess* ensureProcess( Entry& entry );
    void dispatchJob( const QString& id );
    void completeManagement( const QString& operation, const QString& pluginId,
                             const QString& jobId );
    void handleMessage( const QString& pluginId, const QJsonObject& frame );
    void processStopped( const QString& pluginId );
    void stopStream( const QString& id, bool notify );
    void install( const QString& source, const QString& jobId, const QString& expectedId = {},
                  const QString& expectedVersion = {} );
    void commitWhenStopped( const StagedPlugin& stage, const QString& jobId,
                            const InstallSource& source );
    // HTTPS GET within `limit` bytes and 30 s. The handler runs later on this thread, and not
    // at all once the job is cancelled or CILogg shuts down. With a credential, redirects are
    // followed here (at most 5, HTTPS, each checked against the scope; the credential is dropped
    // once a hop leaves it), each hop with a fresh 30 s timer and the same limit. `auth` has no
    // default argument: GCC and Clang refuse one whose type is a nested struct with member
    // initializers, which is not complete inside its enclosing class.
    void download( const QString& url, qint64 limit, const QString& jobId, DownloadHandler handler,
                   DownloadAuth auth );
    void downloadHop( const QUrl& target, const std::shared_ptr<DownloadState>& state );
    QString gitExecutable() const;
    void acquireCredential( const RepositoryRecord& record, const AzureDevOpsLocation& location,
                            const SignInRequest& request, CredentialDone done );
    // The catalog of one repository, signed in when it needs it. A credential manager
    // credential refused with 401 is rejected and asked for once more only when interactive.
    void fetchRepositoryCatalog( const RepositoryRecord& record, const QString& jobId,
                                 const SignInRequest& request, DownloadHandler done,
                                 bool retried = false );
    // A package, with the repository's credential inside its scope. As with a catalog, a credential
    // manager credential refused with 401 is rejected and asked for once more only when
    // interactive.
    void fetchPackage( const QJsonObject& entry, const QString& jobId, bool interactive,
                       bool retried = false );
    void downloadPackage( const QJsonObject& entry, const QString& jobId, const QString& url,
                          DownloadAuth auth );
    void stagePackage( const QJsonObject& entry, const QString& jobId, const QByteArray& bytes );
    QString tokenKeyFor( const RepositoryRecord& record ) const;
    // Tokens of removed repositories: listTokenDeletion() lists a key in the settings (the caller
    // persists them), deletePendingTokens() asks the store to delete every listed key that is not
    // already being deleted and deleteToken() asks for one key. A key stays listed until its
    // deletion works and is tried again at the next repository change and the next start.
    void listTokenDeletion( const QString& key );
    void deletePendingTokens();
    void deleteToken( const QString& key );
    Result manageRepositories( const QString& operation, const QVariantMap& options,
                               const QString& origin );
    Result signIn( const QVariantMap& options, const QString& origin );
    Result signOut( const QVariantMap& options );
    Result startCatalog( const QVariantMap& options, const QString& origin );
    void fetchNextCatalogs( const std::shared_ptr<CatalogRun>& run );
    void finishCatalog( const std::shared_ptr<CatalogRun>& run );
    Result selectPackage( const QString& operation, const QString& id, const QVariantMap& options,
                          QJsonObject* selected ) const;
    QString installedSource( const QString& id ) const;
    QVariantMap sourceInfo( const QString& id, const Entry& entry ) const;
    QVariantList annotated( const QJsonArray& entries ) const;
    QString persistSettings();
    QString configurationKey( const QString& id ) const;
    void rememberSecretFields( const QString& id, const QJsonArray& fields );
    qint64 reservedCaptureBytes() const;
    void refreshCaptureUsage();
    void recordCaptureBytes( qint64 delta );
    void monitorStorage();
    void failStorageJob( QString pluginId, QString jobId, QString error );
    Result configureStorage( const QVariantMap& limits );
    QString dataRoot_;
    QString shippedRoot_;
    QSettings settings_;
    PluginRepositories repositories_;
    CaptureStorage storage_;
    QNetworkAccessManager network_;
    QNetworkAccessManager* networkAccess_;
    std::map<QString, std::unique_ptr<Entry>> entries_;
    std::map<QString, Job> jobs_;
    std::map<QString, Stream> streams_;
    std::vector<OpenView> openViews_;
    std::vector<OpenMessage> messages_;
    QStringList activeWindows_; // most recently active first
    QString instanceId_;
    int cancelGraceMs_ = 10000;
    std::map<QString, std::deque<qint64>> messageTimes_;
    QElapsedTimer clock_;
    int messageRateCount_ = 10;
    int messageRateWindowMs_ = 5000;
    // Usable entries of each repository's last successful fetch, by repository ID.
    std::map<QString, QJsonArray> catalogs_;
    QString catalogJobId_;
    std::shared_ptr<CatalogRun> catalogRun_;
    QString hostVersion_;
    std::unique_ptr<PatStore> patStore_ = createSystemPatStore();
    QStringList deletingTokens_; // token deletions the store is working on
    bool storageScanActive_ = false;
    int storageTicks_ = 0;
    quint64 storageScanGeneration_ = 0;
    bool shuttingDown_ = false;
};
} // namespace cilogg::plugins
