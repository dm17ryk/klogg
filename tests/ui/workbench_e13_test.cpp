// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// E13: interactive plugin views and message boxes (docs/plugin-views.md).
#include "pluginmanager.h"
#include "pluginview.h"
#include "pluginviewhost.h"
#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QFileInfo>
#include <QGroupBox>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch.hpp>

using cilogg::plugins::PluginManager;

namespace {
const QString PluginId = QStringLiteral( "org.test.e13" );

bool waitFor( const std::function<bool()>& predicate, int timeout = 5000 )
{
    return QTest::qWaitFor( predicate, timeout );
}

QJsonObject label( const QString& id, const QString& text, const QString& tone = "normal" )
{
    return { { "type", "label" }, { "id", id }, { "text", text }, { "tone", tone } };
}

QJsonArray tableRows( int count, int columns = 2 )
{
    QJsonArray rows;
    for ( int index = 0; index < count; ++index ) {
        QJsonArray cells;
        for ( int column = 0; column < columns; ++column ) {
            cells.append( QString( "r%1c%2" ).arg( index ).arg( column ) );
        }
        rows.append( QJsonObject{ { "id", QString( "r%1" ).arg( index ) }, { "cells", cells } } );
    }
    return rows;
}

QJsonArray tableColumns()
{
    return { QJsonObject{ { "id", "a" }, { "title", "Alpha" }, { "width", 80 } },
             QJsonObject{ { "id", "b" }, { "title", "Beta" } } };
}

QJsonObject everyElement()
{
    QJsonArray children{
        label( "title", "<b>plain</b>", "error" ),
        QJsonObject{ { "type", "badge" }, { "id", "state" }, { "text", "OK" }, { "tone", "ok" } },
        QJsonObject{ { "type", "row" },
                     { "id", "fields" },
                     { "children", QJsonArray{ QJsonObject{ { "type", "field" },
                                                            { "id", "name" },
                                                            { "kind", "text" },
                                                            { "label", "Name" },
                                                            { "placeholder", "Type a name" },
                                                            { "tooltip", "The name" } },
                                               QJsonObject{ { "type", "field" },
                                                            { "id", "count" },
                                                            { "kind", "number" },
                                                            { "value", 3 } } } } },
        QJsonObject{ { "type", "group" },
                     { "id", "options" },
                     { "title", "Options" },
                     { "collapsible", true },
                     { "collapsed", true },
                     { "children", QJsonArray{ QJsonObject{ { "type", "field" },
                                                            { "id", "mode" },
                                                            { "kind", "choice" },
                                                            { "choices", QJsonArray{ "a", "b" } },
                                                            { "value", "b" } },
                                               QJsonObject{ { "type", "field" },
                                                            { "id", "flag" },
                                                            { "kind", "boolean" },
                                                            { "label", "Enabled" },
                                                            { "value", true } } } } },
        QJsonObject{ { "type", "field" },
                     { "id", "input" },
                     { "kind", "file_open" },
                     { "filter", "CPF files (*.cpf)" } },
        QJsonObject{ { "type", "field" }, { "id", "output" }, { "kind", "file_save" } },
        QJsonObject{ { "type", "field" }, { "id", "folder" }, { "kind", "directory" } },
        QJsonObject{ { "type", "button" },
                     { "id", "go" },
                     { "label", "Go" },
                     { "tone", "primary" },
                     { "action", "process" } },
        QJsonObject{ { "type", "button" },
                     { "id", "off" },
                     { "label", "Off" },
                     { "enabled", false },
                     { "event", true } },
        QJsonObject{
            { "type", "progress" }, { "id", "bar" }, { "value", 0.5 }, { "text", "Half" } },
        QJsonObject{ { "type", "progress" }, { "id", "spinner" }, { "busy", true } },
        QJsonObject{ { "type", "table" },
                     { "id", "rows" },
                     { "columns", tableColumns() },
                     { "rows", tableRows( 3 ) },
                     { "sortable", true },
                     { "filter_box", true },
                     { "selection", "multi" } },
        label( "hidden", "invisible" ),
    };
    auto hidden = children.last().toObject();
    hidden.insert( "visible", false );
    children.replace( children.size() - 1, hidden );
    return { { "type", "column" }, { "id", "body" }, { "children", children } };
}

struct FakeDialogs {
    bool confirmAnswer = true;
    QString savePath;
    QString openPath = "C:/data/input.cpf";
    QString directoryPath = "C:/data";
    int confirms = 0;
    int saves = 0;
    QString lastFilter;
    QString lastSuggested;
    // Runs inside the "modal" dialog, like events a real nested event loop would deliver.
    std::function<void()> duringDialog;
    void runDuringDialog()
    {
        if ( duringDialog ) {
            duringDialog();
            QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
        }
    }
    PluginViewDialogs make()
    {
        PluginViewDialogs dialogs;
        dialogs.confirm
            = [ this ]( QWidget*, const QString&, const QString&, const QString&, const QString& ) {
                  ++confirms;
                  runDuringDialog();
                  return confirmAnswer;
              };
        dialogs.saveFile = [ this ]( QWidget*, const QString&, const QString&,
                                     const QString& filter, const QString& suggested ) {
            ++saves;
            lastFilter = filter;
            lastSuggested = suggested;
            runDuringDialog();
            return savePath;
        };
        dialogs.openFile
            = [ this ]( QWidget*, const QString&, const QString&, const QString& filter ) {
                  lastFilter = filter;
                  runDuringDialog();
                  return openPath;
              };
        dialogs.directory = [ this ]( QWidget*, const QString&, const QString& ) {
            runDuringDialog();
            return directoryPath;
        };
        return dialogs;
    }
};

QAbstractItemModel* modelOf( PluginView& view, const QString& id )
{
    auto* table = qobject_cast<QTableView*>( view.element( id ) );
    return table ? table->model() : nullptr;
}

QStringList columnText( QAbstractItemModel* model, int column = 0 )
{
    QStringList result;
    for ( int row = 0; model && row < model->rowCount(); ++row ) {
        result.append( model->index( row, column ).data().toString() );
    }
    return result;
}

// A running fixture plugin with views; it records host notifications in host-messages.jsonl.
struct FixturePlugin {
    QTemporaryDir temp;
    QString folder;
    std::unique_ptr<PluginManager> manager;

