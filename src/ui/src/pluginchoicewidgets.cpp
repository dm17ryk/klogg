// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginchoicewidgets.h"

#include "plugintablemodel.h"
#include "pluginview.h"
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QStyleOption>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int ChoiceValueRole = Qt::UserRole;
const char* const ChoiceValueProperty = "choiceValue";

QJsonValue limitValue( QJsonValue value )
{
    if ( value.isString() ) {
        value = value.toString().left( PluginView::MaxFieldValueChars );
    }
    return value;
}

// A button text is shown as written: `&` is no mnemonic.
QString buttonText( QString text )
{
    return text.replace( QLatin1Char( '&' ), QStringLiteral( "&&" ) );
}

QString translate( const char* text, int count = -1 )
{
    return QCoreApplication::translate( "PluginView", text, nullptr, count );
}
} // namespace

namespace pluginchoices {
QList<Choice> parse( const QJsonArray& choices, int maximum )
{
    QList<Choice> result;
    for ( const auto& item : choices ) {
        if ( result.size() >= maximum ) {
            break;
        }
        Choice choice;
        if ( item.isObject() ) {
            const auto object = item.toObject();
            const auto type = object.value( "type" ).toString();
            if ( type == QLatin1String( "separator" ) ) {
                choice.type = Choice::Type::Separator;
                result.append( choice );
                continue;
            }
            if ( type == QLatin1String( "section" ) ) {
                choice.type = Choice::Type::Section;
                choice.label = pluginview::text( object, "label" );
                result.append( choice );
                continue;
            }
            choice.value = limitValue( object.value( "value" ) );
            choice.label = object.value( "label" ).isString() ? pluginview::text( object, "label" )
                                                              : pluginview::cellText( choice.value );
            choice.icon = object.value( "icon" );
            choice.tooltip = pluginview::text( object, "tooltip" );
            choice.enabled = object.value( "enabled" ).toBool( true );
        }
        else {
            choice.value = limitValue( item );
            choice.label = pluginview::cellText( choice.value );
        }
        result.append( choice );
    }
    return result;
}

void fillCombo( QComboBox* combo, const QList<Choice>& choices, const IconResolver& icons,
                bool radio )
{
    const auto current = combo->currentData();
    // An editable choice's value is its text: new choices never replace what the user typed.
    const auto typed = combo->isEditable() ? combo->currentText() : QString{};
    const QSignalBlocker blocker( combo );
    combo->clear();
    auto* model = qobject_cast<QStandardItemModel*>( combo->model() );
    for ( const auto& choice : choices ) {
        if ( choice.type == Choice::Type::Separator ) {
            combo->insertSeparator( combo->count() );
            continue;
        }
        if ( choice.type == Choice::Type::Section ) {
            combo->addItem( choice.label );
            if ( auto* item = model ? model->item( combo->count() - 1 ) : nullptr ) {
                item->setFlags( Qt::NoItemFlags );
                auto font = item->font();
                font.setBold( true );
                item->setFont( font );
            }
            continue;
        }
        const auto icon = !choice.icon.isUndefined() && icons ? icons( choice.icon ) : QIcon{};
        combo->addItem( icon, choice.label, choice.value.toVariant() );
        auto* item = model ? model->item( combo->count() - 1 ) : nullptr;
        if ( item != nullptr ) {
            if ( !choice.tooltip.isEmpty() ) {
                item->setToolTip( pluginview::plainToolTip( choice.tooltip ) );
            }
            if ( !choice.enabled ) {
                item->setFlags( item->flags() & ~( Qt::ItemIsEnabled | Qt::ItemIsSelectable ) );
            }
        }
    }
    if ( radio && dynamic_cast<RadioIndicatorDelegate*>( combo->itemDelegate() ) == nullptr ) {
        combo->setItemDelegate( new RadioIndicatorDelegate( combo ) );
    }
    if ( !typed.isEmpty() ) {
        const auto found = findLabel( combo, typed );
        combo->setCurrentIndex( found );
        if ( found < 0 ) {
            combo->setEditText( typed );
        }
        return;
    }
    // The previous value, else the first choice that can be chosen.
    auto index = current.isValid() ? combo->findData( current ) : -1;
    for ( int row = 0; index < 0 && row < combo->count(); ++row ) {
        const auto* item = model ? model->item( row ) : nullptr;
        if ( combo->itemData( row ).isValid() && ( !item || item->isEnabled() ) ) {
            index = row;
        }
    }
    combo->setCurrentIndex( index );
}

int findLabel( const QComboBox* combo, const QString& text )
{
    // A disabled entry cannot be chosen, so typing its label stays literal text.
    const auto* model = qobject_cast<const QStandardItemModel*>( combo->model() );
    for ( int row = 0; row < combo->count(); ++row ) {
        const auto* item = model ? model->item( row ) : nullptr;
        if ( combo->itemData( row ).isValid() && combo->itemText( row ) == text
             && ( !item || item->isEnabled() ) ) {
            return row;
        }
    }
    return -1;
}

void makeEditable( QComboBox* combo )
{
    combo->setEditable( true );
    combo->setInsertPolicy( QComboBox::NoInsert );
    auto* completer = new QCompleter( combo->model(), combo );
    completer->setCompletionMode( QCompleter::PopupCompletion );
    completer->setFilterMode( Qt::MatchContains );
    completer->setCaseSensitivity( Qt::CaseInsensitive );
    combo->setCompleter( completer );
}

void makeSearchable( QComboBox* combo )
{
    // fillCombo gives every choice its value as item data; separators and sections have none.
    new PopupFilter(
        combo, []( const QModelIndex& index ) { return index.data( ChoiceValueRole ).isValid(); },
        false );
}

PopupFilter::PopupFilter( QComboBox* combo, IsChoice isChoice, bool spaceToggles )
    : QObject( combo )
    , combo_( combo )
    , isChoice_( std::move( isChoice ) )
    , spaceToggles_( spaceToggles )
{
    auto* view = combo->view();
    view->installEventFilter( this );
    auto* container = view->parentWidget();
    if ( container == nullptr ) {
        return;
    }
    container->installEventFilter( this );
    // Qt lays the open list out in a box; the filter goes above it.
    if ( auto* box = qobject_cast<QBoxLayout*>( container->layout() ) ) {
        label_ = new QLabel( container );
        label_->setTextFormat( Qt::PlainText );
        label_->setContentsMargins( 4, 2, 4, 2 );
        label_->hide();
        box->insertWidget( 0, label_ );
    }
}

void PopupFilter::setText( const QString& text )
{
    filter_ = text.left( MaxChars );
    apply();
}

bool PopupFilter::eventFilter( QObject* watched, QEvent* event )
{
    if ( !combo_ ) {
        return false;
    }
    if ( watched == combo_->view() && event->type() == QEvent::KeyPress ) {
        const auto* key = static_cast<QKeyEvent*>( event );
        if ( key->key() == Qt::Key_Backspace && !filter_.isEmpty() ) {
            filter_.chop( 1 );
            apply();
            return true;
        }
        const auto typed = key->text();
        const bool shortcut = ( key->modifiers()
                                & ( Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier ) )
                              != 0;
        if ( !shortcut && !typed.isEmpty() && typed.at( 0 ).isPrint()
             && !( spaceToggles_ && typed == QLatin1String( " " ) ) ) {
            setText( filter_ + typed );
            return true;
        }
    }
    else if ( watched == combo_->view()->parentWidget() && event->type() == QEvent::Hide
              && !filter_.isEmpty() ) {
        setText( {} );
    }
    return false;
}

void PopupFilter::apply()
{
    auto* list = qobject_cast<QListView*>( combo_->view() );
    const auto* model = combo_->model();
    if ( list == nullptr || model == nullptr ) {
        return;
    }
    const auto column = combo_->modelColumn();
    int firstShown = -1;
    for ( int row = 0; row < model->rowCount(); ++row ) {
        const auto index = model->index( row, column );
        const bool choice = isChoice_ && isChoice_( index );
        const bool shown
            = filter_.isEmpty()
              || ( choice && index.data().toString().contains( filter_, Qt::CaseInsensitive ) );
        list->setRowHidden( row, !shown );
        if ( shown && choice && firstShown < 0 && model->flags( index ).testFlag( Qt::ItemIsEnabled ) ) {
            firstShown = row;
        }
    }
    if ( label_ ) {
        label_->setObjectName( combo_->objectName() + QStringLiteral( "_filter" ) );
        label_->setText( translate( "Filter: %1" ).arg( filter_ ) );
        label_->setVisible( !filter_.isEmpty() );
    }
    // Keyboard selection stays on a shown choice.
    const auto current = list->currentIndex();
    if ( ( !current.isValid() || list->isRowHidden( current.row() ) ) && firstShown >= 0 ) {
        list->setCurrentIndex( model->index( firstShown, column ) );
    }
}

RadioIndicatorDelegate::RadioIndicatorDelegate( QComboBox* combo )
    : QStyledItemDelegate( combo )
    , combo_( combo )
{
}

void RadioIndicatorDelegate::paint( QPainter* painter, const QStyleOptionViewItem& option,
                                    const QModelIndex& index ) const
{
    if ( index.data( Qt::AccessibleDescriptionRole ).toString() == QLatin1String( "separator" ) ) {
        const auto y = option.rect.center().y();
        painter->save();
        painter->setPen( option.palette.color( QPalette::Mid ) );
        painter->drawLine( option.rect.left() + 4, y, option.rect.right() - 4, y );
        painter->restore();
        return;
    }
    auto* style = combo_ ? combo_->style() : nullptr;
    QStyleOptionViewItem item( option );
    if ( style != nullptr && index.data( ChoiceValueRole ).isValid() ) {
        const auto size = style->pixelMetric( QStyle::PM_ExclusiveIndicatorWidth );
        QStyleOptionButton radio;
        radio.rect = QRect( option.rect.left() + 4, option.rect.center().y() - size / 2, size, size );
        radio.state = ( index.flags().testFlag( Qt::ItemIsEnabled ) ? QStyle::State_Enabled
                                                                    : QStyle::State_None )
                      | ( combo_->currentIndex() == index.row() ? QStyle::State_On
                                                                : QStyle::State_Off );
        style->drawPrimitive( QStyle::PE_IndicatorRadioButton, &radio, painter, combo_ );
        item.rect.adjust( size + 8, 0, 0, 0 );
    }
    QStyledItemDelegate::paint( painter, item, index );
}

QSize RadioIndicatorDelegate::sizeHint( const QStyleOptionViewItem& option,
                                        const QModelIndex& index ) const
{
    if ( index.data( Qt::AccessibleDescriptionRole ).toString() == QLatin1String( "separator" ) ) {
        return { option.rect.width(), 5 };
    }
    auto size = QStyledItemDelegate::sizeHint( option, index );
    if ( combo_ ) {
        size.rwidth() += combo_->style()->pixelMetric( QStyle::PM_ExclusiveIndicatorWidth ) + 8;
    }
    return size;
}
} // namespace pluginchoices

