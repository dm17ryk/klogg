// SPDX-License-Identifier: GPL-3.0-or-later
// Issue #180: the TShark / Lua preview decoder settings offer the known route values (previews,
// assembled protocols, Lua dissectors, pack Python decoders, TShark protocols) and warn on Apply
// about a dissector nothing declares. The Lua here is synthetic: no real protocol is described.
#include <catch2/catch.hpp>

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QTest>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include "previewconfigparser.h"
#include "previewdecodersettings.h"
#include "previewdecodersettingsdialog.h"
#include "previewmanager.h"
#include "previewrepository.h"
#include "previewroutechoices.h"

namespace {
constexpr auto SyntheticLua = R"lua(-- Proto("line_comment", "not a declaration")
--[[ Proto("block_comment", "not a declaration") ]]
--[==[ Proto("level_comment", "not a declaration")
]] still inside ]==]
local alpha = Proto("Alpha", "Alpha protocol")
local beta = Proto ( 'beta_x' , "Beta protocol" )
local gamma = Proto.new("gamma.v2", 'Gamma protocol')
local quoted = "Proto('in_string', \"not a declaration\")"
local long = [[ Proto("in_long_string", "not a declaration") ]]
local again = Proto("alpha", "the same dissector again")
local spaced = Proto("bad name", "not a valid dissector name")
local other = MyProto("my_proto_call", "another function")
local field = tools.Proto("field_call", "a table field")
)lua";

// Restores the decoder settings, the pack contributions and the previews a test changes.
struct RestoreState {
    QVariantMap configuration = PreviewDecoderSettings::instance().configuration();
    PreviewDecoderPacks packs = PreviewDecoderSettings::instance().packs();
    QByteArray previews = PreviewRepository::serialize( PreviewManager::instance().all(),
                                                        PreviewManager::instance().blocks() );
    ~RestoreState()
    {
        PreviewDecoderSettings::instance().setConfiguration( configuration );
        PreviewDecoderSettings::instance().setPacks( packs );
        auto& manager = PreviewManager::instance();
        manager.setPackPreviews( {} );
        manager.clearAll();
        QTemporaryFile file;
        if ( file.open() && file.write( previews ) == previews.size() && file.flush() )
            manager.importFromFile( file.fileName() );
    }
};

QString writeFile( const QTemporaryDir& dir, const QString& name, const QByteArray& body )
{
    const auto path = dir.filePath( name );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( body ) == body.size() );
    return path;
}

QByteArray previewsJson( const QString& name )
{
    return QStringLiteral(
               R"({"version":1,"previews":[{"name":"%1","regex":"^x:(?<b>.*)$",)"
               R"("bufferCapture":"b","type":"hexString",)"
               R"("fields":[{"name":"Byte","type":"uint8","offset":0}]}]})" )
        .arg( name )
        .toUtf8();
}

QStringList items( const QComboBox* combo )
{
    QStringList result;
    for ( int i = 0; i < combo->count(); ++i )
        result << combo->itemText( i );
    return result;
}

// Opens the cell's editor and returns its drop-down.
QComboBox* editor( QTableWidget* table, int row, int column )
{
    table->setCurrentCell( row, column );
    auto* item = table->item( row, column );
    REQUIRE( item );
    const auto index = table->model()->index( row, column );
    if ( !table->indexWidget( index ) )
        table->editItem( item );
    auto* combo = qobject_cast<QComboBox*>( table->indexWidget( index ) );
    REQUIRE( combo );
    CHECK( combo->objectName() == "decoderRouteEditor" );
    return combo;
}

// Writes `text` into the cell through its editor, as picking or typing a value does.
void commit( QTableWidget* table, int row, int column, const QString& text )
{
    auto* combo = editor( table, row, column );
    combo->setCurrentText( text );
    const auto index = table->model()->index( row, column );
    table->itemDelegate()->setModelData( combo, table->model(), index );
    table->closePersistentEditor( table->item( row, column ) );
}
} // namespace

