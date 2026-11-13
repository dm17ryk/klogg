#pragma once

#include <ctime>
#include <memory>
#include <optional>

#include <QCryptographicHash>
#include <QObject>

#include "configuration.h"
#include "persistable.h"
#include "updatetypes.h"

class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;
class QTimer;

class VersionCheckerConfig final : public Persistable<VersionCheckerConfig, session_settings> {
public:
    static const char* persistableName()
    {
        return "VersionCheckerConfig";
    }
    std::time_t nextDeadline() const
    {
        return nextDeadline_;
    }
    void setNextDeadline( std::time_t deadline )
    {
        nextDeadline_ = deadline;
    }
    QByteArray etag() const
    {
        return etag_;
    }
    void setEtag( QByteArray etag )
    {
        etag_ = std::move( etag );
    }
    QByteArray cachedReleases() const
    {
        return cachedReleases_;
    }
    void setCachedReleases( QByteArray releases )
    {
        cachedReleases_ = std::move( releases );
    }

    void saveToStorage( QSettings& settings ) const;
    void retrieveFromStorage( QSettings& settings );

private:
    std::time_t nextDeadline_ = {};
    QByteArray etag_;
    QByteArray cachedReleases_;
};

class VersionChecker : public QObject {
    Q_OBJECT

public:
    // What a persisted update transaction means for the running installation.
    enum class PendingDisposition {
        Invalid,             // incomplete or unverifiable data
        ForeignInstallation, // downloaded by another CILogg installation; never touched
        Completed,           // this installation already runs that version or a newer one
        Applicable           // still to be installed by this installation
    };

    // Construction is passive: it neither reads nor locks the persisted update state, so
    // CLI and secondary processes (one per MCP tool call) never interfere with an update.
    explicit VersionChecker( QObject* parent = nullptr );
    // Test-only transport endpoint. Production code uses the default constructor,
    // which accepts only trusted HTTPS GitHub hosts.
    explicit VersionChecker( const QUrl& testReleasesUrl, QObject* parent = nullptr );
    ~VersionChecker() override;

    // Binds this checker to the running installation: acknowledges an updated startup,
    // reconciles the persisted update transaction and arms the exit-time install. Call once,
    // from the primary GUI process.
    void activate();
    bool isActive() const
    {
        return active_;
    }

    void startCheck();
    // The scheduled check without the configuration and automation gates of startCheck().
    // Failures are logged and retried, never reported through errorOccurred().
    void startBackgroundCheck( UpdateChannel channel );
    UpdateState state() const
    {
        return state_;
    }
    PendingUpdate pendingUpdate() const
    {
        return pendingUpdate_;
    }

    // Test-only (with the test transport constructor): a private update directory and
    // short background retry delays.
    void setUpdateRootForTesting( const QString& updateRoot );
    void setBackgroundRetryDelaysForTesting( const QList<int>& delaysMs );
    void setCurrentVersionForTesting( const QString& version );

    static PendingDisposition classifyPendingUpdate( const PendingUpdate& pending,
                                                     const QString& currentVersion,
                                                     const QString& installationPath );
    // The path an update of `kind` replaces for this process.
    static QString installationPath( InstallKind kind );
    // Moves the scheduled transaction `token` out of the way of every other process.
    // Exactly one caller can claim a transaction.
    static bool claimScheduledUpdate( const QString& updateRoot, const QString& token,
                                      QString* errorMessage = nullptr );
    // `phase` is the helper's "launch" (the installer could not be started) or "installer"
    // (it ran and returned `code`); empty for results without a phase.
    static QString describeInstallResult( int code, const QString& phase = {} );

