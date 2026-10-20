// Copyright (C) 2021 Anton Filimonov and other contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "booleanparser.h"
#include "log.h"
#include <stdexcept>
#include <type_safe/narrow_cast.hpp>

klogg::vector<RegularExpressionPattern> parseBooleanExpressions( QString& pattern,
                                                                 bool isCaseSensitive,
                                                                 bool isPlainText,
                                                                 MatchMode matchMode )
{
    if ( !pattern.contains( '"' ) ) {
        throw std::runtime_error( "Patterns must be enclosed in quotes" );
    }

    klogg::vector<RegularExpressionPattern> subPatterns;
    subPatterns.reserve( static_cast<size_t>( pattern.size() ) );

    int currentIndex = 0;
    int leftQuote = -1;
    int rightQuote = -1;

    while ( currentIndex < pattern.size() ) {
        leftQuote = type_safe::narrow_cast<int>( pattern.indexOf( QChar( '"' ), currentIndex ) );
        if ( leftQuote < 0 ) {
            break;
        }

        currentIndex = leftQuote + 1;
        if ( leftQuote > 0 && pattern[ leftQuote - 1 ] == QChar( '\\' ) ) {
            leftQuote = -1;
            continue;
        }

        while ( currentIndex < pattern.size() ) {
            rightQuote
                = type_safe::narrow_cast<int>( pattern.indexOf( QChar( '"' ), currentIndex ) );
            if ( rightQuote < 0 ) {
                break;
            }

            currentIndex = rightQuote + 1;
            if ( rightQuote > 0 && pattern[ rightQuote - 1 ] == QChar( '\\' ) ) {
                rightQuote = -1;
                continue;
            }

            break;
        }

        if ( rightQuote < 0 ) {
            break;
        }

        const auto subPatternLength = rightQuote - leftQuote - 1;
        auto subPattern = pattern.mid( leftQuote + 1, subPatternLength );
        subPattern.replace( "\\\"", "\"" );

        subPatterns.emplace_back( subPattern, isCaseSensitive, false, false, isPlainText,
                                  matchMode );

        pattern.replace( leftQuote, subPatternLength + 2,
                         QString::fromStdString( subPatterns.back().id() ) );

        currentIndex = 0;
        leftQuote = -1;
        rightQuote = -1;
    }

    if ( pattern.contains( '"' ) ) {
        throw std::runtime_error( "Pattern has unmatched quotes" );
    }

    pattern = pattern.toLower();
    LOG_DEBUG << "Parsed Boolean subpatterns=" << subPatterns.size();
    QRegularExpression finalPatternCheck( "^(and|nand|or|nor|xor|xnor|not|[ ()!|&]|p_[0-9]+)+$" );
    if ( !finalPatternCheck.match( pattern ).hasMatch() ) {
        throw std::runtime_error( "Sub-patterns must be enclosed in quotes" );
    }

    return subPatterns;
}
