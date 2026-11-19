// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// E17c: choices, check boxes, radio buttons, field kinds, completion and validation
// (docs/plugin-sdk-advanced.md section 8).
#include "checkcombobox.h"
#include "pluginchoicewidgets.h"
#include "plugintablemodel.h"
#include "pluginview.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QListView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTableView>
#include <QTest>
#include <catch2/catch.hpp>

namespace {
const QString PluginId = QStringLiteral( "org.test.e17c" );

bool waitFor( const std::function<bool()>& predicate, int timeout = 3000 )
{
    return QTest::qWaitFor( predicate, timeout );
}

QJsonObject field( const QString& id, const QString& kind, const QJsonObject& extra = {} )
{
    QJsonObject element{ { "type", "field" }, { "id", id }, { "kind", kind } };
    for ( auto it = extra.begin(); it != extra.end(); ++it ) {
        element.insert( it.key(), it.value() );
    }
    return element;
}

QJsonObject column( const QJsonArray& children )
{
    return { { "type", "column" }, { "id", "body" }, { "children", children } };
}

QJsonArray choices()
{
    return { QJsonObject{ { "value", "auto" }, { "label", "Automatic" }, { "icon", "builtin:refresh" },
                          { "tooltip", "<b>Detect</b> the speed" } },
             QJsonObject{ { "type", "separator" } },
             QJsonObject{ { "type", "section" }, { "label", "Fixed speeds" } },
             QJsonObject{ { "value", 115200 }, { "label", "115200" } },
             QJsonObject{ { "value", 9600 }, { "label", "9600" }, { "enabled", false } } };
}

struct View {
    PluginView view;
    QSignalSpy events;
    QStringList asked;
    QList<int> timeouts;
    int confirmations = 0;
    QString colorAnswer = QStringLiteral( "#12ab34" );

    explicit View( const QJsonObject& root )
        : view( PluginId, "main", dialogs() )
        , events( &view, &PluginView::eventRaised )
    {
        PluginViewQueries queries;
        queries.icon = []( const QJsonValue& reference ) {
            return reference.toString().startsWith( "builtin:" ) ? QIcon( QPixmap( 16, 16 ) )
                                                                 : QIcon{};
        };
        queries.complete = [ this ]( const QJsonObject& params, int timeoutMs,
                                     std::function<void( const QJsonArray& )> done ) {
            asked.append( params.value( "text" ).toString() );
            timeouts.append( timeoutMs );
            QJsonArray items;
            for ( const auto* name : { "alpha", "alps", "beta" } ) {
                if ( QString::fromLatin1( name ).startsWith( params.value( "text" ).toString() ) ) {
                    items.append( QJsonObject{ { "value", QString::fromLatin1( name ) },
                                               { "label", QString::fromLatin1( name ).toUpper() } } );
                }
            }
            done( items );
            return QStringLiteral( "q:1" );
        };
        view.setQueries( queries );
        view.setTextDebounceMs( 10 );
        view.setRoot( root );
        view.show();
    }
    PluginViewDialogs dialogs()
    {
        PluginViewDialogs result;
        result.color = [ this ]( QWidget*, const QString&, const QString&, const QString& ) {
            return colorAnswer;
        };
        result.confirm = [ this ]( QWidget*, const QString&, const QString&, const QString&,
                                   const QString& ) {
            ++confirmations;
            return true;
        };
        return result;
    }
    QJsonObject last() const
    {
        return events.last().front().toJsonObject();
    }
    template <typename Widget>
    Widget* get( const QString& id ) const
    {
        return qobject_cast<Widget*>( view.element( id ) );
    }
    template <typename Widget>
    Widget* child( const QString& name ) const
    {
        return view.findChild<Widget*>( PluginView::objectNameFor( PluginId, "main", name ) );
    }
};
} // namespace

