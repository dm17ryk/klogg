#include "actionparameterdefinitiondialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <limits>
#include <utility>

#include "actionexpression.h"
#include "log.h"

namespace {
QString variantText( const QVariant& value )
{
    if ( !value.isValid() || value.isNull() ) {
        return {};
    }
    if ( value.typeId() == QMetaType::QVariantList || value.typeId() == QMetaType::QStringList ) {
        QStringList values;
        for ( const auto& item : value.toList() ) {
            values.push_back( item.toString() );
        }
        return values.join( QStringLiteral( ", " ) );
    }
    return value.toString();
}

QVariant parseScalar( const QString& text, ActionParameterType type, bool* ok )
{
    const auto value = text.trimmed();
    if ( value.isEmpty() ) {
        if ( ok ) {
            *ok = true;
        }
        return {};
    }
    bool localOk = false;
    QVariant result;
    switch ( type ) {
    case ActionParameterType::Integer:
        result = value.toLongLong( &localOk );
        break;
    case ActionParameterType::Decimal:
        result = value.toDouble( &localOk );
        break;
    case ActionParameterType::Boolean:
        if ( value.compare( QStringLiteral( "true" ), Qt::CaseInsensitive ) == 0
             || value == QStringLiteral( "1" ) ) {
            result = true;
            localOk = true;
        }
        else if ( value.compare( QStringLiteral( "false" ), Qt::CaseInsensitive ) == 0
                  || value == QStringLiteral( "0" ) ) {
            result = false;
            localOk = true;
        }
        break;
    case ActionParameterType::MultiChoice: {
        QVariantList values;
        for ( const auto& item : value.split( QLatin1Char( ',' ), Qt::SkipEmptyParts ) ) {
            values.push_back( item.trimmed() );
        }
        result = values;
        localOk = true;
        break;
    }
    default:
        result = value;
        localOk = true;
        break;
    }
    if ( ok ) {
        *ok = localOk;
    }
    return result;
}

QVariant parseChoiceText( const QString& text, const QVariant& exemplar, bool* ok )
{
    if ( exemplar.typeId() == QMetaType::QVariantList
         || exemplar.typeId() == QMetaType::QStringList ) {
        const auto items = exemplar.toList();
        return parseChoiceText( text, items.isEmpty() ? QVariant{} : items.front(), ok );
    }
    if ( exemplar.typeId() == QMetaType::Int || exemplar.typeId() == QMetaType::LongLong
         || exemplar.typeId() == QMetaType::UInt || exemplar.typeId() == QMetaType::ULongLong ) {
        bool localOk = false;
        const auto value = text.trimmed().toLongLong( &localOk );
        if ( ok ) {
            *ok = localOk;
        }
        return value;
    }
    if ( exemplar.typeId() == QMetaType::Double || exemplar.typeId() == QMetaType::Float ) {
        bool localOk = false;
        const auto value = text.trimmed().toDouble( &localOk );
        if ( ok ) {
            *ok = localOk;
        }
        return value;
    }
    if ( ok ) {
        *ok = !text.trimmed().isEmpty();
    }
    return text;
}

QVariant parseMultiChoiceValue( const QString& text, bool* ok )
{
    const auto trimmed = text.trimmed();
    bool integerOk = false;
    const auto integer = trimmed.toLongLong( &integerOk, 0 );
    if ( integerOk ) {
        if ( ok ) {
            *ok = true;
        }
        return integer;
    }
    if ( ok ) {
        *ok = !trimmed.isEmpty();
    }
    return trimmed;
}

constexpr auto CustomPreset = "__custom__";

void addPreset( QComboBox* combo, const QString& label, const QString& value )
{
    combo->addItem( label, value );
}

void setPresetValue( QComboBox* combo, const QString& value, QString* customValue )
{
    const QSignalBlocker blocker( combo );
    const auto index = combo->findData( value );
    if ( !value.isEmpty() && index < 0 ) {
        *customValue = value;
        const auto customIndex = combo->findData( QString::fromLatin1( CustomPreset ) );
        combo->setCurrentIndex( customIndex );
        combo->setToolTip( QObject::tr( "Custom value: %1" ).arg( value ) );
    }
    else {
        *customValue = {};
        combo->setCurrentIndex( index >= 0 ? index : 0 );
        combo->setToolTip( {} );
    }
}

QString presetValue( const QComboBox* combo, const QString& customValue )
{
    if ( combo->currentData().toString() == QString::fromLatin1( CustomPreset ) ) {
        return customValue;
    }
    return combo->currentData().toString();
}

QString promptCustomValue( QWidget* parent,
                           const QString& title,
                           const QString& help,
                           const QString& initial,
                           bool* accepted )
{
    QDialog dialog( parent );
    dialog.setWindowTitle( title );
    dialog.setMinimumWidth( 560 );
    auto* layout = new QVBoxLayout( &dialog );
    auto* helpLabel = new QLabel( help, &dialog );
    helpLabel->setWordWrap( true );
    layout->addWidget( helpLabel );
    auto* valueEdit = new QLineEdit( initial, &dialog );
    valueEdit->setAccessibleName( title );
    layout->addWidget( valueEdit );
    auto* buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
    layout->addWidget( buttons );
    QObject::connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
    QObject::connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
    const auto result = dialog.exec();
    if ( accepted ) {
        *accepted = result == QDialog::Accepted;
    }
    return valueEdit->text();
}
} // namespace

