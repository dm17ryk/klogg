/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
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

#ifndef KLOGG_REGULAR_EXPRESSION_PATTERN_H
#define KLOGG_REGULAR_EXPRESSION_PATTERN_H

#include <QRegularExpression>
#include <QString>
#include <atomic>
#include <cstdint>
#include <qregularexpression.h>
#include <string>
#include <tuple>

#include "uuid.h"

enum class MatchMode : uint8_t { Contains, WholeWord, WholeLine };

inline const char* matchModeName( MatchMode mode )
{
    switch ( mode ) {
    case MatchMode::Contains:
        return "contains";
    case MatchMode::WholeWord:
        return "whole_word";
    case MatchMode::WholeLine:
        return "whole_line";
    }

    return "contains";
}

struct RegularExpressionPattern {

    QString pattern;
    bool isCaseSensitive = true;
    bool isExclude = false;
    bool isBoolean = false;
    bool isPlainText = false;
    bool isPrefilter = false;
    MatchMode matchMode = MatchMode::Contains;

    RegularExpressionPattern() = default;

    explicit RegularExpressionPattern( const QString& expression )
        : RegularExpressionPattern( expression, true, false, false, false )
    {
    }

    RegularExpressionPattern( const QString& expression, bool caseSensitive, bool inverse,
                              bool boolean, bool plainText, MatchMode mode = MatchMode::Contains )
        : pattern( expression )
        , isCaseSensitive( caseSensitive )
        , isExclude( inverse )
        , isBoolean( boolean )
        , isPlainText( plainText )
        , matchMode( mode )
        , patternId_( nextId() )
    {
    }

    std::string id() const
    {
        return patternId_;
    }

    explicit operator QRegularExpression() const
    {
        auto patternOptions = QRegularExpression::UseUnicodePropertiesOption
                              | QRegularExpression::DontCaptureOption;

        if ( !isCaseSensitive ) {
            patternOptions |= QRegularExpression::CaseInsensitiveOption;
        }

        return QRegularExpression( qtPattern(), patternOptions );
    }

    QString basePattern() const
    {
        return isPlainText ? QRegularExpression::escape( pattern ) : pattern;
    }

    QString qtPattern() const
    {
        const auto base = basePattern();
        switch ( matchMode ) {
        case MatchMode::Contains:
            return base;
        case MatchMode::WholeWord:
            return QStringLiteral( "(?<!\\w)(?:" ) + base + QStringLiteral( ")(?!\\w)" );
        case MatchMode::WholeLine:
            return QStringLiteral( "\\A(?:" ) + base + QStringLiteral( ")\\r?\\z" );
        }

        return base;
    }

    QString acceleratedPattern() const
    {
        const auto base = basePattern();
        switch ( matchMode ) {
        case MatchMode::Contains:
            return base;
        case MatchMode::WholeWord:
            // Hyperscan rejects \\b in UCP mode. Consuming one non-word
            // delimiter preserves line-level existence semantics without SOM.
            return QStringLiteral( "(?:\\A|\\W)(?:" ) + base + QStringLiteral( ")(?:\\z|\\W)" );
        case MatchMode::WholeLine:
            // Raw CRLF lines retain CR after LogData removes LF.
            return QStringLiteral( "\\A(?:" ) + base + QStringLiteral( ")\\r?\\z" );
        }

        return base;
    }

    bool operator==( const RegularExpressionPattern& other ) const
    {
        return std::tie( pattern, isCaseSensitive, isExclude, isBoolean, isPlainText, isPrefilter,
                         matchMode )
               == std::tie( other.pattern, other.isCaseSensitive, other.isExclude, other.isBoolean,
                            other.isPlainText, other.isPrefilter, other.matchMode );
    }

private:
    static std::string nextId()
    {
        static std::atomic<uint> counter_ = 0;
        return std::string{ "p_" } + std::to_string( counter_++ );
    }

    std::string patternId_;
};

#endif
