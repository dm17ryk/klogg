// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch.hpp>

#include <QColor>
#include <QSettings>
#include <QTemporaryDir>
#include <QVariant>

#include <limits>
#include <utility>
#include <vector>

#include "containers.h"
#include "highlightedmatch.h"
#include "highlighterset.h"

TEST_CASE( "A highlighter's color variance is clamped when it is read (#170)", "[highlighter]" )
{
    // An imported or hand-edited highlighters file can carry any value; the editor offers only
    // 5 to 99. A negative one put the colour distribution's bounds in the wrong order and a huge
    // one overflowed them: undefined behaviour once "Variate colors" and "Match only" are on.
    const std::vector<std::pair<QVariant, int>> cases{
        { -5, 0 },
        { std::numeric_limits<int>::min(), 0 },
        { std::numeric_limits<int>::max(), Highlighter::MaxColorVariance },
        { 101, Highlighter::MaxColorVariance },
        { 0, 0 },
        { 40, 40 },
        { Highlighter::MaxColorVariance, Highlighter::MaxColorVariance },
    };
    QTemporaryDir temp;
    REQUIRE( temp.isValid() );
    QSettings settings( temp.filePath( "highlighters.ini" ), QSettings::IniFormat );
    for ( const auto& [ stored, expected ] : cases ) {
        INFO( "stored color_variance " << stored.toString().toStdString() );
        settings.setValue( "regexp", "match" );
        settings.setValue( "match_only", true );
        settings.setValue( "variate_colors", true );
        settings.setValue( "color_variance", stored );
        settings.setValue( "fore_colour", "#ff102030" );
        settings.setValue( "back_colour", "#ffc0d0e0" );
        Highlighter highlighter;
        highlighter.retrieveFromStorage( settings );
        CHECK( highlighter.colorVariance() == expected );

        // Highlighting a match varies its colours within the clamped range.
        klogg::vector<HighlightedMatch> matches;
        REQUIRE( highlighter.matchLine( "a match here", matches ) );
        REQUIRE( matches.size() == 1 );
        CHECK( matches.front().foreColor().isValid() );
        CHECK( matches.front().backColor().isValid() );
    }
}

TEST_CASE( "A highlighter's color variance set out of range is clamped (#170)", "[highlighter]" )
{
    Highlighter highlighter( "match", false, true, QColor( "#ff102030" ), QColor( "#ffc0d0e0" ) );
    highlighter.setVariateColors( true );
    highlighter.setColorVariance( -1 );
    CHECK( highlighter.colorVariance() == 0 );
    highlighter.setColorVariance( std::numeric_limits<int>::max() );
    CHECK( highlighter.colorVariance() == Highlighter::MaxColorVariance );
    highlighter.setColorVariance( 25 );
    CHECK( highlighter.colorVariance() == 25 );
    klogg::vector<HighlightedMatch> matches;
    REQUIRE( highlighter.matchLine( "one match", matches ) );
    REQUIRE( matches.size() == 1 );
}
