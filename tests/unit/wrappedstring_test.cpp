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
    REQUIRE( wrapped.hasContinuation() );
    REQUIRE( wrapped.processedCharacters() < 4 * 4096 * 32 );
    const WrappedString longer( QString( 16 * 1024 * 1024, QLatin1Char( 'W' ) ), metrics, 400, 4 );
    REQUIRE( longer.processedCharacters() == wrapped.processedCharacters() );
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

TEST_CASE( "Wrapping resumes from the retained source position without losing text", "[wrap]" )
{
    const QFontMetrics metrics( QFont( QStringLiteral( "Arial" ), 12 ) );
    const auto text = QString( 16000, QLatin1Char( 'W' ) )
                      + QString::fromUtf8( " continuation \xf0\x9f\x98\x80 end" );
    WrappedString wrapped( text, metrics, 300, 4 );
    QString restored;
    size_t firstRow = 0;
    do {
        const auto page = wrapped.window( firstRow, 4 );
        QString highlighted;
        for ( const auto& part : page.mid( 0_lcol, LineLength{ text.size() } ) ) {
            highlighted += part.toString();
        }
        QString pageText;
        for ( size_t row = 0; row < page.wrappedLinesCount(); ++row ) {
            const auto part = page.wrappedLine( row );
            REQUIRE_FALSE( part.front().isLowSurrogate() );
            REQUIRE_FALSE( part.back().isHighSurrogate() );
            REQUIRE( page.sourceColumn( row ) == wrapped.sourceColumn( firstRow + row ) );
            REQUIRE( wrapped.rowForColumn( page.sourceColumn( row ) ) == firstRow + row );
            pageText += part.toString();
        }
        REQUIRE( highlighted == pageText );
        restored += pageText;
        firstRow += page.wrappedLinesCount();
        if ( !wrapped.hasContinuation() ) {
            break;
        }
        const auto workBefore = wrapped.processedCharacters();
        wrapped.appendRows( 4 );
        REQUIRE( wrapped.processedCharacters() - workBefore < 4 * 4096 * 32 );
    } while ( true );
    REQUIRE( restored == text );
}

TEST_CASE( "Combining-only records cannot exhaust a viewport layout work budget",
           "[wrap][performance]" )
{
    const QFontMetrics metrics( QFont( QStringLiteral( "Arial" ), 12 ) );
    const QString text( 1024 * 1024, QChar( 0x200b ) );
    WrappedString wrapped( text, metrics, 400, 4 );
    REQUIRE( wrapped.wrappedLinesCount() == 4 );
    REQUIRE( wrapped.hasContinuation() );
    REQUIRE( wrapped.processedCharacters() < 4 * 4096 * 32 );
    const auto workBefore = wrapped.processedCharacters();
    wrapped.appendRows( 4 );
    REQUIRE( wrapped.wrappedLinesCount() == 8 );
    REQUIRE( wrapped.processedCharacters() - workBefore < 4 * 4096 * 32 );
}
