// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// E17d: rich tables and element context menus (docs/plugin-sdk-advanced.md section 10).
#include "mainwindow.h"
#include "pluginicons.h"
#include "pluginmanager.h"
#include "pluginmenus.h"
#include "plugintablebehaviour.h"
#include "plugintablemodel.h"
#include "pluginview.h"
#include "pluginviewhost.h"
#include "session.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QJsonDocument>
#include <QMenu>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch.hpp>
#include <map>
#include <memory>

using cilogg::plugins::PluginManager;

namespace {
const QString PluginId = QStringLiteral( "org.test.e17d" );

bool waitFor( const std::function<bool()>& predicate, int timeout = 5000 )
{
    return QTest::qWaitFor( predicate, timeout );
}

QJsonObject cell( const QString& text, const QJsonObject& extra = {} )
{
    QJsonObject result{ { "text", text } };
    for ( auto it = extra.begin(); it != extra.end(); ++it ) {
        result.insert( it.key(), it.value() );
    }
    return result;
}

QJsonObject boardsTable( const QJsonObject& extra = {} )
{
    QJsonObject table{
        { "type", "table" },
        { "id", "t" },
        { "selection", "multi" },
        { "sortable", true },
        { "columns",
          QJsonArray{ QJsonObject{ { "id", "name" }, { "title", "Name" }, { "sort_as", "natural" } },
                      QJsonObject{ { "id", "done" }, { "title", "Done" }, { "align", "right" },
                                   { "icon", "builtin:info" } },
                      QJsonObject{ { "id", "use" }, { "title", "Use" }, { "check", true },
                                   { "sortable", false }, { "min_width", 60 } },
                      QJsonObject{ { "id", "note" }, { "title", "Note" }, { "hidden", true } },
                      QJsonObject{ { "id", "edit" }, { "title", "Edit" }, { "editable", true } } } },
        { "rows",
          QJsonArray{
              QJsonObject{ { "id", "r1" },
                           { "icon", "builtin:device" },
                           { "cells", QJsonArray{ cell( "board 10", { { "bold", true },
                                                                      { "tooltip", "Tenth" } } ),
                                                  cell( "50%", { { "progress", 0.5 }, { "sort", 0.5 },
                                                                 { "tone", "warn" } } ),
                                                  "", "=SUM(1)", "e1" } },
                           { "editors", QJsonObject{ { "edit", QJsonObject{ { "kind", "text" } } } } } },
              QJsonObject{ { "id", "r2" },
                           { "checked", true },
                           { "cells", QJsonArray{ "board 9", cell( "100%", { { "sort", 1.0 } } ), "",
                                                  "two, words", "e2" } } },
              QJsonObject{ { "id", "r3" },
                           { "cells", QJsonArray{ "board 1", cell( "25%", { { "sort", 0.25 } } ), "",
                                                  "-dash", "e3" } } } } } };
    for ( auto it = extra.begin(); it != extra.end(); ++it ) {
        table.insert( it.key(), it.value() );
    }
    return table;
}

// A plugin view with fake host hooks: icons, an element menu that records its target, kept
// state in a map, and a save dialog.
struct View {
    PluginView view;
    QSignalSpy events;
    QJsonObject menuTarget;
    QString menuId;
    std::map<QString, QJsonObject>* store;
    QString savePath;

    View( const QJsonObject& table, std::map<QString, QJsonObject>* kept, bool extended = true )
        : view( PluginId, "main", dialogs() )
        , events( &view, &PluginView::eventRaised )
        , store( kept )
    {
        PluginViewQueries queries;
        queries.icon = []( const QJsonValue& reference ) {
            return reference.toString().startsWith( "builtin:" ) ? QIcon( QPixmap( 16, 16 ) ) : QIcon{};
        };
        queries.elementMenu = [ this ]( QMenu* menu, const QString&, const QString& id,
                                        const QJsonObject& target ) {
            menuId = id;
            menuTarget = target;
            menu->addSeparator();
            menu->addAction( "Plugin entry" )->setObjectName( "plugin_entry" );
        };
        queries.loadState = [ this ]( const QString& elementId ) {
            return store ? ( *store )[ elementId ] : QJsonObject{};
        };
        queries.saveState = [ this ]( const QString& elementId, const QJsonObject& state ) {
            if ( store ) {
                ( *store )[ elementId ] = state;
            }
        };
        view.setQueries( queries );
        view.setExtendedEvents( extended );
        view.setRoot( QJsonObject{ { "type", "column" },
                                   { "id", "body" },
                                   { "children", QJsonArray{ table } } } );
        view.show();
    }
    PluginViewDialogs dialogs()
    {
        PluginViewDialogs result;
        result.saveFile = [ this ]( QWidget*, const QString&, const QString&, const QString&,
                                    const QString& ) { return savePath; };
        return result;
    }
    QTableView* table() const
    {
        return qobject_cast<QTableView*>( view.element( "t" ) );
    }
    PluginTableBehaviour* behaviour() const
    {
        return table()->findChild<PluginTableBehaviour*>();
    }
    QJsonObject last() const
    {
        return events.last().front().toJsonObject();
    }
    // The last event of a type, and how many there were.
    QJsonObject lastOf( const QString& type ) const
    {
        for ( auto index = events.size() - 1; index >= 0; --index ) {
            const auto event = events.at( index ).front().toJsonObject();
            if ( event.value( "type" ) == type ) {
                return event;
            }
        }
        return {};
    }
    int countOf( const QString& type ) const
    {
        int count = 0;
        for ( const auto& event : events ) {
            count += event.front().toJsonObject().value( "type" ) == type ? 1 : 0;
        }
        return count;
    }
    // The shown index of a row's cell.
    QModelIndex index( const QString& rowId, int column ) const
    {
        auto* model = table()->model();
        for ( int row = 0; row < model->rowCount(); ++row ) {
            if ( model->index( row, 0 ).data( pluginview::RowIdRole ) == rowId ) {
                return model->index( row, column );
            }
        }
        return {};
    }
    QPoint center( const QString& rowId, int column ) const
    {
        return table()->visualRect( index( rowId, column ) ).center();
    }
};
} // namespace

TEST_CASE( "E17d cell objects, row icons, column options and sorting", "[ui][workbench][E17]" )
{
    View fixture( boardsTable(), nullptr );
    auto* table = fixture.table();
    REQUIRE( table );
    const auto name = fixture.index( "r1", 0 );
    REQUIRE( name.data( Qt::DisplayRole ) == "board 10" );
    REQUIRE( name.data( Qt::ToolTipRole ) == "Tenth" );
    REQUIRE( name.data( Qt::FontRole ).value<QFont>().bold() );
    REQUIRE_FALSE( name.data( Qt::DecorationRole ).value<QIcon>().isNull() ); // the row's icon
    const auto done = fixture.index( "r1", 1 );
    REQUIRE( done.data( pluginview::ProgressRole ).toDouble() == 0.5 );
    REQUIRE( done.data( Qt::ForegroundRole ).value<QColor>() == pluginview::toneColor( "warn" ) );
    REQUIRE( ( done.data( Qt::TextAlignmentRole ).value<Qt::Alignment>() & Qt::AlignRight ) );
    REQUIRE_FALSE( table->model()->headerData( 1, Qt::Horizontal, Qt::DecorationRole )
                       .value<QIcon>()
                       .isNull() );
    REQUIRE( fixture.index( "r2", 2 ).data( Qt::CheckStateRole ) == Qt::Checked );
    REQUIRE( fixture.index( "r1", 2 ).data( Qt::CheckStateRole ) == Qt::Unchecked );
    REQUIRE( table->isColumnHidden( 3 ) );
    // The user cannot make a column narrower than its min_width.
    table->horizontalHeader()->resizeSection( 2, 10 );
    REQUIRE( table->horizontalHeader()->sectionSize( 2 ) == 60 );
    // natural: board 1, board 9, board 10; `sort` values sort the Done column.
    table->sortByColumn( 0, Qt::AscendingOrder );
    REQUIRE( table->model()->index( 0, 0 ).data().toString() == "board 1" );
    REQUIRE( table->model()->index( 2, 0 ).data().toString() == "board 10" );
    table->sortByColumn( 1, Qt::DescendingOrder );
    REQUIRE( table->model()->index( 0, 0 ).data().toString() == "board 9" );
    // A column that is not sortable keeps the order.
    table->sortByColumn( 2, Qt::AscendingOrder );
    REQUIRE( table->model()->index( 0, 0 ).data().toString() == "board 9" );
    // header: false makes a list.
    fixture.view.applyPatch( { QJsonObject{ { "id", "t" }, { "set", QJsonObject{ { "header", false } } } } } );
    REQUIRE( table->horizontalHeader()->isHidden() );
}

