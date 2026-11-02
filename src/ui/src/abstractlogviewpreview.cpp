// SPDX-License-Identifier: GPL-3.0-or-later
#include "abstractlogview.h"
#include "previewmanager.h"
#include "previewservice.h"
#include <QCursor>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QProcess>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>

LineNumber AbstractLogView::previewPhysicalLine( LineNumber viewRow ) const
{
    // AbstractLogData exposes editor-style, one-based line numbers.
    return LineNumber{ logData_->getLineNumber( viewRow ).get() - 1 };
}

void AbstractLogView::clearLinePreview()
{
    ++linePreviewSerial_;
    linePreviewActive_ = false;
    linePreviewValid_ = {};
    if ( linePreviewTimer_ )
        linePreviewTimer_->stop();
    PreviewService::cancel( linePreviewProcess_ );
    linePreviewProcess_.clear();
    QToolTip::hideText();
}

bool AbstractLogView::linePreviewCurrent() const
{
    if ( !linePreviewActive_ || !isVisible() || !window()->isActiveWindow() || charHeight_ <= 0 )
        return false;
    const auto position = viewport()->mapFromGlobal( QCursor::pos() );
    if ( !viewport()->rect().contains( position ) || position.x() < leftMarginPx_
         || position.y() < drawingTopOffset_ )
        return false;
    const auto row = convertCoordToLine( position.y() );
    return row && row->get() < logData_->getNbLine().get()
           && previewPhysicalLine( *row ).get() == linePreviewPhysical_
           && ( !linePreviewValid_ || linePreviewValid_() );
}

void AbstractLogView::trackLinePreview( quint64 serial, QProcess* process,
                                        std::function<bool()> valid )
{
    if ( serial != linePreviewSerial_ || !linePreviewActive_ ) {
        PreviewService::cancel( process );
        return;
    }
    linePreviewProcess_ = process;
    linePreviewValid_ = std::move( valid );
}

void AbstractLogView::presentLinePreview( quint64 serial, const QVariantMap& result )
{
    if ( serial != linePreviewSerial_ )
        return;
    if ( !linePreviewCurrent() ) {
        clearLinePreview();
        return;
    }
    const auto html = PreviewService::tooltipHtml( result );
    if ( html.isEmpty() ) {
        clearLinePreview();
        return;
    }
    QToolTip::showText( linePreviewPosition_, html, viewport(), viewport()->rect() );
}

bool AbstractLogView::viewportEvent( QEvent* event )
{
    if ( event->type() == QEvent::ToolTip ) {
        auto* help = static_cast<QHelpEvent*>( event );
        if ( logData_->getNbLine().get() == 0 || charHeight_ <= 0 ) {
            clearLinePreview();
            event->ignore();
            return true;
        }
        const auto row = convertCoordToLine( help->pos().y() );
        clearLinePreview();
        if ( help->pos().x() < leftMarginPx_ || help->pos().y() < drawingTopOffset_ || !row
             || row->get() >= logData_->getNbLine().get() ) {
            event->ignore();
            return true;
        }
        if ( !linePreviewConnectionsReady_ ) {
            linePreviewConnectionsReady_ = true;
            connect( &PreviewManager::instance(), &PreviewManager::previewsChanged, this,
                     &AbstractLogView::clearLinePreview );
            connect( verticalScrollBar(), &QScrollBar::valueChanged, this,
                     &AbstractLogView::clearLinePreview );
            connect( horizontalScrollBar(), &QScrollBar::valueChanged, this,
                     &AbstractLogView::clearLinePreview );
            linePreviewTimer_ = new QTimer( this );
            linePreviewTimer_->setObjectName( "linePreviewValidityTimer" );
            linePreviewTimer_->setInterval( 100 );
            connect( linePreviewTimer_, &QTimer::timeout, this, [ this ] {
                if ( !linePreviewCurrent() )
                    clearLinePreview();
            } );
        }
        linePreviewActive_ = true;
        linePreviewPhysical_ = previewPhysicalLine( *row ).get();
        linePreviewPosition_ = help->globalPos();
        linePreviewTimer_->start();
        Q_EMIT linePreviewRequested( linePreviewPhysical_, linePreviewPosition_,
                                     linePreviewSerial_ );
        event->accept();
        return true;
    }
    if ( linePreviewActive_ ) {
        if ( event->type() == QEvent::Leave || event->type() == QEvent::Hide
             || event->type() == QEvent::Resize || event->type() == QEvent::Wheel
             || event->type() == QEvent::MouseButtonPress )
            clearLinePreview();
        else if ( event->type() == QEvent::MouseMove ) {
            const auto* mouse = static_cast<QMouseEvent*>( event );
            const auto row = convertCoordToLine( mouse->pos().y() );
            if ( !row || row->get() >= logData_->getNbLine().get()
                 || previewPhysicalLine( *row ).get() != linePreviewPhysical_ )
                clearLinePreview();
        }
    }
    return QAbstractScrollArea::viewportEvent( event );
}