ChoiceGroup::ChoiceGroup( bool exclusive, const QString& objectBase, QWidget* parent )
    : QWidget( parent )
    , exclusive_( exclusive )
    , objectBase_( objectBase )
{
    auto* layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    frame_ = new QGroupBox( this );
    frame_->setFlat( true );
    layout->addWidget( frame_ );
    auto* inner = new QVBoxLayout( frame_ );
    inner->setContentsMargins( 0, 0, 0, 0 );
    auto* host = new QWidget( frame_ );
    grid_ = new QGridLayout( host );
    grid_->setContentsMargins( 0, 0, 0, 0 );
    inner->addWidget( host );
    links_ = new QWidget( frame_ );
    auto* linkLayout = new QHBoxLayout( links_ );
    linkLayout->setContentsMargins( 0, 0, 0, 0 );
    for ( const auto checked : { true, false } ) {
        auto* link = new QPushButton( checked ? tr( "All" ) : tr( "None" ), links_ );
        link->setFlat( true );
        link->setObjectName( objectBase_ + ( checked ? "_all" : "_none" ) );
        link->setAccessibleName( checked ? tr( "Check all" ) : tr( "Uncheck all" ) );
        linkLayout->addWidget( link );
        connect( link, &QPushButton::clicked, this, [ this, checked ] {
            setting_ = true;
            for ( auto* button : buttons_ ) {
                if ( button->isEnabled() && !button->isHidden() ) {
                    button->setChecked( checked );
                }
            }
            setting_ = false;
            Q_EMIT changed();
        } );
    }
    linkLayout->addStretch( 1 );
    links_->hide();
    inner->addWidget( links_ );
    group_ = new QButtonGroup( this );
    group_->setExclusive( exclusive );
    // qOverload: before Qt 5.15 buttonToggled also has an int overload.
    connect( group_, qOverload<QAbstractButton*, bool>( &QButtonGroup::buttonToggled ), this,
             [ this ]( QAbstractButton*, bool checked ) {
        // A radio change toggles two buttons: report the one that became checked.
        if ( !setting_ && ( !exclusive_ || checked ) ) {
            Q_EMIT changed();
        }
    } );
}

