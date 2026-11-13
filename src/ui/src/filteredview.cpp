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

#include <cassert>

#include <QAction>
#include <QMenu>
#include <QSignalBlocker>

#include "filteredview.h"
#include "shortcuts.h"

FilteredView::FilteredView( LogFilteredData* newLogData,
                            const QuickFindPattern* const quickFindPattern, QWidget* parent )
    : AbstractLogView( newLogData, quickFindPattern, parent )
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

void FilteredView::setMatchLabeler( std::shared_ptr<const MatchLabeler> labeler )
{
    matchLabeler_ = std::move( labeler );
    annotationCache_.clear();
    update();
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
    if ( !matchedByVisible_ || !matchLabeler_ || matchLabeler_->isEmpty() ) {
        return 0;
    }
    return matchLabeler_->preferredColumnChars();
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
    if ( type.testFlag( LineTypeFlags::Match ) ) {
        if ( !matchLabeler_ || matchLabeler_->isEmpty() ) {
            return {};
        }
        const auto sourceLine = logFilteredData_->getMatchingLineNumber( lineNumber ).get();
        if ( const auto cached = annotationCache_.constFind( sourceLine );
             cached != annotationCache_.cend() && cached->textSize == lineText.size() ) {
            return cached->label;
        }
        if ( annotationCache_.size() >= MaxCachedAnnotations ) {
            annotationCache_.clear();
        }
        const auto label = matchLabeler_->label( lineText );
        annotationCache_.insert( sourceLine, { lineText.size(), label } );
        return label;
    }
    if ( type.testFlag( LineTypeFlags::Mark ) ) {
        return tr( "mark" );
    }
    // Context lines around a match are not selected by any filter.
    return {};
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
