// Configuration packs, slice E16a1 (docs/plugin-repositories.md sections 4.1-4.4).
#include <catch2/catch.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <memory>
#include <tuple>

#include "actionsconfigparser.h"
#include "actionsmanager.h"
#include "actionsrepository.h"
#include "actionsresponseswindow.h"
#include "actionstablemodel.h"
#include "configpack.h"
#include "configurationexport.h"
#include "highlightersdialog.h"
#include "highlighterset.h"
#include "highlightersmenu.h"
#include "importpreviewsdialog.h"
#include "persistentinfo.h"
#include "plugincatalog.h"
#include "pluginmanager.h"
#include "pluginmanifest.h"
#include "pluginrepository.h"
#include "pluginsandpacks.h"
#include "predefinedfilters.h"
#include "predefinedfilterscombobox.h"
#include "predefinedfiltersdialog.h"
#include "previewconfigparser.h"
#include "previewdecodersettings.h"
#include "previewdecodeutils.h"
#include "previewmanager.h"
#include "previewpackregistry.h"
#include "previewservice.h"

using namespace cilogg::plugins;

namespace {
constexpr auto PackId = "org.example.pack";

// A synthetic protocol: a header block that selects a body block by a template.
constexpr auto PackPreviews = R"json({
  "version": 1,
  "blocks": [
    {
      "name": "frame_header_block",
      "format": "match",
      "type": "string",
      "regex": "^(?<kind>\\w+) (?<rest>.*)$",
      "fields": [
        { "name": "kind", "source": "capture", "capture": "kind", "format": "string" },
        { "name": "body", "source": "block", "block": "body_{kind}_block" }
      ],
      "source": "capture",
      "capture": "payload"
    },
    {
      "name": "body_ping_block",
      "format": "match",
      "type": "string",
      "regex": "^(?<raw>.*)$",
      "fields": [ { "name": "raw", "source": "capture", "capture": "raw", "format": "string" } ],
      "source": "capture",
      "capture": "rest"
    },
    {
      "name": "unused_block",
      "format": "match",
      "type": "string",
      "regex": "^(?<raw>.*)$",
      "fields": [ { "name": "raw", "source": "capture", "capture": "raw", "format": "string" } ],
      "source": "capture",
      "capture": "rest"
    }
  ],
  "previews": [
    {
      "name": "%NAME%",
      "regex": "^PACK (?<payload>.*)$",
      "type": "string",
      "format": "fields",
      "bufferCapture": "payload",
      "fields": [ { "name": "frame", "source": "block", "block": "frame_header_block" } ]
    }
  ]
})json";

QByteArray packPreviews( const QString& name = QStringLiteral( "Pack frame" ) )
{
    return QString::fromUtf8( PackPreviews ).replace( "%NAME%", name ).toUtf8();
}

QJsonObject packManifest( const QString& id = PackId, int luaFiles = 2,
                          const QJsonObject& protocolRoutes = {} )
{
    QJsonArray load;
    for ( int index = 0; index < luaFiles; ++index ) {
        load.append( QStringLiteral( "file%1.lua" ).arg( index ) );
    }
    QJsonObject content{
        { "previews", "previews.json" },
        { "decoder_routes",
          QJsonObject{ { "previewRoutes",
                         QJsonObject{ { "Pack frame", QJsonObject{ { "dissector", "packproto" } } } } },
                       { "protocolRoutes", protocolRoutes } } },
        { "python_decoders",
          QJsonArray{ QJsonObject{ { "id", "summary" }, { "module", "python/summary.py" } } } }
    };
    if ( luaFiles > 0 ) {
        content[ "lua" ] = QJsonObject{ { "folder", "lua" }, { "load", load } };
    }
    return { { "id", id },
             { "name", QStringLiteral( "Pack %1" ).arg( id ) },
             { "version", "1.0.0" },
             { "protocol_version", 1 },
             { "kind", "config-pack" },
             { "pack_format", 1 },
             { "content", content } };
}

void writeFile( const QString& path, const QByteArray& bytes )
{
    REQUIRE( QDir().mkpath( QFileInfo( path ).absolutePath() ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
}

// A pack folder at <root>/plugins/<id>, as an installed pack.
QString writePack( const QString& root, const QJsonObject& manifest,
                   const QString& previewName = QStringLiteral( "Pack frame" ) )
{
    const auto id = manifest.value( "id" ).toString();
    const auto folder = QDir( root ).filePath( "plugins/" + id );
    writeFile( folder + "/plugin.json", QJsonDocument( manifest ).toJson() );
    writeFile( folder + "/previews.json", packPreviews( previewName ) );
    writeFile( folder + "/python/summary.py", "def decode(data):\n    return {}\n" );
    const auto lua = manifest.value( "content" ).toObject().value( "lua" ).toObject();
    for ( const auto& file : lua.value( "load" ).toArray() ) {
        writeFile( folder + "/lua/" + file.toString(), "-- synthetic pack Lua\n" );
    }
    return folder;
}

QString manifestError( QJsonObject json )
{
    return Manifest::parse( QJsonDocument( json ).toJson(), {} ).error;
}

QJsonObject withContent( QJsonObject manifest, const QString& key, const QJsonValue& value )
{
    auto content = manifest.value( "content" ).toObject();
    if ( value.isUndefined() ) {
        content.remove( key );
    }
    else {
        content[ key ] = value;
    }
    manifest[ "content" ] = content;
    return manifest;
}

PreviewPackSource parsedPack( const QString& id, const QString& name,
                              const QByteArray& previews )
{
    const auto parsed = PreviewConfigParser().parseJson( previews );
    REQUIRE( parsed.errors.isEmpty() );
    return { id, name, parsed.previews, parsed.blocks };
}

void importUserPreviews( const QByteArray& json )
{
    QTemporaryFile file;
    REQUIRE( file.open() );
    REQUIRE( file.write( json ) == json.size() );
    REQUIRE( file.flush() );
    REQUIRE( PreviewManager::instance().importFromFile( file.fileName() ).ok );
}

const PreviewDefinition* findIn( const QVector<PreviewDefinition>& previews, const QString& name )
{
    const auto it = std::find_if( previews.cbegin(), previews.cend(),
                                  [ &name ]( const auto& preview ) { return preview.name == name; } );
    return it == previews.cend() ? nullptr : &*it;
}
} // namespace

TEST_CASE( "Configuration pack manifests follow the pack rules", "[plugins][E16]" )
{
    const auto valid = packManifest();
    CHECK( validatePackManifest( valid ).isEmpty() );
    const auto parsed = Manifest::parse( QJsonDocument( valid ).toJson(), {} );
    CHECK( parsed.valid() );
    CHECK( parsed.executable.isEmpty() );
    CHECK( isConfigPack( parsed.json ) );
    CHECK( supportedKind( "config-pack" ) );
    CHECK( packContents( valid ) == QStringList{ "previews", "lua", "python_decoders" } );

    SECTION( "a pack declares no process keys" )
    {
        for ( const auto& key : packProcessKeys() ) {
            auto json = valid;
            json[ key ] = key == "executable" ? QJsonValue( "tool.exe" ) : QJsonValue( QJsonArray{} );
            INFO( key.toStdString() );
            CHECK( manifestError( json ).contains( key ) );
        }
    }
    SECTION( "pack_format and content are required" )
    {
        auto json = valid;
        json[ "pack_format" ] = 2;
        CHECK( manifestError( json ).contains( "pack_format" ) );
        json = valid;
        json[ "content" ] = QJsonObject{};
        CHECK( !manifestError( json ).isEmpty() );
        CHECK( manifestError( withContent( valid, "settings", "x.json" ) ).contains( "settings" ) );
        CHECK( !manifestError( withContent( valid, "previews", "../previews.json" ) ).isEmpty() );
    }
    SECTION( "Lua files: 1 to 8 listed .lua files, each once" )
    {
        CHECK( !manifestError( packManifest( PackId, 9 ) ).isEmpty() );
        const auto lua = []( const QJsonArray& load ) {
            return QJsonObject{ { "folder", "lua" }, { "load", load } };
        };
        CHECK( !manifestError( withContent( valid, "lua", lua( { "a.txt" } ) ) ).isEmpty() );
        CHECK( !manifestError( withContent( valid, "lua", lua( { "a.lua", "A.LUA" } ) ) ).isEmpty() );
        CHECK( !manifestError( withContent( valid, "lua", lua( {} ) ) ).isEmpty() );
        CHECK( manifestError( withContent( valid, "lua", lua( { "sub/a.lua" } ) ) ).isEmpty() );
    }
    SECTION( "routes keep the decoder's rules and limits" )
    {
        QJsonObject protocol;
        for ( int index = 0; index < 33; ++index ) {
            protocol[ QStringLiteral( "proto%1" ).arg( index ) ]
                = QJsonObject{ { "dissector", "data" } };
        }
        CHECK( manifestError( packManifest( PackId, 2, protocol ) ).contains( "protocolRoutes" ) );
        const auto bad = QJsonObject{ { "x", QJsonObject{ { "dissector", "1bad" } } } };
        CHECK( !manifestError( packManifest( PackId, 2, bad ) ).isEmpty() );
        const auto encoding
            = QJsonObject{ { "x", QJsonObject{ { "dissector", "data" }, { "inputEncoding", "hex" } } } };
        CHECK( manifestError( packManifest( PackId, 2, encoding ) ).isEmpty() );
        CHECK( !manifestError( withContent( valid, "previews", QJsonValue::Undefined ) ).isEmpty() );
    }
    SECTION( "no content file is the previews file or plugin.json" )
    {
        // CILogg writes those two itself for a pack's effective content (design 4.6).
        const auto reserved = QStringLiteral( "plugin.json or previews file" );
        CHECK( manifestError( withContent( valid, "actions", "previews.json" ) ).contains( reserved ) );
        CHECK( manifestError( withContent( valid, "highlights", "PREVIEWS.json" ) ).contains( reserved ) );
        CHECK( manifestError( withContent( valid, "filters", "plugin.json" ) ).contains( reserved ) );
        CHECK( manifestError( withContent( valid, "previews", "Plugin.json" ) ).contains( "plugin.json" ) );
        CHECK( manifestError( withContent( valid, "actions", "actions.json" ) ).isEmpty() );
        // Spellings that differ only in case are one file on Windows and macOS.
        const auto aliased = withContent( withContent( valid, "actions", "defs/Rules.json" ),
                                          "filters", "defs/rules.json" );
        CHECK( manifestError( aliased ).contains( "differ only in case" ) );
        // CILogg writes the filters file from the pack's filters (E16c): a file carried as it is
        // may not be the same file, or it would replace them (Codex review of #165).
        const auto filtersFile = QStringLiteral( "is the pack's filters file" );
        CHECK( manifestError( withContent( withContent( valid, "actions", "defs/rules.conf" ),
                                           "filters", "defs/rules.conf" ) )
                   .contains( filtersFile ) );
        CHECK( manifestError( withContent( withContent( valid, "highlights", "f.conf" ), "filters",
                                           "f.conf" ) )
                   .contains( filtersFile ) );
        CHECK( manifestError( withContent( valid, "filters", "python/summary.py" ) )
                   .contains( filtersFile ) );
        // The highlighters file is written by CILogg as well (E16c2).
        const auto highlightsFile = QStringLiteral( "is the pack's highlighters file" );
        CHECK( manifestError( withContent( withContent( valid, "actions", "a.json" ), "highlights",
                                           "a.json" ) )
                   .contains( highlightsFile ) );
        CHECK( manifestError( withContent( valid, "highlights", "python/summary.py" ) )
                   .contains( highlightsFile ) );
        CHECK( manifestError( withContent( withContent( valid, "actions", "a.json" ), "highlights",
                                           "h.conf" ) )
                   .isEmpty() );
        // And so is the actions file (E16c3).
        const auto actionsFile = QStringLiteral( "is the pack's actions file" );
        CHECK( manifestError( withContent( valid, "actions", "python/summary.py" ) )
                   .contains( actionsFile ) );
        CHECK( manifestError( withContent( valid, "actions", "lua/file0.lua" ) )
                   .contains( actionsFile ) );
    }
    SECTION( "Python decoders need an id and a .py module" )
    {
        const auto decoders = []( const QString& id, const QString& module ) {
            return QJsonArray{ QJsonObject{ { "id", id }, { "module", module } } };
        };
        CHECK( !manifestError( withContent( valid, "python_decoders", decoders( "Bad Id", "a.py" ) ) )
                    .isEmpty() );
        CHECK( !manifestError( withContent( valid, "python_decoders", decoders( "ok", "a.lua" ) ) )
                    .isEmpty() );
    }
    SECTION( "the catalog offers packs" )
    {
        QJsonObject entry{ { "id", PackId },
                           { "version", "1.0.0" },
                           { "protocol_version", 1 },
                           { "kind", "config-pack" },
                           { "contents", QJsonArray{ "previews", "lua" } },
                           { "os", "any" },
                           { "arch", "any" },
                           { "url", "https://example.com/pack.zip" },
                           { "sha256", QString( 64, 'a' ) } };
        const auto catalog = QJsonDocument(
                                 QJsonObject{ { "schema_version", 1 }, { "plugins", QJsonArray{ entry } } } )
                                 .toJson();
        const auto read = readCatalog( catalog, "repo", "26.4", "windows", "x64" );
        REQUIRE( read.entries.size() == 1 );
        CHECK( read.entries.first().toObject().value( "kind" ) == "config-pack" );
        CHECK( read.hidden.isEmpty() );
    }
}

TEST_CASE( "Configuration pack files are checked when the pack is read", "[plugins][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto folder = writePack( root.path(), packManifest() );
    const auto manifest = Manifest::read( folder );
    REQUIRE( manifest.valid() );
    const auto summary = packSummary( manifest.json, folder );
    CHECK( summary.value( "previews" ).toInt() == 1 );
    CHECK( summary.value( "luaFiles" ).toInt() == 2 );
    CHECK( summary.value( "pythonDecoders" ).toInt() == 1 );
    CHECK( summary.value( "filters" ).toInt() == 0 );
    CHECK( summary.value( "contents" ).toStringList()
           == QStringList{ "previews", "lua", "python_decoders" } );

    SECTION( "a pack's predefined filters are counted (E16c)" )
    {
        auto withFilters = packManifest();
        auto content = withFilters[ "content" ].toObject();
        content[ "filters" ] = "filters.conf";
        withFilters[ "content" ] = content;
        writeFile( folder + "/plugin.json", QJsonDocument( withFilters ).toJson() );
        writeFile( folder + "/filters.conf", "[PredefinedFiltersCollection]\nversion=3\n"
                                             "filters\\size=2\n"
                                             "filters\\1\\id=a\nfilters\\1\\name=A\n"
                                             "filters\\1\\filter=a\nfilters\\1\\regex=true\n"
                                             "filters\\2\\id=b\nfilters\\2\\name=B\n"
                                             "filters\\2\\filter=b\nfilters\\2\\regex=false\n" );
        const auto read = Manifest::read( folder );
        REQUIRE( read.valid() );
        const auto counted = packSummary( read.json, folder );
        CHECK( counted.value( "filters" ).toInt() == 2 );
        CHECK( counted.value( "contents" ).toStringList()
               == QStringList{ "previews", "filters", "lua", "python_decoders" } );
    }
    SECTION( "a pack's actions and responses are counted (E16c3)" )
    {
        CHECK( summary.value( "packActions" ).toInt() == 0 );
        CHECK( summary.value( "packResponses" ).toInt() == 0 );
        auto withActions = packManifest();
        auto content = withActions[ "content" ].toObject();
        content[ "actions" ] = "actions.json";
        withActions[ "content" ] = content;
        writeFile( folder + "/plugin.json", QJsonDocument( withActions ).toJson() );
        writeFile( folder + "/actions.json",
                   R"({ "version": 4,
  "actions": [ { "id": 1, "name": "Status", "sequence": { "type": "string", "value": "s\n" } },
               { "id": 2, "name": "Reset", "sequence": { "type": "string", "value": "r\n" } } ],
  "responses": [ { "id": 1, "name": "Seen", "match": { "type": "string", "value": "x" },
                   "response": { "action_id": 1 } } ] })" );
        const auto read = Manifest::read( folder );
        REQUIRE( read.valid() );
        const auto counted = packSummary( read.json, folder );
        CHECK( counted.value( "packActions" ).toInt() == 2 );
        CHECK( counted.value( "packResponses" ).toInt() == 1 );
        CHECK( counted.value( "contentWarnings" ).toStringList().isEmpty() );
        // An actions file that is not an export costs only its own items, with the reason.
        writeFile( folder + "/actions.json", "// mine\n{}" );
        const auto bad = packSummary( Manifest::read( folder ).json, folder );
        CHECK( bad.value( "packActions" ).toInt() == 0 );
        REQUIRE( bad.value( "contentWarnings" ).toStringList().size() == 1 );
        CHECK( bad.value( "contentWarnings" ).toStringList().first().contains( "actions file" ) );
    }
    SECTION( "every Lua file of the folder is listed" )
    {
        writeFile( folder + "/lua/extra.lua", "-- not listed\n" );
        CHECK( Manifest::read( folder ).error.contains( "extra.lua" ) );
    }
    SECTION( "listed files exist" )
    {
        REQUIRE( QFile::remove( folder + "/python/summary.py" ) );
        CHECK( Manifest::read( folder ).error.contains( "summary.py" ) );
    }
    SECTION( "files stay within 1 MiB" )
    {
        writeFile( folder + "/lua/file0.lua", QByteArray( 1024 * 1024 + 1, '-' ) );
        CHECK( Manifest::read( folder ).error.contains( "1 MiB" ) );
    }
    SECTION( "preview routes name the pack's own previews" )
    {
        writeFile( folder + "/previews.json", packPreviews( "Another frame" ) );
        CHECK( Manifest::read( folder ).error.contains( "Pack frame" ) );
    }
}

