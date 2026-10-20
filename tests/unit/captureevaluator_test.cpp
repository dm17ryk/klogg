// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "captureevaluator.h"
#include "regularexpression.h"
#include <catch2/catch.hpp>

namespace {
QVariantMap evaluateCaptures( const QString& pattern, const QString& text,
                              QVariantMap options = {} )
{
    options[ "pattern" ] = pattern;
    CaptureEvaluator evaluator( options );
    REQUIRE( evaluator.compilation().value( "reason" ).toString().isEmpty() );
    return evaluator.evaluate( { QVariantMap{ { "lineNumber", 1 }, { "text", text } } } );
}
} // namespace

TEST_CASE( "Capture evaluator preserves ordinary named optional and empty groups",
           "[regex][captures]" )
{
    const auto output = evaluateCaptures( "(?<word>a)(b)?()", QString::fromUtf8( "😀a" ) );
    const auto rows = output.value( "rows" ).toList();
    REQUIRE( rows.size() == 1 );
    const auto sub = rows.front().toMap().value( "subpatterns" ).toList().front().toMap();
    const auto match = sub.value( "matches" ).toList().front().toMap();
    CHECK( match.value( "startUtf16" ).toInt() == 2 );
    const auto groups = match.value( "groups" ).toList();
    REQUIRE( groups.size() == 4 );
    CHECK( groups.at( 1 ).toMap().value( "name" ) == "word" );
    CHECK( groups.at( 1 ).toMap().value( "value" ) == "a" );
    CHECK_FALSE( groups.at( 2 ).toMap().value( "matched" ).toBool() );
    CHECK( groups.at( 2 ).toMap().value( "value" ).isNull() );
    CHECK( groups.at( 2 ).toMap().value( "startUtf16" ).toInt() == -1 );
    CHECK( groups.at( 3 ).toMap().value( "matched" ).toBool() );
    CHECK( groups.at( 3 ).toMap().value( "value" ).toString().isEmpty() );
    CHECK( groups.at( 3 ).toMap().value( "lengthUtf16" ).toInt() == 0 );
}

TEST_CASE( "Capture evaluator uses Qt global matching for empty and repeated matches",
           "[regex][captures]" )
{
    for ( const auto& pattern : { QString( "(?=a)|a" ), QString( "a*" ) } ) {
        const auto output = evaluateCaptures( pattern, "aa" );
        INFO( output.value( "reason" ).toString().toStdString() );
        const auto rows = output.value( "rows" ).toList();
        REQUIRE_FALSE( rows.isEmpty() );
        const auto row = rows.front().toMap();
        const auto matches
            = row.value( "subpatterns" ).toList().front().toMap().value( "matches" ).toList();
        const auto reference = QRegularExpression( pattern ).globalMatch( "aa" );
        auto iterator = reference;
        int index = 0;
        while ( iterator.hasNext() ) {
            const auto expected = iterator.next();
            REQUIRE( index < matches.size() );
            CHECK( matches.at( index ).toMap().value( "startUtf16" ).toLongLong()
                   == expected.capturedStart() );
            CHECK( matches.at( index ).toMap().value( "lengthUtf16" ).toLongLong()
                   == expected.capturedLength() );
            ++index;
        }
        CHECK( index == matches.size() );
    }
}

TEST_CASE( "Capture Boolean outcomes preserve search grammar and subpattern boundaries",
           "[regex][captures]" )
{
    for ( const auto& operation : { "and", "or", "nand", "nor", "xor", "xnor" } ) {
        const auto expression = QString( "\"(?<same>a)\" %1 \"(?<same>b)\"" ).arg( operation );
        RegularExpression reference(
            RegularExpressionPattern( expression, true, false, true, false ) );
        REQUIRE( reference.isValid() );
        const auto matcher = reference.createMatcher();
        for ( const auto& text : { "a", "b", "ab", "c" } ) {
            const auto output = evaluateCaptures(
                expression, text, { { "boolean", true }, { "includeUnmatched", true } } );
            const auto row = output.value( "rows" ).toList().front().toMap();
            CHECK( row.value( "matched" ).toBool() == matcher->hasMatch( text ) );
            REQUIRE( row.value( "subpatterns" ).toList().size() == 2 );
        }
    }
}

TEST_CASE( "Capture compilation rejects malformed patterns and unsupported combinations",
           "[regex][captures]" )
{
    CaptureEvaluator malformed( { { "pattern", "(" } } );
    CHECK( malformed.compilation().value( "reason" ) == "invalid_pattern" );
    CHECK( malformed.compilation().value( "errorDetails" ).toMap().contains( "offsetUtf16" ) );
    CaptureEvaluator unsupported(
        { { "pattern", "\"a\"" }, { "boolean", true }, { "matchMode", "whole_line" } } );
    CHECK( unsupported.compilation().value( "reason" ) == "unsupported_options" );
}

TEST_CASE( "Capture expressions preserve quoted operators and supported match options",
           "[regex][captures]" )
{
    for ( const auto& expression :
          { QString( "not \"a\"" ), QString( "!\"a\"" ), QString( "(\"a\" & \"b\") | \"c\"" ),
            QString( "\"a\\\"b\" or \"c\"" ) } ) {
        RegularExpression reference(
            RegularExpressionPattern( expression, false, false, true, false ) );
        INFO( expression.toStdString() );
        INFO( reference.errorString().toStdString() );
        if ( !reference.isValid() ) {
            CaptureEvaluator candidate(
                { { "pattern", expression }, { "boolean", true }, { "caseSensitive", false } } );
            CHECK_FALSE( candidate.compilation().value( "reason" ).toString().isEmpty() );
            continue;
        }
        REQUIRE( reference.isValid() );
        const auto matcher = reference.createMatcher();
        for ( const auto& text : { "A", "ab", "c", "a\"b", "x" } ) {
            const auto output = evaluateCaptures(
                expression, text,
                { { "boolean", true }, { "caseSensitive", false }, { "includeUnmatched", true } } );
            CHECK( output.value( "rows" ).toList().front().toMap().value( "matched" ).toBool()
                   == matcher->hasMatch( text ) );
        }
    }
    CHECK(
        evaluateCaptures(
            "a.b", "A.B",
            { { "plainText", true }, { "caseSensitive", false }, { "matchMode", "whole_line" } } )
            .value( "rows" )
            .toList()
            .size()
        == 1 );
    CHECK( evaluateCaptures( "a", "cat", { { "matchMode", "whole_word" } } )
               .value( "rows" )
               .toList()
               .isEmpty() );
    CHECK( evaluateCaptures( "a", "a\r", { { "matchMode", "whole_line" } } )
               .value( "rows" )
               .toList()
               .size()
           == 1 );
}

TEST_CASE( "Capture evaluator reports engine and output limits explicitly", "[regex][captures]" )
{
    const auto pathological
        = evaluateCaptures( "(*LIMIT_MATCH=10)(a+)+$", QString( 100, 'a' ) + "!" );
    CHECK( pathological.value( "reason" ) == "match_resource_limit" );
    const auto excessive = evaluateCaptures( "", QString( 20000, 'x' ) );
    CHECK( excessive.value( "reason" ) == "result_limit" );
}
