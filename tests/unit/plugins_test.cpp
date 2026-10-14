#include "cli.h"
#include "commander.h"
#include "pluginmanager.h"
#include "pluginrepository.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <catch2/catch.hpp>
#include <cstring>
#include <kzip.h>

using namespace cilogg::plugins;
namespace {
class MemoryReply final : public QNetworkReply {
public:
    MemoryReply( const QNetworkRequest& request, QByteArray data, QObject* parent, int delay = 0 )
        : QNetworkReply( parent )
        , data_( std::move( data ) )
    {
        setRequest( request );
        setUrl( request.url() );
        open( QIODevice::ReadOnly );
        QTimer::singleShot( delay, this, [ this ] {
            if ( !isFinished() ) {
                Q_EMIT readyRead();
                setFinished( true );
                Q_EMIT finished();
            }
        } );
    }
    void abort() override
    {
        if ( !isFinished() ) {
            setError( OperationCanceledError, "cancelled" );
            setFinished( true );
            Q_EMIT finished();
        }
    }
    qint64 bytesAvailable() const override
    {
        return data_.size() + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData( char* destination, qint64 size ) override
    {
        const auto count = qMin<qint64>( size, data_.size() );
        if ( count == 0 ) {
            return -1;
        }
        std::memcpy( destination, data_.constData(), size_t( count ) );
        data_.remove( 0, count );
        return count;
    }

private:
    QByteArray data_;
};
class MemoryNetwork final : public QNetworkAccessManager {
public:
    QMap<QString, QByteArray> responses;
    QMap<QString, int> delays;

protected:
    QNetworkReply* createRequest( Operation, const QNetworkRequest& request, QIODevice* ) override
    {
        return new MemoryReply( request, responses.value( request.url().toString() ), this,
                                delays.value( request.url().toString() ) );
    }
};
bool waitFor( const std::function<bool()>& predicate, int timeout = 5000 )
{
    QElapsedTimer timer;
    timer.start();
    while ( !predicate() && timer.elapsed() < timeout ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 2 );
    }
    return predicate();
}
QJsonObject manifest( const QString& id = "org.test.fixture" )
{
    return { { "id", id },
             { "name", "Fixture" },
             { "version", "1.0.0" },
             { "protocol_version", 1 },
             { "executable", QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName() },
             { "arguments", QJsonArray{} },
             { "capabilities", QJsonArray{ "datasource", "converter", "tool" } },
             { "configuration", QJsonArray{} },
             { "actions",
               QJsonArray{ QJsonObject{ { "id", "source" }, { "capability", "datasource" } },
                           QJsonObject{ { "id", "convert" }, { "capability", "converter" } },
                           QJsonObject{ { "id", "summary" }, { "capability", "tool" } },
                           QJsonObject{ { "id", "wait" }, { "capability", "tool" } },
                           QJsonObject{ { "id", "crash" }, { "capability", "tool" } } } } };
}
void writeFile( const QString& path, const QByteArray& bytes )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
}
QString package( const QString& root, const QString& id = "org.test.fixture",
                 const QString& mode = {} )
{
    const auto directory = QDir( root ).filePath( id );
    REQUIRE( QDir().mkpath( directory ) );
    auto json = manifest( id );
    if ( !mode.isEmpty() ) {
        json[ "arguments" ] = QJsonArray{ mode };
    }
    writeFile( QDir( directory ).filePath( "plugin.json" ), QJsonDocument( json ).toJson() );
    REQUIRE( QFile::copy( QString::fromUtf8( PLUGIN_FIXTURE_PATH ),
                          QDir( directory ).filePath( json[ "executable" ].toString() ) ) );
    return directory;
}
QString jobId( const Result& result )
{
    REQUIRE( result.ok() );
    return result.payload.value( "jobId" ).toString();
}
bool finished( PluginManager& manager, const QString& id )
{
    const auto state = manager.job( id ).payload.value( "state" ).toString();
    return state == "completed" || state == "failed" || state == "cancelled";
}
} // namespace

TEST_CASE( "Preinstalled plugins are enabled lazily and preserve saved disable choices",
           "[plugins][preinstalled]" )
{
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    const auto root = QDir( temp.path() ).filePath( "data" );
    const QStringList defaults{ "org.cilogg.adb-logcat", "org.cilogg.syslog-server" };
    for ( const auto& id : defaults ) {
        const auto folder = package( shipped, id );
        auto json = manifest( id );
        json[ "enabled_by_default" ] = true;
        writeFile( QDir( folder ).filePath( "plugin.json" ), QJsonDocument( json ).toJson() );
    }
    package( shipped );
    {
        PluginManager manager( root, shipped );
        REQUIRE( manager.plugins().size() == 3 );
        for ( const auto& value : manager.plugins() ) {
            const auto plugin = value.toMap();
            REQUIRE( plugin[ "enabled" ].toBool()
                     == defaults.contains( plugin[ "id" ].toString() ) );
            REQUIRE( plugin[ "state" ] == "stopped" );
            REQUIRE_FALSE( plugin.contains( "pid" ) );
        }
        REQUIRE( manager.manage( "disable", defaults.front(), {} ).ok() );
        manager.discover();
        for ( const auto& value : manager.plugins() ) {
            const auto plugin = value.toMap();
            if ( plugin[ "id" ] == defaults.front() ) {
                REQUIRE_FALSE( plugin[ "enabled" ].toBool() );
            }
        }
    }
    PluginManager restored( root, shipped );
    for ( const auto& value : restored.plugins() ) {
        const auto plugin = value.toMap();
        REQUIRE( plugin[ "enabled" ].toBool() == ( plugin[ "id" ] == defaults.back() ) );
    }
}