TEST_CASE( "The example pack is a valid configuration pack", "[plugins][E16]" )
{
    const QString folder = QStringLiteral( CILOGG_EXAMPLE_PACK_PATH );
    const auto manifest = Manifest::read( folder );
    INFO( manifest.error.toStdString() );
    REQUIRE( manifest.valid() );
    CHECK( isConfigPack( manifest.json ) );
    const auto pack = PreviewPackRegistry::read( manifest.id, folder, manifest.json );
    CHECK( pack.error.isEmpty() );
    CHECK( pack.previews.previews.size() == 1 );
    CHECK( pack.luaFiles.size() == 2 );
    CHECK( pack.luaFiles.first().endsWith( "lua/common.lua" ) );
    CHECK( pack.previewRoutes.contains( "Demo frame" ) );
    const auto renamed = PreviewManager::namespaced( pack.previews );
    CHECK( !PreviewService::definitionsFor( renamed.previews, renamed.blocks ).isEmpty() );
    // Its predefined filters (E16c), in the pack's order, with the pack's name.
    REQUIRE( pack.filters.size() == 2 );
    CHECK( pack.filters.first().filter.id == "cilogg-demo-frames" );
    CHECK( pack.filters.first().filter.useRegex );
    CHECK( !pack.filters.last().filter.useRegex );
    CHECK( pack.filters.last().packName == "Example protocol pack" );
    CHECK( packSummary( manifest.json, folder ).value( "filters" ).toInt() == 2 );
    // Its action and automatic response (E16c3), with the pack's own ids.
    CHECK( pack.warnings.isEmpty() );
    REQUIRE( pack.actions.size() == 1 );
    REQUIRE( pack.responses.size() == 1 );
    CHECK( pack.actions.first().id == 1 );
    REQUIRE( pack.responses.first().response.steps.size() == 1 );
    CHECK( pack.responses.first().response.steps.first().actionId == 1 );
    CHECK( packSummary( manifest.json, folder ).value( "packActions" ).toInt() == 1 );
    CHECK( packSummary( manifest.json, folder ).value( "packResponses" ).toInt() == 1 );
}

TEST_CASE( "Configuration packs have no process", "[plugins][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    writePack( root.path(), packManifest() );
    // No shipped plugins: only the pack.
    PluginManager manager( root.path(), QDir( root.path() ).filePath( "none" ) );
    manager.discover();
    const auto list = manager.plugins();
    REQUIRE( list.size() == 1 );
    const auto pack = list.first().toMap();
    CHECK( pack.value( "kind" ) == "config-pack" );
    CHECK( pack.value( "error" ).toString().isEmpty() );
    CHECK( pack.value( "contents" ).toStringList().contains( "lua" ) );
    CHECK( pack.value( "previews" ).toInt() == 1 );
    CHECK( pack.value( "luaFiles" ).toInt() == 2 );
    CHECK( manager.manifest( PackId ).value( "kind" ) == "config-pack" );

    CHECK( !manager.manage( "restart", PackId, {} ).ok() );
    CHECK( !manager.manage( "configure", PackId, { { "configuration", QVariantMap{} } } ).ok() );
    CHECK( !manager.invoke( PackId, "anything", {}, {} ).ok() );

    QString checkedId;
    manager.setPackCheck( [ &checkedId ]( const QString& id, const QString&, const QJsonObject& ) {
        checkedId = id;
        return QStringLiteral( "refused for the test" );
    } );
    const auto refused = manager.manage( "enable", PackId, {} );
    CHECK( refused.error == "refused for the test" );
    CHECK( checkedId == PackId );
    CHECK( !manager.plugins().first().toMap().value( "enabled" ).toBool() );

    manager.setPackCheck( []( const QString&, const QString&, const QJsonObject& ) {
        return QString{};
    } );
    CHECK( manager.manage( "enable", PackId, {} ).ok() );
    CHECK( manager.plugins().first().toMap().value( "enabled" ).toBool() );
    CHECK( manager.pluginState( PackId ) == "stopped" );
    CHECK( manager.manage( "disable", PackId, {} ).ok() );
}

TEST_CASE( "Pack previews are namespaced, preceded by the user's and never saved",
           "[previewdecoder][E16]" )
{
    auto& manager = PreviewManager::instance();
    REQUIRE( manager.clearAll() );
    const auto pack = parsedPack( PackId, "Example pack", packPreviews() );

    SECTION( "pack blocks are renamed with their references" )
    {
        const auto renamed = PreviewManager::namespaced( pack );
        CHECK( renamed.blocks.contains( "org.example.pack/frame_header_block" ) );
        CHECK( renamed.previews.first().fields.first().blockTemplate
               == "org.example.pack/frame_header_block" );
        CHECK( renamed.blocks.value( "org.example.pack/frame_header_block" ).fields.at( 1 ).blockTemplate
               == "org.example.pack/body_{kind}_block" );
        CHECK( PreviewManager::blockClosure( pack.previews.first().fields, pack.blocks )
               == QStringList{ "frame_header_block", "body_ping_block" } );
    }
    SECTION( "the user's items stay apart and take precedence" )
    {
        manager.setPackPreviews( { pack } );
        CHECK( manager.all().isEmpty() );
        CHECK( findIn( manager.effective(), "Pack frame" ) );
        CHECK( manager.effectiveBlocks().contains( "org.example.pack/body_ping_block" ) );
        CHECK( manager.packOrigin( "Pack frame" )->packId == PackId );
        // A pack's own blocks win inside its namespace.
        CHECK( manager.effectiveBlocks().value( "org.example.pack/body_ping_block" ).regex
               == pack.blocks.value( "body_ping_block" ).regex );
        CHECK( !PreviewService::definitionsFor( manager.effective(), manager.effectiveBlocks() )
                    .isEmpty() );

        importUserPreviews( QString::fromUtf8( PackPreviews )
                                .replace( "%NAME%", "Pack frame" )
                                .replace( "^PACK", "^MINE" )
                                .toUtf8() );
        CHECK( manager.replacedByUser( "Pack frame" ) );
        const auto effective = manager.effective();
        CHECK( std::count_if( effective.cbegin(), effective.cend(), []( const auto& preview ) {
                   return preview.name == "Pack frame";
               } ) == 1 );
        CHECK( findIn( effective, "Pack frame" )->regex.startsWith( "^MINE" ) );

        // Saved and reloaded: previews.json has the user's items only.
        manager.loadFromRepository();
        CHECK( manager.all().size() == 1 );
        CHECK( !manager.blocks().contains( "org.example.pack/frame_header_block" ) );
    }
    SECTION( "Copy to My Previews copies the block closure and renames colliding blocks" )
    {
        importUserPreviews( R"json({ "version": 1, "blocks": [ {
            "name": "frame_header_block", "format": "match", "type": "string",
            "regex": "^(?<raw>.*)$", "source": "capture", "capture": "payload",
            "fields": [ { "name": "raw", "source": "capture", "capture": "raw", "format": "string" } ]
          } ], "previews": [ { "name": "Mine", "regex": "^MINE (?<payload>.*)$", "type": "string",
            "format": "fields", "bufferCapture": "payload",
            "fields": [ { "name": "frame", "source": "block", "block": "frame_header_block" } ] } ] })json" );
        manager.setPackPreviews( { pack } );
        CHECK( manager.copyPackPreview( "Pack frame" ).isEmpty() );
        const auto* copy = manager.findByName( "Pack frame" );
        REQUIRE( copy );
        CHECK( copy->fields.first().blockTemplate == "frame_header_block (Example pack)" );
        CHECK( manager.blocks().contains( "frame_header_block" ) );
        CHECK( manager.blocks().contains( "frame_header_block (Example pack)" ) );
        CHECK( manager.blocks().contains( "body_ping_block (Example pack)" ) );
        CHECK( !manager.blocks().contains( "unused_block" ) );
        CHECK( !manager.blocks().contains( "unused_block (Example pack)" ) );
        CHECK( manager.blocks().value( "frame_header_block (Example pack)" ).fields.at( 1 ).blockTemplate
               == "body_{kind}_block (Example pack)" );
        CHECK( manager.replacedByUser( "Pack frame" ) );
        CHECK( !manager.copyPackPreview( "Pack frame" ).isEmpty() );
    }
    SECTION( "a copy of a preview of a pack that is not enabled keeps that preview's on/off state" )
    {
        // Another, enabled pack has a preview of the same name, switched on.
        const auto other = parsedPack( "b.pack", "Pack B", packPreviews( "Shared" ) );
        manager.setPackPreviews( { other } );
        // Switched off by its definition.
        const auto off = parsedPack( "a.pack", "Pack A",
                                     QString::fromUtf8( packPreviews( "Shared" ) )
                                         .replace( "\"name\": \"Shared\"",
                                                   "\"name\": \"Shared\", \"enabled\": false" )
                                         .toUtf8() );
        CHECK( manager.copyPreviewFrom( off, "Shared", "Shared (mine)" ).isEmpty() );
        REQUIRE( manager.findByName( "Shared (mine)" ) );
        CHECK_FALSE( manager.findByName( "Shared (mine)" )->enabled );
        // Switched off by the choice saved for it while its pack was enabled.
        const auto alone = parsedPack( "c.pack", "Pack C", packPreviews( "Alone" ) );
        manager.setPackPreviews( { alone } );
        manager.setEnabled( "Alone", false );
        manager.setPackPreviews( {} );
        CHECK( manager.copyPreviewFrom( alone, "Alone", "Alone (mine)" ).isEmpty() );
        REQUIRE( manager.findByName( "Alone (mine)" ) );
        CHECK_FALSE( manager.findByName( "Alone (mine)" )->enabled );
        manager.setPackPreviews( { alone } );
        manager.setEnabled( "Alone", true );
    }
    SECTION( "a copy under another name never replaces an enabled pack's preview" )
    {
        const auto a = parsedPack( "a.pack", "Pack A", packPreviews( "A frame" ) );
        const auto b = parsedPack( "b.pack", "Pack B",
                                   QString::fromUtf8( packPreviews( "A frame (mine)" ) )
                                       .replace( "frame_header_block", "b_header_block" )
                                       .toUtf8() );
        manager.setPackPreviews( { a, b } );
        CHECK( manager.copyPackPreview( "A frame", "A frame (mine)" )
                   .contains( "An enabled pack has a preview named \"A frame (mine)\"" ) );
        CHECK( !manager.findByName( "A frame (mine)" ) );
        CHECK( manager.packOrigin( "A frame (mine)" )->packId == "b.pack" );
        // Under its own name a copy replaces its original, as Copy to My Previews does.
        CHECK( manager.copyPackPreview( "A frame" ).isEmpty() );
    }
    SECTION( "a copied block never takes the name of another enabled pack's block" )
    {
        // Pack A has a block literally named "b.pack/frame_header_block", which is also what
        // pack B's own frame_header_block is called once applied.
        const auto a = parsedPack( "a.pack", "Pack A",
                                   QString::fromUtf8( packPreviews( "A frame" ) )
                                       .replace( "frame_header_block", "b.pack/frame_header_block" )
                                       .replace( "^(?<kind>", "^A(?<kind>" )
                                       .toUtf8() );
        const auto b = parsedPack( "b.pack", "Pack B", packPreviews( "B frame" ) );
        manager.setPackPreviews( { a, b } );
        REQUIRE( manager.effectiveBlocks().contains( "b.pack/frame_header_block" ) );
        CHECK( manager.copyPackPreview( "A frame" ).isEmpty() );
        const auto* copy = manager.findByName( "A frame" );
        REQUIRE( copy );
        const auto header = copy->fields.first().blockTemplate;
        CHECK( header == "b.pack/frame_header_block (Pack A)" );
        CHECK( !manager.blocks().contains( "b.pack/frame_header_block" ) );
        // The copy runs pack A's block, not pack B's.
        CHECK( manager.effectiveBlocks().value( header ).regex.startsWith( "^A" ) );
    }
    SECTION( "the on/off choice of a pack preview is user state" )
    {
        manager.setPackPreviews( { pack } );
        manager.setEnabled( "Pack frame", false );
        CHECK( !manager.packPreviews().first().enabled );
        manager.setPackPreviews( { pack } );
        CHECK( !manager.packPreviews().first().enabled );
        CHECK( !findIn( manager.effectiveEnabled(), "Pack frame" ) );
        manager.setEnabled( "Pack frame", true );
        CHECK( manager.packPreviews().first().enabled );
    }
    manager.setPackPreviews( {} );
    REQUIRE( manager.clearAll() );
}

