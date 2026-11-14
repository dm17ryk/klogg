#include <catch2/catch.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <system_error>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include "updatearguments.h"
#include "updatefiles.h"
#include "versionchecker.h"

namespace {
constexpr auto PendingName = "pending-update.json";
constexpr auto LaunchedName = "pending-update.launched.json";
constexpr auto ReconcilingName = "pending-update.reconciling.json";
constexpr qint64 HourMs = 60 * 60 * 1000;

std::filesystem::path nativeTestPath( const QString& path )
{
#ifdef Q_OS_WIN
    return std::filesystem::path( QDir::toNativeSeparators( path ).toStdWString() );
#else
    return std::filesystem::path( QFile::encodeName( path ).toStdString() );
#endif
}

// The lock the updater helper holds while it runs.
std::filesystem::path helperLockPath( const QString& root )
{
    return nativeTestPath(
        QDir( root ).filePath( QString::fromLatin1( cilogg::update_protocol::UpdaterLockFileName ) ) );
}

void writeFile( const QString& path, const QByteArray& content )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( content ) == content.size() );
}

// Serves `responses` in order, one per connection; the last response repeats.
void serveSequence( QTcpServer& server, QList<QByteArray> responses,
                    const std::shared_ptr<int>& served )
{
    QObject::connect( &server, &QTcpServer::newConnection, &server,
                      [ &server, responses = std::move( responses ), served ]() {
                          auto* socket = server.nextPendingConnection();
                          const auto index
                              = std::min( *served, static_cast<int>( responses.size() ) - 1 );
                          ++*served;
                          auto send = [ socket, response = responses.at( index ) ] {
                              socket->readAll();
                              socket->write( response );
                              socket->flush();
                              socket->disconnectFromHost();
                          };
                          QObject::connect( socket, &QTcpSocket::readyRead, socket, send );
                          if ( socket->bytesAvailable() )
                              send();
                      } );
}

// A verified windows-setup transaction whose package exists under `root`.
PendingUpdate makeTransaction( const QString& root, const QString& version,
                               const QString& installationPath )
{
    const QDir directory( QDir( root ).filePath( QStringLiteral( "continuous-" ) + version ) );
    QDir().mkpath( directory.path() );
    const QByteArray package = "verified installer bytes for " + version.toLatin1();
    const auto packagePath = directory.filePath( QStringLiteral( "cilogg-setup.exe" ) );
    QFile file( packagePath );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( package ) == package.size() );
    file.close();

    PendingUpdate pending;
    pending.transactionToken = QUuid::createUuid().toString( QUuid::WithoutBraces );
    pending.packagePath = packagePath;
    pending.stagedPath = packagePath;
    pending.currentPath = installationPath;
    pending.acknowledgementPath = directory.filePath( QStringLiteral( "startup.ack" ) );
    pending.helperLogPath = directory.filePath( QStringLiteral( "cilogg-updater.log" ) );
    pending.installOnExit = true;
    pending.release.tag = QStringLiteral( "continuous-" ) + version;
    pending.release.version = version;
    pending.release.asset.platform = QStringLiteral( "windows" );
    pending.release.asset.architecture = QStringLiteral( "x64" );
    pending.release.asset.installKind = InstallKind::WindowsSetup;
    pending.release.asset.fileName = QStringLiteral( "cilogg-setup.exe" );
    pending.release.asset.size = package.size();
    pending.release.asset.sha256 = QCryptographicHash::hash( package, QCryptographicHash::Sha256 );
    pending.release.asset.githubDigest = pending.release.asset.sha256;
    pending.release.asset.downloadUrl = QUrl( "https://github.com/example/cilogg-setup.exe" );
    return pending;
}

void writeTransaction( const QString& path, const PendingUpdate& pending )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( QJsonDocument( pending.toJson() ).toJson() ) > 0 );
}

PendingUpdate readTransaction( const QString& path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return PendingUpdate::fromJson( QJsonDocument::fromJson( file.readAll() ).object() );
}

std::unique_ptr<VersionChecker> makeOfflineChecker( const QString& root,
                                                    const QString& currentVersion )
{
    auto checker
        = std::make_unique<VersionChecker>( QUrl( QStringLiteral( "http://127.0.0.1:9/releases" ) ) );
    checker->setUpdateRootForTesting( root );
    checker->setCurrentVersionForTesting( currentVersion );
    return checker;
}

QString thisInstallation()
{
    return VersionChecker::installationPath( InstallKind::WindowsSetup );
}
void serveResponse( QTcpServer& server, QByteArray response )
{
    QObject::connect( &server, &QTcpServer::newConnection, &server,
                      [ &server, response = std::move( response ) ]() mutable {
                          auto* socket = server.nextPendingConnection();
                          auto send = [ socket, response ] {
                              socket->readAll();
                              socket->write( response );
                              socket->flush();
                              socket->disconnectFromHost();
                          };
                          QObject::connect( socket, &QTcpSocket::readyRead, socket, send );
                          if ( socket->bytesAvailable() )
                              send();
                      } );
}

QByteArray httpResponse( int status, const QByteArray& body )
{
    const auto reason = status == 200 ? QByteArray( "OK" ) : QByteArray( "Too Many Requests" );
    return "HTTP/1.1 " + QByteArray::number( status ) + " " + reason
           + "\r\nContent-Type: application/json\r\nContent-Length: "
           + QByteArray::number( body.size() ) + "\r\nConnection: close\r\n\r\n" + body;
}
} // namespace

