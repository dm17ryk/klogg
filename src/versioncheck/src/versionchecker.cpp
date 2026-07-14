#include "versionchecker.h"

#include "klogg_version.h"
#include "log.h"

#include <algorithm>
#include <ctime>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>
#include <QVersionNumber>

#include <karchive.h>
#include <karchiveentry.h>
#include <kzip.h>

namespace {
constexpr auto ReleasesUrl = "https://api.github.com/repos/dm17ryk/klogg/releases?per_page=30";
constexpr auto ManifestName = "cilogg-update-manifest.json";

std::time_t checkIntervalForFrequency( UpdateFrequency frequency )
{
    switch ( frequency ) {
    case UpdateFrequency::OnStart:
        return 0;
    case UpdateFrequency::Daily:
        return 3600 * 24;
    case UpdateFrequency::Weekly:
        return 3600 * 24 * 7;
    case UpdateFrequency::Monthly:
        return 3600 * 24 * 30;
    }
    LOG_WARNING << "Unknown update frequency; falling back to weekly";
    return 3600 * 24 * 7;
}

QByteArray digestBytes( const QString& digest )
{
    const auto parts = digest.split( QLatin1Char( ':' ) );
    if ( parts.size() != 2
         || parts.at( 0 ).compare( QLatin1String( "sha256" ), Qt::CaseInsensitive ) != 0 ) {
        return {};
    }
    const auto bytes = QByteArray::fromHex( parts.at( 1 ).toLatin1() );
    return bytes.size() == 32 ? bytes : QByteArray{};
}

bool trustedUpdateHost( const QString& host )
{
    const auto normalized = host.toLower();
    return normalized == QLatin1String( "api.github.com" )
           || normalized == QLatin1String( "github.com" )
           || normalized == QLatin1String( "objects.githubusercontent.com" )
           || normalized.endsWith( QLatin1String( ".githubusercontent.com" ) );
}

QString normalizedVersion( QString tag )
{
    if ( tag.startsWith( QLatin1String( "continuous-" ) ) )
        tag.remove( 0, 11 );
    if ( tag.startsWith( QLatin1Char( 'v' ), Qt::CaseInsensitive ) )
        tag.remove( 0, 1 );
    return tag;
}

QString currentPlatform()
{
#ifdef Q_OS_WIN
    return QStringLiteral( "windows" );
#elif defined( Q_OS_MACOS )
    return QStringLiteral( "macos" );
#else
    return QStringLiteral( "linux" );
#endif
}

QString currentArchitecture()
{
    const auto arch = QSysInfo::currentCpuArchitecture().toLower();
    if ( arch.contains( QLatin1String( "arm64" ) )
         || arch.contains( QLatin1String( "aarch64" ) ) ) {
        return QStringLiteral( "arm64" );
    }
    if ( arch == QLatin1String( "i386" ) || arch == QLatin1String( "i686" )
         || arch == QLatin1String( "x86" ) ) {
        return QStringLiteral( "x86" );
    }
    return QStringLiteral( "x64" );
}

QMap<QString, QString> osRelease()
{
    QMap<QString, QString> result;
#ifdef Q_OS_LINUX
    QFile file( QStringLiteral( "/etc/os-release" ) );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        LOG_WARNING << "Package selection cannot read /etc/os-release: " << file.errorString();
        return result;
    }
    while ( !file.atEnd() ) {
        const auto line = QString::fromUtf8( file.readLine() ).trimmed();
        const auto separator = line.indexOf( QLatin1Char( '=' ) );
        if ( separator <= 0 )
            continue;
        auto value = line.mid( separator + 1 );
        if ( value.size() >= 2 && value.front() == QLatin1Char( '"' )
             && value.back() == QLatin1Char( '"' ) ) {
            value = value.mid( 1, value.size() - 2 );
        }
        result.insert( line.left( separator ), value );
    }
#endif
    return result;
}

bool safeArchiveTree( const KArchiveDirectory* directory, const QString& prefix,
                      QString* errorMessage )
{
    for ( const auto& name : directory->entries() ) {
        const auto relative = prefix.isEmpty() ? name : prefix + QLatin1Char( '/' ) + name;
        const auto clean = QDir::cleanPath( relative );
        const auto* entry = directory->entry( name );
        if ( !VersionChecker::isSafeArchivePath( relative,
                                                 entry ? entry->symLinkTarget() : QString{} ) ) {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Unsafe archive entry: %1" ).arg( relative );
            return false;
        }
        if ( entry && entry->isDirectory()
             && !safeArchiveTree( static_cast<const KArchiveDirectory*>( entry ), clean,
                                  errorMessage ) ) {
            return false;
        }
    }
    return true;
}

bool extractPortableArchive( const QString& archivePath, const QString& destination,
                             QString* errorMessage )
{
    KZip archive( archivePath );
    if ( !archive.open( QIODevice::ReadOnly ) ) {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Cannot open portable update archive" );
        return false;
    }
    const auto* root = archive.directory();
    if ( !root || !safeArchiveTree( root, {}, errorMessage ) ) {
        archive.close();
        return false;
    }
    QDir( destination ).removeRecursively();
    if ( !QDir().mkpath( destination ) ) {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Cannot create portable staging directory" );
        archive.close();
        return false;
    }
    const bool copied = root->copyTo( destination, true );
    archive.close();
    if ( !copied && errorMessage )
        *errorMessage = QStringLiteral( "Portable archive extraction failed" );
    return copied;
}

ReleaseAsset githubAssetFromJson( const QJsonObject& json )
{
    ReleaseAsset asset;
    asset.fileName = json.value( QStringLiteral( "name" ) ).toString();
    asset.size = json.value( QStringLiteral( "size" ) ).toVariant().toLongLong();
    asset.downloadUrl = QUrl( json.value( QStringLiteral( "browser_download_url" ) ).toString() );
    asset.githubDigest = digestBytes( json.value( QStringLiteral( "digest" ) ).toString() );
    return asset;
}

