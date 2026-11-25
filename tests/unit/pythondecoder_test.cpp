// SPDX-License-Identifier: GPL-3.0-or-later
// MD03b: the Python preview backend for configuration pack decoders (docs/plugin-repositories.md
// section 4.4): the supervised worker, routing and fallback, the session snapshot, trust and the
// capability flag.
#include <catch2/catch.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <functional>
#include <memory>

#include "configpack.h"
#include "pluginfeatures.h"
#include "pluginmanager.h"
#include "pluginsandpacks.h"
#include "previewdecodersettings.h"
#include "previewdecodersettingsdialog.h"
#include "previewmanager.h"
#include "previewpackregistry.h"
#include "previewservice.h"
#include "pythondecoderclient.h"
#include "pythonruntime.h"

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <signal.h>
#endif

using namespace cilogg::plugins;

namespace {
constexpr auto PackA = "org.example.alpha";
constexpr auto PackB = "org.example.beta";

bool pythonAvailable()
{
    return !qEnvironmentVariable( "CILOGG_SCRIPT_PYTHON" ).isEmpty()
           || !cilogg::pythonruntime::pathInterpreterCandidates().isEmpty();
}

void writeFile( const QString& path, const QByteArray& bytes )
{
    REQUIRE( QDir().mkpath( QFileInfo( path ).absolutePath() ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
}

// A decoder module that echoes what it received and the module's own marker.
QByteArray echoModule( const QString& marker )
{
    return QStringLiteral( "MARKER = %1\n"
                           "def decode(payload, context):\n"
                           "    return {'fields': [\n"
                           "        {'name': 'marker', 'value': MARKER},\n"
                           "        {'name': 'length', 'value': len(payload), 'pos': 0,\n"
                           "         'size': len(payload), 'raw': payload},\n"
                           "        {'name': 'preview', 'value': context['preview'], 'depth': 1},\n"
                           "        {'name': 'protocol', 'value': context['protocol'], 'depth': 1},\n"
                           "        {'name': 'encoding', 'value': context['inputEncoding'],\n"
                           "         'depth': 1},\n"
                           "        {'name': 'decoder', 'value': context['decoder'], 'depth': 1},\n"
                           "    ], 'messageIntegrity': 'valid'}\n" )
        .arg( QStringLiteral( "'" ) + marker + QStringLiteral( "'" ) )
        .toUtf8();
}

// A pack content folder with `modules` (relative path -> source) under `base`, as the profile
// entry the worker receives: {id, root, decoders: [{id, module}]}, the id being the file name.
QVariantMap packProfile( const QString& base, const QString& id,
                         const QMap<QString, QByteArray>& modules )
{
    const auto root = QDir( base ).filePath( id );
    QVariantList decoders;
    for ( auto module = modules.cbegin(); module != modules.cend(); ++module ) {
        writeFile( QDir( root ).filePath( module.key() ), module.value() );
        decoders.append( QVariantMap{ { "id", QFileInfo( module.key() ).completeBaseName() },
                                      { "module", module.key() } } );
    }
    return { { "id", id }, { "root", root }, { "decoders", decoders } };
}

QVariantMap context( const QString& preview = QStringLiteral( "Demo" ) )
{
    return { { "preview", preview },
             { "protocol", "" },
             { "inputEncoding", "hex" },
             { "direction", "unknown" } };
}

// Runs one decode through the client and waits for its result.
QVariantMap decode( QObject* channel, const QVariantList& packs, const QString& decoder,
                    const QByteArray& payload, int timeoutMs = 5000,
                    const QVariantMap& decodeContext = context() )
{
    static int counter = 0;
    QEventLoop loop;
    auto reply = std::make_shared<QVariantMap>();
    const QPointer<QEventLoop> waiter( &loop );
    const auto id = QStringLiteral( "python-%1" ).arg( ++counter );
    PythonDecoderClient::instance().decode(
        channel, QVariantMap{ { "packs", packs } }, decoder, decodeContext, payload, timeoutMs, id,
        [ reply, waiter ]( QVariantMap value ) {
            *reply = std::move( value );
            if ( waiter ) {
                waiter->quit();
            }
        } );
    QTimer::singleShot( timeoutMs + 5000, &loop, &QEventLoop::quit );
    if ( reply->isEmpty() ) {
        loop.exec();
    }
    if ( reply->isEmpty() ) {
        PythonDecoderClient::instance().cancel( id );
        return { { "reason", "test_completion_guard_timeout" } };
    }
    return *reply;
}

QMap<QString, QString> columnsByName( const QVariantMap& result )
{
    QMap<QString, QString> values;
    for ( const auto& field : result.value( "fields" ).toList() ) {
        const auto columns = field.toMap().value( "columns" ).toList();
        values.insert( columns.value( 0 ).toString(), columns.value( 1 ).toString() );
    }
    return values;
}

bool processAlive( qint64 pid )
{
#ifdef Q_OS_WIN
    const auto handle = OpenProcess( SYNCHRONIZE, FALSE, static_cast<DWORD>( pid ) );
    if ( !handle ) {
        return false;
    }
    const bool alive = WaitForSingleObject( handle, 0 ) == WAIT_TIMEOUT;
    CloseHandle( handle );
    return alive;
#else
    // A killed process whose parent is gone may stay a zombie until PID 1 reaps it (containers):
    // it has ended all the same.
    QFile stat( QStringLiteral( "/proc/%1/stat" ).arg( pid ) );
    if ( stat.open( QIODevice::ReadOnly ) ) {
        const auto line = stat.readAll();
        const auto state = line.mid( line.lastIndexOf( ')' ) + 2, 1 );
        return state != "Z" && state != "X";
    }
    return ::kill( static_cast<pid_t>( pid ), 0 ) == 0 || errno != ESRCH;
#endif
}

bool waitUntil( const std::function<bool()>& condition, int milliseconds )
{
    QElapsedTimer clock;
    clock.start();
    while ( !condition() ) {
        if ( clock.elapsed() > milliseconds ) {
            return false;
        }
        QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
        QThread::msleep( 20 );
    }
    return true;
}

// Restores the decoder settings and pack contributions a test changes.
struct RestoreDecoder {
    QVariantMap configuration = PreviewDecoderSettings::instance().configuration();
    PreviewDecoderPacks packs = PreviewDecoderSettings::instance().packs();
    ~RestoreDecoder()
    {
        PreviewDecoderSettings::instance().setConfiguration( configuration );
        PreviewDecoderSettings::instance().setPacks( packs );
        PythonDecoderClient::instance().reset();
    }
};

// Previews for routing: "Demo" carries hex after "DEMO ", "Plain" matches the same lines first
// and has no route.
constexpr auto RoutingPreviews = R"json({
  "version": 1,
  "previews": [
    { "name": "Plain", "regex": "^DEMO (?<payload>[0-9A-Fa-f]+)$", "type": "hexString",
      "format": "fields", "bufferCapture": "payload",
      "fields": [ { "name": "plain", "source": "capture", "capture": "payload",
                    "format": "string" } ] },
    { "name": "Demo", "regex": "^DEMO (?<payload>[0-9A-Fa-f]+)$", "type": "hexString",
      "format": "fields", "bufferCapture": "payload",
      "fields": [ { "name": "json", "source": "capture", "capture": "payload",
                    "format": "string" } ] }
  ]
})json";

PreviewEvaluationRequest routedRequest( const QString& text )
{
    PreviewEvaluationRequest request;
    request.text = text;
    request.definitions = RoutingPreviews;
    request.timeoutMs = 10000;
    request.executable = QString::fromUtf8( PREVIEW_APP_PATH );
    return request;
}

// Routes `routes` (preview name -> route) to the decoders of `packs`, with the TShark decoder off.
void usePacks( const QVariantList& packs, const QVariantMap& previewRoutes,
               const QVariantMap& protocolRoutes = {} )
{
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "enabled", false }, { "inputEncoding", "hex" } } ) );
    PreviewDecoderPacks contributions;
    contributions.python = packs;
    contributions.previewRoutes = previewRoutes;
    contributions.protocolRoutes = protocolRoutes;
    contributions.key = QStringLiteral( "test-%1" ).arg( QDateTime::currentMSecsSinceEpoch() );
    PreviewDecoderSettings::instance().setPacks( contributions );
}

