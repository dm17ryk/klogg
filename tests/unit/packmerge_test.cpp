// The local pack store, its canonical form and the three-way merge, slice E16a2
// (docs/plugin-repositories.md 4.6).
#include <catch2/catch.hpp>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <algorithm>
#include <functional>
#include <tuple>

#include "packcanonical.h"
#include "packmerge.h"
#include "packstore.h"
#include "pluginmanager.h"

using namespace cilogg::plugins;

namespace {
QJsonObject object( const char* json )
{
    return QJsonDocument::fromJson( json ).object();
}

std::optional<QJsonValue> value( const char* json )
{
    return QJsonValue( object( json ) );
}

QJsonObject packManifest( const QString& version = QStringLiteral( "1.0.0" ) )
{
    return { { "id", "org.example.pack" },
             { "name", "Example pack" },
             { "version", version },
             { "protocol_version", 1 },
             { "kind", "config-pack" },
             { "pack_format", 1 },
             { "content",
               QJsonObject{ { "previews", "previews.json" },
                            { "lua", QJsonObject{ { "folder", "lua" },
                                                  { "load", QJsonArray{ "a.lua" } } } } } } };
}

constexpr auto Previews = R"json({ "version": 1,
  "previews": [
    { "name": "Second", "regex": "^B$", "format": "fields", "fields": [] },
    { "name": "First Frame", "regex": "^A$", "format": "fields",
      "fields": [ { "name": "x", "source": "capture", "capture": "x", "format": "string" } ] }
  ],
  "blocks": [ { "name": "Block/1", "format": "match", "regex": "^.*$", "fields": [] } ] })json";

PackFiles packFiles( const QByteArray& previews = Previews, const QByteArray& lua = "-- a\n" )
{
    auto files = packcanonical::fromPreviews( previews );
    files.insert( packcanonical::filePath( "lua/a.lua" ), lua );
    return files;
}

QByteArray readFile( const QString& path )
{
    QFile file( path );
    return file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray{};
}

// A pack package folder: plugin.json, previews.json and lua/a.lua.
QString writePackage( const QString& folder, const QString& version, const QByteArray& previews,
                      const QByteArray& lua )
{
    QDir().mkpath( folder + "/lua" );
    const auto write = [ &folder ]( const QString& name, const QByteArray& bytes ) {
        QFile file( folder + '/' + name );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( bytes );
    };
    write( "plugin.json", QJsonDocument( packManifest( version ) ).toJson() );
    write( "previews.json", previews );
    write( "lua/a.lua", lua );
    return folder;
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

struct FilterRow {
    QString id;
    QString name;
    QString filter;
    QVariant regex; // invalid: the key is left out
};

// A predefined filters file as CILogg exports it (PredefinedFiltersDialog::exportFilters):
// QSettings in INI format, group PredefinedFiltersCollection. `extra` keys are added as they are.
QByteArray filtersConf( const QVector<FilterRow>& rows, int version = 3,
                        const QVariantMap& extra = {} )
{
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto path = folder.filePath( "filters.conf" );
    {
        QSettings settings( path, QSettings::IniFormat );
        settings.beginGroup( "PredefinedFiltersCollection" );
        settings.setValue( "version", version );
        settings.beginWriteArray( "filters" );
        for ( int index = 0; index < rows.size(); ++index ) {
            settings.setArrayIndex( index );
            const auto& row = rows.at( index );
            if ( !row.id.isNull() ) {
                settings.setValue( "id", row.id );
            }
            settings.setValue( "name", row.name );
            settings.setValue( "filter", row.filter );
            if ( row.regex.isValid() ) {
                settings.setValue( "regex", row.regex );
            }
        }
        settings.endArray();
        settings.endGroup();
        for ( auto key = extra.cbegin(); key != extra.cend(); ++key ) {
            settings.setValue( key.key(), key.value() );
        }
        settings.sync();
        REQUIRE( settings.status() == QSettings::NoError );
    }
    return readFile( path );
}

const QVector<FilterRow> SomeFilters{
    { "f-one", "Errors", "ERROR|FATAL", true },
    { "f-two", QString::fromUtf8( "Reset, \"quoted\" \xce\x94" ), "Software or Reset", false },
};

struct RuleRow {
    QString pattern;
    bool ignoreCase = false;
    bool regex = true;
    QString fore = QStringLiteral( "#ff000000" );
    QString back = QStringLiteral( "#ffffff00" );
};

struct SetRow {
    QString id; // null: the key is left out
    QString name;
    QVector<RuleRow> rules;
};

// A highlighters file as CILogg exports it (HighlighterSetCollection::saveToStorage): QSettings in
// INI format, the sets with their rules, the active sets and the color labels. `extra` keys are
// added as they are.
QByteArray highlightsConf( const QVector<SetRow>& sets, const QVariantMap& extra = {},
                           int version = 2 )
{
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto path = folder.filePath( "highlights.conf" );
    {
        QSettings settings( path, QSettings::IniFormat );
        settings.beginGroup( "HighlighterSetCollection" );
        settings.setValue( "version", version );
        settings.setValue( "active_sets", QStringList{ "s-one" } );
        settings.beginWriteArray( "sets" );
        for ( int index = 0; index < sets.size(); ++index ) {
            settings.setArrayIndex( index );
            settings.beginGroup( "HighlighterSet" );
            settings.setValue( "version", 3 );
            settings.setValue( "name", sets.at( index ).name );
            if ( !sets.at( index ).id.isNull() ) {
                settings.setValue( "id", sets.at( index ).id );
            }
            settings.beginWriteArray( "highlighters" );
            for ( int rule = 0; rule < sets.at( index ).rules.size(); ++rule ) {
                const auto& row = sets.at( index ).rules.at( rule );
                settings.setArrayIndex( rule );
                settings.setValue( "regexp", row.pattern );
                settings.setValue( "ignore_case", row.ignoreCase );
                settings.setValue( "match_only", false );
                settings.setValue( "use_regex", row.regex );
                settings.setValue( "variate_colors", false );
                settings.setValue( "color_variance", 15 );
                settings.setValue( "fore_colour", row.fore );
                settings.setValue( "back_colour", row.back );
            }
            settings.endArray();
            settings.endGroup();
        }
        settings.endArray();
        settings.beginWriteArray( "quick" );
        settings.setArrayIndex( 0 );
        settings.setValue( "name", "Color label 1" );
        settings.setValue( "fore_colour", "#ff001e80" );
        settings.setValue( "back_colour", "#ffa1b7ff" );
        settings.setValue( "cycle", true );
        settings.endArray();
        settings.endGroup();
        for ( auto key = extra.cbegin(); key != extra.cend(); ++key ) {
            settings.setValue( key.key(), key.value() );
        }
        settings.sync();
        REQUIRE( settings.status() == QSettings::NoError );
    }
    return readFile( path );
}

const QVector<SetRow> SomeSets{
    { "s-one", "Errors", { { "ERROR" }, { "fatal", true, false, "#ffffffff", "#ffff0000" } } },
    { "s-two", QString::fromUtf8( "Radio, \"quoted\" \xce\x94" ), {} },
};

// An actions file as CILogg exports it (ActionsRepository::serialize, trimmed): version 4, the
// actions and the responses with their ids and order. Reset (order 0) comes before Status.
constexpr auto SomeActions = R"json({
  "version": 4,
  "actions": [
    { "id": 7, "order": 1, "enabled": true, "hidden": false, "name": "Status",
      "description": "", "sequence": { "type": "string", "value": "status\r\n" } },
    { "id": 3, "order": 0, "enabled": true, "hidden": false, "name": "Reset",
      "description": "", "sequence": { "type": "string", "value": "reset\r\n" } }
  ],
  "responses": [
    { "id": 3, "order": 0, "enabled": true, "hidden": false, "name": "After boot",
      "description": "", "match": { "type": "string", "value": "boot done" },
      "response": { "action_id": 7, "steps": [ { "action_id": 3, "delay_ms": 100 } ],
                    "comment": "", "linebreak": false, "timestamp": false, "snapshot": false,
                    "stop_communication": false } }
  ]
})json";

// The actions file's root, with `change` applied to its JSON text.
QByteArray actionsJson( const std::function<void( QJsonObject& )>& change = {} )
{
    auto root = QJsonDocument::fromJson( SomeActions ).object();
    if ( change ) {
        change( root );
    }
    return QJsonDocument( root ).toJson();
}

// One action or response of a parsed actions file's list, by id.
QJsonObject listed( const QJsonObject& root, const QString& list, int id )
{
    for ( const auto& value : root.value( list ).toArray() ) {
        if ( value.toObject().value( "id" ).toInt( -1 ) == id ) {
            return value.toObject();
        }
    }
    return {};
}

QString previewRegex( const QString& folder, const QString& name )
{
    const auto previews = QJsonDocument::fromJson( readFile( folder + "/previews.json" ) )
                              .object()
                              .value( "previews" )
                              .toArray();
    for ( const auto& preview : previews ) {
        if ( preview.toObject().value( "name" ) == name ) {
            return preview.toObject().value( "regex" ).toString();
        }
    }
    return {};
}
} // namespace

TEST_CASE( "Pack items have a canonical one-item-per-file form", "[packmerge][E16]" )
{
    CHECK( packcanonical::encodeName( "First Frame" ) == "%46irst%20%46rame" );
    CHECK( packcanonical::encodeName( "a/b" ) == "a%2Fb" );
    CHECK( packcanonical::encodeName( ".." ) == "%2E%2E" );
    const auto unicode = QString::fromUtf8( "\xce\x94 frame/1" );
    CHECK( packcanonical::decodeName( packcanonical::encodeName( unicode ) ) == unicode );
    // A character outside the BMP (two QChars) is encoded whole, as its four UTF-8 bytes.
    const auto astral = QString::fromUtf8( "Frame \xf0\x9f\x93\xa1" );
    CHECK( packcanonical::encodeName( astral ) == "%46rame%20%F0%9F%93%A1" );
    CHECK( packcanonical::decodeName( packcanonical::encodeName( astral ) ) == astral );
    CHECK( packcanonical::encodeName( "A" ) != packcanonical::encodeName( "a" ) );

    // A malformed previews document is refused, never cleaned up quietly.
    for ( const auto* bad :
          { R"({ "version": 1 })", R"({ "previews": {} })", R"({ "previews": [ 3 ] })",
            R"({ "previews": [ { "regex": "x" } ] })",
            R"({ "previews": [ { "name": "A" }, { "name": "A" } ] })",
            R"({ "previews": [], "blocks": [ { "name": "B" }, { "name": "B" } ] })",
            R"({ "previews": [], "blocks": {} })" } ) {
        QString why;
        INFO( bad );
        CHECK( packcanonical::fromPreviews( bad, &why ).isEmpty() );
        CHECK( why.startsWith( "The pack's previews document is not valid" ) );
    }
    // A pack over the preview limits could never be enabled: refused before it becomes thousands
    // of store items (a 1 MiB document can hold tens of thousands of small ones).
    {
        QJsonArray many;
        for ( int index = 0; index < 20000; ++index ) {
            many.append( QJsonObject{ { "name", QStringLiteral( "p%1" ).arg( index ) } } );
        }
        QString why;
        CHECK( packcanonical::fromPreviews( QJsonDocument( QJsonObject{ { "previews", many } } ).toJson(), &why )
                   .isEmpty() );
        CHECK( why.contains( "more than 128 previews" ) );
    }
    // Every Lua and Python file the manifest lists is carried as a file; filters, highlights and
    // actions have a canonical form of their own (E16c).
    auto withFilters = packManifest();
    auto declared = withFilters[ "content" ].toObject();
    declared[ "filters" ] = "filters/predefined.conf";
    declared[ "highlights" ] = "highlights.conf";
    declared[ "actions" ] = "actions.json";
    withFilters[ "content" ] = declared;
    CHECK( packcanonical::listedFiles( withFilters ) == QStringList{ "files/lua/a.lua" } );

    QString error;
    const auto files = packcanonical::fromPreviews( Previews, &error );
    REQUIRE( error.isEmpty() );
    CHECK( files.contains( "previews/order.txt" ) );
    CHECK( files.value( "previews/order.txt" ) == "Second\nFirst Frame\n" );
    CHECK( files.contains( "previews/%46irst%20%46rame.json" ) );
    CHECK( files.contains( "blocks/%42lock%2F1.json" ) );
    CHECK( packcanonical::kindOf( "previews/%46irst%20%46rame.json" ) == "preview" );
    CHECK( packcanonical::itemOf( "previews/%46irst%20%46rame.json" ) == "First Frame" );
    CHECK( packcanonical::kindOf( "blocks/%42lock%2F1.json" ) == "block" );
    CHECK( packcanonical::kindOf( "files/lua/a.lua" ) == "file" );
    CHECK( packcanonical::itemOf( "files/lua/a.lua" ) == "lua/a.lua" );
    // One member per line, sorted keys.
    const auto item = files.value( "previews/%53econd.json" );
    CHECK( item.indexOf( "\"fields\"" ) < item.indexOf( "\"format\"" ) );
    CHECK( item.count( '\n' ) >= 5 );

    // The round trip keeps every preview, its order and every block.
    const auto back = QJsonDocument::fromJson( packcanonical::toPreviews( files ) ).object();
    const auto original = QJsonDocument::fromJson( Previews ).object();
    CHECK( back.value( "previews" ) == original.value( "previews" ) );
    CHECK( back.value( "blocks" ) == original.value( "blocks" ) );
    CHECK( back.value( "version" ) == 1 );
    CHECK( packcanonical::fromPreviews( packcanonical::toPreviews( files ) ) == files );

    // A preview named "meta" never takes the previews settings' file (previews/meta.json).
    CHECK( packcanonical::encodeName( "meta" ) == "%6Deta" );
    const auto withMeta = packcanonical::fromPreviews(
        R"({ "version": 1, "previews": [ { "name": "meta", "regex": "^M$", "format": "fields", "fields": [] } ] })" );
    CHECK( withMeta.contains( "previews/%6Deta.json" ) );
    CHECK( packcanonical::kindOf( "previews/%6Deta.json" ) == "preview" );
    CHECK( packcanonical::itemOf( "previews/%6Deta.json" ) == "meta" );
    CHECK( packcanonical::isCanonical( "previews/%6Deta.json" ) );
    CHECK( QJsonDocument::fromJson( withMeta.value( "previews/meta.json" ) ).object().value( "version" ).toInt() == 1 );
    const auto metaBack = QJsonDocument::fromJson( packcanonical::toPreviews( withMeta ) ).object();
    REQUIRE( metaBack.value( "previews" ).toArray().size() == 1 );
    CHECK( metaBack.value( "previews" ).toArray().first().toObject().value( "name" ).toString() == "meta" );
}

