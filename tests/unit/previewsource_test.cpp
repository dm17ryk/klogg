// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewsource.h"
#include <catch2/catch.hpp>

TEST_CASE( "Inline preview keeps ordinary text and validates physical row selectors",
           "[previewsource]" )
{
    const QString original = QString::fromUtf8( "EHCP שלום 😀\nsecond row" );
    const auto whole = preparePreviewText( original );
    REQUIRE( whole.error.isEmpty() );
    CHECK( whole.text == original );
    CHECK( whole.source.value( "kind" ) == "text" );
    CHECK( preparePreviewText( original, 2 ).text == "second row" );
    CHECK( preparePreviewText( original, 0 ).error == "invalid_line" );
    CHECK( preparePreviewText( original, 3 ).error == "invalid_line" );
    CHECK( preparePreviewText( QString( 1024 * 1024 + 1, QLatin1Char( 'x' ) ) ).error
           == "input_limit" );
}