TEST_CASE( "Updater compares semantic build versions", "[updater]" )
{
    REQUIRE( VersionChecker::isVersionNewer( "26.04.0.10", "26.04.0.11" ) );
    REQUIRE( VersionChecker::isVersionNewer( "26.04.0", "continuous-26.04.1" ) );
    REQUIRE_FALSE( VersionChecker::isVersionNewer( "26.04.1", "v26.04.1" ) );
    REQUIRE_FALSE( VersionChecker::isVersionNewer( "26.04.2", "26.04.1" ) );
}

TEST_CASE( "Stable and CI channels filter GitHub releases independently", "[updater]" )
{
    const QByteArray releases = R"json([
      {"tag_name":"continuous-27.0.0","draft":false,"prerelease":false,
       "published_at":"2026-07-14T12:00:00Z","html_url":"https://example/ci"},
      {"tag_name":"v26.5.0","draft":false,"prerelease":false,
       "published_at":"2026-07-13T12:00:00Z","html_url":"https://example/stable"},
      {"tag_name":"v99.0.0","draft":true,"prerelease":false,
       "published_at":"2026-07-15T12:00:00Z","html_url":"https://example/draft"}
    ])json";

    QString error;
    const auto stable = VersionChecker::selectRelease( releases, UpdateChannel::Stable, &error );
    REQUIRE( stable );
    REQUIRE( stable->tag == "v26.5.0" );
    const auto ci = VersionChecker::selectRelease( releases, UpdateChannel::Ci, &error );
    REQUIRE( ci );
    REQUIRE( ci->tag == "continuous-27.0.0" );
    REQUIRE_FALSE( ci->prerelease ); // Historical continuous releases were misflagged.
}

TEST_CASE( "Manifest parser rejects malformed hashes and accepts verified entries", "[updater]" )
{
    const auto hash = QString( 64, QLatin1Char( 'a' ) );
    const auto manifest = QString( R"json({"schema":1,"assets":[
      {"platform":"windows","architecture":"x64","kind":"windows-setup",
       "variant":"","filename":"cilogg-setup.exe","size":42,"sha256":"%1"},
      {"platform":"windows","architecture":"x64","kind":"windows-setup",
       "variant":"","filename":"bad.exe","size":42,"sha256":"bad"}
    ]})json" )
                              .arg( hash )
                              .toUtf8();
    QString error;
    const auto assets = VersionChecker::parseManifest( manifest, &error );
    REQUIRE( assets.size() == 1 );
    REQUIRE( assets.front().sha256.size() == 32 );

    auto github = assets.front();
    github.downloadUrl = QUrl( "https://github.com/example/cilogg-setup.exe" );
    github.githubDigest = github.sha256;
    const auto selected = VersionChecker::selectAsset( assets, { github }, &error );
#ifdef Q_OS_WIN
    REQUIRE( selected );
    REQUIRE( selected->isInstallable() );
#else
    REQUIRE_FALSE( selected );
#endif
}

TEST_CASE( "Linux install model is selected only for explicitly supported families", "[updater]" )
{
    REQUIRE( VersionChecker::classifyLinuxInstallKind( "arch", {}, true )
             == InstallKind::AppImage );
    REQUIRE( VersionChecker::classifyLinuxInstallKind( "ubuntu", {}, false ) == InstallKind::Deb );
    REQUIRE( VersionChecker::classifyLinuxInstallKind( "linuxmint", "ubuntu debian", false )
             == InstallKind::Deb );
    REQUIRE( VersionChecker::classifyLinuxInstallKind( "fedora", {}, false ) == InstallKind::Rpm );
    REQUIRE( VersionChecker::classifyLinuxInstallKind( "rocky", "rhel fedora", false )
             == InstallKind::Rpm );

    REQUIRE( VersionChecker::classifyLinuxInstallKind( "arch", {}, false ) == InstallKind::None );
    REQUIRE( VersionChecker::classifyLinuxInstallKind( "alpine", {}, false ) == InstallKind::None );
    REQUIRE(
        VersionChecker::classifyLinuxInstallKind( "opensuse-tumbleweed", "suse opensuse", false )
        == InstallKind::None );
    REQUIRE( VersionChecker::classifyLinuxInstallKind( "notdebian", {}, false )
             == InstallKind::None );
}

TEST_CASE( "Pending update persistence round-trips verified transaction data", "[updater]" )
{
    PendingUpdate pending;
    pending.transactionToken = "transaction-token";
    pending.packagePath = "C:/update/cilogg.exe";
    pending.stagedPath = "C:/update/staged";
    pending.currentPath = "C:/CILogg/cilogg.exe";
    pending.installOnExit = true;
    pending.release.tag = "v27.0.0";
    pending.release.version = "27.0.0";
    pending.release.asset.platform = "windows";
    pending.release.asset.architecture = "x64";
    pending.release.asset.installKind = InstallKind::WindowsSetup;
    pending.release.asset.fileName = "cilogg-setup.exe";
    pending.release.asset.size = 42;
    pending.release.asset.sha256 = QByteArray( 32, 'x' );
    pending.release.asset.githubDigest = pending.release.asset.sha256;
    pending.release.asset.downloadUrl = QUrl( "https://github.com/example/cilogg-setup.exe" );

    QString error;
    const auto restored = PendingUpdate::fromJson( pending.toJson(), &error );
    REQUIRE( error.isEmpty() );
    REQUIRE( restored.isValid() );
    REQUIRE( restored.transactionToken == pending.transactionToken );
    REQUIRE( restored.installOnExit );
}