TEST_CASE( "Pack filters have a canonical one-filter-per-file form", "[packmerge][E16]" )
{
    QString error;
    const auto files = packcanonical::fromFilters( filtersConf( SomeFilters ), &error );
    REQUIRE( error.isEmpty() );
    CHECK( files.value( "filters/order.txt" ) == "f-one\nf-two\n" );
    REQUIRE( files.contains( "filters/f-one.json" ) );
    CHECK( QJsonDocument::fromJson( files.value( "filters/f-one.json" ) ).object()
           == object( R"({"filter":"ERROR|FATAL","id":"f-one","name":"Errors","regex":true})" ) );
    // One member per line, sorted keys, like every structured item.
    CHECK( files.value( "filters/f-one.json" )
           == packcanonical::itemBytes(
               QJsonDocument::fromJson( files.value( "filters/f-one.json" ) ).object() ) );
    CHECK( QJsonDocument::fromJson( files.value( "filters/f-two.json" ) ).object().value( "name" )
           == SomeFilters.at( 1 ).name );
    CHECK( packcanonical::kindOf( "filters/f-one.json" ) == "filter" );
    CHECK( packcanonical::itemOf( "filters/f-one.json" ) == "f-one" );
    CHECK( packcanonical::isCanonical( "filters/f-one.json" ) );
    CHECK( !packcanonical::isCanonical( "filters/F-one.json" ) );
    CHECK( packcanonical::kindOf( "filters/order.txt" ) == "filter-order" );
    CHECK( packcanonical::isCanonical( "filters/order.txt" ) );
    // A filter whose id is "order" is an item like any other (the order file ends in .txt).
    CHECK( packcanonical::kindOf( "filters/order.json" ) == "filter" );
    CHECK( packcanonical::itemOf( "filters/order.json" ) == "order" );

    // The round trip keeps every filter, its order and its values, and the file it writes is the
    // filters file CILogg imports (QSettings, INI format, version 3).
    const auto conf = packcanonical::toFilters( files );
    CHECK( packcanonical::fromFilters( conf ) == files );
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    {
        QFile out( folder.filePath( "back.conf" ) );
        REQUIRE( out.open( QIODevice::WriteOnly ) );
        out.write( conf );
    }
    QSettings back( folder.filePath( "back.conf" ), QSettings::IniFormat );
    CHECK( back.value( "PredefinedFiltersCollection/version" ).toInt() == 3 );
    CHECK( back.value( "PredefinedFiltersCollection/filters/size" ).toInt() == 2 );
    CHECK( back.value( "PredefinedFiltersCollection/filters/2/name" ).toString()
           == SomeFilters.at( 1 ).name );
    CHECK( back.value( "PredefinedFiltersCollection/filters/2/regex" ).toBool() == false );

    // Filters missing from the order follow in id order; order lines without a filter are skipped.
    auto reordered = files;
    reordered.insert( "filters/order.txt", "f-two\ngone\n" );
    const auto again = packcanonical::fromFilters( packcanonical::toFilters( reordered ) );
    CHECK( again.value( "filters/order.txt" ) == "f-two\nf-one\n" );
    // An order edited with CR LF line ends keeps its order (Codex review of #165).
    reordered.insert( "filters/order.txt", "f-two\r\nf-one\r\n" );
    CHECK( packcanonical::fromFilters( packcanonical::toFilters( reordered ) )
               .value( "filters/order.txt" )
           == "f-two\nf-one\n" );
    auto previewOrder = packcanonical::fromPreviews( Previews );
    previewOrder.insert( "previews/order.txt", "First Frame\r\nSecond\r\n" );
    CHECK( packcanonical::fromPreviews( packcanonical::toPreviews( previewOrder ) )
               .value( "previews/order.txt" )
           == "First Frame\nSecond\n" );

    // An exported file without `regex` is a regex filter, as the import reads it.
    const auto noRegex = packcanonical::fromFilters( filtersConf( { { "x", "X", "x", {} } } ) );
    CHECK( QJsonDocument::fromJson( noRegex.value( "filters/x.json" ) ).object().value( "regex" )
           == true );
    // No filters at all is a valid, empty filters file.
    const auto none = packcanonical::fromFilters( filtersConf( {} ), &error );
    CHECK( error.isEmpty() );
    CHECK( none == PackFiles{ { "filters/order.txt", QByteArray{} } } );

    // A file that is not such a filters file is refused, never cleaned up quietly: a filter's
    // identity is its id, and anything the file holds besides the filters would be lost.
    const QVector<std::pair<const char*, QByteArray>> bad{
        { "not INI", QByteArray( "\xff\xfe\x00[[[", 6 ) },
        { "no group", "[Other]\nx=1\n" },
        { "newer version", filtersConf( SomeFilters, 4 ) },
        { "no id", filtersConf( { { QString{}, "A", "a", true } } ) },
        { "empty id", filtersConf( { { "", "A", "a", true } } ) },
        { "id with a line break", filtersConf( { { "a\nb", "A", "a", true } } ) },
        { "repeated id", filtersConf( { { "a", "A", "a", true }, { "a", "B", "b", true } } ) },
        { "no name", filtersConf( { { "a", "", "a", true } } ) },
        { "no pattern", filtersConf( { { "a", "A", "", true } } ) },
        { "regex not a boolean", filtersConf( { { "a", "A", "a", "maybe" } } ) },
        { "another key", filtersConf( SomeFilters, 3, { { "General/other", 1 } } ) },
        { "another filter key",
          filtersConf( SomeFilters, 3,
                       { { "PredefinedFiltersCollection/filters/1/color", "red" } } ) },
        { "a filter past the size",
          filtersConf( SomeFilters, 3,
                       { { "PredefinedFiltersCollection/filters/3/name", "C" } } ) },
    };
    for ( const auto& [ why, bytes ] : bad ) {
        INFO( why );
        QString refused;
        CHECK( packcanonical::fromFilters( bytes, &refused ).isEmpty() );
        CHECK( refused.startsWith( "The pack's filters file is not valid" ) );
    }
    // More filters than a configuration import takes (4,096) is refused before it becomes that
    // many store items.
    {
        QVector<FilterRow> many;
        for ( int index = 0; index <= packcanonical::MaxPackFilterItems; ++index ) {
            many.append( { QStringLiteral( "f%1" ).arg( index ), "F", "f", true } );
        }
        QString refused;
        CHECK( packcanonical::fromFilters( filtersConf( many ), &refused ).isEmpty() );
        CHECK( refused.contains( "more than 4096 filters" ) );
    }

    // A pack folder's filters file becomes its filter items.
    QTemporaryDir pack;
    REQUIRE( pack.isValid() );
    QDir().mkpath( pack.filePath( "lua" ) );
    QDir().mkpath( pack.filePath( "filters" ) );
    const auto write = [ &pack ]( const QString& name, const QByteArray& bytes ) {
        QFile file( pack.filePath( name ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( bytes );
    };
    auto manifest = packManifest();
    auto content = manifest[ "content" ].toObject();
    content[ "filters" ] = "filters/predefined.conf";
    manifest[ "content" ] = content;
    write( "previews.json", Previews );
    write( "lua/a.lua", "-- a\n" );
    write( "filters/predefined.conf", filtersConf( SomeFilters ) );
    error.clear();
    const auto folderFiles = packcanonical::fromPackFolder( manifest, pack.path(), &error );
    CHECK( error.isEmpty() );
    CHECK( folderFiles.contains( "filters/f-two.json" ) );
    CHECK( folderFiles.value( "filters/order.txt" ) == "f-one\nf-two\n" );
    CHECK( !folderFiles.contains( "files/filters/predefined.conf" ) );
    // A bad filters file in a pack folder is the pack's error.
    write( "filters/predefined.conf", "[Other]\nx=1\n" );
    CHECK( packcanonical::fromPackFolder( manifest, pack.path(), &error ).isEmpty() );
    CHECK( error.startsWith( "The pack's filters file is not valid" ) );
}

TEST_CASE( "Pack highlighter sets have a canonical one-set-per-file form", "[packmerge][E16]" )
{
    QString error;
    const auto files = packcanonical::fromHighlights( highlightsConf( SomeSets ), &error );
    REQUIRE( error.isEmpty() );
    CHECK( files.value( "highlights/order.txt" ) == "s-one\ns-two\n" );
    // One set per file, merged as text: its id and name, then one line per rule, so edits of
    // different rules never touch the same line (design 4.6).
    CHECK( files.value( "highlights/s-one.set" )
           == "id \"s-one\"\n"
              "name \"Errors\"\n"
              "rule "
              "{\"back_colour\":\"#ffffff00\",\"color_variance\":15,\"fore_colour\":\"#ff000000\","
              "\"ignore_case\":false,\"match_only\":false,\"regexp\":\"ERROR\",\"use_regex\":true,"
              "\"variate_colors\":false}\n"
              "rule "
              "{\"back_colour\":\"#ffff0000\",\"color_variance\":15,\"fore_colour\":\"#ffffffff\","
              "\"ignore_case\":true,\"match_only\":false,\"regexp\":\"fatal\",\"use_regex\":false,"
              "\"variate_colors\":false}\n" );
    CHECK( files.value( "highlights/s-two.set" )
               .startsWith( "id \"s-two\"\nname \"Radio, \\\"quoted\\\"" ) );
    CHECK( packcanonical::kindOf( "highlights/s-one.set" ) == "highlight" );
    CHECK( packcanonical::itemOf( "highlights/s-one.set" ) == "s-one" );
    CHECK( packcanonical::isCanonical( "highlights/s-one.set" ) );
    CHECK( !packcanonical::isCanonical( "highlights/S-one.set" ) );
    CHECK( packcanonical::kindOf( "highlights/order.txt" ) == "highlight-order" );
    // The active sets and the color labels are the user's, not the pack's: not carried.
    CHECK( !files.value( "highlights/s-one.set" ).contains( "active" ) );
    CHECK( files.size() == 3 );

    // The round trip keeps every set, its rules and the order, and writes the file CILogg imports.
    const auto conf = packcanonical::toHighlights( files );
    CHECK( packcanonical::fromHighlights( conf ) == files );
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    {
        QFile out( folder.filePath( "back.conf" ) );
        REQUIRE( out.open( QIODevice::WriteOnly ) );
        out.write( conf );
    }
    QSettings back( folder.filePath( "back.conf" ), QSettings::IniFormat );
    CHECK( back.value( "HighlighterSetCollection/sets/size" ).toInt() == 2 );
    CHECK( back.value( "HighlighterSetCollection/sets/2/HighlighterSet/name" ).toString()
           == SomeSets.at( 1 ).name );
    CHECK( back.value( "HighlighterSetCollection/sets/1/HighlighterSet/highlighters/2/regexp" )
               .toString()
           == "fatal" );
    CHECK( !back.contains( "HighlighterSetCollection/active_sets" ) );
    // A set's text with CR LF line ends (edited on Windows) reads the same.
    auto crlf = files;
    crlf.insert( "highlights/s-one.set",
                 QByteArray( files.value( "highlights/s-one.set" ) ).replace( "\n", "\r\n" ) );
    crlf.insert( "highlights/order.txt", "s-two\r\ns-one\r\n" );
    const auto again = packcanonical::fromHighlights( packcanonical::toHighlights( crlf ) );
    CHECK( again.value( "highlights/s-one.set" ) == files.value( "highlights/s-one.set" ) );
    CHECK( again.value( "highlights/order.txt" ) == "s-two\ns-one\n" );
    // A set without rules and an export without sets are valid.
    CHECK( packcanonical::fromHighlights( highlightsConf( {} ), &error )
           == PackFiles{ { "highlights/order.txt", QByteArray{} } } );
    CHECK( error.isEmpty() );

    // Anything that is not such a file is refused, never cleaned up quietly.
    const QVector<std::pair<const char*, QByteArray>> bad{
        { "no group", "[Other]\nx=1\n" },
        { "newer version", highlightsConf( SomeSets, {}, 3 ) },
        { "no id", highlightsConf( { { QString{}, "A", {} } } ) },
        { "repeated id", highlightsConf( { { "a", "A", {} }, { "a", "B", {} } } ) },
        { "no name", highlightsConf( { { "a", "", {} } } ) },
        { "another key", highlightsConf( SomeSets, { { "General/other", 1 } } ) },
        { "another rule key",
          highlightsConf(
              SomeSets,
              { { "HighlighterSetCollection/sets/1/HighlighterSet/highlighters/1/x", 1 } } ) },
        { "a rule past the size",
          highlightsConf(
              SomeSets, { { "HighlighterSetCollection/sets/1/HighlighterSet/highlighters/3/regexp",
                            "x" } } ) },
        { "a boolean that is not one",
          highlightsConf(
              SomeSets,
              { { "HighlighterSetCollection/sets/1/HighlighterSet/highlighters/1/use_regex",
                  "maybe" } } ) },
        { "a variance that is not a number",
          highlightsConf(
              SomeSets,
              { { "HighlighterSetCollection/sets/1/HighlighterSet/highlighters/1/color_variance",
                  "x" } } ) },
        // Every counted rule is there: a count alone would make thousands of rules out of a few
        // bytes (independent review of #169).
        { "rules counted but not there",
          highlightsConf(
              SomeSets,
              { { "HighlighterSetCollection/sets/2/HighlighterSet/highlighters/size", 4096 } } ) },
        // A value with an unquoted comma reads as a list, and as a pattern it would be empty,
        // matching every line (independent review of #169).
        { "a list where a pattern goes",
          highlightsConf( SomeSets ).replace( "regexp=ERROR", "regexp=a{1,3}" ) },
        // A variance outside 0..100 makes an invalid colour range (issue #170).
        { "a negative variance",
          highlightsConf(
              SomeSets,
              { { "HighlighterSetCollection/sets/1/HighlighterSet/highlighters/1/color_variance",
                  -50 } } ) },
        { "a huge variance",
          highlightsConf(
              SomeSets,
              { { "HighlighterSetCollection/sets/1/HighlighterSet/highlighters/1/color_variance",
                  2147483647 } } ) },
    };
    for ( const auto& [ why, bytes ] : bad ) {
        INFO( why );
        QString refused;
        CHECK( packcanonical::fromHighlights( bytes, &refused ).isEmpty() );
        CHECK( refused.startsWith( "The pack's highlighters file is not valid" ) );
    }
    // An older export without the rule members CILogg added later still reads, with the defaults
    // CILogg reads them with; only the pattern of every counted rule must be there.
    {
        QByteArray older;
        for ( const auto& line : highlightsConf( SomeSets ).split( '\n' ) ) {
            if ( !line.contains( "match_only" ) && !line.contains( "variate_colors" )
                 && !line.contains( "color_variance" ) ) {
                older += line + '\n';
            }
        }
        QString olderError;
        const auto olderFiles = packcanonical::fromHighlights( older, &olderError );
        CHECK( olderError.isEmpty() );
        CHECK( olderFiles.value( "highlights/s-one.set" ).contains( "\"color_variance\":15" ) );
        QByteArray noPattern;
        for ( const auto& line : highlightsConf( SomeSets ).split( '\n' ) ) {
            if ( !line.contains( "highlighters\\2\\regexp" ) ) {
                noPattern += line + '\n';
            }
        }
        CHECK( packcanonical::fromHighlights( noPattern ).isEmpty() );
    }
    // A set's text that is not one CILogg writes is not a set.
    CHECK(
        packcanonical::highlightSetError( "id \"s-one\"\nname \"E\"\nrule {}\n", "s-one" ).isEmpty()
        == false );
    CHECK( packcanonical::highlightSetError( "name \"E\"\n", "s-one" ).contains( "id" ) );
    CHECK( packcanonical::highlightSetError( files.value( "highlights/s-one.set" ), "s-two" )
               .contains( "id" ) );
    CHECK( packcanonical::highlightSetError( files.value( "highlights/s-one.set" ), "s-one" )
               .isEmpty() );
    // An edited set's variance stays within 0..100 too (issue #170).
    for ( const auto* variance : { "-1", "101" } ) {
        INFO( variance );
        auto edited = files.value( "highlights/s-one.set" );
        REQUIRE( edited.contains( "\"color_variance\":15" ) );
        edited.replace( "\"color_variance\":15", QByteArray( "\"color_variance\":" ) + variance );
        CHECK( !packcanonical::highlightSetError( edited, "s-one" ).isEmpty() );
    }

    // A pack folder's highlighters file becomes its set items.
    QTemporaryDir pack;
    REQUIRE( pack.isValid() );
    auto manifest = packManifest();
    auto content = manifest[ "content" ].toObject();
    content.remove( "lua" );
    content[ "highlights" ] = "highlights.conf";
    manifest[ "content" ] = content;
    {
        QFile previews( pack.filePath( "previews.json" ) );
        REQUIRE( previews.open( QIODevice::WriteOnly ) );
        previews.write( Previews );
        QFile highlights( pack.filePath( "highlights.conf" ) );
        REQUIRE( highlights.open( QIODevice::WriteOnly ) );
        highlights.write( highlightsConf( SomeSets ) );
    }
    error.clear();
    const auto folderFiles = packcanonical::fromPackFolder( manifest, pack.path(), &error );
    CHECK( error.isEmpty() );
    CHECK( folderFiles.contains( "highlights/s-two.set" ) );
    CHECK( !folderFiles.contains( "files/highlights.conf" ) );
    CHECK( packcanonical::listedFiles( manifest ).isEmpty() );
}

TEST_CASE( "Pack actions and responses have a canonical one-item-per-file form",
           "[packmerge][E16]" )
{
    QString error;
    const auto files = packcanonical::fromActions( SomeActions, &error );
    REQUIRE( error.isEmpty() );
    // One action and one response per file, known by its id (actions and responses have ids of
    // their own: both have a 3), and an order for each list. The list order is the one CILogg
    // reads (by "order"), so the items do not carry it.
    CHECK( files.keys()
           == QStringList{ "actions/3.json", "actions/7.json", "actions/order.txt",
                           "responses/3.json", "responses/order.txt" } );
    CHECK( files.value( "actions/order.txt" ) == "3\n7\n" );
    CHECK( files.value( "responses/order.txt" ) == "3\n" );
    auto status = QJsonDocument::fromJson( SomeActions )
                      .object()
                      .value( "actions" )
                      .toArray()
                      .first()
                      .toObject();
    status.remove( "order" );
    CHECK( files.value( "actions/7.json" ) == packcanonical::itemBytes( status ) );
    CHECK( QJsonDocument::fromJson( files.value( "responses/3.json" ) ).object().value( "name" )
           == "After boot" );
    CHECK( packcanonical::kindOf( "actions/7.json" ) == "action" );
    CHECK( packcanonical::itemOf( "actions/7.json" ) == "7" );
    CHECK( packcanonical::kindOf( "actions/order.txt" ) == "action-order" );
    CHECK( packcanonical::kindOf( "responses/3.json" ) == "response" );
    CHECK( packcanonical::itemOf( "responses/3.json" ) == "3" );
    CHECK( packcanonical::kindOf( "responses/order.txt" ) == "response-order" );
    CHECK( packcanonical::isCanonical( "actions/7.json" ) );
    CHECK( packcanonical::isCanonical( "responses/order.txt" ) );
    // An id is a whole number written without a sign or leading zeros.
    for ( const auto* odd : { "actions/07.json", "actions/-1.json", "actions/x.json",
                              "responses/1.5.json", "actions/order.json" } ) {
        INFO( odd );
        CHECK( !packcanonical::isCanonical( odd ) );
    }

    // The round trip keeps every action and response, both orders and every value, and the file
    // it writes is the actions file CILogg imports (version 4, "order" from the list order).
    const auto json = packcanonical::toActions( files, &error );
    REQUIRE( error.isEmpty() );
    CHECK( packcanonical::fromActions( json ) == files );
    const auto back = QJsonDocument::fromJson( json ).object();
    CHECK( back.keys() == QStringList{ "actions", "responses", "version" } );
    CHECK( back.value( "version" ) == 4 );
    REQUIRE( back.value( "actions" ).toArray().size() == 2 );
    CHECK( back.value( "actions" ).toArray().at( 0 ).toObject().value( "name" ) == "Reset" );
    CHECK( back.value( "actions" ).toArray().at( 0 ).toObject().value( "order" ) == 0 );
    CHECK( back.value( "actions" ).toArray().at( 1 ).toObject().value( "order" ) == 1 );
    CHECK( listed( back, "responses", 3 ).value( "response" )
           == QJsonDocument::fromJson( SomeActions )
                  .object()
                  .value( "responses" )
                  .toArray()
                  .first()
                  .toObject()
                  .value( "response" ) );

    // Items missing from the order follow in id order (as numbers); order lines without an item
    // are skipped, and CR LF line ends keep the order.
    auto reordered = files;
    reordered.insert( "actions/10.json",
                      packcanonical::itemBytes( { { "id", 10 }, { "name", "Ten" } } ) );
    reordered.insert( "actions/order.txt", "gone\n" );
    CHECK( packcanonical::fromActions( packcanonical::toActions( reordered ) )
               .value( "actions/order.txt" )
           == "3\n7\n10\n" );
    reordered.insert( "actions/order.txt", "7\r\n3\r\n" );
    CHECK( packcanonical::fromActions( packcanonical::toActions( reordered ) )
               .value( "actions/order.txt" )
           == "7\n3\n10\n" );
    // An item's id is its store path's.
    reordered.insert( "actions/10.json",
                      packcanonical::itemBytes( { { "id", 11 }, { "name", "Ten" } } ) );
    CHECK( listed( QJsonDocument::fromJson( packcanonical::toActions( reordered ) ).object(),
                   "actions", 10 )
               .value( "name" )
           == "Ten" );
    // An item that is not a JSON object cannot be written.
    reordered.insert( "actions/10.json", "not json" );
    CHECK( packcanonical::toActions( reordered, &error ).isEmpty() );
    CHECK( error
           == "The pack's actions file could not be written: action 10 is not a JSON object." );

    // No actions and no responses is a valid, empty actions file; the version may be left out,
    // as CILogg reads it.
    error.clear();
    const auto none = packcanonical::fromActions( R"({ "actions": [], "responses": [] })", &error );
    CHECK( error.isEmpty() );
    CHECK( none
           == PackFiles{ { "actions/order.txt", QByteArray{} },
                         { "responses/order.txt", QByteArray{} } } );
    for ( const auto version : { 1, 3, 4 } ) {
        CHECK( !packcanonical::fromActions( actionsJson( [ version ]( QJsonObject& root ) {
                    root[ "version" ] = version;
                } ) )
                    .isEmpty() );
    }

    // A file that is not such an actions file is refused, never cleaned up quietly: an item's
    // identity is its id, a response runs actions of its own file, and anything else the file
    // holds (comments too) would be lost.
    const auto changed
        = []( const QString& list, int index, const QString& key, const QJsonValue& value ) {
              return actionsJson( [ & ]( QJsonObject& root ) {
                  auto items = root[ list ].toArray();
                  auto item = items.at( index ).toObject();
                  if ( value.isUndefined() ) {
                      item.remove( key );
                  }
                  else {
                      item[ key ] = value;
                  }
                  items[ index ] = item;
                  root[ list ] = items;
              } );
          };
    const auto linking = []( const QJsonObject& response ) {
        return actionsJson( [ &response ]( QJsonObject& root ) {
            auto responses = root[ "responses" ].toArray();
            auto item = responses.first().toObject();
            item[ "response" ] = response;
            responses[ 0 ] = item;
            root[ "responses" ] = responses;
        } );
    };
    const QVector<std::pair<const char*, QByteArray>> bad{
        { "not JSON", "{ [[[" },
        { "comments", QByteArray( "// the pack's actions\n" ) + SomeActions },
        { "not an object", "[]" },
        { "no responses", R"({ "version": 4, "actions": [] })" },
        { "actions not a list", R"({ "version": 4, "actions": {}, "responses": [] })" },
        { "version 2", actionsJson( []( QJsonObject& root ) { root[ "version" ] = 2; } ) },
        { "version as text", actionsJson( []( QJsonObject& root ) { root[ "version" ] = "4"; } ) },
        { "another key", actionsJson( []( QJsonObject& root ) { root[ "variables" ] = 1; } ) },
        { "an action that is not an object", actionsJson( []( QJsonObject& root ) {
              auto actions = root[ "actions" ].toArray();
              actions.append( 3 );
              root[ "actions" ] = actions;
          } ) },
        { "no id", changed( "actions", 0, "id", QJsonValue::Undefined ) },
        { "id not whole", changed( "actions", 0, "id", 1.5 ) },
        { "id as text", changed( "actions", 0, "id", "7" ) },
        { "negative id", changed( "responses", 0, "id", -1 ) },
        { "id too large", changed( "actions", 0, "id", 4294967296.0 ) },
        { "repeated action id", changed( "actions", 1, "id", 7 ) },
        { "no name", changed( "actions", 0, "name", QJsonValue::Undefined ) },
        { "blank name", changed( "responses", 0, "name", "  " ) },
        { "a response running an action of another file",
          linking( QJsonObject{ { "action_id", 9 } } ) },
        { "a step running an action of another file",
          linking( QJsonObject{ { "steps", QJsonArray{ QJsonObject{ { "action_id", 9 } } } } } ) },
        { "a link that is not a number", linking( QJsonObject{ { "action_id", "7" } } ) },
        // Only -1 (no action) is not an action of the file, and only as the response's own link:
        // CILogg drops a step that runs none, so the pack would change (Codex review of #176).
        { "a link below -1", linking( QJsonObject{ { "action_id", -2 }, { "comment", "x" } } ) },
        { "a step that runs no action",
          linking( QJsonObject{ { "steps", QJsonArray{ QJsonObject{ { "action_id", -1 } } } },
                                { "comment", "x" } } ) },
        // CILogg skips a step it cannot read, so the pack would lose it.
        { "a step without an action_id",
          linking( QJsonObject{ { "steps", QJsonArray{ QJsonObject{ { "delay_ms", 10 } } } },
                                { "comment", "x" } } ) },
        { "steps that are not a list",
          linking( QJsonObject{ { "steps", QJsonObject{} }, { "comment", "x" } } ) },
        { "a step naming another pack's action",
          linking( QJsonObject{
              { "steps", QJsonArray{ QJsonObject{ { "action_id", 7 },
                                                  { "pack_action", "b.pack/1" } } } } } ) },
    };
    for ( const auto& [ why, bytes ] : bad ) {
        INFO( why );
        QString refused;
        CHECK( packcanonical::fromActions( bytes, &refused ).isEmpty() );
        CHECK( refused.startsWith( "The pack's actions file is not valid" ) );
    }
    CHECK( [ & ] {
        QString refused;
        packcanonical::fromActions( linking( QJsonObject{ { "action_id", 9 } } ), &refused );
        return refused;
    }()
           == "The pack's actions file is not valid: response 3 runs action 9, which is not in "
              "the file." );
    // A response without an action of the file (-1) runs none.
    CHECK( !packcanonical::fromActions(
                linking( QJsonObject{ { "action_id", -1 }, { "comment", "seen" } } ) )
                .isEmpty() );
    // More actions or responses than a configuration import takes (4,096) is refused before it
    // becomes that many store items.
    {
        QJsonArray many;
        for ( int index = 0; index <= packcanonical::MaxPackActionItems; ++index ) {
            many.append( QJsonObject{ { "id", index }, { "name", "A" } } );
        }
        QString refused;
        CHECK(
            packcanonical::fromActions(
                QJsonDocument( QJsonObject{ { "actions", many }, { "responses", QJsonArray{} } } )
                    .toJson(),
                &refused )
                .isEmpty() );
        CHECK( refused.contains( "more than 4096 actions" ) );
    }

    // A pack folder's actions file becomes its items.
    QTemporaryDir pack;
    REQUIRE( pack.isValid() );
    auto manifest = packManifest();
    auto content = manifest[ "content" ].toObject();
    content.remove( "lua" );
    content[ "actions" ] = "serial/actions.json";
    manifest[ "content" ] = content;
    QDir().mkpath( pack.filePath( "serial" ) );
    const auto write = [ &pack ]( const QString& name, const QByteArray& bytes ) {
        QFile file( pack.filePath( name ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( bytes );
    };
    write( "previews.json", Previews );
    write( "serial/actions.json", SomeActions );
    error.clear();
    const auto folderFiles = packcanonical::fromPackFolder( manifest, pack.path(), &error );
    CHECK( error.isEmpty() );
    CHECK( folderFiles.contains( "actions/7.json" ) );
    CHECK( folderFiles.value( "responses/order.txt" ) == "3\n" );
    CHECK( !folderFiles.contains( "files/serial/actions.json" ) );
    CHECK( packcanonical::listedFiles( manifest ).isEmpty() );
    // A bad actions file in a pack folder is the pack's error.
    write( "serial/actions.json", R"({ "actions": [ { "name": "No id" } ], "responses": [] })" );
    CHECK( packcanonical::fromPackFolder( manifest, pack.path(), &error ).isEmpty() );
    CHECK( error.startsWith( "The pack's actions file is not valid" ) );
}

TEST_CASE( "Pack items merge item by item and field by field", "[packmerge][E16]" )
{
    using packmerge::mergeJson;
    const auto base = value( R"({"name":"P","regex":"a","format":"fields","color":"red"})" );
    const auto mineRegex = value( R"({"name":"P","regex":"mine","format":"fields","color":"red"})" );
    const auto theirsColor = value( R"({"name":"P","regex":"a","format":"fields","color":"blue"})" );
    const auto theirsRegex = value( R"({"name":"P","regex":"theirs","format":"fields","color":"red"})" );

    SECTION( "not edited locally: the new version" )
    {
        const auto merged = mergeJson( base, base, theirsColor, MergeStrategy::Fail );
        CHECK( merged.value == theirsColor );
        CHECK( merged.conflicts.isEmpty() );
    }
    SECTION( "edited only locally: the edit" )
    {
        CHECK( mergeJson( base, mineRegex, base, MergeStrategy::Fail ).value == mineRegex );
    }
    SECTION( "different fields: merged" )
    {
        const auto merged = mergeJson( base, mineRegex, theirsColor, MergeStrategy::Fail );
        CHECK( merged.conflicts.isEmpty() );
        CHECK( merged.value->toObject().value( "regex" ) == "mine" );
        CHECK( merged.value->toObject().value( "color" ) == "blue" );
    }
    SECTION( "the same field changed differently: a conflict" )
    {
        const auto merged = mergeJson( base, mineRegex, theirsRegex, MergeStrategy::Fail );
        CHECK( merged.conflicts == QStringList{ "regex" } );
        CHECK( merged.value->toObject().value( "regex" ) == "mine" );
        CHECK( mergeJson( base, mineRegex, theirsRegex, MergeStrategy::TakeTheirs )
                   .value->toObject()
                   .value( "regex" )
               == "theirs" );
    }
    SECTION( "the same new value on both sides is not a conflict" )
    {
        CHECK( mergeJson( base, mineRegex, mineRegex, MergeStrategy::Fail ).conflicts.isEmpty() );
    }
    SECTION( "deleted on one side, changed on the other: a conflict" )
    {
        CHECK( mergeJson( base, std::nullopt, theirsColor, MergeStrategy::Fail ).conflicts
               == QStringList{ QString{} } );
        CHECK( mergeJson( base, mineRegex, std::nullopt, MergeStrategy::Fail ).conflicts
               == QStringList{ QString{} } );
    }
    SECTION( "added and removed items" )
    {
        CHECK( mergeJson( std::nullopt, std::nullopt, theirsColor, MergeStrategy::Fail ).value
               == theirsColor );
        CHECK( !mergeJson( base, base, std::nullopt, MergeStrategy::Fail ).value );
    }
    SECTION( "named array elements merge one by one" )
    {
        const auto fieldsBase = value(
            R"({"fields":[{"name":"a","regex":"1"},{"name":"b","regex":"2"}]})" );
        const auto fieldsMine = value(
            R"({"fields":[{"name":"a","regex":"mine"},{"name":"b","regex":"2"}]})" );
        const auto fieldsTheirs = value(
            R"({"fields":[{"name":"a","regex":"1"},{"name":"b","regex":"new"},{"name":"c","regex":"3"}]})" );
        const auto merged = mergeJson( fieldsBase, fieldsMine, fieldsTheirs, MergeStrategy::Fail );
        CHECK( merged.conflicts.isEmpty() );
        const auto fields = merged.value->toObject().value( "fields" ).toArray();
        REQUIRE( fields.size() == 3 );
        CHECK( fields.at( 0 ).toObject().value( "regex" ) == "mine" );
        CHECK( fields.at( 1 ).toObject().value( "regex" ) == "new" );
        CHECK( fields.at( 2 ).toObject().value( "name" ) == "c" );
        // A conflict inside an element names the element.
        const auto clash = value( R"({"fields":[{"name":"a","regex":"other"},{"name":"b","regex":"2"}]})" );
        CHECK( mergeJson( fieldsBase, fieldsMine, clash, MergeStrategy::Fail ).conflicts
               == QStringList{ "fields/@a/regex" } );
    }
    SECTION( "field paths read and write values" )
    {
        const auto root = QJsonValue( object( R"({"fields":[{"name":"a/b","regex":"1"}],"x":2})" ) );
        const auto path = packmerge::fieldPath( packmerge::fieldPath( "fields", "a/b", true ), "regex" );
        CHECK( path == "fields/@a~1b/regex" );
        CHECK( packmerge::field( root, path ) == QJsonValue( "1" ) );
        const auto changed = packmerge::withField( root, path, QJsonValue( "9" ) );
        CHECK( packmerge::field( changed, path ) == QJsonValue( "9" ) );
        CHECK( !packmerge::field( packmerge::withField( root, "x", std::nullopt ), "x" ) );
    }
}

TEST_CASE( "Lua and Python files merge as text, like git merge-file", "[packmerge][E16]" )
{
    const QByteArray base = "a\nb\nc\nd\ne\n";
    const QByteArray mine = "a\nB\nc\nd\ne\n";
    const QByteArray theirs = "a\nb\nc\nd\nE\n";
    const auto clean = packmerge::mergeText( base, mine, theirs );
    CHECK( !clean.conflict );
    CHECK( clean.text == "a\nB\nc\nd\nE\n" );

    const QByteArray other = "a\nX\nc\nd\ne\n";
    const auto clash = packmerge::mergeText( base, mine, other );
    CHECK( clash.conflict );
    CHECK( clash.text.contains( "<<<<<<< your edit" ) );
    CHECK( clash.text.contains( "||||||| installed version" ) );
    CHECK( clash.text.contains( ">>>>>>> new version" ) );
    const auto keepMine = packmerge::mergeText( base, mine, other, MergeStrategy::KeepMine );
    CHECK( keepMine.text == mine );
    CHECK( keepMine.conflict );
    CHECK( packmerge::mergeText( base, mine, other, MergeStrategy::TakeTheirs ).text == other );

    // The same result as `git merge-file`, when Git is installed.
    const auto git = QStandardPaths::findExecutable( "git" );
    if ( git.isEmpty() ) {
        WARN( "git not found: the merge-file comparison is skipped" );
        return;
    }
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto write = [ &folder ]( const QString& name, const QByteArray& bytes ) {
        QFile file( folder.filePath( name ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( bytes );
        return file.fileName();
    };
    for ( const auto& [ ours, other2 ] : { std::pair{ mine, theirs }, std::pair{ mine, other } } ) {
        QProcess process;
        process.start( git, { "merge-file", "-p", "--diff3", "-L", "your edit", "-L",
                              "installed version", "-L", "new version", write( "mine", ours ),
                              write( "base", base ), write( "theirs", other2 ) } );
        REQUIRE( process.waitForFinished( 10000 ) );
        CHECK( packmerge::mergeText( base, ours, other2 ).text == process.readAllStandardOutput() );
    }
}

TEST_CASE( "Store files merge with the pack rules", "[packmerge][E16]" )
{
    const auto path = QStringLiteral( "previews/P.json" );
    const auto item = []( const char* json ) {
        return std::optional<QByteArray>( packcanonical::itemBytes( object( json ) ) );
    };
    const auto merged = packmerge::mergeFile( path, item( R"({"name":"P","regex":"a","x":1})" ),
                                              item( R"({"name":"P","regex":"b","x":1})" ),
                                              item( R"({"name":"P","regex":"c","x":2})" ) );
    REQUIRE( merged.conflicts.size() == 1 );
    const auto& conflict = merged.conflicts.first();
    CHECK( conflict.kind == "preview" );
    CHECK( conflict.field == "regex" );
    CHECK( conflict.mine == "\"b\"" );
    CHECK( conflict.theirs == "\"c\"" );
    CHECK( QJsonDocument::fromJson( *merged.result ).object().value( "x" ) == 2 );

    const auto text = packmerge::mergeFile( "files/lua/a.lua", QByteArray( "1\n" ),
                                            std::nullopt, QByteArray( "2\n" ) );
    REQUIRE( text.conflicts.size() == 1 );
    CHECK( !text.result );
}

TEST_CASE( "The pack store keeps versions, edits, history and merges", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PackStore store( root.path(), "org.example.pack" );
    CHECK( !store.exists() );
    const auto installed = store.install( packFiles(), packManifest(), "1.0.0", "Team packs" );
    INFO( installed.toStdString() );
    REQUIRE( installed.isEmpty() );
    REQUIRE( store.exists() );
    CHECK( store.files() == packFiles() );
    CHECK( store.localEdits().isEmpty() );
    const auto content = store.contentDirectory();
    REQUIRE( !content.isEmpty() );
    CHECK( QFileInfo::exists( content + "/plugin.json" ) );
    CHECK( readFile( content + "/lua/a.lua" ) == "-- a\n" );
    CHECK( QJsonDocument::fromJson( readFile( content + "/previews.json" ) )
               .object()
               .value( "previews" )
               .toArray()
               .size()
           == 2 );

    // A local edit is a commit on `local`; the content folder follows.
    const auto second = QStringLiteral( "previews/%53econd.json" );
    auto edited = QJsonDocument::fromJson( store.files().value( second ) ).object();
    edited[ "regex" ] = "^EDITED$";
    REQUIRE( store.commitEdit( second, packcanonical::itemBytes( edited ), "Edit preview 'Second'" )
                 .isEmpty() );
    CHECK( store.localEdits() == QStringList{ second } );
    CHECK( readFile( content + "/previews.json" ).contains( "^EDITED$" ) );
    REQUIRE( store.history( second ).size() == 2 );
    CHECK( store.history( second ).first().message == "Edit preview 'Second'" );
    CHECK( !store.history( second ).first().upstream );
    CHECK( store.history( second ).last().upstream );

    // A new version that changes another item and the Lua merges cleanly.
    auto next = packFiles( Previews, "-- a, version 2\n" );
    const auto first = QStringLiteral( "previews/%46irst%20%46rame.json" );
    auto changed = QJsonDocument::fromJson( next.value( first ) ).object();
    changed[ "regex" ] = "^A2$";
    next.insert( first, packcanonical::itemBytes( changed ) );
    const auto plan = store.planUpdate( next, MergeStrategy::Fail );
    CHECK( plan.conflicts.isEmpty() );
    REQUIRE( store.applyUpdate( next, plan.merged, packManifest( "1.1.0" ), "1.1.0", "Team packs" )
                 .isEmpty() );
    CHECK( store.upstreamFiles() == next );
    CHECK( store.files().value( second ).contains( "^EDITED$" ) );
    CHECK( store.files().value( first ).contains( "^A2$" ) );
    CHECK( readFile( content + "/lua/a.lua" ) == "-- a, version 2\n" );
    CHECK( store.localEdits() == QStringList{ second } );
    // Both branches moved together: the merge commit's second parent is the new version.
    CHECK( store.history().first().message == "Merge 1.1.0" );

    // Only commits of the pack's own history, and a whole earlier state only within this version.
    CHECK( store.restore( {}, "refs/heads/upstream", "Revert" )
           == "That version of the pack is not in its store." );
    CHECK( store.restore( {}, store.history().last().id, "Revert" ).contains( "another version" ) );
    // An install whose package then fails is put back: both branches and the content folder.
    const auto before = store.tips();
    const auto filesBefore = store.files();
    auto later = next;
    later.insert( packcanonical::filePath( "lua/a.lua" ), "-- a, version 3\n" );
    REQUIRE( store.applyUpdate( later, later, packManifest( "1.2.0" ), "1.2.0", "Team packs" )
                 .isEmpty() );
    CHECK( readFile( content + "/lua/a.lua" ) == "-- a, version 3\n" );
    REQUIRE( store.rollback( before ).isEmpty() );
    CHECK( store.tips().local == before.local );
    CHECK( store.tips().upstream == before.upstream );
    CHECK( store.files() == filesBefore );
    CHECK( readFile( content + "/lua/a.lua" ) == "-- a, version 2\n" );

    // Restoring what is already there adds nothing to the history.
    const auto commitsBefore = store.history().size();
    CHECK( store.restore( packcanonical::filePath( "lua/a.lua" ), {}, "Reset" ).isEmpty() );
    CHECK( store.history().size() == commitsBefore );

    // A version that changes the edited field conflicts.
    auto clash = next;
    auto theirs = QJsonDocument::fromJson( clash.value( second ) ).object();
    theirs[ "regex" ] = "^THEIRS$";
    clash.insert( second, packcanonical::itemBytes( theirs ) );
    const auto conflicted = store.planUpdate( clash, MergeStrategy::Fail );
    REQUIRE( conflicted.conflicts.size() == 1 );
    CHECK( conflicted.conflicts.first().item == "Second" );
    CHECK( conflicted.conflicts.first().field == "regex" );
    CHECK( store.planUpdate( clash, MergeStrategy::TakeTheirs ).merged.value( second ).contains( "^THEIRS$" ) );

    // Reset to Pack Version, then Revert to the edit.
    const auto editCommit = store.history( second ).first().id;
    REQUIRE( store.restore( second, {}, "Reset 'Second' to the pack version" ).isEmpty() );
    CHECK( store.localEdits().isEmpty() );
    REQUIRE( store.restore( second, editCommit, "Revert 'Second'" ).isEmpty() );
    CHECK( store.localEdits() == QStringList{ second } );

    // A removed item is an edit too.
    REQUIRE( store.commitEdit( first, std::nullopt, "Delete preview 'First Frame'" ).isEmpty() );
    CHECK( store.localEdits().contains( first ) );

    REQUIRE( store.remove().isEmpty() );
    CHECK( !QFileInfo::exists( root.filePath( "org.example.pack.git" ) ) );
    CHECK( !QFileInfo::exists( content ) );
}

TEST_CASE( "A damaged pack store is rebuilt with the local edits", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto second = QStringLiteral( "previews/%53econd.json" );
    {
        PackStore store( root.path(), "org.example.pack" );
        REQUIRE( store.install( packFiles(), packManifest(), "1.0.0", "Team packs" ).isEmpty() );
        auto edited = QJsonDocument::fromJson( store.files().value( second ) ).object();
        edited[ "regex" ] = "^EDITED$";
        REQUIRE( store.commitEdit( second, packcanonical::itemBytes( edited ), "Edit" ).isEmpty() );
        CHECK( !store.damaged() );
    }
    const auto repository = root.filePath( "org.example.pack.git" );
    SECTION( "a branch is missing" )
    {
        REQUIRE( QFile::remove( repository + "/refs/heads/local" ) );
    }
    SECTION( "the repository does not open" )
    {
        REQUIRE( QDir( repository + "/objects" ).removeRecursively() );
    }
    SECTION( "an object of a branch is missing" )
    {
        // Every object belongs to the tree of one branch, so any one missing breaks a read.
        QDirIterator objects( repository + "/objects", QDir::Files, QDirIterator::Subdirectories );
        REQUIRE( objects.hasNext() );
        const auto object = objects.next();
        QFile::setPermissions( object, QFile::ReadOwner | QFile::WriteOwner );
        REQUIRE( QFile::remove( object ) );
    }
    PackStore store( root.path(), "org.example.pack" );
    REQUIRE( store.damaged() );
    const auto rebuilt = store.rebuild( packFiles(), packManifest(), "1.0.0" );
    INFO( rebuilt.toStdString() );
    REQUIRE( rebuilt.isEmpty() );
    CHECK( !store.damaged() );
    CHECK( store.upstreamFiles() == packFiles() );
    CHECK( store.localEdits() == QStringList{ second } );
    CHECK( store.files().value( second ).contains( "^EDITED$" ) );
    CHECK( readFile( store.contentDirectory() + "/previews.json" ).contains( "^EDITED$" ) );
    // The damaged repository is kept aside, never deleted.
    CHECK( QDir( root.path() )
               .entryList( { "org.example.pack.git.damaged-*" }, QDir::Dirs | QDir::NoDotAndDotDot )
               .size()
           == 1 );
}

TEST_CASE( "An update that stopped part-way is put back by its journal", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto second = QStringLiteral( "previews/%53econd.json" );
    const auto journal = root.filePath( "org.example.pack.update.json" );
    PackStore store( root.path(), "org.example.pack" );
    REQUIRE( store.install( packFiles(), packManifest(), "1.0.0", "Team packs" ).isEmpty() );
    auto edited = QJsonDocument::fromJson( store.files().value( second ) ).object();
    edited[ "regex" ] = "^EDITED$";
    REQUIRE( store.commitEdit( second, packcanonical::itemBytes( edited ), "Edit" ).isEmpty() );
    const auto before = store.localTip();
    // A finished update leaves no journal, and nothing is put back.
    REQUIRE( store.beginUpdate( store.tips() ).isEmpty() );
    CHECK( QFileInfo::exists( journal ) );
    store.endUpdate();
    CHECK( !QFileInfo::exists( journal ) );
    REQUIRE( store.recoverInterruptedUpdate().isEmpty() );
    CHECK( store.localTip() == before );

    // The same version again with other content (a reinstall): another preview changes, the
    // merge keeps the edit. CILogg ends before the journal is removed.
    auto v2Previews = QJsonDocument::fromJson( Previews ).object();
    auto previews = v2Previews[ "previews" ].toArray();
    auto frame = previews[ 1 ].toObject();
    frame[ "regex" ] = "^A2$";
    previews[ 1 ] = frame;
    v2Previews[ "previews" ] = previews;
    const auto v2 = packFiles( QJsonDocument( v2Previews ).toJson() );
    const auto plan = store.planUpdate( v2, MergeStrategy::Fail );
    REQUIRE( plan.conflicts.isEmpty() );
    REQUIRE( store.beginUpdate( store.tips() ).isEmpty() );
    REQUIRE( store.applyUpdate( v2, plan.merged, packManifest( "1.0.0" ), "1.0.0", "Team packs" ).isEmpty() );
    REQUIRE( store.localTip() != before );

    SECTION( "CILogg ended before the package folder was replaced" )
    {
    }
    SECTION( "CILogg ended between moving upstream and local" )
    {
        QFile local( root.filePath( "org.example.pack.git/refs/heads/local" ) );
        REQUIRE( local.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        local.write( before.toLatin1() + '\n' );
    }
    PackStore recovered( root.path(), "org.example.pack" );
    REQUIRE( recovered.recoverInterruptedUpdate().isEmpty() );
    CHECK( !QFileInfo::exists( journal ) );
    CHECK( recovered.localTip() == before );
    CHECK( recovered.upstreamFiles() == packFiles() );
    CHECK( recovered.localEdits() == QStringList{ second } );
    CHECK( readFile( recovered.contentDirectory() + "/previews.json" ).contains( "^EDITED$" ) );
    CHECK( !readFile( recovered.contentDirectory() + "/previews.json" ).contains( "^A2$" ) );
}

TEST_CASE( "A file left in the staging folder is never published", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto second = QStringLiteral( "previews/%53econd.json" );
    PackStore store( root.path(), "org.example.pack" );
    REQUIRE( store.install( packFiles(), packManifest(), "1.0.0", "Team packs" ).isEmpty() );
    // An earlier write left a file in the staging folder, and something still holds it open
    // (on Windows it then cannot be removed; elsewhere it can).
    const auto stale = root.filePath( "org.example.pack/current.new/lua/stale.lua" );
    REQUIRE( QDir().mkpath( QFileInfo( stale ).absolutePath() ) );
    QFile held( stale );
    REQUIRE( held.open( QIODevice::WriteOnly ) );
    held.write( "-- stale\n" );
    held.flush();
    auto edited = QJsonDocument::fromJson( store.files().value( second ) ).object();
    edited[ "regex" ] = "^EDITED$";
    const auto error = store.commitEdit( second, packcanonical::itemBytes( edited ), "Edit" );
    INFO( error.toStdString() );
    // Refused while it cannot be cleared, or written into a cleared folder: never published.
    CHECK( !QFileInfo::exists( store.contentDirectory() + "/lua/stale.lua" ) );
    if ( error.isEmpty() ) {
        CHECK( readFile( store.contentDirectory() + "/previews.json" ).contains( "^EDITED$" ) );
    }
    else {
        CHECK( error.contains( "Cannot clear" ) );
    }
    held.close();
}

TEST_CASE( "A journal is acted on only when it is sure", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto second = QStringLiteral( "previews/%53econd.json" );
    const auto journal = root.filePath( "org.example.pack.update.json" );
    PackStore store( root.path(), "org.example.pack" );
    REQUIRE( store.install( packFiles(), packManifest(), "1.0.0", "Team packs" ).isEmpty() );
    auto edited = QJsonDocument::fromJson( store.files().value( second ) ).object();
    edited[ "regex" ] = "^EDITED$";
    REQUIRE( store.commitEdit( second, packcanonical::itemBytes( edited ), "Edit" ).isEmpty() );
    const auto tip = store.localTip();

    SECTION( "the package folder was replaced: only the journal goes" )
    {
        auto v2Previews = QJsonDocument::fromJson( Previews ).object();
        auto previews = v2Previews[ "previews" ].toArray();
        auto frame = previews[ 1 ].toObject();
        frame[ "regex" ] = "^A2$";
        previews[ 1 ] = frame;
        v2Previews[ "previews" ] = previews;
        const auto v2 = packFiles( QJsonDocument( v2Previews ).toJson() );
        REQUIRE( store.beginUpdate( store.tips() ).isEmpty() );
        REQUIRE( store.applyUpdate( v2, store.planUpdate( v2, MergeStrategy::Fail ).merged,
                                    packManifest( "1.1.0" ), "1.1.0", "Team packs" )
                     .isEmpty() );
        const auto after = store.localTip();
        // What the installed folder holds is the new version: the update finished.
        REQUIRE( store.recoverInterruptedUpdate( &v2, packManifest( "1.1.0" ) ).isEmpty() );
        CHECK( !QFileInfo::exists( journal ) );
        CHECK( store.localTip() == after );
    }
    SECTION( "an undo that stopped part-way is done again, not taken as a finished update" )
    {
        auto v2Previews = QJsonDocument::fromJson( Previews ).object();
        auto previews = v2Previews[ "previews" ].toArray();
        auto frame = previews[ 1 ].toObject();
        frame[ "regex" ] = "^A2$";
        previews[ 1 ] = frame;
        v2Previews[ "previews" ] = previews;
        const auto v2 = packFiles( QJsonDocument( v2Previews ).toJson() );
        const auto before = store.tips();
        REQUIRE( store.beginUpdate( before ).isEmpty() );
        REQUIRE( store.applyUpdate( v2, store.planUpdate( v2, MergeStrategy::Fail ).merged,
                                    packManifest( "1.1.0" ), "1.1.0", "Team packs" )
                     .isEmpty() );
        REQUIRE( store.localTip() != tip );
        // The package was not replaced, and the undo put `upstream` back but not `local`: the
        // installed folder (the old version) matches `upstream`, yet the store is not as it was.
        {
            QFile upstream( root.filePath( "org.example.pack.git/refs/heads/upstream" ) );
            REQUIRE( upstream.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
            upstream.write( before.upstream.toLatin1() + '\n' );
        }
        PackStore recovered( root.path(), "org.example.pack" );
        const auto installed = packFiles();
        REQUIRE( recovered.recoverInterruptedUpdate( &installed, packManifest() ).isEmpty() );
        CHECK( !QFileInfo::exists( journal ) );
        CHECK( recovered.localTip() == tip );
        CHECK( recovered.tips().upstream == before.upstream );
        CHECK( readFile( recovered.contentDirectory() + "/previews.json" ).contains( "^EDITED$" ) );
        CHECK( !readFile( recovered.contentDirectory() + "/previews.json" ).contains( "^A2$" ) );
    }
    SECTION( "a journal without its tips is never taken as \"there was no store\"" )
    {
        // Only `existed: false`: acting on it would remove the store with its edits.
        QFile file( journal );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( QJsonDocument( QJsonObject{ { "existed", false } } ).toJson() );
        file.close();
        CHECK( store.recoverInterruptedUpdate().contains( "cannot be read" ) );
        CHECK( store.hasRepository() );
        CHECK( store.localTip() == tip );
        CHECK( readFile( store.contentDirectory() + "/previews.json" ).contains( "^EDITED$" ) );
    }
    SECTION( "a journal that cannot be read changes nothing" )
    {
        // No "existed": never taken as "there was no store", which would remove it.
        QFile file( journal );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( QJsonDocument( QJsonObject{ { "upstream", tip }, { "local", tip } } ).toJson() );
        file.close();
        CHECK( store.recoverInterruptedUpdate().contains( "cannot be read" ) );
        CHECK( QFileInfo::exists( journal ) );
        CHECK( store.localTip() == tip );
        CHECK( readFile( store.contentDirectory() + "/previews.json" ).contains( "^EDITED$" ) );
    }
    SECTION( "a repository that does not open is never taken as missing" )
    {
        PackStore other( root.path(), "org.example.other" );
        REQUIRE( QDir().mkpath( root.filePath( "org.example.other.git" ) ) );
        const auto tips = other.tips();
        CHECK( tips.existed );
        CHECK( tips.upstream.isEmpty() );
        CHECK( !other.rollback( tips ).isEmpty() );
        CHECK( QFileInfo::exists( root.filePath( "org.example.other.git" ) ) );
    }
}

TEST_CASE( "A store left by an earlier install does not come back into a new one",
           "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto second = QStringLiteral( "previews/%53econd.json" );
    {
        // What a removal that could not finish leaves: a store with an edit, no installed pack.
        PackStore store( root.filePath( "data/pack-store" ), "org.example.pack" );
        REQUIRE( store.install( packFiles(), packManifest(), "1.0.0", "Team packs" ).isEmpty() );
        auto edited = QJsonDocument::fromJson( store.files().value( second ) ).object();
        edited[ "regex" ] = "^LEFTOVER$";
        REQUIRE( store.commitEdit( second, packcanonical::itemBytes( edited ), "Edit" ).isEmpty() );
    }
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto v1 = writePackage( root.filePath( "v1" ), "1.0.0", Previews, "-- a\n" );
    const auto job = finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) );
    INFO( job[ "error" ].toString().toStdString() );
    REQUIRE( job[ "state" ] == "completed" );
    CHECK( manager.manage( "pack_edits", "org.example.pack", {} ).payload[ "files" ].toList().isEmpty() );
    CHECK( previewRegex( manager.packContentDirectory( "org.example.pack" ), "Second" ) == "^B$" );
    // Kept aside, never deleted.
    CHECK( QDir( root.filePath( "data/pack-store" ) )
               .entryList( { "org.example.pack.git.orphaned-*" }, QDir::Dirs | QDir::NoDotAndDotDot )
               .size()
           == 1 );
}

TEST_CASE( "A content folder that missed a commit is written again", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto second = QStringLiteral( "previews/%53econd.json" );
    PackStore store( root.path(), "org.example.pack" );
    REQUIRE( store.install( packFiles(), packManifest(), "1.0.0", "Team packs" ).isEmpty() );
    auto edited = QJsonDocument::fromJson( store.files().value( second ) ).object();
    edited[ "regex" ] = "^EDITED$";
    REQUIRE( store.commitEdit( second, packcanonical::itemBytes( edited ), "Edit" ).isEmpty() );
    const auto previews = store.contentDirectory() + "/previews.json";
    REQUIRE( readFile( previews ).contains( "^EDITED$" ) );
    const auto overwrite = [ &previews ]( const QByteArray& bytes ) {
        QFile file( previews );
        REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        file.write( bytes );
    };
    // A folder that holds the recorded commit is left alone.
    overwrite( "{}" );
    REQUIRE( store.repairContent().isEmpty() );
    CHECK( readFile( previews ) == "{}" );
    // One left behind by a replace that failed after a commit (it holds an older commit) is
    // written again from the store.
    QFile marker( root.filePath( "org.example.pack/current.revision" ) );
    REQUIRE( marker.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    marker.write( "0000000000000000000000000000000000000000" );
    marker.close();
    REQUIRE( store.repairContent().isEmpty() );
    CHECK( readFile( previews ).contains( "^EDITED$" ) );
}

TEST_CASE( "A damaged pack store whose edits cannot be read is left as it is", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto second = QStringLiteral( "previews/%53econd.json" );
    {
        PackStore store( root.path(), "org.example.pack" );
        REQUIRE( store.install( packFiles(), packManifest(), "1.0.0", "Team packs" ).isEmpty() );
        auto edited = QJsonDocument::fromJson( store.files().value( second ) ).object();
        edited[ "regex" ] = "^EDITED$";
        REQUIRE( store.commitEdit( second, packcanonical::itemBytes( edited ), "Edit" ).isEmpty() );
    }
    const auto repository = root.filePath( "org.example.pack.git" );
    REQUIRE( QFile::remove( repository + "/refs/heads/local" ) );
    PackStore store( root.path(), "org.example.pack" );
    REQUIRE( store.damaged() );
    const auto content = store.contentDirectory();
    // A file the manifest lists is gone from the content folder: its edits cannot be read whole.
    REQUIRE( QFile::remove( content + "/lua/a.lua" ) );
    const auto refused = store.rebuild( packFiles(), packManifest(), "1.0.0" );
    CHECK( refused.contains( "cannot be read" ) );
    // Nothing moved: the edits stay on disk, the repository is not set aside.
    CHECK( readFile( content + "/previews.json" ).contains( "^EDITED$" ) );
    CHECK( QFileInfo::exists( repository ) );
    CHECK( QDir( root.path() )
               .entryList( { "org.example.pack.git.damaged-*" }, QDir::Dirs | QDir::NoDotAndDotDot )
               .isEmpty() );
}

TEST_CASE( "A pack store works under a data folder too deep for libgit2", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    // Over 200 characters: libgit2 refuses repository paths that leave it under 68 characters
    // of MAX_PATH on Windows; elsewhere the repository simply stays in the data folder.
    auto deep = root.path();
    while ( deep.size() < 210 ) {
        deep += QStringLiteral( "/deep-data-folder" );
    }
    REQUIRE( QDir().mkpath( deep ) );
    PackStore store( deep, "org.example.pack" );
    const auto installed = store.install( packFiles(), packManifest(), "1.0.0", "Team packs" );
    INFO( installed.toStdString() );
    REQUIRE( installed.isEmpty() );
    CHECK( store.files() == packFiles() );
    CHECK( store.contentDirectory().startsWith( deep ) );
#ifdef Q_OS_WIN
    CHECK( !QFileInfo::exists( deep + "/org.example.pack.git" ) );
#endif
    REQUIRE( store.remove().isEmpty() );
    CHECK( !store.hasRepository() );
}

TEST_CASE( "An update that drops an edited file says so and keeps the pack valid",
           "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    const auto v1 = writePackage( root.filePath( "v1" ), "1.0.0", Previews, "-- a\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    const auto lua = packcanonical::filePath( "lua/a.lua" );
    REQUIRE( manager.manage( "pack_edit", id, { { "path", lua }, { "content", "-- mine\n" } } ).ok() );

    // Version 2.0.0 has no Lua at all.
    auto manifest = packManifest( "2.0.0" );
    manifest[ "content" ] = QJsonObject{ { "previews", "previews.json" } };
    const auto v2 = root.filePath( "v2" );
    REQUIRE( QDir().mkpath( v2 ) );
    for ( const auto& [ name, bytes ] :
          { std::pair{ QStringLiteral( "plugin.json" ), QJsonDocument( manifest ).toJson() },
            std::pair{ QStringLiteral( "previews.json" ), QByteArray( Previews ) } } ) {
        QFile file( v2 + '/' + name );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( bytes );
    }
    const auto job = finished( manager, manager.manage( "install", {}, { { "path", v2 } } ) );
    REQUIRE( job[ "state" ] == "completed" );
    CHECK( job[ "result" ].toMap()[ "droppedEdits" ].toStringList() == QStringList{ "lua/a.lua" } );
    CHECK( job[ "result" ].toMap()[ "warning" ].toString().contains( "lua/a.lua" ) );
    const auto content = manager.packContentDirectory( id );
    CHECK( !QFileInfo::exists( content + "/lua/a.lua" ) );
    const auto files = manager.manage( "pack_files", id, {} ).payload[ "files" ].toList();
    CHECK( std::none_of( files.cbegin(), files.cend(), []( const QVariant& file ) {
        return file.toMap()[ "kind" ] == "file";
    } ) );
}

TEST_CASE( "Pack checks, encodings, revisions and removal follow the effective content",
           "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    // A Lua file in Latin-1 (valid Lua, not UTF-8).
    const auto v1 = writePackage( root.filePath( "v1" ), "1.0.0", Previews, "-- caf\xe9\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    const auto lua = packcanonical::filePath( "lua/a.lua" );
    const auto second = QStringLiteral( "previews/%53econd.json" );

    // Editing it as text would change its bytes: refused.
    CHECK( manager.manage( "pack_read", id, { { "path", lua } } ).error.contains( "not UTF-8" ) );

    // Every edit is a new revision of the content, also within one file-time tick.
    const auto revision = manager.packRevision( id );
    CHECK( !revision.isEmpty() );
    auto item = QJsonDocument::fromJson( manager.manage( "pack_read", id, { { "path", second } } )
                                             .payload[ "content" ]
                                             .toString()
                                             .toUtf8() )
                    .object();
    item[ "regex" ] = "^MINE$";
    REQUIRE( manager.manage( "pack_edit", id,
                             { { "path", second },
                               { "content", QString::fromUtf8( QJsonDocument( item ).toJson() ) } } )
                 .ok() );
    CHECK( manager.packRevision( id ) != revision );

    // Enabling checks the content as edited, and an enabled pack's edits pass the same check.
    QStringList checked;
    QString refusal;
    manager.setPackCheck( [ &checked, &refusal ]( const QString&, const QString& directory,
                                                  const QJsonObject& ) {
        checked << directory;
        return refusal;
    } );
    REQUIRE( manager.manage( "enable", id, {} ).ok() );
    REQUIRE( !checked.isEmpty() );
    CHECK( checked.last() == manager.packContentDirectory( id ) );
    refusal = QStringLiteral( "Refused by the pack check." );
    item[ "regex" ] = "^AGAIN$";
    CHECK( manager.manage( "pack_edit", id,
                           { { "path", second },
                             { "content", QString::fromUtf8( QJsonDocument( item ).toJson() ) } } )
               .error
           == refusal );
    CHECK( previewRegex( manager.packContentDirectory( id ), "Second" ) == "^MINE$" );
    refusal.clear();
    REQUIRE( manager.manage( "disable", id, {} ).ok() );

    // A new version that changes the Latin-1 file while yours changed it too: a conflict that
    // only a whole side resolves, byte for byte.
    REQUIRE( manager.manage( "pack_edit", id, { { "path", lua }, { "content", "-- mine\n" } } ).ok() );
    const auto v2 = writePackage( root.filePath( "v2" ), "1.1.0", Previews, "-- caf\xe9, 2\n" );
    const auto conflicted = finished( manager, manager.manage( "install", {}, { { "path", v2 } } ) );
    REQUIRE( conflicted[ "state" ] == "failed" );
    CHECK( !conflicted[ "result" ].toMap()[ "items" ].toMap()[ lua ].toMap()[ "utf8" ].toBool() );
    const auto shown = conflicted[ "result" ].toMap()[ "revision" ].toString();
    const auto resolve = [ &manager, &v2, &shown ]( const QVariantMap& resolution ) {
        return finished( manager, manager.manage( "install", {},
                                                  { { "path", v2 },
                                                    { "resolutions", QVariantList{ resolution } },
                                                    { "revision", shown } } ) );
    };
    // Resolutions are applied only to the edits they were chosen for: without the revision of
    // the failed update they are refused.
    const auto unbound = finished(
        manager, manager.manage( "install", {},
                                 { { "path", v2 },
                                   { "resolutions",
                                     QVariantList{ QVariantMap{ { "path", lua }, { "take", "theirs" } } } } } ) );
    CHECK( unbound[ "state" ] == "failed" );
    CHECK( unbound[ "error" ].toString().contains( "need the revision" ) );
    const auto asText = resolve( { { "path", lua }, { "text", "x\n" } } );
    CHECK( asText[ "state" ] == "failed" );
    // A file the new version's manifest lists cannot be removed by a resolution: refused, not
    // put back behind the user's back.
    const auto removed = resolve( { { "path", lua }, { "remove", true } } );
    CHECK( removed[ "state" ] == "failed" );
    CHECK( removed[ "error" ].toString().contains( "Invalid merge resolution" ) );
    const auto whole = resolve( { { "path", lua }, { "take", "theirs" } } );
    REQUIRE( whole[ "state" ] == "completed" );
    CHECK( readFile( manager.packContentDirectory( id ) + "/lua/a.lua" ) == "-- caf\xe9, 2\n" );

    // Removing the pack removes its content folder too when the repository is gone.
    const auto contentFolder = root.filePath( "data/pack-store/org.example.pack" );
    REQUIRE( QDir( root.filePath( "data/pack-store/org.example.pack.git" ) ).removeRecursively() );
    REQUIRE( QFileInfo::exists( contentFolder ) );
    REQUIRE( manager.manage( "remove", id, {} ).ok() );
    CHECK( !QFileInfo::exists( contentFolder ) );
}

TEST_CASE( "A merge's job carries each file's texts once, within a budget", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    // Two large Lua files whose first line both the user and the new version change.
    auto manifest = packManifest();
    auto body = manifest[ "content" ].toObject();
    body[ "lua" ] = QJsonObject{ { "folder", "lua" }, { "load", QJsonArray{ "a.lua", "b.lua" } } };
    manifest[ "content" ] = body;
    const QByteArray filler = QByteArray( 900 * 1024, 'x' ) + '\n';
    const auto package = [ &manifest, &filler ]( const QString& folder, const QString& version,
                                                 const QByteArray& head ) {
        auto json = manifest;
        json[ "version" ] = version;
        REQUIRE( QDir().mkpath( folder + "/lua" ) );
        for ( const auto& [ name, bytes ] :
              { std::pair{ QStringLiteral( "plugin.json" ), QJsonDocument( json ).toJson() },
                std::pair{ QStringLiteral( "previews.json" ), QByteArray( Previews ) },
                std::pair{ QStringLiteral( "lua/a.lua" ), head + filler },
                std::pair{ QStringLiteral( "lua/b.lua" ), head + filler } } ) {
            QFile file( folder + '/' + name );
            REQUIRE( file.open( QIODevice::WriteOnly ) );
            file.write( bytes );
        }
        return folder;
    };
    const auto v1 = package( root.filePath( "v1" ), "1.0.0", "-- base\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    const auto a = packcanonical::filePath( "lua/a.lua" );
    const auto b = packcanonical::filePath( "lua/b.lua" );
    for ( const auto& path : { a, b } ) {
        REQUIRE( manager.manage( "pack_edit", id,
                                 { { "path", path }, { "content", QString::fromUtf8( "-- mine\n" + filler ) } } )
                     .ok() );
    }
    const auto v2 = package( root.filePath( "v2" ), "1.1.0", "-- theirs\n" );
    const auto job = finished( manager, manager.manage( "install", {}, { { "path", v2 } } ) );
    REQUIRE( job[ "state" ] == "failed" );
    const auto result = job[ "result" ].toMap();
    REQUIRE( result[ "needsMerge" ].toBool() );
    const auto items = result[ "items" ].toMap();
    // The first file fits the budget with its base (about 2.7 MiB); the second is only listed.
    CHECK( items[ a ].toMap()[ "mine" ].toString().startsWith( "-- mine\n" ) );
    CHECK_FALSE( items[ a ].toMap().contains( "omitted" ) );
    CHECK( items[ b ].toMap()[ "omitted" ].toBool() );
    CHECK_FALSE( items[ b ].toMap().contains( "mine" ) );
    const auto conflicts = result[ "conflicts" ].toList();
    REQUIRE( conflicts.size() == 2 );
    for ( const auto& value : conflicts ) {
        // A file's sides are kept once, in `items`; its conflict has the base when shown.
        const auto conflict = value.toMap();
        CHECK_FALSE( conflict.contains( "mine" ) );
        CHECK( conflict.contains( "base" ) == ( conflict[ "path" ] == a ) );
    }
    // A file too large to show is still resolved, as a whole side.
    const QVariantList resolutions{ QVariantMap{ { "path", a }, { "take", "theirs" } },
                                    QVariantMap{ { "path", b }, { "take", "mine" } } };
    const auto resolved = finished(
        manager, manager.manage( "install", {},
                                 { { "path", v2 },
                                   { "resolutions", resolutions },
                                   { "revision", result[ "revision" ] } } ) );
    REQUIRE( resolved[ "state" ] == "completed" );
    const auto content = manager.packContentDirectory( id );
    CHECK( readFile( content + "/lua/a.lua" ).startsWith( "-- theirs\n" ) );
    CHECK( readFile( content + "/lua/b.lua" ).startsWith( "-- mine\n" ) );
}

TEST_CASE( "Many conflicting fields are one conflict, and stale choices are not applied",
           "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    const auto second = QStringLiteral( "previews/%53econd.json" );
    // "Second" with 40 settings that the user and the new version both change.
    const auto previewsWith = []( const QString& value ) {
        auto document = QJsonDocument::fromJson( Previews ).object();
        auto previews = document[ "previews" ].toArray();
        auto item = previews[ 0 ].toObject();
        for ( int index = 0; index < 40; ++index ) {
            item[ QStringLiteral( "p%1" ).arg( index ) ] = value;
        }
        previews[ 0 ] = item;
        document[ "previews" ] = previews;
        return QJsonDocument( document ).toJson();
    };
    const auto v1 = writePackage( root.filePath( "v1" ), "1.0.0", previewsWith( "0" ), "-- a\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    const auto edit = [ &manager, &id, &second ]( const QString& value ) {
        auto item = QJsonDocument::fromJson(
                        manager.manage( "pack_read", id, { { "path", second } } ).payload[ "content" ].toString().toUtf8() )
                        .object();
        for ( int index = 0; index < 40; ++index ) {
            item[ QStringLiteral( "p%1" ).arg( index ) ] = value;
        }
        REQUIRE( manager.manage( "pack_edit", id,
                                 { { "path", second },
                                   { "content", QString::fromUtf8( QJsonDocument( item ).toJson() ) } } )
                     .ok() );
    };
    edit( "mine" );
    const auto v2 = writePackage( root.filePath( "v2" ), "1.1.0", previewsWith( "theirs" ), "-- a\n" );
    const auto job = finished( manager, manager.manage( "install", {}, { { "path", v2 } } ) );
    REQUIRE( job[ "state" ] == "failed" );
    const auto result = job[ "result" ].toMap();
    const auto conflicts = result[ "conflicts" ].toList();
    // 40 conflicting fields of one preview are listed as the preview as a whole.
    REQUIRE( conflicts.size() == 1 );
    const auto whole = conflicts.first().toMap();
    CHECK( whole[ "path" ] == second );
    CHECK( whole[ "field" ].toString().isEmpty() );
    // The item's sides are carried once, in `items`; the conflict has the base.
    CHECK_FALSE( whole.contains( "mine" ) );
    CHECK( whole.contains( "base" ) );
    const auto sides = result[ "items" ].toMap()[ second ].toMap();
    CHECK( sides[ "mine" ].toString().contains( "\"mine\"" ) );
    CHECK( sides[ "theirs" ].toString().contains( "\"theirs\"" ) );
    const auto revision = result[ "revision" ].toString();
    REQUIRE( !revision.isEmpty() );
    const QVariantList takeTheirs{ QVariantMap{
        { "path", second },
        { "field", QString{} },
        { "value", QJsonDocument::fromJson( sides[ "theirs" ].toString().toUtf8() ).object().toVariantMap() } } };

    // A field path deeper than any item's (automation could send thousands of segments, which
    // would be walked level by level) is refused, and nothing is installed.
    {
        QString deep = QStringLiteral( "p0" );
        for ( int level = 0; level < 200; ++level ) {
            deep += QStringLiteral( "/x" );
        }
        const auto refused = finished(
            manager, manager.manage( "install", {},
                                     { { "path", v2 },
                                       { "strategy", "keep_mine" },
                                       { "resolutions",
                                         QVariantList{ QVariantMap{
                                             { "path", second }, { "field", deep }, { "value", 1 } } } },
                                       { "revision", revision } } ) );
        CHECK( refused[ "state" ] == "failed" );
        CHECK( refused[ "error" ].toString().startsWith( "Invalid merge resolution" ) );
        CHECK( manager.plugins().first().toMap()[ "version" ] == "1.0.0" );
    }

    // Edited again after the merge was shown: the choices are not applied, the merge is asked
    // for again with the new revision.
    edit( "newer" );
    const auto stale = finished( manager, manager.manage( "install", {},
                                                           { { "path", v2 },
                                                             { "resolutions", takeTheirs },
                                                             { "revision", revision } } ) );
    REQUIRE( stale[ "state" ] == "failed" );
    CHECK( stale[ "error" ].toString().contains( "changed after the merge was shown" ) );
    CHECK( stale[ "result" ].toMap()[ "needsMerge" ].toBool() );
    const auto current = stale[ "result" ].toMap()[ "revision" ].toString();
    CHECK( current != revision );
    const auto content = manager.packContentDirectory( id );
    CHECK( readFile( content + "/previews.json" ).contains( "\"newer\"" ) );

    // The same edits but another package (a local folder changed meanwhile): not applied either.
    const auto v3 = writePackage( root.filePath( "v3" ), "1.1.0", previewsWith( "third" ), "-- a\n" );
    const auto otherPackage = finished( manager, manager.manage( "install", {},
                                                                  { { "path", v3 },
                                                                    { "resolutions", takeTheirs },
                                                                    { "revision", current } } ) );
    REQUIRE( otherPackage[ "state" ] == "failed" );
    CHECK( otherPackage[ "error" ].toString().contains( "changed after the merge was shown" ) );
    CHECK( readFile( content + "/previews.json" ).contains( "\"newer\"" ) );
    // Also when the other package would merge without a conflict (it has your "newer" too):
    // nothing is installed on choices made for another package.
    const auto v4 = writePackage( root.filePath( "v4" ), "1.2.0", previewsWith( "newer" ), "-- a\n" );
    const auto clean = finished( manager, manager.manage( "install", {},
                                                           { { "path", v4 },
                                                             { "resolutions", takeTheirs },
                                                             { "revision", current } } ) );
    REQUIRE( clean[ "state" ] == "failed" );
    CHECK( clean[ "error" ].toString().contains( "nothing was installed" ) );
    CHECK_FALSE( clean[ "result" ].toMap()[ "needsMerge" ].toBool() );
    CHECK( manager.plugins().first().toMap()[ "version" ] == "1.0.0" );

    // With the current revision a whole side settles all 40 fields too (`take`, as for an item
    // too large to show).
    const auto applied = finished(
        manager, manager.manage( "install", {},
                                 { { "path", v2 },
                                   { "resolutions",
                                     QVariantList{ QVariantMap{ { "path", second }, { "take", "theirs" } } } },
                                   { "revision", current } } ) );
    INFO( applied[ "error" ].toString().toStdString() );
    REQUIRE( applied[ "state" ] == "completed" );
    CHECK( readFile( content + "/previews.json" ).contains( "\"theirs\"" ) );
    CHECK( !readFile( content + "/previews.json" ).contains( "\"newer\"" ) );

    // The previews document as written (one member per line) stays within 1 MiB: two items of
    // 600 KB and 500 KB each fit alone, together they are refused.
    const auto padded = [ &manager, &id ]( const QString& path, int size ) {
        auto item = QJsonDocument::fromJson( manager.manage( "pack_read", id, { { "path", path } } )
                                                 .payload[ "content" ]
                                                 .toString()
                                                 .toUtf8() )
                        .object();
        item[ "padding" ] = QString( size, 'x' );
        return manager.manage( "pack_edit", id,
                               { { "path", path },
                                 { "content", QString::fromUtf8( QJsonDocument( item ).toJson() ) } } );
    };
    REQUIRE( padded( "previews/%46irst%20%46rame.json", 600 * 1000 ).ok() );
    CHECK( padded( second, 500 * 1000 ).error.contains( "larger than 1 MiB" ) );
}

TEST_CASE( "A large item merges fast and lists few conflicts", "[packmerge][E16]" )
{
    // 20,000 named fields under a long key, each changed differently on both sides.
    const QString longKey( 4000, 'k' );
    QJsonArray base;
    QJsonArray mine;
    QJsonArray theirs;
    for ( int index = 0; index < 20000; ++index ) {
        const auto name = QStringLiteral( "f%1" ).arg( index );
        base.append( QJsonObject{ { "name", name }, { "v", 0 } } );
        mine.append( QJsonObject{ { "name", name }, { "v", 1 } } );
        theirs.append( QJsonObject{ { "name", name }, { "v", 2 } } );
    }
    const auto item = [ &longKey ]( const QJsonArray& fields ) {
        return QJsonValue( QJsonObject{ { "name", "P" }, { longKey, QJsonObject{ { "fields", fields } } } } );
    };
    QElapsedTimer timer;
    timer.start();
    const auto merged = packmerge::mergeJson( item( base ), item( mine ), item( theirs ),
                                              MergeStrategy::Fail );
    CHECK( timer.elapsed() < 10000 );
    // Past the fields a merge keeps, the item is one conflict, as a job lists it.
    CHECK( merged.conflicts == QStringList{ QString{} } );
    REQUIRE( merged.value );
    const auto fields
        = merged.value->toObject().value( longKey ).toObject().value( "fields" ).toArray();
    REQUIRE( fields.size() == 20000 );
    CHECK( fields.last().toObject().value( "v" ).toInt() == 1 ); // yours where both changed
    // A few conflicts are listed field by field.
    const auto few = packmerge::mergeJson( QJsonValue( object( R"({"a":1,"b":1})" ) ),
                                           QJsonValue( object( R"({"a":2,"b":2})" ) ),
                                           QJsonValue( object( R"({"a":3,"b":3})" ) ),
                                           MergeStrategy::Fail );
    CHECK( few.conflicts == QStringList{ "a", "b" } );
}

TEST_CASE( "Resolutions keep an item's name and its bytes", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    const auto second = QStringLiteral( "previews/%53econd.json" );
    const auto firstFrame = QStringLiteral( "previews/%46irst%20%46rame.json" );
    const auto lua = packcanonical::filePath( "lua/a.lua" );
    // Version 1 has a Lua file in Latin-1.
    const auto v1 = writePackage( root.filePath( "v1" ), "1.0.0", Previews, "-- caf\xe9\n" );
    // A first install merges nothing: the choices of a merge are refused, not ignored.
    const auto first = finished(
        manager, manager.manage( "install", {},
                                 { { "path", v1 },
                                   { "resolutions", QVariantList{ QVariantMap{ { "path", second }, { "take", "mine" } } } },
                                   { "revision", "stale" } } ) );
    CHECK( first[ "state" ] == "failed" );
    CHECK( first[ "error" ].toString().contains( "no local edits to merge" ) );
    // Given at all, an empty list too.
    const auto empty = finished( manager, manager.manage( "install", {},
                                                           { { "path", v1 }, { "resolutions", QVariantList{} } } ) );
    CHECK( empty[ "state" ] == "failed" );
    CHECK( empty[ "error" ].toString().contains( "no local edits to merge" ) );
    CHECK( manager.plugins().isEmpty() );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    // Yours: "First Frame" deleted, "Second" and the Lua file (now UTF-8) changed.
    REQUIRE( manager.manage( "pack_edit", id, { { "path", firstFrame }, { "remove", true } } ).ok() );
    auto item = QJsonDocument::fromJson( Previews ).object()[ "previews" ].toArray()[ 0 ].toObject();
    item[ "regex" ] = "^MINE$";
    REQUIRE( manager.manage( "pack_edit", id,
                             { { "path", second },
                               { "content", QString::fromUtf8( QJsonDocument( item ).toJson() ) } } )
                 .ok() );
    REQUIRE( manager.manage( "pack_edit", id, { { "path", lua }, { "content", "-- mine\n" } } ).ok() );
    // Version 2 removes "First Frame" too and changes "Second" and the Lua file otherwise.
    auto document = QJsonDocument::fromJson( Previews ).object();
    auto theirs = document[ "previews" ].toArray()[ 0 ].toObject();
    theirs[ "regex" ] = "^THEIRS$";
    document[ "previews" ] = QJsonArray{ theirs };
    const auto v2 = writePackage( root.filePath( "v2" ), "1.1.0", QJsonDocument( document ).toJson(),
                                  "-- theirs\n" );
    const auto asked = finished( manager, manager.manage( "install", {}, { { "path", v2 } } ) );
    REQUIRE( asked[ "state" ] == "failed" );
    REQUIRE( asked[ "result" ].toMap()[ "needsMerge" ].toBool() );
    // The Lua file's base is not UTF-8: listed so, and resolved only as a whole side.
    CHECK_FALSE( asked[ "result" ].toMap()[ "items" ].toMap()[ lua ].toMap()[ "utf8" ].toBool() );
    const auto revision = asked[ "result" ].toMap()[ "revision" ].toString();
    const QVariantMap secondChoice{ { "path", second }, { "field", "regex" }, { "value", "^B$" } };
    const auto attempt = [ &manager, &v2, &revision ]( const QVariantList& resolutions ) {
        return finished( manager, manager.manage( "install", {},
                                                  { { "path", v2 },
                                                    { "resolutions", resolutions },
                                                    { "revision", revision } } ) );
    };
    const auto text = attempt( { secondChoice, QVariantMap{ { "path", lua }, { "text", "-- merged\n" } } } );
    CHECK( text[ "state" ] == "failed" );
    CHECK( text[ "error" ].toString().contains( "Invalid merge resolution" ) );
    // A field of an item the merge result does not have is never written back as a nameless item.
    const auto revived = attempt( { secondChoice,
                                    QVariantMap{ { "path", lua }, { "take", "theirs" } },
                                    QVariantMap{ { "path", firstFrame }, { "field", "regex" }, { "value", "^x$" } } } );
    CHECK( revived[ "state" ] == "failed" );
    CHECK( revived[ "error" ].toString().contains( "Invalid merge resolution" ) );
    // Nor is anything under the name changed (it would turn the name into an object).
    const auto renamed = attempt( { secondChoice,
                                    QVariantMap{ { "path", lua }, { "take", "theirs" } },
                                    QVariantMap{ { "path", second }, { "field", "name/x" }, { "value", "bad" } } } );
    CHECK( renamed[ "state" ] == "failed" );
    CHECK( renamed[ "error" ].toString().contains( "Invalid merge resolution" ) );
    // A path that does not fit the item's structure (the item itself as a named array, an
    // element of a text field) would rewrite more than one field.
    for ( const auto* field : { "@x", "regex/@x", "/x", "fields//x" } ) {
        INFO( field );
        const auto misfit = attempt( { secondChoice,
                                       QVariantMap{ { "path", lua }, { "take", "theirs" } },
                                       QVariantMap{ { "path", second }, { "field", field }, { "value", 1 } } } );
        CHECK( misfit[ "state" ] == "failed" );
        CHECK( misfit[ "error" ].toString().contains( "Invalid merge resolution" ) );
    }
    // The previews settings are the pack's, never edited: no resolution names them.
    for ( const auto& meta : { QVariantMap{ { "path", "previews/meta.json" }, { "take", "mine" } },
                               QVariantMap{ { "path", "previews/meta.json" }, { "field", "" }, { "remove", true } } } ) {
        const auto settings = attempt( { secondChoice, QVariantMap{ { "path", lua }, { "take", "theirs" } }, meta } );
        CHECK( settings[ "state" ] == "failed" );
        CHECK( settings[ "error" ].toString().contains( "Invalid merge resolution" ) );
    }
    // Removing under a text field would turn the text into an object.
    const auto throughText = attempt(
        { secondChoice, QVariantMap{ { "path", lua }, { "take", "theirs" } },
          QVariantMap{ { "path", second }, { "field", "format/x" }, { "remove", true } } } );
    CHECK( throughText[ "state" ] == "failed" );
    CHECK( throughText[ "error" ].toString().contains( "Invalid merge resolution" ) );
    CHECK( manager.plugins().first().toMap()[ "version" ] == "1.0.0" );
    // Whole sides and fields of present items go ahead.
    const auto applied = attempt( { secondChoice, QVariantMap{ { "path", lua }, { "take", "theirs" } } } );
    INFO( applied[ "error" ].toString().toStdString() );
    CHECK( applied[ "state" ] == "completed" );
    const auto content = manager.packContentDirectory( id );
    CHECK( previewRegex( content, "Second" ) == "^B$" );
    CHECK( previewRegex( content, "First Frame" ).isEmpty() );

    // Version 3 no longer lists the Lua file: a revert does not bring it back (the content
    // folder would never have it, an edit with no effect).
    const auto v3 = writePackage( root.filePath( "v3" ), "1.2.0", QJsonDocument( document ).toJson(),
                                  "-- theirs\n" );
    {
        auto manifest = packManifest( "1.2.0" );
        auto contents = manifest[ "content" ].toObject();
        contents.remove( "lua" );
        manifest[ "content" ] = contents;
        QFile file( v3 + "/plugin.json" );
        REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        file.write( QJsonDocument( manifest ).toJson() );
    }
    const auto third = finished( manager, manager.manage( "install", {}, { { "path", v3 } } ) );
    INFO( third[ "error" ].toString().toStdString() );
    REQUIRE( third[ "state" ] == "completed" );
    const auto luaHistory
        = manager.manage( "pack_history", id, { { "path", lua } } ).payload[ "commits" ].toList();
    REQUIRE( !luaHistory.isEmpty() );
    const auto reverted = manager.manage(
        "pack_reset", id, { { "path", lua }, { "commit", luaHistory.last().toMap()[ "commit" ] } } );
    CHECK( !reverted.ok() );
    CHECK( reverted.error.contains( "no such file" ) );
    const auto edits = manager.manage( "pack_edits", id, {} ).payload[ "files" ].toList();
    CHECK( std::none_of( edits.cbegin(), edits.cend(), [ &lua ]( const QVariant& file ) {
        return file.toMap().value( "path" ).toString() == lua;
    } ) );
}

TEST_CASE( "A version without a previews document takes the previews with it", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    const auto second = QStringLiteral( "previews/%53econd.json" );
    const auto v1 = writePackage( root.filePath( "v1" ), "1.0.0", Previews, "-- a\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    auto item = QJsonDocument::fromJson( Previews ).object()[ "previews" ].toArray()[ 0 ].toObject();
    item[ "regex" ] = "^MINE$";
    REQUIRE( manager.manage( "pack_edit", id,
                             { { "path", second },
                               { "content", QString::fromUtf8( QJsonDocument( item ).toJson() ) } } )
                 .ok() );
    // Version 2 has only its Lua file: no previews document.
    const auto v2 = writePackage( root.filePath( "v2" ), "1.1.0", Previews, "-- b\n" );
    {
        auto manifest = packManifest( "1.1.0" );
        auto contents = manifest[ "content" ].toObject();
        contents.remove( "previews" );
        manifest[ "content" ] = contents;
        QFile file( v2 + "/plugin.json" );
        REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        file.write( QJsonDocument( manifest ).toJson() );
    }
    REQUIRE( QFile::remove( v2 + "/previews.json" ) );
    // Keeping yours keeps nothing that could load: the edited preview goes, and the user is told.
    const auto updated = finished(
        manager, manager.manage( "install", {}, { { "path", v2 }, { "strategy", "keep_mine" } } ) );
    INFO( updated[ "error" ].toString().toStdString() );
    REQUIRE( updated[ "state" ] == "completed" );
    CHECK( updated[ "result" ].toMap()[ "droppedEdits" ].toStringList().contains( "Second" ) );
    const auto files = manager.manage( "pack_files", id, {} ).payload[ "files" ].toList();
    CHECK( std::none_of( files.cbegin(), files.cend(), []( const QVariant& file ) {
        return file.toMap().value( "kind" ).toString() == "preview";
    } ) );
    // Nor does a revert bring a preview back.
    const auto history
        = manager.manage( "pack_history", id, { { "path", second } } ).payload[ "commits" ].toList();
    REQUIRE( !history.isEmpty() );
    const auto reverted = manager.manage(
        "pack_reset", id, { { "path", second }, { "commit", history.last().toMap()[ "commit" ] } } );
    CHECK( !reverted.ok() );
    CHECK( reverted.error.contains( "no previews document" ) );
}

TEST_CASE( "Named elements keep the order changes of either side", "[packmerge][E16]" )
{
    const auto item = []( std::initializer_list<std::pair<const char*, int>> fields ) {
        QJsonArray array;
        for ( const auto& [ name, value ] : fields ) {
            array.append( QJsonObject{ { "name", name }, { "v", value } } );
        }
        return QJsonValue( QJsonObject{ { "name", "P" }, { "fields", array } } );
    };
    const auto names = []( const packmerge::JsonMerge& merged ) {
        QStringList result;
        for ( const auto& field : merged.value->toObject().value( "fields" ).toArray() ) {
            result.append( field.toObject().value( "name" ).toString() );
        }
        return result;
    };
    const auto base = item( { { "A", 0 }, { "B", 0 } } );
    // Yours edits A, the new version reorders: both are kept.
    auto merged = packmerge::mergeJson( base, item( { { "A", 1 }, { "B", 0 } } ),
                                        item( { { "B", 0 }, { "A", 0 } } ), MergeStrategy::Fail );
    CHECK( merged.conflicts.isEmpty() );
    CHECK( names( merged ) == QStringList{ "B", "A" } );
    CHECK( merged.value->toObject().value( "fields" ).toArray().last().toObject().value( "v" ).toInt() == 1 );
    // The new version inserts C between them: it stays there.
    merged = packmerge::mergeJson( base, item( { { "A", 1 }, { "B", 0 } } ),
                                   item( { { "A", 0 }, { "C", 0 }, { "B", 0 } } ), MergeStrategy::Fail );
    CHECK( merged.conflicts.isEmpty() );
    CHECK( names( merged ) == QStringList{ "A", "C", "B" } );
    // Yours inserts D first and the new version C between: both where they were put.
    merged = packmerge::mergeJson( base, item( { { "D", 0 }, { "A", 0 }, { "B", 0 } } ),
                                   item( { { "A", 0 }, { "C", 0 }, { "B", 0 } } ), MergeStrategy::Fail );
    CHECK( merged.conflicts.isEmpty() );
    CHECK( names( merged ) == QStringList{ "D", "A", "C", "B" } );
    // Two different reorderings are one conflict of the list.
    const auto three = item( { { "A", 0 }, { "B", 0 }, { "C", 0 } } );
    merged = packmerge::mergeJson( three, item( { { "C", 0 }, { "A", 0 }, { "B", 0 } } ),
                                   item( { { "B", 0 }, { "A", 0 }, { "C", 0 } } ), MergeStrategy::Fail );
    CHECK( merged.conflicts == QStringList{ "fields" } );
    // An element both sides added, each in another place: the order conflicts too.
    merged = packmerge::mergeJson( base, item( { { "A", 0 }, { "X", 0 }, { "B", 0 } } ),
                                   item( { { "X", 0 }, { "A", 0 }, { "B", 0 } } ), MergeStrategy::Fail );
    CHECK( merged.conflicts == QStringList{ "fields" } );
    // In the same place it merges.
    merged = packmerge::mergeJson( base, item( { { "A", 0 }, { "X", 0 }, { "B", 0 } } ),
                                   item( { { "A", 0 }, { "X", 0 }, { "B", 0 } } ), MergeStrategy::Fail );
    CHECK( merged.conflicts.isEmpty() );
    CHECK( names( merged ) == QStringList{ "A", "X", "B" } );
    // Also while yours moves another element: placing X is not moving C.
    merged = packmerge::mergeJson( three, item( { { "C", 0 }, { "A", 0 }, { "X", 0 }, { "B", 0 } } ),
                                   item( { { "A", 0 }, { "X", 0 }, { "B", 0 }, { "C", 0 } } ),
                                   MergeStrategy::Fail );
    CHECK( merged.conflicts.isEmpty() );
    CHECK( names( merged ) == QStringList{ "C", "A", "X", "B" } );
}

TEST_CASE( "An install that fails before its journal keeps an earlier one", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    const auto storeRoot = root.filePath( "data/pack-store" );
    const auto v1 = writePackage( root.filePath( "v1" ), "1.0.0", Previews, "-- a\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    // An earlier update stopped part-way (its journal is there), and its repository is gone, so
    // it cannot be put back.
    const auto tips = PackStore( storeRoot, id ).tips();
    const auto journal = QDir( storeRoot ).filePath( id + ".update.json" );
    {
        QFile file( journal );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( QJsonDocument( QJsonObject{ { "existed", true },
                                                { "upstream", tips.upstream },
                                                { "local", tips.local } } )
                        .toJson() );
    }
    REQUIRE( QDir( QDir( storeRoot ).filePath( id + ".git" ) ).removeRecursively() );
    // The next update fails on that journal, before it writes its own: the journal stays, so
    // the pack keeps loading its installed version.
    const auto v2 = writePackage( root.filePath( "v2" ), "1.1.0", Previews, "-- b\n" );
    const auto failed = finished( manager, manager.manage( "install", {}, { { "path", v2 } } ) );
    CHECK( failed[ "state" ] == "failed" );
    CHECK( QFileInfo::exists( journal ) );
    CHECK( manager.packContentDirectory( id ) == manager.plugins().first().toMap()[ "directory" ].toString() );
}

TEST_CASE( "A store that is gone is never taken as put back", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto journal = root.filePath( "org.example.pack.update.json" );
    auto v2Previews = QJsonDocument::fromJson( Previews ).object();
    auto previews = v2Previews[ "previews" ].toArray();
    auto frame = previews[ 1 ].toObject();
    frame[ "regex" ] = "^A2$";
    previews[ 1 ] = frame;
    v2Previews[ "previews" ] = previews;
    const auto v2 = packFiles( QJsonDocument( v2Previews ).toJson() );
    {
        // An update that wrote its content, then CILogg ended before the package swap.
        PackStore updating( root.path(), "org.example.pack" );
        REQUIRE( updating.install( packFiles(), packManifest(), "1.0.0", "Team packs" ).isEmpty() );
        REQUIRE( updating.beginUpdate( updating.tips() ).isEmpty() );
        REQUIRE( updating.applyUpdate( v2, updating.planUpdate( v2, MergeStrategy::Fail ).merged,
                                       packManifest( "1.1.0" ), "1.1.0", "Team packs" )
                     .isEmpty() );
    }
    // Then the repository went away: the update cannot be put back, and its content is not
    // trusted.
    REQUIRE( QDir( root.filePath( "org.example.pack.git" ) ).removeRecursively() );
    PackStore recovered( root.path(), "org.example.pack" );
    CHECK( recovered.recoverInterruptedUpdate().contains( "not there to be put back" ) );
    CHECK( QFileInfo::exists( journal ) );
    CHECK( recovered.contentFromUnfinishedUpdate() );
}

TEST_CASE( "A content folder lost and written again by a reading operation is announced",
           "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    const auto v1 = writePackage( root.filePath( "v1" ), "1.0.0", Previews, "-- a\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    auto item = QJsonDocument::fromJson( Previews ).object()[ "previews" ].toArray()[ 0 ].toObject();
    item[ "regex" ] = "^EDITED$";
    REQUIRE( manager.manage( "pack_edit", id,
                             { { "path", "previews/%53econd.json" },
                               { "content", QString::fromUtf8( QJsonDocument( item ).toJson() ) } } )
                 .ok() );
    // The content folder is deleted by hand; its revision record stays.
    const auto current = root.filePath( "data/pack-store/org.example.pack/current" );
    REQUIRE( QDir( current ).removeRecursively() );
    QSignalSpy changed( &manager, &PluginManager::changed );
    REQUIRE( manager.manage( "pack_files", id, {} ).ok() );
    CHECK( changed.count() >= 1 );
    CHECK( manager.packContentDirectory( id ) == current );
    CHECK( readFile( current + "/previews.json" ).contains( "^EDITED$" ) );
}

TEST_CASE( "The previews settings follow the installed version", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PackStore store( root.path(), "org.example.pack" );
    REQUIRE( store.install( packFiles(), packManifest(), "1.0.0", "Team packs" ).isEmpty() );
    const auto meta = QStringLiteral( "previews/meta.json" );
    REQUIRE( store.files().contains( meta ) );
    PackFiles stored;
    bool changes = false;
    CHECK( store.planRestore( meta, {}, &stored, &changes ).contains( "previews settings" ) );
    CHECK( store.restore( meta, {}, "Revert" ).contains( "previews settings" ) );
}

TEST_CASE( "A content folder an unfinished update wrote is not loaded", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto second = QStringLiteral( "previews/%53econd.json" );
    PackStore store( root.path(), "org.example.pack" );
    REQUIRE( store.install( packFiles(), packManifest(), "1.0.0", "Team packs" ).isEmpty() );
    auto edited = QJsonDocument::fromJson( store.files().value( second ) ).object();
    edited[ "regex" ] = "^EDITED$";
    REQUIRE( store.commitEdit( second, packcanonical::itemBytes( edited ), "Edit" ).isEmpty() );
    CHECK_FALSE( store.contentFromUnfinishedUpdate() );
    REQUIRE( store.beginUpdate( store.tips() ).isEmpty() );
    // Whenever a journal is there: the folder is replaced before its revision is recorded, so
    // the record cannot tell which folder is in place.
    CHECK( store.contentFromUnfinishedUpdate() );
    auto v2Previews = QJsonDocument::fromJson( Previews ).object();
    auto previews = v2Previews[ "previews" ].toArray();
    auto frame = previews[ 1 ].toObject();
    frame[ "regex" ] = "^A2$";
    previews[ 1 ] = frame;
    v2Previews[ "previews" ] = previews;
    const auto v2 = packFiles( QJsonDocument( v2Previews ).toJson() );
    REQUIRE( store.applyUpdate( v2, store.planUpdate( v2, MergeStrategy::Fail ).merged,
                                packManifest( "1.1.0" ), "1.1.0", "Team packs" )
                 .isEmpty() );
    // Now it holds the update's content, while the package folder is still the old version.
    CHECK( store.contentFromUnfinishedUpdate() );
    store.endUpdate();
    CHECK_FALSE( store.contentFromUnfinishedUpdate() );
}

TEST_CASE( "A settled journal that cannot be removed keeps the edits loading", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    const auto storeRoot = root.filePath( "data/pack-store" );
    const auto v1 = writePackage( root.filePath( "v1" ), "1.0.0", Previews, "-- a\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    auto item = QJsonDocument::fromJson( Previews ).object()[ "previews" ].toArray()[ 0 ].toObject();
    item[ "regex" ] = "^EDITED$";
    REQUIRE( manager.manage( "pack_edit", id,
                             { { "path", "previews/%53econd.json" },
                               { "content", QString::fromUtf8( QJsonDocument( item ).toJson() ) } } )
                 .ok() );
    const auto before = PackStore( storeRoot, id ).tips();
    // Version 2 changes another preview: it merges with the edit and is installed.
    auto document = QJsonDocument::fromJson( Previews ).object();
    auto previews = document[ "previews" ].toArray();
    auto frame = previews[ 1 ].toObject();
    frame[ "regex" ] = "^A2$";
    previews[ 1 ] = frame;
    document[ "previews" ] = previews;
    const auto v2 = writePackage( root.filePath( "v2" ), "1.1.0", QJsonDocument( document ).toJson(), "-- a\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v2 } } ) )[ "state" ]
             == "completed" );
    // Its journal is still there and cannot be removed (read-only on Windows).
    const auto journal = QDir( storeRoot ).filePath( id + ".update.json" );
    {
        QFile file( journal );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( QJsonDocument( QJsonObject{ { "existed", true },
                                                { "upstream", before.upstream },
                                                { "local", before.local } } )
                        .toJson() );
    }
    REQUIRE( QFile::setPermissions( journal, QFileDevice::ReadOwner | QFileDevice::ReadUser ) );
    manager.discover();
    // The update had finished: settled, so the content folder with the edit loads.
    const auto content = manager.packContentDirectory( id );
    CHECK( content == PackStore( storeRoot, id ).contentDirectory() );
    CHECK( readFile( content + "/previews.json" ).contains( "^EDITED$" ) );
    CHECK( readFile( content + "/previews.json" ).contains( "^A2$" ) );
    QFile::setPermissions( journal, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                        | QFileDevice::ReadUser | QFileDevice::WriteUser );
}

TEST_CASE( "A content folder written again by a reading operation is announced", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    const auto second = QStringLiteral( "previews/%53econd.json" );
    const auto v1 = writePackage( root.filePath( "v1" ), "1.0.0", Previews, "-- a\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    const auto before = manager.packRevision( id );
    REQUIRE( !before.isEmpty() );
    // Something holds a file in the staging folder: on Windows the edit's files cannot be
    // written yet (elsewhere the folder is cleared and they are written at once).
    const auto stale = root.filePath( "data/pack-store/org.example.pack/current.new/lua/stale.lua" );
    REQUIRE( QDir().mkpath( QFileInfo( stale ).absolutePath() ) );
    QFile held( stale );
    REQUIRE( held.open( QIODevice::WriteOnly ) );
    held.write( "-- stale\n" );
    held.flush();
    auto item = QJsonDocument::fromJson( Previews ).object()[ "previews" ].toArray()[ 0 ].toObject();
    item[ "regex" ] = "^EDITED$";
    const auto edit = manager.manage(
        "pack_edit", id, { { "path", second }, { "content", QString::fromUtf8( QJsonDocument( item ).toJson() ) } } );
    held.close();
    if ( edit.ok() ) {
        CHECK( manager.packRevision( id ) != before );
        return;
    }
    // The edit is saved, but the folder still holds the content from before.
    CHECK( edit.error.contains( "could not be written yet" ) );
    CHECK( manager.packRevision( id ) == before );
    // An operation that only reads writes it again, and says so.
    QSignalSpy changed( &manager, &PluginManager::changed );
    REQUIRE( manager.manage( "pack_files", id, {} ).ok() );
    CHECK( manager.packRevision( id ) != before );
    CHECK( changed.count() >= 1 );
    CHECK( readFile( manager.packContentDirectory( id ) + "/previews.json" ).contains( "^EDITED$" ) );
}

TEST_CASE( "A conflict on a field with an empty name is settled as a whole item", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    const auto second = QStringLiteral( "previews/%53econd.json" );
    // "Second" has an option with an empty name, which both sides change.
    const auto previewsWith = []( int value ) {
        auto document = QJsonDocument::fromJson( Previews ).object();
        auto previews = document[ "previews" ].toArray();
        auto item = previews[ 0 ].toObject();
        item[ "options" ] = QJsonObject{ { "", value } };
        previews[ 0 ] = item;
        document[ "previews" ] = previews;
        return QJsonDocument( document ).toJson();
    };
    const auto v1 = writePackage( root.filePath( "v1" ), "1.0.0", previewsWith( 0 ), "-- a\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    auto item = QJsonDocument::fromJson(
                    manager.manage( "pack_read", id, { { "path", second } } ).payload[ "content" ].toString().toUtf8() )
                    .object();
    item[ "options" ] = QJsonObject{ { "", 1 } };
    REQUIRE( manager.manage( "pack_edit", id,
                             { { "path", second },
                               { "content", QString::fromUtf8( QJsonDocument( item ).toJson() ) } } )
                 .ok() );
    const auto v2 = writePackage( root.filePath( "v2" ), "1.1.0", previewsWith( 2 ), "-- a\n" );
    const auto asked = finished( manager, manager.manage( "install", {}, { { "path", v2 } } ) );
    REQUIRE( asked[ "state" ] == "failed" );
    // A field resolution could not name "options/": the item is offered as a whole.
    const auto conflicts = asked[ "result" ].toMap()[ "conflicts" ].toList();
    REQUIRE( conflicts.size() == 1 );
    CHECK( conflicts.first().toMap()[ "path" ] == second );
    CHECK( conflicts.first().toMap()[ "field" ].toString().isEmpty() );
    const auto kept = finished(
        manager, manager.manage( "install", {},
                                 { { "path", v2 },
                                   { "resolutions", QVariantList{ QVariantMap{ { "path", second }, { "take", "mine" } } } },
                                   { "revision", asked[ "result" ].toMap()[ "revision" ] } } ) );
    INFO( kept[ "error" ].toString().toStdString() );
    CHECK( kept[ "state" ] == "completed" );
    const auto previews = QJsonDocument::fromJson( readFile( manager.packContentDirectory( id ) + "/previews.json" ) )
                              .object()[ "previews" ]
                              .toArray();
    CHECK( previews[ 0 ].toObject()[ "options" ].toObject()[ "" ].toInt() == 1 );
}

TEST_CASE( "A merge result stays within the preview limits", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    // 128 previews, the most a pack can have: `first`, then P1 ... P127.
    const auto previews = []( const QString& first ) {
        QJsonArray items;
        for ( int index = 0; index < 128; ++index ) {
            items.append( QJsonObject{
                { "name", index == 0 ? first : QStringLiteral( "P%1" ).arg( index ) },
                { "regex", "^x$" },
                { "format", "fields" },
                { "fields", QJsonArray{} } } );
        }
        return QJsonDocument( QJsonObject{ { "version", 1 }, { "previews", items } } ).toJson();
    };
    const auto v1 = writePackage( root.filePath( "v1" ), "1.0.0", previews( "P0" ), "-- a\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    // P0 is edited; the new version removes it and has Q instead, 128 again.
    const auto p0 = QStringLiteral( "previews/%1.json" ).arg( packcanonical::encodeName( "P0" ) );
    auto item = QJsonDocument::fromJson(
                    manager.manage( "pack_read", id, { { "path", p0 } } ).payload[ "content" ].toString().toUtf8() )
                    .object();
    item[ "regex" ] = "^mine$";
    REQUIRE( manager.manage( "pack_edit", id,
                             { { "path", p0 },
                               { "content", QString::fromUtf8( QJsonDocument( item ).toJson() ) } } )
                 .ok() );
    const auto v2 = writePackage( root.filePath( "v2" ), "1.1.0", previews( "Q" ), "-- a\n" );
    // The open conflict holds your P0 beside the 128: the merge is still offered.
    const auto asked = finished( manager, manager.manage( "install", {}, { { "path", v2 } } ) );
    REQUIRE( asked[ "state" ] == "failed" );
    REQUIRE( asked[ "result" ].toMap()[ "needsMerge" ].toBool() );
    const auto revision = asked[ "result" ].toMap()[ "revision" ].toString();
    const auto settle = [ &manager, &v2, &p0, &revision ]( const QString& side ) {
        return finished( manager,
                         manager.manage( "install", {},
                                         { { "path", v2 },
                                           { "resolutions",
                                             QVariantList{ QVariantMap{ { "path", p0 }, { "take", side } } } },
                                           { "revision", revision } } ) );
    };
    // Keeping it would make 129 previews: refused, and nothing changes. No pack check runs here
    // (as for a disabled pack), so the merge itself must refuse it.
    const auto kept = settle( "mine" );
    CHECK( kept[ "state" ] == "failed" );
    CHECK( kept[ "error" ].toString().contains( "more than 128 previews" ) );
    CHECK( manager.plugins().first().toMap()[ "version" ] == "1.0.0" );
    const auto content = manager.packContentDirectory( id );
    CHECK( previewRegex( content, "P0" ) == "^mine$" );
    CHECK( previewRegex( content, "Q" ).isEmpty() );
    // With the new version's side the update goes ahead.
    const auto taken = settle( "theirs" );
    INFO( taken[ "error" ].toString().toStdString() );
    CHECK( taken[ "state" ] == "completed" );
    CHECK( previewRegex( content, "P0" ).isEmpty() );
    CHECK( previewRegex( content, "Q" ) == "^x$" );
}

TEST_CASE( "An enabled pack's update and reset are checked as they would load",
           "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    const auto v1 = writePackage( root.filePath( "v1" ), "1.0.0", Previews, "-- a\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    const auto second = QStringLiteral( "previews/%53econd.json" );
    const auto content = manager.packContentDirectory( id );
    // The user deletes the pack's "Second"; another enabled pack has a "Second" of its own, so
    // the check refuses any content of this pack that has one.
    REQUIRE( manager.manage( "pack_edit", id, { { "path", second }, { "remove", true } } ).ok() );
    QStringList checked;
    const QString refusal = QStringLiteral( "Another pack has a preview named \"Second\"." );
    manager.setPackCheck( [ &checked, &refusal ]( const QString&, const QString& directory,
                                                  const QJsonObject& ) {
        checked << directory;
        return previewRegex( directory, "Second" ).isEmpty() ? QString{} : refusal;
    } );
    REQUIRE( manager.manage( "enable", id, {} ).ok() );

    // A new version that still ships "Second": what loads is the merge, without it.
    auto v2Previews = QJsonDocument::fromJson( Previews ).object();
    auto previews = v2Previews[ "previews" ].toArray();
    auto firstFrame = previews[ 1 ].toObject();
    firstFrame[ "regex" ] = "^A2$";
    previews[ 1 ] = firstFrame;
    v2Previews[ "previews" ] = previews;
    const auto v2 = writePackage( root.filePath( "v2" ), "1.1.0", QJsonDocument( v2Previews ).toJson(),
                                  "-- a\n" );
    checked.clear();
    const auto updated = finished( manager, manager.manage( "install", {}, { { "path", v2 } } ) );
    INFO( updated[ "error" ].toString().toStdString() );
    REQUIRE( updated[ "state" ] == "completed" );
    CHECK( checked.size() == 1 );
    CHECK( previewRegex( content, "First Frame" ) == "^A2$" );
    CHECK( previewRegex( content, "Second" ).isEmpty() );

    // A reset brings "Second" back, so it is checked first and refused like an edit.
    CHECK( manager.manage( "pack_reset", id, { { "path", second } } ).error == refusal );
    CHECK( manager.manage( "pack_reset", id, {} ).error == refusal );
    CHECK( previewRegex( content, "Second" ).isEmpty() );
    CHECK( manager.manage( "pack_edits", id, {} ).payload[ "files" ].toList().size() == 1 );
    // Once the pack is disabled nothing loads it, and the reset goes through.
    REQUIRE( manager.manage( "disable", id, {} ).ok() );
    REQUIRE( manager.manage( "pack_reset", id, { { "path", second } } ).ok() );
    CHECK( previewRegex( content, "Second" ) == "^B$" );
}

TEST_CASE( "Packs install, edit and update through their local store", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    const auto v1 = writePackage( root.filePath( "v1" ), "1.0.0", Previews, "-- a\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    const auto content = manager.packContentDirectory( id );
    CHECK( content.endsWith( "/current" ) );
    CHECK( previewRegex( content, "Second" ) == "^B$" );

    // Edits through the pack operations, as from the editor, the CLI, MCP or Python.
    const auto second = QStringLiteral( "previews/%53econd.json" );
    auto item = QJsonDocument::fromJson(
                    manager.manage( "pack_read", id, { { "path", second } } ).payload[ "content" ].toString().toUtf8() )
                    .object();
    item[ "regex" ] = "^MINE$";
    const auto edited = manager.manage(
        "pack_edit", id,
        { { "path", second }, { "content", QString::fromUtf8( QJsonDocument( item ).toJson() ) } } );
    REQUIRE( edited.ok() );
    CHECK( previewRegex( content, "Second" ) == "^MINE$" );
    const auto edits = manager.manage( "pack_edits", id, {} ).payload[ "files" ].toList();
    REQUIRE( edits.size() == 1 );
    CHECK( edits.first().toMap()[ "state" ] == "edited" );
    CHECK( manager.plugins().first().toMap()[ "localEdits" ] == 1 );
    CHECK( !manager.manage( "pack_edit", id, { { "path", "files/lua/new.lua" }, { "content", "x" } } ).ok() );
    auto renamed = item;
    renamed[ "name" ] = "Other";
    CHECK( !manager.manage( "pack_edit", id,
                            { { "path", second },
                              { "content", QString::fromUtf8( QJsonDocument( renamed ).toJson() ) } } )
                .ok() );
    CHECK( manager.manage( "pack_history", id, { { "path", second } } ).payload[ "commits" ].toList().size()
           == 2 );
    // Items are edited in place: no new item, no deleted Lua file, one spelling per item.
    CHECK( manager.manage( "pack_edit", id,
                           { { "path", "previews/%4Eew.json" },
                             { "content", R"({ "name": "New", "regex": "^N$" })" } } )
               .error
           == "No such item in the pack." );
    CHECK( !manager.manage( "pack_edit", id,
                            { { "path", packcanonical::filePath( "lua/a.lua" ) }, { "remove", true } } )
                .ok() );
    // An edit says what it does: a missing content is never an empty file.
    CHECK( manager.manage( "pack_edit", id, { { "path", packcanonical::filePath( "lua/a.lua" ) } } ).error
           == "pack_edit takes either content or remove: true." );
    CHECK( !manager.manage( "pack_edit", id, { { "path", second }, { "content", "{}" }, { "remove", true } } )
                .ok() );
    CHECK( readFile( content + "/lua/a.lua" ) == "-- a\n" );
    CHECK( manager.manage( "pack_edit", id, { { "path", "previews/Second.json" }, { "content", "{}" } } )
               .error
           == "Not an item of the pack." );
    CHECK( !manager.manage( "pack_reset", id, { { "path", "files/../outside.lua" } } ).ok() );
    CHECK( !manager.manage( "pack_reset", id, { { "commit", "refs/heads/upstream" } } ).ok() );

    // A content folder whose repository is gone still holds the edit: it is rebuilt, not reset.
    REQUIRE( QDir( root.filePath( "data/pack-store/org.example.pack.git" ) ).removeRecursively() );
    const auto kept = manager.manage( "pack_edits", id, {} ).payload[ "files" ].toList();
    REQUIRE( kept.size() == 1 );
    CHECK( kept.first().toMap()[ "path" ] == second );
    CHECK( previewRegex( content, "Second" ) == "^MINE$" );

    // Options an operation does not take are refused, not ignored.
    CHECK( manager.manage( "pack_read", id, { { "path", second }, { "version", "upstream" } } ).error
           == "Unknown pack_read option: version." );
    CHECK( !manager.manage( "pack_edit", id, { { "path", second }, { "text", "{}" } } ).ok() );
    CHECK( !manager.manage( "pack_files", id, { { "path", second } } ).ok() );

    // A damaged store is rebuilt before the next update merges, with the edit kept.
    REQUIRE( QFile::remove( root.filePath( "data/pack-store/org.example.pack.git/refs/heads/local" ) ) );

    // A version that changes other things merges with the edit.
    auto v2Previews = QJsonDocument::fromJson( Previews ).object();
    auto previews = v2Previews[ "previews" ].toArray();
    auto firstFrame = previews[ 1 ].toObject();
    firstFrame[ "regex" ] = "^A2$";
    previews[ 1 ] = firstFrame;
    v2Previews[ "previews" ] = previews;
    const auto v2 = writePackage( root.filePath( "v2" ), "1.1.0", QJsonDocument( v2Previews ).toJson(),
                                  "-- a, 2\n" );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v2 } } ) )[ "state" ]
             == "completed" );
    CHECK( previewRegex( content, "Second" ) == "^MINE$" );
    CHECK( previewRegex( content, "First Frame" ) == "^A2$" );
    CHECK( readFile( content + "/lua/a.lua" ) == "-- a, 2\n" );
    // The update finished: its journal is gone.
    CHECK( !QFileInfo::exists( root.filePath( "data/pack-store/org.example.pack.update.json" ) ) );
    CHECK( QDir( root.filePath( "data/pack-store" ) )
               .entryList( { "org.example.pack.git.damaged-*" }, QDir::Dirs | QDir::NoDotAndDotDot )
               .size()
           == 1 );

    // A version that changes the edited field: refused from automation, with the conflict.
    auto second3 = previews[ 0 ].toObject();
    second3[ "regex" ] = "^THEIRS$";
    previews[ 0 ] = second3;
    v2Previews[ "previews" ] = previews;
    const auto v3 = writePackage( root.filePath( "v3" ), "1.2.0", QJsonDocument( v2Previews ).toJson(),
                                  "-- a, 2\n" );
    const auto refused = finished( manager, manager.manage( "install", {}, { { "path", v3 } } ) );
    CHECK( refused[ "state" ] == "failed" );
    const auto conflicts = refused[ "result" ].toMap()[ "conflicts" ].toList();
    REQUIRE( conflicts.size() == 1 );
    CHECK( conflicts.first().toMap()[ "item" ] == "Second" );
    CHECK( conflicts.first().toMap()[ "field" ] == "regex" );
    CHECK( manager.plugins().first().toMap()[ "version" ] == "1.1.0" );
    CHECK( manager.plugins().first().toMap()[ "needsMerge" ] == "1.2.0" );
    CHECK( previewRegex( content, "Second" ) == "^MINE$" );

    SECTION( "keep_mine" )
    {
        REQUIRE( finished( manager, manager.manage( "install", {},
                                                     { { "path", v3 }, { "strategy", "keep_mine" } } ) )
                     [ "state" ]
                 == "completed" );
        CHECK( previewRegex( content, "Second" ) == "^MINE$" );
        CHECK( manager.plugins().first().toMap()[ "needsMerge" ].toString().isEmpty() );
    }
    SECTION( "take_theirs" )
    {
        REQUIRE( finished( manager, manager.manage( "install", {},
                                                     { { "path", v3 }, { "strategy", "take_theirs" } } ) )
                     [ "state" ]
                 == "completed" );
        CHECK( previewRegex( content, "Second" ) == "^THEIRS$" );
    }
    SECTION( "a resolution from the merge window" )
    {
        const QVariantList resolutions{ QVariantMap{
            { "path", second }, { "field", "regex" }, { "value", "^CHOSEN$" } } };
        REQUIRE( finished( manager, manager.manage( "install", {},
                                                     { { "path", v3 },
                                                       { "resolutions", resolutions },
                                                       { "revision", refused[ "result" ].toMap()[ "revision" ] } } ) )
                     [ "state" ]
                 == "completed" );
        CHECK( previewRegex( content, "Second" ) == "^CHOSEN$" );
    }
    SECTION( "resolutions only for the pack's own items" )
    {
        // A path outside the pack, a file the manifest does not list and a renamed whole item
        // are refused; nothing is written outside the store.
        for ( const auto& resolution :
              { QVariantMap{ { "path", "files/../../escaped.lua" }, { "text", "x" } },
                QVariantMap{ { "path", "files/lua/other.lua" }, { "text", "x" } },
                QVariantMap{ { "path", second },
                             { "field", QString{} },
                             { "value", QVariantMap{ { "name", "Other" } } } },
                QVariantMap{ { "path", second }, { "field", "name" }, { "value", "Other" } } } ) {
            const auto job = finished(
                manager, manager.manage( "install", {},
                                         { { "path", v3 },
                                           { "resolutions", QVariantList{ resolution } },
                                           { "revision", refused[ "result" ].toMap()[ "revision" ] } } ) );
            CHECK( job[ "state" ] == "failed" );
            CHECK( job[ "error" ].toString().startsWith( "Invalid merge resolution" ) );
        }
        CHECK( !QFileInfo::exists( root.filePath( "data/escaped.lua" ) ) );
        CHECK( previewRegex( content, "Second" ) == "^MINE$" );
    }
    CHECK( !manager.manage( "install", {}, { { "path", v3 }, { "strategy", "both" } } ).ok() );
    // Merge options belong to install and update: any other operation refuses them instead of
    // reporting success without the merge it asked for.
    for ( const auto& [ operation, option ] :
          { std::pair<QString, QString>{ "disable", "strategy" },
            std::pair<QString, QString>{ "pack_reset", "resolutions" },
            std::pair<QString, QString>{ "remove", "revision" } } ) {
        INFO( operation.toStdString() << " " << option.toStdString() );
        const auto refusal = manager.manage(
            operation, id,
            { { option, option == "resolutions" ? QVariant( QVariantList{} ) : QVariant( "fail" ) } } );
        CHECK( !refusal.ok() );
        CHECK( refusal.error == option + " applies only to install and update." );
    }
    REQUIRE( manager.plugins().size() == 1 ); // the refused remove left it installed

    // Reset to Pack Version, then removing the pack removes its store.
    REQUIRE( manager.manage( "pack_reset", id, {} ).ok() );
    CHECK( manager.manage( "pack_edits", id, {} ).payload[ "files" ].toList().isEmpty() );
    REQUIRE( manager.manage( "remove", id, {} ).ok() );
    CHECK( !QFileInfo::exists( root.filePath( "data/pack-store/org.example.pack.git" ) ) );
}

namespace {
// A pack package with predefined filters only (E16c): plugin.json and filters.conf, or no filters
// file at all when `filters` is empty.
QString writeFilterPackage( const QString& folder, const QString& version,
                            const QByteArray& filters )
{
    QDir().mkpath( folder );
    auto manifest = packManifest( version );
    QJsonObject content;
    if ( !filters.isEmpty() ) {
        content[ "filters" ] = "filters.conf";
    }
    else {
        content[ "previews" ] = "previews.json";
    }
    manifest[ "content" ] = content;
    const auto write = [ &folder ]( const QString& name, const QByteArray& bytes ) {
        QFile file( folder + '/' + name );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( bytes );
    };
    write( "plugin.json", QJsonDocument( manifest ).toJson() );
    if ( !filters.isEmpty() ) {
        write( "filters.conf", filters );
    }
    else {
        write( "previews.json", Previews );
    }
    return folder;
}

// The filters the content folder's filters file holds, by id, in its order.
QVector<QJsonObject> contentFilters( const QString& folder )
{
    const auto files = packcanonical::fromFilters( readFile( folder + "/filters.conf" ) );
    QVector<QJsonObject> filters;
    for ( const auto& id : QString::fromUtf8( files.value( "filters/order.txt" ) )
                               .split( '\n', Qt::SkipEmptyParts ) ) {
        filters.append( QJsonDocument::fromJson(
                            files.value( "filters/" + packcanonical::encodeName( id ) + ".json" ) )
                            .object() );
    }
    return filters;
}

QString edit( PluginManager& manager, const QString& path, const QJsonObject& item )
{
    return manager
        .manage( "pack_edit", "org.example.pack",
                 { { "path", path },
                   { "content", QString::fromUtf8( QJsonDocument( item ).toJson() ) } } )
        .error;
}
} // namespace

TEST_CASE( "Pack filters install, edit and update through the local store", "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    const auto v1
        = writeFilterPackage( root.filePath( "v1" ), "1.0.0", filtersConf( SomeFilters ) );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    const auto content = manager.packContentDirectory( id );
    REQUIRE( contentFilters( content ).size() == 2 );
    CHECK( manager.plugins().first().toMap()[ "filters" ] == 2 );

    // The filters are items of the pack, one per id, and an order.
    const auto one = QStringLiteral( "filters/f-one.json" );
    QStringList kinds;
    for ( const auto& file : manager.manage( "pack_files", id, {} ).payload[ "files" ].toList() ) {
        kinds.append( file.toMap()[ "kind" ].toString() + ':' + file.toMap()[ "item" ].toString() );
    }
    kinds.sort();
    CHECK( kinds
           == QStringList{ "filter-order:filters/order.txt", "filter:f-one", "filter:f-two" } );

    // Edited in place: the content folder's filters file follows, with a readable history.
    auto item = QJsonDocument::fromJson( manager.manage( "pack_read", id, { { "path", one } } )
                                             .payload[ "content" ]
                                             .toString()
                                             .toUtf8() )
                    .object();
    item[ "filter" ] = "ERROR|FATAL|PANIC";
    REQUIRE( edit( manager, one, item ).isEmpty() );
    CHECK( contentFilters( content ).first().value( "filter" ) == "ERROR|FATAL|PANIC" );
    CHECK( manager.manage( "pack_history", id, { { "path", one } } )
               .payload[ "commits" ]
               .toList()
               .first()
               .toMap()[ "message" ]
           == "Edit filter 'Errors'" );
    // The id is the filter's identity, and every filter stays one CILogg reads.
    auto otherId = item;
    otherId[ "id" ] = "f-other";
    CHECK( edit( manager, one, otherId )
           == "The filter's id must stay the same (copy it to make another)." );
    for ( const auto& [ key, bad, why ] :
          { std::tuple<QString, QJsonValue, QString>{ "name", "", "needs a name and a pattern" },
            std::tuple<QString, QJsonValue, QString>{ "filter", 3, "needs a name and a pattern" },
            std::tuple<QString, QJsonValue, QString>{ "regex", "yes",
                                                      "needs exactly an id, a name, a pattern" },
            std::tuple<QString, QJsonValue, QString>{
                "color", "red", "needs exactly an id, a name, a pattern" } } ) {
        INFO( key.toStdString() );
        auto invalid = item;
        invalid[ key ] = bad;
        CHECK( edit( manager, one, invalid ).contains( why ) );
    }
    CHECK( contentFilters( content ).first().value( "filter" ) == "ERROR|FATAL|PANIC" );
    // The order is edited as text, never deleted; a filter can be deleted.
    REQUIRE( manager
                 .manage( "pack_edit", id,
                          { { "path", "filters/order.txt" }, { "content", "f-two\nf-one\n" } } )
                 .ok() );
    CHECK( contentFilters( content ).first().value( "id" ) == "f-two" );
    CHECK( manager.manage( "pack_history", id, { { "path", "filters/order.txt" } } )
               .payload[ "commits" ]
               .toList()
               .first()
               .toMap()[ "message" ]
           == "Edit the filter order" );
    CHECK(
        !manager.manage( "pack_edit", id, { { "path", "filters/order.txt" }, { "remove", true } } )
             .ok() );
    REQUIRE( manager.manage( "pack_reset", id, { { "path", "filters/order.txt" } } ).ok() );
    CHECK( contentFilters( content ).first().value( "id" ) == "f-one" );
    REQUIRE(
        manager.manage( "pack_edit", id, { { "path", "filters/f-two.json" }, { "remove", true } } )
            .ok() );
    REQUIRE( contentFilters( content ).size() == 1 );
    REQUIRE( manager.manage( "pack_reset", id, { { "path", "filters/f-two.json" } } ).ok() );
    REQUIRE( contentFilters( content ).size() == 2 );

    // A new version that renames the edited filter and adds one merges field by field.
    auto v2Rows = SomeFilters;
    v2Rows[ 0 ].name = "Errors and worse";
    v2Rows.append( { "f-three", "Warnings", "WARN", false } );
    const auto v2 = writeFilterPackage( root.filePath( "v2" ), "1.1.0", filtersConf( v2Rows ) );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v2 } } ) )[ "state" ]
             == "completed" );
    auto merged = contentFilters( content );
    REQUIRE( merged.size() == 3 );
    CHECK( merged.at( 2 ).value( "id" ) == "f-three" ); // the new version's order
    const auto oneMerged
        = *std::find_if( merged.cbegin(), merged.cend(), []( const QJsonObject& filter ) {
              return filter.value( "id" ) == "f-one";
          } );
    CHECK( oneMerged.value( "filter" ) == "ERROR|FATAL|PANIC" );
    CHECK( oneMerged.value( "name" ) == "Errors and worse" );

    // A version that changes the edited pattern: a conflict on that field of that filter.
    auto v3Rows = v2Rows;
    v3Rows[ 0 ].filter = "ERROR";
    const auto v3 = writeFilterPackage( root.filePath( "v3" ), "1.2.0", filtersConf( v3Rows ) );
    const auto refused = finished( manager, manager.manage( "install", {}, { { "path", v3 } } ) );
    REQUIRE( refused[ "state" ] == "failed" );
    const auto conflicts = refused[ "result" ].toMap()[ "conflicts" ].toList();
    REQUIRE( conflicts.size() == 1 );
    CHECK( conflicts.first().toMap()[ "kind" ] == "filter" );
    CHECK( conflicts.first().toMap()[ "item" ] == "f-one" );
    CHECK( conflicts.first().toMap()[ "field" ] == "filter" );
    const auto revision = refused[ "result" ].toMap()[ "revision" ];
    // A resolution never changes the id, and the whole filter keeps it.
    for ( const auto& resolution :
          { QVariantMap{ { "path", one }, { "field", "id" }, { "value", "f-other" } },
            QVariantMap{ { "path", one },
                         { "field", QString{} },
                         { "value", QVariantMap{ { "id", "f-other" },
                                                 { "name", "X" },
                                                 { "filter", "x" },
                                                 { "regex", true } } } } } ) {
        const auto job
            = finished( manager, manager.manage( "install", {},
                                                 { { "path", v3 },
                                                   { "resolutions", QVariantList{ resolution } },
                                                   { "revision", revision } } ) );
        CHECK( job[ "error" ].toString().startsWith( "Invalid merge resolution" ) );
    }
    // A resolution that would make a filter CILogg cannot read is refused with the reason.
    const auto unreadable = finished(
        manager,
        manager.manage(
            "install", {},
            { { "path", v3 },
              { "resolutions", QVariantList{ QVariantMap{
                                   { "path", one }, { "field", "filter" }, { "value", "" } } } },
              { "revision", revision } } ) );
    CHECK( unreadable[ "error" ].toString().contains( "needs a name and a pattern" ) );
    REQUIRE( finished( manager, manager.manage(
                                    "install", {},
                                    { { "path", v3 },
                                      { "resolutions",
                                        QVariantList{ QVariantMap{ { "path", one },
                                                                   { "field", "filter" },
                                                                   { "value", "ERROR|PANIC" } } } },
                                      { "revision", revision } } ) )[ "state" ]
             == "completed" );
    CHECK( contentFilters( content ).size() == 3 );

    // A version without a filters file takes the filters with it, edits too, and says so.
    const auto v4 = writeFilterPackage( root.filePath( "v4" ), "2.0.0", {} );
    const auto job = finished( manager, manager.manage( "install", {}, { { "path", v4 } } ) );
    REQUIRE( job[ "state" ] == "completed" );
    CHECK( job[ "result" ].toMap()[ "droppedEdits" ].toStringList().contains( "f-one" ) );
    for ( const auto& file : manager.manage( "pack_files", id, {} ).payload[ "files" ].toList() ) {
        CHECK( !file.toMap()[ "kind" ].toString().startsWith( "filter" ) );
    }
    CHECK( manager.plugins().first().toMap()[ "filters" ] == 0 );
}

namespace {
// A pack package with highlighter sets only (E16c2): plugin.json and highlights.conf, or a
// previews document instead when `highlights` is empty.
QString writeHighlightPackage( const QString& folder, const QString& version,
                               const QByteArray& highlights )
{
    QDir().mkpath( folder );
    auto manifest = packManifest( version );
    QJsonObject content;
    content[ highlights.isEmpty() ? "previews" : "highlights" ]
        = highlights.isEmpty() ? "previews.json" : "highlights.conf";
    manifest[ "content" ] = content;
    const auto write = [ &folder ]( const QString& name, const QByteArray& bytes ) {
        QFile file( folder + '/' + name );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( bytes );
    };
    write( "plugin.json", QJsonDocument( manifest ).toJson() );
    write( highlights.isEmpty() ? "previews.json" : "highlights.conf",
           highlights.isEmpty() ? QByteArray( Previews ) : highlights );
    return folder;
}

// The set texts the content folder's highlighters file holds, by id.
PackFiles contentSets( const QString& folder )
{
    return packcanonical::fromHighlights( readFile( folder + "/highlights.conf" ) );
}
} // namespace

TEST_CASE( "Pack highlighter sets install, edit and update through the local store",
           "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    // Three rules: edits of the first and the third are apart (merged as text, changes on adjacent
    // lines would be one conflict, as in Git).
    const QVector<SetRow> v1Sets{
        { "s-one",
          "Errors",
          { { "ERROR" }, { "keep" }, { "fatal", true, false, "#ffffffff", "#ffff0000" } } },
        { "s-two", "Radio", {} }
    };
    const auto v1
        = writeHighlightPackage( root.filePath( "v1" ), "1.0.0", highlightsConf( v1Sets ) );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    const auto content = manager.packContentDirectory( id );
    REQUIRE( contentSets( content ).contains( "highlights/s-one.set" ) );
    CHECK( manager.plugins().first().toMap()[ "highlighterSets" ] == 2 );

    const auto one = QStringLiteral( "highlights/s-one.set" );
    QStringList kinds;
    for ( const auto& file : manager.manage( "pack_files", id, {} ).payload[ "files" ].toList() ) {
        kinds.append( file.toMap()[ "kind" ].toString() + ':' + file.toMap()[ "item" ].toString() );
    }
    kinds.sort();
    CHECK( kinds
           == QStringList{ "highlight-order:highlights/order.txt", "highlight:s-one",
                           "highlight:s-two" } );

    // A set is edited as text: its rules one per line. The edit is stored as CILogg writes it.
    const auto text
        = manager.manage( "pack_read", id, { { "path", one } } ).payload[ "content" ].toString();
    auto edited = text;
    edited.replace( "\"regexp\":\"ERROR\"", "\"regexp\":\"ERROR|PANIC\"" );
    edited.replace( "\n", "\r\n" ); // as an editor on Windows saves it
    REQUIRE( manager.manage( "pack_edit", id, { { "path", one }, { "content", edited } } ).ok() );
    CHECK( contentSets( content ).value( one ).contains( "ERROR|PANIC" ) );
    CHECK( !manager.manage( "pack_read", id, { { "path", one } } )
                .payload[ "content" ]
                .toString()
                .contains( '\r' ) );
    CHECK( manager.manage( "pack_history", id, { { "path", one } } )
               .payload[ "commits" ]
               .toList()
               .first()
               .toMap()[ "message" ]
           == "Edit highlighter set 'Errors'" );
    // Its id stays, every line is a rule CILogg reads, and a set is not deleted.
    auto otherId = text;
    otherId.replace( "id \"s-one\"", "id \"s-other\"" );
    CHECK( manager.manage( "pack_edit", id, { { "path", one }, { "content", otherId } } )
               .error.contains( "must stay" ) );
    CHECK(
        !manager.manage( "pack_edit", id, { { "path", one }, { "content", text + "rule {}\n" } } )
             .ok() );
    CHECK( !manager.manage( "pack_edit", id, { { "path", one }, { "remove", true } } ).ok() );
    CHECK( contentSets( content ).value( one ).contains( "ERROR|PANIC" ) );

    // A new version that changes another rule of the edited set and renames the other set merges
    // line by line.
    auto v2Sets = v1Sets;
    v2Sets[ 0 ].rules[ 2 ].pattern = "fatal|abort";
    v2Sets[ 1 ].name = "Radio 2";
    const auto v2
        = writeHighlightPackage( root.filePath( "v2" ), "1.1.0", highlightsConf( v2Sets ) );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v2 } } ) )[ "state" ]
             == "completed" );
    const auto merged = contentSets( content );
    CHECK( merged.value( one ).contains( "ERROR|PANIC" ) );
    CHECK( merged.value( one ).contains( "fatal|abort" ) );
    CHECK( merged.value( "highlights/s-two.set" ).contains( "name \"Radio 2\"" ) );

    // A version that changes the edited rule: a conflict of the set's text.
    auto v3Sets = v2Sets;
    v3Sets[ 0 ].rules[ 0 ].pattern = "ERROR|WARN";
    const auto v3
        = writeHighlightPackage( root.filePath( "v3" ), "1.2.0", highlightsConf( v3Sets ) );
    const auto refused = finished( manager, manager.manage( "install", {}, { { "path", v3 } } ) );
    REQUIRE( refused[ "state" ] == "failed" );
    const auto conflicts = refused[ "result" ].toMap()[ "conflicts" ].toList();
    REQUIRE( conflicts.size() == 1 );
    CHECK( conflicts.first().toMap()[ "kind" ] == "highlight" );
    CHECK( conflicts.first().toMap()[ "item" ] == "s-one" );
    // The resolved text from the merge window is stored as CILogg writes it; one that is not a set
    // is refused with the reason.
    const auto resolution = [ & ]( const QString& resolved ) {
        return finished(
            manager, manager.manage(
                         "install", {},
                         { { "path", v3 },
                           { "resolutions",
                             QVariantList{ QVariantMap{ { "path", one }, { "text", resolved } } } },
                           { "revision", refused[ "result" ].toMap()[ "revision" ] } } ) );
    };
    CHECK( resolution( "id \"s-one\"\nname \"Errors\"\nrule {\"x\":1}\n" )[ "error" ]
               .toString()
               .contains( "cannot be read" ) );
    auto chosen = QString::fromUtf8( merged.value( one ) );
    chosen.replace( "ERROR|PANIC", "ERROR|WARN|PANIC" );
    REQUIRE( resolution( chosen )[ "state" ] == "completed" );
    CHECK( contentSets( content ).value( one ).contains( "ERROR|WARN|PANIC" ) );

    // A version without a highlighters file takes the sets with it, edits too, and says so.
    const auto v4 = writeHighlightPackage( root.filePath( "v4" ), "2.0.0", {} );
    const auto job = finished( manager, manager.manage( "install", {}, { { "path", v4 } } ) );
    REQUIRE( job[ "state" ] == "completed" );
    CHECK( job[ "result" ].toMap()[ "droppedEdits" ].toStringList().contains( "s-one" ) );
    // An earlier state of a set does not come back into a version without highlighters.
    const auto commits
        = manager.manage( "pack_history", id, { { "path", one } } ).payload[ "commits" ].toList();
    REQUIRE( !commits.isEmpty() );
    CHECK( manager
               .manage( "pack_reset", id,
                        { { "path", one }, { "commit", commits.last().toMap()[ "commit" ] } } )
               .error.contains( "no highlighters file" ) );
    CHECK( manager.plugins().first().toMap()[ "highlighterSets" ] == 0 );
}

