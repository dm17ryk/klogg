// SPDX-License-Identifier: GPL-3.0-or-later
// E16b: Python decoders in configuration packs (docs/plugin-repositories.md 4.4 and 4.6). MD03b
// runs a pack's modules in the preview; these cases follow a Python decoder pack through the pack
// life cycle (install from a zip, edit in place, update with and without a merge, Revert, Reset to
// Pack Version, remove), keep its items out of the user's settings, and show each pack decoder
// with its pack's name where a user picks or sees one.
#include <catch2/catch.hpp>

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <kzip.h>

#include "configpack.h"
#include "configurationexport.h"
#include "packcanonical.h"
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

using namespace cilogg::plugins;

namespace {
constexpr auto PackId = "org.example.pypack";
constexpr auto PackName = "Python pack";

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

QByteArray readFile( const QString& path )
{
    QFile file( path );
    return file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray{};
}

// The helper module: two values far enough apart for a three-way text merge to take a change to
// each from a different side.
QByteArray commonModule( const QString& marker, const QString& note )
{
    return QStringLiteral( "MARKER = '%1'\n"
                           "\n"
                           "# Values the summary decoder reports.\n"
                           "# Changed apart, they merge.\n"
                           "\n"
                           "NOTE = '%2'\n" )
        .arg( marker, note )
        .toUtf8();
}

constexpr auto SummaryModule = "from . import common\n"
                               "def decode(payload, context):\n"
                               "    return [{'name': 'marker', 'value': common.MARKER},\n"
                               "            {'name': 'note', 'value': common.NOTE},\n"
                               "            {'name': 'bytes', 'value': payload.hex()}]\n";

constexpr auto PackPreviews = R"json({
  "version": 1,
  "previews": [
    { "name": "Py frame", "regex": "^PY (?<payload>[0-9A-Fa-f]+)$", "type": "hexString",
      "format": "fields", "bufferCapture": "payload",
      "fields": [ { "name": "json", "source": "capture", "capture": "payload",
                    "format": "string" } ] }
  ]
})json";

// A Python decoder pack package folder: a preview routed to the "summary" decoder, which reports
// the "common" module's values.
QString writePythonPackage( const QString& folder, const QString& version,
                            const QByteArray& common )
{
    const QJsonObject manifest{
        { "id", PackId },
        { "name", PackName },
        { "version", version },
        { "protocol_version", 1 },
        { "kind", "config-pack" },
        { "pack_format", 1 },
        { "requires", QJsonObject{ { "host_features", QJsonArray{ "python_decoders" } } } },
        { "content",
          QJsonObject{
              { "previews", "previews.json" },
              { "python_decoders",
                QJsonArray{ QJsonObject{ { "id", "summary" }, { "module", "python/summary.py" } },
                            QJsonObject{ { "id", "common" }, { "module", "python/common.py" } } } },
              { "decoder_routes",
                QJsonObject{
                    { "previewRoutes",
                      QJsonObject{ { "Py frame",
                                     QJsonObject{ { "python", QStringLiteral( "%1/summary" )
                                                                  .arg( PackId ) } } } } } } } } }
    };
    writeFile( folder + "/plugin.json", QJsonDocument( manifest ).toJson() );
    writeFile( folder + "/previews.json", PackPreviews );
    writeFile( folder + "/python/summary.py", SummaryModule );
    writeFile( folder + "/python/common.py", common );
    return folder;
}

QString zipFolder( const QString& folder )
{
    const auto zipPath = folder + ".zip";
    KZip zip( zipPath );
    REQUIRE( zip.open( QIODevice::WriteOnly ) );
    REQUIRE( zip.addLocalDirectory( folder, "" ) );
    REQUIRE( zip.close() );
    return zipPath;
}

QVariantMap finished( PluginManager& manager, const Result& started )
{
    REQUIRE( started.ok() );
    const auto jobId = started.payload.value( "jobId" ).toString();
    QElapsedTimer timer;
    timer.start();
    while ( timer.elapsed() < 20000 ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
        const auto job = manager.job( jobId ).payload;
        const auto state = job.value( "state" ).toString();
        if ( state == "completed" || state == "failed" || state == "cancelled" ) {
            return job;
        }
    }
    FAIL( "job did not finish" );
    return {};
}

QVariantMap install( PluginManager& manager, const QString& path, const QVariantMap& options = {} )
{
    auto all = options;
    all[ "path" ] = path;
    return finished( manager, manager.manage( "install", {}, all ) );
}

QVariantMap entry( const PluginManager& manager )
{
    for ( const auto& value : manager.plugins() ) {
        if ( value.toMap().value( "id" ) == PackId ) {
            return value.toMap();
        }
    }
    return {};
}