TEST_CASE( "Portable staging rejects archive traversal and escaping symlinks", "[updater]" )
{
    REQUIRE( VersionChecker::isSafeArchivePath( "release/cilogg_portable.exe" ) );
    REQUIRE_FALSE( VersionChecker::isSafeArchivePath( "../outside.exe" ) );
    REQUIRE_FALSE( VersionChecker::isSafeArchivePath( "release\\..\\outside.exe" ) );
    REQUIRE_FALSE( VersionChecker::isSafeArchivePath( "/absolute/outside.exe" ) );
    REQUIRE_FALSE( VersionChecker::isSafeArchivePath( "release/link", "../../outside" ) );
}

TEST_CASE( "Local update server reports an up-to-date release", "[updater][network]" )
{
    QTcpServer server;
    REQUIRE( server.listen( QHostAddress::LocalHost ) );
    const QByteArray body = R"json([{"tag_name":"v1.0.0","draft":false,"prerelease":false,
      "published_at":"2026-07-14T12:00:00Z","html_url":"https://github.com/dm17ryk/klogg"}])json";
    serveResponse( server, httpResponse( 200, body ) );
    VersionChecker checker(
        QUrl( QStringLiteral( "http://127.0.0.1:%1/releases" ).arg( server.serverPort() ) ) );
    QSignalSpy result( &checker, &VersionChecker::checkFinished );
    checker.forceCheck( UpdateChannel::Stable );
    REQUIRE( result.wait( 3000 ) );
    REQUIRE_FALSE( result.at( 0 ).at( 0 ).toBool() );
    REQUIRE( result.at( 0 ).at( 1 ).toString().contains( "up to date", Qt::CaseInsensitive ) );
}

TEST_CASE( "Local update server rejects malformed JSON", "[updater][network]" )
{
    QTcpServer server;
    REQUIRE( server.listen( QHostAddress::LocalHost ) );
    serveResponse( server, httpResponse( 200, "not-json" ) );
    VersionChecker checker(
        QUrl( QStringLiteral( "http://127.0.0.1:%1/releases" ).arg( server.serverPort() ) ) );
    QSignalSpy result( &checker, &VersionChecker::checkFinished );
    checker.forceCheck( UpdateChannel::Stable );
    REQUIRE( result.wait( 3000 ) );
    REQUIRE( result.at( 0 ).at( 1 ).toString().contains( "Malformed", Qt::CaseInsensitive ) );
}

TEST_CASE( "Local update server surfaces rate limiting", "[updater][network]" )
{
    QTcpServer server;
    REQUIRE( server.listen( QHostAddress::LocalHost ) );
    serveResponse( server, httpResponse( 429, "{}" ) );
    VersionChecker checker(
        QUrl( QStringLiteral( "http://127.0.0.1:%1/releases" ).arg( server.serverPort() ) ) );
    QSignalSpy result( &checker, &VersionChecker::checkFinished );
    checker.forceCheck( UpdateChannel::Stable );
    REQUIRE( result.wait( 3000 ) );
    REQUIRE( result.at( 0 ).at( 1 ).toString().contains( "rate", Qt::CaseInsensitive ) );
}

TEST_CASE( "Persisted updates are classified for the running installation", "[updater]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto installation = QStringLiteral( "/apps/cilogg/cilogg.exe" );
    const auto pending = makeTransaction( root.path(), "26.04.2.1203", installation );
    using Disposition = VersionChecker::PendingDisposition;

    REQUIRE( VersionChecker::classifyPendingUpdate( pending, "26.04.2.1197", installation )
             == Disposition::Applicable );
    REQUIRE( VersionChecker::classifyPendingUpdate( pending, "26.04.2.1203", installation )
             == Disposition::Completed );
    REQUIRE( VersionChecker::classifyPendingUpdate( pending, "26.04.2.1300", installation )
             == Disposition::Completed );
    // A development build or second installation sharing the update directory never consumes
    // the transaction, even when its own version is newer.
    REQUIRE( VersionChecker::classifyPendingUpdate( pending, "26.04.2.1300",
                                                    "/build/RelWithDebInfo/cilogg.exe" )
             == Disposition::ForeignInstallation );
#ifdef Q_OS_WIN
    REQUIRE( VersionChecker::classifyPendingUpdate( pending, "26.04.2.1197",
                                                    "\\APPS\\CILogg\\CILOGG.exe" )
             == Disposition::Applicable );
#endif
    REQUIRE( VersionChecker::classifyPendingUpdate( PendingUpdate{}, "26.04.2.1197", installation )
             == Disposition::Invalid );
}

TEST_CASE( "Only one process can claim a scheduled exit-time update", "[updater]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto pending = makeTransaction( root.path(), "999.0.0", thisInstallation() );
    writeTransaction( QDir( root.path() ).filePath( PendingName ), pending );

    QString error;
    REQUIRE_FALSE( VersionChecker::claimScheduledUpdate( root.path(), "another-token", &error ) );
    REQUIRE( error.contains( "replaced" ) );
    REQUIRE( VersionChecker::claimScheduledUpdate( root.path(), pending.transactionToken ) );
    REQUIRE_FALSE( QFile::exists( QDir( root.path() ).filePath( PendingName ) ) );
    const auto claimed = readTransaction( QDir( root.path() ).filePath( LaunchedName ) );
    REQUIRE( claimed.transactionToken == pending.transactionToken );
    REQUIRE( claimed.launchedAt > 0 );
    REQUIRE_FALSE(
        VersionChecker::claimScheduledUpdate( root.path(), pending.transactionToken, &error ) );
    REQUIRE( error.contains( "gone" ) );
}

