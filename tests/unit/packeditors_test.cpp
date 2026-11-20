// The pack item editor, Pack Files, the merge window and the external merge tool, slice E16a2
// (docs/plugin-repositories.md 4.6).
#include <catch2/catch.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <functional>

#include "packcanonical.h"
#include "packeditors.h"
#include "pluginmanager.h"
#include "pluginupdatenotices.h"
#include "previewmanager.h"
#include "previewpackregistry.h"

using namespace cilogg::plugins;

namespace {
constexpr auto PackId = "org.example.pack";
const auto SecondPath = QStringLiteral( "previews/%53econd.json" );

constexpr auto Previews = R"json({ "version": 1,
  "previews": [ { "name": "Second", "regex": "^B$", "format": "fields", "fields": [] } ] })json";

QJsonObject packManifest()
{
    return { { "id", PackId },
             { "name", "Example pack" },
             { "version", "1.0.0" },
             { "protocol_version", 1 },
             { "kind", "config-pack" },
             { "pack_format", 1 },
             { "content",
               QJsonObject{ { "previews", "previews.json" },
                            { "lua", QJsonObject{ { "folder", "lua" },
                                                  { "load", QJsonArray{ "a.lua" } } } } } } };
}

QString writePackage( const QString& folder )
{
    REQUIRE( QDir().mkpath( folder + "/lua" ) );
    const auto write = [ &folder ]( const QString& name, const QByteArray& bytes ) {
        QFile file( folder + '/' + name );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( bytes );
    };
    write( "plugin.json", QJsonDocument( packManifest() ).toJson() );
    write( "previews.json", Previews );
    write( "lua/a.lua", "-- a\n" );
    return folder;
}

void installPack( PluginManager& manager, const QString& folder )
{
    const auto started = manager.manage( "install", {}, { { "path", writePackage( folder ) } } );
    REQUIRE( started.ok() );
    const auto jobId = started.payload.value( "jobId" ).toString();
    QElapsedTimer timer;
    timer.start();
    while ( timer.elapsed() < 20000 ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
        const auto state = manager.job( jobId ).payload.value( "state" ).toString();
        if ( state == "completed" ) {
            return;
        }
        REQUIRE( ( state != "failed" && state != "cancelled" ) );
    }
    FAIL( "the install did not finish" );
}

// The GUI's executor: Commander's plugin_manage on the manager.
pluginsui::Executor executorFor( PluginManager& manager )
{
    return [ &manager ]( const CommanderRequest& request ) {
        const auto result = manager.manage( request.pluginOperation, request.pluginId,
                                            request.pluginArguments );
        CommanderResult answer;
        answer.payload = result.payload;
        if ( !result.ok() ) {
            answer.code = CommanderResultCode::ExecutionFailed;
            answer.message = result.error;
        }
        return answer;
    };
}

// Runs `step` once a modal window matching `match` is open (dialogs here run their own loop).
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

void whenOpen( const QString& objectName, std::function<void( QWidget* )> step )
{
    whenModal( [ objectName ]( QWidget* widget ) { return widget->objectName() == objectName; },
               std::move( step ) );
}

void closeNextMessageBox( QString* text )
{
    whenModal( []( QWidget* widget ) { return qobject_cast<QMessageBox*>( widget ) != nullptr; },
               [ text ]( QWidget* widget ) {
                   auto* box = qobject_cast<QMessageBox*>( widget );
                   *text = box->text();
                   box->reject();
               } );
}

QPushButton* buttonIn( QWidget* window, const char* name )
{
    auto* found = window->findChild<QPushButton*>( name );
    REQUIRE( found );
    return found;
}

// A failed update's job with a conflict in a preview field and one in a Lua file.
QVariantMap conflictJob()
{
    const auto lua = packcanonical::filePath( "lua/a.lua" );
    const QVariantList conflicts{
        QVariantMap{ { "path", SecondPath }, { "kind", "preview" }, { "item", "Second" },
                     { "field", "regex" }, { "base", "\"^B$\"" }, { "mine", "\"^MINE$\"" },
                     { "theirs", "\"^THEIRS$\"" } },
        QVariantMap{ { "path", lua }, { "kind", "file" }, { "item", "lua/a.lua" }, { "field", "" },
                     { "base", "1\n" }, { "mine", "2\n" }, { "theirs", "3\n" } } };
    const QVariantMap items{
        { SecondPath, QVariantMap{ { "mine", "{}" }, { "theirs", "{}" }, { "inMine", true },
                                   { "inTheirs", true } } },
        { lua, QVariantMap{ { "mine", "2\n" }, { "theirs", "3\n" }, { "inMine", true },
                            { "inTheirs", true } } } };
    return { { "state", "failed" },
             { "result", QVariantMap{ { "needsMerge", true },
                                      { "conflicts", conflicts },
                                      { "items", items } } } };
}
} // namespace