TEST_CASE( "Lua Proto declarations give the dissector names", "[previewroutes]" )
{
    SECTION( "declarations in either quote style, Proto.new, without comments and strings" )
    {
        CHECK( previewroutes::luaProtoNames( QString::fromUtf8( SyntheticLua ) )
               == QStringList{ "alpha", "beta_x", "gamma.v2" } );
    }
    SECTION( "several declarations on one line and an unterminated string" )
    {
        CHECK( previewroutes::luaProtoNames(
                   "local a, b = Proto(\"one\", \"1\"), Proto('two', '2') local c = \"x" )
               == QStringList{ "one", "two" } );
    }
    SECTION( "escapes in the name, as Lua reads them" )
    {
        CHECK( previewroutes::luaProtoNames( "local a = Proto(\"foo\\x2ebar\", \"x\")\n"
                                             "local b = Proto('a\\098c', 'x')\n"
                                             "local c = Proto(\"q\\u{72}s\", 'x')\n"
                                             "local d = Proto(\"bad\\q\", 'x')\n" )
               == QStringList{ "foo.bar", "abc", "qrs" } );
    }
    SECTION( "carriage returns end lines" )
    {
        CHECK( previewroutes::luaProtoNames(
                   "-- a comment\rlocal p = Proto('cr_only', 'x')\r"
                   "local s = 'unterminated\rlocal q = Proto(\"after_cr\", 'y')\r" )
               == QStringList{ "cr_only", "after_cr" } );
    }
    SECTION( "comments between the declaration's tokens" )
    {
        CHECK( previewroutes::luaProtoNames(
                   "local p = Proto( -- the name\n  \"foo\", \"Foo\")\n"
                   "local q = Proto --[[ a ]] . --[==[ b ]==] new ( 'bar', 'Bar' )" )
               == QStringList{ "foo", "bar" } );
    }
    SECTION( "strings continued on the next line" )
    {
        // A backslash before a Windows line end, and `\z`, continue a short string.
        CHECK( previewroutes::luaProtoNames(
                   "local s = \"a\\\r\nProto('in_crlf', 'x')\" local p = Proto('real', 'x')" )
               == QStringList{ "real" } );
        CHECK( previewroutes::luaProtoNames(
                   "local s = 'a\\z\r\n   Proto(\"in_z\", \"x\")' local p = Proto('real2', 'x')" )
               == QStringList{ "real2" } );
    }
    SECTION( "nothing declared" )
    {
        CHECK( previewroutes::luaProtoNames( "print(\"hello\")" ).isEmpty() );
        CHECK( previewroutes::luaProtoNames( "Proto(name, \"desc\")" ).isEmpty() );
    }
    SECTION( "a file, and a file that cannot be read" )
    {
        QTemporaryDir dir;
        REQUIRE( dir.isValid() );
        const auto path = writeFile( dir, "synthetic.lua", SyntheticLua );
        CHECK( previewroutes::luaProtoNamesInFile( path )
               == QStringList{ "alpha", "beta_x", "gamma.v2" } );
        CHECK( previewroutes::luaProtoNamesInFile( dir.filePath( "missing.lua" ) ).isEmpty() );
    }
}

TEST_CASE( "Collected Lua names follow the file's changes", "[previewroutes]" )
{
    // The names are read once per version of a file (path, size and time), not on each collect.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto path = writeFile( dir, "changing.lua", "local a = Proto('first', 'x')\n" );
    // The selected files come first, before any enabled pack's.
    const auto names = [ & ] { return previewroutes::collect( { path } ).lua.front().names; };
    CHECK( names() == QStringList{ "first" } );
    CHECK( names() == QStringList{ "first" } );
    writeFile( dir, "changing.lua",
               "local a = Proto('second', 'x')\nlocal b = Proto('third_one', 'y')\n" );
    CHECK( names() == QStringList{ "second", "third_one" } );

    // An edit that keeps the size and, within the file system's time granularity, the time:
    // forgetting the read names (the dialog does on opening and on a settings change) reads it.
    const auto modified = QFileInfo( path ).lastModified();
    writeFile( dir, "changing.lua",
               "local a = Proto('fourth', 'x')\nlocal b = Proto('fifth_one', 'y')\n" );
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::ReadWrite ) );
        REQUIRE( file.setFileTime( modified, QFileDevice::FileModificationTime ) );
    }
    REQUIRE( QFileInfo( path ).lastModified() == modified );
    previewroutes::forgetLuaNames();
    CHECK( names() == QStringList{ "fourth", "fifth_one" } );
}