TEST_CASE( "Preinstalled defaults apply to upgrades without enabling user-only packages",
           "[plugins][preinstalled]" )
{
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    const auto root = QDir( temp.path() ).filePath( "data" );
    REQUIRE( QDir().mkpath( root ) );
    const QString id = "org.cilogg.adb-logcat";
    const auto bundled = package( shipped, id );
    auto json = manifest( id );
    json[ "enabled_by_default" ] = true;
    writeFile( QDir( bundled ).filePath( "plugin.json" ), QJsonDocument( json ).toJson() );
    const auto userRoot = QDir( root ).filePath( "plugins" );
    const auto updated = package( userRoot, id );
    auto update = manifest( id );
    update[ "version" ] = "1.1.0";
    writeFile( QDir( updated ).filePath( "plugin.json" ), QJsonDocument( update ).toJson() );
    const auto userOnly = package( userRoot, "org.test.user" );
    auto userManifest = manifest( "org.test.user" );
    userManifest[ "enabled_by_default" ] = true;
    writeFile( QDir( userOnly ).filePath( "plugin.json" ), QJsonDocument( userManifest ).toJson() );
    QSettings settings( QDir( root ).filePath( "settings.ini" ), QSettings::IniFormat );
    settings.setValue( "catalogUrl", "https://example.test/existing-catalog.json" );
    settings.sync();
    PluginManager manager( root, shipped );
    REQUIRE( manager.plugins().size() == 2 );
    for ( const auto& value : manager.plugins() ) {
        const auto plugin = value.toMap();
        if ( plugin[ "id" ] == id ) {
            REQUIRE( plugin[ "enabled" ].toBool() );
            REQUIRE( plugin[ "version" ] == "1.1.0" );
            REQUIRE( plugin[ "directory" ] == updated );
        }
        else {
            REQUIRE_FALSE( plugin[ "enabled" ].toBool() );
        }
    }
    REQUIRE( manager.catalogUrl() == "https://example.test/existing-catalog.json" );
}

TEST_CASE( "Invalid overrides do not inherit preinstalled enable defaults",
           "[plugins][preinstalled]" )
{
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    const auto root = QDir( temp.path() ).filePath( "data" );
    const QString id = "org.cilogg.syslog-server";
    const auto bundled = package( shipped, id );
    auto json = manifest( id );
    json[ "enabled_by_default" ] = true;
    writeFile( QDir( bundled ).filePath( "plugin.json" ), QJsonDocument( json ).toJson() );
    const auto user = package( QDir( root ).filePath( "plugins" ), id );
    json[ "protocol_version" ] = 999;
    writeFile( QDir( user ).filePath( "plugin.json" ), QJsonDocument( json ).toJson() );
    PluginManager manager( root, shipped );
    REQUIRE( manager.plugins().size() == 1 );
    const auto plugin = manager.plugins().front().toMap();
    REQUIRE_FALSE( plugin[ "enabled" ].toBool() );
    REQUIRE_FALSE( plugin[ "error" ].toString().isEmpty() );
}

TEST_CASE( "Plugin initialization applies advertised configuration before invocation", "[plugins]" )
{
    QTemporaryDir root;
    QTemporaryDir shipped;
    package( QDir( root.path() ).filePath( "plugins" ), "org.test.fixture", "dynamic-config" );
    PluginManager manager( root.path(), shipped.path() );
    REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
    const auto first = jobId( manager.invoke( "org.test.fixture", "summary", {}, {} ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, first ); } ) );
    REQUIRE( manager.job( first )
                 .payload.value( "result" )
                 .toMap()
                 .value( "configuration" )
                 .toMap()
                 .value( "count" )
                 .toInt()
             == 7 );
    REQUIRE( manager.manage( "disable", "org.test.fixture", {} ).ok() );
    REQUIRE( waitFor(
        [ & ] { return manager.plugins().front().toMap().value( "state" ) == "stopped"; } ) );
    REQUIRE_FALSE( manager
                       .manage( "configure", "org.test.fixture",
                                { { "configuration", QVariantMap{ { "count", "bad" } } } } )
                       .ok() );
    REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
    const auto second = jobId( manager.invoke( "org.test.fixture", "summary", {}, {} ) );
    REQUIRE( manager
                 .manage( "configure", "org.test.fixture",
                          { { "configuration", QVariantMap{ { "count", 9 } } } } )
                 .ok() );
    REQUIRE( waitFor( [ & ] { return finished( manager, second ); } ) );
    REQUIRE( manager.job( second )
                 .payload.value( "result" )
                 .toMap()
                 .value( "configuration" )
                 .toMap()
                 .value( "count" )
                 .toInt()
             == 9 );
}

TEST_CASE( "Plugin manifests reject invalid protocol, paths and fields", "[plugins]" )
{
    auto json = manifest();
    REQUIRE( Manifest::parse( QJsonDocument( json ).toJson(), "test" ).valid() );
    for ( const auto& path : { "../evil", "/evil", "C:/evil", "dir\\evil", "" } ) {
        json[ "executable" ] = path;
        REQUIRE_FALSE( Manifest::parse( QJsonDocument( json ).toJson(), "test" ).valid() );
    }
    json = manifest();
    json[ "protocol_version" ] = 2;
    REQUIRE_FALSE( Manifest::parse( QJsonDocument( json ).toJson(), "test" ).valid() );
    QJsonObject values;
    QJsonArray fields{ QJsonObject{
        { "id", "count" }, { "type", "integer" }, { "minimum", 1 }, { "default", 2 } } };
    REQUIRE( validateValues( fields, &values ).isEmpty() );
    REQUIRE( values[ "count" ] == 2 );
    values[ "count" ] = 0;
    REQUIRE_FALSE( validateValues( fields, &values ).isEmpty() );
}

