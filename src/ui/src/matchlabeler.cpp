// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "matchlabeler.h"

#include <algorithm>
#include <exception>
#include <iterator>
#include <optional>

#include <QCoreApplication>

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

// Free-spacing syntax ("(?x)", "(?ix)", "(?x:"): '#' starts a comment that may hold a '|', so
// such a pattern is not split into alternatives. "(?-x)" turns it off and does not count.
bool usesFreeSpacing( const QString& regex )
{
    static const QRegularExpression setting(
        QStringLiteral( "\\(\\?[A-Za-z^]*x[A-Za-z]*(-[A-Za-z]*)?[):]" ) );
    return setting.match( regex ).hasMatch();
}

// Whether `regex` refers to its groups by number or calls them: "\1", "\g2", "\g{-1}", "\k",
// "(?1)", "(?R)", "(?&name)", "(?P>name)", "(?P=name)". Compiled alone, an alternative numbers its
// groups from one again and matches what the whole pattern would not, so such a pattern is not
// split. (An escaped backslash before a digit also counts; it is only left whole.)
bool referencesGroups( const QString& regex )
{
    static const QRegularExpression reference(
        QStringLiteral( "\\\\(?:[1-9]|g|k)|\\(\\?(?:[R+&]|-?[0-9]|P[>=])" ) );
    return reference.match( regex ).hasMatch();
}

// The group of a focused expression (see focusedRegex) that captures the text that matters.
const QString FocusGroup = QStringLiteral( "cilogg_matched" );

QString joinedTokens( std::vector<RegexToken>::const_iterator from,
                      std::vector<RegexToken>::const_iterator to )
{
    QString text;
    for ( ; from != to; ++from ) {
        text += from->atom + from->quantifier;
    }
    return text;
}

// `regex` with what names nothing at its ends (anchors, lookarounds, option settings, ".*") left
// outside a group named FocusGroup around the rest, so that group captures the text that
// matters: "(^(?!.*X).*ERROR)" -> "(^(?!.*X).*(?<cilogg_matched>ERROR))". The capture changes no
// match. Empty when nothing is left out, when everything would be, or when `regex` is not one
// sequence of items that can safely take a group (an option setting between the items would no
// longer reach the items after the group).
QString focusedRegex( const QString& regex, int depth = 0 )
{
    constexpr int MaxUnwrapDepth = 4;
    std::vector<RegexToken> tokens;
    if ( depth > MaxUnwrapDepth || !tokenizeRegex( regex, &tokens ) ) {
        return {};
    }
    const std::vector<RegexToken>& items = tokens;
    const auto first = std::find_if_not( items.begin(), items.end(), isNameless );
    const auto last
        = std::find_if_not( items.rbegin(), std::make_reverse_iterator( first ), isNameless )
              .base();
    if ( first == last ) {
        return {};
    }
    if ( std::any_of( first, last, []( const RegexToken& token ) {
             return token.quantifier.isEmpty() && optionSettingLength( token.atom, 0 ) > 0;
         } ) ) {
        return {};
    }
    if ( std::next( first ) == last && first->quantifier.isEmpty() ) {
        const auto inner = withoutOuterGroup( first->atom );
        if ( inner != first->atom ) {
            const auto focused = focusedRegex( inner, depth + 1 );
            if ( !focused.isEmpty() ) {
                return joinedTokens( items.begin(), first ) + QLatin1Char( '(' ) + focused
                       + QLatin1Char( ')' ) + joinedTokens( last, items.end() );
            }
        }
    }
    if ( first == items.begin() && last == items.end() ) {
        return {};
    }
    return joinedTokens( items.begin(), first ) + QStringLiteral( "(?<" ) + FocusGroup
           + QLatin1Char( '>' ) + joinedTokens( first, last ) + QLatin1Char( ')' )
           + joinedTokens( last, items.end() );
}