TEST_CASE( "Concurrent claims of one scheduled update have exactly one winner", "[updater]" )
{
    constexpr int Claimants = 8;
    for ( int round = 0; round < 10; ++round ) {
        QTemporaryDir root;
        REQUIRE( root.isValid() );
        const auto rootPath = root.path();
        const auto pending = makeTransaction( rootPath, "999.0.0", thisInstallation() );
        writeTransaction( QDir( rootPath ).filePath( PendingName ), pending );
        const auto token = pending.transactionToken;

        std::atomic<bool> go{ false };
        std::atomic<int> winners{ 0 };
        std::vector<std::thread> claimants;
        for ( int i = 0; i < Claimants; ++i ) {
            claimants.emplace_back( [ &go, &winners, &rootPath, &token ] {
                while ( !go.load() )
                    std::this_thread::yield();
                if ( VersionChecker::claimScheduledUpdate( rootPath, token ) )
                    ++winners;
            } );
        }
        go = true;
        for ( auto& claimant : claimants )
            claimant.join();

        REQUIRE( winners.load() == 1 );
        REQUIRE_FALSE( QFile::exists( QDir( rootPath ).filePath( PendingName ) ) );
        REQUIRE( readTransaction( QDir( rootPath ).filePath( LaunchedName ) ).transactionToken
                 == token );
    }
}

TEST_CASE( "A claim never replaces an unreconciled launch record", "[updater]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const QDir rootDir( root.path() );
    const auto running = makeTransaction( root.path(), "998.0.0", thisInstallation() );
    writeTransaction( rootDir.filePath( LaunchedName ), running );
    const auto scheduled = makeTransaction( root.path(), "999.0.0", thisInstallation() );
    writeTransaction( rootDir.filePath( PendingName ), scheduled );

    QString error;
    REQUIRE_FALSE(
        VersionChecker::claimScheduledUpdate( root.path(), scheduled.transactionToken, &error ) );
    REQUIRE( error.contains( "busy" ) );
    REQUIRE( readTransaction( rootDir.filePath( LaunchedName ) ).transactionToken
             == running.transactionToken );
    REQUIRE( readTransaction( rootDir.filePath( PendingName ) ).transactionToken
             == scheduled.transactionToken );
}

TEST_CASE( "Constructing an updater leaves the persisted transaction alone", "[updater]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto pendingPath = QDir( root.path() ).filePath( PendingName );
    const auto pending = makeTransaction( root.path(), "999.0.0", thisInstallation() );
    writeTransaction( pendingPath, pending );

    // CLI and secondary processes (one per MCP tool call) construct an updater too.
    const auto checker = makeOfflineChecker( root.path(), "26.04.2.1197" );
    REQUIRE_FALSE( checker->isActive() );
    REQUIRE( checker->state() == UpdateState::Idle );
    REQUIRE_FALSE( checker->pendingUpdate().isValid() );
    // Nothing holds the transaction open, so the updated CILogg can always clear it.
    REQUIRE( QFile::remove( pendingPath ) );
    writeTransaction( pendingPath, pending );

    checker->activate();
    REQUIRE( checker->isActive() );
    REQUIRE( checker->state() == UpdateState::Scheduled );
    REQUIRE( checker->pendingUpdate().transactionToken == pending.transactionToken );
}

TEST_CASE( "Activation clears a transaction for the version already running", "[updater]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto pendingPath = QDir( root.path() ).filePath( PendingName );
    writeTransaction( pendingPath,
                      makeTransaction( root.path(), "26.04.2.1203", thisInstallation() ) );

    const auto checker = makeOfflineChecker( root.path(), "26.04.2.1203" );
    checker->activate();
    REQUIRE_FALSE( QFile::exists( pendingPath ) );
    REQUIRE( checker->state() == UpdateState::Idle );
    REQUIRE_FALSE( checker->pendingUpdate().isValid() );
}

TEST_CASE( "Activation never touches another installation's transaction", "[updater]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto pendingPath = QDir( root.path() ).filePath( PendingName );
    const auto foreign = makeTransaction( root.path(), "999.0.0",
                                          QStringLiteral( "/other/install/cilogg.exe" ) );
    writeTransaction( pendingPath, foreign );

    const auto checker = makeOfflineChecker( root.path(), "999.1.0" );
    checker->activate();
    REQUIRE( readTransaction( pendingPath ).transactionToken == foreign.transactionToken );
    REQUIRE( checker->state() == UpdateState::Idle );
}

TEST_CASE( "A failed exit-time install is reported once and not re-armed", "[updater]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const QDir rootDir( root.path() );
    const auto launched = makeTransaction( root.path(), "999.0.0", thisInstallation() );
    writeTransaction( rootDir.filePath( LaunchedName ), launched );
    {
        QFile result( QFileInfo( launched.helperLogPath )
                          .dir()
                          .filePath( cilogg::update_protocol::InstallResultFileName ) );
        REQUIRE( result.open( QIODevice::WriteOnly ) );
        result.write( "token=" + launched.transactionToken.toLatin1()
                      + "\nmode=windows-setup\nphase=launch\ncode=1223\n" );
    }

    const auto first = makeOfflineChecker( root.path(), "26.04.2.1197" );
    QSignalSpy failed( first.get(), &VersionChecker::installFailed );
    first->activate();
    REQUIRE( failed.wait( 2000 ) );
    REQUIRE( failed.count() == 1 );
    const auto message = failed.at( 0 ).at( 0 ).toString();
    REQUIRE( message.contains( "999.0.0" ) );
    REQUIRE( message.contains( "declined" ) );
    REQUIRE_FALSE( QFile::exists( rootDir.filePath( LaunchedName ) ) );
    REQUIRE_FALSE( QFile::exists( rootDir.filePath( ReconcilingName ) ) );
    const auto ready = readTransaction( rootDir.filePath( PendingName ) );
    REQUIRE( ready.transactionToken == launched.transactionToken );
    REQUIRE_FALSE( ready.installOnExit );
    REQUIRE( first->state() == UpdateState::Ready );

    const auto second = makeOfflineChecker( root.path(), "26.04.2.1197" );
    QSignalSpy again( second.get(), &VersionChecker::installFailed );
    second->activate();
    REQUIRE_FALSE( again.wait( 200 ) );
    REQUIRE( second->state() == UpdateState::Ready );
}