QVariantMap preview( const QString& text )
{
    PreviewEvaluationRequest request;
    request.text = text;
    request.timeoutMs = 10000;
    request.executable = QString::fromUtf8( PREVIEW_APP_PATH );
    return PreviewService::evaluateAndWait( request );
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

QString commonPath()
{
    return packcanonical::filePath( "python/common.py" );
}

// Restores the decoder settings, previews and pack contributions a case changes.
struct RestoreDecoder {
    QVariantMap configuration = PreviewDecoderSettings::instance().configuration();
    PreviewDecoderPacks packs = PreviewDecoderSettings::instance().packs();
    RestoreDecoder()
    {
        // No manager left attached by an earlier case may replace the packs a case sets.
        PreviewPackRegistry::instance().attach( nullptr );
        REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
            { { "enabled", false }, { "inputEncoding", "hex" } } ) );
        REQUIRE( PreviewManager::instance().clearAll() );
    }
    ~RestoreDecoder()
    {
        PreviewPackRegistry::instance().attach( nullptr );
        PreviewDecoderSettings::instance().setConfiguration( configuration );
        PreviewDecoderSettings::instance().setPacks( packs );
        PythonDecoderClient::instance().reset();
    }
};

// A pack installed from its folder through the local store, enabled, with the registry following
// the manager.
struct InstalledPack {
    QTemporaryDir root;
    PluginManager manager{ root.filePath( "data" ), root.filePath( "none" ) };
    explicit InstalledPack( bool zipped = false )
    {
        REQUIRE( root.isValid() );
        PreviewPackRegistry::instance().attach( &manager );
        const auto folder
            = writePythonPackage( root.filePath( "v1" ), "1.0.0", commonModule( "v1", "shipped" ) );
        REQUIRE( install( manager, zipped ? zipFolder( folder ) : folder )[ "state" ]
                 == "completed" );
    }
    QString content() const
    {
        return manager.packContentDirectory( PackId );
    }
};

QVariantMap pythonRoute( const QString& target )
{
    return { { "python", target } };
}

QTableWidget* routeTable( PreviewDecoderSettingsDialog& dialog, const char* name )
{
    auto* table = dialog.findChild<QTableWidget*>( name );
    REQUIRE( table );
    return table;
}

int rowNamed( QTableWidget* table, const QString& name )
{
    for ( int row = 0; row < table->rowCount(); ++row ) {
        if ( table->item( row, 0 ) && table->item( row, 0 )->text() == name ) {
            return row;
        }
    }
    return -1;
}
} // namespace

TEST_CASE( "A Python decoder pack installs from a local zip through its store and decodes once "
           "enabled",
           "[packpython][E16]" )
{
    RestoreDecoder restore;
    InstalledPack pack( true );
    const auto installed = entry( pack.manager );
    CHECK( installed.value( "kind" ) == "config-pack" );
    CHECK( installed.value( "contents" ).toStringList().contains( "python_decoders" ) );
    CHECK( installed.value( "pythonDecoders" ).toInt() == 2 );
    CHECK_FALSE( installed.value( "pythonDecodersActive" ).toBool() );
    // The content is the store's working folder, which holds the modules.
    CHECK( pack.content().endsWith( "/current" ) );
    CHECK( readFile( pack.content() + "/python/summary.py" ) == SummaryModule );
    // A pack that is not enabled runs nothing.
    CHECK( PreviewDecoderSettings::instance().packs().python.isEmpty() );

    REQUIRE( pack.manager.manage( "enable", PackId, {} ).ok() );
    CHECK( entry( pack.manager ).value( "pythonDecodersActive" ).toBool() );
    const auto python = PreviewDecoderSettings::instance().packs().python;
    REQUIRE( python.size() == 1 );
    CHECK( QDir::cleanPath( python.first().toMap().value( "root" ).toString() )
           == QDir::cleanPath( pack.content() ) );
    // The pack's name goes with its decoders, for the places that show them.
    CHECK( PreviewDecoderSettings::instance().packs().pythonNames.value( PackId ) == PackName );
    // Only "summary" defines decode(); "common" is a helper module the pack lists.
    CHECK( PreviewDecoderSettings::instance().packs().pythonEntries.value( PackId ).toStringList()
           == QStringList{ "summary" } );
    CHECK( PreviewDecoderSettings::instance().pythonDecoderPack(
               QStringLiteral( "%1/summary" ).arg( PackId ) )
           == PackName );
    CHECK( PreviewDecoderSettings::instance()
               .pythonDecoderPack( QStringLiteral( "%1/missing" ).arg( PackId ) )
               .isEmpty() );
    CHECK( PreviewDecoderSettings::instance()
               .pythonDecoderPack( "org.example.other/summary" )
               .isEmpty() );
    // The pack's route is in the session snapshot, below the user's.
    CHECK( PreviewDecoderSettings::instance()
               .snapshot()
               .configuration.value( "previewRoutes" )
               .toMap()
               .contains( "Py frame" ) );

    if ( !pythonAvailable() ) {
        WARN( "No Python interpreter: the pack decoder is not run" );
        return;
    }
    const auto result = preview( "PY 0102" );
    INFO( QJsonDocument::fromVariant( result ).toJson( QJsonDocument::Compact ).toStdString() );
    REQUIRE( result.value( "backend" ) == "python_decoder" );
    CHECK( result.value( "previewName" ) == "Py frame" );
    CHECK( columnsByName( result ).value( "marker" ) == "v1" );
    CHECK( columnsByName( result ).value( "bytes" ) == "0102" );
    // The preview names the decoder with its pack.
    CHECK( PreviewService::pythonDecoderLabel( result )
           == QStringLiteral( "%1/summary (%2)" ).arg( PackId, PackName ) );
}

