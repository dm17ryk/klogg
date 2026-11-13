#include <catch2/catch.hpp>

#include <algorithm>
#include <memory>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include "updatearguments.h"
#include "versionchecker.h"

namespace {
constexpr auto PendingName = "pending-update.json";
constexpr auto LaunchedName = "pending-update.launched.json";

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
    REQUIRE( readTransaction( QDir( root.path() ).filePath( LaunchedName ) ).transactionToken
             == pending.transactionToken );
    REQUIRE_FALSE(
        VersionChecker::claimScheduledUpdate( root.path(), pending.transactionToken, &error ) );
    REQUIRE( error.contains( "gone" ) );
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
}

TEST_CASE( "An exit-time install that may still be running is not reported", "[updater]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto launchedPath = QDir( root.path() ).filePath( LaunchedName );
    const auto launched = makeTransaction( root.path(), "999.0.0", thisInstallation() );
    writeTransaction( launchedPath, launched );
    {
        // The helper has started (fresh log) but has not written its result yet.
        QFile helperLog( launched.helperLogPath );
        REQUIRE( helperLog.open( QIODevice::WriteOnly ) );
        helperLog.write( "Updater helper started; mode=windows-setup\n" );
    }

    const auto checker = makeOfflineChecker( root.path(), "26.04.2.1197" );
    QSignalSpy failed( checker.get(), &VersionChecker::installFailed );
    checker->activate();
    REQUIRE_FALSE( failed.wait( 200 ) );
    REQUIRE( readTransaction( launchedPath ).transactionToken == launched.transactionToken );
    REQUIRE_FALSE( QFile::exists( QDir( root.path() ).filePath( PendingName ) ) );
    REQUIRE( checker->state() == UpdateState::Idle );
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
