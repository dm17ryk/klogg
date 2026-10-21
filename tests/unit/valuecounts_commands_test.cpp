// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli.h"
#include "commander.h"
#include <catch2/catch.hpp>

TEST_CASE( "Value count actions share CLI and Commander serialization", "[commander][valuecounts]" )
{
    const QString json
        = R"json({"scope":"text","pattern":"(?<v>.+)","text":"x","counts":{"capture":"v"}})json";
    for ( const auto& name : { "value_counts", "get_value_count", "export_value_counts",
                               "apply_value_count", "open_value_count", "set_value_counts" } ) {
        CliParameters cli( { "cilogg", "command", "--action", name, "--analysis-json", json,
                             "--window-index", "0", "--tab-index", "1" } );
        INFO( name );
        REQUIRE_FALSE( cli.parse_error );
        REQUIRE( cli.commander_request );
        const auto restored = commanderRequestFromVariantMap(
            commanderRequestToVariantMap( *cli.commander_request ) );
        REQUIRE( restored );
        CHECK( commanderActionToString( restored->action ) == name );
        CHECK( isCommanderAnalysisAction( restored->action ) );
        CHECK( restored->analysisArguments == cli.commander_request->analysisArguments );
        CHECK( restored->definitionPayload == cli.commander_request->definitionPayload );
        CHECK( restored->windowIndex == 0 );
        CHECK( restored->tabIndex == 1 );
    }
    for ( const auto& name : { "show_value_counts", "get_value_counts" } ) {
        CliParameters cli( { "cilogg", "command", "--action", name, "--window-index", "0" } );
        REQUIRE_FALSE( cli.parse_error );
        REQUIRE( cli.commander_request );
        CHECK( commanderActionToString( cli.commander_request->action ) == name );
    }
    CHECK( CliParameters(
               { "cilogg", "command", "--action", "value_counts", "--analysis-json", "[]" } )
               .parse_error );
}
