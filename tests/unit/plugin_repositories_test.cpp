// E15a: plugin repositories, the merged catalog and install-source pinning
// (docs/plugin-repositories.md sections 3.1-3.4 and 8).
#include "../helpers/https_catalog_server.h"
#include "cli.h"
#include "plugincatalog.h"
#include "pluginmanager.h"
#include "semanticversion.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <catch2/catch.hpp>
#include <kzip.h>

using namespace cilogg::plugins;

namespace {
bool waitUntil( const std::function<bool()>& predicate, int timeout = 15000 )
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
    REQUIRE( started.ok() );
    const auto id = started.payload.value( "jobId" ).toString();
    REQUIRE( waitUntil( [ & ] {
        const auto state = manager.job( id ).payload.value( "state" ).toString();
        return state == "completed" || state == "failed" || state == "cancelled";
    } ) );
    return manager.job( id ).payload;
}

QVariantList repositoryIds( const PluginManager& manager )
{
    QVariantList ids;
    for ( const auto& value : manager.repositories() ) {
        ids.append( value.toMap().value( "id" ) );
    }
    return ids;
}

QByteArray catalogBytes( const QJsonArray& entries )
{
    return QJsonDocument( QJsonObject{ { "schema_version", 1 }, { "plugins", entries } } ).toJson();
}

QJsonObject entryFor( const QString& id, const QString& version, const QString& url,
                      const QByteArray& package )
{
    return { { "id", id },
             { "name", "Fixture " + version },
             { "version", version },
             { "protocol_version", 1 },
             { "os", platformName() },
             { "arch", QSysInfo::buildCpuArchitecture() },
             { "url", url },
             { "sha256",
               QString::fromLatin1(
                   QCryptographicHash::hash( package, QCryptographicHash::Sha256 ).toHex() ) } };
}

// A ZIP of the plugin fixture with the given identity.
QByteArray fixturePackage( const QString& root, const QString& id, const QString& version,
                           const QJsonObject& extra = {} )
{
    const auto folder = QDir( root ).filePath( id + "-" + version );
    REQUIRE( QDir().mkpath( folder ) );
    const auto executable = QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName();
    QJsonObject manifest{
        { "id", id },
        { "name", "Fixture" },
        { "version", version },
        { "protocol_version", 1 },
        { "executable", executable },
        { "capabilities", QJsonArray{ "tool" } },
        { "actions", QJsonArray{ QJsonObject{ { "id", "summary" }, { "capability", "tool" } } } }
    };
    for ( auto it = extra.begin(); it != extra.end(); ++it ) {
        manifest.insert( it.key(), it.value() );
    }
    QFile file( QDir( folder ).filePath( "plugin.json" ) );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( QJsonDocument( manifest ).toJson() );
    file.close();
    REQUIRE( QFile::copy( QString::fromUtf8( PLUGIN_FIXTURE_PATH ),
                          QDir( folder ).filePath( executable ) ) );
    const auto zipPath = folder + ".zip";
    KZip zip( zipPath );
    REQUIRE( zip.open( QIODevice::WriteOnly ) );
    REQUIRE( zip.addLocalDirectory( folder, "" ) );
    REQUIRE( zip.close() );
    QFile archive( zipPath );
    REQUIRE( archive.open( QIODevice::ReadOnly ) );
    return archive.readAll();
}

QVariantMap pluginInfo( const PluginManager& manager, const QString& id )
{
    for ( const auto& value : manager.plugins() ) {
        if ( value.toMap().value( "id" ) == id ) {
            return value.toMap();
        }
    }
    return {};
}
} // namespace

TEST_CASE( "One Semantic Versioning 2.0.0 order for manifests, catalogs and updates",
           "[plugins][workbench][E15]" )
{
    const QStringList ascending{ "1.0.0-alpha",
                                 "1.0.0-alpha.1",
                                 "1.0.0-alpha.2",
                                 "1.0.0-alpha.10",
                                 "1.0.0-alpha.beta",
                                 "1.0.0-beta",
                                 "1.0.0-beta.2",
                                 "1.0.0-beta.11",
                                 "1.0.0-rc.1",
                                 "1.0.0",
                                 "1.0.1",
                                 "1.2.0",
                                 "2.0.0",
                                 "10.0.0",
                                 "123456789012345678901.0.0" };
    for ( int i = 0; i < ascending.size(); ++i ) {
        for ( int j = 0; j < ascending.size(); ++j ) {
            INFO( ascending[ i ].toStdString() << " vs " << ascending[ j ].toStdString() );
            REQUIRE( compareSemanticVersions( ascending[ i ], ascending[ j ] )
                     == ( i < j   ? -1
                          : i > j ? 1
                                  : 0 ) );
        }
    }
    // Build metadata is ignored.
    REQUIRE( compareSemanticVersions( "1.0.0+build", "1.0.0" ) == 0 );
    REQUIRE( compareSemanticVersions( "1.0.0-alpha+a", "1.0.0-alpha+b" ) == 0 );
    for ( const auto* invalid : { "1.0", "01.0.0", "1.0.0-", "1.0.0-01", "1.0.0+", "v1.0.0",
                                  "1.0.0.0", "", "1.0.0-a..b" } ) {
        INFO( invalid );
        REQUIRE_FALSE( isSemanticVersion( invalid ) );
        REQUIRE_FALSE( compareSemanticVersions( invalid, "1.0.0" ).has_value() );
    }
    REQUIRE_FALSE( isSemanticVersion( "1.0.0-" + QString( 200, 'a' ) ) );
    // The manifest uses the same rules.
    const auto manifest = []( const QString& version ) {
        return Manifest::parse(
            QJsonDocument( QJsonObject{ { "id", "org.test.version" },
                                        { "name", "Version" },
                                        { "version", version },
                                        { "protocol_version", 1 },
                                        { "executable", "python" },
                                        { "capabilities", QJsonArray{ "tool" } } } )
                .toJson(),
            {} );
    };
    REQUIRE( manifest( "1.0.0-alpha.2+build.7" ).valid() );
    REQUIRE_FALSE( manifest( "01.0.0" ).valid() );
    REQUIRE_FALSE( manifest( "1.0" ).valid() );
}