TEST_CASE( "Plugin jobs stream, convert and render using a stable target", "[plugins]" )
{
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const auto shipped = QDir( temporary.path() ).filePath( "shipped" );
    package( shipped );
    PluginManager manager( QDir( temporary.path() ).filePath( "data" ), shipped );
    const QVariantMap context{ { "windowId", "window-2" },
                               { "windowIndex", 2 },
                               { "tabId", "stable-tab" } };
    REQUIRE_FALSE( manager.invoke( "org.test.fixture", "summary", {}, context ).ok() );
    REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
    QSignalSpy files( &manager, &PluginManager::fileReady );
    QSignalSpy panels( &manager, &PluginManager::panelUpdated );
    const auto source = jobId( manager.invoke( "org.test.fixture", "source", {}, context ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, source ); } ) );
    REQUIRE( manager.job( source ).payload.value( "state" ) == "completed" );
    REQUIRE( files.size() == 1 );
    REQUIRE( files[ 0 ][ 1 ].toBool() );
    REQUIRE( files[ 0 ][ 2 ].toMap() == context );
    QFile capture( files[ 0 ][ 0 ].toString() );
    REQUIRE( capture.open( QIODevice::ReadOnly ) );
    REQUIRE( capture.readAll().startsWith( "one\ntwo\nUnicode:" ) );
    capture.close();
    const auto input = QDir( temporary.path() ).filePath( "input.log" );
    writeFile( input, "original\n" );
    const auto conversion = jobId(
        manager.invoke( "org.test.fixture", "convert", { { "input_path", input } }, context ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, conversion ); } ) );
    REQUIRE( files.size() == 2 );
    QFile converted( files[ 1 ][ 0 ].toString() );
    REQUIRE( converted.open( QIODevice::ReadOnly ) );
    REQUIRE( converted.readAll() == "ORIGINAL\n" );
    converted.close();
    QFile original( input );
    REQUIRE( original.open( QIODevice::ReadOnly ) );
    REQUIRE( original.readAll() == "original\n" );
    const auto summary = jobId( manager.invoke( "org.test.fixture", "summary", {}, context ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, summary ); } ) );
    REQUIRE( panels.size() == 1 );
    REQUIRE( panels[ 0 ][ 2 ].toMap() == context );
    const auto waiting = jobId( manager.invoke( "org.test.fixture", "wait", {}, context ) );
    REQUIRE( manager.cancel( waiting ).ok() );
    REQUIRE( manager.job( waiting ).payload.value( "state" ) == "cancelled" );
    manager.shutdown();
    REQUIRE(
        waitFor( [ & ] { return manager.plugins()[ 0 ].toMap().value( "state" ) == "stopped"; } ) );
    REQUIRE( QFileInfo::exists( capture.fileName() ) );
}

TEST_CASE( "Plugin transport handles fragmented replies and isolates failures", "[plugins]" )
{
    for ( const auto& mode :
          QStringList{ "fragmented", "malformed", "oversize", "incompatible" } ) {
        CAPTURE( mode );
        QTemporaryDir temp;
        const auto shipped = QDir( temp.path() ).filePath( "shipped" );
        package( shipped, "org.test.fixture", mode );
        PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
        REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
        const auto id = jobId( manager.invoke( "org.test.fixture", "summary", {}, {} ) );
        REQUIRE( waitFor( [ & ] { return finished( manager, id ); } ) );
        REQUIRE( manager.job( id ).payload.value( "state" )
                 == ( mode == "fragmented" ? "completed" : "failed" ) );
    }
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    package( shipped );
    package( shipped, "org.test.other" );
    PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
    REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
    REQUIRE( manager.manage( "enable", "org.test.other", {} ).ok() );
    const auto crashed = jobId( manager.invoke( "org.test.fixture", "crash", {}, {} ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, crashed ); } ) );
    REQUIRE( manager.job( crashed ).payload.value( "state" ) == "failed" );
    const auto surviving = jobId( manager.invoke( "org.test.other", "summary", {}, {} ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, surviving ); } ) );
    REQUIRE( manager.job( surviving ).payload.value( "state" ) == "completed" );
}

TEST_CASE( "Plugin folder and ZIP installs preserve settings and reject unsafe entries",
           "[plugins]" )
{
    QTemporaryDir temp;
    const auto source = package( QDir( temp.path() ).filePath( "source" ) );
    const auto data = QDir( temp.path() ).filePath( "data" );
    const auto root = QDir( data ).filePath( "plugins" );
    auto staged = PluginRepository::stage( source, root );
    REQUIRE( staged.error.isEmpty() );
    REQUIRE( PluginRepository::commit( staged, root ).isEmpty() );
    {
        PluginManager manager( data, QDir( temp.path() ).filePath( "none" ) );
        REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
        REQUIRE( manager
                     .manage( "configure", "org.test.fixture",
                              { { "configuration", QVariantMap{ { "custom", "retained" } } } } )
                     .ok() );
    }
    staged = PluginRepository::stage( source, root );
    REQUIRE( staged.error.isEmpty() );
    REQUIRE( PluginRepository::commit( staged, root ).isEmpty() );
    PluginManager restored( data, QDir( temp.path() ).filePath( "none" ) );
    REQUIRE( restored.plugins()[ 0 ].toMap().value( "enabled" ).toBool() );
    REQUIRE(
        restored.plugins()[ 0 ].toMap().value( "configurationValues" ).toMap().value( "custom" )
        == "retained" );
    const auto archive = QDir( temp.path() ).filePath( "bad.zip" );
    KZip zip( archive );
    REQUIRE( zip.open( QIODevice::WriteOnly ) );
    REQUIRE( zip.writeFile( "../escape", QByteArray( "bad" ) ) );
    REQUIRE( zip.close() );
    REQUIRE_FALSE( PluginRepository::stage( archive, root ).error.isEmpty() );
    REQUIRE_FALSE( QFileInfo::exists( QDir( temp.path() ).filePath( "escape" ) ) );
    REQUIRE_FALSE(
        restored.manage( "catalog", {}, { { "url", "http://example.com/plugins.json" } } ).ok() );
}

TEST_CASE( "Plugin Commander fields and CLI options round trip", "[plugins][commander]" )
{
    for ( const auto& name : { "get_plugins", "plugin_manage", "invoke_plugin", "get_plugin_job",
                               "cancel_plugin_job" } ) {
        const auto action = commanderActionFromString( name );
        REQUIRE( action );
        REQUIRE( commanderActionToString( *action ) == name );
        CommanderRequest request;
        request.action = *action;
        request.pluginId = "org.test.fixture";
        request.pluginAction = "source";
        request.pluginOperation = "enable";
        request.pluginJobId = "job";
        request.pluginArguments = { { "count", 5 } };
        const auto restored
            = commanderRequestFromVariantMap( commanderRequestToVariantMap( request ) );
        REQUIRE( restored );
        REQUIRE( restored->pluginArguments == request.pluginArguments );
        REQUIRE( restored->pluginId == request.pluginId );
    }
    const CliParameters parsed( QStringList{ "cilogg", "command", "--action", "invoke_plugin",
                                             "--plugin-id", "org.test.fixture", "--plugin-action",
                                             "source" } );
    REQUIRE( parsed.commander_request );
    REQUIRE( parsed.commander_request->pluginAction == "source" );
    REQUIRE( commanderActionToString( CommanderAction::None ).isEmpty() );
}

