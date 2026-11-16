// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "searchbarlayout.h"

#include <algorithm>
#include <numeric>

#include <QAbstractItemView>
#include <QAction>
#include <QEvent>
#include <QLayout>
#include <QMenu>
#include <QScreen>
#include <QScrollBar>
#include <QToolButton>

#include "log.h"

// ---------------------------------------------------------------------------------------------
// WidePopupComboBox

WidePopupComboBox::WidePopupComboBox( QWidget* parent )
    : QComboBox( parent )
{
}

void WidePopupComboBox::setPopupWidthReference( QWidget* reference )
{
    popupWidthReference_ = reference;
}

int WidePopupComboBox::popupWidth( int comboWidth, int referenceWidth, int contentWidth,
                                   int availableWidth )
{
    const auto wanted = std::max( { comboWidth, referenceWidth, contentWidth } );
    return availableWidth > 0 ? std::min( wanted, std::max( comboWidth, availableWidth ) ) : wanted;
}

void WidePopupComboBox::showPopup()
{
    QComboBox::showPopup();

    auto* itemView = view();
    auto* popup = itemView != nullptr ? itemView->window() : nullptr;
    if ( popup == nullptr || popup == window() ) {
        return;
    }
    const auto* screen = this->screen();
    const auto available = screen != nullptr ? screen->availableGeometry() : QRect{};
    const auto scrollBarWidth
        = itemView->verticalScrollBar() != nullptr && itemView->verticalScrollBar()->isVisible()
              ? itemView->verticalScrollBar()->width()
              : 0;
    const auto contentWidth
        = itemView->sizeHintForColumn( 0 ) + 2 * itemView->frameWidth() + scrollBarWidth + 8;
    const auto referenceWidth = popupWidthReference_ ? popupWidthReference_->width() : 0;
    const auto width = popupWidth( this->width(), referenceWidth, contentWidth, available.width() );
    if ( width <= popup->width() ) {
        return;
    }

    // Start at the reference widget (the search bar) so the list stays under it, and keep it on
    // the screen.
    auto left = popupWidthReference_ ? popupWidthReference_->mapToGlobal( QPoint{ 0, 0 } ).x()
                                     : popup->x();
    if ( !available.isNull() ) {
        left = std::clamp( left, available.left(),
                           std::max( available.left(), available.right() + 1 - width ) );
    }
    LOG_DEBUG << "Widening search combo popup from " << popup->width() << " to " << width << " px";
    popup->setGeometry( left, popup->y(), width, popup->height() );
}

// ---------------------------------------------------------------------------------------------
// SearchBarCollapser

SearchBarCollapser::SearchBarCollapser( QWidget* bar, QToolButton* overflowButton )
    : QObject( bar )
    , bar_( bar )
    , overflowButton_( overflowButton )
    , overflowMenu_( new QMenu( overflowButton ) )
{
    overflowMenu_->setObjectName( QStringLiteral( "searchBarOverflowMenu" ) );
    overflowButton_->setMenu( overflowMenu_ );
    overflowButton_->setPopupMode( QToolButton::InstantPopup );
    overflowButton_->hide();
    connect( overflowMenu_, &QMenu::aboutToShow, this, &SearchBarCollapser::syncOverflowMenu );
    bar_->installEventFilter( this );
}

void SearchBarCollapser::addCollapsible( QWidget* widget, QAction* action )
{
    entries_.push_back( { widget, action } );
}

QAction* SearchBarCollapser::mirrorAction( QToolButton* button, QObject* parent )
{
    auto* action = new QAction(
        button->accessibleName().isEmpty() ? button->toolTip() : button->accessibleName(), parent );
    action->setToolTip( button->toolTip() );
    action->setCheckable( button->isCheckable() );
    action->setChecked( button->isChecked() );
    QPointer<QToolButton> safeButton = button;
    // A click keeps the button's own signals (and so the search) the only source of truth.
    connect( action, &QAction::triggered, button, [ safeButton ] {
        if ( safeButton ) {
            safeButton->click();
        }
    } );
    connect( button, &QToolButton::toggled, action, &QAction::setChecked );
    return action;
}

int SearchBarCollapser::collapsedCount( int available, int fixed,
                                        const std::vector<int>& collapsible, int overflow )
{
    const auto total = static_cast<int>( collapsible.size() );
    auto remaining = std::accumulate( collapsible.begin(), collapsible.end(), 0 );
    if ( fixed + remaining <= available ) {
        return 0;
    }
    for ( int count = 1; count <= total; ++count ) {
        remaining -= collapsible.at( static_cast<size_t>( count - 1 ) );
        if ( fixed + remaining + overflow <= available ) {
            return count;
        }
    }
    return total;
}

