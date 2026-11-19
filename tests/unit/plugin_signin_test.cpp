// E15b: Azure DevOps sign-in of plugin repositories in PluginManager (docs/plugin-repositories.md
// sections 3.5, 3.6, 3.8 and 8). dev.azure.com and downloads.example.test are answered by a local
// HTTPS server; `git credential` is tests/helpers/fake_git.cpp.
#include "../helpers/fake_git_environment.h"
#include "../helpers/https_catalog_server.h"
#include "cli.h"
#include "mcp/mcp.h"
#include "plugincatalog.h"
#include "pluginmanager.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <catch2/catch.hpp>

using namespace cilogg::plugins;
using testgit::FakeGit;

namespace {
const QString RepositoryAddress = QStringLiteral( "https://dev.azure.com/contoso/Tools/_git/catalog" );
const QByteArray CatalogPath = "/contoso/Tools/_apis/git/repositories/catalog/items";

bool waitUntil( const std::function<bool()>& predicate, int timeout = 20000 )
{
    QElapsedTimer timer;
    timer.start();
    while ( !predicate() && timer.elapsed() < timeout ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 2 );
    }
    return predicate();
}

QVariantMap finishedJob( PluginManager& manager, const Result& started )
{
    INFO( started.error.toStdString() );
    REQUIRE( started.ok() );
    const auto id = started.payload.value( "jobId" ).toString();
    REQUIRE( waitUntil( [ & ] {
        const auto state = manager.job( id ).payload.value( "state" ).toString();
        return state == "completed" || state == "failed" || state == "cancelled";
    } ) );
    return manager.job( id ).payload;
}

QByteArray emptyCatalog()
{
    return QJsonDocument( QJsonObject{ { "schema_version", 1 }, { "plugins", QJsonArray{} } } )
        .toJson();
}

QByteArray serialized( const QVariant& value )
{
    return QJsonDocument::fromVariant( value ).toJson( QJsonDocument::Compact );
}

// The requests whose target (path and query) starts with `path`.
QList<testhttps::RecordedRequest> requestsTo( const testhttps::CatalogServer& server,
                                              const QByteArray& path )
{
    QList<testhttps::RecordedRequest> found;
    for ( const auto& request : server.requests ) {
        if ( request.target.startsWith( path ) ) {
            found.append( request );
        }
    }
    return found;
}

#ifdef CILOGG_TEST_HTTPS_SERVER
struct SignInFixture {
    QTemporaryDir temp;
    testhttps::CatalogServer server;
    testhttps::TrustingNetwork network;
    QString data;
    MemoryPatStore* tokens = nullptr;
    std::unique_ptr<PluginManager> manager;

    SignInFixture()
    {
        data = QDir( temp.path() ).filePath( "data" );
        REQUIRE( QDir().mkpath( data ) );
        {
            QSettings settings( QDir( data ).filePath( "settings.ini" ), QSettings::IniFormat );
            settings.setValue( "gitExecutable", FakeGit::path() );
        }
        network.hostToLocalPort.insert( "dev.azure.com", server.port() );
        network.hostToLocalPort.insert( "downloads.example.test", server.port() );
        manager = std::make_unique<PluginManager>( data, QDir( temp.path() ).filePath( "none" ),
                                                   nullptr, &network );
        auto store = std::make_unique<MemoryPatStore>();
        tokens = store.get();
        manager->setPatStore( std::move( store ) );
    }

    QString add( const QVariantMap& extra = {} )
    {
        QVariantMap options{ { "name", "Team" },
                             { "url", RepositoryAddress },
                             { "sign_in", "azure-devops" } };
        for ( auto it = extra.cbegin(); it != extra.cend(); ++it ) {
            options.insert( it.key(), it.value() );
        }
        const auto added = manager->manage( "repository_add", {}, options );
        INFO( added.error.toStdString() );
        REQUIRE( added.ok() );
        return added.payload.value( "repository" ).toMap().value( "id" ).toString();
    }

    QVariantMap repository( const QString& id ) const
    {
        for ( const auto& value : manager->repositories() ) {
            if ( value.toMap().value( "id" ) == id ) {
                return value.toMap();
            }
        }
        return {};
    }