TEST_CASE( "Plugins restart after re-enable and cancel before initialization", "[plugins]" )
{
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    const auto folder = package( shipped, "org.test.fixture", "slow" );
    PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
    REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
    const auto cancelled = jobId( manager.invoke( "org.test.fixture", "summary", {}, {} ) );
    REQUIRE( manager.cancel( cancelled ).ok() );
    REQUIRE(
        waitFor( [ & ] { return manager.plugins()[ 0 ].toMap().value( "state" ) == "ready"; } ) );
    REQUIRE_FALSE( QFileInfo::exists( QDir( folder ).filePath( "invoked" ) ) );
    REQUIRE( manager.job( cancelled ).payload[ "state" ] == "cancelled" );
    REQUIRE( manager.manage( "disable", "org.test.fixture", {} ).ok() );
    REQUIRE(
        waitFor( [ & ] { return manager.plugins()[ 0 ].toMap().value( "state" ) == "stopped"; } ) );
    REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
    const auto restarted = jobId( manager.invoke( "org.test.fixture", "summary", {}, {} ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, restarted ); } ) );
    REQUIRE( manager.job( restarted ).payload[ "state" ] == "completed" );
    const auto restart = manager.manage( "restart", "org.test.fixture", {} );
    REQUIRE( restart.ok() );
    REQUIRE_FALSE( manager.manage( "restart", "org.test.fixture", {} ).ok() );
    REQUIRE_FALSE( manager.manage( "remove", "org.test.fixture", {} ).ok() );
    REQUIRE(
        waitFor( [ & ] { return finished( manager, restart.payload[ "jobId" ].toString() ); } ) );
    REQUIRE( manager.job( restart.payload[ "jobId" ].toString() ).payload[ "state" ]
             == "completed" );
    REQUIRE( manager.plugins()[ 0 ].toMap()[ "state" ] == "ready" );
    auto incompatible = manifest( "org.test.wrong-identity" );
    writeFile( QDir( folder ).filePath( "plugin.json" ), QJsonDocument( incompatible ).toJson() );
    const auto failedRestart = jobId( manager.manage( "restart", "org.test.fixture", {} ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, failedRestart ); } ) );
    REQUIRE( manager.job( failedRestart ).payload[ "state" ] == "failed" );
}

TEST_CASE( "Closing a target prevents delayed action execution", "[plugins]" )
{
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    const auto folder = package( shipped, "org.test.fixture", "slow" );
    PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
    REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
    const auto id
        = jobId( manager.invoke( "org.test.fixture", "source", {}, { { "windowId", "closed" } } ) );
    manager.closeWindow( "closed" );
    REQUIRE(
        waitFor( [ & ] { return manager.plugins()[ 0 ].toMap().value( "state" ) == "ready"; } ) );
    REQUIRE( manager.job( id ).payload[ "state" ] == "cancelled" );
    REQUIRE_FALSE( QFileInfo::exists( QDir( folder ).filePath( "invoked" ) ) );
}

TEST_CASE( "Plugins enforce initialization and shutdown deadlines", "[plugins]" )
{
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    SECTION( "initialization" )
    {
        package( shipped, "org.test.fixture", "silent" );
        PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
        REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
        const auto id = jobId( manager.invoke( "org.test.fixture", "summary", {}, {} ) );
        REQUIRE( waitFor( [ & ] { return finished( manager, id ); }, 12000 ) );
        REQUIRE( manager.job( id ).payload[ "error" ].toString().contains( "timed out" ) );
    }
    SECTION( "graceful deadline and destructor" )
    {
        const auto folder = package( shipped, "org.test.fixture", "hang-shutdown" );
        QElapsedTimer elapsed;
        {
            PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
            REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
            const auto id = jobId( manager.invoke( "org.test.fixture", "summary", {}, {} ) );
            REQUIRE( waitFor( [ & ] { return finished( manager, id ); } ) );
            elapsed.start();
        }
        REQUIRE( elapsed.elapsed() < 4500 );
        REQUIRE( QFileInfo::exists( QDir( folder ).filePath( "shutdown.received" ) ) );
    }
    SECTION( "invalid response" )
    {
        package( shipped, "org.test.fixture", "invalid-response" );
        PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
        REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
        const auto id = jobId( manager.invoke( "org.test.fixture", "summary", {}, {} ) );
        REQUIRE( waitFor( [ & ] { return finished( manager, id ); } ) );
        REQUIRE( manager.job( id ).payload[ "state" ] == "failed" );
    }
    SECTION( "failed startup" )
    {
        const auto folder = package( shipped );
        REQUIRE(
            QFile::remove( QDir( folder ).filePath( manifest()[ "executable" ].toString() ) ) );
        PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
        REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
        const auto id = jobId( manager.invoke( "org.test.fixture", "summary", {}, {} ) );
        REQUIRE( waitFor( [ & ] { return finished( manager, id ); } ) );
        REQUIRE( manager.job( id ).payload[ "state" ] == "failed" );
    }
}

