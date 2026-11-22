// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch.hpp>

#include "matchlabeler.h"

namespace {
RegularExpressionPattern regexSearch( const QString& pattern, bool caseSensitive = true )
{
    return RegularExpressionPattern( pattern, caseSensitive, false, false, false );
}

PredefinedFilter filter( const QString& name, const QString& pattern, bool useRegex )
{
    return PredefinedFilter{ name, name, pattern, useRegex };
}
} // namespace

TEST_CASE( "labels_predefined_filters", "[workbench][U14]" )
{
    const QList<PredefinedFilter> filters{ filter( "Panel ready", "Panel Ready", false ),
                                           filter( "Errors", "ERR(OR)?:", true ) };
    // The search text the crawler builds from these filters with regex search enabled.
    const auto labeler
        = MatchLabeler::fromSearch( regexSearch( "Panel Ready|ERR(OR)?:" ), filters );
    REQUIRE_FALSE( labeler.isEmpty() );
    REQUIRE( labeler.label( "12:00 Panel Ready" ) == "Panel ready" );
    REQUIRE( labeler.label( "12:01 ERROR: modem" ) == "Errors" );
    REQUIRE( labeler.label( "12:02 Panel Ready after ERR: reset" ) == "Panel ready, Errors" );
    REQUIRE( labeler.label( "12:03 nothing" ).isEmpty() );
    REQUIRE( labeler.preferredColumnChars() >= QString( "Panel ready" ).size() );
}

TEST_CASE( "labels_regex_alternatives", "[workbench][U14]" )
{
    REQUIRE( MatchLabeler::splitTopLevelAlternatives( "a|b(c|d)|[x|y]|e\\|f" )
             == QStringList{ "a", "b(c|d)", "[x|y]", "e\\|f" } );
    REQUIRE( MatchLabeler::splitTopLevelAlternatives( "[]|]x|y" ) == QStringList{ "[]|]x", "y" } );
    REQUIRE( MatchLabeler::splitTopLevelAlternatives( "[^]|]x|y" )
             == QStringList{ "[^]|]x", "y" } );
    // A literal '[' inside a class does not reopen it.
    REQUIRE( MatchLabeler::splitTopLevelAlternatives( "[a[]|b" ) == QStringList{ "[a[]", "b" } );

    // Empty alternatives match everything and name nothing.
    const auto withEmpty = MatchLabeler::fromSearch( regexSearch( "a||b|" ) );
    REQUIRE( withEmpty.label( "only b here" ) == "b" );
    REQUIRE( withEmpty.label( "a and b" ) == "a, b" );

    const auto labeler = MatchLabeler::fromSearch( regexSearch( "WARN|ERROR", false ) );
    REQUIRE( labeler.label( "error: disk" ) == "ERROR" );
    REQUIRE( labeler.label( "warn and error" ) == "WARN, ERROR" );
}

TEST_CASE( "labels_boolean_terms", "[workbench][U14]" )
{
    const auto search = RegularExpressionPattern( R"("connect" or "timeout")", true, false, true,
                                                  false );
    const auto labeler = MatchLabeler::fromSearch( search );
    REQUIRE_FALSE( labeler.isEmpty() );
    REQUIRE( labeler.label( "socket timeout after 5 s" ) == "timeout" );
    REQUIRE( labeler.label( "connect retry" ) == "connect" );
}

TEST_CASE( "labels_single_term_shows_match", "[workbench][U14]" )
{
    const auto labeler = MatchLabeler::fromSearch( regexSearch( "id=\\d+" ) );
    REQUIRE( labeler.label( "event id=4711 done" ) == "id=4711" );
    // A match of the whole line says nothing new; the pattern is shown instead.
    REQUIRE( labeler.label( "id=1" ) == "id=\\d+" );

    const auto plain = MatchLabeler::fromSearch(
        RegularExpressionPattern( "a.b", true, false, false, true ) );
    REQUIRE( plain.label( "xa.by" ) == "a.b" );
    REQUIRE( plain.label( "xacby" ).isEmpty() );

    const auto longMatch = MatchLabeler::fromSearch( regexSearch( "x+" ) );
    const auto label
        = longMatch.label( "<" + QString( MatchLabeler::MaxLabelLength + 50, 'x' ) + ">" );
    REQUIRE( label.size() == MatchLabeler::MaxLabelLength );
}