bool fileMatchesVerifiedAsset( const QString& path, const ReleaseAsset& asset )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) || file.size() != asset.size )
        return false;
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    while ( !file.atEnd() ) {
        const auto block = file.read( 1024 * 1024 );
        if ( block.isEmpty() && file.error() != QFileDevice::NoError )
            return false;
        hash.addData( block );
    }
    const auto digest = hash.result();
    return digest == asset.sha256 && digest == asset.githubDigest;
}
} // namespace

bool VersionChecker::isSafeArchivePath( const QString& relativePath, const QString& symlinkTarget )
{
    const auto clean = QDir::cleanPath( relativePath );
    if ( relativePath.isEmpty() || QDir::isAbsolutePath( relativePath )
         || clean == QLatin1String( ".." ) || clean.startsWith( QLatin1String( "../" ) )
         || relativePath.contains( QLatin1String( "/../" ) )
         || relativePath.contains( QLatin1Char( '\\' ) ) ) {
        return false;
    }
    if ( !symlinkTarget.isEmpty() ) {
        const auto target = QDir::cleanPath( symlinkTarget );
        if ( QDir::isAbsolutePath( target ) || target == QLatin1String( ".." )
             || target.startsWith( QLatin1String( "../" ) ) )
            return false;
    }
    return true;
}

void VersionCheckerConfig::retrieveFromStorage( QSettings& settings )
{
    LOG_DEBUG << "VersionCheckerConfig::retrieveFromStorage";
    nextDeadline_
        = settings.value( QStringLiteral( "VersionChecker/nextDeadline" ), 0 ).toLongLong();
    etag_ = settings.value( QStringLiteral( "VersionChecker/etag" ) ).toByteArray();
    cachedReleases_
        = settings.value( QStringLiteral( "VersionChecker/cachedReleases" ) ).toByteArray();
    LOG_DEBUG << "Loaded updater cache: deadline=" << nextDeadline_
              << ", etag-present=" << !etag_.isEmpty()
              << ", cached-bytes=" << cachedReleases_.size();
}

void VersionCheckerConfig::saveToStorage( QSettings& settings ) const
{
    LOG_DEBUG << "VersionCheckerConfig::saveToStorage";
    settings.setValue( QStringLiteral( "VersionChecker/nextDeadline" ),
                       static_cast<qlonglong>( nextDeadline_ ) );
    settings.setValue( QStringLiteral( "VersionChecker/etag" ), etag_ );
    settings.setValue( QStringLiteral( "VersionChecker/cachedReleases" ), cachedReleases_ );
}

VersionChecker::VersionChecker( QObject* parent )
    : QObject( parent )
    , manager_( new QNetworkAccessManager( this ) )
    , releasesUrl_( QString::fromLatin1( ReleasesUrl ) )
{
    initialize();
}

VersionChecker::VersionChecker( const QUrl& testReleasesUrl, QObject* parent )
    : QObject( parent )
    , manager_( new QNetworkAccessManager( this ) )
    , releasesUrl_( testReleasesUrl )
    , testTransport_( true )
{
    initialize();
}

void VersionChecker::initialize()
{
    qRegisterMetaType<ReleaseInfo>( "ReleaseInfo" );
    qRegisterMetaType<PendingUpdate>( "PendingUpdate" );
    qRegisterMetaType<UpdateState>( "UpdateState" );
    manager_->setRedirectPolicy( QNetworkRequest::NoLessSafeRedirectPolicy );
    loadPendingUpdate();
    const QDir cacheRoot( updateRoot() );
    for ( const auto& entry : cacheRoot.entryInfoList( QDir::Dirs | QDir::NoDotAndDotDot ) ) {
        const bool protectsPending
            = pendingUpdate_.isValid()
              && QFileInfo( pendingUpdate_.packagePath )
                     .absoluteFilePath()
                     .startsWith( entry.absoluteFilePath() + QDir::separator() );
        const bool expired = entry.lastModified().daysTo( QDateTime::currentDateTimeUtc() ) > 30;
        if ( expired && !protectsPending ) {
            const bool removed = QDir( entry.absoluteFilePath() ).removeRecursively();
            LOG_INFO << "Updater cache cleanup decision: directory=" << entry.fileName()
                     << ", expired=true, pending=false, removed=" << removed;
        }
        else {
            LOG_DEBUG << "Updater cache cleanup decision: directory=" << entry.fileName()
                      << ", expired=" << expired << ", pending=" << protectsPending
                      << ", removed=false";
        }
    }
    connect( QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
             &VersionChecker::launchPendingUpdate );
    LOG_INFO << "Updater controller initialized with strict TLS and safe redirects";
}

VersionChecker::~VersionChecker()
{
    if ( activeReply_ ) {
        LOG_INFO << "Updater controller is shutting down; aborting active network request";
        disconnect( activeReply_, nullptr, this, nullptr );
        activeReply_->abort();
    }
}

void VersionChecker::setState( UpdateState state, const QString& decision )
{
    if ( state_ == state ) {
        LOG_DEBUG << "Updater remains in state " << updateStateName( state ) << ": " << decision;
        return;
    }
    LOG_INFO << "Updater state transition " << updateStateName( state_ ) << " -> "
             << updateStateName( state ) << ": " << decision;
    state_ = state;
    Q_EMIT stateChanged( state_ );
}

void VersionChecker::startCheck()
{
    const auto& config = Configuration::get();
    if ( !config.versionCheckingEnabled() ) {
        LOG_INFO << "Scheduled update check skipped because version checking is disabled";
        return;
    }
    if ( qEnvironmentVariableIntValue( "CILOGG_AUTOMATION" ) > 0
         || QCoreApplication::arguments().contains( QStringLiteral( "-platform" ) ) ) {
        LOG_INFO << "Scheduled update check skipped in automation/headless mode";
        return;
    }
    const auto& deadline = VersionCheckerConfig::getSynced();
    const bool onStart = config.updateFrequency() == UpdateFrequency::OnStart;
    if ( !onStart && deadline.nextDeadline() >= std::time( nullptr ) ) {
        LOG_DEBUG << "Scheduled update check skipped; deadline is still "
                  << std::difftime( deadline.nextDeadline(), std::time( nullptr ) )
                  << " seconds away";
        return;
    }
    requestReleases( config.updateChannel(), false );
}