TEST_CASE( "Catalog selects platform, verifies checksums and preserves old installs", "[plugins]" )
{
    QTemporaryDir temp;
    const auto source = package( QDir( temp.path() ).filePath( "source" ) );
    const auto zipPath = QDir( temp.path() ).filePath( "package.zip" );
    KZip zip( zipPath );
    REQUIRE( zip.open( QIODevice::WriteOnly ) );
    REQUIRE( zip.addLocalDirectory( source, "" ) );
    REQUIRE( zip.close() );
    QFile archive( zipPath );
    REQUIRE( archive.open( QIODevice::ReadOnly ) );
    const auto bytes = archive.readAll();
    MemoryNetwork network;
    network.responses[ "https://test.invalid/package.zip" ] = bytes;
    const auto root = QDir( temp.path() ).filePath( "data" );
    PluginManager manager( root, QDir( temp.path() ).filePath( "empty" ), nullptr, &network );
#ifdef Q_OS_WIN
    const auto os = "windows";
#elif defined( Q_OS_MAC )
    const auto os = "macos";
#else
    const auto os = "linux";
#endif
    QJsonObject entry{
        { "id", "org.test.fixture" },
        { "version", "1.0.0" },
        { "protocol_version", 1 },
        { "os", os },
        { "arch", QSysInfo::buildCpuArchitecture() },
        { "url", "https://test.invalid/package.zip" },
        { "sha256", QString::fromLatin1(
                        QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() ) }
    };
    const auto catalog = [ & ]( QJsonArray entries ) {
        network.responses[ "https://test.invalid/catalog.json" ]
            = QJsonDocument( QJsonObject{ { "schema_version", 1 }, { "plugins", entries } } )
                  .toJson();
        const auto id = jobId(
            manager.manage( "catalog", {}, { { "url", "https://test.invalid/catalog.json" } } ) );
        REQUIRE( waitFor( [ & ] { return finished( manager, id ); } ) );
        return manager.job( id );
    };
    auto wrongPlatform = entry;
    wrongPlatform[ "arch" ] = "unknown";
    REQUIRE(
        catalog( { entry, wrongPlatform } ).payload[ "result" ].toMap()[ "plugins" ].toList().size()
        == 1 );
    const auto install = jobId( manager.manage( "install", "org.test.fixture", {} ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, install ); } ) );
    REQUIRE( manager.job( install ).payload[ "state" ] == "completed" );
    REQUIRE_FALSE( manager.plugins()[ 0 ].toMap()[ "enabled" ].toBool() );
    REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
    REQUIRE( manager
                 .manage( "configure", "org.test.fixture",
                          { { "configuration", QVariantMap{ { "retained", true } } } } )
                 .ok() );
    auto invalid = entry;
    invalid[ "sha256" ] = QString( 64, '0' );
    REQUIRE( catalog( { invalid } ).payload[ "state" ] == "completed" );
    const auto failed = jobId( manager.manage( "update", "org.test.fixture", {} ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, failed ); } ) );
    REQUIRE( manager.job( failed ).payload[ "error" ].toString().contains( "SHA-256" ) );
    REQUIRE( manager.plugins()[ 0 ].toMap()[ "enabled" ].toBool() );
    REQUIRE(
        manager.plugins()[ 0 ].toMap()[ "configurationValues" ].toMap()[ "retained" ].toBool() );
    invalid.remove( "sha256" );
    REQUIRE( catalog( { invalid } ).payload[ "state" ] == "failed" );
}

TEST_CASE( "User overrides remain visible and failed commits roll back", "[plugins]" )
{
    QTemporaryDir temp;
    const auto root = QDir( temp.path() ).filePath( "data" );
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    package( shipped );
    const auto user = package( QDir( root ).filePath( "plugins" ) );
    auto json = manifest();
    json[ "protocol_version" ] = 99;
    writeFile( QDir( user ).filePath( "plugin.json" ), QJsonDocument( json ).toJson() );
    PluginManager manager( root, shipped );
    REQUIRE( manager.plugins().size() == 1 );
    REQUIRE_FALSE( manager.plugins()[ 0 ].toMap()[ "error" ].toString().isEmpty() );
    REQUIRE_FALSE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
    const auto staged = PluginRepository::stage( QDir( shipped ).filePath( "org.test.fixture" ),
                                                 QDir( root ).filePath( "plugins" ) );
    REQUIRE( staged.error.isEmpty() );
    REQUIRE( QDir( QDir( staged.directory ).filePath( "package" ) ).removeRecursively() );
    REQUIRE( PluginRepository::commit( staged, QDir( root ).filePath( "plugins" ) )
                 .contains( "restored" ) );
    REQUIRE( Manifest::read( user ).json[ "protocol_version" ] == 99 );
}

TEST_CASE( "Portable plugin storage follows resolved settings format", "[plugins]" )
{
    QTemporaryDir temp;
    const auto path = QDir( temp.path() ).filePath( "cilogg.conf" );
    QSettings portable( path, QSettings::IniFormat );
    REQUIRE( PluginRepository::dataRootForSettings( portable )
             == QDir( temp.path() ).filePath( "plugin-data" ) );
#ifndef Q_OS_WIN
    QSettings ordinary( path, QSettings::NativeFormat );
    REQUIRE( PluginRepository::dataRootForSettings( ordinary )
             != PluginRepository::dataRootForSettings( portable ) );
#endif
}

TEST_CASE( "Case-alias plugin packages cannot replace canonical identities", "[plugins]" )
{
    QTemporaryDir temp;
    const auto canonical = package( QDir( temp.path() ).filePath( "canonical" ) );
    const auto alias = package( QDir( temp.path() ).filePath( "alias" ), "org.test.Fixture" );
    REQUIRE_FALSE( Manifest::read( alias ).valid() );
    PluginManager manager( QDir( temp.path() ).filePath( "data" ),
                           QDir( temp.path() ).filePath( "empty" ) );
    const auto first
        = jobId( manager.manage( "install", "org.test.fixture", { { "path", canonical } } ) );
    const auto collision
        = jobId( manager.manage( "install", "org.test.Fixture", { { "path", alias } } ) );
    REQUIRE(
        waitFor( [ & ] { return finished( manager, first ) && finished( manager, collision ); } ) );
    REQUIRE( manager.job( first ).payload[ "state" ] == "completed" );
    REQUIRE( manager.job( collision ).payload[ "state" ] == "failed" );
    const auto retry = jobId( manager.manage( "install", {}, { { "path", alias } } ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, retry ); } ) );
    REQUIRE( manager.job( retry ).payload[ "state" ] == "failed" );
    REQUIRE( manager.plugins().size() == 1 );
    REQUIRE( manager.plugins()[ 0 ].toMap()[ "id" ] == "org.test.fixture" );
}

