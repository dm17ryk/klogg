// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "dataresult.h"
#include "loadingstatus.h"
#include <catch2/catch.hpp>
#include <type_traits>

TEST_CASE( "Checked results coexist with log-loading status", "[communication][D01c-status]" )
{
    static_assert( std::is_enum<::DataStatus>::value, "Loading status keeps its enum identity" );
    static_assert( std::is_class<cilogg::DataStatus>::value,
                   "Operation results have a scoped identity" );
    const cilogg::DataStatus result{ true, {} };
    REQUIRE( result.ok );
    REQUIRE( ::DataStatus::NEW_DATA != ::DataStatus::OLD_DATA );
}