TEST_CASE(
    "Catalog entries fail one by one; kinds and host versions the host cannot use are hidden",
    "[plugins][workbench][E15]" )
{
    const QByteArray package( "package" );
    const auto valid = entryFor( "org.test.valid", "1.0.0", "https://test.invalid/v.zip", package );
    auto anyPlatform = entryFor( "org.test.any", "2.0.0", "https://test.invalid/a.zip", package );
    anyPlatform[ "os" ] = "any";
    anyPlatform[ "arch" ] = "any";
    auto otherPlatform = valid;
    otherPlatform[ "arch" ] = "unknown-cpu";
    auto noSha = valid;
    noSha[ "id" ] = "org.test.nosha";
    noSha.remove( "sha256" );
    auto badVersion = valid;
    badVersion[ "id" ] = "org.test.badversion";
    badVersion[ "version" ] = "1.0";
    auto http = valid;
    http[ "id" ] = "org.test.http";
    http[ "url" ] = "http://test.invalid/v.zip";
    // A kind this host does not run (configuration packs are supported since E16).
    auto pack = valid;
    pack[ "id" ] = "org.test.pack";
    pack[ "kind" ] = "future-kind";
    pack[ "contents" ] = QJsonArray{ "filters" };
    auto tooNew = valid;
    tooNew[ "id" ] = "org.test.new";
    tooNew[ "min_host_version" ] = "99.1";
    auto oldEnough = valid;
    oldEnough[ "id" ] = "org.test.old";
    oldEnough[ "min_host_version" ] = "26.1";
    auto twinA = entryFor( "org.test.twin", "3.0.0", "https://test.invalid/t1.zip", "one" );
    auto twinB = entryFor( "org.test.twin", "3.0.0+other", "https://test.invalid/t2.zip", "two" );
    const auto bytes = catalogBytes( { valid, anyPlatform, otherPlatform, noSha, badVersion, http,
                                       pack, tooNew, oldEnough, twinA, twinB, valid, 42 } );
    const auto read = readCatalog( bytes, "team", "26.4.2.1078" );
    REQUIRE( read.error.isEmpty() );
    QStringList ids;
    for ( const auto& entry : read.entries ) {
        ids.append( entry.toObject().value( "id" ).toString() );
        REQUIRE( entry.toObject().value( "repository" ) == "team" );
        REQUIRE( entry.toObject().value( "kind" ) == "process" );
    }
    // The same entry listed twice is one entry; twins with different packages are both left out.
    REQUIRE( ids == QStringList{ "org.test.valid", "org.test.any", "org.test.old" } );
    REQUIRE( read.warnings.size() == 5 );
    REQUIRE( read.warnings.join( '\n' ).contains( "org.test.nosha" ) );
    REQUIRE( read.warnings.join( '\n' ).contains( "org.test.twin 3.0.0" ) );
    REQUIRE( read.hidden.size() == 2 );
    REQUIRE( read.hidden[ 0 ].toObject().value( "id" ) == "org.test.pack" );
    REQUIRE( read.hidden[ 0 ].toObject().value( "reason" ).toString().contains( "future-kind" ) );
    REQUIRE( read.hidden[ 1 ].toObject().value( "reason" ) == "Needs CILogg 99.1 or newer." );
    // The entry ID covers the repository: the same package in two repositories is two entries.
    const auto other = readCatalog( bytes, "other", "26.4.2.1078" );
    REQUIRE( other.entries[ 0 ].toObject().value( "entry_id" )
             != read.entries[ 0 ].toObject().value( "entry_id" ) );
    REQUIRE( catalogEntryId( valid, "team" )
             == read.entries[ 0 ].toObject().value( "entry_id" ).toString() );
    // A catalog that is not schema 1 fails as a whole.
    REQUIRE_FALSE( readCatalog( "<html>sign in</html>", "team", "1" ).error.isEmpty() );
    REQUIRE_FALSE(
        readCatalog( R"({"schema_version":2,"plugins":[]})", "team", "1" ).error.isEmpty() );
    QJsonArray tooMany;
    for ( int index = 0; index <= MaxCatalogEntries; ++index ) {
        tooMany.append( valid );
    }
    REQUIRE_FALSE( readCatalog( catalogBytes( tooMany ), "team", "1" ).error.isEmpty() );
}