    static bool isVersionNewer( const QString& currentVersion, const QString& candidateVersion );
    static std::optional<ReleaseInfo> selectRelease( const QByteArray& releasesJson,
                                                     UpdateChannel channel,
                                                     QString* errorMessage = nullptr );
    static QList<ReleaseAsset> parseManifest( const QByteArray& manifestJson,
                                              QString* errorMessage = nullptr );
    static InstallKind classifyLinuxInstallKind( const QString& distroId, const QString& distroLike,
                                                 bool appImage );
    static std::optional<ReleaseAsset> selectAsset( const QList<ReleaseAsset>& manifestAssets,
                                                    const QList<ReleaseAsset>& githubAssets,
                                                    QString* errorMessage = nullptr );
    static bool isSafeArchivePath( const QString& relativePath, const QString& symlinkTarget = {} );

public Q_SLOTS:
    void forceCheck();
    void forceCheck( UpdateChannel channel );
    void downloadUpdate( const ReleaseInfo& release, bool installOnExit );
    void cancelDownload();
    void launchPendingUpdate();
    void acknowledgeStartup();

Q_SIGNALS:
    void releaseFound( const ReleaseInfo& release );
    void newVersionFound( const QString& version, const QString& pageUrl, const QString& assetUrl,
                          const QStringList& changes );
    void checkFinished( bool updateAvailable, const QString& message );
    void downloadProgress( qint64 received, qint64 total );
    void updateReady( const PendingUpdate& pending );
    void stateChanged( UpdateState state );
    // User-initiated operations only (manual checks, downloads, installs).
    void errorOccurred( const QString& message );
    // A scheduled check failed; it is retried while the failure looks transient.
    void backgroundCheckFailed( const QString& message, bool willRetry );
    // The install handed to the helper at the previous exit did not complete.
    void installFailed( const QString& message );

private:
    enum class RequestPurpose { Releases, Manifest, Package };

    void requestReleases( UpdateChannel channel, bool forced );
    void requestManifest( const ReleaseInfo& release, const ReleaseAsset& manifestAsset );
    void startRequest( const QUrl& url, RequestPurpose purpose, const QByteArray& etag = {} );
    void handleReplyFinished();
    void handleReleasesReply( QNetworkReply& reply, const QByteArray& data );
    void handleManifestReply( QNetworkReply& reply, const QByteArray& data );
    void handlePackageReply( QNetworkReply& reply );
    void fail( const QString& message, bool transient = false );
    void setState( UpdateState state, const QString& decision );
    void finishScheduledCheck();
    bool preparePendingUpdate( QString* errorMessage );
    bool persistPendingUpdate( QString* errorMessage = nullptr ) const;
    bool writePendingUpdate( const PendingUpdate& pending, QString* errorMessage = nullptr ) const;
    void reconcileLaunchedUpdate();
    void reconcileScheduledUpdate();
    void removeExpiredDownloads();
    void retryBackgroundCheck();
    QString updateRoot() const;
    QString currentVersion() const;
    QString pendingFilePath() const;
    QString launchedFilePath() const;
    void initialize();

    QNetworkAccessManager* manager_ = nullptr;
    QNetworkReply* activeReply_ = nullptr;
    std::unique_ptr<QSaveFile> downloadFile_;
    QCryptographicHash packageHash_{ QCryptographicHash::Sha256 };
    RequestPurpose requestPurpose_ = RequestPurpose::Releases;
    UpdateState state_ = UpdateState::Idle;
    UpdateChannel requestedChannel_ = UpdateChannel::Stable;
    bool forced_ = false;
    bool backgroundCheck_ = false;
    int backgroundRetries_ = 0;
    QList<int> backgroundRetryDelaysMs_{ 30 * 1000, 2 * 60 * 1000, 10 * 60 * 1000 };
    QTimer* backgroundRetryTimer_ = nullptr;
    bool installOnExit_ = false;
    bool active_ = false;
    ReleaseInfo resolvingRelease_;
    QList<ReleaseAsset> githubAssets_;
    PendingUpdate pendingUpdate_;
    QUrl releasesUrl_;
    QString testUpdateRoot_;
    QString testCurrentVersion_;
    bool testTransport_ = false;
};