    FixturePlugin()
    {
        const auto shipped = QDir( temp.path() ).filePath( "shipped" );
        folder = QDir( shipped ).filePath( PluginId );
        REQUIRE( QDir().mkpath( folder ) );
        const auto executable = QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName();
        const QJsonObject manifest{
            { "id", PluginId },
            { "name", "E13 fixture" },
            { "version", "1.0.0" },
            { "protocol_version", 1 },
            { "executable", executable },
            { "capabilities", QJsonArray{ "tool" } },
            { "actions", QJsonArray{ QJsonObject{ { "id", "summary" }, { "capability", "tool" } },
                                     QJsonObject{ { "id", "crash" }, { "capability", "tool" } } } },
            { "views",
              QJsonArray{ QJsonObject{ { "id", "main" }, { "title", "Main" }, { "area", "left" } },
                          QJsonObject{ { "id", "side" }, { "title", "Side" } },
                          QJsonObject{ { "id", "top" }, { "title", "Top" }, { "area", "top" } },
                          QJsonObject{ { "id", "floaty" },
                                       { "title", "Floaty" },
                                       { "floating", true },
                                       { "width", 320 },
                                       { "height", 240 } },
                          QJsonObject{ { "id", "tool" },
                                       { "title", "Tool" },
                                       { "kind", "window" },
                                       { "width", 640 },
                                       { "height", 480 } },
                          QJsonObject{ { "id", "dialog" },
                                       { "title", "Dialog" },
                                       { "kind", "dialog" },
                                       { "modal", false },
                                       { "menu", false } } } }
        };
        QFile file( QDir( folder ).filePath( "plugin.json" ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( QJsonDocument( manifest ).toJson() );
        file.close();
        REQUIRE( QFile::copy( QString::fromUtf8( PLUGIN_FIXTURE_PATH ),
                              QDir( folder ).filePath( executable ) ) );
        manager
            = std::make_unique<PluginManager>( QDir( temp.path() ).filePath( "data" ), shipped );
        REQUIRE( manager->manage( "enable", PluginId, {} ).ok() );
    }
    QList<QJsonObject> messages( const QString& method ) const
    {
        QList<QJsonObject> result;
        QFile log( QDir( folder ).filePath( "host-messages.jsonl" ) );
        if ( log.open( QIODevice::ReadOnly ) ) {
            for ( const auto& line : log.readAll().split( '\n' ) ) {
                const auto frame = QJsonDocument::fromJson( line ).object();
                if ( frame.value( "method" ) == method ) {
                    result.append( frame.value( "params" ).toObject() );
                }
            }
        }
        return result;
    }
    void event( const QString& viewId, const QString& element )
    {
        REQUIRE( manager->sendViewEvent( PluginId, viewId, "w1",
                                         { { "element_id", element },
                                           { "type", "clicked" },
                                           { "value", QJsonValue::Null },
                                           { "inputs", QJsonObject{} } } ) );
    }
};

QString statusText( PluginViewHost& host, const QString& viewId = "main" )
{
    auto* view = host.view( PluginId, viewId );
    auto* status = view ? qobject_cast<QLabel*>( view->element( "status" ) ) : nullptr;
    return status ? status->text() : QString{};
}

struct HostFixture {
    FixturePlugin plugin;
    FakeDialogs dialogs;
    QMainWindow window;
    QList<CommanderRequest> requests;
    std::unique_ptr<PluginViewHost> host;
    HostFixture()
    {
        window.resize( 1000, 700 );
        window.setCentralWidget( new QWidget( &window ) );
        window.show();
        host = std::make_unique<PluginViewHost>(
            &window, "w1", [] { return QVariantMap{ { "windowId", "w1" }, { "windowIndex", 0 } }; },
            plugin.manager.get(),
            [ this ]( const CommanderRequest& request ) {
                requests.append( request );
                return CommanderResult{};
            },
            dialogs.make() );
        host->setRenderIntervalMs( 10 );
    }
    void open( const QString& viewId )
    {
        QString error;
        REQUIRE( host->openView( PluginId, viewId, {}, &error ) );
        INFO( error.toStdString() );
        REQUIRE( waitFor( [ & ] { return statusText( *host, viewId ).startsWith( "ready" ); } ) );
    }
};
} // namespace

TEST_CASE( "E13 renders every element type as plain native widgets", "[ui][workbench][E13]" )
{
    FakeDialogs dialogs;
    PluginView view( PluginId, "main", dialogs.make() );
    view.setRoot( everyElement() );
    view.resize( 600, 900 );
    view.show();
    auto* title = qobject_cast<QLabel*>( view.element( "title" ) );
    REQUIRE( title );
    REQUIRE( title->objectName() == "pluginView_org.test.e13_main_title" );
    REQUIRE( title->textFormat() == Qt::PlainText );
    REQUIRE( title->text() == "<b>plain</b>" );
    REQUIRE( title->property( "tone" ) == "error" );
    auto* badge = qobject_cast<QLabel*>( view.element( "state" ) );
    REQUIRE( badge );
    REQUIRE( badge->property( "tone" ) == "ok" );
    auto* name = qobject_cast<QLineEdit*>( view.element( "name" ) );
    REQUIRE( name );
    REQUIRE( name->placeholderText() == "Type a name" );
    REQUIRE( name->toolTip() == "The name" );
    REQUIRE( name->accessibleName() == "Name" );
    REQUIRE( qobject_cast<QLineEdit*>( view.element( "count" ) )->text() == "3" );
    auto* mode = qobject_cast<QComboBox*>( view.element( "mode" ) );
    REQUIRE( mode );
    REQUIRE( mode->count() == 2 );
    REQUIRE( mode->currentData() == "b" );
    REQUIRE( qobject_cast<QCheckBox*>( view.element( "flag" ) )->isChecked() );
    auto* group = qobject_cast<QGroupBox*>( view.element( "options" ) );
    REQUIRE( group );
    REQUIRE( group->title() == "Options" );
    REQUIRE_FALSE( mode->isVisibleTo( &view ) ); // collapsed
    group->setChecked( true );
    REQUIRE( mode->isVisibleTo( &view ) );
    for ( const auto& id : { "input", "output", "folder" } ) {
        INFO( id );
        REQUIRE( qobject_cast<QLineEdit*>( view.element( id ) ) );
        REQUIRE( view.findChild<QPushButton*>( PluginView::objectNameFor( PluginId, "main", id )
                                               + "_browse" ) );
    }
    view.findChild<QPushButton*>( "pluginView_org.test.e13_main_input_browse" )->click();
    REQUIRE( qobject_cast<QLineEdit*>( view.element( "input" ) )->text() == dialogs.openPath );
    REQUIRE( dialogs.lastFilter == "CPF files (*.cpf)" );
    view.findChild<QPushButton*>( "pluginView_org.test.e13_main_folder_browse" )->click();
    REQUIRE( qobject_cast<QLineEdit*>( view.element( "folder" ) )->text()
             == dialogs.directoryPath );
    auto* go = qobject_cast<QPushButton*>( view.element( "go" ) );
    REQUIRE( go );
    REQUIRE( go->text() == "Go" );
    REQUIRE( go->property( "tone" ) == "primary" );
    REQUIRE_FALSE( view.element( "off" )->isEnabled() );
    auto* bar = qobject_cast<QProgressBar*>( view.element( "bar" ) );
    REQUIRE( bar );
    REQUIRE( bar->value() == bar->maximum() / 2 );
    REQUIRE( bar->format() == "Half" );
    auto* spinner = qobject_cast<QProgressBar*>( view.element( "spinner" ) );
    REQUIRE( spinner->maximum() == 0 );
    auto* table = qobject_cast<QTableView*>( view.element( "rows" ) );
    REQUIRE( table );
    REQUIRE( table->model()->rowCount() == 3 );
    REQUIRE( table->model()->columnCount() == 2 );
    REQUIRE( table->model()->headerData( 1, Qt::Horizontal ).toString() == "Beta" );
    REQUIRE( table->isSortingEnabled() );
    REQUIRE( columnText( table->model() )
             == QStringList{ "r0c0", "r1c0", "r2c0" } ); // plugin order
    table->sortByColumn( 0, Qt::DescendingOrder );
    REQUIRE( columnText( table->model() ) == QStringList{ "r2c0", "r1c0", "r0c0" } );
    table->sortByColumn( -1, Qt::AscendingOrder );
    REQUIRE( table->selectionMode() == QAbstractItemView::ExtendedSelection );
    auto* filter = view.findChild<QLineEdit*>( "pluginView_org.test.e13_main_rows_filter" );
    REQUIRE( filter );
    filter->setText( "r1c" );
    REQUIRE( table->model()->rowCount() == 1 );
    filter->clear();
    REQUIRE_FALSE( view.element( "hidden" )->isVisibleTo( &view ) );
    REQUIRE( view.inputs().value( "mode" ) == "b" );
    REQUIRE( view.inputs().value( "count" ).toDouble() == 3 );
    REQUIRE( view.inputs().value( "flag" ).toBool() );
}

TEST_CASE( "E13 patches keep scroll position, selection and typed field text",
           "[ui][workbench][E13]" )
{
    FakeDialogs dialogs;
    PluginView view( PluginId, "main", dialogs.make() );
    view.setRoot(
        { { "type", "column" },
          { "children",
            QJsonArray{
                label( "status", "start" ),
                QJsonObject{
                    { "type", "field" }, { "id", "name" }, { "kind", "text" }, { "value", "" } },
                QJsonObject{ { "type", "progress" }, { "id", "bar" }, { "value", 0.0 } },
                QJsonObject{ { "type", "table" },
                             { "id", "rows" },
                             { "columns", tableColumns() },
                             { "rows", tableRows( 1000 ) },
                             { "selection", "single" } } } } } );
    view.resize( 500, 400 );
    view.show();
    REQUIRE( QTest::qWaitForWindowExposed( &view ) );
    const auto renders = view.renderCount();
    auto* table = qobject_cast<QTableView*>( view.element( "rows" ) );
    REQUIRE( table );
    table->scrollToBottom();
    const auto scroll = table->verticalScrollBar()->value();
    REQUIRE( scroll > 0 );
    table->selectRow( 990 );
    auto* name = qobject_cast<QLineEdit*>( view.element( "name" ) );
    view.activateWindow();
    name->setFocus();
    QTest::keyClicks( name, "typed" );
    const bool focused = name->hasFocus();
    view.applyPatch(
        { QJsonObject{ { "id", "status" }, { "set", QJsonObject{ { "text", "tick" } } } },
          QJsonObject{ { "id", "bar" }, { "set", QJsonObject{ { "value", 0.25 } } } },
          QJsonObject{
              { "id", "rows" },
              { "update_rows", QJsonArray{ QJsonObject{ { "id", "r10" },
                                                        { "cells", QJsonArray{ "x", "y" } },
                                                        { "tone", "error" } } } } } } );
    REQUIRE( view.renderCount() == renders + 1 );
    REQUIRE( qobject_cast<QLabel*>( view.element( "status" ) )->text() == "tick" );
    REQUIRE( table->verticalScrollBar()->value() == scroll );
    REQUIRE( table->selectionModel()->selectedRows().size() == 1 );
    REQUIRE( table->selectionModel()->selectedRows().front().data().toString() == "r990c0" );
    REQUIRE( name->text() == "typed" );
    REQUIRE( name->hasFocus() == focused );
    REQUIRE( table->model()->index( 10, 1 ).data().toString() == "y" );
    REQUIRE( table->model()->index( 10, 0 ).data( Qt::ForegroundRole ).isValid() );
    // Inserting and removing rows elsewhere keeps the selected row.
    view.applyPatch(
        { QJsonObject{
              { "id", "rows" },
              { "insert_rows",
                QJsonArray{ QJsonObject{ { "id", "new" }, { "cells", QJsonArray{ "n", "m" } } } } },
              { "before", "r0" } },
          QJsonObject{ { "id", "rows" }, { "remove_rows", QJsonArray{ "r5" } } } } );
    REQUIRE( table->model()->rowCount() == 1000 );
    REQUIRE( table->model()->index( 0, 0 ).data().toString() == "n" );
    REQUIRE( table->selectionModel()->selectedRows().front().data().toString() == "r990c0" );
    // A same-shaped root keeps the typed text; an explicit value patch replaces it.
    auto root = QJsonObject{
        { "type", "column" },
        { "children",
          QJsonArray{
              label( "status", "again" ),
              QJsonObject{
                  { "type", "field" }, { "id", "name" }, { "kind", "text" }, { "value", "" } },
              QJsonObject{ { "type", "progress" }, { "id", "bar" }, { "value", 0.5 } },
              QJsonObject{ { "type", "table" },
                           { "id", "rows" },
                           { "columns", tableColumns() },
                           { "selection", "single" } } } }
    };
    view.setRoot( root );
    REQUIRE( name == view.element( "name" ) );
    REQUIRE( name->text() == "typed" );
    REQUIRE( table->model()->rowCount() == 1000 ); // rows omitted: kept
    view.applyPatch(
        { QJsonObject{ { "id", "name" }, { "set", QJsonObject{ { "value", "from plugin" } } } } } );
    REQUIRE( name->text() == "from plugin" );
    // Replacing all rows restores the selection by row id.
    view.applyPatch( { QJsonObject{ { "id", "rows" }, { "replace_rows", tableRows( 995 ) } } } );
    REQUIRE( table->model()->rowCount() == 995 );
    REQUIRE( table->selectionModel()->selectedRows().front().data().toString() == "r990c0" );
}

TEST_CASE( "E13 view events carry the element value and all inputs", "[ui][workbench][E13]" )
{
    FakeDialogs dialogs;
    PluginView view( PluginId, "main", dialogs.make() );
    view.setTextDebounceMs( 20 );
    view.setRoot( everyElement() );
    view.show();
    QSignalSpy events( &view, &PluginView::eventRaised );
    const auto last = [ & ] { return events.last().front().toJsonObject(); };
    auto* name = qobject_cast<QLineEdit*>( view.element( "name" ) );
    name->setText( "a" );
    name->setText( "abc" ); // automation (set_ui) writes the property; typing is debounced
    REQUIRE( waitFor( [ & ] { return events.size() == 1; } ) );
    REQUIRE( last()[ "element_id" ] == "name" );
    REQUIRE( last()[ "type" ] == "changed" );
    REQUIRE( last()[ "value" ] == "abc" );
    REQUIRE( last()[ "inputs" ].toObject()[ "name" ] == "abc" );
    REQUIRE( last()[ "inputs" ].toObject()[ "mode" ] == "b" );
    qobject_cast<QComboBox*>( view.element( "mode" ) )->setCurrentIndex( 0 );
    REQUIRE( events.size() == 2 );
    REQUIRE( last()[ "value" ] == "a" );
    qobject_cast<QCheckBox*>( view.element( "flag" ) )->setChecked( false );
    REQUIRE( events.size() == 3 );
    REQUIRE( last()[ "value" ] == false );
    // Plugin-driven updates never echo back as events.
    view.applyPatch(
        { QJsonObject{ { "id", "flag" }, { "set", QJsonObject{ { "value", true } } } },
          QJsonObject{ { "id", "name" }, { "set", QJsonObject{ { "value", "plugin" } } } } } );
    QTest::qWait( 60 );
    REQUIRE( events.size() == 3 );
    view.applyPatch(
        { QJsonObject{ { "id", "off" }, { "set", QJsonObject{ { "enabled", true } } } } } );
    qobject_cast<QPushButton*>( view.element( "off" ) )->click();
    REQUIRE( events.size() == 4 );
    REQUIRE( last()[ "type" ] == "clicked" );
    REQUIRE( last()[ "element_id" ] == "off" );
    REQUIRE( last()[ "inputs" ].toObject()[ "name" ] == "plugin" );
    auto* table = qobject_cast<QTableView*>( view.element( "rows" ) );
    table->selectRow( 2 );
    REQUIRE( waitFor( [ & ] { return events.size() == 5; } ) );
    REQUIRE( last()[ "type" ] == "selected" );
    REQUIRE( last()[ "value" ].toArray() == QJsonArray{ "r2" } );
}

TEST_CASE( "E13 buttons start actions with inputs after confirm and save dialogs",
           "[ui][workbench][E13]" )
{
    FakeDialogs dialogs;
    PluginView view( PluginId, "main", dialogs.make() );
    view.setRoot(
        { { "type", "column" },
          { "children",
            QJsonArray{
                QJsonObject{ { "type", "field" }, { "id", "name" }, { "value", "n1" } },
                QJsonObject{ { "type", "button" },
                             { "id", "go" },
                             { "action", "process" },
                             { "arguments", QJsonObject{ { "speed", "fast" } } } },
                QJsonObject{ { "type", "button" },
                             { "id", "wipe" },
                             { "tone", "danger" },
                             { "action", "wipe" },
                             { "confirm", QJsonObject{ { "title", "Wipe" },
                                                       { "text", "Sure?" },
                                                       { "ok_label", "Wipe" } } } },
                QJsonObject{ { "type", "button" },
                             { "id", "export" },
                             { "action", "export" },
                             { "save_dialog", QJsonObject{ { "filter", "CSV (*.csv)" },
                                                           { "suggested_name", "rows.csv" } } } },
                QJsonObject{ { "type", "button" },
                             { "id", "ask" },
                             { "event", true },
                             { "confirm", QJsonObject{ { "text", "Send?" } } } } } } } );
    QSignalSpy actions( &view, &PluginView::actionRequested );
    QSignalSpy events( &view, &PluginView::eventRaised );
    const auto click
        = [ & ]( const QString& id ) { qobject_cast<QPushButton*>( view.element( id ) )->click(); };
    click( "go" );
    REQUIRE( actions.size() == 1 );
    REQUIRE( actions[ 0 ][ 0 ] == "process" );
    auto arguments = actions[ 0 ][ 1 ].toMap();
    REQUIRE( arguments[ "speed" ] == "fast" );
    REQUIRE( arguments[ "inputs" ].toMap()[ "name" ] == "n1" );
    REQUIRE_FALSE( arguments.contains( "save_path" ) );
    dialogs.confirmAnswer = false;
    click( "wipe" );
    REQUIRE( dialogs.confirms == 1 );
    REQUIRE( actions.size() == 1 );
    dialogs.confirmAnswer = true;
    click( "wipe" );
    REQUIRE( actions.size() == 2 );
    REQUIRE( actions[ 1 ][ 0 ] == "wipe" );
    dialogs.savePath.clear();
    click( "export" );
    REQUIRE( dialogs.saves == 1 );
    REQUIRE( dialogs.lastFilter == "CSV (*.csv)" );
    REQUIRE( dialogs.lastSuggested == "rows.csv" );
    REQUIRE( actions.size() == 2 ); // cancelled save dialog
    dialogs.savePath = "C:/out/rows.csv";
    click( "export" );
    REQUIRE( actions.size() == 3 );
    REQUIRE( actions[ 2 ][ 1 ].toMap()[ "save_path" ] == "C:/out/rows.csv" );
    dialogs.confirmAnswer = false;
    click( "ask" );
    REQUIRE( events.isEmpty() );
    dialogs.confirmAnswer = true;
    click( "ask" );
    REQUIRE( events.size() == 1 );
    REQUIRE( actions.size() == 3 );
}

TEST_CASE( "E13 ignores unknown elements and enforces element and cell limits",
           "[ui][workbench][E13]" )
{
    FakeDialogs dialogs;
    PluginView view( PluginId, "main", dialogs.make() );
    QJsonArray children;
    for ( int index = 0; index < 600; ++index ) {
        children.append( label( QString( "l%1" ).arg( index ), "x" ) );
    }
    view.setRoot( { { "type", "column" }, { "children", children } } );
    REQUIRE( view.elementCount() == PluginView::MaxElements );
    REQUIRE_FALSE( view.element( "l599" ) );
    view.setRoot(
        { { "type", "column" },
          { "children",
            QJsonArray{
                QJsonObject{ { "type", "chart" }, { "id", "unknown" } },
                QJsonObject{
                    { "type", "label" }, { "id", "known" }, { "text", "k" }, { "color", "red" } },
                QJsonObject{ { "type", "label" }, { "id", "known" }, { "text", "duplicate" } },
                QJsonObject{ { "type", "table" },
                             { "id", "huge" },
                             { "columns", tableColumns() },
                             { "rows", tableRows( 25001 ) } } } } } );
    REQUIRE_FALSE( view.element( "unknown" ) );
    REQUIRE( qobject_cast<QLabel*>( view.element( "known" ) )->text() == "k" );
    REQUIRE( modelOf( view, "huge" ) );
    REQUIRE( modelOf( view, "huge" )->rowCount() == 0 );
    view.applyPatch(
        { QJsonObject{ { "id", "huge" }, { "replace_rows", tableRows( 25000 ) } },
          QJsonObject{ { "id", "missing" }, { "set", QJsonObject{ { "text", "x" } } } } } );
    REQUIRE( modelOf( view, "huge" )->rowCount() == 25000 );
    view.applyPatch( { QJsonObject{
        { "id", "huge" },
        { "insert_rows", QJsonArray{ QJsonObject{ { "id", "over" },
                                                  { "cells", QJsonArray{ "1", "2" } } } } } } } );
    REQUIRE( modelOf( view, "huge" )->rowCount() == 25000 );
}

TEST_CASE( "E13 hosts views as docks on every side, floating docks, windows and dialogs",
           "[ui][workbench][E13]" )
{
    HostFixture fixture;
    auto& host = *fixture.host;
    fixture.open( "main" );
    auto* main = qobject_cast<QDockWidget*>( host.container( PluginId, "main" ) );
    REQUIRE( main );
    REQUIRE( main->objectName() == "pluginView_org.test.e13_main" );
    REQUIRE( main->windowTitle() == "Main" );
    REQUIRE( fixture.window.dockWidgetArea( main ) == Qt::LeftDockWidgetArea );
    fixture.open( "side" );
    REQUIRE( fixture.window.dockWidgetArea(
                 qobject_cast<QDockWidget*>( host.container( PluginId, "side" ) ) )
             == Qt::RightDockWidgetArea );
    fixture.open( "top" );
    REQUIRE( fixture.window.dockWidgetArea(
                 qobject_cast<QDockWidget*>( host.container( PluginId, "top" ) ) )
             == Qt::TopDockWidgetArea );
    fixture.open( "floaty" );
    auto* floaty = qobject_cast<QDockWidget*>( host.container( PluginId, "floaty" ) );
    REQUIRE( floaty->isFloating() );
    REQUIRE( floaty->width() >= 300 );
    fixture.open( "tool" );
    auto* tool = host.container( PluginId, "tool" );
    REQUIRE( tool->isWindow() );
    REQUIRE_FALSE( qobject_cast<QDockWidget*>( tool ) );
    REQUIRE( tool->parentWidget() == &fixture.window );
    REQUIRE( tool->windowTitle() == "Tool" );
    REQUIRE( tool->width() == 640 );
    fixture.open( "dialog" );
    auto* dialog = qobject_cast<QDialog*>( host.container( PluginId, "dialog" ) );
    REQUIRE( dialog );
    REQUIRE_FALSE( dialog->isModal() );
    REQUIRE( host.openViews().size() == 6 );
    // Every kind uses the same elements and events.
    REQUIRE( host.view( PluginId, "tool" )->element( "rows" ) );
    // The user's dock side is remembered per view.
    fixture.window.addDockWidget( Qt::BottomDockWidgetArea, main );
    host.closeView( PluginId, "main" );
    REQUIRE_FALSE( host.container( PluginId, "main" ) );
    REQUIRE( waitFor( [ & ] { return fixture.plugin.messages( "view/closed" ).size() == 1; } ) );
    fixture.open( "main" );
    main = qobject_cast<QDockWidget*>( host.container( PluginId, "main" ) );
    REQUIRE( fixture.window.dockWidgetArea( main ) == Qt::BottomDockWidgetArea );
    REQUIRE( fixture.plugin.messages( "view/opened" ).size() == 7 );
    // Closing a dialog with its frame sends view/closed.
    dialog->reject();
    REQUIRE( waitFor( [ & ] { return fixture.plugin.messages( "view/closed" ).size() == 2; } ) );
    REQUIRE_FALSE( host.container( PluginId, "dialog" ) );
}

TEST_CASE( "E13 button actions run through Commander with the view inputs", "[ui][workbench][E13]" )
{
    HostFixture fixture;
    fixture.open( "main" );
    auto* view = fixture.host->view( PluginId, "main" );
    qobject_cast<QLineEdit*>( view->element( "name" ) )->setText( "typed" );
    qobject_cast<QPushButton*>( view->element( "run" ) )->click();
    REQUIRE( fixture.requests.size() == 1 );
    const auto& request = fixture.requests.front();
    REQUIRE( request.action == CommanderAction::InvokePlugin );
    REQUIRE( request.pluginId == PluginId );
    REQUIRE( request.pluginAction == "summary" );
    REQUIRE( request.windowIndex == 0 );
    REQUIRE( request.pluginArguments[ "inputs" ].toMap()[ "name" ] == "typed" );
    // Event buttons go to the plugin instead, with the same inputs.
    qobject_cast<QPushButton*>( view->element( "ping" ) )->click();
    REQUIRE( waitFor( [ & ] { return statusText( *fixture.host ) == "pong:typed"; } ) );
    const auto events = fixture.plugin.messages( "view/event" );
    REQUIRE( std::any_of( events.begin(), events.end(), []( const QJsonObject& event ) {
        return event[ "element_id" ] == "ping" && event[ "type" ] == "clicked";
    } ) );
}

TEST_CASE( "E13 coalesces view updates to bounded renders", "[ui][workbench][E13]" )
{
    HostFixture fixture;
    fixture.open( "main" );
    fixture.host->setRenderIntervalMs( 100 );
    auto* view = fixture.host->view( PluginId, "main" );
    const auto renders = view->renderCount();
    for ( int index = 0; index < 40; ++index ) {
        Q_EMIT fixture.plugin.manager->viewRequest(
            PluginId, "view/update",
            { { "view_id", "main" },
              { "patch",
                QJsonArray{ QJsonObject{
                    { "id", "status" },
                    { "set", QJsonObject{ { "text", QString::number( index ) } } } } } } } );
    }
    REQUIRE( view->renderCount() == renders );
    REQUIRE( waitFor( [ & ] { return statusText( *fixture.host ) == "39"; } ) );
    REQUIRE( view->renderCount() <= renders + 2 );
}

TEST_CASE( "E13 view lifecycle survives plugin stop, restart and window close",
           "[ui][workbench][E13]" )
{
    HostFixture fixture;
    auto& manager = *fixture.plugin.manager;
    fixture.open( "main" );
    auto* container = fixture.host->container( PluginId, "main" );
    const auto crash = manager.invoke( PluginId, "crash", {}, { { "windowId", "w1" } } );
    REQUIRE( crash.ok() );
    REQUIRE( waitFor( [ & ] { return !fixture.host->view( PluginId, "main" )->isEnabled(); } ) );
    REQUIRE( fixture.host->container( PluginId, "main" ) == container );
    auto* note = container->findChild<QLabel*>( "pluginViewStatus_org.test.e13_main" );
    REQUIRE( note );
    REQUIRE( note->isVisibleTo( container ) );
    REQUIRE( note->text().contains( "stopped", Qt::CaseInsensitive ) );
    // A stopped view never starts jobs, even if an action reaches the host.
    Q_EMIT fixture.host->view( PluginId, "main" )->actionRequested( "summary", {} );
    REQUIRE( fixture.requests.isEmpty() );
    REQUIRE( manager.manage( "restart", PluginId, {} ).ok() );
    REQUIRE( waitFor( [ & ] { return fixture.plugin.messages( "view/opened" ).size() == 2; } ) );
    REQUIRE( waitFor( [ & ] { return fixture.host->view( PluginId, "main" )->isEnabled(); } ) );
    REQUIRE_FALSE( note->isVisibleTo( container ) );
    fixture.host->windowClosing();
    REQUIRE( waitFor( [ & ] { return fixture.plugin.messages( "view/closed" ).size() == 1; } ) );
    REQUIRE( fixture.plugin.messages( "view/closed" ).front()[ "window_id" ] == "w1" );
    // Disabling the plugin closes its views.
    fixture.host = std::make_unique<PluginViewHost>(
        &fixture.window, "w1", [] { return QVariantMap{ { "windowId", "w1" } }; }, &manager,
        []( const CommanderRequest& ) { return CommanderResult{}; }, fixture.dialogs.make() );
    fixture.open( "side" );
    REQUIRE( manager.manage( "disable", PluginId, {} ).ok() );
    REQUIRE( waitFor( [ & ] { return !fixture.host->container( PluginId, "side" ); } ) );
}

TEST_CASE( "E13 plugins show and hide views themselves", "[ui][workbench][E13]" )
{
    HostFixture fixture;
    fixture.open( "main" );
    fixture.plugin.event( "main", "show_dialog" );
    REQUIRE( waitFor( [ & ] { return fixture.host->container( PluginId, "dialog" ); } ) );
    REQUIRE( waitFor( [ & ] { return statusText( *fixture.host, "dialog" ) == "ready dialog"; } ) );
    REQUIRE( fixture.host->container( PluginId, "dialog" )->isVisible() );
    fixture.plugin.event( "main", "show_left" );
    REQUIRE( waitFor( [ & ] { return fixture.host->container( PluginId, "side" ); } ) );
    auto* side = qobject_cast<QDockWidget*>( fixture.host->container( PluginId, "side" ) );
    REQUIRE( fixture.window.dockWidgetArea( side ) == Qt::LeftDockWidgetArea );
    REQUIRE( side->windowTitle() == "Shown side" );
    fixture.plugin.event( "main", "hide_dialog" );
    REQUIRE( waitFor( [ & ] { return !fixture.host->container( PluginId, "dialog" ); } ) );
    REQUIRE( waitFor( [ & ] {
        const auto closed = fixture.plugin.messages( "view/closed" );
        return !closed.isEmpty() && closed.last()[ "view_id" ] == "dialog";
    } ) );
}

TEST_CASE( "E13 message boxes return results, time out, close and respect the limit",
           "[ui][workbench][E13]" )
{
    HostFixture fixture;
    auto& host = *fixture.host;
    host.setTimeScale( 0.01 );
    // This test shows twelve boxes in a few seconds; the rate limit has its own unit test.
    fixture.plugin.manager->setMessageRateLimit( 100, 1000 );
    fixture.open( "main" );

    fixture.plugin.event( "main", "ask" );
    REQUIRE( waitFor( [ & ] { return host.messageBox( PluginId, "ask" ); } ) );
    auto* box = host.messageBox( PluginId, "ask" );
    REQUIRE( box->objectName() == "pluginMessage_org.test.e13_ask" );
    REQUIRE( box->text() == "Proceed?" );
    REQUIRE( box->textFormat() == Qt::PlainText );
    auto* yes = box->findChild<QPushButton*>( "pluginMessage_org.test.e13_ask_yes" );
    REQUIRE( yes );
    REQUIRE( box->defaultButton()
             == box->findChild<QPushButton*>( "pluginMessage_org.test.e13_ask_no" ) );
    yes->click();
    REQUIRE( waitFor( [ & ] { return statusText( host ) == "answer:ask:yes:user"; } ) );
    REQUIRE( waitFor( [ & ] { return !host.messageBox( PluginId, "ask" ); } ) );

    fixture.plugin.event( "main", "ask_timeout" );
    REQUIRE( waitFor( [ & ] { return statusText( host ) == "answer:timed:ok:timeout"; } ) );
    REQUIRE_FALSE( host.messageBox( PluginId, "timed" ) );

    // Any user input stops the countdown.
    host.setTimeScale( 0.2 );
    fixture.plugin.event( "main", "ask_timeout" );
    REQUIRE( waitFor( [ & ] { return host.messageBox( PluginId, "timed" ); } ) );
    box = host.messageBox( PluginId, "timed" );
    auto* ok = box->findChild<QPushButton*>( "pluginMessage_org.test.e13_timed_ok" );
    REQUIRE( ok->text().contains( "(3)" ) );
    QTest::keyClick( box, Qt::Key_Shift );
    QTest::qWait( 900 );
    REQUIRE( host.messageBox( PluginId, "timed" ) == box );
    REQUIRE_FALSE( ok->text().contains( "(" ) );
    box->findChild<QPushButton*>( "pluginMessage_org.test.e13_timed_cancel" )->click();
    REQUIRE( waitFor( [ & ] { return statusText( host ) == "answer:timed:cancel:user"; } ) );

    fixture.plugin.event( "main", "ask_closable" );
    REQUIRE( waitFor( [ & ] { return host.messageBox( PluginId, "closable" ); } ) );
    REQUIRE( host.messageBox( PluginId, "closable" )->windowModality() == Qt::NonModal );
    fixture.plugin.event( "main", "close_message" );
    REQUIRE( waitFor( [ & ] { return statusText( host ) == "answer:closable:closed:user"; } ) );
    REQUIRE_FALSE( host.messageBox( PluginId, "closable" ) );

    fixture.plugin.event( "main", "ask_many" );
    REQUIRE( waitFor( [ & ] { return statusText( host ) == "answer:many-8::user"; } ) );
    for ( int index = 0; index < 8; ++index ) {
        REQUIRE( host.messageBox( PluginId, QString( "many-%1" ).arg( index ) ) );
    }
    REQUIRE_FALSE( host.messageBox( PluginId, "many-8" ) );
    const auto results = fixture.plugin.messages( "ui/message_result" ).size();
    // A stopping plugin's boxes close without results.
    REQUIRE( fixture.plugin.manager->invoke( PluginId, "crash", {}, {} ).ok() );
    REQUIRE( waitFor( [ & ] { return !host.messageBox( PluginId, "many-0" ); } ) );
    QTest::qWait( 50 );
    REQUIRE( fixture.plugin.messages( "ui/message_result" ).size() == results );
}

namespace {
QJsonObject buttonRoot( const QJsonObject& button, const QString& extra = "first" )
{
    return { { "type", "column" },
             { "children",
               QJsonArray{
                   label( "status", extra ),
                   QJsonObject{ { "type", "field" }, { "id", "path" }, { "kind", "file_open" } },
                   button } } };
}
} // namespace

TEST_CASE( "E13 dialogs survive a view that is rebuilt or revoked while they are open",
           "[ui][workbench][E13][review]" )
{
    SECTION( "a confirm dialog during which the tree is rebuilt sends nothing" )
    {
        FakeDialogs dialogs;
        auto view = std::make_unique<PluginView>( PluginId, "main", dialogs.make() );
        view->setRoot(
            buttonRoot( QJsonObject{ { "type", "button" },
                                     { "id", "go" },
                                     { "action", "process" },
                                     { "confirm", QJsonObject{ { "text", "Sure?" } } } } ) );
        QSignalSpy actions( view.get(), &PluginView::actionRequested );
        QSignalSpy events( view.get(), &PluginView::eventRaised );
        dialogs.duringDialog = [ & ] {
            view->setRoot( { { "type", "column" },
                             { "children", QJsonArray{ label( "other", "new shape" ) } } } );
        };
        qobject_cast<QPushButton*>( view->element( "go" ) )->click();
        REQUIRE( dialogs.confirms == 1 );
        REQUIRE( actions.isEmpty() );
        REQUIRE( events.isEmpty() );
        REQUIRE( view->element( "other" ) );
    }
    SECTION( "a file dialog during which the view is deleted touches nothing" )
    {
        FakeDialogs dialogs;
        auto* view = new PluginView( PluginId, "main", dialogs.make() );
        view->setRoot( buttonRoot( QJsonObject{ { "type", "button" }, { "id", "go" } } ) );
        QPointer<PluginView> guard( view );
        bool emitted = false;
        QObject::connect( view, &PluginView::eventRaised, [ & ] { emitted = true; } );
        dialogs.duringDialog = [ & ] { view->deleteLater(); };
        view->findChild<QPushButton*>( "pluginView_org.test.e13_main_path_browse" )->click();
        REQUIRE( guard.isNull() );
        REQUIRE_FALSE( emitted );
    }
    SECTION( "a confirm dialog during which the plugin is disabled starts no job" )
    {
        HostFixture fixture;
        fixture.open( "main" );
        Q_EMIT fixture.plugin.manager->viewRequest(
            PluginId, "view/update",
            { { "view_id", "main" },
              { "patch",
                QJsonArray{ QJsonObject{
                    { "id", "run" },
                    { "set",
                      QJsonObject{ { "confirm", QJsonObject{ { "text", "Sure?" } } } } } } } } } );
        fixture.host->flush();
        fixture.dialogs.duringDialog
            = [ & ] { REQUIRE( fixture.plugin.manager->manage( "disable", PluginId, {} ).ok() ); };
        QPointer<QPushButton> run = qobject_cast<QPushButton*>(
            fixture.host->view( PluginId, "main" )->element( "run" ) );
        run->click();
        REQUIRE( fixture.dialogs.confirms == 1 );
        REQUIRE( fixture.requests.isEmpty() );
        REQUIRE_FALSE( fixture.host->container( PluginId, "main" ) );
    }
}

TEST_CASE( "E13 user input is bounded so it cannot exceed the transport limit",
           "[ui][workbench][E13][review]" )
{
    FakeDialogs dialogs;
    PluginView view( PluginId, "main", dialogs.make() );
    QJsonArray children;
    for ( int index = 0; index < 5; ++index ) {
        children.append( QJsonObject{
            { "type", "field" }, { "id", QString( "f%1" ).arg( index ) }, { "kind", "text" } } );
    }
    children.append( QJsonObject{ { "type", "table" },
                                  { "id", "rows" },
                                  { "columns", tableColumns() },
                                  { "rows", tableRows( 25000 ) },
                                  { "selection", "multi" } } );
    view.setRoot( { { "type", "column" }, { "children", children } } );
    view.setTextDebounceMs( 0 );
    const QString paste( 3 * 1024 * 1024, QLatin1Char( 'x' ) );
    for ( int index = 0; index < 5; ++index ) {
        qobject_cast<QLineEdit*>( view.element( QString( "f%1" ).arg( index ) ) )->setText( paste );
    }
    REQUIRE( qobject_cast<QLineEdit*>( view.element( "f0" ) )->text().size()
             == PluginView::MaxFieldValueChars );
    bool truncated = false;
    const auto inputs = view.inputs( &truncated );
    REQUIRE( truncated );
    REQUIRE( QJsonDocument( inputs ).toJson( QJsonDocument::Compact ).size()
             < PluginView::MaxInputsBytes + 1024 );
    QTest::qWait( 20 ); // let the debounced `changed` events go first
    QSignalSpy events( &view, &PluginView::eventRaised );
    qobject_cast<QTableView*>( view.element( "rows" ) )->selectAll();
    REQUIRE( waitFor( [ & ] { return !events.isEmpty(); } ) );
    QJsonObject selected;
    for ( const auto& arguments : events ) {
        if ( arguments.front().toJsonObject()[ "type" ] == "selected" ) {
            selected = arguments.front().toJsonObject();
        }
    }
    REQUIRE( selected[ "truncated" ] == true ); // also covers the bounded inputs
    REQUIRE( selected[ "type" ] == "selected" );
    REQUIRE( selected[ "value" ].toArray().size() == PluginView::MaxSelectedIds );
    REQUIRE( selected[ "selected_count" ] == 25000 );
    REQUIRE( selected[ "truncated" ] == true );
    REQUIRE( QJsonDocument( selected ).toJson( QJsonDocument::Compact ).size() < 600 * 1024 );
}

TEST_CASE( "E13 a pasted multi-megabyte value reaches the plugin truncated",
           "[ui][workbench][E13][review]" )
{
    HostFixture fixture;
    fixture.open( "main" );
    auto* view = fixture.host->view( PluginId, "main" );
    qobject_cast<QLineEdit*>( view->element( "name" ) )->setText( QString( 3 * 1024 * 1024, 'y' ) );
    qobject_cast<QPushButton*>( view->element( "ping" ) )->click();
    REQUIRE( waitFor( [ & ] { return statusText( *fixture.host ).startsWith( "pong:yyy" ); } ) );
    REQUIRE( statusText( *fixture.host ).size() <= PluginView::MaxTextChars );
    const auto events = fixture.plugin.messages( "view/event" );
    REQUIRE( events.last()[ "inputs" ].toObject()[ "name" ].toString().size()
             == PluginView::MaxFieldValueChars );
    REQUIRE( fixture.plugin.manager->pluginState( PluginId ) == "ready" );
}

TEST_CASE( "E13 same-shaped roots reset dropped properties and update id-less elements",
           "[ui][workbench][E13][review]" )
{
    FakeDialogs dialogs;
    PluginView view( PluginId, "main", dialogs.make() );
    view.setRoot( buttonRoot( QJsonObject{ { "type", "button" },
                                           { "id", "go" },
                                           { "label", "Go" },
                                           { "action", "process" },
                                           { "tooltip", "tip" },
                                           { "enabled", false },
                                           { "confirm", QJsonObject{ { "text", "Sure?" } } } } ) );
    auto* go = qobject_cast<QPushButton*>( view.element( "go" ) );
    REQUIRE_FALSE( go->isEnabled() );
    REQUIRE( go->toolTip() == "tip" );
    // The same shape without action, confirm, enabled and tooltip.
    view.setRoot(
        buttonRoot( QJsonObject{ { "type", "button" }, { "id", "go" }, { "label", "Go" } } ) );
    REQUIRE( go == view.element( "go" ) );
    REQUIRE( go->isEnabled() );
    REQUIRE( go->toolTip().isEmpty() );
    QSignalSpy actions( &view, &PluginView::actionRequested );
    QSignalSpy events( &view, &PluginView::eventRaised );
    go->click();
    REQUIRE( dialogs.confirms == 0 );
    REQUIRE( actions.isEmpty() );
    REQUIRE( events.size() == 1 ); // a button without action sends `clicked`
    // An id-less label is matched by position.
    view.setRoot( { { "type", "column" },
                    { "children", QJsonArray{ QJsonObject{ { "type", "label" }, { "text", "a" } },
                                              QJsonObject{ { "type", "label" },
                                                           { "id", "#0" },
                                                           { "text", "reserved" } } } } } );
    const auto labels = [ & ] {
        QStringList texts;
        for ( auto* item : view.findChildren<QLabel*>() ) {
            texts.append( item->text() );
        }
        return texts;
    };
    REQUIRE( labels().contains( "a" ) );
    REQUIRE( labels().contains( "reserved" ) );
    REQUIRE_FALSE( view.element( "#0" ) ); // reserved ids are treated as id-less
    view.setRoot( { { "type", "column" },
                    { "children", QJsonArray{ QJsonObject{ { "type", "label" }, { "text", "b" } },
                                              QJsonObject{ { "type", "label" },
                                                           { "id", "#0" },
                                                           { "text", "still reserved" } } } } } );
    REQUIRE( labels().contains( "b" ) );
    REQUIRE_FALSE( labels().contains( "a" ) );
    REQUIRE( labels().contains( "still reserved" ) );
}

TEST_CASE( "E13 text, cells and table rows are bounded and de-duplicated",
           "[ui][workbench][E13][review]" )
{
    FakeDialogs dialogs;
    PluginView view( PluginId, "main", dialogs.make() );
    const QString longText( 10000, 'z' );
    view.setRoot(
        { { "type", "column" },
          { "children",
            QJsonArray{
                QJsonObject{ { "type", "label" },
                             { "id", "long" },
                             { "text", longText },
                             { "tooltip", longText } },
                QJsonObject{ { "type", "button" }, { "id", "b" }, { "label", longText } },
                QJsonObject{
                    { "type", "table" },
                    { "id", "rows" },
                    { "columns", tableColumns() },
                    { "selection", "multi" },
                    { "rows",
                      QJsonArray{
                          QJsonObject{ { "id", "dup" },
                                       { "cells", QJsonArray{ longText, "1", "extra" } } },
                          QJsonObject{ { "id", "dup" },
                                       { "cells", QJsonArray{ "second", "2" } } } } } } } } } );
    REQUIRE( qobject_cast<QLabel*>( view.element( "long" ) )->text().size()
             == PluginView::MaxTextChars );
    REQUIRE( view.element( "long" )->toolTip().size() == PluginView::MaxTextChars );
    REQUIRE( qobject_cast<QPushButton*>( view.element( "b" ) )->text().size()
             == PluginView::MaxTextChars );
    auto* model = modelOf( view, "rows" );
    REQUIRE( model->rowCount() == 1 ); // duplicate ids in one replace keep the first row
    REQUIRE( model->index( 0, 0 ).data().toString().size() == PluginView::MaxTextChars );
    view.applyPatch( { QJsonObject{
        { "id", "rows" },
        { "insert_rows",
          QJsonArray{ QJsonObject{ { "id", "n" }, { "cells", QJsonArray{ "a", "b" } } },
                      QJsonObject{ { "id", "n" }, { "cells", QJsonArray{ "c", "d" } } } } } } } );
    REQUIRE( model->rowCount() == 2 ); // duplicates within one insert batch are ignored
    // Large and scattered removals.
    view.applyPatch( { QJsonObject{ { "id", "rows" }, { "replace_rows", tableRows( 25000 ) } } } );
    auto* table = qobject_cast<QTableView*>( view.element( "rows" ) );
    table->selectRow( 24999 );
    QJsonArray removed;
    for ( int index = 0; index < 20000; ++index ) {
        removed.append( QString( "r%1" ).arg( index ) );
    }
    QElapsedTimer elapsed;
    elapsed.start();
    view.applyPatch( { QJsonObject{ { "id", "rows" }, { "remove_rows", removed } } } );
    REQUIRE( elapsed.elapsed() < 5000 );
    REQUIRE( model->rowCount() == 5000 );
    REQUIRE( model->index( 0, 0 ).data().toString() == "r20000c0" );
    REQUIRE( table->selectionModel()->selectedRows().front().data().toString() == "r24999c0" );
    view.applyPatch( { QJsonObject{
        { "id", "rows" }, { "remove_rows", QJsonArray{ "r20001", "r20003", "r20004" } } } } );
    REQUIRE( model->rowCount() == 4997 );
    REQUIRE( columnText( model ).mid( 0, 3 ) == QStringList{ "r20000c0", "r20002c0", "r20005c0" } );
}

TEST_CASE( "E13 oversized patch queues render one view early at most once per interval",
           "[ui][workbench][E13][review]" )
{
    HostFixture fixture;
    fixture.open( "main" );
    fixture.open( "side" );
    fixture.host->setRenderIntervalMs( 10000 );
    auto* main = fixture.host->view( PluginId, "main" );
    auto* side = fixture.host->view( PluginId, "side" );
    const auto mainRenders = main->renderCount();
    const auto sideRenders = side->renderCount();
    QJsonArray patch;
    for ( int index = 0; index < 4097; ++index ) {
        patch.append( QJsonObject{
            { "id", "status" }, { "set", QJsonObject{ { "text", QString::number( index ) } } } } );
    }
    Q_EMIT fixture.plugin.manager->viewRequest(
        PluginId, "view/update",
        { { "view_id", "side" },
          { "patch", QJsonArray{ QJsonObject{
                         { "id", "status" }, { "set", QJsonObject{ { "text", "side" } } } } } } } );
    for ( int burst = 0; burst < 3; ++burst ) {
        Q_EMIT fixture.plugin.manager->viewRequest( PluginId, "view/update",
                                                    { { "view_id", "main" }, { "patch", patch } } );
    }
    REQUIRE( main->renderCount() <= mainRenders + 1 );
    REQUIRE( side->renderCount() == sideRenders ); // other views keep the coalesced schedule
    fixture.host->flush();
    REQUIRE( statusText( *fixture.host ) == "4096" );
    REQUIRE( statusText( *fixture.host, "side" ) == "side" );
}
