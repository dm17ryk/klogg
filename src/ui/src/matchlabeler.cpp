// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "matchlabeler.h"

#include <algorithm>
#include <exception>
#include <iterator>
#include <optional>

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

// End (exclusive) of the character class that starts at `position`, or -1. A ']' right after
// '[' or '[^' is a member, as in the alternative scanner.
int classEnd( const QString& regex, int position )
{
    const auto size = static_cast<int>( regex.size() );
    auto i = position + 1;
    if ( i < size && regex.at( i ) == QLatin1Char( '^' ) ) {
        ++i;
    }
    const auto firstMember = i;
    for ( ; i < size; ++i ) {
        const auto character = regex.at( i );
        if ( character == QLatin1Char( '\\' ) ) {
            ++i;
        }
        else if ( character == QLatin1Char( ']' ) && i > firstMember ) {
            return i + 1;
        }
    }
    return -1;
}

// "(x)" -> "x" when the parentheses are one plain capturing group around everything; otherwise
// `alternative` itself.
QString withoutOuterGroup( const QString& alternative )
{
    if ( alternative.size() < 3 || alternative.front() != QLatin1Char( '(' )
         || alternative.back() != QLatin1Char( ')' ) || alternative.at( 1 ) == QLatin1Char( '?' )
         || alternative.at( 1 ) == QLatin1Char( '*' ) ) {
        return alternative;
    }
    int depth = 0;
    const auto size = static_cast<int>( alternative.size() );
    for ( int i = 0; i < size; ++i ) {
        const auto character = alternative.at( i );
        if ( character == QLatin1Char( '\\' ) ) {
            ++i;
        }
        else if ( character == QLatin1Char( '[' ) ) {
            const auto end = classEnd( alternative, i );
            if ( end < 0 ) {
                return alternative;
            }
            i = end - 1;
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

// One top-level item of a regular expression: an escape, a character class, a group or a single
// character, with the quantifier that follows it.
struct RegexToken {
    QString atom;
    QString quantifier;
};

// End (exclusive) of the group that starts at `position`, or -1.
int groupEnd( const QString& regex, int position )
{
    const auto size = static_cast<int>( regex.size() );
    int depth = 0;
    for ( int i = position; i < size; ++i ) {
        const auto character = regex.at( i );
        if ( character == QLatin1Char( '\\' ) ) {
            ++i;
        }
        else if ( character == QLatin1Char( '[' ) ) {
            const auto end = classEnd( regex, i );
            if ( end < 0 ) {
                return -1;
            }
            i = end - 1;
        }
        else if ( character == QLatin1Char( '(' ) ) {
            ++depth;
        }
        else if ( character == QLatin1Char( ')' ) && --depth == 0 ) {
            return i + 1;
        }
    }
    return -1;
}

// Length of the quantifier ("*", "+?", "{2,5}"...) that starts at `position`, or 0.
int quantifierLength( const QString& regex, int position )
{
    const auto size = static_cast<int>( regex.size() );
    if ( position >= size ) {
        return 0;
    }
    auto end = position;
    const auto character = regex.at( position );
    if ( character == QLatin1Char( '*' ) || character == QLatin1Char( '+' )
         || character == QLatin1Char( '?' ) ) {
        end = position + 1;
    }
    else if ( character == QLatin1Char( '{' ) ) {
        // "{n}", "{n,}" or "{n,m}"; anything else is a literal '{'. Read in place: a pattern
        // of many '{' stays linear.
        end = position + 1;
        const auto digitsFrom = [ &regex, size ]( int from ) {
            while ( from < size && regex.at( from ) >= QLatin1Char( '0' )
                    && regex.at( from ) <= QLatin1Char( '9' ) ) {
                ++from;
            }
            return from;
        };
        const auto afterCount = digitsFrom( end );
        if ( afterCount == end ) {
            return 0;
        }
        end = afterCount;
        if ( end < size && regex.at( end ) == QLatin1Char( ',' ) ) {
            end = digitsFrom( end + 1 );
        }
        if ( end >= size || regex.at( end ) != QLatin1Char( '}' ) ) {
            return 0;
        }
        ++end;
    }
    else {
        return 0;
    }
    if ( end < size
         && ( regex.at( end ) == QLatin1Char( '?' ) || regex.at( end ) == QLatin1Char( '+' ) ) ) {
        ++end;
    }
    return end - position;
}

// The top-level items of `regex`; false when it does not split cleanly (an unbalanced group or
// class, or a top-level '|').
bool tokenizeRegex( const QString& regex, std::vector<RegexToken>* tokens )
{
    const auto size = static_cast<int>( regex.size() );
    for ( int i = 0; i < size; ) {
        const auto character = regex.at( i );
        int end = i + 1;
        if ( character == QLatin1Char( '\\' ) ) {
            end = std::min( size, i + 2 );
        }
        else if ( character == QLatin1Char( '[' ) ) {
            end = classEnd( regex, i );
        }
        else if ( character == QLatin1Char( '(' ) ) {
            end = groupEnd( regex, i );
        }
        else if ( character == QLatin1Char( ')' ) || character == QLatin1Char( '|' ) ) {
            return false;
        }
        if ( end < 0 ) {
            return false;
        }
        RegexToken token{ regex.mid( i, end - i ), {} };
        const auto quantifier = quantifierLength( regex, end );
        token.quantifier = regex.mid( end, quantifier );
        tokens->push_back( std::move( token ) );
        i = end + quantifier;
    }
    return true;
}

// An item that names nothing on its own: an anchor, a lookaround, an option setting or a
// ".*" / ".+" wildcard.
bool isNameless( const RegexToken& token )
{
    static const QStringList anchors{ QStringLiteral( "^" ),   QStringLiteral( "$" ),
                                      QStringLiteral( "\\A" ), QStringLiteral( "\\z" ),
                                      QStringLiteral( "\\Z" ), QStringLiteral( "\\G" ),
                                      QStringLiteral( "\\b" ), QStringLiteral( "\\B" ) };
    const auto& atom = token.atom;
    if ( token.quantifier.isEmpty() && anchors.contains( atom ) ) {
        return true;
    }
    if ( atom.startsWith( QLatin1String( "(?=" ) ) || atom.startsWith( QLatin1String( "(?!" ) )
         || atom.startsWith( QLatin1String( "(?<=" ) )
         || atom.startsWith( QLatin1String( "(?<!" ) ) ) {
        return true;
    }
    if ( token.quantifier.isEmpty() && optionSettingLength( atom, 0 ) == atom.size() ) {
        return true;
    }
    return atom == QLatin1String( "." )
           && ( token.quantifier.startsWith( QLatin1Char( '*' ) )
                || token.quantifier.startsWith( QLatin1Char( '+' ) ) );
}

// The text `tokens` match when they are plain text (characters and escaped punctuation only).
std::optional<QString> literalText( const std::vector<RegexToken>& tokens )
{
    static const QString metacharacters = QStringLiteral( ".^$|?*+()[]\\" );
    QString text;
    for ( const auto& token : tokens ) {
        if ( !token.quantifier.isEmpty() ) {
            return std::nullopt;
        }
        if ( token.atom.size() == 1 && !metacharacters.contains( token.atom.front() ) ) {
            text.append( token.atom );
        }
        else if ( token.atom.size() == 2 && token.atom.front() == QLatin1Char( '\\' )
                  && !token.atom.at( 1 ).isLetterOrNumber() ) {
            text.append( token.atom.at( 1 ) );
        }
        else {
            return std::nullopt;
        }
    }
    return text;
}

// What an alternative says, without what names nothing (see alternativeDisplayName); empty when
// nothing can be left out.
QString essentialPart( const QString& regex, int depth = 0 )
{
    constexpr int MaxUnwrapDepth = 4;
    std::vector<RegexToken> tokens;
    if ( depth > MaxUnwrapDepth || !tokenizeRegex( regex, &tokens ) ) {
        return {};
    }
    const auto first = std::find_if_not( tokens.begin(), tokens.end(), isNameless );
    const auto last
        = std::find_if_not( tokens.rbegin(), std::make_reverse_iterator( first ), isNameless )
              .base();
    std::vector<RegexToken> kept( first, last );
    if ( kept.empty() ) {
        return {};
    }
    if ( kept.size() == 1 && kept.front().quantifier.isEmpty() ) {
        const auto inner = withoutOuterGroup( kept.front().atom );
        if ( inner != kept.front().atom ) {
            const auto unwrapped = essentialPart( inner, depth + 1 );
            return unwrapped.isEmpty() ? inner : unwrapped;
        }
    }
    if ( const auto text = literalText( kept ) ) {
        return text->trimmed();
    }
    QString shown;
    for ( const auto& token : kept ) {
        shown += token.atom + token.quantifier;
    }
    return shown;
}

QString filterDisplayName( const PredefinedFilter& filter )
{
    return filter.name.isEmpty() ? filter.pattern : filter.name;
}

bool hasOwnName( const PredefinedFilter& filter )
{
    return !filter.name.isEmpty() && filter.name != filter.pattern;
}

// The predefined filter a quoted term of a boolean search was built from: the term is exactly
// what the crawler quotes for it (CrawlerWidget::escapeSearchPattern), the pattern escaped first
// when the filter is plain text and the search is not. Null when no filter, or several filters
// with different names, quote to the term: the text alone cannot tell which one was ticked.
const PredefinedFilter* filterOfTerm( const QList<PredefinedFilter>& filters,
                                      const QString& termPattern, bool plainTextSearch )
{
    const PredefinedFilter* found = nullptr;
    for ( const auto& filter : filters ) {
        if ( filter.pattern.isEmpty() ) {
            continue;
        }
        const auto quoted = !filter.useRegex && !plainTextSearch
                                ? QRegularExpression::escape( filter.pattern )
                                : filter.pattern;
        if ( termPattern != quoted ) {
            continue;
        }
        if ( found != nullptr && filterDisplayName( *found ) != filterDisplayName( filter ) ) {
            return nullptr;
        }
        found = found != nullptr ? found : &filter;
    }
    return found;
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
    const auto shown = withoutOuterGroup( alternative );
    if ( alternative.startsWith( QLatin1String( "(*" ) ) ) {
        return shown;
    }
    const auto essential = essentialPart( shown );
    return essential.isEmpty() ? shown : essential;
}

MatchLabeler MatchLabeler::fromSearch( const RegularExpressionPattern& search,
                                       const QList<PredefinedFilter>& filters,
                                       const QList<PredefinedFilter>& knownFilters )
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
    // A term made of several top-level alternatives, such as the predefined filter
    // "(C_PROC:)|(EHCP)|(WiFi)", says which of them matched a line. `partOptions` are the option
    // settings in force before `regex`.
    const auto splitIntoAlternatives = [ & ]( TermSpec& spec, const QString& regex,
                                              QString partOptions ) {
        // "(*...)" settings only apply at the start of a pattern: such a pattern stays whole.
        if ( regex.startsWith( QLatin1String( "(*" ) ) ) {
            return;
        }
        for ( const auto& alternative : scanTopLevelAlternatives( regex ) ) {
            if ( alternative.text != alternative.optionSettings ) {
                spec.partPatterns.append( termPattern( partOptions + alternative.text, false ) );
                spec.partNames.append( alternativeDisplayName( alternative.text ) );
            }
            partOptions += alternative.optionSettings;
        }
        if ( spec.partPatterns.size() < 2 ) {
            spec.partPatterns.clear();
            spec.partNames.clear();
        }
    };
    // Within one regular expression a top-level "(?i)" also applies to the alternatives after
    // it; each term inherits the option settings of the terms before it, as in the search.
    const bool optionsSpanAlternatives = !search.isBoolean && !search.isPlainText;
    // Lines are labelled with filter names even when only one term is a filter.
    bool namesFilters = !filters.isEmpty();

    if ( !filters.isEmpty() ) {
        QString inheritedOptions;
        for ( const auto& filter : filters ) {
            const bool plainText = search.isPlainText || !filter.useRegex;
            const auto expression
                = plainText ? QRegularExpression::escape( filter.pattern ) : filter.pattern;
            addTerm( filterDisplayName( filter ), inheritedOptions + expression, false );
            if ( !plainText ) {
                splitIntoAlternatives( specs.back(), filter.pattern, inheritedOptions );
                specs.back().namePrefix = hasOwnName( filter );
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
                // Each quoted term is a regular expression of its own: option settings do not
                // reach the next term.
                const auto* filter = filterOfTerm( knownFilters, leaf.pattern, search.isPlainText );
                TermSpec spec;
                spec.name = filter != nullptr ? filterDisplayName( *filter ) : leaf.pattern;
                spec.pattern = leaf;
                if ( !search.isPlainText && ( filter == nullptr || filter->useRegex ) ) {
                    splitIntoAlternatives( spec, leaf.pattern, {} );
                    spec.namePrefix = filter != nullptr && hasOwnName( *filter );
                }
                namesFilters = namesFilters || filter != nullptr;
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
    labeler.namesFilters_ = namesFilters;
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
    if ( showsMatchedText() ) {
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

bool MatchLabeler::showsMatchedText() const
{
    // One typed term that is not split: its matched text says more than its pattern. A term of
    // several alternatives (one quoted regex) names the alternatives that matched instead.
    return terms_.size() == 1 && !namesFilters_ && terms_.front().parts.empty();
}

QString MatchLabeler::label( const QString& line ) const
{
    if ( showsMatchedText() ) {
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