TEST_CASE( "The previews dialog lists pack previews after the user's and copies them",
           "[previewimport][E16]" )
{
    auto& manager = PreviewManager::instance();
    REQUIRE( manager.clearAll() );
    importUserPreviews( QString::fromUtf8( PackPreviews )
                            .replace( "%NAME%", "Mine" )
                            .replace( "^PACK", "^MINE" )
                            .toUtf8() );
    manager.setPackPreviews( { parsedPack( PackId, "Example pack", packPreviews() ) } );

    ImportPreviewsDialog dialog;
    dialog.show();
    REQUIRE( QTest::qWaitForWindowExposed( &dialog ) );
    auto* table = dialog.findChild<QTableView*>( "importPreviewsTable" );
    auto* copy = dialog.findChild<QToolButton*>( "copyPackPreviewButton" );
    auto* remove = dialog.findChild<QToolButton*>( "removePreviewButton" );
    auto* moveUp = dialog.findChild<QToolButton*>( "movePreviewUpButton" );
    REQUIRE( table );
    REQUIRE( copy );
    REQUIRE( remove );
    REQUIRE( table->model()->rowCount() == 2 );
    CHECK( table->model()->index( 0, 3 ).data().toString() == "My previews" );
    CHECK( table->model()->index( 1, 0 ).data().toString() == "Pack frame" );
    CHECK( table->model()->index( 1, 3 ).data().toString() == "Example pack" );

    // Edit... shows only with an editor to open (the main window gives the plugin controller's).
    auto* edit = dialog.findChild<QToolButton*>( "editPackPreviewButton" );
    REQUIRE( edit );
    CHECK_FALSE( edit->isVisible() );
    QStringList opened;
    dialog.setPackItemEditor( [ &opened ]( QWidget*, const QString& packId, const QString& path ) {
        opened << packId << path;
    } );
    CHECK( edit->isVisible() );

    // The user's row: Remove, no Copy or Edit.
    table->selectRow( 0 );
    table->setCurrentIndex( table->model()->index( 0, 0 ) );
    CHECK( remove->isEnabled() );
    CHECK_FALSE( copy->isEnabled() );
    CHECK_FALSE( edit->isEnabled() );
    // The pack's row: Copy and Edit, but neither Remove nor Move.
    table->selectRow( 1 );
    table->setCurrentIndex( table->model()->index( 1, 0 ) );
    CHECK( copy->isEnabled() );
    CHECK( edit->isEnabled() );
    CHECK_FALSE( remove->isEnabled() );
    CHECK_FALSE( moveUp->isEnabled() );

    // Edit... opens the pack item's store path (design 4.6).
    QTest::mouseClick( edit, Qt::LeftButton );
    CHECK( opened == QStringList{ PackId, "previews/%50ack%20frame.json" } );

    QTest::mouseClick( copy, Qt::LeftButton );
    REQUIRE( manager.findByName( "Pack frame" ) );
    REQUIRE( table->model()->rowCount() == 3 );
    CHECK( table->model()->index( 2, 3 ).data().toString().contains( "replaced by your item" ) );
    CHECK_FALSE( table->model()
                     ->flags( table->model()->index( 2, 2 ) )
                     .testFlag( Qt::ItemIsUserCheckable ) );

    manager.setPackPreviews( {} );
    REQUIRE( manager.clearAll() );
}

TEST_CASE( "Generic block fallbacks keep a pack's prefix and a copy's suffix",
           "[previewdecoder][E16]" )
{
    CHECK( previewFallbackBlockNames( "at_command_foo_block" )
           == QStringList{ "at_command_generic_args_block" } );
    CHECK( previewFallbackBlockNames( "org.example.pack/at_response_x_block" )
           == QStringList{ "org.example.pack/at_response_generic_args_block",
                           "at_response_generic_args_block" } );
    CHECK( previewFallbackBlockNames( "at_command_foo_block (Example pack)" )
           == QStringList{ "at_command_generic_args_block (Example pack)",
                           "at_command_generic_args_block" } );
    CHECK( previewFallbackBlockNames( "other_block" ).isEmpty() );
    CHECK( previewFallbackBlockNames( "at_command_foo" ).isEmpty() );

    // A copy whose generic block was renamed still finds it through the fallback.
    auto& manager = PreviewManager::instance();
    REQUIRE( manager.clearAll() );
    constexpr auto Generic = R"json({ "version": 1, "blocks": [
        { "name": "at_command_generic_args_block", "format": "match", "type": "string",
          "regex": "^(?<raw>.*)$", "source": "capture", "capture": "args",
          "fields": [ { "name": "raw", "source": "capture", "capture": "raw", "format": "string" } ] },
        { "name": "at_command_ping_block", "format": "match", "type": "string",
          "regex": "^(?<raw>.*)$", "source": "capture", "capture": "args",
          "fields": [ { "name": "raw", "source": "capture", "capture": "raw", "format": "string" } ] }
      ], "previews": [ { "name": "%NAME%", "regex": "^AT(?<command_key>\\w+)(?<args>.*)$",
        "type": "string", "format": "fields",
        "fields": [ { "name": "details", "source": "block", "block": "at_command_{command_key}_block" },
                    { "name": "outside", "source": "block", "block": "not_in_pack_block" } ] } ] })json";
    importUserPreviews( QString::fromUtf8( Generic ).replace( "%NAME%", "Mine" ).toUtf8() );
    manager.setPackPreviews( { parsedPack( PackId, "Example pack",
                                           QString::fromUtf8( Generic )
                                               .replace( "%NAME%", "AT pack" )
                                               .toUtf8() ) } );
    REQUIRE( manager.copyPackPreview( "AT pack" ).isEmpty() );
    const auto* copy = manager.findByName( "AT pack" );
    REQUIRE( copy );
    CHECK( copy->fields.at( 0 ).blockTemplate == "at_command_{command_key}_block (Example pack)" );
    // A reference to a block the pack does not have is left as it is.
    CHECK( copy->fields.at( 1 ).blockTemplate == "not_in_pack_block" );
    const auto fallbacks = previewFallbackBlockNames( "at_command_unknown_block (Example pack)" );
    REQUIRE( !fallbacks.isEmpty() );
    CHECK( manager.blocks().contains( fallbacks.first() ) );
    manager.setPackPreviews( {} );
    REQUIRE( manager.clearAll() );
}

TEST_CASE( "Pack previews give way when the user's previews grow past the limits",
           "[previewdecoder][E16]" )
{
    auto& manager = PreviewManager::instance();
    REQUIRE( manager.clearAll() );
    importUserPreviews( QString::fromUtf8( PackPreviews )
                            .replace( "%NAME%", "Mine" )
                            .replace( "^PACK", "^MINE" )
                            .toUtf8() );
    auto pack = parsedPack( PackId, "Example pack", packPreviews() );
    const auto model = pack.previews.first();
    for ( int index = 0; index < 128; ++index ) {
        auto preview = model;
        preview.name = QStringLiteral( "Pack frame %1" ).arg( index );
        pack.previews.append( preview );
    }
    manager.setPackPreviews( { pack } );
    CHECK( PreviewService::definitionsFor( manager.effective(), manager.effectiveBlocks() ).isEmpty() );
    const auto definitions = QJsonDocument::fromJson( PreviewService::definitions() ).object();
    const auto names = definitions.value( "previews" ).toArray();
    REQUIRE( names.size() == 1 );
    CHECK( names.first().toObject().value( "name" ) == "Mine" );
    manager.setPackPreviews( {} );
    REQUIRE( manager.clearAll() );
}

#ifndef Q_OS_WIN
TEST_CASE( "Pack files reached through a link are refused", "[plugins][E16]" )
{
    QTemporaryDir root;
    QTemporaryDir outside;
    REQUIRE( root.isValid() );
    REQUIRE( outside.isValid() );
    auto manifest = packManifest();
    manifest = withContent( manifest, "lua",
                            QJsonObject{ { "folder", "lua" }, { "load", QJsonArray{ "sub/a.lua" } } } );
    const auto folder = writePack( root.path(), manifest );
    // writePack wrote lua/sub/a.lua; replace the folder with a link to one elsewhere.
    REQUIRE( QDir( folder + "/lua/sub" ).removeRecursively() );
    writeFile( outside.path() + "/a.lua", "-- elsewhere\n" );
    REQUIRE( QFile::link( outside.path(), folder + "/lua/sub" ) );
    CHECK( Manifest::read( folder ).error.contains( "link" ) );
}
#endif

TEST_CASE( "Pack Lua and routes join the decoder snapshot only", "[previewdecoder][E16]" )
{
    auto& settings = PreviewDecoderSettings::instance();
    const auto original = settings.configuration();
    auto user = original;
    user[ "previewRoutes" ] = QVariantMap{ { "Shared", QVariantMap{ { "dissector", "user" } } } };
    REQUIRE( settings.setConfiguration( user ) );

    PreviewDecoderPacks packs;
    packs.lua = { QVariantMap{ { "id", PackId }, { "files", QStringList{ "C:/pack/a.lua" } } } };
    packs.previewRoutes = { { "Shared", QVariantMap{ { "dissector", "pack" } } },
                            { "Pack frame", QVariantMap{ { "dissector", "packproto" } } } };
    const auto before = settings.revision();
    settings.setPacks( packs );
    CHECK( settings.revision() != before );

    const auto snapshot = settings.snapshot().configuration;
    const auto routes = snapshot.value( "previewRoutes" ).toMap();
    CHECK( routes.value( "Shared" ).toMap().value( "dissector" ) == "user" );
    CHECK( routes.value( "Pack frame" ).toMap().value( "dissector" ) == "packproto" );
    CHECK( snapshot.value( "packLua" ).toList() == packs.lua );
    // The saved configuration never holds pack items.
    CHECK( !settings.configuration().value( "previewRoutes" ).toMap().contains( "Pack frame" ) );
    CHECK( !settings.configuration().contains( "packLua" ) );

    const auto unchanged = settings.revision();
    settings.setPacks( packs );
    CHECK( settings.revision() == unchanged );
    // A new pack version whose Lua keeps its file names still starts new sessions.
    auto updated = packs;
    updated.key = "org.example.pack@1.1.0";
    settings.setPacks( updated );
    CHECK( settings.revision() != unchanged );

    // A preview of the user's with the pack preview's name replaces it, route included.
    auto& manager = PreviewManager::instance();
    REQUIRE( manager.clearAll() );
    importUserPreviews( QString::fromUtf8( PackPreviews )
                            .replace( "%NAME%", "Pack frame" )
                            .replace( "^PACK", "^MINE" )
                            .toUtf8() );
    CHECK( !settings.snapshot()
                .configuration.value( "previewRoutes" )
                .toMap()
                .contains( "Pack frame" ) );
    REQUIRE( manager.clearAll() );

    settings.setPacks( {} );
    REQUIRE( settings.setConfiguration( original ) );
}

