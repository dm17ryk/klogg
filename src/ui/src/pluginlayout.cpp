// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginlayout.h"

#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

PluginBusyOverlay::PluginBusyOverlay( QWidget* container, QWidget* covered,
                                      const QString& objectName )
    : QWidget( container )
    , covered_( covered )
{
    setObjectName( objectName );
    setAttribute( Qt::WA_NoMousePropagation );
    setAutoFillBackground( false );
    // Takes the keyboard focus that leaves the disabled contents.
    setFocusPolicy( Qt::StrongFocus );
    setAccessibleName( tr( "Busy" ) );
    auto* layout = new QVBoxLayout( this );
    layout->addStretch( 1 );
    auto* spinner = new QProgressBar( this );
    spinner->setObjectName( objectName + "_indicator" );
    spinner->setRange( 0, 0 ); // a busy indicator
    spinner->setTextVisible( false );
    spinner->setMaximumWidth( 160 );
    layout->addWidget( spinner, 0, Qt::AlignHCenter );
    text_ = new QLabel( this );
    text_->setObjectName( objectName + "_text" );
    text_->setTextFormat( Qt::PlainText );
    text_->setWordWrap( true );
    text_->setAlignment( Qt::AlignCenter );
    text_->setVisible( false );
    layout->addWidget( text_ );
    cancel_ = new QPushButton( tr( "Cancel" ), this );
    cancel_->setObjectName( objectName + "_cancel" );
    cancel_->setVisible( false );
    layout->addWidget( cancel_, 0, Qt::AlignHCenter );
    layout->addStretch( 1 );
    connect( cancel_, &QPushButton::clicked, this, [ this ] {
        cancel_->setEnabled( false );
        cancel_->setText( tr( "Cancelling…" ) );
        Q_EMIT cancelRequested();
    } );
    if ( covered_ ) {
        covered_->installEventFilter( this );
    }
    follow();
    raise();
}

void PluginBusyOverlay::setText( const QString& text )
{
    text_->setText( text );
    text_->setVisible( !text.isEmpty() );
    setAccessibleDescription( text );
}

void PluginBusyOverlay::setCancellable( bool cancellable )
{
    cancel_->setVisible( cancellable );
}

void PluginBusyOverlay::resetCancel()
{
    cancel_->setEnabled( true );
    cancel_->setText( tr( "Cancel" ) );
}

void PluginBusyOverlay::follow()
{
    if ( covered_ ) {
        setGeometry( covered_->geometry() );
    }
}

void PluginBusyOverlay::showEvent( QShowEvent* event )
{
    follow();
    raise();
    QWidget::showEvent( event );
}

bool PluginBusyOverlay::eventFilter( QObject* watched, QEvent* event )
{
    if ( watched == covered_
         && ( event->type() == QEvent::Move || event->type() == QEvent::Resize ) ) {
        follow();
    }
    return QWidget::eventFilter( watched, event );
}

void PluginBusyOverlay::paintEvent( QPaintEvent* )
{
    QPainter painter( this );
    auto background = palette().color( QPalette::Window );
    background.setAlpha( 200 );
    painter.fillRect( rect(), background );
}