TEST_CASE( "Repository IDs are unique and never reused, and the address cannot be edited",
           "[plugins][workbench][E15]" )
{
    QTemporaryDir temp;
    PluginManager manager( QDir( temp.path() ).filePath( "data" ),
                           QDir( temp.path() ).filePath( "none" ) );
    REQUIRE( repositoryIds( manager ) == QVariantList{ "default" } );
    const auto add = [ & ]( const QString& name, const QString& url ) {
        return manager.manage( "repository_add", {}, { { "name", name }, { "url", url } } );
    };
    auto added = add( "Team", "https://example.test/team/plugins.json" );
    REQUIRE( added.ok() );
    REQUIRE( added.payload[ "repository" ].toMap()[ "id" ] == "team" );
    REQUIRE( added.payload[ "repository" ].toMap()[ "position" ] == 2 );
    REQUIRE( added.payload[ "repository" ].toMap()[ "signIn" ] == "none" );
    REQUIRE( add( "Team", "https://example.test/second/plugins.json" )
                 .payload[ "repository" ]
                 .toMap()[ "id" ]
             == "team-2" );
    // The same address, normalized, is refused.
    REQUIRE_FALSE( add( "Again", "HTTPS://EXAMPLE.test:443/team/plugins.json#x" ).ok() );
    REQUIRE_FALSE( add( "Plain", "http://example.test/plugins.json" ).ok() );
    REQUIRE( add( "Secret", "https://user:secret@example.test/plugins.json" )
                 .error.contains( "user name or password" ) );
    REQUIRE_FALSE( add( "", "https://example.test/x.json" ).ok() );
    REQUIRE( manager
                 .manage( "repository_add", {},
                          { { "name", "Other" },
                            { "url", "https://example.test/other.json" },
                            { "sign_in", "github" } } )
                 .error.contains( "sign_in must be none or azure-devops" ) );
    REQUIRE( manager
                 .manage( "repository_add", {},
                          { { "name", "Branch" },
                            { "url", "https://example.test/branch.json" },
                            { "branch", "main" } } )
                 .error.contains( "only to Azure DevOps" ) );
    REQUIRE( manager.manage( "repository_remove", {}, { { "repository", "team" } } ).ok() );
    // A removed ID is never given to a new repository.
    REQUIRE( add( "Team", "https://example.test/third/plugins.json" )
                 .payload[ "repository" ]
                 .toMap()[ "id" ]
             == "team-3" );
    REQUIRE( repositoryIds( manager ) == QVariantList{ "default", "team-2", "team-3" } );
    // Name, enabled and auto-update change; the address does not.
    REQUIRE(
        manager
            .manage( "repository_update", {},
                     { { "repository", "team-2" }, { "url", "https://example.test/new.json" } } )
            .error.contains( "cannot be changed" ) );
    const auto renamed
        = manager.manage( "repository_update", {},
                          { { "repository", "team-2" }, { "name", "Lab" }, { "enabled", false } } );
    REQUIRE( renamed.ok() );
    REQUIRE( renamed.payload[ "repository" ].toMap()[ "name" ] == "Lab" );
    REQUIRE( renamed.payload[ "repository" ].toMap()[ "enabled" ] == false );
    REQUIRE( renamed.payload[ "repository" ].toMap()[ "state" ] == "disabled" );
    REQUIRE( renamed.payload[ "repository" ].toMap()[ "url" ]
             == "https://example.test/second/plugins.json" );
    REQUIRE_FALSE(
        manager
            .manage( "repository_update", {}, { { "repository", "team-2" }, { "enabled", "yes" } } )
            .ok() );
    REQUIRE_FALSE( manager.manage( "repository_update", {}, { { "repository", "nope" } } ).ok() );
    // At most 16 repositories.
    for ( int index = int( manager.repositories().size() );
          index < PluginRepositories::MaxRepositories; ++index ) {
        REQUIRE( add( QString( "Extra %1" ).arg( index ),
                      QString( "https://example.test/extra/%1.json" ).arg( index ) )
                     .ok() );
    }
    REQUIRE( add( "One too many", "https://example.test/last.json" ).error.contains( "16" ) );
    // A new manager reads the same records.
    PluginManager restarted( QDir( temp.path() ).filePath( "data" ),
                             QDir( temp.path() ).filePath( "none" ) );
    REQUIRE( restarted.repositories() == manager.repositories() );
}

TEST_CASE( "The first start turns a changed catalogUrl into the default repository",
           "[plugins][workbench][E15]" )
{
    QTemporaryDir temp;
    const auto data = QDir( temp.path() ).filePath( "data" );
    REQUIRE( QDir().mkpath( data ) );
    {
        QSettings settings( QDir( data ).filePath( "settings.ini" ), QSettings::IniFormat );
        settings.setValue( "catalogUrl", "https://example.test/changed/plugins.json" );
    }
    {
        PluginManager manager( data, QDir( temp.path() ).filePath( "none" ) );
        const auto repositories = manager.repositories();
        REQUIRE( repositories.size() == 1 );
        const auto first = repositories[ 0 ].toMap();
        REQUIRE( first[ "id" ] == "default" );
        REQUIRE( first[ "position" ] == 1 );
        REQUIRE( first[ "name" ] == "CILogg examples (GitHub)" );
        REQUIRE( first[ "url" ] == "https://example.test/changed/plugins.json" );
        REQUIRE( first[ "enabled" ] == true );
        REQUIRE( first[ "signIn" ] == "none" );
        // catalogUrl stays for an older copy that shares the folder.
        REQUIRE( manager.catalogUrl() == "https://example.test/changed/plugins.json" );
        REQUIRE( manager.manage( "repository_remove", {}, { { "repository", "default" } } ).ok() );
    }
    // Migration runs once: a removed default does not come back.
    PluginManager restarted( data, QDir( temp.path() ).filePath( "none" ) );
    REQUIRE( restarted.repositories().isEmpty() );
    QSettings settings( QDir( data ).filePath( "settings.ini" ), QSettings::IniFormat );
    REQUIRE( settings.value( "catalogUrl" ) == "https://example.test/changed/plugins.json" );
    // A fresh folder gets the shipped default address.
    PluginManager fresh( QDir( temp.path() ).filePath( "fresh" ),
                         QDir( temp.path() ).filePath( "none" ) );
    REQUIRE( fresh.repositories()[ 0 ].toMap()[ "url" ] == PluginRepositories::DefaultCatalogUrl );
}

