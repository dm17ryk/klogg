// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "checkcombobox.h"

#include <QAbstractItemView>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStandardItemModel>
#include <QStylePainter>
#include <QTimer>

namespace {
constexpr int MaxChoices = 512;
constexpr int ValueRole = Qt::UserRole + 1;
} // namespace

CheckComboBox::CheckComboBox( QWidget* parent )
    : QComboBox( parent )
    , items_( new QStandardItemModel( this ) )
{
    setModel( items_ );
    view()->viewport()->installEventFilter( this );
    view()->installEventFilter( this );
    connect( items_, &QStandardItemModel::itemChanged, this, [ this ] {
        update();
        Q_EMIT checkedChanged();
    } );
}

void CheckComboBox::setChoices( const QJsonArray& choices )
{
    const auto checked = checkedValues();
    QSignalBlocker block( items_ );
    items_->clear();
    for ( const auto& choice : choices ) {
        if ( items_->rowCount() >= MaxChoices ) {
            break;
        }
        const auto object = choice.toObject();
        const auto value = choice.isObject() ? object.value( "value" ) : choice;
        auto* item = new QStandardItem(
            object.contains( "label" ) ? object.value( "label" ).toString() : value.toVariant().toString() );
        item->setData( value.toVariant(), ValueRole );
        item->setFlags( Qt::ItemIsEnabled | Qt::ItemIsUserCheckable );
        item->setData( checked.contains( value ) ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole );
        items_->appendRow( item );
    }
    update();
}

void CheckComboBox::setCheckedValues( const QJsonArray& values )
{
    {
        QSignalBlocker block( items_ );
        for ( int row = 0; row < items_->rowCount(); ++row ) {
            auto* item = items_->item( row );
            const auto value = QJsonValue::fromVariant( item->data( ValueRole ) );
            item->setCheckState( values.contains( value ) ? Qt::Checked : Qt::Unchecked );
        }
    }
    update();
    Q_EMIT checkedChanged();
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

void CheckComboBox::showPopup()
{
    openedWith_ = checkedValues();
    cancelled_ = false;
    QComboBox::showPopup();
}

void CheckComboBox::hidePopup()
{
    if ( keepOpen_ ) {
        keepOpen_ = false;
        return; // a click toggled an item: stay open
    }
    QComboBox::hidePopup();
    if ( cancelled_ ) {
        setCheckedValues( openedWith_ );
    }
    Q_EMIT popupClosed( !cancelled_ );
}

bool CheckComboBox::eventFilter( QObject* watched, QEvent* event )
{
    if ( watched == view()->viewport() && event->type() == QEvent::MouseButtonRelease ) {
        const auto index = view()->indexAt( static_cast<QMouseEvent*>( event )->position().toPoint() );
        if ( auto* item = items_->itemFromIndex( index ) ) {
            item->setCheckState( item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked );
            // Swallow the release, and the hidePopup() the combo's own handler may still call for it.
            // The flag clears on the next event-loop turn, so a later outside click still closes the popup.
            keepOpen_ = true;
            QTimer::singleShot( 0, this, [ this ] { keepOpen_ = false; } );
            return true;
        }
    }
    if ( watched == view() && event->type() == QEvent::KeyPress
         && static_cast<QKeyEvent*>( event )->key() == Qt::Key_Escape ) {
        cancelled_ = true;
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