void ChoiceGroup::setTitle( const QString& title )
{
    frame_->setTitle( title );
    frame_->setFlat( title.isEmpty() );
    // The All and None links of several groups must not share one accessible name.
    for ( const auto checked : { true, false } ) {
        if ( auto* link = links_->findChild<QPushButton*>( objectBase_
                                                          + ( checked ? "_all" : "_none" ) ) ) {
            const auto action = checked ? tr( "Check all" ) : tr( "Uncheck all" );
            link->setAccessibleName( title.isEmpty() ? action : action + ": " + title );
        }
    }
}

void ChoiceGroup::setChoices( const QList<pluginchoices::Choice>& choices,
                              const pluginchoices::IconResolver& icons )
{
    const auto current = value();
    for ( auto* button : buttons_ ) {
        group_->removeButton( button );
    }
    // Later: a patch may arrive while one of these buttons is still emitting.
    for ( auto* item : items_ ) {
        item->hide();
        item->deleteLater();
    }
    items_.clear();
    buttons_.clear();
    int index = 0;
    for ( const auto& choice : choices ) {
        if ( choice.type == pluginchoices::Choice::Type::Separator ) {
            auto* line = new QFrame( this );
            line->setFrameShape( QFrame::HLine );
            line->setFrameShadow( QFrame::Sunken );
            items_.append( line );
            continue;
        }
        if ( choice.type == pluginchoices::Choice::Type::Section ) {
            auto* title = new QLabel( choice.label, this );
            title->setTextFormat( Qt::PlainText );
            auto font = title->font();
            font.setBold( true );
            title->setFont( font );
            items_.append( title );
            continue;
        }
        QAbstractButton* button = exclusive_ ? static_cast<QAbstractButton*>( new QRadioButton( this ) )
                                             : new QCheckBox( this );
        button->setText( buttonText( choice.label ) );
        button->setObjectName( QStringLiteral( "%1_choice_%2" ).arg( objectBase_ ).arg( index++ ) );
        button->setProperty( ChoiceValueProperty, choice.value.toVariant() );
        button->setEnabled( choice.enabled );
        if ( !choice.icon.isUndefined() && icons ) {
            button->setIcon( icons( choice.icon ) );
        }
        if ( !choice.tooltip.isEmpty() ) {
            button->setToolTip( pluginview::plainToolTip( choice.tooltip ) );
        }
        group_->addButton( button );
        buttons_.append( button );
        items_.append( button );
    }
    relayout();
    setValue( current );
    filter( filter_ ); // the search line still applies to the new buttons
}