ActionParameterDefinitionDialog::ActionParameterDefinitionDialog( QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "Parameter Definition" ) );
    // The editor is intentionally tall enough to show the complete property
    // form and the choices table at once. Keeping the form in the dialog
    // itself avoids nested scroll bars and makes the Add/Edit workflow easier
    // to scan.
    setMinimumSize( 700, 780 );
    resize( 800, 820 );

    nameEdit_ = new QLineEdit( this );
    labelEdit_ = new QLineEdit( this );
    descriptionEdit_ = new QPlainTextEdit( this );
    descriptionEdit_->setMinimumHeight( 55 );
    typeCombo_ = new QComboBox( this );
    const QVector<QPair<QString, ActionParameterType>> types = {
        { tr( "Text" ), ActionParameterType::Text },
        { tr( "Integer" ), ActionParameterType::Integer },
        { tr( "Decimal" ), ActionParameterType::Decimal },
        { tr( "Boolean" ), ActionParameterType::Boolean },
        { tr( "Choice" ), ActionParameterType::Choice },
        { tr( "Multiple choices" ), ActionParameterType::MultiChoice },
        { tr( "Date" ), ActionParameterType::Date },
        { tr( "Time" ), ActionParameterType::Time },
        { tr( "Date/time" ), ActionParameterType::DateTime },
        { tr( "IP address" ), ActionParameterType::IpAddress },
        { tr( "MAC address" ), ActionParameterType::MacAddress },
        { tr( "Phone number" ), ActionParameterType::PhoneNumber },
        { tr( "Hex bytes" ), ActionParameterType::HexBytes },
    };
    for ( const auto& type : types ) {
        typeCombo_->addItem( type.first, actionParameterTypeToString( type.second ) );
    }

    presentationCombo_ = new QComboBox( this );
    presentationCombo_->addItem( tr( "Automatic" ), actionParameterPresentationToString(
                                                          ActionParameterPresentation::Automatic ) );
    presentationCombo_->addItem( tr( "Text box" ), actionParameterPresentationToString(
                                                          ActionParameterPresentation::TextBox ) );
    presentationCombo_->addItem( tr( "Combo box" ), actionParameterPresentationToString(
                                                          ActionParameterPresentation::ComboBox ) );
    presentationCombo_->addItem( tr( "Radio buttons" ), actionParameterPresentationToString(
                                                          ActionParameterPresentation::RadioButtons ) );
    presentationCombo_->addItem( tr( "Check boxes" ), actionParameterPresentationToString(
                                                          ActionParameterPresentation::CheckBoxes ) );

    multiValueModeCombo_ = new QComboBox( this );
    multiValueModeCombo_->addItem( tr( "Comma separated" ), actionMultiValueModeToString(
                                                                ActionMultiValueMode::CommaSeparated ) );
    multiValueModeCombo_->addItem( tr( "Bitwise OR" ), actionMultiValueModeToString(
                                                           ActionMultiValueMode::BitwiseOr ) );
    multiValueModeCombo_->addItem( tr( "Custom separator" ), actionMultiValueModeToString(
                                                                    ActionMultiValueMode::CustomSeparator ) );

    requiredCheck_ = new QCheckBox( tr( "Required" ), this );
    sensitiveCheck_ = new QCheckBox( tr( "Sensitive" ), this );
    rememberCheck_ = new QCheckBox( tr( "Remember last value" ), this );
    rememberCheck_->setChecked( true );
    defaultEdit_ = new QLineEdit( this );
    defaultEdit_->setPlaceholderText( tr( "Optional default value" ) );
    minimumEdit_ = new QLineEdit( this );
    maximumEdit_ = new QLineEdit( this );
    stepEdit_ = new QLineEdit( this );
    validationPatternCombo_ = new QComboBox( this );
    addPreset( validationPatternCombo_, tr( "None (no validation)" ), {} );
    addPreset( validationPatternCombo_, tr( "Any non-empty text" ), QStringLiteral( ".+" ) );
    addPreset( validationPatternCombo_, tr( "Integer (signed)" ), QStringLiteral( "^-?[0-9]+$" ) );
    addPreset( validationPatternCombo_, tr( "Integer (unsigned)" ), QStringLiteral( "^[0-9]+$" ) );
    addPreset( validationPatternCombo_, tr( "Identifier" ), QStringLiteral( "^[A-Za-z_][A-Za-z0-9_]*$" ) );
    addPreset( validationPatternCombo_, tr( "IPv4 address" ),
               QStringLiteral( "^(?:[0-9]{1,3}\\.){3}[0-9]{1,3}$" ) );
    addPreset( validationPatternCombo_, tr( "MAC address" ),
               QStringLiteral( "^(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$" ) );
    addPreset( validationPatternCombo_, tr( "Phone number" ), QStringLiteral( "^\\+?[0-9 ()-]{7,}$" ) );
    addPreset( validationPatternCombo_, tr( "Hex bytes" ),
               QStringLiteral( "^(?:[0-9A-Fa-f]{2})(?:[ :_-]?[0-9A-Fa-f]{2})*$" ) );
    addPreset( validationPatternCombo_, tr( "Custom pattern…" ), QString::fromLatin1( CustomPreset ) );
    validationPatternCombo_->setToolTip(
        tr( "Choose a common validation rule. Custom patterns use Qt regular-expression syntax." ) );

    formatCombo_ = new QComboBox( this );
    addPreset( formatCombo_, tr( "Default" ), {} );
    addPreset( formatCombo_, tr( "ISO date (yyyy-MM-dd)" ), QStringLiteral( "yyyy-MM-dd" ) );
    addPreset( formatCombo_, tr( "ISO time (HH:mm:ss)" ), QStringLiteral( "HH:mm:ss" ) );
    addPreset( formatCombo_, tr( "Short time (HH:mm)" ), QStringLiteral( "HH:mm" ) );
    addPreset( formatCombo_, tr( "ISO date/time" ), QStringLiteral( "yyyy-MM-ddTHH:mm:ss" ) );
    addPreset( formatCombo_, tr( "Custom format…" ), QString::fromLatin1( CustomPreset ) );
    formatCombo_->setToolTip(
        tr( "Date/time formats use Qt tokens, for example yyyy-MM-dd or HH:mm:ss." ) );
    separatorEdit_ = new QLineEdit( this );
    separatorEdit_->setText( QStringLiteral( "," ) );
    expressionCombo_ = new QComboBox( this );
    addPreset( expressionCombo_, tr( "None (use the entered value)" ), {} );
    addPreset( expressionCombo_, tr( "Identity: value" ), QStringLiteral( "value" ) );
    addPreset( expressionCombo_, tr( "Convert to text: str(value)" ), QStringLiteral( "str(value)" ) );
    addPreset( expressionCombo_, tr( "Convert to integer: int(value)" ),
               QStringLiteral( "int(value)" ) );
    addPreset( expressionCombo_, tr( "Convert to decimal: value * 1.0" ),
               QStringLiteral( "value * 1.0" ) );
    addPreset( expressionCombo_, tr( "Format as hex: hex(value, 2)" ),
               QStringLiteral( "hex(value, 2)" ) );
    addPreset( expressionCombo_, tr( "Custom expression…" ), QString::fromLatin1( CustomPreset ) );
    expressionCombo_->setToolTip( tr( "Expressions use the value variable and the action expression functions."
                                       " Choose a preset unless you know the expression syntax." ) );

    choicesTable_ = new QTableWidget( this );
    choicesTable_->setColumnCount( 2 );
    choicesTable_->setHorizontalHeaderLabels( { tr( "Label" ), tr( "Value" ) } );
    choicesTable_->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::Stretch );
    choicesTable_->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Stretch );
    choicesTable_->setSelectionBehavior( QAbstractItemView::SelectRows );
    choicesTable_->setSelectionMode( QAbstractItemView::SingleSelection );
    choicesTable_->setMinimumHeight( 185 );
    auto* choicesButtons = new QHBoxLayout;
    for ( const auto& item : { std::make_pair( tr( "Add" ), 0 ), std::make_pair( tr( "Edit" ), 1 ),
                               std::make_pair( tr( "Delete" ), 2 ), std::make_pair( tr( "Up" ), 3 ),
                               std::make_pair( tr( "Down" ), 4 ) } ) {
        auto* button = new QPushButton( item.first, this );
        choicesButtons->addWidget( button );
        if ( item.second == 0 ) {
            connect( button, &QPushButton::clicked, this, &ActionParameterDefinitionDialog::addChoice );
        }
        else if ( item.second == 1 ) {
            connect( button, &QPushButton::clicked, this, &ActionParameterDefinitionDialog::editChoice );
        }
        else if ( item.second == 2 ) {
            connect( button, &QPushButton::clicked, this, &ActionParameterDefinitionDialog::deleteChoice );
        }
        else {
            connect( button, &QPushButton::clicked, this,
                     [ this, offset = item.second == 3 ? -1 : 1 ] { moveChoice( offset ); } );
        }
    }
    choicesButtons->addStretch();

    auto* form = new QFormLayout;
    form->addRow( tr( "Name" ), nameEdit_ );
    form->addRow( tr( "Label" ), labelEdit_ );
    form->addRow( tr( "Description" ), descriptionEdit_ );
    form->addRow( tr( "Type" ), typeCombo_ );
    form->addRow( tr( "Presentation" ), presentationCombo_ );
    form->addRow( tr( "Multi-value mode" ), multiValueModeCombo_ );
    form->addRow( QString(), requiredCheck_ );
    form->addRow( QString(), sensitiveCheck_ );
    form->addRow( QString(), rememberCheck_ );
    form->addRow( tr( "Default" ), defaultEdit_ );
    form->addRow( tr( "Minimum" ), minimumEdit_ );
    form->addRow( tr( "Maximum" ), maximumEdit_ );
    form->addRow( tr( "Step" ), stepEdit_ );
    form->addRow( tr( "Validation preset" ), validationPatternCombo_ );
    form->addRow( tr( "Format preset" ), formatCombo_ );
    form->addRow( tr( "Separator" ), separatorEdit_ );
    form->addRow( tr( "Value expression" ), expressionCombo_ );
    form->addRow( tr( "Choices" ), choicesTable_ );
    form->addRow( QString(), choicesButtons );

    auto* buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
    connect( buttons, &QDialogButtonBox::accepted, this, &ActionParameterDefinitionDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
    auto* layout = new QVBoxLayout( this );
    layout->addLayout( form, 1 );
    layout->addWidget( buttons );
    connect( typeCombo_, qOverload<int>( &QComboBox::currentIndexChanged ), this,
             [ this ] { updateTypeState(); } );
    connect( presentationCombo_, qOverload<int>( &QComboBox::currentIndexChanged ), this,
             [ this ] { updateTypeState(); } );
    connect( validationPatternCombo_, qOverload<int>( &QComboBox::currentIndexChanged ), this,
             [ this ] { chooseCustomValidationPattern(); } );
    connect( formatCombo_, qOverload<int>( &QComboBox::currentIndexChanged ), this,
             [ this ] { chooseCustomFormat(); } );
    connect( expressionCombo_, qOverload<int>( &QComboBox::currentIndexChanged ), this,
             [ this ] { chooseCustomExpression(); } );
    updateTypeState();
}