TEST_CASE( "Native SDK producers block until opened and acknowledged", "[plugins]" )
{
    QTemporaryDir temp;
    const auto folder = package( temp.path() );
    PluginProcess process( Manifest::read( folder ) );
    QSignalSpy messages( &process, &PluginProcess::message );
    process.start( {}, temp.path(), {} );
    REQUIRE( waitFor( [ & ] { return process.state() == "ready"; } ) );
    process.request( "source-job", "invoke",
                     { { "job_id", "source-job" },
                       { "action", "source" },
                       { "arguments", QJsonObject{} },
                       { "context", QJsonObject{} } } );
    REQUIRE( waitFor( [ & ] { return !messages.isEmpty(); } ) );
    const auto opened = messages.takeFirst()[ 0 ].toJsonObject();
    REQUIRE( opened[ "method" ] == "stream/open" );
    const auto stream = opened[ "params" ].toObject()[ "stream_id" ].toString();
    QTest::qWait( 100 );
    REQUIRE( messages.isEmpty() );
    process.notify( "stream/opened", { { "stream_id", stream } } );
    REQUIRE( waitFor( [ & ] { return !messages.isEmpty(); } ) );
    REQUIRE( messages.takeFirst()[ 0 ].toJsonObject()[ "method" ] == "stream/data" );
    QTest::qWait( 100 );
    REQUIRE( messages.isEmpty() );
    process.notify( "stream/ack", { { "stream_id", stream }, { "sequence", 0 } } );
    REQUIRE( waitFor( [ & ] { return messages.size() >= 2; } ) );
    REQUIRE( messages.takeLast()[ 0 ].toJsonObject()[ "id" ] == "source-job" );
    process.stop();
    REQUIRE( waitFor( [ & ] { return process.state() == "stopped"; } ) );
}

TEST_CASE( "Packages reject Windows-normalized traversal and reserved names", "[plugins]" )
{
    for ( const auto& path : { "safe/.. /outside", "safe/../outside", "safe./file", "dir/file ",
                               "CON.txt", "dir/NUL", "COM1.log", "dir/a?b" } ) {
        INFO( path );
        REQUIRE_FALSE( safeRelativePath( path ) );
    }
    REQUIRE( safeRelativePath( "dir with spaces/file.log" ) );
    QTemporaryDir temp;
    const auto zipPath = QDir( temp.path() ).filePath( "unsafe.zip" );
    KZip zip( zipPath );
    REQUIRE( zip.open( QIODevice::WriteOnly ) );
    REQUIRE( zip.writeFile( "safe/.. /.. /outside", "payload" ) );
    REQUIRE( zip.close() );
    const auto installRoot = QDir( temp.path() ).filePath( "installed" );
    REQUIRE_FALSE( PluginRepository::stage( zipPath, installRoot ).error.isEmpty() );
    REQUIRE_FALSE( QFileInfo::exists( QDir( installRoot ).filePath( "outside" ) ) );
}

TEST_CASE( "Converter validates required input after capturing target context", "[plugins]" )
{
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    const auto folder = package( shipped );
    auto json = manifest();
    auto actions = json[ "actions" ].toArray();
    auto convert = actions[ 1 ].toObject();
    convert[ "fields" ] = QJsonArray{ QJsonObject{
        { "id", "input_path" }, { "type", "file" }, { "required", true } } };
    actions[ 1 ] = convert;
    json[ "actions" ] = actions;
    writeFile( QDir( folder ).filePath( "plugin.json" ), QJsonDocument( json ).toJson() );
    const auto input = QDir( temp.path() ).filePath( "original.log" );
    const QByteArray original( "unchanged input\n" );
    writeFile( input, original );
    PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
    QSignalSpy opened( &manager, &PluginManager::fileReady );
    REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
    const auto id = jobId( manager.invoke( "org.test.fixture", "convert", {},
                                           { { "filePath", input }, { "windowId", "target" } } ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, id ); } ) );
    REQUIRE( manager.job( id ).payload[ "state" ] == "completed" );
    REQUIRE( opened.size() == 1 );
    REQUIRE( opened[ 0 ][ 2 ].toMap()[ "windowId" ] == "target" );
    QFile file( input );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    REQUIRE( file.readAll() == original );
}

TEST_CASE( "Secret configuration is redacted and omitted secrets survive updates", "[plugins]" )
{
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    const auto folder = package( shipped );
    auto json = manifest();
    json[ "configuration" ] = QJsonArray{
        QJsonObject{ { "id", "token" }, { "type", "secret" }, { "default", "private-default" } },
        QJsonObject{ { "id", "count" }, { "type", "integer" }, { "default", 1 } }
    };
    writeFile( QDir( folder ).filePath( "plugin.json" ), QJsonDocument( json ).toJson() );
    const auto root = QDir( temp.path() ).filePath( "data" );
    {
        PluginManager manager( root, shipped );
        REQUIRE( manager
                     .manage( "configure", "org.test.fixture",
                              { { "configuration",
                                  QVariantMap{ { "token", "private-value" }, { "count", 2 } } } } )
                     .ok() );
        REQUIRE( manager
                     .manage( "configure", "org.test.fixture",
                              { { "configuration", QVariantMap{ { "count", 3 } } } } )
                     .ok() );
        const auto visible = QJsonDocument::fromVariant( manager.plugins() ).toJson();
        REQUIRE_FALSE( visible.contains( "private-value" ) );
        REQUIRE_FALSE( visible.contains( "private-default" ) );
        REQUIRE(
            manager.plugins()[ 0 ].toMap()[ "configuredSecrets" ].toMap()[ "token" ].toBool() );
    }
    auto downgradedFields = json[ "configuration" ].toArray();
    auto tokenField = downgradedFields[ 0 ].toObject();
    tokenField[ "type" ] = "text";
    downgradedFields[ 0 ] = tokenField;
    json[ "configuration" ] = downgradedFields;
    writeFile( QDir( folder ).filePath( "plugin.json" ), QJsonDocument( json ).toJson() );
    PluginManager restored( root, shipped );
    REQUIRE( restored.plugins()[ 0 ].toMap()[ "configuration" ].toList()[ 0 ].toMap()[ "type" ]
             == "secret" );
    REQUIRE( restored.plugins()[ 0 ].toMap()[ "configuredSecrets" ].toMap()[ "token" ].toBool() );
    REQUIRE( restored.plugins()[ 0 ].toMap()[ "configurationValues" ].toMap()[ "count" ] == 3 );
    REQUIRE( restored
                 .manage( "configure", "org.test.fixture",
                          { { "configuration", QVariantMap{ { "token", "" } } } } )
                 .ok() );
    REQUIRE_FALSE(
        restored.plugins()[ 0 ].toMap()[ "configuredSecrets" ].toMap()[ "token" ].toBool() );
}