TEST_CASE( "TShark's protocol list gives its filter names", "[previewroutes]" )
{
    const QByteArray output = "Data\tData\tdata\tT\tT\tF\r\n"
                              "JavaScript Object Notation\tJSON\tjson\tT\tT\tT\n"
                              "29West Protocol\t29West\t29west\tT\tT\tT\n"
                              "a line without tabs\n"
                              "Lua Dissection\tLua Dissection\t_ws.lua\tT\tT\tT\n";
    // Names the settings could not take (a leading digit or underscore) are left out.
    CHECK( previewroutes::tsharkProtocolNames( output ) == QStringList{ "data", "json" } );
    CHECK( previewroutes::tsharkProtocolNames( {} ).isEmpty() );
}

TEST_CASE( "Apply warns about dissectors nothing declares", "[previewroutes]" )
{
    previewroutes::Choices choices;
    choices.lua = { { "C:/lua/my_proto.lua", { "my_proto" } },
                    { "C:/lua/other_file.lua", { "other", "other_extra" } },
                    // Declares its name through a variable: nothing to read.
                    { "C:/lua/dynamic.lua", {} } };
    choices.python = { "python:org.example.alpha/summary" };
    choices.pythonModules = { "python:org.example.alpha/summary" };
    choices.tshark = { "data", "json" };
    choices.tsharkLoaded = true;
    QVariantMap configuration{
        { "defaultDissector", "data" },
        { "previewRoutes",
          QVariantMap{ { "Declared", QVariantMap{ { "dissector", "my_proto" } } },
                       { "File name", QVariantMap{ { "dissector", "my_proto.lua" } } },
                       { "Other file", QVariantMap{ { "dissector", "OTHER_FILE.LUA" } } },
                       { "Dynamic file", QVariantMap{ { "dissector", "dynamic.lua" } } },
                       { "Upper case", QVariantMap{ { "dissector", "MY_PROTO" } } },
                       { "TShark", QVariantMap{ { "dissector", "json" } } },
                       { "Unknown", QVariantMap{ { "dissector", "nowhere" } } },
                       { "Python", QVariantMap{ { "python", "org.example.alpha/summary" } } },
                       { "Gone", QVariantMap{ { "python", "org.example.alpha/missing" } } } } },
        { "protocolRoutes",
          QVariantMap{ { "EHCI", QVariantMap{ { "dissector", "stale.lua" } } } } } };

    const auto warnings = previewroutes::check( configuration, choices );
    QMap<QString, previewroutes::Warning> byName;
    for ( const auto& warning : warnings )
        byName.insert( warning.name, warning );
    CHECK( byName.keys()
           == QStringList{ "MY_PROTO", "OTHER_FILE.LUA", "dynamic.lua", "my_proto.lua", "nowhere",
                           "org.example.alpha/missing", "stale.lua" } );

    const auto file = byName.value( "my_proto.lua" );
    CHECK( file.suggestion == "my_proto" );
    CHECK( file.field.contains( "File name" ) );
    CHECK( file.text.contains( "looks like a file name" ) );
    CHECK( file.text.contains( "registers `my_proto`" ) );
    // A file that declares several dissectors suggests its first and names them all.
    const auto other = byName.value( "OTHER_FILE.LUA" );
    CHECK( other.suggestion == "other" );
    CHECK( other.text.contains( "`other`, `other_extra`" ) );
    // The selected file's name, but no declaration could be read from it.
    const auto dynamic = byName.value( "dynamic.lua" );
    CHECK( dynamic.suggestion.isEmpty() );
    CHECK( dynamic.text.contains( "looks like a file name" ) );
    CHECK( dynamic.text.contains( "Proto(\"<name>\", ...)" ) );
    CHECK( byName.value( "MY_PROTO" ).suggestion == "my_proto" );
    CHECK( byName.value( "MY_PROTO" ).text.contains( "lower case" ) );
    CHECK( byName.value( "nowhere" ).suggestion.isEmpty() );
    CHECK( byName.value( "nowhere" ).text.contains( "TShark does not list it" ) );
    CHECK( byName.value( "nowhere" ).text.contains( "may still be a dissector name" ) );
    CHECK( byName.value( "stale.lua" ).suggestion.isEmpty() );
    CHECK( byName.value( "stale.lua" ).field.contains( "EHCI" ) );
    CHECK( byName.value( "stale.lua" ).text.contains( "looks like a file name" ) );
    CHECK( byName.value( "org.example.alpha/missing" ).text.contains( "no enabled pack" ) );

    SECTION( "without TShark's list a dissector only Lua could declare is reported as unchecked" )
    {
        choices.tshark.clear();
        choices.tsharkLoaded = false;
        const auto unchecked = previewroutes::check( configuration, choices );
        bool data = false;
        for ( const auto& warning : unchecked ) {
            if ( warning.name == "data" ) {
                data = true;
                CHECK( warning.text.contains( "TShark's protocol list is not loaded" ) );
                CHECK( warning.suggestion.isEmpty() );
            }
        }
        CHECK( data );
        // Why the list is missing is part of the warning.
        choices.tsharkFailure = "timed out";
        bool reason = false;
        for ( const auto& warning : previewroutes::check( configuration, choices ) )
            reason = reason || ( warning.name == "data" && warning.text.contains( "(timed out)" ) );
        CHECK( reason );
    }
    SECTION( "while TShark's list loads, only what it cannot settle is reported" )
    {
        choices.tshark.clear();
        choices.tsharkLoaded = false;
        choices.tsharkPending = true;
        QStringList names;
        for ( const auto& warning : previewroutes::check( configuration, choices ) )
            names << warning.name;
        CHECK_FALSE( names.contains( "data" ) );
        CHECK_FALSE( names.contains( "json" ) );
        CHECK_FALSE( names.contains( "nowhere" ) );
        // A Lua file's base name may also be a TShark protocol: it waits for the list too.
        CHECK_FALSE( names.contains( "MY_PROTO" ) );
        CHECK( names.contains( "my_proto.lua" ) );
        CHECK( names.contains( "stale.lua" ) );
        CHECK( names.contains( "org.example.alpha/missing" ) );
    }
    SECTION( "with TShark disabled, only what does not need its list is reported" )
    {
        choices.tshark.clear();
        choices.tsharkLoaded = false;
        choices.tsharkDisabled = true;
        QStringList names;
        for ( const auto& warning : previewroutes::check( configuration, choices ) )
            names << warning.name;
        CHECK_FALSE( names.contains( "data" ) );
        CHECK_FALSE( names.contains( "nowhere" ) );
        CHECK( names.contains( "MY_PROTO" ) );
        CHECK( names.contains( "my_proto.lua" ) );
        CHECK( names.contains( "org.example.alpha/missing" ) );
    }
    SECTION( "a module its pack lists is not reported, even one not offered as a decoder" )
    {
        // Routing runs every listed module; Python's check only decides what is offered (its
        // reasons show in the route's tooltip), and a decode() bound at run time still works.
        choices.pythonModules = { "python:org.example.alpha/summary",
                                  "python:org.example.alpha/helper" };
        configuration[ "protocolRoutes" ] = QVariantMap{
            { "EHCI", QVariantMap{ { "python", "org.example.alpha/helper" } } } };
        QStringList names;
        for ( const auto& warning : previewroutes::check( configuration, choices ) )
            names << warning.name;
        CHECK_FALSE( names.contains( "org.example.alpha/helper" ) );
        CHECK( names.contains( "org.example.alpha/missing" ) );
    }
    SECTION( "nothing to report" )
    {
        CHECK( previewroutes::check( { { "defaultDissector", "my_proto" } }, choices ).isEmpty() );
        CHECK( previewroutes::check( {}, choices ).isEmpty() );
    }
}