void ChoiceGroup::setShape( const QString& orientation, int columns )
{
    orientation_ = orientation == QLatin1String( "horizontal" ) ? orientation
                                                                : QStringLiteral( "vertical" );
    columns_ = qBound( 0, columns, 12 );
    relayout();
}

void ChoiceGroup::setAllNone( bool shown )
{
    links_->setVisible( shown && !exclusive_ );
}

void ChoiceGroup::relayout()
{
    while ( grid_->count() > 0 ) {
        delete grid_->takeAt( 0 ); // the layout item; the widget stays
    }
    const auto columns = columns_ > 0 ? columns_
                         : orientation_ == QLatin1String( "horizontal" )
                             ? static_cast<int>( qMax<qsizetype>( 1, buttons_.size() ) )
                             : 1;
    int row = 0;
    int column = 0;
    for ( auto* item : items_ ) {
        if ( qobject_cast<QAbstractButton*>( item ) == nullptr ) {
            // Separators and section titles take a row of their own.
            if ( column > 0 ) {
                ++row;
                column = 0;
            }
            grid_->addWidget( item, row++, 0, 1, columns );
            continue;
        }
        grid_->addWidget( item, row, column );
        if ( ++column >= columns ) {
            column = 0;
            ++row;
        }
    }
}

void ChoiceGroup::filter( const QString& text )
{
    filter_ = text;
    for ( auto* button : buttons_ ) {
        button->setVisible( text.isEmpty()
                            || button->text().replace( QStringLiteral( "&&" ), QStringLiteral( "&" ) )
                                   .contains( text, Qt::CaseInsensitive ) );
    }
}

