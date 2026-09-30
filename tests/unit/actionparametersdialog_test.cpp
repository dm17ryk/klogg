#include <catch2/catch.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QGroupBox>
#include <QLineEdit>
#include <QRadioButton>
#include <QSettings>

#include "actionparametersdialog.h"

TEST_CASE( "Action parameter dialog creates requested choice controls", "[actions][ui]" )
{
    ActionDefinition action;
    action.id = 987654;
    action.name = QStringLiteral( "Typed controls" );
    action.sequence.value = QStringLiteral( "COMMAND:;\r\n" );

    ActionParameterDefinition combo;
    combo.name = QStringLiteral( "mode" );
    combo.type = ActionParameterType::Choice;
    combo.presentation = ActionParameterPresentation::ComboBox;
    combo.choices = { { QStringLiteral( "Queued" ), 0 },
                      { QStringLiteral( "Full" ), 3 } };

    ActionParameterDefinition radio = combo;
    radio.name = QStringLiteral( "reset" );
    radio.presentation = ActionParameterPresentation::RadioButtons;

    ActionParameterDefinition checkboxes;
    checkboxes.name = QStringLiteral( "interfaces" );
    checkboxes.type = ActionParameterType::MultiChoice;
    checkboxes.presentation = ActionParameterPresentation::CheckBoxes;
    checkboxes.multiValueMode = ActionMultiValueMode::BitwiseOr;
    checkboxes.defaultValue = 5;
    checkboxes.choices = { { QStringLiteral( "GSM" ), 1 },
                           { QStringLiteral( "PSTN" ), 2 },
                           { QStringLiteral( "Ethernet" ), 4 },
                           { QStringLiteral( "BLE" ), 8 } };

    ActionParameterDefinition secret;
    secret.name = QStringLiteral( "pin" );
    secret.type = ActionParameterType::Text;
    secret.sensitive = true;
    secret.remember = false;

    ActionParameterDefinition date;
    date.name = QStringLiteral( "date" );
    date.type = ActionParameterType::Date;

    action.parameters.fields = { combo, radio, checkboxes, secret, date };

    QSettings settings;
    settings.remove( QStringLiteral( "advancedActions/history/%1" ).arg( action.id ) );
    ActionParametersDialog dialog( action );

    const auto* comboEditor = dialog.findChild<QComboBox*>( QStringLiteral( "actionParameter_mode" ) );
    REQUIRE( comboEditor != nullptr );
    REQUIRE( comboEditor->count() == 3 );

    const auto* radioGroup = dialog.findChild<QGroupBox*>( QStringLiteral( "actionParameter_reset" ) );
    REQUIRE( radioGroup != nullptr );
    REQUIRE( radioGroup->findChildren<QRadioButton*>().size() == 2 );

    const auto* checkboxGroup
        = dialog.findChild<QGroupBox*>( QStringLiteral( "actionParameter_interfaces" ) );
    REQUIRE( checkboxGroup != nullptr );
    const auto checkboxEditors = checkboxGroup->findChildren<QCheckBox*>();
    REQUIRE( checkboxEditors.size() == 4 );
    REQUIRE( checkboxEditors.at( 0 )->isChecked() );
    REQUIRE_FALSE( checkboxEditors.at( 1 )->isChecked() );
    REQUIRE( checkboxEditors.at( 2 )->isChecked() );
    REQUIRE_FALSE( checkboxEditors.at( 3 )->isChecked() );

    const auto* sensitiveEditor
        = dialog.findChild<QLineEdit*>( QStringLiteral( "actionParameter_pin" ) );
    REQUIRE( sensitiveEditor != nullptr );
    REQUIRE( sensitiveEditor->echoMode() == QLineEdit::Password );

    const auto* dateContainer = dialog.findChild<QWidget*>( QStringLiteral( "actionParameter_date" ) );
    REQUIRE( dateContainer != nullptr );
    const auto* dateEditor = dateContainer->findChild<QDateEdit*>();
    REQUIRE( dateEditor != nullptr );
    REQUIRE( dateEditor->date().isValid() );
    auto* omitDate = dateContainer->findChild<QCheckBox*>();
    REQUIRE( omitDate != nullptr );
    REQUIRE( omitDate->isChecked() );
    REQUIRE_FALSE( dialog.currentValues().contains( date.name ) );
    omitDate->setChecked( false );
    REQUIRE( dialog.currentValues().value( date.name ).toDate() == dateEditor->date() );
}