TEST_CASE( "labels_single_predefined_filter_by_name", "[workbench][U14]" )
{
    // One predefined filter still names the filter, not the matched text.
    const auto labeler = MatchLabeler::fromSearch( regexSearch( "WARN" ),
                                                   { filter( "Warnings", "WARN", false ) } );
    REQUIRE_FALSE( labeler.isEmpty() );
    REQUIRE( labeler.label( "12:00 WARN low battery" ) == "Warnings" );
    REQUIRE( labeler.label( "12:01 ERROR modem" ).isEmpty() );
    REQUIRE( labeler.preferredColumnChars() >= QString( "Warnings" ).size() );
}

TEST_CASE( "labels_keep_inline_options_across_alternatives", "[workbench][U14]" )
{
    REQUIRE( MatchLabeler::topLevelOptionSettings( "(?i)warn" ) == "(?i)" );
    REQUIRE( MatchLabeler::topLevelOptionSettings( "a|(?im)b(?-m)" ) == "(?im)(?-m)" );
    // Scoped groups, nested settings and escaped parentheses are not top-level settings.
    REQUIRE( MatchLabeler::topLevelOptionSettings( "a(?i:b)(c(?m)d)\\(?x)" ).isEmpty() );

    // A top-level (?i) applies to the alternatives after it, as in the search itself.
    const auto labeler = MatchLabeler::fromSearch( regexSearch( "(?i)warn|error" ) );
    REQUIRE( labeler.label( "ERROR modem lost" ) == "error" );
    REQUIRE( labeler.label( "Warn: low battery" ) == "warn" );

    // A setting in a later alternative carries on from there only.
    const auto later = MatchLabeler::fromSearch( regexSearch( "warn|(?i)error|fail" ) );
    REQUIRE( later.label( "WARN" ).isEmpty() );
    REQUIRE( later.label( "Error" ) == "error" );
    REQUIRE( later.label( "FAIL" ) == "fail" );

    // A scoped "(?i:...)" group does not reach the alternatives after it.
    const auto scoped = MatchLabeler::fromSearch( regexSearch( "(?i:warn)|error" ) );
    REQUIRE( scoped.label( "ERROR" ).isEmpty() );
    REQUIRE( scoped.label( "WARN" ) == "(?i:warn)" );

    // Predefined filters combined into one expression inherit the same way, plain-text ones too.
    const auto filters = MatchLabeler::fromSearch(
        regexSearch( "(?i)warn|ERROR" ),
        { filter( "Any warning", "(?i)warn", true ), filter( "Errors", "ERROR", false ) } );
    REQUIRE( filters.label( "error: disk" ) == "Errors" );
    REQUIRE( filters.label( "WARN error" ) == "Any warning, Errors" );

    // "(*...)" settings belong at the start of a pattern: such a pattern stays whole.
    const auto verbs = MatchLabeler::fromSearch( regexSearch( "(*UCP)alpha|beta" ) );
    REQUIRE( verbs.label( "x alpha y" ) == "alpha" );
}

TEST_CASE( "labels_same_text_different_mode", "[workbench][U14]" )
{
    // The labeler reads the typed search: the same text is split by the search mode.
    const QString alternatives = "warn|error";
    const auto asRegex = MatchLabeler::fromSearch(
        RegularExpressionPattern( alternatives, true, false, false, false ) );
    REQUIRE( asRegex.label( "error: disk" ) == "error" );
    const auto asPlain = MatchLabeler::fromSearch(
        RegularExpressionPattern( alternatives, true, false, false, true ) );
    REQUIRE( asPlain.label( "error: disk" ).isEmpty() );
    REQUIRE( asPlain.label( "x warn|error y" ) == "warn|error" );

    const QString terms = R"("warn" or "error")";
    const auto asBoolean
        = MatchLabeler::fromSearch( RegularExpressionPattern( terms, true, false, true, false ) );
    REQUIRE( asBoolean.label( "error: disk" ) == "error" );
    const auto asLiteralRegex
        = MatchLabeler::fromSearch( RegularExpressionPattern( terms, true, false, false, false ) );
    REQUIRE( asLiteralRegex.label( "error: disk" ).isEmpty() );

    // An inverse search names nothing, whatever the mode.
    REQUIRE( MatchLabeler::fromSearch(
                 RegularExpressionPattern( alternatives, true, true, false, false ) )
                 .isEmpty() );
}

