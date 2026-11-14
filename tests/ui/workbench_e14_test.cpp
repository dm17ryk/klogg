#include <catch2/catch.hpp>

#include <QAbstractItemView>
#include <QApplication>
#include <QFocusEvent>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTest>

#include <memory>

#include "checkcombobox.h"
#include "pluginview.h"

namespace {
QJsonArray days()
{
    return { QJsonObject{ { "value", "0" }, { "label", "Sun" } },
             QJsonObject{ { "value", "1" }, { "label", "Mon" } },
             QJsonObject{ { "value", "4" }, { "label", "Thu" } } };
}
} // namespace

TEST_CASE( "E14 CheckComboBox keeps a checked set and a summary", "[ui][workbench][E14]" )
{
    CheckComboBox box;
    box.setChoices( days() );
    CHECK( box.summary() == "None" );
    QSignalSpy changed( &box, &CheckComboBox::checkedChanged );
    box.setCheckedValues( { "4", "0", "99" } );                  // 99 is unknown: ignored
    CHECK( box.checkedValues() == QJsonArray{ "0", "4" } );      // choice order
    CHECK( box.summary() == "Sun, Thu" );
    box.setCheckedValues( { "0", "1", "4" } );
    CHECK( box.summary() == "All" );
    auto* model = qobject_cast<QStandardItemModel*>( box.model() );
    REQUIRE( model );
    changed.clear();
    model->item( 1 )->setCheckState( Qt::Unchecked );           // what a click in the popup does
    CHECK( box.checkedValues() == QJsonArray{ "0", "4" } );
    CHECK( changed.count() == 1 );
}

TEST_CASE( "E14 CheckComboBox drops values whose choices disappear", "[ui][workbench][E14]" )
{
    CheckComboBox box;
    box.setChoices( days() );
    box.setCheckedValues( { "0", "4" } );
    box.setChoices( QJsonArray{ QJsonObject{ { "value", "0" }, { "label", "Sun" } } } );
    CHECK( box.checkedValues() == QJsonArray{ "0" } );
}

TEST_CASE( "E14 CheckComboBox caps choices and keeps numeric values", "[ui][workbench][E14]" )
{
    CheckComboBox box;
    QJsonArray many;
    for ( int i = 0; i < 513; ++i ) {
        many.append( i );
    }
    box.setChoices( many );
    CHECK( box.count() == 512 );

    box.setChoices( QJsonArray{ 1, 2 } );
    box.setCheckedValues( { 2 } );
    CHECK( box.checkedValues() == QJsonArray{ 2 } );
    CHECK( box.summary() == "2" );
}

TEST_CASE( "E14 CheckComboBox popup toggles, cancels and closes with one signal", "[ui][workbench][E14]" )
{
    CheckComboBox box;
    box.setChoices( days() );
    box.setCheckedValues( { "0" } );
    box.show();
    auto* model = qobject_cast<QStandardItemModel*>( box.model() );
    REQUIRE( model );
    CHECK( qobject_cast<QStyledItemDelegate*>( box.view()->itemDelegate() ) != nullptr );
    QSignalSpy closed( &box, &CheckComboBox::popupClosed );

    SECTION( "click toggles and keeps the popup open; Esc restores and reports cancel" )
    {
        box.showPopup();
        REQUIRE( box.view()->isVisible() );
        QTest::mouseClick( box.view()->viewport(), Qt::LeftButton, {},
                           box.view()->visualRect( model->index( 1, 0 ) ).center() );
        CHECK( box.checkedValues() == QJsonArray{ "0", "1" } );
        CHECK( box.view()->isVisible() );
        CHECK( closed.count() == 0 );

        QTest::keyClick( box.view(), Qt::Key_Escape );
        CHECK( box.checkedValues() == QJsonArray{ "0" } );
        CHECK( closed.count() == 1 );
        CHECK( closed.at( 0 ).at( 0 ).toBool() == false );
        CHECK( !box.view()->isVisible() );
    }

    SECTION( "Space toggles the current item and Enter closes as accepted" )
    {
        box.showPopup();
        REQUIRE( box.view()->isVisible() );
        box.view()->setCurrentIndex( model->index( 2, 0 ) );
        QTest::keyClick( box.view(), Qt::Key_Space );
        CHECK( box.checkedValues() == QJsonArray{ "0", "4" } );
        CHECK( box.view()->isVisible() );
        CHECK( closed.count() == 0 );
        QTest::keyClick( box.view(), Qt::Key_Return );
        CHECK( closed.count() == 1 );
        CHECK( closed.at( 0 ).at( 0 ).toBool() == true );
        CHECK( box.checkedValues() == QJsonArray{ "0", "4" } );
    }

    SECTION( "hidePopup after a toggle accepts exactly once" )
    {
        box.showPopup();
        REQUIRE( box.view()->isVisible() );
        model->item( 1 )->setCheckState( Qt::Checked );
        box.hidePopup();
        CHECK( closed.count() == 1 );
        CHECK( closed.at( 0 ).at( 0 ).toBool() == true );
        CHECK( box.checkedValues() == QJsonArray{ "0", "1" } );
        box.hidePopup();
        CHECK( closed.count() == 1 );
    }

    SECTION( "hidePopup on a closed combo emits nothing" )
    {
        box.hidePopup();
        CHECK( closed.count() == 0 );
    }

    SECTION( "the container hiding by itself (an outside click) accepts exactly once" )
    {
        box.showPopup();
        REQUIRE( box.view()->isVisible() );
        model->item( 2 )->setCheckState( Qt::Checked );
        box.view()->window()->hide();
        CHECK( closed.count() == 1 );
        CHECK( closed.at( 0 ).at( 0 ).toBool() == true );
        CHECK( box.checkedValues() == QJsonArray{ "0", "4" } );
    }
}

TEST_CASE( "E14 CheckComboBox repopulating while the popup is open updates the view", "[ui][workbench][E14]" )
{
    CheckComboBox box;
    box.setChoices( days() );
    box.show();
    box.showPopup();
    REQUIRE( box.view()->isVisible() );
    box.setChoices( QJsonArray{ QJsonObject{ { "value", "7" }, { "label", "Sat" } } } );
    CHECK( box.view()->model()->rowCount() == 1 );
    CHECK( box.view()->model()->index( 0, 0 ).data().toString() == "Sat" );
    box.hidePopup();
}

TEST_CASE( "E14 multichoice field: inputs and changed events carry a list", "[ui][workbench][E14]" )
{
    PluginView view( "org.test.e14", "main", PluginViewDialogs{} );
    view.setRoot( QJsonObject{ { "type", "column" },
                               { "children", QJsonArray{ QJsonObject{
                                                 { "type", "field" }, { "id", "days" },
                                                 { "kind", "multichoice" }, { "choices", days() },
                                                 { "value", QJsonArray{ "1" } } } } } } );
    auto* box = qobject_cast<CheckComboBox*>( view.element( "days" ) );
    REQUIRE( box );
    CHECK( view.inputs().value( "days" ).toArray() == QJsonArray{ "1" } );
    QSignalSpy events( &view, &PluginView::eventRaised );
    box->setCheckedValues( { "0", "1" } );
    REQUIRE( events.count() == 1 );
    const auto event = events.last().front().toJsonObject();
    CHECK( event.value( "type" ) == "changed" );
    CHECK( event.value( "value" ).toArray() == QJsonArray{ "0", "1" } );
    view.applyPatch( QJsonArray{ QJsonObject{ { "id", "days" },
                                              { "set", QJsonObject{ { "value", QJsonArray{ "4" } } } } } } );
    CHECK( box->checkedValues() == QJsonArray{ "4" } );
    CHECK( events.count() == 1 );                                // plugin patches never echo back

    // A non-array value is an empty list, not a crash.
    view.applyPatch( QJsonArray{ QJsonObject{ { "id", "days" },
                                              { "set", QJsonObject{ { "value", "oops" } } } } } );
    CHECK( box->checkedValues().isEmpty() );
    CHECK( events.count() == 1 );
}

