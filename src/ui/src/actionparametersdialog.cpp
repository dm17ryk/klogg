#include "actionparametersdialog.h"

#include <algorithm>
#include <limits>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSpinBox>
#include <QTimeEdit>
#include <QVBoxLayout>

#include "log.h"

namespace {
QString historyKey( int actionId )
{
    return QStringLiteral( "advancedActions/history/%1" ).arg( actionId );
}

QDate parameterDate( const QVariant& value )
{
    return value.typeId() == QMetaType::QDate ? value.toDate()
                                               : QDate::fromString( value.toString(), Qt::ISODate );
}

QTime parameterTime( const QVariant& value )
{
    return value.typeId() == QMetaType::QTime ? value.toTime()
                                               : QTime::fromString( value.toString(), Qt::ISODate );
}

QDateTime parameterDateTime( const QVariant& value )
{
    return value.typeId() == QMetaType::QDateTime
               ? value.toDateTime()
               : QDateTime::fromString( value.toString(), Qt::ISODate );
}

QVariantMap jsonMap( const QByteArray& json )
{
    const auto document = QJsonDocument::fromJson( json );
    return document.isObject() ? document.object().toVariantMap() : QVariantMap{};
}

QVariant initialValue( const ActionParameterDefinition& field, const QVariantMap& remembered )
{
    if ( field.remember && remembered.contains( field.name ) ) {
        return remembered.value( field.name );
    }
    return field.defaultValue;
}

bool variantsEqual( const QVariant& left, const QVariant& right )
{
    return left == right || left.toString() == right.toString();
}
} // namespace

