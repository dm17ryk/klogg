#include "actioneditdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QtGlobal>
#include <QSpinBox>
#include <QVBoxLayout>

#include "configuration.h"
#include "previewdecodeutils.h"

namespace {
QByteArray displayedStringToBytes( const QString& text )
{
    QByteArray bytes;
    bytes.reserve( text.size() );

    for ( int index = 0; index < text.size(); ++index ) {
        const auto ch = text.at( index );
        if ( ch != QLatin1Char( '\\' ) ) {
            bytes.push_back( static_cast<char>( ch.toLatin1() ) );
            continue;
        }

        if ( index + 1 >= text.size() ) {
            bytes.push_back( '\\' );
            continue;
        }

        const auto next = text.at( index + 1 );
        switch ( next.toLatin1() ) {
        case '\\':
            bytes.push_back( '\\' );
            ++index;
            break;
        case 'r':
            bytes.push_back( '\r' );
            ++index;
            break;
        case 'n':
            bytes.push_back( '\n' );
            ++index;
            break;
        case 't':
            bytes.push_back( '\t' );
            ++index;
            break;
        case '0':
            bytes.push_back( '\0' );
            ++index;
            break;
        case 'x':
            if ( index + 3 < text.size() ) {
                bool ok = false;
                const auto hexByte = text.mid( index + 2, 2 ).toUInt( &ok, 16 );
                if ( ok ) {
                    bytes.push_back( static_cast<char>( hexByte ) );
                    index += 3;
                    break;
                }
            }
            bytes.push_back( '\\' );
            break;
        default:
            bytes.push_back( '\\' );
            break;
        }
    }

    return bytes;
}

QString bytesToDisplayedString( const QByteArray& bytes )
{
    QString text;
    text.reserve( bytes.size() );
    for ( const auto rawByte : bytes ) {
        const auto byte = static_cast<quint8>( rawByte );
        switch ( byte ) {
        case '\\':
            text += QStringLiteral( "\\\\" );
            break;
        case '\r':
            text += QStringLiteral( "\\r" );
            break;
        case '\n':
            text += QStringLiteral( "\\n" );
            break;
        case '\t':
            text += QStringLiteral( "\\t" );
            break;
        case '\0':
            text += QStringLiteral( "\\0" );
            break;
        default:
            if ( byte >= 0x20 && byte <= 0x7E ) {
                text += QLatin1Char( static_cast<char>( byte ) );
            }
            else {
                text += QStringLiteral( "\\x%1" )
                            .arg( byte, 2, 16, QLatin1Char( '0' ) )
                            .toUpper();
            }
            break;
        }
    }
    return text;
}

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

QString lineEndingEscapeText( const QString& lineEndingMode )
{
    if ( lineEndingMode == QStringLiteral( "cr" ) ) {
        return QStringLiteral( "\\r" );
    }
    if ( lineEndingMode == QStringLiteral( "lf" ) ) {
        return QStringLiteral( "\\n" );
    }
    return QStringLiteral( "\\r\\n" );
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

    lineEndingCombo_ = new QComboBox( this );
    lineEndingCombo_->addItem( tr( "CRLF (\\r\\n)" ), QStringLiteral( "crlf" ) );
    lineEndingCombo_->addItem( tr( "LF (\\n)" ), QStringLiteral( "lf" ) );
    lineEndingCombo_->addItem( tr( "CR (\\r)" ), QStringLiteral( "cr" ) );
    const auto lineEndingIndex
        = lineEndingCombo_->findData( Configuration::get().defaultActionEditorLineEnding() );
    lineEndingCombo_->setCurrentIndex( lineEndingIndex >= 0 ? lineEndingIndex : 0 );

    stringValueEdit_ = new QPlainTextEdit( this );
    stringValueEdit_->setTabChangesFocus( true );
    stringValueEdit_->setMinimumHeight( 80 );
    stringValueEdit_->installEventFilter( this );

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
    formLayout->addRow( tr( "Enter inserts" ), lineEndingCombo_ );
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

bool ActionEditDialog::eventFilter( QObject* watched, QEvent* event )
{
    if ( watched == stringValueEdit_ && event->type() == QEvent::KeyPress ) {
        const auto* keyEvent = static_cast<QKeyEvent*>( event );
        if ( ( keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter )
             && keyEvent->modifiers() == Qt::NoModifier ) {
            auto cursor = stringValueEdit_->textCursor();
            cursor.insertText(
                lineEndingEscapeText( lineEndingCombo_->currentData().toString() ) );
            stringValueEdit_->setTextCursor( cursor );
            return true;
        }
    }

    return QDialog::eventFilter( watched, event );
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
    const auto stringBytes = displayedStringToBytes( stringValueEdit_->toPlainText() );
    const auto hexValue = hexValueEdit_->toPlainText().trimmed();
    action_.sequence.value
        = action_.sequence.type == ActionSequenceType::HexString
              ? hexValue
              : QString::fromLatin1( stringBytes.constData(), stringBytes.size() );
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
    hexValueEdit_->setPlainText( bytesToHexString( displayedStringToBytes( stringValueEdit_->toPlainText() ) ) );
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
    stringValueEdit_->setPlainText( bytesToDisplayedString( decoded.bytes ) );
    syncingSequenceEditors_ = false;
}

void ActionEditDialog::setSequenceEditors( const ActionSequence& sequence )
{
    syncingSequenceEditors_ = true;
    const QSignalBlocker stringBlocker( stringValueEdit_ );
    const QSignalBlocker hexBlocker( hexValueEdit_ );

    if ( sequence.type == ActionSequenceType::HexString ) {
        const auto decoded = decodeHexStringToBytes( sequence.value );
        stringValueEdit_->setPlainText( decoded.ok ? bytesToDisplayedString( decoded.bytes )
                                                   : QString() );
        hexValueEdit_->setPlainText( decoded.ok ? bytesToHexString( decoded.bytes ) : sequence.value );
    }
    else {
        const auto bytes = sequence.value.toLatin1();
        stringValueEdit_->setPlainText( bytesToDisplayedString( bytes ) );
        hexValueEdit_->setPlainText( bytesToHexString( bytes ) );
    }

    syncingSequenceEditors_ = false;
}