void ActionParameterDefinitionDialog::setDefinition( const ActionParameterDefinition& definition )
{
    definition_ = definition;
    nameEdit_->setText( definition.name );
    labelEdit_->setText( definition.label );
    descriptionEdit_->setPlainText( definition.description );
    typeCombo_->setCurrentIndex( typeCombo_->findData( actionParameterTypeToString( definition.type ) ) );
    presentationCombo_->setCurrentIndex(
        presentationCombo_->findData( actionParameterPresentationToString( definition.presentation ) ) );
    multiValueModeCombo_->setCurrentIndex(
        multiValueModeCombo_->findData( actionMultiValueModeToString( definition.multiValueMode ) ) );
    requiredCheck_->setChecked( definition.required );
    sensitiveCheck_->setChecked( definition.sensitive );
    rememberCheck_->setChecked( definition.remember );
    defaultEdit_->setText( variantText( definition.defaultValue ) );
    minimumEdit_->setText( variantText( definition.minimum ) );
    maximumEdit_->setText( variantText( definition.maximum ) );
    stepEdit_->setText( variantText( definition.step ) );
    customValidationPattern_.clear();
    customFormat_.clear();
    customExpression_.clear();
    setPresetValue( validationPatternCombo_, definition.validationPattern, &customValidationPattern_ );
    setPresetValue( formatCombo_, definition.format, &customFormat_ );
    separatorEdit_->setText( definition.separator );
    setPresetValue( expressionCombo_, definition.expression, &customExpression_ );
    choices_ = definition.choices;
    refreshChoices();
    updateTypeState();
}