TEST_CASE( "The pack check refuses shared names and merged limits", "[previewdecoder][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto routes = []( int count, const QString& prefix ) {
        QJsonObject result;
        for ( int index = 0; index < count; ++index ) {
            result[ prefix + QString::number( index ) ] = QJsonObject{ { "dissector", "data" } };
        }
        return result;
    };
    writePack( root.path(), packManifest( "a.pack", 2, routes( 1, "shared" ) ) );
    PluginManager manager( root.path(), QDir( root.path() ).filePath( "none" ) );
    manager.discover();
    auto& registry = PreviewPackRegistry::instance();
    registry.attach( &manager );
    REQUIRE( manager.manage( "enable", "a.pack", {} ).ok() );
    REQUIRE( registry.contributions().size() == 1 );
    CHECK( findIn( PreviewManager::instance().effective(), "Pack frame" ) );
    CHECK( PreviewDecoderSettings::instance().packs().lua.size() == 1 );

    const auto candidate = [ &root ]( const QString& id, const QJsonObject& manifest,
                                      const QString& previewName ) {
        const auto folder = writePack( root.path(), manifest, previewName );
        return PreviewPackRegistry::instance().check( id, folder, manifest );
    };
    // The same preview name in two enabled packs.
    CHECK( candidate( "b.pack", packManifest( "b.pack", 1 ), "Pack frame" ).contains( "Pack a.pack" ) );
    // The same protocol route in two packs.
    auto other = withContent( packManifest( "c.pack", 1 ), "decoder_routes",
                              QJsonObject{ { "protocolRoutes", routes( 1, "shared" ) } } );
    other = withContent( other, "previews", QJsonValue::Undefined );
    CHECK( candidate( "c.pack", other, "C frame" ).contains( "shared0" ) );
    // More protocol routes than the decoder takes, together.
    auto many = withContent( packManifest( "d.pack", 1 ), "decoder_routes",
                             QJsonObject{ { "protocolRoutes", routes( 32, "many" ) } } );
    many = withContent( many, "previews", QJsonValue::Undefined );
    CHECK( candidate( "d.pack", many, "D frame" ).contains( "33" ) );
    // A pack that fits.
    auto fits = withContent( packManifest( "e.pack", 1 ), "decoder_routes", QJsonObject{} );
    CHECK( candidate( "e.pack", fits, "E frame" ).isEmpty() );

    // Two packs never share a route, also while a route of the user's hides both.
    {
        auto& decoder = PreviewDecoderSettings::instance();
        const auto saved = decoder.configuration();
        auto user = saved;
        user[ "protocolRoutes" ]
            = QVariantMap{ { "shared0", QVariantMap{ { "dissector", "data" } } } };
        REQUIRE( decoder.setConfiguration( user ) );
        CHECK( candidate( "c.pack", other, "C frame" ).contains( "shared0" ) );
        REQUIRE( decoder.setConfiguration( saved ) );
    }
    // The pack's block names `<pack-id>/` are reserved for it.
    REQUIRE( PreviewManager::instance().clearAll() );
    importUserPreviews( R"json({ "version": 1, "blocks": [ {
        "name": "e.pack/frame_header_block", "format": "match", "type": "string",
        "regex": "^(?<raw>.*)$", "source": "capture", "capture": "payload",
        "fields": [ { "name": "raw", "source": "capture", "capture": "raw", "format": "string" } ]
      } ], "previews": [ { "name": "Mine", "regex": "^MINE (?<payload>.*)$", "type": "string",
        "format": "fields", "bufferCapture": "payload",
        "fields": [ { "name": "raw", "source": "capture", "capture": "payload", "format": "string" } ] } ] })json" );
    CHECK( candidate( "e.pack", fits, "E frame" ).contains( "e.pack/frame_header_block" ) );
    REQUIRE( PreviewManager::instance().clearAll() );

    // GUI questions about pack Lua: nothing to ask without Lua, or for a pack that is not
    // enabled (its Lua runs only once it is enabled, which asks).
    CHECK( pluginsui::confirmPackLua( nullptr, &manager, "a.pack", "A", { "previews" }, true ) );
    CHECK( pluginsui::confirmPackLua( nullptr, &manager, "b.pack", "B", { "lua" }, false ) );
    CHECK( pluginsui::catalogContents( &manager, "unknown-entry" ).isEmpty() );
    const auto peeked = PluginRepository::packageManifest(
        QDir( root.path() ).filePath( "plugins/a.pack" ) );
    CHECK( peeked.value( "id" ) == "a.pack" );
    CHECK( packContents( peeked ).contains( "lua" ) );
    CHECK( PluginRepository::packageManifest( QDir( root.path() ).filePath( "missing" ) ).isEmpty() );

    // Copy to My Previews: the copy keeps decoding without the pack, so the pack's route becomes
    // the user's.
    auto& settings = PreviewDecoderSettings::instance();
    const auto original = settings.configuration();
    REQUIRE( PreviewManager::instance().clearAll() );
    CHECK( registry.copyPreview( "Pack frame" ).isEmpty() );
    CHECK( PreviewManager::instance().findByName( "Pack frame" ) );
    CHECK( settings.configuration()
               .value( "previewRoutes" )
               .toMap()
               .value( "Pack frame" )
               .toMap()
               .value( "dissector" )
           == "packproto" );
    REQUIRE( settings.setConfiguration( original ) );
    REQUIRE( PreviewManager::instance().clearAll() );

    // A copy that would take your previews and the enabled packs past the preview limits is
    // refused (the service would drop the packs' previews): 127 of yours and the pack's one.
    {
        QJsonArray mine;
        for ( int index = 0; index < 127; ++index ) {
            mine.append( QJsonObject{
                { "name", QStringLiteral( "Mine %1" ).arg( index ) },
                { "regex", "^MINE (?<payload>.*)$" },
                { "type", "string" },
                { "format", "fields" },
                { "bufferCapture", "payload" },
                { "fields", QJsonArray{ QJsonObject{ { "name", "raw" },
                                                     { "source", "capture" },
                                                     { "capture", "payload" },
                                                     { "format", "string" } } } } } );
        }
        importUserPreviews( QJsonDocument( QJsonObject{ { "version", 1 }, { "previews", mine } } ).toJson() );
        REQUIRE( PreviewManager::instance().all().size() == 127 );
        const auto refused = registry.copyPreview( "Pack frame", "Pack frame (mine)" );
        CHECK( refused.contains( "preview limits" ) );
        CHECK_FALSE( PreviewManager::instance().findByName( "Pack frame (mine)" ) );
        CHECK( PreviewManager::instance().all().size() == 127 );
        // Its route, saved before the copy, is taken back.
        CHECK( settings.configuration() == original );
        REQUIRE( settings.setConfiguration( original ) );
        REQUIRE( PreviewManager::instance().clearAll() );
    }

    // A copy whose decoder route cannot be added (the user's preview routes at their limit) is
    // not made at all, so it never lacks its route and its name stays free; Keep Both checks
    // the routes at Apply.
    {
        auto full = original;
        QVariantMap userRoutes;
        for ( int index = 0; index < PreviewDecoderSettings::PreviewRouteLimit; ++index ) {
            userRoutes.insert( QStringLiteral( "Route %1" ).arg( index ),
                               QVariantMap{ { "dissector", "data" } } );
        }
        full[ "previewRoutes" ] = userRoutes;
        REQUIRE( settings.setConfiguration( full ) );
        const auto saved = settings.configuration();
        const auto refused = registry.copyPreview( "Pack frame", "Pack frame (mine)" );
        CHECK( refused.contains( "was not copied" ) );
        CHECK( refused.contains( "128" ) );
        CHECK_FALSE( PreviewManager::instance().findByName( "Pack frame (mine)" ) );
        CHECK( settings.configuration() == saved );

        QString error;
        const auto snapshot = registry.snapshotOfPack( "a.pack", &error );
        REQUIRE( snapshot );
        CHECK( registry.copyPreviewOfSnapshot( *snapshot, "Pack frame", "Pack frame (mine)" )
                   .contains( "was not copied" ) );
        CHECK_FALSE( PreviewManager::instance().findByName( "Pack frame (mine)" ) );
        CHECK( registry.checkCopyRoutes( *snapshot, { { "Pack frame", "Pack frame (mine)" } } )
                   .contains( "128" ) );
        // Another route of the user's under the copy's name (left from a deleted preview): the
        // copy would decode with it, not as the preview does, so it is not made either.
        const auto routeOf = [ &settings ]( const QString& name ) {
            return settings.configuration()
                .value( "previewRoutes" )
                .toMap()
                .value( name )
                .toMap()
                .value( "dissector" )
                .toString();
        };
        full[ "previewRoutes" ]
            = QVariantMap{ { "Pack frame (mine)", QVariantMap{ { "dissector", "stale" } } } };
        REQUIRE( settings.setConfiguration( full ) );
        CHECK( registry.checkCopyRoutes( *snapshot, { { "Pack frame", "Pack frame (mine)" } } )
                   .contains( "another decoder route named \"Pack frame (mine)\"" ) );
        CHECK( registry.copyPreviewOfSnapshot( *snapshot, "Pack frame", "Pack frame (mine)" )
                   .contains( "was not copied" ) );
        CHECK_FALSE( PreviewManager::instance().findByName( "Pack frame (mine)" ) );
        CHECK( routeOf( "Pack frame (mine)" ) == "stale" );
        // The same route as the preview's under that name: nothing to add, the copy is made.
        full[ "previewRoutes" ]
            = QVariantMap{ { "Pack frame (mine)", QVariantMap{ { "dissector", "packproto" } } } };
        REQUIRE( settings.setConfiguration( full ) );
        CHECK( registry.checkCopyRoutes( *snapshot, { { "Pack frame", "Pack frame (mine)" } } )
                   .isEmpty() );
        CHECK( registry.copyPreviewOfSnapshot( *snapshot, "Pack frame", "Pack frame (mine)" )
                   .isEmpty() );
        CHECK( PreviewManager::instance().findByName( "Pack frame (mine)" ) );
        REQUIRE( PreviewManager::instance().clearAll() );
        // The preview decodes with the user's own route for it (it wins over the pack's): the copy
        // takes that one.
        full[ "previewRoutes" ]
            = QVariantMap{ { "Pack frame", QVariantMap{ { "dissector", "users" } } } };
        REQUIRE( settings.setConfiguration( full ) );
        CHECK( registry.copyPreviewOfSnapshot( *snapshot, "Pack frame", "Pack frame (mine)" )
                   .isEmpty() );
        CHECK( routeOf( "Pack frame (mine)" ) == "users" );
        REQUIRE( settings.setConfiguration( original ) );
        REQUIRE( PreviewManager::instance().clearAll() );
    }

    // A copy under another name never takes the name of a preview of a pack that is installed
    // but not enabled: enabled later, that pack would lose it to the copy.
    // (No decoder routes: the default ones name "Pack frame", which this pack does not have.)
    writePack( root.path(), withContent( packManifest( "z.pack", 0 ), "decoder_routes", QJsonObject{} ),
               "Pack frame (mine)" );
    manager.discover();
    REQUIRE( registry.installedPackPreviewNames().contains( "Pack frame (mine)" ) );
    CHECK( registry.copyPreview( "Pack frame", "Pack frame (mine)" )
               .contains( "A pack has a preview named \"Pack frame (mine)\"" ) );
    CHECK_FALSE( PreviewManager::instance().findByName( "Pack frame (mine)" ) );
    // Also while that pack's previews do not load (a regex that is not valid): its names still
    // count, as it can be fixed or reset later.
    {
        const auto previewsFile = root.filePath( "plugins/z.pack/previews.json" );
        auto text = QString::fromUtf8( packPreviews( "Pack frame (mine)" ) );
        writeFile( previewsFile, text.replace( "\"^PACK (?<payload>.*)$\"", "\"(\"" ).toUtf8() );
        REQUIRE( !PreviewPackRegistry::read( "z.pack", root.filePath( "plugins/z.pack" ),
                                             manager.manifest( "z.pack" ) )
                      .error.isEmpty() );
        CHECK( registry.installedPackPreviewNames().contains( "Pack frame (mine)" ) );
    }
    REQUIRE( settings.setConfiguration( original ) );
    REQUIRE( PreviewManager::instance().clearAll() );

    REQUIRE( manager.manage( "disable", "a.pack", {} ).ok() );
    CHECK( registry.contributions().isEmpty() );
    CHECK( !findIn( PreviewManager::instance().effective(), "Pack frame" ) );
    CHECK( PreviewDecoderSettings::instance().packs().lua.isEmpty() );
    registry.attach( nullptr );
}