TEST_CASE( "labels_inverse_and_empty", "[workbench][U14]" )
{
    REQUIRE( MatchLabeler::fromSearch( RegularExpressionPattern( "x", true, true, false, false ) )
                 .isEmpty() );
    REQUIRE( MatchLabeler::fromSearch( RegularExpressionPattern() ).isEmpty() );
}

TEST_CASE( "labels_filter_alternative_that_matched", "[workbench][U14]" )
{
    // A predefined filter whose pattern is several alternatives names the one that matched,
    // not the whole pattern (Dmitry's "(C_PROC:)|(EHCP)|(EHCI)|(WiFi)|(\[TX\])" filter).
    const QString pattern = R"((C_PROC:)|(EHCP)|(EHCI)|(WiFi)|(\[TX\]))";
    const auto unnamed
        = MatchLabeler::fromSearch( regexSearch( pattern ), { filter( "", pattern, true ) } );
    REQUIRE_FALSE( unnamed.isEmpty() );
    CHECK( unnamed.label( "Par(48)EHCP0290000" ) == "EHCP" );
    CHECK( unnamed.label( "08:36:41.893 [TX] - GET VERSION:00;" ) == "[TX]" );
    CHECK( unnamed.label( "(C_PROC:) WiFi up" ) == "C_PROC:, WiFi" );
    CHECK( unnamed.label( "nothing" ).isEmpty() );

    // A filter with a name of its own keeps the name in front.
    const auto named
        = MatchLabeler::fromSearch( regexSearch( pattern ), { filter( "Panel", pattern, true ) } );
    CHECK( named.label( "Par(48)EHCP0290000" ) == "Panel: EHCP" );
    CHECK( named.preferredColumnChars() >= QString( "Panel: C_PROC:" ).size() );

    // Inline options before an alternative still apply to the alternatives after it.
    const auto options = MatchLabeler::fromSearch( regexSearch( "(?i)warn|error" ),
                                                   { filter( "", "(?i)warn|error", true ) } );
    CHECK( options.label( "ERROR: disk" ) == "error" );

    // A filter of one alternative still names the filter.
    const auto single
        = MatchLabeler::fromSearch( regexSearch( "(EHCP)" ), { filter( "Ehcp", "(EHCP)", true ) } );
    CHECK( single.label( "x EHCP y" ) == "Ehcp" );
}

namespace {
// Dmitry's predefined filters (build 26.04.2.1374 report), combined into one boolean search.
const QString ErrorsPattern
    = R"((^(?!.*\+CME ERROR: 553).*ERROR)|(^.*WARNING.*R.*)|( E \()|( W \())";
const QString ModemPattern = R"((C_PROC:)|(EHCP)|(EHCI)|(WiFi)|(\[TX\]))";

RegularExpressionPattern booleanSearch( const QString& text )
{
    return RegularExpressionPattern( text, true, false, true, false );
}

QString quotedOr( const QStringList& patterns )
{
    return "\"" + patterns.join( "\" or \"" ) + "\"";
}
} // namespace