TEST_CASE( "An edited pack Python module is what decodes, until it is reverted or reset",
           "[packpython][E16]" )
{
    RestoreDecoder restore;
    InstalledPack pack;
    REQUIRE( pack.manager.manage( "enable", PackId, {} ).ok() );
    const auto readCommon = [ &pack ] {
        return pack.manager.manage( "pack_read", PackId, { { "path", commonPath() } } )
            .payload.value( "content" )
            .toString();
    };
    REQUIRE( readCommon() == QString::fromUtf8( commonModule( "v1", "shipped" ) ) );
    qint64 before = 0;
    if ( pythonAvailable() ) {
        const auto first = preview( "PY 01" );
        REQUIRE( first.value( "backend" ) == "python_decoder" );
        CHECK( columnsByName( first ).value( "marker" ) == "v1" );
        before = first.value( "enginePid" ).toLongLong();
    }

    // Edit in place, as from Pack Files..., the CLI, MCP or Python.
    REQUIRE(
        pack.manager
            .manage( "pack_edit", PackId,
                     { { "path", commonPath() },
                       { "content", QString::fromUtf8( commonModule( "mine", "shipped" ) ) } } )
            .ok() );
    const auto edits = pack.manager.manage( "pack_edits", PackId, {} ).payload[ "files" ].toList();
    REQUIRE( edits.size() == 1 );
    CHECK( edits.first().toMap()[ "path" ] == commonPath() );
    CHECK( edits.first().toMap()[ "kind" ] == "file" );
    CHECK( edits.first().toMap()[ "state" ] == "edited" );
    CHECK( entry( pack.manager ).value( "localEdits" ).toInt() == 1 );
    CHECK( readFile( pack.content() + "/python/common.py" ) == commonModule( "mine", "shipped" ) );
    if ( pythonAvailable() ) {
        // The same folder, a new session: the edited module runs.
        const auto edited = preview( "PY 01" );
        INFO( QJsonDocument::fromVariant( edited ).toJson( QJsonDocument::Compact ).toStdString() );
        CHECK( columnsByName( edited ).value( "marker" ) == "mine" );
        CHECK( edited.value( "enginePid" ).toLongLong() != before );
    }

    // Revert to the installed version of the file.
    const auto history = pack.manager.manage( "pack_history", PackId, { { "path", commonPath() } } )
                             .payload[ "commits" ]
                             .toList();
    REQUIRE( history.size() == 2 );
    CHECK( history.first().toMap()[ "message" ].toString().startsWith( "Edit" ) );
    CHECK( history.last().toMap()[ "packVersion" ].toBool() );
    REQUIRE( pack.manager
                 .manage( "pack_reset", PackId,
                          { { "path", commonPath() },
                            { "commit", history.last().toMap()[ "commit" ] } } )
                 .ok() );
    CHECK( readCommon() == QString::fromUtf8( commonModule( "v1", "shipped" ) ) );
    if ( pythonAvailable() ) {
        CHECK( columnsByName( preview( "PY 01" ) ).value( "marker" ) == "v1" );
    }

    // Reset to Pack Version for the whole pack.
    REQUIRE(
        pack.manager
            .manage( "pack_edit", PackId,
                     { { "path", commonPath() },
                       { "content", QString::fromUtf8( commonModule( "again", "shipped" ) ) } } )
            .ok() );
    REQUIRE( pack.manager.manage( "pack_reset", PackId, {} ).ok() );
    CHECK( pack.manager.manage( "pack_edits", PackId, {} ).payload[ "files" ].toList().isEmpty() );
    CHECK( readCommon() == QString::fromUtf8( commonModule( "v1", "shipped" ) ) );
    if ( pythonAvailable() ) {
        CHECK( columnsByName( preview( "PY 01" ) ).value( "marker" ) == "v1" );
    }
}