namespace {
// A predefined filters file as CILogg exports it, with regex filters (id, name, pattern).
QByteArray filtersFile( const QVector<std::tuple<QString, QString, QString>>& rows )
{
    QByteArray text = "[PredefinedFiltersCollection]\nversion=3\nfilters\\size="
                      + QByteArray::number( rows.size() ) + "\n";
    int index = 1;
    for ( const auto& [ id, name, pattern ] : rows ) {
        const auto prefix = "filters\\" + QByteArray::number( index++ ) + "\\";
        text += prefix + "id=" + id.toUtf8() + "\n" + prefix + "name=" + name.toUtf8() + "\n"
                + prefix + "filter=" + pattern.toUtf8() + "\n" + prefix + "regex=true\n";
    }
    return text;
}

// An installed pack with predefined filters only (E16c).
QString writeFilterPack( const QString& root, const QString& id, const QByteArray& filters )
{
    auto manifest = packManifest( id, 0 );
    for ( const auto* key : { "previews", "decoder_routes", "python_decoders" } ) {
        manifest = withContent( manifest, QLatin1String( key ), QJsonValue::Undefined );
    }
    manifest = withContent( manifest, "filters", "filters.conf" );
    const auto folder = QDir( root ).filePath( "plugins/" + id );
    writeFile( folder + "/plugin.json", QJsonDocument( manifest ).toJson() );
    writeFile( folder + "/filters.conf", filters );
    return folder;
}
} // namespace

TEST_CASE( "Pack filters join the predefined filters with the pack's name", "[plugins][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    auto& collection = PredefinedFiltersCollection::getSynced();
    const auto saved = collection.getFilters();
    collection.saveToStorage(
        { { "mine", "Mine", "mine", false }, { "f-two", "My reset", "my reset", true } } );
    writeFilterPack(
        root.path(), "a.pack",
        filtersFile( { { "f-one", "Errors", "ERROR" }, { "f-two", "Reset", "reset" } } ) );
    PluginManager manager( root.path(), QDir( root.path() ).filePath( "none" ) );
    manager.discover();
    auto& registry = PreviewPackRegistry::instance();
    registry.attach( &manager );
    QSignalSpy changed( &PackFilters::instance(), &PackFilters::changed );
    REQUIRE( manager.manage( "enable", "a.pack", {} ).ok() );
    CHECK( changed.count() >= 1 );
    REQUIRE( registry.contributions().size() == 1 );
    CHECK( registry.contributions().first().filters.size() == 2 );

    // Yours first, then the pack's; your filter with a pack filter's id replaces it.
    const auto effective = effectivePredefinedFilters( collection.getFilters() );
    REQUIRE( effective.size() == 3 );
    CHECK( effective.at( 0 ).filter.id == "mine" );
    CHECK( effective.at( 1 ).filter.name == "My reset" );
    CHECK( effective.at( 1 ).packId.isEmpty() );
    CHECK( effective.at( 2 ).filter.id == "f-one" );
    CHECK( effective.at( 2 ).packId == "a.pack" );
    CHECK( effective.at( 2 ).packName == "Pack a.pack" );
    // Your filters, saved and exported, never hold the pack's.
    CHECK( PredefinedFiltersCollection::getSynced().getFilters().size() == 2 );

    // The filter list offers them with the pack's name; Commander finds them by id and index.
    {
        PredefinedFiltersComboBox combo( nullptr );
        REQUIRE( combo.filters().size() == 3 );
        const auto listed = combo.commanderFilters();
        REQUIRE( listed.size() == 3 );
        CHECK( listed.at( 2 ).toMap().value( "packId" ) == "a.pack" );
        CHECK( listed.at( 2 ).toMap().value( "packName" ) == "Pack a.pack" );
        CHECK( !listed.at( 0 ).toMap().contains( "packId" ) );
        CHECK( combo.commanderFilterById( "f-one" ).has_value() );
        CHECK( combo.commanderFilterByIndex( 2 )->id == "f-one" );
        auto* model = qobject_cast<QStandardItemModel*>( combo.model() );
        REQUIRE( model );
        REQUIRE( model->rowCount() == 4 ); // the title, then the filters
        CHECK( model->item( 3 )->text() == "Errors (Pack a.pack)" );
        CHECK( model->item( 1 )->text() == "Mine" );
        // A list reloaded (a pack enabled, edited or updated) while the search uses its filters
        // shows them ticked again when it opens (Codex review of #165).
        combo.updateSearchPattern( "ERROR", false );
        combo.showPopup();
        CHECK( model->item( 3 )->checkState() == Qt::Checked );
        combo.hidePopup();
        combo.populatePredefinedFilters();
        CHECK( model->item( 3 )->checkState() == Qt::Unchecked );
        combo.showPopup();
        CHECK( model->item( 3 )->checkState() == Qt::Checked );
        combo.hidePopup();
    }

    // Two enabled packs never share a filter id, and your filters and the packs' together stay
    // within 4,096.
    const auto other
        = writeFilterPack( root.path(), "b.pack", filtersFile( { { "f-one", "Other", "E" } } ) );
    CHECK(
        registry.check( "b.pack", other, Manifest::read( other ).json )
            .contains(
                "\"Pack b.pack\" and \"Pack a.pack\" both have a filter with the id \"f-one\"" ) );
    {
        // Your 2 filters, a.pack's listed 1 (your f-two replaces its f-two) and c.pack's 4,094.
        QVector<std::tuple<QString, QString, QString>> many;
        for ( int index = 0; index < 4094; ++index ) {
            many.append( { QStringLiteral( "m%1" ).arg( index ), "M", "m" } );
        }
        const auto large = writeFilterPack( root.path(), "c.pack", filtersFile( many ) );
        CHECK( registry.check( "c.pack", large, Manifest::read( large ).json )
                   .contains( "would make 4097 predefined filters (at most 4096" ) );
        many.removeLast();
        const auto fits = writeFilterPack( root.path(), "c.pack", filtersFile( many ) );
        CHECK( registry.check( "c.pack", fits, Manifest::read( fits ).json ).isEmpty() );
        // Without your f-two the pack's counts: 4,097 again (independent review of #169).
        collection.saveToStorage(
            { { "mine", "Mine", "mine", false }, { "other", "Other", "other", true } } );
        CHECK( registry.check( "c.pack", fits, Manifest::read( fits ).json )
                   .contains( "would make 4097 predefined filters" ) );
        collection.saveToStorage(
            { { "mine", "Mine", "mine", false }, { "f-two", "My reset", "my reset", true } } );
    }

    // The Filters dialog: your filters, then the pack's with its name, read only and never saved.
    {
        PredefinedFiltersDialog dialog;
        auto* table = dialog.findChild<QTableWidget*>( "filtersTableWidget" );
        REQUIRE( table );
        REQUIRE( table->rowCount() == 4 );
        CHECK( table->item( 2, 0 )->text() == "Errors" );
        CHECK( table->item( 2, 3 )->text() == "Pack a.pack" );
        CHECK( table->item( 3, 3 )->text() == "Pack a.pack (replaced by your filter)" );
        CHECK( !( table->item( 2, 0 )->flags() & Qt::ItemIsEditable ) );
        CHECK( !( table->item( 2, 1 )->flags() & Qt::ItemIsEditable ) );
        auto* copy = dialog.findChild<QToolButton*>( "copyPackFilterButton" );
        auto* edit = dialog.findChild<QToolButton*>( "editPackFilterButton" );
        auto* packs = dialog.findChild<QToolButton*>( "packsButton" );
        auto* remove = dialog.findChild<QToolButton*>( "removeFilterButton" );
        REQUIRE( ( copy && edit && packs && remove ) );
        // Edit... and Packs... appear once the main window gives them something to open.
        CHECK( edit->isHidden() );
        CHECK( packs->isHidden() );
        QString editedPack;
        QString editedPath;
        dialog.setPackItemEditor( [ & ]( QWidget*, const QString& pack, const QString& path ) {
            editedPack = pack;
            editedPath = path;
        } );
        int packsOpened = 0;
        dialog.setPacksOpener( [ &packsOpened ] { ++packsOpened; } );
        CHECK( !edit->isHidden() );
        CHECK( !packs->isHidden() );
        packs->click();
        CHECK( packsOpened == 1 );

        table->setCurrentCell( 2, 0 );
        CHECK( copy->isEnabled() );
        CHECK( edit->isEnabled() );
        CHECK( !remove->isEnabled() );
        edit->click();
        CHECK( editedPack == "a.pack" );
        CHECK( editedPath == "filters/f-one.json" );
        // A pack filter your filter replaces is neither copied nor edited.
        table->setCurrentCell( 3, 0 );
        CHECK( !copy->isEnabled() );
        CHECK( !edit->isEnabled() );
        table->setCurrentCell( 0, 0 );
        CHECK( remove->isEnabled() );
        CHECK( !copy->isEnabled() );

        // Copy to My Filters: your filter with the pack filter's id, which replaces it.
        table->setCurrentCell( 2, 0 );
        copy->click();
        REQUIRE( table->rowCount() == 5 );
        CHECK( table->item( 2, 0 )->text() == "Errors" );
        CHECK( table->item( 2, 3 )->text().isEmpty() );
        CHECK( table->item( 3, 3 )->text() == "Pack a.pack (replaced by your filter)" );
        dialog.findChild<QDialogButtonBox*>( "buttonBox" )
            ->button( QDialogButtonBox::Apply )
            ->click();
        const auto mine = PredefinedFiltersCollection::getSynced().getFilters();
        REQUIRE( mine.size() == 3 );
        CHECK( mine.at( 2 ).id == "f-one" );
        CHECK( mine.at( 2 ).pattern == "ERROR" );
        CHECK( mine.at( 2 ).useRegex );
    }

    // Your own filters are not limited: grown past 4,096 with the packs' (enabling was checked),
    // the packs' filters give way in the lists, never yours (Codex review of #165).
    {
        const auto before = collection.getFilters();
        QList<PredefinedFilter> many;
        for ( int index = 0; index < MaxListedFilters - 1; ++index ) {
            many.append( { QStringLiteral( "u%1" ).arg( index ), "U", "u", false } );
        }
        collection.saveToStorage( many ); // 4,095 of yours and the pack's 2: 4,097
        CHECK( packFiltersGiveWay( many ) );
        CHECK( effectivePredefinedFilters( many ).size() == MaxListedFilters - 1 );
        PredefinedFiltersComboBox combo( nullptr );
        CHECK( combo.filters().size() == MaxListedFilters - 1 );
        PredefinedFiltersDialog dialog;
        auto* table = dialog.findChild<QTableWidget*>( "filtersTableWidget" );
        REQUIRE( table->rowCount() == MaxListedFilters + 1 );
        CHECK( table->item( MaxListedFilters - 1, 3 )->text()
               == "Pack a.pack (not listed: more than 4096 filters)" );
        many.removeLast(); // 4,094 and 2: they fit again
        CHECK( !packFiltersGiveWay( many ) );
        CHECK( effectivePredefinedFilters( many ).size() == MaxListedFilters );
        collection.saveToStorage( before );
    }

    // Disabled, the pack's filters are gone from the list.
    changed.clear();
    REQUIRE( manager.manage( "disable", "a.pack", {} ).ok() );
    CHECK( changed.count() >= 1 );
    CHECK( PackFilters::instance().filters().isEmpty() );
    CHECK(
        effectivePredefinedFilters( PredefinedFiltersCollection::getSynced().getFilters() ).size()
        == 3 );
    registry.attach( nullptr );
    collection.saveToStorage( saved );
}

