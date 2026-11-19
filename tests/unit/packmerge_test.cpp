// The local pack store, its canonical form and the three-way merge, slice E16a2
// (docs/plugin-repositories.md 4.6).
#include <catch2/catch.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "packcanonical.h"
#include "packmerge.h"
#include "packstore.h"

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
} // namespace

TEST_CASE( "Pack items have a canonical one-item-per-file form", "[packmerge][E16]" )
{
    CHECK( packcanonical::encodeName( "First Frame" ) == "%46irst%20%46rame" );
    CHECK( packcanonical::encodeName( "a/b" ) == "a%2Fb" );
    CHECK( packcanonical::encodeName( ".." ) == "%2E%2E" );
    const auto unicode = QString::fromUtf8( "\xce\x94 frame/1" );
    CHECK( packcanonical::decodeName( packcanonical::encodeName( unicode ) ) == unicode );
    CHECK( packcanonical::encodeName( "A" ) != packcanonical::encodeName( "a" ) );

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