QJsonValue ChoiceGroup::value() const
{
    if ( exclusive_ ) {
        for ( const auto* button : buttons_ ) {
            if ( button->isChecked() ) {
                return QJsonValue::fromVariant( button->property( ChoiceValueProperty ) );
            }
        }
        return QJsonValue::Null;
    }
    QJsonArray values;
    for ( const auto* button : buttons_ ) {
        if ( button->isChecked() ) {
            values.append( QJsonValue::fromVariant( button->property( ChoiceValueProperty ) ) );
        }
    }
    return values;
}

void ChoiceGroup::setValue( const QJsonValue& value )
{
    setting_ = true;
    const auto wanted = value.isArray() ? value.toArray() : QJsonArray{ value };
    // An exclusive group cannot uncheck its last button: lift that while setting.
    group_->setExclusive( false );
    for ( auto* button : buttons_ ) {
        const auto own = QJsonValue::fromVariant( button->property( ChoiceValueProperty ) );
        button->setChecked( exclusive_ ? own == value : wanted.contains( limitValue( own ) ) );
    }
    group_->setExclusive( exclusive_ );
    setting_ = false;
}

FieldCompleter::FieldCompleter( QLineEdit* edit, Provider provider,
                                pluginchoices::IconResolver icons )
    : QObject( edit )
    , edit_( edit )
    , provider_( std::move( provider ) )
    , icons_( std::move( icons ) )
{
    model_ = new QStandardItemModel( this );
    completer_ = new QCompleter( model_, this );
    // The plugin filters: the list shows its answer as it is, labels shown, values inserted.
    completer_->setCompletionMode( QCompleter::UnfilteredPopupCompletion );
    completer_->setCompletionRole( ChoiceValueRole );
    completer_->setWidget( edit );
    connect( completer_, qOverload<const QModelIndex&>( &QCompleter::activated ), this,
             [ this ]( const QModelIndex& index ) {
                 if ( edit_ ) {
                     edit_->setText( index.data( ChoiceValueRole ).toString() );
                 }
             } );
    debounce_ = new QTimer( this );
    debounce_->setSingleShot( true );
    debounce_->setInterval( DebounceMs );
    connect( debounce_, &QTimer::timeout, this, &FieldCompleter::ask );
    connect( edit, &QLineEdit::textEdited, this, [ this ] {
        // An answer still in flight is for the text before this edit: drop it now, not only at
        // the next ask() after the debounce.
        ++generation_;
        debounce_->start();
    } );
}

QStringList FieldCompleter::offered() const
{
    QStringList labels;
    for ( int row = 0; row < model_->rowCount(); ++row ) {
        labels.append( model_->item( row )->text() );
    }
    return labels;
}