TEST_CASE( "E17d check columns, toggle selection, the selection patch and activated",
           "[ui][workbench][E17]" )
{
    View fixture( boardsTable( { { "toggle_selection", true } } ), nullptr );
    auto* table = fixture.table();
    REQUIRE( table->selectionMode() == QAbstractItemView::MultiSelection );
    table->setFocus();
    // Space on a check cell changes the row and reports it.
    table->setCurrentIndex( fixture.index( "r1", 2 ) );
    QTest::keyClick( table, Qt::Key_Space );
    REQUIRE( waitFor( [ & ] { return fixture.countOf( "checked" ) == 1; } ) );
    auto value = fixture.lastOf( "checked" ).value( "value" ).toObject();
    REQUIRE( value.value( "row_id" ) == "r1" );
    REQUIRE( value.value( "row_ids" ).toArray() == QJsonArray{ "r1" } );
    REQUIRE( value.value( "checked" ) == true );
    // Shift sets the range from the last check box (r2 was already checked).
    table->selectionModel()->setCurrentIndex( fixture.index( "r3", 2 ), QItemSelectionModel::NoUpdate );
    QTest::keyClick( table, Qt::Key_Space, Qt::ShiftModifier );
    REQUIRE( waitFor( [ & ] { return fixture.countOf( "checked" ) == 2; } ) );
    value = fixture.lastOf( "checked" ).value( "value" ).toObject();
    REQUIRE( value.value( "row_id" ) == "r3" );
    REQUIRE( value.value( "row_ids" ).toArray() == QJsonArray{ "r3" } );
    REQUIRE( fixture.index( "r3", 2 ).data( Qt::CheckStateRole ) == Qt::Checked );
    // Toggle selection: each click adds or removes a row; Shift+click adds the range.
    table->clearSelection();
    QTest::mouseClick( table->viewport(), Qt::LeftButton, {}, fixture.center( "r1", 0 ) );
    QTest::mouseClick( table->viewport(), Qt::LeftButton, Qt::ShiftModifier,
                       fixture.center( "r3", 0 ) );
    REQUIRE( table->selectionModel()->selectedRows().size() == 3 );
    QTest::mouseClick( table->viewport(), Qt::LeftButton, {}, fixture.center( "r2", 0 ) );
    REQUIRE( table->selectionModel()->selectedRows().size() == 2 );
    // `selected` carries the current row for a plugin with `handles`.
    REQUIRE( fixture.lastOf( "selected" ).value( "current" ) == "r2" );
    // The selection patch sets the selection and the current row without an event.
    const auto before = fixture.events.size();
    fixture.view.applyPatch( { QJsonObject{ { "id", "t" },
                                            { "select", QJsonArray{ "r3", "zz" } },
                                            { "current", "r3" },
                                            { "scroll_to", "r3" } } } );
    REQUIRE( fixture.events.size() == before );
    REQUIRE( table->selectionModel()->selectedRows().size() == 1 );
    REQUIRE( fixture.behaviour()->currentRowId() == "r3" );
    // activated: a double-click or Enter on a cell that is not editable.
    QTest::mouseDClick( table->viewport(), Qt::LeftButton, {}, fixture.center( "r2", 0 ) );
    REQUIRE( waitFor( [ & ] { return fixture.countOf( "activated" ) == 1; } ) );
    REQUIRE( fixture.lastOf( "activated" ).value( "value" ).toObject()
             == QJsonObject{ { "row_id", "r2" }, { "column_id", "name" } } );
    table->setCurrentIndex( fixture.index( "r1", 1 ) );
    QTest::keyClick( table, Qt::Key_Return );
    REQUIRE( fixture.countOf( "activated" ) == 2 );
    REQUIRE( fixture.lastOf( "activated" ).value( "value" ).toObject().value( "row_id" ) == "r1" );
    table->setCurrentIndex( fixture.index( "r1", 4 ) ); // editable: no activated
    QTest::keyClick( table, Qt::Key_Return );
    REQUIRE( fixture.countOf( "activated" ) == 2 );
}

TEST_CASE( "E17d plugins without handles get no new event keys", "[ui][workbench][E17]" )
{
    View fixture( boardsTable(), nullptr, false );
    auto* table = fixture.table();
    QTest::mouseClick( table->viewport(), Qt::LeftButton, {}, fixture.center( "r1", 0 ) );
    REQUIRE( fixture.countOf( "selected" ) > 0 );
    REQUIRE_FALSE( fixture.lastOf( "selected" ).contains( "current" ) );
    QTest::mouseDClick( table->viewport(), Qt::LeftButton, {}, fixture.center( "r2", 0 ) );
    QTest::qWait( 50 );
    REQUIRE( fixture.countOf( "activated" ) == 0 );
}

TEST_CASE( "E17d context and header menus, Copy, Copy All and Export CSV",
           "[ui][workbench][E17]" )
{
    QTemporaryDir temp;
    View fixture( boardsTable( { { "context_menu", "rows" }, { "export", true } } ), nullptr );
    fixture.savePath = QDir( temp.path() ).filePath( "boards.csv" );
    auto* table = fixture.table();
    // A right-click on a row selects it and builds the host entries, then the plugin's.
    std::unique_ptr<QMenu> menu( fixture.view.buildElementMenu( "t", fixture.center( "r2", 0 ) ) );
    REQUIRE( menu );
    const auto names = [ & ] {
        QStringList result;
        for ( const auto* action : menu->actions() ) {
            if ( !action->isSeparator() ) {
                result.append( action->objectName() );
            }
        }
        return result;
    }();
    const auto base = PluginView::objectNameFor( PluginId, "main", "t" );
    REQUIRE( names == QStringList{ base + "_copy", base + "_copy_all", base + "_export", "plugin_entry" } );
    REQUIRE( fixture.menuId == "rows" );
    REQUIRE( fixture.menuTarget.value( "row_id" ) == "r2" );
    REQUIRE( fixture.menuTarget.value( "column_id" ) == "name" );
    REQUIRE( fixture.menuTarget.value( "selected_ids" ).toArray() == QJsonArray{ "r2" } );
    REQUIRE( fixture.menuTarget.value( "selected_count" ) == 1 );
    // Copy: the selected rows, shown columns in shown order; Copy All adds a header line.
    menu->findChild<QAction*>( base + "_copy" )->trigger();
    REQUIRE( QApplication::clipboard()->text() == "board 9\t100%\t\te2" );
    menu->findChild<QAction*>( base + "_copy_all" )->trigger();
    REQUIRE( QApplication::clipboard()->text().startsWith( "Name\tDone\tUse\tEdit\nboard 10\t50%" ) );
    // Export CSV: header and rows; a formula is never a formula, quotes where needed.
    table->setColumnHidden( 3, false );
    menu->findChild<QAction*>( base + "_export" )->trigger();
    QFile csv( fixture.savePath );
    REQUIRE( csv.open( QIODevice::ReadOnly ) );
    const auto content = QString::fromUtf8( csv.readAll() );
    REQUIRE( content.startsWith( "Name,Done,Use,Note,Edit\r\n" ) );
    REQUIRE( content.contains( "board 10,50%,,'=SUM(1),e1\r\n" ) );
    REQUIRE( content.contains( "\"two, words\"" ) );
    REQUIRE( content.contains( ",'-dash," ) );
    // Copied text is formula-safe as well: a paste into a spreadsheet never runs plugin text.
    menu->findChild<QAction*>( base + "_copy_all" )->trigger();
    const auto copied = QApplication::clipboard()->text();
    REQUIRE( copied.contains( "\t'=SUM(1)\t" ) );
    REQUIRE( copied.contains( "\t'-dash\t" ) );
    REQUIRE( copied.contains( "\t100%\t" ) ); // numbers stay as they are
    // The header menu hides and shows columns; the last shown column stays.
    std::unique_ptr<QMenu> columns( fixture.behaviour()->buildHeaderMenu() );
    REQUIRE( columns );
    columns->findChild<QAction*>( base + "_column_done" )->trigger();
    REQUIRE( table->isColumnHidden( 1 ) );
    // Without copy, export and context_menu there is no menu at all.
    View plain( boardsTable( { { "copy", false } } ), nullptr );
    REQUIRE( plain.view.buildElementMenu( "t", plain.center( "r1", 0 ) ) == nullptr );
    // Copy All stops at whole rows before 1 MiB of text: 300 rows of 4,000 characters would be
    // 1.2 million.
    QJsonArray rows;
    for ( int row = 0; row < 300; ++row ) {
        rows.append( QJsonObject{
            { "id", QString( "big%1" ).arg( row ) },
            { "cells", QJsonArray{ QString( "%1" ).arg( row, 3, 10, QChar( '0' ) ) + QString( 3997, 'x' ) } } } );
    }
    View big( QJsonObject{ { "type", "table" },
                           { "id", "t" },
                           { "columns", QJsonArray{ QJsonObject{ { "id", "text" }, { "title", "Text" } } } },
                           { "rows", rows } },
              nullptr );
    const auto all = big.behaviour()->copyText( true );
    REQUIRE( all.size() <= 1024 * 1024 );
    REQUIRE( all.size() > 1000 * 1000 );
    const auto lines = all.split( QLatin1Char( '\n' ) );
    REQUIRE( lines.front() == "Text" );
    REQUIRE( lines.size() < 301 );
    REQUIRE( lines.back().size() == 4000 ); // the last row is whole
}

