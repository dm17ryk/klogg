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

TEST_CASE( "labels_inverse_and_empty", "[workbench][U14]" )
{
    REQUIRE( MatchLabeler::fromSearch( RegularExpressionPattern( "x", true, true, false, false ) )
                 .isEmpty() );
    REQUIRE( MatchLabeler::fromSearch( RegularExpressionPattern() ).isEmpty() );
}