void FieldCompleter::ask()
{
    if ( !edit_ || !provider_ ) {
        return;
    }
    const auto generation = ++generation_;
    const QPointer<FieldCompleter> self( this );
    provider_( edit_->text(), edit_->cursorPosition(), [ self, generation ]( const QJsonArray& items ) {
        // Only the latest answer, for a field that still exists.
        if ( !self || generation != self->generation_ || !self->edit_ ) {
            return;
        }
        self->model_->clear();
        for ( const auto& value : items ) {
            if ( self->model_->rowCount() >= MaxItems ) {
                break;
            }
            const auto entry = value.toObject();
            const auto itemValue = entry.value( "value" ).toString().left( PluginView::MaxFieldValueChars );
            auto* item = new QStandardItem(
                entry.value( "label" ).toString( itemValue ).left( PluginView::MaxTextChars ) );
            item->setData( itemValue, ChoiceValueRole );
            if ( entry.contains( "icon" ) && self->icons_ ) {
                item->setIcon( self->icons_( entry.value( "icon" ) ) );
            }
            self->model_->appendRow( item );
        }
        if ( self->model_->rowCount() > 0 && self->edit_->hasFocus() ) {
            self->completer_->complete();
        }
        else if ( self->completer_->popup() ) {
            self->completer_->popup()->hide();
        }
    } );
}

namespace pluginfields {
namespace {
// A length rule: a whole number of at least 0, else -1 (no rule).
int lengthRule( const QJsonObject& props, const char* key )
{
    const auto value = props.value( QLatin1String( key ) );
    return value.isDouble() && value.toDouble() >= 0 ? value.toInt( -1 ) : -1;
}

// The anchored expression of a pattern, compiled once: rules are checked on every change.
QRegularExpression compiled( const QString& pattern )
{
    static QHash<QString, QRegularExpression> cache;
    auto found = cache.constFind( pattern );
    if ( found == cache.constEnd() ) {
        if ( cache.size() >= 64 ) {
            cache.clear();
        }
        QRegularExpression expression( QRegularExpression::anchoredPattern( pattern ) );
        expression.optimize();
        found = cache.insert( pattern, expression );
    }
    return *found;
}

QString number( double value )
{
    return QString::number( value, 'g', 15 ); // the same text on Qt 5 and Qt 6
}
} // namespace

bool validates( const QJsonObject& props )
{
    // By value: a later root that drops a rule leaves its neutral default (required false, an
    // empty pattern or error), which is no rule.
    return props.value( "required" ).toBool() || !props.value( "pattern" ).toString().isEmpty()
           || props.value( "min" ).isDouble() || props.value( "max" ).isDouble()
           || lengthRule( props, "min_length" ) >= 0 || lengthRule( props, "max_length" ) >= 0
           || !props.value( "error" ).toString().isEmpty();
}

QString validationError( const QJsonObject& props, const QJsonValue& value )
{
    const auto error = props.value( "error" ).toString();
    if ( !error.isEmpty() ) {
        return error.left( PluginView::MaxTextChars ); // the plugin's own message
    }
    const bool empty = value.isNull() || value.isUndefined()
                       || ( value.isString() && value.toString().isEmpty() )
                       || ( value.isArray() && value.toArray().isEmpty() );
    if ( empty ) {
        return props.value( "required" ).toBool() ? translate( "Required" ) : QString{};
    }
    if ( value.isString() ) {
        const auto text = value.toString();
        const auto length = static_cast<int>( text.size() );
        const auto shortest = lengthRule( props, "min_length" );
        if ( shortest >= 0 && length < shortest ) {
            return translate( "At least %n character(s)", shortest );
        }
        const auto longest = lengthRule( props, "max_length" );
        if ( longest >= 0 && length > longest ) {
            return translate( "At most %n character(s)", longest );
        }
        // A pattern runs on the GUI thread: at most 256 characters, matched against at most
        // MaxPatternText characters (a longer value is left to the plugin).
        const auto pattern = props.value( "pattern" ).toString();
        if ( !pattern.isEmpty() && pattern.size() <= MaxPatternChars
             && text.size() <= MaxPatternText ) {
            const auto expression = compiled( pattern );
            if ( expression.isValid() && !expression.match( text ).hasMatch() ) {
                return translate( "Not in the expected form" );
            }
        }
    }
    if ( value.isDouble() ) {
        const auto given = value.toDouble();
        if ( props.value( "min" ).isDouble() && given < props.value( "min" ).toDouble() ) {
            return translate( "At least %1" ).arg( number( props.value( "min" ).toDouble() ) );
        }
        if ( props.value( "max" ).isDouble() && given > props.value( "max" ).toDouble() ) {
            return translate( "At most %1" ).arg( number( props.value( "max" ).toDouble() ) );
        }
    }
    return {};
}
} // namespace pluginfields