QVariantMap pythonRoute( const QString& target )
{
    return { { "python", target } };
}

// Runs `step` once a modal window matching `match` is open.
void whenModal( std::function<bool( QWidget* )> match, std::function<void( QWidget* )> step )
{
    auto* timer = new QTimer;
    auto* clock = new QElapsedTimer;
    clock->start();
    timer->setInterval( 20 );
    QObject::connect( timer, &QTimer::timeout, [ timer, clock, match, step ] {
        auto* widget = QApplication::activeModalWidget();
        const bool found = widget && match( widget );
        if ( found || clock->elapsed() > 10000 ) {
            timer->stop();
            timer->deleteLater();
            delete clock;
            if ( found ) {
                step( widget );
            }
        }
    } );
    timer->start();
}

QJsonObject pythonPack( const QString& id, const QJsonObject& previewRoutes,
                        const QJsonObject& protocolRoutes = {} )
{
    QJsonObject content{
        { "previews", "previews.json" },
        { "python_decoders",
          QJsonArray{ QJsonObject{ { "id", "summary" }, { "module", "python/summary.py" } },
                      QJsonObject{ { "id", "common" }, { "module", "python/common.py" } } } },
    };
    if ( !previewRoutes.isEmpty() || !protocolRoutes.isEmpty() ) {
        content[ "decoder_routes" ] = QJsonObject{ { "previewRoutes", previewRoutes },
                                                   { "protocolRoutes", protocolRoutes } };
    }
    return { { "id", id },
             { "name", QStringLiteral( "Pack %1" ).arg( id ) },
             { "version", "1.0.0" },
             { "protocol_version", 1 },
             { "kind", "config-pack" },
             { "pack_format", 1 },
             { "content", content } };
}