namespace {
// A highlighters file as CILogg exports it, one regex rule per set (id, name, pattern).
QByteArray highlightersFile( const QVector<std::tuple<QString, QString, QString>>& sets )
{
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto path = folder.filePath( "highlights.conf" );
    {
        QSettings settings( path, QSettings::IniFormat );
        settings.beginGroup( "HighlighterSetCollection" );
        settings.setValue( "version", 2 );
        settings.beginWriteArray( "sets" );
        int index = 0;
        for ( const auto& [ id, name, pattern ] : sets ) {
            settings.setArrayIndex( index++ );
            settings.beginGroup( "HighlighterSet" );
            settings.setValue( "version", 3 );
            settings.setValue( "id", id );
            settings.setValue( "name", name );
            settings.beginWriteArray( "highlighters" );
            settings.setArrayIndex( 0 );
            settings.setValue( "regexp", pattern );
            settings.setValue( "ignore_case", false );
            settings.setValue( "match_only", false );
            settings.setValue( "use_regex", true );
            settings.setValue( "variate_colors", false );
            settings.setValue( "color_variance", 15 );
            settings.setValue( "fore_colour", "#ff000000" );
            settings.setValue( "back_colour", "#ffffff00" );
            settings.endArray();
            settings.endGroup();
        }
        settings.endArray();
        settings.endGroup();
        settings.sync();
        REQUIRE( settings.status() == QSettings::NoError );
    }
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
}

// An installed pack with highlighter sets only (E16c2).
QString writeHighlightPack( const QString& root, const QString& id, const QByteArray& highlights )
{
    auto manifest = packManifest( id, 0 );
    for ( const auto* key : { "previews", "decoder_routes", "python_decoders" } ) {
        manifest = withContent( manifest, QLatin1String( key ), QJsonValue::Undefined );
    }
    manifest = withContent( manifest, "highlights", "highlights.conf" );
    const auto folder = QDir( root ).filePath( "plugins/" + id );
    writeFile( folder + "/plugin.json", QJsonDocument( manifest ).toJson() );
    writeFile( folder + "/highlights.conf", highlights );
    return folder;
}

// Saves these as the user's highlighter sets, none active.
void saveUserSets( const QList<HighlighterSet>& sets )
{
    auto& collection = HighlighterSetCollection::getSynced();
    collection.setHighlighterSets( sets );
    collection.deactivateAll();
    collection.save();
}

// The user's sets with these ids and names, read as CILogg reads its own.
QList<HighlighterSet> userSets( const QVector<std::tuple<QString, QString, QString>>& sets )
{
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto path = folder.filePath( "mine.conf" );
    writeFile( path, highlightersFile( sets ) );
    QSettings settings( path, QSettings::IniFormat );
    HighlighterSetCollection collection;
    collection.retrieveFromStorage( settings );
    return collection.highlighterSets();
}

// A widget of the Highlighters dialog itself, not of the set editor in it (they share names).
template <typename T>
T* dialogWidget( QWidget& dialog, const QString& name )
{
    for ( auto* widget : dialog.findChildren<T*>( name ) ) {
        bool inEditor = false;
        for ( auto* parent = widget->parentWidget(); parent && parent != &dialog;
              parent = parent->parentWidget() ) {
            inEditor = inEditor || parent->inherits( "HighlighterSetEdit" );
        }
        if ( !inEditor ) {
            return widget;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE( "Pack highlighter sets join the Highlighters menu and dialog with the pack's name",
           "[plugins][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const HighlighterSetCollection saved = HighlighterSetCollection::getSynced();
    saveUserSets( userSets( { { "mine", "Mine", "mine" }, { "h-two", "My reset", "my reset" } } ) );
    writeHighlightPack(
        root.path(), "a.pack",
        highlightersFile( { { "h-one", "Errors", "ERROR" }, { "h-two", "Reset", "reset" } } ) );
    PluginManager manager( root.path(), QDir( root.path() ).filePath( "none" ) );
    manager.discover();
    auto& registry = PreviewPackRegistry::instance();
    registry.attach( &manager );
    QSignalSpy changed( &PackHighlighters::instance(), &PackHighlighters::changed );
    REQUIRE( manager.manage( "enable", "a.pack", {} ).ok() );
    CHECK( changed.count() >= 1 );
    REQUIRE( registry.contributions().size() == 1 );
    CHECK( registry.contributions().first().highlighterSets.size() == 2 );

    // Yours first, then the pack's; your set with a pack set's id replaces it.
    auto& collection = HighlighterSetCollection::get();
    {
        const auto listed = collection.listedSets();
        REQUIRE( listed.size() == 3 );
        CHECK( listed.at( 0 ).set.id() == "mine" );
        CHECK( listed.at( 1 ).set.name() == "My reset" );
        CHECK( listed.at( 1 ).packId.isEmpty() );
        CHECK( listed.at( 2 ).set.id() == "h-one" );
        CHECK( listed.at( 2 ).packId == "a.pack" );
        CHECK( listed.at( 2 ).packName == "Pack a.pack" );
        // Your sets, saved and exported, never hold the pack's.
        CHECK( collection.highlighterSets().size() == 2 );
    }

    // A pack set is activated like yours and highlights with its rules; which sets are active
    // stays yours, saved with your sets.
    collection.activateSet( "h-one" );
    collection.save();
    CHECK( collection.activeSetIds() == QStringList{ "h-one" } );
    REQUIRE( collection.currentActiveSet().highlighters().size() == 1 );
    CHECK( collection.currentActiveSet().highlighters().at( 0 ).pattern() == "ERROR" );
    CHECK( HighlighterSetCollection::getSynced().highlighterSets().size() == 2 );
    CHECK( HighlighterSetCollection::get().activeSetIds() == QStringList{ "h-one" } );

    // The Highlighters menu offers it with the pack's name, ticked.
    {
        HighlightersMenu menu( "Highlighters", nullptr );
        menu.createHighlightersMenu();
        menu.populateHighlightersMenu();
        QStringList texts;
        const QAction* packAction = nullptr;
        for ( const auto* action : menu.actions() ) {
            if ( !action->isSeparator() ) {
                texts.append( action->text() );
            }
            if ( action->data().toString() == "h-one" ) {
                packAction = action;
            }
        }
        CHECK( texts == QStringList{ "None", "Mine", "My reset", "Errors (Pack a.pack)" } );
        REQUIRE( packAction );
        CHECK( packAction->isChecked() );
    }

    // Two enabled packs never share a set id.
    {
        const auto other = writeHighlightPack( root.path(), "b.pack",
                                               highlightersFile( { { "h-one", "Other", "E" } } ) );
        CHECK( registry.check( "b.pack", other, Manifest::read( other ).json )
                   .contains( "\"Pack b.pack\" and \"Pack a.pack\" both have a highlighter set "
                              "with the id \"h-one\"" ) );
    }

    // The Highlighters dialog: your sets, then the pack's with its name, read only and never saved.
    {
        HighlightersDialog dialog;
        auto* list = dialogWidget<QListWidget>( dialog, "highlighterListWidget" );
        auto* remove = dialogWidget<QToolButton>( dialog, "removeHighlighterButton" );
        auto* copy = dialog.findChild<QToolButton*>( "copyPackSetButton" );
        auto* edit = dialog.findChild<QToolButton*>( "editPackSetButton" );
        auto* packs = dialog.findChild<QToolButton*>( "packsButton" );
        auto* nameEdit = dialog.findChild<QLineEdit*>( "nameEdit" );
        REQUIRE( ( list && remove && copy && edit && packs && nameEdit ) );
        REQUIRE( list->count() == 4 );
        CHECK( list->item( 1 )->text() == "My reset" );
        CHECK( list->item( 2 )->text() == "Errors (Pack a.pack)" );
        CHECK( list->item( 3 )->text() == "Reset (Pack a.pack, replaced by your set)" );
        // Edit... and Packs... appear once the main window gives them something to open.
        CHECK( edit->isHidden() );
        CHECK( packs->isHidden() );
        QString editedPack;
        QString editedPath;
        dialog.setPackItemEditor( [ & ]( QWidget*, const QString& pack, const QString& path ) {
            editedPack = pack;
            editedPath = path;
        } );
        int packsOpened = 0;
        dialog.setPacksOpener( [ &packsOpened ] { ++packsOpened; } );
        CHECK( !edit->isHidden() );
        CHECK( !packs->isHidden() );
        packs->click();
        CHECK( packsOpened == 1 );

        // A pack set is shown, not edited here: Edit... opens its store item.
        list->setCurrentRow( 2 );
        CHECK( nameEdit->text() == "Errors" );
        CHECK( !nameEdit->isEnabled() );
        CHECK( copy->isEnabled() );
        CHECK( edit->isEnabled() );
        CHECK( !remove->isEnabled() );
        edit->click();
        CHECK( editedPack == "a.pack" );
        CHECK( editedPath == "highlights/h-one.set" );
        // A pack set your set replaces is neither copied nor edited.
        list->setCurrentRow( 3 );
        CHECK( !copy->isEnabled() );
        CHECK( !edit->isEnabled() );
        list->setCurrentRow( 0 );
        CHECK( nameEdit->isEnabled() );
        CHECK( remove->isEnabled() );
        CHECK( !copy->isEnabled() );

        // Copy to My Highlighters: your set with the pack set's id, which replaces it and stays
        // active.
        list->setCurrentRow( 2 );
        copy->click();
        REQUIRE( list->count() == 5 );
        CHECK( list->item( 2 )->text() == "Errors" );
        CHECK( list->item( 3 )->text() == "Errors (Pack a.pack, replaced by your set)" );
        dialog.findChild<QDialogButtonBox*>( "buttonBox" )
            ->button( QDialogButtonBox::Apply )
            ->click();
        const auto mine = HighlighterSetCollection::getSynced().highlighterSets();
        REQUIRE( mine.size() == 3 );
        CHECK( mine.at( 2 ).id() == "h-one" );
        REQUIRE( mine.at( 2 ).highlighters().size() == 1 );
        CHECK( mine.at( 2 ).highlighters().at( 0 ).pattern() == "ERROR" );
        CHECK( HighlighterSetCollection::get().activeSetIds() == QStringList{ "h-one" } );
        CHECK( HighlighterSetCollection::get().currentActiveSet().highlighters().size() == 1 );
    }

    // Removing that copy lists the pack's set again, still active: the copy kept its id
    // (independent review of #169).
    {
        HighlightersDialog dialog;
        auto* list = dialogWidget<QListWidget>( dialog, "highlighterListWidget" );
        auto* remove = dialogWidget<QToolButton>( dialog, "removeHighlighterButton" );
        REQUIRE( ( list && remove ) );
        REQUIRE( list->count() == 5 );
        REQUIRE( list->item( 2 )->text() == "Errors" );
        list->setCurrentRow( 2 );
        REQUIRE( remove->isEnabled() );
        remove->click();
        // The removal is queued (the dialog's own pattern for row changes).
        for ( int round = 0; round < 10 && list->count() != 4; ++round ) {
            QCoreApplication::processEvents();
        }
        REQUIRE( list->count() == 4 );
        CHECK( list->item( 2 )->text() == "Errors (Pack a.pack)" );
        dialog.findChild<QDialogButtonBox*>( "buttonBox" )
            ->button( QDialogButtonBox::Apply )
            ->click();
        CHECK( HighlighterSetCollection::getSynced().highlighterSets().size() == 2 );
        CHECK( HighlighterSetCollection::get().activeSetIds() == QStringList{ "h-one" } );
        CHECK( HighlighterSetCollection::get().currentActiveSet().highlighters().size() == 1 );
    }

    // Your own sets are not limited: grown past 4,096 with the packs' (enabling is checked), the
    // packs' sets give way in the menu and the highlighting, never yours.
    {
        QList<HighlighterSet> many;
        for ( int index = 0; index < MaxListedHighlighterSets - 2; ++index ) {
            many.append( HighlighterSet::createNewSet( QStringLiteral( "U%1" ).arg( index ) ) );
        }
        saveUserSets( many ); // 4,094 of yours and the pack's 2: 4,096
        CHECK( HighlighterSetCollection::get().listedSets().size() == MaxListedHighlighterSets );
        const auto third = writeHighlightPack( root.path(), "c.pack",
                                               highlightersFile( { { "h-three", "T", "t" } } ) );
        CHECK( registry.check( "c.pack", third, Manifest::read( third ).json )
                   .contains( "would make 4097 highlighter sets (at most 4096" ) );
        many.append( HighlighterSet::createNewSet( "U-last" ) );
        saveUserSets( many ); // 4,095 and 2: the pack's give way
        CHECK( HighlighterSetCollection::get().listedSets().size()
               == MaxListedHighlighterSets - 1 );
        {
            HighlightersDialog dialog;
            auto* list = dialogWidget<QListWidget>( dialog, "highlighterListWidget" );
            REQUIRE( list );
            REQUIRE( list->count() == MaxListedHighlighterSets + 1 );
            CHECK( list->item( MaxListedHighlighterSets - 1 )->text()
                   == "Errors (Pack a.pack, not listed: more than 4096 sets)" );
        }
        many.erase( many.end() - 3, many.end() ); // 4,092, the pack's 2 and c.pack's 1: they fit
        saveUserSets( many );
        CHECK( registry.check( "c.pack", third, Manifest::read( third ).json ).isEmpty() );
        // A set of yours that replaces a pack set (a copy keeps its id) is counted once: 4,093 and
        // your copies of a.pack's two sets, a.pack's none listed and c.pack's 1 make 4,096
        // (independent review of #169).
        many.append( HighlighterSet::createNewSet( "U-again" ) );
        saveUserSets(
            many + userSets( { { "h-one", "Errors", "ERROR" }, { "h-two", "Reset", "reset" } } ) );
        CHECK( HighlighterSetCollection::get().highlighterSets().size()
               == MaxListedHighlighterSets - 1 );
        CHECK( registry.check( "c.pack", third, Manifest::read( third ).json ).isEmpty() );
    }

    // Disabled, the pack's sets are gone; an active one stays in your active sets and highlights
    // again once the pack is enabled.
    saveUserSets( {} );
    HighlighterSetCollection::get().activateSet( "h-one" );
    HighlighterSetCollection::get().save();
    changed.clear();
    REQUIRE( manager.manage( "disable", "a.pack", {} ).ok() );
    CHECK( changed.count() >= 1 );
    CHECK( PackHighlighters::instance().sets().isEmpty() );
    CHECK( HighlighterSetCollection::get().listedSets().isEmpty() );
    CHECK( HighlighterSetCollection::get().currentActiveSet().highlighters().isEmpty() );
    CHECK( HighlighterSetCollection::get().activeSetIds() == QStringList{ "h-one" } );
    REQUIRE( manager.manage( "enable", "a.pack", {} ).ok() );
    CHECK( HighlighterSetCollection::get().currentActiveSet().highlighters().size() == 1 );
    REQUIRE( manager.manage( "disable", "a.pack", {} ).ok() );
    registry.attach( nullptr );
    HighlighterSetCollection::get() = saved;
    HighlighterSetCollection::get().refreshPackSets();
    HighlighterSetCollection::get().save();
}

TEST_CASE( "A pack whose filters or highlighters file is not an export keeps its other content",
           "[plugins][E16]" )
{
    // A pack enabled before E16c could carry a filters or highlighters file that is not exactly
    // what Export writes (an older export without ids): only that file's items are left out, not
    // the pack's previews, Lua, decoders or other items (independent review of #169).
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    HighlighterSetCollection::getSynced();
    const auto id = QStringLiteral( "d.pack" );
    auto manifest = packManifest( id, 0 );
    for ( const auto* key : { "decoder_routes", "python_decoders" } ) {
        manifest = withContent( manifest, QLatin1String( key ), QJsonValue::Undefined );
    }
    manifest = withContent( manifest, "filters", "filters.conf" );
    manifest = withContent( manifest, "highlights", "highlights.conf" );
    const auto folder = QDir( root.path() ).filePath( "plugins/" + id );
    writeFile( folder + "/plugin.json", QJsonDocument( manifest ).toJson() );
    writeFile( folder + "/previews.json", packPreviews() );
    writeFile( folder + "/filters.conf", filtersFile( { { "f-d", "D", "d" } } ) );
    writeFile( folder + "/highlights.conf", highlightersFile( { { "h-d", "D", "d" } } ) );
    PluginManager manager( root.path(), QDir( root.path() ).filePath( "none" ) );
    manager.discover();
    auto& registry = PreviewPackRegistry::instance();
    registry.attach( &manager );
    REQUIRE( manager.manage( "enable", id, {} ).ok() );
    REQUIRE( PackFilters::instance().filters().size() == 1 );
    REQUIRE( PackHighlighters::instance().sets().size() == 1 );

    // The highlighters file is one CILogg does not read as an export (a set without an id).
    const auto noIds = highlightersFile( { { QString{}, "Old", "old" } } );
    writeFile( folder + "/highlights.conf", noIds );
    {
        // The pack's folder changed (as after an update of CILogg): it is read again.
        QFile file( folder + "/plugin.json" );
        REQUIRE( file.open( QIODevice::ReadWrite ) );
        REQUIRE( file.setFileTime( QDateTime::currentDateTime().addSecs( 60 ),
                                   QFileDevice::FileModificationTime ) );
    }
    const auto read = PreviewPackRegistry::read( id, folder, manifest );
    CHECK( read.error.isEmpty() );
    CHECK( read.previews.previews.size() == 1 );
    CHECK( read.filters.size() == 1 );
    CHECK( read.highlighterSets.isEmpty() );
    REQUIRE( read.warnings.size() == 1 );
    CHECK( read.warnings.first().contains( "highlighters" ) );
    registry.refresh();
    REQUIRE( registry.contributions().size() == 1 );
    CHECK( PackFilters::instance().filters().size() == 1 );
    CHECK( PackHighlighters::instance().sets().isEmpty() );
    CHECK( findIn( PreviewManager::instance().effective(), "Pack frame" ) != nullptr );
    // get_plugins says why the pack has no highlighter sets.
    const auto summary = packSummary( manifest, folder );
    CHECK( summary.value( "highlighterSets" ).toInt() == 0 );
    CHECK( summary.value( "filters" ).toInt() == 1 );
    REQUIRE( summary.value( "contentWarnings" ).toStringList().size() == 1 );
    CHECK( summary.value( "contentWarnings" ).toStringList().first().contains( "highlighters" ) );
    // Enabling it again (or installing an update like it) is refused with the reason.
    REQUIRE( manager.manage( "disable", id, {} ).ok() );
    const auto refused = manager.manage( "enable", id, {} );
    CHECK( !refused.ok() );
    CHECK( refused.error.contains( "highlighters" ) );
    registry.attach( nullptr );
}

namespace {
// An installed pack with actions and automatic responses only (E16c3).
QString writeActionPack( const QString& root, const QString& id, const QByteArray& actions )
{
    auto manifest = packManifest( id, 0 );
    for ( const auto* key : { "previews", "decoder_routes", "python_decoders" } ) {
        manifest = withContent( manifest, QLatin1String( key ), QJsonValue::Undefined );
    }
    manifest = withContent( manifest, "actions", "actions.json" );
    const auto folder = QDir( root ).filePath( "plugins/" + id );
    writeFile( folder + "/plugin.json", QJsonDocument( manifest ).toJson() );
    writeFile( folder + "/actions.json", actions );
    return folder;
}

// Two actions and a response that runs both, as Export writes them (trimmed).
constexpr auto PackActionsJson = R"json({ "version": 4,
  "actions": [ { "id": 3, "name": "Reset", "sequence": { "type": "string", "value": "reset\n" } },
               { "id": 7, "name": "Status", "sequence": { "type": "string", "value": "status\n" } } ],
  "responses": [ { "id": 3, "name": "After boot", "match": { "type": "string", "value": "boot done" },
                   "response": { "steps": [ { "action_id": 7, "delay_ms": 0 },
                                            { "action_id": 3, "delay_ms": 0 } ] } } ] })json";

ActionDefinition userAction( int id, const QString& name = QStringLiteral( "Mine" ) )
{
    ActionDefinition action;
    action.id = id;
    action.name = name;
    action.sequence.value = QStringLiteral( "mine" );
    return action;
}

ResponseDefinition userResponse( int id, int runs )
{
    ResponseDefinition response;
    response.id = id;
    response.name = QStringLiteral( "Mine %1" ).arg( id );
    response.match.value = QStringLiteral( "mine" );
    response.response.hasActionId = true;
    response.response.actionId = runs;
    response.response.steps = { { runs, 0 } };
    return response;
}

// Keeps the user's actions and responses, and the packs' run-time ids and states in the settings,
// as they were.
class ScopedPackActions {
public:
    ScopedPackActions()
        : actions_( ActionsManager::instance().actions() )
        , responses_( ActionsManager::instance().responses() )
    {
        PersistentInfo::getSettings( app_settings{} ).remove( "PackActions" );
    }
    ~ScopedPackActions()
    {
        ActionsManager::instance().setPackItems( {} );
        CHECK( ActionsManager::instance().importFromDefinitions( actions_, responses_ ).ok );
        PersistentInfo::getSettings( app_settings{} ).remove( "PackActions" );
    }
    ScopedPackActions( const ScopedPackActions& ) = delete;
    ScopedPackActions& operator=( const ScopedPackActions& ) = delete;

private:
    QVector<ActionDefinition> actions_;
    QVector<ResponseDefinition> responses_;
};
} // namespace

TEST_CASE( "Pack actions and responses join the user's with run-time ids, responses off",
           "[plugins][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    ScopedPackActions restore;
    auto& actions = ActionsManager::instance();
    REQUIRE( actions.importFromDefinitions( { userAction( 1 ) }, { userResponse( 1, 1 ) } ).ok );
    writeActionPack( root.path(), "a.pack", PackActionsJson );
    PluginManager manager( root.path(), QDir( root.path() ).filePath( "none" ) );
    manager.discover();
    auto& registry = PreviewPackRegistry::instance();
    registry.attach( &manager );
    QSignalSpy changed( &actions, &ActionsManager::actionsChanged );
    REQUIRE( manager.manage( "enable", "a.pack", {} ).ok() );
    CHECK( changed.count() >= 1 );
    REQUIRE( registry.contributions().size() == 1 );
    CHECK( registry.contributions().first().actions.size() == 2 );
    CHECK( manager.plugins().first().toMap()[ "packActions" ] == 2 );

    // The pack's actions come after the user's, with run-time ids from 1,000,000 and the pack's
    // name; the user's own list (saved and exported) does not hold them.
    const auto packActions = actions.packActions();
    REQUIRE( packActions.size() == 2 );
    CHECK( packActions.at( 0 ).localId == 3 );
    CHECK( packActions.at( 0 ).packId == "a.pack" );
    CHECK( packActions.at( 0 ).packName == "Pack a.pack" );
    const auto reset = packActions.at( 0 ).action.id;
    const auto status = packActions.at( 1 ).action.id;
    CHECK( reset >= PackItemIdBase );
    CHECK( status >= PackItemIdBase );
    CHECK( reset != status );
    REQUIRE( actions.findActionById( status ) != nullptr );
    CHECK( actions.findActionById( status )->name == "Status" );
    CHECK( actions.actions().size() == 1 );
    // "<pack-id>/<local-id>" names it the same on every PC.
    CHECK( namespacedItemId( "a.pack", 7 ) == "a.pack/7" );
    CHECK( actions.resolvePackAction( "a.pack/7" ) == status );
    CHECK( !actions.resolvePackAction( "a.pack/9" ) );
    CHECK( !actions.resolvePackAction( "b.pack/7" ) );
    CHECK( !actions.resolvePackAction( "a.pack/07" ) );

    // The pack's response runs its own pack's actions, starts off and is matched after the
    // user's.
    const auto packResponses = actions.packResponses();
    REQUIRE( packResponses.size() == 1 );
    const auto boot = packResponses.first().response.id;
    CHECK( boot >= PackItemIdBase );
    CHECK( !packResponses.first().response.enabled );
    const auto& steps = packResponses.first().response.response.steps;
    REQUIRE( steps.size() == 2 );
    CHECK( steps.at( 0 ).actionId == status );
    CHECK( steps.at( 1 ).actionId == reset );
    CHECK( actions.resolvePackResponse( "a.pack/3" ) == boot );
    CHECK( actions.findResponseByName( "after BOOT" ) == actions.findResponseById( boot ) );
    const auto effective = actions.effectiveResponses();
    REQUIRE( effective.size() == 2 );
    CHECK( effective.at( 0 ).id == 1 );
    CHECK( effective.at( 1 ).id == boot );

    // On or off is the user's, kept per pack and item: a disable and enable (as an update) keeps
    // it, and keeps the run-time ids.
    REQUIRE( actions.setResponseEnabled( boot, true ) );
    CHECK( actions.packResponses().first().response.enabled );
    REQUIRE( manager.manage( "disable", "a.pack", {} ).ok() );
    CHECK( actions.packActions().isEmpty() );
    CHECK( actions.findActionById( status ) == nullptr ); // a user's link to it runs nothing
    REQUIRE( manager.manage( "enable", "a.pack", {} ).ok() );
    CHECK( actions.resolvePackAction( "a.pack/7" ) == status );
    REQUIRE( actions.packResponses().size() == 1 );
    CHECK( actions.packResponses().first().response.id == boot );
    CHECK( actions.packResponses().first().response.enabled );
    CHECK( actions.setPackResponsesEnabled( "a.pack", false ) == 1 );
    CHECK( !actions.packResponses().first().response.enabled );
    CHECK( actions.setPackResponsesEnabled( "a.pack", false ) == 0 );
    // Removing the pack forgets which of its responses were on: installed again, they start off
    // (Codex review of #176); the run-time ids stay.
    REQUIRE( actions.setResponseEnabled( boot, true ) );
    REQUIRE( manager.manage( "remove", "a.pack", {} ).ok() );
    CHECK( actions.packActions().isEmpty() );
    writeActionPack( root.path(), "a.pack", PackActionsJson );
    manager.discover();
    REQUIRE( manager.manage( "enable", "a.pack", {} ).ok() );
    REQUIRE( actions.packResponses().size() == 1 );
    CHECK( actions.packResponses().first().response.id == boot );
    CHECK( !actions.packResponses().first().response.enabled );
    CHECK( actions.resolvePackAction( "a.pack/7" ) == status );

    // A pack's items are not changed as the user's (copy them, or edit the pack's item), and the
    // packs' id range is not the user's.
    QString error;
    CHECK( !actions.updateAction( status, packActions.at( 1 ).action, &error ) );
    CHECK( error.contains( "a.pack/7" ) );
    CHECK( !actions.deleteResponse( boot, &error ) );
    CHECK( error.contains( "a.pack/3" ) );
    CHECK( !actions.createAction( userAction( PackItemIdBase ), &error ) );
    CHECK( error.contains( "kept for configuration packs" ) );
    CHECK( !actions.importFromDefinitions( { userAction( PackItemIdBase + 5 ) }, {} ).ok );
    // A response of the user's may run a pack action.
    REQUIRE( actions.createResponse( userResponse( 2, status ), &error ) );

    // Copy to My Actions: the user's own copy, with a new id.
    const auto copy = actions.copyPackAction( status, &error );
    REQUIRE( copy );
    CHECK( *copy < PackItemIdBase );
    CHECK( actions.findActionById( *copy )->name == "Status" );
    // A response's copy brings the actions it runs and runs those copies; it starts off.
    const auto before = actions.actions().size();
    const auto responseCopy = actions.copyPackResponse( boot, &error );
    REQUIRE( responseCopy );
    CHECK( actions.actions().size() == before + 2 );
    const auto* copied = actions.findResponseById( *responseCopy );
    REQUIRE( copied != nullptr );
    CHECK( !copied->enabled );
    REQUIRE( copied->response.steps.size() == 2 );
    for ( const auto& step : copied->response.steps ) {
        CHECK( step.actionId < PackItemIdBase );
        CHECK( actions.findActionById( step.actionId ) != nullptr );
    }
    registry.attach( nullptr );
    CHECK( actions.packActions().isEmpty() );
}

TEST_CASE( "A pack's actions must be ones CILogg runs, within the action limits", "[plugins][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    ScopedPackActions restore;
    auto& actions = ActionsManager::instance();
    // An action CILogg would not run (no sequence): the pack's actions are left out with the
    // reason, and enabling the pack is refused with it.
    const auto folder
        = writeActionPack( root.path(), "a.pack",
                           R"({ "actions": [ { "id": 1, "name": "Empty" } ], "responses": [] })" );
    const auto manifest = Manifest::read( folder );
    REQUIRE( manifest.valid() );
    const auto read = PreviewPackRegistry::read( "a.pack", folder, manifest.json );
    CHECK( read.error.isEmpty() );
    CHECK( read.actions.isEmpty() );
    REQUIRE( read.warnings.size() == 1 );
    CHECK( read.warnings.first().contains( "Missing sequence" ) );
    CHECK( PreviewPackRegistry::instance()
               .check( "a.pack", folder, manifest.json )
               .contains( "Missing sequence" ) );
    // Nor one the parser reads otherwise than written (an unknown sequence type would be sent as
    // text): its warning is the pack's error (Codex review of #176).
    writeFile( folder + "/actions.json",
               R"({ "actions": [ { "id": 1, "name": "Hex",
                    "sequence": { "type": "hex", "value": "0102" } } ], "responses": [] })" );
    const auto warned = PreviewPackRegistry::read( "a.pack", folder, manifest.json );
    CHECK( warned.actions.isEmpty() );
    REQUIRE( warned.warnings.size() == 1 );
    CHECK( warned.warnings.first().contains( "Unknown sequence type" ) );

    // The user's actions and every enabled pack's within 4,096 together.
    writeFile( folder + "/actions.json", PackActionsJson );
    QVector<ActionDefinition> many;
    for ( int id = 1; id < MaxListedActionItems; ++id ) {
        many.append( userAction( id ) );
    }
    REQUIRE( actions.importFromDefinitions( many, {} ).ok );
    CHECK( PreviewPackRegistry::instance()
               .check( "a.pack", folder, Manifest::read( folder ).json )
               .contains( "would make 4097 actions" ) );
    // When the user's grow past it, the packs' give way, and so do the packs' responses that
    // run them (Codex review of #176).
    actions.setPackItems( { { "a.pack", "A", { userAction( 1 ) }, { userResponse( 5, 1 ) } } } );
    REQUIRE( actions.packActions().size() == 1 );
    CHECK( actions.packResponses().size() == 1 );
    const auto fits = actions.packActions().first().action.id;
    CHECK( actions.findActionById( fits ) != nullptr );
    // An action of the user's that makes them give way tells the responses too (the Responses
    // table follows), and deleting it brings them back.
    {
        QSignalSpy responsesChanged( &actions, &ActionsManager::responsesChanged );
        QString error;
        REQUIRE( actions.createAction( userAction( 0, "One more" ), &error ) );
        CHECK( actions.packResponses().isEmpty() );
        CHECK( responsesChanged.count() == 1 );
        REQUIRE( actions.deleteAction( actions.actions().last().id, &error ) );
        CHECK( actions.packResponses().size() == 1 );
    }
    actions.setPackItems(
        { { "a.pack", "A", { userAction( 1 ), userAction( 2 ) }, { userResponse( 5, 1 ) } } } );
    CHECK( actions.packActions().isEmpty() );
    CHECK( actions.findActionById( fits ) == nullptr );
    CHECK( actions.packResponses().isEmpty() );
    CHECK( actions.effectiveResponses().isEmpty() );
    actions.setPackItems( {} );

    // Past the limit with the user's own, a pack that brings no actions is not refused over them
    // (Codex review of #176); one that brings some is.
    many.append( userAction( MaxListedActionItems ) );
    many.append( userAction( MaxListedActionItems + 1 ) );
    REQUIRE( actions.importFromDefinitions( many, {} ).ok );
    QTemporaryDir other;
    REQUIRE( other.isValid() );
    const auto previewsOnly = writePack( other.path(), packManifest() );
    CHECK( PreviewPackRegistry::instance()
               .check( PackId, previewsOnly, Manifest::read( previewsOnly ).json )
               .isEmpty() );
    CHECK( PreviewPackRegistry::instance()
               .check( "a.pack", folder, Manifest::read( folder ).json )
               .contains( "actions" ) );
}

