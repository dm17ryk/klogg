// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch.hpp>

#include <QStringList>

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

// What matched `line`, joined with ", ": the first column of the cell's tooltip.
QString names( const MatchLabeler& labeler, const QString& line )
{
    QStringList found;
    for ( const auto& match : labeler.matches( line ) ) {
        found.append( match.name );
    }
    return found.join( ", " );
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
    REQUIRE( names( labeler, "12:00 Panel Ready" ) == "Panel ready" );
    REQUIRE( names( labeler, "12:01 ERROR: modem" ) == "Errors" );
    REQUIRE( names( labeler, "12:02 Panel Ready after ERR: reset" ) == "Panel ready, Errors" );
    REQUIRE( names( labeler, "12:03 nothing" ).isEmpty() );
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
    REQUIRE( names( withEmpty, "only b here" ) == "b" );
    REQUIRE( names( withEmpty, "a and b" ) == "a, b" );

    const auto labeler = MatchLabeler::fromSearch( regexSearch( "WARN|ERROR", false ) );
    REQUIRE( names( labeler, "error: disk" ) == "ERROR" );
    REQUIRE( names( labeler, "warn and error" ) == "WARN, ERROR" );
}

TEST_CASE( "labels_boolean_terms", "[workbench][U14]" )
{
    const auto search = RegularExpressionPattern( R"("connect" or "timeout")", true, false, true,
                                                  false );
    const auto labeler = MatchLabeler::fromSearch( search );
    REQUIRE_FALSE( labeler.isEmpty() );
    REQUIRE( names( labeler, "socket timeout after 5 s" ) == "timeout" );
    REQUIRE( names( labeler, "connect retry" ) == "connect" );
}