void VersionChecker::forceCheck()
{
    forceCheck( Configuration::get().updateChannel() );
}

void VersionChecker::forceCheck( UpdateChannel channel )
{
    LOG_INFO << "Manual update check requested for channel "
             << ( channel == UpdateChannel::Ci ? "CI" : "Stable" );
    requestReleases( channel, true );
}

void VersionChecker::requestReleases( UpdateChannel channel, bool forced )
{
    if ( activeReply_ ) {
        LOG_WARNING << "Update check ignored because another updater request is active";
        if ( forced )
            Q_EMIT checkFinished( false, tr( "An update check is already running." ) );
        return;
    }
    requestedChannel_ = channel;
    forced_ = forced;
    setState( UpdateState::Checking, forced ? QStringLiteral( "manual check" )
                                            : QStringLiteral( "scheduled deadline reached" ) );
    startRequest( releasesUrl_, RequestPurpose::Releases,
                  VersionCheckerConfig::getSynced().etag() );
}

void VersionChecker::startRequest( const QUrl& url, RequestPurpose purpose, const QByteArray& etag )
{
    const bool trusted
        = url.scheme() == QLatin1String( "https" ) && trustedUpdateHost( url.host() );
    const bool localTest = testTransport_
                           && ( url.host() == QLatin1String( "127.0.0.1" )
                                || url.host() == QLatin1String( "localhost" ) );
    if ( !trusted && !localTest ) {
        fail( tr( "The updater refused an untrusted update URL." ) );
        return;
    }
    QNetworkRequest request( url );
    request.setRawHeader( "Accept", "application/vnd.github+json" );
    request.setRawHeader( "X-GitHub-Api-Version", "2022-11-28" );
    request.setRawHeader( "User-Agent", QByteArray( "CILogg/" ) + kloggVersion().toUtf8() );
    request.setAttribute( QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::NoLessSafeRedirectPolicy );
#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
    request.setTransferTimeout( 60000 );
#endif
    if ( !etag.isEmpty() )
        request.setRawHeader( "If-None-Match", etag );
    requestPurpose_ = purpose;
    activeReply_ = manager_->get( request );
    connect( activeReply_, &QNetworkReply::finished, this, &VersionChecker::handleReplyFinished );
    if ( purpose == RequestPurpose::Package ) {
        connect( activeReply_, &QNetworkReply::downloadProgress, this,
                 &VersionChecker::downloadProgress );
        connect( activeReply_, &QIODevice::readyRead, this, [ this ] {
            if ( !activeReply_ || !downloadFile_ )
                return;
            const auto data = activeReply_->readAll();
            packageHash_.addData( data );
            if ( downloadFile_->write( data ) != data.size() ) {
                LOG_ERROR << "Package write failed; aborting download";
                activeReply_->abort();
            }
        } );
    }
    LOG_INFO << "Updater HTTP request started; purpose=" << static_cast<int>( purpose )
             << ", host=" << url.host() << ", path=" << url.path()
             << ", etag-present=" << !etag.isEmpty();
}

void VersionChecker::handleReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>( sender() );
    if ( !reply || reply != activeReply_ ) {
        LOG_WARNING << "Ignoring stale updater network completion";
        return;
    }
    const auto purpose = requestPurpose_;
    activeReply_ = nullptr;
    const auto status = reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt();
    const auto finalUrl = reply->url();
    LOG_INFO << "Updater HTTP completed; purpose=" << static_cast<int>( purpose )
             << ", status=" << status << ", error=" << reply->error()
             << ", redirect-target-host=" << finalUrl.host();
    const bool trustedFinal
        = finalUrl.scheme() == QLatin1String( "https" ) && trustedUpdateHost( finalUrl.host() );
    const bool localTestFinal = testTransport_
                                && ( finalUrl.host() == QLatin1String( "127.0.0.1" )
                                     || finalUrl.host() == QLatin1String( "localhost" ) );
    if ( !trustedFinal && !localTestFinal ) {
        reply->deleteLater();
        fail( tr( "The updater refused an unsafe redirect." ) );
        return;
    }
    if ( purpose == RequestPurpose::Package ) {
        handlePackageReply( *reply );
        reply->deleteLater();
        return;
    }
    QByteArray data;
    if ( status == 304 && purpose == RequestPurpose::Releases ) {
        data = VersionCheckerConfig::getSynced().cachedReleases();
        LOG_INFO << "GitHub returned 304; using cached release-list bytes=" << data.size();
    }
    else if ( reply->error() == QNetworkReply::NoError && status >= 200 && status < 300 ) {
        data = reply->readAll();
    }
    else {
        const auto message
            = status == 403 || status == 429
                  ? tr( "GitHub rate-limited the update check. Please try again later." )
                  : tr( "Update request failed (%1): %2" )
                        .arg( status )
                        .arg( reply->errorString() );
        reply->deleteLater();
        fail( message );
        return;
    }
    if ( purpose == RequestPurpose::Releases )
        handleReleasesReply( *reply, data );
    else
        handleManifestReply( *reply, data );
    reply->deleteLater();
}

bool VersionChecker::isVersionNewer( const QString& currentVersion,
                                     const QString& candidateVersion )
{
    const auto current = QVersionNumber::fromString( normalizedVersion( currentVersion ) );
    const auto candidate = QVersionNumber::fromString( normalizedVersion( candidateVersion ) );
    return !current.isNull() && !candidate.isNull()
           && QVersionNumber::compare( candidate, current ) > 0;
}