constexpr auto PackPreviews = R"json({
  "version": 1,
  "previews": [
    { "name": "%NAME%", "regex": "^%PREFIX% (?<payload>[0-9A-Fa-f]+)$", "type": "hexString",
      "format": "fields", "bufferCapture": "payload",
      "fields": [ { "name": "json", "source": "capture", "capture": "payload",
                    "format": "string" } ] }
  ]
})json";

// An installed pack at <root>/plugins/<id> whose "summary" decoder reports its pack's marker
// through a helper module, imported relatively.
QString writePythonPack( const QString& root, const QJsonObject& manifest, const QString& preview,
                         const QString& prefix )
{
    const auto id = manifest.value( "id" ).toString();
    const auto folder = QDir( root ).filePath( "plugins/" + id );
    writeFile( folder + "/plugin.json", QJsonDocument( manifest ).toJson() );
    writeFile( folder + "/previews.json", QString::fromUtf8( PackPreviews )
                                              .replace( "%NAME%", preview )
                                              .replace( "%PREFIX%", prefix )
                                              .toUtf8() );
    writeFile( folder + "/python/common.py", "MARKER = '" + id.toUtf8() + "'\n" );
    writeFile( folder + "/python/summary.py", "from . import common\n"
                                              "def decode(payload, context):\n"
                                              "    return [{'name': 'pack', 'value': common.MARKER},\n"
                                              "            {'name': 'bytes', 'value': payload.hex()}]\n" );
    return folder;
}
} // namespace

TEST_CASE( "Python decoders are a host feature reported with packs", "[pythondecoder]" )
{
    CHECK( hostFeatureVersions().value( "python_decoders" ).toInt() == 1 );
    // A pack that requires the feature is fine with this host.
    auto manifest = pythonPack( PackA, {} );
    manifest[ "requires" ] = QJsonObject{ { "host_features", QJsonArray{ "python_decoders" } } };
    const auto parsed = Manifest::parse( QJsonDocument( manifest ).toJson(), {} );
    CHECK( parsed.missingFeatures.isEmpty() );

    // The Installed tab's text follows the flag: an older host says "requires a newer CILogg".
    QVariantMap pack{ { "kind", "config-pack" }, { "pythonDecoders", 2 },
                      { "pythonDecodersActive", true } };
    CHECK( pluginsui::pythonDecodersState( pack, hostFeatureVersions().toVariantMap() )
           == "Python decoders: 2" );
    pack[ "pythonDecodersActive" ] = false;
    CHECK( pluginsui::pythonDecodersState( pack, hostFeatureVersions().toVariantMap() )
           == "Python decoders: 2 (inactive)" );
    CHECK( pluginsui::pythonDecodersState( pack, QVariantMap{ { "views", 2 } } )
           == "Python decoders: requires a newer CILogg" );
    pack[ "pythonDecoders" ] = 0;
    CHECK( pluginsui::pythonDecodersState( pack, hostFeatureVersions().toVariantMap() ).isEmpty() );

    // get_plugins: active while the pack is enabled.
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    writePythonPack( root.path(), pythonPack( PackA, {} ), "Alpha", "ALPHA" );
    PluginManager manager( root.path(), QDir( root.path() ).filePath( "none" ) );
    manager.discover();
    const auto entry = [ &manager ] { return manager.plugins().first().toMap(); };
    CHECK( entry().value( "pythonDecoders" ).toInt() == 2 );
    CHECK_FALSE( entry().value( "pythonDecodersActive" ).toBool() );
    REQUIRE( manager.manage( "enable", PackA, {} ).ok() );
    CHECK( entry().value( "pythonDecodersActive" ).toBool() );
}