TEST_CASE( "Decoder settings offer the known route values", "[previewroutes]" )
{
    // The folder outlives the restored settings: deleting a watched Lua file's folder would
    // change the settings revision later, in another test.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    RestoreState restore;
    const auto userLua = writeFile( dir, "user_file.lua",
                                    "local p = Proto(\"user_proto\", \"User protocol\")\n" );
    const auto packLua = writeFile( dir, "pack_file.lua",
                                    "local p = Proto('pack_proto', 'Pack protocol')\n" );

    auto& manager = PreviewManager::instance();
    manager.clearAll();
    {
        QTemporaryFile file;
        REQUIRE( file.open() );
        const auto json = previewsJson( "User carrier" );
        REQUIRE( file.write( json ) == json.size() );
        REQUIRE( file.flush() );
        REQUIRE( manager.importFromFile( file.fileName() ).ok );
    }
    const auto parsed = PreviewConfigParser().parseJson( previewsJson( "Pack carrier" ) );
    REQUIRE( parsed.errors.isEmpty() );
    manager.setPackPreviews(
        { PreviewPackSource{ "org.example.alpha", "Alpha pack", parsed.previews, parsed.blocks } } );
    PreviewDecoderPacks packs;
    packs.lua = { QVariantMap{ { "id", "org.example.alpha" },
                               { "files", QStringList{ packLua } } } };
    // Two listed modules; Python's check (E16b) found decode() in `summary` only, `common` is a
    // helper and is not offered.
    packs.python = { QVariantMap{
        { "id", "org.example.alpha" },
        { "root", dir.path() },
        { "decoders",
          QVariantList{ QVariantMap{ { "id", "summary" }, { "module", "summary.py" } },
                        QVariantMap{ { "id", "common" }, { "module", "common.py" } } } } } };
    packs.pythonEntries = { { "org.example.alpha", QStringList{ "summary" } } };
    packs.key = "previewroutes-test";
    PreviewDecoderSettings::instance().setPacks( packs );
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "lua", QVariantList{ userLua } },
          { "inputEncoding", "hex" },
          { "previewRoutes",
            QVariantMap{ { "User carrier", QVariantMap{ { "dissector", "user_proto" } } } } },
          { "protocolRoutes",
            QVariantMap{ { "EHCI", QVariantMap{ { "dissector", "pack_proto" },
                                                { "protocol", "pack_proto" } } } } } } ) );

    PreviewDecoderSettingsDialog dialog;
    auto* previews = dialog.findChild<QTableWidget*>( "decoderPreviewRoutes" );
    auto* protocols = dialog.findChild<QTableWidget*>( "decoderProtocolRoutes" );
    REQUIRE( previews );
    REQUIRE( protocols );
    REQUIRE( previews->rowCount() == 1 );
    REQUIRE( protocols->rowCount() == 1 );

    SECTION( "previews, assembled protocols, dissectors and encodings" )
    {
        CHECK( items( editor( previews, 0, 0 ) ) == QStringList{ "User carrier", "Pack carrier" } );
        const auto dissectors = items( editor( previews, 0, 1 ) );
        CHECK( dissectors.mid( 0, 3 )
               == QStringList{ "user_proto", "pack_proto", "python:org.example.alpha/summary" } );
        CHECK_FALSE( dissectors.contains( "python:org.example.alpha/common" ) );
        CHECK( editor( previews, 0, 1 )->isEditable() );
        // The PDML protocol offers the row's dissector first, and no Python decoder.
        const auto pdml = items( editor( previews, 0, 2 ) );
        CHECK( pdml.value( 0 ) == "user_proto" );
        CHECK( pdml.contains( "pack_proto" ) );
        CHECK_FALSE( pdml.contains( "python:org.example.alpha/summary" ) );
        CHECK( items( editor( protocols, 0, 0 ) ).contains( "EHCI" ) );
        auto* encoding = editor( protocols, 0, 3 );
        CHECK( items( encoding ) == QStringList{ "utf8", "hex", "base64" } );
        CHECK_FALSE( encoding->isEditable() );
        auto* fallback = dialog.findChild<QComboBox*>( "decoderDefaultDissector" );
        REQUIRE( fallback );
        CHECK( fallback->isEditable() );
        CHECK( fallback->currentText().isEmpty() );
        // The default is TShark's: no Python decoder (and no TShark configured here).
        CHECK( items( fallback ) == QStringList{ "user_proto", "pack_proto" } );
        auto* status = dialog.findChild<QLabel*>( "decoderChoicesStatus" );
        REQUIRE( status );
        CHECK( status->text().contains( "2 from Lua files" ) );
        CHECK( status->text().contains( "1 Python decoder" ) );
    }
    SECTION( "typed text is kept, and the PDML protocol follows the dissector" )
    {
        commit( previews, 0, 0, "Typed preview" );
        CHECK( previews->item( 0, 0 )->text() == "Typed preview" );
        // The empty PDML protocol takes the chosen dissector's name.
        CHECK( previews->item( 0, 2 )->text().isEmpty() );
        commit( previews, 0, 1, "pack_proto" );
        CHECK( previews->item( 0, 2 )->text() == "pack_proto" );
        commit( previews, 0, 1, "user_proto" );
        CHECK( previews->item( 0, 2 )->text() == "user_proto" );
        // One the user chose stays...
        commit( previews, 0, 2, "custom" );
        commit( previews, 0, 1, "pack_proto" );
        CHECK( previews->item( 0, 2 )->text() == "custom" );
        // ...but a Python decoder takes none: the settings would refuse the route.
        commit( previews, 0, 1, "python:org.example.alpha/summary" );
        CHECK( previews->item( 0, 2 )->text().isEmpty() );
        // A typed dissector nothing declares may not name a PDML protocol: every decoded
        // protocol keeps showing.
        commit( previews, 0, 1, "typed_unknown" );
        CHECK( previews->item( 0, 2 )->text().isEmpty() );
        commit( previews, 0, 1, "pack_proto" );
        CHECK( previews->item( 0, 2 )->text() == "pack_proto" );
        commit( previews, 0, 1, "typed_unknown" );
        CHECK( previews->item( 0, 2 )->text().isEmpty() );
    }
    SECTION( "refreshing the lists keeps the default dissector being typed" )
    {
        auto* fallback = dialog.findChild<QComboBox*>( "decoderDefaultDissector" );
        auto* remove = dialog.findChild<QPushButton*>( "decoderRemoveLua" );
        auto* scripts = dialog.findChild<QListWidget*>( "decoderLuaScripts" );
        REQUIRE( fallback );
        REQUIRE( remove );
        REQUIRE( scripts );
        fallback->setEditText( "user_pr" );
        fallback->lineEdit()->setSelection( 1, 3 );
        // Removing the user's Lua file takes its dissector out of the list: the list is refilled,
        // and the text being typed keeps its selection (and so its cursor).
        scripts->setCurrentRow( 0 );
        remove->click();
        REQUIRE( scripts->count() == 0 );
        CHECK( items( fallback ) == QStringList{ "pack_proto" } );
        CHECK( fallback->currentText() == "user_pr" );
        CHECK( fallback->lineEdit()->selectedText() == "ser" );
        CHECK( fallback->lineEdit()->cursorPosition() == 4 );
    }
    SECTION( "an empty encoding stays empty when its drop-down is only visited" )
    {
        protocols->item( 0, 3 )->setText( {} );
        auto* encoding = editor( protocols, 0, 3 );
        CHECK( encoding->currentIndex() == -1 );
        const auto index = protocols->model()->index( 0, 3 );
        protocols->itemDelegate()->setModelData( encoding, protocols->model(), index );
        CHECK( protocols->item( 0, 3 )->text().isEmpty() );
        encoding->setCurrentIndex( encoding->findText( "base64" ) );
        protocols->itemDelegate()->setModelData( encoding, protocols->model(), index );
        CHECK( protocols->item( 0, 3 )->text() == "base64" );
    }
    SECTION( "a new route row is filled through the same drop-downs" )
    {
        auto* add = dialog.findChild<QPushButton*>( "decoderAddPreviewRoute" );
        REQUIRE( add );
        add->click();
        REQUIRE( previews->rowCount() == 2 );
        commit( previews, 1, 0, "Pack carrier" );
        commit( previews, 1, 1, "pack_proto" );
        CHECK( previews->item( 1, 2 )->text() == "pack_proto" );
    }
}