namespace {
// A pack package with actions and responses only (E16c3): plugin.json and actions.json, or a
// previews document instead when `actions` is empty.
QString writeActionPackage( const QString& folder, const QString& version,
                            const QByteArray& actions )
{
    QDir().mkpath( folder );
    auto manifest = packManifest( version );
    QJsonObject content;
    content[ actions.isEmpty() ? "previews" : "actions" ]
        = actions.isEmpty() ? "previews.json" : "actions.json";
    manifest[ "content" ] = content;
    const auto write = [ &folder ]( const QString& name, const QByteArray& bytes ) {
        QFile file( folder + '/' + name );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( bytes );
    };
    write( "plugin.json", QJsonDocument( manifest ).toJson() );
    write( actions.isEmpty() ? "previews.json" : "actions.json",
           actions.isEmpty() ? QByteArray( Previews ) : actions );
    return folder;
}

// The content folder's actions file.
QJsonObject contentActions( const QString& folder )
{
    return QJsonDocument::fromJson( readFile( folder + "/actions.json" ) ).object();
}

// The ids of a list of the content folder's actions file, in its order.
QList<int> contentIds( const QString& folder, const QString& list )
{
    QList<int> ids;
    for ( const auto& value : contentActions( folder ).value( list ).toArray() ) {
        ids.append( value.toObject().value( "id" ).toInt( -1 ) );
    }
    return ids;
}

// The actions file `SomeActions` with one action's field changed (or added).
QByteArray withAction( const QByteArray& actions, int id, const QString& key,
                       const QJsonValue& value )
{
    auto root = QJsonDocument::fromJson( actions ).object();
    auto list = root[ "actions" ].toArray();
    for ( int index = 0; index < list.size(); ++index ) {
        auto item = list.at( index ).toObject();
        if ( item.value( "id" ).toInt( -1 ) == id ) {
            item[ key ] = value;
            list[ index ] = item;
        }
    }
    root[ "actions" ] = list;
    return QJsonDocument( root ).toJson();
}
} // namespace