TEST_CASE( "E17d kept columns and reordering", "[ui][workbench][E17]" )
{
    std::map<QString, QJsonObject> store;
    {
        View fixture( boardsTable( { { "reorderable", true } } ), &store );
        auto* table = fixture.table();
        // Reordering sends `moved`; the plugin confirms, so the rows stay.
        REQUIRE( fixture.behaviour()->moveRows( { "r3" }, "r1" ) );
        REQUIRE( fixture.lastOf( "moved" ).value( "value" ).toObject()
                 == QJsonObject{ { "row_ids", QJsonArray{ "r3" } }, { "before", "r1" } } );
        REQUIRE( fixture.behaviour()->moveRows( { "r1" }, {} ) );
        REQUIRE( fixture.lastOf( "moved" ).value( "value" ).toObject().value( "before" ).isNull() );
        REQUIRE( table->model()->index( 0, 0 ).data().toString() == "board 10" );
        // The user hides a column, widens one and sorts: kept after a moment.
        std::unique_ptr<QMenu> columns( fixture.behaviour()->buildHeaderMenu() );
        columns->findChild<QAction*>( PluginView::objectNameFor( PluginId, "main", "t" )
                                      + "_column_done" )
            ->trigger();
        table->horizontalHeader()->resizeSection( 0, 222 );
        table->sortByColumn( 0, Qt::DescendingOrder );
        // While sorted, rows do not move.
        REQUIRE_FALSE( fixture.behaviour()->moveRows( { "r3" }, "r1" ) );
        QTest::mouseRelease( table->horizontalHeader()->viewport(), Qt::LeftButton );
        REQUIRE( waitFor( [ & ] {
            return store[ "t" ].value( "sort" ).toObject().value( "order" ) == "desc";
        } ) );
    }
    // A new view of the same table gets them back.
    View again( boardsTable(), &store );
    auto* table = again.table();
    REQUIRE( table->isColumnHidden( 1 ) );
    REQUIRE( table->horizontalHeader()->sectionSize( 0 ) == 222 );
    REQUIRE( table->model()->index( 0, 0 ).data().toString() == "board 10" ); // natural, desc
}

TEST_CASE( "E17d hidden widths, minimum widths when shown, focus without selection, save on close",
           "[ui][workbench][E17]" )
{
    std::map<QString, QJsonObject> store;
    const auto base = PluginView::objectNameFor( PluginId, "main", "t" );
    {
        // The note column (hidden at first) has a minimum width above its default width.
        auto element = boardsTable();
        auto columns = element.value( "columns" ).toArray();
        auto note = columns.at( 3 ).toObject();
        note.insert( "min_width", 333 );
        columns.replace( 3, note );
        element.insert( "columns", columns );
        View fixture( element, &store );
        auto* table = fixture.table();
        auto* header = table->horizontalHeader();
        // Shown from the header menu, it is at least its minimum width at once.
        std::unique_ptr<QMenu> show( fixture.behaviour()->buildHeaderMenu() );
        show->findChild<QAction*>( base + "_column_note" )->trigger();
        REQUIRE_FALSE( table->isColumnHidden( 3 ) );
        REQUIRE( header->sectionSize( 3 ) >= 333 );
        // A resized column that is then hidden keeps its width in the kept state.
        header->resizeSection( 0, 222 );
        std::unique_ptr<QMenu> hide( fixture.behaviour()->buildHeaderMenu() );
        hide->findChild<QAction*>( base + "_column_name" )->trigger();
        REQUIRE( table->isColumnHidden( 0 ) );
        const auto name = fixture.behaviour()->state().value( "columns" ).toArray().at( 0 ).toObject();
        REQUIRE( name.value( "hidden" ) == true );
        REQUIRE( name.value( "width" ) == 222 );
        // The view closes within the save delay: the change is written all the same.
    }
    const auto saved = store[ "t" ].value( "columns" ).toArray().at( 0 ).toObject();
    REQUIRE( saved.value( "hidden" ) == true );
    REQUIRE( saved.value( "width" ) == 222 );
    // Showing it again in a new view gives back that width.
    {
        View again( boardsTable(), &store );
        std::unique_ptr<QMenu> show( again.behaviour()->buildHeaderMenu() );
        show->findChild<QAction*>( base + "_column_name" )->trigger();
        REQUIRE( again.table()->horizontalHeader()->sectionSize( 0 ) == 222 );
    }
    // `current` sets the keyboard focus in a table without selection too.
    View plain( boardsTable( { { "selection", "none" } } ), nullptr );
    plain.view.applyPatch( { QJsonObject{ { "id", "t" }, { "current", "r2" } } } );
    REQUIRE( plain.behaviour()->currentRowId() == "r2" );
}

TEST_CASE( "E17d menu answers stay within their budget, drags end when rows change, menus resolve "
           "columns by id",
           "[ui][workbench][E17]" )
{
    // Dynamic answers: one with no cache time is kept only as a native menu bar's last answer,
    // an expired one goes when looked up, and all of them stay within 16 MiB.
    auto& registry = PluginMenuRegistry::instance();
    const QString plugin = QStringLiteral( "org.test.cache" );
    const auto prefix = QString::number( plugin.size() ) + QLatin1Char( ':' ) + plugin;
    registry.forgetPlugin( plugin );
    const auto count = registry.answerCount();
    const QJsonArray one{ QJsonObject{ { "id", "a" }, { "title", "A" } } };
    registry.storeAnswer( prefix + "now", one, 0 );
    REQUIRE( registry.answerCount() == count );
    registry.storeAnswer( prefix + "bar", one, 0, true );
    REQUIRE( registry.answerCount() == count + 1 );
    REQUIRE( registry.answer( prefix + "bar", true ) );
    registry.storeAnswer( prefix + "soon", one, 1 );
    QTest::qWait( 5 );
    REQUIRE_FALSE( registry.answer( prefix + "soon", false ) );
    REQUIRE( registry.answerCount() == count + 1 ); // dropped on lookup
    const QJsonArray big{ QJsonObject{ { "id", "big" }, { "title", QString( 1000 * 1000, 'x' ) } } };
    for ( int index = 0; index < 40; ++index ) {
        registry.storeAnswer( prefix + QString::number( index ), big, 60000 );
    }
    REQUIRE( registry.answerBytes() <= 16 * 1024 * 1024 );
    REQUIRE( registry.answer( prefix + "39", false ) ); // the newest stays
    REQUIRE_FALSE( registry.answer( prefix + "0", false ) ); // the oldest went
    registry.forgetPlugin( plugin );
    REQUIRE( registry.answerCount() == count );

    // A press that may start a drag is called off when the rows change before the drag: the
    // held selection is reported once the patch is done, and no drag starts from stale ranges.
    View toggle( boardsTable( { { "reorderable", true }, { "toggle_selection", true } } ), nullptr );
    auto* viewport = toggle.table()->viewport();
    QTest::mouseClick( viewport, Qt::LeftButton, {}, toggle.center( "r1", 0 ) );
    QTest::mouseClick( viewport, Qt::LeftButton, {}, toggle.center( "r2", 0 ) );
    const auto clicks = toggle.countOf( "selected" );
    const auto at = toggle.center( "r1", 0 );
    QTest::mousePress( viewport, Qt::LeftButton, {}, at );
    REQUIRE( toggle.behaviour()->draggedRows() == QStringList{ "r1", "r2" } );
    toggle.view.applyPatch( { QJsonObject{
        { "id", "t" },
        { "insert_rows", QJsonArray{ QJsonObject{ { "id", "x" }, { "cells", QJsonArray{ "x" } } } } },
        { "before", "r2" } } } );
    REQUIRE( toggle.behaviour()->draggedRows().isEmpty() );
    REQUIRE( waitFor( [ & ] { return toggle.countOf( "selected" ) == clicks + 1; } ) );
    REQUIRE( toggle.lastOf( "selected" ).value( "value" ).toArray() == QJsonArray{ "r2" } );
    QTest::mouseRelease( viewport, Qt::LeftButton, {}, at );
    QTest::qWait( 20 );
    REQUIRE( toggle.countOf( "selected" ) == clicks + 1 ); // nothing more on the release

    // A header menu entry finds its column by id when picked: a column that went does nothing,
    // and the last shown column stays.
    View columns( QJsonObject{ { "type", "table" },
                               { "id", "t" },
                               { "columns", QJsonArray{ QJsonObject{ { "id", "a" } }, QJsonObject{ { "id", "b" } } } },
                               { "rows", QJsonArray{ QJsonObject{ { "id", "r" }, { "cells", QJsonArray{ "1", "2" } } } } } },
                  nullptr );
    const auto base = PluginView::objectNameFor( PluginId, "main", "t" );
    std::unique_ptr<QMenu> menu( columns.behaviour()->buildHeaderMenu() );
    auto* hideA = menu->findChild<QAction*>( base + "_column_a" );
    auto* hideB = menu->findChild<QAction*>( base + "_column_b" );
    REQUIRE( hideA );
    REQUIRE( hideB );
    columns.view.applyPatch( { QJsonObject{ { "id", "t" },
                                            { "set", QJsonObject{ { "columns", QJsonArray{ QJsonObject{ { "id", "b" } } } } } } } } );
    hideA->trigger(); // column a is gone
    hideB->trigger(); // b is the last shown column
    REQUIRE( columns.table()->model()->columnCount() == 1 );
    REQUIRE_FALSE( columns.table()->isColumnHidden( 0 ) );
}