TEST_CASE( "Pack Python modules merge on update, and their conflicts are settled like any file",
           "[packpython][E16]" )
{
    RestoreDecoder restore;
    InstalledPack pack;
    REQUIRE( pack.manager.manage( "enable", PackId, {} ).ok() );
    REQUIRE( pack.manager
                 .manage( "pack_edit", PackId,
                          { { "path", commonPath() },
                            { "content", QString::fromUtf8( commonModule( "v1", "mine" ) ) } } )
                 .ok() );
    qint64 before = 0;
    if ( pythonAvailable() ) {
        before = preview( "PY 01" ).value( "enginePid" ).toLongLong();
    }

    // A version that changes another line of the edited module merges with the edit.
    const auto v2 = writePythonPackage( pack.root.filePath( "v2" ), "1.1.0",
                                        commonModule( "v2", "shipped" ) );
    REQUIRE( install( pack.manager, v2 )[ "state" ] == "completed" );
    CHECK( entry( pack.manager ).value( "version" ) == "1.1.0" );
    CHECK( readFile( pack.content() + "/python/common.py" ) == commonModule( "v2", "mine" ) );
    CHECK( entry( pack.manager ).value( "localEdits" ).toInt() == 1 );
    if ( pythonAvailable() ) {
        // The update starts new sessions, which run the merged module.
        const auto merged = preview( "PY 01" );
        INFO( QJsonDocument::fromVariant( merged ).toJson( QJsonDocument::Compact ).toStdString() );
        CHECK( columnsByName( merged ).value( "marker" ) == "v2" );
        CHECK( columnsByName( merged ).value( "note" ) == "mine" );
        CHECK( merged.value( "enginePid" ).toLongLong() != before );
    }

    // A version that changes the edited line: refused from automation, with the conflict.
    const auto v3
        = writePythonPackage( pack.root.filePath( "v3" ), "1.2.0", commonModule( "v2", "theirs" ) );
    const auto checkRefused = [ &pack ]( const QVariantMap& refused ) {
        CHECK( refused[ "state" ] == "failed" );
        const auto conflicts = refused[ "result" ].toMap()[ "conflicts" ].toList();
        REQUIRE( conflicts.size() == 1 );
        CHECK( conflicts.first().toMap()[ "path" ] == commonPath() );
        CHECK( conflicts.first().toMap()[ "kind" ] == "file" );
        CHECK( entry( pack.manager ).value( "version" ) == "1.1.0" );
        CHECK( entry( pack.manager ).value( "needsMerge" ) == "1.2.0" );
        CHECK( readFile( pack.content() + "/python/common.py" ) == commonModule( "v2", "mine" ) );
    };
    const auto refused = install( pack.manager, v3 );
    checkRefused( refused );

    SECTION( "fail" )
    {
        checkRefused( install( pack.manager, v3, { { "strategy", "fail" } } ) );
    }
    SECTION( "keep_mine" )
    {
        REQUIRE( install( pack.manager, v3, { { "strategy", "keep_mine" } } )[ "state" ]
                 == "completed" );
        CHECK( entry( pack.manager ).value( "version" ) == "1.2.0" );
        CHECK( readFile( pack.content() + "/python/common.py" ) == commonModule( "v2", "mine" ) );
    }
    SECTION( "take_theirs" )
    {
        REQUIRE( install( pack.manager, v3, { { "strategy", "take_theirs" } } )[ "state" ]
                 == "completed" );
        CHECK( readFile( pack.content() + "/python/common.py" ) == commonModule( "v2", "theirs" ) );
        if ( pythonAvailable() ) {
            CHECK( columnsByName( preview( "PY 01" ) ).value( "note" ) == "theirs" );
        }
    }
    SECTION( "a text from the three-pane merge" )
    {
        const auto text = QString::fromUtf8( commonModule( "v2", "merged" ) );
        REQUIRE( install( pack.manager, v3,
                          { { "resolutions", QVariantList{ QVariantMap{ { "path", commonPath() },
                                                                        { "text", text } } } },
                            { "revision", refused[ "result" ].toMap()[ "revision" ] } } )[ "state" ]
                 == "completed" );
        CHECK( readFile( pack.content() + "/python/common.py" ) == text.toUtf8() );
        if ( pythonAvailable() ) {
            CHECK( columnsByName( preview( "PY 01" ) ).value( "note" ) == "merged" );
        }
    }
}

TEST_CASE( "Removing a Python decoder pack takes its decoders, routes and store",
           "[packpython][E16]" )
{
    RestoreDecoder restore;
    InstalledPack pack;
    REQUIRE( pack.manager.manage( "enable", PackId, {} ).ok() );
    REQUIRE( PreviewDecoderSettings::instance().packs().python.size() == 1 );
    if ( pythonAvailable() ) {
        REQUIRE( preview( "PY 01" ).value( "backend" ) == "python_decoder" );
    }
    REQUIRE( pack.manager.manage( "remove", PackId, {} ).ok() );
    CHECK( entry( pack.manager ).isEmpty() );
    CHECK( PreviewDecoderSettings::instance().packs().python.isEmpty() );
    CHECK( PreviewDecoderSettings::instance().packs().pythonNames.isEmpty() );
    CHECK( !PreviewDecoderSettings::instance()
                .snapshot()
                .configuration.value( "previewRoutes" )
                .toMap()
                .contains( "Py frame" ) );
    CHECK( !QFileInfo::exists( pack.root.filePath( "data/pack-store/org.example.pypack.git" ) ) );
    CHECK_FALSE( preview( "PY 01" ).value( "available" ).toBool() );
}