TEST_CASE( "E17c choices carry icons, tooltips, separators, sections and disabled entries",
           "[ui][workbench][E17]" )
{
    View fixture( column( { field( "speed", "choice", { { "choices", choices() } } ),
                            field( "radio", "choice",
                                   { { "choices", choices() }, { "indicator", "radio" } } ),
                            field( "many", "multichoice", { { "choices", choices() } } ) } ) );
    auto* combo = fixture.get<QComboBox>( "speed" );
    REQUIRE( combo );
    REQUIRE( combo->count() == 5 );
    REQUIRE( combo->currentData().toString() == "auto" ); // the first choice that can be chosen
    REQUIRE_FALSE( combo->itemIcon( 0 ).isNull() );
    REQUIRE( combo->itemData( 0, Qt::ToolTipRole ).toString().contains( "&lt;b&gt;Detect" ) );
    REQUIRE( combo->itemData( 1, Qt::AccessibleDescriptionRole ).toString() == "separator" );
    auto* model = qobject_cast<QStandardItemModel*>( combo->model() );
    REQUIRE( model->item( 2 )->flags() == Qt::NoItemFlags ); // the section title
    REQUIRE_FALSE( model->item( 4 )->isEnabled() );
    REQUIRE( fixture.view.inputs().value( "speed" ) == "auto" );
    // indicator radio draws radio marks in the open list.
    auto* radio = fixture.get<QComboBox>( "radio" );
    REQUIRE( dynamic_cast<pluginchoices::RadioIndicatorDelegate*>( radio->itemDelegate() ) );
    // Multichoice lists skip separators and sections; only real choices count.
    auto* many = fixture.view.element( "many" );
    REQUIRE( many );
    fixture.view.applyPatch(
        { QJsonObject{ { "id", "many" }, { "set", QJsonObject{ { "value", QJsonArray{ "auto", 115200 } } } } } } );
    REQUIRE( fixture.view.inputs().value( "many" ).toArray() == QJsonArray{ "auto", 115200 } );
}

TEST_CASE( "E17c inline radio and check box styles, radio_group, check_group and checkbox",
           "[ui][workbench][E17]" )
{
    View fixture( column(
        { field( "inline", "choice",
                 { { "style", "radio" }, { "choices", QJsonArray{ "a", "b", "c" } },
                   { "orientation", "horizontal" }, { "value", "b" } } ),
          field( "boxes", "multichoice",
                 { { "style", "checkboxes" }, { "searchable", true },
                   { "choices", QJsonArray{ "red", "green", "blue" } } } ),
          QJsonObject{ { "type", "radio_group" }, { "id", "mode" }, { "title", "Mode" },
                       { "choices", QJsonArray{ "fast", "safe" } }, { "value", "safe" },
                       { "columns", 2 } },
          QJsonObject{ { "type", "check_group" }, { "id", "ports" }, { "title", "Ports" },
                       { "choices", QJsonArray{ "COM1", "COM2", "COM3" } },
                       { "value", QJsonArray{ "COM2" } }, { "all_none", true } },
          QJsonObject{ { "type", "checkbox" }, { "id", "agree" }, { "text", "Agree & go" },
                       { "tristate", true }, { "value", QJsonValue::Null } } } ) );
    auto& view = fixture.view;
    auto inputs = view.inputs();
    REQUIRE( inputs.value( "inline" ) == "b" );
    REQUIRE( inputs.value( "boxes" ).toArray().isEmpty() );
    REQUIRE( inputs.value( "mode" ) == "safe" );
    REQUIRE( inputs.value( "ports" ).toArray() == QJsonArray{ "COM2" } );
    REQUIRE( inputs.value( "agree" ).isNull() ); // partly checked
    auto* agree = fixture.get<QCheckBox>( "agree" );
    REQUIRE( agree->text() == "Agree && go" );
    REQUIRE( agree->checkState() == Qt::PartiallyChecked );
    // A click on a radio button reports the chosen value.
    fixture.child<QRadioButton>( "inline_choice_2" )->click();
    REQUIRE( fixture.events.size() == 1 );
    REQUIRE( fixture.last().value( "element_id" ) == "inline" );
    REQUIRE( fixture.last().value( "value" ) == "c" );
    REQUIRE_FALSE( fixture.last().contains( "valid" ) ); // no validation declared
    // All and None.
    fixture.child<QPushButton>( "ports_all" )->click();
    REQUIRE( fixture.last().value( "value" ).toArray() == QJsonArray{ "COM1", "COM2", "COM3" } );
    fixture.child<QPushButton>( "ports_none" )->click();
    REQUIRE( fixture.last().value( "value" ).toArray().isEmpty() );
    // The searchable inline list filters its choices.
    auto* search = fixture.child<QLineEdit>( "boxes_search" );
    REQUIRE( search );
    search->setText( "re" );
    REQUIRE_FALSE( fixture.child<QCheckBox>( "boxes_choice_0" )->isHidden() ); // red
    REQUIRE_FALSE( fixture.child<QCheckBox>( "boxes_choice_1" )->isHidden() ); // green
    REQUIRE( fixture.child<QCheckBox>( "boxes_choice_2" )->isHidden() );       // blue
    agree->setCheckState( Qt::Checked );
    REQUIRE( fixture.last().value( "element_id" ) == "agree" );
    REQUIRE( fixture.last().value( "value" ) == true );
    // Plugin updates never echo back.
    const auto before = fixture.events.size();
    view.applyPatch( { QJsonObject{ { "id", "mode" }, { "set", QJsonObject{ { "value", "fast" } } } } } );
    REQUIRE( fixture.events.size() == before );
    REQUIRE( view.inputs().value( "mode" ) == "fast" );
}