TEST_CASE( "Pack actions and responses install, edit and update through the local store",
           "[packmerge][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    const auto id = QStringLiteral( "org.example.pack" );
    const auto v1 = writeActionPackage( root.filePath( "v1" ), "1.0.0", SomeActions );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v1 } } ) )[ "state" ]
             == "completed" );
    const auto content = manager.packContentDirectory( id );
    CHECK( contentIds( content, "actions" ) == QList<int>{ 3, 7 } );
    CHECK( contentIds( content, "responses" ) == QList<int>{ 3 } );
    CHECK( manager.plugins().first().toMap()[ "packActions" ] == 2 );
    CHECK( manager.plugins().first().toMap()[ "packResponses" ] == 1 );

    // The actions and responses are items of the pack, one per id, and an order for each list.
    QStringList kinds;
    for ( const auto& file : manager.manage( "pack_files", id, {} ).payload[ "files" ].toList() ) {
        kinds.append( file.toMap()[ "kind" ].toString() + ':' + file.toMap()[ "item" ].toString() );
    }
    kinds.sort();
    CHECK( kinds
           == QStringList{ "action-order:actions/order.txt", "action:3", "action:7",
                           "response-order:responses/order.txt", "response:3" } );

    // Edited in place: the content folder's actions file follows, with a readable history.
    const auto status = QStringLiteral( "actions/7.json" );
    auto item = QJsonDocument::fromJson( manager.manage( "pack_read", id, { { "path", status } } )
                                             .payload[ "content" ]
                                             .toString()
                                             .toUtf8() )
                    .object();
    item[ "description" ] = "Mine";
    REQUIRE( edit( manager, status, item ).isEmpty() );
    CHECK( listed( contentActions( content ), "actions", 7 ).value( "description" ) == "Mine" );
    CHECK( manager.manage( "pack_history", id, { { "path", status } } )
               .payload[ "commits" ]
               .toList()
               .first()
               .toMap()[ "message" ]
           == "Edit action 'Status'" );
    auto response = QJsonDocument::fromJson(
                        manager.manage( "pack_read", id, { { "path", "responses/3.json" } } )
                            .payload[ "content" ]
                            .toString()
                            .toUtf8() )
                        .object();
    response[ "description" ] = "Mine too";
    REQUIRE( edit( manager, "responses/3.json", response ).isEmpty() );
    CHECK( manager.manage( "pack_history", id, { { "path", "responses/3.json" } } )
               .payload[ "commits" ]
               .toList()
               .first()
               .toMap()[ "message" ]
           == "Edit response 'After boot'" );
    // The id is the item's identity, and every item stays one CILogg reads.
    for ( const auto& otherId : { QJsonValue( 8 ), QJsonValue( "7" ), QJsonValue( 7.5 ) } ) {
        auto other = item;
        other[ "id" ] = otherId;
        CHECK( edit( manager, status, other )
               == "The action's id must stay the same (copy it to make another)." );
    }
    auto unnamed = item;
    unnamed[ "name" ] = "";
    CHECK( edit( manager, status, unnamed ).contains( "needs a name" ) );
    auto elsewhere = response;
    elsewhere[ "response" ] = QJsonObject{ { "action_id", 9 } };
    CHECK( edit( manager, "responses/3.json", elsewhere ).contains( "which is not in the file" ) );
    // An action a response runs is not removed; one no response runs can be, and reset brings
    // it back.
    CHECK( manager.manage( "pack_edit", id, { { "path", status }, { "remove", true } } )
               .error.contains( "which is not in the file" ) );
    REQUIRE(
        manager.manage( "pack_edit", id, { { "path", "responses/3.json" }, { "remove", true } } )
            .ok() );
    REQUIRE( manager.manage( "pack_edit", id, { { "path", status }, { "remove", true } } ).ok() );
    CHECK( contentIds( content, "actions" ) == QList<int>{ 3 } );
    CHECK( contentIds( content, "responses" ).isEmpty() );
    REQUIRE( manager.manage( "pack_reset", id, { { "path", status } } ).ok() );
    REQUIRE( manager.manage( "pack_reset", id, { { "path", "responses/3.json" } } ).ok() );
    CHECK( contentIds( content, "actions" ) == QList<int>{ 3, 7 } );
    CHECK( contentIds( content, "responses" ) == QList<int>{ 3 } );
    // The orders are edited as text, never deleted.
    REQUIRE(
        manager
            .manage( "pack_edit", id, { { "path", "actions/order.txt" }, { "content", "7\n3\n" } } )
            .ok() );
    CHECK( contentIds( content, "actions" ) == QList<int>{ 7, 3 } );
    CHECK( manager.manage( "pack_history", id, { { "path", "actions/order.txt" } } )
               .payload[ "commits" ]
               .toList()
               .first()
               .toMap()[ "message" ]
           == "Edit the action order" );
    for ( const auto* order : { "actions/order.txt", "responses/order.txt" } ) {
        CHECK( !manager.manage( "pack_edit", id, { { "path", order }, { "remove", true } } ).ok() );
    }
    REQUIRE( manager.manage( "pack_reset", id, { { "path", "actions/order.txt" } } ).ok() );
    // The reset took the edit with it: edited again for the updates below.
    REQUIRE( edit( manager, status, item ).isEmpty() );

    // A new version that renames the edited action and adds one merges field by field.
    auto v2Json = withAction( SomeActions, 7, "name", "Status report" );
    {
        auto root2 = QJsonDocument::fromJson( v2Json ).object();
        auto actions = root2[ "actions" ].toArray();
        actions.append( QJsonObject{
            { "id", 12 },
            { "order", 2 },
            { "name", "Version" },
            { "sequence", QJsonObject{ { "type", "string" }, { "value", "version\r\n" } } } } );
        root2[ "actions" ] = actions;
        v2Json = QJsonDocument( root2 ).toJson();
    }
    const auto v2 = writeActionPackage( root.filePath( "v2" ), "1.1.0", v2Json );
    REQUIRE( finished( manager, manager.manage( "install", {}, { { "path", v2 } } ) )[ "state" ]
             == "completed" );
    CHECK( contentIds( content, "actions" ) == QList<int>{ 3, 7, 12 } ); // the new version's order
    const auto merged = listed( contentActions( content ), "actions", 7 );
    CHECK( merged.value( "description" ) == "Mine" );
    CHECK( merged.value( "name" ) == "Status report" );

    // A version that changes the edited field: a conflict on that field of that action.
    const auto v3Json = withAction( v2Json, 7, "description", "Theirs" );
    const auto v3 = writeActionPackage( root.filePath( "v3" ), "1.2.0", v3Json );
    const auto refused = finished( manager, manager.manage( "install", {}, { { "path", v3 } } ) );
    REQUIRE( refused[ "state" ] == "failed" );
    const auto conflicts = refused[ "result" ].toMap()[ "conflicts" ].toList();
    REQUIRE( conflicts.size() == 1 );
    CHECK( conflicts.first().toMap()[ "kind" ] == "action" );
    CHECK( conflicts.first().toMap()[ "item" ] == "7" );
    CHECK( conflicts.first().toMap()[ "field" ] == "description" );
    const auto revision = refused[ "result" ].toMap()[ "revision" ];
    // A resolution never changes the id, and the whole action keeps it.
    for ( const auto& resolution :
          { QVariantMap{ { "path", status }, { "field", "id" }, { "value", 8 } },
            QVariantMap{ { "path", status },
                         { "field", QString{} },
                         { "value", QVariantMap{ { "id", 8 }, { "name", "X" } } } } } ) {
        const auto job
            = finished( manager, manager.manage( "install", {},
                                                 { { "path", v3 },
                                                   { "resolutions", QVariantList{ resolution } },
                                                   { "revision", revision } } ) );
        CHECK( job[ "error" ].toString().startsWith( "Invalid merge resolution" ) );
    }
    // A resolution that would make an action CILogg cannot read is refused with the reason.
    const auto unreadable = finished(
        manager, manager.manage(
                     "install", {},
                     { { "path", v3 },
                       { "resolutions",
                         QVariantList{ QVariantMap{ { "path", status },
                                                    { "field", QString{} },
                                                    { "value", QVariantMap{ { "id", 7 } } } } } },
                       { "revision", revision } } ) );
    CHECK( unreadable[ "error" ].toString().contains( "needs a name" ) );
    REQUIRE( finished( manager,
                       manager.manage(
                           "install", {},
                           { { "path", v3 },
                             { "resolutions", QVariantList{ QVariantMap{ { "path", status },
                                                                         { "field", "description" },
                                                                         { "value", "Both" } } } },
                             { "revision", revision } } ) )[ "state" ]
             == "completed" );
    CHECK( listed( contentActions( content ), "actions", 7 ).value( "description" ) == "Both" );

    // A version without an actions file takes the actions and responses with it, edits too, and
    // says so.
    const auto v4 = writeActionPackage( root.filePath( "v4" ), "2.0.0", {} );
    const auto job = finished( manager, manager.manage( "install", {}, { { "path", v4 } } ) );
    REQUIRE( job[ "state" ] == "completed" );
    CHECK( job[ "result" ].toMap()[ "droppedEdits" ].toStringList().contains( "action 7" ) );
    for ( const auto& file : manager.manage( "pack_files", id, {} ).payload[ "files" ].toList() ) {
        CHECK( !file.toMap()[ "kind" ].toString().startsWith( "action" ) );
        CHECK( !file.toMap()[ "kind" ].toString().startsWith( "response" ) );
    }
    CHECK( !QFileInfo::exists( content + "/actions.json" ) );
    CHECK( manager.plugins().first().toMap()[ "packActions" ] == 0 );
    CHECK( manager.plugins().first().toMap()[ "packResponses" ] == 0 );
}