TEST_CASE( "A completed exit-time install clears its record silently", "[updater]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto launchedPath = QDir( root.path() ).filePath( LaunchedName );
    writeTransaction( launchedPath,
                      makeTransaction( root.path(), "26.04.2.1203", thisInstallation() ) );

    const auto checker = makeOfflineChecker( root.path(), "26.04.2.1203" );
    QSignalSpy failed( checker.get(), &VersionChecker::installFailed );
    checker->activate();
    REQUIRE_FALSE( failed.wait( 200 ) );
    REQUIRE_FALSE( QFile::exists( launchedPath ) );
    REQUIRE( checker->state() == UpdateState::Idle );
}

TEST_CASE( "The helper retries only transient installer launch failures", "[updater]" )
{
    namespace protocol = cilogg::update_protocol;
    REQUIRE( protocol::isTransientLaunchError( protocol::PathNotFoundError ) );
    REQUIRE( protocol::isTransientLaunchError( protocol::FileNotFoundError ) );
    REQUIRE( protocol::isTransientLaunchError( protocol::AccessDeniedError ) );
    REQUIRE( protocol::isTransientLaunchError( protocol::SharingViolationError ) );
    REQUIRE_FALSE( protocol::isTransientLaunchError( protocol::CancelledError ) );
    REQUIRE_FALSE( protocol::isTransientLaunchError( 0 ) );
    REQUIRE( protocol::installerLaunchRetryDelayMs( 1 ) == 1000 );
    REQUIRE( protocol::installerLaunchRetryDelayMs( 2 ) == 2000 );
    REQUIRE( protocol::installerLaunchRetryDelayMs( 4 ) == 8000 );
    REQUIRE( protocol::installerLaunchRetryDelayMs( 9 ) == 8000 );

    const QString launch = protocol::LaunchPhase;
    const QString installer = protocol::InstallerPhase;
    REQUIRE( VersionChecker::describeInstallResult( 1223, launch ).contains( "declined" ) );
    REQUIRE( VersionChecker::describeInstallResult( 3, launch ).contains( "could not start" ) );
    REQUIRE( VersionChecker::describeInstallResult( protocol::StagedPackageMissingExitCode, launch )
                 .contains( "missing" ) );
    // The same numbers returned by a running installer mean something else.
    REQUIRE( VersionChecker::describeInstallResult( 2, installer ).contains( "stopped before" ) );
    REQUIRE( VersionChecker::describeInstallResult( 5, installer ).contains( "exit code 5" ) );
    REQUIRE( VersionChecker::describeInstallResult( protocol::PackageVerificationFailedExitCode,
                                                    launch )
                 .contains( "verification" ) );
    // A failed restart after a successful install is not blamed on the installer.
    const QString relaunch = protocol::RelaunchPhase;
    const auto restartFailure
        = VersionChecker::describeInstallResult( protocol::RelaunchFailedExitCode, relaunch );
    REQUIRE( restartFailure.contains( "installed" ) );
    REQUIRE( restartFailure.contains( "could not be started" ) );
    REQUIRE_FALSE( restartFailure.contains( "installer failed" ) );
}

TEST_CASE( "A freshly claimed exit-time install without a result is kept", "[updater]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto launchedPath = QDir( root.path() ).filePath( LaunchedName );
    auto launched = makeTransaction( root.path(), "999.0.0", thisInstallation() );
    // Claimed moments ago: the helper may still be starting and has no lock yet.
    launched.launchedAt = QDateTime::currentMSecsSinceEpoch();
    writeTransaction( launchedPath, launched );

    const auto checker = makeOfflineChecker( root.path(), "26.04.2.1197" );
    QSignalSpy failed( checker.get(), &VersionChecker::installFailed );
    checker->activate();
    REQUIRE_FALSE( failed.wait( 200 ) );
    REQUIRE( readTransaction( launchedPath ).transactionToken == launched.transactionToken );
    REQUIRE_FALSE( QFile::exists( QDir( root.path() ).filePath( PendingName ) ) );
    REQUIRE( checker->state() == UpdateState::Idle );
}

