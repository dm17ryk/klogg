#include "updatetypes.h"

#include <QJsonArray>

namespace {
QJsonObject assetToJson( const ReleaseAsset& asset )
{
    return { { QStringLiteral( "platform" ), asset.platform },
             { QStringLiteral( "architecture" ), asset.architecture },
             { QStringLiteral( "kind" ), installKindName( asset.installKind ) },
             { QStringLiteral( "variant" ), asset.variant },
             { QStringLiteral( "filename" ), asset.fileName },
             { QStringLiteral( "size" ), asset.size },
             { QStringLiteral( "sha256" ), QString::fromLatin1( asset.sha256.toHex() ) },
             { QStringLiteral( "downloadUrl" ), asset.downloadUrl.toString() },
             { QStringLiteral( "githubDigest" ),
               QString::fromLatin1( asset.githubDigest.toHex() ) } };
}

ReleaseAsset assetFromJson( const QJsonObject& json )
{
    ReleaseAsset asset;
    asset.platform = json.value( QStringLiteral( "platform" ) ).toString();
    asset.architecture = json.value( QStringLiteral( "architecture" ) ).toString();
    asset.installKind = installKindFromName( json.value( QStringLiteral( "kind" ) ).toString() );
    asset.variant = json.value( QStringLiteral( "variant" ) ).toString();
    asset.fileName = json.value( QStringLiteral( "filename" ) ).toString();
    asset.size = json.value( QStringLiteral( "size" ) ).toVariant().toLongLong();
    asset.sha256
        = QByteArray::fromHex( json.value( QStringLiteral( "sha256" ) ).toString().toLatin1() );
    asset.downloadUrl = QUrl( json.value( QStringLiteral( "downloadUrl" ) ).toString() );
    asset.githubDigest = QByteArray::fromHex(
        json.value( QStringLiteral( "githubDigest" ) ).toString().toLatin1() );
    return asset;
}
} // namespace

QString installKindName( InstallKind kind )
{
    switch ( kind ) {
    case InstallKind::WindowsSetup:
        return QStringLiteral( "windows-setup" );
    case InstallKind::WindowsPortable:
        return QStringLiteral( "windows-portable" );
    case InstallKind::MacBundle:
        return QStringLiteral( "macos-bundle" );
    case InstallKind::AppImage:
        return QStringLiteral( "appimage" );
    case InstallKind::Deb:
        return QStringLiteral( "deb" );
    case InstallKind::Rpm:
        return QStringLiteral( "rpm" );
    case InstallKind::None:
        return QStringLiteral( "none" );
    }
    return QStringLiteral( "none" );
}

InstallKind installKindFromName( const QString& name )
{
    if ( name == QLatin1String( "windows-setup" ) )
        return InstallKind::WindowsSetup;
    if ( name == QLatin1String( "windows-portable" ) )
        return InstallKind::WindowsPortable;
    if ( name == QLatin1String( "macos-bundle" ) )
        return InstallKind::MacBundle;
    if ( name == QLatin1String( "appimage" ) )
        return InstallKind::AppImage;
    if ( name == QLatin1String( "deb" ) )
        return InstallKind::Deb;
    if ( name == QLatin1String( "rpm" ) )
        return InstallKind::Rpm;
    return InstallKind::None;
}

QString updateStateName( UpdateState state )
{
    switch ( state ) {
    case UpdateState::Idle:
        return QStringLiteral( "idle" );
    case UpdateState::Checking:
        return QStringLiteral( "checking" );
    case UpdateState::ResolvingPackage:
        return QStringLiteral( "resolving-package" );
    case UpdateState::Available:
        return QStringLiteral( "available" );
    case UpdateState::Downloading:
        return QStringLiteral( "downloading" );
    case UpdateState::Verifying:
        return QStringLiteral( "verifying" );
    case UpdateState::Staging:
        return QStringLiteral( "staging" );
    case UpdateState::Ready:
        return QStringLiteral( "ready" );
    case UpdateState::Scheduled:
        return QStringLiteral( "scheduled" );
    case UpdateState::Installing:
        return QStringLiteral( "installing" );
    case UpdateState::Error:
        return QStringLiteral( "error" );
    }
    return QStringLiteral( "unknown" );
}

QJsonObject PendingUpdate::toJson() const
{
    const auto& info = release;
    QJsonObject releaseJson{ { QStringLiteral( "tag" ), info.tag },
                             { QStringLiteral( "version" ), info.version },
                             { QStringLiteral( "name" ), info.name },
                             { QStringLiteral( "body" ), info.body },
                             { QStringLiteral( "pageUrl" ), info.pageUrl.toString() },
                             { QStringLiteral( "prerelease" ), info.prerelease },
                             { QStringLiteral( "asset" ), assetToJson( info.asset ) } };
    return { { QStringLiteral( "schema" ), 1 },
             { QStringLiteral( "transactionToken" ), transactionToken },
             { QStringLiteral( "release" ), releaseJson },
             { QStringLiteral( "packagePath" ), packagePath },
             { QStringLiteral( "stagedPath" ), stagedPath },
             { QStringLiteral( "currentPath" ), currentPath },
             { QStringLiteral( "backupPath" ), backupPath },
             { QStringLiteral( "acknowledgementPath" ), acknowledgementPath },
             { QStringLiteral( "helperLogPath" ), helperLogPath },
             { QStringLiteral( "installOnExit" ), installOnExit } };
}

PendingUpdate PendingUpdate::fromJson( const QJsonObject& json, QString* errorMessage )
{
    PendingUpdate pending;
    if ( json.value( QStringLiteral( "schema" ) ).toInt() != 1 ) {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Unsupported pending-update schema" );
        return pending;
    }
    const auto releaseJson = json.value( QStringLiteral( "release" ) ).toObject();
    pending.transactionToken = json.value( QStringLiteral( "transactionToken" ) ).toString();
    pending.release.tag = releaseJson.value( QStringLiteral( "tag" ) ).toString();
    pending.release.version = releaseJson.value( QStringLiteral( "version" ) ).toString();
    pending.release.name = releaseJson.value( QStringLiteral( "name" ) ).toString();
    pending.release.body = releaseJson.value( QStringLiteral( "body" ) ).toString();
    pending.release.pageUrl = QUrl( releaseJson.value( QStringLiteral( "pageUrl" ) ).toString() );
    pending.release.prerelease = releaseJson.value( QStringLiteral( "prerelease" ) ).toBool();
    pending.release.asset
        = assetFromJson( releaseJson.value( QStringLiteral( "asset" ) ).toObject() );
    pending.packagePath = json.value( QStringLiteral( "packagePath" ) ).toString();
    pending.stagedPath = json.value( QStringLiteral( "stagedPath" ) ).toString();
    pending.currentPath = json.value( QStringLiteral( "currentPath" ) ).toString();
    pending.backupPath = json.value( QStringLiteral( "backupPath" ) ).toString();
    pending.acknowledgementPath = json.value( QStringLiteral( "acknowledgementPath" ) ).toString();
    pending.helperLogPath = json.value( QStringLiteral( "helperLogPath" ) ).toString();
    pending.installOnExit = json.value( QStringLiteral( "installOnExit" ) ).toBool();
    if ( !pending.isValid() && errorMessage ) {
        *errorMessage = QStringLiteral( "Pending-update data is incomplete or unverified" );
    }
    return pending;
}
