// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "matchlabeler.h"

#include <algorithm>
#include <exception>

#include "booleanparser.h"
#include "log.h"

namespace {
QString elided( QString text )
{
    if ( text.size() > MatchLabeler::MaxLabelLength ) {
        text.truncate( MatchLabeler::MaxLabelLength - 1 );
        text.append( QChar( 0x2026 ) );
    }
    return text;
}
} // namespace

QStringList MatchLabeler::splitTopLevelAlternatives( const QString& regex )
{
    QStringList alternatives;
    QString current;
    int groupDepth = 0;
    bool inClass = false;
    for ( int i = 0; i < regex.size(); ++i ) {
        const auto character = regex.at( i );
        if ( character == QLatin1Char( '\\' ) && i + 1 < regex.size() ) {
            current.append( character );
            current.append( regex.at( ++i ) );
            continue;
        }
        if ( inClass ) {
            // A ']' right after '[' or '[^' is a literal member of the class.
            const bool classStart = current.endsWith( QLatin1Char( '[' ) )
                                    || current.endsWith( QLatin1String( "[^" ) );
            if ( character == QLatin1Char( ']' ) && !classStart )
                inClass = false;
            current.append( character );
            continue;
        }
        if ( character == QLatin1Char( '[' ) )
            inClass = true;
        else if ( character == QLatin1Char( '(' ) )
            ++groupDepth;
        else if ( character == QLatin1Char( ')' ) && groupDepth > 0 )
            --groupDepth;
        else if ( character == QLatin1Char( '|' ) && groupDepth == 0 ) {
            alternatives.append( current );
            current.clear();
            continue;
        }
        current.append( character );
    }
    alternatives.append( current );
    return alternatives;
}

MatchLabeler MatchLabeler::fromTerms( const QList<RegularExpressionPattern>& patterns,
                                      const QStringList& names )
{
    MatchLabeler labeler;
    for ( int i = 0; i < patterns.size(); ++i ) {
        const auto expression = static_cast<QRegularExpression>( patterns.at( i ) );
        if ( !expression.isValid() ) {
            LOG_DEBUG << "Match labeler term rejected: " << patterns.at( i ).pattern << ": "
                      << expression.errorString();
            return {};
        }
        labeler.terms_.push_back( { names.at( i ), expression } );
    }
    return labeler;
}

MatchLabeler MatchLabeler::fromSearch( const RegularExpressionPattern& search,
                                       const QList<PredefinedFilter>& filters )
{
    // An inverse search selects lines because nothing matched them: there is nothing to name.
    if ( search.pattern.isEmpty() || search.isExclude ) {
        return {};
    }

    QList<RegularExpressionPattern> patterns;
    QStringList names;
    const auto addTerm = [ & ]( const QString& name, const QString& pattern, bool plainText ) {
        patterns.append( RegularExpressionPattern( pattern, search.isCaseSensitive, false, false,
                                                   plainText, search.matchMode ) );
        names.append( name );
    };

    if ( !filters.isEmpty() ) {
        for ( const auto& filter : filters ) {
            addTerm( filter.name.isEmpty() ? filter.pattern : filter.name, filter.pattern,
                     search.isPlainText || !filter.useRegex );
        }
    }
    else if ( search.isBoolean ) {
        try {
            auto expression = search.pattern;
            const auto leaves = parseBooleanExpressions( expression, search.isCaseSensitive,
                                                         search.isPlainText, search.matchMode );
            for ( const auto& leaf : leaves ) {
                patterns.append( leaf );
                names.append( leaf.pattern );
            }
        } catch ( const std::exception& error ) {
            LOG_DEBUG << "Match labeler cannot parse boolean search: " << error.what();
            return {};
        }
    }
    else if ( !search.isPlainText ) {
        for ( const auto& alternative : splitTopLevelAlternatives( search.pattern ) ) {
            addTerm( alternative, alternative, false );
        }
    }
    else {
        addTerm( search.pattern, search.pattern, true );
    }

    if ( patterns.isEmpty() ) {
        return {};
    }
    auto labeler = fromTerms( patterns, names );
    if ( labeler.isEmpty() && patterns.size() > 1 ) {
        // A split broke a construct that spans alternatives; label with the whole pattern.
        return fromTerms( { search }, { search.pattern } );
    }
    return labeler;
}

int MatchLabeler::preferredColumnChars() const
{
    constexpr int SingleTermChars = 24;
    constexpr int MinChars = 8;
    constexpr int MaxChars = 32;
    if ( terms_.size() <= 1 ) {
        return terms_.empty() ? 0 : SingleTermChars;
    }
    int longest = 0;
    for ( const auto& term : terms_ ) {
        longest = std::max( longest, static_cast<int>( term.name.size() ) );
    }
    return std::clamp( longest + 2, MinChars, MaxChars );
}

QString MatchLabeler::label( const QString& line ) const
{
    if ( terms_.size() == 1 ) {
        const auto match = terms_.front().expression.match( line );
        if ( !match.hasMatch() ) {
            return {};
        }
        const auto text = match.captured( 0 );
        // A match of nothing or of the whole line says less than the pattern itself.
        const auto shown = text.isEmpty() || text.size() == line.size() ? terms_.front().name
                                                                       : text;
        return elided( shown );
    }

    QStringList matched;
    for ( const auto& term : terms_ ) {
        if ( term.expression.match( line ).hasMatch() ) {
            matched.append( term.name );
        }
    }
    return elided( matched.join( QStringLiteral( ", " ) ) );
}