std::optional<ReleaseInfo> VersionChecker::selectRelease( const QByteArray& releasesJson,
                                                          UpdateChannel channel,
                                                          QString* errorMessage )
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( releasesJson, &parseError );
    if ( parseError.error != QJsonParseError::NoError || !document.isArray() ) {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Malformed GitHub releases JSON: %1" )
                                .arg( parseError.errorString() );
        return std::nullopt;
    }
    std::optional<ReleaseInfo> selected;
    QDateTime selectedPublished;
    for ( const auto& value : document.array() ) {
        const auto object = value.toObject();
        const auto tag = object.value( QStringLiteral( "tag_name" ) ).toString();
        const bool draft = object.value( QStringLiteral( "draft" ) ).toBool();
        const bool prerelease = object.value( QStringLiteral( "prerelease" ) ).toBool();
        const bool continuous = tag.startsWith( QLatin1String( "continuous-" ) );
        const bool accepted
            = !draft
              && ( channel == UpdateChannel::Ci ? continuous : ( !continuous && !prerelease ) );
        LOG_DEBUG << "Release filter decision: tag=" << tag << ", draft=" << draft
                  << ", prerelease=" << prerelease << ", continuous=" << continuous
                  << ", accepted=" << accepted;
        if ( !accepted )
            continue;
        const auto published = QDateTime::fromString(
            object.value( QStringLiteral( "published_at" ) ).toString(), Qt::ISODate );
        if ( selected && published <= selectedPublished )
            continue;
        ReleaseInfo info;
        info.tag = tag;
        info.version = normalizedVersion( tag );
        info.name = object.value( QStringLiteral( "name" ) ).toString();
        info.body = object.value( QStringLiteral( "body" ) ).toString();
        info.pageUrl = QUrl( object.value( QStringLiteral( "html_url" ) ).toString() );
        info.prerelease = prerelease;
        selected = info;
        selectedPublished = published;
    }
    if ( !selected && errorMessage )
        *errorMessage = QStringLiteral( "No release matches the selected channel" );
    return selected;
}

void VersionChecker::handleReleasesReply( QNetworkReply& reply, const QByteArray& data )
{
    if ( data.isEmpty() ) {
        fail( tr( "GitHub returned no release data." ) );
        return;
    }
    if ( reply.attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt() != 304 ) {
        auto& config = VersionCheckerConfig::get();
        config.setCachedReleases( data );
        config.setEtag( reply.rawHeader( "ETag" ) );
        config.save();
        LOG_INFO << "Stored release-list cache; bytes=" << data.size()
                 << ", etag-present=" << !config.etag().isEmpty();
    }
    QString error;
    const auto release = selectRelease( data, requestedChannel_, &error );
    if ( !release ) {
        fail( error );
        return;
    }
    if ( !isVersionNewer( kloggVersion(), release->version ) ) {
        LOG_INFO << "Update decision: current=" << kloggVersion()
                 << ", candidate=" << release->version << ", result=up-to-date";
        setState( UpdateState::Idle, QStringLiteral( "no newer release" ) );
        if ( forced_ )
            Q_EMIT checkFinished( false, tr( "CILogg is up to date." ) );
        finishScheduledCheck();
        return;
    }
    const auto document = QJsonDocument::fromJson( data );
    githubAssets_.clear();
    ReleaseAsset manifestAsset;
    for ( const auto& value : document.array() ) {
        const auto object = value.toObject();
        if ( object.value( QStringLiteral( "tag_name" ) ).toString() != release->tag )
            continue;
        for ( const auto& assetValue : object.value( QStringLiteral( "assets" ) ).toArray() ) {
            const auto asset = githubAssetFromJson( assetValue.toObject() );
            githubAssets_.append( asset );
            if ( asset.fileName == QLatin1String( ManifestName ) )
                manifestAsset = asset;
        }
        break;
    }
    resolvingRelease_ = *release;
    if ( manifestAsset.fileName.isEmpty() ) {
        resolvingRelease_.automaticUpdateBlockReason
            = tr( "This release has no CILogg update manifest; automatic download is disabled." );
        LOG_WARNING << "Package selection blocked: manifest asset is missing";
        setState( UpdateState::Available, QStringLiteral( "notification-only release" ) );
        Q_EMIT releaseFound( resolvingRelease_ );
        Q_EMIT newVersionFound(
            resolvingRelease_.version, resolvingRelease_.pageUrl.toString(), {},
            resolvingRelease_.body.split( QLatin1Char( '\n' ), Qt::SkipEmptyParts ) );
        if ( forced_ )
            Q_EMIT checkFinished( true, tr( "A new version is available." ) );
        finishScheduledCheck();
        return;
    }
    if ( manifestAsset.githubDigest.size() != 32 ) {
        resolvingRelease_.automaticUpdateBlockReason
            = tr( "GitHub did not provide a SHA-256 digest for the update manifest." );
        LOG_WARNING << "Package selection blocked: manifest GitHub digest is missing";
        setState( UpdateState::Available, QStringLiteral( "unverified manifest" ) );
        Q_EMIT releaseFound( resolvingRelease_ );
        if ( forced_ )
            Q_EMIT checkFinished(
                true, tr( "A new version is available, but cannot be installed automatically." ) );
        finishScheduledCheck();
        return;
    }
    setState( UpdateState::ResolvingPackage, QStringLiteral( "downloading verified manifest" ) );
    requestManifest( resolvingRelease_, manifestAsset );
}

void VersionChecker::requestManifest( const ReleaseInfo& release,
                                      const ReleaseAsset& manifestAsset )
{
    Q_UNUSED( release );
    resolvingRelease_.asset = manifestAsset;
    startRequest( manifestAsset.downloadUrl, RequestPurpose::Manifest );
}