TEST_CASE( "Python decoder routes follow the decoder settings' rules", "[pythondecoder]" )
{
    RestoreDecoder restore;
    auto& settings = PreviewDecoderSettings::instance();
    QString error;
    REQUIRE( settings.setConfiguration(
        { { "previewRoutes", QVariantMap{ { "Demo", pythonRoute( "org.example.alpha/summary" ) } } },
          { "protocolRoutes", QVariantMap{ { "DEMO", pythonRoute( "org.example.alpha/summary" ) } } } },
        &error ) );
    const auto saved = settings.configuration();
    CHECK( saved.value( "previewRoutes" ).toMap().value( "Demo" ).toMap()
           == pythonRoute( "org.example.alpha/summary" ) );
    // A protocol route gets the default input encoding, as a TShark one.
    CHECK( saved.value( "protocolRoutes" ).toMap().value( "DEMO" ).toMap().value( "inputEncoding" )
           == "utf8" );
    for ( const auto& route : QVector<QVariantMap>{
              pythonRoute( "summary" ),
              pythonRoute( "Org.Example/summary" ),
              pythonRoute( "org.example.alpha/" ),
              pythonRoute( "org.example.alpha/a/b" ),
              QVariantMap{ { "python", "org.example.alpha/summary" }, { "dissector", "data" } },
              QVariantMap{ { "python", "org.example.alpha/summary" }, { "protocol", "x" } },
              QVariantMap{ { "python", "org.example.alpha/summary" }, { "inputEncoding", "hex" } } } ) {
        INFO( QJsonDocument::fromVariant( route ).toJson( QJsonDocument::Compact ).toStdString() );
        CHECK_FALSE( settings.check( { { "previewRoutes", QVariantMap{ { "Demo", route } } } },
                                     &error ) );
        CHECK( error == "invalid_route" );
    }
}

TEST_CASE( "The decoder settings dialog keeps Python routes", "[pythondecoder]" )
{
    RestoreDecoder restore;
    auto& settings = PreviewDecoderSettings::instance();
    REQUIRE( settings.setConfiguration(
        { { "previewRoutes", QVariantMap{ { "Demo", pythonRoute( "org.example.alpha/summary" ) } } },
          { "protocolRoutes",
            QVariantMap{ { "DEMO", QVariantMap{ { "python", "org.example.alpha/summary" },
                                                { "inputEncoding", "hex" } } } } } } ) );
    const auto saved = settings.configuration();
    PreviewDecoderSettingsDialog dialog;
    auto* previewRoutes = dialog.findChild<QTableWidget*>( "decoderPreviewRoutes" );
    REQUIRE( previewRoutes );
    REQUIRE( previewRoutes->rowCount() == 1 );
    CHECK( previewRoutes->item( 0, 1 )->text() == "python:org.example.alpha/summary" );
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    REQUIRE( buttons );
    buttons->button( QDialogButtonBox::Apply )->click();
    auto* message = dialog.findChild<QLabel*>( "decoderValidationError" );
    REQUIRE( message );
    CHECK( message->text() == "Settings applied." );
    CHECK( settings.configuration() == saved );
}

TEST_CASE( "Pack Python routes name the pack's own decoders", "[pythondecoder]" )
{
    const auto routes = []( const QString& target ) {
        return QJsonObject{ { "Alpha", QJsonObject{ { "python", target } } } };
    };
    CHECK( validatePackManifest( pythonPack( PackA, routes( "org.example.alpha/summary" ) ) )
               .isEmpty() );
    CHECK( validatePackManifest( pythonPack( PackA, {},
                                             QJsonObject{ { "ALPHA",
                                                            QJsonObject{ { "python",
                                                                           "org.example.alpha/summary" },
                                                                         { "inputEncoding",
                                                                           "hex" } } } } ) )
               .isEmpty() );
    CHECK( validatePackManifest( pythonPack( PackA, routes( "org.example.beta/summary" ) ) )
               .contains( "Python decoder" ) );
    CHECK( validatePackManifest( pythonPack( PackA, routes( "org.example.alpha/missing" ) ) )
               .contains( "Python decoder" ) );
    CHECK( !validatePackManifest(
                pythonPack( PackA, QJsonObject{ { "Alpha",
                                                  QJsonObject{ { "python",
                                                                 "org.example.alpha/summary" },
                                                               { "dissector", "data" } } } } ) )
                .isEmpty() );
}

