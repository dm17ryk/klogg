// E15c: update checks, notices, Skip this version and auto-update within the major version
// (docs/plugin-repositories.md sections 3.7, 3.8 and 8).
#include "../helpers/https_catalog_server.h"
#include "pluginmanager.h"
#include "pluginupdatechecker.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSettings>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <catch2/catch.hpp>
#include <kzip.h>

using namespace cilogg::plugins;

namespace {
const QString FixtureId = QStringLiteral( "org.test.fixture" );

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

// Runs the event loop for `ms` milliseconds.
void pump( int ms )
{
    waitUntil( [] { return false; }, ms );
}

bool finished( const PluginManager& manager, const QString& id )
{
    const auto state = manager.job( id ).payload.value( "state" ).toString();
    return state == "completed" || state == "failed" || state == "cancelled";
}

QVariantMap finishedJob( PluginManager& manager, const Result& started )
{
    REQUIRE( started.ok() );
    const auto id = started.payload.value( "jobId" ).toString();
    REQUIRE_FALSE( id.isEmpty() );
    REQUIRE( waitUntil( [ & ] { return finished( manager, id ); } ) );
    return manager.job( id ).payload;
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
QByteArray fixturePackage( const QString& root, const QString& id, const QString& version )
{
    const auto folder = QDir( root ).filePath( id + "-" + version );
    REQUIRE( QDir().mkpath( folder ) );
    const auto executable = QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName();
    const QJsonObject manifest{
        { "id", id },
        { "name", "Fixture" },
        { "version", version },
        { "protocol_version", 1 },
        { "executable", executable },
        { "capabilities", QJsonArray{ "tool" } },
        { "actions", QJsonArray{ QJsonObject{ { "id", "summary" }, { "capability", "tool" } } } }
    };
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

// Every signal of the update notices, in the order they came.
struct Notices {
    QVariantList found;
    QVariantList updated;
    QVariantList failed;
    explicit Notices( PluginManager& manager )
    {
        QObject::connect( &manager, &PluginManager::updatesFound,
                          [ this ]( const QVariantList& updates ) { found.append( updates ); } );
        QObject::connect( &manager, &PluginManager::autoUpdated,
                          [ this ]( const QVariantMap& update ) { updated.append( update ); } );
        QObject::connect( &manager, &PluginManager::autoUpdateFailed,
                          [ this ]( const QVariantMap& update ) { failed.append( update ); } );
    }
};

// A repository "a" that serves the fixture packages of `versions` and a catalog that lists
// `listed`, and a data folder whose plugin manager has it as its only repository.
struct UpdateFixture {
    testhttps::CatalogServer server;
    testhttps::TrustingNetwork network;
    QTemporaryDir temp;
    std::map<QString, QByteArray> packages;

    QString data() const
    {
        return QDir( temp.path() ).filePath( "data" );
    }
    std::unique_ptr<PluginManager> manager()
    {
        return std::make_unique<PluginManager>( data(), QDir( temp.path() ).filePath( "none" ),
                                                nullptr, &network );
    }
    void publish( const QStringList& listed, const QString& other = {} )
    {
        QJsonArray entries;
        for ( const auto& version : listed ) {
            if ( !packages.count( version ) ) {
                packages[ version ] = fixturePackage( temp.path(), FixtureId, version );
                server.route( "/a/" + version + ".zip",
                              { 200, packages[ version ], "application/zip", 0 } );
            }
            entries.append( entryFor( FixtureId, version, server.url( "/a/" + version + ".zip" ),
                                      packages[ version ] ) );
        }
        if ( !other.isEmpty() ) {
            const auto package = fixturePackage( temp.path(), other, "1.0.0" );
            server.route( "/a/other.zip", { 200, package, "application/zip", 0 } );
            entries.append( entryFor( other, "1.0.0", server.url( "/a/other.zip" ), package ) );
        }
        server.route( "/a/plugins.json", { 200, catalogBytes( entries ), "application/json", 0 } );
    }
    // Adds repository "a", fetches it and installs the fixture from it.
    void install( PluginManager& manager, const QString& version, bool autoUpdate = false )
    {
        publish( { version } );
        REQUIRE( manager.manage( "repository_remove", {}, { { "repository", "default" } } ).ok() );
        REQUIRE( manager
                     .manage( "repository_add", {},
                              { { "name", "A" },
                                { "url", server.url( "/a/plugins.json" ) },
                                { "auto_update", autoUpdate } } )
                     .ok() );
        REQUIRE( finishedJob( manager, manager.manage( "catalog", {}, {} ) )[ "state" ]
                 == "completed" );
        REQUIRE( finishedJob( manager, manager.manage( "install", FixtureId, {} ) )[ "state" ]
                 == "completed" );
        REQUIRE( pluginInfo( manager, FixtureId )[ "version" ] == version );
    }
    // Makes the last check of repository "a" two days old, as after a long break.
    void ageLastCheck()
    {
        QSettings settings( QDir( data() ).filePath( "settings.ini" ), QSettings::IniFormat );
        settings.setValue( "repositories/items/a/lastCheck",
                           QDateTime::currentDateTimeUtc().addDays( -2 ).toString( Qt::ISODate ) );
        settings.sync();
        REQUIRE( settings.status() == QSettings::NoError );
    }
    qsizetype catalogRequests() const
    {
        return server.requestsFor( "/a/plugins.json" ).size();
    }
};

QString scheduledJob( PluginManager& manager )
{
    const auto started = manager.runScheduledUpdateCheck();
    REQUIRE( started.ok() );
    return started.payload.value( "jobId" ).toString();
}
} // namespace

TEST_CASE( "The silent update check skips repositories checked in the last 24 hours and "
           "announces each new version once",
           "[plugins][workbench][E15][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    UpdateFixture fixture;
    if ( !fixture.server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    {
        auto manager = fixture.manager();
        fixture.install( *manager, "1.0.0" );
        Notices notices( *manager );
        // Fetched a moment ago: nothing is due, nothing is fetched or announced.
        const auto before = fixture.catalogRequests();
        REQUIRE( scheduledJob( *manager ).isEmpty() );
        REQUIRE( fixture.catalogRequests() == before );
        REQUIRE( notices.found.isEmpty() );
        REQUIRE( manager->availableUpdates().isEmpty() );
        REQUIRE( pluginInfo( *manager, FixtureId )[ "update" ].isNull() );
    }
    // A new version is published while CILogg is closed; two days pass.
    fixture.publish( { "1.0.0", "1.1.0" } );
    fixture.ageLastCheck();
    {
        auto manager = fixture.manager();
        // The saved catalog is read at start: the repository's packages are known before any
        // fetch, and the installed version is still the newest of them.
        REQUIRE( manager->catalogEntries().size() == 1 );
        REQUIRE( manager->availableUpdates().isEmpty() );
        Notices notices( *manager );
        const auto jobId = scheduledJob( *manager );
        REQUIRE_FALSE( jobId.isEmpty() );
        REQUIRE( waitUntil( [ & ] { return finished( *manager, jobId ); } ) );
        const auto job = manager->job( jobId ).payload;
        REQUIRE( job[ "state" ] == "completed" );
        const auto updates = job[ "result" ].toMap()[ "updates" ].toList();
        REQUIRE( updates.size() == 1 );
        const auto update = updates.first().toMap();
        REQUIRE( update[ "id" ] == FixtureId );
        REQUIRE( update[ "installed" ] == "1.0.0" );
        REQUIRE( update[ "available" ] == "1.1.0" );
        REQUIRE( update[ "repository" ] == "a" );
        REQUIRE_FALSE( update[ "entry_id" ].toString().isEmpty() );
        REQUIRE( update[ "skipped" ] == false );
        REQUIRE( update[ "majorChange" ] == false );
        // One notice, and get_plugins reports the update.
        REQUIRE( notices.found.size() == 1 );
        REQUIRE( notices.found.first().toMap()[ "available" ] == "1.1.0" );
        const auto field = pluginInfo( *manager, FixtureId )[ "update" ].toMap();
        REQUIRE( field[ "version" ] == "1.1.0" );
        REQUIRE( field[ "repository" ] == "a" );
        // Nothing was installed: installing stays a user action without auto-update.
        REQUIRE( pluginInfo( *manager, FixtureId )[ "version" ] == "1.0.0" );
        // The next scheduled run fetches nothing and announces nothing again.
        const auto fetches = fixture.catalogRequests();
        REQUIRE( scheduledJob( *manager ).isEmpty() );
        REQUIRE( fixture.catalogRequests() == fetches );
        REQUIRE( notices.found.size() == 1 );
    }
    {
        // After a restart the update is still known from the saved catalog, and not announced
        // again.
        auto manager = fixture.manager();
        Notices notices( *manager );
        REQUIRE( manager->availableUpdates().size() == 1 );
        REQUIRE( scheduledJob( *manager ).isEmpty() );
        REQUIRE( notices.found.isEmpty() );
        // The saved catalog is the catalog as published, one file per repository.
        const QDir cache( QDir( fixture.data() ).filePath( "catalog-cache" ) );
        const auto files = cache.entryList( QDir::Files );
        REQUIRE( files.size() == 1 );
        REQUIRE( files.first().startsWith( "a-" ) );
        QFile saved( cache.filePath( files.first() ) );
        REQUIRE( saved.open( QIODevice::ReadOnly ) );
        REQUIRE( QJsonDocument::fromJson( saved.readAll() ).object()[ "schema_version" ] == 1 );
        saved.close();
        // Removing the repository removes its saved catalog.
        REQUIRE( manager->manage( "repository_remove", {}, { { "repository", "a" } } ).ok() );
        REQUIRE( cache.entryList( QDir::Files ).isEmpty() );
    }
#endif
}

TEST_CASE( "check_updates and Skip this version", "[plugins][workbench][E15][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    UpdateFixture fixture;
    if ( !fixture.server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    auto manager = fixture.manager();
    fixture.install( *manager, "1.0.0" );
    Notices notices( *manager );
    REQUIRE_FALSE( manager->manage( "check_updates", {}, { { "colour", "blue" } } ).ok() );
    REQUIRE_FALSE( manager->manage( "check_updates", FixtureId, {} ).ok() );
    REQUIRE_FALSE( manager->manage( "check_updates", {}, { { "repository", "nope" } } ).ok() );
    // check_updates fetches whenever it is asked, unlike the silent check.
    fixture.publish( { "1.0.0", "1.1.0" } );
    const auto before = fixture.catalogRequests();
    const auto checked = finishedJob(
        *manager, manager->manage( "check_updates", {}, { { "repository", "a" } } ) );
    REQUIRE( checked[ "state" ] == "completed" );
    REQUIRE( fixture.catalogRequests() == before + 1 );
    REQUIRE( checked[ "result" ].toMap()[ "updates" ].toList().size() == 1 );
    REQUIRE( notices.found.size() == 1 );
    // Skip this version hides it from notices until a newer one appears; Update still works.
    REQUIRE_FALSE( manager->manage( "skip_update", FixtureId, { { "version", "soon" } } ).ok() );
    REQUIRE_FALSE( manager->manage( "skip_update", "org.test.none", { { "version", "1.1.0" } } )
                       .ok() );
    REQUIRE_FALSE(
        manager->manage( "skip_update", FixtureId, { { "version", "1.1.0" }, { "x", 1 } } ).ok() );
    const auto skipped = manager->manage( "skip_update", FixtureId, { { "version", "1.1.0" } } );
    REQUIRE( skipped.ok() );
    REQUIRE( skipped.payload[ "update" ].toMap()[ "skipped" ] == true );
    REQUIRE( pluginInfo( *manager, FixtureId )[ "update" ].toMap()[ "skipped" ] == true );
    REQUIRE( manager->availableUpdates().first().toMap()[ "skipped" ] == true );
    fixture.publish( { "1.0.0", "1.1.0", "1.2.0" } );
    REQUIRE( finishedJob( *manager, manager->manage( "check_updates", {}, {} ) )[ "state" ]
             == "completed" );
    REQUIRE( notices.found.size() == 2 );
    REQUIRE( notices.found.last().toMap()[ "available" ] == "1.2.0" );
    REQUIRE( pluginInfo( *manager, FixtureId )[ "update" ].toMap()[ "skipped" ] == false );
    REQUIRE( finishedJob( *manager, manager->manage( "update", FixtureId, {} ) )[ "state" ]
             == "completed" );
    REQUIRE( pluginInfo( *manager, FixtureId )[ "version" ] == "1.2.0" );
    REQUIRE( pluginInfo( *manager, FixtureId )[ "update" ].isNull() );
    // A disabled source gives no update.
    fixture.publish( { "1.2.0", "1.3.0" } );
    REQUIRE( finishedJob( *manager, manager->manage( "check_updates", {}, {} ) )[ "state" ]
             == "completed" );
    REQUIRE( manager->availableUpdates().size() == 1 );
    REQUIRE(
        manager->manage( "repository_update", {}, { { "repository", "a" }, { "enabled", false } } )
            .ok() );
    REQUIRE( manager->availableUpdates().isEmpty() );
    REQUIRE( pluginInfo( *manager, FixtureId )[ "update" ].isNull() );
#endif
}

TEST_CASE( "Auto-update stays within the major version, installs nothing new and waits for a "
           "running plugin to stop",
           "[plugins][workbench][E15][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    UpdateFixture fixture;
    if ( !fixture.server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    auto manager = fixture.manager();
    fixture.install( *manager, "1.0.0", true );
    Notices notices( *manager );
    // 1.1.0 is installed automatically; 2.0.0 waits for a click; the other plugin is never
    // installed.
    fixture.publish( { "1.0.0", "1.1.0", "2.0.0" }, "org.test.other" );
    const auto checked = finishedJob( *manager, manager->manage( "check_updates", {}, {} ) );
    REQUIRE( checked[ "state" ] == "completed" );
    REQUIRE( waitUntil( [ & ] { return !notices.updated.isEmpty(); } ) );
    REQUIRE( pluginInfo( *manager, FixtureId )[ "version" ] == "1.1.0" );
    REQUIRE( pluginInfo( *manager, FixtureId )[ "source" ].toMap()[ "version" ] == "1.1.0" );
    const auto automatic = notices.updated.first().toMap();
    REQUIRE( automatic[ "id" ] == FixtureId );
    REQUIRE( automatic[ "from" ] == "1.0.0" );
    REQUIRE( automatic[ "to" ] == "1.1.0" );
    REQUIRE( automatic[ "repositoryName" ] == "A" );
    REQUIRE( notices.found.size() == 1 );
    REQUIRE( notices.found.first().toMap()[ "available" ] == "2.0.0" );
    REQUIRE( notices.found.first().toMap()[ "majorChange" ] == true );
    REQUIRE( pluginInfo( *manager, "org.test.other" ).isEmpty() );
    // Configuration and the enabled state are kept: a disabled plugin stays disabled.
    REQUIRE( pluginInfo( *manager, FixtureId )[ "enabled" ] == false );

    // A running plugin is replaced only when it stops.
    REQUIRE( manager->manage( "enable", FixtureId, {} ).ok() );
    REQUIRE( finishedJob( *manager, manager->manage( "restart", FixtureId, {} ) )[ "state" ]
             == "completed" );
    REQUIRE( manager->pluginState( FixtureId ) == "ready" );
    fixture.publish( { "1.1.0", "1.2.0", "2.0.0" } );
    REQUIRE( finishedJob( *manager, manager->manage( "check_updates", {}, {} ) )[ "state" ]
             == "completed" );
    QString waiting;
    REQUIRE( waitUntil( [ & ] {
        for ( const auto& value : manager->jobs() ) {
            const auto j = value.toMap();
            if ( j[ "action" ] == "update" && j[ "result" ].toMap()[ "waiting" ].toBool() ) {
                waiting = j[ "jobId" ].toString();
            }
        }
        return !waiting.isEmpty();
    } ) );
    pump( 300 );
    REQUIRE( manager->pluginState( FixtureId ) == "ready" );
    REQUIRE( pluginInfo( *manager, FixtureId )[ "version" ] == "1.1.0" );
    REQUIRE_FALSE( finished( *manager, waiting ) );
    REQUIRE( manager->manage( "disable", FixtureId, {} ).ok() );
    REQUIRE( waitUntil( [ & ] { return finished( *manager, waiting ); } ) );
    REQUIRE( manager->job( waiting ).payload[ "state" ] == "completed" );
    REQUIRE( pluginInfo( *manager, FixtureId )[ "version" ] == "1.2.0" );
    REQUIRE( pluginInfo( *manager, FixtureId )[ "enabled" ] == false );
    REQUIRE( notices.updated.size() == 2 );
    // 2.0.0 was announced once and is not announced again.
    REQUIRE( notices.found.size() == 1 );
#endif
}

TEST_CASE( "Skip this version and the user's own operations take precedence over auto-update",
           "[plugins][workbench][E15][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    UpdateFixture fixture;
    if ( !fixture.server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    auto manager = fixture.manager();
    fixture.install( *manager, "1.0.0", true );
    Notices notices( *manager );
    const auto staged = [ & ] {
        return QDir( QDir( fixture.data() ).filePath( "plugins" ) )
            .entryList( { ".stage-*" }, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot );
    };
    const auto check = [ & ] {
        REQUIRE( finishedJob( *manager, manager->manage( "check_updates", {}, {} ) )[ "state" ]
                 == "completed" );
    };
    // A new major version is announced; skipping it does not stop a later 1.x from being
    // installed automatically.
    fixture.publish( { "1.0.0", "2.0.0" } );
    check();
    REQUIRE( notices.found.size() == 1 );
    REQUIRE( manager->manage( "skip_update", FixtureId, { { "version", "2.0.0" } } ).ok() );
    fixture.publish( { "1.0.0", "1.5.0", "2.0.0" } );
    check();
    REQUIRE( waitUntil( [ & ] { return !notices.updated.isEmpty(); } ) );
    REQUIRE( notices.updated.first().toMap()[ "to" ] == "1.5.0" );
    REQUIRE( pluginInfo( *manager, FixtureId )[ "version" ] == "1.5.0" );
    REQUIRE( notices.found.size() == 1 ); // 2.0.0 stays skipped

    // An automatic update waiting for the running plugin: cancelling it removes its package.
    REQUIRE( manager->manage( "enable", FixtureId, {} ).ok() );
    REQUIRE( finishedJob( *manager, manager->manage( "restart", FixtureId, {} ) )[ "state" ]
             == "completed" );
    const auto waitingJob = [ & ] {
        QString found;
        for ( const auto& value : manager->jobs() ) {
            const auto j = value.toMap();
            if ( j[ "action" ] == "update" && j[ "result" ].toMap()[ "waiting" ].toBool()
                 && !finished( *manager, j[ "jobId" ].toString() ) ) {
                found = j[ "jobId" ].toString();
            }
        }
        return found;
    };
    fixture.publish( { "1.5.0", "1.6.0", "2.0.0" } );
    check();
    REQUIRE( waitUntil( [ & ] { return !waitingJob().isEmpty(); } ) );
    REQUIRE( staged().size() == 1 );
    const auto cancelled = waitingJob();
    REQUIRE( manager->cancel( cancelled ).ok() );
    REQUIRE( manager->job( cancelled ).payload[ "state" ] == "cancelled" );
    REQUIRE( staged().isEmpty() );
    REQUIRE( pluginInfo( *manager, FixtureId )[ "version" ] == "1.5.0" );

    // A new fetch tries it again; a Remove the user starts replaces it.
    check();
    REQUIRE( waitUntil( [ & ] { return !waitingJob().isEmpty(); } ) );
    const auto replaced = waitingJob();
    REQUIRE( finishedJob( *manager, manager->manage( "remove", FixtureId, {} ) )[ "state" ]
             == "completed" );
    REQUIRE( manager->job( replaced ).payload[ "state" ] == "cancelled" );
    REQUIRE( staged().isEmpty() );
    REQUIRE( pluginInfo( *manager, FixtureId ).isEmpty() );
    REQUIRE( notices.updated.size() == 1 );
    // A removed plugin keeps no skipped or announced version.
    QSettings settings( QDir( fixture.data() ).filePath( "settings.ini" ), QSettings::IniFormat );
    REQUIRE_FALSE( settings.contains( "plugins/org.test.fixture/skippedUpdate" ) );
    REQUIRE_FALSE( settings.contains( "plugins/org.test.fixture/announcedUpdate" ) );
#endif
}

TEST_CASE( "A failed automatic update is reported once and tried again at the next check",
           "[plugins][workbench][E15][https]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    UpdateFixture fixture;
    if ( !fixture.server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    auto manager = fixture.manager();
    fixture.install( *manager, "1.0.0", true );
    Notices notices( *manager );
    // The catalog names a package whose SHA-256 does not match.
    const auto good = fixturePackage( fixture.temp.path(), FixtureId, "1.1.0" );
    auto broken = entryFor( FixtureId, "1.1.0", fixture.server.url( "/a/broken.zip" ), good );
    fixture.server.route( "/a/broken.zip", { 200, QByteArray( "not this one" ), "application/zip",
                                             0 } );
    fixture.server.route( "/a/plugins.json",
                          { 200, catalogBytes( { broken } ), "application/json", 0 } );
    for ( int check = 0; check < 2; ++check ) {
        REQUIRE( finishedJob( *manager, manager->manage( "check_updates", {}, {} ) )[ "state" ]
                 == "completed" );
        REQUIRE( waitUntil( [ & ] {
            const auto jobs = manager->jobs();
            return fixture.server.requestsFor( "/a/broken.zip" ).size() == check + 1
                   && std::none_of( jobs.cbegin(), jobs.cend(), []( const QVariant& value ) {
                          const auto state = value.toMap()[ "state" ];
                          return state == "pending" || state == "running";
                      } );
        } ) );
    }
    // Tried at both checks, reported once.
    REQUIRE( fixture.server.requestsFor( "/a/broken.zip" ).size() == 2 );
    REQUIRE( notices.failed.size() == 1 );
    const auto failed = notices.failed.first().toMap();
    REQUIRE( failed[ "id" ] == FixtureId );
    REQUIRE( failed[ "to" ] == "1.1.0" );
    REQUIRE( failed[ "error" ].toString().contains( "SHA-256" ) );
    REQUIRE( pluginInfo( *manager, FixtureId )[ "version" ] == "1.0.0" );
    // A silent run without a new fetch does not download it again.
    REQUIRE( scheduledJob( *manager ).isEmpty() );
    pump( 200 );
    REQUIRE( fixture.server.requestsFor( "/a/broken.zip" ).size() == 2 );
#endif
}

TEST_CASE( "The silent check runs only in an interactive process with the option on",
           "[plugins][workbench][E15]" )
{
    const auto previous = qgetenv( "CILOGG_AUTOMATION" );
    const bool hadPrevious = qEnvironmentVariableIsSet( "CILOGG_AUTOMATION" );
    const auto restore = qScopeGuard( [ & ] {
        if ( hadPrevious ) {
            qputenv( "CILOGG_AUTOMATION", previous );
        }
        else {
            qunsetenv( "CILOGG_AUTOMATION" );
        }
    } );
    qunsetenv( "CILOGG_AUTOMATION" );
    REQUIRE( PluginUpdateChecker::mayRun( false, "windows" ) );
    REQUIRE( PluginUpdateChecker::mayRun( false, "xcb" ) );
    REQUIRE_FALSE( PluginUpdateChecker::mayRun( true, "windows" ) );
    REQUIRE_FALSE( PluginUpdateChecker::mayRun( false, "offscreen" ) );
    REQUIRE_FALSE( PluginUpdateChecker::mayRun( false, "Minimal" ) );
    qputenv( "CILOGG_AUTOMATION", "1" );
    REQUIRE_FALSE( PluginUpdateChecker::mayRun( false, "windows" ) );

    QTemporaryDir temp;
    PluginManager manager( QDir( temp.path() ).filePath( "data" ),
                           QDir( temp.path() ).filePath( "none" ) );
    REQUIRE( manager.manage( "repository_remove", {}, { { "repository", "default" } } ).ok() );
    bool allowed = false;
    int runs = 0;
    PluginUpdateChecker checker( &manager, [ &allowed ] { return allowed; } );
    QObject::connect( &checker, &PluginUpdateChecker::checked, [ &runs ] { ++runs; } );
    // Turned off in the options: the timer keeps running, no check happens.
    checker.start( 0, 20 );
    pump( 120 );
    REQUIRE( checker.isActive() );
    REQUIRE( runs == 0 );
    allowed = true;
    REQUIRE( waitUntil( [ & ] { return runs >= 2; }, 5000 ) );
    checker.stop();
    REQUIRE_FALSE( checker.isActive() );
}