TEST_CASE( "E17d a patch's own selection is never echoed after a press, menu answers belong to "
           "the whole context",
           "[ui][workbench][E17]" )
{
    // A toggle table: the press deselects its row, then a patch replaces the rows and sets the
    // selection. The drag is called off and the plugin hears nothing: neither its own selection
    // nor the press's change that the patch replaced.
    View toggle( boardsTable( { { "reorderable", true }, { "toggle_selection", true } } ), nullptr );
    auto* viewport = toggle.table()->viewport();
    QTest::mouseClick( viewport, Qt::LeftButton, {}, toggle.center( "r1", 0 ) );
    QTest::mouseClick( viewport, Qt::LeftButton, {}, toggle.center( "r2", 0 ) );
    const auto clicks = toggle.countOf( "selected" );
    const auto at = toggle.center( "r1", 0 );
    QTest::mousePress( viewport, Qt::LeftButton, {}, at );
    REQUIRE( toggle.behaviour()->holdsSelection() );
    toggle.view.applyPatch( { QJsonObject{
        { "id", "t" },
        { "replace_rows", QJsonArray{ QJsonObject{ { "id", "r1" }, { "cells", QJsonArray{ "1" } } },
                                      QJsonObject{ { "id", "r2" }, { "cells", QJsonArray{ "2" } } },
                                      QJsonObject{ { "id", "r3" }, { "cells", QJsonArray{ "3" } } } } },
        { "select", QJsonArray{ "r3" } } } } );
    REQUIRE_FALSE( toggle.behaviour()->holdsSelection() );
    REQUIRE( toggle.behaviour()->draggedRows().isEmpty() );
    QTest::qWait( 20 );
    REQUIRE( toggle.countOf( "selected" ) == clicks );
    REQUIRE( toggle.behaviour()->selectedRowIds( 10 ) == QStringList{ "r3" } );
    QTest::mouseRelease( viewport, Qt::LeftButton, {}, at );
    QTest::qWait( 20 );
    REQUIRE( toggle.countOf( "selected" ) == clicks );

    // A press that changed nothing (an extended table keeps the selection): rows changing under
    // it send nothing either.
    View extended( boardsTable( { { "reorderable", true } } ), nullptr );
    auto* plain = extended.table()->viewport();
    QTest::mouseClick( plain, Qt::LeftButton, {}, extended.center( "r1", 0 ) );
    const auto before = extended.countOf( "selected" );
    QTest::mousePress( plain, Qt::LeftButton, {}, extended.center( "r1", 0 ) );
    REQUIRE( extended.behaviour()->holdsSelection() );
    extended.view.applyPatch( { QJsonObject{
        { "id", "t" },
        { "insert_rows", QJsonArray{ QJsonObject{ { "id", "x" }, { "cells", QJsonArray{ "x" } } } } },
        { "before", "r1" } } } );
    REQUIRE_FALSE( extended.behaviour()->holdsSelection() );
    QTest::mouseRelease( plain, Qt::LeftButton, {}, extended.center( "r1", 0 ) );
    QTest::qWait( 20 );
    REQUIRE( extended.countOf( "selected" ) == before );
    REQUIRE( extended.behaviour()->selectedRowIds( 10 ) == QStringList{ "r1" } );

    // A dynamic submenu's cached answer belongs to the whole context menu/populate sends: a log
    // menu opened on another line, selection, view, pane or window asks the plugin again.
    PluginMenuContext context;
    context.location = "log";
    context.windowId = "w1";
    context.tabId = "tab";
    context.view = "main";
    context.line = 10;
    context.ranges = { { 10, 12 } };
    context.selectedLines = 3;
    const auto key = []( const PluginMenuContext& menuContext ) {
        return PluginMenus::populateKey( PluginId, "log_menu", "dynamic", menuContext );
    };
    const std::vector<std::function<void( PluginMenuContext& )>> changes{
        []( PluginMenuContext& other ) { other.line = 11; },
        []( PluginMenuContext& other ) { other.ranges = { { 20, 22 } }; },
        []( PluginMenuContext& other ) { other.selectedLines = 4; },
        []( PluginMenuContext& other ) { other.view = "filtered"; },
        []( PluginMenuContext& other ) { other.paneIndex = 1; },
        []( PluginMenuContext& other ) { other.windowId = "w2"; },
        []( PluginMenuContext& other ) { other.currentTabId = "other"; },
    };
    for ( const auto& change : changes ) {
        auto other = context;
        change( other );
        REQUIRE( key( other ) != key( context ) );
    }
    // The selected text is only sent when an entry is picked: it does not split the cache.
    auto picked = context;
    picked.pickedText = "text";
    picked.textRead = true;
    REQUIRE( key( picked ) == key( context ) );
}