TEST_CASE( "Move Up, Move Down and repository_move persist the order", "[plugins][workbench][E15]" )
{
    QTemporaryDir temp;
    const auto data = QDir( temp.path() ).filePath( "data" );
    {
        PluginManager manager( data, QDir( temp.path() ).filePath( "none" ) );
        for ( const auto* name : { "Alpha", "Beta" } ) {
            REQUIRE(
                manager
                    .manage( "repository_add", {},
                             { { "name", name },
                               { "url", QString( "https://example.test/%1.json" ).arg( name ) } } )
                    .ok() );
        }
        const auto moved = manager.manage( "repository_move", {},
                                           { { "repository", "beta" }, { "position", 1 } } );
        REQUIRE( moved.ok() );
        REQUIRE( moved.payload[ "repositories" ].toList()[ 0 ].toMap()[ "id" ] == "beta" );
        REQUIRE( manager
                     .manage( "repository_move", {},
                              { { "repository", "default" }, { "direction", "up" } } )
                     .ok() );
        REQUIRE( manager
                     .manage( "repository_move", {},
                              { { "repository", "default" }, { "direction", "up" } } )
                     .ok() ); // already first: unchanged
        REQUIRE( manager
                     .manage( "repository_move", {},
                              { { "repository", "alpha" }, { "direction", "down" } } )
                     .ok() ); // already last: unchanged
        REQUIRE_FALSE(
            manager
                .manage( "repository_move", {}, { { "repository", "alpha" }, { "position", 4 } } )
                .ok() );
        REQUIRE_FALSE(
            manager
                .manage( "repository_move", {}, { { "repository", "alpha" }, { "position", 1.5 } } )
                .ok() );
        REQUIRE_FALSE(
            manager
                .manage( "repository_move", {},
                         { { "repository", "alpha" }, { "position", 1 }, { "direction", "up" } } )
                .ok() );
        REQUIRE_FALSE( manager
                           .manage( "repository_move", {},
                                    { { "repository", "alpha" }, { "direction", "left" } } )
                           .ok() );
    }
    PluginManager restarted( data, QDir( temp.path() ).filePath( "none" ) );
    REQUIRE( repositoryIds( restarted ) == QVariantList{ "default", "beta", "alpha" } );
    int position = 0;
    for ( const auto& value : restarted.repositories() ) {
        REQUIRE( value.toMap()[ "position" ] == ++position );
    }
}