ActionParametersDialog::ActionParametersDialog( const ActionDefinition& action,
                                                QWidget* parent,
                                                bool embedded )
    : QDialog( parent )
    , action_( action )
    , embedded_( embedded )
{
    setObjectName( QStringLiteral( "actionParametersDialog" ) );
    if ( embedded_ ) {
        setWindowFlags( Qt::Widget );
        setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
    }
    else {
        setWindowTitle( tr( "Send %1" ).arg( action.name ) );
        setMinimumWidth( 420 );
    }

    auto* outerLayout = new QVBoxLayout( this );
    auto* parameterHint = new QLabel(
        tr( "Choose the parameter values to send. The action sequence is generated from these "
            "values when you press Send." ),
        this );
    parameterHint->setWordWrap( true );
    parameterHint->setObjectName( QStringLiteral( "actionParameterHint" ) );
    outerLayout->addWidget( parameterHint );
    if ( !action.description.trimmed().isEmpty() ) {
        auto* description = new QLabel( action.description, this );
        description->setWordWrap( true );
        outerLayout->addWidget( description );
    }

    const auto recent = recentValues();
    if ( !recent.isEmpty() ) {
        recentCombo_ = new QComboBox( this );
        recentCombo_->setObjectName( QStringLiteral( "actionParameterHistoryCombo" ) );
        recentCombo_->setAccessibleName( tr( "Recent parameter sets" ) );
        recentCombo_->addItem( tr( "Current values" ), -1 );
        for ( qsizetype index = 0; index < recent.size(); ++index ) {
            recentCombo_->addItem( tr( "Recent %1" ).arg( index + 1 ), index );
        }
        auto* historyLayout = new QFormLayout;
        historyLayout->addRow( tr( "History" ), recentCombo_ );
        outerLayout->addLayout( historyLayout );
    }

    auto* formLayout = new QFormLayout;
    const auto remembered = rememberedValues();
    for ( const auto& field : action.parameters.fields ) {
        const auto initial = initialValue( field, remembered );
        const auto label = field.label.trimmed().isEmpty() ? field.name : field.label;
        QWidget* editor = nullptr;
        EditorBinding binding;
        binding.field = field;

        const bool isMulti = field.type == ActionParameterType::MultiChoice;
        const bool useRadio = field.type == ActionParameterType::Choice
                              && field.presentation == ActionParameterPresentation::RadioButtons;
        const bool useCombo = field.type == ActionParameterType::Choice && !useRadio;
        if ( isMulti ) {
            auto* group = new QGroupBox( this );
            group->setFlat( true );
            auto* choicesLayout = new QVBoxLayout( group );
            QVector<QPair<QCheckBox*, QVariant>> choices;
            const auto selected = initial.toList();
            const auto initialMask = initial.toULongLong();
            const bool useInitialMask = field.multiValueMode == ActionMultiValueMode::BitwiseOr
                                        && !initial.canConvert<QVariantList>();
            for ( const auto& choice : field.choices ) {
                auto* check = new QCheckBox( choice.label, group );
                check->setObjectName( QStringLiteral( "actionParameter_%1_%2" )
                                          .arg( field.name, choice.label ) );
                const auto choiceMask = choice.value.toULongLong();
                const bool selectedByMask = useInitialMask && choiceMask != 0
                                            && ( initialMask & choiceMask ) == choiceMask;
                check->setChecked( selectedByMask
                                   || std::any_of( selected.cbegin(), selected.cend(),
                                                   [ &choice ]( const QVariant& value ) {
                    return variantsEqual( value, choice.value );
                } ) );
                choicesLayout->addWidget( check );
                choices.push_back( { check, choice.value } );
            }
            binding.read = [ choices ] {
                QVariantList values;
                for ( const auto& choice : choices ) {
                    if ( choice.first->isChecked() ) {
                        values.push_back( choice.second );
                    }
                }
                return QVariant{ values };
            };
            binding.write = [ choices ]( const QVariant& value ) {
                const auto selectedValues = value.toList();
                for ( const auto& choice : choices ) {
                    choice.first->setChecked( std::any_of(
                        selectedValues.cbegin(), selectedValues.cend(),
                        [ &choice ]( const QVariant& selectedValue ) {
                            return variantsEqual( selectedValue, choice.second );
                        } ) );
                }
            };
            editor = group;
        }
        else if ( useRadio ) {
            auto* group = new QGroupBox( this );
            group->setFlat( true );
            auto* choicesLayout = new QVBoxLayout( group );
            auto* buttons = new QButtonGroup( group );
            QVariantList choiceValues;
            QRadioButton* noneRadio = nullptr;
            for ( qsizetype index = 0; index < field.choices.size(); ++index ) {
                const auto& choice = field.choices.at( index );
                auto* radio = new QRadioButton( choice.label, group );
                radio->setObjectName( QStringLiteral( "actionParameter_%1_%2" )
                                          .arg( field.name, choice.label ) );
                buttons->addButton( radio, static_cast<int>( index ) );
                choicesLayout->addWidget( radio );
                choiceValues.push_back( choice.value );
                if ( variantsEqual( initial, choice.value ) ) {
                    radio->setChecked( true );
                }
            }
            binding.read = [ buttons, choiceValues ] {
                const auto index = buttons->checkedId();
                return index >= 0 && index < choiceValues.size() ? choiceValues.at( index )
                                                                 : QVariant{};
            };
            binding.write = [ buttons, noneRadio, choiceValues ]( const QVariant& value ) {
                if ( !value.isValid() || value.isNull() ) {
                    if ( noneRadio != nullptr ) {
                        noneRadio->setChecked( true );
                    }
                    return;
                }
                for ( qsizetype index = 0; index < choiceValues.size(); ++index ) {
                    if ( variantsEqual( value, choiceValues.at( index ) ) ) {
                        buttons->button( static_cast<int>( index ) )->setChecked( true );
                        return;
                    }
                }
                buttons->setExclusive( false );
                if ( auto* checked = buttons->checkedButton() ) {
                    checked->setChecked( false );
                }
                buttons->setExclusive( true );
            };
            editor = group;
        }
        else if ( useCombo ) {
            auto* combo = new QComboBox( this );
            if ( !field.required ) {
                combo->addItem( tr( "(none)" ), QVariant{} );
            }
            for ( const auto& choice : field.choices ) {
                combo->addItem( choice.label, choice.value );
            }
            const auto selectedIndex = combo->findData( initial );
            combo->setCurrentIndex( selectedIndex >= 0 ? selectedIndex : 0 );
            binding.read = [ combo ] { return combo->currentData(); };
            binding.write = [ combo ]( const QVariant& value ) {
                const auto index = combo->findData( value );
                combo->setCurrentIndex( index >= 0 ? index : 0 );
            };
            editor = combo;
        }
        else if ( field.type == ActionParameterType::Boolean ) {
            auto* check = new QCheckBox( this );
            check->setChecked( initial.toBool() );
            binding.read = [ check ] { return QVariant{ check->isChecked() }; };
            binding.write = [ check ]( const QVariant& value ) { check->setChecked( value.toBool() ); };
            editor = check;
        }
        else if ( field.type == ActionParameterType::Integer ) {
            auto* lineEdit = new QLineEdit( this );
            lineEdit->setPlaceholderText( tr( "Integer value (64-bit)" ) );
            lineEdit->setValidator( new QRegularExpressionValidator(
                QRegularExpression( QStringLiteral( "^-?[0-9]*$" ) ), lineEdit ) );
            if ( initial.isValid() && !initial.isNull() ) {
                lineEdit->setText( QString::number( initial.toLongLong() ) );
            }
            auto* none = new QCheckBox( tr( "(none)" ), this );
            const bool initiallyUnset = !field.required && ( !initial.isValid() || initial.isNull() );
            none->setChecked( initiallyUnset );
            none->setVisible( !field.required );
            lineEdit->setEnabled( !initiallyUnset );
            connect( none, &QCheckBox::toggled, lineEdit, &QWidget::setDisabled );
            auto* wrapper = new QWidget( this );
            auto* wrapperLayout = new QHBoxLayout( wrapper );
            wrapperLayout->setContentsMargins( 0, 0, 0, 0 );
            wrapperLayout->addWidget( none );
            wrapperLayout->addWidget( lineEdit, 1 );
            binding.read = [ lineEdit, none ] {
                if ( none->isChecked() || lineEdit->text().trimmed().isEmpty() ) {
                    return QVariant{};
                }
                bool ok = false;
                const auto value = lineEdit->text().trimmed().toLongLong( &ok );
                return ok ? QVariant{ value } : QVariant{};
            };
            binding.write = [ lineEdit, none ]( const QVariant& value ) {
                if ( !value.isValid() || value.isNull() ) {
                    none->setChecked( true );
                    return;
                }
                none->setChecked( false );
                lineEdit->setText( QString::number( value.toLongLong() ) );
            };
            editor = wrapper;
        }
        else if ( field.type == ActionParameterType::Decimal ) {
            auto* spin = new QDoubleSpinBox( this );
            spin->setDecimals( 8 );
            spin->setRange( field.minimum.isValid() ? field.minimum.toDouble() : -1.0e100,
                            field.maximum.isValid() ? field.maximum.toDouble() : 1.0e100 );
            spin->setSingleStep( field.step.isValid() ? field.step.toDouble() : 1.0 );
            const bool initiallyUnset = !field.required && ( !initial.isValid() || initial.isNull() );
            spin->setValue( initiallyUnset ? 0.0 : initial.toDouble() );
            auto* none = new QCheckBox( tr( "(none)" ), this );
            none->setChecked( initiallyUnset );
            none->setVisible( !field.required );
            spin->setEnabled( !initiallyUnset );
            connect( none, &QCheckBox::toggled, spin, &QWidget::setDisabled );
            auto* wrapper = new QWidget( this );
            auto* wrapperLayout = new QHBoxLayout( wrapper );
            wrapperLayout->setContentsMargins( 0, 0, 0, 0 );
            wrapperLayout->addWidget( none );
            wrapperLayout->addWidget( spin, 1 );
            binding.read = [ spin, none ] { return none->isChecked() ? QVariant{} : QVariant{ spin->value() }; };
            binding.write = [ spin, none ]( const QVariant& value ) {
                if ( !value.isValid() || value.isNull() ) {
                    none->setChecked( true );
                    return;
                }
                none->setChecked( false );
                spin->setValue( value.toDouble() );
            };
            editor = wrapper;
        }
        else if ( field.type == ActionParameterType::Date ) {
            auto* date = new QDateEdit( this );
            date->setCalendarPopup( true );
            const auto initialDate = parameterDate( initial );
            const bool initiallyUnset = !field.required && !initialDate.isValid();
            date->setDate( initialDate.isValid() ? initialDate : QDate::currentDate() );
            auto* none = new QCheckBox( tr( "(none)" ), this );
            none->setChecked( initiallyUnset );
            none->setVisible( !field.required );
            date->setEnabled( !initiallyUnset );
            connect( none, &QCheckBox::toggled, date, &QWidget::setDisabled );
            auto* wrapper = new QWidget( this );
            auto* wrapperLayout = new QHBoxLayout( wrapper );
            wrapperLayout->setContentsMargins( 0, 0, 0, 0 );
            wrapperLayout->addWidget( none );
            wrapperLayout->addWidget( date, 1 );
            binding.read = [ date, none ] { return none->isChecked() ? QVariant{} : QVariant{ date->date() }; };
            binding.write = [ date, none ]( const QVariant& value ) {
                const auto parsed = parameterDate( value );
                if ( parsed.isValid() ) {
                    none->setChecked( false );
                    date->setDate( parsed );
                }
                else {
                    none->setChecked( true );
                }
            };
            editor = wrapper;
        }
        else if ( field.type == ActionParameterType::Time ) {
            auto* time = new QTimeEdit( this );
            const auto initialTime = parameterTime( initial );
            const bool initiallyUnset = !field.required && !initialTime.isValid();
            time->setTime( initialTime.isValid() ? initialTime : QTime::currentTime() );
            auto* none = new QCheckBox( tr( "(none)" ), this );
            none->setChecked( initiallyUnset );
            none->setVisible( !field.required );
            time->setEnabled( !initiallyUnset );
            connect( none, &QCheckBox::toggled, time, &QWidget::setDisabled );
            auto* wrapper = new QWidget( this );
            auto* wrapperLayout = new QHBoxLayout( wrapper );
            wrapperLayout->setContentsMargins( 0, 0, 0, 0 );
            wrapperLayout->addWidget( none );
            wrapperLayout->addWidget( time, 1 );
            binding.read = [ time, none ] { return none->isChecked() ? QVariant{} : QVariant{ time->time() }; };
            binding.write = [ time, none ]( const QVariant& value ) {
                const auto parsed = parameterTime( value );
                if ( parsed.isValid() ) {
                    none->setChecked( false );
                    time->setTime( parsed );
                }
                else {
                    none->setChecked( true );
                }
            };
            editor = wrapper;
        }
        else if ( field.type == ActionParameterType::DateTime ) {
            auto* dateTime = new QDateTimeEdit( this );
            dateTime->setCalendarPopup( true );
            const auto initialDateTime = parameterDateTime( initial );
            const bool initiallyUnset = !field.required && !initialDateTime.isValid();
            dateTime->setDateTime( initialDateTime.isValid() ? initialDateTime
                                                              : QDateTime::currentDateTime() );
            auto* none = new QCheckBox( tr( "(none)" ), this );
            none->setChecked( initiallyUnset );
            none->setVisible( !field.required );
            dateTime->setEnabled( !initiallyUnset );
            connect( none, &QCheckBox::toggled, dateTime, &QWidget::setDisabled );
            auto* wrapper = new QWidget( this );
            auto* wrapperLayout = new QHBoxLayout( wrapper );
            wrapperLayout->setContentsMargins( 0, 0, 0, 0 );
            wrapperLayout->addWidget( none );
            wrapperLayout->addWidget( dateTime, 1 );
            binding.read = [ dateTime, none ] {
                return none->isChecked() ? QVariant{} : QVariant{ dateTime->dateTime() };
            };
            binding.write = [ dateTime, none ]( const QVariant& value ) {
                const auto parsed = parameterDateTime( value );
                if ( parsed.isValid() ) {
                    none->setChecked( false );
                    dateTime->setDateTime( parsed );
                }
                else {
                    none->setChecked( true );
                }
            };
            editor = wrapper;
        }
        else {
            auto* lineEdit = new QLineEdit( this );
            lineEdit->setText( initial.toString() );
            lineEdit->setPlaceholderText( field.format );
            if ( field.sensitive ) {
                lineEdit->setEchoMode( QLineEdit::Password );
            }
            binding.read = [ lineEdit ] { return QVariant{ lineEdit->text() }; };
            binding.write = [ lineEdit ]( const QVariant& value ) {
                lineEdit->setText( value.toString() );
            };
            editor = lineEdit;
        }

        editor->setObjectName( QStringLiteral( "actionParameter_%1" ).arg( field.name ) );
        editor->setAccessibleName( label );
        editor->setToolTip( field.description );
        formLayout->addRow( field.required ? label + QStringLiteral( " *" ) : label, editor );
        editors_.push_back( std::move( binding ) );
    }
    outerLayout->addLayout( formLayout );

    if ( recentCombo_ ) {
        connect( recentCombo_, &QComboBox::currentIndexChanged, this, [ this, recent ] {
            const auto recentIndex = recentCombo_->currentData().toInt();
            if ( recentIndex >= 0 && recentIndex < recent.size() ) {
                LOG_DEBUG << "Applying remembered parameter tuple for action id=" << action_.id
                          << ", history_index=" << recentIndex;
                applyValues( recent.at( recentIndex ) );
            }
        } );
    }

    if ( !embedded_ ) {
        auto* buttons
            = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
        buttons->button( QDialogButtonBox::Ok )->setText( tr( "Send" ) );
        connect( buttons, &QDialogButtonBox::accepted, this, &ActionParametersDialog::accept );
        connect( buttons, &QDialogButtonBox::rejected, this, &ActionParametersDialog::reject );
        outerLayout->addWidget( buttons );
    }
}

