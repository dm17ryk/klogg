#include <catch2/catch.hpp>

#include <QAbstractItemView>
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTest>

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
