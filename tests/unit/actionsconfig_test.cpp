#include <catch2/catch.hpp>

#include "actionsconfig.h"

TEST_CASE( "Response definition roundtrips linked action steps", "[actionsconfig]" )
{
    ResponseDefinition response;
    response.id = 11;
    response.name = QStringLiteral( "ready" );
    response.match.type = ResponseMatchType::String;
    response.match.value = QStringLiteral( "OK" );
    response.response.steps = { { 101, 0 }, { 202, 125 } };
    response.response.hasActionId = true;
    response.response.actionId = 101;
    response.response.comment = QStringLiteral( "done" );

    const auto map = responseDefinitionToVariantMap( response );
    QString errorMessage;
    const auto roundTripped = responseDefinitionFromVariantMap( map, &errorMessage );

    REQUIRE( errorMessage.isEmpty() );
    REQUIRE( roundTripped.response.steps.size() == 2 );
    REQUIRE( roundTripped.response.steps.at( 0 ).actionId == 101 );
    REQUIRE( roundTripped.response.steps.at( 0 ).delayMs == 0 );
    REQUIRE( roundTripped.response.steps.at( 1 ).actionId == 202 );
    REQUIRE( roundTripped.response.steps.at( 1 ).delayMs == 125 );
}

TEST_CASE( "Response definition migrates legacy single action id to first step", "[actionsconfig]" )
{
    QVariantMap responseAction;
    responseAction.insert( QStringLiteral( "action_id" ), 77 );
    responseAction.insert( QStringLiteral( "comment" ), QStringLiteral( "legacy" ) );

    QVariantMap match;
    match.insert( QStringLiteral( "type" ), responseMatchTypeToString( ResponseMatchType::String ) );
    match.insert( QStringLiteral( "value" ), QStringLiteral( "READY" ) );

    QVariantMap map;
    map.insert( QStringLiteral( "id" ), 5 );
    map.insert( QStringLiteral( "name" ), QStringLiteral( "legacy response" ) );
    map.insert( QStringLiteral( "description" ), QStringLiteral( "compat" ) );
    map.insert( QStringLiteral( "match" ), match );
    map.insert( QStringLiteral( "response" ), responseAction );

    QString errorMessage;
    const auto response = responseDefinitionFromVariantMap( map, &errorMessage );

    REQUIRE( errorMessage.isEmpty() );
    REQUIRE( response.response.steps.size() == 1 );
    REQUIRE( response.response.steps.front().actionId == 77 );
    REQUIRE( response.response.steps.front().delayMs == 0 );
    REQUIRE( response.response.hasActionId );
    REQUIRE( response.response.actionId == 77 );
}

TEST_CASE( "Response definition rejects negative step delay", "[actionsconfig]" )
{
    ResponseDefinition response;
    response.name = QStringLiteral( "invalid" );
    response.match.type = ResponseMatchType::String;
    response.match.value = QStringLiteral( "BAD" );
    response.response.steps = { { 42, -1 } };
    response.response.hasActionId = true;
    response.response.actionId = 42;

    QString errorMessage;
    REQUIRE_FALSE( validateResponseDefinition( response, &errorMessage ) );
    REQUIRE( errorMessage.contains( "delay" ) );
}