QVariantMap ActionParametersDialog::values() const
{
    return acceptedValues_;
}

QVariantMap ActionParametersDialog::currentValues() const
{
    QVariantMap current;
    for ( const auto& editor : editors_ ) {
        const auto value = editor.read();
        if ( value.isValid() && !value.isNull() ) {
            current.insert( editor.field.name, value );
        }
    }
    return current;
}

void ActionParametersDialog::rememberCurrentValues()
{
    const auto current = currentValues();
    saveRememberedValues( current );
    LOG_DEBUG << "Remembered values from embedded parameter editor for action id=" << action_.id
              << ", parameter_count=" << current.size();
}

bool ActionParametersDialog::validateCurrentValues( QString* errorMessage ) const
{
    QStringList missing;
    const auto validation = actionDefinitionToBytesWithParameters( action_, currentValues(), &missing );
    if ( !validation.ok && errorMessage != nullptr ) {
        *errorMessage = validation.error;
    }
    return validation.ok;
}

void ActionParametersDialog::accept()
{
    const auto current = currentValues();

    QString validationError;
    if ( !validateCurrentValues( &validationError ) ) {
        QStringList missing;
        actionDefinitionToBytesWithParameters( action_, current, &missing );
        LOG_WARNING << "Parameter dialog rejected action id=" << action_.id
                    << ", missing_count=" << missing.size();
        QMessageBox::warning( this, tr( "Send action" ), validationError );
        return;
    }

    acceptedValues_ = current;
    saveRememberedValues( current );
    LOG_DEBUG << "Parameter dialog accepted action id=" << action_.id
              << ", parameter_count=" << current.size();
    QDialog::accept();
}