ActionParameterDefinition ActionParameterDefinitionDialog::definition() const
{
    return definition_;
}

void ActionParameterDefinitionDialog::updateTypeState()
{
    bool typeOk = false;
    const auto type = actionParameterTypeFromString( typeCombo_->currentData().toString(), &typeOk );
    const bool choices = type == ActionParameterType::Choice || type == ActionParameterType::MultiChoice;
    const bool multi = type == ActionParameterType::MultiChoice;
    choicesTable_->setEnabled( choices );
    presentationCombo_->setEnabled( choices || type == ActionParameterType::Text );
    multiValueModeCombo_->setEnabled( multi );
    separatorEdit_->setEnabled( multi
                                && multiValueModeCombo_->currentData().toString()
                                       == QStringLiteral( "custom_separator" ) );
    minimumEdit_->setEnabled( type == ActionParameterType::Integer || type == ActionParameterType::Decimal );
    maximumEdit_->setEnabled( minimumEdit_->isEnabled() );
    stepEdit_->setEnabled( minimumEdit_->isEnabled() );
    validationPatternCombo_->setEnabled( type != ActionParameterType::Boolean
                                         && type != ActionParameterType::Choice
                                         && type != ActionParameterType::MultiChoice );
    formatCombo_->setEnabled( type == ActionParameterType::Date || type == ActionParameterType::Time
                              || type == ActionParameterType::DateTime );
    expressionCombo_->setEnabled( true );
    Q_UNUSED( typeOk );
}

