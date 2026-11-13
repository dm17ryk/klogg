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

bool isOptionCharacter( QChar character )
{
    return QStringLiteral( "imnsxJU^-" ).contains( character );
}

// Length of the inline option setting, such as "(?i)" or "(?im-s)", that starts at `position`,
// or 0. A "(?i:...)" group is not one: its options stay inside the group.
int optionSettingLength( const QString& regex, int position )
{
    const auto regexSize = static_cast<int>( regex.size() );
    const auto lettersStart = position + 2;
    if ( lettersStart >= regexSize || regex.at( position ) != QLatin1Char( '(' )
         || regex.at( position + 1 ) != QLatin1Char( '?' ) ) {
        return 0;
    }
    auto end = lettersStart;
    while ( end < regexSize && isOptionCharacter( regex.at( end ) ) ) {
        ++end;
    }
    if ( end == lettersStart || end >= regexSize || regex.at( end ) != QLatin1Char( ')' ) ) {
        return 0;
    }
    return end - position + 1;
}
} // namespace

std::vector<MatchLabeler::Alternative>
MatchLabeler::scanTopLevelAlternatives( const QString& regex )
{
    std::vector<Alternative> alternatives( 1 );
    int groupDepth = 0;
    bool inClass = false;
    // Index in the current alternative of the first member of the open character class.
    qsizetype classMembersStart = 0;
    const auto regexSize = static_cast<int>( regex.size() );
    for ( int i = 0; i < regexSize; ++i ) {
        auto& current = alternatives.back().text;
        const auto character = regex.at( i );
        if ( character == QLatin1Char( '\\' ) && i + 1 < regexSize ) {
            current.append( character );
            current.append( regex.at( ++i ) );
            continue;
        }
        if ( inClass ) {
            // A ']' as the first member (after '[' or '[^') is a literal.
            if ( character == QLatin1Char( '^' ) && current.size() == classMembersStart
                 && current.endsWith( QLatin1Char( '[' ) ) ) {
                ++classMembersStart;
            }
            else if ( character == QLatin1Char( ']' ) && current.size() > classMembersStart ) {
                inClass = false;
            }
            current.append( character );
            continue;
        }
        if ( character == QLatin1Char( '[' ) ) {
            inClass = true;
            classMembersStart = current.size() + 1;
        }
        else if ( character == QLatin1Char( '(' ) ) {
            const auto settingLength = groupDepth == 0 ? optionSettingLength( regex, i ) : 0;
            if ( settingLength > 0 ) {
                const auto setting = regex.mid( i, settingLength );
                current.append( setting );
                alternatives.back().optionSettings.append( setting );
                i += settingLength - 1;
                continue;
            }
            ++groupDepth;
        }
        else if ( character == QLatin1Char( ')' ) && groupDepth > 0 ) {
            --groupDepth;
        }
        else if ( character == QLatin1Char( '|' ) && groupDepth == 0 ) {
            alternatives.emplace_back();
            continue;
        }
        current.append( character );
    }
    return alternatives;
}

QStringList MatchLabeler::splitTopLevelAlternatives( const QString& regex )
{
    QStringList texts;
    for ( const auto& alternative : scanTopLevelAlternatives( regex ) ) {
        texts.append( alternative.text );
    }
    return texts;
}

QString MatchLabeler::topLevelOptionSettings( const QString& regex )
{
    QString settings;
    for ( const auto& alternative : scanTopLevelAlternatives( regex ) ) {
        settings.append( alternative.optionSettings );
    }
    return settings;
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
    // Within one regular expression a top-level "(?i)" also applies to the alternatives after
    // it; each term inherits the option settings of the terms before it, as in the search.
    const bool optionsSpanAlternatives = !search.isBoolean && !search.isPlainText;

    if ( !filters.isEmpty() ) {
        QString inheritedOptions;
        for ( const auto& filter : filters ) {
            const bool plainText = search.isPlainText || !filter.useRegex;
            const auto expression
                = plainText ? QRegularExpression::escape( filter.pattern ) : filter.pattern;
            addTerm( filter.name.isEmpty() ? filter.pattern : filter.name,
                     inheritedOptions + expression, false );
            if ( optionsSpanAlternatives && !plainText ) {
                inheritedOptions += topLevelOptionSettings( filter.pattern );
            }
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
    else if ( !search.isPlainText && !search.pattern.startsWith( QLatin1String( "(*" ) ) ) {
        // A pattern starting with "(*...)" settings is kept whole: they only apply at its start.
        QString inheritedOptions;
        for ( const auto& alternative : scanTopLevelAlternatives( search.pattern ) ) {
            // An empty alternative (or one of option settings only) matches every line and
            // names nothing.
            if ( alternative.text != alternative.optionSettings ) {
                addTerm( alternative.text, inheritedOptions + alternative.text, false );
            }
            inheritedOptions += alternative.optionSettings;
        }
    }
    else {
        addTerm( search.pattern, search.pattern, search.isPlainText );
    }

    if ( patterns.isEmpty() ) {
        return {};
    }
    auto labeler = fromTerms( patterns, names );
    if ( labeler.isEmpty() && patterns.size() > 1 ) {
        // A split broke a construct that spans alternatives; label with the whole pattern.
        return fromTerms( { search }, { search.pattern } );
    }
    labeler.namesFilters_ = !filters.isEmpty();
    return labeler;
}

int MatchLabeler::preferredColumnChars() const
{
    constexpr int SingleTermChars = 24;
    constexpr int MinChars = 8;
    constexpr int MaxChars = 32;
    if ( terms_.empty() ) {
        return 0;
    }
    if ( terms_.size() == 1 && !namesFilters_ ) {
        return SingleTermChars;
    }
    int longest = 0;
    for ( const auto& term : terms_ ) {
        longest = std::max( longest, static_cast<int>( term.name.size() ) );
    }
    return std::clamp( longest + 2, MinChars, MaxChars );
}

QString MatchLabeler::label( const QString& line ) const
{
    if ( terms_.size() == 1 && !namesFilters_ ) {
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
