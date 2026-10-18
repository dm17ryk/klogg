/*
 * Copyright (C) 2023 -- 2024 Anton Filimonov and other contributors
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

#ifndef WRAPPEDSTRING_H
#define WRAPPEDSTRING_H

#include <QFontMetrics>
#include <QString>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <qchar.h>
#include <qglobal.h>
#include <utility>

#include <QStringView>

#include "containers.h"
#include "linetypes.h"
#include "log.h"

class WrappedString {
public:
    using WrappedStringPart = QStringView;

    static WrappedStringPart makeWrappedStringPart( const QString& lineText, LineColumn firstCol,
                                                    LineLength length )
    {
        return QStringView( lineText ).mid( firstCol.get(), length.get() );
    }

    explicit WrappedString( QString longLine, LineLength visibleColumns,
                            size_t maxRows = std::numeric_limits<size_t>::max() )
        : unwrappedLine_( std::move( longLine ) )
    {
        const auto columns = static_cast<int>(
            std::clamp<qint64>( visibleColumns.get(), 1, std::numeric_limits<int>::max() ) );
        width_ = columns;
        wrap( maxRows );
    }

    WrappedString( QString longLine, const QFontMetrics& metrics, int pixelWidth,
                   size_t maxRows = std::numeric_limits<size_t>::max() )
        : unwrappedLine_( std::move( longLine ) )
        , metrics_( metrics )
        , width_( std::max( 1, pixelWidth ) )
    {
        wrap( maxRows );
    }

    size_t wrappedLinesCount() const
    {
        return wrappedLines_.size();
    }

    klogg::vector<WrappedStringPart> mid( LineColumn start, LineLength length ) const
    {
        klogg::vector<WrappedStringPart> resultChunks;
        if ( length <= 0_length ) {
            return resultChunks;
        }
        const auto begin = static_cast<qint64>( start.get() );
        const auto end = begin + static_cast<qint64>( length.get() );
        for ( const auto& row : wrappedLines_ ) {
            const auto rowBegin = static_cast<qint64>( row.data() - unwrappedLine_.constData() );
            const auto rowEnd = rowBegin + row.size();
            const auto first = std::max( begin, rowBegin );
            const auto last = std::min( end, rowEnd );
            if ( first < last ) {
                resultChunks.push_back( row.mid( first - rowBegin, last - first ) );
            }
        }
        return resultChunks;
    }

    bool hasContinuation() const
    {
        return consumed_ < unwrappedLine_.size();
    }

    void appendRows( size_t maxRows )
    {
        wrap( maxRows );
    }

    // Counts code units submitted to font measurement and whitespace examination,
    // including repeated measurements. This measures work, rather than retained output.
    uint64_t processedCharacters() const
    {
        return processedCharacters_;
    }

    LineColumn nextColumn() const
    {
        return LineColumn{ consumed_ };
    }

    uint64_t estimatedRetainedBytes() const
    {
        return static_cast<uint64_t>( unwrappedLine_.size() ) * sizeof( QChar )
               + static_cast<uint64_t>( wrappedLines_.capacity() ) * sizeof( WrappedStringPart );
    }

    LineColumn sourceColumn( size_t row ) const
    {
        return LineColumn{ type_safe::narrow_cast<LineColumn::UnderlyingType>(
            wrappedLines_.at( row ).data() - unwrappedLine_.constData() ) };
    }

    size_t rowForColumn( LineColumn column ) const
    {
        const auto found
            = std::upper_bound( wrappedLines_.begin(), wrappedLines_.end(), column.get(),
                                [ this ]( auto source, const auto& row ) {
                                    return source < row.data() - unwrappedLine_.constData();
                                } );
        return found == wrappedLines_.begin()
                   ? 0
                   : static_cast<size_t>( std::distance( wrappedLines_.begin(), found ) - 1 );
    }

    WrappedString window( size_t firstRow, size_t maxRows ) const
    {
        // Do not copy the full accumulated row index into every painted row.
        WrappedString result;
        result.unwrappedLine_ = unwrappedLine_;
        const auto first = std::min( firstRow, wrappedLines_.size() );
        const auto count = std::min( maxRows, wrappedLines_.size() - first );
        const auto begin = wrappedLines_.begin() + static_cast<ptrdiff_t>( first );
        result.wrappedLines_.assign( begin, begin + static_cast<ptrdiff_t>( count ) );
        result.consumed_ = count ? static_cast<int>( result.wrappedLines_.back().data()
                                                     - unwrappedLine_.constData()
                                                     + result.wrappedLines_.back().size() )
                                 : 0;
        return result;
    }

    bool isEmpty() const
    {
        return unwrappedLine_.isEmpty();
    }

    WrappedStringPart unwrappedLine() const
    {
        return WrappedStringPart{ unwrappedLine_ };
    }

    WrappedStringPart wrappedLine( size_t index ) const
    {
        return wrappedLines_[ index ];
    }

private:
    WrappedString() = default;

    int fittingLength( WrappedStringPart text )
    {
        // A zero-width/combining run must not turn one row into a full-record scan.
        constexpr int maximumProbe = 4096;
        const auto length = std::min( klogg::isize( text ), maximumProbe );
        const auto measure = [ & ]( int count ) {
            processedCharacters_ += static_cast<uint64_t>( count );
            return metrics_->horizontalAdvance( QString::fromRawData( text.data(), count ) );
        };
        int low = 0;
        int high = std::min( length,
                             std::max( 1, width_ / std::max( 1, metrics_->averageCharWidth() ) ) );
        // Grow from the viewport width, not from half of a potentially enormous line.
        while ( high < length && measure( high ) <= width_ ) {
            low = high;
            high += std::min( high, length - high );
        }
        if ( measure( high ) <= width_ ) {
            return high;
        }
        while ( low + 1 < high ) {
            const auto middle = low + ( high - low ) / 2;
            if ( measure( middle ) <= width_ ) {
                low = middle;
            }
            else {
                high = middle;
            }
        }
        return std::max( 1, low );
    }

    void wrap( size_t maxRows )
    {
        WrappedStringPart remaining = QStringView( unwrappedLine_ ).mid( consumed_ );
        if ( unwrappedLine_.isEmpty() && wrappedLines_.empty() ) {
            wrappedLines_.push_back( remaining );
            LOG_DEBUG << "Wrapped empty record into one row";
            return;
        }
        const auto rowsBefore = wrappedLines_.size();
        const auto budget = std::max( size_t{ 1 }, maxRows );
        while ( !remaining.isEmpty() && wrappedLines_.size() - rowsBefore < budget ) {
            auto count = metrics_ ? fittingLength( remaining )
                                  : std::min( klogg::isize( remaining ), width_ );
            if ( count < remaining.size() ) {
                const auto candidate = remaining.left( count );
                const auto lastSpace
                    = std::find_if( candidate.rbegin(), candidate.rend(), [ this ]( QChar c ) {
                          ++processedCharacters_;
                          return c.isSpace();
                      } );
                if ( lastSpace != candidate.rend() ) {
                    count
                        = static_cast<int>( std::distance( candidate.begin(), lastSpace.base() ) );
                }
                if ( remaining.at( count - 1 ).isHighSurrogate()
                     && remaining.at( count ).isLowSurrogate() ) {
                    count += count == 1 ? 1 : -1;
                }
            }
            wrappedLines_.push_back( remaining.left( count ) );
            consumed_ += count;
            remaining = remaining.mid( count );
        }
        LOG_DEBUG << "Wrapped record slice: appendedRows=" << wrappedLines_.size() - rowsBefore
                  << " rowBudget=" << budget << " consumed=" << consumed_
                  << " total=" << unwrappedLine_.size() << " more=" << hasContinuation()
                  << " processedCharacters=" << processedCharacters_;
    }

    klogg::vector<WrappedStringPart> wrappedLines_;
    QString unwrappedLine_;
    std::optional<QFontMetrics> metrics_;
    int width_ = 1;
    int consumed_ = 0;
    uint64_t processedCharacters_ = 0;
};

#endif // WRAPPEDSTRING_H