QVariantMap ActionParametersDialog::rememberedValues() const
{
    QSettings settings;
    return jsonMap( settings.value( historyKey( action_.id ) + QStringLiteral( "/last" ) )
                        .toByteArray() );
}

QVector<QVariantMap> ActionParametersDialog::recentValues() const
{
    QSettings settings;
    QVector<QVariantMap> recent;
    const auto entries
        = settings.value( historyKey( action_.id ) + QStringLiteral( "/recent" ) ).toStringList();
    for ( const auto& entry : entries ) {
        const auto values = jsonMap( entry.toUtf8() );
        if ( !values.isEmpty() ) {
            recent.push_back( values );
        }
    }
    return recent;
}

void ActionParametersDialog::applyValues( const QVariantMap& values )
{
    for ( const auto& editor : editors_ ) {
        if ( values.contains( editor.field.name ) ) {
            editor.write( values.value( editor.field.name ) );
        }
    }
}

void ActionParametersDialog::saveRememberedValues( const QVariantMap& values )
{
    QVariantMap remembered;
    bool hasRememberableField = false;
    for ( const auto& editor : editors_ ) {
        if ( editor.field.remember && !editor.field.sensitive ) {
            hasRememberableField = true;
        }
        if ( editor.field.remember && !editor.field.sensitive && values.contains( editor.field.name ) ) {
            remembered.insert( editor.field.name, values.value( editor.field.name ) );
        }
    }
    if ( remembered.isEmpty() && !hasRememberableField ) {
        LOG_DEBUG << "No non-sensitive remembered parameters for action id=" << action_.id;
        return;
    }

    QSettings settings;
    const auto key = historyKey( action_.id );
    if ( remembered.isEmpty() ) {
        settings.remove( key + QStringLiteral( "/last" ) );
        settings.remove( key + QStringLiteral( "/recent" ) );
        LOG_DEBUG << "Cleared remembered parameter history for action id=" << action_.id;
        return;
    }

    const auto encoded = QJsonDocument::fromVariant( remembered ).toJson( QJsonDocument::Compact );
    settings.setValue( key + QStringLiteral( "/last" ), encoded );
    auto recent = settings.value( key + QStringLiteral( "/recent" ) ).toStringList();
    const auto encodedText = QString::fromUtf8( encoded );
    recent.removeAll( encodedText );
    recent.prepend( encodedText );
    while ( recent.size() > 10 ) {
        recent.removeLast();
    }
    settings.setValue( key + QStringLiteral( "/recent" ), recent );
    LOG_DEBUG << "Saved remembered parameter tuple for action id=" << action_.id
              << ", remembered_count=" << remembered.size();
}