TEST_CASE( "E17d selection events stay bounded and unechoed, text sorts as text, columns keep sorts",
           "[ui][workbench][E17]" )
{
    // The ids of `selected` are bounded by their encoded JSON: a control character takes six bytes.
    {
        QJsonArray rows;
        for ( int row = 0; row < 63; ++row ) {
            rows.append( QJsonObject{
                { "id", QString( "%1" ).arg( row, 2, 10, QChar( '0' ) ) + QString( 4090, QChar( 0x01 ) ) },
                { "cells", QJsonArray{ "x" } } } );
        }
        View controls( QJsonObject{ { "type", "table" },
                                    { "id", "t" },
                                    { "selection", "multi" },
                                    { "columns", QJsonArray{ QJsonObject{ { "id", "a" } } } },
                                    { "rows", rows } },
                       nullptr );
        controls.table()->selectAll();
        const auto selected = controls.lastOf( "selected" );
        const auto ids = selected.value( "value" ).toArray();
        REQUIRE_FALSE( ids.isEmpty() );
        REQUIRE( ids.size() < 63 );
        REQUIRE( QJsonDocument( ids ).toJson( QJsonDocument::Compact ).size() <= pluginview::MaxIdBytes );
        REQUIRE( selected.value( "selected_count" ) == 63 );
        REQUIRE( selected.value( "truncated" ) == true );
    }
    // A patch entry that replaces the rows and sets the selection sends no `selected` at all.
    {
        View fixture( boardsTable(), nullptr );
        auto* table = fixture.table();
        table->selectionModel()->select( fixture.index( "r1", 0 ), QItemSelectionModel::ClearAndSelect
                                                                       | QItemSelectionModel::Rows );
        const auto before = fixture.countOf( "selected" );
        fixture.view.applyPatch( { QJsonObject{
            { "id", "t" },
            { "replace_rows", QJsonArray{ QJsonObject{ { "id", "r9" }, { "cells", QJsonArray{ "nine" } } } } },
            { "select", QJsonArray{ "r9" } } } } );
        REQUIRE( fixture.countOf( "selected" ) == before );
        REQUIRE( fixture.behaviour()->selectedRowIds( 10 ) == QStringList{ "r9" } );
    }
    // sort_as "text" compares the text itself: "02" is no number 2 there.
    {
        View texts( QJsonObject{
                        { "type", "table" },
                        { "id", "t" },
                        { "sortable", true },
                        { "columns", QJsonArray{ QJsonObject{ { "id", "a" }, { "sort_as", "text" } } } },
                        { "rows", QJsonArray{ QJsonObject{ { "id", "r1" }, { "cells", QJsonArray{ "1" } } },
                                              QJsonObject{ { "id", "r10" }, { "cells", QJsonArray{ "10" } } },
                                              QJsonObject{ { "id", "r02" }, { "cells", QJsonArray{ "02" } } } } } },
                    nullptr );
        texts.table()->sortByColumn( 0, Qt::AscendingOrder );
        const auto* model = texts.table()->model();
        REQUIRE( model->index( 0, 0 ).data().toString() == "02" );
        REQUIRE( model->index( 1, 0 ).data().toString() == "1" );
        REQUIRE( model->index( 2, 0 ).data().toString() == "10" );
    }
    // New columns: a pending sort is saved with its own column, and the sort index of a column
    // that is gone does not sort another one.
    std::map<QString, QJsonObject> store;
    const auto columnsOf = []( const QStringList& ids ) {
        QJsonArray columns;
        for ( const auto& id : ids ) {
            columns.append( QJsonObject{ { "id", id }, { "title", id.toUpper() } } );
        }
        return columns;
    };
    View sorted( QJsonObject{ { "type", "table" },
                              { "id", "t" },
                              { "sortable", true },
                              { "columns", columnsOf( { "a", "b" } ) },
                              { "rows", QJsonArray{ QJsonObject{ { "id", "r1" }, { "cells", QJsonArray{ "2", "x" } } },
                                                    QJsonObject{ { "id", "r2" }, { "cells", QJsonArray{ "1", "y" } } } } } },
                 &store );
    auto* table = sorted.table();
    table->sortByColumn( 0, Qt::AscendingOrder );
    QTest::mouseRelease( table->horizontalHeader()->viewport(), Qt::LeftButton ); // schedules a save
    REQUIRE( table->model()->index( 0, 0 ).data( pluginview::RowIdRole ) == "r2" );
    sorted.view.applyPatch( { QJsonObject{ { "id", "t" }, { "set", QJsonObject{ { "columns", columnsOf( { "b", "c" } ) } } } } } );
    REQUIRE( store[ "t" ].value( "sort" ).toObject().value( "column" ) == "a" );
    REQUIRE( table->horizontalHeader()->sortIndicatorSection() == -1 );
    REQUIRE( table->model()->index( 0, 0 ).data( pluginview::RowIdRole ) == "r1" ); // unsorted
    QTest::qWait( 700 );
    REQUIRE( store[ "t" ].value( "sort" ).toObject().value( "column" ) != "b" );
    // A press on a selected row of a toggle table holds its `selected` until the release.
    View toggle( boardsTable( { { "reorderable", true }, { "toggle_selection", true } } ), nullptr );
    auto* viewport = toggle.table()->viewport();
    QTest::mouseClick( viewport, Qt::LeftButton, {}, toggle.center( "r1", 0 ) );
    QTest::mouseClick( viewport, Qt::LeftButton, {}, toggle.center( "r2", 0 ) );
    const auto clicks = toggle.countOf( "selected" );
    QTest::mousePress( viewport, Qt::LeftButton, {}, toggle.center( "r1", 0 ) );
    REQUIRE( toggle.countOf( "selected" ) == clicks ); // the press toggled r1 off, silently
    QTest::mouseRelease( viewport, Qt::LeftButton, {}, toggle.center( "r1", 0 ) );
    REQUIRE( toggle.countOf( "selected" ) == clicks + 1 ); // no drag: one event, after the release
    REQUIRE( toggle.lastOf( "selected" ).value( "value" ).toArray() == QJsonArray{ "r2" } );
}

TEST_CASE( "E17d drags move the selection, bars show on alternate rows, a new root saves first",
           "[ui][workbench][E17]" )
{
    // A press in a toggle-selection table deselects its row at once; a drag it starts still moves
    // the rows selected before the press.
    {
        View toggle( boardsTable( { { "reorderable", true }, { "toggle_selection", true } } ), nullptr );
        auto* viewport = toggle.table()->viewport();
        QTest::mouseClick( viewport, Qt::LeftButton, {}, toggle.center( "r1", 0 ) );
        QTest::mouseClick( viewport, Qt::LeftButton, {}, toggle.center( "r2", 0 ) );
        REQUIRE( toggle.behaviour()->selectedRowIds( 10 ) == QStringList{ "r1", "r2" } );
        QTest::mousePress( viewport, Qt::LeftButton, {}, toggle.center( "r1", 0 ) );
        REQUIRE( toggle.behaviour()->selectedRowIds( 10 ) == QStringList{ "r2" } );
        REQUIRE( toggle.behaviour()->draggedRows() == QStringList{ "r1", "r2" } );
        QTest::mouseRelease( viewport, Qt::LeftButton, {}, toggle.center( "r1", 0 ) );
        REQUIRE( toggle.behaviour()->draggedRows().isEmpty() );
        // A row that was not selected moves alone.
        QTest::mousePress( viewport, Qt::LeftButton, {}, toggle.center( "r3", 0 ) );
        REQUIRE( toggle.behaviour()->draggedRows() == QStringList{ "r3" } );
        QTest::mouseRelease( viewport, Qt::LeftButton, {}, toggle.center( "r3", 0 ) );
    }
    // The whole selection decides, not only its first 1,000 ids: a press on the 1,051st of
    // 1,100 selected rows moves the selection, whose first 1,000 ids `moved` sends.
    {
        QJsonArray many;
        for ( int row = 0; row < 1100; ++row ) {
            many.append( QJsonObject{ { "id", QString( "m%1" ).arg( row ) },
                                      { "cells", QJsonArray{ QString( "row %1" ).arg( row ) } } } );
        }
        View lots( QJsonObject{ { "type", "table" },
                                { "id", "t" },
                                { "selection", "multi" },
                                { "toggle_selection", true },
                                { "reorderable", true },
                                { "columns", QJsonArray{ QJsonObject{ { "id", "name" } } } },
                                { "rows", many } },
                   nullptr );
        auto* table = lots.table();
        table->selectAll();
        const auto target = lots.index( "m1050", 0 );
        table->scrollTo( target );
        const auto at = table->visualRect( target ).center();
        QTest::mousePress( table->viewport(), Qt::LeftButton, {}, at );
        const auto dragged = lots.behaviour()->draggedRows();
        REQUIRE( dragged.size() == pluginview::MaxTableIds );
        REQUIRE( dragged.front() == "m0" );
        REQUIRE( dragged.back() == "m999" );
        QTest::mouseRelease( table->viewport(), Qt::LeftButton, {}, at );
        REQUIRE( lots.behaviour()->draggedRows().isEmpty() ); // no drag: the snapshot is gone
    }
    // A full bar on an alternate row is not covered by the row's background.
    {
        auto bars = boardsTable( { { "selection", "none" }, { "sortable", false } } );
        QJsonArray rows;
        for ( int row = 0; row < 4; ++row ) {
            const auto filled = row == 1; // rows 1 and 3 are alternate rows
            rows.append( QJsonObject{
                { "id", QString( "p%1" ).arg( row ) },
                { "cells", QJsonArray{ filled ? QJsonValue( cell( "", { { "progress", 1.0 } } ) )
                                              : QJsonValue( "" ),
                                       "", "", "", "" } } } );
        }
        bars.insert( "rows", rows );
        View fixture( bars, nullptr );
        auto* table = fixture.table();
        table->clearSelection();
        table->setCurrentIndex( {} );
        const auto image = table->viewport()->grab().toImage();
        const auto pixel = [ & ]( const QString& rowId ) {
            return image.pixelColor( fixture.center( rowId, 0 ) * image.devicePixelRatio() );
        };
        REQUIRE( pixel( "p1" ) != pixel( "p3" ) );
    }
    // A root of another shape replaces the tables: their column changes are saved first, for
    // every table of the view.
    std::map<QString, QJsonObject> store;
    View fixture( QJsonObject{ { "type", "column" },
                               { "id", "tables" },
                               { "children", QJsonArray{ boardsTable(), boardsTable( { { "id", "t2" } } ) } } },
                  &store );
    auto* second = qobject_cast<QTableView*>( fixture.view.element( "t2" ) );
    REQUIRE( second );
    // A user's resize ends with a release on the header, which schedules the save.
    fixture.table()->horizontalHeader()->resizeSection( 0, 211 );
    QTest::mouseRelease( fixture.table()->horizontalHeader()->viewport(), Qt::LeftButton );
    second->horizontalHeader()->resizeSection( 0, 212 );
    QTest::mouseRelease( second->horizontalHeader()->viewport(), Qt::LeftButton );
    REQUIRE( store[ "t" ].value( "columns" ).toArray().at( 0 ).toObject().value( "width" ) != 211 );
    fixture.view.setRoot( QJsonObject{
        { "type", "column" },
        { "id", "body" },
        { "children", QJsonArray{ QJsonObject{ { "type", "label" }, { "id", "done" }, { "text", "Done" } } } } } );
    REQUIRE( store[ "t" ].value( "columns" ).toArray().at( 0 ).toObject().value( "width" ) == 211 );
    REQUIRE( store[ "t2" ].value( "columns" ).toArray().at( 0 ).toObject().value( "width" ) == 212 );
}

