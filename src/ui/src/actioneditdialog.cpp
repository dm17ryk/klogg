#include "actioneditdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QtGlobal>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
QStringList splitCsvValues( const QString& text )
{
    QStringList values;
    const auto parts = text.split( QLatin1Char( ',' ), Qt::SkipEmptyParts );
    for ( const auto& part : parts ) {
        const auto trimmed = part.trimmed();
        if ( !trimmed.isEmpty() ) {
            values.push_back( trimmed );
        }
    }
    return values;
}
} // namespace

ActionEditDialog::ActionEditDialog( QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "Edit Action" ) );

    nameEdit_ = new QLineEdit( this );
    descriptionEdit_ = new QPlainTextEdit( this );
    descriptionEdit_->setTabChangesFocus( true );
    descriptionEdit_->setMinimumHeight( 90 );

    sequenceTypeCombo_ = new QComboBox( this );
    sequenceTypeCombo_->addItem( tr( "String" ), actionSequenceTypeToString( ActionSequenceType::String ) );
    sequenceTypeCombo_->addItem( tr( "Hex String" ), actionSequenceTypeToString( ActionSequenceType::HexString ) );

    sequenceValueEdit_ = new QPlainTextEdit( this );
    sequenceValueEdit_->setTabChangesFocus( true );
    sequenceValueEdit_->setMinimumHeight( 110 );

    delaySpin_ = new QSpinBox( this );
    delaySpin_->setRange( 0, 3600000 );
    delaySpin_->setSuffix( tr( " ms" ) );

    repeatCountSpin_ = new QSpinBox( this );
    repeatCountSpin_->setRange( 1, 1000 );

    repeatIntervalSpin_ = new QSpinBox( this );
    repeatIntervalSpin_->setRange( 0, 3600000 );
    repeatIntervalSpin_->setSuffix( tr( " ms" ) );

    variableNamesEdit_ = new QLineEdit( this );
    variableNamesEdit_->setPlaceholderText( tr( "value1, value2" ) );

    checksumEnabledCheck_ = new QCheckBox( tr( "Enable checksum" ), this );
    checksumAlgorithmCombo_ = new QComboBox( this );
    checksumAlgorithmCombo_->addItem( QStringLiteral( "sum8" ) );
    checksumAlgorithmCombo_->addItem( QStringLiteral( "crc16_ccitt" ) );
    checksumPlaceholderEdit_ = new QLineEdit( this );
    checksumPlaceholderEdit_->setPlaceholderText( QStringLiteral( "${CHECKSUM}" ) );

    auto* formLayout = new QFormLayout;
    formLayout->addRow( tr( "Name" ), nameEdit_ );
    formLayout->addRow( tr( "Description" ), descriptionEdit_ );
    formLayout->addRow( tr( "Sequence type" ), sequenceTypeCombo_ );
    formLayout->addRow( tr( "Sequence" ), sequenceValueEdit_ );
    formLayout->addRow( tr( "Initial delay" ), delaySpin_ );
    formLayout->addRow( tr( "Repeat count" ), repeatCountSpin_ );
    formLayout->addRow( tr( "Repeat interval" ), repeatIntervalSpin_ );
    formLayout->addRow( tr( "Template variables" ), variableNamesEdit_ );
    formLayout->addRow( QString(), checksumEnabledCheck_ );
    formLayout->addRow( tr( "Checksum algorithm" ), checksumAlgorithmCombo_ );
    formLayout->addRow( tr( "Checksum placeholder" ), checksumPlaceholderEdit_ );

    auto* buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
    connect( buttons, &QDialogButtonBox::accepted, this, &ActionEditDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, this, &ActionEditDialog::reject );

    auto* layout = new QVBoxLayout( this );
    layout->addLayout( formLayout );
    layout->addWidget( buttons );
    setLayout( layout );
}

void ActionEditDialog::setAction( const ActionDefinition& action )
{
    action_ = action;
    populateFromAction( action );
}

ActionDefinition ActionEditDialog::action() const
{
    return action_;
}

void ActionEditDialog::accept()
{
    action_.name = nameEdit_->text().trimmed();
    action_.description = descriptionEdit_->toPlainText().trimmed();
    bool ok = false;
    action_.sequence.type = actionSequenceTypeFromString(
        sequenceTypeCombo_->currentData().toString(), &ok );
    if ( !ok ) {
        action_.sequence.type = ActionSequenceType::String;
    }
    action_.sequence.value = sequenceValueEdit_->toPlainText().trimmed();
    action_.parameters.delay = delaySpin_->value();
    action_.parameters.repeatCount = repeatCountSpin_->value();
    action_.parameters.repeat = action_.parameters.repeatCount > 1;
    action_.parameters.repeatInterval = repeatIntervalSpin_->value();
    action_.parameters.variableNames = splitCsvValues( variableNamesEdit_->text() );
    action_.checksum.enabled = checksumEnabledCheck_->isChecked();
    action_.checksum.algorithm = checksumAlgorithmCombo_->currentText();
    action_.checksum.placeholder = checksumPlaceholderEdit_->text().trimmed();

    QString errorMessage;
    if ( !validateActionDefinition( action_, &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Edit Action" ), errorMessage );
        return;
    }

    QDialog::accept();
}

void ActionEditDialog::populateFromAction( const ActionDefinition& action )
{
    nameEdit_->setText( action.name );
    descriptionEdit_->setPlainText( action.description );
    const auto index = sequenceTypeCombo_->findData( actionSequenceTypeToString( action.sequence.type ) );
    sequenceTypeCombo_->setCurrentIndex( index >= 0 ? index : 0 );
    sequenceValueEdit_->setPlainText( action.sequence.value );
    delaySpin_->setValue( action.parameters.delay );
    repeatCountSpin_->setValue( qMax( 1, action.parameters.repeatCount ) );
    repeatIntervalSpin_->setValue( action.parameters.repeatInterval );
    variableNamesEdit_->setText( action.parameters.variableNames.join( QStringLiteral( ", " ) ) );
    checksumEnabledCheck_->setChecked( action.checksum.enabled );
    const auto checksumIndex = checksumAlgorithmCombo_->findText( action.checksum.algorithm );
    checksumAlgorithmCombo_->setCurrentIndex( checksumIndex >= 0 ? checksumIndex : 0 );
    checksumPlaceholderEdit_->setText( action.checksum.placeholder );
}