TEST_CASE( "E14 multichoice inputs stay within the documented bound", "[ui][workbench][E14]" )
{
    PluginView view( "org.test.e14", "main", PluginViewDialogs{} );
    QJsonArray choices;
    QJsonArray all;
    for ( int i = 0; i < 512; ++i ) {
        const auto value = QString::number( i ).rightJustified( 1000, QLatin1Char( 'v' ) );
        choices.append( QJsonObject{ { "value", value }, { "label", QString::number( i ) } } );
        all.append( value );
    }
    QJsonArray children;
    for ( int field = 0; field < 3; ++field ) {
        children.append( QJsonObject{ { "type", "field" }, { "id", QString( "m%1" ).arg( field ) },
                                      { "kind", "multichoice" }, { "choices", choices },
                                      { "value", all } } );
    }
    view.setRoot( QJsonObject{ { "type", "column" }, { "children", children } } );
    bool truncated = false;
    const auto inputs = view.inputs( &truncated );
    CHECK( truncated );
    CHECK( QJsonDocument( inputs ).toJson( QJsonDocument::Compact ).size()
           < PluginView::MaxInputsBytes + 1024 );
    for ( const auto& key : inputs.keys() ) {
        CHECK( inputs.value( key ).isArray() );
    }
    CHECK( inputs.value( "m0" ).toArray().size() > 0 );          // the first field keeps its head
    CHECK( inputs.value( "m2" ).toArray().size() < 512 );

    // Small lists are not touched.
    bool small = true;
    PluginView few( "org.test.e14", "main", PluginViewDialogs{} );
    few.setRoot( QJsonObject{ { "type", "column" },
                              { "children", QJsonArray{ QJsonObject{
                                                { "type", "field" }, { "id", "d" },
                                                { "kind", "multichoice" }, { "choices", days() },
                                                { "value", QJsonArray{ "0", "4" } } } } } } );
    CHECK( few.inputs( &small ).value( "d" ).toArray() == QJsonArray{ "0", "4" } );
    CHECK( !small );
}

TEST_CASE( "E14 multichoice patches: choices raise no event and apply before value", "[ui][workbench][E14]" )
{
    PluginView view( "org.test.e14", "main", PluginViewDialogs{} );
    view.setRoot( QJsonObject{ { "type", "column" },
                               { "children", QJsonArray{ QJsonObject{
                                                 { "type", "field" }, { "id", "days" },
                                                 { "kind", "multichoice" }, { "choices", days() },
                                                 { "value", QJsonArray{ "1" } } } } } } );
    auto* box = qobject_cast<CheckComboBox*>( view.element( "days" ) );
    REQUIRE( box );
    QSignalSpy events( &view, &PluginView::eventRaised );
    view.applyPatch( QJsonArray{ QJsonObject{
        { "id", "days" },
        { "set", QJsonObject{ { "choices", QJsonArray{ QJsonObject{ { "value", "1" }, { "label", "Mon" } },
                                                       QJsonObject{ { "value", "5" }, { "label", "Fri" } } } } } } } } );
    CHECK( events.count() == 0 );
    CHECK( box->checkedValues() == QJsonArray{ "1" } );

    view.applyPatch( QJsonArray{ QJsonObject{
        { "id", "days" },
        { "set", QJsonObject{ { "choices", days() }, { "value", QJsonArray{ "0", "4" } } } } } } );
    CHECK( box->checkedValues() == QJsonArray{ "0", "4" } );
    CHECK( events.count() == 0 );
}

namespace {
QJsonObject editableTable()
{
    const QJsonObject choice{ { "kind", "choice" },
                              { "choices", QJsonArray{ QJsonObject{ { "value", "0" }, { "label", "Off (0)" } },
                                                       QJsonObject{ { "value", "1" }, { "label", "On (1)" } } } } };
    const QJsonObject multi{ { "kind", "multichoice" }, { "choices", days() } };
    const QJsonObject number{ { "kind", "number" }, { "min", 1 }, { "max", 180 } };
    return QJsonObject{
        { "type", "table" }, { "id", "fields" }, { "sortable", true }, { "selection", "single" },
        { "columns", QJsonArray{ QJsonObject{ { "id", "name" }, { "title", "Name" } },
                                 QJsonObject{ { "id", "file" }, { "title", "File" }, { "editable", true } } } },
        { "rows", QJsonArray{
                      QJsonObject{ { "id", "r1" }, { "cells", QJsonArray{ "zeta", "1" } },
                                   { "editors", QJsonObject{ { "file", choice } } } },
                      QJsonObject{ { "id", "r2" }, { "cells", QJsonArray{ "alpha", "Sun" } },
                                   { "tooltip", "was Mon" }, { "tone", "accent" },
                                   { "editors", QJsonObject{ { "file", multi } } } },
                      QJsonObject{ { "id", "r3" }, { "cells", QJsonArray{ "beta", "30" } },
                                   { "editors", QJsonObject{ { "file", number } } } },
                      QJsonObject{ { "id", "r4" }, { "cells", QJsonArray{ "gamma", "ro" } } } } } };
}

struct EditableView {
    PluginView view{ "org.test.e14", "main", PluginViewDialogs{} };
    QTableView* table = nullptr;
    EditableView()
    {
        view.setRoot( QJsonObject{ { "type", "column" }, { "children", QJsonArray{ editableTable() } } } );
        table = qobject_cast<QTableView*>( view.element( "fields" ) );
    }
    QModelIndex cell( const QString& rowId, int column ) const
    {
        auto* model = table->model();
        for ( int row = 0; row < model->rowCount(); ++row ) {
            if ( model->index( row, 0 ).data( Qt::UserRole + 1 ).toString() == rowId ) {
                return model->index( row, column );
            }
        }
        return {};
    }
};

// QAbstractItemView::state() is protected; a derived class may still form a pointer to it.
struct ViewState : QTableView {
    static bool editing( const QAbstractItemView* view )
    {
        return ( view->*&ViewState::state )() == EditingState;
    }
};

bool editing( const QTableView* table )
{
    return ViewState::editing( table );
}
} // namespace

TEST_CASE( "E14 only cells with an editor in an editable column are editable", "[ui][workbench][E14]" )
{
    EditableView v;
    REQUIRE( v.table );
    CHECK( v.cell( "r1", 1 ).flags().testFlag( Qt::ItemIsEditable ) );
    CHECK_FALSE( v.cell( "r1", 0 ).flags().testFlag( Qt::ItemIsEditable ) );   // column not editable
    CHECK_FALSE( v.cell( "r4", 1 ).flags().testFlag( Qt::ItemIsEditable ) );   // no editor
    CHECK( v.cell( "r2", 1 ).data( Qt::ToolTipRole ).toString() == "was Mon" );
    CHECK( v.cell( "r2", 0 ).data( Qt::ForegroundRole ).value<QColor>() == QColor( 0x15, 0x65, 0xc0 ) );
}