TEST_CASE( "The pack item editor saves an edit through Commander", "[packeditors][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    installPack( manager, root.filePath( "package" ) );
    const auto executor = executorFor( manager );

    // A renamed item is refused with a message, and the editor stays open.
    QString refusal;
    whenOpen( "packItemEditor", [ &refusal ]( QWidget* editor ) {
        auto* text = editor->findChild<QPlainTextEdit*>( "packItemEditorText" );
        REQUIRE( text );
        CHECK( text->toPlainText().contains( "^B$" ) );
        text->setPlainText( text->toPlainText().replace( "\"Second\"", "\"Other\"" ) );
        closeNextMessageBox( &refusal );
        buttonIn( editor, "packItemEditorSave" )->click();
        CHECK( editor->isVisible() );
        buttonIn( editor, "packItemEditorClose" )->click();
    } );
    pluginsui::openPackItemEditor( nullptr, executor, PackId, "Example pack", SecondPath );
    CHECK( refusal.contains( "name must stay the same" ) );
    CHECK( manager.manage( "pack_edits", PackId, {} ).payload[ "files" ].toList().isEmpty() );

    // Save commits the edit.
    whenOpen( "packItemEditor", []( QWidget* editor ) {
        auto* text = editor->findChild<QPlainTextEdit*>( "packItemEditorText" );
        text->setPlainText( text->toPlainText().replace( "^B$", "^EDITED$" ) );
        buttonIn( editor, "packItemEditorSave" )->click();
    } );
    pluginsui::openPackItemEditor( nullptr, executor, PackId, "Example pack", SecondPath );
    CHECK( manager.manage( "pack_read", PackId, { { "path", SecondPath } } )
               .payload[ "content" ]
               .toString()
               .contains( "^EDITED$" ) );

    // Pack Files lists the items with their state; the previews settings (version) are listed
    // but not edited (the pack operations refuse them).
    QStringList rows;
    bool settingsEditable = true;
    bool itemEditable = false;
    whenOpen( "packFilesDialog", [ &rows, &settingsEditable, &itemEditable ]( QWidget* dialog ) {
        auto* table = dialog->findChild<QTableWidget*>( "packFilesTable" );
        REQUIRE( table );
        for ( int row = 0; row < table->rowCount(); ++row ) {
            rows << table->item( row, 0 )->text() + '=' + table->item( row, 2 )->text();
            table->setCurrentCell( row, 0 );
            const bool enabled = buttonIn( dialog, "packFilesEdit" )->isEnabled();
            if ( table->item( row, 1 )->text() == "Previews settings" ) {
                settingsEditable = enabled;
            }
            if ( table->item( row, 0 )->text() == "Second" ) {
                itemEditable = enabled;
            }
        }
        buttonIn( dialog, "packFilesClose" )->click();
    } );
    pluginsui::openPackFiles( nullptr, executor, PackId, "Example pack" );
    CHECK( rows.contains( "Second=Edited" ) );
    CHECK( rows.contains( "lua/a.lua=As in the pack" ) );
    REQUIRE( rows.contains( "previews/meta.json=As in the pack" ) );
    CHECK_FALSE( settingsEditable );
    CHECK( itemEditable );
}