TEST_CASE( "A running helper keeps its install unreported until it ends", "[updater]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const QDir rootDir( root.path() );
    auto launched = makeTransaction( root.path(), "999.0.0", thisInstallation() );
    // An installer or UAC prompt can wait far longer than any fixed timeout.
    launched.launchedAt = QDateTime::currentMSecsSinceEpoch() - HourMs;
    writeTransaction( rootDir.filePath( LaunchedName ), launched );
    cilogg::update_files::ExclusiveFileLock helperLock;
    REQUIRE( helperLock.acquire( helperLockPath( root.path() ), 4242 ) );
    {
        const auto whileRunning = makeOfflineChecker( root.path(), "26.04.2.1197" );
        QSignalSpy failed( whileRunning.get(), &VersionChecker::installFailed );
        whileRunning->activate();
        REQUIRE_FALSE( failed.wait( 200 ) );
        REQUIRE( readTransaction( rootDir.filePath( LaunchedName ) ).transactionToken
                 == launched.transactionToken );
        REQUIRE_FALSE( QFile::exists( rootDir.filePath( PendingName ) ) );
        REQUIRE( QFileInfo::exists( launched.packagePath ) );
    }

    // The helper ended without a result: the abandoned claim is reported exactly once and the
    // verified package stays ready, not re-armed.
    helperLock.release();
    const auto afterwards = makeOfflineChecker( root.path(), "26.04.2.1197" );
    QSignalSpy reported( afterwards.get(), &VersionChecker::installFailed );
    afterwards->activate();
    REQUIRE( reported.wait( 2000 ) );
    REQUIRE( reported.count() == 1 );
    REQUIRE( reported.at( 0 ).at( 0 ).toString().contains( "did not start or finish" ) );
    REQUIRE_FALSE( QFile::exists( rootDir.filePath( LaunchedName ) ) );
    REQUIRE_FALSE( QFile::exists( rootDir.filePath( ReconcilingName ) ) );
    const auto ready = readTransaction( rootDir.filePath( PendingName ) );
    REQUIRE( ready.transactionToken == launched.transactionToken );
    REQUIRE_FALSE( ready.installOnExit );
    REQUIRE( afterwards->state() == UpdateState::Ready );

    const auto later = makeOfflineChecker( root.path(), "26.04.2.1197" );
    QSignalSpy repeated( later.get(), &VersionChecker::installFailed );
    later->activate();
    REQUIRE_FALSE( repeated.wait( 200 ) );
    REQUIRE( later->state() == UpdateState::Ready );
}

TEST_CASE( "An interrupted reconciliation is recovered without a report", "[updater]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const QDir rootDir( root.path() );
    const auto interrupted = makeTransaction( root.path(), "999.0.0", thisInstallation() );
    writeTransaction( rootDir.filePath( ReconcilingName ), interrupted );

    const auto checker = makeOfflineChecker( root.path(), "26.04.2.1197" );
    QSignalSpy failed( checker.get(), &VersionChecker::installFailed );
    checker->activate();
    REQUIRE_FALSE( failed.wait( 200 ) );
    REQUIRE_FALSE( QFile::exists( rootDir.filePath( ReconcilingName ) ) );
    const auto ready = readTransaction( rootDir.filePath( PendingName ) );
    REQUIRE( ready.transactionToken == interrupted.transactionToken );
    REQUIRE_FALSE( ready.installOnExit );
    REQUIRE( checker->state() == UpdateState::Ready );
}

TEST_CASE( "An interrupted reconciliation never replaces a pending update", "[updater]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const QDir rootDir( root.path() );
    writeTransaction( rootDir.filePath( ReconcilingName ),
                      makeTransaction( root.path(), "998.0.0", thisInstallation() ) );
    const auto newer = makeTransaction( root.path(), "999.0.0", thisInstallation() );
    writeTransaction( rootDir.filePath( PendingName ), newer );

    const auto checker = makeOfflineChecker( root.path(), "26.04.2.1197" );
    checker->activate();
    REQUIRE_FALSE( QFile::exists( rootDir.filePath( ReconcilingName ) ) );
    const auto pending = readTransaction( rootDir.filePath( PendingName ) );
    REQUIRE( pending.transactionToken == newer.transactionToken );
    REQUIRE( pending.installOnExit );
    REQUIRE( checker->state() == UpdateState::Scheduled );
}

TEST_CASE( "Cache cleanup keeps every package a persisted record references", "[updater]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const QDir rootDir( root.path() );
    // Another installation's waiting update, and an install of this one that is still young.
    const auto foreign = makeTransaction( root.path(), "999.0.0",
                                          QStringLiteral( "/other/install/cilogg.exe" ) );
    writeTransaction( rootDir.filePath( PendingName ), foreign );
    auto running = makeTransaction( root.path(), "998.0.0", thisInstallation() );
    running.launchedAt = QDateTime::currentMSecsSinceEpoch();
    writeTransaction( rootDir.filePath( LaunchedName ), running );
    const auto unreferenced = rootDir.filePath( QStringLiteral( "continuous-1.0.0" ) );
    REQUIRE( QDir().mkpath( unreferenced ) );

    const auto longAgo
        = std::filesystem::file_time_type::clock::now() - std::chrono::hours( 24 * 40 );
    const QStringList directories{ QStringLiteral( "continuous-999.0.0" ),
                                   QStringLiteral( "continuous-998.0.0" ),
                                   QStringLiteral( "continuous-1.0.0" ) };
    for ( const auto& directory : directories ) {
        std::error_code error;
        std::filesystem::last_write_time( nativeTestPath( rootDir.filePath( directory ) ),
                                          longAgo, error );
        REQUIRE( error.value() == 0 );
    }

    const auto checker = makeOfflineChecker( root.path(), "26.04.2.1197" );
    checker->activate();
    REQUIRE( QFileInfo::exists( foreign.packagePath ) );
    REQUIRE( QFileInfo::exists( running.packagePath ) );
    REQUIRE_FALSE( QFileInfo::exists( unreferenced ) );
}