TEST_CASE( "E14 a committed edit is pending and raises an edited event", "[ui][workbench][E14]" )
{
    EditableView v;
    v.table->sortByColumn( 0, Qt::AscendingOrder );                 // proxy rows differ from source rows
    QSignalSpy events( &v.view, &PluginView::eventRaised );
    REQUIRE( v.table->model()->setData( v.cell( "r1", 1 ), QVariant( QJsonValue( "0" ) ), Qt::EditRole ) );
    REQUIRE( events.count() == 1 );
    const auto event = events.last().front().toJsonObject();
    CHECK( event.value( "type" ) == "edited" );
    CHECK( event.value( "element_id" ) == "fields" );
    CHECK( event.value( "row_id" ) == "r1" );
    CHECK( event.value( "column_id" ) == "file" );
    CHECK( event.value( "value" ) == "0" );
    CHECK( event.value( "previous" ) == "1" );
    CHECK( v.cell( "r1", 1 ).data().toString() == "Off (0)" );     // the chosen item's label (fix round 1)
    CHECK( v.cell( "r1", 1 ).data( Qt::FontRole ).value<QFont>().italic() );
    v.view.applyPatch( QJsonArray{ QJsonObject{ { "id", "fields" },
                                                { "update_rows", QJsonArray{ QJsonObject{
                                                                     { "id", "r1" },
                                                                     { "cells", QJsonArray{ "zeta", "0" } },
                                                                     { "tone", "accent" } } } } } } );
    CHECK_FALSE( v.cell( "r1", 1 ).data( Qt::FontRole ).value<QFont>().italic() );
}

TEST_CASE( "E14 multichoice and number edits", "[ui][workbench][E14]" )
{
    EditableView v;
    QSignalSpy events( &v.view, &PluginView::eventRaised );
    REQUIRE( v.table->model()->setData( v.cell( "r2", 1 ), QVariant( QJsonValue( QJsonArray{ "0", "4" } ) ),
                                        Qt::EditRole ) );
    CHECK( events.last().front().toJsonObject().value( "value" ).toArray() == QJsonArray{ "0", "4" } );
    CHECK( v.cell( "r2", 1 ).data().toString() == "Sun, Thu" );
    REQUIRE( v.table->model()->setData( v.cell( "r3", 1 ), QVariant( QJsonValue( "45" ) ), Qt::EditRole ) );
    CHECK( events.last().front().toJsonObject().value( "value" ) == "45" );
}

TEST_CASE( "E14 revertPendingEdits restores the previous text", "[ui][workbench][E14]" )
{
    EditableView v;
    v.table->model()->setData( v.cell( "r3", 1 ), QVariant( QJsonValue( "45" ) ), Qt::EditRole );
    v.view.revertPendingEdits();
    CHECK( v.cell( "r3", 1 ).data().toString() == "30" );
    CHECK_FALSE( v.cell( "r3", 1 ).data( Qt::FontRole ).value<QFont>().italic() );
}

TEST_CASE( "E14 the delegate builds the right editor for each kind", "[ui][workbench][E14]" )
{
    EditableView v;
    auto* delegate = v.table->itemDelegate();
    QStyleOptionViewItem option;
    std::unique_ptr<QWidget> combo( delegate->createEditor( v.table->viewport(), option, v.cell( "r1", 1 ) ) );
    CHECK( qobject_cast<QComboBox*>( combo.get() ) );
    CHECK_FALSE( qobject_cast<CheckComboBox*>( combo.get() ) );
    std::unique_ptr<QWidget> checks( delegate->createEditor( v.table->viewport(), option, v.cell( "r2", 1 ) ) );
    REQUIRE( qobject_cast<CheckComboBox*>( checks.get() ) );
    delegate->setEditorData( checks.get(), v.cell( "r2", 1 ) );
    CHECK( qobject_cast<CheckComboBox*>( checks.get() )->checkedValues() == QJsonArray{ "0" } );   // "Sun"
    std::unique_ptr<QWidget> line( delegate->createEditor( v.table->viewport(), option, v.cell( "r3", 1 ) ) );
    CHECK( qobject_cast<QLineEdit*>( line.get() ) );
}

TEST_CASE( "E14 replacing rows while an editor is open closes it without an event", "[ui][workbench][E14]" )
{
    EditableView v;
    v.table->show();
    v.table->edit( v.cell( "r3", 1 ) );
    QSignalSpy events( &v.view, &PluginView::eventRaised );
    v.view.applyPatch( QJsonArray{ QJsonObject{ { "id", "fields" },
                                                { "replace_rows", editableTable().value( "rows" ) } } } );
    QApplication::processEvents();
    CHECK( events.count() == 0 );
    CHECK( !editing( v.table ) );
}

// Controller ruling A10 and hardening around the editable-cell contract.

namespace {
bool italic( const QModelIndex& index )
{
    return index.data( Qt::FontRole ).value<QFont>().italic();
}
} // namespace

TEST_CASE( "E14 a multichoice cell editor opens its popup and Esc cancels without an event", "[ui][workbench][E14]" )
{
    EditableView v;
    v.view.show();
    QSignalSpy events( &v.view, &PluginView::eventRaised );
    v.table->edit( v.cell( "r2", 1 ) );
    REQUIRE( editing( v.table ) );
    auto* box = v.table->viewport()->findChild<CheckComboBox*>();
    REQUIRE( box );
    QApplication::processEvents();                                  // the popup opens once the editor is shown
    REQUIRE( box->view()->isVisible() );
    CHECK( box->checkedValues() == QJsonArray{ "0" } );
    QTest::mouseClick( box->view()->viewport(), Qt::LeftButton, {},
                       box->view()->visualRect( box->model()->index( 1, 0 ) ).center() );
    CHECK( box->checkedValues() == QJsonArray{ "0", "1" } );
    QTest::keyClick( box->view(), Qt::Key_Escape );
    QApplication::processEvents();
    CHECK( events.count() == 0 );
    CHECK( v.cell( "r2", 1 ).data().toString() == "Sun" );
    CHECK_FALSE( italic( v.cell( "r2", 1 ) ) );
    CHECK( !editing( v.table ) );   // Esc ended the edit
}

TEST_CASE( "E14 a multichoice popup commit raises one event even when focus loss commits again", "[ui][workbench][E14]" )
{
    EditableView v;
    v.view.show();
    QSignalSpy events( &v.view, &PluginView::eventRaised );
    v.table->edit( v.cell( "r2", 1 ) );
    QPointer<CheckComboBox> box = v.table->viewport()->findChild<CheckComboBox*>();
    REQUIRE( box );
    QApplication::processEvents();
    REQUIRE( box->view()->isVisible() );
    box->view()->setCurrentIndex( box->model()->index( 2, 0 ) );
    QTest::keyClick( box->view(), Qt::Key_Space );                   // ticks Thu
    QTest::keyClick( box->view(), Qt::Key_Return );                  // popupClosed(true): commit and close
    REQUIRE( events.count() == 1 );
    const auto event = events.last().front().toJsonObject();
    CHECK( event.value( "type" ) == "edited" );
    CHECK( event.value( "value" ).toArray() == QJsonArray{ "0", "4" } );
    CHECK( event.value( "previous" ) == "Sun" );
    CHECK( !editing( v.table ) );
    REQUIRE( box );                                                   // deleted later, never inside the Hide handler
    QFocusEvent focusOut( QEvent::FocusOut, Qt::ActiveWindowFocusReason );
    QApplication::sendEvent( box, &focusOut );
    v.table->itemDelegate()->setModelData( box, v.table->model(), v.cell( "r2", 1 ) );   // the same value again
    QApplication::processEvents();
    CHECK( events.count() == 1 );
    CHECK( v.cell( "r2", 1 ).data().toString() == "Sun, Thu" );
    CHECK( italic( v.cell( "r2", 1 ) ) );
}

TEST_CASE( "E14 a choice cell editor opens its list and commits when an item is activated", "[ui][workbench][E14]" )
{
    EditableView v;
    v.view.show();
    QSignalSpy events( &v.view, &PluginView::eventRaised );
    v.table->edit( v.cell( "r1", 1 ) );
    auto* combo = v.table->viewport()->findChild<QComboBox*>();
    REQUIRE( combo );
    REQUIRE_FALSE( qobject_cast<CheckComboBox*>( combo ) );
    CHECK( combo->currentData().toString() == "1" );                  // starts at the cell's value
    QApplication::processEvents();
    REQUIRE( combo->view()->isVisible() );
    combo->view()->setCurrentIndex( combo->model()->index( 0, 0 ) );
    QTest::keyClick( combo->view(), Qt::Key_Return );                 // activates "Off (0)"
    REQUIRE( events.count() == 1 );
    const auto event = events.last().front().toJsonObject();
    CHECK( event.value( "type" ) == "edited" );
    CHECK( event.value( "row_id" ) == "r1" );
    CHECK( event.value( "column_id" ) == "file" );
    CHECK( event.value( "value" ) == "0" );
    CHECK( event.value( "previous" ) == "1" );
    CHECK( !editing( v.table ) );
    QApplication::processEvents();
    CHECK( events.count() == 1 );
    CHECK( v.cell( "r1", 1 ).data().toString() == "Off (0)" );
    CHECK( italic( v.cell( "r1", 1 ) ) );
}