TEST_CASE( "E17c field kinds: multiline, secret, integer, slider, color, date, time, datetime",
           "[ui][workbench][E17]" )
{
    View fixture( column( { field( "notes", "multiline", { { "rows", 3 }, { "monospace", true },
                                                          { "value", "a\nb" } } ),
                            field( "token", "secret", { { "value", "s3" } } ),
                            field( "count", "integer", { { "min", 1 }, { "max", 5 }, { "value", 9 } } ),
                            field( "level", "slider", { { "min", 0 }, { "max", 10 }, { "step", 2 },
                                                        { "value", 4 } } ),
                            field( "tint", "color", { { "value", "#ff0000" } } ),
                            field( "day", "date", { { "value", "2026-11-19" } } ),
                            field( "at", "time", { { "value", "13:45:10" } } ),
                            field( "when", "datetime", { { "value", "2026-11-19T13:45:10" } } ) } ) );
    auto inputs = fixture.view.inputs();
    REQUIRE( inputs.value( "notes" ) == "a\nb" );
    REQUIRE( inputs.value( "token" ) == "s3" );
    REQUIRE( inputs.value( "count" ).toInt() == 5 ); // clamped to max
    REQUIRE( inputs.value( "level" ).toInt() == 4 );
    REQUIRE( inputs.value( "tint" ) == "#ff0000" );
    REQUIRE( inputs.value( "day" ) == "2026-11-19" );
    REQUIRE( inputs.value( "at" ) == "13:45:10" );
    REQUIRE( inputs.value( "when" ) == "2026-11-19T13:45:10" );
    REQUIRE( fixture.get<QLineEdit>( "token" )->echoMode() == QLineEdit::Password );
    REQUIRE( fixture.get<QSpinBox>( "count" )->maximum() == 5 );
    REQUIRE( fixture.get<QSlider>( "level" )->singleStep() == 2 );
    REQUIRE( fixture.child<QLabel>( "level_value" )->text() == "4" );
    REQUIRE( fixture.get<QPlainTextEdit>( "notes" )->font().family()
             == QFontDatabase::systemFont( QFontDatabase::FixedFont ).family() );
    // The color button asks the host dialog and reports the chosen color at once.
    fixture.child<QPushButton>( "tint_pick" )->click();
    REQUIRE( fixture.last().value( "element_id" ) == "tint" );
    REQUIRE( fixture.last().value( "value" ) == "#12ab34" );
    // A changed value is debounced like text.
    fixture.get<QSpinBox>( "count" )->setValue( 2 );
    REQUIRE( waitFor( [ & ] { return fixture.last().value( "element_id" ) == "count"; } ) );
    REQUIRE( fixture.last().value( "value" ).toInt() == 2 );
    fixture.get<QDateTimeEdit>( "day" )->setDate( QDate( 2027, 1, 2 ) );
    REQUIRE( waitFor( [ & ] { return fixture.last().value( "element_id" ) == "day"; } ) );
    REQUIRE( fixture.last().value( "value" ) == "2027-01-02" );
}