TEST_CASE( "labels_boolean_search_of_predefined_filters", "[workbench][U14]" )
{
    const QList<PredefinedFilter> known{ filter( "Software", "SW version", false ),
                                         filter( "Errors", ErrorsPattern, true ),
                                         filter( "Modem", ModemPattern, true ) };
    // The search box text after a restart: the filter selection itself is gone.
    const auto search = booleanSearch( quotedOr( { ErrorsPattern, ModemPattern } ) );
    const auto labeler = MatchLabeler::fromSearch( search, {}, known );
    REQUIRE_FALSE( labeler.isEmpty() );

    CHECK( labeler.label( "16/11/2026 02:24:56.322 [RX] - #12 4823 INFO:NET Socket bound to WiFi "
                          "(resolved)" )
           == "Modem: WiFi" );
    CHECK( labeler.label( "16/11/2026 02:23:15.273 [RX] - #22 1668 ERROR:VoIP SipTask Exiting... "
                          "State: 0" )
           == "Errors: ERROR" );
    CHECK( labeler.label( "16/11/2026 02:25:06.248 [RX] - #12 4469 ERROR:NET Failed socket "
                          "connect errno (116)" )
           == "Errors: ERROR" );
    CHECK( labeler.label( "16/11/2026 02:23:15.196 [TX] - VOIP: config proxy 192.168.1.10:5070 ;" )
           == "Modem: [TX]" );
    CHECK( labeler.label( "12:00:01 I (2210) wifi: started" ).isEmpty() ); // case-sensitive
    CHECK( labeler.label( "12:00:01 E (2210) net: no route" ) == "Errors: E (" );
    CHECK( labeler.label( "16/11/2026 02:23:49.488 [RX] - EHCP0480409 ERROR" )
           == "Errors: ERROR, Modem: EHCP" );
    // The lookahead keeps "+CME ERROR: 553" out of the errors.
    CHECK( labeler.label( "16/11/2026 02:23:20.454 [RX] - +CME ERROR: 553" ).isEmpty() );
    CHECK( labeler.preferredColumnChars() >= QString( "Errors: ERROR" ).size() );

    // The same labels while the selection is known.
    const auto selected = MatchLabeler::fromSearch( search, { known.at( 1 ), known.at( 2 ) } );
    CHECK( selected.label( "x ERROR:VoIP" ) == "Errors: ERROR" );
    CHECK( selected.label( "x [TX] - y" ) == "Modem: [TX]" );

    // The filtered pane's filter of the same report has one more alternative.
    const QString paneModem = ModemPattern + "|(TXCTL)";
    const auto pane = MatchLabeler::fromSearch(
        booleanSearch( quotedOr( { ErrorsPattern, paneModem } ) ), {},
        { filter( "Errors", ErrorsPattern, true ), filter( "Modem", paneModem, true ) } );
    CHECK( pane.label( "x TXCTL 0x01" ) == "Modem: TXCTL" );
}

TEST_CASE( "labels_boolean_terms_split_into_alternatives", "[workbench][U14]" )
{
    // A quoted term that is no predefined filter names the alternative that matched.
    const auto typed
        = MatchLabeler::fromSearch( booleanSearch( quotedOr( { "timeout", ModemPattern } ) ) );
    CHECK( typed.label( "Socket bound to WiFi" ) == "WiFi" );
    CHECK( typed.label( "read timeout" ) == "timeout" );
    CHECK( typed.label( "[TX] timeout" ) == "timeout, [TX]" );

    // An unnamed filter is named by its alternatives alone.
    const auto unnamed = MatchLabeler::fromSearch( booleanSearch( quotedOr( { ModemPattern } ) ),
                                                   {}, { filter( "", ModemPattern, true ) } );
    CHECK( unnamed.label( "Par(48)EHCP029" ) == "EHCP" );

    // A plain-text filter is quoted escaped; it is still found, and never split.
    const auto plain
        = MatchLabeler::fromSearch( booleanSearch( quotedOr( { R"(a\.b\|c)", "ERROR" } ) ), {},
                                    { filter( "Dots", "a.b|c", false ) } );
    CHECK( plain.label( "x a.b|c y" ) == "Dots" );
    CHECK( plain.label( "ERROR" ) == "ERROR" );

    // With regex search off, the terms are plain text: nothing is split.
    const auto literal = MatchLabeler::fromSearch(
        RegularExpressionPattern( quotedOr( { "a|b" } ), true, false, true, true ), {},
        { filter( "Bar", "a|b", true ) } );
    CHECK( literal.label( "x a|b y" ) == "Bar" );
}