TEST_CASE( "E14 any patch to a pending row ends its pending state", "[ui][workbench][E14]" )
{
    EditableView v;
    REQUIRE( v.table->model()->setData( v.cell( "r1", 1 ), QVariant( QJsonValue( "0" ) ), Qt::EditRole ) );
    REQUIRE( italic( v.cell( "r1", 1 ) ) );
    v.view.applyPatch( QJsonArray{ QJsonObject{
        { "id", "fields" },
        { "update_rows", QJsonArray{ QJsonObject{ { "id", "r1" }, { "tone", "warn" } } } } } } );
    CHECK_FALSE( italic( v.cell( "r1", 1 ) ) );
    CHECK( v.cell( "r1", 1 ).data().toString() == "Off (0)" );        // no cells in the patch: the text stays
    CHECK( v.cell( "r1", 1 ).data( Qt::ForegroundRole ).value<QColor>() == QColor( 0xb2, 0x6a, 0x00 ) );
}

TEST_CASE( "E14 setData takes a QJsonValue or a plain variant", "[ui][workbench][E14]" )
{
    EditableView v;
    QSignalSpy events( &v.view, &PluginView::eventRaised );
    auto* model = v.table->model();
    REQUIRE( model->setData( v.cell( "r3", 1 ), QVariant( QStringLiteral( "60" ) ), Qt::EditRole ) );
    CHECK( events.last().front().toJsonObject().value( "value" ) == "60" );
    REQUIRE( model->setData( v.cell( "r2", 1 ), QVariant( QVariantList{ "1", "4" } ), Qt::EditRole ) );
    CHECK( events.last().front().toJsonObject().value( "value" ).toArray() == QJsonArray{ "1", "4" } );
    CHECK( v.cell( "r2", 1 ).data().toString() == "Mon, Thu" );
    CHECK_FALSE( model->setData( v.cell( "r2", 1 ), QVariant( QStringLiteral( "Mon" ) ), Qt::EditRole ) );
    CHECK_FALSE( model->setData( v.cell( "r1", 0 ), QVariant( QStringLiteral( "x" ) ), Qt::EditRole ) );
    CHECK_FALSE( model->setData( v.cell( "r4", 1 ), QVariant( QStringLiteral( "x" ) ), Qt::EditRole ) );
    CHECK_FALSE( model->setData( v.cell( "r3", 1 ), QVariant( QStringLiteral( "61" ) ), Qt::DisplayRole ) );
    CHECK( events.count() == 2 );
}

TEST_CASE( "E14 a disabled view refuses cell edits", "[ui][workbench][E14]" )
{
    EditableView v;
    QSignalSpy events( &v.view, &PluginView::eventRaised );
    v.view.setEnabled( false );                                       // a stopped plugin's view
    CHECK_FALSE( v.table->model()->setData( v.cell( "r3", 1 ), QVariant( QJsonValue( "45" ) ), Qt::EditRole ) );
    CHECK( v.cell( "r3", 1 ).data().toString() == "30" );
    CHECK_FALSE( italic( v.cell( "r3", 1 ) ) );
    CHECK( events.count() == 0 );
}

TEST_CASE( "E14 an editor value is the starting value; an unchanged value raises no event", "[ui][workbench][E14]" )
{
    EditableView v;
    QSignalSpy events( &v.view, &PluginView::eventRaised );
    // The plugin shows a bitmask and gives the checked values separately.
    const QJsonObject multi{ { "kind", "multichoice" }, { "choices", days() }, { "value", QJsonArray{ "0", "4" } } };
    v.view.applyPatch( QJsonArray{ QJsonObject{
        { "id", "fields" },
        { "update_rows", QJsonArray{ QJsonObject{ { "id", "r2" },
                                                  { "cells", QJsonArray{ "alpha", "17" } },
                                                  { "editors", QJsonObject{ { "file", multi } } } } } } } } );
    auto* delegate = v.table->itemDelegate();
    auto* model = v.table->model();
    QStyleOptionViewItem option;
    std::unique_ptr<QWidget> editor( delegate->createEditor( v.table->viewport(), option, v.cell( "r2", 1 ) ) );
    auto* box = qobject_cast<CheckComboBox*>( editor.get() );
    REQUIRE( box );
    delegate->setEditorData( box, v.cell( "r2", 1 ) );
    CHECK( box->checkedValues() == QJsonArray{ "0", "4" } );
    delegate->setModelData( box, model, v.cell( "r2", 1 ) );         // closed without a change
    CHECK( events.count() == 0 );
    CHECK( v.cell( "r2", 1 ).data().toString() == "17" );
    CHECK_FALSE( italic( v.cell( "r2", 1 ) ) );

    box->setCheckedValues( { "1" } );
    delegate->setModelData( box, model, v.cell( "r2", 1 ) );
    REQUIRE( events.count() == 1 );
    CHECK( v.cell( "r2", 1 ).data().toString() == "Mon" );
    // A pending cell reopens with its pending value, not the plugin's older starting value...
    delegate->setEditorData( box, v.cell( "r2", 1 ) );
    CHECK( box->checkedValues() == QJsonArray{ "1" } );
    // ...and going back to the plugin's value is a real edit while the first one is pending.
    box->setCheckedValues( { "0", "4" } );
    delegate->setModelData( box, model, v.cell( "r2", 1 ) );
    CHECK( events.count() == 2 );
    CHECK( events.last().front().toJsonObject().value( "value" ).toArray() == QJsonArray{ "0", "4" } );
}

TEST_CASE( "E14 a choice editor starts at the cell's value, numeric values included", "[ui][workbench][E14]" )
{
    EditableView v;
    const QJsonObject numbers{ { "kind", "choice" }, { "choices", QJsonArray{ 1, 2, 3 } } };
    v.view.applyPatch( QJsonArray{ QJsonObject{
        { "id", "fields" },
        { "update_rows", QJsonArray{ QJsonObject{ { "id", "r1" },
                                                  { "cells", QJsonArray{ "zeta", "2" } },
                                                  { "editors", QJsonObject{ { "file", numbers } } } } } } } } );
    auto* delegate = v.table->itemDelegate();
    QStyleOptionViewItem option;
    std::unique_ptr<QWidget> editor( delegate->createEditor( v.table->viewport(), option, v.cell( "r1", 1 ) ) );
    auto* combo = qobject_cast<QComboBox*>( editor.get() );
    REQUIRE( combo );
    delegate->setEditorData( combo, v.cell( "r1", 1 ) );
    CHECK( combo->currentIndex() == 1 );
    CHECK( combo->currentText() == "2" );
}

TEST_CASE( "E14 a root that re-sends the original rows replaces an edited cell", "[ui][workbench][E14]" )
{
    EditableView v;
    REQUIRE( v.table->model()->setData( v.cell( "r3", 1 ), QVariant( QJsonValue( "45" ) ), Qt::EditRole ) );
    v.view.setRoot( QJsonObject{ { "type", "column" }, { "children", QJsonArray{ editableTable() } } } );
    CHECK( v.cell( "r3", 1 ).data().toString() == "30" );
    CHECK_FALSE( italic( v.cell( "r3", 1 ) ) );
}

