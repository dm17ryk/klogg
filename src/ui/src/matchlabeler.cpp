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
    std::vector<TermSpec> specs;
    for ( int i = 0; i < patterns.size(); ++i ) {
        TermSpec spec;
        spec.name = names.at( i );
        spec.pattern = patterns.at( i );
        specs.push_back( std::move( spec ) );
    }
    return fromSpecs( specs );
}

MatchLabeler MatchLabeler::fromSpecs( const std::vector<TermSpec>& specs )
{
    const auto compiled = []( const RegularExpressionPattern& pattern ) {
        const auto expression = static_cast<QRegularExpression>( pattern );
        if ( !expression.isValid() ) {
            LOG_DEBUG << "Match labeler term rejected: " << pattern.pattern << ": "
                      << expression.errorString();
        }
        return expression;
    };

    MatchLabeler labeler;
    for ( const auto& spec : specs ) {
        Term term{ spec.name, compiled( spec.pattern ), {}, spec.namePrefix };
        if ( !term.expression.isValid() ) {
            return {};
        }
        for ( int i = 0; i < spec.partPatterns.size(); ++i ) {
            auto expression = compiled( spec.partPatterns.at( i ) );
            if ( !expression.isValid() ) {
                // The filter still matches as a whole; it is just not split.
                term.parts.clear();
                break;
            }
            term.parts.push_back( { spec.partNames.at( i ), std::move( expression ) } );
        }
        labeler.terms_.push_back( std::move( term ) );
    }
    return labeler;
}

QString MatchLabeler::alternativeDisplayName( const QString& alternative )
{
    // "(x)" -> "x" when the parentheses are one plain capturing group around everything.
    if ( alternative.size() < 3 || alternative.front() != QLatin1Char( '(' )
         || alternative.back() != QLatin1Char( ')' ) || alternative.at( 1 ) == QLatin1Char( '?' )
         || alternative.at( 1 ) == QLatin1Char( '*' ) ) {
        return alternative;
    }
    int depth = 0;
    bool inClass = false;
    const auto size = static_cast<int>( alternative.size() );
    for ( int i = 0; i < size; ++i ) {
        const auto character = alternative.at( i );
        if ( character == QLatin1Char( '\\' ) ) {
            ++i;
            continue;
        }
        if ( inClass ) {
            inClass = character != QLatin1Char( ']' );
            continue;
        }
        if ( character == QLatin1Char( '[' ) ) {
            inClass = true;
        }
        else if ( character == QLatin1Char( '(' ) ) {
            ++depth;
        }
        else if ( character == QLatin1Char( ')' ) ) {
            --depth;
            if ( depth == 0 && i != size - 1 ) {
                return alternative; // "(a)(b)": the first group closes early
            }
        }
    }
    return depth == 0 ? alternative.mid( 1, size - 2 ) : alternative;
}

MatchLabeler MatchLabeler::fromSearch( const RegularExpressionPattern& search,
                                       const QList<PredefinedFilter>& filters )
{
    // An inverse search selects lines because nothing matched them: there is nothing to name.
    if ( search.pattern.isEmpty() || search.isExclude ) {
        return {};
    }

    std::vector<TermSpec> specs;
    const auto termPattern = [ &search ]( const QString& pattern, bool plainText ) {
        return RegularExpressionPattern( pattern, search.isCaseSensitive, false, false, plainText,
                                         search.matchMode );
    };
    const auto addTerm = [ & ]( const QString& name, const QString& pattern, bool plainText ) {
        TermSpec spec;
        spec.name = name;
        spec.pattern = termPattern( pattern, plainText );
        specs.push_back( std::move( spec ) );
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
            if ( !plainText && !filter.pattern.startsWith( QLatin1String( "(*" ) ) ) {
                // A filter of several alternatives, such as "(C_PROC:)|(EHCP)|(WiFi)", says
                // which of them matched a line.
                auto& spec = specs.back();
                QString partOptions = inheritedOptions;
                for ( const auto& alternative : scanTopLevelAlternatives( filter.pattern ) ) {
                    if ( alternative.text != alternative.optionSettings ) {
                        spec.partPatterns.append(
                            termPattern( partOptions + alternative.text, false ) );
                        spec.partNames.append( alternativeDisplayName( alternative.text ) );
                    }
                    partOptions += alternative.optionSettings;
                }
                if ( spec.partPatterns.size() < 2 ) {
                    spec.partPatterns.clear();
                    spec.partNames.clear();
                }
                spec.namePrefix = !filter.name.isEmpty() && filter.name != filter.pattern;
            }
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
                TermSpec spec;
                spec.name = leaf.pattern;
                spec.pattern = leaf;
                specs.push_back( std::move( spec ) );
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
                addTerm( alternativeDisplayName( alternative.text ),
                         inheritedOptions + alternative.text, false );
            }
            inheritedOptions += alternative.optionSettings;
        }
    }
    else {
        addTerm( search.pattern, search.pattern, search.isPlainText );
    }

    if ( specs.empty() ) {
        return {};
    }
    auto labeler = fromSpecs( specs );
    if ( labeler.isEmpty() && specs.size() > 1 ) {
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
        if ( term.parts.empty() ) {
            longest = std::max( longest, static_cast<int>( term.name.size() ) );
            continue;
        }
        const auto prefix = term.namePrefix ? static_cast<int>( term.name.size() ) + 2 : 0;
        for ( const auto& part : term.parts ) {
            longest = std::max( longest, prefix + static_cast<int>( part.name.size() ) );
        }
    }
    return std::clamp( longest + 2, MinChars, MaxChars );
}

QString MatchLabeler::termLabel( const Term& term, const QString& line ) const
{
    if ( term.parts.empty() ) {
        return term.name;
    }
    QStringList matchedParts;
    for ( const auto& part : term.parts ) {
        if ( part.expression.match( line ).hasMatch() ) {
            matchedParts.append( part.name );
        }
    }
    if ( matchedParts.isEmpty() ) {
        return term.name;
    }
    const auto parts = matchedParts.join( QStringLiteral( ", " ) );
    return term.namePrefix ? term.name + QStringLiteral( ": " ) + parts : parts;
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
            matched.append( termLabel( term, line ) );
        }
    }
    return elided( matched.join( QStringLiteral( ", " ) ) );
}