TEST_CASE( "Only the modules that define decode() are offered as decoders", "[packpython][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const QMap<QString, QByteArray> modules{
        { "plain", "def decode(payload, context):\n    return None\n" },
        { "crlf", "import struct\r\n\r\ndef decode(payload, context):\r\n    return None\r\n" },
        // The worker calls decode() and never awaits it: an async one is not offered.
        { "asynchronous", "async def decode(payload, context):\n    return None\n" },
        { "assigned", "def _impl(payload, context):\n    return None\ndecode = _impl\n" },
        { "annotated", "from typing import Callable\ndecode: Callable = lambda p, c: None\n" },
        { "imported", "from .plain import decode\n" },
        { "parenthesized", "from .plain import (\n    decode,\n)\n" },
        { "rebound", "from .plain import decode as _plain, _impl as decode\n" },
        // Not decoders: a helper, a name bound otherwise, a method, a nested function and
        // look-alike names.
        { "helper", "MARKER = 'x'\n" },
        { "aliased", "from .plain import decode as other\n" },
        { "method",
          "class Codec:\n    def decode(self, payload, context):\n        return None\n" },
        { "nested", "def outer():\n    def decode(payload, context):\n        return None\n" },
        { "lookalike", "def decode_all(payload):\n    return None\ndecoded = 1\n# def decode(\n" },
        // Text in strings and comments binds nothing.
        { "docstring", "\"\"\"Example:\n\ndef decode(payload, context):\n    return None\n\"\"\"\n"
                       "MARKER = 1\n" },
        { "quoted", "EXAMPLE = '''\ndecode = None\nfrom .plain import decode\n'''\n" },
        { "commented", "from .plain import other  # not decode\n" },
        // A keyword argument at the start of a line inside a call binds nothing either.
        { "keyword", "def make(**kw):\n    return kw\nCODEC = make(\ndecode = make,\n)\n" },
        // Real ones after a bracket and a backslash that continue a statement over lines.
        { "afterbrackets", "ITEMS = (\n    1,\n)\ndecode = None\n" },
        { "continued", "VALUE = 1 + \\\n    2\ndecode = None\n" },
        // An annotation alone binds nothing; with a value after its brackets it does.
        { "annotationonly", "def marker(**kw):\n    return int\ndecode: marker(enabled=True)\n" },
        { "annotatedbrackets", "from typing import Callable\ndecode: Callable[[bytes, dict], list] "
                               "= lambda p, c: []\n" },
        // A generic function (Python 3.12).
        { "generic", "def decode[T](payload: T, context):\n    return None\n" },
        // Generic bounds may hold brackets of their own.
        { "nestedgeneric", "def decode[T: tuple[int, str]](payload: T, context):\n"
                           "    return None\n" },
        // A lambda in an annotation: its default is no assignment.
        { "lambdaannotation", "decode: lambda x=1: x\n" },
        // The last binding decides: an async one afterwards takes it away, a later def gives it.
        { "asyncredefined", "def decode(payload, context):\n    return None\n"
                            "async def decode(payload, context):\n    return None\n" },
        { "syncredefined", "async def decode(payload, context):\n    return None\n"
                           "def decode(payload, context):\n    return None\n" },
        { "classdecode", "class decode:\n    pass\n" },
        // A list or tuple target deletes the name too; a subscript inside one does not.
        { "dellist", "def decode(payload, context):\n    return None\ndel [decode]\n" },
        { "delnested",
          "def decode(payload, context):\n    return None\nitems = {}\ndel (items, [decode])\n" },
        { "delsubscript", "def decode(payload, context):\n    return None\nitems = {decode: 1}\n"
                          "del (items[decode],)\n" },
        // A Python 3.12 f-string whose replacement field spans lines stays one logical line.
        { "fstring",
          "def func(**kw):\n    return 1\nTEXT = f\"{func(\ndecode=lambda: None\n)}\"\n" },
        // Deleting something else that holds the name keeps the decoder.
        { "delattribute", "def decode(payload, context):\n    return None\nclass Holder:\n"
                          "    decode = 1\ndel Holder.decode\n" },
        // Form feeds are whitespace; one at the start of a line does not indent it.
        { "formfeed", "\x0c"
                      "def decode\x0c"
                      "(payload, context):\n    return None\n" },
        // A string over lines stays in its logical line: the decode is the one-line def's own.
        { "multilinesuite", "def helper(): text = \"\"\"a\nb\"\"\"; decode = helper\n" },
        // A name deleted again, and a plain import, which binds a module, are not decoders.
        { "deleted", "def decode(payload, context):\n    return None\ndel decode\n" },
        { "moduleimport", "import decode\n" },
        { "aliasedmodule", "import json as decode\n" },
        // Statements after a semicolon at the top level, but not the body of a one-line def.
        { "semicolon", "VERSION = 1; from .plain import decode\n" },
        { "semicolonassign", "VERSION = 1; decode = None\n" },
        { "onelinebody", "def helper(): value = 1; decode = value\n" },
        // `match` and `case` are soft keywords: here a name being assigned.
        { "softkeyword", "match = 1; decode = None\n" },
        // A semicolon in an indented block separates statements of that block.
        { "indented", "def helper():\n    value = 1; decode = value\n    return decode\n" },
        // Lines that end with a lone carriage return, which Python takes as a newline too.
        { "cr", "import struct\rdef decode(payload, context):\r    return None\r" },
        // A UTF-8 byte order mark before the first statement, as Python reads it.
        { "bom", "\xEF\xBB\xBF"
                 "def decode(payload, context):\n    return None\n" },
        // A real one after a string with an escaped quote and a quote inside a comment.
        { "escaped",
          "TEXT = \"a \\\" b\" # it's\ndef decode(payload, context):\n    return None\n" },
    };
    // Each module in a pack of its own: together they are more than a pack may list.
    QStringList entries;
    for ( auto module = modules.cbegin(); module != modules.cend(); ++module ) {
        INFO( module.key().toStdString() );
        writeFile( root.filePath( "python/" + module.key() + ".py" ), module.value() );
        const QJsonArray decoders{ QJsonObject{ { "id", module.key() },
                                                { "module", "python/" + module.key() + ".py" } } };
        const QJsonObject manifest{ { "id", PackId },
                                    { "name", PackName },
                                    { "version", "1.0.0" },
                                    { "protocol_version", 1 },
                                    { "kind", "config-pack" },
                                    { "pack_format", 1 },
                                    { "content", QJsonObject{ { "python_decoders", decoders } } } };
        const auto pack = PreviewPackRegistry::read( PackId, root.path(), manifest );
        REQUIRE( pack.error.isEmpty() );
        REQUIRE( pack.pythonModules.size() == 1 );
        entries.append( pack.pythonEntries );
    }
    entries.sort();
    CHECK( entries
           == QStringList{ "afterbrackets",
                           "annotated",
                           "annotatedbrackets",
                           "assigned",
                           "bom",
                           "continued",
                           "cr",
                           "crlf",
                           "delattribute",
                           "delsubscript",
                           "escaped",
                           "formfeed",
                           "generic",
                           "imported",
                           "nestedgeneric",
                           "parenthesized",
                           "plain",
                           "rebound",
                           "semicolon",
                           "semicolonassign",
                           "softkeyword",
                           "syncredefined" } );
}