TEST_CASE( "E17c validation marks fields, carries valid and holds buttons back",
           "[ui][workbench][E17]" )
{
    View fixture( column(
        { field( "label", "text", { { "required", true }, { "pattern", "[A-Za-z0-9_-]+" },
                                    { "max_length", 8 } } ),
          field( "retries", "integer", { { "min", 0 }, { "max", 10 }, { "value", 3 } } ),
          field( "free", "text" ),
          QJsonObject{ { "type", "button" }, { "id", "apply" }, { "label", "Apply" }, { "event", true },
                       { "requires_valid", QJsonArray{ "label", "retries" } } },
          QJsonObject{ { "type", "button" }, { "id", "run" }, { "label", "Run" },
                       { "action", "summary" } } } ) );
    auto& view = fixture.view;
    QSignalSpy actions( &view, &PluginView::actionRequested );
    auto* error = fixture.child<QLabel>( "label_error" );
    auto* apply = fixture.get<QPushButton>( "apply" );
    // Required and empty: marked, described and the button held back.
    REQUIRE( error );
    REQUIRE_FALSE( error->isHidden() );
    REQUIRE( error->text() == "Required" );
    REQUIRE( fixture.get<QLineEdit>( "label" )->accessibleDescription() == "Required" );
    REQUIRE_FALSE( apply->isEnabled() );
    // An action never starts with invalid fields.
    fixture.get<QPushButton>( "run" )->click();
    REQUIRE( actions.isEmpty() );
    // A value in the pattern clears the mark; changed carries valid.
    fixture.get<QLineEdit>( "label" )->setText( "board_1" );
    REQUIRE( waitFor( [ & ] { return !fixture.events.isEmpty(); } ) );
    REQUIRE( fixture.last().value( "valid" ) == true );
    REQUIRE( error->isHidden() );
    REQUIRE( apply->isEnabled() );
    fixture.get<QPushButton>( "run" )->click();
    REQUIRE( actions.size() == 1 );
    // Outside the pattern, then too long.
    fixture.get<QLineEdit>( "label" )->setText( "bad name" );
    REQUIRE( waitFor( [ & ] { return fixture.last().value( "valid" ) == false; } ) );
    REQUIRE( error->text() == "Not in the expected form" );
    REQUIRE_FALSE( apply->isEnabled() );
    fixture.get<QLineEdit>( "label" )->setText( "abcdefghij" );
    REQUIRE( waitFor( [ & ] { return error->text().startsWith( "At most 8" ); } ) );
    // The plugin's own message wins and an empty string clears it.
    view.applyPatch( { QJsonObject{ { "id", "free" }, { "set", QJsonObject{ { "error", "Taken" } } } } } );
    REQUIRE( fixture.child<QLabel>( "free_error" )->text() == "Taken" );
    view.applyPatch( { QJsonObject{ { "id", "free" }, { "set", QJsonObject{ { "error", "" } } } } } );
    REQUIRE( fixture.child<QLabel>( "free_error" )->isHidden() );
    // A field without validation keys never carries valid.
    fixture.get<QLineEdit>( "free" )->setText( "x" );
    REQUIRE( waitFor( [ & ] { return fixture.last().value( "element_id" ) == "free"; } ) );
    REQUIRE_FALSE( fixture.last().contains( "valid" ) );
}

