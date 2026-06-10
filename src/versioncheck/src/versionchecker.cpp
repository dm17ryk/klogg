/*
 * Copyright (C) 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "versionchecker.h"
#include "configuration.h"
#include "log.h"

#include "klogg_version.h"

#include <QSysInfo>

namespace {

#if defined( Q_OS_WIN )
static constexpr QLatin1String OsSuffix = QLatin1String( "-win", 4 );
static constexpr QLatin1String OsAssetPrefix = QLatin1String( "win", 3 );
#elif defined( Q_OS_MACOS )
static constexpr QLatin1String OsSuffix = QLatin1String( "-osx", 4 );
static constexpr QLatin1String OsAssetPrefix = QLatin1String( "macos", 5 );
#else
static constexpr QLatin1String OsSuffix = QLatin1String( "-linux", 6 );
static constexpr QLatin1String OsAssetPrefix = QLatin1String( "linux", 5 );
#endif

static constexpr QLatin1String VERSION_URL
    = QLatin1String( "https://raw.githubusercontent.com/dm17ryk/klogg/master/latest.json", 65 );

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
    return 3600 * 24 * 7;
}

QString currentArchKey()
{
    const auto arch = QSysInfo::currentCpuArchitecture();
    if ( arch.contains( QLatin1String( "arm" ), Qt::CaseInsensitive )
         || arch.contains( QLatin1String( "aarch64" ), Qt::CaseInsensitive ) ) {
        return QStringLiteral( "arm64" );
    }
    return QStringLiteral( "x64" );
}

bool isVersionNewer( const QString& current_version, const QString& new_version )
{
#if ( QT_VERSION >= QT_VERSION_CHECK( 6, 4, 0 ) )
    const auto parseVersion = []( const QString& version_string ) {
        qsizetype tweak_index = 0;
        auto version = QVersionNumber::fromString( QAnyStringView(version_string), &tweak_index );
        return std::make_pair( version, version_string.right( tweak_index + 1 ).toUInt() );
    };
#else
    const auto parseVersion = []( const QString& version_string ) {
        int tweak_index = 0;
        auto version = QVersionNumber::fromString( version_string, &tweak_index );
        return std::make_pair( version, version_string.right( tweak_index + 1 ).toUInt() );
    };
#endif

    const auto old = parseVersion( current_version );
    const auto next = parseVersion( new_version );

    return next > old;
}

} // namespace

void VersionCheckerConfig::retrieveFromStorage( QSettings& settings )
{
    LOG_DEBUG << "VersionCheckerConfig::retrieveFromStorage";

    if ( settings.contains( "VersionChecker/nextDeadline" ) )
        next_deadline_ = settings.value( "VersionChecker/nextDeadline" ).toLongLong();
}

void VersionCheckerConfig::saveToStorage( QSettings& settings ) const
{
    LOG_DEBUG << "VersionCheckerConfig::saveToStorage";

    settings.setValue( "VersionChecker/nextDeadline", static_cast<long long>( next_deadline_ ) );
}

VersionChecker::VersionChecker()
    : QObject()
    , manager_( new QNetworkAccessManager( this ) )
{
    manager_->setRedirectPolicy( QNetworkRequest::NoLessSafeRedirectPolicy );
}

void VersionChecker::startCheck()
{
    LOG_DEBUG << "VersionChecker::startCheck()";

    const auto& deadlineConfig = VersionCheckerConfig::getSynced();
    const auto& appConfig = Configuration::get();

    if ( !appConfig.versionCheckingEnabled() ) {
        return;
    }

    const auto frequency = appConfig.updateFrequency();
    const bool runEveryStart = ( frequency == UpdateFrequency::OnStart );

    if ( runEveryStart || deadlineConfig.nextDeadline() < std::time( nullptr ) ) {
        requestVersionData();
    }
    else {
        LOG_DEBUG << "Deadline not reached yet, next check in "
                  << std::difftime( deadlineConfig.nextDeadline(), std::time( nullptr ) );
    }
}

void VersionChecker::forceCheck()
{
    LOG_DEBUG << "VersionChecker::forceCheck()";
    forced_ = true;
    requestVersionData();
}

void VersionChecker::requestVersionData()
{
    connect( manager_, &QNetworkAccessManager::finished, this,
             &VersionChecker::downloadFinished, Qt::UniqueConnection );

    LOG_DEBUG << "Requesting new version info from " << VERSION_URL;

    QNetworkRequest request;
    request.setUrl( QUrl( VERSION_URL ) );
    manager_->get( request );
}

void VersionChecker::downloadFinished( QNetworkReply* reply )
{
    LOG_DEBUG << "VersionChecker::downloadFinished()";

    if ( reply->error() == QNetworkReply::NoError ) {
        const auto rawReply = reply->readAll();
        checkVersionData( rawReply );
    }
    else {
        LOG_WARNING << "Download failed: err " << reply->error();
    }

    reply->deleteLater();

    // Extend the deadline based on the user's configured frequency.
    const auto frequency = Configuration::get().updateFrequency();
    auto& config = VersionCheckerConfig::get();
    config.setNextDeadline( std::time( nullptr ) + checkIntervalForFrequency( frequency ) );
    config.save();

    forced_ = false;
}

void VersionChecker::checkVersionData( QByteArray versionData )
{
    LOG_DEBUG << "Version reply: " << QString::fromUtf8( versionData );

    const auto latestJson = QJsonDocument::fromJson( versionData );
    const auto latestVersionMap = latestJson.toVariant().toMap();

    const auto channel = Configuration::get().updateChannel();
    const QString channelPrefix
        = ( channel == UpdateChannel::Ci ) ? QStringLiteral( "ci" ) : QStringLiteral( "stable" );

    const QString latestVersion = latestVersionMap.value( channelPrefix ).toString();
    const QString pageUrl = latestVersionMap.value( channelPrefix + "_url" ).toString();

    // Resolve a direct, per-OS asset URL when published; otherwise fall back
    // to the existing channel URL (legacy latest.json without *_assets).
    QString assetUrl;
    const auto assets = latestVersionMap.value( channelPrefix + "_assets" ).toMap();
    if ( !assets.isEmpty() ) {
        const QString assetKey = OsAssetPrefix + QLatin1Char( '-' ) + currentArchKey();
        assetUrl = assets.value( assetKey ).toString();
    }
    if ( assetUrl.isEmpty() ) {
        // Legacy fallback: ci_url historically had no OS suffix, so append it
        // here to preserve behaviour for older latest.json payloads.
        assetUrl = ( channel == UpdateChannel::Ci ) ? ( pageUrl + OsSuffix ) : pageUrl;
    }

    const auto currentVersion = kloggVersion();
    const auto changeLog = latestVersionMap.value( "changelog" ).toList();

    QStringList changes;
    for ( const auto& entry : changeLog ) {
        const auto entryData = entry.toMap();
        const auto version = entryData.value( "version" ).toString();

        if ( isVersionNewer( currentVersion, version ) ) {
            changes
                << QString( "%1: %2" ).arg( version, entryData.value( "description" ).toString() );
        }
    }

    LOG_DEBUG << "Current version: " << currentVersion << ". Latest version is " << latestVersion
              << ", page url " << pageUrl << ", asset url " << assetUrl;
    if ( isVersionNewer( currentVersion, latestVersion ) ) {
        LOG_INFO << "Sending new version notification";

        Q_EMIT newVersionFound( latestVersion, pageUrl, assetUrl, changes );
    }
}