TEST_CASE( "The Python decoder worker is supervised and restarts", "[pythondecoder]" )
{
    if ( !pythonAvailable() ) {
        WARN( "No Python interpreter: the Python decoder worker tests are skipped" );
        return;
    }
    QTemporaryDir base;
    REQUIRE( base.isValid() );
    auto& client = PythonDecoderClient::instance();
    client.reset();
    const auto pack = packProfile(
        base.path(), PackA,
        { { "echo.py", echoModule( "alpha" ) },
          { "crash.py", "import os\ndef decode(payload, context):\n    os._exit(5)\n" },
          { "broken.py", "def decode(payload, context):\n    raise ValueError()\n" },
          { "loop.py", "def decode(payload, context):\n    while True:\n        pass\n" },
          { "rows.py", "def decode(payload, context):\n"
                       "    return [{'name': 'row', 'value': i} for i in range(600)]\n" },
          { "deep.py", "def decode(payload, context):\n"
                       "    return [{'name': 'level', 'depth': d} for d in range(40)]\n" } } );
    const QVariantList packs{ pack };
    QObject channel;

    const auto first = decode( &channel, packs, "org.example.alpha/echo", "\x01\x02\x03" );
    INFO( QJsonDocument::fromVariant( first ).toJson( QJsonDocument::Compact ).toStdString() );
    REQUIRE( first.value( "state" ) == "completed" );
    CHECK( first.value( "backend" ) == "python_decoder" );
    CHECK( first.value( "offsetSpace" ) == "decoder_payload" );
    CHECK( first.value( "messageIntegrity" ) == "valid" );
    CHECK( first.value( "messageCompleteness" ) == "not_reported" );
    CHECK( first.value( "moduleSha256" ).toString().size() == 64 );
    CHECK( columnsByName( first ).value( "marker" ) == "alpha" );
    CHECK( columnsByName( first ).value( "decoder" ) == "org.example.alpha/echo" );
    const auto firstPid = first.value( "enginePid" ).toLongLong();
    REQUIRE( firstPid > 0 );

    SECTION( "one worker per session" )
    {
        const auto again = decode( &channel, packs, "org.example.alpha/echo", "\x04" );
        CHECK( again.value( "enginePid" ).toLongLong() == firstPid );
    }
    SECTION( "a crash fails the request and the next one starts a new worker" )
    {
        CHECK( decode( &channel, packs, "org.example.alpha/crash", "\x01" ).value( "reason" )
               == "worker_failure" );
        const auto next = decode( &channel, packs, "org.example.alpha/echo", "\x01" );
        CHECK( next.value( "state" ) == "completed" );
        CHECK( next.value( "enginePid" ).toLongLong() != firstPid );
    }
    SECTION( "a failure that ends the worker's channel starts a new worker for the next request" )
    {
        // The worker answers a request it cannot take and exits: the next request, sent at once,
        // must not reach the exiting worker.
        auto bad = context();
        bad[ "direction" ] = "sideways";
        CHECK( decode( &channel, packs, "org.example.alpha/echo", "\x01", 5000, bad )
                   .value( "reason" )
               == "invalid_context" );
        const auto next = decode( &channel, packs, "org.example.alpha/echo", "\x01" );
        CHECK( next.value( "state" ) == "completed" );
        CHECK( next.value( "enginePid" ).toLongLong() != firstPid );
    }
    SECTION( "a decoder failure keeps the worker" )
    {
        CHECK( decode( &channel, packs, "org.example.alpha/broken", "\x01" ).value( "reason" )
               == "decoder_failed" );
        const auto again = decode( &channel, packs, "org.example.alpha/echo", "\x02" );
        CHECK( again.value( "enginePid" ).toLongLong() == firstPid );
    }
    SECTION( "the deadline ends a request that never returns" )
    {
        QElapsedTimer clock;
        clock.start();
        CHECK( decode( &channel, packs, "org.example.alpha/loop", "\x01", 500 ).value( "reason" )
               == "timeout" );
        CHECK( clock.elapsed() < 5000 );
        const auto next = decode( &channel, packs, "org.example.alpha/echo", "\x01" );
        CHECK( next.value( "state" ) == "completed" );
        CHECK( next.value( "enginePid" ).toLongLong() != firstPid );
    }
    SECTION( "requests and results stay within the limits" )
    {
        CHECK( decode( &channel, packs, "org.example.alpha/echo", QByteArray( 65537, 'x' ) )
                   .value( "reason" )
               == "input_limit" );
        CHECK( decode( &channel, packs, "org.example.alpha/echo", QByteArray( 65536, 'x' ) )
                   .value( "state" )
               == "completed" );
        const auto rows = decode( &channel, packs, "org.example.alpha/rows", "\x01" );
        CHECK( rows.value( "fields" ).toList().size() == 512 );
        CHECK( rows.value( "limited" ).toBool() );
        CHECK( rows.value( "reason" ) == "rendered_field_limit" );
        const auto deep = decode( &channel, packs, "org.example.alpha/deep", "\x01" );
        CHECK( deep.value( "fields" ).toList().size() == 33 );
        CHECK( deep.value( "reason" ) == "depth_limit" );
    }
    SECTION( "a changed pack contribution starts new sessions" )
    {
        RestoreDecoder restore;
        PreviewDecoderPacks changed;
        changed.key = QStringLiteral( "changed-%1" ).arg( QDateTime::currentMSecsSinceEpoch() );
        PreviewDecoderSettings::instance().setPacks( changed );
        const auto next = decode( &channel, packs, "org.example.alpha/echo", "\x01" );
        CHECK( next.value( "enginePid" ).toLongLong() != firstPid );
    }
    client.reset();
}