TEST_CASE( "E14 removing the edited row closes its editor without an event", "[ui][workbench][E14]" )
{
    EditableView v;
    v.view.show();
    v.table->edit( v.cell( "r3", 1 ) );
    REQUIRE( editing( v.table ) );
    QSignalSpy events( &v.view, &PluginView::eventRaised );
    v.view.applyPatch( QJsonArray{ QJsonObject{ { "id", "fields" }, { "remove_rows", QJsonArray{ "r3" } } } } );
    QApplication::processEvents();
    CHECK( events.count() == 0 );
    CHECK( !editing( v.table ) );
    v.table->edit( v.cell( "r1", 1 ) );                               // editing still works afterwards
    CHECK( editing( v.table ) );
}

// Task 7 fix round 1 (ruling A12).

namespace {
// One row per editor kind; each cell's text differs from how the editor reads its value.
QJsonObject kindsTable()
{
    const QJsonObject boolean{ { "kind", "boolean" } };
    const QJsonObject choice{ { "kind", "choice" },
                              { "choices", QJsonArray{ QJsonObject{ { "value", "0" }, { "label", "Off (0)" } },
                                                       QJsonObject{ { "value", "1" }, { "label", "On (1)" } } } } };
    const QJsonObject number{ { "kind", "number" }, { "min", 1 }, { "max", 180 }, { "value", 30 } };
    const QJsonObject text{ { "kind", "text" } };
    const QJsonObject multi{ { "kind", "multichoice" }, { "choices", days() }, { "value", QJsonArray{ "0", "4" } } };
    const auto row = []( const char* id, const char* cell, const QJsonObject& editor ) {
        return QJsonObject{ { "id", id },
                            { "cells", QJsonArray{ id, cell } },
                            { "editors", QJsonObject{ { "v", editor } } } };
    };
    return QJsonObject{
        { "type", "table" }, { "id", "kinds" },
        { "columns", QJsonArray{ QJsonObject{ { "id", "k" } }, QJsonObject{ { "id", "v" }, { "editable", true } } } },
        { "rows", QJsonArray{ row( "b1", "1", boolean ), row( "b2", "Yes", boolean ), row( "b3", "on", boolean ),
                              row( "c1", "On (1)", choice ), row( "n1", "30 s", number ), row( "t1", "hello", text ),
                              row( "m1", "17", multi ),
                              // Text that matches no item (fix round 2): an out-of-range value, an empty boolean.
                              row( "c2", "7", choice ), row( "b4", "", boolean ) } } };
}

QJsonObject multiEditor( const QJsonArray& value )
{
    return QJsonObject{ { "kind", "multichoice" }, { "choices", days() }, { "value", value } };
}

struct KindsView {
    PluginView view{ "org.test.e14", "kinds", PluginViewDialogs{} };
    QTableView* table = nullptr;
    QSignalSpy events{ &view, &PluginView::eventRaised };
    KindsView()
    {
        view.setRoot( QJsonObject{ { "type", "column" }, { "children", QJsonArray{ kindsTable() } } } );
        table = qobject_cast<QTableView*>( view.element( "kinds" ) );
    }
    QModelIndex cell( const QString& rowId ) const
    {
        auto* model = table->model();
        for ( int row = 0; row < model->rowCount(); ++row ) {
            if ( model->index( row, 0 ).data( Qt::UserRole + 1 ).toString() == rowId ) {
                return model->index( row, 1 );
            }
        }
        return {};
    }
    // An editor built and filled by the delegate, as the view does when an edit starts.
    std::unique_ptr<QWidget> open( const QString& rowId ) const
    {
        QStyleOptionViewItem option;
        std::unique_ptr<QWidget> editor(
            table->itemDelegate()->createEditor( table->viewport(), option, cell( rowId ) ) );
        table->itemDelegate()->setEditorData( editor.get(), cell( rowId ) );
        return editor;
    }
    void commit( QWidget* editor, const QString& rowId ) const
    {
        table->itemDelegate()->setModelData( editor, table->model(), cell( rowId ) );
    }
    void patch( const QJsonObject& row )
    {
        view.applyPatch( QJsonArray{ QJsonObject{ { "id", "kinds" }, { "update_rows", QJsonArray{ row } } } } );
    }
    QJsonValue lastValue() const
    {
        return events.last().front().toJsonObject().value( "value" );
    }
};
} // namespace

TEST_CASE( "E14 closing a cell editor without a change raises no event", "[ui][workbench][E14]" )
{
    KindsView v;
    REQUIRE( v.table );
    for ( const auto* id : { "b1", "b2", "b3", "c1", "n1", "t1", "m1", "c2", "b4" } ) {
        INFO( "row " << id );
        const auto before = v.cell( id ).data().toString();
        auto editor = v.open( id );
        REQUIRE( editor );
        v.commit( editor.get(), id );
        CHECK( v.events.count() == 0 );
        CHECK( v.cell( id ).data().toString() == before );
        CHECK_FALSE( italic( v.cell( id ) ) );
    }
    // What the editors started with.
    auto b1 = v.open( "b1" );
    CHECK( qobject_cast<QComboBox*>( b1.get() )->currentData() == QVariant( true ) );
    auto b3 = v.open( "b3" );
    CHECK( qobject_cast<QComboBox*>( b3.get() )->currentData() == QVariant( true ) );   // "on"
    auto c1 = v.open( "c1" );
    CHECK( qobject_cast<QComboBox*>( c1.get() )->currentData() == QVariant( "1" ) );   // found by its label
    auto n1 = v.open( "n1" );
    CHECK( qobject_cast<QLineEdit*>( n1.get() )->text() == "30" );                      // the editor value
    auto m1 = v.open( "m1" );
    CHECK( qobject_cast<CheckComboBox*>( m1.get() )->checkedValues() == QJsonArray{ "0", "4" } );
}

TEST_CASE( "E14 a boolean cell edit commits a bool and shows the item's label", "[ui][workbench][E14]" )
{
    KindsView v;
    auto editor = v.open( "b1" );
    auto* combo = qobject_cast<QComboBox*>( editor.get() );
    REQUIRE( combo );
    combo->setCurrentIndex( combo->findData( false ) );
    v.commit( combo, "b1" );
    REQUIRE( v.events.count() == 1 );
    CHECK( v.lastValue().isBool() );
    CHECK( v.lastValue() == QJsonValue( false ) );
    CHECK( v.cell( "b1" ).data().toString() == "No" );
    CHECK( italic( v.cell( "b1" ) ) );
}

TEST_CASE( "E14 a confirmed edit becomes the editor's starting value", "[ui][workbench][E14]" )
{
    KindsView v;
    {
        auto editor = v.open( "m1" );
        auto* box = qobject_cast<CheckComboBox*>( editor.get() );
        REQUIRE( box );
        box->setCheckedValues( { "1" } );
        v.commit( box, "m1" );
    }
    REQUIRE( v.events.count() == 1 );
    REQUIRE( v.cell( "m1" ).data().toString() == "Mon" );

    SECTION( "a cells-only confirmation showing the edit keeps it as the starting value" )
    {
        v.patch( QJsonObject{ { "id", "m1" }, { "cells", QJsonArray{ "m1", "Mon" } } } );
        CHECK_FALSE( italic( v.cell( "m1" ) ) );
        auto editor = v.open( "m1" );
        auto* box = qobject_cast<CheckComboBox*>( editor.get() );
        CHECK( box->checkedValues() == QJsonArray{ "1" } );
        // A real change after the confirmation is never swallowed, even back to the old value.
        box->setCheckedValues( { "0", "4" } );
        v.commit( box, "m1" );
        REQUIRE( v.events.count() == 2 );
        CHECK( v.lastValue().toArray() == QJsonArray{ "0", "4" } );
    }
    SECTION( "the plugin's editors win" )
    {
        v.patch( QJsonObject{ { "id", "m1" },
                              { "cells", QJsonArray{ "m1", "Mon" } },
                              { "editors", QJsonObject{ { "v", multiEditor( { "4" } ) } } } } );
        auto editor = v.open( "m1" );
        CHECK( qobject_cast<CheckComboBox*>( editor.get() )->checkedValues() == QJsonArray{ "4" } );
    }
    SECTION( "a confirmation showing other text leaves the starting value to the plugin" )
    {
        v.patch( QJsonObject{ { "id", "m1" }, { "cells", QJsonArray{ "m1", "2" } } } );
        auto editor = v.open( "m1" );
        CHECK( qobject_cast<CheckComboBox*>( editor.get() )->checkedValues() == QJsonArray{ "0", "4" } );
    }
}