TEST_CASE( "Apply saves, warns and offers the declared name", "[previewroutes]" )
{
    // The folder outlives the restored settings (see above).
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    RestoreState restore;
    const auto lua = writeFile( dir, "my_proto.lua",
                                "local my_proto = Proto(\"my_proto\", \"Synthetic protocol\")\n" );
    PreviewManager::instance().clearAll();
    PreviewDecoderSettings::instance().setPacks( {} );
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "lua", QVariantList{ lua } },
          { "previewRoutes",
            QVariantMap{ { "Carrier", QVariantMap{ { "dissector", "my_proto.lua" } } } } } } ) );

    PreviewDecoderSettingsDialog dialog;
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    REQUIRE( buttons );
    auto* warnings = dialog.findChild<QLabel*>( "decoderRouteWarnings" );
    auto* useDeclared = dialog.findChild<QPushButton*>( "decoderUseDeclaredNames" );
    auto* status = dialog.findChild<QLabel*>( "decoderValidationError" );
    REQUIRE( warnings );
    REQUIRE( useDeclared );
    REQUIRE( status );
    // The dialog checks the saved routes when it opens.
    CHECK( warnings->text().contains( "`my_proto.lua` looks like a file name" ) );
    CHECK_FALSE( useDeclared->isHidden() );
    auto* routesTable = dialog.findChild<QTableWidget*>( "decoderPreviewRoutes" );
    REQUIRE( routesTable );
    const auto savedDissector = [] {
        return PreviewDecoderSettings::instance()
            .configuration()
            .value( "previewRoutes" )
            .toMap()
            .value( "Carrier" )
            .toMap()
            .value( "dissector" )
            .toString();
    };

    SECTION( "Apply saves as typed, warns, and the declared name replaces the file name" )
    {
        // Spaces around a typed dissector are not part of it.
        routesTable->item( 0, 1 )->setText( "  my_proto.lua " );
        buttons->button( QDialogButtonBox::Apply )->click();
        // The warning does not block the save.
        CHECK( status->text() == "Settings applied." );
        CHECK( savedDissector() == "my_proto.lua" );
        CHECK( warnings->text().contains( "`my_proto.lua` looks like a file name" ) );
        CHECK( warnings->text().contains( "registers `my_proto`" ) );
        CHECK_FALSE( useDeclared->isHidden() );

        useDeclared->click();
        CHECK( routesTable->item( 0, 1 )->text() == "my_proto" );
        CHECK( savedDissector() == "my_proto" );
        CHECK( status->text() == "Settings applied." );
        CHECK( warnings->text().isEmpty() );
        CHECK( useDeclared->isHidden() );
    }
    SECTION( "OK saves and stays open once to show new warnings" )
    {
        routesTable->item( 0, 1 )->setText( "my_proto" );
        buttons->button( QDialogButtonBox::Apply )->click();
        CHECK( warnings->text().isEmpty() );
        dialog.show();
        routesTable->item( 0, 1 )->setText( "my_proto.lua" );
        buttons->button( QDialogButtonBox::Ok )->click();
        CHECK( savedDissector() == "my_proto.lua" );
        CHECK( dialog.isVisible() );
        CHECK( warnings->text().contains( "looks like a file name" ) );
        // The same warnings again: OK closes.
        buttons->button( QDialogButtonBox::Ok )->click();
        CHECK_FALSE( dialog.isVisible() );
    }
    SECTION( "the saved routes are checked against the saved Lua files" )
    {
        // A Lua file removed from the list but not applied yet still declares the saved
        // routes' dissector: a later check (here, showing the dialog) keeps its suggestion.
        auto* scripts = dialog.findChild<QListWidget*>( "decoderLuaScripts" );
        REQUIRE( scripts );
        scripts->setCurrentRow( 0 );
        dialog.findChild<QPushButton*>( "decoderRemoveLua" )->click();
        REQUIRE( scripts->count() == 0 );
        dialog.show();
        CHECK( warnings->text().contains( "registers `my_proto`" ) );
        CHECK_FALSE( useDeclared->isHidden() );
    }
    SECTION( "warnings the dialog already showed do not hold OK" )
    {
        dialog.show();
        buttons->button( QDialogButtonBox::Ok )->click();
        CHECK_FALSE( dialog.isVisible() );
        // Opened again, it shows them again.
        dialog.reload();
        CHECK( warnings->text().contains( "looks like a file name" ) );
    }
}