TEST_CASE( "A Python decoder's child processes end with its worker", "[pythondecoder]" )
{
    if ( !pythonAvailable() ) {
        WARN( "No Python interpreter: the descendant test is skipped" );
        return;
    }
    QTemporaryDir base;
    REQUIRE( base.isValid() );
    const auto pidFile = QDir( base.path() ).filePath( "child.pid" );
    const auto module = QStringLiteral( "import subprocess, sys, time\n"
                                        "def decode(payload, context):\n"
                                        "    child = subprocess.Popen([sys.executable, '-c',\n"
                                        "                              'import time; time.sleep(60)'])\n"
                                        "    with open(r'%1', 'w') as handle:\n"
                                        "        handle.write(str(child.pid))\n"
                                        "    while True:\n"
                                        "        time.sleep(0.05)\n" )
                            .arg( pidFile )
                            .toUtf8();
    const QVariantList packs{ packProfile( base.path(), PackA, { { "spawn.py", module } } ) };
    QObject channel;
    // A generous deadline: a cold runner starts two interpreters before the pid file exists.
    CHECK( decode( &channel, packs, "org.example.alpha/spawn", "\x01", 5000 ).value( "reason" )
           == "timeout" );
    QFile file( pidFile );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const auto pid = file.readAll().trimmed().toLongLong();
    REQUIRE( pid > 0 );
    CHECK( waitUntil( [ pid ] { return !processAlive( pid ); }, 5000 ) );
    PythonDecoderClient::instance().reset();
}

TEST_CASE( "Each pack's Python modules have their own namespace", "[pythondecoder]" )
{
    if ( !pythonAvailable() ) {
        WARN( "No Python interpreter: the isolation test is skipped" );
        return;
    }
    QTemporaryDir base;
    REQUIRE( base.isValid() );
    const auto helper = []( const QString& marker ) {
        return QMap<QString, QByteArray>{
            { "python/common.py", "MARKER = '" + marker.toUtf8() + "'\n" },
            { "python/summary.py", "from . import common\n"
                                   "def decode(payload, context):\n"
                                   "    return [{'name': 'marker', 'value': common.MARKER}]\n" } };
    };
    const QVariantList packs{ packProfile( base.path(), PackA, helper( "alpha" ) ),
                              packProfile( base.path(), PackB, helper( "beta" ) ) };
    QObject channel;
    const auto alpha = decode( &channel, packs, "org.example.alpha/summary", "\x01" );
    const auto beta = decode( &channel, packs, "org.example.beta/summary", "\x01" );
    INFO( QJsonDocument::fromVariant( beta ).toJson( QJsonDocument::Compact ).toStdString() );
    CHECK( columnsByName( alpha ).value( "marker" ) == "alpha" );
    CHECK( columnsByName( beta ).value( "marker" ) == "beta" );
    CHECK( alpha.value( "enginePid" ) == beta.value( "enginePid" ) );
    PythonDecoderClient::instance().reset();
}