TEST_CASE( "E17d ranges, menus, Copy and sorting at their edges", "[ui][workbench][E17]" )
{
    const auto texts = [ & ]( QTableView* view ) {
        QStringList result;
        for ( int row = 0; row < view->model()->rowCount(); ++row ) {
            result.append( view->model()->index( row, 0 ).data().toString() );
        }
        return result;
    };
    const auto oneColumn = [ & ]( const QString& sortAs, const QStringList& cells ) {
        QJsonArray rows;
        for ( qsizetype at = 0; at < cells.size(); ++at ) {
            rows.append( QJsonObject{ { "id", QStringLiteral( "r%1" ).arg( at ) },
                                      { "cells", QJsonArray{ cells.at( at ) } } } );
        }
        return QJsonObject{ { "type", "table" }, { "id", "t" }, { "sortable", true },
                            { "columns", QJsonArray{ QJsonObject{ { "id", "v" }, { "title", "V" },
                                                                  { "sort_as", sortAs } } } },
                            { "rows", rows } };
    };
    SECTION( "natural sort skips leading zeros of any length; number sort puts nan last" )
    {
        const auto zeros = QStringLiteral( "item " ) + QString( 4000, QLatin1Char( '0' ) ) + "2";
        View natural( oneColumn( "natural", { "item 10", zeros, "item 1" } ), nullptr );
        natural.table()->sortByColumn( 0, Qt::AscendingOrder );
        REQUIRE( texts( natural.table() ) == QStringList{ "item 1", zeros, "item 10" } );
        View number( oneColumn( "number", { "x", "nan", "2", "inf", "1" } ), nullptr );
        number.table()->sortByColumn( 0, Qt::AscendingOrder );
        REQUIRE( texts( number.table() ) == QStringList{ "1", "2", "inf", "nan", "x" } );
        // Descending: the numbers reverse, text that is no number stays last.
        number.table()->sortByColumn( 0, Qt::DescendingOrder );
        REQUIRE( texts( number.table() ) == QStringList{ "2", "1", "x", "nan", "inf" } );
    }
    SECTION( "an element menu context keeps its ids within the byte budget" )
    {
        // 300 selected rows with ids of about 4,000 characters: the context stays far below the
        // 1 MiB frame limit, and selected_count stays the full count.
        QJsonArray rows;
        for ( int at = 0; at < 300; ++at ) {
            rows.append( QJsonObject{
                { "id", QString( 4000, QLatin1Char( 'a' ) ) + QString::number( at ) },
                { "cells", QJsonArray{ QString::number( at ) } } } );
        }
        View big( QJsonObject{ { "type", "table" }, { "id", "t" }, { "selection", "multi" },
                               { "columns", QJsonArray{ "v" } }, { "rows", rows } },
                  nullptr );
        big.table()->selectAll();
        const auto target = big.behaviour()->menuTarget( {}, {} );
        REQUIRE( target.value( "selected_count" ) == 300 );
        const auto ids = target.value( "selected_ids" ).toArray();
        REQUIRE( ids.size() > 50 );
        REQUIRE( ids.size() < 300 );
        REQUIRE( QJsonDocument( ids ).toJson( QJsonDocument::Compact ).size()
                 <= pluginview::MaxIdBytes + 2 );
    }
    SECTION( "a Shift check range over the id limit keeps the clicked row" )
    {
        QJsonArray rows;
        for ( int at = 0; at < 1200; ++at ) {
            rows.append( QJsonObject{ { "id", QStringLiteral( "r%1" ).arg( at ) },
                                      { "cells", QJsonArray{ QStringLiteral( "n%1" ).arg( at ), "" } } } );
        }
        View fixture( QJsonObject{ { "type", "table" }, { "id", "t" }, { "rows", rows },
                                   { "columns", QJsonArray{ QJsonObject{ { "id", "name" } },
                                                            QJsonObject{ { "id", "use" }, { "check", true } } } } },
                      nullptr );
        auto* table = fixture.table();
        table->setFocus();
        table->setCurrentIndex( fixture.index( "r1", 1 ) );
        QTest::keyClick( table, Qt::Key_Space );
        REQUIRE( waitFor( [ & ] { return fixture.countOf( "checked" ) == 1; } ) );
        table->selectionModel()->setCurrentIndex( fixture.index( "r1100", 1 ),
                                                  QItemSelectionModel::NoUpdate );
        QTest::keyClick( table, Qt::Key_Space, Qt::ShiftModifier );
        REQUIRE( waitFor( [ & ] { return fixture.countOf( "checked" ) == 2; } ) );
        const auto value = fixture.lastOf( "checked" ).value( "value" ).toObject();
        const auto ids = value.value( "row_ids" ).toArray();
        REQUIRE( value.value( "row_id" ) == "r1100" );
        REQUIRE( ids.size() == 1000 );
        REQUIRE( ids.first() == "r101" );
        REQUIRE( ids.last() == "r1100" );
        REQUIRE( fixture.index( "r1100", 1 ).data( Qt::CheckStateRole ) == Qt::Checked );
    }
    SECTION( "check cells: no selection range on Shift+click and no activated" )
    {
        View fixture( boardsTable( { { "toggle_selection", true } } ), nullptr );
        auto* table = fixture.table();
        QTest::mouseClick( table->viewport(), Qt::LeftButton, {}, fixture.center( "r1", 0 ) );
        QTest::mouseClick( table->viewport(), Qt::LeftButton, Qt::ShiftModifier,
                           fixture.center( "r3", 2 ) );
        const auto r2 = fixture.index( "r2", 0 );
        REQUIRE_FALSE( table->selectionModel()->isRowSelected( r2.row(), r2.parent() ) );
        QTest::mouseDClick( table->viewport(), Qt::LeftButton, {}, fixture.center( "r2", 2 ) );
        table->setFocus();
        table->setCurrentIndex( fixture.index( "r1", 2 ) );
        QTest::keyClick( table, Qt::Key_Return );
        QTest::qWait( 50 );
        REQUIRE( fixture.countOf( "activated" ) == 0 );
    }
    SECTION( "a right-click reports the clicked row as current; Ctrl+C on nothing keeps the clipboard" )
    {
        View fixture( boardsTable( { { "context_menu", "rows" } } ), nullptr );
        auto* table = fixture.table();
        table->setCurrentIndex( fixture.index( "r1", 0 ) );
        std::unique_ptr<QMenu> menu( fixture.view.buildElementMenu( "t", fixture.center( "r3", 0 ) ) );
        REQUIRE( menu );
        REQUIRE( fixture.lastOf( "selected" ).value( "value" ).toArray() == QJsonArray{ "r3" } );
        REQUIRE( fixture.lastOf( "selected" ).value( "current" ) == "r3" );
        table->clearSelection();
        table->selectionModel()->clearCurrentIndex();
        QApplication::clipboard()->setText( "keep" );
        table->setFocus();
        QTest::keyClick( table, Qt::Key_C, Qt::ControlModifier );
        REQUIRE( QApplication::clipboard()->text() == "keep" );
    }
    SECTION( "never every column hidden" )
    {
        View fixture( QJsonObject{ { "type", "table" }, { "id", "t" }, { "keep_columns", false },
                                   { "columns", QJsonArray{ QJsonObject{ { "id", "a" }, { "hidden", true } },
                                                            QJsonObject{ { "id", "b" }, { "hidden", true } } } },
                                   { "rows", QJsonArray{ QJsonObject{ { "id", "r1" },
                                                                      { "cells", QJsonArray{ "x", "y" } } } } } },
                      nullptr );
        REQUIRE_FALSE( fixture.table()->isColumnHidden( 0 ) );
        REQUIRE( fixture.table()->isColumnHidden( 1 ) );
    }
}