TEST_CASE( "A pending update of another existing installation is never replaced", "[updater]" )
{
    QTemporaryDir root;
    QTemporaryDir otherInstallation;
    REQUIRE( root.isValid() );
    REQUIRE( otherInstallation.isValid() );
    const auto otherExecutable
        = QDir( otherInstallation.path() ).filePath( QStringLiteral( "cilogg.exe" ) );
    writeFile( otherExecutable, "other installation" );

    const auto foreign = makeTransaction( root.path(), "999.0.0", otherExecutable );
    REQUIRE_FALSE( VersionChecker::mayReplacePendingUpdate( foreign, thisInstallation() ) );
    REQUIRE( VersionChecker::mayReplacePendingUpdate( foreign, otherExecutable ) );
    // A record of a removed installation would otherwise block every other one forever.
    const auto orphaned = makeTransaction(
        root.path(), "998.0.0",
        QDir( otherInstallation.path() ).filePath( QStringLiteral( "removed/cilogg.exe" ) ) );
    REQUIRE( VersionChecker::mayReplacePendingUpdate( orphaned, thisInstallation() ) );
    REQUIRE( VersionChecker::mayReplacePendingUpdate( PendingUpdate{}, thisInstallation() ) );
}

TEST_CASE( "Exit-time installs run only verified packages from the update directory",
           "[updater]" )
{
    QTemporaryDir root;
    QTemporaryDir elsewhere;
    REQUIRE( root.isValid() );
    REQUIRE( elsewhere.isValid() );
    const auto pending = makeTransaction( root.path(), "999.0.0", thisInstallation() );
    QString error;
    REQUIRE( VersionChecker::validateLaunchPaths( pending, root.path(), &error ) );

    // A system installer runs elevated: it must be the verified package itself.
    auto swapped = pending;
    swapped.stagedPath = makeTransaction( root.path(), "998.0.0", thisInstallation() ).packagePath;
    REQUIRE_FALSE( VersionChecker::validateLaunchPaths( swapped, root.path(), &error ) );
    REQUIRE( error.contains( "verified package" ) );

    // Nothing outside the update directory, whatever the install kind.
    const auto outside = makeTransaction( elsewhere.path(), "999.0.0", thisInstallation() );
    auto escaped = pending;
    escaped.stagedPath = outside.packagePath;
    REQUIRE_FALSE( VersionChecker::validateLaunchPaths( escaped, root.path(), &error ) );
    REQUIRE( error.contains( "outside" ) );
    REQUIRE_FALSE( VersionChecker::validateLaunchPaths( outside, root.path(), &error ) );

    auto portable = pending;
    portable.release.asset.installKind = InstallKind::WindowsPortable;
    portable.stagedPath = QFileInfo( pending.packagePath ).absolutePath();
    REQUIRE( VersionChecker::validateLaunchPaths( portable, root.path(), &error ) );
    portable.stagedPath = elsewhere.path();
    REQUIRE_FALSE( VersionChecker::validateLaunchPaths( portable, root.path(), &error ) );
}

TEST_CASE( "Headless Qt platforms keep the updater passive", "[updater]" )
{
    REQUIRE( VersionChecker::isHeadlessPlatform( QStringLiteral( "offscreen" ) ) );
    REQUIRE( VersionChecker::isHeadlessPlatform( QStringLiteral( "minimal" ) ) );
    REQUIRE_FALSE( VersionChecker::isHeadlessPlatform( QStringLiteral( "windows" ) ) );
    REQUIRE_FALSE( VersionChecker::isHeadlessPlatform( QStringLiteral( "xcb" ) ) );
    REQUIRE_FALSE( VersionChecker::isHeadlessPlatform( QStringLiteral( "cocoa" ) ) );
    REQUIRE_FALSE( VersionChecker::isHeadlessPlatform( QString{} ) );

    // QApplication consumes "-platform offscreen"; the active platform still tells.
    const auto platform = QCoreApplication::instance()->property( "platformName" ).toString();
    if ( !VersionChecker::isHeadlessPlatform( platform ) ) {
        WARN( "Not running on a headless Qt platform; production activation is not exercised" );
        return;
    }
    VersionChecker production;
    production.activate();
    REQUIRE_FALSE( production.isActive() );
}