QList<ReleaseAsset> VersionChecker::parseManifest( const QByteArray& manifestJson,
                                                   QString* errorMessage )
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( manifestJson, &parseError );
    if ( parseError.error != QJsonParseError::NoError || !document.isObject()
         || document.object().value( QStringLiteral( "schema" ) ).toInt() != 1 ) {
        if ( errorMessage )
            *errorMessage
                = QStringLiteral( "Invalid updater manifest: %1" ).arg( parseError.errorString() );
        return {};
    }
    QList<ReleaseAsset> assets;
    for ( const auto& value : document.object().value( QStringLiteral( "assets" ) ).toArray() ) {
        const auto object = value.toObject();
        ReleaseAsset asset;
        asset.platform = object.value( QStringLiteral( "platform" ) ).toString();
        asset.architecture = object.value( QStringLiteral( "architecture" ) ).toString();
        asset.installKind
            = installKindFromName( object.value( QStringLiteral( "kind" ) ).toString() );
        asset.variant = object.value( QStringLiteral( "variant" ) ).toString();
        asset.fileName = object.value( QStringLiteral( "filename" ) ).toString();
        asset.size = object.value( QStringLiteral( "size" ) ).toVariant().toLongLong();
        auto hashText = object.value( QStringLiteral( "sha256" ) ).toString();
        if ( hashText.startsWith( QLatin1String( "sha256:" ) ) )
            hashText.remove( 0, 7 );
        asset.sha256 = QByteArray::fromHex( hashText.toLatin1() );
        if ( asset.fileName.isEmpty() || asset.size < 0 || asset.sha256.size() != 32
             || asset.installKind == InstallKind::None ) {
            LOG_WARNING << "Manifest entry rejected as incomplete: filename=" << asset.fileName;
            continue;
        }
        assets.append( asset );
    }
    if ( assets.isEmpty() && errorMessage )
        *errorMessage = QStringLiteral( "Manifest has no valid installable assets" );
    return assets;
}

std::optional<ReleaseAsset> VersionChecker::selectAsset( const QList<ReleaseAsset>& manifestAssets,
                                                         const QList<ReleaseAsset>& githubAssets,
                                                         QString* errorMessage )
{
    const auto platform = currentPlatform();
    const auto architecture = currentArchitecture();
    const auto releaseData = osRelease();
    const auto distroId = releaseData.value( QStringLiteral( "ID" ) ).toLower();
    const auto distroLike = releaseData.value( QStringLiteral( "ID_LIKE" ) ).toLower();
    const auto distroVariant = releaseData.value( QStringLiteral( "VERSION_CODENAME" ) ).toLower();
    const auto distroVersion = releaseData.value( QStringLiteral( "VERSION_ID" ) ).toLower();
    const auto distroVersionVariant = distroId + distroVersion;
    const bool appImage = !qEnvironmentVariable( "APPIMAGE" ).isEmpty();
#if defined( Q_OS_WIN ) || defined( Q_OS_MACOS )
    Q_UNUSED( distroId );
    Q_UNUSED( distroLike );
    Q_UNUSED( distroVariant );
    Q_UNUSED( distroVersion );
    Q_UNUSED( distroVersionVariant );
    Q_UNUSED( appImage );
#endif
    for ( auto manifestAsset : manifestAssets ) {
        bool kindAccepted = false;
#ifdef Q_OS_WIN
        const bool portable = QFileInfo( QCoreApplication::applicationFilePath() )
                                  .completeBaseName()
                                  .contains( QLatin1String( "portable" ), Qt::CaseInsensitive );
        kindAccepted = portable ? manifestAsset.installKind == InstallKind::WindowsPortable
                                : manifestAsset.installKind == InstallKind::WindowsSetup;
#elif defined( Q_OS_MACOS )
        kindAccepted = manifestAsset.installKind == InstallKind::MacBundle;
#else
        if ( appImage )
            kindAccepted = manifestAsset.installKind == InstallKind::AppImage;
        else if ( distroId.contains( QLatin1String( "ubuntu" ) )
                  || distroId.contains( QLatin1String( "debian" ) )
                  || distroLike.contains( QLatin1String( "debian" ) ) ) {
            kindAccepted = manifestAsset.installKind == InstallKind::Deb;
        }
        else {
            kindAccepted = manifestAsset.installKind == InstallKind::Rpm;
        }
#endif
        const bool variantAccepted
            = manifestAsset.variant.isEmpty() || manifestAsset.installKind == InstallKind::AppImage
              || manifestAsset.installKind == InstallKind::WindowsSetup
              || manifestAsset.installKind == InstallKind::WindowsPortable
              || manifestAsset.installKind == InstallKind::MacBundle
              || manifestAsset.variant.compare( distroVariant, Qt::CaseInsensitive ) == 0
              || manifestAsset.variant.compare( distroId, Qt::CaseInsensitive ) == 0
              || manifestAsset.variant.compare( distroVersionVariant, Qt::CaseInsensitive ) == 0;
        const bool candidate = manifestAsset.platform == platform
                               && manifestAsset.architecture == architecture && kindAccepted
                               && variantAccepted;
        LOG_DEBUG << "Package selection decision: file=" << manifestAsset.fileName
                  << ", platform-match=" << ( manifestAsset.platform == platform )
                  << ", arch-match=" << ( manifestAsset.architecture == architecture )
                  << ", kind-match=" << kindAccepted << ", variant-match=" << variantAccepted;
        if ( !candidate )
            continue;
        const auto github = std::find_if( githubAssets.cbegin(), githubAssets.cend(),
                                          [ &manifestAsset ]( const auto& asset ) {
                                              return asset.fileName == manifestAsset.fileName;
                                          } );
        if ( github == githubAssets.cend() )
            continue;
        manifestAsset.downloadUrl = github->downloadUrl;
        manifestAsset.githubDigest = github->githubDigest;
        if ( github->size != manifestAsset.size || github->githubDigest != manifestAsset.sha256 ) {
            LOG_WARNING
                << "Package candidate rejected: GitHub size/digest disagrees with manifest for "
                << manifestAsset.fileName;
            continue;
        }
        return manifestAsset;
    }
    if ( errorMessage )
        *errorMessage = QStringLiteral( "No verified package matches this installation" );
    return std::nullopt;
}

