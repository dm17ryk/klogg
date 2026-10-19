/*
 * Copyright (C) 2010, 2013 Nicolas Bonnefon and other contributors
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

// This file implements Selection.
// This class implements the selection handling. No check is made on
// the validity of the selection, it must be handled by the caller.
// Whole-row ranges and a partial-text selection are mutually exclusive.

#include <numeric>

#include "abstractlogdata.h"
#include "containers.h"
#include "linetypes.h"
#include "log.h"
#include "selection.h"

Selection::Selection() = default;

void Selection::clear()
{
    selectedRanges_.clear();
    selectedPartial_.line = {};
    anchorLine_ = {};
    LOG_DEBUG << "Selection cleared";
}

void Selection::selectLine( LineNumber line )
{
    clear();
    selectedRanges_.emplace( line, line );
    anchorLine_ = line;
    LOG_DEBUG << "Selection replaced with row=" << line;
}

void Selection::addRange( LineNumber first, LineNumber last )
{
    auto it = selectedRanges_.lower_bound( first );
    if ( it != selectedRanges_.begin() ) {
        const auto previous = std::prev( it );
        if ( previous->second >= first || first.get() - previous->second.get() == 1 ) {
            it = previous;
        }
    }
    while ( it != selectedRanges_.end()
            && ( it->first <= last || it->first.get() - last.get() == 1 ) ) {
        first = std::min( first, it->first );
        last = std::max( last, it->second );
        it = selectedRanges_.erase( it );
    }
    selectedRanges_.emplace( first, last );
    LOG_DEBUG << "Selection range merged first=" << first << " last=" << last
              << " ranges=" << selectedRanges_.size();
}

void Selection::toggleLine( LineNumber line )
{
    // Starting a row set from a text portion replaces that portion.
    selectedPartial_.line = {};
    anchorLine_ = line;
    auto it = selectedRanges_.upper_bound( line );
    if ( it != selectedRanges_.begin() && std::prev( it )->second >= line ) {
        --it;
        const auto first = it->first;
        const auto last = it->second;
        selectedRanges_.erase( it );
        if ( first < line ) {
            selectedRanges_.emplace( first, line - 1_lcount );
        }
        if ( line < last ) {
            selectedRanges_.emplace( line + 1_lcount, last );
        }
        LOG_DEBUG << "Selection row removed=" << line << " ranges=" << selectedRanges_.size();
    }
    else {
        addRange( line, line );
        LOG_DEBUG << "Selection row added=" << line << " ranges=" << selectedRanges_.size();
    }
}

void Selection::selectPortion( LineNumber line, LineColumn startColumn, LineColumn endColumn )
{
    selectedRanges_.clear();
    anchorLine_ = line;

    selectedPartial_.line = line;
    selectedPartial_.startColumn = std::min( startColumn, endColumn );
    selectedPartial_.endColumn = std::max( startColumn, endColumn );
    LOG_DEBUG << "Selection replaced with portion row=" << line
              << " first=" << selectedPartial_.startColumn
              << " last=" << selectedPartial_.endColumn;
}

void Selection::selectRange( LineNumber startLine, LineNumber endLine )
{
    clear();
    anchorLine_ = startLine;
    addRange( std::min( startLine, endLine ), std::max( startLine, endLine ) );
}

void Selection::selectRangeFromPrevious( LineNumber line, bool additive )
{
    const auto anchor = anchorLine_.value_or( 0_lnum );
    if ( !additive ) {
        selectedRanges_.clear();
    }
    selectedPartial_.line = {};
    anchorLine_ = anchor;
    addRange( std::min( anchor, line ), std::max( anchor, line ) );
    LOG_DEBUG << "Selection anchored range anchor=" << anchor << " end=" << line
              << " additive=" << additive;
}

void Selection::crop( LineNumber last_line )
{
    if ( selectedPartial_.line.has_value() && *selectedPartial_.line > last_line )
        selectedPartial_.line = {};

    selectedRanges_.erase( selectedRanges_.upper_bound( last_line ), selectedRanges_.end() );
    if ( !selectedRanges_.empty() ) {
        auto& last = selectedRanges_.rbegin()->second;
        last = std::min( last, last_line );
    }
    if ( anchorLine_ && *anchorLine_ > last_line ) {
        anchorLine_ = isEmpty() ? OptionalLineNumber{} : OptionalLineNumber{ last_line };
    }
    LOG_DEBUG << "Selection cropped last=" << last_line << " ranges=" << selectedRanges_.size();
}

Portion Selection::getPortionForLine( LineNumber line ) const
{
    if ( selectedPartial_.line.has_value() && *selectedPartial_.line == line ) {
        return Portion( *selectedPartial_.line, selectedPartial_.startColumn,
                        selectedPartial_.endColumn );
    }

    return {};
}

bool Selection::isLineSelected( LineNumber line ) const
{
    const auto it = selectedRanges_.upper_bound( line );
    return it != selectedRanges_.begin() && line <= std::prev( it )->second;
}

bool Selection::isPortionSelected( LineNumber line, LineColumn startColumn,
                                   LineColumn endColumn ) const
{
    if ( isLineSelected( line ) ) {
        return true;
    }

    const auto portion = getPortionForLine( line );
    if ( !portion.isValid() ) {
        return false;
    }

    return startColumn >= portion.startColumn() && endColumn <= portion.endColumn();
}

OptionalLineNumber Selection::selectedLine() const
{
    return isSingleLine() ? OptionalLineNumber{ selectedRanges_.begin()->first }
                          : OptionalLineNumber{};
}

klogg::vector<LineNumber> Selection::getLines() const
{
    klogg::vector<LineNumber> selection;

    for ( const auto& range : getLineRanges() ) {
        const auto offset = selection.size();
        selection.resize( offset + static_cast<size_t>( range.second.get() ) );
        std::iota( selection.begin() + static_cast<ptrdiff_t>( offset ), selection.end(),
                   range.first );
    }

    return selection;
}

klogg::vector<std::pair<LineNumber, LinesCount>> Selection::getLineRanges() const
{
    klogg::vector<std::pair<LineNumber, LinesCount>> ranges;
    if ( selectedPartial_.line ) {
        ranges.emplace_back( *selectedPartial_.line, 1_lcount );
    }
    else {
        ranges.reserve( selectedRanges_.size() );
        for ( const auto& range : selectedRanges_ ) {
            ranges.emplace_back( range.first, ( range.second - range.first ) + 1_lcount );
        }
    }
    return ranges;
}

LinesCount Selection::getSelectedLinesCount() const
{
    auto count = selectedPartial_.line ? 1_lcount : 0_lcount;
    for ( const auto& range : selectedRanges_ ) {
        count += ( range.second - range.first ) + 1_lcount;
    }
    return count;
}

// The tab behaviour is a bit odd at the moment, full lines are not expanded
// but partials (part of line) are, they probably should not ideally.
QString Selection::getSelectedText( const AbstractLogData* logData, bool lineNumbers ) const
{
    const auto selectionData = getSelectionWithLineNumbers( logData );

    QString text;

    const auto selectionSizeEstimate = std::accumulate(
        selectionData.begin(), selectionData.end(), klogg::isize( selectionData ),
        []( const auto& acc, const auto& next ) { return acc + next.second.size(); } );

    text.reserve( selectionSizeEstimate );

    bool first = true;
    for ( const auto& [ lineNumber, line ] : selectionData ) {
        if ( !first ) {
#if defined( Q_OS_WIN )
            text.append( QChar::CarriageReturn );
#endif
            text.append( QChar::LineFeed );
        }
        first = false;

        if ( lineNumbers ) {
            text.append( QStringLiteral( "%1: %2" ).arg( lineNumber.get() ).arg( line ) );
        }
        else {
            text.append( line );
        }
    }

    return text;
}

std::map<LineNumber, QString>
Selection::getSelectionWithLineNumbers( const AbstractLogData* logData ) const
{
    std::map<LineNumber, QString> selectionData;

    if ( selectedPartial_.line.has_value() ) {
        selectionData.emplace(
            logData->getLineNumber( selectedPartial_.line.value() ),
            logData->getExpandedLineString( *selectedPartial_.line )
                .mid( selectedPartial_.startColumn.get(), selectedPartial_.size().get() ) );
    }
    else {
        for ( const auto& range : getLineRanges() ) {
            const auto list = logData->getLines( range.first, range.second );
            auto ln = range.first;
            for ( const auto& line : list ) {
                selectionData.emplace( logData->getLineNumber( ln ), line );
                ln++;
            }
        }
    }

    return selectionData;
}

FilePosition Selection::getNextPosition() const
{
    LineNumber line;
    LineColumn column = 0_lcol;

    if ( !selectedRanges_.empty() ) {
        line = selectedRanges_.rbegin()->second + 1_lcount;
    }
    else if ( selectedPartial_.line.has_value() ) {
        line = *selectedPartial_.line;
        column = selectedPartial_.endColumn + 1_length;
    }

    return FilePosition( line, column );
}

FilePosition Selection::getPreviousPosition() const
{
    LineNumber line = 0_lnum;
    LineColumn column = 0_lcol;

    if ( !selectedRanges_.empty() ) {
        line = selectedRanges_.begin()->first;
    }
    else if ( selectedPartial_.line.has_value() ) {
        line = *selectedPartial_.line;
        column = selectedPartial_.startColumn - 1_length;
    }

    return FilePosition( line, column );
}
