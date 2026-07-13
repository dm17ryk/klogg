#include <catch2/catch.hpp>

#include "configuration.h"

TEST_CASE( "Fresh configuration shows the tabs bar by default", "[configuration]" )
{
    const Configuration configuration;

    REQUIRE( configuration.showTabsBarByDefault() );
}