TEST_CASE( "The merge window resolves fields and files before anything changes",
           "[packeditors][E16]" )
{
    QVariantList applied;
    bool called = false;
    const auto apply = [ &applied, &called ]( const QVariantList& resolutions,
                                              const pluginsui::AfterPackMerge& afterInstall ) {
        applied = resolutions;
        called = true;
        CHECK_FALSE( afterInstall ); // no Keep Both: nothing to do after the update
    };
    CHECK( pluginsui::needsPackMerge( conflictJob() ) );

    SECTION( "Cancel changes nothing" )
    {
        whenOpen( "packMergeDialog",
                  []( QWidget* dialog ) { buttonIn( dialog, "packMergeCancel" )->click(); } );
        CHECK_FALSE( pluginsui::openPackMerge( nullptr, "Example pack", conflictJob(), apply ) );
        CHECK_FALSE( called );
    }
    SECTION( "a field and a file resolved" )
    {
        QString markers;
        whenOpen( "packMergeDialog", [ &markers ]( QWidget* dialog ) {
            auto* tree = dialog->findChild<QTreeWidget*>( "packMergeTree" );
            REQUIRE( tree );
            REQUIRE( tree->topLevelItemCount() == 1 );
            auto* field = tree->topLevelItem( 0 )->child( 0 );
            CHECK( field->text( 4 ) == "\"^MINE$\"" ); // Keep Mine is the default
            tree->setCurrentItem( field );
            buttonIn( dialog, "packMergeTakeTheirs" )->click();
            CHECK( field->text( 4 ) == "\"^THEIRS$\"" );

            // The file's result starts as the diff3 merge; Apply refuses it with markers.
            auto* result = dialog->findChild<QPlainTextEdit*>( "packMergeResult" );
            REQUIRE( result );
            CHECK( result->toPlainText().contains( "<<<<<<< your edit" ) );
            closeNextMessageBox( &markers );
            buttonIn( dialog, "packMergeApply" )->click();
            CHECK( dialog->isVisible() );
            buttonIn( dialog, "packMergeUseMine" )->click();
            CHECK( result->toPlainText() == "2\n" );
            buttonIn( dialog, "packMergeApply" )->click();
        } );
        REQUIRE( pluginsui::openPackMerge( nullptr, "Example pack", conflictJob(), apply ) );
        CHECK( markers.contains( "conflict markers" ) );
        REQUIRE( applied.size() == 2 );
        // Settled conflict by conflict, the result is a text (a whole side is only what a
        // whole-side button chose).
        CHECK( applied.at( 0 ).toMap()
               == QVariantMap{ { "path", packcanonical::filePath( "lua/a.lua" ) }, { "text", "2\n" } } );
        CHECK( applied.at( 1 ).toMap()
               == QVariantMap{ { "path", SecondPath }, { "field", "regex" }, { "value", "^THEIRS$" } } );
    }
    SECTION( "a file that is not UTF-8 is resolved whole" )
    {
        auto job = conflictJob();
        auto result = job[ "result" ].toMap();
        auto items = result[ "items" ].toMap();
        auto lua = items[ packcanonical::filePath( "lua/a.lua" ) ].toMap();
        lua[ "utf8" ] = false;
        // Different bytes whose readings are the same: the button chosen decides, not the text.
        lua[ "mine" ] = QStringLiteral( "�\n" );
        lua[ "theirs" ] = QStringLiteral( "�\n" );
        items[ packcanonical::filePath( "lua/a.lua" ) ] = lua;
        result[ "items" ] = items;
        job[ "result" ] = result;
        whenOpen( "packMergeDialog", []( QWidget* dialog ) {
            auto* text = dialog->findChild<QPlainTextEdit*>( "packMergeResult" );
            REQUIRE( text );
            CHECK( text->isReadOnly() );
            CHECK_FALSE( buttonIn( dialog, "packMergeUseMine" )->isEnabled() );
            CHECK_FALSE( buttonIn( dialog, "packMergeTool" )->isEnabled() );
            buttonIn( dialog, "packMergeWholeTheirs" )->click();
            buttonIn( dialog, "packMergeApply" )->click();
        } );
        REQUIRE( pluginsui::openPackMerge( nullptr, "Example pack", job, apply ) );
        CHECK( applied.at( 0 ).toMap()
               == QVariantMap{ { "path", packcanonical::filePath( "lua/a.lua" ) },
                               { "take", "theirs" } } );
        // Untouched, the whole file stays yours.
        whenOpen( "packMergeDialog", []( QWidget* dialog ) {
            buttonIn( dialog, "packMergeApply" )->click();
        } );
        REQUIRE( pluginsui::openPackMerge( nullptr, "Example pack", job, apply ) );
        CHECK( applied.at( 0 ).toMap()
               == QVariantMap{ { "path", packcanonical::filePath( "lua/a.lua" ) },
                               { "take", "mine" } } );
    }
    SECTION( "a file with CRLF line endings keeps them" )
    {
        auto job = conflictJob();
        auto result = job[ "result" ].toMap();
        auto conflicts = result[ "conflicts" ].toList();
        auto file = conflicts[ 1 ].toMap();
        file[ "base" ] = "1\r\n";
        conflicts[ 1 ] = file;
        result[ "conflicts" ] = conflicts;
        auto items = result[ "items" ].toMap();
        auto lua = items[ packcanonical::filePath( "lua/a.lua" ) ].toMap();
        lua[ "mine" ] = "2\r\n";
        lua[ "theirs" ] = "3\r\n";
        items[ packcanonical::filePath( "lua/a.lua" ) ] = lua;
        result[ "items" ] = items;
        job[ "result" ] = result;
        // A whole side is sent as that side (the editor shows it with LF).
        whenOpen( "packMergeDialog", []( QWidget* dialog ) {
            buttonIn( dialog, "packMergeWholeTheirs" )->click();
            buttonIn( dialog, "packMergeApply" )->click();
        } );
        REQUIRE( pluginsui::openPackMerge( nullptr, "Example pack", job, apply ) );
        CHECK( applied.at( 0 ).toMap()
               == QVariantMap{ { "path", packcanonical::filePath( "lua/a.lua" ) },
                               { "take", "theirs" } } );
        // A text result gets the file's CRLF back.
        whenOpen( "packMergeDialog", []( QWidget* dialog ) {
            buttonIn( dialog, "packMergeUseMine" )->click();
            buttonIn( dialog, "packMergeApply" )->click();
        } );
        REQUIRE( pluginsui::openPackMerge( nullptr, "Example pack", job, apply ) );
        CHECK( applied.at( 0 ).toMap()
               == QVariantMap{ { "path", packcanonical::filePath( "lua/a.lua" ) },
                               { "text", "2\r\n" } } );
    }
    SECTION( "a file too large to show is resolved whole" )
    {
        auto job = conflictJob();
        auto result = job[ "result" ].toMap();
        auto items = result[ "items" ].toMap();
        auto lua = items[ packcanonical::filePath( "lua/a.lua" ) ].toMap();
        lua.remove( "mine" );
        lua.remove( "theirs" );
        lua[ "omitted" ] = true;
        items[ packcanonical::filePath( "lua/a.lua" ) ] = lua;
        result[ "items" ] = items;
        job[ "result" ] = result;
        bool explained = false;
        whenOpen( "packMergeDialog", [ &explained ]( QWidget* dialog ) {
            explained = dialog->findChild<QLabel*>( "packMergeOmitted" ) != nullptr;
            CHECK( dialog->findChild<QPlainTextEdit*>( "packMergeResult" )->isReadOnly() );
            buttonIn( dialog, "packMergeWholeTheirs" )->click();
            buttonIn( dialog, "packMergeApply" )->click();
        } );
        REQUIRE( pluginsui::openPackMerge( nullptr, "Example pack", job, apply ) );
        CHECK( explained );
        CHECK( applied.at( 0 ).toMap()
               == QVariantMap{ { "path", packcanonical::filePath( "lua/a.lua" ) },
                               { "take", "theirs" } } );
    }
    SECTION( "Keep Mine for All; a file the manifest lists is never removed" )
    {
        bool removable = true;
        whenOpen( "packMergeDialog", [ &removable ]( QWidget* dialog ) {
            buttonIn( dialog, "packMergeAllTheirs" )->click();
            buttonIn( dialog, "packMergeAllMine" )->click();
            removable = dialog->findChild<QCheckBox*>( "packMergeRemove" ) != nullptr;
            buttonIn( dialog, "packMergeWholeTheirs" )->click();
            buttonIn( dialog, "packMergeApply" )->click();
        } );
        REQUIRE( pluginsui::openPackMerge( nullptr, "Example pack", conflictJob(), apply ) );
        CHECK_FALSE( removable );
        REQUIRE( applied.size() == 2 );
        CHECK( applied.at( 0 ).toMap()
               == QVariantMap{ { "path", packcanonical::filePath( "lua/a.lua" ) }, { "take", "theirs" } } );
        CHECK( applied.at( 1 ).toMap().value( "value" ) == "^MINE$" );
    }
}