TEST_CASE( "Concurrent local installations serialize unknown package identities", "[plugins]" )
{
    QTemporaryDir temp;
    const auto first = package( QDir( temp.path() ).filePath( "first" ) );
    const auto second = package( QDir( temp.path() ).filePath( "second" ) );
    auto json = manifest();
    json[ "version" ] = "2.0.0";
    writeFile( QDir( second ).filePath( "plugin.json" ), QJsonDocument( json ).toJson() );
    PluginManager manager( QDir( temp.path() ).filePath( "data" ),
                           QDir( temp.path() ).filePath( "empty" ) );
    const auto id = jobId( manager.manage( "install", {}, { { "path", first } } ) );
    REQUIRE_FALSE( manager.manage( "install", {}, { { "path", second } } ).ok() );
    REQUIRE_FALSE( manager.manage( "remove", "org.test.fixture", {} ).ok() );
    REQUIRE( waitFor( [ & ] { return finished( manager, id ); } ) );
    REQUIRE( manager.job( id ).payload[ "state" ] == "completed" );
    REQUIRE( manager.plugins()[ 0 ].toMap()[ "version" ] == "1.0.0" );
    const auto next = jobId( manager.manage( "install", {}, { { "path", second } } ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, next ); } ) );
    REQUIRE( manager.job( next ).payload[ "state" ] == "completed" );
    REQUIRE( manager.plugins()[ 0 ].toMap()[ "version" ] == "2.0.0" );
}

TEST_CASE( "Capture quotas persist and retain records when per-file or total limits stop streams",
           "[plugins]" )
{
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    package( shipped );
    const auto root = QDir( temp.path() ).filePath( "data" );
    PluginManager manager( root, shipped );
    REQUIRE( manager.storageStatus()[ "limits" ].toMap()[ "max_capture_bytes" ].toLongLong()
             == 1024LL * 1024 * 1024 );
    REQUIRE( manager.storageStatus()[ "limits" ].toMap()[ "max_total_bytes" ].toLongLong()
             == 10LL * 1024 * 1024 * 1024 );
    const QVariantMap limits{ { "max_capture_bytes", 8 },
                              { "max_total_bytes", 12 },
                              { "min_free_bytes", 0 } };
    REQUIRE( manager.manage( "configure", {}, { { "storage_limits", limits } } ).ok() );
    REQUIRE_FALSE(
        manager
            .manage( "configure", {},
                     { { "storage_limits", QVariantMap{ { "max_capture_bytes", -1 } } } } )
            .ok() );
    REQUIRE_FALSE(
        manager
            .manage( "configure", {},
                     { { "storage_limits", QVariantMap{ { "max_capture_bytes", 13 } } } } )
            .ok() );
    REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
    QSignalSpy opened( &manager, &PluginManager::fileReady );
    const auto first
        = jobId( manager.invoke( "org.test.fixture", "source",
                                 { { "records", QVariantList{ "one" } }, { "batches", 3 } }, {} ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, first ); } ) );
    REQUIRE( manager.job( first ).payload[ "state" ] == "failed" );
    REQUIRE( opened.size() == 1 );
    QFile retained( opened[ 0 ][ 0 ].toString() );
    REQUIRE( retained.open( QIODevice::ReadOnly ) );
    REQUIRE( retained.readAll() == "one\none\n" );
    retained.close();
    REQUIRE( CaptureStorage::directoryUsage( manager.storageStatus()[ "directory" ].toString() )
             == 8 );
    const auto second
        = jobId( manager.invoke( "org.test.fixture", "source",
                                 { { "records", QVariantList{ "two" } }, { "batches", 2 } }, {} ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, second ); } ) );
    INFO( QJsonDocument::fromVariant( manager.storageStatus() ).toJson().toStdString() );
    INFO( QJsonDocument::fromVariant( manager.job( second ).payload ).toJson().toStdString() );
    REQUIRE( manager.job( second ).payload[ "state" ] == "failed" );
    REQUIRE( opened.size() == 2 );
    QFile aggregate( opened[ 1 ][ 0 ].toString() );
    REQUIRE( aggregate.open( QIODevice::ReadOnly ) );
    REQUIRE( aggregate.readAll() == "two\n" );
    aggregate.close();
    REQUIRE( QFile::remove( retained.fileName() ) );
    const auto third
        = jobId( manager.invoke( "org.test.fixture", "source",
                                 { { "records", QVariantList{ "new" } }, { "batches", 2 } }, {} ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, third ); } ) );
    REQUIRE( manager.job( third ).payload[ "state" ] == "completed" );
    PluginManager restored( root, shipped );
    REQUIRE( restored.storageStatus()[ "limits" ].toMap() == limits );
}