void VersionChecker::handleManifestReply( QNetworkReply& reply, const QByteArray& data )
{
    const auto actual = QCryptographicHash::hash( data, QCryptographicHash::Sha256 );
    if ( actual != resolvingRelease_.asset.githubDigest ) {
        resolvingRelease_.automaticUpdateBlockReason
            = tr( "The update manifest digest did not match GitHub." );
        LOG_ERROR << "Manifest verification failed: GitHub digest mismatch";
        setState( UpdateState::Available, QStringLiteral( "manifest digest mismatch" ) );
        Q_EMIT releaseFound( resolvingRelease_ );
        if ( forced_ )
            Q_EMIT checkFinished( true, resolvingRelease_.automaticUpdateBlockReason );
        finishScheduledCheck();
        return;
    }
    LOG_INFO << "Manifest verification succeeded; bytes=" << data.size();
    QString error;
    const auto assets = parseManifest( data, &error );
    const auto selected = selectAsset( assets, githubAssets_, &error );
    if ( selected ) {
        resolvingRelease_.asset = *selected;
        LOG_INFO << "Selected verified update package " << selected->fileName
                 << ", kind=" << installKindName( selected->installKind )
                 << ", bytes=" << selected->size;
    }
    else {
        resolvingRelease_.asset = {};
        resolvingRelease_.automaticUpdateBlockReason = error;
        LOG_WARNING << "Automatic update unavailable: " << error;
    }
    Q_UNUSED( reply );
    setState( UpdateState::Available, QStringLiteral( "release metadata resolved" ) );
    Q_EMIT releaseFound( resolvingRelease_ );
    Q_EMIT newVersionFound(
        resolvingRelease_.version, resolvingRelease_.pageUrl.toString(),
        resolvingRelease_.asset.downloadUrl.toString(),
        resolvingRelease_.body.split( QLatin1Char( '\n' ), Qt::SkipEmptyParts ) );
    if ( forced_ )
        Q_EMIT checkFinished( true, tr( "A new version is available." ) );
    finishScheduledCheck();
}

QString VersionChecker::updateRoot() const
{
    return QDir( QStandardPaths::writableLocation( QStandardPaths::AppLocalDataLocation ) )
        .filePath( QStringLiteral( "updates" ) );
}

void VersionChecker::downloadUpdate( const ReleaseInfo& release, bool installOnExit )
{
    if ( activeReply_ ) {
        fail( tr( "Another updater operation is already active." ) );
        return;
    }
    if ( !release.canAutomaticallyUpdate() ) {
        fail( release.automaticUpdateBlockReason.isEmpty()
                  ? tr( "This release has no verified package for this installation." )
                  : release.automaticUpdateBlockReason );
        return;
    }
    const auto directory = QDir( updateRoot() ).filePath( release.tag );
    if ( !QDir().mkpath( directory ) ) {
        fail( tr( "Cannot create the update download directory." ) );
        return;
    }
    const auto packagePath = QDir( directory ).filePath( release.asset.fileName );
    downloadFile_ = std::make_unique<QSaveFile>( packagePath );
    if ( !downloadFile_->open( QIODevice::WriteOnly ) ) {
        fail( tr( "Cannot open the update package for writing: %1" )
                  .arg( downloadFile_->errorString() ) );
        downloadFile_.reset();
        return;
    }
    packageHash_.reset();
    resolvingRelease_ = release;
    installOnExit_ = installOnExit;
    setState( UpdateState::Downloading, QStringLiteral( "user confirmed download" ) );
    startRequest( release.asset.downloadUrl, RequestPurpose::Package );
}

void VersionChecker::cancelDownload()
{
    if ( requestPurpose_ != RequestPurpose::Package || !activeReply_ ) {
        LOG_DEBUG << "Cancel request ignored because no package download is active";
        return;
    }
    LOG_INFO << "User cancelled package download";
    activeReply_->abort();
}

void VersionChecker::handlePackageReply( QNetworkReply& reply )
{
    if ( downloadFile_ ) {
        const auto trailing = reply.readAll();
        if ( !trailing.isEmpty() ) {
            packageHash_.addData( trailing );
            downloadFile_->write( trailing );
        }
    }
    if ( reply.error() != QNetworkReply::NoError ) {
        if ( downloadFile_ )
            downloadFile_->cancelWriting();
        downloadFile_.reset();
        fail( reply.error() == QNetworkReply::OperationCanceledError
                  ? tr( "Update download was cancelled." )
                  : tr( "Update download failed: %1" ).arg( reply.errorString() ) );
        return;
    }
    setState( UpdateState::Verifying, QStringLiteral( "download completed" ) );
    const auto received = downloadFile_ ? downloadFile_->size() : -1;
    const auto digest = packageHash_.result();
    if ( received != resolvingRelease_.asset.size || digest != resolvingRelease_.asset.sha256
         || digest != resolvingRelease_.asset.githubDigest ) {
        LOG_ERROR << "Package verification failed: received=" << received
                  << ", expected=" << resolvingRelease_.asset.size
                  << ", manifest-match=" << ( digest == resolvingRelease_.asset.sha256 )
                  << ", github-match=" << ( digest == resolvingRelease_.asset.githubDigest );
        if ( downloadFile_ )
            downloadFile_->cancelWriting();
        downloadFile_.reset();
        fail( tr( "The downloaded package failed size or SHA-256 verification." ) );
        return;
    }
    if ( !downloadFile_->commit() ) {
        const auto error = downloadFile_->errorString();
        downloadFile_.reset();
        fail( tr( "Could not commit the verified package: %1" ).arg( error ) );
        return;
    }
    const auto packagePath = downloadFile_->fileName();
    downloadFile_.reset();
    pendingUpdate_ = {};
    pendingUpdate_.transactionToken = QUuid::createUuid().toString( QUuid::WithoutBraces );
    pendingUpdate_.release = resolvingRelease_;
    pendingUpdate_.packagePath = packagePath;
    pendingUpdate_.installOnExit = installOnExit_;
    QString error;
    if ( !preparePendingUpdate( &error ) || !persistPendingUpdate( &error ) ) {
        fail( error );
        return;
    }
    setState( installOnExit_ ? UpdateState::Scheduled : UpdateState::Ready,
              installOnExit_ ? QStringLiteral( "verified update scheduled for clean exit" )
                             : QStringLiteral( "verified package ready" ) );
    Q_EMIT updateReady( pendingUpdate_ );
}

