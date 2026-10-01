#include <catch2/catch.hpp>

#include <QTableWidget>

#include "actionparameterseditorwidget.h"

TEST_CASE( "Structured action parameter editor displays and preserves definitions", "[actions][ui]" )
{
    ActionParameterDefinition mode;
    mode.name = QStringLiteral( "mode" );
    mode.label = QStringLiteral( "Mode" );
    mode.type = ActionParameterType::Choice;
    mode.presentation = ActionParameterPresentation::ComboBox;
    mode.choices = { { QStringLiteral( "Queued" ), 0 }, { QStringLiteral( "Full" ), 3 } };

    ActionParametersEditorWidget editor;
    editor.setFields( { mode } );
    const auto* table = editor.findChild<QTableWidget*>(
        QStringLiteral( "actionParametersDefinitionTable" ) );
    REQUIRE( table != nullptr );
    REQUIRE( table->rowCount() == 1 );
    REQUIRE( table->item( 0, 1 )->text() == QStringLiteral( "mode" ) );
    REQUIRE( editor.fields().front().choices.size() == 2 );
}