TEST_CASE( "Automatic previews route to Python decoders and fall back only when not dispatched",
           "[pythondecoder]" )
{
    if ( !pythonAvailable() ) {
        WARN( "No Python interpreter: the routing tests are skipped" );
        return;
    }
    RestoreDecoder restore;
    QTemporaryDir base;
    REQUIRE( base.isValid() );
    const QVariantList packs{ packProfile(
        base.path(), PackA,
        { { "echo.py", echoModule( "alpha" ) },
          { "decline.py", "def decode(payload, context):\n    return None\n" },
          { "broken.py", "def decode(payload, context):\n    raise ValueError()\n" } } ) };

    SECTION( "a routed preview decodes with the pack's decoder, before an earlier JSON preview" )
    {
        usePacks( packs, { { "Demo", pythonRoute( "org.example.alpha/echo" ) } } );
        const auto result = PreviewService::evaluateAndWait( routedRequest( "DEMO 0a0b0c" ) );
        INFO( QJsonDocument::fromVariant( result ).toJson( QJsonDocument::Compact ).toStdString() );
        REQUIRE( result.value( "state" ) == "completed" );
        CHECK( result.value( "backend" ) == "python_decoder" );
        CHECK( result.value( "previewName" ) == "Demo" );
        CHECK( result.value( "decoder" ) == "org.example.alpha/echo" );
        const auto values = columnsByName( result );
        CHECK( values.value( "length" ) == "3" );
        CHECK( values.value( "preview" ) == "Demo" );
        CHECK( values.value( "encoding" ) == "hex" );
        CHECK( !result.contains( "fallbackReason" ) );
        CHECK( PreviewService::tooltipHtml( result ).contains( "Python decoder" ) );
    }
    SECTION( "a decoder that does not dispatch falls back to JSON" )
    {
        usePacks( packs, { { "Demo", pythonRoute( "org.example.alpha/decline" ) } } );
        const auto result = PreviewService::evaluateAndWait( routedRequest( "DEMO 0a0b0c" ) );
        INFO( QJsonDocument::fromVariant( result ).toJson( QJsonDocument::Compact ).toStdString() );
        CHECK( result.value( "backend" ) == "json_preview" );
        CHECK( result.value( "available" ).toBool() );
        CHECK( result.value( "preferredBackend" ) == "python_decoder" );
        CHECK( result.value( "fallbackReason" ) == "decoder_not_dispatched" );
        CHECK( result.value( "decoderAttempts" ).toStringList()
               == QStringList{ "python_decoder", "json_preview" } );
    }
    SECTION( "a failing decoder is a visible failure, never a fallback" )
    {
        usePacks( packs, { { "Demo", pythonRoute( "org.example.alpha/broken" ) } } );
        const auto result = PreviewService::evaluateAndWait( routedRequest( "DEMO 0a0b0c" ) );
        CHECK( result.value( "backend" ) == "python_decoder" );
        CHECK( result.value( "state" ) == "failed" );
        CHECK( result.value( "reason" ) == "decoder_failed" );
        CHECK( result.value( "detail" ).toString().startsWith( "ValueError" ) );
        CHECK( !result.contains( "fallbackReason" ) );
    }
    SECTION( "a route to a decoder that is not loaded does not apply" )
    {
        usePacks( {}, { { "Demo", pythonRoute( "org.example.alpha/echo" ) } } );
        const auto result = PreviewService::evaluateAndWait( routedRequest( "DEMO 0a0b0c" ) );
        CHECK( result.value( "backend" ) == "json_preview" );
        CHECK( result.value( "previewName" ) == "Plain" );
        CHECK( result.value( "available" ).toBool() );
    }
    SECTION( "explicit JSON ignores the routes" )
    {
        usePacks( packs, { { "Demo", pythonRoute( "org.example.alpha/echo" ) } } );
        auto request = routedRequest( "DEMO 0a0b0c" );
        request.decoder = "json";
        CHECK( PreviewService::evaluateAndWait( request ).value( "backend" ) == "json_preview" );
    }
    SECTION( "an assembled protocol routes direct input to a decoder" )
    {
        usePacks( packs, {},
                  { { "DEMO", QVariantMap{ { "python", "org.example.alpha/echo" },
                                           { "inputEncoding", "hex" } } } } );
        auto request = routedRequest( "01020304" );
        request.assemblyProtocol = "DEMO";
        const auto result = PreviewService::evaluateAndWait( request );
        INFO( QJsonDocument::fromVariant( result ).toJson( QJsonDocument::Compact ).toStdString() );
        REQUIRE( result.value( "state" ) == "completed" );
        CHECK( result.value( "backend" ) == "python_decoder" );
        CHECK( columnsByName( result ).value( "protocol" ) == "DEMO" );
        CHECK( columnsByName( result ).value( "length" ) == "4" );
    }
}

TEST_CASE( "Pack Python asks before it can run", "[pythondecoder]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    writePythonPack( root.path(), pythonPack( PackA, {} ), "Alpha", "ALPHA" );
    PluginManager manager( root.path(), QDir( root.path() ).filePath( "none" ) );
    manager.discover();
    const auto ask = [ &manager ]( const QStringList& contents, bool enabling, bool answer ) {
        QString text;
        whenModal( []( QWidget* widget ) { return qobject_cast<QMessageBox*>( widget ); },
                   [ &text, answer ]( QWidget* widget ) {
                       auto* box = qobject_cast<QMessageBox*>( widget );
                       text = box->text();
                       box->done( answer ? QMessageBox::Yes : QMessageBox::No );
                   } );
        const bool accepted
            = pluginsui::confirmPackLua( nullptr, &manager, PackA, "Alpha", contents, enabling );
        return std::make_pair( accepted, text );
    };
    // Enabling a pack with Python decoders asks, and says what runs.
    const auto python = ask( { "previews", "python_decoders" }, true, false );
    CHECK_FALSE( python.first );
    CHECK( python.second.contains( "Python" ) );
    CHECK( python.second.contains( "your permissions" ) );
    CHECK( ask( { "python_decoders" }, true, true ).first );
    // Lua and Python together are named together.
    const auto both = ask( { "lua", "python_decoders" }, true, false );
    CHECK( both.second.contains( "Lua" ) );
    CHECK( both.second.contains( "Python" ) );
    // A pack that is not enabled runs nothing: installing it asks when it is enabled.
    CHECK( pluginsui::confirmPackLua( nullptr, &manager, PackA, "Alpha", { "python_decoders" },
                                      false ) );
    // An update of an enabled pack with Python asks too.
    REQUIRE( manager.manage( "enable", PackA, {} ).ok() );
    const auto update = ask( { "python_decoders" }, false, false );
    CHECK_FALSE( update.first );
    CHECK( update.second.contains( "This version" ) );
}

