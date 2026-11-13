/*
 * Copyright (C) 2009, 2010, 2012, 2017 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

// This file implements the FilteredView concrete class.
// Most of the actual drawing and event management is done in AbstractLogView
// Only behaviour specific to the filtered (bottom) view is implemented here.

#include <algorithm>
#include <cassert>

#include <QAction>
#include <QCoreApplication>
#include <QMenu>
#include <QMetaObject>
#include <QPointer>
#include <QSignalBlocker>
#include <QThreadPool>

#include "filteredview.h"
#include "log.h"
#include "runnable_lambda.h"
#include "shortcuts.h"

namespace {
// Labels of every filtered view are computed on one background thread: an expression that
// backtracks badly on a crafted line delays its label, never the painting of the view.
QThreadPool* annotationThreadPool()
{
    static QThreadPool* const pool = [] {
        auto* threadPool = new QThreadPool( QCoreApplication::instance() );
        threadPool->setObjectName( QStringLiteral( "matchedByLabeler" ) );
        threadPool->setMaxThreadCount( 1 );
        return threadPool;
    }();
    return pool;
}
} // namespace

FilteredView::FilteredView( LogFilteredData* newLogData,
                            const QuickFindPattern* const quickFindPattern, QWidget* parent )
    : AbstractLogView( newLogData, quickFindPattern, parent )
    , labelerGeneration_( std::make_shared<std::atomic<quint64>>( 0 ) )
{
    // We keep a copy of the filtered data for fast lookup of the line type
    logFilteredData_ = newLogData;

    matchedByAction_ = new QAction( tr( "Show \"Matched by\" column" ), this );
    matchedByAction_->setObjectName( QStringLiteral( "showMatchedByColumnAction" ) );
    matchedByAction_->setCheckable( true );
    matchedByAction_->setChecked( matchedByVisible_ );
    connect( matchedByAction_, &QAction::toggled, this, [ this ]( bool visible ) {
        setMatchedByColumnVisible( visible );
        Q_EMIT matchedByColumnToggled( visible );
    } );

    clearResultsAction_ = new QAction( tr( "Clear results" ), this );
    clearResultsAction_->setObjectName( QStringLiteral( "clearFilteredResultsAction" ) );
    clearResultsAction_->setToolTip(
        tr( "Remove the current matches; new matching lines keep arriving" ) );
    connect( clearResultsAction_, &QAction::triggered, this,
             [ this ] { Q_EMIT clearResultsRequested(); } );

    if ( auto* menu = popupMenu(); menu != nullptr ) {
        menu->addSeparator();
        menu->addAction( matchedByAction_ );
        menu->addAction( clearResultsAction_ );
    }
}

FilteredView::~FilteredView()
{
    // Labels still queued for this view are skipped.
    labelerGeneration_->fetch_add( 1 );
}

void FilteredView::setMatchLabeler( std::shared_ptr<const MatchLabeler> labeler )
{
    matchLabeler_ = std::move( labeler );
    labelerGeneration_->fetch_add( 1 );
    annotationCache_.clear();
    pendingAnnotations_.clear();
    // The column width depends on the labeler: lay the text area out again.
    forceRefresh();
}

void FilteredView::setMatchedByColumnVisible( bool visible )
{
    if ( matchedByVisible_ == visible ) {
        return;
    }
    matchedByVisible_ = visible;
    {
        const QSignalBlocker blocker( matchedByAction_ );
        matchedByAction_->setChecked( visible );
    }
    // The text area starts after the column, so the whole view is laid out again.
    updateData();
}

int FilteredView::annotationColumnChars() const
{
    if ( !matchedByVisible_ ) {
        return 0;
    }
    constexpr int LabelPaddingChars = 2;
    const auto markChars = static_cast<int>( tr( "mark" ).size() ) + LabelPaddingChars;
    if ( matchLabeler_ && !matchLabeler_->isEmpty() ) {
        return std::max( matchLabeler_->preferredColumnChars(), markChars );
    }
    // Nothing names the matches (an inverse search, no search), but marked lines still say
    // "mark" while marks are shown.
    using VisibilityFlags = LogFilteredData::VisibilityFlags;
    const bool marksShown = logFilteredData_->visibility().testFlag( VisibilityFlags::Marks )
                            && logFilteredData_->getNbMarks() > 0_lcount;
    return marksShown ? markChars : 0;
}

QString FilteredView::matchedByText( LineNumber index ) const
{
    if ( index >= logFilteredData_->getNbLine() ) {
        return {};
    }
    return lineAnnotation( index, logFilteredData_->getLineString( index ) );
}

QString FilteredView::lineAnnotation( LineNumber lineNumber, const QString& lineText ) const
{
    using LineTypeFlags = LogFilteredData::LineTypeFlags;
    const auto type = logFilteredData_->lineTypeByIndex( lineNumber );
    // A marked line is the user's own: it says "mark" even when the search matches it too.
    if ( type.testFlag( LineTypeFlags::Mark ) ) {
        return tr( "mark" );
    }
    // Context lines around a match are not selected by any filter.
    if ( !type.testFlag( LineTypeFlags::Match ) || !matchLabeler_ || matchLabeler_->isEmpty() ) {
        return {};
    }
    const auto sourceLine = logFilteredData_->getMatchingLineNumber( lineNumber ).get();
    const auto textHash = static_cast<std::size_t>( qHash( lineText ) );
    if ( const auto cached = annotationCache_.constFind( sourceLine );
         cached != annotationCache_.cend() && cached->textHash == textHash
         && cached->textSize == lineText.size() ) {
        return cached->label;
    }
    requestAnnotation( sourceLine, lineText, textHash );
    // Blank until the labelling thread answers; the view is repainted then.
    return {};
}

void FilteredView::requestAnnotation( quint64 sourceLine, const QString& lineText,
                                      std::size_t textHash ) const
{
    if ( const auto pending = pendingAnnotations_.constFind( sourceLine );
         pending != pendingAnnotations_.cend() && pending.value() == textHash ) {
        return;
    }
    if ( pendingAnnotations_.size() >= MaxPendingAnnotations ) {
        return;
    }
    pendingAnnotations_.insert( sourceLine, textHash );

    auto* pool = annotationThreadPool();
    // The view is only touched back on the GUI thread, through the queued delivery below.
    const QPointer<FilteredView> target( const_cast<FilteredView*>( this ) );
    const auto requestGeneration = labelerGeneration_->load();
    pool->start( createRunnable( [ labeler = matchLabeler_, currentGeneration = labelerGeneration_,
                                   requestGeneration, target, pool, sourceLine, lineText,
                                   textHash ] {
        if ( currentGeneration->load() != requestGeneration ) {
            return;
        }
        const auto lineLabel = labeler->label( lineText );
        const auto textSize = lineText.size();
        QMetaObject::invokeMethod(
            pool,
            [ target, requestGeneration, sourceLine, textHash, textSize, lineLabel ] {
                if ( target ) {
                    target->storeAnnotation( requestGeneration, sourceLine, textHash, textSize,
                                             lineLabel );
                }
            },
            Qt::QueuedConnection );
    } ) );
}

void FilteredView::storeAnnotation( quint64 labelerGeneration, quint64 sourceLine,
                                    std::size_t textHash, qsizetype textSize,
                                    const QString& label )
{
    if ( labelerGeneration != labelerGeneration_->load() ) {
        LOG_DEBUG << "Dropping the label of line " << sourceLine << " from a replaced labeler";
        return;
    }
    if ( const auto pending = pendingAnnotations_.find( sourceLine );
         pending != pendingAnnotations_.end() && pending.value() == textHash ) {
        pendingAnnotations_.erase( pending );
    }
    if ( annotationCache_.size() >= MaxCachedAnnotations ) {
        annotationCache_.clear();
    }
    annotationCache_.insert( sourceLine, { textHash, textSize, label } );
    // Coalesced with the other labels that arrive before the next paint.
    forceRefresh();
}

void FilteredView::setVisibility( Visibility visi )
{
    assert( logFilteredData_ );

    logFilteredData_->setVisibility( visi );

    updateData();
}

FilteredView::Visibility FilteredView::visibility() const
{
    assert( logFilteredData_ );

    return logFilteredData_->visibility();
}

// For the filtered view, a line is always matching!
AbstractLogData::LineType FilteredView::lineType( LineNumber lineNumber ) const
{
    // line in filteredview corresponds to index
    return logFilteredData_->lineTypeByIndex( lineNumber );
}

bool FilteredView::groupSeparatorBefore( LineNumber lineNumber ) const
{
    return logFilteredData_->isContextGroupStart( lineNumber );
}

LineNumber FilteredView::displayLineNumber( LineNumber lineNumber ) const
{
    // Display a 1-based index
    return logFilteredData_->getMatchingLineNumber( lineNumber ) + 1_lcount;
}

LineNumber FilteredView::lineIndex( LineNumber lineNumber ) const
{
    return logFilteredData_->getLineIndexNumber( lineNumber );
}

LineNumber FilteredView::maxDisplayLineNumber() const
{
    return LineNumber( logFilteredData_->getNbTotalLines().get() );
}

void FilteredView::doRegisterShortcuts()
{
    LOG_INFO << "Registering shortcuts for filtered view";
    AbstractLogView::doRegisterShortcuts();
    registerShortcut( ShortcutAction::LogViewNextMark, [ this ] {
        using LineTypeFlags = LogFilteredData::LineTypeFlags;
        auto i = getViewPosition() - 1_lcount;
        bool foundMark = false;
        for ( ; i != 0_lnum; --i ) {
            if ( lineType( i ).testFlag( LineTypeFlags::Mark ) ) {
                foundMark = true;
                break;
            }
        }

        if ( !foundMark ) {
            foundMark = lineType( i ).testFlag( LineTypeFlags::Mark );
        }

        if ( foundMark ) {
            selectAndDisplayLine( i );
        }
    } );
    registerShortcut( ShortcutAction::LogViewPrevMark, [ this ] {
        const auto nbLines = logFilteredData_->getNbLine();
        for ( auto i = getViewPosition() + 1_lcount; i < nbLines; ++i ) {
            if ( lineType( i ).testFlag( LogFilteredData::LineTypeFlags::Mark ) ) {
                selectAndDisplayLine( i );
                break;
            }
        }
    } );
}