TEST_CASE( "The decode() check reads a module near the size limit in linear time",
           "[packpython][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    // One line of 200,000 statements after a compound header (about 1 MB), then a real decode().
    QByteArray module = "def helper(): ";
    for ( int index = 0; index < 200000; ++index ) {
        module += "x=1; ";
    }
    module += "\ndef decode(payload, context):\n    return None\n";
    REQUIRE( module.size() < MaxPackFileBytes );
    writeFile( root.filePath( "python/long.py" ), module );
    const QJsonObject manifest{
        { "id", PackId },
        { "name", PackName },
        { "version", "1.0.0" },
        { "protocol_version", 1 },
        { "kind", "config-pack" },
        { "pack_format", 1 },
        { "content", QJsonObject{ { "python_decoders",
                                    QJsonArray{ QJsonObject{
                                        { "id", "long" }, { "module", "python/long.py" } } } } } }
    };
    QElapsedTimer clock;
    clock.start();
    const auto pack = PreviewPackRegistry::read( PackId, root.path(), manifest );
    INFO( "elapsed ms " << clock.elapsed() );
    REQUIRE( pack.error.isEmpty() );
    CHECK( pack.pythonEntries == QStringList{ "long" } );
    // Linear work takes milliseconds; copying the line at each semicolon took minutes.
    CHECK( clock.elapsed() < 5000 );
}

TEST_CASE( "A pack lists at most 32 Python decoders", "[packpython][E16]" )
{
    const auto manifest = []( int count ) {
        QJsonArray decoders;
        for ( int index = 0; index < count; ++index ) {
            decoders.append(
                QJsonObject{ { "id", QStringLiteral( "d%1" ).arg( index ) },
                             { "module", QStringLiteral( "python/d%1.py" ).arg( index ) } } );
        }
        return QJsonObject{ { "id", PackId },
                            { "name", PackName },
                            { "version", "1.0.0" },
                            { "protocol_version", 1 },
                            { "kind", "config-pack" },
                            { "pack_format", 1 },
                            { "content", QJsonObject{ { "python_decoders", decoders } } } };
    };
    CHECK( validatePackManifest( manifest( MaxPackPythonDecoders ) ).isEmpty() );
    CHECK( validatePackManifest( manifest( MaxPackPythonDecoders + 1 ) )
               .contains( QString::number( MaxPackPythonDecoders ) ) );
    CHECK( MaxPackPythonDecoders == 32 );
}

TEST_CASE( "Pack Python never reaches the user's decoder settings, exports or projects",
           "[packpython][E16]" )
{
    RestoreDecoder restore;
    InstalledPack pack;
    REQUIRE( pack.manager.manage( "enable", PackId, {} ).ok() );
    REQUIRE( PreviewDecoderSettings::instance().packs().python.size() == 1 );
    // A setting of the user's is saved while the pack is enabled.
    auto user = PreviewDecoderSettings::instance().configuration();
    user[ "previewRoutes" ]
        = QVariantMap{ { "Mine", pythonRoute( QStringLiteral( "%1/summary" ).arg( PackId ) ) } };
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration( user ) );

    const auto saved = PreviewDecoderSettings::instance().configuration();
    CHECK( !saved.contains( "packPython" ) );
    CHECK( !saved.contains( "packLua" ) );
    CHECK( saved.value( "previewRoutes" ).toMap().keys() == QStringList{ "Mine" } );

    // What an export or a project takes from the preferences is the user's configuration only.
    ConfigurationProject project;
    QString error;
    REQUIRE( ConfigurationExport::captureConfiguration( &project, &error ) );
    const auto stored = project.preferences.value( "previewDecoder/configuration" ).toByteArray();
    REQUIRE( !stored.isEmpty() );
    const auto document = QJsonDocument::fromJson( stored ).object();
    CHECK( !document.contains( "packPython" ) );
    CHECK( !document.contains( "packLua" ) );
    CHECK( document.value( "previewRoutes" ).toObject().keys() == QStringList{ "Mine" } );
    CHECK( !stored.contains( pack.content().toUtf8() ) );
}