TEST_CASE( "labels_stay_bounded", "[workbench][U14]" )
{
    QStringList alternatives;
    QString line;
    for ( char letter = 'a'; letter <= 'z'; ++letter ) {
        alternatives.append( QString( 12, QChar( letter ) ) );
        line += QString( 12, QChar( letter ) ) + ' ';
    }
    const auto pattern = alternatives.join( '|' );
    const auto labeler
        = MatchLabeler::fromSearch( booleanSearch( quotedOr( { pattern } ) ), {},
                                    { filter( QString( 40, 'N' ), pattern, true ) } );
    const auto label = labeler.label( line );
    CHECK( label.size() == MatchLabeler::MaxLabelLength );
    CHECK( label.endsWith( QChar( 0x2026 ) ) );
    CHECK( labeler.preferredColumnChars() <= 32 );
}

TEST_CASE( "labels_readable_alternative_names", "[workbench][U14]" )
{
    CHECK( MatchLabeler::alternativeDisplayName( R"((^(?!.*\+CME ERROR: 553).*ERROR))" )
           == "ERROR" );
    CHECK( MatchLabeler::alternativeDisplayName( R"((\[TX\]))" ) == "[TX]" );
    CHECK( MatchLabeler::alternativeDisplayName( R"(( E \())" ) == "E (" );
    CHECK( MatchLabeler::alternativeDisplayName( R"(\bWiFi\b)" ) == "WiFi" );
    CHECK( MatchLabeler::alternativeDisplayName( "(?i)warn" ) == "warn" );
    CHECK( MatchLabeler::alternativeDisplayName( "^(ERROR)$" ) == "ERROR" );
    CHECK( MatchLabeler::alternativeDisplayName( "^.*WARNING.*R.*" ) == "WARNING.*R" );
    CHECK( MatchLabeler::alternativeDisplayName( R"(id=\d+)" ) == R"(id=\d+)" );
    // Nothing is left out when that would leave nothing.
    CHECK( MatchLabeler::alternativeDisplayName( "^.*$" ) == "^.*$" );
    CHECK( MatchLabeler::alternativeDisplayName( "(*UCP)x" ) == "(*UCP)x" );
    CHECK( MatchLabeler::alternativeDisplayName( "a{2}" ) == "a{2}" );
    CHECK( MatchLabeler::alternativeDisplayName( "a{2,}" ) == "a{2,}" );
    CHECK( MatchLabeler::alternativeDisplayName( "[a-z]+" ) == "[a-z]+" );
    // A '{' that starts no count is a plain character; many of them stay cheap.
    CHECK( MatchLabeler::alternativeDisplayName( "x{" ) == "x{" );
    const QString braces( 50000, QChar( '{' ) );
    CHECK( MatchLabeler::alternativeDisplayName( braces ) == braces );
    // A ']' first in a class is a member, also when the outer group is taken off.
    CHECK( MatchLabeler::alternativeDisplayName( "([])])" ) == "[])]" );
}

TEST_CASE( "labels_regex_term_with_the_text_of_a_plain_filter", "[workbench][U14]" )
{
    // The crawler quotes a plain-text filter escaped when regex search is on: a regex term with
    // the filter's raw text is not that filter.
    const auto labeler
        = MatchLabeler::fromSearch( booleanSearch( quotedOr( { "Error (code)", "x" } ) ), {},
                                    { filter( "Paren", "Error (code)", false ) } );
    CHECK( labeler.label( "Error code 5" ) == "Error (code)" );
}

TEST_CASE( "labels_typed_alternatives_without_group_parentheses", "[workbench][U14]" )
{
    const auto labeler = MatchLabeler::fromSearch( regexSearch( R"((EHCP)|(WiFi)|(a)(b))" ) );
    CHECK( labeler.label( "x EHCP" ) == "EHCP" );
    CHECK( labeler.label( "ab" ) == "(a)(b)" );
    CHECK( MatchLabeler::alternativeDisplayName( "(x|y)" ) == "x|y" );
    CHECK( MatchLabeler::alternativeDisplayName( "(?:x)" ) == "(?:x)" );
    CHECK( MatchLabeler::alternativeDisplayName( R"((a\)))" ) == "a)" );
    CHECK( MatchLabeler::alternativeDisplayName( "([)])" ) == "[)]" );
    CHECK( MatchLabeler::alternativeDisplayName( "(a" ) == "(a" );
}

