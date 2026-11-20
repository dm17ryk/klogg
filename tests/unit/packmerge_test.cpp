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
#include <QStandardPaths>
#include <QTemporaryDir>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <algorithm>

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
    // Every file the manifest lists is carried, also actions, highlights and filters.
    auto withFilters = packManifest();
    auto declared = withFilters[ "content" ].toObject();
    declared[ "filters" ] = "filters/predefined.json";
    withFilters[ "content" ] = declared;
    CHECK( packcanonical::listedFiles( withFilters )
           == QStringList{ "files/lua/a.lua", "files/filters/predefined.json" } );

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
    const auto asText = finished(
        manager, manager.manage( "install", {},
                                 { { "path", v2 },
                                   { "resolutions",
                                     QVariantList{ QVariantMap{ { "path", lua }, { "text", "x\n" } } } } } ) );
    CHECK( asText[ "state" ] == "failed" );
    // A file the new version's manifest lists cannot be removed by a resolution: refused, not
    // put back behind the user's back.
    const auto removed = finished(
        manager, manager.manage( "install", {},
                                 { { "path", v2 },
                                   { "resolutions",
                                     QVariantList{ QVariantMap{ { "path", lua }, { "remove", true } } } } } ) );
    CHECK( removed[ "state" ] == "failed" );
    CHECK( removed[ "error" ].toString().contains( "Invalid merge resolution" ) );
    const auto whole = finished(
        manager, manager.manage( "install", {},
                                 { { "path", v2 },
                                   { "resolutions",
                                     QVariantList{ QVariantMap{ { "path", lua }, { "take", "theirs" } } } } } ) );
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
        manager, manager.manage( "install", {}, { { "path", v2 }, { "resolutions", resolutions } } ) );
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
    CHECK( whole[ "mine" ].toString().contains( "\"mine\"" ) );
    CHECK( whole[ "theirs" ].toString().contains( "\"theirs\"" ) );
    const auto revision = result[ "revision" ].toString();
    REQUIRE( !revision.isEmpty() );
    const QVariantList takeTheirs{ QVariantMap{
        { "path", second },
        { "field", QString{} },
        { "value", QJsonDocument::fromJson( whole[ "theirs" ].toString().toUtf8() ).object().toVariantMap() } } };

    // Edited again after the merge was shown: the choices are not applied, the merge is asked
    // for again with the new revision.
    edit( "newer" );
    const auto stale = finished( manager, manager.manage( "install", {},
                                                           { { "path", v2 },
                                                             { "resolutions", takeTheirs },
                                                             { "revision", revision } } ) );
    REQUIRE( stale[ "state" ] == "failed" );
    CHECK( stale[ "error" ].toString().contains( "edited after the merge was shown" ) );
    CHECK( stale[ "result" ].toMap()[ "needsMerge" ].toBool() );
    const auto current = stale[ "result" ].toMap()[ "revision" ].toString();
    CHECK( current != revision );
    const auto content = manager.packContentDirectory( id );
    CHECK( readFile( content + "/previews.json" ).contains( "\"newer\"" ) );

    // With the current revision the whole-item choice settles all 40 fields.
    const auto applied = finished( manager, manager.manage( "install", {},
                                                             { { "path", v2 },
                                                               { "resolutions", takeTheirs },
                                                               { "revision", current } } ) );
    INFO( applied[ "error" ].toString().toStdString() );
    REQUIRE( applied[ "state" ] == "completed" );
    CHECK( readFile( content + "/previews.json" ).contains( "\"theirs\"" ) );
    CHECK( !readFile( content + "/previews.json" ).contains( "\"newer\"" ) );
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
                                                     { { "path", v3 }, { "resolutions", resolutions } } ) )
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
                                         { { "path", v3 }, { "resolutions", QVariantList{ resolution } } } ) );
            CHECK( job[ "state" ] == "failed" );
            CHECK( job[ "error" ].toString().startsWith( "Invalid merge resolution" ) );
        }
        CHECK( !QFileInfo::exists( root.filePath( "data/escaped.lua" ) ) );
        CHECK( previewRegex( content, "Second" ) == "^MINE$" );
    }
    CHECK( !manager.manage( "install", {}, { { "path", v3 }, { "strategy", "both" } } ).ok() );

    // Reset to Pack Version, then removing the pack removes its store.
    REQUIRE( manager.manage( "pack_reset", id, {} ).ok() );
    CHECK( manager.manage( "pack_edits", id, {} ).payload[ "files" ].toList().isEmpty() );
    REQUIRE( manager.manage( "remove", id, {} ).ok() );
    CHECK( !QFileInfo::exists( root.filePath( "data/pack-store/org.example.pack.git" ) ) );
}