TEST_CASE( "E14 Esc in a choice cell's list ends the edit without an event", "[ui][workbench][E14]" )
{
    EditableView v;
    v.view.show();
    QSignalSpy events( &v.view, &PluginView::eventRaised );
    v.table->edit( v.cell( "r1", 1 ) );
    auto* combo = v.table->viewport()->findChild<QComboBox*>();
    REQUIRE( combo );
    QApplication::processEvents();
    REQUIRE( combo->view()->isVisible() );
    combo->view()->setCurrentIndex( combo->model()->index( 0, 0 ) );   // moved to, not chosen
    QTest::keyClick( combo->view(), Qt::Key_Escape );
    QApplication::processEvents();
    CHECK( !editing( v.table ) );
    CHECK( events.count() == 0 );
    CHECK( v.cell( "r1", 1 ).data().toString() == "1" );
    CHECK_FALSE( italic( v.cell( "r1", 1 ) ) );
}

TEST_CASE( "E14 a text cell commits through the view on Enter", "[ui][workbench][E14]" )
{
    KindsView v;
    v.view.show();
    v.table->edit( v.cell( "t1" ) );
    auto* line = v.table->viewport()->findChild<QLineEdit*>();
    REQUIRE( line );
    CHECK( line->text() == "hello" );
    line->selectAll();
    QTest::keyClicks( line, "world" );
    QTest::keyClick( line, Qt::Key_Return );
    QApplication::processEvents();                                    // Enter commits on the next turn
    REQUIRE( v.events.count() == 1 );
    const auto event = v.events.last().front().toJsonObject();
    CHECK( event.value( "value" ) == "world" );
    CHECK( event.value( "previous" ) == "hello" );
    CHECK( !editing( v.table ) );
    CHECK( v.cell( "t1" ).data().toString() == "world" );
}

TEST_CASE( "E14 a number cell refuses values outside its range and other notations", "[ui][workbench][E14]" )
{
    KindsView v;
    auto editor = v.open( "n1" );
    auto* line = qobject_cast<QLineEdit*>( editor.get() );
    REQUIRE( line );
    for ( const auto* text : { "500", "0", "1e2", "1,000" } ) {
        INFO( "text " << text );
        line->setText( text );
        CHECK_FALSE( line->hasAcceptableInput() );
        v.commit( line, "n1" );
    }
    CHECK( v.events.count() == 0 );
    line->setText( "45" );
    CHECK( line->hasAcceptableInput() );
    v.commit( line, "n1" );
    REQUIRE( v.events.count() == 1 );
    CHECK( v.lastValue() == "45" );
}

// Task 7 fix round 2 (ruling A13).

TEST_CASE( "E14 a cell list with no matching item commits any pick", "[ui][workbench][E14]" )
{
    KindsView v;
    SECTION( "an out-of-range choice: picking the first item from the list commits it" )
    {
        v.view.show();
        v.table->edit( v.cell( "c2" ) );
        auto* combo = v.table->viewport()->findChild<QComboBox*>();
        REQUIRE( combo );
        CHECK( combo->currentIndex() == -1 );                         // "7" is no item
        QApplication::processEvents();
        REQUIRE( combo->view()->isVisible() );
        combo->view()->setCurrentIndex( combo->model()->index( 0, 0 ) );
        QTest::keyClick( combo->view(), Qt::Key_Return );             // picks "Off (0)"
        QApplication::processEvents();
        REQUIRE( v.events.count() == 1 );
        CHECK( v.lastValue() == "0" );
        CHECK( v.events.last().front().toJsonObject().value( "previous" ) == "7" );
        CHECK( v.cell( "c2" ).data().toString() == "Off (0)" );
        CHECK( !editing( v.table ) );
    }
    SECTION( "an empty boolean: picking No commits false" )
    {
        auto editor = v.open( "b4" );
        auto* combo = qobject_cast<QComboBox*>( editor.get() );
        REQUIRE( combo );
        CHECK( combo->currentIndex() == -1 );                         // "" is neither true nor false
        combo->setCurrentIndex( combo->findData( false ) );
        v.commit( combo, "b4" );
        REQUIRE( v.events.count() == 1 );
        CHECK( v.lastValue().isBool() );
        CHECK( v.lastValue() == QJsonValue( false ) );
        CHECK( v.cell( "b4" ).data().toString() == "No" );
    }
}

TEST_CASE( "E14 replacing rows while a choice list is open closes the editor without an event", "[ui][workbench][E14]" )
{
    EditableView v;
    v.view.show();
    QSignalSpy events( &v.view, &PluginView::eventRaised );
    v.table->edit( v.cell( "r1", 1 ) );
    auto* combo = v.table->viewport()->findChild<QComboBox*>();
    REQUIRE( combo );
    QApplication::processEvents();
    REQUIRE( combo->view()->isVisible() );
    v.view.applyPatch( QJsonArray{ QJsonObject{ { "id", "fields" },
                                                { "replace_rows", editableTable().value( "rows" ) } } } );
    QApplication::processEvents();
    CHECK( events.count() == 0 );
    CHECK( !editing( v.table ) );
    CHECK( v.cell( "r1", 1 ).data().toString() == "1" );
}

// Final review fixes (ruling A14).

TEST_CASE( "E14 CheckComboBox emits checkedChanged only when the checked set changes", "[ui][workbench][E14]" )
{
    CheckComboBox box;
    box.setChoices( days() );
    QSignalSpy changed( &box, &CheckComboBox::checkedChanged );
    box.setCheckedValues( { "0", "4" } );
    CHECK( changed.count() == 1 );
    box.setCheckedValues( { "4", "0" } );                        // the same set in another order
    CHECK( changed.count() == 1 );
    box.setCheckedValues( { "0", "4", "99" } );                  // an unknown value changes nothing
    CHECK( changed.count() == 1 );
    box.setCheckedValues( {} );
    CHECK( changed.count() == 2 );
    box.setCheckedValues( {} );
    CHECK( changed.count() == 2 );
}

TEST_CASE( "E14 a multichoice field closed with Esc and no toggle sends no changed event", "[ui][workbench][E14]" )
{
    PluginView view( "org.test.e14", "main", PluginViewDialogs{} );
    view.setRoot( QJsonObject{ { "type", "column" },
                               { "children", QJsonArray{ QJsonObject{
                                                 { "type", "field" }, { "id", "days" },
                                                 { "kind", "multichoice" }, { "choices", days() },
                                                 { "value", QJsonArray{ "1" } } } } } } );
    view.show();
    auto* box = qobject_cast<CheckComboBox*>( view.element( "days" ) );
    REQUIRE( box );
    QSignalSpy events( &view, &PluginView::eventRaised );
    box->showPopup();
    REQUIRE( box->view()->isVisible() );
    QTest::keyClick( box->view(), Qt::Key_Escape );
    CHECK( !box->view()->isVisible() );
    CHECK( events.count() == 0 );
    CHECK( box->checkedValues() == QJsonArray{ "1" } );

    // Each toggle while the list is open sends `changed`; Esc then sends the restored set.
    box->showPopup();
    REQUIRE( box->view()->isVisible() );
    box->view()->setCurrentIndex( box->model()->index( 2, 0 ) );
    QTest::keyClick( box->view(), Qt::Key_Space );
    REQUIRE( events.count() == 1 );
    CHECK( events.last().front().toJsonObject().value( "value" ).toArray() == QJsonArray{ "1", "4" } );
    QTest::keyClick( box->view(), Qt::Key_Escape );
    REQUIRE( events.count() == 2 );
    CHECK( events.last().front().toJsonObject().value( "value" ).toArray() == QJsonArray{ "1" } );
}