TEST_CASE( "labels_terms_of_filters_with_the_same_pattern", "[workbench][U14]" )
{
    // Two filters with one pattern: the search text cannot tell which was ticked, so the term
    // keeps its pattern rather than naming the first filter.
    const auto ambiguous = MatchLabeler::fromSearch(
        booleanSearch( quotedOr( { "ERROR", "WARN" } ) ), {},
        { filter( "Errors A", "ERROR", true ), filter( "Errors B", "ERROR", true ) } );
    CHECK( ambiguous.label( "x ERROR y" ) == "ERROR" );
    // The same name twice is not ambiguous.
    const auto same = MatchLabeler::fromSearch(
        booleanSearch( quotedOr( { "ERROR" } ) ), {},
        { filter( "Errors", "ERROR", true ), filter( "Errors", "ERROR", true ) } );
    CHECK( same.label( "x ERROR y" ) == "Errors" );
    // A search built from one of them, with the selection known, names that one.
    const auto selected = MatchLabeler::fromSearch( booleanSearch( quotedOr( { "ERROR" } ) ),
                                                    { filter( "Errors B", "ERROR", true ) } );
    CHECK( selected.label( "x ERROR y" ) == "Errors B" );
}

TEST_CASE( "labels_one_quoted_term_of_alternatives", "[workbench][U14]" )
{
    // One quoted term of several alternatives names the one that matched, like the same
    // pattern typed as a regular expression.
    const auto quoted
        = MatchLabeler::fromSearch( booleanSearch( quotedOr( { R"(ERR\d+|WARN)" } ) ) );
    const auto typed = MatchLabeler::fromSearch( regexSearch( R"(ERR\d+|WARN)" ) );
    for ( const auto* labeler : { &quoted, &typed } ) {
        CHECK( labeler->label( "x ERR42 y" ) == R"(ERR\d+)" );
        CHECK( labeler->label( "WARN only" ) == "WARN" );
        CHECK( labeler->preferredColumnChars() >= QString( R"(ERR\d+)" ).size() );
    }
    // One quoted term of one alternative still shows the matched text.
    const auto single = MatchLabeler::fromSearch( booleanSearch( quotedOr( { R"(id=\d+)" } ) ) );
    CHECK( single.label( "event id=4711 done" ) == "id=4711" );
}

TEST_CASE( "labels_free_spacing_and_lookahead_alternatives", "[workbench][U14]" )
{
    // In free-spacing syntax a '|' inside a comment is no alternative: the term stays whole.
    const QString extended = "(?x)foo # ignored | fake\n|bar";
    const auto quoted
        = MatchLabeler::fromSearch( booleanSearch( quotedOr( { extended, "zzz" } ) ) );
    CHECK( quoted.label( "foo fake" ) == extended );
    const auto typed = MatchLabeler::fromSearch( regexSearch( extended ) );
    CHECK( typed.label( "x foo y" ) == "foo" );
    // "(?-x)" turns it off: that pattern is split as usual.
    const auto off = MatchLabeler::fromSearch( regexSearch( "(?-x)warn|error" ) );
    CHECK( off.label( "error 5" ) == "error" );

    // Alternatives that differ only by a lookahead keep it in their names.
    const auto lookahead = MatchLabeler::fromSearch( regexSearch( "foo(?=bar)|foo(?=baz)" ) );
    CHECK( lookahead.label( "foobar" ) == "foo(?=bar)" );
    CHECK( lookahead.label( "foobaz" ) == "foo(?=baz)" );
    const auto named
        = MatchLabeler::fromSearch( booleanSearch( quotedOr( { "foo(?=bar)|foo(?=baz)" } ) ), {},
                                    { filter( "Foo", "foo(?=bar)|foo(?=baz)", true ) } );
    CHECK( named.label( "foobaz" ) == "Foo: foo(?=baz)" );
}