TEST_CASE( "A pack store from before E16c reads its filters and highlighters files as items",
           "[packmerge][E16]" )
{
    // E16a2 kept a pack's filters and highlighters files as they are, under files/ (Codex review
    // of #169): read as items, those files are never lost, also when an edit or a repair writes
    // the content folder again.
    auto manifest = packManifest();
    manifest[ "content" ] = QJsonObject{ { "filters", "conf/filters.conf" },
                                         { "highlights", "conf/highlights.conf" } };
    const auto filters = filtersConf( SomeFilters );
    const auto highlights = highlightsConf( SomeSets );
    const PackFiles legacy{ { "files/conf/filters.conf", filters },
                            { "files/conf/highlights.conf", highlights } };
    auto items = packcanonical::fromFilters( filters );
    const auto sets = packcanonical::fromHighlights( highlights );
    for ( auto set = sets.cbegin(); set != sets.cend(); ++set ) {
        items.insert( set.key(), set.value() );
    }
    REQUIRE( items.contains( "filters/f-one.json" ) );
    REQUIRE( items.contains( "highlights/s-one.set" ) );

    SECTION( "the conversion" )
    {
        CHECK( packcanonical::fromLegacyDocuments( legacy, manifest ) == items );
        // Items already there win; other files and other kinds are left as they are.
        auto both = items;
        both.insert( "files/conf/filters.conf", "[stale]\n" );
        both.insert( "files/lua/a.lua", "-- a\n" );
        auto expected = items;
        expected.insert( "files/lua/a.lua", "-- a\n" );
        CHECK( packcanonical::fromLegacyDocuments( both, manifest ) == expected );
        // A file that is not exactly what Export writes stays as it is.
        const auto noIds = filtersConf( { { QString{}, "Old", "old", true } } );
        const PackFiles odd{ { "files/conf/filters.conf", noIds } };
        CHECK( packcanonical::fromLegacyDocuments( odd, manifest ) == odd );
        // A manifest without those documents keeps the files.
        CHECK( packcanonical::fromLegacyDocuments( legacy, packManifest() ) == legacy );
    }

    SECTION( "an unrelated edit and an update keep them" )
    {
        QTemporaryDir root;
        REQUIRE( root.isValid() );
        PackStore store( root.path(), "org.example.pack" );
        REQUIRE( store.install( legacy, manifest, "1.0.0", "Team packs" ).isEmpty() );
        CHECK( store.files() == items );
        CHECK( store.upstreamFiles() == items );
        CHECK( store.localEdits().isEmpty() );
        const auto content = store.contentDirectory() + "/conf";
        CHECK( contentFilters( content ).size() == 2 );
        CHECK( contentSets( content ).size() == 3 ); // two sets and the order

        // An edit of one set rewrites the content folder: the filters and the other set stay.
        const auto one = QStringLiteral( "highlights/s-one.set" );
        auto edited = store.files().value( one );
        edited.replace( "\"ERROR\"", "\"ERROR|PANIC\"" );
        REQUIRE( store.commitEdit( one, edited, "Edit highlighter set 'Errors'" ).isEmpty() );
        CHECK( store.localEdits() == QStringList{ one } );
        CHECK( contentFilters( content ).size() == 2 );
        CHECK( contentSets( content ).size() == 3 );
        CHECK( contentSets( content ).value( one ).contains( "ERROR|PANIC" ) );

        // A new version in the item form merges with that edit item by item.
        auto next = items;
        next.insert( "highlights/s-three.set",
                     packcanonical::fromHighlights( highlightsConf( { { "s-three", "New", {} } } ) )
                         .value( "highlights/s-three.set" ) );
        next.insert( "highlights/order.txt", "s-one\ns-two\ns-three\n" );
        const auto plan = store.planUpdate( next, MergeStrategy::Fail );
        CHECK( plan.conflicts.isEmpty() );
        CHECK( plan.merged.value( one ).contains( "ERROR|PANIC" ) );
        CHECK( plan.merged.contains( "highlights/s-three.set" ) );
        CHECK( !plan.merged.contains( "files/conf/highlights.conf" ) );
    }

    SECTION( "a local edit made to the file itself" )
    {
        QTemporaryDir root;
        REQUIRE( root.isValid() );
        PackStore store( root.path(), "org.example.pack" );
        REQUIRE( store.install( legacy, manifest, "1.0.0", "Team packs" ).isEmpty() );
        // E16a2 committed an edit of the filters file as the whole file.
        auto mine = legacy;
        auto rows = SomeFilters;
        rows[ 0 ].filter = "ERROR|PANIC";
        mine.insert( "files/conf/filters.conf", filtersConf( rows ) );
        REQUIRE( store.applyUpdate( legacy, mine, manifest, "1.0.0", "Team packs" ).isEmpty() );
        CHECK( store.localEdits() == QStringList{ "filters/f-one.json" } );
        CHECK( store.files().value( "filters/f-one.json" ).contains( "ERROR|PANIC" ) );
        CHECK( contentFilters( store.contentDirectory() + "/conf" )
                   .first()
                   .value( "filter" )
                   .toString()
               == "ERROR|PANIC" );
    }

    SECTION( "a file that is not an export" )
    {
        QTemporaryDir root;
        REQUIRE( root.isValid() );
        PackStore store( root.path(), "org.example.pack" );
        const auto noIds = filtersConf( { { QString{}, "Old", "old", true } } );
        auto odd = legacy;
        odd.insert( "files/conf/filters.conf", noIds );
        REQUIRE( store.install( odd, manifest, "1.0.0", "Team packs" ).isEmpty() );
        CHECK( store.files().value( "files/conf/filters.conf" ) == noIds );
        // The content folder gets it back unchanged, never an empty filters file.
        CHECK( readFile( store.contentDirectory() + "/conf/filters.conf" ) == noIds );
        CHECK( contentSets( store.contentDirectory() + "/conf" ).size() == 3 );
    }
}