TEST_CASE( "labels_single_term_shows_match", "[workbench][U14]" )
{
    const auto labeler = MatchLabeler::fromSearch( regexSearch( "id=\\d+" ) );
    REQUIRE( labeler.label( "event id=4711 done" ) == "id=4711" );
    REQUIRE( names( labeler, "event id=4711 done" ) == "id=\\d+" );
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
    REQUIRE( names( labeler, "12:00 WARN low battery" ) == "Warnings" );
    REQUIRE( names( labeler, "12:01 ERROR modem" ).isEmpty() );
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
    REQUIRE( names( labeler, "ERROR modem lost" ) == "error" );
    REQUIRE( names( labeler, "Warn: low battery" ) == "warn" );

    // A setting in a later alternative carries on from there only.
    const auto later = MatchLabeler::fromSearch( regexSearch( "warn|(?i)error|fail" ) );
    REQUIRE( names( later, "WARN" ).isEmpty() );
    REQUIRE( names( later, "Error" ) == "error" );
    REQUIRE( names( later, "FAIL" ) == "fail" );

    // A scoped "(?i:...)" group does not reach the alternatives after it.
    const auto scoped = MatchLabeler::fromSearch( regexSearch( "(?i:warn)|error" ) );
    REQUIRE( names( scoped, "ERROR" ).isEmpty() );
    REQUIRE( names( scoped, "WARN" ) == "(?i:warn)" );

    // Predefined filters combined into one expression inherit the same way, plain-text ones too.
    const auto filters = MatchLabeler::fromSearch(
        regexSearch( "(?i)warn|ERROR" ),
        { filter( "Any warning", "(?i)warn", true ), filter( "Errors", "ERROR", false ) } );
    REQUIRE( names( filters, "error: disk" ) == "Errors" );
    REQUIRE( names( filters, "WARN error" ) == "Any warning, Errors" );

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
    REQUIRE( names( asRegex, "error: disk" ) == "error" );
    const auto asPlain = MatchLabeler::fromSearch(
        RegularExpressionPattern( alternatives, true, false, false, true ) );
    REQUIRE( names( asPlain, "error: disk" ).isEmpty() );
    REQUIRE( names( asPlain, "x warn|error y" ) == "warn|error" );

    const QString terms = R"("warn" or "error")";
    const auto asBoolean
        = MatchLabeler::fromSearch( RegularExpressionPattern( terms, true, false, true, false ) );
    REQUIRE( names( asBoolean, "error: disk" ) == "error" );
    const auto asLiteralRegex
        = MatchLabeler::fromSearch( RegularExpressionPattern( terms, true, false, false, false ) );
    REQUIRE( names( asLiteralRegex, "error: disk" ).isEmpty() );

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
    CHECK( names( unnamed, "Par(48)EHCP0290000" ) == "EHCP" );
    CHECK( names( unnamed, "08:36:41.893 [TX] - GET VERSION:00;" ) == "[TX]" );
    CHECK( names( unnamed, "(C_PROC:) WiFi up" ) == "C_PROC:, WiFi" );
    CHECK( names( unnamed, "nothing" ).isEmpty() );

    // A filter with a name of its own keeps the name in front.
    const auto named
        = MatchLabeler::fromSearch( regexSearch( pattern ), { filter( "Panel", pattern, true ) } );
    CHECK( names( named, "Par(48)EHCP0290000" ) == "Panel: EHCP" );
    CHECK( named.preferredColumnChars() >= QString( "C_PROC:" ).size() );

    // Inline options before an alternative still apply to the alternatives after it.
    const auto options = MatchLabeler::fromSearch( regexSearch( "(?i)warn|error" ),
                                                   { filter( "", "(?i)warn|error", true ) } );
    CHECK( names( options, "ERROR: disk" ) == "error" );

    // A filter of one alternative still names the filter.
    const auto single
        = MatchLabeler::fromSearch( regexSearch( "(EHCP)" ), { filter( "Ehcp", "(EHCP)", true ) } );
    CHECK( names( single, "x EHCP y" ) == "Ehcp" );
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

    CHECK( names( labeler, "16/11/2026 02:24:56.322 [RX] - #12 4823 INFO:NET Socket bound to WiFi "
                           "(resolved)" )
           == "Modem: WiFi" );
    CHECK( names( labeler, "16/11/2026 02:23:15.273 [RX] - #22 1668 ERROR:VoIP SipTask Exiting... "
                           "State: 0" )
           == "Errors: ERROR" );
    CHECK( names( labeler, "16/11/2026 02:25:06.248 [RX] - #12 4469 ERROR:NET Failed socket "
                           "connect errno (116)" )
           == "Errors: ERROR" );
    CHECK( names( labeler, "16/11/2026 02:23:15.196 [TX] - VOIP: config proxy 192.168.1.10:5070 ;" )
           == "Modem: [TX]" );
    CHECK( names( labeler, "12:00:01 I (2210) wifi: started" ).isEmpty() ); // case-sensitive
    CHECK( names( labeler, "12:00:01 E (2210) net: no route" ) == "Errors: E (" );
    CHECK( names( labeler, "16/11/2026 02:23:49.488 [RX] - EHCP0480409 ERROR" )
           == "Errors: ERROR, Modem: EHCP" );
    // The lookahead keeps "+CME ERROR: 553" out of the errors.
    CHECK( names( labeler, "16/11/2026 02:23:20.454 [RX] - +CME ERROR: 553" ).isEmpty() );
    CHECK( labeler.preferredColumnChars() >= QString( "WARNING.*R" ).size() );

    // The same labels while the selection is known.
    const auto selected = MatchLabeler::fromSearch( search, { known.at( 1 ), known.at( 2 ) } );
    CHECK( names( selected, "x ERROR:VoIP" ) == "Errors: ERROR" );
    CHECK( names( selected, "x [TX] - y" ) == "Modem: [TX]" );

    // The filtered pane's filter of the same report has one more alternative.
    const QString paneModem = ModemPattern + "|(TXCTL)";
    const auto pane = MatchLabeler::fromSearch(
        booleanSearch( quotedOr( { ErrorsPattern, paneModem } ) ), {},
        { filter( "Errors", ErrorsPattern, true ), filter( "Modem", paneModem, true ) } );
    CHECK( names( pane, "x TXCTL 0x01" ) == "Modem: TXCTL" );
}

