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
    explicit VersionChecker( QObject* parent = nullptr );
    // Test-only transport endpoint. Production code uses the default constructor,
    // which accepts only trusted HTTPS GitHub hosts.
    explicit VersionChecker( const QUrl& testReleasesUrl, QObject* parent = nullptr );
    ~VersionChecker() override;

    void startCheck();
    UpdateState state() const
    {
        return state_;
    }
    PendingUpdate pendingUpdate() const
    {
        return pendingUpdate_;
    }

    static bool isVersionNewer( const QString& currentVersion, const QString& candidateVersion );
    static std::optional<ReleaseInfo> selectRelease( const QByteArray& releasesJson,
                                                     UpdateChannel channel,
                                                     QString* errorMessage = nullptr );
    static QList<ReleaseAsset> parseManifest( const QByteArray& manifestJson,
                                              QString* errorMessage = nullptr );
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
    void errorOccurred( const QString& message );

private:
    enum class RequestPurpose { Releases, Manifest, Package };

    void requestReleases( UpdateChannel channel, bool forced );
    void requestManifest( const ReleaseInfo& release, const ReleaseAsset& manifestAsset );
    void startRequest( const QUrl& url, RequestPurpose purpose, const QByteArray& etag = {} );
    void handleReplyFinished();
    void handleReleasesReply( QNetworkReply& reply, const QByteArray& data );
    void handleManifestReply( QNetworkReply& reply, const QByteArray& data );
    void handlePackageReply( QNetworkReply& reply );
    void fail( const QString& message );
    void setState( UpdateState state, const QString& decision );
    void finishScheduledCheck();
    bool preparePendingUpdate( QString* errorMessage );
    bool persistPendingUpdate( QString* errorMessage = nullptr ) const;
    void loadPendingUpdate();
    QString updateRoot() const;
    void initialize();

    QNetworkAccessManager* manager_ = nullptr;
    QNetworkReply* activeReply_ = nullptr;
    std::unique_ptr<QSaveFile> downloadFile_;
    QCryptographicHash packageHash_{ QCryptographicHash::Sha256 };
    RequestPurpose requestPurpose_ = RequestPurpose::Releases;
    UpdateState state_ = UpdateState::Idle;
    UpdateChannel requestedChannel_ = UpdateChannel::Stable;
    bool forced_ = false;
    bool installOnExit_ = false;
    ReleaseInfo resolvingRelease_;
    QList<ReleaseAsset> githubAssets_;
    PendingUpdate pendingUpdate_;
    QUrl releasesUrl_;
    bool testTransport_ = false;
};