namespace {
// Answers the next modal message box with `answer` once it shows.
void answerNextQuestion( QMessageBox::StandardButton answer )
{
    auto* timer = new QTimer;
    auto tries = std::make_shared<int>( 0 );
    timer->setInterval( 10 );
    QObject::connect( timer, &QTimer::timeout, [ timer, answer, tries ] {
        auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() );
        if ( box != nullptr || ++*tries > 500 ) {
            timer->stop();
            timer->deleteLater();
        }
        if ( box != nullptr && box->button( answer ) != nullptr ) {
            box->button( answer )->click();
        }
    } );
    timer->start();
}
} // namespace

TEST_CASE( "The Actions window lists pack items read only, copies them and turns responses on",
           "[plugins][E16]" )
{
    ScopedPackActions restore;
    auto& actions = ActionsManager::instance();
    REQUIRE( actions.importFromDefinitions( { userAction( 1 ) }, { userResponse( 1, 1 ) } ).ok );
    const auto parsed = ActionsConfigParser().parseJson( PackActionsJson );
    REQUIRE( parsed.errors.isEmpty() );
    actions.setPackItems( { { "a.pack", "Pack A", parsed.actions, parsed.responses } } );
    ActionsResponsesWindow window;
    QStringList edited;
    window.setPackItemEditor( [ &edited ]( QWidget*, const QString& packId, const QString& path ) {
        edited.append( packId + ':' + path );
    } );
    int packsOpened = 0;
    window.setPacksOpener( [ &packsOpened ] { ++packsOpened; } );
    auto* actionsTable = window.findChild<QTableView*>( "actionsTableView" );
    auto* responsesTable = window.findChild<QTableView*>( "responsesTableView" );
    REQUIRE( actionsTable != nullptr );
    REQUIRE( responsesTable != nullptr );
    const auto button = [ &window ]( const char* name ) {
        auto* found = window.findChild<QPushButton*>( name );
        REQUIRE( found != nullptr );
        return found;
    };

    // Yours first, then the pack's with its name and the name it has on every PC.
    auto* actionModel = actionsTable->model();
    REQUIRE( actionModel->rowCount() == 3 );
    CHECK( actionModel->index( 0, 2 ).data().toString() == "Mine" );
    CHECK( actionModel->index( 2, 2 ).data().toString() == "Status (Pack A)" );
    CHECK( actionModel->index( 2, 0 ).data( ActionsTableModel::NamespacedIdRole ).toString()
           == "a.pack/7" );
    CHECK( actionModel->index( 2, 0 ).data( Qt::ToolTipRole ).toString().contains( "a.pack/7" ) );

    // A pack's action is read only: Edit opens its store item, Copy to My Actions makes it yours.
    actionsTable->selectRow( 2 );
    CHECK( button( "editActionButton" )->isEnabled() );
    CHECK( !button( "deleteActionButton" )->isEnabled() );
    CHECK( !button( "duplicateActionButton" )->isEnabled() );
    CHECK( button( "copyPackActionButton" )->isEnabled() );
    button( "editActionButton" )->click();
    CHECK( edited == QStringList{ "a.pack:actions/7.json" } );
    button( "copyPackActionButton" )->click();
    REQUIRE( actions.actions().size() == 2 );
    CHECK( actions.actions().last().name == "Status" );
    actionsTable->selectRow( 0 );
    CHECK( button( "deleteActionButton" )->isEnabled() );
    CHECK( !button( "copyPackActionButton" )->isEnabled() );
    button( "actionPacksButton" )->click();
    CHECK( packsOpened == 1 );

    // The pack's response starts off; Turn On All asks first, with the list.
    auto* responseModel = responsesTable->model();
    REQUIRE( responseModel->rowCount() == 2 );
    CHECK( responseModel->index( 1, 2 ).data().toString() == "After boot (Pack A)" );
    CHECK( responseModel->index( 1, 1 ).data( Qt::CheckStateRole ).toInt() == Qt::Unchecked );
    responsesTable->selectRow( 1 );
    CHECK( button( "turnOnPackResponsesButton" )->isEnabled() );
    CHECK( !button( "deleteResponseButton" )->isEnabled() );
    answerNextQuestion( QMessageBox::No );
    button( "turnOnPackResponsesButton" )->click();
    CHECK( !actions.packResponses().first().response.enabled );
    answerNextQuestion( QMessageBox::Yes );
    button( "turnOnPackResponsesButton" )->click();
    CHECK( actions.packResponses().first().response.enabled );
    // Its check box turns it off, as for yours.
    REQUIRE(
        responseModel->setData( responseModel->index( 1, 1 ), Qt::Unchecked, Qt::CheckStateRole ) );
    CHECK( !actions.packResponses().first().response.enabled );

    // Copy to My Responses brings copies of the actions it runs.
    responsesTable->selectRow( 1 );
    const auto before = actions.actions().size();
    button( "copyPackResponseButton" )->click();
    CHECK( actions.responses().size() == 2 );
    CHECK( actions.actions().size() == before + 2 );
    // The pack's response follows yours (now two) and opens in the pack item editor.
    REQUIRE( responseModel->rowCount() == 3 );
    CHECK( responseModel->index( 2, 2 ).data().toString() == "After boot (Pack A)" );
    responsesTable->selectRow( 2 );
    button( "editResponseButton" )->click();
    CHECK( edited.last() == "a.pack:responses/3.json" );
}

