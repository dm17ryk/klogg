/*
 * Copyright (C) 2025
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

#include "previewdecodeutils.h"

TEST_CASE( "Hex parsing allows odd digits", "[previewdecode]" )
{
    const auto result = parseHexToU64AllowOddDigits( "042" );
    REQUIRE( result.ok );
    CHECK( result.value == 0x42 );
    CHECK( result.digitCount == 3 );
}

TEST_CASE( "Hex parsing handles even digits", "[previewdecode]" )
{
    const auto result = parseHexToU64AllowOddDigits( "D774" );
    REQUIRE( result.ok );
    CHECK( result.value == 0xD774 );
    CHECK( result.digitCount == 4 );
}

TEST_CASE( "Hex parsing reports invalid digits", "[previewdecode]" )
{
    const auto result = parseHexToU64AllowOddDigits( "04Z" );
    REQUIRE_FALSE( result.ok );
    CHECK_FALSE( result.error.isEmpty() );
}

TEST_CASE( "Expression evaluation supports subtraction", "[previewdecode]" )
{
    PreviewValueExpr expr;
    expr.isSet = true;
    expr.expression = "{size}-5";

    QMap<QString, qint64> values;
    values.insert( "size", 66 );

    const auto result = evaluatePreviewExpression( expr, values );
    REQUIRE( result.ok );
    CHECK( result.value == 61 );
}

TEST_CASE( "Expression evaluation reports missing variables", "[previewdecode]" )
{
    PreviewValueExpr expr;
    expr.isSet = true;
    expr.expression = "{missing}+1";

    QMap<QString, qint64> values;
    values.insert( "size", 66 );

    const auto result = evaluatePreviewExpression( expr, values );
    REQUIRE_FALSE( result.ok );
    CHECK( result.missingVariable == "missing" );
}