    QVariantMap fetch( const QString& id, const QString& origin )
    {
        return finishedJob( *manager,
                            manager->manage( "catalog", {}, { { "repository", id } }, origin ) );
    }

    QByteArray settingsText() const
    {
        QFile file( QDir( data ).filePath( "settings.ini" ) );
        REQUIRE( file.open( QIODevice::ReadOnly ) );
        return file.readAll();
    }
};
#endif
} // namespace

TEST_CASE( "Azure DevOps repositories are added from every address form",
           "[plugins][workbench][E15]" )
{
    QTemporaryDir temp;
    PluginManager manager( QDir( temp.path() ).filePath( "data" ),
                           QDir( temp.path() ).filePath( "none" ) );
    manager.setPatStore( std::make_unique<MemoryPatStore>() );
    const auto add = [ & ]( const QVariantMap& options ) {
        return manager.manage( "repository_add", {}, options );
    };
    const auto added = add( { { "name", "Team" },
                              { "url", "https://dev.azure.com/contoso/Tools/_git/catalog"
                                       "?path=/release/plugins.json&version=GBpublished" },
                              { "sign_in", "azure-devops" } } );
    INFO( added.error.toStdString() );
    REQUIRE( added.ok() );
    const auto record = added.payload.value( "repository" ).toMap();
    CHECK( record.value( "url" ) == RepositoryAddress );
    CHECK( record.value( "branch" ) == "published" );
    CHECK( record.value( "path" ) == "/release/plugins.json" );
    CHECK( record.value( "signIn" ) == "azure-devops" );
    CHECK( record.value( "credentialSource" ) == "git-credential-manager" );
    CHECK( record.value( "signedIn" ) == false );
    CHECK( record.value( "state" ) == "not checked" );

    // The same catalog, whatever the case of its names, is refused.
    CHECK( add( { { "name", "Again" },
                  { "url", "https://CONTOSO.visualstudio.com/tools/_git/CATALOG" },
                  { "branch", "PUBLISHED" },
                  { "path", "/release/PLUGINS.json" },
                  { "sign_in", "azure-devops" } } )
               .error.contains( "already has this address" ) );
    // Another catalog file of the same repository is another repository.
    const auto other = add( { { "name", "Other" },
                              { "url", "https://contoso.visualstudio.com/Tools/_git/catalog" },
                              { "sign_in", "azure-devops" },
                              { "credential_source", "personal-access-token" } } );
    REQUIRE( other.ok() );
    CHECK( other.payload.value( "repository" ).toMap().value( "url" ) == RepositoryAddress );
    CHECK( other.payload.value( "repository" ).toMap().value( "credentialSource" )
           == "personal-access-token" );

    CHECK( add( { { "name", "Bad" },
                  { "url", "https://dev.azure.com/contoso/Tools" },
                  { "sign_in", "azure-devops" } } )
               .error.contains( "Azure DevOps Git repository" ) );
    CHECK( add( { { "name", "Secret" },
                  { "url", "https://me:pat@dev.azure.com/contoso/Tools/_git/x" },
                  { "sign_in", "azure-devops" } } )
               .error.contains( "password" ) );
    CHECK( add( { { "name", "Source" },
                  { "url", "https://dev.azure.com/contoso/Tools/_git/y" },
                  { "sign_in", "azure-devops" },
                  { "credential_source", "browser" } } )
               .error.contains( "credential_source" ) );

    const auto id = record.value( "id" ).toString();
    const auto changed = manager.manage(
        "repository_update", {},
        { { "repository", id }, { "credential_source", "personal-access-token" } } );
    REQUIRE( changed.ok() );
    CHECK( changed.payload.value( "repository" ).toMap().value( "credentialSource" )
           == "personal-access-token" );
    CHECK( manager
               .manage( "repository_update", {},
                        { { "repository", id }, { "branch", "main" } } )
               .error.contains( "cannot be changed" ) );
    CHECK( manager
               .manage( "repository_update", {},
                        { { "repository", "default" }, { "credential_source", "personal-access-token" } } )
               .error.contains( "only to Azure DevOps" ) );
    CHECK( manager.manage( "repository_update", {}, { { "repository", id }, { "token", "x" } } )
               .error.contains( "only to repository_sign_in" ) );
    CHECK( manager
               .manage( "repository_sign_in", {}, { { "repository", "default" } },
                        RequestOriginGui )
               .error.contains( "only to Azure DevOps" ) );
}

