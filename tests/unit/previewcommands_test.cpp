// SPDX-License-Identifier: GPL-3.0-or-later
#include "commander.h"
#include <catch2/catch.hpp>

TEST_CASE( "Preview messages use Commander names and preserve their structured request",
           "[commander][previewcommands]" )
{
    const auto action = commanderActionFromString( "preview_message" );
    REQUIRE( action );
    CHECK( commanderActionToString( *action ) == "preview_message" );
    CHECK( isCommanderAnalysisAction( *action ) );
    CommanderRequest request;
    request.action = *action;
    request.tabId = "source-tab";
    request.analysisArguments
        = { { "line", 7 }, { "previewName", "Example" }, { "reassemble", true } };
    QString error;
    const auto decoded
        = commanderRequestFromVariantMap( commanderRequestToVariantMap( request ), &error );
    INFO( error.toStdString() );
    REQUIRE( decoded );
    CHECK( decoded->action == request.action );
    CHECK( decoded->tabId == request.tabId );
    CHECK( decoded->analysisArguments == request.analysisArguments );
}