TEST_CASE( "The decoder settings dialog offers the enabled packs' Python decoders by pack name",
           "[packpython][E16]" )
{
    RestoreDecoder restore;
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "inputEncoding", "hex" },
          { "previewRoutes",
            QVariantMap{ { "Bold", pythonRoute( "org.example.beta/summary" ) },
                         { "Demo", pythonRoute( "org.example.alpha/summary" ) },
                         { "Gone", pythonRoute( "org.example.gone/summary" ) },
                         { "Helper", pythonRoute( "org.example.alpha/common" ) },
                         { "Shark", QVariantMap{ { "dissector", "data" } } } } } } ) );
    const auto pack = []( const QString& id, const QVariantList& decoders ) {
        return QVariantMap{ { "id", id }, { "root", QDir::tempPath() }, { "decoders", decoders } };
    };
    const auto module = []( const QString& id, const QString& path ) {
        return QVariantMap{ { "id", id }, { "module", path } };
    };
    PreviewDecoderPacks packs;
    packs.python = {
        // "common" is a helper module the pack lists so the worker copies it: it has no decode().
        pack( "org.example.alpha", { module( "summary", "python/summary.py" ),
                                     module( "common", "python/common.py" ) } ),
        // A second pack with a decoder of the same ID, and a name with markup and an ampersand.
        pack( "org.example.beta", { module( "summary", "python/<b>.py" ) } ),
        // A third pack with the first one's name.
        pack( "org.example.gamma", { module( "summary", "python/summary.py" ) } ),
    };
    packs.pythonNames = { { "org.example.alpha", "Alpha pack" },
                          { "org.example.beta", "Beta & <b>bold</b>" },
                          { "org.example.gamma", "Alpha pack" } };
    packs.pythonEntries = { { "org.example.alpha", QStringList{ "summary" } },
                            { "org.example.beta", QStringList{ "summary" } },
                            { "org.example.gamma", QStringList{ "summary" } } };
    packs.key = QStringLiteral( "dialog-%1" ).arg( QDateTime::currentMSecsSinceEpoch() );
    PreviewDecoderSettings::instance().setPacks( packs );

    PreviewDecoderSettingsDialog dialog;
    for ( const auto* name : { "decoderPreviewRoutes", "decoderProtocolRoutes" } ) {
        INFO( name );
        auto* add = dialog.findChild<QPushButton*>( QStringLiteral( "%1AddPython" ).arg( name ) );
        REQUIRE( add );
        CHECK( add->isEnabled() );
        CHECK( !add->accessibleName().isEmpty() );
        REQUIRE( add->menu() );
        // Only the modules with a decode(): the helper is not offered.
        const auto actions = add->menu()->actions();
        REQUIRE( actions.size() == 3 );
        // Two packs with the same name are told apart by their IDs.
        CHECK( actions[ 0 ]->text() == "Alpha pack (org.example.alpha): summary" );
        CHECK( actions[ 0 ]->data() == "python:org.example.alpha/summary" );
        CHECK( actions[ 0 ]->toolTip().contains( "python/summary.py" ) );
        CHECK( actions[ 0 ]->toolTip().contains( "python:org.example.alpha/summary" ) );
        // The same decoder ID in another pack is its own choice; an ampersand is shown, not a
        // shortcut, and the name's markup is text.
        CHECK( actions[ 1 ]->text() == "Beta && <b>bold</b>: summary" );
        CHECK( actions[ 1 ]->data() == "python:org.example.beta/summary" );
        CHECK( actions[ 1 ]->toolTip().contains( "&lt;b&gt;bold&lt;/b&gt;" ) );
        CHECK( !actions[ 1 ]->toolTip().contains( "<b>bold" ) );
        CHECK( actions[ 2 ]->text() == "Alpha pack (org.example.gamma): summary" );
        CHECK( actions[ 2 ]->data() == "python:org.example.gamma/summary" );
        CHECK( actions[ 2 ]->toolTip().contains( "python:org.example.gamma/summary" ) );
    }

    // A route's decoder is shown with its pack, as a helper without decode(), or as not loaded.
    auto* previewRoutes = routeTable( dialog, "decoderPreviewRoutes" );
    REQUIRE( previewRoutes->rowCount() == 5 );
    const auto tip = [ previewRoutes ]( const QString& route ) {
        const auto row = rowNamed( previewRoutes, route );
        REQUIRE( row >= 0 );
        return previewRoutes->item( row, 1 )->toolTip();
    };
    CHECK( tip( "Demo" ).contains( "Alpha pack" ) );
    CHECK( tip( "Bold" ).contains( "Beta &amp; &lt;b&gt;bold&lt;/b&gt;" ) );
    CHECK( tip( "Helper" ).contains( "Alpha pack" ) );
    // The check is static and conservative: such a route is not called broken.
    CHECK( tip( "Helper" ).contains( "found no decode()" ) );
    CHECK( tip( "Helper" ).contains( "still works" ) );
    CHECK( !tip( "Helper" ).contains( "fails" ) );
    CHECK( tip( "Gone" ).contains( "not enabled" ) );
    CHECK( tip( "Shark" ).isEmpty() );

    // Choosing a decoder adds a route to it, ready for the preview's name.
    auto* add = dialog.findChild<QPushButton*>( "decoderPreviewRoutesAddPython" );
    add->menu()->actions()[ 2 ]->trigger();
    REQUIRE( previewRoutes->rowCount() == 6 );
    const auto added = previewRoutes->currentRow();
    REQUIRE( added == 5 );
    CHECK( previewRoutes->item( added, 1 )->text() == "python:org.example.gamma/summary" );
    CHECK( previewRoutes->item( added, 1 )->toolTip().contains( "Alpha pack" ) );
    previewRoutes->setItem( added, 0, new QTableWidgetItem( "Added" ) );
    dialog.findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Apply )->click();
    CHECK( dialog.findChild<QLabel*>( "decoderValidationError" )->text() == "Settings applied." );
    CHECK( PreviewDecoderSettings::instance()
               .configuration()
               .value( "previewRoutes" )
               .toMap()
               .value( "Added" )
           == pythonRoute( "org.example.gamma/summary" ) );

    // An edited target follows: a decoder the packs do not have is shown as not enabled.
    previewRoutes->item( added, 1 )->setText( "python:org.example.alpha/missing" );
    CHECK( previewRoutes->item( added, 1 )->toolTip().contains( "not enabled" ) );

    // Without packs' decoders there is nothing to choose; the open dialog follows the packs.
    PreviewDecoderSettings::instance().setPacks( {} );
    CHECK_FALSE( add->isEnabled() );
    CHECK( add->menu()->actions().isEmpty() );
    CHECK( tip( "Demo" ).contains( "not enabled" ) );
}

