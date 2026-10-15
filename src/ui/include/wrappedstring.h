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
#include <limits>
#include <qchar.h>
#include <qglobal.h>
#include <utility>

#include <QStringView>

#include "containers.h"
#include "linetypes.h"

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
        wrap( nullptr, columns, maxRows );
    }

    WrappedString( QString longLine, const QFontMetrics& metrics, int pixelWidth,
                   size_t maxRows = std::numeric_limits<size_t>::max() )
        : unwrappedLine_( std::move( longLine ) )
    {
        wrap( &metrics, std::max( 1, pixelWidth ), maxRows );
    }

    size_t wrappedLinesCount() const
    {
        return wrappedLines_.size();
    }

    klogg::vector<WrappedStringPart> mid( LineColumn start, LineLength length ) const
    {
        auto getLength = []( const auto& view ) -> LineLength::UnderlyingType {
            return type_safe::narrow_cast<LineLength::UnderlyingType>( view.size() );
        };

        klogg::vector<WrappedStringPart> resultChunks;
        if ( wrappedLines_.size() == 1 ) {
            auto& wrappedLine = wrappedLines_.front();
            auto len = std::min( length.get(), getLength( wrappedLine ) - start.get() );
            resultChunks.push_back( wrappedLine.mid( start.get(), ( len > 0 ? len : 0 ) ) );
            return resultChunks;
        }

        size_t wrappedLineIndex = 0;
        auto positionInWrappedLine = start.get();
        while ( positionInWrappedLine > getLength( wrappedLines_[ wrappedLineIndex ] ) ) {
            positionInWrappedLine -= getLength( wrappedLines_[ wrappedLineIndex ] );
            wrappedLineIndex++;
            if ( wrappedLineIndex >= wrappedLines_.size() ) {
                return resultChunks;
            }
        }

        auto chunkLength = length.get();
        while ( positionInWrappedLine + chunkLength
                > getLength( wrappedLines_[ wrappedLineIndex ] ) ) {
            resultChunks.push_back(
                wrappedLines_[ wrappedLineIndex ].mid( positionInWrappedLine ) );
            wrappedLineIndex++;
            positionInWrappedLine = 0;
            chunkLength -= getLength( resultChunks.back() );
            if ( wrappedLineIndex >= wrappedLines_.size() ) {
                return resultChunks;
            }
        }

        if ( chunkLength > 0 ) {
            auto& wrappedLine = wrappedLines_[ wrappedLineIndex ];
            auto len = std::min( chunkLength, getLength( wrappedLine ) - positionInWrappedLine );
            resultChunks.push_back(
                wrappedLine.mid( positionInWrappedLine, ( len > 0 ? len : 0 ) ) );
        }

        return resultChunks;
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
    static int fittingLength( WrappedStringPart text, const QFontMetrics& metrics, int width )
    {
        const auto length = klogg::isize( text );
        const auto measure = [ & ]( int count ) {
            return metrics.horizontalAdvance( QString::fromRawData( text.data(), count ) );
        };
        int low = 0;
        int high
            = std::min( length, std::max( 1, width / std::max( 1, metrics.averageCharWidth() ) ) );
        // Grow from the viewport width, not from half of a potentially enormous line.
        while ( high < length && measure( high ) <= width ) {
            low = high;
            high += std::min( high, length - high );
        }
        if ( measure( high ) <= width ) {
            return high;
        }
        while ( low + 1 < high ) {
            const auto middle = low + ( high - low ) / 2;
            if ( measure( middle ) <= width ) {
                low = middle;
            }
            else {
                high = middle;
            }
        }
        return std::max( 1, low );
    }

    void wrap( const QFontMetrics* metrics, int width, size_t maxRows )
    {
        WrappedStringPart remaining( unwrappedLine_ );
        const auto rowLimit = std::max( size_t{ 1 }, maxRows );
        if ( remaining.isEmpty() ) {
            wrappedLines_.push_back( remaining );
            return;
        }
        while ( !remaining.isEmpty() && wrappedLines_.size() < rowLimit ) {
            auto count = metrics ? fittingLength( remaining, *metrics, width )
                                 : std::min( klogg::isize( remaining ), width );
            if ( count < remaining.size() ) {
                const auto candidate = remaining.left( count );
                const auto lastSpace = std::find_if( candidate.rbegin(), candidate.rend(),
                                                     []( QChar c ) { return c.isSpace(); } );
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
            remaining = remaining.mid( count );
        }
    }

    klogg::vector<WrappedStringPart> wrappedLines_;
    QString unwrappedLine_;
};

#endif // WRAPPEDSTRING_H
