#include <catch2/catch.hpp>

#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "versionchecker.h"

namespace {
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