// The expression of `pattern` that also captures the text that matters (see focusedRegex), or
// the plain expression of `pattern` when there is nothing to leave out or it cannot be done.
QRegularExpression focusedExpression( const RegularExpressionPattern& pattern )
{
    auto expression = static_cast<QRegularExpression>( pattern );
    const auto& regex = pattern.pattern;
    if ( pattern.isPlainText || !expression.isValid() || regex.startsWith( QLatin1String( "(*" ) )
         || usesFreeSpacing( regex ) || referencesGroups( regex )
         || regex.contains( QLatin1String( "(?(" ) ) ) {
        return expression;
    }
    const auto focused = focusedRegex( regex );
    if ( focused.isEmpty() ) {
        return expression;
    }
    auto focusedPattern = pattern;
    focusedPattern.pattern = focused;
    auto withFocus = static_cast<QRegularExpression>( focusedPattern );
    // A group of that name in the pattern itself makes the focused expression invalid.
    return withFocus.isValid() ? withFocus : expression;
}

// What `match` shows: the focused capture when there is one, else the whole match, without the
// spaces around it; `shown` when that leaves nothing or is the whole line, since it says no more
// than the pattern.
QString shownText( const QRegularExpressionMatch& match, const QString& line, const QString& shown )
{
    const auto text = match.capturedStart( FocusGroup ) >= 0 ? match.captured( FocusGroup )
                                                             : match.captured( 0 );
    const auto trimmed = text.trimmed();
    return elided( trimmed.isEmpty() || text.size() == line.size() ? shown : trimmed );
}

QString toolTipOf( const std::vector<MatchLabeler::Match>& matches )
{
    if ( matches.empty() ) {
        return {};
    }
    QStringList shownRows;
    int hidden = 0;
    for ( const auto& match : matches ) {
        const auto row
            = QStringLiteral( "<tr><td>%1&nbsp;&nbsp;</td><td>%2</td></tr>" )
                  .arg( elided( match.name ).toHtmlEscaped(), match.text.toHtmlEscaped() );
        if ( shownRows.contains( row ) ) {
            continue;
        }
        if ( shownRows.size() >= MatchLabeler::MaxToolTipRows ) {
            ++hidden;
            continue;
        }
        shownRows.append( row );
    }
    if ( hidden > 0 ) {
        shownRows.append( QStringLiteral( "<tr><td colspan=\"2\">%1</td></tr>" )
                              .arg( QCoreApplication::translate( "MatchLabeler", "and %n more",
                                                                 nullptr, hidden ) ) );
    }
    return QStringLiteral( "<qt><table cellspacing=\"0\" cellpadding=\"1\">"
                           "<tr><th align=\"left\">%1&nbsp;&nbsp;</th>"
                           "<th align=\"left\">%2</th></tr>%3</table></qt>" )
        .arg( QCoreApplication::translate( "MatchLabeler", "Matched by" ),
              QCoreApplication::translate( "MatchLabeler", "Text" ), shownRows.join( QString{} ) );
}