void ActionParameterDefinitionDialog::chooseCustomValidationPattern()
{
    if ( validationPatternCombo_->currentData().toString() != QString::fromLatin1( CustomPreset ) ) {
        return;
    }
    bool ok = false;
    const auto value = promptCustomValue(
        this, tr( "Custom validation pattern" ),
        tr( "Enter a Qt regular expression. Examples: ^[A-Za-z_][A-Za-z0-9_]*$ for an identifier, "
            "^[0-9]+$ for digits, or ^(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$ for a MAC address." ),
        customValidationPattern_, &ok );
    if ( ok && !value.trimmed().isEmpty() ) {
        customValidationPattern_ = value.trimmed();
        validationPatternCombo_->setToolTip( tr( "Custom pattern: %1" ).arg( customValidationPattern_ ) );
        return;
    }
    validationPatternCombo_->setCurrentIndex( 0 );
}

void ActionParameterDefinitionDialog::chooseCustomFormat()
{
    if ( formatCombo_->currentData().toString() != QString::fromLatin1( CustomPreset ) ) {
        return;
    }
    bool ok = false;
    const auto value = promptCustomValue(
        this, tr( "Custom display format" ),
        tr( "This is a Qt date/time format used only for Date, Time, and Date/time parameters. "
            "Tokens include yyyy, MM, dd, HH, mm, ss; example: yyyy-MM-dd HH:mm:ss. "
            "Use single quotes around literal text." ),
        customFormat_, &ok );
    if ( ok && !value.trimmed().isEmpty() ) {
        customFormat_ = value.trimmed();
        formatCombo_->setToolTip( tr( "Custom format: %1" ).arg( customFormat_ ) );
        return;
    }
    formatCombo_->setCurrentIndex( 0 );
}