// Captures the dialog for the docs and reviews: run with `-platform windows` and
// CILOGG_PREVIEW_ROUTES_SCREENSHOTS set to a folder.
TEST_CASE( "Decoder settings route drop-downs screenshots", "[.][previewroutesscreenshots]" )
{
    const auto folder = qEnvironmentVariable( "CILOGG_PREVIEW_ROUTES_SCREENSHOTS" );
    REQUIRE_FALSE( folder.isEmpty() );
    REQUIRE( QDir().mkpath( folder ) );
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    RestoreState restore;
    const auto lua = writeFile( dir, "my_proto.lua",
                                "local my_proto = Proto(\"my_proto\", \"Synthetic protocol\")\n"
                                "local other = Proto.new('My_Other', \"Another protocol\")\n" );
    PreviewManager::instance().clearAll();
    PreviewDecoderSettings::instance().setPacks( {} );
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "lua", QVariantList{ lua } },
          { "previewRoutes",
            QVariantMap{ { "My carrier", QVariantMap{ { "dissector", "my_proto.lua" } } } } },
          { "protocolRoutes",
            QVariantMap{ { "EHCI", QVariantMap{ { "dissector", "my_proto" },
                                                { "protocol", "my_proto" } } } } } } ) );
    PreviewDecoderSettingsDialog dialog;
    dialog.resize( 900, 760 );
    dialog.show();
    REQUIRE( QTest::qWaitForWindowExposed( &dialog ) );
    const auto shoot = [ & ]( const QString& name ) {
        QTest::qWait( 300 );
        const auto frame = dialog.frameGeometry();
        auto* screen = dialog.screen();
        REQUIRE( screen );
        // Only the dialog's frame: the open drop-down lies within it.
        REQUIRE( screen->grabWindow( 0, frame.x(), frame.y(), frame.width(), frame.height() )
                     .save( QDir( folder ).filePath( name ) ) );
    };
    dialog.findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Apply )->click();
    shoot( "route-warnings.png" );
    auto* previews = dialog.findChild<QTableWidget*>( "decoderPreviewRoutes" );
    auto* combo = editor( previews, 0, 1 );
    combo->showPopup();
    shoot( "route-dissector-dropdown.png" );
    combo->hidePopup();
}