TEST_CASE( "labels_boolean_terms_split_into_alternatives", "[workbench][U14]" )
{
    // A quoted term that is no predefined filter names the alternative that matched.
    const auto typed
        = MatchLabeler::fromSearch( booleanSearch( quotedOr( { "timeout", ModemPattern } ) ) );
    CHECK( names( typed, "Socket bound to WiFi" ) == "WiFi" );
    CHECK( names( typed, "read timeout" ) == "timeout" );
    CHECK( names( typed, "[TX] timeout" ) == "timeout, [TX]" );

    // An unnamed filter is named by its alternatives alone.
    const auto unnamed = MatchLabeler::fromSearch( booleanSearch( quotedOr( { ModemPattern } ) ),
                                                   {}, { filter( "", ModemPattern, true ) } );
    CHECK( names( unnamed, "Par(48)EHCP029" ) == "EHCP" );

    // A plain-text filter is quoted escaped; it is still found, and never split.
    const auto plain
        = MatchLabeler::fromSearch( booleanSearch( quotedOr( { R"(a\.b\|c)", "ERROR" } ) ), {},
                                    { filter( "Dots", "a.b|c", false ) } );
    CHECK( names( plain, "x a.b|c y" ) == "Dots" );
    CHECK( names( plain, "ERROR" ) == "ERROR" );

    // With regex search off, the terms are plain text: nothing is split.
    const auto literal = MatchLabeler::fromSearch(
        RegularExpressionPattern( quotedOr( { "a|b" } ), true, false, true, true ), {},
        { filter( "Bar", "a|b", true ) } );
    CHECK( names( literal, "x a|b y" ) == "Bar" );
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
    CHECK( names( labeler, "Error code 5" ) == "Error (code)" );
}

TEST_CASE( "labels_typed_alternatives_without_group_parentheses", "[workbench][U14]" )
{
    const auto labeler = MatchLabeler::fromSearch( regexSearch( R"((EHCP)|(WiFi)|(a)(b))" ) );
    CHECK( names( labeler, "x EHCP" ) == "EHCP" );
    CHECK( names( labeler, "ab" ) == "(a)(b)" );
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
    CHECK( names( ambiguous, "x ERROR y" ) == "ERROR" );
    // The same name twice is not ambiguous.
    const auto same = MatchLabeler::fromSearch(
        booleanSearch( quotedOr( { "ERROR" } ) ), {},
        { filter( "Errors", "ERROR", true ), filter( "Errors", "ERROR", true ) } );
    CHECK( names( same, "x ERROR y" ) == "Errors" );
    // A search built from one of them, with the selection known, names that one.
    const auto selected = MatchLabeler::fromSearch( booleanSearch( quotedOr( { "ERROR" } ) ),
                                                    { filter( "Errors B", "ERROR", true ) } );
    CHECK( names( selected, "x ERROR y" ) == "Errors B" );
}