TEST_CASE( "Converter limits reject completed output and stop active writers", "[plugins]" )
{
    for ( const bool hold : { false, true } ) {
        INFO( hold );
        QTemporaryDir temp;
        const auto shipped = QDir( temp.path() ).filePath( "shipped" );
        package( shipped );
        const auto input = QDir( temp.path() ).filePath( "input.log" );
        const QByteArray original( "original records exceed eight bytes\n" );
        writeFile( input, original );
        PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
        REQUIRE( manager
                     .manage( "configure", {},
                              { { "storage_limits", QVariantMap{ { "max_capture_bytes", 8 },
                                                                 { "max_total_bytes", 12 },
                                                                 { "min_free_bytes", 0 } } } } )
                     .ok() );
        REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
        QSignalSpy opened( &manager, &PluginManager::fileReady );
        const auto id = jobId( manager.invoke(
            "org.test.fixture", "convert", { { "input_path", input }, { "hold", hold } }, {} ) );
        REQUIRE( waitFor( [ & ] { return finished( manager, id ); } ) );
        REQUIRE( manager.job( id ).payload[ "state" ] == "failed" );
        REQUIRE( opened.isEmpty() );
        REQUIRE(
            waitFor( [ & ] { return manager.plugins()[ 0 ].toMap()[ "state" ] == "failed"; } ) );
        QFile file( input );
        REQUIRE( file.open( QIODevice::ReadOnly ) );
        REQUIRE( file.readAll() == original );
        REQUIRE_FALSE( QDir( QDir( manager.dataRoot() ).filePath( "captures" ) )
                           .entryList( QDir::Files )
                           .isEmpty() );
    }
}

TEST_CASE( "Catalog requests supersede older results and select exact package versions",
           "[plugins]" )
{
    QTemporaryDir temp;
    MemoryNetwork network;
    QJsonArray entries;
    for ( const auto& version : { "1.0.0", "2.0.0" } ) {
        const auto folder = package( QDir( temp.path() ).filePath( version ) );
        auto json = manifest();
        json[ "version" ] = version;
        writeFile( QDir( folder ).filePath( "plugin.json" ), QJsonDocument( json ).toJson() );
        const auto zipPath = QDir( temp.path() ).filePath( QString( version ) + ".zip" );
        KZip zip( zipPath );
        REQUIRE( zip.open( QIODevice::WriteOnly ) );
        REQUIRE( zip.addLocalDirectory( folder, "" ) );
        REQUIRE( zip.close() );
        QFile archive( zipPath );
        REQUIRE( archive.open( QIODevice::ReadOnly ) );
        const auto bytes = archive.readAll();
        const auto url = "https://test.invalid/" + QString( version ) + ".zip";
        network.responses[ url ] = bytes;
#ifdef Q_OS_WIN
        const auto os = "windows";
#elif defined( Q_OS_MAC )
        const auto os = "macos";
#else
        const auto os = "linux";
#endif
        entries.append( QJsonObject{
            { "id", "org.test.fixture" },
            { "version", version },
            { "protocol_version", 1 },
            { "os", os },
            { "arch", QSysInfo::buildCpuArchitecture() },
            { "url", url },
            { "sha256",
              QString::fromLatin1(
                  QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() ) } } );
    }
    const auto encode = []( const QJsonArray& plugins ) {
        return QJsonDocument( QJsonObject{ { "schema_version", 1 }, { "plugins", plugins } } )
            .toJson();
    };
    network.responses[ "https://test.invalid/old.json" ] = encode( { entries[ 0 ] } );
    network.delays[ "https://test.invalid/old.json" ] = 150;
    network.responses[ "https://test.invalid/new.json" ] = encode( entries );
    PluginManager manager( QDir( temp.path() ).filePath( "data" ),
                           QDir( temp.path() ).filePath( "empty" ), nullptr, &network );
    const auto old
        = jobId( manager.manage( "catalog", {}, { { "url", "https://test.invalid/old.json" } } ) );
    const auto current
        = jobId( manager.manage( "catalog", {}, { { "url", "https://test.invalid/new.json" } } ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, current ); } ) );
    REQUIRE( manager.job( old ).payload[ "state" ] == "cancelled" );
    REQUIRE( manager.job( current ).payload[ "state" ] == "completed" );
    QTest::qWait( 200 );
    REQUIRE_FALSE( manager.manage( "install", "org.test.fixture", {} ).ok() );
    const auto selected
        = manager.job( current ).payload[ "result" ].toMap()[ "plugins" ].toList()[ 1 ].toMap();
    const auto installed = jobId( manager.manage(
        "install", "org.test.fixture", { { "catalog_entry_id", selected[ "entry_id" ] } } ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, installed ); } ) );
    REQUIRE( manager.job( installed ).payload[ "state" ] == "completed" );
    REQUIRE( manager.plugins()[ 0 ].toMap()[ "version" ] == "2.0.0" );
    network.responses[ "https://test.invalid/new.json" ] = encode( { entries[ 0 ] } );
    const auto refreshed
        = jobId( manager.manage( "catalog", {}, { { "url", "https://test.invalid/new.json" } } ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, refreshed ); } ) );
    REQUIRE_FALSE( manager
                       .manage( "update", "org.test.fixture",
                                { { "catalog_entry_id", selected[ "entry_id" ] } } )
                       .ok() );
}

TEST_CASE( "Full converter history preserves supervision and leaves restart capacity", "[plugins]" )
{
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    package( shipped );
    const auto input = QDir( temp.path() ).filePath( "input.log" );
    writeFile( input, "record\n" );
    PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
    REQUIRE( manager.manage( "enable", "org.test.fixture", {} ).ok() );
    for ( int index = 0; index < 240; ++index ) {
        const auto id = jobId(
            manager.invoke( "org.test.fixture", "convert", { { "input_path", input } }, {} ) );
        REQUIRE( waitFor( [ & ] { return finished( manager, id ); } ) );
        REQUIRE( manager.job( id ).payload[ "state" ] == "completed" );
    }
    REQUIRE_FALSE(
        manager.invoke( "org.test.fixture", "convert", { { "input_path", input } }, {} ).ok() );
    const auto restart = jobId( manager.manage( "restart", "org.test.fixture", {} ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, restart ); } ) );
    REQUIRE( manager.job( restart ).payload[ "state" ] == "completed" );
    const auto next
        = jobId( manager.invoke( "org.test.fixture", "convert", { { "input_path", input } }, {} ) );
    REQUIRE( waitFor( [ & ] { return finished( manager, next ); } ) );
    REQUIRE( manager.job( next ).payload[ "state" ] == "completed" );
    REQUIRE(
        QDir( QDir( manager.dataRoot() ).filePath( "captures" ) ).entryList( QDir::Files ).size()
        == 241 );
}
