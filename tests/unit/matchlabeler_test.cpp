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
    const auto label = longMatch.label( "<" + QString( 200, 'x' ) + ">" );
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
    REQUIRE( labeler.label( "Warn: low battery" ) == "(?i)warn" );

    // A setting in a later alternative carries on from there only.
    const auto later = MatchLabeler::fromSearch( regexSearch( "warn|(?i)error|fail" ) );
    REQUIRE( later.label( "WARN" ).isEmpty() );
    REQUIRE( later.label( "Error" ) == "(?i)error" );
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
