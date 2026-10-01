#include "actioneditdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QtGlobal>

#include "configuration.h"
#include "actionparameterseditorwidget.h"
#include "log.h"
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
    descriptionEdit_->setMinimumHeight( 60 );

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
    stringValueEdit_->setMinimumHeight( 65 );
    stringValueEdit_->installEventFilter( this );

    hexValueEdit_ = new QPlainTextEdit( this );
    hexValueEdit_->setTabChangesFocus( true );
    hexValueEdit_->setMinimumHeight( 65 );

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
    variableNamesEdit_->setToolTip( tr( "Legacy ${name} substitutions. Leave empty when typed fields "
                                        "are used; their values are selected after clicking Send." ) );

    expressionEdit_ = new QPlainTextEdit( this );
    expressionEdit_->setObjectName( QStringLiteral( "actionExpressionEdit" ) );
    expressionEdit_->setPlaceholderText(
        tr( "Example: concat(\"SCHED STATS:\", mode == null ? \"\" : str(mode), \";\\r\\n\")" ) );
    expressionEdit_->setTabChangesFocus( true );
    expressionEdit_->setMinimumHeight( 65 );
    expressionEdit_->setToolTip( tr( "Optional advanced expression that builds the complete output. "
                                      "Leave empty when the sequence uses ${field} placeholders." ) );

    fieldsJsonEdit_ = new QPlainTextEdit( this );
    fieldsJsonEdit_->setObjectName( QStringLiteral( "actionParameterFieldsJsonEdit" ) );
    fieldsJsonEdit_->setAccessibleName( tr( "Typed action parameter definitions" ) );
    fieldsJsonEdit_->setPlaceholderText( tr(
        R"([{"name":"mode","type":"choice","presentation":"combo_box","choices":[{"label":"Queued","value":0}]}])" ) );
    fieldsJsonEdit_->setToolTip(
        tr( "Defines the controls shown by Send: automatic, text_box, combo_box, "
            "radio_buttons, or check_boxes. This is a definition, not the values to send. "
            "Multi-value modes: bitwise_or, comma_separated, custom_separator." ) );
    fieldsJsonEdit_->setTabChangesFocus( true );
    fieldsJsonEdit_->setMinimumHeight( 130 );

    checksumEnabledCheck_ = new QCheckBox( tr( "Enable checksum" ), this );
    checksumAlgorithmCombo_ = new QComboBox( this );
    checksumAlgorithmCombo_->addItem( QStringLiteral( "sum8" ) );
    checksumAlgorithmCombo_->addItem( QStringLiteral( "crc16_ccitt" ) );
    checksumPlaceholderEdit_ = new QLineEdit( this );
    checksumPlaceholderEdit_->setPlaceholderText( QStringLiteral( "${CHECKSUM}" ) );

    auto* tabs = new QTabWidget( this );
    tabs->setObjectName( QStringLiteral( "actionEditorTabs" ) );

    auto* sequencePage = new QWidget( tabs );
    auto* sequenceLayout = new QFormLayout( sequencePage );
    sequenceLayout->addRow( tr( "Name" ), nameEdit_ );
    sequenceLayout->addRow( tr( "Description" ), descriptionEdit_ );
    sequenceLayout->addRow( tr( "Sequence type" ), sequenceTypeCombo_ );
    sequenceLayout->addRow( tr( "Enter inserts" ), lineEndingCombo_ );
    sequenceLayout->addRow( tr( "String value" ), stringValueEdit_ );
    sequenceLayout->addRow( tr( "Hex string value" ), hexValueEdit_ );
    tabs->addTab( sequencePage, tr( "Sequence" ) );

    auto* parametersPage = new QWidget( tabs );
    auto* parametersLayout = new QVBoxLayout( parametersPage );
    auto* parameterHelp = new QLabel(
        tr( "Typed parameter definitions below create the combo boxes, radio buttons, and "
            "checkboxes shown when you press Send in the actions table. Add each parameter with "
            "the structured editor; current values are selected after clicking Send." ),
        this );
    parameterHelp->setWordWrap( true );
    parameterHelp->setObjectName( QStringLiteral( "actionParameterDefinitionHelp" ) );
    parametersLayout->addWidget( parameterHelp );
    parametersEditor_ = new ActionParametersEditorWidget( parametersPage );
    parametersEditor_->setObjectName( QStringLiteral( "actionParametersEditor" ) );
    parametersLayout->addWidget( parametersEditor_, 1 );
    auto* expressionLayout = new QFormLayout;
    expressionLayout->addRow( tr( "Output expression (advanced)" ), expressionEdit_ );
    parametersLayout->addLayout( expressionLayout );

    auto* advancedToggle = new QToolButton( parametersPage );
    advancedToggle->setText( tr( "Advanced JSON" ) );
    advancedToggle->setCheckable( true );
    advancedToggle->setChecked( false );
    advancedToggle->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    auto* advancedJson = new QWidget( parametersPage );
    auto* advancedJsonLayout = new QVBoxLayout( advancedJson );
    advancedJsonLayout->setContentsMargins( 0, 0, 0, 0 );
    auto* advancedLabel = new QLabel(
        tr( "Compatibility fallback. Apply valid JSON to replace the structured definitions." ),
        advancedJson );
    advancedLabel->setWordWrap( true );
    applyFieldsJsonButton_ = new QPushButton( tr( "Apply JSON" ), advancedJson );
    advancedJsonLayout->addWidget( advancedLabel );
    advancedJsonLayout->addWidget( fieldsJsonEdit_, 1 );
    advancedJsonLayout->addWidget( applyFieldsJsonButton_, 0, Qt::AlignRight );
    advancedJson->setVisible( false );
    parametersLayout->addWidget( advancedToggle );
    parametersLayout->addWidget( advancedJson );
    const auto syncAdvancedFields = [ this ] {
        QVariantList fields;
        for ( const auto& field : parametersEditor_->fields() ) {
            QVariantMap fieldMap;
            fieldMap.insert( QStringLiteral( "name" ), field.name );
            fieldMap.insert( QStringLiteral( "label" ), field.label );
            fieldMap.insert( QStringLiteral( "description" ), field.description );
            fieldMap.insert( QStringLiteral( "type" ), actionParameterTypeToString( field.type ) );
            fieldMap.insert( QStringLiteral( "presentation" ),
                             actionParameterPresentationToString( field.presentation ) );
            fieldMap.insert( QStringLiteral( "multi_value_mode" ),
                             actionMultiValueModeToString( field.multiValueMode ) );
            fieldMap.insert( QStringLiteral( "required" ), field.required );
            fieldMap.insert( QStringLiteral( "sensitive" ), field.sensitive );
            fieldMap.insert( QStringLiteral( "remember" ), field.remember );
            if ( field.defaultValue.isValid() ) {
                fieldMap.insert( QStringLiteral( "default" ), field.defaultValue );
            }
            if ( field.minimum.isValid() ) {
                fieldMap.insert( QStringLiteral( "minimum" ), field.minimum );
            }
            if ( field.maximum.isValid() ) {
                fieldMap.insert( QStringLiteral( "maximum" ), field.maximum );
            }
            if ( field.step.isValid() ) {
                fieldMap.insert( QStringLiteral( "step" ), field.step );
            }
            fieldMap.insert( QStringLiteral( "validation_pattern" ), field.validationPattern );
            fieldMap.insert( QStringLiteral( "format" ), field.format );
            fieldMap.insert( QStringLiteral( "separator" ), field.separator );
            fieldMap.insert( QStringLiteral( "expression" ), field.expression );
            QVariantList choices;
            for ( const auto& choice : field.choices ) {
                choices.push_back( QVariantMap { { QStringLiteral( "label" ), choice.label },
                                                  { QStringLiteral( "value" ), choice.value } } );
            }
            fieldMap.insert( QStringLiteral( "choices" ), choices );
            fields.push_back( fieldMap );
        }
        fieldsJsonEdit_->setPlainText(
            QString::fromUtf8( QJsonDocument::fromVariant( fields ).toJson( QJsonDocument::Indented ) ) );
    };
    connect( advancedToggle, &QToolButton::toggled, this,
             [ advancedJson, syncAdvancedFields ]( const bool checked ) {
                 if ( checked ) {
                     syncAdvancedFields();
                 }
                 advancedJson->setVisible( checked );
             } );
    connect( applyFieldsJsonButton_, &QPushButton::clicked, this, [ this ] {
        QJsonParseError parseError;
        const auto document
            = QJsonDocument::fromJson( fieldsJsonEdit_->toPlainText().toUtf8(), &parseError );
        if ( parseError.error != QJsonParseError::NoError || !document.isArray() ) {
            LOG_WARNING << "Action editor rejected advanced parameter JSON";
            QMessageBox::warning( this, tr( "Edit Action" ),
                                  tr( "Typed fields must be a valid JSON array: %1" )
                                      .arg( parseError.errorString() ) );
            return;
        }
        QVariantMap actionMap = actionDefinitionToVariantMap( action_ );
        auto parametersMap = actionMap.value( QStringLiteral( "parameters" ) ).toMap();
        parametersMap.insert( QStringLiteral( "fields" ), document.array().toVariantList() );
        actionMap.insert( QStringLiteral( "parameters" ), parametersMap );
        QString errorMessage;
        const auto parsed = actionDefinitionFromVariantMap( actionMap, &errorMessage );
        if ( !errorMessage.isEmpty() ) {
            LOG_WARNING << "Action editor advanced parameter JSON parse failed";
            QMessageBox::warning( this, tr( "Edit Action" ), errorMessage );
            return;
        }
        parametersEditor_->setFields( parsed.parameters.fields );
        LOG_DEBUG << "Applied advanced action parameter JSON; field_count="
                  << parsed.parameters.fields.size();
    } );
    tabs->addTab( parametersPage, tr( "Parameters" ) );

    auto* advancedPage = new QWidget( tabs );
    auto* advancedLayout = new QFormLayout( advancedPage );
    advancedLayout->addRow( tr( "Initial delay" ), delaySpin_ );
    advancedLayout->addRow( tr( "Repeat count" ), repeatCountSpin_ );
    advancedLayout->addRow( tr( "Repeat interval" ), repeatIntervalSpin_ );
    advancedLayout->addRow( tr( "Template variables (legacy)" ), variableNamesEdit_ );
    advancedLayout->addRow( QString(), checksumEnabledCheck_ );
    advancedLayout->addRow( tr( "Checksum algorithm" ), checksumAlgorithmCombo_ );
    advancedLayout->addRow( tr( "Checksum placeholder" ), checksumPlaceholderEdit_ );
    tabs->addTab( advancedPage, tr( "Advanced" ) );

    auto* buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
    connect( buttons, &QDialogButtonBox::accepted, this, &ActionEditDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, this, &ActionEditDialog::reject );

    auto* layout = new QVBoxLayout( this );
    layout->addWidget( tabs, 1 );
    layout->addWidget( buttons );
    setLayout( layout );
    setSizeGripEnabled( true );
    setMinimumSize( 620, 430 );
    resize( 780, 560 );

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
    action_.expression = expressionEdit_->toPlainText().trimmed();

    auto actionMap = actionDefinitionToVariantMap( action_ );
    auto parametersMap = actionMap.value( QStringLiteral( "parameters" ) ).toMap();
    QVariantList fields;
    for ( const auto& field : parametersEditor_->fields() ) {
        QVariantMap fieldMap;
        fieldMap.insert( QStringLiteral( "name" ), field.name );
        fieldMap.insert( QStringLiteral( "label" ), field.label );
        fieldMap.insert( QStringLiteral( "description" ), field.description );
        fieldMap.insert( QStringLiteral( "type" ), actionParameterTypeToString( field.type ) );
        fieldMap.insert( QStringLiteral( "presentation" ),
                         actionParameterPresentationToString( field.presentation ) );
        fieldMap.insert( QStringLiteral( "multi_value_mode" ),
                         actionMultiValueModeToString( field.multiValueMode ) );
        fieldMap.insert( QStringLiteral( "required" ), field.required );
        fieldMap.insert( QStringLiteral( "sensitive" ), field.sensitive );
        fieldMap.insert( QStringLiteral( "remember" ), field.remember );
        if ( field.defaultValue.isValid() ) fieldMap.insert( QStringLiteral( "default" ), field.defaultValue );
        if ( field.minimum.isValid() ) fieldMap.insert( QStringLiteral( "minimum" ), field.minimum );
        if ( field.maximum.isValid() ) fieldMap.insert( QStringLiteral( "maximum" ), field.maximum );
        if ( field.step.isValid() ) fieldMap.insert( QStringLiteral( "step" ), field.step );
        fieldMap.insert( QStringLiteral( "validation_pattern" ), field.validationPattern );
        fieldMap.insert( QStringLiteral( "format" ), field.format );
        fieldMap.insert( QStringLiteral( "separator" ), field.separator );
        fieldMap.insert( QStringLiteral( "expression" ), field.expression );
        QVariantList choices;
        for ( const auto& choice : field.choices ) {
            QVariantMap choiceMap;
            choiceMap.insert( QStringLiteral( "label" ), choice.label );
            choiceMap.insert( QStringLiteral( "value" ), choice.value );
            choices.push_back( choiceMap );
        }
        fieldMap.insert( QStringLiteral( "choices" ), choices );
        fields.push_back( fieldMap );
    }
    parametersMap.insert( QStringLiteral( "fields" ), fields );
    actionMap.insert( QStringLiteral( "parameters" ), parametersMap );
    actionMap.insert( QStringLiteral( "expression" ), action_.expression );
    QString fieldError;
    const auto parsedAction = actionDefinitionFromVariantMap( actionMap, &fieldError );
    if ( !fieldError.isEmpty() ) {
        QMessageBox::warning( this, tr( "Edit Action" ), fieldError );
        return;
    }
    action_ = parsedAction;
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
    expressionEdit_->setPlainText( action.expression );
    const auto actionMap = actionDefinitionToVariantMap( action );
    const auto fields = actionMap.value( QStringLiteral( "parameters" ) )
                            .toMap()
                            .value( QStringLiteral( "fields" ) )
                            .toList();
    fieldsJsonEdit_->setPlainText( QString::fromUtf8(
        QJsonDocument::fromVariant( fields ).toJson( QJsonDocument::Indented ) ) );
    parametersEditor_->setFields( action.parameters.fields );
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