void ActionParameterDefinitionDialog::chooseCustomExpression()
{
    if ( expressionCombo_->currentData().toString() != QString::fromLatin1( CustomPreset ) ) {
        return;
    }
    bool ok = false;
    const auto value = promptCustomValue(
        this, tr( "Custom value expression" ),
        tr( "Use the variable value and expression operators/functions. Examples: "
            "str(value), int(value), hex(value, 2), upper(value), trim(value), "
            "concat(\"ID=\", value). Available helpers include concat, str, int, hex, "
            "utf8, latin1, hex_bytes, bytes, join, split, replace, upper, lower, trim, len, "
            "abs, clamp, contains, slice, reverse, unique, sort, ipv4_bytes, mac_bytes, "
            "regex_match, regex_replace, and checksums." ),
        customExpression_, &ok );
    if ( ok && !value.trimmed().isEmpty() ) {
        customExpression_ = value.trimmed();
        expressionCombo_->setToolTip( tr( "Custom expression: %1" ).arg( customExpression_ ) );
        return;
    }
    expressionCombo_->setCurrentIndex( 0 );
}

int ActionParameterDefinitionDialog::selectedChoiceRow() const
{
    const auto rows = choicesTable_->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.front().row();
}

void ActionParameterDefinitionDialog::refreshChoices()
{
    choicesTable_->setRowCount( static_cast<int>( choices_.size() ) );
    for ( int row = 0; row < choices_.size(); ++row ) {
        choicesTable_->setItem( row, 0, new QTableWidgetItem( choices_.at( row ).label ) );
        choicesTable_->setItem( row, 1, new QTableWidgetItem( choices_.at( row ).value.toString() ) );
    }
}