TEST_CASE( "The merged HTTPS catalog keeps every repository's result",
           "[plugins][workbench][E15][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    testhttps::CatalogServer server;
    if ( !server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    QTemporaryDir temp;
    const auto package = fixturePackage( temp.path(), "org.test.fixture", "1.0.0" );
    server.route( "/pkg/fixture-1.0.0.zip", { 200, package, "application/zip", 0 } );
    auto broken = entryFor( "org.test.broken", "1.0.0", server.url( "/pkg/x.zip" ), package );
    broken.remove( "sha256" );
    server.route( "/good/plugins.json",
                  { 200,
                    catalogBytes( { entryFor( "org.test.fixture", "1.0.0",
                                              server.url( "/pkg/fixture-1.0.0.zip" ), package ),
                                    broken } ),
                    "application/json", 0 } );
    server.route( "/also/plugins.json",
                  { 200,
                    catalogBytes( { entryFor( "org.test.fixture", "1.0.0",
                                              server.url( "/pkg/fixture-1.0.0.zip" ), package ) } ),
                    "application/json", 0 } );
    server.route( "/empty/plugins.json", { 200, catalogBytes( {} ), "application/json", 0 } );
    server.route( "/page/plugins.json",
                  { 200, "<!DOCTYPE html><html>Sign in</html>", "text/html", 0 } );
    testhttps::TrustingNetwork network;
    PluginManager manager( QDir( temp.path() ).filePath( "data" ),
                           QDir( temp.path() ).filePath( "none" ), nullptr, &network );
    REQUIRE( manager.manage( "repository_remove", {}, { { "repository", "default" } } ).ok() );
    for ( const auto& [ name, path ] : { std::make_pair( "Empty", "/empty/plugins.json" ),
                                         std::make_pair( "Good", "/good/plugins.json" ),
                                         std::make_pair( "Missing", "/missing/plugins.json" ),
                                         std::make_pair( "Page", "/page/plugins.json" ),
                                         std::make_pair( "Also", "/also/plugins.json" ) } ) {
        REQUIRE(
            manager
                .manage( "repository_add", {}, { { "name", name }, { "url", server.url( path ) } } )
                .ok() );
    }
    const auto job = finishedJob( manager, manager.manage( "catalog", {}, {} ) );
    // One failing repository does not hide the others.
    REQUIRE( job[ "state" ] == "completed" );
    const auto result = job[ "result" ].toMap();
    const auto repositories = result[ "repositories" ].toList();
    REQUIRE( repositories.size() == 5 );
    QStringList states;
    for ( const auto& value : repositories ) {
        states.append( value.toMap()[ "repository" ].toString() + '='
                       + value.toMap()[ "state" ].toString() );
    }
    REQUIRE( states
             == QStringList{ "empty=empty", "good=loaded", "missing=failed", "page=failed",
                             "also=loaded" } );
    REQUIRE( repositories[ 0 ].toMap()[ "empty" ] == true );
    REQUIRE( repositories[ 1 ].toMap()[ "warnings" ].toStringList().size() == 1 );
    REQUIRE( repositories[ 2 ].toMap()[ "error" ].toString().contains( "404" ) );
    REQUIRE( repositories[ 3 ].toMap()[ "error" ].toString().contains( "web page" ) );
    // Plugins in repository order, each with its repository and kind; two repositories offering
    // the same package give two entries.
    const auto plugins = result[ "plugins" ].toList();
    REQUIRE( plugins.size() == 2 );
    REQUIRE( plugins[ 0 ].toMap()[ "repository" ] == "good" );
    REQUIRE( plugins[ 0 ].toMap()[ "repositoryName" ] == "Good" );
    REQUIRE( plugins[ 0 ].toMap()[ "kind" ] == "process" );
    REQUIRE( plugins[ 1 ].toMap()[ "repository" ] == "also" );
    REQUIRE( plugins[ 0 ].toMap()[ "entry_id" ] != plugins[ 1 ].toMap()[ "entry_id" ] );
    // The Repositories table shows each result after a restart as well.
    const auto records = manager.repositories();
    REQUIRE( records[ 0 ].toMap()[ "state" ] == "empty" );
    REQUIRE( records[ 0 ].toMap()[ "pluginCount" ] == 0 );
    REQUIRE( records[ 1 ].toMap()[ "pluginCount" ] == 1 );
    REQUIRE( records[ 1 ].toMap()[ "warnings" ] == 1 );
    REQUIRE_FALSE( records[ 1 ].toMap()[ "lastCheck" ].toString().isEmpty() );
    REQUIRE( records[ 2 ].toMap()[ "state" ] == "failed" );
    REQUIRE( records[ 2 ].toMap()[ "lastCheck" ].toString().isEmpty() );
    REQUIRE_FALSE( records[ 2 ].toMap()[ "lastError" ].toString().isEmpty() );
    // A public repository gets plain GET requests without credentials.
    REQUIRE( server.requestsFor( "/good/plugins.json" ).size() == 1 );
    for ( const auto& request : server.requests ) {
        REQUIRE( request.method == "GET" );
        REQUIRE_FALSE( request.headers.contains( "authorization" ) );
    }
    // A new install takes the higher repository.
    const auto installed
        = finishedJob( manager, manager.manage( "install", "org.test.fixture", {} ) );
    REQUIRE( installed[ "state" ] == "completed" );
    REQUIRE( installed[ "result" ].toMap()[ "repository" ] == "good" );
    REQUIRE( server.requestsFor( "/pkg/fixture-1.0.0.zip" ).size() == 1 );
    const auto source = pluginInfo( manager, "org.test.fixture" )[ "source" ].toMap();
    REQUIRE( source[ "type" ] == "repository" );
    REQUIRE( source[ "repository" ] == "good" );
    REQUIRE( source[ "version" ] == "1.0.0" );
    REQUIRE( pluginInfo( manager, "org.test.fixture" )[ "kind" ] == "process" );
    // Only one repository fetched: the others keep their last results.
    const auto single
        = finishedJob( manager, manager.manage( "catalog", {}, { { "repository", "empty" } } ) );
    REQUIRE( single[ "state" ] == "completed" );
    REQUIRE( single[ "result" ].toMap()[ "repositories" ].toList().size() == 1 );
    REQUIRE( manager.catalogEntries().size() == 2 );
    // Every repository failing fails the job, with each reason.
    const auto failed
        = finishedJob( manager, manager.manage( "catalog", {}, { { "repository", "missing" } } ) );
    REQUIRE( failed[ "state" ] == "failed" );
    REQUIRE( failed[ "error" ].toString().startsWith( "Missing:" ) );
    // Disabled repositories are not fetched.
    REQUIRE(
        manager
            .manage( "repository_update", {}, { { "repository", "page" }, { "enabled", false } } )
            .ok() );
    const auto before = server.requestsFor( "/page/plugins.json" ).size();
    REQUIRE( finishedJob( manager, manager.manage( "catalog", {}, {} ) )[ "state" ]
             == "completed" );
    REQUIRE( server.requestsFor( "/page/plugins.json" ).size() == before );
    REQUIRE_FALSE( manager.manage( "catalog", {}, { { "repository", "page" } } ).ok() );
#endif
}

TEST_CASE( "Updates come only from the install source; another repository needs switch_source",
           "[plugins][workbench][E15][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    testhttps::CatalogServer server;
    if ( !server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    QTemporaryDir temp;
    const auto v100 = fixturePackage( temp.path(), "org.test.fixture", "1.0.0" );
    const auto v110 = fixturePackage( temp.path(), "org.test.fixture", "1.1.0" );
    const auto v120 = fixturePackage( temp.path(), "org.test.fixture", "1.2.0-beta.1" );
    const auto wrongKind
        = fixturePackage( temp.path(), "org.test.kind", "1.0.0", { { "kind", "future-kind" } } );
    server.route( "/a/1.0.0.zip", { 200, v100, "application/zip", 0 } );
    server.route( "/b/1.1.0.zip", { 200, v110, "application/zip", 0 } );
    server.route( "/a/1.2.0.zip", { 200, v120, "application/zip", 0 } );
    server.route( "/a/kind.zip", { 200, wrongKind, "application/zip", 0 } );
    const auto routeA = [ & ]( QJsonArray entries ) {
        server.route( "/a/plugins.json", { 200, catalogBytes( entries ), "application/json", 0 } );
    };
    routeA( { entryFor( "org.test.fixture", "1.0.0", server.url( "/a/1.0.0.zip" ), v100 ),
              entryFor( "org.test.kind", "1.0.0", server.url( "/a/kind.zip" ), wrongKind ) } );
    server.route( "/b/plugins.json",
                  { 200,
                    catalogBytes( { entryFor( "org.test.fixture", "1.1.0",
                                              server.url( "/b/1.1.0.zip" ), v110 ) } ),
                    "application/json", 0 } );
    testhttps::TrustingNetwork network;
    PluginManager manager( QDir( temp.path() ).filePath( "data" ),
                           QDir( temp.path() ).filePath( "none" ), nullptr, &network );
    REQUIRE( manager.manage( "repository_remove", {}, { { "repository", "default" } } ).ok() );
    REQUIRE( manager
                 .manage( "repository_add", {},
                          { { "name", "A" }, { "url", server.url( "/a/plugins.json" ) } } )
                 .ok() );
    REQUIRE( manager
                 .manage( "repository_add", {},
                          { { "name", "B" }, { "url", server.url( "/b/plugins.json" ) } } )
                 .ok() );
    REQUIRE( finishedJob( manager, manager.manage( "catalog", {}, {} ) )[ "state" ]
             == "completed" );
    // The higher repository wins a new install, although B has a newer version.
    REQUIRE( finishedJob( manager, manager.manage( "install", "org.test.fixture", {} ) )[ "state" ]
             == "completed" );
    REQUIRE( pluginInfo( manager, "org.test.fixture" )[ "version" ] == "1.0.0" );
    REQUIRE( pluginInfo( manager, "org.test.fixture" )[ "source" ].toMap()[ "repository" ] == "a" );
    // B's newer version is never offered as an update of a plugin installed from A.
    QString bEntry;
    for ( const auto& value : manager.catalogEntries() ) {
        if ( value.toMap()[ "repository" ] == "b" ) {
            bEntry = value.toMap()[ "entry_id" ].toString();
            REQUIRE( value.toMap()[ "installed" ] == "1.0.0" );
            REQUIRE( value.toMap()[ "installedSource" ] == "a" );
        }
    }
    const auto refused
        = manager.manage( "update", "org.test.fixture", { { "catalog_entry_id", bEntry } } );
    REQUIRE_FALSE( refused.ok() );
    REQUIRE( refused.error.contains( "switch_source" ) );
    REQUIRE( refused.error.contains( "\"A\"" ) );
    REQUIRE( refused.error.contains( "\"B\"" ) );
    REQUIRE_FALSE(
        manager.manage( "install", "org.test.fixture", { { "catalog_entry_id", bEntry } } ).ok() );
    // Reordering changes no installed plugin and starts nothing.
    const auto jobsBefore = manager.jobs().size();
    REQUIRE( manager.manage( "repository_move", {}, { { "repository", "b" }, { "position", 1 } } )
                 .ok() );
    REQUIRE( manager.jobs().size() == jobsBefore );
    REQUIRE( pluginInfo( manager, "org.test.fixture" )[ "source" ].toMap()[ "repository" ] == "a" );
    // Without an entry, update takes the newest version of the source, by Semantic Versioning.
    routeA( { entryFor( "org.test.fixture", "1.0.0", server.url( "/a/1.0.0.zip" ), v100 ),
              entryFor( "org.test.fixture", "1.2.0-beta.1", server.url( "/a/1.2.0.zip" ), v120 ),
              entryFor( "org.test.kind", "1.0.0", server.url( "/a/kind.zip" ), wrongKind ) } );
    REQUIRE( finishedJob( manager, manager.manage( "catalog", {}, {} ) )[ "state" ]
             == "completed" );
    const auto updated = finishedJob( manager, manager.manage( "update", "org.test.fixture", {} ) );
    REQUIRE( updated[ "state" ] == "completed" );
    REQUIRE( pluginInfo( manager, "org.test.fixture" )[ "version" ] == "1.2.0-beta.1" );
    REQUIRE( pluginInfo( manager, "org.test.fixture" )[ "source" ].toMap()[ "version" ]
             == "1.2.0-beta.1" );
    // An explicit Switch source installs B's package and makes B the source.
    const auto switched = finishedJob(
        manager, manager.manage( "install", "org.test.fixture",
                                 { { "catalog_entry_id", bEntry }, { "switch_source", true } } ) );
    REQUIRE( switched[ "state" ] == "completed" );
    REQUIRE( pluginInfo( manager, "org.test.fixture" )[ "version" ] == "1.1.0" );
    REQUIRE( pluginInfo( manager, "org.test.fixture" )[ "source" ].toMap()[ "repository" ] == "b" );
    // B offers only the installed version: an update has nothing to do.
    REQUIRE(
        manager.manage( "update", "org.test.fixture", {} ).error.contains( "already installed" ) );
    // Removing the source keeps the plugin; its source becomes unavailable.
    REQUIRE( manager.manage( "repository_remove", {}, { { "repository", "b" } } ).ok() );
    const auto orphan = pluginInfo( manager, "org.test.fixture" );
    REQUIRE( orphan[ "version" ] == "1.1.0" );
    REQUIRE( orphan[ "source" ].toMap()[ "repository" ] == "b" );
    REQUIRE( orphan[ "source" ].toMap()[ "available" ] == false );
    REQUIRE( manager.manage( "update", "org.test.fixture", {} ).error.contains( "(removed)" ) );
    // A package whose kind this host does not run is rejected after download.
    const auto kind = finishedJob( manager, manager.manage( "install", "org.test.kind", {} ) );
    REQUIRE( kind[ "state" ] == "failed" );
    REQUIRE( kind[ "error" ].toString().contains( "kind" ) );
    REQUIRE( pluginInfo( manager, "org.test.kind" ).isEmpty() );
    // A local install has no source; removing the plugin clears its record.
    REQUIRE( manager.manage( "remove", "org.test.fixture", {} ).ok() );
    REQUIRE( pluginInfo( manager, "org.test.fixture" ).isEmpty() );
    QSettings settings( QDir( QDir( temp.path() ).filePath( "data" ) ).filePath( "settings.ini" ),
                        QSettings::IniFormat );
    REQUIRE_FALSE( settings.contains( "plugins/org.test.fixture/sourceRepository" ) );
#endif
}

TEST_CASE( "The catalog url option adds its address as a repository once",
           "[plugins][workbench][E15]" )
{
    QTemporaryDir temp;
    PluginManager manager( QDir( temp.path() ).filePath( "data" ),
                           QDir( temp.path() ).filePath( "none" ) );
    REQUIRE_FALSE(
        manager.manage( "catalog", {}, { { "url", "http://example.test/p.json" } } ).ok() );
    REQUIRE( manager.manage( "catalog", {}, { { "url", "https://example.test/p.json" } } ).ok() );
    REQUIRE( manager.manage( "catalog", {}, { { "url", "https://EXAMPLE.test/p.json" } } ).ok() );
    REQUIRE( repositoryIds( manager ) == QVariantList{ "default", "example-test" } );
    REQUIRE( manager.repositories()[ 1 ].toMap()[ "name" ] == "example.test" );
    REQUIRE_FALSE( manager.manage( "catalog", {}, { { "repository", "nope" } } ).ok() );
    REQUIRE_FALSE(
        manager
            .manage( "catalog", {}, { { "repository", "default" }, { "url", "https://x.test/" } } )
            .ok() );
    REQUIRE_FALSE( manager.manage( "catalog", {}, { { "colour", "blue" } } ).ok() );
}

TEST_CASE( "get_plugins reports kind and source for shipped and local plugins",
           "[plugins][workbench][E15]" )
{
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    const auto folder = QDir( shipped ).filePath( "org.test.shipped" );
    REQUIRE( QDir().mkpath( folder ) );
    QFile manifest( QDir( folder ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QJsonDocument( QJsonObject{ { "id", "org.test.shipped" },
                                                { "name", "Shipped" },
                                                { "version", "1.0.0" },
                                                { "protocol_version", 1 },
                                                { "executable", "python" },
                                                { "capabilities", QJsonArray{ "tool" } } } )
                        .toJson() );
    manifest.close();
    PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
    const auto plugin = pluginInfo( manager, "org.test.shipped" );
    REQUIRE( plugin[ "kind" ] == "process" );
    REQUIRE( plugin[ "source" ].toMap()[ "type" ] == "shipped" );
    const auto local = QDir( temp.path() ).filePath( "local.zip" );
    QFile zip( local );
    REQUIRE( zip.open( QIODevice::WriteOnly ) );
    zip.write( fixturePackage( temp.path(), "org.test.local", "2.0.0" ) );
    zip.close();
    REQUIRE(
        finishedJob( manager, manager.manage( "install", {}, { { "path", local } } ) )[ "state" ]
        == "completed" );
    REQUIRE( pluginInfo( manager, "org.test.local" )[ "source" ].toMap()[ "type" ] == "local" );
    // Without a source there is nothing to update from.
    REQUIRE_FALSE( manager.manage( "update", "org.test.local", {} ).ok() );
}

TEST_CASE( "Repository operations round trip through the CLI",
           "[plugins][workbench][E15][commander]" )
{
    QTemporaryDir temp;
    const auto options = QDir( temp.path() ).filePath( "move.json" );
    QFile file( options );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( R"({"repository":"team","direction":"up"})" );
    file.close();
    const CliParameters parsed( QStringList{ "cilogg", "command", "--action", "plugin_manage",
                                             "--plugin-operation", "repository_move", "--json-file",
                                             options } );
    REQUIRE( parsed.commander_request );
    REQUIRE( parsed.commander_request->pluginOperation == "repository_move" );
    REQUIRE( parsed.commander_request->pluginArguments
             == QVariantMap{ { "repository", "team" }, { "direction", "up" } } );
    const auto restored = commanderRequestFromVariantMap(
        commanderRequestToVariantMap( *parsed.commander_request ) );
    REQUIRE( restored );
    REQUIRE( restored->pluginArguments == parsed.commander_request->pluginArguments );
}

TEST_CASE( "Repository names never collide with the list keys, and addresses compare normalized",
           "[plugins][workbench][E15][review]" )
{
    QTemporaryDir temp;
    const auto data = QDir( temp.path() ).filePath( "data" );
    {
        PluginManager manager( data, QDir( temp.path() ).filePath( "none" ) );
        for ( const auto* name : { "Order", "Retired", "Schema", "Items" } ) {
            REQUIRE(
                manager
                    .manage( "repository_add", {},
                             { { "name", name },
                               { "url",
                                 QString( "https://example.test/%1/plugins.json" ).arg( name ) } } )
                    .ok() );
        }
        REQUIRE( repositoryIds( manager )
                 == QVariantList{ "default", "order", "retired", "schema", "items" } );
        for ( const auto* id : { "order", "retired", "schema", "items" } ) {
            REQUIRE( manager.manage( "repository_remove", {}, { { "repository", id } } ).ok() );
        }
        REQUIRE( repositoryIds( manager ) == QVariantList{ "default" } );
        // The retired list survived: the IDs are not given again.
        REQUIRE( manager
                     .manage( "repository_add", {},
                              { { "name", "Order" }, { "url", "https://example.test/o2.json" } } )
                     .payload[ "repository" ]
                     .toMap()[ "id" ]
                 == "order-2" );
        // https://h and https://h/ are the same address.
        REQUIRE( manager
                     .manage( "repository_add", {},
                              { { "name", "Host" }, { "url", "https://host.example.test" } } )
                     .ok() );
        REQUIRE_FALSE(
            manager
                .manage( "repository_add", {},
                         { { "name", "Host again" }, { "url", "https://host.example.test/" } } )
                .ok() );
        // A bidirectional override in a name could disguise it.
        REQUIRE_FALSE( manager
                           .manage( "repository_add", {},
                                    { { "name", QString( "Team" ) + QChar( 0x202E ) + "slooT" },
                                      { "url", "https://example.test/bidi.json" } } )
                           .ok() );
    }
    // The schema marker survived the removals: no second migration.
    PluginManager restarted( data, QDir( temp.path() ).filePath( "none" ) );
    REQUIRE( repositoryIds( restarted ) == QVariantList{ "default", "order-2", "host" } );
}

TEST_CASE( "An unusable saved catalogUrl migrates to the shipped address",
           "[plugins][workbench][E15][review]" )
{
    for ( const auto* saved : { "", "http://example.test/plugins.json", "not an address" } ) {
        QTemporaryDir temp;
        const auto data = QDir( temp.path() ).filePath( "data" );
        REQUIRE( QDir().mkpath( data ) );
        {
            QSettings settings( QDir( data ).filePath( "settings.ini" ), QSettings::IniFormat );
            settings.setValue( "catalogUrl", saved );
        }
        PluginManager manager( data, QDir( temp.path() ).filePath( "none" ) );
        INFO( saved );
        REQUIRE( manager.repositories()[ 0 ].toMap()[ "url" ]
                 == PluginRepositories::DefaultCatalogUrl );
    }
}

TEST_CASE( "Versions, IDs and checksums with a trailing line break are invalid",
           "[plugins][workbench][E15][review]" )
{
    REQUIRE_FALSE( isSemanticVersion( "1.0.0\n" ) );
    const QByteArray package( "package" );
    auto entry = entryFor( "org.test.newline", "1.0.0", "https://test.invalid/a.zip", package );
    auto badId = entry;
    badId[ "id" ] = "org.test.newline\n";
    auto badSha = entry;
    badSha[ "sha256" ] = entry[ "sha256" ].toString() + '\n';
    auto badVersion = entry;
    badVersion[ "version" ] = "1.0.0\n";
    const auto read = readCatalog( catalogBytes( { badId, badSha, badVersion } ), "team", "1" );
    REQUIRE( read.entries.isEmpty() );
    REQUIRE( read.warnings.size() == 3 );
    const auto manifest
        = Manifest::parse( QJsonDocument( QJsonObject{ { "id", "org.test.newline\n" },
                                                       { "name", "Newline" },
                                                       { "version", "1.0.0" },
                                                       { "protocol_version", 1 },
                                                       { "executable", "python" },
                                                       { "capabilities", QJsonArray{ "tool" } } } )
                               .toJson(),
                           {} );
    REQUIRE_FALSE( manifest.valid() );
}

TEST_CASE( "A disabled repository offers nothing from its last fetch",
           "[plugins][workbench][E15][review][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    testhttps::CatalogServer server;
    if ( !server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    QTemporaryDir temp;
    const auto package = fixturePackage( temp.path(), "org.test.fixture", "1.0.0" );
    server.route( "/pkg.zip", { 200, package, "application/zip", 0 } );
    server.route( "/plugins.json",
                  { 200,
                    catalogBytes( { entryFor( "org.test.fixture", "1.0.0", server.url( "/pkg.zip" ),
                                              package ) } ),
                    "application/json", 0 } );
    testhttps::TrustingNetwork network;
    PluginManager manager( QDir( temp.path() ).filePath( "data" ),
                           QDir( temp.path() ).filePath( "none" ), nullptr, &network );
    REQUIRE( manager.manage( "repository_remove", {}, { { "repository", "default" } } ).ok() );
    REQUIRE( manager
                 .manage( "repository_add", {},
                          { { "name", "Team" }, { "url", server.url( "/plugins.json" ) } } )
                 .ok() );
    REQUIRE( finishedJob( manager, manager.manage( "catalog", {}, {} ) )[ "state" ]
             == "completed" );
    const auto entryId = manager.catalogEntries()[ 0 ].toMap()[ "entry_id" ];
    REQUIRE(
        manager
            .manage( "repository_update", {}, { { "repository", "team" }, { "enabled", false } } )
            .ok() );
    REQUIRE( manager.catalogEntries().isEmpty() );
    REQUIRE_FALSE(
        manager.manage( "install", "org.test.fixture", { { "catalog_entry_id", entryId } } ).ok() );
    REQUIRE(
        manager.manage( "repository_update", {}, { { "repository", "team" }, { "enabled", true } } )
            .ok() );
    REQUIRE( manager.catalogEntries().size() == 1 );
#endif
}

TEST_CASE( "A failed refresh keeps the last successful catalog, and a saved install is never "
           "reported as failed",
           "[plugins][workbench][E15][review]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    testhttps::CatalogServer server;
    if ( !server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    QTemporaryDir temp;
    const auto v100 = fixturePackage( temp.path(), "org.test.fixture", "1.0.0" );
    const auto v110 = fixturePackage( temp.path(), "org.test.fixture", "1.1.0" );
    server.route( "/1.0.0.zip", { 200, v100, "application/zip", 0 } );
    server.route( "/1.1.0.zip", { 200, v110, "application/zip", 0 } );
    server.route( "/plugins.json",
                  { 200,
                    catalogBytes( { entryFor( "org.test.fixture", "1.0.0",
                                              server.url( "/1.0.0.zip" ), v100 ) } ),
                    "application/json", 0 } );
    testhttps::TrustingNetwork network;
    const auto data = QDir( temp.path() ).filePath( "data" );
    PluginManager manager( data, QDir( temp.path() ).filePath( "none" ), nullptr, &network );
    REQUIRE( manager.manage( "repository_remove", {}, { { "repository", "default" } } ).ok() );
    REQUIRE( manager
                 .manage( "repository_add", {},
                          { { "name", "Team" }, { "url", server.url( "/plugins.json" ) } } )
                 .ok() );
    REQUIRE( finishedJob( manager, manager.manage( "catalog", {}, {} ) )[ "state" ]
             == "completed" );
    // The catalog address fails now: its entries stay offered.
    server.route( "/plugins.json", { 404, "gone", "text/plain", 0 } );
    const auto failed = finishedJob( manager, manager.manage( "catalog", {}, {} ) );
    REQUIRE( failed[ "state" ] == "failed" );
    const auto summary = failed[ "result" ].toMap()[ "repositories" ].toList()[ 0 ].toMap();
    REQUIRE( summary[ "state" ] == "failed" );
    REQUIRE( summary[ "cachedEntries" ] == 1 );
    REQUIRE( manager.repositories()[ 0 ].toMap()[ "state" ] == "failed" );
    REQUIRE( manager.catalogEntries().size() == 1 );
    REQUIRE( finishedJob( manager, manager.manage( "install", "org.test.fixture", {} ) )[ "state" ]
             == "completed" );
    // An update whose source record cannot be written still reports the installed version.
    server.route( "/plugins.json",
                  { 200,
                    catalogBytes( { entryFor( "org.test.fixture", "1.1.0",
                                              server.url( "/1.1.0.zip" ), v110 ) } ),
                    "application/json", 0 } );
    REQUIRE( finishedJob( manager, manager.manage( "catalog", {}, {} ) )[ "state" ]
             == "completed" );
    const auto settingsFile = QDir( data ).filePath( "settings.ini" );
    const auto permissions = QFile::permissions( settingsFile );
    REQUIRE( QFile::setPermissions( settingsFile, QFile::ReadOwner | QFile::ReadUser ) );
    const auto updated = finishedJob( manager, manager.manage( "update", "org.test.fixture", {} ) );
    QFile::setPermissions( settingsFile, permissions );
    REQUIRE( updated[ "state" ] == "completed" );
    REQUIRE( updated[ "error" ].toString().isEmpty() );
    REQUIRE( pluginInfo( manager, "org.test.fixture" )[ "version" ] == "1.1.0" );
    const auto warning = updated[ "result" ].toMap()[ "warning" ].toString();
    INFO( "warning: " << warning.toStdString() );
    REQUIRE( ( warning.isEmpty() || warning.contains( "was installed" ) ) );
#endif
}