TEST_CASE( "E17d plugin icons are resolved once until the plugin's images change",
           "[ui][workbench][E17]" )
{
    QTemporaryDir temp;
    QImage image( 16, 16, QImage::Format_ARGB32 );
    image.fill( Qt::red );
    const auto path = QDir( temp.path() ).filePath( "row.png" );
    REQUIRE( image.save( path ) );
    auto& registry = PluginIconRegistry::instance();
    const auto plugin = QStringLiteral( "org.test.e17d.icons" );
    REQUIRE_FALSE( registry.icon( plugin, temp.path(), QJsonValue( "row.png" ) ).isNull() );
    // Painting asks again and again: the file is not read again (it may even be gone).
    REQUIRE( QFile::remove( path ) );
    REQUIRE_FALSE( registry.icon( plugin, temp.path(), QJsonValue( "row.png" ) ).isNull() );
    // A stopped plugin may be updated on disk: its icons are resolved anew.
    registry.clear( plugin );
    REQUIRE( registry.icon( plugin, temp.path(), QJsonValue( "row.png" ) ).isNull() );
    // More references than the cache holds: the least recently used go first, so an icon still
    // in use (the shown rows) is never read again (here it could not be: the file is gone).
    REQUIRE( image.save( path ) );
    registry.clear( plugin ); // the missing file's null icon was cached above
    REQUIRE_FALSE( registry.icon( plugin, temp.path(), QJsonValue( "row.png" ) ).isNull() );
    REQUIRE( QFile::remove( path ) );
    bool kept = true;
    for ( int at = 0; at < 600; ++at ) {
        registry.icon( plugin, temp.path(), QJsonValue( QString( "other%1.png" ).arg( at ) ) );
        kept = kept && !registry.icon( plugin, temp.path(), QJsonValue( "row.png" ) ).isNull();
    }
    REQUIRE( kept );
    registry.clear( plugin );
}

namespace {
QJsonObject e17dManifest( const QString& executable, bool withHandles, bool dynamicEntry = false )
{
    QJsonArray items{ QJsonObject{ { "id", "restart" }, { "title", "Restart" }, { "event", true } },
                      QJsonObject{ { "id", "forget" },
                                   { "title", "Forget" },
                                   { "event", true },
                                   { "when", QJsonObject{ { "selection", "any" } } } } };
    if ( dynamicEntry ) {
        // The fixture answers the "cached" entry with cache_ms 60000 and a counted title.
        items.append( QJsonObject{ { "id", "cached" }, { "title", "Cached" }, { "dynamic", true } } );
    }
    QJsonObject json{
        { "id", PluginId },
        { "name", "E17d fixture" },
        { "version", "1.0.0" },
        { "protocol_version", 1 },
        { "executable", executable },
        { "capabilities", QJsonArray{ "tool" } },
        { "actions", QJsonArray{ QJsonObject{ { "id", "summary" }, { "capability", "tool" } } } },
        { "views", QJsonArray{ QJsonObject{ { "id", "tables" }, { "title", "Tables" } } } },
        { "menus",
          QJsonArray{ QJsonObject{
              { "id", "rows_menu" }, { "location", "element" }, { "items", items } } } } };
    if ( withHandles ) {
        json.insert( "handles", dynamicEntry ? QJsonArray{ "ui/command", "menu/populate", "query/cancel" }
                                             : QJsonArray{ "ui/command" } );
    }
    return json;
}

QList<QJsonObject> recorded( const QString& folder, const QString& method )
{
    QList<QJsonObject> result;
    QFile log( QDir( folder ).filePath( "host-messages.jsonl" ) );
    if ( !log.open( QIODevice::ReadOnly ) ) {
        return result;
    }
    for ( const auto& line : log.readAll().split( '\n' ) ) {
        const auto frame = QJsonDocument::fromJson( line ).object();
        if ( frame.value( "method" ) == method ) {
            result.append( frame.value( "params" ).toObject() );
        }
    }
    return result;
}

// A MainWindow with the fixture plugin's `tables` view open.
struct TablesWindow {
    QTemporaryDir temp;
    QString folder;
    std::unique_ptr<PluginManager> manager;
    std::unique_ptr<MainWindow> window;