void ActionParameterDefinitionDialog::addChoice()
{
    QDialog dialog( this );
    dialog.setWindowTitle( tr( "Choice" ) );
    auto* label = new QLineEdit( &dialog );
    auto* value = new QLineEdit( &dialog );
    auto* form = new QFormLayout( &dialog );
    form->addRow( tr( "Label" ), label );
    form->addRow( tr( "Value" ), value );
    auto* buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
    form->addWidget( buttons );
    connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
    if ( dialog.exec() != QDialog::Accepted || label->text().trimmed().isEmpty() ) {
        return;
    }
    if ( value->text().trimmed().isEmpty() ) {
        QMessageBox::warning( this, tr( "Choice" ), tr( "Choice value is required." ) );
        return;
    }
    bool ok = false;
    const auto selectedType = actionParameterTypeFromString( typeCombo_->currentData().toString() );
    auto choiceValue = selectedType == ActionParameterType::MultiChoice
                           ? parseMultiChoiceValue( value->text(), &ok )
                           : parseScalar( value->text(), selectedType, &ok );
    if ( !ok ) {
        QMessageBox::warning( this, tr( "Choice" ), tr( "Choice value has an invalid format." ) );
        return;
    }
    choices_.push_back( { label->text().trimmed(), choiceValue } );
    refreshChoices();
    LOG_DEBUG << "Added action parameter choice";
}

void ActionParameterDefinitionDialog::editChoice()
{
    const auto row = selectedChoiceRow();
    if ( row < 0 || row >= choices_.size() ) {
        return;
    }
    const auto existing = choices_.at( row );
    QDialog dialog( this );
    dialog.setWindowTitle( tr( "Choice" ) );
    auto* label = new QLineEdit( existing.label, &dialog );
    auto* value = new QLineEdit( existing.value.toString(), &dialog );
    auto* form = new QFormLayout( &dialog );
    form->addRow( tr( "Label" ), label );
    form->addRow( tr( "Value" ), value );
    auto* buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
    form->addWidget( buttons );
    connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
    if ( dialog.exec() != QDialog::Accepted || label->text().trimmed().isEmpty() ) {
        return;
    }
    bool ok = false;
    const auto choiceValue = parseChoiceText( value->text(), existing.value, &ok );
    if ( !ok ) {
        QMessageBox::warning( this, tr( "Choice" ), tr( "Choice value has an invalid format." ) );
        return;
    }
    choices_[row] = { label->text().trimmed(), choiceValue };
    refreshChoices();
    choicesTable_->selectRow( row );
}

void ActionParameterDefinitionDialog::deleteChoice()
{
    const auto row = selectedChoiceRow();
    if ( row >= 0 && row < choices_.size() ) {
        choices_.removeAt( row );
        refreshChoices();
        LOG_DEBUG << "Deleted action parameter choice";
    }
}

void ActionParameterDefinitionDialog::moveChoice( int offset )
{
    const auto row = selectedChoiceRow();
    const auto target = row + offset;
    if ( row < 0 || target < 0 || target >= choices_.size() ) {
        return;
    }
    choices_.swapItemsAt( row, target );
    refreshChoices();
    choicesTable_->selectRow( target );
}

QVariant ActionParameterDefinitionDialog::parseDefaultValue( bool* ok, QString* errorMessage ) const
{
    const auto type = actionParameterTypeFromString( typeCombo_->currentData().toString() );
    auto value = parseScalar( defaultEdit_->text(), type, ok );
    if ( *ok && type == ActionParameterType::Choice ) {
        for ( const auto& choice : choices_ ) {
            if ( choice.label == defaultEdit_->text().trimmed()
                 || choice.value.toString() == defaultEdit_->text().trimmed() ) {
                value = choice.value;
                break;
            }
        }
    }
    else if ( *ok && type == ActionParameterType::MultiChoice ) {
        QVariantList normalized;
        for ( const auto& item : value.toList() ) {
            auto matched = item;
            for ( const auto& choice : choices_ ) {
                if ( choice.label == item.toString() || choice.value.toString() == item.toString() ) {
                    matched = choice.value;
                    break;
                }
            }
            normalized.push_back( matched );
        }
        value = normalized;
    }
    if ( ok && !*ok && errorMessage ) {
        *errorMessage = tr( "Default value is invalid for the selected type." );
    }
    return value;
}

