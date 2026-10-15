#include <catch2/catch.hpp>

#include <QFont>
#include <QFontMetrics>

#include "wrappedstring.h"

TEST_CASE( "Wrapping makes progress with zero columns", "[wrap]" )
{
    const WrappedString wrapped( QStringLiteral( "abc" ), 0_length );
    REQUIRE( wrapped.wrappedLinesCount() == 3 );
    REQUIRE( wrapped.wrappedLine( 0 ).toString() == QStringLiteral( "a" ) );
    REQUIRE( wrapped.wrappedLine( 2 ).toString() == QStringLiteral( "c" ) );
}

TEST_CASE( "Pixel wrapping preserves source text and Unicode scalars", "[wrap]" )
{
    const QFontMetrics metrics( QFont( QStringLiteral( "Arial" ), 12 ) );
    const auto width = metrics.horizontalAdvance( QStringLiteral( "WWWW" ) );
    const auto text = QString::fromUtf8( "Wi Wi WWWW narrow \xf0\x9f\x98\x80 wide" );
    const WrappedString wrapped( text, metrics, width );
    QString restored;
    for ( size_t i = 0; i < wrapped.wrappedLinesCount(); ++i ) {
        const auto row = wrapped.wrappedLine( i );
        REQUIRE_FALSE( row.isEmpty() );
        REQUIRE_FALSE( row.front().isLowSurrogate() );
        REQUIRE_FALSE( row.back().isHighSurrogate() );
        REQUIRE( metrics.horizontalAdvance( row.toString() ) <= width );
        restored += row.toString();
    }
    REQUIRE( restored == text );
}

TEST_CASE( "Viewport wrapping only constructs requested rows of enormous lines",
           "[wrap][performance]" )
{
    const QFontMetrics metrics( QFont( QStringLiteral( "Arial" ), 12 ) );
    const QString text( 1024 * 1024, QLatin1Char( 'W' ) );
    const WrappedString wrapped( text, metrics, 400, 4 );
    REQUIRE( wrapped.wrappedLinesCount() == 4 );
    QString visible;
    for ( size_t i = 0; i < wrapped.wrappedLinesCount(); ++i ) {
        const auto row = wrapped.wrappedLine( i );
        REQUIRE( metrics.horizontalAdvance( row.toString() ) <= 400 );
        visible += row.toString();
    }
    REQUIRE( visible.size() < 1000 );
    REQUIRE( text.startsWith( visible ) );

    QString highlighted;
    for ( const auto& chunk : wrapped.mid( 0_lcol, LineLength{ text.size() } ) ) {
        highlighted += chunk.toString();
    }
    REQUIRE( highlighted == visible );
}