TEST_CASE( "A pack store from before E16c3 reads its actions file as items", "[packmerge][E16]" )
{
    // Until E16c3 a pack's actions file was kept as it is, under files/: read as items, it is never
    // lost, also when an edit or a repair writes the content folder again.
    auto manifest = packManifest();
    manifest[ "content" ]
        = QJsonObject{ { "previews", "previews.json" }, { "actions", "serial/actions.json" } };
    const QByteArray actions( SomeActions );
    auto legacy = packcanonical::fromPreviews( Previews );
    legacy.insert( "files/serial/actions.json", actions );
    auto items = packcanonical::fromPreviews( Previews );
    const auto converted = packcanonical::fromActions( actions );
    for ( auto item = converted.cbegin(); item != converted.cend(); ++item ) {
        items.insert( item.key(), item.value() );
    }
    REQUIRE( items.contains( "actions/7.json" ) );

    SECTION( "the conversion" )
    {
        CHECK( packcanonical::fromLegacyDocuments( legacy, manifest ) == items );
        // Items already there win.
        auto both = items;
        both.insert( "files/serial/actions.json", "{}" );
        CHECK( packcanonical::fromLegacyDocuments( both, manifest ) == items );
        // A file that is not exactly what Export writes (here: with a comment) stays as it is.
        auto odd = legacy;
        odd.insert( "files/serial/actions.json", "// mine\n" + actions );
        CHECK( packcanonical::fromLegacyDocuments( odd, manifest ) == odd );
    }

    SECTION( "an unrelated edit keeps them" )
    {
        QTemporaryDir root;
        REQUIRE( root.isValid() );
        PackStore store( root.path(), "org.example.pack" );
        REQUIRE( store.install( legacy, manifest, "1.0.0", "Team packs" ).isEmpty() );
        CHECK( store.files() == items );
        const auto content = store.contentDirectory() + "/serial";
        CHECK( contentIds( content, "actions" ) == QList<int>{ 3, 7 } );
        const auto one = QStringLiteral( "actions/3.json" );
        auto edited = QJsonDocument::fromJson( store.files().value( one ) ).object();
        edited[ "description" ] = "Mine";
        REQUIRE( store.commitEdit( one, packcanonical::itemBytes( edited ), "Edit action 'Reset'" )
                     .isEmpty() );
        CHECK( listed( contentActions( content ), "actions", 3 ).value( "description" ) == "Mine" );
        CHECK( contentIds( content, "responses" ) == QList<int>{ 3 } );
    }

    SECTION( "a file that is not an export" )
    {
        QTemporaryDir root;
        REQUIRE( root.isValid() );
        PackStore store( root.path(), "org.example.pack" );
        auto odd = legacy;
        const auto commented = "// mine\n" + actions;
        odd.insert( "files/serial/actions.json", commented );
        REQUIRE( store.install( odd, manifest, "1.0.0", "Team packs" ).isEmpty() );
        CHECK( store.files().value( "files/serial/actions.json" ) == commented );
        // The content folder gets it back unchanged, never an empty actions file.
        CHECK( readFile( store.contentDirectory() + "/serial/actions.json" ) == commented );
    }
}
