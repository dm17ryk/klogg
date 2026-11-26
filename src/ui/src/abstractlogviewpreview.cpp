// SPDX-License-Identifier: GPL-3.0-or-later
#include "abstractlogview.h"
#include "previewmanager.h"
#include "previewservice.h"
#include <QCursor>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QProcess>
#include <QTimer>
#include <QToolTip>
#include <algorithm>

namespace {
// A view's tooltip stays until the mouse moves to another line or column, leaves the view, or
// the user scrolls, clicks or types; Qt's default (10 s plus the text length) is only the backstop.
constexpr int ViewToolTipDisplayMs = 10 * 60 * 1000;
} // namespace

LineNumber AbstractLogView::previewPhysicalLine( LineNumber viewRow ) const
{
    // AbstractLogData exposes editor-style, one-based line numbers.
    return LineNumber{ logData_->getLineNumber( viewRow ).get() - 1 };
}

void AbstractLogView::showViewToolTip( const QPoint& globalPosition, const QString& text,
                                       const QRect& area )
{
    QToolTip::showText( globalPosition, text, viewport(), area, ViewToolTipDisplayMs );
    shownToolTip_ = QToolTip::isVisible() ? QToolTip::text() : QString{};
}

void AbstractLogView::hideViewToolTip()
{
    // Another view's tooltip, or one this view no longer owns, stays.
    if ( !shownToolTip_.isEmpty() && QToolTip::isVisible() && QToolTip::text() == shownToolTip_ ) {
        QToolTip::hideText();
    }
    shownToolTip_.clear();
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
    hideViewToolTip();
}

bool AbstractLogView::linePreviewCurrent() const
{
    if ( !linePreviewActive_ || !isVisible() || !window()->isActiveWindow() || charHeight_ <= 0 )
        return false;
    const auto position = viewport()->mapFromGlobal( QCursor::pos() );
    if ( !viewport()->rect().contains( position ) || position.x() < leftMarginPx_
         || position.y() < drawingTopOffset_ )
        return false;
    // The row under a still mouse is not compared: new lines that scroll the view (follow mode)
    // keep the preview of the line that was hovered. A mouse move to another line ends it
    // (viewportEvent).
    return !linePreviewValid_ || linePreviewValid_();
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
    showViewToolTip( linePreviewPosition_, html, viewport()->rect() );
}

bool AbstractLogView::viewportEvent( QEvent* event )
{
    if ( event->type() == QEvent::ToolTip ) {
        auto* help = static_cast<QHelpEvent*>( event );
        if ( rightMarginPx_ > 0 && help->pos().x() >= viewport()->width() - rightMarginPx_ ) {
            // The annotation column: its label in full when the column cuts it short. It stays
            // while the mouse stays in that cell of the column.
            clearLinePreview();
            if ( const auto annotation = elidedAnnotationAt( help->pos() );
                 !annotation.isEmpty() && charHeight_ > 0 ) {
                const auto row
                    = std::max( 0, ( help->pos().y() - drawingTopOffset_ ) / charHeight_ );
                const QRect cell( viewport()->width() - rightMarginPx_,
                                  drawingTopOffset_ + row * charHeight_, rightMarginPx_,
                                  charHeight_ );
                showViewToolTip( help->globalPos(), annotation, cell );
            }
            else {
                event->ignore();
            }
            return true;
        }
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
            // A scroll is no reason to close the preview: follow mode scrolls on every new line
            // of a live capture. The user's own scrolling (wheel, keys, the scroll bars) ends it
            // through those events, or the mouse leaving the view.
            connect( &PreviewManager::instance(), &PreviewManager::previewsChanged, this,
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
        // Not a resize: a live capture's long line that brings the horizontal scroll bar
        // resizes the viewport under a still mouse.
        if ( event->type() == QEvent::Leave || event->type() == QEvent::Hide
             || event->type() == QEvent::Wheel || event->type() == QEvent::MouseButtonPress )
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
