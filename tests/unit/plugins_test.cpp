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
    MemoryReply( const QNetworkRequest& request, QByteArray data, QObject* parent )
        : QNetworkReply( parent )
        , data_( std::move( data ) )
    {
        setRequest( request );
        setUrl( request.url() );
        open( QIODevice::ReadOnly );
        QTimer::singleShot( 0, this, [ this ] {
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

protected:
    QNetworkReply* createRequest( Operation, const QNetworkRequest& request, QIODevice* ) override
    {
        return new MemoryReply( request, responses.value( request.url().toString() ), this );
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
