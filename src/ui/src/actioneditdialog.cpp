#include "actioneditdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QtGlobal>
#include <QSpinBox>
#include <QVBoxLayout>

#include "previewdecodeutils.h"

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

QString bytesToHexString( const QByteArray& bytes )
{
    QStringList parts;
    parts.reserve( bytes.size() );
    for ( const auto byte : bytes ) {
        parts.push_back( QStringLiteral( "%1" )
                             .arg( static_cast<quint8>( byte ), 2, 16, QLatin1Char( '0' ) )
                             .toUpper() );
    }
    return parts.join( QLatin1Char( ' ' ) );
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

    stringValueEdit_ = new QPlainTextEdit( this );
    stringValueEdit_->setTabChangesFocus( true );
    stringValueEdit_->setMinimumHeight( 80 );

    hexValueEdit_ = new QPlainTextEdit( this );
    hexValueEdit_->setTabChangesFocus( true );
    hexValueEdit_->setMinimumHeight( 80 );

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
    formLayout->addRow( tr( "String value" ), stringValueEdit_ );
    formLayout->addRow( tr( "Hex string value" ), hexValueEdit_ );
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

    connect( stringValueEdit_, &QPlainTextEdit::textChanged, this, &ActionEditDialog::syncHexFromString );
    connect( hexValueEdit_, &QPlainTextEdit::textChanged, this, &ActionEditDialog::syncStringFromHex );
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
    const auto stringValue = stringValueEdit_->toPlainText();
    const auto hexValue = hexValueEdit_->toPlainText().trimmed();
    action_.sequence.value = action_.sequence.type == ActionSequenceType::HexString ? hexValue : stringValue;
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
    setSequenceEditors( action.sequence );
    delaySpin_->setValue( action.parameters.delay );
    repeatCountSpin_->setValue( qMax( 1, action.parameters.repeatCount ) );
    repeatIntervalSpin_->setValue( action.parameters.repeatInterval );
    variableNamesEdit_->setText( action.parameters.variableNames.join( QStringLiteral( ", " ) ) );
    checksumEnabledCheck_->setChecked( action.checksum.enabled );
    const auto checksumIndex = checksumAlgorithmCombo_->findText( action.checksum.algorithm );
    checksumAlgorithmCombo_->setCurrentIndex( checksumIndex >= 0 ? checksumIndex : 0 );
    checksumPlaceholderEdit_->setText( action.checksum.placeholder );
}

void ActionEditDialog::syncHexFromString()
{
    if ( syncingSequenceEditors_ ) {
        return;
    }

    syncingSequenceEditors_ = true;
    const QSignalBlocker blocker( hexValueEdit_ );
    hexValueEdit_->setPlainText( bytesToHexString( stringValueEdit_->toPlainText().toLatin1() ) );
    syncingSequenceEditors_ = false;
}

void ActionEditDialog::syncStringFromHex()
{
    if ( syncingSequenceEditors_ ) {
        return;
    }

    const auto decoded = decodeHexStringToBytes( hexValueEdit_->toPlainText() );
    if ( !decoded.ok ) {
        return;
    }

    syncingSequenceEditors_ = true;
    const QSignalBlocker blocker( stringValueEdit_ );
    stringValueEdit_->setPlainText( QString::fromLatin1( decoded.bytes ) );
    syncingSequenceEditors_ = false;
}

void ActionEditDialog::setSequenceEditors( const ActionSequence& sequence )
{
    syncingSequenceEditors_ = true;
    const QSignalBlocker stringBlocker( stringValueEdit_ );
    const QSignalBlocker hexBlocker( hexValueEdit_ );

    if ( sequence.type == ActionSequenceType::HexString ) {
        const auto decoded = decodeHexStringToBytes( sequence.value );
        stringValueEdit_->setPlainText( decoded.ok ? QString::fromLatin1( decoded.bytes ) : QString() );
        hexValueEdit_->setPlainText( decoded.ok ? bytesToHexString( decoded.bytes ) : sequence.value );
    }
    else {
        stringValueEdit_->setPlainText( sequence.value );
        hexValueEdit_->setPlainText( bytesToHexString( sequence.value.toLatin1() ) );
    }

    syncingSequenceEditors_ = false;
}