TEST_CASE( "Keep Both copies your preview only once the update is installed", "[packeditors][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    installPack( manager, root.filePath( "package" ) );
    auto& registry = PreviewPackRegistry::instance();
    registry.attach( &manager );
    REQUIRE( PreviewManager::instance().clearAll() );
    auto job = conflictJob();
    job[ "pluginId" ] = PackId;
    const auto keepBoth = []( QWidget* dialog ) {
        auto* tree = dialog->findChild<QTreeWidget*>( "packMergeTree" );
        REQUIRE( tree );
        tree->setCurrentItem( tree->topLevelItem( 0 ) );
        buttonIn( dialog, "packMergeKeepBoth" )->click();
        buttonIn( dialog, "packMergeWholeMine" )->click();
        buttonIn( dialog, "packMergeApply" )->click();
    };

    pluginsui::AfterPackMerge afterInstall;
    whenOpen( "packMergeDialog", keepBoth );
    REQUIRE( pluginsui::openPackMerge(
        nullptr, "Example pack", job,
        [ &afterInstall ]( const QVariantList& resolutions, const pluginsui::AfterPackMerge& after ) {
            // The pack's preview becomes the new version as a whole.
            CHECK( resolutions.at( 1 ).toMap().value( "path" ) == SecondPath );
            CHECK( resolutions.at( 1 ).toMap().value( "field" ).toString().isEmpty() );
            afterInstall = after;
        } ) );
    REQUIRE( afterInstall );
    // Nothing is copied before the update is installed: a failed update leaves no copy.
    CHECK_FALSE( PreviewManager::instance().findByName( "Second (mine)" ) );
    CHECK( afterInstall().isEmpty() );
    const auto* copy = PreviewManager::instance().findByName( "Second (mine)" );
    REQUIRE( copy );
    CHECK( copy->regex == "^B$" ); // yours, as it was when Apply was pressed

    // A copy that could not be made stops Apply, before anything changes.
    QString refusal;
    bool stillOpen = false;
    whenOpen( "packMergeDialog", [ &refusal, &stillOpen ]( QWidget* dialog ) {
        closeNextMessageBox( &refusal );
        auto* tree = dialog->findChild<QTreeWidget*>( "packMergeTree" );
        tree->setCurrentItem( tree->topLevelItem( 0 ) );
        buttonIn( dialog, "packMergeKeepBoth" )->click();
        buttonIn( dialog, "packMergeWholeMine" )->click();
        buttonIn( dialog, "packMergeApply" )->click();
        stillOpen = dialog->isVisible();
        buttonIn( dialog, "packMergeCancel" )->click();
    } );
    CHECK_FALSE( pluginsui::openPackMerge( nullptr, "Example pack", job,
                                           []( const QVariantList&, const pluginsui::AfterPackMerge& ) {
                                               FAIL( "applied with a copy that cannot be made" );
                                           } ) );
    CHECK( stillOpen );
    CHECK( refusal.contains( "already have a preview named \"Second (mine)\"" ) );
    REQUIRE( PreviewManager::instance().clearAll() );
    registry.attach( nullptr );
}