bool VersionChecker::preparePendingUpdate( QString* errorMessage )
{
    const auto root = QFileInfo( pendingUpdate_.packagePath ).absolutePath();
    pendingUpdate_.acknowledgementPath = QDir( root ).filePath( QStringLiteral( "startup.ack" ) );
    pendingUpdate_.helperLogPath = QDir( root ).filePath( QStringLiteral( "cilogg-updater.log" ) );
    pendingUpdate_.backupPath = QDir( root ).filePath( QStringLiteral( "backup" ) );
    pendingUpdate_.currentPath = QCoreApplication::applicationFilePath();
    switch ( pendingUpdate_.release.asset.installKind ) {
    case InstallKind::WindowsPortable: {
        setState( UpdateState::Staging, QStringLiteral( "extracting portable archive" ) );
        pendingUpdate_.currentPath = QCoreApplication::applicationDirPath();
        pendingUpdate_.stagedPath = QDir( root ).filePath( QStringLiteral( "staged" ) );
        if ( !extractPortableArchive( pendingUpdate_.packagePath, pendingUpdate_.stagedPath,
                                      errorMessage ) )
            return false;
        const auto executableName = QFileInfo( QCoreApplication::applicationFilePath() ).fileName();
        QDirIterator executables( pendingUpdate_.stagedPath, { executableName }, QDir::Files,
                                  QDirIterator::Subdirectories );
        if ( !executables.hasNext() ) {
            if ( errorMessage )
                *errorMessage = tr( "Portable archive does not contain %1." ).arg( executableName );
            return false;
        }
        executables.next();
        pendingUpdate_.stagedPath = executables.fileInfo().absolutePath();
        LOG_INFO << "Portable staging root selected from executable location: "
                 << pendingUpdate_.stagedPath;
        break;
    }
    case InstallKind::MacBundle: {
        setState( UpdateState::Staging,
                  QStringLiteral( "extracting macOS app bundle with ditto" ) );
        pendingUpdate_.currentPath = QDir( QCoreApplication::applicationDirPath() ).absolutePath();
        for ( auto i = 0; i < 2; ++i )
            pendingUpdate_.currentPath = QFileInfo( pendingUpdate_.currentPath ).absolutePath();
        pendingUpdate_.stagedPath = QDir( root ).filePath( QStringLiteral( "staged" ) );
        QDir( pendingUpdate_.stagedPath ).removeRecursively();
        QDir().mkpath( pendingUpdate_.stagedPath );
        const auto exitCode
            = QProcess::execute( QStringLiteral( "/usr/bin/ditto" ),
                                 { QStringLiteral( "-x" ), QStringLiteral( "-k" ),
                                   pendingUpdate_.packagePath, pendingUpdate_.stagedPath } );
        if ( exitCode != 0 ) {
            if ( errorMessage )
                *errorMessage
                    = tr( "ditto failed to stage the macOS update (%1)." ).arg( exitCode );
            return false;
        }
        QDirIterator bundles( pendingUpdate_.stagedPath, { QStringLiteral( "*.app" ) },
                              QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories );
        if ( !bundles.hasNext() ) {
            if ( errorMessage )
                *errorMessage = tr( "The macOS update archive contains no app bundle." );
            return false;
        }
        bundles.next();
        pendingUpdate_.stagedPath = bundles.fileInfo().absoluteFilePath();
        break;
    }
    case InstallKind::AppImage:
        pendingUpdate_.currentPath
            = qEnvironmentVariable( "APPIMAGE", QCoreApplication::applicationFilePath() );
        pendingUpdate_.stagedPath = pendingUpdate_.packagePath;
        break;
    case InstallKind::WindowsSetup:
    case InstallKind::Deb:
    case InstallKind::Rpm:
        pendingUpdate_.stagedPath = pendingUpdate_.packagePath;
        break;
    case InstallKind::None:
        if ( errorMessage )
            *errorMessage = tr( "The update installation kind is invalid." );
        return false;
    }
    if ( pendingUpdate_.release.asset.installKind == InstallKind::WindowsPortable
         || pendingUpdate_.release.asset.installKind == InstallKind::MacBundle
         || pendingUpdate_.release.asset.installKind == InstallKind::AppImage ) {
        pendingUpdate_.backupPath = pendingUpdate_.currentPath + QStringLiteral( ".cilogg-backup-" )
                                    + pendingUpdate_.transactionToken;
    }
    LOG_INFO << "Update staging decision complete; kind="
             << installKindName( pendingUpdate_.release.asset.installKind )
             << ", install-on-exit=" << pendingUpdate_.installOnExit;
    return true;
}

bool VersionChecker::persistPendingUpdate( QString* errorMessage ) const
{
    const auto path = QDir( updateRoot() ).filePath( QStringLiteral( "pending-update.json" ) );
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly )
         || file.write( QJsonDocument( pendingUpdate_.toJson() ).toJson( QJsonDocument::Indented ) )
                < 0
         || !file.commit() ) {
        if ( errorMessage )
            *errorMessage
                = tr( "Cannot persist pending update state: %1" ).arg( file.errorString() );
        return false;
    }
    LOG_INFO << "Pending update state persisted; install-on-exit=" << pendingUpdate_.installOnExit;
    return true;
}

