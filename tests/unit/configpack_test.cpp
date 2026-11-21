// Configuration packs, slice E16a1 (docs/plugin-repositories.md sections 4.1-4.4).
#include <catch2/catch.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTableView>
#include <QTemporaryFile>
#include <QTest>
#include <QToolButton>
#include <algorithm>

#include "configpack.h"
#include "importpreviewsdialog.h"
#include "pluginrepository.h"
#include "pluginsandpacks.h"
#include "plugincatalog.h"
#include "pluginmanager.h"
#include "pluginmanifest.h"
#include "previewconfigparser.h"
#include "previewdecodeutils.h"
#include "previewdecodersettings.h"
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
    CHECK( summary.value( "contents" ).toStringList()
           == QStringList{ "previews", "lua", "python_decoders" } );

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
    REQUIRE( settings.setConfiguration( original ) );
    REQUIRE( PreviewManager::instance().clearAll() );

    REQUIRE( manager.manage( "disable", "a.pack", {} ).ok() );
    CHECK( registry.contributions().isEmpty() );
    CHECK( !findIn( PreviewManager::instance().effective(), "Pack frame" ) );
    CHECK( PreviewDecoderSettings::instance().packs().lua.isEmpty() );
    registry.attach( nullptr );
}