TEST_CASE( "labels_one_quoted_term_of_alternatives", "[workbench][U14]" )
{
    // One quoted term of several alternatives names the one that matched, like the same
    // pattern typed as a regular expression.
    const auto quoted
        = MatchLabeler::fromSearch( booleanSearch( quotedOr( { R"(ERR\d+|WARN)" } ) ) );
    const auto typed = MatchLabeler::fromSearch( regexSearch( R"(ERR\d+|WARN)" ) );
    for ( const auto* labeler : { &quoted, &typed } ) {
        CHECK( names( *labeler, "x ERR42 y" ) == R"(ERR\d+)" );
        CHECK( names( *labeler, "WARN only" ) == "WARN" );
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
    CHECK( names( quoted, "foo fake" ) == extended );
    const auto typed = MatchLabeler::fromSearch( regexSearch( extended ) );
    CHECK( typed.label( "x foo y" ) == "foo" );
    // "(?-x)" turns it off: that pattern is split as usual.
    const auto off = MatchLabeler::fromSearch( regexSearch( "(?-x)warn|error" ) );
    CHECK( names( off, "error 5" ) == "error" );

    // Alternatives that differ only by a lookahead keep it in their names.
    const auto lookahead = MatchLabeler::fromSearch( regexSearch( "foo(?=bar)|foo(?=baz)" ) );
    CHECK( names( lookahead, "foobar" ) == "foo(?=bar)" );
    CHECK( names( lookahead, "foobaz" ) == "foo(?=baz)" );
    const auto named
        = MatchLabeler::fromSearch( booleanSearch( quotedOr( { "foo(?=bar)|foo(?=baz)" } ) ), {},
                                    { filter( "Foo", "foo(?=bar)|foo(?=baz)", true ) } );
    CHECK( names( named, "foobaz" ) == "Foo: foo(?=baz)" );
}

TEST_CASE( "labels_backreferences_and_quoted_literals", "[workbench][U14]" )
{
    // A term that refers to its groups by number stays whole: compiled alone, the second
    // alternative would number its group 1 and match "bb", which the whole term does not.
    const QString backreference = R"((?<x>a)|(?<y>b)\1)";
    const auto whole
        = MatchLabeler::fromSearch( booleanSearch( quotedOr( { backreference, "zzz" } ) ) );
    CHECK( names( whole, "a bb" ) == backreference );

    // "\Q...\E" quotes a '|': it is part of the literal, not an alternative boundary.
    CHECK( MatchLabeler::splitTopLevelAlternatives( R"(\Qa|b\E|c)" )
           == QStringList{ R"(\Qa|b\E)", "c" } );
    const auto quoted
        = MatchLabeler::fromSearch( booleanSearch( quotedOr( { R"(\Qa|b\E|c)", "zzz" } ) ) );
    CHECK( names( quoted, "x a|b y" ) == R"(\Qa|b\E)" );
    CHECK( names( quoted, "c" ) == "c" );
}

TEST_CASE( "matched_by_cell_shows_the_matched_text", "[workbench][U14]" )
{
    // #184: the cell shows the text that matched on the line; the filter and the alternative
    // that matched it are in the tooltip.
    const QList<PredefinedFilter> known{ filter( "Errors", ErrorsPattern, true ),
                                         filter( "Modem", ModemPattern, true ) };
    const auto labeler = MatchLabeler::fromSearch(
        booleanSearch( quotedOr( { ErrorsPattern, ModemPattern } ) ), {}, known );
    // The anchor, the lookahead and the ".*" before "ERROR" are not shown.
    CHECK( labeler.label( "16/11/2026 02:23:15.273 [RX] - #22 1668 ERROR:VoIP SipTask Exiting" )
           == "ERROR" );
    CHECK( labeler.label( "x Socket bound to WiFi (resolved)" ) == "WiFi" );
    CHECK( labeler.label( "16/11/2026 02:23:15.196 [TX] - VOIP: config" ) == "[TX]" );
    CHECK( labeler.label( "12:00:01 E (2210) net: no route" ) == "E (" );
    CHECK( labeler.label( "nothing here" ).isEmpty() );

    const auto both = labeler.annotate( "16/11/2026 02:23:49.488 [RX] - EHCP0480409 ERROR" );
    CHECK( both.text == "ERROR, EHCP" );
    CHECK( both.toolTip.contains( "<td>Errors: ERROR&nbsp;&nbsp;</td><td>ERROR</td>" ) );
    CHECK( both.toolTip.contains( "<td>Modem: EHCP&nbsp;&nbsp;</td><td>EHCP</td>" ) );
    const auto none = labeler.annotate( "nothing here" );
    CHECK( none.text.isEmpty() );
    CHECK( none.toolTip.isEmpty() );

    // A filter of one alternative: its matched text, not its name.
    const QString modulePattern = R"(LTE \w+-\w+)";
    const auto module = MatchLabeler::fromSearch(
        regexSearch( modulePattern ),
        { filter( "Module Version and Type LTE", modulePattern, true ) } );
    const auto moduleLine = module.annotate( "12:00 modem LTE CAT4-EU1 ready" );
    CHECK( moduleLine.text == "LTE CAT4-EU1" );
    CHECK( moduleLine.toolTip.contains(
        "<td>Module Version and Type LTE&nbsp;&nbsp;</td><td>LTE CAT4-EU1</td>" ) );
}

TEST_CASE( "matched_by_cell_lists_each_text_once", "[workbench][U14]" )
{
    // Two filters with the same text: the cell says it once, the tooltip names both.
    const auto twice
        = MatchLabeler::fromSearch( regexSearch( "WARN|WARN" ), { filter( "A", "WARN", false ),
                                                                  filter( "B", "WARN", false ) } );
    const auto annotation = twice.annotate( "12:00 WARN low battery" );
    CHECK( annotation.text == "WARN" );
    CHECK( annotation.toolTip.contains( "<td>A&nbsp;&nbsp;</td><td>WARN</td>" ) );
    CHECK( annotation.toolTip.contains( "<td>B&nbsp;&nbsp;</td><td>WARN</td>" ) );

    // Markup in the text is shown as text.
    const auto markup
        = MatchLabeler::fromSearch( regexSearch( "<b>" ), { filter( "Bold", "<b>", false ) } );
    const auto bold = markup.annotate( "x <b> y" );
    CHECK( bold.text == "<b>" );
    CHECK( bold.toolTip.contains( "<td>&lt;b&gt;</td>" ) );

    // Many matches: the tooltip stays bounded and says how many more there are.
    QStringList alternatives;
    QString line;
    for ( char letter = 'a'; letter <= 'z'; ++letter ) {
        alternatives.append( QString( 3, QChar( letter ) ) );
        line += QString( 3, QChar( letter ) ) + ' ';
    }
    const auto many = MatchLabeler::fromSearch( regexSearch( alternatives.join( '|' ) ) );
    const auto manyRows = many.annotate( line ).toolTip;
    CHECK( manyRows.count( "<tr>" ) == MatchLabeler::MaxToolTipRows + 2 );
    CHECK( manyRows.contains( "and 10 more" ) );
}

TEST_CASE( "matched_by_text_of_whole_line_and_odd_patterns", "[workbench][U14]" )
{
    // ".*" around the text is not shown.
    const auto around = MatchLabeler::fromSearch( regexSearch( "^.*ERROR.*$" ),
                                                  { filter( "Errors", "^.*ERROR.*$", true ) } );
    CHECK( around.label( "x ERROR y" ) == "ERROR" );
    // A match of the whole line shows the pattern.
    const QString pair = R"(\w+ \d+)";
    const auto whole
        = MatchLabeler::fromSearch( regexSearch( pair ), { filter( "Pair", pair, true ) } );
    CHECK( whole.label( "abc 12" ) == pair );
    // An option setting between the items keeps the expression as written: "(?i)" still
    // reaches the lookahead, so the line matches.
    const auto options = MatchLabeler::fromSearch( regexSearch( "^.*a(?i)b(?=X)" ) );
    CHECK( options.label( "zzz aBx" ) == "zzz aB" );
    // A group of the name the labeler uses gives the text to show.
    const auto named = MatchLabeler::fromSearch( regexSearch( R"(^.*(?<cilogg_matched>id)=\d)" ) );
    CHECK( named.label( "x id=4 y" ) == "id" );
}