    explicit TablesWindow( bool withHandles = true, bool dynamicEntry = false )
    {
        PluginMenuRegistry::instance().forgetPlugin( PluginId );
        const auto shipped = QDir( temp.path() ).filePath( "shipped" );
        folder = QDir( shipped ).filePath( PluginId );
        REQUIRE( QDir().mkpath( folder ) );
        const auto executable = QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName();
        QFile file( QDir( folder ).filePath( "plugin.json" ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( QJsonDocument( e17dManifest( executable, withHandles, dynamicEntry ) ).toJson() );
        file.close();
        REQUIRE( QFile::copy( QString::fromUtf8( PLUGIN_FIXTURE_PATH ),
                              QDir( folder ).filePath( executable ) ) );
        manager = std::make_unique<PluginManager>( QDir( temp.path() ).filePath( "data" ), shipped );
        REQUIRE( manager->manage( "enable", PluginId, {} ).ok() );
        auto session = std::make_shared<Session>();
        window = std::make_unique<MainWindow>( WindowSession{ session, "PluginSdkE17d", 0 } );
        window->setPluginManager( *manager, [ this ]( const CommanderRequest& request ) {
            const auto result = manager->manage( request.pluginOperation, request.pluginId,
                                                 request.pluginArguments );
            return result.ok()
                       ? commanderSuccess( {}, result.payload )
                       : commanderFailure( CommanderResultCode::ExecutionFailed, result.error );
        } );
        window->show();
        open();
    }
    ~TablesWindow()
    {
        Q_EMIT window->windowClosed();
        QCoreApplication::processEvents();
    }
    PluginViewHost* host() const
    {
        return window->findChild<PluginViewHost*>();
    }
    PluginView* view() const
    {
        return host() ? host()->view( PluginId, "tables" ) : nullptr;
    }
    QTableView* table() const
    {
        return view() ? qobject_cast<QTableView*>( view()->element( "boards" ) ) : nullptr;
    }
    void open()
    {
        REQUIRE( waitFor( [ & ] { return host() != nullptr; } ) );
        QString error;
        REQUIRE( host()->openView( PluginId, "tables", {}, &error ) );
        REQUIRE( waitFor( [ & ] { return table() && table()->model()->rowCount() == 3; }, 10000 ) );
        REQUIRE( waitFor( [ & ] { return view()->isEnabled(); } ) );
    }
    CommanderResult run( CommanderRequest request )
    {
        return window->executeCommanderRequest( request );
    }
};
} // namespace

TEST_CASE( "E17d element menus run plugin entries with the clicked row and the selection",
           "[ui][workbench][E17]" )
{
    TablesWindow fixture;
    auto* table = fixture.table();
    REQUIRE( fixture.view()->extendedEvents() );
    // The GUI menu: host entries, then the plugin's; picking one sends ui/command.
    const auto row = [ & ]( const QString& id ) {
        for ( int index = 0; index < table->model()->rowCount(); ++index ) {
            if ( table->model()->index( index, 0 ).data( pluginview::RowIdRole ) == id ) {
                return table->visualRect( table->model()->index( index, 0 ) ).center();
            }
        }
        return QPoint{};
    };
    std::unique_ptr<QMenu> menu( fixture.view()->buildElementMenu( "boards", row( "r2" ) ) );
    REQUIRE( menu );
    auto* restart = menu->findChild<QAction*>(
        PluginMenus::entryObjectName( PluginId, "rows_menu", "restart" ) );
    REQUIRE( restart );
    REQUIRE( menu->findChild<QAction*>( PluginMenus::entryObjectName( PluginId, "rows_menu", "forget" ) ) );
    restart->trigger();
    REQUIRE( waitFor( [ & ] { return !recorded( fixture.folder, "ui/command" ).isEmpty(); } ) );
    auto command = recorded( fixture.folder, "ui/command" ).last();
    REQUIRE( command.value( "location" ) == "element" );
    REQUIRE( command.value( "item_id" ) == "restart" );
    auto context = command.value( "context" ).toObject();
    REQUIRE( context.value( "view_id" ) == "tables" );
    REQUIRE( context.value( "element_id" ) == "boards" );
    REQUIRE( context.value( "row_id" ) == "r2" );
    REQUIRE( context.value( "selected_ids" ).toArray() == QJsonArray{ "r2" } );
    REQUIRE( context.value( "selected_count" ) == 1 );
    // Automation: get_plugin_menus and trigger_plugin_menu with --location element.
    CommanderRequest get;
    get.action = CommanderAction::GetPluginMenus;
    get.pluginId = PluginId;
    get.viewId = "tables";
    get.pluginArguments = { { "location", "element" }, { "elementId", "boards" } };
    const auto entries = fixture.run( get );
    REQUIRE( entries.ok() );
    REQUIRE( entries.payload.value( "entries" ).toList().size() == 2 );
    REQUIRE( entries.payload.value( "context" ).toMap().value( "element_id" ) == "boards" );
    CommanderRequest trigger;
    trigger.action = CommanderAction::TriggerPluginMenu;
    trigger.pluginId = PluginId;
    trigger.viewId = "tables";
    trigger.pluginArguments = { { "menuId", "rows_menu" },
                                { "itemId", "restart" },
                                { "elementId", "boards" },
                                { "rowId", "r1" } };
    REQUIRE( fixture.run( trigger ).ok() );
    REQUIRE( waitFor( [ & ] { return recorded( fixture.folder, "ui/command" ).size() == 2; } ) );
    REQUIRE( recorded( fixture.folder, "ui/command" ).last().value( "context" ).toObject().value( "row_id" )
             == "r1" );
    // Without --element-id the entry is refused.
    trigger.pluginArguments.remove( "elementId" );
    REQUIRE_FALSE( fixture.run( trigger ).ok() );
    // A check box and a reorder reach the plugin as events.
    table->selectionModel()->setCurrentIndex( table->model()->index( 0, 2 ),
                                              QItemSelectionModel::NoUpdate );
    table->setFocus();
    QTest::keyClick( table, Qt::Key_Space );
    REQUIRE( waitFor( [ & ] {
        const auto events = recorded( fixture.folder, "view/event" );
        return std::any_of( events.begin(), events.end(), []( const QJsonObject& event ) {
            return event.value( "type" ) == "checked";
        } );
    } ) );
}

TEST_CASE( "E17d kept columns live in the plugin manager across views", "[ui][workbench][E17]" )
{
    TablesWindow fixture;
    auto* table = fixture.table();
    // Entries of tables the plugin no longer has fill the view's state almost to its limit: the
    // one saved longest ago gives way to the table being saved.
    const QString pad( 32700, QLatin1Char( 'x' ) );
    REQUIRE( fixture.manager->setKeptViewState(
        PluginId, "tables",
        QJsonObject{ { "elements",
                       QJsonObject{ { "old1", QJsonObject{ { "saved", 1.0 }, { "pad", pad } } },
                                    { "old2", QJsonObject{ { "saved", 2.0 }, { "pad", pad } } } } } } ) );
    {
        // The menu is a child of the table: released before the view closes and deletes it.
        std::unique_ptr<QMenu> columns(
            table->findChild<PluginTableBehaviour*>()->buildHeaderMenu() );
        columns->findChild<QAction*>( PluginView::objectNameFor( PluginId, "tables", "boards" )
                                      + "_column_done" )
            ->trigger();
    }
    REQUIRE( waitFor( [ & ] {
        return fixture.manager->keptViewState( PluginId, "tables" )
            .value( "elements" )
            .toObject()
            .contains( "boards" );
    } ) );
    const auto elements
        = fixture.manager->keptViewState( PluginId, "tables" ).value( "elements" ).toObject();
    REQUIRE_FALSE( elements.contains( "old1" ) );
    REQUIRE( elements.contains( "old2" ) );
    fixture.host()->closeView( PluginId, "tables" );
    REQUIRE( waitFor( [ & ] { return fixture.view() == nullptr; } ) );
    fixture.open();
    REQUIRE( fixture.table()->isColumnHidden( 1 ) );
}

TEST_CASE( "E17d without handles a plugin view gets no extended events", "[ui][workbench][E17]" )
{
    TablesWindow fixture( false );
    REQUIRE_FALSE( fixture.view()->extendedEvents() );
}

TEST_CASE( "E17d a dynamic element entry's cached answer belongs to its row and selection",
           "[ui][workbench][E17]" )
{
    TablesWindow fixture( true, true );
    auto* table = fixture.table();
    const auto row = [ & ]( const QString& id ) {
        for ( int index = 0; index < table->model()->rowCount(); ++index ) {
            if ( table->model()->index( index, 0 ).data( pluginview::RowIdRole ) == id ) {
                return table->visualRect( table->model()->index( index, 0 ) ).center();
            }
        }
        return QPoint{};
    };
    // The title of the dynamic submenu's first entry after a right-click on a row.
    const auto open = [ & ]( const QString& rowId ) {
        std::unique_ptr<QMenu> menu( fixture.view()->buildElementMenu( "boards", row( rowId ) ) );
        REQUIRE( menu );
        auto* cached = menu->findChild<QMenu*>( "pluginMenu_" + PluginId + "_rows_menu_cached" );
        REQUIRE( cached );
        Q_EMIT cached->aboutToShow();
        QString title;
        REQUIRE( waitFor( [ & ] {
            const auto* first = cached->findChild<QAction*>( "pluginMenuItem_" + PluginId
                                                             + "_rows_menu_cached_r1" );
            title = first ? first->text() : QString{};
            return first != nullptr;
        } ) );
        return title;
    };
    const auto first = open( "r1" );
    REQUIRE( open( "r1" ) == first ); // the same row and selection: the cached answer
    REQUIRE( open( "r2" ) != first ); // another row asks the plugin again

    // Selections are told apart by every id, whatever characters the ids hold, and by their size
    // when the ids they carry are bounded.
    const auto setRows = [ & ]( const QStringList& ids ) {
        QJsonArray rows;
        for ( const auto& id : ids ) {
            rows.append( QJsonObject{ { "id", id }, { "cells", QJsonArray{ "row" } } } );
        }
        fixture.view()->applyPatch( { QJsonObject{ { "id", "boards" }, { "replace_rows", rows } } } );
        REQUIRE( table->model()->rowCount() == ids.size() );
    };
    const auto select = [ & ]( const QStringList& ids ) {
        QItemSelection chosen;
        for ( int index = 0; index < table->model()->rowCount(); ++index ) {
            const auto cellIndex = table->model()->index( index, 0 );
            if ( ids.contains( cellIndex.data( pluginview::RowIdRole ).toString() ) ) {
                chosen.select( cellIndex, cellIndex );
            }
        }
        table->selectionModel()->select( chosen, QItemSelectionModel::ClearAndSelect
                                                     | QItemSelectionModel::Rows );
    };
    const QString unit( QChar( 0x1f ) );
    setRows( { "x", "a" + unit + "b", "c", "a", "b" + unit + "c" } );
    select( { "x", "a" + unit + "b", "c" } );
    const auto joined = open( "x" );
    REQUIRE( open( "x" ) == joined );
    select( { "x", "a", "b" + unit + "c" } ); // the same ids joined by the separator
    REQUIRE( open( "x" ) != joined );
    // Ids of 4,000 characters: only about 65 fit in the menu context, so 66 and 67 selected rows
    // carry the same ids and differ by their count alone.
    QStringList longIds;
    for ( int index = 0; index < 70; ++index ) {
        longIds.append( QString( "%1" ).arg( index, 3, 10, QChar( '0' ) ) + QString( 3997, 'x' ) );
    }
    setRows( longIds );
    select( longIds.mid( 0, 66 ) );
    const auto sixtySix = open( longIds.front() );
    REQUIRE( open( longIds.front() ) == sixtySix );
    select( longIds.mid( 0, 67 ) );
    REQUIRE( open( longIds.front() ) != sixtySix );
}
