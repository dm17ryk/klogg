// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "checkcombobox.h"
#include "pluginview.h" // the plugin view limits (pluginview.h does not include this header)

#include <QAbstractItemView>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QStylePainter>
#include <QTimer>

#include <utility>

namespace {
constexpr int ValueRole = Qt::UserRole + 1;

// A string value is capped like a `choice` field's value.
QJsonValue limitValue( const QJsonValue& value )
{
    return value.isString() ? QJsonValue( value.toString().left( PluginView::MaxFieldValueChars ) )
                            : value;
}
} // namespace

CheckComboBox::CheckComboBox( QWidget* parent )
    : QComboBox( parent )
    , items_( new QStandardItemModel( this ) )
{
    setModel( items_ );
    // The combo's default (menu-style) delegate does not paint CheckStateRole under Fusion / Windows 11.
    view()->setItemDelegate( new QStyledItemDelegate( view() ) );
    view()->viewport()->installEventFilter( this );
    view()->installEventFilter( this );
    view()->window()->installEventFilter( this ); // the popup container: its Hide is the one close signal
    connect( items_, &QStandardItemModel::itemChanged, this, [ this ] {
        if ( populating_ ) {
            return;
        }
        update();
        Q_EMIT checkedChanged();
    } );
}

void CheckComboBox::setChoices( const QJsonArray& choices )
{
    const auto checked = checkedValues();
    populating_ = true;
    items_->clear();
    for ( const auto& choice : choices ) {
        if ( items_->rowCount() >= PluginView::MaxElements ) {
            break;
        }
        const auto object = choice.toObject();
        const auto value = limitValue( choice.isObject() ? object.value( "value" ) : choice );
        const auto label = object.value( "label" );
        // Labels are plain text of at most MaxTextChars, as for a `choice` field.
        auto* item = new QStandardItem(
            ( label.isString() ? label.toString() : value.toVariant().toString() )
                .left( PluginView::MaxTextChars ) );
        item->setData( value.toVariant(), ValueRole );
        item->setFlags( Qt::ItemIsEnabled | Qt::ItemIsUserCheckable );
        item->setData( checked.contains( value ) ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole );
        items_->appendRow( item );
    }
    populating_ = false;
    update();
}

void CheckComboBox::setCheckedValues( const QJsonArray& values )
{
    QJsonArray wanted;
    for ( const auto& value : values ) {
        wanted.append( limitValue( value ) );
    }
    const auto before = checkedValues();
    populating_ = true;
    for ( int row = 0; row < items_->rowCount(); ++row ) {
        auto* item = items_->item( row );
        const auto value = QJsonValue::fromVariant( item->data( ValueRole ) );
        item->setCheckState( wanted.contains( value ) ? Qt::Checked : Qt::Unchecked );
    }
    populating_ = false;
    update();
    // Both lists are in choice order, so equal lists are equal sets.
    if ( checkedValues() != before ) {
        Q_EMIT checkedChanged();
    }
}

QJsonArray CheckComboBox::checkedValues() const
{
    QJsonArray values;
    for ( int row = 0; row < items_->rowCount(); ++row ) {
        const auto* item = items_->item( row );
        if ( item->checkState() == Qt::Checked ) {
            values.append( QJsonValue::fromVariant( item->data( ValueRole ) ) );
        }
    }
    return values;
}

QString CheckComboBox::summary() const
{
    QStringList labels;
    for ( int row = 0; row < items_->rowCount(); ++row ) {
        if ( items_->item( row )->checkState() == Qt::Checked ) {
            labels.append( items_->item( row )->text() );
        }
    }
    if ( labels.isEmpty() ) {
        return tr( "None" );
    }
    return labels.size() == items_->rowCount() && labels.size() > 1 ? tr( "All" ) : labels.join( ", " );
}

void CheckComboBox::toggle( QStandardItem* item )
{
    item->setCheckState( item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked );
}

void CheckComboBox::showPopup()
{
    openedWith_ = checkedValues();
    cancelled_ = false;
    pressedInView_ = false;
    QComboBox::showPopup();
    open_ = view()->window()->isVisible();
}

void CheckComboBox::hidePopup()
{
    if ( keepOpen_ ) {
        keepOpen_ = false;
        return; // a click toggled an item: stay open
    }
    QComboBox::hidePopup(); // the container's Hide event reports the close
}

// The single place popupClosed is emitted: once per real close, however the popup was dismissed
// (Esc, Enter, hidePopup(), or a click outside, where Qt hides the container without calling hidePopup()).
void CheckComboBox::finishClose()
{
    if ( !open_ ) {
        return;
    }
    open_ = false;
    pressedInView_ = false;
    const bool accepted = !cancelled_;
    if ( cancelled_ ) {
        cancelled_ = false;
        setCheckedValues( openedWith_ );
    }
    Q_EMIT popupClosed( accepted );
}

bool CheckComboBox::eventFilter( QObject* watched, QEvent* event )
{
    if ( watched == view()->window() && event->type() == QEvent::Hide ) {
        finishClose();
    }
    else if ( watched == view()->viewport() && open_ ) {
        if ( event->type() == QEvent::MouseButtonPress ) {
            pressedInView_ = true;
        }
        else if ( event->type() == QEvent::MouseButtonRelease ) {
            // Only a release that follows a press inside the popup toggles: the release of the click
            // that opened the popup must not.
            const bool pressed = std::exchange( pressedInView_, false );
            const auto index = view()->indexAt( static_cast<QMouseEvent*>( event )->position().toPoint() );
            if ( pressed ) {
                if ( auto* item = items_->itemFromIndex( index ) ) {
                    toggle( item );
                    // Swallow the release, and the hidePopup() the combo's own handler may still call for it.
                    // The flag clears on the next event-loop turn, so a later outside click still closes the popup.
                    keepOpen_ = true;
                    QTimer::singleShot( 0, this, [ this ] { keepOpen_ = false; } );
                }
            }
            return true;
        }
    }
    else if ( watched == view() && event->type() == QEvent::KeyPress ) {
        const auto key = static_cast<QKeyEvent*>( event )->key();
        if ( key == Qt::Key_Escape ) {
            cancelled_ = true;
        }
        else if ( key == Qt::Key_Space ) {
            if ( auto* item = items_->itemFromIndex( view()->currentIndex() ) ) {
                toggle( item );
            }
            return true;
        }
    }
    return QComboBox::eventFilter( watched, event );
}

void CheckComboBox::paintEvent( QPaintEvent* )
{
    QStylePainter painter( this );
    QStyleOptionComboBox option;
    initStyleOption( &option );
    option.currentText = summary();
    painter.drawComplexControl( QStyle::CC_ComboBox, option );
    painter.drawControl( QStyle::CE_ComboBoxLabel, option );
}