void VersionChecker::loadPendingUpdate()
{
    QFile file( QDir( updateRoot() ).filePath( QStringLiteral( "pending-update.json" ) ) );
    if ( !file.exists() ) {
        LOG_DEBUG << "No persisted pending update exists";
        return;
    }
    if ( !file.open( QIODevice::ReadOnly ) ) {
        LOG_WARNING << "Cannot read persisted pending update: " << file.errorString();
        return;
    }
    QString error;
    pendingUpdate_
        = PendingUpdate::fromJson( QJsonDocument::fromJson( file.readAll() ).object(), &error );
    if ( !pendingUpdate_.isValid() ) {
        LOG_WARNING << "Persisted pending update rejected: " << error;
        pendingUpdate_ = {};
        return;
    }
    if ( !fileMatchesVerifiedAsset( pendingUpdate_.packagePath, pendingUpdate_.release.asset ) ) {
        LOG_ERROR << "Persisted pending update package failed restart-time verification";
        pendingUpdate_ = {};
        QFile::remove( file.fileName() );
        return;
    }
    LOG_INFO << "Persisted pending update package passed restart-time SHA-256 verification";
    setState( pendingUpdate_.installOnExit ? UpdateState::Scheduled : UpdateState::Ready,
              QStringLiteral( "restored persisted pending update" ) );
}

void VersionChecker::launchPendingUpdate()
{
    if ( !pendingUpdate_.isValid() || !pendingUpdate_.installOnExit ) {
        LOG_DEBUG << "Exit-time updater launch skipped: no scheduled verified update";
        return;
    }
    if ( !fileMatchesVerifiedAsset( pendingUpdate_.packagePath, pendingUpdate_.release.asset ) ) {
        LOG_ERROR << "Exit-time install blocked because the pending package no longer matches its "
                     "verified digest";
        setState( UpdateState::Error, QStringLiteral( "pending package was modified" ) );
        return;
    }
    LOG_INFO << "Exit-time package re-verification succeeded";
    const auto helperName = QStringLiteral( "cilogg_updater" )
#ifdef Q_OS_WIN
                            + QStringLiteral( ".exe" )
#endif
        ;
    const auto source = QDir( QCoreApplication::applicationDirPath() ).filePath( helperName );
    const auto helper
        = QDir( QFileInfo( pendingUpdate_.packagePath ).absolutePath() ).filePath( helperName );
    QFile::remove( helper );
    if ( !QFile::copy( source, helper ) ) {
        LOG_ERROR << "Updater helper copy failed; source=" << source << ", destination=" << helper;
        return;
    }
    QFile::setPermissions( helper, QFile::permissions( helper ) | QFileDevice::ExeUser );
    QStringList arguments{
        QStringLiteral( "--wait-pid" ), QString::number( QCoreApplication::applicationPid() ),
        QStringLiteral( "--mode" ),     installKindName( pendingUpdate_.release.asset.installKind ),
        QStringLiteral( "--current" ),  pendingUpdate_.currentPath,
        QStringLiteral( "--staged" ),   pendingUpdate_.stagedPath,
        QStringLiteral( "--backup" ),   pendingUpdate_.backupPath,
        QStringLiteral( "--relaunch" ), QCoreApplication::applicationFilePath(),
        QStringLiteral( "--ack" ),      pendingUpdate_.acknowledgementPath,
        QStringLiteral( "--token" ),    pendingUpdate_.transactionToken,
        QStringLiteral( "--log" ),      pendingUpdate_.helperLogPath
    };
    const bool launched
        = QProcess::startDetached( helper, arguments, QFileInfo( helper ).absolutePath() );
    LOG_INFO << "Exit-time updater helper launch result=" << launched
             << ", kind=" << installKindName( pendingUpdate_.release.asset.installKind );
    if ( launched )
        setState( UpdateState::Installing, QStringLiteral( "helper detached" ) );
}

void VersionChecker::acknowledgeStartup()
{
    const auto arguments = QCoreApplication::arguments();
    const auto ackIndex = arguments.indexOf( QStringLiteral( "--cilogg-update-ack" ) );
    const auto tokenIndex = arguments.indexOf( QStringLiteral( "--cilogg-update-token" ) );
    if ( ackIndex < 0 || tokenIndex < 0 || ackIndex + 1 >= arguments.size()
         || tokenIndex + 1 >= arguments.size() ) {
        LOG_DEBUG << "Startup acknowledgement skipped: no updater transaction arguments";
        return;
    }
    QSaveFile ack( arguments.at( ackIndex + 1 ) );
    if ( !ack.open( QIODevice::WriteOnly )
         || ack.write( arguments.at( tokenIndex + 1 ).toUtf8() ) < 0 || !ack.commit() ) {
        LOG_ERROR << "Failed to acknowledge updated startup: " << ack.errorString();
        return;
    }
    LOG_INFO << "Updated startup acknowledged successfully";
    const auto pendingPath
        = QDir( updateRoot() ).filePath( QStringLiteral( "pending-update.json" ) );
    if ( !QFile::remove( pendingPath ) && QFileInfo::exists( pendingPath ) ) {
        LOG_WARNING << "Startup was acknowledged but pending-update state could not be removed";
    }
    else {
        pendingUpdate_ = {};
        setState( UpdateState::Idle, QStringLiteral( "startup transaction acknowledged" ) );
        LOG_INFO << "Cleared completed pending-update transaction";
    }
}

void VersionChecker::finishScheduledCheck()
{
    if ( !forced_ ) {
        auto& config = VersionCheckerConfig::get();
        const auto deadline = std::time( nullptr )
                              + checkIntervalForFrequency( Configuration::get().updateFrequency() );
        config.setNextDeadline( deadline );
        config.save();
        LOG_INFO << "Scheduled update deadline advanced to " << deadline;
    }
    else {
        LOG_DEBUG << "Manual update check preserved the scheduled deadline";
    }
    forced_ = false;
}

void VersionChecker::fail( const QString& message )
{
    LOG_ERROR << "Updater failure in state " << updateStateName( state_ ) << ": " << message;
    setState( UpdateState::Error, message );
    if ( forced_ )
        Q_EMIT checkFinished( false, message );
    Q_EMIT errorOccurred( message );
    finishScheduledCheck();
}
