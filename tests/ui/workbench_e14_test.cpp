#include <catch2/catch.hpp>

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardItemModel>

#include "checkcombobox.h"

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
    model->item( 1 )->setCheckState( Qt::Unchecked );           // what a click in the popup does
    CHECK( box.checkedValues() == QJsonArray{ "0", "4" } );
    CHECK( changed.count() >= 1 );
}

TEST_CASE( "E14 CheckComboBox drops values whose choices disappear", "[ui][workbench][E14]" )
{
    CheckComboBox box;
    box.setChoices( days() );
    box.setCheckedValues( { "0", "4" } );
    box.setChoices( QJsonArray{ QJsonObject{ { "value", "0" }, { "label", "Sun" } } } );
    CHECK( box.checkedValues() == QJsonArray{ "0" } );
}