TEST_CASE( "A merge asked for again replaces the earlier choices, Keep Both included",
           "[packeditors][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    installPack( manager, root.filePath( "package" ) );
    auto& registry = PreviewPackRegistry::instance();
    registry.attach( &manager );
    REQUIRE( PreviewManager::instance().clearAll() );
    const auto editSecond = [ &manager ]( const QString& regex ) {
        auto item = QJsonDocument::fromJson( manager.manage( "pack_read", PackId, { { "path", SecondPath } } )
                                                 .payload[ "content" ]
                                                 .toString()
                                                 .toUtf8() )
                        .object();
        item[ "regex" ] = regex;
        REQUIRE( manager.manage( "pack_edit", PackId,
                                 { { "path", SecondPath },
                                   { "content", QString::fromUtf8( QJsonDocument( item ).toJson() ) } } )
                     .ok() );
    };
    editSecond( "^MINE$" );
    // Version 1.1.0 changes the regex you edited.
    const auto v2 = writePackage( root.filePath( "v2" ) );
    {
        auto manifest = packManifest();
        manifest[ "version" ] = "1.1.0";
        QFile plugin( v2 + "/plugin.json" );
        REQUIRE( plugin.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        plugin.write( QJsonDocument( manifest ).toJson() );
        QFile previews( v2 + "/previews.json" );
        REQUIRE( previews.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        previews.write( QByteArray( Previews ).replace( "^B$", "^THEIRS$" ) );
    }
    CommanderRequest request;
    request.action = CommanderAction::PluginManage;
    request.pluginOperation = "install";
    request.pluginArguments = { { "path", v2 } };
    request.origin = CommanderOriginGui;

    // First merge: Keep Both, but the pack is edited again before the update runs.
    bool firstApplied = false;
    bool secondShown = false;
    whenOpen( "packMergeDialog", [ &firstApplied, &editSecond ]( QWidget* dialog ) {
        auto* tree = dialog->findChild<QTreeWidget*>( "packMergeTree" );
        REQUIRE( tree );
        tree->setCurrentItem( tree->topLevelItem( 0 ) );
        buttonIn( dialog, "packMergeKeepBoth" )->click();
        editSecond( "^NEWER$" );
        firstApplied = true;
        buttonIn( dialog, "packMergeApply" )->click();
    } );
    // The merge is asked for again; this time the new version is taken.
    whenModal(
        [ &firstApplied ]( QWidget* widget ) {
            return firstApplied && widget->objectName() == "packMergeDialog" && widget->isVisible();
        },
        [ &secondShown ]( QWidget* dialog ) {
            secondShown = true;
            buttonIn( dialog, "packMergeAllTheirs" )->click();
            buttonIn( dialog, "packMergeApply" )->click();
        } );
    QWidget host;
    QVariantMap last;
    bool done = false;
    pluginsui::runPackAwareInstall( &host, &manager, executorFor( manager ), request, "Example pack",
                                    [ &last, &done ]( const QVariantMap& job ) {
                                        last = job;
                                        done = true;
                                    } );
    QElapsedTimer timer;
    timer.start();
    while ( !done && timer.elapsed() < 30000 ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    }
    REQUIRE( done );
    INFO( last.value( "error" ).toString().toStdString() );
    CHECK( secondShown );
    REQUIRE( last.value( "state" ) == "completed" );
    CHECK( manager.manage( "pack_read", PackId, { { "path", SecondPath } } )
               .payload[ "content" ]
               .toString()
               .contains( "^THEIRS$" ) );
    // The first merge's Keep Both was not chosen again: no copy of yours.
    CHECK_FALSE( PreviewManager::instance().findByName( "Second (mine)" ) );
    REQUIRE( PreviewManager::instance().clearAll() );
    registry.attach( nullptr );
}

TEST_CASE( "The external merge tool's result is read back and checked", "[packeditors][E16]" )
{
    const auto previous = pluginsui::mergeToolCommand();
#ifdef Q_OS_WIN
    pluginsui::setMergeToolCommand( "cmd /c copy /y \"$REMOTE\" \"$MERGED\"" );
#else
    pluginsui::setMergeToolCommand( "cp \"$REMOTE\" \"$MERGED\"" );
#endif
    QString error;
    const auto merged = pluginsui::runMergeTool( nullptr, "1\n", "2\n", "3\n", ".lua", &error );
    INFO( error.toStdString() );
    REQUIRE( merged );
    CHECK( *merged == "3\n" );
    // A result that still has conflict markers is refused.
    const auto refused = pluginsui::runMergeTool(
        nullptr, "1\n", "2\n", "<<<<<<< a\n2\n=======\n3\n>>>>>>> b\n", ".lua", &error );
    CHECK_FALSE( refused );
    CHECK( error.contains( "conflict markers" ) );
    // A tool that returns without saving leaves the merge with its markers: refused too, never
    // taken as "yours".
#ifdef Q_OS_WIN
    pluginsui::setMergeToolCommand( "cmd /c rem \"$MERGED\"" );
#else
    pluginsui::setMergeToolCommand( "true \"$MERGED\"" );
#endif
    error.clear();
    CHECK_FALSE( pluginsui::runMergeTool( nullptr, "1\n", "2\n", "3\n", ".lua", &error ) );
    CHECK( error.contains( "conflict markers" ) );
    pluginsui::setMergeToolCommand( previous );
    CHECK( pluginsui::mergeToolPresets().size() == 5 );
}

TEST_CASE( "An automatic pack update that needs a merge says so", "[packeditors][E16]" )
{
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    PluginManager manager( root.filePath( "data" ), root.filePath( "none" ) );
    PluginUpdateNotices notices( &manager );
    notices.setInteractive( true );
    Q_EMIT manager.autoUpdateFailed( { { "id", PackId },
                                       { "name", "Example pack" },
                                       { "to", "1.2.0" },
                                       { "needsMerge", true },
                                       { "error", "The new version changes an item" } } );
    QMessageBox* notice = nullptr;
    for ( auto* widget : QApplication::topLevelWidgets() ) {
        if ( widget->objectName() == "pluginAutoUpdateMergeNotice" ) {
            notice = qobject_cast<QMessageBox*>( widget );
        }
    }
    REQUIRE( notice );
    CHECK( notice->text().contains( "Example pack 1.2.0 changes items you edited" ) );
    CHECK( notice->findChild<QPushButton*>( "pluginUpdateNoticeOpen" ) );
    notice->close();
}