TEST_CASE( "E14 CheckComboBox caps labels and values like a choice field", "[ui][workbench][E14]" )
{
    const QString longLabel( PluginView::MaxTextChars + 10, QLatin1Char( 'L' ) );
    const QString longValue( PluginView::MaxFieldValueChars + 10, QLatin1Char( 'v' ) );
    const QString unlabelled( PluginView::MaxTextChars + 10, QLatin1Char( 'u' ) );
    CheckComboBox box;
    box.setChoices( QJsonArray{ QJsonObject{ { "value", longValue }, { "label", longLabel } },
                                QJsonObject{ { "value", unlabelled } } } );
    auto* model = qobject_cast<QStandardItemModel*>( box.model() );
    REQUIRE( model );
    CHECK( model->item( 0 )->text().size() == PluginView::MaxTextChars );
    CHECK( model->item( 1 )->text().size() == PluginView::MaxTextChars );   // label from the value
    box.setCheckedValues( { longValue } );                        // the value as the plugin sent it
    REQUIRE( box.checkedValues().size() == 1 );
    CHECK( box.checkedValues().at( 0 ).toString().size() == PluginView::MaxFieldValueChars );
}

TEST_CASE( "E14 a row patch keeps the input of an open text cell editor", "[ui][workbench][E14]" )
{
    KindsView v;
    v.view.show();
    v.table->edit( v.cell( "t1" ) );
    QPointer<QLineEdit> line = v.table->viewport()->findChild<QLineEdit*>();
    REQUIRE( line );
    line->selectAll();
    QTest::keyClicks( line, "typed" );
    v.patch( QJsonObject{ { "id", "t1" }, { "tone", "warn" } } );
    QApplication::processEvents();
    REQUIRE( line );
    CHECK( line->text() == "typed" );
    QTest::keyClick( line, Qt::Key_Return );
    QApplication::processEvents();
    REQUIRE( v.events.count() == 1 );
    CHECK( v.lastValue() == "typed" );
    CHECK( v.events.last().front().toJsonObject().value( "previous" ) == "hello" );
    CHECK( v.cell( "t1" ).data().toString() == "typed" );
    CHECK( !editing( v.table ) );
}

TEST_CASE( "E14 a row patch keeps the ticks of an open multichoice cell list", "[ui][workbench][E14]" )
{
    KindsView v;
    v.view.show();
    v.table->edit( v.cell( "m1" ) );
    QPointer<CheckComboBox> box = v.table->viewport()->findChild<CheckComboBox*>();
    REQUIRE( box );
    QApplication::processEvents();                                  // the list opens once the editor is shown
    REQUIRE( box->view()->isVisible() );
    REQUIRE( box->checkedValues() == QJsonArray{ "0", "4" } );
    box->view()->setCurrentIndex( box->model()->index( 1, 0 ) );
    QTest::keyClick( box->view(), Qt::Key_Space );                   // ticks Mon
    REQUIRE( box->checkedValues() == QJsonArray{ "0", "1", "4" } );
    v.patch( QJsonObject{ { "id", "m1" }, { "tone", "warn" } } );
    QApplication::processEvents();
    REQUIRE( box );
    CHECK( box->checkedValues() == QJsonArray{ "0", "1", "4" } );
    CHECK( box->view()->isVisible() );
    QTest::keyClick( box->view(), Qt::Key_Return );                  // closing with Enter commits
    QApplication::processEvents();
    REQUIRE( v.events.count() == 1 );
    CHECK( v.lastValue().toArray() == QJsonArray{ "0", "1", "4" } );
    CHECK( v.cell( "m1" ).data().toString() == "Sun, Mon, Thu" );
    CHECK( !editing( v.table ) );
}

TEST_CASE( "E14 removing another row keeps an open cell editor", "[ui][workbench][E14]" )
{
    EditableView v;
    v.view.show();
    v.table->edit( v.cell( "r3", 1 ) );
    QPointer<QLineEdit> line = v.table->viewport()->findChild<QLineEdit*>();
    REQUIRE( line );
    line->selectAll();
    QTest::keyClicks( line, "45" );
    QSignalSpy events( &v.view, &PluginView::eventRaised );
    v.view.applyPatch( QJsonArray{ QJsonObject{ { "id", "fields" }, { "remove_rows", QJsonArray{ "r4" } } } } );
    QApplication::processEvents();
    REQUIRE( line );
    CHECK( line->isVisible() );                                      // Qt releases only the removed rows' editors
    CHECK( line->text() == "45" );
    CHECK( events.count() == 0 );
    QTest::keyClick( line, Qt::Key_Return );
    QApplication::processEvents();
    REQUIRE( events.count() == 1 );
    CHECK( events.last().front().toJsonObject().value( "row_id" ) == "r3" );
    CHECK( events.last().front().toJsonObject().value( "value" ) == "45" );
}

TEST_CASE( "E14 text and number cell editors take at most MaxTextChars characters", "[ui][workbench][E14]" )
{
    KindsView v;
    const auto maxLength = []( const std::unique_ptr<QWidget>& editor ) {
        const auto* line = qobject_cast<const QLineEdit*>( editor.get() );
        return line ? line->maxLength() : -1;
    };
    CHECK( maxLength( v.open( "t1" ) ) == PluginView::MaxTextChars );
    CHECK( maxLength( v.open( "n1" ) ) == PluginView::MaxTextChars );
    const auto textEditor = []( int length ) {
        return QJsonObject{ { "v", QJsonObject{ { "kind", "text" }, { "max_length", length } } } };
    };
    v.patch( QJsonObject{ { "id", "t1" }, { "editors", textEditor( 100000 ) } } );
    CHECK( maxLength( v.open( "t1" ) ) == PluginView::MaxTextChars );   // clamped
    v.patch( QJsonObject{ { "id", "t1" }, { "editors", textEditor( 10 ) } } );
    CHECK( maxLength( v.open( "t1" ) ) == 10 );

    // The editor holds no more than the cell shows, so every change is a change of the cell.
    v.patch( QJsonObject{ { "id", "t1" }, { "editors", QJsonObject{ { "v", QJsonObject{ { "kind", "text" } } } } } } );
    auto editor = v.open( "t1" );
    auto* line = qobject_cast<QLineEdit*>( editor.get() );
    REQUIRE( line );
    line->setText( QString( PluginView::MaxTextChars + 100, QLatin1Char( 'a' ) ) );
    CHECK( line->text().size() == PluginView::MaxTextChars );
    v.commit( line, "t1" );
    REQUIRE( v.events.count() == 1 );
    CHECK( v.lastValue().toString().size() == PluginView::MaxTextChars );
}

TEST_CASE( "E14 an unknown cell editor kind gets a text editor", "[ui][workbench][E14]" )
{
    KindsView v;
    v.patch( QJsonObject{ { "id", "t1" }, { "editors", QJsonObject{ { "v", QJsonObject{ { "kind", "slider" } } } } } } );
    for ( int open = 0; open < 2; ++open ) {                         // logged once per view and kind
        auto editor = v.open( "t1" );
        auto* line = qobject_cast<QLineEdit*>( editor.get() );
        REQUIRE( line );
        CHECK( line->text() == "hello" );
        CHECK( line->validator() == nullptr );
    }
}