bool SearchBarCollapser::isCollapsible( const QWidget* widget ) const
{
    return std::any_of( entries_.begin(), entries_.end(),
                        [ widget ]( const Entry& entry ) { return entry.widget == widget; } );
}

int SearchBarCollapser::fixedWidth() const
{
    const auto* layout = bar_->layout();
    if ( layout == nullptr ) {
        return 0;
    }
    const auto margins = layout->contentsMargins();
    const auto spacing = std::max( 0, layout->spacing() );
    int width = margins.left() + margins.right();
    for ( int index = 0; index < layout->count(); ++index ) {
        const auto* widget = layout->itemAt( index )->widget();
        if ( widget == nullptr || widget == overflowButton_ || isCollapsible( widget )
             || widget->isHidden() ) {
            continue;
        }
        const auto ignored = widget->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored;
        width += ( ignored ? widget->minimumWidth()
                           : std::max( widget->minimumSizeHint().width(), widget->minimumWidth() ) )
                 + spacing;
    }
    return width;
}

int SearchBarCollapser::compactWidth() const
{
    const auto spacing = bar_->layout() != nullptr ? std::max( 0, bar_->layout()->spacing() ) : 0;
    return fixedWidth() + overflowButton_->sizeHint().width() + spacing;
}

int SearchBarCollapser::unfoldedWidth() const
{
    const auto spacing = bar_->layout() != nullptr ? std::max( 0, bar_->layout()->spacing() ) : 0;
    int width = fixedWidth();
    for ( const auto& entry : entries_ ) {
        if ( entry.widget ) {
            width
                += std::max( entry.widget->minimumSizeHint().width(), entry.widget->minimumWidth() )
                   + spacing;
        }
    }
    return width;
}

void SearchBarCollapser::setMinimumWidthReduction( int factor )
{
    minimumWidthReduction_ = factor;
    relayout();
}

void SearchBarCollapser::relayout()
{
    if ( relayingOut_ || bar_->layout() == nullptr ) {
        return;
    }
    relayingOut_ = true;
    if ( minimumWidthReduction_ > 0 ) {
        const auto minimum = std::max( compactWidth(), unfoldedWidth() / minimumWidthReduction_ );
        if ( minimum != bar_->minimumWidth() ) {
            LOG_DEBUG << "Search bar minimum width " << minimum << " px (unfolded "
                      << unfoldedWidth() << " px)";
            bar_->setMinimumWidth( minimum );
        }
    }
    const auto spacing = std::max( 0, bar_->layout()->spacing() );
    std::vector<int> widths;
    widths.reserve( entries_.size() );
    for ( const auto& entry : entries_ ) {
        widths.push_back( entry.widget ? entry.widget->sizeHint().width() + spacing : 0 );
    }
    const auto count = collapsedCount( bar_->width(), fixedWidth(), widths,
                                       overflowButton_->sizeHint().width() + spacing );
    for ( size_t index = 0; index < entries_.size(); ++index ) {
        if ( auto* widget = entries_.at( index ).widget.data() ) {
            widget->setVisible( static_cast<int>( index ) >= count );
        }
    }
    overflowButton_->setVisible( count > 0 );
    if ( count != collapsed_ ) {
        LOG_DEBUG << "Search bar width " << bar_->width() << " px: " << count
                  << " control(s) in the overflow menu";
    }
    collapsed_ = count;
    relayingOut_ = false;
}

void SearchBarCollapser::syncOverflowMenu()
{
    overflowMenu_->clear();
    for ( int index = 0; index < collapsed_ && index < static_cast<int>( entries_.size() );
          ++index ) {
        const auto& entry = entries_.at( static_cast<size_t>( index ) );
        if ( entry.action == nullptr || entry.widget == nullptr ) {
            continue;
        }
        entry.action->setEnabled( entry.widget->isEnabled() );
        if ( auto* button = qobject_cast<QToolButton*>( entry.widget.data() );
             button != nullptr && button->isCheckable() ) {
            entry.action->setChecked( button->isChecked() );
        }
        overflowMenu_->addAction( entry.action );
    }
}

bool SearchBarCollapser::eventFilter( QObject* watched, QEvent* event )
{
    if ( watched == bar_
         && ( event->type() == QEvent::Resize || event->type() == QEvent::Show
              || event->type() == QEvent::LayoutRequest ) ) {
        relayout();
    }
    return QObject::eventFilter( watched, event );
}