void ActionParameterDefinitionDialog::accept()
{
    const auto name = nameEdit_->text().trimmed();
    if ( name.isEmpty() ) {
        QMessageBox::warning( this, tr( "Parameter Definition" ), tr( "Parameter name is required." ) );
        return;
    }
    bool typeOk = false;
    const auto type = actionParameterTypeFromString( typeCombo_->currentData().toString(), &typeOk );
    if ( !typeOk ) {
        QMessageBox::warning( this, tr( "Parameter Definition" ), tr( "Parameter type is invalid." ) );
        return;
    }
    bool defaultOk = false;
    QString defaultError;
    const auto defaultValue = parseDefaultValue( &defaultOk, &defaultError );
    if ( !defaultOk ) {
        QMessageBox::warning( this, tr( "Parameter Definition" ), defaultError );
        return;
    }
    if ( ( type == ActionParameterType::Choice || type == ActionParameterType::MultiChoice )
         && choices_.isEmpty() ) {
        QMessageBox::warning( this, tr( "Parameter Definition" ),
                              tr( "Choice parameters require at least one choice." ) );
        return;
    }

    const auto parseConstraint = [this, type]( QLineEdit* edit, const QString& label,
                                               bool* ok ) -> QVariant {
        if ( edit->text().trimmed().isEmpty() ) {
            *ok = true;
            return {};
        }
        const auto value = parseScalar( edit->text(), type, ok );
        if ( !*ok ) {
            QMessageBox::warning( this, tr( "Parameter Definition" ),
                                  tr( "%1 is invalid for the selected type." ).arg( label ) );
        }
        return value;
    };
    bool minimumOk = false;
    bool maximumOk = false;
    bool stepOk = false;
    const auto minimum = parseConstraint( minimumEdit_, tr( "Minimum" ), &minimumOk );
    if ( !minimumOk ) {
        return;
    }
    const auto maximum = parseConstraint( maximumEdit_, tr( "Maximum" ), &maximumOk );
    if ( !maximumOk ) {
        return;
    }
    const auto step = parseConstraint( stepEdit_, tr( "Step" ), &stepOk );
    if ( !stepOk ) {
        return;
    }
    if ( minimum.isValid() && maximum.isValid() && minimum.toDouble() > maximum.toDouble() ) {
        QMessageBox::warning( this, tr( "Parameter Definition" ),
                              tr( "Minimum must not be greater than maximum." ) );
        return;
    }
    if ( step.isValid() && step.toDouble() <= 0.0 ) {
        QMessageBox::warning( this, tr( "Parameter Definition" ),
                              tr( "Step must be greater than zero." ) );
        return;
    }

    definition_.name = name;
    definition_.label = labelEdit_->text().trimmed().isEmpty() ? name : labelEdit_->text().trimmed();
    definition_.description = descriptionEdit_->toPlainText().trimmed();
    definition_.type = type;
    definition_.presentation = actionParameterPresentationFromString(
        presentationCombo_->currentData().toString() );
    definition_.multiValueMode = actionMultiValueModeFromString( multiValueModeCombo_->currentData().toString() );
    definition_.required = requiredCheck_->isChecked();
    definition_.sensitive = sensitiveCheck_->isChecked();
    definition_.remember = rememberCheck_->isChecked();
    definition_.defaultValue = defaultValue;
    const bool numericType = type == ActionParameterType::Integer || type == ActionParameterType::Decimal;
    definition_.minimum = numericType ? minimum : QVariant{};
    definition_.maximum = numericType ? maximum : QVariant{};
    definition_.step = numericType ? step : QVariant{};
    definition_.validationPattern = presetValue( validationPatternCombo_, customValidationPattern_ );
    definition_.format = presetValue( formatCombo_, customFormat_ );
    definition_.separator = separatorEdit_->text();
    definition_.expression = presetValue( expressionCombo_, customExpression_ );
    definition_.choices = choices_;
    LOG_DEBUG << "Accepted structured action parameter definition " << name.toStdString();
    QDialog::accept();
}