// How `alternatives` are shown. Short names that would be the same for two alternatives (they
// differ only in what the short form leaves out, such as a lookahead) keep the longer form, so
// the label still says which one matched.
QStringList distinctDisplayNames( const QStringList& alternatives )
{
    QStringList shortNames;
    for ( const auto& alternative : alternatives ) {
        shortNames.append( MatchLabeler::alternativeDisplayName( alternative ) );
    }
    auto names = shortNames;
    for ( int i = 0; i < names.size(); ++i ) {
        if ( shortNames.count( shortNames.at( i ) ) > 1 ) {
            names[ i ] = withoutOuterGroup( alternatives.at( i ) );
        }
    }
    return names;
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
        if ( character == QLatin1Char( '\\' ) && i + 1 < regexSize
             && regex.at( i + 1 ) == QLatin1Char( 'Q' ) && !inClass ) {
            // "\Q...\E" quotes everything up to "\E" (or the end): a '|' there is literal.
            const auto quoteEnd = regex.indexOf( QLatin1String( "\\E" ), i + 2 );
            const auto end = quoteEnd < 0 ? regexSize : static_cast<int>( quoteEnd ) + 2;
            current.append( regex.mid( i, end - i ) );
            i = end - 1;
            continue;
        }
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
        const auto expression = focusedExpression( pattern );
        if ( !expression.isValid() ) {
            LOG_DEBUG << "Match labeler term rejected: " << pattern.pattern << ": "
                      << expression.errorString();
        }
        return expression;
    };

    MatchLabeler labeler;
    for ( const auto& spec : specs ) {
        const auto shownPattern = spec.pattern.isPlainText
                                      ? spec.pattern.pattern
                                      : alternativeDisplayName( spec.pattern.pattern );
        Term term{ spec.name, compiled( spec.pattern ), {}, spec.namePrefix, shownPattern };
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
        // "(*...)" settings only apply at the start of a pattern: such a pattern stays whole, as
        // does one in free-spacing syntax or one that refers to its groups.
        if ( regex.startsWith( QLatin1String( "(*" ) ) || usesFreeSpacing( partOptions + regex )
             || referencesGroups( regex ) ) {
            return;
        }
        QStringList texts;
        for ( const auto& alternative : scanTopLevelAlternatives( regex ) ) {
            if ( alternative.text != alternative.optionSettings ) {
                spec.partPatterns.append( termPattern( partOptions + alternative.text, false ) );
                texts.append( alternative.text );
            }
            partOptions += alternative.optionSettings;
        }
        spec.partNames = distinctDisplayNames( texts );
        if ( spec.partPatterns.size() < 2 ) {
            spec.partPatterns.clear();
            spec.partNames.clear();
        }
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
                specs.push_back( std::move( spec ) );
            }
        } catch ( const std::exception& error ) {
            LOG_DEBUG << "Match labeler cannot parse boolean search: " << error.what();
            return {};
        }
    }
    else if ( !search.isPlainText && !search.pattern.startsWith( QLatin1String( "(*" ) )
              && !usesFreeSpacing( search.pattern ) && !referencesGroups( search.pattern ) ) {
        // A pattern starting with "(*...)" settings is kept whole: they only apply at its start.
        // So is one in free-spacing syntax, whose comments may hold a '|'.
        QString inheritedOptions;
        QStringList texts;
        QStringList expressions;
        for ( const auto& alternative : scanTopLevelAlternatives( search.pattern ) ) {
            // An empty alternative (or one of option settings only) matches every line and
            // names nothing.
            if ( alternative.text != alternative.optionSettings ) {
                texts.append( alternative.text );
                expressions.append( inheritedOptions + alternative.text );
            }
            inheritedOptions += alternative.optionSettings;
        }
        const auto names = distinctDisplayNames( texts );
        for ( int i = 0; i < names.size(); ++i ) {
            addTerm( names.at( i ), expressions.at( i ), false );
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
    // One pattern of one alternative: what it matches can be anything.
    if ( terms_.size() == 1 && terms_.front().parts.empty() ) {
        return SingleTermChars;
    }
    // The cell shows matched text: the alternatives as written are the best guess of its size.
    int longest = 0;
    for ( const auto& term : terms_ ) {
        if ( term.parts.empty() ) {
            longest = std::max( longest, static_cast<int>( term.shownPattern.size() ) );
        }
        for ( const auto& part : term.parts ) {
            longest = std::max( longest, static_cast<int>( part.name.size() ) );
        }
    }
    return std::clamp( longest + 2, MinChars, MaxChars );
}

std::vector<MatchLabeler::Match> MatchLabeler::matches( const QString& line ) const
{
    std::vector<Match> found;
    for ( const auto& term : terms_ ) {
        const auto termMatch = term.expression.match( line );
        if ( !termMatch.hasMatch() ) {
            continue;
        }
        bool partMatched = false;
        for ( const auto& part : term.parts ) {
            const auto partMatch = part.expression.match( line );
            if ( !partMatch.hasMatch() ) {
                continue;
            }
            partMatched = true;
            found.push_back(
                { term.namePrefix ? term.name + QStringLiteral( ": " ) + part.name : part.name,
                  shownText( partMatch, line, part.name ) } );
        }
        if ( !partMatched ) {
            found.push_back( { term.name, shownText( termMatch, line, term.shownPattern ) } );
        }
    }
    return found;
}

MatchLabeler::Annotation MatchLabeler::annotate( const QString& line ) const
{
    const auto found = matches( line );
    QStringList texts;
    for ( const auto& match : found ) {
        if ( !texts.contains( match.text ) ) {
            texts.append( match.text );
        }
    }
    return { elided( texts.join( QStringLiteral( ", " ) ) ), toolTipOf( found ) };
}

QString MatchLabeler::label( const QString& line ) const
{
    return annotate( line ).text;
}