TEST_CASE( "Update file primitives hash, rename, lock and verify like the helper",
           "[updater]" )
{
    namespace files = cilogg::update_files;
    const std::vector<int> sizes{ 0, 1, 55, 56, 63, 64, 65, 1000, 1024 * 1024 + 7 };
    for ( const auto size : sizes ) {
        QByteArray data( size, Qt::Uninitialized );
        for ( int i = 0; i < size; ++i )
            data[ i ] = static_cast<char>( ( i * 31 + 7 ) & 0xff );
        files::Sha256 hash;
        hash.update( reinterpret_cast<const unsigned char*>( data.constData() ),
                     static_cast<std::size_t>( data.size() ) );
        REQUIRE( QByteArray::fromStdString( files::toHex( hash.finish() ) )
                 == QCryptographicHash::hash( data, QCryptographicHash::Sha256 ).toHex() );
    }

    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const QDir rootDir( root.path() );
    const auto at = [ &rootDir ]( const char* name ) {
        return nativeTestPath( rootDir.filePath( QString::fromLatin1( name ) ) );
    };
    writeFile( rootDir.filePath( QStringLiteral( "a" ) ), "abc" );
    writeFile( rootDir.filePath( QStringLiteral( "b" ) ), "other" );
    REQUIRE( files::renameNoReplace( at( "a" ), at( "b" ) )
             == files::RenameResult::DestinationExists );
    REQUIRE( files::renameNoReplace( at( "missing" ), at( "c" ) )
             == files::RenameResult::SourceMissing );
    REQUIRE( files::renameNoReplace( at( "a" ), at( "c" ) ) == files::RenameResult::Renamed );
    REQUIRE_FALSE( QFile::exists( rootDir.filePath( QStringLiteral( "a" ) ) ) );

    const auto lockPath = at( "updater.lock" );
    REQUIRE_FALSE( files::isLockHeld( lockPath ) );
    {
        files::ExclusiveFileLock lock;
        REQUIRE( lock.acquire( lockPath, 77 ) );
        REQUIRE( files::isLockHeld( lockPath ) );
        files::ExclusiveFileLock second;
        REQUIRE_FALSE( second.acquire( lockPath, 78 ) );
    }
    REQUIRE_FALSE( files::isLockHeld( lockPath ) );

    const auto digest
        = QCryptographicHash::hash( QByteArray( "abc" ), QCryptographicHash::Sha256 ).toHex();
    files::VerifiedFile verified;
    REQUIRE( verified.open( at( "c" ), digest.toUpper().toStdString() )
             == files::VerifiedFile::Result::Verified );
    REQUIRE( verified.isOpen() );
#ifdef Q_OS_WIN
    // While it is verified, nobody can change, rename or delete the package.
    QFile writer( rootDir.filePath( QStringLiteral( "c" ) ) );
    REQUIRE_FALSE( writer.open( QIODevice::Append ) );
    REQUIRE( files::renameNoReplace( at( "c" ), at( "d" ) ) != files::RenameResult::Renamed );
#endif
    verified.close();
    REQUIRE( verified.open( at( "b" ), digest.toStdString() )
             == files::VerifiedFile::Result::Mismatch );
    REQUIRE_FALSE( verified.isOpen() );
    REQUIRE( verified.open( at( "missing" ), digest.toStdString() )
             == files::VerifiedFile::Result::Unavailable );
}

TEST_CASE( "A failed background check is silent and retried", "[updater][network]" )
{
    QTcpServer server;
    REQUIRE( server.listen( QHostAddress::LocalHost ) );
    const QByteArray body = R"json([{"tag_name":"v1.0.0","draft":false,"prerelease":false,
      "published_at":"2026-07-14T12:00:00Z","html_url":"https://github.com/dm17ryk/klogg"}])json";
    const auto served = std::make_shared<int>( 0 );
    serveSequence( server, { httpResponse( 503, "{}" ), httpResponse( 200, body ) }, served );
    VersionChecker checker(
        QUrl( QStringLiteral( "http://127.0.0.1:%1/releases" ).arg( server.serverPort() ) ) );
    checker.setBackgroundRetryDelaysForTesting( { 10, 10 } );
    QSignalSpy errors( &checker, &VersionChecker::errorOccurred );
    QSignalSpy backgroundFailures( &checker, &VersionChecker::backgroundCheckFailed );
    QSignalSpy finished( &checker, &VersionChecker::checkFinished );

    checker.startBackgroundCheck( UpdateChannel::Stable );
    REQUIRE( QTest::qWaitFor(
        [ & ] { return *served >= 2 && checker.state() == UpdateState::Idle; }, 5000 ) );
    REQUIRE( errors.isEmpty() );
    REQUIRE( finished.isEmpty() );
    REQUIRE( backgroundFailures.count() == 1 );
    REQUIRE( backgroundFailures.at( 0 ).at( 1 ).toBool() );
}

TEST_CASE( "A rate-limited background check is silent and not retried", "[updater][network]" )
{
    QTcpServer server;
    REQUIRE( server.listen( QHostAddress::LocalHost ) );
    const auto served = std::make_shared<int>( 0 );
    serveSequence( server, { httpResponse( 429, "{}" ) }, served );
    VersionChecker checker(
        QUrl( QStringLiteral( "http://127.0.0.1:%1/releases" ).arg( server.serverPort() ) ) );
    checker.setBackgroundRetryDelaysForTesting( { 10 } );
    QSignalSpy errors( &checker, &VersionChecker::errorOccurred );
    QSignalSpy backgroundFailures( &checker, &VersionChecker::backgroundCheckFailed );

    checker.startBackgroundCheck( UpdateChannel::Stable );
    REQUIRE( backgroundFailures.wait( 3000 ) );
    REQUIRE_FALSE( backgroundFailures.at( 0 ).at( 1 ).toBool() );
    QTest::qWait( 200 );
    REQUIRE( *served == 1 );
    REQUIRE( errors.isEmpty() );
}

TEST_CASE( "A failed manual check is reported to the user", "[updater][network]" )
{
    QTcpServer server;
    REQUIRE( server.listen( QHostAddress::LocalHost ) );
    const auto served = std::make_shared<int>( 0 );
    serveSequence( server, { httpResponse( 503, "{}" ) }, served );
    VersionChecker checker(
        QUrl( QStringLiteral( "http://127.0.0.1:%1/releases" ).arg( server.serverPort() ) ) );
    QSignalSpy errors( &checker, &VersionChecker::errorOccurred );
    QSignalSpy backgroundFailures( &checker, &VersionChecker::backgroundCheckFailed );
    QSignalSpy finished( &checker, &VersionChecker::checkFinished );

    checker.forceCheck( UpdateChannel::Stable );
    REQUIRE( finished.wait( 3000 ) );
    REQUIRE_FALSE( finished.at( 0 ).at( 0 ).toBool() );
    REQUIRE( errors.count() == 1 );
    REQUIRE( backgroundFailures.isEmpty() );
}