TEST_CASE( "A catalog fetch signs in through the Git credential manager",
           "[plugins][workbench][E15][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    SignInFixture fixture;
    if ( !fixture.server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    FakeGit git( "bearer", "gcm-token" );
    fixture.server.route( CatalogPath, { 200, emptyCatalog() } );
    const auto id = fixture.add();

    const auto job = fixture.fetch( id, "mcp" );
    INFO( serialized( job ).toStdString() );
    REQUIRE( job.value( "state" ) == "completed" );
    const auto requests = requestsTo( fixture.server, CatalogPath );
    REQUIRE( requests.size() == 1 );
    CHECK( requests.at( 0 ).target.contains( "path=%2Fplugins.json" ) );
    CHECK( requests.at( 0 ).target.contains( "versionDescriptor.version=catalog" ) );
    CHECK( requests.at( 0 ).headers.value( "authorization" ) == "Bearer gcm-token" );
    CHECK( requests.at( 0 ).headers.value( "x-tfs-fedauthredirect" ) == "Suppress" );
    CHECK( requests.at( 0 ).headers.value( "accept" ) == "application/octet-stream" );
    // approve runs after the job finished.
    CHECK( waitUntil( [ & ] { return git.verbs().size() == 2; } ) );
    CHECK( git.verbs() == QStringList{ "fill", "approve" } );
    CHECK( git.calls().at( 0 ).value( "GCM_INTERACTIVE" ).toString() == "never" );
    CHECK( git.calls().at( 0 ).value( "stdin" ).toString().contains(
        "path=contoso/Tools/_git/catalog\n" ) );
    CHECK( fixture.repository( id ).value( "signedIn" ) == true );
    CHECK( fixture.repository( id ).value( "state" ) == "empty" );

    // The credential is never written out (design 3.6).
    CHECK_FALSE( fixture.settingsText().contains( "gcm-token" ) );
    CHECK_FALSE( serialized( job ).contains( "gcm-token" ) );
    CHECK_FALSE( serialized( fixture.manager->jobs() ).contains( "gcm-token" ) );
    CHECK_FALSE( serialized( fixture.manager->repositories() ).contains( "gcm-token" ) );
#endif
}

TEST_CASE( "401, 203 and web pages mean sign-in needed; only the GUI rejects and asks again",
           "[plugins][workbench][E15][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    SignInFixture fixture;
    if ( !fixture.server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    FakeGit git( "basic", "stale,fresh" );
    fixture.server.route( CatalogPath, { 401, "denied", "text/plain" } );
    const auto id = fixture.add();

    SECTION( "silent checks never reject the credential" )
    {
        const auto job = fixture.fetch( id, "cli" );
        CHECK( job.value( "state" ) == "failed" );
        CHECK( job.value( "error" ).toString().contains( "Sign-in needed" ) );
        const auto summary
            = job.value( "result" ).toMap().value( "repositories" ).toList().value( 0 ).toMap();
        CHECK( summary.value( "state" ) == "sign_in_required" );
        CHECK( summary.value( "signInNeeded" ) == true );
        CHECK( git.verbs() == QStringList{ "fill" } );
        CHECK( fixture.repository( id ).value( "state" ) == "sign_in_required" );
        CHECK( fixture.repository( id ).value( "signedIn" ) == false );
    }
    SECTION( "the GUI rejects the refused credential and asks once more, interactively" )
    {
        fixture.fetch( id, RequestOriginGui );
        REQUIRE( git.verbs() == QStringList{ "fill", "reject", "fill" } );
        const auto calls = git.calls();
        CHECK( calls.at( 1 ).value( "stdin" ).toString().contains( "password=stale\n" ) );
        CHECK( calls.at( 1 ).value( "stdin" ).toString().contains( "host=dev.azure.com\n" ) );
        CHECK( calls.at( 2 ).value( "GCM_INTERACTIVE" ).toString() == "<unset>" );
        const auto requests = requestsTo( fixture.server, CatalogPath );
        REQUIRE( requests.size() == 2 );
        CHECK( requests.at( 1 ).headers.value( "authorization" )
               == "Basic " + QByteArray( "user:fresh" ).toBase64() );
        CHECK( fixture.repository( id ).value( "state" ) == "sign_in_required" );

        // Once the server accepts it, the next GUI fetch approves the credential.
        fixture.server.route( CatalogPath, { 200, emptyCatalog() } );
        CHECK( fixture.fetch( id, RequestOriginGui ).value( "state" ) == "completed" );
        CHECK( waitUntil( [ & ] { return git.verbs().last() == "approve"; } ) );
        CHECK( fixture.repository( id ).value( "signedIn" ) == true );
    }
    SECTION( "a 203 or an HTML page is a sign-in page, never a catalog" )
    {
        fixture.server.route( CatalogPath, { 203, emptyCatalog() } );
        CHECK( fixture.fetch( id, "mcp" ).value( "error" ).toString().contains( "Sign-in needed" ) );
        CHECK( fixture.repository( id ).value( "state" ) == "sign_in_required" );
        fixture.server.route( CatalogPath, { 200, "<html><body>Sign in</body></html>", "text/html" } );
        CHECK( fixture.fetch( id, "mcp" ).value( "error" ).toString().contains( "Sign-in needed" ) );
        fixture.server.route( CatalogPath, { 200, "  <!DOCTYPE html><html/>", "application/json" } );
        CHECK( fixture.fetch( id, "mcp" ).value( "error" ).toString().contains( "Sign-in needed" ) );
        CHECK( git.verbs().count( "reject" ) == 0 );
    }
    SECTION( "no credential from the manager is sign-in needed without a request" )
    {
        git.setMode( "fail" );
        const auto job = fixture.fetch( id, "mcp" );
        CHECK( job.value( "error" ).toString().contains( "Sign-in needed" ) );
        CHECK_FALSE( job.value( "error" ).toString().contains( "SECRET-STDERR" ) );
        CHECK( requestsTo( fixture.server, CatalogPath ).isEmpty() );
        CHECK( fixture.repository( id ).value( "state" ) == "sign_in_required" );
    }
#endif
}

TEST_CASE( "The credential stays inside the repository scope across redirects",
           "[plugins][workbench][E15][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    SignInFixture fixture;
    if ( !fixture.server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    FakeGit git( "bearer", "scoped" );
    const auto id = fixture.add();
    const auto redirect = []( const QByteArray& location ) {
        testhttps::Route route{ 302, {}, "text/plain" };
        route.headers.append( { "Location", location } );
        return route;
    };

    SECTION( "in scope keeps the credential, leaving the scope drops it for good" )
    {
        fixture.server.route( CatalogPath,
                              redirect( "https://dev.azure.com/CONTOSO/Other/_apis/git/"
                                        "repositories/catalog/items?path=%2Fplugins.json" ) );
        fixture.server.route( "/CONTOSO/Other/_apis/git/repositories/catalog/items",
                              redirect( "https://downloads.example.test/hop/plugins.json" ) );
        fixture.server.route( "/hop/plugins.json",
                              redirect( "https://dev.azure.com/contoso/Back/plugins.json" ) );
        fixture.server.route( "/contoso/Back/plugins.json", { 200, emptyCatalog() } );
        const auto job = fixture.fetch( id, "mcp" );
        INFO( serialized( job ).toStdString() );
        REQUIRE( job.value( "state" ) == "completed" );
        const auto first = requestsTo( fixture.server, CatalogPath );
        const auto second = requestsTo( fixture.server, "/CONTOSO/Other/" );
        const auto third = requestsTo( fixture.server, "/hop/" );
        const auto fourth = requestsTo( fixture.server, "/contoso/Back/" );
        REQUIRE( first.size() == 1 );
        REQUIRE( second.size() == 1 );
        REQUIRE( third.size() == 1 );
        REQUIRE( fourth.size() == 1 );
        CHECK( first.at( 0 ).headers.value( "authorization" ) == "Bearer scoped" );
        CHECK( second.at( 0 ).headers.value( "authorization" ) == "Bearer scoped" );
        CHECK_FALSE( third.at( 0 ).headers.contains( "authorization" ) );
        // Back in scope after leaving it: still without the credential.
        CHECK_FALSE( fourth.at( 0 ).headers.contains( "authorization" ) );
    }
    SECTION( "another organization, a dot segment or an encoded slash is out of scope" )
    {
        fixture.server.route( CatalogPath,
                              redirect( "https://dev.azure.com/fabrikam/T/plugins.json" ) );
        fixture.server.route( "/fabrikam/T/plugins.json", { 200, emptyCatalog() } );
        REQUIRE( fixture.fetch( id, "mcp" ).value( "state" ) == "completed" );
        CHECK_FALSE(
            requestsTo( fixture.server, "/fabrikam/" ).at( 0 ).headers.contains( "authorization" ) );

        fixture.server.route( CatalogPath,
                              redirect( "https://dev.azure.com/contoso%2Ffabrikam/plugins.json" ) );
        fixture.server.route( "/contoso%2Ffabrikam/plugins.json", { 200, emptyCatalog() } );
        fixture.fetch( id, "mcp" );
        const auto encoded = requestsTo( fixture.server, "/contoso%2F" );
        REQUIRE( encoded.size() == 1 );
        CHECK_FALSE( encoded.at( 0 ).headers.contains( "authorization" ) );
    }
    SECTION( "plain HTTP and redirect loops are refused" )
    {
        fixture.server.route( CatalogPath,
                              redirect( "http://dev.azure.com/contoso/Tools/plugins.json" ) );
        CHECK( fixture.fetch( id, "mcp" ).value( "error" ).toString().contains( "HTTPS" ) );
        fixture.server.route( CatalogPath,
                              redirect( "https://dev.azure.com" + CatalogPath + "?again=1" ) );
        CHECK( fixture.fetch( id, "mcp" ).value( "error" ).toString().contains(
            "Too many redirects" ) );
        CHECK( requestsTo( fixture.server, CatalogPath ).size() == 1 + 6 );
        fixture.server.route( CatalogPath, { 302, "", "text/plain" } );
        CHECK( fixture.fetch( id, "mcp" ).value( "error" ).toString().contains(
            "Unexpected redirect" ) );
    }
#endif
}

TEST_CASE( "Packages of an Azure DevOps catalog get the credential only inside the scope",
           "[plugins][workbench][E15][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    SignInFixture fixture;
    if ( !fixture.server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    FakeGit git( "bearer", "package-token" );
    const auto entry = [ & ]( const QString& id, const QString& url ) {
        return QJsonObject{ { "id", id },
                            { "name", id },
                            { "version", "1.0.0" },
                            { "protocol_version", 1 },
                            { "os", platformName() },
                            { "arch", QSysInfo::buildCpuArchitecture() },
                            { "url", url },
                            { "sha256", QString( 64, '0' ) } };
    };
    const auto catalog = QJsonDocument( QJsonObject{
        { "schema_version", 1 },
        { "plugins",
          QJsonArray{ entry( "org.test.inside",
                             "https://contoso.visualstudio.com/Tools/_git/catalog"
                             "?path=/packages/inside-1.0.0.zip&version=GBcatalog" ),
                      entry( "org.test.outside",
                             "https://downloads.example.test/outside-1.0.0.zip" ) } } } );
    fixture.server.route( CatalogPath, { 200, catalog.toJson() } );
    fixture.server.route( "/outside-1.0.0.zip", { 200, "not a zip", "application/zip" } );
    const auto id = fixture.add();
    REQUIRE( fixture.fetch( id, "mcp" ).value( "state" ) == "completed" );

    const auto inside
        = finishedJob( *fixture.manager, fixture.manager->manage( "install", "org.test.inside", {} ) );
    CHECK( inside.value( "error" ).toString().contains( "SHA-256" ) );
    const auto packageRequests = requestsTo( fixture.server, CatalogPath + "?path=%2Fpackages" );
    REQUIRE( packageRequests.size() == 1 );
    CHECK( packageRequests.at( 0 ).headers.value( "authorization" ) == "Bearer package-token" );
    CHECK( packageRequests.at( 0 ).headers.value( "accept" ) == "application/octet-stream" );

    const auto outside = finishedJob(
        *fixture.manager, fixture.manager->manage( "install", "org.test.outside", {} ) );
    CHECK( outside.value( "error" ).toString().contains( "SHA-256" ) );
    const auto outsideRequests = requestsTo( fixture.server, "/outside-1.0.0.zip" );
    REQUIRE( outsideRequests.size() == 1 );
    CHECK_FALSE( outsideRequests.at( 0 ).headers.contains( "authorization" ) );
    CHECK_FALSE( serialized( fixture.manager->jobs() ).contains( "package-token" ) );
#endif
}

TEST_CASE( "A personal access token comes only from the GUI or the CLI and is kept after it works",
           "[plugins][workbench][E15][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    SignInFixture fixture;
    if ( !fixture.server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    FakeGit git( "fail" );
    fixture.server.route( CatalogPath, { 200, emptyCatalog() } );
    const auto id = fixture.add( { { "credential_source", "personal-access-token" } } );
    const QVariantMap withToken{ { "repository", id },
                                 { "method", "personal-access-token" },
                                 { "token", "secret-pat" } };

    for ( const auto* origin : { "mcp", "python", "scenario", "" } ) {
        INFO( origin );
        CHECK( fixture.manager->manage( "repository_sign_in", {}, withToken, origin )
                   .error.contains( "only in the CILogg window" ) );
    }
    CHECK( fixture.manager
               ->manage( "repository_sign_in", {},
                         { { "repository", id },
                           { "method", "git-credential-manager" },
                           { "token", "secret-pat" } },
                         RequestOriginCli )
               .error.contains( "only to method personal-access-token" ) );
    CHECK_FALSE( fixture.tokens->contains( id ) );

    const auto signedIn = finishedJob(
        *fixture.manager,
        fixture.manager->manage( "repository_sign_in", {}, withToken, RequestOriginCli ) );
    INFO( serialized( signedIn ).toStdString() );
    REQUIRE( signedIn.value( "state" ) == "completed" );
    CHECK( signedIn.value( "result" ).toMap().value( "signedIn" ) == true );
    CHECK( signedIn.value( "result" ).toMap().value( "sessionOnly" ) == true );
    CHECK( fixture.tokens->contains( id ) );
    CHECK( requestsTo( fixture.server, CatalogPath ).last().headers.value( "authorization" )
           == "Basic " + QByteArray( ":secret-pat" ).toBase64() );
    CHECK( fixture.repository( id ).value( "signedIn" ) == true );
    CHECK( git.calls().isEmpty() ); // a PAT never runs Git

    // A later silent check uses the stored token.
    REQUIRE( fixture.fetch( id, "mcp" ).value( "state" ) == "completed" );
    CHECK( requestsTo( fixture.server, CatalogPath ).last().headers.value( "authorization" )
           == "Basic " + QByteArray( ":secret-pat" ).toBase64() );

    CHECK_FALSE( fixture.settingsText().contains( "secret-pat" ) );
    CHECK_FALSE( serialized( fixture.manager->jobs() ).contains( "secret-pat" ) );
    CHECK_FALSE( serialized( fixture.manager->repositories() ).contains( "secret-pat" ) );

    // Sign Out deletes the stored token; the next check needs a sign-in and sends nothing.
    const auto requestCount = requestsTo( fixture.server, CatalogPath ).size();
    const auto signedOut = finishedJob(
        *fixture.manager,
        fixture.manager->manage( "repository_sign_out", {}, { { "repository", id } }, "mcp" ) );
    CHECK( signedOut.value( "result" ).toMap().value( "signedOut" ) == true );
    CHECK_FALSE( fixture.tokens->contains( id ) );
    CHECK( fixture.repository( id ).value( "signedIn" ) == false );
    CHECK( fixture.fetch( id, "mcp" ).value( "error" ).toString().contains( "Sign-in needed" ) );
    CHECK( requestsTo( fixture.server, CatalogPath ).size() == requestCount );

    // A token that does not work is not stored.
    fixture.server.route( CatalogPath, { 401, "denied", "text/plain" } );
    const auto refused = finishedJob(
        *fixture.manager,
        fixture.manager->manage( "repository_sign_in", {}, withToken, RequestOriginGui ) );
    CHECK( refused.value( "result" ).toMap().value( "signedIn" ) == false );
    CHECK_FALSE( fixture.tokens->contains( id ) );

    // Removing the repository deletes its token.
    fixture.server.route( CatalogPath, { 200, emptyCatalog() } );
    finishedJob( *fixture.manager,
                 fixture.manager->manage( "repository_sign_in", {}, withToken, RequestOriginGui ) );
    REQUIRE( fixture.tokens->contains( id ) );
    REQUIRE( fixture.manager->manage( "repository_remove", {}, { { "repository", id } } ).ok() );
    CHECK_FALSE( fixture.tokens->contains( id ) );
#endif
}

TEST_CASE( "A token is not kept for a removed repository or when the store refuses it",
           "[plugins][workbench][E15][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    SignInFixture fixture;
    if ( !fixture.server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    FakeGit git( "fail" );
    const QVariantMap withToken{ { "method", "personal-access-token" }, { "token", "late-pat" } };
    const auto signIn = [ & ]( const QString& id ) {
        auto options = withToken;
        options.insert( "repository", id );
        return fixture.manager->manage( "repository_sign_in", {}, options, RequestOriginGui );
    };

    SECTION( "removed while the sign-in test runs" )
    {
        fixture.server.route( CatalogPath, { 200, emptyCatalog(), "application/json", 800 } );
        const auto id = fixture.add();
        const auto started = signIn( id );
        REQUIRE( started.ok() );
        REQUIRE( fixture.manager->manage( "repository_remove", {}, { { "repository", id } } ).ok() );
        const auto job = finishedJob( *fixture.manager, started );
        CHECK( job.value( "state" ) == "failed" );
        CHECK( job.value( "error" ).toString().contains( "removed" ) );
        CHECK_FALSE( fixture.tokens->contains( id ) );
    }
    SECTION( "the credential store refuses the token" )
    {
        class RefusingStore : public MemoryPatStore {
        public:
            void write( const QString&, const QByteArray&, Done done ) override
            {
                done( { QStringLiteral( "The credential store did not accept the token." ), false } );
            }
        };
        fixture.manager->setPatStore( std::make_unique<RefusingStore>() );
        fixture.server.route( CatalogPath, { 200, emptyCatalog() } );
        const auto id = fixture.add();
        const auto job = finishedJob( *fixture.manager, signIn( id ) );
        CHECK( job.value( "state" ) == "failed" );
        CHECK( job.value( "error" ).toString().contains( "did not accept" ) );
        // Not recorded as signed in, and still on the credential manager.
        CHECK( fixture.repository( id ).value( "signedIn" ) == false );
        CHECK( fixture.repository( id ).value( "state" ) == "sign_in_required" );
        CHECK( fixture.repository( id ).value( "credentialSource" ) == "git-credential-manager" );
    }
#endif
}

TEST_CASE( "The transport sets the request origin and the CLI reads a token only from a file",
           "[plugins][workbench][E15]" )
{
    QTemporaryDir temp;
    const auto write = [ & ]( const QString& name, const QByteArray& content ) {
        const auto path = QDir( temp.path() ).filePath( name );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( content );
        return path;
    };
    const auto options = write( "options.json", R"({"repository":"team"})" );
    const auto tokenFile = write( "token.txt", "  file-pat\n" );
    const QStringList signIn{ "cilogg",          "command",
                              "--action",        "plugin_manage",
                              "--plugin-operation", "repository_sign_in",
                              "--json-file",     options };

    const CliParameters fromFile( signIn + QStringList{ "--token-file", tokenFile } );
    REQUIRE( fromFile.commander_request );
    CHECK( fromFile.commander_request->origin == "cli" );
    CHECK( fromFile.commander_request->pluginArguments.value( "token" ) == "file-pat" );

    // A token in the options file, or for another operation, is refused.
    const CliParameters inJson( QStringList{ "cilogg", "command", "--action", "plugin_manage",
                                             "--plugin-operation", "repository_sign_in",
                                             "--json-file",
                                             write( "bad.json", R"({"token":"x"})" ) } );
    CHECK_FALSE( inJson.commander_request );
    CHECK( inJson.exit_message.contains( "--token-file" ) );
    const CliParameters otherOperation(
        QStringList{ "cilogg", "command", "--action", "plugin_manage", "--plugin-operation",
                     "catalog", "--token-file", tokenFile } );
    CHECK_FALSE( otherOperation.commander_request );
    // There is no option that takes the token itself.
    const CliParameters argument( signIn + QStringList{ "--token", "argv-pat" } );
    CHECK_FALSE( argument.commander_request );

    // The MCP server runs the CLI with CILOGG_COMMANDER_ORIGIN=mcp; nothing can claim gui.
    qputenv( "CILOGG_COMMANDER_ORIGIN", "mcp" );
    const CliParameters fromMcp( signIn );
    qputenv( "CILOGG_COMMANDER_ORIGIN", "gui" );
    const CliParameters claimsGui( signIn );
    qunsetenv( "CILOGG_COMMANDER_ORIGIN" );
    REQUIRE( fromMcp.commander_request );
    CHECK( fromMcp.commander_request->origin == "mcp" );
    REQUIRE( claimsGui.commander_request );
    CHECK( claimsGui.commander_request->origin == "cli" );

    // A payload cannot set the origin; the receiving transport does.
    auto map = commanderRequestToVariantMap( *fromFile.commander_request );
    CHECK( map.value( "origin" ) == "cli" );
    map.insert( "origin", "gui" );
    const auto restored = commanderRequestFromVariantMap( map );
    REQUIRE( restored );
    CHECK( restored->origin.isEmpty() );

    // MCP clients are not offered the token options.
    const CliParameters help( { "cilogg", "command", "--help" } );
    const auto mcpOptions
        = cilogg::mcp::parseCommandHelp( help.exit_message ).value( "options" ).toObject();
    CHECK( mcpOptions.contains( "plugin_operation" ) );
    CHECK_FALSE( mcpOptions.contains( "token_file" ) );
    CHECK_FALSE( mcpOptions.contains( "token_stdin" ) );
}

TEST_CASE( "A credential manager sign-in test prompts only from the GUI",
           "[plugins][workbench][E15][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    SignInFixture fixture;
    if ( !fixture.server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    FakeGit git( "jwt", "entra" );
    fixture.server.route( CatalogPath, { 200, emptyCatalog() } );
    const auto id = fixture.add();
    const QVariantMap options{ { "repository", id }, { "method", "git-credential-manager" } };

    const auto silent = finishedJob(
        *fixture.manager, fixture.manager->manage( "repository_sign_in", {}, options, "python" ) );
    CHECK( silent.value( "result" ).toMap().value( "signedIn" ) == true );
    CHECK( git.calls().at( 0 ).value( "GCM_INTERACTIVE" ).toString() == "never" );
    CHECK( requestsTo( fixture.server, CatalogPath )
               .last()
               .headers.value( "authorization" )
               .startsWith( "Bearer eyJ" ) );
    REQUIRE( waitUntil( [ & ] { return git.verbs() == QStringList{ "fill", "approve" }; } ) );

    finishedJob( *fixture.manager,
                 fixture.manager->manage( "repository_sign_in", {}, options, RequestOriginGui ) );
    const auto calls = git.calls();
    REQUIRE( calls.size() >= 3 );
    CHECK( calls.at( 2 ).value( "verb" ).toString() == "fill" );
    CHECK( calls.at( 2 ).value( "GCM_INTERACTIVE" ).toString() == "<unset>" );
#endif
}