TEST_CASE( "E17c later roots, dialogs and typed text keep validity and values right",
           "[ui][workbench][E17]" )
{
    const auto button = []( const QString& id, const QJsonObject& extra ) {
        QJsonObject element{ { "type", "button" }, { "id", id }, { "label", id } };
        for ( auto it = extra.begin(); it != extra.end(); ++it ) {
            element.insert( it.key(), it.value() );
        }
        return element;
    };
    const auto root = [ & ]( bool rules ) {
        return column(
            { field( "name", "text", rules ? QJsonObject{ { "required", true } } : QJsonObject{} ),
              field( "board", "choice", { { "editable", true }, { "choices", QJsonArray{ "a", "b" } } } ),
              field( "level", "slider", { { "min", 10 } } ),
              button( "apply", rules ? QJsonObject{ { "event", true }, { "requires_valid", true } }
                                     : QJsonObject{ { "event", true } } ),
              button( "run", { { "action", "summary" },
                               { "confirm", QJsonObject{ { "text", "Sure?" } } } } ) } );
    };
    View fixture( root( true ) );
    auto& view = fixture.view;
    QSignalSpy actions( &view, &PluginView::actionRequested );
    // An action with invalid fields is refused before its confirmation is asked.
    fixture.get<QPushButton>( "run" )->click();
    REQUIRE( fixture.confirmations == 0 );
    REQUIRE( actions.isEmpty() );
    REQUIRE_FALSE( fixture.get<QPushButton>( "apply" )->isEnabled() );
    // A slider with only `min` keeps its own maximum.
    REQUIRE( fixture.get<QSlider>( "level" )->minimum() == 10 );
    REQUIRE( fixture.get<QSlider>( "level" )->maximum() == 100 );
    // Picking in an editable drop-down sends one changed; new choices keep a typed value.
    auto* board = fixture.get<QComboBox>( "board" );
    fixture.events.clear();
    board->setCurrentIndex( 1 );
    REQUIRE( waitFor( [ & ] { return !fixture.events.isEmpty(); } ) );
    QTest::qWait( 50 );
    REQUIRE( fixture.events.size() == 1 );
    REQUIRE( fixture.last().value( "value" ) == "b" );
    board->setEditText( "custom" );
    view.applyPatch( { QJsonObject{ { "id", "board" },
                                    { "set", QJsonObject{ { "choices", QJsonArray{ "c", "d" } } } } } } );
    REQUIRE( board->currentText() == "custom" );
    REQUIRE( view.inputs().value( "board" ) == "custom" );
    // A later root without the rules: no valid in changed, and the button is enabled again.
    view.setRoot( root( false ) );
    REQUIRE( fixture.get<QPushButton>( "apply" )->isEnabled() );
    fixture.events.clear();
    fixture.get<QLineEdit>( "name" )->setText( "x" );
    REQUIRE( waitFor( [ & ] { return !fixture.events.isEmpty(); } ) );
    REQUIRE( fixture.last().value( "element_id" ) == "name" );
    REQUIRE_FALSE( fixture.last().contains( "valid" ) );
}