TEST_CASE( "A preview names the pack of the Python decoder that decoded it", "[packpython][E16]" )
{
    RestoreDecoder restore;
    PreviewDecoderPacks packs;
    packs.python = { QVariantMap{
        { "id", "org.example.alpha" },
        { "root", QDir::tempPath() },
        { "decoders", QVariantList{ QVariantMap{ { "id", "summary" },
                                                 { "module", "python/summary.py" } } } } } };
    packs.pythonNames = { { "org.example.alpha", "Alpha <pack>" } };
    packs.key = QStringLiteral( "label-%1" ).arg( QDateTime::currentMSecsSinceEpoch() );
    PreviewDecoderSettings::instance().setPacks( packs );
    const QVariantMap result{ { "backend", "python_decoder" },
                              { "state", "completed" },
                              { "decoder", "org.example.alpha/summary" } };
    CHECK( PreviewService::pythonDecoderLabel( result )
           == "org.example.alpha/summary (Alpha <pack>)" );
    // The tooltip is HTML: the pack's name is escaped.
    CHECK( PreviewService::tooltipHtml( result ).contains(
        "org.example.alpha/summary (Alpha &lt;pack&gt;)" ) );
    // A decoder whose pack is no longer enabled is named alone.
    PreviewDecoderSettings::instance().setPacks( {} );
    CHECK( PreviewService::pythonDecoderLabel( result ) == "org.example.alpha/summary" );
    CHECK( PreviewService::pythonDecoderLabel( { { "backend", "json_preview" } } ).isEmpty() );
}

TEST_CASE( "The Installed tab names a pack's Python decoders", "[packpython][E16]" )
{
    const auto features = hostFeatureVersions().toVariantMap();
    const auto decoders = []( int count ) {
        QVariantList list;
        for ( int index = 0; index < count; ++index ) {
            list.append(
                QVariantMap{ { "id", QStringLiteral( "d%1" ).arg( index ) },
                             { "module", QStringLiteral( "python/d%1.py" ).arg( index ) } } );
        }
        return list;
    };
    QVariantMap pack{
        { "kind", "config-pack" },
        { "pythonDecoders", 2 },
        { "pythonDecodersActive", true },
        { "content",
          QVariantMap{
              { "python_decoders",
                QVariantList{
                    QVariantMap{ { "id", "record" }, { "module", "python/record.py" } },
                    QVariantMap{ { "id", "common" }, { "module", "python/common.py" } } } } } }
    };
    CHECK( pluginsui::pythonDecodersState( pack, features ) == "Python decoders: record, common" );
    pack[ "pythonDecodersActive" ] = false;
    CHECK( pluginsui::pythonDecodersState( pack, features )
           == "Python decoders: record, common (inactive)" );
    // Many decoders: the first few, and how many in all.
    pack[ "pythonDecodersActive" ] = true;
    pack[ "pythonDecoders" ] = 6;
    pack[ "content" ] = QVariantMap{ { "python_decoders", decoders( 6 ) } };
    CHECK( pluginsui::pythonDecodersState( pack, features )
           == "Python decoders: d0, d1, d2, d3, … (6 in all)" );
    // An older host still says what the pack needs.
    CHECK( pluginsui::pythonDecodersState( pack, QVariantMap{ { "views", 2 } } )
           == "Python decoders: requires a newer CILogg" );
}
