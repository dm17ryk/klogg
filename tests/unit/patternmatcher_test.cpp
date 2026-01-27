/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <catch2/catch.hpp>

#include "regularexpression.h"

SCENARIO( "Pattern matcher in boolean mode", "[patternmatcher]" )
{
    std::string_view matchLine = "\"This\" is matching pattern";

    WHEN( "Using single pattern" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"matching\"", false, false, true, true ) );
        const auto matcher = expression.createMatcher();
        REQUIRE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using complex pattern" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"not_match\" | \"match\"", false, false, true, true ) );
        const auto matcher = expression.createMatcher();
        REQUIRE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using complex pattern with ()" )
    {
        RegularExpression expression( RegularExpressionPattern(
            "(\"not_match\" | \"match\") & !(\"pattern\")", false, false, true, false ) );
        const auto matcher = expression.createMatcher();
        REQUIRE_FALSE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using pattern with escaped quotes" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"\\\"This\\\"\"", false, false, true, false ) );
        const auto matcher = expression.createMatcher();
        REQUIRE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Using pattern with not matched quotes" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"not_match\" | \"match", false, false, true, false ) );

        REQUIRE_FALSE( expression.isValid() );
    }
}

SCENARIO( "Boolean combinations still work with multi-pattern matching", "[patternmatcher]" )
{
    std::string_view lineMatchAnd = "foo ... bar";
    std::string_view lineNoAnd = "foo only";
    std::string_view lineNot = "keep this line";

    GIVEN( "A boolean AND expression with two subpatterns" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"foo\" & \"bar\"", false, false, true, false ) );
        REQUIRE( expression.isValid() );

        const auto matcher = expression.createMatcher();
        THEN( "Both tokens must be present" )
        {
            REQUIRE( matcher->hasMatch( lineMatchAnd ) );
            REQUIRE_FALSE( matcher->hasMatch( lineNoAnd ) );
        }
    }

    GIVEN( "A boolean OR expression" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"foo\" | \"bar\"", false, false, true, false ) );
        REQUIRE( expression.isValid() );

        const auto matcher = expression.createMatcher();
        THEN( "Either token is sufficient" )
        {
            REQUIRE( matcher->hasMatch( lineMatchAnd ) );
            REQUIRE( matcher->hasMatch( lineNoAnd ) );
            REQUIRE_FALSE( matcher->hasMatch( lineNot ) );
        }
    }

    GIVEN( "A boolean XOR expression" )
    {
        RegularExpression expression(
            RegularExpressionPattern( "\"foo\" xor \"bar\"", false, false, true, false ) );
        REQUIRE( expression.isValid() );

        const auto matcher = expression.createMatcher();
        THEN( "Exactly one token present matches" )
        {
            REQUIRE( matcher->hasMatch( lineNoAnd ) );          // only foo
            REQUIRE_FALSE( matcher->hasMatch( lineMatchAnd ) ); // foo and bar
            REQUIRE_FALSE( matcher->hasMatch( lineNot ) );      // none
        }
    }
}

SCENARIO( "Pattern matcher splits top level alternation", "[patternmatcher]" )
{
    std::string_view matchLine = "this line contains baz token";
    std::string_view noMatchLine = "nothing interesting here";

    RegularExpression expression(
        RegularExpressionPattern( "foo|bar|baz", false, false, false, false ) );
    REQUIRE( expression.isValid() );

    WHEN( "Line matches one of the later alternatives" )
    {
        const auto matcher = expression.createMatcher();
        REQUIRE( matcher->hasMatch( matchLine ) );
    }

    WHEN( "Line does not match any alternative" )
    {
        const auto matcher = expression.createMatcher();
        REQUIRE_FALSE( matcher->hasMatch( noMatchLine ) );
    }
}