TEST_CASE( "An enabled pack's Python decoder previews end to end", "[pythondecoder]" )
{
    if ( !pythonAvailable() ) {
        WARN( "No Python interpreter: the end-to-end pack decoder test is skipped" );
        return;
    }
    RestoreDecoder restore;
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "enabled", false }, { "inputEncoding", "hex" } } ) );
    REQUIRE( PreviewManager::instance().clearAll() );
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const QJsonObject routeA{ { "Alpha frame", QJsonObject{ { "python", "org.example.alpha/summary" } } } };
    const QJsonObject routeB{ { "Beta frame", QJsonObject{ { "python", "org.example.beta/summary" } } } };
    writePythonPack( root.path(), pythonPack( PackA, routeA ), "Alpha frame", "ALPHA" );
    writePythonPack( root.path(), pythonPack( PackB, routeB ), "Beta frame", "BETA" );
    PluginManager manager( root.path(), QDir( root.path() ).filePath( "none" ) );
    manager.discover();
    auto& registry = PreviewPackRegistry::instance();
    registry.attach( &manager );
    REQUIRE( manager.manage( "enable", PackA, {} ).ok() );

    const auto preview = []( const QString& text ) {
        PreviewEvaluationRequest request;
        request.text = text;
        request.timeoutMs = 10000;
        request.executable = QString::fromUtf8( PREVIEW_APP_PATH );
        return PreviewService::evaluateAndWait( request );
    };
    // The pack's modules are in the session snapshot with their pack.
    const auto python = PreviewDecoderSettings::instance().snapshot().configuration.value( "packPython" ).toList();
    REQUIRE( python.size() == 1 );
    CHECK( python.first().toMap().value( "id" ) == PackA );
    CHECK( python.first().toMap().value( "decoders" ).toList().size() == 2 );

    const auto first = preview( "ALPHA 0102" );
    INFO( QJsonDocument::fromVariant( first ).toJson( QJsonDocument::Compact ).toStdString() );
    REQUIRE( first.value( "backend" ) == "python_decoder" );
    CHECK( first.value( "previewName" ) == "Alpha frame" );
    CHECK( columnsByName( first ).value( "pack" ) == PackA );
    CHECK( columnsByName( first ).value( "bytes" ) == "0102" );

    // Enabling another pack starts new sessions; each pack keeps its own helper module.
    REQUIRE( manager.manage( "enable", PackB, {} ).ok() );
    const auto beta = preview( "BETA 03" );
    INFO( QJsonDocument::fromVariant( beta ).toJson( QJsonDocument::Compact ).toStdString() );
    CHECK( columnsByName( beta ).value( "pack" ) == PackB );
    CHECK( beta.value( "enginePid" ) != first.value( "enginePid" ) );
    CHECK( columnsByName( preview( "ALPHA 04" ) ).value( "pack" ) == PackA );

    // A disabled pack's previews, routes and decoders are gone.
    REQUIRE( manager.manage( "disable", PackA, {} ).ok() );
    CHECK( preview( "ALPHA 04" ).value( "available" ).toBool() == false );

    // The pack check keeps the session's modules within the profile limit.
    QJsonArray many;
    for ( int index = 0; index < 32; ++index ) {
        many.append( QJsonObject{ { "id", QStringLiteral( "d%1" ).arg( index ) },
                                  { "module", QStringLiteral( "python/d%1.py" ).arg( index ) } } );
    }
    for ( const auto* id : { "org.example.c", "org.example.d" } ) {
        auto manifest = pythonPack( id, {} );
        auto content = manifest.value( "content" ).toObject();
        content[ "python_decoders" ] = many;
        content.remove( "previews" );
        manifest[ "content" ] = content;
        const auto folder = QDir( root.path() ).filePath( QStringLiteral( "plugins/" ) + id );
        writeFile( folder + "/plugin.json", QJsonDocument( manifest ).toJson() );
        for ( int index = 0; index < 32; ++index ) {
            writeFile( folder + QStringLiteral( "/python/d%1.py" ).arg( index ), "VALUE = 1\n" );
        }
    }
    manager.discover();
    REQUIRE( manager.manage( "enable", "org.example.c", {} ).ok() );
    const auto refused = registry.check(
        "org.example.d", QDir( root.path() ).filePath( "plugins/org.example.d" ),
        manager.manifest( "org.example.d" ) );
    CHECK( refused.contains( "Python" ) );
    CHECK( refused.contains( "64" ) );

    registry.attach( nullptr );
    PythonDecoderClient::instance().reset();
}