TEST_CASE( "E14 an edited event the host could not deliver puts that cell back", "[ui][workbench][E14]" )
{
    KindsView v;
    bool drop = false;
    QObject::connect( &v.view, &PluginView::eventRaised, &v.view, [ & ]( const QJsonObject& event ) {
        if ( drop && event.value( "type" ) == "edited" ) {
            v.view.markEventDropped();
        }
    } );
    const auto edit = [ & ]( const QString& rowId, const QString& text ) {
        auto editor = v.open( rowId );
        auto* line = qobject_cast<QLineEdit*>( editor.get() );
        REQUIRE( line );
        line->setText( text );
        v.commit( line, rowId );
    };
    edit( "t1", "first" );                                          // delivered: pending
    REQUIRE( v.events.count() == 1 );
    REQUIRE( v.cell( "t1" ).data().toString() == "first" );
    REQUIRE( italic( v.cell( "t1" ) ) );

    drop = true;
    edit( "t1", "second" );
    CHECK( v.events.count() == 2 );                                 // raised, but never reached the plugin
    CHECK( v.cell( "t1" ).data().toString() == "first" );          // back to the delivered edit...
    CHECK( italic( v.cell( "t1" ) ) );                              // ...which still waits for the plugin
    edit( "n1", "45" );
    CHECK( v.cell( "n1" ).data().toString() == "30 s" );           // a cell without an earlier edit
    CHECK_FALSE( italic( v.cell( "n1" ) ) );

    v.view.revertPendingEdits();                                     // the plugin stops
    CHECK( v.cell( "t1" ).data().toString() == "hello" );

    drop = false;
    edit( "n1", "45" );
    CHECK( v.cell( "n1" ).data().toString() == "45" );
    CHECK( italic( v.cell( "n1" ) ) );
}

// PR #109 review: whether an edit changed anything is judged by value, not by the text a cell shows.

namespace {
// Two choices that share a label.
QJsonObject twinChoices()
{
    return QJsonObject{ { "kind", "choice" },
                        { "choices", QJsonArray{ QJsonObject{ { "value", "a" }, { "label", "Same" } },
                                                 QJsonObject{ { "value", "b" }, { "label", "Same" } } } } };
}
} // namespace

TEST_CASE( "E14 picking a choice that shares the cell's label is an edit", "[ui][workbench][E14]" )
{
    KindsView v;
    v.patch( QJsonObject{ { "id", "c1" },
                          { "cells", QJsonArray{ "c1", "Same" } },
                          { "editors", QJsonObject{ { "v", twinChoices() } } } } );
    SECTION( "through the cell's list" )
    {
        v.view.show();
        v.table->edit( v.cell( "c1" ) );
        auto* combo = v.table->viewport()->findChild<QComboBox*>();
        REQUIRE( combo );
        CHECK( combo->currentData() == QVariant( "a" ) );             // the first item with that label
        QApplication::processEvents();
        REQUIRE( combo->view()->isVisible() );
        combo->view()->setCurrentIndex( combo->model()->index( 1, 0 ) );
        QTest::keyClick( combo->view(), Qt::Key_Return );             // picks "b", also labelled "Same"
        QApplication::processEvents();
        REQUIRE( v.events.count() == 1 );
        CHECK( v.lastValue() == "b" );
        CHECK( v.events.last().front().toJsonObject().value( "previous" ) == "Same" );
        CHECK( v.cell( "c1" ).data().toString() == "Same" );
        CHECK( italic( v.cell( "c1" ) ) );
        CHECK( !editing( v.table ) );
    }
    SECTION( "a pending cell reopens on its value: the same value again is no edit, the other one is" )
    {
        auto editor = v.open( "c1" );
        auto* combo = qobject_cast<QComboBox*>( editor.get() );
        REQUIRE( combo );
        combo->setCurrentIndex( 1 );
        v.commit( combo, "c1" );
        REQUIRE( v.events.count() == 1 );
        v.commit( combo, "c1" );                                      // the focus leaving after the commit
        CHECK_FALSE( v.table->model()->setData( v.cell( "c1" ), QVariant( QJsonValue( "b" ) ), Qt::EditRole ) );
        CHECK( v.events.count() == 1 );
        auto reopened = v.open( "c1" );
        auto* again = qobject_cast<QComboBox*>( reopened.get() );
        REQUIRE( again );
        CHECK( again->currentData() == QVariant( "b" ) );             // the pending value, not the first "Same"
        again->setCurrentIndex( 0 );
        v.commit( again, "c1" );
        REQUIRE( v.events.count() == 2 );
        CHECK( v.lastValue() == "a" );
    }
}

TEST_CASE( "E14 a cell already showing the picked label is edited when its editor started elsewhere",
           "[ui][workbench][E14]" )
{
    KindsView v;
    // The plugin's editor starts at "0" (Off) while the cell already reads "On (1)".
    const QJsonObject choice{ { "kind", "choice" },
                              { "choices", QJsonArray{ QJsonObject{ { "value", "0" }, { "label", "Off (0)" } },
                                                       QJsonObject{ { "value", "1" }, { "label", "On (1)" } } } },
                              { "value", "0" } };
    v.patch( QJsonObject{ { "id", "c1" },
                          { "cells", QJsonArray{ "c1", "On (1)" } },
                          { "editors", QJsonObject{ { "v", choice } } } } );
    auto editor = v.open( "c1" );
    auto* combo = qobject_cast<QComboBox*>( editor.get() );
    REQUIRE( combo );
    CHECK( combo->currentData() == QVariant( "0" ) );
    combo->setCurrentIndex( combo->findData( "1" ) );
    v.commit( combo, "c1" );
    REQUIRE( v.events.count() == 1 );
    CHECK( v.lastValue() == "1" );
    CHECK( v.events.last().front().toJsonObject().value( "previous" ) == "On (1)" );
    CHECK( v.cell( "c1" ).data().toString() == "On (1)" );
    CHECK( italic( v.cell( "c1" ) ) );

    // A text cell's value is its text: committing the text it shows is no edit.
    CHECK_FALSE( v.table->model()->setData( v.cell( "t1" ), QVariant( QJsonValue( "hello" ) ), Qt::EditRole ) );
    CHECK( v.events.count() == 1 );
}

// PR #109 review: a replace_rows the table rejects leaves an open cell editor alone.

TEST_CASE( "E14 a replace_rows over the cell limit keeps the open cell editor and its input",
           "[ui][workbench][E14]" )
{
    EditableView v;
    v.view.show();
    v.table->edit( v.cell( "r3", 1 ) );
    QPointer<QLineEdit> line = v.table->viewport()->findChild<QLineEdit*>();
    REQUIRE( line );
    line->selectAll();
    QTest::keyClicks( line, "45" );
    QSignalSpy events( &v.view, &PluginView::eventRaised );
    QJsonArray tooMany;                                               // two columns: one row too many
    for ( int i = 0; i <= PluginView::MaxTableCells / 2; ++i ) {
        tooMany.append( QJsonObject{ { "id", QString( "x%1" ).arg( i ) }, { "cells", QJsonArray{ "x", "y" } } } );
    }
    v.view.applyPatch( QJsonArray{ QJsonObject{ { "id", "fields" }, { "replace_rows", tooMany } } } );
    QApplication::processEvents();
    CHECK( v.table->model()->rowCount() == 4 );                        // rejected: the table is unchanged
    CHECK( v.cell( "r3", 1 ).data().toString() == "30" );
    REQUIRE( line );
    CHECK( editing( v.table ) );
    CHECK( line->isVisible() );
    CHECK( line->text() == "45" );
    CHECK( events.count() == 0 );
    QTest::keyClick( line, Qt::Key_Return );
    QApplication::processEvents();
    REQUIRE( events.count() == 1 );
    CHECK( events.last().front().toJsonObject().value( "row_id" ) == "r3" );
    CHECK( events.last().front().toJsonObject().value( "value" ) == "45" );
}