TEST_CASE( "E17c searchable and editable drop-downs and completion", "[ui][workbench][E17]" )
{
    View fixture( column( { field( "pick", "choice",
                                   { { "searchable", true },
                                     { "choices", QJsonArray{ "alpha", "beta", "gamma" } } } ),
                            field( "board", "choice",
                                   { { "editable", true }, { "completion", true },
                                     { "choices", QJsonArray{ QJsonObject{ { "value", "b1" },
                                                                           { "label", "Board one" } } } } } ),
                            field( "name", "text",
                                   { { "completion", true }, { "completion_timeout_ms", 900 } } ),
                            field( "tags", "multichoice",
                                   { { "searchable", true },
                                     { "value", QJsonArray{ "blue" } },
                                     { "choices",
                                       QJsonArray{ QJsonObject{ { "type", "section" }, { "label", "Colors" } },
                                                   "red", "green", "blue" } } } ) } ) );
    auto& view = fixture.view;
    // Searchable: typing in the open list filters it, the filter shows above the list, and the
    // keyboard selection moves to a shown choice; closing the list clears the filter.
    auto* pick = fixture.get<QComboBox>( "pick" );
    REQUIRE_FALSE( pick->isEditable() );
    auto* filter = pick->findChild<pluginchoices::PopupFilter*>();
    REQUIRE( filter );
    pick->showPopup();
    auto* list = qobject_cast<QListView*>( pick->view() );
    REQUIRE( list );
    QTest::keyClicks( pick->view(), "MM" );
    REQUIRE( filter->text() == "MM" );
    REQUIRE( list->isRowHidden( 0 ) );
    REQUIRE( list->isRowHidden( 1 ) );
    REQUIRE_FALSE( list->isRowHidden( 2 ) );
    REQUIRE( list->currentIndex().row() == 2 );
    auto* shown = pick->view()->parentWidget()->findChild<QLabel*>();
    REQUIRE( shown );
    REQUIRE( shown->text().contains( "MM" ) );
    REQUIRE( shown->isVisibleTo( pick->view()->parentWidget() ) );
    QTest::keyClick( pick->view(), Qt::Key_Backspace );
    REQUIRE( filter->text() == "M" );
    pick->hidePopup();
    REQUIRE( filter->text().isEmpty() );
    REQUIRE_FALSE( list->isRowHidden( 0 ) );
    REQUIRE( view.inputs().value( "pick" ) == "alpha" );
    // The same in a check-dropdown: Space still toggles, sections never get a check box.
    auto* tags = fixture.get<CheckComboBox>( "tags" );
    REQUIRE( tags );
    REQUIRE_FALSE( tags->model()->index( 0, 0 ).data( Qt::CheckStateRole ).isValid() );
    tags->showPopup();
    auto* tagList = qobject_cast<QListView*>( tags->view() );
    QTest::keyClicks( tags->view(), "re" );
    REQUIRE( tagList->isRowHidden( 0 ) );       // the section
    REQUIRE_FALSE( tagList->isRowHidden( 1 ) ); // red
    REQUIRE_FALSE( tagList->isRowHidden( 2 ) ); // green
    REQUIRE( tagList->isRowHidden( 3 ) );       // blue
    QTest::keyClick( tags->view(), Qt::Key_Space );
    REQUIRE( tags->checkedValues() == QJsonArray{ "red", "blue" } );
    tags->hidePopup();
    REQUIRE( view.inputs().value( "tags" ) == QJsonArray{ "red", "blue" } );
    // Editable: a typed value is the value; a choice's label gives its value.
    auto* board = fixture.get<QComboBox>( "board" );
    board->setEditText( "custom" );
    REQUIRE( view.inputs().value( "board" ) == "custom" );
    board->setEditText( "Board one" );
    REQUIRE( view.inputs().value( "board" ) == "b1" );
    // Completion asks the plugin with the typed text and offers its answer.
    auto* name = fixture.get<QLineEdit>( "name" );
    name->setFocus();
    QTest::keyClicks( name, "al" );
    auto* completer = name->findChild<FieldCompleter*>();
    REQUIRE( completer );
    REQUIRE( waitFor( [ & ] { return completer->offered() == QStringList{ "ALPHA", "ALPS" }; } ) );
    REQUIRE( fixture.asked.last() == "al" );
    REQUIRE( fixture.timeouts.last() == 900 );
    REQUIRE( board->lineEdit()->findChild<FieldCompleter*>() );
}

TEST_CASE( "E17c cell editors show choice icons; patches cannot restyle fields",
           "[ui][workbench][E17]" )
{
    const QJsonArray editorChoices{ QJsonObject{ { "value", "on" }, { "label", "On" }, { "icon", "builtin:ok" } },
                                    QJsonObject{ { "type", "separator" } },
                                    QJsonObject{ { "value", "off" }, { "label", "Off" } } };
    View fixture( column(
        { QJsonObject{
              { "type", "table" }, { "id", "rows" },
              { "columns", QJsonArray{ QJsonObject{ { "id", "state" }, { "title", "State" },
                                                    { "editable", true } } } },
              { "rows", QJsonArray{ QJsonObject{
                            { "id", "r1" }, { "cells", QJsonArray{ "On" } },
                            { "editors", QJsonObject{ { "state", QJsonObject{ { "kind", "choice" },
                                                                             { "choices", editorChoices } } } } } } } } },
          field( "pick", "choice", { { "choices", QJsonArray{ "a", "b" } } } ) } ) );
    auto* table = fixture.get<QTableView>( "rows" );
    const auto index = table->model()->index( 0, 0 );
    REQUIRE_FALSE( index.data( Qt::DecorationRole ).value<QIcon>().isNull() );
    // A patch cannot turn a drop-down into radio buttons; a root can.
    fixture.view.applyPatch(
        { QJsonObject{ { "id", "pick" }, { "set", QJsonObject{ { "style", "radio" } } } } } );
    REQUIRE( fixture.get<QComboBox>( "pick" ) );
    fixture.view.setRoot( column(
        { field( "pick", "choice", { { "choices", QJsonArray{ "a", "b" } }, { "style", "radio" } } ) } ) );
    REQUIRE( fixture.get<ChoiceGroup>( "pick" ) );
}
