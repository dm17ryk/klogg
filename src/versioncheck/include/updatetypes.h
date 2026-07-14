#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QUrl>

enum class UpdateState {
    Idle,
    Checking,
    ResolvingPackage,
    Available,
    Downloading,
    Verifying,
    Staging,
    Ready,
    Scheduled,
    Installing,
    Error
};

enum class InstallKind { None, WindowsSetup, WindowsPortable, MacBundle, AppImage, Deb, Rpm };

struct ReleaseAsset {
    QString platform;
    QString architecture;
    InstallKind installKind = InstallKind::None;
    QString variant;
    QString fileName;
    qint64 size = -1;
    QByteArray sha256;
    QUrl downloadUrl;
    QByteArray githubDigest;

    bool isInstallable() const
    {
        return installKind != InstallKind::None && !fileName.isEmpty() && size >= 0
               && sha256.size() == 32 && githubDigest == sha256 && downloadUrl.isValid()
               && downloadUrl.scheme() == QStringLiteral( "https" );
    }
};

struct ReleaseInfo {
    QString tag;
    QString version;
    QString name;
    QString body;
    QUrl pageUrl;
    bool prerelease = false;
    ReleaseAsset asset;
    QString automaticUpdateBlockReason;

    bool canAutomaticallyUpdate() const
    {
        return asset.isInstallable();
    }
};

struct PendingUpdate {
    QString transactionToken;
    ReleaseInfo release;
    QString packagePath;
    QString stagedPath;
    QString currentPath;
    QString backupPath;
    QString acknowledgementPath;
    QString helperLogPath;
    bool installOnExit = false;

    bool isValid() const
    {
        return !transactionToken.isEmpty() && !packagePath.isEmpty()
               && release.asset.isInstallable();
    }

    QJsonObject toJson() const;
    static PendingUpdate fromJson( const QJsonObject& json, QString* errorMessage = nullptr );
};

QString installKindName( InstallKind kind );
InstallKind installKindFromName( const QString& name );
QString updateStateName( UpdateState state );

Q_DECLARE_METATYPE( ReleaseAsset )
Q_DECLARE_METATYPE( ReleaseInfo )
Q_DECLARE_METATYPE( PendingUpdate )
Q_DECLARE_METATYPE( UpdateState )