TEST_CASE( "Pack run-time ids never take the user's, and the user's ids stay below them",
           "[plugins][E16]" )
{
    // Codex review of #176: an older CILogg let the user's ids reach 1,000,000.
    ScopedPackActions restore;
    auto& actions = ActionsManager::instance();
    REQUIRE( ActionsRepository{}.save( { userAction( PackItemIdBase, "Mine, old" ),
                                         userAction( PackItemIdBase - 1, "Mine, high" ) },
                                       {} ) );
    actions.loadFromRepository();
    REQUIRE( actions.actions().size() == 2 );
    // A run-time id kept from before that a user's item has now is given again.
    PersistentInfo::getSettings( app_settings{} )
        .setValue( "PackActions/a.pack/actions/7", PackItemIdBase );
    actions.setPackItems( { { "a.pack", "Pack A", { userAction( 7, "Pack seven" ) }, {} } } );
    REQUIRE( actions.packActions().size() == 1 );
    const auto seven = actions.packActions().first().action.id;
    CHECK( seven > PackItemIdBase );
    CHECK( actions.findActionById( PackItemIdBase )->name == "Mine, old" );
    CHECK( actions.findActionById( seven )->name == "Pack seven" );
    CHECK( actions.resolvePackAction( "a.pack/7" ) == seven );
    // The user's next id is a free one below the packs' range.
    CHECK( actions.nextActionId() == 1 );
    QString error;
    REQUIRE( actions.createAction( userAction( 0, "New" ), &error ) );
    CHECK( actions.findActionById( 1 )->name == "New" );
    const auto copy = actions.copyPackAction( seven, &error );
    REQUIRE( copy );
    CHECK( *copy == 2 );
}

TEST_CASE( "A response of the user's that runs a pack action names it, so it is exported and "
           "imported",
           "[plugins][E16]" )
{
    // Codex review of #176: the export kept only responses whose actions were exported with them.
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    ScopedPackActions restore;
    auto& actions = ActionsManager::instance();
    REQUIRE( actions.importFromDefinitions( { userAction( 1 ) }, {} ).ok );
    const auto parsed = ActionsConfigParser().parseJson( PackActionsJson );
    REQUIRE( parsed.errors.isEmpty() );
    const PackActionItems pack{ "a.pack", "Pack A", parsed.actions, parsed.responses };
    actions.setPackItems( { pack } );
    const auto status = actions.resolvePackAction( "a.pack/7" );
    REQUIRE( status );
    QString error;
    REQUIRE( actions.createResponse( userResponse( 2, *status ), &error ) );
    REQUIRE( actions.findResponseById( 2 ) != nullptr );
    CHECK( actions.findResponseById( 2 )->response.steps.first().packAction == "a.pack/7" );

    // The export keeps it, with the name of the pack action it runs.
    const auto path = root.filePath( "mine.json" );
    REQUIRE( ConfigurationExport::exportSelected( ConfigurationExportKind::Actions, { 0 }, path,
                                                  &error ) );
    const auto exported = ActionsConfigParser().parseFile( path );
    REQUIRE( exported.responses.size() == 1 );
    CHECK( exported.responses.first().response.steps.first().packAction == "a.pack/7" );

    // Elsewhere (other run-time ids) the name finds the pack's action, also for the user's own.
    PersistentInfo::getSettings( app_settings{} ).remove( "PackActions" );
    PersistentInfo::getSettings( app_settings{} )
        .setValue( "PackActions/nextId", PackItemIdBase + 500 );
    actions.setPackItems( { pack } );
    const auto elsewhere = actions.resolvePackAction( "a.pack/7" );
    REQUIRE( elsewhere );
    CHECK( *elsewhere != *status );
    CHECK( actions.findResponseById( 2 )->response.steps.first().actionId == *elsewhere );
    const auto imported = actions.importFromFile( path );
    REQUIRE( imported.ok );
    CHECK( imported.warnings.isEmpty() );
    CHECK( actions.findResponseById( 2 )->response.steps.first().actionId == *elsewhere );

    // Without the pack it runs nothing, keeps the name, and says so on import.
    actions.setPackItems( {} );
    CHECK( actions.findActionById( actions.findResponseById( 2 )->response.steps.first().actionId )
           == nullptr );
    CHECK( actions.findResponseById( 2 )->response.steps.first().packAction == "a.pack/7" );
    const auto without = actions.importFromFile( path );
    REQUIRE( without.ok );
    REQUIRE( without.warnings.size() == 1 );
    CHECK( without.warnings.first().contains( "a.pack/7" ) );
    // A response that runs it can still be made or edited (it keeps the name and runs nothing
    // until the pack is back), as after an import (independent review of #176).
    auto runsPack = userResponse( 3, 1 );
    runsPack.response.steps.first().packAction = "a.pack/7";
    REQUIRE( actions.createResponse( runsPack, &error ) );
    REQUIRE( actions.findResponseById( 3 ) != nullptr );
    CHECK( actions.findResponseById( 3 )->response.steps.first().packAction == "a.pack/7" );
    CHECK( actions.findActionById( actions.findResponseById( 3 )->response.steps.first().actionId )
           == nullptr );
    auto renamed = *actions.findResponseById( 2 );
    renamed.name = QStringLiteral( "Renamed" );
    REQUIRE( actions.updateResponse( 2, renamed, &error ) );
    actions.setPackItems( { pack } );
    CHECK( actions.findResponseById( 2 )->response.steps.first().actionId == *elsewhere );
    CHECK( actions.findResponseById( 3 )->response.steps.first().actionId == *elsewhere );
}
